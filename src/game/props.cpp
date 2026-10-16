#include "props.h"

#include "mstack.h"
#include "model.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>

namespace game {

namespace {
constexpr double kAngle = 6.283185307179586 / 65536.0;  // _DAT_004C4370

// MSVC _ftol (0x004ACF50): truncation toward zero.
int Ftol(double v) { return static_cast<int>(v); }

int RandOr0(PropWorld& w) { return w.rand ? w.rand() : 0; }

Mat4 Scale(float x, float y, float z) {
    Mat4 r = Mat4::Identity();
    r.m[0] = x;
    r.m[5] = y;
    r.m[10] = z;
    return r;
}

std::vector<uint8_t> ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}
}  // namespace

// ---------------------------------------------------------------- spawn records

bool SpawnRecord::Read(const EventMemory& mem, uint32_t a) {
    addr = a;
    uint32_t w[9];
    for (int i = 0; i < 9; ++i)
        if (!mem.Read(a + 4 * i, &w[i])) return false;
    type = w[0];
    flags = w[1];
    memcpy(pos, &w[2], 12);
    memcpy(rot, &w[5], 12);
    lo = static_cast<uint16_t>(w[8]);
    state = static_cast<uint16_t>(w[8] >> 16);
    for (int i = 0; i < 0x14; ++i) {
        uint32_t v = 0;
        mem.Read(a + 0x24 + 4 * i, &v);
        memcpy(&params[4 * i], &v, 4);
    }
    return true;
}

int16_t SpawnRecord::ParamS16(int off) const {
    int16_t v;
    memcpy(&v, &params[off], 2);
    return v;
}

int32_t SpawnRecord::ParamS32(int off) const {
    int32_t v;
    memcpy(&v, &params[off], 4);
    return v;
}

// ---------------------------------------------------------------- prop skeletons

bool PropSkeleton::Load(const ExeImage& exe, int t) {
    *this = PropSkeleton{};
    type = t;
    count = exe.S16(0x004D5404 + 2 * t);
    uint8_t mode = 0;
    exe.Read(0x004D5440 + t, &mode, 1);
    interp = mode;
    const uint32_t root = exe.U32(0x004D5390 + 4 * t);
    if (!exe.InData(root)) return false;
    // Node: u32 model; s16 index; s16 childCount; Node* children[]
    std::function<bool(uint32_t, int)> visit = [&](uint32_t va, int depth) -> bool {
        if (!exe.InData(va) || depth > 16 || nodes.size() > 256) return false;
        Node n;
        n.model = static_cast<int>(exe.U32(va));
        n.index = exe.S16(va + 4);
        const int children = exe.S16(va + 6);
        const int self = static_cast<int>(nodes.size());
        nodes.push_back(n);
        for (int i = 0; i < children; ++i) {
            nodes[self].children.push_back(static_cast<int>(nodes.size()));
            if (!visit(exe.U32(va + 8 + 4 * i), depth + 1)) return false;
        }
        return true;
    };
    return visit(root, 0);
}

bool LoadMotionById(const ExeImage& exe, const std::string& gameDir, int motionId, Motion& out) {
    for (const MotionFileEntry& f : ListMotionFiles(exe)) {
        for (int16_t id : f.motionIds) {
            if (id != motionId) continue;
            const std::vector<uint8_t> file = ReadFile(gameDir + "/mot/" + f.name);
            if (file.empty()) return false;
            for (Motion& m : SplitMotionFile(exe, f, file, 0))
                if (m.id == motionId) {
                    out = std::move(m);
                    return true;
                }
        }
    }
    return false;
}

namespace {
// 0x0040E040 / 0x0040E070: position / rotation of slot i at stored frame f.
void SlotPos(const PropSkeleton& s, const Motion& m, int f, int i, float out[3]) {
    const size_t off = 4 + f * s.FrameStride() + 12 * i;
    if (off + 12 <= m.data.size()) memcpy(out, &m.data[off], 12);
    else out[0] = out[1] = out[2] = 0;
}
void SlotRot(const PropSkeleton& s, const Motion& m, int f, int i, int16_t out[3]) {
    const size_t off = 4 + f * s.FrameStride() + 12 * (s.count - 1) + 6 * i;
    if (off + 6 <= m.data.size()) memcpy(out, &m.data[off], 6);
    else out[0] = out[1] = out[2] = 0;
}

// 0x0040D9D0: node transform premultiplied onto `parent`. Sets *approx when the original would take the
// big-angle path (0x00412750), which is not ported (plain interpolation is used instead).
Mat4 NodeTransform(const PropSkeleton& s, const Motion& m, const PropAnim& a, int slot, const Mat4& parent,
                   bool* approx) {
    float p[3];
    int r[3];
    const int f = a.frame;
    if (s.interp == 0 || f % 2 == 0) {
        const int sf = s.interp == 0 ? f : f / 2;
        int16_t rr[3];
        SlotPos(s, m, sf, slot, p);
        SlotRot(s, m, sf, slot, rr);
        for (int k = 0; k < 3; ++k) r[k] = rr[k];
    } else {
        const int fa = f / 2;
        int fb;
        if (f == m.ticks - 1 && f != a.prevFrame && f - a.prevFrame >= 0) fb = 0;
        else if (f == 0 && -a.prevFrame < 0) fb = m.ticks - 1;
        else fb = f / 2 + 1;
        float pa[3], pb[3];
        int16_t ra[3], rb[3];
        SlotPos(s, m, fa, slot, pa);
        SlotPos(s, m, fb, slot, pb);
        SlotRot(s, m, fa, slot, ra);
        SlotRot(s, m, fb, slot, rb);
        for (int k = 0; k < 3; ++k)
            p[k] = static_cast<float>((static_cast<double>(pb[k]) - pa[k]) * 0.5 + pa[k]);  // _DAT_004C43AC = 0.5
        if (s.interp == 2 && f > 1 && std::abs(rb[2] - ra[2]) > 0x3000 && std::abs(rb[1] - ra[1]) > 0x3000 &&
            std::abs(rb[0] - ra[0]) > 0x3000) {
            // big-angle path: the halfway rotation by matrix blend 0x00412750(A, B, 0.5), pre-multiplied after the
            // translation (0x004A92A0)
            Mat4 A = Mat4::Identity(), B = Mat4::Identity(), top;
            mx::RotZ(A, ra[2]);
            mx::RotY(A, ra[1]);
            mx::RotX(A, ra[0]);
            mx::RotZ(B, rb[2]);
            mx::RotY(B, rb[1]);
            mx::RotX(B, rb[0]);
            mx::BlendRotation(top, A, B, 0.5f);
            (void)approx;
            return top * (Mat4::Translate(p[0], p[1], p[2]) * parent);
        }
        for (int k = 0; k < 3; ++k) {
            int d = static_cast<uint16_t>(rb[k] - ra[k]);
            if (d > 0x8000) d -= 0x10000;
            r[k] = d / 2 + ra[k];
        }
    }
    // translate, then Z, Y, X onto the stack: M = Rx Ry Rz T * parent
    return Mat4::RotateX(r[0]) * Mat4::RotateY(r[1]) * Mat4::RotateZ(r[2]) * Mat4::Translate(p[0], p[1], p[2]) *
           parent;
}
}  // namespace

// 0x0040DDC0 + 0x0040DE50
std::vector<PropDraw> PropPose(const PropSkeleton& s, const Motion& m, PropAnim& a, const Mat4& parent,
                               int modelOverride, bool alt, int storeIndex, Mat4* stored, int (*rnd)()) {
    std::vector<PropDraw> out;
    if (a.frame >= m.ticks - 1) a.frame = 0;
    if (a.frame < 0) a.frame = m.ticks - 2;
    std::function<void(int, const Mat4&)> visit = [&](int ni, const Mat4& top) {
        const PropSkeleton::Node& n = s.nodes[ni];
        Mat4 cur = top;  // Mtx_Push
        if (n.index >= 1) {
            bool approx = false;
            cur = NodeTransform(s, m, a, n.index - 1, top, &approx);
            if (n.model == 0x10CE) {  // random scale per draw: (rand() % 11) * 0.01 + 0.27
                const int r = rnd ? rnd() : 0;
                const float k = static_cast<float>((r % 11) * 0.01f + 0.27f);
                cur = Scale(k, k, k) * cur;
            }
            PropDraw d;
            d.model = modelOverride >= 0 ? modelOverride : n.model;
            d.alt = modelOverride < 0 && alt;
            d.world = cur;
            d.approx = approx;
            out.push_back(d);
            if (storeIndex > 0 && storeIndex - 1 == n.index && stored) *stored = cur;
        }
        for (int c : n.children) visit(c, cur);
    };
    if (!s.nodes.empty()) visit(0, parent);
    a.prevFrame = a.frame;
    return out;
}

// ---------------------------------------------------------------- hanging prop

bool HangingTables::Load(const ExeImage& exe) {
    for (int k = 0; k < 16; ++k) {
        triple[k].clear();
        single[k].clear();
    }
    for (int k = 0; k < 5; ++k) {
        const int frames = k == 4 ? 130 : 60;
        const uint32_t t3 = exe.U32(0x005960B4 + 4 * k);
        if (exe.InData(t3) && (k == 0 || k == 2 || k == 3)) {
            triple[k].resize(frames * 3);
            exe.Read(t3, triple[k].data(), frames * 6);
        }
        const uint32_t t1 = exe.U32(0x005960C8 + 4 * k);
        if (exe.InData(t1) && k != 0 && k != 2 && k != 3) {
            single[k].resize(frames);
            exe.Read(t1, single[k].data(), frames * 2);
        }
    }
    return !triple[0].empty();
}

// 0x00472BD0
void HangingProp::Init(const SpawnRecord& r) {
    *this = HangingProp{};
    memcpy(pos, r.pos, 12);
    baseRot[1] = r.rot[1];
    rot[1] = 0;
    flags |= 0x51;
    model = r.ParamS16(4);
    hitId = r.ParamS32(8);
    amp = r.ParamS32(0x10);
    phase = r.ParamS32(0x14);
    kind = r.ParamS16(0);
    triggerFlag = r.ParamS8(0x20);
    killFlag = r.ParamS8(0x21);
    frame = 0;
    ryScale = 1.0f;
    scale[0] = scale[1] = scale[2] = 1.0f;
}

// 0x00473CF0 (logic part)
PropResult HangingProp::Update(PropWorld& w, const HangingTables& t) {
    if (killFlag >= 0 && w.flags[killFlag & 0xFF] == 1) return hitId == -1 ? PropResult::Killed : PropResult::Removed;
    if (w.stage == 1 && w.flags[0x77] != 0) return PropResult::Removed;
    if (model == 0xA60) {
        if (w.flags[0x36] != 0 && frame == 0) {
            w.flags[triggerFlag & 0xFF] = 1;
            kind = 1;
            w.sounds.push_back(0x1F16A9);
        }
        if (w.flags[0x23] != 0 && frame == 0 && kind == 0) {
            w.sounds.push_back(0x2116A9);  // + 0x00420810(3, 0x14) (not ported)
            hitId = -1;
        }
        if (rot[1] > 0x4000) frame = 0x3C;
    }
    if (w.stage == 3 && (w.stageSub == 1 || w.stageSub == 7) && model == 0x1866 &&
        (w.flags[2] == 1 || w.flags[4] == 1) && frame == 0 && once == 0) {
        w.sounds.push_back(0x361BA9);  // + 0x00420810(4, 0x14) (not ported)
        once = 1;
    }
    bool animate = w.flags[triggerFlag & 0xFF] == 1;
    if (animate) animate = kind == 4 ? frame < 0x82 : frame < 0x3C;
    if (animate) {
        int a = 0, c = 0;
        int b;
        if (kind == 0 || kind == 2 || kind == 3) {
            const std::vector<int16_t>& tab = t.triple[kind];
            a = tab[frame * 3];
            b = tab[frame * 3 + 1];
            c = tab[frame * 3 + 2];
        } else {
            const std::vector<uint16_t>& tab = t.single[kind & 15];
            b = tab.empty() ? 0 : tab[frame];
        }
        rot[2] = baseRot[2] + c;
        const double scaled = static_cast<double>(b) * ryScale;  // fild; fmul dword [+0x2C0] (x87, unrounded)
        if (amp > 0) {
            rot[0] = baseRot[0] + a;
            rot[1] = Ftol(scaled);
        } else {
            rot[0] = baseRot[0] - a;
            rot[1] = -Ftol(scaled);
        }
        ++frame;
    } else if (!(flags & 0x40000000) && (flags & 8)) {  // shot: start the 16-frame swing
        phase = 0;
        flags = (flags & 0xFFFFFFF1) | 0x40000000;
        swingBase = rot[1];
    }
    if (flags & 0x40000000) {
        phase += 0x1000;
        const double k = model == 0x1A46 ? 1024.0 : -1024.0;  // 0x00569178 / 0x00569170
        rot[1] = swingBase - Ftol(std::sin(phase * kAngle) * k) * amp;
        if (phase > 0xFFFF) {
            rot[1] = swingBase;
            flags &= 0xBFFFFFFF;
        }
    }
    return PropResult::Alive;
}

Mat4 HangingProp::World() const {
    // push; T(pos); RotY(+0x1D0); RotZ(+0x6C); RotY(+0x68); RotX(+0x64); Scale
    float s0 = scale[0], s1 = scale[1], s2 = scale[2];
    if (model == 0x1817 || model == 0x1816) {
        s0 = 1.05f;  // 0x3F866666
        s1 = s2 = 1.0f;
    }
    return Scale(s0, s1, s2) * Mat4::RotateX(rot[0]) * Mat4::RotateY(rot[1]) * Mat4::RotateZ(rot[2]) *
           Mat4::RotateY(baseRot[1]) * Mat4::Translate(pos[0], pos[1], pos[2]);
}

PropDraw HangingProp::Draw(const PropWorld& w) const {
    PropDraw d;
    d.model = model;
    d.world = World();
    d.alt = !(w.gameMode == 1 || w.altModels == 0 || w.camPath == 0x46);
    return d;
}

// ---------------------------------------------------------------- breakable props

bool BreakableTables::Load(const ExeImage& exe) {
    rows.clear();
    for (int k = 0; k < 11; ++k) {
        const uint32_t a = 0x00593DB8 + 12 * k;
        Row r;
        r.skeleton = exe.S16(a);
        r.motion = exe.S16(a + 2);
        r.sound = exe.U32(a + 4);
        r.radius = exe.S16(a + 8);
        r.height = exe.S16(a + 10);
        rows.push_back(r);
    }
    return true;
}

void BreakableProp::InitCommon(const SpawnRecord& r, PropWorld& w, const BreakableTables& t, int16_t sub) {
    subKind = sub;
    memcpy(pos, r.pos, 12);
    rotY = r.rot[1];
    const BreakableTables::Row& row = t.rows[static_cast<size_t>(sub) % t.rows.size()];
    hitRadius = static_cast<float>(row.radius);
    areaChanges = 0;
    areaSeen = static_cast<uint8_t>(w.area);
    flags = 0x80000001;
    model = -1;
    if (sub == 2) model = 0x17A9;
    if (sub == 3) model = 0x19E8;
    if (sub == 8) model = 0x17AA;
    if (sub == 9) model = 0x17AB;
    anim.skeleton = row.skeleton;
    anim.motion = row.motion;
    anim.frame = 0;
    anim.prevFrame = 0;
}

// 0x00473770
bool BreakableProp::InitState15(const SpawnRecord& r, PropWorld& w, const BreakableTables& t) {
    *this = BreakableProp{};
    if (w.gameMode != 1) {
        if (w.stage == 1 && w.stageSub == 0x15) return false;
        if (w.stage == 2 && (w.stageSub == 1 || w.stageSub == 8 || w.stageSub == 4)) return false;
        if (w.stage == 5 && w.stageSub == 1) return false;
    }
    InitCommon(r, w, t, static_cast<int16_t>(r.rot[2]));
    lifetime = r.ParamS16(0);
    itemSlot = r.ParamS8(4);
    field2A0 = r.ParamS32(8);
    if (itemSlot > 0) {
        if (r.rot[0] > 1) w.itemCounter[itemSlot & 15] = static_cast<uint8_t>(RandOr0(w) % r.rot[0] + 1);
        else w.itemCounter[itemSlot & 15] = 1;
    }
    return true;
}

// 0x00462E10
void BreakableProp::InitKind4(const SpawnRecord& r, PropWorld& w, const BreakableTables& t) {
    *this = BreakableProp{};
    InitCommon(r, w, t, static_cast<int16_t>(r.rot[2]));
    lifetime = static_cast<int16_t>(r.state);       // actor +0x11C
    itemSlot = static_cast<int8_t>(r.params[0]);    // actor +0x1F4 (record byte 0x24)
    field2A0 = -1;
    if (itemSlot > 0) {
        if (r.rot[0] > 1) w.itemCounter[itemSlot & 15] = static_cast<uint8_t>(RandOr0(w) % r.rot[0] + 1);
        else w.itemCounter[itemSlot & 15] = 1;
    }
}

// 0x00465FB0 (logic part)
PropResult BreakableProp::Update(PropWorld& w, const BreakableTables& t, uint32_t shotBits, int motionTicks) {
    flags |= shotBits;
    shakeValid = false;
    // 0x00466640
    if (w.stage == 1 && w.flags[0x77] != 0) return PropResult::Removed;
    if (static_cast<uint8_t>(w.area) != areaSeen) {
        ++areaChanges;
        if (lifetime < static_cast<int8_t>(areaChanges)) return PropResult::Removed;
        areaSeen = static_cast<uint8_t>(w.area);
    }
    if (w.stage == 0 && w.camPath == 0x29) {
        if (w.camTime == 0x14A) return PropResult::Removed;
    } else if (w.camPath == 0x2F && w.camTime == 0x96) {
        return PropResult::Removed;
    }
    if (anim.frame == 0 && (flags & 8) && (w.stage != 1 || w.stageSub != 0x11 || w.flags[0x28] != 0)) {
        const uint32_t f = flags;
        // 0x004650F0: the shooter is chosen by rand() when both gun bits are set
        if ((f & 2) && (f & 4)) RandOr0(w);
        if (model == 0x19E8) {
            flags = f & ~8u;
            w.sounds.push_back(0x1D16A9);
            model = -1;
            rotY = w.camYaw;
            events.push_back("effect 0x00465860");
            shake = 1.0f;
        } else {
            events.push_back("score 0x004156C0(+10)");
            w.sounds.push_back(t.rows[static_cast<size_t>(subKind) % t.rows.size()].sound);
            anim.frame = 1;
            flags = f & ~8u;
            events.push_back("hit spark 0x004666B0");
            if (itemSlot > 0) {
                uint8_t& c = w.itemCounter[itemSlot & 15];
                --c;
                if (c == 0) events.push_back("item drop slot " + std::to_string(itemSlot));
            }
        }
    }
    flags &= 0xFFFFFFF1;
    if (shake > 0.01f) {  // _DAT_004C4CC0
        const int r1 = RandOr0(w);
        shakeOffset[0] = static_cast<float>((static_cast<float>(r1 % 151) - 75.0f) * shake * 0.01f);
        const int r2 = RandOr0(w);
        shakeOffset[1] = static_cast<float>((static_cast<float>(r2 % 151) - 75.0f) * shake * 0.01f);
        shake *= 0.85f;  // _DAT_00564534
        shakeValid = true;
    }
    if (anim.frame > 0) {
        ++anim.frame;
        if (motionTicks - 2 < anim.frame) {
            if (itemSlot != 0 && itemSlot != 4) return PropResult::Removed;
            anim.frame = motionTicks - 2;
        }
    }
    return PropResult::Alive;
}

std::vector<PropDraw> BreakableProp::Draw(const PropWorld& w, const BreakableTables& t, const PropSkeleton& s,
                                          const Motion& m, bool motionLoaded, const Mat4& view) {
    std::vector<PropDraw> out;
    if (motionLoaded) {
        const float dx = shakeValid ? shakeOffset[0] : 0.0f, dz = shakeValid ? shakeOffset[1] : 0.0f;
        Mat4 top = Mat4::RotateY(rotY) * Mat4::Translate(dx + pos[0], pos[1], dz + pos[2]);
        if (w.stage == 3 && w.stageSub == 0x11) top = Scale(0.6f, 1.0f, 1.0f) * top;  // 0x3F19999A
        if (anim.frame == 0 && model != -1) {
            if (model != 0x19E8) top = Mat4::Translate(0, 0.8f, 0) * top;  // 0x3F4CCCCD
            PropDraw d;
            d.model = model;
            d.world = top;
            d.alt = w.altModels != 0;
            out.push_back(d);
        } else {
            const bool alt = itemSlot == 4 || w.stage == 3;  // 0x0040DFA0 vs 0x0040DD90
            for (PropDraw& d : PropPose(s, m, anim, top, -1, alt, 0, nullptr, w.rand)) out.push_back(d);
        }
    }
    int shadow = -1;
    float k = 0;
    switch (subKind) {
        case 0: case 3: case 6: case 7: case 10: shadow = 0x10D0; k = 10.0f; break;
        case 4: case 5: shadow = 0x10D1; k = 8.0f; break;
        default: break;
    }
    if (shadow >= 0) {
        PropDraw d;
        d.model = shadow;
        d.world = Scale(k, 1.0f, k) * Mat4::Translate(pos[0], w.groundY + 0.2f, pos[2]);  // _DAT_004D1D24 = 0.2
        out.push_back(d);
    }
    if (anim.frame == 0) {  // hit point: (x, y + height, z) in view space (0x004A8A80) -> +0x70
        const float p[3] = { pos[0], static_cast<float>(t.rows[static_cast<size_t>(subKind) % t.rows.size()].height) + pos[1],
                             pos[2] };
        for (int j = 0; j < 3; ++j)
            hitPoint[j] = p[0] * view.m[j] + p[1] * view.m[4 + j] + p[2] * view.m[8 + j] + view.m[12 + j];
    }
    return out;
}

// ---------------------------------------------------------------- komono kind 45

void DecorKind45::Init(const SpawnRecord& r, const PropWorld& w) {
    *this = DecorKind45{};
    lifetime = static_cast<int16_t>(r.state);
    areaSeen = static_cast<uint8_t>(w.area);
    areaChanges = 0;
}

// 0x0046DAB0 (logic part; the vertex wave edits the loaded models: DecorWave, applied by the renderer's scene)
PropResult DecorKind45::Update(PropWorld& w) {
    if (static_cast<uint8_t>(w.area) != areaSeen) {
        ++areaChanges;
        if (lifetime < static_cast<int8_t>(areaChanges)) return PropResult::Killed;
        areaSeen = static_cast<uint8_t>(w.area);
    }
    if (w.stageSub == 0xE) return PropResult::Killed;
    if (w.camPath == 0x2A && w.camTime == 500) extra = 1;
    // Vertex wave over models 0x1731..0x1734 (k = 0..3, DecorWave), phase_{k+1} = phase_k + 0x200
    phase = (phase + 4 * 0x200) & 0xFFFF;
    return PropResult::Alive;
}

std::vector<PropDraw> DecorKind45::Draw(const ExeImage& exe) const {
    std::vector<PropDraw> out;
    for (int i = 0; i < 11; ++i) {  // table 0x005949F8 (x, z), models 0x00594A50
        if (i >= 5 || extra) {
            float xz[2];
            exe.Read(0x005949F8 + 8 * i, xz, 8);
            PropDraw d;
            d.model = exe.S16(0x00594A50 + 2 * i);
            d.world = Mat4::RotateY(i < 5 ? 0x145E : 0x3C4D) * Mat4::Translate(xz[0], 65.0f, xz[1]);
            out.push_back(d);
        }
    }
    auto fixed = [&](uint32_t x, uint32_t z, int angle) {
        float fx, fz;
        memcpy(&fx, &x, 4);
        memcpy(&fz, &z, 4);
        PropDraw d;
        d.model = 0x1735;
        d.world = Mat4::RotateY(angle) * Mat4::Translate(fx, 65.0f, fz);
        out.push_back(d);
    };
    fixed(0xC400F49C, 0xC3E36937, 0x3C4D);
    fixed(0xC423B312, 0xC3FC2EFA, 0x145E);
    return out;
}

// 0x0046DC3D..0x0046DD29: the walk over the model's chunks (material chunks skipped; per strip each entry is an
// inline 32-byte vertex when bit 0 of its first word is set, else an 8-byte reference). With references resolved,
// a reference shows the inline vertex it points to, so editing every resolved vertex gives the same picture.
bool DecorWave(Model& m, int k, int phase) {
    bool ok = true;
    for (ModelMesh& mesh : m.meshes)
        for (ModelStrip& s : mesh.strips) {
            if (!(s.ctrl & 0x20)) ok = false;
            for (ModelVertex& v : s.vertices) {
                uint32_t xb;
                memcpy(&xb, &v.pos[0], 4);
                if (!(xb & 1)) continue;
                const int t = static_cast<int>(v.pos[1]);  // __ftol (truncation)
                if (t == 0) continue;
                const int a = ((5 * k - t) * 0x200 + phase) & 0xFFFF;
                const double c = static_cast<double>(-0.1f);  // float 0x0056470C
                v.pos[2] = static_cast<float>(static_cast<double>(v.pos[1]) * c * std::sin(a * 9.587379924285257e-05));
            }
        }
    return ok;
}

// ---------------------------------------------------------------- type 0x44 state 3

// 0x00472E00
void AnimatedProp::Init(const SpawnRecord& r, const PropWorld& w) {
    *this = AnimatedProp{};
    flags |= 0x51;
    anim.skeleton = 0xB;
    memcpy(pos, r.pos, 12);
    triggerFlag = r.ParamS8(0x20);
    killFlag = r.ParamS8(0x21);
    anim.motion = 0x1D6;
    anim.frame = 0;
    anim.prevFrame = 0;
    hitId = r.ParamS32(8);
    model = w.stage == 0 ? 0x17EE : 0x197C;
}

// 0x00474120 (logic part)
PropResult AnimatedProp::Update(PropWorld& w, int motionTicks) {
    if (w.flags[killFlag & 0xFF] == 1) return hitId != -1 ? PropResult::Removed : PropResult::Killed;
    if (w.flags[triggerFlag & 0xFF] == 1 && anim.frame < motionTicks - 2) ++anim.frame;
    const int f = anim.frame;
    if (f == 0x41 || f == 0x58 || f == 100 || f == 0x6E) w.sounds.push_back(0x1116A9);
    return PropResult::Alive;
}

std::vector<PropDraw> AnimatedProp::Draw(const PropWorld& w, const PropSkeleton& s, const Motion& m, Mat4* stored) {
    if (w.motionState != 2) return {};
    // push; T(pos); 0x0040DFD0(+0x324, 1, model): override model, store node 1's matrix (+0x338 -> +0x150);
    // then +0x64/+0x68/+0x6C are read from the motion frame at slot (triggerFlag - 1) (unused by the draw)
    return PropPose(s, m, anim, Mat4::Translate(pos[0], pos[1], pos[2]), model, false, 2, stored, w.rand);
}

}  // namespace game
