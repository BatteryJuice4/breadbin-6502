#pragma once
/*
    Game packs: game.toml (plus optional icon.png / banner.png) describing one game.
    See docs/gamepack.md for the format. Without a game pack the launcher runs in
    generic mode (any cartridge, built-in defaults).

    Parsing is pure (no file access) so it can be unit-tested and so packs can come
    from a folder or be embedded in a branded executable.
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAMEPACK_MAX_SHA1 16
#define GAMEPACK_ID_MAX 64

typedef enum {
    GAMEPACK_MEDIA_CARTRIDGE = 0,
    GAMEPACK_MEDIA_DISK,
    GAMEPACK_MEDIA_TAPE,
} gamepack_media_t;

typedef enum {
    GAMEPACK_CART_AUTO = 0,     // whatever the .crt header says
    GAMEPACK_CART_8K,
    GAMEPACK_CART_16K,
    GAMEPACK_CART_ULTIMAX,
} gamepack_cart_t;

typedef struct {
    char id[GAMEPACK_ID_MAX];       // [a-z0-9_-], used for the settings folder
    char title[128];
    gamepack_media_t media;
    bool ntsc;                      // video_standard = "NTSC"
    int sid_model;                  // 6581 or 8580
    char min_launcher_version[32];  // "" = any
    bool needs_original_roms;       // system_roms = "original"
    // [rom]
    gamepack_cart_t cart_type;
    uint16_t bin_load_address;      // raw .bin dumps; 0 = $8000
    int known_sha1_count;
    char known_sha1[GAMEPACK_MAX_SHA1][41];     // lowercase hex, ROM chip data only
    // [load] (disk and tape games)
    char load_file[17];             // file to LOAD; "" = the first one
    int load_done_pc;               // loading is over when this PC runs; -1 = not set
    int load_done_frames;           // ... or after this many frames; 0 = not set
    // [video] (display only)
    char palette[72];               // palette id; also the default for settings
    int extended_view_max_width;    // 0 = unsupported
} gamepack_t;

// Parse game.toml. Pack defaults ([video], [input.default], joystick_port) are applied
// on top of *defaults (pass settings_default() values; may be NULL).
// Returns false with err set when the pack is unusable (syntax error, missing or
// invalid id/title). Unknown keys and bad optional values only add warnings.
bool gamepack_parse(const char* text, size_t len, gamepack_t* out, settings_t* defaults,
                    char* err, size_t err_size, char* warnings, size_t warnings_size);

bool gamepack_id_valid(const char* id);

// Compare "major.minor.patch" versions (missing parts count as 0, a "-suffix" is ignored).
// Returns <0, 0, >0 like strcmp; *ok is set false if either string isn't a version.
int gamepack_version_compare(const char* a, const char* b, bool* ok);

// True if launcher_version satisfies min_launcher_version.
bool gamepack_launcher_ok(const gamepack_t* p, const char* launcher_version);

// True if sha1 (40 hex chars, any case) is one of the known dumps.
bool gamepack_knows_sha1(const gamepack_t* p, const char* sha1);

const char* gamepack_media_name(gamepack_media_t m);
const char* gamepack_cart_name(gamepack_cart_t c);

#ifdef __cplusplus
}
#endif
