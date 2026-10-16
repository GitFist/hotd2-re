#pragma once

// The engine's matrix-stack operations (top matrix 0x007E7990), as free functions on a game::Mat4 (row vectors,
// v' = v * M, translation in m[12..14]). Every operation PRE-multiplies the top matrix the way the engine does
// (local-first), so a sequence  T(p); RotY(a); Scale(s)  applied to the view V leaves  S * Ry * T * V.
// The formulas (and their operand order) follow the decompiled originals. The game runs the x87 FPU at 24-bit precision
// (Direct3D default), so every add / multiply is rounded to float: the operations are written in float arithmetic
// (SSE2 single precision gives the same results). With double intermediates the stage-1 demo drifted by one angle unit
// (enemy facing at 3410 / 3569, camera at 3585).

#include <cmath>
#include <cstdint>

#include "anim.h"  // Mat4

namespace game::mx {

constexpr double kToRad = 9.587379924285257e-05;   // double 0x004C4370 = 2*pi / 65536
constexpr double kToAngle = 10430.378350470453;     // double 0x004C4378 = 65536 / (2*pi)

// fild angle; fmul qword K; fstp float (the angle is quantized to float precision); fcos / fsin.
inline void SinCos(int a, float* s, float* c) {
    const float ang = static_cast<float>(static_cast<double>(a) * kToRad);
    *c = static_cast<float>(std::cos(static_cast<double>(ang)));
    *s = static_cast<float>(std::sin(static_cast<double>(ang)));
}

// 0x004A9E10
inline void Identity(Mat4& m) { m = Mat4::Identity(); }

// 0x004A9D80 (24-bit x87: every product / sum rounded to float, in the order of the original's code)
inline void Translate(Mat4& m, float x, float y, float z) {
    float* t = m.m;
    t[12] = ((y * t[4] + z * t[8]) + x * t[0]) + t[12];
    t[13] = ((x * t[1] + y * t[5]) + z * t[9]) + t[13];
    t[14] = ((x * t[2] + y * t[6]) + z * t[10]) + t[14];
    t[15] = ((x * t[3] + y * t[7]) + z * t[11]) + t[15];
}

// 0x004A99F0: rows 1 and 2
inline void RotX(Mat4& m, int a) {
    float s, c;
    SinCos(a, &s, &c);
    float* t = m.m;
    for (int k = 0; k < 4; ++k) {
        const float r1 = t[4 + k], r2 = t[8 + k];
        t[4 + k] = static_cast<float>(r1 * c + s * r2);
        t[8 + k] = static_cast<float>(c * r2 - r1 * s);
    }
}

// 0x004A9AE0: rows 0 and 2
inline void RotY(Mat4& m, int a) {
    float s, c;
    SinCos(a, &s, &c);
    float* t = m.m;
    for (int k = 0; k < 4; ++k) {
        const float r0 = t[k], r2 = t[8 + k];
        t[k] = static_cast<float>(r0 * c - s * r2);
        t[8 + k] = static_cast<float>(r0 * s + c * r2);
    }
}

// 0x004A9BD0: rows 0 and 1
inline void RotZ(Mat4& m, int a) {
    float s, c;
    SinCos(a, &s, &c);
    float* t = m.m;
    for (int k = 0; k < 4; ++k) {
        const float r0 = t[k], r1 = t[4 + k];
        t[k] = static_cast<float>(r0 * c + s * r1);
        t[4 + k] = static_cast<float>(c * r1 - r0 * s);
    }
}

// 0x004A9CC0
inline void Scale(Mat4& m, float x, float y, float z) {
    for (int k = 0; k < 4; ++k) {
        m.m[k] *= x;
        m.m[4 + k] *= y;
        m.m[8 + k] *= z;
    }
}

// 0x004A8A80: point (row vector) times the matrix, in the summation order of its x87 code (24-bit rounding after
// every operation): x' = ((m20 z + m10 y) + x m00) + m30, y' = ((m21 z + m01 x) + m11 y) + m31,
// z' = ((m22 z + m02 x) + m12 y) + m32.
inline void Transform(const Mat4& m, const float in[3], float out[3]) {
    const float* t = m.m;
    const float x = in[0], y = in[1], z = in[2];
    const float r0 = ((t[8] * z + t[4] * y) + x * t[0]) + t[12];
    const float r1 = ((t[9] * z + t[1] * x) + t[5] * y) + t[13];
    const float r2 = ((t[10] * z + t[2] * x) + t[6] * y) + t[14];
    out[0] = r0;
    out[1] = r1;
    out[2] = r2;
}

// (same as Transform; kept for the call sites that named the x87 order explicitly)
inline void TransformX87(const Mat4& m, const float in[3], float out[3]) { Transform(m, in, out); }

// The summation order the port used before Transform followed the x87 code: x m00 + m10 y + m20 z + m30 (and
// m11 y + m01 x + m21 z + m31, m12 y + m02 x + m22 z + m32). Only where an input is not the original's exact value
// and this order is empirically closer (actor 0x31 view position, world_st2_actor31.cpp).
inline void TransformWorldOrder(const Mat4& m, const float in[3], float out[3]) {
    const float* t = m.m;
    const float x = in[0], y = in[1], z = in[2];
    out[0] = x * t[0] + t[4] * y + t[8] * z + t[12];
    out[1] = t[5] * y + t[1] * x + t[9] * z + t[13];
    out[2] = t[6] * y + t[2] * x + t[10] * z + t[14];
}

// 0x004A8AF0: direction (no translation).
inline void TransformDir(const Mat4& m, const float in[3], float out[3]) {
    const float* t = m.m;
    const float x = in[0], y = in[1], z = in[2];
    const float r0 = (t[8] * z + t[4] * y) + x * t[0];
    const float r1 = (t[9] * z + t[1] * x) + t[5] * y;
    const float r2 = (t[10] * z + t[2] * x) + t[6] * y;
    out[0] = r0;
    out[1] = r1;
    out[2] = r2;
}

// 0x004A9570: M = M * P (per element the summation order of the x87 code, 24-bit rounding)
inline void MulRight(Mat4& m, const Mat4& pm) {
    const Mat4 cm = m;
    const float* c = cm.m;
    const float* p = pm.m;
    float* t = m.m;
    for (int r = 0; r < 16; r += 4) {
        t[r + 0] = ((c[r + 1] * p[4] + c[r + 3] * p[12]) + c[r + 2] * p[8]) + c[r + 0] * p[0];
        t[r + 1] = ((c[r + 0] * p[1] + c[r + 1] * p[5]) + c[r + 3] * p[13]) + c[r + 2] * p[9];
        t[r + 2] = ((c[r + 1] * p[6] + c[r + 3] * p[14]) + c[r + 2] * p[10]) + c[r + 0] * p[2];
    }
    // the last column: its own order in rows 0..2, another one in row 3
    t[3] = ((c[3] * p[15] + c[2] * p[11]) + c[0] * p[3]) + c[1] * p[7];
    t[7] = ((c[7] * p[15] + c[6] * p[11]) + c[4] * p[3]) + c[5] * p[7];
    t[11] = ((c[11] * p[15] + c[10] * p[11]) + c[8] * p[3]) + c[9] * p[7];
    t[15] = ((c[14] * p[11] + c[12] * p[3]) + c[13] * p[7]) + c[15] * p[15];
}

// 0x004A92A0: M = P * M (per element the summation order of the x87 code, 24-bit rounding)
inline void MulLeft(Mat4& m, const Mat4& pm) {
    const Mat4 cm = m;
    const float* c = cm.m;
    const float* p = pm.m;
    float* t = m.m;
    for (int j = 0; j < 4; ++j) {
        t[j] = ((c[4 + j] * p[1] + c[12 + j] * p[3]) + c[8 + j] * p[2]) + c[j] * p[0];
        for (int i = 4; i < 16; i += 4)
            t[i + j] = ((c[j] * p[i] + c[4 + j] * p[i + 1]) + c[12 + j] * p[i + 3]) + c[8 + j] * p[i + 2];
    }
}

// 0x004A8CC0: translation row
inline void GetTranslation(const Mat4& m, float out[3]) {
    out[0] = m.m[12];
    out[1] = m.m[13];
    out[2] = m.m[14];
}

// 0x004A9F70: rotation part = identity (billboard), translation kept.
inline void ClearRotation(Mat4& m) {
    m.m[0] = m.m[5] = m.m[10] = 1.0f;
    m.m[1] = m.m[2] = m.m[4] = m.m[6] = m.m[8] = m.m[9] = 0.0f;
}

// 0x004A8D20: general inverse (cofactors / determinant); a singular matrix becomes 3.4e38 everywhere.
Mat4 Inverse(const Mat4& m);  // 24-bit x87: float, the original operation order (bit-exact)

// 0x004A98D0: M = R(axis, angle) * M, axis normalized first (0x004AA9C0).
void RotAxis(Mat4& m, const float axis[3], int angle);

// 0x00412750: rotation blend of two rotation matrices (only their 3x3 parts are used) by factor f in [0, 1], left on
// `top` (which the original first sets to identity): the relative rotation of b against a is reduced to an axis and
// an angle; the result is a, rotated by f * angle about that axis.
void BlendRotation(Mat4& top, const Mat4& a, const Mat4& b, float f);

// 0x004019E0: Euler angles (x, y, z) of the top matrix's rotation, as Anim_* use them (RotZ, RotY, RotX order).
void MatrixToAngles(const Mat4& m, int* ax, int* ay, int* az);

// Cam_DirToAngles 0x004016B0 on a direction vector (pitch, yaw), exactly like game::LookAngles(dir, 0).
void DirToAngles(float dx, float dy, float dz, int* pitch, int* yaw);

// 0x00401C50(ref, &ax, &ay, &az): Euler angles of m's rotation for the order RotZ(az), RotY(ay), RotX(ax) (the
// players' angles 0x009C71EC / 0x009C71F0 / 0x009C71F4 of rail 0x0040C380); ref picks the y branch (0x00401BE0).
void MatrixToAnglesRef(const Mat4& m, int ref, int* ax, int* ay, int* az);

}  // namespace game::mx
