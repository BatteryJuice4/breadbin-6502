#pragma once
/*
    Per-user, per-game settings, stored as TOML in
    <config dir>/games/<game id>/settings.toml ("generic" without a game pack).

    Loading only overrides keys that are present and valid, so callers can
    layer: built-in defaults -> game pack defaults (Phase 4) -> user file.
    Invalid values are ignored (with a warning collected in `warnings`), never fatal.
*/
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_PATH_MAX 1024

typedef enum {
    VIDEO_FILTER_NEAREST = 0,
    VIDEO_FILTER_LINEAR,
} video_filter_t;

typedef struct {
    int window_scale;           // 1..8, applied at window creation
    bool fullscreen;
    bool integer_scaling;
    bool aspect_correction;     // use the C64's non-square pixel aspect
    bool crop_border;           // show only the 320x200 display window
    video_filter_t filter;
} video_settings_t;

typedef struct {
    float volume;               // 0..1
    bool mute;
    int latency_ms;             // device buffer size, 20..250
} audio_settings_t;

typedef enum {
    INPUT_UP = 0,
    INPUT_DOWN,
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_FIRE,
    INPUT_ACTION_COUNT,
} input_action_t;

#define INPUT_MAX_BINDS 2

typedef struct {
    int keys[INPUT_ACTION_COUNT][INPUT_MAX_BINDS];  // host keycodes, 0 = unbound
    int joystick_port;                              // 1 or 2
    int menu_key;                                   // opens/closes the launcher menu
} input_settings_t;

typedef struct {
    char rom_path[SETTINGS_PATH_MAX];               // last selected cartridge ("" = none)
    video_settings_t video;
    audio_settings_t audio;
    input_settings_t input;
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

#ifdef __cplusplus
}
#endif
