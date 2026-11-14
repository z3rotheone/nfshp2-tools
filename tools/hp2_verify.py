"""
Check that a patched disc image really changed, and find out what the game
reads instead when it didn't.

A mod that boots but behaves as though nothing was patched has only three
causes, and this tells them apart:

  check   diff a patched image against the original and map every differing
          run back to the archive entry it falls inside. If nothing differs,
          the patch never landed. If the right entry differs, the patch landed
          and the game is reading something else.

  banks   list every archive entry that parses as a music bank. A second bank
          holding the same music is the usual "something else".

  hunt    take one track out of an unmodified bank, fingerprint its audio, and
          find every copy of it in the image. Copies outside the entry you
          patched are what the game is still playing.

Usage:
    python hp2_verify.py check modded.iso --orig game.iso
    python hp2_verify.py check modded.iso --orig game.iso --entry 321
    python hp2_verify.py banks modded.iso
    python hp2_verify.py hunt  modded.iso 321_ad921759.bin 15

Reads only; it never writes to either image.
"""
import bisect
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hp2_iso import SECTOR, find                     # noqa: E402

CHUNK = 8 << 20
ENTRY_TABLE, ENTRY_STRIDE = 0x60, 96


# ------------------------------------------------------------------ disc layout
def layout(path):
    """Where ZZDATA.BIN lives in the image, and the archive index beside it."""
    f = open(path, "rb")
    zd_lba, zd_size = find(f, "NFSHP2/ZDIR.BIN")
    zz_lba, zz_size = find(f, "NFSHP2/ZZDATA.BIN")
    if zd_lba is None or zz_lba is None:
        raise SystemExit("%s does not hold NFSHP2/ZDIR.BIN and ZZDATA.BIN - is it "
                         "a Hot Pursuit 2 disc?" % os.path.basename(path))
    f.seek(zd_lba * SECTOR)
    z = f.read(zd_size)
    ents = []
    for i in range(len(z) // 12):
        _id, off, size = struct.unpack_from("<III", z, i * 12)
        if size:
            ents.append(dict(i=i, id=_id, at=zz_lba * SECTOR + off * SECTOR, size=size))
    return f, dict(zz_at=zz_lba * SECTOR, zz_size=zz_size, zd_at=zd_lba * SECTOR,
                   zd_size=zd_size, entries=ents)


def entry_at(ents, pos):
    """The archive entry containing an absolute image offset, if any."""
    for e in ents:
        if e["at"] <= pos < e["at"] + e["size"]:
            return e
    return None


def names():
    """index -> original path, from archive_names.csv if it is beside us."""
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "archive_names.csv")
    out = {}
    if not os.path.exists(p):
        return out
    for line in open(p, encoding="utf-8").read().splitlines()[1:]:
        c = line.split(",")
        if len(c) >= 2 and c[0].strip().isdigit():
            out[int(c[0])] = c[-1].strip()
    return out


def progress(done, total):
    """A progress line, but only to a terminal - it would litter a pasted log."""
    if sys.stderr.isatty():
        sys.stderr.write("\r  %3d%%" % (100 * done // max(1, total)))


def progress_done():
    if sys.stderr.isatty():
        sys.stderr.write("\r      \r")


def add(buckets, key, e, count, first, last):
    v = buckets.get(key)
    if v is None:
        buckets[key] = [count, first, last, e]
    else:
        v[0] += count
        v[1] = min(v[1], first)
        v[2] = max(v[2], last)


def describe(e, nm):
    return "entry %d (id %08x%s)" % (e["i"], e["id"],
                                     ", " + nm[e["i"]] if e["i"] in nm else "")


# ------------------------------------------------------------------------ check
def cmd_check(iso, orig, only=None):
    if os.path.getsize(iso) != os.path.getsize(orig):
        print("the two images are different sizes (%s vs %s) - these are not the same"
              % ("{:,}".format(os.path.getsize(iso)), "{:,}".format(os.path.getsize(orig))))
        return
    fa, L = layout(iso)
    fb = open(orig, "rb")
    nm = names()
    ents = L["entries"]

    lo, hi = 0, os.path.getsize(iso)
    if only is not None:
        e = next((x for x in ents if x["i"] == only), None)
        if e is None:
            raise SystemExit("entry %d is not in the archive index" % only)
        lo, hi = e["at"], e["at"] + e["size"]
        print("comparing only %s: %s bytes at offset %s"
              % (describe(e, nm), "{:,}".format(e["size"]), "{:,}".format(e["at"])))
    else:
        print("comparing the whole image, %s bytes" % "{:,}".format(hi))

    sys.stdout.flush()
    # Differing bytes, bucketed by the archive entry they fall in. A patched
    # region is never one solid run - about one byte in 256 matches the old
    # data by chance - so runs are useless and totals are what matter.
    byat = sorted(ents, key=lambda e: e["at"])
    starts = [e["at"] for e in byat]
    buckets = {}
    total = 0
    pos = lo
    fa.seek(lo)
    fb.seek(lo)
    while pos < hi:
        n = min(CHUNK, hi - pos)
        a, b = fa.read(n), fb.read(n)
        if a != b:
            d = np.frombuffer(a, dtype=np.uint8) != np.frombuffer(b, dtype=np.uint8)
            total += int(d.sum())
            # entries overlapping this chunk: the last one starting at or
            # before pos, then forward while they still start before the end
            j = max(0, bisect.bisect_right(starts, pos) - 1)
            while j < len(byat) and byat[j]["at"] < pos + n:
                e = byat[j]
                j += 1
                s = max(e["at"], pos)
                t = min(e["at"] + e["size"], pos + n)
                if t <= s:
                    continue
                sub = d[s - pos:t - pos]
                c = int(sub.sum())
                if not c:
                    continue
                add(buckets, e["i"], e, c,
                    s + int(np.argmax(sub)),
                    t - 1 - int(np.argmax(sub[::-1])))
                d[s - pos:t - pos] = False      # so the remainder is "outside"
            c = int(d.sum())
            if c:
                add(buckets, None, None, c,
                    pos + int(np.argmax(d)), pos + n - 1 - int(np.argmax(d[::-1])))
        pos += n
        progress(pos - lo, hi - lo)
    progress_done()

    if not total:
        print()
        if only is None:
            print("NOTHING DIFFERS. The two images are byte-identical, so the patch never")
            print("reached this file. Check that install-bank printed its three lines, and")
            print("that you patched the copy you are booting.")
        else:
            print("This entry is byte-identical in the two images - it was not patched.")
        return

    print()
    print("%s bytes differ, in %d archive entr%s:"
          % ("{:,}".format(total), len(buckets), "y" if len(buckets) == 1 else "ies"))
    for key in sorted(buckets, key=lambda k: (k is None, k)):
        n, first, last, e = buckets[key]
        if e is None:
            print("  outside any archive entry: %s bytes, offsets %s to %s"
                  % ("{:,}".format(n), "{:,}".format(first), "{:,}".format(last)))
            continue
        print("  %s" % describe(e, nm))
        print("      %s of %s bytes differ, between +%s and +%s"
              % ("{:,}".format(n), "{:,}".format(e["size"]),
                 "{:,}".format(first - e["at"]), "{:,}".format(last - e["at"])))
    print()
    print("The patch is in the image. If the game still plays the old audio it is")
    print("reading a different copy - try 'banks' and 'hunt'.")


# ------------------------------------------------------------------------ banks
def music_bank(f, e):
    """(count, rate, channels) if this entry parses as a music bank, else None."""
    f.seek(e["at"])
    head = f.read(min(0x60, e["size"]))
    if len(head) < 0x24:
        return None
    channels, rate = struct.unpack_from("<II", head, 4)
    count = struct.unpack_from("<I", head, 0x20)[0]
    if channels not in (1, 2) or not (2000 <= rate <= 48000) or not (1 <= count <= 4096):
        return None
    need = ENTRY_TABLE + count * ENTRY_STRIDE
    if need > e["size"]:
        return None
    f.seek(e["at"] + ENTRY_TABLE)
    tbl = f.read(count * ENTRY_STRIDE)
    off, size, pad = struct.unpack_from("<III", tbl, 0)
    if pad != 0 or off + 48 > e["size"]:
        return None
    f.seek(e["at"] + off)
    if f.read(4) != b"VAGp":
        return None
    return count, rate, channels, tbl


def cmd_banks(iso):
    f, L = layout(iso)
    nm = names()
    found = 0
    for e in L["entries"]:
        m = music_bank(f, e)
        if not m:
            continue
        found += 1
        count, rate, channels, tbl = m
        audio = sum(struct.unpack_from("<III", tbl, i * ENTRY_STRIDE)[1] for i in range(count))
        print("%s" % describe(e, nm))
        print("    %s bytes at offset %s, %d tracks, %d Hz, %s"
              % ("{:,}".format(e["size"]), "{:,}".format(e["at"]), count, rate,
                 "stereo" if channels == 2 else "mono"))
        print("    %s bytes of audio (%.0f%% of the entry)"
              % ("{:,}".format(audio), 100.0 * audio / e["size"]))
        for i in range(min(count, 3)):
            o = i * ENTRY_STRIDE
            t_off, t_size, _ = struct.unpack_from("<III", tbl, o)
            t_nm = tbl[o + 12:o + ENTRY_STRIDE].split(b"\0")[0].decode("ascii", "replace").strip()
            print("      %2d  %12s bytes  %s" % (i, "{:,}".format(t_size), t_nm))
        if count > 3:
            print("      ... %d more" % (count - 3))
    if not found:
        print("no entry in this image parses as a music bank")
    elif found > 1:
        print()
        print("More than one music bank. If the one you patched is not the one the game")
        print("loads, patching the other is the fix.")


# ------------------------------------------------------------------------- hunt
def cmd_hunt(iso, bank_path, track):
    d = open(bank_path, "rb").read()
    count = struct.unpack_from("<I", d, 0x20)[0]
    if not 0 <= track < count:
        raise SystemExit("%s holds %d tracks, so %d is out of range"
                         % (os.path.basename(bank_path), count, track))
    o = ENTRY_TABLE + track * ENTRY_STRIDE
    t_off, t_size, _ = struct.unpack_from("<III", d, o)
    t_nm = d[o + 12:o + ENTRY_STRIDE].split(b"\0")[0].decode("ascii", "replace").strip()

    # a fingerprint from a quarter of the way into the audio, past the VAG header
    # and past any leading silence, and long enough not to collide by chance
    start = t_off + 48 + (t_size // 4 & ~0xF)
    needle = d[start:start + 4096]
    if len(needle) < 4096 or not any(needle):
        raise SystemExit("track %d has no distinctive audio to fingerprint" % track)

    print("hunting for track %d, %s" % (track, t_nm))
    print("  fingerprint: 4,096 bytes from offset %s of the bank" % "{:,}".format(start))
    sys.stdout.flush()

    f, L = layout(iso)
    nm = names()
    size = os.path.getsize(iso)
    hits = []
    pos, tail = 0, b""
    f.seek(0)
    while pos < size:
        buf = f.read(CHUNK)
        if not buf:
            break
        hay = tail + buf
        base = pos - len(tail)
        k = hay.find(needle)
        while k != -1:
            hits.append(base + k)
            k = hay.find(needle, k + 1)
        tail = hay[-(len(needle) - 1):]
        pos += len(buf)
        progress(pos, size)
    progress_done()

    if not hits:
        print("  no copy of this audio is in the image at all - the bank you")
        print("  fingerprinted did not come from this disc.")
        return
    print("  %d cop%s in the image:" % (len(hits), "y" if len(hits) == 1 else "ies"))
    for h in hits:
        e = entry_at(L["entries"], h)
        where = describe(e, nm) + " +%s" % "{:,}".format(h - e["at"]) if e else "outside any archive entry"
        print("    offset %-14s %s" % ("{:,}".format(h), where))
    if len(hits) > 1:
        print()
        print("  Every copy has to be patched, or the game may load any of them.")


# -------------------------------------------------------------------------- cli
def main():
    a = sys.argv[1:]
    orig = entry = None
    if "--orig" in a:
        i = a.index("--orig")
        orig = a[i + 1]
        del a[i:i + 2]
    if "--entry" in a:
        i = a.index("--entry")
        entry = int(a[i + 1])
        del a[i:i + 2]
    if not a:
        print(__doc__)
        return
    if a[0] == "check" and len(a) == 2 and orig:
        cmd_check(a[1], orig, entry)
    elif a[0] == "banks" and len(a) == 2:
        cmd_banks(a[1])
    elif a[0] == "hunt" and len(a) == 4:
        cmd_hunt(a[1], a[2], int(a[3]))
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
