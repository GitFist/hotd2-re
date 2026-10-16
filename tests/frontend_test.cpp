// frontend_test <game dir> <capture> [--script file] [--credit-setting N] [--from F] [--to F] [--verbose] [--dump F]
//
// Runs game::World from boot (World::Boot, notes/agents/frontend.md) and compares every frame of a capture of the
// ORIGINAL that starts at boot (build\scene_capture_hud.txt, scene_capture_attract_loop.txt, scene_capture_play1full.txt,
// scene_capture_2p.txt, ...) wherever the capture has the data:
//   mode     W 0x009C8E98 / 0x009C90A4 (game mode / sub-state), 0x009A5C44 (mode frame counter), 0x009A3400 (demo
//            index), 0x009A1A08 (event mode), 0x009A2BC0 (scene)
//   tasks    K lines (serial + function, in order): the task serials are history-dependent, so they check that every
//            mode created exactly the original's tasks since boot
//   rand     Z (state after the frame) and Y (calls) lines
//   events   E lines (event-op handler calls)
//   view     the rendered view vs the F line's camera (all modes but the boot logos)
//   draws    M lines tagged with a task serial: model ids and model-view matrices per task (as world_test)
//   sprites  S lines (Spr_Draw definitions) in the modes whose 2D the world draws completely (3 title, 4 mode select,
//            9 how-to-play), PRESS START (id 0x900) in the attract modes, and in game mode 6 the CONTINUE screen /
//            per-player CONTINUE (WorldHud::continueShown -> hud.h ContinueScreen / ContinuePlayer)
// Input: --script (the hook's scripted input, frames = capture frames) from frame 0. Results are listed per game mode.

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
#include "game/sprite.h"
#include "game/world.h"
#include "route_planner.h"

namespace {

struct MLine {
    int model;
    float m[16];
};
struct YLine {
    uint32_t site, serial;
    int value;
};
struct Frame {
    int n = -1;
    bool f = false;
    int ctx = 0, camId = 0, tick = 0, pitch = 0, yaw = 0, roll = 0;
    float pos[3] = {};
    bool hasZ = false, hasK = false, hasS = false;
    uint32_t z = 0;
    std::vector<YLine> y;
    std::vector<std::pair<uint32_t, uint32_t>> k;
    std::map<uint32_t, std::vector<MLine>> m;
    std::vector<std::pair<uint32_t, uint32_t>> e;
    std::map<uint32_t, std::vector<uint8_t>> w;
    std::vector<std::pair<game::SpriteDef, uint32_t>> s;  // definition, caller of Spr_Draw's caller
    std::vector<std::pair<uint32_t, uint32_t>> u;  // Snd_PlayGameSound: id, task serial (HOTD2_SCENECAP_SOUNDS)
    uint32_t lessonFn = 0;  // T line of the training actor 0x6D in 0x00497760 (lesson running) / 0x00497A70 (grading)
    int lessonCount = 0;    // its +0x1330 (the wall-clock timer 0x0049DF20 of the frame)
    struct Enemy {  // T dumps of the enemies 0x004533F0 / 0x00452DA0 (as world_test)
        int hp, beh, sub, tick;
        uint32_t flags;
        float pos[3], body[3];
    };
    std::map<uint32_t, Enemy> enemies;
};

// The lessons' time left (0x00498C10: digits 0xACF + secs / 10 and secs % 10 from 0x00498D4C / 0x00498D70, scale
// (d % 60) / 60 + 1 below 181 frames): d = limit - timer, or -99999 when the sprites do not determine it.
int TimeLeftFromSprites(const Frame& f) {
    int tens = -1, units = -1;
    float s = 1.0f;
    for (auto& [d, caller] : f.s) {
        if (caller == 0x00498D4C) tens = d.id - 0xACF, s = d.sx;
        if (caller == 0x00498D70) units = d.id - 0xACF;
    }
    if (tens < 0 || units < 0) return -99999;
    const int secs = tens * 10 + units;
    if (secs > 3) return -99999;
    const int m = static_cast<int>(std::lround((s - 1.0f) / 0.01666666753590107f));
    if (secs == 0) return m;
    return (secs - 1) * 60 + (m == 0 ? 60 : m);
}

std::vector<uint8_t> Hex(const std::string& s) {
    std::vector<uint8_t> out;
    auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    for (size_t i = 0; i + 1 < s.size(); i += 2) out.push_back(static_cast<uint8_t>(nib(s[i]) * 16 + nib(s[i + 1])));
    return out;
}

template <class T> T Get(const std::vector<uint8_t>& b, size_t off) {
    T v{};
    if (off + sizeof(T) <= b.size()) memcpy(&v, &b[off], sizeof(T));
    return v;
}

bool g_sawEnemyT = false;  // the capture dumps the enemies (full T lines of 0x004533F0 / 0x00452DA0): compared from then on

class Capture {
public:
    explicit Capture(const char* path) : in_(path) {}
    bool ok() const { return static_cast<bool>(in_); }
    bool Next(Frame& out) {
        Frame cur;
        for (std::string line; std::getline(in_, line);) {
            if (line.empty()) continue;
            const char c = line[0];
            if (line.size() < 2 || line[1] != ' ') {
                if (c == 'K') cur.hasK = true;
                continue;
            }
            if (c == 'F') {
                std::istringstream ss(line.substr(2));
                ss >> cur.n >> cur.ctx >> cur.camId >> cur.tick >> cur.pos[0] >> cur.pos[1] >> cur.pos[2] >> cur.pitch >> cur.yaw >> cur.roll;
                cur.f = true;
                out = std::move(cur);
                return true;
            }
            Parse(c, line, cur);
        }
        return false;
    }

private:
    static void Parse(char c, const std::string& line, Frame& cur) {
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
            std::istringstream ss(line.substr(2));
            std::string id, caller;
            uint32_t serial = 0;
            ss >> id >> caller >> serial;
            cur.u.push_back({ static_cast<uint32_t>(std::stoul(id, nullptr, 16)), serial });
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
            unsigned serial = 0, fn = 0;
            if (sscanf(line.c_str() + 2, "%u %x", &serial, &fn) != 2) return;
            if (fn == 0x004533F0 || fn == 0x00452DA0) {
                std::istringstream ss(line.substr(2));
                std::string t;
                ss >> t >> t >> t;  // serial, fn, header
                std::vector<uint32_t> words;
                for (std::string x; ss >> x;) words.push_back(static_cast<uint32_t>(std::stoul(x, nullptr, 16)));
                if (words.size() * 4 < 0x1320) return;
                auto h = [&](int off) { return static_cast<int16_t>((words[off / 4] >> (8 * (off & 3))) & 0xFFFF); };
                Frame::Enemy d{};
                d.hp = h(0x11C);
                d.beh = h(0x1310);
                d.sub = h(0x1312);
                d.flags = words[0x34 / 4];
                d.tick = static_cast<int>(words[0x19C / 4]);
                memcpy(d.pos, &words[0x40 / 4], 12);
                memcpy(d.body, &words[0x100 / 4], 12);
                cur.enemies[serial] = d;
                g_sawEnemyT = true;
                return;
            }
            if (fn != 0x00497760 && fn != 0x00497A70) return;
            std::istringstream ss(line.substr(2));
            std::string t;
            for (int i = 0; i < 3 + 0x4CC && ss >> t; ++i) {}
            if (ss >> t) {
                cur.lessonFn = fn;
                cur.lessonCount = static_cast<int>(std::stoul(t, nullptr, 16));
            }
        } else if (c == 'S') {
            cur.hasS = true;
            std::istringstream ss(line.substr(2));
            uint32_t w[14] = {};
            std::string t;
            for (int i = 0; i < 14 && ss >> t; ++i) w[i] = static_cast<uint32_t>(std::stoul(t, nullptr, 16));
            uint32_t caller = 0;
            std::string a, b;
            ss >> a >> b;
            const std::string& c2 = b.size() > 1 ? b : a;  // the caller of Spr_DrawSimple's caller, or Spr_Draw's caller
            if (c2.size() > 1 && c2[0] == '@') caller = static_cast<uint32_t>(std::stoul(c2.substr(1), nullptr, 16));
            cur.s.push_back({ game::SpriteDef::FromWords(w), caller });
        }
    }
    std::ifstream in_;
};

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
    void Frame(int fr, game::WorldInput out[2]) {
        uint8_t pressedNow[2] = {};
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
                out[p] = game::WorldInput{};
                continue;
            }
            out[p] = game::MakeWorldInput(x[p], y[p], held[p], static_cast<uint8_t>(held[p] & ~pressedNow[p]));
        }
    }
};

double MaxDiff(const float* a, const float* b) {
    double e = 0;
    for (int i = 0; i < 16; ++i) e = std::max(e, std::fabs(static_cast<double>(a[i]) - b[i]));
    return e;
}

// draw matrices: the capture prints %.7g, so a captured value stands for an interval of half its last digit (as world_test)
double MaxDiffPrinted(const float* a, const float* b) {
    double e = 0;
    for (int i = 0; i < 16; ++i) {
        const double c = b[i];
        const double half = c != 0.0 ? 0.5 * std::pow(10.0, std::floor(std::log10(std::fabs(c))) - 6.0) : 0.0;
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

bool SameSprite(const game::SpriteDef& a, const game::SpriteDef& b) {
    return a.id == b.id && a.x == b.x && a.y == b.y && a.depth == b.depth && a.sx == b.sx && a.sy == b.sy && a.rot == b.rot &&
           a.alpha == b.alpha && a.blend == b.blend && a.flags == b.flags;
}

std::string SpriteText(const game::SpriteDef& d) {
    char b[160];
    snprintf(b, sizeof(b), "%X (%g, %g) z %g s %g/%g a %g f %X", d.id, d.x, d.y, d.depth, d.sx, d.sy, d.alpha, d.flags);
    return b;
}

const char* ModeName(int m) {
    switch (m) {
    case 3: return "3 title";
    case 4: return "4 mode select";
    case 5: return "5 attract demo";
    case 6: return "6 game";
    case 7: return "7 game over";
    case 8: return "8 ranking";
    case 12: return "12 options";
    case 15: return "15 network";
    case 9: return "9 how-to-play";
    case 10: return "10 story (advevtbl)";
    case 11: return "11 story (adv2evtbl)";
    case 16: return "16 boot logos";
    default: return "other";
    }
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::vector<std::string> pos;
    std::string scriptPath, saveDir, saveNative;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> savePatch;
    int from = 0, to = 1 << 30, dumpFrame = -1, dumpTo = -1, creditSetting = 5;
    int p2mode = 0;  // P2's gun device mode 0x007DC698 (not 6 in the coordinator's setup, as world_test --p2mode)
    bool verbose = false, resync = false;  // --resync: follow the capture's mode switches (World::ForceMode)
    bool wallClock = true;  // --no-wall-clock: the timer 0x0049DF20 counts game frames (no capture values)
    plan::Planner planner;  // --plan: the route planner (route_planner.h) on a world booted like the capture
    struct PokeArg {
        int frame;
        uint32_t addr;
        std::vector<uint8_t> bytes;
    };
    std::vector<PokeArg> pokes;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--script" && i + 1 < argc) scriptPath = argv[++i];
        else if (a == "--from" && i + 1 < argc) from = atoi(argv[++i]);
        else if (a == "--to" && i + 1 < argc) to = atoi(argv[++i]);
        else if (a == "--dump" && i + 1 < argc) dumpFrame = atoi(argv[++i]);
        else if (a == "--dump-to" && i + 1 < argc) dumpTo = atoi(argv[++i]);  // --dump F --dump-to G: frames F..G
        else if (a == "--credit-setting" && i + 1 < argc) creditSetting = atoi(argv[++i]);
        else if (a == "--verbose") verbose = true;
        else if (a == "--resync") resync = true;
        else if (a == "--no-wall-clock") wallClock = false;
        else if (a == "--p2mode" && i + 1 < argc) p2mode = atoi(argv[++i]);
        else if (a == "--save-dir" && i + 1 < argc) saveDir = argv[++i];
        else if (a == "--save-native" && i + 1 < argc) saveNative = argv[++i];  // the port's own save file (read / written)
        else if (a == "--poke" && i + 1 < argc) {  // F:<va>:<hex bytes>[,...]: the capture's HOTD2_POKE in game memory (World::TestPoke)
            for (std::string s = argv[++i]; !s.empty();) {
                const size_t c = s.find(',');
                unsigned f = 0, va = 0;
                char hex[64] = {};
                if (sscanf(s.substr(0, c).c_str(), "%u:%x:%63[0-9A-Fa-f]", &f, &va, hex) == 3) {
                    PokeArg p{ static_cast<int>(f), va, {} };
                    for (size_t k = 0; hex[k] && hex[k + 1]; k += 2) {
                        unsigned b = 0;
                        sscanf(hex + k, "%2x", &b);
                        p.bytes.push_back(static_cast<uint8_t>(b));
                    }
                    pokes.push_back(p);
                }
                s = c == std::string::npos ? std::string() : s.substr(c + 1);
            }
        }
        else if (a == "--plan" && i + 1 < argc) planner.out = argv[++i];  // route planner from boot (world_test --plan)
        else if (a == "--plan-from" && i + 1 < argc) planner.from = atoi(argv[++i]);
        else if (a == "--plan-end" && i + 1 < argc) planner.end = atoi(argv[++i]);
        else if (a == "--plan-prefer" && i + 1 < argc) planner.prefer.push_back(plan::Planner::ParseFilter(argv[++i]));
        else if (a == "--plan-skip" && i + 1 < argc) planner.skip.push_back(plan::Planner::ParseFilter(argv[++i]));
        else if (a == "--plan-quiet" && i + 1 < argc) planner.quiet.push_back(plan::Planner::ParseFilter(std::string("0@") + argv[++i]));
        else if (a == "--plan-debug" && i + 1 < argc) planner.debugFn = static_cast<uint32_t>(strtoul(argv[++i], nullptr, 16));
        else if (a == "--plan-civ" && i + 1 < argc) planner.civ.push_back(plan::Planner::ParseFilter(std::string("0@") + argv[++i]));
        else if (a == "--plan-watch" && i + 1 < argc) planner.watch.push_back(static_cast<uint32_t>(strtoul(argv[++i], nullptr, 0)));
        else if (a == "--save-patch" && i + 1 < argc) {  // <va>:<hex bytes>[,...]: the capture's HOTD2_POKE in the save block
            for (std::string s = argv[++i]; !s.empty();) {
                const size_t c = s.find(','), colon = s.find(':');
                const std::string one = s.substr(0, c);
                if (colon != std::string::npos && colon < one.size()) {
                    std::vector<uint8_t> b;
                    const std::string hex = one.substr(colon + 1);
                    for (size_t k = 0; k + 1 < hex.size(); k += 2) b.push_back(static_cast<uint8_t>(std::stoul(hex.substr(k, 2), nullptr, 16)));
                    savePatch.emplace_back(static_cast<uint32_t>(std::stoul(one.substr(0, colon), nullptr, 16)), b);
                }
                s = c == std::string::npos ? std::string() : s.substr(c + 1);
            }
        }
        else pos.push_back(a);
    }
    if (pos.size() < 2) {
        fprintf(stderr, "usage: frontend_test <game dir> <capture> [--script file] [--credit-setting N] [--from F] [--to F] [--verbose] [--dump F]\n");
        return 2;
    }
    Script script;
    if (!scriptPath.empty() && !script.Load(scriptPath)) {
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
    if (!world.Load(pos[0], 0, &err)) {
        fprintf(stderr, "load: %s\n", err.c_str());
        return 2;
    }
    game::WorldConfig cfg;
    cfg.creditSetting = creditSetting;
    cfg.saveDir = saveDir;  // the original's save files (read only; default: the game directory)
    cfg.savePath = saveNative;
    cfg.savePatch = savePatch;
    cfg.startFrame = 0;
    auto applyPokes = [&](int fr) {  // (the hook pokes at the first input poll of boot frame F = before that frame's step)
        for (const PokeArg& p : pokes)
            if (p.frame == fr && !world.TestPoke(p.addr, p.bytes)) printf("poke %d: address %X (%zu bytes) not supported\n", fr, p.addr, p.bytes.size());
    };

    if (!planner.out.empty()) {
        // --plan: boot, replay the script's lines before --plan-from, then plan P1's input to --plan-end (no comparison;
        // the capture is not read). Prints scene / variant / route / lives / civilian changes like world_test --plan.
        planner.Init();
        std::vector<Script::Line> keep;
        for (const auto& l : script.lines)
            if (l.frame < planner.from) {
                keep.push_back(l);
                char s[96];
                snprintf(s, sizeof(s), "%d %d %.6f %.6f %d", l.frame, l.player, l.x, l.y, l.buttons);
                planner.lines.push_back(s);
            }
        script.lines = keep;
        script.pos = 0;
        int scene = -1, route = -1, emode = -1, lives = -1, civR = -1, civM = -1, gmode = -1;
        std::map<uint32_t, std::string> civState;
        std::set<std::string> planNotes;
        for (int fr = 0; fr <= planner.end; ++fr) {
            if (fr >= planner.from && fr > 0) {
                plan::Line l{};
                if (planner.Decide(world, fr, &l)) script.lines.push_back(Script::Line{ l.frame, l.player, l.x, l.y, l.buttons });
            }
            game::WorldInput in[2];
            script.Frame(fr, in);
            if (in[1].kind == 1) in[1].deviceMode = p2mode;
            world.SetInput(0, in[0]);
            world.SetInput(1, in[1]);
            if (fr == 0) world.Boot(cfg);
            else {
                applyPokes(fr);
                world.Step();
            }
            for (const auto& n : world.Notes())
                if (planNotes.insert(n).second) printf("plan %d note: %s\n", fr, n.c_str());
            if (world.GameMode() != gmode) {
                gmode = world.GameMode();
                printf("plan %d: game mode %d\n", fr, gmode);
            }
            if (gmode != 6) continue;
            const auto& ev = world.Events();
            const auto& hp = world.Hud().player[0];
            const auto& h2 = world.Hud().player[1];
            if (ev.scene != scene || ev.mode != emode || ev.camera.routeBranch != route || hp.lives != lives) {
                printf("plan %d: event mode %d scene %d variant %d route %d | P1 st%d lives %d score %d | P2 st%d lives %d score %d\n", fr, ev.mode,
                       ev.scene, ev.variant, ev.camera.routeBranch, hp.state, hp.lives, hp.score, h2.state, h2.lives, h2.score);
                scene = ev.scene;
                emode = ev.mode;
                route = ev.camera.routeBranch;
                lives = hp.lives;
            }
            int r = 0, m = 0;
            world.Civilians(&r, &m);
            if (r != civR || m != civM) printf("plan %d: civilians rescued %d / met %d\n", fr, r, m);
            civR = r;
            civM = m;
            std::map<uint32_t, uint32_t> fnOf;
            for (const auto& t : world.Tasks()) fnOf[t.serial] = t.fn;
            for (const auto& e : world.Enemies(true)) {
                const uint32_t f = fnOf[e.serial];
                const bool civ = f == 0x0048A920 || f == 0x0048A3E0;
                bool watched = civ;
                for (uint32_t wfn : planner.watch) watched = watched || wfn == f;
                if (!watched) continue;
                char s[160];
                snprintf(s, sizeof(s), "%X hp %d beh %d/%d f34 %X", f, e.hp, e.behaviour, e.sub, e.flags34 & 0xFC000000u);
                if (civState[e.serial] != s) {
                    printf("plan %d:   %s %u %s\n", fr, civ ? "civilian" : "watched", e.serial, s);
                    civState[e.serial] = s;
                }
            }
        }
        std::ofstream o(planner.out);
        for (const auto& s : planner.lines) o << s << "\n";
        printf("plan: %zu lines written to %s (frames %d..%d planned: %d shots, %d civilian-blocked aims)\n", planner.lines.size(),
               planner.out.c_str(), planner.from, planner.end, planner.shots, planner.rejects);
        return 0;
    }

    // checks per (name, game mode)
    std::map<std::pair<std::string, int>, Check> checks;
    std::vector<std::string> order;
    auto add = [&](const std::string& name, int mode, int fr, bool good, const std::string& why = {}) {
        if (std::find(order.begin(), order.end(), name) == order.end()) order.push_back(name);
        if (fr >= from) checks[{ name, mode }].Add(fr, good, why);
        static const char* listBad = getenv("HOTD2_FT_LISTBAD");  // debug: every failing frame of that check
        if (listBad && !good && fr >= from && name == listBad) printf("bad %s %d: %s\n", listBad, fr, why.c_str());
    };
    std::map<int, int> modeFrames;
    std::vector<std::pair<int, int>> switches;  // (frame, mode) native mode changes
    std::set<std::string> notes;
    int lastMode = -1, lastFrame = -1;
    long long drawsOk = 0, drawsBad = 0;
    std::map<uint32_t, std::pair<long long, long long>> fnDraws;  // per task function: draws ok / bad
    std::map<std::pair<int, int>, int> aliases;  // (native model, captured id) with the same matrix
    bool sawY = false, sawS = false, sawE = false, sawU = false;
    bool sawM = false;
    int notRendered = 0;
    int capMode = -1, capPrev = -1, capDemo = -1, resyncs = 0;  // the capture's game mode / demo index after the last frame
    bool capZ = false;
    uint32_t capZv = 0;  // the capture logs rand calls / sprites

    uint32_t clockPrevFn = 0;
    int clockPrevCount = 0, clockPrevD = -99999;
    Frame cf, pend;
    bool havePend = false;
    int uncompared = 0;  // frames before the capture's first one (HOTD2_SCENECAP_FROM): run, not compared
    for (int fr = 0; fr <= to; ++fr) {
        if (!havePend) {
            if (!cap.Next(pend)) break;
            havePend = true;
        }
        if (pend.n < fr) {
            printf("capture frame %d out of order (got %d)\n", fr, pend.n);
            break;
        }
        if (pend.n > fr) {
            game::WorldInput in[2];
            script.Frame(fr, in);
            if (in[1].kind == 1) in[1].deviceMode = p2mode;
            world.SetInput(0, in[0]);
            world.SetInput(1, in[1]);
            if (fr == 0) world.Boot(cfg);
            else {
                applyPokes(fr);
                world.Step();
            }
            lastMode = world.GameMode();
            for (auto& n : world.Notes()) notes.insert(n);
            ++uncompared;
            continue;
        }
        cf = std::move(pend);
        havePend = false;
        game::WorldInput in[2];
        script.Frame(fr, in);
        if (in[1].kind == 1) in[1].deviceMode = p2mode;
        world.SetInput(0, in[0]);
        world.SetInput(1, in[1]);
        if (resync && fr > 0 && capMode >= 0 && capPrev >= 0 && capMode != capPrev) {
            // the original switched modes at the end of the last frame: restart the native in that mode with the new mode's first task serial (this frame's K) and the rand state (last Z)
            uint32_t serial = 0xFFFFFFFFu;
            for (auto& k : cf.k) serial = std::min(serial, k.first);
            const auto tasksNow = world.Tasks();
            if (serial == 0xFFFFFFFFu) serial = 0;
            world.ForceMode(capMode, serial ? serial : 0x10000, capZ ? capZv : world.RandState(), capDemo >= 0 ? capDemo : world.Hud().demoIndex);
            if (!serial) printf("resync at %d: no K line, serial counter unknown\n", fr);
            printf("resync at %d: native mode %d -> capture mode %d (serial %u)\n", fr, lastMode, capMode, serial);
            ++resyncs;
            (void)tasksNow;
        }
        {   // the original's wall-clock timer 0x0049DF20 in this frame (GetTickCount): the boss-mode fight time (W 0x009C7060
            // in game type 3), the lesson timer (the 0x6D actor's +0x1330 in its T line; the frame that ends the lesson
            // resets it, so its value there = the last one + the change of the time-left sprites' value)
            bool have = false;
            int clock = 0;
            auto gt = cf.w.find(0x009CA08C), bt = cf.w.find(0x009C7060);
            if (gt != cf.w.end() && Get<int32_t>(gt->second, 0) == 3 && bt != cf.w.end()) {
                clock = Get<int32_t>(bt->second, 0);
                have = true;
            }
            const int d = TimeLeftFromSprites(cf);
            if (cf.lessonFn == 0x00497760) {
                clock = cf.lessonCount;
                have = true;
            } else if (cf.lessonFn == 0x00497A70 && clockPrevFn == 0x00497760) {
                clock = d != -99999 && clockPrevD != -99999 ? clockPrevCount + clockPrevD - d : clockPrevCount + 1;
                have = true;
            }
            if (have && wallClock) world.SetWallClock(clock);
            clockPrevFn = cf.lessonFn;
            clockPrevCount = cf.lessonCount;
            clockPrevD = d;
        }
        const int ranMode = fr == 0 ? 16 : world.GameMode();  // checks are listed under the mode that ran this frame
        if (fr == 0) {
            world.Boot(cfg);
            for (auto& n : world.Notes()) printf("boot note: %s\n", n.c_str());
        } else {
            if (getenv("FE_TRACE")) fprintf(stderr, "step %d\n", fr);
            applyPokes(fr);
            world.Step();
            if (getenv("FE_TRACE")) fprintf(stderr, "stepped %d\n", fr);
        }
        lastFrame = fr;
        const game::WorldHud& h = world.Hud();
        const int mode = ranMode, modeNow = world.GameMode();
        // a mode committed at the frame's end shows in W already (as in the original)
        if (modeNow != lastMode) switches.push_back({ fr, modeNow });
        lastMode = modeNow;
        ++modeFrames[mode];
        for (auto& n : world.Notes()) notes.insert(n);

        // ---- mode state
        auto W16 = [&](uint32_t a, size_t off = 0) -> int {
            auto it = cf.w.find(a);
            return it == cf.w.end() ? -99999 : Get<int16_t>(it->second, off);
        };
        auto W32 = [&](uint32_t a, size_t off = 0) -> int {
            auto it = cf.w.find(a);
            return it == cf.w.end() ? -99999 : Get<int32_t>(it->second, off);
        };
        if (cf.w.count(0x009C8E98)) {
            const int m = W16(0x009C8E98);
            const int s = cf.w.count(0x009C90A4) ? W32(0x009C90A4) : h.subState;
            add("game mode / sub-state", mode, fr, m == modeNow && s == h.subState,
                "native " + std::to_string(modeNow) + "/" + std::to_string(h.subState) + " capture " + std::to_string(m) + "/" + std::to_string(s));
        }
        if (cf.w.count(0x009A5C44) && mode != 6 && mode != 7)
            add("mode frames 0x009A5C44", mode, fr, W32(0x009A5C44) == h.modeFrames,
                "native " + std::to_string(h.modeFrames) + " capture " + std::to_string(W32(0x009A5C44)));
        if (cf.w.count(0x009A5C40))
            add("boot frames 0x009A5C40", mode, fr, static_cast<uint32_t>(W32(0x009A5C40)) == h.bootFrames,
                "native " + std::to_string(h.bootFrames) + " capture " + std::to_string(W32(0x009A5C40)));
        if (cf.w.count(0x009A3400))
            add("demo index 0x009A3400", mode, fr, W16(0x009A3400) == h.demoIndex,
                "native " + std::to_string(h.demoIndex) + " capture " + std::to_string(W16(0x009A3400)));
        if (cf.w.count(0x009C8E60) && (mode == 4 || mode == 6))
            add("credits 0x009C8E60", mode, fr, W32(0x009C8E60) == h.credits,
                "native " + std::to_string(h.credits) + " capture " + std::to_string(W32(0x009C8E60)));
        if (cf.w.count(0x009C8E96) && mode == 6)
            add("rank 0x009C8E96", mode, fr, W16(0x009C8E96) == h.rank,
                "native " + std::to_string(h.rank) + " capture " + std::to_string(W16(0x009C8E96)));
        if (mode == 6) {  // the players' point 0x009C71E0
            auto r = cf.w.find(0x009C71E0);
            if (r != cf.w.end() && r->second.size() >= 12) {
                const auto& c = world.Camera();
                bool same = true;
                for (int k = 0; k < 3; ++k) same = same && Get<float>(r->second, 4 * k) == c.playerPos[k];
                char why[160];
                snprintf(why, sizeof(why), "native %g %g %g capture %g %g %g", c.playerPos[0], c.playerPos[1], c.playerPos[2],
                         Get<float>(r->second, 0), Get<float>(r->second, 4), Get<float>(r->second, 8));
                add("players' point 0x009C71E0", mode, fr, same, why);
            }
        }
        for (int p = 0; p < 2; ++p) {
            const uint32_t a = p == 0 ? 0x009A5C60 : 0x009A5D90;
            if (cf.w.count(a) && mode != 6 && mode != 7)
                add("player states", mode, fr, W16(a, 2) == h.player[p].state,
                    "P" + std::to_string(p + 1) + " native " + std::to_string(h.player[p].state) + " capture " + std::to_string(W16(a, 2)));
        }
        // ---- rand
        if (cf.hasZ) add("rand state (Z)", mode, fr, world.RandState() == cf.z, "native " + Hex32(world.RandState()) + " capture " + Hex32(cf.z));
        sawY = sawY || !cf.y.empty();
        if (sawY && cf.hasZ) {
            const auto& mine = world.RandCalls();
            bool same = mine.size() == cf.y.size();
            for (size_t i = 0; same && i < mine.size(); ++i) same = mine[i].value == cf.y[i].value && mine[i].serial == cf.y[i].serial;
            add("rand calls (Y)", mode, fr, same, "native " + std::to_string(mine.size()) + " capture " + std::to_string(cf.y.size()));
        }
        // ---- sound commands (U lines = Snd_PlayGameSound; the world's direct stop / command calls are not logged)
        sawU = sawU || !cf.u.empty();
        if (sawU) {
            std::vector<game::World::SoundEvent> mine;
            for (const auto& s : world.Sounds())
                if (s.kind == game::World::kPlayGameSound) mine.push_back(s);
            bool same = mine.size() == cf.u.size();
            std::string why = "native " + std::to_string(mine.size()) + " capture " + std::to_string(cf.u.size());
            for (size_t i = 0; i < std::min(mine.size(), cf.u.size()); ++i)
                if (mine[i].id != cf.u[i].first || mine[i].serial != cf.u[i].second) {
                    why = "command " + std::to_string(i) + ": native " + Hex32(mine[i].id) + "/" + std::to_string(mine[i].serial) + " capture " +
                          Hex32(cf.u[i].first) + "/" + std::to_string(cf.u[i].second);
                    same = false;
                    break;
                }
            if (same || !mine.empty() || !cf.u.empty()) add("sound commands (U)", mode, fr, same, why);
        }
        // ---- tasks
        const auto tasks = world.Tasks();
        if (cf.hasK) {
            bool same = tasks.size() == cf.k.size();
            std::string why;
            for (size_t i = 0; i < std::min(tasks.size(), cf.k.size()); ++i)
                if (tasks[i].serial != cf.k[i].first || tasks[i].fn != cf.k[i].second) {
                    if (same) why = "entry " + std::to_string(i) + ": native " + std::to_string(tasks[i].serial) + ":" + Hex32(tasks[i].fn) +
                                    " capture " + std::to_string(cf.k[i].first) + ":" + Hex32(cf.k[i].second);
                    same = false;
                }
            if (tasks.size() != cf.k.size() && why.empty()) {
                why = "count native " + std::to_string(tasks.size()) + " capture " + std::to_string(cf.k.size());
                const size_t n = std::min(tasks.size(), cf.k.size());
                if (tasks.size() > n) why += " (first extra native " + std::to_string(tasks[n].serial) + ":" + Hex32(tasks[n].fn) + ")";
                if (cf.k.size() > n) why += " (first extra capture " + std::to_string(cf.k[n].first) + ":" + Hex32(cf.k[n].second) + ")";
            }
            add("task list (K)", mode, fr, same, why);
        }
        // ---- events
        sawE = sawE || !cf.e.empty();
        if (sawE) {
            const auto& ops = world.EventOps();
            bool same = ops.size() == cf.e.size();
            for (size_t i = 0; same && i < ops.size(); ++i) same = ops[i].opcode == cf.e[i].first && ops[i].addr == cf.e[i].second;
            if (!ops.empty() || !cf.e.empty())
                add("event ops (E)", mode, fr, same, "native " + std::to_string(ops.size()) + " ops, capture " + std::to_string(cf.e.size()));
        }
        // ---- enemies (T dumps of 0x004533F0 / 0x00452DA0, as world_test): hp / behaviour / flags, position, body point
        if (g_sawEnemyT && mode == 6) {
            std::map<uint32_t, game::World::EnemyInfo> mine;
            for (auto& e : world.Enemies()) mine[e.serial] = e;
            bool ok = mine.size() == cf.enemies.size(), posOk = ok, bodyOk = ok;
            std::string why, posWhy, bodyWhy;
            if (!ok) why = posWhy = bodyWhy = "count native " + std::to_string(mine.size()) + " capture " + std::to_string(cf.enemies.size());
            static const bool enemyWhy = getenv("HOTD2_FT_ENEMYWHY") != nullptr;  // debug: every differing enemy
            for (auto& [s, d] : cf.enemies) {
                auto it = mine.find(s);
                if (it == mine.end()) {
                    ok = posOk = bodyOk = false;
                    continue;
                }
                const auto& e = it->second;
                const bool same = e.hp == d.hp && e.behaviour == d.beh && e.sub == d.sub && e.flags34 == d.flags;
                if (!same && why.empty())
                    why = "serial " + std::to_string(s) + ": native hp " + std::to_string(e.hp) + " beh " + std::to_string(e.behaviour) + "/" +
                          std::to_string(e.sub) + " flags " + Hex32(e.flags34) + " | capture hp " + std::to_string(d.hp) + " beh " +
                          std::to_string(d.beh) + "/" + std::to_string(d.sub) + " flags " + Hex32(d.flags);
                ok = ok && same;
                double pe = 0, be = 0;
                for (int i = 0; i < 3; ++i) pe = std::max(pe, std::fabs(static_cast<double>(e.pos[i]) - d.pos[i]));
                for (int i = 0; i < 3; ++i) be = std::max(be, std::fabs(static_cast<double>(e.body[i]) - d.body[i]));
                char b[200];
                snprintf(b, sizeof(b), "serial %u: native (%g %g %g) capture (%g %g %g)", s, e.pos[0], e.pos[1], e.pos[2], d.pos[0], d.pos[1], d.pos[2]);
                if (pe > 1e-3) {
                    if (posWhy.empty()) posWhy = b;
                    posOk = false;
                }
                if (be > 1e-3) {  // (as the position: a few float ulps of skeleton rounding pass)
                    if (bodyWhy.empty()) {
                        snprintf(b, sizeof(b), "serial %u: native (%g %g %g) capture (%g %g %g)", s, e.body[0], e.body[1], e.body[2], d.body[0], d.body[1], d.body[2]);
                        bodyWhy = b;
                    }
                    bodyOk = false;
                }
                static const double whyEps = getenv("HOTD2_FT_ENEMYEPS") ? atof(getenv("HOTD2_FT_ENEMYEPS")) : 1e-4;
                if (enemyWhy && (!same || pe > whyEps || be > whyEps || e.tick != d.tick))
                    printf("enemy frame %d serial %u: hp %d/%d beh %d/%d %d/%d flags %X/%X tick %d/%d pos d %.3g body (%g %g %g)/(%g %g %g)\n", fr, s,
                           e.hp, d.hp, e.behaviour, d.beh, e.sub, d.sub, e.flags34, d.flags, e.tick, d.tick, pe, e.body[0], e.body[1],
                           e.body[2], d.body[0], d.body[1], d.body[2]);
            }
            if (!cf.enemies.empty() || !mine.empty()) {
                add("enemy hp/state (T)", mode, fr, ok, why);
                add("enemy position (T)", mode, fr, posOk, posWhy);
                add("enemy body point (T)", mode, fr, bodyOk, bodyWhy);
            }
        }
        // ---- view and draws
        game::Mat4 V;
        bool haveV = false;
        bool fromF = false;  // no stage draw: the F line's context vs the native context at the end of the frame
        {
            uint32_t stageSerial = 0;
            for (auto& k : cf.k)
                if (k.second == 0x00401260) stageSerial = k.first;
            auto sm = cf.m.find(stageSerial);
            if (sm != cf.m.end() && !sm->second.empty()) {
                memcpy(V.m, sm->second[0].m, 64);
                haveV = true;
            } else if (cf.f && mode != 16) {
                V = game::ViewMatrix(cf.pos, cf.pitch, cf.yaw, cf.roll);
                haveV = true;
                fromF = true;
            }
        }
        if (haveV && mode != 16 && mode != 4) {
            game::Mat4 N = world.Camera().view;
            if (fromF) {  // (the views may have been built before the context changed in this frame)
                const game::WorldCamera now = world.ContextNow();
                N = game::ViewMatrix(now.pos, now.pitch, now.yaw, now.roll);
            }
            const double e = MaxDiff(N.m, V.m);
            add("camera view", mode, fr, e < 2e-3, "max |V native - V captured| " + std::to_string(e));
            static const bool camWhy = getenv("HOTD2_FT_CAMWHY") != nullptr;  // debug: both contexts of each bad frame
            if (camWhy && !(e < 2e-3)) {
                const game::WorldCamera now = world.ContextNow();
                printf("camera frame %d: err %g native pos %.4f %.4f %.4f ang %d %d %d capture F pos %.4f %.4f %.4f ang %d %d %d%s\n", fr, e,
                       now.pos[0], now.pos[1], now.pos[2], now.pitch, now.yaw, now.roll, cf.pos[0], cf.pos[1], cf.pos[2], cf.pitch, cf.yaw,
                       cf.roll, fromF ? " (F)" : " (stage draw)");
            }
            if (fr == dumpFrame || (dumpFrame >= 0 && dumpFrame <= fr && fr <= dumpTo)) {
                const game::WorldCamera now = world.ContextNow();
                printf(" native view (ctx %d pos %g %g %g angles %d %d %d):", now.ctx, now.pos[0], now.pos[1], now.pos[2], now.pitch, now.yaw, now.roll);
                for (float v : N.m) printf(" %g", v);
                printf("\n capture view%s:", fromF ? " (F line)" : "");
                for (float v : V.m) printf(" %g", v);
                printf("\n");
                // the camera context (0x009A6000 + 0x1A4 * ctx, W 0x9A6000 / 0x9A61A4): camera matrix +0x40, target +0xD8
                const auto& cd = world.Events().camera;
                const int ci = cd.activeCtx & 3;
                const auto& cx = cd.ctx[ci];
                printf(" native ctx %d camera:", ci);
                for (float v : cx.camera) printf(" %.9g", v);
                printf(" target %.9g %.9g %.9g\n", cx.target[0], cx.target[1], cx.target[2]);
                auto wc = cf.w.find(ci == 0 ? 0x009A6000 : ci == 1 ? 0x009A61A4 : 0);  // (contexts 0 / 1 are watched)
                if (ci <= 1 && wc != cf.w.end() && wc->second.size() >= 0xE4) {
                    printf(" capture ctx %d camera:", ci);
                    for (int k = 0; k < 16; ++k) printf(" %.9g", Get<float>(wc->second, 0x40 + 4 * k));
                    printf(" target %.9g %.9g %.9g\n", Get<float>(wc->second, 0xD8), Get<float>(wc->second, 0xDC), Get<float>(wc->second, 0xE0));
                }
                for (int k = 0; k < 16; ++k) {
                    float pt[3];
                    int tp = -1;
                    if (world.LockOnInfo(k, pt, &tp)) printf(" native lock slot %d: %.9g %.9g %.9g target player %d\n", k, pt[0], pt[1], pt[2], tp);
                }
            }
        }
        // a frame without any draw line was not rendered by the original (0x0049DFD0 at a mode's init sets the skip flag
        // 0x007DEBAC): not compared
        if (sawM && cf.m.empty() && !world.Draws().empty()) ++notRendered;
        sawM = sawM || !cf.m.empty();
        if (haveV && !cf.m.empty() && fr >= from) {
            std::map<uint32_t, std::vector<const game::WorldDraw*>> mine;
            std::map<uint32_t, uint32_t> fnOf;
            for (const auto& d : world.Draws()) {
                if (d.task == 0) continue;  // the capture's ^0 lines are not read (draws of the mode functions themselves)
                mine[d.task].push_back(&d);
                fnOf[d.task] = d.fn;
            }
            for (auto& t : tasks) fnOf[t.serial] = t.fn;
            std::set<uint32_t> serials;
            for (auto& [s, v] : mine) serials.insert(s);
            for (auto& [s, v] : cf.m) serials.insert(s);
            bool ok = true;
            std::string why;
            for (uint32_t s : serials) {
                const auto& my = mine[s];
                static const std::vector<MLine> kNone;
                auto cit = cf.m.find(s);
                const std::vector<MLine>& capd = cit == cf.m.end() ? kNone : cit->second;
                const uint32_t f = fnOf.count(s) ? fnOf[s] : 0;
                auto mv = [&](const game::WorldDraw* d) { return d->viewSpace ? d->world : d->world * V; };
                if (f == 0x00401260) {  // the original culls per object: ordered subsequence
                    size_t j = 0;
                    for (const MLine& c : capd) {
                        // (an alternative model 0x004185E0, or a model loaded into a freed slot's memory, is logged by
                        // the capture's pointer cache under another id: a draw with the same matrix counts as an alias)
                        const size_t j0 = j;
                        while (j < my.size() && my[j]->model != c.model && !(my[j]->alt && MaxDiffPrinted(mv(my[j]).m, c.m) <= 2e-3)) ++j;
                        if (j == my.size()) {
                            for (j = j0; j < my.size() && MaxDiffPrinted(mv(my[j]).m, c.m) > 2e-3; ++j) {
                            }
                            if (j < my.size()) ++aliases[{ my[j]->model, c.model }];
                        }
                        if (j == my.size() || MaxDiffPrinted(mv(my[j]).m, c.m) > 2e-3) {
                            ok = false;
                            if (why.empty()) why = "stage draw model " + std::to_string(c.model);
                            break;
                        }
                        ++j;
                    }
                    continue;
                }
                if (my.size() != capd.size()) {
                    ok = false;
                    drawsBad += static_cast<long long>(std::max(my.size(), capd.size()) - std::min(my.size(), capd.size()));
                    if (why.empty())
                        why = "task " + std::to_string(s) + " (" + Hex32(f) + "): " + std::to_string(my.size()) + " draws, capture " + std::to_string(capd.size());
                }
                for (size_t i = 0; i < std::min(my.size(), capd.size()); ++i) {
                    double e = my[i]->model == capd[i].model ? MaxDiffPrinted(mv(my[i]).m, capd[i].m) : 1e9;
                    if (my[i]->model != capd[i].model && MaxDiffPrinted(mv(my[i]).m, capd[i].m) < 2e-3) {  // capture id alias
                        ++aliases[{ my[i]->model, capd[i].model }];
                        e = MaxDiffPrinted(mv(my[i]).m, capd[i].m);
                    }
                    if (e < 2e-3) {
                        ++drawsOk;
                        ++fnDraws[f].first;
                    } else {
                        ++drawsBad;
                        ++fnDraws[f].second;
                        ok = false;
                        // debug: HOTD2_FT_DRAWFN=<hex fn> prints the first 40 (HOTD2_FT_DRAWMAX) bad draws of that task function
                        static const char* dbgFn = getenv("HOTD2_FT_DRAWFN");
                        static const int dbgMax = getenv("HOTD2_FT_DRAWMAX") ? atoi(getenv("HOTD2_FT_DRAWMAX")) : 40;
                        static int dbgShown = 0;
                        if (dbgFn && f == strtoul(dbgFn, nullptr, 16) && dbgShown++ < dbgMax) {
                            const auto nm = mv(my[i]);
                            printf("frame %d task %u draw %zu model %d err %g\n  native ", fr, s, i, my[i]->model, e);
                            for (float v : nm.m) printf(" %.7g", v);
                            printf("\n  capture");
                            for (float v : capd[i].m) printf(" %.7g", v);
                            printf("\n");
                        }
                        if (why.empty())
                            why = "task " + std::to_string(s) + " (" + Hex32(f) + ") draw " + std::to_string(i) + ": model " + std::to_string(my[i]->model) +
                                  " vs " + std::to_string(capd[i].model) + " err " + std::to_string(e);
                    }
                }
            }
            add("draws (M, per task)", mode, fr, ok, why);
            static const bool drawWhy = getenv("HOTD2_FT_DRAWWHY") != nullptr;  // debug: every frame's first bad draw
            if (drawWhy && !ok) printf("draws frame %d: %s\n", fr, why.c_str());
        }
        // ---- sprites
        sawS = sawS || cf.hasS;
        if (sawS) {
            std::vector<game::SpriteDef> mine = h.sprites;
            if (mode == 3 || mode == 4 || mode == 9 || mode == 8 || mode == 12) {
                bool same = mine.size() == cf.s.size();
                std::string why = "native " + std::to_string(mine.size()) + " capture " + std::to_string(cf.s.size());
                for (size_t i = 0; i < std::min(mine.size(), cf.s.size()); ++i)
                    if (!SameSprite(mine[i], cf.s[i].first)) {
                        same = false;
                        why = "sprite " + std::to_string(i) + ": native " + SpriteText(mine[i]) + " capture " + SpriteText(cf.s[i].first);
                        break;
                    }
                add("sprites (S)", mode, fr, same, why);
            } else if (mode == 6) {
                // the select screens / name entry of the game types (world_modes*.cpp, SpriteDef::source != 0) against
                // the capture's sprites drawn from those functions
                auto ours = [](uint32_t c) {
                    return (c >= 0x00425010 && c < 0x00425F40) || (c >= 0x00434920 && c < 0x00434DA0) || (c >= 0x00496BA0 && c < 0x00499000) ||
                           (c >= 0x00488820 && c < 0x0048A100) || (c >= 0x00480D90 && c < 0x00482D00) ||
                           (c >= 0x004995B0 && c < 0x00499700) ||  // the lessons' time left 0x004995B0
                           c == 0x0041B09D || c == 0x00432607 || c == 0x0041C47F;  // (the wrappers 0x0041B020 / 0x00432590 / 0x0041C400)
                };
                std::vector<game::SpriteDef> a, b;
                for (auto& d : mine)
                    if (d.source != 0) a.push_back(d);
                for (auto& d : cf.s)
                    if (ours(d.second)) b.push_back(d.first);
                if (!a.empty() || !b.empty()) {
                    bool same = a.size() == b.size();
                    std::string why = "native " + std::to_string(a.size()) + " capture " + std::to_string(b.size());
                    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
                        if (!SameSprite(a[i], b[i])) {
                            same = false;
                            why = "sprite " + std::to_string(i) + ": native " + SpriteText(a[i]) + " capture " + SpriteText(b[i]);
                            break;
                        }
                    add("select / name sprites (S)", mode, fr, same, why);
                }
                // CONTINUE: the whole-game screen 0x00460530 (callers 0x00460621 / 0x00460658) and the per-player prompt
                // 0x00414280 -> 0x00416880 / 0x004168C0 (retained layer 2, flushed by 0x0041CF30 at 0x0041CF48: ids
                // 0x22C and the digits 0x4F..0x58), from WorldHud::continueShown as play.exe draws them
                std::vector<game::SpriteDef> c, d;
                if (h.continueShown >= 0) {
                    game::HudState cs;
                    cs.continueCount = h.continueShown;
                    for (auto& x : game::ContinueScreen(cs)) c.push_back(x);
                }
                for (int p = 1; p >= 0; --p)  // head insertion: the later player task's sprites first
                    if (h.player[p].continueShown >= 0)
                        for (auto& x : game::ContinuePlayer(p, h.player[p].continueShown)) c.push_back(x);
                for (auto& x : cf.s)
                    if (x.second == 0x00460621 || x.second == 0x00460658 ||
                        (x.second == 0x0041CF48 && (x.first.id == 0x22C || (x.first.id >= 0x4F && x.first.id <= 0x58))))
                        d.push_back(x.first);
                if (!c.empty() || !d.empty()) {
                    bool same = c.size() == d.size();
                    std::string why = "native " + std::to_string(c.size()) + " capture " + std::to_string(d.size());
                    for (size_t i = 0; i < std::min(c.size(), d.size()); ++i)
                        if (!SameSprite(c[i], d[i])) {
                            same = false;
                            why = "sprite " + std::to_string(i) + ": native " + SpriteText(c[i]) + " capture " + SpriteText(d[i]);
                            break;
                        }
                    add("CONTINUE sprites (S)", mode, fr, same, why);
                }
            } else if (mode == 5 || mode == 10 || mode == 11) {
                bool a = false, b = false;
                for (auto& d : mine) a = a || d.id == 0x900;
                for (auto& d : cf.s) b = b || (d.first.id == 0x900 && d.second == 0x00406851);
                add("PRESS START (S 0x900)", mode, fr, a == b, std::string("native ") + (a ? "on" : "off") + " capture " + (b ? "on" : "off"));
            }
        }
        if (fr == dumpFrame || (dumpFrame >= 0 && dumpFrame <= fr && fr <= dumpTo)) {
            printf("frame %d mode %d/%d frames %d boot %u\n native tasks:", fr, mode, h.subState, h.modeFrames, h.bootFrames);
            for (auto& t : tasks) printf(" %u:%X", t.serial, t.fn);
            printf("\n capture tasks:");
            for (auto& k : cf.k) printf(" %u:%X", k.first, k.second);
            printf("\n native rand:");
            for (const auto& r : world.RandCalls()) printf(" %X/%u=%d", r.site, r.serial, r.value);
            printf("\n capture rand:");
            for (const auto& y : cf.y) printf(" %X/%u=%d", y.site, y.serial, y.value);
            printf("\n native sprites:");
            for (auto& d : h.sprites) printf(" [%s]", SpriteText(d).c_str());
            printf("\n capture sprites:");
            for (auto& d : cf.s) printf(" [%s @%X]", SpriteText(d.first).c_str(), d.second);
            printf("\n native draws:");
            for (auto& d : world.Draws()) printf(" %d^%u", d.model, d.task);
            printf("\n capture draws:");
            for (auto& [s, v] : cf.m)
                for (auto& m : v) printf(" %d^%u", m.model, s);
            if (const char* ts = getenv("FE_DUMPTASK")) {  // the model-view matrices of one task, native and captured
                const uint32_t want = static_cast<uint32_t>(atoi(ts));
                game::Mat4 V2 = world.Camera().view;
                for (auto& d : world.Draws())
                    if (d.task == want) {
                        const game::Mat4 m = d.viewSpace ? d.world : d.world * V2;
                        printf("\n  native %d:", d.model);
                        for (float v : m.m) printf(" %.6g", v);
                    }
                auto it = cf.m.find(want);
                if (it != cf.m.end())
                    for (auto& m : it->second) {
                        printf("\n  capture %d:", m.model);
                        for (float v : m.m) printf(" %.6g", v);
                    }
            }
            printf("\n native rand:");
            for (auto& r : world.RandCalls()) printf(" %X/%u=%d", r.site, r.serial, r.value);
            printf("\n capture rand:");
            for (auto& y : cf.y) printf(" %X/%u=%d", y.site, y.serial, y.value);
            printf("\n native enemies:");
            for (auto& e : world.Enemies(true))
                printf(" [%u kind %d hp %d beh %d/%d motion %X tick %d]", e.serial, e.kind, e.hp, e.behaviour, e.sub, e.motion, e.tick);
            printf("\n screens:");
            for (auto& s : h.screens) printf(" [%s %g,%g a %g]", s.file.c_str(), s.x, s.y, s.alpha);
            printf("\n");
            for (auto& n : world.Notes()) printf(" note: %s\n", n.c_str());
        }
        if (verbose) {
            for (auto& [key, c] : checks)
                if (c.firstBad == fr) printf("frame %d first %s difference (mode %d): %s\n", fr, key.first.c_str(), key.second, c.firstWhy.c_str());
        }
        capPrev = capMode;
        capMode = cf.w.count(0x009C8E98) ? Get<int16_t>(cf.w[0x009C8E98], 0) : -1;
        capDemo = cf.w.count(0x009A3400) ? Get<int16_t>(cf.w[0x009A3400], 0) : -1;
        capZ = cf.hasZ;
        capZv = cf.z;
        if (world.QuitRequested()) {
            printf("quit requested at frame %d\n", fr);
            break;
        }
    }

    printf("frames run: 0..%d (%d resyncs, %d frames before the capture's first one not compared)\nnative game modes:", lastFrame, resyncs, uncompared);
    for (auto& [f, m] : switches) printf(" %d@%d", m, f);
    printf("\nframes per mode:");
    for (auto& [m, n] : modeFrames) printf(" %d:%d", m, n);
    printf("\n");
    std::set<int> modes;
    for (auto& [key, c] : checks) modes.insert(key.second);
    for (int m : modes) {
        printf("\n-- game mode %s\n", ModeName(m));
        for (const std::string& name : order) {
            auto it = checks.find({ name, m });
            if (it == checks.end()) continue;
            const Check& c = it->second;
            printf("  %-28s %6d / %6d frames match", name.c_str(), c.ok, c.ok + c.bad);
            if (c.firstBad >= 0) printf("; first divergence %d: %s", c.firstBad, c.firstWhy.c_str());
            printf("\n");
        }
    }
    printf("\ndraw matrices: %lld ok, %lld bad; %d frames with native draws but none in the capture (not rendered)\n", drawsOk, drawsBad, notRendered);
    for (const auto& [f, n] : fnDraws)
        if (n.second) printf("  fn %06X: %lld ok, %lld bad\n", f, n.first, n.second);
    for (auto& [p, n] : aliases)
        printf("  model id alias (capture pointer cache): native %d logged as %d, %d draws\n", p.first, p.second, n);
    if (!notes.empty()) {
        printf("unported paths reached:\n");
        for (auto& n : notes) printf("  %s\n", n.c_str());
    }
    return 0;
}
