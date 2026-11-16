# Breadbin-6502

A standalone launcher that runs Commodore 64 games (cartridge, disk and tape) as if
they were native PC games: double-click, pick your game once, play. Under the hood
it's a small embedded C64 emulator, but you never see emulator menus or a BASIC prompt.

Each game is described by a **game pack** (config, branding, optional mods), and
games are published as separate repos that pin a Breadbin-6502 release.

**No game ROMs are included.** You supply your own dumps.

> Status: **Phase 4 (game packs).** Runs standard 8K, 16K and Ultimax cartridges
> (`.crt`, or raw `.bin` at $8000) with a Select-ROM screen, an in-game menu, saved
> settings and game packs (branding, default controls, ROM verification). Disk/tape
> and mods come next.

## Building

Requires CMake ≥ 3.20 and C11 + C++17 compilers (C++ is used for the menu UI only).

Linux build dependencies (Debian/Ubuntu names):
`libx11-dev libxi-dev libxcursor-dev libgl1-mesa-dev libpulse-dev libdbus-1-dev`
(Arch: `libx11 libxi libxcursor mesa libpulse dbus`). The file picker uses your
desktop's own dialog through xdg-desktop-portal. If that isn't available, a built-in
file browser is used instead.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/breadbin-6502
```

## Using it

On first run, Breadbin-6502 shows the **Select ROM** screen. Pick a `.crt` or `.bin`
file (or drag it onto the window) and the game starts. The launcher remembers it and
starts it straight away next time. Your ROM files are only read, never copied or changed.

Press **Esc** during play to pause and open the menu:

- **ROM:** the current cartridge, its type and SHA-1, and Select ROM…
- **Video:** window scale (applies on the next launch), fullscreen, integer scaling,
  aspect correction, border crop, sharp or smooth filtering
- **Audio:** volume, mute, latency
- **Input:** joystick port (1 or 2), key bindings for the joystick (two keys per
  direction; click to rebind, right-click to clear) and the menu key

Settings are saved per user and per game (`<id>` is the game pack's id, or `generic`):

| OS | File |
|---|---|
| Linux / BSD | `~/.config/breadbin-6502/games/<id>/settings.toml` (or `$XDG_CONFIG_HOME/...`) |
| Windows | `%APPDATA%\breadbin-6502\games\<id>\settings.toml` |
| macOS | `~/Library/Application Support/breadbin-6502/games/<id>/settings.toml` |

A cartridge passed on the command line is played without being remembered.

## Game packs

A game pack (`game.toml`, plus an optional `icon.png` and `banner.png`) turns the
launcher into a specific game: the window and menus use the game's title and artwork,
the controls and display start with the game's defaults, and the ROM you pick is
checked against known good dumps (you're warned about unknown dumps, and can still
play). Game repos build their pack into the executable. You can also try one with
`--gamepack DIR`. See [docs/gamepack.md](docs/gamepack.md) for the format and
`examples/gamepack/` for a small example.

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

## Default controls

| Key | C64 |
|---|---|
| Typing, Enter, Backspace | keyboard, RETURN, DEL |
| Cursor keys | cursor keys |
| Home / End / Insert / Page Up | CLR/HOME, RUN/STOP, INST, RESTORE |
| Left Ctrl / Left Alt | CTRL / C= |
| F1–F8 | F1–F8 |
| Keypad 8/2/4/6, Keypad 0 or Right Ctrl | joystick and fire (port 2 by default; some games use port 1) |
| F11 | toggle fullscreen |
| Esc | launcher menu |

Keys bound to the joystick don't type on the C64 keyboard. Everything else does.

Command-line options: `[cartridge]`, `--sysroms DIR`, `--settings FILE`, `--gamepack DIR`,
`--generic` (ignore built-in/bundled game packs), `--scale N`,
`--frames N` (quit after N frames, for testing). Set `BREADBIN_NO_NATIVE_DIALOG=1` to
always use the built-in file browser.

## License

Breadbin-6502 is licensed under the **GNU Affero General Public License v3.0**
(see `LICENSE`). Bundled third-party code keeps its own license. See
`external/VENDORED.md`:

- [floooh/chips](https://github.com/floooh/chips) and [floooh/sokol](https://github.com/floooh/sokol): zlib
- [MEGA65 Open ROMs](https://github.com/MEGA65/open-roms): LGPL-3.0-or-later
- [Dear ImGui](https://github.com/ocornut/imgui), [tomlc17](https://github.com/cktan/tomlc17): MIT
- [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended): zlib
