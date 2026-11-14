"""
Name a PCSX2 texture pack exactly, by recomputing PCSX2's own filenames.

PCSX2 names a dumped or replacement texture
    <texture hash>-<palette hash>-<format bits>.png|dds
where the texture hash is XXH3-64 over the texture as it sits in the PS2's
video memory. Nothing in that depends on what the picture looks like once it
has been repainted, so a pack whose art was redrawn - which defeats matching
by appearance (hp2_match.py) - can still be named with certainty: build each
of the game's textures the way the GS stores it, hash it the same way, and
look the result up among the filenames.

How PCSX2 hashes (GSTextureCache.cpp, HashTextureLevel):
  * the texture is 2^TW x 2^TH pixels; TW, TH and the pixel format are in the
    filename's last field (format: bits 0-5, TW: bits 6-9, TH: bits 10-13)
  * video memory is made of 256-byte blocks: 16x16 pixels at 8 bits, 32x16 at
    4 bits, 8x8 at 32 bits, with the pixels inside a block in a fixed
    scrambled order (the column tables below, copied from GSTables.cpp)
  * a texture at least one block in each direction is hashed block by block,
    left to right and top to bottom, each block's bytes exactly as stored
  * a smaller one is hashed as plain rows, one byte per pixel when it has a
    palette and four when it does not
  * when the game supplies mip levels and PCSX2 is set to use them, the levels
    are fed into the same hash one after another
The palette hash is XXH3-64 over the palette as 16 or 256 RGBA words.

Every run of consecutive levels is tried for every texture - the top level
alone, all levels together, any lower level on its own or with those beneath
it - because which one PCSX2 saw depends on how the game drew it. A match is exact or absent;
there is no "close".

Usage:
    python hp2_hash.py extracted PCSX2_TEXTURE_DIR [-o map.csv]

Needs: pip install xxhash numpy
"""
import csv
import os
import sys

import numpy as np

try:
    import xxhash
except ImportError:
    raise SystemExit("this needs the xxhash package:  pip install xxhash")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hp2_tpk as T                                   # noqa: E402

# Position of each pixel inside its block, row-major, from PCSX2's GSTables.cpp.
# 8-bit: byte offset. 4-bit: nibble offset. 32-bit: word offset.
CT8 = np.array([
    0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54,
    8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62,
    33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23,
    41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31,
    96, 100, 112, 116, 64, 68, 80, 84, 98, 102, 114, 118, 66, 70, 82, 86,
    104, 108, 120, 124, 72, 76, 88, 92, 106, 110, 122, 126, 74, 78, 90, 94,
    65, 69, 81, 85, 97, 101, 113, 117, 67, 71, 83, 87, 99, 103, 115, 119,
    73, 77, 89, 93, 105, 109, 121, 125, 75, 79, 91, 95, 107, 111, 123, 127,
    128, 132, 144, 148, 160, 164, 176, 180, 130, 134, 146, 150, 162, 166, 178, 182,
    136, 140, 152, 156, 168, 172, 184, 188, 138, 142, 154, 158, 170, 174, 186, 190,
    161, 165, 177, 181, 129, 133, 145, 149, 163, 167, 179, 183, 131, 135, 147, 151,
    169, 173, 185, 189, 137, 141, 153, 157, 171, 175, 187, 191, 139, 143, 155, 159,
    224, 228, 240, 244, 192, 196, 208, 212, 226, 230, 242, 246, 194, 198, 210, 214,
    232, 236, 248, 252, 200, 204, 216, 220, 234, 238, 250, 254, 202, 206, 218, 222,
    193, 197, 209, 213, 225, 229, 241, 245, 195, 199, 211, 215, 227, 231, 243, 247,
    201, 205, 217, 221, 233, 237, 249, 253, 203, 207, 219, 223, 235, 239, 251, 255
], dtype=np.int64).reshape(16, 16)
CT4 = np.array([
    0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106,
    4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110,
    16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122,
    20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126,
    65, 73, 97, 105, 1, 9, 33, 41, 67, 75, 99, 107, 3, 11, 35, 43,
    69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47,
    81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59,
    85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63,
    192, 200, 224, 232, 128, 136, 160, 168, 194, 202, 226, 234, 130, 138, 162, 170,
    196, 204, 228, 236, 132, 140, 164, 172, 198, 206, 230, 238, 134, 142, 166, 174,
    208, 216, 240, 248, 144, 152, 176, 184, 210, 218, 242, 250, 146, 154, 178, 186,
    212, 220, 244, 252, 148, 156, 180, 188, 214, 222, 246, 254, 150, 158, 182, 190,
    129, 137, 161, 169, 193, 201, 225, 233, 131, 139, 163, 171, 195, 203, 227, 235,
    133, 141, 165, 173, 197, 205, 229, 237, 135, 143, 167, 175, 199, 207, 231, 239,
    145, 153, 177, 185, 209, 217, 241, 249, 147, 155, 179, 187, 211, 219, 243, 251,
    149, 157, 181, 189, 213, 221, 245, 253, 151, 159, 183, 191, 215, 223, 247, 255,
    256, 264, 288, 296, 320, 328, 352, 360, 258, 266, 290, 298, 322, 330, 354, 362,
    260, 268, 292, 300, 324, 332, 356, 364, 262, 270, 294, 302, 326, 334, 358, 366,
    272, 280, 304, 312, 336, 344, 368, 376, 274, 282, 306, 314, 338, 346, 370, 378,
    276, 284, 308, 316, 340, 348, 372, 380, 278, 286, 310, 318, 342, 350, 374, 382,
    321, 329, 353, 361, 257, 265, 289, 297, 323, 331, 355, 363, 259, 267, 291, 299,
    325, 333, 357, 365, 261, 269, 293, 301, 327, 335, 359, 367, 263, 271, 295, 303,
    337, 345, 369, 377, 273, 281, 305, 313, 339, 347, 371, 379, 275, 283, 307, 315,
    341, 349, 373, 381, 277, 285, 309, 317, 343, 351, 375, 383, 279, 287, 311, 319,
    448, 456, 480, 488, 384, 392, 416, 424, 450, 458, 482, 490, 386, 394, 418, 426,
    452, 460, 484, 492, 388, 396, 420, 428, 454, 462, 486, 494, 390, 398, 422, 430,
    464, 472, 496, 504, 400, 408, 432, 440, 466, 474, 498, 506, 402, 410, 434, 442,
    468, 476, 500, 508, 404, 412, 436, 444, 470, 478, 502, 510, 406, 414, 438, 446,
    385, 393, 417, 425, 449, 457, 481, 489, 387, 395, 419, 427, 451, 459, 483, 491,
    389, 397, 421, 429, 453, 461, 485, 493, 391, 399, 423, 431, 455, 463, 487, 495,
    401, 409, 433, 441, 465, 473, 497, 505, 403, 411, 435, 443, 467, 475, 499, 507,
    405, 413, 437, 445, 469, 477, 501, 509, 407, 415, 439, 447, 471, 479, 503, 511
], dtype=np.int64).reshape(16, 32)
CT32 = np.array([
    0, 1, 4, 5, 8, 9, 12, 13,
    2, 3, 6, 7, 10, 11, 14, 15,
    16, 17, 20, 21, 24, 25, 28, 29,
    18, 19, 22, 23, 26, 27, 30, 31,
    32, 33, 36, 37, 40, 41, 44, 45,
    34, 35, 38, 39, 42, 43, 46, 47,
    48, 49, 52, 53, 56, 57, 60, 61,
    50, 51, 54, 55, 58, 59, 62, 63
], dtype=np.int64).reshape(8, 8)

PSM = {0x00: 32, 0x13: 8, 0x14: 4}       # the three formats the game's packs use
IMAGES = (".png", ".dds", ".jpg", ".jpeg", ".bmp", ".tga", ".webp")


def is_pow2(n):
    return n >= 1 and n & (n - 1) == 0


def gs_bytes(px, w, h, bpp):
    """One level of a texture as the bytes PCSX2 feeds its hash.

    `px` is row-major: one palette index per pixel for 8 and 4 bits, raw RGBA
    bytes for 32.
    """
    if bpp == 8:
        if w < 16 or h < 16:
            return px.tobytes()
        y, x = np.mgrid[0:h, 0:w]
        blk = (y >> 4) * (w >> 4) + (x >> 4)
        out = np.zeros(w * h, dtype=np.uint8)
        out[(blk * 256 + CT8[y & 15, x & 15]).ravel()] = px
        return out.tobytes()
    if bpp == 4:
        if w < 32 or h < 16:
            return px.tobytes()                  # expanded to a byte per pixel
        y, x = np.mgrid[0:h, 0:w]
        blk = (y >> 4) * (w >> 5) + (x >> 5)
        n = (blk * 512 + CT4[y & 15, x & 31]).ravel()
        out = np.zeros(w * h // 2, dtype=np.uint8)
        lo = (n & 1) == 0
        out[n[lo] >> 1] = px[lo] & 15
        out[n[~lo] >> 1] |= (px[~lo] & 15) << 4
        return out.tobytes()
    words = px.reshape(h * w, 4)
    if w < 8 or h < 8:
        return words.tobytes()
    y, x = np.mgrid[0:h, 0:w]
    blk = (y >> 3) * (w >> 3) + (x >> 3)
    out = np.zeros((w * h, 4), dtype=np.uint8)
    out[(blk * 64 + CT32[y & 7, x & 7]).ravel()] = words
    return out.tobytes()


def levels(d, base, r):
    """[(w, h, row-major pixels)] for every stored level, plus bits per pixel
    and the palette as PCSX2 holds it. None if the format is not understood."""
    f = T.fmt_of(r)
    if not f:
        return None
    bpp, npal, fix8 = f
    w, h = r["w"], r["h"]
    po = base + r["pix_off"]
    if bpp == 32:
        raw = np.frombuffer(d[po: po + w * h * 4], dtype=np.uint8)
        return [(w, h, raw)], 32, b""
    out = []
    for lw, lh, at, sz in T.levels_of(w, h, r["pix_sz"], 1 if bpp == 8 else 2):
        raw = np.frombuffer(d[po + at: po + at + sz], dtype=np.uint8)
        idx = T.unswizzle_indices(raw, lw, lh, bpp)
        if idx is None:
            break
        out.append((lw, lh, idx))
    if not out:
        return None
    pal = np.frombuffer(d[base + r["pal_off"]: base + r["pal_off"] + npal * 4],
                        dtype=np.uint8).reshape(npal, 4)
    if fix8:                                     # stored order -> index order
        q = pal.copy()
        for b in range(0, 256, 32):
            q[b + 8: b + 16] = pal[b + 16: b + 24]
            q[b + 16: b + 24] = pal[b + 8: b + 16]
        pal = q
    return out, bpp, pal.tobytes()


def h64(*parts):
    st = xxhash.xxh3_64()
    for p in parts:
        st.update(p)
    return st.intdigest()


def game_hashes(root):
    """{texture hash: [(pack, name, w, h, bpp, palette hash, how)]}"""
    table, total, skipped = {}, 0, {}
    for n in sorted(os.listdir(root)):
        fp = os.path.join(root, n)
        if not os.path.isfile(fp):
            continue
        with open(fp, "rb") as f:
            if f.read(4) != b"\x01\x00\x30\xb0":
                continue
            d = b"\x01\x00\x30\xb0" + f.read()
        try:
            recs, base = T.parse_pack(d)
        except Exception:
            continue
        pack = os.path.splitext(n)[0]
        for r in recs:
            total += 1
            got = levels(d, base, r)
            if got is None:
                skipped["format not understood"] = skipped.get("format not understood", 0) + 1
                continue
            lv, bpp, pal = got
            if not all(is_pow2(w) and is_pow2(h) for w, h, _ in lv):
                # PCSX2 sees these through a clamp region, which changes the name
                skipped["not a power-of-two size"] = skipped.get("not a power-of-two size", 0) + 1
                continue
            ph = h64(pal) if pal else 0
            blobs = [gs_bytes(px, w, h, bpp) for w, h, px in lv]
            # every run of consecutive levels: PCSX2 hashes from whichever level
            # the game addressed, through however many it was told exist
            keys = []
            for i in range(len(blobs)):
                for j in range(i, len(blobs)):
                    if i == j:
                        how = "top level" if i == 0 else "level %d" % i
                    elif i == 0 and j == len(blobs) - 1:
                        how = "all levels"
                    else:
                        how = "levels %d-%d" % (i, j)
                    keys.append((h64(*blobs[i:j + 1]), lv[i][0], lv[i][1], how))
            for key, w, h, how in keys:
                table.setdefault(key, []).append((pack, r["name"], w, h, bpp, ph, how))
    return table, total, skipped


def parse_name(fn):
    """(texture hash, palette hash or None, format bits) from a PCSX2 filename."""
    stem = os.path.splitext(fn)[0]
    parts = stem.split("-")
    try:
        if len(parts) == 3:
            return int(parts[0], 16), int(parts[1], 16), int(parts[2], 16)
        if len(parts) == 2:
            return int(parts[0], 16), None, int(parts[1], 16)
    except ValueError:
        pass
    return None


def main():
    a = sys.argv[1:]
    out = None
    if "-o" in a:
        i = a.index("-o")
        out = a[i + 1]
        del a[i:i + 2]
    if len(a) != 2:
        print(__doc__)
        return
    root, texdir = a
    if not os.path.isdir(texdir):
        raise SystemExit("%s is not a folder" % texdir)

    print("hashing the game's textures the way PCSX2 does ...", flush=True)
    table, total, skipped = game_hashes(root)
    print("  %d textures, %d distinct hashes" % (total, len(table)))
    for why, n in sorted(skipped.items()):
        print("  %d left out: %s" % (n, why))

    files = []
    for dirpath, _d, fns in os.walk(texdir):
        for f in fns:
            if os.path.splitext(f)[1].lower() in IMAGES:
                files.append(os.path.join(dirpath, f))
    files.sort()
    print("%d image files under %s" % (len(files), texdir))
    if not table or not files:
        print("nothing to do")
        return

    rows, odd, miss = [], 0, []
    hows, pal_ok, pal_bad = {}, 0, 0
    for p in files:
        rel = os.path.relpath(p, texdir)
        nm = parse_name(os.path.basename(p))
        if nm is None:
            odd += 1
            continue
        th, ph, bits = nm
        fmt = PSM.get(bits & 63)
        tw, thh = 1 << ((bits >> 6) & 15), 1 << ((bits >> 10) & 15)
        hits = table.get(th)
        if not hits:
            miss.append((rel, fmt, tw, thh))
            rows.append(dict(file=rel, asset="", also="", packs=0, width=tw, height=thh,
                             bits=fmt or "", hashed="", size_agrees="", palette=""))
            continue
        # the filename also states the size and format; a hit that disagrees
        # with them would be a coincidence and is reported as such
        good = [x for x in hits if x[2] == tw and x[3] == thh and x[4] == fmt]
        use = good or hits
        # several textures can share their pixels and differ only in palette;
        # the palette hash in the filename says which one this file is
        if ph is not None:
            exact = [x for x in use if x[5] == ph]
            use = exact or use
        names = []
        for x in use:
            if x[1] not in names:
                names.append(x[1])
        palmatch = ph is None or any(x[5] == ph for x in use)
        if ph is not None:
            pal_ok += palmatch
            pal_bad += not palmatch
        hows[use[0][6]] = hows.get(use[0][6], 0) + 1
        rows.append(dict(file=rel, asset=names[0], also=" ".join(names[1:]),
                         packs=len({x[0] for x in use}), width=use[0][2], height=use[0][3],
                         bits=use[0][4], hashed=use[0][6],
                         size_agrees="yes" if good else "NO",
                         palette="same" if palmatch else "different"))

    named = [r for r in rows if r["asset"]]
    print()
    print("%d of %d files named exactly" % (len(named), len(files) - odd))
    for how, n in sorted(hows.items(), key=lambda kv: -kv[1]):
        print("  %5d by hashing %s" % (n, how))
    if named:
        print("  palette hash also reproduced for %d, differs for %d" % (pal_ok, pal_bad))
        bad = sum(1 for r in named if r["size_agrees"] == "NO")
        if bad:
            print("  %d hits disagree with the size in their filename - treat as suspect" % bad)
    if odd:
        print("%d files have names in another form (region textures) and were not tried" % odd)
    if miss:
        print("%d files match no texture hash:" % len(miss))
        kinds = {}
        for _rel, fmt, tw, thh in miss:
            k = "%s-bit %dx%d" % (fmt if fmt else "?", tw, thh)
            kinds[k] = kinds.get(k, 0) + 1
        for k, n in sorted(kinds.items(), key=lambda kv: -kv[1])[:12]:
            print("  %5d  %s" % (n, k))
    for r in named[:20]:
        print("  %-44s -> %-24s %4dx%-4d %s" % (r["file"][-44:], r["asset"], r["width"],
                                                 r["height"], r["hashed"]))
    if len(named) > 20:
        print("  ... and %d more" % (len(named) - 20))
    if not named:
        print()
        print("No filename was reproduced, so one of the assumptions in this file's\n"
              "header is wrong for this game. The image-based hp2_match.py is unaffected.")

    if out:
        with open(out, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=["file", "asset", "also", "packs", "width", "height",
                                              "bits", "hashed", "size_agrees", "palette"])
            w.writeheader()
            w.writerows(rows)
        print()
        print("wrote %s - unnamed files are listed too, with the asset left blank" % out)


if __name__ == "__main__":
    main()
