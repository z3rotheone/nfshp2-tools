# File formats — Need for Speed: Hot Pursuit 2 (PS2)

Worked out from the NTSC release, disc serial **SLUS-203.62**. Every offset
below was verified against real data; where something is inferred rather than
confirmed it says so.

A note on confidence, because it matters when you build on this:

- **Confirmed against the game's own code** — read out of the disassembly.
- **Verified against data** — a decoder built from the format reproduces real
  files byte-for-byte.
- **Inferred** — consistent with everything seen so far, not proven.

All integers are little-endian unless stated. "BE" marks big-endian.

---

## 1. The archive: `ZDIR.BIN` + `ZZDATA.BIN`

*Verified against data: all 405 entries parse and every extent lies inside the
archive.*

`NFSHP2/ZZDATA.BIN` is one ~3.1 GB blob holding nearly every asset.
`NFSHP2/ZDIR.BIN` is its index: 4,860 bytes, exactly 405 records of 12 bytes.

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | id — the name hash (§9) of the file's original path |
| 4 | u32 | offset, **in 2048-byte sectors** |
| 8 | u32 | size, **in bytes** |

Records are ordered by offset. 33 of the 405 have size 0; they appear to be
placeholders or aliases (*inferred*). The remaining 372 are the real files.

Entries are sector-aligned so the PS2 can DMA straight from the disc, which is
also why the archive holds many byte-identical duplicates: a copy of a texture
pack sits next to each track that needs it, avoiding a seek. Any mod that
changes a shared asset has to change every copy.

The id is the §10 hash of the path the game asks for, such as
`GLOBAL\\DRIVTEX.BIN` or `CARS\\CORVETTE\\GEOMETRY.BIN`. Note that these runtime
paths are *not* the development paths stored inside the assets themselves — a
pack whose header reads `Global\\DriverTextures.tpk` is fetched as
`GLOBAL\\DRIVTEX.BIN`, which is why hashing the embedded path never matches.

Hashing candidate strings out of the executable against the 372 ids recovers
the real path for about two thirds of them; `tools/archive_names.csv` holds the
result, and `hp2_extract.py` writes it beside the files it extracts. A
compressed asset lives under the same path with the extension `.LZC` in place
of `.BIN`, which is how the archive carries both forms of the same file.

Read with `tools/hp2_extract.py`, then `tools/hp2_index.py`.

---

## 2. Compression: `COMP`

*Confirmed against the game's own code — the decompressor is at virtual address
`0x1b41c8` in `SLUS_203.62`, the compressor's header write at `0x1b3e2c`.
Also verified against data: 58 files that exist in both compressed and
uncompressed form reproduce byte-for-byte.*

81 of the 372 archive entries begin with `COMP`.

### Header (16 bytes)

| Offset | Type | Meaning |
|---|---|---|
| 0 | char[4] | `COMP` |
| 4 | u8 | version, always 1 |
| 5 | u8 | header size, always 0x10 |
| 6 | u16 | **method**: 0 = LZ below, 1 = stored (copied verbatim) |
| 8 | u32 | uncompressed size |
| 12 | u32 | compressed size — equals the file size |

Do not skip the method field. A stored file fed through the LZ decoder yields
plausible-looking garbage rather than an error.

### The LZ scheme (method 0)

Payload starts at offset 16 and is a sequence of blocks:

1. A u16 flag word, read little-endian.
2. Up to 16 tokens, selected by that word's bits **from bit 0 upward**.

| Bit | Token |
|---|---|
| 0 | one literal byte, copied out |
| 1 | two bytes `b0 b1`: a back-reference |

For a back-reference:

```
length   = 3 + (b0 & 0x0F)                 /* 3 .. 18 */
distance = ((b0 & 0xF0) << 4) | b1         /* 1 .. 4095, back from the output */
```

The copy is byte-by-byte and overlapping distances are legal, so a distance of
1 repeats the previous byte.

The shipped code ORs `0x10000` into the flag word as a sentinel, so one shift
per token tells it when all 16 are spent — a neat trick worth knowing if you
compare your decoder against the disassembly.

Read with `tools/hp2_comp.py`.

---

## 3. Textures: `.tpk` packs

*Verified against data: 3,284 of 3,287 textures in the archive decode to clean
images; the 3 left are two unusual palette sizes.*

A chunk tree. Every chunk is:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | id — **top bit set means it contains child chunks** |
| 4 | u32 | size of the payload |
| 8 | … | payload |

The chunk walk lands exactly on the end of a well-formed file, which makes it a
good validity check. The same convention is used by the sound banks (§5), so it
is a house format rather than something specific to textures.

| Id | Contents |
|---|---|
| `0xB0300001` | texture pack (container) |
| `0x30300002` | pack header, 84 bytes: pack name and original path |
| `0x30300003` | texture index: N records of 164 bytes |
| `0x30300004` | texture data: 0x11 bytes of padding, then palettes, then pixels |
| `0xB0300100` | trailer (container), followed by a zero-id padding chunk |

### Index record (164 bytes)

| Offset | Type | Meaning |
|---|---|---|
| 8 | char[24] | name, NUL-padded |
| 32 | u32 | hash of the name |
| 36 | u16 | width |
| 38 | u16 | height |
| 48 | u32 | pixel offset |
| 52 | u32 | palette offset |
| 56 | u32 | pixel size in bytes |
| 60 | u32 | palette size in bytes |

Offsets are relative to the **first palette** in the data chunk, i.e. the data
chunk's start plus its leading padding.

### Pixel formats

Palette size tells you which: 1024 bytes = 256 colours (8 bits per pixel), 64
bytes = 16 colours (4 bpp), 0 = 32-bit RGBA.

Palette entries are RGBA bytes with PS2 alpha, running 0..0x80 rather than
0..0xFF, so scale by 2 and clamp.

**256-colour palettes are reordered.** Within each group of 32 entries,
entries 8–15 and 16–23 are swapped. Undo this before use.

A texture's pixel data may be followed by smaller mipmap levels: level 0 is
first, each subsequent level half the size in both axes. A pixel size larger
than `width × height` (or half that, for 4 bpp) means mipmaps are present.

### Swizzling — the part that catches people out

Only textures that fill whole GS memory pages are stored swizzled:

- 8 bpp: width a multiple of 128 **and** height a multiple of 64
- 4 bpp: width **and** height both multiples of 128

Everything else — and that is most textures — is stored as plain rows of
pixels. For 4 bpp linear data the **low nibble of each byte is the left pixel**.

Swizzled data is a raster of 32-bit words that the Graphics Synthesizer reads
back as 8- or 4-bit pixels. Reproducing it needs the GS block and column
tables; `tools/hp2_tpk.py` rebuilds the addressing from them, and PCSX2's
`GSTables.cpp` is the reference for the table values themselves.

Read with `tools/hp2_tpk.py`.

---

## 4. Audio: Sony VAG

*Verified against data: 382 sounds and 23 music streams decode to clean audio.*

All game audio is PS-ADPCM in a standard VAG wrapper.

### VAG header (48 bytes, big-endian)

| Offset | Type | Meaning |
|---|---|---|
| 0 | char[4] | `VAGp` |
| 4 | u32 BE | version |
| 8 | u32 BE | reserved |
| 12 | u32 BE | data size in bytes |
| 16 | u32 BE | sample rate in Hz |
| 32 | char[16] | name — empty in this game |

### PS-ADPCM

16-byte blocks, each yielding 28 samples:

| Byte | Meaning |
|---|---|
| 0 | low nibble = shift, high nibble = predictor (0–4) |
| 1 | flags; **7 ends the sound** |
| 2–15 | 28 four-bit samples, **low nibble first** |

```
sample = (sign_extend_4(nibble) << 12) >> shift
output = sample + (f0 * hist1 + f1 * hist2 + 32) >> 6     /* clamp to int16 */
```

with the usual coefficient table, scaled by 1/64:

| Predictor | f0 | f1 |
|---|---|---|
| 0 | 0 | 0 |
| 1 | 60 | 0 |
| 2 | 115 | −52 |
| 3 | 98 | −55 |
| 4 | 122 | −60 |

Clamp a predictor above 4 to 0 and a shift above 12 to 9, as the hardware does.

---

## 5. Sound banks

*Verified against data.*

Same chunk convention as §3.

| Id | Contents |
|---|---|
| `0x00014200` / `0x00014201` / `0x00014202` | bank header chunks |
| `0x80014103` | one sound (container) |
| ⟶ `0x00000001` | its name, NUL-padded |
| ⟶ `0x00014204` | a VAG file |

Names survive in full: `CopSiren01`, `BurnOutPeel`, `CrashMetalHard`,
`LongitudinalSkid`, `TirePop`, `MenuStart`. Mostly 22 kHz, some at 44 kHz,
mono.

Read with `tools/hp2_audio.py`.

---

## 6. Music banks

*Verified against data; the stereo interleave was measured per bank, not assumed.*

A different layout from the sound banks.

### Header

| Offset | Type | Meaning |
|---|---|---|
| 4 | u32 | channel count (2) |
| 8 | u32 | sample rate (32000) |
| 16 | u32 | 4096 — a buffer size, **not** the audio interleave |
| 32 | u32 | track count |
| 36 | char[] | bank name, e.g. `InGame`, padded with `0xCC` |

### Track table — starts at 0x60, stride 96 bytes

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | offset of the track's VAG header |
| 4 | u32 | size, including the 48-byte VAG header |
| 8 | u32 | zero |
| 12 | char[] | name as `Artist-Title-Album-Genre-Number`, NUL-terminated |

The trailing number appears to be a usage flag: instrumental versions end in 2,
vocal versions in 4, and tracks with no album credit in 0 (*inferred* from the
pattern across all 23 tracks).

### Stereo interleave — differs between banks

The two channels alternate in fixed-size chunks: L, R, L, R. **The chunk size is
a property of the bank, not of the format**, and the two banks on the disc do
not agree:

| Entry | Name | Interleave | Per-channel size |
|---|---|---|---|
| 321 | `SOUND\JUKEBOX.JBC` | 16 bytes (one ADPCM block) | not aligned |
| 322 | `SOUND\JUKEBOXM.JBC` | 2048 bytes | a whole number of 2048-byte blocks |

Decode each channel with its own predictor history, reassembling it from its
chunks first.

This was measured, not taken from the header. A wrong chunk size splices the
two channels into each other's predictor history at every boundary, so the
waveform jumps there while staying smooth inside a chunk, and the two decoded
"channels" stop correlating. Comparing the mean sample-to-sample step across
boundaries with the mean overall separates the cases, and channel correlation
separates them sharply: ~0.7–0.9 for the right chunk size against ~0 for the
wrong one. `tools/hp2_audio.py --probe` prints that table.

The 2048-byte alignment of every track in entry 322 corroborates it: all 23
tracks have a per-channel size that is an exact multiple of 2048, which only a
2048-byte chunk interleave requires. No track in entry 321 is so aligned.

The `4096` at `+0x10` is **not** the interleave — it is 4096 in both banks,
which differ. It may be the DMA buffer size for a stereo pair in entry 322
(2 × 2048) and simply unused in entry 321.

A track's playing time is `data_size / 2 / 16 * 28 / 32000` seconds either way,
since the interleave does not change how much audio is present.

Both banks hold the same 23 tracks under the same names in the same order, but
independently encoded — every track in 322 is a few thousand bytes longer. The
game loads 322; patching only 321 has no audible effect (*verified in an
emulator*).

Read with `tools/hp2_audio.py`, write with `tools/hp2_music.py`.

---

## 7. Car models: `GEOMETRY.BIN`

*Verified against data for `CARS\\4DR_SEDAN02\\GEOMETRY.BIN`; the vertex layout
is confirmed by the result measuring 4.68 m x 2.16 m x 1.15 m, symmetric about
y=0 and sitting on z=0.*

The same chunk convention as the texture packs (§3).

| Id | Contents |
|---|---|
| `0x00034013` | file header: 6 attachment records of 32 bytes |
| `0x80034000` | model (container) |
| ⟶ `0x00034001` | the development path the file was built from |
| ⟶ `0x80034002` | one part (container), repeated |
| ⟶⟶ `0x00034003` | part header: name hash, name length, name, bounding box |
| ⟶⟶ `0x00034006` | texture name hashes for this part, as (u32 hash, u32 pad) |
| ⟶⟶ `0x0003401d` | further texture hashes, the generic ones such as `MIRROR` |
| ⟶⟶ `0x00034004` | sub-mesh records with bounding boxes |
| ⟶⟶ `0x00034005` | the geometry, as a VIF display list |

Sequences of `0x11111111` are pointer placeholders the loader fixes up.

### Parts and names

Parts are named `<CAR>_A`, `_B`, `_C` and `<CAR>_TIRE_FRONT_A`, with geometry
shrinking down the letters: 22,032 / 13,376 / 6,320 bytes for the sedan. They
are levels of detail. The letter is a variant suffix carried by the hash but
not always by the stored name — a part whose name field reads
`4DR_SEDAN02_TIRE_FRONT_` hashes as `4DR_SEDAN02_TIRE_FRONT_A`, and its
sibling as `..._B`, giving consecutive hashes.

Texture references are name hashes (§10) only, so recovering what a car is
painted with means hashing candidate names; `4DR_SEDAN02_SKIN1`, `INTERIOR`
and `MIRROR` check out.

### The file header's attachment records

Six 32-byte records, each: two u32 (both 11), the hash of the part to attach,
the hash of the car's own base name, a position as three floats, and flags.
The sedan's are at x ≈ ±2.2 with y in ± pairs: wheel positions.

### Geometry: a VIF display list

`0x00034005` is a PS2 VIF packet stream, the format the hardware DMAs to the
Vector Unit. It alternates `UNPACK` commands, which push data into VU1 memory
at an address given by the tag's immediate, with `MSCAL`, which runs the
microprogram that turns it into triangles. The sedan's largest part is 17 such
batches.

Within a batch, by VU address:

| Address | Data |
|---|---|
| 0 | `V4_8`, one quad: batch header |
| 1 | `V4_32`, one quad, all zero |
| **2** | **`V4_32`: vertex positions** |
| 14, 23, 46 | `V2_32` / `S_32`: texture coordinates |
| 32 | `V4_32`: a second vector stream, normals or lighting |
| 52 | `V4_5551`: packed colours |
| 92 | `V4_8` |

**Positions are transposed for the VU1's SIMD registers.** Each group of three
quads holds the X, then the Y, then the Z of the *same four vertices*, so a
block of `num` quads yields `num/3 * 4` vertices and `num` is always a
multiple of 3. Reading the quads as (x, y, z, w) per vertex produces noise.

One practical trap: VIF payloads are padded to a 4-byte boundary. A
`V4_5551` run of 29 elements occupies 60 bytes, not 58, and a walker that
misses this desynchronises partway through a batch.

Turning the stream into triangles needs the VU1 microprogram's own strip
logic, which is not decoded. `tools/hp2_geom.py` reports the structure and
dumps the positions as a point cloud, which is enough to confirm a model and
to measure it.

---

## 8. Open questions

Things not yet worked out, roughly in order of how tractable they look:

- **The remaining 120 archive filenames.** 252 of 372 are recovered (§1).
  The rest are mostly unnamed blobs whose paths are not literal strings in the
  executable — notably entries 23–58, a run of 36 files of 60–240 KB, which is
  suggestively 6 locations x 6 sub-tracks but matches no `TRACKS\\...` pattern
  tried. They are probably composed at runtime from a table not yet found.
  `tools/hp2_hash.py --solve` narrows a guess: it gives the exact hash state
  any proposed prefix would have to produce.
- **Triangle reconstruction from the VIF stream** (§10). Positions come out;
  assembling them into strips needs the VU1 microprogram, which lives in the
  executable's `.vutext` section.
- **Track scenery** — the `eLabScenery0.bin` files, 7–11 MB each.
- **Menu text** — the `FE_ENGLISH.bin` family. Strings are readable; the
  surrounding structure is not parsed.
- **Video** — 21 AVI files extract cleanly as containers, but the codec is
  probably custom; the executable has `movieplayer/dctimage.cpp` among its
  source paths.
- **Three textures** with 128-byte and zero-length palettes.

---

## 9. The executable

`SLUS_203.62` is a 2.3 MB ELF: 32-bit, little-endian, MIPS, entry point
`0x100008`. `.text` is 1.7 MB at `0x100000`, roughly 431,000 instructions.
There is a `.vutext` section of Vector Unit microcode, which any port replaces
wholesale. No symbol table survives.

What does survive is 296 string constants that are source file paths, passed to
a debug allocator at `0x2577c0` together with a line number — so each site
gives a file name and an exact line from the original build. `ghidra/` holds a
map derived from these; see its notes for what is attributed directly versus
inferred through the call graph.

The paths reveal the original layout: `..\indep\src\` for platform-independent
game code (`car.cpp`, `player.cpp`, `racecoordinator.cpp`, `trackroute.cpp`,
`carsounds.cpp`, `frontendscreens\*.cpp`, `movieplayer\*.cpp`, `lzcompress.cpp`)
and `src\b*.cpp` for a base library (`bFile.cpp`, `bMemory.cpp`, `bSound.cpp`,
`bJukebox.cpp`, `bDriverPS2.cpp`). `bDriverPS2.cpp` is the platform layer a
port would discard.
## 10. The name hash

*Confirmed twice: it reproduces the hash stored beside all 29 texture names
checked, and the routine is in the executable.*

```
h = 0xFFFFFFFF
for each byte:  h = (h * 33 + byte) & 0xFFFFFFFF
```

djb2 with a seed of −1 rather than the usual 5381. Three forms exist in the
binary:

| Address | Signature |
|---|---|
| `0x103520` | `hash(data, length)` |
| `0x2534a8` | `hash(string)` — seeds with `0xFFFFFFFF` |
| `0x2534e8` | `hash_continue(string, seed)` — carries an existing hash on |

The third lets a name be hashed in pieces, which the game uses to look
textures up by a composed name: at `0x198200` it builds a car's badge as
`hash("CARLOGO_" + "CORVETTE")`.

It is used for the `hash` field at offset 32 of every texture index record
(§3), and for the ids in `ZDIR.BIN` (§1) — though see §8 for what remains
unknown about the latter.

Because 33 is odd it is invertible modulo 2^32, so given a known suffix you
can solve exactly for the hash state its prefix must have produced. That makes
guessing an unknown prefix a search over short strings rather than over whole
paths. `tools/hp2_hash.py` computes, verifies and solves.

---

