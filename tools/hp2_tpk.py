"""
Export textures from NFS Hot Pursuit 2 (PS2) texture packs (.tpk) to PNG.

Container: little-endian chunks, each  u32 id, u32 size, payload.
  0xB0300001  texture pack (container)
    0x30300002  pack header (84 bytes: pack name, path)
    0x30300003  texture index: N records of 164 bytes
    0x30300004  texture data: padding (0x11 bytes), palettes, pixels
  0xB0300100  trailer (container), then a 0x00000000 padding chunk

Index record (164 bytes), offsets within the record:
  +8   name (24 bytes, NUL padded)     +32 hash (u32)
  +36  width (u16)   +38 height (u16)
  +48  pixel offset (u32)  +52 palette offset (u32)
  +56  pixel bytes  (u32)  +60 palette bytes  (u32)
Offsets are relative to the first palette in the data chunk. The pixel data
of a texture may be followed by smaller mipmap levels; only level 0 is exported.

Pixel layouts: textures that fill whole GS memory pages are stored "swizzled" as a
plain raster of 32-bit words which the graphics chip reads back as 8-bit or 4-bit
pixels (8-bit: width multiple of 128 and height multiple of 64; 4-bit: both multiples
of 128; addressing follows the GS memory model, tables from GSdx). Every other
texture is stored linearly: rows of pixels, and for 4-bit the low nibble of each byte
is the left pixel.

Handled:
  8 bits per pixel: 256-colour palette (entries 8-15 / 16-23 swapped in each 32)
  4 bits per pixel: 16-colour palette
  32 bits per pixel: no palette, plain RGBA
PS2 alpha 0..0x80 is scaled to 0..255.
Not handled (listed and skipped): other palette sizes.

Usage:  python hp2_tpk.py [extracted_dir] [output_dir]
Needs:  pip install pillow numpy
"""
import hashlib
import os
import struct
import sys

import numpy as np
from PIL import Image

REC = 164

# ---- GS memory tables (from GSdx GSTables.cpp) --------------------------------
BT32 = np.array([[0, 1, 4, 5, 16, 17, 20, 21], [2, 3, 6, 7, 18, 19, 22, 23],
                 [8, 9, 12, 13, 24, 25, 28, 29], [10, 11, 14, 15, 26, 27, 30, 31]])
CT32 = np.array([[0, 1, 4, 5, 8, 9, 12, 13], [2, 3, 6, 7, 10, 11, 14, 15],
                 [16, 17, 20, 21, 24, 25, 28, 29], [18, 19, 22, 23, 26, 27, 30, 31],
                 [32, 33, 36, 37, 40, 41, 44, 45], [34, 35, 38, 39, 42, 43, 46, 47],
                 [48, 49, 52, 53, 56, 57, 60, 61], [50, 51, 54, 55, 58, 59, 62, 63]])
BT4 = np.array([[0, 2, 8, 10], [1, 3, 9, 11], [4, 6, 12, 14], [5, 7, 13, 15],
                [16, 18, 24, 26], [17, 19, 25, 27], [20, 22, 28, 30], [21, 23, 29, 31]])
_R4 = [[0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106,
        4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110],
       [16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122,
        20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126]]


def _build_ct4():
    t = np.zeros((16, 32), dtype=np.int64)
    for y in range(16):
        for x in range(32):
            v = _R4[y & 1][x]
            if ((y + 2) >> 2) & 1:
                v = (v + 64) & 127
            t[y][x] = 128 * (y >> 2) + v + ((y >> 1) & 1)
    return t


CT4 = _build_ct4()
# inverse tables: block number -> (row, col), column word -> (row, col)
INV_BY = np.zeros(32, dtype=np.int64)
INV_BX = np.zeros(32, dtype=np.int64)
for _r in range(4):
    for _c in range(8):
        INV_BY[BT32[_r][_c]], INV_BX[BT32[_r][_c]] = _r, _c
INV_CY = np.zeros(64, dtype=np.int64)
INV_CX = np.zeros(64, dtype=np.int64)
for _r in range(8):
    for _c in range(8):
        INV_CY[CT32[_r][_c]], INV_CX[CT32[_r][_c]] = _r, _c

_cache = {}


def positions8(w, h):
    """Byte offset in the stored data of every pixel (row-major) for 8-bit."""
    key = (8, w, h)
    if key not in _cache:
        y, x = np.mgrid[0:h, 0:w].astype(np.int64)
        block = (y & ~0xF) * w + (x & ~0xF) * 2
        swap = (((y + 2) >> 2) & 1) * 4
        posy = (((y & ~3) >> 1) + (y & 1)) & 7
        col = posy * w * 2 + ((x + swap) & 7) * 4
        bn = ((y >> 1) & 1) + ((x >> 2) & 2)
        _cache[key] = (block + col + bn).ravel()
    return _cache[key]


def positions4(w, h):
    """(byte offset, odd-nibble flag) of every pixel (row-major) for 4-bit."""
    key = (4, w, h)
    if key not in _cache:
        y, x = np.mgrid[0:h, 0:w].astype(np.int64)
        pw = max(1, (w + 127) // 128)
        page = (y >> 7) * pw + (x >> 7)
        blk = BT4[(y >> 4) & 7, (x >> 5) & 3]
        n = page * 16384 + blk * 512 + CT4[y & 15, x & 31]
        word = n >> 3
        pg, wip = word // 2048, word % 2048
        b, c = wip // 64, wip % 64
        wx = (pg % pw) * 64 + INV_BX[b] * 8 + INV_CX[c]
        wy = (pg // pw) * 32 + INV_BY[b] * 8 + INV_CY[c]
        # The stored data is a raster of the rectangle of 32-bit words that the texture
        # occupies in GS memory. For whole pages that is w/2 x h/4; for small textures
        # it is the bounding rectangle of the blocks actually used.
        rw, rh = int(wx.max()) + 1, int(wy.max()) + 1
        if rw * rh != w * h // 8:      # rectangle not completely filled: not understood yet
            _cache[key] = None
        else:
            pos = (wy * rw + wx) * 4 + ((n & 7) >> 1)
            _cache[key] = (pos.ravel(), (n & 1).ravel())
    return _cache[key]


def chunks(d, start, end):
    pos = start
    while pos + 8 <= end:
        cid, sz = struct.unpack_from("<II", d, pos)
        if pos + 8 + sz > end:
            return
        yield cid, pos + 8, sz
        pos += 8 + sz


def mip_levels(w, h, pix_sz, div):
    """Number of mip levels (1 = no mipmaps) if pix_sz is exactly the size of
    w x h plus its successively halved levels, else 0. div=1 for 8-bit, 2 for 4-bit."""
    total = 0
    for k in range(8):
        lw, lh = w >> k, h >> k
        if lw < 8 or lh < 8:
            break
        total += lw * lh // div
        if total == pix_sz:
            return k + 1
    return 0


def parse_pack(d):
    index = data = None
    for cid, off, sz in chunks(d, 0, len(d)):
        if cid == 0xB0300001:
            for cid2, off2, sz2 in chunks(d, off, off + sz):
                if cid2 == 0x30300003:
                    index = (off2, sz2)
                elif cid2 == 0x30300004:
                    data = (off2, sz2)
    if not index or not data:
        raise ValueError("no texture index/data chunk")
    ioff, isz = index
    doff, dsz = data
    recs = []
    for i in range(isz // REC):
        b = d[ioff + i * REC: ioff + (i + 1) * REC]
        name = b[8:32].split(b"\0")[0].decode("ascii", "replace")
        w, h = struct.unpack_from("<HH", b, 36)
        pix_off, pal_off, pix_sz, pal_sz = struct.unpack_from("<IIII", b, 48)
        recs.append(dict(name=name, w=w, h=h, pix_off=pix_off, pal_off=pal_off,
                         pix_sz=pix_sz, pal_sz=pal_sz))
    total = max((r["pix_off"] + r["pix_sz"] for r in recs), default=0)
    pad = dsz - total
    if pad < 0:
        raise ValueError("data chunk smaller than index claims")
    return recs, doff + pad


def make_palette(raw, n, fix8):
    p = np.frombuffer(raw, dtype=np.uint8).reshape(n, 4).copy()
    p[:, 3] = np.minimum(255, p[:, 3].astype(np.int32) * 2).astype(np.uint8)
    if fix8:
        q = p.copy()
        for blk in range(0, 256, 32):
            q[blk + 8: blk + 16] = p[blk + 16: blk + 24]
            q[blk + 16: blk + 24] = p[blk + 8: blk + 16]
        p = q
    return p


def is_swizzled(w, h, bpp):
    """Textures that fill whole GS memory pages are stored pre-swizzled; every
    smaller or odd-sized texture is stored as plain rows of pixels."""
    if bpp == 8:
        return w % 128 == 0 and h % 64 == 0
    if bpp == 4:
        return w % 128 == 0 and h % 128 == 0
    return False


def decode(d, base, r):
    """Return an RGBA numpy image (h, w, 4) or a string explaining why not."""
    w, h = r["w"], r["h"]
    if w <= 0 or h <= 0:
        return "empty"
    po = base + r["pix_off"]
    if r["pal_sz"] == 1024 and mip_levels(w, h, r["pix_sz"], 1):
        nbytes = w * h
        raw = np.frombuffer(d[po: po + nbytes], dtype=np.uint8)
        if raw.size < nbytes:
            return "other"
        idx = raw[positions8(w, h)] if is_swizzled(w, h, 8) else raw
        pal = make_palette(d[base + r["pal_off"]: base + r["pal_off"] + 1024], 256, True)
        return pal[idx].reshape(h, w, 4)
    if r["pal_sz"] == 64 and w % 2 == 0 and mip_levels(w, h, r["pix_sz"], 2):
        nbytes = w * h // 2
        raw = np.frombuffer(d[po: po + nbytes], dtype=np.uint8)
        if raw.size < nbytes:
            return "other"
        if is_swizzled(w, h, 4):
            lay = positions4(w, h)
            if lay is None:
                return "4-bit-unverified"
            pos, odd = lay
            b = raw[pos]
            idx = np.where(odd == 1, b >> 4, b & 15)
        else:                                  # linear: low nibble is the left pixel
            idx = np.empty(w * h, dtype=np.uint8)
            idx[0::2] = raw & 15
            idx[1::2] = raw >> 4
        pal = make_palette(d[base + r["pal_off"]: base + r["pal_off"] + 64], 16, False)
        return pal[idx].reshape(h, w, 4)
    if r["pal_sz"] == 0 and r["pix_sz"] == w * h * 4:
        raw = np.frombuffer(d[po: po + w * h * 4], dtype=np.uint8)
        if raw.size < w * h * 4:
            return "other"
        img = raw.reshape(h, w, 4).copy()
        img[:, :, 3] = np.minimum(255, img[:, :, 3].astype(np.int32) * 2).astype(np.uint8)
        return img
    return "other"


def export(path, outdir):
    d = open(path, "rb").read()
    recs, base = parse_pack(d)
    done = skipped = 0
    for r in recs:
        res = decode(d, base, r)
        if isinstance(res, str):
            skipped += 1
            print("   skipped %-28s %dx%d (%s, pixels %d bytes, palette %d bytes)" %
                  (r["name"], r["w"], r["h"], res, r["pix_sz"], r["pal_sz"]))
            continue
        safe = "".join(c if c.isalnum() or c in "-_." else "_" for c in r["name"])
        Image.fromarray(res, "RGBA").save(os.path.join(outdir, safe + ".png"))
        done += 1
    return done, skipped, len(recs)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "extracted"
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(root.rstrip("\\/")), "textures")
    seen = {}
    total_done = total_skip = 0
    for n in sorted(os.listdir(root)):
        fp = os.path.join(root, n)
        if not os.path.isfile(fp):
            continue
        with open(fp, "rb") as f:
            if f.read(4) != b"\x01\x00\x30\xb0":
                continue
        h = hashlib.md5(open(fp, "rb").read()).hexdigest()
        if h in seen:
            print("%s: identical to %s, skipped" % (n, seen[h]))
            continue
        seen[h] = n
        od = os.path.join(out, os.path.splitext(n)[0])
        os.makedirs(od, exist_ok=True)
        try:
            done, skipped, count = export(fp, od)
        except Exception as e:
            print("%s: cannot parse (%s)" % (n, e))
            continue
        total_done += done
        total_skip += skipped
        print("%s: %d textures, %d exported, %d skipped" % (n, count, done, skipped))
    print()
    print("exported:", total_done, " skipped:", total_skip)
    print("output folder:", out)


if __name__ == "__main__":
    main()
