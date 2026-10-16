"""Dump Tex_GetPaletteBank (0x0041C9E0) for every texture file index and texture index, by running the
game's function in the emulator. Output lines: "<fileIndex> <texIndex> <bank>" for tests/bank_test.

Covers all table entries (0..max named index) and indexes 0..max(count, 400) per file, plus a few
negative indexes, so table-driven and range cases are exercised beyond the textures that exist.

Usage: python tools/dump_banks.py build/banks_emu.txt
"""
import sys

from emu import ExeEmu
from tex_export import Exe, texture_files


def main():
    exe = Exe()
    emu = ExeEmu()
    counts = {n: c for n, _, _, c in texture_files(exe)}
    max_file = max(counts) + 4
    lines = []
    for n in range(0, max_file + 1):
        limit = max(counts.get(n, 0), 400)
        for i in list(range(-3, 0)) + list(range(limit)):
            v = emu.call_cdecl(0x0041C9E0, n, i)
            lines.append(f"{n} {i} {v - (1 << 32) if v & 0x80000000 else v}")
    with open(sys.argv[1], "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{len(lines)} (file, index) pairs")


if __name__ == "__main__":
    main()
