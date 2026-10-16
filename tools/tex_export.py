"""Export HOTD2 textures to PNG, using the descriptor tables in Hod2.exe.

Reference implementation of the game's texture upload (0x004AC270, Tex_CreateSurface).

Tables (indexed by texture file number N, see notes/formats.md):
  0x004D1410 + N*4   char* file name in tex\
  0x0055B9B8 + N*4   -> descriptor array, 16 bytes per texture:
                        s16 width, s16 height, u32 format, u32 dataOffset, u32 textureId
  0x0055C0A8 + N*2   s16 texture count
format: bits 0-2 PVR pixel format (0 ARGB1555, 1 RGB565, 2 ARGB4444, 3 YUV422 [unsupported by the game]),
        bits 8-15 layout (0x01 twiddled, 0x03 VQ, 0x05 4bpp palette twiddled, 0x09 rectangle,
        0x0D rectangle twiddled).

Usage:
  python tools/tex_export.py --list                      # summary of all files/formats
  python tools/tex_export.py --out build/tex_png [name]  # export (all files, or one e.g. common.bin)
  python tools/tex_export.py --hashes build/tex_hashes.txt   # SHA-256 of decoded RGBA, for tests
"""
import argparse
import hashlib
import os
import struct
import sys

import pefile

sys.path.insert(0, os.path.dirname(__file__))
from prs import decompress  # noqa: E402

EXE = "game/Hod2.exe"
TEX_DIR = "game/tex"
NAME_TABLE, DESC_TABLE, COUNT_TABLE = 0x004D1410, 0x0055B9B8, 0x0055C0A8
PALETTE_TABLE = 0x0057A010   # static 16-colour ARGB1555 banks (copied to 0x00810200 by Tex_LoadPalettes 0x004ACCA0)
GET_BANK = 0x0041C9E0        # Tex_GetPaletteBank(fileIndex, texIndex), executed via tools/emu.py
COMMON_FILE = 1               # loaded by Res_LoadCommon -> no bank set -> bank 0
LAYOUT_NAMES = {0x1: "twiddled", 0x3: "vq", 0x5: "pal4", 0x9: "rect", 0xD: "rect_twiddled"}
PIXEL_NAMES = {0: "argb1555", 1: "rgb565", 2: "argb4444", 3: "yuv422"}


class Exe:
    def __init__(self, path=EXE):
        self.pe = pefile.PE(path)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.lo, self.hi = 0x004C4000, 0x009CC000  # .rdata .. end of .data

    def u32(self, va):
        return struct.unpack("<I", self.pe.get_data(va - self.base, 4))[0]

    def s16(self, va):
        return struct.unpack("<h", self.pe.get_data(va - self.base, 2))[0]

    def bytes(self, va, n):
        # Uninitialized .data (beyond the raw section data) reads as zero, as when loaded.
        return self.pe.get_data(va - self.base, n).ljust(n, b"\0")

    def cstr(self, va):
        if not (self.base <= va < self.hi):
            return None
        try:
            return self.bytes(va, 64).split(b"\0")[0].decode("ascii")
        except UnicodeDecodeError:
            return None


def texture_files(exe):
    """Yields (index, name, descriptor_table_va, count) for valid table entries."""
    misses, n = 0, 0
    while misses < 8:
        name = exe.cstr(exe.u32(NAME_TABLE + n * 4))
        if name and name.endswith(".bin"):
            misses = 0
            desc, count = exe.u32(DESC_TABLE + n * 4), exe.s16(COUNT_TABLE + n * 2)
            if exe.lo <= desc < exe.hi and 0 < count <= 4096:
                yield n, name, desc, count
        else:
            misses += 1
        n += 1


def descriptors(exe, desc_va, count):
    for i in range(count):
        w, h, fmt, off, tid = struct.unpack("<hhIII", exe.bytes(desc_va + i * 16, 16))
        yield {"index": i, "w": w, "h": h, "fmt": fmt, "offset": off, "id": tid}


def load_file(name):
    data = open(os.path.join(TEX_DIR, name), "rb").read()
    want = struct.unpack_from("<I", data)[0]
    try:
        out = decompress(data[4:])
        if len(out) == want:
            return out
    except (ValueError, IndexError):
        pass
    return data  # stored uncompressed


# --- texel decoding -------------------------------------------------------------------

def to_rgba(v, pixfmt):
    if pixfmt == 0:  # ARGB1555
        a = 255 if v & 0x8000 else 0
        r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
        return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2, a)
    if pixfmt == 1:  # RGB565
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2, 255)
    if pixfmt == 2:  # ARGB4444
        return (((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 15) * 17)
    raise ValueError(f"unsupported pixel format {pixfmt}")


def twiddle(x, y):
    """Morton index with y in the even bits, x in the odd bits (matches 0x55556/0x2AAAB steps)."""
    r, bit = 0, 0
    while (x >> bit) or (y >> bit):
        r |= ((y >> bit) & 1) << (2 * bit)
        r |= ((x >> bit) & 1) << (2 * bit + 1)
        bit += 1
    return r


def decode_texture(data, d, palette=None):
    """Returns (w, h, list of u16 texels in row-major order) or raises ValueError."""
    w, h, fmt, off = d["w"], d["h"], d["fmt"], d["offset"]
    layout = (fmt >> 8) & 0xFF
    if w <= 0 or h <= 0:
        raise ValueError("empty")

    def u16(i):
        return data[off + 2 * i] | (data[off + 2 * i + 1] << 8)

    out = [0] * (w * h)
    if layout == 0x1:  # twiddled square
        for y in range(h):
            for x in range(w):
                out[y * w + x] = u16(twiddle(x, y))
    elif layout == 0x9:  # rectangle, linear
        for i in range(w * h):
            out[i] = u16(i)
    elif layout == 0xD:  # rectangle split into square twiddled blocks
        s = min(w, h)
        for blk in range(max(w, h) // s):
            base = blk * s * s
            for y in range(s):
                for x in range(s):
                    v = u16(base + twiddle(x, y))
                    if w < h:
                        out[(blk * s + y) * w + x] = v
                    else:
                        out[y * w + blk * s + x] = v
    elif layout == 0x3:  # VQ: 256 x (4 texels) codebook, then twiddled indices per 2x2 block
        book = off
        idx = off + 0x800
        for by in range(h // 2):
            for bx in range(w // 2):
                e = data[idx + twiddle(bx, by)]
                t = [data[book + e * 8 + k * 2] | (data[book + e * 8 + k * 2 + 1] << 8) for k in range(4)]
                x, y = bx * 2, by * 2
                out[y * w + x] = t[0]
                out[(y + 1) * w + x] = t[1]
                out[y * w + x + 1] = t[2]
                out[(y + 1) * w + x + 1] = t[3]
    elif layout == 0x5:  # 4bpp palette, twiddled; palette is global (0x00810200), not in the file
        if palette is None:
            raise ValueError("pal4 needs the runtime palette")
        s = min(w, h)  # non-square: square twiddled blocks along the long axis (0x004AC270 loop)
        for y in range(h):
            for x in range(w):
                blk = (x // s) if w >= h else (y // s)
                t = blk * s * s + twiddle(x % s, y % s)
                b = data[off + t // 2]
                out[y * w + x] = palette[(b >> (4 * (t & 1))) & 15]
    else:
        raise ValueError(f"unknown layout 0x{layout:X}")
    return w, h, out


def load_palette_banks(exe):
    """Static banks: entries of {s16 ?, s16 count=0x10, u32 colours*, s32 n} (12 bytes) at PALETTE_TABLE+0."""
    banks = []
    while True:
        cnt, ptr, n = struct.unpack("<hIi", exe.bytes(PALETTE_TABLE + 2 + len(banks) * 12, 10))
        if cnt != 0x10 or n < 1 or not (exe.lo <= ptr < exe.hi):
            break
        banks.append(list(struct.unpack("<16H", exe.bytes(ptr, 32))))
    return banks


def palette_for(banks, bank, pixfmt):
    """Bank entries (ARGB1555) converted to the texture's surface format, as in 0x004AC270."""
    if bank < 0 or bank > 0x3F:
        bank = 0  # Tex_RegisterFileTexture stores -1; Tex_CreateSurface then uses bank 0
    src = banks[bank] if bank < len(banks) else [0] * 16  # banks past the static table stay zero
    out = []
    for v in src:
        if pixfmt == 1:    # -> RGB565
            out.append(((v & 0xFFE0) << 1 | (v & 0x1F)) & 0xFFFF)
        elif pixfmt == 2:  # -> ARGB4444
            a = 0xF if v & 0x8000 else 0
            out.append(a << 12 | ((v >> 11) & 15) << 8 | ((v >> 6) & 15) << 4 | ((v >> 1) & 15))
        else:              # ARGB1555 as-is
            out.append(v)
    return out


def rgba_bytes(texels, pixfmt):
    buf = bytearray()
    for v in texels:
        buf.extend(to_rgba(v, pixfmt))
    return bytes(buf)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--out")
    ap.add_argument("--hashes")
    ap.add_argument("name", nargs="?")
    args = ap.parse_args()

    exe = Exe()
    banks = load_palette_banks(exe)
    emu = None
    stats = {}
    ok = fail = 0
    hash_lines = []
    bank_lines = []
    for n, name, desc, count in texture_files(exe):
        if args.name and name != args.name:
            continue
        if not os.path.exists(os.path.join(TEX_DIR, name)):
            continue
        data = None
        for d in descriptors(exe, desc, count):
            key = (LAYOUT_NAMES.get((d["fmt"] >> 8) & 0xFF, hex((d["fmt"] >> 8) & 0xFF)),
                   PIXEL_NAMES.get(d["fmt"] & 7, str(d["fmt"] & 7)))
            stats[key] = stats.get(key, 0) + 1
            if args.list:
                continue
            if data is None:
                data = load_file(name)
            palette = None
            if (d["fmt"] >> 8) & 0xFF == 0x5:
                if n == COMMON_FILE:
                    bank = -1
                else:
                    if emu is None:
                        from emu import ExeEmu
                        emu = ExeEmu()
                    bank = emu.call_cdecl(GET_BANK, n, d["index"])
                    bank = bank - (1 << 32) if bank & 0x80000000 else bank
                palette = palette_for(banks, bank, d["fmt"] & 7)
                bank_lines.append(f"{name} {d['index']} {bank}")
            try:
                w, h, texels = decode_texture(data, d, palette)
                rgba = rgba_bytes(texels, d["fmt"] & 7)
            except (ValueError, IndexError) as e:
                fail += 1
                hash_lines.append(f"{name} {d['index']} FAIL {e}")
                continue
            ok += 1
            hash_lines.append(f"{name} {d['index']} {w}x{h} {d['fmt']:08X} {hashlib.sha256(rgba).hexdigest()}")
            if args.out:
                from PIL import Image
                os.makedirs(args.out, exist_ok=True)
                # Assets are stored bottom-up (the game's surfaces hold them this way and UVs flip them);
                # flip for viewing only. Hashes above are of the unflipped game layout.
                Image.frombytes("RGBA", (w, h), rgba).transpose(Image.FLIP_TOP_BOTTOM).save(
                    os.path.join(args.out, f"{os.path.splitext(name)[0]}_{d['index']:03d}.png"))
    for k, v in sorted(stats.items(), key=lambda kv: -kv[1]):
        print(f"{k[0]:>14} {k[1]:>9}: {v}")
    if not args.list:
        print(f"decoded {ok}, failed {fail}")
    if args.hashes:
        with open(args.hashes, "w") as f:
            f.write("\n".join(hash_lines) + "\n")
        # Palette bank per pal4 texture (from the emulated game function), for tests/tex_test.
        with open(os.path.splitext(args.hashes)[0] + "_banks.txt", "w") as f:
            f.write("\n".join(bank_lines) + "\n")
    print(f"palette banks in exe: {len(banks)}")


if __name__ == "__main__":
    main()
