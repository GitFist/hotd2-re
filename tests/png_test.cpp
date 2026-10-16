// png_test <list file> : decodes every PNG named in the list file (one path per line, optionally followed by a space
// and the expected CRC-32 of its RGBA8 bytes, rows top-down, as tools/png_hashes.py writes with zlib.crc32) with the
// port's own reader (src/port/render/png_read.cpp) and checks the CRC. Without CRCs it only reports decode failures.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#include "render/png_read.h"

static uint32_t Crc32(const std::vector<uint8_t>& b) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t v : b) c = table[(c ^ v) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: png_test <list file>\n");
        return 2;
    }
    std::ifstream list(argv[1]);
    int ok = 0, bad = 0;
    for (std::string line; std::getline(list, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        std::string path = line, want;
        if (const size_t sp = line.rfind(' '); sp != std::string::npos) {
            path = line.substr(0, sp);
            want = line.substr(sp + 1);
        }
        port::PngImage img;
        std::string err;
        if (!port::ReadPngFile(path, img, &err)) {
            printf("FAIL %s: %s\n", path.c_str(), err.c_str());
            ++bad;
            continue;
        }
        char got[16];
        snprintf(got, sizeof(got), "%08x", Crc32(img.rgba));
        if (!want.empty() && want != got) {
            printf("FAIL %s: %d x %d crc %s, expected %s\n", path.c_str(), img.width, img.height, got, want.c_str());
            ++bad;
            continue;
        }
        ++ok;
    }
    printf("%d ok, %d failed\n", ok, bad);
    return bad ? 1 : 0;
}
