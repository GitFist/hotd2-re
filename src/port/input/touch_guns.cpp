#include "touch_guns.h"

#include <algorithm>
#include <cmath>

namespace input {

void TouchGuns::Press(uint8_t bit) {
    p1_.pressed |= bit & ~p1_.held;
}

void TouchGuns::Aim(float nx, float ny) {
    float gx, gy;
    p1_.onScreen = ApplyAspect(nx, ny, config.displayW, config.displayH, config.aspect, &gx, &gy);
    p1_.x = gx;
    p1_.y = gy;
}

void TouchGuns::OnFingerDown(uint64_t finger, float nx, float ny, double t) {
    fingers_.push_back({ finger, nx, ny, t });
    const size_t n = fingers_.size();
    if (n == 1) {
        float gx, gy;
        const bool onImage = ApplyAspect(nx, ny, config.displayW, config.displayH, config.aspect, &gx, &gy);
        if (onImage) {
            if (!p1_.connected) {
                p1_ = PlayerState{};
                p1_.connected = true;
            }
            havePrimary_ = true;
            primary_ = finger;
            Aim(nx, ny);
            Press(kTrigger);
            p1_.held |= kTrigger;
        } else if (p1_.connected) {
            p1_.onScreen = false;  // the bars: off-screen shot
            Press(kReload);
        }
    } else if (n == 2) {
        if (p1_.connected) Press(kReload);
    } else if (n == 3) {
        if (!p1_.connected) {
            p1_ = PlayerState{};
            p1_.connected = true;
        }
        Press(kStart);
    }
}

void TouchGuns::OnFingerMove(uint64_t finger, float nx, float ny, double) {
    for (Finger& f : fingers_) {
        if (f.id != finger) continue;
        if (std::hypot(nx - f.x0, ny - f.y0) > config.moveTolerance) f.moved = true;
        if (havePrimary_ && finger == primary_) Aim(nx, ny);
    }
}

void TouchGuns::OnFingerUp(uint64_t finger, double) {
    fingers_.erase(std::remove_if(fingers_.begin(), fingers_.end(), [&](const Finger& f) { return f.id == finger; }), fingers_.end());
    if (havePrimary_ && finger == primary_) {
        havePrimary_ = false;
        p1_.released |= p1_.held & kTrigger;
        p1_.held &= ~kTrigger;
    }
}

void TouchGuns::Update(double t) {
    if (!havePrimary_ || fingers_.size() != 1) return;
    Finger& f = fingers_.front();
    if (f.id != primary_ || f.moved || f.longFired) return;
    if (t - f.t0 >= config.longPressSeconds) {
        f.longFired = true;
        Press(kReload);
    }
}

void TouchGuns::OnStartKey(bool down) {
    if (!down) return;
    if (!p1_.connected) {
        p1_ = PlayerState{};
        p1_.connected = true;
    }
    Press(kStart);
}

}  // namespace input
