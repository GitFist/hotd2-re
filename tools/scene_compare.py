"""Compare native scene renders with screenshots of the ORIGINAL game at the same camera time, per layer.

Inputs (--ref DIR, default game): DIR/shot_<frame>.bmp + DIR/scene_capture.txt from a LIGHTLY instrumented capture run
(heavy logging slows the game and its frame skipping lets screenshots drift from the logged frame), e.g.
  HOTD2_WINDOWED=1 HOTD2_KEEPACTIVE=1 HOTD2_SCENECAP=1 HOTD2_SCENECAP_FRAMES=3300
  HOTD2_SCENECAP_SHOTS=650,800,950,1050,2820,2860,3100,3200  build\\src\\hook\\Release\\hotd2_launch.exe game\\Hod2.exe
Kept reference: build\\ref (shots + capture).

For each shot the camera id/time of that frame is read from the capture and the scene is rendered with scene_viewer at
that time (and at time+60 as a control). Reported per frame:
  3D     edge correlation outside the 2D overlay regions, port rendered WITHOUT overlays, vs the control. This is the
         discriminating scene/camera check (pass: > 0.5 and > control + 0.2); overlays cannot inflate it.
  frame  whole-frame edge correlation and block colour difference with overlays drawn (raw, no pass margin).
  2D     block colour difference inside the overlay regions, with and without the port's overlays (shows whether the
         overlays account for those regions).
Usage: python tools/scene_compare.py [--world] [--ref DIR] [--mask title|stage] [viewer flags: --simple --nofog --lit] <scene> <frame>...
"""
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageFilter

VIEWER = r"build\src\port\Release\scene_viewer.exe"


def camera_times(capture, frames):
    out, last = {}, None
    for line in open(capture):
        if line[0] == "C":
            last = line.split()
        elif line[0] == "F":
            f = int(line.split()[1])
            if f in frames and last:
                out[f] = (int(last[1]), float(last[2]))
            last = None
    return out


def edges(path, size):
    g = Image.open(path).convert("L").resize(size).filter(ImageFilter.GaussianBlur(1.5))
    a = np.asarray(g).astype(float)
    gx, gy = np.zeros_like(a), np.zeros_like(a)
    gx[:, 1:-1] = a[:, 2:] - a[:, :-2]
    gy[1:-1] = a[2:] - a[:-2]
    return np.hypot(gx, gy)


def main():
    args = sys.argv[1:]
    ref, mask_kind = "game", "title"
    world = "--world" in args  # scene driven by game::World: frame-based, control = frame + 60
    args = [a for a in args if a != "--world"]
    if "--ref" in args:
        i = args.index("--ref"); ref = args[i + 1]; del args[i:i + 2]
    if "--mask" in args:
        i = args.index("--mask"); mask_kind = args[i + 1]; del args[i:i + 2]
    extra = [a for a in args if a in ("--simple", "--nofog", "--lit")]
    args = [a for a in args if a not in extra and a != "--no2d"]
    scene, frames = args[0], [int(x) for x in args[1:]]
    times = camera_times(os.path.join(ref, "scene_capture.txt"), set(frames))
    size = Image.open(os.path.join(ref, f"shot_{frames[0]:05d}.bmp")).size
    overlay = np.zeros((size[1], size[0]), bool)  # 2D overlay regions
    if mask_kind == "title":
        overlay[55:345, 35:635] = True    # title logo + silhouette
        overlay[325:465, 150:570] = True  # "presented by", SEGA, copyright, PRESS START
    else:  # stage demo: PRESS START BUTTON
        overlay[330:372, 160:470] = True
    top = np.zeros_like(overlay)
    top[:12, :] = True
    scene3d = ~overlay & ~top
    whole = ~top

    def ncc(a, b, m):
        a = a[m] - a[m].mean()
        b = b[m] - b[m].mean()
        return float((a * b).sum() / np.sqrt((a * a).sum() * (b * b).sum()))

    def blocks(path):
        a = np.asarray(Image.open(path).convert("RGB").resize(size)).astype(float)
        h, w = size[1] // 16 * 16, size[0] // 16 * 16
        return a[:h, :w].reshape(h // 16, 16, w // 16, 16, 3).mean(axis=(1, 3)), h, w

    def coldiff(p1, p2, m):
        (a, h, w), (b, _, _) = blocks(p1), blocks(p2)
        mb = m[:h, :w].reshape(h // 16, 16, w // 16, 16).all(axis=(1, 3))
        return float(np.abs(a - b)[mb].mean()) if mb.any() else float("nan")

    def render(f, t, no2d):
        out = f"build/renders/scenecmp_{f}_{int(t)}{'_3d' if no2d else ''}.png"
        subprocess.run([VIEWER, "game", scene, "--time", str(t), "--frame", str(f + (60 if world and t < 0 else 0)), "--screenshot", out, "--width", str(size[0]),
                        "--height", str(size[1])] + extra + (["--no2d"] if no2d else []), check=True, capture_output=True)
        return out

    ok = True
    for f in frames:
        cam, t = times.get(f, (-1, 0.0)) if world else times[f]
        shot = os.path.join(ref, f"shot_{f:05d}.bmp")
        es = edges(shot, size)
        if world:  # control: the same world 60 frames later (marked by a negative time)
            p3d, pctl, pall = render(f, t, True), render(f, -1, True), render(f, t, False)
        else:
            p3d, pctl, pall = render(f, t, True), render(f, t + 60, True), render(f, t, False)
        e3d, ectl, eall = ncc(es, edges(p3d, size), scene3d), ncc(es, edges(pctl, size), scene3d), ncc(es, edges(pall, size), whole)
        good = e3d > 0.5 and e3d > ectl + 0.2
        ok &= good
        print(f"{'ok  ' if good else 'FAIL'} frame {f} cam {cam} t={t:g}: 3D edges {e3d:.3f} (control {ectl:.3f}) colour {coldiff(shot, p3d, scene3d):.1f} | "
              f"frame edges {eall:.3f} colour {coldiff(shot, pall, whole):.1f} | "
              f"2D-region colour {coldiff(shot, pall, overlay):.1f} (without overlays {coldiff(shot, p3d, overlay):.1f})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
