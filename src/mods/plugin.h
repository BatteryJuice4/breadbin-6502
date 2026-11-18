#pragma once
/*
    Trusted plugins: native code that a game repo compiles into its own
    executable. Mods can't contain native code; a mod can only switch a built-in
    plugin on (`plugins = ["name"]`) and configure it (`[plugin.name]` in mod.toml).
    Guide: docs/plugins.md.

    Writing one: a file `name.c` (name: a-z, 0-9, _) defines

        const bb_plugin_t bb_plugin_name = { .api = BB_PLUGIN_API, .name = "name", ... };

    and the game repo's CMake lists it in BB_PLUGIN_SOURCES. The build generates
    the code that registers it at startup. Plugins talk to the machine through
    core/core.h only (never chips), and must be deterministic like everything in
    the frame loop: no clock, no unseeded randomness, no files.

    Hooks run in main.c's step_frame, in mod load order, before script mods:
      start          when the game starts (and after every reset / state load /
                     mod change); returns the plugin's state, or NULL with err set
      stop           when the mod stops (or before a restart)
      before_frame   before every frame; f->joy can still be changed
      after_frame    after every frame
*/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BB_PLUGIN_API 1
#define PLUGIN_MAX 32               // built-in plugins
#define PLUGIN_MAX_RUNNING 32       // instances at once (one per mod and plugin)

typedef struct mod_plugin_cfg mod_plugin_cfg_t;     // mods/mod.h

// A mod's settings for one plugin ([plugin.<name>] in mod.toml). Values are kept
// as text; these read them with a default for missing or unparsable values.
typedef struct {
    const mod_plugin_cfg_t* entries;
    int count;
    const char* plugin;
} bb_plugin_config_t;

const char* bb_plugin_config_string(const bb_plugin_config_t* c, const char* key, const char* def);
int64_t bb_plugin_config_int(const bb_plugin_config_t* c, const char* key, int64_t def);
double bb_plugin_config_float(const bb_plugin_config_t* c, const char* key, double def);
bool bb_plugin_config_bool(const bb_plugin_config_t* c, const char* key, bool def);

typedef struct {
    core_t* core;
    uint8_t* joy;               // before_frame: ports 1 and 2 (CORE_JOY_* bits), changeable; else NULL
} bb_plugin_frame_t;

typedef struct bb_plugin {
    int api;                    // BB_PLUGIN_API
    const char* name;           // as used in mod.toml
    const char* description;    // one line, shown in the Mods tab
    void* (*start)(core_t* core, const bb_plugin_config_t* config, char* err, size_t err_size);
    void (*stop)(void* self, core_t* core);
    void (*before_frame)(void* self, const bb_plugin_frame_t* f);
    void (*after_frame)(void* self, const bb_plugin_frame_t* f);
} bb_plugin_t;

// ---- registry (the executable registers its plugins at startup) ----------------
bool plugin_register(const bb_plugin_t* p);         // false: full, duplicate, bad name or API
const bb_plugin_t* plugin_find(const char* name);
int plugin_count(void);
const bb_plugin_t* plugin_get(int index);
void plugin_unregister_all(void);                   // tests
bool plugin_name_valid(const char* name);

// ---- running instances ----------------------------------------------------------
typedef struct pluginset pluginset_t;
struct mod;
pluginset_t* pluginset_create(void);
void pluginset_destroy(pluginset_t* s, core_t* core);
void pluginset_clear(pluginset_t* s, core_t* core);
// Start every plugin a mod asks for, with its config. On failure the mod's
// instances started so far are stopped again, and err says why.
bool pluginset_add_mod(pluginset_t* s, core_t* core, const struct mod* m, char* err, size_t err_size);
int pluginset_count(const pluginset_t* s);
void pluginset_before_frame(pluginset_t* s, core_t* core, uint8_t joy[2]);
void pluginset_after_frame(pluginset_t* s, core_t* core);

#ifdef __cplusplus
}
#endif
