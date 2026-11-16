#pragma once
/*
    Launcher UI (Dear ImGui, C++ inside, C API outside).

    The app owns ui_state_t, fills in what to show, lets the UI edit
    settings in place, and handles the act_* requests after ui_frame().
*/
#include <stdbool.h>

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
} rom_status_t;

#define UI_CAPTURE_NONE (-1)
#define UI_CAPTURE_MENU_KEY INPUT_ACTION_COUNT

typedef struct {
    ui_screen_t screen;
    ui_screen_t menu_return;    // where closing the menu goes (NONE or SELECT_ROM)
    settings_t* settings;       // edited in place
    const char* game_title;     // "Breadbin-6502" in generic mode
    const char* version;

    // current cartridge
    bool cart_loaded;
    char rom_path[SETTINGS_PATH_MAX];
    char rom_name[33];
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
    bool act_load_path;         // load_path chosen in the built-in browser
    char load_path[SETTINGS_PATH_MAX];

    // key capture for rebinding
    int capture_action;         // UI_CAPTURE_NONE, an input_action_t, or UI_CAPTURE_MENU_KEY
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

#ifdef __cplusplus
}
#endif
