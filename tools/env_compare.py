"""Environment of the ORIGINAL (V lines, HOTD2_SCENECAP_ENV=1) vs the port's world (play.exe with HOTD2_PLAY_ENVLOG),
frame by frame: env block 0 / 1 (light angles, fog colour / near / far, light colour / intensity) and, while
0x009A2BB4 != 0, the light table (ambient, enabled lights' positions / directions).
The hook writes a V line only when something changed, so its state is carried forward.
Usage: python tools/env_compare.py <capture with V lines> <port env log> [--from F] [--to F] [--show N]
"""
import sys


def parse_capture(path):
    out = {}
    for line in open(path):
        if not line.startswith("V "):
            continue
        parts = line.split("|")
        head = parts[0].split()
        f = int(head[1])
        b0 = [float(x) for x in parts[1].split()]
        b1 = [float(x) for x in parts[2].split()]
        alt = parts[4].split()
        slots = parts[5].split() if len(parts) > 5 else []
        out[f] = (int(head[2]), b0, b1, int(alt[0]), int(alt[1]), [float(x) for x in alt[2:5]], slots)
    return out


def parse_port(path):
    out = {}
    for line in open(path):
        if not line.startswith("V "):
            continue
        parts = line.split("|")
        head = parts[0].split()
        f = int(head[1])
        tail = parts[4].split()
        out[f] = (int(head[2]), [float(x) for x in parts[1].split()], [float(x) for x in parts[2].split()],
                  [float(x) for x in parts[3].split()], int(tail[0]), [float(x) for x in tail[1:]])
    return out


NAMES = ["pitch", "yaw", "fogR", "fogG", "fogB", "near", "far", "+38", "colR", "colG", "colB", "intensity"]


def close(a, b, i):
    if i < 2:  # angles: modulo a turn
        return (int(a) - int(b)) % 65536 == 0
    if i == 7:  # +0x38 not modelled by the port
        return True
    return abs(a - b) <= 1e-4 * max(1.0, abs(b)) + 1e-6


def main():
    args = sys.argv[1:]
    lo, hi, show = 0, 10 ** 9, 20
    for k in ("--from", "--to", "--show"):
        if k in args:
            i = args.index(k)
            v = int(args[i + 1])
            del args[i:i + 2]
            lo, hi, show = (v, hi, show) if k == "--from" else (lo, v, show) if k == "--to" else (lo, hi, v)
    cap, port = parse_capture(args[0]), parse_port(args[1])
    keys = sorted(cap)
    ki, cur = 0, None
    ok = bad = 0
    shown = 0
    bad_by_field = {}
    runs = []  # (first, last, description)
    for f in sorted(port):
        if f < lo or f > hi:
            continue
        while ki < len(keys) and keys[ki] <= f:
            cur = cap[keys[ki]]
            ki += 1
        if cur is None:
            continue
        mode, b0, b1, a2bb4, a8d4c, amb, slots = cur
        pm, p0, p1, pamb, pn, pslots = port[f]
        diffs = []
        for bi, (c, p) in enumerate(((b0, p0), (b1, p1))):
            for i in range(12):
                if not close(p[i], c[i], i):
                    diffs.append(f"b{bi}.{NAMES[i]} {p[i]:g} vs {c[i]:g}")
        if a2bb4:
            if any(abs(pamb[k] - amb[k]) > 1e-4 for k in range(3)):
                diffs.append(f"table ambient {pamb} vs {amb}")
            n = sum(1 for s in slots if s.count(":") == 2)
            if pn != n:
                diffs.append(f"table lights {pn} vs {n}")
        if diffs:
            bad += 1
            for d in diffs:
                key = d.split()[0]
                bad_by_field[key] = bad_by_field.get(key, 0) + 1
            desc = "; ".join(diffs)
            if runs and runs[-1][1] == f - 1 and runs[-1][2].split(" ")[0] == desc.split(" ")[0]:
                runs[-1] = (runs[-1][0], f, runs[-1][2])
            else:
                runs.append((f, f, desc))
        else:
            ok += 1
    print(f"frames: {ok} match, {bad} differ")
    for k, v in sorted(bad_by_field.items(), key=lambda x: -x[1]):
        print(f"  {k}: {v} frames")
    for a, b, d in runs[:show]:
        print(f"  {a}..{b}: {d}")
    if len(runs) > show:
        print(f"  ... {len(runs) - show} more runs")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
