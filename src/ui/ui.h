#pragma once
/*
    Launcher UI (Dear ImGui, C++ inside, C API outside).

    The app owns ui_state_t, fills in what to show, lets the UI edit
    settings in place, and handles the act_* requests after ui_frame().
*/
#include <stdbool.h>

#include "mods/modset.h"
#include "settings/settings.h"
#include "sokol_app.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_SCREEN_NONE = 0,         // playing
    UI_SCREEN_SELECT_ROM,       // first run / no cartridge
    UI_SCREEN_MENU,             // in-game menu (game paused)
} ui_screen_t;

typedef enum {
    ROM_STATUS_NONE = 0,        // nothing loaded
    ROM_STATUS_GENERIC,         // no game pack: any cartridge accepted
    ROM_STATUS_KNOWN,           // matches a known dump of the game pack
    ROM_STATUS_UNKNOWN,         // game pack present, hash not in its list
    ROM_STATUS_UNVERIFIED,      // game pack present but lists no known hashes
} rom_status_t;

#define UI_CAPTURE_NONE (-1)
#define UI_CAPTURE_MENU_KEY INPUT_ACTION_COUNT
#define UI_CAPTURE_QUICK_SAVE (INPUT_ACTION_COUNT + 1)
#define UI_CAPTURE_QUICK_LOAD (INPUT_ACTION_COUNT + 2)

#define UI_STATE_SLOTS 6        // slot 0 = quick save (hotkeys), 1-5 in the States tab

typedef struct {
    bool used;                  // a file exists
    bool loadable;              // it fits the running game (else note says why)
    char saved_at[32];
    char note[384];             // why it can't be loaded, or a read error
} ui_state_slot_t;

typedef struct {
    ui_screen_t screen;
    ui_screen_t menu_return;    // where closing the menu goes (NONE or SELECT_ROM)
    settings_t* settings;       // edited in place
    const settings_t* defaults; // built-in + game pack defaults ("Reset to defaults")
    const char* game_title;     // game pack title, "Breadbin-6502" in generic mode
    const char* version;

    // game pack (has_pack false = generic mode)
    bool has_pack;
    const char* pack_id;
    const char* pack_source;    // folder or "built in"
    char pack_notes[512];       // e.g. "expects NTSC, running PAL" (one per line)
    char blocked[1024];         // non-empty: the game can't start, explained here

    // what the game pack plays, for wording: "cartridge", "disk" or "tape" (NULL = generic: any)
    const char* pack_media;

    // current game media (cartridge, disk or tape)
    bool cart_loaded;           // any media loaded (name kept from the cartridge-only days)
    const char* media_kind;     // "Cartridge", "Disk" or "Tape"
    bool can_swap;              // disk/tape: offer "Swap disk/tape..."
    bool loading;               // disk/tape loading at warp speed (no picture yet)
    float loading_seconds;      // emulated loading time so far
    char rom_path[SETTINGS_PATH_MAX];
    char rom_name[96];
    char rom_sha1[41];
    const char* rom_mode;
    rom_status_t rom_status;
    char system_roms[64];       // "Open ROMs" / "user-supplied"

    char message[SETTINGS_PATH_MAX + 256];  // status or error text
    bool message_is_error;
    bool allow_no_cart;         // generic mode: offer "Start without cartridge"
    int startup_window_scale;   // to show "applies on next launch"

    // requests from the UI, handled and cleared by the app
    bool act_resume, act_reset, act_quit, act_select_rom, act_no_cart;
    bool act_video_changed, act_audio_changed, act_input_changed;
    bool act_swap_media;        // "Swap disk/tape..." (keeps the machine running)
    bool act_load_path;         // load_path chosen in the built-in browser
    char load_path[SETTINGS_PATH_MAX];

    // "this doesn't look like a known dump" confirmation
    bool confirm_open;
    char confirm_text[512];
    bool act_confirm_play, act_confirm_cancel;

    // mods
    modset_t* mods;             // the app's mod set (edited in place: enable, order)
    bool mods_restart_needed;   // patch mods changed since the game was loaded
    char mods_message[512];
    bool mods_message_is_error;
    bool act_mods_changed;      // enabled set or load order changed
    bool act_mod_install;       // "Install mod...": open the file dialog
    bool act_mod_install_path;  // load_path chosen in the built-in browser (mods mode)
    bool act_mod_uninstall;     // mod_index
    int mod_index;
    bool act_mods_rescan;
    bool act_mods_restart;      // reload the game so patch mods apply
    // diagnostic replays
    bool replay_recording, replay_playing;
    unsigned replay_frame, replay_total;    // frames recorded / played, total when playing
    float replay_fps;
    char replay_message[SETTINGS_PATH_MAX + 256];
    bool replay_message_is_error;
    bool act_replay_record, act_replay_stop;
    // save states
    bool states_ready;          // a game is running and has finished loading
    char states_unavailable[160];   // when not ready: why (shown in the States tab)
    ui_state_slot_t state_slots[UI_STATE_SLOTS];
    char states_message[SETTINGS_PATH_MAX + 256];
    bool states_message_is_error;
    bool act_state_save, act_state_load, act_state_delete;
    int state_slot;
    // short notice shown over the game (e.g. "Saved"), counted down in display frames
    char toast[160];
    int toast_frames;
    // key capture for rebinding
    int capture_action;         // UI_CAPTURE_NONE, an input_action_t, or UI_CAPTURE_MENU_KEY / _QUICK_*
    int capture_slot;
} ui_state_t;

void ui_init(void);
void ui_shutdown(void);
// Returns true if the UI consumed the event (always while a screen is shown).
bool ui_handle_event(const sapp_event* ev, ui_state_t* st);
// Build this frame's UI (call every frame; draws nothing when screen == NONE).
void ui_frame(ui_state_t* st);
// Call outside the render pass, after ui_frame().
void ui_flush(void);
// Overlay callback for video_present() (inside the render pass).
void ui_draw(void);
// Open the built-in file browser (fallback for the native dialog).
void ui_open_file_browser(const char* start_dir);
// Same, listing .c64mod packages (result: act_mod_install_path).
void ui_open_mod_browser(const char* start_dir);
// Game pack banner (RGBA8), shown on the Select-ROM screen and in the ROM tab.
// Call after ui_init(); pixels are copied.
void ui_set_banner(const unsigned char* rgba, int width, int height);

#ifdef __cplusplus
}
#endif
