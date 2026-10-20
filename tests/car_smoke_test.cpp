// car_smoke_test <game directory>
// CPU-only coverage of every shipped tall car-smoke record and the fire-only
// actors sharing its effect bank. Execute the real spawn/task paths; inspect
// immutable draw snapshots rather than assuming a model ID is always smoke.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "game/world_internal.h"

using namespace game;
namespace fs = std::filesystem;
static void Require(bool value, const char* why) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static std::vector<uint8_t> Read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
enum class Family { ZealCar, LaterCar, PathFire, StoryFire };
struct Record { int mode, scene, variant; uint32_t op; SpawnRecord spawn; Family family; };
struct Data {
    ExeImage exe;
    std::map<int, EventMemory> memory;
    std::vector<Record> records;
    int scripts = 0, instructions = 0;
};
static Data Inventory(const std::string& dir) {
    Data data;
    Require(data.exe.Load((fs::path(dir) / "Hod2.exe").string()), "original executable tables load");
    const auto common = Read(fs::path(dir) / "evt/comevtbl.bin");
    Require(!common.empty(), "common event memory loads");
    for (int mode : {0, 1, 2, 3, 4, 5, 6, 9, 10, 11}) {
        const int index = data.exe.U32(0x00579928 + 4 * mode);
        const auto filename = data.exe.CStr(data.exe.U32(0x004D1C7C + 4 * index));
        const auto bytes = Read(fs::path(dir) / "evt" / filename);
        Require(!bytes.empty(), "every shipped event table loads");
        auto& mem = data.memory[mode];
        mem.Load(EventMemory::kCommonBase, common);
        mem.Load(EventMemory::kStageBase, bytes);
        // -1 scene-header holes precede valid boss/training selectors. The
        // first pointer bounds the complete header, including those selectors.
        const uint32_t base = EventMemory::kStageBase;
        uint32_t first = base + 0x1000;
        for (int i = 0; i < 256; ++i) {
            uint32_t p = 0;
            if (mem.Read(base + 4 * i, &p) && p > base && p < first) first = p;
        }
        const int scenes = (first - base) / 4;
        Require(scenes > 0 && scenes < 256, "declared scene header is bounded");
        for (int scene = 0; scene < scenes; ++scene) {
            uint32_t variants = 0;
            Require(mem.Read(base + 4 * scene, &variants), "declared scene list readable");
            if (!variants || variants == 0xFFFFFFFFu) continue;
            for (int variant = 0; variant < 128; ++variant) {
                uint32_t pc = 0;
                if (!mem.Read(variants + 4 * variant, &pc) || pc == 0xFFFFFFFFu) break;
                if (!pc) continue;
                ++data.scripts;
                bool ended = false;
                for (int i = 0; i < 10000; ++i) {
                    EventOp event;
                    Require(DecodeEvent(mem, pc, event), "all declared scripts decode");
                    ++data.instructions;
                    uint32_t op = 0;
                    if (event.opcode >= 1 && event.opcode <= 8) op = 9 + ((event.opcode - 1) & 3);
                    else if (event.opcode >= 9 && event.opcode <= 12) op = event.opcode;
                    else if (event.opcode == 13 || event.opcode == 0x4B) op = 11;
                    if (op) for (uint32_t address : event.args) {
                        if (address >= 0xFFFFFFFEu) continue;
                        SpawnRecord r;
                        if (!r.Read(mem, address)) continue;
                        Family family;
                        if (r.type == 0x28) family = Family::ZealCar;
                        else if (r.type == 0x41 && r.ParamS8(1) == 53) family = Family::LaterCar;
                        else if (r.type == 0x27) family = Family::PathFire;
                        else if (mode == 10 && r.type == 0x26 && r.state == 0) family = Family::StoryFire;
                        else continue;
                        data.records.push_back({mode, scene, variant, op, r, family});
                    }
                    if (event.opcode == 0x4E || event.opcode == 0x4F) { ended = true; break; }
                    pc += event.length;
                }
                Require(ended, "every script reaches its halt or next-variant boundary");
            }
        }
    }
    EventOp halt;
    Require(DecodeEvent(data.memory.at(0), data.memory.at(0).Script(7, 0, 0), halt)
            && halt.opcode == 0x4E, "common stage-end context has no effect spawns");
    ++data.scripts; ++data.instructions;
    Require(data.scripts == 588 && data.instructions == 17168, "complete shipped script inventory");
    Require(data.records.size() == 15, "all fifteen car-smoke or shared-fire script references found");
    std::map<Family, int> count;
    for (const auto& r : data.records) ++count[r.family];
    Require(count[Family::ZealCar] == 8 && count[Family::LaterCar] == 4
            && count[Family::PathFire] == 2 && count[Family::StoryFire] == 1,
            "stage1/story, stage5, path-fire and parked-story-car occurrences retained");
    return data;
}
static void Setup(World::Impl& w, const Data& data, const std::string& dir, const Record& record) {
    w.gameDir = dir; w.exe = data.exe; w.mem = data.memory.at(record.mode);
    Require(w.cameraBank.Load(w.exe, [&](const std::string& name) { return Read(fs::path(dir) / "cam" / name); }),
            "native camera paths load");
    w.cfg.eventMode = record.mode; w.cfg.scene = record.scene;
    w.cfg.gameMode = w.gameMode = record.mode == 10 ? 10 : 6;
    w.cfg.freePlay = true; w.players = 1; w.playersInGame = 0;
    w.runner = std::make_unique<EventRunner>(w.mem, w, &w.env, &w.exe);
    w.runner->scene = record.scene; w.runner->mode = record.mode; w.runner->variant = record.variant;
    w.runner->cfg.gameMode = w.cfg.gameMode; w.runner->cfg.players = 1;
    w.runner->camera.modeCur = 2; w.runner->camera.camId = 0;
    w.runner->camera.tick = record.family == Family::PathFire ? 200 : 0;
    w.cam.pos[0] = -904.609375f; w.cam.pos[1] = 6.8f; w.cam.pos[2] = -497.591187f;
    w.residency = true; w.modelLoaded.assign(0x4000, 8);
    for (uint32_t& fn : w.actorType) fn = 0x0041EBB0;
    for (uint32_t address = 0x00593358;; address += 8) {
        const int type = static_cast<int32_t>(w.exe.U32(address));
        if (type < 0 || type >= 0x70) break;
        w.actorType[type] = w.exe.U32(address + 4);
    }
    w.LightScene(); w.rng.Seed(12345);
}
struct Totals { int fixtures = 0, smoke = 0, fire = 0, other = 0, transitions = 0; };
static void Run(const Data& data, const std::string& dir, const Record& r, Totals& total) {
    World::Impl w; Setup(w, data, dir, r); SetCurrentWorld(&w);
    w.Spawn(r.op, r.spawn.addr);
    // Komono53 replaces its spawn task with a child. Warm both scheduler paths
    // before measuring phases, retaining the actual task instead of a mock draw.
    for (int i = 0; i < 2; ++i) { w.ts.RunFrame(); w.ts.FreeDead(); w.draws.clear(); }
    std::set<int> smokePhases, firePhases;
    const std::vector<int> scenes = r.family == Family::LaterCar ? std::vector<int>{2, 3, 4, 5, 6}
                                                                : std::vector<int>{r.scene};
    for (int scene : scenes) {
        w.runner->scene = scene;
        const bool smokeExpected = r.family == Family::ZealCar || (r.family == Family::LaterCar && (scene == 4 || scene == 5));
        const bool fireExpected = smokeExpected || r.family == Family::PathFire || r.family == Family::StoryFire;
        for (int phase = 0; phase < 30; ++phase) {
            w.frame = phase; w.seedCounter = phase; w.frameCounter2BAC = phase;
            w.draws.clear(); w.ts.RunFrame(); w.ts.FreeDead();
            int smoke = 0, fire = 0;
            for (const auto& draw : w.draws) {
                const bool isSmoke = draw.model >= 0x135F && draw.model <= 0x136D;
                const bool isFire = draw.model >= 0xB67 && draw.model <= 0xB6E;
                Require(draw.sortBackToFront == isSmoke,
                        "only tall smoke submissions receive the dedicated blend-order hint");
                if (isSmoke) {
                    ++smoke; ++total.smoke; smokePhases.insert(draw.model);
                    Require(draw.model == 0x135F + phase % 15, "native fifteen-phase smoke cycle retained");
                } else if (isFire) { ++fire; ++total.fire; firePhases.insert(draw.model); }
                else ++total.other;
            }
            Require(smoke == (smokeExpected ? 1 : 0), "native smoke visibility and stage5 scene boundaries retained");
            Require(fire == (fireExpected ? 1 : 0), "fire-only paths and smoke-adjacent fire remain present");
        }
    }
    if (r.family == Family::ZealCar || r.family == Family::LaterCar)
        Require(smokePhases.size() == 15, "every native smoke phase exercised in each script context");
    Require(firePhases.size() == 8, "every shared fire phase exercised in each script context");
    if (r.family == Family::ZealCar) {
        if (r.mode == 10) w.runner->camera.camId = 8;
        else {
            w.runner->camera.camId = 0x2F;
            w.runner->camera.tick = w.exe.S16(0x00589AE0 + 4 * r.spawn.state + 2);
        }
        w.draws.clear(); w.ts.RunFrame(); w.ts.FreeDead();
        for (const auto& draw : w.draws)
            Require(!draw.sortBackToFront && !(draw.model >= 0x135F && draw.model <= 0x136D)
                    && !(draw.model >= 0xB67 && draw.model <= 0xB6E),
                    "following/story-exit transition removes both effects and their sort hint");
        if (r.mode == 10) Require(w.draws.empty(), "story camera8 kills the actual car task");
        ++total.transitions;
    }
    SetCurrentWorld(nullptr); ++total.fixtures;
    std::printf("mode%d scene%d variant%d record%08X family%d passed\n",
                r.mode, r.scene, r.variant, r.spawn.addr, static_cast<int>(r.family));
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto data = Inventory(argv[1]);
    Totals total;
    for (const auto& record : data.records) Run(data, argv[1], record, total);
    std::printf("PASS %d contexts/%d instructions, %d real actor fixtures, %d smoke/%d fire/%d other draws, %d exit transitions\n",
                data.scripts, data.instructions, total.fixtures, total.smoke, total.fire, total.other, total.transitions);
}
