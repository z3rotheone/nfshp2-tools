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
| 0 | u32 | id — a hash of the original path (see §7) |
| 4 | u32 | offset, **in 2048-byte sectors** |
| 8 | u32 | size, **in bytes** |

Records are ordered by offset. 33 of the 405 have size 0; they appear to be
placeholders or aliases (*inferred*). The remaining 372 are the real files.

Entries are sector-aligned so the PS2 can DMA straight from the disc, which is
also why the archive holds many byte-identical duplicates: a copy of a texture
pack sits next to each track that needs it, avoiding a seek. Any mod that
changes a shared asset has to change every copy.

Read with `tools/hp2_index.py`.

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

*Verified against data; the stereo interleave was measured, not assumed.*

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

### Stereo interleave

The two channels alternate **one 16-byte ADPCM block at a time**: L, R, L, R.
Decode each channel with its own predictor history.

This was established by measurement rather than taken from the header. Decoded
as a single channel the waveform breaks every 28 samples — the block boundary,
where the data jumps between channels. Split into alternating blocks, both
channels are continuous and correlate at 0.80, as real stereo music does.

A track's playing time is therefore `data_size / 2 / 16 * 28 / 32000` seconds.

Read with `tools/hp2_audio.py`, write with `tools/hp2_music.py`.

---

## 7. Open questions

Things not yet worked out, roughly in order of how tractable they look:

- **What string the `ZDIR.BIN` ids hash.** The *function* is solved (§9) and
  the ids are produced by it, but the exact string it is given is not known.
  A pack's own stored path (`TRACK11\\TrackTextures.tpk`) does not reproduce
  its archive id under any prefix, suffix, case or separator tried, nor with
  the 16-byte group field prepended or the 64-byte field's NUL padding
  included. Since the hash is invertible you can work backwards: for pack
  `241` the prefix would have to leave the hash standing at `0xf27afbe9`
  before the stored path begins (`tools/hp2_hash.py --solve`). Finding the
  caller that hashes a path before a `ZDIR` lookup would settle it.
- **Car models** — 88 `GEOMETRY.BIN` and 6 `DASHGEOM.BIN` files, untouched.
- **Track scenery** — the `eLabScenery0.bin` files, 7–11 MB each.
- **Menu text** — the `FE_ENGLISH.bin` family. Strings are readable; the
  surrounding structure is not parsed.
- **Video** — 21 AVI files extract cleanly as containers, but the codec is
  probably custom; the executable has `movieplayer/dctimage.cpp` among its
  source paths.
- **Three textures** with 128-byte and zero-length palettes.

---

## 8. The executable

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
## 9. The name hash

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
(§3), and for the ids in `ZDIR.BIN` (§1) — though see §7 for what remains
unknown about the latter.

Because 33 is odd it is invertible modulo 2^32, so given a known suffix you
can solve exactly for the hash state its prefix must have produced. That makes
guessing an unknown prefix a search over short strings rather than over whole
paths. `tools/hp2_hash.py` computes, verifies and solves.

---

