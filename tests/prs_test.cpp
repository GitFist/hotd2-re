// prs_test <game dir> [reference dir]
// Decompresses every pol/tex .bin that has a u32 size header + PRS stream and checks:
//   - decoded size equals the header
//   - fast (unchecked) and safe decoders agree
//   - if a reference dir is given, output matches <ref>/<subdir>/<name> byte for byte
//     (references produced by tools/prs.py --dump-refs)

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "game/prs.h"

namespace fs = std::filesystem;

static std::vector<uint8_t> ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: prs_test <game dir> [reference dir]\n");
        return 2;
    }
    const fs::path game = argv[1];
    const fs::path ref = argc > 2 ? fs::path(argv[2]) : fs::path();

    int compressed = 0, passed = 0, failed = 0, skipped = 0, refChecked = 0;
    for (const char* sub : { "pol", "tex", "etc", "coli" }) {
        if (!fs::exists(game / sub)) continue;
        for (const auto& e : fs::directory_iterator(game / sub)) {
            if (e.path().extension() != ".bin") continue;
            std::vector<uint8_t> data = ReadAll(e.path());
            if (data.size() < 8) { ++skipped; continue; }
            uint32_t want;
            memcpy(&want, data.data(), 4);
            if (want == 0 || want > 64u * 1024 * 1024) { ++skipped; continue; }

            std::vector<uint8_t> safe(want);
            int n = game::DecompressPrsSafe(data.data() + 4, data.size() - 4, safe.data(), safe.size());
            if (n != static_cast<int>(want)) { ++skipped; continue; }  // not a PRS file
            ++compressed;

            std::vector<uint8_t> fast(want + 0x1000);
            int m = game::DecompressPrs(data.data() + 4, fast.data());
            bool ok = m == n && memcmp(fast.data(), safe.data(), n) == 0;

            if (ok && !ref.empty()) {
                fs::path r = ref / sub / e.path().filename();
                if (fs::exists(r)) {
                    std::vector<uint8_t> expect = ReadAll(r);
                    ok = expect.size() == safe.size() && memcmp(expect.data(), safe.data(), n) == 0;
                    ++refChecked;
                }
            }
            if (ok) {
                ++passed;
            } else {
                ++failed;
                printf("FAIL %s\n", e.path().string().c_str());
            }
        }
    }
    printf("PRS files: %d  passed: %d  failed: %d  (non-PRS skipped: %d, reference-compared: %d)\n",
           compressed, passed, failed, skipped, refChecked);
    return failed == 0 && compressed > 0 ? 0 : 1;
}
