// Optional score rendering must not change the original HUD or its animation state.
// These checks use sprite definitions only and need no original game assets.
#include <climits>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

#include "game/hud.h"

namespace {

int checks = 0, failures = 0;

void Check(bool ok, const char* description) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", description);
    }
}

auto Fields(const game::SpriteDef& d) {
    return std::make_tuple(d.id, d.x, d.y, d.depth, d.sx, d.sy, d.rot, d.alpha, d.blend, d.flags,
                           d.tinted, d.tint[0], d.tint[1], d.tint[2], d.source);
}

bool Equal(const std::vector<game::SpriteDef>& a, const std::vector<game::SpriteDef>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (Fields(a[i]) != Fields(b[i])) return false;
    return true;
}

game::HudState Playing() {
    game::HudState s;
    s.gameMode = 6;
    s.canShoot = 1;
    s.showScores = true;
    for (int p = 0; p < 2; ++p) {
        s.player[p].state = 5;
        s.player[p].lives = 3;
        s.player[p].ammo = 6;
        s.player[p].gunValid = 1;
        s.player[p].inputKind = 0;
        s.player[p].score = p == 0 ? 123450 : 98760;
    }
    return s;
}

void NumericScores() {
    const int values[] = {0, 7, 123450, 987654321, INT_MAX, -1, INT_MIN};
    const char* expected[] = {"0", "7", "123450", "987654321", "2147483647", "0", "0"};
    for (int p = 0; p < 2; ++p) {
        for (size_t n = 0; n < sizeof(values) / sizeof(values[0]); ++n) {
            auto s = Playing();
            s.player[p].score = values[n];
            const auto sprites = game::HudScore(s, p);
            std::string digits;
            for (const auto& d : sprites) {
                Check(d.id >= 0x59 && d.id <= 0x62, "score uses original numeric art");
                digits.push_back(static_cast<char>('0' + d.id - 0x59));
                Check(d.y == 8 && d.sx == d.sy && d.sx == 0.85f, "score uses readable upper-corner glyphs");
                Check(d.x >= 24 && d.x <= 616, "score anchors stay inside screen margins");
                Check((d.flags & 15) == (p == 0 ? 5u : 7u), "scores use left/right top anchoring");
                Check(p == 0 ? d.x <= 132 : d.x >= 508, "score avoids central boss and round UI");
            }
            Check(digits == expected[n], "score digits preserve full value and clamp invalid negative scores");
            Check(!sprites.empty() && (p == 0 ? sprites.front().x == 24 : sprites.back().x == 616),
                  "score grows inward from its own corner");
        }
    }
}

void Visibility() {
    Check(!game::HudState{}.showScores, "score display is off by default");
    for (int p = 0; p < 2; ++p) {
        auto s = Playing();
        Check(!game::HudScore(s, p).empty(), "active player score is visible during gameplay");
        s.showScores = false;
        Check(game::HudScore(s, p).empty(), "disabled score is invisible");
        for (int mode : {0, 3, 5, 7, 9, 12}) {
            s = Playing();
            s.gameMode = mode;
            Check(game::HudScore(s, p).empty(), "score is hidden outside gameplay");
        }
        for (int letterbox : {1, 3, 4}) {
            s = Playing();
            s.letterbox = static_cast<int8_t>(letterbox);
            Check(game::HudScore(s, p).empty(), "score does not cover letterboxed scenes");
        }
        for (int state : {4, 6, 9, 10}) {
            s = Playing();
            s.player[p].state = static_cast<int16_t>(state);
            Check(game::HudScore(s, p).empty(), "continue/idle/waiting/out players have no score HUD");
            Check(!game::HudScore(s, 1 - p).empty(), "other active player keeps their score");
        }
        s = Playing();
        s.player[p].lives = 0;
        Check(game::HudScore(s, p).empty(), "dead player score is hidden");
        s = Playing();
        s.canShoot = 0;
        Check(game::HudScore(s, p).empty(), "score is hidden during non-shooting scenes");
    }
}

void OriginalHudPreserved() {
    for (int p = 0; p < 2; ++p) {
        for (int letterbox : {1, 2, 3, 4}) {
            auto off = Playing();
            off.showScores = false;
            off.letterbox = static_cast<int8_t>(letterbox);
            off.player[p].empty = 1;
            off.player[p].ammo = 0;
            off.player[p].reloadTimer = 120;
            off.player[p].ammoZoom = 5;
            auto on = off;
            on.showScores = true;
            const auto score = game::HudScore(on, p);
            auto expected = game::HudCrosshair(off, p);
            const auto ammo = game::HudAmmo(off, p), lives = game::HudLives(off, p);
            expected.insert(expected.end(), ammo.begin(), ammo.end());
            expected.insert(expected.end(), lives.begin(), lives.end());
            Check(Equal(game::HudPlayerTask(off, p), expected), "disabled HUD exactly preserves native sprite fields/order");
            expected.insert(expected.end(), score.begin(), score.end());
            Check(Equal(game::HudPlayerTask(on, p), expected), "enabled HUD only appends optional score sprites");
            for (int q = 0; q < 2; ++q) {
                Check(on.player[q].reloadTimer == off.player[q].reloadTimer, "score does not affect reload animation");
                Check(on.player[q].ammoZoom == off.player[q].ammoZoom, "score does not affect ammo animation");
                Check(on.player[q].score == off.player[q].score, "rendering does not alter gameplay scores");
            }
        }
    }
}

}  // namespace

int main() {
    NumericScores();
    Visibility();
    OriginalHudPreserved();
    std::printf("score_hud_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
