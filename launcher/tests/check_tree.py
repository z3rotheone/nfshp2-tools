#!/usr/bin/env python3
"""Compares hp2_tree.c (test_assets --treedump) with tools/hp2_eventtree.py on
a real FRONTB.BUN: every event's race type, tier, cop level, cars, races,
opponents, rewards, unlock conditions and goal.

    python3 tests/check_tree.py ./test_assets FRONTB.BUN
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import hp2_eventtree as T  # noqa: E402


def line(k, e):
    rt = T.RACE_TYPES.index(e["race_type"]) if e["race_type"] in T.RACE_TYPES else e["race_type"]
    rk = {"car": 0, "track": 1}
    s = "%d %d type %d tier %d cops %d cars" % (k, e["number"], rt, e["tier"], e["cops"])
    s += "".join(" %d" % (c & 0x3FFF) for c in e["cars"][:8])
    s += " races" + "".join(" %d/%d/%d" % (r["track"], r["ii"], r["laps"]) for r in e["races"][:8])
    s += " opp" + "".join(" %d/%d/%d" % (o["car"], -1 if o["colour"] is None else o["colour"], o["skill"]) for o in e["opponents"][:8])
    s += " rew" + "".join(" %d/%d/%d/%d" % (rk.get(r["kind"], r["kind"]), r["id"], r["flag"], r["value"])
                          for r in e["rewards"][:4])
    s += " need" + "".join(" %d/%d/%d" % (c["kind"], c["event"], c["value"]) for c in e["unlocked_by"][:8])
    g = e["target"] or dict(kind=0, event=0, value=0)
    s += " goal %d/%d/%d" % (g["kind"], g["event"], g["value"])
    return s


def main():
    exe, frontb = sys.argv[1], sys.argv[2]
    d = open(frontb, "rb").read()
    trees = {t["name"]: t for t in (T.parse_tree(d, ch) for ch in T.find_trees(d))}
    want = [line(k, e) for k, name in enumerate(("Championship", "Hot Pursuit")) for e in trees[name]["events"]]
    got = subprocess.run([exe, "--treedump", frontb], capture_output=True, text=True, check=True).stdout.splitlines()
    bad = [(w, g) for w, g in zip(want, got) if w != g]
    if len(want) != len(got) or bad:
        print("%d / %d events, %d differ" % (len(got), len(want), len(bad)))
        for w, g in bad[:5]:
            print("  py: " + w + "\n  c:  " + g)
        sys.exit(1)
    print("%d events: identical" % len(got))


if __name__ == "__main__":
    main()
