// game::World - enemy behaviour 37 (0x0045B380) and the object it carries (task 0x00442740 -> 0x005926A8[kind]):
// held between the hands (nodes 4 and 7) until the holder dies (drop 0x00442950 -> tumbling fall 0x00444D80) or the
// holder's motion list reaches the throw tick (0x00442B90, not ported); shot to pieces -> debris animation 0x00444EE0.
// Object definitions 0x005644D8[type]: {s16 skeleton, s16 motion, f32 radius +0x124, f32 +0x128, f32 half sizes
// +0xC / +0x10 / +0x14, u32 hit sound +0x18, u32 break sound +0x1C, s16 hp +0x20, s16 models by hp +0x22..}.
// See notes/agents/stage1b.md. CONFIRMED / CODE as in world_st1b.cpp.

#include <cmath>
#include <cstring>
#include <set>

#include "game/mstack.h"
#include "game/shoot_camera.h"  // AngleBetween (0x00401D70)
#include "game/world_held.h"
#include "game/world_internal.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

// 8-byte constants (fld / fcomp qword ptr): 0x00564520 = -15.0, 0x00564528 = -40.0 (F32 read their low half: 0)
double F64(const ExeImage& exe, uint32_t va) {
    double d = 0;
    exe.Read(va, &d, 8);
    return d;
}

float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

int16_t EvS16(const EventMemory& mem, uint32_t a) {
    uint32_t w = 0;
    mem.Read(a & ~3u, &w);
    return static_cast<int16_t>((a & 2) ? (w >> 16) : (w & 0xFFFF));
}

int32_t EvS32(const EventMemory& mem, uint32_t a) {
    uint32_t w = 0;
    mem.Read(a, &w);
    return static_cast<int32_t>(w);
}

float EvF32(const EventMemory& mem, uint32_t a) {
    uint32_t w = 0;
    mem.Read(a, &w);
    return Bits(w);
}

// 0x0045CA30(&flags, tick)
void ScriptFlags(uint32_t& f, int v) {
    if (v == -3) f |= 0x100;
    else if (v != -2) f |= 0x2400;
    else f = (f & ~0x2000u) | 0x400;
}

// fpatan result * 65536 / 2pi (fmul qword at 24-bit precision: rounded to float), __ftol, movsx
int AngS16(double a) {
    const float f = static_cast<float>(a * mx::kToAngle);
    return static_cast<int16_t>(static_cast<int>(f));
}

// 0x00401720(dx, dy, dz, &a, &b): a = atan2(-dz, dx); b = atan2(dy, dx / cos a) (or -dz / sin a near +-90 degrees)
void DirAngles401720(float dx, float dy, float dz, int* a, int* b) {
    const int ia = AngS16(std::atan2(-static_cast<double>(dz), static_cast<double>(dx)));
    *a = ia;
    const float ang = static_cast<float>(static_cast<double>(ia) * mx::kToRad);
    float v;
    if (((ia + 0x2000) & 0x4000) == 0) v = static_cast<float>(static_cast<double>(dx) / std::cos(static_cast<double>(ang)));
    else v = -static_cast<float>(static_cast<double>(dz) / std::sin(static_cast<double>(ang)));
    *b = AngS16(std::atan2(static_cast<double>(dy), static_cast<double>(v)));
}

// 0x004A9FD0: translation row of the top matrix = v
void SetTranslation(Mat4& m, const float v[3]) {
    m.m[12] = v[0];
    m.m[13] = v[1];
    m.m[14] = v[2];
}

}  // namespace

// HeldTask: world_held.h

namespace {

uint32_t Def(const World::Impl& w, int type) { return w.exe.U32(0x005644D8 + 4 * static_cast<uint32_t>(type)); }

// the holder / civilian pointers are only followed while their task is alive
EnemyTask* Alive(World::Impl& w, EnemyTask* e, uint32_t serial) {
    if (!e) return nullptr;
    for (Task* t : w.ts.Live())
        if (t->serial == serial) return t->data.get() == e ? e : nullptr;
    return nullptr;
}

// 0x00442EB0(holder, dy, out): hands midpoint (nodes 4 / 7 model-view matrices, 4 units down) and the angles of the
// line between them; out = {x, y, z, roll, a, b} (model-view space). The node matrices are the holder's model-view
// chain (node +0x28, EnemyNode::mv: bit-exact; world x view rounded differently and turned thrown objects by a unit)
struct Grip {
    float p[3];
    int roll, a, b;
};

Grip HandsGrip(World::Impl& w, EnemyTask& h, float dy) {
    const Mat4& mv4 = h.anim.nodes[4].mv;
    const Mat4& mv7 = h.anim.nodes[7].mv;
    float pa[3], pb[3];
    {
        Mat4 m = mv4;
        mx::Translate(m, 0.0f, dy, 0.0f);
        mx::GetTranslation(m, pa);
        m = mv7;
        mx::Translate(m, 0.0f, dy, 0.0f);
        mx::GetTranslation(m, pb);
    }
    Grip g;
    DirAngles401720(pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2], &g.a, &g.b);
    auto up = [&](const Mat4& node, float out[3]) {
        Mat4 r = node;
        r.m[3] = r.m[7] = r.m[11] = 0.0f;
        r.m[12] = r.m[13] = r.m[14] = 0.0f;
        r.m[15] = 1.0f;
        Mat4 m = Mat4::Identity();
        mx::RotZ(m, -g.b);
        mx::RotY(m, -g.a);
        mx::MulLeft(m, r);  // 0x004A92A0
        const float y[3] = { 0.0f, 1.0f, 0.0f };
        mx::Transform(m, y, out);
    };
    float u4[3], u7[3];
    up(mv4, u4);
    up(mv7, u7);
    const int s = AngS16(std::atan2(static_cast<double>(u7[2]), static_cast<double>(u7[1])));
    double kneg = 0;
    w.exe.Read(0x0055CB20, &kneg, 8);  // -65536 / 2pi
    const int e = static_cast<int>(static_cast<float>(std::atan2(static_cast<double>(u4[2]), static_cast<double>(u4[1])) * kneg));
    int d = (-s - e) & 0xFFFF;
    if (d >= 0x8000) d -= 0x10000;
    g.roll = d / 2 + 0x8000 + s;
    g.p[0] = (pb[0] + pa[0]) * 0.5f;
    g.p[1] = (pb[1] + pa[1]) * 0.5f;
    g.p[2] = (pb[2] + pa[2]) * 0.5f;
    return g;
}

// 0x004459C0: the view position (+0x70) with the radius projects inside the screen
bool HeldOnScreen(const World::Impl& w, const HeldTask& k) {
    const float vx = k.viewPos[0], vy = k.viewPos[1], vz = k.viewPos[2], r = k.radius, f = w.focal;
    if (0.0f <= vz) return false;
    const float ex = vx <= 0.0f ? -vx - r : r - vx;
    const float ey = vy <= 0.0f ? -vy - r : r - vy;
    const float s = f / vz;
    const float a = s * ex, b = s * ey;
    const float c = -((f * vx) / vz), d = -((f * vy) / vz);
    if ((a < 320.0f || c < 320.0f) && (-320.0f < a || -320.0f < c) && (b < 240.0f || d < 240.0f)) {
        if (b <= -240.0f && d <= -240.0f) return false;
        return true;
    }
    return false;
}

void HeldDestroy(World::Impl& w, HeldTask& k) {  // Actor_Destroy 0x00409CC0
    k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
    w.ts.KillCurrent();
}

void TaskHeldDebris(Task& t);

// 0x004423F0: this frame's shots: hit counter, hp, model by hp + hit sound; at hp 0 the break (debris animation, or
// type 2's fly-off 0x00444F70) and the break sound. top = the object's model-view matrix.
void HeldShots(World::Impl& w, HeldTask& k, const Mat4& top) {
    if (!(k.flags34 & 8)) return;
    const uint32_t def = Def(w, k.type);
    int p;
    const uint32_t f6 = k.flags34 & 6;
    if (f6 == 2) p = 0;
    else if (f6 == 4) p = 1;
    else p = w.Rand(0x0044242D) & 1;  // (signed % 2 of a non-negative value)
    if (w.GlobalInt(0x009A5C48, 0) == 0) ++w.player[p].hits;
    const uint32_t f = k.flags34;
    if (!(f & 0x100)) --k.hp;
    k.flags34 = f & ~0xEu;
    if (k.hp == 0) {
        if (k.type == 2) {
            HeldType2Break(w, k);  // world_st4_held.cpp
        } else {
            k.anim.skeleton = w.exe.S16(def);
            k.anim.motion = w.exe.S16(def + 2);
            k.anim.frame = 0;
            k.anim.prevFrame = 0;
            Task* cur = w.ts.Current();
            cur->fn = TaskHeldDebris;
            k.fn = 0x00444EE0;
            Mat4 m = top;
            mx::MulRight(m, w.cam.inverse);  // 0x004A9230(camera) + 0x004A92A0(top): world matrix
            mx::GetTranslation(m, k.pos);
            const float zero[3] = {};
            SetTranslation(m, zero);
            const float x[3] = { 1.0f, 0.0f, 0.0f };
            float d[3];
            mx::Transform(m, x, d);
            DirAngles401720(d[0], d[1], d[2], &k.rot[1], &k.rot[2]);
        }
        if (k.slot121 >= 0) w.targetTaken[k.slot121] = 0;
        w.Sound(w.exe.U32(def + 0x1C));
        EnemyTask* civ = Alive(w, k.civ, k.civSerial);
        if (civ && !(civ->flags34 & 0x4000000) && civ->civilian) static_cast<CivTask*>(civ)->c.altScript = 0;
    } else if (!(f & 0x100)) {
        k.model = w.exe.S16(def + 0x20 + 2 * static_cast<uint32_t>(k.hp));
        w.Sound(w.exe.U32(def + 0x18));
    }
}

// 0x00444280: floor contact of the tumbling object (box half sizes c+0xC / +0x10 / +0x14 of the definition): the
// lowest end (or the bottom centre) below the floor lifts it out, bounces the velocity (xz * 0.895, y * -0.3), damps
// the spin (or re-aims the spin axis from the end's motion) and rolls it. 1 = contact.
int HeldFloor(World::Impl& w, HeldTask& k) {
    const ExeImage& exe = w.exe;
    const uint32_t def = Def(w, k.type);
    const float h0C = F32(exe, def + 0xC), h10 = F32(exe, def + 0x10), h14 = F32(exe, def + 0x14);
    int corner = 0;
    int mat = 0;
    float floor = w.FloorHeight(k.pos[0], k.pos[1] + F32(exe, 0x004C43B0), k.pos[2], &mat);
    if (w.cac40 == 5 || w.cac40 == 0x37) {
        w.cac40 = 0;
        floor = w.ground;
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], floor, k.pos[2]);
    Mat4 l40 = m;
    mx::Translate(m, 0.0f, k.pos[1] - floor, 0.0f);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    const float ex[3] = { h14, 0.0f, 0.0f }, exn[3] = { -h14, 0.0f, 0.0f };
    float a0[3], c4[3], d0[3], t74[3];
    mx::Transform(m, ex, a0);
    mx::Transform(m, exn, c4);
    float lever = -h14;  // local_d4
    mx::RotAxis(m, k.axis, k.spin[2]);
    mx::Transform(m, ex, d0);
    mx::Transform(m, exn, t74);
    float mb8[3] = { d0[0] - a0[0], d0[1] - a0[1], d0[2] - a0[2] };     // motion of the +x end
    float me0[3] = { t74[0] - c4[0], t74[1] - c4[1], t74[2] - c4[2] };  // motion of the -x end
    if (w.cac40 == 0) {
        m = l40;
    } else {
        const float up[3] = { 0.0f, 1.0f, 0.0f };
        const int tilt = AngleBetween(w.cac64, up);  // 0x00401D70
        const int yaw = AngS16(std::atan2(static_cast<double>(w.cac64[0]), static_cast<double>(w.cac64[2])));
        m = l40;
        mx::RotY(m, yaw);
        mx::RotX(m, tilt);
        l40 = m;
    }
    m = mx::Inverse(m);  // 0x004A8D20(0)
    float lp[3];
    mx::Transform(m, k.pos, lp);
    {
        float t[3];
        mx::Transform(m, a0, t);
        memcpy(a0, t, 12);
        mx::Transform(m, c4, t);
        memcpy(c4, t, 12);
    }
    int ya = 0, pb = 0;
    DirAngles401720(a0[0] - c4[0], a0[1] - c4[1], a0[2] - c4[2], &ya, &pb);
    m = Mat4::Identity();
    mx::RotY(m, ya);
    mx::RotZ(m, pb);
    SetTranslation(m, lp);
    mx::Translate(m, 0.0f, -h0C, 0.0f);
    float p94[3], ac[3], e88[3];
    mx::GetTranslation(m, p94);
    memcpy(ac, p94, 12);
    SetTranslation(m, a0);
    mx::Translate(m, 0.0f, -h10, 0.0f);
    mx::GetTranslation(m, t74);
    memcpy(e88, t74, 12);
    SetTranslation(m, c4);
    mx::Translate(m, 0.0f, -h10, 0.0f);
    mx::GetTranslation(m, c4);
    if (c4[1] <= t74[1]) {
        if (c4[1] < p94[1]) {
            memcpy(ac, c4, 12);
            memcpy(p94, me0, 12);
            corner = 1;
        }
    } else if (t74[1] < p94[1]) {
        memcpy(ac, e88, 12);
        memcpy(p94, mb8, 12);
        corner = 1;
        lever = h14;
    }
    if (!(ac[1] <= 0.0f)) return 0;
    m = l40;
    SetTranslation(m, k.pos);
    mx::Translate(m, 0.0f, -ac[1], 0.0f);
    mx::GetTranslation(m, k.pos);
    m = mx::Inverse(m);
    float v[3];
    mx::TransformDir(m, k.vel, v);
    m = l40;
    const float kxz = F32(exe, 0x0056453C), ky = F32(exe, 0x0055D2C4);
    v[0] = v[0] - v[0] * kxz;
    v[2] = v[2] - v[2] * kxz;
    v[1] = v[1] * ky;
    const float va0[3] = { v[0], v[1], v[2] };
    mx::TransformDir(m, v, k.vel);
    float vy = k.vel[1];
    if (vy < 0.0f) vy = vy * -1.0f;
    if (vy < F32(exe, 0x004E3100)) k.vel[1] = 0.0f;
    if (corner == 0) {
        k.spin[0] = static_cast<int>(static_cast<float>(k.spin[0]) * F32(exe, 0x005643E0));
        k.spin[2] = static_cast<int>(static_cast<float>(k.spin[2]) * F32(exe, 0x00564534));
    } else {
        m = l40;
        m = mx::Inverse(m);
        float e[3];
        mx::TransformDir(m, p94, e);
        float y = e[1];
        if (y < 0.0f) y = e[1] * ky;
        const float s[3] = { e[0] + va0[0], y + va0[1], e[2] + va0[2] };
        m = Mat4::Identity();
        mx::RotX(m, -k.rot[0]);
        mx::RotY(m, -k.rot[1]);
        mx::RotZ(m, -k.rot[2]);
        float l[3];
        mx::Transform(m, s, l);
        k.axis[0] = 0.0f;
        k.axis[1] = -(l[2] * lever);
        k.axis[2] = l[1] * lever;
        k.spin[0] = static_cast<int>(static_cast<float>(k.spin[0]) * F32(exe, 0x005643E0));
        const float len = std::sqrt(l[2] * l[2] + l[1] * l[1]);
        const int a = AngS16(std::atan2(static_cast<double>(len), static_cast<double>(h14)));
        k.spin[2] = static_cast<int>(static_cast<float>(a) * F32(exe, 0x00564538));
    }
    if (0.0f < t74[1] || 0.0f < c4[1]) {
        m = l40;
        float wc[3];
        mx::Transform(m, ac, wc);
        m = Mat4::Identity();
        mx::RotX(m, -k.rot[0]);
        mx::RotY(m, -k.rot[1]);
        mx::RotZ(m, -k.rot[2]);
        const float y1[3] = { 0.0f, 1.0f, 0.0f };
        float n[3], q[3];
        mx::Transform(m, y1, n);
        const float dlt[3] = { k.pos[0] - wc[0], k.pos[1] - wc[1], k.pos[2] - wc[2] };
        mx::Transform(m, dlt, q);
        const float cr[3] = { q[2] * n[1] - q[1] * n[2], q[0] * n[2] - q[2] * n[0], q[1] * n[0] - q[0] * n[1] };
        int ang = AngleBetween(n, q);
        if (0x100 < ang) {
            m = Mat4::Identity();
            mx::RotAxis(m, k.axis, k.spin[2]);
            const int st = corner == 0 ? -(ang / 256) : ang / 256;
            mx::RotAxis(m, cr, st);
            const float x1[3] = { 1.0f, 0.0f, 0.0f };
            float e[3];
            mx::Transform(m, x1, e);
            k.axis[2] = e[1];
            k.axis[1] = -e[2];
            k.axis[0] = 0.0f;
            k.spin[2] = AngleBetween(x1, e);
        }
    }
    const float ang = static_cast<float>(static_cast<double>(pb) * mx::kToRad);
    float roll = static_cast<float>(std::cos(static_cast<double>(ang)) * static_cast<double>(k.spin[0]));
    roll = roll * (corner ? h10 : h0C);
    roll = roll * F32(exe, 0x00564530);
    m = l40;
    mx::RotY(m, ya);
    mx::RotZ(m, pb);
    const float fz[3] = { 0.0f, 0.0f, roll };
    float dz[3];
    mx::TransformDir(m, fz, dz);
    k.pos[0] = dz[0] + k.pos[0];
    k.pos[1] = dz[1] + k.pos[1];
    k.pos[2] = dz[2] + k.pos[2];
    if (corner) w.Note("held object corner contact (0x00444280) reached (not verified)");
    else w.Note("held object floor contact (0x00444280) reached (not verified)");
    return 1;
}

// 0x00444D80: falling, tumbling about the axis; removed off screen (0x004459C0)
void TaskHeldFall(Task& t) {
    World::Impl& w = *CurrentWorld();
    HeldTask& k = t.As<HeldTask>();
    k.pos[0] = k.vel[0] + k.pos[0];
    k.pos[1] = k.pos[1] + k.vel[1];
    k.pos[2] = k.vel[2] + k.pos[2];
    k.vel[1] = k.acc[1] + k.vel[1];
    Mat4 m = Mat4::Identity();
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    mx::RotAxis(m, k.axis, k.spin[2]);
    mx::RotX(m, k.spin[0]);
    mx::MatrixToAngles(m, &k.rot[0], &k.rot[1], &k.rot[2]);  // 0x004019E0
    HeldFloor(w, k);
    memcpy(k.lastView, k.pos, 12);
    Mat4 mv = w.cam.view;
    mx::Translate(mv, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(mv, k.rot[2]);
    mx::RotY(mv, k.rot[1]);
    mx::RotX(mv, k.rot[0]);
    {
        Mat4 wm = Mat4::Identity();
        mx::Translate(wm, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotZ(wm, k.rot[2]);
        mx::RotY(wm, k.rot[1]);
        mx::RotX(wm, k.rot[0]);
        w.Draw(k.model, wm);
    }
    mx::GetTranslation(mv, k.viewPos);
    if (HeldOnScreen(w, k)) {
        w.RegisterTarget(&k);
        HeldShots(w, k, mv);
        return;
    }
    HeldDestroy(w, k);
}

// 0x00444EE0: debris animation (definition skeleton / motion) until its last tick
void TaskHeldDebris(Task& t) {
    World::Impl& w = *CurrentWorld();
    HeldTask& k = t.As<HeldTask>();
    if (w.MotionResident(k.anim.motion)) {  // 0x009A37E4[motion * 4] == 2
        if (k.rot[2] != 0) k.rot[2] = k.rot[2] / 2;
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotZ(m, k.rot[2]);
        if (const Motion* mo = w.PropMotion(k.anim.motion)) {
            for (const PropDraw& d : PropPose(w.PropSkel(k.anim.skeleton), *mo, k.anim, m)) {  // 0x0040DD90
                if (d.approx) w.Note("prop skeleton big-angle interpolation (0x00412750) approximated");
                w.Draw(d.model, d.world);
            }
        }
    }
    if (w.MotionTicks(k.anim.motion) - 1 <= ++k.anim.frame) HeldDestroy(w, k);
}

// 0x00442950(obj, grip): dropped by its dead holder: world position / angles from the held matrix, a push forward or
// back (0.15) by the holder's facing and the grip direction, spin about z
void HeldDrop(World::Impl& w, HeldTask& k, const Mat4& top, const Grip& g, EnemyTask& h) {
    k.flags34 &= ~0x100u;
    Mat4 m = top;
    mx::MulRight(m, w.cam.inverse);
    mx::GetTranslation(m, k.pos);
    memcpy(k.lastView, k.viewPos, 12);
    mx::MatrixToAngles(m, &k.rot[0], &k.rot[1], &k.rot[2]);  // 0x004019E0
    Mat4 c = w.cam.inverse;
    mx::RotY(c, g.a);
    mx::RotZ(c, g.b);
    mx::RotX(c, g.roll);
    const float z1[3] = { 0.0f, 0.0f, 1.0f };
    float d[3];
    mx::TransformDir(c, z1, d);
    int pitch = 0, yaw = 0;
    mx::DirToAngles(d[0], d[1], d[2], &pitch, &yaw);  // 0x004016B0
    Mat4 r = Mat4::Identity();
    mx::RotX(r, h.rot[0]);
    mx::RotZ(r, h.rot[2]);
    mx::RotY(r, h.rot[1]);
    const float k15 = Bits(0x3E19999A);
    if (!(h.flags34 & 0x1000000) && pitch < 1) {
        const float f[3] = { 0.0f, 0.0f, k15 };
        mx::Transform(r, f, k.vel);
        k.spin[0] = -0x200;
        k.spin[1] = 0;
        k.spin[2] = -0x100;
    } else {
        const float f[3] = { 0.0f, 0.0f, -k15 };
        mx::Transform(r, f, k.vel);
        k.spin[0] = 0x200;
        k.spin[1] = 0;
        k.spin[2] = 0x100;
    }
    k.axis[0] = 0.0f;
    k.axis[1] = 0.0f;
    k.axis[2] = 1.0f;
    w.ts.Current()->fn = TaskHeldFall;
    k.fn = 0x00444D80;
    if (k.slot121 >= 0) {
        w.targetTaken[k.slot121] = 0;
        k.slot121 = -1;
    }
}

// 0x00443DC0 (0x005926A8[5]): thrown at the players: flight with gravity, tumbling about the axis (as the fall), the
// floor contact (0x00444280, sound 0x1916A9); close in front of the view it sticks there (0x00444160) and hits the
// player in front (0x00415300 kind 7); after a floor contact the vertical speed is re-aimed at the view
void TaskHeldThrown(Task& t) {
    World::Impl& w = *CurrentWorld();
    HeldTask& k = t.As<HeldTask>();
    const ExeImage& exe = w.exe;
    k.pos[0] = k.pos[0] + k.vel[0];
    k.pos[1] = k.pos[1] + k.vel[1];
    k.pos[2] = k.pos[2] + k.vel[2];
    k.vel[1] = k.vel[1] + k.acc[1];
    Mat4 r = Mat4::Identity();
    mx::RotZ(r, k.rot[2]);
    mx::RotY(r, k.rot[1]);
    mx::RotX(r, k.rot[0]);
    mx::RotAxis(r, k.axis, k.spin[2]);
    mx::RotX(r, k.spin[0]);
    mx::MatrixToAngles(r, &k.rot[0], &k.rot[1], &k.rot[2]);  // 0x004019E0
    const int contact = HeldFloor(w, k);
    if (contact != 0) w.Sound(0x1916A9);
    memcpy(k.lastView, k.pos, 12);  // c+0x14
    Mat4 mv = w.cam.view;
    mx::Translate(mv, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(mv, k.rot[2]);
    mx::RotY(mv, k.rot[1]);
    mx::RotX(mv, k.rot[0]);
    {
        Mat4 wm = Mat4::Identity();
        mx::Translate(wm, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotZ(wm, k.rot[2]);
        mx::RotY(wm, k.rot[1]);
        mx::RotX(wm, k.rot[0]);
        w.Draw(k.model, wm);
    }
    memcpy(k.body, k.pos, 12);
    HeldRegisterLock(w, k);  // 0x00408EC0
    mx::GetTranslation(mv, k.viewPos);
    w.RegisterTarget(&k);  // 0x00405160
    HeldShots(w, k, mv);   // 0x004423F0
    if (k.hp <= 0 || w.ts.Current()->fn != TaskHeldThrown) {
        HeldSyncLock(k);
        return;
    }
    if (static_cast<float>(F64(exe, 0x00564520)) <= k.viewPos[2]) {
        Mat4 v = w.cam.view;
        mx::Translate(v, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotZ(v, k.rot[2]);
        mx::RotY(v, k.rot[1]);
        mx::RotX(v, k.rot[0]);
        mx::GetTranslation(v, k.pos);
        mx::MatrixToAngles(v, &k.rot[0], &k.rot[1], &k.rot[2]);
        k.stuck = 0x5A;
        w.ts.Current()->fn = TaskHeldStuck;  // 0x00444160 (world_st4_held.cpp)
        k.fn = 0x00444160;
        if (w.players == 1) k.slot121 = static_cast<int8_t>(w.playersInGame);
        else if (w.players == 2) k.slot121 = F32(exe, 0x004C436C) <= k.viewPos[0] ? 1 : 0;
        w.PlayerTakeHit(k.slot121, 1, 7);  // 0x00415300
        w.targetTaken[k.slot121 & 1] = 1;   // 0x009A2BA0
        HeldSyncLock(k);
        return;
    }
    if (contact != 0 && static_cast<float>(F64(exe, 0x00564528)) <= k.viewPos[2]) {
        const float z1[3] = { 0.0f, 0.0f, 1.0f };
        float d[3];
        mx::TransformDir(w.cam.inverse, z1, d);  // 0x004A8AF0 on the camera matrix
        int pitch = 0, yaw = 0;
        mx::DirToAngles(d[0], d[1], d[2], &pitch, &yaw);  // 0x004016B0
        float s1, c1, s2, c2;
        mx::SinCos(pitch, &s1, &c1);
        mx::SinCos(yaw, &s2, &c2);
        const float h = s2 * k.vel[0] + c2 * k.vel[2];
        const float n = (-k.viewPos[1] * s1 + (static_cast<float>(F64(exe, 0x00564520)) - k.viewPos[2]) * c1) / h;
        k.vel[1] = (-k.viewPos[1] / n - h * s1) / c1 - n * k.acc[1] * F32(exe, 0x004C43AC);
    }
    HeldSyncLock(k);
}

// 0x00445C00(r, centre, p0, p1, &a, &b): the line p0 p1 against the sphere; 0 none, 1 touching (a = b), 2 two points
int TossLineSphere(float r, const float* c, const float* p0, const float* p1, float* a, float* b) {
    const float ex = p1[0] - p0[0], ey = p1[1] - p0[1], ez = p1[2] - p0[2];
    const float dx = p0[0] - c[0], dy = p0[1] - c[1], dz = p0[2] - c[2];
    const float t = -(((dx * ex + dy * ey) + dz * ez) / ((ex * ex + ey * ey) + ez * ez));
    const float ox = t * ex + dx, oy = t * ey + dy, oz = t * ez + dz;
    const float q[3] = { ox + c[0], oy + c[1], oz + c[2] };
    const float d = std::sqrt((oz * oz + oy * oy) + ox * ox);
    if (d < r) {
        const float* pf = (q[0] == p0[0] && q[1] == p0[1] && q[2] == p0[2]) ? p1 : p0;
        const float gx = c[0] - pf[0], gy = c[1] - pf[1], gz = c[2] - pf[2];
        float f2, f3, l;
        if (d == 0.0f) {
            l = std::sqrt((gx * gx + gy * gy) + gz * gz);
            f2 = r / l;
            f3 = l + r;
        } else {
            l = std::sqrt(((gx * gx + gy * gy) + gz * gz) - d * d);
            const float s = std::sqrt(r * r - d * d);
            f2 = s / l;
            f3 = s + l;
        }
        f3 = f3 / l;
        for (int i = 0; i < 3; ++i) a[i] = (pf[i] - q[i]) * f2 + q[i];
        for (int i = 0; i < 3; ++i) b[i] = f3 * q[i] - f2 * pf[i];
        return 2;
    }
    if (d == r) {
        memcpy(a, q, 12);
        memcpy(b, q, 12);
        return 1;
    }
    return 0;
}

// 0x00443540(obj, civilian node 2): the tossed object's view position against the civilian's node-2 hit sphere (view
// space, radius node +0x78 + object +0x128); a hit puts the object on the sphere (the crossing nearer the previous
// position), bounces the velocity off the contact normal (0.8 / 0.8 / 0.7), spins it 0x200 about the tangent and sets
// the spin pivot c+0x20 (object space). CODE (no capture: story mode 10 skips it).
bool HeldTossContact(World::Impl& w, HeldTask& k, const EnemyNode& n) {
    const float dz = k.viewPos[2] - n.hit[2], dy = k.viewPos[1] - n.hit[1], dx = k.viewPos[0] - n.hit[0];
    const float R = n.radius + k.radius128;
    if (std::sqrt((dz * dz + dy * dy) + dx * dx) < R) {
        float prev[3];
        {
            Mat4 v = w.cam.view;  // 0x009A6000 + ctx * 0x1A4
            mx::Translate(v, k.lastView[0], k.lastView[1], k.lastView[2]);
            mx::GetTranslation(v, prev);
        }
        float A[3], B[3];
        if (TossLineSphere(R, n.hit, k.viewPos, prev, A, B) != 0) {
            auto dist = [&](const float* x) {
                const float a = prev[2] - x[2], b = prev[1] - x[1], c = prev[0] - x[0];
                return std::sqrt((a * a + b * b) + c * c);
            };
            const float* p = dist(B) <= dist(A) ? B : A;
            const Mat4& camM = w.cam.inverse;  // 0x009A6040 + ctx * 0x1A4
            mx::Transform(camM, p, k.pos);
            float q1[3], q2[3];
            mx::Transform(camM, p, q1);
            mx::Transform(camM, n.hit, q2);
            const float d[3] = { q2[0] - q1[0], q2[1] - q1[1], q2[2] - q1[2] };
            int pitch = 0, yaw = 0;
            mx::DirToAngles(d[0], d[1], d[2], &pitch, &yaw);  // 0x004016B0
            {
                Mat4 r = Mat4::Identity();
                mx::RotX(r, -pitch);
                mx::RotY(r, -yaw);
                float v[3];
                mx::Transform(r, k.vel, v);
                memcpy(k.vel, v, 12);
            }
            {
                Mat4 r = Mat4::Identity();
                mx::RotY(r, yaw);
                mx::RotX(r, pitch);
                const float s[3] = { k.vel[0] * Bits(0x3F4CCCCD), k.vel[1] * Bits(0x3F4CCCCD), k.vel[2] * Bits(0x3F333333) };
                mx::Transform(r, s, k.vel);
            }
            Mat4 inv = Mat4::Identity();
            mx::RotX(inv, -k.rot[0]);
            mx::RotY(inv, -k.rot[1]);
            mx::RotZ(inv, -k.rot[2]);
            const float x1[3] = { 1.0f, 0.0f, 0.0f };
            float e1[3], e2[3];
            mx::Transform(inv, x1, e1);
            mx::Transform(inv, d, e2);
            const float cr[3] = { e2[2] * e1[1] - e2[1] * e1[2], e2[0] * e1[2] - e2[2] * e1[0], e2[1] * e1[0] - e2[0] * e1[1] };
            float axis[3];
            mx::Transform(inv, cr, axis);
            Mat4 m = Mat4::Identity();
            mx::RotZ(m, k.rot[2]);
            mx::RotY(m, k.rot[1]);
            mx::RotX(m, k.rot[0]);
            mx::RotAxis(m, axis, 0x200);
            int a0 = 0, a1 = 0, a2 = 0;
            mx::MatrixToAngles(m, &a0, &a1, &a2);  // 0x004019E0
            k.spin[0] = a0 - k.rot[0];
            k.spin[1] = a1 - k.rot[1];
            k.spin[2] = a2 - k.rot[2];
            const float f3 = k.radius128 / R, f2 = n.radius / R;
            float pv[3];
            for (int i = 0; i < 3; ++i) pv[i] = f3 * n.hit[i] + f2 * p[i];
            float pw[3];
            mx::Transform(camM, pv, pw);
            const float rel[3] = { pw[0] - k.pos[0], pw[1] - k.pos[1], pw[2] - k.pos[2] };
            mx::Transform(inv, rel, k.pivot);
            return true;
        }
    }
    k.pivot[0] = k.pivot[1] = k.pivot[2] = 0.0f;
    return false;
}

// 0x004432D0 (0x005926A8[3]): tossed: flight with gravity, spinning by c+0x2C..0x34 about the pivot c+0x20 (object
// space); outside game mode 10 the contact test against the civilian's node 2 (0x00443540; a hit kills the civilian,
// sound 0x1D16A9); removed off screen (0x004459C0)
void TaskHeldTossed(Task& t) {
    World::Impl& w = *CurrentWorld();
    HeldTask& k = t.As<HeldTask>();
    k.pos[0] = k.vel[0] + k.pos[0];
    k.pos[1] = k.pos[1] + k.vel[1];
    k.pos[2] = k.vel[2] + k.pos[2];
    k.vel[1] = k.acc[1] + k.vel[1];
    {
        Mat4 v = w.cam.view;
        mx::Translate(v, k.pos[0], k.pos[1], k.pos[2]);
        mx::GetTranslation(v, k.viewPos);
    }
    if (w.cfg.gameMode != 10) {
        EnemyTask* civ = Alive(w, k.civ, k.civSerial);
        if (!civ) {
            w.Note("held object tossed without its civilian (0x004432D0 reads the freed block)");
        } else if (HeldTossContact(w, k, civ->anim.nodes[2]) && !(civ->flags34 & 0x4000000)) {
            civ->flags34 |= 0x4000000;
            w.Sound(0x1D16A9);
        }
    }
    float p[3];
    {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotZ(m, k.rot[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotX(m, k.rot[0]);
        mx::Translate(m, k.pivot[0], k.pivot[1], k.pivot[2]);
        mx::GetTranslation(m, p);
    }
    k.rot[1] += k.spin[1];
    k.rot[0] += k.spin[0];
    k.rot[2] += k.spin[2];
    {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, p[0], p[1], p[2]);
        mx::RotZ(m, k.rot[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotX(m, k.rot[0]);
        mx::Translate(m, -k.pivot[0], -k.pivot[1], -k.pivot[2]);
        mx::GetTranslation(m, k.pos);
    }
    memcpy(k.lastView, k.pos, 12);  // c+0x14
    Mat4 mv = w.cam.view;           // 0x004A9230(0x009A6000 + ctx * 0x1A4)
    mx::Translate(mv, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(mv, k.rot[2]);
    mx::RotY(mv, k.rot[1]);
    mx::RotX(mv, k.rot[0]);
    w.Draw(k.model, mv, 8, -1.0f, false, -1, true);
    mx::GetTranslation(mv, k.viewPos);
    if (HeldOnScreen(w, k)) {
        w.RegisterTarget(&k);  // 0x00405160
        HeldShots(w, k, mv);   // 0x004423F0
        return;
    }
    HeldDestroy(w, k);
}

// 0x00443B90 (0x005926A8[4]): thrown at a player's aim point: flight with gravity (+0x5C), spinning by c+0x2C..0x34,
// drawn on the camera view; in front of the view (z >= double 0x00564520) while it has hp it sticks there (0x00444160)
// and hits its target +0x121 (kind 7; type 2: kind 1 with the pitch -0x800). CODE (no capture).
void TaskHeldAimed(Task& t) {
    World::Impl& w = *CurrentWorld();
    HeldTask& k = t.As<HeldTask>();
    const ExeImage& exe = w.exe;
    k.pos[0] = k.vel[0] + k.pos[0];
    k.pos[1] = k.pos[1] + k.vel[1];
    k.pos[2] = k.vel[2] + k.pos[2];
    k.vel[1] = k.acc[1] + k.vel[1];
    k.rot[1] = k.rot[1] + k.spin[1];
    k.rot[0] = k.rot[0] + k.spin[0];
    k.rot[2] = k.rot[2] + k.spin[2];
    memcpy(k.lastView, k.pos, 12);  // c+0x14
    Mat4 mv = w.cam.view;           // 0x004A9230(0x009A6000 + ctx * 0x1A4)
    mx::Translate(mv, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(mv, k.rot[2]);
    mx::RotY(mv, k.rot[1]);
    mx::RotX(mv, k.rot[0]);
    w.Draw(k.model, mv, 8, -1.0f, false, -1, true);
    memcpy(k.body, k.pos, 12);
    HeldRegisterLock(w, k);  // 0x00408EC0
    mx::GetTranslation(mv, k.viewPos);
    w.RegisterTarget(&k);  // 0x00405160
    HeldShots(w, k, mv);   // 0x004423F0
    if (0 < k.hp && static_cast<float>(F64(exe, 0x00564520)) <= k.viewPos[2]) {
        Mat4 v = w.cam.view;
        mx::Translate(v, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotZ(v, k.rot[2]);
        mx::RotY(v, k.rot[1]);
        mx::RotX(v, k.rot[0]);
        mx::GetTranslation(v, k.pos);
        mx::MatrixToAngles(v, &k.rot[0], &k.rot[1], &k.rot[2]);  // 0x004019E0
        k.stuck = 0x5A;
        w.ts.Current()->fn = TaskHeldStuck;  // 0x00444160 (world_st4_held.cpp)
        k.fn = 0x00444160;
        int kind = 7;
        if (k.type == 2) {
            k.rot[0] = -0x800;
            kind = 1;
        }
        w.PlayerTakeHit(k.slot121, 1, kind);  // 0x00415300
    }
    HeldSyncLock(k);
}

// 0x00442B90 (the holder's motion list set the state at its throw tick): world position / angles from the held
// matrix, the lock-on record, then by state; state 5: the throw velocity turned by the holder's angles (X, Z, Y), the
// tumbling axis z -> 0x00443DC0; state 3: the velocity turned the same way -> 0x004432D0; state 4: aimed at the target
// player's point (camera matrix x (x, 0, -15), x = 0 with one player, else (2 * +0x121 - 1) * double 0x0055CB00): the
// record's vel x = the flight frames n, vel y = the gravity (-> +0x5C); vel x / z = the horizontal distance / n along
// the yaw to the point, vel y = dy / n - n * g * 0.5 (float 0x004C43AC) -> 0x00443B90
bool HeldThrow(World::Impl& w, HeldTask& k, const Mat4& top, EnemyTask& h) {
    if (k.state == 4) {
        k.flags34 &= ~0x100u;
        Mat4 m = top;
        mx::MulRight(m, w.cam.inverse);  // 0x004A9230(camera) + 0x004A92A0(top)
        mx::GetTranslation(m, k.pos);
        memcpy(k.lastView, k.viewPos, 12);
        mx::MatrixToAngles(m, &k.rot[0], &k.rot[1], &k.rot[2]);  // 0x004019E0
        memcpy(k.body, k.pos, 12);
        HeldRegisterLock(w, k);  // 0x00408EC0
        float lx = 0.0f;
        if (w.players != 1) lx = static_cast<float>(static_cast<double>(k.slot121 * 2 - 1) * F64(w.exe, 0x0055CB00));
        const float local[3] = { lx, 0.0f, -15.0f };
        float tp[3];
        mx::Transform(w.cam.inverse, local, tp);  // 0x004A8A80 on the camera matrix
        int pitch = 0, yaw = 0;
        mx::DirToAngles(tp[0] - k.pos[0], tp[1] - k.pos[1], tp[2] - k.pos[2], &pitch, &yaw);  // 0x004016B0
        const float dz = k.pos[2] - tp[2], dx = k.pos[0] - tp[0];
        const float n = k.vel[0];
        k.acc[1] = k.vel[1];  // +0x5C = +0x50
        const float dy = tp[1] - k.pos[1];
        Mat4 r = Mat4::Identity();
        mx::RotY(r, yaw);
        const float f[3] = { 0.0f, 0.0f, std::sqrt(dz * dz + dx * dx) / n };
        float v[3];
        mx::Transform(r, f, v);
        k.vel[0] = v[0];
        k.vel[2] = v[2];
        k.vel[1] = dy / n - n * k.acc[1] * F32(w.exe, 0x004C43AC);
        w.ts.Current()->fn = TaskHeldAimed;  // 0x005926A8[4]
        k.fn = 0x00443B90;
        return true;
    }
    if (k.state == 3) {
        k.flags34 &= ~0x100u;
        Mat4 m = top;
        mx::MulRight(m, w.cam.inverse);  // 0x004A9230(camera) + 0x004A92A0(top)
        mx::GetTranslation(m, k.pos);
        memcpy(k.lastView, k.viewPos, 12);
        mx::MatrixToAngles(m, &k.rot[0], &k.rot[1], &k.rot[2]);  // 0x004019E0
        memcpy(k.body, k.pos, 12);
        HeldRegisterLock(w, k);  // 0x00408EC0
        Mat4 r = Mat4::Identity();
        mx::RotX(r, h.rot[0]);
        mx::RotZ(r, h.rot[2]);
        mx::RotY(r, h.rot[1]);
        float v[3];
        mx::Transform(r, k.vel, v);  // 0x004A8A80
        memcpy(k.vel, v, 12);
        w.ts.Current()->fn = TaskHeldTossed;  // 0x005926A8[3]
        k.fn = 0x004432D0;
        return true;
    }
    if (k.state != 5) return false;
    k.flags34 &= ~0x100u;
    Mat4 m = top;
    mx::MulRight(m, w.cam.inverse);  // 0x004A9230(camera) + 0x004A92A0(top)
    mx::GetTranslation(m, k.pos);
    memcpy(k.lastView, k.viewPos, 12);
    mx::MatrixToAngles(m, &k.rot[0], &k.rot[1], &k.rot[2]);  // 0x004019E0
    memcpy(k.body, k.pos, 12);
    HeldRegisterLock(w, k);  // 0x00408EC0
    Mat4 r = Mat4::Identity();
    mx::RotX(r, h.rot[0]);
    mx::RotZ(r, h.rot[2]);
    mx::RotY(r, h.rot[1]);
    float v[3];
    mx::Transform(r, k.vel, v);  // 0x004A8A80
    memcpy(k.vel, v, 12);
    k.axis[0] = 0.0f;
    k.axis[1] = 0.0f;
    k.axis[2] = 1.0f;
    w.ts.Current()->fn = TaskHeldThrown;  // 0x005926A8[5]
    k.fn = 0x00443DC0;
    return true;
}

// 0x00442820: between the holder's hands
void TaskHeld(Task& t) {
    World::Impl& w = *CurrentWorld();
    HeldTask& k = t.As<HeldTask>();
    EnemyTask* h = Alive(w, k.holder, k.holderSerial);
    if (!h) {
        w.Note("held object without its holder task (0x00442820)");
        HeldDestroy(w, k);
        return;
    }
    const Grip g = HandsGrip(w, *h, -4.0f);  // 0xC0800000
    Mat4 mv = Mat4::Identity();
    mx::Translate(mv, g.p[0], g.p[1], g.p[2]);
    mx::RotY(mv, g.a);
    mx::RotZ(mv, g.b);
    mx::RotX(mv, g.roll);
    mx::Translate(mv, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotX(mv, k.rot[0]);
    mx::RotZ(mv, k.rot[2]);
    mx::RotY(mv, k.rot[1]);
    w.Draw(k.model, mv, 8, -1.0f, false, -1, true);  // drawn on the identity: view space (exact vs the capture)
    if (h->flags34 & 0x4000000) {
        HeldDrop(w, k, mv, g, *h);
        EnemyTask* civ = Alive(w, k.civ, k.civSerial);
        if (civ && !(civ->flags34 & 0x4000000) && civ->civilian) static_cast<CivTask*>(civ)->c.altScript = 0;
    } else if (k.state != 10) {
        if (!HeldThrow(w, k, mv, *h))  // 0x00442B90 -> 0x00443DC0 from the next frame
            w.Note("held object throw (0x00442B90 -> 0x005926A8[" + std::to_string(k.state) + "]) not ported");
    }
    mx::GetTranslation(mv, k.viewPos);
    w.RegisterTarget(&k);
    HeldShots(w, k, mv);
}

// 0x00442740: first run: control block, position / velocity / spin from the record, definition sizes and hp
void TaskHeldInit(Task& t) {
    World::Impl& w = *CurrentWorld();
    HeldTask& k = t.As<HeldTask>();
    const EventMemory& mem = w.mem;
    const uint32_t p = k.params;
    if (k.holder) {
        k.civ = k.holder->parent;
        if (k.civ) {
            for (Task* q : w.ts.Live())
                if (q->data.get() == k.civ) k.civSerial = q->serial;
        }
    }
    k.type = EvS32(mem, p);
    const uint32_t def = Def(w, k.type);
    const int hp = w.exe.S16(def + 0x20);
    k.model = w.exe.S16(def + 0x20 + 2 * static_cast<uint32_t>(hp));
    k.state = 10;
    for (int i = 0; i < 3; ++i) {
        k.pos[i] = EvF32(mem, p + 0xC + 4 * i);
        k.rot[i] = 0;
        k.vel[i] = EvF32(mem, p + 0x24 + 4 * i);
        k.spin[i] = EvS32(mem, p + 0x18 + 4 * i);
    }
    k.acc[1] = Bits(0xBCDF0123);
    k.radius = F32(w.exe, def + 4);
    k.radius128 = F32(w.exe, def + 8);
    k.hp = static_cast<int16_t>(hp);
    k.slot121 = -1;
    const int fi = EvS32(mem, p + 4);
    if (fi == 1) {
        t.fn = TaskHeld;
        k.fn = 0x00442820;
    } else {
        w.Note("held object start function 0x005926A8[" + std::to_string(fi) + "] not ported");
        t.fn = TaskHeld;
        k.fn = w.exe.U32(0x005926A8 + 4 * static_cast<uint32_t>(fi));
    }
}

HeldTask* HeldOf(World::Impl& w, const EnemyTask& e) {
    const HeldTask* k = static_cast<const HeldTask*>(e.held139C);
    if (!k || !HeldTask::Live().count(k)) return nullptr;
    return const_cast<HeldTask*>(k);
}

}  // namespace

// exports for world_st4_held.cpp (world_held.h)
void HeldShotsExt(World::Impl& w, HeldTask& k, const Mat4& top) { HeldShots(w, k, top); }
bool HeldOnScreenExt(const World::Impl& w, const HeldTask& k) { return HeldOnScreen(w, k); }
void HeldDestroyExt(World::Impl& w, HeldTask& k) { HeldDestroy(w, k); }

// Behaviour 37 (0x0045B380): carry an object (motion list = 0x0045CA10: the object record, then {motion, start, loops,
// tick} entries from +0x38); sub 2 holds it until it is destroyed (then sub 4) or the throw tick; at the list end the
// script ends (0x0045C8D0) or continues as behaviour 35
void World::Impl::EnemyBeh37(EnemyTask& e) {
    switch (e.sub) {
    case 0: {
        const uint32_t l = static_cast<uint32_t>(e.behaviour == e.record.ParamS8(3) ? e.P(2) : e.P(1));  // 0x0045CA10
        HeldTask* k = NewTask<HeldTask>(TaskHeldInit, 0x00442740);
        e.held139C = k;
        k->holder = &e;
        k->holderSerial = CurrentSerial();
        k->params = l;
        ScriptFlags(e.flags34, EvS16(mem, l + 0x36));
        e.attackLimit = EvS32(mem, l + 8);  // +0x1358 throw state
        const int m = EvS16(mem, l + 0x30);
        if (e.anim.motion[0] != m) AnimBlendTo(e, m, EvS16(mem, l + 0x32), 0);
        e.headPitch = m;  // +0x1320
        e.facing1350 = EvS16(mem, l + 0x34);
        e.f1354 = EvS16(mem, l + 0x36);
        e.list1398 = l + 0x38;
        e.sub = 2;
        break;
    }
    case 1:
    case 3: {
        const uint32_t ps = e.list1398;
        const int m = EvS16(mem, ps), tick = EvS16(mem, ps + 6);
        ScriptFlags(e.flags34, tick);
        if (e.anim.motion[0] != m) AnimBlendTo(e, m, EvS16(mem, ps + 2), 10);
        e.headPitch = m;
        e.facing1350 = EvS16(mem, ps + 4);
        ++e.sub;
        e.f1354 = tick;
        e.list1398 = ps + 8;
        break;
    }
    case 2:
    case 4: {
        HeldTask* k = HeldOf(*this, e);
        if (e.sub == 2) {
            if (!k || k->hp < 1) {
                if (e.parent && e.parent->civilian) static_cast<CivTask*>(e.parent)->c.flags |= 0x800;
                if (e.flags34 & 0x1000000) {
                    e.flags34 &= ~0x1000000u;
                    e.flags136C |= 1;
                }
                e.variant = 0;  // +0x130C
                e.sub = 4;
            } else if (e.anim.tick[0] == e.f1354 && !(e.anim.flags[0] & 1)) {
                k->state = e.attackLimit;
                if (e.flags34 & 0x1000000) {
                    e.flags34 &= ~0x1000000u;
                    e.flags136C |= 1;
                }
                e.variant = 0;
            }
        }
        EnemyTurnTo(e, playerPos[0], playerPos[2], e.f1354 < 0 ? 0x34 : 0xD0);  // 0x00409F90
        bool end = true;
        if (e.sub != 4 || e.f1354 >= 0) {
            if (e.anim.tick[0] != MotionTicks(e.anim.motion[0]) - 1 || --e.facing1350 != 0) {
                end = false;
            } else if (e.f1354 < 0) {
                end = false;
                if (e.attackLimit != 4) {
                    if (runner->cfg.gameType == 2 && GlobalInt(0x009A2234, 0) == 1) {
                        if (e.anim.motion[0] != 0x10F) AnimBlendTo(e, 0x10F, 0, 10);
                        e.facing1350 = 1;
                        e.headPitch = e.anim.motion[0];
                    } else {
                        --e.sub;
                    }
                } else {
                    // throw at a player (state 4): the object's target +0x121 (chosen when the next entry has an
                    // attack tick), one thrower per player (0x009A2BA0)
                    if (!k) {
                        Note("behaviour 37 throw without its object (0x0045B380)");
                    } else {
                        if (0 < EvS16(mem, e.list1398 + 6)) {
                            if (players == 1) {
                                k->slot121 = static_cast<int8_t>(playersInGame);
                            } else if (players == 2) {
                                if (targetTaken[0] == 0 && (targetTaken[1] != 0 || e.viewPos[0] < 0.0f)) k->slot121 = 0;
                                else k->slot121 = 1;
                            }
                        }
                        const int s = k->slot121;
                        if (s == -1 || targetTaken[s] != 0) {
                            if (e.anim.motion[0] != 0x10F) AnimBlendTo(e, 0x10F, 0, 10);
                            e.facing1350 = 1;
                            e.headPitch = e.anim.motion[0];
                            k->slot121 = -1;
                        } else {
                            targetTaken[s] = 1;
                            --e.sub;
                        }
                    }
                }
            } else if (0 < EvS16(mem, e.list1398)) {
                e.behaviour = 0x23;
                e.sub = 1;
                end = false;
            }
        }
        if (end) EnemyScriptEnd(e);  // 0x0045C8D0
        break;
    }
    default:
        break;
    }
    if (!(e.flags34 & 0x40000000)) {
        const int m = e.headPitch;
        if (e.anim.motion[0] != m) {
            if (e.facing1350 < 2) {
                AnimBlendTo(e, m, MotionTicks(e.anim.motion[0]) - 1, 1);
                return;
            }
            AnimBlendTo(e, m, 0, 10);
            --e.facing1350;
        }
    }
}

// Behaviour 36 (0x0045B190): play the motion list {motion, start, loops, tick, flag} (10 bytes); at the tick (not
// blending) event flag `flag` (0x009C7200) is set; at the list end (motion < 1, tick >= 0) the script ends (0x0045C8D0),
// else behaviour 35 continues it
void World::Impl::EnemyBeh36(EnemyTask& e) {
    if (e.sub == 0 || e.sub == 1) {
        const bool first = e.sub == 0;
        const uint32_t l = first ? static_cast<uint32_t>(e.behaviour == e.record.ParamS8(3) ? e.P(2) : e.P(1)) : e.list1398;
        const int m = EvS16(mem, l), tick = EvS16(mem, l + 6);
        ScriptFlags(e.flags34, tick);
        if (first || e.anim.motion[0] != m) AnimBlendTo(e, m, EvS16(mem, l + 2), first ? 0 : 10);
        e.headPitch = m;
        e.facing1350 = EvS16(mem, l + 4);
        e.f1354 = tick;
        e.attackLimit = EvS16(mem, l + 8);  // +0x1358: event flag
        e.list1398 = l + 10;
        e.sub = first ? 2 : static_cast<int16_t>(e.sub + 1);
    } else if (e.sub == 2) {
        if (e.anim.tick[0] == e.f1354 && !(e.anim.flags[0] & 1)) runner->flags[e.attackLimit & 0xFF] = 1;
        if (e.anim.tick[0] == MotionTicks(e.anim.motion[0]) - 1 && --e.facing1350 == 0) {
            if (-1 < e.f1354) {
                if (EvS16(mem, e.list1398) < 1) {
                    EnemyScriptEnd(e);  // 0x0045C8D0
                    goto blend;
                }
                e.behaviour = 0x23;
            }
            e.sub = 1;
        }
    }
blend:
    if (!(e.flags34 & 0x40000000)) {
        const int m = e.headPitch;
        if (e.anim.motion[0] != m) {
            if (e.facing1350 < 2) {
                AnimBlendTo(e, m, MotionTicks(e.anim.motion[0]) - 1, 1);
                return;
            }
            AnimBlendTo(e, m, 0, 10);
            --e.facing1350;
        }
    }
}

}  // namespace game