// tex_test <game dir> <python hashes file>   (also reads <hashes>_banks.txt for pal4 banks)
// Decodes every texture with src/game/texture.cpp and compares the SHA-256 of its RGBA output
// with the reference produced by `python tools/tex_export.py --hashes`.

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

#include "game/exe_image.h"
#include "game/prs.h"
#include "game/texture.h"

#pragma comment(lib, "bcrypt.lib")

namespace fs = std::filesystem;

static std::string Sha256Hex(const uint8_t* data, size_t size) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    uint8_t digest[32];
    BCryptHash(alg, nullptr, 0, const_cast<PUCHAR>(data), static_cast<ULONG>(size), digest, sizeof(digest));
    BCryptCloseAlgorithmProvider(alg, 0);
    char hex[65];
    for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return hex;
}

static std::vector<uint8_t> LoadDecoded(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), {});
    if (raw.size() >= 8) {
        uint32_t want;
        memcpy(&want, raw.data(), 4);
        if (want > 0 && want < 64u * 1024 * 1024) {
            std::vector<uint8_t> out(want);
            if (game::DecompressPrsSafe(raw.data() + 4, raw.size() - 4, out.data(), out.size()) == static_cast<int>(want)) {
                return out;
            }
        }
    }
    return raw;  // stored uncompressed
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: tex_test <game dir> <hashes file>\n");
        return 2;
    }
    const fs::path game = argv[1];

    // name idx -> "WxH FMT HASH" or "FAIL ..."
    std::map<std::string, std::string> ref;
    std::ifstream hf(argv[2]);
    for (std::string line; std::getline(hf, line);) {
        std::istringstream ss(line);
        std::string name, idx, rest;
        ss >> name >> idx;
        std::getline(ss, rest);
        if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
        ref[name + " " + idx] = rest;
    }

    // name idx -> palette bank, from the emulated Tex_GetPaletteBank (tools/tex_export.py).
    std::map<std::string, int> bankOf;
    {
        std::string bp = argv[2];
        bp = bp.substr(0, bp.find_last_of('.')) + "_banks.txt";
        std::ifstream bf(bp);
        std::string name, idx;
        int bank;
        while (bf >> name >> idx >> bank) bankOf[name + " " + idx] = bank;
    }

    game::ExeImage exe;
    if (!exe.Load((game / "Hod2.exe").string())) {
        fprintf(stderr, "cannot load Hod2.exe\n");
        return 2;
    }
    const auto banks = game::LoadPaletteBanks(exe);
    int match = 0, mismatch = 0, skippedBoth = 0, missingRef = 0, files = 0, palMatched = 0;
    for (const game::TextureFileEntry& f : game::ListTextureFiles(exe)) {
        const fs::path path = game / "tex" / f.name;
        if (!fs::exists(path)) continue;
        ++files;
        const std::vector<uint8_t> data = LoadDecoded(path);
        for (int i = 0; i < f.count; ++i) {
            const game::TextureDesc d = game::ReadTextureDesc(exe, f, i);
            const std::string key = f.name + " " + std::to_string(i);
            auto it = ref.find(key);
            if (it == ref.end()) { ++missingRef; continue; }

            std::vector<uint16_t> texels;
            std::vector<uint8_t> rgba;
            uint16_t palette[16] = {};
            const uint16_t* pal = nullptr;
            if (game::LayoutOf(d) == game::TexLayout::Pal4Twiddled) {
                auto b = bankOf.find(key);
                int bank = b == bankOf.end() ? -1 : b->second;
                if (bank < 0 || bank > 0x3F) bank = 0;
                for (int k = 0; k < 16; ++k) {
                    const uint16_t v = bank < static_cast<int>(banks.size()) ? banks[bank][k] : 0;
                    palette[k] = game::ConvertPaletteEntry(v, game::PixelFormatOf(d));
                }
                pal = palette;
            }
            bool ok = game::DecodeTexels(data.data(), data.size(), d, pal, texels);
            if (ok) {
                rgba.resize(texels.size() * 4);
                ok = game::TexelsToRgba(texels.data(), texels.size(), game::PixelFormatOf(d), rgba.data());
            }
            const bool refFail = it->second.rfind("FAIL", 0) == 0;
            if (!ok && refFail) { ++skippedBoth; continue; }
            char expect[160];
            snprintf(expect, sizeof(expect), "%dx%d %08X %s", d.width, d.height, d.format,
                     ok ? Sha256Hex(rgba.data(), rgba.size()).c_str() : "");
            if (ok && !refFail && it->second == expect) {
                ++match;
                if (pal) ++palMatched;
            } else {
                ++mismatch;
                if (mismatch <= 10) printf("MISMATCH %s: ours '%s' ref '%s'\n", key.c_str(), ok ? expect : "FAIL", it->second.c_str());
            }
        }
    }
    printf("texture files: %d  palette banks: %zu  textures matched: %d (pal4: %d)  mismatched: %d  failed in both: %d  no reference: %d\n",
           files, banks.size(), match, palMatched, mismatch, skippedBoth, missingRef);
    return mismatch == 0 && match > 0 ? 0 : 1;
}
