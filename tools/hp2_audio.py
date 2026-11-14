"""
Extract the audio from Need for Speed: Hot Pursuit 2 (PS2) to WAV.

Two kinds of file are handled.

1. SOUND BANK (e.g. 359_e2f0cc8a.bin) - mono effects and interface sounds.
   Chunks of u32 id + u32 size, little-endian, a container being an id with the
   top bit set, the same convention as the .tpk texture packs:
       0x80014103  one sound (container)
           0x00000001   its name
           0x00014204   a Sony VAG file

2. MUSIC BANK (e.g. 321_ad921759.bin) - stereo streams.
   A header at offset 0:
       +0x04 channel count   +0x08 sample rate   +0x20 track count
       +0x24 bank name ("InGame"), padded with 0xCC
   then a table of 96-byte entries starting at 0x60, each holding the track's
   offset (u32), its size (u32), a zero, and its name as
   "Artist-Title-Album-Genre-Number".

Both store Sony VAG: a 48-byte big-endian header ('VAGp', version, reserved,
data size, sample rate) followed by PS-ADPCM in 16-byte blocks. Each block has
the shift in the low nibble of byte 0 and the predictor in the high nibble,
flags in byte 1 (7 ends the sound), then 28 four-bit samples, low nibble first.

A stereo track interleaves the two channels one 16-byte block at a time. That
was measured, not assumed: decoded as a single channel the signal breaks every
28 samples, and split into alternating blocks both channels run smooth and
correlate at 0.80, as real stereo music does. (The 4096 at +0x10 of the music
header is some other buffer size; it is not the audio interleave.)

If a file matches neither layout, the script scans it for VAG headers instead,
so an unfamiliar bank still extracts.

Usage:
    python hp2_audio.py FILE --list          list the contents, decode nothing
    python hp2_audio.py FILE                 extract everything to WAV
    python hp2_audio.py FILE --match=rush    only names containing "rush"
    python hp2_audio.py FILE --out=D         write into D

Decoding runs at roughly 2 MB of audio a second and WAV is about 3.5x the size
of the compressed audio, so use --list first on the large banks.
"""
import os
import re
import struct
import sys
import wave
from array import array

F0 = (0, 60, 115, 98, 122)
F1 = (0, 0, -52, -55, -60)
SOUND, NAME, VAG = 0x80014103, 0x00000001, 0x00014204
ENTRY_TABLE, ENTRY_STRIDE = 0x60, 96


def decode_channel(a, first, stride):
    """Decode one channel's PS-ADPCM blocks, starting at byte `first` and
    advancing `stride` bytes per block (16 for mono, 32 for one of two channels)."""
    out = array("h")
    h1 = h2 = 0
    for p in range(first, len(a) - 15, stride):
        b0 = a[p]
        if a[p + 1] == 7:
            break
        shift = b0 & 0x0F
        pred = (b0 >> 4) & 0x0F
        if pred > 4:
            pred = 0
        if shift > 12:
            shift = 9
        f0, f1 = F0[pred], F1[pred]
        blk = []
        push = blk.append
        for byte in a[p + 2:p + 16]:
            for nib in (byte & 0x0F, byte >> 4):
                s = nib - 16 if nib > 7 else nib
                v = ((s << 12) >> shift) + ((f0 * h1 + f1 * h2 + 32) >> 6)
                if v > 32767:
                    v = 32767
                elif v < -32768:
                    v = -32768
                push(v)
                h2 = h1
                h1 = v
        out.extend(blk)
    return out


def decode(adpcm, channels):
    if channels == 1:
        return decode_channel(adpcm, 0, 16)
    left = decode_channel(adpcm, 0, 32)
    right = decode_channel(adpcm, 16, 32)
    n = min(len(left), len(right))
    both = array("h", bytes(4 * n))
    both[0::2] = left[:n]
    both[1::2] = right[:n]
    return both


def read_vag_header(d, off):
    """(data size, sample rate) if a valid VAG header sits at off, else None."""
    if d[off:off + 4] != b"VAGp" or off + 48 > len(d):
        return None
    _ver, _res, dsz, rate = struct.unpack_from(">IIII", d, off + 4)
    if not (2000 <= rate <= 48000) or dsz < 16 or off + 48 + dsz > len(d):
        return None
    return dsz, rate


def parse_music(d):
    """Entries of a music bank, or None if this is not one."""
    if len(d) < 0x200:
        return None
    channels, rate = struct.unpack_from("<II", d, 4)
    count = struct.unpack_from("<I", d, 0x20)[0]
    if channels not in (1, 2) or not (2000 <= rate <= 48000) or not (1 <= count <= 4096):
        return None
    ents = []
    for i in range(count):
        o = ENTRY_TABLE + i * ENTRY_STRIDE
        if o + 16 > len(d):
            return None
        off, _size, pad = struct.unpack_from("<III", d, o)
        if pad != 0 or not read_vag_header(d, off):
            return None
        name = d[o + 12:o + ENTRY_STRIDE].split(b"\0")[0].decode("ascii", "replace").strip()
        ents.append((name or "track_%03d" % i, off, channels))
    return ents


def parse_bank(d):
    """Entries of a chunked sound bank, or None if this is not one."""
    ents = []
    pos = 0
    while pos + 8 <= len(d):
        cid, sz = struct.unpack_from("<II", d, pos)
        body = pos + 8
        if body + sz > len(d):
            return None
        if cid == SOUND:
            name = vag = None
            p = body
            while p + 8 <= body + sz:
                c2, s2 = struct.unpack_from("<II", d, p)
                if p + 8 + s2 > body + sz:
                    return None
                if c2 == NAME:
                    name = d[p + 8:p + 8 + s2].split(b"\0")[0].decode("ascii", "replace")
                elif c2 == VAG:
                    vag = p + 8
                p += 8 + s2
            if vag is not None:
                ents.append((name or "sound", vag, 1))
        pos = body + sz
    return ents if (pos == len(d) and ents) else None


def scan_vags(d):
    ents = []
    pos = 0
    while True:
        i = d.find(b"VAGp", pos)
        if i < 0:
            return ents
        if read_vag_header(d, i):
            ents.append(("sound_%04d" % len(ents), i, 1))
            pos = i + 4
        else:
            pos = i + 1


def load(path):
    d = open(path, "rb").read()
    for fn, how in ((parse_music, "music bank"), (parse_bank, "sound bank")):
        ents = fn(d)
        if ents:
            return d, ents, how
    ents = scan_vags(d)
    if not ents:
        raise SystemExit("%s: no VAG audio found" % os.path.basename(path))
    return d, ents, "scanned for VAG headers"


def safe(name):
    return re.sub(r"_+", "_", re.sub(r"[^A-Za-z0-9._()-]", "_", name)).strip("_")[:80]


def main():
    argv = sys.argv[1:]
    files = [a for a in argv if not a.startswith("--")]
    listing = "--list" in argv
    match = next((a.split("=", 1)[1].lower() for a in argv if a.startswith("--match=")), None)
    outroot = next((a.split("=", 1)[1] for a in argv if a.startswith("--out=")), None)
    base = r"C:\Users\Clear\Desktop\hp2-decomp"

    if not files:
        print(__doc__.split("Usage:")[1])
        return

    for path in files:
        d, ents, how = load(path)
        stem = os.path.splitext(os.path.basename(path))[0]
        outdir = outroot or os.path.join(base, "audio", stem)
        print("\n=== %s: %d items (%s)" % (os.path.basename(path), len(ents), how))
        chosen = []
        for i, (name, off, ch) in enumerate(ents):
            hdr = read_vag_header(d, off)
            if not hdr:
                print("  [%d] bad VAG header at %d, skipped" % (i, off))
                continue
            dsz, rate = hdr
            if match and match not in name.lower():
                continue
            chosen.append((i, name, off, dsz, rate, ch))
        mb = sum(c[3] for c in chosen) / 1048576.0
        if listing:
            for i, name, off, dsz, rate, ch in chosen:
                secs = dsz / ch / 16.0 * 28 / rate
                print("  %-62s %5d Hz %s %3d:%05.2f" %
                      (name[:62], rate, "stereo" if ch == 2 else "mono  ", int(secs // 60), secs % 60))
            print("  -- %d items, %.1f MB compressed (about %.0f MB as WAV)" % (len(chosen), mb, mb * 3.5))
            continue
        os.makedirs(outdir, exist_ok=True)
        print("  writing %d WAV files to %s (about %.0f MB)" % (len(chosen), outdir, mb * 3.5))
        for n, (i, name, off, dsz, rate, ch) in enumerate(chosen):
            pcm = decode(d[off + 48: off + 48 + dsz], ch)
            fn = os.path.join(outdir, "%03d_%s.wav" % (i, safe(name)))
            with wave.open(fn, "wb") as w:
                w.setnchannels(ch)
                w.setsampwidth(2)
                w.setframerate(rate)
                w.writeframes(pcm.tobytes())
            print("    %d/%d  %s" % (n + 1, len(chosen), os.path.basename(fn)), flush=True)
        print("  done:", outdir)


if __name__ == "__main__":
    main()
