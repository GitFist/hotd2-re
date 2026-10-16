#include "palette_bank.h"

namespace game {

namespace {
int Table(const ExeImage& exe, uint32_t table, int i) {
    return exe.S16(table + static_cast<uint32_t>(i) * 2);
}
}  // namespace

// 0x0041C9E0
int GetPaletteBank(const ExeImage& exe, int file, int i) {
    switch (file) {
    case 0x147: return Table(exe, 0x0057A524, i);  // scr_common.bin
    case 0x14E: return Table(exe, 0x0057A464, i);
    case 0x169: return Table(exe, 0x0057A43C, i);
    case 0x16D: return Table(exe, 0x0057A38C, i);
    case 0x170: return Table(exe, 0x0057A490, i);
    case 0x171: return Table(exe, 0x0057A4DC, i);
    case 0x173: return Table(exe, 0x0057A4CC, i);
    case 0x184: return Table(exe, 0x0057A4F8, i);

    case 0x156:
        return 0x14;
    case 0x168:
        return (i < 0x2B || i > 0x38) ? 0 : 0x2F;
    case 0x176:
        return i == 0x10 ? 0x31 : 0;
    case 0x177:
    case 0x186: case 0x187: case 0x188: case 0x189: case 0x18A: case 0x18B:
        return 10;
    case 0x179:
    case 0x17D: case 0x17E: case 0x17F: case 0x180: case 0x181: case 0x182: case 0x183:
        return 1;
    case 0x17A:
        return 0x21;
    case 0x17C:
        return 0x1A;
    case 0x185:
        return i > 0x6D ? 0x0D : 0x2B;
    case 0x18C: case 0x18D: case 0x18E: case 0x18F: case 0x190: case 0x191:
        return 0x0B;
    case 0x1B5:
        return 0x1B;
    case 0x1B8:
        return 0x2E;
    case 0x1B9:
        return 0x2D;
    case 0x192:
        // Range split by index; the negative branch is kept as decompiled.
        if (i < 0) return 0x2D;
        if (i < 0x4B) return 0x1C;
        if (i < 0x96) return 0x1D;
        if (i < 0xE1) return 0x1E;
        return i > 299 ? 0x20 : 0x1F;
    default:
        if (file >= 0x193 && file <= 0x1B4) return 0x14;
        return 0;
    }
}

}  // namespace game
