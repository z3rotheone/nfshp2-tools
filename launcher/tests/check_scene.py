#!/usr/bin/env python3
"""Compare the C scenery placement (test_assets --scenedump FILE) with
tools/hp2_scenery.py and hp2_geom.py on the same file.

    check_scene.py ./test_assets SCENERY.bin

Checks, per section: instances placed, triangles, and the bounding box of
the placed vertices (to 0.01); and over the file: repeated records, models
missing, batches that fail to decode. The sky (SKYDOME, PANORAMA) is kept
apart by the C side and SKYDOME_ENVMAP is not drawn, so their triangles
count in no section."""
SKY = ("SKYDOME", "PANORAMA")
ENVMAP = "SKYDOME_ENVMAP"
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import hp2_geom as g          # noqa: E402
import hp2_scenery as sc      # noqa: E402


def python_side(path):
    d, info = g.parse(path)
    meshes = sc.Meshes(d, info["parts"])
    done = set()
    tot = dict(inst=0, placed=0, dup=0, missing=0, tris=0, sky=0)
    per = []
    for s in sc.sections(d):
        n = tris = 0
        lo, hi = [1e30] * 3, [-1e30] * 3
        for i in s["inst"]:
            tot["inst"] += 1
            if i["raw"] in done:
                tot["dup"] += 1
                continue
            done.add(i["raw"])
            if i["model"] >= len(s["models"]):
                tot["missing"] += 1
                continue
            p, mesh = meshes.get(s["models"][i["model"]]["hash"])
            if not mesh or not any(m["tris"] for m in mesh):
                tot["missing"] += 1
                continue
            n += 1
            if p["name"] == ENVMAP:
                continue
            if p["name"] in SKY:
                t = sum(len(m["tris"]) for m in mesh)
                tot["sky"] += t
                tot["tris"] += t
                continue
            for m in mesh:
                tris += len(m["tris"])
                for t in m["tris"]:
                    for k in t:
                        w = sc.place(m["pos"][k], i)
                        for a in range(3):
                            lo[a] = min(lo[a], w[a])
                            hi[a] = max(hi[a], w[a])
        tot["placed"] += n
        tot["tris"] += tris
        per.append((s["num"], n, tris, lo + hi))
    return tot, per


def python_ground(path, points):
    """The highest walkable collision polygon under each point, as
    hp2_coll_ground does with zref far above: walls (0x12xx, 0x16xx) and
    polygons steeper than normal z 0.3 left out; a quad is the triangles
    (0, 1, 2) and (0, 2, 3)."""
    d = open(path, "rb").read()
    cells, index, polys = sc.collision(d)
    flat = []
    for pl in polys:
        v = pl["v"]
        n = len(v)
        a = [v[2][k] - v[0][k] for k in range(3)]
        b = [v[3 if n == 4 else 1][k] - v[1 if n == 4 else 0][k] for k in range(3)]
        nx, ny, nz = a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]
        ln = (nx * nx + ny * ny + nz * nz) ** 0.5 or 1.0
        wall = (pl["flags"] & 0xFF00) in (0x1200, 0x1600) or abs(nz / ln) < 0.3
        flat.append(None if wall else v)
    grid = {}
    for x, y, s, cnt in cells:
        grid[(int(x // 32), int(y // 32))] = index[s:s + cnt]
    walls = sum(1 for v in flat if v is None)

    def tri(v, a, b, c, x, y):
        d_ = (v[b][0] - v[a][0]) * (v[c][1] - v[a][1]) - (v[c][0] - v[a][0]) * (v[b][1] - v[a][1])
        if abs(d_) < 1e-6:
            return None
        u = ((x - v[a][0]) * (v[c][1] - v[a][1]) - (v[c][0] - v[a][0]) * (y - v[a][1])) / d_
        w = ((v[b][0] - v[a][0]) * (y - v[a][1]) - (x - v[a][0]) * (v[b][1] - v[a][1])) / d_
        if u < -1e-4 or w < -1e-4 or u + w > 1 + 1e-4:
            return None
        return v[a][2] + u * (v[b][2] - v[a][2]) + w * (v[c][2] - v[a][2])
    out = []
    for x, y in points:
        best = None
        for k in grid.get((int(x // 32), int(y // 32)), ()):
            v = flat[k] if k < len(flat) else None
            if v is None:
                continue
            h = tri(v, 0, 1, 2, x, y)
            if h is None and len(v) == 4:
                h = tri(v, 0, 2, 3, x, y)
            if h is not None and (best is None or h > best):
                best = h
        out.append(best)
    return len(polys), walls, len(cells), out


def main():
    exe, path = sys.argv[1], sys.argv[2]
    tmp = None
    with open(path, "rb") as f:
        head = f.read(4)
    if head == b"COMP":                    # an .LZC entry: compare the decompressed data
        import tempfile
        import hp2_comp
        with open(path, "rb") as f:
            data, _ = hp2_comp.decompress(f.read())
        fd, tmp = tempfile.mkstemp(suffix=".bin")
        with os.fdopen(fd, "wb") as f:
            f.write(data)
    try:
        run(exe, path, tmp or path)
    finally:
        if tmp:
            os.remove(tmp)


def run(exe, path, plain):
    out = subprocess.run([exe, "--scenedump", path], capture_output=True, text=True, check=True).stdout
    c_tot, c_per, c_coll, c_ground = {}, [], None, []
    for line in out.splitlines():
        w = line.split()
        if w and w[0] == "coll":
            c_coll = tuple(int(x) for x in w[1:4])
        elif w and w[0] == "ground":
            c_ground.append((float(w[1]), float(w[2]), float(w[3])))
        elif w and w[0] == "total":
            c_tot = {k: int(v) for k, v in (x.split("=") for x in w[1:])}
        elif w and w[0] == "section":
            c_per.append((int(w[1]), int(w[2]), int(w[3]), [float(x) for x in w[4:10]]))
    tot, per = python_side(plain)
    bad = 0
    for k in ("inst", "placed", "dup", "missing", "tris", "sky"):
        if c_tot.get(k) != tot[k]:
            print("%s: C %s, Python %s" % (k, c_tot.get(k), tot[k]))
            bad += 1
    if c_tot.get("bad"):
        print("C: %d batches failed to decode" % c_tot["bad"])
        bad += 1
    if len(c_per) != len(per):
        print("sections: C %d, Python %d" % (len(c_per), len(per)))
        bad += 1
    for a, b in zip(c_per, per):
        if a[:3] != b[:3] or (a[1] and max(abs(x - y) for x, y in zip(a[3], b[3])) > 0.01):
            if bad < 20:
                print("section %d: C %s, Python %s" % (a[0], a, b))
            bad += 1
    # the collision mesh: counts, and the ground wherever C found it
    npoly, walls, ncell, py_ground = python_ground(plain, [(x, y) for x, y, _ in c_ground])
    if c_coll != (npoly, walls, ncell):
        print("collision: C %s, Python %s" % (c_coll, (npoly, walls, ncell)))
        bad += 1
    gbad = sum(1 for (x, y, z), p in zip(c_ground, py_ground) if p is None or abs(p - z) > 0.01)
    if gbad:
        print("ground: %d of %d points differ" % (gbad, len(c_ground)))
        bad += 1
    print("%s: %d sections, %d placed, %d triangles; collision %d polygons (%d walls), ground at %d points: %s"
          % (os.path.basename(path), len(per), tot["placed"], tot["tris"], npoly, walls, len(c_ground),
             "identical" if not bad else "%d differences" % bad))
    if bad:
        sys.exit(1)


if __name__ == "__main__":
    main()
