#pragma once

#include <initializer_list>
#include <string_view>

namespace port {

// Local appearance only; the original red and green choices keep their saved names.
enum class BloodColor { Red, Green, Blue, Purple };

inline const char* BloodColorName(BloodColor color) {
    switch (color) {
    case BloodColor::Green: return "green";
    case BloodColor::Blue: return "blue";
    case BloodColor::Purple: return "purple";
    default: return "red";
    }
}

inline const char* BloodColorLabel(BloodColor color) {
    switch (color) {
    case BloodColor::Green: return "Green";
    case BloodColor::Blue: return "Blue";
    case BloodColor::Purple: return "Purple";
    default: return "Red";
    }
}

inline bool ParseBloodColor(std::string_view value, BloodColor& color) {
    for (const auto candidate : {BloodColor::Red, BloodColor::Green, BloodColor::Blue, BloodColor::Purple}) {
        if (value == BloodColorName(candidate)) {
            color = candidate;
            return true;
        }
    }
    return false;
}

}  // namespace port
