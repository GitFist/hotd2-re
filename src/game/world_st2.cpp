// game::World - later stages of a game (stage 2 and on): the mid-game stage start and the stage-2 objects.
// notes/agents/stage2.md describes the stage boundary, the carried state and how it was verified against
// build\scene_capture_auto_st2.txt.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/world_internal.h"

namespace game {

namespace {
constexpr uint32_t kPlayerStateFn = 0x00579CD0;  // task function per player state (row stride 0x14)
}  // namespace

// The stage switch of game mode 6: the stage-end sub-states 5 / 6 (0x00460390 / 0x004603B0) count 0x007DCCCC down,
// then 0x009A1A08 += 1, Evt_LoadTable 0x00413160 (st<n>evtbl.bin), sub-state 1. Sub-state 1 = 0x0045FFF0:
//   every player in state 5 -> state 2, Stage_Start 0x00460030(&0x007DCCD0, 0), sub-state 2.
// Unlike sub-state 0 (0x0045FEF0) it does NOT run the rank init 0x00460770 (rank 0x009C8E96, rank timer 0x009C8A7C /
// 0x009A2C30, 0x009A3794), the route record clear (0x009A5920), the 0x009C89C0.. / 0x009C8EC0.. resets, nor the
// game-mode switch that clears 0x009C8E80 / 0x009C8E84 (Game_ApplyState 0x0040E860). Stage_Start itself is the same
// (srand from 0x009A32A0, Mem_Init, the stage tasks, Stage_ResetState: per player +0x22 / +0x24 / +0x26 = 0).
// StartStage ran the common part; this restores what sub-state 1 keeps.
void World::Impl::ApplyCarry() {
    const WorldCarry& c = cfg.carry;
    for (int p = 0; p < 2; ++p) {
        Player& P = player[p];
        const WorldCarry::Player& q = c.player[p];
        int state = q.state;
        if (state == 5) state = 2;  // 0x0045FFF0
        P.state = static_cast<int16_t>(state);
        P.lives = static_cast<int16_t>(q.lives);
        P.livesShown = static_cast<int16_t>(q.livesShown);
        P.score = q.score;
        P.credit0 = q.credit[0];
        P.credit1 = q.credit[1];
        P.credit2 = q.credit[2];
        P.credit3 = q.credit[3];
        P.nextState = static_cast<int16_t>(q.nextState);
        P.flags12C = q.flags12C;
        P.aim[0] = q.aim[0];
        P.aim[1] = q.aim[1];
        P.ammo = static_cast<int16_t>(q.ammo);
        P.empty = static_cast<int16_t>(q.empty);
        P.reloadBlink = static_cast<int16_t>(q.reloadBlink);
        P.outTimer = q.outTimer;
        P.contTimer = q.contTimer;
        P.crosshair[0] = q.crosshair[0];
        P.crosshair[1] = q.crosshair[1];
        if (q.hitTimer >= 0) P.hitTimer = q.hitTimer;
        // streak / shots / hits (+0x22..+0x26): Stage_ResetState clears them (Player{} already did)
        if (P.task && P.task->data) P.task->As<PlayerTask>().fn = exe.U32(kPlayerStateFn + 0x14 * static_cast<uint32_t>(state));
    }
    rank = c.rank;
    rankTimer = c.rankTimer;
    rankTimerOn = c.rankTimerOn;
    rankAdj = 0;  // 0x009A3794 is consumed by every Game_UpdateRank
    players = c.players;
    rankPrevPlayers = c.rankPrevPlayers;
    route = c.route;
    if (c.haveFlags5900) flags5900 = c.flags5900;
    if (c.haveRouteRec) memcpy(routeRec, c.routeRec, sizeof(routeRec));
    if (c.haveKomono40) memcpy(komono40Intact, c.komono40, sizeof(komono40Intact));
    if (c.haveCivilians) {  // stage 6 (notes/agents/stage6.md)
        rescueTotal = c.rescueTotal;
        civTotal = c.civTotal;
        memcpy(rescueCount, c.rescueCount, sizeof(rescueCount));
        memcpy(rescueKinds, c.rescueKinds, sizeof(rescueKinds));
        memcpy(civCount, c.civCount, sizeof(civCount));
    }
    if (c.haveTracers)
        for (int p = 0; p < 2; ++p)
            for (int i = 0; i < 6; ++i) player[p].tracer[i].roll = c.tracerRoll[p][i];
}


// The carried state taken from the world itself (the in-run counterpart of world_test --stage-start, which reads it
// from a capture): everything ApplyCarry restores, as it stands before the Stage_Start frame.
WorldCarry World::Impl::MakeCarry() const {
    WorldCarry c;
    c.valid = true;
    for (int p = 0; p < 2; ++p) {
        const Player& P = player[p];
        WorldCarry::Player& q = c.player[p];
        q.state = P.state;
        q.lives = P.lives;
        q.livesShown = P.livesShown;
        q.score = P.score;
        q.credit[0] = P.credit0;
        q.credit[1] = P.credit1;
        q.credit[2] = P.credit2;
        q.credit[3] = P.credit3;
        q.nextState = P.nextState;
        q.flags12C = P.flags12C;
        q.aim[0] = P.aim[0];
        q.aim[1] = P.aim[1];
        q.ammo = P.ammo;
        q.empty = P.empty;
        q.reloadBlink = P.reloadBlink;
        q.outTimer = P.outTimer;
        q.contTimer = P.contTimer;
        q.crosshair[0] = P.crosshair[0];
        q.crosshair[1] = P.crosshair[1];
        q.hitTimer = P.hitTimer;
        for (int i = 0; i < 6; ++i) c.tracerRoll[p][i] = P.tracer[i].roll;
    }
    c.rank = rank;
    c.rankTimer = rankTimer;
    c.rankTimerOn = rankTimerOn;
    c.players = players;
    c.rankPrevPlayers = rankPrevPlayers;
    c.route = route;
    c.haveFlags5900 = true;
    c.flags5900 = flags5900;
    c.haveRouteRec = true;
    memcpy(c.routeRec, routeRec, sizeof(c.routeRec));
    c.haveTracers = true;
    c.haveKomono40 = true;
    memcpy(c.komono40, komono40Intact, sizeof(c.komono40));
    c.haveCivilians = true;
    c.rescueTotal = rescueTotal;
    c.civTotal = civTotal;
    memcpy(c.rescueCount, rescueCount, sizeof(c.rescueCount));
    memcpy(c.rescueKinds, rescueKinds, sizeof(c.rescueKinds));
    memcpy(c.civCount, civCount, sizeof(c.civCount));
    return c;
}

// Game mode 6 sub-state 1 for a later stage (0x0045FFF0): the stage-end sub-state 6 set the next event mode
// (0x009A1A08 + Evt_LoadTable 0x00413160); this frame runs Stage_Start 0x00460030 with the carried state.
void World::Impl::ChainStage() {
    WorldConfig c = cfg;
    c.eventMode = nextEventMode;
    // 0x009A2BC0 as the stage end left it (op 0x4F past the last scene: 0x00597890[mode] row scene - 6 bytes; 0 after
    // stage 1, 7 after stage 2)
    c.scene = runner ? runner->scene : 0;
    c.startFrame = frame;
    c.seedCounter = seedCounter;  // already counted for this frame
    c.firstSerial = ts.NextSerial();
    c.carry = MakeCarry();
    for (int p = 0; p < 2; ++p) c.playerState[p] = player[p].state;
    nextEventMode = -1;
    std::string err;
    if (!LoadStageTable(c.eventMode, &err)) {
        Note("next stage: " + err);
        return;
    }
    StartStage(c);
}
}  // namespace game
