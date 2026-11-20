#!/usr/bin/env python3
"""Compares hp2_vag (C) with tools/hp2_audio.py on the real music bank.

    python3 tests/check_vag.py ./test_assets BANK [FRAMES]

Decodes the first FRAMES stereo frames (default 100,000: about 3 s) of
songs 0, 14 and 22 of the bank (entry 322) both ways and compares a hash of
the samples. Exit status 0 = identical.
"""
import os
import struct
import subprocess
import sys
from array import array

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import hp2_audio  # noqa: E402

CHUNK = 32768


def expected(path, k, frames):
    f = open(path, "rb")
    head = f.read(0x60 + 0x60 * 64)
    off = struct.unpack_from("<I", head, 0x60 + 0x60 * k)[0]
    f.seek(off)
    vag = f.read(48)
    size = struct.unpack_from(">I", vag, 12)[0]
    need = min(size, ((frames * 16 // 28) // CHUNK + 2) * 2 * CHUNK)
    adpcm = f.read(need)
    la, ra = hp2_audio.deinterleave(adpcm, CHUNK)
    left = hp2_audio.decode_channel(la, 0, 16)
    right = hp2_audio.decode_channel(ra, 0, 16)
    n = min(len(left), len(right), frames)
    both = array("h", bytes(4 * n))
    both[0::2] = left[:n]
    both[1::2] = right[:n]
    if sys.byteorder != "little":
        both.byteswap()
    h = 1469598103934665603
    for x in both.tobytes():
        h = ((h ^ x) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "%d %016x" % (n, h)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    exe, bank = sys.argv[1], sys.argv[2]
    frames = int(sys.argv[3]) if len(sys.argv) > 3 else 100000
    bad = 0
    for k in (0, 14, 22):
        got = subprocess.run([exe, "--vagdump", bank, str(k), str(frames)], check=True,
                             capture_output=True, encoding="ascii").stdout.strip()
        want = expected(bank, k, frames)
        same = got == want
        bad += not same
        print("song %2d: C %s, Python %s %s" % (k, got, want, "identical" if same else "DIFFERENT"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
