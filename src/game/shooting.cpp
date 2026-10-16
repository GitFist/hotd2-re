#include "shooting.h"

#include <cmath>

#include "camera.h"

namespace game {

namespace {
constexpr double kFromAngle = 6.283185307179586 / 65536.0;  // double at 0x004C4370
}

// 0x00415710
DemoShot DemoAutoGun(CrtRand& rng, int player, bool enemiesAlive) {
    DemoShot s{};
    const int roll = rng.Next() % 60;          // call at 0x00415729 (no enemy) / 0x0041573B (enemies)
    s.fired = roll < (enemiesAlive ? 6 : 1);  // cmp edx, 1 / cmp edx, 6; setl
    if (s.fired) {
        s.x = static_cast<int16_t>((rng.Next() % 150) * 2 - 150);  // 0x00415752
        s.y = static_cast<int16_t>((rng.Next() % 100) * 2 - 100);  // 0x00415766
    } else {
        // 0x00415779: both words come from the record pointer argument.
        const uint32_t rec = kGunRecordBase + static_cast<uint32_t>(player) * kGunRecordStride;
        s.x = s.y = static_cast<int16_t>(rec & 0xFFFF);
    }
    return s;
}

// 0x00415785..0x00415792
void ApplyDemoShot(const DemoShot& s, GunRecord& rec) {
    rec.x = s.x;
    rec.y = s.y;
    rec.fired = s.fired ? 1 : 0;
    rec.valid = 1;
}

// 0x00414940 (normal weapon; reload = 0x00414B30)
TriggerResult PlayerTrigger(const TriggerInput& in, int16_t ammo) {
    TriggerResult r;
    r.ammo = ammo;
    auto reload = [&] {
        r.ammo = in.fullAmmo;
        r.empty = false;
        r.action = TriggerAction::Reload;
    };
    if (in.fired) {
        if (!in.valid || r.ammo == 0) {
            if (in.gunDevice && !in.valid && r.ammo != in.fullAmmo) reload();  // off-screen shot reloads
        } else if (in.canShoot) {
            if (!in.demo && !in.infiniteAmmo) --r.ammo;
            if (r.ammo == 0) r.empty = true;
            r.action = TriggerAction::Shot;  // player +0x18 = 1, Shot_BuildRay, tracer 0x00416F70, gun sound
        }
    }
    if (in.padReload && r.ammo != in.fullAmmo) reload();
    return r;
}

// 0x00418528..0x0041853B
float ShotFocalLength(double halfFov) {
    return static_cast<float>(240.0 / std::tan(halfFov));
}

// 0x00406110 (view-space part; the world origin/direction use the camera matrix 0x009A6040 + ctx*0x1A4)
ShotRay MakeShotRay(float x, float y, float focal) {
    ShotRay r{};
    const float v[3] = { x, y, -focal };
    const double len = std::sqrt(static_cast<double>(v[0]) * v[0] + static_cast<double>(v[1]) * v[1] +
                                 static_cast<double>(v[2]) * v[2]);  // Vec_Normalize 0x004AAA00
    for (int i = 0; i < 3; ++i) r.dir[i] = static_cast<float>(v[i] / len);
    const float zero[3] = { 0, 0, 0 };
    LookAngles(r.dir, zero, &r.pitch, &r.yaw);  // Cam_DirToAngles(dir) 0x004016B0
    r.sinP = static_cast<float>(std::sin(-r.pitch * kFromAngle));
    r.cosP = static_cast<float>(std::cos(-r.pitch * kFromAngle));
    r.sinY = static_cast<float>(std::sin(-r.yaw * kFromAngle));
    r.cosY = static_cast<float>(std::cos(-r.yaw * kFromAngle));
    return r;
}

// 0x004062A0
float ShotRayDistance(const ShotRay& r, float x, float y, float z) {
    const double a = -(static_cast<double>(x) * r.cosY) - static_cast<double>(z) * r.sinY;
    const double b = static_cast<double>(z) * r.cosY * r.sinP - (static_cast<double>(y) * r.cosP + static_cast<double>(x) * r.sinY * r.sinP);
    return static_cast<float>(std::sqrt(b * b + a * a));
}

// 0x00405160
bool IsShotTarget(uint32_t flags, float viewZ) {
    return (flags & 0x8000) == 0 && ((flags & 0x10) != 0 || viewZ <= 0.0f);
}

// 0x0040468C..0x0040469D
int32_t HitSortKey(float viewZ) {
    return static_cast<int32_t>(-(static_cast<double>(viewZ) * 10.0));  // __ftol truncates
}

// 0x00405080
void SortHitKeys(std::vector<std::pair<uint32_t, int32_t>>& e) {
    std::vector<std::pair<uint32_t, int32_t>> tmp(e.size());
    for (int shift = 0; shift < 16; shift += 8) {
        int count[256] = {};
        for (const auto& x : e) ++count[(x.first >> shift) & 0xFF];
        for (int i = 1; i < 256; ++i) count[i] += count[i - 1];
        for (size_t i = e.size(); i-- > 0;) tmp[--count[(e[i].first >> shift) & 0xFF]] = e[i];
        e = tmp;
    }
}

// 0x0040D4F0
int GunCalibrate(int raw, const int16_t cal[4], const int16_t out[3], int wrap) {
    int v = raw - cal[3];
    if (v < 0) v += wrap;  // 0x009A6694
    const int p0 = cal[0], p1 = cal[1], p2 = cal[2];
    if ((p2 <= p0) != (p1 <= v)) {
        if (p2 == p1) return 0;
        return ((out[2] - out[1]) * (v - p1)) / (p2 - p1) + out[1];
    }
    if (p0 == p1) return 0;
    return ((out[0] - out[1]) * (v - p1)) / (p0 - p1) + out[1];
}

// 0x0040CD37 (device mode 6 path of 0x0040CBB0)
void GunCalibrateMode6(int rawX, int rawY, int16_t* x, int16_t* y, bool* valid) {
    static const int16_t calX[4] = { -265, 0, 265, 320 }, outX[3] = { -264, 0, 264 };
    static const int16_t calY[4] = { 50, 240, 430, 0 }, outY[3] = { 192, 0, -192 };
    const int cx = GunCalibrate(rawX, calX, outX), cy = GunCalibrate(rawY, calY, outY);
    // Gun_CalibrateClamped 0x0040D3E0: out of range -> 0 and invalid
    const bool okX = cx >= -320 && cx <= 319, okY = cy >= -240 && cy <= 239;
    *x = static_cast<int16_t>(okX ? cx : 0);
    *y = static_cast<int16_t>(okY ? cy : 0);
    *valid = okX && okY;
}

}  // namespace game
