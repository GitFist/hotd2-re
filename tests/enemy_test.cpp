// enemy_test <game dir> <scene_capture_enemies_full.txt>
// Checks game::Enemy* (src/game/enemy.*) against the two stage-1 attract-demo enemies captured from the ORIGINAL
// game (full 0x13F4-byte object dumps "T <serial> <fn> <hdr> <words>", rand state "Z", rand callers "N"):
//   tables    static exe tables used by the port (behaviour table, corpse hold ticks)
//   spawn     kind / behaviour / hp at spawn (Enemy_Create 0x00452DA0, Enemy_InitHp 0x0040A8B0)
//   motion    tick / stored frame / interpolation flag of every non-blending frame (0x004111A0)
//   shots     every hp / node-stage / node-flag change replayed with EnemyApplyShot (0x00409430); reports the
//             damage ranks (0x009C8E96) consistent with all hits
//   death     death motion + 6-frame blend (0x004560B0), knock-back jump velocities (0x0044DB50 / 0x0044DDE0),
//             fall gravity / bounces / landing (0x004550E0)
//   corpse    120-frame countdown, sinking, task end (0x00454F20)
//   rand      corpse twitch ticks and target picks predicted from the captured CRT rand state, assuming the
//             enemy tasks make the last rand() calls of a frame, in spawn order

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

#include "game/anim.h"
#include "game/enemy.h"
#include "game/exe_image.h"

namespace {

struct Obj {
    std::vector<uint32_t> w;
    uint32_t U(int off) const { return w[off / 4]; }
    int32_t S(int off) const { return static_cast<int32_t>(w[off / 4]); }
    int16_t H(int off) const { return static_cast<int16_t>((w[off / 4] >> (8 * (off & 3))) & 0xFFFF); }
    uint8_t B(int off) const { return static_cast<uint8_t>((w[off / 4] >> (8 * (off & 3))) & 0xFF); }
    float F(int off) const {
        float f;
        uint32_t u = w[off / 4];
        memcpy(&f, &u, 4);
        return f;
    }
};

struct Frame {
    std::map<int, Obj> enemies;           // serial -> object
    std::vector<std::pair<uint32_t, int>> rand;  // caller, count
    bool hasZ = false;
    uint32_t z = 0;  // rand state at the end of the frame
};

int fails = 0;
void Check(bool ok, const char* what, int frame, int serial) {
    if (!ok) {
        ++fails;
        if (fails < 40) printf("  FAIL %s frame %d serial %d\n", what, frame, serial);
    }
}

constexpr int kNodeBase = 0x20C, kNodeStride = 0x90;

game::EnemyShotState ShotState(const Obj& o, int slots) {
    game::EnemyShotState s;
    s.kind = o.H(0x1F4);
    s.flags34 = o.U(0x34);
    s.flags136C = o.U(0x136C);
    s.hp = o.H(0x11C);
    s.severMask = o.B(0x1318);
    for (int i = 0; i < slots; ++i) {
        game::EnemyNodeState n;
        n.model = o.S(kNodeBase + i * kNodeStride);
        n.flags = o.U(kNodeBase + i * kNodeStride + 0x74);
        n.stage = o.H(kNodeBase + i * kNodeStride + 0x8C);
        s.nodes.push_back(n);
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: enemy_test <game dir> <scene_capture_enemies_full.txt>\n");
        return 2;
    }
    game::ExeImage exe;
    if (!exe.Load(std::string(argv[1]) + "/Hod2.exe")) return 2;
    game::EnemyTables tab;
    const bool tablesOk = tab.Load(exe);

    // ---- tables
    int tableChecks = 0;
    auto tcheck = [&](bool ok, const char* what) { ++tableChecks; Check(ok, what, 0, 0); };
    tcheck(tablesOk, "behaviour[23] == 0x00457B50");
    tcheck(tab.behaviour[7] == 0x00454F20 && tab.behaviour[9] == 0x004550E0 && tab.behaviour[10] == 0x00455490,
           "behaviour 7/9/10");
    tcheck(tab.hooks[0] == 0x00456920 && tab.hooks[2] == 0x00456B70 && tab.hooks[3] == 0x00456C50, "hooks");
    for (int m = 0x3D9; m <= 0x3E0; ++m)
        for (int k = 0; k < 2; ++k)
            tcheck(game::EnemyCorpseHoldTick(m, k ? 16 : 0) ==
                       static_cast<int>(exe.U32(0x0059301C + (m * 2 - 0x7B2 + k) * 4)),
                   "hold tick table 0x0059301C");
    const std::pair<int, uint32_t> other[] = {
        {0x3F8, 0x0059305C}, {0x1DF, 0x00593064}, {0x3F7, 0x0059307C}, {0x404, 0x00593074}, {0x41A, 0x0059306C}};
    for (const auto& [m, va] : other)
        for (int k = 0; k < 2; ++k)
            tcheck(game::EnemyCorpseHoldTick(m, k ? 16 : 0) == static_cast<int>(exe.U32(va + 4 * k)),
                   "hold tick tables");
    for (int d = 0; d < 5; ++d) tcheck(game::EnemyInitialHp(tab, 1, d, 5) == 1, "hp(1) in demo mode");
    tcheck(game::EnemyInitialHp(tab, 0, 0, 5) == 1 && game::EnemyInitialHp(tab, 500, 2, 0) == 300, "hp clamp");
    printf("tables: %d checks\n", tableChecks);

    // ---- capture
    std::ifstream cap(argv[2]);
    if (!cap) return 2;
    std::map<int, Frame> frames;
    Frame cur;
    for (std::string line; std::getline(cap, line);) {
        if (line.size() < 2 || line[1] != ' ') continue;
        std::istringstream ss(line.substr(2));
        switch (line[0]) {
        case 'T': {
            int serial;
            std::string fn, hdr;
            ss >> serial >> fn >> hdr;
            if (serial != 65605 && serial != 65607) break;
            Obj o;
            for (std::string h; ss >> h;) o.w.push_back(static_cast<uint32_t>(std::stoul(h, nullptr, 16)));
            if (o.w.size() * 4 >= 0x13F4) cur.enemies[serial] = std::move(o);
            break;
        }
        case 'N':
            for (std::string t; ss >> t;) {
                const size_t c = t.find(':');
                if (c != std::string::npos)
                    cur.rand.push_back({static_cast<uint32_t>(std::stoul(t.substr(0, c), nullptr, 16)),
                                        std::stoi(t.substr(c + 1))});
            }
            break;
        case 'Z': {
            std::string z;
            ss >> z;
            cur.hasZ = true;
            cur.z = static_cast<uint32_t>(std::stoul(z, nullptr, 16));
            break;
        }
        case 'F': {
            int f;
            ss >> f;
            frames[f] = std::move(cur);
            cur = Frame();
            break;
        }
        }
    }
    const int serials[2] = {65605, 65607};
    int seen = 0;
    for (auto& [f, fr] : frames) seen += static_cast<int>(fr.enemies.size());
    printf("capture: %zu frames, %d enemy dumps\n", frames.size(), seen);
    if (seen == 0) return 2;

    int spawnChecks = 0, motionChecks = 0, shotEvents = 0, jumpChecks = 0, fallChecks = 0, bounces = 0,
        corpseChecks = 0, twitchChecks = 0, pickChecks = 0, randSkipped = 0, rankEvents = 0;
    std::set<int> ranks;
    for (int r = 0; r < 16; ++r) ranks.insert(r);

    for (int serial : serials) {
        const Obj* prev = nullptr;
        int prevFrame = -1, lastFrame = -1;
        game::EnemyJump jump;
        bool jumping = false;
        float gravity = 0;
        for (auto& [f, fr] : frames) {
            auto it = fr.enemies.find(serial);
            if (it == fr.enemies.end()) continue;
            const Obj& o = it->second;
            lastFrame = f;
            const int kind = o.H(0x1F4);
            const int slots = exe.S16(0x004E0724 + 2 * kind);
            if (!prev) {  // spawn frame: constructor ran, main loop not yet
                ++spawnChecks;
                Check(o.U(0) == 0x004533F0 && kind == 15 && o.H(0x1310) == 23 && o.H(0x1312) == 0, "spawn state", f,
                      serial);
                Check(o.H(0x11C) == 1 && o.H(0x11E) == 1, "spawn hp", f, serial);
                Check(o.S(0x1B4) == 0x3BC && o.U(0x12EC) == 0x004534A0 && o.U(0x12F0) == 0x00454900,
                      "spawn motion/callbacks", f, serial);
                Check(std::fabs(o.F(0x124) - tab.kindRadius[kind]) < 1e-6f && o.F(0x128) == 3.5f, "spawn radii", f,
                      serial);
                prev = &o;
                prevFrame = f;
                continue;
            }
            const int beh = o.H(0x1310), sub = o.H(0x1312), pbeh = prev->H(0x1310), psub = prev->H(0x1312);
            const int motion = o.S(0x1B4);
            const int ticks = game::MotionTicks(exe, motion);

            // ---- motion tick (track 0, not blending)
            if ((o.B(0x1CB) & 1) == 0 && ticks > 0) {
                const int counter = (o.U(0x34) & 0x4000) ? o.S(0x194) : o.S(0x194) - 1;
                const game::MotionTick m = game::EnemyMotionTick(counter, ticks);
                ++motionChecks;
                Check(m.tick == o.S(0x19C) && m.frame == o.S(0x1AC) && m.interpolate == ((o.B(0x1CB) & 0x20) != 0),
                      "motion tick/frame/interp", f, serial);
            }

            // ---- shots: replay hits (nodes whose stage rose, plus per-player hit slots that changed)
            bool hit = false;
            {
                std::vector<int> cand;
                for (int i = 0; i < slots; ++i)
                    if (o.H(kNodeBase + i * kNodeStride + 0x8C) != prev->H(kNodeBase + i * kNodeStride + 0x8C) ||
                        o.U(kNodeBase + i * kNodeStride + 0x74) != prev->U(kNodeBase + i * kNodeStride + 0x74))
                        cand.push_back(i);
                for (int p = 0; p < 2; ++p) {
                    const int n = o.B(0x190 + p);
                    if (n != prev->B(0x190 + p) && std::find(cand.begin(), cand.end(), n) == cand.end())
                        cand.push_back(n);
                }
                if (o.H(0x11C) != prev->H(0x11C) || !cand.empty()) {
                    hit = true;
                    ++shotEvents;
                    std::set<int> ok;
                    for (int rank = 0; rank < 16; ++rank) {
                        game::EnemyShotState s = ShotState(*prev, slots);
                        for (int n : cand) game::EnemyApplyShot(tab, s, n, 5, rank);
                        bool same = s.hp == o.H(0x11C);
                        for (int i = 0; i < slots; ++i)
                            same = same && s.nodes[i].stage == o.H(kNodeBase + i * kNodeStride + 0x8C) &&
                                   (s.nodes[i].flags & 0x80000000u) ==
                                       (o.U(kNodeBase + i * kNodeStride + 0x74) & 0x80000000u) &&
                                   s.nodes[i].model == o.S(kNodeBase + i * kNodeStride);
                        same = same && (s.flags34 & 0x4000E00) == (o.U(0x34) & 0x4000E00);
                        if (same) ok.insert(rank);
                    }
                    Check(!ok.empty(), "shot replay", f, serial);
                    std::set<int> keep;
                    for (int r : ranks)
                        if (ok.count(r)) keep.insert(r);
                    ranks = keep;
                    ++rankEvents;
                }
            }

            // ---- death: behaviour 9 entered this frame
            if (beh == 9 && pbeh != 9) {
                const auto dm = game::EnemyDeathMotion(kind, o.S(0x130C), prev->U(0x34), prev->U(0x1368),
                                                       prev->U(0x136C), 0);
                Check(dm.motion == motion && o.S(0x19C) == dm.startTick && (o.B(0x1CB) & 1), "death motion", f,
                      serial);
                float start[3] = {o.F(0x13C0), o.F(0x13C4), o.F(0x13C8)};
                float target[3] = {o.F(0x13CC), o.F(0x13D0), o.F(0x13D4)};
                jump.Setup(start, target, o.U(0x136C), o.U(0x34));
                Check(jump.duration == o.S(0x1334), "jump duration", f, serial);
                jumping = true;
            }
            if (jumping && beh == 9 && sub == 1) {
                float vel[3];
                const bool more = jump.Step(vel);
                ++jumpChecks;
                Check(more && vel[0] == o.F(0x4C) && vel[1] == o.F(0x50) && vel[2] == o.F(0x54) &&
                          jump.counter == o.S(0x1330),
                      "jump velocity", f, serial);
            }
            if (beh == 9 && sub == 2) {
                if (psub == 1) {  // fell through from the last jump frame
                    float vel[3];
                    Check(!jump.Step(vel), "jump end", f, serial);
                    gravity = game::EnemyFallGravity(o.S(0x130C));
                    Check(gravity == o.F(0x5C), "fall gravity", f, serial);
                    jumping = false;
                }
                // vy = prev vy + g, or a bounce (vy + g) * -0.25 (the floor height itself is not checked here)
                const float fall = prev->F(0x50) + gravity;
                const float bounce = fall * -0.25f;
                ++fallChecks;
                if (o.F(0x50) == bounce && std::fabs(fall) > 0.15f) ++bounces;
                else Check(o.F(0x50) == fall, "fall velocity", f, serial);
                Check(o.S(0x1334) == prev->S(0x1334) + 1 || psub == 1, "fall frame counter", f, serial);
            }
            if (beh == 7 && pbeh == 9) {  // landed this frame
                ++fallChecks;
                const float fall = prev->F(0x50) + gravity;
                Check(std::fabs(fall) <= 0.15f && o.F(0x4C) == 0 && o.F(0x50) == 0 && o.F(0x54) == 0 &&
                          (o.U(0x34) & 0xC000) == 0xC000 && o.S(0x194) == prev->S(0x194) - 1,
                      "landing", f, serial);
            }

            // ---- corpse
            if (beh == 7 && pbeh == 7) {
                ++corpseChecks;
                Check(o.S(0x1330) == (psub == 0 ? game::kEnemyCorpseFrames - 1 : prev->S(0x1330) - 1),
                      "corpse countdown", f, serial);
                Check(std::fabs(o.F(0x44) - (prev->F(0x44) - game::kEnemyCorpseSink)) < 1e-4f, "corpse sink", f,
                      serial);
            }

            // ---- rand: predict this enemy's calls from the frame's rand sequence (enemy calls come last)
            const Frame& fp = frames.at(prevFrame);
            if (fr.hasZ && fp.hasZ && f == prevFrame + 1) {
                int total = 0;
                for (auto& [c, k] : fr.rand) total += k;
                std::vector<int> seq;
                uint32_t st = fp.z;
                for (int i = 0; i < total; ++i) {
                    st = st * 214013u + 2531011u;
                    seq.push_back(static_cast<int>((st >> 16) & 0x7FFF));
                }
                // calls made by each enemy this frame, in update order
                auto enemyCalls = [&](int ser, std::vector<char>& kinds) {
                    const Obj& e = fr.enemies.at(ser);
                    const auto pit = fp.enemies.find(ser);
                    if (pit == fp.enemies.end()) return;
                    const Obj& ep = pit->second;
                    const bool eHit = e.H(0x11C) != ep.H(0x11C);
                    auto count = [&](uint32_t caller) {
                        for (auto& [c, k] : fr.rand)
                            if (c == caller) return k;
                        return 0;
                    };
                    if (eHit) {
                        for (int i = 0; i < count(0x4093E2); ++i) kinds.push_back('o');  // 0x004093C0 player order
                        for (uint32_t c : {0x40A737u, 0x40A778u, 0x40A7B9u})  // 0x0040A6F0 hurt/death voice
                            for (int i = 0; i < count(c); ++i) kinds.push_back('s');
                    }
                    const int b = e.H(0x1310), pb = ep.H(0x1310);
                    if (pb == 23 && ep.H(0x1312) == 0) kinds.push_back('p');            // pick target
                    if (b == 9 && pb != 9) { kinds.push_back('j'); kinds.push_back('j'); }  // jump target
                    if (b == 7 && pb == 7 && game::EnemyCorpseHoldUsesRand(e.S(0x1B4))) kinds.push_back('t');
                };
                // Both enemies are needed to place this one's calls; only predict when the bookkeeping is complete.
                std::vector<char> callsBy[2];
                int nEnemy = 0;
                for (int k = 0; k < 2; ++k)
                    if (fr.enemies.count(serials[k])) enemyCalls(serials[k], callsBy[k]);
                for (int k = 0; k < 2; ++k) nEnemy += static_cast<int>(callsBy[k].size());
                int known = 0;
                for (auto& [c, k] : fr.rand)
                    if (c == 0x454E59 || c == 0x455E52 || c == 0x455218 || c == 0x45524B) known += k;
                int counted = 0;
                for (int k = 0; k < 2; ++k)
                    for (char c : callsBy[k]) counted += (c == 't' || c == 'p' || c == 'j');
                if (counted != known || nEnemy > total) {
                    ++randSkipped;
                } else {
                    int pos = total - nEnemy;
                    for (int k = 0; k < 2; ++k) {
                        for (char c : callsBy[k]) {
                            const int r = seq[pos++];
                            if (serials[k] != serial) continue;
                            if (c == 't') {
                                ++twitchChecks;
                                Check(game::EnemyCorpseHoldTick(motion, r) == o.S(0x194), "corpse twitch", f, serial);
                            } else if (c == 'p') {
                                ++pickChecks;
                                const bool taken[2] = {false, false};
                                Check(game::EnemyPickTargetSingleAttacker(r, taken) == static_cast<int8_t>(o.B(0x121)),
                                      "pick target", f, serial);
                            }
                        }
                    }
                }
            }
            prev = &o;
            prevFrame = f;
        }
        // task ends right after the corpse countdown reaches 1
        ++corpseChecks;
        const auto next = frames.find(lastFrame + 1);
        Check(prev && prev->H(0x1310) == 7 && prev->S(0x1330) == 1 && next != frames.end() &&
                  next->second.enemies.count(serial) == 0,
              "task end", lastFrame, serial);
    }

    printf("spawn: %d, motion ticks: %d, shot events: %d, jump frames: %d, fall frames: %d (bounces %d), corpse: %d\n",
           spawnChecks, motionChecks, shotEvents, jumpChecks, fallChecks, bounces, corpseChecks);
    printf("rand: corpse twitches %d, target picks %d (frames skipped: %d)\n", twitchChecks, pickChecks, randSkipped);
    printf("damage rank (0x009C8E96) consistent with all %d hit events:", rankEvents);
    for (int r : ranks) printf(" %d", r);
    printf("\n");
    if (ranks.empty()) ++fails;
    printf("%s (%d failures)\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
