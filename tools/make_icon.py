"""Writes the .ico for hotd2.exe.

Usage: python tools/make_icon.py <out.ico> [--from-exe game/Hod2.exe]

With --from-exe and the file present, the icon group of that exe (RT_GROUP_ICON + its RT_ICON images) is rebuilt as an
.ico. The output goes to the build folder only, never into git: the original's icon is not ours to distribute.
Otherwise our own icon is drawn: a red crosshair on dark grey, 16..256 px.
"""
import argparse
import io
import os
import struct
import sys


def icon_from_exe(exe):
    import pefile

    pe = pefile.PE(exe)
    groups, images = [], {}
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        for name in t.directory.entries:
            for lang in name.directory.entries:
                d = lang.data.struct
                data = pe.get_data(d.OffsetToData, d.Size)
                if t.struct.Id == pefile.RESOURCE_TYPE['RT_GROUP_ICON']:
                    groups.append(data)
                elif t.struct.Id == pefile.RESOURCE_TYPE['RT_ICON']:
                    images[name.struct.Id] = data
    if not groups:
        return None
    grp = groups[0]
    _, typ, count = struct.unpack_from('<HHH', grp, 0)
    entries = []
    for i in range(count):
        w, h, colors, _, planes, bits, size, rid = struct.unpack_from('<BBBBHHIH', grp, 6 + i * 14)
        if rid in images:
            entries.append((w, h, colors, planes, bits, images[rid]))
    if not entries:
        return None
    out = io.BytesIO()
    out.write(struct.pack('<HHH', 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    for w, h, colors, planes, bits, data in entries:
        out.write(struct.pack('<BBBBHHII', w, h, colors, 0, planes, bits, len(data), offset))
        offset += len(data)
    for e in entries:
        out.write(e[5])
    return out.getvalue()


def own_icon():
    from PIL import Image, ImageDraw

    s = 256
    img = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((8, 8, s - 8, s - 8), radius=40, fill=(32, 28, 30, 255))
    red = (210, 24, 24, 255)
    c, r = s // 2, 78
    d.ellipse((c - r, c - r, c + r, c + r), outline=red, width=16)
    d.ellipse((c - 14, c - 14, c + 14, c + 14), fill=red)
    for a, b in (((c, 30), (c, c - 40)), ((c, c + 40), (c, s - 30)), ((30, c), (c - 40, c)), ((c + 40, c), (s - 30, c))):
        d.line((a, b), fill=red, width=16)
    out = io.BytesIO()
    img.save(out, format='ICO', sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    return out.getvalue()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--from-exe')
    a = ap.parse_args()
    data = None
    if a.from_exe and os.path.exists(a.from_exe):
        try:
            data = icon_from_exe(a.from_exe)
        except Exception as e:  # pefile missing or no icon: fall back to our own
            print(f'make_icon: {e}; using the own icon', file=sys.stderr)
    src = 'exe' if data else 'own'
    if not data:
        data = own_icon()
    with open(a.out, 'wb') as f:
        f.write(data)
    print(f'make_icon: {a.out} ({src}, {len(data)} bytes)')


if __name__ == '__main__':
    main()
