#pragma once
/*
    Mods: a mod.toml manifest plus data files, as a .c64mod package (zip) or an
    unpacked folder (game pack bundles and development). A mod never contains
    native code; native behaviour can only come from trusted plugins compiled into
    the executable, which a mod may ask for by name. See docs/mods.md.

    Types built so far:
      patch    byte patches applied in memory to the game data at load time:
               inline [[patch]] entries and/or IPS/BPS files
      cheat    RAM writes, every frame ("freeze") or once after loading
      palette  a 16-colour palette the player can pick in Video
      script   a sandboxed Lua script run with every frame (mods/script.h)

    Parsing is pure: files come through a reader callback, so the same code
    serves zips, folders and tests. Anything invalid makes the mod unusable with
    a reason; a broken mod never stops the launcher.
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOD_ID_MAX 64
#define MOD_MAX_LIST 16         // sha1s, dependencies, conflicts, plugins
#define MOD_MAX_PATCHES 256
#define MOD_MAX_PATCH_BYTES 256
#define MOD_MAX_FILES 8         // IPS/BPS files per mod
#define MOD_MAX_CHEATS 64
#define MOD_MAX_CHEAT_BYTES 16
#define MOD_MAX_SCRIPT (256 * 1024)     // bytes of Lua source

typedef enum {
    MOD_TYPE_PATCH = 0,
    MOD_TYPE_CHEAT,
    MOD_TYPE_PALETTE,
    MOD_TYPE_SCRIPT,
} mod_type_t;

typedef struct {
    char id[MOD_ID_MAX];
    char min_version[32];       // "" = any version
} mod_dep_t;

typedef struct {
    uint32_t offset;            // into the game data, as hashed (see docs/mods.md)
    uint16_t len;
    uint8_t bytes[MOD_MAX_PATCH_BYTES];
    bool has_expect;            // the original bytes must match, or the mod refuses to apply
    uint8_t expect[MOD_MAX_PATCH_BYTES];
} mod_patch_t;

typedef enum { MOD_FILE_IPS, MOD_FILE_BPS } mod_file_kind_t;

typedef struct {
    char name[64];
    mod_file_kind_t kind;
    uint8_t* data;              // owned
    size_t size;
} mod_file_t;

typedef struct {
    char label[64];
    uint16_t address;
    uint8_t len;
    uint8_t bytes[MOD_MAX_CHEAT_BYTES];
    bool freeze;                // true: every frame; false: once, after loading
} mod_cheat_t;

typedef struct {
    // manifest
    char id[MOD_ID_MAX];
    char name[96];
    char version[32];
    char author[96];
    char description[512];
    char game[MOD_ID_MAX];
    char min_launcher_version[32];
    mod_type_t type;
    int requires_count;
    char requires_sha1[MOD_MAX_LIST][41];   // empty = any dump
    int dep_count;
    mod_dep_t deps[MOD_MAX_LIST];
    int conflict_count;
    char conflicts[MOD_MAX_LIST][MOD_ID_MAX];
    int plugin_count;
    char plugins[MOD_MAX_LIST][MOD_ID_MAX];
    // patch
    int patch_count;
    mod_patch_t* patches;       // owned (malloc'd, patch_count entries)
    int file_count;
    mod_file_t files[MOD_MAX_FILES];
    // cheat
    int cheat_count;
    mod_cheat_t cheats[MOD_MAX_CHEATS];
    // palette
    char palette_name[64];
    uint32_t palette[16];       // 0xRRGGBB
    // script
    char script_name[64];       // file in the mod ("main.lua" unless mod.toml says otherwise)
    char* script;               // owned, NUL-terminated Lua source
    size_t script_size;
} mod_t;

// Reads a file from the mod (path relative to the mod root). Returns a malloc'd
// buffer or NULL (with err set).
typedef uint8_t* (*mod_read_fn)(void* user, const char* path, size_t* size, char* err, size_t err_size);

// Parse a manifest. Returns false with err set when the mod is unusable.
// Unknown keys only add warnings.
bool mod_parse(const char* toml, size_t len, mod_read_fn read, void* user, mod_t* out,
               char* err, size_t err_size, char* warnings, size_t warnings_size);
// Load from a .c64mod package in memory (also checks that it holds only data files).
bool mod_load_package(const uint8_t* data, size_t size, mod_t* out, char* err, size_t err_size,
                      char* warnings, size_t warnings_size);
// Load from an unpacked folder.
bool mod_load_dir(const char* dir, mod_t* out, char* err, size_t err_size, char* warnings, size_t warnings_size);
void mod_free(mod_t* m);

const char* mod_type_name(mod_type_t t);
bool mod_id_valid(const char* id);

#ifdef __cplusplus
}
#endif
