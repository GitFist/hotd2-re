#include "assets/asset_override.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>

namespace port {

namespace {

std::string& Dir() {
    static std::string dir;
    static bool init = false;
    if (!init) {
        init = true;
        if (const char* e = getenv("HOTD2_ASSET_DIR"); e && *e) dir = e;
    }
    return dir;
}

std::atomic<int> g_hits{ 0 };  // AssetStore's prefetch threads look overrides up too

}  // namespace

const std::string& AssetDir() { return Dir(); }

void SetAssetDir(const std::string& dir) {
    if (const char* e = getenv("HOTD2_ASSET_DIR"); e && *e) return;
    Dir() = dir;
}

std::string AssetOverride(const std::string& rel) {
    const std::string& dir = Dir();
    if (dir.empty()) return {};
    std::string r = rel;
    std::replace(r.begin(), r.end(), '\\', '/');
    std::filesystem::path p = std::filesystem::path(dir) / r;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) return {};
    ++g_hits;
    return p.string();
}

int AssetOverrideHits() { return g_hits; }

std::string GamePath(const std::string& gameDir, const std::string& rel) {
    std::string p = gameDir;
    if (!p.empty() && p.back() != '/' && p.back() != '\\') p += '/';
    p += rel;
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

std::string SoundPath(const std::string& gameDir, const std::string& rel) {
    if (std::string o = AssetOverride(rel); !o.empty()) return o;
    return GamePath(gameDir, rel);
}

}  // namespace port
