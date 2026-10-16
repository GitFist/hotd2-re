// bank_test <Hod2.exe> <banks_emu.txt>
// Compares game::GetPaletteBank with the original 0x0041C9E0 as executed by tools/dump_banks.py.

#include <cstdio>
#include <fstream>

#include "game/exe_image.h"
#include "game/palette_bank.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: bank_test <Hod2.exe> <banks_emu.txt>\n");
        return 2;
    }
    game::ExeImage exe;
    if (!exe.Load(argv[1])) {
        fprintf(stderr, "cannot load %s\n", argv[1]);
        return 2;
    }
    std::ifstream in(argv[2]);
    int file, idx, want, checked = 0, bad = 0;
    while (in >> file >> idx >> want) {
        const int got = game::GetPaletteBank(exe, file, idx);
        ++checked;
        if (got != want) {
            if (++bad <= 10) printf("MISMATCH file 0x%X index %d: ours %d, original %d\n", file, idx, got, want);
        }
    }
    printf("palette bank pairs checked: %d, mismatches: %d\n", checked, bad);
    return bad == 0 && checked > 0 ? 0 : 1;
}
