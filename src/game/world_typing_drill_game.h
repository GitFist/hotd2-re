#pragma once

#include "game/world.h"
#include "game/typing_drill.h"
#include "game/typing_mode.h"

#include <array>
#include <cstdint>
#include <set>

namespace game {

struct SpawnTask;
struct Task;
struct ActorTask;

enum class TypingDrillGamePhase { Off, Instructions, Ready, Running, Ended };
enum class TypingDrillEndReason { None, CourseComplete, TimeUp, Damage, Mistype, LowRank, MissedTarget, ScriptFailure };

// Source actor6D's live exercise state. The event VM and native actors retain
// ownership of targets, movement, visibility, attacks and word assignments.
struct TypingDrillGameState {
    bool active=false,finished=false,failed=false,resultsReady=false;
    TypingDrillGamePhase phase=TypingDrillGamePhase::Off;
    TypingDrillEndReason endReason=TypingDrillEndReason::None;
    int exercise=0,sourceScene=25,readyTicks=0,elapsedTicks=0,penaltyTicks=0;
    int completedWords=0,damageCount=0,mistypeCount=0,barrelsPassed=0,objectHits=0;
    int lastObjectHits=0,lastEnemies=0,perfectPoints=0;
    int courseCount=0,reflexWaves=0;
    std::array<unsigned,5> rankCounts{};
    uint32_t supervisorSerial=0,endScript=0;
    int endVariant=0,runStartFrame=0;
    uint64_t startingCorrect=0,startingErrors=0;
    std::set<uint32_t> mistypedTargets;
    // Freeze the source result counters when the lesson ends; the authored
    // cleanup camera and Finish interval continue before the book opens.
    TypingDrillResult outcome;
};

void ConfigureTypingDrillGame(World::Impl& world,int exercise);
bool SpawnTypingDrillGame(World::Impl& world,Task& task,SpawnTask& spawn);
bool TypingDrillGameActive(const World::Impl& world);
bool TypingDrillGameRunning(const World::Impl& world);
bool TypingDrillGameFinished(const World::Impl& world);
bool TypingDrillGameResultsReady(const World::Impl& world);
// Called before the word-target snapshot, including while Ready is displayed.
void TypingDrillGameBeforeTargets(World::Impl& world);
void TypingDrillGameWordComplete(World::Impl& world,const TypingCompletion& completion);
void TypingDrillGameMistype(World::Impl& world,uint32_t serial);
void TypingDrillGameCivilianHit(World::Impl& world);
// Call after the native accepted hit decrements lives, preserving its normal
// hit feedback and invulnerability period while the drill owns termination.
void TypingDrillGameDamage(World::Impl& world,int player);
void TypingDrillGameTypedKey(World::Impl& world,unsigned char expected,bool error);
// Source4013C6: each eligible post polls its expected character once per
// active input frame, including frames without a keyboard event.
void TypingDrillGameWordPoll(World::Impl& world);
TypingDrillResult TypingDrillGameSnapshotResult(const World::Impl& world);

} // namespace game
