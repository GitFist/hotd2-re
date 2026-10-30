#pragma once
#include "game/world.h"

namespace game {
struct TypingDrillBookUi {
    int kind=-1,phase=0,ticks=0,confirmation=0;
    int recordPage=0,recordTurn=0;
    bool nameDone=false;
    float position=0,alpha=0;
};
enum class TypingDrillBookAction { None, Name, Supplementary, Select };
void BeginTypingDrillResults(World::Impl& world);
void BeginTypingDrillRecords(World::Impl& world);
TypingDrillBookAction TypingDrillResultsFrame(World::Impl& world,uint32_t buttons);
TypingDrillBookAction TypingDrillRecordsFrame(World::Impl& world,uint32_t buttons);
bool TypingDrillResultsVisuals(World::Impl& world);
bool TypingDrillRecordsVisuals(World::Impl& world);
void DrawTypingDrillInstructionCriteria(World::Impl& world,int exercise);
} // namespace game
