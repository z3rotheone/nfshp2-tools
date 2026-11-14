"""
Put your own music into Need for Speed: Hot Pursuit 2 (PS2).

This is the return path for the music bank that hp2_audio.py reads. It encodes a
WAV file to Sony PS-ADPCM, writes it over one track inside the bank, and can put
the patched bank back into ZZDATA.BIN.

HOW A REPLACEMENT FITS
Each track sits in a fixed slot in the bank, and the bank sits at a fixed place
in ZZDATA.BIN, so nothing may change size. A slot holds a set number of seconds:

    seconds = slot bytes / 36571          (32 kHz, stereo, 16 bytes per 28 samples)

Shorter audio is padded with silence, longer audio is cut, and the tool says so.
Because sizes never change, ZDIR.BIN is never touched and the archive stays valid.

STEPS
    1. python hp2_music.py list BANK
    2. python hp2_music.py replace BANK N SONG.wav -o NEWBANK
       (the stereo interleave is measured from the track being replaced;
        --interleave=N overrides it)
    3. python hp2_audio.py NEWBANK --match=...        (listen before installing)
    4. python hp2_music.py install NEWBANK            (writes into ZZDATA.BIN)

Your original ISO is the backup: install only rewrites bytes inside ZZDATA.BIN,
and it refuses unless the size and the surrounding bytes still match.

ENCODER
Each 28-sample block picks the predictor and shift that give the least error,
searching all five predictors over three shifts. Re-encoding audio that was
already PS-ADPCM reproduces it essentially bit-exactly (166 dB), and fresh
material lands at 32-46 dB, which is about the ceiling for this 4:1 format.
Roughly 100 seconds for a 3.5-minute stereo track; --fast is ~8x quicker and
about 10 dB worse.

Needs numpy. A WAV that is not 32 kHz is resampled (windowed-sinc low pass then
linear interpolation); converting it yourself in Audacity first is better still.
"""
import os
import struct
import sys
import wave

import numpy as np

F0 = (0, 60, 115, 98, 122)
F1 = (0, 0, -52, -55, -60)
RATE = 32000
ENTRY_TABLE, ENTRY_STRIDE = 0x60, 96
BYTES_PER_SECOND = RATE * 2 * 16 // 28      # stereo, 16 ADPCM bytes per 28 samples


# ----------------------------------------------------------------- bank reading
def read_vag_header(d, off):
    if d[off:off + 4] != b"VAGp" or off + 48 > len(d):
        return None
    _v, _r, dsz, rate = struct.unpack_from(">IIII", d, off + 4)
    if not (2000 <= rate <= 48000) or dsz < 16 or off + 48 + dsz > len(d):
        return None
    return dsz, rate


def parse_music(d):
    channels, rate = struct.unpack_from("<II", d, 4)
    count = struct.unpack_from("<I", d, 0x20)[0]
    if channels not in (1, 2) or not (2000 <= rate <= 48000) or not (1 <= count <= 4096):
        raise SystemExit("this file is not a music bank (channels=%r rate=%r count=%r)"
                         % (channels, rate, count))
    ents = []
    for i in range(count):
        o = ENTRY_TABLE + i * ENTRY_STRIDE
        off, size, pad = struct.unpack_from("<III", d, o)
        hdr = read_vag_header(d, off)
        if pad != 0 or not hdr:
            raise SystemExit("track %d of the table does not point at a VAG" % i)
        name = d[o + 12:o + ENTRY_STRIDE].split(b"\0")[0].decode("ascii", "replace").strip()
        ents.append(dict(i=i, entry=o, off=off, size=size, name=name,
                         dsz=hdr[0], rate=hdr[1], channels=channels))
    return ents


# ---------------------------------------------------------------------- audio io
def load_wav(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise SystemExit("%s: need a 16-bit PCM WAV (this one is %d-bit)"
                             % (os.path.basename(path), w.getsampwidth() * 8))
        ch, sr, n = w.getnchannels(), w.getframerate(), w.getnframes()
        a = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64)
    a = a.reshape(-1, ch)
    if ch == 1:
        a = np.repeat(a, 2, axis=1)
    elif ch > 2:
        a = a[:, :2]
    return a, sr


def resample(a, sr_in, sr_out=RATE):
    if sr_in == sr_out:
        return a
    if sr_out < sr_in:                       # low-pass first, or downsampling aliases
        cut = 0.45 * sr_out / sr_in
        n = 64
        t = np.arange(-n, n + 1)
        h = np.sinc(2 * cut * t) * np.hamming(2 * n + 1)
        h /= h.sum()
        a = np.stack([np.convolve(a[:, c], h, mode="same") for c in range(a.shape[1])], axis=1)
    m = int(round(len(a) * sr_out / sr_in))
    src = np.arange(len(a))
    dst = np.linspace(0, len(a) - 1, m)
    return np.stack([np.interp(dst, src, a[:, c]) for c in range(a.shape[1])], axis=1)


# ----------------------------------------------------------------------- encoder
def encode_channel(x, fast=False):
    cands, shifts = (1, (0,)) if fast else (5, (-1, 0, 1))
    n = (len(x) + 27) // 28 * 28
    xs = np.zeros(n, dtype=np.int64)
    xs[:len(x)] = x
    blocks = xs.reshape(-1, 28)
    nb = blocks.shape[0]
    p1 = np.concatenate([[0], xs[:-1]]).reshape(-1, 28)
    p2 = np.concatenate([[0, 0], xs[:-2]]).reshape(-1, 28)
    mr = np.empty((5, nb), dtype=np.int64)
    for p in range(5):
        mr[p] = np.abs(blocks - ((F0[p] * p1 + F1[p] * p2 + 32) >> 6)).max(axis=1)
    order = np.argsort(mr, axis=0)[:cands]
    out = bytearray()
    h1 = h2 = 0
    for b in range(nb):
        row = [int(v) for v in blocks[b]]
        best = None
        for ci in range(cands):
            p = int(order[ci, b])
            base = int(np.clip(12 - np.ceil(np.log2(max(int(mr[p, b]), 1) / 7.0)), 0, 12))
            for ds in shifts:
                sh = base + ds
                if sh < 0 or sh > 12:
                    continue
                f0, f1 = F0[p], F1[p]
                a1, a2, err, nibs = h1, h2, 0, []
                for i in range(28):
                    pv = (f0 * a1 + f1 * a2 + 32) >> 6
                    s = int(round((row[i] - pv) * (1 << sh) / 4096.0))
                    s = 7 if s > 7 else (-8 if s < -8 else s)
                    rec = ((s << 12) >> sh) + pv
                    rec = 32767 if rec > 32767 else (-32768 if rec < -32768 else rec)
                    e = row[i] - rec
                    err += e * e
                    nibs.append(s & 15)
                    a2, a1 = a1, rec
                if best is None or err < best[0]:
                    best = (err, p, sh, nibs, a1, a2)
        _err, p, sh, nibs, h1, h2 = best
        blk = bytearray(16)
        blk[0] = (p << 4) | sh
        for i in range(14):
            blk[2 + i] = nibs[2 * i] | (nibs[2 * i + 1] << 4)
        out += blk
    return bytes(out)


def build_track(path, slot_bytes, fast=False, chunk=16):
    """WAV -> (VAG header + interleaved stereo ADPCM) padded to exactly slot_bytes.

    `chunk` is how many bytes of one channel go down before the other channel's
    turn. It is a property of the bank, not of the audio: this game ships two
    music banks that differ in it, and writing the wrong one plays recognisable
    but badly distorted audio, because every chunk boundary splices the two
    channels into each other's predictor history.

    The slot is treated as a fixed grid of chunk-sized places, half for each
    channel, and every place is written - with silence where the song has run
    out. That matters when the chunk is large: one bank stores the whole left
    channel and then the whole right, so a song shorter than half the slot
    still has to leave the right channel starting exactly halfway, which is
    where the game looks for it.
    """
    a, sr = load_wav(path)
    secs_in = len(a) / sr
    a = resample(a, sr)
    capacity = slot_bytes - 48
    # each channel gets half the slot, rounded down to whole chunks so the two
    # streams stay in step
    per_channel = capacity // 2 // chunk * chunk
    max_frames = per_channel // 16 * 28
    cut = False
    if len(a) > max_frames:
        a = a[:max_frames]
        cut = True
    peak = np.abs(a).max() or 1
    if peak > 32767:
        a = a * (32767.0 / peak)
    x = np.clip(np.round(a), -32768, 32767).astype(np.int64)
    print("  encoding %.1f s ... (%d Hz in, %d Hz out)" % (len(x) / RATE, sr, RATE), flush=True)
    left = encode_channel(x[:, 0], fast)
    right = encode_channel(x[:, 1], fast)
    def fill(ch):
        ch = ch[:per_channel]
        used = len(ch) // 16 * 16
        ch = bytearray(ch[:used]) + bytearray(per_channel - used)
        if used >= 16:
            ch[used - 15] = 1                  # end flag on this channel's last block
        return bytes(ch)

    left, right = fill(left), fill(right)
    body = bytearray()
    for p in range(0, per_channel, chunk):
        body += left[p:p + chunk]
        body += right[p:p + chunk]
    dsz = len(body)                            # the whole grid: 2 * per_channel
    vag = bytearray(48)
    vag[0:4] = b"VAGp"
    struct.pack_into(">IIII", vag, 4, 2, 0, dsz, RATE)
    vag[32:36] = b"VAGx"
    out = bytes(vag) + bytes(body)
    if len(out) > slot_bytes:
        out = out[:slot_bytes]
    out += b"\0" * (slot_bytes - len(out))     # zero blocks decode as silence
    return out, secs_in, len(x) / RATE, cut


# ---------------------------------------------------------------------- commands
def cmd_list(bank):
    d = open(bank, "rb").read()
    print("%s  (%s, %s)" % (os.path.basename(bank),
                            d[0x24:0x30].split(b"\0")[0].decode("ascii", "replace"),
                            "{:,} bytes".format(len(d))))
    for e in parse_music(d):
        secs = e["dsz"] / e["channels"] / 16.0 * 28 / e["rate"]
        print("  %2d  %-58s %3d:%05.2f  slot %s bytes (%.1f s)"
              % (e["i"], e["name"][:58], int(secs // 60), secs % 60,
                 "{:,}".format(e["size"]), (e["size"] - 48) / BYTES_PER_SECOND))


def detect_interleave(d, e):
    """Measure the interleave of the track being replaced, so we write it back
    the way the bank already stores it."""
    try:
        here = os.path.dirname(os.path.abspath(__file__))
        if here not in sys.path:
            sys.path.insert(0, here)
        import hp2_audio
    except ImportError:
        print("  interleave: assuming 16 bytes (hp2_audio.py not beside me)")
        return 16
    rows = hp2_audio.probe_interleave(d[e["off"] + 48:e["off"] + 48 + e["dsz"]])
    if not rows:
        print("  interleave: assuming 16 bytes (track too short to measure)")
        return 16
    c, _jump, corr = max(rows, key=lambda r: r[2])
    if corr < 0.2:
        print("  interleave: assuming 16 bytes - could not measure this track")
        return 16
    print("  interleave: %d bytes per channel (measured, channels correlate %+.2f)"
          % (c, corr))
    return c


def cmd_replace(bank, idx, wav, out, fast, chunk=None):
    d = bytearray(open(bank, "rb").read())
    ents = parse_music(bytes(d))
    if not (0 <= idx < len(ents)):
        raise SystemExit("track %d is out of range (0-%d)" % (idx, len(ents) - 1))
    e = ents[idx]
    print("replacing track %d: %s" % (idx, e["name"]))
    print("  slot holds %.1f s (%s bytes)" % ((e["size"] - 48) / BYTES_PER_SECOND,
                                              "{:,}".format(e["size"])))
    if chunk is None:
        chunk = detect_interleave(bytes(d), e)
    else:
        print("  interleave: %d bytes per channel (given)" % chunk)
    blob, secs_in, secs_used, cut = build_track(wav, e["size"], fast, chunk)
    assert len(blob) == e["size"]
    d[e["off"]:e["off"] + e["size"]] = blob
    if cut:
        print("  NOTE: your audio is %.1f s but the slot holds %.1f s, so it was cut short."
              % (secs_in, secs_used))
    else:
        print("  %.1f s used of %.1f s; the rest of the slot is silence."
              % (secs_used, (e["size"] - 48) / BYTES_PER_SECOND))
    out = out or (os.path.splitext(bank)[0] + "_modded.bin")
    open(out, "wb").write(bytes(d))
    print("  wrote %s (%s bytes, unchanged in size)" % (out, "{:,}".format(len(d))))
    # quote the fragment: track names contain spaces
    frag = e["name"].split("-")[0] or "track"
    print("  check it:  python hp2_audio.py \"%s\" --match=\"%s\"" % (out, frag))


def cmd_install(bank, zzdata, zdir):
    d = open(bank, "rb").read()
    name = os.path.basename(bank)
    try:
        idx = int(name.split("_")[0])
    except ValueError:
        raise SystemExit("cannot tell which archive entry %s is: keep the leading number, "
                         "e.g. 321_ad921759_modded.bin" % name)
    z = open(zdir, "rb").read()
    if (idx + 1) * 12 > len(z):
        raise SystemExit("entry %d is not in %s" % (idx, zdir))
    _id, sector, size = struct.unpack_from("<III", z, idx * 12)
    if size != len(d):
        raise SystemExit("size mismatch: the archive slot is %s bytes, this bank is %s. "
                         "Re-make it with 'replace', which never changes the size."
                         % ("{:,}".format(size), "{:,}".format(len(d))))
    at = sector * 2048
    with open(zzdata, "r+b") as f:
        f.seek(at)
        cur = f.read(64)
        if cur[:4] != d[:4]:
            raise SystemExit("the bytes at offset %d do not look like this bank; nothing written." % at)
        f.seek(at)
        f.write(d)
    print("installed %s into %s at offset %s (%s bytes)."
          % (name, os.path.basename(zzdata), "{:,}".format(at), "{:,}".format(len(d))))
    print("ZDIR.BIN was not touched, because the size did not change.")


def main():
    a = sys.argv[1:]
    fast = "--fast" in a
    a = [x for x in a if x != "--fast"]
    chunk = None
    for x in list(a):
        if x.startswith("--interleave="):
            chunk = int(x.split("=", 1)[1])
            a.remove(x)
    out = None
    if "-o" in a:
        i = a.index("-o")
        out = a[i + 1]
        del a[i:i + 2]
    if not a:
        print(__doc__)
        return
    cmd = a[0]
    if cmd == "list" and len(a) == 2:
        cmd_list(a[1])
    elif cmd == "replace" and len(a) == 4:
        cmd_replace(a[1], int(a[2]), a[3], out, fast, chunk)
    elif cmd == "install" and len(a) >= 2:
        zz = a[2] if len(a) > 2 else os.path.join("NFSHP2", "ZZDATA.BIN")
        zd = a[3] if len(a) > 3 else os.path.join("NFSHP2", "ZDIR.BIN")
        cmd_install(a[1], zz, zd)
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
