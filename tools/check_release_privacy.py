#!/usr/bin/env python3
"""Reject personal paths, credentials, and private runtime files in release inputs.

Usage: python tools/check_release_privacy.py path/to/hotd2.exe path/to/package.zip
Extra exact identifiers can be supplied with --forbid; never commit those values.
Checks ASCII and UTF-16 text, ZIP member names/comments, and uncompressed contents.
This is a release safeguard, not a guarantee that arbitrary personal data is absent.
"""
import argparse
import getpass
import hashlib
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
    r"(?:^|/)(?:\.env(?:\.[^/]*)?|\.git|\.ssh|hotd2re_(?:(?:save|rejoin)\.bin|scores_pending\.txt(?:\.(?:tmp|lock))?))(?:/|$)"
    r"|\.(?:pdb|dmp|log|pem|key)$",
    re.IGNORECASE,
)

# Byte-exact official redistributables only. This DLL was compared with the
# configured NVIDIA DLSS SDK release binary and its NVIDIA Corporation
# Authenticode signature was valid. It embeds a vendor model-training .pth path,
# not an application developer's path. A version update requires a new review;
# renaming or changing even one byte restores every ordinary privacy gate.
VENDOR_BUILD_PATH_BINARIES = {
    ("nvngx_dlss.dll", "d00b5b0ab29c3ca101b70c3a5ecb3a01b7ef33766a3383a28a18623083e6123e"):
        "NVIDIA DLSS 310.9.16.0 release DLL; vendor training-checkpoint path; valid NVIDIA Authenticode signature verified",
}


def vendor_build_path_exception(name, data):
    filename = Path(name.replace("\\", "/")).name
    if not any(key[0] == filename for key in VENDOR_BUILD_PATH_BINARIES):
        return None
    identity = (filename, hashlib.sha256(data).hexdigest())
    provenance = VENDOR_BUILD_PATH_BINARIES.get(identity)
    if provenance is None:
        return None
    return dict(file=name, sha256=identity[1], provenance=provenance,
                scope="Vendor home path and identifier collisions inside >1024-byte opaque Base64 runs only; credentials, plaintext/path identifiers and member names remain checked")


def forbidden_identifier(data, forbidden, *, vendor_encoded_payload=False):
    lowered = data.lower()
    # These ranges are considered only for a byte-exact reviewed vendor DLL.
    # A path component or standalone word is never an encoded-payload collision.
    runs = [(match.start(), match.end()) for match in re.finditer(rb"[A-Za-z0-9+/=]{1025,}", data)] if vendor_encoded_payload else []
    alphanumeric = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    for value in forbidden:
        for encoding in ("utf-8", "utf-16le"):
            needle = value.lower().encode(encoding)
            start = 0
            while True:
                at = lowered.find(needle, start)
                if at < 0:
                    break
                end = at + len(needle)
                encoded = (encoding == "utf-8" and needle and at > 0 and end < len(data) and
                           data[at - 1] in alphanumeric and data[end] in alphanumeric and
                           any(left < at and end < right for left, right in runs))
                if not encoded:
                    return True
                start = at + 1
    return False


def check_bytes(data, forbidden, *, vendor_home_path=False, vendor_encoded_payload=False):
    reasons = []
    if not vendor_home_path and (HOME_PATH.search(data) or UTF16_HOME.search(data)):
        reasons.append("personal home-directory path")
    if SECRETS.search(data):
        reasons.append("credential pattern")
    if forbidden_identifier(data, forbidden, vendor_encoded_payload=vendor_encoded_payload):
        reasons.append("forbidden identifier")
    return reasons


def check_path(path, forbidden):
    findings, count, vendor_exceptions = [], 0, []

    def check(name, data):
        nonlocal count
        count += 1
        exception = vendor_build_path_exception(name, data)
        if exception:
            vendor_exceptions.append(exception)
        reasons = check_bytes(data, forbidden, vendor_home_path=exception is not None,
                              vendor_encoded_payload=exception is not None) + check_bytes(name.encode("utf-8"), forbidden)
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
    return {"input": path.name, "files_checked": count, "passed": not findings, "findings": findings,
            "vendor_build_path_exceptions": vendor_exceptions}


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
