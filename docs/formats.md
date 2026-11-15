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

Paths the executable builds at run time (`NIS\\%s.bin`, `REPLAY\\%s`, `TRACKS\\TRACKB%d.BUN`) are not literal strings, so they were found by solving the hash: it is linear, so the hash of an unknown prefix follows exactly from a known ending. `NIS\\NIS_` came out of both the arrest and ticket cutscenes (their own chunks hold COP_ARREST01, COP_TICKET04...). That named 17 more, and the sound banks 52 (322 of 372). The other 50 are identified by content in `tools/archive_contents.csv`: 36 developer ghost replays (chunk 0x39100 holds the driver name), 26 sound banks, now all named `SOUND\\<NAME>.MSH` (headers) + `.MSB` (samples): COMMON, FRONTEND, INGAME, the five ambiences PARKLAND, DESERT, GREECE, ALPINE, TROPICS, and 18 engines `NEWE<engine>` named by the car database (§12). profile defaults, the police speech event list, four AVIs and the testers' QA notes.

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

Each mip level is laid out by **its own** dimensions, so a 256×128 8-bit
texture has swizzled levels at 256×128 and 128×64 and linear ones below that,
within the same pixel blob.

### Packs inside bundles

Three bundles carry a pack inside them rather than being one: `GLOBAL\\GLOBALB.BUN` (`GlobalTextures.tpk`, 116 textures: windows, underside, plates, brakes, fonts), `GLOBAL\\INGAMEB.BUN` (`InGameTextures.tpk`, 121: HUD, debris, dials) and `FRONTEND\\FRONTB.BUN` (the front end, 361). `hp2_tpk.py embedded EXTRACTED OUTDIR` writes them out as `.tpk` files the other commands read.

### Replacing a texture

Every layout above is a permutation of the stored bytes, so the same table that
reads a texture writes it when used as a scatter instead of a gather. That
makes the import an exact inverse, and `tools/hp2_tpk.py verify` holds it to
that: for every texture and every mip level it takes the bytes as shipped,
unpacks them to indices, packs them again, and requires the result to be
identical — then does the same for the palette.

Indices and palettes are checked separately and against the stored bytes rather
than through a decoded picture, because a palette with two identical colours
makes a pixel's index ambiguous; no round trip through RGBA could be byte-exact,
and a test that went that way would be measuring the wrong thing.

**A replacement cannot change size.** The slot is fixed and the GS has 4 MB of
video memory, so a texture can be repainted but not enlarged; upscaling is a
job for a port, not for the disc. Writing therefore keeps `width × height`,
rebuilds every mip level from the new image, and re-swizzles each by its own
dimensions.

The palette is rebuilt from the new image by median cut — but **not** when the
record's palette offset is shared with another texture, since rewriting it would
damage the other one. In that case the shipped palette is kept and the new image
is quantised to it, which costs quality; the tool says which it did.

Read and write with `tools/hp2_tpk.py`.

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

| Entry | Name | Interleave | Status |
|---|---|---|---|
| 321 | `SOUND\JUKEBOX.JBC` | 16 bytes — one ADPCM block | *measured*, channels correlate 0.80 |
| 322 | `SOUND\JUKEBOXM.JBC` | not 16 bytes | *open* — see below |

Decode each channel with its own predictor history, reassembling it from its
chunks first.

A wrong chunk size splices the two channels into each other's predictor history
at every boundary, so the waveform jumps there while staying smooth inside a
chunk, and the two decoded "channels" stop correlating. `tools/hp2_audio.py
--probe` reports both measures for every candidate size, and both reading and
writing measure the track rather than assume a value.

Channel correlation is the reliable measure; the boundary step is weak and can
be absent. **A moderate correlation is not enough.** If the real layout stores
each channel in one large block, a window taken inside the left channel and
split at some small size yields left-channel data in *both* halves, which are
adjacent in time and so correlate — around 0.45 at 2048 bytes in a measured
case, beating every other small candidate while being entirely wrong. Only the
true split reaches 0.9. Candidates therefore run from one ADPCM block up to
half a track.

#### What is known about entry 322

Patching it with a 16-byte interleave plays recognisable but badly distorted
audio in an emulator, and so does a 2048-byte one, so neither is right.
Decoding its untouched tracks at either size is audibly wrong, while the game
plays them correctly — so the layout is still unidentified.

What the sizes say: in entry 322 every track's data size is an exact multiple
of 2048, and so is half of it (3712, 4320 and 3840 sectors for the first three
tracks — all even). No track in entry 321 is aligned either way. Each channel
of a 322 track therefore begins on a sector boundary, which is what a layout
storing the whole left channel and then the whole right would require. That is
a hypothesis from arithmetic, not a measurement; `--probe` now searches far
enough to test it.

Track durations confirm both banks are genuinely stereo: 3:28, 4:02 and 3:35
for the first three tracks, matching the real songs. Read as mono they would be
twice that.

The `4096` at `+0x10` is **not** the interleave — it is 4096 in both banks,
which differ.

A track's playing time is `data_size / 2 / 16 * 28 / 32000` seconds either way,
since the interleave does not change how much audio is present.

Both banks hold the same 23 tracks under the same names in the same order, but
not byte-identically — every track in 322 is a few thousand bytes longer. The
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

The part header also carries totals, as three u16 forty bytes after the
`0x0000000b` that precedes the name: batches, triangles, vertices. For the
sedan's body that is 37, 481 and 803, which is what decoding the geometry
produces.

Each part has two lists of name hashes (§10), each entry a u32 hash and a u32
of zero, and either list may be preceded by eight bytes of `0x11` filler:

- `0x00034006`, **textures**: `4DR_SEDAN02_SKIN1`, `CARBOTTOM`, and others not
  yet named.
- `0x0003401d`, **materials**: `MIRROR`, `CARSKIN`, `INTERIOR`, `RUBBER`, and
  others not yet named. A batch's material index (§ below) selects from this
  list: index 0 of the sedan's body is `MIRROR` and its two batches are the
  two wing mirrors; index 2 is `CARSKIN` and holds most of the body. One
  material, `0x010cb64a`, is the one whose batches carry no normals, in every
  part that uses it.

### The batch records (`0x00034004`)

One 64-byte record per batch, in draw order:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | **texture**: index into the part's `0x00034006` list |
| 8 | u32 | offset of the batch's VIF packet in the geometry chunk |
| 12 | u8 | packet size in quadwords (the offset steps by 16 times this) |
| 15 | u8 | flags: `0x40`, `0x41`, `0x50` seen, meaning unknown |
| 16 | 3×f32 | bounding box min |
| 28 | u8 | running draw-group number; rises through the list, exceeds the material count, so not the material |
| 29 | u8 | vertices |
| 30 | u8 | triangles |
| 31 | u8 | **material**: index into `0x0003401d` |
| 32 | 3×f32 | bounding box max |
| 44–63 | | unknown; 44–45 repeat 14–15, the last four bytes are zero |

Records are sorted by texture, then material. In the 911's body the first
batches use the per-car brake light and headlight textures, then
`911TURBO_SKIN1` for interior and body. Offsets 0 and 31 were read from one
part's dump; `hp2_geom.py --check` tests on every part that offset 31 matches
the material in the batch's own VU header and that offset 0 is a valid
texture index.

#### Light textures are slots, filled at runtime

The texture list names `<CAR>_HEADLIGHT_LEFT`, `_RIGHT`, `<CAR>_BRAKELIGHT_LEFT`
and `_RIGHT`, but the shared car pack (`012_0287bf4f`) holds no such entries for
most cars. It holds each light in three states: `<CAR>_HEADLIGHT`,
`<CAR>_HEADLIGHT_ON` and `<CAR>_HEADLIGHT_DAMAGE0` (pack names are cut at 23
characters, so this often shows as `_DAMA`). So the left and right slots are
probably filled while the game runs, letting one side be lit or broken on its
own. CROWNVIC, JAGUAR, VANQUISH and CHALLENGE do have `_LEFT`/`_RIGHT` brake
light art. The attachment points of the same names (`HEADLIGHT_LEFT`,
`WINDOW_FRONT`...) are probably where a hit breaks that piece. Inferred from
names; the executable has not been checked.

The six `WINDOW_*` textures and `CARBOTTOM` are not in the car pack either.

The low-detail tyre lists one material and uses indices 0 and 1, so either the
list is shared with its sibling or an out-of-range index has a default.

### The file header's flare records

Six 32-byte records, each: two u32 (both 11), a name hash, the hash of the
car's own base name, a position as three floats, and flags. The names are
`FLARE_HEADLIGHT_LEFT`, `FLARE_HEADLIGHT_RIGHT`, `FLARE_BRAKELIGHT_LEFT` and
`FLARE_BRAKELIGHT_RIGHT` (the sedan has two brake light pairs): where the
light glows are drawn. They are not wheel positions; those are not in this
file.

### Geometry: a VIF display list

`0x00034005` is a PS2 VIF packet stream, the format the hardware DMAs to the
Vector Unit. It alternates `UNPACK` commands, which push data into VU1 memory
at an address given by the tag's immediate, with `MSCAL`, which runs the
microprogram that draws it. Each `MSCAL` is one batch of at most 30 vertices;
the sedan's body is 37 of them.

Within a batch, by VU address:

| Address | Unpack | Data |
|---|---|---|
| 0 | `V4_8`, one quad | vertex count N, (N-1)/4, 3N, material index x 4 |
| 1 | `V4_32`, one quad | strip masks (below) |
| 2 | `V4_32` | positions for the first N - N%4 vertices |
| after them | `S_32`/`V2_32`/`V3_32` x 3 | positions of the N%4 left over |
| 32 | `V4_32` | texture coordinates, two vertices a quad |
| after them | `V2_32` | texture coordinates of an odd last vertex |
| 52 | `V4_5551` | one colour per vertex |
| 92 | `V4_8` | normals, signed bytes; absent for some materials |

**Positions are transposed for the VU1's SIMD registers.** Each group of three
quads holds the X, then the Y, then the Z of the *same four vertices*. Reading
the quads as (x, y, z, w) per vertex produces noise. The leftover vertices use
the same arrangement at their own width: for two of them, `x0 x1`, `y0 y1`,
`z0 z1`. Normals are grouped the same way, twelve bytes a group, and the last
group is zero padded rather than narrowed; every decoded normal has length
126 or 127.

**Texture coordinates** come as `u0 v0 v1 u1` per quad: the second vertex's
pair is stored reversed. Of the four orderings, this is the one under which
vertices repeated within a batch mostly carry the same coordinates (281 of 377
on the sedan; the rest sit on texture seams).

One practical trap: VIF payloads are padded to a 4-byte boundary. A
`V4_5551` run of 29 elements occupies 60 bytes, not 58, and a walker that
misses this desynchronises partway through a batch.

### Strips

A batch is a run of triangle strips with nothing shared between them: where
two strips meet, the vertices are simply stored twice. A vertex either
completes the triangle formed with the two before it, or it does not, and the
first two vertices of every strip do not. Which is which is in the quad at
address 1, as one or two non-zero 32-bit words (which of the four slots they
occupy varies with the batch size and does not matter):

- **The first word counts from the start of the batch.** Bit 14 is vertex 0,
  bit 13 is vertex 1, down to bit 0 for vertex 14. Every batch therefore
  begins `0x6...`: vertices 0 and 1 flagged.
- **The second word counts from the end.** Round N up to a multiple of four
  and call it P. Bit 1 is vertex P, bit 2 is vertex P-1, up to bit 17 for
  vertex P-16. Only bits for vertex 15 and above are needed.
- Bits 19-22 of either word are often set and are not strip flags. Their
  meaning is unknown.
- For the largest batches the two words leave vertex 15 uncovered. A strip
  start is always two flagged vertices in a row, so vertex 15 is flagged when
  vertex 14 is flagged alone or vertex 16 is flagged alone.

Within a strip the winding alternates, and the first triangle faces the way
its vertices' normals point.

The file checks this itself. Chunk `0x00034004` holds one 64-byte record per
batch, preceded in some parts by eight bytes of `0x11`:

| Offset | Type | Meaning |
|---|---|---|
| 0x08 | u32 | offset of the batch within the geometry chunk |
| 0x0C | u16 | its length in 16-byte units |
| 0x10 | 3 x f32 | bounding box minimum |
| 0x1C | u8 | material index |
| 0x1D | u8 | vertex count |
| 0x1E | u8 | **triangle count** |
| 0x20 | 3 x f32 | bounding box maximum |

Decoding the masks as above gives exactly the recorded triangle count for all
all 16,789 batches of all 44 car models on the disc, about 260,000
triangles, and the sedan renders as a closed body with consistent winding.
`tools/hp2_geom.py --check` repeats the comparison for one file and
`--survey` for many.

Batches of fewer than four vertices have no whole group, so their only
position block is the leftover one, at address 2.

In six parts the part header's triangle total exceeds the sum of that part's
own batch records by 2 to 32. Every batch decodes to its record, so this is
the file disagreeing with itself; what the header counts extra is unknown.

None of this needed the VU1 microprogram.

---

### Car textures: what the executable fills in

0x188790 builds each car's table of texture-name hashes from the model name
(`%s` below). This confirms the exporter's rules, which had been inferred
from names only:

| Slot | Texture | Notes |
|---|---|---|
| `SKIN1` (paint) | `%s_SKIN<n>` | n = the colour chosen, byte 5 of the car's settings |
| | `%s_SKIN<n>B` | only when the third argument is 0 |
| | `%s_DASHSKIN<n>` | when byte 6 is set |
| `HEADLIGHT_LEFT/RIGHT` | `%s_HEADLIGHT`, `_ON`, `_DAMAGE0` | three states; left and right use the same state texture |
| `BRAKELIGHT_LEFT/RIGHT` | `%s_BRAKELIGHT`, `_ON`, `_DAMAGE0` | the same |
| `REVERSELIGHT` | `%s_REVERSELIGHT`, `_ON` | |
| shadow | `%s_SHADOWFE` / `%s_SHADOWIG` | front end / in game, chosen by the third argument |
| tyre, wheel blur | `<record>_TIRE`, `<record>_WHEEL_BLUR` | built from the record name, not the model name (§12), so 911TURBOHP draws `911TURBOHP_TIRE` |
| licence plate | `%s_LICENSE_PLATE` | |

**Windows and the underside.** The six window slots (`WINDOW_FRONT`, `_REAR` and the four side windows) all draw one shared texture. `CarRenderInfo` (0x11E860 → 0x11FF68) builds a 12-entry substitution table; for each window it picks from the table at 0x2E0380 by that window's damage level: `WINDOW`, then `WINDOW_DAMAGE0`, `_DAMAGE1`, `_DAMAGE2`. These, and `CARBOTTOM`, live in `GlobalTextures.tpk`, a pack embedded in `GLOBAL\\GLOBALB.BUN` that the texture export had never opened. `WINDOW` is see-through tinted glass with a darker band along the top; the damage levels add more and more cracks. The pack also has every car's licence plate (`<CAR>_LICENSE_PLATE`), `BRAKESFRONT`/`REAR`, `TIREBACK`, `SKRATCHES`, `WINDOW_DASH`, `WINDOW_TRAFFIC`, the headlight flares, cop lights and fonts. That names three of the 911's hashes: `cd677a20` = BRAKESFRONT, `c8c5a8a4` = TIREBACK, `3d743a07` = SKRATCHES. The fourth, `ece04847`, is `WIPER01` (the 911 has `911TURBO_WIPER_LEFT`/`_RIGHT` parts), and no pack on the disc holds it. `hp2_geom.py --obj` now uses these textures, with `map_d` for the glass's alpha.

## 8. Open questions

Things not yet worked out, roughly in order of how tractable they look:

- **The stereo interleave of music bank 322** (§6), the bank the game actually
  plays. 16 bytes and 2048 bytes are both ruled out by ear, in the emulator and
  on decoded originals. Sector alignment of each half points at one block per
  channel; `tools/hp2_audio.py --probe` on an untouched track settles it.
- **The remaining 120 archive filenames.** 252 of 372 are recovered (§1).
  The rest are mostly unnamed blobs whose paths are not literal strings in the
  executable — notably entries 23–58, a run of 36 files of 60–240 KB, which is
  suggestively 6 locations x 6 sub-tracks but matches no `TRACKS\\...` pattern
  tried. They are probably composed at runtime from a table not yet found.
  `tools/hp2_hash.py --solve` narrows a guess: it gives the exact hash state
  any proposed prefix would have to produce.
- **Which texture each material draws with** (§7). Material names are known
  for some (`CARSKIN`, `RUBBER`, `MIRROR`, `INTERIOR`); how a material picks
  from the part's texture list is not.
- ~~**Where wheels attach.**~~ In the car database (§12).
- **Bits 19-22 of the strip mask words** (§7).
- **Track scenery** — models and placement are decoded (§11); the other
  top-level chunks of `eLabScenery99.bin` are not.
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

## 11. Track scenery: `eLabScenery99.bin`

**All 30 tracks.** The archive holds `TRACKS\\TRACKB11` to `TRACKB66` for locations 1, 2, 3, 4 and 6 (there is no location 5): 30 tracks. 8 are stored plain (`.BUN`, the entries named below); every one also exists as a `.LZC` that `hp2_comp.py` decompresses. The 8 that exist both ways decompress byte-identical to the plain copy. Each file carries its own devpath (`TRACK11\\Pipeline\\Scenery\\...`). Within a location, tracks n and n+3 hold the same sections, collision, routes and cameras: the same circuit at another time of day or in other weather (§13); the third and sixth have an open main route (point-to-point). `hp2_scenery.py` passes every check below on all 30: placement 97.8–100%, box tree in every section, collision heights 97.6–100%, every loop closes, every camera fits.


One file per **location**, 9–11 MB, found with `grep -l eLabScenery`:
entries 196, 198, 200 (TRACK24/25/26), 211, 213, 215 (TRACK44/45/46),
221, 223 (TRACK65/66). Several tracks share a location's file.

### Models

The models sit in `0x80034000` containers, the same as the cars, and
`hp2_geom.py` decodes them unchanged (7,883 parts across the 8 files, every
`--check` passing). Roads (`RD_SECTIONnn_CHOPn`), dirt roads (`RDDRT_`,
`DIRTRD_`), terrain (`TRN_`), the skydome and the `PANORAMA` ring are stored in
world space. Props (`XT_` plants, `XW_` poles and windmills, `XS_` signs,
`*_SHADOW_*`, `SHAD_*`) are stored once at the origin and placed by the
sections below.

### Sections: `0x80034100`

One per track section (63–159 per file; numbers like 41–187), each followed
by 504 zero bytes. Four children, any of which may start with `0x11` filler:

| Chunk | Contents |
|---|---|
| `0x00034101` | 56 bytes: `u32 0, u32 0, u32 section number`, then zeros |
| `0x00034102` | the models this section uses, 40 bytes each |
| `0x00034103` | the instances, 48 bytes each |
| `0x00034104` | a box tree over the instances, for culling |

**Model record (`0x34102`, 40 bytes):**

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | part name hash |
| 4, 8 | u32 | two more part hashes, 0 if none (probably lower-detail versions) |
| 12, 14 | u16 | 0x10/0 for most; 10/20, 3/30, 50/70 on plants (probably LOD distances) |
| 16 | u32 | 0, 3 or 10 (always 0 in TRACK44 onward) |
| 20 | 16 bytes | zero |
| 36 | f32 | radius of the model |

The hash is a part of the same file for 98.5% of records (16,807 of 17,056).
Most others are road and terrain sections of a neighbouring location
(entry 200 uses TRACK24's SECTION70 pieces; 223 uses SECTION10 from another
file). About 120 distinct hashes are parts of no extracted file: probably
objects the game spawns itself.

**Instance record (`0x34103`, 48 bytes):**

| Offset | Type | Meaning |
|---|---|---|
| 0 | 6 × s16 | world bounding box, min xyz then max xyz |
| 12 | u16 | index into this section's model list |
| 14 | u8 | 4 or 8 on a few dozen instances, else 0 (unknown) |
| 15 | u8 | kind: 0 placed model, 1 ground shadow decal, 2 world-space part |
| 16 | 3 × f32 | position |
| 28 | 9 × s16 | 3×3 matrix, 0x4000 = 1.0, row-major |
| 46 | u16 | 0 |

`world = position + v · M`: each row of M is one of the model's axes in the
world. M may carry a uniform scale (0.5, 0.75 on bushes and trees; 8,778 of
20,573 instances in TRACK44). Kind 2 has position 0 and the identity matrix.

**Check:** placing each model's vertices this way reproduces the instance's
stored box within 1.5 units for 99.3–100% of instances in every file
(`hp2_scenery.py FILE`). The transposed matrix fits clearly worse. The misses
are a few oversized evergreens and the shadow decals, which sit about
3 units off (projected onto the ground at run time?). Rendered section 41 of
entry 196: pylons upright along the dirt trail, plants on the ground.

**Box tree (`0x34104`):** nodes of `6 × s16 box, u16 n, n × s16 children`.
Each node starts on an 8-byte boundary **of the file**, not of the chunk.
The chunk may end in zero bytes. A child ≥ 0 is an instance, a child < 0 is
node `-child`, and node 0 is the root. Every instance appears exactly once in
all 891 sections.


### Collision: `0x34130`–`0x34133`

A separate collision mesh, found through a 32-unit grid. In entry 196 it has
19,941 polygons in 923 cells.

**Cells (`0x34131`):** `u32 999999, u32 999999, u32 0`, then 12 bytes per
cell: `f32 x, f32 y` (the cell's centre, always 16 mod 32), `u16 start,
u16 count` into `0x34133`. The starts chain (each start is the previous
start + count), and every polygon a cell lists overlaps that cell, in all
8 files.

**Index list (`0x34133`):** u16 polygon indices, the cells' lists back to back.

**Polygon (`0x34132`, 32 bytes):**

| Offset | Type | Meaning |
|---|---|---|
| 0 | u16 | high byte: four 2-bit fields (a rotation of 0,1,2,3 on quads; vertex order?); low byte 0 |
| 2 | u16 | surface (below) |
| 4 | s16 | base height, whole units |
| 6 | u16 | 0xFFFF, else a small number (about 5% of polygons; unknown) |
| 8 | 4 × s16 | x of the 4 vertices, 1/8 units |
| 16 | 4 × s16 | y, 1/8 units |
| 24 | 4 × s16 | height above the base, 1/256 units: **z = base + v/256** |

A triangle repeats its third vertex as the fourth.

**Check:** most collision vertices sit on a road or terrain mesh vertex in
x and y. Leaving out the walls, 97.6–100% of those (99.98% in entry 196)
match that vertex's height within 0.05 units.

**Surface word:** the low nibble is the surface. The names below come from
the textures of the visible faces the polygons sit on:

| Nibble | Surface |
|---|---|
| 1 | asphalt |
| 2 | dirt |
| 3 | gravel, packed dirt road |
| 4 | puddles, leaves |
| 5 | bridge deck |
| 6 | grass |
| 7 | road shoulder |
| 9 | verge, forest floor (mixed) |
| b | leaves on the road |
| c | sidewalk, curb |
| e | bridge and lava-road blends (mixed) |

The upper byte `0x16` or `0x12` marks a **wall** with no visible mesh. 97% of
walls run from z = -127 to +127, the full range the encoding allows: the
track's boundary lines, extruded so nothing gets past them. The rest are
10–50 units tall. `0x0800` is set on off-road terrain; `0x1000` is set on
many road faces (meaning unknown).

`0x34130` (4,304 bytes, a u16 table starting 410, 13, 0, 1, 2, …) is unread.

`hp2_scenery.py FILE` checks all of this; `--collision OUT.obj` exports one
group per surface.


### Prop collision: `0x80036000`

This is the first chunk of the file. It gives one placed plant per entry a
collision size, so a car can drive into bushes, yuccas and cacti.

| Chunk | Contents |
|---|---|
| `0x36001` | 24 bytes; the u32 at +12 holds the number of objects in its low half (771 in entry 196) |
| `0x36002` | a box tree over the objects (f32 centre, two u16, f32 half size, u16 children); layout not fully read |
| `0x36003` | the objects, 32 bytes each |

**Object (`0x36003`, 32 bytes):**

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | hash, unique per object |
| 4 | u16, u16 | 3, 1 |
| 8 | u16 | instance index in that section's `0x34103` |
| 10 | u16 | section number |
| 12 | u32 | 0 |
| 16 | 3 × f32 | position, the instance's own |
| 28 | f32 | radius |

**Check:** 3,853 of 3,956 objects (97.4%) sit exactly on the instance they
name. All misses are in files that use sections of a neighbouring
location. The plants are bushes, yuccas, prickly pears and shrubs in the
desert; palms, ferns and dracaenas in TRACK65/66.


### Road quads: `0x80034800`

The road, as quads joined edge to edge down the middle of every road
(533 in entry 196; 533–1,246 per file).

**Header (`0x34801`):** `u32 n, u32 0, u32` (an address), `f32 min x, f32 max x,
f32`, then 256 u16. Each u16 is the first quad whose min x reaches that slice,
for 256 equal slices of x.

**Quad (`0x34802`, 64 bytes):** 4 corners of 16 bytes each (3 × f32 + 4 spare
bytes). The spare bytes hold:

| Corner | Spare 4 bytes |
|---|---|
| 0 | the quad's min x |
| 1 | its max x |
| 2 | flags: low byte = corner count (4, or 3 with corner 3 zero); bits 0x100, 0x400, 0x1000, 0x4000 unknown |
| 3 | 0 |

Quads are sorted by min x. Nearly all have two neighbours; a few are
junctions with 3–4.

### Routes: `0x80034147`

The race routes: a main route plus shortcuts.

**Route (`0x34148`, 40 bytes):**

| Offset | Type | Meaning |
|---|---|---|
| 0 | u8 | id |
| 1 | u8 | 1 = closed loop |
| 2 | u16 | first point |
| 4 | u16 | end |
| 6 | u16 | point count |
| 8 | f32 | progress on the main route where it starts |
| 12 | f32 | progress where it ends (the main route: 0 and its length) |
| 16 | f32 | 1.0 or 1.4 (unknown) |
| 20 | 2 × f32 | x, y of the first point |
| 28 | 2 × f32 | x, y of the last point |

**Point (`0x34149`, 20 bytes):**

| Offset | Type | Meaning |
|---|---|---|
| 0 | 2 × f32 | x, y |
| 8 | 4 × u16 | 0, then up to three indices of other route points (0xFFFF = none) |
| 16 | f32 | progress |

On the main route, progress is the arc length of a smooth curve through
the points, a little more than the straight lines between them. On a loop,
the last point's progress plus the gap back to the start equals the stored
length, within 1 unit, in all 5 looped tracks. On a shortcut, progress is
the main route's progress at that point. It can be shorter than the
shortcut itself, so race position works on any path.

The main route is closed in 5 files and open in 3 (entries 200, 215, 223:
probably the point-to-point tracks). Plotted over the collision mesh,
every route point in entry 196 sits on the drivable surface.

### Police: `0x80034500`

Three children, each starting with a u32 count. All sizes match the record
layouts exactly in all 8 files.

**`0x34510`: radar spots, 32 bytes each**

| Offset | Type | Meaning |
|---|---|---|
| 0 | char[16] | name, e.g. "Radar21_38"; one per file fills all 16 bytes, "Radar21_001_BeA…" |
| 16 | 2 × f32 | x, y |
| 24 | u32 | flag: 1 on that one record |
| 28 | u32 | 1 on one other record, else 0 |

All 231 sit on the drivable surface.

**`0x34520`: roadblocks, 28 bytes each**

| Offset | Type | Meaning |
|---|---|---|
| 0 | char[16] | name `RB<track>_<Easy\|Med\|Hard>_<B\|F\|R><nn>` |
| 16 | 2 × f32 | x, y |
| 24 | u32 | `0x467Cxx` |

In `xx`, the high nibble is the difficulty (8 Easy, 9 Med, A Hard); it
agrees with the name in all 416 of the first 8 files. In TRACKB13/16 every
roadblock is coded one level below its name ("Hard" = 9, "Med" = 8), so the
code, not the name, is probably what the game uses. The low nibble is 0 for B (89 of 92). F and
R share the codes 1/2 in bits 0–1 and 0–2 in bits 2–3 (meaning unknown).
There's no heading, so the game probably lines roadblocks up with the road.
All 416 sit on the drivable surface.

**`0x34530`: zones, 60 bytes each**

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | corner count, 4–6 |
| 4 | 6 × (f32 x, f32 y) | corners, room for 6 |
| 52 | u32 | value, 2–238 (unknown) |
| 56 | u32 | 0, once 1 |

### Replay cameras: `0x80034405`

`0x34440` (20 zero bytes), then one `0x34450` per camera: 120–363 per file,
1,696 in all 8.

**Header:**

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | type: 0 world camera (1,401), 1 (216), 2 (79). Onboard ones ("CamPOnboard_1") are type 1/2, with points in car-relative units |
| 4 | char[] | name ("CamBeforeTunnelXD", "CamMineEntrance"…) |
| 0x74 | 3 × u32 | type 0: area points p, triangles t, camera points c |
| 0x48 | 4 × u32 | type 1: p, t, c, d |
| 0x4C | 4 × u32 | type 2: p, t, c, d |

Type 0 also has a float at 0x44 (0.199, field of view?) and, from 0x80,
-3.0, 1, 0.5, 5000. Every type then has 32 slots of 16 bytes holding one
float (15.0, or 71.5 on type 1/2).

**Tail:** starts at 0x2A0 (type 0) or 0x270 (types 1/2):

| Block | Size |
|---|---|
| camera points (3 × f32 + pad) | c × 16 |
| a second block of the same shape (zeros in many) | c × 16 |
| type 1/2 only: an extra block | d × 16 |
| triangles (3 × u32 + pad) | t × 16 |
| activation area (f32 x, y) | p × 8 |

The size formula fits all 1,696 records. In 1,685 the triangles are a fan
over the area points (t = p − 2), so the area is a polygon on the ground
where the camera takes over. The type-0 camera points sit next to their
area.

### Identified, not decoded

| Chunk | What it holds |
|---|---|
| `80034020` | physics shapes for smashable objects, one group of 5 per object (`34021` name, e.g. "CS_BarrelA_L", "CS_GasPump"; vertex/edge/face counts that satisfy Euler's formula), `34024` vertices, `34023` edges, `34022` face planes, `34025` state |
| `80038000` | ambient sound and effect emitters ("Crickets01", "PowerLine_01", "Sandstorm", "Waterfall01") |
| `34200` | track description text |
| `34202` | world bounds (6000, 5500, 2000) |
| `34250` | contains "FOG<MAIN>": fog/environment settings? |
| `34121` | about 360 records named "RAGNAR" |

Still unread: `80034015` (name hash table), `80034019`, `35020`/`35021`, `34026`, `80034425`,
`34106`, `34122`, `34130`, `34146`, and the inside of the chunks in the table above.

## 12. Car database: chunk `0x00034600`

The car database is in `GLOBAL\\GLOBALB.BUN` (archive entry 302; the `.LZC` is
entry 1). It holds 64 records of 0x560 bytes, from the first 16-byte boundary
after the chunk header. `carinfo.cpp` loads it (0x187F98 keeps a pointer at
`gp-0x5088`). `tools/hp2_carinfo.py` lists, checks and exports it (`--json`).

| Offset | Type | Meaning |
|---|---|---|
| 0x000 | char[32] | record name: `911TURBO`, `911TURBOHP` (the Hot Pursuit version), `BMWM5COP`, `TAXI`… |
| 0x020 | char[32] | model name: the `GEOMETRY.BIN` and texture prefix; the HP record shares it |
| 0x040 | char[32] | `CARS\\<model>\\GEOMETRY.BIN` |
| 0x060 | char[32] | the same path, `.LZC` |
| 0x0C0 | char[32] | make: `PORSCHE`, `FERRARI`… |
| 0x120 | 4 × 32 bytes | the wheels, in the order front left, front right, rear right, rear left (layout below) |
| 0x1E4 | 3 × f32 | length, width, height. All 26 traffic cars hold a 5 × 2 × 1.2 default |
| 0x278 | f32 | final drive |
| 0x288 | u32 | number of forward gears |
| 0x290 | 8 × f32 | reverse, neutral (0), then the forward ratios |
| 0x2B0 | 3 × f32 | idle rpm, a peak rpm, redline rpm |
| 0x320 | f32 | top speed (mph?) |
| 0x330 | char[32] | engine sound, "NewE" + a name |

**Wheel record (32 bytes):**

| Offset | Type | Meaning |
|---|---|---|
| 0 | 3 × f32 | hub x, y, z, in the model's axes (+x front, +y left, +z up) |
| 12 | f32 | radius |
| 16 | f32 | tyre width |
| 20 | u32 | index 0–3 |
| 24 | f32 | toe/camber, ±0.01 |
| 28 | 4 bytes | 0 |

**Engine sound:** the bank is `SOUND\\NEWE<NAME>.MSH` (sample headers) plus
`.MSB` (samples). All 49 records with an engine sound name a bank whose two
files are in the archive.

**Checks:**
- The 911 Turbo's gears (3.82, 2.05, 1.41, 1.12, 0.92, 0.75), final drive 3.44,
  tyres 225/295 mm and size 4.43 × 1.83 × 1.30 m are the real 996 Turbo's.
- Against the 43 car models, the stored length and width agree within 12% for
  41. The McLaren LM model is 14% wider and the Jaguar 11% narrower, so the
  stored size is approximate (a collision box?).
- Every car's wheels sit inside the body's footprint, with the tyres reaching
  0.1–0.35 below the body: the ground clearance.
- This answers "where do the wheels attach": not in `GEOMETRY.BIN`, here.

The rest of the record (0x0F0–0x118, 0x1A0–0x1E0, 0x200–0x270, 0x2C0–0x310 (a
0.4–0.7 curve, torque?), 0x350–0x550) is not read yet.

## 13. Track list: chunk `0x00034201`

The track list is in `GLOBAL\\GLOBALB.BUN`, next to the car database. It has
66 slots of 0xA4 bytes, one per track number; 30 are used. The executable
keeps it at `gp-0x55BC` (0x153608). `tools/hp2_tracklist.py` lists it and
exports JSON.

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | char[32] | name: "Coastal Parklands T11", "Wine Sunset T34"… |
| 0x20 | char[32] | "track, NFS" |
| 0x40 | u8 | 1 on a used slot |
| 0x41 | u8 | track number: `TRACKS\\TRACKB<n>.LZC` |
| 0x42 | u8 | the track whose scenery file contents it shares (14 → 11) |
| 0x43 | u8 | 1 = point-to-point (the 3rd and 6th of each location) |
| 0x44 | u32 | variant: 0 base, 1 overcast, 2 sunset/sunrise/dawn, 4 location 4's sunsets |
| 0x50 | f32 | advertised length (miles?); the route length is 1,110–1,745 × this |
| 0x5C | 3 × f32 | a point 2,000–3,400 units above the track (overview camera?) |
| 0x84 | char[] | location: Parkland, Desert, Greece, Alpine, Tropics |

**Tracks n and n+3 are not reversed.** They're the same circuit at another
time of day or in other weather: "Coastal Parklands" / "Coastal Overcast",
"Wine Country" / "Wine Sunset", "Fall Winds" / "Fall Sunset". The executable
has `TN_TRACK%dR`, but no code uses it.

The location names are the ambience banks' names (`SOUND\\PARKLAND.MSH`…),
so location 3 is "Greece".
