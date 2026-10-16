// game::World - stage-6 enemies: Enemy_SetupKind cases 9 / 0x12 (the fading-in enemy and its hit proxy), the proxy's
// task 0x00453290, the fade nodes of 0x004534A0. Status: notes/agents/stage6.md.

#include <cmath>
#include <cstring>

#include "game/world_st6.h"

namespace game {

using namespace st6;

// Enemy_SetupKind 0x00452FD0 cases 9 and 0x12
void World::Impl::EnemySetupKind18(EnemyTask& e) {
    if (e.anim.kind == 9) {
        e.flags136C &= 0x9FFFFFFFu;
        e.flags1368 |= 0x20;
        e.alpha138C = Bits(0x3E800000);  // 0.25
        e.fadeStep1388 = Bits(0x3C888889);  // 1 / 60
        e.fade134C = 100.0f;
        return;
    }
    // case 0x12: fades in over 100 + 30 frames (node 0x1C6C); a kind-9 copy (task 0x00453290) takes the shots meanwhile
    const uint32_t f = e.flags34;
    e.alpha138C = 0.0f;
    e.fadeStep1388 = Bits(0x3D088889);  // 1 / 30
    e.fade134C = 100.0f;
    e.flags136C &= 0x9FFFFFFFu;
    e.flags1368 |= 0x20;
    e.flags34 = f | 0x48500;
    if (f & 0x10000000) return;
    EnemyTask* c = NewTask<EnemyTask>(WorldTramp<&World::Impl::TaskEnemy>, fn::kEnemyCreate);  // block 0x13F4, Task_Clear
    c->lock = &lockTargets[nextLockTarget++];
    c->record = e.record;  // +0x1390
    c->flags34 = 0;
    c->objMesh = 0;
    c->slot120 = 0;
    c->target = 0;
    c->killer = 0;
    c->c131D = 0;
    memcpy(c->pos, e.pos, 12);  // +0x40..+0x9F
    memcpy(c->vel, e.vel, 12);
    memcpy(c->acc, e.acc, 12);
    memcpy(c->rot, e.rot, 12);
    memcpy(c->viewPos, e.viewPos, 12);
    c->parent13A4 = &e;
    c->parentSerial = CurrentSerial();
    c->parentFlags = e.flags34;
    c->variant = e.variant;
    c->anim.kind = 9;
    c->hp = 999;
    c->hpMax = 0;
    c->lo = 0;
}

// 0x00453290: the kind-9 hit proxy of a kind-0x12 enemy: follows its owner (position, head, motions) while its own
// fade (+0x138C) is above 0 and the owner is not removed (+0x34 bit 0x4000000); takes shots, registers as a target
EnemyTask* World::Impl::Kind9Owner(EnemyTask& e) {
    if (!e.parent13A4) return nullptr;
    for (Task* t : ts.Live())
        if (t->serial == e.parentSerial && t->data && dynamic_cast<EnemyTask*>(t->data.get()) == e.parent13A4)
            return e.parent13A4;
    return nullptr;
}

void World::Impl::EnemyKind9Update(EnemyTask& e) {
    EnemyTask* o = Kind9Owner(e);
    if (o) e.parentFlags = o->flags34;
    else if (e.parent13A4) {
        Note("kind-9 hit proxy: owner removed (0x00453290 reads its freed block)");
        e.parent13A4 = nullptr;
    }
    if (!(0.0f < e.alpha138C) || (e.parentFlags & 0x4000000)) {
        EnemyDestroy(e);  // Actor_Destroy 0x00409CC0
        return;
    }
    EnemyProcessHits(e);  // 0x00453EB0
    const uint32_t f = e.flags34;
    e.flags34 = f | 0x80040000u;
    if (!(e.parentFlags & 0x10000000)) {
        if (o) {
            memcpy(e.pos, o->pos, 12);
            memcpy(e.vel, o->vel, 12);
            memcpy(e.acc, o->acc, 12);
            memcpy(e.rot, o->rot, 12);
            memcpy(e.viewPos, o->viewPos, 12);
            e.headPitch = o->headPitch;
            e.headYaw = o->headYaw;
            e.anim.motion[0] = o->anim.motion[0];
            e.anim.motion[1] = o->anim.motion[1];
            e.anim.counter[0] = o->anim.counter[0];
            e.anim.counter[1] = o->anim.counter[1];
        }
    } else {
        e.flags34 = ((f | 0x80040000u) & 0xFFFF0000u) | (f & 0x7EFFu);
    }
    EnemyAnimate(e);  // 0x00454860
    mx::Transform(cam.view, e.body, e.viewPos);  // 0x004A9230(camera of the context) + 0x004A8A80(+0x100) -> +0x70
    SyncLock(e);
    RegisterTarget(&e);  // 0x00405160
}

// 0x004534A0 nodes 0x1C6C (kind 0x12 fading in) / 0x1C7C (kind 9 fading out); false: not a fade node
bool World::Impl::EnemyFadeNode(EnemyTask& e, int base) {
    const float one = 1.0f, zero = 0.0f;
    if (base == 0x1C7C) {
        const float f = e.fade134C - one;
        e.fade134C = f;
        if (f < zero) {
            const float a = e.alpha138C - e.fadeStep1388;
            e.alpha138C = a;
            if (a < zero) e.alpha138C = 0.0f;
        }
        return true;
    }
    if (base == 0x1C6C) {
        if (e.flags1368 & 0x20) {
            bool done = false;
            const float f = e.fade134C - one;
            e.fade134C = f;
            if (f < zero) {
                const float a = e.fadeStep1388 + e.alpha138C;
                e.alpha138C = a;
                if (one < a) done = true;
            }
            if ((e.flags34 & 0x10000000) || done) {
                e.flags136C |= 0x60000000u;
                e.alpha138C = 1.0f;
                e.flags1368 &= ~0x20u;
                e.flags34 &= 0xFFFF7EFFu;
                Sound(0x2225A9);  // 0x0041CFD0
            }
        }
        return true;
    }
    return false;
}

// 0x00454FD0: behaviour 8, the corpse of kinds 0x12 / 3 (0x00456740): blinks for 120 frames (draw bit and attachments
// every other frame), then removed
void World::Impl::EnemyBeh8(EnemyTask& e) {
    if (e.sub == 0) {
        SpawnBloodPool(e);  // 0x00407DA0
        ++e.sub;
        e.flags136C &= 0xDFFFFDFFu;
        e.flags34 |= 0xA0000;
        e.counter1330 = 0x78;
        e.facing1350 = e.rot[1];
    } else if (e.sub != 1) {
        return;
    }
    const bool on = e.counter1330 % 2 == 0;
    if (on) e.anim.drawFlags |= 1;
    else e.anim.drawFlags &= ~1u;
    e.anim.attachHidden = !on;  // 0x00409D10(anim, on)
    if (--e.counter1330 < 1) {
        e.anim.drawFlags &= ~1u;
        e.anim.attachHidden = true;
        e.flags34 |= 0x10000;
        if (e.slot120 != -1) lockOn.Release(e.slot120);
        EnemyDestroy(e);
    }
    const int m = e.anim.motion[0];  // Enemy_CorpseTwitch 0x00454E00
    if (EnemyCorpseHoldUsesRand(m)) {
        const uint32_t site = (m >= 0x3D9 && m <= 0x3E0) ? 0x00454E59u : 0u;
        e.anim.counter[0] = EnemyCorpseHoldTick(m, Rand(site));
    }
}

bool World::Impl::EnemyBehaviourSt6(EnemyTask& e) {
    switch (e.behaviour) {
    case 8: EnemyBeh8(e); return true;
    default: return false;
    }
}

// 0x00419B40 for kinds 9 / 0x12 (translucent with +0x138C; Mdl_SelectAlt with +0x38 & 8) and 0x4D..0x52 (opaque) of
// attachment i (skin: up to four node matrices of the table row, base node +0x10)
bool World::Impl::AttachDrawSt6(EnemyTask& e, int i, uint32_t def, uint32_t tab, int skinNodes) {
    const int k = e.anim.kind;
    if (!(k == 9 || k == 0x12 || (k >= 0x4D && k <= 0x52))) return false;
    const uint32_t row = tab + static_cast<uint32_t>(i) * 0x14;
    const int baseNode = static_cast<int>(exe.U32(row + 0x10)) & 15;
    const bool alt = (e.flags38 & 8) && altModels != 0;
    const float alpha = (k == 9 || k == 0x12) ? e.alpha138C : -1.0f;
    envMapDraw = k != 0x12;  // 0x00418660 (env-map uv) before the draw for kinds 9 / 0x4D..0x52
    WorldDraw* d = Draw(static_cast<int>(exe.U32(def)), e.anim.nodes[baseNode].mv, 8, alpha, alt, baseNode, true);
    envMapDraw = false;
    if (!d) return true;
    d->skinCount = skinNodes;
    for (int s = 0; s < skinNodes && s < 4; ++s) d->skin[s] = e.anim.nodes[static_cast<int>(exe.U32(row + 4 * s)) & 15].mv;
    return true;
}

}  // namespace game