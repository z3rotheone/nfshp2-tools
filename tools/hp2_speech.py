#!/usr/bin/env python3
"""
Police radio and commentary speech: SOUND\\SPEECH\\<LANGUAGE>BAN.PCK (index)
and <LANGUAGE>DAT.PCK (audio), for English, French, German, Italian, Spanish.

BAN.PCK:
    u32 n (1,806 lines in English), u32 table size (n x 16)
    n x 16 bytes: u32 0, u32 offset of the line's audio in DAT.PCK,
                  u32 0, u32 offset of its 44-byte descriptor in BAN.PCK
                  (the last record is not a line: its fields are 0xFFFF0026...)
    descriptors, 44 bytes each; word 0 = 0xFFFF0000 + a line number
    (0x227, 0x228...), the rest not read yet
DAT.PCK: one standard Sony VAG file per line ("VAGp", mono, 24,000 Hz),
each at a 256-byte boundary, decoded with hp2_audio.py.
SOUND\\SPEECH\\EVENTS.EVT presumably maps game events to lines (not read).

Usage:
    hp2_speech.py BAN.PCK DAT.PCK                     list the lines
    hp2_speech.py BAN.PCK DAT.PCK --wav OUTDIR [FIRST [COUNT]]
                                                      write lines as WAV
"""
import os
import struct
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hp2_audio


def lines(ban):
    n = struct.unpack_from("<I", ban, 0)[0]
    out = []
    for i in range(n):
        _a, dat_off, _b, desc_off = struct.unpack_from("<4I", ban, 8 + 16 * i)
        if desc_off + 44 > len(ban):
            continue
        desc = struct.unpack_from("<11I", ban, desc_off)
        out.append(dict(index=i, dat=dat_off, desc_offset=desc_off, line=desc[0] & 0xFFFF,
                        desc=desc))
    return out


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        print(__doc__)
        return
    ban = open(a[0], "rb").read()
    dat = open(a[1], "rb")
    ls = lines(ban)
    if "--wav" in a:
        i = a.index("--wav")
        outdir = a[i + 1]
        first = int(a[i + 2]) if len(a) > i + 2 else 0
        count = int(a[i + 3]) if len(a) > i + 3 else len(ls)
        os.makedirs(outdir, exist_ok=True)
        done = 0
        for l in ls[first:first + count]:
            dat.seek(l["dat"])
            head = dat.read(48)
            if head[:4] != b"VAGp":
                print("line %d: no VAG at 0x%x" % (l["index"], l["dat"]))
                continue
            size, rate = struct.unpack_from(">II", head, 12)
            pcm = hp2_audio.decode(dat.read(size), 1)
            name = os.path.join(outdir, "%04d_line%04x.wav" % (l["index"], l["line"]))
            with wave.open(name, "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(rate)
                w.writeframes(pcm.tobytes())
            done += 1
        print("wrote %d lines to %s" % (done, outdir))
        return
    dat.seek(0, 2)
    size = dat.tell()
    ok = 0
    for l in ls:
        dat.seek(l["dat"])
        ok += dat.read(4) == b"VAGp"
    print("%d lines; %d start with a VAG header in %s (%d bytes)" % (len(ls), ok, a[1], size))
    for l in ls[:20]:
        print("  %4d  line 0x%04x  audio at 0x%08x  desc %s"
              % (l["index"], l["line"], l["dat"], " ".join("%08x" % x for x in l["desc"][1:6])))


if __name__ == "__main__":
    main()
