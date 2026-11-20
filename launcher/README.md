# hp2launcher

A prototype of the settings file and input layer for a Need for Speed: Hot
Pursuit 2 port, written to stand on its own until there is a port to host it.
Nothing here depends on the game, so it can be finished and tested now and
dropped in later.

## What it does

- **A settings file** both the launcher and the game read and write, so the
  same options exist in both places rather than being duplicated.
- **An options menu model** (`hp2_menu`): the settings as rows, with labels,
  value text and what left/right does, so the launcher and the in-game Options
  screen show one list and cannot drift apart. Video: internal resolution
  (PS2 640x448 up to 4K; with 21:9 the ultrawide 1720x720 up to 5120x2160,
  and switching aspect keeps the height), aspect ratio (auto, 4:3, 16:9,
  16:10, 21:9), window mode, VSync, frame cap, anti-aliasing, anisotropic
  filtering, FPS counter. Controls: scheme, button icons,
  dead zone, vibration, and a button for every action (Accelerate, Brake,
  Handbrake, Shift Up/Down, Look Back, Camera, Siren, Pause, Next Song):
  remap by stepping or by pressing the button; taking a button another
  action has swaps the two, so none is ever shared or left unbound; Default
  Controls resets them. Audio (the game's own rows): seven volumes (menu
  music, racing music, ambient, tires, engine, menu sounds, speech; 0-100%
  in 5% steps), audio mode (stereo, mono, surround), and Default Volumes
  and Default Jukebox buttons; the Jukebox has its own entry on the Options
  menu, under Audio. A front end only draws rows and
  forwards input.
- **Controller detection** that works out which button names to show. The same
  physical button is `A` on an Xbox pad, `Cross` on a PlayStation pad and `B`
  on a Nintendo pad — binding by position and labelling separately is the only
  way to get this right for everyone.
- **The original menu layouts** (`hp2_fe`): parses the 41 front-end screens
  of the user's own `FRONTEND\FRONTB.BUN` (chunk `0x80010200`, see
  `docs/formats.md` section 17) into widgets with name, position, scale, font,
  colour, string id or texture, finds screens by name and the `MENUITEM_<n>`
  rows in order, and works out where the 640 x 480 canvas lands on a window:
  FIT (4:3 with bars), STRETCH (fills, distorts) or EXPAND (4:3 at window
  height, side room for a background). It does not draw; nothing of the game
  is compiled in.
- **The original Options menus, drawn by the port** (`hp2fe`): the OPTIONS
  screen with the game's own rows and text, and the `hp2_menu` settings in
  the DISPLAY_OPTIONS layout (scrolling list, arrows, row masks, help ticker),
  using the game's textures (`hp2_tex`, a C port of `tools/hp2_tpk.py`'s
  decoder) and bitmap fonts (`hp2_font`), with text from FE_<LANGUAGE>.BIN
  (`hp2_lang`). Everything is read at run time from your own disc copy
  (`hp2_zdir`: ZDIR.BIN + ZZDATA.BIN); nothing of the game is in the repo.
- **Axis shaping** with a dead zone that rescales the remaining travel, so the
  stick still reaches full tilt instead of being clipped short.

## Layout

| File | Needs SDL | Tested |
|---|---|---|
| `src/hp2_settings.c` | no | yes |
| `src/hp2_menu.c` | no | yes |
| `src/hp2_glyphs.c` | no | yes |
| `src/hp2_pad.c` | no | yes |
| `src/hp2_fe.c` | no | yes, and against the real FRONTB.BUN |
| `src/hp2_tex.c` | no | yes; identical to `hp2_tpk.py` on 481 textures |
| `src/hp2_font.c` | no | yes, and on the real fonts |
| `src/hp2_lang.c` | no | yes, and on FE_ENGLISH.BIN |
| `src/hp2_zdir.c` | no | yes, and through the real archive |
| `src/hp2_femenu.c` | no | yes, on the real layouts |
| `src/hp2_fedraw.c` | yes | screenshots, offscreen |
| `src/fe_main.c` | yes | screenshots, offscreen |
| `src/hp2_input.c` | yes | compile-checked |
| `src/main.c` | yes | compile-checked |

The split is deliberate: everything with logic worth testing is kept away from
SDL, and `hp2_input.c` is a thin adapter. SDL's own controller-type constants
are translated to `hp2_pad_kind` in one function, so no numeric value of SDL's
is assumed anywhere else.

## Building

```sh
make test        # unit tests, no SDL needed
make stubcheck   # compile-check the SDL sources without SDL installed
make fecheck FRONTB=~/hp2work/extracted/021_14c57a19.bin
                 # hp2_fe on the real bundle, compared with hp2_fescreens.py
make             # build the launcher (needs SDL2)
make hp2fe       # build the front-end menus (needs SDL2 2.0.18 or newer)
```

SDL2:

- CachyOS / Arch: `sudo pacman -S sdl2`
- Debian / Ubuntu: `sudo apt install libsdl2-dev`
- Windows: MSYS2, then `pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-gcc`,
  and build from the MinGW64 shell.

## Running

```sh
./hp2launcher                                   # live input display
./hp2launcher --show                            # print settings and bindings
./hp2launcher --menu                            # the options menu rows as the game shows them
./hp2launcher --set glyphs=playstation --show
./hp2launcher --config /path/to/hp2.ini
```

With no arguments it loads `hp2.ini` (writing one if absent), opens the first
controller it finds, prints which pad it detected and the bindings in that
pad's own button names, then shows live steering, throttle, brake and the
actions currently held. Escape or Ctrl+C to quit.

### The menus (`hp2fe`)

```sh
./hp2fe --disc /path/to/disc/NFSHP2          # the folder with ZDIR.BIN + ZZDATA.BIN
./hp2fe --files 021_14c57a19.bin 302_4c937012.bin 003_0e1049ed.bin
                                              # or extracted FRONTB.BUN, GLOBALB.BUN, FE_ENGLISH.BIN
./hp2fe --disc ... --fit expand --size 2560x1080
./hp2fe --disc ... --shot options.bmp --keys down,down,accept   # one frame, offscreen
```

Arrows move, Enter/Space accepts, Esc/Backspace goes back, H or F1 is help; on
a controller the d-pad or left stick move, Cross (south) or Start accepts,
Circle (east) goes back and Triangle (north) is help, which switches the
ticker between the row's help line and the menu buttons (named for your pad).
The corner prompt shows the Triangle glyph. Keyboard and controller are
confirmed on a desktop. The selected row looks as in the game (orange label,
white and orange lines, the help line in the ticker; checked against PCSX2
screenshots). hp2fe starts on the title screen (PRESS START; `--options`
starts on Options instead). Its splash picture is filled at run time from the
intro movies, which the port cannot play yet, so it shows Main Menu's backdrop
and the NFS logo. Accept leads to the *Main Menu* (Hot Pursuit, World Racing, Quick Race,
Options). Quick Race is the port's own row: it picks Hot Pursuit or World
Racing settings at random and opens *car select*, laid out as on a PCSX2 screenshot: the car's name top-left,
the 23 cars as a strip of make logos along the bottom (left/right), the colour
swatches down the right (up/down) and the game's own specs (0-60, top speed,
power) at the left. Back returns to Main Menu. The game also shows the car in
3D with a spotlit floor, and its track list comes next: neither is in the port
yet, and the swatch colours are those of the BMW M5 for every car. Hot Pursuit opens Ultimate Fugitive, Racing But With Pigs and You're The
Pig (the game's Ultimate Racer, Challenge and You're The Cop); World Racing
opens Ultimate Racer and Just Race (the game's Championship and Challenge).
The game's help lines go with the original rows; picking a mode does nothing
yet. Back walks up the same path; back on the title quits. On OPTIONS, *Display* opens the
Video page, *Controller* the Controls page and *Audio* the Audio page (the
game's AUDIO_OPTIONS layout, volumes as its sliders). *Jukebox*, right under
Audio on OPTIONS (shown when there is a music bank), lists the music bank's songs by genre, title over artist, with where
each plays: Off, Menu (menus only) or On (menus and races; the game's
World Racing, Hot Pursuit, Menu/Racing and Always all become On). Left/right
changes it: a song set to Menu or On starts at once; setting the song
playing to Off stops the music until a song is set to Menu or On again, Next
Song is pressed, or you leave the Jukebox (then another Menu or On song
plays). Songs set to Off are never picked. *Default Jukebox*
puts them all back. *Save/Load* is
for player progress and does nothing yet; Player Names, Credits and
Languages are left out. `--disc` takes the music from the archive's own bank;
`--jukebox BANK` uses another one instead (extracted entry 322, or a bank you
modded with `hp2_music.py`), with `--disc` or `--files`.

**Menu music.** hp2fe plays the Jukebox's songs set to Menu or On one after
another in random order, at the Menu Music volume, streamed from the music
bank in place (`hp2_vag`: the game's PS-ADPCM, identical sample for sample
to `tools/hp2_audio.py`; `hp2_jbplay`: the playlist). The Next Song button
(default: right stick click, RS / R3; N on the keyboard) skips to another.
`--no-music` keeps it quiet.

**Remapping.** On the Controls page, Enter / A on an action shows "Press a
button": press the one you want on the controller (Esc cancels), or step
with left/right. Button names follow the pad in hand (or the Button icons
setting). Left/right change a setting; `hp2.ini` (the same file the
launcher reads, `--config` to choose another) is saved when you leave the list.
The window follows the aspect setting: 4:3 draws the 640 x 480 canvas with
side bars (FIT), anything else keeps it at full height and tiles the
background into the sides (EXPAND); `--fit` overrides. Window mode applies at
once; the other video settings are for the game, which does not exist yet.

## The settings file

```ini
# Need for Speed: Hot Pursuit 2 - settings
# Written by the launcher; safe to edit by hand.

[video]
internal_width = 1920
internal_height = 1080
aspect = 16:9       # auto, 4:3, 16:9, 16:10, 21:9
window = borderless # windowed, borderless, fullscreen
vsync = on

[input]
scheme = modern     # modern, classic
glyphs = auto       # auto, xbox, playstation, nintendo, generic, keyboard
deadzone = 0.20
rumble = on

[audio]             # volumes in percent, 0 = mute
menu_music = 70
race_music = 70
ambient = 80
tires = 80
engine = 80
menu_sfx = 80
speech = 80
mode = stereo       # stereo, mono, surround

[jukebox]           # only songs you changed; n = order in the music bank
song2 = on          # off, menu, on

[controls]          # only remapped actions; the rest follow the scheme
next_song = up      # south east west north lb rb lt rt start select up down left right ls rs
```

Unknown keys are ignored rather than rejected, so a newer game can read an
older file and the reverse. Every value is clamped on load, so a hand-mangled
or corrupt file can never leave the game unable to start. `internal_width` and
`internal_height` are what the game renders at; the window can be any size.
The lower bound is the PS2's own 512x448, so "render exactly as the original
did, present at any size" is expressible.

`scheme = classic` puts throttle and brake back on the face buttons where the
PS2 release had them; `modern` puts them on the analog triggers.

## Why glyphs are handled this way

SDL names face buttons by position: `SDL_CONTROLLER_BUTTON_A` is always the
bottom button, whatever is printed on it. So actions bind to positions, and
the label is looked up separately from the detected pad family. The result:

```
Xbox Controller          | Accelerate: RT | Brake: LT | Handbrake: A
DualSense                | Accelerate: R2 | Brake: L2 | Handbrake: Cross
Switch Pro Controller    | Accelerate: ZR | Brake: ZL | Handbrake: B
```

One binding, three correct sets of prompts. A pad that isn't recognised gets
neutral labels (`Button 1`, `L1`) rather than a confident guess at Xbox.

## State

988 assertions pass across the two test programs, covering parsing,
whitespace and case handling, clamping of out-of-range and corrupt values,
file round-tripping, the full glyph table, binding collisions, and axis and
trigger shaping across their whole range.

`test_fe` builds bundles of its own (padding chunks, odd chunks between
screens, more widgets than the header's count, rows stored out of order),
damages every byte and cuts every prefix of them; built with
`-fsanitize=address,undefined` it reads nothing past the buffer. On the real
FRONTB.BUN it finds 41 screens and OPTIONS / DISPLAY_OPTIONS / AUDIO_OPTIONS
with 7 / 9 / 11 rows, and `tests/check_fe.py` finds all 1,549 widgets
identical to `tools/hp2_fescreens.py`. The rows are about 37 px apart from
y = 71 but placed by hand (up to 2 px off) and stored out of order, so look
them up with `hp2_fe_row`, not by position in the list.

`test_assets` covers the renderer's asset modules: the string codes, fonts,
language files and texture packs built in the test (every cut of each, under
the sanitizers), and on the real files 8 fonts (kerning "LT" −3 in
FONT_IMPACTNORMAL), 481 textures, 2,437 strings, FRONTB.BUN read through the
archive, and the menus: OPTIONS' 7 rows in FE_ENGLISH's words, the settings
list showing 7 of the 12 rows with the selection always among them. `make
fecheck` adds `tests/check_tex.py`: every texture decoded by `hp2_tex` and by
`hp2_tpk.py`, all 481 identical.

`hp2fe` has been run only offscreen (`--shot`, SDL's software renderer) in a
machine with no display or controller: the screenshots match the layouts, but
the window, vsync and controller paths are untested until it runs on a desktop.

The two launcher SDL files compile cleanly but have **never been run**, because the
machine they were written on has no SDL. The first real run is the test: plug
in a controller and check that the detected name and button labels match the
pad in your hands.
