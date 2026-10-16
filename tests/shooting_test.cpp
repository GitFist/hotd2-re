// shooting_test <game dir> <capture with Z/N/K/T lines (HOTD2_SCENECAP_TASKS=...,4533F0,465FB0,...)>
// Validates game/shooting.* against the ORIGINAL game (kept capture: build\scene_capture_actors.txt, stage-1 demo):
//   1. unit checks: CRT rand sequence, mode-6 gun calibration, trigger/reload rules, focal length vs the captured
//      projection.
//   2. rand integrity: every frame's rand state (Z) equals the previous state advanced by the frame's rand calls (N).
//   3. demo auto-gun replay: starting from the previous frame's rand state, DemoAutoGun for P1 then P2 must reproduce
//      the frame's calls from 0x00415710 exactly (roll site 0x0041572E/0x00415740 = no enemy/enemy rate, aim sites
//      0x00415757/0x0041576B = number of shots).
//   4. hit test: every replayed shot is tested (MakeShotRay + ShotRayDistance) against the shootable objects of the
//      previous frame's task dumps (the hit task 0x00404570 runs before the actors re-register):
//      - enemies (0x004533F0): a captured enemy hit (rand at 0x004093E2, Enemy damage 0x004093C0) must have a ray
//        within an enemy's body sphere; a shot that misses every body sphere must not hit;
//      - props (0x00465FB0, no node test): the nearest sphere hit (HitSortKey) must be exactly the prop whose +0x32C
//        goes from 0 to non-zero in that frame, and no prop may change without a predicted hit.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "game/camera.h"
#include "game/exe_image.h"
#include "game/shooting.h"

namespace {

int g_fail = 0;
void Check(bool ok, const char* what) {
    if (!ok) {
        ++g_fail;
        printf("FAIL: %s\n", what);
    }
}

struct Task {
    uint32_t fn = 0;
    std::vector<uint32_t> w;
    uint32_t U(int off) const { return off / 4 < static_cast<int>(w.size()) ? w[off / 4] : 0; }
    float F(int off) const {
        const uint32_t u = U(off);
        float f;
        memcpy(&f, &u, 4);
        return f;
    }
};

struct Frame {
    bool hasZ = false;
    uint32_t z = 0;
    std::map<uint32_t, int> rand;    // caller return address -> calls
    std::map<uint32_t, Task> tasks;  // serial -> dump
};

int Calls(const Frame& f, uint32_t a) {
    auto it = f.rand.find(a);
    return it == f.rand.end() ? 0 : it->second;
}

void UnitChecks(const game::ExeImage& exe, float* focal) {
    game::CrtRand r;  // srand(1)
    Check(r.Next() == 41 && r.Next() == 18467 && r.Next() == 6334 && r.Next() == 26500, "CRT rand sequence for seed 1");

    int16_t x, y;
    bool valid;
    game::GunCalibrateMode6(320, 240, &x, &y, &valid);
    Check(x == 0 && y == 0 && valid, "mode-6 calibration centre");
    game::GunCalibrateMode6(585, 430, &x, &y, &valid);
    Check(x == 264 && y == -192 && valid, "mode-6 calibration bottom right point");
    game::GunCalibrateMode6(55, 50, &x, &y, &valid);
    Check(x == -264 && y == 192 && valid, "mode-6 calibration top left point");
    game::GunCalibrateMode6(0, 0, &x, &y, &valid);
    Check(x == -318 && y == 0 && !valid, "mode-6 calibration: y above the calibrated range is invalid");

    game::TriggerInput in{};
    in.fired = in.valid = in.gunDevice = in.canShoot = true;
    auto t = game::PlayerTrigger(in, 6);
    Check(t.action == game::TriggerAction::Shot && t.ammo == 5, "shot consumes a bullet");
    t = game::PlayerTrigger(in, 1);
    Check(t.action == game::TriggerAction::Shot && t.ammo == 0 && t.empty, "last bullet empties the gun");
    t = game::PlayerTrigger(in, 0);
    Check(t.action == game::TriggerAction::None && t.ammo == 0, "empty gun on screen does nothing");
    in.valid = false;
    t = game::PlayerTrigger(in, 0);
    Check(t.action == game::TriggerAction::Reload && t.ammo == 6, "off-screen trigger reloads");
    t = game::PlayerTrigger(in, 6);
    Check(t.action == game::TriggerAction::None, "off-screen trigger with a full gun does nothing");
    in.valid = true;
    in.demo = true;
    t = game::PlayerTrigger(in, 6);
    Check(t.action == game::TriggerAction::Shot && t.ammo == 6, "demo shots do not consume ammo");
    in.demo = false;
    in.canShoot = false;
    t = game::PlayerTrigger(in, 6);
    Check(t.action == game::TriggerAction::None && t.ammo == 6, "no shots while the letterbox is closed");

    std::vector<std::pair<uint32_t, int32_t>> keys = { { 592, 0 }, { 465, 1 }, { static_cast<uint32_t>(-129), 2 }, { 3481, 3 },
                                                       { 465, 4 }, { 0x10000 + 5, 5 } };
    game::SortHitKeys(keys);
    const int order[] = { 5, 1, 4, 0, 3, 2 };  // bits 0-15 only, stable, negative keys last
    bool ok = true;
    for (int i = 0; i < 6; ++i) ok = ok && keys[i].second == order[i];
    Check(ok, "radix sort on the low 16 key bits (stable)");

    double halfFov = 0;
    exe.Read(game::kHalfFovVa, &halfFov, 8);
    *focal = game::ShotFocalLength(halfFov);
    const float projY = game::DeviceProjection().m[5] * 240.0f, projX = game::DeviceProjection().m[0] * 320.0f;
    printf("focal length %.4f (projection: %.4f / %.4f)\n", *focal, projY, projX);
    Check(std::fabs(*focal - projY) < 1e-3f && std::fabs(*focal - projX) < 0.05f, "focal length matches the projection");

    const game::ShotRay centre = game::MakeShotRay(0, 0, *focal);
    const float p[3] = { 0, 0, -100 };
    Check(game::ShotRayDistance(centre, p[0], p[1], p[2]) < 1e-4f, "centre ray passes through the view axis");
    const game::ShotRay side = game::MakeShotRay(100, 50, *focal);
    const float s = 300.0f / *focal;  // the point on the ray at depth 300
    Check(game::ShotRayDistance(side, 100 * s, 50 * s, -300) < 0.05f, "off-centre ray passes through its own points");
    Check(game::ShotRayDistance(side, 100 * s + 3, 50 * s, -300) > 2.5f, "distance grows sideways");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: shooting_test <game dir> <capture>\n");
        return 2;
    }
    game::ExeImage exe;
    if (!exe.Load(std::string(argv[1]) + "/Hod2.exe")) return 2;
    float focal = 0;
    UnitChecks(exe, &focal);

    std::map<int, Frame> frames;
    {
        std::ifstream cap(argv[2]);
        if (!cap) {
            fprintf(stderr, "cannot open %s\n", argv[2]);
            return 2;
        }
        Frame cur;
        for (std::string line; std::getline(cap, line);) {
            if (line.size() < 1) continue;
            std::istringstream ss(line.size() > 2 ? line.substr(2) : std::string());
            if (line[0] == 'Z') {
                std::string h;
                ss >> h;
                cur.z = static_cast<uint32_t>(std::stoul(h, nullptr, 16));
                cur.hasZ = true;
            } else if (line[0] == 'N') {
                for (std::string tok; ss >> tok;) {
                    const size_t c = tok.find(':');
                    cur.rand[static_cast<uint32_t>(std::stoul(tok.substr(0, c), nullptr, 16))] = std::stoi(tok.substr(c + 1));
                }
            } else if (line[0] == 'T') {
                uint32_t serial;
                std::string fn;
                int header;
                ss >> serial >> fn >> header;
                Task t;
                t.fn = static_cast<uint32_t>(std::stoul(fn, nullptr, 16));
                for (std::string tok; ss >> tok;) t.w.push_back(static_cast<uint32_t>(std::stoul(tok, nullptr, 16)));
                cur.tasks[serial] = std::move(t);
            } else if (line[0] == 'F') {
                int f;
                ss >> f;
                if (cur.hasZ) frames[f] = std::move(cur);
                cur = Frame();
            }
        }
    }

    // 2. rand integrity
    int zOk = 0, zBad = 0;
    for (const auto& [f, fr] : frames) {
        auto prev = frames.find(f - 1);
        if (prev == frames.end()) continue;
        int n = 0;
        for (const auto& [a, c] : fr.rand) n += c;
        game::CrtRand r{ prev->second.z };
        for (int i = 0; i < n; ++i) r.Next();
        (r.state == fr.z ? zOk : zBad)++;
    }
    printf("rand state chain: %d frames consistent, %d not\n", zOk, zBad);

    // 3. demo auto-gun replay
    struct Shot { int frame, player; int16_t x, y; };
    std::vector<Shot> shots;
    int demoFrames = 0, demoOk = 0, demoBad = 0;
    for (const auto& [f, fr] : frames) {
        auto prev = frames.find(f - 1);
        if (prev == frames.end()) continue;
        const int quiet = Calls(fr, 0x0041572E), alert = Calls(fr, 0x00415740);
        if (quiet + alert == 0) continue;
        ++demoFrames;
        const bool enemies = alert > 0;
        game::CrtRand r{ prev->second.z };
        int fired = 0;
        std::vector<Shot> mine;
        for (int p = 0; p < 2; ++p) {
            const game::DemoShot s = game::DemoAutoGun(r, p, enemies);
            if (s.fired) {
                ++fired;
                mine.push_back({ f, p, s.x, s.y });
            }
        }
        const bool ok = quiet + alert == 2 && (quiet == 0 || alert == 0) && Calls(fr, 0x00415757) == fired &&
                        Calls(fr, 0x0041576B) == fired;
        if (ok) {
            ++demoOk;
            shots.insert(shots.end(), mine.begin(), mine.end());
        } else {
            ++demoBad;
            printf("  demo replay mismatch at frame %d (rolls %d/%d, aims %d, predicted %d)\n", f, quiet, alert,
                   Calls(fr, 0x00415757), fired);
        }
    }
    printf("demo auto-gun: %d frames, %d reproduced, %d not; %zu shots\n", demoFrames, demoOk, demoBad, shots.size());
    Check(demoFrames > 0 && demoBad == 0, "demo auto-gun replay");

    // 4. hit test against the previous frame's shootable objects
    constexpr uint32_t kEnemy = 0x004533F0, kProp = 0x00465FB0;
    int propPredicted = 0, propConfirmed = 0, propBad = 0;
    std::set<std::pair<int, uint32_t>> predictedProp;
    std::map<int, bool> frameInSphere;  // shot frame -> some shot passed an enemy body sphere
    for (const Shot& s : shots) {
        const auto prev = frames.find(s.frame - 1), cur = frames.find(s.frame);
        if (prev == frames.end()) continue;
        const game::ShotRay ray = game::MakeShotRay(s.x, s.y, focal);
        bool enemyInSphere = false;
        int bestKey = 0x7FFFFFFF;
        uint32_t best = 0, bestFn = 0;
        for (const auto& [serial, t] : prev->second.tasks) {
            const uint32_t flags = t.U(0x34);
            const float v[3] = { t.F(0x70), t.F(0x74), t.F(0x78) };
            if (v[0] == 0 && v[1] == 0 && v[2] == 0) continue;  // never updated: not registered yet
            if (!game::IsShotTarget(flags, v[2]) || (flags & 0x10)) continue;  // collision-mesh targets not modelled
            if (!game::ShotHitsSphere(ray, v, t.F(0x124))) continue;
            if (t.fn == kEnemy) enemyInSphere = true;
            const int key = static_cast<int>(static_cast<uint32_t>(game::HitSortKey(v[2])) & 0xFFFF);
            if (key < bestKey) bestKey = key, best = serial, bestFn = t.fn;
        }
        frameInSphere[s.frame] = frameInSphere[s.frame] || enemyInSphere;
        if (bestFn == kProp) {
            ++propPredicted;
            predictedProp.insert({ s.frame, best });
        }
        (void)cur;
    }
    int enemyHits = 0, enemyHitsInSphere = 0, enemyMissOutside = 0, enemyInSphereNoHit = 0;
    for (const auto& [f, inSphere] : frameInSphere) {
        const bool enemyHit = Calls(frames[f], 0x004093E2) > 0;
        if (enemyHit) {
            ++enemyHits;
            if (inSphere) ++enemyHitsInSphere;
            else printf("  enemy hit at frame %d but no shot is inside a body sphere\n", f);
        } else if (!inSphere) {
            ++enemyMissOutside;
        } else {
            ++enemyInSphereNoHit;  // passed the body sphere, missed the node spheres (not in the dumps)
        }
    }
    // every captured enemy hit must come from a replayed shot frame
    for (const auto& [f, fr] : frames)
        if (Calls(fr, 0x004093E2) > 0 && !frameInSphere.count(f)) {
            ++enemyHits;
            printf("  enemy hit at frame %d without a replayed shot\n", f);
        }
    // prop reactions: +0x32C goes 0 -> non-zero when the prop handles its shot flag (0x00465FB0)
    for (const auto& [f, fr] : frames) {
        const auto prev = frames.find(f - 1);
        if (prev == frames.end()) continue;
        for (const auto& [serial, t] : fr.tasks) {
            if (t.fn != kProp) continue;
            const auto pt = prev->second.tasks.find(serial);
            if (pt == prev->second.tasks.end()) continue;
            const bool shot = pt->second.U(0x32C) == 0 && t.U(0x32C) != 0;
            const bool predicted = predictedProp.count({ f, serial }) > 0;
            if (shot && predicted) ++propConfirmed;
            else if (shot || predicted) {
                ++propBad;
                printf("  prop %u frame %d: reacted %d, predicted %d\n", serial, f, shot, predicted);
            }
        }
    }
    printf("enemy hits: %d captured, %d inside a body sphere; shot frames outside every body sphere: %d (none hit);\n"
           "  inside a body sphere without a hit (node test): %d\n",
           enemyHits, enemyHitsInSphere, enemyMissOutside, enemyInSphereNoHit);
    printf("prop hits: %d predicted (nearest sphere), %d confirmed by the prop, %d mismatches\n", propPredicted, propConfirmed, propBad);
    Check(enemyHits > 0 && enemyHits == enemyHitsInSphere, "every captured enemy hit is inside a body sphere");
    Check(propBad == 0 && propConfirmed == propPredicted, "prop hits = nearest sphere hits");

    printf(g_fail ? "%d FAILURES\n" : "all checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
