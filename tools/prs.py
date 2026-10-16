"""Sega PRS decompressor, written from the game's own routine at 0x0040ACD0 (Lz_DecompressPrs).

Bitstream: control bits are read LSB-first from bytes interleaved with the data.
  1          -> literal byte
  0 1        -> long copy: u16 w; w == 0 ends stream; offset = (w >> 3) | ~0x1FFF;
                size = (w & 7) + 2, or if (w & 7) == 0: size = next byte + 1
  0 0 b1 b0  -> short copy: size = (b1 << 1 | b0) + 2; offset = next byte | ~0xFF

Usage: python tools/prs.py <in.bin> [out.bin]
       python tools/prs.py --dump-refs <game dir> <out dir>
"""
import sys


def decompress(src: bytes) -> bytes:
    out = bytearray()
    pos = 0
    bits = 0
    nbits = 0

    def bit() -> int:
        nonlocal bits, nbits, pos
        if nbits == 0:
            bits = src[pos]
            pos += 1
            nbits = 8
        b = bits & 1
        bits >>= 1
        nbits -= 1
        return b

    while True:
        if bit():
            out.append(src[pos])
            pos += 1
            continue
        if bit():
            w = src[pos] | (src[pos + 1] << 8)
            pos += 2
            if w == 0:
                break
            offset = (w >> 3) - 0x2000
            size = w & 7
            if size == 0:
                size = src[pos] + 1
                pos += 1
            else:
                size += 2
        else:
            size = ((bit() << 1) | bit()) + 2
            offset = src[pos] - 0x100
            pos += 1
        start = len(out) + offset
        if start < 0:
            raise ValueError(f"back-reference before start of output at input offset {pos}")
        for i in range(size):
            out.append(out[start + i])
    return bytes(out)


def dump_refs(game_dir: str, out_dir: str):
    """Writes decoded pol/tex/etc/coli files (u32 size header + PRS) as references for tests/prs_test."""
    import os
    import struct
    count = 0
    for sub in ("pol", "tex", "etc", "coli"):
        src_dir = os.path.join(game_dir, sub)
        if not os.path.isdir(src_dir):
            continue
        os.makedirs(os.path.join(out_dir, sub), exist_ok=True)
        for name in os.listdir(src_dir):
            if not name.lower().endswith(".bin"):
                continue
            data = open(os.path.join(src_dir, name), "rb").read()
            if len(data) < 8:
                continue
            want = struct.unpack_from("<I", data)[0]
            try:
                out = decompress(data[4:])
            except (ValueError, IndexError):
                continue
            if len(out) == want:
                open(os.path.join(out_dir, sub, name), "wb").write(out)
                count += 1
    print(f"wrote {count} reference files to {out_dir}")


def main():
    if sys.argv[1] == "--dump-refs":
        dump_refs(sys.argv[2], sys.argv[3])
        return
    data = open(sys.argv[1], "rb").read()
    out = decompress(data)
    print(f"{sys.argv[1]}: {len(data):,} -> {len(out):,} bytes")
    if len(sys.argv) > 2:
        open(sys.argv[2], "wb").write(out)


if __name__ == "__main__":
    main()
