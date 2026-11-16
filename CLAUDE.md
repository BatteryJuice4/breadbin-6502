# CLAUDE.md: breadbin-6502

The generic C64 launcher (engine + menu + mod system). The full project guide lives
in the workspace `CLAUDE.md` one level up (`../CLAUDE.md`). Read it first, especially:
**Architecture**, **Supported media**, **System ROMs**, **chips limitations**,
**Cartridges**, **ROM handling**, **Mod system**, **Development phases**, **Testing**
and **Working conventions**.

Repo-specific rules:

- **No game-specific code here.** Gorf (or any game) lives in its own repo. Add generic,
  config-driven options instead.
- **Only `src/core/core_chips.c` includes chips headers.** Everything else goes through
  `src/core/core.h`.
- **Determinism:** the core runs a fixed cycle count per frame. Never feed wall-clock
  time or unseeded randomness into emulation. `tests/test_core.c` checks this.
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
