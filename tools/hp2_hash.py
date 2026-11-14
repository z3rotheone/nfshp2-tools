"""
The name hash used throughout Need for Speed: Hot Pursuit 2 (PS2).

    h = 0xFFFFFFFF
    for each byte:  h = (h * 33 + byte) & 0xFFFFFFFF

That is djb2 with a seed of -1 rather than the usual 5381. It is confirmed
twice over: it reproduces the hash stored beside every texture name in a .tpk
index, and the routine itself is in the executable in three forms —

    0x103520   hash(data, length)
    0x2534a8   hash(string)                 seeds with 0xFFFFFFFF
    0x2534e8   hash_continue(string, seed)  carries an existing hash on

The third exists so a name can be hashed in pieces. The game uses it to look
textures up by composed name, e.g. at 0x198200 it builds a car's badge as
hash("CARLOGO_" + "CORVETTE").

Because 33 is odd it is invertible modulo 2^32, so the seed that a known
suffix must have been given can be solved for exactly — see `solve`, which is
how you work backwards from a hash toward an unknown prefix.

Usage:
    python hp2_hash.py NAME...              hash each name
    python hp2_hash.py --check PACK.tpk     verify a pack's stored hashes
    python hp2_hash.py --crack HASH -w LIST try a wordlist against a hash
    python hp2_hash.py --solve HASH SUFFIX  the seed SUFFIX must have started from
"""
import os
import struct
import sys

MASK = 0xFFFFFFFF
SEED = 0xFFFFFFFF


def name_hash(s, seed=SEED):
    """The game's hash. `s` may be str (encoded ASCII) or bytes."""
    if isinstance(s, str):
        s = s.encode("ascii", "replace")
    h = seed
    for b in s:
        h = (h * 33 + b) & MASK
    return h


def solve_seed(target, suffix):
    """The seed that `suffix` must have been hashed from to reach `target`.

    hash(prefix + suffix) == hash_continue(suffix, hash(prefix)), and the
    recurrence is linear, so  target = seed * 33^len + tail(suffix)  and the
    seed follows by dividing out 33^len, which is invertible mod 2^32.
    """
    if isinstance(suffix, str):
        suffix = suffix.encode("ascii", "replace")
    tail = 0
    for b in suffix:
        tail = (tail * 33 + b) & MASK
    inv = pow(pow(33, len(suffix), 1 << 32), -1, 1 << 32)
    return ((target - tail) & MASK) * inv % (1 << 32)


def pack_names(path):
    """(name, stored hash) for every texture in a .tpk pack."""
    d = open(path, "rb").read()
    out = []

    def walk(start, end):
        p = start
        while p + 8 <= end:
            cid, sz = struct.unpack_from("<II", d, p)
            if p + 8 + sz > end:
                return
            if cid == 0x30300003:
                for i in range(sz // 164):
                    b = d[p + 8 + i * 164: p + 8 + (i + 1) * 164]
                    nm = b[8:32].split(b"\0")[0].decode("ascii", "replace")
                    out.append((nm, struct.unpack_from("<I", b, 32)[0]))
            if cid & 0x80000000 and sz:
                walk(p + 8, p + 8 + sz)
            p += 8 + sz

    walk(0, len(d))
    return out


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return

    if a[0] == "--check":
        total = ok = 0
        for path in a[1:]:
            names = pack_names(path)
            good = sum(1 for n, h in names if name_hash(n) == h)
            total += len(names)
            ok += good
            flag = "" if good == len(names) else "   <-- MISMATCH"
            print("%-34s %3d/%-3d names hash correctly%s"
                  % (os.path.basename(path), good, len(names), flag))
            for n, h in names:
                if name_hash(n) != h:
                    print("      %-24s stored %08x  computed %08x" % (n, h, name_hash(n)))
        if total:
            print("\n%d of %d verified" % (ok, total))
        return

    if a[0] == "--crack" and len(a) >= 2:
        target = int(a[1], 16)
        words = []
        if "-w" in a:
            words = [l.strip() for l in open(a[a.index("-w") + 1]) if l.strip()]
        else:
            print("give a wordlist with -w FILE")
            return
        found = [w for w in words
                 for v in (w, w.upper(), w.lower())
                 if name_hash(v) == target]
        print("%d candidates for %08x: %s" % (len(found), target, found or "none"))
        return

    if a[0] == "--solve" and len(a) >= 3:
        target = int(a[1], 16)
        suffix = a[2]
        seed = solve_seed(target, suffix)
        print("for %r to end at %08x it must have been hashed from %08x"
              % (suffix, target, seed))
        print("(a seed of %08x means there was no prefix)" % SEED)
        if seed == SEED:
            print("-> the whole hashed string is exactly that suffix")
        return

    for s in a:
        print("%08x  %s" % (name_hash(s), s))


if __name__ == "__main__":
    main()
