#!/usr/bin/env python3
"""
Boot straight to the title screen: skip the EA logo and the intro movie.

On start-up the game builds a small movie-sequence object (function 0x16F000)
that plays the intro movies in order (EALOGO_<lang>, then NFSINTRO_<lang>; see
the table at 0x2FB750) and, when they are done, moves on to the TITLE_IMAGE
screen. The object has two states: 2 = play the movies, 1 = go to the title.
The constructor picks 1 when a flag word at gp-0x59FC is set and 2 otherwise:

    0x16F090  lw    v0, -0x59fc(gp)
    0x16F094  beqz  v0, 0x16f0b4        <- 10 40 00 07   (the one word changed)
    0x16F098  sw    zero, 8(s1)

Replacing the beqz with a nop makes it always fall through to state 1. The
movie files stay on the disc; they are simply never opened. Same size, one
word, and --undo writes the original back.

Usage:
    python hp2_skipintro.py GAME.iso            patch SLUS_203.62 inside the image
    python hp2_skipintro.py GAME.iso --undo     put the original instruction back
    python hp2_skipintro.py GAME.iso --check    say whether it is patched
    python hp2_skipintro.py SLUS_203.62         the same on a loose executable

Applies to the NTSC-U release (SLUS-203.62). Work on a copy of your image.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hp2_iso

OFFSET = 0x70094          # file offset of virtual address 0x16F094 in SLUS_203.62
ORIGINAL = 0x10400007     # beqz v0, +7
PATCHED = 0x00000000      # nop
EXE_SIZE = 2321608
EXE_PATH = "SLUS_203.62"


def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    undo, check = "--undo" in sys.argv, "--check" in sys.argv
    if not a:
        print(__doc__)
        return
    target = a[0]
    with open(target, "r+b") as f:
        f.seek(0, 2)
        is_exe = f.tell() == EXE_SIZE
        if is_exe:
            base = 0
        else:
            lba, size = hp2_iso.find(f, EXE_PATH)
            if lba is None:
                raise SystemExit("%s is not in this image" % EXE_PATH)
            if size != EXE_SIZE:
                raise SystemExit("%s is %d bytes, expected %d: this is not the NTSC-U "
                                 "release, so nothing was changed" % (EXE_PATH, size, EXE_SIZE))
            base = lba * hp2_iso.SECTOR
        f.seek(base + OFFSET)
        word = struct.unpack("<I", f.read(4))[0]
        if word == ORIGINAL:
            state = "original"
        elif word == PATCHED:
            state = "patched"
        else:
            raise SystemExit("unexpected instruction %08x at 0x16F094; nothing changed" % word)
        if check:
            print("intro skip: %s" % ("on" if state == "patched" else "off"))
            return
        want = ORIGINAL if undo else PATCHED
        if word == want:
            print("already %s" % ("original" if undo else "patched"))
            return
        f.seek(base + OFFSET)
        f.write(struct.pack("<I", want))
    print("%s: intro %s" % (os.path.basename(target), "restored" if undo else "skipped"))


if __name__ == "__main__":
    main()
