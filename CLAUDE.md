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
- Disk/tape media live in `src/media/` (pure C on top of `core.h`); `tests/test_media.c`
  builds its own test disks and tapes, so no third-party images are needed.
- Mods live in `src/mods/` (strict zip reader, `mod.toml` parser, inline/IPS/BPS
  patches, `modset` = validation, load order, cheats, install). Format: `docs/mods.md`.
  Packages are untrusted input: keep the size caps and the data-files-only rule.
- Diagnostic replays live in `src/replay/`. Every emulated frame in the app goes
  through `step_frame()` in `main.c` (live or replayed input, cheats, autostart,
  fingerprint), so anything that affects emulation must happen there or at game
  load, never elsewhere, or replays stop matching. `--record FILE --headless --frames N`
  and `--replay FILE --verify` make and check replays without a window.
- Gamepads: `src/platform/gamepad*.c` (own code, no library): `gamepad_evdev.c` (Linux;
  pure `evpad_*` model tested in `tests/test_gamepad.c` with made-up devices, plus a
  device layer that rescans `/dev/input` about once a second), `gamepad_xinput.c`
  (Windows, XInput DLL loaded at run time; not compilable on the Linux dev machine, CI
  builds it), none elsewhere. Buttons are bindable codes `INPUT_GAMEPAD_BASE + button`
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
