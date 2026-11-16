#include "gamepack/gamepack_load.h"
#include "platform/paths.h"
#include "rom/rom_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAMEPACK_MAX_TOML (256 * 1024)
#define GAMEPACK_MAX_IMAGE (4 * 1024 * 1024)

static bool join(char* out, size_t size, const char* dir, const char* name) {
    int n = snprintf(out, size, "%s%c%s", dir, platform_path_sep(), name);
    return n > 0 && (size_t)n < size;
}

static uint8_t* dup_bytes(const uint8_t* src, size_t n) {
    uint8_t* p = malloc(n ? n : 1);
    if (p && n) memcpy(p, src, n);
    return p;
}

// Optional file: missing is fine, unreadable is a warning.
static void read_optional(const char* dir, const char* name, uint8_t** data, size_t* size,
                          char* warnings, size_t warnings_size) {
    char path[1100];
    if (!join(path, sizeof(path), dir, name) || !platform_file_exists(path)) return;
    char err[256];
    if (!rom_read_file(path, GAMEPACK_MAX_IMAGE, data, size, err, sizeof(err))) {
        size_t len = warnings ? strlen(warnings) : 0;
        if (warnings && len + 1 < warnings_size) {
            snprintf(warnings + len, warnings_size - len, "%s: %s\n", name, err);
        }
        *data = NULL;
        *size = 0;
    }
}

static bool load_from_dir(const char* dir, gamepack_loaded_t* out, settings_t* defaults,
                          char* err, size_t err_size, char* warnings, size_t warnings_size) {
    char path[1100];
    if (!join(path, sizeof(path), dir, "game.toml")) {
        snprintf(err, err_size, "game pack path too long");
        return false;
    }
    uint8_t* text;
    size_t size;
    char rerr[256];
    if (!rom_read_file(path, GAMEPACK_MAX_TOML, &text, &size, rerr, sizeof(rerr))) {
        snprintf(err, err_size, "%s", rerr);
        return false;
    }
    char* z = realloc(text, size + 1);     // tomlc17 wants a NUL-terminated buffer
    if (!z) {
        free(text);
        snprintf(err, err_size, "out of memory");
        return false;
    }
    z[size] = 0;
    bool ok = gamepack_parse(z, size, &out->pack, defaults, err, err_size, warnings, warnings_size);
    free(z);
    if (!ok) return false;
    snprintf(out->source, sizeof(out->source), "%s", dir);
    // warnings so far belong to game.toml; images append their own
    read_optional(dir, "icon.png", &out->icon_png, &out->icon_size, warnings, warnings_size);
    read_optional(dir, "banner.png", &out->banner_png, &out->banner_size, warnings, warnings_size);
    out->present = true;
    return true;
}

static bool load_embedded(const gamepack_embedded_t* e, gamepack_loaded_t* out, settings_t* defaults,
                          char* err, size_t err_size, char* warnings, size_t warnings_size) {
    char* z = malloc(e->toml_size + 1);
    if (!z) {
        snprintf(err, err_size, "out of memory");
        return false;
    }
    memcpy(z, e->toml, e->toml_size);
    z[e->toml_size] = 0;
    bool ok = gamepack_parse(z, e->toml_size, &out->pack, defaults, err, err_size, warnings, warnings_size);
    free(z);
    if (!ok) return false;
    snprintf(out->source, sizeof(out->source), "built in");
    if (e->icon_size) { out->icon_png = dup_bytes(e->icon, e->icon_size); out->icon_size = out->icon_png ? e->icon_size : 0; }
    if (e->banner_size) { out->banner_png = dup_bytes(e->banner, e->banner_size); out->banner_size = out->banner_png ? e->banner_size : 0; }
    out->present = true;
    return true;
}

bool gamepack_load(const char* cli_dir, const gamepack_embedded_t* embedded, bool search_default,
                   gamepack_loaded_t* out, settings_t* defaults,
                   char* err, size_t err_size, char* warnings, size_t warnings_size) {
    memset(out, 0, sizeof(*out));
    if (err && err_size) err[0] = 0;
    if (warnings && warnings_size) warnings[0] = 0;
    if (cli_dir) {
        if (!platform_is_dir(cli_dir)) {
            snprintf(err, err_size, "game pack folder not found: %s", cli_dir);
            return false;
        }
        return load_from_dir(cli_dir, out, defaults, err, err_size, warnings, warnings_size);
    }
    if (!search_default) return true;
    if (embedded && embedded->toml_size) {
        return load_embedded(embedded, out, defaults, err, err_size, warnings, warnings_size);
    }
    char exe[1024], dir[1100], toml[1200];
    if (platform_exe_dir(exe, sizeof(exe)) && join(dir, sizeof(dir), exe, "gamepack") &&
        join(toml, sizeof(toml), dir, "game.toml") && platform_file_exists(toml)) {
        return load_from_dir(dir, out, defaults, err, err_size, warnings, warnings_size);
    }
    return true;    // generic mode
}

void gamepack_loaded_free(gamepack_loaded_t* lp) {
    free(lp->icon_png);
    free(lp->banner_png);
    lp->icon_png = lp->banner_png = NULL;
    lp->icon_size = lp->banner_size = 0;
}
