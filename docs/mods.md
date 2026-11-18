# Making mods

A mod changes a game without touching the player's ROM file. There are four kinds:

| Type | What it does | Since |
|---|---|---|
| `patch` | Changes bytes of the game data in memory when the game loads | 0.3.0 |
| `cheat` | Writes to the C64's RAM before every frame, or once | 0.3.0 |
| `palette` | Adds a 16-colour palette to the Video tab | 0.3.0 |
| `script` | Runs a sandboxed Lua script with every frame | 0.5.0 |

Set `min_launcher_version = "0.5.0"` in script mods, so older launchers explain why
they can't run them.

## Packages

A mod is a **`.c64mod`** file: an ordinary zip archive with `mod.toml` at its top level
and any data files it needs. Players install it from the **Mods** tab.

A package may only contain data files: `.toml`, `.ips`, `.bps`, `.txt`, `.md`, `.png`
and `.lua` (script mods' source, run in the sandbox below). Anything else (`.exe`, `.dll`, `.so`, ...) and the
launcher refuses the whole package. Mods never run native code. Native features come
only from trusted plugins compiled into a game's executable, which a mod can ask for in
`plugins` (none exist yet, so such a mod can't be enabled).

Limits: 256 files, 16 MB per file, 64 MB in all; stored or deflate compression; no
encryption.

While developing, put the unpacked folder (with `mod.toml` inside) in a folder game
pack's `mods/` folder, or in `<config dir>/mods/<game id>/`. Use **Rescan** in the
Mods tab after editing. To package it: `cd my-mod && zip -r ../my-mod.c64mod .`

## mod.toml

```toml
id          = "gorf-easy-start"     # a-z 0-9 - _ . ; unique per game
name        = "Easy start"
version     = "1.0.0"
author      = "someone"
description = "Start with more lives."
game        = "gorf"                # the game pack id ("generic" without a pack)
min_launcher_version = "0.3.0"      # optional
requires_sha1 = ["4d24d5ed3d8b38235d9ae5965b675ffece0981cc"]   # optional: dumps it was made for
dependencies  = ["gorf-base>=1.1.0"]  # optional: other mod ids, optionally with a minimum version
conflicts     = ["gorf-hard-mode"]    # optional
plugins       = []                    # optional: trusted built-in plugins (none exist yet)
type        = "patch"               # "patch" | "cheat" | "palette" | "script"
```

Required: `id`, `name`, `version`, `game`, `type`. Unknown keys are ignored with a
warning. A mod that can't be read is listed as broken with the reason, and never
stops the launcher.

### When a mod can run

The Mods tab shows why a mod can't run. It must:

- be for this game (`game`) and need no newer launcher (`min_launcher_version`)
- match the player's dump, if `requires_sha1` is given (the SHA-1 shown in the ROM tab)
- have its dependencies installed, enabled, new enough and **earlier** in the load order
- not conflict with an enabled mod (the one later in the load order gives way)

Mods are off until the player enables them. The load order is the order in the
Enabled list; for overlapping patches the later mod wins (the Mods tab notes it).

## Patch mods

Patches apply to the **game data**, the same bytes the game's SHA-1 covers:

| Media | Game data |
|---|---|
| Cartridge (`.crt`, `.bin`) | the ROM chip contents in file order (16K carts: `$8000-$BFFF`, so offset `0x1000` is `$9000`) |
| Disk (`.d64`) | the sector data (track 1 sector 0 first, no error bytes) |
| Program (`.prg`) | the whole file, including the 2-byte load address |
| Tape (`.tap`, `.t64`) | can't be patched |

Patches never change the size of the data. Inline patches:

```toml
type = "patch"

[[patch]]
offset = 0x1234                 # into the game data
bytes  = "A9 05 EA"             # hex; spaces optional
expect = "A9 03 EA"             # optional: the original bytes; the mod refuses to apply if they differ

[[patch]]
offset = 0x2000
bytes  = "60"
```

Or IPS / BPS files made with the usual tools against the game data (for a `.crt`,
extract the chip data first; for `.d64` and `.prg` use the file itself):

```toml
type = "patch"
patch_files = ["fix.ips", "music.bps"]
```

Inline patches apply first, then the files in order. A BPS patch also checks the
source and result checksums. If anything fails, the whole mod is skipped and the
Mods tab says why. Patches apply when the game loads, so after enabling a patch mod
use **Restart the game now** in the Mods tab.

## Cheat mods

```toml
type = "cheat"

[[cheat]]
address = 0x00C5        # C64 RAM address
value   = 9             # one byte...
label   = "Lives"

[[cheat]]
address = 0x0340
bytes   = "01 02 03"    # ...or several (hex)
mode    = "once"        # "freeze" (default): before every frame; "once": first frame only
```

Cheats write to RAM (not to I/O registers or ROM). They start once a disk or tape
game has finished loading. `once` writes happen on the first frame after the game
starts, a reset, or a change in the Mods tab.

## Palette mods

```toml
type = "palette"

[palette]
name   = "Greyscale"        # shown in the Video tab (default: the mod's name)
colors = ["#000000", "#FFFFFF", "#555555", "#AAAAAA", "#666666", "#888888", "#333333", "#DDDDDD",
          "#666666", "#444444", "#888888", "#4A4A4A", "#7B7B7B", "#BBBBBB", "#777777", "#B2B2B2"]
```

16 colours in C64 order (black, white, red, cyan, purple, green, blue, yellow, orange,
brown, light red, dark grey, grey, light green, light blue, light grey). Once enabled,
the palette appears in the Video tab. Palettes only change what's shown; the
emulation (and replays) never see them. A game pack can default to one with
`palette = "mod:<mod id>"` in its `[video]` table.

## Script mods

```toml
type   = "script"
script = "main.lua"         # the Lua file in the package (default: main.lua)
min_launcher_version = "0.5.0"
```

The script is Lua 5.4 source (at most 256 KB, one file; compiled chunks are refused).
Its main chunk runs when the game starts, then the launcher calls these global
functions if the script defines them:

| Hook | When |
|---|---|
| `on_frame()` | before every frame; the joystick input for that frame can still be changed |
| `on_after_frame()` | after every frame |
| `on_write(addr, value)` | after a frame, once for each CPU write to a watched address, in order |

The `bb` table is the script's view of the machine:

| Function | |
|---|---|
| `bb.read(addr)`, `bb.read16(addr)` | read RAM (`read16`: little-endian word) |
| `bb.write(addr, value)` | write a byte to RAM (0..255) |
| `bb.frame()` | frames since the machine was switched on |
| `bb.joystick(port)` | joystick bits for port 1 or 2 this frame (the player's, after earlier scripts) |
| `bb.set_joystick(port, bits)` | replace them, only in `on_frame`: `bb.UP`, `bb.DOWN`, `bb.LEFT`, `bb.RIGHT`, `bb.FIRE` combined with `\|` |
| `bb.watch_write(addr [, on])` | call `on_write` for CPU writes to `addr` (up to 64 addresses; `false` stops) |
| `bb.text(x, y, text [, rgb])` | show text over the picture this frame; `x`, `y` in C64 pixels from the top-left of the 320x200 screen area (negative = into the border), `rgb` like `0xFFFF00`; up to 32 per frame |
| `bb.log(...)`, `print(...)` | write a line to the launcher's log (stderr), prefixed with the mod id |
| `bb.api` | 1 (the version of this API) |

Example: show the score from RAM and auto-fire while the button is held.

```lua
function on_frame()
  local j = bb.joystick(2)
  if j & bb.FIRE ~= 0 and bb.frame() % 8 >= 4 then
    bb.set_joystick(2, j & ~bb.FIRE)     -- release every few frames
  end
end

function on_after_frame()
  bb.text(0, -10, string.format("score %d", bb.read16(0x0340)), 0xFFFF00)
end
```

**The sandbox.** Scripts get Lua's base, `string`, `table`, `math`, `utf8` and
`coroutine` libraries, and nothing else: no `io`, `os`, `package`/`require`, `debug`,
`dofile`/`loadfile` or `string.dump`; `load` only accepts source text. Each script has
16 MB of memory and 5 million Lua instructions per hook call. A script that raises an
error, runs too long or runs out of memory is stopped; the Mods tab shows why, until the
game is loaded again. It never takes the launcher down.

**Determinism.** Replays must play back identically, so a script only sees the emulated
machine: no clock, no files. `math.random` starts from a fixed seed (also after
`math.randomseed()` without arguments), and `pairs` visits string and number keys in
the same order every run (not tables or functions used as keys: their order follows
memory addresses, and so does `tostring` of a table). Scripts restart with a fresh state
whenever the game is loaded or reset, a save state is loaded, or the enabled mods
change: keep what matters in C64 RAM, or rebuild it from RAM in the main chunk. Text
from `bb.text` is display only.

## Replays and mods

A diagnostic replay records which mods were enabled (id and package SHA-1). Playing
it enables the same mods for that session and warns if one is missing or different.
