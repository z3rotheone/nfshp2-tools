#!/usr/bin/env python3
"""hp2_ragnar.py - the recorded drives in chunk 0x34121 of a track file.

Usage: hp2_ragnar.py TRACKBnn.bin [--csv OUT.csv] [--check]

Chunk 0x34121 (a top-level chunk of a decompressed TRACKBnn file; 412,048
bytes in TRACKB24) is a list of recordings of a driver ("RAGNAR") driving the
track. Each recording is:

    40 bytes   header: u32 version-ish (0x036d0208 ..), u32 junk, u32 id
               (n << 16 | 1, n odd: 1, 3, 5 ..), u32 0, u32 N, u32 N,
               u32 1, three zero words
    1024 bytes entries: up to 20 x 48 bytes (u32 1 = a time, 2 = empty slot;
               f32 seconds, or 999.0 when empty; 40-byte name "RAGNAR" padded
               with spaces), then zeros
    N x 112    samples: f32 x, y, z (world units, the collision's),
               f32 heading x, y (unit vector), f32 step (about the distance
               to the next sample: 5 to 13), f32 (5.0 .. 6.0 in steps of
               0.5), f32 (smooth, 5 .. 9), f32 (about -0.13 .. 0.09), f32
               (about -0.1 .. 0.09), then words that look like uninitialised
               memory (garbage floats, 3.016 at +0x5C)

The recordings follow each other with nothing between them and end exactly
at the end of the chunk (--check). TRACKB24: 13 recordings, 11 short (21 to
104 samples, 240 to 1160 m) and two long (404 and 435 samples, 4.2 and 4.4
km of the track's 5.19 km lap, both starting at the start line).

What the samples are for is not known: the step is close to the distance
between samples, so they are probably taken at a fixed time interval and the
fields after the heading are a speed-like and a smoothed value, but the
units are not worked out.
"""
import csv
import math
import struct
import sys

HDR = 40
BLOCK = 1024
SAMPLE = 112


def chunks(d, start, end):
    p = start
    while p + 8 <= end:
        cid, sz = struct.unpack_from("<II", d, p)
        if p + 8 + sz > end:
            return
        yield cid, p + 8, sz
        p += 8 + sz


def find_chunk(d, want, start=0, end=None):
    end = len(d) if end is None else end
    for cid, a, sz in chunks(d, start, end):
        if cid == want:
            return a, sz
        if cid & 0x80000000:
            r = find_chunk(d, want, a, a + sz)
            if r:
                return r
    return None


def parse(d, a, sz):
    """Returns (recordings, leftover bytes). A recording is a dict."""
    out = []
    p = a
    end = a + sz
    while p + HDR + BLOCK <= end:
        w = struct.unpack_from("<10I", d, p)
        n = w[4]
        if w[5] != n or p + HDR + BLOCK + n * SAMPLE > end:
            break
        entries = []
        for k in range(20):
            q = p + HDR + 48 * k
            flag, t = struct.unpack_from("<If", d, q)
            name = d[q + 8:q + 48].rstrip(b"\0 ").decode("latin-1")
            if flag in (1, 2):
                entries.append((flag, t, name))
        s0 = p + HDR + BLOCK
        pts = [struct.unpack_from("<9f", d, s0 + SAMPLE * k) for k in range(n)]
        out.append({"offset": p - a, "id": w[2], "n": n, "entries": entries, "samples": pts})
        p = s0 + n * SAMPLE
    return out, end - p


def length(pts):
    return sum(math.dist(pts[k][:3], pts[k + 1][:3]) for k in range(len(pts) - 1))


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    d = open(args[0], "rb").read()
    r = find_chunk(d, 0x34121)
    if not r:
        print("%s: no chunk 0x34121" % args[0])
        return 1
    recs, left = parse(d, *r)
    print("%s: chunk 0x34121, %d bytes, %d recordings, %d bytes left over" % (args[0], r[1], len(recs), left))
    for i, rc in enumerate(recs):
        s = rc["samples"]
        times = [e[1] for e in rc["entries"] if e[0] == 1]
        print("%2d  id %#x  %4d samples  %6.0f m  start (%.0f, %.0f)  end (%.0f, %.0f)  %d times" % (
            i, rc["id"], rc["n"], length(s), s[0][0], s[0][1], s[-1][0], s[-1][1], len(times)))
    if "--csv" in args:
        out = args[args.index("--csv") + 1]
        with open(out, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["recording", "sample", "x", "y", "z", "hx", "hy", "f5", "f6", "f7"])
            for i, rc in enumerate(recs):
                for k, s in enumerate(rc["samples"]):
                    w.writerow([i, k] + ["%.4f" % v for v in s[:8]] + ["%.4f" % s[8]])
        print("wrote", out)
    if "--check" in args:
        ok = left == 0 and recs and all(abs(math.hypot(s[3], s[4]) - 1) < 0.05 for rc in recs for s in rc["samples"])
        print("check: %s" % ("ok (ends at the chunk end, headings are unit vectors)" if ok else "FAILED"))
        return 0 if ok else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
