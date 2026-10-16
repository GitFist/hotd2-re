"""Offline textured renderer for HOTD2 pol models (visual verification of the model/texture formats).

Software z-buffer rasterizer (numpy): perspective camera fitted to the model, Lambert lighting from the
vertex normals, textures resolved like the game (descriptor index into the same-named tex file), sampled in the game's surface layout (v=0 is
the first stored row) with wrap addressing; texels with alpha < 0.5 are discarded.

Usage: python tools/pol_render.py <pol file> <model index|all> <out.png> [--yaw 35] [--pitch 25] [--size 512]
"""
import argparse
import math
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from pol_parse import Exe, header_count, iter_models, load_pol, parse_model, read_header, triangles  # noqa: E402
from tex_export import (decode_texture, descriptors, load_file, load_palette_banks, palette_for,  # noqa: E402
                        rgba_bytes, texture_files)


class Textures:
    r"""Material texture field = descriptor index into the texture file named like the pol file
    (pol\X.bin -> tex\X.bin; pol\pol_X.bin -> tex\X.bin, inferred). Verified at runtime: in memory the
    field becomes 0x40000000 | descriptor.id of that table (389/389 logged models)."""

    def __init__(self, exe):
        self.exe = exe
        self.tables = {}
        self.banks = load_palette_banks(exe)
        self.emu = None
        self.cache = {}
        for n, name, desc, count in texture_files(exe):
            self.tables.setdefault(name, (n, list(descriptors(exe, desc, count))))

    def table_for(self, pol_name):
        if pol_name in self.tables:
            return pol_name
        if pol_name.startswith("pol_") and pol_name[4:] in self.tables:
            return pol_name[4:]
        return None

    def get(self, pol_name, field):
        if field == 0xFFFFFFFF:
            return None
        tex_name = self.table_for(pol_name)
        key = (tex_name, field)
        if key in self.cache:
            return self.cache[key]
        img = None
        if tex_name and field < len(self.tables[tex_name][1]) and os.path.exists(os.path.join("game/tex", tex_name)):
            n, descs = self.tables[tex_name]
            d = descs[field]
            pal = None
            if (d["fmt"] >> 8) == 5:
                bank = -1
                if n != 1:
                    if self.emu is None:
                        from emu import ExeEmu
                        self.emu = ExeEmu()
                    bank = self.emu.call_cdecl(0x0041C9E0, n, d["index"])
                    bank = bank - (1 << 32) if bank & 0x80000000 else bank
                pal = palette_for(self.banks, bank, d["fmt"] & 7)
            try:
                w, h, tx = decode_texture(load_file(tex_name), d, pal)
                img = np.frombuffer(rgba_bytes(tx, d["fmt"] & 7), np.uint8).reshape(h, w, 4).astype(np.float32) / 255
            except (ValueError, IndexError):
                img = None
        self.cache[key] = img
        return img

def render(tris, out, yaw, pitch, size):
    """tris: list of (verts[3] of 8-float tuples, texture ndarray|None)."""
    pts = np.array([v[:3] for t, _ in tris for v in t], np.float32)
    center = (pts.min(0) + pts.max(0)) / 2
    radius = float(np.linalg.norm(pts - center, axis=1).max()) or 1.0
    cy, sy, cp, sp = math.cos(math.radians(yaw)), math.sin(math.radians(yaw)), math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    rot = np.array([[cy, 0, -sy], [0, 1, 0], [sy, 0, cy]], np.float32) @ np.array([[1, 0, 0], [0, cp, sp], [0, -sp, cp]], np.float32)
    dist = radius * 2.6
    focal = size * 1.1
    color = np.zeros((size, size, 3), np.float32) + np.array([0.18, 0.2, 0.26], np.float32)
    depth = np.full((size, size), np.inf, np.float32)
    light = np.array([0.4, 0.8, 0.45], np.float32)
    light /= np.linalg.norm(light)
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float32)
    for verts, tex in tris:
        v = np.array(verts, np.float32)
        p = (v[:, :3] - center) @ rot.T
        z = p[:, 2] + dist
        if (z <= 0.01).any():
            continue
        sx = size / 2 + p[:, 0] * focal / z
        sy_ = size / 2 - p[:, 1] * focal / z
        x0, x1 = int(max(0, math.floor(sx.min()))), int(min(size - 1, math.ceil(sx.max())))
        y0, y1 = int(max(0, math.floor(sy_.min()))), int(min(size - 1, math.ceil(sy_.max())))
        if x0 > x1 or y0 > y1:
            continue
        area = (sx[1] - sx[0]) * (sy_[2] - sy_[0]) - (sx[2] - sx[0]) * (sy_[1] - sy_[0])
        if abs(area) < 1e-6:
            continue
        X, Y = xs[y0:y1 + 1, x0:x1 + 1], ys[y0:y1 + 1, x0:x1 + 1]
        w0 = ((sx[1] - X) * (sy_[2] - Y) - (sx[2] - X) * (sy_[1] - Y)) / area
        w1 = ((sx[2] - X) * (sy_[0] - Y) - (sx[0] - X) * (sy_[2] - Y)) / area
        w2 = 1 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            continue
        iz = w0 / z[0] + w1 / z[1] + w2 / z[2]  # perspective-correct interpolation
        zz = 1 / np.where(inside, iz, 1)
        cur = depth[y0:y1 + 1, x0:x1 + 1]
        mask = inside & (zz < cur)
        if not mask.any():
            continue
        n = v[:, 3:6] @ rot.T
        nrm = (w0[..., None] * n[0] / z[0] + w1[..., None] * n[1] / z[1] + w2[..., None] * n[2] / z[2]) * zz[..., None]
        shade = 0.35 + 0.65 * np.abs((nrm / (np.linalg.norm(nrm, axis=-1, keepdims=True) + 1e-9)) @ light)
        if tex is not None:
            u = (w0 * v[0, 6] / z[0] + w1 * v[1, 6] / z[1] + w2 * v[2, 6] / z[2]) * zz
            t = (w0 * v[0, 7] / z[0] + w1 * v[1, 7] / z[1] + w2 * v[2, 7] / z[2]) * zz
            th, tw = tex.shape[:2]
            tu = (np.floor(u * tw).astype(np.int64)) % tw
            tv = (np.floor(t * th).astype(np.int64)) % th
            texel = tex[tv, tu]
            mask &= texel[..., 3] >= 0.5
            rgb = texel[..., :3]
        else:
            rgb = np.full(X.shape + (3,), 0.7, np.float32)
        cur[mask] = zz[mask]
        color[y0:y1 + 1, x0:x1 + 1][mask] = (rgb * shade[..., None])[mask]
    Image.fromarray((np.clip(color, 0, 1) * 255).astype(np.uint8)).save(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("model")
    ap.add_argument("out")
    ap.add_argument("--yaw", type=float, default=35)
    ap.add_argument("--pitch", type=float, default=25)
    ap.add_argument("--size", type=int, default=512)
    args = ap.parse_args()
    exe = Exe()
    texs = Textures(exe)
    data, _ = load_pol(args.file)
    hdr = read_header(data, header_count(data))
    picks = range(len(hdr) - 1) if args.model == "all" else [int(args.model)]
    tris, textured, untextured, missing = [], 0, 0, 0
    for k in picks:
        _, meshes = parse_model(data, hdr[k][2], hdr[k + 1][0])
        for m in meshes:
            tex = texs.get(args.file, m.material[8])
            if m.material[8] == 0xFFFFFFFF:
                untextured += 1
            elif tex is None:
                missing += 1
            else:
                textured += 1
            tris.extend((t, tex) for t in triangles(m))
    render(tris, args.out, args.yaw, args.pitch, args.size)
    print(f"{args.out}: {len(tris)} triangles; meshes textured {textured}, untextured {untextured}, texture missing {missing}")


if __name__ == "__main__":
    main()
