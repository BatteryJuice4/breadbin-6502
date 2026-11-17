#pragma once
/*
    The mods available for the current game, which of them are enabled (in load
    order), whether each can actually run, and the runtime hooks:
      - patch mods change the game data when it's loaded
      - cheat mods write RAM before every frame ("freeze") or once after loading
      - palette mods add palettes to the Video menu

    Mods come from two places:
      installed  <config>/mods/<game id>/<mod id>.c64mod  (Mods menu: Install...)
      bundled    a folder game pack's mods/ folder (.c64mod files or unpacked folders)

    A mod can run only if it parsed, is for this game, the launcher is new enough,
    the loaded game is one it was made for (requires_sha1), its dependencies are
    enabled and usable, nothing enabled conflicts with it, any trusted plugins it
    asks for are built in, and its type is supported. Otherwise status explains why.
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/core.h"
#include "mods/mod.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODSET_MAX 64
#define MODSET_PATH_MAX 1024

typedef enum { MOD_SOURCE_INSTALLED = 0, MOD_SOURCE_BUNDLED } mod_source_t;

typedef struct {
    mod_t mod;                  // valid when loaded
    bool loaded;
    char file_id[MOD_ID_MAX];   // id from mod.toml, or the file name if it didn't load
    mod_source_t source;
    char path[MODSET_PATH_MAX];
    char sha1[41];              // of the package file (folders: of mod.toml); for replays
    bool enabled;
    bool usable;                // enabled and able to run
    bool failed;                // a patch failed to apply at load time
    char status[320];           // why it can't run / notes ("" = fine)
} mod_entry_t;

typedef struct {
    int count;
    mod_entry_t* entries[MODSET_MAX];
    int order_count;                    // enabled mods in load order (indices into entries)
    int order[MODSET_MAX];
    char game_id[MOD_ID_MAX];
    char launcher_version[32];
    char rom_sha1[41];
    char scan_errors[1024];             // problems while scanning (e.g. too many mods)
} modset_t;

void modset_init(modset_t* s, const char* game_id, const char* launcher_version);
void modset_free(modset_t* s);
// Scan installed and bundled folders (either may be NULL or missing). Replaces the list.
void modset_scan(modset_t* s, const char* installed_dir, const char* bundled_dir);
// Add one mod directly (tests, or a package already in memory). Returns the entry or NULL.
mod_entry_t* modset_add_package(modset_t* s, const uint8_t* data, size_t size, const char* path, mod_source_t src);

// Enabled ids in load order (from settings). Unknown ids are dropped.
void modset_set_enabled(modset_t* s, const char (*ids)[MOD_ID_MAX], int count);
void modset_enable(modset_t* s, int index, bool on);   // appends to / removes from the load order
void modset_move(modset_t* s, int order_pos, int delta); // move within the load order
// Recompute usable/status for the current game data hash ("" = nothing loaded).
void modset_evaluate(modset_t* s, const char* rom_sha1);
int modset_find(const modset_t* s, const char* id);

// Runtime. Patches: usable patch mods in load order; a mod that fails to apply is
// marked failed (with the reason in status) and skipped. Returns how many applied.
int modset_apply_patches(modset_t* s, uint8_t* buf, size_t size);
bool modset_has_patches(const modset_t* s);     // any usable patch mod
// Cheats: call before every frame once loading is over; first = the first such frame.
void modset_apply_cheats(const modset_t* s, core_t* core, bool first);

// Install a .c64mod into installed_dir (as <mod id>.c64mod, replacing an older copy
// of the same mod). Checks the package and that it's for this game first.
bool modset_install(const char* installed_dir, const char* package_path, const char* game_id,
                    char* msg, size_t msg_size);
bool modset_uninstall(modset_t* s, int index, char* msg, size_t msg_size);

#ifdef __cplusplus
}
#endif
