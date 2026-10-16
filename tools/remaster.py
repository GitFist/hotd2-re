"""Optional "original look" asset pack for the port: inventory, classification, pilot previews, batch upscaling of
every texture, and 48 kHz resampling of every WAV. Nothing here touches the game's files; everything it writes goes
under remaster\\ (ignored by git). The port loads the pack with HOTD2_ASSET_DIR=<dir> (or assets=<dir> in
hotd2re_settings.ini). See notes/agents/remaster.md.

Stages (run in this order; each one is resumable and only reads the previous stage's output):
  python tools/remaster.py inventory                 -> remaster\\inventory.json (needs remaster\\src_png from
                                                        tools/tex_export.py --out remaster\\src_png --hashes remaster\\tex_hashes.txt)
  python tools/remaster.py classify                  -> remaster\\classes.json
  python tools/remaster.py pilot [--count N]         -> remaster\\preview\\*.png contact sheets (original vs candidates)
  python tools/remaster.py batch [--scale 4] [--class ui_pixel,...] [--method M] [--dry-run] [--force] [--limit N]
                                                     -> remaster\\tex\\<file>\\<index>.png + remaster\\manifest.json
  python tools/remaster.py audio [--rate 48000] [--dry-run] [--force]
                                                     -> remaster\\sound\\... + remaster\\sound_manifest.json

Pack layout read by the port (src/port/render/asset_store.cpp, src/port/audio/*): tex\\<tex file stem>\\<index>.png
(upright, as exported; the loader flips to the game's bottom-up row order), bmp\\<file>.png for the BMP front
screens, sound\\<path as in the game>.
"""
import argparse
import collections
import hashlib
import json
import os
import re
import struct
import sys
import time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from tex_export import Exe, texture_files, descriptors, LAYOUT_NAMES, PIXEL_NAMES  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
GAME = os.path.join(ROOT, "game")
OUT = os.environ.get("HOTD2_REMASTER_OUT") or os.path.join(ROOT, "remaster")  # the pack (never committed)
SRC_PNG = os.path.join(OUT, "src_png")

SPRITE_SLOT_TABLE, SPRITE_TEXID_TABLE = 0x0057A5BC, 0x0057D448   # game::SpriteTables (sprite id -> file slot / texture id)
SPRITE_COUNT = (SPRITE_TEXID_TABLE - SPRITE_SLOT_TABLE) // 4       # 2979 ids
SKELETON_ROOTS, SKELETON_COUNT = 0x004E0430, 64                     # tools/skel_pose.py
MODEL_FILE_TABLE = 0x004E83B4

# TSP decoding as in ModelRenderer::BuildMeshes / Mdl_ApplyPvrState 0x004A7780.
BLEND_NAMES = ["zero", "one", "other", "inv_other", "src_alpha", "inv_src_alpha", "dst_alpha", "inv_dst_alpha"]
ADDR_NAMES = ["wrap", "mirror", "clamp", "mirror"]


def tsp_info(tsp):
    return {
        "translucent": (tsp & 0x180000) != 0x80000,
        "src": BLEND_NAMES[tsp >> 29],
        "dst": BLEND_NAMES[(tsp >> 26) & 7],
        "addr_u": ADDR_NAMES[(((tsp >> 3) & 0x8000) | (tsp & 0x10000)) >> 15],
        "addr_v": ADDR_NAMES[(((tsp >> 3) & 0x4000) | (tsp & 0x8000)) >> 14],
        "linear": (tsp & 0x6000) != 0,
        "alpha_from_texture": ((tsp >> 6) & 3) == 1,
    }


def src_png_path(name, index):
    return os.path.join(SRC_PNG, f"{os.path.splitext(name)[0]}_{index:03d}.png")


def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def save_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=1)
    os.replace(tmp, path)


# --- inventory ------------------------------------------------------------------------------------------------

def analyze_png(path):
    """Alpha use, colour count and wrap-seam continuity of an exported texture."""
    img = np.asarray(Image.open(path).convert("RGBA"))
    a = img[..., 3]
    n = a.size
    transparent = int((a == 0).sum())
    partial = int(((a > 0) & (a < 255)).sum())
    kind = "opaque" if transparent == 0 and partial == 0 else ("binary" if partial == 0 else "gradient")
    packed = img.view(np.uint32).reshape(-1) if img.flags["C_CONTIGUOUS"] else np.ascontiguousarray(img).view(np.uint32).reshape(-1)
    colors = int(np.unique(packed).size)
    f = img[..., :3].astype(np.int16)
    eps = 1e-3

    def seam(axis):
        if img.shape[1 - axis] < 2:
            return None
        if axis == 0:  # u: left column vs right column
            s = np.abs(f[:, 0] - f[:, -1]).mean()
            i = np.abs(f[:, 1:] - f[:, :-1]).mean()
        else:
            s = np.abs(f[0] - f[-1]).mean()
            i = np.abs(f[1:] - f[:-1]).mean()
        return round(float(s / (i + eps)), 3), round(float(i), 3)

    su, sv = seam(0), seam(1)
    return {
        "alpha": kind,
        "transparent_frac": round(transparent / n, 4),
        "partial_frac": round(partial / n, 4),
        "colors": colors,
        "seam_u": su[0] if su else None,  # seam difference / interior difference (1.0 = continuous like the interior)
        "seam_v": sv[0] if sv else None,
        "detail": su[1] if su else None,  # mean adjacent-pixel difference (texture busyness)
    }


def wav_info(path):
    d = open(path, "rb").read()
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        return None
    pos, info, chunks = 12, {}, []
    while pos + 8 <= len(d):
        cid, sz = d[pos:pos + 4], struct.unpack_from("<I", d, pos + 4)[0]
        chunks.append(cid.decode("latin1").strip())
        if cid == b"fmt ":
            tag, ch, sr, _, ba, bps = struct.unpack_from("<HHIIHH", d, pos + 8)
            info.update(format_tag=tag, channels=ch, rate=sr, bits=bps, block_align=ba)
        elif cid == b"data":
            info["data_bytes"] = min(sz, len(d) - pos - 8)
        pos += 8 + sz + (sz & 1)
    if "block_align" in info and info.get("block_align"):
        info["frames"] = info["data_bytes"] // info["block_align"]
        info["seconds"] = round(info["frames"] / info["rate"], 3)
    info["chunks"] = chunks
    info["size"] = len(d)
    return info


def cmd_inventory(args):
    from pol_parse import pol_files, all_pol_models, parse_model, FormatError
    from skel_pose import skeleton

    t0 = time.time()
    exe = Exe()
    tex_table = {}  # name -> (slot, descriptors)
    slot_of = {}
    for n, name, desc, count in texture_files(exe):
        if name in tex_table:
            continue  # first entry wins (AssetStore does the same)
        tex_table[name] = (n, list(descriptors(exe, desc, count)))
        slot_of[n] = name
    on_disk = sorted(os.listdir(os.path.join(GAME, "tex")))
    print(f"texture table: {len(tex_table)} files; on disk: {len(on_disk)}")

    # Palette banks per pal4 texture (from tex_export --hashes, emulated Tex_GetPaletteBank).
    banks = {}
    bank_file = os.path.join(OUT, "tex_hashes_banks.txt")
    if os.path.exists(bank_file):
        for line in open(bank_file):
            parts = line.split()
            if len(parts) == 3:
                banks[(parts[0], int(parts[1]))] = int(parts[2])
    hashes = {}
    hash_file = os.path.join(OUT, "tex_hashes.txt")
    if os.path.exists(hash_file):
        for line in open(hash_file):
            parts = line.split()
            if len(parts) == 5:
                hashes[(parts[0], int(parts[1]))] = parts[4]

    # Model users: every pol file on disk (tabled or pol_* copies) -> materials -> (tex file, descriptor index).
    users = collections.defaultdict(lambda: {"pol_files": set(), "meshes": 0, "tsp": set(), "uv": [1e9, -1e9, 1e9, -1e9]})
    pol_stats = collections.Counter()
    pol_file_set = set()
    for name, k, data, start, end, root in all_pol_models(exe):
        pol_file_set.add(name)
        pol_stats["models"] += 1
        try:
            hdr, meshes = parse_model(data, root, end)
        except (FormatError, struct.error):
            pol_stats["models_failed"] += 1
            continue
        stem = name[:-4]
        tex_name = name if name in tex_table else (stem[4:] + ".bin" if stem.startswith("pol_") and stem[4:] + ".bin" in tex_table else None)
        for m in meshes:
            pol_stats["meshes"] += 1
            pol_stats["strips"] += len(m.strips)
            pol_stats["vertices"] += sum(len(s[2]) for s in m.strips)
            ref = m.material[8]
            if ref == 0xFFFFFFFF or tex_name is None:
                continue
            if ref >= len(tex_table[tex_name][1]):
                pol_stats["texture_refs_out_of_range"] += 1
                continue
            u = users[(tex_name, ref)]
            u["pol_files"].add(name)
            u["meshes"] += 1
            u["tsp"].add(m.material[2])
            for _, _, verts in m.strips:
                for v in verts:
                    if v[6] < u["uv"][0]: u["uv"][0] = v[6]
                    if v[6] > u["uv"][1]: u["uv"][1] = v[6]
                    if v[7] < u["uv"][2]: u["uv"][2] = v[7]
                    if v[7] > u["uv"][3]: u["uv"][3] = v[7]
    pol_stats["files"] = len(pol_file_set)
    print(f"pol: {dict(pol_stats)} ({time.time() - t0:.0f}s)")

    # Sprite users: sprite id -> (file slot, texture id) -> descriptor index.
    id_index = {name: {d["id"]: d["index"] for d in descs} for name, (n, descs) in tex_table.items()}
    sprite_users = collections.defaultdict(list)
    sprite_files = set()
    for sid in range(SPRITE_COUNT):
        slot, tid = exe.u32(SPRITE_SLOT_TABLE + sid * 4), exe.u32(SPRITE_TEXID_TABLE + sid * 4)
        name = slot_of.get(slot)
        if name is None:
            continue
        sprite_files.add(name)
        idx = id_index[name].get(tid)
        if idx is not None:
            sprite_users[(name, idx)].append(sid)

    # Character skins: texture files of the pol files drawn at skeleton nodes.
    pol_names = {n: name for n, name, ids in pol_files(exe)}
    skeleton_files = collections.defaultdict(set)
    for t in range(SKELETON_COUNT):
        try:
            root, _ = skeleton(exe, t)
        except Exception:
            continue
        for node in root.walk():
            if node.model_id:
                f = struct.unpack("<H", exe.bytes(MODEL_FILE_TABLE + 2 * node.model_id, 2))[0]
                name = pol_names.get(f)
                if name in tex_table:
                    skeleton_files[name].add(t)

    textures = []
    missing_png = 0
    for name, (slot, descs) in tex_table.items():
        if not os.path.exists(os.path.join(GAME, "tex", name)):
            continue
        for d in descs:
            png = src_png_path(name, d["index"])
            rec = {
                "file": name, "index": d["index"], "id": d["id"], "w": d["w"], "h": d["h"],
                "fmt": f"0x{d['fmt']:X}", "layout": LAYOUT_NAMES.get((d["fmt"] >> 8) & 0xFF, "?"),
                "pixel": PIXEL_NAMES.get(d["fmt"] & 7, "?"),
                "bank": banks.get((name, d["index"])), "sha256": hashes.get((name, d["index"])),
            }
            if os.path.exists(png):
                rec.update(analyze_png(png))
            else:
                missing_png += 1
            u = users.get((name, d["index"]))
            if u:
                tsps = sorted(u["tsp"])
                infos = [tsp_info(t) for t in tsps]
                rec["models"] = {
                    "pol_files": sorted(u["pol_files"]), "meshes": u["meshes"],
                    "tsp": [f"0x{t:08X}" for t in tsps],
                    "translucent": any(i["translucent"] for i in infos),
                    "opaque_pass": any(not i["translucent"] for i in infos),
                    "blend": sorted({f"{i['src']}/{i['dst']}" for i in infos}),
                    "addr_u": sorted({i["addr_u"] for i in infos}), "addr_v": sorted({i["addr_v"] for i in infos}),
                    "linear": any(i["linear"] for i in infos),
                    "uv": [round(x, 3) for x in u["uv"]],
                }
            sp = sprite_users.get((name, d["index"]))
            if sp:
                rec["sprites"] = sp
            if name in skeleton_files:
                rec["skeletons"] = sorted(skeleton_files[name])
            textures.append(rec)
    print(f"textures: {len(textures)} (missing PNG: {missing_png}) ({time.time() - t0:.0f}s)")

    # Texture files that are not in the exe table: BMP front screens and leftovers.
    untabled = []
    for name in on_disk:
        if name in tex_table:
            continue
        p = os.path.join(GAME, "tex", name)
        head = open(p, "rb").read(30)
        entry = {"file": name, "size": os.path.getsize(p)}
        if head[:2] == b"BM" and len(head) >= 30:
            entry.update(kind="bmp", w=struct.unpack_from("<i", head, 18)[0], h=struct.unpack_from("<i", head, 22)[0],
                         bpp=struct.unpack_from("<H", head, 28)[0])
        else:
            entry["kind"] = "unknown"
        untabled.append(entry)

    sounds = []
    sroot = os.path.join(GAME, "Sound")
    for dp, _, fn in os.walk(sroot):
        for f in sorted(fn):
            p = os.path.join(dp, f)
            info = wav_info(p)
            rel = os.path.relpath(p, GAME)
            if info is None:
                sounds.append({"path": rel, "error": "not a RIFF WAVE"})
            else:
                info["path"] = rel
                sounds.append(info)

    inv = {
        "generated": time.strftime("%Y-%m-%d %H:%M:%S"),
        "texture_files_tabled": len(tex_table), "texture_files_on_disk": len(on_disk),
        "sprite_texture_files": sorted(sprite_files),
        "skeleton_texture_files": {k: sorted(v) for k, v in sorted(skeleton_files.items())},
        "pol": dict(pol_stats),
        "textures": textures,
        "untabled_tex_files": untabled,
        "sounds": sounds,
    }
    save_json(os.path.join(OUT, "inventory.json"), inv)
    print_inventory_summary(inv)
    print(f"wrote {os.path.join(OUT, 'inventory.json')} ({time.time() - t0:.0f}s)")


def print_inventory_summary(inv):
    tex = inv["textures"]
    c = collections.Counter()
    for t in tex:
        c["model+sprite" if "models" in t and "sprites" in t else "model" if "models" in t else "sprite" if "sprites" in t else "unreferenced"] += 1
    print(f"textures {len(tex)}: users {dict(c)}")
    c = collections.Counter(t.get("alpha", "?") for t in tex)
    print(f"  alpha: {dict(c)}")
    c = collections.Counter((t["layout"], t["pixel"]) for t in tex)
    print("  formats: " + ", ".join(f"{k[0]}/{k[1]} {v}" for k, v in c.most_common()))
    c = collections.Counter(f"{t['w']}x{t['h']}" for t in tex)
    print("  sizes: " + ", ".join(f"{k} {v}" for k, v in c.most_common(12)))
    print(f"untabled tex files: {len(inv['untabled_tex_files'])} "
          f"(bmp {sum(1 for u in inv['untabled_tex_files'] if u['kind'] == 'bmp')})")
    s = inv["sounds"]
    c = collections.Counter((x["path"].split(os.sep)[1], x.get("rate"), x.get("channels"), x.get("bits")) for x in s if "rate" in x)
    print(f"sounds {len(s)}: " + ", ".join(f"{k[0]} {k[1]} Hz {k[2]}ch {k[3]}b: {v}" for k, v in sorted(c.items(), key=lambda kv: (kv[0][0], kv[0][1]))))
    print(f"  total {sum(x.get('seconds', 0) for x in s) / 60:.1f} min, {sum(x['size'] for x in s) / 1e6:.0f} MB")
    print(f"pol: {inv['pol']}")


# --- classification -------------------------------------------------------------------------------------------

SKY_FILE = re.compile(r"^st\d_1b?\.bin$")


def classify_texture(t, sprite_files):
    """Returns (class, reasons). Classes: ui_pixel, ui_soft, sky, skin, effect_add, effect_alpha, surface, unused."""
    reasons = []
    name = t["file"]
    m = t.get("models")
    colors = t.get("colors", 0)
    pixels = t["w"] * t["h"]
    if "sprites" in t or name in sprite_files:
        reasons.append("sprite table" if "sprites" in t else "sprite-table file")
        # Pixel art (fonts, digits, icons, frames): few colours for its size, or a tiny image. Painted / photographic
        # UI (logos, backdrops, blood splats) gets the surface method.
        if colors <= 48 or (colors <= 160 and pixels <= 64 * 64) or t.get("alpha") == "binary" and colors <= 256:
            reasons.append(f"{colors} colours")
            return "ui_pixel", reasons
        reasons.append(f"{colors} colours, {t.get('alpha')} alpha")
        return "ui_soft", reasons
    if m:
        if any(SKY_FILE.match(p) or SKY_FILE.match(p[4:]) for p in m["pol_files"]):
            reasons.append("sky model file")
            return "sky", reasons
        if "skeletons" in t:
            reasons.append(f"skeleton types {t['skeletons'][:4]}")
            return "skin", reasons
        if m["translucent"] and not m["opaque_pass"]:
            if any(b.endswith("/one") for b in m["blend"]):
                reasons.append(f"additive {m['blend']}")
                return "effect_add", reasons
            reasons.append(f"translucent {m['blend']}")
            return "effect_alpha", reasons
        if name.startswith("eff_") or name == "common.bin" and t.get("alpha") == "gradient":
            reasons.append("effect file / gradient alpha in common.bin")
            return "effect_alpha", reasons
        reasons.append("opaque model material")
        return "surface", reasons
    if name.startswith("scr_"):  # e.g. scr_blood_green.bin, the boot-time swap for scr_blood_red's sprites
        reasons.append("unreferenced 2D screen file")
        return ("ui_pixel" if colors <= 48 else "ui_soft"), reasons
    reasons.append("no model or sprite user found")
    return "unused", reasons


def _crosses_seam(lo, hi, tol=0.02):
    """The sampled UV range runs across an integer boundary (model v ranges are usually [-1, 0]: stored bottom-up)."""
    import math
    return math.floor(lo + tol) != math.floor(hi - tol)


def tiled_axes(t):
    """Per axis: ("wrap" | "mirror" | "clamp", tiled). Tiled = a referencing material wraps and the model UVs cross
    the texture's edge; with no model user the image's own seam continuity decides. The mode picks the filter's
    padding (wrap across the seam, symmetric for mirrored materials, edge replication for clamped ones)."""
    m = t.get("models")
    tiled_u = tiled_v = False
    mode_u = mode_v = "clamp"
    why = []
    if m:
        u0, u1, v0, v1 = m["uv"]
        mode_u = "wrap" if "wrap" in m["addr_u"] else "mirror" if "mirror" in m["addr_u"] else "clamp"
        mode_v = "wrap" if "wrap" in m["addr_v"] else "mirror" if "mirror" in m["addr_v"] else "clamp"
        if mode_u == "wrap" and _crosses_seam(u0, u1):
            tiled_u, why = True, why + [f"u {u0}..{u1}"]
        if mode_v == "wrap" and _crosses_seam(v0, v1):
            tiled_v, why = True, why + [f"v {v0}..{v1}"]
    else:
        # Image continuity: the wrap seam differs no more than the interior does (busy textures only; flat ones
        # are continuous everywhere and are safe either way).
        if t.get("seam_u") is not None and t["seam_u"] <= 1.3 and (t.get("detail") or 0) > 2:
            tiled_u, mode_u, why = True, "wrap", why + [f"seam_u {t['seam_u']}"]
        if t.get("seam_v") is not None and t["seam_v"] <= 1.3 and (t.get("detail") or 0) > 2:
            tiled_v, mode_v, why = True, "wrap", why + [f"seam_v {t['seam_v']}"]
    return (mode_u if tiled_u else ("mirror" if mode_u == "mirror" else "clamp"), tiled_u), \
           (mode_v if tiled_v else ("mirror" if mode_v == "mirror" else "clamp"), tiled_v), why


def cmd_classify(args):
    inv = load_json(os.path.join(OUT, "inventory.json"))
    sprite_files = set(inv["sprite_texture_files"])
    classes = {}
    counts = collections.Counter()
    tiled_counts = collections.Counter()
    for t in inv["textures"]:
        cls, reasons = classify_texture(t, sprite_files)
        (mu, tu), (mv, tv), why = tiled_axes(t)
        key = f"{t['file']}:{t['index']}"
        classes[key] = {"class": cls, "tiled_u": tu, "tiled_v": tv, "edge_u": mu, "edge_v": mv, "alpha": t.get("alpha"),
                        "why": reasons + why}
        counts[cls] += 1
        tiled_counts[("u" if tu else "") + ("v" if tv else "") or "none"] += 1
    out = {"generated": time.strftime("%Y-%m-%d %H:%M:%S"), "counts": dict(counts), "tiled": dict(tiled_counts),
           "textures": classes}
    save_json(os.path.join(OUT, "classes.json"), out)
    print("classes:", dict(counts))
    print("tiled:", dict(tiled_counts))


# --- pilot / batch ---------------------------------------------------------------------------------------------

# Method per class, chosen in the pilot (notes/agents/remaster.md). Keys: see remaster_filters.METHODS.
CLASS_METHOD = {
    "ui_pixel": "xbr",
    "ui_soft": "lanczos_clamp",
    "sky": "lanczos_clamp",
    "skin": "lanczos_sharp",
    "effect_add": "lanczos_clamp",
    "effect_alpha": "lanczos_clamp",
    "surface": "lanczos_sharp",
    "unused": "lanczos_clamp",
}
PILOT_METHODS = {
    "ui_pixel": ["nearest", "scale2x", "xbr", "lanczos_clamp"],
    "ui_soft": ["nearest", "xbr", "lanczos_clamp", "lanczos_sharp"],
    "sky": ["nearest", "catmull", "lanczos_clamp", "lanczos_sharp"],
    "skin": ["nearest", "catmull", "lanczos_clamp", "lanczos_sharp"],
    "effect_add": ["nearest", "catmull", "lanczos_clamp", "xbr"],
    "effect_alpha": ["nearest", "catmull", "lanczos_clamp", "xbr"],
    "surface": ["nearest", "catmull", "lanczos_clamp", "lanczos_sharp"],
    "unused": ["nearest", "lanczos_clamp"],
}


def load_source(t):
    """Source texture as float32 RGBA 0..1 (upright, as exported)."""
    img = Image.open(src_png_path(t["file"], t["index"])).convert("RGBA")
    return np.asarray(img).astype(np.float32) / 255.0


def to_image(arr):
    return Image.fromarray(np.clip(np.rint(arr * 255.0), 0, 255).astype(np.uint8), "RGBA")


def checkerboard(w, h, cell=8):
    y, x = np.mgrid[0:h, 0:w]
    c = (((x // cell) + (y // cell)) & 1).astype(np.float32)
    g = 0.55 + 0.25 * c
    return np.stack([g, g, g, np.ones_like(g)], axis=-1)


def composite(arr):
    """RGBA over a checkerboard, for the contact sheets."""
    bg = checkerboard(arr.shape[1], arr.shape[0])
    a = arr[..., 3:4]
    rgb = arr[..., :3] * a + bg[..., :3] * (1 - a)
    return np.concatenate([rgb, np.ones_like(a)], axis=-1)


def pick_pilot(inv, classes, count):
    """About `count` textures spread over the classes, preferring referenced, mid-sized, varied ones."""
    by_class = collections.defaultdict(list)
    for t in inv["textures"]:
        c = classes["textures"][f"{t['file']}:{t['index']}"]
        if t["w"] * t["h"] < 16 * 16 or t.get("colors", 0) < 3:
            continue
        by_class[c["class"]].append(t)
    want = {"ui_pixel": 5, "ui_soft": 3, "sky": 2, "skin": 4, "effect_add": 2, "effect_alpha": 2, "surface": 6}
    # Always include a few stage / prop surfaces (the bulk of what the player sees).
    forced = {("st1_03.bin", 1), ("st2_06.bin", 3), ("komono_st1b.bin", 0), ("bg_adv00.bin", 9)}
    picks = [t for t in inv["textures"] if (t["file"], t["index"]) in forced]
    rng = np.random.default_rng(7)
    for cls, n in want.items():
        pool = by_class.get(cls, [])
        if not pool:
            continue
        # Spread over files: one per file first, deterministic shuffle.
        rng.shuffle(pool)
        seen = set()
        chosen = []
        for t in sorted(pool, key=lambda t: -(t.get("detail") or 0) * (1 if 64 <= max(t["w"], t["h"]) <= 256 else 0.3)):
            if t["file"] in seen:
                continue
            seen.add(t["file"])
            chosen.append(t)
            if len(chosen) >= n:
                break
        picks.extend(chosen)
    return picks[:count] if count else picks


def contact_sheet(t, cls_info, methods, scale, out_path):
    import remaster_filters as rf
    from PIL import ImageDraw
    src = load_source(t)
    wrap = (cls_info["edge_u"], cls_info["edge_v"])
    binary = cls_info["alpha"] == "binary"
    panels = [("original x%d (nearest)" % scale, rf.upscale(src, scale, "nearest", wrap, binary))]
    for m in methods:
        if m == "nearest":
            continue
        panels.append((m, rf.upscale(src, scale, m, wrap, binary)))
    # Crop a region when large so the sheet stays readable.
    ph, pw = panels[0][1].shape[:2]
    max_side = 512
    cy0 = cx0 = 0
    if max(ph, pw) > max_side:
        cy0, cx0 = max(0, ph // 2 - max_side // 2), max(0, pw // 2 - max_side // 2)
    crops = [(n, composite(p[cy0:cy0 + max_side, cx0:cx0 + max_side])) for n, p in panels]
    ch, cw = crops[0][1].shape[:2]
    label_h = 18
    sheet = Image.new("RGB", (len(crops) * (cw + 8) + 8, ch + label_h * 2 + 16), (24, 24, 28))
    draw = ImageDraw.Draw(sheet)
    title = (f"{t['file']}:{t['index']}  {t['w']}x{t['h']} {t['layout']}/{t['pixel']}  class {cls_info['class']}  "
             f"alpha {cls_info['alpha']}  edges u={cls_info['edge_u']} v={cls_info['edge_v']}  x{scale}")
    draw.text((8, 4), title, fill=(230, 230, 230))
    for i, (n, c) in enumerate(crops):
        x = 8 + i * (cw + 8)
        sheet.paste(to_image(c).convert("RGB"), (x, label_h + 12))
        draw.text((x, label_h + ch + 14), n, fill=(200, 200, 200))
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    sheet.save(out_path)


def cmd_pilot(args):
    inv = load_json(os.path.join(OUT, "inventory.json"))
    classes = load_json(os.path.join(OUT, "classes.json"))
    picks = pick_pilot(inv, classes, args.count)
    index_lines = []
    for t in picks:
        key = f"{t['file']}:{t['index']}"
        ci = classes["textures"][key]
        methods = PILOT_METHODS[ci["class"]]
        for scale in (2, 4):
            out = os.path.join(OUT, "preview", f"{ci['class']}_{t['file'][:-4]}_{t['index']:03d}_x{scale}.png")
            if os.path.exists(out) and not args.force:
                continue
            t0 = time.time()
            contact_sheet(t, ci, methods, scale, out)
            print(f"{out} ({time.time() - t0:.1f}s)")
        index_lines.append(f"{ci['class']:12s} {key:28s} {t['w']}x{t['h']} {t['layout']}/{t['pixel']} alpha {ci['alpha']} "
                           f"edges {ci['edge_u']}/{ci['edge_v']} methods {methods}")
    with open(os.path.join(OUT, "preview", "index.txt"), "w") as f:
        f.write("\n".join(index_lines) + "\n")
    print(f"{len(picks)} pilot textures; sheets in {os.path.join(OUT, 'preview')}")


def cmd_batch(args):
    import remaster_filters as rf
    inv = load_json(os.path.join(OUT, "inventory.json"))
    classes = load_json(os.path.join(OUT, "classes.json"))
    manifest_path = os.path.join(OUT, "manifest.json")
    manifest = load_json(manifest_path) if os.path.exists(manifest_path) else {"textures": {}}
    only = set(args.classes.split(",")) if args.classes else None
    scales = [int(s) for s in args.scale.split(",")]
    done = skipped = 0
    t0 = time.time()
    for t in inv["textures"]:
        key = f"{t['file']}:{t['index']}"
        ci = classes["textures"][key]
        if only and ci["class"] not in only:
            continue
        if args.file and t["file"] != args.file:
            continue
        method = args.method or CLASS_METHOD[ci["class"]]
        stem = t["file"][:-4]
        entry = manifest["textures"].get(key, {})
        todo = []
        for s in scales:
            rel = f"tex/{stem}/{t['index']}.png" if s == args.primary else f"tex{s}x/{stem}/{t['index']}.png"
            path = os.path.join(OUT, rel.replace("/", os.sep))
            prev = entry.get(f"x{s}")
            if not args.force and prev and prev.get("method") == method and prev.get("sha256") == t.get("sha256") and os.path.exists(path):
                skipped += 1
                continue
            todo.append((s, rel, path))
        if not todo:
            continue
        if args.dry_run:
            print(f"{key:30s} {ci['class']:12s} {method:14s} {t['w']}x{t['h']} -> " + ", ".join(r for _, r, _ in todo))
            done += 1
            continue
        src = load_source(t)
        wrap = (ci["edge_u"], ci["edge_v"])
        binary = ci["alpha"] == "binary"
        for s, rel, path in todo:
            out = rf.upscale(src, s, method, wrap, binary)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            to_image(out).save(path, optimize=False)
            entry[f"x{s}"] = {"path": rel, "method": method, "sha256": t.get("sha256"), "w": out.shape[1], "h": out.shape[0]}
        entry.update({"class": ci["class"], "edge_u": ci["edge_u"], "edge_v": ci["edge_v"], "alpha": ci["alpha"],
                      "source": {"w": t["w"], "h": t["h"], "fmt": t["fmt"]}})
        manifest["textures"][key] = entry
        done += 1
        if done % 200 == 0:
            manifest["generated"] = time.strftime("%Y-%m-%d %H:%M:%S")
            save_json(manifest_path, manifest)
            print(f"  {done} done, {skipped} up to date ({time.time() - t0:.0f}s)")
        if args.limit and done >= args.limit:
            break
    if not args.dry_run:
        manifest["generated"] = time.strftime("%Y-%m-%d %H:%M:%S")
        manifest["methods"] = CLASS_METHOD
        manifest["layout"] = "tex/<file stem>/<index>.png = the primary scale (%dx); tex<N>x/ for the others" % args.primary
        save_json(manifest_path, manifest)
    print(f"batch: {done} textures processed, {skipped} outputs up to date ({time.time() - t0:.0f}s)")


# --- BMP front screens -----------------------------------------------------------------------------------------

def cmd_bmp(args):
    """The untabled BMP screens (title / logo / mode screens) -> bmp\\<file>.png at the given scale."""
    import remaster_filters as rf
    inv = load_json(os.path.join(OUT, "inventory.json"))
    done = 0
    for u in inv["untabled_tex_files"]:
        if u["kind"] != "bmp":
            continue
        path = os.path.join(GAME, "tex", u["file"])
        out = os.path.join(OUT, "bmp", u["file"] + ".png")
        if os.path.exists(out) and not args.force:
            continue
        img = Image.open(path).convert("RGBA")
        src = np.asarray(img).astype(np.float32) / 255.0
        res = rf.upscale(src, args.scale, args.method, ("clamp", "clamp"), False)
        if args.dry_run:
            print(f"{u['file']} {img.size} -> {out}")
            continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        to_image(res).save(out)
        done += 1
    print(f"bmp: {done} screens written")


# --- audio ------------------------------------------------------------------------------------------------------

def cmd_audio(args):
    import remaster_audio as ra
    inv = load_json(os.path.join(OUT, "inventory.json"))
    manifest_path = os.path.join(OUT, "sound_manifest.json")
    manifest = load_json(manifest_path) if os.path.exists(manifest_path) else {"files": {}}
    done = skipped = 0
    clipped_total = 0
    t0 = time.time()
    for s in inv["sounds"]:
        if "rate" not in s:
            continue
        rel = s["path"]  # Sound\...\x.wav as on disk
        out = os.path.join(OUT, rel.replace("/", os.sep))
        src = os.path.join(GAME, rel)
        prev = manifest["files"].get(rel)
        if not args.force and prev and prev.get("rate") == args.rate and os.path.exists(out):
            skipped += 1
            continue
        if args.dry_run:
            print(f"{rel}: {s['rate']} Hz {s['channels']}ch {s['bits']}b {s.get('seconds')}s -> {args.rate} Hz")
            done += 1
            continue
        info = ra.resample_wav(src, out, args.rate)
        clipped_total += info["clipped"]
        manifest["files"][rel] = {"source_rate": s["rate"], "rate": args.rate, "channels": s["channels"], "bits": 16,
                                  "frames": info["frames"], "clipped": info["clipped"], "source_sha256": info["source_sha256"]}
        done += 1
        if done % 50 == 0:
            save_json(manifest_path, manifest)
            print(f"  {done} done ({time.time() - t0:.0f}s)")
    if not args.dry_run:
        manifest["generated"] = time.strftime("%Y-%m-%d %H:%M:%S")
        manifest["method"] = ra.DESCRIPTION
        save_json(manifest_path, manifest)
    print(f"audio: {done} files, {skipped} up to date, {clipped_total} clipped samples ({time.time() - t0:.0f}s)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("inventory")
    sub.add_parser("classify")
    p = sub.add_parser("pilot")
    p.add_argument("--count", type=int, default=24)
    p.add_argument("--force", action="store_true")
    p = sub.add_parser("batch")
    p.add_argument("--scale", default="4,2", help="scales to write (default 4,2)")
    p.add_argument("--primary", type=int, default=4, help="scale written to tex\\ (the others to tex<N>x\\)")
    p.add_argument("--class", dest="classes", help="only these classes (comma separated)")
    p.add_argument("--file", help="only this texture file")
    p.add_argument("--method", help="override the per-class method")
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--force", action="store_true")
    p.add_argument("--limit", type=int, default=0)
    p = sub.add_parser("bmp")
    p.add_argument("--scale", type=int, default=2)
    p.add_argument("--method", default="lanczos_clamp")
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--force", action="store_true")
    p = sub.add_parser("audio")
    p.add_argument("--rate", type=int, default=48000)
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--force", action="store_true")
    args = ap.parse_args()
    os.chdir(ROOT)  # tex_export / pol_parse use game/ relative paths
    {"inventory": cmd_inventory, "classify": cmd_classify, "pilot": cmd_pilot, "batch": cmd_batch, "bmp": cmd_bmp,
     "audio": cmd_audio}[args.cmd](args)


if __name__ == "__main__":
    main()
