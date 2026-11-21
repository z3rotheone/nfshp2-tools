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
    c_tot, c_per = {}, []
    for line in out.splitlines():
        w = line.split()
        if w and w[0] == "total":
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
    print("%s: %d sections, %d placed, %d triangles: %s"
          % (os.path.basename(path), len(per), tot["placed"], tot["tris"],
             "identical" if not bad else "%d differences" % bad))
    if bad:
        sys.exit(1)


if __name__ == "__main__":
    main()
