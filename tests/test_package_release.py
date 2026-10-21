"""Synthetic public packages and read-only game metadata tests; no game ZIPs."""
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
        return release.stage_package(kind="public", architecture=arguments.pop("architecture", "x86"), exe=arguments.pop("exe", self.exe), source_dir=self.source,
                                     out=arguments.pop("out", self.root / "new-output"), **arguments)

    def test_actual_pe_selects_ns32_when_source_lists_ns64_first(self):
        info = release.executable_info(self.exe, self.source, "x86")
        self.assertEqual((info["buildLabel"], info["sourceBuildId"], info["machine"]), ("2026 10 19 NS32", 2026101905, "0x014c"))
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
        with zipfile.ZipFile(archive) as contents:
            names = contents.namelist()
            self.assertNotIn("hotd2re/game/Hod2.exe", names)
            self.assertFalse(any("save.bin" in name or "random-sdk" in name for name in names))
            self.assertIn("hotd2re/game/PUT_YOUR_GAME_FILES_HERE.txt", names)
        package = archive.parent / "hotd2re"
        manifest = json.loads((package / "PACKAGE-MANIFEST.json").read_text())
        self.assertFalse(manifest["originalGameDataIncluded"])
        self.assertEqual(manifest["executable"]["architecture"], "x86")
        for entry in manifest["files"]:
            self.assertEqual(release.sha256(package / entry["path"]), entry["sha256"])
        checks = (package / "SHA256SUMS.txt").read_text().splitlines()
        self.assertTrue(any(line.endswith("  PACKAGE-MANIFEST.json") for line in checks))
        for line in checks:
            digest, relative = line.split("  ", 1)
            self.assertEqual(release.sha256(package / relative), digest)
        self.assertEqual((archive.parent / (archive.name + ".sha256")).read_text().split()[0], release.sha256(archive))

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
