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
pack in it is read. PCSX2_TEXTURE_DIR is searched recursively for PNG and DDS files, so
point it at the game's folder and both dumps and replacements are picked up.

--missing also lists the game textures that no file in the pack matches, which
is the work left to do.

Needs: pip install pillow numpy
"""
import csv
import hashlib
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
    assets, seen, total = [], {}, 0
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
            pack = os.path.splitext(n)[0]
            total += 1
            # The same picture is stored once per track pack, and sometimes
            # under two names. Those are one candidate, not rivals: a file that
            # matches it matches all of them.
            key = (r["w"], r["h"], hashlib.sha1(rgb.tobytes()).digest())
            a = seen.get(key)
            if a is None:
                a = dict(pack=pack, name=r["name"], w=r["w"], h=r["h"], rgb=rgb,
                         fp=fingerprint(rgb), names=[], packs=[], uses=[])
                seen[key] = a
                assets.append(a)
            if r["name"] not in a["names"]:
                a["names"].append(r["name"])
            if pack not in a["packs"]:
                a["packs"].append(pack)
            a["uses"].append((pack, r["name"]))
    return assets, total


IMAGES = (".png", ".dds", ".jpg", ".jpeg", ".bmp", ".tga", ".webp")


def find_images(root):
    """Every image file under `root`, sorted, with a count per extension."""
    if not os.path.isdir(root):
        raise SystemExit("%s is not a folder. PCSX2 shows the right path under\n"
                         "Settings -> Graphics -> Texture Replacements; a Flatpak install\n"
                         "keeps it under ~/.var/app/net.pcsx2.PCSX2/config/PCSX2/textures."
                         % root)
    paths, kinds = [], {}
    for dirpath, _dirs, files in os.walk(root):
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext in IMAGES:
                paths.append(os.path.join(dirpath, f))
                kinds[ext] = kinds.get(ext, 0) + 1
    paths.sort()
    return paths, kinds


def read_images(paths, failed):
    """Decode one file at a time. An enhanced pack can run to gigabytes once
    decoded, so nothing is kept after it has been matched."""
    for p in paths:
        try:
            with Image.open(p) as im:
                rgb = np.array(im.convert("RGB"), dtype=np.uint8)
        except Exception as e:
            ext = os.path.splitext(p)[1].lower()
            failed.setdefault(ext, [0, str(e)])[0] += 1
            continue
        yield p, rgb


ROOT = ["."]


def relation(w, h, aw, ah):
    """How a file's size stands to the game texture it matched."""
    if (w, h) == (aw, ah):
        return "same size"
    if w * ah == h * aw:
        if w > aw:
            return "upscaled %gx" % (w / aw)
        return "smaller 1/%g" % (aw / w)
    return "other shape"


def compare(rgb, a):
    """Fitted and raw error between a file and one game texture, measured at
    whichever of the two sizes is smaller so neither side is invented."""
    h, w = rgb.shape[:2]
    if w * h < a["w"] * a["h"]:
        x = rgb.astype(np.float64)
        ref = box_to(a["rgb"], w, h)
    else:
        x = box_to(rgb, a["w"], a["h"])
        ref = a["rgb"].astype(np.float64)
    return fitted_error(x, ref), float(np.abs(x - ref).mean())


def match(assets, images, progress=None):
    F = np.array([a["fp"] for a in assets])
    rows, unmatched = [], []
    seen = set()
    for n, (path, rgb) in enumerate(images, 1):
        if progress and n % 50 == 0:
            progress(n)
        fp = fingerprint(rgb)
        d = np.abs(F - fp).mean(1)
        order = np.argsort(d)[:8]
        h, w = rgb.shape[:2]
        scored = []
        for i in order:
            a = assets[i]
            try:
                err, raw = compare(rgb, a)
            except Exception:
                continue
            # a candidate of a different shape is a worse answer than one of
            # the same shape that fits equally well
            shape = 0 if w * a["h"] == h * a["w"] else 1
            scored.append((shape, err, raw, i))
        if not scored:
            unmatched.append((path, None, None))
            continue
        scored.sort()
        shape, err, raw, i = scored[0]
        a = assets[i]
        rivals = [s for s in scored[1:] if s[0] == shape]
        runner = rivals[0][1] if rivals else float("inf")
        if err > MATCH_OK:
            unmatched.append((path, a["name"], err))
            continue
        rel = relation(w, h, a["w"], a["h"])
        kind = "exact" if raw == 0 and rel == "same size" else \
               ("close" if err < SURE else "likely")
        flat = float(rgb.reshape(-1, 3).std(0).max()) < 2.0
        # an additive term as well as the ratio, or two pictures that both
        # score zero tie without it being noticed
        if flat or runner < err * AMBIGUOUS + 0.5:
            kind = "ambiguous"
        rows.append(dict(file=os.path.relpath(path, ROOT[0]), asset=a["name"],
                         also=" ".join(a["names"][1:]), packs=len(a["packs"]),
                         width=a["w"], height=a["h"], file_width=w, file_height=h,
                         relation=rel, error="%.2f" % err, raw_error="%.2f" % raw,
                         confidence=kind,
                         runner_up="(plain colour)" if flat else
                                   (assets[rivals[0][3]]["name"] if rivals else "")))
        seen.update(a["uses"])
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
    assets, total = load_assets(root)
    print("  %d textures in %d packs, %d different pictures"
          % (total, len({p for x in assets for p in x["packs"]}), len(assets)))
    paths, kinds = find_images(texdir)
    print("%d image files in %s (%s)"
          % (len(paths), texdir,
             ", ".join("%d %s" % (n, e) for e, n in sorted(kinds.items())) or "none"))
    if not assets or not paths:
        print("nothing to match")
        return
    ROOT[0] = texdir
    tty = sys.stdout.isatty()
    failed = {}
    rows, unmatched, seen = match(
        assets, read_images(paths, failed),
        (lambda n: print("\r  matched %d of %d" % (n, len(paths)), end="", flush=True))
        if tty else None)
    if tty:
        print("\r" + " " * 40 + "\r", end="")
    for ext, (n, why) in sorted(failed.items()):
        print("could not read %d %s files: %s" % (n, ext, why))
        if ext == ".dds":
            print("  pip install --upgrade pillow  (BC7 needs a recent Pillow), or convert"
                  " with  mogrify -format png *.dds")
    names = {}
    for r in rows:
        names.setdefault(r["file"], []).append(r["asset"])

    exact = sum(1 for r in rows if r["confidence"] == "exact")
    close = sum(1 for r in rows if r["confidence"] == "close")
    regr = sum(1 for r in rows if r["confidence"] == "likely")
    amb = sum(1 for r in rows if r["confidence"] == "ambiguous")
    print()
    print("%d matched: %d exact, %d close, %d likely, %d ambiguous"
          % (len(rows), exact, close, regr, amb))
    print("%d files matched nothing in the game's own textures" % len(unmatched))

    up = sum(1 for r in rows if r["relation"].startswith("upscaled"))
    same = sum(1 for r in rows if r["relation"] == "same size")
    small = sum(1 for r in rows if r["relation"].startswith("smaller"))
    print("by size: %d larger than the disc texture, %d the same size, %d smaller"
          % (up, same, small))
    if small:
        print("  smaller files are the reduced copies (mip levels) PCSX2 dumps"
              " alongside each texture")
    if exact and not up:
        print("  nothing here is larger than the disc version and %d are pixel-identical"
              " to it:\n  this looks like a dump of the game's own textures, not an"
              " enhanced pack" % exact)

    for r in rows[:25]:
        print("  %-40s -> %-24s %4dx%-4d %-12s err %-6s %s"
              % (r["file"][-40:], r["asset"], r["width"], r["height"],
                 r["relation"], r["error"], r["confidence"]))
    if len(rows) > 25:
        print("  ... and %d more" % (len(rows) - 25))

    if unmatched:
        print()
        print("not matched - nearest candidate and its error, for you to judge:")
        for path, nm, e in unmatched[:15]:
            print("  %-40s %s" % (os.path.relpath(path, texdir)[-40:],
                                  "nearest %s at %.1f" % (nm, e) if nm else "no candidate"))
        if len(unmatched) > 15:
            print("  ... and %d more" % (len(unmatched) - 15))

    if amb:
        print()
        print("ambiguous ones, where a second texture fits nearly as well:")
        for r in rows:
            if r["confidence"] == "ambiguous":
                print("  %-40s %s or %s" % (r["file"][-40:], r["asset"], r["runner_up"]))

    dup = {k: v for k, v in names.items() if len(v) > 1}
    if dup:
        print()
        print("one file matching several textures (PCSX2 needs unique filenames):")
        for k, v in list(dup.items())[:10]:
            print("  %-40s %s" % (k[-40:], ", ".join(v)))

    if show_missing:
        miss = [u for x in assets for u in x["uses"] if u not in seen]
        print()
        print("%d of %d game textures have nothing matching them in the pack:"
              % (len(miss), total))
        bypack = {}
        for pk, nm in miss:
            bypack.setdefault(pk, []).append(nm)
        for p in sorted(bypack):
            v = bypack[p]
            print("  %-22s %4d missing   e.g. %s" % (p, len(v), ", ".join(v[:4])))

    if out:
        with open(out, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=["file", "asset", "also", "packs", "width",
                                              "height", "file_width", "file_height",
                                              "relation", "error", "raw_error",
                                              "confidence", "runner_up"])
            w.writeheader()
            w.writerows(rows)
        print()
        print("wrote %s - one row per PCSX2 file, with the game texture it is" % out)


if __name__ == "__main__":
    main()
