// stage_test <game dir> <capture with E and M lines>
// Checks game::StageSections against the ORIGINAL: after each event op 0x29 (switch section, stage 1 tables),
// every drawn level-chunk model (file st<N>_0*.bin) must belong to the selected section.

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "game/exe_image.h"
#include "game/stage.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: stage_test <game dir> <capture>\n");
        return 2;
    }
    game::ExeImage exe;
    if (!exe.Load(std::string(argv[1]) + "/Hod2.exe")) return 2;
    game::StageSections st;
    if (!st.Load(exe, 0)) return 2;
    auto isChunk = [&](int id) {
        uint16_t file = 0;
        exe.Read(0x004E83B4 + id * 2, &file, 2);
        const std::string n = exe.CStr(exe.U32(0x004D0EF4 + file * 4));
        return n.size() > 5 && n.compare(0, 2, "st") == 0 && n[3] == '_' && n[4] == '0';
    };
    std::ifstream cap(argv[2]);
    int section = -1, inSection = 0, outside = 0, switches = 0;
    std::set<int> current;
    for (std::string line; std::getline(cap, line);) {
        std::istringstream ss(line);
        char kind;
        ss >> kind;
        if (kind == 'E') {
            unsigned frame, op;
            std::string addr, a1;
            ss >> frame >> op >> addr >> a1;
            if (op == 0x29) {
                section = static_cast<int>(std::stoul(a1, nullptr, 16) & 0xFFFF);
                auto models = st.SectionModels(section);
                current = std::set<int>(models.begin(), models.end());
                ++switches;
            }
        } else if (kind == 'M' && section >= 0) {
            int id;
            ss >> id;
            if (id < 0 || !isChunk(id)) continue;
            if (current.count(id)) ++inSection;
            else if (++outside <= 5) printf("chunk %d drawn outside section %d\n", id, section);
        }
    }
    printf("stage 1 sections: %zu; section switches in capture: %d; chunk draws in selected section: %d, outside: %d\n",
           st.sections.size(), switches, inSection, outside);
    return switches > 0 && outside == 0 && inSection > 0 ? 0 : 1;
}
