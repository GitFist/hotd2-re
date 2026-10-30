#include "game/event_runner.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/camera.h"
#include "game/mstack.h"

namespace game {

namespace {

float AsFloat(uint32_t w) {
    float f;
    memcpy(&f, &w, 4);
    return f;
}

int Ftol(float f) { return static_cast<int>(f); }  // __ftol truncates

constexpr float kAlign = 0.99999f;  // _DAT_004C439C

}  // namespace

// ============================================================================================================
// CameraDirector
// ============================================================================================================

// 0x00402260 (after 0x004021D0): contexts reset, BD0(0,0), BD0(1,1), queue empty, command state idle (2).
void CameraDirector::Init() {
    activeCtx = 0;  // 0x004021D0
    ctx2Mode = 0;
    SetMode(0, 0);
    settled = returning = false;
    lookTableOn = 0;
    SetMode(1, 1);
    readIdx = writeIdx = 0;
    pending = 0;
    cmdState = 2;
    cmd = Cmd::Nop;
}

// 0x00403BB0: requested mode/sub (committed by Cam_BuildViews at the end of the camera task) + setup function.
void CameraDirector::RequestMode(int mode, int sub) {
    modeReq = mode;
    subReq = sub;
    SubmodeSetup(mode, sub);
}

// 0x00403BD0: like 0x00403BB0 but also sets the current mode/sub.
void CameraDirector::SetMode(int mode, int sub) {
    modeReq = modeCur = mode;
    subReq = subCur = sub;
    SubmodeSetup(mode, sub);
}

// Table 0x00576C14[mode * 9 + sub]; unlisted entries are 0x00402710 (an endless loop in the original).
void CameraDirector::SubmodeSetup(int mode, int sub) {
    const int k = mode * 9 + sub;
    if (exe_ && k >= 0 && k < 27) setupFn = exe_->U32(0x00576C14 + 4 * static_cast<uint32_t>(k));
    ++setupCount;
    switch (k) {
    case 0 * 9 + 0: rail = Rail::Nop; break;        // 0x00403970
    case 1 * 9 + 1: rail = Rail::Mode1Sub1; break;  // 0x00403980 (+ player draw functions)
    case 1 * 9 + 2: rail = Rail::Nop; break;        // 0x004039A0
    case 1 * 9 + 3:                                 // 0x004039D0
        activeCtx = 2;
        rail = Rail::Event3;
        break;
    case 2 * 9 + 4: rail = Rail::StandInit; break;  // 0x00403A00
    case 2 * 9 + 5: rail = Rail::LookInit; break;   // 0x00403A30
    case 2 * 9 + 6: rail = Rail::Move6Init; break;  // 0x00403A60
    case 2 * 9 + 7: rail = Rail::Move7Init; break;  // 0x00403A90
    default:
        error = true;
        host_.Error("camera sub-mode setup " + std::to_string(mode) + "/" + std::to_string(sub) + " is 0x00402710 (hang)");
        break;
    }
}

// Cam_Evaluate 0x004041E0 (outputs untouched when the camera id is unknown).
void CameraDirector::Evaluate(int id, float t, float* p, float* tg, int* r) {
    evals.emplace_back(id, t);
    float pp[3], tt[3];
    int rr = 0;
    bool ok = false;
    if (bank_) ok = bank_->Evaluate(id, t, pp, tt, &rr, rollEnable != 0);
    else if (host_.EvaluateCamera(id, t, pp, tt, &rr)) { ok = true; if (!rollEnable) rr = 0; }
    if (ok) {
        for (int k = 0; k < 3; ++k) { p[k] = pp[k]; tg[k] = tt[k]; }
        if (r) *r = rr;
    }
}

// op 0x30: 0x0045F7F0 (non-skipping path).
void CameraDirector::QueueBlock(uint32_t blockAddr) {
    ring[writeIdx] = blockAddr;
    writeIdx = (writeIdx + 1) & 15;
    ++pending;
}

// op 0x31: 0x0045F870.
void CameraDirector::StartEvent(int sub) {
    RequestMode(1, sub);
    modeCur = 1;
    subCur = sub;
    state = 0;
    lookTableOn = 0;
    smoothFollow = false;
    playerFlags &= ~1u;  // (a write: both players' bit 0)
    ++playerFlagsWrites;
    cmd = Cmd::Nop;
    cmdState = 1;
    --pending;
}

// op 0x32 success path: 0x0045F900.
void CameraDirector::StartEventReady(int sub) {
    RequestMode(1, sub);
    modeCur = 1;
    subCur = sub;
    playerFlags &= ~1u;  // (a write: both players' bit 0)
    ++playerFlagsWrites;
    --pending;
    state = 0;
    cmd = Cmd::Nop;
    cmdState = 1;
}

// Camera part of op 0x4D (0x0045EEC0).
void CameraDirector::ResetForStage() {
    tick = 0;
    RequestMode(1, 3);
    playerFlags &= ~1u;  // (a write: both players' bit 0)
    ++playerFlagsWrites;
    ticksRemaining = 0x7FFFFFFF;
    stepTable = 1;  // 0x009C6F38 (0x00403BA0)
    lookTableOn = 0;
    firstStart = true;
    smoothFollow = false;
    forceState6 = false;
    rollEnable = 0;
}

void CameraDirector::Complete() {
    cmdState = 1;
    --pending;
}

// Block type -> command function: 0x005776EC[type >> 4][type & 15]. The sub-tables overlap
// (0x005776C4 / 0x005776DC / 0x005776E4 / 0x005776E8), so some types alias others.
void CameraDirector::Dispatch(int32_t type) {
    switch (type) {
    case 0x10: cmd = Cmd::SetPlayerBit; break;  // 0x00403830
    case 0x11: cmd = Cmd::SetSub; break;        // 0x004036B0
    case 0x12: cmd = Cmd::SetPlayerFn; break;   // 0x00403780
    case 0x13: cmd = Cmd::WaitSetup; break;     // 0x00403250
    case 0x14: cmd = Cmd::SetActiveCtx; break;  // 0x004037E0
    case 0x15: cmd = Cmd::SmoothFollow; break;  // 0x00403930
    case 0x16: case 0x20: cmd = Cmd::Preset; break;    // 0x004032E0
    case 0x17: case 0x21: cmd = Cmd::Gameplay; break;  // 0x00403710
    case 0x18: case 0x22: case 0x40: cmd = Cmd::Seg; break;  // 0x00403360
    case 0x19: case 0x23: case 0x41: case 0x60: cmd = Cmd::LookTable; break;  // 0x004038A0
    default:
        cmd = Cmd::Invalid;
        error = true;
        host_.Error("camera block type " + std::to_string(type) + " has no handler");
        break;
    }
}

// 0x00402320: call the running command, then start queued blocks while the command state allows.
void CameraDirector::ProcessQueue() {
    for (;;) {
        Call();
        if (readIdx == writeIdx) {
            if (cmdState >= 1 && cmdState <= 2) cmd = Cmd::Nop;
            return;
        }
        bool again = false;
        if (cmdState >= 1 && cmdState <= 2) {
            const uint32_t block = ring[readIdx];
            readIdx = (readIdx + 1) & 15;
            uint32_t type = 0;
            mem_.Read(block + 4, &type);
            Dispatch(static_cast<int32_t>(type));
            for (int i = 0; i < 8; ++i) {  // always 8 words (0x9A6184..0x9A61A3), even past a short block
                uint32_t w = 0;
                mem_.Read(block + 8 + 4 * i, &w);
                args[i] = static_cast<int32_t>(w);
            }
            again = cmdState == 1;
        }
        cmdState = 0;
        if (!again) return;
    }
}

void CameraDirector::Call() {
    CameraContext& c = ctx[0];
    switch (cmd) {
    case Cmd::Nop:
    case Cmd::Forever:
    case Cmd::Invalid:
        return;
    case Cmd::SetPlayerBit:  // 0x00403830: bit 0 of both players' flag words = arg
        playerFlags = (playerFlags & ~1u) | (static_cast<uint32_t>(args[0]) & 1u);
        ++playerFlagsWrites;
        Complete();
        return;
    case Cmd::SetSub:  // 0x004036B0
        SetMode(modeReq, args[0]);
        Complete();
        return;
    case Cmd::SetPlayerFn:  // 0x00403780: player functions 0x009A5CE0 / 0x009A5E10 = 0x00579E90[arg]
        if (exe_) playerFn = exe_->U32(0x00579E90 + 4 * static_cast<uint32_t>(args[0]));
        ++playerFnCount;
        Complete();
        return;
    case Cmd::WaitSetup:  // 0x00403250
        cmd = args[0] != 0 ? Cmd::WaitCount : Cmd::Forever;
        return;
    case Cmd::WaitCount:  // 0x00403290
        if (--args[0] == 0) Complete();
        return;
    case Cmd::SetActiveCtx:  // 0x004037E0
        activeCtx = args[0];
        Complete();
        return;
    case Cmd::SmoothFollow:  // 0x00403930
        smoothFollow = true;
        Complete();
        return;
    case Cmd::Preset:  // 0x004032E0: pos + pitch/yaw/roll = 24-byte pose 0x00576CF0[args[1]]; args[0] frames (0 = forever)
        settled = true;
        if (exe_) {
            const uint32_t p = 0x00576CF0 + args[1] * 0x18;
            for (int k = 0; k < 3; ++k) c.pos[k] = AsFloat(exe_->U32(p + 4 * k));
            c.pitch = static_cast<int32_t>(exe_->U32(p + 12));
            c.yaw = static_cast<int32_t>(exe_->U32(p + 16));
            c.roll = static_cast<int32_t>(exe_->U32(p + 20));
        }
        if (args[0] != 0 && --args[0] == 0) Complete();
        return;
    case Cmd::Gameplay: {  // 0x00403710 (type 0x21 = 33): enter shooting mode 2 / sub args[0]
        host_.TargetsReset();  // targets taken 0x009A2BA0 / 0x009A2BA4 = 0
        state = 3;
        RequestMode(2, args[0]);
        // 0x00576B20[sub]: 4 -> 0x00402460, 6 -> 0x00402580, 7 -> 0x00402DB0, others null
        switch (subReq) {
        case 4: cmd = Cmd::GameStart4; break;
        case 6: cmd = Cmd::GameStart6; break;
        case 7: cmd = Cmd::GameStart7; break;
        default:
            cmd = Cmd::Invalid;
            error = true;
            host_.Error("gameplay camera sub " + std::to_string(subReq) + " has no start function");
            break;
        }
        cmdState = 0;
        return;
    }
    case Cmd::Seg:  // 0x00403360
        if (args[0] == args[1]) {  // 0x004033B0: single pose
            camId = host_.SegmentCamera(args[2], true);
            tick = args[0];
            int r = 0;
            Evaluate(camId, static_cast<float>(args[0]), c.pos, c.target, &r);
            ContextLookAngles(c, c.target, r);
            Complete();
        } else if (args[3] & 2) {  // 0x00403490: defer (rail segment for the gameplay camera)
            camId = args[2];
            railTick = args[0] == -1 ? tick + 1 : args[0];
            railEnd = args[1];
            railFlags = static_cast<uint32_t>(args[3]);
            Complete();
        } else {  // 0x00403510: start playing
            if (args[3] & 4) {
                tick = args[0];
                playTick = railTick;
                endTick = railEnd;
            } else {
                camId = host_.SegmentCamera(args[2], false);
                if (args[0] == -1) {
                    playTick = tick;
                } else {
                    tick = args[0];
                    playTick = args[0];
                }
                endTick = args[1];
            }
            segFlags = static_cast<uint32_t>(args[3]);
            cmd = Cmd::PlayStep;
            Call();
        }
        return;
    case Cmd::PlayStep: {  // 0x004035E0 Cam_PlayStep
        tick = playTick;
        int r = 0;
        Evaluate(camId, static_cast<float>(playTick), c.pos, c.target, &r);
        ContextLookAngles(c, c.target, r);
        ticksRemaining = endTick - playTick;
        if (endTick <= playTick) {
            ++playTick;
            Complete();
            return;
        }
        ++playTick;
        return;
    }
    case Cmd::LookTable:  // 0x004038A0
        lookTableOn = 1;
        for (int i = 0; i < 6; ++i) lookTable[i] = args[i];
        Complete();
        return;
    case Cmd::GameStart4:  // 0x00402460 (also places the players 15 below context 0's camera: 0x009C71E0..)
    case Cmd::GameStart6:  // 0x00402580
        if (firstStart) {
            GameInit();
            if (cmd == Cmd::GameStart4) {
                playerPos[0] = ctx[0].pos[0];
                playerPos[1] = ctx[0].pos[1] - 15.0f;  // 0x004C4398
                playerPos[2] = ctx[0].pos[2];
                playerYaw = (ctx[0].yaw - 0x8000) & 0xFFFF;  // 0x009C71F0
                playerPitch = playerRoll = 0;                 // 0x009C71EC / 0x009C71F4
            }
            RailLookPoint();
        }
        firstStart = true;
        cmd = Cmd::GameStep;
        GameStep();
        return;
    case Cmd::GameStart7:  // 0x00402DB0
        if (firstStart) {
            GameInit();
            settled = true;
            state = 0;
        }
        firstStart = true;
        cmd = Cmd::GameRail;
        Call();
        return;
    case Cmd::GameRail:  // 0x00402E00: the view is the rail pose; settled unless a lock-on slot is active
        host_.LockOnSlots(slots);
        settled = !AnySlot();
        for (int k = 0; k < 3; ++k) { c.pos[k] = railPos[k]; c.target[k] = railTarget[k]; }
        c.pitch = railPitch;
        c.yaw = railYaw;
        c.roll = railRoll;
        tick = Ftol(railTickF);
        return;
    case Cmd::GameStep:
        GameStep();
        return;
    }
}

// 0x004031E0
void CameraDirector::GameInit() {
    playerFlags &= ~1u;  // (a write: both players' bit 0)
    ++playerFlagsWrites;
    railTickF = static_cast<float>(tick);
    activeCtx = 0;
    settled = returning = false;
    moveTo = moveTo2 = moveTo3 = false;
    style = 0;
    ticksRemaining = 0x7FFFFFFF;
    turnStep = 0x200;
}

// 0x00402460 / 0x00402580 (after 0x004031E0): rail look point 0x009C70D8 = (0, 0, -30) in the camera frame of
// context 0: Mtx I, T(pos), Ry(yaw), Rx(pitch).
void CameraDirector::RailLookPoint() {
    const CameraContext& c = ctx[0];
    MtxStack s;
    s.LoadIdentity();
    s.Translate(c.pos[0], c.pos[1], c.pos[2]);
    s.RotateY(c.yaw);
    s.RotateX(c.pitch);
    const float p[3] = { 0.0f, 0.0f, -30.0f };
    s.TransformPoint(p, railTarget);
}

bool CameraDirector::AnySlot() const {
    for (const LockOnSlot& s : slots)
        if (s.active) return true;
    return false;
}

// |0x00401DF0(look-at - pos, target - pos)| > 0.99999 (or the host's oracle override).
bool CameraDirector::Aligned() {
    const CameraContext& c = ctx[0];
    const float a[3] = { lookAt[0] - c.pos[0], lookAt[1] - c.pos[1], lookAt[2] - c.pos[2] };
    const float b[3] = { c.target[0] - c.pos[0], c.target[1] - c.pos[1], c.target[2] - c.pos[2] };
    lastCos2 = SignedCos2(a, b);
    const int o = host_.CameraAlignedOverride();
    if (o >= 0) return o != 0;
    return std::fabs(lastCos2) > static_cast<double>(kAlign);
}

// 0x00402650
void CameraDirector::GameStep() {
    host_.LockOnSlots(slots);
    const bool target = AnySlot();
    if (style >= 0 && style <= 2) {
        const int count = style == 2 ? host_.Count7006() : host_.LiveEnemies();
        state = (count == 0 && !target) ? 2 : 3;
    }
    if (forceState6) state = 6;
    if (state != 2) settled = returning = false;
    if (moveTo || moveTo3) state = 7;
    switch (state) {  // 0x00576CBC
    case 2: ReturnToRail(); break;
    case 3: TrackTargets(); break;
    case 6: break;  // 0x00420810 (empty)
    case 7: MoveTo(); break;
    default:
        error = true;
        host_.Error("gameplay camera state " + std::to_string(state) + " is 0x00402710 (hang)");
        break;
    }
    tick = Ftol(railTickF);
}

// 0x00402E60: evaluate the rail camera at the current tick; position 1/16 of the way there.
void CameraDirector::RailApproach() {
    CameraContext& c = ctx[0];
    float t[3] = {};
    int r = 0;
    Evaluate(camId, static_cast<float>(tick), evalPos, t, &r);
    for (int k = 0; k < 3; ++k) c.pos[k] = Lerp(c.pos[k], evalPos[k], 1, 15);
}

// 0x00402F80: evaluate again for the look-at 0x009C6FA8 (the rail target) and turn the view toward it.
void CameraDirector::RailAim() {
    CameraContext& c = ctx[0];
    float p[3] = {};
    int r = 0;
    Evaluate(camId, static_cast<float>(tick), p, lookAt, &r);
    float out[3];
    TurnToward(c.pos, lookAt, c.target, out, 1, TurnStepRail(exe_));
    for (int k = 0; k < 3; ++k) c.target[k] = out[k];
}

// 0x00402720 -> 0x00576CE0[style]: 0x00402740 (styles 0 and 2) or 0x00402860 (style 1).
void CameraDirector::ReturnToRail() {
    CameraContext& c = ctx[0];
    if (style == 1) {  // 0x00402860
        returning = false;
        settled = true;
        RailApproach();
        RailAim();
        ContextLookAngles(c, c.target, 0);
        return;
    }
    if (style != 0 && style != 2) {
        error = true;
        host_.Error("return-to-rail style " + std::to_string(style) + " has no function");
        return;
    }
    const int b = (settled ? 2 : 0) | (returning ? 1 : 0);
    if (b >= 2) {  // back on the rail: follow it exactly
        int r = 0;
        Evaluate(camId, static_cast<float>(tick), c.pos, c.target, &r);
        ContextLookAngles(c, c.target, 0);
        return;
    }
    if (b == 0) returning = true;
    if (lookTableOn) LookTableEval();
    RailApproach();
    RailAim();
    if (Aligned()) {
        returning = false;
        settled = true;
        aimedOnce = true;
    }
    ContextLookAngles(c, c.target, 0);
}

// 0x00402890: follow the rail position (0x00402EF0, 1/16 smoothing when 0x009C6F33), aim at the lock-on targets
// (0x00403050) by turning the view (0x00403C00). No Cam_Evaluate unless the look table is on and the ticks ran out.
void CameraDirector::TrackTargets() {
    CameraContext& c = ctx[0];
    if (ticksRemaining < 0 && lookTableOn) LookTableEval();
    for (int k = 0; k < 3; ++k) c.pos[k] = smoothFollow ? Lerp(c.pos[k], railPos[k], 1, 15) : railPos[k];
    targetSeen = SelectLookAt(slots, railTarget, lookAt);
    if (getenv("WT_DBG")) fprintf(stderr, "cam track %d %d %d %d look %.4f %.4f %.4f s0 %.3f %.3f %.3f s2 %.3f %.3f %.3f\n", slots[0].active, slots[1].active, slots[2].active, slots[3].active, lookAt[0], lookAt[1], lookAt[2], slots[0].point[0], slots[0].point[1], slots[0].point[2], slots[2].point[0], slots[2].point[1], slots[2].point[2]);
    float out[3];
    TurnToward(c.pos, lookAt, c.target, out, 1, turnStep);
    for (int k = 0; k < 3; ++k) c.target[k] = out[k];
    ContextLookAngles(c, c.target, c.roll);
    const bool aligned = Aligned();
    if (!targetSeen) {
        if (aligned) aimedOnce = true;
        turnStep = TurnStepRail(exe_);
    } else if (host_.LiveEnemies() == 0 && style == 2) {
        turnStep = 0;
    } else {
        turnStep = TurnStepFromAngle(exe_, stepTable, c.pos, c.target, lookAt);
    }
}

// 0x00402A40: scripted move to 0x009C6FC0 at 0.8 per frame (until within 10), look there, then back to the rail
// position once 0x009C6F31 is set (snaps within 8). Not exercised by the stage-1 demo.
void CameraDirector::MoveTo() {
    CameraContext& c = ctx[0];
    auto towards = [&](const float to[3]) {  // 0x004AAA00 Vec_Normalize, * 0.8 (0x004C43A8)
        const float d[3] = { to[0] - c.pos[0], to[1] - c.pos[1], to[2] - c.pos[2] };
        const float len = std::sqrt((d[0] * d[0] + d[1] * d[1]) + d[2] * d[2]);
        for (int k = 0; k < 3; ++k) moveVel[k] = (d[k] / len) * 0.8f;
    };
    auto dist = [&](const float to[3]) {
        const float dx = to[0] - c.pos[0], dy = to[1] - c.pos[1], dz = to[2] - c.pos[2];
        return std::sqrt((dx * dx + dy * dy) + dz * dz);
    };
    if (moveTo) {
        moveTo = false;
        moveStage = 0;
    }
    switch (moveStage) {
    case 0:
        towards(moveGoal);
        moveTo3 = true;
        ++moveStage;
        [[fallthrough]];
    case 1:
        for (int k = 0; k < 3; ++k) c.pos[k] += moveVel[k];
        for (int k = 0; k < 3; ++k) c.target[k] = Lerp(c.target[k], moveGoal[k], 1, 15);
        if (dist(moveGoal) < 10.0f) ++moveStage;  // 0x004C43A4
        break;
    case 2:
        for (int k = 0; k < 3; ++k) c.target[k] = Lerp(c.target[k], moveGoal[k], 1, 15);
        if (moveTo2) {
            towards(railPos);
            ++moveStage;
        }
        break;
    case 3:
        if (dist(railPos) <= 8.0f) {  // 0x004C43A0
            for (int k = 0; k < 3; ++k) c.pos[k] = railPos[k];
            moveTo2 = moveTo = moveTo3 = false;
        } else {
            for (int k = 0; k < 3; ++k) c.pos[k] += moveVel[k];
        }
        break;
    default:
        break;
    }
    ContextLookAngles(c, c.target, 0);
}

// 0x00403DB0: look table entry routeBranch = (tick, camera) evaluated into the rail pose.
void CameraDirector::LookTableEval() {
    const int i = routeBranch >= 0 && routeBranch < 3 ? routeBranch : 0;
    railTickF = static_cast<float>(lookTable[2 * i]);
    camId = lookTable[2 * i + 1];
    tick = Ftol(railTickF);
    int r = 0;
    Evaluate(camId, static_cast<float>(tick), railPos, railTarget, &r);
}

// Camera task 0x004022B0: mode function 0x00576B10[mode], contexts 1-3 (always idle here), Cam_BuildViews.
void CameraDirector::Frame() {
    aimedOnce = false;  // 0x009C6F2F
    targetSeen = true;  // 0x0059C988
    if (modeCur == 1) {  // 0x004023D0
        ProcessQueue();
        if (subCur == 3 && ctx2Mode == 0) {  // 0x004023F0: context 2 mirrors context 0
            const CameraContext& a = ctx[0];
            CameraContext& b = ctx[2];
            for (int k = 0; k < 3; ++k) { b.pos[k] = a.pos[k]; b.target[k] = a.target[k]; }
            b.pitch = a.pitch; b.yaw = a.yaw; b.roll = a.roll;
            ContextLookAngles(b, b.target, a.roll);
        } else if (subCur == 3 && ctx2Mode == 5) {
            ContextLookAngles(ctx[2], ctx[2].target, ctx[2].roll);
        }
    } else if (modeCur == 2) {  // 0x004023E0
        ProcessQueue();
    }
    BuildContextViews(ctx, activeCtx & 3, host_.ShakeY());  // Cam_BuildViews 0x00401F40
    modeCur = modeReq;  // 0x00402177..0x0040218D
    subCur = subReq;
}

// 0x0040C370: jmp [0x009C7080] (task 0x00414F10, runs after the camera task)
void CameraDirector::RailStep() {
    // players' position from the rail position (0x0040C470 / 0x0040C790 / 0x0040C8B0)
    auto playersAtRail = [&]() {
        playerPos[0] = railPos[0];
        playerPos[1] = eyeMode == 1 ? eyeHeight : railPos[1] - 15.0f;  // 0x004C4398
        playerPos[2] = railPos[2];
    };
    switch (rail) {
    case Rail::Event3: {  // 0x0040C380: players 15 below context 0's camera, angles from its orientation
        Mat4 r = Mat4::Identity();
        const int ref = (ctx[0].yaw - 0x8000) & 0xFFFF;
        mx::RotY(r, ref);
        mx::RotX(r, -ctx[0].pitch);
        mx::RotZ(r, ctx[0].roll);
        mx::MatrixToAnglesRef(r, ref, &playerPitch, &playerYaw, &playerRoll);  // 0x00401C50
        MtxStack s;
        s.Load(ctx[0].camera);
        s.Translate(0.0f, -15.0f, 0.0f);
        for (int k = 0; k < 3; ++k) playerPos[k] = s.Top()[12 + k];
        return;
    }
    case Rail::Nop:
    case Rail::Mode1Sub1:
    case Rail::Look:
        return;
    case Rail::LookInit:  // 0x0040C4C0: players at context 0's camera (rail height), its angles; then 0x0040C5C0
        playerPos[0] = ctx[0].pos[0];
        playerPos[1] = eyeMode == 1 ? eyeHeight : railPos[1] - 15.0f;  // 0x004C4398
        playerPos[2] = ctx[0].pos[2];
        playerPitch = -ctx[0].pitch;
        playerYaw = (ctx[0].yaw - 0x8000) & 0xFFFF;
        playerRoll = ctx[0].roll;
        // 0x0040C5C0 (Look): a sidestep while P2's button bit 1 (0x009C9028 & 0x20000) is pressed is not modelled
        rail = Rail::Look;
        return;
    case Rail::StandInit:  // 0x0040C430
        for (int k = 0; k < 3; ++k) railPos[k] = ctx[0].pos[k];
        rail = Rail::Stand;
        [[fallthrough]];
    case Rail::Stand:  // 0x0040C470
        playersAtRail();
        ticksRemaining = -1;
        return;
    case Rail::Move6Init:
    case Rail::Move7Init:
        railStart = railTick;
        rail = rail == Rail::Move6Init ? Rail::Move6 : Rail::Move7;
        [[fallthrough]];
    case Rail::Move6:    // 0x0040C790
    case Rail::Move7: {  // 0x0040C8B0
        const bool more = rail == Rail::Move6 ? railTick < railEnd : railTick <= railEnd;
        if (more) {
            if (railForce == 1 || host_.RailAdvance()) ++railTick;
            railTickF = static_cast<float>(railTick);
            int r = 0;
            Evaluate(camId, railTickF, railPos, railTarget, &r);
            DirToAngles(railPos[0] - railTarget[0], railPos[1] - railTarget[1], railPos[2] - railTarget[2], &railPitch,
                        &railYaw);  // 0x00403AC0 on the rail block
            railRoll = r;
            playersAtRail();
            // 0x0040C84C / 0x0040C96C: the players face along the rail (0x009C71EC..F4)
            playerPitch = playerRoll = 0;
            playerYaw = (railYaw - 0x8000) & 0xFFFF;
        }
        ticksRemaining = railEnd - railTick;
        return;
    }
    }
}

// ============================================================================================================
// EventRunner
// ============================================================================================================

uint32_t EventRunner::W(int i) const {
    uint32_t w = 0;
    mem_.Read(script + 4 * i, &w);
    return w;
}

void EventRunner::Fail(const std::string& what) {
    if (!error) errorText = what;
    error = true;
    host_.Error(what);
}

void EventRunner::SkipList() {
    uint32_t a = script + 4, w = 0;
    while (mem_.Read(a, &w) && w != 0xFFFFFFFFu) a += 4;
    script = a + 4;
}

void EventRunner::SpawnList(uint32_t kind, bool onlyIfNotSkipping) {
    uint32_t a = script + 4, w = 0;
    while (mem_.Read(a, &w) && w != 0xFFFFFFFFu) {
        if (!onlyIfNotSkipping || !skipping) host_.Spawn(kind, w);
        a += 4;
    }
    script = a + 4;
}

// 0x49 / 0x4A / 0x4B (0x0045F3E0 / 0x0045F470 / 0x00408AE0)
void EventRunner::PerPlayer(uint32_t op) {
    uint32_t a = script, w = 0;
    for (int i = 0; i < cfg.playerVariant; ++i) {
        a += 4;
        while (mem_.Read(a, &w) && w != 0xFFFFFFFFu) a += 4;
    }
    a += 4;
    while (mem_.Read(a, &w) && w != 0xFFFFFFFFu) {
        if (op == 0x49) host_.Resource(0x52, w);
        else if (op == 0x4A) host_.Resource(0x53, w);
        else host_.Spawn(0x0B, w);
        a += 4;
    }
    a += 4;
    while (mem_.Read(a, &w) && w != 0xFFFFFFFEu) a += 4;
    script = a + 4;
}

// 0x0045F000
void EventRunner::NextVariant() {
    auto routeWord = [&](int word, int16_t& value) {
        if (mem_.TypingDialect()) return mem_.SceneRoute(mode, word, &value);
        if (!exe_) return false;
        value = exe_->S16(exe_->U32(0x00597890 + 4 * mode) + word * 2);
        return true;
    };
    ++variant;
    uint32_t s = mem_.Script(mode, scene, variant);
    if (s == 0xFFFFFFFFu) {
        int16_t kind = 0, next = 0;
        if (!routeWord(scene * 4, kind)) { Fail("op 0x4F has no scene routing table"); halted = true; return; }
        if (kind == 0) {
            if (!routeWord(scene * 4 + 1, next)) { Fail("scene destination outside routing table"); return; }
            scene = next;
        } else if (kind == 1) {
            if (!routeWord(camera.routeBranch + scene * 4 + 1, next)) { Fail("route branch outside routing table"); return; }
            scene = next;
        }
        else if (kind == 2) scene = scene + 1;
        variant = 1;
    }
    uint32_t sceneList = 0, level = 0;
    mem_.Read(EventMemory::kCommonBase + mode * 4, &level);
    if (level) mem_.Read(level + scene * 4, &sceneList);
    if (sceneList == 0xFFFFFFFFu) {  // end of the mode's scenes: 0x0045ED90, then comevtbl[7][0][0]
        host_.StageEnd();
        if (cfg.gameType == 2) scene = static_cast<int>(globals[0x009A2BBC]);
        else {
            int16_t next = 0;
            if (routeWord(scene * 4 - 3, next)) scene = next;
        }
        // The English PC typing reference uses common mode 9 for this halt;
        // the gun game uses mode 7. Both are data, not executable callbacks.
        script = mem_.Script(mem_.TypingDialect() ? 9 : 7, 0, 0);
        if (!script) { halted = true; Fail("common stage-end script is not loaded"); }
        return;
    }
    script = mem_.Script(mode, scene, variant);
    camera.routeBranch = 0;  // 0x009C88A4
}

// Evt_Init 0x0045EBC0 + camera task init 0x00402260 (created in this order by the stage task 0x00460710).
void EventRunner::Start(int mode_, int scene_, const Config& c) {
    typingCheckpoint={};
    cfg = c;
    mode = mode_;
    scene = scene_;
    if (cfg.gameMode == 5 || cfg.gameMode == 9) variant = 0;
    else if (cfg.gameType == 1 && mode == 0) variant = 5;
    else if (cfg.gameType == 2 || cfg.gameType == 3) variant = 0;
    else variant = 1;
    camera.routeBranch = 0;
    script = mem_.Script(mode, scene, variant);
    waitCounter = -1;
    yield = false;
    skippable = skipping = false;
    globals[0x009C8E40] = 2;  // 0x00408D60
    globals[0x009C8E44] = 3;
    globals[0x009C8E48] = 4;
    camera.Init();  // also resets the queue like Evt_Init does
    halted = error = false;
    trace.clear();
}

void EventRunner::Frame() {
    camera.evals.clear();
    RunEvents();
    camera.Frame();
    camera.RailStep();
    ++frame;
}

// Evt_Run 0x0045ECC0
void EventRunner::RunEvents() {
    if (halted || error) return;
    running = host_.GameRunning();
    for (int guard = 0;; ++guard) {
        if (guard > 100000) { Fail("event VM made no progress (endless loop in the original)"); return; }
        uint32_t op = 0;
        if (!mem_.Read(script, &op)) { Fail("script pointer outside event memory"); return; }
        if (cfg.gameMode == 5 && cfg.demoIndex == 2 && op == 0x45 && W(1) == 0x13) {
            script += 8;  // demo 3 skips its wait on flag 0x13
            mem_.Read(script, &op);
        }
        trace.push_back({ frame, op, script });
        Exec(op);
        if (error || halted) return;
        if (yield) return;
    }
}

void EventRunner::Exec(uint32_t op) {
    const uint32_t a1 = W(1);
    const int32_t s1 = static_cast<int32_t>(a1);
    switch (op) {
    // ---- invalid: 0x0041EBB0 returns without advancing ----
    case 0x00: case 0x2A: case 0x34: case 0x3C: case 0x4C:
        Fail("invalid opcode " + std::to_string(op));
        return;
    // ---- spawn lists ----
    case 0x01: case 0x02: case 0x03: case 0x04:  // 0x00408820: only on route 1, as 0x09/0x0A/0x0B/0x0C
    case 0x05: case 0x06: case 0x07: case 0x08:  // 0x00408860: only on route 2
        if (cfg.route == (op <= 4 ? 1 : 2)) {
            static const uint32_t kAs[4] = { 0x09, 0x0A, 0x0B, 0x0C };
            SpawnList(kAs[(op - 1) & 3], false);
        } else {
            SkipList();
        }
        return;
    case 0x09: case 0x0A: case 0x0B: case 0x0C:  // 0x004088A0 / 0x00408990 / 0x00408AA0 / 0x00408C40
        SpawnList(op, false);
        return;
    case 0x0D:  // 0x00408B70: 0x0B unless skipping
        SpawnList(0x0B, true);
        return;
    case 0x0E:  // 0x00408D10: table 0x009A2BE0[i] = 3 words (4 entries; defaults 0x004C4CD0 restored by 0x4D)
        for (int k = 0; k < 3; ++k) globals[0x009A2BE0 + a1 * 12 + 4 * k] = W(2 + k);
        script += 24;
        return;
    case 0x0F:    // 0x00408C80: values -> 0x009C8E40.. (dword each)
    case 0x12: {  // 0x00408CC0: same, then first += 1 when 0x009C8E80 == 2
        uint32_t a = script + 4, w = 0, dst = 0x009C8E40;
        while (mem_.Read(a, &w) && w != 0xFFFFFFFFu) { globals[dst] = w; dst += 4; a += 4; }
        if (op == 0x12 && cfg.players == 2) globals[0x009C8E40] += 1;
        script = a + 4;
        return;
    }
    case 0x10:  // 0x0045F130
        globals[0x009A1A14] = script + 4;
        SkipList();
        return;
    case 0x11:  // 0x0045F160
        globals[0x009A34D8] = script + 4;
        SkipList();
        return;
    case 0x13: {  // 0x0045F190: [0] value, [1..4] pointers to dwords
        static const uint32_t kDst[5] = { 0x009A5FE0, 0x009A1A0C, 0x009A21BC, 0x009A21B4, 0x009A2444 };
        uint32_t a = script + 4, w = 0;
        for (int i = 0; mem_.Read(a, &w) && w != 0xFFFFFFFFu; ++i, a += 4) {
            if (i == 0) globals[kDst[0]] = w;
            else if (i < 5) { uint32_t v = 0; mem_.Read(w, &v); globals[kDst[i]] = v; }
        }
        script = a + 4;
        return;
    }
    case 0x14: globals[0x009A2BB4] = static_cast<uint32_t>(static_cast<int8_t>(a1)); script += 8; return;
    case 0x15: globals[0x009C8D4C] = static_cast<uint32_t>(static_cast<int8_t>(a1)); script += 8; return;
    case 0x16:  // 0x00480D20: three dwords through pointers
        for (int k = 0; k < 3; ++k) { uint32_t v = 0; mem_.Read(W(1 + k), &v); globals[0x009C71D4 + 4 * k] = v; }
        script += 16;
        return;
    case 0x17: {  // 0x0045F2A0: light-angle task (host)
        EventOp eop;
        if (!DecodeEvent(mem_, script, eop)) { Fail("undecodable env op"); return; }
        host_.Env(eop);
        script += eop.length;
        return;
    }
    case 0x18: case 0x19: case 0x20:
    case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: {  // game::Environment
        EventOp eop;
        if (!DecodeEvent(mem_, script, eop)) { Fail("undecodable env op"); return; }
        if (env_) env_->Apply(mem_, eop);
        host_.Env(eop);
        script += eop.length;
        return;
    }
    case 0x1A: {  // 0x0045F2D0
        uint32_t v = 0;
        mem_.Read(a1, &v);
        globals[0x009C8E58] = v;
        memcpy(&camera.eyeHeight, &v, 4);
        script += 8;
        return;
    }
    case 0x1B: globals[0x009A2C34] = a1; script += 8; return;
    case 0x1C: globals[0x009CA090] = a1; script += 8; return;
    case 0x1D: globals[0x009C8E50] = a1; script += 8; return;
    case 0x1E: globals[0x009C8A78] = static_cast<uint32_t>(static_cast<int16_t>(a1)); script += 8; return;
    case 0x1F: globals[0x009CA0F4] = a1 & 0xFF; script += 8; return;
    case 0x28: {  // 0x00401670
        const int s = static_cast<int16_t>(a1);
        if (s == section) host_.Section(0x28, section, prevSection);
        else host_.Section(0x28, s, section);
        script += 8;
        return;
    }
    case 0x29:  // 0x00401630
        prevSection = section;
        section = static_cast<int16_t>(a1);
        host_.Section(0x29, section, prevSection);
        script += 8;
        return;
    case 0x2B: host_.PlayerBonus(); script += 4; return;
    case 0x2C:  // 0x0045FD90
        if (a1 != 0) { globals[0x009A2230] = 0; skippable = true; }
        else { skippable = false; skipping = false; }
        script += 8;
        return;
    case 0x2D:
        if (mem_.TypingDialect()) host_.TypingVoice(static_cast<int16_t>(a1)); else host_.Voice(static_cast<int16_t>(a1));
        script += 8; return;
    case 0x2E: if (skipping) host_.Sound(0x80000002u); script += 4; return;
    case 0x2F: globals[0x009A5C48] = static_cast<uint32_t>(static_cast<int16_t>(a1)); script += 8; return;
    case 0x30: {  // 0x0045F7F0
        const uint32_t len = static_cast<uint32_t>((s1 / 16) * 4 + 8);
        if (!skipping) {
            camera.QueueBlock(script);
            if (s1 == 0x21) globals[0x009A34F0] = 0;
        }
        script += len;
        return;
    }
    case 0x31:  // 0x0045F870
        camera.StartEvent(s1);
        script += 8;
        return;
    case 0x32:  // 0x0045F900
        if (!yield) yield = true;
        if (host_.PlayersReady()) {
            camera.StartEventReady(s1);
            script += 8;
            yield = false;
        }
        return;
    case 0x33:  // 0x0045F9F0
        camera.cmdState = s1;
        camera.pending += static_cast<int32_t>(W(2));
        script += 12;
        return;
    case 0x35: camera.rollEnable = s1; script += 8; return;
    case 0x36: globals[0x009C70F4] = a1; camera.eyeMode = s1; script += 8; return;
    case 0x37: camera.railForce = s1; script += 8; return;
    case 0x38:
        if (mem_.TypingDialect()) host_.TypingSound(a1); else host_.Sound(a1);
        script += 8; return;
    case 0x39:
        if (mem_.TypingDialect()) host_.TypingSoundRepeat(a1, static_cast<int16_t>(W(2)), static_cast<int16_t>(W(3)));
        else host_.SoundRepeat(a1, static_cast<int16_t>(W(2)), static_cast<int16_t>(W(3)));
        script += 16; return;
    case 0x3A:
        if (!skipping) { if (mem_.TypingDialect()) host_.TypingSound(a1); else host_.Sound(a1); }
        script += 8; return;
    case 0x3B:
        if (!skipping) {
            if (mem_.TypingDialect()) host_.TypingSoundRepeat(a1, static_cast<int16_t>(W(2)), static_cast<int16_t>(W(3)));
            else host_.SoundRepeat(a1, static_cast<int16_t>(W(2)), static_cast<int16_t>(W(3)));
        }
        script += 16;
        return;
    case 0x3D: script += 16; return;
    case 0x3E: script += 8; return;
    case 0x3F: case 0x5B: case 0x5C: script += 4; return;
    // ---- waits ----
    case 0x40:  // 0x0045FA80
        if (!skipping) {
            if (camera.pending != 0) { yield = true; return; }
            if (!running) {
                if (!yield) Fail("op 0x40 with 0x007DCCA4 == 0 mid-frame: endless loop in the original");
                return;
            }
        }
        yield = false;
        script += 4;
        return;
    case 0x41:  // 0x0045FAC0
        if (!skipping) {
            if (!yield) { yield = true; return; }
            if (!running) return;
            if (s1 == 0 ? !(camera.ticksRemaining < 1) : !(s1 < camera.tick)) return;
        }
        yield = false;
        script += 8;
        return;
    case 0x42:  // 0x0045FB30
        if (skipping) { yield = false; waitCounter = -1; script += 8; return; }
        if (!yield) {
            waitCounter = s1;
            yield = true;
        } else if ((waitCounter < 0 || --waitCounter < 0) && running) {
            yield = false;
            waitCounter = -1;
            script += 8;
        }
        return;
    case 0x43:  // 0x0045FBC0
        if (!yield) { yield = true; return; }
        if (host_.Count7006() <= s1 && running && camera.settled) {
            if (mem_.TypingDialect() && host_.Count7006() == 0) host_.TypingSceneClear();
            yield = false; script += 8;
        }
        return;
    case 0x44:  // 0x0045FC10
        if (!yield) { yield = true; return; }
        if (mem_.TypingDialect()) {
            // English PC reference 0x0044EE70 has no extra gun-engine wait
            // counter. 0x00811D9C is its explicit one-shot override flag.
            if ((host_.LiveEnemies() <= s1 && running && camera.settled) || globals[0x00811D9C] == 1) {
                if (host_.LiveEnemies() == 0) host_.TypingSceneClear();
                globals[0x00811D9C] = 0;
                yield = false; script += 8;
            }
            return;
        }
        if (host_.LiveEnemies() <= s1 && running && camera.settled && enemyWaitFrames > 0) {
            enemyWaitFrames = 0;
            yield = false;
            script += 8;
            return;
        }
        ++enemyWaitFrames;
        return;
    case 0x45:  // 0x0045FC80
        if (!yield) { yield = true; return; }
        if (flags[a1 & 0xFF] && running) { yield = false; script += 8; }
        return;
    case 0x46:  // 0x0045FCD0
        if (!yield) { yield = true; return; }
        if (host_.CountA0E8() <= s1 && running && camera.settled) { yield = false; script += 8; }
        return;
    case 0x47:  // 0x0045FD20
        if (!yield) { yield = true; return; }
        if (running && (camera.aimedOnce || camera.settled) && !host_.Busy93C()) { yield = false; script += 4; }
        return;
    case 0x48: flags[a1 & 0xFF] = 1; script += 8; return;
    case 0x49: case 0x4A: case 0x4B: PerPlayer(op); return;
    case 0x4D:  // 0x0045EEC0
        camera.ResetForStage();
        globals[0x009A5C50] = 0;
        globals[0x009A5C48] = 0;
        globals[0x009C70F4] = 0;
        camera.eyeMode = 0;
        globals[0x009C8A74] = 0;
        if (exe_) for (uint32_t k = 0; k < 12; ++k) globals[0x009A2BE0 + 4 * k] = exe_->U32(0x004C4CD0 + 4 * k);
        script += 4;
        return;
    case 0x4E:  // 0x0045EFF0: yields forever
        yield = true;
        return;
    case 0x4F:
        NextVariant();
        return;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        host_.Resource(op, a1);
        script += 8;
        return;
    case 0x58: case 0x59: case 0x5A:
        host_.Resource(op, 0);
        script += 4;
        return;
    case 0x5D: script += 12; return;  // SS_SndLoadPack (debug stub on PC)
    case 0x5E: script += 8; return;   // SS_SndFreePack (debug stub on PC)
    case 0x5F: host_.Bgm(a1, W(2), W(3), W(4)); script += 20; return;
    case 0x60: {
        if (!mem_.TypingDialect()) { Fail("typing opcode in a native gun script"); return; }
        EventOp challenge;
        if (!DecodeEvent(mem_, script, challenge) || !host_.TypingChallenge(challenge)) {
            Fail("unsupported prepared typing challenge configuration"); return;
        }
        script += challenge.length;
        return;
    }
    case 0x62:
        if (!mem_.TypingDialect()) { Fail("typing opcode in a native gun script"); return; }
        host_.TypingCountdown(s1, static_cast<int32_t>(W(2)));
        script += 12;
        return;
    case 0x63:
        if (!mem_.TypingDialect()) { Fail("typing opcode in a native gun script"); return; }
        if (!yield) { yield = true; return; }
        if (host_.TypingChallengeStatus() == s1) { yield = false; script += 8; }
        return;
    case 0x61: case 0x64: case 0x65:
        if (!mem_.TypingDialect()) { Fail("typing opcode in a native gun script"); return; }
        if (op == 0x61 || host_.TypingPlayers() == (op == 0x64 ? 1 : 2))
            host_.TypingWords(s1, static_cast<int32_t>(W(2)));
        script += 12;
        return;
    case 0x66:
        if (!mem_.TypingDialect()) { Fail("typing opcode in a native gun script"); return; }
        host_.TypingProps(s1 == 1);
        script += 8;
        return;
    case 0x67:
        if (!mem_.TypingDialect()) { Fail("typing opcode in a native gun script"); return; }
        host_.TypingClearActorSlots();
        script += 4;
        return;
    case 0x68:
        if (!mem_.TypingDialect()) { Fail("typing opcode in a native gun script"); return; }
        host_.TypingWordLimits(s1, static_cast<int32_t>(W(2)));
        script += 12;
        return;
    case 0x6B:
        if (!mem_.TypingDialect()) { Fail("typing opcode in a native gun script"); return; }
        if (!yield) { yield = true; return; }
        // Original handler 0x0044F2D0 waits on the live-enemy count and the
        // frame's GameRunning value; it intentionally does not wait for camera
        // settling (unlike the ordinary enemy-clear command above).
        if (host_.LiveEnemies() <= s1 && running) { yield = false; script += 8; }
        return;
    case 0x70:
        if (!mem_.TypingDialect()) { Fail("typing opcode in a native gun script"); return; }
        script+=4;
        typingCheckpoint={script,mode,scene,variant,camera.routeBranch};
        return;
    case 0x71:
        if (!mem_.TypingDialect()) { Fail("typing opcode in a native gun script"); return; }
        if(typingCheckpoint.script) {
            script=typingCheckpoint.script;
            mode=typingCheckpoint.mode;scene=typingCheckpoint.scene;variant=typingCheckpoint.variant;
            camera.routeBranch=typingCheckpoint.routeBranch;
        } else script+=4;
        return;
    case 0x73: {
        if (!mem_.TypingDialect()) { Fail("typing opcode in a native gun script"); return; }
        const int modeId = host_.TypingModeId();
        const bool jump = s1 < 100 ? s1 != modeId : s1 - 100 == modeId;
        script = jump ? W(2) : script + 12;
        return;
    }
    default:
        Fail(op >= 0x60 && op < 117 ? std::string("unsupported prepared typing opcode ") + EventOpName(op) : "opcode out of range");
        return;
    }
}

}  // namespace game
