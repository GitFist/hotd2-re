// cam_test <game dir> <scene_capture.txt>
// Validates game::CameraFile / LookAngles / ViewMatrix against data captured from the ORIGINAL game
// (src/hook/scene_capture.cpp):
//   1. every captured Cam_Evaluate result (C lines) vs CameraFile::Evaluate at the same camera id and time;
//   2. static level chunks (models of st*_0x level files) must come out at identity world placement:
//      captured model-view matrix M (M lines) * inverse(ViewMatrix(captured camera)) == I.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "game/camera.h"
#include "game/exe_image.h"

namespace fs = std::filesystem;

static std::vector<uint8_t> ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

static bool Invert(const game::Mat4& in, game::Mat4& out) {
    double a[4][8];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 8; ++c) a[r][c] = c < 4 ? in.m[r * 4 + c] : (c - 4 == r);
    for (int c = 0; c < 4; ++c) {
        int p = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
        if (std::fabs(a[p][c]) < 1e-12) return false;
        for (int k = 0; k < 8; ++k) std::swap(a[c][k], a[p][k]);
        const double d = a[c][c];
        for (int k = 0; k < 8; ++k) a[c][k] /= d;
        for (int r = 0; r < 4; ++r)
            if (r != c) {
                const double f = a[r][c];
                for (int k = 0; k < 8; ++k) a[r][k] -= f * a[c][k];
            }
    }
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out.m[r * 4 + c] = static_cast<float>(a[r][4 + c]);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: cam_test <game dir> <scene_capture.txt>\n");
        return 2;
    }
    const fs::path game = argv[1];
    game::ExeImage exe;
    if (!exe.Load((game / "Hod2.exe").string())) return 2;

    std::map<int, std::pair<game::CameraFile*, int>> cams;
    std::vector<std::unique_ptr<game::CameraFile>> files;
    for (const auto& f : game::ListCameraFiles(exe)) {
        if (f.cameraIds.empty() || !fs::exists(game / "cam" / f.name)) continue;
        auto cf = std::make_unique<game::CameraFile>();
        cf->Load(ReadAll(game / "cam" / f.name), static_cast<int>(f.cameraIds.size()));
        for (size_t k = 0; k < f.cameraIds.size(); ++k) cams[f.cameraIds[k]] = { cf.get(), static_cast<int>(k) };
        files.push_back(std::move(cf));
    }
    // Static level chunk files: st<digit>_<digits>[letter].bin (not the st<digit>_1 sky files).
    auto isChunkFile = [&](int modelId) {
        uint16_t file = 0;
        exe.Read(0x004E83B4 + modelId * 2, &file, 2);
        const std::string n = exe.CStr(exe.U32(0x004D0EF4 + file * 4));
        return n.size() > 5 && n.compare(0, 2, "st") == 0 && n[3] == '_' && n[4] == '0';
    };

    std::ifstream cap(argv[2]);
    int evals = 0, evalBad = 0, chunkDraws = 0, chunkIdentity = 0, chunkFrames = 0;
    float evalWorst = 0;
    bool haveCam = false;
    game::Mat4 viewInv{};
    for (std::string line; std::getline(cap, line);) {
        std::istringstream ss(line);
        char kind;
        ss >> kind;
        if (kind == 'C') {
            int id, roll, flag;
            float t, pos[3], tgt[3];
            ss >> id >> t >> pos[0] >> pos[1] >> pos[2] >> tgt[0] >> tgt[1] >> tgt[2] >> roll >> flag;
            auto it = cams.find(id);
            if (it == cams.end()) continue;
            float p[3], g[3];
            int r;
            it->second.first->Evaluate(it->second.second, t, p, g, &r);
            float err = 0;
            for (int i = 0; i < 3; ++i) {
                err = std::max(err, std::fabs(p[i] - pos[i]) / std::max(1.0f, std::fabs(pos[i])));
                err = std::max(err, std::fabs(g[i] - tgt[i]) / std::max(1.0f, std::fabs(tgt[i])));
            }
            ++evals;
            evalWorst = std::max(evalWorst, err);
            if (err > 1e-4f && ++evalBad <= 5) printf("curve MISMATCH cam %d t %g err %g\n", id, t, err);
            // View from the captured (engine) position/target, as the engine does (roll flag was 0 in captures).
            int pitch, yaw;
            game::LookAngles(pos, tgt, &pitch, &yaw);
            haveCam = Invert(game::ViewMatrix(pos, pitch, yaw, flag ? roll : 0), viewInv);
        } else if (kind == 'M' && haveCam) {
            int id;
            game::Mat4 m;
            ss >> id;
            for (float& v : m.m) ss >> v;
            if (id < 0 || !isChunkFile(id)) continue;
            const game::Mat4 w = m * viewInv;
            float scale = 1, err = 0;
            for (float v : m.m) scale = std::max(scale, std::fabs(v));
            const game::Mat4 I = game::Mat4::Identity();
            for (int i = 0; i < 16; ++i) err = std::max(err, std::fabs(w.m[i] - I.m[i]));
            ++chunkDraws;
            if (err <= 5e-3f * scale) ++chunkIdentity;
        } else if (kind == 'F') {
            if (haveCam) ++chunkFrames;
            haveCam = false;  // a camera evaluation must precede the frame's draws
        }
    }
    printf("camera evaluations checked: %d, mismatched: %d, worst relative error %g\n", evals, evalBad, evalWorst);
    printf("static chunk draws under a captured camera: %d, at identity world: %d (%.1f%%)\n", chunkDraws, chunkIdentity,
           chunkDraws ? 100.0 * chunkIdentity / chunkDraws : 0.0);
    return evalBad == 0 && evals > 0 ? 0 : 1;
}
