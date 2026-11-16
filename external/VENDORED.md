# Vendored third-party code

Copied verbatim (no local modifications unless noted). Update deliberately and
record the new commit here.

| Directory | Upstream | Commit | Date | License |
|---|---|---|---|---|
| `chips/` | https://github.com/floooh/chips | `7ef8776f637a6424ac615da64de6b968b06de791` | 2026-11-08 | zlib (`chips/LICENSE`) |
| `sokol/` | https://github.com/floooh/sokol | `93aa187b5d387cc24b7290b3ca3f13f61e139e7c` | 2026-11-10 | zlib (`sokol/LICENSE`) |
| `open-roms/` | https://github.com/MEGA65/open-roms | `ad178dbe4d48cd6a317737a8e0e7e662f7e33d32` | 2026-05-16 | LGPL-3.0-or-later (`open-roms/LICENSE`, `COPYING`, `COPYING.LESSER`) |

## What was copied

- **chips:** `chips/{chips_common,m6502,m6526,m6569,m6581,kbd,mem,clk,m6522}.h`,
  `systems/{c64,c1530,c1541}.h`.
- **sokol:** `sokol_{app,gfx,audio,glue,log,time}.h`, `util/sokol_gl.h`.
- **open-roms:** prebuilt `bin/` images from the generic C64 set. These are free
  replacement ROMs, not Commodore's code. They're embedded into the executable at build
  time as the default system ROMs.

  | File | SHA-1 |
  |---|---|
  | `kernal_generic.rom` | `2ecf1bf1553bf77ba8a007a18a1a7c291d215b33` |
  | `basic_generic.rom` | `4468852d4fde3394ca750f45d1c83914e218dac5` |
  | `chargen_openroms.rom` | `466e399a5e994b52f6cef6019b32b9f6b504cd14` |

  The Open ROMs source is at the upstream commit above. Generic KERNAL and BASIC must
  only be used together.
