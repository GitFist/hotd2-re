#pragma once

// Enemy actors (type 0x30, "zombies"): the parts of the enemy lifecycle that are pure functions of exe tables,
// object state and rand() results. See notes/agents/enemy.md for the object layout and the full lifecycle.
//
// Sources: Enemy_Create 0x00452DA0, Enemy_Update 0x004533F0, Enemy_ApplyShot 0x00409430, Enemy_InitHp 0x0040A8B0,
// Enemy_RankDamageMod 0x00409870, Enemy_CorpseTwitch 0x00454E00, Enemy_DeathMotion 0x004560B0,
// Enemy_JumpSetup 0x0044DB50, Enemy_JumpStep 0x0044DDE0, Enemy_BehDeathFall 0x004550E0, Enemy_BehCorpse 0x00454F20,
// Enemy_PickTarget 0x00455DE0, Anim_TrackTick 0x004111A0 and the CRT rand 0x004ADED2.

#include <array>
#include <cstdint>
#include <vector>

#include "exe_image.h"
#include "rng.h"  // CrtRand: VC6 CRT rand() 0x004ADED2

namespace game {

// Static enemy tables read from Hod2.exe.
struct EnemyTables {
    static constexpr int kBehaviourCount = 54;          // 0x00592AE8[0..53]
    std::array<uint32_t, kBehaviourCount> behaviour{};  // Enemy behaviour handlers, indexed by obj+0x1310
    std::array<uint32_t, 5> hooks{};                    // 0x00592BC0..0x00592BD0 (per-frame / landing / spawn hooks)
    std::array<int16_t, 5> hpBonus{};                   // 0x005776B0[difficulty] (stride 4)
    std::array<int8_t, 5> initialRank{};                // 0x005679F4[difficulty] -> 0x009C8E96 at stage start
    std::array<float, 0x48> kindRadius{};               // 0x004C4D28[kind] -> obj+0x124
    std::array<uint8_t, 16> partBit{};                  // 0x004C4D18[slot]: bit in obj+0x1318 for a lost part

    struct Kind {
        int nodeSlots = 0;                 // 0x004E0724[kind]
        std::vector<int16_t> nodeTable;    // 0x004C7160[kind]: per (slot*6 + stage) pairs {model to swap in, next}
        std::vector<int16_t> baseDamage;   // 0x004C8350[kind]: per (slot*6 + stage) damage of a hit
        std::vector<int8_t> rankMod;       // 0x004D0D84[kind]: per (slot*16 + rank) damage modifier (empty = none)
    };
    std::vector<Kind> kinds;  // indexed by kind (= skeleton type, obj+0x1F4), 0..63

    bool Load(const ExeImage& exe);
};

// 0x0040A8B0: hp = clamp(recordHp + hpBonus[difficulty], 1, 300); gameMode 5 (attract demo) uses difficulty 2.
int EnemyInitialHp(const EnemyTables& t, int recordHp, int difficulty, int gameMode);

// 0x00409870: rank damage modifier (signed char), 0 if the kind has no table. Kind 0x17 maps slots > 15 to 0.
int EnemyRankDamageMod(const EnemyTables& t, int kind, int slot, int rank);

// Per-node state inside the node buffer (obj+0x20C + slot*0x90).
struct EnemyNodeState {
    int32_t model = 0;     // +0x00: model id drawn at the node (swapped for wound models)
    uint32_t flags = 0;    // +0x74: bit 31 = wound applied once; bits (1 << (player+1)) / 8 = hit this frame
    int16_t stage = 0;     // +0x8C: damage stage of this node
};

// Enemy state touched by a shot (Enemy_ApplyShot 0x00409430).
struct EnemyShotState {
    int kind = 0;           // obj+0x1F4
    uint32_t flags34 = 0;   // obj+0x34
    uint32_t flags136C = 0; // obj+0x136C
    int16_t hp = 0;         // obj+0x11C
    uint8_t severMask = 0;  // obj+0x1318 (bit per body part from 0x004C4D18[slot]) - only partly modelled
    int8_t killer = -1;     // obj+0x131C
    std::vector<EnemyNodeState> nodes;
    // game type 1 (0x009CA08C == 1): damage x the shooter's item factor 0x009A224C + p * 0x14 (-1: the whole hp),
    // x 0x004C4CA0 with item 0x1D (0x009C88A9); 24-bit x87 products, truncated (_ftol)
    bool itemDamage = false;
    float damageFactor = 1.0f;
    float damageBoost = 0.0f;  // 0: none
};

enum class EnemyHitType { None = 0, Wound = 1, Plain = 2, Sever = 3, Deflect = 5 };  // 0x009A58F8[player]

struct EnemyShotResult {
    EnemyHitType type = EnemyHitType::None;
    int damage = 0;           // amount subtracted from hp (0 if flags34 & 0x1000)
    bool killed = false;      // this shot set flags34 |= 0x4000000
    int score = 0;            // points given to the player via Player_AddScore 0x004156C0 (kill + hit)
    bool modelSwapped = false;
    int randCalls = 0;        // rand() calls made (only game mode 6 head kills)
    bool decapitate = false;  // that rand() & 3 == 0: head blown off (0x00407200 / 0x0040A130 / node 2 model 0)
};

// Applies one shot of `player` on node `slot` (0x00409430, without the 0x009CA08C == 1 override and without the
// wound-table lookup 0x004099A0 / severed-subtree walk 0x00409AB0, which only matter when models are swapped).
// gameMode = 0x009C8E98 (5 = attract demo; only mode 6 swaps wound models), rank = 0x009C8E96.
EnemyShotResult EnemyApplyShot(const EnemyTables& t, EnemyShotState& s, int slot, int gameMode, int rank,
                               CrtRand* rng = nullptr);

// 0x00454E00: corpse "twitch". For the death motions with a hold table the anim counter (obj+0x194) is set to
// table[r % 17 == 16] (r = one rand()); returns the new counter or -1 if the motion has no table (no rand call).
int EnemyCorpseHoldTick(int motion, int randValue);
bool EnemyCorpseHoldUsesRand(int motion);

// Motion tick of a track that is not blending (Anim_TrackTick 0x004111A0, track 0, (+0x37 & 1) == 0).
struct MotionTick {
    int tick = 0;        // +0x08: counter % (ticks + 1)
    int frame = 0;       // +0x18: stored 30 Hz frame
    bool interpolate = false;  // +0x37 |= 0x20: odd tick between frame and frame + 1 (or frame 0)
    int nextFrame = 0;
    bool ended = false;  // +0x5D: tick >= ticks
    bool nearEnd = false;  // +0x5E: tick >= ticks - 1
};
MotionTick EnemyMotionTick(int counter, int motionTicks);

// 0x004560B0, the branches that pick a fixed death motion. Returns the motion and the blend length passed to
// Anim_BlendTo 0x004119A0 (always 5 -> 6 blend frames), or -1 for the walk-dependent default branch
// (0x00456220 / 0x004563F0, not decoded). randValue is only consumed for variant 4.
struct EnemyDeathMotionChoice { int motion = -1; int startTick = 0; bool usesRand = false; };
EnemyDeathMotionChoice EnemyDeathMotion(int kind, int variant, uint32_t flags34, uint32_t flags1368,
                                        uint32_t flags136C, int randValue);

// Death "knock-back" jump (Enemy_BehDeathFall 0x004550E0 state 0..2).
// State 0 computes a target point in the camera space of the body point obj+0x70 (rand: sign then % 5),
// transforms it to world with the inverse view and calls Enemy_JumpSetup.
struct EnemyDeathJumpOffset { float view[3]; float divisor; int randCalls; };
// view: obj+0x70..0x78 (view-space body point); r[]: rand() values in call order (2, or 4 for variant 4).
EnemyDeathJumpOffset EnemyDeathJumpTarget(int variant, const float view[3], const int* r);

struct EnemyJump {
    float start[3] = {};   // obj+0x13C0
    float target[3] = {};  // obj+0x13CC
    int counter = 0;       // obj+0x1330
    int duration = 0;      // obj+0x1334
    // 0x0044DB50: duration = max(minT, ftol(xzDistance / (30 / minT))), minT = 15 (10 if flag136C & 0x2000000
    // and !(flags34 & 0x44000000)).
    void Setup(const float from[3], const float to[3], uint32_t flags136C, uint32_t flags34);
    // 0x0044DDE0: returns true and the velocity while counter < duration (counter is incremented either way).
    bool Step(float vel[3]);
};

// Gravity used after the jump (obj+0x5C): variant 4/5 -> 0xBCDF0123 (-0.0272), else 0xBD16872B (-0.0368).
float EnemyFallGravity(int variant);

// One frame of the fall (state 2) before the main loop adds the velocity: vy += gravity; if y + vy is still above
// `floorY` -> Falling. Otherwise y = floorY and the enemy bounces (vy *= -0.25) while |vy| > 0.15 and
// fallFrames < 120, else it lands (velocity cleared, behaviour -> 7 corpse). Non-water stages only.
enum class EnemyFallResult { Falling, Bounce, Landed };
EnemyFallResult EnemyFallStep(float& y, float& vy, float gravity, float floorY, int fallFrames);

// Corpse (Enemy_BehCorpse 0x00454F20): 120 frames, sinking 0.04 per frame, then the task ends.
constexpr int kEnemyCorpseFrames = 0x78;
constexpr float kEnemyCorpseSink = 0.04f;  // 0x004C4D04

// Enemy_PickTarget 0x00455DE0, branch taken when 2 players are active, 0x009C8E80 != 1 and only one enemy
// is attacking (0x009C7006 == 1): player = rand() & 1, inverted (-1/-2 = none) if that player's slot is taken.
int EnemyPickTargetSingleAttacker(int randValue, const bool slotTaken[2]);

}  // namespace game
