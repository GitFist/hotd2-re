// game::World - enemy behaviours first met in stage 5. Status and verification: notes/agents/stage5.md.

#include <cmath>
#include <cstring>

#include "game/world_st5.h"

namespace game {

using namespace st5;

namespace {

// 0x00409DC0: the target is valid only in the shooting camera (mode 2) and (demo or player state 5)
bool TargetValid(World::Impl& w, int t) {
    return w.runner->camera.modeCur == 2 && (w.cfg.gameMode == 5 || (t >= 0 && w.player[t & 1].state == 5));
}

uint32_t AttackEntry(const World::Impl& w, const EnemyTask& e, int idx) {  // 0x00592F18[kind][variant] + idx * 0x10
    return w.exe.U32(w.exe.U32(0x00592F18 + 4 * static_cast<uint32_t>(e.anim.kind)) + 4 * static_cast<uint32_t>(e.variant)) +
           static_cast<uint32_t>(idx) * 0x10;
}

// 0x00456490: the attack's hit at its tick (Player_TakeHit with entry +0xA unless the lost parts mask it)
void AttackHit(World::Impl& w, EnemyTask& e) {
    const int t = e.target;
    const uint32_t ent = AttackEntry(w, e, e.attack131A);
    const uint16_t mask = static_cast<uint16_t>(w.exe.S16(ent + 0xC));
    if ((t == 0 || t == 1) && ((static_cast<int16_t>(static_cast<int8_t>(e.lostParts & 7)) & mask) != mask)) {
        const int kind = w.exe.S16(ent + 0xA);
        if (e.flags34 & 0x2000000) {
            w.PlayerTakeHit(t, 0, kind);
            w.EnemyDespawn(e);  // 0x00455490
            return;
        }
        w.PlayerTakeHit(t, 1, kind);
    }
}

// 0x0045E830 (behaviour 32): an enemy riding the object 0x009A5C34 (record flag 8, 0x0045E770): wait P s16 4 frames,
// then P s16 6 more with players in the game, attack the player P s8 8 (-1: random; else the free one) with an attack
// of 0x00592DC0, repeat from the second wait; idle motion 0x00592CBC[.][.][(+0x136C >> 21) & 1]; when the ridden
// object has +0x34 & 0x40000000 for more than 20 frames, behaviour 10 (despawn)
void Beh32(World::Impl& w, EnemyTask& e) {
    const int idle = w.EnemyMotionSet(e, static_cast<int>((e.flags136C >> 21) & 1));
    uint32_t ent = AttackEntry(w, e, e.attack131A);
    bool attack = false;
    switch (e.sub) {
    case 0:
        e.counter1330 = e.record.ParamS16(4);
        ++e.sub;
        [[fallthrough]];  // (the switch's break leads to case 1's countdown)
    case 1:
        if (0 < --e.counter1330) break;
        ++e.sub;
        [[fallthrough]];
    case 2:
        ++e.sub;
        e.counter1330 = e.record.ParamS16(6);
        [[fallthrough]];
    case 3:
        if (w.players == 0) break;
        ++e.sub;
        [[fallthrough]];
    case 4: {
        if (0 < --e.counter1330 || w.players == 0) break;
        int t = e.record.ParamS8(8);
        e.target = static_cast<int8_t>(t);
        if (t == -1) {
            t = w.Rand(0x0045E930) & 1;
            e.target = static_cast<int8_t>(t);
        }
        if (!TargetValid(w, t) || w.targetTaken[t & 1] == 1) {
            if (t == 0) t = (TargetValid(w, 1) && w.targetTaken[1] == 0) ? 1 : -1;
            else if (t == 1) t = (TargetValid(w, 0) && w.targetTaken[0] == 0) ? 0 : -1;
            e.target = static_cast<int8_t>(t);
        }
        if (t == -1) break;
        w.targetTaken[t & 1] = 1;
        w.EnemyFaceTarget(e);  // 0x00455F40
        e.flags34 |= 0x10000000;
        const int r = w.Rand(0x0045E9FB);
        const uint32_t tab = w.exe.U32(w.exe.U32(0x00592DC0 + 4 * static_cast<uint32_t>(e.anim.kind)) + 4 * static_cast<uint32_t>(e.variant));
        const int8_t pick = static_cast<int8_t>(w.exe.U32(tab + static_cast<uint32_t>(((r >> 4) % 10 + (e.lostParts & 7) * 10) * 4)) & 0xFF);
        e.attack131A = pick;
        ent = AttackEntry(w, e, pick);
        w.AnimBlendTo(e, w.exe.S16(ent), 0, 5);  // 0x004119A0
        w.EnemyVoice(e, 3);                      // 0x0040A6F0
        ++e.sub;
        attack = true;
        break;
    }
    case 5:
        attack = true;
        break;
    default:
        break;
    }
    if (attack) {
        w.EnemyFaceTarget(e);
        if (e.anim.tick[0] == w.exe.S16(ent + 8)) AttackHit(w, e);
        if (w.MotionTicks(e.anim.motion[0]) - 1 <= e.anim.tick[0]) {
            w.EnemyReleaseTarget(e);  // 0x00456520
            ++e.sub;
            e.flags34 &= ~0x10000000u;
        }
    }
    if (!(e.flags34 & 0x10000000) && e.anim.motion[0] != idle) {
        const int r = w.Rand(0x0045EAD1);
        w.AnimBlendTo(e, idle, r % 5, 5);
    }
    if (e.flags38 & 0x20) w.EnemyTurnTo(e, w.playerPos[0], w.playerPos[2], 0x1A0);  // 0x00409E80
    const ActorTask* o = w.actor33 ? static_cast<const ActorTask*>(w.actor33) : w.obj5C34;
    if (o && (o->flags34 & 0x40000000)) {
        if (0x14 < ++e.counter1334) {
            e.behaviour = 10;
            e.sub = 0;
        }
    } else {
        e.counter1334 = 0;
    }
}

}  // namespace

// 0x0045E770: an enemy riding the object 0x009A5C34: position = its saved local point (+0x13D8) in the object's frame
// (yaw + 0x8000), yaw = the frame's yaw + the saved yaw (+0x135C)
void World::Impl::EnemyRide5C34(EnemyTask& e) {
    const EnemyTask* o = actor33 ? actor33 : dynamic_cast<const EnemyTask*>(obj5C34);
    if (!o) {
        Note("enemy 0x0045E770: no object 0x009A5C34");
        return;
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, o->pos[0], o->pos[1], o->pos[2]);
    mx::RotY(m, o->rot[1]);
    mx::RotY(m, 0x8000);
    float p[3];
    mx::TransformX87(m, e.savedPos, p);
    memcpy(e.pos, p, 12);
    int ax = 0, az = 0;
    MatrixToAnglesYAxis(m, &ax, &e.rot[1], &az);  // 0x00401AE0
    e.rot[1] += e.f135C;
}

// Enemy_SetupKind 0x00452FD0, record flag 8: the rider's local point / yaw, then 0x0045E770
void World::Impl::EnemySetupRide(EnemyTask& e) {
    memcpy(e.savedPos, e.pos, 12);  // +0x13D8
    e.flags136C |= 0x10000000;
    e.f135C = e.rot[1];  // +0x135C
    EnemyRide5C34(e);
    e.flags136C |= 0x100000;
}

namespace {

int64_t Ftol(double v) { return static_cast<int64_t>(v); }  // _ftol 0x004ACF50 (truncation)

// ---------------------------------------------------------------------------------------------------------------------
// The thing that bursts out of a behaviour-25 enemy's node 1 (0x0043E720 -> task 0x0043E790 -> 0x0043E880): models
// 0x1D31 + (+0x1330 % 40) flying in VIEW space from the node towards the camera (1P: x = z = 0; 2P: a random side
// 0x00564418 / 0x00564410); +0x1344 the path parameter, +0x1348 its step (0.08, x 0x00564408 each frame, at least
// 0.005); 30 frames after the parameter passes 0x00564404 it hits the player (Player_TakeHit(p, 1, 9)) and falls
// (state 4); shot: +80, blood 0x00430C50, falls spinning the other way (state 6); removed below y 0x005643FC.
// The extra heap block +0x1390 (0x504 bytes) holds: +0x00 position, +0x18 start, +0x24 spin, +0x38 / +0x3C target x / z,
// +0x64 start distance.
struct BurstTask : EnemyTask {
    EnemyTask* parent = nullptr;  // 0x007DCBD8[+0x1350]: the enemy it came out of
    uint32_t parentSerial = 0;
    float t1344 = 0, step1348 = 0, f134C = 0;  // +0x1344 / +0x1348 / +0x134C
    int c133C = 0;                // +0x133C
    float start[3] = {};          // block +0x18
    int spin = 0;                 // block +0x24
    float tx = 0, tz = 0;         // block +0x38 / +0x3C
    float d0 = 0;                 // block +0x64
};

bool LiveParent(World::Impl& w, const BurstTask& k) {
    if (!k.parent) return false;
    for (Task* t : w.ts.Live())
        if (t->serial == k.parentSerial && t->data.get() == k.parent) return true;
    return false;
}

void BurstDraw(World::Impl& w, BurstTask& k) {
    // 0x0041DC70 / 0x0041DCC0 (light: renderer); identity (0x004A9E10), translate, RotX, model
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotX(m, k.rot[0]);
    w.Draw(k.counter1330 % 0x28 + 0x1D31, m, 8, -1.0f, false, -1, true);
}

void TaskBurst(Task& t) {
    World::Impl& w = *CurrentWorld();
    BurstTask& k = t.As<BurstTask>();
    const ExeImage& exe = w.exe;
    if (k.lock) k.slot120 = static_cast<int8_t>(k.lock->slot);
    auto releaseSlot = [&] {
        if (!(k.flags34 & 0x10000) && k.slot120 != -1) w.lockOn.Release(k.slot120);  // 0x009A5EC0[+0x120 * 8] = 0
    };
    if (k.fn == 0x0043E790) {
        // first run: hp 1, radius 0.8, +0x100 = the enemy's body point; +0x70 from +0x40 (stale heap contents in the
        // original: not modelled, the target is registered at the view origin, which no shot reaches)
        k.flags34 = 1;
        k.hp = 1;
        k.f134C = Bits(0x3E99999A);
        k.t1344 = 0;
        k.counter1330 = 0;
        k.c133C = 1;
        k.behaviour = 0;
        k.radius = Bits(0x3F4CCCCD);
        ++w.attackers;
        ++w.liveEnemies;
        if (k.parent) memcpy(k.body, k.parent->body, 12);
        w.SyncLock(k);  // the lock-on table reads +0x100 / +0x34 live: the camera's next look-at uses this frame's point
        memset(k.viewPos, 0, 12);  // (0x00405160 with that stale point: not registered here)
        k.fn = 0x0043E880;
        return;
    }
    switch (k.behaviour) {
    case 0: {
        const bool live = LiveParent(w, k);
        if (!live) w.Note("behaviour-25 burst: its enemy is gone (the original reads its memory)");
        // the matrix-stack top (the view) inverted (0x004A8D20), x node 1's model-view +0x28 (0x004A92A0): node 1 in
        // the world; 1.5 up (0x004C4CB8), back into the view by the top (0x004A8A80)
        float p[3] = {};
        Mat4 a = mx::Inverse(w.cam.view);
        if (k.parent) mx::MulLeft(a, k.parent->anim.nodes[1].mv);
        mx::GetTranslation(a, p);
        p[1] = p[1] + F32(exe, 0x004C4CB8);
        mx::Transform(w.cam.view, p, k.pos);
        w.Sound(0x3A16A9);
        k.counter1330 = 0;
        k.counter1334 = 0;
        k.flags34 |= 0x10000000;
        k.t1344 = 0;
        k.step1348 = Bits(0x3DA3D70A);
        k.behaviour = 3;
        k.rot[0] = 0;
        k.rot[1] = w.cam.yaw;  // 0x009A60D0 + context
        k.rot[2] = 0;
        memcpy(k.start, k.pos, 12);
        k.tx = k.tz = 0;
        k.d0 = static_cast<float>(std::sqrt(static_cast<double>(k.pos[2]) * k.pos[2] + static_cast<double>(k.pos[0]) * k.pos[0]));
        if (w.players == 2) {
            const int r = w.Rand(0x0043EA1C) & 1;
            k.target = static_cast<int8_t>(r);
            const double a = F64(exe, r == 0 ? 0x00564418 : 0x00564410);
            k.tx = static_cast<float>(std::sin(a) * F64(exe, 0x004E3108));
            k.tz = static_cast<float>(std::cos(a) * F64(exe, 0x004E3108));
        } else {
            if (w.playersInGame == 0) k.target = 0;
            if (w.playersInGame == 1) k.target = 1;
        }
        break;
    }
    case 3: {
        ++k.counter1330;
        if (k.flags34 & 8) {
            w.Sound(0x3B16A9);
            const uint32_t g = k.flags34 & 2;
            int p;
            if (g == 0 || !(k.flags34 & 4)) p = g == 0 ? 1 : 0;
            else p = w.Rand(0x0043EBAB) & 1;
            w.AddScore(p, 0x50);  // 0x004156C0
            ++w.player[p].hits;
            w.SpawnBlood430BD0(k.pos);  // 0x00430C50 (the position is in view space)
            k.counter1330 = 0;
            k.vel[1] = 0;
            k.spin = 0x100;
            k.behaviour = 6;
            releaseSlot();
            return;
        }
        const double t = k.t1344;
        k.pos[0] = static_cast<float>((static_cast<double>(k.tx) - k.start[0]) * t + k.start[0]);
        const int64_t a = Ftol(t * F32(exe, 0x0056440C)) & 0xFFFF;
        const double s = std::sin(static_cast<double>(a) * F64(exe, 0x004C4370));
        k.pos[1] = static_cast<float>((s + -static_cast<double>(k.start[1]) * t) + k.start[1]);
        const double z = (static_cast<double>(k.tz) - k.start[2]) * t + k.start[2];
        k.pos[2] = static_cast<float>(z);
        if (k.counter1334 == 0) {
            const double x = k.pos[0];
            double r = std::sqrt(x * x + z * z) / k.d0;
            if (r < F32(exe, 0x004C436C)) r *= F32(exe, 0x004C4C64);
            r *= F32(exe, 0x004E1FE0);
            const int64_t b = Ftol(r) & 0xFFFF;
            const double sb = std::sin(static_cast<double>(b) * F64(exe, 0x004C4370)) * F32(exe, 0x0055D22C);
            k.rot[0] = -static_cast<int>(Ftol(sb) & 0xFFFF);
            k.t1344 = static_cast<float>(static_cast<double>(k.step1348) + k.t1344);
            const double st = static_cast<double>(k.step1348) * F32(exe, 0x00564408);
            k.step1348 = static_cast<float>(st);
            if (st < F32(exe, 0x004E30E0)) k.step1348 = Bits(0x3BA3D70A);
            if (k.t1344 > F32(exe, 0x00564404)) k.counter1334 = 1;
        }
        if (k.counter1334 > 0 && 0x1E < ++k.counter1334) {
            releaseSlot();
            const int p = k.target;
            if (w.runner->camera.modeCur == 2 && w.players > 0 && (p == 0 || p == 1) && w.player[p].state == 5) {
                w.PlayerTakeHit(p, 1, 9);  // 0x00415300
                w.Sound(0);
            }
            k.behaviour = 4;
            k.spin = -0x200;
            k.vel[1] = 0;
            k.vel[2] = 0;
        }
        memcpy(k.viewPos, k.pos, 12);
        w.RegisterTarget(&k);  // 0x00405160
        break;
    }
    case 4:
    case 6: {
        const double vy = static_cast<double>(k.vel[1]) - F32(exe, 0x00564400);  // (fst: the sum uses the unrounded value)
        k.vel[1] = static_cast<float>(vy);
        k.pos[1] = static_cast<float>(vy + k.pos[1]);
        k.rot[0] += k.spin;
        k.spin = static_cast<int>(Ftol(static_cast<double>(k.spin) * F32(exe, 0x005643B8)));
        if (k.behaviour == 4) {
            if (k.rot[0] < -0x4000) k.rot[0] = -0x4000;
        } else if (0x4000 < k.rot[0]) {
            k.rot[0] = 0x4000;
        }
        if (k.pos[1] <= F32(exe, 0x005643FC)) {
            --w.attackers;
            --w.liveEnemies;
            w.EnemyDestroy(k);  // 0x00409CC0
            return;
        }
        break;
    }
    default:
        break;
    }
    BurstDraw(w, k);
    if (!(k.flags34 & 0x10000) && (k.behaviour == 0 || k.behaviour == 3)) {
        if (k.behaviour == 3) mx::Transform(w.cam.inverse, k.pos, k.body);  // camera matrix 0x009A6040 + context
        else memcpy(k.body, k.pos, 12);
        w.SyncLock(k);
        if (k.lock) w.lockOn.Register(k.lock, w.playerPos);  // 0x00408EC0
    }
}

// 0x0043E720(enemy): the burst task (first run 0x0043E790), its lock-on slot (0x00408E80)
void SpawnBurst(World::Impl& w, EnemyTask& e) {
    BurstTask* k = w.NewTask<BurstTask>(TaskBurst, 0x0043E790);
    k->parent = &e;
    k->parentSerial = w.CurrentSerial();
    memcpy(k->body, e.body, 12);
    k->slot120 = -1;
    k->lock = &w.lockTargets[w.nextLockTarget++];
    memcpy(k->lock->pos, k->pos, 12);
    memcpy(k->lock->point, k->body, 12);
    k->lock->targetPlayer = -1;
    w.lockOn.AddDirect(k->lock);
    k->slot120 = static_cast<int8_t>(k->lock->slot);
}

// 0x00457FB0 (behaviour 25): no damage (+0x11C = 0, +0x34 | 0x3500); walks in motion 0x1E3 while nearer than the
// approach distance 0x009A2BE0[+0x131F] (k = 0) to the players; then motion 0x1DF, and 95..104 frames later node 1
// bursts (stage-1 wound model of node 1, blood 0x00407200(1), the burst task 0x0043E720, +0x136C | 0x8000); at the
// motion's end behaviour 6.
void Beh25(World::Impl& w, EnemyTask& e) {
    switch (e.sub) {
    case 0:
        e.hp = 0;
        e.flags34 |= 0x3500;
        e.flags136C |= 0x80000000u;
        ++e.sub;
        break;
    case 1:
        break;
    case 2: case 3: case 4:
        break;
    default:
        return;
    }
    if (e.sub == 1) {
        const float dz = e.pos[2] - w.playerPos[2], dx = e.pos[0] - w.playerPos[0];
        const double d = std::sqrt(static_cast<double>(dz) * dz + static_cast<double>(dx) * dx);  // (compared unrounded)
        if (!(w.ApproachDistance(e.c131F, 0) <= d)) {
            if (e.anim.motion[0] != 0x1E3) w.AnimBlendTo(e, 0x1E3, w.Rand(0x0045804B) % 10, 5);
            return;
        }
        ++e.sub;
    }
    if (e.sub == 2) {
        w.AnimBlendTo(e, 0x1DF, w.Rand(0x00458078) % 10, 10);
        e.counter1330 = 0;
        const int r = w.Rand(0x004580A4);
        ++e.sub;
        e.counter1334 = r % 10 + 0x5F;
    }
    if (e.sub == 3) {
        if (!(e.flags136C & 0x8000) && ++e.counter1330 == e.counter1334) {
            EnemyNode& n1 = e.anim.nodes[1];
            n1.stage = 1;  // +0x328
            n1.model = static_cast<uint16_t>(w.exe.S16(w.exe.U32(0x004C7160 + 4 * static_cast<uint32_t>(e.anim.kind)) + 0xE));
            w.SpawnNodeBlood(e, 1);  // 0x00407200
            SpawnBurst(w, e);        // 0x0043E720 (0x0041EBB0(obj, 1) between: a no-op)
            e.flags136C |= 0x8000;
            w.EnemyReleaseTarget(e);  // 0x00456520
            e.texCounter = -1;        // +0x1328
        }
        if (e.anim.tick[0] < w.MotionTicks(e.anim.motion[0]) - 1) return;
        ++e.sub;
    }
    e.behaviour = 6;
    e.sub = 0;
    e.flags136C |= 0x80000000u;
}

}  // namespace

bool World::Impl::EnemyBehaviourSt5(EnemyTask& e) {
    switch (e.behaviour) {
    case 10:
        EnemyDespawn(e);  // 0x00455490
        return true;
    case 25:
        Beh25(*this, e);
        return true;
    case 32:
        Beh32(*this, e);
        return true;
    default:
        return false;
    }
}

}  // namespace game
