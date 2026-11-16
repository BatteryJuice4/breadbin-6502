# Breadbin-6502

A standalone launcher that runs Commodore 64 games (cartridge, disk and tape) as if
they were native PC games: double-click, pick your game once, play. Under the hood
it's a small embedded C64 emulator, but you never see emulator menus or a BASIC prompt.

Each game is described by a **game pack** (config, branding, optional mods), and
games are published as separate repos that pin a Breadbin-6502 release.

**No game ROMs are included.** You supply your own dumps.

> Status: **Phase 2 (cartridges).** Runs standard 8K, 16K and Ultimax cartridges
> (`.crt`, or raw `.bin` at $8000). The launcher menu, game packs and mods come next.

## Building

Requires CMake ≥ 3.20 and a C11 compiler.

Linux build dependencies (Debian/Ubuntu names):
`libx11-dev libxi-dev libxcursor-dev libgl1-mesa-dev libpulse-dev`
(Arch: `libx11 libxi libxcursor mesa libpulse`).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/breadbin-6502 path/to/game.crt
```

You can also drag a `.crt` or `.bin` file onto the window. Without a cartridge,
the C64 starts at the BASIC prompt.

Supported cartridge types so far: `.crt` hardware type 0 (normal cartridge) as 8K,
16K or Ultimax. Raw `.bin` dumps of 4K/8K/16K are loaded at $8000. Bank-switching
types (Ocean, Magic Desk, EasyFlash, ...) are added when a game needs them, and are
reported as unsupported until then.

Windows (MSVC) and macOS work the same way, without extra dependencies.

## System ROMs

The C64 needs system ROMs (KERNAL, BASIC, character set) to run. Breadbin-6502
bundles the free [MEGA65 Open ROMs](https://github.com/MEGA65/open-roms) so it works
out of the box. Some games need Commodore's original ROMs. If you have dumps of your
own, put them here:

| OS | Folder |
|---|---|
| Linux / BSD | `~/.config/breadbin-6502/roms/system/` (or `$XDG_CONFIG_HOME/...`) |
| Windows | `%APPDATA%\breadbin-6502\roms\system\` |
| macOS | `~/Library/Application Support/breadbin-6502/roms/system/` |

Name them `kernal.rom` (8 KB), `basic.rom` (8 KB) and `chargen.rom` (4 KB).
KERNAL and BASIC must be supplied together. CHARGEN can be replaced on its own.
You can also pass `--sysroms <folder>`.

## Controls (Phase 1, fixed; remapping comes with the launcher menu)

| Key | C64 |
|---|---|
| Typing, Enter, Backspace | keyboard, RETURN, DEL |
| Cursor keys | cursor keys |
| Home / End / Insert / Page Up | CLR/HOME, RUN/STOP, INST, RESTORE |
| Left Ctrl / Left Alt | CTRL / C= |
| F1–F8 | F1–F8 |
| Keypad 8/2/4/6, Keypad 0 or Right Ctrl | joystick (port 2) and fire |
| F11 | toggle fullscreen |
| Esc | reserved for the launcher menu |

Command-line options: `[cartridge]`, `--sysroms DIR`, `--joyport 1|2`, `--scale N`, `--frames N`.

## License

Breadbin-6502 is licensed under the **GNU Affero General Public License v3.0**
(see `LICENSE`). Bundled third-party code keeps its own license. See
`external/VENDORED.md`:

- [floooh/chips](https://github.com/floooh/chips) and [floooh/sokol](https://github.com/floooh/sokol): zlib
- [MEGA65 Open ROMs](https://github.com/MEGA65/open-roms): LGPL-3.0-or-later
