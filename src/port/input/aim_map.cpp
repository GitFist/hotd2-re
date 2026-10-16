#include "aim_map.h"

#include <algorithm>
#include <cmath>

namespace input {

namespace {
float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
}  // namespace

void AbsoluteToScreen(long rawX, long rawY, const Rect& desktop, float* sx, float* sy) {
    *sx = desktop.left + rawX / 65535.0f * desktop.Width();
    *sy = desktop.top + rawY / 65535.0f * desktop.Height();
}

bool ScreenToNormalized(float sx, float sy, const Rect& c, float* nx, float* ny) {
    const float w = static_cast<float>(std::max(c.Width(), 1));
    const float h = static_cast<float>(std::max(c.Height(), 1));
    const float x = (sx - c.left) / w;
    const float y = (sy - c.top) / h;
    *nx = Clamp01(x);
    *ny = Clamp01(y);
    return x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f;
}

bool ApplyAspect(float nx, float ny, float displayW, float displayH, Aspect aspect, float* gx, float* gy) {
    if (aspect == Aspect::Stretch || displayW <= 0 || displayH <= 0) {
        *gx = nx;
        *gy = ny;
        return true;
    }
    const float displayAspect = displayW / displayH;
    constexpr float kGameAspect = 4.0f / 3.0f;
    float x = nx, y = ny;
    if (displayAspect > kGameAspect) {
        const float frac = kGameAspect / displayAspect;  // width fraction covered by the image
        x = (nx - (1.0f - frac) / 2.0f) / frac;
    } else if (displayAspect < kGameAspect) {
        const float frac = displayAspect / kGameAspect;  // height fraction covered by the image
        y = (ny - (1.0f - frac) / 2.0f) / frac;
    }
    *gx = Clamp01(x);
    *gy = Clamp01(y);
    return x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f;
}

void NormalizedToGame(float gx, float gy, int gameW, int gameH, int* x, int* y) {
    *x = std::clamp(static_cast<int>(std::floor(Clamp01(gx) * gameW)), 0, gameW - 1);
    *y = std::clamp(static_cast<int>(std::floor(Clamp01(gy) * gameH)), 0, gameH - 1);
}

ImageRect FitImage(int windowW, int windowH, Aspect aspect) {
    ImageRect r;
    r.w = std::max(windowW, 1);
    r.h = std::max(windowH, 1);
    if (aspect == Aspect::Stretch) return r;
    // 4:3 in integer pixels: the limiting side keeps the window's size, the other is derived from it
    if (r.w * 3 > r.h * 4) {  // wider than 4:3: bars left / right
        r.w = r.h * 4 / 3;
    } else if (r.w * 3 < r.h * 4) {  // taller: bars top / bottom
        r.h = r.w * 3 / 4;
    }
    r.w = std::max(r.w, 1);
    r.h = std::max(r.h, 1);
    r.x = (std::max(windowW, 1) - r.w) / 2;
    r.y = (std::max(windowH, 1) - r.h) / 2;
    return r;
}

bool WindowToImage(float px, float py, const ImageRect& image, float* gx, float* gy) {
    const float w = static_cast<float>(std::max(image.w, 1)), h = static_cast<float>(std::max(image.h, 1));
    const float x = (px - static_cast<float>(image.x)) / w, y = (py - static_cast<float>(image.y)) / h;
    *gx = Clamp01(x);
    *gy = Clamp01(y);
    return x >= 0.0f && x < 1.0f && y >= 0.0f && y < 1.0f;
}

}  // namespace input
