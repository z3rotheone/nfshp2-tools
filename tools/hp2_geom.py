"""
Parse Need for Speed: Hot Pursuit 2 (PS2) GEOMETRY.BIN car models.

Structure (same u32 id + u32 size chunk convention as the texture packs and
sound banks, a container being an id with the top bit set):

    0x00034013  file header: 6 x 32-byte light flare positions
    0x80034000  model (container)
        0x00034001  the development path the file was built from
        0x80034002  one part (container), repeated
            0x00034003  part header: name, name hash, bounding box
            0x00034006  texture hashes used by this part (u32 hash, u32 pad)
            0x0003401d  material names, hashed: CARSKIN, RUBBER, MIRROR...
                        A batch's material index selects from this list.
            0x00034004  sub-mesh records, each with a bounding box
            0x00034005  the geometry itself, as a VIF display list

Parts are named <CAR>_A, _B, _C and <CAR>_TIRE_FRONT_A. The letter is a
variant suffix; the part header's hash covers the full name including it, so
`4DR_SEDAN02_TIRE_FRONT_` in the name field actually hashes as
`4DR_SEDAN02_TIRE_FRONT_A`. Hashes are the §9 name hash (see hp2_hash.py, and docs/formats.md section 10).

The file header's 6 records are where the light flares go: each holds a
position, a hashed name (FLARE_HEADLIGHT_LEFT, FLARE_BRAKELIGHT_RIGHT ...) and
the hash of the car's own base name.

The geometry chunk is a PS2 VIF display list, one batch of at most 30
vertices per MSCAL. Each batch unpacks, at fixed VU1 addresses:

    0   V4_8     vertex count N, (N-1)/4, 3N, material index * 4
    1   V4_32    strip masks, up to two 32-bit words (see strip_flags)
    2   V4_32    positions, four vertices at a time and transposed: x0 x1 x2 x3,
                 y0..y3, z0..z3. The N % 4 left over follow as three V1/V2/V3
                 rows laid out the same way.
    32  V4_32    texture coordinates, two vertices a quad as u0 v0 v1 u1; an
                 odd last vertex follows as one V2_32
    52  V4_5551  one colour per vertex
    92  V4_8     normals as signed bytes, in transposed groups of four like the
                 positions, the last group zero padded. Some materials have none.

The batch is a run of triangle strips. Which vertices start a strip is in the
masks at address 1, decoded by strip_flags(); every batch's resulting triangle
count equals the count stored in its 64-byte record in chunk 0x00034004, which
is the check `--check` runs. No VU1 microprogram was needed for any of this.

Usage:
    python hp2_geom.py CAR.BIN              summary of parts and textures
    python hp2_geom.py CAR.BIN --vif        walk the VIF display lists
    python hp2_geom.py CAR.BIN --dump-verts write part_N.xyz point clouds
    python hp2_geom.py CAR.BIN --check      verify triangle counts against the file
    python hp2_geom.py CAR.BIN --records    print every batch record raw, as hex
    python hp2_geom.py CAR.BIN --obj DIR    write one Wavefront OBJ per part
          [--textures PNGDIR]               ...with an .mtl giving each batch its
                                            own texture, from hp2_tpk.py's PNGs
          [--lights off|on|damaged]         which state light textures show
    python hp2_geom.py extracted/* --survey one line per model file: do the
                                            decoded triangle counts match?
    python hp2_geom.py extracted/* --names --tpk PACK.tpk
                                            which texture and material hashes
                                            the pack's names account for
    python hp2_geom.py extracted/* --guess --tpk extracted --strings SLUS_203.62 -o names.csv
                                            name unknown hashes as two pieces
                                            joined, from names the game holds;
                                            --known names.csv reuses them
    --tpk PACK may be given with any mode, more than once; PACK may be a
    folder, in which case every texture pack in it is read.
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
                            if len(blob) >= i + 46:
                                # batches, triangles, vertices
                                part["counts"] = struct.unpack_from("<3H", blob, i + 40)
                        elif c3 in (TEX1, TEX2):
                            key = "tex" if c3 == TEX1 else "tex2"
                            base = o3
                            # chunks are sometimes preceded by 0x11 alignment filler
                            while base + 4 <= o3 + s3 and d[base:base + 4] == b"\x11" * 4:
                                base += 4
                            for k in range((o3 + s3 - base) // 8):
                                part[key].append(struct.unpack_from("<I", d, base + k * 8)[0])
                        elif c3 == SUBMESH:
                            part["submesh"] = (o3, s3)
                        elif c3 == GEOM:
                            part["geom"] = (o3 + 8, s3 - 8)
                    out["parts"].append(part)
    return d, out


def batches(d, start, end):
    """Yield one dict per MSCAL: {VU address: [(cmd, num, payload bytes)]}."""
    cur = {}
    for off, cmd, num, imm, po, pn in walk_vif(d, start, end):
        if 0x60 <= cmd <= 0x7F:
            cur.setdefault(imm & 0x3FF, []).append((cmd, num, d[po:po + pn]))
        elif cmd in (0x14, 0x15, 0x17):
            if cur:
                yield cur
            cur = {}
    if cur:
        yield cur


def strip_flags(q1, n):
    """Per vertex, 1 if it completes no triangle - the first two of each strip.

    The quad at address 1 holds one or two non-zero 32-bit words. The first
    describes the batch from its start: bit 14 is vertex 0, bit 13 vertex 1,
    down to bit 0 for vertex 14. The second describes it from its end: with
    the vertex count rounded up to a multiple of four as P, bit 1 is vertex P
    and bit 17 is vertex P-16. Bits above those are set in the data but do not
    mark strips. A strip start is always two flagged vertices in a row, and
    vertex 15 can fall in the gap between the two words; it is flagged when
    the pair on either side of it would otherwise be cut in half.
    """
    words = [w for w in struct.unpack_from("<4I", q1, 0) if w]
    f = [0] * (n + 40)
    if not words:
        f[0] = f[1] = 1
        return f[:n]
    a = words[0]
    for b in range(15):
        if (a >> b) & 1:
            f[14 - b] = 1
    if len(words) > 1:
        padded = (n + 3) // 4 * 4
        for b in range(18):
            v = padded + 1 - b
            if v >= 15 and (words[1] >> b) & 1:
                f[v] = 1
        if f[16] and not f[17]:
            f[15] = 1
    if f[14] and not f[13]:
        f[15] = 1
    return f[:n]


def _transposed(vals, n, groups_padded):
    """Undo the x0 x1 x2 x3 / y0.. / z0.. grouping; returns n (x, y, z)."""
    out = []
    full = len(vals) // 12 if groups_padded else n // 4
    for k in range(full):
        g = vals[k * 12:k * 12 + 12]
        for j in range(4):
            out.append((g[j], g[4 + j], g[8 + j]))
    return out[:n], full


def decode_batch(b):
    """Positions, normals, texture coordinates, colours and triangles of one batch."""
    hdr = b[0][0][2]
    n = hdr[0]
    out = {"n": n, "material": hdr[3] >> 2, "normals": None}

    pos_blocks = sorted((addr, e) for addr in b if 2 <= addr < 32 for e in b[addr])
    if not pos_blocks:
        raise ValueError("no position data")
    first = pos_blocks[0][1]
    vals = struct.unpack_from("<%df" % (len(first[2]) // 4), first[2])
    rem = n % 4
    if rem and (n < 4 or len(pos_blocks) > 1):
        # fewer than four vertices: the leftover block is the only one
        pos, _ = _transposed(vals, n, False) if n >= 4 else ([], 0)
        tail = pos_blocks[1 if n >= 4 else 0][1][2]
        if len(tail) < 12 * rem:
            raise ValueError("leftover positions: %d bytes for %d vertices" % (len(tail), rem))
        t = struct.unpack_from("<%df" % (3 * rem), tail)
        for j in range(rem):
            pos.append((t[j], t[rem + j], t[2 * rem + j]))
    elif len(vals) >= (n + 3) // 4 * 12:
        # the last group padded out to four, as the normals are
        pos, _ = _transposed(vals, n, True)
    elif not rem:
        raise ValueError("positions: %d floats for %d vertices" % (len(vals), n))
    else:
        raise ValueError("positions: %d floats for %d vertices and no leftover block"
                         % (len(vals), n))
    out["pos"] = pos

    uv = []
    for addr, e in sorted((addr, e) for addr in b if 32 <= addr < 52 for e in b[addr]):
        v = struct.unpack_from("<%df" % (len(e[2]) // 4), e[2])
        if (e[0] >> 2) & 3 == 3:                       # V4: u0 v0 v1 u1
            for k in range(0, len(v) - 3, 4):
                uv.append((v[k], v[k + 1]))
                uv.append((v[k + 3], v[k + 2]))
        else:                                          # the odd last vertex
            uv.append((v[0], v[1]))
    out["uv"] = uv[:n]

    col = []
    if 52 in b:
        raw = b[52][0][2]
        for k in range(min(n, len(raw) // 2)):
            c = struct.unpack_from("<H", raw, k * 2)[0]
            col.append(((c & 31) / 31.0, ((c >> 5) & 31) / 31.0, ((c >> 10) & 31) / 31.0))
    out["col"] = col

    if 92 in b:
        raw = b[92][0][2]
        raw = raw[:len(raw) // 12 * 12]
        nv, _ = _transposed(struct.unpack_from("<%db" % len(raw), raw), n, True)
        out["normals"] = [(x / 127.0, y / 127.0, z / 127.0) for x, y, z in nv]

    f = strip_flags(b[1][0][2], n) if 1 in b else [1, 1] + [0] * (n - 2)
    out["flags"] = f
    tris, k = [], 0
    for v in range(n):
        if f[v]:
            k = 0
            continue
        if v < 2:
            continue
        # strips alternate their winding; the first triangle faces along its normals
        tris.append((v - 2, v - 1, v) if k % 2 == 0 else (v - 1, v - 2, v))
        k += 1
    out["tris"] = tris
    return out


def batch_records(d, part):
    """The 64-byte record chunk 0x00034004 keeps for each batch.

        0   u32   texture: index into the part's texture list (0x00034006)
        8   u32   offset of the batch's VIF packet in the geometry chunk
        12  u8    size of that packet in quadwords (offset steps by 16x this)
        15  u8    flags: 0x00 with no material; 0x40, 0x41, 0x50 and, on the
                  911's windows, 0xC1. Bit 0 is tested against "has normals"
                  and bit 7 may be transparency.
        16  3f    bounding box min
        28  u8    running draw-group number (rises through the list; not the
                  material, it exceeds the material count)
        29  u8    vertices
        30  u8    triangles
        31  u8    material: index into the part's material list (0x0003401d),
                  0xFF for none (the VU header then holds 0x3F)
        32  3f    bounding box max

    Records are sorted by texture then material, as a renderer would want
    to minimise state changes."""
    if not part["submesh"]:
        return []
    o, sz = part["submesh"]
    body = d[o:o + sz]
    lead = len(body) % 64
    out = []
    for i in range(len(body) // 64):
        r = body[lead + i * 64: lead + (i + 1) * 64]
        out.append(dict(texture=struct.unpack_from("<I", r, 0)[0], material=r[31], flags=r[15],
                        group=r[28], verts=r[29], tris=r[30],
                        bbox=(struct.unpack_from("<3f", r, 16), struct.unpack_from("<3f", r, 32))))
    return out


def describe(b):
    """The unpacks of a batch, for reporting one that will not decode."""
    out = []
    for addr in sorted(b):
        for cmd, num, pay in b[addr]:
            out.append("%d:%s x%d" % (addr, unpack_label(cmd).replace("UNPACK ", ""), num))
    return " ".join(out)


def part_mesh(d, part, failures=None):
    """All batches of a part decoded, and how many disagree with their record.

    A batch that cannot be decoded is left out and, if `failures` is a list,
    reported there as (batch index, error, layout)."""
    o, n = part["geom"]
    recs = batch_records(d, part)
    out, wrong = [], 0
    for i, b in enumerate(batches(d, o, o + n)):
        if 0 not in b or 2 not in b:
            continue
        try:
            m = decode_batch(b)
        except (ValueError, IndexError, struct.error) as e:
            wrong += 1
            if failures is not None:
                failures.append((i, str(e), describe(b), b[0][0][2][:4].hex()))
            continue
        if i < len(recs):
            r = recs[i]
            if r["verts"] != m["n"] or r["tris"] != len(m["tris"]):
                wrong += 1
            m["texture"] = r["texture"]
            m["index"] = i
            # 0xFF means no material; the VU header holds material*4 in a
            # byte, so the same "none" reads back there as 0x3F
            m["rec_material"] = 0x3F if r["material"] == 0xFF else r["material"]
            m["rec_flags"] = r["flags"]
        out.append(m)
    return out, wrong, len(recs)


def material_name(part, k):
    """Name for material index k: an entry of the part's 0x0003401d list."""
    lst = part.get("tex2") or []
    if k == 0x3F:
        return "nomaterial"
    if k < len(lst):
        return KNOWN.get(lst[k]) or "%08x" % lst[k]
    return "material_%d" % k


def texture_name(part, k, car):
    """Name for texture index k: an entry of the part's 0x00034006 list."""
    lst = part.get("tex") or []
    if k is None or k >= len(lst):
        return None
    h = lst[k]
    if h in KNOWN:
        return KNOWN[h]
    for suf in CAR_SUFFIXES:
        if name_hash(car + suf) == h:
            return car + suf
    return "%08x" % h


_PNG_INDEX = {}
PACK_NAME_LEN = 23     # .tpk name fields hold 23 characters; longer names are cut


def _key(name):
    return name.upper()[:PACK_NAME_LEN] + ".PNG"


def runtime_textures(name, car, state="off"):
    """Texture names to try, in order, for a name the model asks for.

    The model names slots, not always pack entries. The pack holds lights in
    three states - <CAR>_HEADLIGHT, _HEADLIGHT_ON, _HEADLIGHT_DAMAGE0 - while
    the model asks for <CAR>_HEADLIGHT_LEFT and _RIGHT, so each side can be
    lit or broken on its own. A few cars (CROWNVIC, JAGUAR, VANQUISH,
    CHALLENGE) do have _LEFT/_RIGHT brake light art, so the exact name is
    tried first. The generic TIRE slot is the car's own <CAR>_TIRE.
    This is inferred from the pack's names, not read from the executable."""
    out = [name]
    up = name.upper()
    for side in ("_LEFT", "_RIGHT"):
        if car and up.startswith(car.upper() + "_") and up.endswith(side):
            base = name[:-len(side)]
            out += {"on": [base + "_ON"], "damaged": [base + "_DAMAGE0"]}.get(state, [])
            out.append(base)
    if up == "TIRE" and car:
        out.append(car + "_TIRE")
    return out


def find_png(root, name, car=None):
    """Path of NAME.png under root (hp2_tpk.py exports one folder per pack).

    Generic names - WINDOW_FRONT, CARBOTTOM, TIRE - exist in every car's
    pack, so the folder holding <CAR>_SKIN1.png is searched first and any
    other folder only as a fallback."""
    if not root:
        return None
    if root not in _PNG_INDEX:
        idx = {}
        for dirpath, _d, files in os.walk(root):
            for f in files:
                if f.upper().endswith(".PNG"):
                    idx.setdefault(_key(f[:-4]), []).append(os.path.join(dirpath, f))
        _PNG_INDEX[root] = idx
    idx = _PNG_INDEX[root]
    hits = idx.get(_key(name), [])
    if not hits:
        return None
    if car:
        for skin in idx.get(_key(car + "_SKIN1"), []):
            home = os.path.dirname(skin)
            for h in hits:
                if os.path.dirname(h) == home:
                    return h
    return hits[0]


def write_mtl(path, entries):
    """One OBJ material per (material, texture) pair the part draws with.
    `entries` is a list of (name, relative PNG path or None)."""
    with open(path, "w") as f:
        f.write("# texture per batch from byte 0 of its 0x00034004 record\n")
        for nm, png in entries:
            f.write("\nnewmtl %s\nKd 1 1 1\n" % nm)
            if png:
                f.write("map_Kd %s\n" % png)


def draw_key(part, m, car):
    """OBJ material name for a batch: <material>__<texture>."""
    tex = texture_name(part, m.get("texture"), car) or "notexture"
    mat = material_name(part, m["material"]) if part else "material_%d" % m["material"]
    return "%s__%s" % (mat, tex), tex


def write_obj(path, name, mesh, part=None, mtl=None, car=""):
    """Wavefront OBJ: positions with vertex colours, texture coordinates, normals,
    one group per material. X is forward, Y is left-right and Z is up, as stored."""
    with open(path, "w") as f:
        f.write("# %s\n# axes as stored: X forward, Y sideways, Z up\n" % name)
        if mtl:
            f.write("mtllib %s\n" % mtl)
        f.write("o %s\n" % name)
        vbase = nbase = 1
        cur = None
        for m in sorted(mesh, key=lambda m: draw_key(part, m, car)[0]):
            nm = draw_key(part, m, car)[0]
            if nm != cur:
                cur = nm
                f.write("g %s\nusemtl %s\n" % (nm, nm))
            for i, p in enumerate(m["pos"]):
                c = m["col"][i] if i < len(m["col"]) else (1.0, 1.0, 1.0)
                f.write("v %.6f %.6f %.6f %.4f %.4f %.4f\n" % (p + c))
            for u, v in m["uv"]:
                f.write("vt %.6f %.6f\n" % (u, 1.0 - v))
            if m["normals"]:
                for nx in m["normals"]:
                    f.write("vn %.5f %.5f %.5f\n" % nx)
            for a, b_, c in m["tris"]:
                if m["normals"]:
                    f.write("f %d/%d/%d %d/%d/%d %d/%d/%d\n" % (
                        vbase + a, vbase + a, nbase + a, vbase + b_, vbase + b_, nbase + b_,
                        vbase + c, vbase + c, nbase + c))
                else:
                    f.write("f %d/%d %d/%d %d/%d\n" % (vbase + a, vbase + a, vbase + b_,
                                                       vbase + b_, vbase + c, vbase + c))
            vbase += m["n"]
            if m["normals"]:
                nbase += m["n"]


KNOWN = {0x2787edab: "INTERIOR", 0x6bfa56da: "MIRROR", 0xd6d6080a: "CARSKIN",
         0x78743ca1: "RUBBER", 0xbd1f0e0a: "CARBOTTOM",
         0xacec665c: "FLARE_HEADLIGHT_LEFT", 0x4ae7f96f: "FLARE_HEADLIGHT_RIGHT",
         0x7efcf06f: "FLARE_BRAKELIGHT_LEFT", 0x5f09c5e2: "FLARE_BRAKELIGHT_RIGHT"}

# Names found from the 911's attachment points and texture lists. Each is a
# plain one-word-plus-side name, and each sits where its name says (the
# windows at roof height, CORNER_* at the bumper corners, EXHAUST twice at the
# back), which is the check that they are not chance collisions. They also fix
# the axes: +x is the front and +y is the car's left.
for _n in ("WINDOW_FRONT", "WINDOW_REAR", "WINDOW_LEFT_FRONT", "WINDOW_RIGHT_FRONT",
           "WINDOW_LEFT_REAR", "WINDOW_RIGHT_REAR",
           "HEADLIGHT_LEFT", "HEADLIGHT_RIGHT", "BRAKELIGHT_LEFT", "BRAKELIGHT_RIGHT",
           "FLARE_REVERSELIGHT_LEFT", "FLARE_REVERSELIGHT_RIGHT", "FLARE_BRAKELIGHT_MIDDLE",
           "CORNER_LEFT_FRONT", "CORNER_RIGHT_FRONT", "CORNER_LEFT_REAR", "CORNER_RIGHT_REAR",
           "SIDE_MIRROR_LEFT", "SIDE_MIRROR_RIGHT", "LICENSE_PLATE_FRONT", "LICENSE_PLATE_REAR",
           "EXHAUST", "ENGINE", "TIRE", "BRAKES", "PLASTIC", "WHEEL_BLUR"):
    KNOWN[name_hash(_n)] = _n

# Per-car texture names: <CAR> + one of these.
CAR_SUFFIXES = ("_SKIN1", "_SKIN2", "_SKIN3", "_SKIN4", "_SKIN5", "_SKIN6", "_SKIN7",
                "_TIRE", "_GLASS", "_LICENSE_PLATE",
                "_HEADLIGHT_LEFT", "_HEADLIGHT_RIGHT", "_BRAKELIGHT_LEFT", "_BRAKELIGHT_RIGHT")


def learn_names(path):
    """Add every texture name in a .tpk pack - or in every pack in a folder -
    to KNOWN, so hashes print as names."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import hp2_tpk
    files = ([os.path.join(path, f) for f in sorted(os.listdir(path))]
             if os.path.isdir(path) else [path])
    packs = names = 0
    for fp in files:
        if not os.path.isfile(fp):
            continue
        with open(fp, "rb") as f:
            d = f.read()
        if d[:4] != b"\x01\x00\x30\xb0":
            if not os.path.isdir(path):
                raise SystemExit("%s is not a texture pack" % fp)
            continue
        try:
            recs, _ = hp2_tpk.parse_pack(d)
        except Exception as e:
            print("skipped %s (%s)" % (os.path.basename(fp), e))
            continue
        packs += 1
        for r in recs:
            # The stored hash is of the full name; the name field holds only
            # 23 characters, so for long names the two hashes differ. Models
            # refer to the stored one.
            nm = r["name"]
            if r.get("hash") and r["hash"] != name_hash(nm):
                nm += "..."
            if KNOWN.setdefault(r.get("hash") or name_hash(r["name"]), nm) == nm:
                names += 1
    print("learned %d texture names from %d pack%s in %s"
          % (names, packs, "" if packs == 1 else "s", os.path.basename(path.rstrip("/")) or path))


def name_report(paths):
    """Every texture and material hash used by the models, and which have names."""
    seen = {}
    for path in paths:
        try:
            with open(path, "rb") as f:
                if struct.unpack("<I", f.read(4))[0] != HDR:
                    continue
            d, m = parse(path)
        except Exception:
            continue
        for p in m["parts"]:
            for kind, lst in (("texture", p["tex"]), ("material", p["tex2"])):
                for h in lst:
                    e = seen.setdefault((kind, h), [0, set()])
                    e[0] += 1
                    e[1].add(p["name"].split("_")[0])
    for kind in ("texture", "material"):
        items = [(k[1], v) for k, v in seen.items() if k[0] == kind]
        named = [x for x in items if x[0] in KNOWN]
        uses = sum(v[0] for _, v in items)
        nuses = sum(v[0] for _, v in named)
        print("%ss: %d distinct, %d named, covering %d of %d uses"
              % (kind, len(items), len(named), nuses, uses))
    for kind in ("texture", "material"):
        items = [(k[1], v) for k, v in seen.items() if k[0] == kind]
        print()
        print("%ss used by more than one car:" % kind)
        for h, (n, cars) in sorted(items, key=lambda kv: -kv[1][0]):
            if len(cars) > 1:
                print("  %08x  %-28s used by %3d parts in %2d cars"
                      % (h, KNOWN.get(h, ""), n, len(cars)))


def model_hashes(paths):
    """(set of texture/material hashes the models use, list of car names)"""
    want, cars = set(), set()
    for path in paths:
        try:
            with open(path, "rb") as f:
                if struct.unpack("<I", f.read(4))[0] != HDR:
                    continue
            d, m = parse(path)
        except Exception:
            continue
        car = os.path.basename(os.path.dirname(m["devpath"].replace("\\", "/")))
        if car:
            cars.add(car)
        for p in m["parts"]:
            want.update(p["tex"])
            want.update(p["tex2"])
            want.add(p.get("hash", 0))
        for a in m["attach"]:
            want.add(a["part"])
    want.discard(0)
    return want, sorted(cars)


def guess_names(paths, string_files, out_csv=None):
    """Name unknown hashes as two pieces joined, drawn from texture names,
    strings in given files (the executable), car names and their parts."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import hp2_hash
    want, cars = model_hashes(paths)
    unknown = {h for h in want if h not in KNOWN}
    pool = set(cars) | {v.rstrip(".") for v in KNOWN.values()}
    for fp in string_files:
        got = hp2_hash.strings_in(fp)
        print("%d strings from %s" % (len(got), os.path.basename(fp)))
        for t in got:
            t = t.strip()
            if 2 <= len(t) <= 40:
                pool.add(t)
                if "%s" in t and t.count("%") == 1:
                    for c in cars:
                        pool.add(t.replace("%s", c))
    words = set()
    for t in list(pool):
        for w in t.replace("\\", "_").replace("/", "_").replace(".", "_").split("_"):
            if 2 <= len(w) <= 24:
                words.add(w)
    pool |= words
    pool |= {t.upper() for t in pool}
    prefixes = pool | {t + "_" for t in pool}
    print("%d unknown hashes; trying %d prefixes x %d suffixes"
          % (len(unknown), len(prefixes), len(pool)), flush=True)
    found = hp2_hash.two_piece(unknown, prefixes, pool)

    import re
    clean = re.compile(r"^[A-Z0-9][A-Z0-9_]*[A-Z0-9]$")

    def rank(c):
        # a real asset name is upper case, digits and underscores; anything
        # else that hashes the same is a coincidence with executable junk
        return (0 if clean.match(c) else 1, c.count("_") > 6, len(c), c)

    kinds = {}
    for path in paths:
        try:
            with open(path, "rb") as f:
                if struct.unpack("<I", f.read(4))[0] != HDR:
                    continue
            d, m = parse(path)
        except Exception:
            continue
        for p in m["parts"]:
            for h in p["tex"]:
                kinds.setdefault(h, "texture")
            for h in p["tex2"]:
                kinds.setdefault(h, "material")
            kinds.setdefault(p.get("hash", 0), "part")
        for a in m["attach"]:
            kinds.setdefault(a["part"], "flare")

    rows = []
    for h in sorted(unknown):
        opts = sorted({(p + s_).strip() for p, s_ in found.get(h, ())}, key=rank)
        good = [o for o in opts if clean.match(o)]
        if good:
            KNOWN[h] = good[0]
            rows.append((kinds.get(h, "?"), h, good[0], "sure" if len(good) == 1 else
                         "also " + ", ".join(good[1:3])))
        else:
            rows.append((kinds.get(h, "?"), h, "", "only junk matches" if opts else "no match"))
    for kind in ("texture", "material", "flare", "part"):
        sel = [r for r in rows if r[0] == kind]
        if not sel:
            continue
        got = [r for r in sel if r[2]]
        print()
        print("%ss: %d of %d unknown now named" % (kind, len(got), len(sel)))
        if kind == "part":
            continue                  # part names are already in the files
        for _, h, nm, note in sorted(sel, key=lambda r: (not r[2], r[2])):
            print("  %08x  %-34s %s" % (h, nm or "-", note if note != "sure" else ""))
    if out_csv:
        with open(out_csv, "w") as f:
            f.write("hash,name,kind\n")
            for h, nm in sorted(KNOWN.items()):
                f.write("%08x,%s,%s\n" % (h, nm, kinds.get(h, "")))
        print()
        print("wrote %d names to %s; pass it back with --known %s" % (len(KNOWN), out_csv, out_csv))


def load_known(path):
    n = 0
    with open(path) as f:
        for line in f:
            parts = line.rstrip("\n").split(",")
            if len(parts) >= 2 and parts[0] != "hash" and parts[1]:
                try:
                    KNOWN.setdefault(int(parts[0], 16), parts[1])
                    n += 1
                except ValueError:
                    pass
    print("loaded %d names from %s" % (n, os.path.basename(path)))


def survey(paths):
    """One line per file: do the decoded triangle counts match the file's own?"""
    tot = {"files": 0, "parts": 0, "batches": 0, "agree": 0, "tris": 0}
    trouble = []
    for path in paths:
        if os.path.isdir(path):
            continue
        try:
            with open(path, "rb") as f:
                if struct.unpack("<I", f.read(4))[0] != HDR:
                    continue
            d, m = parse(path)
        except Exception:
            continue
        car = os.path.basename(os.path.dirname(m["devpath"].replace("\\", "/"))) or "?"
        nb = ok = tris = 0
        notes, info = [], []
        for p in m["parts"]:
            if not p["geom"]:
                continue
            fails = []
            try:
                mesh, wrong, nrec = part_mesh(d, p, fails)
            except Exception as e:
                notes.append("%s: could not decode (%s)" % (p["name"], e))
                continue
            for i, err, lay, hdr in fails[:2]:
                notes.append("%s batch %d: %s | header %s | %s" % (p["name"], i, err, hdr, lay))
            if len(fails) > 2:
                notes.append("%s: %d more batches like that" % (p["name"], len(fails) - 2))
            nb += nrec
            ok += nrec - wrong
            tris += sum(len(x["tris"]) for x in mesh)
            if wrong > len(fails):
                notes.append("%s: %d of %d batches disagree" % (p["name"], wrong - len(fails), nrec))
            t = (nrec, sum(len(x["tris"]) for x in mesh), sum(x["n"] for x in mesh))
            if not wrong and p.get("counts") and p["counts"][::2] == t[::2] and p["counts"][1] != t[1]:
                # every batch matches its own record, so this is the file disagreeing
                # with itself; noted, not counted against the decode
                info.append("%s: header total %d triangles, its batch records sum to %d"
                            % (p["name"], p["counts"][1], t[1]))
            elif p.get("counts") and p["counts"] != t:
                notes.append("%s: part header says %d/%d/%d batches/triangles/vertices, decoded %d/%d/%d"
                             % ((p["name"],) + p["counts"] + t))
        tot["files"] += 1
        tot["parts"] += len(m["parts"])
        tot["batches"] += nb
        tot["agree"] += ok
        tot["tris"] += tris
        print("%-24s %-18s %2d parts %5d batches %6d triangles  %s"
              % (os.path.basename(path)[:24], car[:18], len(m["parts"]), nb, tris,
                 "ok" if not notes else "CHECK"))
        for n in notes:
            print("      " + n)
        for n in info:
            print("      (note) " + n)
        if notes:
            trouble.append(os.path.basename(path))
    print()
    print("%d model files, %d parts, %d triangles" % (tot["files"], tot["parts"], tot["tris"]))
    print("%d of %d batches give exactly the triangle count the file records"
          % (tot["agree"], tot["batches"]))
    if trouble:
        print("files to look at: %s" % ", ".join(trouble))


def main():
    argv = sys.argv[1:]
    objdir = None
    if "--obj" in argv:
        i = argv.index("--obj")
        if i + 1 >= len(argv):
            raise SystemExit("--obj needs a folder to write into")
        objdir = argv[i + 1]
        del argv[i:i + 2]
    while "--tpk" in argv:
        i = argv.index("--tpk")
        if i + 1 >= len(argv):
            raise SystemExit("--tpk needs a texture pack")
        learn_names(argv[i + 1])
        del argv[i:i + 2]
    out_csv = None
    if "-o" in argv:
        i = argv.index("-o")
        out_csv = argv[i + 1]
        del argv[i:i + 2]
    state = "off"
    if "--lights" in argv:
        i = argv.index("--lights")
        state = argv[i + 1]
        if state not in ("off", "on", "damaged"):
            raise SystemExit("--lights takes off, on or damaged")
        del argv[i:i + 2]
    texdir = None
    if "--textures" in argv:
        i = argv.index("--textures")
        texdir = argv[i + 1]
        del argv[i:i + 2]
    while "--known" in argv:
        i = argv.index("--known")
        load_known(argv[i + 1])
        del argv[i:i + 2]
    string_files = []
    while "--strings" in argv:
        i = argv.index("--strings")
        string_files.append(argv[i + 1])
        del argv[i:i + 2]
    args = [a for a in argv if not a.startswith("--")]
    check = "--check" in argv
    if not args:
        print(__doc__)
        return
    show_vif = "--vif" in sys.argv
    dump = "--dump-verts" in sys.argv

    if "--survey" in argv:
        survey(args)
        return
    if "--names" in argv:
        name_report(args)
        return
    if "--guess" in argv:
        guess_names(args, string_files, out_csv)
        return

    for path in args:
        d, m = parse(path)
        print("%s  (%s bytes)" % (os.path.basename(path), "{:,}".format(m["size"])))
        if m["devpath"]:
            print("  built from: %s" % m["devpath"])
        car = os.path.basename(os.path.dirname(m["devpath"].replace("\\", "/"))) or "?"
        print("  car: %s   (hash %08x)" % (car, name_hash(car)))

        print("\n  attachment points (%d):" % len(m["attach"]))
        for i, a in enumerate(m["attach"]):
            print("    [%d] %-24s at (%7.3f,%7.3f,%7.3f)  flags %08x"
                  % (i, KNOWN.get(a["part"]) or "%08x" % a["part"],
                     a["pos"][0], a["pos"][1], a["pos"][2], a["flags"]))

        print("\n  parts (%d):" % len(m["parts"]))
        for p in m["parts"]:
            gs = p["geom"][1] if p["geom"] else 0
            print("    %-28s hash %08x  geometry %s bytes  %d+%d textures"
                  % (p["name"] or "(unnamed)", p.get("hash", 0), "{:,}".format(gs),
                     len(p["tex"]), len(p["tex2"])))
            for label, lst in (("texture", p["tex"]), ("material", p["tex2"])):
                for k, h in enumerate(lst):
                    known = KNOWN.get(h)
                    guess = ""
                    for suf in CAR_SUFFIXES:
                        if name_hash(car + suf) == h:
                            guess = car + suf
                    idx = "%d " % k if label == "material" else ""
                    print("        %s %s%08x%s" % (label, idx, h, "  = " + (known or guess)
                                                   if (known or guess) else ""))

        if "--records" in argv:
            # Raw batch records, to find which field picks the texture. Bytes
            # 16-27 and 32-43 are the bounding box and 28-30 are material,
            # vertices, triangles; everything else is printed as hex.
            for p in m["parts"]:
                if not p["submesh"]:
                    continue
                o, sz = p["submesh"]
                body = d[o:o + sz]
                lead = len(body) % 64
                vu = {x.get("index"): x["material"] for x in part_mesh(d, p)[0]} if p["geom"] else {}
                print("\n  records for %s (%d textures, %d materials):"
                      % (p["name"], len(p["tex"]), len(p["tex2"])))
                print("      #   grp  v   t  | bytes 0-15 (0 = texture)         |mat | vu | bytes 44-63")
                for i in range(len(body) // 64):
                    r = body[lead + i * 64: lead + (i + 1) * 64]
                    print("    %3d  %2d %3d %3d  | %s | %02x | %s | %s"
                          % (i, r[28], r[29], r[30], r[0:16].hex(" "), r[31],
                             "%02x" % vu[i] if i in vu else "--", r[44:64].hex(" ")))

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

        if check or objdir:
            stem = os.path.splitext(os.path.basename(path))[0]
            if objdir:
                os.makedirs(objdir, exist_ok=True)
            print("\n  meshes:")
            used = {}
            for p in m["parts"]:
                if not p["geom"]:
                    continue
                mesh, wrong, nrec = part_mesh(d, p)
                tris = sum(len(x["tris"]) for x in mesh)
                verts = sum(x["n"] for x in mesh)
                if nrec != len(mesh):
                    verdict = "%d batches but %d records - not checked" % (len(mesh), nrec)
                elif wrong:
                    verdict = "%d of %d batches DISAGREE with the file's own counts" % (wrong, nrec)
                else:
                    verdict = "all %d batches agree with the file's own counts" % nrec
                # does byte 31 of each record name the same material as the
                # batch's own VU header? and is byte 0 a valid texture index?
                both = [x for x in mesh if "rec_material" in x]
                matdiff = sum(1 for x in both if x["rec_material"] != x["material"])
                texbad = sum(1 for x in both if x["texture"] >= len(p["tex"]))
                nrmbad = sum(1 for x in both if bool(x["rec_flags"] & 1) != bool(x["normals"]))
                verdict += "; record material %s, texture index %s, flag bit 0 %s" % (
                    "agrees" if not matdiff else "DIFFERS in %d" % matdiff,
                    "in range" if not texbad else "OUT OF RANGE in %d" % texbad,
                    "= has normals" if not nrmbad else "MISMATCHES normals in %d" % nrmbad)
                if p.get("counts") and p["counts"] != (len(mesh), tris, verts):
                    verdict += "; part header says %d batches, %d triangles, %d vertices" % p["counts"]
                print("    %-28s %5d vertices %5d triangles  %s"
                      % (p["name"], verts, tris, verdict))
                if objdir:
                    nm = "%s_%s" % (stem, p["name"] or "part")
                    used[nm] = used.get(nm, 0) + 1
                    if used[nm] > 1:
                        nm += "%d" % used[nm]
                    out = os.path.join(objdir, nm + ".obj")
                    entries, missing = {}, set()
                    for x in mesh:
                        key, tex = draw_key(p, x, car)
                        if key in entries:
                            continue
                        png = None
                        for cand in (runtime_textures(tex, car, state) if texdir else []):
                            png = find_png(texdir, cand, car)
                            if png:
                                break
                        if texdir and not png:
                            missing.add(tex)
                        entries[key] = os.path.relpath(png, objdir) if png else None
                    write_mtl(os.path.join(objdir, nm + ".mtl"), sorted(entries.items()))
                    write_obj(out, nm, mesh, p, nm + ".mtl", car)
                    print("        -> %s  (%d material/texture pairs)" % (out, len(entries)))
                    if missing:
                        print("           no PNG found for: %s" % ", ".join(sorted(missing)))

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
