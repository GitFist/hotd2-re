// Keyboard repeat/consumption and actual title/Classic/Night Shift menu routing.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "game/world.h"
#include "port/play/keyboard.h"
#include "port/play/score_display_code.h"

namespace {
constexpr uint32_t left = 0x40, right = 0x80, enter = 8;
constexpr uint32_t sequence[] = {left, left, right, right, right, left, right};
int checks = 0;
void Require(bool ok, const char* reason) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", reason);
        std::exit(1);
    }
}

void Arrows(play::ScoreDisplayCode& code) {
    for (auto key : sequence) {
        Require(code.Update(key, true).keys == key, "arrows remain available for normal menus");
        code.Update(0, true);
    }
}

void Keyboard() {
    play::ScoreDisplayCode code;
    Require(!code.Enabled(), "scores default off");
    Arrows(code);
    Require(!code.Enabled(), "arrows alone do not toggle");
    auto input = code.Update(enter, true);
    Require(input.toggled && !input.keys && code.Enabled(), "exact code and Enter enable scores and consume Enter");
    for (int i = 0; i < 60; ++i) {
        input = code.Update(enter, i < 30);
        Require(!input.toggled && !input.keys && code.Enabled(), "held/repeated Enter cannot toggle or leak after leaving menu");
    }
    code.Update(0, true);
    Require(code.Update(enter, true).keys == enter, "fresh ordinary Enter works after release");
    code.Update(0, true);
    Arrows(code);
    Require(code.Update(enter, true).toggled && !code.Enabled(), "repeat code disables scores");

    // A held arrow produces one press, regardless of OS key-repeat frequency.
    code = {};
    for (int i = 0; i < 20; ++i) code.Update(left, true);
    code.Update(0, true);
    for (auto key : {right, right, right, left, right}) {
        code.Update(key, true);
        code.Update(0, true);
    }
    input = code.Update(enter, true);
    Require(!input.toggled && input.keys == enter, "held Left cannot stand in for two Left taps");

    for (uint32_t interrupt : {0x10u, 0x20u, 4u, 2u, play::kEscapeKey, left | right, enter | left}) {
        code = {};
        Arrows(code);
        code.Update(interrupt, true);
        code.Update(0, true);
        input = code.Update(enter, true);
        Require(!input.toggled && input.keys == enter, "wrong key or chord cancels code without swallowing regular Enter");
    }
    code = {};
    Arrows(code);
    code.Update(0, false);
    Require(!code.Update(enter, true).toggled, "leaving eligible screens clears partial code");
    code.Update(0, true);
    Arrows(code);
    code.ResetSequence();
    Require(!code.Update(enter, true).toggled, "focus loss clears partial code");
    code = {};
    for (auto key : sequence) {
        code.Update(key, false);
        code.Update(0, false);
    }
    Require(!code.Update(enter, false).toggled && !code.Enabled(), "gameplay input cannot toggle scores");
    code = {};
    code.Update(right, true);
    code.Update(0, true);
    Arrows(code);
    Require(code.Update(enter, true).toggled, "full retry succeeds after a wrong leading arrow");
    for (int page : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10})
        Require(!play::ScoreDisplayCode::FrontEnd(4, page), "code excluded from submenu input");
}

void Menus(const char* dir) {
    game::World world;
    std::string error;
    Require(world.Load(dir, 0, &error), error.c_str());
    for (int mode : {3, 4}) {
        for (bool modern : {false, true}) {
            game::WorldConfig cfg;
            cfg.portFeatures = true;
            cfg.portModernMenu = modern;
            world.Boot(cfg);
            world.ForceMode(mode, 0x10000, 0, 0);
            play::ScoreDisplayCode code;
            uint32_t previous = 0;
            auto step = [&](uint32_t keys) {
                const auto filtered = code.Update(keys, play::ScoreDisplayCode::FrontEnd(world.GameMode(), world.Hud().nightShiftPage));
                game::WorldInput input;
                input.kind = 1;
                input.x = 630;
                input.y = 465;
                input.held = play::WorldKeyboardBits(filtered.keys, false, false);
                input.pressed = play::WorldKeyboardBits(filtered.keys & ~previous, false, false);
                previous = keys;
                world.SetInput(0, input);
                world.SetInput(1, {});
                world.Step();
            };
            for (int i = 0; i < 4; ++i) step(0);
            Require(world.GameMode() == mode, "fixture entered requested front end");
            const int page = world.Hud().nightShiftPage;
            for (int pass = 0; pass < 2; ++pass) {
                for (auto key : sequence) {
                    step(key);
                    step(0);
                }
                for (int i = 0; i < 20; ++i) step(enter);
                Require(code.Enabled() == (pass == 0), "actual front end enables then disables score display");
                Require(world.GameMode() == mode && world.Hud().nightShiftPage == page,
                        "confirming/held Enter leaves title or main menu in place");
                step(0);
            }
            step(enter);
            for (int i = 0; i < 3; ++i) step(0);
            Require(world.GameMode() != mode || world.Hud().nightShiftPage != page,
                    "subsequent regular Enter still starts or selects normally");
        }
    }
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    Keyboard();
    Menus(argv[1]);
    std::printf("score_display_code_test: %d checks passed\n", checks);
    return 0;
}
