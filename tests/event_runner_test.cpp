// event_runner_test <game dir> <capture> [stage table = st1evtbl.bin] [mode = 0] [scene = 4]
//                   [--slots-from-w | --oracle | --no-inputs] [--verbose]
//
// Replays the stage-1 attract demo (game mode 5, demo 0: comevtbl[0][4][0], st1evtbl.bin) through
// game::EventRunner (event VM + camera command system + shooting camera + Cam_BuildViews) and compares, frame by
// frame, with a capture of the ORIGINAL game (HOTD2_SCENECAP=1 with _EVENTS, _TASKS, _WATCH; e.g.
// build\scene_capture_stage3.txt):
//   E lines  every opcode handler call (opcode, address)              -> runner trace
//   C lines  every Cam_Evaluate call (camera id, time, pos, target)   -> CameraDirector::evals
//   F lines  active context 0x009C6F00, camera id 0x009A2D78, tick (+0x144), pos, pitch/yaw/roll of the
//            active context
//   W lines  0x009A6000 (context 0: view +0x00, pos +0xC0, angles +0xCC, target +0xD8) and the camera / VM
//            globals (state 0x009C6F20, flags 0x009C6F2C..2F, command state, pending, ...)
//
// Inputs: the gameplay values the camera and the VM read from other subsystems come from the capture's
// end-of-frame state of the PREVIOUS frame (actors run after the camera). Nothing the camera or the VM produces is
// fed back.
// default         live enemies 0x009C904A / 0x009C7006 from W; the lock-on table is game::LockOnTable, fed with the
//                 enemies of the task dumps (T lines, fn 0x004533F0: position +0x40, body point +0x100, target
//                 player +0x121, flags +0x34) in task order: registration 0x00408EC0 at the player position of
//                 the rail step, rebuild by the table task 0x00408DD0 after the camera, slot released when the
//                 enemy's flags get 0x10000 (death). The rebuilt slots 0..3 are also checked against W 0x009A5EC0.
// --slots-from-w  the lock-on slots 0x009A5EC0 straight from W (objects resolved through the T/K task lists)
// --oracle        the Stage-2 oracle (enemy visibility frame ranges + alignment frames, no slots)
// --no-inputs     control run: no enemies, no slots

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

#include "game/camera.h"
#include "game/event_runner.h"

namespace fs = std::filesystem;

static std::vector<uint8_t> ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

static float F32(uint32_t w) {
    float f;
    memcpy(&f, &w, 4);
    return f;
}

// ---- capture ----
struct TaskDump {
    uint32_t serial = 0, fn = 0, prev = 0, next = 0;
    std::vector<uint32_t> words;  // enemies only (fn 0x004533F0)
};
struct Op { uint32_t op, addr; };
struct CamCall { int id; float t; float pos[3], target[3]; };
struct FrameCap {
    std::vector<Op> ops;
    std::vector<CamCall> cams;
    bool f = false;
    int ctx = -1, camId = -1, tick = -1;
    float pos[3] = {};
    int ang[3] = {};
    std::map<uint32_t, std::vector<uint8_t>> w;    // W: address -> bytes
    std::vector<std::pair<uint32_t, uint32_t>> k;  // K: (serial, parent) in list order
    std::vector<TaskDump> t;
    const uint8_t* W(uint32_t a, size_t n) const {
        auto it = w.find(a);
        return it != w.end() && it->second.size() >= n ? it->second.data() : nullptr;
    }
    int S16(uint32_t a) const { const uint8_t* p = W(a, 2); return p ? static_cast<int16_t>(p[0] | p[1] << 8) : 0; }
    int32_t I32(uint32_t a) const { const uint8_t* p = W(a, 4); int32_t v = 0; if (p) memcpy(&v, p, 4); return v; }
};

static std::map<int, FrameCap> LoadCapture(const std::string& path, int* firstE, int* lastF) {
    std::map<int, FrameCap> cap;
    FrameCap cur;
    std::ifstream in(path);
    if (!in) return cap;
    for (std::string line; std::getline(in, line);) {
        if (line.size() < 2 || line[1] != ' ') continue;
        const char type = line[0];
        if (type == 'M' || type == 'Y' || type == 'Z' || type == 'N') continue;
        std::istringstream ss(line.substr(2));
        if (type == 'E') {
            int fr; unsigned op; std::string addr;
            ss >> fr >> op >> addr;
            cap[fr].ops.push_back({ op, static_cast<uint32_t>(std::stoul(addr, nullptr, 16)) });
            if (*firstE < 0) *firstE = fr;
        } else if (type == 'C') {
            CamCall c{};
            ss >> c.id >> c.t >> c.pos[0] >> c.pos[1] >> c.pos[2] >> c.target[0] >> c.target[1] >> c.target[2];
            cur.cams.push_back(c);
        } else if (type == 'W') {
            for (std::string tok; ss >> tok;) {
                const size_t colon = tok.find(':');
                if (colon == std::string::npos) continue;
                const uint32_t a = static_cast<uint32_t>(std::stoul(tok.substr(0, colon), nullptr, 16));
                std::vector<uint8_t> bytes;
                for (size_t i = colon + 1; i + 1 < tok.size(); i += 2)
                    bytes.push_back(static_cast<uint8_t>(std::stoul(tok.substr(i, 2), nullptr, 16)));
                cur.w[a] = std::move(bytes);
            }
        } else if (type == 'K') {
            for (std::string tok; ss >> tok;) {
                unsigned s = 0, fn = 0, parent = 0;
                if (sscanf(tok.c_str(), "%u:%x:%u", &s, &fn, &parent) == 3) cur.k.emplace_back(s, parent);
            }
        } else if (type == 'T') {
            TaskDump d;
            std::string fn;
            int header;
            ss >> d.serial >> fn >> header;
            d.fn = static_cast<uint32_t>(std::stoul(fn, nullptr, 16));
            std::vector<uint32_t> words;
            for (std::string tok; ss >> tok;) words.push_back(static_cast<uint32_t>(std::stoul(tok, nullptr, 16)));
            if (words.size() > 7) { d.prev = words[6]; d.next = words[7]; }
            if (d.fn == 0x004533F0) d.words = std::move(words);
            cur.t.push_back(std::move(d));
        } else if (type == 'F') {
            int fr;
            ss >> fr >> cur.ctx >> cur.camId >> cur.tick >> cur.pos[0] >> cur.pos[1] >> cur.pos[2] >> cur.ang[0] >> cur.ang[1] >> cur.ang[2];
            cur.f = true;
            FrameCap& fc = cap[fr];
            cur.ops = std::move(fc.ops);
            fc = std::move(cur);
            cur = FrameCap{};
            *lastF = fr;
        }
    }
    return cap;
}

// Lock-on slots at the end of a captured frame: 0x009A5EC0 + 8k = {u8 active, ..., object*}; the object is found
// through the task list (K order + the prev/next links of the dumped tasks), its +0x100 / +0x121 from its dump.
static void SlotsFromCapture(const FrameCap& fc, game::LockOnSlot out[4], uint32_t serials[4], int* unresolved) {
    for (int k = 0; k < 4; ++k) { out[k] = game::LockOnSlot{}; serials[k] = 0; }
    const uint8_t* s = fc.W(0x009A5EC0, 32);
    if (!s) return;
    std::map<uint32_t, uint32_t> addrToSerial;
    std::map<uint32_t, size_t> index;  // serial -> position in K
    for (size_t i = 0; i < fc.k.size(); ++i) index[fc.k[i].first] = i;
    auto sibling = [&](size_t i, int dir) -> uint32_t {
        for (long j = static_cast<long>(i) + dir; j >= 0 && j < static_cast<long>(fc.k.size()); j += dir)
            if (fc.k[j].second == fc.k[i].second) return fc.k[j].first;
        return 0;
    };
    for (const TaskDump& d : fc.t) {
        auto it = index.find(d.serial);
        if (it == index.end()) continue;
        if (d.prev) if (uint32_t p = sibling(it->second, -1)) addrToSerial[d.prev] = p;
        if (d.next) if (uint32_t n = sibling(it->second, +1)) addrToSerial[d.next] = n;
    }
    for (int k = 0; k < 4; ++k) {
        if (!s[8 * k]) continue;
        uint32_t ptr;
        memcpy(&ptr, s + 8 * k + 4, 4);
        out[k].active = true;
        auto a = addrToSerial.find(ptr);
        const TaskDump* dump = nullptr;
        if (a != addrToSerial.end())
            for (const TaskDump& d : fc.t)
                if (d.serial == a->second && d.words.size() > 0x49) dump = &d;
        if (!dump) { ++*unresolved; continue; }
        serials[k] = dump->serial;
        for (int c = 0; c < 3; ++c) out[k].point[c] = F32(dump->words[0x40 + c]);
        out[k].targetPlayer = static_cast<int8_t>((dump->words[0x48] >> 8) & 0xFF);
    }
}

// ---- Stage-2 ORACLE (kept as a fallback, --oracle) ----
struct Oracle {
    std::vector<std::pair<int, int>> tracking = { { 2881, 2897 }, { 2970, 2983 }, { 3303, 3599 } };
    std::set<int> aligned = { 2937, 3030, 3242 };
};

enum class InputMode { Enemies, SlotsFromW, Oracle, None };

class DemoHost : public game::EventHost {
public:
    InputMode mode;
    Oracle oracle;
    const std::map<int, FrameCap>& cap;
    const int* frame;
    int errors = 0, unresolved = 0;
    // Enemies mode: the lock-on table and the enemies' lock-on view (by task serial)
    game::LockOnTable table;
    std::map<uint32_t, game::LockOnTarget> objs;  // enemies seen in an earlier frame (a new task only ran its constructor)
    int slotFrames = 0, slotFramesOk = 0, slotFramesForeign = 0;
    DemoHost(InputMode m, const std::map<int, FrameCap>& c, const int* f) : mode(m), cap(c), frame(f) {}
    const FrameCap* Prev() const {
        auto it = cap.find(*frame - 1);
        return it != cap.end() && !it->second.w.empty() ? &it->second : nullptr;
    }
    bool Real() const { return mode == InputMode::Enemies || mode == InputMode::SlotsFromW; }
    bool GameRunning() override { return true; }  // attract mode: 0x00413280() is true
    int LiveEnemies() override {
        if (mode == InputMode::Oracle) {
            for (auto [a, b] : oracle.tracking)
                if (*frame >= a && *frame <= b) return 1;
            return 0;
        }
        if (Real()) if (const FrameCap* p = Prev()) return p->S16(0x009C904A);
        return 0;
    }
    int Count7006() override {
        if (Real()) if (const FrameCap* p = Prev()) return p->S16(0x009C7006);
        return 0;
    }
    int CountA0E8() override { return 0; }  // 0x009CA0E8 (op 0x46, not used by this script)
    void LockOnSlots(game::LockOnSlot out[4]) override {
        for (int k = 0; k < 4; ++k) out[k] = game::LockOnSlot{};
        if (mode == InputMode::Enemies) {
            table.Slots(out);
        } else if (mode == InputMode::SlotsFromW) {
            uint32_t serials[4];
            if (const FrameCap* p = Prev()) SlotsFromCapture(*p, out, serials, &unresolved);
        }
    }
    int CameraAlignedOverride() override {
        if (mode != InputMode::Oracle) return -1;
        return oracle.aligned.count(*frame) != 0 ? 1 : 0;
    }
    bool RailAdvance() override { return true; }
    void Error(const std::string& what) override {
        if (++errors <= 5) printf("runner error at frame %d: %s\n", *frame, what.c_str());
    }
    // After the camera task and the rail step of frame `fc`: the lock-on table task 0x00408DD0, then the actors
    // (enemies in task order: death releases their slot, live ones register at the players' position).
    void AfterCameraAndRail(const FrameCap& fc, const float playerPos[3], int frameNo, int* shown, int maxShown) {
        if (mode != InputMode::Enemies) return;
        table.Build();
        for (const TaskDump& d : fc.t) {
            if (d.fn != 0x004533F0 || d.words.size() <= 0x49) continue;
            const bool spawnedNow = objs.count(d.serial) == 0;  // Enemy_Create ran this frame, Enemy_Update did not
            game::LockOnTarget& o = objs[d.serial];
            for (int c = 0; c < 3; ++c) {
                o.pos[c] = F32(d.words[0x10 + c]);    // +0x40
                o.point[c] = F32(d.words[0x40 + c]);  // +0x100
            }
            o.targetPlayer = static_cast<int8_t>((d.words[0x48] >> 8) & 0xFF);  // +0x121
            o.flags = d.words[0x0D];                                            // +0x34
            if ((o.flags & 0x10000) && o.slot >= 0 && table.obj[o.slot] == &o) table.Release(o.slot);
            if (!spawnedNow) table.Register(&o, playerPos);  // Actor_UpdateViewPos 0x00409B70
        }
        // the rebuilt slots 0..3 vs W 0x009A5EC0 (flag + object)
        game::LockOnSlot wslots[4];
        uint32_t wser[4];
        int unres = 0;
        SlotsFromCapture(fc, wslots, wser, &unres);
        if (!fc.W(0x009A5EC0, 32)) return;
        ++slotFrames;
        bool same = true, foreignOnly = true;  // foreign: an active slot holding a non-enemy object (not modelled)
        for (int k = 0; k < 4; ++k) {
            uint32_t mine = 0;
            for (auto& [s, o] : objs)
                if (&o == table.obj[k]) mine = s;
            const bool slotSame = table.on[k] == wslots[k].active && (!table.on[k] || mine == wser[k]);
            if (!slotSame) {
                same = false;
                if (!(wslots[k].active && wser[k] == 0 && !table.on[k])) foreignOnly = false;
            }
        }
        if (same) ++slotFramesOk;
        else if (foreignOnly) {
            ++slotFramesForeign;
            if ((*shown)++ < maxShown) printf("frame %d lock-on slots: a non-enemy object is in a slot (not modelled)\n", frameNo);
        } else if ((*shown)++ < maxShown) {
            printf("frame %d lock-on slots differ: capture", frameNo);
            for (int k = 0; k < 4; ++k) printf(" %d:%u", wslots[k].active ? 1 : 0, wser[k]);
            printf(" | table");
            for (int k = 0; k < 4; ++k) {
                uint32_t mine = 0;
                for (auto& [s, o] : objs)
                    if (&o == table.obj[k]) mine = s;
                printf(" %d:%u", table.on[k] ? 1 : 0, mine);
            }
            printf("\n");
        }
    }
};
int main(int argc, char** argv) {
    std::vector<std::string> pos;
    InputMode inputMode = InputMode::Enemies;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--oracle") inputMode = InputMode::Oracle;
        else if (a == "--slots-from-w") inputMode = InputMode::SlotsFromW;
        else if (a == "--no-inputs" || a == "--no-oracle") inputMode = InputMode::None;
        else if (a == "--verbose") verbose = true;
        else pos.push_back(a);
    }
    if (pos.size() < 2) {
        fprintf(stderr, "usage: event_runner_test <game dir> <capture> [stage table] [mode] [scene] [--slots-from-w|--oracle|--no-inputs] [--verbose]\n");
        return 2;
    }
    const fs::path game = pos[0];
    const std::string capPath = pos[1];
    const std::string stage = pos.size() > 2 ? pos[2] : "st1evtbl.bin";
    const int mode = pos.size() > 3 ? atoi(pos[3].c_str()) : 0;
    const int scene = pos.size() > 4 ? atoi(pos[4].c_str()) : 4;

    game::EventMemory mem;
    mem.Load(game::EventMemory::kCommonBase, ReadAll(game / "evt" / "comevtbl.bin"));
    mem.Load(game::EventMemory::kStageBase, ReadAll(game / "evt" / stage));
    game::ExeImage exe;
    const bool haveExe = exe.Load((game / "Hod2.exe").string());
    game::CameraBank bank;
    const bool haveBank = haveExe && bank.Load(exe, [&](const std::string& n) { return ReadAll(game / "cam" / n); });

    int firstE = -1, lastF = -1;
    const std::map<int, FrameCap> cap = LoadCapture(capPath, &firstE, &lastF);
    if (firstE < 0) { fprintf(stderr, "no E lines in %s\n", capPath.c_str()); return 2; }
    bool haveW = false;
    for (const auto& [f, fc] : cap) haveW = haveW || !fc.w.empty();
    if (!haveW && (inputMode == InputMode::Enemies || inputMode == InputMode::SlotsFromW)) {
        printf("no W lines (HOTD2_SCENECAP_WATCH) in the capture: using the Stage-2 oracle\n");
        inputMode = InputMode::Oracle;
    }
    printf("capture: frames %d..%d (event trace from %d); exe tables %s; camera paths %s; inputs: %s\n", firstE, lastF,
           firstE, haveExe ? "loaded" : "missing", haveBank ? "loaded" : "missing",
           inputMode == InputMode::Enemies      ? "enemies from T dumps + lock-on table, enemy counts from W"
           : inputMode == InputMode::SlotsFromW ? "lock-on slots and enemy counts from W"
           : inputMode == InputMode::Oracle     ? "Stage-2 oracle"
                                                : "none (control)");

    int curFrame = firstE;
    DemoHost host(inputMode, cap, &curFrame);
    game::Environment env;
    game::EventRunner runner(mem, host, &env, haveExe ? &exe : nullptr);
    if (haveBank) runner.camera.SetCameraBank(&bank);
    game::EventRunner::Config cfg;  // game mode 5 (attract demo), demo 0
    runner.Start(mode, scene, cfg);
    runner.frame = firstE;
    printf("script comevtbl[%d][%d][%d] = %08X\n", mode, scene, runner.variant, runner.script);

    int opsMatch = 0, opsCap = 0, opsRun = 0, opFramesOk = 0, opFrames = 0;
    int camMatch = 0, camCap = 0, camRun = 0, camFramesOk = 0;
    float camValErr = 0;
    int ctxOk = 0, idOk = 0, tickOk = 0, posOk = 0, angOk = 0, fFrames = 0;
    float fPosErr = 0;
    // view / context 0 vs W 0x009A6000
    int wFrames = 0, viewExact = 0, viewClose = 0, camExact = 0, ctxPosExact = 0, ctxAngOk = 0, ctxTargetClose = 0;
    float viewErr = 0, targetErr = 0;
    int viewErrFrame = -1;
    // globals vs W
    struct G { const char* name; uint32_t addr; int ok = 0, n = 0; };
    G gState{ "state 0x009C6F20", 0x009C6F20 }, gFlags{ "flags 0x009C6F2C..2F", 0x009C6F2C }, gCmd{ "cmd state 0x009A1A10", 0x009A1A10 },
      gPend{ "pending 0x009A2C8C", 0x009A2C8C }, gWait{ "op44 frames 0x007DCCA8", 0x007DCCA8 }, gTick{ "tick 0x009A6110", 0x009A6110 },
      gRem{ "ticks left 0x009C6F28", 0x009C6F28 }, gAct{ "active ctx 0x009C6F00", 0x009C6F00 }, gMode{ "mode 0x009C6F08", 0x009C6F08 },
      gSub{ "sub 0x009C6F10", 0x009C6F10 }, gCam{ "camera 0x009A2D78", 0x009A2D78 }, gYield{ "yield 0x009C8EA0", 0x009C8EA0 };
    int shown = 0;
    const int kShow = verbose ? 400 : 24;
    for (int fr = firstE; fr <= lastF; ++fr) {
        curFrame = fr;
        const size_t before = runner.trace.size();
        runner.Frame();
        auto capIt = cap.find(fr);
        static const FrameCap kEmpty;
        const FrameCap& fc = capIt != cap.end() ? capIt->second : kEmpty;
        const game::CameraDirector& cam = runner.camera;
        host.AfterCameraAndRail(fc, cam.playerPos, fr, &shown, kShow);
        // ops
        std::vector<Op> mine;
        for (size_t i = before; i < runner.trace.size(); ++i) mine.push_back({ runner.trace[i].opcode, runner.trace[i].addr });
        ++opFrames;
        opsCap += static_cast<int>(fc.ops.size());
        opsRun += static_cast<int>(mine.size());
        bool same = mine.size() == fc.ops.size();
        for (size_t i = 0; i < std::min(mine.size(), fc.ops.size()); ++i) {
            if (mine[i].op == fc.ops[i].op && mine[i].addr == fc.ops[i].addr) ++opsMatch;
            else same = false;
        }
        if (same) ++opFramesOk;
        else if (shown++ < kShow) {
            printf("frame %d ops differ: capture", fr);
            for (auto& o : fc.ops) printf(" %02X@%08X", o.op, o.addr);
            printf(" | runner");
            for (auto& o : mine) printf(" %02X@%08X", o.op, o.addr);
            printf("\n");
        }
        // camera evaluations (id, time; values as a check of the camera bank)
        const auto& ev = cam.evals;
        camCap += static_cast<int>(fc.cams.size());
        camRun += static_cast<int>(ev.size());
        bool camSame = ev.size() == fc.cams.size();
        for (size_t i = 0; i < std::min(ev.size(), fc.cams.size()); ++i) {
            if (ev[i].first == fc.cams[i].id && ev[i].second == fc.cams[i].t) {
                ++camMatch;
                float p[3], t[3];
                int r;
                if (haveBank && bank.Evaluate(ev[i].first, ev[i].second, p, t, &r, false))
                    for (int k = 0; k < 3; ++k) {
                        const float e = std::max(std::fabs(p[k] - fc.cams[i].pos[k]), std::fabs(t[k] - fc.cams[i].target[k]));
                        camValErr = std::max(camValErr, e / std::max(1.0f, std::fabs(fc.cams[i].pos[k])));
                    }
            } else {
                camSame = false;
            }
        }
        if (camSame) ++camFramesOk;
        else if (shown++ < kShow) {
            printf("frame %d camera evaluations differ: capture", fr);
            for (auto& c : fc.cams) printf(" (%d,%g)", c.id, c.t);
            printf(" | runner");
            for (auto& c : ev) printf(" (%d,%g)", c.first, c.second);
            printf("\n");
        }
        // F line: active context, camera id, tick (+0x144: ctx0 = play tick; ctx2 is never stepped), pos, angles
        if (fc.f) {
            ++fFrames;
            const int ctx = cam.activeCtx;
            const game::CameraContext& c = cam.ctx[ctx & 3];
            const int tick = ctx == 0 ? cam.playTick : 0;
            if (ctx == fc.ctx) ++ctxOk;
            if (cam.camId == fc.camId) ++idOk;
            if (tick == fc.tick) ++tickOk;
            float pe = 0;
            for (int k = 0; k < 3; ++k) pe = std::max(pe, std::fabs(c.pos[k] - fc.pos[k]) / std::max(1.0f, std::fabs(fc.pos[k])));
            fPosErr = std::max(fPosErr, pe);
            const bool pOk = pe <= 2e-6f;  // F prints 7 significant digits
            const bool aOk = c.pitch == fc.ang[0] && c.yaw == fc.ang[1] && c.roll == fc.ang[2];
            if (pOk) ++posOk;
            if (aOk) ++angOk;
            if ((ctx != fc.ctx || cam.camId != fc.camId || tick != fc.tick || !pOk || !aOk) && shown++ < kShow)
                printf("frame %d F differs: capture ctx %d cam %d tick %d pos %g %g %g ang %d %d %d | runner ctx %d cam %d tick %d pos %g %g %g ang %d %d %d\n",
                       fr, fc.ctx, fc.camId, fc.tick, fc.pos[0], fc.pos[1], fc.pos[2], fc.ang[0], fc.ang[1], fc.ang[2], ctx,
                       cam.camId, tick, c.pos[0], c.pos[1], c.pos[2], c.pitch, c.yaw, c.roll);
        }
        // context 0 (view after 0x00403C00 + Cam_BuildViews) vs W 0x009A6000
        if (const uint8_t* w = fc.W(0x009A6000, 0x1A4)) {
            ++wFrames;
            const game::CameraContext& c = cam.ctx[0];
            float view[16], wpos[3], wtarget[3];
            int wang[3];
            memcpy(view, w, 64);
            memcpy(wpos, w + 0xC0, 12);
            memcpy(wang, w + 0xCC, 12);
            memcpy(wtarget, w + 0xD8, 12);
            float e = 0;
            bool exact = true;
            for (int i = 0; i < 16; ++i) {
                e = std::max(e, std::fabs(view[i] - c.view[i]));
                exact = exact && view[i] == c.view[i];
            }
            if (e > viewErr) { viewErr = e; viewErrFrame = fr; }
            if (exact) ++viewExact;
            if (!memcmp(w + 0x40, c.camera, 64)) ++camExact;  // camera matrix (inverse view, before normalisation)
            if (e <= 1e-4f) ++viewClose;
            if (!memcmp(wpos, c.pos, 12)) ++ctxPosExact;
            const bool angSame = wang[0] == c.pitch && wang[1] == c.yaw && wang[2] == c.roll;
            if (angSame) ++ctxAngOk;
            float te = 0;
            for (int k = 0; k < 3; ++k) te = std::max(te, std::fabs(wtarget[k] - c.target[k]));
            targetErr = std::max(targetErr, te);
            if (te <= 1e-3f) ++ctxTargetClose;
            if ((e > 1e-4f || !angSame || te > 1e-3f) && shown++ < kShow)
                printf("frame %d ctx0 differs: view err %g; capture pos %g %g %g ang %d %d %d target %g %g %g | runner pos %g %g %g ang %d %d %d target %g %g %g\n",
                       fr, e, wpos[0], wpos[1], wpos[2], wang[0], wang[1], wang[2], wtarget[0], wtarget[1], wtarget[2], c.pos[0],
                       c.pos[1], c.pos[2], c.pitch, c.yaw, c.roll, c.target[0], c.target[1], c.target[2]);
            // globals
            const uint8_t* fl = fc.W(0x009C6F2C, 4);
            const int flags = fl ? (fl[0] ? 1 : 0) | (fl[1] ? 2 : 0) | (fl[2] << 2 & 0xFC) | (fl[3] ? 0x100 : 0) : -1;
            const int mineFlags = (cam.returning ? 1 : 0) | (cam.settled ? 2 : 0) | ((cam.style & 0x3F) << 2) | (cam.aimedOnce ? 0x100 : 0);
            auto check = [&](G& g, int capV, int mineV) {
                ++g.n;
                if (capV == mineV) ++g.ok;
                else if (shown++ < kShow) printf("frame %d %s: capture %d runner %d\n", fr, g.name, capV, mineV);
            };
            check(gState, fc.I32(0x009C6F20), cam.state);
            check(gFlags, flags, mineFlags);
            check(gCmd, fc.I32(0x009A1A10), cam.cmdState);
            check(gPend, fc.I32(0x009A2C8C), cam.pending);
            check(gWait, fc.I32(0x007DCCA8), runner.enemyWaitFrames);
            check(gTick, fc.I32(0x009A6110), cam.tick);
            check(gRem, fc.I32(0x009C6F28), cam.ticksRemaining);
            check(gAct, fc.I32(0x009C6F00), cam.activeCtx);
            check(gMode, fc.I32(0x009C6F08), cam.modeCur);
            check(gSub, fc.I32(0x009C6F10), cam.subCur);
            check(gCam, fc.I32(0x009A2D78), cam.camId);
            check(gYield, fc.I32(0x009C8EA0), runner.yield ? 1 : 0);
        }
        if (runner.error) { printf("runner stopped: %s\n", runner.errorText.c_str()); break; }
    }

    printf("ops: %d captured, %d executed, %d matching in place; frames with identical op sequences %d / %d\n",
           opsCap, opsRun, opsMatch, opFramesOk, opFrames);
    printf("camera evaluations: %d captured, %d produced, %d matching (id and time); frames identical %d / %d; "
           "max value error %.3g\n", camCap, camRun, camMatch, camFramesOk, opFrames, camValErr);
    printf("F lines (active context): context %d / %d, camera id %d / %d, tick %d / %d, pos %d / %d (max rel err %.3g), "
           "pitch/yaw/roll %d / %d\n", ctxOk, fFrames, idOk, fFrames, tickOk, fFrames, posOk, fFrames, fPosErr, angOk, fFrames);
    printf("context 0 vs W 0x009A6000 (%d frames): view bit-exact %d, within 1e-4 %d, max abs error %.3g (frame %d); "
           "camera matrix +0x40 bit-exact %d; pos bit-exact %d; pitch/yaw/roll %d; target within 1e-3 %d (max error %.3g)\n", wFrames, viewExact, viewClose,
           viewErr, viewErrFrame, camExact, ctxPosExact, ctxAngOk, ctxTargetClose, targetErr);
    bool globalsOk = true;
    for (const G* g : { &gState, &gFlags, &gCmd, &gPend, &gWait, &gTick, &gRem, &gAct, &gMode, &gSub, &gCam, &gYield }) {
        printf("  %-24s %d / %d\n", g->name, g->ok, g->n);
        globalsOk = globalsOk && g->ok == g->n;
    }
    if (host.unresolved) printf("lock-on slots whose object was not found in the task dumps: %d\n", host.unresolved);
    if (inputMode == InputMode::Enemies)
        printf("lock-on table (rebuilt from the enemies) vs W 0x009A5EC0 slots 0..3: %d / %d frames (+%d with a non-enemy "
               "object in a slot)\n", host.slotFramesOk, host.slotFrames, host.slotFramesForeign);
    printf("env after replay: fog %d,%d,%d near %g far %g, light angles %d/%d\n", env.fogColor[0], env.fogColor[1],
           env.fogColor[2], env.fogNear, env.fogFar, env.lightPitch, env.lightYaw);
    bool ok = opFramesOk == opFrames && camFramesOk == opFrames && ctxOk == fFrames && idOk == fFrames &&
              tickOk == fFrames && opsMatch == opsCap && opsRun == opsCap && host.errors == 0;
    if (host.Real() && wFrames > 0)  // positions are only meaningful with real inputs
        ok = ok && posOk == fFrames && angOk == fFrames && viewClose == wFrames && ctxAngOk == wFrames && globalsOk &&
             host.slotFramesOk + host.slotFramesForeign == host.slotFrames;
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
