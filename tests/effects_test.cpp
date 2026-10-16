// effects_test <game dir> <capture with task/rand lines (HOTD2_SCENECAP_TASKS=..., e.g. build\scene_capture_actors.txt)>
// Validates game/rng.* and game/effects.* against the ORIGINAL game (attract demo, stage 1):
//   1. rand chain: every frame's captured CRT state (Z) follows from the previous one and the frame's rand call
//      counts (N); the only breaks are srand(0) at the rain init frame (stage start) and at event op 0x4D.
//   2. frame replay: the stream is replayed in task order with the ported consumers - players' demo input
//      (Player_DemoInput, 1 + 2 per shot), then the rain (3 per respawn), then the remaining callers (taken from N) -
//      and must land on Z; the rain's respawn counts must equal the captured counts of 0x004136DD.
//   3. rain draws: the 50 model-83 matrices of the rain task (M lines tagged with its serial) = Rain::World() * V.
//   4. sky: model 6048/6911 matrices of the sky task, with type/mode from the traced event ops 0x1B / 0x1C.
//   5. billboard effects (task 0x00407A70): spawn angles/scale/model range from Spawn() (camera-facing) equal the
//      task's first dump; every frame's model and matrix from Step(); lifetime.
// V comes from the frame's F line (camera context: pos, pitch, yaw, roll).

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
#include "game/effects.h"
#include "game/exe_image.h"
#include "game/rng.h"

namespace {

struct Draw {
    int model;
    game::Mat4 m;
    uint32_t tag;
};

struct Frame {
    int frame = 0;
    game::CameraState cam;
    bool hasZ = false;
    uint32_t z = 0;
    std::map<uint32_t, int> calls;                     // return address -> calls
    std::vector<std::pair<uint32_t, uint32_t>> tasks;  // serial, fn (K order)
    std::map<uint32_t, std::vector<uint32_t>> dumps;   // serial -> words from +0
    std::vector<std::pair<int, uint32_t>> ops;         // event opcode, first argument word
    std::vector<Draw> draws;
};

float F32(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

double MaxDiff(const game::Mat4& a, const game::Mat4& b) {
    double d = 0;
    for (int i = 0; i < 16; ++i) d = std::fmax(d, std::fabs(static_cast<double>(a.m[i]) - b.m[i]));
    return d;
}

uint32_t TaskWithFn(const Frame& f, uint32_t fn) {
    for (const auto& [s, t] : f.tasks)
        if (t == fn) return s;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: effects_test <game dir> <capture>\n");
        return 2;
    }
    game::ExeImage exe;
    if (!exe.Load(std::string(argv[1]) + "/Hod2.exe")) return 2;
    const std::vector<game::SkyEntry> skyTable = game::LoadSkyTable(exe);

    // ---- parse
    std::vector<Frame> frames;
    {
        std::ifstream cap(argv[2]);
        if (!cap) {
            fprintf(stderr, "cannot open %s\n", argv[2]);
            return 2;
        }
        Frame cur;
        for (std::string line; std::getline(cap, line);) {
            if (line.size() < 1) continue;
            const char k = line[0];
            if (line.size() > 1 && line[1] != ' ') continue;
            std::istringstream ss(line.size() > 2 ? line.substr(2) : std::string());
            if (k == 'M') {
                Draw d{};
                ss >> d.model;
                for (float& v : d.m.m) ss >> v;
                std::string tag;
                if (ss >> tag && tag[0] == '^') d.tag = static_cast<uint32_t>(std::stoul(tag.substr(1)));
                else d.tag = 0xFFFFFFFF;
                cur.draws.push_back(d);
            } else if (k == 'Z') {
                std::string h;
                ss >> h;
                cur.z = static_cast<uint32_t>(std::stoul(h, nullptr, 16));
                cur.hasZ = true;
            } else if (k == 'N') {
                for (std::string t; ss >> t;) {
                    const size_t c = t.find(':');
                    cur.calls[static_cast<uint32_t>(std::stoul(t.substr(0, c), nullptr, 16))] = std::stoi(t.substr(c + 1));
                }
            } else if (k == 'K') {
                for (std::string t; ss >> t;) {
                    const size_t a = t.find(':'), b = t.find(':', a + 1);
                    cur.tasks.push_back({ static_cast<uint32_t>(std::stoul(t.substr(0, a))),
                                          static_cast<uint32_t>(std::stoul(t.substr(a + 1, b - a - 1), nullptr, 16)) });
                }
            } else if (k == 'T') {
                uint32_t serial;
                std::string fn, hdr;
                if (!(ss >> serial >> fn >> hdr)) continue;
                std::vector<uint32_t> w;
                for (std::string t; ss >> t;) {
                    try {
                        w.push_back(static_cast<uint32_t>(std::stoul(t, nullptr, 16)));
                    } catch (...) {
                        w.clear();
                        break;
                    }
                }
                if (!w.empty()) cur.dumps[serial] = std::move(w);
            } else if (k == 'E') {
                int fr, op;
                std::string addr, a1;
                if (ss >> fr >> op >> addr >> a1) cur.ops.push_back({ op, static_cast<uint32_t>(std::stoul(a1, nullptr, 16)) });
            } else if (k == 'F') {
                int ctx, camId, tick;
                ss >> cur.frame >> ctx >> camId >> tick >> cur.cam.pos[0] >> cur.cam.pos[1] >> cur.cam.pos[2] >> cur.cam.pitch >>
                    cur.cam.yaw >> cur.cam.roll;
                frames.push_back(std::move(cur));
                cur = Frame{};
            }
        }
    }

    // ---- 1. rand chain
    int chainOk = 0, chainBad = 0, reseeds = 0, reseedsExplained = 0;
    uint32_t prevZ = 1;
    for (const Frame& f : frames) {
        if (!f.hasZ) continue;
        uint64_t total = 0;
        for (const auto& [a, n] : f.calls) total += n;
        if (game::CrtRand::Advance(prevZ, total) == f.z) {
            ++chainOk;
        } else if (game::CrtRand::Advance(0, total) == f.z) {
            ++chainOk;
            ++reseeds;
            bool op4d = false;
            for (const auto& [op, a] : f.ops) op4d |= op == 0x4D;
            if (op4d || f.calls.count(0x004138BB)) ++reseedsExplained;
        } else {
            ++chainBad;
            if (chainBad <= 5) printf("  rand chain break at frame %d\n", f.frame);
        }
        prevZ = f.z;
    }

    // ---- 2-4. replay: players, rain, rest; rain and sky draws
    game::Rain rain;
    game::Sky sky;
    bool rainInit = false;
    int rainFlag = 0, skyType = 0, skyMode = 0;
    int replayOk = 0, replayBad = 0, rainCountOk = 0, rainCountBad = 0, shotsOk = 0, shotsBad = 0;
    int rainDraws = 0, rainDrawsBad = 0, rainFrames = 0, skyDraws = 0, skyDrawsBad = 0, skyFrames = 0;
    double rainWorst = 0, skyWorst = 0;
    const std::set<uint32_t> playerRoll = { 0x0041572E, 0x00415740 }, playerAim = { 0x00415757, 0x0041576B };
    const std::set<uint32_t> rainSites = { 0x004136DD, 0x004136FB, 0x00413718 }, initSites = { 0x004138BB, 0x004138D9, 0x004138F6 };
    prevZ = 1;
    for (const Frame& f : frames) {
        if (!f.hasZ) continue;
        game::CrtRand rng(prevZ);
        bool reseed = false;
        for (const auto& [op, a] : f.ops) {
            if (op == 0x4D) reseed = true;     // Game_SeedRandom via event op 0x4D (runs in the event task, first)
            if (op == 0x1B) skyType = static_cast<int>(a);
            if (op == 0x1C) skyMode = static_cast<int>(a);
            if (op == 0x1D) rainFlag = static_cast<int>(a);
        }
        auto count = [&](const std::set<uint32_t>& s) {
            int n = 0;
            for (uint32_t a : s) {
                auto it = f.calls.find(a);
                if (it != f.calls.end()) n += it->second;
            }
            return n;
        };
        if (count(initSites) > 0) {  // stage start 0x00460030: srand(0), then the task init creates the rain
            rng.Seed(game::GameRandomSeed(true, 0));
            rain.Init(rng);
            rainInit = true;
            rainFlag = 0;
            skyMode = 0;  // 0x0045EDD0 clears 0x009CA090 and 0x009C8E50
        }
        if (reseed) rng.Seed(0);
        // players (tasks 0x00413E90, P1 then P2)
        const int rolls = count(playerRoll);
        const bool busy = f.calls.count(0x00415740) != 0;
        int shots = 0;
        for (int p = 0; p < rolls; ++p) shots += game::PlayerDemoInput(rng, busy).fire ? 1 : 0;
        (shots * 2 == count(playerAim) ? shotsOk : shotsBad)++;
        const game::Mat4 view = game::ViewMatrix(f.cam.pos, f.cam.pitch, f.cam.yaw, f.cam.roll);
        // sky task (runs before the players in task order, no rand)
        if (const uint32_t s = TaskWithFn(f, 0x004132D0)) {
            std::vector<game::Sky::Draw> out;
            sky.Update(skyTable, skyType, skyMode, f.cam, &out);
            std::vector<const Draw*> got;
            for (const Draw& d : f.draws)
                if (d.tag == s) got.push_back(&d);
            if (!got.empty() || !out.empty()) ++skyFrames;
            for (size_t i = 0; i < std::max(got.size(), out.size()); ++i) {
                ++skyDraws;
                if (i >= got.size() || i >= out.size() || got[i]->model != out[i].model) {
                    ++skyDrawsBad;
                    continue;
                }
                const double d = MaxDiff(out[i].world * view, got[i]->m);
                skyWorst = std::fmax(skyWorst, d);
                if (d > 1e-3) ++skyDrawsBad;
            }
        }
        // rain task
        const uint32_t rainSerial = TaskWithFn(f, 0x004136A0);
        if (rainInit && rainSerial && rainFlag == 1 && count(initSites) == 0) {
            const int respawns = rain.Update(rng, f.cam);
            (respawns * 3 == count(rainSites) ? rainCountOk : rainCountBad)++;
            ++rainFrames;
            int i = 0;
            for (const Draw& d : f.draws) {
                if (d.tag != rainSerial) continue;
                ++rainDraws;
                const double diff = i < game::Rain::kCount && d.model == game::Rain::kModel ? MaxDiff(rain.World()[i] * view, d.m) : 1e9;
                rainWorst = std::fmax(rainWorst, diff);
                if (diff > 1e-3) ++rainDrawsBad;
                ++i;
            }
            if (i != game::Rain::kCount) rainDrawsBad += std::abs(i - game::Rain::kCount);
        } else if (count(rainSites) != 0) {
            ++rainCountBad;
        }
        // everything after the rain in task order (enemies, props, hit sounds, effects): counts from the capture
        uint64_t rest = 0;
        for (const auto& [a, n] : f.calls)
            if (!playerRoll.count(a) && !playerAim.count(a) && !rainSites.count(a) && !initSites.count(a)) rest += n;
        rng.Skip(rest);
        if (rng.State() == f.z) {
            ++replayOk;
        } else {
            ++replayBad;
            if (replayBad <= 5) printf("  replay mismatch at frame %d\n", f.frame);
        }
        prevZ = f.z;
    }

    // ---- 5. billboard effects
    struct Eff {
        int firstFrame = -1;
        std::vector<uint32_t> dump;
        game::CameraState cam;
        game::Mat4 view;
    };
    std::map<uint32_t, Eff> effs;
    std::map<uint32_t, int> lastSeen;
    for (const Frame& f : frames) {
        const game::Mat4 view = game::ViewMatrix(f.cam.pos, f.cam.pitch, f.cam.yaw, f.cam.roll);
        for (const auto& [s, fn] : f.tasks) {
            if (fn != 0x00407A70) continue;
            lastSeen[s] = f.frame;
            Eff& e = effs[s];
            if (e.firstFrame < 0) {
                e.firstFrame = f.frame;
                e.cam = f.cam;
                e.view = view;
                auto it = f.dumps.find(s);
                if (it != f.dumps.end()) e.dump = it->second;
            }
        }
    }
    int effOk = 0, effBad = 0, effDraws = 0, effDrawsBad = 0;
    double effWorst = 0;
    for (auto& [s, e] : effs) {
        if (e.dump.size() < 26) {
            ++effBad;
            continue;
        }
        const auto& w = e.dump;
        game::EffectSpawn in;
        for (int i = 0; i < 3; ++i) in.pos[i] = F32(w[14 + i]);
        in.type = static_cast<int>(w[23] & 0xFF);
        in.faceMode = 1;
        std::vector<game::BillboardEffect> made;
        game::CrtRand dummy;
        game::BillboardEffect::Spawn(in, e.cam, e.view, dummy, &made);
        game::BillboardEffect fx = made[0];
        const bool spawnOk = fx.pitch == static_cast<int>(w[17]) && fx.yaw == static_cast<int>(w[18]) &&
                             fx.roll == static_cast<int>(w[19]) && fx.scale[0] == F32(w[20]) && fx.scale[1] == F32(w[21]) &&
                             fx.scale[2] == F32(w[22]) && fx.model + 1 == static_cast<int>(w[24]) && fx.last == static_cast<int>(w[25]);
        printf("  effect %u type 0x%02X frames %d-%d: spawn pitch %d/%d yaw %d/%d scale %.4g/%.4g models 0x%X-0x%X %s\n", s,
               in.type, e.firstFrame, lastSeen[s], fx.pitch, static_cast<int>(w[17]), fx.yaw, static_cast<int>(w[18]),
               fx.scale[0], F32(w[20]), fx.model, fx.last, spawnOk ? "ok" : "MISMATCH");
        bool ok = spawnOk;
        // draws, frame by frame
        bool alive = true;
        int drawn = 0;
        for (const Frame& f : frames) {
            if (f.frame < e.firstFrame) continue;
            const Draw* got = nullptr;
            for (const Draw& d : f.draws)
                if (d.tag == s) got = &d;
            if (!alive) {
                if (got) ok = false;
                break;
            }
            int model;
            game::Mat4 world;
            alive = fx.Step(&model, &world);
            ++effDraws;
            ++drawn;
            const game::Mat4 view = game::ViewMatrix(f.cam.pos, f.cam.pitch, f.cam.yaw, f.cam.roll);
            const double d = got && got->model == model ? MaxDiff(world * view, got->m) : 1e9;
            effWorst = std::fmax(effWorst, d);
            if (d > 1e-3) {
                ++effDrawsBad;
                ok = false;
            }
        }
        // the task kills itself in the frame after its last K sighting (after drawing), unless the capture ends first
        if (lastSeen[s] != frames.back().frame && drawn != lastSeen[s] - e.firstFrame + 2) ok = false;
        (ok ? effOk : effBad)++;
    }

    printf("rand chain: %d frames ok, %d breaks, %d reseeds to 0 (%d at rain init / op 0x4D)\n", chainOk, chainBad, reseeds,
           reseedsExplained);
    printf("replay (players + rain ported, rest from N): %d frames reproduce Z, %d do not\n", replayOk, replayBad);
    printf("demo shots: %d frames ok, %d bad\n", shotsOk, shotsBad);
    printf("rain: %d frames, respawn counts %d ok / %d bad, draws %d (%d bad, worst %.3g)\n", rainFrames, rainCountOk,
           rainCountBad, rainDraws, rainDrawsBad, rainWorst);
    printf("sky: %d frames, draws %d (%d bad, worst %.3g)\n", skyFrames, skyDraws, skyDrawsBad, skyWorst);
    printf("effects: %d ok, %d bad, draws %d (%d bad, worst %.3g)\n", effOk, effBad, effDraws, effDrawsBad, effWorst);
    const bool pass = chainBad == 0 && reseeds == reseedsExplained && replayBad == 0 && shotsBad == 0 && rainCountBad == 0 &&
                      rainDrawsBad == 0 && rainFrames > 0 && skyDrawsBad == 0 && skyFrames > 0 && effBad == 0;
    printf("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
