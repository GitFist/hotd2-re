#!/usr/bin/env python3
"""Shared Windows package validation/staging. Public packages never copy game data.

Existing outputs are always refused. Complete packages are explicitly local and
require a game directory plus its verified extraction metadata.
"""
import argparse
import base64
import gzip
import hashlib
import json
from pathlib import Path, PureWindowsPath
import re
import shutil
import struct
import subprocess
import sys
import zipfile

import check_release_privacy

REPO = Path(__file__).resolve().parent.parent
MACHINES = {0x14C: ("x86", 0x10B, "PE32"), 0x8664: ("x64", 0x20B, "PE32+")}
RUNTIME_GROUPS = (
    (("nvngx_dlss.dll", "nvngx_dlssg.dll"), "vendor-licenses/nvidia", ("LICENSE.txt",), True),
    (("amd_fidelityfx_upscaler_dx12.dll", "amd_fidelityfx_framegeneration_dx12.dll"), "vendor-licenses/amd", ("license.md", "3rdpartynotice.md"), True),
    (("libxess.dll",), "vendor-licenses/intel", ("LICENSE.txt", "third-party-programs.txt"), True),
    (("dxcompiler.dll", "dxil.dll"), "dxc-licenses", ("LICENSE-MS.txt", "LICENSE-LLVM.txt", "LICENCE-MIT.txt"), False),
)
EXCLUDED_GAME = {name.casefold() for name in (
    "Config.exe", "pol/bg_adv19.bin", "tex/scr_tod_itm_itamidome2.bin", "pol/komono_0.bin", "pol/tv2.bin")}
REQUIRED_GAME_FILES = {name.casefold() for name in (
    "Hod2.exe", "evt/comevtbl.bin", "evt/st1evtbl.bin", "pol/common.bin", "tex/common.bin", "tex/scr_common.bin")}
MAX_PUBLIC_FILES = 512
MAX_PUBLIC_FILE_BYTES = 512 * 1024 * 1024
MAX_PUBLIC_TOTAL_BYTES = 1024 * 1024 * 1024
MAX_FILE_INDEX_BYTES = 2 * 1024 * 1024
MAX_FILE_ASSETS_BYTES = 512 * 1024 * 1024
PUBLIC_MANAGED_PATH = re.compile(
    r"hotd2\.exe|scenes/st1_play\.scene|README\.txt|BUILD\.txt|CREDITS\.txt|LICENSE-SDL\.txt|"
    r"LICENSE-native-code\.txt|THIRD_PARTY_NOTICES\.txt|nvngx_dlss\.dll|nvngx_dlssg\.dll|"
    r"amd_fidelityfx_upscaler_dx12\.dll|amd_fidelityfx_framegeneration_dx12\.dll|libxess\.dll|"
    r"dxcompiler\.dll|dxil\.dll|(?:vendor-licenses/(?:nvidia|amd|intel)|dxc-licenses)/[A-Za-z0-9_./ -]+\.(?:txt|md)", re.I)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def linked(path):
    # Also rejects Windows junction/reparse points on Python versions before
    # Path.is_junction was introduced.
    return path.is_symlink() or bool(getattr(path.lstat(), "st_file_attributes", 0) & 0x400)


def require_file(path):
    path = Path(path)
    if not path.is_file() or linked(path):
        raise ValueError(f"Required regular file missing or linked: {path.name}")
    return path


def pe_info(path, *, dll=False):
    """Read bounded PE metadata without executing a binary or trusting its name."""
    path = require_file(path)
    size = path.stat().st_size
    with path.open("rb") as stream:
        dos = stream.read(64)
        if len(dos) != 64 or dos[:2] != b"MZ":
            raise ValueError(f"Not a Windows PE file: {path.name}")
        offset = struct.unpack_from("<I", dos, 0x3C)[0]
        if offset < 64 or offset > size - 26:
            raise ValueError(f"Invalid PE header offset: {path.name}")
        stream.seek(offset)
        header = stream.read(24)
        if header[:4] != b"PE\0\0":
            raise ValueError(f"Invalid PE signature: {path.name}")
        machine, sections = struct.unpack_from("<HH", header, 4)
        optional_size, flags = struct.unpack_from("<HH", header, 20)
        if machine not in MACHINES or not 1 <= sections <= 96 or not flags & 2 or bool(flags & 0x2000) != dll:
            raise ValueError(f"Unsupported PE machine or executable kind: {path.name}")
        architecture, magic, name = MACHINES[machine]
        minimum = 112 if architecture == "x64" else 96
        if optional_size < minimum or offset + 24 + optional_size + sections * 40 > size:
            raise ValueError(f"Truncated PE optional/section header: {path.name}")
        optional = stream.read(optional_size)
        if struct.unpack_from("<H", optional)[0] != magic:
            raise ValueError(f"PE machine and optional header disagree: {path.name}")
    return dict(architecture=architecture, machine=f"0x{machine:04x}", peFormat=name, bytes=size)


def executable_info(exe, source_dir, architecture):
    info = pe_info(exe)
    if info["architecture"] != architecture:
        raise ValueError(f"Requested {architecture}, executable is {info['architecture']}")
    source = require_file(source_dir / "src/port/tools/play.cpp").read_text(encoding="utf-8")
    labels = set(re.findall(r'\bkBuildName\s*=\s*"([^"\r\n]+)"', source))
    # An x86 client also embeds the architecture-independent updater version,
    # which can equal the x64 display label. Select source labels for the actual
    # PE architecture before looking for their bytes in the executable.
    def label_architecture(label):
        marker = re.search(r"\bNS(32|64)\b", label)
        return ("x64" if marker[1] == "64" else "x86") if marker else None
    specific = {label for label in labels if label_architecture(label) == architecture}
    labels = specific or {label for label in labels if label_architecture(label) is None}
    binary = exe.read_bytes()
    matching = sorted(label for label in labels if label.encode("ascii") + b"\0" in binary)
    if len(matching) != 1:
        raise ValueError("Executable must contain exactly one matching source build label; rebuild or select matching -SourceRoot")
    label = matching[0]
    ids = dict((label, int(build_id)) for build_id, label in re.findall(
        r'\bkBuildId\s*=\s*(\d+)\s*;\s*const\s+char\s*\*\s*const\s+kBuildName\s*=\s*"([^"\r\n]+)"', source))
    info.update(buildLabel=label, sourceBuildId=ids.get(label), sha256=sha256(exe))
    if re.search(r"\bkReleaseVersion\b", source):
        versions = re.findall(r'\bkReleaseVersion\s*=\s*"([^"\r\n]*)"', source)
        if len(versions) != 1 or not re.fullmatch(r"[1-9][0-9]*\.[0-9]", versions[0]):
            raise ValueError("Source release version must be canonical major.minor, with one fractional digit (for example 1.0)")
        version = versions[0]
        if version.encode("ascii") + b"\0" not in binary:
            raise ValueError("Executable release version does not match source; rebuild or select matching -SourceRoot")
        info["releaseVersion"] = version
    return info


def license_tree(directory):
    """Licenses are bounded text files, never an arbitrary recursive SDK copy."""
    if not directory.is_dir() or linked(directory):
        raise ValueError(f"Required license directory missing or linked: {directory.name}")
    files = []
    for path in directory.rglob("*"):
        if linked(path):
            raise ValueError("Linked runtime license paths are not allowed")
        if path.is_file():
            if path.suffix.casefold() not in {".txt", ".md"} or path.stat().st_size > 2 * 1024 * 1024:
                raise ValueError(f"Unexpected runtime license payload: {path.name}")
            files.append(path)
            if len(files) > 256:
                raise ValueError("Runtime license tree exceeds its bounded size")
    return files


def runtime_files(exe, architecture):
    directory = exe.parent
    result = []
    for names, license_dir, required, x64_only in RUNTIME_GROUPS:
        present = [directory / name for name in names if (directory / name).exists()]
        if not present:
            continue
        if x64_only and architecture != "x64":
            raise ValueError("Vendor reconstruction DLLs cannot be bundled with an x86 client")
        if license_dir == "dxc-licenses" and len(present) != len(names):
            raise ValueError("DXC requires both dxcompiler.dll and dxil.dll")
        for binary in present:
            if pe_info(binary, dll=True)["architecture"] != architecture:
                raise ValueError(f"Runtime architecture does not match executable: {binary.name}")
            result.append((binary, binary.name))
        folder = directory / license_dir
        for name in required:
            require_file(folder / name)
        result.extend((path, path.relative_to(directory).as_posix()) for path in license_tree(folder))
    return result


def sdl_license(source_dir, exe):
    build_root = exe.parent.parent.parent.parent
    candidates = [build_root / "_deps/sdl3-src/LICENSE.txt", build_root / "_sdl3_src/LICENSE.txt"]
    for build in ("build", "build-x64"):
        candidates += [source_dir / build / "_deps/sdl3-src/LICENSE.txt", source_dir / build / "_sdl3_src/LICENSE.txt"]
    # Offline FetchContent builds can share an external SDL source checkout.
    # Read that exact configured path; no machine-specific fallback is assumed.
    for configured_build in (build_root, source_dir / "build", source_dir / "build-x64"):
        cache = configured_build / "CMakeCache.txt"
        if cache.is_file() and cache.stat().st_size < 4 * 1024 * 1024:
            for line in cache.read_text(encoding="utf-8-sig").splitlines():
                match = re.fullmatch(r"(?:FETCHCONTENT_SOURCE_DIR_SDL3|SDL3_SOURCE_DIR):[^=]+=(.+)", line)
                if match:
                    candidates.append(Path(match[1]) / "LICENSE.txt")
    for candidate in candidates:
        if candidate.is_file():
            return require_file(candidate)
    raise ValueError("SDL license not found in the build dependency directory")


def verified_game_entries(game_dir, manifest_path):
    manifest_path = require_file(manifest_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    if not isinstance(manifest, list) or not manifest or len(manifest) > 10000:
        raise ValueError("Invalid game extraction manifest")
    root = game_dir.resolve(strict=True)
    result, seen = [], set()
    for entry in manifest:
        if not isinstance(entry, dict) or not isinstance(entry.get("file"), str):
            raise ValueError("Malformed game manifest entry")
        name = entry["file"].replace("\\", "/")
        if name.casefold() in EXCLUDED_GAME:
            continue
        parts = name.split("/")
        allowed = re.fullmatch(r"Hod2\.(exe|ini)|(?:cam|coli|evt|mot|pol|sound|tex)/[^:]+", name, re.I)
        if not allowed or any(part in {"", ".", ".."} for part in parts) or PureWindowsPath(name).is_absolute() or name.casefold() in seen:
            raise ValueError(f"Unexpected or duplicate game manifest path: {name}")
        seen.add(name.casefold())
        size, digest = entry.get("bytes"), entry.get("sha256")
        if type(size) is not int or size < 0 or not isinstance(digest, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
            raise ValueError("Invalid game manifest length/hash")
        if name.casefold() in REQUIRED_GAME_FILES and not size:
            raise ValueError(f"Required boot data is empty: {name}")
        path = root.joinpath(*parts)
        if not path.resolve(strict=True).is_relative_to(root):
            raise ValueError("Game manifest path escapes the selected game directory")
        require_file(path)
        if path.stat().st_size != size or sha256(path) != digest.lower():
            raise ValueError(f"Game file differs from the extraction manifest: {name}")
        result.append((path, f"game/{name}", size, digest.lower()))
    missing = REQUIRED_GAME_FILES - seen
    if missing:
        raise ValueError("Game manifest is missing required boot-data records: " + ", ".join(sorted(missing)))
    return result


def source_revision(source_dir):
    def git(*arguments):
        result = subprocess.run(["git", "-C", str(source_dir), *arguments], capture_output=True, text=True, check=True)
        return result.stdout.strip()
    return dict(baseRevision=git("rev-parse", "HEAD"),
                status="local working tree changes" if git("status", "--porcelain", "--untracked-files=all", "--", "src", "tools", "CMakeLists.txt") else "clean committed source")


def privacy_check(path):
    username = check_release_privacy.getpass.getuser()
    forbidden = [username] if len(username) >= 5 and username.casefold() not in {"administrator", "runner", "build", "public", "default"} else []
    result = check_release_privacy.check_path(path, forbidden)
    if not result["passed"]:
        raise ValueError(f"Release privacy check failed for {path.name}; inspect with check_release_privacy.py before packaging")
    return result


def write_manifest(package_dir, metadata):
    def files():
        return sorted(path for path in package_dir.rglob("*") if path.is_file())
    payload = [dict(path=path.relative_to(package_dir).as_posix(), bytes=path.stat().st_size, sha256=sha256(path)) for path in files()]
    metadata = dict(metadata, files=payload, fileScope="Payload; excludes PACKAGE-MANIFEST.json and SHA256SUMS.txt")
    (package_dir / "PACKAGE-MANIFEST.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    sums = [f"{sha256(path)}  {path.relative_to(package_dir).as_posix()}" for path in files()]
    (package_dir / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n", encoding="ascii")


def write_file_assets(package_dir, out, info):
    """Publish a bounded, content-addressed alternative to the unchanged ZIP."""
    metadata = {}
    for name in ("PACKAGE-MANIFEST.json", "SHA256SUMS.txt"):
        path = require_file(package_dir / name)
        if path.stat().st_size > MAX_FILE_INDEX_BYTES:
            raise ValueError("File update metadata exceeds its bounded size")
        metadata[name] = path.read_bytes()
    manifest = json.loads(metadata["PACKAGE-MANIFEST.json"])
    if (manifest.get("format") != 1 or manifest.get("packageKind") != "public" or
            manifest.get("originalGameDataIncluded") is not False or manifest.get("executable") != info):
        raise ValueError("File update manifest does not match the public package")
    records = manifest.get("files")
    if not isinstance(records, list) or not 2 <= len(records) <= MAX_PUBLIC_FILES:
        raise ValueError("Invalid file update payload count")
    selected, seen = [], set()
    unpacked = sum(len(data) for data in metadata.values())
    for record in records:
        if not isinstance(record, dict):
            raise ValueError("Invalid file update payload record")
        name, size, digest = record.get("path"), record.get("bytes"), record.get("sha256")
        if (not isinstance(name, str) or not name or len(name) > 180 or
                "\\" in name or re.search(r'[\x00-\x1f\x7f<>:"|?*]', name) or
                any(part in {"", ".", ".."} or part.endswith((".", " ")) or
                    re.fullmatch(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", part, re.I)
                    for part in name.split("/")) or name.casefold() in seen or
                (name != "game/PUT_YOUR_GAME_FILES_HERE.txt" and not PUBLIC_MANAGED_PATH.fullmatch(name))):
            raise ValueError("Unexpected file update payload path")
        if (type(size) is not int or not 0 <= size <= MAX_PUBLIC_FILE_BYTES or
                not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest)):
            raise ValueError("Invalid file update payload size or hash")
        unpacked += size
        if unpacked > MAX_PUBLIC_TOTAL_BYTES:
            raise ValueError("File update payload exceeds its unpacked size limit")
        source = require_file(package_dir / name)
        if (not source.resolve(strict=True).is_relative_to(package_dir.resolve(strict=True)) or
                any(linked(parent) for parent in source.parents if parent != package_dir and parent.is_relative_to(package_dir)) or
                source.stat().st_size != size or sha256(source) != digest):
            raise ValueError(f"File update payload verification failed: {name}")
        selected.append((source, dict(path=name, bytes=size, sha256=digest)))
        seen.add(name.casefold())

    # Verify every staged source before emitting any blob. Identical payloads
    # share one deterministic gzip asset, irrespective of their original names.
    assets = out / "file-assets"
    assets.mkdir(exist_ok=False)
    packed, entries, packed_total = {}, [], 0
    for source, record in selected:
        digest = record["sha256"]
        if digest not in packed:
            blob = assets / f"hotd2re-file-{digest}.gz"
            observed, size = hashlib.sha256(), 0
            with source.open("rb") as incoming, blob.open("xb") as destination:
                with gzip.GzipFile(filename="", mode="wb", fileobj=destination, compresslevel=6, mtime=0) as stream:
                    for block in iter(lambda: incoming.read(1024 * 1024), b""):
                        size += len(block)
                        if size > record["bytes"]:
                            raise ValueError(f"File update payload changed during compression: {record['path']}")
                        observed.update(block)
                        stream.write(block)
            if size != record["bytes"] or observed.hexdigest() != digest:
                raise ValueError(f"File update payload changed during compression: {record['path']}")
            packed[digest] = dict(packedBytes=blob.stat().st_size, packedSha256=sha256(blob))
        entry = dict(record, **packed[digest])
        # Count every row, even shared blobs, to match consumer size validation.
        packed_total += entry["packedBytes"]
        if packed_total > MAX_FILE_ASSETS_BYTES:
            raise ValueError("File update payload exceeds its compressed size limit")
        entries.append(entry)
    index = dict(format=1, architecture=info["architecture"], buildLabel=info["buildLabel"],
                 manifestBase64=base64.b64encode(metadata["PACKAGE-MANIFEST.json"]).decode("ascii"),
                 checksumsBase64=base64.b64encode(metadata["SHA256SUMS.txt"]).decode("ascii"), files=entries)
    encoded = (json.dumps(index, separators=(",", ":")) + "\n").encode("utf-8")
    if len(encoded) > MAX_FILE_INDEX_BYTES:
        raise ValueError("File update index exceeds its bounded size")
    name = f"hotd2re-win-{info['architecture']}.files.json"
    with (out / name).open("xb") as stream:
        stream.write(encoded)
    with (out / f"{name}.sha256").open("x", encoding="ascii") as stream:
        stream.write(f"{hashlib.sha256(encoded).hexdigest()}  {name}\n")


def stage_package(*, kind, architecture, exe, source_dir, out, game_dir=None, game_manifest=None):
    source_dir, exe, out = source_dir.resolve(strict=True), exe.resolve(strict=True), out.resolve()
    if out.exists():
        raise ValueError("Output already exists. Choose a fresh -Out directory; existing files are never removed")
    info = executable_info(exe, source_dir, architecture)
    notices = require_file(source_dir / "THIRD_PARTY_NOTICES.txt")
    scene = require_file(source_dir / "scenes/st1_play.scene")
    runtime = runtime_files(exe, architecture)
    licenses = [(sdl_license(source_dir, exe), "LICENSE-SDL.txt"), (notices, "THIRD_PARTY_NOTICES.txt")]
    if (source_dir / "LICENSE").is_file():
        licenses.append((require_file(source_dir / "LICENSE"), "LICENSE-native-code.txt"))
    game, game_metadata = [], None
    if kind == "complete":
        if game_dir is None:
            raise ValueError("Complete-local packaging requires explicit -GameDir")
        game_dir = game_dir.resolve(strict=True)
        if out.is_relative_to(game_dir):
            raise ValueError("Package output must not be inside the read-only game directory")
        game_manifest = game_manifest or game_dir.parent / "original/game-manifest.json"
        game = verified_game_entries(game_dir, game_manifest)
        game_metadata = dict(files=len(game), extractionManifestSha256=sha256(game_manifest))
    elif kind != "public":
        raise ValueError("Unknown package kind")
    revision = source_revision(source_dir)
    privacy_check(exe)
    runtime_exceptions = []
    for original, _ in runtime:
        runtime_exceptions.extend(privacy_check(original)["vendor_build_path_exceptions"])
    readme = require_file(Path(__file__).with_name(f"release_readme_{kind}.txt")).read_text(encoding="utf-8")
    # All inputs are validated before creating anything. mkdir refuses races or
    # pre-existing output; there is no recursive deletion or overwrite path.
    out.mkdir(parents=True, exist_ok=False)
    package_dir = out / "hotd2re"
    package_dir.mkdir()
    for original, relative in [(exe, "hotd2.exe"), (scene, "scenes/st1_play.scene"), *licenses, *runtime]:
        destination = package_dir / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, destination)
    if sha256(package_dir / "hotd2.exe") != info["sha256"]:
        raise ValueError("Executable changed during packaging; select a stable completed build")
    for original, relative, size, digest in game:
        destination = package_dir / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, destination)
        if destination.stat().st_size != size or sha256(destination) != digest:
            raise ValueError(f"Copied game file failed verification: {relative}")
    (package_dir / "game").mkdir(exist_ok=True)
    if kind == "public":
        (package_dir / "game/PUT_YOUR_GAME_FILES_HERE.txt").write_text("Copy your own House of the Dead 2 installation here: Hod2.exe and its data folders.\n", encoding="ascii")
    else:
        (package_dir / "hotd2re_settings.ini").write_text("master=5\nmusic=5\nguns=5\nsfx=5\nattraction=5\ninput=mouse\nfullscreen=0\nscale=1\naspect=4:3\ncrt=0\nrender=1\nlobby=64.23.186.88:27020\n", encoding="ascii")
    release_version = info.get("releaseVersion", info["buildLabel"])
    (package_dir / "README.txt").write_text(readme.replace("{{BUILD_LABEL}}", info["buildLabel"]).replace("{{RELEASE_VERSION}}", release_version).replace("{{ARCHITECTURE}}", architecture), encoding="utf-8")
    (package_dir / "BUILD.txt").write_text(f"Native Windows {architecture} / {info['peFormat']} ({info['machine']})\nRelease version: {release_version}\nBuild label: {info['buildLabel']}\nSource base revision: {revision['baseRevision']} ({revision['status']})\nhotd2.exe SHA256: {info['sha256']}\nPackage kind: {kind}; original game data included: {kind == 'complete'}\n", encoding="ascii")
    (package_dir / "CREDITS.txt").write_text("Native code, SDL and shader notices are retained in LICENSE-native-code.txt (when supplied), LICENSE-SDL.txt and THIRD_PARTY_NOTICES.txt.\nOptional runtime license/notice trees accompany their DLLs.\nOriginal game data retains its respective rights and is not covered by the native code license.\n", encoding="ascii")
    metadata = dict(format=1, packageKind=kind, originalGameDataIncluded=kind == "complete", executable=info,
                    sourceAtPackaging=revision, optionalRuntimeDlls=[name for _, name in runtime if name.endswith(".dll")],
                    thirdPartyBuildPathExceptions=runtime_exceptions, game=game_metadata)
    write_manifest(package_dir, metadata)
    privacy_check(package_dir)
    if kind == "public":
        write_file_assets(package_dir, out, info)
    name = "hotd2re-complete.zip" if kind == "complete" else f"hotd2re-win-{architecture}.zip"
    archive_path = out / name
    with zipfile.ZipFile(archive_path, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted(package_dir.rglob("*")):
            if path.is_file():
                archive.write(path, f"hotd2re/{path.relative_to(package_dir).as_posix()}")
    privacy_check(archive_path)
    (out / f"{name}.sha256").write_text(f"{sha256(archive_path)}  {name}\n", encoding="ascii")
    return archive_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=("public", "complete"), required=True)
    parser.add_argument("--architecture", choices=("x86", "x64"), default="x86")
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--source-root", type=Path, default=REPO)
    parser.add_argument("--game-dir", type=Path)
    parser.add_argument("--game-manifest", type=Path)
    args = parser.parse_args()
    default_build = "build-x64" if args.architecture == "x64" else "build"
    exe = args.exe or REPO / default_build / "src/port/Release/hotd2.exe"
    out = args.out or REPO / "build" / ("dist-complete-local" if args.kind == "complete" else "dist")
    resolve = lambda path: path if path is None or path.is_absolute() else REPO / path
    try:
        archive = stage_package(kind=args.kind, architecture=args.architecture, exe=resolve(exe), source_dir=resolve(args.source_root),
                                out=resolve(out), game_dir=resolve(args.game_dir), game_manifest=resolve(args.game_manifest))
        print(f"Package: {archive.name} ({archive.stat().st_size} bytes); SHA256 manifest written")
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Packaging refused: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
