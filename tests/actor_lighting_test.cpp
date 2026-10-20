// actor_lighting_test <game> <input script> <first frame> <last frame> <task fn in hex>
// Replay the real actor and check the lighting captured by each model submission.
// Original reference: the reflected skeleton in 0x0043C440 is bracketed by
// 0x0041DC70/0x0041DCC0; schooling fish 0x0042FCA0 calls them at 0x00430135/0x00430A66.
// Owl mode 0x004460C0 also checks death task 0x00448210: its scene-light
// continuity is a deliberate correction of the original PC brightness pop.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "game/world.h"

static void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

int main(int argc, char** argv) {
    if (argc != 6) return 2;
    const int first = std::stoi(argv[3]), last = std::stoi(argv[4]);
    const uint32_t task = static_cast<uint32_t>(std::stoul(argv[5], nullptr, 16));
    Require(first > 0 && last >= first, "valid frame window");
    const bool owl = task == 0x004460C0;
    Require(task == 0x0043C440 || task == 0x0042FCA0 || owl, "supported actor lighting reference");
    struct Input { int frame; float x, y; uint8_t buttons; };
    std::vector<Input> inputs;
    std::ifstream file(argv[2]); std::string line;
    while (std::getline(file, line)) {
        int frame, player; float x, y; unsigned buttons;
        std::istringstream row(line);
        if (row >> frame >> player >> x >> y >> buttons && player == 0)
            inputs.push_back({frame, x, y, static_cast<uint8_t>(buttons)});
    }
    Require(!inputs.empty(), "input script loaded");
    game::World world;
    std::string error;
    Require(world.Load(argv[1], 0, &error), error.c_str());
    game::WorldConfig cfg;
    cfg.freePlay = true;
    cfg.gameOver = true;
    world.Boot(cfg);
    size_t pos = 0;
    float x = .5f, y = .5f;
    uint8_t buttons = 0;
    int draws = 0, mismatches = 0, firstBad = -1, shadowDraws = 0, reflectionPairs = 0, fishShadows = 0;
    int livingOwls = 0, deadOwls = 0;
    for (int frame = 1; frame <= last; ++frame) {
        const uint8_t previous = buttons;
        while (pos < inputs.size() && inputs[pos].frame <= frame) {
            const auto input = inputs[pos++];
            x = input.x; y = input.y; buttons = input.buttons;
        }
        world.SetInput(0, game::MakeWorldInput(x, y, buttons, previous));
        world.SetInput(1, {});
        world.Step();
        Require(world.Frame() == frame, "replay advances one game frame");
        if (frame < first) continue;
        const auto& env = world.Env();
        std::map<std::pair<uint32_t, int>, const game::WorldDraw*> bodies;
        for (const auto& draw : world.Draws()) {
            if (owl) {
                if (draw.fn != task && draw.fn != 0x00448210) continue;
                // Check physical bodies only; blood, debris and muzzle effects
                // have their own authored materials and remain outside this fix.
                if (draw.model != 0xBBF && draw.model != 0xBC0) continue;
                if (draw.model == 0xBBF) ++livingOwls;
                else ++deadOwls;
            } else if (draw.fn != task) continue;
            // A separate floor shadow follows the leech's body and reflection in scene lighting.
            const bool shadow = task == 0x0043C440 && draw.model == 0x10D0;
            if (task == 0x0043C440)
                Require(draw.unitSpecular == !shadow,
                        "leech physical bodies/reflections opt into unit specular; floor shadow does not");
            else if (!owl)
                Require(!draw.unitSpecular,"schooling fish and their shadows retain existing specular state");
            const auto& light = draw.light;
            const bool scene = shadow || owl;
            const float intensity = scene ? env.lightIntensity : env.block1.lightIntensity;
            const float* color = scene ? env.lightColor : env.block1.lightColor;
            bool matches = std::abs(light.intensity - intensity) < 1e-6f;
            for (int c = 0; c < 3; ++c) matches &= std::abs(light.color[c] - color[c]) < 1e-6f;
            if (owl) matches &= light.pitch == env.lightPitch && light.yaw == env.lightYaw;
            else if (!shadow) matches &= light.pitch == env.block1.lightPitch && light.yaw == env.block1.lightYaw;
            if (shadow) ++shadowDraws;
            else ++draws;
            if (task == 0x0042FCA0 && draw.model == 0x85B) ++fishShadows;
            if (task == 0x0043C440 && !shadow && world.Events().scene == 3 && draw.node >= 1) {
                const auto key = std::make_pair(draw.task, draw.node);
                const auto firstBody = bodies.find(key);
                if (firstBody == bodies.end()) bodies.emplace(key, &draw);
                else {
                    Require(firstBody->second->edit == draw.edit, "reflection reuses original body material edits");
                    bool differentMatrix = false;
                    for (int i = 0; i < 16; ++i)
                        differentMatrix |= firstBody->second->world.m[i] != draw.world.m[i];
                    Require(differentMatrix, "reflection has a distinct mirrored transform");
                    ++reflectionPairs;
                }
            }
            if (!matches) {
                if (firstBad < 0) {
                    firstBad = frame;
                    std::fprintf(stderr, "first mismatch frame %d task %08X model %X intensity %.6f vs %.6f\n",
                                 frame, task, draw.model, light.intensity, intensity);
                }
                ++mismatches;
            }
        }
    }
    std::printf("actor %08X frames %d..%d: %d body/reflection draws, %d floor shadows, %d mismatches\n",
                task, first, last, draws, shadowDraws, mismatches);
    Require(draws > 0, "requested real actor was exercised");
    Require(task != 0x0043C440 || (reflectionPairs > 0 && shadowDraws > 0), "reflection and floor shadow paths exercised");
    Require(task != 0x0042FCA0 || fishShadows > 0, "schooling fish shadow path exercised");
    Require(!owl || (livingOwls > 0 && deadOwls > 0), "both living and shot owl bodies exercised");
    Require(mismatches == 0, "actor light snapshots match their intended character or scene light");
}
