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

`write` refuses unless the replacement is exactly the size of the file it
replaces. Work on a copy of your image; the original disc is the backup.

`install-bank` puts one archive entry back without copying the 3 GB archive
out of the image first. It reads ZDIR.BIN from inside the ISO to find where
entry N lives inside ZZDATA.BIN, then writes there. Same size rule applies.
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

    print(__doc__)


if __name__ == "__main__":
    main()
