#pragma once
#include "game/world.h"
#include "game/typing_drill.h"
#include "game/world_typing_drill_name.h"
#include "game/world_typing_drill_result_visuals.h"

namespace game {
struct TypingDrillUi {
    TypingDrillController controller;
    TypingDrillNameUi nameUi;
    TypingDrillBookUi bookUi;
    std::unique_ptr<World> game;
    std::string error;
    bool cancel=false,pauseRequest=false;
    std::vector<WorldDraw> heldDraws;
    std::vector<SpriteDef> heldSprites;
    int view = 0, selected = 0, resultRow = 0, record = 0;
    int visualSelected = -1, visualPrevious = -1;
    uint64_t visualChangedFrame = 0;
    int visualView = -1, visualAge = 0;
    int cursorHeldDirection=0,cursorHeldTicks=0;
    bool visualDescription=false;
    std::array<float,3> visualAnchor{0,0,-30};
    bool visualAnchorValid = false;
    std::string characters;
};
void TypingDrillMenuFrame(World::Impl& world);
bool QueueTypingDrillText(World::Impl& world, const std::string& text, bool cancel);
bool TypingDrillInputActive(const World::Impl& world);
void DrawTypingDrillPrompt(World::Impl& world, std::string_view text, size_t progress);
} // namespace game
