"""
Split ZZDATA.BIN into its individual files, using ZDIR.BIN as the index.

This is the first step: everything else in tools/ works on what this produces.

ZDIR.BIN is a flat table of 12-byte records, one per archive entry:

    offset 0   u32   id      a hash of the original path
    offset 4   u32   offset  in 2048-byte sectors
    offset 8   u32   size    in bytes

Files are written as <index>_<id>.<ext>, where the extension is guessed from
the first four bytes (.comp for compressed, .avi for video, .bin otherwise).
Keeping the leading index matters: hp2_music.py reads it back to find the
entry again when installing a modified bank.

Alongside them it writes names.txt, mapping each entry to the real path the
game knows it by, where that is known. Those came from hashing candidate
strings out of the executable against the ids (see tools/archive_names.csv and
docs/formats.md section 10); about two thirds of the archive is covered.

Entries with size 0 are placeholders and are skipped.

Usage:
    python hp2_extract.py <folder containing ZDIR.BIN and ZZDATA.BIN> [-o OUT]
    python hp2_extract.py NFSHP2 -o extracted
    python hp2_extract.py NFSHP2 --list      # just show the table, write nothing

Needs about as much free space as ZZDATA.BIN itself (roughly 3 GB).
"""
import os
import struct
import sys

SECTOR = 2048
EXT = {
    b"COMP": ".comp",
    b"RIFF": ".riff",       # refined to .avi below when it is one
    b"VAGp": ".vag",
}


def read_index(zdir_path):
    d = open(zdir_path, "rb").read()
    if len(d) % 12:
        raise SystemExit("%s is %d bytes, not a multiple of 12 - is this really ZDIR.BIN?"
                         % (os.path.basename(zdir_path), len(d)))
    return [struct.unpack_from("<III", d, i * 12) for i in range(len(d) // 12)]


def load_names():
    """entry id (hex) -> original path, from archive_names.csv beside this script."""
    import csv
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "archive_names.csv")
    out = {}
    try:
        with open(p, newline="") as f:
            for row in csv.DictReader(f):
                if row.get("name"):
                    out[row["id"].lower()] = row["name"]
    except OSError:
        pass
    return out


def guess_ext(head):
    if head[:4] == b"RIFF":
        return ".avi" if head[8:12] == b"AVI " else ".wav" if head[8:12] == b"WAVE" else ".riff"
    return EXT.get(head[:4], ".bin")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    listing = "--list" in sys.argv
    out = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--out=")), None)
    if out is None and "-o" in sys.argv:
        i = sys.argv.index("-o")
        if i + 1 < len(sys.argv):
            out = sys.argv[i + 1]
            if out in args:
                args.remove(out)
    if not args:
        print(__doc__)
        return
    src = args[0]
    out = out or "extracted"

    zdir = os.path.join(src, "ZDIR.BIN")
    zz = os.path.join(src, "ZZDATA.BIN")
    for p in (zdir, zz):
        if not os.path.isfile(p):
            raise SystemExit("cannot find %s" % p)

    ents = read_index(zdir)
    total = os.path.getsize(zz)
    print("%d entries in ZDIR.BIN, %s bytes in ZZDATA.BIN"
          % (len(ents), "{:,}".format(total)))

    # validate before writing anything
    bad = [i for i, (_id, off, sz) in enumerate(ents)
           if sz and off * SECTOR + sz > total]
    if bad:
        raise SystemExit("entries %s point past the end of ZZDATA.BIN; "
                         "the two files may not belong together"
                         % ", ".join(str(b) for b in bad[:8]))
    empty = sum(1 for _id, _off, sz in ents if sz == 0)
    print("%d real files, %d empty placeholders" % (len(ents) - empty, empty))

    known = load_names()
    if known:
        print("%d of these entries have a known original path" % len(known))
    if not listing:
        os.makedirs(out, exist_ok=True)
    written = 0
    manifest = []
    with open(zz, "rb") as f:
        for i, (eid, off, sz) in enumerate(ents):
            if sz == 0:
                continue
            f.seek(off * SECTOR)
            if listing:
                head = f.read(16)
                print("  %3d  id %08x  sector %-8d %12s bytes  %s%s"
                      % (i, eid, off, "{:,}".format(sz), head[:4].hex(" "),
                         "  " + guess_ext(head)))
                continue
            name = "%03d_%08x%s" % (i, eid, guess_ext(f.read(16)))
            f.seek(off * SECTOR)
            path = os.path.join(out, name)
            with open(path, "wb") as o:
                left = sz
                while left:
                    chunk = f.read(min(1 << 22, left))   # 4 MB at a time
                    if not chunk:
                        raise SystemExit("ZZDATA.BIN ended early while writing %s" % name)
                    o.write(chunk)
                    left -= len(chunk)
            manifest.append((i, eid, sz, known.get("%08x" % eid, "")))
            written += 1
            if written % 50 == 0:
                print("  %d files..." % written, flush=True)

    if listing:
        return
    with open(os.path.join(out, "names.txt"), "w") as f:
        f.write("# entry  id        size          original path (blank = not recovered)\n")
        for i, eid, sz, nm in manifest:
            f.write("%3d  %08x  %12d  %s\n" % (i, eid, sz, nm))
    print("extracted %d files to %s/  (names.txt lists their original paths)" % (written, out))
    print("next:  python hp2_comp.py %s" % out)


if __name__ == "__main__":
    main()
