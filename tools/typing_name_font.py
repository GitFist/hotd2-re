"""Offline preparation of the English PC name-entry font (453160/453370).

The original 24x24 two-bit glyphs become one outlined 32x32 glyph each. Runtime
uses the prepared atlas; it neither loads font.bin nor reads the reference exe.
"""
import struct

FONT_FILE = 0x10300
FONT_TEXTURE = 0x70000000
FIRST_GLYPH = 0xF74


def prepare_name_font(source, root):
    font = (root / "word" / "font.bin").read_bytes()
    punctuation = bytes(source.image[0x5AFDD8-source.base:0x5AFDD8-source.base+26])
    punctuation_codes = bytes(source.image[0x5AFDF4-source.base:0x5AFDF4-source.base+52])
    dx = struct.unpack("<8b", source.image[0x4F13C4-source.base:0x4F13CC-source.base])
    dy = struct.unpack("<8b", source.image[0x4F13CC-source.base:0x4F13D4-source.base])
    atlas = [0] * (512 * 256)
    for character in range(32, 128):
        if 48 <= character <= 57:
            code = 0x824F + character - 48
        elif 65 <= character <= 90:
            code = 0x8260 + character - 65
        elif 97 <= character <= 122:
            code = 0x8281 + character - 97
        elif character in punctuation:
            index = punctuation.index(character)
            code = int.from_bytes(punctuation_codes[index*2:index*2+2], "big")
        else:
            continue
        index = source.i16(0x592878 + code*2)
        if index <= 0:
            continue
        start = 16 + index*32
        if start + 144 > len(font):
            raise ValueError(f"name glyph {character:02X} runs past font.bin")
        glyph = [0] * 1024
        for y in range(24):
            for x in range(24):
                value = (font[start+y*6+x//4] >> (6-2*(x%4))) & 3
                if value == 0:
                    continue
                # Source4533C4..EB uses the low five bits of55/AA/FF.
                gray = (0, 0x55 & 31, 0xAA & 31, 31)[value]
                pixel = 0x8000 | (gray << 10) | (gray << 5) | gray
                gx, gy = x+2, y+2
                glyph[gy*32+gx] = pixel
                for ox, oy in zip(dx, dy):
                    glyph[(gy+oy)*32+gx+ox] |= 0x8000
        column, row = (character-32)%16, (character-32)//16
        for y in range(32):
            at = (row*32+y)*512+column*32
            atlas[at:at+32] = glyph[y*32:y*32+32]
    raw = struct.pack("<"+"H"*len(atlas), *atlas)
    # A size-prefixed PRS stream containing literals only keeps the ordinary
    # texture resource decoder's file contract; the font is prepared once.
    packed = struct.pack("<I", len(raw)) + b"".join(b"\xff"+raw[i:i+8] for i in range(0,len(raw),8)) + b"\x02\x00\x00"
    descriptor = struct.pack("<hhIII",512,256,0x900,0,FONT_TEXTURE)
    resource = (1,FONT_FILE,"typing_name_font.bin",packed,(),descriptor,b"\0",b"")
    sprites = [(FONT_FILE,FONT_TEXTURE)] * 96
    return resource, sprites
