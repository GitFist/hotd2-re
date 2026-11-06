#include "gamepad_settings.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <string_view>

namespace input::gamepad {
namespace {
constexpr int kControlCount = static_cast<int>(Control::Count);
constexpr uint16_t kValidControls = (uint16_t(1) << kControlCount) - 1;
constexpr std::array<const char*, kControlCount> kControlNames{
    "A/Cross", "B/Circle", "X/Square", "Y/Triangle", "View/Share",
    "Menu/Options", "LB/L1", "RB/R1", "LT/L2", "RT/R2"
};
constexpr std::array<const char*, 3> kActionKeys{"fire", "reload", "pause"};

std::string_view Trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool ParseInteger(std::string_view text, int& value) {
    text = Trim(text);
    if (text.empty()) return false;
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2);
        base = 16;
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
} // namespace

void Profile::Normalize() {
    sensitivity = std::clamp(sensitivity, 25, 300);
    deadzone = std::clamp(deadzone, 0, 40);
    uint16_t used = 0;
    for (auto& binding : bindings) {
        binding &= kValidControls & ~used;
        used |= binding;
    }
}

void Profile::CycleBinding(int action, int direction) {
    if (action < 0 || action >= static_cast<int>(bindings.size()) || !direction) return;
    Normalize();
    int current = -1;
    for (int i = 0; i < kControlCount; ++i) {
        if (bindings[action] & (uint16_t(1) << i)) { current = i; break; }
    }
    const uint16_t previous = current < 0 ? 0 : uint16_t(1) << current;
    int next = current < 0 ? (direction > 0 ? -1 : 0) : current;
    for (int attempt = 0; attempt < kControlCount; ++attempt) {
        next = (next + (direction > 0 ? 1 : kControlCount - 1) + kControlCount) % kControlCount;
        const uint16_t chosen = uint16_t(1) << next;
        // Keep alternate controls (for example RT alongside A) on this action.
        if (bindings[action] & chosen) continue;
        bool used = false;
        for (int other = 0; other < int(bindings.size()); ++other)
            if (other != action && (bindings[other] & chosen)) used = true;
        if (!previous && used) continue;  // an unassigned action can claim a free control
        for (int other = 0; other < int(bindings.size()); ++other)
            if (other != action && (bindings[other] & chosen))
                bindings[other] = (bindings[other] & ~chosen) | previous;
        bindings[action] = (bindings[action] & ~previous) | chosen;
        return;
    }
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
        if (!ParseInteger(entry.substr(equals + 1), value)) continue;
        for (size_t i = 0; i < loaded.player.size(); ++i) {
            const std::string prefix = "player" + std::to_string(i + 1) + ".";
            if (!key.starts_with(prefix)) continue;
            const auto field = key.substr(prefix.size());
            auto& profile = loaded.player[i];
            if (field == "sensitivity") profile.sensitivity = value;
            else if (field == "deadzone") profile.deadzone = value;
            else if (value >= 0 && value <= kValidControls) {
                for (size_t action = 0; action < kActionKeys.size(); ++action)
                    if (field == kActionKeys[action]) profile.bindings[action] = uint16_t(value);
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
    file << "# Controller sensitivity/deadzone are percentages. Bindings are control bit masks.\n";
    for (size_t i = 0; i < normalized.player.size(); ++i) {
        const auto& profile = normalized.player[i];
        const std::string prefix = "player" + std::to_string(i + 1) + ".";
        file << prefix << "sensitivity=" << profile.sensitivity << '\n'
             << prefix << "deadzone=" << profile.deadzone << '\n';
        for (size_t action = 0; action < kActionKeys.size(); ++action)
            file << prefix << kActionKeys[action] << '=' << profile.bindings[action] << '\n';
    }
    file.flush();
    return bool(file);
}

const char* ControlName(Control control) {
    const int index = static_cast<int>(control);
    return index >= 0 && index < kControlCount ? kControlNames[index] : "Unassigned";
}

std::string BindingName(uint16_t mask) {
    std::string name;
    for (int i = 0; i < kControlCount; ++i) {
        if (!(mask & (uint16_t(1) << i))) continue;
        if (!name.empty()) name += " + ";
        name += kControlNames[i];
    }
    return name.empty() ? "Unassigned" : name;
}
} // namespace input::gamepad
