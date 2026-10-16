// sound_test <game dir> [--dump]
// Sound tables of Snd_PlayGameSound 0x0041CFD0 read from Hod2.exe:
//  - table sizes and that every referenced file exists (only the 36 "_OFF" SE files are absent on PC);
//  - every sound id used by the event scripts (ops 0x38 / 0x39 / 0x3A / 0x5F in evt\*.bin) resolves to an existing file
//    (or is a stop command / an "_OFF" loop-stop id); the 4 sound events of the st1 demo trace are found;
//  - WAV headers parse like Snd_OpenChannel 0x004A3EF0;
//  - SoundDriver (Snd_PlayFile 0x004AC020 channel/ring/loop logic) scenarios on a fake backend.
// --dump prints the full id -> file table. --ids <file>: every id of a list ("<hex id> ..." per line, e.g. the distinct U-line ids
// of the captures) resolved like the port plays it (game modes 5 and 6): files present, categories, ids without an effect.

#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "game/exe_image.h"
#include "game/sound_driver.h"
#include "game/sound_table.h"

namespace fs = std::filesystem;
using namespace game;

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
            ++g_fail;                                                    \
        }                                                                \
    } while (0)

static std::vector<uint8_t> ReadFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

static bool IsOffName(const std::string& s) {
    std::string u;
    for (char c : s) u += static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return u.find("_OFF") != std::string::npos;
}

// Event script scan: opcode word 0x38 (play), 0x3A (play unless 0x009A2D74) or 0x39 (play + repeat) followed by an
// id that the sound tables resolve. (Opcode handlers 0x0045F700 / 0x0045F750 / 0x0045F720.)
struct EvtSound { std::string file; uint32_t offset, op, id; };
static std::vector<EvtSound> ScanEvt(const SoundTable& t, const fs::path& dir) {
    std::vector<EvtSound> out;
    for (const auto& de : fs::directory_iterator(dir)) {
        if (de.path().extension() != ".bin") continue;
        const std::vector<uint8_t> d = ReadFile(de.path());
        const size_t n = d.size() / 4;
        auto w = [&](size_t i) { uint32_t v; memcpy(&v, &d[4 * i], 4); return v; };
        for (size_t i = 0; i + 1 < n; ++i) {
            const uint32_t op = w(i);
            if (op == 0x5F && i + 4 < n && w(i + 2) == 0xFFFFFFFF && (w(i + 3) >> 28) == 1 &&
                ResolveSound(t, w(i + 3)).action == SoundAction::PlayFile) {
                // Snd_EntryBgm 0x0041D450(0, -1, id, 0): stop BGM, then play id.
                out.push_back({de.path().filename().string(), static_cast<uint32_t>(4 * i), op, w(i + 3)});
                continue;
            }
            if (op != 0x38 && op != 0x39 && op != 0x3A) continue;
            const uint32_t id = w(i + 1);
            if (ResolveSound(t, id).action == SoundAction::None) continue;
            if (id == 0x100A0 || id > 0x80000002) continue;  // (scripts only use 0x80000000 of the commands)
            out.push_back({de.path().filename().string(), static_cast<uint32_t>(4 * i), op, id});
        }
    }
    return out;
}

// Fake DirectSound layer with the rules of 0x004A3EF0 / 0x004A4BE0 / 0x004A34B0 / ...
struct FakeBackend : SoundBackend {
    struct Ch { bool buffer = false, streamed = false, playing = false, loop = false; std::string path; };
    Ch ch[kSndChannels];
    std::set<std::string> missing;
    int opens = 0, dups = 0;
    bool Open(int c, const std::string& p, bool s) override {
        if (ch[c].buffer || missing.count(p) || IsOffName(p)) return false;
        ch[c] = {true, s, false, false, p};
        ++opens;
        return true;
    }
    bool Duplicate(int src, int dst) override {
        if (ch[dst].buffer || !ch[src].buffer || ch[src].streamed) return false;
        ch[dst] = ch[src];
        ch[dst].playing = false;
        ++dups;
        return true;
    }
    bool Play(int c, bool loop) override {
        if (!ch[c].buffer) return false;
        ch[c].playing = true;
        ch[c].loop = loop;
        return true;
    }
    void Pause(int c) override { ch[c].playing = false; }
    void Stop(int c) override { ch[c].playing = false; }
    void Release(int c) override { ch[c] = {}; }
    bool IsPlaying(int c) override { return ch[c].playing; }
    void FinishAll() {  // one-shots end
        for (Ch& x : ch)
            if (!x.loop) x.playing = false;
    }
};

static void DriverScenarios(const SoundTable& t) {
    // 1) one-shot SE: auto channel 1 (channel 0 starts out cached: the ring is zero-initialised); replaying the
    //    same id while it plays duplicates the cached buffer into the next free channel.
    {
        FakeBackend b;
        SoundDriver d(b);
        d.PlayGameSound(t, 0x004616A9);
        CHECK(b.ch[1].playing && b.ch[1].path == "Sound\\SE\\COMMON\\SIBUKI8_16.WAV");
        d.PlayGameSound(t, 0x004616A9);
        CHECK(b.dups == 1 && b.opens == 1 && b.ch[2].playing);
        CHECK(d.Ring()[0].channel == 1 && d.Ring()[1].channel == 1 && d.Ring()[1].id == 0x004616A9);
        // after it finished, the cached channel 1 is kept (not released) and reused by duplication again
        b.FinishAll();
        d.PlayGameSound(t, 0x004616A9);
        CHECK(b.dups == 2 && b.opens == 1 && b.ch[1].buffer);
    }
    // 2) looped SE and its "_OFF" id: RAIN3ST_44 loops; RAIN3ST_44_OFF stops every looped SE (file is absent).
    {
        FakeBackend b;
        SoundDriver d(b);
        d.PlayGameSound(t, 0x002118A9);
        CHECK(b.ch[1].playing && b.ch[1].loop && d.LoopCount() == 1);
        d.PlayGameSound(t, 0x000718A9);  // CAR_FIRE loops too
        CHECK(b.ch[2].playing && b.ch[2].loop && d.LoopCount() == 2);
        d.PlayGameSound(t, 0x002218A9);
        CHECK(!b.ch[1].playing && !b.ch[2].playing && d.LoopCount() == 0);
    }
    // 3) channel exhaustion: 15 different one-shots that never finish fit (channel 0 frees up once the six
    //    zero entries left the ring), the 16th is dropped.
    {
        FakeBackend b;
        SoundDriver d(b);
        int ok = 0;
        for (int i = 0; i < 16; ++i) ok += d.PlayFile("Sound\\SE\\X" + std::to_string(i) + ".WAV", false, -1, 0x1000 + i) ? 1 : 0;
        CHECK(ok == 15);
    }
    // 4) BGM: channel 15, streamed, looped; table B in game state 6 when 0x009CA08C == 0; stop clears current.
    {
        FakeBackend b;
        SoundDriver d(b);
        d.PlayGameSound(t, 0x10000001);
        CHECK(b.ch[15].playing && b.ch[15].streamed && b.ch[15].loop && b.ch[15].path == "Sound\\BGM\\ST1_AR.WAV");
        CHECK(d.CurrentBgm() == 0x10000001);
        d.PlayGameSound(t, 0x10000001, SoundContext{6, 0});
        CHECK(b.ch[15].path == "Sound\\BGM\\ST1.WAV");
        d.PlayGameSound(t, 0x10000009);  // OVR_AR plays once
        CHECK(b.ch[15].playing && !b.ch[15].loop);
        d.OnActivate(false);
        CHECK(!b.ch[15].playing && b.ch[15].buffer);
        d.OnActivate(true);  // resume always loops (0x3A0 -> Snd_PlayChannel(15, 1))
        CHECK(b.ch[15].playing && b.ch[15].loop);
        d.PlayGameSound(t, 0x80000000);
        CHECK(!b.ch[15].buffer && d.CurrentBgm() == 0);
    }
    // 5) voice on channel 16, coin on 17, stop-all-SE releases the cached ring channels too.
    {
        FakeBackend b;
        SoundDriver d(b);
        d.PlayGameSound(t, 0x2000009D);
        CHECK(b.ch[16].playing && b.ch[16].path == "Sound\\Voice\\ST2\\67_YNG_M.WAV");
        d.PlayGameSound(t, 0x000021A9);
        CHECK(b.ch[17].playing && b.ch[17].path == "Sound\\SE\\START_COIN\\COIN1_16.wav");
        d.PlayGameSound(t, 0x002116A9);
        b.FinishAll();
        CHECK(b.ch[1].buffer);
        d.PlayGameSound(t, 0x80000001);
        CHECK(!b.ch[1].buffer);
        d.PlayGameSound(t, 0x80000002);
        CHECK(!b.ch[16].buffer);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: sound_test <game dir> [--dump]\n");
        return 2;
    }
    const fs::path game = argv[1];
    const bool dump = argc > 2 && std::string(argv[2]) == "--dump";
    std::string idList;
    for (int i = 2; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--ids") idList = argv[i + 1];
    ExeImage exe;
    if (!exe.Load((game / "Hod2.exe").string())) {
        fprintf(stderr, "cannot load Hod2.exe\n");
        return 2;
    }
    SoundTable t;
    CHECK(t.Load(exe));
    printf("SE %zu, voice %zu, BGM %zu + %zu, looped SE %zu\n", t.Se().size(), t.Voices().size(), t.BgmA().size(),
           t.BgmB().size(), t.LoopIds().size());
    CHECK(t.Se().size() == 324 && t.Voices().size() == 467 && t.LoopIds().size() == 44);
    if (!idList.empty()) {  // the ids the original issued (captures' U lines) through ResolveSound
        std::ifstream in(idList);
        std::map<std::string, int> cats;
        int n = 0;
        for (std::string line; std::getline(in, line);) {
            if (line.empty()) continue;
            const uint32_t id = static_cast<uint32_t>(std::stoul(line.substr(0, line.find(' ')), nullptr, 16));
            ++n;
            for (int mode : { 6, 5 }) {
                SoundContext ctx;
                ctx.gameState = mode;
                const SoundRequest r = ResolveSound(t, id, ctx);
                std::string cat;
                switch (r.action) {
                case SoundAction::PlayFile:
                    if (fs::exists(game / r.path)) cat = r.channel == kSndBgmChannel ? (r.loop ? "BGM looped" : "BGM once") : r.channel == kSndVoiceChannel ? "voice" : r.loop ? "SE looped" : "SE";
                    else if (IsOffName(r.path)) cat = "SE _OFF (stops the looped SEs; no file on PC)";
                    else {
                        cat = "MISSING FILE";
                        printf("MISSING %08X %s\n", id, r.path.c_str());
                        ++g_fail;
                    }
                    break;
                case SoundAction::StopBgm: cat = "stop BGM"; break;
                case SoundAction::StopAllSe: cat = "stop all SE"; break;
                case SoundAction::StopVoice: cat = "stop voice"; break;
                case SoundAction::AllOff: cat = "all off (no-op on PC)"; break;
                default:
                    cat = "no effect";
                    printf("no effect (mode %d): %08X\n", mode, id);
                    break;
                }
                ++cats[std::string(mode == 6 ? "mode 6: " : "mode 5: ") + cat];
            }
        }
        printf("%d ids from %s:\n", n, idList.c_str());
        for (const auto& [c, k] : cats) printf("  %-60s %d\n", c.c_str(), k);
    }

    // Every table entry: file present (except "_OFF" SEs), WAV header parses.
    int files = 0, missing = 0, missingOff = 0, badWav = 0;
    auto checkFile = [&](const std::string& rel, uint32_t id) {
        const fs::path p = game / rel;
        if (!fs::exists(p)) {
            if (IsOffName(rel)) ++missingOff;
            else {
                ++missing;
                printf("MISSING %08X %s\n", id, rel.c_str());
            }
            return;
        }
        ++files;
        const std::vector<uint8_t> d = ReadFile(p);
        WavInfo w;
        if (!ParseWavHeader(d.data(), d.size(), w) || w.formatTag != 1 || w.bitsPerSample != 16 ||
            w.dataOffset + w.dataSize > d.size()) {
            ++badWav;
            printf("BAD WAV %s\n", rel.c_str());
        }
    };
    std::map<uint32_t, std::string> all;
    for (const SeEntry& e : t.Se()) all[e.id] = ResolveSound(t, e.id).path;
    for (size_t i = 0; i < t.BgmA().size(); ++i)
        if (!t.BgmA()[i].empty()) all[0x10000000 | static_cast<uint32_t>(i)] = ResolveSound(t, 0x10000000 | static_cast<uint32_t>(i)).path;
    for (size_t i = 0; i < t.Voices().size(); ++i)
        if (t.Voices()[i].number != -1) all[0x20000000 | static_cast<uint32_t>(i)] = ResolveSound(t, 0x20000000 | static_cast<uint32_t>(i)).path;
    for (const auto& [id, rel] : all) checkFile(rel, id);
    for (size_t i = 0; i < t.BgmB().size(); ++i)
        if (!t.BgmB()[i].empty()) checkFile("Sound\\BGM\\" + t.BgmB()[i], 0x10000000 | static_cast<uint32_t>(i));
    printf("table files present: %d, missing: %d (+%d \"_OFF\" SEs, absent on PC), bad WAV headers: %d\n", files,
           missing, missingOff, badWav);
    CHECK(missing == 0 && missingOff == 36 && badWav == 0);
    for (size_t i = 0; i < t.LoopOffIds().size(); ++i) CHECK(IsOffName(t.FindSe(t.LoopOffIds()[i])->file));

    // Event scripts.
    const std::vector<EvtSound> ev = ScanEvt(t, game / "evt");
    std::map<std::string, int> perFile;
    std::set<uint32_t> ids;
    int evMissing = 0;
    for (const EvtSound& e : ev) {
        ++perFile[e.file];
        ids.insert(e.id);
        const SoundRequest r = ResolveSound(t, e.id);
        if (r.action == SoundAction::PlayFile && !fs::exists(game / r.path) && !r.stopLoopedSe) {
            ++evMissing;
            printf("EVT MISSING %s+0x%X %08X %s\n", e.file.c_str(), e.offset, e.id, r.path.c_str());
        }
    }
    for (const auto& [f, c] : perFile) printf("  %s: %d sound ops\n", f.c_str(), c);
    printf("event-script sound ops: %zu, distinct ids: %zu, unresolved files: %d\n", ev.size(), ids.size(), evMissing);
    CHECK(evMissing == 0 && ev.size() >= 320);
    // Runtime trace (build\scene_capture_st1demo.txt "E <f> 56 <addr> <id>", stage table at 0x00977400).
    const std::pair<uint32_t, uint32_t> traced[] = {{0x0097A42C, 0x004616A9}, {0x0097A444, 0x004616A9},
                                                    {0x0097A480, 0x004616A9}, {0x0097A52C, 0x002116A9}};
    for (const auto& [addr, id] : traced) {
        bool found = false;
        for (const EvtSound& e : ev)
            found |= e.file == "st1evtbl.bin" && e.offset == addr - 0x00977400 && e.id == id && e.op == 0x38;
        CHECK(found);
    }

    DriverScenarios(t);

    if (dump) {
        for (const auto& [id, rel] : all) {
            const SoundRequest r = ResolveSound(t, id);
            printf("%08X ch=%d loop=%d off=%d %s\n", id, r.channel, r.loop, r.stopLoopedSe, rel.c_str());
        }
        for (size_t i = 0; i < t.BgmB().size(); ++i)
            if (!t.BgmB()[i].empty()) printf("%08X (state 6, 0x009CA08C == 0) Sound\\BGM\\%s\n", 0x10000000 | static_cast<unsigned>(i), t.BgmB()[i].c_str());
        for (uint32_t id : ids) printf("evt id %08X -> %s\n", id, ResolveSound(t, id).path.c_str());
    }
    printf("%s\n", g_fail ? "FAILED" : "OK");
    return g_fail ? 1 : 0;
}
