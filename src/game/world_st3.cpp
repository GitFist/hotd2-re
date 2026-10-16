// game::World - stage 3 (st3evtbl.bin): shared pieces of the stage-3 objects. notes/agents/stage3.md describes the
// capture (build\scene_capture_auto_st3.txt), the stage start (world_test --stage-start 29736) and what is verified.

#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"

namespace game {

namespace {

float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

}  // namespace

// 0x00484FF0 (actor type 0x25 draw) extras by record P s16 6, after the skeleton (0x00411090):
//   1 / 2: the boat 0x1A37 moored at a fixed place; 3: the boat on camera path +0x135C at the camera tick with its two
//   bow waves (0x24A + frame % 22, the second mirrored); 4: model 0x10DF while camera 0x93 plays.
bool World::Impl::Actor25Extra(EnemyTask& e, int extra, int path) {
    (void)e;
    Mat4 m = Mat4::Identity();
    switch (extra) {
    case 1:
        mx::Translate(m, Bits(0xC4AAE000), Bits(0xC1880000), Bits(0xC4E6A99A));
        mx::RotY(m, 0x8000);
        Draw(0x1A37, m);
        return true;
    case 2:
        mx::Translate(m, Bits(0x43560000), Bits(0xC1880000), Bits(0xC507C000));
        mx::RotY(m, 0xAAAA);
        Draw(0x1A37, m);
        return true;
    case 3: {
        float pos[3] = {}, tg[3] = {};
        int roll = 0;
        if (!cameraBank.Evaluate(path, static_cast<float>(runner->camera.tick), pos, tg, &roll, false)) {  // 0x004042D0
            Note("actor 0x25 extra 3: camera path not evaluated");
            return true;
        }
        int ang[3];
        for (int k = 0; k < 3; ++k) ang[k] = static_cast<int>(tg[k]);  // __ftol
        mx::Translate(m, pos[0], pos[1], pos[2]);
        mx::RotZ(m, ang[2]);
        mx::RotY(m, ang[1]);
        mx::RotX(m, ang[0]);
        Draw(0x1A37, m);
        Mat4 r = Mat4::Identity();
        mx::RotZ(r, ang[2]);
        mx::RotY(r, ang[1]);
        mx::RotX(r, ang[0]);
        int ax = 0, ay = 0, az = 0;
        MatrixToAnglesYAxis(r, &ax, &ay, &az);  // 0x00401AE0
        Mat4 w = Mat4::Identity();
        mx::Translate(w, pos[0], Bits(0xC1C80000), pos[2]);
        mx::RotY(w, ay);
        mx::Translate(w, Bits(0x3FD9999A), 0.0f, Bits(0x41A00000));
        const int model = static_cast<int>(seedCounter % 22u) + 0x24A;
        Draw(model, w);
        mx::Translate(w, Bits(0xC059999A), 0.0f, 0.0f);
        mx::Scale(w, -1.0f, 1.0f, 1.0f);
        Draw(model, w);
        return true;
    }
    case 4:
        if (runner->camera.camId != 0x93) return true;
        mx::Translate(m, Bits(0xC41F0000), Bits(0x422D6666), Bits(0xC46E0000));
        mx::RotX(m, 0xC000);
        Draw(0x10DF, m);
        return true;
    default:
        return false;
    }
}

// 0x00449020 variant 2: the walk is kept inside the box x in [P float 2, P float 3], z in [P float 4, P float 5]; at an
// x edge the yaw turns by 0x100 (away from the edge), at a z edge (when x did not clamp) too
void World::Impl::Actor20BoxWalk(EnemyTask& a) {
    auto pf = [&](int off) {
        const int32_t v = a.record.ParamS32(off);
        float f;
        memcpy(&f, &v, 4);
        return f;
    };
    bool clamped = false;
    if (!(a.pos[0] < pf(0xC))) {
        a.pos[0] = pf(0xC);
        a.rot[1] += a.rot[1] < 0xC000 ? -0x100 : 0x100;
        clamped = true;
    } else if (a.pos[0] <= pf(8)) {
        a.pos[0] = pf(8);
        a.rot[1] += a.rot[1] > 0x3FFF ? 0x100 : -0x100;
        clamped = true;
    }
    if (!(a.pos[2] < pf(0x14))) {
        a.pos[2] = pf(0x14);
        if (!clamped) a.rot[1] += a.rot[1] < 0x8000 ? -0x100 : 0x100;
    } else if (!(pf(0x10) < a.pos[2])) {
        a.pos[2] = pf(0x10);
        if (!clamped) a.rot[1] += a.rot[1] < 0x8000 ? 0x100 : -0x100;
    }
}

}  // namespace game
