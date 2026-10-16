"""Build the port's main-menu background tiles from a gameplay screenshot.

The mode select's background is six BMP tiles (tex\\hod2title00..11.bin, a 640 x 512 picture of which 640 x 480 is
shown; tiles 10 / 11 show their left 128 px). The asset pack replaces them with bmp\\hod2title<nn>.bin.png of any size
(FrontScreens::Load), so this script composes a new picture at 4 x (2560 x 2048) and writes the six 1024 x 1024 tiles
into the pack:

  - the screenshot (a --no-hud render of the stage-1 opening, e.g. frame 2424 of build\\script_auto_st1.txt at
    2560 x 1920), with the renderer's black letterbox bands cropped and the 4:3 aspect kept;
  - a dark gradient down the left side behind the menu list (the list is at x 24..315, y 148..446) and a slight
    overall darkening, for legible text;
  - the title screen's logo art (sprite strips 0xAB1..0xAB5 = scr_title.bin descriptors 84..88, true alpha) scaled to the
    compact logo's place in the original picture (top left, ~205 px wide in 640 space), "Presented by" (0x8FE,
    scr_title 82), the SEGA mark (0xA5A, scr_etc_1 18) and the copyright line (0x231, scr_title 0) at the bottom right,
    where the original picture has them.

The pack's upscaled textures (remaster\\tex\\<stem>\\<index>.png, 4 x) are the sources for the logo and the marks. The
previous tiles are kept once in <pack>\\bmp_menu_backup. Everything written derives from the game's own assets and stays
in the gitignored pack directory.

  python tools/menu_background.py build\\shots_menubg\\full_2424.png [--pack remaster] [--out-preview out.png]
"""

import argparse
import os
import shutil
import sys

from PIL import Image, ImageChops

SCALE = 4
BG_W, BG_H = 640 * SCALE, 512 * SCALE
SCREEN_H = 480 * SCALE
TILES = [("00", 0, 0, 256), ("01", 256, 0, 256), ("02", 0, 256, 256), ("03", 256, 256, 256), ("10", 512, 0, 128), ("11", 512, 256, 128)]

# the title art strips, left to right, and the marks (pack texture PNGs, 4 x the original size)
LOGO_STRIPS = ["tex/scr_title/84.png", "tex/scr_title/85.png", "tex/scr_title/86.png", "tex/scr_title/87.png", "tex/scr_title/88.png"]
PRESENTED = "tex/scr_title/82.png"
SEGA = "tex/scr_etc_1/18.png"
COPYRIGHT = "tex/scr_title/0.png"

# placement in 640 x 480 space (the original picture's compact logo spans ~x 10..218, y 10..135; "Presented by" is at
# x >= 480, y >= 390, the SEGA mark under it, the copyright line x 410..630, y 446..460)
LOGO_BOX = (10, 8, 218, 140)          # the logo art is fitted into this box (aspect kept, top-left aligned)
PRESENTED_CENTER = (538, 399)
SEGA_CENTER = (538, 428)
COPYRIGHT_RIGHT_BOTTOM = (632, 462)
COPYRIGHT_WIDTH = 222

# legibility: overall multiplier and the left gradient (alpha at x = 0, fading to 0 at x = GRADIENT_END)
DARKEN = 0.88
GRADIENT_ALPHA = 0.55
GRADIENT_END = 400


def crop_letterbox(img, thresh=10):
    """Crop the near-black rows at the top and bottom of a render (the 3D viewport's bands)."""
    g = img.convert("L")
    w, h = g.size
    top, bottom = 0, h
    for y in range(h):
        if g.crop((0, y, w, y + 1)).getextrema()[1] > thresh:
            top = y
            break
    for y in range(h - 1, -1, -1):
        if g.crop((0, y, w, y + 1)).getextrema()[1] > thresh:
            bottom = y + 1
            break
    return img.crop((0, top, w, bottom)), top, h - bottom


def fit_aspect(img, aspect):
    """Centre-crop to the aspect ratio (width / height)."""
    w, h = img.size
    if w / h > aspect:
        nw = int(round(h * aspect))
        x0 = (w - nw) // 2
        return img.crop((x0, 0, x0 + nw, h))
    nh = int(round(w / aspect))
    y0 = (h - nh) // 2
    return img.crop((0, y0, w, y0 + nh))


def alpha_bbox(img):
    return img.split()[3].getbbox()


def load_rgba(pack, rel):
    p = os.path.join(pack, rel)
    if not os.path.exists(p):
        sys.exit("missing pack texture: %s (run tools/remaster.py first)" % p)
    return Image.open(p).convert("RGBA")


def compose_logo(pack):
    strips = [load_rgba(pack, s) for s in LOGO_STRIPS]
    w = sum(s.width for s in strips)
    art = Image.new("RGBA", (w, strips[0].height), (0, 0, 0, 0))
    x = 0
    for s in strips:
        art.alpha_composite(s, (x, 0))
        x += s.width
    return art.crop(alpha_bbox(art))


def paste_fit(canvas, img, box):
    """Scale img (aspect kept) to fit box (4 x space), top-left aligned."""
    bx0, by0, bx1, by1 = box
    sc = min((bx1 - bx0) / img.width, (by1 - by0) / img.height)
    size = (max(1, int(round(img.width * sc))), max(1, int(round(img.height * sc))))
    canvas.alpha_composite(img.resize(size, Image.LANCZOS), (bx0, by0))


def paste_centered(canvas, img, center, scale=1.0):
    if scale != 1.0:
        img = img.resize((max(1, int(round(img.width * scale))), max(1, int(round(img.height * scale)))), Image.LANCZOS)
    canvas.alpha_composite(img, (int(round(center[0] - img.width / 2)), int(round(center[1] - img.height / 2))))


def build(shot_path, pack):
    shot = Image.open(shot_path).convert("RGB")
    shot, top, bottom = crop_letterbox(shot)
    shot = fit_aspect(shot, 4.0 / 3.0).resize((BG_W, SCREEN_H), Image.LANCZOS)
    # legibility
    shot = Image.eval(shot, lambda v: int(v * DARKEN))
    grad = Image.new("L", (BG_W, 1))
    end = GRADIENT_END * SCALE
    grad.putdata([int(255 * GRADIENT_ALPHA * max(0.0, 1.0 - x / end) ** 1.5) for x in range(BG_W)])
    grad = grad.resize((BG_W, SCREEN_H))
    shot = Image.composite(Image.new("RGB", shot.size, (0, 0, 0)), shot, grad)

    canvas = Image.new("RGBA", (BG_W, BG_H), (0, 0, 0, 255))
    canvas.paste(shot, (0, 0))

    logo = compose_logo(pack)
    paste_fit(canvas, logo, tuple(v * SCALE for v in LOGO_BOX))

    presented = load_rgba(pack, PRESENTED)
    presented = presented.crop(alpha_bbox(presented))
    paste_centered(canvas, presented, tuple(v * SCALE for v in PRESENTED_CENTER))
    sega = load_rgba(pack, SEGA)
    sega = sega.crop(alpha_bbox(sega))
    paste_centered(canvas, sega, tuple(v * SCALE for v in SEGA_CENTER))
    copy = load_rgba(pack, COPYRIGHT)
    copy = copy.crop(alpha_bbox(copy))
    sc = COPYRIGHT_WIDTH * SCALE / copy.width
    copy = copy.resize((int(round(copy.width * sc)), int(round(copy.height * sc))), Image.LANCZOS)
    rx, by = (v * SCALE for v in COPYRIGHT_RIGHT_BOTTOM)
    canvas.alpha_composite(copy, (rx - copy.width, by - copy.height))
    return canvas.convert("RGB"), (top, bottom)


def write_tiles(canvas, pack):
    bmp = os.path.join(pack, "bmp")
    backup = os.path.join(pack, "bmp_menu_backup")
    os.makedirs(bmp, exist_ok=True)
    os.makedirs(backup, exist_ok=True)
    for name, tx, ty, tw in TILES:
        fn = "hod2title%s.bin.png" % name
        dst = os.path.join(bmp, fn)
        bak = os.path.join(backup, fn)
        if os.path.exists(dst) and not os.path.exists(bak):
            shutil.copy2(dst, bak)
        tile = Image.new("RGB", (256 * SCALE, 256 * SCALE), (0, 0, 0))
        tile.paste(canvas.crop((tx * SCALE, ty * SCALE, (tx + tw) * SCALE, (ty + 256) * SCALE)), (0, 0))
        tile.save(dst, optimize=True)
        print("wrote", dst)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("screenshot")
    ap.add_argument("--pack", default="remaster")
    ap.add_argument("--out-preview", help="also save the composed 640 x 480 picture (half size) here")
    ap.add_argument("--no-write", action="store_true", help="preview only, leave the pack alone")
    a = ap.parse_args()
    canvas, bands = build(a.screenshot, a.pack)
    print("letterbox rows cropped: top %d bottom %d" % bands)
    if a.out_preview:
        canvas.crop((0, 0, BG_W, SCREEN_H)).resize((1280, 960), Image.LANCZOS).save(a.out_preview)
        print("preview", a.out_preview)
    if not a.no_write:
        write_tiles(canvas, a.pack)


if __name__ == "__main__":
    main()
