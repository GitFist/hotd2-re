#pragma once
#include <array>
#include <string>
#include <string_view>
#include "game/typing_profile.h"
#include "game/world.h"

namespace game {
struct TypingScoreUi {
    enum Phase { Profile, Opening, Reading, Closing, Done } phase=Opening;
    TypingScoreRecord record;
    std::array<std::string,4> fields;
    std::string text;
    bool original=false, back=false;
    int field=0, age=0, page=0, turn=0;
};
void StartTypingScoreUi(World::Impl& world,TypingScoreUi& ui,const TypingFinalEvaluation& evaluation);
void TypingScoreUiFrame(World::Impl& world,TypingScoreUi& ui);
bool QueueTypingScoreUiText(TypingScoreUi& ui,std::string_view text);
}
