"""Regression checks for the release privacy gate; fixtures are synthetic."""
import importlib.util
import hashlib
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("privacy", ROOT / "tools/check_release_privacy.py")
privacy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(privacy)


class ReleasePrivacyTest(unittest.TestCase):
    def test_embedded_home_paths_in_both_encodings(self):
        for path in ["C:" + "\\Users\\ExamplePerson\\source.cpp", "/home/" + "example/source.cpp",
                     "D:" + "\\Documents and Settings\\ExamplePerson\\source.cpp"]:
            for encoding in ("utf-8", "utf-16le"):
                self.assertIn("personal home-directory path", privacy.check_bytes(b"\x01" + path.encode(encoding), []))

    def test_credentials_and_custom_identifiers(self):
        self.assertIn("credential pattern", privacy.check_bytes(b"ghp_" + b"x" * 36, []))
        self.assertIn("forbidden identifier", privacy.check_bytes("ExamplePerson".encode("utf-16le"), ["ExamplePerson"]))

    def test_zip_checks_contents_names_and_comments(self):
        with tempfile.TemporaryDirectory() as scratch:
            archive = Path(scratch) / "release.zip"
            with zipfile.ZipFile(archive, "w") as z:
                z.writestr("hotd2re/hotd2.exe", ("C:" + "\\Users\\ExamplePerson\\source.cpp").encode())
                z.writestr("hotd2re/hotd2re_rejoin.bin", b"private runtime state")
                z.comment = b"ghp_" + b"x" * 36
            result = privacy.check_path(archive, [])
            self.assertFalse(result["passed"])
            self.assertEqual(len(result["findings"]), 3)

    def test_clean_package_keeps_public_attribution_and_game_data(self):
        with tempfile.TemporaryDirectory() as scratch:
            archive = Path(scratch) / "release.zip"
            with zipfile.ZipFile(archive, "w") as z:
                z.writestr("hotd2re/game/tex/example.bin", b"original game data")
                z.writestr("hotd2re/LICENSE.txt", b"Copyright contributors; SDL")
                z.writestr("hotd2re/hotd2re_settings.ini", b"music=7\ninput=mouse\n")
            self.assertTrue(privacy.check_path(archive, ["ExamplePerson"])["passed"])

    def test_vendor_path_exception_is_exact_name_and_hash_only(self):
        data = b"vendor fixture /home/vendor-training/model/checkpoint.pth\0"
        key = ("known-vendor.dll", hashlib.sha256(data).hexdigest())
        with tempfile.TemporaryDirectory() as scratch, patch.dict(privacy.VENDOR_BUILD_PATH_BINARIES, {key: "synthetic vendor fixture"}, clear=True):
            binary = Path(scratch) / key[0]
            binary.write_bytes(data)
            result = privacy.check_path(binary, [])
            self.assertTrue(result["passed"])
            self.assertEqual(result["vendor_build_path_exceptions"][0]["sha256"], key[1])
            self.assertFalse(privacy.check_path(binary, ["vendor-training"])["passed"])
            changed = bytearray(data)
            changed[-1] = 1
            binary.write_bytes(changed)
            self.assertFalse(privacy.check_path(binary, [])["passed"])
            renamed = Path(scratch) / "renamed.dll"
            renamed.write_bytes(data)
            self.assertFalse(privacy.check_path(renamed, [])["passed"])

    def test_vendor_exception_keeps_credential_name_comment_and_identifier_checks(self):
        data = b"/home/vendor/model.pth\0ghp_" + b"x" * 36
        key = ("known-vendor.dll", hashlib.sha256(data).hexdigest())
        with tempfile.TemporaryDirectory() as scratch, patch.dict(privacy.VENDOR_BUILD_PATH_BINARIES, {key: "synthetic vendor fixture"}, clear=True):
            binary = Path(scratch) / key[0]
            binary.write_bytes(data)
            result = privacy.check_path(binary, [])
            self.assertFalse(result["passed"])
            self.assertEqual(result["findings"][0]["reasons"], ["credential pattern"])
            self.assertIn("forbidden identifier", privacy.check_path(binary, ["vendor"])["findings"][0]["reasons"])
            archive = Path(scratch) / "release.zip"
            with zipfile.ZipFile(archive, "w") as z:
                z.writestr("hotd2re/known-vendor.dll", data)
                z.comment = b"/home/local-user/private.txt"
            self.assertFalse(privacy.check_path(archive, [])["passed"])

    def test_vendor_zip_exception_is_reported_and_not_applied_to_member_names(self):
        data = b"/home/vendor/model.pth\0"
        key = ("known-vendor.dll", hashlib.sha256(data).hexdigest())
        with tempfile.TemporaryDirectory() as scratch, patch.dict(privacy.VENDOR_BUILD_PATH_BINARIES, {key: "synthetic vendor fixture"}, clear=True):
            archive = Path(scratch) / "release.zip"
            with zipfile.ZipFile(archive, "w") as z:
                z.writestr("hotd2re/known-vendor.dll", data)
            result = privacy.check_path(archive, [])
            self.assertTrue(result["passed"])
            self.assertEqual(len(result["vendor_build_path_exceptions"]), 1)
            with zipfile.ZipFile(archive, "w") as z:
                z.writestr("/home/local-user/known-vendor.dll", data)
            self.assertFalse(privacy.check_path(archive, [])["passed"])

    def test_exact_vendor_hash_allows_only_opaque_base64_identifier_collision(self):
        identifier = "ExamplePerson"
        encoded = b"A" * 1100 + identifier.encode() + b"B" * 1100
        for suffix, accepted in ((b"", True), (b"\0" + identifier.encode() + b"\0", False),
                                 (b"\0/home/" + identifier.encode() + b"/source.cpp\0", False),
                                 (b"\0short" + identifier.encode() + b"word\0", False),
                                 (b"\0" + identifier.encode("utf-16le"), False),
                                 (b"\0ghp_" + b"x" * 36, False)):
            data = encoded + suffix
            key = ("known-vendor.dll", hashlib.sha256(data).hexdigest())
            with tempfile.TemporaryDirectory() as scratch, patch.dict(privacy.VENDOR_BUILD_PATH_BINARIES, {key: "synthetic vendor fixture"}, clear=True):
                binary = Path(scratch) / key[0]
                binary.write_bytes(data)
                self.assertEqual(privacy.check_path(binary, [identifier])["passed"], accepted)
                if accepted:
                    binary.write_bytes(data[:-1] + b"C")
                    self.assertFalse(privacy.check_path(binary, [identifier])["passed"])
                    renamed = Path(scratch) / "renamed.dll"
                    renamed.write_bytes(data)
                    self.assertFalse(privacy.check_path(renamed, [identifier])["passed"])
                    self.assertIn("forbidden identifier", privacy.check_bytes(data, [identifier]))

    def test_vendor_encoded_exception_requires_length_and_no_path_or_word_boundary(self):
        identifier = b"ExamplePerson"
        for data in (b"A" * 500 + identifier + b"B" * 500,
                     b"A" * 1100 + b"/" + identifier + b"/" + b"B" * 1100,
                     b"A" * 1100 + b"+" + identifier + b"=" + b"B" * 1100):
            key = ("known-vendor.dll", hashlib.sha256(data).hexdigest())
            with tempfile.TemporaryDirectory() as scratch, patch.dict(privacy.VENDOR_BUILD_PATH_BINARIES, {key: "synthetic vendor fixture"}, clear=True):
                binary = Path(scratch) / key[0]
                binary.write_bytes(data)
                self.assertFalse(privacy.check_path(binary, [identifier.decode()])["passed"])


if __name__ == "__main__":
    unittest.main()
