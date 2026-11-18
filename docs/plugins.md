# Trusted plugins

Mods are data only: a `.c64mod` can never contain native code. When a game needs
something a cheat or a Lua script can't do (speed, or access to things scripts don't
get), its **game repo** can compile a plugin into the game's own executable. Players
can't add plugins; a mod can only switch a built-in one on and configure it.

Plugins are "trusted" because they're part of the game build you ship: review them
like any other code in your repo.

## Writing one

A plugin is one C file named after the plugin (`a-z`, `0-9`, `_`), defining a
`bb_plugin_t` called `bb_plugin_<name>`. The full interface is in
[`src/mods/plugin.h`](../src/mods/plugin.h); the example
[`examples/plugins/autofire.c`](../examples/plugins/autofire.c) is a complete one.

```c
#include "mods/plugin.h"

static void* start(core_t* core, const bb_plugin_config_t* cfg, char* err, size_t err_size) {
    // read settings, allocate state; return NULL (and fill err) to refuse
}
static void stop(void* self, core_t* core) { /* free state */ }
static void before_frame(void* self, const bb_plugin_frame_t* f) { /* f->joy can be changed */ }
static void after_frame(void* self, const bb_plugin_frame_t* f) { /* read/write RAM */ }

const bb_plugin_t bb_plugin_example = {
    .api = BB_PLUGIN_API, .name = "example", .description = "One line for the Mods tab",
    .start = start, .stop = stop, .before_frame = before_frame, .after_frame = after_frame,
};
```

Every hook is optional. `start` must return a non-NULL pointer when it succeeds (any
pointer, if the plugin has no state).

- **When hooks run:** `start` when the game starts, and again after every reset, save
  state load or change to the enabled mods (`stop` runs first). `before_frame` and
  `after_frame` run around every emulated frame, in the mods' load order, before script
  mods.
- **The machine:** use `core/core.h` only (`core_ram_read`, `core_ram_write`, write
  watches, ...). Never include the emulator's own headers: the core can be replaced.
- **Determinism:** replays re-run plugins, so a plugin must behave the same given the
  same machine and input. Don't use the clock, unseeded random numbers, files, threads or
  the network. Keep state in your struct (it restarts with the game) or in C64 RAM.
- **Display:** a plugin that only changes what's shown should stay display-only; one
  that changes game logic (for example spawning enemies in the extended view's extra
  space) belongs to that game's repo, never to the launcher.

## Building it in

In the game repo's `CMakeLists.txt`, before `add_subdirectory(external/breadbin-6502)`:

```cmake
set(BB_PLUGIN_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/plugins/example.c CACHE STRING "" FORCE)
```

The launcher's build compiles the files into the executable and generates the code
that registers them at startup. The Mods tab lists the built-in plugins.

## Switching it on from a mod

```toml
id      = "my-game-turbo"
name    = "Turbo fire"
version = "1.0.0"
game    = "my-game"
min_launcher_version = "0.5.0"
type    = "plugin"              # a mod that only switches plugins on
plugins = ["autofire"]

[plugin.autofire]               # settings for that plugin: strings, numbers, booleans
port = 2
rate = 4
```

Any mod type can list `plugins`; `type = "plugin"` is for mods that do nothing else. If
a plugin isn't built into the running game, the Mods tab says so and the mod can't be
enabled. If `start` refuses (for example a bad setting), the mod is switched off and the
Mods tab shows the plugin's message.

Plugins read settings with `bb_plugin_config_string/int/float/bool(cfg, "key",
default)`; a missing or unreadable value gives the default.
