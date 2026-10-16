"""Regression checks for the release privacy gate; fixtures are synthetic."""
import importlib.util
import tempfile
import unittest
import zipfile
from pathlib import Path

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


if __name__ == "__main__":
    unittest.main()
