#include <cmath>
#include <cstdio>
#include <initializer_list>
#include "platform/fixed_clock.h"
#include "platform/present_clock.h"
#include "input/aim_map.h"
#include "play/display_viewport.h"
#include "render/frame_statistics.h"
#include <limits>

int main() {
    int failed = 0;
    auto check = [&](bool value, const char* why) { if (!value) { ++failed; std::fprintf(stderr, "FAIL: %s\n", why); } };
    for (int cap : {0, 30, 60, 120, 144, 165, 240}) {
        platform::FixedClock game;
        platform::PresentClock presentation;
        game.Reset(0);
        int ticks = 0, frames = 0, polls = 0;
        for (int ms = 1; ms <= 1000; ++ms) {
            const double now = ms / 1000.0;
            ++polls; ticks += game.Advance(now);
            if (presentation.Due(now, cap)) ++frames;
        }
        check(ticks >= 59 && ticks <= 60 && polls == 1000, "presentation limit preserves simulation and polling");
        check(cap == 0 ? frames == 1000 : std::abs(frames - cap) <= 1, "requested presentation rate is independent of simulation");
    }
    platform::PresentClock deadlines;
    check(deadlines.Due(0, 30) && !deadlines.Due(.001, 30), "early frame waits without a blocking sleep");
    check(deadlines.Due(.001, 120), "new frame limit applies immediately");
    check(deadlines.Due(10, 120) && !deadlines.Due(10, 120), "a stall drops old presentation deadlines");
    check(deadlines.Due(1, 120), "clock reset recovers without a long freeze");
    const auto integer = input::FitIntegerImage(1920, 1080);
    check(integer.x == 320 && integer.y == 60 && integer.w == 1280 && integer.h == 960, "integer 4:3 image is centered");
    float x = 0, y = 0;
    check(input::WindowToImage(960, 540, integer, &x, &y) && std::abs(x-.5f)<.001f && std::abs(y-.5f)<.001f, "aim maps over the actual integer picture");
    check(!input::WindowToImage(100, 540, integer, &x, &y), "integer black bars remain off-screen shots");
    const auto small = input::FitIntegerImage(480, 360);
    check(small.w == 480 && small.h == 360, "small window falls back to visible proportional fit");
    const auto wide = play::DisplayImageRect(1920,1200,16.f/9.f,false,false);
    check(wide.x==0 && wide.y==60 && wide.w==1920 && wide.h==1080,"wide image fits with correct letterbox");
    check(input::WindowToImage(960,600,wide,&x,&y) && std::abs(x-.5f)<.001f && std::abs(y-.5f)<.001f,"wide center maps accurately");
    check(!input::WindowToImage(960,20,wide,&x,&y),"wide letterbox remains off screen");
    check(std::abs(play::MenuPointerX(.125f,16.f/9.f))<.001f && std::abs(play::MenuPointerX(.875f,16.f/9.f)-1)<.001f,"wide centered menu pointer matches original HUD region");
    check(play::DisplayViewportAspect(0,3840,1080)==port::kNativeAspect && play::DisplayViewportAspect(4,3840,1080)==32.f/9.f,"original is default and window mode clamps valid wide aspect");
    port::FrameStatistics intervals;
    for(int i=0;i<99;++i) intervals.Observe(16.7);
    intervals.Observe(800);
    intervals.Observe(std::numeric_limits<double>::quiet_NaN());
    intervals.Observe(-1);
    check(intervals.Percentile(.95)==16.7&&intervals.Percentile(1)==800,"delivery history retains real long stalls and ignores invalid samples");
    for(int i=0;i<240;++i)intervals.Observe(8.3);
    check(intervals.Percentile(.99)==8.3,"delivery history replaces expired stalls after its full window");
    std::printf("Presentation clock and integer aim mapping: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
