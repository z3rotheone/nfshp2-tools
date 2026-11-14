"""
Parse Need for Speed: Hot Pursuit 2 (PS2) GEOMETRY.BIN car models.

Structure (same u32 id + u32 size chunk convention as the texture packs and
sound banks, a container being an id with the top bit set):

    0x00034013  file header: 6 x 32-byte attachment records
    0x80034000  model (container)
        0x00034001  the development path the file was built from
        0x80034002  one part (container), repeated
            0x00034003  part header: name, name hash, bounding box
            0x00034006  texture hashes used by this part (u32 hash, u32 pad)
            0x0003401d  more texture hashes, generic ones such as MIRROR
            0x00034004  sub-mesh records, each with a bounding box
            0x00034005  the geometry itself, as a VIF display list

Parts are named <CAR>_A, _B, _C and <CAR>_TIRE_FRONT_A. The letter is a
variant suffix; the part header's hash covers the full name including it, so
`4DR_SEDAN02_TIRE_FRONT_` in the name field actually hashes as
`4DR_SEDAN02_TIRE_FRONT_A`. Hashes are the §9 name hash (see hp2_hash.py, and docs/formats.md section 10).

The file header's 6 records are wheel and attachment points: each holds a
position, the hash of the part to attach, and the hash of the car's own base
name. Their Y coordinates come in +/- pairs, which is the left/right symmetry.

The geometry chunk is a PS2 VIF display list: UNPACK commands push vertex
positions (V4_32 floats), texture coordinates (V2_32) and colours (V4_8 or
V4_5551) into VU1 memory, then MSCAL runs the microprogram that builds the
strips. Turning that into triangles needs the VU1 program's own layout, which
is not decoded here -- this tool reports the packets and can dump the raw
float blocks, which is enough to see the mesh.

Usage:
    python hp2_geom.py CAR.BIN              summary of parts and textures
    python hp2_geom.py CAR.BIN --vif        walk the VIF display lists
    python hp2_geom.py CAR.BIN --dump-verts write part_N.xyz point clouds
"""
import os
import struct
import sys

HDR, MODEL, DEVPATH, PART = 0x00034013, 0x80034000, 0x00034001, 0x80034002
PARTHDR, TEX1, TEX2, SUBMESH, GEOM = 0x00034003, 0x00034006, 0x0003401d, 0x00034004, 0x00034005

VIFCMD = {0x00: "NOP", 0x01: "STCYCL", 0x02: "OFFSET", 0x03: "BASE", 0x04: "ITOP",
          0x05: "STMOD", 0x06: "MSKPATH3", 0x07: "MARK", 0x10: "FLUSHE", 0x11: "FLUSH",
          0x13: "FLUSHA", 0x14: "MSCAL", 0x15: "MSCALF", 0x17: "MSCNT",
          0x20: "STMASK", 0x30: "STROW", 0x31: "STCOL", 0x4A: "MPG",
          0x50: "DIRECT", 0x51: "DIRECTHL"}


def name_hash(s):
    h = 0xFFFFFFFF
    for b in s.encode("ascii", "replace"):
        h = (h * 33 + b) & 0xFFFFFFFF
    return h


def chunks(d, start, end):
    p = start
    while p + 8 <= end:
        cid, sz = struct.unpack_from("<II", d, p)
        if p + 8 + sz > end:
            return
        yield cid, p + 8, sz
        p += 8 + sz


def unpack_size(cmd, num):
    """Bytes of payload after an UNPACK tag, rounded up to the 4-byte
    alignment VIF requires -- without the rounding the walk desyncs on any
    odd-sized block, such as a V4_5551 run of 29."""
    vn = (cmd >> 2) & 3
    vl = cmd & 3
    if vl == 3:                 # V4-5551: one 16-bit word per element
        n = num * 2
    else:
        n = num * (vn + 1) * {0: 4, 1: 2, 2: 1}[vl]
    return (n + 3) & ~3


def unpack_label(cmd):
    vn = (cmd >> 2) & 3
    vl = cmd & 3
    comp = ["S", "V2", "V3", "V4"][vn]
    width = ["32", "16", "8", "5551"][vl]
    return "UNPACK %s_%s%s" % (comp, width, " (masked)" if cmd & 0x10 else "")


def walk_vif(d, start, end, report=None):
    """Yield (offset, cmd, num, imm, payload offset, payload size)."""
    p = start
    while p + 4 <= end:
        w = struct.unpack_from("<I", d, p)[0]
        cmd = (w >> 24) & 0xFF
        num = (w >> 16) & 0xFF
        imm = w & 0xFFFF
        if 0x60 <= cmd <= 0x7F:
            n = unpack_size(cmd, num if num else 256)
            yield p, cmd, num, imm, p + 4, n
            p += 4 + n
        else:
            yield p, cmd, num, imm, p + 4, 0
            p += 4


def parse(path):
    d = open(path, "rb").read()
    out = {"path": path, "size": len(d), "attach": [], "devpath": "", "parts": []}
    for cid, off, sz in chunks(d, 0, len(d)):
        if cid == HDR:
            for i in range((sz - 8) // 32):
                o = off + 8 + i * 32
                a, b = struct.unpack_from("<II", d, o)
                h1, h2 = struct.unpack_from("<II", d, o + 8)
                x, y, z = struct.unpack_from("<fff", d, o + 16)
                fl = struct.unpack_from("<I", d, o + 28)[0]
                out["attach"].append(dict(a=a, b=b, part=h1, car=h2,
                                          pos=(x, y, z), flags=fl))
        elif cid == MODEL:
            for c2, o2, s2 in chunks(d, off, off + sz):
                if c2 == DEVPATH:
                    out["devpath"] = d[o2:o2 + s2].split(b"\0")[0].decode("ascii", "replace")
                elif c2 == PART:
                    part = {"tex": [], "tex2": [], "geom": None, "submesh": None, "name": ""}
                    for c3, o3, s3 in chunks(d, o2, o2 + s2):
                        if c3 == PARTHDR:
                            blob = d[o3:o3 + s3]
                            i = blob.find(b"\x0b\x00\x00\x00")
                            if i < 0:
                                i = 12
                            part["hash"] = struct.unpack_from("<I", blob, max(0, i - 4))[0]
                            nm = blob[i + 4:]
                            part["name"] = nm.split(b"\0")[0].decode("ascii", "replace")
                        elif c3 in (TEX1, TEX2):
                            key = "tex" if c3 == TEX1 else "tex2"
                            base = o3 + (8 if c3 == TEX2 else 0)
                            for k in range((o3 + s3 - base) // 8):
                                h = struct.unpack_from("<I", d, base + k * 8)[0]
                                if h:
                                    part[key].append(h)
                        elif c3 == SUBMESH:
                            part["submesh"] = (o3, s3)
                        elif c3 == GEOM:
                            part["geom"] = (o3 + 8, s3 - 8)
                    out["parts"].append(part)
    return d, out


KNOWN = {0x2787edab: "INTERIOR", 0x6bfa56da: "MIRROR"}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return
    show_vif = "--vif" in sys.argv
    dump = "--dump-verts" in sys.argv

    for path in args:
        d, m = parse(path)
        print("%s  (%s bytes)" % (os.path.basename(path), "{:,}".format(m["size"])))
        if m["devpath"]:
            print("  built from: %s" % m["devpath"])
        car = os.path.basename(os.path.dirname(m["devpath"].replace("\\", "/"))) or "?"
        print("  car: %s   (hash %08x)" % (car, name_hash(car)))

        print("\n  attachment points (%d):" % len(m["attach"]))
        for i, a in enumerate(m["attach"]):
            print("    [%d] part %08x  at (%7.3f,%7.3f,%7.3f)  flags %08x"
                  % (i, a["part"], a["pos"][0], a["pos"][1], a["pos"][2], a["flags"]))

        print("\n  parts (%d):" % len(m["parts"]))
        for p in m["parts"]:
            gs = p["geom"][1] if p["geom"] else 0
            print("    %-28s hash %08x  geometry %s bytes  %d+%d textures"
                  % (p["name"] or "(unnamed)", p.get("hash", 0), "{:,}".format(gs),
                     len(p["tex"]), len(p["tex2"])))
            for h in p["tex"] + p["tex2"]:
                known = KNOWN.get(h)
                guess = ""
                for suf in ("_SKIN1", "_SKIN2", "_SKIN3", "_TIRE", "_GLASS"):
                    if name_hash(car + suf) == h:
                        guess = car + suf
                print("        texture %08x%s" % (h, "  = " + (known or guess) if (known or guess) else ""))

        if show_vif:
            for p in m["parts"]:
                if not p["geom"]:
                    continue
                o, n = p["geom"]
                print("\n  VIF display list for %s (%d bytes):" % (p["name"], n))
                counts = {}
                shown = 0
                for off, cmd, num, imm, po, pn in walk_vif(d, o, o + n):
                    lbl = VIFCMD.get(cmd) or (unpack_label(cmd) if 0x60 <= cmd <= 0x7F
                                              else "unknown %02x" % cmd)
                    counts[lbl.split()[0]] = counts.get(lbl.split()[0], 0) + 1
                    if shown < 12:
                        print("      %08x  %-18s num=%-4d imm=%#06x  payload %d" %
                              (off, lbl, num, imm, pn))
                        shown += 1
                print("      ... %s" % ", ".join("%s x%d" % (k, v) for k, v in sorted(counts.items())))

        if dump:
            stem = os.path.splitext(os.path.basename(path))[0]
            for idx, p in enumerate(m["parts"]):
                if not p["geom"]:
                    continue
                o, n = p["geom"]
                pts = []
                for off, cmd, num, imm, po, pn in walk_vif(d, o, o + n):
                    # Positions arrive as V4_32 at VU address 2, transposed
                    # for the VU1's SIMD registers: each group of three quads
                    # holds the X, then Y, then Z of the same four vertices.
                    # num is a multiple of 3, giving num/3*4 vertices.
                    if not (0x60 <= cmd <= 0x7F):
                        continue
                    if (cmd & 3) != 0 or ((cmd >> 2) & 3) != 3:
                        continue
                    if (imm & 0x3FF) != 2 or num < 3 or num % 3:
                        continue
                    for grp in range(num // 3):
                        X = struct.unpack_from("<ffff", d, po + (grp * 3 + 0) * 16)
                        Y = struct.unpack_from("<ffff", d, po + (grp * 3 + 1) * 16)
                        Z = struct.unpack_from("<ffff", d, po + (grp * 3 + 2) * 16)
                        for i in range(4):
                            pts.append((X[i], Y[i], Z[i]))
                fn = "%s_part%d_%s.xyz" % (stem, idx, (p["name"] or "x").replace("\\", "_"))
                with open(fn, "w") as f:
                    for x, y, z in pts:
                        f.write("%.6f %.6f %.6f\n" % (x, y, z))
                print("  wrote %s  (%d points)" % (fn, len(pts)))
        print()


if __name__ == "__main__":
    main()
