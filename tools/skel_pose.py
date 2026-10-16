"""Skeletons, motions and posed rendering for HOTD2 characters (reference implementation).

Evidence: Anim_DrawSkeleton 0x0040F520 (+ node walkers 0x0040F680/0x0040F710/0x0040F7B0), Anim_NodeTransform
0x0040ED30, Anim_RootTransform 0x0040EFB0, Anim_GetFrame 0x00412F50, Anim_AdvanceTime 0x0040F610,
Res_LoadMotionFile 0x00412C10 / Res_SetupMotionSlots 0x00412D90, matrix ops 0x004A9D80 (translate),
0x004A99F0/0x004A9AE0/0x004A9BD0 (rotate X/Y/Z, angle * 2pi/65536).

Exe tables:
  0x004E0430 + t*4   root Node* of skeleton type t (64 types)
  0x004E0724 + t*2   s16 node count of type t
  Node: u32 modelId (global, 0 = none); f32 offset[3]; u32 flags; s16 index; s16 childCount; Node* child[]
  0x004D1B00 + n*4   char* mot file name; 0x004E2B14 + n*4 s16* motion ids; 0x004E2BDC + n*2 u16 count
  0x004E07D0 + m*2   s16 length of motion m in 60 Hz ticks
  0x004E83B4 + id*2  u16 pol file of global model id (index within the file via that file's id list)
Mot file: u32 offset[count]; motion = u32 header + frames, stored at 30 Hz:
  frames = (ticks - 1) // 2 + 2, each align4(12 + 6 * nodes): f32 rootPos[3], s16 rot[nodes][3] (x, y, z)
Pose (row vectors, pre-multiplied like the engine's matrix stack):
  root:  M0 = Rx(r0x) Ry(r0y) Rz(r0z) T(rootPos)
  node:  Mi = Rx(rix) Riy... Ry(riy) Rz(riz) T(offset_i) M_parent

Usage: python tools/skel_pose.py --list
       python tools/skel_pose.py <skeletonType> <motionId> <tick> out.png [--yaw Y --pitch P --size N]
"""
import argparse
import math
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from pol_parse import header_count, load_pol, parse_model, read_header, triangles  # noqa: E402
from pol_render import Textures, render  # noqa: E402
from tex_export import Exe  # noqa: E402

ANGLE = 2 * math.pi / 65536


def rot_x(a):
    c, s = math.cos(a * ANGLE), math.sin(a * ANGLE)
    return np.array([[1, 0, 0, 0], [0, c, s, 0], [0, -s, c, 0], [0, 0, 0, 1]], np.float64)


def rot_y(a):
    c, s = math.cos(a * ANGLE), math.sin(a * ANGLE)
    return np.array([[c, 0, -s, 0], [0, 1, 0, 0], [s, 0, c, 0], [0, 0, 0, 1]], np.float64)


def rot_z(a):
    c, s = math.cos(a * ANGLE), math.sin(a * ANGLE)
    return np.array([[c, s, 0, 0], [-s, c, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]], np.float64)


def translate(x, y, z):
    m = np.eye(4)
    m[3, :3] = (x, y, z)
    return m


class Node:
    def __init__(self, exe, va):
        w = exe.bytes(va, 0x18)
        self.va = va
        self.model_id, ox, oy, oz, self.flags = struct.unpack_from("<IfffI", w)
        self.offset = (ox, oy, oz)
        self.index, n = struct.unpack_from("<hh", w, 0x14)
        self.children = [Node(exe, exe.u32(va + 0x18 + 4 * i)) for i in range(n)]

    def walk(self):
        yield self
        for c in self.children:
            yield from c.walk()


def skeleton(exe, t):
    return Node(exe, exe.u32(0x004E0430 + 4 * t)), exe.s16(0x004E0724 + 2 * t)


def mot_files(exe):
    misses, n = 0, 0
    while misses < 8:
        name = exe.cstr(exe.u32(0x004D1B00 + 4 * n))
        if name and name.endswith(".bin"):
            misses = 0
            cnt = struct.unpack("<H", exe.bytes(0x004E2BDC + 2 * n, 2))[0]
            p = exe.u32(0x004E2B14 + 4 * n)
            ids = list(struct.unpack(f"<{cnt}h", exe.bytes(p, 2 * cnt))) if 0 < cnt < 4096 and exe.lo <= p < exe.hi else []
            yield n, name, ids
        else:
            misses += 1
        n += 1


def motion_data(exe, motion_id):
    """Returns (bytes of the motion block, ticks) for a global motion id."""
    for n, name, ids in mot_files(exe):
        if motion_id in ids and os.path.exists(os.path.join("game/mot", name)):
            d = open(os.path.join("game/mot", name), "rb").read()
            k = ids.index(motion_id)
            offs = list(struct.unpack_from(f"<{len(ids)}I", d)) + [len(d)]
            return d[offs[k]:offs[k + 1]], exe.s16(0x004E07D0 + 2 * motion_id)
    raise KeyError(f"motion {motion_id} not found")


def frame_at(block, nodes, frame):
    stride = (12 + 6 * nodes + 3) & ~3
    base = 4 + frame * stride
    root = struct.unpack_from("<3f", block, base)
    rots = [struct.unpack_from("<3h", block, base + 12 + 6 * i) for i in range(nodes)]
    return root, rots


def pose(root_node, nodes, block, tick):
    """World matrices per node index (exact stored frame for even ticks)."""
    root_pos, rots = frame_at(block, nodes, tick // 2)
    mats = {}
    r = rots[0]
    mats[root_node.index] = rot_x(r[0]) @ rot_y(r[1]) @ rot_z(r[2]) @ translate(*root_pos)

    def visit(node, parent):
        a = rots[node.index]
        m = rot_x(a[0]) @ rot_y(a[1]) @ rot_z(a[2]) @ translate(*node.offset) @ parent
        mats[node.index] = m
        for c in node.children:
            visit(c, m)

    for c in root_node.children:
        visit(c, mats[root_node.index])
    return mats


class ModelLookup:
    def __init__(self, exe):
        from pol_parse import pol_files
        self.exe = exe
        self.lists = {n: (name, ids) for n, name, ids in pol_files(exe)}
        self.cache = {}

    def get(self, model_id):
        f = struct.unpack("<H", self.exe.bytes(0x004E83B4 + 2 * model_id, 2))[0]
        name, ids = self.lists.get(f, (None, []))
        if not name or model_id not in ids or not os.path.exists(os.path.join("game/pol", name)):
            return None, None
        if name not in self.cache:
            data, _ = load_pol(name)
            self.cache[name] = (data, read_header(data, header_count(data)))
        data, hdr = self.cache[name]
        k = ids.index(model_id)
        if k + 1 >= len(hdr):
            return name, None
        return name, parse_model(data, hdr[k][2], hdr[k + 1][0])[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--dump")
    ap.add_argument("skel", nargs="?", type=int)
    ap.add_argument("motion", nargs="?", type=int)
    ap.add_argument("tick", nargs="?", type=int, default=0)
    ap.add_argument("out", nargs="?")
    ap.add_argument("--yaw", type=float, default=200)
    ap.add_argument("--pitch", type=float, default=10)
    ap.add_argument("--size", type=int, default=512)
    args = ap.parse_args()
    exe = Exe()
    look = ModelLookup(exe)

    if args.dump:
        # Reference poses for tests/anim_test: per skeleton type, up to 3 size-consistent motions, 2 even ticks.
        motions = []
        for n, name, ids in mot_files(exe):
            p = os.path.join("game/mot", name)
            if ids and os.path.exists(p):
                d = open(p, "rb").read()
                offs = list(struct.unpack_from(f"<{len(ids)}I", d)) + [len(d)]
                for k, mid in enumerate(ids):
                    motions.append((mid, d[offs[k]:offs[k + 1]], exe.s16(0x004E07D0 + 2 * mid)))
        lines = []
        for t in range(64):
            root, nodes = skeleton(exe, t)
            order = list(root.walk())
            picked = 0
            for mid, block, ticks in motions:
                stored = (ticks - 1) // 2 + 2
                if ticks <= 0 or 4 + stored * ((12 + 6 * nodes + 3) & ~3) != len(block):
                    continue
                for tick in sorted({0, ((ticks - 1) // 2) * 2}):
                    mats = pose(root, nodes, block, tick)
                    for i, nd in enumerate(order):
                        vals = " ".join(f"{v:.6g}" for v in mats[nd.index].reshape(16))
                        lines.append(f"{t} {mid} {tick} {i} {vals}")
                picked += 1
                if picked == 3:
                    break
        with open(args.dump, "w") as f:
            f.write("\n".join(lines) + "\n")
        print(f"{len(lines)} node matrices")
        return

    if args.list:
        for t in range(64):
            root, n = skeleton(exe, t)
            nodes = list(root.walk())
            files = {look.get(nd.model_id)[0] for nd in nodes if nd.model_id}
            print(f"type {t:2}: {n:2} nodes (tree {len(nodes)}), part files {sorted(f for f in files if f)}")
        return

    root, nodes = skeleton(exe, args.skel)
    block, ticks = motion_data(exe, args.motion)
    stored = (ticks - 1) // 2 + 2
    if 4 + stored * ((12 + 6 * nodes + 3) & ~3) != len(block):
        print(f"warning: motion {args.motion} size {len(block)} does not match {nodes} nodes x {stored} frames")
    mats = pose(root, nodes, block, min(args.tick, ticks - 1))
    texs = Textures(exe)
    tris, parts, missing = [], 0, 0
    for nd in root.walk():
        if not nd.model_id:
            continue
        name, meshes = look.get(nd.model_id)
        if meshes is None:
            missing += 1
            continue
        parts += 1
        m = mats[nd.index]
        rot = m[:3, :3]
        for mesh in meshes:
            tex = texs.get(name, mesh.material[8])
            for tri in triangles(mesh):
                out = []
                for v in tri:
                    p = np.array([v[0], v[1], v[2], 1.0]) @ m
                    nrm = np.array(v[3:6]) @ rot
                    out.append((p[0], p[1], p[2], nrm[0], nrm[1], nrm[2], v[6], v[7]))
                tris.append((out, tex))
    render(tris, args.out, args.yaw, args.pitch, args.size)
    print(f"{args.out}: skeleton {args.skel} ({nodes} nodes), motion {args.motion} ({ticks} ticks) tick {args.tick}: "
          f"{parts} parts, {missing} missing, {len(tris)} triangles")


if __name__ == "__main__":
    main()
