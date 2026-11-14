# nfshp2-tools

Reverse-engineering tools and format documentation for **Need for Speed:
Hot Pursuit 2** on PlayStation 2 (NTSC, SLUS-203.62).

Read assets out of the game, write your own back in, and start on the
executable. Nothing here contains any game data — you supply your own disc.

## What works

| | |
|---|---|
| **Archive** | `ZDIR.BIN` / `ZZDATA.BIN` fully parsed and extracted — 405 entries, 372 real files |
| **Compression** | The `COMP` LZ scheme, confirmed against the game's own decompressor |
| **Textures** | 3,284 of 3,287 textures export to PNG, including PS2 swizzling and mipmaps |
| **Sound** | 382 named sound effects to WAV |
| **Music** | 23 stereo tracks to WAV, and your own audio back in |
| **Executable** | 377 functions attributed to source files, 258 sites carrying file and line |
| **Name hash** | solved — djb2 with a `-1` seed, verified against every texture name checked |

[`docs/formats.md`](docs/formats.md) documents every format in detail, marking
what is confirmed against the disassembly, what is verified against data, and
what is still inferred.

## Layout

```
tools/      Python tools for reading and writing game data (start with hp2_extract.py)
launcher/   SDL2 settings and input prototype for a port (C99, 988 tests)
ghidra/     Function map and an import script for Ghidra
docs/       Format documentation
```

## Getting started

You need a disc image of your own copy. Unpack it with any ISO tool (`7z x
game.iso`) — you should end up with a `NFSHP2/` folder containing `ZDIR.BIN`
and `ZZDATA.BIN`. Then, from your working folder:

```sh
# 1. split the archive into individual files  (~3 GB of free space)
python tools/hp2_extract.py NFSHP2 -o extracted

# 2. decompress everything that is compressed
python tools/hp2_comp.py extracted

# 3. textures to PNG
pip install pillow numpy
python tools/hp2_tpk.py extracted

# 4. find the audio, then extract a bank
python tools/hp2_scan.py extracted
python tools/hp2_audio.py extracted/359_e2f0cc8a.bin --list

# 5. check a texture pack's name hashes
python tools/hp2_hash.py --check extracted/241_73607f72.bin
```

Every tool prints its usage when run with no arguments, and takes paths
relative to wherever you run it. Add `--list` to `hp2_extract.py` to see the
archive table without writing anything.

### Replacing music

```sh
python tools/hp2_music.py list <music bank>
python tools/hp2_music.py replace <music bank> 15 mysong.wav -o modded.bin
python tools/hp2_audio.py modded.bin --match=rush      # listen before installing
python tools/hp2_music.py install modded.bin
```

Nothing ever changes size: a replacement is padded with silence or trimmed to
fit its slot, so the archive index stays valid and `ZDIR.BIN` is never touched.
Keep your original disc image — it is the backup.

### The launcher

A standalone prototype of the settings file and input layer, so a port has
something to adopt when it exists. It auto-detects the controller and shows
the right button names for it:

```
Xbox Controller          | Accelerate: RT | Brake: LT | Handbrake: A
DualSense                | Accelerate: R2 | Brake: L2 | Handbrake: Cross
Switch Pro Controller    | Accelerate: ZR | Brake: ZL | Handbrake: B
```

```sh
cd launcher
make test      # 988 assertions, no SDL needed
make           # needs SDL2
./hp2launcher
```

See [`launcher/README.md`](launcher/README.md).

### Ghidra

Load `SLUS_203.62` with the
[Emotion Engine extension](https://github.com/chaoticgd/ghidra-emotionengine-reloaded),
let analysis finish, then run `ghidra/HP2Import.java` from the Script Manager
and point it at the `ghidra/` folder. It names functions by source file and
annotates each allocation site with its original file and line.

`functions.csv` lists all 12,094 function boundaries with call counts; sorting
by `times_called` is a reasonable way to find the code that matters.

## Status and caveats

The format work is solid — the compression is confirmed against the shipped
code, and the texture and audio decoders are checked against thousands of real
files. What is **not** proven:

- No modified archive has been booted in an emulator yet, so writing back is
  verified only to the extent that the tools read their own output correctly.
- The launcher's two SDL source files compile cleanly but have never run.
- Function attribution past the 199 direct hits is a call-graph heuristic;
  `functions_attributed.csv` marks each row `direct` or `inferred`.

## Scope

Tools and documentation only. No game assets, no disc images, no executable.
`.gitignore` blocks the obvious extensions so none get committed by accident.
Reverse engineering for interoperability, and modifying a copy you own, is the
intent here; redistributing the game's content is not.

Not affiliated with or endorsed by Electronic Arts.
