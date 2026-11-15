#!/usr/bin/env python3
"""
Menu and HUD text: LANGUAGES\\FE_<LANGUAGE>.BIN (entries 3-10).

One chunk 0x00039000:
    +0x08  2 x u32   (load-time values; kept as they are)
    +0x10  char[68]  the file's development path
    +0x54  u32 n     number of strings (2,437 in every language)
    +0x58  u32 n     again
    +0x5C  u32       text size in bytes
    +0x60  3 x u32   (load-time values; kept)
    +0x6C  n x (u32 key, u32 offset)   sorted by key as signed 32-bit, for a
                     binary search. The key is the game's name hash
                     (hp2_hash.py) of the English wording: it matches the
                     English text for 2,218 of 2,437 strings (the rest were
                     reworded later). Every language has the same keys.
    then   n x (u32 id, u32 offset)    sorted by id: 10000, 10001...
    then   the text: NUL-terminated strings, Latin-1, in no particular order.
FE_ID.BIN is the same with each string's id as its text (a debug language).

Usage:
    hp2_lang.py FE_ENGLISH.BIN                      list id, key, text
    hp2_lang.py FE_ENGLISH.BIN --json OUT.json      every string as {id, key, text}
    hp2_lang.py FE_ENGLISH.BIN --write IN.json OUT  rebuild with the texts from
                                                    IN.json (by id); keys and
                                                    ids stay as they are
    hp2_lang.py FE_ENGLISH.BIN --check              rebuild unchanged and compare

Confirmed in game: an edited string installed with
`hp2_iso.py install-bank COPY.iso 3 FILE` shows in PCSX2. The file must keep
its size for that, so an edit can only grow a string by what others shrink.
"""
import json
import struct
import sys

HDR = 0x6C


def parse(d):
    if struct.unpack_from("<I", d, 0)[0] != 0x00039000:
        raise SystemExit("not a language file (chunk 0x00039000)")
    n, n2, tsize = struct.unpack_from("<3I", d, 0x54)
    t1 = HDR
    t2 = t1 + 8 * n
    tx = t2 + 8 * n
    keys = [struct.unpack_from("<2I", d, t1 + 8 * i) for i in range(n)]
    ids = [struct.unpack_from("<2I", d, t2 + 8 * i) for i in range(n)]
    text = d[tx:tx + tsize]

    def at(o):
        return text[o:text.index(b"\0", o)].decode("latin1")
    key_of = {}
    for k, o in keys:
        key_of.setdefault(o, k)
    strings = [dict(id=i, key=key_of.get(o), offset=o, text=at(o)) for i, o in ids]
    return dict(n=n, n2=n2, tsize=tsize, tail=d[tx + tsize:], keys=keys, strings=strings)


def build(d, info, texts=None):
    """The file with the given texts (id -> str). Every string in the text area
    is kept in its place and order, including the few that only the key table
    points at; keys and ids stay."""
    texts = texts or {}
    tx = HDR + 16 * info["n"]
    text = d[tx:tx + info["tsize"]]
    by_off = {}
    for s in info["strings"]:
        by_off.setdefault(s["offset"], []).append(s)
    blob = bytearray()
    new_off = {}
    extra = []                                   # (id, text) needing their own copy
    o = 0
    while o < len(text):
        j = text.find(b"\0", o)
        j = len(text) if j < 0 else j
        old = text[o:j]
        users = by_off.get(o, [])
        wanted = {texts.get(s["id"], s["text"]) for s in users}
        if len(wanted) == 1 and users:
            new = next(iter(wanted)).encode("latin1")
        else:
            new = old
            for s in users:
                if s["id"] in texts and texts[s["id"]] != s["text"]:
                    extra.append(s["id"])
        new_off[o] = len(blob)
        blob += new + b"\0"
        o = j + 1
    ids = []
    for s in info["strings"]:
        if s["id"] in extra:
            ids.append((s["id"], len(blob)))
            blob += texts[s["id"]].encode("latin1") + b"\0"
        else:
            ids.append((s["id"], new_off[s["offset"]]))
    keys = [(k, new_off.get(o, o)) for k, o in info["keys"]]
    out = bytearray(d[:HDR])
    struct.pack_into("<I", out, 0x5C, len(blob))
    for k, o in keys:
        out += struct.pack("<2I", k, o)
    for i, o in ids:
        out += struct.pack("<2I", i, o)
    out += blob + info["tail"]
    out += b"\0" * ((-len(out)) % 4)
    struct.pack_into("<I", out, 4, len(out) - 8)
    return bytes(out)


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    d = open(a[0], "rb").read()
    info = parse(d)
    if "--check" in a:
        same = build(d, info) == d
        print("%s: %d strings; rebuilt unchanged: %s" % (a[0], info["n"], "identical" if same else "DIFFERENT"))
        return
    if "--json" in a:
        out = a[a.index("--json") + 1]
        json.dump([{k: s[k] for k in ("id", "key", "text")} for s in info["strings"]],
                  open(out, "w", encoding="utf-8"), ensure_ascii=False, indent=0)
        print("wrote %s: %d strings" % (out, info["n"]))
        return
    if "--write" in a:
        i = a.index("--write")
        src, out = a[i + 1], a[i + 2]
        texts = {s["id"]: s["text"] for s in json.load(open(src, encoding="utf-8"))}
        changed = {k: v for k, v in texts.items()
                   if any(s["id"] == k and s["text"] != v for s in info["strings"])}
        open(out, "wb").write(build(d, info, changed))
        print("wrote %s: %d strings changed" % (out, len(changed)))
        return
    for s in info["strings"]:
        print("%d  %08x  %s" % (s["id"], s["key"] or 0, s["text"]))


if __name__ == "__main__":
    main()
