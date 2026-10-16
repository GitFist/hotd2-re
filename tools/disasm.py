"""Disassemble a range of the game exe by virtual address.

Usage: python tools/disasm.py <va> [count=40] [--exe game/Hod2.exe]
       python tools/disasm.py --calls-to <va>     (list call sites + the following instruction)
"""
import argparse

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs


def load(exe):
    pe = pefile.PE(exe)
    text = next(s for s in pe.sections if s.Name.startswith(b".text"))
    base = pe.OPTIONAL_HEADER.ImageBase + text.VirtualAddress
    return base, text.get_data()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("va", nargs="?")
    ap.add_argument("count", nargs="?", type=int, default=40)
    ap.add_argument("--exe", default="game/Hod2.exe")
    ap.add_argument("--calls-to")
    args = ap.parse_args()

    base, code = load(args.exe)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.skipdata = True

    if args.calls_to:
        target = int(args.calls_to, 16)
        prev = None
        for insn in md.disasm(code, base):
            if prev is not None:
                print(f"0x{prev.address:08X}: {prev.mnemonic} {prev.op_str:<14} -> next: {insn.mnemonic} {insn.op_str}")
                prev = None
            if insn.mnemonic == "call" and insn.op_str == hex(target):
                prev = insn
        return

    va = int(args.va, 16)
    for i, insn in enumerate(md.disasm(code[va - base:], va)):
        if i >= args.count:
            break
        print(f"0x{insn.address:08X}: {insn.bytes.hex():<16} {insn.mnemonic} {insn.op_str}")


if __name__ == "__main__":
    main()
