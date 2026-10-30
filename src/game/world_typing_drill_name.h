#pragma once
#include "game/world.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace game {
struct TypingDrillNameUi {
    bool active=false,saveFailed=false;
    int confirmation=0; // Editing=0; Yes=1; No=2.
    std::string name,text;
};
void BeginTypingDrillName(World::Impl& world);
bool TypingDrillNameFrame(World::Impl& world,uint32_t buttons); // True after the accepted name is saved.
bool QueueTypingDrillName(World::Impl& world,std::string_view text);
bool TypingDrillNameActive(const World::Impl& world);
} // namespace game
