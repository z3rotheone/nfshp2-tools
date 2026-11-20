#!/usr/bin/env python3
"""
Front-end screen layouts: chunk 0x80010200 of FRONTEND\\FRONTB.BUN (entry 21).

The menus ("Flailer" in the game's own strings) are laid out as data: 41
screens on a 640 x 480 canvas, each a list of widgets with a name, a
position and either a text string or a texture. What a screen DOES (its
handlers, how many rows it really has, what left/right changes) is code in
the executable (frontendscreens\\options.cpp and friends); this file only
places things.

    80010200                     41 x
      80010201                   one screen
        00010203  (40 bytes)     header
            +0x08  u32  screen id: name hash of the upper-case screen name
                         ("OPTIONS", "DISPLAY_OPTIONS"...), djb2, seed
                         0xFFFFFFFF, x33
            +0x10  u32 x 3       0xFFFFFFFF
            +0x18  u16 x 2       canvas 640, 480
            +0x1C  u16           number of widgets that follow
        00010212  text widget
        00010210  image widget
        (00010213, 00000003 appear once each, around the screen list)

    widget body (both kinds):
        +0x08  u32  name hash (hash of the widget name below)
        +0x0C  u32  second hash (parent or type; same for every item of a row)
        +0x10  char name[24]       "MENUITEM_1", "MI_1_MASK", "NONAME"...
        +0x28  u8 kind, u8 1       1 = text, 2 = image
        +0x2A  u8                  image: blend/layer
        +0x30  f32 x, f32 y        position (canvas pixels)
        text:  +0x38 f32 sx, sy    scale
               +0x50 u32 font      font id (FONT_IMPACTBIG = 0x2D213565 ...)
               +0x54 u32           capacity of the text (characters)
               +0x58 u32           2
               +0x5C u8 x 4        colour R G B A
               +0x60 u32 string id  key into FE_<LANGUAGE>.BIN; 0 = none
               +0x64 char[]        default text, used when the id is 0 or
                                    only as a placeholder in the layout
        image: +0x38 f32 sx, sy    scale of the unit quad (10 x 6 = a 10x6 mask)
               +0x50 u32 texture   name hash of a texture in the FE pack
               +0x58 ...           four vertex colours, then UVs

The row text of the options screens is looked up by string id (100001 =
"Audio", 140001 = "Track Map"...), so renaming a row is an edit to
FE_<LANGUAGE>.BIN; moving or adding a row is an edit to this chunk.

Usage:
    hp2_fescreens.py FRONTB.BUN                      list the screens
    hp2_fescreens.py FRONTB.BUN --screen OPTIONS     list one screen's widgets
    hp2_fescreens.py FRONTB.BUN --strings en.json    resolve string ids
                                                     (en.json from hp2_lang.py --json)
    hp2_fescreens.py FRONTB.BUN --json OUT.json      everything as JSON
"""
import json
import struct
import sys

# Screen names whose hashes match an id in the file. The rest stay as hex.
NAMES = """CHALLENGE CREDITS OPTIONS POPUP AUDIO_OPTIONS CREDITS_SCREEN CUSTOMIZE_HUD
DISPLAY_OPTIONS JOYSTICK_CALIBRATION JOYSTICK_CONFIG JUKEBOX_INGAME LANGUAGE_OPTIONS
LOADSAVE RACE_SETUP SELECT_CAR SELECT_TRACK TITLE_SCREEN JOYSTICK_PRECONFIG
MEMCARD_POPUP TREE_VIEW USERNAME_OPTIONS""".split()
FONTS = {0x2D213565: "FONT_IMPACTBIG", 0x5F3A4A3C: "FONT_IMPACTNORMAL"}


def name_hash(s):
    x = 0xFFFFFFFF
    for c in s.encode():
        x = (x * 33 + c) & 0xFFFFFFFF
    return x


SCREEN_IDS = {name_hash(n): n for n in NAMES}


def chunks(b, o, end):
    while o + 8 <= end:
        cid, sz = struct.unpack_from("<II", b, o)
        yield cid, o + 8, sz
        o += 8 + sz


def parse(b):
    """The screens of a FRONTB.BUN, as a list of dicts."""
    out = []
    top = None
    for cid, o, sz in chunks(b, 0, len(b)):
        if cid == 0x80010200:
            top = (o, sz)
            break
    if top is None:
        raise SystemExit("no 0x80010200 chunk: is this FRONTB.BUN?")
    for cid, o, sz in chunks(b, top[0], top[0] + top[1]):
        if cid != 0x80010201:
            continue
        scr = dict(offset=o - 8, widgets=[])
        for c2, o2, s2 in chunks(b, o, o + sz):
            body = b[o2:o2 + s2]
            if c2 == 0x10203:
                scr["id"] = struct.unpack_from("<I", body, 8)[0]
                scr["canvas"] = struct.unpack_from("<HH", body, 0x18)
                scr["count"] = struct.unpack_from("<H", body, 0x1C)[0]
            elif c2 in (0x10210, 0x10212):
                w = dict(kind="text" if c2 == 0x10212 else "image", offset=o2 - 8,
                         name=body[16:40].split(b"\0")[0].decode("latin1"))
                w["hash"] = struct.unpack_from("<I", body, 8)[0]
                w["x"], w["y"] = struct.unpack_from("<ff", body, 0x30)
                if c2 == 0x10212:
                    w["scale"] = struct.unpack_from("<ff", body, 0x38)
                    w["font"], w["capacity"] = struct.unpack_from("<II", body, 0x50)
                    w["color"] = body[0x5C:0x60].hex()
                    w["string_id"] = struct.unpack_from("<I", body, 0x60)[0]
                    w["default"] = body[0x64:].split(b"\0")[0].decode("latin1")
                else:
                    w["scale"] = struct.unpack_from("<ff", body, 0x38)
                    w["texture"] = struct.unpack_from("<I", body, 0x50)[0]
                scr["widgets"].append(w)
        scr["name"] = SCREEN_IDS.get(scr.get("id"), "%08x" % scr.get("id", 0))
        out.append(scr)
    return out


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    b = open(a[0], "rb").read()
    screens = parse(b)
    strings = {}
    if "--strings" in a:
        for d in json.load(open(a[a.index("--strings") + 1], encoding="utf-8")):
            strings[d["id"]] = d["text"]
    if "--json" in a:
        out = a[a.index("--json") + 1]
        json.dump(screens, open(out, "w", encoding="utf-8"), indent=1)
        print("wrote %d screens to %s" % (len(screens), out))
        return
    if "--screen" in a:
        want = a[a.index("--screen") + 1].upper()
        hit = [s for s in screens if s["name"].upper() == want or "%08x" % s["id"] == want.lower()]
        if not hit:
            raise SystemExit("no screen %s; names: %s" % (want, ", ".join(s["name"] for s in screens)))
        for s in hit:
            print("%s  id %08x  %dx%d  %d widgets" % (s["name"], s["id"], s["canvas"][0], s["canvas"][1], len(s["widgets"])))
            for w in s["widgets"]:
                if w["kind"] == "text":
                    txt = strings.get(w["string_id"], w["default"])
                    print("  text   %-20s (%4.0f,%4.0f) %-15s id %-6d %r" % (
                        w["name"], w["x"], w["y"], FONTS.get(w["font"], "%08x" % w["font"]),
                        w["string_id"], txt))
                else:
                    print("  image  %-20s (%4.0f,%4.0f) scale %g x %g  texture %08x" % (
                        w["name"], w["x"], w["y"], w["scale"][0], w["scale"][1], w["texture"]))
        return
    for i, s in enumerate(screens):
        t = sum(1 for w in s["widgets"] if w["kind"] == "text")
        items = sum(1 for w in s["widgets"] if w["name"].startswith("MENUITEM_"))
        print("%2d  %-22s id %08x  %3d widgets (%d text, %d menu rows)  count field %d"
              % (i, s["name"], s["id"], len(s["widgets"]), t, items, s["count"]))


if __name__ == "__main__":
    main()
