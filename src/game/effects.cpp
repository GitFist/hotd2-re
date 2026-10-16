#include "effects.h"

#include "camera.h"  // LookAngles (Cam_DirToAngles 0x004016B0)

namespace game {

namespace {

// Point transform 0x004A8A80 (row vector times the current matrix).
void TransformPoint(const Mat4& m, const float in[3], float out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = in[0] * m.m[i] + in[1] * m.m[4 + i] + in[2] * m.m[8 + i] + m.m[12 + i];
}

// Cam_DirToAngles 0x004016B0 called with the vector d: angles of d as seen from the origin.
void DirToAngles(float dx, float dy, float dz, int* pitch, int* yaw) {
    const float zero[3] = { 0, 0, 0 };
    const float d[3] = { dx, dy, dz };
    LookAngles(d, zero, pitch, yaw);
}

}  // namespace

Mat4 ScaleMatrix(float x, float y, float z) {
    Mat4 r = Mat4::Identity();
    r.m[0] = x;
    r.m[5] = y;
    r.m[10] = z;
    return r;
}

// ---------------------------------------------------------------------------------------------------------------
// 0x004138A0
void Rain::Init(CrtRand& rng) {
    for (auto& p : p_) {
        p.x = static_cast<float>(rng.Next() % 20) - 10.0f;  // 0x004138BB
        p.y = static_cast<float>(rng.Next() % 50) - 25.0f;  // 0x004138D9
        p.z = static_cast<float>(rng.Next() % 25) - 35.0f;  // 0x004138F6
    }
    // The original also clears the rain flag 0x009C8E50 and 0x009C8A78, and stores two per-mode pointers
    // (0x00579C40 / 0x00579C70 [0x009A1A08]) to 0x007C2114 / 0x007C2110 (just past the pool; no reader found).
}

// 0x004136A0
int Rain::Update(CrtRand& rng, const CameraState& cam) {
    int respawns = 0;
    const Mat4 camYaw = Mat4::RotateY(cam.yaw);
    for (int i = 0; i < kCount; ++i) {
        Particle& p = p_[i];
        p.y = p.y - 2.0f;  // _DAT_004E30F0
        if (p.y <= -7.0f) {  // _DAT_004E30EC
            ++respawns;
            p.x = static_cast<float>(rng.Next() % 20) - 10.0f;  // 0x004136DD, _DAT_004C43A4
            p.y = static_cast<float>(rng.Next() % 50) - 25.0f;  // 0x004136FB, _DAT_004E30E8
            p.z = static_cast<float>(rng.Next() % 25) - 35.0f;  // 0x00413718, _DAT_004E30E4
        }
        // push; identity; rotY(ctx yaw); transform; pop
        const float local[3] = { p.x, p.y, p.z };
        float w[3];
        TransformPoint(camYaw, local, w);
        for (int k = 0; k < 3; ++k) w[k] = w[k] + cam.pos[k];
        int pitch, yaw;
        DirToAngles(w[0] - cam.pos[0], 0.0f, w[2] - cam.pos[2], &pitch, &yaw);
        // push; translate(w); rotY(yaw); rotZ(0x100); scale(1.5, 3.5, 1.0); draw 0x53 (alpha 0.5); pop
        world_[i] = ScaleMatrix(1.5f, 3.5f, 1.0f) * Mat4::RotateZ(0x100) * Mat4::RotateY(yaw) * Mat4::Translate(w[0], w[1], w[2]);
    }
    return respawns;
}

// ---------------------------------------------------------------------------------------------------------------
std::vector<SkyEntry> LoadSkyTable(const ExeImage& exe, int count) {
    std::vector<SkyEntry> t(count);
    for (int i = 0; i < count; ++i) {
        const uint32_t a = 0x00579968 + i * 16;
        t[i].model = exe.S16(a);
        t[i].model2 = exe.S16(a + 2);
        exe.Read(a + 4, &t[i].yOffset, 4);
        t[i].yawStep = static_cast<int32_t>(exe.U32(a + 8));
        t[i].yawInit = static_cast<int32_t>(exe.U32(a + 12));
    }
    return t;
}

// 0x004132D0
bool Sky::Update(const std::vector<SkyEntry>& table, int type, int mode, const CameraState& cam, std::vector<Draw>* out) {
    if (out) out->clear();
    if (type < 0 || type >= static_cast<int>(table.size())) return false;
    const SkyEntry& e = table[type];
    if (type != prevType) yaw_ = e.yawInit;
    if (mode == 0) {
        prevType = type;
        return true;
    }
    if (type == 8 || type == 10 || type == 11) {  // 0x00413620 / 0x00413500 (0x0041DC10 path for type 8)
        prevType = type;
        return false;
    }
    const float y = e.yOffset + cam.pos[1];
    if (mode != 2) yaw_ += e.yawStep;
    // 0x004A9AE0: fild angle; fmul qword 0x004C4370; fstp dword; fsincos. The angle in radians is rounded to a float,
    // which matters for the leftover yaws of Sky_Create (up to ~1e9 units: a float step every 6..7 frames)
    auto rotY = [](int a) {
        const float r = static_cast<float>(static_cast<double>(a) * (6.283185307179586 / 65536.0));
        const float c = static_cast<float>(std::cos(static_cast<double>(r))), s = static_cast<float>(std::sin(static_cast<double>(r)));
        Mat4 m = Mat4::Identity();
        m.m[0] = c; m.m[2] = -s; m.m[8] = s; m.m[10] = c;
        return m;
    };
    Mat4 rot = (type == 5) ? rotY(-yaw_) * Mat4::RotateZ(0x8000) : rotY(yaw_);
    const Mat4 t = Mat4::Translate(cam.pos[0], y, cam.pos[2]);
    if (out) out->push_back({ e.model, ScaleMatrix(1.2f, 1.2f, -1.2f) * rot * t });
    if (e.model2 != 0 && out) out->push_back({ e.model2, t });
    prevType = type;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// The type switch of 0x004073B0.
EffectDef EffectDefinition(int type) {
    EffectDef d;
    auto set = [&](int first, int last, float scale) {
        d.first = first;
        d.last = last;
        d.scale = scale;
    };
    switch (type & 0xFF) {
    case 0x01: case 0x33: set(0x91A, 0x92F, 1.0f); break;
    case 0x02: case 0x34: set(0xDC3, 0xDD1, 1.0f); break;
    case 0x03: case 0x35: case 0x52: set(0xE25, 0xE33, 1.0f); break;
    case 0x05: case 0x37: set(0x8F8, 0x903, 4.0f); break;
    case 0x06: case 0x38: set(0x904, 0x919, 1.0f); break;
    case 0x41: set(0xDD7, 0xE22, 1.0f); break;
    case 0x44: set(0xFD4, 0x1031, 1.0f); break;
    case 0x45: set(0x174A, 0x1785, 1.0f); break;
    case 0x46: case 0x4B: set(0x94, 0xA2, 0.7f); break;
    case 0x50: set(0x23A, 0x248, 1.0f); break;
    case 0x51: set(0x54, 0x62, 1.0f); break;
    case 0x53: set(0x125, 0x13D, 1.0f); d.randomRoll = true; break;
    case 0x5A: case 0x5B: set(0xAA4, 0xAB6, 2.0f); d.roll = 0xC000; break;
    case 0x5C: set(0xA87, 0xAA3, 2.0f); d.roll = 0xC000; break;
    case 0x5D: set(0xAB7, 0xAD3, 1.0f); break;
    case 0x61: set(0x1339, 0x1356, 1.0f); break;
    case 0x62: set(0x1339, 0x1356, 1.5f); break;
    case 0x63: set(0x91A, 0x92F, 5.0f); break;
    default: set(0x904, 0x904, 0.1f); break;
    }
    return d;
}

// 0x004073B0
void BillboardEffect::Spawn(const EffectSpawn& inArg, const CameraState& cam, const Mat4& view, CrtRand& rng,
                            std::vector<BillboardEffect>* out) {
    EffectSpawn in = inArg;  // the original writes the angles (and, for 0x5A, the type) back into the caller's block
    const size_t self = out->size();
    out->push_back(BillboardEffect{});
    {
        BillboardEffect& e = (*out)[self];
        e.flags = 1;
        for (int i = 0; i < 3; ++i) e.pos[i] = in.pos[i];
        e.roll = 0;
    }
    if (in.faceMode != 0) {
        DirToAngles(cam.pos[0] - in.pos[0], cam.pos[1] - in.pos[1], cam.pos[2] - in.pos[2], &in.pitch, &in.yaw);
        if (in.faceMode == 2) in.pitch = 0;
    }
    const int type = in.type & 0xFF;
    const EffectDef d = EffectDefinition(type);
    {
        BillboardEffect& e = (*out)[self];
        e.pitch = in.pitch;
        e.yaw = in.yaw;
        e.type = type;
        e.model = d.first;
        e.last = d.last;
        e.scale[0] = d.scale;
        if (d.roll) e.roll = d.roll;
        if (d.randomRoll) e.roll = rng.Next() % 0xFFFF;  // 0x00407610
    }
    if (type == 0x5A) {  // also spawns 0x5C and 0x5D (after itself)
        EffectSpawn sub = in;
        sub.type = 0x5C;
        Spawn(sub, cam, view, rng, out);
        sub.type = 0x5D;
        Spawn(sub, cam, view, rng, out);
    }
    BillboardEffect& e = (*out)[self];
    if (in.scale[0] != -1.0f) {
        e.scale[0] = in.scale[0];
        e.scale[1] = in.scale[1];
        e.scale[2] = in.scale[2];
        return;
    }
    if (type != 0x63) {
        float v[3];
        TransformPoint(view, e.pos, v);  // depth of the effect in the active camera's view space (in front: z < 0)
        float z = v[2];
        if (type == 0x53) {
            if (-35.0f < z) {                 // _DAT_004C4C70
                if (-0.001f < z) z = -0.001f;  // _DAT_004C4C6C
                z = z * -0.028571429f;        // _DAT_004C4C68 (-1/35)
                if (z < 0.0f) z = z * -1.0f;
                e.scale[0] = z;
            }
        } else if (-15.0f < z) {             // _DAT_004C4C78
            const float s = z * -0.0667f;     // _DAT_004C4C74
            e.scale[0] = s < 0.25f ? 0.25f : s;
        }
    }
    e.scale[1] = e.scale[2] = e.scale[0];
}

// 0x00407BC0
void BillboardEffect::SpawnScaled(const float pos[3], int type, const CameraState& cam, const Mat4& view,
                                  std::vector<BillboardEffect>* out) {
    const size_t self = out->size();
    out->push_back(BillboardEffect{});
    {
        BillboardEffect& e = (*out)[self];
        e.flags = 1;
        for (int i = 0; i < 3; ++i) e.pos[i] = pos[i];
        DirToAngles(cam.pos[0] - pos[0], cam.pos[1] - pos[1], cam.pos[2] - pos[2], &e.pitch, &e.yaw);
        e.type = type & 0xFF;
        e.roll = 0;
        e.model = e.last = 0;  // task memory is cleared (0x004A73D0): types without a range draw nothing and die
        e.scale[0] = 0;
        switch (e.type) {
        case 0x5B: e.model = 0xAA4; e.last = 0xAB6; e.scale[0] = 4.0f; e.roll = 0xC000; break;
        case 0x5C: e.model = 0xA87; e.last = 0xAA3; e.scale[0] = 2.0f; e.roll = 0xC000; break;
        case 0x5D: e.model = 0xAB7; e.last = 0xAD3; e.scale[0] = 2.0f; break;
        default: break;
        }
    }
    if ((type & 0xFF) == 0x5A) {
        SpawnScaled(pos, 0x5B, cam, view, out);
        SpawnScaled(pos, 0x5C, cam, view, out);
        SpawnScaled(pos, 0x5D, cam, view, out);
    }
    BillboardEffect& e = (*out)[self];
    float v[3];
    TransformPoint(view, e.pos, v);
    if (-30.0f < v[2]) {                                 // _DAT_004C4C84
        const float s = v[2] * -0.0333333f * e.scale[0];  // _DAT_004C4C80
        e.scale[0] = s < 0.25f ? 0.25f : s;
    }
    e.scale[1] = e.scale[2] = e.scale[0];
}

// 0x00407A70
bool BillboardEffect::Step(int* modelOut, Mat4* world) {
    // draw mode 0xE (0x004A79F0) for the draw, back to 8 afterwards
    float s[3] = { scale[0], scale[1], scale[2] };
    if (type == 'S' && static_cast<uint32_t>(model) < 0x129) {
        for (float& v : s) v = v * 0.75f;  // _DAT_004C4C7C
    }
    // 'P': light colour (1.0, 0.3, 0); 'S': light colour (0.85, 0.85, 0.75) via Light_SetColor 0x004AA0A0 (not modelled)
    if (world) {
        *world = ScaleMatrix(s[0], s[1], s[2]) * Mat4::RotateZ(roll) * Mat4::RotateX(pitch) * Mat4::RotateY(yaw) *
                 Mat4::Translate(pos[0], pos[1], pos[2]);
    }
    if (modelOut) *modelOut = model;  // 0x00418560: model 0 draws nothing
    ++model;
    return !(static_cast<uint32_t>(last) < static_cast<uint32_t>(model) || (flags & 2) != 0);
}

// 0x00407950
uint32_t EffectSound(int type, int mode) {
    switch (type & 0xFF) {
    case 0x01: case 0x33: return 0x1316A9;
    case 0x02: case 0x34: return 0xE16A9;
    case 0x03: case 0x35: return 0x1216A9;
    case 0x05: case 0x37: return 0x1416A9;
    case 0x06: case 0x38: return 0x1516A9;
    case 0x45: return mode == 4 ? 0x1623A9 : 0xB16A9;
    case 0x44: case 0x53: return 0xB16A9;
    case 0x61: case 0x62: return 0xC16A9;
    default: return 0;
    }
}

}  // namespace game
