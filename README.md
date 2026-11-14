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
| **Filenames** | 252 of 372 archive entries mapped back to their original paths |
| **Car models** | `GEOMETRY.BIN` decoded to triangles with normals, texture coordinates and vertex colours; exports OBJ. Checked on one car |

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

# extracted/names.txt now lists each file's original path, e.g.
#   CARS\CORVETTE\GEOMETRY.BIN, GLOBAL\HUDTEX.BIN, MOVIES\NFSINTRO_EN.AVI
```

Every tool prints its usage when run with no arguments, and takes paths
relative to wherever you run it. Add `--list` to `hp2_extract.py` to see the
archive table without writing anything.

### Replacing music

```sh
python tools/hp2_music.py list <music bank>
python tools/hp2_music.py replace <music bank> 15 mysong.wav -o modded.bin
#   the stereo interleave is measured from the track being replaced - the two
#   music banks on the disc differ in it, and the wrong one plays distorted
python tools/hp2_audio.py modded.bin --match=rush      # listen before installing
python tools/hp2_music.py install modded.bin
```

Nothing ever changes size: a replacement is padded with silence or trimmed to
fit its slot, so the archive index stays valid and `ZDIR.BIN` is never touched.

### Testing it in an emulator

An emulator boots a disc image, not loose files, so a modified bank has to go
back into the ISO. Because sizes never change it can be written straight over
the original — no rebuild. `install-bank` does it without copying the 3 GB
archive out of the image first: it reads `ZDIR.BIN` from inside the ISO to find
where the entry lives, and writes there.

```sh
cp game.iso modded.iso
python tools/hp2_iso.py install-bank modded.iso 321 modded.bin
```

If you already have a whole modified `ZZDATA.BIN` on disk, replace the file
instead:

```sh
python tools/hp2_iso.py find  modded.iso NFSHP2/ZZDATA.BIN
python tools/hp2_iso.py write modded.iso NFSHP2/ZZDATA.BIN path/to/ZZDATA.BIN
```

Then boot `modded.iso`. Work on a copy; your original image is the backup.

### Replacing a texture

A replacement must be exactly the dimensions of the slot: sizes never change,
because the slot is fixed and the GS has 4 MB of video memory. A texture can be
repainted but not enlarged — upscaling belongs to a port, not to the disc.

```sh
python tools/hp2_tpk.py list   extracted/241_73607f72.bin
python tools/hp2_tpk.py write  extracted/241_73607f72.bin BARN1 new_barn.png out.tpk
python tools/hp2_iso.py install-bank modded.iso 241 out.tpk
```

The palette is rebuilt from the new image, except when it is shared with
another texture — rewriting it would damage that one, so the shipped palette is
kept and the image quantised to it. Every mip level is regenerated.

Before trusting any of it, hold the import to being an exact inverse of the
export:

```sh
python tools/hp2_tpk.py verify extracted
```

For every texture and every mip level that takes the bytes as shipped, unpacks
them to palette indices, packs them again and requires the same bytes back,
then does the same for the palette. Anything that does not round-trip is a bug
in the writer, and it is named.

### Carrying a PCSX2 texture pack to a port

PCSX2 names replacement textures by a hash it computes from the texture upload,
so a pack is a folder of opaque filenames. The pictures are reusable by a port;
the names are not, because a port loads the game's own assets by name.

There are two ways to recover the names, and they fail differently.

```sh
python tools/hp2_pcsx2.py extracted ~/.config/PCSX2/textures/SLUS-20362 -o map.csv
python tools/hp2_match.py extracted ~/.config/PCSX2/textures/SLUS-20362 -o map.csv
```

`hp2_pcsx2.py` recomputes PCSX2's own filenames: it lays each of the game's
textures out the way the GS stores it and hashes it the way PCSX2 does. A hit
is exact, and it does not matter how thoroughly the replacement was repainted.
It needs `pip install xxhash`. Run against a real pack it named 1092 of 1316
replacement files; every hit agreed with the size and format stated in its
filename, the palette hash was reproduced independently for all but two, and
all 30 dump files that could also be identified pixel-for-pixel got the same
name both ways. Files it cannot name are listed in the CSV with a blank asset:
textures whose size is not a power of two are seen by PCSX2 through a clamp
region, which this does not model.

`hp2_match.py` compares pictures instead. That is right for a dump folder and
for a pack that is a plain upscale. It is wrong for a pack whose art was
redrawn: run against a real one, every match that could be checked against a
dump's hash (13 of 13) was the wrong texture, including ones labelled "close".
Do not trust its output for an enhanced pack.

Note that PCSX2 texture replacement can load textures at any resolution,
because it swaps them as the GS uploads them. Patching the disc cannot: that is
what the fixed slot size above is about.

### Exporting a car model

```sh
python tools/hp2_geom.py extracted/063_508439c9.bin --obj models
python tools/hp2_geom.py extracted/* --survey
```

`--obj` writes one Wavefront OBJ per part: `_A`, `_B` and `_C` are the same
car at three levels of detail, and the tyre is separate. Vertex colours ride
on the `v` lines, which Blender and MeshLab read. Axes are left as stored (X
forward, Z up), and faces are grouped by material (`CARSKIN`, `MIRROR`,
`RUBBER`...; unnamed ones appear as their hash). Which texture each material
draws with is not yet known, so the OBJ has no `.mtl`.

Every model file records how many triangles each batch should produce.
`--survey` decodes every model it is given and reports whether it got that
number, one line per file. For the one car examined so far it does, for all 77
batches. Run it over the whole `extracted` folder before trusting it on the
rest.

### When a mod boots but nothing changed

Three causes, and `hp2_verify.py` tells them apart:

```sh
python tools/hp2_verify.py check modded.iso --orig game.iso --entry 321
python tools/hp2_verify.py banks modded.iso
python tools/hp2_verify.py hunt  modded.iso 321_ad921759.bin 15
```

`check` diffs the two images and maps every differing byte back to the archive
entry it falls inside — if nothing differs, the patch never landed. `banks`
lists every entry that parses as a music bank, since a second bank holding the
same music is the usual reason a patched one has no effect. `hunt` fingerprints
one track out of an unmodified bank and finds every copy of that audio anywhere
in the image, inside the archive or out. All three are read-only.

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

- One modified disc has been booted in PCSX2: a replaced music track plays
  correctly in game. Texture writing is verified only to the extent that the
  tools read their own output correctly.
- The model decoder has been checked on one car. `hp2_geom.py --survey` tests
  the rest.
- The launcher's two SDL source files compile cleanly but have never run.
- Function attribution past the 199 direct hits is a call-graph heuristic;
  `functions_attributed.csv` marks each row `direct` or `inferred`.

## Scope

Tools and documentation only. No game assets, no disc images, no executable.
`.gitignore` blocks the obvious extensions so none get committed by accident.
Reverse engineering for interoperability, and modifying a copy you own, is the
intent here; redistributing the game's content is not.

Not affiliated with or endorsed by Electronic Arts.
