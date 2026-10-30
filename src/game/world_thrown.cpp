// game::World - weapons thrown by enemies (behaviour 33, Beh33 in world_st2_enemy.cpp): the throw 0x0045A240 and
// the weapon task 0x0045A4F0 (states 0x00593170: 1 straight flight 0x00459690, 2 arc 0x004598F0, 3 shot down
// 0x00459D20). A weapon flies at a point in front of its target player's view, hits the player on arrival
// (Player_TakeHit 0x00415300, kinds 4 / 6) and can be shot down on the way (a non-skeleton target, radius 2).

#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"
#include "game/world_modes.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

struct ThrownTask : EnemyTask {
    Task* owner = nullptr;      // +0x1390 the thrower
    uint32_t ownerSerial = 0;
    int hand = 0;               // +0x1358 node slot the weapon left (5 / 8)
    int f1338 = 7, f133C = 7;   // +0x1338 / +0x133C (set to 7, not read here)
    int spin = 0;               // +0x135C angle step per frame
    int route = 0;              // +0x1360 0x009C8E84 at the throw
    float frames = 0;           // +0x1344 flight frames left
    float speed = 1.0f;         // +0x1370
    float aim[3] = {};          // +0x13C0 the point it flies at
    int model = 0;              // +0x13F0
};

// 0x0040A040: angle v within +-range of centre
bool InRange(uint32_t v, uint32_t centre, int range) {
    uint32_t hi = (centre & 0xFFFF) + 0x10000 + range;
    uint32_t lo = ((centre & 0xFFFF) - range) + 0x10000;
    uint32_t mn = lo;
    if (hi < lo) {
        mn = hi;
        hi = lo;
    }
    v &= 0xFFFF;
    if (v < mn) v = v + (((mn - v) + 0xFFFF) & 0xFFFF0000u);
    return v <= hi;
}

// rand() & 1 with the compiler's signed-modulo fix-up (rand is never negative: plain & 1)
int RandBit(World::Impl& w, uint32_t site) { return w.Rand(site) & 1; }

// 0x0045A070: the aim point in front of the camera (camera matrix 0x009A6040[context]): (0, -1.5, -4) / (0, 0, -4)
// when both players play (route 1), else x = (1 - 2 * target) * -0.6 (0x00565EF0) to the target's side
void AimPoint(World::Impl& w, ThrownTask& k) {
    float local[3];
    const bool axe = k.model == 0x249;
    if (k.route == 1) {
        local[0] = 0.0f;
    } else {
        local[0] = static_cast<float>(k.target * -2 + 1) * F32(w.exe, 0x00565EF0);
    }
    local[1] = axe ? -1.5f : 0.0f;
    local[2] = -4.0f;
    mx::Transform(w.cam.inverse, local, k.aim);
    if (w.horde) HordeThrowAim(w, k.target, k.hand, k.aim);  // port only: at a barrel of the row (world_horde.cpp)
}

// the arrival: the target player's life (Player_TakeHit), or in the Horde mode the barrel's hp
void ThrownHit(World::Impl& w, ThrownTask& k, int kind) {
    if (w.horde) HordeThrowStrike(w, k.target, k.hand);
    else w.PlayerTakeHit(k.target, 1, kind);
}

// 0x00459B70(weapon, from, to, g, speed, &frames): a parabola with acceleration g along the axis the thrower faces
// across (yaw of aim - thrower near 0 / 0x8000: x, else z); the flight takes |to - from| (in the other two axes) / speed
void Ballistic(World::Impl& w, ThrownTask& k, const float from[3], const float to[3], float g, float speed) {
    const EnemyTask* o = (k.owner && k.owner->serial == k.ownerSerial && k.owner->data) ? &k.owner->As<EnemyTask>() : nullptr;
    const float ox = o ? o->pos[0] : from[0], oz = o ? o->pos[2] : from[2];
    int pitch = 0, yaw = 0;
    mx::DirToAngles(k.aim[0] - ox, 0.0f, k.aim[2] - oz, &pitch, &yaw);
    const bool alongZ = InRange(static_cast<uint32_t>(yaw), 0, 0x2000) || InRange(static_cast<uint32_t>(yaw), 0x8000, 0x2000);
    const float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
    if (alongZ) {
        const float f = std::sqrt(dz * dz + dy * dy) / speed;
        k.frames = f;
        k.acc[0] = g;
        k.acc[1] = 0.0f;
        k.acc[2] = 0.0f;
        k.vel[0] = ((dx + dx) + (g * -1.0f) * f * f) / (f + f);
        k.vel[1] = dy / f;
        k.vel[2] = dz / f;
    } else {
        const float f = std::sqrt(dx * dx + dy * dy) / speed;
        k.frames = f;
        k.acc[0] = 0.0f;
        k.acc[1] = 0.0f;
        k.acc[2] = g;
        k.vel[0] = dx / f;
        k.vel[1] = dy / f;
        k.vel[2] = ((g * -1.0f) * f * f + dz + dz) / (f + f);
    }
}

// The arrival and fade-out shared by the flight states (sub-states 2..4 of 0x00459690 / 0x004598F0)
void Arrive(World::Impl& w, ThrownTask& k, uint32_t site1, uint32_t site2) {
    if (k.sub == 2) {
        k.flags34 |= 0x4008000;  // no longer a target
        mx::DirToAngles(k.aim[0] - k.pos[0], 0.0f, k.aim[2] - k.pos[2], &k.rot[0], &k.rot[1]);
        k.rot[1] += 0x8000;
        const int s = RandBit(w, site1);
        const int r = w.Rand(site2);
        k.counter1330 = 0x1E;
        k.rot[0] += (s * -2 + 1) * (r & 2) * 0x100;
        w.Sound(0x5017A9);
        ++k.sub;
    }
    if (k.sub == 3) {  // stuck in front of the view for 30 frames
        AimPoint(w, k);
        memcpy(k.pos, k.aim, sizeof(k.pos));
        if (0 < --k.counter1330) return;
        ++k.sub;
        k.counter1330 = 0x3C;
    }
    if (k.sub == 4) {  // then blinks out over 60 frames
        AimPoint(w, k);
        memcpy(k.pos, k.aim, sizeof(k.pos));
        if ((k.counter1330 & 1) == 0) k.anim.drawFlags |= 1;
        else k.anim.drawFlags &= ~1u;
        if (0 < --k.counter1330) return;
        w.EnemyReleaseTarget(k);  // 0x00456520
        w.EnemyDestroy(k);        // Actor_Destroy 0x00409CC0
    }
}

// 0x00459690: straight flight
void State1(World::Impl& w, ThrownTask& k) {
    if (k.sub == 0) {
        const float dz = k.aim[2] - k.pos[2], dy = k.aim[1] - k.pos[1], dx = k.aim[0] - k.pos[0];
        const float f = std::sqrt(dz * dz + dy * dy + dx * dx) / k.speed;
        k.frames = f;
        k.vel[0] = (k.aim[0] - k.pos[0]) / f;
        k.vel[1] = (k.aim[1] - k.pos[1]) / f;
        k.vel[2] = (k.aim[2] - k.pos[2]) / f;
        w.Sound(0x4F17A9);
        ++k.sub;
    }
    if (k.sub == 1) {
        k.rot[0] += k.spin;
        for (int c = 0; c < 3; ++c) k.pos[c] = k.vel[c] + k.pos[c];
        k.frames = k.frames - 1.0f;
        if (!(k.frames <= 0.0f)) return;
        ThrownHit(w, k, 4);
        ++k.sub;
    }
    Arrive(w, k, 0x004597D4, 0x004597EE);
}

// 0x004598F0: arc
void State2(World::Impl& w, ThrownTask& k) {
    if (k.sub == 0) {
        Ballistic(w, k, k.pos, k.aim, k.hand != 5 ? -Bits(0x3C1374BC) : Bits(0x3C1374BC), 1.0f);
        w.Sound(0x4F17A9);
        ++k.sub;
    }
    if (k.sub == 1) {
        k.rot[1] += k.spin;
        for (int c = 0; c < 3; ++c) k.vel[c] = k.acc[c] + k.vel[c];
        for (int c = 0; c < 3; ++c) k.pos[c] = k.vel[c] + k.pos[c];
        k.frames = k.frames - 1.0f;
        if (!(k.frames <= 0.0f)) return;
        ThrownHit(w, k, 6);
        ++k.sub;
    }
    Arrive(w, k, 0x00459A5D, 0x00459A77);
}

// 0x00459D20: shot down: effect 0x52, then it flies off to a random point beside the view
void State3(World::Impl& w, ThrownTask& k) {
    if (k.sub == 0) {
        w.SpawnEffectAt(k.pos, 0, 0, 0x52, 1, -1);  // 0x00407340
        k.flags34 |= 0x4008000;
        w.EnemyReleaseTarget(k);
        ++k.sub;
    }
    if (k.sub == 1) {
        const int s1 = RandBit(w, 0x00459DAD);
        const int r1 = w.Rand(0x00459DD3);
        float local[3];
        local[0] = static_cast<float>(r1 % 10 + 1) * 10.0f * static_cast<float>(s1 * -2 + 1) + k.viewPos[0];  // 0x004C43A4
        const int s2 = RandBit(w, 0x00459DFA);
        const int r2 = w.Rand(0x00459E20);
        local[2] = k.viewPos[2];
        local[1] = static_cast<float>(r2 % 10 + 1) * 10.0f * static_cast<float>(s2 * -2 + 1) + k.viewPos[1];
        mx::Transform(w.cam.inverse, local, k.aim);
        const int s3 = RandBit(w, 0x00459E82);
        const float spin = static_cast<float>(s3 * -2 + 1) * (static_cast<float>(k.spin) * F32(w.exe, 0x00565EEC));  // * 1.3
        k.spin = static_cast<int>(spin);  // __ftol (truncation)
        k.counter1330 = 5;
        w.Sound(0x5017A9);
        w.Sound(0x1116A9);
        ++k.sub;
    }
    if (k.sub == 2) {
        if (0 < --k.counter1330) return;
        ++k.sub;
        k.counter1330 = 0;
    }
    if (k.sub == 3) {
        float d[3] = { k.aim[0] - k.pos[0], k.aim[1] - k.pos[1], k.aim[2] - k.pos[2] };
        const float len = std::sqrt(d[2] * d[2] + d[1] * d[1] + d[0] * d[0]);  // 0x004AAA00
        const float n[3] = { d[0] / len, d[1] / len, d[2] / len };
        const float v = k.speed;
        for (int c = 0; c < 3; ++c) k.vel[c] = v * n[c];
        ++k.counter1330;
        k.rot[0] += k.spin;
        for (int c = 0; c < 3; ++c) k.pos[c] = v * n[c] + k.pos[c];
        if (k.counter1330 < 0xB4) {
            for (float& c : d)
                if (c < 0.0f) c = c * -1.0f;
            if (v < d[0] || v < d[1] || v < d[2]) return;
        }
        w.EnemyDestroy(k);
    }
}

}  // namespace

// 0x0045A4F0
void World::Impl::TaskThrown(Task& t) {
    ThrownTask& k = t.As<ThrownTask>();
    if ((k.flags34 & 8) && !(k.flags34 & 0x4000000)) {  // shot: the shooter's hit count (+0x26), state 3
        ++player[(k.flags34 & 0x10) ? 1 : 0].hits;
        k.behaviour = 3;
        k.sub = 0;
    }
    switch (k.behaviour) {  // 0x00593170 (0: 0x00420810, nothing)
    case 1: State1(*this, k); break;
    case 2: State2(*this, k); break;
    case 3: State3(*this, k); break;
    default: break;
    }
    // (Actor_Destroy ends the task function: Task_Kill 0x004A7040 does not return)
    if (k.anim.drawFlags & 1) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotZ(m, k.rot[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotX(m, k.rot[0]);
        Draw(k.model, m);
        mx::Transform(cam.view, k.pos, k.viewPos);  // 0x009A6000[context]
        RegisterTarget(&k);                          // 0x00405160
        // 0x0040A600(weapon, 5, 5) -> 0x0040A620: shadow 0x10D0 at the floor below (drawFlags 4: floor query)
        if (!(k.flags34 & 0x80000)) {
            float y;
            if (cfg.gameMode == 0xD) y = ground;
            else if (!(k.anim.drawFlags & 4)) y = k.pos[1];
            else {
                int mat = 0;
                y = FloorHeight(k.pos[0], k.pos[1] + F32(exe, 0x004C49C0), k.pos[2], &mat);
            }
            Mat4 s = Mat4::Identity();
            mx::Translate(s, k.pos[0], y + F32(exe, 0x004C4CC8), k.pos[2]);
            mx::Scale(s, 5.0f, 1.0f, 5.0f);
            Draw(0x10D0, s, 0xD);
        }
    }
    if (k.behaviour == 1 || k.behaviour == 2) {  // lock-on candidate while it flies (0x00408EC0)
        k.body[0] = k.pos[0];
        k.body[1] = k.model == 0x249 ? k.pos[1] + F32(exe, 0x004C4CB8) : k.pos[1];
        k.body[2] = k.pos[2];
        SyncLock(k);
        lockOn.Register(k.lock, playerPos);
    } else {
        SyncLock(k);  // the camera reads a slot's object live (+0x121 = -1 once shot down)
    }
}

// 0x0045A240(thrower, hand): the weapon in node slot `hand` (5 / 8) leaves the hand (the slot shows the empty hand)
// and flies at its target player
void World::Impl::SpawnThrownWeapon(EnemyTask& e, int hand) {
    ThrownTask* k = NewTask<ThrownTask>(WorldTramp<&Impl::TaskThrown>, 0x0045A4F0);
    k->lock = &lockTargets[nextLockTarget++];  // its lock-on record (0x00408EC0 candidates)
    k->actorSlot = -1;  // Actor_RegisterSlot 0x00409270
    for (int i = 0; i < 14; ++i)
        if (!actorSlots[i]) {
            k->flags38 |= 0x40;
            actorSlots[i] = k;
            k->actorSlot = i;
            break;
        }
    auto emptyHand = [&](int m5, int m8, int weapon) {
        if (hand == 5) {
            e.anim.nodes[5].model = m5;
            e.anim.nodes[5].radius = 0.0f;
            k->model = weapon;
        } else if (hand == 8) {
            e.anim.nodes[8].model = m8;
            e.anim.nodes[8].radius = 0.0f;
            k->model = weapon;
        }
    };
    if (e.anim.kind == 1) {
        if (hand == 5) emptyHand(0x1BAC, 0, 0x1B8D);
        else if (hand == 8) emptyHand(0, 0x1BA8, 0x1B8C);
    } else if (e.anim.kind == 0x13) {
        emptyHand(0x1ECB, 0x1EC7, 0x249);
    } else if (e.anim.kind == 0x14) {
        emptyHand(0x1EF6, 0x1EF3, 0x249);
    }
    if (hand >= 0 && hand < static_cast<int>(e.anim.nodes.size()))  // the hand's view-space point, to world
        mx::Transform(cam.inverse, e.anim.nodes[static_cast<size_t>(hand)].hit, k->pos);
    k->flags34 = 0x80000001u;
    if (cfg.portTyping && k->model == 0x249) {
        // PC source 0x00449488 explicitly registers thrown axes as one-key words.
        // This helper runs under the thrower, outside its original SpawnTask.
        k->typingOriginType = 0x30;
        k->typingOriginKind = e.anim.kind;
        k->typingFlags = 0x38000A12u;
    }
    k->radius = 2.0f;
    k->radius128 = 2.0f;
    k->target = e.target;
    e.target = 0;  // (+0x121 = 0, not -1)
    k->anim.drawFlags = 5;
    k->owner = ts.Current();
    k->ownerSerial = k->owner ? k->owner->serial : 0;
    k->hand = hand;
    if (k->model == 0x249) {
        k->spin = 0xB00;
        k->behaviour = 1;
    } else {
        k->spin = 0x1600;
        k->behaviour = 2;
    }
    k->sub = 0;
    k->speed = e.variant == 7 ? 1.5f : 1.0f;
    k->route = route;  // 0x009C8E84
    AimPoint(*this, *k);
    mx::DirToAngles(k->aim[0] - k->pos[0], 0.0f, k->aim[2] - k->pos[2], &k->rot[0], &k->rot[1]);
    k->rot[2] = 0x800;
    k->body[0] = k->pos[0];
    k->body[2] = k->pos[2];
    k->body[1] = k->pos[1] + F32(exe, 0x004C4CB8);
    SyncLock(*k);
    lockOn.Register(k->lock, playerPos);  // 0x00408EC0
}

}  // namespace game
