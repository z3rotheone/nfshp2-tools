"""
Work out which game texture each PCSX2 replacement file actually is.

PCSX2 names replacement textures by a hash it computes from the texture upload,
so a replacement pack is a folder of opaque filenames. A native port has no such
hash - it loads the game's own .tpk assets by name - so the pictures in a pack
are reusable but the thing identifying them is not.

This matches them up by image content instead of by hash, which avoids
reimplementing PCSX2's hashing and works even when the replacement has been
upscaled: every texture is reduced to a small resolution-independent
fingerprint, the nearest candidates are then compared properly, and anything
that stays ambiguous is reported as ambiguous rather than guessed at.

Matching ignores alpha and compares colour only, because a dumped alpha channel
has been through a scaling convention this tool should not have to assume.

Usage:
    python hp2_match.py extracted PCSX2_TEXTURE_DIR [-o map.csv]
    python hp2_match.py extracted PCSX2_TEXTURE_DIR --missing

`extracted` is the folder of archive entries from hp2_extract.py; every texture
pack in it is read. PCSX2_TEXTURE_DIR is searched recursively for PNGs, so
point it at the game's folder and both dumps and replacements are picked up.

--missing also lists the game textures that no file in the pack matches, which
is the work left to do.

Needs: pip install pillow numpy
"""
import csv
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hp2_tpk as T                                   # noqa: E402

GRID = 8                  # fingerprint resolution
AMBIGUOUS = 1.5           # best must beat the runner-up by this factor
MATCH_OK = 32.0           # mean abs colour error, 0-255, past which it is not a match
SURE = 8.0                # below this the match needs no judgement from the reader


def fingerprint(rgb):
    """A GRID x GRID mean-colour summary, independent of resolution."""
    h, w = rgb.shape[:2]
    ys = (np.arange(GRID + 1) * h // GRID)
    xs = (np.arange(GRID + 1) * w // GRID)
    out = np.zeros((GRID, GRID, 3), dtype=np.float64)
    for i in range(GRID):
        for j in range(GRID):
            y0, y1 = ys[i], max(ys[i] + 1, ys[i + 1])
            x0, x1 = xs[j], max(xs[j] + 1, xs[j + 1])
            out[i, j] = rgb[y0:y1, x0:x1].reshape(-1, 3).mean(0)
    return out.ravel()


def fitted_error(small, ref):
    """Mean colour error after allowing each channel one gain and offset.

    A replacement pack is often brightened, saturated or otherwise regraded, so
    a raw difference says "not the same texture" about a texture that plainly
    is. Fitting a gain and offset per channel by least squares removes exactly
    that kind of global change and leaves genuine differences in place: two
    unrelated images do not become similar under a linear adjustment.
    """
    err = np.empty(3)
    for c in range(3):
        x = small[:, :, c].ravel()
        y = ref[:, :, c].ravel()
        vx = x.var()
        if vx < 1e-6:                       # flat channel: nothing to fit
            err[c] = np.abs(x.mean() - y.mean())
            continue
        g = ((x - x.mean()) * (y - y.mean())).mean() / vx
        g = float(np.clip(g, 0.25, 4.0))
        o = y.mean() - g * x.mean()
        err[c] = np.abs(g * x + o - y).mean()
    return float(err.mean())


def box_to(rgb, w, h):
    """Reduce an image to w x h by averaging, so an upscaled replacement can be
    compared with the original it was made from."""
    return np.array(Image.fromarray(rgb, "RGB").resize((w, h), Image.BOX), dtype=np.float64)


def load_assets(root):
    """Every texture in every pack under `root`, decoded."""
    assets = []
    for n in sorted(os.listdir(root)):
        fp = os.path.join(root, n)
        if not os.path.isfile(fp):
            continue
        with open(fp, "rb") as f:
            if f.read(4) != b"\x01\x00\x30\xb0":
                continue
        try:
            d = open(fp, "rb").read()
            recs, base = T.parse_pack(d)
        except Exception:
            continue
        for r in recs:
            res = T.decode(d, base, r)
            if isinstance(res, str):
                continue
            rgb = np.ascontiguousarray(res[:, :, :3])
            assets.append(dict(pack=os.path.splitext(n)[0], name=r["name"],
                               w=r["w"], h=r["h"], rgb=rgb, fp=fingerprint(rgb)))
    return assets


IMAGES = (".png", ".jpg", ".jpeg", ".bmp", ".dds", ".tga", ".webp")


def load_pngs(root):
    if not os.path.isdir(root):
        raise SystemExit("%s is not a folder. PCSX2 shows the right path under\n"
                         "Settings -> Graphics -> Texture Replacements; a Flatpak install\n"
                         "keeps it under ~/.var/app/net.pcsx2.PCSX2/config/PCSX2/textures."
                         % root)
    out, other = [], {}
    for dirpath, _dirs, files in os.walk(root):
        for f in sorted(files):
            ext = os.path.splitext(f)[1].lower()
            if ext != ".png":
                if ext in IMAGES:
                    other[ext] = other.get(ext, 0) + 1
                continue
            p = os.path.join(dirpath, f)
            try:
                im = Image.open(p).convert("RGB")
            except Exception:
                continue
            out.append((p, np.array(im, dtype=np.uint8)))
    if not out and other:
        print("  no PNGs, but this folder holds %s"
              % ", ".join("%d %s" % (n, e) for e, n in sorted(other.items())))
        print("  convert them first, e.g.  mogrify -format png *%s"
              % sorted(other, key=lambda e: -other[e])[0])
    return out


def match(assets, pngs):
    F = np.array([a["fp"] for a in assets])
    rows, unmatched = [], []
    seen = set()
    for path, rgb in pngs:
        fp = fingerprint(rgb)
        d = np.abs(F - fp).mean(1)
        order = np.argsort(d)[:6]
        scored = []
        for i in order:
            a = assets[i]
            try:
                small = box_to(rgb, a["w"], a["h"])
            except Exception:
                continue
            ref = a["rgb"].astype(np.float64)
            raw = float(np.abs(small - ref).mean())
            scored.append((fitted_error(small, ref), raw, i))
        if not scored:
            unmatched.append((path, None, None))
            continue
        scored.sort()
        err, raw, i = scored[0]
        runner = scored[1][0] if len(scored) > 1 else float("inf")
        a = assets[i]
        h, w = rgb.shape[:2]
        scale = "%gx" % (w / a["w"]) if a["w"] and w % a["w"] == 0 and w // a["w"] > 1 else \
                ("same size" if (w, h) == (a["w"], a["h"]) else "%dx%d vs %dx%d" % (w, h, a["w"], a["h"]))
        if err > MATCH_OK:
            unmatched.append((path, a["name"], err))
            continue
        kind = "exact" if raw == 0 else ("close" if err < SURE else "likely")
        # an additive term as well as the ratio, or two textures that are
        # byte-identical both score zero and the tie goes unnoticed
        if runner < err * AMBIGUOUS + 0.5:
            kind = "ambiguous"
        rows.append(dict(file=os.path.basename(path), asset=a["name"], pack=a["pack"],
                         width=a["w"], height=a["h"], scale=scale,
                         error="%.2f" % err, raw_error="%.2f" % raw, confidence=kind,
                         runner_up=assets[scored[1][2]]["name"] if len(scored) > 1 else ""))
        seen.add((a["pack"], a["name"]))
    return rows, unmatched, seen


def main():
    a = sys.argv[1:]
    out = None
    if "-o" in a:
        i = a.index("-o")
        out = a[i + 1]
        del a[i:i + 2]
    show_missing = "--missing" in a
    a = [x for x in a if not x.startswith("--")]
    if len(a) != 2:
        print(__doc__)
        return
    root, texdir = a

    print("reading textures from the disc ...", flush=True)
    assets = load_assets(root)
    print("  %d textures in %d packs"
          % (len(assets), len({x["pack"] for x in assets})))
    print("reading PNGs from %s ..." % texdir, flush=True)
    pngs = load_pngs(texdir)
    print("  %d PNG files" % len(pngs))
    if not assets or not pngs:
        print("nothing to match")
        return

    rows, unmatched, seen = match(assets, pngs)
    names = {}
    for r in rows:
        names.setdefault(r["file"], []).append(r["asset"])

    exact = sum(1 for r in rows if r["confidence"] == "exact")
    close = sum(1 for r in rows if r["confidence"] == "close")
    regr = sum(1 for r in rows if r["confidence"] == "likely")
    amb = sum(1 for r in rows if r["confidence"] == "ambiguous")
    print()
    print("%d matched: %d exact, %d close (upscaled), %d likely (recoloured), %d ambiguous"
          % (len(rows), exact, close, regr, amb))
    print("%d PNGs matched nothing in the game's own textures" % len(unmatched))

    for r in rows[:25]:
        print("  %-40s -> %-24s %4dx%-4d %-10s err %-6s %s"
              % (r["file"][:40], r["asset"], r["width"], r["height"],
                 r["scale"], r["error"], r["confidence"]))
    if len(rows) > 25:
        print("  ... and %d more" % (len(rows) - 25))

    if unmatched:
        print()
        print("not matched - nearest candidate and its error, for you to judge:")
        for path, nm, e in unmatched[:15]:
            print("  %-40s %s" % (os.path.basename(path)[:40],
                                  "nearest %s at %.1f" % (nm, e) if nm else "no candidate"))
        if len(unmatched) > 15:
            print("  ... and %d more" % (len(unmatched) - 15))

    if amb:
        print()
        print("ambiguous ones, where a second texture fits nearly as well:")
        for r in rows:
            if r["confidence"] == "ambiguous":
                print("  %-40s %s or %s" % (r["file"][:40], r["asset"], r["runner_up"]))

    dup = {k: v for k, v in names.items() if len(v) > 1}
    if dup:
        print()
        print("one file matching several textures (PCSX2 needs unique filenames):")
        for k, v in list(dup.items())[:10]:
            print("  %-40s %s" % (k[:40], ", ".join(v)))

    if show_missing:
        miss = [x for x in assets if (x["pack"], x["name"]) not in seen]
        print()
        print("%d of %d game textures have nothing matching them in the pack:"
              % (len(miss), len(assets)))
        bypack = {}
        for x in miss:
            bypack.setdefault(x["pack"], []).append(x)
        for p in sorted(bypack):
            v = bypack[p]
            print("  %-22s %4d missing   e.g. %s"
                  % (p, len(v), ", ".join(x["name"] for x in v[:4])))

    if out:
        with open(out, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=["file", "asset", "pack", "width", "height",
                                              "scale", "error", "raw_error", "confidence",
                                              "runner_up"])
            w.writeheader()
            w.writerows(rows)
        print()
        print("wrote %s - one row per PCSX2 file, with the game texture it is" % out)


if __name__ == "__main__":
    main()
