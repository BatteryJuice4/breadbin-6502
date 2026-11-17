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
