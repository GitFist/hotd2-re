// world_test <game dir> <capture> [--from F] [--to F] [--verbose] [--draws <fn>] [--enemy <serial>]
//            [--script <input script>] [--trace]
//
// Runs game::World (the native world runtime) and compares EVERY frame with a capture of the ORIGINAL
// (HOTD2_SCENECAP with _TASKS, _RANDLOG, _WATCH, _EVENTS):
//   rand   Z (CRT rand state at the frame end) and the ordered Y list (call site, task serial, value)
//   tasks  the K line: live tasks in execution order (serial + original function)
//   draws  M lines tagged with a task serial: model ids in order and model-view matrices (world * view, with the
//          frame's captured view = the stage task's identity draw), per task; stage section draws (culled per
//          object by the original) are checked as an ordered subsequence of the native list
//   enemy  live-enemy count 0x009C904A, attackers 0x009C7006 (W lines), and per enemy (T dumps of 0x004533F0):
//          hp +0x11C, behaviour +0x1310, sub-state +0x1312, flags +0x34, position +0x40
//   events E lines: opcode + address of every handler call
//   view   the native rendered view (camera director + shooting camera, no oracle) vs the captured one
//   (a draw whose model id differs but whose matrix matches is counted as a capture model-id alias and listed: the
//   capture's pointer -> id cache keeps the id of the first model seen at an address)
// Two modes:
//   default    the stage-1 attract demo (game mode 5) from its stage start (build\scene_capture_stage3.txt, 2786).
//              Only input from the capture: the task serial counter at the stage start.
//   --script   a game (mode 6) played with the scripted input the capture was made with (build\script_p1_*.txt):
//              the world starts at the first mode-6 frame with stage tasks (the Stage_Start frame) and gets the
//              script's gun input every frame (the same conversion as the hook, MakeWorldInput). Inputs from the
//              capture: the serial counter and the seed counter 0x009A32A0 at the stage start, the players' states
//              before it (0 = start pressed on the title). Extra checks from the W lines: game mode / sub-state,
//              letterbox / can-shoot, rank, hit shake, player struct (state, lives, score, ammo, empty, shots, hits,
//              aim), the P1 gun record. Two-player captures (notes/agents/twoplayer.md) add: P2 gun record, credits,
//              continue timers, sub-state, the players' point / angles, the CONTINUE sprites (S lines, captures
//              with HOTD2_SCENECAP_SPRITES: hud.h ContinueScreen / ContinuePlayer from WorldHud); P2's device mode is --p2mode (default 0, the
//              coordinator's setup), the credits / calibration come from the capture. With WorldConfig::gameOver the
//              game-over frames (game mode 7) to the title are compared too and reported separately (--no-gameover:
//              stop at the switch to mode 7 as before).
// The capture is streamed (the gameplay captures are 140-250 MB).

#include <algorithm>
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
#include "game/hud.h"
#include "game/shooting.h"
#include "game/sprite.h"
#include "game/world.h"
#include "game/world_config_io.h"
#include "route_planner.h"

namespace {

using plan::Planner;

struct MLine {
    int model;
    float m[16];
};
struct YLine {
    uint32_t site, serial;
    int value;
};
struct EnemyDump {
    int hp, beh, sub;
    uint32_t flags;
    float pos[3], body[3], view[3];
    int rot[3], tick;
    int motion = 0, motion2 = 0, tick2 = 0, target = -1;
    uint32_t f136C = 0, f1368 = 0;
};
struct Frame {
    int n = -1;
    bool f = false;
    int ctx = 0, camId = 0, tick = 0, pitch = 0, yaw = 0, roll = 0;
    float pos[3] = {};
    bool hasZ = false;
    uint32_t z = 0;
    std::vector<YLine> y;
    std::vector<std::pair<uint32_t, uint32_t>> u;  // sound id, task serial (HOTD2_SCENECAP_SOUNDS)
    std::vector<std::pair<uint32_t, uint32_t>> k;  // serial, fn
    bool hasK = false;
    std::map<uint32_t, std::vector<MLine>> m;      // serial -> draws (serial != 0)
    std::vector<std::pair<uint32_t, uint32_t>> e;  // op, addr
    std::map<uint32_t, std::vector<uint8_t>> w;    // watched globals
    std::map<uint32_t, EnemyDump> enemies;
    std::map<uint32_t, EnemyDump> civs;  // civilian dumps (--enemy only)
    // S lines of the CONTINUE screen 0x00460530 (callers 0x00460621 / 0x00460658) and the per-player CONTINUE (flushed
    // by 0x0041CF30 at 0x0041CF48: ids 0x22C, digits 0x4F..0x58), in order (HOTD2_SCENECAP_SPRITES)
    std::vector<game::SpriteDef> cont;
};

std::vector<uint8_t> Hex(const std::string& s) {
    std::vector<uint8_t> out;
    out.reserve(s.size() / 2);
    auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    for (size_t i = 0; i + 1 < s.size(); i += 2) out.push_back(static_cast<uint8_t>(nib(s[i]) * 16 + nib(s[i + 1])));
    return out;
}

template <class T> T Get(const std::vector<uint8_t>& b, size_t off) {
    T v{};
    if (off + sizeof(T) <= b.size()) memcpy(&v, &b[off], sizeof(T));
    return v;
}

bool g_sawU = false;  // the capture logs sound commands (U lines); compared from the first one on
bool g_sawS = false;  // the capture logs sprites (S lines): the CONTINUE sprites are compared from the first one on

bool SameSprite(const game::SpriteDef& a, const game::SpriteDef& b) {
    return a.id == b.id && a.x == b.x && a.y == b.y && a.depth == b.depth && a.sx == b.sx && a.sy == b.sy && a.rot == b.rot &&
           a.alpha == b.alpha && a.blend == b.blend && a.flags == b.flags;
}

// Streams a capture frame by frame (lines accumulate until "F n", which closes frame n).
class Capture {
public:
    explicit Capture(const char* path) : in_(path) {}
    bool ok() const { return static_cast<bool>(in_); }
    // Next frame with number >= from (earlier frames are skipped cheaply: only F lines are parsed).
    bool Next(Frame& out, int from = -1 << 30) {
        Frame cur;
        bool skipping = false;
        for (std::string line; std::getline(in_, line);) {
            if (line.empty()) continue;
            const char c = line[0];
            if (line.size() < 2 || line[1] != ' ') {
                if (c == 'K') cur.hasK = true;  // empty task list
                continue;
            }
            if (c == 'F') {
                std::istringstream ss(line.substr(2));
                int n;
                ss >> n;
                if (n < from) {
                    cur = Frame{};
                    skipping = n + 1 < from;
                    continue;
                }
                cur.n = n;
                cur.f = true;
                ss >> cur.ctx >> cur.camId >> cur.tick >> cur.pos[0] >> cur.pos[1] >> cur.pos[2] >> cur.pitch >> cur.yaw >> cur.roll;
                out = std::move(cur);
                return true;
            }
            if (skipping) continue;
            ParseLine(c, line, cur);
        }
        return false;
    }

private:
    static void ParseLine(char c, const std::string& line, Frame& cur) {
        if (c == 'M') {
            const size_t caret = line.rfind('^');
            if (caret == std::string::npos) return;
            const uint32_t serial = static_cast<uint32_t>(std::stoul(line.substr(caret + 1)));
            if (serial == 0) return;
            MLine m;
            const char* p = line.c_str() + 2;
            char* end = nullptr;
            m.model = static_cast<int>(strtol(p, &end, 10));
            p = end;
            for (float& v : m.m) {
                v = strtof(p, &end);
                p = end;
            }
            cur.m[serial].push_back(m);
        } else if (c == 'Y') {
            std::istringstream ss(line.substr(2));
            std::string site;
            YLine y;
            ss >> site >> y.serial >> y.value;
            y.site = static_cast<uint32_t>(std::stoul(site, nullptr, 16));
            cur.y.push_back(y);
        } else if (c == 'U') {
            unsigned id = 0, site = 0, serial = 0;
            if (sscanf(line.c_str() + 2, "%x %x %u", &id, &site, &serial) == 3) cur.u.push_back({ id, serial });
            g_sawU = true;
        } else if (c == 'S') {
            g_sawS = true;
            auto endsWith = [&](const char* s) {
                const size_t n = strlen(s);
                return line.size() >= n && line.compare(line.size() - n, n, s) == 0;
            };
            const bool screen = endsWith("@00460621") || endsWith("@00460658"), retained = endsWith("@0041CF48");
            if (!screen && !retained) return;
            std::istringstream ss(line.substr(2));
            uint32_t w[14] = {};
            std::string t;
            for (int i = 0; i < 14 && ss >> t; ++i) w[i] = static_cast<uint32_t>(std::stoul(t, nullptr, 16));
            const game::SpriteDef d = game::SpriteDef::FromWords(w);
            if (screen || d.id == 0x22C || (d.id >= 0x4F && d.id <= 0x58)) cur.cont.push_back(d);
        } else if (c == 'Z') {
            cur.hasZ = true;
            cur.z = static_cast<uint32_t>(std::stoul(line.substr(2), nullptr, 16));
        } else if (c == 'K') {
            cur.hasK = true;
            std::istringstream ss(line.substr(2));
            for (std::string t; ss >> t;) {
                unsigned s = 0, f = 0, p = 0;
                if (sscanf(t.c_str(), "%u:%x:%u", &s, &f, &p) >= 2) cur.k.push_back({ s, f });
            }
        } else if (c == 'E') {
            std::istringstream ss(line.substr(2));
            int fr;
            unsigned op;
            std::string addr;
            ss >> fr >> op >> addr;
            cur.e.push_back({ op, static_cast<uint32_t>(std::stoul(addr, nullptr, 16)) });
        } else if (c == 'W') {
            std::istringstream ss(line.substr(2));
            for (std::string t; ss >> t;) {
                const size_t colon = t.find(':');
                if (colon == std::string::npos) continue;
                cur.w[static_cast<uint32_t>(std::stoul(t.substr(0, colon), nullptr, 16))] = Hex(t.substr(colon + 1));
            }
        } else if (c == 'T') {
            std::istringstream ss(line.substr(2));
            uint32_t serial;
            std::string fn;
            int hdr;
            ss >> serial >> fn >> hdr;
            const bool civ = fn == "48A920" || fn == "48A3E0" || fn == "449910";  // traced only (--enemy; + actor 0x31)
            if (fn != "4533F0" && fn != "452DA0" && !civ) return;
            std::vector<uint32_t> words;
            for (std::string x; ss >> x;) words.push_back(static_cast<uint32_t>(std::stoul(x, nullptr, 16)));
            if (words.size() * 4 < 0x1320) return;
            auto h = [&](int off) { return static_cast<int16_t>((words[off / 4] >> (8 * (off & 3))) & 0xFFFF); };
            EnemyDump d;
            d.hp = h(0x11C);
            d.beh = h(0x1310);
            d.sub = h(0x1312);
            d.flags = words[0x34 / 4];
            memcpy(d.pos, &words[0x40 / 4], 12);
            memcpy(d.body, &words[0x100 / 4], 12);
            memcpy(d.view, &words[0x70 / 4], 12);
            memcpy(d.rot, &words[0x64 / 4], 12);
            d.tick = static_cast<int>(words[0x19C / 4]);
            d.tick2 = static_cast<int>(words[0x1A0 / 4]);
            d.motion = static_cast<int>(words[0x1B4 / 4]);
            d.motion2 = static_cast<int>(words[0x1B8 / 4]);
            d.f1368 = words[0x1368 / 4];
            d.f136C = words[0x136C / 4];
            d.target = static_cast<int8_t>((words[0x120 / 4] >> 8) & 0xFF);
            (civ ? cur.civs : cur.enemies)[serial] = d;
        }
    }
    std::ifstream in_;
};

// Scripted input (src/hook/input_hook.cpp ApplyScript): "<boot frame> <player> <x> <y> <buttons>"; a player's state
// holds until its next line. Boot frame 0x009A5C40 is read before Game_UpdateInput increments it, i.e. it equals the
// capture's F number.
struct Script {
    struct Line {
        int frame, player;
        float x, y;
        int buttons;
    };
    std::vector<Line> lines;
    size_t pos = 0;
    bool connected[2] = {};
    float x[2] = { 0.5f, 0.5f }, y[2] = { 0.5f, 0.5f };
    uint8_t held[2] = {};
    bool Load(const std::string& path) {
        std::ifstream in(path);
        if (!in) return false;
        for (std::string s; std::getline(in, s);) {
            if (s.empty() || s[0] == '#') continue;
            Line l{};
            if (sscanf(s.c_str(), "%d %d %f %f %d", &l.frame, &l.player, &l.x, &l.y, &l.buttons) == 5 && l.player >= 0 && l.player < 2)
                lines.push_back(l);
        }
        return true;
    }
    // Consume the lines up to frame fr without producing edges (a world started in the middle of the script).
    void Seek(int fr) {
        while (pos < lines.size() && lines[pos].frame <= fr) {
            const Line& l = lines[pos++];
            connected[l.player] = true;
            x[l.player] = l.x;
            y[l.player] = l.y;
            held[l.player] = static_cast<uint8_t>(l.buttons);
        }
    }
    // Input of frame fr for both players.
    void Frame(int fr, game::WorldInput out[2]) {
        uint8_t pressedNow[2] = {};
        uint8_t before[2] = { held[0], held[1] };
        while (pos < lines.size() && lines[pos].frame <= fr) {
            const Line& l = lines[pos++];
            const uint8_t old = held[l.player];
            connected[l.player] = true;
            x[l.player] = l.x;
            y[l.player] = l.y;
            held[l.player] = static_cast<uint8_t>(l.buttons);
            pressedNow[l.player] |= static_cast<uint8_t>(l.buttons & ~old);
        }
        for (int p = 0; p < 2; ++p) {
            if (!connected[p]) {
                out[p] = game::WorldInput{};  // pad / keyboard player (input kind 0), not driven
                continue;
            }
            // MakeWorldInput derives `pressed` from buttons & ~prev: pass a "previous" state that gives this frame's
            // edges (several lines in one frame OR their edges like the hook)
            const uint8_t prev = static_cast<uint8_t>(held[p] & ~pressedNow[p]);
            (void)before;
            out[p] = game::MakeWorldInput(x[p], y[p], held[p], prev);
        }
    }
};


double MaxDiff(const float* a, const float* b) {
    double e = 0;
    for (int i = 0; i < 16; ++i) e = std::max(e, std::fabs(static_cast<double>(a[i]) - b[i]));
    return e;
}

// Draw matrices: the capture prints them with %.7g, so a captured value c stands for every value within half a unit
// of its 7th significant digit (0.005 at 10000..99999). The error of a native value is its distance to that interval
// (--strict: the plain difference, as before).
bool g_strictDraws = false;
double MaxDiffPrinted(const float* a, const float* b) {
    if (g_strictDraws) return MaxDiff(a, b);
    double e = 0;
    for (int i = 0; i < 16; ++i) {
        const double c = b[i];
        // (+ half a float ulp: the printed decimal was parsed into a float, rounded by up to that much)
        const double ulp = static_cast<double>(std::nextafter(std::fabs(b[i]), INFINITY)) - std::fabs(c);
        const double half = c != 0.0 ? 0.5 * std::pow(10.0, std::floor(std::log10(std::fabs(c))) - 6.0) + 0.5 * ulp : 0.0;
        e = std::max(e, std::max(0.0, std::fabs(static_cast<double>(a[i]) - c) - half));
    }
    return e;
}

std::string Hex32(uint32_t v) {
    char b[16];
    snprintf(b, sizeof(b), "%X", v);
    return b;
}

struct Check {
    const char* name;
    int ok = 0, bad = 0, firstBad = -1;
    std::string firstWhy;
    void Add(int frame, bool good, const std::string& why = {}) {
        if (good) {
            ++ok;
            return;
        }
        ++bad;
        if (firstBad < 0) {
            firstBad = frame;
            firstWhy = why;
        }
    }
};

}  // namespace

#if defined(_WIN32) && defined(WT_CRASH_TRACE)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
static LONG WINAPI WtCrash(EXCEPTION_POINTERS* e) {
    HANDLE p = GetCurrentProcess(), th = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES);
    SymInitialize(p, nullptr, TRUE);
    CONTEXT c = *e->ContextRecord;
    STACKFRAME64 s{};
    s.AddrPC.Offset = c.Eip; s.AddrPC.Mode = AddrModeFlat;
    s.AddrFrame.Offset = c.Ebp; s.AddrFrame.Mode = AddrModeFlat;
    s.AddrStack.Offset = c.Esp; s.AddrStack.Mode = AddrModeFlat;
    fprintf(stderr, "CRASH %08lX at %08lX (image base %p, fault address %p)\n", e->ExceptionRecord->ExceptionCode, c.Eip,
            static_cast<void*>(GetModuleHandleA(nullptr)), reinterpret_cast<void*>(e->ExceptionRecord->ExceptionInformation[1]));
    for (int i = 0; i < 24 && StackWalk64(IMAGE_FILE_MACHINE_I386, p, th, &s, &c, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr); ++i) {
        char buf[sizeof(SYMBOL_INFO) + 256] = {};
        SYMBOL_INFO* si = reinterpret_cast<SYMBOL_INFO*>(buf);
        si->SizeOfStruct = sizeof(SYMBOL_INFO);
        si->MaxNameLen = 255;
        DWORD64 d = 0;
        IMAGEHLP_LINE64 l{};
        l.SizeOfStruct = sizeof(l);
        DWORD dl = 0;
        const bool hs = SymFromAddr(p, s.AddrPC.Offset, &d, si) != 0;
        const bool hl = SymGetLineFromAddr64(p, s.AddrPC.Offset, &dl, &l) != 0;
        fprintf(stderr, "  %08llX %s %s:%lu\n", s.AddrPC.Offset, hs ? si->Name : "?", hl ? l.FileName : "?", hl ? l.LineNumber : 0);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int Run(int argc, char** argv);

int main(int argc, char** argv) {
#if defined(_WIN32) && defined(WT_CRASH_TRACE)
    SetUnhandledExceptionFilter(WtCrash);
#endif
    setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        return Run(argc, argv);
    } catch (const std::exception& e) {
        printf("exception: %s\n", e.what());
        return 3;
    }
}

int Run(int argc, char** argv) {
    std::vector<std::string> pos;
    int from = -1, to = 1 << 30;
    bool verbose = false, trace = false;
    int vfrom = -1;
    std::string drawFilter = "zzz", scriptPath;
    uint32_t traceEnemy = 0;  // --enemy S: per-frame native / capture state of enemy S
    // P2's gun device mode (0x007DC698): not 6 in the coordinator's setup (the 2p captures' P2 gun record maps raw 384
    // to 52 = the per-player calibration {0, 320, 640}, not the mode-6 table)
    int p2mode = 0;
    bool noGameOver = false;  // --no-gameover: stop at the switch to game mode 7 (as before the game-over port)
    int dumpFrame = -1;       // --dump F: print the native and captured rand calls, sounds and tasks of frame F
    // --stage-start F: start at a later stage's Stage_Start frame F of a game (mode 6 sub-state 1); the state the stage
    // inherits (game::WorldCarry) is read from the capture's frame F - 1 (needs --script)
    int stageStart = -1;
    std::string saveConfig;    // --save-config <file>: write the WorldConfig of the start frame
    bool players1 = false;     // --players1: 0x009C8E80 = 1 / 1 / 1 when the capture lacks it
    std::string komono40Arg;  // --komono40 <12 digits>: 0x007DCDB8 when the capture lacks it
    int viewLimit = 20;
    double viewEps = 2e-3;  // --view-eps: --verbose lists view differences from this size (the check stays at 2e-3)
    // --prefix <capture>: the game starts at that capture's stage start (its config as usual) and runs, uncompared, up
    // to the frame before the main capture's first frame; then every frame of the main capture is compared. Checks a
    // later stage reached inside one run (World::Impl::ChainStage) against a capture of that stage.
    std::string prefixPath;
    std::string civArg;
    Planner plan;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--from" && i + 1 < argc) from = atoi(argv[++i]);
        else if (a == "--to" && i + 1 < argc) to = atoi(argv[++i]);
        else if (a == "--verbose") verbose = true;
        else if (a == "--strict") g_strictDraws = true;  // draw matrices: plain difference (no print-precision interval)
        else if (a == "--vfrom" && i + 1 < argc) vfrom = atoi(argv[++i]);  // --verbose only from frame F on
        else if (a == "--trace") trace = true;
        else if (a == "--draws" && i + 1 < argc) drawFilter = argv[++i];
        else if (a == "--enemy" && i + 1 < argc) traceEnemy = static_cast<uint32_t>(atoi(argv[++i]));
        else if (a == "--script" && i + 1 < argc) scriptPath = argv[++i];
        else if (a == "--p2mode" && i + 1 < argc) p2mode = atoi(argv[++i]);
        else if (a == "--no-gameover") noGameOver = true;
        else if (a == "--dump" && i + 1 < argc) dumpFrame = atoi(argv[++i]);
        else if (a == "--stage-start" && i + 1 < argc) stageStart = atoi(argv[++i]);
        else if (a == "--komono40" && i + 1 < argc) komono40Arg = argv[++i];
        else if (a == "--players1") players1 = true;
        else if (a == "--save-config" && i + 1 < argc) saveConfig = argv[++i];  // the stage-start WorldConfig (play.exe --config)
        else if (a == "--view-limit" && i + 1 < argc) viewLimit = atoi(argv[++i]);  // --verbose: camera view errors listed
        else if (a == "--view-eps" && i + 1 < argc) viewEps = atof(argv[++i]);
        else if (a == "--prefix" && i + 1 < argc) prefixPath = argv[++i];
        else if (a == "--civilians" && i + 1 < argc) civArg = argv[++i];  // R,T: rescued / met (stage start without them)
        else if (a == "--plan" && i + 1 < argc) plan.out = argv[++i];
        else if (a == "--plan-from" && i + 1 < argc) plan.from = atoi(argv[++i]);
        else if (a == "--plan-end" && i + 1 < argc) plan.end = atoi(argv[++i]);
        else if (a == "--plan-prefer" && i + 1 < argc) plan.prefer.push_back(Planner::ParseFilter(argv[++i]));
        else if (a == "--plan-skip" && i + 1 < argc) plan.skip.push_back(Planner::ParseFilter(argv[++i]));
        else if (a == "--plan-quiet" && i + 1 < argc) plan.quiet.push_back(Planner::ParseFilter(std::string("0@") + argv[++i]));
        else if (a == "--plan-debug" && i + 1 < argc) plan.debugFn = static_cast<uint32_t>(strtoul(argv[++i], nullptr, 16));
        else if (a == "--plan-civ" && i + 1 < argc) plan.civ.push_back(Planner::ParseFilter(std::string("0@") + argv[++i]));
        else if (a == "--plan-watch" && i + 1 < argc) plan.watch.push_back(static_cast<uint32_t>(strtoul(argv[++i], nullptr, 0)));
        else pos.push_back(a);
    }
    if (pos.size() < 2) {
        fprintf(stderr, "usage: world_test <game dir> <capture> [--from F] [--to F] [--verbose] [--script file]\n");
        return 2;
    }
    const bool play = !scriptPath.empty();
    Script script;
    if (play && !script.Load(scriptPath)) {
        fprintf(stderr, "cannot read %s\n", scriptPath.c_str());
        return 2;
    }
    Capture cap(pos[1].c_str());
    if (!cap.ok()) {
        fprintf(stderr, "cannot read %s\n", pos[1].c_str());
        return 2;
    }

    game::World world;
    std::string err;
    std::unique_ptr<Capture> prefixCap;
    if (!prefixPath.empty()) {
        prefixCap = std::make_unique<Capture>(prefixPath.c_str());
        if (!prefixCap->ok() || !play) {
            fprintf(stderr, "--prefix needs a readable capture and --script\n");
            return 2;
        }
    }
    Capture& startCap = prefixCap ? *prefixCap : cap;

    // ---- find the stage start frame
    Frame first, before;
    if (stageStart >= 0) {
        if (!play) {
            fprintf(stderr, "--stage-start needs --script\n");
            return 2;
        }
        // (with --prefix: the stage start of the prefix capture, then the main capture compared in the same run)
        if (!startCap.Next(before, stageStart - 1) || before.n != stageStart - 1 || !startCap.Next(first) || first.n != stageStart) {
            fprintf(stderr, "frames %d / %d not in the capture\n", stageStart - 1, stageStart);
            return 2;
        }
        from = stageStart;
    } else if (!play) {
        if (from < 0) from = 2786;
        if (!cap.Next(first, from)) {
            fprintf(stderr, "frame %d not in the capture\n", from);
            return 2;
        }
    } else {
        // first mode-6 frame with stage tasks (0x0045FEF0 -> Stage_Start); --from overrides
        for (;;) {
            if (!startCap.Next(first, from < 0 ? 0 : from)) {
                fprintf(stderr, "no mode-6 stage start in the capture\n");
                return 2;
            }
            if (from >= 0) break;
            auto m = first.w.find(0x009C8E98);
            if (m != first.w.end() && Get<int16_t>(m->second, 0) == 6 && !first.k.empty()) break;
        }
        from = first.n;
    }
    // The serial counter is history dependent (title / instruction scenes): take the stage start's first serial
    // from the capture (smallest new serial in the start frame's K line).
    uint32_t firstSerial = 0x10000;
    if (!first.k.empty()) {
        firstSerial = 0xFFFFFFFFu;
        for (auto& k : first.k) firstSerial = std::min(firstSerial, k.first);
    }
    game::WorldConfig cfg;
    cfg.firstSerial = firstSerial;
    cfg.startFrame = from;
    printf("capture: stage start frame %d\n", from);
    printf("input: task serial counter at the stage start = %u (from the capture)\n", firstSerial);
    if (stageStart >= 0) {
        auto em = first.w.find(0x009A1A08);
        if (em != first.w.end()) cfg.eventMode = Get<int16_t>(em->second, 0);
    }
    if (!world.Load(pos[0], cfg.eventMode, &err)) {
        fprintf(stderr, "load: %s\n", err.c_str());
        return 2;
    }
    if (play) {
        cfg.gameMode = 6;
        cfg.scene = 0;
        auto sc = first.w.find(0x009A32A0);
        cfg.seedCounter = sc != first.w.end() ? Get<uint32_t>(sc->second, 0) : static_cast<uint32_t>(from + 1);
        for (int p = 0; p < 2; ++p) {
            auto pw = first.w.find(p == 0 ? 0x009A5C60 : 0x009A5D90);
            if (pw != first.w.end()) cfg.playerState[p] = Get<int16_t>(pw->second, 2);
        }
        auto gw = first.w.find(0x009C8FC0);  // gun records: per-player calibration at +0x16 (x) / +0x1E (y)
        if (gw != first.w.end() && gw->second.size() >= 0x50)
            for (int p = 0; p < 2; ++p)
                for (int k = 0; k < 8; ++k) cfg.gunCal[p][k] = Get<int16_t>(gw->second, 0x28 * p + 0x16 + 2 * k);
        // credits / free play (0x009C8E60 / 0x009C8E70 blocks, two-player captures): the coordinator's setup has credits
        auto c60 = first.w.find(0x009C8E60), c70 = first.w.find(0x009C8E70);
        if (c60 != first.w.end() && c70 != first.w.end() && c70->second.size() >= 16) {
            cfg.credits = Get<int32_t>(c60->second, 0);
            cfg.freePlay = Get<int32_t>(c70->second, 0) == 1;
            cfg.coinsStart = Get<int32_t>(c70->second, 8);
            cfg.coinsContinue = Get<int32_t>(c70->second, 12);
            printf("input: credits %d, free play %d, coins %d / %d (from the capture)\n", cfg.credits, cfg.freePlay ? 1 : 0, cfg.coinsStart, cfg.coinsContinue);
        }
        auto dw = first.w.find(0x009C8E94);
        if (dw != first.w.end()) cfg.difficulty = Get<int16_t>(dw->second, 0);
        printf("input: seed counter 0x009A32A0 = %u, player states %d / %d, difficulty %d (from the capture)\n", cfg.seedCounter,
               cfg.playerState[0], cfg.playerState[1], cfg.difficulty);
    }
    if (stageStart >= 0) {
        // A later stage (mode 6 sub-state 1, 0x0045FFF0): event mode / scene of the start frame, the inherited state
        // from the frame before (game::WorldCarry, notes/agents/stage2.md)
        auto W = [&](uint32_t a) -> const std::vector<uint8_t>* {
            auto it = before.w.find(a);
            return it == before.w.end() ? nullptr : &it->second;
        };
        auto sc = first.w.find(0x009A2BC0);
        if (sc != first.w.end()) cfg.scene = Get<int16_t>(sc->second, 0);
        game::WorldCarry& c = cfg.carry;
        c.valid = true;
        for (int p = 0; p < 2; ++p) {
            const auto* b = W(p == 0 ? 0x009A5C60 : 0x009A5D90);
            if (!b || b->size() < 0x130) {
                fprintf(stderr, "frame %d: no player struct %d (W 0x009A5C60 / 0x009A5D90, 0x130 bytes)\n", before.n, p + 1);
                return 2;
            }
            auto& q = c.player[p];
            q.state = Get<int16_t>(*b, 2);
            q.lives = Get<int16_t>(*b, 6);
            q.livesShown = Get<int16_t>(*b, 8);
            q.score = Get<int32_t>(*b, 0xC);
            for (int k = 0; k < 4; ++k) q.credit[k] = Get<int32_t>(*b, 0x2C + 4 * k);
            q.nextState = Get<int16_t>(*b, 0x48);
            q.flags12C = Get<uint32_t>(*b, 0x12C);
            q.aim[0] = Get<float>(*b, 0x10);
            q.aim[1] = Get<float>(*b, 0x14);
            q.ammo = Get<int16_t>(*b, 0x1C);
            q.empty = Get<int16_t>(*b, 0x1E);
            q.reloadBlink = Get<int16_t>(*b, 0x20);
            q.outTimer = Get<int32_t>(*b, 0x28);
            q.contTimer = Get<int32_t>(*b, 0x68);
            q.crosshair[0] = Get<float>(*b, 0xE8);
            q.crosshair[1] = Get<float>(*b, 0xEC);
            if (const auto* h = W(0x009C8E08); h && h->size() >= 8) q.hitTimer = Get<int32_t>(*h, 4 * p);
            cfg.playerState[p] = q.state;
        }
        std::string missing;
        auto need = [&](uint32_t a, size_t n) {
            const auto* b = W(a);
            if (!b || b->size() < n) {
                missing += " " + Hex32(a);
                return static_cast<const std::vector<uint8_t>*>(nullptr);
            }
            return b;
        };
        if (auto b = need(0x009C8E96, 2)) c.rank = Get<int16_t>(*b, 0);
        if (auto b = need(0x009C8A7C, 4)) c.rankTimer = Get<int32_t>(*b, 0);
        if (auto b = need(0x009A2C30, 4)) c.rankTimerOn = Get<int32_t>(*b, 0) != 0;
        if (auto b = need(0x009C8E80, 8)) {
            c.players = Get<int16_t>(*b, 0);
            c.rankPrevPlayers = Get<int16_t>(*b, 2);
            c.route = Get<int16_t>(*b, 4);
        } else if (players1) {  // --players1: one-player game (captures without W 0x009C8E80)
            c.players = c.rankPrevPlayers = c.route = 1;
        }
        if (auto b = need(0x009A5900, 4)) {
            c.haveFlags5900 = true;
            c.flags5900 = Get<uint32_t>(*b, 0);
        }
        if (auto b = need(0x009A5920, 160)) {
            c.haveRouteRec = true;
            memcpy(c.routeRec, b->data(), 160);
        }
        if (auto b = need(0x009A2460, 0x240)) {
            c.haveTracers = true;
            for (int p = 0; p < 2; ++p)
                for (int k = 0; k < 6; ++k) c.tracerRoll[p][k] = Get<int32_t>(*b, (p * 6 + k) * 0x30 + 0x18);
        }
        if (auto b = need(0x007DCDB8, 12)) {
            c.haveKomono40 = true;
            memcpy(c.komono40, b->data(), 12);
        }
        {  // civilians met / rescued (only in captures that watch them; else unknown: 0)
            const auto *r = W(0x009CA0EC), *t = W(0x009A21BA), *n = W(0x009C89C0), *k = W(0x009C8EC0), *s = W(0x009C9100);
            if (r && t && n && k && s && r->size() >= 2 && t->size() >= 2 && n->size() >= 20 && k->size() >= 200 && s->size() >= 20) {
                c.haveCivilians = true;
                c.rescueTotal = Get<int16_t>(*r, 0);
                c.civTotal = Get<int16_t>(*t, 0);
                memcpy(c.rescueCount, n->data(), 20);
                memcpy(c.rescueKinds, k->data(), 200);
                memcpy(c.civCount, s->data(), 20);
            } else if (!civArg.empty()) {
                c.haveCivilians = true;
                sscanf(civArg.c_str(), "%d,%d", &c.rescueTotal, &c.civTotal);
                printf("input: civilians rescued / met %d / %d (--civilians)\n", c.rescueTotal, c.civTotal);
            } else {
                printf("note: civilians met / rescued (W 0x009CA0EC / 0x009A21BA / 0x009C89C0 / 0x009C8EC0 / 0x009C9100) not in the capture: 0\n");
            }
        }
        if (komono40Arg.size() == 12) {  // --komono40: for captures without W 0x007DCDB8
            c.haveKomono40 = true;
            for (int k = 0; k < 12; ++k) c.komono40[k] = komono40Arg[k] == '1' ? 1 : 0;
        }
        printf("input (stage start, frame %d state): event mode %d scene %d; P1 st%d l%d sc%d, P2 st%d l%d sc%d; rank %d timer %d; players %d/%d route %d\n",
               before.n, cfg.eventMode, cfg.scene, c.player[0].state, c.player[0].lives, c.player[0].score, c.player[1].state,
               c.player[1].lives, c.player[1].score, c.rank, c.rankTimer, c.players, c.rankPrevPlayers, c.route);
        if (!missing.empty()) printf("WARNING: not in the capture's W lines (defaults used):%s\n", missing.c_str());
        script.Seek(stageStart - 1);
    }
    bool started = false;
    if (prefixCap) {
        // run the prefix uncompared, then compare from the main capture's first frame
        Frame mainFirst;
        if (!cap.Next(mainFirst)) {
            fprintf(stderr, "main capture empty\n");
            return 2;
        }
        game::WorldInput in[2];
        cfg.gameOver = true;
        script.Frame(from, in);
        if (in[1].kind == 1) in[1].deviceMode = p2mode;  // (two-player prefixes: P2's device mode as when compared)
        world.SetInput(0, in[0]);
        world.SetInput(1, in[1]);
        world.StartStage(cfg);
        if (!plan.out.empty()) {
            // --plan: replay to --plan-from, then plan P1's input to --plan-end (no comparison)
            plan.Init();
            std::vector<Script::Line> keep;
            for (const auto& l : script.lines) {
                if (l.frame < plan.from) {
                    keep.push_back(l);
                    char s[96];
                    snprintf(s, sizeof(s), "%d %d %.6f %.6f %d", l.frame, l.player, l.x, l.y, l.buttons);
                    plan.lines.push_back(s);
                }
            }
            script.lines.resize(script.pos);  // (lines up to the start frame were consumed)
            for (const auto& l : keep)
                if (l.frame > from) script.lines.push_back(l);
            int scene = -1, route = -1, mode = -1, lives = -1, civR = -1, civM = -1;
            std::map<uint32_t, std::string> civState;
            std::set<std::string> planNotes;
            for (int fr = from + 1; fr <= plan.end; ++fr) {
                if (fr >= plan.from) {
                    plan::Line l{};
                    if (plan.Decide(world, fr, &l)) script.lines.push_back(Script::Line{ l.frame, l.player, l.x, l.y, l.buttons });
                }
                script.Frame(fr, in);
                world.SetInput(0, in[0]);
                world.SetInput(1, in[1]);
                world.Step();
                for (const auto& n : world.Notes())
                    if (planNotes.insert(n).second) printf("plan %d note: %s\n", fr, n.c_str());
                const auto& ev = world.Events();
                const auto& hp = world.Hud().player[0];
                if (ev.scene != scene || ev.mode != mode || ev.camera.routeBranch != route || hp.lives != lives) {
                    printf("plan %d: event mode %d scene %d variant %d route %d | P1 st%d lives %d score %d\n", fr, ev.mode, ev.scene,
                           ev.variant, ev.camera.routeBranch, hp.state, hp.lives, hp.score);
                    scene = ev.scene;
                    mode = ev.mode;
                    route = ev.camera.routeBranch;
                    lives = hp.lives;
                }
                {
                    int r = 0, m = 0;
                    world.Civilians(&r, &m);
                    if (r != civR || m != civM) printf("plan %d: civilians rescued %d / met %d\n", fr, r, m);
                    civR = r;
                    civM = m;
                }
                std::map<uint32_t, uint32_t> fnOf;
                for (const auto& t : world.Tasks()) fnOf[t.serial] = t.fn;
                for (const auto& e : world.Enemies(true)) {
                    const uint32_t f = fnOf[e.serial];
                    const bool civ = f == 0x0048A920 || f == 0x0048A3E0;
                    bool watched = civ;
                    for (uint32_t wfn : plan.watch) watched = watched || wfn == f;
                    if (!watched) continue;
                    char s[160];
                    snprintf(s, sizeof(s), "%X hp %d beh %d/%d f34 %X", f, e.hp, e.behaviour, e.sub, e.flags34 & 0xFC000000u);
                    if (civState[e.serial] != s) {
                        printf("plan %d:   %s %u %s\n", fr, civ ? "civilian" : "watched", e.serial, s);
                        civState[e.serial] = s;
                    }
                }
            }
            std::ofstream o(plan.out);
            for (const auto& s : plan.lines) o << s << "\n";
            printf("plan: %zu lines written to %s (frames %d..%d planned: %d shots, %d civilian-blocked aims)\n", plan.lines.size(),
                   plan.out.c_str(), plan.from, plan.end, plan.shots, plan.rejects);
            return 0;
        }
        for (int fr = from + 1; fr < mainFirst.n; ++fr) {
            script.Frame(fr, in);
            if (in[1].kind == 1) in[1].deviceMode = p2mode;
            world.SetInput(0, in[0]);
            world.SetInput(1, in[1]);
            world.Step();
        }
        printf("prefix: frames %d..%d run uncompared (%s); comparing from %d\n", from, mainFirst.n - 1, prefixPath.c_str(), mainFirst.n);
        first = std::move(mainFirst);
        from = first.n;
        started = true;
    }

    Check cRand{ "rand state (Z)" }, cRandSeq{ "rand calls (Y: value + serial)" }, cRandSite{ "rand call sites" },
        cTasks{ "task list (K)" }, cDraws{ "draws (M, per task)" }, cStage{ "stage section draws" },
        cLive{ "live enemies 0x009C904A" }, cAtt{ "attackers 0x009C7006" }, cEnemy{ "enemy hp/state (T)" },
        cEnemyPos{ "enemy position (T)" }, cOps{ "event ops (E)" }, cView{ "camera view (native vs captured)" },
        cLock{ "lock-on slots 0..3 (W 0x009A5EC0)" }, cMode{ "game mode / sub-state" }, cLetter{ "letterbox / can-shoot" },
        cRank{ "rank 0x009C8E96" }, cShake{ "hit shake 0x009CA0E4" }, cPlayer{ "players: state/lives/score/ammo/shots/hits" },
        cAim{ "players: aim +0x10/+0x14" }, cSound{ "sound commands (U: id + serial)" }, cGun{ "P1 gun record (raw, aim, valid, fired)" }, cGun2{ "P2 gun record (aim, valid)" },
        cCont{ "continue timers +0x68 / 0x009A2BB8" }, cSub{ "sub-state 0x009C90A4" }, cCredit{ "credits 0x009C8E60" }, cRail{ "players' point / angles 0x009C71E0" },
        cContS{ "CONTINUE sprites (S)" }, cKill{ "kill counter 0x009A244A (change)" };
    int killBase = -1;  // capture value - native value at the first frame with W 0x009A244A
    int drawsOk = 0, drawsBad = 0, notRendered = 0;
    double maxErr = 0;
    std::map<uint32_t, std::pair<int, int>> drawsPerFn;  // fn -> ok, bad
    std::map<uint32_t, int> framesBadByFn;               // fn -> frames where one of its tasks' draws differ
    std::map<uint32_t, int> roundingPerFn;               // fn -> bad draws with the same model and |error| < 1e-2
    std::map<std::pair<int, int>, int> aliases;          // (native model, captured id) with the same matrix
    std::map<std::pair<int, int>, uint32_t> aliasFn;     // the task function of the first such draw
    std::set<std::string> allNotes;
    int shown = 0, viewShown = 0, drawShown = 0;
    int lastFrame = from;
    // Game-over frames (game mode 7, World runs them with WorldConfig::gameOver): the same checks, counted separately
    // (the frames from the one after the switch to mode 7 to the switch to the title). The checks swap their
    // counters with these while such a frame is compared.
    Check* all[] = { &cRand, &cRandSeq, &cRandSite, &cTasks, &cDraws, &cStage, &cLive, &cAtt, &cEnemy, &cEnemyPos, &cOps,
                     &cView, &cLock, &cMode, &cLetter, &cRank, &cShake, &cPlayer, &cAim, &cSound, &cGun, &cGun2, &cCont, &cSub, &cCredit, &cRail, &cContS, &cKill };
    std::vector<Check> goChecks;
    for (Check* c : all) goChecks.push_back(Check{ c->name });
    auto swapGameOver = [&] {
        for (size_t i = 0; i < goChecks.size(); ++i) std::swap(*all[i], goChecks[i]);
    };
    int goFrames = 0, goFirst = -1, goLast = -1, drawsOkMain = 0, drawsBadMain = 0;
    cfg.gameOver = play && !noGameOver;

    Frame cf = std::move(first);
    const bool verboseReq = verbose;
    for (int fr = from; fr <= to; ++fr) {
        verbose = verboseReq && fr >= vfrom;
        const bool inGameOver = play && world.GameMode() == 7 && fr != from;  // this frame runs mode 7
        if (inGameOver) {
            if (goFirst < 0) {
                goFirst = fr;
                drawsOkMain = drawsOk;
                drawsBadMain = drawsBad;
            }
            goLast = fr;
            ++goFrames;
            swapGameOver();
        }
        if (fr != from) {
            if (!cap.Next(cf)) break;
            if (cf.n != fr) {
                printf("capture frame %d missing (got %d)\n", fr, cf.n);
                break;
            }
        }
        if (fr == from && started) {
            game::WorldInput in[2];
            script.Frame(fr, in);
            if (in[1].kind == 1) in[1].deviceMode = p2mode;
            world.SetInput(0, in[0]);
            world.SetInput(1, in[1]);
            world.Step();
        } else if (fr == from) {
            if (play) {
                game::WorldInput in[2];
                script.Frame(fr, in);  // lines up to the start frame (the Stage_Start frame reads input too)
                if (in[1].kind == 1) in[1].deviceMode = p2mode;
                world.SetInput(0, in[0]);
                world.SetInput(1, in[1]);
            }
            if (!saveConfig.empty()) {
                if (game::SaveWorldConfig(cfg, saveConfig)) printf("config written: %s\n", saveConfig.c_str());
                else printf("cannot write %s\n", saveConfig.c_str());
            }
            world.StartStage(cfg);
        } else {
            if (play) {
                game::WorldInput in[2];
                script.Frame(fr, in);
                if (in[1].kind == 1) in[1].deviceMode = p2mode;
                world.SetInput(0, in[0]);
                world.SetInput(1, in[1]);
            }
            world.Step();
        }
        lastFrame = fr;
        for (auto& n : world.Notes()) allNotes.insert(n);
        if (traceEnemy) {
            const EnemyDump* cd = nullptr;
            if (auto it = cf.enemies.find(traceEnemy); it != cf.enemies.end()) cd = &it->second;
            else if (auto ic = cf.civs.find(traceEnemy); ic != cf.civs.end()) cd = &ic->second;
            for (const auto& e : world.Enemies(true)) {
                if (e.serial != traceEnemy) continue;
                printf("%d N beh %d/%d m %d t %d hp %d f34 %X f136C %X pos %.4f %.4f %.4f rot %d tg %d body %.6f %.6f %.6f%s", fr, e.behaviour, e.sub, e.motion, e.tick, e.hp,
                       e.flags34, e.flags136C, e.pos[0], e.pos[1], e.pos[2], e.rot[1], e.target, e.body[0], e.body[1], e.body[2], cd ? "" : "\n");
            }
            if (cd) {
                const auto& d = *cd;
                printf("  | C beh %d/%d m %d/%d t %d/%d hp %d f34 %X f136C %X f1368 %X pos %.4f %.4f %.4f rot %d tg %d body %.6f %.6f %.6f\n", d.beh, d.sub, d.motion, d.motion2,
                       d.tick, d.tick2, d.hp, d.flags, d.f136C, d.f1368, d.pos[0], d.pos[1], d.pos[2], d.rot[1], d.target, d.body[0], d.body[1], d.body[2]);
            }
            for (auto& n : world.Notes()) printf("%d note: %s\n", fr, n.c_str());
        }

        if (fr == dumpFrame) {
            printf("frame %d native rand:", fr);
            for (const auto& r : world.RandCalls()) printf(" %X/%u=%d", r.site, r.serial, r.value);
            printf("\nframe %d capture rand:", fr);
            for (const auto& y : cf.y) printf(" %X/%u=%d", y.site, y.serial, y.value);
            printf("\nframe %d native sounds:", fr);
            for (const auto& s : world.Sounds()) printf(" %X/%u", s.id, s.serial);
            printf("\nframe %d capture sounds:", fr);
            for (const auto& u : cf.u) printf(" %X/%u", u.first, u.second);
            printf("\nframe %d native tasks:", fr);
            for (const auto& t : world.Tasks()) printf(" %u:%X", t.serial, t.fn);
            printf("\nframe %d capture tasks:", fr);
            for (const auto& k : cf.k) printf(" %u:%X", k.first, k.second);
            printf("\n");
            for (auto& n : world.Notes()) printf("frame %d note: %s\n", fr, n.c_str());
        }
        // ---- rand
        if (g_sawU) {
            // World::Impl::Voice lists a voice line as 0x40000000 | id before the line's own sound command; the
            // original's Snd_PlayVoiceLine 0x00435B80 does not go through Snd_PlayGameSound (no U line): not compared
            std::vector<game::World::SoundEvent> mine;
            for (const auto& s : world.Sounds())  // U lines = Snd_PlayGameSound only (not the direct stop / command calls)
                if ((s.id & 0xFFFF0000u) != 0x40000000u && s.kind == game::World::kPlayGameSound) mine.push_back(s);
            bool same = mine.size() == cf.u.size();
            std::string why = "native " + std::to_string(mine.size()) + " capture " + std::to_string(cf.u.size());
            for (size_t i = 0; same && i < mine.size(); ++i)
                if (mine[i].id != cf.u[i].first || mine[i].serial != cf.u[i].second) {
                    same = false;
                    why = "command " + std::to_string(i) + ": native " + Hex32(mine[i].id) + "/" + std::to_string(mine[i].serial) + " capture " + Hex32(cf.u[i].first) + "/" + std::to_string(cf.u[i].second);
                }
            if (!same && mine.size() != cf.u.size())
                for (size_t i = 0; i < std::min(mine.size(), cf.u.size()); ++i)
                    if (mine[i].id != cf.u[i].first) { why += ", first differing command " + std::to_string(i) + ": native " + Hex32(mine[i].id) + " capture " + Hex32(cf.u[i].first); break; }
            cSound.Add(fr, same, why);
        }
        if (cf.hasZ) cRand.Add(fr, world.RandState() == cf.z, "native " + Hex32(world.RandState()) + " capture " + Hex32(cf.z));
        {
            const auto& mine = world.RandCalls();
            bool same = mine.size() == cf.y.size();
            bool sites = same;
            std::string siteWhy = "site mismatch";
            std::string why;
            for (size_t i = 0; i < std::min(mine.size(), cf.y.size()); ++i) {
                if (mine[i].value != cf.y[i].value || mine[i].serial != cf.y[i].serial) {
                    if (same) why = "call " + std::to_string(i) + ": native " + Hex32(mine[i].site) + "/" +
                                    std::to_string(mine[i].serial) + "=" + std::to_string(mine[i].value) + " capture " +
                                    Hex32(cf.y[i].site) + "/" + std::to_string(cf.y[i].serial) + "=" + std::to_string(cf.y[i].value);
                    same = false;
                }
                if (mine[i].site != 0 && mine[i].site != cf.y[i].site && sites) {
                    sites = false;
                    siteWhy = "site mismatch: call " + std::to_string(i) + " native " + Hex32(mine[i].site) + " capture " + Hex32(cf.y[i].site);
                }
            }
            if (mine.size() != cf.y.size() && why.empty()) {
                why = "count native " + std::to_string(mine.size()) + " capture " + std::to_string(cf.y.size());
                const size_t n = std::min(mine.size(), cf.y.size());
                if (mine.size() > n) why += " (first extra native: " + Hex32(mine[n].site) + "/" + std::to_string(mine[n].serial) + ")";
                if (cf.y.size() > n) why += " (first extra capture: " + Hex32(cf.y[n].site) + "/" + std::to_string(cf.y[n].serial) + ")";
            }
            cRandSeq.Add(fr, same, why);
            cRandSite.Add(fr, sites, siteWhy);
        }
        // ---- tasks
        const auto tasks = world.Tasks();
        if (cf.hasK) {
            bool same = tasks.size() == cf.k.size();
            std::string why;
            for (size_t i = 0; i < std::min(tasks.size(), cf.k.size()); ++i)
                if (tasks[i].serial != cf.k[i].first || tasks[i].fn != cf.k[i].second) {
                    if (same) why = "entry " + std::to_string(i) + ": native " + std::to_string(tasks[i].serial) + ":" +
                                    Hex32(tasks[i].fn) + " capture " + std::to_string(cf.k[i].first) + ":" + Hex32(cf.k[i].second);
                    same = false;
                }
            if (tasks.size() != cf.k.size() && why.empty()) {
                why = "count native " + std::to_string(tasks.size()) + " capture " + std::to_string(cf.k.size());
                const size_t n = std::min(tasks.size(), cf.k.size());
                if (tasks.size() > n) why += " (first extra native " + std::to_string(tasks[n].serial) + ":" + Hex32(tasks[n].fn) + ")";
                if (cf.k.size() > n) why += " (first extra capture " + std::to_string(cf.k[n].first) + ":" + Hex32(cf.k[n].second) + ")";
            }
            cTasks.Add(fr, same, why);
        }
        // ---- draws
        {
            // captured view: identity draw of the stage task, else the F line's context
            game::Mat4 V;
            bool haveV = false;
            bool fromF = false;  // no stage draw: the F line's context (end of frame) vs the native context now
            uint32_t stageSerial = 0;
            for (auto& k : cf.k)
                if (k.second == 0x00401260) stageSerial = k.first;
            auto sm = cf.m.find(stageSerial);
            size_t vi = 0;  // (the ending draws model 0x1918 9 units up: not an identity draw)
            if (sm != cf.m.end())
                while (vi < sm->second.size() && sm->second[vi].model == 0x1918) ++vi;
            if (sm != cf.m.end() && vi < sm->second.size()) {
                memcpy(V.m, sm->second[vi].m, 64);
                haveV = true;
            } else if (cf.f && !tasks.empty()) {
                V = game::ViewMatrix(cf.pos, cf.pitch, cf.yaw, cf.roll);
                haveV = true;
                fromF = true;
            }
            if (haveV) {
                game::Mat4 N = world.Camera().view;
                if (fromF) {  // (the views may have been built before the context changed in this frame)
                    const game::WorldCamera now = world.ContextNow();
                    N = game::ViewMatrix(now.pos, now.pitch, now.yaw, now.roll);
                }
                const double e = MaxDiff(N.m, V.m);
                cView.Add(fr, e < 2e-3, "max |V native - V captured| " + std::to_string(e));
                if (verbose && e >= viewEps && viewShown < viewLimit) {
                    ++viewShown;
                    const auto& c = world.Camera();
                    printf("frame %d view err %g (ctx %d) native pos %.4f %.4f %.4f ang %d %d %d | F ctx %d pos %.4f %.4f %.4f ang %d %d %d\n",
                           fr, e, c.ctx, c.pos[0], c.pos[1], c.pos[2], c.pitch, c.yaw, c.roll, cf.ctx, cf.pos[0], cf.pos[1],
                           cf.pos[2], cf.pitch, cf.yaw, cf.roll);
                    for (int k = 0; k < 4; ++k) {  // the shooting camera's look-at candidates (native)
                        float pt[3];
                        int tp = -1;
                        if (world.LockOnInfo(k, pt, &tp)) printf("  native lock slot %d: point %.6f %.6f %.6f target %d\n", k, pt[0], pt[1], pt[2], tp);
                    }
                    if (viewEps < 1e-3) {
                        printf("  native  ");
                        for (float v : c.view.m) printf(" %.9g", v);
                        printf("\n  capture ");
                        for (float v : V.m) printf(" %.9g", v);
                        printf("\n");
                    }
                }
            }
            // a frame the original did not render (catch-up update after a load: no draw line at all) is not compared
            const bool rendered = !play || !cf.m.empty();
            if (!rendered) ++notRendered;
            if (rendered) {
            std::map<uint32_t, std::vector<const game::WorldDraw*>> mine;
            std::map<uint32_t, uint32_t> fnOf;
            for (const auto& d : world.Draws()) {
                if (d.task == 0 && cf.m.count(0) == 0) continue;  // draws outside a task: the capture keeps no ^0 lines
                mine[d.task].push_back(&d);
                fnOf[d.task] = d.fn;
            }
            for (auto& t : tasks) fnOf[t.serial] = t.fn;
            std::set<uint32_t> serials;
            for (auto& [s, v] : mine) serials.insert(s);
            for (auto& [s, v] : cf.m) serials.insert(s);
            bool frameOk = true, stageOk = true;
            std::string why, stageWhy;
            for (uint32_t s : serials) {
                const auto& my = mine[s];
                auto cit = cf.m.find(s);
                static const std::vector<MLine> kNone;
                const std::vector<MLine>& capd = cit == cf.m.end() ? kNone : cit->second;
                const uint32_t f = fnOf.count(s) ? fnOf[s] : 0;
                auto model = [&](const game::WorldDraw* d) {
                    game::Mat4 mv = d->viewSpace ? d->world : d->world * V;
                    return mv;
                };
                if (f == 0x00401260) {  // ordered subsequence (the original culls per object)
                    size_t j = 0;
                    for (const MLine& c : capd) {
                        while (j < my.size() && my[j]->model != c.model) ++j;
                        if (j == my.size()) {
                            stageOk = false;
                            stageWhy = "model " + std::to_string(c.model) + " not in the native section list";
                            static int stageShown = 0;
                            if (verbose && stageShown++ < 3) {
                                printf("frame %d stage draws:\n   native:", fr);
                                for (auto* d : my) printf(" %d", d->model);
                                printf("\n   capture:");
                                for (auto& d : capd) printf(" %d", d.model);
                                printf("\n");
                            }
                            break;
                        }
                        const double e = MaxDiffPrinted(model(my[j]).m, c.m);
                        if (e > 2e-3) {
                            stageOk = false;
                            stageWhy = "model " + std::to_string(c.model) + " err " + std::to_string(e);
                        }
                        ++j;
                    }
                    continue;
                }
                bool ok = my.size() == capd.size();
                std::string tw;
                if (!ok)
                    tw = "task " + std::to_string(s) + " (" + Hex32(f) + "): " + std::to_string(my.size()) + " draws, capture " +
                         std::to_string(capd.size());
                for (size_t i = 0; i < std::min(my.size(), capd.size()); ++i) {
                    double e = my[i]->model == capd[i].model ? MaxDiffPrinted(model(my[i]).m, capd[i].m) : 1e9;
                    // The capture maps model pointers back to ids through a cache filled on first sight
                    // (scene_capture.cpp ModelId): a model loaded later into a freed slot's memory is logged with
                    // the old id. A different id with the same matrix counts as such an alias (listed at the end).
                    if (my[i]->model != capd[i].model && MaxDiffPrinted(model(my[i]).m, capd[i].m) < 2e-3) {
                        if (++aliases[{ my[i]->model, capd[i].model }] == 1) aliasFn[{ my[i]->model, capd[i].model }] = f;
                        e = MaxDiffPrinted(model(my[i]).m, capd[i].m);
                    }
                    if (my[i]->model == capd[i].model) maxErr = std::max(maxErr, e < 1e8 ? e : 0.0);
                    const bool good = e < 2e-3;
                    (good ? drawsOk : drawsBad)++;
                    auto& pf = drawsPerFn[f];
                    (good ? pf.first : pf.second)++;
                    if (!good && e < 1e-2) ++roundingPerFn[f];
                    if (!good) {
                        if (verbose && my[i]->model == capd[i].model && Hex32(f).find(drawFilter) != std::string::npos) {
                            const auto& mm = model(my[i]).m;
                            printf("frame %d task %u draw %zu model %d\n   native:", fr, s, i, capd[i].model);
                            for (int k = 0; k < 16; ++k) printf(" %.7g", mm[k]);
                            printf("\n   capture:");
                            for (int k = 0; k < 16; ++k) printf(" %.7g", capd[i].m[k]);
                            printf("\n");
                        }
                        ok = false;
                        if (tw.empty())
                            tw = "task " + std::to_string(s) + " (" + Hex32(f) + ") draw " + std::to_string(i) + ": model " +
                                  std::to_string(my[i]->model) + " vs " + std::to_string(capd[i].model) + " err " + std::to_string(e);
                    }
                }
                if (my.size() != capd.size()) {
                    auto& pf = drawsPerFn[f];
                    pf.second += static_cast<int>(std::max(my.size(), capd.size()) - std::min(my.size(), capd.size()));
                    drawsBad += static_cast<int>(std::max(my.size(), capd.size()) - std::min(my.size(), capd.size()));
                }
                frameOk = frameOk && ok;
                if (!ok) ++framesBadByFn[f];
                if (!ok) {
                    if (why.empty()) why = tw;
                    if (verbose && shown < 400 && tw.find(drawFilter) != std::string::npos) {
                        ++shown;
                        printf("frame %d draws: %s\n", fr, tw.c_str());
                        if (my.size() != capd.size()) {
                            printf("   native:");
                            for (auto* d : my) printf(" %d", d->model);
                            printf("\n   capture:");
                            for (auto& d : capd) printf(" %d", d.model);
                            printf("\n");
                        }
                    }
                }
            }
            cDraws.Add(fr, frameOk, why);
            if (verbose && !frameOk && drawShown < 30) {
                ++drawShown;
                printf("frame %d draws: %s\n", fr, why.c_str());
            }
            cStage.Add(fr, stageOk, stageWhy);
            }
        }
        // ---- enemies
        {
            auto ws = cf.w.find(0x009A5EC0);
            if (ws != cf.w.end() && ws->second.size() >= 32) {
                bool same = true;
                std::string why = "native";
                for (int k = 0; k < 4; ++k) {
                    same = same && (ws->second[8 * k] != 0) == world.LockOnActive(k);
                    why += world.LockOnActive(k) ? " 1" : " 0";
                }
                why += " capture";
                for (int k = 0; k < 4; ++k) why += ws->second[8 * k] ? " 1" : " 0";
                cLock.Add(fr, same, why);
            }
            auto w1 = cf.w.find(0x009C904A), w2 = cf.w.find(0x009C7006);
            if (w1 != cf.w.end()) {
                const int v = Get<int16_t>(w1->second, 0);
                cLive.Add(fr, v == world.LiveEnemies(), "native " + std::to_string(world.LiveEnemies()) + " capture " + std::to_string(v));
            }
            if (w2 != cf.w.end()) {
                const int v = Get<int16_t>(w2->second, 0);
                cAtt.Add(fr, v == world.Attackers(), "native " + std::to_string(world.Attackers()) + " capture " + std::to_string(v));
            }
            std::map<uint32_t, game::World::EnemyInfo> mine;
            for (auto& e : world.Enemies()) mine[e.serial] = e;
            bool ok = mine.size() == cf.enemies.size(), posOk = ok;
            std::string why, posWhy;
            if (!ok) why = "count native " + std::to_string(mine.size()) + " capture " + std::to_string(cf.enemies.size());
            for (auto& [s, d] : cf.enemies) {
                auto it = mine.find(s);
                if (it == mine.end()) {
                    ok = posOk = false;
                    continue;
                }
                const auto& e = it->second;
                const bool same = e.hp == d.hp && e.behaviour == d.beh && e.sub == d.sub && e.flags34 == d.flags;
                if (!same && why.empty())
                    why = "serial " + std::to_string(s) + ": native hp " + std::to_string(e.hp) + " beh " + std::to_string(e.behaviour) +
                          "/" + std::to_string(e.sub) + " flags " + Hex32(e.flags34) + " | capture hp " + std::to_string(d.hp) +
                          " beh " + std::to_string(d.beh) + "/" + std::to_string(d.sub) + " flags " + Hex32(d.flags);
                ok = ok && same;
                double pe = 0;
                for (int i = 0; i < 3; ++i) pe = std::max(pe, std::fabs(static_cast<double>(e.pos[i]) - d.pos[i]));
                double be = 0;
                for (int i = 0; i < 3; ++i) be = std::max(be, std::fabs(static_cast<double>(e.body[i]) - d.body[i]));
                if (verbose && (pe > 1e-4 || be > 1e-4 || e.tick != d.tick || e.rot[1] != d.rot[1]) && shown < 60) {
                    ++shown;
                    printf("frame %d enemy %u: pos d %.2g tick %d/%d rot %d/%d body (%g %g %g)/(%g %g %g) view z %g/%g\n", fr, s, pe, e.tick, d.tick, e.rot[1], d.rot[1], e.body[0], e.body[1], e.body[2], d.body[0], d.body[1], d.body[2], e.viewPos[2], d.view[2]);
                }
                if (pe > 1e-3) {
                    if (posWhy.empty())
                        posWhy = "serial " + std::to_string(s) + ": native (" + std::to_string(e.pos[0]) + ", " + std::to_string(e.pos[1]) +
                                 ", " + std::to_string(e.pos[2]) + ") capture (" + std::to_string(d.pos[0]) + ", " +
                                 std::to_string(d.pos[1]) + ", " + std::to_string(d.pos[2]) + ")";
                    posOk = false;
                }
            }
            if (!cf.enemies.empty() || !mine.empty()) {
                cEnemy.Add(fr, ok, why);
                cEnemyPos.Add(fr, posOk, posWhy);
            }
        }
        // ---- event ops
        {
            const auto& ops = world.EventOps();
            bool same = ops.size() == cf.e.size();
            std::string opWhy;
            for (size_t i = 0; i < std::min(ops.size(), cf.e.size()); ++i) {
                const bool eq = ops[i].opcode == cf.e[i].first && ops[i].addr == cf.e[i].second;
                if (!eq && opWhy.empty()) {
                    char b[96];
                    snprintf(b, sizeof b, " (op %zu: native %X@%08X capture %X@%08X)", i, static_cast<unsigned>(ops[i].opcode),
                             static_cast<unsigned>(ops[i].addr), static_cast<unsigned>(cf.e[i].first), static_cast<unsigned>(cf.e[i].second));
                    opWhy = b;
                }
                same = same && eq;
            }
            cOps.Add(fr, same, "native " + std::to_string(ops.size()) + " ops, capture " + std::to_string(cf.e.size()) + opWhy);
        }
        // ---- game / player state (play mode)
        if (play) {
            const game::WorldHud& h = world.Hud();
            auto W16 = [&](uint32_t a, size_t off = 0) -> int {
                auto it = cf.w.find(a);
                return it == cf.w.end() ? -99999 : Get<int16_t>(it->second, off);
            };
            auto W32 = [&](uint32_t a, size_t off = 0) -> int {
                auto it = cf.w.find(a);
                return it == cf.w.end() ? -99999 : Get<int32_t>(it->second, off);
            };
            auto W8 = [&](uint32_t a) -> int {
                auto it = cf.w.find(a);
                return it == cf.w.end() || it->second.empty() ? -99999 : it->second[0];
            };
            if (cf.w.count(0x009C8E98)) {
                const int m = W16(0x009C8E98);
                cMode.Add(fr, m == world.GameMode(), "native mode " + std::to_string(world.GameMode()) + " capture " + std::to_string(m));
                const int lb = W8(0x009CA0F4), cs = W32(0x009C8E00);
                if (world.GameMode() == 6)
                    cLetter.Add(fr, lb == h.letterbox && (cs != 0) == h.canShoot,
                                "native " + std::to_string(h.letterbox) + "/" + std::to_string(h.canShoot) + " capture " + std::to_string(lb) + "/" + std::to_string(cs));
                const int rk = W16(0x009C8E96);
                cRank.Add(fr, rk == h.rank, "native " + std::to_string(h.rank) + " capture " + std::to_string(rk));
                const int sh = W32(0x009CA0E4);
                cShake.Add(fr, sh == h.shake, "native " + std::to_string(h.shake) + " capture " + std::to_string(sh));
                if (cf.w.count(0x009A244A)) {  // kill counter (word, counted since boot): compared as the change since
                    const auto& g = world.Events().globals;  // the first frame that has it
                    const auto gi = g.find(0x009A244A);
                    const int nat = gi == g.end() ? 0 : static_cast<int>(gi->second & 0xFFFF), cap = W16(0x009A244A) & 0xFFFF;
                    if (killBase < 0) killBase = (cap - nat) & 0xFFFF;
                    cKill.Add(fr, ((nat + killBase) & 0xFFFF) == cap,
                              "native " + std::to_string((nat + killBase) & 0xFFFF) + " capture " + std::to_string(cap));
                }
                bool pok = true, aok = true;
                std::string pwhy, awhy;
                for (int p = 0; p < 2; ++p) {
                    const uint32_t a = p == 0 ? 0x009A5C60 : 0x009A5D90;
                    if (!cf.w.count(a)) continue;
                    const auto& hp = h.player[p];
                    const int st = W16(a, 2), lv = W16(a, 6), sc = W32(a, 0xC), am = W16(a, 0x1C), em = W16(a, 0x1E), shots = W16(a, 0x24),
                              hits = W16(a, 0x26);
                    const bool same = st == hp.state && lv == hp.lives && sc == hp.score && am == hp.ammo && (em != 0) == hp.empty &&
                                      shots == hp.shots && hits == hp.hits;
                    if (!same && pwhy.empty()) {
                        char b[256];
                        snprintf(b, sizeof(b), "P%d native st%d l%d sc%d am%d em%d sh%d ht%d | capture st%d l%d sc%d am%d em%d sh%d ht%d", p + 1, hp.state,
                                 hp.lives, hp.score, hp.ammo, hp.empty ? 1 : 0, hp.shots, hp.hits, st, lv, sc, am, em, shots, hits);
                        pwhy = b;
                    }
                    pok = pok && same;
                    const auto it = cf.w.find(a);
                    const float ax = Get<float>(it->second, 0x10), ay = Get<float>(it->second, 0x14);
                    const bool asame = ax == hp.aim[0] && ay == hp.aim[1];
                    if (!asame && awhy.empty()) {
                        char b[160];
                        snprintf(b, sizeof(b), "P%d native (%g, %g) capture (%g, %g)", p + 1, hp.aim[0], hp.aim[1], ax, ay);
                        awhy = b;
                    }
                    aok = aok && asame;
                }
                cPlayer.Add(fr, pok, pwhy);
                cAim.Add(fr, aok, awhy);
                auto g = cf.w.find(0x009C8FC0);
                if (g != cf.w.end() && world.GameMode() == 6) {
                    const int gx = Get<int16_t>(g->second, 4), gy = Get<int16_t>(g->second, 6), gv = Get<int16_t>(g->second, 0x10);
                    const bool same = gx == h.player[0].gunX && gy == h.player[0].gunY && (gv != 0) == h.player[0].gunValid;
                    char b[160];
                    snprintf(b, sizeof(b), "native (%d, %d) v%d capture (%d, %d) v%d", h.player[0].gunX, h.player[0].gunY, h.player[0].gunValid ? 1 : 0, gx, gy, gv);
                    cGun.Add(fr, same, b);
                    if (g->second.size() >= 0x50 && world.Hud().player[1].inputKind == 1) {  // P2 record (two players)
                        const int x2 = Get<int16_t>(g->second, 0x2C), y2 = Get<int16_t>(g->second, 0x2E), v2 = Get<int16_t>(g->second, 0x38);
                        const auto& h2 = h.player[1];
                        snprintf(b, sizeof(b), "native (%d, %d) v%d capture (%d, %d) v%d", h2.gunX, h2.gunY, h2.gunValid ? 1 : 0, x2, y2, v2);
                        cGun2.Add(fr, x2 == h2.gunX && y2 == h2.gunY && (v2 != 0) == h2.gunValid, b);
                    }
                }
                // continue countdowns: per player +0x68, whole game 0x009A2BB8 (two-player captures)
                {
                    bool same = true;
                    std::string why;
                    for (int p = 0; p < 2; ++p) {
                        const uint32_t a = p == 0 ? 0x009A5C60 : 0x009A5D90;
                        if (!cf.w.count(a)) continue;
                        const int c = W32(a, 0x68);
                        if (c != h.player[p].continueTimer) {
                            if (why.empty()) why = "P" + std::to_string(p + 1) + " native " + std::to_string(h.player[p].continueTimer) + " capture " + std::to_string(c);
                            same = false;
                        }
                    }
                    if (cf.w.count(0x009A2BB8) && W32(0x009A2BB8) != h.continueTimer) {
                        if (why.empty()) why = "game native " + std::to_string(h.continueTimer) + " capture " + std::to_string(W32(0x009A2BB8));
                        same = false;
                    }
                    cCont.Add(fr, same, why);
                }
                // CONTINUE sprites (S): WorldHud::continueShown -> hud.h ContinueScreen / ContinuePlayer, as play.exe
                // draws them, vs the original's (only on frames where either side has some)
                if (g_sawS) {
                    std::vector<game::SpriteDef> mine;
                    if (h.continueShown >= 0) {
                        game::HudState cs;
                        cs.continueCount = h.continueShown;
                        for (auto& d : game::ContinueScreen(cs)) mine.push_back(d);
                    }
                    for (int p = 1; p >= 0; --p)
                        if (h.player[p].continueShown >= 0)
                            for (auto& d : game::ContinuePlayer(p, h.player[p].continueShown)) mine.push_back(d);
                    if (!mine.empty() || !cf.cont.empty()) {
                        bool same = mine.size() == cf.cont.size();
                        std::string why = "native " + std::to_string(mine.size()) + " capture " + std::to_string(cf.cont.size());
                        for (size_t i = 0; same && i < mine.size(); ++i)
                            if (!SameSprite(mine[i], cf.cont[i])) {
                                same = false;
                                char b[160];
                                snprintf(b, sizeof(b), "sprite %zu: native %X (%g, %g) capture %X (%g, %g)", i, mine[i].id, mine[i].x, mine[i].y,
                                         cf.cont[i].id, cf.cont[i].x, cf.cont[i].y);
                                why = b;
                            }
                        cContS.Add(fr, same, why);
                    }
                }
                if (cf.w.count(0x009C8E60))
                    cCredit.Add(fr, W32(0x009C8E60) == h.credits, "native " + std::to_string(h.credits) + " capture " + std::to_string(W32(0x009C8E60)));
                if (cf.w.count(0x009C90A4) && world.GameMode() == W16(0x009C8E98))
                    cSub.Add(fr, W32(0x009C90A4) == h.subState, "native " + std::to_string(h.subState) + " capture " + std::to_string(W32(0x009C90A4)));
                // players' point 0x009C71E0 (rail; two players: the midpoint of the bodies, 0x0040C9C0) and angles
                {
                    auto r = cf.w.find(0x009C71E0);
                    if (r != cf.w.end() && r->second.size() >= 12) {
                        const auto& c = world.Camera();
                        bool same = true;
                        for (int k = 0; k < 3; ++k) same = same && Get<float>(r->second, 4 * k) == c.playerPos[k];
                        if (r->second.size() >= 24)
                            same = same && Get<int32_t>(r->second, 12) == c.playerPitch && Get<int32_t>(r->second, 16) == c.playerYaw &&
                                   Get<int32_t>(r->second, 20) == c.playerRoll;
                        char b[256];
                        snprintf(b, sizeof(b), "native (%g %g %g | %d %d %d) capture (%g %g %g | %d %d %d)", c.playerPos[0], c.playerPos[1],
                                 c.playerPos[2], c.playerPitch, c.playerYaw, c.playerRoll, Get<float>(r->second, 0), Get<float>(r->second, 4),
                                 Get<float>(r->second, 8), Get<int32_t>(r->second, 12), Get<int32_t>(r->second, 16), Get<int32_t>(r->second, 20));
                        cRail.Add(fr, same, b);
                        if (verbose && !same) printf("frame %d rail: %s\n", fr, b);
                    }
                }
                if (trace) {
                    printf("%d ppos %.2f %.2f %.2f mode %d/%d sub %d lb %d/%d rank %d/%d shake %d/%d P1 st%d/%d l%d/%d sc%d/%d am%d/%d sh%d/%d aim(%g,%g)/(%g,%g) Y %zu/%zu K %zu/%zu E %zu/%zu\n",
                           fr, world.Camera().playerPos[0], world.Camera().playerPos[1], world.Camera().playerPos[2], world.GameMode(), m, h.subState, h.letterbox, lb, h.rank, rk, h.shake, sh, h.player[0].state, W16(0x009A5C60, 2),
                           h.player[0].lives, W16(0x009A5C60, 6), h.player[0].score, W32(0x009A5C60, 0xC), h.player[0].ammo,
                           W16(0x009A5C60, 0x1C), h.player[0].shots, W16(0x009A5C60, 0x24), h.player[0].aim[0], h.player[0].aim[1],
                           Get<float>(cf.w[0x009A5C60], 0x10), Get<float>(cf.w[0x009A5C60], 0x14), world.RandCalls().size(), cf.y.size(),
                           tasks.size(), cf.k.size(), world.EventOps().size(), cf.e.size());
                }
            }
        }
        if (verbose && shown < 40 && (cRand.firstBad == fr || cTasks.firstBad == fr || cDraws.firstBad == fr)) {
            ++shown;
            printf("frame %d notes:", fr);
            for (auto& n : world.Notes()) printf(" [%s]", n.c_str());
            printf("\n");
        }
        if (inGameOver) swapGameOver();
        if (play && world.GameMode() != 6) {
            if (world.GameMode() == 7 && cfg.gameOver) {
                if (!inGameOver) printf("game mode 7 reached at frame %d: game-over frames counted separately\n", fr);
                continue;
            }
            printf("game mode %d reached at frame %d: end of the comparison\n", world.GameMode(), fr);
            break;
        }
    }

    if (goFirst >= 0) {
        printf("\ngame-over frames (game mode 7) compared: %d (%d..%d)\n", goFrames, goFirst, goLast);
        for (const Check& c : goChecks) {
            if (c.ok + c.bad == 0) continue;
            printf("  %-40s %5d / %5d frames match", c.name, c.ok, c.ok + c.bad);
            if (c.firstBad >= 0) printf("; first divergence %d: %s", c.firstBad, c.firstWhy.c_str());
            printf("\n");
        }
        printf("  draw matrices: %d ok, %d bad\n", drawsOk - drawsOkMain, drawsBad - drawsBadMain);
        drawsOk = drawsOkMain;
        drawsBad = drawsBadMain;
        lastFrame = goFirst - 1;
    }
    const int total = lastFrame - from + 1;
    printf("\nframes compared: %d (%d..%d)\n", total, from, lastFrame);
    std::vector<const Check*> checks = { &cRand, &cRandSeq, &cRandSite, &cTasks, &cDraws, &cStage, &cLive, &cAtt, &cEnemy, &cEnemyPos, &cOps, &cView, &cLock };
    if (cSound.ok + cSound.bad > 0) checks.push_back(&cSound);
    if (play)
        for (const Check* c : { &cMode, &cLetter, &cRank, &cShake, &cPlayer, &cAim, &cGun, &cGun2, &cCont, &cSub, &cCredit, &cRail, &cContS, &cKill })
            if (c->ok + c->bad > 0) checks.push_back(c);
    for (const Check* c : checks) {
        printf("%-42s %5d / %5d frames match", c->name, c->ok, c->ok + c->bad);
        if (c->firstBad >= 0) printf("; first divergence %d: %s", c->firstBad, c->firstWhy.c_str());
        printf("\n");
    }
    printf("draw matrices: %d ok, %d bad (max |W*V - M| %.3g over matching models)\n", drawsOk, drawsBad, maxErr);
    if (play) printf("frames not rendered by the original (no draw lines; draws not compared): %d\n", notRendered);
    for (auto& [f, v] : drawsPerFn)
        printf("  fn %08X: %d ok, %d bad (frames with a difference: %d) [error < 1e-2: %d]\n", f, v.first, v.second,
               framesBadByFn[f], roundingPerFn[f]);
    for (auto& [p, n] : aliases)
        printf("  model id alias (capture pointer cache): native %d logged as %d, %d draws (fn %08X)\n", p.first, p.second, n, aliasFn[p]);
    printf("unported paths reached:\n");
    for (auto& n : allNotes) printf("  %s\n", n.c_str());
    return 0;
}
