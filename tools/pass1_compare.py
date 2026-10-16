"""Compare the port's pass-1 list (scene_viewer --dump-pass1) with the ORIGINAL's (capture P/p lines).

Usage: python tools/pass1_compare.py <capture with P/p and C lines> <scene> <first frame> <last frame> [step]

For each frame the scene is rendered at the captured camera time with --frame <frame>; both lists are reduced to the
entries the scene can contain (its models and sprites; actors the port does not spawn yet are dropped from the
game's list and counted) and compared in draw order. Reports per-frame equality of the order, model-set
differences and the largest depth difference.
"""
import struct
import subprocess
import sys

VIEWER = r"build\src\port\Release\scene_viewer.exe"


def load(capture):
    frames, frame, cam = {}, 0, None
    P = None
    for line in open(capture):
        t = line.split()
        if not t:
            continue
        if t[0] == "C":
            cam = (int(t[1]), float(t[2]))
        elif t[0] == "F":
            frame = int(t[1])
            frames.setdefault(frame, {})["cam"] = cam
            cam = None
        elif t[0] == "P" and int(t[1]) > 0:
            P = t[2:]
        elif t[0] == "p" and P is not None:
            order = []
            for i in map(int, t[2:]):
                k, ident, key, dep = P[i].split(":")
                order.append((k, ident if k == "M" else ident.upper(), key, struct.unpack("<f", struct.pack("<I", int(dep, 16)))[0]))
            frames.setdefault(frame + 1, {}).setdefault("lists", []).append(order)
            P = None
    return frames


def main():
    capture, scene, first, last = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    step = int(sys.argv[5]) if len(sys.argv) > 5 else 10
    frames = load(capture)
    same = diff = skipped = 0
    worst = 0.0
    dropped = 0
    for f in range(first, last + 1, step):
        info = frames.get(f, {})
        cam = frames.get(f, {}).get("cam")
        lists = info.get("lists")
        if not cam or not lists:
            skipped += 1
            continue
        out = subprocess.run([VIEWER, "game", scene, "--time", str(cam[1]), "--frame", str(f), "--screenshot",
                              "build/renders/pass1cmp.png", "--dump-pass1"], capture_output=True, text=True).stdout
        line = next((l for l in out.splitlines() if l.startswith("pass1")), None)
        if line is None:
            skipped += 1
            continue
        mine = []
        for tok in line.split()[2:]:
            k, ident, key, dep = tok.split(":")
            mine.append((k, ident if k == "M" else ident.upper(), key, float(dep)))
        known = {(m[0], m[1]) for m in mine}
        game = max(lists, key=len)  # the frame's main list
        kept = [g for g in game if (g[0], g[1]) in known or g[0] == "S"]
        dropped += len(game) - len(kept)
        ok = [(m[0], m[1]) for m in mine] == [(g[0], g[1]) for g in kept]
        if ok:
            same += 1
            for m, g in zip(mine, kept):
                worst = max(worst, abs(m[3] - g[3]))
        else:
            diff += 1
            if diff <= 3:
                print(f"frame {f}: port {[(m[0], m[1]) for m in mine]}\n          game {[(g[0], g[1]) for g in kept]}")
    print(f"frames compared {same + diff}: order equal {same}, different {diff}, skipped {skipped}; "
          f"worst depth difference {worst:.4g}; game entries for actors not in the scene (dropped) {dropped}")
    return 0 if diff == 0 and same > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
