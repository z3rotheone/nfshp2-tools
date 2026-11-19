"""
Find and replace a file inside a PlayStation 2 disc image, in place.

An emulator boots a disc image, not loose files, so a modified ZZDATA.BIN has
to go back into the ISO before the game will see it. Rebuilding the image is
avoidable: every tool here keeps file sizes identical, so the replacement can
be written straight over the original at the same offset and the image stays
structurally valid. Nothing else in the ISO moves.

It reads the ISO9660 primary volume descriptor and walks the directory tree,
which is enough for a PS2 disc; it does not handle Joliet or multi-extent
files, neither of which a PS2 game uses.

Usage:
    python hp2_iso.py list GAME.iso
    python hp2_iso.py find GAME.iso NFSHP2/ZZDATA.BIN
    python hp2_iso.py write GAME.iso NFSHP2/ZZDATA.BIN new_ZZDATA.BIN
    python hp2_iso.py install-bank GAME.iso 321 modded_bank.bin
    python hp2_iso.py install GAME.iso 3 FE_ENGLISH_new.bin [--force]

`write` refuses unless the replacement is exactly the size of the file it
replaces. Work on a copy of your image; the original disc is the backup.

`install-bank` puts one archive entry back without copying the 3 GB archive
out of the image first. It reads ZDIR.BIN from inside the ISO to find where
entry N lives inside ZZDATA.BIN, then writes there. Same size rule applies.

`install` takes an archive entry of any size. If it still fits in the sectors
the entry already owns, it is written in place; if not, it goes after the
last file on the disc and ZDIR.BIN inside the image is pointed at it. The
game turns every ZDIR offset into an absolute disc sector at boot and never
checks it against the end of ZZDATA.BIN, so this works without moving
anything. When the image grows, the ISO9660 volume size and the UDF partition,
integrity table and closing anchor are updated to match.
"""
import os
import struct
import sys

SECTOR = 2048


def pvd(f):
    """The primary volume descriptor, at sector 16."""
    for s in range(16, 32):
        f.seek(s * SECTOR)
        b = f.read(SECTOR)
        if len(b) < 7:
            break
        if b[1:6] == b"CD001" and b[0] == 1:
            return b
        if b[0] == 255:
            break
    raise SystemExit("no ISO9660 primary volume descriptor - is this a disc image?")


def records(f, lba, length):
    """Directory records in the extent at `lba`."""
    f.seek(lba * SECTOR)
    data = f.read(length)
    p = 0
    while p < len(data):
        rl = data[p]
        if rl == 0:
            # the rest of this sector is padding; move to the next one
            p = (p // SECTOR + 1) * SECTOR
            if p >= len(data):
                return
            continue
        rec = data[p:p + rl]
        ext_lba = struct.unpack_from("<I", rec, 2)[0]
        size = struct.unpack_from("<I", rec, 10)[0]
        flags = rec[25]
        nlen = rec[32]
        name = rec[33:33 + nlen]
        if nlen == 1 and name in (b"\x00", b"\x01"):
            nm = "." if name == b"\x00" else ".."
        else:
            nm = name.decode("ascii", "replace").split(";")[0]
        yield nm, ext_lba, size, bool(flags & 2)
        p += rl


def walk(f, lba, length, prefix=""):
    for nm, elba, size, isdir in records(f, lba, length):
        if nm in (".", ".."):
            continue
        path = prefix + nm
        if isdir:
            yield path + "/", elba, size, True
            for sub in walk(f, elba, size, path + "/"):
                yield sub
        else:
            yield path, elba, size, False


def find(f, want):
    want = want.replace("\\", "/").strip("/").upper()
    root = pvd(f)
    rlba = struct.unpack_from("<I", root, 158)[0]
    rsize = struct.unpack_from("<I", root, 166)[0]
    for path, lba, size, isdir in walk(f, rlba, rsize):
        if not isdir and path.upper() == want:
            return lba, size
    return None, None


# ---------------------------------------------------------------- growing

ZZ_PATH = "NFSHP2/ZZDATA.BIN"
ZD_PATH = "NFSHP2/ZDIR.BIN"


def udf_crc(data):
    """CRC-CCITT (poly 0x1021, start 0), as UDF descriptor tags use."""
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def udf_retag(b, location=None):
    """Fix a UDF descriptor's tag after its body changed (and move it if asked)."""
    b = bytearray(b)
    if location is not None:
        struct.pack_into("<I", b, 12, location)
    crclen = struct.unpack_from("<H", b, 10)[0]
    struct.pack_into("<H", b, 8, udf_crc(b[16:16 + crclen]))
    b[4] = sum(b[i] for i in range(16) if i != 4) & 0xFF
    return bytes(b)


def udf_tag(b):
    """(tag id, location) when `b` starts with a valid UDF tag, else None."""
    if len(b) < 16 or b[4] != sum(b[i] for i in range(16) if i != 4) & 0xFF:
        return None
    return struct.unpack_from("<H", b, 0)[0], struct.unpack_from("<I", b, 12)[0]


def read_sector(f, s):
    f.seek(s * SECTOR)
    return f.read(SECTOR)


def write_sector(f, s, b):
    f.seek(s * SECTOR)
    f.write(b)


def grow_volume(f, old_total, new_total, log, end_sector=None):
    """
    The image is now `new_total` sectors long instead of `old_total`.
    Update the ISO9660 volume size and, when the disc also carries UDF, the
    partition length, the integrity size table and the closing anchor (UDF
    puts an anchor in the last sector, so it has to move to the new end).
    """
    p = bytearray(read_sector(f, 16))
    struct.pack_into("<I", p, 80, new_total)
    struct.pack_into(">I", p, 84, new_total)
    write_sector(f, 16, p)
    log.append("ISO9660 volume size %d -> %d sectors" % (old_total, new_total))

    main = udf_tag(read_sector(f, 256))
    if not main or main[0] != 2:
        return
    a = read_sector(f, 256)
    seqs = [struct.unpack_from("<II", a, 16), struct.unpack_from("<II", a, 24)]
    grew = new_total - old_total
    lvid = None
    for length, start in seqs:
        for s in range(start, start + length // SECTOR):
            b = read_sector(f, s)
            t = udf_tag(b)
            if not t:
                continue
            if t[0] == 5:                      # partition descriptor
                b = bytearray(b)
                plen = struct.unpack_from("<I", b, 192)[0]
                struct.pack_into("<I", b, 192, plen + grew)
                write_sector(f, s, udf_retag(b))
                log.append("UDF partition descriptor (sector %d) length %d -> %d"
                           % (s, plen, plen + grew))
            elif t[0] == 6:                    # logical volume: integrity extent
                ilen, iloc = struct.unpack_from("<II", b, 432)
                if ilen:
                    lvid = iloc
            elif t[0] == 8:
                break
    if lvid is not None:
        b = read_sector(f, lvid)
        t = udf_tag(b)
        if t and t[0] == 9:
            b = bytearray(b)
            n = struct.unpack_from("<I", b, 72)[0]
            for i in range(n):
                at = 80 + 4 * n + 4 * i        # size table follows free-space table
                size = struct.unpack_from("<I", b, at)[0]
                struct.pack_into("<I", b, at, size + grew)
            write_sector(f, lvid, udf_retag(b))
            log.append("UDF integrity size table updated (sector %d)" % lvid)

    end = end_sector if end_sector is not None else read_sector(f, old_total - 1)
    t = udf_tag(end)
    if t and t[0] == 2:
        write_sector(f, new_total - 1, udf_retag(end, new_total - 1))
        log.append("UDF closing anchor moved from sector %d to %d"
                   % (old_total - 1, new_total - 1))


def install(iso, idx, src, force=False):
    """
    Put archive entry `idx` back into the image at any size.

    The game reads ZDIR.BIN at boot, adds ZZDATA.BIN's start sector to every
    entry's offset, and from then on reads entries by absolute disc sector.
    Nothing checks an entry against the end of ZZDATA.BIN. So an entry that
    has outgrown its slot can live past the end of the archive, after every
    other file on the disc, as long as ZDIR.BIN points at it.

      fits in its slot (its sectors up to the next entry) -> written in place
      does not fit                                         -> appended at the
                                                              end of the image
    Either way the entry's size (and offset, when moved) is updated in the
    ZDIR.BIN inside the image.
    """
    new = open(src, "rb").read()
    log = []
    with open(iso, "r+b") as f:
        zd_lba, zd_size = find(f, ZD_PATH)
        zz_lba, zz_size = find(f, ZZ_PATH)
        if zd_lba is None or zz_lba is None:
            raise SystemExit("this image has no %s / %s" % (ZD_PATH, ZZ_PATH))
        f.seek(zd_lba * SECTOR)
        zd = f.read(zd_size)
        n = zd_size // 12
        if not 0 <= idx < n:
            raise SystemExit("entry %d is not in ZDIR.BIN (it holds %d entries)" % (idx, n))
        ents = [struct.unpack_from("<III", zd, i * 12) for i in range(n)]
        eid, off, size = ents[idx]
        if size == 0:
            raise SystemExit("entry %d is an empty placeholder" % idx)
        twins = [i for i, e in enumerate(ents) if e[0] == eid and i != idx]

        f.seek(0, 2)
        total = f.tell() // SECTOR
        vol = struct.unpack_from("<I", pvd(f), 80)[0]
        if total != vol:
            raise SystemExit("the image is %d sectors but its volume says %d;\n"
                             "refusing to guess where the disc ends" % (total, vol))

        start = zz_lba + off                       # absolute sector of the entry
        f.seek(start * SECTOR)
        if f.read(4) != new[:4] and not force:
            raise SystemExit("entry %d does not start like %s; nothing written.\n"
                             "Is the entry number right? (--force skips this check)"
                             % (idx, os.path.basename(src)))

        # its slot runs to the next thing on the disc after it
        later = [zz_lba + e[1] for e in ents if zz_lba + e[1] > start and e[2]]
        if start < zz_lba + (zz_size + SECTOR - 1) // SECTOR:
            later.append(zz_lba + (zz_size + SECTOR - 1) // SECTOR)
        last_sector = read_sector(f, total - 1)
        end_anchor = udf_tag(last_sector)
        disc_end = total - 1 if end_anchor and end_anchor[0] == 2 else total
        later.append(disc_end)
        slot = min(later) - start
        need = (len(new) + SECTOR - 1) // SECTOR

        past_archive = start >= zz_lba + (zz_size + SECTOR - 1) // SECTOR
        if need <= slot:
            at = start
            log.append("fits in its slot (%d of %d sectors): written in place" % (need, slot))
        elif past_archive and min(later) == disc_end:
            at = start
            log.append("already at the end of the image: extended in place")
        else:
            at = disc_end
            log.append("needs %d sectors, its slot has %d: moved to the end of the image"
                       % (need, slot))
        f.seek(at * SECTOR)
        f.write(new + b"\0" * (need * SECTOR - len(new)))

        new_total = max(total, at + need + (total - disc_end))
        if new_total != total:
            grow_volume(f, total, new_total, log, last_sector)
            f.truncate(new_total * SECTOR)

        f.seek(zd_lba * SECTOR + idx * 12)
        f.write(struct.pack("<III", eid, at - zz_lba, len(new)))
        log.insert(0, "entry %d (id %08x): %s -> %s bytes, sector %d -> %d of ZZDATA.BIN"
                   % (idx, eid, "{:,}".format(size), "{:,}".format(len(new)),
                      off, at - zz_lba))

    for line in log:
        print(line)
    if twins:
        print("note: entries %s share this id; the game reads whichever copy is\n"
              "nearest the last sector it read, so install the file into those too"
              % ", ".join(map(str, twins)))


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        print(__doc__)
        return
    cmd, iso = a[0], a[1]

    if cmd == "list":
        with open(iso, "rb") as f:
            root = pvd(f)
            rlba = struct.unpack_from("<I", root, 158)[0]
            rsize = struct.unpack_from("<I", root, 166)[0]
            for path, lba, size, isdir in walk(f, rlba, rsize):
                if isdir:
                    print("  %-44s <dir>" % path)
                else:
                    print("  %-44s %12s bytes   sector %-8d offset %s"
                          % (path, "{:,}".format(size), lba,
                             "{:,}".format(lba * SECTOR)))
        return

    if cmd == "find" and len(a) >= 3:
        with open(iso, "rb") as f:
            lba, size = find(f, a[2])
        if lba is None:
            raise SystemExit("%s is not in %s" % (a[2], os.path.basename(iso)))
        print("%s" % a[2])
        print("  sector %d, byte offset %s, size %s"
              % (lba, "{:,}".format(lba * SECTOR), "{:,}".format(size)))
        return

    if cmd == "write" and len(a) >= 4:
        src = a[3]
        new = open(src, "rb").read()
        with open(iso, "r+b") as f:
            lba, size = find(f, a[2])
            if lba is None:
                raise SystemExit("%s is not in %s" % (a[2], os.path.basename(iso)))
            if size != len(new):
                raise SystemExit(
                    "size mismatch: %s holds %s bytes, %s is %s. Replacements must be\n"
                    "the same size - hp2_music.py never changes a bank's size, so if\n"
                    "these differ something else did."
                    % (a[2], "{:,}".format(size), os.path.basename(src),
                       "{:,}".format(len(new))))
            at = lba * SECTOR
            f.seek(at)
            head = f.read(16)
            f.seek(at)
            f.write(new)
        print("wrote %s (%s bytes) into %s at offset %s"
              % (os.path.basename(src), "{:,}".format(len(new)),
                 os.path.basename(iso), "{:,}".format(at)))
        print("the image is unchanged in size and structure; boot it in your emulator")
        return

    if cmd == "install-bank" and len(a) >= 4:
        try:
            idx = int(a[2])
        except ValueError:
            raise SystemExit("the entry number must be a number, e.g. 321")
        src = a[3]
        new = open(src, "rb").read()
        zzpath = a[4] if len(a) > 4 else "NFSHP2/ZZDATA.BIN"
        zdpath = a[5] if len(a) > 5 else "NFSHP2/ZDIR.BIN"
        with open(iso, "r+b") as f:
            zd_lba, zd_size = find(f, zdpath)
            if zd_lba is None:
                raise SystemExit("%s is not in %s" % (zdpath, os.path.basename(iso)))
            zz_lba, zz_size = find(f, zzpath)
            if zz_lba is None:
                raise SystemExit("%s is not in %s" % (zzpath, os.path.basename(iso)))
            if (idx + 1) * 12 > zd_size:
                raise SystemExit("entry %d is not in %s (it holds %d entries)"
                                 % (idx, zdpath, zd_size // 12))
            f.seek(zd_lba * SECTOR + idx * 12)
            _id, off, size = struct.unpack_from("<III", f.read(12), 0)
            if size == 0:
                raise SystemExit("entry %d is an empty placeholder" % idx)
            if size != len(new):
                raise SystemExit(
                    "size mismatch: entry %d holds %s bytes, %s is %s. Replacements\n"
                    "must be the same size - hp2_music.py 'replace' never changes a\n"
                    "bank's size, so if these differ something else did."
                    % (idx, "{:,}".format(size), os.path.basename(src),
                       "{:,}".format(len(new))))
            if off * SECTOR + size > zz_size:
                raise SystemExit("entry %d runs past the end of %s" % (idx, zzpath))
            at = zz_lba * SECTOR + off * SECTOR
            f.seek(at)
            if f.read(4) != new[:4]:
                raise SystemExit("the bytes at offset %s do not start like this bank;\n"
                                 "nothing written. Is the entry number right?"
                                 % "{:,}".format(at))
            f.seek(at)
            f.write(new)
        print("entry %d (id %08x) is at sector %d of %s, %s bytes"
              % (idx, _id, off, os.path.basename(zzpath), "{:,}".format(size)))
        print("wrote %s into %s at offset %s"
              % (os.path.basename(src), os.path.basename(iso), "{:,}".format(at)))
        print("ZDIR.BIN was not touched; the image is unchanged in size and structure")
        return

    if cmd == "install" and len(a) >= 4:
        try:
            idx = int(a[2])
        except ValueError:
            raise SystemExit("the entry number must be a number, e.g. 3")
        install(iso, idx, a[3], force="--force" in a[4:])
        return

    print(__doc__)


if __name__ == "__main__":
    main()
