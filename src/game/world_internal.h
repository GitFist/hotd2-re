#pragma once

// Private implementation of game::World (world.cpp, world_enemy.cpp). Not part of the renderer API.

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "game/anim.h"
#include "game/camera.h"
#include "game/effects.h"
#include "game/enemy.h"
#include "game/environment.h"
#include "game/event.h"
#include "game/event_runner.h"
#include "game/exe_image.h"
#include "game/props.h"
#include "game/rng.h"
#include "game/shoot_camera.h"
#include "game/shooting.h"
#include "game/stage.h"
#include "game/task.h"
#include "game/typing_reference.h"
#include "game/world.h"
#include "game/world_typing_drill.h"
#include "game/world_typing_drill_game.h"

namespace game {

// Original task function addresses (the capture's K-line ids).
namespace fn {
constexpr uint32_t kEvtInit = 0x0045ECB0, kEvt = 0x0045ECC0, kEnvInterp = 0x0040AD90, kCamera = 0x004022B0,
                   kSky = 0x004132D0, kRail = 0x00414F10, kPlayerInit = 0x004145C0, kPlayer = 0x00413E90,
                   kPlayerCount = 0x00414F40, kLetterbox = 0x00413970, kAltModels = 0x00480970,
                   kStageDraw = 0x00401260, kRain = 0x004136A0, kLockOn = 0x00408DD0, kAttackOrder = 0x004090B0,
                   kCollision = 0x00406010, kHit = 0x004044A0, kDemoTimer = 0x00426960, kEnemyCreate = 0x00452DA0,
                   kEnemy = 0x004533F0, kEffect = 0x00407A70, kSpark = 0x004667A0, kHanging = 0x00473CF0,
                   kBreakable = 0x00465FB0, kDecor = 0x0046DAB0, kAnimated = 0x00474120, kKomono = 0x00461CD0,
                   kType44 = 0x00472B10, kType33 = 0x00432FF0, kType33State3 = 0x00433AC0,
                   // player task functions (0x00579CD0[state * 5] / 0x00579DE8 rows)
                   kPlayerJoin = 0x00413DB0, kPlayerIdle = 0x004145F0, kPlayerContInit = 0x00414200,
                   kPlayerCont = 0x00414280, kPlayerOut = 0x00414420, kPlayerOut2 = 0x004144C0,
                   // two players / continue (world_player.cpp, notes/agents/twoplayer.md)
                   kPlayerRejoin = 0x00413DE0,   // state 1: continue accepted (row 1)
                   kPlayerRow2 = 0x00413E40,     // state 2 (row 2)
                   kPlayerJoinLate = 0x00413E50, // state 3: joined during the game (row 3)
                   kPlayerWaitInit = 0x00414680, // state 10: wait until 0x009A5900 bit 0 (first run)
                   kPlayerWait = 0x00414690,     // state 10: then the saved state (+0x48)
                   kPlayerNone = 0x0041EBB0,     // no-op (0x00414520 state 7)
                   kPlayerContRestart = 0x00414250,  // continue countdown restarted (0x004141D0)
                   kSkipWatch = 0x00435F20, kSkipWatch2 = 0x00435F40, kSkipWatch3 = 0x00435FA0;
}  // namespace fn

struct WTask : TaskData {
    uint32_t fn = 0;  // original function address; updated where the original switches the task function
};

// Player task (0x00414ED0 creates one per player with 0x00579CD0[state * 5]; task +0x34 = player index)
struct PlayerTask : WTask {
    int index = 0;  // +0x34
};

// A spawned actor before its type function ran (ops 0x09..0x0C)
struct SpawnTask : WTask {
    SpawnRecord rec;
    uint32_t op = 0;
};

// Enemy node record (obj + 0x20C + index * 0x90).
struct EnemyNode {
    int32_t model = 0;    // +0x00
    int cur[3] = {};      // +0x04 angles in use
    int s1[3] = {};       // +0x10 blend / interpolation source
    int s2[3] = {};       // +0x1C blend / interpolation target
    Mat4 world;           // +0x28 node matrix (the original keeps model-view; world here)
    Mat4 mv;              // the original's +0x28: model-view (AnimDraw's second chain, from the view)
    float hit[3] = {};    // +0x68 view-space hit point
    uint32_t flags = 0;   // +0x74 (0x21 after init; 2 << p / 8 = hit; 0x80000000 wound applied)
    float radius = 0;     // +0x78
    float local[3] = {};  // +0x7C local hit offset
    int32_t mesh = 0;     // +0x88
    int16_t stage = 0;    // +0x8C damage stage
    int16_t track = 0;    // +0x8E animation track
    bool used = false;    // slot belongs to the skeleton
};

// Shootable actor: the object fields the hit system reads (Hit_TestObjectSphere 0x00404630, Hit_Deliver 0x00404DB0).
struct ActorTask : WTask {
    uint32_t flags34 = 0;      // +0x34
    float viewPos[3] = {};     // +0x70
    float radius = 0;          // +0x124
    int8_t hitNode[2] = {0, 0};  // +0x190 + player
    bool typingStrike = false;
    int typingPlayer = 0;
    int typingPickupItem = 0;
    uint32_t typingRevision = 0;
    // Reference: explicit word-post registration, separate from gun collision.
    // Preserve the event source because identical props from type 0x44 do not
    // inherit the typing words of generic komono type 0x41.
    uint32_t typingFlags = 0;
    float typingFrameColor = 1; // source4B17B2: word-post +0x48; zero flashes, .5 yellow, one green
    int typingWordStyle = 0;  // original common actor +0x11C; distinct from custom actors' private HP
    uint32_t typingOriginType = 0xFFFFFFFFu;
    int typingOriginKind = -1;
    float radius128 = 0;       // +0x128 collision radius (0: the pusher copies +0x124 into it)
    float sphere[3] = {};      // +0x12C collision sphere centre
    bool pushed = false;       // +0x138 != 0 (pushed by another actor)
    uint32_t pusherFlags = 0;  // +0x34 of the actor at +0x138 (copied at push time)
    float pushAmount = 0;      // +0x13C
    float pushDir[3] = {};     // +0x140
    int32_t objMesh = -1;      // +0x14C collision mesh of a flags-0x10 target (exe address in the coli files)
    Mat4 m150 = Mat4::Identity();  // +0x150 its matrix: model-view when stored, world after Hit_Register 0x00405160
    // Skeleton targets (flags 0x80): node records and the skeleton type (+0x1F4).
    virtual int SkeletonKind() const { return -1; }
    virtual EnemyNode* Node(int index) { return nullptr; }
    // The angles +0x64 / +0x68 / +0x6C (the object-mesh shot turns its face normal by them, 0x00404A00); nullptr: 0
    virtual const int* Rot64() const { return nullptr; }
};

// Enemy animation block A = obj + 0x194 (enemy.md "Animation block A").
struct EnemyAnim {
    int counter[2] = {};       // +0x00 / +0x04
    int tick[2] = {};          // +0x08 / +0x0C
    int prevFrame = 0;         // +0x10 stored frame of the previous evaluation (root motion)
    int frame[2] = {};         // +0x18 / +0x1C stored 30 Hz frame
    int motion[2] = {};        // +0x20 / +0x24
    int blendStart[2] = {};    // +0x28 / +0x2C
    int8_t blendLen[2] = {};   // +0x30 / +0x31 (frames + 1)
    int8_t loops = 0;          // +0x33
    int8_t partial[2] = {};    // +0x34 / +0x35 partial-skeleton node per track
    int8_t track1 = 0;         // +0x36 track 1 active
    uint8_t flags[2] = {};     // +0x37 / +0x38 (1 blending, 0x20 odd-tick interpolation, 8/0x10 track-1 modes)
    float rootS1[3] = {};      // +0x44
    float rootS2[3] = {};      // +0x50
    bool ended = false;        // +0x5D
    bool nearEnd = false;      // +0x5E
    int16_t kind = 0;          // +0x60 skeleton type
    uint32_t drawFlags = 0;    // +0x64 (1 draw, 2 root motion, 4 floor shadow, 8 matrix blend for interpolation)
    uint8_t rotOrder = 0;      // +0x68
    float root[3] = {};        // +0x6C
    std::array<EnemyNode, 32> nodes;  // +0x78 (room for 30 records before +0x1160; skeletons use up to 19)
    float prevRoot[3] = {};    // +0x1160
    float scale = 1.0f;        // +0x116C
    bool attachHidden = false; // 0x00409D10(anim, 0): every attachment entry (+0x40 list, byte +1) disabled
    // per-evaluation globals 0x007C1C24 (factor per track) / 0x007C1C30 (frame per track)
    float factor[2] = {};
    int framePtrMotion[2] = { -1, -1 }, framePtrFrame[2] = {};
};

struct EnemyTask : ActorTask {
    uint32_t flags38 = 0;      // +0x38
    int actorSlot = -1;        // +0x3C
    float pos[3] = {};         // +0x40
    float vel[3] = {};         // +0x4C
    float acc[3] = {};         // +0x58
    int rot[3] = {};           // +0x64
    const int* Rot64() const override { return rot; }
    float body[3] = {};        // +0x100 world body point
    float bodyView[3] = {};    // +0x10C
    int16_t hp = 0, hpMax = 0;  // +0x11C / +0x11E
    int8_t slot120 = -1;       // +0x120 lock-on slot
    int8_t target = -1;        // +0x121 target player
    EnemyAnim anim;            // +0x194
    int variant = 0;           // +0x130C
    int16_t behaviour = 0, sub = 0;  // +0x1310 / +0x1312
    int16_t soundTick = 0;     // +0x1314
    int16_t lo = 0;            // +0x1316
    uint8_t lostParts = 0;     // +0x1318
    int8_t lastHitSlot = 0;    // +0x1319
    int8_t attack131A = 0;     // +0x131A attack entry (0x00592F18 table index)
    int8_t c131B = 0;          // +0x131B group member (0x00456600)
    int8_t killer = -1;        // +0x131C
    int8_t c131D = -1, c131E = 0, c131F = 0;
    int headPitch = 0, headYaw = 0;  // +0x1320 / +0x1324
    int texCounter = 0;        // +0x1328
    int counter1330 = 0;       // +0x1330
    int counter1334 = 0;       // +0x1334
    int timer1338 = 0;         // +0x1338
    int cooldown133C = 0;      // +0x133C
    int facing1350 = 0;        // +0x1350
    int attackLimit = 0;       // +0x1358 attack rank limit (0x00456650)
    int lastHitType = 0;       // +0x1364
    uint32_t flags1368 = 0;    // +0x1368
    uint32_t flags136C = 0;    // +0x136C
    float walkDistance = 0;    // +0x1370 behaviour 15 walk length (record param 1, raw bits)
    float walked = 0;          // +0x1374
    uint32_t splashList = 0;   // +0x13A0 exe VA of the death-motion splash tick list (0x004563F0)
    float jumpStart[3] = {};   // +0x13C0
    float jumpTarget[3] = {};  // +0x13CC
    float savedPos[3] = {};    // +0x13D8 position at the end of the walk-in / attack start
    float faceTarget[3] = {};  // +0x13E4
    SpawnRecord record;        // params at +0x1390 = record + 0x24
    EnemyTask* parent = nullptr;  // +0x1394 civilian of a child enemy (0x0048A3E0 children list)
    int f1354 = 0;             // +0x1354 behaviours 34/35/45: attack tick / saved behaviour
    int f135C = 0;             // +0x135C behaviour 33: throwing hand (5 / 8)
    int jump1360 = 0;          // +0x1360 jump motion state (0x0044D860, generic enemies: world_st4_enemy.cpp)
    int jumpTab[12] = {};      // 0x009C8AA0 + actor slot * 0x30: jump motion table (0x0044DA60)
    EnemyTask* vehicle = nullptr;  // actor 0x13 ridden (actor 0x18 +0x13B0 / civilian ctl +0x68; world_st3_vehicle.cpp)
    bool framed = false;       // AnimDraw / shadow start from rame (the vehicle's matrix) instead of identity
    Mat4 frame = Mat4::Identity();
    Mat4 frameMv = Mat4::Identity();  // the same vehicle ops applied on the view (the original's matrix-stack top)
    uint32_t list1398 = 0;     // +0x1398 behaviours 34/35: next motion-list entry (event memory)
    void* held139C = nullptr;   // +0x139C behaviour 37: the carried object (world_st1b_held.cpp)
    LockOnTarget* lock = nullptr;  // what the lock-on table / shooting camera read (pos, body point, target, slot)
    bool civilian = false;     // CivTask (actor type 0x10): civilian node / post-root callbacks
    bool enemyPost = false;    // +0x12F0 = 0x00454900 (set only by Enemy_Create 0x00452DA0; 0x00410440's default is the
                               // no-op 0x00420810)
    uint32_t nodeCb = 0;       // node callback +0x12EC when not the type default (0x00411050 plain, 0x0049D980 ...)
    int8_t bodyOf = -1;        // player body object (player +0x78, 0x00416450): node callback 0x00416570
    uint32_t parts = 0;        // anim +0x1170: part list (event memory, s16 ids, -1 ends; 0x00412440 / 0x004124F0)
    // node draw (+0x12EC) / post-root (+0x12F0) callbacks of actor types ported outside world_enemy.cpp (checked first)
    void (*nodeFn)(World::Impl& w, EnemyTask& e, int idx, const Mat4& m) = nullptr;
    void (*postRootFn)(World::Impl& w, EnemyTask& e) = nullptr;
    // stage 6 (world_st6_enemy.cpp): fade of kinds 9 / 0x12 (0x004534A0 nodes 0x1C7C / 0x1C6C), kind-9 hit proxy owner
    float fade134C = 0;          // +0x134C frames before the fade
    float fadeStep1388 = 0;      // +0x1388
    float alpha138C = 0;         // +0x138C (0x00453AE0: drawn with this alpha while +0x1368 & 0x20)
    EnemyTask* parent13A4 = nullptr;  // +0x13A4 (task 0x00453290)
    uint32_t parentSerial = 0, parentFlags = 0;  // owner's task serial / last seen +0x34
    int32_t P(int i) const { return record.ParamS32(4 * i); }
    virtual float Alpha138C() const { return 1.0f; }  // +0x138C fade (actor 0x31: world_st2_actor31.cpp)
    int SkeletonKind() const override { return anim.kind; }
    EnemyNode* Node(int index) override {
        return index >= 0 && index < 32 && anim.nodes[index].used ? &anim.nodes[index] : nullptr;
    }
};

// Scripted civilian control block (heap block of 0xC4 bytes at obj + 0x1310, allocated by 0x0048A3E0).
struct CivCtl {
    uint32_t flags = 0;        // +0x00 wait conditions of the current script block (op 0x2C)
    uint32_t flags2 = 0;       // +0x04 (1: 0x009CA0E8 already decremented)
    int32_t loopTick = 0;      // +0x08
    int16_t loops = 0;         // +0x0C motion loops (-1 endless)
    int16_t blendN = 10;       // +0x0E
    int16_t waitPath = 0, waitTime = 0;  // +0x10 / +0x12 (condition 0x80: camera path / tick)
    int16_t delay = -1;        // +0x14
    int16_t waitTick = 0;      // +0x16 (condition 0x200)
    int16_t c18 = 0;           // +0x18 (condition 0x400)
    int16_t flagIdx = 0;       // +0x1A (condition 0x2000: event flag)
    int16_t skip = 0;          // +0x1C blocks skipped when a condition is met
    int16_t children = 0;      // +0x1E
    int16_t cnt20 = 0, cnt22 = 0, cnt24 = 0;  // +0x20 / +0x22 / +0x24 (conditions 4, 1 / 2, 8)
    int16_t trigPath = 0, trigTime = 0;       // +0x26 / +0x28 (params +2 / +4: lifetime trigger)
    int16_t lifeTimer = 0;     // +0x2A
    int16_t c2C = 0, c2E = 0;  // +0x2C / +0x2E
    float target[3] = {};      // +0x30
    float radius = 0;          // +0x3C arrival radius (op 5)
    int32_t path = 0;          // +0x40 (exe address of a point, 1 = computed point, -1 / -2 = players)
    uint32_t path2 = 0;        // +0x44
    uint32_t script = 0;       // +0x48 next block
    uint32_t altScript = 0, altScript2 = 0;  // +0x4C / +0x50 (when shot)
    uint32_t jump = 0;         // +0x54
    uint32_t soundList = 0;    // +0x58
    uint32_t perFrameFn = 0x0041EBB0;  // +0x5C
    float radiusTarget = 1.0f, radiusStep = 0;  // +0x78 / +0x7C
    uint8_t sphereMode = 2;    // +0x80
    int8_t voice = -1;         // +0x81
    int16_t attachSlot = 0;    // +0x82
    int16_t attachCount = 0;   // +0x6E
    std::vector<std::pair<uint32_t, uint32_t>> attachList;  // +0x70 {record, mask} (world_st1b_civ.cpp)
    uint32_t attachChosen = 0;  // +0x74 record picked by op 0x15
    int16_t scorePlayer = -1;  // +0x6C
    uint32_t sound = 0;        // +0x84
    int32_t soundTimer = 0;    // +0x88
    int32_t lookMode = 0;      // +0x8C
    uint32_t lookTarget = 0;   // +0x90 (modes 4 / 5: address of a point)
    EnemyTask* lookObj = nullptr;  // +0x90 (mode 2: the child enemy looked at)
    int32_t look94 = 0, look98 = 0, look9C = 0;  // +0x94 / +0x98 / +0x9C head angles added (x, y, z), 0x0048D9B0 steps
    int32_t faceFrame = 0, faceTimer = 0, faceMode = 6;  // +0xA0 / +0xA4 / +0xA8
    int16_t penaltyNode = 2;   // +0xAC
    int16_t moveTimer = 0;     // +0xAE
    float moveTarget[3] = {};  // +0xB0
    int16_t c2F = 0;           // +0xBC
    std::vector<EnemyTask*> childList;  // +0x60 child enemies (count +0x1E)
    EnemyTask* child64 = nullptr;       // +0x64
};

struct CivTask : EnemyTask {
    CivCtl c;
    int16_t lifeDelay = 0;     // params +6
};

// Actor 0x22 (world_actors.cpp; variant 1 = the stage-1 boss, world_st1b_boss.cpp): skeleton 0x45 + a second object
// (skeleton 0x46) at +0x13B0
struct Actor22Task : EnemyTask {
    std::unique_ptr<EnemyTask> second;
    int talk = 0;        // +0x1324 (second object animates)
    int mouthMode = 0;   // +0x1338 (node 2 model cycle: 1 table 0x00570F08, 2 table 0x00570F14)
    int mouthTick = 0;   // +0x133C
    int secondWant = 1, secondHave = 1;  // second object +0x1350 / +0x1354 (motion 0x00570C00[i])
    std::unique_ptr<TaskData> boss;      // variant 1 state (world_st1b_boss.cpp)
};

struct HydraState;  // world_st3_boss.cpp
struct St5State;    // world_st5.h
struct ModesState;  // world_modes.h
struct St6State;    // world_st6.h

struct TypingTargetSpec {
    int node = -1;
    uint32_t token = 0;
    bool boss = false, danger = false, retain = false, projectile = false;
    float view[3] = {};
    const TypingWordRecord* word = nullptr;
};

struct World::Impl : public EventHost, public CrtRand::Observer {
    Impl();

    // ---------------------------------------------------------------- data
    std::string gameDir;
    TypingReference typingReference;
    bool typingReferenceActive = false;
    struct TypingChallengeCtl {
        bool active = false, inputBlocked = false, success = false, rewardPending = false, keyActivity = false;
        int state = 0, shared = 0, deadline = 0, interval = 0, maximum = 0, quota = 0;
        int timer = 0, spawned = 0, side = 0, finishFrames = 0, reward = 0;
        int instruction = -1, instructionFrames = 0, instructionAge = 0, instructionTarget = 0;
        uint64_t startingErrors = 0;
        std::vector<uint32_t> records;
        std::vector<int> weighted;
    } typingChallenge;
    int typingSavedRank = 0;          // reference A372A8: rank before the most recent item
    int typingRankClears = 0;         // reference A372A4: scene clears before rank restoration
    ExeImage exe;
    EventMemory mem;
    std::vector<SkyEntry> skyTable;
    HangingTables hanging;
    BreakableTables breakable;
    EnemyTables enemyTables;
    StageSections sections;
    CameraBank cameraBank;
    std::vector<MotionFileEntry> motionFiles;
    std::map<int, PropSkeleton> propSkeletons;
    std::map<int, Motion> propMotions;            // prop-skeleton layout (LoadMotionById)
    std::map<std::pair<int, int>, Motion> enemyMotions;  // (motion, node count)
    std::map<int, Skeleton> enemySkeletons;
    uint32_t actorType[0x70] = {};                // 0x009A2280 (Actor_InitTypeTable 0x0040AC90 from 0x00593358)
    float focal = 640.2079f;                      // 0x009A2D70

    // ---------------------------------------------------------------- state
    WorldConfig cfg;
    TypingModeController typing;
    TypingDrillUi typingDrill;
    TypingDrillGameState typingDrillGame;
    std::string typingCharacters;
    bool typingCancel = false;
    std::map<std::pair<uint32_t,uint32_t>,std::array<float,2>> typingPositions;
    std::map<uint32_t, std::pair<TypingTarget, TypingTargetSpec>> typingCachedTargets;
    std::map<uint32_t,uint32_t> typingPendingBoss;
    uint32_t typingEpoch = 0;
    bool typingWasReady = false;
    bool typingPropsEnabled = false;  // TOTD event 0x66; original global 0x0083497C
    unsigned typingGradeProgress = 0;
    std::string typingQuestion;
    float typingQuestionSeconds = 0;
    struct TypingHudPresentation {
        int lastFrame = -1;
        unsigned previousStreak = 0;
        int perfectFrames = 0;
        bool brokenStreak = false;
        uint64_t displayedGauge = 0, previousThreshold = 400;
        int lifeAwardAge = -1;
        int questionSlide = -960;
        bool questionVisible = false;
        struct Mistake {
            uint32_t serial = 0, token = 0;
            std::array<float, 2> anchor{};
            int age = 0, lastFrame = -1;
        };
        struct Complete {
            std::array<float, 3> view{};
            TypingRank grade = TypingRank::Unranked;
            int age = 0, lastFrame = -1;
        };
        std::map<uint32_t, Mistake> mistakes;
        std::vector<Complete> completions;
        struct PosterState {
            std::array<float, 3> view{}; // Original word-post XYZ, including the five-unit depth offset.
            int lastFrame = -1;
        };
        std::map<std::pair<uint32_t, uint32_t>, PosterState> posters;
        int postersFrame = -1;
        struct StoryLine {
            TypingWordRecord word{};
            std::array<float, 3> view{}; // Original Strength child actor's direct view-space position.
            float velocity = 0;
            int phase = 0, bounce = 0, hold = 0;
            bool enabled = false;
        };
        struct StoryPosters {
            uint32_t owner = 0, generation = 0;
            unsigned line = 0;
            int age = 0, lastFrame = -1, deadline = -1, bossState = 0;
            bool active = false, available = false;
            std::array<StoryLine, 4> lines;
        } story;
    } typingHud;
    TypingModeController typing2;
    std::string typingCharacters2;
    bool typingCancel2 = false, typingWasReady2 = false;
    decltype(typingPositions) typingPositions2;
    decltype(typingCachedTargets) typingCachedTargets2;
    decltype(typingPendingBoss) typingPendingBoss2;
    decltype(typingHud) typingHud2;
    unsigned typingGradeProgress2 = 0;
    int typingPlayer = 0;
    uint8_t typingItemKeys[2] = {};
    TypingModeController& TypingController(int p) { return p == typingPlayer ? typing : typing2; }
    const TypingModeController& TypingController(int p) const { return p == typingPlayer ? typing : typing2; }
    void SwapTypingPlayer();
    uint64_t typingEmperorAnswerWeight = 0;
    struct TypingPreviousOptions {
        bool valid = false, freePlay = false;
        int difficulty = 2, lives = 3, creditSetting = -1, credits = 0;
    } typingPreviousOptions;
    int typingAttackDelay = 0;  // Reference 0x005E53D8, shared attack grace counter
    int typingAttractDemo = -1; // Isolated preview: PC demo script variant, with native typing controls.
    bool typingResultsActive = false;
    struct TypingChapterChooser {
        bool active = false;
        int selected = 0, remaining = 720;
        float rotation = 0, desired = 0;
        uint32_t savedFlags = 0;
    } typingChapterChooser;
    int frame = 0;
    TaskSystem ts;
    CrtRand rng;
    Environment env;
    std::unique_ptr<EventRunner> runner;

    WorldCamera cam;          // active context after the camera task
    Mat4 view0 = Mat4::Identity(), inverse0 = Mat4::Identity();  // context 0 (0x009A6000 / 0x009A6040)
    CameraState camState;     // pos / pitch / yaw / roll of the active context (effects)
    float playerPos[3] = {};  // 0x009C71E0
    int playerYaw = 0;        // 0x009C71F0
    size_t evalsAfterCamera = 0;

    // globals (original addresses)
    int skyType = 0, skyMode = 0;   // 0x009A2C34 / 0x009CA090
    float projCenter[2] = { 320.0f, 240.0f };       // 0x00419490 (0x009C7300 / 0x009C7304 as screen pixels)
    float projCenterFrame[2] = { 320.0f, 240.0f };  // the value Proj_Setup 0x004184C0 used for this frame
    std::map<int, int> skyScroll;   // sky panel model -> uv scroll runs 0x00413560 since the stage start (WorldDraw::uvScroll)
    Sky sky;
    int rainFlag = 0;               // 0x009C8E50
    Rain rain;
    float ground = 0;               // 0x009C8E58
    int letterbox = 0, letterboxPrev = 0;  // 0x009CA0F4 / 0x009C8E9C
    int letterboxCounter = 0;       // letterbox task +0x50
    int canShoot = 0;               // 0x009C8E00
    uint32_t flags5900 = 0;         // 0x009A5900
    int altModels = 0;              // 0x009A2BB4
    // ---- light (renderer state only; world_light.cpp) ----
    WorldLight light;               // light globals 0x007E7998.. (snapshotted by every Draw)
    int lightDir0[2] = {};          // block-0 direction as the camera task built it (0x009A354C from 0x009A3558 / 5C)
    WorldLights lights;             // fog + light table of the frame
    int flashlightOn = 0;           // 0x009C8D4C (op 0x15: player flashlights in the light table, 0x00480AC0)
    float tableAmbient[3] = { 0.5f, 0.5f, 0.5f };  // 0x009C71D4.. (op 0x16; 0x004809D0 sets 0.5)
    struct LightSlot {              // 0x009A1A20 + i * 0x74: D3DLIGHT7 (world space) + enabled +0x68, index +0x70
        WorldLights::Light l;
        bool enabled = false;
        bool used = false;          // +0x6C (slots 3..15 handed out by Light_AllocSlot 0x00480A60)
    };
    LightSlot lightSlots[16];
    int animLight = 1;              // AnimDraw: 1 = between 0x0041DC70 / 0x0041DCC0 (default), 0 = the caller's light
    bool envMapDraw = false;        // the next Draw()s follow 0x00418660 (env-map uv rewrite, WorldDraw::envMap)
    void LightScene();              // 0x0041DCC0: block-0 intensity / direction / colour
    void LightChar();               // 0x0041DC70: block-1 intensity / direction (from the active camera) / colour
    void LightCustom(float intensity, int pitch, int yaw, const float color[3]);  // 0x0041DC10
    void LightColor(float r, float g, float b);  // 0x004AA0A0
    void LightIntensity(float i);   // 0x004AA070
    void TaskEnv(Task& t);          // 0x0040AD90
    void TaskLightTable(Task& t);   // 0x00480970
    void UpdateLightsFrame();       // end of a frame: WorldLights from env / table
    int section = 0;                // 0x009A2224
    int liveEnemies = 0;            // 0x009C904A
    int countA0E8 = 0;              // 0x009CA0E8 (scripted actors alive; op 0x46)
    int attackers = 0;              // 0x009C7006
    int playersInGame = 0;          // 0x009C7000
    int players = 0;                // 0x009C8E80
    int route = 0;                  // 0x009C8E84
    int rank = 0;                   // 0x009C8E96
    int rankPrevPlayers = 0;        // 0x009C8E82
    int targetTaken[2] = {};        // 0x009A2BA0
    int attackSlot = 0;             // 0x009A34F0
    int frameCounter5C50 = 0;       // 0x009A5C50
    int frameCounter2BAC = 0;       // 0x009A2BAC (frames since Stage_ResetState 0x0045EDD0)
    int demoFlag = 0;               // 0x009C71C4
    bool seededSinceStart = false;  // 0x009A26A4
    uint8_t itemCounter[16] = {};   // 0x009C7010
    void* actorSlots[14] = {};      // 0x009C88C0
    int c7310 = 0;                  // 0x009C7310

    struct Player {
        int16_t state = 0;          // +0x02
        int16_t lives = 0;          // +0x06
        int16_t livesShown = 0;     // +0x08
        int score = 0;              // +0x0C
        float aim[2] = {};          // +0x10 / +0x14
        int pending = 0;            // +0x18
        int16_t ammo = 0;           // +0x1C
        int16_t empty = 0;          // +0x1E
        int16_t reloadBlink = 0;    // +0x20
        int16_t streak = 0;         // +0x22
        int16_t shots = 0, hits = 0;  // +0x24 / +0x26
        int32_t outTimer = 0;       // +0x28 (state 6 "game over" display: 0x78 frames, 0x00414420 / 0x004144C0)
        int32_t credit0 = 0, credit1 = 0, credit2 = 0, credit3 = 0;  // +0x2C..+0x38 credit snapshot (continue)
        int16_t nextState = 0;      // +0x48 state taken after the state-10 wait (0x00414FC0 / 0x00414690)
        Task* task = nullptr;       // +0x74 the player's task (0x004141D0 switches its function)
        int32_t contTimer = 0;      // +0x68
        int16_t hitShake = 0;       // +0x70 (1: Player_HitShake restarts) / +0x72 hit kind
        int16_t hitKind = 0;
        uint32_t cb7C = 0, cb80 = 0;  // +0x7C / +0x80 per-state callbacks (0x00579DE8 rows)
        float crosshair[2] = {};    // +0xE8 / +0xEC
        bool crosshairOn = false;
        uint32_t flags12C = 0;      // +0x12C
        int8_t deviceKind = -1;     // +0x124
        int8_t inputKind = 0;       // +0x125 (1 gun, 0 pad, -1 none)
        int hitTimer = 0;           // 0x009C8E08[p]
        float ammoAnim = 0;         // 0x007C2120[p] (ammo HUD scale while the letterbox opens)
        struct Overlay {            // 0x009A26C0 + p * 0x14 (damage overlay model, 0x00417440 / 0x00417300)
            int active = 0, timer = 0, kind = 0, route = 0;
            float x = 0;
        } overlay;
        GunRecord gun;              // 0x009C8FC0 + p * 0x28
        ShotRay ray;                // 0x009A2CB8 + p * 0x68 (view part)
        bool firedThisFrame = false;
        int32_t contShown = -1;     // per-player CONTINUE drawn this frame (0x00416880 / 0x004168C0 from 0x00414280):
                                    // +0x68 as drawn; -1 none (WorldHud::Player::continueShown)
        // muzzle flashes 0x009A2960 and tracers 0x009A2460 (rings of 6, index 0x009CA09C[p]), Shot_SpawnTracer
        // 0x00416F70 / drawn by 0x00416B00
        struct Flash {
            bool active = false;
            float pos[3] = {};  // view space
            int pitch = 0, yaw = 0, counter = 0, type = 0;
        } flash[6];
        struct Tracer {
            bool active = false;
            float pos[3] = {}, vel[3] = {};  // world
            int roll = 0, counter = 0, type = 0;
        } tracer[6];
        int ring = 0;
    } player[2];

    // ---- game mode 6 (world_player.cpp) ----
    int gameMode = 5;                // 0x009C8E98
    int subState = 0;                // 0x009C90A4
    uint32_t seedCounter = 0;        // 0x009A32A0
    int contCountdown = 0;           // 0x009A2BB8 (sub-states 3 / 4)
    int contScreenShown = -1;        // CONTINUE screen drawn this frame (0x00460530): 0x009A2BB8 as drawn; -1 none
    int contCredit[4] = {};          // 0x007DCCAC..0x007DCCB8
    int route88 = 0;                 // 0x009C8E88 (route when the game ended)
    int rankAdj = 0;                 // 0x009A3794
    int rankTimer = 1;               // 0x009C8A7C
    bool rankTimerOn = false;        // 0x009A2C30
    int shakeTimer = 0;              // 0x009C8E8C
    int shake = 0;                   // 0x009CA0E4
    bool skipRequest = false;        // 0x009A1A18
    int skip2230 = 0;                // 0x009A2230
    int padClear = 1;                // 0x009A34DC (Stage_ResetState: pad cursors cleared until the letterbox opens)
    WorldInput input[2];             // device state written by SetInput (gun slots 0 / 6)
    bool inputEdge[2] = {};          // input[p].fired / offscreen / pressed not consumed yet
    struct ShotIn {                  // 0x009C8800 + p * 0x14 (Game_UpdateShots 0x0040CE70)
        int16_t x = 0, y = 0, on = 0, fired = 0;
    } shotIn[2];
    uint32_t pressed = 0, held = 0;  // 0x009C9028 / 0x009C9034 (P1 bits 0..11, P2 << 16)
    int devicePos[2][2] = {};        // 0x007DBD50 slot positions after Input_LatchShots (0xFFFF off screen)
    void InputFrame();               // Game_UpdateInput 0x0040E4D0 (latch, shots, buttons, gun records)
    bool paused = false;             // 0x009A1A00
    int pausePlayer = 0;             // 0x007C1792 (the player who paused; only that player's start resumes)
    bool pauseHist[2] = {};          // 0x007C1798 / 0x007C179A (pause state one / two frames ago)
    void PauseFrame();               // 0x0040E500 / toggle check 0x0040E9C0
    uint32_t camPlayerWrites = 0;    // camera writes of the players' +0x12C bit 0 applied so far
    void SyncPlayerDrawBit();        // camera writes of the players' +0x12C bit 0 -> both players
    // ---- game mode 7: game over + map screen (world_gameover.cpp) ----
    int goTimer = 0;                 // 0x009CA0F8
    uint8_t routeRec[10][16] = {};   // 0x009A5920: scenes played per stage (op 0x4D appends; 0xFF ends)
    int routeCount = 0;              // 0x009A5C30 (Stage_ResetState: 0)
    struct MapState {                // 0x007DCCD8..0x007DCD00 (map screen)
        int scroll = 0;              // 0x007DCCD8
        int footprint = 0x14C2;      // 0x007DCCE0 footprint model
        int state = 0;               // 0x007DCCE4 (0 walking, 1 arrived, 2 / 3 end motion, -1 done)
        int stage = 0;               // 0x007DCCE6 route row
        int scene = 0;               // 0x007DCCE8
        int dir = 0;                 // 0x007DCCEA (2 up, 4 right, 6 left, 8 down)
        int step = 0;                // 0x007DCCEC point of the (stage, scene) path
        int idx = 0;                 // 0x007DCCEE index in the route row
        int mapY = 0;                // 0x007DCCF4
        int targetX = 0, targetY = 0;  // 0x007DCCF8 / 0x007DCCFA
        int screenY = 0;             // 0x007DCCFC
        int mapX = 0;                // 0x007DCCFE
    } map;
    std::vector<WorldHud::Effect> hudEffects;  // 2D sprites submitted by world tasks this frame (mode 7)
    std::vector<SpriteDef> hudSprites;         // other 2D sprites of world tasks (WorldHud::sprites)
    std::vector<WorldHud::SubtitleLine> hudSubtitles;  // WorldHud::subtitles
    uint32_t hudTitleSerial = 0;                       // WorldHud::titleSerial
    int routeMapScroll = -1;         // map screen task 0x00461180 ran this frame: its scroll 0x007DCCD8
    void GameOverFrame();            // 0x00460960
    void CameraReset();              // 0x004021D0
    void BuildViews();               // Cam_BuildViews 0x00401F40 (+ mode commit, 0x009A5900 bit 0)
    void CreatePlayerTasks();        // 0x00414F20 + 0x00414ED0 + 0x00414FB0
    void SpawnSpriteFx(int id, float x, float y, float sx, float sy, float alpha, int duration, int scaleMode,
                       int alphaMode);  // 0x00499C60
    void TaskSpriteFx(Task& t);      // 0x00499CE0
    void TaskGoCamera(Task& t);      // 0x00460E60
    void TaskGoText(Task& t);        // 0x00460CD0
    void MapInit();                  // 0x00460F00 (+ Task_InitRoot(0x00460FF0))
    void MapFrame();                 // 0x004610F0
    void TaskMapViews(Task& t);      // 0x00482C40
    void TaskMapRoute(Task& t);      // 0x00461180
    void TaskMapAgent(Task& t);      // 0x004614C0 / 0x004618C0
    void TaskFootprint(Task& t);     // 0x00461AC0
    bool NonInteractive() const;     // 0x00413280
    bool CanContinue() const;        // 0x00406ED0
    int CreditOf(int p) const;       // 0x00406780
    bool UseCredit(int p, bool cont);  // 0x00406DC0
    void PlayerInit(Task& t, int row);       // 0x00414770
    void PlayerMain(Task& t);                // Player_Task 0x00413E90
    void PlayerTrigger(Task& t);             // 0x00414940
    void PlayerReload(int p);                // 0x00414B30
    void PlayerSetState(Task& t, int state, bool setFn);  // 0x00415080
    bool PlayerStartCheck(Task& t, int newState);         // 0x00414FC0
    void PlayerIdle(Task& t);                // 0x00414600
    void PlayerContinue(Task& t);            // 0x00414280
    void PlayerOutStart(Task& t);            // 0x00414420 (state 6)
    void PlayerOut(Task& t);                 // 0x004144C0
    void PlayerAmmoHud(Task& t);             // 0x004177D0 (state part: reload prompt counter, empty click)
    void PlayerOverlay(Task& t);             // 0x00417300
    void PlayerFlashes(int p);               // 0x00416B00
    void PlayerCrosshair(Task& t);           // 0x004169C0
    void PlayerTakeHit(int p, int flag, int kind);  // Player_TakeHit 0x00415300
    void AddScore(int p, int points);        // Player_AddScore 0x004156C0
    void HitShake();                         // Player_HitShake 0x00415270
    void UpdateRank();                       // Game_UpdateRank 0x004607B0
    void StageFrame();                       // 0x004606D0
    void Mode6Frame();                       // 0x0045FEE0 sub-states 2..4
    void SetGameMode(int m);                 // Game_SetState 0x0040E850 + Game_ApplyState 0x0040E860
    void FillHud();
    int modelWave[4] = { -1, -1, -1, -1 };  // phase of the 0x0046DAB0 vertex wave in models 0x1731..0x1734 (-1: none)
    void StampModelWaves();                 // world.cpp (FillHud)

    // hit system
    struct Target {  // entry {obj, +0x34, +0x12C, +0x130, +0x134}: the sphere centre is copied at registration
        ActorTask* obj;
        float sphere[3];
    };
    std::vector<Target> targets;      // 0x0059D8E8 (registered this frame)
    std::vector<Target> targetCopy;   // 0x005A3098 (Hit_CopyTargets 0x00405360)
    bool SpherePush(EnemyTask& e);    // 0x00405B10 (+ 0x00405760): pushed by overlapping target spheres
    bool levelMeshes = false;         // level collision mesh list (op 0x10 -> task 0x00406010) not empty
    float cac48 = 0, cac4c = 0, cac64[3] = {};  // 0x009CAC48 / 0x009CAC4C / 0x009CAC64 (last collision result)
    int hitFlag[2] = {};                  // 0x009C9010
    int hitSlot[2] = {};                  // 0x009A2D88
    int hitType[2] = {};                  // 0x009A58F8
    int shotOrder[2] = { -1, -1 };        // 0x009C8908
    // lock-on table 0x009A5EC0 (shoot_camera.h); its targets outlive their enemy tasks (the original's slots keep
    // pointing at freed task memory, whose contents stay readable)
    LockOnTable lockOn;
    std::map<uint32_t, LockOnTarget> lockTargets;  // one per enemy, by creation index
    uint32_t nextLockTarget = 0;
    void SyncLock(EnemyTask& e);
    std::vector<std::pair<int32_t, EnemyTask*>> attackList;  // 0x00409010 list 0x005A4D58

    // resources: motion files resident (op 0x56 requests pending in the queue)
    struct Request {
        uint32_t op, arg;
    };
    std::vector<Request> resQueue;
    void ProcessRequest(const Request& r);
    // model slots loaded (0x009A66AC flags 0x8001), tracked in game modes 5 / 6 / 10 / 11 (residency = true: their
    // init frees the slots but common.bin's, 0x0041D510): Mdl_Draw 0x00418560 skips models whose slot is not loaded.
    std::vector<uint8_t> modelLoaded;
    bool residency = false;
    std::set<int> pendingMotionFiles;     // requested, not yet processed
    std::set<int> loadedMotionFiles;      // loaded this stage: by the queue (op 0x56) or on demand (0x00412F50)
    std::set<int> releasedMotionFiles;    // released (op 0x57) and not loaded again
    // texture files: state byte 0x009C7328 (12-byte entries 0x009C7320, 0x1BD files; 4 = loaded). Request type 6 (op
    // 0x54, 0x0041CCA0: steps 0x0041C890 / 0x0041C950 / 0x0041CC10; a file in any non-zero state is left alone) and
    // Res_LoadTexture 0x0041CDA0 load it, type 7 (op 0x55, 0x0041CD00) unloads it. Only Spr_Draw 0x0041C630 reads it:
    // a sprite whose file (0x0057A5BC[id]) is not in state 4 is not drawn. (Model textures are created with their
    // model, 0x004AC980, and need nothing here.) Tracked where the model slots are (residency).
    std::vector<uint8_t> texFiles;
    void ResetTexFiles();  // 0x0041D510 -> 0x00418690
    std::vector<int> polFileLoads;  // pol files loaded by the queue since the last FillHud (WorldHud::polFileLoads)
    void NoteSectionPolFiles();     // + the current sections' objects' pol files

    // in-memory model edits by model id (bat skeleton draw 0x0040F960): the global model data keeps them, so every
    // later draw of that model carries the latest one (world_st1b_bats.cpp)
    std::map<int, std::shared_ptr<const ModelEdit>> modelEdits;
    // the inline 32-byte vertex records (file order, raw words: x with its flag bit 0, y, z, normal, u, v) of a model as
    // its pol file holds them, for in-memory edits made from the original data (cached; empty if not found)
    const std::vector<std::array<uint32_t, 8>>& InlineRecords(int model);
    std::map<int, std::vector<std::array<uint32_t, 8>>> inlineRecords;
    // komono kind 1 water surfaces (0x0046E3A0) and the final-boss blast streak 0x16B6 (0x00429C90): their models'
    // vertex uv as edited in memory
    std::map<int, std::vector<std::array<float, 2>>> waterUv;
    // 0x0040F960 seam-normal cache (bats / leeches, first draw of each creature): vertex indices 0x007C17B0 (A) /
    // 0x007C1840 (B, + the word 0x007C18CC after it, never matched here) and their normals 0x007C18D0 / 0x007C1A80,
    // kept as the source vertex (model 0 = the zero normal of the bss); normal-x writes persist in the global model
    struct NormalSrc {
        int model = 0, index = 0;
    };
    int batCacheIdx[2][36] = {};          // [0] A, [1] B
    NormalSrc batCacheSrc[2][36] = {};
    std::map<int, std::map<int, NormalSrc>> batNormalX;  // model -> inline vertex -> source of its normal x
    // 0x004759C0(angle): bends the book page model 0x7EE (boss name cards 0x00437AC0 / 0x00424900) before each letter
    void PageBend(int angle) {
        auto e = std::make_shared<ModelEdit>();
        e->pageBend = angle;
        modelEdits[0x7EE] = std::move(e);
    }

    // outputs of the current frame
    std::vector<WorldDraw> draws;
    // While a skeleton node callback runs (AnimNode) it gets the node's model-view matrix (the original's matrix-stack
    // top, built from the view) and every draw is recorded as model-view (viewSpace).
    bool drawMv = false;
    int nativeModelKind = -1, nativeModelNode = -1;
    struct NativeModelDrawScope {
        Impl& w;
        int kind, node;
        NativeModelDrawScope(Impl& wi, int nextKind, int nextNode = -1)
            : w(wi), kind(wi.nativeModelKind), node(wi.nativeModelNode) {
            w.nativeModelKind = nextKind; w.nativeModelNode = nextNode;
        }
        ~NativeModelDrawScope() { w.nativeModelKind = kind; w.nativeModelNode = node; }
    };
    // Draw-only code that builds its matrices on cam.view (the matrix-stack top) like the original: every draw in the
    // scope is recorded as model-view
    struct DrawMvScope {
        Impl& w;
        bool saved;
        explicit DrawMvScope(Impl& wi) : w(wi), saved(wi.drawMv) { w.drawMv = true; }
        ~DrawMvScope() { w.drawMv = saved; }
    };
    std::vector<World::RandCall> randCalls;
    std::vector<WorldEventOp> ops;
    std::vector<World::SoundEvent> sounds;
    std::vector<std::string> notes;
    WorldHud hud;
    StageResults stageResults;
    void BeginStageResults(int bonus = 0);
    bool StageResultsFrame();
    void ResultRescue(int player);

    // rand call-site attribution: sites used round-robin by the next rand calls
    std::vector<uint32_t> sites;
    size_t siteIndex = 0;

    // ---------------------------------------------------------------- frame
    void StartStage(const WorldConfig& c);
    void PresentTypingDrillGame();
    void Step();
    void BeginFrame();
    void StepResources(int steps);

    // ---------------------------------------------------------------- helpers
    int Rand(uint32_t site);
    void SetSites(std::initializer_list<uint32_t> s);
    void OnRand(int value) override;
    uint32_t CurrentSerial();
    WTask* CurrentTask();
    // nullptr when the original draws nothing (id 0, slot not loaded)
    WorldDraw* Draw(int model, const Mat4& world, uint8_t mode = 8, float alpha = -1.0f, bool alt = false, int node = -1,
                    bool viewSpace = false, bool cull = false);
    void Note(const std::string& s);
    void Sound(uint32_t id) override;
    void SoundCall(int kind, uint32_t id = 0);  // World::SoundKind: the sound-layer calls besides Snd_PlayGameSound
    void SoundStopAll() { SoundCall(World::kStopAll); }  // Snd_StopAll 0x0041D350
    uint32_t currentBgm = 0;  // 0x009C8FB8
    template <class T> T* NewTask(TaskFn f, uint32_t origFn) {
        Task* t = ts.Create(f, std::make_unique<T>());
        T& d = t->As<T>();
        d.fn = origFn;
        if (auto* actor = dynamic_cast<ActorTask*>(&d)) InitTypingSpawnOrigin(*actor);
        return &d;
    }
    const Motion* PropMotion(int id);
    const PropSkeleton& PropSkel(int type);
    const Motion* EnemyMotion(int id, int nodes);
    const Skeleton& EnemySkeleton(int kind);
    int MotionTicks(int id) const {
        if (typingReferenceActive) if (const Motion* motion = typingReference.ExtraMotion(id)) return motion->ticks;
        return exe.S16(0x004E07D0 + id * 2);
    }
    bool MotionResident(int motion);
    void MotionTouch(int motion);  // 0x00412F50: an animation fetch loads a non-resident motion's file at once
    void UpdateCamera();
    void SpawnEffectAt(const float pos[3], int pitch, int yaw, int type, int faceMode, int arg);  // 0x00407340
    void SpawnEffectBlock(const EffectSpawn& in);                                              // 0x004073B0
    void RegisterTarget(ActorTask* a);    // Hit_RegisterTarget 0x00405160
    void SpawnSpark(float viewZ, int p, float scale, float z);  // 0x004666B0
    void TaskKomono40(Task& t);           // 0x0046C570
    void SpawnBloodPool(EnemyTask& e);    // 0x00407DA0
    void TaskBloodPool(Task& t);          // 0x00407E30 / 0x00408100 / 0x00408220
    void SpawnBloodPoolAt(const float pos[3], int yaw, float scale);  // 0x00408370
    void SpawnFlyingHead(EnemyTask& e);   // 0x0040A130 (world_enemy_beh.cpp)
    void TaskKomono41(Task& t);           // 0x0046CC50
    PropWorld PropState();
    void AfterPropUpdate(const PropWorld& w);

    // ---------------------------------------------------------------- tasks (world.cpp)
    void TaskEvent(Task& t);
    void TaskCamera(Task& t);
    void TaskSky(Task& t);
    void TaskRail(Task& t);
    void TaskPlayer(Task& t);
    void TaskPlayerCount(Task& t);
    void TaskLetterbox(Task& t);
    void TaskStageDraw(Task& t);
    void TaskRain(Task& t);
    void TaskLockOn(Task& t);
    void TaskAttackOrder(Task& t);
    void TaskHit(Task& t);
    void TypingStep();
    void TypingPlayerStep();
    void ResetTyping();
    void RestoreTypingGameOptions();
    bool SpawnTypingFlow(Task& task, SpawnTask& spawn);
    void TaskTypingFlow(Task& task);
    void InitTypingSpawnOrigin(ActorTask& actor);
    void SetTypingSpawnOrigin(ActorTask& actor, const SpawnRecord& record);
    uint32_t CivWord(uint32_t address) const;
    float CivFloat(uint32_t address) const;
    void TypingAttackTimer(int operation);  // TOTD 0x004AFFF0: clear/tick/reload/minimum
    bool TypingAttackReady() const { return !cfg.portTyping || typingAttackDelay == 0; }
    void TaskSpawn(Task& t);      // actor type function (first run of a spawned actor)
    void TaskEffect(Task& t);
    void TaskSpark(Task& t);
    void TaskHanging(Task& t);
    void TaskBreakable(Task& t);
    void TaskDecor(Task& t);
    void TaskAnimated(Task& t);
    void TaskType33(Task& t);
    void TaskPlaceholder(Task& t);

    // ---------------------------------------------------------------- enemies (world_enemy.cpp)
    void TaskEnemy(Task& t);            // 0x00452DA0 on the first run, then 0x004533F0
    void EnemyCreate(EnemyTask& e);     // 0x00452DA0
    void EnemyUpdate(EnemyTask& e);     // 0x004533F0
    void EnemyProcessHits(EnemyTask& e);     // 0x00453EB0
    void EnemyCollectShots(EnemyTask& e);    // 0x004092F0
    void EnemyVoice(EnemyTask& e, int which);  // 0x0040A6F0
    void EnemyBehaviour(EnemyTask& e);
    void EnemyBeh23(EnemyTask& e);      // 0x00457B50
    void EnemyBeh9(EnemyTask& e);       // 0x004550E0
    void EnemyBeh7(EnemyTask& e);       // 0x00454F20
    // generic walking-enemy behaviours (world_enemy_ai.cpp)
    void EnemyBeh15(EnemyTask& e);      // 0x00457220 scripted walk-in
    void EnemyBeh14(EnemyTask& e);      // 0x00457160 walk on for P[1] frames (out through a door), then behaviour 1
    void EnemyBeh1(EnemyTask& e);       // 0x004554D0 idle / wait for an approach band
    void EnemyBeh2(EnemyTask& e);       // 0x00455720 approach
    void EnemyBeh3(EnemyTask& e);       // 0x00455A40 attack
    void EnemyBeh4(EnemyTask& e);       // 0x00455C30 back off
    void EnemyBeh5(EnemyTask& e);       // 0x00455670 wait for an attack rank
    void EnemyBeh6(EnemyTask& e);       // 0x00454D20 die on the spot
    void EnemyBeh18(EnemyTask& e);      // 0x004575A0 wait for a camera tick, then behaviour P byte 3
    void EnemyMotionSounds(EnemyTask& e);  // 0x00452A10
    void EnemyBeh13(EnemyTask& e);      // 0x00456F50 rise at a camera tick (world_enemy_beh.cpp)
    void EnemyBeh34(EnemyTask& e);      // 0x0045A890 walk to the civilian
    void EnemyBeh35(EnemyTask& e);      // 0x0045AAA0 motion list (attacks the civilian)
    void EnemyBeh45(EnemyTask& e);      // 0x0045C7D0 short idle, then the saved behaviour
    void EnemyBeh40(EnemyTask& e);      // 0x0045BCB0 walk past a point
    bool EnemyPointBehind(const EnemyTask& e, const float pt[3]);  // 0x0045BC10
    void EnemyScriptEnd(EnemyTask& e);  // 0x0045C8D0
    bool EnemyParentDead(EnemyTask& e); // 0x0045C8A0
    EnemyTask* SpawnRecordEnemy(uint32_t record);  // 0x00408A20 for an enemy record (civilian children)
    int EnemyMotionSet(const EnemyTask& e, int i);       // 0x00592CBC[kind][variant][i]
    float ApproachDistance(int c, int k);                // 0x009A2BE0[c][k]
    int GlobalInt(uint32_t addr, int def);               // event-written global (runner->globals)
    void EnemySetMotion(EnemyTask& e, int motion, int start, int n);  // 0x00454770
    int EnemyApproachBand(EnemyTask& e);                 // 0x00456650
    void EnemyTurnToPlayers(EnemyTask& e, int step);     // 0x00409ED0
    void EnemyTurnTo(EnemyTask& e, float x, float z, int step);  // 0x00409E80 / 0x00409F90
    void EnemyCheckParts(EnemyTask& e);                  // 0x00455920
    void EnemyDeathMotionWalk(EnemyTask& e);             // 0x00456220
    void EnemySplashList(EnemyTask& e);                  // 0x004563F0
    void EnemyDeathSplash(EnemyTask& e);                 // hook 0x00592BC4 = 0x004569B0
    void EnemyAttackSplash(EnemyTask& e, bool atTick);   // hooks 0x00592BCC = 0x00456C50 / 0x00592BD0 = 0x00456D10
    void EnemyDespawn(EnemyTask& e);                     // 0x00455490
    int EnemyPickTarget(EnemyTask& e);  // 0x00455DE0
    void EnemyFaceTarget(EnemyTask& e); // 0x00455F40
    void EnemyStartDeathMotion(EnemyTask& e);  // 0x004560B0
    void EnemyOnDeath(EnemyTask& e);    // 0x004565A0
    void EnemyReleaseTarget(EnemyTask& e);  // 0x00456520
    void EnemyEnterCorpse(EnemyTask& e);    // 0x00456740
    void EnemyDestroy(EnemyTask& e);    // Actor_Destroy 0x00409CC0
    void SpawnThrownWeapon(EnemyTask& thrower, int hand);  // 0x0045A240 (world_thrown.cpp)
    void TaskThrown(Task& t);                              // 0x0045A4F0
    void EnemyAnimate(EnemyTask& e);    // 0x00454860
    void EnemyInitPose(EnemyTask& e);   // Anim_InitEnemy 0x00410440 / Enemy_InitPose 0x00410590
    void AnimSetMotion(EnemyTask& e, int motion);                         // 0x00411930
    void AnimBlendTo(EnemyTask& e, int motion, int startTick, int n);     // 0x004119A0
    void AnimLoadSlot(EnemyTask& e, int which, int motion, int frame, int track);  // 0x00411C20
    void AnimBlendTrack(EnemyTask& e, int partial, int motion, int track);  // 0x004119F0
    void AnimSetNodeTracks(EnemyTask& e, int partial, int track);           // 0x00412200
    void AnimTrack1Start(EnemyTask& e, int partial, int motion, int start, int n, int loops);  // 0x00411B70
    void AnimTrack1Hold(EnemyTask& e, int partial, int motion, int start, int n);  // 0x00411B20
    void AnimTrack1Return(EnemyTask& e, int partial, int n);  // 0x00411BD0
    void AnimBlendLower(EnemyTask& e, int motion, int start, int n);  // 0x00411AC0
    void AnimTick1(EnemyTask& e);       // 0x004112E0
    void EnemyHitEffects(EnemyTask& e, int p);   // 0x00454050
    void EnemyHitReaction(EnemyTask& e, int p);  // 0x004543F0
    void EnemyHitMotion(EnemyTask& e, int p);    // 0x004544C0
    void EnemyArmVariant(EnemyTask& e);          // 0x00454270
    void TaskBlood(Task& t);            // 0x00407230 (created by 0x00407310) / 0x00407120 (created by 0x00407200)
    void SpawnNodeBlood(EnemyTask& e, int node);  // 0x00407200
    void AnimTick0(EnemyTask& e);       // 0x004111A0
    void AnimDraw(EnemyTask& e);        // 0x004110D0 (+ 0x00410920, 0x00410C50, 0x004114C0)
    void AnimNode(EnemyTask& e, const Skeleton& s, int idx, const Mat4& parent, const Mat4& parentMv);  // 0x004114C0
    void EnemyPostRoot(EnemyTask& e);   // 0x00454900
    void EnemyDrawNode(EnemyTask& e, int idx, const Mat4& m);  // 0x004534A0
    void EnemyHeadLook(EnemyTask& e, Mat4& m);                  // 0x00453BE0
    void EnemyShadow(EnemyTask& e);     // 0x0040A590 / 0x0040A620
    void EnemyViewPos(EnemyTask& e);    // 0x00409B70
    float FloorHeight(float x, float y, float z, int* material);  // Coll_FloorHeight 0x00409D40
    bool OnScreen(const float view[3]); // 0x00409C10

    // ---------------------------------------------------------------- civilians (world_civilian.cpp)
    void TaskCivilian(Task& t);          // 0x0048A3E0 on the first run, then 0x0048A920
    void CivCreate(CivTask& c);          // 0x0048A3E0
    void CivUpdate(CivTask& c);          // 0x0048A920
    bool CivRunScript(CivTask& c, uint32_t addr);   // 0x0048B9E0 (false: stopped at an unported op)
    uint32_t CivDryRun(CivTask& c, uint32_t addr);  // 0x0048B760
    bool CivWait(CivTask& c);            // 0x0048B1E0
    void CivSetMotion(CivTask& c, uint32_t entryFlags, int start);  // 0x0048C310
    void CivTurn(CivTask& c);            // 0x0048C850 / 0x0048C990
    void CivPostRoot(EnemyTask& e);      // node callback +0x12F0 = 0x0048D070
    void CivDrawNode(EnemyTask& e, int idx, const Mat4& m);  // node callback +0x12EC = 0x0048D1F0
    bool CivHeadLook(CivTask& c);                            // 0x0048D1F0 node 2: the head look
    bool CivOnScreen(const CivTask& c);  // 0x0045CA60
    void CivPlayerTarget(const CivTask& c, float out[3], bool fromTop);  // path < 0: the players' point (or its mirror)
    int CivLocalAngle(const EnemyTask& e, const float v[3]);  // atan2 of v in the actor's frame (0x0048C990)
    void DrawParts(EnemyTask& e);        // anim +0x1174 = 0x004124F0 / 0x00412620
    void ReleasePartModels(uint32_t parts);  // 0x004124B0 (part list in event memory)

    // ---------------------------------------------------------------- EventHost
    bool GameRunning() override;  // 0x007DCCA4 (Evt_Run 0x0045ECC0)
    int LiveEnemies() override { return liveEnemies; }
    int Count7006() override { return attackers; }
    int CountA0E8() override { return countA0E8; }
    int SegmentCamera(int camId, bool singlePose) override;  // training lesson 3 (world_modes_train.cpp)
    void LockOnSlots(LockOnSlot out[4]) override {  // (port: the Horde mode's camera stays on the rail: no lock-on targets)
        lockOn.Slots(out);
        if (hordeHoldRail)
            for (int k = 0; k < 4; ++k) out[k].active = false;
    }
    bool RailAdvance() override {  // 0x0040C790 (port: the Horde mode holds the gameplay camera where it took over)
        if (hordeHoldRail) return false;
        return NonInteractive() || (shakeTimer == 0 && players != 0);
    }
    bool PlayersReady() override;  // op 0x32 (0x0045F900)
    int ShakeY() override { return shake; }
    void Spawn(uint32_t kind, uint32_t record) override;
    void TypingWords(int group, int selector) override;
    void TypingProps(bool enabled) override;
    void TypingWordLimits(int maximum, int minimum) override;
    void TypingSceneClear() override;
    bool TypingChallenge(const EventOp& op) override;
    void TypingCountdown(int instruction, int frames) override;
    int TypingChallengeStatus() override;
    void TypingClearActorSlots() override;
    void TypingChallengeStep();
    void TypingSprite(int rawId, float x, float y, float alpha = 1, float depth = .1f,
                      uint32_t flags = 0xA, uint32_t source = 0);
    void TypingSound(uint32_t id) override;
    void TypingSoundRepeat(uint32_t id, int count, int interval) override;
    void TypingVoice(int id) override;
    int TypingPlayers() override;
    int TypingModeId() override;
    void SoundRepeat(uint32_t id, int count, int interval) override;
    void Voice(int id) override;
    void Bgm(uint32_t a, uint32_t b, uint32_t c, uint32_t d) override;
    void Resource(uint32_t opcode, uint32_t arg) override;
    void Section(uint32_t opcode, int newSection, int otherSection) override;
    void Env(const EventOp& op) override;
    void Error(const std::string& what) override;

    // ---------------------------------------------------------------- scripted / intro actors (world_actors.cpp)
    bool SpawnScripted(Task& t, SpawnTask& s);  // actor types 0x20, 0x22, 0x26, 0x60, 0x63; komono kinds 3 / 40 / 41
    void TaskVoice(Task& t);       // 0x00435AA0 voice line / subtitle (Snd_PlayVoiceLine 0x00435B80)
    void TaskSkipWatch(Task& t);   // actor 0x63: 0x00435F20 / 0x00435F40 / 0x00435FA0
    void TaskTitle(Task& t);       // actor 0x60: 0x004342E0 stage title card
    void TaskCar(Task& t);         // actor 0x26 state 1: 0x0048E600 the players' car (intro)
    void TaskCarLight(Task& t);    // komono kind 3: 0x004659D0 / 0x00465BC0 (car model patch)
    void TaskActor22(Task& t);     // actor 0x22: 0x0049B0D0 / 0x0049B260 (behaviours 0x00598000)
    void TaskActor20(Task& t);     // actor 0x20: 0x00448ED0 / 0x00449020
    void TaskScriptedPlaceholder(Task& t);
    void ApplySetupCallbacks();    // camera sub-mode setups 0x00576C14 also set the player callbacks
    uint32_t lastSetupCount = 0, lastPlayerFnCount = 0;
    // player bodies (player +0x78 objects, created by 0x00416450 at the stage start; world_player.cpp)
    std::unique_ptr<EnemyTask> playerBody[2];
    void PlayerBodyInit(int p);              // 0x00416450
    void PlayerCb80(Task& t);                // player +0x80 callbacks (0x004159A0, 0x00415B60, 0x00415BD0, ...)
    void PlayerCb7C(Task& t);                // player +0x7C callbacks (0x00415120 body draw, 0x00415180 overlay)
    void PlayerBodyNode(EnemyTask& e, int idx, const Mat4& m);  // node callback 0x00416570
    void Actor22Node(EnemyTask& e, int idx, const Mat4& m);     // node callback 0x0049D980
    void Actor22Draw(EnemyTask& e);                             // 0x0049D770
    void RailPlayersFromBodies();            // rail 0x0040C9C0 (camera sub-mode 1/1)

    // ---------------------------------------------------------------- level collision meshes (world_collision.cpp)
    std::vector<uint8_t> coli[2];            // coli0.bin at 0x0098F200, coli<stage + 1>.bin at 0x00990A00
    bool coliLoaded = false;
    std::vector<uint32_t> collList[2];       // 0x009C7040 (op 0x10) / 0x009CA0C0 (op 0x11): mesh addresses
    uint32_t collPending[2] = {}, collEnd[2] = {};  // 0x009A1A14 / 0x009A34D8, 0x009C8A80 / 0x009C8E14
    bool collHasPending[2] = {};
    struct CollCand {                        // 0x0059F4D0 entry + key 0x0059ECD8
        uint32_t key;
        float pos[3], n[3], dist2;
        uint32_t material;
    };
    std::vector<CollCand> collCands;
    uint32_t cac40 = 0;                      // 0x009CAC40 material of the last hit
    float cac50 = 0, cac54 = 0, cac58 = 0, cac60 = 0;  // 0x009CAC50 z / 0x009CAC54 x / 0x009CAC58 y, plane d
    bool CollLoad();
    const uint8_t* CollAt(uint32_t addr, size_t bytes);
    void CollListOp(int which, uint32_t list);
    void TaskCollision(Task& t);                                           // 0x00406010
    uint32_t MeshSegment(const float s[3], const float e[3], uint32_t mesh);  // 0x004AAA40
    uint32_t MeshSphere(const float c[3], float r, uint32_t mesh);           // 0x004AAFF0
    void CollAddCandidate(const float end[3]);                             // 0x00405650 / 0x00405680
    bool CollPickCandidate();                                              // 0x00405760
    uint32_t CollSegmentAll(const float s[3], const float e[3]);           // 0x004053B0
    float FloorQuery(float x, float y, float z, int* material);            // 0x00409D40
    bool MeshPush(const float c[3], float r);                              // 0x004057F0
    struct LevelShotHit {                    // shot candidate of Hit_TestLevel 0x00404B80 (0x00404CB0)
        int32_t key;                         // ftol(-view z * 10)
        float pos[3], n[3];
        uint32_t material;
        ActorTask* obj = nullptr;            // collision-mesh object (Hit_TestObjectMesh 0x00404A00)
    };
    void ShotLevel(int p, std::vector<LevelShotHit>& out);                 // 0x00404B80
    bool ShotObjMesh(int p, ActorTask& a, LevelShotHit& out);              // 0x00404A00
    void ShotImpact(int p, const LevelShotHit& h);                         // Hit_RecordImpact 0x00405260
    uint32_t ObjMeshSegment(const ActorTask& a, const float s[3], const float e[3]);  // 0x00404F60
    bool ShotNodeMesh(int p, const EnemyNode& n, LevelShotHit& out);       // 0x004048A0 (world_st2c_enemy.cpp)

    // ---------------------------------------------------------------- scene-1 objects (world_scene1.cpp)
    bool SpawnScene1(Task& t, SpawnTask& s);  // komono kinds 7, 9, 38, 39, 40 (variant 0), 44; actor types 0x24, 0x33
    bool SpawnActor25(Task& t, SpawnTask& s);                  // world_npc.cpp: 0x004840D0
    void DropItem(const BreakableProp& b, int slot);           // 0x004723F0 (breakable prop items)
    void SpawnPenaltyMark(int p, const float pos[3]);          // 0x0048E080 (civilian shot)
    void Actor25Node(EnemyTask& e, int idx, const Mat4& m);    // node callback 0x00485260
    void Actor25Flash(EnemyTask& e, const Mat4& m, int k, bool casing);  // 0x00485725 / 0x00485790 flash sprite 0xE24
    void SpawnCasing(EnemyTask& e, const Mat4& node);          // 0x00485DE0 -> task 0x00485F00 (world_npc.cpp)

    // ---------------------------------------------------------------- stage 1 after scene 1 (world_st1b.cpp)
    int rescueTotal = 0;                 // 0x009CA0EC civilians rescued
    int16_t rescueCount[10] = {};        // 0x009C89C0 per stage (event mode)
    int16_t rescueKinds[10][10] = {};    // 0x009C8EC0 [stage][i]: skeleton kind of each rescued civilian
    bool DropItemSt1b(const BreakableProp& b, int slot);       // item slots 1 / 3: 0x00471BD0 / 0x004722A0
    void CreatureItemNode(EnemyTask& e, int idx, const Mat4& m);  // node callback 0x00463F90
    void EnemyBeh39(EnemyTask& e);       // 0x0045BAD0 hidden child of a civilian until the civilian's op 0x1A
    void EnemyBeh33(EnemyTask& e);       // 0x00459080 weapon thrower (world_st2_enemy.cpp)
    void EnemyBeh26(EnemyTask& e);       // 0x004581A0 scripted jump to a point (world_st1b.cpp)
    void EnemyBeh12(EnemyTask& e);       // 0x00456DF0 fall to the floor, corpse (world_st1b.cpp)
    void EnemyBeh37(EnemyTask& e);       // 0x0045B380 carry an object (world_st1b_held.cpp)
    void EnemyBeh36(EnemyTask& e);       // 0x0045B190 motion list setting event flags (world_st1b_held.cpp)
    void CivAttachOp(CivTask& c, int op, uint32_t a1, uint32_t a2);  // script ops 0x13 / 0x14 / 0x15 (world_st1b_civ.cpp)
    void CivAttachments(CivTask& c);  // 0x0048CD10
    // destroyed civilians stay readable for their children's +0x1394 (the original's freed block keeps its contents
    // until the pool reuses it); cleared by StartStage (Mem_Init)
    std::vector<std::unique_ptr<TaskData>> civGhosts;
    ActorTask* actor8E4C = nullptr;  // 0x009C8E4C the actor whose task ran last (enemies, civilians, actor 0x31)
    // stage-1 boss (world_st1b_boss.cpp)
    bool SpawnBoss22(Task& t, const SpawnRecord& r);  // actor 0x22 variant 1 (0x0049B0D0)
    bool SpawnStageClear(Task& t, SpawnTask& s);  // actor types 0x61 / 0x62 (0x00434EF0 / 0x00435930)
    void StageEnd() override;   // 0x0045ED90 (world_st1b_boss.cpp)
    void Mode6StageEnd();        // game mode 6 sub-states 5 / 6 (0x00460390 / 0x004603B0) and 1 after them
    int stageEndCounter = 0;     // 0x007DCCCC
    int nextEventMode = -1;      // 0x009A1A08 after the stage end
    void SpawnSoundRepeat(uint32_t id, int count, int interval);  // 0x00435EE0 (task 0x00435E90)
    void SpawnEffectScaled(const float pos[3], int type, int arg);  // Eff_SpawnScaled 0x00407BC0 (world.cpp)
    float bossBar = 0;    // 0x009C8E10 boss health bar value (-1: bar ends)
    int bossActive = 0;   // 0x009CA0EA
    void EnemyLandingDust(EnemyTask& e); // hook 0x00592BC8 = 0x00456B70 (world_st1b.cpp)
    bool SpawnSt1b(Task& t, SpawnTask& s);  // actor types first reached after scene 1 (chained from SpawnScene1)
    uint8_t dcc48 = 0;                   // 0x007DCC48 the scene-3 creature group has started (the board falls)
    // bat group (actor type 0x40 variants 0 / 1, world_st1b_bats.cpp)
    void SpawnBatGroup(const SpawnRecord& r, int variant, int count = 0);  // 0x0043BD30 (count: port only, the Horde mode)
    int batAlive = 0;                    // 0x007DCBD0 bats alive in the group
    int batAttacker = 0;                 // 0x007DCBD4 index of the next bat to dive
    uint32_t batLastAttack = 0;          // 0x007DCC1C frame counter 0x009A32A0 at the last dive
    ActorTask* batList[10] = {};         // 0x007DCC20
    void AnimatedPropMesh(ActorTask& a, const AnimatedProp& p, const PropAnim& before);  // 0x00474120 mesh target part
    // ---------------------------------------------------------------- later stages (world_st2.cpp)
    uint8_t komono40Intact[12] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };  // 0x007DCDB8 (per game: 0x00463680)
    int8_t komono40Count = 0;  // 0x007DCF18: kind-9 pieces broken (game type 1; reset by their ctor 0x004636A0, route 2 at two)
    WorldCarry MakeCarry() const;  // the carry from this world's own state (in-run stage switch)
    void ChainStage();             // mode 6 sub-state 1 after a stage end: the next stage inside the same run
    bool LoadStageTable(int eventMode, std::string* error, int gameType = -1, int typingMode = -1);  // Evt_LoadTable + prepared typing data
    void ApplyCarry();  // WorldConfig::carry: mode 6 sub-state 1 (0x0045FFF0) instead of sub-state 0 (0x0045FEF0)
    bool SpawnStage2(Task& t, SpawnTask& s);  // world_st2_actors.cpp: stage-2 actor types (first run)
    bool EnemyBehaviourSt2(EnemyTask& e);     // world_st2_enemy.cpp: behaviours first met in stage 2 (false: not ported)
    void EnemySetupKind12(EnemyTask& e);      // world_st2_enemy.cpp: Enemy_SetupKind 0x00452FD0 case 0xC
    void EnemySetupKind2(EnemyTask& e);       // world_st2c_enemy.cpp: Enemy_SetupKind 0x00452FD0 cases 2 / 3
    bool EnemyBehaviourSt2c(EnemyTask& e);    // world_st2c_enemy.cpp: behaviours first reached after 22122
    uint32_t& GroupCount() { return runner->globals[0x009C8A74]; }  // 0x009C8A74 (op 0x4D clears it)
    // ---- stage-2 boss (world_st2c_boss.cpp; notes/agents/stage2c.md) ----
    bool SpawnStage2c(Task& t, SpawnTask& s);  // actor types 0x14 (boss) / 0x16 (water level), komono kind 18
    void PlayerBonus() override;               // event op 0x2B = 0x0045FE40 (hit-rate bonus, world_st2c_boss.cpp)
    float st2Water = 0;                        // 0x007DCC4C block +8 (actor type 0x16 = 0x00442290: record y)
    std::shared_ptr<TaskData> st2Waves;        // 0x007DCC4C wave objects (actor type 0x17, world_st2_water.cpp)
    float st2Feet[4][4] = {};                  // 0x009A3500: the stage-2 boss's foot points {strength, x, y, z} (actor 0x15)
    void SpawnNameCard(uint32_t rec);          // 0x00437A70 (world_st1b_boss.cpp)
    void SpawnFishJump(float x, float y, float z, int frames, int kind);  // 0x00438640 (world_st3_fish.cpp)
    void SpawnExplosionAt(const float pos[3], const int rot[3], int type, float scale);  // 0x0043FCA0 (world_st3_vehicle.cpp)
    EnemyTask* actor33 = nullptr;             // 0x009A5C34 actor 0x33 state-1 object (ridden by behaviour 29)
    void SpawnSplashRing(const float pos[3], float base);  // 0x004567C0 (world_enemy_beh.cpp)
    void SpawnHitBlood(EnemyTask& e, int slot, float scale);  // 0x00407310 (task 0x00407230), world_enemy.cpp
    void SpawnFlyingHeadModel(EnemyTask& e, int model);      // 0x0040A130(obj, model), world_enemy_beh.cpp
    ActorTask* pushObj = nullptr;                             // 0x009C71C8: the object of the last 0x00405B10 push
    void StartLightTask(const EventOp& op);                   // event op 0x17: task 0x0040E3B0 (world_st2_misc.cpp)
    void SpawnSmoke(float viewZ, int p, float z);             // 0x00465860 (task 0x00465950), world_st1b.cpp
    int bat43Attacker = -1;                                // 0x008111E0: index of the attacking bat (world_st2_bats.cpp)
    float bat43SegStep = 0;                                     // 0x007DCC68 (0.06, set by the bat ctor 0x00445DB0)

    // ---------------------------------------------------------------- stage 3 (world_st3*.cpp)
    bool Actor25Extra(EnemyTask& e, int extra, int path);  // 0x00484FF0 extras (P s16 6); false: not ported
    void Actor20BoxWalk(EnemyTask& a);                      // 0x00449020 variant 2 (world_st3.cpp)
    bool SpawnStage3(Task& t, SpawnTask& s);  // world_st3_actors.cpp: stage-3 actor types (first run)
    ActorTask* obj5C34 = nullptr;             // 0x009A5C34: the players' boat (0x0048EAD0) / actor 0x33 (0x004331D0)
    void SpawnWake(EnemyTask& e, float waterY, int yaw);  // world_st3_enemy.cpp: 0x00408740 (two wake tasks 0x004083D0)
    void AddLife(int p);                                   // Player_AddLife 0x00415630 (world_st3_actors.cpp)
    void SpawnAimFx(float viewZ, int p, float z);          // 0x00465860: aim effect task 0x00465950 (world_st3_actors.cpp)
    bool EnemyBehaviourSt3(EnemyTask& e);  // world_st3_enemy.cpp: behaviours first met in stage 3 (false: not ported)
    void SpawnFish(Task& t, SpawnTask& s);  // actor type 0x51 (0x00438540, world_st3_fish.cpp)
    int fishSlots[4] = {};                  // 0x009A2C20: fish attack points taken
    float waterLevel = 0;                   // 0x007DCBB0 (fish record kind 6)
    ActorTask* obj9A2C88 = nullptr;         // 0x009A2C88: the last vehicle (actor 0x13) set up
    void SpawnVehicle(Task& t, SpawnTask& s);         // 0x0043FE10 (world_st3_vehicle.cpp)
    EnemyTask* SpawnVehicleEnemy(const SpawnRecord& r);  // actor 0x18 child (0x0045CD60)
    bool EnemyBehaviourVehicle(EnemyTask& e);         // behaviour 46 (0x0045CFC0)
    void CivOnVehicle(CivTask& c);                    // 0x0048B140
    bool EnemyThrowReady(EnemyTask& e);               // 0x00458E10 (world_st3_enemy.cpp)
    void SpawnBirds(Task& t, SpawnTask& s);           // actor type 0x46 (0x0042D9C0, world_st3_birds.cpp)
    void* birdTable[75] = {};                         // 0x007DC918[group * 25 + index]

    // ---------------------------------------------------------------- stage 4 (world_st4*.cpp)
    bool EnemyBehaviourSt4(EnemyTask& e);  // world_st4_enemy.cpp: behaviours first met in stage 4 (false: not ported)
    void EnemySetupKind14(EnemyTask& e);   // world_st4_enemy.cpp: Enemy_SetupKind 0x00452FD0 case 0xE
    bool SpawnStage4(Task& t, SpawnTask& s);  // world_st4_actors.cpp: stage-4 actor types (first run)
    Mat4 camViewPrev = Mat4::Identity();  // active view before this frame's camera task (built the nodes' model-views)
    void TargetsReset() override { targetTaken[0] = targetTaken[1] = 0; }  // 0x00403710 (gameplay camera command)
    float impactPos[2][3] = {}, impactN[2][3] = {};  // 0x009A2C40 + p * 0x1C: last impact (Hit_RecordImpact 0x00405260)
    uint32_t impactMat[2] = {};                      // +0xC material
    bool SpawnBoss4(Task& t, SpawnTask& s);          // actor type 0x19 (0x004917E0), world_st4_boss.cpp
    void AnimAttach17(EnemyTask& e, int i, uint32_t def, uint32_t tab);  // skeleton 0x17 attachments (world_st4_actors.cpp)
    // vehicle (actor 0x13) motions 2 / 3 / 4 (0x00440190 table: 0x004408A0 / 0x00440AD0 / 0x00440C20), world_st4_vehicle.cpp;
    // data = the motion's block (ctl +4); returns 0 not ported, 1 done, 2 the task was killed
    int VehicleMotionSt4(EnemyTask& v, int mode, int16_t& state, float& alpha, int16_t& model, uint8_t& drawMode, int* data);
    void SpawnBird0(const SpawnRecord& r);  // actor 0x46 variant 0 (0x0042D9C0 -> task 0x0042E230), world_st4_birds.cpp
    void BirdSpawnBody(EnemyTask& member, int group, int idx);  // 0x0042E060 companion (world_st3_birds.cpp)
    void BirdShotFx(EnemyTask& b, uint32_t siteSound, uint32_t siteShooter);  // shot sound / score / blood (world_st3_birds.cpp)
    // ---- stage-3 boss (world_st3_boss.cpp, notes/agents/stage3b.md)
    void SpawnHydra(Task& t, SpawnTask& s);           // actor type 0x45 (0x0041FC00)
    std::shared_ptr<HydraState> hydra;                // boss block 0x007DC6F0.. / 0x00811200
    void SpawnBossBar(float x, float y);              // 0x00435E50 (task 0x00435C80, world_st1b_boss.cpp)
    void SpawnDecor44(const SpawnRecord& r);          // actor 0x44 state 14 (0x004736D0 -> task 0x004758E0)
    int goOverCounter = 0;                            // 0x007DCCD4 (mode 6 sub-states 0xB / 0xC, world_player.cpp)

    // ---------------------------------------------------------------- stage 5 (world_st5*.cpp, notes/agents/stage5.md)
    bool SpawnStage5(Task& t, SpawnTask& s);          // world_st5_actors.cpp: stage-5 actor types (first run)
    std::shared_ptr<St5State> st5;                    // stage-5 state (world_st5.h)
    bool AttachDraw4C(EnemyTask& e, int i, uint32_t def);  // 0x004198B0 / 0x00419B40 for skeleton 0x4C (attachment i)
    bool AttachDraw18(EnemyTask& e, int i, uint32_t def);  // 0x00419B40 case 0x18 (world_st2_actor31.cpp)
    void SpawnBoss5(Task& t, const SpawnRecord& r);  // actor type 0x32 (0x0047F5F0, world_st5_boss.cpp)
    void SkyPanels();  // Sky_Task 0x004132D0 types 8 / 11 (0x00413620) and 10 (0x00413500): view-space panels
    bool EnemyBehaviourSt5(EnemyTask& e);  // world_st5_enemy.cpp: behaviours first met in stage 5 (false: not ported)
    void EnemyRide5C34(EnemyTask& e);      // 0x0045E770 (enemy +0x136C & 0x10000000: riding 0x009A5C34)
    void EnemySetupRide(EnemyTask& e);     // Enemy_SetupKind 0x00452FD0, record flag 8

    // ---------------------------------------------------------------- stage 6, final boss, ending (world_st6*.cpp, stage6.md)
    bool SpawnStage6(Task& t, SpawnTask& s);          // world_st6.cpp: stage-6 actor types (first run)
    bool SpawnFinalBoss(Task& t, const SpawnRecord& r);  // world_st6_boss.cpp: actor 0x2D variant 1 (0x00426A70)
    // ---- the ending (world_ending.cpp): game mode 6 sub-states 7 (0x00431550), 8 (0x00431660), name entry 9 / 10
    bool endingRoot = false;                          // StartStage builds the ending root 0x00431740 (no rain / lock-on)
    void EndingFrame();
    void NameEntryPlayer(Task& t);                    // player task functions 0x00414540 / 0x00414570
    bool SpawnEnding(Task& t, SpawnTask& s);          // actor types of event mode 9 (staff roll, results, the end)
    std::shared_ptr<TaskData> ending;                 // ending / name-entry state (world_ending.cpp)
    std::shared_ptr<St6State> st6;                    // stage-6 state (world_st6.h)
    void EnemySetupKind18(EnemyTask& e);              // world_st6_enemy.cpp: Enemy_SetupKind 0x00452FD0 cases 9 / 0x12
    void EnemyKind9Update(EnemyTask& e);              // 0x00453290 (kind-9 hit proxy)
    EnemyTask* Kind9Owner(EnemyTask& e);              // its kind-0x12 owner while alive
    bool EnemyFadeNode(EnemyTask& e, int base);       // 0x004534A0 nodes 0x1C6C / 0x1C7C
    void EnemyBeh8(EnemyTask& e);                     // 0x00454FD0 blinking corpse
    bool EnemyBehaviourSt6(EnemyTask& e);             // behaviours first met in stage 6 (false: not ported)
    bool AttachDrawSt6(EnemyTask& e, int i, uint32_t def, uint32_t tab, int skinNodes);  // 0x00419B40 kinds 9 / 0x12 / 0x4D..0x52
    int civTotal = 0;                                 // 0x009A21BA civilians met (0x0045FEF0: 0 at the game start)
    int16_t civCount[10] = {};                        // 0x009C9100 per stage (event mode)
    void CountCivilian() {
        ++civTotal;
        if (cfg.eventMode >= 0 && cfg.eventMode < 10) ++civCount[cfg.eventMode];
    }

    // ---------------------------------------------------------------- front end (world_front.cpp, frontend.md)
    // World::Boot: Step() is then one Game_Update 0x0040E730 for every game mode (FrontStep). The stage modes (5, 6, 7,
    // 10, 11) reuse StartStage / Mode6Frame / GameOverFrame / StageFrame; inFrontStep tells StartStage that this frame's
    // input already ran.
    bool frontEnd = false;
    bool inFrontStep = false;
    struct FrontScreen {               // 0x0049E950 slot (0x007DDAA8 + i * 0x28)
        std::string file;
        float x = 0, y = 0, w = 0, h = 0, z = 0, u = 1, v = 1, alpha = 1;  // +4.. +0x20
        bool visible = false;          // +0x24 (0x0049EB30)
    };
    struct Front {
        int modeRequest = -1;          // 0x007C17A0 (Game_SetState 0x0040E850; Game_ApplyState 0x0040E860 commits it)
        int modeFrames = 0;            // 0x009A5C44
        uint32_t bootFrames = 0;       // 0x009A5C40
        int32_t blink = 0, blinkLast = 0;  // 0x005A4D44 / 0x005A4D48 (0x004067D0, reset by 0x004067A0)
        int demoIndex = 0;             // 0x009A3400
        int demoActive = 0, demoFrames = 0;  // 0x007DC90C / 0x007DC910 (demo timer task 0x00426960)
        int gameType = 0;              // 0x009CA08C
        int menuCursor = 0;            // 0x009A2226
        int startPlayer = 0;           // 0x009A3790 (0x00404460: the player whose start was seen)
        bool menuConfirmed = false;    // 0x009A21C0 (a mode-select entry 0..3 was confirmed this frame)
        int cheatLen = 0;              // 0x009C8FBC
        uint8_t cheat[16] = {};        // 0x009A34E0 (title: L / R presses)
        int cheatFlag = 0;             // 0x009C87FC
        int bootCounter = 0;           // 0x007DAC58
        float bootFade = 0;            // 0x007DAC5C
        std::vector<FrontScreen> screens;  // 0x007DDAA8 (count 0x007DE4A8)
        bool quit = false;             // 0x0049E930
        int dimCounter = 0;            // 0x007C1EB0 (mode-select dimming, 0x00413CC0)
        // port only (WorldConfig::portFeatures, world_scene_select.cpp): the Scene Select entry was confirmed (the
        // boss-mode game's actor 0x6C becomes the chapter list), and the stage a confirmed chapter starts at the next
        // GameStartFrame (-1: the normal start)
        bool sceneSelect = false;
        int sceneStart = -1;
        // port only (world_horde.cpp): the modern menu's "Horde Mode" entry was confirmed; the next GameStartFrame
        // starts the Horde game (stage 1's street)
        bool hordeStart = false;
        bool randomizerStart = false;  // Original mode with a randomized campaign
        int nightShiftPage = 0, nightShiftRow = 0, nightShiftTable = 0;
        bool typingHome = false, menuSwitchTargetTyping = false;
        int menuSwitchAge = -1;
        bool typingOptionsLoaded = false;
        int typingSoundEffect = 0, typingSoundMusic = 0;
        int nightShiftAimX = -1, nightShiftAimY = -1;
        struct Recording { uint32_t id; std::string name; };
        std::vector<Recording> extrasMusic, extrasEffects;
        bool extrasLoaded = false;
        int modelViewerRequests = 0;
        int extrasPage = 0;  // 0 hub, 1 Music Player, 2 Sound Effects; Model Viewer is a separate port modal.
        int extrasColumn = 0, extrasControl = 0, extrasRow[2] = {}, extrasTop[2] = {};
        int extrasWheel = 0, extrasWheelColumn = -1;
        int extrasAimX[2] = {-1,-1}, extrasAimY[2] = {-1,-1};
        int portLobbyRequests = 0, portLobbyAction = 0;
        uint32_t portLobbyRoom = 0;
        std::vector<PortHomeRoom> homeRooms;
        std::string homeRoomsStatus = "ROOMS UNAVAILABLE";
        std::vector<HordeLeaderboardEntry> hordeLeaderboard;
        std::vector<SharedLeaderboardEntry> typingLeaderboard;
        bool nightShiftShared = false;
        int portLeaderboardRequests = 0, sharedTable = -1;
        std::vector<SharedLeaderboardEntry> sharedLeaderboard;
        std::string sharedLeaderboardStatus;
        int portLeaderboardPublishRequests = 0, publishCursor = 0;
        bool leaderboardPublishable = false;
        std::string leaderboardPublishStatus;
        std::string leaderboardInitials;  // online name; never stored in the original three-letter save fields
        int nightShiftDisplayRequests = 0, nightShiftDisplayPage = 0, nightShiftReturnPage = 0;
        // how-to-play (mode 9, 0x004996C0)
        int tutEnd = 0;                // 0x007DD164 (camera tick > 0xE6: end the mode)
        int tutClock = 0;              // 0x007DD15C (script task 0x00499FE0)
        int tutHold = 0;               // 0x007DD154 (1: the camera holds)
        int tutPause = 0;              // 0x007DD16C (1: the zombies' motions hold)
        int tutLives = 3;              // 0x007DD160
        int tutBullets = 6;            // 0x007DD150
        int tutReload = 0;             // 0x007DD158 (1: RELOAD prompt, 2: done)
    } front;
    void FrontStep();                  // Game_Update 0x0040E730 with the mode dispatch Game_UpdateMode 0x004608A0
    void FrontStartStage(const WorldConfig& c);  // StartStage from a mode function (tracer rolls kept)
    void FrontModeFunction();          // 0x004608A0 switch
    void FrontApplyMode();             // Game_ApplyState 0x0040E860
    void FrontStartCheck();            // 0x004043C0 (start in the title / attract modes)
    void FrontPressStart();            // 0x004067D0
    void FrontButtons();               // Game_UpdateInput's button part for mode 5 (the demo's guns are its own)
    void FrontResetBlink();            // 0x004067A0
    void FrontAfterStart();            // 0x0040AC10 (start pressed in the title / attract)
    int FrontScreenAdd(uint32_t nameVa, int x, int y, int w, int h, float z, float u, float v, float alpha);  // 0x0049E950
    void FrontScreensFree();           // 0x0049ED70
    void BootFrame();                  // game mode 16, 0x0041E1F0
    void TitleFrame();                 // game mode 3, 0x00495EB0
    void MenuFrame();                  // game mode 4, 0x00496200
    void DemoFrame();                  // game mode 5, 0x00426800
    void GameStartFrame();             // game mode 6 sub-state 0, 0x0045FEF0
    void AdvFrame();                   // game modes 10 / 11, 0x0041F9B0 / 0x0041FB00
    void TutorialFrame();              // game mode 9, 0x004996C0
    void TaskTitleCamera(Task& t);     // 0x004961A0
    void TaskTitleLogo(Task& t);       // 0x004960A0
    void TaskMenu(Task& t);            // 0x00496960
    void TaskScreens(Task& t);         // 0x00496840 -> 0x0049EAF0
    void TaskDemoTimer(Task& t);       // 0x00426960
    void TutorialInit();               // root 0x00499F90
    void TaskTutCamera(Task& t);       // 0x0049AE90
    void TaskTutScript(Task& t);       // 0x00499FE0
    void TaskTutLabel(Task& t);        // 0x0049AD70 / caption 0x0049ADC0
    void TaskTutReload(Task& t);       // 0x0049AC40
    void TaskTutTorches(Task& t);      // 0x0049AB80
    void TaskTutBullets(Task& t);      // 0x0049AA00
    void TaskTutFloor(Task& t);        // 0x0049AE20
    void TaskTutZombie(Task& t);       // 0x0049A210 / 0x0049A470 / 0x0049A680 / 0x0049A7F0 / 0x0049AFB0
    void TaskTutFlash(Task& t);        // 0x0049AA90
    void TaskTutShot(Task& t);         // 0x00416E90
    void TutCaption(int kind, float x, float y, int duration);  // 0x0049A8C0

    // ---------------------------------------------------------------- alternate routes (world_routes.cpp, routes.md)
    bool SpawnRoutes(Task& t, SpawnTask& s);  // actor type 0x11, komono kinds 0 / 6 (stage-1 no-rescue route)
    std::shared_ptr<TaskData> routes;         // barrel table 0x007DCDD4 (world_routes.cpp)
    // port only: the Horde mode's state (world_horde.cpp; null outside a Horde game) and its hold on the rail
    std::shared_ptr<TaskData> horde;
    std::shared_ptr<TaskData> prototype;  // hidden authored chapter
    bool hordeHoldRail = false;
    bool hordeBlackout = false;  // the store page: no sky / rain (the stage itself is off through 0x009A5900 bit 5)
    bool EnemyBehaviourRoutes(EnemyTask& e);  // behaviours first met on the alternate routes (false: not ported)
    void SpawnHangingCopy(const HangingProp& p);  // world.cpp: a hanging-prop task 0x00473CF0 from a prepared block
    bool SpawnFishSchool(Task& t, SpawnTask& s);  // actor 0x42 (world_routes_fish.cpp)
    std::shared_ptr<TaskData> fishSchool;         // 0x007DCA48.. (world_routes_fish.cpp)
    void SpawnBlood430BD0(const float view[3]);                               // 0x00430C50 (world_st1b_bats.cpp)
    void SpawnSplash43E4C0(int yaw, float x, float y, float z, float scale);  // 0x0043E4C0 (world_st1b_bats.cpp)

    // ---------------------------------------------------------------- game types / menu modes (world_modes*.cpp)
    std::shared_ptr<ModesState> modes;  // persistent data, mode 8 / 12 / 15 state, select screens (world_modes.h)
    // Presentation-only completion snapshot. Never serialized into lockstep / save data.
    bool portLeaderboardRecording = false, portRunRecorded = false;
    bool portRunOriginEligible = false;
    bool portRunJoined[2] = {}; // this run's participation survives a player's continue/out state
    int portRunParticipants = 0;
    LeaderboardRunSnapshot portLeaderboardRun;
};

// 0x00425F40: boss-mode (game type 3) grade / record update when a boss is down (world_modes_boss.cpp; no-op otherwise)
void BossRecords(World::Impl& w);
// 0x004603B0 for game types 2 / 3 (world_modes_end.cpp): the next stage / scene, the name entry or the select again
bool ModesStageEnd(World::Impl& w);
// 0x009C8FB1: the training lesson chosen (world_modes_train.cpp; 0 without the persistent data)
int TrainLesson(World::Impl& w);
// 0x00453B30: an enemy's node callback in game type 2 while the lesson is not running (plain 0x00453AE0 draws)
void TrainPlainNode(World::Impl& w, EnemyTask& e, int idx, const Mat4& m);
// game type 1: player p's damage factor 0x009A224C + p * 0x14 and the item-0x1D boost 0x004C4CA0 (0 none); false without
// the persistent data (world_modes_orig.cpp)
bool ItemDamage(World::Impl& w, int p, float* factor, float* boost);
// game type 1 pickups (world_modes_items.cpp): 0x004629C0 item pick, generic komono kinds 70..72, 0x00466B40 drop
// (y, item slot, source position / area / area count / life), komono 69 (0x00470500), 0x00467B90
void ItemPick(World::Impl& w, int slot, int16_t* item, int16_t* model, int16_t* model2, float* scale, uint32_t randSite = 0x004629DE);
void SpawnChainItem(World::Impl& w, float y, int slot, const float x0z2[3], int area, int areaCount, int life);  // 0x004699C0
void SpawnStageItem(World::Impl& w, const SpawnRecord& r);
void SpawnDropItem(World::Impl& w, float y, int slot, const float pos[3], int area, int areaCount, int life);
void SpawnKomono69(World::Impl& w, const SpawnRecord& r);
void SpawnItemAt(World::Impl& w, const float pos[3], int area, int areaCount, int life, int slot);
bool PlayerHasItem(World::Impl& w, int item);  // 0x00461C70
void CivItemGiven(World::Impl& w, int kind);  // 0x0048DD60: a civilian's item handed over (inventory, caption)
void ItemInventoryCaption(World::Impl& w, int item, int player = -1, bool randomized = false);
// Actor type 0x44 state 17 (task 0x00474F30) in game type 1 (world_modes_items.cpp): the fields the two ports
// (world_st1b.cpp MeshPropTask, world_st2_actors.cpp Prop17Task) share. Returns false when the object is destroyed.
struct Prop17Type1 {
    int8_t shot = 0;        // +0x192 (1: shot with the record's item, the swing table runs)
    int8_t kind = 0;        // +0x194 swing table 0x005960B4[kind]
    int16_t life = 1;       // +0x11C
    int c1DC = 0;           // +0x1DC (< 1: mirrored swing)
    int flag2A0 = -1;       // +0x2A0 event flag that sets the route branch 2 (then -1); item slot for 0x00467B90
    int t2A8 = 0, t2AC = 0, t2B0 = 0;  // +0x2A8 swing frame, +0x2AC blink count, +0x2B0 stage-3 counter
    int8_t items[4] = { -1, -1, -1, -1 };  // +0x1FC / +0x202 / +0x208 / +0x20E (P bytes 0x20..0x23)
};
bool Prop17GameType1(World::Impl& w, ActorTask& a, Prop17Type1& s, float pos[3], int rot[3]);
void Prop17Created(World::Impl& w);  // the ctor 0x00473A70: 0x009A26EC = 0
void SetItemTaken(World::Impl& w, int v);  // 0x007DCD14
int ItemLivesMax(World::Impl& w, int p);
int ItemMagazine(World::Impl& w, int p);
void ItemsPlayerStart(World::Impl& w, int p, int row);  // 0x00414770 game type 1 part (world_modes_weapons.cpp)
void ItemsFinalBoss(World::Impl& w);
void ModesSaveItems(World::Impl& w);
int ItemWeapon(World::Impl& w, int p);
bool ItemFlag(World::Impl& w, int i);    // game type 1: 0x009C88A8 + i (items 0x15, 0x1D, 0x1E, 0x1F, 0xC / 0x14)  // game type 1: the special weapon type 0x009A2247 + p * 0x14 (0: none)  // game over in game type 1: inventory -> save, Game_Save  // 0x00428400 game type 1: item 0x18 + 1, save flags 0x009C9F5E | 1, 0x009C9F5F    // game type 1: (s8) 0x009A2248 + p * 0x14 (-1 infinite; 6 without the persistent data)    // game type 1: 0x009A2245 + p * 0x14 (5 without the persistent data)
// 0x00401AE0: Euler angles of a rotation matrix from its y axis (world_npc.cpp)
void MatrixToAnglesYAxis(const Mat4& top, int* ax, int* ay, int* az);

// world_st2_water.cpp: actor types 0x15 / 0x17, the water level 0x00442390, a new water block (actor 0x16)
bool SpawnWater(World::Impl& w, Task& t, SpawnTask& s);
float WaterLevelAt(World::Impl& w, const float p[3]);
void WaterReset(World::Impl& w);
// 0x004567C0(pos, base scale): water splash ring task 0x00456880 (world_enemy_beh.cpp)
void SpawnSplashRing(World::Impl& w, const float pos[3], float base);

World::Impl* CurrentWorld();  // the world whose tasks are running
bool TypingNativeProp(World::Impl&, ActorTask&, TypingTargetSpec&, bool complete);
bool TypingBoss1(World::Impl&, ActorTask&, TypingTargetSpec&, bool complete);
bool TypingBoss2(World::Impl&, ActorTask&, TypingTargetSpec&, bool complete);
bool TypingBoss3(World::Impl&, ActorTask&, TypingTargetSpec&, bool complete);
bool TypingBoss4(World::Impl&, ActorTask&, TypingTargetSpec&, bool complete);
bool TypingBoss5(World::Impl&, ActorTask&, TypingTargetSpec&, bool complete);
bool TypingBoss6(World::Impl&, ActorTask&, TypingTargetSpec&, bool complete);
void TypingDeliver(ActorTask&, int node);
float TypingDamage(ActorTask&, float base);
void SetCurrentWorld(World::Impl* w);

// Task trampoline for Impl member task functions.
template <void (World::Impl::*M)(Task&)> void WorldTramp(Task& t) { (CurrentWorld()->*M)(t); }

}  // namespace game
