# NFS Hot Pursuit 2 (PS2) — project continuity notes

Updated 2026-11-20 (late evening). Hand this to a new chat to pick up where this one left off. The newest work is in "Session 2026-11-16 to 11-19" below; sections after it are older and still accurate unless that section says otherwise.

## RESUME HERE (written 2026-11-21, late morning)
**Newest: def9243 (committed, not pushed; ec7fd02 too).** Suspension decoded (formats.md 12): block +0 progression, +4 rate kN/m, +8/+C bump/rebound damping, +10 anti-roll (pairs 0-1, 2-3), +14/+18 travel, +1C ?; force >= 0. Inertia = 4x4 matrix at 0x1F0 (body ctor 0x19C9B0 -> 0x19CC58 copies rec+0x1F0; +0x118 mass). hp2_drive: body heave/pitch/roll on springs about the model origin (CG height = ride height, i.e. model origin), loads from springs; gripping tyres pass drive torque unclamped (game: Fx = torque/r), brakes and sliding clamp to mu N; render leans body only. Sims: 911 60-0 ~33 m, 1.45 g; F50 0-81 mph in 4 s.
Next: user tries it; then opponents on the route (AI) or hp2fe event -> race.

## (previous) RESUME HERE (written 2026-11-21, morning)
**Newest: ec7fd02 (committed, not pushed).** Tyres decoded: PhysicsCar ctor 0x137100 (record ptr +0x4C; body 0x19C9B0 from rec+0x1E0; tyres 0x1A3F10 vtable 0x2B5C40 (wheel rec+0x120+0x20i, tyre block 0x1A0/0x1C0, rec+0x300); suspensions 0x1A5EA0 vtable 0x2B5CD0 (0x230/0x250); engine 0x18A7E8 (0x2B0); drivetrain 0x18AE70 (0x270); aero inline (0x2F0)). Tire::ComputeForce 0x1A4930: brake 4*[0x304]*pedal, handbrake 10*[0x308] kN m; slide when |(w r - vx, vy)| > 0.5 m/s -> F = load*tb[4]*surface_grip opposing slip; grip -> lateral table 0x1A4D28 (curves 0x310F60, 7 angles x 6 loads, x = angle*12/tb[0]*0.5 per 2 deg, load*0.8/tb[1], *2.5*tb[2]). Surface table 0x318C00 stride 0x90 (+0 name ptr, +0x24 grip, +0x28 rolling?). hp2_drive: planar 4-tyre model at 240 Hz, loads with guessed CG 0.45 m, separate |Fx|,|Fy| <= mu N (a friction circle spun the F50/Elise), yaw inertia rec 0x218; --arcade keeps the first model. Tests: 911 60-0 ~30 m, 1.5 g turn, F50 full lock drifts but no spin.
Next: user tries the new tyres; then suspension (0x1A5FBC update, blocks 0x230/0x250) or opponents / hp2fe event -> race.

## (previous) RESUME HERE (written 2026-11-21, after midnight)
**Newest: d4fe9e8 (committed, not pushed; b4f10e3 too).** User: "the drive felt good". Started on the game's handling. Found: physics is a rigid-body sim (strings PhysicsCar::ResolveForces(), PhysicsSystem::IntegrateEuler(), slot pools Engine/DriveTrain/Suspension/Aerodynamics/RigidBody created in 0x136f28; PhysicsCar serialise at 0x13744c: +0x20..0x2c 4 ptrs (0x1a5ed0), +0x30..0x3c 4 ptrs (0x1a3f60), +0x40 (0x18a8e8), +0x44 (0x18af10), +0x48 (0x1add48)); the label strings have no direct xrefs. carinfo.cpp 0x188DF0 builds a gear acceleration table (decoded; formats.md 12 "The drivetrain fields"): mass 0x1E0 t, final 0x278/0x27C blended by rear share 0x280, radius 0x12C/0x16C, torque 9 pts 0x2C0 kN m idle..redline, drag 0x2BC; upshift 0.95. hp2_drive now uses these (efficiency 0x270 0.8 assumed, traction cap 1.1 g, rolling 0.15, shift cut 0.25 s, governor at listed top speed). Simulated 0-60: LamboCop 2.6, F50 3.0, 911 3.3, M5 3.9, CL55 6.2. Unknown: 0x1A0-0x1DC tyre blocks (12,1,1,1.86,1.47..), 0x1F0-0x26C inertia/suspension, 0x284, 0x28C, 0x2E4-0x31C.
Next: decode the tyre/suspension parts (functions 0x1a3f60/0x1a5ed0 classes, find their update and what record fields they read) to replace the made-up grip; opponents; hp2fe event -> race. hp2track does not build in the device VM (no SDL there); the user builds it.

## (previous) RESUME HERE (written 2026-11-20, night)
**Newest: b4f10e3 (committed, not pushed): a race in hp2track.** hp2_race.{c,h}: progress = nearest point on the main route polyline (route record 0 kept in hp2_scene: route/route_n/route_loop/route_len); circuit deltas wrapped to +-len/2, lap when travelled >= len*lap; p2p finishes at len-10; wrong way = moving against the route direction > 0.5 s; countdown 3 s. hp2_hud.{c,h}: GL drawer for fe layouts (EXPAND view, left-half widgets anchored left, right-half right; align bit 0 read as right-aligned, inferred). Race screen 0xF1F1F8E5 in INGAMEB.BUN; prerace 0x936ACE42 (PRERACE_3/2/1/GO; GO parked at y 492, drawn at 288,183). Rev counter: SPEEDOMETER face = 3RDPERSON_<ceil(redline/1000) clamped 6..10>000 from HUDTEX.BIN; TACH_NEEDLE hub at texture (12,56), needle hangs down -> angle 180-130+160*rpm/face. hp2_engine.{c,h}: .MSH/.MSB sample banks decoded (documented in formats.md 5); engine banks: <pfx>{C,L}{E,X}{id/Lo,Md,Hi} + Horn; assumed sample rpm idle/30/55/80% redline (not in file; autocorrelation unreliable). hp2_drive: automatic gearbox (up at 94% redline, down when lower gear < 80%), rpm/load for sound; clutch slip at low speed. hp2track: --laps, --no-sound, loose --ingame/--hudtex/--msh/--msb; SDL audio queue ~60 ms. test_assets --enginedump: all 18 engine banks fill 12/12 slots. User had built 29e38ac's hp2track at 20:36 (no feedback yet).
Next: the game's own handling (physics in SLUS_203.62, carinfo unknown fields), opponents on the route, then hp2fe event -> race.

## (previous) RESUME HERE (written 2026-11-20, evening session, fourth part)
**Newest: 29e38ac (committed, not pushed; 0941879 may be pushed): a drivable car in hp2track.** hp2_car.{c,h}: car database (records with a name; 64 on the disc; wheels, size, gears, top speed mph) + assembly (body _A, side mirrors, rear plate in place; _TIRE_FRONT_A at origin, right-hand, scaled per wheel radius/width and mirrored for left; _BRAKE_FRONT/_REAR at hubs), hp2_car_texture remaps slots (SKIN1->SKINn, <CAR>_HEADLIGHT_LEFT->_HEADLIGHT, TIRE-><record>_TIRE, WINDOW_*->WINDOW). hp2_coll.{c,h}: 0x34131/2/3 parsed; ground = highest walkable poly <= zref+step under the point (walls 0x12xx/0x16xx or normal z < 0.3 excluded); push = circle vs each wall's furthest-vertex footprint segment; walls spanning -127..127 apply at any height (track 44 is at z 290: before this the car drove through them). hp2_drive.{c,h}: STAND-IN handling (not the game's): 1/120 s steps, 9 m/s^2 engine fading to top speed, brakes 14, reverse, lateral grip exp decay (handbrake 1.5, dirt/grass x0.7), yaw = v tan(steer)/wheelbase, slope pull, two wall circles with slide, pitch/roll from 4 ground samples. 0-60 mph ~4 s. hp2track: car by default (--car NAME default 911TURBO, --skin, --no-car; loose files --globalb/--cartex/--geometry), chase camera, C toggles free cam, title shows mph, --sim S[,STEER] drives before --shot. Disc loads GLOBAL\\GLOBALB.BUN, CARS\\TEXTURES.BIN (one pack for all cars) and the record's GEOMETRY.BIN/.LZC. Tests: test_assets builds a floor + wall and drives on it; real 911TURBO record checked; trackcheck now also compares ground heights on a 25-unit grid (identical on all 30 tracks; the Python side reimplements the same rule over hp2_scenery.collision()).
Next: user tries it on the desktop (window/keyboard/pad untested); then the game's own handling (find the physics in SLUS_203.62: carinfo fields 0x0F0-0x118, 0x1A0-0x1E0, 0x200-0x270, 0x2C0-0x310 torque curve?, 0x350-0x550), a HUD (speedo/gear from HUDTEX), engine sound (SOUND\\NEWE<NAME>.MSH/.MSB), opponents on the route, laps/checkpoints from routes, then hooking hp2track into hp2fe (event -> race).

## (previous) RESUME HERE (written 2026-11-20, evening session, third part)
**Newest: 0941879 (committed, not yet pushed): hp2track, the track viewer.** launcher/src/hp2_geom.{c,h} (models: VIF batches -> triangle lists, hp2_vert = pos/uv/PS2 colour (128 = 1.0)/normal; textures and materials by hash from the batch records), hp2_scene.{c,h} (sections + instances placed as hp2_scenery.py, records repeated in two sections drawn once, grouped per section by texture; sky = SKYDOME + PANORAMA kept apart, SKYDOME_ENVMAP not drawn; main route's first two points = start; FOG<MAIN>; hp2_comp_decode for .LZC), track_main.c (`make hp2track`, SDL2 + OpenGL 1.x display lists, one per section/texture, frustum cull by section box; texture x vertex colour x 2 via GL_COMBINE RGB/ALPHA scale 2; alpha only clear/solid -> alpha test 0.5, much in-between -> blended pass; fog linear start..start+600/density, a guess; --disc DIR --track NN loads TRACKS\\TRACKBnn.BUN/.LZC + TEXnnLOCATION/TEXnnTRACK.BIN; --scenery/--tex for extracted files; --shot/--cam). `make trackcheck SCENERY="..."` (tests/check_scene.py vs hp2_scenery.py): all 30 tracks identical per section, 0 failed batches. Fog record offsets in formats.md fixed (colour +0x54, start +0x58, density +0x70; were 4 high). Stub grew SDL_opengl.h. Container renders with `xvfb-run -a -s "-screen 0 1920x1080x24"` (apt: xvfb libgl1-mesa-dri libgl-dev). Untested on a real desktop: window, mouse, controller.
Next for the race: (1) user tries hp2track on the desktop; (2) a car in the world (hp2_geom already decodes GEOMETRY.BIN; car textures need the car pack + GlobalTextures in GLOBALB), (3) collision from 0x34130-33 to drive on, (4) a chase camera, then simple physics. Puddles on track 44 draw a sky texture cut out (probably the game's reflection layer; may need blending/envmap).

## (previous) RESUME HERE (written 2026-11-20, evening session, second part)
Standing instruction from the user: "just do what you recommend, no need to ask what's next." Run the work, commit when tests pass, tell the user the push command (`cd ~/nfshp2-tools; and git push`; the user pushes).
State: repo ~/nfshp2-tools (github z3rotheone/nfshp2-tools), HEAD **c4f383a** (f1e6759 pushed; c4f383a committed, not yet pushed). c4f383a: after Handling, an event shows its details (HP2_FEM_EVENT_INFO, evinfo_items: game panel screen a744ba7b, objective strings 63001/64000/64001/65000/65001, labels 66001-66007, 67001 "No reward"; pursuit objective is the port's own text); accept does nothing (no race), back returns to Handling. f1e6759: accept on an event -> car select with only that event's cars (roster: m->ev_mode/ev_cars, CARDB index->name table and MORE_NAMES in hp2_femenu.c; pursuit cars 41102/41116-41119; SPIDERHP named "Ferrari 360 Modena" by the port); after Handling it returns to the list (no race yet). **User decisions (2026-11-20): no tree view: Ultimate Fugitive / Ultimate Racer show their events as a plain list, 1 at the top (7506648); no event descriptions in the list.** 660797f: every career event's set-up decoded (formats.md "Event trees"; tools/hp2_eventtree.py lists them; hp2_tree.c reads the same; tests/check_tree.py compares, 64 events identical): 34A01 cars to pick, 34A02 races (track, II bit 9, laps v>>14&0xF), 34A03 conditions (kind 1 place<=value, kind 2 time, 4000 ticks/s), 34A04 rewards (car/track), 34A06 opponents (car, colour, skill), record +0x29 race type (0 Single, 1 Time Trial, 2 Lap KO, 3 Race KO, 4 Tournament, 5 Delivery, 6 Pursuit), +0x2A tier, +0x2C cop level.
Done this session:
- 6b2cb22: swizzle rule fixed in hp2_tpk.py + hp2_tex.c (swizzled only when width AND height are powers of two and >= one GS page: 128x64 at 8-bit, 128x128 at 4-bit). 25 textures disc-wide changed reading, all checked by eye (list in formats.md "Swizzling"). Badge textures found by their full names (pack names are cut at 23 chars): PURSUIT_/SPEED_EVENT_PANEL_LOCKED/UNLOCKED/WON.
- 1e4dbf4: **event trees decoded** (formats.md "Event trees"; launcher/src/hp2_tree.{c,h}). 34A03 = prerequisite links; 34A01/02/04/06 = race parameters (34A02 byte 0 = track number), not lines. Layout = a 20x10 ' '/'x' grid per tree in SLUS_203.62 (Championship 0x31B3C0, Hot Pursuit +208; found by content), row = link depth for all 64 events, x = 130*col, y = 350*depth; connectors = per group a bar at mid-height + verticals. hp2fe `--exe SLUS_203.62` (or found next to `--disc`) draws neighbouring badges and red connectors round the current event, clipped to the map panel; map (scale 1.25) scrolls with the event's column/row.
- Executable tooling in the container: /home/claude/exe/hx.py (capstone helpers: dis, words, xrefs for lui+addiu pairs, func_of, cstr; gp = 0x3358F0; text va 0x100000 = file 0x1000, data va 0x2AB000 = file 0x1AC000). Gone with the container; rebuild from this note (pip install capstone --break-system-packages).
- Scratch left on the PC: ~/hp2work/swizzle_check/ (4 comparison PNGs; the user may delete it).
Next, in order:
1. (Tree screenshots no longer needed: the user chose a list.) Possible list extras: a details line or popup per event (blurb strings World Racing 70064+r, Hot Pursuit 70095+r, long, with @ = new line and $TARGET_TIME), lock/won state once there is progress.
2. ~~Event start data~~ done (660797f). Still unknown: 34A02 bits 18..25 and 26..31 (0 or 25..100 / 25, 30, 40), record bytes +0x2B..+0x3A (copied to the race set-up at 0x260..0x288 by 0x1FEE20), car reward flag (1, 9 once). ~~Accept on an event -> car select with its cars~~ done (f1e6759). ~~Event summary~~ done (c4f383a). Next: the race itself (3D: track scenery + car models + collision, all decoded in tools/), starting with a track viewer.
3. Rewards (no field yet), POPUP screen (037f3133), track elevation data, then the race itself.
4. Keep README and header comments current; add tests per flow.
Workflow that works: request folder access to ~/nfshp2-tools and ~/hp2work at session start (`device_request_folder_access`), then `device_request_delete_permission` for /home/z3ro/nfshp2-tools BEFORE committing. In the container: `apt-get install libsdl2-dev`; stage launcher/, tests, tools/hp2_tpk.py, docs/formats.md, ~/hp2work/iso/SLUS_203.62 (for --exe) and the three data files (~/hp2work/extracted/021_14c57a19.bin FRONTB, 302_4c937012.bin GLOBALB, 003_0e1049ed.bin FE_ENGLISH); `make hp2fe test_assets`; screenshots via `SDL_VIDEODRIVER=dummy ./hp2fe --files F G L --no-music --config /tmp/hp2.ini --size 1280x960 --fit fit --shot out.bmp --keys accept,accept,accept` (that key path = Ultimate Fugitive tree; `accept,down,accept,accept` = Ultimate Racer tree). Asset tests: `./test_assets --frontb F --globalb G --lang L`; `python3 tests/check_tex.py ./test_assets F G`. Send changed files with device_commit_files (expectedMtimeMs from staging), verify md5 on the device, run `make test` + `make stubcheck` there, `make clean`, then commit with device_bash git. Commit trailer: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and the session's `Claude-Session:` line from its system reminder. Afterwards `find .git -name "*.lock" -delete; find .git -name "tmp_obj_*" -delete`. device_commit_files refuses Makefiles. Shell is fish. Untracked "Claude outputs/" in the repo is left alone. The repo tracks six `launcher/.fuse_hidden*` files (stray; ask the user before removing them from git).
Ground rules: repo holds tools and docs only (no game assets, discs or executables); always patch a COPY of the ISO; never overwrite a file without checking the name is free; real paths, no placeholders; commit only when tests pass.
Key facts: FRONTB layouts chunk 0x80010200 (41 screens, 640x480; lower layer = in front); textures/fonts addressed by hp2_fe_hash(UPPERCASE full name) = x*33+c from 0xFFFFFFFF (pack names are cut at 23 chars: hash the full name); missing textures draw white; HP2_FEDRAW_SOLID (0xFFFFFFFF) is a tintable flat stand-in; screen ids: Title b5761d00, Main ae0f8aa1, Hot Pursuit 5b35f533, World Racing 033e7771, SELECT_CAR b5074ab4, SELECT_TRACK 15417b53, CHALLENGE 2d62cfe2, You're The Cop e55f1297, RACE_SETUP d1eadbca, TREE_VIEW 492e86e9, POPUP 037f3133. Event tree data: docs/formats.md "Event trees". Trees: FRONTB 0xd97108 = World Racing (31 events), 0xd98e3c = Hot Pursuit (32).

## Goal

Reverse-engineer **Need for Speed: Hot Pursuit 2, PS2 NTSC (SLUS-203.62)** toward a native PC/Android port the user can play and enhance:

- swap the music
- use upscaled textures
- have a launcher for pre-game settings (internal resolution, aspect ratio, modern controls, automatic controller-glyph detection), with the same settings available in game

## Ground rules

- The repo holds **tools and documentation only**: no game assets, disc images or executable. `.gitignore` blocks them.
- Always patch a **copy** of the ISO, never the original.
- Commits end with `Co-Authored-By` / `Claude-Session` attribution lines, as the session asks (use the lines in the current session's system reminder).
- Never overwrite a file without checking the name is free.
- Repo files keep their existing line endings (`formats.md`, the launcher and most tools are LF; some CSVs are CRLF). Check with `file` before patching.
- Claude commits on the PC through the device bridge; **the user runs `git push`** (push from the device shell is blocked). Never treat a check that reads back its own output as proof (see "Lessons").

## Environment

| What | Where |
|---|---|
| Repo | `github.com/z3rotheone/nfshp2-tools`, cloned at `~/nfshp2-tools` |
| Work folder | `~/hp2work` |
| Extracted archive entries | `~/hp2work/extracted/` (`NNN_hash.bin`) |
| Disc files, incl. the executable | `~/hp2work/iso/` (`iso/SLUS_203.62`) |
| Modded image | `~/hp2work/modded.iso` |
| PCSX2 HD texture pack | `/run/media/z3ro/10192026/PS2/PCSX2 Stuff/textures/SLUS-20362/` (external drive; `dumps/` PNG, `replacements/` DDS). The PCSX2 config texture folders are empty. |
| Texture name map | `~/hp2work/map2.csv` (current, after the palette fix; `map.csv` is the old run) |
| Scenery export | `~/hp2work/scenery196b/` (OBJ of entry 196, textures resolved; `scenery196/` is an old export with hash names, don't use) |
| Check outputs | `~/hp2work/check_all2.txt` (58 model files), `check_scenery.txt`, `export196b.txt` |
| Chunk tree printer | `~/Downloads/chunktree.py` (not in the repo) |
| Hash name list | `~/hp2work/names.csv` (made by `hp2_geom.py --guess -o`) |
| Scenery placement | `~/hp2work/check_placement.txt` (all 8 files), `check_scenery_all.txt` (`--check` on 198–223), `placed196/` (sections 41–45 placed; `top.png`, `s41_three4.png` renders) |

- **OS:** CachyOS (Arch). Shell is **fish**: no `VAR=value cmd`, and quote paths with spaces.
- **pip** is blocked by PEP 668. Use pacman, a venv, or `--break-system-packages`. `xxhash` is already installed.
- **Emulator:** PCSX2.
- **git:** repo-local identity is set. Pushing needs `gh auth login` + `gh auth setup-git` (not yet done; password auth fails), so local commits may not be on GitHub yet.
- **Texture PNGs:** `~/hp2work/textures/<pack>/` (`hp2_tpk.py extracted textures`). All cars share pack `012_0287bf4f`. Pack names are cut at 23 characters.
- **Remote access: works** (since 2026-11-14) through the Claude desktop app. Claude gets a sandboxed Linux shell (`device_bash`, not the host) that sees only granted folders, mounted at `$HOME/mnt/hp2work` and `$HOME/mnt/nfshp2-tools`. It has python3, numpy, PIL; **no xxhash** in the 11-15 session (install before running `hp2_pcsx2.py`). No Blender, PCSX2 or display. Deleting is off until Claude asks (`device_request_delete_permission`); git commit needs it for its lock files. Granted for `~/nfshp2-tools` on 11-15, so Claude committed directly. Files Claude sends with `device_commit_files` must use a **new staged file name each time**: re-sending the same name wrote the stale copy once.

## Session 2026-11-16 to 11-19 (newest work; read this first)

Repo state: everything below is committed and **pushed** (latest d21e310). Working tree clean. Claude's working scripts for the executable live in the cloud workspace (`/home/claude/exe/`), not the repo.

### Bigger-than-original archive entries (confirmed in game)

- `tools/hp2_iso.py install ISO ENTRY FILE [--force]` (commit 0b246ff, formats.md section 14 "Growing an entry"). Slot = to the next entry or the disc end. Written in place if it fits, extended in place if already last, otherwise **appended at the disc end** and ZDIR repointed. The loader (`bLoadDirectoryFile`, 0x255ca0) adds ZZDATA's start sector to each offset and never bounds-checks, so sectors past ZZDATA work.
- Growing the image also updates the ISO9660 PVD volume size (sector 16, +80 LE / +84 BE) and UDF: partition descriptors (sectors 34 and 50, length at +192), integrity descriptor (sector 64), closing anchor in the last sector; tags re-checksummed (byte 4) and CRC-CCITT 0x1021. The old copy stays as dead space. Twin ids: 11/228, 12/62, 18/225 (the tool prints a note).
- **Confirmed:** FE_ENGLISH.BIN grown (107,576 to 111,680 bytes) boots and shows "HP2 BIG FILE OK!". `~/hp2work/modded_grow.iso`.

### English-only extraction and rebuilds

- `hp2_extract.py --english` skips 20 files (8 speech, 5 FE_*.BIN, 7 intro clips) = 1,317,121,016 bytes (commit bfed8c2). Unnamed entries are always kept. Swedish has text and an intro clip but no speech.

### Intro skip (confirmed in game 2026-11-19)

- `tools/hp2_skipintro.py ISO|EXE [--check|--undo]` (commit 1e521bc). Patches the word at file offset 0x70094 of SLUS_203.62 (the `beqz` at 0x16F094, `0x10400007` to 0) so the movie-sequence object always takes state 1 (TITLE_IMAGE) instead of state 2 (EALOGO then NFSINTRO). Refuses non-NTSC-U executables. **User confirmed `~/hp2work/modded_nointro.iso` boots straight to the title screen.**

### Launcher: settings and an options-menu model (commit e26e2af)

- `launcher/src/hp2_settings.*` gained frame_cap (0 = no limit, else 20..1000, default 60), msaa (0/2/4/8, default 4), aniso (1/2/4/8/16, default 8), show_fps. New `hp2_menu.*` = 12 rows (internal resolution presets from 640x448 PS2 to 3840x2160, aspect, window, vsync, frame cap, msaa, aniso, show fps, scheme, glyphs, deadzone, rumble) with `hp2_menu_row_count / section / label / kind / value / adjust / needs_restart`. Tests: `make test` (178 + 810 + 1058 checks, 0 failures), `make stubcheck`. `--menu` flag in `main.c`. No SDL in the core. The SDL window part has never run with real hardware.

### Front-end screens (commit eddb39d, formats.md section 17, `tools/hp2_fescreens.py`)

- `FRONTB.BUN` (entry 21): chunk `80010200` holds **41 screens** on a 640x480 canvas; each screen is `00010203` header (id at +8, canvas at +0x18, u16 widget count at +0x1C) then widgets `00010212` (text) / `00010210` (image). Screen id = djb2 hash (seed 0xFFFFFFFF, x33) of the upper-case name; **21 named**, 20 left as hex. Widget fields are listed in the `hp2_fescreens.py` docstring.
- Text widgets carry a font id (`FONT_IMPACTBIG` 0x2D213565, `FONT_IMPACTNORMAL` 0x5F3A4A3C), RGBA and a string id into FE_<LANG>.BIN (ids like 100001 "Audio", 140001 "Track Map", 110001 "Menu Music"; larger than 10000-12436).
- OPTIONS has 7 rows, DISPLAY_OPTIONS 9, AUDIO_OPTIONS 11; rows 37 px apart from y=71, values at x about 500.
- **The layout only places widgets.** Each screen is a C++ class registered in a static initialiser (0x1F552C); adding a row on the PS2 build needs new MIPS code. Decision: **the native port draws these layouts itself** and binds rows to `hp2_menu`. Renaming a row is a text edit in FE_<LANG>.BIN.

### Speech (corrected, formats.md section 16, `tools/hp2_events.py`)

- Archive names: 361/362 English BAN/DAT, 363/364 French, 365/366 German, 368/369 Italian, 370/371 Spanish. (An earlier note labelled 365 as English; wrong.)
- BAN index record = `(tag, dat_offset, 0, desc_offset)` at 8+16i. **Descriptors are variable length**, not 44 bytes: `u16 line, u16 variant (0xFFFF = main), u8 takes, u8 cues`, 16 bytes of timing, then 5-byte cue marks (`u16 ms, u8 index, u8 1, u8 flag`) ended by `70 70`. About 480 line numbers cover about 1,800 clips. `hp2_events.py EVT --lines BAN.PCK` writes the line CSV; `~/hp2work/speech_lines_en.csv` has the 1,805 English clips.
- **Entry 367** is the unnamed speech *source* table (201 events, 69 parameter types, 508 line names). The compiled **EVENTS.EVT is entries 18 and 225** (48,092 bytes): header `03 08 3C 07`, `u32 201`, table at 0x14 with 100, 500, then 201 event offsets. **Its rule format is not decoded** (matcher `iSPCH_MatchSample`, loader `iSPCH_BindData`).

### Working files and housekeeping

- `~/hp2work/`: `modded_text.iso`, `modded_grow.iso`, `modded_nointro.iso`, `en_big.json`, `FE_ENGLISH_big*.bin` (test inputs), `speech_lines_en.csv`. `~/hp2work/_to_delete/` holds `seq_test.iso` (about 3 GB), `e4_big.bin`, `e3_bigger.bin`: the user may delete them.
- Device shell cannot delete without `device_request_delete_permission` (granted for `/home/z3ro/nfshp2-tools`, not the sandbox path). After each commit remove leftovers: `find .git -name '*.lock' -o -name 'tmp_obj_*' | xargs -r rm -f`.
- Tool quirks: `xxd` is not installed in the workspace; `device_commit_files` needs freshly staged files (verify md5 both sides).

### Later the same night (c1fd976, 1a9ecb3, 95752d9, d21e310, all pushed)

- **hp2fe runs on the desktop**; keyboard, d-pad and A/B buttons confirmed by the user.
- **21:9 aspect** (64:27) after 16:10 in `hp2.ini` and the menu; with 21:9 the resolution row offers 640x448, 1720x720, 2560x1080, 3440x1440, 5120x2160; switching aspect keeps the height (1920x1080 <-> 2560x1080).
- **Selected row as in the game** (from the user's PCSX2 screenshots): orange label (248/128/7), `MI_OVERSCORE` white line above and `MI_UNDERSCORE` orange line below (centres 0.5 above / 32.5 below the label y), its row mask lifted, help line in the ticker. OPTIONS help = string 100019 + n. OPTIONS shows 6 rows (Languages hidden, as on the US disc). **The user chose arrows on every row** (the game omits them on on/off rows; noted in code as deliberate).
- **Pages:** Display -> Video (8 rows), Controller -> Controls (4), Audio -> Audio on the AUDIO_OPTIONS layout: 7 volumes as the game's sliders (SLIDER1_BAR 128 px at x 452, marker centred at 452 + 128 x volume, matches the screenshot), Audio Mode (stereo/mono/surround), Default Volumes button. `[audio]` section in `hp2.ini`. Jukebox / Default Jukebox rows left out until a jukebox exists. Volumes are stored but nothing plays yet.
- **Bug fixed in d21e310:** hp2fe treated `hp2_settings_load`'s success (returns 1) as failure, so a saved `hp2.ini` was never read back.
- Tests: `make test` = 192 + 810 + 1863 + 3260 + 172; `make fecheck` = 428 real-data checks + 1,549 widgets + 481 textures identical.
- **Bridge lesson:** after the desktop reconnects, delete permission is gone; ask again (`device_request_delete_permission`) **before** any commit or `make clean`, or git leaves `.git/HEAD.lock` and `tmp_obj_*` files and test scratch files (`test_settings_tmp.ini`, now in .gitignore) get committed.
- **Workspace lesson:** when re-syncing from `/mnt/user-data/uploads`, copy only the files staged in that call; the folder keeps older staged copies (this overwrote newer `hp2_fe.c/.h` once; restored).
- The user's screenshots of the real Jukebox screen (genre with arrows, song title + artist, Play Status "Hot Pursuit" / "Menu/Racing") are the reference for a future jukebox page.

### hp2fe: the Options menus drawn by the port (commit 66736e6)

- `launcher/hp2fe` (`make hp2fe`, needs SDL2 >= 2.0.18): OPTIONS with the game's own rows and FE_ENGLISH text ("Save/Load", "Player Names", "Languages"); *Display* / *Controller* open the 12 `hp2_menu` rows in the DISPLAY_OPTIONS layout (7 visible, scrolling inside `SCROLLING_MENU_CLIP_REG`, up/down arrows, left/right arrows on the selected value, title = section "Video"/"Controls", help ticker). Left/right change settings; `hp2.ini` saved on leaving the list. 4:3 aspect -> FIT, else EXPAND (backgrounds tiled into the sides); `--fit` overrides; window mode applies live.
- Run: `./hp2fe --disc ~/hp2work/iso/NFSHP2` (reads ZDIR.BIN + ZZDATA.BIN by path) or `--files 021 302 003 extracted entries`; `--shot OUT.bmp --keys down,down,accept` renders offscreen.
- **Never run on a desktop yet.** Only offscreen (SDL software renderer) in Claude's cloud workspace, where `apt-get install libsdl2-dev` works; the PC's device shell has no SDL. User needs `sudo pacman -S sdl2` (or sdl2-compat) then `make hp2fe`.
- New C99 modules (no SDL): `hp2_tex` (texture packs, identical to `hp2_tpk.py` on 481 FE+global textures, all 5 layouts), `hp2_font`, `hp2_lang`, `hp2_zdir`, `hp2_femenu` (menu logic -> draw list). SDL: `hp2_fedraw` (SDL_RenderGeometry), `fe_main.c`. `hp2_fe_item` lives in `hp2_fe.h`.
- Tests: `make test` = 178 + 810 + 1058 + 3388 + 172; `make fecheck FRONTB=~/hp2work/extracted/021_14c57a19.bin GLOBALB=~/hp2work/extracted/302_4c937012.bin FELANG=~/hp2work/extracted/003_0e1049ed.bin DISC=~/hp2work/iso/NFSHP2` = 285 real-data checks + 1,549 widgets + 481 textures identical. `make stubcheck` covers the new SDL files (stub extended).
- The `device_commit_files` tool refuses Makefiles ("protected"); edit them in place with device_bash.

### Front-end findings (formats.md section 17 table, new section 18)

- **Widget fields:** +0x29 visible, +0x2A layer (**lower = in front**), text +0x58 alignment (2 left, 4 centred on x), image +0x58 four corner colours (TL, TR, BR, BL; PS2 scale) and +0x70 four UV pairs; image size = texture size x scale. Text colours: rows 0x50, selected row 0x80 (`ANIMATING_MENUITEM`, parked at y 531), values 0x64.
- **Fonts (chunk 0x15010):** 4 in FRONTB (IMPACTSMALL = menu rows, IMPACTBIG = titles, IMPACTNORMAL, EVENTNUMBERS), 4 in **GLOBALB** (FONT_A, BUTTONSOUT = button glyphs, FONT_HUD = help ticker, FONT_STATS = values). Font id = name hash = its texture's hash. Header: id, n, k, glyph offset, kern offset (offsets from the chunk's id word). Glyph 32 bytes: u16 char (Latin-1), u16 kern count, f32 x y w h advance xoff, u32 first kern. Kern pair: u32 char, f32 adj, listing the character **before** (T lists L: "LT" -3).
- The two `80034A00` chunks are the "Championship" tree view, not fonts.
- Lines at y 353 / 381 frame the help ticker, not the selection. DISPLAY_OPTIONS scrolls 7 rows.
- Strings: `$RGB(r,g,b)`, `$FONT(NAME)`, `@` = new line. `$RGB` values treated as 0..255 (not checked in game).

### hp2_fe finished (commit ee05ea6, not yet pushed)

- `launcher/src/hp2_fe.{c,h}`: pure C99. `hp2_fe_parse` finds `80010200` (also inside other containers), collects `80010201` screens; `hp2_fe_free`, `hp2_fe_find` (any case) / `find_id`, `hp2_fe_row` / `row_count` (MENUITEM_n by name), `hp2_fe_view_make` / `hp2_fe_to_window`. The `s->count` bug is fixed (separate fill counter; header count is only a hint). Failures leave nothing allocated.
- Viewport: FIT = min scale, centred, bars; STRETCH = per-axis; EXPAND = height scale, centred, margins in canvas units for a background; on a window narrower than 4:3 EXPAND falls back to FIT. On a wide window FIT and EXPAND place the canvas the same; the renderer decides bars vs background.
- Tests: `make test` (178 + 810 + 1058 + 2983 checks, 0 failures; `test_fe` also cuts every prefix and damages every byte, clean under ASan/UBSan). `make fecheck FRONTB=~/hp2work/extracted/021_14c57a19.bin`: 41 screens, OPTIONS/DISPLAY_OPTIONS/AUDIO_OPTIONS = 7/9/11 rows, and `tests/check_fe.py` finds **all 1,549 widgets identical** to `hp2_fescreens.py`.
- **Correction:** rows are about 37 px apart from y=71 but hand-placed (109, 181, 220, 294: up to 2 px off), and the file stores them **out of order** (OPTIONS has MENUITEM_7 before _6). Always look rows up by name. Row x is 153 (152 on AUDIO_OPTIONS).
- `~/hp2work/wip_hp2_fe/` is now an old copy; the repo has the current one.

### Next steps (in order)

1. ~~Finish `hp2_fe`~~ done (ee05ea6); ~~renderer~~ done (66736e6). User: `git push`.
2. ~~Desktop run, controller, selection look, Audio page~~ done.
3. Menu follow-ups: Jukebox page (music bank track names + the user's screenshot), Save/Load, Player Names, Credits; the ticker's sideways scroll for long help lines; `$RGB` scale.
3. Lower priority: decode compiled EVENTS.EVT rules; name the 20 unnamed screen ids; unknown scenery chunks (35020/35021, 34017, 34106, 34121, 34122, 34130, 80034019, 80034425); ghost replay format; car record unknown fields.

## Done and verified

### Music: working in game

- `hp2_music.py replace` swaps a track; `hp2_iso.py install-bank` writes the bank back into the ISO in place.
- The game plays bank **entry 322** (`SOUND\JUKEBOXM.JBC`), not 321.
- Stereo interleave **differs per bank**. Bank 321 uses 16 bytes; bank 322 is not 16 or 2048. Measure it with the probe; the writer fills a fixed grid.
- **Result:** Linkin Park "Faint" replaced Rush "One Little Victory" (track 15) and plays clean in game and in the jukebox.

### Textures

- `hp2_tpk.py` decodes all 13,871 textures and has a write path (`write`, `verify`). The writer is only tested on synthetic packs.
- The user's PCSX2 pack is **real HD art, redrawn** (mostly 4–8×). Matching by appearance (`hp2_match.py`) **fails** on it: 0 of 13 checkable matches were right.
- `hp2_pcsx2.py` recomputes PCSX2's own filenames: XXH3-64 of the texture as laid out in GS memory, plus the palette hash. **1,214 of 1,441 files are named exactly**, and the palette check agrees for 1,212.
- **After the palette fix (`~/hp2work/map2.csv`): 1,372 of 1,441 named**, palette hash agreeing for 1,370. 69 unnamed (26 are 4-bit 64×32); with every pack decoding right, these are probably runtime-built textures or skipped sizes. Parked.
- **The HD pack (on the external drive, `/run/media/z3ro/10192026/PS2/PCSX2 Stuff/textures/SLUS-20362`, files are `.dds`) has no player-car paint.** Car skins: traffic only (sedans, vans, buses, taxis...) plus BARCHETTA/SPIDER dash skins. Player cars get only lights (`<CAR>_HEADLIGHT`, `_ON`, `BRAKELIGHT`, `_ON`). Port plan: HD pack by name where it has art; upscale player skins from our PNGs.
- **Palette/offset fix (patch 8):** pixel and palette offsets count from the **end of the `0x11` filler** at the start of data chunk `0x30300004`. The old `doff + (dsz - total)` was wrong for packs 002, 011, 012 and 022, which have spare bytes after the last pixel (128 for 012, 1024 for the others). Real palettes have alpha `0x7F` in every 4th byte. Pack 012 went from 43 to 686 of 782 real palettes. Packs 002 (LOADING logo), 011 (dash gauges, 8-bit swizzled) and 022 (track preview TRACK11B) fail the 0x7F test but **decode correctly**: checked by eye on 2026-11-14. They use real alpha (gauge faces ~20%, soft edges). **All packs decode correctly**; the 0x7F rule only fits solid textures.
- Track packs keep palettes at the start; the car and front-end packs put each palette right after its pixels.

### Car models: fully decoded, no VU1 microprogram needed

`hp2_geom.py` decodes them completely:

- **Batches:** each `MSCAL` is one batch of at most 30 vertices.
- **Batch layout by VU address:**
  - 0 — header: N, (N-1)/4, 3N, material×4
  - 1 — strip masks
  - 2 — positions, transposed x4/y4/z4, then N%4 leftovers in their own block. With N<4 the leftover block is the only one.
  - 32 — UVs as `u0 v0 v1 u1`, then a V2 for an odd last vertex
  - 52 — 5551 colours
  - 92 — normals, signed bytes in transposed groups, padded
- **Strip masks:**
  - The first word counts from the start: bit 14 is vertex 0, down to bit 0 for vertex 14.
  - The second word counts from the padded end P: bit 1 is vertex P, up to bit 17 for vertex P-16.
  - A strip start is always a flagged pair; vertex 15 gets filled in when a pair is cut in half.
  - Winding alternates within a strip.
- **Check:** every batch's 64-byte record in chunk `0x00034004` stores vertex and triangle counts.
- **Result:** 16,789 of 16,789 batches across all 44 cars (about 260,000 triangles) match those counts.
- **The game's own totals disagree in 6 parts:** the part header's triangle total exceeds the sum of its batch records by 2–32. That is the file disagreeing with itself.
- **Header records = attachment points** (30 on the 911): flares, plus HEADLIGHT/BRAKELIGHT_LEFT/RIGHT, FLARE_REVERSELIGHT_*, FLARE_BRAKELIGHT_MIDDLE, WINDOW_FRONT/REAR/LEFT_FRONT/..., CORNER_LEFT_FRONT/..., SIDE_MIRROR_*, LICENSE_PLATE_FRONT/REAR, EXHAUST (×2), ENGINE. Flags 0x101 = brake, 0x102 = reverse. Positions fix the axes: **+x front, +y left, +z up**. No wheels. `68100815` (beside ENGINE) unnamed.
- **Batch record (`0x00034004`, 64 bytes):** byte 0 (u32) = **texture index**; 8 = VIF packet offset; 12 = size in quadwords; 15 = flags (bit 0 is NOT has-normals: 8/16 parts fail; bit 7 set on windows); 28 = draw-group counter (not material); 29/30 = verts/tris; 31 = **material index**, 0xFF = none (VU header then shows 0x3F). Records sort by texture then material. `--check` verifies both on every part; `--records` dumps them raw.
- **Material → texture is per batch, not "everything uses SKIN1".** The OBJ export groups by `<material>__<texture>` and points each at its own PNG.
- **Runtime texture slots:** the model names `<CAR>_HEADLIGHT_LEFT/RIGHT`, `<CAR>_BRAKELIGHT_LEFT/RIGHT`; the pack holds `<CAR>_HEADLIGHT`, `_ON`, `_DAMAGE0` (a few cars also have real _LEFT/_RIGHT brake art). Generic `TIRE` → `<CAR>_TIRE`. `WINDOW_*` and `CARBOTTOM` have no art anywhere; `BROKEN_CARWINDOW` exists in pack 252. Exporter: `--lights off|on|damaged`, glass/dark stand-ins in the `.mtl`. Inferred from names, executable not checked.
- **911 pack:** all car textures live in shared pack `012_0287bf4f` (783 textures). SKIN1–SKIN9 (512×320, 4-bit, linear) each with a 128×80 `SKINnB`, `SHADOWIG`, `SHADOWFE`, `TIRE`, `WHEEL_BLUR`, `DASHWINDOW`, plus `911TURBOHP_*`. Skins = paint colours: 1 silver, 2 grey, 3 gold, 4 red, 5 black, 6 dark green, 7 dark blue, 8 navy, 9 two-tone grey. `--skin N` exports with SKINn.
- **Verified in Blender (2026-11-14):** the 911 with SKIN3 has the paint, panel lines, headlights and brake lights all on the right faces. **Material → texture mapping confirmed.**
- **PS2 lighting:** pixel = texture × vertex colour / 128. Vertex colours are 5-bit baked shading (no paint), widened <<3 by the GS, so 16 = 1.0 and 31 = ×1.94. Skins are stored at about half brightness. `tools/blender_ps2_shade.py` adds texture × vertex colour × (31/16)^2.2 (linear) to every material; checked in Blender, looks right.
- **OBJ is written Y-up** (front toward -Z); `--game-axes` keeps stored axes.
- **Name lists:**
  - chunk `0x0003401d` is the **material** list, and a batch's material index selects from it
  - chunk `0x00034006` is the **texture** list
- **Commands:**
  - `--check` / `--survey` verify against the stored counts
  - `--obj DIR` exports OBJ with vertex colours, UVs, normals and material groups
  - `--textures PNGDIR` adds an `.mtl`
  - `--names`, `--guess` and `--known` handle hash names

### Executable findings (2026-11-15, capstone in the cloud workspace)

- Helper scripts live in the cloud workspace (`/home/claude/exe/hp2_exe.py`, `annot.py`), not the repo: `strings`, `xref` (lui+addiu pairs), annotated disassembly with string and call names. The PC shell has no capstone/xxhash; stage `SLUS_203.62` + `ghidra/functions*.csv` to the workspace and work there.
- **Car texture table (0x188790):** paint slot SKIN1 ← `<CAR>_SKIN<n>` (n = colour byte 5); lights: `<CAR>_HEADLIGHT/_ON/_DAMAGE0`, `_BRAKELIGHT/_ON/_DAMAGE0`, `_REVERSELIGHT/_ON`; shadow `_SHADOWFE` (front end) / `_SHADOWIG`; tyre/wheel blur from the **record** name (911TURBOHP_TIRE); `_LICENSE_PLATE`.
- **Windows (0x11E860 → 0x11FF68):** 12-entry substitution table; each WINDOW_* slot draws `WINDOW`, then `WINDOW_DAMAGE0/1/2` by damage (table 0x2E0380). These + `CARBOTTOM`, plates, `BRAKESFRONT/REAR`, `TIREBACK`, `SKRATCHES` are in **GlobalTextures.tpk inside GLOBALB.BUN** (entry 302). INGAMEB.BUN and FRONTB.BUN also carry packs: `hp2_tpk.py embedded extracted embedded` then `hp2_tpk.py embedded textures` → `textures/302_GlobalTextures/`, `060_InGameTextures/`, `021_FrontendTextures/`. 911 hashes now named: cd677a20 BRAKESFRONT, c8c5a8a4 TIREBACK, 3d743a07 SKRATCHES, ece04847 WIPER01 (no art on the disc).
- **Car database** (chunk 0x34600 in GLOBALB.BUN, `carinfo.cpp`, `tools/hp2_carinfo.py`, `~/hp2work/carinfo.json`): 64 × 0x560 records; names, model path, make, 4 wheels (hub xyz, radius, width, index, toe), size, final drive, gears, rpm, top speed, engine sound. 996 Turbo values exact. Wheels inside all 43 models.
- **Whole cars:** `tools/hp2_car.py GEOMETRY.BIN GLOBALB.BUN OUT.obj --textures textures` places `_TIRE_FRONT_A` + `_BRAKE_*` at the 4 hubs (mirrored left, scaled to stored radius/width by default; `--no-scale`). `~/hp2work/cars/`. `render_obj.py` mirrors its preview images (the plate's UVs are right).
- **Track list** (chunk 0x34201, `tools/hp2_tracklist.py`, `~/hp2work/tracklist.json`): 30 tracks, names ("Coastal Parklands T11"), scenery track, point-to-point flag, variant (overcast/sunset), length, location (Parkland, Desert, Greece, Alpine, Tropics). n+3 = same circuit, other time of day; `TN_TRACK%dR` unused.
- **Sound banks named:** `SOUND\<NAME>.MSH` + `.MSB`: COMMON, FRONTEND, INGAME, PARKLAND, DESERT, GREECE, ALPINE, TROPICS, `NEWE<engine>` (from the car database's engine field). 322 of 372 archive entries named.
- **Menu text** (commit 931b165, §14, `tools/hp2_lang.py`): FE_*.BIN = chunk 0x39000: n=2,437 strings, key table (name hash of the English wording, sorted signed) + id table (10000…) + Latin-1 text with `$RGB()`/`$FONT()` codes. All 8 rebuild byte-identical; `--write IN.json OUT` edits. **Confirmed in game** 2026-11-15: `modded_text.iso` shows "HP2 MOD OK!" on the title screen (install with `hp2_iso.py install-bank ISO 3 FILE`; same size required).
- **Video** (commit 1e330f1, §15, `tools/hp2_video.py`): MV30 = MidiVid 3.0 (20 files), entry 390 = MVDV; ffmpeg decodes both. MV30 frames come out upside down (vflip), MVDV upright. All 21 converted: `~/hp2work/videos/*.mp4`.
- **Speech** (commit 25abe43, §16, `tools/hp2_speech.py`): `<LANG>BAN.PCK` index (n × 16: 0, DAT offset, 0, descriptor offset; 44-byte descriptors, word 0 = 0xFFFF0000 + line no.) + `<LANG>DAT.PCK` = one VAG per line (mono 24 kHz). ~1,800 lines × 5 languages. `~/hp2work/speech_en/` has 6 sample WAVs (spectrogram = clear speech). EVENTS.EVT and entry 367 (event names) not read.
- **Audio:** bank 322 interleave measured = **32,768 bytes** on all 23 tracks (commit f08ca2f).
- **Texture writer verified on real data:** 13,871 + 598 embedded textures re-pack byte-identical.
- **Lights / fog** (commit 47c171f): `80034015/34018` = 96-byte lights (shadow lights, tunnel lights, one ambient "Sun"); `34250` = 128-byte fog records (MAIN, TUNNELnn, SMOKEnn; colour, start, centre, radius, density). Overcast variant: greyer, denser fog; variants also differ in 35020/35021 (unread) and sky dome model.
- **Other models:** INGAMEB.BUN = drivers/cops (3 LODs), SPIKEBELT, ROADBLOCK_BARREL, SMOKEBOMB, ROCKET, TICKETBOOK, BUSHSHAKE, markers.

### Names

- **Texture names:** 104 named straight from pack names. The two-piece guesser (`--guess`, meet-in-the-middle over pack names, car names and executable strings) names **133 of the 139** still unknown.
- **Materials:** 7 of 27 guessed (HEADLIGHT, METAL, REFHEADLIGHT, VENTS, WHEEL_BLUR, SPIDERGRILL). Known before: CARSKIN, RUBBER, MIRROR, INTERIOR. Added today: PLASTIC (`d800626f`), PIPES (`037be4e0`), BRAKES, TIRE. Windows use `09a915b2`, not `010cb64a`.
- **Still unnamed on the 911:** textures `cd677a20` (brakes), `c8c5a8a4` (tyre), `3d743a07` (scuffs), `ece04847` (wipers), `911TURBO_LICENSE_PLATE` (no art); materials `010cb64a`, `09a915b2`, `84042024`, `12c9453c`, `3ecbcc34`, `76d2f63f`.
- **Caution:** two-piece guesses can collide by chance. `LI_F01_STAIR01CRACKED` for material `010cb64a` is almost certainly false.

### Track scenery: same model format as the cars (2026-11-15)

- **Files:** `grep -l eLabScenery extracted/*.bin` → entries 196 (`5ebf48d3`), 198 (`5ed16154`), 200 (`5ee379d5`), 211 (`63699a15`), 213 (`637bb296`), 215 (`638dcb17`), 221 (`682603d8`), 223 (`68381c59`); ZZDATA.BIN also matches the string. One file per **location**: several tracks share one (196 = `TRACK24\Pipeline\Scenery\eLabScenery99.bin`, holds TRACK21 and TRACK24 parts), 9,863,612 bytes.
- **Top-level chunks of 196** (from `chunktree.py`):
  - `80036000` → `36001` / `36002` / `36003`
  - `35021`, `35020`, `34250`
  - `80034015` → `34016` header, `34017` hash table, `34018` name list (starts "ELAmbien…")
  - `80034019` → `3401a` / `3401b`
  - `80034020` → repeating groups `34021 / 34024 / 34023 / 34022 / 34025`; sizes scale together (`34025` +576 bytes per unit)
  - `34026`
  - **8 × `80034000` MODEL containers**, the same as the cars (devpath "TRACK24\")
  - many `80034100` chunks (9–68 KB), each followed by 504 zero bytes
  - strings are `XE_*` particle effects (lightning, sandstorm, waterfall mist)
- **`hp2_geom.py` decodes it unchanged:** 613 parts, every `--check` passes.
- **Parts:** TRACK_HELICOPTER, SKYDOME, SKYDOME_ENVMAP, TRACK21_STARTLINE_FORW/BACK, PANORAMA (backdrop ring at the origin), `TRN_SECTION*_CHOP*` (terrain), `RD_SECTION*_CHOP*` (road).
- **Roads and terrain are stored in world space:** the overhead Blender view shows the whole location's road network laid out. Whether props are placed by `80034100` (or are also world-space) is unknown.
- **Textures:** patch 11 (`learn_png_names`) names hashes from every exported PNG filename under `--textures` ("named 2833 texture hashes"). Unresolved in 196: **24 materials**, all from two hashes, `a920b721` and `f6c11a30`.
- **Blender for scenery:** hide SKYDOME and PANORAMA (they block the view and the light). To jump to one piece, run this from **Text → New → ▶ Run Script** (pasting into the Python console runs lines out of order):
  ```python
  import bpy
  C = bpy.context
  for o in bpy.data.objects:
      o.hide_set('SKYDOME' in o.name or 'PANORAMA' in o.name)
      o.select_set('RD_SECTION50_CHOP1' in o.name)
  a = [x for x in C.screen.areas if x.type == 'VIEW_3D'][0]
  r = [x for x in a.regions if x.type == 'WINDOW'][0]
  with C.temp_override(area=a, region=r):
      bpy.ops.view3d.view_selected()
  ```
- **`--check` on all 8 files passes:** 7,270 parts (198: 658, 200: 712, 211: 921, 213: 921, 215: 820, 221: 1,344, 223: 1,281).
- **Placement decoded (2026-11-15), `tools/hp2_scenery.py`, commit 94d07ae, `docs/formats.md` §11.** Each `80034100` is one track section with four children:
  - `34101`: section number at +8
  - `34102`: models used, 40 bytes: part hash, two more hashes (likely LODs), u16 pair (LOD distances?), u32, f32 radius at +36
  - `34103`: instances, 48 bytes: s16 world box, u16 model index, u8 flags (4/8 rare), u8 kind (0 model, 1 shadow decal, 2 world-space part), 3 f32 position, 9 s16 matrix (0x4000 = 1.0) with uniform scale. **world = position + v·M** (rows are the model's axes)
  - `34104`: box tree; nodes 8-byte aligned **in the file**; child ≥ 0 instance, < 0 node
- **Checks:** placed models inside their stored box for 98.9–100% per file (139,179 instances); the transposed matrix fits worse. Box tree covers each instance once in 891 of 891 sections. 98.5% of model hashes are parts of the same file; most others are neighbouring locations' road/terrain sections; the rest exist in no extracted file (runtime-spawned?).
- **Rendered** (`render_obj.py`, Z-up export): sections 41–45 top view joins up; section 41 angled view shows pylons upright on the dirt trail, plants on the ground.
- **Export:** `hp2_scenery.py extracted/196_5ebf48d3.bin --obj OUT.obj --textures textures [--sections 41-45] [--no-shadows] [--game-axes]`. Five sections ≈ 27 MB of OBJ.
- The Blender road close-up check is superseded by these renders.
- **Collision decoded (commit 4446c7a, §11):** `34131` = 32-unit grid cells (f32 centre x,y; u16 start,count into `34133`); `34133` = u16 polygon indices; `34132` = 32-byte polygons: u16 order bits, u16 surface, s16 base height, u16 (0xFFFF or unknown number), 4×s16 x and 4×s16 y in 1/8 units, 4×s16 heights: **z = base + v/256**. Triangles repeat vertex 3. Heights match the road/terrain mesh to 0.05 for 97.6–100% (walls excluded). Surface low nibble: 1 asphalt, 2 dirt, 3 gravel, 4 puddle/leaves, 5 bridge, 6 grass, 7 shoulder, 9 verge, b leaves, c sidewalk, e blends (named from the textures underneath). `0x16xx`/`0x12xx` = invisible walls, 97% extruded the full -127..127. `--collision OUT.obj` exports by surface. `34130` unread.
- **Prop collision (commit 6bea6bb):** `80036000` → `36003` 32-byte objects: hash, u16 3, u16 1, u16 instance index, u16 section number, u32 0, 3 f32 position, f32 radius. 3,853 of 3,956 sit exactly on the named plant instance (misses are in files that borrow sections). `36002` is a box tree over them (not fully read); `36001` +12 = count.
- **Road quads (`80034800`, commit 255ce42):** `34801` = n, x range, 256-entry x lookup; `34802` = 64-byte quads (4 corners × [3 f32 + spare]: corner 0 spare = min x, 1 = max x, 2 = flags with low byte = corner count 4/3). Connected edge to edge down every road; sorted by min x.
- **Race routes (`80034147`):** `34148` = 40-byte routes (u8 id, u8 loop, u16 first, u16 end, u16 count, f32 from/to progress on main route, f32 1.0/1.4, first xy, last xy); `34149` = 20-byte points (x, y, u16 0 + up to 3 indices of other points, f32 progress). Main route progress = arc length of a curve through the points; loops close exactly (5 tracks); 3 tracks open (200, 215, 223: point-to-point?). Shortcut progress = main-route progress. `routes196.png` plots routes + quads over collision; `--routes OUT.obj` exports.
- **Police (`80034500`, commit 819eedf):** `34510` radar = count + 32-byte (name[16], x, y, u32, u32); `34520` roadblocks = count + 28-byte (name[16] `RB<trk>_<Easy|Med|Hard>_<B|F|R><nn>`, x, y, u32 0x467Cxx: high nibble 8/9/A = difficulty, agrees with name 416/416; low nibble 0 for B, F/R mixed 1/2 + variant); `34530` zones = count + 60-byte (n corners, 6 xy slots, u32 value, u32). All 231 radar + 416 roadblocks on the drivable surface. No heading stored.
- **Replay cameras (`80034405`):** 1,696 `34450` records, types 0 (world, 1,401) / 1 / 2 (onboard among them, car-relative). Counts at 0x74 (type 0: p, t, c) or 0x48 / 0x4C (types 1/2: p, t, c, d). Tail from 0x2A0 (type 0) or 0x270: c×16 points, c×16 second block, d×16, t×16 triangles, p×8 area xy. Formula fits all 1,696; triangles fan the area (t = p−2) in 1,685.
- **All 30 tracks (commit 10b92b3):** archive has `TRACKS\TRACKB11`–`TRACKB66` for locations 1, 2, 3, 4, 6 (no location 5). Decompressed with `hp2_comp.decompress` (returns a tuple: take [0]) into `~/hp2work/tracks/TRACKBnn.bin` (26 s for all). The 8 with plain `.BUN` twins are byte-identical. `hp2_scenery.py` passes every check on all 30; outputs `~/hp2work/tracks/check_nn.txt`. Tracks n and n+3 share data (forward/reverse? exe has `TN_TRACK%dR`); 3rd and 6th of each location are point-to-point. Location 1 has a surface code `f` (12–36 polygons, unnamed). TRACKB13/16 roadblocks are coded one level below their names.
- **Archive (commit b91df8e):** 270 of 372 named (+17: `GLOBAL\CHARACTERSA.BIN` = driver model; `NIS\NIS_ARREST01–03` and `NIS_TICKET01–04, 06` `.BIN` + `.CAM` cutscenes; prefix `NIS\NIS_` solved from both families). The other 102 are identified in `tools/archive_contents.csv`: 23–58 developer ghost replays (chunk 0x39100: version, track number from v5, driver WILLIAM/MARTIN/PAXTON); 26 sound banks as body + header pairs (header id = body id + 6, a .VH/.VB-style pair: menu, effects, 5 ambiences, 18 engines with tags 348, 993, BMW…; names not cracked, the ending after the tag is 4 characters); 359/360 profile defaults ("Guest"); 367 police speech events; 393 hex key; 399–402 QA notes; AVIs.
- **Identified only:** `80034020` smashable-object physics shapes ("CS_BarrelA_L", vertex/edge/face counts satisfy Euler); `80034500` radar spots ("Radar21_83") + roadblocks ("RB21_East_B1"); `80038000` ambient sound/effect emitters; `34200` track description; `34202` world bounds.
- `34121` (412 KB) holds "RAGNAR" padded to 40 bytes about 360 times; looks like records of `u32 1, f32, 40-byte name`; not central.
- `34250` contains "FOG<MAIN>" (fog/environment settings?).

## Waiting on the user right now

- Nothing pending; all pushed. (Older note about `gh auth` is obsolete: the user pushes from the PC.)
- PNGs live in `~/hp2work/textures` (not `png`). Blender workflow: File → New → General, delete cube, File → **Import** → Wavefront (not File → Open), Scripting tab → open and run `~/nfshp2-tools/tools/blender_ps2_shade.py`, Material Preview. Useful keys: Alt+H unhides everything, Esc closes a menu or stops playback (Space plays the animation, Ctrl+A opens Apply), Ctrl+Space maximises the view, numpad . frames the selection.
- The device bridge works now: Claude runs the tools itself (see Environment).

## Open items, roughly in priority order

1. ~~Visual check of the per-batch textures~~, ~~PCSX2 re-run~~, ~~packs 002/011/022~~ done. ~~`--check` on all 58 model files~~: 7,848 of 7,848 parts pass (counts, record material, texture index), 2026-11-14. Scenery decoded (see above). Minor: the thin yellow outline on the 911's hood in SKIN1.
2. ~~WINDOW_*, CARBOTTOM, light slots~~ done 2026-11-15 (executable + GlobalTextures.tpk).
3. ~~Wheel positions~~: car database (§12); `hp2_car.py` assembles cars.
4. **Track scenery, next steps:**
   - ~~road close-up~~, ~~`80034100` placement~~, ~~`--check` on 198–223~~ done 2026-11-15
   - ~~collision (`3413x`)~~, ~~prop collision (`80036000`)~~ done 2026-11-15
   - ~~road quads~~, ~~race routes~~ done 2026-11-15; cameras, smashables, police, emitters identified (formats §11 table)
   - ~~police~~, ~~cameras~~ done 2026-11-15 (meaning of camera float slots, second point block, zone values, roadblock low nibble still open)
   - still unread: `80034015` (name hash table), `80034019`, `35020`/`35021`, `34026`, `80034425`, `34106`, `34122`, `34130`, `34146`; inner layouts of smashables (`80034020`) and emitters (`80038000`)
   - route point u16 links, route f32 1.0/1.4, road-quad flag bits
   - collision header: the u16 at +6 (5% of polygons), the order bits, bit 0x1000
   - what the `34102` second/third hashes and u16 pair do (LODs?); instance flags 4/8; why shadow decals sit ~3 units off their box
   - name `a920b721` and `f6c11a30`
5. ~~Bank 322 interleave~~: 32,768 bytes (2026-11-15).
6. ~~Texture writer on real data~~: all identical (2026-11-15).
7. ~~The unnamed archive entries~~: all identified (2026-11-15); sound bank paths still unnamed.
8. Minor items:
   - `modded.iso` entry 321 still holds an earlier wrong-interleave patch (harmless)
   - the SDL launcher has never been run with a controller
   - ~~menu text~~ parsed (§14); ~~video codec~~ MidiVid (§15)
   - speech event mapping (EVENTS.EVT, entry 367); ghost replay format; scenery 35020/35021, 34017, 34106, 34121, 34122, 34130, 80034019, 80034425; car record unknown fields

## Tool index (`tools/`)

| Tool | Use |
|---|---|
| `hp2_extract.py` | archive extraction |
| `hp2_iso.py` | find files in the ISO, `install-bank`, `install` (any size, grows the image) |
| `hp2_skipintro.py` | patch an ISO or exe to boot straight to the title screen (`--undo`, `--check`) |
| `hp2_fescreens.py` | front-end screen layouts of FRONTB.BUN (list, `--screen`, `--strings`, `--json`) |
| `hp2_events.py` | speech source table (entry 367) and BAN line list |
| `hp2_verify.py` | `check` / `banks` / `hunt`: confirm what changed in an ISO |
| `hp2_audio.py` | VAG/ADPCM decode, `--probe` interleave |
| `hp2_music.py` | music bank `replace` |
| `hp2_tpk.py` | texture decode and write |
| `hp2_match.py` | match images by appearance (good for dumps, not for repainted packs) |
| `hp2_pcsx2.py` | exact PCSX2 filename → texture name |
| `hp2_hash.py` | the game's name hash (djb2, seed 0xFFFFFFFF), `--crack`, `--solve`, `two_piece` |
| `hp2_geom.py` | car and scenery models: decode, check, `--records`, OBJ export with per-batch textures, hash naming (also learns names from PNG filenames) |
| `hp2_carinfo.py` | car database (GLOBALB.BUN chunk 0x34600): list, `--check` against models, `--json` |
| `hp2_car.py` | assemble a whole car (body + wheels + brakes) as one OBJ |
| `hp2_tracklist.py` | track list (GLOBALB.BUN chunk 0x34201) |
| `hp2_scenery.py` | scenery: check placement, collision, prop collision, road quads and routes; export placed scenery (`--obj`), collision (`--collision`), road quads + routes (`--routes`) |
| `launcher/hp2fe` | the original Options menus drawn natively from your disc (`make hp2fe`) |
| `blender_ps2_shade.py` | run inside Blender: texture × vertex colour × (31/16)^2.2, like the PS2 |

Other folders: `launcher/` (SDL2 settings, input and the `hp2_menu` options-row model; C99 core with tests), `ghidra/` (function map), `docs/formats.md` (all format notes).

## Lessons from this session

- **A closed loop proves nothing.** Writing and reading back with the same assumption hid a wrong audio interleave. Always decode an untouched original as the control.
- **Check which entry the game actually loads.** There were two music banks.
- **Appearance matching fails on redrawn art.** Use the emulator's own hash.
- **Never create a file without checking the name is free.** `hp2_hash.py` was overwritten once; restored, and the new tool renamed `hp2_pcsx2.py`.
- **Large two-piece search spaces produce chance collisions.** Prefer clean names and treat the results as guesses.
- **Give commands with real paths, never placeholders.** The user runs them as written.
- **Model names are often slots, not pack entries.** When a texture name is missing from every pack, check the pack for the same stem with a state suffix.
- **Blender reuses a material that already has the same name** on import, so a re-import keeps the old images. Every visual check starts from File → New.
- **Test the palette, not your guess of it:** the first offset test used alpha 0x80; real PS2 palettes here use 0x7F.
- **Short, single-word names from a small wordlist are solid when they sit where their names say.** Positions are the check.
- **fish: `ls x; and echo stop` does not stop the next lines.** Guard with `if test -e x; echo exists, stop; else ...; end`.
- **Check the columns of a list file before feeding it to a command** (`geom_files.txt` has extra columns: use `awk '{print "extracted/"$1}'`).
- **Find real paths before giving them** (`grep -l`, `find`): the user ran an example path literally.
- **Multi-line Python in Blender goes in the Text editor,** not the console.
- **Verify a placement transform against data the file stores** (each instance's own bounding box), not by eye: it picked v·M over M·v at 99% vs 93%.
- **Alignment can be relative to the file, not the chunk** (scenery box tree).
- **Check that a file sent to the PC actually changed** (md5 both sides).
- **When a field fails as a coordinate, look for a local encoding:** collision heights are base + v/256; found by noticing values rose in step with the mesh heights inside one cell.
- **Bundles can hide packs:** GLOBALB/INGAMEB/FRONTB carry .tpk containers mid-file; search for the 0xB0300001 + 0x30300002 pair, not just files that start with it.
- **Follow a gp-relative global to its loader:** `sw $x, -0xNNNN($gp)` near a chunk-id compare names the chunk that fills a table (car database 0x34600, track list 0x34201).
- **The hash is linear: solve for a shared prefix.** If a family of names differs only in a known part, `solve_seed(id, known_ending)` gives the same value for all of them; crack that value as a short prefix. Ids that differ by a small number differ only in the last character (sound banks: +6 = 'H' − 'B'); a difference of 824 is '.CAM' vs '.BIN'.
- **Check what is compressed before calling data missing:** 22 tracks existed only as `.LZC`.
- **A stored distance that is shorter than the path it labels is not that path's length:** shortcut points carry main-route progress.
- **Split a match rate by category before trusting the total:** the collision height check looked like 87% until walls (which stand above the ground) were separated out.

## Front end port (hp2fe), commits e8d2552 .. 5131251

Port code is in `launcher/` (SDL2 front end over the real FRONTB / GLOBALB / FE_ENGLISH data). Repo holds tools and docs only. Tests: `make test` and `make fecheck FRONTB=... GLOBALB=... FELANG=... DISC=...` (assets 1172 checks, 0 failures).

| Commit | What |
|---|---|
| e8d2552..a1ebe72 | Jukebox, Options trimmed to Audio / Controller / Display / Save-Load, menu music |
| e4a2cb5 | Title screen, Main Menu and the two mode menus |
| 9254a63 | Quick Race on Main Menu (random Hot Pursuit or World Racing settings); renamed modes; Circle = back, Cross/Start = accept, Triangle = help |
| a6c2dbe, 8a27bd4 | Car select (23 cars), laid out from a PCSX2 screenshot |
| 35df472 | Track select (15 courses); every car and course open, no points |
| 884fd24 | Car select shows CARLOGO_<car>; Transmission and Handling panels; track select Time of Day column; Challenge screen; **GRID texture fix** |
| 5131251 | You're The Pig setup; Challenge titled with the renamed row |

Names in the port: Hot Pursuit = Ultimate Fugitive (the game's Ultimate Racer / Championship), Racing But With Pigs (Challenge), You're The Pig (You're The Cop); World Racing = Ultimate Racer (Championship), Just Race (Challenge).

Facts worth keeping:
- Layout screens: Title b5761d00, Main ae0f8aa1, Hot Pursuit 5b35f533, World Racing 033e7771, SELECT_CAR b5074ab4, SELECT_TRACK 15417b53, CHALLENGE 2d62cfe2, You're The Cop e55f1297. Texture and font ids are `hp2_fe_hash(upper-case name)`, so TRACKnn, LOGOTRACKnn, TN_TRACKnn, CARLOGO_<car>, TOD_* can be addressed by name.
- Widgets are often stored hidden or parked off screen; the screen code places them. Missing textures draw white; `HP2_FEDRAW_SOLID` is a flat tintable stand-in (swatches, the popup dim).
- PCSX2 screenshots: x = 4.025 px per canvas unit; y = (py + 20) / 2.406.
- **Texture decode: a 4-bit texture with 3 page rows (GRID, 512 x 384) is stored linear, not swizzled.** Rule: swizzled only when height is 128 or a multiple of 256. This had made every menu grid scrambled and faint.
- Track: n + 3 is the same course in its other lighting; point-to-point = the third of each location (number ends in 3). Elevations ("50 to 278 ft") have no known data source yet.
- Car specs strings: 0-60 = 42001+n, top speed 43001+n, power 44001+n (Barchetta n = 40). The real car select shows no specs, so the port does not either.
- Challenge help: players 19000/19001; race type 20010-20013; skill 19002-19004 (opponents) or 19006-19008 (cops); Setup 19005.
- Render check without a PC build: build the front end in the container with SDL2 and `--shot out.bmp --keys accept,down,...` under `SDL_VIDEODRIVER=dummy`. The device VM has no SDL2 and an older glibc, so it cannot run the container's binary.

Not done yet: event trees (Ultimate Fugitive / Ultimate Racer; PURSUIT_TREE_BG is in the pack), track elevations, the POPUP screen, the race itself.

## Front end port, later commits (566ebb3, 384e851, cb5e3eb)
- 566ebb3: Customize (RACE_SETUP) screen between Challenge Setup and car select.
- 384e851: event tree viewer (TREE_VIEW) for Ultimate Fugitive / Ultimate Racer: number, "Event n", Status (all Unlocked), Reward ("No reward"), title bar, Challenge backdrop; left/right/up/down steps events, Circle goes back.
- cb5e3eb: docs/formats.md event tree chunk notes (0x34A05 records etc.).
- Open: star ring texture (PURSUIT_EVENT_PANEL_UNL exists in FRONTB but the C lookup draws nothing), map backdrop PURSUIT_TREE_BG (8-bit 512x448 decodes scrambled; swizzle rule for 8-bit probably wrong), connectors (chunks 0x34A01-06 undecoded), reward data, track elevation, POPUP screen, the race itself.

## Commit ecb71f4 — hp2track: opponents (2026-11-21)
- New `launcher/src/hp2_ai.c/h`: stand-in opponent driver + driving line (`hp2_ai_line_build(l, race, coll)`).
  Line = A* over a 2 m collision-ground grid (band 150 m round the main route, up to 4 levels per cell,
  edges where hp2_coll_blocked or step > 1.6 m); waypoints from the route every 150 m only where the route
  has ground for ±150 m; cost x(1+((off-30)/20)^2) x(1+4/(1+clearance)); ignores small walled islands (<2000 nodes).
  Resampled every 4 m, smoothed. Driver: look-ahead 8+0.45v, lane clamped to half-width, speed from line
  curvature with mu = 0.85*min(front,rear sliding friction)*skill, throttle cut when slip > 0.12 rad, reverse when stuck.
- Finding: main route points often lie off the collision ground (196: 22/73, 211: 20/121, 221: 22/119);
  docs/formats.md Routes corrected. Game AI probably uses 0x34121 "RAGNAR" data (unread).
- hp2_coll_blocked(c, ax, ay, bx, by, z); hp2_race_point is Catmull-Rom; hp2_race_order; hp2_drive_collide.
- Sim results (one 911 lap): 196 239 s, 211 334 s, 221 343 s, 0 stuck. F50 a bit wild (spins on light rear).
- Next: start a race from an hp2fe event (event tree 34A06 opponents: car, colour, skill).

## Commit 94575c9 — hp2fe: start races from events and Quick Race (2026-11-21)
- hp2_femenu: HP2_FEM_RACE result bit + `m->go` (hp2_fem_race: track, reverse, laps, car, skin, opponents with
  skill/skin, event, race_type). EVENT_INFO accept → event_race(); SELECT LAPS accept → single_race()
  (Customize opponents; quick mode = 7 random). hp2_femenu_race_args() builds hp2track args.
- fe_main: runs hp2track (posix_spawn/_spawnv, waits), hides window, pauses audio; --hp2track PATH;
  --keys prints "race: ..." instead. Not yet tried in a real window (needs the user).
- hp2track: --reverse (hp2_scene_reverse_route), HP2_MAX_CARS 8, --opponent NAME[:SKILL[:PAINT]],
  skill 0-9 → ai.skill 0.78+0.02*s. Event colours 1..8 taken as paint numbers (inferred).
- hp2_race: progress search penalises jumps > 400 m from the last progress (fixed early finish, reversed 65).
- hp2_ai line: retries other levels / skips unreachable waypoints; components must be ≥ largest/8;
  never routes through edge nodes (clear 0).
- Next ideas: knockout/tournament rules, results back into hp2fe, cops for pursuit events, decode RAGNAR (34121).
