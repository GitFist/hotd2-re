// Exercise menu selection against the submitted label positions, including the bottom of visible capitals.
// The original font textures have ink at rows 12..32; using their texture origins as hitbox origins was wrong.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "game/world.h"

static void Require(bool ok, const char* why) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static void Step(game::World& w, int x, int y, uint32_t pressed = 0, bool click = false) {
    game::WorldInput in;
    in.kind = 1; in.x = x; in.y = y;
    in.pressed = in.held = pressed;
    in.fired = click;
    w.SetInput(0, in);
    w.SetInput(1, {});
    w.Step();
}
static void Menu(game::World& w, bool modern = true) {
    game::WorldConfig cfg;
    cfg.portFeatures = cfg.portModernMenu = modern;
    w.Boot(cfg);
    while (w.Frame() < 650) Step(w, 600, 200);
    Step(w, 600, 200, 8);
    for (int i = 0; i < 4; ++i) Step(w, 600, 200);
    Require(w.GameMode() == 4, "boot and enter main menu");
}
static std::vector<game::SpriteDef> Labels(const game::World& w) {
    std::vector<game::SpriteDef> out;
    // The first capital of each left-aligned label. Positions come from the actual frame's draw submissions.
    for (const auto& d : w.Hud().sprites)
        if (d.x == 24 && d.sx == 1.25f && d.tinted) out.push_back(d);
    Require(out.size() == 11, "all eleven labels rendered");
    return out;
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    game::World w;
    std::string error;
    Require(w.Load(argv[1], 0, &error), error.c_str());
    Menu(w);
    const auto labels = Labels(w);
    auto x = [&](int row) { return int(std::lround(labels[row].x + 7 * labels[row].sx)); };
    auto y = [&](int row, int texel = 22) { return int(std::lround(labels[row].y + texel * labels[row].sy)); };
    for (int direction : {1, -1}) {
        for (int n = 0; n < 11; ++n) {
            const int row = direction == 1 ? n : 10 - n;
            for (int inkRow : {13, 22, 30}) {
                Step(w, x(row), y(row, inkRow));
                Require(w.Hud().menuCursor == row, "hover top/centre/bottom of rendered capital selects its label");
                const auto drawn = Labels(w);
                for (int i = 0; i < 11; ++i)
                    Require(drawn[i].tint[0] == (i == row ? 1.0f : 0.6f), "visible highlight agrees with hover");
            }
        }
    }
    // The final option was especially hard to hit: its bottom used to fall outside every row.
    Step(w, x(10), y(10, 30));
    Require(w.Hud().menuCursor == 10 && !w.QuitRequested(), "hover Exit's visible bottom without activating it");
    for (const auto& p : {std::pair<int, int>{600, 200}, {40, 145}, {40, 479}}) {
        Step(w, p.first, p.second, 4, true);
        Require(w.GameMode() == 4 && !w.QuitRequested(), "click outside labels cannot activate the selected option");
    }
    Step(w, x(5), y(5));
    Step(w, x(5), y(5), 0x20);
    Step(w, x(5), y(5));
    Require(w.Hud().menuCursor == 6, "stationary pointer preserves keyboard selection");
    Step(w, x(5) + 1, y(5));
    Require(w.Hud().menuCursor == 5, "moving within the same row restores pointer selection");
    Step(w, x(5) + 1, y(5), 0x20);
    Step(w, x(5) + 1, y(5), 8);
    Step(w, x(5) + 1, y(5));
    Require(w.GameMode() == 15, "Enter confirms keyboard-selected Network, despite pointer resting over Options");
    Menu(w);
    Step(w, x(5), y(5));
    Step(w, x(5), y(5), 0x20);
    Step(w, x(5), y(5), 4, true);
    Step(w, x(5), y(5));
    Require(w.GameMode() == 12, "click confirms pointed-at Options, despite keyboard-selected Network");
    Menu(w);
    Step(w, 600, 200, 0x10);
    Require(w.Hud().menuCursor == 10, "Up wraps to Exit");
    Step(w, 600, 200, 0x20);
    Require(w.Hud().menuCursor == 0, "Down wraps to Arcade");
    Menu(w, false);
    Step(w, x(6), y(6));
    Require(w.Hud().menuCursor == 0, "original menu remains keyboard-driven");
    puts("PASS: 66 label positions, matching highlights, blank-space clicks, mixed mouse/keyboard, wrapping and original menu");
}
