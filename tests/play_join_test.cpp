// play_join_test <game dir>
//
// play.exe's raw-input join logic, driven with SCRIPTED reports only (no physical device; nothing here proves anything
// about real guns or mice, and the device filtering is the production code, unchanged):
//   reports -> input::RawInput::ProcessMouse (fixed fake device handles, fixed geometry; null-handle reports must be
//   ignored) -> per-player PlayerState -> play::DeviceWorldInput (the conversion play.exe uses) -> game::World::Boot
//   (front end from boot frame 0) -> player states.
// Expected player states = the original's, from build\scene_capture_2p.txt (script_2p.txt: P1 start 700, trigger 900;
// P2 start 1400): P1 0 at 900, 5 from 902; P2 10 (waiting for the intro cutscene) from 1400, 5 from 2431. Here P1's
// device pulls its trigger at 700 (title: the trigger presses start for a player not in the game) and 900 (mode
// select: joins), P2's device joins the input layer with its first trigger pull at 1400 (start for the world).

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include "game/world.h"
#include "input/raw_input.h"
#include "port/play/gun_input.h"

namespace {

int g_failures = 0, g_checks = 0;
#define CHECK(cond)                                                \
    do {                                                           \
        ++g_checks;                                                \
        if (!(cond)) {                                             \
            ++g_failures;                                          \
            printf("FAIL line %d: %s\n", __LINE__, #cond);         \
        }                                                          \
    } while (0)

HANDLE Dev(int n) { return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x2000 + n)); }

RAWMOUSE Abs(long x, long y, USHORT buttons = 0) {
    RAWMOUSE m = {};
    m.usFlags = MOUSE_MOVE_ABSOLUTE;
    m.lLastX = x;
    m.lLastY = y;
    m.usButtonFlags = buttons;
    return m;
}
RAWMOUSE Rel(long dx, long dy, USHORT buttons = 0) {
    RAWMOUSE m = {};
    m.usFlags = MOUSE_MOVE_RELATIVE;
    m.lLastX = dx;
    m.lLastY = dy;
    m.usButtonFlags = buttons;
    return m;
}

struct Report {
    HANDLE dev;
    RAWMOUSE m;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: play_join_test <game dir>\n");
        return 2;
    }
    game::World world;
    std::string err;
    if (!world.Load(argv[1], 0, &err)) {
        fprintf(stderr, "load: %s\n", err.c_str());
        return 2;
    }
    input::RawInput raw;
    raw.geometryProvider = [] {
        input::Geometry g;
        g.client = { 0, 0, 640, 480 };
        g.primary = { 0, 0, 640, 480 };
        g.virtualDesk = { 0, 0, 640, 480 };
        return g;
    };
    // Reports delivered before the game frame with that number (play.exe: messages between two game frames).
    std::multimap<int, Report> reports;
    const long cx = 32768, cy = 32768;  // the centre of the "screen" (absolute 0..65535)
    auto at = [&](int f, HANDLE h, RAWMOUSE m) { reports.insert({ f, Report{ h, m } }); };
    at(650, nullptr, Abs(cx, cy, RI_MOUSE_LEFT_BUTTON_DOWN));   // synthetic (no handle): must not join or press
    at(651, nullptr, Abs(cx, cy, RI_MOUSE_LEFT_BUTTON_UP));
    at(690, Dev(2), Rel(40, 0));                                 // P2's mouse moves first: moving does not join
    at(700, Dev(1), Abs(cx, cy, RI_MOUSE_LEFT_BUTTON_DOWN));    // P1's gun: joins the input layer, title start
    at(712, Dev(1), Abs(cx, cy, RI_MOUSE_LEFT_BUTTON_UP));
    at(900, Dev(1), Abs(cx, cy, RI_MOUSE_LEFT_BUTTON_DOWN));    // mode select: joins the game
    at(903, Dev(1), Abs(cx, cy, RI_MOUSE_LEFT_BUTTON_UP));
    at(1400, Dev(2), Rel(0, 0, RI_MOUSE_LEFT_BUTTON_DOWN));     // P2's mouse: first trigger pull = P2, start
    at(1403, Dev(2), Rel(0, 0, RI_MOUSE_LEFT_BUTTON_UP));
    at(1500, Dev(3), Abs(cx, cy, RI_MOUSE_LEFT_BUTTON_DOWN));   // a third device: no free player
    at(1501, Dev(3), Abs(cx, cy, RI_MOUSE_LEFT_BUTTON_UP));
    at(2600, Dev(2), Rel(300, -200));                            // P2 aims: only P2's aim moves
    at(2700, Dev(1), Abs(16384, 16384));                         // P1 aims at (0.25, 0.25)

    game::WorldConfig boot;
    boot.creditSetting = 4;  // 5 credits (the 2p capture's setup; free play joins the same way)
    boot.startFrame = 0;
    boot.gameOver = true;
    world.Boot(boot);
    uint8_t prevButtons[2] = {};
    int p1Join = -1, p1Play = -1, p2Wait = -1, p2Play = -1, firstConnect[2] = { -1, -1 };
    float aim2600[2][2] = {}, aim2800[2][2] = {};
    for (int f = 1; f <= 2800; ++f) {
        auto range = reports.equal_range(f);
        for (auto it = range.first; it != range.second; ++it) {
            const bool accepted = raw.ProcessMouse(it->second.dev, it->second.m);
            if (!it->second.dev) CHECK(!accepted);
        }
        for (int p = 0; p < 2; ++p) {
            const input::PlayerState& ps = raw.Player(p);
            if (!ps.connected) continue;
            if (firstConnect[p] < 0) firstConnect[p] = f;
            play::DevicePlayer dp;
            dp.connected = true;
            dp.x = ps.x;
            dp.y = ps.y;
            dp.onScreen = ps.onScreen;
            dp.held = ps.held;
            dp.pressed = ps.pressed;
            world.SetInput(p, play::DeviceWorldInput(dp, world.Hud().player[p].state, prevButtons[p]));
        }
        raw.BeginFrame();
        world.Step();
        CHECK(world.Frame() == f);
        const auto& h = world.Hud();
        if (f == 899) CHECK(world.GameMode() == 4);
        if (p1Join < 0 && h.player[0].state == 0) p1Join = f;
        if (p1Play < 0 && h.player[0].state == 5) p1Play = f;
        if (p2Wait < 0 && h.player[1].state == 10) p2Wait = f;
        if (p2Play < 0 && h.player[1].state == 5) p2Play = f;
        if (f == 1399) CHECK(h.player[1].state == 9);
        if (f == 2599)
            for (int p = 0; p < 2; ++p) aim2600[p][0] = h.player[p].crosshairPos[0], aim2600[p][1] = h.player[p].crosshairPos[1];
        if (f == 2800)
            for (int p = 0; p < 2; ++p) aim2800[p][0] = h.player[p].crosshairPos[0], aim2800[p][1] = h.player[p].crosshairPos[1];
    }
    printf("input layer: P1 connected at %d, P2 at %d (null-handle and third-device reports ignored)\n", firstConnect[0], firstConnect[1]);
    printf("world: P1 state 0 at %d, 5 at %d; P2 state 10 at %d, 5 at %d (original: 900, 902; 1400, 2431)\n", p1Join, p1Play,
           p2Wait, p2Play);
    printf("aim 2599 -> 2800: P1 (%.1f, %.1f) -> (%.1f, %.1f), P2 (%.1f, %.1f) -> (%.1f, %.1f)\n", aim2600[0][0], aim2600[0][1],
           aim2800[0][0], aim2800[0][1], aim2600[1][0], aim2600[1][1], aim2800[1][0], aim2800[1][1]);
    CHECK(firstConnect[0] == 700);
    CHECK(firstConnect[1] == 1400);
    CHECK(p1Join == 900);
    CHECK(p1Play == 902);
    CHECK(p2Wait == 1400);
    CHECK(p2Play == 2431);
    // (crosshairs in screen pixels) P2's relative report moved P2's right and up; P1's absolute report moved P1's left
    CHECK(aim2800[1][0] > aim2600[1][0] && aim2800[1][1] != aim2600[1][1]);
    CHECK(aim2800[0][0] < aim2600[0][0]);
    printf("%d / %d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
