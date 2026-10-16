"""Reference CRCs for build\\Release\\png_test.exe: zlib.crc32 of each PNG's RGBA8 bytes (rows top-down) as Pillow
decodes it.

  python tools/png_hashes.py <out list> <png or dir>... [--sample N] [--seed S]

A directory is searched recursively; --sample keeps N files chosen at random (default: all). Then:
  build\\Release\\png_test.exe <out list>
"""

import argparse
import os
import random
import sys
import zlib

from PIL import Image


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("inputs", nargs="+")
    ap.add_argument("--sample", type=int, default=0)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    files = []
    for p in a.inputs:
        if os.path.isdir(p):
            for root, _, names in os.walk(p):
                files += [os.path.join(root, n) for n in names if n.lower().endswith(".png")]
        else:
            files.append(p)
    files.sort()
    if a.sample and a.sample < len(files):
        random.Random(a.seed).shuffle(files)
        files = sorted(files[:a.sample])
    with open(a.out, "w") as f:
        for i, p in enumerate(files):
            im = Image.open(p)
            rgba = im.convert("RGBA").tobytes()
            f.write("%s %08x\n" % (p, zlib.crc32(rgba) & 0xFFFFFFFF))
            if (i + 1) % 100 == 0:
                print(i + 1, "/", len(files), file=sys.stderr)
    print(len(files), "files ->", a.out)


if __name__ == "__main__":
    main()
