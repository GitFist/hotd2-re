#!/usr/bin/env python3
"""Package the optional 2x graphics only. Requires Python 3.10+ and Pillow."""
import argparse
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

from PIL import Image


INSTALL = """HOTD2-RE - OPTIONAL 2X GRAPHICS

Requires the complete Windows game download (Build R or later).
This is an add-on, not a standalone game. It contains 2x textures and menu
graphics only. It does not include 4x textures or upgraded audio.

INSTALL AND PLAY
1. Close the game. Extract hotd2re-graphics-2x.zip.
2. Copy its contents into the hotd2re folder containing hotd2.exe.
   Do not put them inside the game folder or inside another remaster folder.
3. Double-click Play with 2x Graphics.cmd to play with the upgrade.

The layout should be:
  hotd2re/
    hotd2.exe
    game/
    scenes/
    Play with 2x Graphics.cmd
    remaster-2x/
      tex/
      bmp/

The launcher selects the pack for that launch only. It does not replace your
settings or saves. Launch hotd2.exe normally to use your usual graphics
(original graphics if you have not configured another asset pack).

The 2x images have twice the original width and height; they do not change
the level geometry. To see more detail, you can increase Render Scale in
Pause > Display and use a larger window or fullscreen. This is optional;
the pack does not change your display settings automatically.

ONLINE PLAY
Graphics are local. Your friend can use the original graphics or this pack,
as long as you both use the same game build.

OPTIONAL: ENABLE FOR NORMAL LAUNCHES
With the game closed, open hotd2re_settings.ini in Notepad and add or replace
the assets line with:
assets=remaster-2x
Then hotd2.exe will use the pack too. Remove that line to disable it again.
A HOTD2_ASSET_DIR environment variable, if set, takes priority over the INI.

UNINSTALL
Close the game, delete remaster-2x and Play with 2x Graphics.cmd, and remove
assets=remaster-2x from the INI if you added it. Keep game, scenes, and saves.

CREDITS
Upscaled graphics are derived from The House of the Dead 2 assets, owned by
Sega. They are separate from the project's MIT-licensed original code.
Independent fan project: https://github.com/GitFist/hotd2-re
"""

LAUNCHER = r"""@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0hotd2.exe" (
  echo Put this launcher and remaster-2x beside hotd2.exe, then try again.
  pause
  exit /b 1
)
if not exist "%~dp0remaster-2x\tex\common\0.png" (
  echo The 2x graphics folder is missing. Extract the entire graphics ZIP here.
  pause
  exit /b 1
)
set "HOTD2_ASSET_DIR=%~dp0remaster-2x"
start "" /D "%~dp0" "%~dp0hotd2.exe"
endlocal
"""


def digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(block)
    return sha.hexdigest()


def write_text(path, text):
    path.write_bytes(text.replace("\n", "\r\n").encode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("remaster"))
    parser.add_argument("--out", type=Path, required=True,
                        help="New output directory (must not already exist)")
    args = parser.parse_args()
    source = args.source.resolve()
    out = args.out.resolve()
    manifest = json.loads((source / "manifest.json").read_text())
    inventory = json.loads((source / "inventory.json").read_text())
    textures = manifest["textures"]
    bmp_sizes = {x["file"]: (x["w"] * 2, x["h"] * 2)
                 for x in inventory["untabled_tex_files"] if x.get("kind") == "bmp"}
    expected = {v["x2"]["path"] for v in textures.values()}
    actual = {p.relative_to(source).as_posix() for p in (source / "tex2x").rglob("*.png")}
    if actual != expected:
        raise ValueError("2x texture directory does not match the manifest")
    out.mkdir(parents=True, exist_ok=False)
    staging = out / "contents"
    pack = staging / "remaster-2x"
    resized = []
    for number, (key, metadata) in enumerate(sorted(textures.items()), 1):
        relative = Path(metadata["x2"]["path"])
        if relative.is_absolute() or ".." in relative.parts or relative.parts[0] != "tex2x":
            raise ValueError(f"Invalid texture path: {relative}")
        src = source / relative
        with Image.open(src) as im:
            size = (metadata["source"]["w"] * 2, metadata["source"]["h"] * 2)
            if im.format != "PNG" or im.size != size:
                raise ValueError(f"Texture is not exactly 2x: {key}: {im.size}, expected {size}")
            im.verify()
        dest = pack / "tex" / relative.relative_to("tex2x")
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dest)
        if number % 2000 == 0:
            print(f"Validated and copied {number}/{len(textures)} textures", flush=True)
    bmp_files = sorted((source / "bmp").glob("*.png"))
    (pack / "bmp").mkdir(parents=True)
    for src in bmp_files:
        size = bmp_sizes[src.name.removesuffix(".png")]
        dest = pack / "bmp" / src.name
        with Image.open(src) as im:
            im.load()
            if im.size == size:
                shutil.copyfile(src, dest)
            elif im.size == (size[0] * 2, size[1] * 2):
                # Current modern menu tiles are 4x; ship only 2x copies.
                im.convert("RGBA").resize(size, Image.Resampling.LANCZOS).save(dest)
                resized.append(src.name)
            else:
                raise ValueError(f"Unexpected menu image size: {src}: {im.size}")
    write_text(staging / "INSTALL-2X-GRAPHICS.txt", INSTALL)
    write_text(staging / "Play with 2x Graphics.cmd", LAUNCHER)
    files = sorted(p for p in staging.rglob("*") if p.is_file())
    hashes = {p.relative_to(staging).as_posix(): digest(p) for p in files}
    sums = staging / "GRAPHICS-2X-SHA256SUMS.txt"
    write_text(sums, "".join(f"{value}  {key}\n" for key, value in hashes.items()))
    files.append(sums)
    archive = out / "hotd2re-graphics-2x.zip"
    print("Compressing graphics ZIP", flush=True)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for file in files:
            z.write(file, file.relative_to(staging).as_posix())
    print("Checking every archived file", flush=True)
    with zipfile.ZipFile(archive) as z:
        if len(z.namelist()) != len(files):
            raise ValueError("ZIP file count mismatch")
        for name, expected_hash in hashes.items():
            if hashlib.sha256(z.read(name)).hexdigest() != expected_hash:
                raise ValueError(f"ZIP content mismatch: {name}")
        if z.read(sums.name) != sums.read_bytes():
            raise ValueError("ZIP checksum manifest mismatch")
    sha = digest(archive)
    write_text(archive.with_suffix(".zip.sha256"), f"{sha}  {archive.name}\n")
    result = {"archive": archive.name, "size": archive.stat().st_size,
              "sha256": sha, "textures": len(textures), "menu_images": len(bmp_files),
              "menus_resized_from_4x_to_2x": resized, "zip_verified": True,
              "unpacked_bytes": sum(p.stat().st_size for p in files)}
    (out / "verification.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
