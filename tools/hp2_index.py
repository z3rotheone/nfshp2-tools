"""
Build an index of everything extracted from ZZDATA.BIN.

For each raw file in <root> and each .dec file in <root>\\decomp it records:
size, first 8 bytes (hex), and up to 4 readable path-like strings.
(COMP files themselves are skipped; their .dec output is indexed instead.)

Usage:  python hp2_index.py [extracted_dir]
Output: index.txt in the parent folder of extracted_dir
"""
import os
import re
import sys

STR = re.compile(rb"[\x20-\x7e]{6,}")
PATHLIKE = re.compile(rb"[\\/]|\.[A-Za-z0-9]{2,4}\b")
SAMPLE = 4 * 1024 * 1024  # look at the first 4 MB for strings


def describe(path):
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        head = f.read(SAMPLE)
    magic = head[:8].hex(" ")
    found = []
    for m in STR.finditer(head):
        s = m.group()
        if PATHLIKE.search(s):
            found.append(s.decode("ascii")[:70])
            if len(found) == 4:
                break
    return size, magic, found


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "extracted"
    files = []
    for n in sorted(os.listdir(root)):
        fp = os.path.join(root, n)
        if os.path.isfile(fp) and not n.endswith(".comp"):
            files.append(fp)
    dec = os.path.join(root, "decomp")
    if os.path.isdir(dec):
        for n in sorted(os.listdir(dec)):
            files.append(os.path.join(dec, n))

    outp = os.path.join(os.path.dirname(root.rstrip("\\/")), "index.txt")
    with open(outp, "w", encoding="utf-8") as o:
        for fp in files:
            size, magic, found = describe(fp)
            rel = os.path.relpath(fp, root)
            o.write("%s | %d | %s\n" % (rel, size, magic))
            for s in found:
                o.write("    %s\n" % s)
    print("wrote", outp, "-", len(files), "files indexed")


if __name__ == "__main__":
    main()
