#!/usr/bin/env python3
"""Compares hp2_tex (C) with tools/hp2_tpk.py on real bundles.

    python3 tests/check_tex.py ./test_assets FRONTB.BUN GLOBALB.BUN ...

Decodes every texture of every pack in the files both ways and compares a
hash of the RGBA. Exit status 0 = identical. Needs numpy and pillow (as
hp2_tpk.py does).
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import numpy as np  # noqa: E402
import hp2_tpk  # noqa: E402


def fnv(b):
    h = 1469598103934665603
    for x in b:
        h = ((h ^ x) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def expected(paths):
    out, seen = [], set()
    for p in paths:
        d = open(p, "rb").read()
        for _off, pk in hp2_tpk.find_embedded(d):
            recs, base = hp2_tpk.parse_pack(pk)
            for r in recs:
                if r["hash"] in seen:
                    continue
                seen.add(r["hash"])
                im = hp2_tpk.decode(pk, base, r)
                h = "none" if isinstance(im, str) else "%016x" % fnv(np.ascontiguousarray(im, dtype=np.uint8).tobytes())
                out.append("%08x %d %d %s %s" % (r["hash"], r["w"], r["h"], h, r["name"]))
    return out


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    exe, paths = sys.argv[1], sys.argv[2:]
    got = subprocess.run([exe, "--texdump"] + paths, check=True, capture_output=True,
                         encoding="latin1").stdout.splitlines()
    want = expected(paths)
    bad = sum(1 for a, b in zip(got, want) if a != b) + abs(len(got) - len(want))
    for a, b in [(a, b) for a, b in zip(got, want) if a != b][:10]:
        print("  C:      %s\n  Python: %s" % (a, b))
    print("%d textures: %s" % (len(want), "identical" if not bad else "%d differences" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
