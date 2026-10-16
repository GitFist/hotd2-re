#include "camera.h"

#include <cmath>
#include <cstring>

#include "game/shoot_camera.h"

namespace game {

namespace {
constexpr uint32_t kCamFileNames = 0x004D1BC8;
constexpr uint32_t kCamFileIds = 0x004C470C;
constexpr uint32_t kCamFileCounts = 0x004C476C;
}  // namespace

std::vector<CameraFileEntry> ListCameraFiles(const ExeImage& exe) {
    std::vector<CameraFileEntry> out;
    for (int n = 1; n < 64; ++n) {
        const std::string name = exe.CStr(exe.U32(kCamFileNames + n * 4));
        if (name.size() < 5 || name.compare(name.size() - 4, 4, ".bin") != 0) break;
        uint16_t count = 0;
        exe.Read(kCamFileCounts + n * 2, &count, 2);
        CameraFileEntry e{ n, name, {} };
        if (count > 0) {
            e.cameraIds.resize(count);
            exe.Read(exe.U32(kCamFileIds + n * 4), e.cameraIds.data(), count * 2);
        }
        out.push_back(std::move(e));
    }
    return out;
}

bool CameraFile::Load(std::vector<uint8_t> data, int count) {
    data_ = std::move(data);
    count_ = count;
    base_ = 4 + 4 * static_cast<size_t>(count);
    return data_.size() >= base_;
}

void CameraFile::Channels(int k, int32_t out[8]) const {
    uint32_t off;
    memcpy(&off, &data_[k * 4], 4);
    memcpy(out, &data_[off], 32);
}

float CameraFile::EvalCurve(int32_t word, float t) const {
    const uint8_t* c = &data_[base_ + static_cast<size_t>(word) * 4];
    uint16_t n0, n1;
    memcpy(&n0, c, 2);
    memcpy(&n1, c + 2, 2);
    auto key = [&](int i, int field) {
        float v;
        memcpy(&v, c + 4 + i * 16 + field * 4, 4);
        return v;
    };
    int step = n0 >> 2;
    int idx = n0 >> 1;
    for (int i = n1 - 1; i > 0; --i) {
        idx += (t <= key(idx, 0)) ? -step : step;
        step /= 2;
    }
    if (key(idx, 0) < t) ++idx;
    // Hermite between keys idx-1 and idx, in the x87 operation order of 0x0040414E..0x004041D3. The game runs
    // the FPU with 24-bit precision control (Direct3D's FPU setup), so every add/sub/mul/div rounds to a float
    // significand: float arithmetic reproduces it.
    const float t0 = key(idx - 1, 0), v0 = key(idx - 1, 1), a0 = key(idx - 1, 2);
    const float t1 = key(idx, 0), v1 = key(idx, 1), b1 = key(idx, 3);
    const float s = t - t0;
    const float s2 = s * s;
    const float s3 = s2 * s;
    const float h = t1 - t0;
    const float h2 = h * h;
    const float h3 = h2 * h;
    const float a = s3 / h2;
    const float b = (s3 + s3) / h3;
    const float c3 = (s2 * 3.0f) / h2;
    const float f = (a - (s2 + s2) / h) + s;
    const float g = a - s2 / h;
    const float sum = (f * a0 + g * b1) + ((b - c3) + 1.0f) * v0;
    return sum + (c3 - b) * v1;
}

void CameraFile::Evaluate(int k, float t, float pos[3], float target[3], int* roll) const {
    int32_t ch[8];
    Channels(k, ch);
    for (int i = 0; i < 3; ++i) pos[i] = EvalCurve(ch[i], t);
    for (int i = 0; i < 3; ++i) target[i] = EvalCurve(ch[3 + i], t);
    if (roll) *roll = static_cast<int>(EvalCurve(ch[6], t));
}

float CameraFile::Duration(int k) const {
    int32_t ch[8];
    Channels(k, ch);
    const uint8_t* c = &data_[base_ + static_cast<size_t>(ch[0]) * 4];
    uint16_t n0;
    memcpy(&n0, c, 2);
    float last;
    memcpy(&last, c + 4 + (n0 - 1) * 16, 4);  // padding keys repeat the last key
    return last;
}

bool CameraBank::Load(const ExeImage& exe, const std::function<std::vector<uint8_t>(const std::string&)>& read) {
    files_.clear();
    ids_.clear();
    for (const auto& f : ListCameraFiles(exe)) {
        if (f.cameraIds.empty()) continue;
        std::vector<uint8_t> data = read(f.name);
        if (data.empty()) continue;
        auto cf = std::make_unique<CameraFile>();
        if (!cf->Load(std::move(data), static_cast<int>(f.cameraIds.size()))) continue;
        for (size_t k = 0; k < f.cameraIds.size(); ++k) ids_[f.cameraIds[k]] = { cf.get(), static_cast<int>(k) };
        files_.push_back(std::move(cf));
    }
    return !ids_.empty();
}

bool CameraBank::Evaluate(int id, float t, float pos[3], float target[3], int* roll, bool rollEnabled) const {
    auto it = ids_.find(id);
    if (it == ids_.end()) return false;
    int r = 0;
    // channel 6 is only read when the roll channel is on (Cam_Evaluate 0x004041E0); object paths (e.g. 0xFD..0xFF of
    // the stage-1 intro, read by 0x004042D0) have no valid roll channel word
    it->second.first->Evaluate(it->second.second, t, pos, target, rollEnabled ? &r : nullptr);
    if (roll) *roll = rollEnabled ? r : 0;
    return true;
}

void LookAngles(const float pos[3], const float target[3], int* pitch, int* yaw) {
    // 0x00403AC0 -> 0x004016B0 with the game's 24-bit FPU precision (game::DirToAngles, shoot_camera.cpp)
    DirToAngles(pos[0] - target[0], pos[1] - target[1], pos[2] - target[2], pitch, yaw);
}

Mat4 ViewMatrix(const float pos[3], int pitch, int yaw, int roll) {
    // Matrix-stack sequence of Cam_BuildViews 0x00401F40 (bit-exact with the engine's float arithmetic)
    MtxStack s;
    s.LoadIdentity();
    s.RotateZ(-roll);
    s.RotateX(-pitch);
    s.RotateY(-yaw);
    s.Translate(-pos[0], -pos[1], -pos[2]);
    Mat4 m;
    s.Store(m.m);
    return m;
}

Mat4 DeviceView() {
    Mat4 m = Mat4::Identity();
    m.m[10] = -1;
    return m;
}

Mat4 DeviceProjection() {
    // Captured from the original (scene_capture.txt "T 3" lines), 640x480 mode.
    Mat4 m{};
    m.m[0] = 2.00065f;
    m.m[5] = 2.667533f;
    m.m[10] = 1.0001f;
    m.m[11] = 1.0f;
    m.m[14] = -0.80008f;
    return m;
}

}  // namespace game
