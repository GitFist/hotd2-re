"""Quick recon of a Win32 PE: header, sections, imports, compiler hints, copy-protection markers.

Usage: python tools/pe_info.py game/<exe> [--json out.json]
"""
import argparse
import datetime
import json
import math
import sys

import pefile

# Byte/string signatures of common 2000-era protections.
PROTECTION_MARKERS = {
    "SafeDisc": [b"BoG_ *90.0&!!  Yy>", b"stxt774", b"stxt371", b"SafeDisc", b"~df394b.tmp"],
    "SecuROM": [b".securom", b"SecuROM", b".cms_t", b".cms_d"],
    "LaserLock": [b"LASERLOK", b"laserlok"],
    "StarForce": [b"protect.dll", b"StarForce"],
    "CD-Cops": [b"CD-Cops", b".grand"],
}

PROTECTION_SECTIONS = {"stxt774", "stxt371", ".securom", ".cms_t", ".cms_d", ".grand", ".laser"}


def entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = [0] * 256
    for b in data:
        counts[b] += 1
    n = len(data)
    return -sum(c / n * math.log2(c / n) for c in counts if c)


def rich_header(pe):
    rich = pe.parse_rich_header()
    if not rich:
        return []
    out = []
    vals = rich["values"]
    for i in range(0, len(vals), 2):
        comp_id = vals[i]
        out.append({"prod_id": comp_id >> 16, "build": comp_id & 0xFFFF, "count": vals[i + 1]})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("--json")
    args = ap.parse_args()

    pe = pefile.PE(args.exe)
    raw = open(args.exe, "rb").read()
    fh, oh = pe.FILE_HEADER, pe.OPTIONAL_HEADER

    info = {
        "file": args.exe,
        "size": len(raw),
        "machine": hex(fh.Machine),
        "timestamp": datetime.datetime.fromtimestamp(fh.TimeDateStamp, datetime.UTC).isoformat(),
        "image_base": hex(oh.ImageBase),
        "entry_point": hex(oh.ImageBase + oh.AddressOfEntryPoint),
        "linker": f"{oh.MajorLinkerVersion}.{oh.MinorLinkerVersion}",
        "subsystem": oh.Subsystem,
        "sections": [],
        "imports": {},
        "rich": rich_header(pe),
        "protection": [],
    }

    for s in pe.sections:
        name = s.Name.rstrip(b"\0").decode(errors="replace")
        data = s.get_data()
        info["sections"].append({
            "name": name,
            "va": hex(oh.ImageBase + s.VirtualAddress),
            "vsize": s.Misc_VirtualSize,
            "rsize": s.SizeOfRawData,
            "entropy": round(entropy(data), 2),
        })
        if name in PROTECTION_SECTIONS:
            info["protection"].append(f"section {name}")

    if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            dll = entry.dll.decode(errors="replace")
            info["imports"][dll] = [
                (imp.name.decode(errors="replace") if imp.name else f"ord_{imp.ordinal}")
                for imp in entry.imports
            ]

    for prot, sigs in PROTECTION_MARKERS.items():
        for sig in sigs:
            if sig in raw:
                info["protection"].append(f"{prot}: {sig!r}")

    # Print report
    print(f"File:        {info['file']} ({info['size']:,} bytes)")
    print(f"Machine:     {info['machine']}   Linker: {info['linker']}   Built: {info['timestamp']}")
    print(f"Image base:  {info['image_base']}   Entry: {info['entry_point']}")
    print("\nSections:")
    for s in info["sections"]:
        flag = "  <-- packed/encrypted?" if s["entropy"] > 7.2 else ""
        print(f"  {s['name']:<10} {s['va']:>10}  vsize={s['vsize']:>9,}  raw={s['rsize']:>9,}  H={s['entropy']}{flag}")
    print("\nImports:")
    for dll, funcs in sorted(info["imports"].items(), key=lambda kv: kv[0].lower()):
        print(f"  {dll} ({len(funcs)})")
    if info["rich"]:
        print("\nRich header (MSVC toolchain fingerprint):")
        for r in info["rich"]:
            print(f"  prod={r['prod_id']:>3} build={r['build']:>5} count={r['count']}")
    print("\nCopy protection:", ", ".join(info["protection"]) if info["protection"] else "none detected")

    if args.json:
        with open(args.json, "w") as f:
            json.dump(info, f, indent=2)


if __name__ == "__main__":
    sys.exit(main())
