// anim_test <game dir> <pose dump>
// Checks game::LoadSkeleton / SplitMotionFile / EvaluatePose against tools/skel_pose.py --dump (float tolerance:
// the reference computes in double precision). Also reports motion size consistency for every mot file.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "game/anim.h"
#include "game/exe_image.h"

namespace fs = std::filesystem;

static std::vector<uint8_t> ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: anim_test <game dir> <pose dump>\n");
        return 2;
    }
    const fs::path game = argv[1];
    game::ExeImage exe;
    if (!exe.Load((game / "Hod2.exe").string())) return 2;

    int skeletons = 0, skeletonFail = 0;
    std::vector<game::Skeleton> skel(64);
    for (int t = 0; t < 64; ++t) {
        if (game::LoadSkeleton(exe, t, skel[t])) ++skeletons;
        else ++skeletonFail;
    }

    // All motions, raw (node count resolved per skeleton below).
    struct Raw { int id, ticks; std::vector<uint8_t> data; };
    std::map<int, Raw> motions;
    int files = 0;
    for (const auto& f : game::ListMotionFiles(exe)) {
        if (f.motionIds.empty() || !fs::exists(game / "mot" / f.name)) continue;
        ++files;
        const auto data = ReadAll(game / "mot" / f.name);
        for (auto& m : game::SplitMotionFile(exe, f, data, 0)) motions[m.id] = { m.id, m.ticks, m.data };
    }
    int consistent = 0;
    for (auto& [id, r] : motions) {
        for (int n = 1; n < 200; ++n) {
            game::Motion m{ id, r.ticks, n, r.data };
            if (r.ticks > 0 && m.SizeConsistent()) { ++consistent; break; }
        }
    }

    std::ifstream dump(argv[2]);
    int checked = 0, bad = 0;
    float worst = 0;
    std::map<std::tuple<int, int, int>, std::vector<game::Mat4>> cache;
    for (std::string line; std::getline(dump, line);) {
        std::istringstream ss(line);
        int t, mid, tick, node;
        ss >> t >> mid >> tick >> node;
        float ref[16];
        for (float& v : ref) ss >> v;
        auto key = std::make_tuple(t, mid, tick);
        auto it = cache.find(key);
        if (it == cache.end()) {
            game::Motion m{ mid, motions[mid].ticks, skel[t].nodeCount, motions[mid].data };
            it = cache.emplace(key, game::EvaluatePose(skel[t], m, tick)).first;
        }
        ++checked;
        if (node >= static_cast<int>(it->second.size())) { ++bad; continue; }
        float err = 0;
        for (int i = 0; i < 16; ++i)
            err = std::max(err, std::fabs(it->second[node].m[i] - ref[i]) / std::max(1.0f, std::fabs(ref[i])));
        worst = std::max(worst, err);
        if (err > 1e-3f) {
            if (++bad <= 5) printf("MISMATCH skeleton %d motion %d tick %d node %d: rel err %g\n", t, mid, tick, node, err);
        }
    }
    printf("skeletons loaded: %d/64 (failed %d)  mot files: %d  motions: %zu (size-consistent: %d)\n", skeletons,
           skeletonFail, files, motions.size(), consistent);
    printf("pose matrices checked: %d  mismatched: %d  worst relative error: %g\n", checked, bad, worst);
    return bad == 0 && checked > 0 ? 0 : 1;
}
