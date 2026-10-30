#pragma once

#include <array>
#include <algorithm>
#include <utility>

#include "game/stage.h"
#include "game/typing_reference.h"

namespace game {
namespace typing_drill_data {
#include "game/typing_drill_stage_data.inc"
}

inline int TypingDrillSourceScene(int exercise) {
    // PC menu 0x5891F8, reordered to the native profile's category order.
    constexpr std::array<int,13> scenes={25,9,21,5,17,19,11,15,13,1,7,23,3};
    return scenes[size_t(std::clamp(exercise,0,12))];
}

// Mode 7's full section directory includes all final-exam transitions. The gun
// game's mode 6 directory ends earlier and cannot stand in for this PC table.
inline bool LoadTypingDrillSections(const TypingReference& reference,StageSections& target) {
    if(!reference.Available())return false;
    StageSections prepared;
    for(const auto& row:typing_drill_data::kTypingDrillSections) {
        std::vector<int> section;
        for(int object:row) {if(object==-1)break;section.push_back(object);}
        prepared.sections.push_back(std::move(section));
    }
    for(const auto& row:typing_drill_data::kTypingDrillObjects) {
        const int model=reference.SourceModel(row[0]);
        if(row[0] && model<=0)return false;
        prepared.objects.emplace_back(model,row[1]);
    }
    target=std::move(prepared);
    return true;
}
}
