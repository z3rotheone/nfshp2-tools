"""
Decompress NFS Hot Pursuit 2 (PS2) "COMP" files and verify against known twins.

Format (confirmed against the game's own decompressor at 0x1b41c8 in SLUS_203.62):
  0x00  'COMP'
  0x04  u8  version, always 1
  0x05  u8  header size, always 0x10
  0x06  u16 method: 0 = LZ as below, 1 = stored (the game just copies it)
  0x08  uncompressed size (u32 LE)
  0x0C  compressed size   (u32 LE, equals the file size)
  0x10  payload: repeated blocks of
          u16 LE flag word, then up to 16 tokens, consumed LSB first
          flag bit 0 -> literal: 1 byte copied as-is
          flag bit 1 -> match:   2 bytes b0, b1
                          length   = (b0 & 0x0F) + 3
                          distance = ((b0 >> 4) << 8) | b1   (copy from output)

Usage:
  python hp2_comp.py [extracted_dir]

Decompressed files go to <extracted_dir>\\decomp\\. For every COMP file the
script also looks for an uncompressed file of the same size and, if there is
one, checks that the output is byte-identical.
"""
import os
import struct
import sys


def decompress(data):
    if data[:4] != b"COMP":
        raise ValueError("missing COMP magic")
    version, hdr_size, method = data[4], data[5], struct.unpack_from("<H", data, 6)[0]
    us, cs = struct.unpack_from("<II", data, 8)
    if cs != len(data):
        raise ValueError("header compressed size %d != file size %d" % (cs, len(data)))
    if hdr_size != 16:
        raise ValueError("unexpected header size %d" % hdr_size)
    if method == 1:
        # stored: the game memcpy's the payload, so there is nothing to decode
        body = data[16:16 + us]
        if len(body) != us:
            raise ValueError("stored payload is %d bytes, header says %d" % (len(body), us))
        return body, 16 + us
    if method != 0:
        raise ValueError("unknown compression method %d" % method)
    p = data
    i = 16
    out = bytearray()
    while len(out) < us:
        if i + 2 > len(p):
            raise ValueError("ran out of input reading flags at in=%d out=%d" % (i, len(out)))
        flags = p[i] | (p[i + 1] << 8)
        i += 2
        for b in range(16):
            if len(out) >= us:
                break
            if flags & (1 << b):
                if i + 2 > len(p):
                    raise ValueError("truncated match at in=%d" % i)
                b0, b1 = p[i], p[i + 1]
                i += 2
                ln = (b0 & 0x0F) + 3
                d = ((b0 >> 4) << 8) | b1
                if d == 0 or d > len(out):
                    raise ValueError(
                        "bad distance %d (len %d) at in=%d out=%d" % (d, ln, i - 2, len(out))
                    )
                for _ in range(ln):
                    out.append(out[-d])
            else:
                if i >= len(p):
                    raise ValueError("truncated literal at in=%d" % i)
                out.append(p[i])
                i += 1
    if len(out) != us:
        raise ValueError("output size %d != expected %d" % (len(out), us))
    return bytes(out), i


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "extracted"
    outdir = os.path.join(root, "decomp")
    os.makedirs(outdir, exist_ok=True)

    names = sorted(os.listdir(root))
    by_size = {}
    for n in names:
        fp = os.path.join(root, n)
        if os.path.isfile(fp) and not n.endswith(".comp"):
            by_size.setdefault(os.path.getsize(fp), []).append(fp)

    ok_twin = bad_twin = no_twin = failed = 0
    for n in names:
        if not n.endswith(".comp"):
            continue
        fp = os.path.join(root, n)
        data = open(fp, "rb").read()
        try:
            out, used = decompress(data)
        except Exception as e:
            failed += 1
            print("FAIL     %s: %s" % (n, e))
            continue
        with open(os.path.join(outdir, n[:-5] + ".dec"), "wb") as f:
            f.write(out)
        extra = "" if used == len(data) else " (input bytes left over: %d)" % (len(data) - used)
        twins = by_size.get(len(out), [])
        if not twins:
            no_twin += 1
            print("no twin  %s -> %d bytes%s" % (n, len(out), extra))
            continue
        match = next((t for t in twins if open(t, "rb").read() == out), None)
        if match:
            ok_twin += 1
            print("MATCH    %s == %s%s" % (n, os.path.basename(match), extra))
        else:
            bad_twin += 1
            print("DIFFERS  %s (same-size files exist but none identical)%s" % (n, extra))

    print()
    print("verified against a twin:", ok_twin)
    print("same-size file but different content:", bad_twin)
    print("no twin to compare (decompressed anyway):", no_twin)
    print("failed to decompress:", failed)


if __name__ == "__main__":
    main()
