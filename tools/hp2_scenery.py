#!/usr/bin/env python3
"""
Place track scenery the way the game does: read the 0x80034100 section
chunks of an eLabScenery file and put every model where it belongs.

A scenery file (one per location, e.g. TRACK24\\Pipeline\\Scenery\\
eLabScenery99.bin) holds its models in the same 0x80034000 containers as the
cars (hp2_geom.py decodes them), plus one 0x80034100 chunk per track section.
Each section has four children, any of which may start with 0x11 filler:

    0x00034101  56 bytes: u32 0, u32 0, u32 section number (41, 42, ...)
    0x00034102  the models this section uses, 40 bytes each:
                  +0  u32  part name hash (a part in this file, or in the
                           file of a neighbouring location)
                  +4  u32  second hash, +8 u32 third hash (0 if none): other
                           parts, often the same name with a later suffix,
                           probably lower detail versions
                  +12 u16, u16  two small numbers (0x10 for most; 10/20,
                           3/30, 50/70 on plants: probably LOD distances)
                  +16 u32  3, 10 or 0
                  +20      16 zero bytes
                  +36 f32  radius of the model
    0x00034103  the instances, 48 bytes each:
                  +0  6 x s16  world bounding box, min xyz then max xyz
                  +12 u16  index into this section's 0x34102 list
                  +14 u8   flags 4 or 8 on a few dozen instances, else 0
                  +15 u8   kind: 0 placed model, 1 ground shadow decal,
                           2 world-space part (road, terrain): no position,
                           identity matrix
                  +16 3 x f32  position
                  +28 9 x s16  3x3 matrix, 0x4000 = 1.0, row-major; each
                           row is one of the model's axes in the world and
                           may carry a uniform scale (0.5 and 0.75 on bushes
                           and trees):  world = position + v * M
                  +46 u16  0
    0x00034104  a box tree over the instances, for culling. Each node is a
                s16 box (12 bytes), u16 n, then n s16 children; the next node
                starts on an 8-byte boundary of the file (not of the chunk),
                and the chunk may end in zero bytes. A child >= 0 is an
                instance; a child < 0 is node -child. Node 0 is the root.
                Every instance appears exactly once.

Collision: 0x00034130-0x00034133 (top level, after the sections)

    0x00034131  the cells of a 32-unit grid: u32 999999, u32 999999, u32 0,
                then 12 bytes per cell: f32 x, f32 y (the cell's centre,
                always 16 mod 32), u16 start, u16 count into 0x34133. The
                starts chain: each is the previous start + count.
    0x00034133  u16 polygon indices, the cells' lists back to back.
    0x00034132  the polygons, 32 bytes each:
                  +0  u16  high byte: four 2-bit fields, on quads a rotation
                           of 0,1,2,3 (vertex order?); low byte 0
                  +2  u16  surface. Low nibble, named from the textures the
                           polygons sit on: 1 asphalt, 2 dirt, 3 gravel,
                           4 puddle/leaves, 5 bridge, 6 grass, 7 shoulder,
                           9 verge (mixed), b leaves, c sidewalk/curb, e blends.
                           0x16xx and 0x12xx are walls with no visible mesh
                           (invisible barriers); 0x0800 set on off-road
                           terrain, 0x1000 on many road faces (meaning unknown)
                  +4  s16  base height (whole units)
                  +6  u16  0xFFFF, else a number (unknown)
                  +8  4 x s16  x of 4 vertices, in 1/8 units
                  +16 4 x s16  y, in 1/8 units
                  +24 4 x s16  height above the base, in 1/256 units:
                           z = base + v/256
                A triangle repeats its third vertex as the fourth.
    0x00034130  unknown (u16 table, starts 410, 13, 0, 1, 2 ...)

Prop collision: 0x80036000 (first chunk of the file)

    0x00036001  24 bytes; u32 at +12 holds the number of objects (low half)
    0x00036002  a box tree over the objects (f32 centre, u16 x2, f32 half
                size, u16 children); layout not fully read
    0x00036003  the objects, 32 bytes each:
                  +0  u32  hash (unique per object)
                  +4  u16  3,  +6 u16 1
                  +8  u16  instance index in that section's 0x34103
                  +10 u16  section number
                  +12 u32  0
                  +16 3 x f32  position (the instance's own)
                  +28 f32  radius
                Each points at a placed plant a car can drive into (bushes,
                yuccas, cacti in entry 196): the radius is its collision size.

    Every polygon overlaps the cell that lists it. Where a collision vertex
    sits on a road or terrain mesh vertex (93% do, in x and y), its height
    matches that vertex's within 0.05 for 99.96%.

Usage:
    hp2_scenery.py FILE...                 check every section and report
    hp2_scenery.py FILE --collision OUT.obj [--game-axes]
        the collision polygons as an OBJ, one group per surface
    hp2_scenery.py FILE --list             one line per section
    hp2_scenery.py FILE --obj OUT.obj [--textures PNGDIR] [--sections 41-45,50]
                   [--no-shadows] [--game-axes]
        one OBJ for the location (or the chosen sections), all models placed.
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hp2_geom as g

SECTION, SEC_HDR, SEC_MODELS, SEC_INST, SEC_TREE = 0x80034100, 0x34101, 0x34102, 0x34103, 0x34104
KIND = {0: "model", 1: "shadow", 2: "world"}


def _body(d, o, s):
    b = o
    while b + 4 <= o + s and d[b:b + 4] == b"\x11" * 4:
        b += 4
    return b, o + s


def sections(d):
    """Every 0x80034100 chunk as a dict: number, models, instances, tree."""
    out = []
    for cid, off, sz in g.chunks(d, 0, len(d)):
        if cid != SECTION:
            continue
        kids = {c: _body(d, o, s) for c, o, s in g.chunks(d, off, off + sz)}
        b, e = kids[SEC_HDR]
        num = struct.unpack_from("<I", d, b + 8)[0]
        b, e = kids[SEC_MODELS]
        models = []
        for r in range(b, e - 39, 40):
            w = struct.unpack_from("<3I2HI", d, r)
            models.append(dict(hash=w[0], alt=[h for h in w[1:3] if h], near=w[3], far=w[4],
                               x=w[5], radius=struct.unpack_from("<f", d, r + 36)[0]))
        b, e = kids[SEC_INST]
        inst = []
        for r in range(b, e - 47, 48):
            box = struct.unpack_from("<6h", d, r)
            idx, flags, kind = struct.unpack_from("<HBB", d, r + 12)
            pos = struct.unpack_from("<3f", d, r + 16)
            m = [v / 16384.0 for v in struct.unpack_from("<9h", d, r + 28)]
            inst.append(dict(box=box, model=idx, flags=flags, kind=kind, pos=pos, m=m,
                             raw=d[r:r + 48]))
        b, e = kids[SEC_TREE]
        tree, q = [], b
        while q + 14 <= e:
            n = struct.unpack_from("<H", d, q + 12)[0]
            if n == 0 or q + 14 + 2 * n > e:
                break
            tree.append(dict(box=struct.unpack_from("<6h", d, q),
                             kids=struct.unpack_from("<%dh" % n, d, q + 14)))
            q += 14 + 2 * n
            q += (-q) & 7                  # 8-byte aligned in the file
        rest = d[q:e].strip(b"\0")         # the chunk may end in zero padding
        out.append(dict(num=num, models=models, inst=inst, tree=tree, tree_rest=len(rest)))
    return out


COLL_CELLS, COLL_POLYS, COLL_INDEX = 0x34131, 0x34132, 0x34133


# Low nibble of a collision polygon's surface word, named from the textures
# of the visible faces the polygons sit on (all 8 scenery files). 9 and e
# sit on mixed textures; 0 is rare.
SURFACES = {1: "asphalt", 2: "dirt", 3: "gravel", 4: "puddle", 5: "bridge", 6: "grass",
            7: "shoulder", 9: "verge", 0xB: "leaves", 0xC: "sidewalk", 0xE: "blend"}


def surface(flags):
    if flags & 0xFF00 in (0x1600, 0x1200):
        return "wall"
    return SURFACES.get(flags & 0xF, "surface%x" % (flags & 0xF))


def collision(d):
    """(cells, index list, polygons) from the 0x3413x chunks, or None."""
    ch = {}
    for cid, off, sz in g.chunks(d, 0, len(d)):
        if cid in (COLL_CELLS, COLL_POLYS, COLL_INDEX):
            ch[cid] = _body(d, off, sz)
    if len(ch) < 3:
        return None
    b, e = ch[COLL_POLYS]
    polys = []
    for r in range(b, e - 31, 32):
        order, flags, base, extra = struct.unpack_from("<HHhH", d, r)
        c = struct.unpack_from("<12h", d, r + 8)
        vs = [(c[i] / 8.0, c[4 + i] / 8.0, base + c[8 + i] / 256.0) for i in range(4)]
        if vs[3] == vs[2]:
            vs = vs[:3]
        polys.append(dict(order=order >> 8, flags=flags, extra=extra, v=vs))
    b, e = ch[COLL_INDEX]
    index = struct.unpack_from("<%dH" % ((e - b) // 2), d, b)
    b, e = ch[COLL_CELLS]
    cells = [struct.unpack_from("<2f2H", d, q) for q in range(b + 12, e - 11, 12)]
    return cells, index, polys


def prop_colliders(d):
    """The 0x36003 objects: (hash, section, instance, position, radius)."""
    out = []
    for cid, off, sz in g.chunks(d, 0, len(d)):
        if cid != 0x80036000:
            continue
        for c2, o2, s2 in g.chunks(d, off, off + sz):
            if c2 != 0x36003:
                continue
            b, e = _body(d, o2, s2)
            for r in range(b, e - 31, 32):
                h, _a, _c, inst, sec = struct.unpack_from("<IHHHH", d, r)
                out.append(dict(hash=h, section=sec, inst=inst,
                                pos=struct.unpack_from("<3f", d, r + 16),
                                radius=struct.unpack_from("<f", d, r + 28)[0]))
    return out


def check_props(d, secs, parts):
    objs = prop_colliders(d)
    if not objs:
        print("  no prop collision objects")
        return
    by = {s["num"]: s for s in secs}
    names = {p["hash"]: p["name"] for p in parts}
    ok, kinds = 0, {}
    for o in objs:
        s = by.get(o["section"])
        if not s or o["inst"] >= len(s["inst"]):
            continue
        i = s["inst"][o["inst"]]
        if max(abs(i["pos"][k] - o["pos"][k]) for k in range(3)) < 0.05:
            ok += 1
            nm = names.get(s["models"][i["model"]]["hash"], "?")
            nm = nm.split("_")[1] if nm.startswith("X") and "_" in nm else nm
            kinds[nm] = kinds.get(nm, 0) + 1
    top = sorted(kinds.items(), key=lambda x: -x[1])[:6]
    print("  prop collision: %d objects; %d sit exactly on the instance they name (%s)"
          % (len(objs), ok, ", ".join("%s %d" % kv for kv in top)))


def check_collision(d, parts):
    got = collision(d)
    if not got:
        print("  no collision chunks")
        return
    cells, index, polys = got
    chain = all(cells[i][2] + cells[i][3] == cells[i + 1][2] for i in range(len(cells) - 1))
    listed = inside = 0
    for x, y, s, n in cells:
        for k in index[s:s + n]:
            listed += 1
            if k >= len(polys):
                continue
            v = polys[k]["v"]
            inside += (min(p[0] for p in v) <= x + 16.5 and max(p[0] for p in v) >= x - 16.5 and
                       min(p[1] for p in v) <= y + 16.5 and max(p[1] for p in v) >= y - 16.5)
    # heights against the visible road and terrain meshes
    grid = {}
    for p in parts:
        if p["geom"] and p["name"].startswith(("RD", "DIRTRD", "TRN")):
            for m in g.part_mesh(d, p)[0]:
                for q in m["pos"]:
                    grid.setdefault((int(q[0] // 2), int(q[1] // 2)), []).append(q)
    on = good = 0
    for pl in polys:
        if surface(pl["flags"]) == "wall":   # stand above the ground they share x, y with
            continue
        for x, y, z in pl["v"]:
            best = None
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for q in grid.get((int(x // 2) + dx, int(y // 2) + dy), ()):
                        if abs(q[0] - x) < 0.25 and abs(q[1] - y) < 0.25:
                            e = abs(q[2] - z)
                            best = e if best is None else min(best, e)
            if best is not None:
                on += 1
                good += best < 0.05
    kinds = {}
    for pl in polys:
        k = surface(pl["flags"])
        kinds[k] = kinds.get(k, 0) + 1
    nverts = sum(len(pl["v"]) for pl in polys)
    print("  collision: %d polygons (%s) in %d cells; starts chain %s; "
          "%d of %d listed polygons overlap their cell"
          % (len(polys), ", ".join("%d %s" % (v, k) for k, v in sorted(kinds.items(), key=lambda x: -x[1])),
             len(cells), "yes" if chain else "NO", inside, listed))
    print("  collision heights (walls left out): %d of %d vertices sit on a road/terrain "
          "mesh vertex; %d of those (%.2f%%) match its height within 0.05"
          % (on, nverts, good, 100.0 * good / max(1, on)))


def export_collision(path, out, yup=True):
    d, info = g.parse(path)
    got = collision(d)
    if not got:
        raise SystemExit("no collision chunks in %s" % path)
    polys = got[2]
    conv = g.to_yup if yup else (lambda v: tuple(v))
    colour = {"asphalt": (0.35, 0.35, 0.40), "dirt": (0.65, 0.50, 0.30),
              "gravel": (0.60, 0.58, 0.50), "grass": (0.35, 0.65, 0.30),
              "shoulder": (0.55, 0.50, 0.45), "verge": (0.50, 0.55, 0.35),
              "bridge": (0.70, 0.70, 0.75), "sidewalk": (0.80, 0.80, 0.80),
              "wall": (0.85, 0.20, 0.20)}
    groups = {}
    for pl in polys:
        groups.setdefault(surface(pl["flags"]), []).append(pl)
    k = 1
    with open(out, "w") as f:
        f.write("# collision of %s\n# axes: %s\n" % (os.path.basename(path),
                                                    "Y up" if yup else "as stored: Z up"))
        for name in sorted(groups):
            c = colour.get(name, (0.8, 0.8, 0.2))
            f.write("o collision_%s\n" % name)
            for pl in groups[name]:
                for p in pl["v"]:
                    f.write("v %.3f %.3f %.3f %.2f %.2f %.2f\n" % (conv(p) + c))
                f.write("f %s\n" % " ".join(str(k + i) for i in range(len(pl["v"]))))
                k += len(pl["v"])
    print("wrote %s: %d polygons (%s)" % (out, len(polys),
          ", ".join("%d %s" % (len(v), n) for n, v in sorted(groups.items()))))


def place(v, inst):
    m, p = inst["m"], inst["pos"]
    x, y, z = v
    return (p[0] + x * m[0] + y * m[3] + z * m[6],
            p[1] + x * m[1] + y * m[4] + z * m[7],
            p[2] + x * m[2] + y * m[5] + z * m[8])


def turn(n, inst):
    m = inst["m"]
    x, y, z = n
    r = (x * m[0] + y * m[3] + z * m[6], x * m[1] + y * m[4] + z * m[7],
         x * m[2] + y * m[5] + z * m[8])
    ln = math.sqrt(r[0] ** 2 + r[1] ** 2 + r[2] ** 2) or 1.0
    return (r[0] / ln, r[1] / ln, r[2] / ln)


def parse_ranges(s):
    keep = set()
    for piece in s.split(","):
        a, _, b = piece.partition("-")
        keep.update(range(int(a), int(b or a) + 1))
    return keep


class Meshes:
    def __init__(self, d, parts):
        self.d, self.by = d, {p["hash"]: p for p in parts if p.get("hash") is not None}
        self.cache = {}

    def get(self, h):
        if h not in self.cache:
            p = self.by.get(h)
            self.cache[h] = (p, g.part_mesh(self.d, p)[0]) if p and p["geom"] else (p, None)
        return self.cache[h]


def check(path, listing=False):
    d, info = g.parse(path)
    secs = sections(d)
    meshes = Meshes(d, info["parts"])
    print("%s  (%s bytes)  built from %s" % (os.path.basename(path), "{:,}".format(len(d)),
                                             info["devpath"] or "?"))
    t = dict(models=0, unknown=0, inst=0, idx_bad=0, fit=0, fitted=0, tree_ok=0, scaled=0)
    kinds, unknown = {}, set()
    for s in secs:
        own = [mm["hash"] in meshes.by for mm in s["models"]]
        t["models"] += len(own)
        t["unknown"] += own.count(False)
        unknown.update(mm["hash"] for mm, ok in zip(s["models"], own) if not ok)
        seen = {}
        for node in s["tree"]:
            for k in node["kids"]:
                if k >= 0:
                    seen[k] = seen.get(k, 0) + 1
        tree_ok = (s["tree_rest"] == 0 and len(seen) == len(s["inst"])
                   and all(v == 1 for v in seen.values())
                   and all(-k < len(s["tree"]) for n in s["tree"] for k in n["kids"] if k < 0))
        t["tree_ok"] += tree_ok
        fit = fitted = 0
        for i in s["inst"]:
            t["inst"] += 1
            kinds[i["kind"]] = kinds.get(i["kind"], 0) + 1
            if i["model"] >= len(s["models"]):
                t["idx_bad"] += 1
                continue
            if abs(math.sqrt(sum(v * v for v in i["m"][0:3])) - 1) > 0.02:
                t["scaled"] += 1
            p, mesh = meshes.get(s["models"][i["model"]]["hash"])
            if not mesh:
                continue
            pts = [place(v, i) for m in mesh for v in m["pos"]]
            if not pts:
                continue
            lo = [min(q[k] for q in pts) for k in range(3)]
            hi = [max(q[k] for q in pts) for k in range(3)]
            err = max(max(abs(lo[k] - i["box"][k]) for k in range(3)),
                      max(abs(hi[k] - i["box"][k + 3]) for k in range(3)))
            fitted += 1
            fit += err <= 1.5
        t["fit"] += fit
        t["fitted"] += fitted
        if listing:
            print("  section %3d  %3d models (%d not in this file)  %5d instances  "
                  "%4d/%4d inside their stored box  tree %s"
                  % (s["num"], len(s["models"]), own.count(False), len(s["inst"]), fit, fitted,
                     "ok" if tree_ok else "WRONG"))
    print("  %d sections (%d-%d)   %d instances: %s"
          % (len(secs), min(s["num"] for s in secs), max(s["num"] for s in secs), t["inst"],
             ", ".join("%d %s" % (v, KIND.get(k, k)) for k, v in sorted(kinds.items()))))
    print("  model index in range for %d of %d instances" % (t["inst"] - t["idx_bad"], t["inst"]))
    print("  placed model inside its stored box (within 1.5) for %d of %d (%.1f%%)"
          % (t["fit"], t["fitted"], 100.0 * t["fit"] / max(1, t["fitted"])))
    print("  %d instances scaled" % t["scaled"])
    print("  box tree covers every instance once in %d of %d sections" % (t["tree_ok"], len(secs)))
    print("  model hashes: %d of %d are parts of this file; %d distinct others"
          % (t["models"] - t["unknown"], t["models"], len(unknown)))
    check_collision(d, info["parts"])
    check_props(d, secs, info["parts"])
    print()


def export(path, out, texdir=None, keep=None, shadows=True, yup=True):
    d, info = g.parse(path)
    secs = sections(d)
    meshes = Meshes(d, info["parts"])
    car = os.path.basename(os.path.dirname(info["devpath"].replace("\\", "/"))) or "Scenery"
    conv = g.to_yup if yup else (lambda v: tuple(v))
    outdir = os.path.dirname(os.path.abspath(out))
    stem = os.path.splitext(os.path.basename(out))[0]
    mtl_entries, missing = {}, set()
    done, dup, skipped, n_obj = set(), 0, {}, 0
    vbase = nbase = tbase = 1
    with open(out, "w") as f:
        f.write("# %s, sections %s\n# axes: %s\nmtllib %s.mtl\n"
                % (os.path.basename(path), "all" if keep is None else sorted(keep),
                   "Y up" if yup else "as stored: Z up", stem))
        for s in secs:
            if keep is not None and s["num"] not in keep:
                continue
            for k, i in enumerate(s["inst"]):
                if i["kind"] == 1 and not shadows:
                    continue
                if i["raw"] in done:          # the same record in two places
                    dup += 1
                    continue
                done.add(i["raw"])
                h = s["models"][i["model"]]["hash"]
                p, mesh = meshes.get(h)
                if not mesh:
                    skipped[h] = skipped.get(h, 0) + 1
                    continue
                n_obj += 1
                f.write("o S%d_%s_%d\n" % (s["num"], p["name"] or "%08x" % h, k))
                for m in sorted(mesh, key=lambda m: g.draw_key(p, m, car)[0]):
                    key, tex = g.draw_key(p, m, car)
                    if key not in mtl_entries:
                        png = None
                        for cand in (g.runtime_textures(tex, car) if texdir else []):
                            png = g.find_png(texdir, cand, car)
                            if png:
                                break
                        if texdir and not png:
                            missing.add(tex)
                        mtl_entries[key] = os.path.relpath(png, outdir) if png else None
                    f.write("usemtl %s\n" % key)
                    for j, v in enumerate(m["pos"]):
                        c = m["col"][j] if j < len(m["col"]) else (1.0, 1.0, 1.0)
                        f.write("v %.4f %.4f %.4f %.4f %.4f %.4f\n"
                                % (conv(place(v, i)) + tuple(c)))
                    for u, v in m["uv"]:
                        f.write("vt %.5f %.5f\n" % (u, 1.0 - v))
                    for nx in m["normals"] or []:
                        f.write("vn %.4f %.4f %.4f\n" % conv(turn(nx, i)))
                    for a, b, c in m["tris"]:
                        if m["normals"]:
                            f.write("f %d/%d/%d %d/%d/%d %d/%d/%d\n" % (
                                vbase + a, tbase + a, nbase + a, vbase + b, tbase + b, nbase + b,
                                vbase + c, tbase + c, nbase + c))
                        else:
                            f.write("f %d/%d %d/%d %d/%d\n" % (
                                vbase + a, tbase + a, vbase + b, tbase + b, vbase + c, tbase + c))
                    vbase += m["n"]
                    tbase += len(m["uv"])
                    if m["normals"]:
                        nbase += len(m["normals"])
    g.write_mtl(os.path.join(outdir, stem + ".mtl"), sorted(mtl_entries.items()))
    print("wrote %s: %d placed models, %d material/texture pairs" % (out, n_obj, len(mtl_entries)))
    if dup:
        print("  %d repeated instance records written once" % dup)
    if skipped:
        print("  %d instances of %d models not in this file left out"
              % (sum(skipped.values()), len(skipped)))
    if missing:
        print("  no PNG for: %s" % ", ".join(sorted(missing)[:20])
              + (" ..." if len(missing) > 20 else ""))


def main():
    argv = sys.argv[1:]

    def opt(name):
        if name in argv:
            i = argv.index(name)
            v = argv[i + 1]
            del argv[i:i + 2]
            return v
        return None
    out, texdir, secs = opt("--obj"), opt("--textures"), opt("--sections")
    coll = opt("--collision")
    if coll:
        files = [a for a in argv if not a.startswith("--")]
        export_collision(files[0], coll, yup="--game-axes" not in argv)
        return
    if texdir:
        print("named %d texture hashes from the PNG names under %s"
              % (g.learn_png_names(texdir), texdir))
    files = [a for a in argv if not a.startswith("--")]
    if not files:
        print(__doc__)
        return
    if out:
        export(files[0], out, texdir, parse_ranges(secs) if secs else None,
               shadows="--no-shadows" not in argv, yup="--game-axes" not in argv)
        return
    for f in files:
        check(f, "--list" in argv)


if __name__ == "__main__":
    main()
