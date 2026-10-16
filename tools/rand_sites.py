"""List every rand() call site in Hod2.exe (calls to the thunk 0x004ABE60 or to rand 0x004ADED2).

Usage: python tools/rand_sites.py [--capture build/scene_capture_actors.txt] > notes/agents/effects_rand_sites.csv

Columns: return address (what the capture's N lines record), call address, containing function, how the function
was found (ghidra = inside a Ghidra function; code-ref = nearest preceding call target / code pointer), how the
result is used (%N = idiv by constant, &M = mask; rand() is never negative, so the compiler's signed-mask sequences
are plain masks), and the number of calls in the capture (N lines summed), if given.
"""
import argparse
import bisect
import collections
import csv
import re
import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs

THUNK, RAND = 0x004ABE60, 0x004ADED2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default="game/Hod2.exe")
    ap.add_argument("--functions", default="ghidra/export-hotd2/functions.csv")
    ap.add_argument("--capture")
    args = ap.parse_args()

    pe = pefile.PE(args.exe)
    text = next(s for s in pe.sections if s.Name.startswith(b".text"))
    base = pe.OPTIONAL_HEADER.ImageBase + text.VirtualAddress
    code = text.get_data()
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.skipdata = True

    ghidra = {int(r["address"], 16): int(r["size"]) for r in csv.DictReader(open(args.functions))}
    insns = list(md.disasm(code, base))
    targets = set(ghidra)
    for ins in insns:
        if ins.mnemonic == "call" and ins.op_str.startswith("0x"):
            targets.add(int(ins.op_str, 16))
        # code pointers (task functions, callbacks): imm into .text at a padded 16-byte boundary
        if ins.mnemonic in ("push", "mov"):
            m = re.search(r"(?:^|, )(0x[0-9a-f]{6})$", ins.op_str)
            if m:
                v = int(m.group(1), 16)
                if base <= v < base + len(code) and v % 16 == 0 and code[v - base - 1] in (0x90, 0xCC, 0xC3):
                    targets.add(v)
    starts = sorted(targets)

    demo = collections.Counter()
    if args.capture:
        with open(args.capture) as f:
            for line in f:
                if line.startswith("N "):
                    for tok in line.split()[1:]:
                        a, n = tok.split(":")
                        demo[int(a, 16)] += int(n)

    seen = set()
    rows = []
    for i, ins in enumerate(insns):
        if ins.address in seen or ins.mnemonic != "call" or ins.op_str not in (hex(THUNK), hex(RAND)):
            continue
        seen.add(ins.address)
        nxt = insns[i + 1: i + 7]
        use = ""
        if nxt[0].mnemonic == "cdq":
            regs = {}
            for x in nxt[1:]:
                m = re.match(r"(\w+), (0x[0-9a-f]+|\d+)$", x.op_str)
                if x.mnemonic == "mov" and m:
                    regs[m.group(1)] = int(m.group(2), 0)
                if x.mnemonic == "idiv":
                    use = f"%{regs[x.op_str]}" if x.op_str in regs else f"%[{x.op_str}]"
                    break
            if not use:
                use = "cdq;" + ";".join(f"{x.mnemonic} {x.op_str}" for x in nxt[1:3])
        elif nxt[0].mnemonic == "and" and nxt[0].op_str.startswith("eax, 0x"):
            use = f"&0x{int(nxt[0].op_str.split(',')[1], 0) & 0x7FFFFFFF:X}"
        elif nxt[0].mnemonic == "sar":
            use = ">>" + nxt[0].op_str.split(",")[1].strip()
        else:
            use = f"{nxt[0].mnemonic} {nxt[0].op_str}"
        fn = starts[bisect.bisect_right(starts, ins.address) - 1]
        src = "ghidra" if fn in ghidra and ins.address < fn + ghidra[fn] else "code-ref"
        rows.append((ins.address + 5, ins.address, fn, src, use, demo.get(ins.address + 5, 0)))

    w = csv.writer(sys.stdout, lineterminator="\n")
    w.writerow(["return_addr", "call_addr", "function", "function_from", "result_use", "capture_calls"])
    for r in rows:
        w.writerow([f"0x{r[0]:08X}", f"0x{r[1]:08X}", f"0x{r[2]:08X}", r[3], r[4], r[5]])
    print(f"{len(rows)} call sites in {len(set(r[2] for r in rows))} functions; capture: {sum(1 for r in rows if r[5])} sites, "
          f"{sum(r[5] for r in rows)} of {sum(demo.values())} calls", file=sys.stderr)


if __name__ == "__main__":
    main()
