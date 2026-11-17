# Game packs

A game pack tells Breadbin-6502 about one game: its name, which ROM dumps are known
to be good, and the default controls and display settings. Without a game pack the
launcher runs in **generic mode**: any cartridge, disk or tape is accepted and the built-in
defaults apply.

A game pack never contains the game itself. Players always supply their own dump.

## Layout

```
mygame/
├── game.toml     required
├── icon.png      optional: window/taskbar icon (square, up to 512x512)
└── banner.png    optional: shown on the Select ROM screen and in the menu (up to 2048x2048)
```

Use original artwork only. Don't ship the game's box art, logos or screenshots.

## Where the launcher looks

1. `--gamepack DIR` on the command line.
2. A pack **built into the executable**. Game repos do this with the CMake option
   `-DBB_GAMEPACK_DIR=path/to/pack` (and usually `-DBB_OUTPUT_NAME=mygame` to name
   the executable). Re-run CMake after adding or removing `icon.png`/`banner.png`.
3. A `gamepack/` folder next to the executable.

`--generic` skips 2 and 3. Each game keeps its own settings in
`<config dir>/games/<id>/settings.toml`.

## game.toml

```toml
id      = "mygame"         # required: a-z 0-9 - _ (1-63 chars), used for the settings folder
title   = "My Game"        # required: window title and menus
media   = "cartridge"      # "cartridge" (default) | "disk" (.d64 .prg) | "tape" (.tap .t64)
video_standard = "PAL"     # "PAL" (default) | "NTSC" (6567R8 timing, 59.83 fps); --pal/--ntsc override it
sid_model = "6581"         # "6581" (default) | "8580" (only the 6581 is emulated so far)
joystick_port = 2          # default joystick port (1 or 2)
min_launcher_version = "0.1.0"   # refuse to start on older launchers
system_roms = "any"        # "any" (default) | "original": needs real Commodore ROMs

[rom]
cart_type    = "auto"      # "auto" (from the .crt header) | "8k" | "16k" | "ultimax"
load_address = 0x8000      # raw .bin dumps only: 0x8000 (default), 0xA000, 0xE000, 0xF000
known_sha1   = []          # SHA-1 of the game data (see below); for disk/tape games too

[load]                     # disk and tape games only (see "Loading")
file        = ""           # file to LOAD; "" = the first one on the disk/tape
done_pc     = 0x0810       # loading is over when the CPU runs this address
done_frames = 300          # ... or this many frames after the reset (when done_pc isn't set)

[input.default]            # default keys; players can rebind them in the menu
up    = "Up"
down  = "Down"
left  = "Left"
right = "Right"
fire  = ["Space", "LCtrl"] # up to two keys per action
menu  = "Escape"           # opens the launcher menu

[video]                    # display defaults (players can change them)
default_scale     = 3      # window scale 1-8
integer_scaling   = true
aspect_correction = true
crop_border       = false  # show only the 320x200 picture
filter            = "nearest"   # "nearest" | "smooth"
palette           = "colodore"  # default palette: "colodore", "pepto", or "mod:<mod id>"
extended_view_max_width = 0     # reserved: 0 = no extended (widescreen) view
```

Everything except `id` and `title` is optional. Unknown keys and invalid values are
reported as warnings on the console and otherwise ignored, so a typo never stops a game
from starting.

### Key names

Letters `A`-`Z`, digits `0`-`9`, `Space`, `Enter`, `Escape`, `Tab`, `Backspace`,
`Up`/`Down`/`Left`/`Right`, `LCtrl`/`RCtrl`, `LShift`/`RShift`, `LAlt`/`RAlt`,
`F1`-`F12`, keypad keys `KP_0`-`KP_9`, `KP_Enter`, and so on (case-insensitive). Gamepad
buttons (`Gamepad_A`) are accepted but ignored until gamepad support arrives. Keys bound
to the joystick don't also type on the C64 keyboard, so a game that needs a typed key
(for example `P` for pause) just works as long as that key isn't bound.

### ROM hashes

`known_sha1` lists the SHA-1 of the game data, defined per format so that the same game
matches no matter which header or padding a tool wrote:

| Format | What the SHA-1 covers |
|---|---|
| `.crt` / `.bin` cartridge | the ROM **chip data only**: the CHIP packet payloads of a `.crt`, or the whole raw `.bin` (so a dump matches wrapped or not) |
| `.d64` disk | the sector data (683 or 768 sectors), without the optional error bytes |
| `.prg` program | the whole file |
| `.t64` tape archive | each file as loaded (2-byte load address + data), in directory order |
| `.tap` raw tape | the pulse data after the 20-byte header |

The launcher shows the SHA-1 of the loaded game in the menu's ROM tab.

When the player picks a file that isn't in the list (or is a different cartridge type
than `cart_type`, or a different kind of media than `media`), the launcher warns them and
lets them play anyway. With an empty list, files aren't checked. For games on several
disks, list every disk's SHA-1.

### Loading (disk and tape)

There are no loading screens: the launcher resets the C64, types `LOAD"<file>",8,1`
(disk) or `LOAD"<file>",1,1` (tape) and then `RUN`, and runs at full speed with no
picture or sound until loading is over.

- **Disks** (`.d64`, `.prg`) and **`.t64`** archives are served instantly: when the
  game calls the KERNAL's LOAD routine, the launcher hands over the file directly. Games
  that load with their own fast loader (talking to the 1541 drive directly) don't work
  yet; that needs full drive emulation.
- **`.tap`** tapes play through the emulated datasette in real time, sped up as much as
  the computer allows, so turbo loaders work. Long tapes still take a while.

By default loading counts as over when no file was loaded (and the tape motor was off)
for a moment after `RUN`. Many games then unpack themselves for a few seconds (the
striped border); set `done_pc` (an address the game's own code reaches, such as its
main loop or title screen) or `done_frames` to keep the picture hidden until the game is
really ready. There's a time limit (2 minutes of C64 time for disks, 20 for tapes), so a
wrong value never hides a game forever.

Players can insert another disk or tape from the menu ("Swap disk..."), without
restarting the game.

## Example

See `examples/gamepack/` for a minimal pack you can try with
`breadbin-6502 --gamepack examples/gamepack`.
