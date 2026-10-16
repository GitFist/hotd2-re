"""HOTD2 camera paths (cam\\*.bin) - reference implementation + validation against a scene capture.

Evidence: Cam_LoadFile 0x00403E70, Cam_Evaluate 0x004041E0 / Cam_EvaluateAngles 0x004042D0, Cam_Curve 0x004040F0,
Cam_LookAngles 0x00403AC0 -> Cam_DirToAngles 0x004016B0, Cam_BuildViews 0x00401F40.

Tables: name 0x004D1BC8[n] (cam\\%s, n=1 cp_demo.bin), camera ids 0x004C470C[n], count 0x004C476C[n],
        file of camera id 0x004C479C[id] (s8).
File:   u32 offset[count]; u32 0xFFFFFFFF; curve area starts at 4 + 4*count (the "data base").
        camera k block at offset[k]: s32 channel[8] = word indexes into the curve area
        (0-2 position, 3-5 target, 6 roll (used only when 0x009A21B0 != 0), 7 unused by Cam_Evaluate).
Curve:  u16 n (key count padded to a power of two), u16 log2(n); keys {f32 time, value, outTangent, inTangent}
        from +4; cubic Hermite between keys k-1 and k found by the game's binary search.
Angles: yaw = (s16)trunc(atan2(dx, dz) * 65536/2pi), d = dz/cos(yaw) or dx/sin(yaw) (near +-90 deg),
        pitch = -(s16)trunc(atan2(dy, d) * 65536/2pi), with (dx,dy,dz) = pos - target.
View:   V = T(-pos) * Ry(-yaw) * Rx(-pitch) * Rz(-roll)   (row vectors; engine matrix stack order)
        then the D3D view transform diag(1,1,-1,1) and projection (captured) are applied by the device.

Usage: python tools/cam.py --list
       python tools/cam.py --validate game/scene_capture.txt
"""
import argparse
import math
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from tex_export import Exe  # noqa: E402

K = 65536 / (2 * math.pi)
ANGLE = 2 * math.pi / 65536


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def trunc16(x):
    v = int(x)  # __ftol truncates toward zero
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class CamFile:
    def __init__(self, data, count):
        self.data = data
        self.count = count
        self.base = 4 + 4 * count
        self.offsets = struct.unpack_from(f"<{count}I", data)

    def channels(self, k):
        return struct.unpack_from("<8i", self.data, self.offsets[k])

    def curve(self, channel_word):
        return self.base + channel_word * 4

    def eval_curve(self, off, t):
        """Mirror of Cam_Curve 0x004040F0 (binary search, then Hermite), in float32 where the game stores floats."""
        d = self.data
        n0, n1 = struct.unpack_from("<HH", d, off)
        step = n0 >> 2
        idx = n0 >> 1
        for _ in range(n1 - 1):
            key_t = struct.unpack_from("<f", d, off + 4 + idx * 16)[0]
            idx = idx - step if t <= key_t else idx + step
            step = int(step / 2)  # C integer division (truncates)
        if struct.unpack_from("<f", d, off + 4 + idx * 16)[0] < t:
            idx += 1
        t0, v0, out0, _ = struct.unpack_from("<4f", d, off + 4 + (idx - 1) * 16)
        t1, v1, _, in1 = struct.unpack_from("<4f", d, off + 4 + idx * 16)
        s = t - t0
        s2, s3 = s * s, s * s * s
        h = t1 - t0
        h2 = f32(h * h)
        a = f32(s3 / h2)
        b = (s3 + s3) / (h2 * h)
        c = (s2 * 3.0) / h2
        return (c - b) * v1 + ((b - c) + 1.0) * v0 + (a - s2 / h) * in1 + ((a - (s2 + s2) / h) + s) * out0

    def evaluate(self, k, t):
        ch = self.channels(k)
        pos = [f32(self.eval_curve(self.curve(ch[i]), t)) for i in range(3)]
        tgt = [f32(self.eval_curve(self.curve(ch[i]), t)) for i in range(3, 6)]
        roll = int(self.eval_curve(self.curve(ch[6]), t))
        return pos, tgt, roll


def cam_files(exe):
    for n in range(1, 64):
        name = exe.cstr(exe.u32(0x004D1BC8 + 4 * n))
        if not name or not name.endswith(".bin"):
            break
        count = struct.unpack("<H", exe.bytes(0x004C476C + 2 * n, 2))[0]
        p = exe.u32(0x004C470C + 4 * n)
        ids = list(struct.unpack(f"<{count}h", exe.bytes(p, 2 * count))) if count else []
        yield n, name, ids


def camera_lookup(exe):
    """camera id -> (CamFile, index within file)"""
    out = {}
    for n, name, ids in cam_files(exe):
        path = os.path.join("game/cam", name)
        if not ids or not os.path.exists(path):
            continue
        cf = CamFile(open(path, "rb").read(), len(ids))
        for k, cid in enumerate(ids):
            out[cid] = (cf, k, name)
    return out


def look_angles(pos, tgt, roll):
    dx, dy, dz = (f32(pos[i] - tgt[i]) for i in range(3))
    yaw = trunc16(math.atan2(dx, dz) * K)
    if ((yaw + 0x2000) & 0x4000) == 0:
        d = dz / math.cos(yaw * ANGLE)
    else:
        d = dx / math.sin(yaw * ANGLE)
    pitch = -trunc16(math.atan2(dy, d) * K)
    return pitch, yaw, roll


def rot_x(a):
    c, s = math.cos(a * ANGLE), math.sin(a * ANGLE)
    return np.array([[1, 0, 0, 0], [0, c, s, 0], [0, -s, c, 0], [0, 0, 0, 1]])


def rot_y(a):
    c, s = math.cos(a * ANGLE), math.sin(a * ANGLE)
    return np.array([[c, 0, -s, 0], [0, 1, 0, 0], [s, 0, c, 0], [0, 0, 0, 1]])


def rot_z(a):
    c, s = math.cos(a * ANGLE), math.sin(a * ANGLE)
    return np.array([[c, s, 0, 0], [-s, c, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]])


def view_matrix(pos, pitch, yaw, roll):
    t = np.eye(4)
    t[3, :3] = [-pos[0], -pos[1], -pos[2]]
    return t @ rot_y(-yaw) @ rot_x(-pitch) @ rot_z(-roll)


def validate(capture):
    exe = Exe()
    cams = camera_lookup(exe)
    evals = bad = 0
    worst = 0.0
    frames = []  # per frame: (cam eval of last C line, model matrices)
    cur_c, cur_m = None, []
    for line in open(capture):
        p = line.split()
        if p[0] == "C":
            cur_c = (int(p[1]), float(p[2]), [float(x) for x in p[3:6]], [float(x) for x in p[6:9]], int(p[9]))
            cid, t, pos, tgt, roll = cur_c
            if cid in cams:
                cf, k, _ = cams[cid]
                mp, mt, mr = cf.evaluate(k, t)
                err = max(abs(a - b) / max(1.0, abs(b)) for a, b in zip(mp + mt, pos + tgt))
                evals += 1
                worst = max(worst, err)
                if err > 1e-4:
                    bad += 1
                    if bad <= 5:
                        print(f"  curve MISMATCH cam {cid} t {t}: ours {mp + mt} captured {pos + tgt}")
        elif p[0] == "M":
            cur_m.append((int(p[1]), np.array([float(x) for x in p[2:18]]).reshape(4, 4)))
        elif p[0] == "F":
            frames.append((cur_c, cur_m))
            cur_c, cur_m = None, []
    print(f"camera curve evaluations checked: {evals}, mismatched: {bad}, worst relative error {worst:.2e}")

    # View check: models whose captured matrix equals our view matrix are placed at identity world.
    identity_hits = total = 0
    per_model = {}
    for c, models in frames:
        if not c or c[0] not in cams:
            continue
        cid, t, pos, tgt, roll = c
        pitch, yaw, r = look_angles(pos, tgt, 0)  # roll flag was 0 for every captured evaluation
        v = view_matrix(pos, pitch, yaw, r)
        for mid, m in models:
            total += 1
            world = m @ np.linalg.inv(v)
            is_id = np.allclose(world, np.eye(4), atol=2e-3 * max(1.0, np.abs(m).max()))
            identity_hits += is_id
            per_model.setdefault(mid, [0, 0])
            per_model[mid][0 if is_id else 1] += 1
    always = sorted(m for m, (a, b) in per_model.items() if a and not b)
    never = sorted(m for m, (a, b) in per_model.items() if b and not a)
    mixed = sorted(m for m, (a, b) in per_model.items() if a and b)
    print(f"model draws under a captured camera: {total}; world == identity under our view matrix: {identity_hits}")
    print(f"  models always at identity: {len(always)}, never: {len(never)}, mixed: {len(mixed)}")
    return always, never


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--validate")
    args = ap.parse_args()
    exe = Exe()
    if args.list:
        for n, name, ids in cam_files(exe):
            ok = os.path.exists(os.path.join("game/cam", name))
            print(f"{n:2} {name:14} cameras {len(ids):3} ids {ids[:6]}{'...' if len(ids) > 6 else ''} {'' if ok else '(missing)'}")
    if args.validate:
        validate(args.validate)


if __name__ == "__main__":
    main()
