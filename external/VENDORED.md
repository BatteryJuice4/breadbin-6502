# Vendored third-party code

Copied verbatim (no local modifications unless noted). Update deliberately and
record the new commit here.

| Directory | Upstream | Commit | Date | License |
|---|---|---|---|---|
| `chips/` | https://github.com/floooh/chips | `7ef8776f637a6424ac615da64de6b968b06de791` | 2026-11-08 | zlib (`chips/LICENSE`) |
| `sokol/` | https://github.com/floooh/sokol | `93aa187b5d387cc24b7290b3ca3f13f61e139e7c` | 2026-11-10 | zlib (`sokol/LICENSE`) |
| `open-roms/` | https://github.com/MEGA65/open-roms | `ad178dbe4d48cd6a317737a8e0e7e662f7e33d32` | 2026-05-16 | LGPL-3.0-or-later (`open-roms/LICENSE`, `COPYING`, `COPYING.LESSER`) |
| `imgui/` | https://github.com/ocornut/imgui (tag v1.93.0) | `c1aaac65e896a45f81f47a4d68f9959e0f86c859` | | MIT (`imgui/LICENSE.txt`) |
| `nfd/` | https://github.com/btzy/nativefiledialog-extended (tag v1.4.1) | `65b2f070a0fcfee0d453a64e770a217ab0b6ff4f` | | zlib (`nfd/LICENSE`) |
| `tomlc17/` | https://github.com/cktan/tomlc17 (tag R261109) | `aa4ca1bc5fe6382ac631a8d1428077a9a084a64d` | 2026-11-09 | MIT (`tomlc17/LICENSE`) |
| `stb/` | https://github.com/nothings/stb (stb_image v2.32) | `9600522de53973336fa033a289858a4354cbb04c` | 2026-11-12 | public domain or MIT (`stb/LICENSE`) |
| `lua/` | https://www.lua.org/ftp/lua-5.4.9.tar.gz (sha256 `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`) | 5.4.9 | 2026-08-10 | MIT (`lua/LICENSE`, from the end of `lua.h`) |

## Patched copies

- `src/core/patched/c64.h`: chips `systems/c64.h` @ `7ef8776`, altered to add the
  expansion port (EXROM/GAME, ROML/ROMH banks, PLA-accurate CPU memory map, and the
  Ultimax VIC view). Every change is marked `BREADBIN PATCH`. The original stays
  untouched in `external/chips/`. When updating chips, re-apply the patch to the new
  `c64.h` and re-run `test_cart_boot`.

## What was copied

- **chips:** `chips/{chips_common,m6502,m6526,m6569,m6581,kbd,mem,clk,m6522}.h`,
  `systems/{c64,c1530,c1541}.h`.
- **sokol:** `sokol_{app,gfx,audio,glue,log,time}.h`, `util/sokol_gl.h`, `util/sokol_imgui.h`.
- **lua:** everything in `src/` except `lua.c`, `luac.c` and the Makefile. Built through
  `src/mods/lua_impl.c` (our file), which sets a fixed string-hash seed and compiles only
  the core, `lauxlib` and the base/coroutine/table/string/math/utf8 libraries.
- **imgui:** core files only (`imgui*.cpp/.h`, `imconfig.h`, `imstb_*.h`), no backends
  (sokol_imgui is the backend).
- **nfd:** `CMakeLists.txt`, `src/`, `3ps/` (built with `NFD_PORTAL=ON`: on Linux/BSD
  it talks to xdg-desktop-portal over D-Bus, so no GTK dependency).
- **tomlc17:** `src/tomlc17.{c,h}` (successor of the now-obsolete tomlc99).
- **stb:** `stb_image.h` only, compiled in `src/video/image.c` as a PNG-only decoder
  (no stdio, dimensions capped) for game pack icons and banners.
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
