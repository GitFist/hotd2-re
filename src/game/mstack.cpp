#include "mstack.h"

#include "shoot_camera.h"  // game::DirToAngles (Cam_DirToAngles 0x004016B0)

namespace game::mx {

namespace {

// __ftol (truncation) of an angle in engine units, stored through a short (movsx eax, ax).
int AngleToShort(double v) { return static_cast<int16_t>(static_cast<uint16_t>(static_cast<int32_t>(v))); }

}  // namespace

// 0x004A8B60 (determinant) and 0x004A8D20 (inverse): the 24-bit x87 code (every product / sum / quotient rounded to
// float) in its exact operation order, generated from the disassembly (symbolic x87 stack; verified bit-exact against
// the original run in an emulator, 2000 matrices). Small relative rotations of the blend 0x00412750 come from
// differences of near-1 values, so the order matters (stage-3 boss chain, motion blends).
static float Determinant24(const float* p) {
    const float t1 = (p[9] * p[14]);
    const float t2 = (p[13] * p[11]);
    const float t3 = (p[15] * p[10]);
    const float t4 = (p[13] * p[10]);
    const float t5 = (p[15] * p[9]);
    const float t6 = (p[14] * p[11]);
    const float t7 = (p[12] * p[11]);
    const float t8 = (p[8] * p[14]);
    const float t9 = (p[12] * p[10]);
    const float t10 = (p[15] * p[8]);
    const float t11 = (p[13] * p[8]);
    const float t12 = (p[9] * p[12]);
    return ((((((((((t1 * p[7]) + (t3 * p[5])) + (t2 * p[6])) - (t4 * p[7])) - (t5 * p[6])) - (t6 * p[5])) * p[0]) - (((((((t8 * p[7]) + (t3 * p[4])) + (t7 * p[6])) - (t9 * p[7])) - (t10 * p[6])) - (t6 * p[4])) * p[1])) + (((((((t11 * p[7]) + (t5 * p[4])) + (t7 * p[5])) - (t12 * p[7])) - (t10 * p[5])) - (t2 * p[4])) * p[2])) - (((((((t1 * p[4]) + (t11 * p[6])) + (t9 * p[5])) - (t12 * p[6])) - (t8 * p[5])) - (t4 * p[4])) * p[3]));
}

static void Inverse24(const float* p, float det, float* o) {
    const float t1 = (p[9] * p[14]);
    const float t2 = (p[13] * p[11]);
    const float t3 = (p[15] * p[10]);
    const float t4 = (p[13] * p[10]);
    const float t5 = (p[15] * p[9]);
    const float t6 = (p[11] * p[14]);
    const float t19 = (p[11] * p[12]);
    const float t20 = (p[14] * p[8]);
    const float t21 = (p[12] * p[10]);
    const float t22 = (p[15] * p[8]);
    const float t36 = (p[13] * p[8]);
    const float t37 = (p[9] * p[12]);
    const float t113 = (p[5] * p[14]);
    const float t114 = (p[13] * p[7]);
    const float t115 = (p[15] * p[6]);
    const float t116 = (p[13] * p[6]);
    const float t117 = (p[15] * p[5]);
    const float t118 = (p[7] * p[14]);
    const float t131 = (p[7] * p[12]);
    const float t132 = (p[14] * p[4]);
    const float t133 = (p[12] * p[6]);
    const float t134 = (p[15] * p[4]);
    const float t148 = (p[13] * p[4]);
    const float t149 = (p[5] * p[12]);
    const float t175 = (p[5] * p[10]);
    const float t176 = (p[9] * p[7]);
    const float t177 = (p[11] * p[6]);
    const float t178 = (p[9] * p[6]);
    const float t179 = (p[11] * p[5]);
    const float t180 = (p[7] * p[10]);
    const float t194 = (p[7] * p[8]);
    const float t195 = (p[10] * p[4]);
    const float t196 = (p[8] * p[6]);
    const float t197 = (p[11] * p[4]);
    const float t210 = (p[9] * p[4]);
    const float t211 = (p[5] * p[8]);
    o[0] = (((((((t3 * p[5]) + (t1 * p[7])) + (t2 * p[6])) - (t4 * p[7])) - (t5 * p[6])) - (t6 * p[5])) / det);
    o[1] = (-(((((((t3 * p[1]) + (t1 * p[3])) + (t2 * p[2])) - (t4 * p[3])) - (t5 * p[2])) - (t6 * p[1])) / det));
    o[2] = (((((((t114 * p[2]) + (t115 * p[1])) + (t113 * p[3])) - (t116 * p[3])) - (t117 * p[2])) - (t118 * p[1])) / det);
    o[3] = (-(((((((t176 * p[2]) + (t177 * p[1])) + (t175 * p[3])) - (t178 * p[3])) - (t179 * p[2])) - (t180 * p[1])) / det));
    o[4] = (-(((((((t20 * p[7]) + (t19 * p[6])) + (t3 * p[4])) - (t21 * p[7])) - (t22 * p[6])) - (t6 * p[4])) / det));
    o[5] = (((((((t20 * p[3]) + (t19 * p[2])) + (t3 * p[0])) - (t21 * p[3])) - (t22 * p[2])) - (t6 * p[0])) / det);
    o[6] = (-(((((((t131 * p[2]) + (t115 * p[0])) + (t132 * p[3])) - (t133 * p[3])) - (t134 * p[2])) - (t118 * p[0])) / det));
    o[7] = (((((((t194 * p[2]) + (t177 * p[0])) + (t195 * p[3])) - (t196 * p[3])) - (t197 * p[2])) - (t180 * p[0])) / det);
    o[8] = (((((((t36 * p[7]) + (t19 * p[5])) + (t5 * p[4])) - (t37 * p[7])) - (t22 * p[5])) - (t2 * p[4])) / det);
    o[9] = (-(((((((t36 * p[3]) + (t19 * p[1])) + (t5 * p[0])) - (t37 * p[3])) - (t22 * p[1])) - (t2 * p[0])) / det));
    o[10] = (((((((t117 * p[0]) + (t148 * p[3])) + (t131 * p[1])) - (t149 * p[3])) - (t134 * p[1])) - (t114 * p[0])) / det);
    o[11] = (-(((((((t179 * p[0]) + (t210 * p[3])) + (t194 * p[1])) - (t211 * p[3])) - (t197 * p[1])) - (t176 * p[0])) / det));
    o[12] = (-(((((((t21 * p[5]) + (t36 * p[6])) + (t1 * p[4])) - (t37 * p[6])) - (t20 * p[5])) - (t4 * p[4])) / det));
    o[13] = (((((((t21 * p[1]) + (t36 * p[2])) + (t1 * p[0])) - (t37 * p[2])) - (t20 * p[1])) - (t4 * p[0])) / det);
    o[14] = (-(((((((t148 * p[2]) + (t113 * p[0])) + (t133 * p[1])) - (t149 * p[2])) - (t132 * p[1])) - (t116 * p[0])) / det));
    o[15] = (((((((t210 * p[2]) + (t175 * p[0])) + (t196 * p[1])) - (t211 * p[2])) - (t195 * p[1])) - (t178 * p[0])) / det);
}

Mat4 Inverse(const Mat4& in) {
    const float det = Determinant24(in.m);
    Mat4 out;
    if (det == 0.0f) {
        for (float& v : out.m) v = 3.4e38f;  // 0x7F7FC99E
        return out;
    }
    Inverse24(in.m, det, out.m);
    return out;
}

// 0x004A98D0 (normalize 0x004AA9C0, matrix built in the static 0x00598C18, then 0x004A92A0)
void RotAxis(Mat4& m, const float axisIn[3], int angle) {
    float s, c;
    SinCos(angle, &s, &c);  // fild; fmul qword (24-bit: the argument is rounded to float); fcos / fsin
    float a[3] = { axisIn[0], axisIn[1], axisIn[2] };
    const float len = std::sqrt((a[0] * a[0] + a[1] * a[1]) + a[2] * a[2]);  // 0x004AA9C0 (float precision)
    for (float& v : a) v = v / len;
    const float x = a[0], y = a[1], z = a[2];
    const float t = 1.0f - c;
    Mat4 r = Mat4::Identity();
    r.m[0] = x * x * t + c;
    const float yxt = y * x * t, zxt = z * x * t, zyt = z * y * t;
    r.m[1] = z * s + yxt;
    r.m[2] = zxt - y * s;
    r.m[4] = yxt - z * s;
    r.m[5] = y * y * t + c;
    r.m[6] = x * s + zyt;
    r.m[8] = y * s + zxt;
    r.m[9] = zyt - x * s;
    r.m[10] = z * z * t + c;
    MulLeft(m, r);
}

namespace {
Mat4 Rotation3(const Mat4& a) {  // 3x3 part, zero translation, w row (0,0,0,1)
    Mat4 r = Mat4::Identity();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i * 4 + j] = a.m[i * 4 + j];
    return r;
}
}  // namespace

// 0x00412750
void BlendRotation(Mat4& top, const Mat4& a, const Mat4& b, float f) {
    // top = B * inverse(A) (3x3 parts)
    Mat4 m = Inverse(Rotation3(a));
    MulLeft(m, Rotation3(b));
    const Mat4 rel = Rotation3(m);
    const float yAxis[3] = { 0, 1, 0 }, xAxis[3] = { 1, 0, 0 };
    float v[3];
    Transform(rel, yAxis, v);
    // 24-bit precision control: products, sums, fsqrt and the fmul by the qword constant round to float; fpatan's
    // result does not (it is only rounded by the following fmul)
    const float len = std::sqrt(v[2] * v[2] + v[0] * v[0]);
    const int theta = AngleToShort(static_cast<float>(std::atan2(static_cast<double>(len), static_cast<double>(v[1])) * kToAngle));
    if (theta == 0) {
        float w[3];
        Transform(rel, xAxis, w);
        top = a;  // 0x004A9230(A): the whole matrix
        const int phi = AngleToShort(static_cast<float>(std::atan2(-static_cast<double>(w[2]), static_cast<double>(w[0])) * kToAngle));
        RotY(top, static_cast<int>(static_cast<float>(phi) * f));  // fild; fmul dword (rounded to float); __ftol
        return;
    }
    float axis[3];
    if (theta == 0x8000) {  // unreachable after the short conversion, kept like the original
        axis[0] = 1.0f;
        axis[1] = axis[2] = 0.0f;
    } else {
        axis[0] = v[2];
        axis[1] = 0.0f;
        axis[2] = -v[0];
    }
    Mat4 t = Mat4::Identity();
    RotAxis(t, axis, -theta);
    MulLeft(t, rel);  // rel * R(axis, -theta)
    float w[3];
    Transform(t, xAxis, w);
    top = Rotation3(a);
    RotAxis(top, axis, static_cast<int>(static_cast<float>(theta) * f));
    const int phi = AngleToShort(static_cast<float>(std::atan2(-static_cast<double>(w[2]), static_cast<double>(w[0])) * kToAngle));
    RotY(top, static_cast<int>(static_cast<float>(phi) * f));
}

// 0x00401790: angles of a direction (x, y, z): *a2 = atan2(y, x), *a1 = -atan2(z, d)
// (24-bit precision control: the fmul qword results and the fdivr are rounded to float; the final angle of
// 0x004019E0 below is left in double: rounding it too improved stage 1 / 3 draws but lost a few demo / 2p ones)
static void DirAngles2(double x, double y, double z, int* a1, int* a2) {
    const int k = AngleToShort(static_cast<float>(std::atan2(y, x) * kToAngle));
    *a2 = k;
    const double a = static_cast<float>(k * kToRad);
    double d;
    if (((k + 0x2000) & 0x4000) == 0) d = static_cast<float>(x / std::cos(a));
    else d = static_cast<float>(y / std::sin(a));
    *a1 = -AngleToShort(static_cast<float>(std::atan2(z, d) * kToAngle));
}

// 0x004019E0
void MatrixToAngles(const Mat4& top, int* ax, int* ay, int* az) {
    const float xAxis[3] = { 1, 0, 0 }, yAxis[3] = { 0, 1, 0 };
    float d1[3], d2[3];
    TransformDir(top, xAxis, d1);
    DirAngles2(d1[0], d1[1], d1[2], ay, az);
    TransformDir(top, yAxis, d2);
    Mat4 m = Mat4::Identity();
    RotY(m, -*ay);
    RotZ(m, -*az);
    float d3[3];
    TransformDir(m, d2, d3);
    // fpatan; fmul qword 0x004C4378 (24-bit precision control: the product is rounded to float); __ftol
    *ax = AngleToShort(static_cast<float>(std::atan2(static_cast<double>(d3[2]), static_cast<double>(d3[1])) * kToAngle));
}

// 0x004016B0 with the vector (dx, dy, dz)
void DirToAngles(float dx, float dy, float dz, int* pitch, int* yaw) {
    game::DirToAngles(dx, dy, dz, pitch, yaw);  // the 24-bit FPU version (shoot_camera.cpp)
}

// 0x00401C50
void MatrixToAnglesRef(const Mat4& m, int ref, int* ax, int* ay, int* az) {
    Mat4 r = m;
    r.m[12] = r.m[13] = r.m[14] = 0.0f;  // 0x004A9FD0(0, 0, 0)
    const float ex[3] = { 1.0f, 0.0f, 0.0f }, ey[3] = { 0.0f, 1.0f, 0.0f };
    float c[3], d[3], e[3];
    TransformX87(r, ex, c);
    int a = 0, b = 0;
    game::DirToAngles(c[1], c[2], c[0], &a, &b);  // 0x00401BE0 -> Cam_DirToAngles 0x004016B0 (24-bit FPU version)
    if (static_cast<int16_t>(static_cast<int16_t>(ref) - static_cast<int16_t>(a) + 0x4000) < 0) {
        *ay = 0x8000 - a;
        *az = b + 0x8000;
    } else {
        *ay = a;
        *az = b;
    }
    TransformX87(r, ey, d);
    Mat4 q = Mat4::Identity();
    RotY(q, -*ay);
    RotZ(q, -*az);
    TransformX87(q, d, e);
    // fpatan; fmul qword 0x004C4378 (24-bit precision control: the product is rounded to float); __ftol
    *ax = AngleToShort(static_cast<float>(std::atan2(-static_cast<double>(e[2]), static_cast<double>(e[1])) * kToAngle));
}

}  // namespace game::mx
