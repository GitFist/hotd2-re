#pragma once
#include "game/world.h"
#include <string_view>

namespace game {
void StartTypingReport(World::Impl& world, bool cleared);
void TypingReportFrame(World::Impl& world);
bool TypingReportActive(World::Impl& world);
bool TypingReportNameActive(World::Impl& world);
bool QueueTypingReportText(World::Impl& world,std::string_view text);
}
