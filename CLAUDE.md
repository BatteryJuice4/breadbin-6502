# CLAUDE.md: breadbin-6502

The generic C64 launcher (engine + menu + mod system). The full project guide lives
in the workspace `CLAUDE.md` one level up (`../CLAUDE.md`). Read it first, especially:
**Architecture**, **Supported media**, **System ROMs**, **chips limitations**,
**Cartridges**, **ROM handling**, **Mod system**, **Development phases**, **Testing**
and **Working conventions**.

Repo-specific rules:

- **No game-specific code here.** Gorf (or any game) lives in its own repo. Add generic,
  config-driven options instead.
- **Only `src/core/core_chips.c` includes chips headers.** It uses the patched copies in
  `src/core/patched/` (`c64.h`: expansion port, NTSC machine; `m6569.h`: NTSC timing;
  `c1530.h`: bigger tape buffer), not the originals in `external/chips/`. Changes in them
  are marked `BREADBIN PATCH`. Everything else goes through `src/core/core.h`.
- `core_t` (it holds a `c64_t`, framebuffer `alignas(64)`) needs 64-byte alignment: only
  allocate it through `core_alloc()` in `core_chips.c`, never plain `malloc`. CI's
  `ubuntu-latest` (26.04) compiles for x86-64-v3 (AVX2), where misalignment faults.
- Disk/tape media live in `src/media/` (pure C on top of `core.h`); `tests/test_media.c`
  builds its own test disks and tapes (shared builders in `tests/test_images.h`), so no
  third-party images are needed.
- 1541 drive: `src/core/patched/c1541.h` (the drive, rewritten from chips' stub) +
  the serial bus in the patched `c64.h`; disks reach the core as `core_disk_t` GCR tracks
  built by `src/media/gcr.c`. Only with the user's 1541 ROM (`sysroms`); `main.c`'s
  `use_drive()` decides (ROM present, pack `[load] drive`, `--no-drive`), then the trap
  leaves disks alone (`media_image_t.use_drive`), except plain KERNAL LOADs when fast
  loading is on (`media_image_t.fast_load`, served from the drive's current surface). Writes mark `core_disk_t.dirty`; the app
  keeps a save copy via `src/state/disksave.c` (never the user's file; not during
  replays). `tests/test_drive.c` needs `BB_TEST_ROM_DIR` with a 1541 ROM, else it's
  skipped (CI). The drive is part of determinism, save states (never its ROM) and replay
  fingerprints (its RAM).
- Recompilation readiness (workspace `docs/RECOMP_PLAN.md`): the machine cycle in the
  patched `c64.h` is split into `_c64_tick_devices` / CPU / `_c64_tick_bus`, so another
  CPU can drive the same chips (the contract is in the comment above it); keep new
  hardware work on the bus side of that seam. Coverage tracing: `core_coverage_*`
  (opcode fetches x memory configuration, hooked in `core_on_fetch`), files in
  `src/debug/coverage.c` (`.bbcov`), `--coverage DIR` in `main.c`, `tests/test_coverage.c`.
  Tracing only observes: never in states, replays or fingerprints. `generated/` is
  git-ignored (recompiled C is ROM-derived).
- Mods live in `src/mods/` (strict zip reader, `mod.toml` parser, inline/IPS/BPS
  patches, `modset` = validation, load order, cheats, install). Format: `docs/mods.md`.
  Packages are untrusted input: keep the size caps and the data-files-only rule.
- Script mods: `src/mods/script.c` (Lua host: sandbox, memory cap, instruction budget,
  `bb` API) on Lua 5.4 in `external/lua`, built only through `src/mods/lua_impl.c` (fixed
  string-hash seed; no io/os/package/debug compiled). Scripts run in `step_frame`
  (`on_frame` before, `on_after_frame` + `on_write` after) and restart whenever
  `cheats_first` is set. Write watches: `core_watch_write`. Anything that would let a
  script see the host (time, files, addresses) breaks the sandbox or determinism.
- Trusted plugins: `src/mods/plugin.h` (interface for game repos), `plugin.c` (registry,
  `[plugin.x]` config, instances). Game repos set `BB_PLUGIN_SOURCES`; CMake generates
  `plugins_registry.c` (from `cmake/plugins_registry.c.in`) for the executable only, so
  the launcher itself ships none. `examples/plugins/autofire.c` is the reference and is
  built into `test_plugins`. Plugins run before scripts in `step_frame`.
- Extended view: `src/video/view.c` (`video_view_rect`, pure and tested in
  `test_settings`) picks the shown rectangle; display only, the VIC still draws its fixed
  392-pixel screen, so emulation (sprite collisions included) never depends on it.
- Diagnostic replays live in `src/replay/`. Every emulated frame in the app goes
  through `step_frame()` in `main.c` (live or replayed input, cheats, autostart,
  fingerprint), so anything that affects emulation must happen there or at game
  load, never elsewhere, or replays stop matching. `--record FILE --headless --frames N`
  and `--replay FILE --verify` make and check replays without a window.
- Gamepads: `src/platform/gamepad*.c` (own code, no library): `gamepad_evdev.c` (Linux and
  FreeBSD, `<dev/evdev/input.h>`;
  pure `evpad_*` model tested in `tests/test_gamepad.c` with made-up devices, plus a
  device layer that rescans `/dev/input` about once a second), `gamepad_xinput.c`
  (Windows, XInput DLL loaded at run time; not compilable on the Linux dev machine, CI
  builds it), `gamepad_gc.m` (macOS GameController framework; Objective-C, only
  syntax-checked here against a stub header, CI compiles it), none elsewhere. Buttons go
  by position (Xbox layout, `GAMEPAD_A` = bottom face button). evdev pads with an odd
  button order get an entry in `known_pads` (USB vendor:product, `evpad_identify`); only
  add one checked on a real pad. Verified so far: DragonRise `0079:0011` (SNES-style). Buttons are bindable codes `INPUT_GAMEPAD_BASE + button`
  named `Gamepad_*`; `settings_add_default_gamepad` fills actions that list none.
- CRT filter: `src/video/crt.c`, its own sokol_gfx pipeline with hand-written GLSL 410 /
  GLSL ES 300 / MSL (keep the three in step; MSL isn't compiled on the Linux dev machine).
  Display only, drawn in `video_present` before sokol_gl.
- Packaging: `cmake/packaging.cmake` (install rules, CPack zip/tgz, macOS bundle + .icns,
  Linux .desktop), `packaging/linux/make-appimage.sh`, launcher icon from
  `tools/make_icon.py` (`assets/breadbin-6502.png`, original art). Packages must never
  include ROMs; NOTICE.txt carries the AGPL source offer (`BB_SOURCE_URL`).
- Save states: `core_save_state`/`core_load_state` (chips snapshot, ROM arrays zeroed and
  the 8 MB tape buffer left out: **a state must never contain ROM data**, `test_core`
  checks it), files in `src/state/` (`.bbstate`, header says what the state depends on,
  `savestate_compatible` refuses mismatches). States are raw structs: they only load in
  a build with the same `core_state_layout()`. `core_reset` is a full power cycle.
- **Determinism:** the core runs a fixed cycle count per frame. Never feed wall-clock
  time or unseeded randomness into emulation. `tests/test_core.c` and
  `tests/test_replay.c` check this.
- Vendored code goes in `external/` with its license, and is recorded in
  `external/VENDORED.md` (upstream commit). Don't edit vendored files. Work around
  them in our code and note it (see the `'0'` keymap workaround in `core_chips.c`).
- License: AGPL-3.0. Ask the owner before adding any dependency, and always before
  adding copyleft code.
- Never commit ROMs or game media (`.gitignore` covers `*.crt *.bin *.prg *.d64 *.g64
  *.tap *.t64 roms/`). The bundled Open ROMs are the only exception: `.rom` files under
  `external/open-roms/`.
- Before calling a task done: clean build with no new warnings, `ctest` passes, and
  for visible changes, the app has actually been run.

Build: `cmake -S . -B build && cmake --build build && ctest --test-dir build`.
