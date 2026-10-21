// game::World - the end of a stage in the game types 2 (training) and 3 (boss mode): game mode 6 sub-state 6
// (0x004603B0) when its counter runs out. notes/agents/modes.md (section 7); verified against
// build\scene_capture_modes_boss0.txt (stage end 2804, name entry 2805..6523, the select again from 6524).

#include "game/world_modes.h"
#include "game/world_nightshift.h"

namespace game {

// 0x004603B0, counter 0x007DCCCC at 0, game types 2 / 3 (false: another game type, the caller goes on). Sets the next
// event mode / scene (0x009A1A08 / 0x009A2BC0, Evt_LoadTable 0x00413160 + coli 0x0048A3B0 in ChainStage) with
// sub-state 1, or sub-state 9 (the name entry) for a qualifying boss time; 0x0049DFD0 (resources) in both cases.
bool ModesStageEnd(World::Impl& w) {
    const int gt = w.runner->cfg.gameType;
    if (gt != 2 && gt != 3) return false;
    if (gt == 2) {  // training: trnevtbl again (the scene as the lesson left 0x009A2BC0)
        w.nextEventMode = 6;
        w.subState = 1;
        return true;
    }
    ModesState& m = *w.modes;
    // The original fight timer has stopped and bossIndex still identifies the fight
    // just won. A rush records only after Emperor, using its accumulated timer.
    if(m.a2bbc==0||(m.a2bbc==1&&m.bossIndex==8))
        PortLeaderboardComplete(w,m.a2bbc==1?12:3+m.bossIndex,m.bossTime,m.bossDiff);
    if (m.a2bbc == 0 || (m.a2bbc == 1 && m.bossIndex == 8)) {
        if (BossRankCheck(w, 0) != -1 || BossRankCheck(w, 1) != -1) {
            if (m.a2bbc == 1) ++m.bossIndex;
            w.subState = 9;
            return true;
        }
    }
    if (m.bossRecord == 1) m.Store(w);  // Game_Save 0x004011F0
    if (m.a2bbc == 1) {  // boss rush: the next boss (0x0055CBBC {stage, scene}; 9 = the select)
        ++m.bossIndex;
        const uint32_t e = 0x0055CBBC + static_cast<uint32_t>(m.bossIndex) * 4;
        w.nextEventMode = w.exe.S16(e);
        w.runner->scene = w.exe.S16(e + 2);
    } else {
        w.nextEventMode = 0;
        w.runner->scene = 0x10;  // the boss select
    }
    w.subState = 1;
    return true;
}

}  // namespace game
