// Breadbin-6502: sokol_app entry point and frame loop.
//
// Frame loop: poll input -> (mod hooks, later) -> run one C64 frame ->
// (post-frame hooks, later) -> upload video -> audio is pushed from inside the
// emulation -> (UI overlay, later).
//
// Wall-clock time decides only *when* the next emulated frame runs; each frame
// always executes the same fixed number of CPU cycles, so emulation stays
// deterministic.
#include "audio/audio.h"
#include "core/core.h"
#include "input/input.h"
#include "platform/paths.h"
#include "rom/sysroms.h"
#include "video/video.h"

#include "sokol_app.h"
#include "sokol_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BB_VERSION
#define BB_VERSION "dev"
#endif

#define BB_MAX_FRAMES_PER_TICK 4     // catch-up limit after a hitch
#define BB_MAX_ACCUM_SEC 0.25

static struct {
    core_t* core;
    sysroms_t roms;
    double frame_period;
    double accum;
    long quit_after_frames;     // --frames N (0 = run until closed)
    int scale;
    int joy_port;
    const char* sysrom_dir;
} app;

static void init_cb(void) {
    video_init();
}

static void frame_cb(void) {
    app.accum += sapp_frame_duration();
    if (app.accum > BB_MAX_ACCUM_SEC) {
        app.accum = BB_MAX_ACCUM_SEC;
    }
    int n = 0;
    while (app.accum >= app.frame_period && n < BB_MAX_FRAMES_PER_TICK) {
        input_apply(app.core);
        core_run_frame(app.core);
        app.accum -= app.frame_period;
        n++;
    }
    const core_video_t fb = core_video(app.core);
    video_draw(&fb);

    if (app.quit_after_frames > 0 && core_frame_count(app.core) >= (uint64_t)app.quit_after_frames) {
        sapp_request_quit();
    }
}

static void event_cb(const sapp_event* ev) {
    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN && !ev->key_repeat) {
        if (ev->key_code == SAPP_KEYCODE_F11) {
            sapp_toggle_fullscreen();
            return;
        }
        if (ev->key_code == SAPP_KEYCODE_ESCAPE) {
            return;     // reserved: opens the launcher menu (Phase 3)
        }
    }
    input_handle_event(ev);
}

static void cleanup_cb(void) {
    core_destroy(app.core);
    audio_shutdown();
    video_shutdown();
}

static void usage(void) {
    fprintf(stderr,
        "Breadbin-6502 " BB_VERSION "\n"
        "usage: breadbin-6502 [options]\n"
        "  --sysroms DIR   load real C64 system ROMs (kernal.rom, basic.rom, chargen.rom) from DIR\n"
        "                  default: <config dir>/roms/system, falling back to the bundled Open ROMs\n"
        "  --joyport N     joystick port for the keypad joystick (1 or 2, default 2)\n"
        "  --scale N       initial window scale (default 3)\n"
        "  --frames N      quit after N emulated frames (testing)\n");
}

static bool parse_args(int argc, char* argv[]) {
    app.scale = 3;
    app.joy_port = 2;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        const char* v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "--sysroms") && v)      { app.sysrom_dir = v; i++; }
        else if (!strcmp(a, "--joyport") && v) { app.joy_port = atoi(v); i++; }
        else if (!strcmp(a, "--scale") && v)   { app.scale = atoi(v); i++; }
        else if (!strcmp(a, "--frames") && v)  { app.quit_after_frames = atol(v); i++; }
        else { usage(); return false; }
    }
    if (app.scale < 1) app.scale = 1;
    if (app.scale > 8) app.scale = 8;
    if (app.joy_port != 1 && app.joy_port != 2) app.joy_port = 2;
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
    fprintf(stderr, "breadbin-6502: KERNAL/BASIC: %s, CHARGEN: %s\n",
            sysroms_source_name(app.roms.kernal_basic_source),
            sysroms_source_name(app.roms.chargen_source));
}

sapp_desc sokol_main(int argc, char* argv[]) {
    if (!parse_args(argc, argv)) {
        exit(2);
    }
    load_system_roms();
    if (!audio_init()) {
        fprintf(stderr, "breadbin-6502: no audio device, running silently\n");
    }
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
    input_init(app.joy_port);

    const core_video_t fb = core_video(app.core);
    int w, h;
    video_window_size(fb.screen_w, fb.screen_h, app.scale, &w, &h);
    return (sapp_desc){
        .init_cb = init_cb,
        .frame_cb = frame_cb,
        .event_cb = event_cb,
        .cleanup_cb = cleanup_cb,
        .width = w,
        .height = h,
        .window_title = "Breadbin-6502",
        .logger.func = slog_func,
    };
}
