#pragma once
// Finding and loading a game pack: from a folder (game.toml, icon.png, banner.png)
// or from data embedded in a branded executable.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gamepack/gamepack.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t* toml;    size_t toml_size;      // size 0 = no embedded pack
    const uint8_t* icon;    size_t icon_size;
    const uint8_t* banner;  size_t banner_size;
} gamepack_embedded_t;

typedef struct {
    bool present;               // false = generic mode
    gamepack_t pack;
    char source[1024];          // folder, or "built in"
    uint8_t* icon_png;   size_t icon_size;     // malloc'd copies (NULL if none)
    uint8_t* banner_png; size_t banner_size;
} gamepack_loaded_t;

// Search order: cli_dir (when given), the embedded pack, then <exe dir>/gamepack/
// (the last two only when search_default is true).
// Returns true with present=false when there is no pack (generic mode), and false
// with err set when a pack exists but can't be used. Pack defaults are applied to
// *defaults.
bool gamepack_load(const char* cli_dir, const gamepack_embedded_t* embedded, bool search_default,
                   gamepack_loaded_t* out, settings_t* defaults,
                   char* err, size_t err_size, char* warnings, size_t warnings_size);
void gamepack_loaded_free(gamepack_loaded_t* lp);

#ifdef __cplusplus
}
#endif
