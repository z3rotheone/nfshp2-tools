#!/usr/bin/env python3
"""
Speech event definitions: archive entry 367 (no file name on the disc).

This is the source-side description of the police speech system: event names,
their parameters and the line names. It is NOT SOUND\\SPEECH\\EVENTS.EVT (entries
18 and 225, 48,092 bytes), which is the compiled rule data the game reads.

Layout (all little-endian):
    +0x00  u32 0x3E8      version / magic
    +0x04  u32 201        number of events
    +0x08  u32 69         number of parameter types
    +0x0C  u32 0x1FC      (unread)
    +0x10  u32 0x1C       offset of the event records
    +0x14  u32 0x5E54     offset of the parameter-type blocks
    +0x18  u32 0x29AD0    offset of the line-name records

    events, 120 bytes each:  char name[48]; 18 x s32:
        [0..3]  0
        [4]     event id (not the record number: ids skip 18, 31...)
        [5..]   parameter type ids, in the order the game supplies them,
                padded with 0 (a real id 0 = arrestType is told apart by
                position: the list ends at the last non-zero entry, except
                for 'arrest', whose list is version, arrestType, cop)

    parameter-type blocks, 2,124 bytes each:
        u32 type id, u32 number of values, u32 kind (0 enum, 1/2 numbers,
        3 location, 4 count), char name[64], then 32 x char value[64]
        ("SPCH1Type_cop_cop27"...), unused slots zero.

    line-name records, 72 bytes each (508):
        u32 line number, u32 takes, u32 4, u32 flag, 24 zero bytes,
        char name[32]            ("19chat", "SupAddDown", "31AddCount"...)
    The line number is word 0 of the line's descriptor in <LANG>BAN.PCK, so
    this names the lines. `takes` equals byte 4 of that descriptor.

BAN descriptor, start: u16 line number, u16 variant (0xFFFF for the main
clip of a line; other values number the extra clips that share its name, e.g.
spoken numbers), u8 takes, u8 cue count, then 16 bytes of timing and
(count x 5 bytes) cue marks: u16 time in ms, u8 index, u8 1, u8 flag; the
list ends with the bytes 70 70. About 480 line numbers cover the ~1,800 clips.

The mapping from an event's parameter values to a line number is NOT in this
file; it is compiled into the game. What this file gives is the vocabulary:
which events exist, what they are told, and what each line is called.

Usage:
    hp2_events.py EVENTS.EVT                    events and their parameters
    hp2_events.py EVENTS.EVT --types            parameter types and values
    hp2_events.py EVENTS.EVT --lines BAN.PCK    every BAN line with its name
                                                (CSV: index,line,name,variant,takes,cues)
"""
import struct
import sys


def parse(b):
    n_ev, n_ty = struct.unpack_from("<II", b, 4)
    ev_off, ty_off, ln_off = struct.unpack_from("<III", b, 0x10)
    types = {}
    o = ty_off
    for _ in range(n_ty):
        tid, nvals, kind = struct.unpack_from("<3I", b, o)
        name = b[o + 12:o + 76].split(b"\0")[0].decode("latin1")
        vals = []
        for j in range(32):
            v = b[o + 76 + 64 * j:o + 140 + 64 * j].split(b"\0")[0].decode("latin1")
            if v:
                vals.append(v)
        types[tid] = dict(id=tid, name=name, count=nvals, kind=kind, values=vals)
        o += 2124
    events = []
    for i in range(n_ev):
        r = b[ev_off + 120 * i:ev_off + 120 * i + 120]
        name = r[:48].split(b"\0")[0].decode("latin1")
        w = struct.unpack_from("<18i", r, 48)
        ps = list(w[5:])
        while ps and ps[-1] == 0:
            ps.pop()
        if name == "arrest" and not ps:
            ps = [0]
        events.append(dict(name=name, id=w[4], params=ps))
    lines = {}
    o = ln_off
    while o + 72 <= len(b):
        num, takes, _four, flag = struct.unpack_from("<4I", b, o)
        name = b[o + 40:o + 72].split(b"\0")[0].decode("latin1")
        lines[num] = dict(number=num, name=name, takes=takes, flag=flag)
        o += 72
    return events, types, lines


def ban_lines(ban, lines):
    """Every line in a BAN.PCK: index, audio offset, line number, variant."""
    n = struct.unpack_from("<I", ban, 0)[0]
    out = []
    for i in range(n):
        _tag, dat, _z, desc = struct.unpack_from("<4I", ban, 8 + 16 * i)
        if desc + 8 > len(ban):
            continue
        num, sub, takes, cues = struct.unpack_from("<HHBB", ban, desc)
        if num not in lines:
            continue                      # the closing record is not a line
        out.append(dict(index=i, dat=dat, line=num, sub=sub, takes=takes, cues=cues))
    return out


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    b = open(a[0], "rb").read()
    events, types, lines = parse(b)
    if "--types" in a:
        for t in types.values():
            print("%3d %-24s kind %d  %d values" % (t["id"], t["name"], t["kind"], t["count"]))
            for v in t["values"]:
                print("        " + v)
        return
    if "--lines" in a:
        ban = open(a[a.index("--lines") + 1], "rb").read()
        print("index,line,name,variant,takes,cues")
        for l in ban_lines(ban, lines):
            print("%d,%d,%s,%s,%d,%d" % (l["index"], l["line"], lines[l["line"]]["name"],
                  "" if l["sub"] == 0xFFFF else l["sub"], l["takes"], l["cues"]))
        return
    for e in events:
        ps = ", ".join("%s%s" % (types[p]["name"], "[%d]" % types[p]["count"] if types[p]["count"] else "")
                       if p in types else "?%d" % p for p in e["params"])
        print("%3d %-34s %s" % (e["id"], e["name"], ps))


if __name__ == "__main__":
    main()
