"""Synthetic package and read-only game metadata tests; no original game data."""
import base64
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import package_release as release


def fake_pe(path, architecture="x86", label=None, dll=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray(4096)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 128)
    data[128:132] = b"PE\0\0"
    wide = architecture == "x64"
    struct.pack_into("<HHIIIHH", data, 132, 0x8664 if wide else 0x14C, 1, 0, 0, 0, 240 if wide else 224, 0x102 | (0x2000 if dll else 0))
    struct.pack_into("<H", data, 152, 0x20B if wide else 0x10B)
    if label:
        text = label.encode("ascii") + b"\0"
        data[1024:1024 + len(text)] = text
    path.write_bytes(data)
    return path


class Packaging(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="hotd2-package-fixture-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        (self.source / "src/port/tools").mkdir(parents=True)
        (self.source / "src/port/tools/play.cpp").write_text('''#if INTPTR_MAX == INT64_MAX
constexpr uint32_t kBuildId = 2026101906;
const char* const kBuildName = "2026 10 19 NS64";
#else
constexpr uint32_t kBuildId = 2026101905;
const char* const kBuildName = "2026 10 19 NS32";
#endif
''')
        (self.source / "scenes").mkdir()
        (self.source / "scenes/st1_play.scene").write_text("world external\n")
        (self.source / "THIRD_PARTY_NOTICES.txt").write_text("Fixture shader notices\n")
        (self.source / "LICENSE").write_text("Fixture code license\n")
        dependency = self.source / "build/_deps/sdl3-src"
        dependency.mkdir(parents=True)
        (dependency / "LICENSE.txt").write_text("Fixture SDL license\n")
        self.exe = fake_pe(self.source / "build/src/port/Release/hotd2.exe", label="2026 10 19 NS32")
        self.revision = patch.object(release, "source_revision", return_value={"baseRevision": "0" * 40, "status": "synthetic fixture"})
        self.revision.start()
        self.addCleanup(self.revision.stop)

    def package(self, **arguments):
        return release.stage_package(kind=arguments.pop("kind", "public"), architecture=arguments.pop("architecture", "x86"), exe=arguments.pop("exe", self.exe), source_dir=self.source,
                                     out=arguments.pop("out", self.root / "new-output"), **arguments)

    def test_actual_pe_selects_ns32_when_source_lists_ns64_first(self):
        info = release.executable_info(self.exe, self.source, "x86")
        self.assertEqual((info["buildLabel"], info["sourceBuildId"], info["machine"]), ("2026 10 19 NS32", 2026101905, "0x014c"))
        self.assertNotIn("releaseVersion", info)
        other = fake_pe(self.root / "x64.exe", "x64", "2026 10 19 NS64")
        self.assertEqual(release.executable_info(other, self.source, "x64")["peFormat"], "PE32+")

    def test_wrong_architecture_kind_or_label_is_rejected(self):
        with self.assertRaises(ValueError):
            release.executable_info(self.exe, self.source, "x64")
        for label in ("old build", "2026 10 19 NS64"):
            broken = fake_pe(self.root / "wrong.exe", label=label)
            with self.assertRaises(ValueError):
                release.executable_info(broken, self.source, "x86")
        with self.assertRaises(ValueError):
            release.pe_info(fake_pe(self.root / "not-an-exe.exe", dll=True))

    def test_updater_version_does_not_override_architecture_display_label(self):
        (self.source / "src/port/tools/play.cpp").write_text('''#if INTPTR_MAX == INT64_MAX
constexpr uint32_t kBuildId = 2026102300;
const char* const kBuildName = "2026 10 23 A";
#else
constexpr uint32_t kBuildId = 2026102301;
const char* const kBuildName = "2026 10 23 NS32 A";
#endif
const char* const kUpdateBuildName = "2026 10 23 A";
''')
        exe = fake_pe(self.root / "updater.exe", "x86", "2026 10 23 NS32 A")
        exe.write_bytes(exe.read_bytes() + b"2026 10 23 A\0")
        info = release.executable_info(exe, self.source, "x86")
        self.assertEqual((info["buildLabel"], info["sourceBuildId"]), ("2026 10 23 NS32 A", 2026102301))
        wide = fake_pe(self.root / "updater64.exe", "x64", "2026 10 23 A")
        self.assertEqual(release.executable_info(wide, self.source, "x64")["sourceBuildId"], 2026102300)
        missing_display = fake_pe(self.root / "missing-label.exe", "x86", "2026 10 23 A")
        with self.assertRaises(ValueError):
            release.executable_info(missing_display, self.source, "x86")

    def test_truncated_or_hostile_pe_offsets_are_rejected(self):
        for data in (b"MZ", b"not an exe", self.exe.read_bytes()[:160]):
            broken = self.root / "broken.exe"
            broken.write_bytes(data)
            with self.assertRaises(ValueError):
                release.pe_info(broken)
        broken = bytearray(self.exe.read_bytes())
        struct.pack_into("<I", broken, 0x3C, 0xFFFFFFFF)
        (self.root / "offset.exe").write_bytes(broken)
        with self.assertRaises(ValueError):
            release.pe_info(self.root / "offset.exe")

    def test_existing_output_is_refused_without_removing_anything(self):
        output = self.root / "keep"
        output.mkdir()
        marker = output / "important.txt"
        marker.write_bytes(b"preserve this")
        with self.assertRaisesRegex(ValueError, "Output already exists"):
            self.package(out=output)
        self.assertEqual(marker.read_bytes(), b"preserve this")
        self.assertEqual(list(output.iterdir()), [marker])

    def test_release_version_is_public_while_internal_build_labels_are_preserved(self):
        (self.source / "src/port/tools/play.cpp").write_text('''#if INTPTR_MAX == INT64_MAX
constexpr uint32_t kBuildId = 2026102302;
const char* const kBuildName = "2026 10 23 B";
#else
constexpr uint32_t kBuildId = 2026102303;
const char* const kBuildName = "2026 10 23 NS32 B";
#endif
const char* const kReleaseVersion = "1.0";
''')
        game, game_manifest, _ = self.game_manifest()
        for architecture, label, build_id in (("x64", "2026 10 23 B", 2026102302), ("x86", "2026 10 23 NS32 B", 2026102303)):
            exe = fake_pe(self.root / architecture / "hotd2.exe", architecture, label)
            exe.write_bytes(exe.read_bytes() + b"1.0\0")
            info = release.executable_info(exe, self.source, architecture)
            self.assertEqual((info["releaseVersion"], info["buildLabel"], info["sourceBuildId"]), ("1.0", label, build_id))
            for kind in ("public", "complete"):
                with self.subTest(architecture=architecture, kind=kind):
                    arguments = dict(game_dir=game, game_manifest=game_manifest) if kind == "complete" else {}
                    archive = self.package(kind=kind, architecture=architecture, exe=exe,
                                           out=self.root / f"release-{architecture}-{kind}", **arguments)
                    package = archive.parent / "hotd2re"
                    manifest = json.loads((package / "PACKAGE-MANIFEST.json").read_bytes())
                    self.assertEqual(manifest["executable"]["releaseVersion"], "1.0")
                    self.assertEqual(manifest["executable"]["buildLabel"], label)
                    self.assertIn(f"Release 1.0 (Windows {architecture}).", (package / "README.txt").read_text())
                    self.assertIn(f"Release version: 1.0\nBuild label: {label}\n", (package / "BUILD.txt").read_text())
                    if kind == "public":
                        index = json.loads((archive.parent / f"hotd2re-win-{architecture}.files.json").read_bytes())
                        self.assertEqual(index["buildLabel"], label)
                        self.assertEqual(json.loads(base64.b64decode(index["manifestBase64"]))["executable"]["releaseVersion"], "1.0")

    def test_release_versions_reject_noncanonical_source_or_stale_executables(self):
        source_path = self.source / "src/port/tools/play.cpp"
        original_source = source_path.read_text()
        original_binary = self.exe.read_bytes()
        for version in ("0.0", "01.0", "1", "1.00", "1.10", "-1.0", "1.a", "1.0-beta", "", "1.0 "):
            with self.subTest(version=version):
                source_path.write_text(original_source + f'\nconst char* const kReleaseVersion = "{version}";\n')
                with self.assertRaisesRegex(ValueError, "canonical major.minor"):
                    release.executable_info(self.exe, self.source, "x86")
        for version in ("1.0", "1.9", "2.0", "10.0"):
            source_path.write_text(original_source + f'\nconst char* const kReleaseVersion = "{version}";\n')
            self.exe.write_bytes(original_binary + version.encode("ascii") + b"\0")
            self.assertEqual(release.executable_info(self.exe, self.source, "x86")["releaseVersion"], version)
        source_path.write_text(original_source + '\nconst char* const kReleaseVersion = "1.0";\n')
        for marker in (b"", b"1.1\0", b"1.0"):
            with self.subTest(marker=marker):
                self.exe.write_bytes(original_binary + marker)
                with self.assertRaisesRegex(ValueError, "Executable release version does not match"):
                    self.package()
                self.assertFalse((self.root / "new-output").exists())
        source_path.write_text(original_source + '\nconst char* const kReleaseVersion = GetVersion();\n')
        with self.assertRaisesRegex(ValueError, "canonical major.minor"):
            release.executable_info(self.exe, self.source, "x86")

    @unittest.skipUnless(os.name == "nt" and shutil.which("powershell"), "PowerShell wrapper integration requires Windows")
    def test_powershell_wrappers_forward_paths_and_preserve_existing_outputs(self):
        output = self.root / "existing output with spaces"
        output.mkdir()
        marker = output / "keep.txt"
        marker.write_bytes(b"keep this output")
        for script in ("make_dist.ps1", "make_complete_dist.ps1"):
            arguments = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(release.REPO / "tools" / script),
                         "-Architecture", "x86", "-Exe", str(self.exe), "-SourceRoot", str(self.source), "-Out", str(output), "-Python", sys.executable]
            if script == "make_complete_dist.ps1":
                arguments += ["-GameDir", str(self.root / "not-read")]
            result = subprocess.run(arguments, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Output already exists", result.stderr)
            self.assertEqual(marker.read_bytes(), b"keep this output")
            self.assertEqual(list(output.iterdir()), [marker])

    def test_public_package_has_no_game_data_or_private_siblings(self):
        (self.exe.parent / "hotd2re_save.bin").write_bytes(b"private")
        (self.exe.parent / "random-sdk-file.dll").write_bytes(b"not permitted")
        (self.source / "game").mkdir()
        (self.source / "game/Hod2.exe").write_bytes(b"original game is never copied")
        archive = self.package()
        self.assertEqual(archive.name, "hotd2re-win-x86.zip")
        with zipfile.ZipFile(archive) as contents:
            names = contents.namelist()
            self.assertNotIn("hotd2re/game/Hod2.exe", names)
            self.assertFalse(any("save.bin" in name or "random-sdk" in name for name in names))
            self.assertIn("hotd2re/game/PUT_YOUR_GAME_FILES_HERE.txt", names)
        package = archive.parent / "hotd2re"
        manifest = json.loads((package / "PACKAGE-MANIFEST.json").read_text())
        self.assertFalse(manifest["originalGameDataIncluded"])
        self.assertEqual(manifest["executable"]["architecture"], "x86")
        self.assertNotIn("releaseVersion", manifest["executable"])
        self.assertIn("Release 2026 10 19 NS32 (Windows x86).", (package / "README.txt").read_text())
        self.assertIn("Release version: 2026 10 19 NS32\nBuild label: 2026 10 19 NS32\n", (package / "BUILD.txt").read_text())
        for entry in manifest["files"]:
            self.assertEqual(release.sha256(package / entry["path"]), entry["sha256"])
        checks = (package / "SHA256SUMS.txt").read_text().splitlines()
        self.assertTrue(any(line.endswith("  PACKAGE-MANIFEST.json") for line in checks))
        for line in checks:
            digest, relative = line.split("  ", 1)
            self.assertEqual(release.sha256(package / relative), digest)
        self.assertEqual((archive.parent / (archive.name + ".sha256")).read_text().split()[0], release.sha256(archive))
        index = json.loads((archive.parent / "hotd2re-win-x86.files.json").read_bytes())
        self.assertEqual({entry["path"] for entry in index["files"]}, {entry["path"] for entry in manifest["files"]})
        self.assertFalse(any("save.bin" in entry["path"] or "random-sdk" in entry["path"] for entry in index["files"]))

    def test_file_assets_reconstruct_the_exact_legacy_zip_payload(self):
        for architecture in ("x86", "x64"):
            with self.subTest(architecture=architecture):
                exe = fake_pe(self.root / architecture / "hotd2.exe", architecture,
                              "2026 10 19 NS64" if architecture == "x64" else "2026 10 19 NS32")
                archive = self.package(architecture=architecture, exe=exe, out=self.root / architecture / "output")
                package = archive.parent / "hotd2re"
                index_path = archive.parent / f"hotd2re-win-{architecture}.files.json"
                index = json.loads(index_path.read_bytes())
                manifest_bytes = base64.b64decode(index["manifestBase64"], validate=True)
                checksums_bytes = base64.b64decode(index["checksumsBase64"], validate=True)
                self.assertEqual(manifest_bytes, (package / "PACKAGE-MANIFEST.json").read_bytes())
                self.assertEqual(checksums_bytes, (package / "SHA256SUMS.txt").read_bytes())
                manifest = json.loads(manifest_bytes)
                self.assertEqual((index["format"], index["architecture"], index["buildLabel"]),
                                 (1, architecture, manifest["executable"]["buildLabel"]))
                self.assertEqual([{key: entry[key] for key in ("path", "bytes", "sha256")} for entry in index["files"]],
                                 manifest["files"])
                self.assertEqual(index_path.with_name(index_path.name + ".sha256").read_text(),
                                 f"{release.sha256(index_path)}  {index_path.name}\n")
                restored = {"PACKAGE-MANIFEST.json": manifest_bytes, "SHA256SUMS.txt": checksums_bytes}
                for entry in index["files"]:
                    blob = archive.parent / "file-assets" / f"hotd2re-file-{entry['sha256']}.gz"
                    compressed = blob.read_bytes()
                    self.assertEqual(len(compressed), entry["packedBytes"])
                    self.assertEqual(hashlib.sha256(compressed).hexdigest(), entry["packedSha256"])
                    restored[entry["path"]] = gzip.decompress(compressed)
                    self.assertEqual(len(restored[entry["path"]]), entry["bytes"])
                    self.assertEqual(hashlib.sha256(restored[entry["path"]]).hexdigest(), entry["sha256"])
                self.assertIn("game/PUT_YOUR_GAME_FILES_HERE.txt", restored)
                for line in checksums_bytes.decode("ascii").splitlines():
                    digest, relative = line.split("  ", 1)
                    self.assertEqual(hashlib.sha256(restored[relative]).hexdigest(), digest)
                with zipfile.ZipFile(archive) as legacy_zip:
                    self.assertIsNone(legacy_zip.testzip())
                    self.assertEqual(set(legacy_zip.namelist()), {"hotd2re/" + name for name in restored})
                    for name, data in restored.items():
                        self.assertEqual(legacy_zip.read("hotd2re/" + name), data)

    def test_file_assets_are_deterministic_and_deduplicate_identical_payloads(self):
        (self.source / "LICENSE").write_bytes((self.source / "THIRD_PARTY_NOTICES.txt").read_bytes())
        first = self.package(out=self.root / "first")
        second = self.package(out=self.root / "second")
        name = "hotd2re-win-x86.files.json"
        self.assertEqual((first.parent / name).read_bytes(), (second.parent / name).read_bytes())
        index = json.loads((first.parent / name).read_bytes())
        entries = {entry["path"]: entry for entry in index["files"]}
        self.assertEqual(entries["LICENSE-native-code.txt"]["sha256"], entries["THIRD_PARTY_NOTICES.txt"]["sha256"])
        self.assertEqual(entries["LICENSE-native-code.txt"]["packedSha256"], entries["THIRD_PARTY_NOTICES.txt"]["packedSha256"])
        blobs = list((first.parent / "file-assets").iterdir())
        self.assertEqual(len(blobs), len({entry["sha256"] for entry in index["files"]}))
        for blob in blobs:
            data = blob.read_bytes()
            self.assertEqual(data, (second.parent / "file-assets" / blob.name).read_bytes())
            self.assertEqual(data[:4], b"\x1f\x8b\x08\x00")  # no original filename
            self.assertEqual(data[4:8], b"\0" * 4)  # deterministic mtime

        # The consumer bounds all rows even though shared blobs download once.
        maximum = sum(entry["packedBytes"] for entry in index["files"]) - 1
        self.assertLessEqual(sum(blob.stat().st_size for blob in blobs), maximum)
        output = self.root / "compressed-limit"
        output.mkdir()
        manifest = json.loads((first.parent / "hotd2re/PACKAGE-MANIFEST.json").read_bytes())
        with patch.object(release, "MAX_FILE_ASSETS_BYTES", maximum):
            with self.assertRaisesRegex(ValueError, "compressed size limit"):
                release.write_file_assets(first.parent / "hotd2re", output, manifest["executable"])
        self.assertFalse((output / name).exists())

    @unittest.skipUnless(os.name == "nt" and shutil.which("powershell"), "File-update consumer requires Windows PowerShell")
    def test_python_file_assets_stage_with_windows_powershell_consumer(self):
        (self.source / "src/port/tools/play.cpp").write_text(
            'constexpr uint32_t kBuildId = 2026102400;\nconst char* const kBuildName = "2026 10 24 A";\n')
        exe = fake_pe(self.root / "interop/hotd2.exe", "x64", "2026 10 24 A")
        (self.source / "LICENSE").write_bytes((self.source / "THIRD_PARTY_NOTICES.txt").read_bytes())
        archive = self.package(architecture="x64", exe=exe)
        index = archive.parent / "hotd2re-win-x64.files.json"
        installed = self.root / "isolated installation"
        installed.mkdir()
        script = self.root / "stage-file-update.ps1"
        script.write_text(r'''param($Consumer, $Root, $Index, $Hash, $Assets)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. $Consumer
$calls = [Collections.Generic.List[string]]::new()
$download = {
    param($Asset, $Destination, $MaximumBytes, $Description)
    $calls.Add($Asset)
    Copy-Item -LiteralPath (Join-Path $Assets $Asset) -Destination $Destination
}.GetNewClosure()
$payload = Expand-VerifiedFileUpdate -Root $Root -IndexPath $Index -ExpectedSha256 $Hash `
    -Stage (Join-Path $Root '.hotd2-update/stage') -Architecture x64 -Tag 'v2026.10.24-a' -DownloadFile $download
[pscustomobject]@{ payload = $payload; downloads = $calls.ToArray() } | ConvertTo-Json -Compress
''', encoding="utf-8")
        powershell = Path(os.environ["SystemRoot"]) / "System32/WindowsPowerShell/v1.0/powershell.exe"
        result = subprocess.run(
            [str(powershell), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", str(script),
             "-Consumer", str(release.REPO / "src/port/update/update_transaction.ps1"),
             "-Root", str(installed), "-Index", str(index), "-Hash", release.sha256(index),
             "-Assets", str(archive.parent / "file-assets")], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        output = json.loads(result.stdout)
        payload = Path(output["payload"])
        expected = {"hotd2re-file-" + entry["sha256"] + ".gz" for entry in json.loads(index.read_bytes())["files"]}
        self.assertEqual(set(output["downloads"]), expected)
        self.assertEqual(len(output["downloads"]), len(expected))
        with zipfile.ZipFile(archive) as contents:
            restored = {path.relative_to(payload).as_posix(): path.read_bytes() for path in payload.rglob("*") if path.is_file()}
            self.assertEqual({"hotd2re/" + name for name in restored}, set(contents.namelist()))
            for name, data in restored.items():
                self.assertEqual(data, contents.read("hotd2re/" + name), name)

    def test_file_assets_recheck_staged_payload_before_emitting_blobs(self):
        archive = self.package()
        package = archive.parent / "hotd2re"
        manifest = json.loads((package / "PACKAGE-MANIFEST.json").read_bytes())
        path = package / "hotd2.exe"
        original = path.read_bytes()
        for name, changed in (("same-size", bytes([original[0] ^ 1]) + original[1:]), ("wrong-size", original + b"x")):
            with self.subTest(name=name):
                path.write_bytes(changed)
                output = self.root / name
                output.mkdir()
                with self.assertRaisesRegex(ValueError, "payload verification failed"):
                    release.write_file_assets(package, output, manifest["executable"])
                self.assertEqual(list(output.iterdir()), [])
        path.write_bytes(original)

    def test_file_asset_bounds_and_player_paths_are_rejected(self):
        archive = self.package()
        package = archive.parent / "hotd2re"
        manifest_path = package / "PACKAGE-MANIFEST.json"
        original = manifest_path.read_bytes()
        manifest = json.loads(original)
        records = manifest["files"]
        index = archive.parent / "hotd2re-win-x86.files.json"
        limits = (("MAX_PUBLIC_FILES", len(records) - 1, "payload count"),
                  ("MAX_PUBLIC_FILE_BYTES", max(entry["bytes"] for entry in records) - 1, "payload size"),
                  ("MAX_PUBLIC_TOTAL_BYTES", sum(entry["bytes"] for entry in records), "unpacked size limit"),
                  ("MAX_FILE_INDEX_BYTES", index.stat().st_size - 1, "index exceeds"))
        for constant, maximum, message in limits:
            with self.subTest(limit=constant):
                output = self.root / constant
                output.mkdir()
                with patch.object(release, constant, maximum), self.assertRaisesRegex(ValueError, message):
                    release.write_file_assets(package, output, manifest["executable"])
                self.assertFalse((output / index.name).exists())
                self.assertFalse((output / (index.name + ".sha256")).exists())
        for name in ("hotd2re_settings.ini", "game/Hod2.exe", "../outside.txt", "dxc-licenses/NUL.txt"):
            with self.subTest(path=name):
                manifest["files"] = records + [dict(path=name, bytes=0, sha256=hashlib.sha256(b"").hexdigest())]
                manifest_path.write_text(json.dumps(manifest))
                output = self.root / ("invalid-path-" + str(len(list(self.root.iterdir()))))
                output.mkdir()
                with self.assertRaisesRegex(ValueError, "payload path"):
                    release.write_file_assets(package, output, manifest["executable"])
                self.assertEqual(list(output.iterdir()), [])
        manifest_path.write_bytes(original)

    def test_optional_runtime_requires_matching_pe_and_all_notices(self):
        exe = fake_pe(self.root / "vendor/hotd2.exe", "x64", "2026 10 19 NS64")
        expected = []
        for names, folder, required, _ in release.RUNTIME_GROUPS:
            for name in names:
                fake_pe(exe.parent / name, "x64", dll=True)
                expected.append(name)
            license_dir = exe.parent / folder
            license_dir.mkdir(parents=True)
            for name in (*required, "NOTICE-extra.md"):
                (license_dir / name).write_text("Fixture runtime notice\n")
        archive = self.package(exe=exe, architecture="x64")
        with zipfile.ZipFile(archive) as contents:
            for name in expected:
                self.assertIn("hotd2re/" + name, contents.namelist())
            for _, folder, required, _ in release.RUNTIME_GROUPS:
                for name in (*required, "NOTICE-extra.md"):
                    self.assertIn(f"hotd2re/{folder}/{name}", contents.namelist())

    def test_missing_runtime_licenses_prevent_any_output(self):
        fake_pe(self.exe.parent / "dxcompiler.dll", dll=True)
        with self.assertRaisesRegex(ValueError, "DXC requires both"):
            self.package()
        self.assertFalse((self.root / "new-output").exists())
        fake_pe(self.exe.parent / "dxil.dll", dll=True)
        with self.assertRaises(ValueError):
            self.package()
        self.assertFalse((self.root / "new-output").exists())

    def test_mismatched_runtime_architecture_is_rejected(self):
        fake_pe(self.exe.parent / "dxcompiler.dll", "x64", dll=True)
        fake_pe(self.exe.parent / "dxil.dll", "x64", dll=True)
        with self.assertRaisesRegex(ValueError, "Runtime architecture"):
            release.runtime_files(self.exe, "x86")

    def test_unknown_runtime_home_path_is_rejected_before_staging(self):
        for name in ("dxcompiler.dll", "dxil.dll"):
            path = fake_pe(self.exe.parent / name, dll=True)
            if name == "dxcompiler.dll":
                with path.open("ab") as stream:
                    stream.write(b"/home/local-developer/private/source.cpp\0")
        licenses = self.exe.parent / "dxc-licenses"
        licenses.mkdir()
        for name in release.RUNTIME_GROUPS[-1][2]:
            (licenses / name).write_text("Fixture license\n")
        with self.assertRaisesRegex(ValueError, "privacy check failed"):
            self.package()
        self.assertFalse((self.root / "new-output").exists())

    def test_offline_sdl_source_is_discovered_from_build_metadata(self):
        original = self.source / "build/_deps/sdl3-src/LICENSE.txt"
        external = self.root / "external-sdl"
        external.mkdir()
        original.rename(external / "LICENSE.txt")
        (self.source / "build/CMakeCache.txt").write_text(f"FETCHCONTENT_SOURCE_DIR_SDL3:PATH={external.as_posix()}\n")
        self.assertEqual(release.sdl_license(self.source, self.exe), external / "LICENSE.txt")

    def game_manifest(self, entries=None):
        directory = self.root / "separate-installation/game"
        directory.mkdir(parents=True, exist_ok=True)
        files = {name: ("synthetic fixture: " + name).encode() for name in sorted(release.REQUIRED_GAME_FILES)}
        files["tex/model.bin"] = b"synthetic texture"
        result = []
        for name, data in files.items():
            path = directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            result.append(dict(file=name.replace("/", "\\"), bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
        metadata = directory.parent / "original/game-manifest.json"
        metadata.parent.mkdir()
        metadata.write_text(json.dumps(entries if entries is not None else result))
        return directory, metadata, result

    def test_external_game_directory_and_manifest_validate_without_packaging(self):
        directory, metadata, result = self.game_manifest()
        result.append(dict(file="pol\\bg_adv19.bin", bytes=5, sha256="0" * 64))
        metadata.write_text(json.dumps(result))
        selected = release.verified_game_entries(directory, metadata)
        self.assertEqual({name.casefold() for _, name, _, _ in selected},
                         {"game/" + name for name in release.REQUIRED_GAME_FILES} | {"game/tex/model.bin"})
        self.assertFalse(list(self.root.rglob("*.zip")))

    def test_complete_archive_uses_stable_name_and_matching_checksum(self):
        directory, metadata, entries = self.game_manifest()
        for architecture in ("x86", "x64"):
            with self.subTest(architecture=architecture):
                exe = fake_pe(self.root / architecture / "hotd2.exe", architecture,
                              "2026 10 19 NS64" if architecture == "x64" else "2026 10 19 NS32")
                archive = self.package(kind="complete", architecture=architecture, exe=exe,
                                       game_dir=directory, game_manifest=metadata,
                                       out=self.root / ("complete-" + architecture))
                self.assertEqual(archive.name, "hotd2re-complete.zip")
                checksum = archive.with_name("hotd2re-complete.zip.sha256")
                self.assertEqual(checksum.read_text(),
                                 f"{release.sha256(archive)}  hotd2re-complete.zip\n")
                self.assertFalse((archive.parent / "file-assets").exists())
                self.assertFalse(list(archive.parent.glob("*.files.json*")))
                with zipfile.ZipFile(archive) as contents:
                    self.assertIsNone(contents.testzip())
                    for entry in entries:
                        relative = entry["file"].replace("\\", "/")
                        self.assertEqual(contents.read("hotd2re/game/" + relative),
                                         (directory / relative).read_bytes())
                    manifest = json.loads(contents.read("hotd2re/PACKAGE-MANIFEST.json"))
                    self.assertEqual(manifest["packageKind"], "complete")
                    self.assertEqual(manifest["executable"]["architecture"], architecture)
                    self.assertTrue(manifest["originalGameDataIncluded"])

    def test_truncated_manifest_missing_boot_data_is_refused(self):
        directory, metadata, result = self.game_manifest()
        metadata.write_text(json.dumps([entry for entry in result if entry["file"].casefold() == "hod2.exe"]))
        with self.assertRaisesRegex(ValueError, "missing required boot-data records"):
            release.verified_game_entries(directory, metadata)
        self.assertFalse(list(self.root.rglob("*.zip")))

    def test_game_paths_duplicates_and_hashes_are_checked(self):
        directory, metadata, result = self.game_manifest()
        for bad in (dict(file="tex\\..\\..\\private.txt", bytes=0, sha256="0" * 64), result[0],
                    dict(file="tex/model.bin", bytes=17, sha256="0" * 64)):
            metadata.write_text(json.dumps(result + [bad]))
            with self.assertRaises((ValueError, OSError)):
                release.verified_game_entries(directory, metadata)
        damaged = [dict(entry) for entry in result]
        damaged[0]["sha256"] = "0" * 64
        metadata.write_text(json.dumps(damaged))
        with self.assertRaisesRegex(ValueError, "differs from"):
            release.verified_game_entries(directory, metadata)


if __name__ == "__main__":
    unittest.main()
