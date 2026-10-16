#include "game/shoot_camera.h"

#include <cmath>
#include <cstring>

#include "game/camera.h"

// Precision model (CONFIRMED against the captured context angles/views): the game runs the x87 FPU with
// 24-bit precision control (Direct3D's default FPU setup), so every fadd/fsub/fmul/fdiv/fsqrt result is rounded
// to a float significand, while fpatan/fsin/fcos results are not. Below, basic operations are done in float in
// the original order (x86 SSE float arithmetic rounds identically), and transcendental results are kept in double
// until the next basic operation rounds them. Example: Cam_DirToAngles on (6479, 13958) round-trips to
// (6479, 13958) under 24-bit precision and to (6478, 13958) with extended intermediates; the capture has 6479.

namespace game {

namespace {
constexpr double kToAngle = 10430.378350470453;       // _DAT_004C4378 (65536 / 2pi)
constexpr double kFromAngle = 9.587379924285257e-05;  // _DAT_004C4370 (2pi / 65536)

// fild angle; fmul qword [0x004C4370] (rounded to 24 bits)
float AngleToRadians(int angle) { return static_cast<float>(angle * kFromAngle); }

// fsincos of the float angle, results stored as floats (0x004A99F0 / 0x004A9AE0 / 0x004A9BD0)
void SinCos(int angle, float* s, float* c) {
    const double a = AngleToRadians(angle);
    *s = static_cast<float>(std::sin(a));
    *c = static_cast<float>(std::cos(a));
}
}  // namespace

// ============================================================================================================
// MtxStack
// ============================================================================================================

MtxStack::MtxStack() { LoadIdentity(); }

void MtxStack::Push() {  // 0x004A9880(0)
    if (top_ + 1 >= 8) return;
    memcpy(m_[top_ + 1], m_[top_], sizeof(m_[0]));
    ++top_;
}

void MtxStack::Pop() {  // 0x004A9840
    if (top_ > 0) --top_;
}

void MtxStack::LoadIdentity() {  // 0x004A9E10 (copies 0x00571210)
    float* m = m_[top_];
    for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

void MtxStack::Load(const float m[16]) { memcpy(m_[top_], m, 64); }  // 0x004A9230

// 0x004A9D80: row 3 += x * row 0 + y * row 1 + z * row 2 (operation order of the x87 code).
void MtxStack::Translate(float x, float y, float z) {
    float* m = m_[top_];
    m[12] = ((y * m[4] + z * m[8]) + x * m[0]) + m[12];
    m[13] = ((x * m[1] + y * m[5]) + z * m[9]) + m[13];
    m[14] = ((x * m[2] + y * m[6]) + z * m[10]) + m[14];
    m[15] = ((x * m[3] + y * m[7]) + z * m[11]) + m[15];
}

// 0x004A99F0: rows 1, 2.
void MtxStack::RotateX(int angle) {
    float s, c;
    SinCos(angle, &s, &c);
    float* m = m_[top_];
    for (int j = 0; j < 4; ++j) {
        const float r1 = m[4 + j], r2 = m[8 + j];
        m[4 + j] = s * r2 + r1 * c;
        m[8 + j] = c * r2 - r1 * s;
    }
}

// 0x004A9AE0: rows 0, 2.
void MtxStack::RotateY(int angle) {
    float s, c;
    SinCos(angle, &s, &c);
    float* m = m_[top_];
    for (int j = 0; j < 4; ++j) {
        const float r0 = m[j], r2 = m[8 + j];
        m[j] = r0 * c - s * r2;
        m[8 + j] = s * r0 + c * r2;
    }
}

// 0x004A9BD0: rows 0, 1.
void MtxStack::RotateZ(int angle) {
    float s, c;
    SinCos(angle, &s, &c);
    float* m = m_[top_];
    for (int j = 0; j < 4; ++j) {
        const float r0 = m[j], r1 = m[4 + j];
        m[j] = s * r1 + r0 * c;
        m[4 + j] = c * r1 - s * r0;
    }
}

void MtxStack::TransformPoint(const float in[3], float out[3]) const {  // 0x004A8A80
    const float* m = m_[top_];
    const float x = in[0], y = in[1], z = in[2];
    const float ox = ((m[8] * z + m[4] * y) + x * m[0]) + m[12];
    const float oy = ((m[9] * z + m[1] * x) + m[5] * y) + m[13];
    const float oz = ((m[10] * z + m[2] * x) + m[6] * y) + m[14];
    out[0] = ox; out[1] = oy; out[2] = oz;
}

void MtxStack::TransformDir(const float in[3], float out[3]) const {  // 0x004A8AF0
    const float* m = m_[top_];
    const float x = in[0], y = in[1], z = in[2];
    const float ox = (m[8] * z + m[4] * y) + x * m[0];
    const float oy = (m[9] * z + m[1] * x) + m[5] * y;
    const float oz = (m[10] * z + m[2] * x) + m[6] * y;
    out[0] = ox; out[1] = oy; out[2] = oz;
}

void MtxStack::Store(float out[16]) const { memcpy(out, m_[top_], 64); }  // 0x004A8CA0

// ============================================================================================================
// angles
// ============================================================================================================

// fmul qword [0x004C4378] (rounded to 24 bits), __ftol (truncation; NaN gives the integer indefinite
// 0x80000000), movsx ax.
int TruncToAngle(double radians) {
    const float v = static_cast<float>(radians * kToAngle);
    int32_t i = static_cast<int32_t>(0x80000000u);
    if (v == v && v > -2147483648.0f && v < 2147483648.0f) i = static_cast<int32_t>(v);
    return static_cast<int16_t>(static_cast<uint16_t>(i));
}

// 0x004016B0 Cam_DirToAngles
void DirToAngles(float x, float y, float z, int* pitch, int* yaw) {
    const int a = TruncToAngle(std::atan2(static_cast<double>(x), static_cast<double>(z)));
    *yaw = a;
    const double r = AngleToRadians(a);
    float d;
    if (((a + 0x2000) & 0x4000) == 0) d = static_cast<float>(z / std::cos(r));
    else d = static_cast<float>(x / std::sin(r));
    *pitch = -TruncToAngle(std::atan2(static_cast<double>(y), static_cast<double>(d)));
}

// 0x00401790
void DirToAnglesXY(float x, float y, float z, int* a, int* b) {
    const int bb = TruncToAngle(std::atan2(static_cast<double>(y), static_cast<double>(x)));
    *b = bb;
    const double r = AngleToRadians(bb);
    float d;
    if (((bb + 0x2000) & 0x4000) == 0) d = static_cast<float>(x / std::cos(r));
    else d = static_cast<float>(y / std::sin(r));
    *a = -TruncToAngle(std::atan2(static_cast<double>(z), static_cast<double>(d)));
}

// 0x00401D70
int AngleBetween(const float a[3], const float b[3]) {
    const float dot = (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2];
    const float la = (a[0] * a[0] + a[1] * a[1]) + a[2] * a[2];
    const float lb = (b[0] * b[0] + b[1] * b[1]) + b[2] * b[2];
    const float r = la * lb - dot * dot;
    const float s = std::sqrt(r);  // a negative r gives NaN -> __ftol indefinite -> 0
    return TruncToAngle(std::atan2(static_cast<double>(s), static_cast<double>(dot)));
}

// 0x00401DF0
double SignedCos2(const float a[3], const float b[3]) {
    const float dot = (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2];
    const float la = (a[0] * a[0] + a[1] * a[1]) + a[2] * a[2];
    const float lb = (b[0] * b[0] + b[1] * b[1]) + b[2] * b[2];
    const float r = (dot * dot) / (la * lb);
    return dot < 0.0f ? -r : r;
}

// 0x00401E60
float Lerp(float cur, float target, int wt, int wc) {
    return (static_cast<float>(wc) * cur + static_cast<float>(wt) * target) / static_cast<float>(wt + wc);
}

// 0x00403C00
void TurnToward(const float pos[3], const float lookAt[3], const float target[3], float out[3], int num, int den) {
    const float d[3] = { target[0] - pos[0], target[1] - pos[1], target[2] - pos[2] };
    int a = 0, b = 0;
    DirToAnglesXY(d[0], d[1], d[2], &a, &b);  // frame in which pos->target is +x
    MtxStack s;
    s.LoadIdentity();
    s.RotateY(-a);
    s.RotateZ(-b);
    s.Translate(-pos[0], -pos[1], -pos[2]);
    float l[3];
    s.TransformPoint(lookAt, l);
    const int c = TruncToAngle(std::atan2(static_cast<double>(l[2]), static_cast<double>(l[1])));  // look-at into the xy plane
    s.LoadIdentity();
    s.RotateX(-c);
    s.RotateY(-a);
    s.RotateZ(-b);
    s.Translate(-pos[0], -pos[1], -pos[2]);
    float t[3];
    s.TransformPoint(target, t);  // computed and unused by the original
    s.TransformPoint(lookAt, l);
    const int dAng = TruncToAngle(std::atan2(static_cast<double>(l[1]), static_cast<double>(l[0])));
    const int e = (dAng * num) / (num + den);  // idiv: truncates toward zero
    const double ang = AngleToRadians(e);
    const float v[3] = { static_cast<float>(std::cos(ang) * 100.0), static_cast<float>(std::sin(ang) * 100.0), 0.0f };
    s.LoadIdentity();
    s.Translate(pos[0], pos[1], pos[2]);
    s.RotateZ(b);
    s.RotateY(a);
    s.RotateX(c);
    s.TransformPoint(v, out);
}

// 0x004018E0
void NormalizeAngles(MtxStack& s, int* pitch, int* yaw, int* roll) {
    s.Push();
    const float back[3] = { 0, 0, 1 }, side[3] = { 1, 0, 0 };
    float d[3], v[3], w[3];
    s.TransformDir(back, d);
    DirToAngles(d[0], d[1], d[2], pitch, yaw);
    s.TransformDir(side, v);
    s.LoadIdentity();
    s.RotateX(-*pitch);
    s.RotateY(-*yaw);
    s.TransformDir(v, w);
    *roll = TruncToAngle(std::atan2(static_cast<double>(w[1]), static_cast<double>(w[0])));
    s.Pop();
}

// 0x00403AC0 Cam_LookAngles (+ 0x004016B0 on pos - target)
void ContextLookAngles(CameraContext& c, const float target[3], int roll) {
    DirToAngles(c.pos[0] - target[0], c.pos[1] - target[1], c.pos[2] - target[2], &c.pitch, &c.yaw);
    c.roll = roll;
}

// ============================================================================================================
// contexts
// ============================================================================================================

// 0x00401F40 (matrix part; the light/fog setup and the mode commit are elsewhere)
void BuildContextViews(CameraContext ctx[4], int active, int shakeY) {
    MtxStack s;
    for (int i = 0; i < 4; ++i) {
        CameraContext& c = ctx[i];
        if (i == active) {
            s.LoadIdentity();
            s.Translate(c.pos[0], c.pos[1], c.pos[2]);
            s.RotateY(c.yaw);
            s.RotateX(c.pitch);
            const float p[3] = { 0.0f, static_cast<float>(shakeY), -1000.0f };
            float q[3];
            s.TransformPoint(p, q);
            static const bool dbg = getenv("HOTD2_CAMDBG") != nullptr;
            if (dbg)
                fprintf(stderr, "camviews pos %.9g %.9g %.9g ang %d %d %d q %.9g %.9g %.9g", c.pos[0], c.pos[1], c.pos[2], c.pitch,
                        c.yaw, c.roll, q[0], q[1], q[2]);
            ContextLookAngles(c, q, c.roll);
            if (dbg) fprintf(stderr, " -> %d %d\n", c.pitch, c.yaw);
        }
        s.LoadIdentity();
        s.Translate(c.pos[0], c.pos[1], c.pos[2]);
        s.RotateY(c.yaw);
        s.RotateX(c.pitch);
        s.RotateZ(c.roll);
        s.Store(c.camera);
        NormalizeAngles(s, &c.pitch, &c.yaw, &c.roll);
        // pos = translation row of the camera matrix (0x004A8CC0): unchanged
        s.LoadIdentity();
        s.RotateZ(-c.roll);
        s.RotateX(-c.pitch);
        s.RotateY(-c.yaw);
        s.Translate(-c.pos[0], -c.pos[1], -c.pos[2]);
        s.Store(c.view);
    }
}

// ============================================================================================================
// inputs
// ============================================================================================================

// 0x00403050
bool SelectLookAt(const LockOnSlot slots[4], const float railLook[3], float out[3]) {
    auto use = [&](const LockOnSlot& a) { for (int k = 0; k < 3; ++k) out[k] = a.point[k]; };
    auto mid = [&](const LockOnSlot& a, const LockOnSlot& b) {  // fld a; fadd b; fmul 0.5
        for (int k = 0; k < 3; ++k) out[k] = (a.point[k] + b.point[k]) * 0.5f;
    };
    const LockOnSlot &s0 = slots[0], &s1 = slots[1], &s2 = slots[2], &s3 = slots[3];
    if (s0.active && s1.active) {
        const bool t0 = s0.targetPlayer != -1, t1 = s1.targetPlayer != -1;
        if (t0 && !t1) use(s0);
        else if (!t0 && t1) use(s1);
        else mid(s1, s0);
        return true;
    }
    if (s0.active) { use(s0); return true; }
    if (s1.active) { use(s1); return true; }
    if (s2.active && s3.active) { mid(s3, s2); return true; }
    if (s2.active) { use(s2); return true; }
    if (s3.active) { use(s3); return true; }
    for (int k = 0; k < 3; ++k) out[k] = railLook[k];
    return false;
}

// ---- lock-on table ----

void LockOnTable::Reset() {  // 0x00408D90: candidate list, count, 16 slots cleared
    for (Candidate& c : list_) c = Candidate{};
    count_ = 0;
    for (int k = 0; k < 16; ++k) { on[k] = false; obj[k] = nullptr; }
}

// 0x00408EC0 (from Actor_UpdateViewPos 0x00409B70 every frame)
void LockOnTable::Register(LockOnTarget* t, const float playerPos[3]) {
    if (t->flags & 0x10000) return;
    if (count_ >= 14) return;
    const float dz = t->pos[2] - playerPos[2], dy = t->pos[1] - playerPos[1], dx = t->pos[0] - playerPos[0];
    const float d = std::sqrt((dx * dx + dy * dy) + dz * dz);
    const float k = d * 10.0f;  // 0x004C43A4
    int32_t key = static_cast<int32_t>(0x80000000u);
    if (k == k && k < 2147483648.0f) key = static_cast<int32_t>(k);
    list_[count_++] = { static_cast<uint32_t>(key), t };
}

// 0x00408E80
void LockOnTable::AddDirect(LockOnTarget* t) {
    t->slot = -1;
    for (int c = 2; c < 14; ++c) {
        if (!on[c]) {
            t->slot = c;
            on[c] = true;
            obj[c] = t;
            return;
        }
    }
}

// 0x004092B0
void LockOnTable::Remove(LockOnTarget* t) {
    Release(t->slot);
    t->slot = -1;
}

// Task 0x00408DD0: sort 0x00408F30 (stable LSD radix sort on the low 16 bits of the key, two byte passes),
// clear the 16 slots, assign, count = 0.
void LockOnTable::Build() {
    for (int pass = 0; pass < 16; pass += 8) {
        int counts[256] = {};
        for (int i = 0; i < count_; ++i) ++counts[(list_[i].key >> pass) & 0xFF];
        for (int b = 1; b < 256; ++b) counts[b] += counts[b - 1];
        Candidate sorted[14];
        for (int i = count_ - 1; i >= 0; --i) sorted[--counts[(list_[i].key >> pass) & 0xFF]] = list_[i];
        for (int i = 0; i < count_; ++i) list_[i] = sorted[i];
    }
    for (int k = 0; k < 16; ++k) { on[k] = false; obj[k] = nullptr; }
    for (int i = 0; i < count_; ++i) {
        LockOnTarget* t = list_[i].t;
        if (t->targetPlayer != -1) {
            for (int c = 0; c < 2; ++c) {
                if (!obj[c]) {
                    on[c] = true;
                    obj[c] = t;
                    t->slot = c;
                    break;
                }
            }
        } else {
            on[2 + i] = true;
            obj[2 + i] = t;
            t->slot = 2 + i;
        }
    }
    count_ = 0;
}

void LockOnTable::Slots(LockOnSlot out[4]) const {
    for (int k = 0; k < 4; ++k) {
        out[k] = LockOnSlot{};
        out[k].active = on[k];
        if (obj[k]) {
            for (int c = 0; c < 3; ++c) out[k].point[c] = obj[k]->point[c];
            out[k].targetPlayer = obj[k]->targetPlayer;
        }
    }
}

// 0x00403B00
int TurnStepFromAngle(const ExeImage* exe, int table, const float pos[3], const float target[3],
                      const float lookAt[3]) {
    const float t[3] = { target[0] - pos[0], target[1] - pos[1], target[2] - pos[2] };
    const float l[3] = { lookAt[0] - pos[0], lookAt[1] - pos[1], lookAt[2] - pos[2] };
    int a = AngleBetween(t, l);
    if (static_cast<uint16_t>(a) >= 0x2000) a = 0x1FFF;
    const int idx = a >> 7;
    if (!exe) return 12;
    const uint32_t p = exe->U32(0x00576C04 + 4 * table);
    int8_t v = 0;
    exe->Read(p + idx, &v, 1);
    return v;
}

int TurnStepRail(const ExeImage* exe) {
    if (!exe) return 12;
    int8_t v = 0;
    exe->Read(exe->U32(0x00576C0C), &v, 1);
    return v;
}

}  // namespace game
