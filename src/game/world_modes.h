#pragma once

// game::World - the game types (0x009CA08C: 1 original, 2 training, 3 boss) and the menu modes (8 ranking, 12 options,
// 15 network) of the mode select, the persistent data they read / write, the boss / training / item select screens and
// the name entry. notes/agents/modes.md is the map (entry -> mode / game type / functions / globals / data files) and
// the verification record. Files: world_modes_*.cpp.

#include <cstdint>
#include <memory>
#include <string>

#include "game/save_data.h"
#include "game/world_internal.h"

namespace game {

struct ModesState {
    // ---- persistent data (Game_Load 0x004A0B60 / 0x0040AB50 copies; save_data.h)
    SaveData save;
    bool saveFromGame = false;      // the original's files were decoded (else defaults / the port's file)
    std::string savePath;           // the port's save file ("" = none: nothing is written)
    RankEntry tables[12][10];       // 0x0057A354[t]: 0x009A3420 arcade, 0x009C7120 original, 0x009A32C0.. boss 0..9
    int8_t trainGrades[10] = {};    // 0x009C8D40
    int8_t bossGrades[10] = {};     // 0x009A2C10
    int8_t items[33] = {};          // 0x009C90C0 (original mode inventory)
    void Load(World::Impl& w);      // at boot (World::Boot)
    void Store(World::Impl& w);     // Game_Save 0x004011F0: runtime copies -> save block -> the port's file
    int a2bbc = 0;                  // 0x009A2BBC: training lesson scene / boss rush flag (mode select: 0)
    int netState = 0;               // 0x007DE560 (network mode 15: 0x0049F380)
    int netAimX = -1, netAimY = -1;  // port connected-room pointer movement
    int netSub = 0;                 // 0x007DE564 (step inside a state)
    int netCursor = 0;              // 0x007DE588 (lobby menu: 0 join, 1 create, 2 exit)

    // ---- mode 8: ranking (0x0041A5E0)
    struct RankRow {                // 0x008113A0 / 0x00811220 rows (0x24 bytes)
        float x = 0, y = 0, depth = 45.0f, vx = 0, targetX = 0, alpha = 1.0f;  // +0x00 .. +0x14
        int16_t delay = 0, r1A = 0, page = 0;                                  // +0x18 / +0x1A / +0x1C
        int table = 0, entry = 0;                                              // +0x20 entry pointer
    } rows[2][10];
    int8_t rankRepeat = 0;          // 0x007C216C (auto-repeat counter of a held L / R)
    int8_t rankRepeatDir = 0;       // 0x007C2174 (1 right, 2 left)
    bool rankAllPages = false;      // 0x007C216D (all eight boss grades > 0: pages 10 / 11 open)

    // ---- mode 12: options (0x004869E0)
    int8_t optCursor = 0;           // 0x009C8E20
    int8_t optDifficulty = 0, optLives = 0, optCredits = 0, optCross[2] = {}, optSound = 0;  // 0x009C8E22.. (copies)
    int16_t optBgm = 0;             // 0x009C8E32 sound test
    int8_t optVoice = 0;            // 0x009C8E34
    int16_t optRepeat = 0;          // 0x009C8E36 (sound tests: L / R held)
    int8_t optHidden = 0;           // 0x007DD030 (item 3 shown)
    int16_t optScroll = -1;         // 0x007DD028
    int optKind[2] = {};            // 0x007DD01C / 0x007DD020 (players' input kinds when the screen opened)
    // port only (play.exe, WorldConfig::portVolumes >= 0): the volume rows 11..13 (Music, Gun Shots, Sound Effects) of
    // the options screen, 0..10; off in the exactness tests
    bool optPortRows = false;
    int optVolume[3] = { 10, 10, 10 };
    int optLightgunRequests = 0;
    int optWiiRequests = 0;
    int optDisplayRequests = 0;     // port only: times the "Display Settings" row (14) was confirmed; play.exe opens its
                                    // display page (src/port/play/display_menu.h) over the screen when it grows
    int optState = 0;               // 0x009CA0F0 handler: 0 main list, 1 calibration (0x004882D0), 2 controller (0x00485F90)

    // ---- boss mode (game type 3)
    int bossIndex = 0;              // 0x009A2268 boss chosen (0..8; rush: the current one)
    int bossDiff = 0;               // 0x009C8FB0 difficulty chosen
    int bossRecord = 0;             // 0x009A34C0 (a grade / record improved: saved at the stage end)
    int bossTime = 0;               // 0x009C7060 fight time (frames)
    int timerStart = 0, timerBase = 0;  // 0x007DD174 / 0x007DD178 (the original: GetTickCount; here game frames)
    int clockOverride = -0x7FFFFFFF;    // World::SetWallClock: 0x0049DF20's value for this frame (tests)
    int Clock(int frame) const {        // 0x0049DF20
        return clockOverride != -0x7FFFFFFF ? clockOverride : (frame - timerStart) + timerBase;
    }
    struct Slot {                   // 0x007DC818 + k * 12 carousel entries
        float x = 0, a = 0;
        int16_t id = 0;
    } bossSlots[10];
    int16_t bossCur = 0, bossPrev = 0;   // 0x007DC89A / 0x007DC890
    int16_t bossAnim = 0, bossAnimCount = 0, bossSpin = 0;  // 0x007DC894 / 0x007DC892 / 0x007DC896
    int8_t bossRepeat = 0, bossRepeatDir = 0;  // 0x007DC810 / 0x007DC898

    // ---- training (game type 2): the lesson select carousel (0x007DD0C0..0x007DD14A), lesson / difficulty
    int lesson = 0;                 // 0x009C8FB1 lesson chosen
    int lessonDiff = 0;             // 0x009C9118 difficulty chosen (also the event runner's playerVariant)
    bool trainSave = false;         // 0x009C6FF4 (a grade / item changed: saved when the result is dismissed)
    Slot trainSlots[10];            // 0x007DD0C8 + k * 12
    int16_t trainCur = 0, trainPrev = 0;          // 0x007DD148 / 0x007DD144
    int16_t trainAnim = 0, trainAnimCount = 0;    // 0x007DD142 / 0x007DD146
    int16_t trainSpin = 0;                        // 0x007DD140
    int8_t trainRepeat = 0, trainRepeatDir = 0;   // 0x007DD0C0 / 0x007DD14A

    // ---- original mode (game type 1): the players' items 0x009A2240 + p * 0x14 (0x0048A0D0 / 0x00415FE0), the item
    // list of the select 0x007DD068, the global item flags 0x009C88A8..0x009C88AC
    struct PlayerItems {
        int8_t slot[2] = { -1, -1 };  // +0 / +1 the two items
        int8_t character = 0;         // +2 (0 / 1 the agents, 2..7 items 0x16..0x1B, 8 / 9 item 0x1C)
        int8_t b3 = 1;                // +3 (item 0x20: 2)
        int8_t lives = 3;             // +4 (items 0xE / 0xF)
        int8_t b5 = 5;                // +5 (item 0xF: 8)
        int8_t credits = 0;           // +6 continues added (items 0x10..0x13; -1 free play)
        int8_t weapon = 0;            // +7 weapon type (items 0..2: 1..3, 0xB: 0xC, 0xC: 0xD)
        uint8_t b8[4] = {};           // +8 ammo, +9 (pol requests 3 / 4 / 5), +0xA, +0xB (0x004EC928 rows)
        float damage = 1.0f;          // +0xC damage factor
        uint8_t b10[4] = {};          // +0x10 (0x00416420: 0x00579ED8[weapon * 8 + 0..3])
    } pitems[2];
    int8_t itemList[33] = {};       // 0x007DD068 (items in stock, -1 padded)
    uint8_t itemFlags[5] = {};      // 0x009C88A8..0x009C88AC (items 0x15, 0x1D, 0x1E, 0x1F, 0xC / 0x14)
    // Port-only Original-based run. A separate RNG makes encounter rolls independent of gunfire.
    struct Randomizer {
        bool active = false;
        uint32_t seed = 0, rng = 0;
        int chapter = -1, banner = 0;
        int slots[2][2] = {{-1, -1}, {-1, -1}};
        int encounters = 0, extraEnemies = 0;
        // Derived from immutable game tables at run start; cosmetic choices do not consume rng.
        std::vector<int> voiceLines;
        uint64_t reportedCostumes = 0;  // diagnostics only, excluded from the simulation hash
    } randomizer;
    struct Extra {                  // 0x009A2700 + (p * 6 + i) * 0x30: the third effect ring of 0x00416F70 (game type 1)
        bool active = false;        // +0x00
        float pos[3] = {};          // +0x04 view space
        int pitch = 0, yaw = 0, spin = 0;  // +0x10 / +0x14 / +0x18
        int counter = 0, type = 0;  // +0x28 / +0x2C
    } extras[2][6];
    uint8_t itemTaken = 0;          // 0x007DCD14 (the stage items ignore hits while set)
    uint8_t prop17Shot = 0;         // 0x009A26EC (type 0x44 state 17: one of them was shot with its item)
    int8_t captions = 0;            // 0x007DCD04 item captions shown (0x00475E40 / 0x00475D00)
};

// world_modes_boss.cpp
bool SpawnModes(World::Impl& w, Task& t, SpawnTask& s);  // actor types 0x6C / 0x6D / 0x6E (the select screens)
void BossGameSetup(World::Impl& w);                       // 0x00425E90
void BossSwitchScene(World::Impl& w, int stage, int scene);
void SelectAgentDraw(World::Impl& w, EnemyTask& a);       // 0x00497F80
void ModesDimCount(World::Impl& w);                       // 0x00413CC0
int BossRankCheck(World::Impl& w, int p);                 // 0x004157F0 for game type 3
int ModesTableIndex(World::Impl& w);                      // 0x00480F70: name-entry table (index of 0x0057A354)

// world_modes_train.cpp
bool SpawnTrain(World::Impl& w, Task& t, SpawnTask& s);   // actor type 0x6D (0x00496BA0): lesson select / lesson flow
bool SpawnLessonActor(World::Impl& w, Task& t, SpawnTask& s);  // actor types 0x1A / 0x1B (0x00498FF0 / 0x00499420)
void TrainTimeLeft(World::Impl& w, int limit, int counter);    // 0x00498C10 (the lessons' time left)
void SpawnKomono67(World::Impl& w, const SpawnRecord& r);       // generic komono kind 67 (0x00461CF0 case 0x43)

// world_routes.cpp
void SpawnBarrelPiecesFrom(World::Impl& w, int idx, int group, int skel, const Mat4& mv);  // 0x00465170

// world_modes_orig.cpp
bool SpawnItemSelect(World::Impl& w, Task& t, SpawnTask& s);  // actor type 0x6E (0x00488820): the item select
void ItemsInit(World::Impl& w);                                // 0x0048A0D0 (every game start)
bool OriginalItemsCompatible(World::Impl& w, int a, int b);
void OriginalSetLoadouts(World::Impl& w, const int slots[2][2]);

// world_randomizer.cpp: deterministic Original-based enemies, encounter sizes and equipment.
bool RandomizerActive(const World::Impl& w);
void RandomizerBegin(World::Impl& w, bool enabled);
void RandomizerStage(World::Impl& w);
void RandomizerStageReady(World::Impl& w);
bool RandomizerSpawn(World::Impl& w, uint32_t op, const SpawnRecord& record);
int RandomizerPickItem(World::Impl& w);
bool RandomizerCollect(World::Impl& w, int player, int item);
uint32_t RandomizerHash(const World::Impl& w);
int RandomizerVoiceLine(const World::Impl& w, int line);
uint32_t RandomizerVoiceSound(const World::Impl& w, uint32_t sound);
void RandomizerClearSubtitles(World::Impl& w);
int RandomizerCharacterKind(const World::Impl& w, int kind);
void RandomizerCharacterDraw(World::Impl& w, const EnemyTask& actor, size_t firstDraw);

// world_modes_end.cpp
bool ModesStageEnd(World::Impl& w);                       // 0x004603B0 at the count's end, game types 2 / 3

// world_scene_select.cpp (port only, WorldConfig::portFeatures: the mode select's "Scene Select" entry and its page)
bool SpawnSceneSelect(World::Impl& w, Task& t, SpawnTask& s);  // actor 0x6C while Front::sceneSelect: the chapter list
void SceneSelectMenuConfirm(World::Impl& w);  // the entry confirmed on the mode select (Boss mode's game, arcade credits)
void SceneSelectStart(World::Impl& w, int stage);  // a chapter confirmed: the next frame is GameStartFrame at that stage
int SceneSelectFirstScene(int stage);         // 0x009A2BC0 at a normal game's Stage_Start of that stage

// world_scene_select.cpp: the port's 2D text helpers (the subtitle glyph font 0x0055E054, tagged with the boss select's
// draw function) for the port-only pages
void PortGlyphText(World::Impl& w, float x, float y, float scale, const std::string& s, float depth, bool centred,
                   const float* tint = nullptr);
void PortSprite(World::Impl& w, int id, float x, float y, float depth, float sx = 1, float sy = 1, int rot = 0,
                uint32_t flags = 0, const float* tint = nullptr, float alpha = 1.0f);

// world_horde.cpp (port only, WorldConfig::portFeatures + portModernMenu: the mode select's "Horde Mode" entry)
constexpr int kHordeBehaviour = 90;             // enemy behaviour of the Horde mode's own zombies (not in the original's table)
void HordeMenuConfirm(World::Impl& w);          // the entry confirmed on the mode select
void HordeGameStart(World::Impl& w);            // after FrontStartStage of a Horde game: the mode's task
bool HordeActive(World::Impl& w);               // a Horde game is running (its state exists)
bool HordeSpecialWeapon(World::Impl& w, int p); // the player bought a special weapon (the trigger is 0x00414B90's)
void HordeEnemyBehaviour(World::Impl& w, EnemyTask& e);  // behaviour kHordeBehaviour
void HordeEnemyKilled(World::Impl& w, EnemyTask& e, int p, bool head);  // a player's shot killed an enemy (money)
void HordeShotDamage(World::Impl& w, int p, EnemyShotState& s);  // the player's bought damage upgrades
void HordeBatDive(World::Impl& w, float diveOff[2]);  // a bat's dive (world_st1b_bats.cpp): the offset from the camera to a barrel
void HordeBatStrike(World::Impl& w);                  // the bite at the dive's end: a barrel's hp
void HordeThrowAim(World::Impl& w, int target, int hand, float aim[3]);  // a thrown weapon's aim point (world_thrown.cpp): a barrel
void HordeThrowStrike(World::Impl& w, int target, int hand);             // its arrival: that barrel's hp

// world_modes_menu.cpp
void ModesBoot(World::Impl& w);                 // the persistent data (World::Boot)
bool ModesMenuConfirm(World::Impl& w, int entry);  // 0x00496960: entries 1..6 (true: handled)
bool ModesFrame(World::Impl& w);                // Game_UpdateMode 0x004608A0 for modes 8 / 12 / 15 (false: other mode)
int ModesCredits(World::Impl& w, int entry);    // 0x00496B70

// world_modes_weapons.cpp (game type 1)
void SpecialTrigger(World::Impl& w, Task& t);                       // 0x00414B90
void SpecialReload(World::Impl& w, int p);                          // 0x00414E40
void SpecialExtraDraw(World::Impl& w, int p, int i);                // 0x00416B00: the third ring's slot i
void SpecialTracer5Draw(World::Impl& w, const float pos[3], int counter);  // 0x00416B00: tracer type 5

// world_modes_items.cpp
bool SpawnKomonoModes(World::Impl& w, const SpawnRecord& r, int kind);  // generic komono kinds 68 / 73 / 75 / 76
void SpawnCreatureKomono68(World::Impl& w, const SpawnRecord& r);      // 0x00463E50 (world_st1b.cpp)

// world_st1b_boss.cpp
bool SpawnMount23(World::Impl& w, Task& t, const SpawnRecord& r);  // actor 0x23 from its own record (training lesson 4)

// world_modes_story.cpp
bool SpawnStory(World::Impl& w, Task& t, SpawnTask& s);  // mode-11 title card 0x00434DA0, actor type 0x50 (0x004997E0)

// world_modes_net.cpp
void NetFrame(World::Impl& w);                  // game mode 15 (0x0049F380): the lobby without a network

// world_modes_rank.cpp
void RankFrame(World::Impl& w);                 // game mode 8 (0x0041A5E0)
// world_modes_options.cpp
void OptionsFrame(World::Impl& w);              // game mode 12 (0x004869E0)

}  // namespace game
