#include "anim.h"

#include <cmath>
#include <cstring>
#include <functional>

namespace game {

namespace {
constexpr uint32_t kSkeletonRoots = 0x004E0430;
constexpr uint32_t kSkeletonNodeCounts = 0x004E0724;
constexpr uint32_t kMotFileNames = 0x004D1B00;
constexpr uint32_t kMotFileIds = 0x004E2B14;
constexpr uint32_t kMotFileCounts = 0x004E2BDC;
constexpr uint32_t kMotionTicks = 0x004E07D0;
constexpr double kAngle = 6.283185307179586 / 65536.0;  // _DAT_004C4370

// Shortest signed difference between two engine angles, as in Anim_NodeTransform's interpolation.
int AngleDelta(int16_t from, int16_t to) {
    int d = static_cast<uint16_t>(to - from);
    if (d > 0x8000) d -= 0x10000;
    return d;
}
}  // namespace

bool LoadSkeleton(const ExeImage& exe, int type, Skeleton& out) {
    out = Skeleton{};
    out.type = type;
    out.nodeCount = exe.S16(kSkeletonNodeCounts + type * 2);
    const uint32_t root = exe.U32(kSkeletonRoots + type * 4);
    if (!exe.InData(root)) return false;
    std::function<bool(uint32_t, int, int)> visit = [&](uint32_t va, int parent, int depth) -> bool {
        if (!exe.InData(va) || depth > 64 || out.nodes.size() > 512) return false;
        SkeletonNode n;
        uint8_t raw[0x18];
        exe.Read(va, raw, sizeof(raw));
        memcpy(&n.modelId, raw, 4);
        memcpy(n.offset, raw + 4, 12);
        memcpy(&n.flags, raw + 0x10, 4);
        int16_t childCount;
        memcpy(&n.index, raw + 0x14, 2);
        memcpy(&childCount, raw + 0x16, 2);
        n.parent = parent;
        const int self = static_cast<int>(out.nodes.size());
        out.nodes.push_back(n);
        if (parent >= 0) out.nodes[parent].children.push_back(self);
        for (int i = 0; i < childCount; ++i)
            if (!visit(exe.U32(va + 0x18 + i * 4), self, depth + 1)) return false;
        return true;
    };
    return visit(root, -1, 0);
}

std::vector<MotionFileEntry> ListMotionFiles(const ExeImage& exe) {
    std::vector<MotionFileEntry> out;
    int misses = 0;
    for (int n = 0; misses < 8; ++n) {
        const std::string name = exe.CStr(exe.U32(kMotFileNames + n * 4));
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bin") == 0) {
            misses = 0;
            uint16_t count = 0;
            exe.Read(kMotFileCounts + n * 2, &count, 2);
            const uint32_t ids = exe.U32(kMotFileIds + n * 4);
            MotionFileEntry e{ n, name, {} };
            if (count > 0 && count < 4096 && exe.InData(ids)) {
                e.motionIds.resize(count);
                exe.Read(ids, e.motionIds.data(), count * 2);
            }
            out.push_back(std::move(e));
        } else {
            ++misses;
        }
    }
    return out;
}

int MotionTicks(const ExeImage& exe, int motionId) {
    return exe.S16(kMotionTicks + motionId * 2);
}

std::vector<Motion> SplitMotionFile(const ExeImage& exe, const MotionFileEntry& f, const std::vector<uint8_t>& file,
                                    int nodes) {
    std::vector<Motion> out;
    const size_t n = f.motionIds.size();
    if (file.size() < n * 4) return out;
    for (size_t k = 0; k < n; ++k) {
        uint32_t start, end;
        memcpy(&start, &file[k * 4], 4);
        if (k + 1 < n) memcpy(&end, &file[(k + 1) * 4], 4);
        else end = static_cast<uint32_t>(file.size());
        if (start > end || end > file.size()) continue;
        Motion m;
        m.id = f.motionIds[k];
        m.ticks = MotionTicks(exe, m.id);
        m.nodes = nodes;
        m.data.assign(file.begin() + start, file.begin() + end);
        out.push_back(std::move(m));
    }
    return out;
}

void Motion::RootPosition(int frame, float out[3]) const {
    memcpy(out, &data[4 + frame * FrameStride()], 12);
}

void Motion::Rotation(int frame, int slot, int16_t out[3]) const {
    memcpy(out, &data[4 + frame * FrameStride() + 12 + slot * 6], 6);
}

Mat4 Mat4::Identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
    return r;
}

Mat4 Mat4::Translate(float x, float y, float z) {
    Mat4 r = Identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

// Same element layout as the engine's in-place matrix-stack rotations (D3DX row-vector conventions).
Mat4 Mat4::RotateX(int a) {
    const float c = static_cast<float>(std::cos(a * kAngle)), s = static_cast<float>(std::sin(a * kAngle));
    Mat4 r = Identity();
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}

Mat4 Mat4::RotateY(int a) {
    const float c = static_cast<float>(std::cos(a * kAngle)), s = static_cast<float>(std::sin(a * kAngle));
    Mat4 r = Identity();
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
    return r;
}

Mat4 Mat4::RotateZ(int a) {
    const float c = static_cast<float>(std::cos(a * kAngle)), s = static_cast<float>(std::sin(a * kAngle));
    Mat4 r = Identity();
    r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
    return r;
}

Mat4 Mat4::operator*(const Mat4& b) const {
    Mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}

std::vector<Mat4> EvaluatePose(const Skeleton& s, const Motion& m, int tick) {
    std::vector<Mat4> out(s.nodes.size(), Mat4::Identity());
    if (s.nodes.empty() || !m.SizeConsistent()) return out;
    const int frame = tick / 2;
    const bool half = (tick & 1) != 0 && frame + 1 < m.StoredFrames();

    auto rotation = [&](int slot, int r[3]) {
        int16_t a[3];
        m.Rotation(frame, slot, a);
        for (int i = 0; i < 3; ++i) r[i] = a[i];
        if (half) {
            int16_t b[3];
            m.Rotation(frame + 1, slot, b);
            for (int i = 0; i < 3; ++i) r[i] = a[i] + AngleDelta(a[i], b[i]) / 2;
        }
    };

    float pos[3];
    m.RootPosition(frame, pos);
    if (half) {
        float next[3];
        m.RootPosition(frame + 1, next);
        for (int i = 0; i < 3; ++i) pos[i] += (next[i] - pos[i]) * 0.5f;  // _DAT_004C43AC
    }
    int r[3];
    rotation(s.nodes[0].index, r);
    out[0] = Mat4::RotateX(r[0]) * Mat4::RotateY(r[1]) * Mat4::RotateZ(r[2]) * Mat4::Translate(pos[0], pos[1], pos[2]);

    std::function<void(int)> visit = [&](int i) {
        const SkeletonNode& n = s.nodes[i];
        int a[3];
        rotation(n.index, a);
        out[i] = Mat4::RotateX(a[0]) * Mat4::RotateY(a[1]) * Mat4::RotateZ(a[2]) *
                 Mat4::Translate(n.offset[0], n.offset[1], n.offset[2]) * out[n.parent];
        for (int c : n.children) visit(c);
    };
    for (int c : s.nodes[0].children) visit(c);
    return out;
}

}  // namespace game
