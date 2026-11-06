#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace input::gamepad {

// Local menu metadata, distinct from rebindable gameplay Pause. FocusedGuns
// suppresses this together with the rest of a held menu control on refocus.
constexpr uint32_t kMenuStart = 0x4000;
constexpr uint32_t kMenuPreviousCategory = 0x8000;
constexpr uint32_t kMenuNextCategory = 0x10000;

enum class Control {
    South, East, West, North, Back, Start, LeftShoulder, RightShoulder,
    LeftTrigger, RightTrigger, Count
};
enum class Action { Fire, Reload, Pause };

constexpr uint16_t ControlMask(Control control) {
    return uint16_t(1) << static_cast<int>(control);
}

struct Profile {
    int sensitivity = 100;
    int deadzone = 18;
    std::array<uint16_t, 3> bindings{
        ControlMask(Control::South) | ControlMask(Control::RightTrigger),
        ControlMask(Control::East) | ControlMask(Control::West) |
            ControlMask(Control::RightShoulder) | ControlMask(Control::LeftTrigger),
        ControlMask(Control::Start)
    };

    // Cycle the first control, preserving alternates and swapping conflicts.
    // A physical control belongs to one action at a time; other actions stay bound.
    void CycleBinding(int action, int direction);
    void Normalize();
    bool operator==(const Profile&) const = default;
};

struct Settings {
    std::array<Profile, 2> player;
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
    bool operator==(const Settings&) const = default;
};

const char* ControlName(Control control);
std::string BindingName(uint16_t mask);

} // namespace input::gamepad
