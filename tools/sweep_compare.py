"""Whole-game screenshot sweep: the ORIGINAL's shots vs play.exe renders of the same scripted run, frame by frame.

Original shots: a lightly instrumented run with HOTD2_SCENECAP_SHOT_EVERY=N (game\\shot_<frame>.bmp, copied to a
folder of your own). Port renders: play.exe game --script <same script> --screenshot <dir>\\o_%d.png --every N --frame F.
Per frame (16x16 block means, top 12 rows ignored):
  colour  mean absolute block difference (0..255)
  bias    mean (port - original) per channel R G B: a colour cast (e.g. port warmer: R > 0, B < 0)
  edges   edge correlation of the whole frame (geometry / camera agreement)
Usage: python tools/sweep_compare.py <shot dir> <render dir> [--csv out.csv] [--threshold 12] [--from F] [--to F]
"""
import os
import re
import sys

import numpy as np
from PIL import Image, ImageFilter


def blocks(img, size):
    a = np.asarray(img.convert("RGB").resize(size)).astype(float)
    h, w = size[1] // 16 * 16, size[0] // 16 * 16
    a = a[16:h, :w]  # skip the top block row (window frame / letterbox jitter)
    return a.reshape(a.shape[0] // 16, 16, w // 16, 16, 3).mean(axis=(1, 3))


def edges(img, size):
    g = img.convert("L").resize(size).filter(ImageFilter.GaussianBlur(1.5))
    a = np.asarray(g).astype(float)[12:]
    gx, gy = np.zeros_like(a), np.zeros_like(a)
    gx[:, 1:-1] = a[:, 2:] - a[:, :-2]
    gy[1:-1] = a[2:] - a[:-2]
    return np.hypot(gx, gy)


def ncc(a, b):
    a = a - a.mean()
    b = b - b.mean()
    d = np.sqrt((a * a).sum() * (b * b).sum())
    return float((a * b).sum() / d) if d > 0 else float("nan")


def main():
    args = sys.argv[1:]
    opt = {"--csv": None, "--threshold": "12", "--from": "0", "--to": "10000000"}
    for k in list(opt):
        if k in args:
            i = args.index(k)
            opt[k] = args[i + 1]
            del args[i:i + 2]
    shots, renders = args[0], args[1]
    thr, lo, hi = float(opt["--threshold"]), int(opt["--from"]), int(opt["--to"])
    frames = sorted(int(m.group(1)) for n in os.listdir(shots) if (m := re.match(r"shot_(\d+)\.bmp$", n)))
    rows = []
    for f in frames:
        if f < lo or f > hi:
            continue
        rp = os.path.join(renders, f"o_{f}.png")
        if not os.path.exists(rp):
            continue
        a = Image.open(os.path.join(shots, f"shot_{f:05d}.bmp"))
        b = Image.open(rp)
        size = a.size
        ba, bb = blocks(a, size), blocks(b, size)
        col = float(np.abs(ba - bb).mean())
        bias = (bb - ba).mean(axis=(0, 1))
        e = ncc(edges(a, size), edges(b, size))
        rows.append((f, col, bias, e, float(ba.mean()), float(bb.mean())))
    out = open(opt["--csv"], "w") if opt["--csv"] else None
    if out:
        out.write("frame,colour,biasR,biasG,biasB,edges,meanOrig,meanPort\n")
    big = 0
    for f, col, bias, e, ma, mb in rows:
        flag = col > thr or e < 0.5
        big += flag
        print(f"{'**' if flag else '  '} {f:6d} colour {col:5.1f} bias R {bias[0]:+6.1f} G {bias[1]:+6.1f} B {bias[2]:+6.1f} "
              f"edges {e:5.3f} mean {ma:5.1f} / {mb:5.1f}")
        if out:
            out.write(f"{f},{col:.2f},{bias[0]:.2f},{bias[1]:.2f},{bias[2]:.2f},{e:.4f},{ma:.2f},{mb:.2f}\n")
    if rows:
        print(f"{len(rows)} frames, {big} flagged (colour > {thr:g} or edges < 0.5); mean colour {np.mean([r[1] for r in rows]):.2f}, "
              f"mean edges {np.nanmean([r[3] for r in rows]):.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
