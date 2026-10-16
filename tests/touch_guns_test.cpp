// Unit tests of the platform layer's platform-free gun sources: touch gestures (src/port/input/touch_guns.*) and
// the SDL mouse source (src/port/input/pointer_guns.*).

#include <cstdio>
#include <cstdlib>

#include "input/pointer_guns.h"
#include "input/touch_guns.h"

namespace {
int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                               \
        }                                                           \
    } while (0)

void TestTap() {
    input::TouchGuns t;
    CHECK(!t.Player(0).connected);
    t.OnFingerDown(1, 0.25f, 0.5f, 0.0);
    auto p = t.Player(0);
    CHECK(p.connected && (p.pressed & input::kTrigger) && (p.held & input::kTrigger) && p.onScreen);
    CHECK(p.x == 0.25f && p.y == 0.5f);
    t.BeginFrame();
    t.OnFingerMove(1, 0.3f, 0.6f, 0.01);
    p = t.Player(0);
    CHECK(!(p.pressed & input::kTrigger) && (p.held & input::kTrigger) && p.x == 0.3f && p.y == 0.6f);
    t.OnFingerUp(1, 0.05);
    p = t.Player(0);
    CHECK(!(p.held & input::kTrigger) && (p.released & input::kTrigger));
    CHECK(!t.Player(1).connected);  // touch drives P1 only
}

void TestLongPressReload() {
    input::TouchGuns t;
    t.OnFingerDown(7, 0.5f, 0.5f, 1.0);
    t.BeginFrame();
    t.Update(1.2);
    CHECK(!(t.Player(0).pressed & input::kReload));
    t.Update(1.6);
    CHECK(t.Player(0).pressed & input::kReload);
    t.BeginFrame();
    t.Update(2.5);  // fires once per press
    CHECK(!(t.Player(0).pressed & input::kReload));
    t.OnFingerUp(7, 2.6);
    // a finger that moved is aiming, not a long press
    t.OnFingerDown(8, 0.5f, 0.5f, 3.0);
    t.OnFingerMove(8, 0.6f, 0.5f, 3.1);
    t.BeginFrame();
    t.Update(4.0);
    CHECK(!(t.Player(0).pressed & input::kReload));
}

void TestSecondFingerAndStart() {
    input::TouchGuns t;
    t.OnFingerDown(1, 0.5f, 0.5f, 0.0);
    t.BeginFrame();
    t.OnFingerDown(2, 0.8f, 0.8f, 0.1);
    CHECK(t.Player(0).pressed & input::kReload);
    t.BeginFrame();
    t.OnFingerDown(3, 0.2f, 0.8f, 0.2);
    CHECK(t.Player(0).pressed & input::kStart);
}

void TestPillarboxBars() {
    input::TouchGuns t;
    t.config.aspect = input::Aspect::Pillarbox43;
    t.config.displayW = 1920;
    t.config.displayH = 1080;
    t.OnFingerDown(1, 0.02f, 0.5f, 0.0);  // on the bar before joining: ignored
    CHECK(!t.Player(0).connected);
    t.OnFingerUp(1, 0.05);
    t.OnFingerDown(2, 0.5f, 0.5f, 0.1);  // image centre
    auto p = t.Player(0);
    CHECK(p.connected && p.onScreen && p.x > 0.49f && p.x < 0.51f);
    t.OnFingerUp(2, 0.15);
    t.BeginFrame();
    t.OnFingerDown(3, 0.98f, 0.5f, 0.2);  // right bar: off-screen shot = reload
    p = t.Player(0);
    CHECK((p.pressed & input::kReload) && !p.onScreen && !(p.pressed & input::kTrigger));
}

void TestPointer() {
    input::PointerGuns g;
    g.OnMotion(0, 0.1f, 0.1f);  // motion alone does not join
    CHECK(!g.Player(0).connected);
    g.OnButton(0, 3, true, 0.2f, 0.2f);  // reload does not join
    CHECK(!g.Player(0).connected);
    g.OnButton(0, 1, true, 0.4f, 0.6f);
    auto p = g.Player(0);
    CHECK(p.connected && (p.pressed & input::kTrigger) && p.x == 0.4f && p.y == 0.6f && p.onScreen);
    g.OnButton(5, 1, true, 0.7f, 0.7f);  // single-pointer mode: another device id is still P1
    CHECK(!g.Player(1).connected);
    g.BeginFrame();
    g.OnButton(0, 1, false, 0.4f, 0.6f);
    CHECK(!(g.Player(0).held & input::kTrigger));
    // the pointer query (system pointer) wins for the system-pointer player, also outside the window
    g.pointerQuery = [](float* x, float* y) { *x = 1.2f; *y = 0.5f; return true; };
    p = g.Player(0);
    CHECK(!p.onScreen && p.x == 1.2f);
    // multi-device: ids join in trigger order
    input::PointerGuns m;
    m.multiDevice = true;
    m.OnButton(11, 1, true, 0.1f, 0.1f);
    m.OnButton(22, 1, true, 0.9f, 0.9f);
    CHECK(m.Player(0).connected && m.Player(1).connected && m.Player(1).x == 0.9f && m.Player(0).x == 0.1f);
    m.OnMotion(22, 0.5f, 0.25f);
    CHECK(m.Player(1).x == 0.5f && m.Player(0).x == 0.1f);
    // keyboard start joins P1
    input::PointerGuns k;
    k.OnStartKey(true);
    CHECK(k.Player(0).connected && (k.Player(0).pressed & input::kStart));
}
}  // namespace

int main() {
    TestTap();
    TestLongPressReload();
    TestSecondFingerAndStart();
    TestPillarboxBars();
    TestPointer();
    if (g_fail) {
        fprintf(stderr, "%d failure(s)\n", g_fail);
        return 1;
    }
    printf("touch_guns_test: all passed\n");
    return 0;
}
