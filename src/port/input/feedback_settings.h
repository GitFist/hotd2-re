#pragma once

#include <array>
#include <string>

#include "native_gun_feedback.h"

namespace input::feedback {

struct Profile {
    int controllerStrength = 0;
    GunType gun = GunType::Off;
    int port = 0;
    int sindenGun = 1, sindenPort = 13000;
    bool recoil = true, damage = true;
    void Normalize();
    bool operator==(const Profile&) const = default;
};

struct Settings {
    std::array<Profile, 2> player;
    Settings() { player[1].sindenGun = 2; }
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
    bool operator==(const Settings&) const = default;
};

} // namespace input::feedback
