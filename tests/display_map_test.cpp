// The game image's placement in the window (src/port/input/aim_map.h FitImage) and the aim mapping over it
// (WindowToImage), as play.exe's display settings use them: 4:3 pillarbox / letterbox and stretch, integer window
// scales, agreement with the older normalized ApplyAspect mapping. No game data needed.
#include <cmath>
#include <cstdio>

#include "input/aim_map.h"

namespace {

int g_checks = 0, g_failed = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        printf("FAIL: %s\n", what);
    }
}

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

}  // namespace

int main() {
    using input::Aspect;
    using input::FitImage;
    using input::ImageRect;
    using input::WindowToImage;

    // exact 4:3 windows (the 1x / 2x / 3x window scales): the image is the whole window
    for (int k = 1; k <= 3; ++k) {
        const ImageRect r = FitImage(640 * k, 480 * k, Aspect::Pillarbox43);
        Check(r.x == 0 && r.y == 0 && r.w == 640 * k && r.h == 480 * k, "4:3 window: no bars");
    }
    // a 16:9 desktop (fullscreen): pillarbox, 4:3 of the height, centred
    {
        const ImageRect r = FitImage(1920, 1080, Aspect::Pillarbox43);
        Check(r.w == 1440 && r.h == 1080 && r.x == 240 && r.y == 0, "1920x1080: 1440 wide, bars of 240");
        const ImageRect s = FitImage(1920, 1080, Aspect::Stretch);
        Check(s.x == 0 && s.y == 0 && s.w == 1920 && s.h == 1080, "stretch fills the window");
    }
    // a tall window: letterbox
    {
        const ImageRect r = FitImage(800, 1000, Aspect::Pillarbox43);
        Check(r.w == 800 && r.h == 600 && r.x == 0 && r.y == 200, "800x1000: 600 high, bars of 200");
    }
    // degenerate sizes never produce an empty rectangle
    {
        const ImageRect r = FitImage(0, 0, Aspect::Pillarbox43);
        Check(r.w >= 1 && r.h >= 1, "zero window: 1x1 image");
    }

    // aim mapping: window pixels -> normalized over the image; the bars are off screen
    {
        const ImageRect r = FitImage(1920, 1080, Aspect::Pillarbox43);
        float gx, gy;
        Check(WindowToImage(240.0f, 0.0f, r, &gx, &gy) && Near(gx, 0.0f) && Near(gy, 0.0f), "image top-left corner -> (0, 0)");
        Check(WindowToImage(960.0f, 540.0f, r, &gx, &gy) && Near(gx, 0.5f) && Near(gy, 0.5f), "window centre -> image centre");
        Check(!WindowToImage(100.0f, 540.0f, r, &gx, &gy) && gx == 0.0f, "left bar: off screen, clamped to 0");
        Check(!WindowToImage(1900.0f, 540.0f, r, &gx, &gy) && gx == 1.0f, "right bar: off screen, clamped to 1");
        Check(WindowToImage(240.0f + 1440.0f * 0.25f, 1080.0f * 0.75f, r, &gx, &gy) && Near(gx, 0.25f) && Near(gy, 0.75f), "a quarter in");
        // agreement with ApplyAspect (the touch source's normalized mapping) at the same window
        float ax, ay;
        input::ApplyAspect(0.6f, 0.3f, 1920.0f, 1080.0f, Aspect::Pillarbox43, &ax, &ay);
        WindowToImage(0.6f * 1920.0f, 0.3f * 1080.0f, r, &gx, &gy);
        Check(Near(ax, gx, 1e-3f) && Near(ay, gy, 1e-3f), "FitImage + WindowToImage agree with ApplyAspect");
    }
    // stretch: identity over the window
    {
        const ImageRect r = FitImage(1000, 500, Aspect::Stretch);
        float gx, gy;
        Check(WindowToImage(250.0f, 125.0f, r, &gx, &gy) && Near(gx, 0.25f) && Near(gy, 0.25f), "stretch: window fraction = image fraction");
        Check(!WindowToImage(1000.0f, 250.0f, r, &gx, &gy), "the right edge itself is outside");
    }

    printf("%d / %d checks passed\n", g_checks - g_failed, g_checks);
    return g_failed == 0 ? 0 : 1;
}
