#!/usr/bin/env python3
"""
The car database: chunk 0x00034600 in GLOBAL\\GLOBALB.BUN (archive entry 302).

The executable loads it in carinfo.cpp (0x187f98): 64 records of 0x560 bytes,
starting at the first 16-byte boundary after the chunk header. Fields read so
far (offsets in a record; f32 unless noted):

    0x000  char[32]  record name: 911TURBO, 911TURBOHP (the Hot Pursuit
                     version), BMWM5COP, TAXI...
    0x020  char[32]  model name: the GEOMETRY.BIN and the texture prefix. The
                     HP record shares it (911TURBOHP -> 911TURBO)
    0x040  char[32]  model path, .BIN    0x060  char[32]  the same, .LZC
    0x0C0  char[32]  make: PORSCHE, FERRARI...
    0x120  4 wheels of 32 bytes, front left, front right, rear right, rear
           left: x, y, z of the hub in the model's axes (+x front, +y left,
           +z up), radius, tyre width, u32 index 0-3, toe/camber (+-0.01),
           4 bytes 0
    0x1E4  length, width, height (traffic cars hold a 5 x 2 x 1.2 default)
    0x278  final drive
    0x288  u32 number of forward gears
    0x290  8 ratios: reverse, neutral (0), then forward gears
    0x2B0  idle rpm, a peak rpm, redline rpm
    0x320  top speed (mph?)
    0x330  char[32]  engine sound: "NewE" + a name; the bank is
           SOUND\\NEWE<NAME>.MSH (header) + .MSB (samples)

The texture code (0x188790) builds the car's texture names from the model
name, except the tyre and wheel blur, which it builds from the record name
(so 911TURBOHP draws 911TURBOHP_TIRE).

Checked against real cars: the 911 Turbo's gears (3.82, 2.05, 1.41, 1.12,
0.92, 0.75), final drive 3.44, tyre widths 225/295 mm and size 4.43 x 1.83 x
1.30 m are the 996 Turbo's. --check compares sizes and wheels with the models.

Usage:
    hp2_carinfo.py GLOBALB.BUN                 list the cars
    hp2_carinfo.py GLOBALB.BUN --check DIR     compare with DIR's car models
    hp2_carinfo.py GLOBALB.BUN --json OUT      write every record's fields
"""
import json
import os
import struct
import sys

CHUNK = 0x00034600
RECORD = 0x560
WHEELS = ("front_left", "front_right", "rear_right", "rear_left")


def find_table(d):
    o = 0
    while o + 8 <= len(d):
        cid, sz = struct.unpack_from("<II", d, o)
        if cid == CHUNK:
            b = (o + 8 + 15) & ~15
            return b, (o + 8 + sz - b) // RECORD
        if sz > len(d) - o - 8:
            break
        o += 8 + sz
    # not at the top level: search for it
    i = d.find(struct.pack("<I", CHUNK))
    while i != -1:
        if i % 4 == 0:
            sz = struct.unpack_from("<I", d, i + 4)[0]
            if RECORD * 8 < sz < len(d) - i:
                b = (i + 8 + 15) & ~15
                return b, (i + 8 + sz - b) // RECORD
        i = d.find(struct.pack("<I", CHUNK), i + 4)
    raise SystemExit("no 0x00034600 chunk")


def text(d, o, n=32):
    return d[o:o + n].split(b"\0")[0].decode("latin1")


def records(d):
    b, n = find_table(d)
    out = []
    for i in range(n):
        r = b + RECORD * i
        name = text(d, r)
        if not name:
            continue
        wheels = []
        for k in range(4):
            x, y, z, radius, width = struct.unpack_from("<5f", d, r + 0x120 + 32 * k)
            idx = struct.unpack_from("<I", d, r + 0x134 + 32 * k)[0]
            toe = struct.unpack_from("<f", d, r + 0x138 + 32 * k)[0]
            wheels.append(dict(wheel=WHEELS[k], index=idx, x=x, y=y, z=z, radius=radius,
                               width=width, toe=toe))
        ng = struct.unpack_from("<I", d, r + 0x288)[0]
        ratios = struct.unpack_from("<8f", d, r + 0x290)
        sound = text(d, r + 0x330)
        out.append(dict(
            slot=i, name=name, model=text(d, r + 0x20), path=text(d, r + 0x40),
            path_lzc=text(d, r + 0x60), make=text(d, r + 0xC0), wheels=wheels,
            size=dict(zip(("length", "width", "height"), struct.unpack_from("<3f", d, r + 0x1E4))),
            final_drive=struct.unpack_from("<f", d, r + 0x278)[0], gears=ng,
            reverse=ratios[0], ratios=list(ratios[2:2 + ng]),
            rpm=dict(zip(("idle", "peak", "redline"), struct.unpack_from("<3f", d, r + 0x2B0))),
            top_speed=struct.unpack_from("<f", d, r + 0x320)[0],
            engine_sound=sound,
            engine_bank=("SOUND\\%s.MSH" % sound.upper()) if sound else ""))
    return out


def check(cars, extracted):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import csv
    import glob
    import hp2_geom as g
    names = {}
    here = os.path.dirname(os.path.abspath(__file__))
    for row in csv.DictReader(open(os.path.join(here, "archive_names.csv"))):
        names[row["name"]] = int(row["entry"])
    ok_size = ok_wheels = n = banks = with_sound = 0
    print("%-12s %-20s %-20s %s" % ("car", "stored L x W x H", "model L x W x H", "notes"))
    for c in cars:
        if c["engine_sound"]:
            with_sound += 1
            banks += c["engine_bank"] in names and c["engine_bank"][:-1] + "B" in names
        if c["name"].endswith("HP"):
            continue
        e = names.get(c["path"])
        files = glob.glob(os.path.join(extracted, "%03d_*" % e)) if e else []
        if not files:
            print("%-12s no model file for %s" % (c["name"], c["path"]))
            continue
        d, info = g.parse(files[0])
        pts = []
        for p in info["parts"]:
            if p["geom"] and p["name"].endswith("_A") and "TIRE" not in p["name"]:
                for m in g.part_mesh(d, p)[0]:
                    pts += m["pos"]
        lo = [min(q[k] for q in pts) for k in range(3)]
        hi = [max(q[k] for q in pts) for k in range(3)]
        dims = [hi[k] - lo[k] for k in range(3)]
        s = c["size"]
        stored = [s["length"], s["width"], s["height"]]
        default = stored == [5.0, 2.0, 1.2] or (abs(stored[0] - 5) < 1e-4 and abs(stored[1] - 2) < 1e-4)
        close = all(abs(a - b) / b < 0.12 for a, b in zip(stored[:2], dims[:2]))
        inside = all(lo[0] - 0.3 < w["x"] < hi[0] + 0.3 and lo[1] - 0.3 < w["y"] < hi[1] + 0.3
                     for w in c["wheels"])
        clearance = min(w["z"] - w["radius"] for w in c["wheels"]) - lo[2]
        n += 1
        ok_size += close or default
        ok_wheels += inside and -0.5 < clearance < 0
        print("%-12s %5.2f %5.2f %5.2f    %5.2f %5.2f %5.2f    %s%s wheels %s, tyres reach %.2f below the body"
              % (c["name"], *stored, *dims, "default size; " if default else "",
                 "" if close or default else "SIZE DIFFERS; ", "inside" if inside else "OUTSIDE",
                 -clearance))
    print("\n%d models: stored length and width within 12%% (or the traffic default) for %d; "
          "wheels inside the body's footprint with tyres below it for %d" % (n, ok_size, ok_wheels))
    print("engine sound banks: %d of %d records name a bank whose .MSH and .MSB are both in the archive"
          % (banks, with_sound))


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    cars = records(open(a[0], "rb").read())
    if "--json" in a:
        out = a[a.index("--json") + 1]
        json.dump(cars, open(out, "w"), indent=1)
        print("wrote %s: %d cars" % (out, len(cars)))
        return
    if "--check" in a:
        check(cars, a[a.index("--check") + 1])
        return
    for c in cars:
        w = c["wheels"]
        print("%2d %-12s %-11s %-10s wheelbase %.2f track %.2f/%.2f  %d gears %s fd %.2f  "
              "rpm %d/%d/%d  top %.0f  %s"
              % (c["slot"], c["name"], c["model"], c["make"], w[0]["x"] - w[3]["x"],
                 w[0]["y"] - w[1]["y"], w[3]["y"] - w[2]["y"], c["gears"],
                 "/".join("%.2f" % x for x in c["ratios"]), c["final_drive"],
                 c["rpm"]["idle"], c["rpm"]["peak"], c["rpm"]["redline"], c["top_speed"],
                 c["engine_sound"]))


if __name__ == "__main__":
    main()
