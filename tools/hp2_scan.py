"""
Scan the extracted NFS Hot Pursuit 2 files for audio (and other media) signatures.

Reads every file in <extracted_dir> and <extracted_dir>\\decomp (read-only) and counts
well-known magic strings, for example:
  VAGp          Sony VAG ADPCM audio
  SShd / SSbd   Sony SS2 audio container (header / body)
  RIFF....WAVE  WAV audio
  SCHl SCCl SCDl SCEl   EA (Electronic Arts) stream chunks, common in EA PS2 games
  EACS          EA audio
  OggS, ID3     Ogg / MP3 tags
  MThd          MIDI
  AIFF, FORM    AIFF
  RIFF....AVI   AVI video

Usage:  python hp2_scan.py [extracted_dir]
Output: audio_scan.txt next to the extracted folder, plus a summary on screen.
"""
import mmap
import os
import sys

SIGS = [
    (b"VAGp", "VAG"),
    (b"SShd", "SShd"),
    (b"SSbd", "SSbd"),
    (b"SCHl", "EA-SCHl"),
    (b"SCCl", "EA-SCCl"),
    (b"SCDl", "EA-SCDl"),
    (b"SCEl", "EA-SCEl"),
    (b"EACS", "EACS"),
    (b"OggS", "Ogg"),
    (b"ID3", "ID3"),
    (b"MThd", "MIDI"),
    (b"FORM", "FORM"),
]
MAX_COUNT = 100000


def count_sig(mm, sig):
    n, first, pos = 0, -1, 0
    while True:
        i = mm.find(sig, pos)
        if i < 0 or n >= MAX_COUNT:
            break
        if n == 0:
            first = i
        n += 1
        pos = i + 1
    return n, first


def count_riff(mm):
    """RIFF containers: count WAVE and AVI separately."""
    wave = avi = 0
    first_wave = first_avi = -1
    pos = 0
    while True:
        i = mm.find(b"RIFF", pos)
        if i < 0:
            break
        kind = mm[i + 8: i + 12]
        if kind == b"WAVE":
            wave += 1
            if first_wave < 0:
                first_wave = i
        elif kind == b"AVI ":
            avi += 1
            if first_avi < 0:
                first_avi = i
        pos = i + 1
    return (wave, first_wave), (avi, first_avi)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "extracted"
    paths = []
    for base in (root, os.path.join(root, "decomp")):
        if os.path.isdir(base):
            for n in sorted(os.listdir(base)):
                p = os.path.join(base, n)
                if os.path.isfile(p) and not n.endswith(".comp") and os.path.getsize(p) > 0:
                    paths.append(p)
    out_path = os.path.join(os.path.dirname(root.rstrip("\\/")), "audio_scan.txt")
    totals = {}
    hits = []
    with open(out_path, "w", encoding="utf-8") as out:
        for p in paths:
            with open(p, "rb") as f:
                mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
                found = {}
                for sig, label in SIGS:
                    n, first = count_sig(mm, sig)
                    if n:
                        found[label] = (n, first)
                (wn, wf), (an, af) = count_riff(mm)
                if wn:
                    found["WAV"] = (wn, wf)
                if an:
                    found["AVI"] = (an, af)
                mm.close()
            rel = os.path.relpath(p, root)
            if found:
                line = "%s | %d bytes | %s" % (
                    rel, os.path.getsize(p),
                    ", ".join("%s x%d (first at %d)" % (k, v[0], v[1]) for k, v in sorted(found.items())))
                out.write(line + "\n")
                hits.append((rel, found))
                for k, v in found.items():
                    totals[k] = totals.get(k, 0) + v[0]
    print("scanned %d files, %d contain at least one signature" % (len(paths), len(hits)))
    for k, v in sorted(totals.items(), key=lambda kv: -kv[1]):
        print("  %-8s %d occurrences" % (k, v))
    print("details:", out_path)


if __name__ == "__main__":
    main()
