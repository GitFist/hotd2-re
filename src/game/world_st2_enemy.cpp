// game::World - enemy behaviours (table 0x00592AE8) first reached in stage 2. Verification: notes/agents/stage2.md.

#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"

namespace game {

namespace {

// 0x004577F0 (behaviour 21): motion P word 1 after a delay of P word 2 frames (flag 0x4000 cleared when it starts);
// at its last tick: behaviour P byte 3 (0x15 -> 1), flag 0x2000 cleared, behaviour 4 takes its attack limit from the
// distance bands (as 0x00456650)
void Beh21(World::Impl& w, EnemyTask& e) {
    if (e.sub == 0) {
        w.AnimSetMotion(e, e.P(1));  // 0x00411930
        e.counter1330 = e.P(2);
        ++e.sub;
    } else if (e.sub != 1) {
        if (e.sub != 2) return;
        goto play;
    }
    if (0 < --e.counter1330) return;
    ++e.sub;
    e.flags34 &= ~0x4000u;
play:
    if (e.anim.motion[0] == 0x39B && e.anim.tick[0] == 0x26) e.flags34 &= ~0x100u;
    if (w.MotionTicks(e.anim.motion[0]) - 1 <= e.anim.tick[0]) {
        e.sub = 0;
        e.flags34 &= ~0x2000u;
        const int8_t b = static_cast<int8_t>(e.record.params[3]);
        e.behaviour = b;
        if (b == 0x15) e.behaviour = 1;
        if (e.behaviour == 4) w.EnemyApproachBand(e);
    }
}

// 0x004584E0 (behaviour 27): rise (motion 0xB9 hold for P word 1 frames, hidden when P byte 3 == 1), then motion P
// word 2 (0xB2: splashes 0x62 / 0x61 at ticks 0x16 / 0x23), then behaviour 1
void Beh27(World::Impl& w, EnemyTask& e) {
    switch (e.sub) {
    case 0: {
        uint32_t f;
        if (!(e.flags34 & 0x200000)) {
            f = e.flags136C | 0x100002;
        } else {
            e.flags34 &= ~0x200000u;
            f = e.flags136C | 0x10;
        }
        e.flags136C = f;
        e.flags34 |= 0x2100;
        e.flags136C = f & ~0x20000000u;
        if (e.record.params[3] == 1) {
            e.anim.attachHidden = true;  // 0x00409D10(anim, 0)
            e.flags34 |= 0x90000;
            e.anim.drawFlags &= ~1u;
        }
        w.AnimSetMotion(e, 0xB9);
        e.counter1330 = e.P(1);
        ++e.sub;
        memcpy(e.jumpTarget, e.pos, 12);  // +0x13CC
    }
        // fallthrough
    case 1:
        if (0 < --e.counter1330) return;
        e.anim.attachHidden = false;  // 0x00409D10(anim, 1)
        e.anim.drawFlags |= 1;
        e.flags34 &= 0xFFF6FEFFu;
        w.AnimSetMotion(e, e.P(2));
        ++e.sub;
        // fallthrough
    case 2:
        if (e.P(2) == 0xB2) {
            if (e.anim.tick[0] == 0x16) w.SpawnEffectAt(e.pos, 0, 0, 0x62, 1, -1);  // 0x00407340
            else if (e.anim.tick[0] == 0x23) w.SpawnEffectAt(e.pos, 0, 0, 0x61, 1, -1);
        }
        if (w.MotionTicks(e.anim.motion[0]) - 1 > e.anim.tick[0]) return;
        ++e.sub;
        // fallthrough
    case 3:
        e.behaviour = 1;
        e.sub = 0;
        e.flags34 &= ~0x2000u;
        e.flags136C = (e.flags136C & ~0x100000u) | 0x20000000u;
        return;
    default:
        return;
    }
}

// Weapons held for behaviour 33 (node 5 / node 8 models of skeletons 1 / 0x13 / 0x14): 0x1BA9 / 0x1BA5, 0x1ECE / 0x1ECA,
// 0x1EF9 / 0x1EF5; held5 / held8 = still in hand
bool Weapons(const EnemyTask& e, bool* held5, bool* held8) {
    int m5, m8;
    switch (e.anim.kind) {
    case 1: m5 = 0x1BA9; m8 = 0x1BA5; break;
    case 0x13: m5 = 0x1ECE; m8 = 0x1ECA; break;
    case 0x14: m5 = 0x1EF9; m8 = 0x1EF5; break;
    default: *held5 = *held8 = false; return false;
    }
    *held5 = e.anim.nodes[5].model == m5;
    *held8 = e.anim.nodes[8].model == m8;
    return true;
}

// 0x00458F00: which hand throws (5 / 8, 0 none); +0x131A = attack entry (0 for node 5, else 1)
int PickThrow(World::Impl& w, EnemyTask& e) {
    static const uint32_t kSite[3] = { 0x00459011, 0x00458F9E, 0x00458F27 };  // kinds 1 / 0x13 / 0x14
    const int k = e.anim.kind == 1 ? 0 : e.anim.kind == 0x13 ? 1 : e.anim.kind == 0x14 ? 2 : -1;
    int r = 0;
    if (k >= 0) {
        bool h5, h8;
        Weapons(e, &h5, &h8);
        const int coin = w.Rand(kSite[k]) & 1;
        if (coin == 0) {
            if (h8) r = 8;
            if (h5) {
                e.attack131A = 0;
                return 5;
            }
        } else {
            if (h5) r = 5;
            if (h8) {
                e.attack131A = 1;
                return 8;
            }
        }
        if (r == 5) {
            e.attack131A = 0;
            return 5;
        }
    }
    e.attack131A = 1;
    return r;
}

// 0x00459080 (behaviour 33): an enemy holding throwing weapons (variant 7): waits (P word 2 with one weapon, P word 1
// with two), picks a target (0x00455DE0), throw motion of attack entry +0x131A, the weapon leaves at its tick
// (0x0045A240); without weapons left it waits P word 3, then walks (behaviour 0xF) or behaviour 0x1A (P byte 3 != 0)
void Beh33(World::Impl& w, EnemyTask& e) {
    const int m0 = w.EnemyMotionSet(e, 0);
    auto entry = [&]() {
        const uint32_t tab = w.exe.U32(w.exe.U32(0x00592F18 + static_cast<uint32_t>(e.anim.kind) * 4) + static_cast<uint32_t>(e.variant) * 4);
        return tab + static_cast<uint32_t>(static_cast<int>(e.attack131A)) * 0x10;
    };
    auto idle = [&](uint32_t site) {  // hold the stand motion
        if (e.anim.motion[0] != m0) {
            const int r = w.Rand(site);
            w.AnimBlendTo(e, m0, r % w.MotionTicks(m0), 5);
        }
    };
    auto count = [&]() {
        bool h5, h8;
        Weapons(e, &h5, &h8);
        return (h5 ? 1 : 0) + (h8 ? 1 : 0);
    };
    uint32_t ent = entry();
    switch (e.sub) {
    case 0:
        if (e.variant == 7) {
            if (!(e.flags34 & 0x1000000)) {
                const uint32_t f = e.flags136C;
                e.flags136C = f | 1;
                if (e.flags34 & 0x20000) e.flags136C = f | 0x100001;
            }
            const int c = count();
            if (c == 1) e.counter1330 = e.P(2);
            else if (c == 2) e.counter1330 = e.P(1);
            else w.EnemyDespawn(e);  // 0x00455490
        }
        ++e.sub;
        // fallthrough
    case 1:
        if (e.variant == 7) {
            idle(0x004591C2);
            if (0 < --e.counter1330) return;
        }
        ++e.sub;
        // fallthrough
    case 2:
        if (e.variant == 7 && w.EnemyPickTarget(e) == 0) {
            if (e.anim.motion[0] == m0) return;
            const int r = w.Rand(0x0045922B);
            w.AnimBlendTo(e, m0, r % w.MotionTicks(m0), 5);
            return;
        }
        e.flags34 |= 0x10000000;
        if (e.target >= 0 && e.target < 2) w.targetTaken[e.target] = 1;  // 0x009A2BA0
        e.f135C = PickThrow(w, e);
        ent = entry();
        w.AnimBlendTo(e, w.exe.S16(ent), 0, 4);
        w.EnemyVoice(e, 3);  // 0x0040A6F0
        ++e.sub;
        // fallthrough
    case 3:
        if (e.anim.tick[0] != w.exe.S16(ent + 8)) return;
        w.SpawnThrownWeapon(e, e.f135C);  // 0x0045A240
        if (e.anim.kind == 1) {
            w.EnemyPickTarget(e);
            e.f135C = PickThrow(w, e);
            w.SpawnThrownWeapon(e, e.f135C);  // the second weapon
        }
        if (e.variant == 8 && count() == 0) {  // 0x004595F0
            e.variant = 0;
            e.flags34 |= 0x8000000;
        }
        ++e.sub;
        // fallthrough
    case 4:
        if (e.anim.tick[0] < w.MotionTicks(e.anim.motion[0]) - 1) return;
        e.flags34 &= ~0x10000000u;
        if (e.flags136C & 0x20000) {
            e.flags136C &= ~0x20000u;
            w.attackSlot = 0;  // 0x009A34F0
        }
        if (e.variant != 7) {
            e.behaviour = 1;
            e.sub = 0;
            return;
        }
        if (count() != 0) {
            e.sub = 0;
            return;
        }
        e.counter1330 = e.P(3);
        ++e.sub;
        // fallthrough
    case 5:
        idle(0x00459429);
        if (0 < --e.counter1330) return;
        if (!(e.flags38 & 0x10)) {
            e.flags34 &= ~0x1000000u;
            if (e.record.params[3] == 0) {
                e.flags34 |= 0x20000000;
                const int32_t p4 = e.P(4);
                memcpy(&e.walkDistance, &p4, 4);  // +0x1370
                e.sub = 1;
                e.behaviour = 0xF;
                return;
            }
            e.behaviour = 0x1A;
            e.sub = 1;
            e.counter1334 = 0;
            w.Note("enemy behaviour 33 -> 0x1A (+0x1374..+0x1380 from P words 4, 5, 6, 8) not modelled");
            if (e.variant == 7) e.variant = 5;
            return;
        }
        if (!(e.flags38 & 2)) {  // 0x00456560
            e.flags38 |= 2;
            --w.liveEnemies;
        }
        if (!(e.flags38 & 4)) {  // 0x00456580
            e.flags38 |= 4;
            --w.attackers;
        }
        w.EnemyReleaseTarget(e);  // 0x00456520
        if (e.slot120 != -1) w.lockOn.Release(e.slot120);
        if ((e.flags38 & 0x40) && e.actorSlot != -1) {
            w.actorSlots[e.actorSlot] = nullptr;
        }
        e.counter1330 = e.P(7);
        ++e.sub;
        e.flags34 |= 0x10100;
        // fallthrough
    case 6:
        idle(0x00459571);
        if (w.attackers < 1 && w.players != 0 && --e.counter1330 < 1 && (e.flags38 & 0x10)) w.EnemyDestroy(e);  // 0x00409CC0
        return;
    default:
        return;
    }
}

float F32At(const ExeImage& exe, uint32_t va) {
    float f;
    exe.Read(va, &f, 4);
    return f;
}

// 0x00454B90 (behaviour 11): fall to the floor under the body point (variant 4: blend to motion 0x41D, lighter
// gravity, bounces with sound 0x2A16A9 while |v| > 0x004C4D08), then behaviour 1 at the motion's last tick
void Beh11(World::Impl& w, EnemyTask& e) {
    const ExeImage& exe = w.exe;
    if (e.sub == 0) {
        e.flags34 |= 0x22400;
        e.flags136C |= 0x100000;
        if (e.variant == 4) {
            const int r = w.Rand(0x00454BDD);
            w.AnimBlendTo(e, 0x41D, r % 5, 5);  // 0x004119A0
            const uint32_t u = 0xBDA740DAu;
            memcpy(&e.acc[1], &u, 4);
        } else {
            const uint32_t u = 0xBE2740DAu;
            memcpy(&e.acc[1], &u, 4);
        }
        ++e.sub;
    } else if (e.sub != 1) {
        if (e.sub != 2) return;
        goto settle;
    }
    {
        e.vel[1] = e.acc[1] + e.vel[1];
        int mat = 0;
        const float f = w.FloorHeight(e.pos[0], e.body[1], e.pos[2], &mat);  // 0x00409D40(x, +0x104, z)
        if (f < e.pos[1] + e.vel[1]) return;
        e.pos[1] = f;
        if (e.variant == 4) {
            float v = e.vel[1] * F32At(exe, 0x004C4D0C);
            e.vel[1] = v;
            if (v < 0.0f) v = v * F32At(exe, 0x004C4C64);
            if (F32At(exe, 0x004C4D08) < v) {
                w.Sound(0x2A16A9);
                return;
            }
        } else if (e.hp < 1) {
            w.AnimBlendTo(e, 0x3BA, 0xF, 1);
        }
        for (int k = 0; k < 3; ++k) e.acc[k] = e.vel[k] = 0;  // 0x0044E120
        e.pos[1] = f;
        ++e.sub;
        e.flags34 &= 0xFFFDDBFFu;
        e.flags136C &= 0xFFEFFFFFu;
    }
settle:
    if (e.anim.tick[0] < w.MotionTicks(e.anim.motion[0]) - 1) return;
    e.behaviour = 1;
    e.sub = 0;
}

// 0x00458960 (behaviour 29): ride the actor 0x33 state-1 object 0x009A5C34 at the spawn offset (+0x13E4), motion set
// entry 0 (re-blended from rand % 5 when it changed), facing the players with flag 0x20 of +0x38; when the object is
// flagged 0x10000000: behaviour P byte 3
void Beh29(World::Impl& w, EnemyTask& e) {
    const int motion = w.EnemyMotionSet(e, 0);
    if (e.sub == 0) {
        memcpy(e.faceTarget, e.pos, 12);
        e.flags136C |= 0x100000;
        ++e.sub;
    }
    const int r = w.Rand(0x004589CE) % 5;
    w.EnemySetMotion(e, motion, r, 10);  // 0x00454770
    EnemyTask* o = w.actor33;
    if (!o) {
        w.Note("enemy behaviour 29 without its actor 0x33 object");
        return;
    }
    e.pos[0] = e.faceTarget[0] + o->pos[0];
    e.pos[1] = e.faceTarget[1] + o->pos[1];
    e.pos[2] = e.faceTarget[2] + o->pos[2];
    if (e.flags38 & 0x20) w.EnemyTurnTo(e, w.playerPos[0], w.playerPos[2], 0x1A0);  // 0x00409E80
    if (o->flags34 & 0x10000000) {
        e.flags136C &= ~0x100000u;
        e.behaviour = static_cast<int8_t>(e.record.params[3]);
        e.sub = 0;
    }
}

}  // namespace

void World::Impl::EnemyBeh33(EnemyTask& e) { Beh33(*this, e); }  // 0x00459080 (dispatched from EnemyBehaviour)

bool World::Impl::EnemyBehaviourSt2(EnemyTask& e) {
    switch (e.behaviour) {
    case 11: Beh11(*this, e); return true;
    case 21: Beh21(*this, e); return true;
    case 29: Beh29(*this, e); return true;
    case 27: Beh27(*this, e); return true;
    default: return false;
    }
}

// 0x0045DD70(entry): the skeleton entry's node and its subtree switched off (flags +0x74 = 0x20, model 0, radius 0)
static void NodesOff(World::Impl& w, EnemyTask& e, uint32_t entry) {
    const int slot = w.exe.S16(entry + 0x14);
    if (slot >= 0 && slot < 32) {
        e.anim.nodes[slot].flags = 0x20;
        e.anim.nodes[slot].model = 0;
        e.anim.nodes[slot].radius = 0;
    }
    const int n = w.exe.S16(entry + 0x16);
    for (int i = 0; i < n; ++i) NodesOff(w, e, w.exe.U32(entry + 0x18 + 4 * static_cast<uint32_t>(i)));
}

// Enemy_SetupKind 0x00452FD0 case 0xC: flag 0x80; variant 4 (0x0045DA10): collision radius +0x128 * 0x0056798C, flags
// 0x6000080, the second top-level subtree off (0x0045DD30(9): 0x004E0430[kind] + 0x1C) and node 9 model 0x1DA3
void World::Impl::EnemySetupKind12(EnemyTask& e) {
    e.flags136C |= 0x80u;
    if (e.variant != 4) return;
    float k;
    exe.Read(0x0056798C, &k, 4);
    e.radius128 = e.radius128 * k;
    e.flags136C |= 0x6000080u;
    NodesOff(*this, e, exe.U32(exe.U32(0x004E0430 + 4 * static_cast<uint32_t>(e.anim.kind)) + 0x1C));
    e.anim.nodes[9].model = 0x1DA3;  // +0x71C
}

}  // namespace game
