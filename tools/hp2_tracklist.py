#!/usr/bin/env python3
"""
The track list: chunk 0x00034201 in GLOBAL\\GLOBALB.BUN (archive entry 302).

The executable keeps it at gp-0x55BC (0x153608, beside the 0x34200 chunk):
66 records of 0xA4 bytes, one slot per track number 1..66; 30 are used.

    0x00  char[32]  name: "Coastal Parklands T11", "Wine Sunset T34"...
    0x20  char[32]  "track, NFS"
    0x40  u8   1 on a used slot
    0x41  u8   track number: TRACKS\\TRACKB<n>.LZC
    0x42  u8   the track whose scenery, routes and cameras it uses: 14 -> 11.
               Tracks n+3 are the same circuit at another time of day or in
               other weather ("Coastal Overcast", "Wine Sunset"), not reversed
    0x43  u8   1 = point-to-point (the 3rd and 6th of each location)
    0x44  u32  variant: 0 the base track, 1 overcast, 2 sunset/sunrise/dawn,
               4 the sunsets of location 4
    0x50  f32  advertised length (miles?): route length / 1,110-1,745
    0x5C  3 x f32  a point 2,000-3,400 units above the track (preview camera?)
    0x84  char[]  location: Parkland, Desert, Greece, Alpine, Tropics. The same
               names as the ambience sound banks (SOUND\\<LOCATION>.MSH)

The executable has "TN_TRACK%dR", but no code uses it: no reverse tracks.

Usage:  hp2_tracklist.py GLOBALB.BUN [--json OUT]
"""
import json
import struct
import sys

CHUNK, RECORD = 0x00034201, 0xA4


def tracks(d):
    i = d.find(struct.pack("<I", CHUNK))
    while i != -1:
        sz = struct.unpack_from("<I", d, i + 4)[0]
        if i % 4 == 0 and sz % RECORD == 0 and 0 < sz < len(d) - i:
            break
        i = d.find(struct.pack("<I", CHUNK), i + 4)
    else:
        raise SystemExit("no 0x00034201 chunk")
    b = i + 8
    out = []
    for k in range(sz // RECORD):
        r = b + RECORD * k
        if not d[r + 0x40]:
            continue

        def txt(o, n=32):
            return d[r + o:r + o + n].split(b"\0")[0].decode("latin1")
        out.append(dict(number=d[r + 0x41], scenery=d[r + 0x42], point_to_point=bool(d[r + 0x43]),
                        variant=struct.unpack_from("<I", d, r + 0x44)[0], name=txt(0), kind=txt(0x20),
                        length=struct.unpack_from("<f", d, r + 0x50)[0],
                        view=struct.unpack_from("<3f", d, r + 0x5C), location=txt(0x84, 0x20),
                        file="TRACKS\\TRACKB%d.LZC" % d[r + 0x41]))
    return out


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    ts = tracks(open(a[0], "rb").read())
    if "--json" in a:
        out = a[a.index("--json") + 1]
        json.dump(ts, open(out, "w"), indent=1)
        print("wrote %s: %d tracks" % (out, len(ts)))
        return
    for t in ts:
        print("%2d  %-28s %-9s scenery %d  %-14s variant %d  length %.2f"
              % (t["number"], t["name"], t["location"], t["scenery"],
                 "point-to-point" if t["point_to_point"] else "circuit", t["variant"], t["length"]))


if __name__ == "__main__":
    main()
