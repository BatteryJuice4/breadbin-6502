# Breadbin-6502

A standalone launcher that runs Commodore 64 games (cartridge, disk and tape) as if
they were native PC games: double-click, pick your game once, play. Under the hood
it's a small embedded C64 emulator, but you never see emulator menus or a BASIC prompt.

Each game is described by a **game pack** (config, branding, optional mods), and
games are published as separate repos that pin a Breadbin-6502 release.

**No game ROMs are included.** You supply your own dumps.

> Status: **Phase 7 (polish), in progress.** Runs standard 8K, 16K and Ultimax cartridges
> (`.crt`, or raw `.bin` at $8000), disks (`.d64`, `.prg`) and tapes (`.tap`, `.t64`),
> on PAL or NTSC machines, with a Select-ROM screen, an in-game menu, saved settings,
> game packs (branding, default controls, ROM verification), mods (patches, cheats,
> palettes), diagnostic replays and save states.

## Building

Requires CMake ≥ 3.20 and C11 + C++17 compilers (C++ is used for the menu UI only).

Linux build dependencies (Debian/Ubuntu names):
`libx11-dev libxi-dev libxcursor-dev libgl1-mesa-dev libpulse-dev libdbus-1-dev`
(Arch: `libx11 libxi libxcursor mesa libpulse dbus`; FreeBSD: `pkg install cmake-core ninja
pkgconf libX11 libXi libXcursor libglvnd pulseaudio`, tested on 15.1). The file picker uses your
desktop's own dialog through xdg-desktop-portal. If that isn't available, a built-in
file browser is used instead.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/breadbin-6502
```

### Release packages

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBB_SOURCE_URL=https://…   # where your source is published
cmake --build build --config Release
(cd build && cpack -C Release)              # .zip on Windows and macOS (.app inside), .tar.gz on Linux
packaging/linux/make-appimage.sh build      # Linux AppImage (needs appimagetool on PATH)
```

Packages contain the program, README, `NOTICE.txt` (license and where to get the
source, as the AGPL requires) and the third-party licenses. They never contain ROMs.
Game repos get packages named after their game (`BB_OUTPUT_NAME`, `BB_PACKAGE_VERSION`, `BB_PACKAGE_README`,
the pack's title and icon). The Linux AppImage doesn't bundle system libraries, so build
it on the oldest distribution you want to support. The macOS `.app` isn't signed:
the first time, open it with right-click → Open. CI builds all packages for version
tags (`package` job).

## Using it

On first run, Breadbin-6502 shows the **Select ROM** screen. Pick a cartridge (`.crt`,
`.bin`), disk (`.d64`, `.prg`) or tape (`.tap`, `.t64`) image (or drag it onto the
window) and the game starts. Disk and tape games load at full speed behind a short
"Loading" notice: no BASIC prompt, no waiting for the tape. The launcher remembers it and
starts it straight away next time. Your ROM files are only read, never copied or changed.

Press **Esc** during play to pause and open the menu:

- **ROM:** the current game, its type and SHA-1, Select ROM…, and Swap disk/tape… for
  games on several disks or tapes
- **Video:** window scale (applies on the next launch), fullscreen, integer scaling,
  aspect correction, border crop, filter (sharp, smooth, or CRT: scanlines and a faint
  RGB mask, strongest from about 3x size), colour palette
  (Colodore, Pepto, or one from a palette mod), extended view (a wider picture that
  shows part of the side borders, for games whose pack allows it)
- **Audio:** volume, mute, latency
- **Input:** joystick port (1 or 2), bindings for the joystick (four keys or gamepad
  buttons per direction; click to rebind, then press a key or a gamepad button;
  right-click to clear), the menu key, the quick save/load keys, and the connected
  gamepads
- **States:** save states: a Quick slot (F9 saves, F10 loads, also during play) and five
  more slots, with when each was saved
- **Mods:** install `.c64mod` packages, switch mods on and off, set their load order;
  a mod that can't run says why (see [Mods](#mods))

A **save state** keeps the whole machine at that moment. It only loads with the same
game file, system ROMs and patch mods, and in the same version of Breadbin-6502 (the
States tab says why one can't be loaded). States never contain ROM data. They're kept in
the same `breadbin-6502` config folder as the settings (below), under `states/<id>/`.

The ROM tab can also **record a diagnostic replay**: it restarts the game and records
your input and a fingerprint of the machine after every frame. Playing it back
(`--replay FILE`) shows exactly where behaviour changed, so a replay file is the most
useful thing to attach to a bug report.

Settings are saved per user and per game (`<id>` is the game pack's id, or `generic`):

| OS | File |
|---|---|
| Linux / BSD | `~/.config/breadbin-6502/games/<id>/settings.toml` (or `$XDG_CONFIG_HOME/...`) |
| Windows | `%APPDATA%\breadbin-6502\games\<id>\settings.toml` |
| macOS | `~/Library/Application Support/breadbin-6502/games/<id>/settings.toml` |

A game passed on the command line is played without being remembered.

## Mods

Mods are `.c64mod` files (a zip with a `mod.toml` and data files; never native code).
Install them from the **Mods** tab; they're copied to `<config dir>/mods/<id>/`. A
folder game pack can also bring its own in its `mods/` folder. Mods are off until you
enable them, and each one is checked before it runs: the right game, a new enough
launcher, your dump's SHA-1 (if the mod asks for one), its dependencies and conflicts.

- **Patch** mods change the game data in memory when it loads (inline byte patches,
  IPS or BPS files). Your ROM file is never changed.
- **Cheat** mods write to RAM before every frame (e.g. lock the lives counter) or once.
- **Palette** mods add a colour palette to the Video tab.
- **Script** mods run a small Lua script with every frame, in a sandbox (no files, no
  network, no clock; memory and time limits). They can read and write RAM, change the
  joystick input, react to writes and show text over the picture. A script that fails
  is switched off and the Mods tab says why.
- **Plugin** mods switch on native plugins that a game's own build includes (never
  code from the mod itself). Game repos: [docs/plugins.md](docs/plugins.md).

How to make one: [docs/mods.md](docs/mods.md).

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

Disks (`.d64`, `.g64`) go into an emulated 1541 drive if you install a 1541 ROM (your own
dump: `1541.rom`, or `1541-c000.rom` + `1541-e000.rom`, in the system ROM folder). The
drive runs its real DOS, so fast loaders, copy protection and saving to disk work; a game
that writes to its disk gets a save copy in your settings folder, and your own file is
never changed. A small red light in the corner shows when the drive is busy, like the
LED on a real 1541 ("Drive light" in the ROM tab). Plain loads are still instant ("Fast
loading" in the ROM tab, on by default); turn it off and every load takes as long as on a real 1541, sped up as much as
your computer allows. Without a 1541 ROM, disks and `.prg` files are served instantly through
the C64's own LOAD routine, which covers BASIC loads and games that load more parts with
the KERNAL, but not games with their own fast loaders (`--no-drive` chooses this even with
the ROM installed). Tapes play
through an emulated datasette, so tape turbo loaders work; how fast a long tape loads
depends on your computer. See [docs/gamepack.md](docs/gamepack.md#loading-disk-and-tape).

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
| F9 / F10 | quick save / quick load (the C64 has no F9/F10 keys) |
| F11 | toggle fullscreen |
| Esc | launcher menu |

Keys bound to the joystick don't type on the C64 keyboard. Everything else does.

**Gamepads** (Linux, FreeBSD, Windows and macOS) work out of the box and
can be plugged in at any time: d-pad or left stick to move, A or B to fire, Start to open
the menu, which the d-pad, A (select), B (back) and LB/RB (switch tabs) control. On Linux,
pads are read from `/dev/input` (your desktop normally gives the logged-in user access);
on FreeBSD also from `/dev/input`, which is root-only by default: allow your user with a
devfs rule, e.g. in `/etc/devfs.rules` `[localrules=10]` + `add path 'input/event*' mode 0660
group operator` (and `devfs_system_ruleset="localrules"` in `/etc/rc.conf`, user in group
`operator`); on Windows through XInput, so Xbox-compatible pads; on macOS through Apple's
GameController framework (Xbox, PlayStation and MFi pads paired with the Mac). Every connected pad drives the same
joystick. Button names follow the Xbox layout by position: A is the bottom face button,
whatever its label (on a Super Nintendo-style pad, the one marked B). Some no-name pads
number their buttons in an odd order; the ones we know are fixed up automatically (so far a
DragonRise SNES-style pad, USB id `0079:0011`). For any other, rebind in the Input tab, and
please report it.

Command-line options: `[game file]`, `--sysroms DIR`, `--settings FILE`, `--gamepack DIR`,
`--generic` (ignore built-in/bundled game packs), `--scale N`, `--pal` / `--ntsc` (video standard;
default: the game pack's, else PAL),
`--frames N` (quit after N frames, for testing), `--replay FILE` (play a diagnostic replay
and compare every frame), `--verify` (with `--replay`: no window, exit code 0 if every frame
matched, 1 if not), `--record FILE` and `--headless` (record a replay without a window,
for regression tests), `--no-drive` (disks through the instant LOAD only), `--coverage DIR`
(record which addresses run as code, merged into `DIR/<game sha1>.bbcov`; analysis for a
future recompiler, also works with `--replay`). Set `BREADBIN_NO_NATIVE_DIALOG=1` to
always use the built-in file browser.

## License

Breadbin-6502 is licensed under the **GNU Affero General Public License v3.0**
(see `LICENSE`). Bundled third-party code keeps its own license. See
`external/VENDORED.md`:

- [floooh/chips](https://github.com/floooh/chips) and [floooh/sokol](https://github.com/floooh/sokol): zlib
- [MEGA65 Open ROMs](https://github.com/MEGA65/open-roms): LGPL-3.0-or-later
- [Dear ImGui](https://github.com/ocornut/imgui), [tomlc17](https://github.com/cktan/tomlc17): MIT
- [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended): zlib
