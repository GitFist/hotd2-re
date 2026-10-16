"""Summarize functions from the Ghidra xref export: referenced strings, imported APIs, callers, callees.

Usage: python tools/func_info.py 0x00413070 0x0041EC10 ...
       python tools/func_info.py --string ".bin"     (functions referencing strings containing text)
"""
import csv
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
X = ROOT / "ghidra" / "xrefs"


def norm(a: str) -> str:
    return f"0x{int(a, 16):08X}"


def load():
    strings = defaultdict(set)
    by_text = []
    with open(X / "strings.csv", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["function"]:
                strings[r["function"]].add(r["value"])
                by_text.append((r["value"], r["function"]))
    apis = defaultdict(set)
    with open(X / "imports.csv", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            apis[r["function"]].add(r["api"])
    callers, callees = defaultdict(set), defaultdict(set)
    with open(X / "callgraph.csv", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            callees[r["caller"]].add(r["callee"])
            callers[r["callee"]].add(r["caller"])
    return strings, by_text, apis, callers, callees


def main():
    strings, by_text, apis, callers, callees = load()
    args = sys.argv[1:]
    if args and args[0] == "--string":
        needle = args[1].lower()
        hits = defaultdict(set)
        for value, fn in by_text:
            if needle in value.lower():
                hits[fn].add(value)
        for fn in sorted(hits):
            print(fn, "|", " ; ".join(sorted(hits[fn]))[:200])
        return
    for a in args:
        a = norm(a)
        print(f"== {a}")
        if apis[a]:
            print("   api:    ", ", ".join(sorted(apis[a])))
        if strings[a]:
            print("   strings:", " ; ".join(sorted(strings[a]))[:400])
        print("   callers:", ", ".join(sorted(callers[a])[:12]), "..." if len(callers[a]) > 12 else "")
        ext = sorted(c for c in callees[a] if c.startswith("EXT:"))
        internal = sorted(c for c in callees[a] if not c.startswith("EXT:"))
        print("   callees:", ", ".join(internal[:16]), "..." if len(internal) > 16 else "")


if __name__ == "__main__":
    main()
