// Deterministic tests for aim coordinate conversion, raw-input device state, and gun-slot state.
// No real devices: reports are fed through RawInput::ProcessMouse with fixed fake handles and a
// fixed geometry provider.

#include <cmath>
#include <cstdio>

#include "gun_state.h"
#include "input/aim_map.h"
#include "input/raw_input.h"

static int g_failures = 0, g_checks = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) {                                                      \
            ++g_failures;                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                   \
    } while (0)

static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

static HANDLE Dev(int n) { return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x1000 + n)); }

static RAWMOUSE Rel(long dx, long dy, USHORT buttons = 0) {
    RAWMOUSE m = {};
    m.usFlags = MOUSE_MOVE_RELATIVE;
    m.lLastX = dx;
    m.lLastY = dy;
    m.usButtonFlags = buttons;
    return m;
}

static RAWMOUSE Abs(long x, long y, USHORT buttons = 0, bool virtualDesk = false) {
    RAWMOUSE m = {};
    m.usFlags = MOUSE_MOVE_ABSOLUTE | (virtualDesk ? MOUSE_VIRTUAL_DESKTOP : 0);
    m.lLastX = x;
    m.lLastY = y;
    m.usButtonFlags = buttons;
    return m;
}

static void TestAimMap() {
    using namespace input;
    int x, y;

    // Game pixels: corners and center, clamped to [0, size-1].
    NormalizedToGame(0.0f, 0.0f, 640, 480, &x, &y);
    CHECK(x == 0 && y == 0);
    NormalizedToGame(1.0f, 1.0f, 640, 480, &x, &y);
    CHECK(x == 639 && y == 479);
    NormalizedToGame(0.5f, 0.5f, 640, 480, &x, &y);
    CHECK(x == 320 && y == 240);

    // The bug this fixes: a 2048x1280 client must still produce 640x480 game coordinates.
    float nx, ny;
    CHECK(ScreenToNormalized(1024.0f, 640.0f, Rect{ 0, 0, 2048, 1280 }, &nx, &ny));
    NormalizedToGame(nx, ny, 640, 480, &x, &y);
    CHECK(x == 320 && y == 240);
    CHECK(ScreenToNormalized(2047.0f, 1279.0f, Rect{ 0, 0, 2048, 1280 }, &nx, &ny));
    NormalizedToGame(nx, ny, 640, 480, &x, &y);
    CHECK(x == 639 && y == 479);

    // Client rect offset on screen (windowed).
    CHECK(ScreenToNormalized(100.0f + 320.0f, 50.0f + 240.0f, Rect{ 100, 50, 740, 530 }, &nx, &ny));
    CHECK(Near(nx, 0.5f) && Near(ny, 0.5f));
    CHECK(!ScreenToNormalized(99.0f, 300.0f, Rect{ 100, 50, 740, 530 }, &nx, &ny));
    CHECK(Near(nx, 0.0f));

    // Absolute reports over primary and virtual desktop.
    float sx, sy;
    AbsoluteToScreen(65535, 0, Rect{ 0, 0, 2048, 1280 }, &sx, &sy);
    CHECK(Near(sx, 2048.0f) && Near(sy, 0.0f));
    AbsoluteToScreen(0, 65535, Rect{ -1920, 0, 2048, 1280 }, &sx, &sy);
    CHECK(Near(sx, -1920.0f) && Near(sy, 1280.0f));

    // Aspect: 16:10 display with a centered 4:3 image (bars 1/12 of the width on each side).
    float gx, gy;
    CHECK(ApplyAspect(0.5f, 0.5f, 2048, 1280, Aspect::Pillarbox43, &gx, &gy));
    CHECK(Near(gx, 0.5f) && Near(gy, 0.5f));
    const float bar = (1.0f - (4.0f / 3.0f) / 1.6f) / 2.0f;  // 0.08333
    CHECK(ApplyAspect(bar, 0.2f, 2048, 1280, Aspect::Pillarbox43, &gx, &gy));
    CHECK(Near(gx, 0.0f) && Near(gy, 0.2f));
    CHECK(!ApplyAspect(bar * 0.5f, 0.5f, 2048, 1280, Aspect::Pillarbox43, &gx, &gy));  // on the bar
    CHECK(!ApplyAspect(1.0f - bar * 0.5f, 0.5f, 2048, 1280, Aspect::Pillarbox43, &gx, &gy));
    // 5:4 display: letterbox top/bottom.
    CHECK(!ApplyAspect(0.5f, 0.01f, 1280, 1024, Aspect::Pillarbox43, &gx, &gy));
    // 4:3 display and Stretch are identity.
    CHECK(ApplyAspect(0.3f, 0.7f, 640, 480, Aspect::Pillarbox43, &gx, &gy) && Near(gx, 0.3f) && Near(gy, 0.7f));
    CHECK(ApplyAspect(0.03f, 0.7f, 2048, 1280, Aspect::Stretch, &gx, &gy) && Near(gx, 0.03f));
}

static input::RawInput MakeRaw() {
    input::RawInput r;
    r.geometryProvider = [] {
        input::Geometry g;
        g.client = { 0, 0, 2048, 1280 };
        g.primary = { 0, 0, 2048, 1280 };
        g.virtualDesk = { 0, 0, 2048, 1280 };
        return g;
    };
    return r;
}

static void TestRawInputDevices() {
    using namespace input;
    RawInput r = MakeRaw();

    // Synthetic input (null handle) is rejected and cannot join.
    CHECK(!r.ProcessMouse(nullptr, Abs(32768, 32768, RI_MOUSE_LEFT_BUTTON_DOWN)));
    CHECK(!r.Player(0).connected && !r.Player(1).connected);

    // Movement without a trigger pull does not join.
    r.ProcessMouse(Dev(1), Rel(100, 0));
    CHECK(!r.Player(0).connected);

    // First trigger -> P1 (absolute gun), second device -> P2 (relative mouse), third ignored.
    r.ProcessMouse(Dev(1), Abs(16384, 49151, RI_MOUSE_LEFT_BUTTON_DOWN));
    CHECK(r.Player(0).connected && !r.Player(1).connected);
    CHECK(Near(r.Player(0).x, 0.25f, 0.002f) && Near(r.Player(0).y, 0.75f, 0.002f));
    CHECK(r.Player(0).onScreen);
    CHECK(r.Player(0).pressed & kTrigger);
    CHECK(r.Player(0).held & kTrigger);

    r.ProcessMouse(Dev(2), Rel(0, 0, RI_MOUSE_LEFT_BUTTON_DOWN));
    CHECK(r.Player(1).connected);
    CHECK(Near(r.Player(1).x, 0.5f) && Near(r.Player(1).y, 0.5f));
    r.ProcessMouse(Dev(3), Rel(0, 0, RI_MOUSE_LEFT_BUTTON_DOWN));  // no free slot
    CHECK(r.Player(0).connected && r.Player(1).connected);

    // Per-player independence: moving P2 does not move P1.
    const float p1x = r.Player(0).x;
    r.ProcessMouse(Dev(2), Rel(500, 0));
    CHECK(Near(r.Player(0).x, p1x));
    CHECK(r.Player(1).x > 0.5f);

    // Relative movement clamps at the edge.
    r.ProcessMouse(Dev(2), Rel(100000, 100000));
    CHECK(Near(r.Player(1).x, 1.0f) && Near(r.Player(1).y, 1.0f));

    // Edges clear on BeginFrame; held persists until release.
    r.BeginFrame();
    CHECK(r.Player(0).pressed == 0 && (r.Player(0).held & kTrigger));
    r.ProcessMouse(Dev(1), Abs(16384, 49151, RI_MOUSE_LEFT_BUTTON_UP));
    CHECK(!(r.Player(0).held & kTrigger) && (r.Player(0).released & kTrigger));

    // Absolute device pointing off the window: off-screen, position clamped.
    RawInput w;
    w.geometryProvider = [] {
        Geometry g;
        g.client = { 100, 100, 740, 580 };  // 640x480 window on a 2048x1280 desktop
        g.primary = { 0, 0, 2048, 1280 };
        g.virtualDesk = g.primary;
        return g;
    };
    w.ProcessMouse(Dev(9), Abs(0, 0, RI_MOUSE_LEFT_BUTTON_DOWN));
    CHECK(w.Player(0).connected && !w.Player(0).onScreen);
    CHECK(Near(w.Player(0).x, 0.0f) && Near(w.Player(0).y, 0.0f));
    // Center of that window.
    w.ProcessMouse(Dev(9), Abs(static_cast<long>(420 / 2048.0 * 65535), static_cast<long>(340 / 1280.0 * 65535)));
    CHECK(w.Player(0).onScreen && Near(w.Player(0).x, 0.5f, 0.01f) && Near(w.Player(0).y, 0.5f, 0.01f));

    // Unplugging frees the slot; the next trigger rejoins.
    r.RemoveDevice(Dev(1));
    CHECK(!r.Player(0).connected);
    r.ProcessMouse(Dev(3), Rel(0, 0, RI_MOUSE_LEFT_BUTTON_DOWN));
    CHECK(r.Player(0).connected);
}

static void TestGunSlot() {
    using namespace hook;
    AimConfig cfg;  // 640x480, stretch
    uint32_t prev = 0;
    input::PlayerState p;
    p.connected = true;

    // Trigger pressed on-screen: fired, aim in game pixels.
    p.x = 0.5f; p.y = 0.25f; p.onScreen = true;
    p.held = input::kTrigger; p.pressed = input::kTrigger;
    GunSlotState s = ComputeGunSlot(p, prev, cfg);
    CHECK(s.fired == 1 && s.offscreen == 0);
    CHECK(s.x == 320 && s.y == 120);
    CHECK(s.held == kBitTrigger && s.pressed == kBitTrigger && s.released == 0);

    // Held without a new press: no repeat fire; release reported once.
    p.pressed = 0;
    s = ComputeGunSlot(p, prev, cfg);
    CHECK(s.fired == 0 && s.held == kBitTrigger);
    p.held = 0;
    s = ComputeGunSlot(p, prev, cfg);
    CHECK(s.released == kBitTrigger);
    s = ComputeGunSlot(p, prev, cfg);
    CHECK(s.released == 0);

    // Reload button: off-screen shot.
    p.pressed = input::kReload; p.held = input::kReload;
    s = ComputeGunSlot(p, prev, cfg);
    CHECK(s.fired == 0 && s.offscreen == 1);

    // Trigger + reload together: off-screen only (mode 5 rule).
    p.pressed = input::kTrigger | input::kReload;
    s = ComputeGunSlot(p, prev, cfg);
    CHECK(s.fired == 0 && s.offscreen == 1);

    // Trigger while the device points off the window: reload, like a real light gun.
    p.pressed = input::kTrigger; p.held = input::kTrigger; p.onScreen = false;
    s = ComputeGunSlot(p, prev, cfg);
    CHECK(s.fired == 0 && s.offscreen == 1);

    // Trigger on a pillarbox bar (4:3 mode on 16:10): reload.
    cfg.aspect = input::Aspect::Pillarbox43;
    cfg.displayW = 2048; cfg.displayH = 1280;
    p.onScreen = true; p.x = 0.02f; p.y = 0.5f;
    s = ComputeGunSlot(p, prev, cfg);
    CHECK(s.fired == 0 && s.offscreen == 1);
    // Inside the image: fires, mapped through the bars.
    p.x = 0.5f;
    s = ComputeGunSlot(p, prev, cfg);
    CHECK(s.fired == 1 && s.x == 320 && s.y == 240);

    // Start button maps to bit 0x8.
    cfg.aspect = input::Aspect::Stretch;
    p.pressed = input::kStart; p.held = input::kStart;
    s = ComputeGunSlot(p, prev, cfg);
    CHECK(s.pressed == kBitStart && s.fired == 0 && s.offscreen == 0);
}

int main() {
    TestAimMap();
    TestRawInputDevices();
    TestGunSlot();
    printf("input tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
