// game::World - enemy kinds / behaviours first reached in stage 2 after frame 22122 (scene 0x1B on).
// Verification: notes/agents/stage2c.md.

#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"

namespace game {

namespace {

// 0x00457780 (behaviour 20): hold motion P word 1 (unless dying) until event flag P word 2 (0x009C7200) is 1, then
// behaviour P byte 3
void Beh20(World::Impl& w, EnemyTask& e) {
    if (e.sub == 0) {
        if (!(e.flags34 & 0x40000000) && e.anim.motion[0] != e.P(1)) w.AnimSetMotion(e, e.P(1));  // 0x00411930
        if (w.runner->flags[e.P(2) & 0xFF] != 1) return;
        ++e.sub;
    } else if (e.sub != 1) {
        return;
    }
    e.behaviour = static_cast<int8_t>(e.record.params[3]);
    e.sub = 0;
}

// 0x00458CE0 (behaviour 31, created without counting as live / attacker): hold motion P word 2 (random start) until
// event flag P byte 4 is set, then counted, wait P word 3 frames, motion P word 4 (start rand % 10), behaviour 1 at
// the end of motion 0x398 (any other motion: at once)
void Beh31(World::Impl& w, EnemyTask& e) {
    switch (e.sub) {
    case 0: {
        e.flags34 |= 0x100;
        const int m = e.P(2);
        const int r = w.Rand(0x00458D1A);
        w.AnimBlendTo(e, m, r % w.MotionTicks(m), 5);  // 0x004119A0
        ++e.sub;
    }
        // fallthrough
    case 1:
        if (w.runner->flags[static_cast<uint8_t>(e.record.params[4])] != 1) return;
        e.flags34 &= ~0x10000u;
        ++w.attackers;    // 0x009C7006
        ++w.liveEnemies;  // 0x009C904A
        ++e.sub;
        e.counter1330 = e.P(3);
        // fallthrough
    case 2: {
        if (0 < --e.counter1330) return;
        const int r = w.Rand(0x00458D93);
        w.AnimBlendTo(e, e.P(4), r % 10, 5);
        ++e.sub;
    }
        // fallthrough
    case 3:
        if (e.anim.motion[0] != 0x398 || w.MotionTicks(0x398) - 1 <= e.anim.tick[0]) {
            e.behaviour = 1;
            e.sub = 0;
            e.flags34 &= ~0x100u;
        }
        return;
    default:
        return;
    }
}

}  // namespace

// Enemy_SetupKind 0x00452FD0 cases 2 / 3: the hands (nodes 5 / 8) hold collision-mesh weapons (flags 0x51, radius 0,
// mesh P word 4); kind 2 also sets 0x136C bit 0x400. The first member of a group plays 0x4D17A9 (kind 2) / 0x1F25A9;
// 0x009C8A74 counts the members (0x00456600 counts them down).
void World::Impl::EnemySetupKind2(EnemyTask& e) {
    if (e.anim.kind == 2) e.flags136C |= 0x400;
    e.flags34 |= 0x400;
    for (int n : { 5, 8 }) {
        EnemyNode& nd = e.anim.nodes[n];
        nd.flags |= 0x51;
        nd.mesh = e.P(4);
        nd.radius = 0;
    }
    uint32_t& count = GroupCount();
    if (count == 0) Sound(e.anim.kind == 2 ? 0x4D17A9 : 0x1F25A9);
    ++count;
    e.c131B = 1;
}

// 0x004048A0 (Hit_TestNodeRecursive 0x00404750 for a node with flags 0x10 and a mesh): the node's world matrix
// (inverse camera x node model-view) becomes the object matrix 0x0059D898, the shot segment of 0x00404AD0 is tested
// in that frame (0x00404F60); the normal goes through the matrix without translation (0x004A8AF0); the entry gets the
// node flags | 0x40 and 0x00404CB0 adds 0x10 (key from the view-space hit point).
bool World::Impl::ShotNodeMesh(int p, const EnemyNode& n, LevelShotHit& out) {
    Mat4 rot = cam.inverse;
    rot.m[12] = rot.m[13] = rot.m[14] = 0.0f;
    float dir[3], origin[3];
    const float zero[3] = { 0, 0, 0 };
    mx::Transform(cam.inverse, zero, origin);
    mx::Transform(rot, player[p].ray.dir, dir);
    const float s[3] = { dir[0] * 1000.0f + origin[0], dir[1] * 1000.0f + origin[1], dir[2] * 1000.0f + origin[2] };
    // the node keeps its model-view of the last evaluation (built with the previous frame's view); the original
    // multiplies it by the current camera matrix 0x009A6040
    const Mat4 m = n.world * camViewPrev * cam.inverse;
    const Mat4 inv = mx::Inverse(m);
    float le[3], ls[3];
    mx::Transform(inv, origin, le);
    mx::Transform(inv, s, ls);
    cac40 = 0;
    if (MeshSegment(ls, le, static_cast<uint32_t>(n.mesh)) == 0) return false;
    const float h[3] = { cac54, cac58, cac50 };
    mx::Transform(m, h, out.pos);
    mx::TransformDir(m, cac64, out.n);
    out.material = cac40;
    float v[3];
    mx::Transform(cam.view, out.pos, v);
    out.key = static_cast<int32_t>(-(v[2] * 10.0f));
    return true;
}

bool World::Impl::EnemyBehaviourSt2c(EnemyTask& e) {
    switch (e.behaviour) {
    case 20: Beh20(*this, e); return true;
    case 31: Beh31(*this, e); return true;
    default: return false;
    }
}

}  // namespace game
