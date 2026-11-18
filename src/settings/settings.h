#pragma once
/*
    Per-user, per-game settings, stored as TOML in
    <config dir>/games/<game id>/settings.toml ("generic" without a game pack).

    Loading only overrides keys that are present and valid, so callers can
    layer: built-in defaults -> game pack defaults (Phase 4) -> user file.
    Invalid values are ignored (with a warning collected in `warnings`), never fatal.
*/
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_PATH_MAX 1024
#define SETTINGS_MAX_MODS 64
#define SETTINGS_MOD_ID_MAX 64

typedef enum {
    VIDEO_FILTER_NEAREST = 0,
    VIDEO_FILTER_LINEAR,
    VIDEO_FILTER_CRT,       // scanlines + RGB mask (video/crt.c)
} video_filter_t;

typedef struct {
    int window_scale;           // 1..8, applied at window creation
    bool fullscreen;
    bool integer_scaling;
    bool aspect_correction;     // use the C64's non-square pixel aspect
    bool crop_border;           // show only the 320x200 display window
    video_filter_t filter;
    char palette[72];           // palette id (see video/palettes.h)
    bool extended_view;         // widen the cropped picture into the side borders
    int extended_max_width;     // from the game pack, not saved: 0 = no extended view
} video_settings_t;

typedef struct {
    float volume;               // 0..1
    bool mute;
    int latency_ms;             // device buffer size, 20..250
} audio_settings_t;

typedef struct {
    bool fast_load;             // with the 1541: plain KERNAL LOADs answered instantly
    bool drive_light;           // show the 1541's busy light over the picture
} disk_settings_t;

typedef enum {
    INPUT_UP = 0,
    INPUT_DOWN,
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_FIRE,
    INPUT_ACTION_COUNT,
} input_action_t;

#define INPUT_MAX_BINDS 4   // keyboard keys and gamepad buttons (INPUT_GAMEPAD_BASE + button)

typedef struct {
    int keys[INPUT_ACTION_COUNT][INPUT_MAX_BINDS];  // host keycodes, 0 = unbound
    int joystick_port;                              // 1 or 2
    int menu_key;                                   // opens/closes the launcher menu
    int quick_save_key, quick_load_key;             // save states, 0 = none
    uint8_t pad_c64_key[32];                        // C64 key each gamepad button presses (0 = none)
} input_settings_t;

typedef struct {
    char rom_path[SETTINGS_PATH_MAX];               // last selected cartridge ("" = none)
    video_settings_t video;
    audio_settings_t audio;
    input_settings_t input;
    disk_settings_t disk;
    int mods_enabled_count;                         // enabled mods, in load order
    char mods_enabled[SETTINGS_MAX_MODS][SETTINGS_MOD_ID_MAX];
} settings_t;

void settings_default(settings_t* s);

// Parse TOML text on top of the current values in *s.
// Returns false on a syntax error (s unchanged); warnings lists ignored values.
bool settings_parse(settings_t* s, const char* text, size_t len,
                    char* err, size_t err_size, char* warnings, size_t warnings_size);

// Serialize to TOML. Returns the number of bytes needed (excluding NUL), like snprintf.
size_t settings_serialize(const settings_t* s, char* buf, size_t cap);

// File helpers. load: returns false and sets *missing=true if the file doesn't exist.
bool settings_load_file(settings_t* s, const char* path, bool* missing,
                        char* err, size_t err_size, char* warnings, size_t warnings_size);
// Writes atomically (temp file + rename). Creates parent directories.
bool settings_save_file(const settings_t* s, const char* path, char* err, size_t err_size);

const char* settings_action_key(input_action_t a);   // TOML key: "up", "down", ...
// Give every action that has no gamepad button the default ones (d-pad + left
// stick, A/B for fire), in its free slots. For settings files from before gamepad
// support and game packs that only list keys.
void settings_add_default_gamepad(input_settings_t* in);

#ifdef __cplusplus
}
#endif
