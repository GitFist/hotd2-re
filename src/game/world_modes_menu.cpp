// game::World - the mode select's other entries (0x00496960 entries 1..6), the persistent data, the menu-mode dispatch
// (Game_UpdateMode 0x004608A0 cases 8 / 12 / 15). notes/agents/modes.md.

#include <cstring>
#include <filesystem>

#include "game/world_modes.h"
#include "game/menu_audio.h"

namespace game {

// ---------------------------------------------------------------------------------------------------------------------
// Persistent data: Game_Load 0x004A0B60 (+ 0x0041C490(1) tables, 0x0040AB50 settings / grades / items)
// ---------------------------------------------------------------------------------------------------------------------

void ModesState::Load(World::Impl& w) {
    std::string why;
    saveFromGame = false;
    savePath = w.cfg.savePath;
    if (!savePath.empty() && save.LoadNative(savePath)) {
        // the port's own copy
    } else {
        const std::string dir = w.cfg.saveDir.empty() ? w.gameDir : w.cfg.saveDir;
        saveFromGame = save.LoadOriginal(dir, w.exe, &why);  // read-only; defaults (0x00401060) when missing
        if (!saveFromGame) w.Note("save data: " + why + " (defaults 0x00401060)");
    }
    for (const auto& [va, bytes] : w.cfg.savePatch)  // test setup (WorldConfig::savePatch)
        for (size_t i = 0; i < bytes.size(); ++i) save.SetU8(va + static_cast<uint32_t>(i), bytes[i]);
    for (int t = 0; t < SaveData::kTables; ++t)  // 0x0041C490(1): save 0x009C97A0 -> the tables 0x0057A354[t]
        for (int i = 0; i < 10; ++i) tables[t][i] = save.Entry(t, i);
    for (int i = 0; i < 10; ++i) {  // 0x0040AB50
        trainGrades[i] = save.S8(SaveData::kTrainGrades + static_cast<uint32_t>(i));
        bossGrades[i] = save.S8(SaveData::kBossGrades + static_cast<uint32_t>(i));
    }
    for (int i = 0; i < 33; ++i) items[i] = save.S8(SaveData::kItems + static_cast<uint32_t>(i));
}

// Game_Save 0x004011F0: the callers copy their runtime values into the block first (0x00425F40 grades / items, the
// options screen its settings, the name entry its table): here every runtime copy is written back, then the port's file
void ModesState::Store(World::Impl& w) {
    if (RandomizerActive(w)) return;  // randomized rewards/records never replace Original-mode progress
    for (int t = 0; t < SaveData::kTables; ++t)  // 0x0041C5C0
        for (int i = 0; i < 10; ++i) save.SetEntry(t, i, tables[t][i]);
    for (int i = 0; i < 10; ++i) {
        save.SetU8(SaveData::kTrainGrades + static_cast<uint32_t>(i), static_cast<uint8_t>(trainGrades[i]));
        save.SetU8(SaveData::kBossGrades + static_cast<uint32_t>(i), static_cast<uint8_t>(bossGrades[i]));
    }
    for (int i = 0; i < 33; ++i) save.SetU8(SaveData::kItems + static_cast<uint32_t>(i), static_cast<uint8_t>(items[i]));
    if (!savePath.empty() && !save.SaveNative(savePath)) w.Note("save data: cannot write " + savePath);
    // 0x0040AB50 (end of Game_Save): lives 0x004D0EDC[0x009C9F21], difficulty 0x009C9F20; the credit setting is read
    // directly from 0x009C9F25 by 0x00496B70
    w.cfg.livesSetting = w.exe.S16(0x004D0EDC + 2 * static_cast<uint32_t>(save.U8(SaveData::kLives) % 5));
    w.cfg.difficulty = save.U8(SaveData::kDifficulty) % 5;
    const int c = save.S8(SaveData::kCredits);
    if (!w.cfg.portNetworkRoom) w.cfg.creditSetting = c < 0 ? -1 : c;
}

void ModesBoot(World::Impl& w) {
    w.modes = std::make_shared<ModesState>();
    w.modes->Load(w);
    if (w.cfg.applySaveSettings) {  // 0x0040AB50: lives 0x004D0EDC[0x009C9F21], difficulty 0x009C9F20; credits 0x009C9F25
        const SaveData& s = w.modes->save;
        w.cfg.livesSetting = w.exe.S16(0x004D0EDC + 2 * static_cast<uint32_t>(s.U8(SaveData::kLives) % 5));
        w.cfg.difficulty = s.U8(SaveData::kDifficulty) % 5;
        const int c = s.S8(SaveData::kCredits);
        // The host selected continues when creating the room; the shared save supplies
        // difficulty/lives, but must not replace that explicit room rule.
        if (!w.cfg.portNetworkRoom) w.cfg.creditSetting = c < 0 ? -1 : c;
    }
}

// 0x00496B70: credits a game type gets (original mode 6, training / boss 1, arcade the setting + 1 or free play -1)
int ModesCredits(World::Impl& w, int entry) {
    if (entry == 1) return 6;
    if (entry > 1 && entry < 4) return 1;
    return w.cfg.creditSetting == -1 ? -1 : w.cfg.creditSetting + 1;
}

// 0x00496960, a confirmed entry other than arcade / exit (the caller freed the screens: 0x0049ED70, 0x0049DFD0)
bool ModesMenuConfirm(World::Impl& w, int e) {
    if (e < 1 || e > 6) return false;
    if (e == 5 && w.cfg.portFeatures) {
        // Both menu themes open the port's complete tabbed settings directly.
        // Keep the original mode-12 handlers for exactness, without exposing a second options screen in the port.
        w.front.nightShiftDisplayPage = 0;
        ++w.front.nightShiftDisplayRequests;
        w.SoundCall(World::kOptionFeedback, kOptionSelectSound);
        return true;
    }
    ModesState& m = *w.modes;
    if (e <= 3) {
        // 0x009A21C0 = 1, credits 0x00406F60(0x00496B70(e)), 0x009CA08C = e, 0x009A2BBC = 0, sound 0x121A9; the player's
        // task (later in the list) sees the same trigger / start: Player_TryJoin -> Game_SetState(6)
        w.front.menuConfirmed = true;
        const int credits = ModesCredits(w, e);
        if (credits == -1) {
            w.cfg.freePlay = true;
            w.cfg.credits = 0;
        } else {
            w.cfg.freePlay = false;
            w.cfg.credits = credits;
        }
        w.front.gameType = e;
        m.a2bbc = 0;
        w.Sound(0x121A9);
        if (e == 2 || e == 3) w.front.cheatFlag = 0;  // 0x009C87FC
        return true;
    }
    // ranking 8 / options 12 / network 15: Game_SetState, sound 0x121A9
    static const int kMode[3] = { 8, 12, 15 };
    w.front.modeRequest = kMode[e - 4];
    w.Sound(0x121A9);
    return true;
}

// Game_UpdateMode 0x004608A0: the menu modes
bool ModesFrame(World::Impl& w) {
    switch (w.gameMode) {
    case 8:
        RankFrame(w);
        return true;
    case 12:
        OptionsFrame(w);
        return true;
    case 15:
        NetFrame(w);  // 0x0049F380: the lobby without a network (world_modes_net.cpp)
        return true;
    default:
        return false;
    }
}

}  // namespace game
