// Route planner shared by world_test and frontend_test (--plan ..., notes/agents/routes.md section 4).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "game/shooting.h"
#include "game/world.h"

namespace plan {

struct Line {  // one input-script line (player 0)
    int frame, player;
    float x, y;
    int buttons;
};
// --plan (route steering, notes/agents/routes.md): with --prefix, the native world runs uncompared from the prefix
// capture's stage start; the script's lines before --plan-from are replayed, then this player decides P1's input frame
// by frame from the native world (like the hook's HOTD2_INPUT_AUTO: a shot every 2 frames at the nearest target, reload
// when empty; --plan-prefer fn[,fn]@a-b first, --plan-skip never, --plan-quiet a-b no input), every aim checked with
// World::PreviewShot (the hit task's own pick, never a civilian). Writes the script (input lines up to --plan-end) and
// prints the scene / route / civilian changes it saw. The original replays the script exactly where the port is exact.
struct Planner {
    struct Filter {
        std::vector<uint32_t> fns;
        int from = 0, to = 0x7FFFFFFF;
        bool Has(uint32_t fn, int fr) const {
            if (fr < from || fr > to) return false;
            for (uint32_t f : fns)
                if (f == 0 || f == fn) return true;
            return false;
        }
    };
    static Filter ParseFilter(const std::string& s) {
        Filter f;
        std::string list = s;
        const size_t at = s.find('@');
        if (at != std::string::npos) {
            list = s.substr(0, at);
            f.from = atoi(s.c_str() + at + 1);
            const size_t dash = s.find('-', at);
            if (dash != std::string::npos) f.to = atoi(s.c_str() + dash + 1);
        }
        for (size_t p = 0; p < list.size();) {
            char* e = nullptr;
            f.fns.push_back(static_cast<uint32_t>(strtoul(list.c_str() + p, &e, 16)));
            p = static_cast<size_t>(e - list.c_str());
            while (p < list.size() && (list[p] == ',' || list[p] == ' ')) ++p;
            if (e == list.c_str()) break;
        }
        return f;
    }
    std::string out;
    int from = -1, end = -1;
    std::vector<Filter> prefer, skip, quiet, civ;  // civ: shoot civilians first in these windows (route branches)
    std::vector<uint32_t> watch;  // task fns whose state changes are printed (civilians always)
    uint32_t debugFn = 0;         // --plan-debug fn: print its targets and the shot preview at their centre
    // state
    int lastPress = -100;
    uint8_t held = 0;
    float x = 0.5f, y = 0.5f;
    std::vector<std::string> lines;
    std::vector<int> calX, calY;  // calibrated centred value of raw pixel x / y (mode 6)
    std::map<uint32_t, int> misses;
    std::map<uint32_t, int> blocked;
    uint32_t shotSerial = 0;
    bool shotPending = false;
    int shots = 0, hits = 0, rejects = 0;

    void Init() {
        calX.resize(640);
        calY.resize(480);
        for (int i = 0; i < 640; ++i) {
            int16_t gx, gy;
            bool v;
            game::GunCalibrateMode6(i, 240, &gx, &gy, &v);
            calX[i] = v ? gx : 1 << 20;
        }
        for (int i = 0; i < 480; ++i) {
            int16_t gx, gy;
            bool v;
            game::GunCalibrateMode6(320, i, &gx, &gy, &v);
            calY[i] = v ? gy : 1 << 20;
        }
    }
    static int Best(const std::vector<int>& c, double want) {
        int best = -1;
        double bd = 1e30;
        for (size_t i = 0; i < c.size(); ++i) {
            const double d = std::fabs(c[i] - want);
            if (c[i] != (1 << 20) && d < bd) {
                bd = d;
                best = static_cast<int>(i);
            }
        }
        return bd <= 1.0 ? best : -1;
    }
    void Emit(int fr, float nx, float ny, uint8_t b) {
        char s[96];
        snprintf(s, sizeof(s), "%d 0 %.6f %.6f %d", fr, nx, ny, b);
        lines.push_back(s);
        x = nx;
        y = ny;
        held = b;
    }
    // Input of frame fr (decided after frame fr - 1). Returns true when a line was added (caller feeds it to the script).
    bool Decide(game::World& w, int fr, plan::Line* line) {
        // feedback of the last shot: the world's hit flag is not public; use the target's disappearance from blocking
        const uint8_t before = held;
        bool changed = false;
        auto emit = [&](float nx, float ny, uint8_t b) {
            Emit(fr, nx, ny, b);
            *line = plan::Line{ fr, 0, nx, ny, b };
            changed = true;
        };
        for (const Filter& q : quiet)
            if (fr >= q.from && fr <= q.to) {
                if (before != 0) emit(x, y, 0);
                return changed;
            }
        if (before != 0) {  // release
            emit(x, y, 0);
            return changed;
        }
        const game::WorldHud& h = w.Hud();
        const auto& P = h.player[0];
        if (w.GameMode() != 6 || P.state != 5 || P.lives <= 0 || !h.canShoot) return false;
        if (fr - lastPress < 2) return false;
        if (P.ammo <= 0) {
            lastPress = fr;
            emit(x, y, 2);
            return true;
        }
        const float F = w.Focal();
        const auto targets = w.ShotTargets();
        if (debugFn)
            for (const auto& t : targets) {
                if (t.fn != debugFn && debugFn != 1) continue;
                const int rx = Best(calX, F * t.view[0] / -t.view[2]), ry = Best(calY, F * t.view[1] / -t.view[2]);
                const auto pv = rx >= 0 && ry >= 0 ? w.PreviewShot(0, rx, ry) : game::World::ShotPreview{};
                printf("plan %d debug: %X serial %u flags %X view %.2f %.2f %.2f r %.2f nodes %zu -> pixel %d %d hits %s %X/%u node %d\n", fr,
                       t.fn, t.serial, t.flags, t.view[0], t.view[1], t.view[2], t.radius, t.nodes.size(), rx, ry,
                       pv.any ? (pv.level ? "level" : "object") : "nothing", pv.fn, pv.serial, pv.node);
            }
        struct Cand {
            double score;
            size_t i;
        };
        std::vector<Cand> cands;
        for (size_t i = 0; i < targets.size(); ++i) {
            const auto& t = targets[i];
            bool civOk = false;
            for (const Filter& f : civ) civOk = civOk || (fr >= f.from && fr <= f.to);
            if ((t.civilian && !civOk) || (t.flags & 0x4000000) || (t.flags & 0x8000) || (t.flags & 0x10)) continue;
            if (t.view[2] > -1.0f) continue;
            bool sk = false;
            for (const Filter& f : skip) sk = sk || f.Has(t.fn, fr);
            if (sk && !(t.civilian && civOk)) continue;
            auto bl = blocked.find(t.serial);
            if (bl != blocked.end() && fr < bl->second) continue;
            const double dist = -t.view[2];
            double score = !t.nodes.empty() ? dist : dist * 3.0 + 100.0;
            for (const Filter& f : prefer)
                if (f.Has(t.fn, fr)) score = -300000.0 + dist;
            if (t.civilian) score = -400000.0 + dist;
            cands.push_back({ score, i });
        }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score < b.score; });
        for (const Cand& c : cands) {
            const auto& t = targets[c.i];
            std::vector<std::array<float, 4>> pts = t.nodes;
            std::stable_sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) { return a[3] > b[3]; });
            if (pts.empty()) pts.push_back({ t.view[0], t.view[1], t.view[2], t.radius });
            for (const auto& p : pts) {
                if (p[2] > -1.0f) continue;
                // the point and four offsets inside its sphere
                static const float off[5][2] = { { 0, 0 }, { 0.5f, 0 }, { -0.5f, 0 }, { 0, 0.5f }, { 0, -0.5f } };
                for (const auto& o : off) {
                    const double vx = p[0] + o[0] * p[3], vy = p[1] + o[1] * p[3];
                    const int rx = Best(calX, F * vx / -p[2]), ry = Best(calY, F * vy / -p[2]);
                    if (rx < 0 || ry < 0) continue;
                    const game::World::ShotPreview pv = w.PreviewShot(0, rx, ry);
                    if (!pv.any || pv.serial != t.serial) {
                        if (pv.civilian) ++rejects;
                        continue;
                    }
                    lastPress = fr;
                    ++shots;
                    shotSerial = t.serial;
                    emit((rx + 0.5f) / 640.0f, (ry + 0.5f) / 480.0f, 1);
                    return true;
                }
            }
            // no aim reaches it first: try it again later
            if (++misses[t.serial] >= 3) {
                blocked[t.serial] = fr + 30;
                misses[t.serial] = 0;
            }
        }
        if (P.ammo < 6) {
            lastPress = fr;
            emit(x, y, 2);
            return true;
        }
        return false;
    }
};

}  // namespace plan
