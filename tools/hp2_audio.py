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
    python hp2_audio.py FILE --probe         measure the stereo interleave
    python hp2_audio.py FILE --interleave=2048   decode with that chunk size
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


def deinterleave(a, chunk):
    """Split a stereo stream that alternates `chunk` bytes per channel."""
    if chunk == 16:
        return a, a                     # stride-16 is read in place, below
    left, right = bytearray(), bytearray()
    for p in range(0, len(a), chunk * 2):
        left += a[p:p + chunk]
        right += a[p + chunk:p + 2 * chunk]
    return bytes(left), bytes(right)


def decode(adpcm, channels, chunk=16):
    if channels == 1:
        return decode_channel(adpcm, 0, 16)
    if chunk == 16:
        left = decode_channel(adpcm, 0, 32)
        right = decode_channel(adpcm, 16, 32)
    else:
        la, ra = deinterleave(adpcm, chunk)
        left = decode_channel(la, 0, 16)
        right = decode_channel(ra, 0, 16)
    n = min(len(left), len(right))
    both = array("h", bytes(4 * n))
    both[0::2] = left[:n]
    both[1::2] = right[:n]
    return both


def fmt_chunk(c, half=None):
    """A chunk size, noting when it means 'the whole channel in one block'."""
    if half and c == half:
        return "{:,} B = whole channel".format(c)
    return "{:,} B".format(c)


def _corr_and_jumps(L, R, per):
    """Channel correlation, and the step across chunk boundaries over the
    average step. `per` is samples per chunk, or 0 when the window holds no
    boundary (a chunk bigger than the window)."""
    n = min(len(L), len(R))
    if n < 4096:
        return None
    lm = sum(L[:n]) / n
    rm = sum(R[:n]) / n
    num = sum((L[i] - lm) * (R[i] - rm) for i in range(n))
    dl = sum((L[i] - lm) ** 2 for i in range(n)) ** 0.5
    dr = sum((R[i] - rm) ** 2 for i in range(n)) ** 0.5
    corr = num / (dl * dr) if dl and dr else 0.0
    jump = float("nan")
    if per and n > per * 2:
        ratios = []
        for ch in (L, R):
            d = [abs(ch[i + 1] - ch[i]) for i in range(n - 1)]
            mean = sum(d) / len(d) or 1.0
            edges = [d[k - 1] for k in range(per, n - 1, per)]
            if edges:
                ratios.append((sum(edges) / len(edges)) / mean)
        if ratios:
            jump = sum(ratios) / len(ratios)
    return corr, jump


def probe_interleave(adpcm, candidates=None, window=1 << 18):
    """Which chunk size splits this stream into two real channels?

    The two channels of a stereo mix correlate strongly; a wrong chunk size
    splices them into each other, and the correlation collapses to about zero.
    Where the window spans several chunks the boundary step is reported too: a
    wrong size jumps at every boundary while staying smooth inside a chunk.

    Candidates run from one ADPCM block up to half the track, because this game
    uses both extremes - 16 bytes in one music bank, and in the other the whole
    left channel followed by the whole right.
    """
    n = len(adpcm)
    if candidates is None:
        candidates = [1 << k for k in range(4, 17)]        # 16 B .. 64 KB
        half = n // 2
        candidates += [half - half % 2048, half - half % 16]
        candidates = sorted({c for c in candidates if 16 <= c <= half})
    rows = []
    for c in candidates:
        if c * 8 <= window:
            # the window holds several chunk pairs: read it contiguously, so
            # boundary steps are measurable too
            big = c * 2
            start = min(n // 4 // big * big, max(0, n - window))
            a = adpcm[start:start + window]
            if c == 16:
                L, R = decode_channel(a, 0, 32), decode_channel(a, 16, 32)
            else:
                la, ra = deinterleave(a, c)
                L, R = decode_channel(la, 0, 16), decode_channel(ra, 0, 16)
            per = c // 16 * 28
        else:
            # a chunk larger than the window: take the same slice out of each
            # channel's turn instead, and judge on correlation alone
            pairs = n // (2 * c)
            if pairs < 1:
                continue
            lo = (pairs // 4) * 2 * c
            take = min(c, window)
            la, ra = adpcm[lo:lo + take], adpcm[lo + c:lo + c + take]
            if len(la) < 4096 or len(ra) < 4096:
                continue
            L, R = decode_channel(la, 0, 16), decode_channel(ra, 0, 16)
            per = 0
        got = _corr_and_jumps(L, R, per)
        if got:
            rows.append((c, got[1], got[0]))
    return rows


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
    probe = "--probe" in argv
    chunk = next((int(a.split("=", 1)[1]) for a in argv if a.startswith("--interleave=")), None)

    if not files:
        print(__doc__.split("Usage:")[1])
        return

    for path in files:
        d, ents, how = load(path)
        stem = os.path.splitext(os.path.basename(path))[0]
        outdir = outroot or os.path.join("audio", stem)
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
        if probe:
            for i, name, off, dsz, rate, ch in chosen:
                print("  [%d] %s" % (i, name[:70]))
                if ch != 2:
                    print("      mono - nothing to interleave")
                    continue
                adpcm = d[off + 48: off + 48 + dsz]
                rows = probe_interleave(adpcm)
                half = len(adpcm) // 2
                if not rows:
                    print("      too short to probe")
                    continue
                best = max(rows, key=lambda r: r[2])
                print("      %-14s %-12s %s" % ("chunk", "jump", "L/R correlation"))
                for c, jmp, corr in rows:
                    js = "    -" if jmp != jmp else "%5.2f" % jmp
                    print("      %-14s %-12s %+.2f%s"
                          % (fmt_chunk(c, half), js, corr,
                             "   <-- two real channels" if c == best[0] else ""))
                if best[2] < 0.2:
                    print("      inconclusive: no chunk size gives two correlated channels.")
                    print("      This track may not be interleaved stereo at all.")
                else:
                    print("      interleave looks like %s per channel" % fmt_chunk(best[0], half))
            continue
        os.makedirs(outdir, exist_ok=True)
        print("  writing %d WAV files to %s (about %.0f MB)" % (len(chosen), outdir, mb * 3.5))
        for n, (i, name, off, dsz, rate, ch) in enumerate(chosen):
            adpcm = d[off + 48: off + 48 + dsz]
            c = chunk
            if c is None and ch == 2:
                # the interleave is a property of the bank and the banks differ,
                # so measure it rather than assume one
                rows = probe_interleave(adpcm)
                top = max(rows, key=lambda r: r[2]) if rows else None
                c = top[0] if top and top[2] >= 0.2 else 16
                print("      interleave %s%s"
                      % (fmt_chunk(c, len(adpcm) // 2), "" if top and top[2] >= 0.2 else " (assumed; could not measure)"))
            pcm = decode(adpcm, ch, c or 16)
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
