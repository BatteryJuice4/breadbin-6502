// Breadbin-6502: sokol_app entry point, app state and frame loop.
//
// Frame loop: poll input -> (mod hooks, later) -> run one C64 frame ->
// (post-frame hooks, later) -> upload video -> audio is pushed from inside the
// emulation -> UI overlay.
//
// Wall-clock time decides only *when* the next emulated frame runs; each frame
// always executes the same fixed number of CPU cycles, so emulation stays
// deterministic. The emulation is paused whenever a UI screen is shown.
#include "audio/audio.h"
#include "core/core.h"
#include "input/input.h"
#include "platform/filedialog.h"
#include "platform/paths.h"
#include "rom/rom_file.h"
#include "rom/sysroms.h"
#include "settings/settings.h"
#include "ui/ui.h"
#include "video/video.h"

#include "sokol_app.h"
#include "sokol_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BB_VERSION
#define BB_VERSION "dev"
#endif

#define BB_GAME_ID "generic"            // game packs (Phase 4) supply their own id
#define BB_MAX_FRAMES_PER_TICK 4        // catch-up limit after a hitch
#define BB_MAX_ACCUM_SEC 0.25

static struct {
    core_t* core;
    sysroms_t roms;
    settings_t settings;
    char settings_path[SETTINGS_PATH_MAX];
    bool settings_dirty;
    bool settings_corrupt;              // keep a backup before the first save
    ui_state_t ui;
    double frame_period;
    double accum;
    long quit_after_frames;             // --frames N (0 = run until closed)
    int scale_override;                 // --scale N (this launch only)
    const char* sysrom_dir;
    const char* cli_cart;
    const char* cli_settings;
    char title[128];
    bool window_ready;
} app;

static void update_title(void) {
    if (app.window_ready) sapp_set_window_title(app.title);
}

// ---- settings --------------------------------------------------------------

static void save_settings(void) {
    if (!app.settings_dirty || !app.settings_path[0]) {
        return;
    }
    if (app.settings_corrupt) {
        char bak[SETTINGS_PATH_MAX + 8];
        snprintf(bak, sizeof(bak), "%s.bad", app.settings_path);
        platform_replace_file(app.settings_path, bak);
        fprintf(stderr, "breadbin-6502: kept the unreadable settings file as %s\n", bak);
        app.settings_corrupt = false;
    }
    char err[512];
    if (settings_save_file(&app.settings, app.settings_path, err, sizeof(err))) {
        app.settings_dirty = false;
    } else {
        fprintf(stderr, "breadbin-6502: %s\n", err);
    }
}

static void load_settings(void) {
    settings_default(&app.settings);
    if (app.cli_settings) {
        snprintf(app.settings_path, sizeof(app.settings_path), "%s", app.cli_settings);
    } else if (!platform_settings_path(BB_GAME_ID, app.settings_path, sizeof(app.settings_path))) {
        fprintf(stderr, "breadbin-6502: no config directory; settings won't be saved\n");
        app.settings_path[0] = 0;
        return;
    }
    bool missing = false;
    char err[512], warns[2048];
    if (!settings_load_file(&app.settings, app.settings_path, &missing, err, sizeof(err), warns, sizeof(warns))) {
        if (!missing) {
            fprintf(stderr, "breadbin-6502: settings ignored, %s\n", err);
            app.settings_corrupt = true;
        }
    }
    if (warns[0]) {
        fprintf(stderr, "breadbin-6502: settings warnings:\n%s", warns);
    }
}

// ---- cartridge -------------------------------------------------------------

static void set_message(bool is_error, const char* fmt, const char* arg) {
    snprintf(app.ui.message, sizeof(app.ui.message), fmt, arg);
    app.ui.message_is_error = is_error;
}

static void enter_game(void) {
    app.ui.screen = UI_SCREEN_NONE;
    app.ui.capture_action = UI_CAPTURE_NONE;
    app.accum = 0.0;
    input_release_all();
}

// Load and insert a cartridge. remember: store the path in the settings.
static bool load_cart(const char* path, bool remember) {
    static cart_t cart;     // ~16 KB, keep off the stack
    char err[256];
    if (!rom_load_cart(path, 0, &cart, err, sizeof(err))) {
        set_message(true, "%s", err);
        fprintf(stderr, "breadbin-6502: %s\n", err);
        return false;
    }
    core_insert_cart(app.core, &cart);
    ui_state_t* u = &app.ui;
    u->cart_loaded = true;
    snprintf(u->rom_path, sizeof(u->rom_path), "%s", path);
    if (cart.name[0]) {
        snprintf(u->rom_name, sizeof(u->rom_name), "%s", cart.name);
    } else {
        // many .crt files have an empty name: use the file name without extension
        const char* base = path;
        for (const char* p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
        snprintf(u->rom_name, sizeof(u->rom_name), "%s", base);
        char* dot = strrchr(u->rom_name, '.');
        if (dot && dot != u->rom_name) *dot = 0;
    }
    snprintf(u->rom_sha1, sizeof(u->rom_sha1), "%s", cart.sha1);
    u->rom_mode = cart_mode_name(cart.mode);
    u->rom_status = ROM_STATUS_GENERIC;     // known-hash checks arrive with game packs
    u->message[0] = 0;
    fprintf(stderr, "breadbin-6502: cartridge \"%s\" (%s, sha1 %s)\n",
            u->rom_name, cart_mode_name(cart.mode), cart.sha1);
    snprintf(app.title, sizeof(app.title), "Breadbin-6502 - %s", u->rom_name);
    update_title();
    if (remember && strcmp(app.settings.rom_path, path) != 0) {
        snprintf(app.settings.rom_path, sizeof(app.settings.rom_path), "%s", path);
        app.settings_dirty = true;
        save_settings();
    }
    enter_game();
    return true;
}

static void start_without_cart(void) {
    core_remove_cart(app.core);
    app.ui.cart_loaded = false;
    app.ui.rom_status = ROM_STATUS_NONE;
    app.ui.rom_path[0] = app.ui.rom_name[0] = app.ui.rom_sha1[0] = 0;
    snprintf(app.title, sizeof(app.title), "Breadbin-6502");
    update_title();
    enter_game();
}

static void select_rom(void) {
    char dir[SETTINGS_PATH_MAX] = { 0 };
    // start in the folder of the current/last ROM
    const char* last = app.ui.rom_path[0] ? app.ui.rom_path : app.settings.rom_path;
    snprintf(dir, sizeof(dir), "%s", last);
    char* sep = strrchr(dir, platform_path_sep());
    if (sep) *sep = 0; else dir[0] = 0;

    char path[SETTINGS_PATH_MAX], err[256];
    switch (platform_open_rom_dialog(dir, path, sizeof(path), err, sizeof(err))) {
        case FILEDIALOG_OK:
            load_cart(path, true);
            break;
        case FILEDIALOG_CANCEL:
            break;
        case FILEDIALOG_ERROR:
            if (platform_native_dialog_available()) {
                fprintf(stderr, "breadbin-6502: %s; using the built-in browser\n", err);
            }
            ui_open_file_browser(dir[0] ? dir : NULL);
            break;
    }
    app.accum = 0.0;    // the dialog blocked the frame loop
}

// ---- UI actions ------------------------------------------------------------

static void handle_ui_actions(void) {
    ui_state_t* u = &app.ui;
    if (u->act_video_changed) {
        video_set_settings(&app.settings.video);
        if (app.settings.video.fullscreen != sapp_is_fullscreen()) sapp_toggle_fullscreen();
        app.settings_dirty = true;
    }
    if (u->act_audio_changed) {
        audio_set_settings(&app.settings.audio);
        app.settings_dirty = true;
    }
    if (u->act_input_changed) {
        input_set_config(&app.settings.input);
        app.settings_dirty = true;
    }
    if (u->act_resume) {
        u->screen = u->menu_return;
        u->capture_action = UI_CAPTURE_NONE;
        if (u->screen == UI_SCREEN_NONE) enter_game();
        save_settings();
    }
    if (u->act_reset) {
        core_reset(app.core);
        enter_game();
    }
    if (u->act_no_cart) start_without_cart();
    if (u->act_load_path) load_cart(u->load_path, true);
    if (u->act_select_rom) select_rom();
    if (u->act_quit) {
        save_settings();
        sapp_request_quit();
    }
    u->act_resume = u->act_reset = u->act_quit = u->act_select_rom = u->act_no_cart = false;
    u->act_video_changed = u->act_audio_changed = u->act_input_changed = u->act_load_path = false;
}

// ---- sokol callbacks -------------------------------------------------------

static void init_cb(void) {
    video_init();
    video_set_settings(&app.settings.video);
    ui_init();
    app.window_ready = true;
    update_title();
}

static void frame_cb(void) {
    // a window manager may toggle fullscreen behind our back
    if (sapp_is_fullscreen() != app.settings.video.fullscreen) {
        app.settings.video.fullscreen = sapp_is_fullscreen();
        app.settings_dirty = true;
    }

    ui_frame(&app.ui);
    handle_ui_actions();

    const bool playing = app.ui.screen == UI_SCREEN_NONE;
    if (playing) {
        app.accum += sapp_frame_duration();
        if (app.accum > BB_MAX_ACCUM_SEC) app.accum = BB_MAX_ACCUM_SEC;
        int n = 0;
        while (app.accum >= app.frame_period && n < BB_MAX_FRAMES_PER_TICK) {
            input_apply(app.core);
            core_run_frame(app.core);
            app.accum -= app.frame_period;
            n++;
        }
    }
    sapp_show_mouse(!playing);

    // Show the machine while playing or behind the menu; nothing behind the Select-ROM screen.
    const bool show_machine = playing || app.ui.menu_return == UI_SCREEN_NONE;
    const core_video_t fb = core_video(app.core);
    video_draw(show_machine && app.ui.screen != UI_SCREEN_SELECT_ROM ? &fb : NULL, !playing);
    ui_flush();
    video_present(ui_draw);

    if (app.quit_after_frames > 0 && (long)sapp_frame_count() >= app.quit_after_frames) {
        sapp_request_quit();
    }
}

static void event_cb(const sapp_event* ev) {
    ui_state_t* u = &app.ui;
    const bool capturing = u->capture_action != UI_CAPTURE_NONE;

    if (ev->type == SAPP_EVENTTYPE_FILES_DROPPED) {
        if (sapp_get_num_dropped_files() > 0) load_cart(sapp_get_dropped_file_path(0), true);
        return;
    }
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN && !ev->key_repeat && !capturing) {
        if (ev->key_code == SAPP_KEYCODE_F11) {
            sapp_toggle_fullscreen();
            return;
        }
        if ((int)ev->key_code == app.settings.input.menu_key) {
            if (u->screen == UI_SCREEN_NONE) {
                u->menu_return = UI_SCREEN_NONE;
                u->screen = UI_SCREEN_MENU;
                u->message[0] = 0;
                input_release_all();
            } else if (u->screen == UI_SCREEN_MENU) {
                u->act_resume = true;   // handled next frame, like the button
            }
            return;
        }
    }
    if (ui_handle_event(ev, u)) {
        return;
    }
    input_handle_event(ev);
}

static void cleanup_cb(void) {
    save_settings();
    ui_shutdown();
    core_destroy(app.core);
    audio_shutdown();
    video_shutdown();
}

// ---- startup ---------------------------------------------------------------

static void usage(void) {
    fprintf(stderr,
        "Breadbin-6502 " BB_VERSION "\n"
        "usage: breadbin-6502 [options] [cartridge.crt|cartridge.bin]\n"
        "  A cartridge given here is played without being remembered. Use the\n"
        "  launcher's Select ROM (or drag a file onto the window) to remember one.\n"
        "  --sysroms DIR     load real C64 system ROMs (kernal.rom, basic.rom, chargen.rom) from DIR\n"
        "                    default: <config dir>/roms/system, falling back to the bundled Open ROMs\n"
        "  --settings FILE   use FILE instead of the per-user settings file\n"
        "  --scale N         window scale for this launch (1-8)\n"
        "  --frames N        quit after N displayed frames (testing)\n");
}

static bool parse_args(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        const char* v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "--sysroms") && v)       { app.sysrom_dir = v; i++; }
        else if (!strcmp(a, "--settings") && v) { app.cli_settings = v; i++; }
        else if (!strcmp(a, "--scale") && v)    { app.scale_override = atoi(v); i++; }
        else if (!strcmp(a, "--frames") && v)   { app.quit_after_frames = atol(v); i++; }
        else if (a[0] != '-' && !app.cli_cart)  { app.cli_cart = a; }
        else { usage(); return false; }
    }
    if (app.scale_override < 0 || app.scale_override > 8) app.scale_override = 0;
    return true;
}

static void load_system_roms(void) {
    char dir[1024] = { 0 };
    const char* rom_dir = app.sysrom_dir;
    if (!rom_dir && platform_system_rom_dir(dir, sizeof(dir))) {
        rom_dir = dir;
    }
    char err[512];
    if (!sysroms_load(&app.roms, rom_dir, err, sizeof(err))) {
        fprintf(stderr, "breadbin-6502: %s\n", err);
    }
    snprintf(app.ui.system_roms, sizeof(app.ui.system_roms), "%s%s",
             sysroms_source_name(app.roms.kernal_basic_source),
             app.roms.chargen_source != app.roms.kernal_basic_source ? " (custom CHARGEN)" : "");
}

sapp_desc sokol_main(int argc, char* argv[]) {
    if (!parse_args(argc, argv)) {
        exit(2);
    }
    load_settings();
    load_system_roms();
    if (!audio_init(app.settings.audio.latency_ms)) {
        fprintf(stderr, "breadbin-6502: no audio device, running silently\n");
    }
    audio_set_settings(&app.settings.audio);
    app.core = core_create(&(core_desc_t){
        .video_std = CORE_VIDEO_PAL,
        .rom_kernal = app.roms.kernal,   .rom_kernal_size = sizeof(app.roms.kernal),
        .rom_basic = app.roms.basic,     .rom_basic_size = sizeof(app.roms.basic),
        .rom_chargen = app.roms.chargen, .rom_chargen_size = sizeof(app.roms.chargen),
        .audio_sample_rate = audio_sample_rate(),
        .audio_fn = audio_push,
    });
    if (!app.core) {
        fprintf(stderr, "breadbin-6502: failed to create emulator core\n");
        exit(1);
    }
    app.frame_period = 1.0 / core_frame_rate(app.core);
    input_init(&app.settings.input);

    ui_state_t* u = &app.ui;
    u->settings = &app.settings;
    u->game_title = "Breadbin-6502";
    u->version = BB_VERSION;
    u->allow_no_cart = true;            // generic mode
    u->capture_action = UI_CAPTURE_NONE;
    u->startup_window_scale = app.settings.video.window_scale;
    u->screen = UI_SCREEN_SELECT_ROM;
    u->menu_return = UI_SCREEN_SELECT_ROM;
    snprintf(app.title, sizeof(app.title), "Breadbin-6502");

    // Cartridge to start with: command line (not remembered) > remembered ROM > Select-ROM screen.
    // (The window doesn't exist yet; init_cb applies the title.)
    if (app.cli_cart) {
        load_cart(app.cli_cart, false);
    } else if (app.settings.rom_path[0]) {
        if (platform_file_exists(app.settings.rom_path)) {
            load_cart(app.settings.rom_path, false);
        } else {
            set_message(true, "The last ROM wasn't found: %s", app.settings.rom_path);
        }
    }
    if (u->screen == UI_SCREEN_NONE) u->menu_return = UI_SCREEN_NONE;

    video_settings_t vs = app.settings.video;
    if (app.scale_override) vs.window_scale = app.scale_override;
    const core_video_t fb = core_video(app.core);
    int w, h;
    video_window_size(&fb, &vs, &w, &h);
    return (sapp_desc){
        .init_cb = init_cb,
        .frame_cb = frame_cb,
        .event_cb = event_cb,
        .cleanup_cb = cleanup_cb,
        .width = w,
        .height = h,
        .fullscreen = app.settings.video.fullscreen,
        .window_title = app.title,
        .enable_dragndrop = true,
        .max_dropped_files = 1,
        .enable_clipboard = true,
        .logger.func = slog_func,
    };
}
