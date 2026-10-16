// env_test <game dir> <stage table file> <capture with E, G and L lines> [first frame]
// Validates game::Environment against the ORIGINAL game: the event ops 0x18 / 0x20 that the original executed
// (E lines, HOTD2_SCENECAP_EVENTS=1) are applied in order, and every captured state (HOTD2_SCENECAP_LIGHTS=1)
// is compared with it:
//   G lines: env block fog colour / near / far (raw memory) and the derived FOGCOLOR / FOGSTART / FOGEND;
//   L lines: AMBIENT render state, light diffuse and light ambient (from the start of the capture: before any env op
//   the game::Environment defaults must hold, as in the title).
// Captured D3D states may lag the env block by one frame (they are applied once per frame), so a derived
// mismatch in the frame of an env change or the next one is reported as "lag", not as a failure.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "game/environment.h"

namespace fs = std::filesystem;

static std::vector<uint8_t> ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

static bool Near(double a, double b) { return std::fabs(a - b) <= 1e-4 * std::max(1.0, std::fabs(b)); }

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: env_test <game dir> <stage table> <capture> [first frame]\n");
        return 2;
    }
    const fs::path game = argv[1];
    const unsigned firstFrame = argc > 4 ? static_cast<unsigned>(atoi(argv[4])) : 0;
    game::EventMemory mem;
    mem.Load(game::EventMemory::kCommonBase, ReadAll(game / "evt" / "comevtbl.bin"));
    mem.Load(game::EventMemory::kStageBase, ReadAll(game / "evt" / argv[2]));

    game::Environment env;
    unsigned frame = 0, lastChange = 0;
    bool started = false;
    int applied = 0, rawOk = 0, rawBad = 0, fogOk = 0, fogBad = 0, lightOk = 0, lightBad = 0, lag = 0;
    std::ifstream cap(argv[3]);
    for (std::string line; std::getline(cap, line);) {
        if (line.size() < 2) continue;
        std::istringstream ss(line.substr(2));
        const char kind = line[0];
        if (kind == 'F') {
            ss >> frame;
        } else if (kind == 'E') {
            unsigned f, op;
            std::string addr;
            ss >> f >> op >> addr;
            if (f < firstFrame || (op != 0x18 && op != 0x20)) continue;
            game::EventOp eop;
            if (!game::DecodeEvent(mem, static_cast<uint32_t>(std::stoul(addr, nullptr, 16)), eop)) continue;
            if (env.Apply(mem, eop)) {
                ++applied;
                started = true;
                lastChange = f;
            }
        } else if (kind == 'G' && started) {
            std::string color;
            unsigned table, vmode, range, r, g, b;
            double start, end, density, nearV, farV;
            ss >> color >> table >> vmode >> range >> start >> end >> density >> r >> g >> b >> nearV >> farV;
            const bool raw = r == static_cast<unsigned>(env.fogColor[0]) && g == static_cast<unsigned>(env.fogColor[1]) &&
                             b == static_cast<unsigned>(env.fogColor[2]) && Near(nearV, env.fogNear) && Near(farV, env.fogFar);
            raw ? ++rawOk : ++rawBad;
            if (!raw) printf("frame %u: env block %u %u %u %g %g, expected %d %d %d %g %g\n", frame, r, g, b, nearV, farV,
                             env.fogColor[0], env.fogColor[1], env.fogColor[2], env.fogNear, env.fogFar);
            const unsigned expColor = (env.fogColor[0] << 16) | (env.fogColor[1] << 8) | env.fogColor[2];
            const bool fog = (std::stoul(color, nullptr, 16) & 0xFFFFFF) == expColor && Near(start, env.FogStart()) &&
                             Near(end, env.FogEnd()) && table == 3;
            if (fog) ++fogOk;
            else if (frame <= lastChange + 1) ++lag;
            else {
                ++fogBad;
                printf("frame %u: fog %s %g %g mode %u, expected %06X %g %g\n", frame, color.c_str(), start, end, table, expColor,
                       env.FogStart(), env.FogEnd());
            }
        } else if (kind == 'L') {  // also before the first env op: the defaults (title)
            unsigned lighting, enabled, type;
            std::string ambient;
            float v[16];
            ss >> lighting >> ambient >> enabled >> type;
            for (float& x : v) ss >> x;  // diffuse[4] specular[4] ambient[4] ...
            float d[3], a[3];
            env.LightDiffuse(d);
            env.LightAmbient(a);
            bool ok = (std::stoul(ambient, nullptr, 16) == env.AmbientRenderState());
            for (int k = 0; k < 3; ++k) ok = ok && Near(v[k], d[k]) && Near(v[8 + k], a[k]);
            if (ok) ++lightOk;
            else if (frame <= lastChange + 1) ++lag;
            else {
                ++lightBad;
                if (lightBad <= 5)
                    printf("frame %u: AMBIENT %s diffuse %g %g %g ambient %g %g %g, expected %08X %g %g\n", frame,
                           ambient.c_str(), v[0], v[1], v[2], v[8], v[9], v[10], env.AmbientRenderState(), d[0], a[0]);
            }
        }
    }
    printf("env ops applied: %d; env block states: %d match, %d differ; fog states: %d match, %d differ; "
           "light states: %d match, %d differ; within one frame of a change: %d\n",
           applied, rawOk, rawBad, fogOk, fogBad, lightOk, lightBad, lag);
    return applied > 0 && rawBad == 0 && fogBad == 0 && lightBad == 0 && fogOk + lightOk > 0 ? 0 : 1;
}
