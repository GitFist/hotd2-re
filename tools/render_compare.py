"""Regression check: native D3D11 model renderer (model_viewer --screenshot) vs the offline reference
(tools/pol_render.py) on a fixed set of models.

Checks per case: identical triangle/texture counts, silhouette IoU >= 0.95 (thin spiky features lose a few edge
pixels), and <= 15% of foreground 4x4 blocks whose averaged colour differs by more than 24 levels (block averaging
removes point-sampling noise on high-frequency textures). Writes side-by-side images to build/renders/cmp_<case>.png.

Usage: python tools/render_compare.py
"""
import re
import subprocess
import sys

import numpy as np
from PIL import Image

VIEWER = r"build\src\port\Release\model_viewer.exe"
CASES = [  # (pol file, model, yaw, pitch)
    ("common.bin", "338", 35, 25),  # largest common.bin model (model 0 is sub-pixel blood droplets)
    ("znken.bin", "3", 200, 10),
    ("znken.bin", "12", 160, 20),
    ("st1_03.bin", "1", 35, 25),
    ("st3_01.bin", "2", 60, 15),
    ("boss5b.bin", "all", 30, 15),
]
# Posed characters: (skeleton type, motion id, tick, yaw, pitch) - native viewer vs tools/skel_pose.py.
POSES = [(0, 0, 30, 200, 10), (1, 0, 20, 200, 10), (5, 3, 10, 160, 15)]
BG = np.array([46, 51, 66])


def counts(text):
    m = re.search(r"(\d+) triangles; meshes textured (\d+), untextured (\d+), texture missing (\d+)", text)
    if m:
        return tuple(map(int, m.groups()))
    m = re.search(r"(\d+) triangles", text)  # skel_pose.py / character mode: compare triangle count only
    return (int(m.group(1)),) if m else None


def main():
    failures = 0
    jobs = []
    for pol, model, yaw, pitch in CASES:
        args = ["--yaw", str(yaw), "--pitch", str(pitch), "--size", "384"]
        tag = f"{pol[:-4]}_{model}"
        jobs.append((tag, [VIEWER, "game", pol, model], [sys.executable, "tools/pol_render.py", pol, model], args))
    for skel, motion, tick, yaw, pitch in POSES:
        args = ["--yaw", str(yaw), "--pitch", str(pitch), "--size", "384"]
        tag = f"pose_t{skel}_m{motion}_{tick}"
        jobs.append((tag, [VIEWER, "game", "--skeleton", str(skel), "--motion", str(motion), "--tick", str(tick)],
                     [sys.executable, "tools/skel_pose.py", str(skel), str(motion), str(tick)], args))
    for tag, viewer_cmd, ref_cmd, args in jobs:
        a, b = f"build/renders/cpp_{tag}.png", f"build/renders/py_{tag}.png"
        ca = subprocess.run(viewer_cmd + ["--screenshot", a] + args, capture_output=True, text=True)
        cb = subprocess.run(ref_cmd + [b] + args, capture_output=True, text=True)
        if ca.returncode or cb.returncode:
            print(f"FAIL {tag}: renderer error\n{ca.stderr}{cb.stderr}")
            failures += 1
            continue
        x = np.asarray(Image.open(a).convert("RGB")).astype(int)
        y = np.asarray(Image.open(b).convert("RGB")).astype(int)
        mx, my = np.abs(x - BG).max(axis=2) > 3, np.abs(y - BG).max(axis=2) > 3
        fg = mx | my
        iou = (mx & my).sum() / max(fg.sum(), 1)
        # Compare 4x4 box-averaged images: point-sampling noise on high-frequency textures averages out,
        # a wrong texture or misplaced geometry does not.
        def down(img):
            h, w = img.shape[0] // 4 * 4, img.shape[1] // 4 * 4
            return img[:h, :w].reshape(h // 4, 4, w // 4, 4, 3).mean(axis=(1, 3))
        fgd = fg[: fg.shape[0] // 4 * 4, : fg.shape[1] // 4 * 4].reshape(fg.shape[0] // 4, 4, fg.shape[1] // 4, 4).any(axis=(1, 3))
        diff = (np.abs(down(x) - down(y)).max(axis=2)[fgd] > 24).mean() if fgd.any() else 0.0
        ka, kb = counts(ca.stdout), counts(cb.stdout)
        same_counts = ka is not None and kb is not None and ka[0] == kb[0] and (len(ka) == 1 or len(kb) == 1 or ka == kb)
        ok = same_counts and iou >= 0.95 and diff <= 0.15
        failures += not ok
        Image.fromarray(np.concatenate([x, y], axis=1).astype(np.uint8)).save(f"build/renders/cmp_{tag}.png")
        print(f"{'ok  ' if ok else 'FAIL'} {tag:16} counts {counts(ca.stdout)} {'==' if same_counts else '!='} "
              f"{counts(cb.stdout)}  IoU {iou:.3f}  fg 4x4 blocks differing {diff:.3f}")
    print(f"{len(jobs) - failures}/{len(jobs)} cases passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
