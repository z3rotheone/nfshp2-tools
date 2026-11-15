#!/usr/bin/env python3
"""
Assemble a whole car as one OBJ: the body, plus its wheels and brake discs
placed where the car database (hp2_carinfo.py) says the wheels are.

A car's GEOMETRY.BIN holds parts in two kinds of place:
  - already in position: <CAR>_A.._D (the body, four levels of detail),
    _SIDE_MIRROR_LE/_RI, _LICENSE_PLATE_, _SCUFFS
  - at the origin, for the game to place: _TIRE_FRONT_A/B/C (one tyre, hub at
    the origin, its outer face toward -y: a right-hand wheel), _BRAKE_FRONT,
    _BRAKE_REAR, _WHEEL_BLUR, _WIPER_LEFT/_RIGHT
The car database gives each wheel's hub position, radius and tyre width.

This puts the tyre and the matching brake disc at each hub, mirrored in y
for the left-hand wheels. By default it also scales the tyre to the stored
radius (x, z) and tyre width (y); the model's own tyre is a little larger
than the stored sizes (911: radius 0.35 vs 0.319). Whether the game does
the same is not checked: --no-scale keeps the model's size.
Wipers and wheel blur are left out (their pivots are not known). The
_SCUFFS overlay (SKRATCHES, a mostly transparent damage layer) is added only
with --lights damaged.

Usage:
    hp2_car.py GEOMETRY.BIN GLOBALB.BUN OUT.obj [--textures PNGDIR] [--skin N]
               [--lights off|on|damaged] [--lod A|B|C|D] [--no-scale] [--game-axes]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hp2_carinfo
import hp2_geom as g


def transform(mesh, f, flip=False):
    out = []
    for m in mesh:
        m2 = dict(m)
        m2["pos"] = [f(p) for p in m["pos"]]
        if m.get("normals"):
            m2["normals"] = [(n[0], -n[1], n[2]) if flip else n for n in m["normals"]]
        if flip:
            m2["tris"] = [(a, c, b) for a, b, c in m["tris"]]
        out.append(m2)
    return out


def bounds(mesh):
    pts = [p for m in mesh for p in m["pos"]]
    return ([min(p[k] for p in pts) for k in range(3)], [max(p[k] for p in pts) for k in range(3)])


def main():
    a = sys.argv[1:]

    def opt(name, default=None):
        if name in a:
            i = a.index(name)
            v = a[i + 1]
            del a[i:i + 2]
            return v
        return default
    texdir, lod = opt("--textures"), opt("--lod", "A")
    g.SKIN = int(opt("--skin", "1"))
    state = opt("--lights", "off")
    scale = "--no-scale" not in a
    yup = "--game-axes" not in a
    a = [x for x in a if not x.startswith("--")]
    if len(a) != 3:
        print(__doc__)
        return
    geo, globalb, out = a
    if texdir:
        g.learn_png_names(texdir)
    d, info = g.parse(geo)
    car = os.path.basename(os.path.dirname(info["devpath"].replace("\\", "/")))
    cars = [c for c in hp2_carinfo.records(open(globalb, "rb").read()) if c["model"] == car]
    if not cars:
        raise SystemExit("%s is not in the car database" % car)
    rec = cars[0]
    parts = {p["name"]: p for p in info["parts"]}

    def part(suffix):
        for nm, p in parts.items():
            if nm == car + suffix or (len(nm) == 23 and (car + suffix).startswith(nm)):
                return p, g.part_mesh(d, p)[0]
        return None, None

    pieces = []                                       # (name, part, mesh)
    # _SCUFFS is a damage overlay (SKRATCHES, mostly transparent): only when damaged
    for suffix in ("_" + lod, "_SIDE_MIRROR_LEFT", "_SIDE_MIRROR_RIGHT", "_LICENSE_PLATE_REAR") + \
            (("_SCUFFS",) if state == "damaged" else ()):
        p, m = part(suffix)
        if p:
            pieces.append((p["name"], p, m))
    tp, tmesh = part("_TIRE_FRONT_" + lod)
    if not tp:
        tp, tmesh = part("_TIRE_FRONT_A")
    lo, hi = bounds(tmesh)
    t_radius = max(hi[0] - lo[0], hi[2] - lo[2]) / 2
    t_width = hi[1] - lo[1]
    for w in rec["wheels"]:
        left = w["y"] > 0
        sx = sz = (w["radius"] / t_radius) if scale else 1.0
        sy = (w["width"] / t_width) if scale else 1.0
        sgn = -1.0 if left else 1.0

        def place(p, w=w, sx=sx, sy=sy, sz=sz, sgn=sgn):
            return (w["x"] + p[0] * sx, w["y"] + sgn * p[1] * sy, w["z"] + p[2] * sz)
        pieces.append(("%s_TIRE_%s" % (car, w["wheel"].upper()), tp, transform(tmesh, place, left)))
        bp, bmesh = part("_BRAKE_FRONT" if w["wheel"].startswith("front") else "_BRAKE_REAR")
        if bp:
            def place_b(p, w=w, sgn=sgn):
                return (w["x"] + p[0], w["y"] + sgn * p[1], w["z"] + p[2])
            pieces.append(("%s_BRAKE_%s" % (car, w["wheel"].upper()), bp, transform(bmesh, place_b, left)))

    conv = g.to_yup if yup else (lambda v: tuple(v))
    outdir = os.path.dirname(os.path.abspath(out))
    stem = os.path.splitext(os.path.basename(out))[0]
    mtl = {}
    vbase = tbase = nbase = 1
    with open(out, "w") as f:
        f.write("# %s assembled: body %s, wheels from the car database record %s\n"
                "# tyres %s\n# axes: %s\nmtllib %s.mtl\n"
                % (car, lod, rec["name"], "scaled to the stored radius and width" if scale
                   else "at the model's own size", "Y up" if yup else "as stored", stem))
        for name, p, mesh in pieces:
            f.write("o %s\n" % name)
            for m in sorted(mesh, key=lambda m: g.draw_key(p, m, car)[0]):
                key, tex = g.draw_key(p, m, car)
                if key not in mtl:
                    png = None
                    for cand in (g.runtime_textures(tex, car, state) if texdir else []):
                        png = g.find_png(texdir, cand, car)
                        if png:
                            break
                    mtl[key] = os.path.relpath(png, outdir) if png else None
                f.write("usemtl %s\n" % key)
                for i, v in enumerate(m["pos"]):
                    c = m["col"][i] if i < len(m["col"]) else (1.0, 1.0, 1.0)
                    f.write("v %.5f %.5f %.5f %.4f %.4f %.4f\n" % (conv(v) + tuple(c)))
                for u, v in m["uv"]:
                    f.write("vt %.5f %.5f\n" % (u, 1.0 - v))
                for nx in m.get("normals") or []:
                    f.write("vn %.4f %.4f %.4f\n" % conv(nx))
                for x, y, z in m["tris"]:
                    if m.get("normals"):
                        f.write("f %d/%d/%d %d/%d/%d %d/%d/%d\n" % (
                            vbase + x, tbase + x, nbase + x, vbase + y, tbase + y, nbase + y,
                            vbase + z, tbase + z, nbase + z))
                    else:
                        f.write("f %d/%d %d/%d %d/%d\n" % (vbase + x, tbase + x, vbase + y,
                                                           tbase + y, vbase + z, tbase + z))
                vbase += m["n"]
                tbase += len(m["uv"])
                if m.get("normals"):
                    nbase += len(m["normals"])
    g.write_mtl(os.path.join(outdir, stem + ".mtl"), sorted(mtl.items()))
    print("wrote %s: %d pieces (%s), wheels from %s; tyre model radius %.3f width %.3f -> "
          "stored %s" % (out, len(pieces), ", ".join(n for n, _, _ in pieces[:3]) + "...",
                         rec["name"], t_radius, t_width,
                         ", ".join("%.3f/%.3f" % (w["radius"], w["width"]) for w in rec["wheels"][::2])))
    missing = sorted(k for k, v in mtl.items() if v is None)
    if texdir and missing:
        print("  no PNG for: %s" % ", ".join(missing))


if __name__ == "__main__":
    main()
