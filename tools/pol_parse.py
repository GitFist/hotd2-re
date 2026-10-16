"""Parse HOTD2 pol\\*.bin model files (reference implementation + coverage statistics).

Evidence: Res_LoadCommon 0x00418200, Res_LoadModel 0x00418820, Mdl_Draw 0x004A7EF0 / 0x004A8440,
Tex_RegisterModelTextures 0x004AC980. See notes/formats.md.

Exe tables (indexed by pol file number N, global model id M):
  0x004D0EF4 + N*4  char* file name in pol\\
  0x004E794C + N*4  s16* list of global model ids in the file (file order)
  0x004E803C + N*2  u16 model count
  0x004E83B4 + M*2  u16 file number of model M

File: header of (count + 1) entries {u32 start (bit31 = flag), u32 modelOffset}; model k occupies
[start_k, start_{k+1}) and its root is at modelOffset_k. Offsets are file offsets (into the decoded
buffer for PRS files).

Model:  +0x00 u32, +0x04 u32 flags (0x10: alternate vertex path), +0x08 f32[4] bounding sphere,
        +0x18 chunk stream, terminated by a 0 word.
Chunks: material (bit31 set): 20 words, [0x13] = byte length of the strip chunks that follow.
        strip (bit31 clear): [ctrl][count] then vertices; ctrl 0x08 = triangle list (count*3 vertices),
        0x20 = vertex references, ctrl&3 = cull mode, 0x40 = shade mode.
Vertex: 8 floats x y z nx ny nz u v. Reference entry (ctrl 0x20): bit0 of word0 set -> inline 8-float
        vertex; clear -> {u32 flag, u32 byteOffset} pointing at a vertex relative to the entry's end.

Usage: python tools/pol_parse.py --stats
       python tools/pol_parse.py --obj build/obj <file.bin> [modelIndex]
"""
import argparse
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
from prs import decompress  # noqa: E402
from tex_export import Exe  # noqa: E402

POL_DIR = "game/pol"
NAME_TABLE, IDLIST_TABLE, COUNT_TABLE, MODEL_FILE_TABLE = 0x004D0EF4, 0x004E794C, 0x004E803C, 0x004E83B4


class FormatError(Exception):
    pass


def pol_files(exe):
    """Yields (N, name, model_ids). Stops after 8 consecutive invalid names."""
    misses, n = 0, 0
    while misses < 8:
        name = exe.cstr(exe.u32(NAME_TABLE + n * 4))
        if name and name.endswith(".bin"):
            misses = 0
            count = struct.unpack("<H", exe.bytes(COUNT_TABLE + n * 2, 2))[0]
            idp = exe.u32(IDLIST_TABLE + n * 4)
            ids = []
            if 0 < count <= 4096 and exe.lo <= idp < exe.hi:
                ids = list(struct.unpack(f"<{count}h", exe.bytes(idp, count * 2)))
            yield n, name, ids
        else:
            misses += 1
        n += 1


def load_pol(name):
    data = open(os.path.join(POL_DIR, name), "rb").read()
    want = struct.unpack_from("<I", data)[0]
    if 0 < want < 64 << 20:
        try:
            out = decompress(data[4:])
            if len(out) == want:
                return out, True
        except (ValueError, IndexError):
            pass
    return data, False


def header_count(data):
    """Models in a file: the header ends with an entry whose start equals the file size (true for all files)."""
    size, k, prev = len(data), 0, -1
    while (k + 1) * 8 <= size:
        s = struct.unpack_from("<I", data, k * 8)[0] & 0x7FFFFFFF
        if s == size:
            return k
        if s < prev or s > size:
            raise FormatError("header has no terminator")
        prev, k = s, k + 1
    raise FormatError("header has no terminator")


def read_header(data, count):
    entries = [struct.unpack_from("<II", data, i * 8) for i in range(count + 1)]
    return [(s & 0x7FFFFFFF, s >> 31, off) for s, off in entries]


class Mesh:
    def __init__(self, material):
        self.material = material  # 20 u32 words
        self.strips = []          # list of (ctrl, kind, [vertex tuples]) ; kind 'strip' or 'list'


def f32(u):
    return struct.unpack("<f", struct.pack("<I", u))[0]


def parse_model(data, root, end, stats=None):
    """Returns (header dict, [Mesh]). Raises FormatError on structural inconsistencies."""
    if root + 0x18 > end:
        raise FormatError("root past model block")
    w0, flags = struct.unpack_from("<II", data, root)
    sphere = struct.unpack_from("<4f", data, root + 8)
    pos = root + 0x18
    meshes = []
    mesh = None
    strip_end = None
    while True:
        if pos + 4 > end:
            raise FormatError(f"chunk stream runs past block at 0x{pos:x}")
        ctrl = struct.unpack_from("<I", data, pos)[0]
        if ctrl == 0:
            break
        if ctrl & 0x80000000:
            if mesh and strip_end is not None and pos != strip_end:
                raise FormatError(f"material at 0x{pos:x} but previous strips ended at 0x{strip_end:x}")
            words = struct.unpack_from("<20I", data, pos)
            mesh = Mesh(words)
            meshes.append(mesh)
            strip_end = pos + 0x50 + (words[0x13] & ~3)
            pos += 0x50
            if stats is not None:
                stats["materials"] += 1
            continue
        if mesh is None:
            raise FormatError("strip before any material")
        count = struct.unpack_from("<I", data, pos + 4)[0]
        if ctrl & 0x08:
            count *= 3
        pos += 8
        verts = []
        if flags & 0x10:
            # Alternate vertex path (Mdl_Draw skips with 2 or 14 words per entry); not decoded yet.
            for _ in range(count):
                w = struct.unpack_from("<I", data, pos)[0]
                pos += 4 * (14 if w & 1 else 2)
            if stats is not None:
                stats["alt_vertex_strips"] += 1
        elif ctrl & 0x20:
            for _ in range(count):
                w = struct.unpack_from("<I", data, pos)[0]
                if w & 1:
                    verts.append(struct.unpack_from("<8f", data, pos))
                    pos += 32
                else:
                    off = struct.unpack_from("<i", data, pos + 4)[0]  # signed: references point backwards
                    tgt = pos + 8 + off
                    if not (root <= tgt and tgt + 32 <= end):
                        raise FormatError(f"vertex reference out of block at 0x{pos:x}")
                    verts.append(struct.unpack_from("<8f", data, tgt))
                    pos += 8
            if stats is not None:
                stats["ref_strips"] += 1
        else:
            for _ in range(count):
                verts.append(struct.unpack_from("<8f", data, pos))
                pos += 32
        if pos > end:
            raise FormatError("strip runs past block")
        mesh.strips.append((ctrl, "list" if ctrl & 0x08 else "strip", verts))
        if stats is not None:
            stats["strips"] += 1
            stats["vertices"] += count
            stats["ctrl_bits"][ctrl & 0xFF] += 1
    if strip_end is not None and pos != strip_end:
        raise FormatError(f"last material length mismatch: stream at 0x{pos:x}, expected 0x{strip_end:x}")
    return {"w0": w0, "flags": flags, "sphere": sphere}, meshes


def iter_models(exe, only=None):
    """Yields (N, name, k, modelId, data, isPrs, start, end, root) for every model of every pol file on disk."""
    for n, name, ids in pol_files(exe):
        if only and name != only:
            continue
        path = os.path.join(POL_DIR, name)
        if not ids or not os.path.exists(path):
            continue
        data, is_prs = load_pol(name)
        hdr = read_header(data, header_count(data))
        for k in range(len(hdr) - 1):
            mid = ids[k] if k < len(ids) else None
            start, flag, root = hdr[k]
            end = hdr[k + 1][0]
            yield n, name, k, mid, data, is_prs, start, end, root, flag


def triangles(mesh):
    """Triangles (as vertex tuples) in drawing order; strips alternate winding."""
    for ctrl, kind, v in mesh.strips:
        if kind == "list":
            for i in range(0, len(v) - 2, 3):
                yield v[i], v[i + 1], v[i + 2]
        else:
            for i in range(len(v) - 2):
                yield (v[i], v[i + 1], v[i + 2]) if i % 2 == 0 else (v[i + 1], v[i], v[i + 2])


def model_digest(hdr, meshes):
    """SHA-256 over flags, material words and decoded vertices (in drawing order); mirrored by tests/model_test."""
    import hashlib
    h = hashlib.sha256()
    h.update(struct.pack("<II4f", hdr["w0"], hdr["flags"], *hdr["sphere"]))
    for m in meshes:
        h.update(struct.pack("<20I", *m.material))
        for ctrl, kind, verts in m.strips:
            h.update(struct.pack("<II", ctrl, len(verts)))
            for v in verts:
                h.update(struct.pack("<8f", *v))
    return h.hexdigest()


def all_pol_models(exe):
    """Every model in every pol file with a model header (tabled or not): (name, k, data, start, end, root)."""
    for name in sorted(os.listdir(POL_DIR)):
        if not name.endswith(".bin"):
            continue
        data, _ = load_pol(name)
        try:
            hdr = read_header(data, header_count(data))
        except FormatError:
            continue
        for k in range(len(hdr) - 1):
            yield name, k, data, hdr[k][0], hdr[k + 1][0], hdr[k][2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--digests")
    ap.add_argument("--obj")
    ap.add_argument("file", nargs="?")
    ap.add_argument("model", nargs="?", type=int)
    args = ap.parse_args()
    exe = Exe()

    if args.digests:
        lines = []
        for name, k, data, start, end, root in all_pol_models(exe):
            hdr, meshes = parse_model(data, root, end)
            strips = sum(len(m.strips) for m in meshes)
            verts = sum(len(s[2]) for m in meshes for s in m.strips)
            lines.append(f"{name} {k} {len(meshes)} {strips} {verts} {model_digest(hdr, meshes)}")
        with open(args.digests, "w") as f:
            f.write("\n".join(lines) + "\n")
        print(f"{len(lines)} model digests")
        return

    if args.stats:
        st = collections.Counter()
        st["ctrl_bits"] = collections.Counter()
        tex_refs = collections.Counter()
        model_flags = collections.Counter()
        errors = collections.Counter()
        files_on_disk = set(os.listdir(POL_DIR))
        listed = set()
        prs_files = raw_files = 0
        seen_files = set()
        for n, name, k, mid, data, is_prs, start, end, root, flag in iter_models(exe, args.file):
            listed.add(name)
            if name not in seen_files:
                seen_files.add(name)
                prs_files += is_prs
                raw_files += not is_prs
            st["models"] += 1
            st["padding_skipped"] += 0
            try:
                hdr, meshes = parse_model(data, root, end, st)
                model_flags[hdr["flags"]] += 1
                for m in meshes:
                    t = m.material[8]
                    tex_refs["none" if t == 0xFFFFFFFF else f"flags {t >> 30:02b}"] += 1
                st["models_ok"] += 1
            except (FormatError, struct.error) as e:
                errors[str(e).split(" at ")[0]] += 1
        ctrl = st.pop("ctrl_bits")
        print(f"pol files with models on disk: {len(seen_files)} (PRS {prs_files}, raw {raw_files}); "
              f"files on disk not in table: {len(files_on_disk - listed)}")
        for k in ("models", "models_ok", "materials", "strips", "ref_strips", "alt_vertex_strips", "vertices"):
            print(f"  {k}: {st[k]}")
        print("  model flags:", dict(model_flags.most_common(12)))
        print("  material texture field:", dict(tex_refs))
        print("  strip ctrl low byte:", {hex(k): v for k, v in ctrl.most_common(16)})
        print("  errors:", dict(errors) or "none")
        return

    if args.obj:
        os.makedirs(args.obj, exist_ok=True)
        for n, name, k, mid, data, is_prs, start, end, root, flag in iter_models(exe, args.file):
            if args.model is not None and k != args.model:
                continue
            hdr, meshes = parse_model(data, root, end)
            base = os.path.join(args.obj, f"{os.path.splitext(name)[0]}_{k:03d}")
            with open(base + ".obj", "w") as f:
                vi = 1
                for mi, m in enumerate(meshes):
                    f.write(f"g mesh{mi}_tex{m.material[8]:08x}\n")
                    for tri in triangles(m):
                        for v in tri:
                            f.write(f"v {v[0]} {v[1]} {v[2]}\nvn {v[3]} {v[4]} {v[5]}\nvt {v[6]} {v[7]}\n")
                        f.write(f"f {vi}/{vi}/{vi} {vi + 1}/{vi + 1}/{vi + 1} {vi + 2}/{vi + 2}/{vi + 2}\n")
                        vi += 3
            print(base + ".obj", len(meshes), "meshes")


if __name__ == "__main__":
    main()
