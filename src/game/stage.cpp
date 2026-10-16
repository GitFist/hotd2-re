#include "stage.h"

#include <algorithm>

namespace game {

bool StageSections::Load(const ExeImage& exe, int stage, bool alt) {
    sections.clear();
    objects.clear();
    const uint32_t a = exe.U32((alt ? 0x00576A8C : 0x00576A2C) + stage * 4);
    const uint32_t b = exe.U32((alt ? 0x00576ABC : 0x00576A5C) + stage * 4);
    if (!exe.InData(a) || !exe.InData(b)) return false;
    int maxObj = -1;
    // The table has no count; read until two consecutive empty sections after the first, or 64 sections.
    for (int s = 0; s < 64; ++s) {
        std::vector<int> list;
        bool terminated = false;
        for (int i = 0; i < 12; ++i) {
            const int v = exe.S16(a + s * 0x18 + i * 2);
            if (v == -1) {
                terminated = true;
                break;
            }
            list.push_back(v);
        }
        if (!terminated) break;
        if (s > 0 && list.empty() && sections.back().empty()) {
            sections.pop_back();
            break;
        }
        for (int o : list) maxObj = std::max(maxObj, o);
        sections.push_back(std::move(list));
    }
    for (int o = 0; o <= maxObj; ++o) objects.emplace_back(exe.S16(b + o * 4), exe.S16(b + o * 4 + 2));
    return !sections.empty();
}

std::vector<int> StageSections::SectionModels(int section) const {
    std::vector<int> out;
    if (section < 0 || section >= static_cast<int>(sections.size())) return out;
    for (int o : sections[section])
        if (o >= 0 && o < static_cast<int>(objects.size())) out.push_back(objects[o].first);
    return out;
}

}  // namespace game
