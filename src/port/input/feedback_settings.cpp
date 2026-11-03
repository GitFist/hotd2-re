#include "feedback_settings.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <string_view>

namespace input::feedback {
namespace {
std::string_view Trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}
bool Integer(std::string_view text, int& value) {
    text = Trim(text);
    if (text.empty()) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
}

void Profile::Normalize() {
    controllerStrength = ((std::clamp(controllerStrength, 0, 100) + 12) / 25) * 25;
    if (static_cast<int>(gun) < 0 || static_cast<int>(gun) >= kGunTypeCount) gun = GunType::Off;
    port = std::clamp(port, 0, 256);
    sindenGun = std::clamp(sindenGun, 1, 2);
    if (sindenPort < 1 || sindenPort > 65535) sindenPort = 13000;
}

bool Settings::Load(const std::string& path) {
    std::ifstream file(path);
    if (!file) return false;
    Settings loaded;
    std::string line;
    while (std::getline(file, line)) {
        auto entry = std::string_view(line);
        entry = Trim(entry.substr(0, entry.find_first_of("#;")));
        const auto equals = entry.find('=');
        if (equals == std::string_view::npos) continue;
        const auto key = Trim(entry.substr(0, equals));
        int value;
        if (!Integer(entry.substr(equals + 1), value)) continue;
        for (size_t slot = 0; slot < loaded.player.size(); ++slot) {
            const std::string prefix = "player" + std::to_string(slot + 1) + ".";
            if (!key.starts_with(prefix)) continue;
            const auto field = key.substr(prefix.size());
            auto& profile = loaded.player[slot];
            if (field == "controller_strength") profile.controllerStrength = value;
            else if (field == "gun") {
                // Validate before converting: GunType may have a narrow underlying type.
                profile.gun = value >= 0 && value < kGunTypeCount ? static_cast<GunType>(value) : GunType::Off;
            } else if (field == "port") profile.port = value;
            else if (field == "sinden_gun" && value >= 1 && value <= 2) profile.sindenGun = value;
            else if (field == "sinden_port") profile.sindenPort = value;
            else if (value == 0 || value == 1) {
                if (field == "recoil") profile.recoil = value != 0;
                else if (field == "damage") profile.damage = value != 0;
            }
        }
    }
    if (file.bad()) return false;
    for (auto& profile : loaded.player) profile.Normalize();
    *this = loaded;
    return true;
}

bool Settings::Save(const std::string& path) const {
    auto normalized = *this;
    for (auto& profile : normalized.player) profile.Normalize();
    std::ofstream file(path, std::ios::trunc);
    if (!file) return false;
    file << "# Native feedback. Ports are local COM numbers; 0 means not set.\n";
    for (size_t slot = 0; slot < normalized.player.size(); ++slot) {
        const auto& profile = normalized.player[slot];
        const std::string prefix = "player" + std::to_string(slot + 1) + ".";
        file << prefix << "controller_strength=" << profile.controllerStrength << '\n'
             << prefix << "gun=" << static_cast<int>(profile.gun) << '\n'
             << prefix << "port=" << profile.port << '\n'
             << prefix << "sinden_gun=" << profile.sindenGun << '\n'
             << prefix << "sinden_port=" << profile.sindenPort << '\n'
             << prefix << "recoil=" << profile.recoil << '\n'
             << prefix << "damage=" << profile.damage << '\n';
    }
    file.flush();
    return bool(file);
}
} // namespace input::feedback
