#pragma once

// Native event-script VM and camera command system (see notes/agents/events.md, notes/agents/shoot_camera.md).
//
// Per-frame API for the world runtime, in the original order (task list of the stage task 0x00460710,
// confirmed by the captured task order):
//   1. Evt_Run 0x0045ECC0             EventRunner::RunEvents()
//   2. env interpolator 0x0040AD90    (game::Environment::Step; ops 0x18..0x27 but 0x17 are applied by the VM)
//   3. camera task 0x004022B0         EventRunner::CameraTask() = CameraDirector::Frame(): mode function
//                                     (queue 0x00402320 -> command / shooting camera 0x00402650), the event-mode
//                                     mirror of context 2, Cam_BuildViews 0x00401F40 (views + mode commit)
//   4. 0x004132D0                     (not here)
//   5. rail step 0x00414F10 -> 0x0040C370 -> [0x009C7080]    EventRunner::RailStep() (also camera.playerPos)
//   6. lock-on table task 0x00408DD0  game::LockOnTable::Build() (shoot_camera.h; owned by the world)
//   7. ... actors (enemies register with LockOnTable::Register(obj, camera.playerPos) when they update),
//      demo timer 0x00426960          (not here)
// EventRunner::Frame() does 1, 3 and 5. After CameraTask() the render view of the frame is
// camera.ActiveView() (= context camera.activeCtx, 0x009A6000 + i * 0x1A4); camera.ctx[i] has every context.
// A world host answers EventHost::LockOnSlots with LockOnTable::Slots (the camera reads slots 0..3).
//
// Gameplay inputs (enemy counts, lock-on slots with their enemies' positions, hit shake) are read through
// EventHost at the moment the original reads its globals; actors run after the camera, so the values a host
// returns during frame N are those the actors left at the end of frame N - 1 (plus spawns made by the VM in
// frame N). Everything another subsystem owns (spawns, sounds, resources, sections, players) goes through
// EventHost. Globals the original writes and nobody here interprets are kept in `globals` keyed by address.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "game/camera.h"
#include "game/environment.h"
#include "game/event.h"
#include "game/exe_image.h"
#include "game/shoot_camera.h"

namespace game {

class EventHost {
public:
    virtual ~EventHost() = default;

    // ---- values the VM / camera read from other subsystems ----
    // 0x007DCCA4, computed at the top of Evt_Run: a player is in play (state 5 with lives) or the mode is
    // non-interactive (0x00413280: game modes 5, 9, 10, 11, or 6 with sub-state 8).
    virtual bool GameRunning() { return true; }
    virtual int LiveEnemies() { return 0; }        // 0x009C904A (op 0x44, camera styles 0/1)
    virtual int Count7006() { return 0; }          // 0x009C7006 (op 0x43, camera style 2)
    virtual int CountA0E8() { return 0; }          // 0x009CA0E8 (op 0x46)
    virtual bool Busy93C() { return false; }       // 0x009CA93C != 0 (op 0x47)
    // Lock-on slots 0x009A5EC0 (4 x 8 bytes) with the body point (+0x100) and target player (+0x121) of each
    // slot's object. Read by the shooting camera 0x00402650 / 0x00403050 and the rail camera 0x00402E00.
    virtual void LockOnSlots(LockOnSlot out[4]) { (void)out; }
    // 0x009CA0E4 (int): vertical aim offset used by Cam_BuildViews for the active context (HYPOTHESIS: the
    // player-hit shake of 0x00415270; 0 while nobody is hit).
    virtual int ShakeY() { return 0; }
    // Oracle hook (tests only): >= 0 replaces the alignment test of the return-to-rail step 0x00402740
    // (|signed cos^2| > 0.99999, computed by the camera itself when this returns -1).
    virtual int CameraAlignedOverride() { return -1; }
    // 0x0040C790 increments the rail tick if 0x009CA098 == 1 (op 0x37, handled here) or this returns true:
    // 0x00413280() (non-interactive mode) || (0x009C8E8C == 0 && 0x009C8E80 != 0).
    virtual bool RailAdvance() { return true; }
    // op 0x32: both players either have a type entry 0x00579CE0[state*5] != 0 or lives > 0.
    virtual bool PlayersReady() { return true; }

    // ---- effects ----
    // One spawn record (already relocated pointer) for ops 0x09 / 0x0A / 0x0B / 0x0C (the handler that runs:
    // route and per-player variants are resolved to these four; 0x0D reports as 0x0B).
    virtual void Spawn(uint32_t kind, uint32_t record) {}
    virtual void Sound(uint32_t id) {}                                   // Snd_PlayGameSound 0x0041CFD0
    virtual void SoundRepeat(uint32_t id, int count, int interval) {}    // 0x00435EE0 (first play included)
    virtual void Voice(int id) {}                                        // 0x00435B80
    virtual void Bgm(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {}  // op 0x5F (0x0041D450)
    virtual void Resource(uint32_t opcode, uint32_t arg) {}              // 0x50..0x5A (pp lists as 0x52/0x53)
    virtual void Section(uint32_t opcode, int newSection, int otherSection) {}  // 0x28 / 0x29
    virtual void Env(const EventOp& op) {}           // every env op 0x17..0x27 (game::Environment applies 0x18..0x27 first)
    virtual void PlayerBonus() {}                    // op 0x2B
    virtual void StageEnd() {}                       // 0x0045ED90 (from op 0x4F at the end of a mode's scene list)
    virtual void TargetsReset() {}                   // 0x00403710 (gameplay camera command): 0x009A2BA0 / 0x009A2BA4 = 0
    // Prepared English PC reference typing scripts. Operands remain in their authored units;
    // the controller applies its word-bank/rank rules synchronously at the op.
    virtual void TypingWords(int group, int selector) {}
    virtual void TypingWordLimits(int maximum, int minimum) {}
    virtual void TypingProps(bool enabled) {}
    virtual void TypingSceneClear() { TypingProps(false); }
    virtual int TypingPlayers() { return 0; }
    virtual int TypingModeId() { return 0; }
    virtual bool TypingChallenge(const EventOp& op) { return false; }
    virtual void TypingCountdown(int instruction, int frames) {}
    virtual int TypingChallengeStatus() { return 0; }
    virtual void TypingClearActorSlots() {}
    virtual void TypingSound(uint32_t id) { Sound(id); }
    virtual void TypingSoundRepeat(uint32_t id, int count, int interval) { SoundRepeat(id, count, interval); }
    virtual void TypingVoice(int id) { Voice(id); }
    // A Cam_Evaluate 0x004041E0 call when no CameraBank is attached (CameraDirector::SetCameraBank); return true
    // and fill pos/target/roll to give the camera real positions.
    virtual bool EvaluateCamera(int camId, float t, float pos[3], float target[3], int* roll) { return false; }
    // 0x004033B0 / 0x00403510: training lesson 3 (0x009CA08C == 2, 0x009C8FB1 == 3) plays the difficulty's camera
    // 0x004C4384[0x009C9118] (single poses only for camera 0x1A2); default: the segment's own camera
    virtual int SegmentCamera(int camId, bool singlePose) { return camId; }
    virtual void Error(const std::string& what) {}
};

// Camera command system (camera task 0x004022B0, queue processor 0x00402320, gameplay camera 0x00402650,
// rail step 0x0040C370). Only context 0 receives commands; context 2 mirrors it in event mode (mode 1 sub 3).
class CameraDirector {
public:
    enum class Cmd {
        Nop,          // 0x0041EBB0
        Seg,          // 0x00403360 type 0x40 dispatcher
        PlayStep,     // 0x004035E0 Cam_PlayStep
        Preset,       // 0x004032E0 type 0x20
        WaitSetup,    // 0x00403250 type 0x13
        WaitCount,    // 0x00403290
        Forever,      // 0x00420810 (empty: never completes)
        Gameplay,     // 0x00403710 type 0x21
        GameStart4,   // 0x00402460
        GameStart6,   // 0x00402580
        GameStart7,   // 0x00402DB0
        GameStep,     // 0x00402650
        GameRail,     // 0x00402E00
        SetPlayerBit, // 0x00403830 type 0x10
        SetSub,       // 0x004036B0 type 0x11
        SetPlayerFn,  // 0x00403780 type 0x12
        SetActiveCtx, // 0x004037E0 type 0x14
        SmoothFollow, // 0x00403930 type 0x15
        LookTable,    // 0x004038A0 type 0x60
        Invalid,      // null table entry / unknown type (original jumps to garbage)
    };
    enum class Rail {  // [0x009C7080], set by the sub-mode setup functions 0x00576C14[mode*9+sub]
        Nop,          // 0x0041EBB0
        Event3,       // 0x0040C380 (mode 1 sub 3: player placed 15 behind the camera)
        Mode1Sub1,    // 0x0040C9C0 (player/gun bookkeeping, no camera effect traced)
        StandInit, Stand,      // 0x0040C430 / 0x0040C470 (mode 2 sub 4)
        LookInit, Look,        // 0x0040C4C0 / 0x0040C5C0 (mode 2 sub 5, not decoded)
        Move6Init, Move6,      // 0x0040C770 / 0x0040C790 (mode 2 sub 6: rail tick < end)
        Move7Init, Move7,      // 0x0040C8A0 / 0x0040C8B0 (mode 2 sub 7: rail tick <= end)
    };

    CameraDirector(const EventMemory& mem, EventHost& host, const ExeImage* exe) : mem_(mem), host_(host), exe_(exe) {}

    // Camera paths for Cam_Evaluate (without one, positions come from EventHost::EvaluateCamera).
    void SetCameraBank(const CameraBank* bank) { bank_ = bank; }

    void Init();                          // 0x00402260 (camera task start)
    void QueueBlock(uint32_t blockAddr);  // op 0x30 body (not skipping)
    void StartEvent(int sub);             // op 0x31 body
    void StartEventReady(int sub);        // op 0x32 success path
    void ResetForStage();                 // camera part of op 0x4D
    void Frame();                         // camera task body 0x004022B0 (incl. Cam_BuildViews)
    void RailStep();                      // 0x0040C370

    // 0x00403BB0 (request) / 0x00403BD0 (request + current)
    void RequestMode(int mode, int sub);
    void SetMode(int mode, int sub);

    // Rendered view of the frame (Cam_BuildViews loads it as the matrix-stack base), row vectors.
    const float* ActiveView() const { return ctx[activeCtx & 3].view; }

    // ---- state (original addresses) ----
    int modeCur = 0, subCur = 0, modeReq = 0, subReq = 0;  // 0x009C6F08 / 0x009C6F10 / 0x009C6F0C / 0x009C6F14
    int activeCtx = 0;                                     // 0x009C6F00
    int ctx2Mode = 0;                                      // 0x009C6F1C (0: context 2 mirrors context 0 in event mode)
    CameraContext ctx[4];                                  // 0x009A6000 + i * 0x1A4 (pos, angles, target, matrices)
    uint32_t ring[16] = {};                                // 0x009C9060
    int readIdx = 0, writeIdx = 0;                         // 0x009A34D0 / 0x009CA108
    int pending = 0;                                       // 0x009A2C8C
    int cmdState = 2;                                      // 0x009A1A10: 0 running, 1 done (next now), 2 idle (next frame)
    Cmd cmd = Cmd::Nop;                                    // ctx0 +0x10C
    int32_t args[8] = {};                                  // ctx0 +0x184..+0x1A0 (block words after the type)
    int camId = 0;                                         // 0x009A2D78
    int tick = 0;                                          // 0x009A6110 (ctx0 +0x110)
    int playTick = 0, endTick = 0;                         // ctx0 +0x144 / +0x148
    uint32_t segFlags = 0;                                 // ctx0 +0x150
    int ticksRemaining = 0x7FFFFFFF;                       // 0x009C6F28
    // deferred segment / rail
    int railTick = 0, railEnd = 0, railStart = 0;          // 0x009C70AC / 0x009C70B0 / 0x009C709C
    uint32_t railFlags = 0;                                // 0x009C70B8
    float railTickF = 0;                                   // 0x009C70BC
    float railPos[3] = {};                                 // 0x009C70C0
    int railPitch = 0, railYaw = 0, railRoll = 0;          // 0x009C70CC / 0x009C70D0 / 0x009C70D4
    float railTarget[3] = {};                              // 0x009C70D8 (rail look point)
    Rail rail = Rail::Nop;
    uint32_t setupFn = 0;                                  // last sub-mode setup function (0x00576C14[mode * 9 + sub])
    uint32_t playerFn = 0, playerFnCount = 0;              // camera type 0x12 (0x00403780): players' +0x80 = 0x00579E90[arg]
    uint32_t setupCount = 0;                               // number of setup calls (the setups also set the players'
                                                           // callbacks 0x009A5CDC / 0x009A5CE0: game::World applies them)
    int railForce = 0;                                     // 0x009CA098 (op 0x37)
    float playerPos[3] = {};                               // 0x009C71E0 (players' position, set by the rail step)
    int playerYaw = 0;                                     // 0x009C71F0 (set by the gameplay camera start 0x00402460)
    int playerPitch = 0, playerRoll = 0;                   // 0x009C71EC / 0x009C71F4 (rail 0x0040C380)
    int eyeMode = 0;                                       // 0x009C70F4 (op 0x36; 1: player y = eyeHeight)
    float eyeHeight = 0;                                   // 0x009C8E58 (op 0x1A)
    // shooting camera
    int state = 0;                                         // 0x009C6F20 (2 back to rail, 3 tracking, 6 hold, 7 move)
    bool returning = false, settled = false;               // 0x009C6F2C / 0x009C6F2D
    int style = 0;                                         // 0x009C6F2E (s8)
    bool aimedOnce = false;                                // 0x009C6F2F (cleared every frame)
    bool moveTo = false, moveTo2 = false, moveTo3 = false;  // 0x009C6F30 / 0x009C6F31 / 0x009C6F32
    bool smoothFollow = false;                             // 0x009C6F33
    bool firstStart = true;                                // 0x009C6F3C
    bool forceState6 = false;                              // 0x009CA094 (written by other subsystems)
    int lookTableOn = 0;                                   // 0x009C6FD8
    int32_t lookTable[6] = {};                             // type 0x60 args: (tick, camera) x 3 at 0x009C6FE0/DC + 8i
    int routeBranch = 0;                                   // 0x009C88A4
    int rollEnable = 0;                                    // 0x009A21B0 (op 0x35)
    uint32_t playerFlags = 0;                              // bit 0 of 0x009A5EBC / 0x009A5D8C
    uint32_t playerFlagsWrites = 0;                        // count of the writes above (each one sets both players' bit)
    float lookAt[3] = {};                                  // 0x009C6FA8 (target look-at / rail look point)
    float evalPos[3] = {};                                 // 0x009C6F90 (return-to-rail evaluation)
    int turnStep = 0x200;                                  // 0x009C6F36 (s16): 0x00403C00 turns 1/(1+step) per frame
    int stepTable = 1;                                     // 0x009C6F38 (s16): table index for 0x00403B00
    bool targetSeen = true;                                // 0x0059C988 (1 each frame; 0 when 0x00403050 finds no slot)
    int moveStage = 0;                                     // 0x009C6F18 (state 7)
    float moveGoal[3] = {};                                // 0x009C6FC0 (state 7 goal, set by other code)
    float moveVel[3] = {};                                 // 0x009CA940
    LockOnSlot slots[4];                                   // inputs of the current frame (EventHost::LockOnSlots)
    double lastCos2 = 0;                                   // last alignment value (diagnostics)
    bool error = false;

    // Cam_Evaluate calls of the current frame (cleared by EventRunner::Frame), in call order.
    std::vector<std::pair<int, float>> evals;

private:
    void ProcessQueue();       // 0x00402320
    void Call();               // ctx0 command function
    void Complete();           // cmdState = 1, pending -= 1
    void Dispatch(int32_t type);
    void Evaluate(int id, float t, float* p, float* tg, int* r);
    void SubmodeSetup(int mode, int sub);  // 0x00576C14 table
    void GameInit();           // 0x004031E0
    void RailLookPoint();      // 0x00402460 / 0x00402580: rail look point = camera frame point (0, 0, -30)
    void GameStep();           // 0x00402650
    void ReturnToRail();       // 0x00402720 -> 0x00402740 / 0x00402860
    void RailApproach();       // 0x00402E60
    void RailAim();            // 0x00402F80
    void TrackTargets();       // 0x00402890
    void MoveTo();             // 0x00402A40
    void LookTableEval();      // 0x00403DB0
    bool AnySlot() const;
    bool Aligned();            // 0x00401DF0 test (or the host's oracle override)

    const EventMemory& mem_;
    EventHost& host_;
    const ExeImage* exe_;
    const CameraBank* bank_ = nullptr;
};

class EventRunner {
public:
    struct Config {
        int gameMode = 5;      // 0x009C8E98 (5 = attract demo)
        int demoIndex = 0;     // 0x009A3400
        int gameType = 0;      // 0x009CA08C
        int route = 0;         // 0x009C8E84 (ops 0x01-0x08)
        int players = 0;       // 0x009C8E80 (op 0x12)
        int playerVariant = 0; // 0x009C9118 (ops 0x49-0x4B)
    };

    EventRunner(const EventMemory& mem, EventHost& host, Environment* env, const ExeImage* exe)
        : mem_(mem), host_(host), env_(env), exe_(exe), camera(mem, host, exe) {}

    // Stage start: camera task init 0x00402260 and Evt_Init 0x0045EBC0 for comevtbl[mode][scene][variant].
    void Start(int mode, int scene, const Config& cfg);
    // One game frame: RunEvents(), CameraTask(), RailStep() (clears camera.evals first).
    void Frame();
    void RunEvents();                          // Evt_Run 0x0045ECC0
    void CameraTask() { camera.Frame(); }      // camera task 0x004022B0
    void RailStep() { camera.RailStep(); }     // task 0x00414F10 -> 0x0040C370

    struct Exec { int frame; uint32_t opcode, addr; };
    std::vector<Exec> trace;  // every handler call, like the capture's E lines
    int frame = 0;

    // VM state (original addresses)
    uint32_t script = 0;        // 0x009C7108
    bool yield = false;         // 0x009C8EA0
    int waitCounter = -1;       // 0x009C8904
    int enemyWaitFrames = 0;    // 0x007DCCA8
    bool running = true;        // 0x007DCCA4
    bool skipping = false;      // 0x009A2D74 (cutscene skip in progress)
    bool skippable = false;     // 0x009A2D7C
    int mode = 0, scene = 0, variant = 0;  // 0x009A1A08 / 0x009A2BC0 / 0x009A2BB0
    struct TypingCheckpoint { uint32_t script=0; int mode=0,scene=0,variant=0,routeBranch=0; } typingCheckpoint;
    int section = 0, prevSection = 0;      // 0x009A2224 / 0x009C7004
    uint8_t flags[256] = {};    // 0x009C7200
    std::map<uint32_t, uint32_t> globals;
    bool halted = false, error = false;
    std::string errorText;
    Config cfg;
    CameraDirector camera;

private:
    void Exec(uint32_t op);
    uint32_t W(int i) const;  // word i of the current instruction
    void SkipList();          // script -> after the -1 terminator
    void SpawnList(uint32_t kind, bool onlyIfNotSkipping);
    void PerPlayer(uint32_t op);
    void NextVariant();
    void Fail(const std::string& what);

    const EventMemory& mem_;
    EventHost& host_;
    Environment* env_;
    const ExeImage* exe_;
};

}  // namespace game
