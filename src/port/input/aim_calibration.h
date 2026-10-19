#pragma once
#include <array>

namespace input {
struct AimPoint { double x=0, y=0; };
struct AimCalibration {
    std::array<double,9> h{1,0,0,0,1,0,0,0,1};
    bool calibrated=false;
    bool Apply(AimPoint raw, AimPoint& mapped) const;
    // Four inset targets, clockwise from top left: (.15,.15) to (.85,.85).
    // Invalid samples leave the previous calibration intact.
    bool Fit(const std::array<AimPoint,4>& samples);
};
}
