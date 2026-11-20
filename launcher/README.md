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
  (PS2 640x448 up to 4K), aspect ratio, window mode, VSync, frame cap, anti-
  aliasing, anisotropic filtering, FPS counter. Controls: scheme, button icons,
  dead zone, vibration. A front end only draws rows and forwards input.
- **Controller detection** that works out which button names to show. The same
  physical button is `A` on an Xbox pad, `Cross` on a PlayStation pad and `B`
  on a Nintendo pad — binding by position and labelling separately is the only
  way to get this right for everyone.
- **Axis shaping** with a dead zone that rescales the remaining travel, so the
  stick still reaches full tilt instead of being clipped short.

## Layout

| File | Needs SDL | Tested |
|---|---|---|
| `src/hp2_settings.c` | no | yes |
| `src/hp2_menu.c` | no | yes |
| `src/hp2_glyphs.c` | no | yes |
| `src/hp2_pad.c` | no | yes |
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
make             # build the launcher (needs SDL2)
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

## The settings file

```ini
# Need for Speed: Hot Pursuit 2 - settings
# Written by the launcher; safe to edit by hand.

[video]
internal_width = 1920
internal_height = 1080
aspect = 16:9       # auto, 4:3, 16:9, 16:10
window = borderless # windowed, borderless, fullscreen
vsync = on

[input]
scheme = modern     # modern, classic
glyphs = auto       # auto, xbox, playstation, nintendo, generic, keyboard
deadzone = 0.20
rumble = on
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

The two SDL files compile cleanly but have **never been run**, because the
machine they were written on has no SDL. The first real run is the test: plug
in a controller and check that the detected name and button labels match the
pad in your hands.
