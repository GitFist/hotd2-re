// game::World - the port's Horde mode (play.exe / hotd2.exe only: WorldConfig::portFeatures with the modern mode select;
// nothing of this exists in the original and it is off in the exactness tests). notes/agents/horde.md.
//
//   - the mode select's "Horde Mode" entry (world_front.cpp TaskMenu) starts an arcade game at stage 1 scene 0 (the
//     street of the opening) with the Arcade entry's credits (HordeMenuConfirm -> GameStartFrame -> HordeGameStart);
//   - the opening cutscene is skipped as soon as the script allows (the skip watcher, as a reload press would); when the
//     gameplay camera starts in the street (mode 2, letterbox open) the mode takes over: the event VM is halted (no
//     further scripted spawns, sections or camera changes), the rail stops advancing (Impl::hordeHoldRail), the scripted
//     zombies are despawned and the players' point / the camera stay where the script left them;
//   - a row of kLanes barrels (the stage-1 route barrels' model 0x19E8 / cracked 0x19E6, komono_2.bin) stands across the
//     street kBarrelDistance in front of the players; the zombies (behaviour kHordeBehaviour) walk to the nearest intact
//     barrel (the walk-in motions 2 / 3 of their motion set) and hit it with their attack motions (the attack tables
//     0x00592DC0 / 0x00592F18, as behaviour 3) until it breaks (15 pieces, 0x00465170); once their lane is open they
//     approach the players as any zombie (behaviour 2) and take a life with their attack;
//   - rounds: a round lasts its time; the spawner keeps the alive count below the round's cap; the round ends when the
//     time is out and every zombie is dead, then the store (the buy phase) and the next round;
//   - the store: a page in the look of the original's select screens (the boss select's background tiles, the glyph
//     font, the item select's name sprites 0x5F9.., the original mode's item models of etc_1.bin, the agents' own bodies
//     from the torso up); a gun player aims at a cell and pulls the trigger to buy with the money of kills (the hook in
//     Enemy_ApplyShot's score tail) and round bonuses: the original mode's special weapons (0x00414B90: shotgun burst,
//     machine gun, grenade), power ups (damage factor), magazines (CHAMBER), lives, barrel repairs / reinforcement and
//     three CPU agents (Amy, Harry, Goldman: the player skeletons 0x3C / 0x3B / 0x3D) who stand by the barrels and shoot
//     the zombies with their own rate / accuracy / damage;
// Everything here runs inside the deterministic world sim (lockstep co-op works as for any game).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#include "game/mstack.h"
#include "game/world_modes.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}
uint8_t B8(const ExeImage& exe, uint32_t va) {
    uint8_t v = 0;
    exe.Read(va, &v, 1);
    return v;
}

constexpr uint32_t kHordeRecordType = 0x7F01;   // SpawnRecord::type of the mode's own zombies
constexpr uint32_t kDespawnRecordType = 0x7F02; // the scripted enemies found at the take-over (despawned by the behaviour)
constexpr int kLanes = 5;
constexpr int kBarrelModel = 0x19E8, kBarrelCracked = 0x19E6;
constexpr uint32_t kBarrelFile = 0x5A;          // komono_2.bin (0x004E83B4[0x19E8])
constexpr float kBarrelDistance = 30.0f;        // from the players' point, along the camera's forward: the row right in
                                                // front of the players, the barrels' tops in view, their bases below the screen
constexpr float kLaneSpacing = 6.5f;            // (a tight barricade: the barrels touch)
constexpr float kBarrelLift = 3.770148f;        // the route barrels' draw lift over their position (world_routes.cpp)
constexpr float kBarrelSink = 4.5f;             // the row is drawn this much lower: only the rims and the upper staves show
                                                // along the screen's bottom (the camera is 15 units up)
constexpr float kSpawnNear = 170.0f, kSpawnFar = 260.0f, kSpawnHalfWidth = 26.0f;
constexpr float kReach = 9.0f;                  // a zombie this close to its barrel attacks it
constexpr int kBuyFrames = 30 * 60;
constexpr int kBarrelHp = 120;
constexpr int kMaxLives = 5;                    // the HUD's torches (0x004174A0)

struct Lane {
    float pos[3] = {};
    int yaw = 0;
    float hp = kBarrelHp, hpMax = kBarrelHp;
    bool alive = true;
    float shake = 0;
    Mat4 mv = Mat4::Identity();  // the last model-view (the pieces start from it)
};

// The zombies: the original's own kinds / variants (HOTD2_ENEMYLOG=1 over the auto-play lists what the stages spawn),
// each a threat type (walker: the barrel attack; thrower: axes at the players from mid-street, then a walker; swamp:
// rises from a pool of its own blood near the players), with its hp, barrel damage and walk pace (x/256 of the
// walk-in motion's rate). The rounds phase them in and out through Weight(). (The 0x00592CBC sets: 0 the approach
// shuffle in place, 2 / 3 the walk-in motions with root travel, 4 the back-off.)
enum SpawnType { kWalker, kThrower, kSwamp, kZeal, kKnight };
struct Spawn {
    int kind, variant, hp, barrelDamage, pace;
    SpawnType type;
    const char* name;
};
constexpr Spawn kSpawns[] = {
    { 0x07, 0, 100, 10, 256, kWalker, "basic" },           // 0 znsam2 (walk 0x3FE, charge 0x3C8)
    { 0x13, 7, 120, 10, 256, kThrower, "axe" },            // 1 znonoo, the axe thrower (behaviour 33's variant)
    { 0x08, 0, 170, 12, 256, kWalker, "shirt" },           // 2 znsam2b
    { 0x05, 0, 320, 20, 230, kWalker, "brute" },           // 3 znnick, the grey brute
    { 0x02, 2, 260, 24, 330, kWalker, "chainsaw" },        // 4 znchain (0xC3: a fast charge)
    { 0x00, 0, 480, 30, 210, kWalker, "heavy" },           // 5 harold
    { 0x0B, 0, 110, 10, 420, kWalker, "speedy" },          // 6 znkage
    { 0x0F, 0, 90, 8, 280, kSwamp, "swamp" },              // 7 znebi2 (rises with 0xB8)
    { 0x0E, 1, 360, 28, 256, kWalker, "sword" },           // 8 znken, the giant sword (0x1C0 / 0x1C1)
    { 0x04, 2, 300, 24, 330, kWalker, "gold chainsaw" },   // 9 zngold
    { 0x10, 6, 110, 10, 280, kSwamp, "swamp 2" },          // 10 znebi3
    { 0x12, 0, 150, 12, 256, kWalker, "znele" },           // 11
    { 0x14, 2, 170, 12, 256, kWalker, "onoopa" },          // 12 znonoopa
    { 0x45, 1, 700, 45, 256, kZeal, "zeal" },              // 13 the boss round's flyer (the stage-1 boss's rider, its own flight here)
    { 0x44, 0, 1000, 0, 256, kKnight, "kuarl" },           // 14 the boss round's knight (the stage-1 boss's mount)
};
constexpr int kSpawnCount = static_cast<int>(sizeof(kSpawns) / sizeof(kSpawns[0]));
constexpr int kSpawnZeal = 13, kSpawnKnight = 14;
constexpr int kBossEvery = 10;
constexpr float kHoverDistance = 78.0f, kHoverHeight = 18.0f;  // Zeal's hover point over the street
constexpr int kChargeFrames = 70, kRetreatFrames = 55;

// the spawn weights of a round (0: not in the round)
int Weight(int i, int r) {
    switch (i) {
    case 0: return r == 1 ? 100 : r == 2 ? 75 : r <= 4 ? 55 : r == 5 ? 45 : r == 6 ? 25 : 0;  // the basic phases out
    case 1: return r < 2 ? 0 : r <= 4 ? 45 : r <= 6 ? 30 : 22;
    case 2: return r < 6 ? 0 : r == 6 ? 30 : r <= 8 ? 50 : 35;                                // the shirt takes over
    case 3: return r < 5 ? 0 : r <= 7 ? 15 : r == 8 ? 6 : 0;                                  // the brute, then the heavy
    case 4: return r < 7 ? 0 : r < 11 ? 10 : 12;
    case 5: return r < 8 ? 0 : 12;
    case 6: return r < 9 ? 0 : 18;
    case 7: return r < 9 ? 0 : 15;
    case 8: return r < 11 ? 0 : 12;
    case 9: return r < 13 ? 0 : 8;
    case 10: return r < 12 ? 0 : 10;
    case 11: return r < 12 ? 0 : 12;
    case 12: return r < 12 ? 0 : 10;
    default: return 0;
    }
}
bool BossRound(int r) { return r % kBossEvery == 0; }

// ---- the store ----
// the 4 x 4 grid: the weapons, their upgrades, the base, the agents (+ READY)
enum Item {
    kPistol, kShotgun, kMachineGun, kGrenade,
    kPistolUpg, kShotgunUpg, kMachineGunUpg, kGrenadeUpg,
    kFirstAid, kNewBarrel, kReinforce, kLife,
    kAgentAmy, kAgentHarry, kAgentGoldman, kReady,
    kItems
};
// the weapons (0 pistol, 1 shotgun, 2 machine gun, 3 grenade launcher: the original mode's item rows 0x004EC928,
// magazine 6 / 2 / 6 / 3, damage 1 / 1 / 1 / 4) start weak and are upgraded one by one (levels 1..3): the damage
// factor on the row's and more rounds a magazine
constexpr int kMaxLevel = 3;
constexpr float kDamageLevel[4][4] = {
    { 1.0f, 1.15f, 1.3f, 1.5f },    // the pistol
    { 0.6f, 0.8f, 1.0f, 1.25f },    // the shotgun
    { 0.6f, 0.8f, 1.0f, 1.2f },     // the machine gun
    { 0.5f, 0.65f, 0.8f, 1.0f },    // the grenade launcher (the row's 4.0: 2.0 .. 4.0)
};
constexpr int kMagazineAdd[4][4] = { { 0, 2, 4, 8 }, { 0, 1, 2, 3 }, { 0, 3, 6, 10 }, { 0, 0, 0, 0 } };
constexpr int kWeaponPrice[4] = { 0, 4500, 6000, 8000 };
constexpr int kUpgradePrice[4][3] = { { 800, 1600, 3200 }, { 2000, 3500, 5000 }, { 2500, 4000, 6000 }, { 3000, 5000, 7000 } };
constexpr int kGrenadeRounds = 4;      // a purchase / refill
constexpr int kGrenadeRefill = 2500;
int GrenadeCapacity(int level) { return kGrenadeRounds + level * 2; }
constexpr int kAgents = 3;
struct AgentDef {
    const char* name;
    int kind;          // player skeleton
    int label;         // the item select's name sprite
    int price;
    int interval;      // frames between shots
    int accuracy;      // percent
    int damage;
    float flashX, flashY;  // the muzzle flash in view space (the agent stands beside the players, off screen)
};
constexpr AgentDef kAgentDef[kAgents] = {
    { "AMY", 0x3C, 0x60F, 2500, 50, 60, 25, -0.46f, -0.30f },
    { "HARRY", 0x3B, 0x610, 6000, 32, 70, 35, 0.46f, -0.30f },
    { "GOLDMAN", 0x3D, 0x614, 10000, 14, 55, 30, -0.46f, -0.12f },
};

struct Agent {
    bool owned = false;
    int cooldown = 0;
    int flash = 0;     // frames of muzzle flash left
    std::unique_ptr<EnemyTask> portrait;  // the store's cell (torso up)
};

struct HordeState : TaskData {
    int phase = 0;          // 0 waiting for the street (cutscene skipped), 1 fight, 2 buy
    int round = 0;
    int timer = 0;          // frames left of the fight / buy phase
    int spawnTimer = 0;
    int alive = 0;          // the mode's zombies alive (counted every frame)
    int spawnedThisRound = 0, killedThisRound = 0;
    float origin[3] = {};   // the players' point 0x009C71E0 at the take-over
    float fwd[3] = {}, right[3] = {};
    Lane lanes[kLanes];
    int money[2] = {};
    int barrelHpMax = kBarrelHp;
    int reinforce = 0;      // REINFORCE bought (0..3)
    int level[2][4] = {};   // the weapons' upgrade levels (0..kMaxLevel)
    int weapon[2] = {};     // equipped: 0 pistol, 1 shotgun, 2 machine gun, 3 grenade
    bool owned[2][4] = { { true, false, false, false }, { true, false, false, false } };
    int grenades[2] = {};   // the grenade launcher's rounds left (limited; kGrenadeRounds a purchase)
    bool ready[2] = {};     // the store: the player is done
    int pointerRow[2] = {}; // the store: cell under the pointer last frame (-1 none)
    bool pressedTrigger[2] = {};
    Agent agents[kAgents];
    int lastKillerBonus = 0;
    uint32_t takeoverFrame = 0;
    bool debug = false;
    // the swamp pools: a zombie due to rise from each
    struct Pool {
        float pos[3];
        int spawn, due;
    };
    std::vector<Pool> pools;
    int batTimer = 0;
    // the boss round: Kuarl (the knight, skeleton 0x44) walks the street down on the timer; Zeal (the flyer, 0x45, with
    // its wings 0x46) charges the barrels, staggers when shot, dies to end the round early
    bool bossRound = false;
    bool bossOver = false;  // Zeal dead (the bonus paid) or the knight's blow landed
    bool bossBonus = false;
    std::unique_ptr<EnemyTask> wings;  // Zeal's second object (skeleton 0x46), drawn at its node 1 as 0x0049D770 does
    int wingsMotion = -1;
    float hoverT = 0;
    int bossBatTimer = 0;
};

struct HordeTaskData : WTask {};

HordeState* Ptr(World::Impl& w) { return static_cast<HordeState*>(w.horde.get()); }

// the round's parameters
int RoundSeconds(int r) { return BossRound(r) ? 90 : std::min(40 + 5 * r, 100); }
int RoundCap(int r) { return std::min(5 + r, 16); }
int RoundSpawnInterval(int r) { return std::max(22, 96 - 7 * r); }
int RoundHp(int base, int r) { return base + base * (r - 1) * 8 / 100; }            // +8 % a round
int RoundPace(int pace, int r) { return std::min(pace + 20 * (r - 1), pace * 8 / 5); }  // the walk's tick rate, /256 (to 1.6x)
int RoundChargeChance(int r) { return std::min(10 + 5 * r, 60); }                   // percent that start with the charge motion
int KillMoney(int r, int hp, bool head) { return 20 + 3 * r + hp / 40 + (head ? 15 : 0); }
int RoundBonus(int r) { return 60 + 30 * r; }
int BatCount(int r) { return std::min(2 + r / 3, 6); }
int BatInterval(int r) { return std::max(12 * 60, 28 * 60 - r * 60); }               // frames between groups (normal rounds)
int BossHp(int r) { return 3000 + 1000 * (r / kBossEvery - 1); }
int BossKillBonus(int r) { return 2500 + 1000 * (r / kBossEvery - 1); }
// the player's damage factor on the pistol's: the equipped weapon's row damage x its level
float WeaponFactor(World::Impl& w, const HordeState& h, int p) {
    const int weapon = std::clamp(h.weapon[p], 0, 3);
    const float row = w.modes ? w.modes->pitems[p].damage : 1.0f;
    return row * kDamageLevel[weapon][std::clamp(h.level[p][weapon], 0, kMaxLevel)];
}

float Dist2D(const float a[3], const float b[3]) {
    const double dx = static_cast<double>(a[0]) - b[0], dz = static_cast<double>(a[2]) - b[2];
    return static_cast<float>(std::sqrt(dx * dx + dz * dz));
}

bool PlayerIn(World::Impl& w, int p) {
    const int s = w.player[p].state;
    return s == 5 || s == 1 || s == 4;
}

// ---------------------------------------------------------------------------------------------------------------------
// Bodies (the player skeletons as agents and portraits; as 0x00416450 builds the players' bodies)
// ---------------------------------------------------------------------------------------------------------------------

std::unique_ptr<EnemyTask> MakeBody(World::Impl& w, int kind, bool torsoOnly, int motion = -1) {
    auto b = std::make_unique<EnemyTask>();
    b->anim.kind = static_cast<int16_t>(kind);
    b->anim.motion[0] = motion >= 0 ? motion : static_cast<int>(w.exe.U32(0x004EC8A4));  // the standing motion (0x32C)
    w.EnemyInitPose(*b);
    if (b->actorSlot >= 0 && b->actorSlot < 14) w.actorSlots[b->actorSlot] = nullptr;
    b->actorSlot = -1;
    b->anim.rotOrder = 1;
    b->anim.drawFlags &= ~2u;  // no root motion
    b->flags34 |= 0x80000;     // no shadow
    if (torsoOnly)
        for (int i = 9; i < 16; ++i) b->anim.nodes[i].model = 0;  // the pelvis and the legs (nodes 9..15 of the player skeletons)
    return b;
}

// ---------------------------------------------------------------------------------------------------------------------
// Spawns
// ---------------------------------------------------------------------------------------------------------------------

// (the record's param 3 keeps the kSpawns index; the enemy's +0x1374 (behaviour 15's walked length, unused here) its pace)
EnemyTask* SpawnZombie(World::Impl& w, int spawn, int hp, const float pos[3], int yaw) {
    const Spawn& r = kSpawns[spawn];
    EnemyTask* e = w.NewTask<EnemyTask>(WorldTramp<&World::Impl::TaskEnemy>, fn::kEnemyCreate);
    e->lock = &w.lockTargets[w.nextLockTarget++];
    e->flags34 = 1;
    memcpy(e->pos, pos, 12);
    e->rot[1] = yaw & 0xFFFF;
    e->hp = e->hpMax = static_cast<int16_t>(hp);
    e->lo = 0;
    SpawnRecord rec;
    rec.type = kHordeRecordType;
    rec.state = static_cast<uint16_t>(hp);
    memcpy(rec.pos, pos, 12);
    rec.rot[1] = e->rot[1];
    rec.params[0] = static_cast<uint8_t>(r.kind);
    rec.params[1] = static_cast<uint8_t>(r.variant);
    rec.params[2] = static_cast<uint8_t>(kHordeBehaviour);
    rec.params[3] = static_cast<uint8_t>(spawn);
    e->record = rec;
    e->anim.kind = static_cast<int16_t>(r.kind);
    e->variant = r.variant;
    e->walked = static_cast<float>(r.pace);  // (+0x1328 is the blinking face's counter of the 0x1F09 heads: not free)
    return e;
}

const Spawn& SpawnOf(const EnemyTask& e) { return kSpawns[std::clamp<int>(e.record.params[3], 0, kSpawnCount - 1)]; }

bool IsHordeEnemy(const EnemyTask& e) { return e.record.type == kHordeRecordType; }

// the mode's live zombie of a spawn index (the bosses), or null
EnemyTask* FindSpawn(World::Impl& w, int spawn) {
    for (Task* t : w.ts.Live()) {
        if (!t->data) continue;
        EnemyTask* e = dynamic_cast<EnemyTask*>(t->data.get());
        if (e && IsHordeEnemy(*e) && e->record.params[3] == spawn && (e->flags34 & 1)) return e;
    }
    return nullptr;
}

// the bats alive (TaskBat 0x0043C440; 0x007DCBD0 keeps 1 once the last of a group is shot)
int BatsAlive(World::Impl& w) {
    int n = 0;
    for (Task* t : w.ts.Live()) {
        if (!t->data) continue;
        const WTask* k = dynamic_cast<const WTask*>(t->data.get());
        if (k && k->fn == 0x0043C440) ++n;
    }
    return n;
}

// the yaw of a skeleton at (x, z) facing (fx, fz): Cam_DirToAngles of self - target (0x0049DE60's way; the walkers'
// Enemy_TurnTo agrees: spawned the other way round they walk backwards while turning)
int YawTowards(float fx, float fz, float x, float z) {
    int pitch = 0, yaw = 0;
    DirToAngles(x - fx, 0.0f, z - fz, &pitch, &yaw);
    return yaw & 0xFFFF;
}

// the pol files of a skeleton kind's node models (0x004E83B4[model]; whole files below 0xE7, as the request rule)
void LoadKind(World::Impl& w, int kind, std::set<uint32_t>& files) {
    const Skeleton& s = w.EnemySkeleton(kind);
    for (const SkeletonNode& n : s.nodes) {
        if (n.modelId == 0 || n.modelId >= 0x2800) continue;
        uint16_t f16 = 0;
        w.exe.Read(0x004E83B4 + n.modelId * 2, &f16, 2);
        const uint32_t file = f16;
        if (file < 0xE7) {
            if (files.insert(file).second) w.ProcessRequest({ 0x52, file });
        } else {
            w.ProcessRequest({ 0x50, n.modelId });
        }
    }
}

void LoadSpriteFile(World::Impl& w, int sprite) {
    const uint32_t file = w.exe.U32(0x0057A5BC + static_cast<uint32_t>(sprite) * 4);
    if (file < w.texFiles.size()) w.ProcessRequest({ 0x54, file });
}

void LoadModelFile(World::Impl& w, int model, std::set<uint32_t>& files) {
    uint16_t f16 = 0;
    w.exe.Read(0x004E83B4 + static_cast<uint32_t>(model) * 2, &f16, 2);
    if (f16 < 0xE7) {
        if (files.insert(f16).second) w.ProcessRequest({ 0x52, f16 });
    } else {
        w.ProcessRequest({ 0x50, static_cast<uint32_t>(model) });
    }
}

float StreetPoint(const HordeState& h, float dist, float lat, float out[3]) {
    for (int k = 0; k < 3; ++k) out[k] = h.origin[k] + h.fwd[k] * dist + h.right[k] * lat;
    return out[1];
}

void StartBoss(World::Impl& w, HordeState& h);
void SpawnBats(World::Impl& w, HordeState& h, int count);

void StartRound(World::Impl& w, HordeState& h) {
    ++h.round;
    h.phase = 1;
    h.timer = RoundSeconds(h.round) * 60;
    if (const char* s = h.debug ? getenv("HOTD2_HORDE_SECONDS") : nullptr) h.timer = std::max(1, atoi(s)) * 60;  // development
    h.spawnTimer = 30;
    h.batTimer = h.round >= 3 ? 8 * 60 : BatInterval(h.round);
    h.spawnedThisRound = h.killedThisRound = 0;
    h.pools.clear();
    w.canShoot = 1;
    w.flags5900 &= ~0x20u;
    w.hordeBlackout = false;
    w.Sound(0x121A9);
    h.bossRound = BossRound(h.round);
    if (h.bossRound) StartBoss(w, h);
    if (h.debug) fprintf(stderr, "horde: round %d starts at frame %d (%d s, cap %d%s)\n", h.round, w.frame, RoundSeconds(h.round), RoundCap(h.round), h.bossRound ? ", BOSS" : "");
}

int PickSpawn(World::Impl& w, int round) {
    int total = 0;
    for (int i = 0; i < kSpawnCount; ++i) total += Weight(i, round);
    if (total <= 0) return 0;
    int r = w.Rand(0x48000001) % total;
    for (int i = 0; i < kSpawnCount; ++i) {
        r -= Weight(i, round);
        if (r < 0) return i;
    }
    return 0;
}

void SpawnOne(World::Impl& w, HordeState& h) {
    const int spawn = PickSpawn(w, h.round);
    const Spawn& r = kSpawns[spawn];
    float pos[3];
    if (r.type == kSwamp) {  // a pool of its blood on the near street; the zombie rises from it
        const float dist = 68.0f + static_cast<float>(w.Rand(0x48000002) % 40);
        const float lat = static_cast<float>(w.Rand(0x48000003) % 40) - 20.0f;
        StreetPoint(h, dist, lat, pos);
        int mat = 0;
        pos[1] = w.FloorHeight(pos[0], pos[1] + 30.0f, pos[2], &mat);
        w.SpawnBloodPoolAt(pos, w.Rand(0x48000009) & 0xFFFF, 1.6f);  // 0x00408370
        h.pools.push_back({ { pos[0], pos[1], pos[2] }, spawn, w.frame + 50 });
        ++h.spawnedThisRound;
        if (h.debug) fprintf(stderr, "horde: frame %d pool for %s at %.1f %.1f\n", w.frame, r.name, pos[0], pos[2]);
        return;
    }
    const float dist = kSpawnNear + static_cast<float>(w.Rand(0x48000002) % static_cast<int>(kSpawnFar - kSpawnNear));
    const float lat = static_cast<float>(w.Rand(0x48000003) % static_cast<int>(2 * kSpawnHalfWidth)) - kSpawnHalfWidth;
    StreetPoint(h, dist, lat, pos);
    int mat = 0;
    pos[1] = w.FloorHeight(pos[0], pos[1] + 30.0f, pos[2], &mat);
    EnemyTask* e = SpawnZombie(w, spawn, RoundHp(r.hp, h.round), pos, YawTowards(h.origin[0], h.origin[2], pos[0], pos[2]));
    if (r.type == kWalker && w.Rand(0x48000007) % 100 < RoundChargeChance(h.round)) e->flags34 |= 0x08000000;  // the charge motion from the start
    ++h.spawnedThisRound;
    if (h.debug) fprintf(stderr, "horde: frame %d spawn %s (kind %X) at %.1f %.1f %.1f\n", w.frame, r.name, r.kind, pos[0], pos[1], pos[2]);
}

// the pools' zombies rise when due (+0x34 bit 8 while rising: not shot, as behaviour 13's wait)
void PoolsFrame(World::Impl& w, HordeState& h) {
    for (size_t i = 0; i < h.pools.size();) {
        const HordeState::Pool& p = h.pools[i];
        if (w.frame < p.due) {
            ++i;
            continue;
        }
        EnemyTask* e = SpawnZombie(w, p.spawn, RoundHp(kSpawns[p.spawn].hp, h.round), p.pos, YawTowards(h.origin[0], h.origin[2], p.pos[0], p.pos[2]));
        e->flags34 |= 0x100;
        h.pools.erase(h.pools.begin() + static_cast<long>(i));
    }
}

// the boss round: the knight far down the street, the flyer at its hover point, the health bar
void StartBoss(World::Impl& w, HordeState& h) {
    h.bossOver = h.bossBonus = false;
    h.hoverT = 0;
    h.bossBatTimer = 120;
    float pos[3];
    StreetPoint(h, kSpawnFar - 40.0f, 0.0f, pos);
    int mat = 0;
    pos[1] = w.FloorHeight(pos[0], pos[1] + 30.0f, pos[2], &mat);
    EnemyTask* k = SpawnZombie(w, kSpawnKnight, kSpawns[kSpawnKnight].hp, pos, YawTowards(h.origin[0], h.origin[2], pos[0], pos[2]));
    k->flags34 |= 0x100;  // never hit
    StreetPoint(h, kHoverDistance, 0.0f, pos);
    pos[1] = w.FloorHeight(pos[0], pos[1] + 30.0f, pos[2], &mat) + kHoverHeight;
    EnemyTask* z = SpawnZombie(w, kSpawnZeal, BossHp(h.round), pos, YawTowards(h.origin[0], h.origin[2], pos[0], pos[2]));
    z->flags34 |= 0x100;  // its hits are taken by its own behaviour (0x0049D640's way), not Enemy_ApplyShot
    if (!h.wings) h.wings = MakeBody(w, 0x46, false, w.exe.S16(0x00570C00 + 2));
    h.wingsMotion = -1;
    w.SpawnBossBar(320.0f, 62.0f);  // 0x00435E50 (under the round / time lines)
    w.Sound(0x3018A9);
    if (h.debug) fprintf(stderr, "horde: boss round %d: Zeal %d hp\n", h.round, BossHp(h.round));
}

bool BossOver(const HordeState& h) { return h.bossOver; }

// the boss round's bats: a group whenever none are left, bigger as Zeal's health drops (none once it is dead)
void BossFrame(World::Impl& w, HordeState& h) {
    EnemyTask* z = FindSpawn(w, kSpawnZeal);
    if (!z || h.bossOver) return;
    if (--h.bossBatTimer > 0 || BatsAlive(w) > 0) return;
    const float left = z->hpMax > 0 ? static_cast<float>(std::max<int>(z->hp, 0)) / static_cast<float>(z->hpMax) : 0.0f;
    SpawnBats(w, h, 3 + static_cast<int>((1.0f - left) * 5.0f));
    h.bossBatTimer = 150;
}

// Zeal's wings (skeleton 0x46) at its node 1, the motion by its flight (0x00570C00[0..4]: still .. fast)
void DrawWings(World::Impl& w, HordeState& h) {
    EnemyTask* z = FindSpawn(w, kSpawnZeal);
    if (!z || !h.wings) return;
    EnemyTask& s = *h.wings;
    const Mat4& n1 = z->anim.nodes[1].world;
    s.pos[0] = n1.m[12];
    s.pos[1] = n1.m[13];
    s.pos[2] = n1.m[14];
    mx::MatrixToAngles(n1, &s.rot[0], &s.rot[1], &s.rot[2]);  // 0x004019E0
    const int want = (z->sub == 2 || z->sub == 5) ? 4 : z->sub == 6 ? 0 : 1;
    if (want != h.wingsMotion) {
        w.AnimSetMotion(s, w.exe.S16(0x00570C00 + static_cast<uint32_t>(want) * 2));
        h.wingsMotion = want;
    }
    ++s.anim.counter[0];
    w.AnimDraw(s);
}

// a bat group over the street (the stage-1 bats, world_st1b_bats.cpp: they dive at the barrels here)
void SpawnBats(World::Impl& w, HordeState& h, int count) {
    SpawnRecord r;
    StreetPoint(h, 115.0f, 0.0f, r.pos);
    int mat = 0;
    r.pos[1] = w.FloorHeight(r.pos[0], r.pos[1] + 30.0f, r.pos[2], &mat) + 1.0f;  // on the street (as the cellar window's group of scene 3)
    int pitch = 0, yaw = 0;
    DirToAngles(-h.fwd[0], 0.0f, -h.fwd[2], &pitch, &yaw);
    r.rot[1] = yaw & 0xFFFF;
    w.SpawnBatGroup(r, 1, count);
    if (h.debug) fprintf(stderr, "horde: frame %d bats x%d\n", w.frame, count);
}

// ---------------------------------------------------------------------------------------------------------------------
// The take-over of the street
// ---------------------------------------------------------------------------------------------------------------------

void TakeOver(World::Impl& w, HordeState& h) {
    w.runner->halted = true;
    w.hordeHoldRail = true;
    h.takeoverFrame = static_cast<uint32_t>(w.frame);
    const CameraContext& c = w.runner->camera.ctx[0];
    float s = 0, co = 0;
    mx::SinCos(c.yaw, &s, &co);
    h.fwd[0] = -s;
    h.fwd[1] = 0;
    h.fwd[2] = -co;
    h.right[0] = co;
    h.right[1] = 0;
    h.right[2] = -s;
    memcpy(h.origin, w.playerPos, 12);
    for (int i = 0; i < kLanes; ++i) {
        Lane& l = h.lanes[i];
        const float lat = static_cast<float>(i - kLanes / 2) * kLaneSpacing;
        for (int k = 0; k < 3; ++k) l.pos[k] = h.origin[k] + h.fwd[k] * kBarrelDistance + h.right[k] * lat;
        int mat = 0;
        l.pos[1] = w.FloorHeight(l.pos[0], l.pos[1] + 30.0f, l.pos[2], &mat);
        l.yaw = w.Rand(0x48000004) & 0xFFFF;
        l.hp = l.hpMax = static_cast<float>(h.barrelHpMax);
        l.alive = true;
    }
    // resources: the barrels' file, the zombies' and agents' files, the special weapons' effects, the store's sprites
    // and item models
    std::set<uint32_t> files;
    files.insert(kBarrelFile);
    w.ProcessRequest({ 0x52, kBarrelFile });
    for (const Spawn& r : kSpawns) LoadKind(w, r.kind, files);
    for (const AgentDef& a : kAgentDef) LoadKind(w, a.kind, files);
    for (int kind : { 0x44, 0x45, 0x46 }) LoadKind(w, kind, files);  // the bosses
    for (int m = 0x136E; m <= 0x1381; ++m) LoadModelFile(w, m, files);  // the bats (mol.bin)
    for (int m : { 0x1ECE, 0x1ECA, 0x1ECB, 0x1EC7, 0x249 }) LoadModelFile(w, m, files);  // the thrower's axes (held, thrown)
    w.ProcessRequest({ 0x50, 0x109D });  // weapon effects (0x00460030 for game type 1)
    w.ProcessRequest({ 0x52, 0x3D });
    w.ProcessRequest({ 0x52, 0x41 });
    for (int m : { 0x109E, 0x109B, 0x1087, 0x10A5, 0x10A6, 0x10A7, 0x10A8, 0x10A9, 0x10AA, 0x107E, 0x107F, 0x1080, 0x1081, 0x1082,
                   0x1083, 0x109A, 0x1098, 0x1097, 0x1092, 0x1591 })
        w.ProcessRequest({ 0x50, static_cast<uint32_t>(m) });  // the item models (etc_1.bin, requested one by one)
    for (int id : { 0xB5, 0x5F5, 0x5F9, 0x604, 0x60F, 0x614, 0xADB, 0xAEF }) LoadSpriteFile(w, id);
    // the scripted enemies of the opening leave
    for (Task* t : w.ts.Live()) {
        if (!t->data) continue;
        EnemyTask* e = dynamic_cast<EnemyTask*>(t->data.get());
        if (!e || e->civilian) continue;
        WTask& wt = t->As<WTask>();
        if (wt.fn != fn::kEnemy && wt.fn != fn::kEnemyCreate) continue;
        e->record.type = kDespawnRecordType;
        e->behaviour = kHordeBehaviour;
        e->sub = 0;
    }
    if (h.debug)
        fprintf(stderr, "horde: take-over at frame %d: camera %.2f %.2f %.2f yaw %d; players %.2f %.2f %.2f; fwd %.3f %.3f\n", w.frame,
                c.pos[0], c.pos[1], c.pos[2], c.yaw & 0xFFFF, h.origin[0], h.origin[1], h.origin[2], h.fwd[0], h.fwd[2]);
    StartRound(w, h);
}

// ---------------------------------------------------------------------------------------------------------------------
// Barrels
// ---------------------------------------------------------------------------------------------------------------------

void BreakLane(World::Impl& w, HordeState& h, int i) {
    Lane& l = h.lanes[i];
    l.alive = false;
    l.hp = 0;
    w.Sound(0x1A16A9);
    float view[3];
    mx::Transform(w.cam.view, l.pos, view);
    w.SpawnSmoke(view[2], 1, l.pos[2]);
    SpawnBarrelPiecesFrom(w, i, 0, 0, l.mv);
    if (h.debug) fprintf(stderr, "horde: frame %d barrel %d broke\n", w.frame, i);
}

void DrawLanes(World::Impl& w, HordeState& h) {
    const ExeImage& exe = w.exe;
    for (int i = 0; i < kLanes; ++i) {
        Lane& l = h.lanes[i];
        if (!l.alive) continue;
        float sx = 0, sz = 0;
        if (F32(exe, 0x004C4CC0) < l.shake) {  // the route barrels' shake (world_routes.cpp TaskBarrel)
            const float c40 = F32(exe, 0x00569040), c64 = F32(exe, 0x004D5464);
            sx = ((static_cast<float>(w.Rand(0x48000005) % 0x97) - c40) * l.shake) * c64;
            sz = ((static_cast<float>(w.Rand(0x48000006) % 0x97) - c40) * l.shake) * c64;
            l.shake = l.shake * F32(exe, 0x00564534);
        }
        Mat4 m = Mat4::Identity();
        mx::Translate(m, l.pos[0] + sx, l.pos[1] + kBarrelLift - kBarrelSink, l.pos[2] + sz);
        mx::RotY(m, l.yaw);
        l.mv = w.cam.view;
        mx::MulLeft(l.mv, m);
        w.Draw(l.hp * 2.0f <= l.hpMax ? kBarrelCracked : kBarrelModel, m, 8);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Agents
// ---------------------------------------------------------------------------------------------------------------------

// a shot of an agent: damage (the player's shots go through Enemy_ApplyShot; the agents' are simpler), the kill as
// Enemy_ProcessHits ends it (the death behaviour, the voice), half the kill money to every player
void AgentHit(World::Impl& w, HordeState& h, EnemyTask& e, int damage) {
    e.hp = static_cast<int16_t>(e.hp - damage);
    w.SpawnEffectAt(e.body, 0, 0, 2, 1, 0);  // blood
    if (e.hp > 0 || (e.flags34 & 0x4000000)) return;
    e.flags34 |= 0x4000000;
    e.flags136C |= 0x80000000u;
    e.killer = -1;
    w.EnemyVoice(e, 1);
    e.sub = 0;
    e.behaviour = 6;
    ++h.killedThisRound;
    for (int p = 0; p < 2; ++p)
        if (PlayerIn(w, p)) h.money[p] += KillMoney(h.round, e.hpMax, false) / 2;
}

// The agents stand beside the players, off screen: a shot is the muzzle flash at the screen's edge (the flash models of
// 0x00416B00 in view space, as the players' own), the gun sound and the hit on the zombie nearest the players.
void AgentsFrame(World::Impl& w, HordeState& h) {
    for (int a = 0; a < kAgents; ++a) {
        Agent& ag = h.agents[a];
        if (!ag.owned) continue;
        const AgentDef& d = kAgentDef[a];
        if (--ag.cooldown <= 0) {
            EnemyTask* target = nullptr;
            float best = 0;
            for (Task* t : w.ts.Live()) {
                if (!t->data) continue;
                EnemyTask* e = dynamic_cast<EnemyTask*>(t->data.get());
                if (!e || e->record.type != kHordeRecordType || !(e->flags34 & 1) || (e->flags34 & 0x4000000)) continue;
                if (e->behaviour == 6 || e->behaviour == 7 || e->behaviour == 9) continue;
                const float dist = Dist2D(e->pos, h.origin);
                if (!target || dist < best) {
                    target = e;
                    best = dist;
                }
            }
            if (target) {
                ag.cooldown = d.interval;
                ag.flash = 4;
                w.Sound(w.exe.U32(0x004EC8BC + (a == 1 ? 0u : 4u)));  // the agents' gun sounds (P1's / P2's)
                if (w.Rand(0x48000020) % 100 < d.accuracy) AgentHit(w, h, *target, d.damage);
            }
        }
        if (ag.flash > 0) {  // 0x00416B00's flash (models 0x00579F78[p] + frame, the second set at half size)
            const int p = a == 1 ? 0 : 1;
            const int m1 = w.exe.S16(0x00579F78 + 2 * p), m2 = w.exe.S16(0x00579F7C + 2 * p);
            const int counter = 4 - ag.flash;
            Mat4 m = Mat4::Identity();
            mx::Translate(m, d.flashX, d.flashY, -1.0f);
            int pitch = 0, yaw = 0;
            mx::DirToAngles(d.flashX, d.flashY, -1.0f, &pitch, &yaw);
            mx::RotY(m, yaw + 0x8000);
            mx::RotX(m, -pitch);
            mx::Scale(m, 0.1f, 0.1f, 0.1f);
            w.Draw(m1 + counter, m, 0xC, -1.0f, false, -1, true);
            mx::Scale(m, 0.5f, 0.5f, 0.5f);
            w.Draw(m2 + counter, m, 0xC, -1.0f, false, -1, true);
            --ag.flash;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------------------------------------------------

std::string Seconds(int frames) { return std::to_string(std::max(0, (frames + 59) / 60)); }  // (the glyph font has no ':')

constexpr float kYellow[3] = { 1.0f, 0.85f, 0.2f };
constexpr float kRed[3] = { 1.0f, 0.35f, 0.3f };
constexpr float kGrey[3] = { 0.55f, 0.55f, 0.55f };
constexpr float kGreen[3] = { 0.5f, 1.0f, 0.5f };

void DrawMoney(World::Impl& w, HordeState& h, float y) {
    for (int p = 0; p < 2; ++p) {
        if (!PlayerIn(w, p)) continue;
        const std::string s = std::to_string(p + 1) + "P  " + std::to_string(h.money[p]);
        if (p == 0) PortGlyphText(w, 130.0f, y, 0.8f, s, 1.0f, false, kYellow);
        else PortGlyphText(w, 510.0f - static_cast<float>(s.size()) * 16.0f * 0.8f, y, 0.8f, s, 1.0f, false, kYellow);
    }
}

void DrawHud(World::Impl& w, HordeState& h) {
    // (the glyphs' capitals are ~28 px tall at scale 1)
    PortGlyphText(w, 320.0f, 6.0f, 0.8f, "ROUND " + std::to_string(h.round), 1.0f, true, kYellow);
    PortGlyphText(w, 320.0f, 34.0f, 0.5f, h.timer > 0 ? "TIME " + Seconds(h.timer) : "CLEAR THE STREET", 1.0f, true,
                  h.timer > 0 && h.timer < 10 * 60 ? kRed : nullptr);
    DrawMoney(w, h, 452.0f);
    // (the barrels show their state themselves: cracked below half, gone when broken)
}

// ---------------------------------------------------------------------------------------------------------------------
// The store
// ---------------------------------------------------------------------------------------------------------------------

constexpr float kCellW = 152.0f, kCellH = 90.0f, kGridX = 16.0f, kGridY = 44.0f;  // 4 x 4 cells (44..404, above the HUD's torches)
constexpr int kGridRows = 4, kGridCols = 4;
constexpr float kModelDepth = 95.0f;  // view-space depth of the cells' models (a 10-unit object is ~67 px)
// the weapon row in the fight: small fixed icons above the ammo (P1 bottom left, P2 bottom right); a shot on one equips it
constexpr float kRowY = 346.0f, kRowSlot = 46.0f, kRowW = 42.0f, kRowH = 34.0f;
constexpr float kRowDepth = 40.0f;  // nearer than the street there (the ground is ~90 off at that screen line; the depth test)
constexpr int kWeaponModel[4] = { 0x1591, 0x109E, 0x109B, 0x1087 };  // the agent's pistol, the item models
constexpr float kWeaponIconScale[4] = { 0.6f, 0.37f, 0.4f, 0.37f };    // (about half the store's cells' size)
constexpr float kWeaponIconDy[4] = { -4.0f, 9.0f, 9.0f, 9.0f };          // the models' origins (px at kRowDepth): the guns'
                                                                         // below their barrels, the pistol's above its grip
constexpr int kWeaponIconYaw = 0x6000;                                   // the side profile
float RowX(int p, int slot) { return p == 0 ? 24.0f + static_cast<float>(slot) * kRowSlot : 616.0f - static_cast<float>(4 - slot) * kRowSlot; }
void CellModel(World::Impl& w, int model, float cx, float cy, float depth, float scale, int spin, bool stand, bool opaque = false);

// a line of the grid: the boss bar's sprites (0xB5 blue, 0xB6 red; 16 x 12 px) stretched, top-left anchored
void GridLine(World::Impl& w, float x, float y, float len, bool vertical, int sprite = 0xB5, float thick = 0.15f) {
    if (vertical) PortSprite(w, sprite, x, y, 3.0f, thick, len / 16.0f, 0, 0x5 | 0x700);
    else PortSprite(w, sprite, x, y, 3.0f, len / 16.0f, thick, 0, 0x5 | 0x700);
}

int ItemPrice(const HordeState& h, int p, int item) {
    switch (item) {
    case kPistol: return 0;
    case kShotgun: case kMachineGun: return kWeaponPrice[item];
    case kGrenade: return h.owned[p][3] ? kGrenadeRefill : kWeaponPrice[3];  // the launcher, then its rounds
    case kPistolUpg: case kShotgunUpg: case kMachineGunUpg: case kGrenadeUpg:
        return kUpgradePrice[item - kPistolUpg][std::clamp(h.level[p][item - kPistolUpg], 0, kMaxLevel - 1)];
    case kLife: return 2000;
    case kFirstAid: return 800;
    case kNewBarrel: return 1000;
    case kReinforce: return 1500 + 1000 * h.reinforce;
    case kAgentAmy: case kAgentHarry: case kAgentGoldman: return kAgentDef[item - kAgentAmy].price;
    default: return 0;
    }
}

const char* kWeaponName[4] = { "PISTOL", "SHOTGUN", "MACHINE GUN", "GRENADE" };

// what the player has of the item (text), and whether more can be bought
std::string ItemStatus(World::Impl& w, const HordeState& h, int p, int item, bool* available) {
    *available = true;
    switch (item) {
    case kPistol:
        if (h.weapon[p] == 0) { *available = false; return "EQUIPPED"; }
        return "EQUIP";
    case kShotgun: case kMachineGun:
        if (h.owned[p][item]) { *available = false; return h.weapon[p] == item ? "EQUIPPED" : "OWNED"; }
        return "LV1";
    case kGrenade:
        if (!h.owned[p][3]) return std::to_string(kGrenadeRounds) + " ROUNDS";
        if (h.grenades[p] >= GrenadeCapacity(h.level[p][3])) { *available = false; return "FULL  " + std::to_string(h.grenades[p]); }
        return "REFILL  " + std::to_string(h.grenades[p]) + " OF " + std::to_string(GrenadeCapacity(h.level[p][3]));
    case kPistolUpg: case kShotgunUpg: case kMachineGunUpg: case kGrenadeUpg: {
        const int wpn = item - kPistolUpg, lvl = h.level[p][wpn];
        if (!h.owned[p][wpn]) { *available = false; return "NOT OWNED"; }
        if (lvl >= kMaxLevel) { *available = false; return "MAX"; }
        // what the next level brings
        const int dmg = static_cast<int>(kDamageLevel[wpn][lvl + 1] * 100.0f + 0.5f);
        if (wpn == 3) return "DMG " + std::to_string(dmg) + "  CAP " + std::to_string(GrenadeCapacity(lvl + 1));
        return "DMG " + std::to_string(dmg) + "  MAG +" + std::to_string(kMagazineAdd[wpn][lvl + 1]);
    }
    case kLife:
        if (w.player[p].lives >= kMaxLives) { *available = false; return "MAX"; }
        return "";
    case kFirstAid: {  // the standing barrels back to full
        bool full = true;
        for (const Lane& l : h.lanes) full = full && (!l.alive || l.hp >= l.hpMax);
        if (full) { *available = false; return "NO DAMAGE"; }
        return "REPAIR ALL";
    }
    case kNewBarrel: {  // one destroyed barrel back
        int down = 0;
        for (const Lane& l : h.lanes) down += l.alive ? 0 : 1;
        if (down == 0) { *available = false; return "ROW COMPLETE"; }
        return std::to_string(down) + " DOWN";
    }
    case kReinforce:
        if (h.reinforce >= 3) { *available = false; return "MAX"; }
        return "BARRELS +40 HP";
    case kAgentAmy: case kAgentHarry: case kAgentGoldman:
        if (h.agents[item - kAgentAmy].owned) { *available = false; return "ON DUTY"; }
        return "";
    case kReady:
        if (h.ready[p]) { *available = false; return "WAITING"; }
        return "";
    default:
        *available = false;
        return "";
    }
}

// Equip a weapon: the original mode's item rows 0..3 (0x004EC928: magazine, effect, sound, -; damage) into
// ModesState::PlayerItems (0 = the pistol, as ItemsInit), the bought magazine on top, the magazine filled
void Equip(World::Impl& w, HordeState& h, int p, int weapon) {
    if (!w.modes) return;
    ModesState::PlayerItems& it = w.modes->pitems[p];
    const ExeImage& exe = w.exe;
    h.weapon[p] = weapon;
    it.weapon = static_cast<int8_t>(weapon);
    for (int k = 0; k < 4; ++k) it.b8[k] = B8(exe, 0x004EC928 + static_cast<uint32_t>(weapon * 8 + k));
    it.damage = F32(exe, 0x004EC92C + static_cast<uint32_t>(weapon * 8));
    const int add = kMagazineAdd[std::clamp(weapon, 0, 3)][std::clamp(h.level[p][weapon], 0, kMaxLevel)];
    if (static_cast<int8_t>(it.b8[0]) != -1) it.b8[0] = static_cast<uint8_t>(static_cast<int8_t>(it.b8[0]) + add);
    if (weapon == 0) {
        w.player[p].ammo = static_cast<int16_t>(static_cast<int8_t>(it.b8[0]));
        w.player[p].empty = 0;
        return;
    }
    w.player[p].empty = 1;  // (0x00414E40 reloads weapon 2 only when empty)
    SpecialReload(w, p);
}

// The weapon row of the fight: the owned weapons as small icons (fixed pose, no spin), the equipped one framed, the
// grenade launcher with its rounds; a player's shot landing on an icon equips it. A grenade shot spends a round; at none
// the pistol comes back.
void WeaponRow(World::Impl& w, HordeState& h) {
    for (int p = 0; p < 2; ++p) {
        if (!PlayerIn(w, p)) continue;
        World::Impl::Player& P = w.player[p];
        if (h.weapon[p] == 3 && P.firedThisFrame && --h.grenades[p] <= 0) {
            h.grenades[p] = 0;
            Equip(w, h, p, 0);
        }
        if (P.firedThisFrame) {  // the shot's screen point (+0x10 / +0x14: centred, y up)
            const float sx = 320.0f + P.aim[0], sy = 240.0f - P.aim[1];
            for (int s = 0; s < 4; ++s) {
                if (!h.owned[p][s] || s == h.weapon[p]) continue;
                const float x = RowX(p, s);
                if (sx >= x - 2.0f && sx < x + kRowW + 2.0f && sy >= kRowY - kRowH * 0.5f - 4.0f && sy < kRowY + kRowH * 0.5f + 4.0f) {
                    if (s == 3 && h.grenades[p] <= 0) break;
                    Equip(w, h, p, s);
                    w.Sound(0x121A9);
                    break;
                }
            }
        }
        for (int s = 0; s < 4; ++s) {
            if (!h.owned[p][s]) continue;
            const float x = RowX(p, s), cx = x + kRowW * 0.5f;
            const bool on = s == h.weapon[p];
            const int frame = on ? 0xB6 : 0xB5;
            const float thick = on ? 0.25f : 0.12f;
            GridLine(w, x, kRowY - kRowH * 0.5f, kRowW, false, frame, thick);
            GridLine(w, x, kRowY + kRowH * 0.5f - 2.0f, kRowW, false, frame, thick);
            GridLine(w, x, kRowY - kRowH * 0.5f, kRowH, true, frame, thick);
            GridLine(w, x + kRowW - 2.0f, kRowY - kRowH * 0.5f, kRowH, true, frame, thick);
            {  // a dimmer light for the icons (the directional diffuse 1.4 x intensity x colour saturates the gun metal)
                const WorldLight saved = w.light;
                w.LightColor(0.45f, 0.45f, 0.45f);
                w.LightIntensity(0.5f);
                CellModel(w, kWeaponModel[s], cx, kRowY + kWeaponIconDy[s], kRowDepth, kWeaponIconScale[s], kWeaponIconYaw, false, true);
                w.light = saved;
            }
            if (s == 3) PortGlyphText(w, cx, kRowY + kRowH * 0.5f + 2.0f, 0.45f, std::to_string(h.grenades[p]), 1.0f, true, h.grenades[p] > 0 ? kYellow : kRed);
        }
    }
}

void Buy(World::Impl& w, HordeState& h, int p, int item) {
    bool available = true;
    ItemStatus(w, h, p, item, &available);
    const int price = ItemPrice(h, p, item);
    if (!available || h.money[p] < price) {
        w.Sound(0x500A9);  // (the selects' refusal)
        return;
    }
    h.money[p] -= price;
    w.Sound(0x121A9);
    switch (item) {
    case kPistol:
        Equip(w, h, p, 0);
        break;
    case kShotgun: case kMachineGun:
        h.owned[p][item] = true;
        Equip(w, h, p, item);
        break;
    case kGrenade:
        h.owned[p][3] = true;
        h.grenades[p] = std::min(h.grenades[p] + kGrenadeRounds, GrenadeCapacity(h.level[p][3]));
        Equip(w, h, p, 3);
        break;
    case kPistolUpg: case kShotgunUpg: case kMachineGunUpg: case kGrenadeUpg: {
        const int wpn = item - kPistolUpg;
        ++h.level[p][wpn];
        if (h.weapon[p] == wpn) Equip(w, h, p, wpn);  // the new magazine, filled
        break;
    }
    case kLife:
        w.player[p].lives = static_cast<int16_t>(std::min(kMaxLives, w.player[p].lives + 1));
        w.player[p].livesShown = w.player[p].lives;
        break;
    case kFirstAid:
        for (Lane& l : h.lanes)
            if (l.alive) l.hp = l.hpMax;
        break;
    case kNewBarrel: {  // the destroyed lane nearest the middle
        int best = -1;
        for (int i = 0; i < kLanes; ++i)
            if (!h.lanes[i].alive && (best < 0 || std::abs(i - kLanes / 2) < std::abs(best - kLanes / 2))) best = i;
        if (best >= 0) {
            h.lanes[best].alive = true;
            h.lanes[best].hp = h.lanes[best].hpMax;
            h.lanes[best].yaw = w.Rand(0x48000008) & 0xFFFF;
        }
        break;
    }
    case kReinforce:
        ++h.reinforce;
        h.barrelHpMax += 40;
        for (Lane& l : h.lanes) {
            l.hpMax = static_cast<float>(h.barrelHpMax);
            if (l.alive) l.hp = std::min(l.hp + 40.0f, l.hpMax);
        }
        break;
    case kAgentAmy: case kAgentHarry: case kAgentGoldman:
        h.agents[item - kAgentAmy].owned = true;
        break;
    default:
        break;
    }
    if (h.debug) fprintf(stderr, "horde: frame %d player %d buys item %d for %d (left %d)\n", w.frame, p, item, price, h.money[p]);
}

// a model of the original mode's items in a store cell: view space, at the cell's centre, turning
void CellModel(World::Impl& w, int model, float cx, float cy, float depth, float scale, int spin, bool stand, bool opaque) {
    const float x = (cx - 320.0f) * depth / w.focal, y = (240.0f - cy) * depth / w.focal;
    Mat4 m = Mat4::Identity();
    mx::Translate(m, x, y, -depth);
    if (stand) mx::RotX(m, 0x0800);
    mx::RotY(m, spin);
    mx::Scale(m, scale, scale, scale);
    w.Draw(model, m, opaque ? 0xC : 8, opaque ? -1.0f : 0.999f, false, -1, true);  // (0xC: the effects' mode, as the flashes)
}

void StoreDraw(World::Impl& w, HordeState& h) {
    const ExeImage& exe = w.exe;
    // the street is not drawn (0x009A5900 bit 5, as under the chapter cards): the page is the sky, the models and the
    // sprites
    PortGlyphText(w, 320.0f, 2.0f, 0.75f, "ROUND " + std::to_string(h.round) + " CLEAR", 1.0f, true, kYellow);
    PortGlyphText(w, 320.0f, 26.0f, 0.42f, "NEXT ROUND IN " + Seconds(h.timer) + "   TRIGGER BUYS   START WHEN READY", 1.0f, true,
                  h.timer < 10 * 60 ? kRed : kGrey);
    for (int p = 0; p < 2; ++p) {  // the money, beside the title
        if (!PlayerIn(w, p)) continue;
        const std::string s = std::to_string(p + 1) + "P " + std::to_string(h.money[p]);
        if (p == 0) PortGlyphText(w, kGridX, 6.0f, 0.6f, s, 1.0f, false, kYellow);
        else PortGlyphText(w, kGridX + kGridCols * kCellW - static_cast<float>(s.size()) * 16.0f * 0.6f, 6.0f, 0.6f, s, 1.0f, false, kYellow);
    }
    // the grid's lines
    const float gridW = kGridCols * kCellW, gridH = kGridRows * kCellH;
    for (int r = 0; r <= kGridRows; ++r) GridLine(w, kGridX, kGridY + static_cast<float>(r) * kCellH, gridW, false);
    for (int c = 0; c <= kGridCols; ++c) GridLine(w, kGridX + static_cast<float>(c) * kCellW, kGridY, gridH, true);
    const int spin = static_cast<int>(w.frame * 0x120) & 0xFFFF;
    for (int item = 0; item < kItems; ++item) {
        const int col = item % kGridCols, row = item / kGridCols;
        const float x = kGridX + static_cast<float>(col) * kCellW, y = kGridY + static_cast<float>(row) * kCellH;
        const float cx = x + kCellW * 0.5f;
        bool pointed = false;
        for (int p = 0; p < 2; ++p) pointed = pointed || h.pointerRow[p] == item;
        if (pointed) {  // the pointed cell's frame in the bar's red
            GridLine(w, x, y, kCellW, false, 0xB6, 0.3f);
            GridLine(w, x, y + kCellH - 3.0f, kCellW, false, 0xB6, 0.3f);
            GridLine(w, x, y, kCellH, true, 0xB6, 0.3f);
            GridLine(w, x + kCellW - 3.0f, y, kCellH, true, 0xB6, 0.3f);
        }
        // the price / state, for the pointing player (else player 1)
        int p = 0;
        for (int q = 0; q < 2; ++q)
            if (h.pointerRow[q] == item) p = q;
        // the picture, centred in the upper 50 px of the cell (the pickup models' origins are at their bases: their
        // point is lower; the guns' and the barrel's at their centres)
        const float my = y + 24.0f, mb = y + 36.0f, d = kModelDepth;
        switch (item) {
        case kPistol: CellModel(w, 0x1591, cx, my - 2.0f, d, 0.9f, spin, false); break;
        case kShotgun: CellModel(w, 0x109E, cx, my, d, 0.85f, spin, false); break;
        case kMachineGun: CellModel(w, 0x109B, cx, my + 7.0f, d, 0.85f, spin, false); break;
        case kGrenade: CellModel(w, 0x1087, cx, my, d, 0.85f, spin, false); break;
        case kPistolUpg: {  // the chamber item for the pistol, the power ups for the others (their levels)
            const int lvl = std::min(h.level[p][0], 2);
            CellModel(w, 0x107E + lvl * 2, cx, mb, d, 0.7f, spin, true);
            CellModel(w, 0x107F + lvl * 2, cx, mb, d, 0.7f, spin, true);
            break;
        }
        case kShotgunUpg: case kMachineGunUpg: case kGrenadeUpg: {
            const int lvl = std::min(h.level[p][item - kPistolUpg], 2);
            CellModel(w, 0x10A5 + lvl * 2, cx, mb, d, 0.6f, spin, true);
            CellModel(w, 0x10A6 + lvl * 2, cx, mb, d, 0.6f, spin, true);
            break;
        }
        case kLife: CellModel(w, 0x109A, cx, mb, d, 0.9f, spin, true); break;
        case kFirstAid: CellModel(w, 0x1097, cx, mb, d, 0.65f, spin, true); break;
        case kNewBarrel: CellModel(w, kBarrelModel, cx, my + 2.0f, d, 0.55f, spin, false); break;
        case kReinforce: CellModel(w, kBarrelCracked, cx, my + 2.0f, d, 0.55f, spin, false); break;
        case kAgentAmy: case kAgentHarry: case kAgentGoldman: {
            Agent& ag = h.agents[item - kAgentAmy];
            if (!ag.portrait) ag.portrait = MakeBody(w, kAgentDef[item - kAgentAmy].kind, true);
            EnemyTask& b = *ag.portrait;
            // the body at the cell: its chest (root + ~14) at the cell's picture point, facing the camera
            const float depth = kModelDepth * 1.5f;
            const float local[3] = { (cx - 320.0f) * depth / w.focal, (240.0f - (my + 2.0f)) * depth / w.focal - 14.5f, -depth };
            mx::Transform(w.cam.inverse, local, b.pos);
            int pitch = 0, yaw = 0;
            DirToAngles(b.pos[0] - w.cam.pos[0], 0.0f, b.pos[2] - w.cam.pos[2], &pitch, &yaw);  // (as EnemyTurnTo: facing the point)
            b.rot[1] = yaw & 0xFFFF;
            ++b.anim.counter[0];
            w.AnimDraw(b);
            break;
        }
        default:
            break;
        }
        // the name (the item select's name sprites where they exist), the price, the state: three lines under the picture
        const float ny = y + 46.0f, ls = 0.5f, ns = ny - 4.0f;  // (the name sprites are centred on their point)
        bool available = true;
        const std::string st = ItemStatus(w, h, p, item, &available);
        switch (item) {
        case kPistol: PortGlyphText(w, cx, ny, 0.55f, "PISTOL", 1.0f, true); break;
        case kShotgun: PortSprite(w, 0x5F9, cx, ns, 1.0f, ls, ls, 0, 0x02); break;
        case kMachineGun: PortSprite(w, 0x5FA, cx, ns, 1.0f, ls, ls, 0, 0x02); break;
        case kGrenade: PortSprite(w, 0x5FB, cx, ns, 1.0f, ls, ls, 0, 0x02); break;
        case kPistolUpg: case kShotgunUpg: case kMachineGunUpg: case kGrenadeUpg: {
            const int wpn = item - kPistolUpg, lvl = std::min(h.level[p][wpn], kMaxLevel);
            const std::string name = std::string(kWeaponName[wpn]) + (lvl < kMaxLevel ? " LV" + std::to_string(lvl + 2) : " MAX");
            PortGlyphText(w, cx, ny, 0.55f, name, 1.0f, true);
            break;
        }
        case kLife: PortGlyphText(w, cx, ny, 0.55f, "LIFE UP", 1.0f, true); break;
        case kFirstAid: PortSprite(w, 0x617, cx, ns, 1.0f, ls, ls, 0, 0x02); break;
        case kNewBarrel: PortGlyphText(w, cx, ny, 0.55f, "NEW BARREL", 1.0f, true); break;
        case kReinforce: PortGlyphText(w, cx, ny, 0.55f, "REINFORCE", 1.0f, true); break;
        case kAgentAmy: case kAgentHarry: case kAgentGoldman:
            PortGlyphText(w, cx, ny, 0.55f, std::string("AGENT ") + kAgentDef[item - kAgentAmy].name, 1.0f, true);
            break;
        case kReady: {
            bool waiting = false;
            for (int q = 0; q < 2; ++q) waiting = waiting || (PlayerIn(w, q) && h.ready[q]);
            PortGlyphText(w, cx, y + 28.0f, 0.9f, waiting ? "WAITING" : "READY", 1.0f, true, kGreen);
            PortGlyphText(w, cx, y + 62.0f, 0.45f, "NEXT ROUND", 1.0f, true, kGrey);
            break;
        }
        default: break;
        }
        if (item == kReady) continue;
        const float py = y + 61.0f, sy = y + 76.0f;
        const int price = ItemPrice(h, p, item);
        if (!available) {
            PortGlyphText(w, cx, py, 0.55f, st, 1.0f, true, kGrey);
        } else {
            if (price > 0) PortGlyphText(w, cx, py, 0.55f, std::to_string(price), 1.0f, true, h.money[p] >= price ? kYellow : kRed);
            else PortGlyphText(w, cx, py, 0.55f, "FREE", 1.0f, true, kYellow);
            if (!st.empty()) PortGlyphText(w, cx, sy, 0.45f, st, 1.0f, true, kGrey);
        }
    }
    // the pointers: the players' crosshairs (0x00579F58[p * 4 + style], as the HUD's 0x004169C0)
    for (int p = 0; p < 2; ++p) {
        if (!PlayerIn(w, p) || w.input[p].kind != 1) continue;
        const int id = exe.S16(0x00579F58 + static_cast<uint32_t>(p * 4) * 2);
        PortSprite(w, id, static_cast<float>(w.input[p].x), static_cast<float>(w.input[p].y), 0.9f, 1.0f, 1.0f, 0, 0xA);
    }
}

void StoreFrame(World::Impl& w, HordeState& h) {
    w.canShoot = 0;
    w.flags5900 |= 0x20;  // the street is not drawn (TaskStageDraw, as under the chapter cards)
    w.hordeBlackout = true;  // nor the sky and the rain
    bool allReady = true;
    for (int p = 0; p < 2; ++p) {
        h.pointerRow[p] = -1;
        if (!PlayerIn(w, p)) continue;
        if (!h.ready[p]) allReady = false;
        const uint32_t trig = p == 0 ? 0x4u : 0x40000u, start = p == 0 ? 0x8u : 0x80000u;  // 0x009C9028: 4 trigger, 8 start
        if (w.input[p].kind == 1) {
            const float x = static_cast<float>(w.input[p].x) - kGridX, y = static_cast<float>(w.input[p].y) - kGridY;
            if (x >= 0 && y >= 0) {
                const int col = static_cast<int>(x / kCellW), row = static_cast<int>(y / kCellH);
                if (col < kGridCols && row < kGridRows) h.pointerRow[p] = row * kGridCols + col;
            }
        }
        if (w.pressed & trig) {
            if (h.pointerRow[p] == kReady) h.ready[p] = true;
            else if (h.pointerRow[p] >= 0) Buy(w, h, p, h.pointerRow[p]);
        }
        if (w.pressed & start) h.ready[p] = true;
    }
    if (--h.timer <= 0 || allReady) {
        for (int p = 0; p < 2; ++p) h.ready[p] = false;
        StartRound(w, h);
        return;
    }
    StoreDraw(w, h);
}

// ---------------------------------------------------------------------------------------------------------------------
// The mode's task
// ---------------------------------------------------------------------------------------------------------------------

void CountAlive(World::Impl& w, HordeState& h) {
    int n = 0;
    for (Task* t : w.ts.Live()) {
        if (!t->data) continue;
        const EnemyTask* e = dynamic_cast<const EnemyTask*>(t->data.get());
        if (e && e->record.type == kHordeRecordType && (e->flags34 & 1)) {
            ++n;
            if (h.debug && w.frame % 120 == 0)
                fprintf(stderr, "horde: frame %d enemy kind %X beh %d sub %d pos %.1f %.1f target %d rank %d limit %d attackSlot %d taken %d %d flags34 %08X 136C %08X hp %d\n",
                        w.frame, e->anim.kind, e->behaviour, e->sub, e->pos[0], e->pos[2], e->target, e->c131D, e->attackLimit, w.attackSlot,
                        w.targetTaken[0], w.targetTaken[1], e->flags34, e->flags136C, e->hp);
        }
    }
    h.alive = n;
}

void Fight(World::Impl& w, HordeState& h) {
    CountAlive(w, h);
    PoolsFrame(w, h);
    const int bats = BatsAlive(w);
    if (h.bossRound) {
        BossFrame(w, h);
        if (BossOver(h)) h.timer = 0;
        else if (h.timer > 0) --h.timer;
        if (h.timer <= 0 && h.alive == 0 && bats == 0 && BossOver(h)) {
            h.bossRound = false;
            w.bossBar = -1.0f;
        }
    } else if (h.timer > 0) {
        --h.timer;
        if (--h.spawnTimer <= 0 && h.alive + static_cast<int>(h.pools.size()) < RoundCap(h.round)) {
            SpawnOne(w, h);
            h.spawnTimer = RoundSpawnInterval(h.round);
        }
        if (h.round >= 3 && --h.batTimer <= 0 && bats == 0) {  // a bat group once the last is gone
            SpawnBats(w, h, BatCount(h.round));
            h.batTimer = BatInterval(h.round);
        }
    }
    if (h.timer <= 0 && h.alive == 0 && bats == 0 && h.pools.empty() && !h.bossRound) {
        h.phase = 2;
        h.timer = kBuyFrames;
        for (int p = 0; p < 2; ++p) {
            if (PlayerIn(w, p)) h.money[p] += RoundBonus(h.round);
            h.ready[p] = false;
        }
        w.Sound(0x10000024);
        if (h.debug) fprintf(stderr, "horde: round %d clear at frame %d (spawned %d, killed %d)\n", h.round, w.frame, h.spawnedThisRound, h.killedThisRound);
    }
}

void TaskHorde(Task&) {
    World::Impl& w = *CurrentWorld();
    HordeState* hp = Ptr(w);
    if (!hp) return;
    HordeState& h = *hp;
    if (w.gameMode != 6) return;
    if (h.phase == 0) {
        if (w.runner->skippable && !w.runner->skipping) w.skipRequest = true;  // the opening cutscene, skipped
        if (w.runner->camera.modeCur == 2 && w.canShoot != 0 && !w.runner->skipping) TakeOver(w, h);
        return;
    }
    if (h.phase == 1 && h.debug && getenv("HOTD2_HORDE_STORE") && w.frame == static_cast<int>(h.takeoverFrame) + 120) {
        // development: the store at once, with money
        h.money[0] = h.money[1] = 20000;
        h.phase = 2;
        h.timer = kBuyFrames;
    }
    if (h.phase == 1) {
        Fight(w, h);
        if (h.bossRound) DrawWings(w, h);
        DrawLanes(w, h);
        AgentsFrame(w, h);
        WeaponRow(w, h);
        DrawHud(w, h);
    } else if (h.phase == 2) {
        StoreFrame(w, h);
        if (h.phase == 2) return;  // (the store page covers the street)
        DrawLanes(w, h);
        DrawHud(w, h);
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------------------------------------------------

bool HordeActive(World::Impl& w) { return w.horde != nullptr; }

HordeRunSnapshot World::PortHordeRun() const {
    auto& w=*impl_;
    const HordeState* h=Ptr(w);
    HordeRunSnapshot r;
    if(!h||w.gameMode!=6)return r;
    r.active=h->phase>0; r.debug=h->debug;
    r.rounds=std::max(0,h->round-(h->phase==2?0:1));
    r.score=int(std::min<int64_t>(2000000000,int64_t(std::max(0,w.player[0].score))+std::max(0,w.player[1].score)));
    r.players=std::max(w.players,w.portRunParticipants); r.difficulty=w.cfg.difficulty;
    return r;
}

bool HordeSpecialWeapon(World::Impl& w, int p) {
    HordeState* h = Ptr(w);
    return h && p >= 0 && p < 2 && h->weapon[p] != 0;
}

// The mode select's entry: as the Arcade entry's confirm (0x009A21C0 = 1, the credits 0x00406F60(0x00496B70(0)), game type
// 0), plus the flag GameStartFrame reads
void HordeMenuConfirm(World::Impl& w) {
    w.front.menuConfirmed = true;
    if (w.cfg.creditSetting == -1) {
        w.cfg.freePlay = true;
        w.cfg.credits = 0;
    } else {
        w.cfg.freePlay = false;
        w.cfg.credits = w.cfg.creditSetting + 1;
    }
    w.front.gameType = 0;
    w.front.hordeStart = true;
    w.Sound(0x121A9);
}

void HordeGameStart(World::Impl& w) {
    auto h = std::make_shared<HordeState>();
    h->debug = getenv("HOTD2_HORDE_DEBUG") != nullptr;
    if (const char* r = h->debug ? getenv("HOTD2_HORDE_ROUND") : nullptr) h->round = std::max(0, atoi(r) - 1);  // development: start at this round
    w.horde = h;
    w.NewTask<HordeTaskData>(TaskHorde, 0x48000000);
}

void HordeEnemyKilled(World::Impl& w, EnemyTask& e, int p, bool head) {
    HordeState* h = Ptr(w);
    if (!h || e.record.type != kHordeRecordType || p < 0 || p > 1) return;
    h->money[p] += KillMoney(h->round, e.hpMax, head);
    ++h->killedThisRound;
}

// a bat's dive (0x0043D4F0): the offset from the camera to a barrel of the row (its bite lands there)
void HordeBatDive(World::Impl& w, float diveOff[2]) {
    HordeState* h = Ptr(w);
    if (!h) return;
    int alive[kLanes], n = 0;
    for (int i = 0; i < kLanes; ++i)
        if (h->lanes[i].alive) alive[n++] = i;
    const float* at = n > 0 ? h->lanes[alive[w.Rand(0x48000020) % n]].pos : h->origin;
    diveOff[0] = at[0] - w.cam.pos[0];
    diveOff[1] = at[2] - w.cam.pos[2];
}

// a thrown axe's barrel: the standing one nearest the target player's side (the hand spreads them); -1 with none
int ThrowLane(const HordeState& h, int target, int hand) {
    const int want = (target == 1 ? 3 : 1) + (hand == 5 ? 0 : 1);
    int best = -1;
    for (int i = 0; i < kLanes; ++i)
        if (h.lanes[i].alive && (best < 0 || std::abs(i - want) < std::abs(best - want))) best = i;
    return best;
}

// a thrown weapon's aim point (0x0045A070): the barrel's top (none standing: the original's point before the view)
void HordeThrowAim(World::Impl& w, int target, int hand, float aim[3]) {
    HordeState* h = Ptr(w);
    if (!h) return;
    const int lane = ThrowLane(*h, target, hand);
    if (lane < 0) return;
    const Lane& l = h->lanes[lane];
    aim[0] = l.pos[0];
    aim[1] = l.pos[1] + 4.0f;
    aim[2] = l.pos[2];
}

// its arrival: the barrel's hp (none standing: the player's life, as the original)
void HordeThrowStrike(World::Impl& w, int target, int hand) {
    HordeState* h = Ptr(w);
    if (!h) return;
    const int lane = ThrowLane(*h, target, hand);
    if (lane < 0) {
        w.PlayerTakeHit(target, 1, 4);
        return;
    }
    Lane& l = h->lanes[lane];
    l.hp -= 14.0f + static_cast<float>(h->round) / 2.0f;
    l.shake = 1.0f;
    w.Sound(0x1D16A9);
    if (h->debug) fprintf(stderr, "horde: frame %d barrel %d hit by an axe: hp %.0f\n", w.frame, lane, l.hp);
    if (l.hp <= 0) BreakLane(w, *h, lane);
}

// the bite at the dive's end: the nearest standing barrel's hp (none: a player's life, as the original)
void HordeBatStrike(World::Impl& w) {
    HordeState* h = Ptr(w);
    if (!h) return;
    int best = -1;
    for (int i = 0; i < kLanes; ++i)
        if (h->lanes[i].alive && (best < 0 || h->lanes[i].hp < h->lanes[best].hp)) best = i;
    if (best < 0) {
        for (int p = 0; p < 2; ++p)
            if (w.player[p].state == 5) w.PlayerTakeHit(p, 1, 10);
        return;
    }
    Lane& l = h->lanes[best];
    l.hp -= 8.0f + static_cast<float>(h->round) / 2.0f;
    l.shake = 1.0f;
    w.Sound(0x1D16A9);
    if (h->debug) fprintf(stderr, "horde: frame %d barrel %d bitten by a bat: hp %.0f\n", w.frame, best, l.hp);
    if (l.hp <= 0) BreakLane(w, *h, best);
}

// the bought damage: the weapon's own factor (the grenade's 4.0 of the item rows) x the power ups
void HordeShotDamage(World::Impl& w, int p, EnemyShotState& s) {
    HordeState* h = Ptr(w);
    if (!h || p < 0 || p > 1) return;
    const float f = WeaponFactor(w, *h, p);
    if (f == 1.0f) return;
    s.itemDamage = true;
    s.damageFactor = f;
    s.damageBoost = 0.0f;
}

// Behaviour kHordeBehaviour: walk to the nearest intact barrel (the walk-in motions, as 0x00457220), attack it with the
// attack tables (as 0x00455A40: entry motion / approach motion / range / hit tick), back off to the attack's start (as
// 0x00455C30) and again; with every barrel of the row broken: behaviour 2 towards the players. Scripted enemies found
// at the take-over (kDespawnRecordType) leave.
// The walk's pace: the engine advances the animation a tick a frame (0x00454AE0); pace / 256 ticks a frame here, by
// extra ticks or held frames (+0x34 bit 14 holds the counter). Not during a blend (the engine's own rate).
void WalkPace(EnemyTask& e, int pace) {
    if (e.anim.flags[0] & 1) {
        e.flags34 &= ~0x4000u;
        return;
    }
    if (e.timer1338 < 0) e.timer1338 = 0;  // (the collision pass 0x00454B10 decrements it)
    e.timer1338 += pace;
    if (e.timer1338 < 256) {
        e.flags34 |= 0x4000;
        return;
    }
    e.flags34 &= ~0x4000u;
    e.timer1338 -= 256;
    while (e.timer1338 >= 256) {
        e.timer1338 -= 256;
        ++e.anim.counter[0];
    }
}

// the walk-in motions 2 / 3 of the set (0x00457220: 3 once hit, +0x34 bit 27), the root motion carries, towards (x, z)
void WalkMotion(World::Impl& w, HordeState& h, EnemyTask& e, float x, float z) {
    const int m = w.EnemyMotionSet(e, 2 + ((e.flags34 >> 27) & 1));
    if (e.anim.motion[0] != m) {
        const int r = w.Rand(0x48000010);
        w.EnemySetMotion(e, m, r % w.MotionTicks(m), 5);
        w.Sound(0x1917A9);
    } else {
        WalkPace(e, RoundPace(static_cast<int>(e.walked), h.round));
    }
    w.EnemyTurnTo(e, x, z, 0x100);
}

bool HoldsAxe(const EnemyTask& e) { return e.anim.nodes[5].model == 0x1ECE || e.anim.nodes[8].model == 0x1ECA; }

// a player to throw at / hit: a random one of those in the game (0x00455DE0 needs the attack slots, which the mode
// does not keep)
int PickPlayer(World::Impl& w) {
    const bool in0 = PlayerIn(w, 0), in1 = PlayerIn(w, 1);
    if (in0 && in1) return w.Rand(0x48000016) & 1;
    return in1 ? 1 : 0;
}

void HitPlayers(World::Impl& w, int kind) {
    for (int p = 0; p < 2; ++p)
        if (PlayerIn(w, p)) w.PlayerTakeHit(p, 1, kind);
    if (HordeState* h = Ptr(w); h && h->debug) fprintf(stderr, "horde: frame %d the players hit (kind %d)\n", w.frame, kind);
}

// Zeal (skeleton 0x45, the stage-1 boss's rider; its own flight here): hovers over the street, charges a barrel in a
// zigzag and strikes it (motion 0x413), is staggered by enough hits during a charge, retreats; its hits are taken as
// 0x0049D640 does (the hit node, blood, 120 / 10 points, 30 damage x the bought factors); at 0 hp it falls and the
// round ends early with the bonus. +0x34 bit 8 keeps Enemy_ApplyShot off it.
void ZealBehaviour(World::Impl& w, HordeState& h, EnemyTask& e) {
    const ExeImage& exe = w.exe;
    const Spawn& sp = SpawnOf(e);
    const int idle = exe.S16(0x00570BE4), attack = exe.S16(0x00570BF4), taunt = exe.S16(0x00570BEC);
    auto atEnd = [&] { return e.anim.tick[0] >= w.MotionTicks(e.anim.motion[0]) - 1; };
    // (the boss skeletons face the other way round: 0x0049DE60's yaw is Cam_DirToAngles of self - target)
    auto facePlayers = [&] { e.rot[1] = YawTowards(h.origin[0], h.origin[2], e.pos[0], e.pos[2]); };
    auto hoverPoint = [&](float out[3]) {
        StreetPoint(h, kHoverDistance, std::sin(h.hoverT) * 22.0f, out);
        int mat = 0;
        out[1] = w.FloorHeight(out[0], out[1] + 30.0f, out[2], &mat) + kHoverHeight + std::sin(h.hoverT * 1.7f) * 3.0f;
    };
    auto stagger = [&](uint32_t site) {
        const int r = w.Rand(site) & 1;
        w.AnimBlendTo(e, exe.S16(0x00570BFC + static_cast<uint32_t>(r) * 2), 0, 3);
    };
    if (e.sub >= 1 && e.sub <= 5) {  // the shots of this frame (+0x190: Enemy_CollectShots clears the +0x34 bits, not these)
        for (int p = 0; p < 2; ++p) {
            const uint32_t bit = 1u << (p + 1);
            const int node = e.hitNode[p];
            if (node != 0) {
                w.AddScore(p, node == 2 ? 0x78 : 10);
                w.SpawnNodeBlood(e, node);  // 0x00407200
                float dmg = w.playersInGame != 2 ? 30.0f : 25.0f;
                dmg *= std::min(WeaponFactor(w, h, p), 2.0f);  // (the bought weapons tell, but a boss is not a magazine's work)
                e.hp = static_cast<int16_t>(e.hp - static_cast<int>(dmg));
                if (e.sub == 2) ++e.counter1334;
                if (h.debug) fprintf(stderr, "horde: frame %d Zeal hit by P%d at node %d: hp %d\n", w.frame, p + 1, node, e.hp);
                ++w.player[p].hits;
                h.money[p] += 5;
            }
            e.hitNode[p] = 0;
            e.anim.nodes[node & 31].flags &= ~bit & ~8u;
        }
        e.flags34 &= ~0xEu;
        w.bossBar = std::max(0.0f, static_cast<float>(e.hp) / static_cast<float>(std::max<int>(e.hpMax, 1)));
        if (e.hp <= 0) {  // dies: the fall, the bonus, the round ends early (the knight leaves)
            stagger(0x48000030);
            w.Sound(0x2818A9);
            w.bossBar = 0.0f;
            e.sub = 6;
            e.counter1334 = 0;
            for (int p = 0; p < 2; ++p)
                if (PlayerIn(w, p)) h.money[p] += BossKillBonus(h.round);
            h.bossOver = h.bossBonus = true;  // (the knight leaves on its own: Enemy_Despawn ends the current task)
            if (h.debug) fprintf(stderr, "horde: frame %d Zeal killed (bonus %d)\n", w.frame, BossKillBonus(h.round));
            return;
        }
    }
    if (h.bossOver && !h.bossBonus && e.sub >= 1 && e.sub <= 5) {  // the knight's blow landed: it flies off
        w.AnimBlendTo(e, idle, 0, 3);
        e.counter1334 = 0;
        e.sub = 7;
    }
    switch (e.sub) {
    case 0:
        e.anim.drawFlags &= ~2u;  // no root motion: the flight places it
        e.flags34 |= 0x20000;     // no floor snap (0x00454B10)
        e.flags136C &= ~0x60000000u;  // not pushed by the actors / the meshes
        for (EnemyNode& n : e.anim.nodes) n.radius *= 3.0f;  // its hit spheres are made for the original's close fight (0.7..1.5)
        e.radius = 14.0f;
        w.AnimSetMotion(e, idle);
        e.hp = e.hpMax = static_cast<int16_t>(BossHp(h.round));  // (Enemy_Create's 0x0040A8B0 caps the record hp)
        e.counter1334 = 150;
        e.sub = 1;
        [[fallthrough]];
    case 1: {  // the hover, then a charge at a standing barrel (none left: at the players)
        h.hoverT += 0.02f;
        float at[3];
        hoverPoint(at);
        for (int k = 0; k < 3; ++k) e.pos[k] += (at[k] - e.pos[k]) * 0.08f;
        facePlayers();
        if (--e.counter1334 > 0) return;
        int alive[kLanes], n = 0;
        for (int i = 0; i < kLanes; ++i)
            if (h.lanes[i].alive) alive[n++] = i;
        e.counter1330 = n > 0 ? alive[w.Rand(0x48000031) % n] : -1;
        if (h.debug) fprintf(stderr, "horde: frame %d Zeal charges lane %d (%d standing) from %.1f %.1f %.1f\n", w.frame, e.counter1330, n, e.pos[0], e.pos[1], e.pos[2]);
        memcpy(e.jumpStart, e.pos, 12);
        if (e.counter1330 >= 0) {
            const Lane& l = h.lanes[e.counter1330];
            for (int k = 0; k < 3; ++k) e.jumpTarget[k] = l.pos[k] + h.fwd[k] * 9.0f;
            e.jumpTarget[1] = l.pos[1] + 7.0f;
        } else {
            for (int k = 0; k < 3; ++k) e.jumpTarget[k] = h.origin[k] + h.fwd[k] * 12.0f;
            e.jumpTarget[1] = h.origin[1] + 8.0f;
        }
        e.walked = 0;
        e.counter1334 = 0;  // the hits of the charge
        w.AnimBlendTo(e, taunt, 0, 3);
        w.Sound(0x3018A9);
        e.sub = 2;
        return;
    }
    case 2: {  // the zigzag charge; three hits during it stagger it
        if (e.counter1334 >= 3) {
            stagger(0x48000033);
            w.Sound(0x2A18A9);
            e.sub = 4;
            return;
        }
        e.walked += 1.0f;
        const float t = std::min(e.walked / static_cast<float>(kChargeFrames), 1.0f);
        const float zig = std::sin(t * 3.14159f * 3.0f) * 16.0f * (1.0f - t);
        for (int k = 0; k < 3; ++k) e.pos[k] = e.jumpStart[k] + (e.jumpTarget[k] - e.jumpStart[k]) * t + h.right[k] * zig;
        e.rot[1] = YawTowards(e.jumpTarget[0], e.jumpTarget[2], e.pos[0], e.pos[2]);
        if (e.anim.motion[0] == taunt && atEnd()) w.AnimBlendTo(e, idle, 0, 3);
        if (t < 1.0f) return;
        w.AnimBlendTo(e, attack, 0, 4);
        e.f1354 = 0;  // (+0x131E is the attack order's: not a flag to keep)
        e.sub = 3;
        return;
    }
    case 3: {  // the strike at the attack's middle tick
        if (e.f1354 == 0 && e.anim.tick[0] >= w.MotionTicks(e.anim.motion[0]) / 2) {
            e.f1354 = 1;
            if (e.counter1330 < 0 || !h.lanes[e.counter1330].alive) {  // its barrel went meanwhile: the nearest standing one
                e.counter1330 = -1;
                float bd = 0;
                for (int i = 0; i < kLanes; ++i) {
                    if (!h.lanes[i].alive) continue;
                    const float d = Dist2D(e.pos, h.lanes[i].pos);
                    if (e.counter1330 < 0 || d < bd) {
                        e.counter1330 = i;
                        bd = d;
                    }
                }
            }
            if (e.counter1330 >= 0 && h.lanes[e.counter1330].alive) {
                Lane& l = h.lanes[e.counter1330];
                l.hp -= static_cast<float>(sp.barrelDamage + h.round / 2);
                l.shake = 1.0f;
                w.Sound(0x1D16A9);
                if (h.debug) fprintf(stderr, "horde: frame %d barrel %d struck by Zeal: hp %.0f\n", w.frame, e.counter1330, l.hp);
                if (l.hp <= 0) BreakLane(w, h, e.counter1330);
            } else {
                HitPlayers(w, 4);
            }
            w.shakeTimer = 0x18;
        }
        if (!atEnd()) return;
        w.AnimBlendTo(e, idle, 0, 3);
        memcpy(e.jumpStart, e.pos, 12);
        e.walked = 0;
        e.sub = 5;
        return;
    }
    case 4:  // staggered: thrown back a little, then the retreat
        for (int k = 0; k < 3; ++k) e.pos[k] += h.fwd[k] * 0.5f;
        if (!atEnd()) return;
        w.AnimBlendTo(e, idle, 0, 3);
        memcpy(e.jumpStart, e.pos, 12);
        e.walked = 0;
        e.sub = 5;
        return;
    case 5: {  // back to the hover point
        e.walked += 1.0f;
        const float t = std::min(e.walked / static_cast<float>(kRetreatFrames), 1.0f);
        float at[3];
        hoverPoint(at);
        for (int k = 0; k < 3; ++k) e.pos[k] = e.jumpStart[k] + (at[k] - e.jumpStart[k]) * t;
        facePlayers();
        if (t < 1.0f) return;
        e.counter1334 = std::max(40, 80 + w.Rand(0x48000032) % 60 - h.round);
        e.sub = 1;
        return;
    }
    case 6: {  // dying: the fall to the street, then gone
        ++e.counter1334;
        int mat = 0;
        const float floor = w.FloorHeight(e.pos[0], e.pos[1] + 30.0f, e.pos[2], &mat) + 1.0f;
        if (e.pos[1] > floor) e.pos[1] = std::max(floor, e.pos[1] - 0.45f);
        if (e.counter1334 == 40) w.SpawnNodeBlood(e, 2);
        if (e.counter1334 >= 150) w.EnemyDespawn(e);
        return;
    }
    case 7:  // flies up and away down the street
        ++e.counter1334;
        for (int k = 0; k < 3; ++k) e.pos[k] += h.fwd[k] * 1.6f;
        e.pos[1] += 0.5f;
        e.rot[1] = YawTowards(e.pos[0] + h.fwd[0], e.pos[2] + h.fwd[2], e.pos[0], e.pos[2]);
        if (e.counter1334 >= 150) w.EnemyDespawn(e);
        return;
    default:
        e.sub = 0;
        return;
    }
}

// Kuarl (skeleton 0x44, the stage-1 boss's mount; 0x0048FD90: node 2 hidden): walks the street down (0x393) so slowly
// that it reaches the row as the timer ends - the walk's root travel is scaled back each frame and the animation held
// to a fraction of its rate - then its attack (row 0 of 0x005703E0: 0x383, the hit at its tick) breaks every barrel
// and takes the players' last life. Never hit (+0x34 bit 8).
void KnightBehaviour(World::Impl& w, HordeState& h, EnemyTask& e) {
    const ExeImage& exe = w.exe;
    float goal[3];
    StreetPoint(h, kBarrelDistance + 46.0f, 0.0f, goal);  // the attack row's range (50) covers the row from here
    const int tick = e.anim.tick[0], mo = e.anim.motion[0];
    switch (e.sub) {
    case 0:
        w.AnimSetMotion(e, 0x393);
        e.anim.nodes[2].model = 0;
        e.anim.nodes[2].radius = 0;
        e.walkDistance = 0.3f;  // the walk's travel an animated frame (measured)
        memcpy(e.savedPos, e.pos, 12);
        e.sub = 1;
        return;
    case 1: {
        if (mo == 0x393 && (tick == 0x37 || tick == 0x81 || tick == 0xCF || tick == 0x11A) && e.soundTick != tick) {
            e.soundTick = static_cast<int16_t>(tick);
            w.Sound(0x218A9);
            w.shakeTimer = 0x12;
        }
        const float moved = Dist2D(e.pos, e.savedPos);  // the last frame's root travel (0 when held)
        if (moved > 0.001f) e.walkDistance = e.walkDistance * 0.9f + moved * 0.1f;
        const float d = (e.pos[0] - goal[0]) * h.fwd[0] + (e.pos[2] - goal[2]) * h.fwd[2];  // left to the goal
        const float want = h.timer > 0 ? d / static_cast<float>(h.timer) : e.walkDistance;  // a frame
        const float k = e.walkDistance > 0.001f ? std::clamp(want / e.walkDistance, 0.0f, 1.0f) : 1.0f;
        const float rate = std::clamp(k * 2.0f, 0.25f, 1.0f);  // the animation's rate; the rest by the travel's scale
        for (int c = 0; c < 3; ++c) e.pos[c] = e.savedPos[c] + (e.pos[c] - e.savedPos[c]) * (k / rate);
        memcpy(e.savedPos, e.pos, 12);
        e.rot[1] = YawTowards(goal[0], goal[2], e.pos[0], e.pos[2]);  // (the boss skeletons' facing, as 0x0049DE60)
        if (h.bossBonus) {  // Zeal is dead: it turns and walks off
            e.flags34 &= ~0x4000u;
            e.counter1334 = 0;
            e.sub = 4;
            return;
        }
        if (d > 0.5f && h.timer > 0) {
            WalkPace(e, static_cast<int>(rate * 256.0f));
            return;
        }
        e.flags34 &= ~0x4000u;
        w.AnimBlendTo(e, exe.S16(0x005703E0), 0, 5);
        w.Sound(0x218A9);
        e.flags34 |= 0x10000000;
        e.f1354 = 0;
        e.sub = 2;
        return;
    }
    case 2:
        if (tick == exe.S16(0x005703E8) && e.soundTick != tick) {
            e.soundTick = static_cast<int16_t>(tick);
            w.Sound(0x18A9);
        }
        if (tick >= exe.S16(0x005703E2) && e.f1354 == 0) {  // the blow
            e.f1354 = 1;
            for (int i = 0; i < kLanes; ++i)
                if (h.lanes[i].alive) BreakLane(w, h, i);
            for (int p = 0; p < 2; ++p) {
                if (!PlayerIn(w, p)) continue;
                w.player[p].hitTimer = 0;
                w.player[p].lives = 1;
                w.PlayerTakeHit(p, 1, 4);
            }
            w.shakeTimer = 0x30;
            h.bossOver = true;
            if (h.debug) fprintf(stderr, "horde: frame %d the knight's blow\n", w.frame);
        }
        if (!e.anim.ended) return;
        e.flags34 &= ~0x10000000u;
        w.AnimBlendTo(e, 0x38E, 0, 5);
        e.counter1334 = 0;
        e.sub = 3;
        return;
    case 3:  // stands, then leaves
        if (++e.counter1334 > 150) w.EnemyDespawn(e);
        return;
    case 4:  // walks away up the street, then gone
        e.rot[1] = YawTowards(e.pos[0], e.pos[2], goal[0], goal[2]);
        if (++e.counter1334 > 240) w.EnemyDespawn(e);
        return;
    default:
        e.sub = 0;
        return;
    }
}

void HordeEnemyBehaviour(World::Impl& w, EnemyTask& e) {
    HordeState* hp = Ptr(w);
    if (!hp || e.record.type == kDespawnRecordType) {
        w.EnemyDespawn(e);
        return;
    }
    HordeState& h = *hp;
    const Spawn& sp = SpawnOf(e);
    if (sp.type == kZeal) {
        ZealBehaviour(w, h, e);
        return;
    }
    if (sp.type == kKnight) {
        KnightBehaviour(w, h, e);
        return;
    }
    const ExeImage& exe = w.exe;
    auto entry = [&](int idx) { return exe.U32(exe.U32(0x00592F18 + 4 * e.anim.kind) + 4 * e.variant) + static_cast<uint32_t>(idx) * 0x10; };
    Lane* lane = e.counter1330 >= 0 && e.counter1330 < kLanes ? &h.lanes[e.counter1330] : nullptr;
    if (e.sub != 0 && e.sub < 10 && (!lane || !lane->alive)) e.sub = 0;
    if (lane && lane->alive && e.sub < 10) {  // the barrel row is solid: a zombie stays on its far side (the lunges end at the barrel)
        const float d = (e.pos[0] - lane->pos[0]) * h.fwd[0] + (e.pos[2] - lane->pos[2]) * h.fwd[2];  // along fwd: + = beyond the row
        const float stop = e.radius * 0.5f + 4.0f;
        if (d < stop) {
            e.pos[0] = e.pos[0] + h.fwd[0] * (stop - d);
            e.pos[2] = e.pos[2] + h.fwd[2] * (stop - d);
        }
    }
    if (e.sub == 0) {
        if (sp.type == kSwamp && (e.flags34 & 0x100)) e.sub = 20;       // fresh from its pool: the rise
        else if (sp.type == kThrower && HoldsAxe(e)) e.sub = 10;         // axes in hand: the throws first
    }
    switch (e.sub) {
    case 0: {
        int best = -1;
        float bd = 0;
        for (int i = 0; i < kLanes; ++i) {
            if (!h.lanes[i].alive) continue;
            const float d = Dist2D(e.pos, h.lanes[i].pos);
            if (best < 0 || d < bd) {
                best = i;
                bd = d;
            }
        }
        if (best < 0) {  // the row is open: the players
            e.flags34 &= ~0x4000u;
            e.behaviour = 2;
            e.sub = 0;
            return;
        }
        e.counter1330 = best;
        lane = &h.lanes[best];
        e.sub = 1;
        [[fallthrough]];
    }
    case 1: {  // the walk to the barrel
        WalkMotion(w, h, e, lane->pos[0], lane->pos[2]);
        if (Dist2D(e.pos, lane->pos) <= kReach + e.radius) {
            e.flags34 &= ~0x4000u;
            e.sub = 2;
        }
        return;
    }
    case 2: {  // the attack entry (0x00455A40 sub-state 0)
        const int r = w.Rand(0x48000011);
        const uint32_t tab = exe.U32(exe.U32(0x00592DC0 + 4 * e.anim.kind) + 4 * e.variant);
        const int8_t pick = static_cast<int8_t>(exe.S16(tab + ((((r >> 4) % 10) + (e.lostParts & 7) * 10) * 4)) & 0xFF);
        e.attack131A = pick;
        e.flags34 = (e.flags34 & ~0x100u) | 0x10000000;
        memcpy(e.savedPos, e.pos, 12);
        memcpy(e.faceTarget, lane->pos, 12);
        e.sub = 3;
        [[fallthrough]];
    }
    case 3: {  // approach until within the entry's range, then the attack motion (0x00455A40 sub-state 1)
        const uint32_t ent = entry(e.attack131A);
        const int motion = exe.S16(ent), approachMotion = exe.S16(ent + 2);
        w.EnemyTurnTo(e, lane->pos[0], lane->pos[2], 0x1000);
        const float d = Dist2D(e.pos, lane->pos) - e.radius;
        if (F32(exe, ent + 4) < d) {
            if (e.anim.motion[0] != approachMotion) w.AnimBlendTo(e, approachMotion, 0, 10);
            return;
        }
        w.AnimBlendTo(e, motion, 0, 5);
        w.EnemyAttackSplash(e, false);
        e.flags136C = (e.flags136C & ~0x10000u) | 0x80000;
        w.EnemyVoice(e, 3);
        e.f1354 = 0;
        e.sub = 4;
        return;
    }
    case 4: {  // the attack plays; the hit tick damages the barrel
        const uint32_t ent = entry(e.attack131A);
        const int hitTick = exe.S16(ent + 8);
        if (e.anim.tick[0] == hitTick && e.f1354 == 0) {
            e.f1354 = 1;
            lane->hp -= static_cast<float>(sp.barrelDamage + h.round / 2);
            lane->shake = 1.0f;
            w.Sound(0x1D16A9);
            if (h.debug) fprintf(stderr, "horde: frame %d barrel %d hit by kind %X: hp %.0f\n", w.frame, e.counter1330, e.anim.kind, lane->hp);
            if (lane->hp <= 0) {
                BreakLane(w, h, e.counter1330);
                e.sub = 0;
                return;
            }
        }
        w.EnemyAttackSplash(e, true);
        if (w.MotionTicks(e.anim.motion[0]) - 1 <= e.anim.tick[0]) {
            e.sub = 5;
            e.counter1334 = 0;
            e.flags34 = (e.flags34 & 0xEFFFFFFFu) | 0x20000000;
        }
        return;
    }
    case 5: {  // back off to the attack's start (0x00455C30), then walk again
        const int m = w.EnemyMotionSet(e, 4);
        const int r = w.Rand(0x48000012);
        w.EnemySetMotion(e, m, r % 5, 10);
        w.EnemyTurnTo(e, e.savedPos[0], e.savedPos[2], -0x40);
        ++e.counter1334;
        if (e.counter1334 > 40 || Dist2D(e.pos, e.savedPos) < 1.5f) {
            e.flags34 &= ~0x20000000u;
            e.sub = 1;
        }
        return;
    }
    // the thrower (kind 0x13 variant 7, as behaviour 33 0x00459080): to mid-street, then the axes at a player one by one
    // (the throw motion of the hand's attack entry, 0x0045A240 at its tick), then a walker
    case 10:
        if (Dist2D(e.pos, h.origin) > 112.0f) {
            WalkMotion(w, h, e, h.origin[0], h.origin[2]);
            return;
        }
        e.flags34 &= ~0x4000u;
        e.counter1334 = 60;
        e.sub = 11;
        [[fallthrough]];
    case 11: {  // the stand motion, a pause, then the throw
        const int m0 = w.EnemyMotionSet(e, 0);
        if (e.anim.motion[0] != m0) {
            const int r = w.Rand(0x48000013);
            w.AnimBlendTo(e, m0, r % w.MotionTicks(m0), 5);
        }
        w.EnemyTurnTo(e, h.origin[0], h.origin[2], 0x200);
        if (--e.counter1334 > 0) return;
        if (!HoldsAxe(e)) {
            e.sub = 0;
            return;
        }
        const bool h5 = e.anim.nodes[5].model == 0x1ECE, h8 = e.anim.nodes[8].model == 0x1ECA;
        const int hand = h5 && h8 ? ((w.Rand(0x48000014) & 1) ? 8 : 5) : h5 ? 5 : 8;
        e.f135C = hand;
        e.attack131A = static_cast<int8_t>(hand == 5 ? 0 : 1);
        e.target = static_cast<int8_t>(PickPlayer(w));
        e.flags34 |= 0x10000000;
        w.AnimBlendTo(e, exe.S16(entry(e.attack131A)), 0, 4);
        w.EnemyVoice(e, 3);
        e.sub = 12;
        return;
    }
    case 12:  // the weapon leaves at the entry's tick
        if (e.anim.tick[0] < exe.S16(entry(e.attack131A) + 8)) return;
        w.SpawnThrownWeapon(e, e.f135C);  // 0x0045A240
        e.sub = 13;
        return;
    case 13:
        if (e.anim.tick[0] < w.MotionTicks(e.anim.motion[0]) - 1) return;
        e.flags34 &= ~0x10000000u;
        if (HoldsAxe(e)) {
            e.counter1334 = 100 + w.Rand(0x48000015) % 80;
            e.sub = 11;
        } else {
            e.sub = 0;  // out of axes: a walker
        }
        return;
    // the swamp zombie's rise from its pool (behaviour 13's motion 0xB8 with its splashes; shot from the splash on)
    case 20:
        w.AnimSetMotion(e, 0xB8);
        e.sub = 21;
        return;
    case 21: {
        const int t = e.anim.tick[0];
        if (e.anim.motion[0] == 0xB8 && (t == 0x15 || t == 0x1B)) {
            int mat = 0;
            float p[3] = { e.pos[0], 0.0f, e.pos[2] };
            p[1] = w.FloorHeight(e.pos[0], e.pos[1] + 20.0f, e.pos[2], &mat);
            if (t == 0x1B) {
                w.SpawnSplashRing(p, 1.0f);
                w.SpawnSplashRing(p, 0.5f);
            }
            Mat4 m = Mat4::Identity();
            mx::Translate(m, p[0], p[1], p[2]);
            mx::RotY(m, e.rot[1]);
            const float l[3] = { 0.0f, 0.0f, -1.5f };
            float o[3];
            mx::Transform(m, l, o);
            w.SpawnEffectAt(o, 0, 0, 0x62, 1, -1);  // 0x00407340
        }
        if (t >= 0x15) e.flags34 &= ~0x100u;
        if (w.MotionTicks(e.anim.motion[0]) - 1 <= t) {
            e.flags34 &= ~0x100u;
            e.sub = 0;
        }
        return;
    }
    default:
        e.sub = 0;
        return;
    }
}

}  // namespace game
