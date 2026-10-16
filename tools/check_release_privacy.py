#!/usr/bin/env python3
"""Reject personal paths, credentials, and private runtime files in release inputs.

Usage: python tools/check_release_privacy.py path/to/hotd2.exe path/to/package.zip
Extra exact identifiers can be supplied with --forbid; never commit those values.
Checks ASCII and UTF-16 text, ZIP member names/comments, and uncompressed contents.
This is a release safeguard, not a guarantee that arbitrary personal data is absent.
"""
import argparse
import getpass
import json
import re
import zipfile
from pathlib import Path


HOME_PATH = re.compile(
    rb"(?:[A-Za-z]:[\\/]+(?:Users|Documents and Settings)[\\/]+|/(?:Users|home)/)[^\s\x00\"<>]+",
    re.IGNORECASE,
)
UTF16_HOME = re.compile(
    rb"(?:[A-Za-z]\x00:\x00[\\/]\x00(?:U\x00s\x00e\x00r\x00s\x00|D\x00o\x00c\x00u\x00m\x00e\x00n\x00t\x00s\x00 \x00a\x00n\x00d\x00 \x00S\x00e\x00t\x00t\x00i\x00n\x00g\x00s\x00)[\\/]\x00|/\x00(?:U\x00s\x00e\x00r\x00s\x00|h\x00o\x00m\x00e\x00)/\x00)",
    re.IGNORECASE,
)
SECRETS = re.compile(
    rb"-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----"
    rb"|\b(?:gh[pousr]_[A-Za-z0-9]{30,255}|github_pat_[A-Za-z0-9_]{30,255})\b"
    rb"|\b(?:AKIA|ASIA)[A-Z0-9]{16}\b"
)
PRIVATE_NAME = re.compile(
    r"(?:^|/)(?:\.env(?:\.[^/]*)?|\.git|\.ssh|hotd2re_(?:save|rejoin)\.bin)(?:/|$)"
    r"|\.(?:pdb|dmp|log|pem|key)$",
    re.IGNORECASE,
)


def check_bytes(data, forbidden):
    reasons = []
    if HOME_PATH.search(data) or UTF16_HOME.search(data):
        reasons.append("personal home-directory path")
    if SECRETS.search(data):
        reasons.append("credential pattern")
    lowered = data.lower()
    for value in forbidden:
        if any(value.lower().encode(encoding) in lowered for encoding in ("utf-8", "utf-16le")):
            reasons.append("forbidden identifier")
            break
    return reasons


def check_path(path, forbidden):
    findings, count = [], 0

    def check(name, data):
        nonlocal count
        count += 1
        reasons = check_bytes(data, forbidden) + check_bytes(name.encode("utf-8"), forbidden)
        if PRIVATE_NAME.search(name.replace("\\", "/")):
            reasons.append("private or debug file")
        if reasons:
            # Report the member and reason, never copy leaked values into build logs.
            findings.append({"file": name, "reasons": sorted(set(reasons))})

    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            check("<archive comment>", archive.comment)
            for member in archive.infolist():
                if member.is_dir():
                    continue
                check(member.filename, archive.read(member) + member.comment)
    elif path.is_dir():
        for member in sorted(path.rglob("*")):
            if member.is_file():
                check(member.relative_to(path).as_posix(), member.read_bytes())
    else:
        check(path.name, path.read_bytes())
    return {"input": path.name, "files_checked": count, "passed": not findings, "findings": findings}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", type=Path, nargs="+")
    parser.add_argument("--forbid", action="append", default=[])
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    forbidden = list(args.forbid)
    username = getpass.getuser()
    # Short generic account names are too likely to occur in ordinary game data.
    if len(username) >= 5 and username.casefold() not in {"administrator", "runner", "build", "public", "default"}:
        forbidden.append(username)
    results = [check_path(path, forbidden) for path in args.paths]
    report = {"passed": all(r["passed"] for r in results), "inputs": results}
    output = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.write_text(output, encoding="utf-8")
    print(output, end="")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
