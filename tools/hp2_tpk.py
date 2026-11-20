"""
Export textures from NFS Hot Pursuit 2 (PS2) texture packs (.tpk) to PNG.

Container: little-endian chunks, each  u32 id, u32 size, payload.
  0xB0300001  texture pack (container)
    0x30300002  pack header (84 bytes: pack name, path)
    0x30300003  texture index: N records of 164 bytes
    0x30300004  texture data: padding (0x11 bytes), palettes, pixels. Offsets
              count from the end of the padding; some packs (cars, front end)
              have spare bytes after the last pixel, so the end of the chunk
              is not a safe reference.
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

Usage:
    python hp2_tpk.py [extracted_dir] [output_dir]     export every pack to PNG
    python hp2_tpk.py list PACK.tpk                    what is in one pack
    python hp2_tpk.py verify PACK.tpk | DIR            re-pack and demand the same bytes
    python hp2_tpk.py write PACK NAME NEW.png OUT.tpk  put a PNG into one slot

A replacement must have exactly the dimensions of the slot it goes into. Sizes
never change: the slot is fixed and the graphics chip has 4 MB of memory, so a
texture can be repainted but not enlarged. The palette is rebuilt from the new
image unless it is shared with another texture, or --keep-palette is given; all
mip levels are regenerated.

Needs:  pip install pillow numpy
"""
import hashlib
import os
import re
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


def unswizzle_indices(raw, w, h, bpp):
    """Stored pixel bytes -> one palette index per pixel, row-major."""
    if bpp == 8:
        return raw[positions8(w, h)] if is_swizzled(w, h, 8) else raw
    if is_swizzled(w, h, 4):
        lay = positions4(w, h)
        if lay is None:
            return None
        pos, odd = lay
        b = raw[pos]
        return np.where(odd == 1, b >> 4, b & 15).astype(np.uint8)
    idx = np.empty(w * h, dtype=np.uint8)       # linear: low nibble is the left pixel
    idx[0::2] = raw & 15
    idx[1::2] = raw >> 4
    return idx


def swizzle_indices(idx, w, h, bpp):
    """The exact inverse of unswizzle_indices: indices -> stored pixel bytes.

    Every layout here is a permutation of the stored bytes, so the inverse is
    the same table used as a scatter rather than a gather.
    """
    idx = np.ascontiguousarray(idx, dtype=np.uint8).ravel()
    if bpp == 8:
        if not is_swizzled(w, h, 8):
            return idx.tobytes()
        raw = np.zeros(w * h, dtype=np.uint8)
        raw[positions8(w, h)] = idx
        return raw.tobytes()
    if is_swizzled(w, h, 4):
        lay = positions4(w, h)
        if lay is None:
            return None
        pos, odd = lay
        raw = np.zeros(w * h // 2, dtype=np.uint8)
        lo, hi = odd == 0, odd == 1
        raw[pos[lo]] = idx[lo] & 15
        raw[pos[hi]] |= (idx[hi] & 15) << 4
        return raw.tobytes()
    raw = (idx[0::2] & 15) | ((idx[1::2] & 15) << 4)
    return raw.astype(np.uint8).tobytes()


def levels_of(w, h, pix_sz, div):
    """[(level_width, level_height, byte_offset, byte_size)] for every mip level."""
    n = mip_levels(w, h, pix_sz, div)
    out, at = [], 0
    for k in range(n):
        lw, lh = w >> k, h >> k
        sz = lw * lh // div
        out.append((lw, lh, at, sz))
        at += sz
    return out


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
        recs.append(dict(name=name, hash=struct.unpack_from("<I", b, 32)[0],
                         w=w, h=h, pix_off=pix_off, pal_off=pal_off,
                         pix_sz=pix_sz, pal_sz=pal_sz))
    total = max((r["pix_off"] + r["pix_sz"] for r in recs), default=0)
    if dsz < total:
        raise ValueError("data chunk smaller than index claims")
    # Offsets count from the end of the 0x11 filler that opens the data
    # chunk. (An earlier rule, "the data ends exactly at the last pixel",
    # holds for the track packs but not for the car and front-end packs,
    # which carry 128 or 1024 bytes after it; that put every read there late
    # and read the car palettes out of pixel data.)
    fill = 0
    while fill < dsz and d[doff + fill] == 0x11:
        fill += 1
    if fill > dsz - total:
        fill = dsz - total
    return recs, doff + fill


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


def pack_palette(p, fix8):
    """The inverse of make_palette: RGBA entries -> stored palette bytes.

    PS2 alpha runs 0..0x80, which export doubles to 0..255, so 255 came from
    0x80 and every other exported value halves exactly. A hand-edited PNG can
    hold odd values, which round.
    """
    q = np.ascontiguousarray(p, dtype=np.uint8).copy()
    if fix8:
        s = q.copy()
        for blk in range(0, 256, 32):
            s[blk + 8: blk + 16] = q[blk + 16: blk + 24]
            s[blk + 16: blk + 24] = q[blk + 8: blk + 16]
        q = s
    a = q[:, 3].astype(np.int32)
    q[:, 3] = np.where(a >= 255, 128, (a + 1) // 2).astype(np.uint8)
    return q.tobytes()


def pack_alpha32(img):
    """Same alpha convention for 32-bit textures, which carry no palette."""
    out = np.ascontiguousarray(img, dtype=np.uint8).copy()
    a = out[:, :, 3].astype(np.int32)
    out[:, :, 3] = np.where(a >= 255, 128, (a + 1) // 2).astype(np.uint8)
    return out.tobytes()


def is_swizzled(w, h, bpp):
    """Textures that fill whole GS memory pages are stored pre-swizzled; every
    smaller or odd-sized texture is stored as plain rows of pixels."""
    if bpp == 8:
        return w % 128 == 0 and h % 64 == 0
    if bpp == 4:
        # 3 page rows (GRID, 512 x 384) is stored linear; 1, 2, 4... are swizzled
        return w % 128 == 0 and h % 128 == 0 and (h == 128 or h % 256 == 0)
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
        idx = unswizzle_indices(raw, w, h, 8)
        pal = make_palette(d[base + r["pal_off"]: base + r["pal_off"] + 1024], 256, True)
        return pal[idx].reshape(h, w, 4)
    if r["pal_sz"] == 64 and w % 2 == 0 and mip_levels(w, h, r["pix_sz"], 2):
        nbytes = w * h // 2
        raw = np.frombuffer(d[po: po + nbytes], dtype=np.uint8)
        if raw.size < nbytes:
            return "other"
        idx = unswizzle_indices(raw, w, h, 4)
        if idx is None:
            return "4-bit-unverified"
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


def fmt_of(r):
    """(bits per pixel, palette entries, whether the 8-bit swap applies) or None."""
    w, h = r["w"], r["h"]
    if w <= 0 or h <= 0:
        return None
    if r["pal_sz"] == 1024 and mip_levels(w, h, r["pix_sz"], 1):
        return 8, 256, True
    if r["pal_sz"] == 64 and w % 2 == 0 and mip_levels(w, h, r["pix_sz"], 2):
        return 4, 16, False
    if r["pal_sz"] == 0 and r["pix_sz"] == w * h * 4:
        return 32, 0, False
    return None


def verify(path):
    """Re-pack every texture exactly as shipped and insist on the same bytes.

    This is the test that the import path is a true inverse, and it avoids the
    one thing a picture cannot settle: a palette with two identical colours
    makes the index of a pixel ambiguous, so going through RGBA could never be
    byte-exact. Indices and palettes are therefore checked separately, each
    against the bytes actually on the disc, for every mip level.
    """
    d = open(path, "rb").read()
    recs, base = parse_pack(d)
    ok = bad = skip = 0
    notes = []
    for r in recs:
        f = fmt_of(r)
        if f is None:
            skip += 1
            continue
        bpp, nent, fix8 = f
        w, h = r["w"], r["h"]
        good = True

        if bpp == 32:
            raw = d[base + r["pix_off"]: base + r["pix_off"] + w * h * 4]
            img = np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 4).copy()
            img[:, :, 3] = np.minimum(255, img[:, :, 3].astype(np.int32) * 2)
            if pack_alpha32(img) != raw:
                good, why = False, "32-bit alpha"
        else:
            div = 1 if bpp == 8 else 2
            for lw, lh, at, sz in levels_of(w, h, r["pix_sz"], div):
                po = base + r["pix_off"] + at
                raw = np.frombuffer(d[po: po + sz], dtype=np.uint8)
                if raw.size < sz:
                    good, why = False, "truncated"
                    break
                idx = unswizzle_indices(raw, lw, lh, bpp)
                if idx is None:
                    skip += 1
                    good = None            # layout not understood; not a failure
                    break
                back = swizzle_indices(idx, lw, lh, bpp)
                if back != raw.tobytes():
                    good, why = False, "%dx%d level" % (lw, lh)
                    break
            if good:
                praw = d[base + r["pal_off"]: base + r["pal_off"] + r["pal_sz"]]
                if pack_palette(make_palette(praw, nent, fix8), fix8) != praw:
                    good, why = False, "palette"
                    hi = max(praw[3::4])
                    if hi > 0x80:
                        notes.append("%s: palette alpha reaches 0x%02X, above the "
                                     "0x80 the format is meant to use" % (r["name"], hi))
        if good is None:
            continue
        if good:
            ok += 1
        else:
            bad += 1
            if len(notes) < 12:
                notes.append("%s %dx%d %d-bit: %s does not round-trip"
                             % (r["name"], w, h, bpp, why))
    return ok, bad, skip, notes


def median_cut(cols, counts, n):
    """A palette of at most n RGBA entries, splitting the widest box each time."""
    boxes = [np.arange(len(cols))]
    while len(boxes) < n:
        bi, best = None, -1.0
        for i, b in enumerate(boxes):
            if len(b) < 2:
                continue
            ext = cols[b].max(0).astype(np.int64) - cols[b].min(0).astype(np.int64)
            score = float(ext.max()) * float(counts[b].sum())
            if score > best:
                bi, best = i, score
        if bi is None:
            break
        b = boxes.pop(bi)
        ext = cols[b].max(0).astype(np.int64) - cols[b].min(0).astype(np.int64)
        order = b[np.argsort(cols[b][:, int(np.argmax(ext))], kind="stable")]
        w = counts[order].cumsum()
        half = int(np.searchsorted(w, w[-1] / 2.0)) + 1
        half = min(max(half, 1), len(order) - 1)
        boxes += [order[:half], order[half:]]
    pal = []
    for b in boxes:
        wt = counts[b].astype(np.float64)
        pal.append((cols[b].astype(np.float64) * wt[:, None]).sum(0) / wt.sum())
    return np.array(pal).round().clip(0, 255).astype(np.uint8)


def build_palette(img, n):
    """A palette for this image. Fewer than n distinct colours is kept exactly,
    which makes re-importing an unmodified export lossless."""
    flat = img.reshape(-1, 4)
    cols, counts = np.unique(flat, axis=0, return_counts=True)
    if len(cols) <= n:
        pal = np.zeros((n, 4), dtype=np.uint8)
        pal[:len(cols)] = cols
        return pal, len(cols)
    pal = median_cut(cols, counts, n)
    out = np.zeros((n, 4), dtype=np.uint8)
    out[:len(pal)] = pal
    return out, len(pal)


def to_indices(img, pal, used):
    """Nearest palette entry for every pixel, alpha weighted like a colour."""
    flat = img.reshape(-1, 4).astype(np.int32)
    p = pal[:used].astype(np.int32)
    out = np.zeros(len(flat), dtype=np.uint8)
    step = 1 << 16
    for s in range(0, len(flat), step):
        blk = flat[s:s + step]
        d = ((blk[:, None, :] - p[None, :, :]) ** 2).sum(2)
        out[s:s + step] = np.argmin(d, axis=1).astype(np.uint8)
    # anything present verbatim in the palette keeps that entry, so an
    # unmodified export comes back with the indices it started with
    for i, c in enumerate(pal[:used]):
        m = (flat == c.astype(np.int32)).all(1)
        if m.any():
            out[m] = i
    return out


def downsample(img):
    """Halve an RGBA image, averaging with alpha premultiplied."""
    h, w = img.shape[:2]
    a = img[:h // 2 * 2, :w // 2 * 2].astype(np.float64)
    al = a[:, :, 3:4] / 255.0
    pm = np.concatenate([a[:, :, :3] * al, a[:, :, 3:4]], axis=2)
    pm = pm.reshape(h // 2, 2, w // 2, 2, 4).mean(axis=(1, 3))
    al2 = np.maximum(pm[:, :, 3:4] / 255.0, 1e-6)
    rgb = np.clip(pm[:, :, :3] / al2, 0, 255)
    return np.concatenate([rgb, pm[:, :, 3:4]], axis=2).round().astype(np.uint8)


def shared_palette(recs, r):
    return sum(1 for o in recs if o["pal_off"] == r["pal_off"] and o["pal_sz"] == r["pal_sz"]) > 1


def write_texture(path, name, png, out_path, keep_palette=False):
    d = bytearray(open(path, "rb").read())
    recs, base = parse_pack(bytes(d))
    hit = [r for r in recs if r["name"].lower() == name.lower()]
    if not hit:
        raise SystemExit("no texture called %r in %s. Names in this pack:\n  %s"
                         % (name, os.path.basename(path),
                            "\n  ".join(r["name"] for r in recs[:40])))
    r = hit[0]
    f = fmt_of(r)
    if f is None:
        raise SystemExit("%s is in a format this tool does not write "
                         "(%dx%d, pixels %d bytes, palette %d bytes)"
                         % (r["name"], r["w"], r["h"], r["pix_sz"], r["pal_sz"]))
    bpp, nent, fix8 = f
    w, h = r["w"], r["h"]
    img = np.array(Image.open(png).convert("RGBA"))
    if img.shape[0] != h or img.shape[1] != w:
        raise SystemExit("%s is %dx%d but the slot is %dx%d. A texture cannot change\n"
                         "size on this console: the slot is fixed and the graphics chip\n"
                         "has 4 MB of memory. Resize the PNG to %dx%d and try again."
                         % (os.path.basename(png), img.shape[1], img.shape[0], w, h, w, h))
    print("%s  %dx%d  %d-bit" % (r["name"], w, h, bpp))

    if bpp == 32:
        pix = pack_alpha32(img)
        pal = b""
    else:
        if keep_palette or shared_palette(recs, r):
            praw = bytes(d[base + r["pal_off"]: base + r["pal_off"] + r["pal_sz"]])
            pal_rgba = make_palette(praw, nent, fix8)
            used = nent
            pal = praw
            print("  palette: kept as shipped (%s)"
                  % ("asked for" if keep_palette else "shared with another texture"))
        else:
            pal_rgba, used = build_palette(img, nent)
            pal = pack_palette(pal_rgba, fix8)
            print("  palette: rebuilt, %d of %d entries used" % (used, nent))
        div = 1 if bpp == 8 else 2
        lv = levels_of(w, h, r["pix_sz"], div)
        print("  %d mip level%s" % (len(lv), "" if len(lv) == 1 else "s"))
        cur = img
        parts = []
        for lw, lh, at, sz in lv:
            if cur.shape[0] != lh or cur.shape[1] != lw:
                cur = downsample(cur)
            idx = to_indices(cur, pal_rgba, used)
            b = swizzle_indices(idx, lw, lh, bpp)
            if b is None:
                raise SystemExit("the %dx%d mip level uses a 4-bit layout this tool "
                                 "cannot write yet" % (lw, lh))
            if len(b) != sz:
                raise SystemExit("internal: %dx%d level made %d bytes, slot wants %d"
                                 % (lw, lh, len(b), sz))
            parts.append(b)
        pix = b"".join(parts)

    if len(pix) != r["pix_sz"]:
        raise SystemExit("internal: built %d pixel bytes, slot holds %d"
                         % (len(pix), r["pix_sz"]))
    if len(pal) != r["pal_sz"]:
        raise SystemExit("internal: built %d palette bytes, slot holds %d"
                         % (len(pal), r["pal_sz"]))
    before = len(d)
    d[base + r["pix_off"]: base + r["pix_off"] + len(pix)] = pix
    if pal:
        d[base + r["pal_off"]: base + r["pal_off"] + len(pal)] = pal
    if len(d) != before:
        raise SystemExit("internal: the pack changed size")
    open(out_path, "wb").write(bytes(d))
    print("  wrote %s (%s bytes, unchanged in size)"
          % (out_path, "{:,}".format(len(d))))


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


def find_embedded(d):
    """Texture packs inside a bundle (GLOBALB.BUN, INGAMEB.BUN, FRONTB.BUN):
    a 0xB0300001 container whose first child is the 0x30300002 header, plus
    the 0xB0300100 trailer when it follows. Yields (offset, bytes)."""
    magic = struct.pack("<I", 0xB0300001)
    i = d.find(magic)
    while i != -1:
        if i % 4 == 0 and i + 12 <= len(d):
            sz = struct.unpack_from("<I", d, i + 4)[0]
            if 0 < sz <= len(d) - i - 8 and struct.unpack_from("<I", d, i + 8)[0] == 0x30300002:
                end = i + 8 + sz
                if end + 8 <= len(d) and struct.unpack_from("<I", d, end)[0] == 0xB0300100:
                    end += 8 + struct.unpack_from("<I", d, end + 4)[0]
                yield i, d[i:end]
                i = end - 4
        i = d.find(magic, i + 4)


def main():
    a = sys.argv[1:]
    keep = "--keep-palette" in a
    a = [x for x in a if x != "--keep-palette"]

    if a and a[0] == "embedded" and len(a) == 3:
        # write the packs found inside bundles as <entry>_<pack name>.tpk
        os.makedirs(a[2], exist_ok=True)
        for n in sorted(os.listdir(a[1])):
            fp = os.path.join(a[1], n)
            if not os.path.isfile(fp) or os.path.getsize(fp) > 60000000:
                continue
            d = open(fp, "rb").read()
            for off, blob in find_embedded(d):
                if off == 0:
                    continue          # the file is itself a pack
                name = blob[16:16 + 32].split(b"\0")[0].decode("latin1") or "pack"
                out = os.path.join(a[2], "%s_%s.tpk" % (n.split("_")[0], re.sub(r"[^A-Za-z0-9]+", "_", name)))
                open(out, "wb").write(blob)
                print("%s: pack %r at 0x%x, %d bytes -> %s" % (n, name, off, len(blob), out))
        return

    if a and a[0] == "list" and len(a) == 2:
        d = open(a[1], "rb").read()
        recs, _ = parse_pack(d)
        print("%s: %d textures" % (os.path.basename(a[1]), len(recs)))
        for r in recs:
            f = fmt_of(r)
            kind = "%d-bit" % f[0] if f else "unsupported"
            sw = ""
            if f and f[0] in (4, 8):
                sw = "swizzled" if is_swizzled(r["w"], r["h"], f[0]) else "linear"
                n = mip_levels(r["w"], r["h"], r["pix_sz"], 1 if f[0] == 8 else 2)
                sw += ", %d mip level%s" % (n, "" if n == 1 else "s")
            print("  %-26s %4dx%-4d %-12s %s" % (r["name"], r["w"], r["h"], kind, sw))
        return

    if a and a[0] == "verify":
        if len(a) != 2:
            print(__doc__)
            return
        targets = []
        if os.path.isdir(a[1]):
            for n in sorted(os.listdir(a[1])):
                fp = os.path.join(a[1], n)
                if os.path.isfile(fp) and open(fp, "rb").read(4) == b"\x01\x00\x30\xb0":
                    targets.append(fp)
        else:
            targets = [a[1]]
        tok = tbad = tskip = 0
        for fp in targets:
            try:
                ok, bad, skip, notes = verify(fp)
            except Exception as e:
                print("%s: cannot parse (%s)" % (os.path.basename(fp), e))
                continue
            tok += ok
            tbad += bad
            tskip += skip
            if bad or notes:
                print("%s: %d exact, %d FAILED, %d skipped"
                      % (os.path.basename(fp), ok, bad, skip))
                for n in notes:
                    print("    " + n)
        print()
        print("%d textures re-pack to the same bytes, %d do not, %d in formats not handled"
              % (tok, tbad, tskip))
        if tbad == 0:
            print("the import path is an exact inverse of the export path")
        return

    if a and a[0] == "write":
        if len(a) != 5:
            print(__doc__)
            return
        write_texture(a[1], a[2], a[3], a[4], keep)
        return

    root = a[0] if a else "extracted"
    out = a[1] if len(a) > 1 else os.path.join(os.path.dirname(root.rstrip("\\/")), "textures")
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
