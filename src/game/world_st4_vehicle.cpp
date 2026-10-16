// game::World - vehicle (actor type 0x13, world_st3_vehicle.cpp) motions first reached in stage 4 (0x00440190 table):
// mode 2 = 0x004408A0 (the boss's truck on camera path 0x175, its doors 0x952 / 0x953 swinging open), mode 3 =
// 0x00440AD0 (fading out to 0.7 and back, draw mode 9), mode 4 = 0x00440C20 (path 0x176 with a fire, then the prop
// animations 0x1CD / 0x1CC of prop skeleton 0x15). Verification: notes/agents/stage4b.md.

#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"

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

int AsInt(float f) {
    int i;
    memcpy(&i, &f, 4);
    return i;
}
float AsFloat(int i) {
    float f;
    memcpy(&f, &i, 4);
    return f;
}

// 0x00440130(obj, path, tick): Cam_Evaluate 0x004042D0 into the position and angles (+0x40 / +0x64)
void PathPose(World::Impl& w, EnemyTask& v, int path, int tick) {
    float p[3] = {}, tg[3] = {};
    int roll = 0;
    if (!w.cameraBank.Evaluate(path, static_cast<float>(tick), p, tg, &roll, false)) {
        w.Note("camera path " + std::to_string(path) + " not evaluated (0x004042D0)");
        return;
    }
    memcpy(v.pos, p, 12);
    for (int k = 0; k < 3; ++k) v.rot[k] = static_cast<int>(tg[k]);
}

Mat4 Frame(const EnemyTask& v) {  // translate, RotX(+0x64), RotZ(+0x6C), RotY(+0x68)
    Mat4 m = Mat4::Identity();
    mx::Translate(m, v.pos[0], v.pos[1], v.pos[2]);
    mx::RotX(m, v.rot[0]);
    mx::RotZ(m, v.rot[2]);
    mx::RotY(m, v.rot[1]);
    return m;
}

// 0x004408A0 (mode 2): data {door yaw 0x952, door yaw 0x953, step}
void Motion2(World::Impl& w, EnemyTask& v, int mode, int16_t& state, int* d) {
    const CameraDirector& cd = w.runner->camera;
    switch (state) {
    case 0:
        if (mode == 9) {
            PathPose(w, v, 0x175, 0x168);
            d[0] = w.exe.S16(0x00592744) + 0xC000;
            d[1] = 0xC000 - w.exe.S16(0x00592744);
            if (cd.camId == 0xBD) v.rot[1] += 0x8000;
            state = 4;
            break;
        }
        d[1] = 0xC000;
        d[0] = 0xC000;
        d[2] = 0;
        ++state;
        [[fallthrough]];
    case 1:
        if (cd.tick < 0xBE) {
            PathPose(w, v, 0x175, 0xBE);
        } else if (cd.tick < 0x169) {
            if (cd.tick == 0x15E) w.Sound(0xB16A9);
            PathPose(w, v, 0x175, cd.tick);
        } else {
            PathPose(w, v, 0x175, 0x168);
            ++state;
        }
        if (cd.camId == 0xBB) v.rot[1] += 0x8000;
        break;
    case 2:
        if ((cd.camId == 0xB4 || cd.camId == 0xBC) && 0xFA <= cd.tick) {
            w.Sound(0x2316A9);
            ++state;
        }
        break;
    case 3: {
        const int i = d[2];
        const int a = w.exe.S16(0x005926D0 + static_cast<uint32_t>(i) * 2);
        d[0] = a + 0xC000;
        d[1] = 0xC000 - a;
        d[2] = i + 1;
        if (0x3A < static_cast<unsigned>(i + 1)) ++state;
        break;
    }
    default:
        break;
    }
    const Mat4 f = Frame(v);
    Mat4 m = f;
    mx::Translate(m, Bits(0x41DDA3D7), Bits(0xC1F0A234), Bits(0xC1C6D326));
    mx::RotY(m, d[0]);
    w.Draw(0x952, m);
    m = f;
    mx::Translate(m, Bits(0x41DDA3D7), Bits(0xC1F0A234), Bits(0x41C96F69));
    mx::RotY(m, d[1]);
    w.Draw(0x953, m);
}

// 0x00440AD0 (mode 3): data {counter 0x958.., timer}; fades (ctl +0x18) to 0.7, holds until camera tick 0x1DF + 0xB9
// frames, back to 1, then the task ends; draw mode 9 after the first frame
int Motion3(World::Impl& w, int16_t& state, float& alpha, uint8_t& drawMode, int* d) {
    drawMode = 9;
    switch (state) {
    case 0:
        d[0] = 0x958;
        ++state;
        alpha = 1.0f;
        w.Sound(0x2F1BA9);
        drawMode = 8;
        return 1;
    case 1:
        if (d[0] < 0x95D) {
            ++d[0];
            return 1;
        }
        break;
    case 2: {
        const float a = alpha - F32(w.exe, 0x005644F8);
        alpha = a;
        if (a <= F32(w.exe, 0x005644F4)) {
            alpha = Bits(0x3F333333);
            ++state;
        }
        return 1;
    }
    case 3:
        if (0x1DE < w.runner->camera.tick) {
            d[1] = 0xB9;
            ++state;
        }
        return 1;
    case 4: {
        const int t = d[1];
        d[1] = t - 1;
        if (t == 0) ++state;
        return 1;
    }
    case 5: {
        const float a = alpha + F32(w.exe, 0x005644F8);
        alpha = a;
        if (a < 1.0f) return 1;
        alpha = 1.0f;
        break;
    }
    case 6:
        if (0x958 < d[0]) {
            --d[0];
            return 1;
        }
        break;
    case 7:
        w.ts.KillCurrent();  // Task_Kill 0x004A7040
        return 2;
    default:
        drawMode = 8;
        return 1;
    }
    ++state;
    return 1;
}

// 0x00440C20 (mode 4): data = prop animation {skeleton 0x15, motion 0x1CD, frame, prevFrame} + [0x15] fire model
void Motion4(World::Impl& w, EnemyTask& v, int mode, int16_t& state, int16_t& model, int* d) {
    const CameraDirector& cd = w.runner->camera;
    const int tick = cd.tick;
    const int end = static_cast<int>(w.exe.U32(0x00577310));
    auto offset = [&] {
        v.pos[0] = v.pos[0] - F32(w.exe, 0x00564508);
        v.pos[1] = v.pos[1] - F32(w.exe, 0x00564504);
        v.pos[2] = v.pos[2] + F32(w.exe, 0x00564500);
    };
    auto prop = [&](uint32_t x, uint32_t y, uint32_t z) {
        if (!w.MotionResident(d[1])) return;  // 0x009A37E4[motion * 4] == 2
        Mat4 m = Mat4::Identity();
        mx::Translate(m, Bits(x), Bits(y), Bits(z));
        PropAnim a;
        a.skeleton = d[0];
        a.motion = d[1];
        a.frame = d[2];
        a.prevFrame = d[3];
        if (const Motion* mo = w.PropMotion(a.motion)) {
            for (const PropDraw& p : PropPose(w.PropSkel(a.skeleton), *mo, a, m)) {  // 0x0040DD90
                if (p.approx) w.Note("prop skeleton big-angle interpolation (0x00412750) approximated");
                w.Draw(p.model, p.world);
            }
        }
        d[2] = a.frame;
        d[3] = a.prevFrame;
    };
    auto next = [&] {
        d[2] = 0;
        d[3] = 0;
        ++state;
    };
    switch (state) {
    case 0:
        d[0] = 0x15;
        d[1] = 0x1CD;
        d[0x15] = 0x1AF0;
        if (mode == 7) {
            PathPose(w, v, 0x176, end);
            offset();
            state = 2;
            break;
        }
        w.Sound(0x1A1BA9);
        ++state;
        [[fallthrough]];
    case 1: {
        if (w.shakeTimer < 0x1E) w.shakeTimer = 0x28;  // 0x009C8E8C
        PathPose(w, v, 0x176, tick);
        if (tick == end - 0x1E) {
            w.Sound(0x1B1BA9);
            w.Sound(0x1C1BA9);
        }
        if (end <= tick) ++state;
        offset();
        float s;
        if (tick < 100) s = static_cast<float>(tick) * F32(w.exe, 0x0055D230);
        else if (tick < 0x96) s = 10.0f;
        else s = (static_cast<float>(end - tick) / static_cast<float>(end - 0x96)) * F32(w.exe, 0x005644FC) + F32(w.exe, 0x004C49C0);
        Mat4 m = Mat4::Identity();
        mx::Translate(m, Bits(0xC1C80000), Bits(0x422C0000), Bits(0xC4CB2000));
        mx::RotY(m, w.cam.yaw);  // 0x009A60D0[context]
        mx::Translate(m, 0.0f, s + s, 0.0f);
        mx::Scale(m, 30.0f, s, 1.0f);
        w.Draw(d[0x15], m);
        if (0x1AF9 < ++d[0x15]) d[0x15] = 0x1AF0;
        break;
    }
    case 2:
        if (cd.camId == 0xB9 && 0xE7 <= tick) next();
        break;
    case 3:
        if (tick == 0x1CC) {
            w.shakeTimer = 0x28;
            w.Sound(0x1E1BA9);
            ++state;
        }
        [[fallthrough]];
    case 4:
        if (tick < 600) {
            prop(0x4397AC57, 0x4229F0D8, 0xC4D2B0F6);
            if (d[2] < w.MotionTicks(d[1]) - 2 && 0x1D5 < tick) ++d[2];
        } else if (799 < tick) {
            d[1] = 0x1CC;
            next();
        }
        break;
    case 5:
        if (tick == 0x3C0) {
            w.shakeTimer = 0x28;
            w.Sound(0x1E1BA9);
            ++state;
        }
        [[fallthrough]];
    case 6:
        prop(0x43A364F4, 0x4229F0D8, 0xC4F7DE46);
        if (d[2] < w.MotionTicks(d[1]) - 2 && 0x3CA <= tick) ++d[2];
        break;
    default:
        break;
    }
    if (tick == 0x38E) model = 0x954;
}

// 0x00441000 (modes 5 / 8; mode 8 = boss mode's stage-4 scene 29 start, at the path's end): path 0x177 with the fire,
// then the prop animations 0x1CC / 0x1CD of prop skeleton 0x18 at two places (turned 0x8000)
void Motion5(World::Impl& w, EnemyTask& v, int mode, int16_t& state, int* d) {
    const CameraDirector& cd = w.runner->camera;
    const int tick = cd.tick;
    const int end = static_cast<int>(w.exe.U32(0x00577314));
    auto offset = [&] {
        v.pos[0] = v.pos[0] + F32(w.exe, 0x00564510);
        v.pos[2] = v.pos[2] + F32(w.exe, 0x0056450C);
    };
    auto prop = [&](uint32_t x, uint32_t y, uint32_t z) {
        if (!w.MotionResident(d[1])) return;  // 0x009A37E4[motion * 4] == 2
        Mat4 m = Mat4::Identity();
        mx::Translate(m, Bits(x), Bits(y), Bits(z));
        mx::RotY(m, 0x8000);
        PropAnim a;
        a.skeleton = d[0];
        a.motion = d[1];
        a.frame = d[2];
        a.prevFrame = d[3];
        if (const Motion* mo = w.PropMotion(a.motion)) {
            for (const PropDraw& p : PropPose(w.PropSkel(a.skeleton), *mo, a, m)) {  // 0x0040DD90
                if (p.approx) w.Note("prop skeleton big-angle interpolation (0x00412750) approximated");
                w.Draw(p.model, p.world);
            }
        }
        d[2] = a.frame;
        d[3] = a.prevFrame;
    };
    switch (state) {
    case 0:
        d[0] = 0x18;
        d[1] = 0x1CC;
        d[0x15] = 0x1AF0;
        if (mode == 8) {
            PathPose(w, v, 0x177, end);
            offset();
            state = 2;
            return;
        }
        w.Sound(0x1A1BA9);
        ++state;
        [[fallthrough]];
    case 1: {
        if (w.shakeTimer < 0x1E) w.shakeTimer = 0x28;  // 0x009C8E8C
        PathPose(w, v, 0x177, tick);
        if (tick == end - 0x1E) {
            w.Sound(0x1B1BA9);
            w.Sound(0x1C1BA9);
        }
        if (end <= tick) ++state;
        offset();
        float s;
        if (tick < 100) s = static_cast<float>(tick) * F32(w.exe, 0x0055D230);
        else if (tick < 0x96) s = 10.0f;
        else s = (static_cast<float>(end - tick) / static_cast<float>(end - 0x96)) * F32(w.exe, 0x005644FC) + F32(w.exe, 0x004C49C0);
        Mat4 m = Mat4::Identity();
        mx::Translate(m, Bits(0xC3B18000), Bits(0x422C0000), Bits(0xC4CB2000));
        mx::RotY(m, w.cam.yaw);  // 0x009A60D0[context]
        mx::Translate(m, 0.0f, s + s, 0.0f);
        mx::Scale(m, 30.0f, s, 1.0f);
        w.Draw(d[0x15], m);
        if (0x1AF9 < ++d[0x15]) d[0x15] = 0x1AF0;
        return;
    }
    case 2:
        if (cd.camId == 0xC1 && 0x1C2 < tick) {
            d[2] = 0;
            d[3] = 0;
            ++state;
        }
        return;
    case 3:
        if (tick == 0x208) {
            w.shakeTimer = 0x28;
            w.Sound(0x1E1BA9);
            ++state;
        }
        [[fallthrough]];
    case 4:
        if (tick < 0x28A) {
            prop(0xC3684FDF, 0x4229F4F1, 0xC4E4ADC3);
            if (d[2] < w.MotionTicks(d[1]) - 2 && 0x211 < tick) ++d[2];
        } else if (799 < tick) {
            d[1] = 0x1CD;
            d[2] = 0;
            d[3] = 0;
            ++state;
        }
        return;
    case 5:
        if (tick == 0x3CA) {
            w.shakeTimer = 0x28;
            w.Sound(0x1E1BA9);
            ++state;
        }
        [[fallthrough]];
    case 6:
        prop(0xC3BADEFA, 0x4229F4F1, 0xC5034000);
        if (d[2] < w.MotionTicks(d[1]) - 2 && 0x3D3 < tick) ++d[2];
        return;
    default:
        return;
    }
}

}  // namespace

int World::Impl::VehicleMotionSt4(EnemyTask& v, int mode, int16_t& state, float& alpha, int16_t& model, uint8_t& drawMode,
                                  int* data) {
    (void)AsInt;
    (void)AsFloat;
    switch (mode) {
    case 2:
    case 9:
        Motion2(*this, v, mode, state, data);
        return 1;
    case 3:
        return Motion3(*this, state, alpha, drawMode, data);
    case 4:
    case 7:
        Motion4(*this, v, mode, state, model, data);
        return 1;
    case 5:
    case 8:
        Motion5(*this, v, mode, state, data);
        return 1;
    default:
        return 0;
    }
}

}  // namespace game
