# Recompiled code

A game repo can build a recompiled version of its game's cartridge code into its
executable. [breadbin-recomp](https://github.com/BatteryJuice4/breadbin-recomp) generates
the C from the user's own ROM at build time. The launcher ships none: generated C is
ROM-derived and is never committed or published.

The recompiled code runs on the same emulated chips as the interpreter, cycle for cycle,
so frames, fingerprints, save states and replays are identical on both. A replay
recorded with one verifies on the other.

## Building it in

```cmake
breadbin-recomp gen <rom> --hints recomp/hints.toml -o ${CMAKE_BINARY_DIR}/generated   # at build time
set(BB_RECOMP_SOURCES <generated>/program.c;<generated>/shard_000.c;...)  # from recomp.cmake
set(BB_RECOMP_INCLUDE <breadbin-recomp>/runtime)
add_subdirectory(external/breadbin-6502)
```

`BB_RECOMP_SOURCES` adds the generated files to the executable and defines
`BB_HAS_RECOMP`. Run with `--core recomp` (the default stays the interpreter).

## When it runs

`core_set_recomp()` (in `core.h`) takes the program. It runs only:

- while the inserted cartridge's data hashes to the SHA-1 it was generated from, so a
  patch mod turns it off and the interpreter runs the patched game;
- at addresses where the cartridge is mapped (code copied to RAM is interpreted);
- between interrupts: interrupt entry, BRK and JAM go to the interpreter;
- not in the last 200 cycles of a frame (frames end at an exact cycle, often
  mid-instruction, which only the interpreter can do).

The bridge (`bb_rc_cycle()` and friends in `src/core/core_chips.c`) implements
`src/core/recomp_abi.h`, a copy of breadbin-recomp's `runtime/bb_rc_abi.h`. Keep the two
identical. `tests/test_recomp.c` checks the bridge with a hand-written program against
the interpreter on PAL and NTSC, with VIC DMA stalls.

## Checking a game

`recomp_lockstep` (built on request in a build with `BB_RECOMP_SOURCES`) runs both cores
on the same seeded random joystick input and compares every frame:

```sh
cmake --build build --target recomp_lockstep
build/recomp_lockstep game.crt --port 2 --ntsc --frames 20000 [--key-every 3000]
```

Also: `--replay FILE --verify --core recomp`.
