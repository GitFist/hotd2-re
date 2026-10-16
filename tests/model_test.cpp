// model_test <game dir> <python digests file>
// Parses every model in every pol\*.bin with src/game/model.cpp and compares mesh/strip/vertex counts and a
// SHA-256 over flags, material words and decoded vertices with tools/pol_parse.py --digests.

#include <windows.h>

#include <bcrypt.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "game/model.h"
#include "game/prs.h"

#pragma comment(lib, "bcrypt.lib")

namespace fs = std::filesystem;

namespace {

class Sha256 {
public:
    Sha256() {
        BCryptOpenAlgorithmProvider(&alg_, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        BCryptCreateHash(alg_, &hash_, nullptr, 0, nullptr, 0, 0);
    }
    ~Sha256() {
        BCryptDestroyHash(hash_);
        BCryptCloseAlgorithmProvider(alg_, 0);
    }
    void Add(const void* p, size_t n) { BCryptHashData(hash_, (PUCHAR)p, (ULONG)n, 0); }
    std::string Hex() {
        uint8_t d[32];
        BCryptFinishHash(hash_, d, 32, 0);
        char hex[65];
        for (int i = 0; i < 32; ++i) snprintf(hex + 2 * i, 3, "%02x", d[i]);
        return hex;
    }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
};

std::vector<uint8_t> LoadDecoded(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), {});
    if (raw.size() >= 8) {
        uint32_t want;
        memcpy(&want, raw.data(), 4);
        if (want > 0 && want < (64u << 20)) {
            std::vector<uint8_t> out(want);
            if (game::DecompressPrsSafe(raw.data() + 4, raw.size() - 4, out.data(), out.size()) == static_cast<int>(want))
                return out;
        }
    }
    return raw;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: model_test <game dir> <digests file>\n");
        return 2;
    }
    std::map<std::string, std::string> ref;  // "name k" -> "meshes strips verts sha"
    std::ifstream df(argv[2]);
    for (std::string line; std::getline(df, line);) {
        std::istringstream ss(line);
        std::string name, k, rest;
        ss >> name >> k;
        std::getline(ss, rest);
        if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
        ref[name + " " + k] = rest;
    }

    int files = 0, noHeader = 0, models = 0, match = 0, mismatch = 0, parseFail = 0;
    size_t meshes = 0, strips = 0, verts = 0;
    for (const auto& e : fs::directory_iterator(fs::path(argv[1]) / "pol")) {
        if (e.path().extension() != ".bin") continue;
        const std::vector<uint8_t> data = LoadDecoded(e.path());
        const auto entries = game::ReadPolHeader(data.data(), data.size());
        if (game::PolModelCount(data.data(), data.size()) < 0) {
            ++noHeader;
            continue;
        }
        ++files;
        const std::string name = e.path().filename().string();
        for (size_t k = 0; k < entries.size(); ++k) {
            ++models;
            game::Model m;
            std::string err;
            if (!game::ParseModel(data.data(), data.size(), entries[k], m, &err)) {
                ++parseFail;
                if (parseFail <= 10) printf("PARSE FAIL %s %zu: %s\n", name.c_str(), k, err.c_str());
                continue;
            }
            Sha256 h;
            h.Add(&m.word0, 4);
            h.Add(&m.flags, 4);
            h.Add(m.sphere, 16);
            size_t ns = 0, nv = 0;
            for (const auto& mesh : m.meshes) {
                h.Add(mesh.material.words, 80);
                for (const auto& s : mesh.strips) {
                    const uint32_t hdr[2] = { s.ctrl, static_cast<uint32_t>(s.vertices.size()) };
                    h.Add(hdr, 8);
                    h.Add(s.vertices.data(), s.vertices.size() * sizeof(game::ModelVertex));
                    ++ns;
                    nv += s.vertices.size();
                }
            }
            meshes += m.meshes.size();
            strips += ns;
            verts += nv;
            char mine[128];
            snprintf(mine, sizeof(mine), "%zu %zu %zu %s", m.meshes.size(), ns, nv, h.Hex().c_str());
            auto it = ref.find(name + " " + std::to_string(k));
            if (it != ref.end() && it->second == mine) {
                ++match;
            } else {
                ++mismatch;
                if (mismatch <= 10) printf("MISMATCH %s %zu: ours '%s' ref '%s'\n", name.c_str(), k, mine,
                                           it == ref.end() ? "(none)" : it->second.c_str());
            }
        }
    }
    printf("pol files with models: %d (without model header: %d)  models: %d  matched: %d  mismatched: %d  parse failures: %d\n",
           files, noHeader, models, match, mismatch, parseFail);
    printf("meshes: %zu  strips: %zu  vertices: %zu\n", meshes, strips, verts);
    return mismatch == 0 && parseFail == 0 && match > 0 ? 0 : 1;
}
