#!/usr/bin/env python3
"""Compares hp2_fe (C) with tools/hp2_fescreens.py on a real FRONTB.BUN.

    python3 tests/check_fe.py ./test_fe FRONTB.BUN

Runs `test_fe --dump`, builds the same lines from hp2_fescreens.parse and
reports every screen or widget that differs. Exit status 0 = identical.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import hp2_fescreens  # noqa: E402


def g(v):
    return "%.9g" % v


def expected(path):
    out = []
    for s in hp2_fescreens.parse(open(path, "rb").read()):
        out.append("S %08x %dx%d %d" % (s["id"], s["canvas"][0], s["canvas"][1], len(s["widgets"])))
        for w in s["widgets"]:
            head = "W %s %s %08x %s %s %s %s" % (w["kind"], w["name"][:24], w["hash"], g(w["x"]), g(w["y"]),
                                                g(w["scale"][0]), g(w["scale"][1]))
            if w["kind"] == "text":
                out.append("%s %08x %u %s %u |%s|" % (head, w["font"], w["capacity"], w["color"],
                                                      w["string_id"], w["default"][:63]))
            else:
                out.append("%s %08x" % (head, w["texture"]))
    return out


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    exe, path = sys.argv[1:]
    got = subprocess.run([exe, "--dump", path], check=True, capture_output=True,
                         encoding="latin1").stdout.splitlines()
    want = expected(path)
    bad = 0
    for i, (a, b) in enumerate(zip(got, want)):
        if a != b:
            bad += 1
            if bad <= 10:
                print("line %d\n  C:      %s\n  Python: %s" % (i + 1, a, b))
    if len(got) != len(want):
        bad += 1
        print("C wrote %d lines, Python %d" % (len(got), len(want)))
    screens = sum(1 for x in want if x.startswith("S "))
    print("%d screens, %d widgets: %s" % (screens, len(want) - screens,
                                          "identical" if not bad else "%d differences" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
