// props_test <game dir> <stage table> <actor capture>
// Replays the event spawn records of a capture (HOTD2_SCENECAP_EVENTS E lines + HOTD2_SCENECAP_TASKS K/T/M lines,
// e.g. build\scene_capture_actors.txt with st1evtbl.bin) through game::TaskSystem and the ported prop actors, and
// compares every frame with the ORIGINAL:
//   - task creation: the prop tasks get the same serials (creation order) and functions as the original's;
//   - task state (T dumps): hanging prop rotations/frame, breakable prop frame, decor phase, animated prop frame;
//   - draws (M lines tagged with the task serial): same models in the same order, matrices W * V within tolerance,
//     V = the frame's view (draw of the stage task 0x00401260, whose world is the identity).
// Inputs NOT simulated (taken from the capture, reported): the hit system's shot (frame where the original's
// break animation starts), motion residency (frames where a prop has no draw at all), shadow ground height
// (0x009C8E58, from the first shadow draw) and the camera path/time (C line).

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "game/props.h"

namespace fs = std::filesystem;
using game::Mat4;

namespace {

constexpr uint32_t kFnHanging = 0x00473CF0, kFnBreakable = 0x00465FB0, kFnDecor = 0x0046DAB0,
                   kFnAnimated = 0x00474120, kFnStage = 0x00401260;

struct MLine {
    int model;
    float m[16];
};
struct FrameData {
    std::vector<std::pair<int, uint32_t>> K;               // serial, fn
    std::map<int, std::vector<uint32_t>> T;                // serial -> words from +0
    std::map<int, std::vector<MLine>> M;                   // serial -> draws (prop tasks + the stage task)
    std::vector<std::vector<uint32_t>> E;                  // op, words after the opcode
    int camPath = 0, camTime = 0;
};

std::vector<uint8_t> ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

bool ParseCapture(const char* path, std::map<int, FrameData>& frames) {
    std::ifstream cap(path);
    if (!cap) return false;
    FrameData cur;
    std::map<int, std::vector<MLine>> allM;
    for (std::string line; std::getline(cap, line);) {
        if (line.size() < 2 || line[1] != ' ') continue;
        const char k = line[0];
        std::istringstream ss(line.substr(2));
        if (k == 'M') {
            MLine m;
            ss >> m.model;
            for (float& v : m.m) ss >> v;
            std::string tag;
            ss >> tag;
            if (tag.size() > 1 && tag[0] == '^') allM[std::stoi(tag.substr(1))].push_back(m);
        } else if (k == 'E') {
            unsigned f, op;
            std::string addr;
            ss >> f >> op >> addr;
            std::vector<uint32_t> e{ op };
            for (std::string w; ss >> w;) e.push_back(static_cast<uint32_t>(std::stoul(w, nullptr, 16)));
            cur.E.push_back(e);
        } else if (k == 'C') {
            ss >> cur.camPath >> cur.camTime;
        } else if (k == 'K') {
            for (std::string e; ss >> e;) {
                int s, par;
                unsigned fn;
                if (sscanf(e.c_str(), "%d:%x:%d", &s, &fn, &par) == 3) cur.K.push_back({ s, fn });
            }
        } else if (k == 'T') {
            int serial;
            std::string fn;
            int hdr;
            ss >> serial >> fn >> hdr;
            const uint32_t f = static_cast<uint32_t>(std::stoul(fn, nullptr, 16));
            if (f == kFnHanging || f == kFnBreakable || f == kFnDecor || f == kFnAnimated) {
                std::vector<uint32_t> w;
                for (std::string x; ss >> x;) w.push_back(static_cast<uint32_t>(std::stoul(x, nullptr, 16)));
                cur.T[serial] = std::move(w);
            }
        } else if (k == 'F') {
            int n;
            ss >> n;
            for (auto& [s, fn] : cur.K)
                if (fn == kFnHanging || fn == kFnBreakable || fn == kFnDecor || fn == kFnAnimated || fn == kFnStage) {
                    auto it = allM.find(s);
                    if (it != allM.end()) cur.M[s] = it->second;
                }
            frames[n] = std::move(cur);
            cur = FrameData{};
            allM.clear();
        }
    }
    return true;
}

float WordF(const std::vector<uint32_t>& w, int off) {
    float f;
    memcpy(&f, &w[off / 4], 4);
    return f;
}
int WordI(const std::vector<uint32_t>& w, int off) { return static_cast<int32_t>(w[off / 4]); }

// ---------------------------------------------------------------- simulation context

struct Sim {
    game::ExeImage exe;
    std::string gameDir;
    game::EventMemory mem;
    game::TaskSystem ts;
    game::PropWorld w;
    game::HangingTables hanging;
    game::BreakableTables breakable;
    std::map<int, game::PropSkeleton> skels;
    std::map<int, game::Motion> motions;
    Mat4 view;
    int frame = 0;
    // inputs from the capture
    std::map<int, int> shotFrame;            // serial -> frame of the shot
    std::set<std::pair<int, int>> noMotion;  // (serial, frame) with no draw in the original
    // outputs per frame
    std::map<int, std::vector<game::PropDraw>> draws;
    std::map<int, uint32_t> serialFn;
    std::map<int, const game::TaskData*> state;

    const game::PropSkeleton& Skel(int t) {
        auto it = skels.find(t);
        if (it == skels.end()) {
            game::PropSkeleton s;
            s.Load(exe, t);
            it = skels.emplace(t, std::move(s)).first;
        }
        return it->second;
    }
    const game::Motion& Mot(int id) {
        auto it = motions.find(id);
        if (it == motions.end()) {
            game::Motion m;
            if (!game::LoadMotionById(exe, gameDir, id, m)) printf("motion %d not found\n", id);
            it = motions.emplace(id, std::move(m)).first;
        }
        return it->second;
    }
};
Sim* g;

struct SpawnActor : game::TaskData {
    game::SpawnRecord rec;
    int op = 0;
};

void Register(game::Task& t, uint32_t fn) {
    g->serialFn[static_cast<int>(t.serial)] = fn;
    g->state[static_cast<int>(t.serial)] = t.data.get();
}

void HangingFn(game::Task& t) {
    auto& p = t.As<game::HangingProp>();
    if (p.Update(g->w, g->hanging) != game::PropResult::Alive) g->ts.KillCurrent();
    g->draws[static_cast<int>(t.serial)].push_back(p.Draw(g->w));
}

void BreakableFn(game::Task& t) {
    auto& p = t.As<game::BreakableProp>();
    const int serial = static_cast<int>(t.serial);
    auto sf = g->shotFrame.find(serial);
    const uint32_t shot = (sf != g->shotFrame.end() && sf->second == g->frame) ? 0xA : 0;
    const game::Motion& m = g->Mot(p.anim.motion);
    if (p.Update(g->w, g->breakable, shot, m.ticks) != game::PropResult::Alive) g->ts.KillCurrent();
    const bool loaded = !g->noMotion.count({ serial, g->frame });
    g->draws[serial] = p.Draw(g->w, g->breakable, g->Skel(p.anim.skeleton), m, loaded, g->view);
}

void DecorFn(game::Task& t) {
    auto& p = t.As<game::DecorKind45>();
    if (p.Update(g->w) != game::PropResult::Alive) g->ts.KillCurrent();
    g->draws[static_cast<int>(t.serial)] = p.Draw(g->exe);
}

void AnimatedFn(game::Task& t) {
    auto& p = t.As<game::AnimatedProp>();
    const int serial = static_cast<int>(t.serial);
    const game::Motion& m = g->Mot(p.anim.motion);
    if (p.Update(g->w, m.ticks) != game::PropResult::Alive) g->ts.KillCurrent();
    g->w.motionState = g->noMotion.count({ serial, g->frame }) ? 0 : 2;
    Mat4 stored;
    g->draws[serial] = p.Draw(g->w, g->Skel(p.anim.skeleton), m, &stored);
    g->w.motionState = 2;
}

// Actor_Type44 0x00472B10 / Actor_Komono 0x00461CD0 (+ the state / kind constructors), then Task_Kill.
void SpawnFn(game::Task& t) {
    const auto& a = t.As<SpawnActor>();
    const game::SpawnRecord& r = a.rec;
    if (r.type == 0x44) {
        if (r.state == 1) {
            game::Task* n = g->ts.Create(HangingFn, std::make_unique<game::HangingProp>());
            n->As<game::HangingProp>().Init(r);
            Register(*n, kFnHanging);
        } else if (r.state == 3) {
            game::Task* n = g->ts.Create(AnimatedFn, std::make_unique<game::AnimatedProp>());
            n->As<game::AnimatedProp>().Init(r, g->w);
            Register(*n, kFnAnimated);
        } else if (r.state == 15) {
            game::BreakableProp tmp;
            if (tmp.InitState15(r, g->w, g->breakable)) {
                game::Task* n = g->ts.Create(BreakableFn, std::make_unique<game::BreakableProp>(tmp));
                Register(*n, kFnBreakable);
            }
        }
    } else if (r.type == 0x41) {
        const int kind = static_cast<int8_t>(r.params[1]);
        if (kind == 4) {
            game::Task* n = g->ts.Create(BreakableFn, std::make_unique<game::BreakableProp>());
            n->As<game::BreakableProp>().InitKind4(r, g->w, g->breakable);
            Register(*n, kFnBreakable);
        } else if (kind == 45) {
            game::Task* n = g->ts.Create(DecorFn, std::make_unique<game::DecorKind45>());
            n->As<game::DecorKind45>().Init(r, g->w);
            Register(*n, kFnDecor);
        }
    }
    g->ts.KillCurrent();
}

bool MatNear(const Mat4& a, const float* b, double* err) {
    double e = 0;
    for (int i = 0; i < 16; ++i) e = std::max(e, std::fabs(static_cast<double>(a.m[i]) - b[i]));
    *err = e;
    return e < 2e-3;
}

int CrtRand() {  // MSVC rand(); the seed does not matter for the checks (only item counters use it here)
    static uint32_t s = 1;
    s = s * 214013u + 2531011u;
    return static_cast<int>((s >> 16) & 0x7FFF);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: props_test <game dir> <stage table> <actor capture>\n");
        return 2;
    }
    Sim sim;
    g = &sim;
    sim.gameDir = argv[1];
    if (!sim.exe.Load(sim.gameDir + "/Hod2.exe")) return 2;
    sim.mem.Load(game::EventMemory::kCommonBase, ReadAll(fs::path(argv[1]) / "evt" / "comevtbl.bin"));
    sim.mem.Load(game::EventMemory::kStageBase, ReadAll(fs::path(argv[1]) / "evt" / argv[2]));
    sim.hanging.Load(sim.exe);
    sim.breakable.Load(sim.exe);
    sim.w.rand = CrtRand;

    std::map<int, FrameData> frames;
    if (!ParseCapture(argv[3], frames)) {
        printf("cannot read %s\n", argv[3]);
        return 2;
    }

    // Capture-derived inputs.
    std::set<int> seenSerial;
    for (auto& [n, fd] : frames)
        for (auto& [s, words] : fd.T) {
            uint32_t fn = 0;
            for (auto& k : fd.K)
                if (k.first == s) fn = k.second;
            if (fn == kFnBreakable && WordI(words, 0x32C) != 0 && !sim.shotFrame.count(s)) sim.shotFrame[s] = n;
            if ((fn == kFnBreakable || fn == kFnAnimated) && !fd.M.count(s)) sim.noMotion.insert({ s, n });
        }
    for (auto& [s, f] : sim.shotFrame) printf("input: serial %d shot at frame %d (from the capture)\n", s, f);
    printf("input: %zu (serial, frame) pairs without motion residency (no draw in the capture)\n", sim.noMotion.size());
    // Shadow ground height 0x009C8E58 per frame, from the first breakable prop's shadow draw (its last draw):
    // M[13] = x V1 + (g + 0.2) V5 + z V9 + V13 with x, z = the prop's +0x19C / +0x1A4.
    std::map<int, float> ground;
    for (auto& [n, fd] : frames) {
        const float* V = nullptr;
        for (auto& k : fd.K)
            if (k.second == kFnStage && fd.M.count(k.first)) {
                V = fd.M[k.first][0].m;
                break;
            }
        if (!V) continue;
        for (auto& k : fd.K) {
            auto t = fd.T.find(k.first);
            auto m = fd.M.find(k.first);
            if (k.second != kFnBreakable || t == fd.T.end() || m == fd.M.end()) continue;
            const MLine& sh = m->second.back();
            if (sh.model != 0x10D0 && sh.model != 0x10D1) continue;
            const float x = WordF(t->second, 0x19C), z = WordF(t->second, 0x1A4);
            ground[n] = (sh.m[13] - x * V[1] - z * V[9] - V[13]) / V[5] - 0.2f;
            break;
        }
    }
    {
        float last = 1e30f;
        for (auto& [n, gy] : ground)
            if (std::fabs(gy - last) > 1e-3f) {
                printf("input: shadow ground height %.4f from frame %d (from the capture)\n", gy, n);
                last = gy;
            }
    }

    int checkedFrames = 0, serialOk = 0, serialBad = 0, stateOk = 0, stateBad = 0, drawOk = 0, drawBad = 0,
        drawApprox = 0, countBad = 0, orderBad = 0;
    double maxErr = 0;
    std::map<uint32_t, std::pair<int, double>> perFn;  // fn -> draws, max err
    std::map<int, int> ourToCap;

    for (auto& [n, fd] : frames) {
        sim.frame = n;
        if (ground.count(n)) sim.w.groundY = ground[n];
        sim.w.camPath = fd.camPath;
        sim.w.camTime = fd.camTime;
        auto st = fd.M.end();
        for (auto& k : fd.K)
            if (k.second == kFnStage && fd.M.count(k.first)) {
                st = fd.M.find(k.first);
                break;
            }
        if (st != fd.M.end()) memcpy(sim.view.m, st->second[0].m, sizeof(sim.view.m));

        // Event ops of this frame, in execution order (the event task runs first in the task list).
        int spawns = 0;
        std::vector<game::SpawnRecord> recs;
        for (auto& e : fd.E) {
            const uint32_t op = e[0];
            if (op == 0x48 && e.size() > 1) sim.w.flags[e[1] & 0xFF] = 1;  // Evt_SetFlag 0x0045FD70
            if (op == 0x09 || op == 0x0B || op == 0x0C) {
                for (size_t i = 1; i < e.size() && e[i] != 0xFFFFFFFF; ++i) {
                    game::SpawnRecord r;
                    if (!r.Read(sim.mem, e[i])) break;
                    recs.push_back(r);
                    ++spawns;
                }
            }
        }
        bool propSpawn = false;
        for (auto& r : recs) propSpawn = propSpawn || r.type == 0x41 || r.type == 0x44;
        if (spawns) {
            // spawn actors take serials S..S+k-1, then each creates one task: S+k... (checked below)
            std::set<int> before;
            if (frames.count(n - 1))
                for (auto& k : frames[n - 1].K) before.insert(k.first);
            int minBorn = INT32_MAX;
            for (auto& k : fd.K)
                if (!before.count(k.first)) minBorn = std::min(minBorn, k.first);
            if (minBorn != INT32_MAX && propSpawn) sim.ts.SetNextSerial(static_cast<uint32_t>(minBorn - spawns));
            for (auto& r : recs) {
                game::Task* t = sim.ts.Create(SpawnFn, std::make_unique<SpawnActor>());
                t->As<SpawnActor>().rec = r;
            }
        }

        sim.draws.clear();
        sim.ts.RunFrame();
        sim.ts.FreeDead();

        // ---- compare
        std::map<int, uint32_t> capFn;
        for (auto& k : fd.K) capFn[k.first] = k.second;
        bool any = false;
        std::vector<int> ourOrder, capOrder;
        for (game::Task* t : sim.ts.Live()) {
            const int s = static_cast<int>(t->serial);
            ourOrder.push_back(s);
            const uint32_t fn = sim.serialFn[s];
            auto cf = capFn.find(s);
            if (cf == capFn.end() || cf->second != fn) {
                if (!ourToCap.count(s)) {
                    printf("frame %d: our task %d (fn %08X) not in the capture with that fn\n", n, s, fn);
                    ourToCap[s] = -1;
                }
                ++serialBad;
                continue;
            }
            ++serialOk;
            any = true;
            // task state
            auto tw = fd.T.find(s);
            if (tw != fd.T.end()) {
                const auto& w = tw->second;
                bool ok = true;
                if (fn == kFnHanging) {
                    const auto& p = t->As<game::HangingProp>();
                    ok = WordI(w, 0x64) == p.rot[0] && WordI(w, 0x68) == p.rot[1] && WordI(w, 0x6C) == p.rot[2] &&
                         WordI(w, 0x2A8) == p.frame && w[0x34 / 4] == p.flags;
                } else if (fn == kFnBreakable) {
                    const auto& p = t->As<game::BreakableProp>();
                    ok = WordI(w, 0x32C) == p.anim.frame && w[0x34 / 4] == p.flags && WordI(w, 0x328) == p.anim.motion &&
                         WordI(w, 0x324) == p.anim.skeleton && static_cast<int16_t>(w[0x290 / 4]) == p.subKind;
                    if (p.anim.frame == 0)
                        for (int j = 0; j < 3; ++j) ok = ok && std::fabs(WordF(w, 0x70 + 4 * j) - p.hitPoint[j]) < 2e-3;
                } else if (fn == kFnDecor) {
                    const auto& p = t->As<game::DecorKind45>();
                    ok = WordI(w, 0x1D0) == p.phase && WordI(w, 0x2A0) == p.extra;
                } else if (fn == kFnAnimated) {
                    const auto& p = t->As<game::AnimatedProp>();
                    ok = WordI(w, 0x32C) == p.anim.frame && static_cast<int16_t>(w[0x28C / 4]) == p.model;
                }
                if (ok) ++stateOk;
                else {
                    if (stateBad < 10) printf("frame %d serial %d: task state differs\n", n, s);
                    ++stateBad;
                }
            }
            // draws
            const std::vector<game::PropDraw>& ours = sim.draws[s];
            auto cm = fd.M.find(s);
            const size_t capCount = cm == fd.M.end() ? 0 : cm->second.size();
            if (capCount != ours.size()) {
                if (countBad < 10) printf("frame %d serial %d: %zu draws, original %zu\n", n, s, ours.size(), capCount);
                ++countBad;
                continue;
            }
            for (size_t i = 0; i < ours.size(); ++i) {
                const MLine& ml = cm->second[i];
                const game::PropDraw& d = ours[i];
                double err;
                const Mat4 mv = d.world * sim.view;
                const bool ok = d.model == ml.model && MatNear(mv, ml.m, &err);
                if (d.approx) {
                    ++drawApprox;
                    continue;
                }
                auto& pf = perFn[sim.serialFn[s]];
                ++pf.first;
                if (d.model == ml.model) pf.second = std::max(pf.second, err);
                if (ok) {
                    ++drawOk;
                    maxErr = std::max(maxErr, err);
                } else {
                    if (drawBad < 10)
                        printf("frame %d serial %d draw %zu: model %d vs %d, err %g\n", n, s, i, d.model, ml.model, err);
                    ++drawBad;
                }
            }
        }
        for (auto& k : fd.K)
            if (std::find(ourOrder.begin(), ourOrder.end(), k.first) != ourOrder.end()) capOrder.push_back(k.first);
        std::vector<int> ourKnown;
        for (int s : ourOrder)
            if (capFn.count(s)) ourKnown.push_back(s);
        if (ourKnown != capOrder) ++orderBad;
        if (any) ++checkedFrames;
    }

    printf("frames with prop tasks: %d\n", checkedFrames);
    printf("task identity (serial + fn per frame): %d ok, %d bad; list order mismatches: %d\n", serialOk, serialBad,
           orderBad);
    printf("task state (T dumps): %d ok, %d bad\n", stateOk, stateBad);
    printf("draws: %d ok (max |W*V - M| %.3g), %d bad, %d count mismatches, %d skipped (big-angle interpolation)\n",
           drawOk, maxErr, drawBad, countBad, drawApprox);
    for (auto& [fn, v] : perFn) printf("  fn %08X: %d draws, max err %.3g\n", fn, v.first, v.second);
    const bool pass = serialBad == 0 && orderBad == 0 && stateBad == 0 && drawBad == 0 && countBad == 0 && drawOk > 0;
    printf("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
