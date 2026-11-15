#!/usr/bin/env python3
"""
Convert the game's videos (archive entries 372-392, AVI) to MP4 with ffmpeg.

Twenty use FourCC MV30 (MidiVid 3.0) and one, entry 390, MVDV (MidiVid VQ);
FFmpeg has decoded both since version 4.3. Video is 30 fps, 512x256 or
256x384; ten files carry PCM audio (pcm_u8). The MV30 decoder does not apply
the AVI's bottom-up row order, so its frames come out upside down: they get a
vertical flip here. MVDV comes out upright and is left alone.

Usage:  hp2_video.py EXTRACTED_DIR OUT_DIR   (names come from archive_names.csv)
"""
import csv
import glob
import os
import subprocess
import sys


def codec(path):
    r = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                        "stream=codec_name", "-of", "csv=p=0", path], capture_output=True, text=True)
    return r.stdout.strip()


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return
    src, out = sys.argv[1:]
    os.makedirs(out, exist_ok=True)
    here = os.path.dirname(os.path.abspath(__file__))
    names = {int(r["entry"]): r["name"].strip()
             for r in csv.DictReader(open(os.path.join(here, "archive_names.csv"), newline=""))}
    for f in sorted(glob.glob(os.path.join(src, "*.avi"))):
        e = int(os.path.basename(f).split("_")[0])
        stem = names.get(e) or os.path.basename(f)[:-4]
        stem = "%03d_%s" % (e, stem.split("\\")[-1].rsplit(".", 1)[0].replace(" ", "_"))
        dst = os.path.join(out, stem + ".mp4")
        if os.path.exists(dst):
            continue
        c = codec(f)
        vf = ["-vf", "vflip"] if c == "mv30" else []
        subprocess.run(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-i", f] + vf +
                       ["-c:v", "libx264", "-preset", "veryfast", "-crf", "18", "-pix_fmt", "yuv420p",
                        "-c:a", "aac", "-y", dst], check=True)
        print("%s  (%s%s)" % (dst, c, ", flipped" if vf else ""))


if __name__ == "__main__":
    main()
