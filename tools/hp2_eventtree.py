#!/usr/bin/env python3
"""
The two career event lists of FRONTEND\\FRONTB.BUN: Championship (World
Racing) and Hot Pursuit, each a 0x80034A00 chunk. Prints every event's set-up:
race type, cars you may pick, tracks with laps, opponents, reward and what it
takes to unlock. See docs/formats.md, "Event trees".

    hp2_eventtree.py FRONTB.BUN [--lang FE_ENGLISH.BIN] [--json OUT.json]
    hp2_eventtree.py FRONTB.BUN --carinfo GLOBALB.BUN     names cars from the car database

FRONTB.BUN is archive entry 21 (021_14c57a19.bin); FE_ENGLISH.BIN entry 3;
GLOBALB.BUN entry 302. Without --lang titles and track names are left out.

Layout (all little-endian; offsets inside each child chunk's data):
    0x00000001  64 bytes: the tree's name, "Championship" or "Hot Pursuit"
    0x00034A01  4 bytes each: u16 0xFFFF, u16 car (car database index, 0..63;
                0x4000 is set on Championship event 31's car)    = cars to pick
    0x00034A02  4 bytes each, read as u32 v                      = one race
                  v & 0xFF        track number (11..66, as the track list)
                  v >> 9 & 1      "II": the course run the other way
                  v >> 14 & 0xF   laps (the game's $NUM_LAPS reads this)
                  v >> 18 & 0xFF  unknown a (0 or 25..100)
                  v >> 26         unknown b (25 most, 30 or 40 on some)
                  bits 12, 13 always set; the others 0
    0x00034A03  8 bytes each: u16 kind, u16 event, u32 value     = a condition
                  kind 1: finish event in place <= value
                  kind 2: finish event within value ticks (4000 a second)
    0x00034A04  8 bytes each: u8 kind, u8 id, u16 flag, u32 value = a reward
                  kind 0: car id (car database index); flag 1 (9 once)
                  kind 1: track id (track number), flag 1 = its "II"
                  value: the condition (as 34A03's value; 1 = win)
    0x00034A05  188-byte records, one per event:
                  +0x28 event number, +0x29 race type (RACE_TYPES),
                  +0x2A tier (5 first events .. 1 last), +0x2C cop level
                  (Hot Pursuit only, 0..4); counts at +0x3B (34A01),
                  +0x3C (34A02), +0x3D (34A03), +0x3E (34A04), +0x3F (34A06)
                  and s16 index lists at +0x44, +0x6C, +0x7C, +0x8C, +0x9C;
                  +0x42 one more 34A03 index: the event's own target
                  ($TARGET_TIME when its kind is 2). Other bytes in
                  +0x2B..+0x3A are copied into the race set-up, meaning not
                  known; unused list slots hold leftover bytes.
    0x00034A06  4 bytes each: u8 car, u8 colour (255 = any), u8 skill
                (0..9), u8 0                                     = an opponent
Laps on point-to-point courses read 1, except 2 on Hot Pursuit's first three
Deliveries. Championship has a 32nd record the game does not list: a
one-on-one 360 Spider race (cop level 6) after event 31, with no title.
The 34A03 conditions an event lists are the ones that unlock it: each names
an earlier event and the place or time needed there.
"""
import json
import os
import struct
import sys

RACE_TYPES = ["Single Race", "Time Trial", "Lap Knockout", "Race Knockout", "Tournament", "Delivery", "Pursuit"]
CHUNK_TREE = 0x80034A00
TICKS = 4000


def chunks(d, o, end):
    while o + 8 <= end:
        cid, sz = struct.unpack_from("<II", d, o)
        if sz > end - o - 8:
            return
        yield cid, o + 8, sz
        o += 8 + sz


def find_trees(d):
    trees = []
    o = d.find(struct.pack("<I", CHUNK_TREE))
    while o != -1:
        if o % 4 == 0 and o + 8 <= len(d):
            sz = struct.unpack_from("<I", d, o + 4)[0]
            if 8 <= sz <= len(d) - o - 8:
                ch = {cid: (p, s) for cid, p, s in chunks(d, o + 8, o + 8 + sz)}
                if 1 in ch and 0x34A05 in ch:
                    trees.append(ch)
        o = d.find(struct.pack("<I", CHUNK_TREE), o + 4)
    return trees


def fmt_time(ticks):
    s = ticks / TICKS
    return "%d:%05.2f" % (int(s // 60), s - 60 * int(s // 60))


def parse_tree(d, ch):
    def items(cid, w):
        p, s = ch.get(cid, (0, 0))
        return [d[p + w * i:p + w * i + w] for i in range(s // w)]
    name = d[ch[1][0]:ch[1][0] + 64].split(b"\0")[0].decode("latin1")
    a01, a02, a03, a04, a06 = items(0x34A01, 4), items(0x34A02, 4), items(0x34A03, 8), items(0x34A04, 8), items(0x34A06, 4)
    p5, s5 = ch[0x34A05]
    events = {}
    for r in range(s5 // 188):
        R = d[p5 + 188 * r:p5 + 188 * r + 188]
        n = R[0x28]
        if n == 0 or n in events:
            continue

        def idx(count_at, list_at, table):
            c = R[count_at]
            if c >= 0x80:
                c = 0
            out = []
            for j in range(c):
                i = struct.unpack_from("<h", R, list_at + 2 * j)[0]
                if 0 <= i < len(table):
                    out.append(table[i])
            return out

        def cond(b):
            kind, ev, value = struct.unpack("<HHI", b)
            c = dict(kind=kind, event=ev, value=value)
            if kind == 1:
                c["place"] = value
            elif kind == 2:
                c["time"] = fmt_time(value)
            return c
        races = []
        for b in idx(0x3C, 0x6C, a02):
            v = struct.unpack("<I", b)[0]
            races.append(dict(track=v & 0xFF, ii=v >> 9 & 1, laps=v >> 14 & 0xF, a=v >> 18 & 0xFF, b=v >> 26))
        rewards = []
        for b in idx(0x3E, 0x8C, a04):
            kind, rid, ii, value = struct.unpack("<BBHI", b)
            rw = dict(kind="car" if kind == 0 else "track" if kind == 1 else kind, id=rid, value=value, flag=ii)
            if kind == 1:
                rw["ii"] = ii
            rewards.append(rw)
        own = struct.unpack_from("<h", R, 0x42)[0]
        rt = R[0x29]
        events[n] = dict(
            number=n,
            race_type=RACE_TYPES[rt] if rt < len(RACE_TYPES) else rt,
            tier=R[0x2A], cops=R[0x2C],
            cars=[struct.unpack("<HH", b)[1] for b in idx(0x3B, 0x44, a01)],
            races=races,
            opponents=[dict(car=b[0], colour=None if b[1] == 255 else b[1], skill=b[2]) for b in idx(0x3F, 0x9C, a06)],
            rewards=rewards,
            unlocked_by=[cond(b) for b in idx(0x3D, 0x7C, a03)],
            target=cond(a03[own]) if 0 <= own < len(a03) else None,
            raw=R[0x2B:0x3B].hex(" "),
        )
    return dict(name=name, events=[events[k] for k in sorted(events)])


def load_lang(path):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import hp2_lang
    return {s["id"]: s["text"] for s in hp2_lang.parse(open(path, "rb").read())["strings"]}


def load_cars(path):
    """slot -> car database name (ELISE, BMWM5COP...)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import hp2_carinfo
    return {r["slot"]: r["name"] for r in hp2_carinfo.records(open(path, "rb").read())}


LIGHTING = {1: "overcast", 2: "overcast", 3: "sunset", 4: "sunset", 6: "sunset"}   # by location (first digit)


def track_name(lang, num, ii):
    """The course's name (string 50000 + 10 x course), "II" when the race runs
    it as its II version (51000 + 10 x course), and the other lighting for
    track numbers ending in 4..6."""
    base = num - 3 if num % 10 > 3 else num
    s = lang.get((51000 if ii else 50000) + 10 * base) if lang else None
    s = s or ("Track %d%s" % (base, " II" if ii else ""))
    if num % 10 > 3:
        s += " (%s)" % LIGHTING.get(num // 10, "other lighting")
    return s


def main():
    a = sys.argv[1:]
    if not a or a[0].startswith("-"):
        print(__doc__)
        return
    opt = {}
    i = 1
    while i < len(a):
        if a[i] in ("--lang", "--json", "--carinfo") and i + 1 < len(a):
            opt[a[i]] = a[i + 1]
            i += 2
        else:
            raise SystemExit("unknown option %s" % a[i])
    d = open(a[0], "rb").read()
    trees = [parse_tree(d, ch) for ch in find_trees(d)]
    if not trees:
        raise SystemExit("no 0x80034A00 event trees in %s" % a[0])
    lang = load_lang(opt["--lang"]) if "--lang" in opt else {}
    cars = load_cars(opt["--carinfo"]) if "--carinfo" in opt else {}
    # titles: string base + event - 1, for the events the game lists (World
    # Racing's record 32, a hidden 360 Spider duel, has no title of its own)
    title_base = {"Championship": (70000, 31), "Hot Pursuit": (70031, 32)}
    for t in trees:
        base, listed = title_base.get(t["name"], (None, 0))
        for e in t["events"]:
            if lang and base is not None and e["number"] <= listed:
                e["title"] = lang.get(base + e["number"] - 1)
    if "--json" in opt:
        out = opt["--json"]
        if os.path.exists(out):
            raise SystemExit("%s exists; not overwriting" % out)
        json.dump(trees, open(out, "w"), indent=1)
        print("wrote", out)
        return
    car = lambda c: cars.get(c & 0x3FFF, "car %d" % (c & 0x3FFF))
    for t in trees:
        print("== %s: %d events" % (t["name"], len(t["events"])))
        for e in t["events"]:
            print("%2d  %-13s %s" % (e["number"], e["race_type"], e.get("title") or ""))
            print("      cars: %s" % ", ".join(car(c) for c in e["cars"]))
            for r in e["races"]:
                print("      race: %s, %d lap%s" % (track_name(lang, r["track"], r["ii"]), r["laps"], "" if r["laps"] == 1 else "s"))
            if e["opponents"]:
                print("      opponents (%d): %s" % (len(e["opponents"]), ", ".join(
                    "%s skill %d" % (car(o["car"]), o["skill"]) for o in e["opponents"])))
            if e["cops"]:
                print("      cop level %d" % e["cops"])
            if e["target"] and e["target"]["kind"] == 2:
                print("      target time %s" % e["target"]["time"])
            for rw in e["rewards"]:
                what = car(rw["id"]) if rw["kind"] == "car" else track_name(lang, rw["id"], rw.get("ii", 0))
                print("      reward: %s (%s)" % (what, ("time " + fmt_time(rw["value"])) if rw["value"] > 100 else "place %d" % rw["value"]))
            for c in e["unlocked_by"]:
                print("      unlocked by event %d: %s" % (c["event"], ("place %d or better" % c["place"]) if c["kind"] == 1 else "within " + c.get("time", "?")))


if __name__ == "__main__":
    main()
