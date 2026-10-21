// Port-only crosshair visibility must leave the original ammo/lives HUD and its
// state advancement intact. No game data or renderer is needed.
#include <cstdio>
#include <tuple>
#include <vector>

#include "game/shooting.h"
#include "port/play/gameplay_hud.h"

namespace {

int g_checks = 0, g_failed = 0, g_cases = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        if (g_failed <= 20) std::printf("FAIL case %d: %s\n", g_cases, what);
    }
}

// Explicit comparisons cover all semantic fields, without comparing padding.
auto SpriteFields(const game::SpriteDef& d) {
    return std::make_tuple(d.id, d.x, d.y, d.depth, d.sx, d.sy, d.rot, d.alpha, d.blend, d.flags, d.tinted, d.tint[0], d.tint[1], d.tint[2],
                           d.source);
}

bool EqualSprites(const std::vector<game::SpriteDef>& a, const std::vector<game::SpriteDef>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (SpriteFields(a[i]) != SpriteFields(b[i])) return false;
    return true;
}

auto PlayerFields(const game::HudPlayer& p) {
    return std::make_tuple(p.state, p.lives, p.aimX, p.aimY, p.ammo, p.empty, p.reloadTimer, p.inputKind, p.gunValid, p.gunFired,
                           p.deviceMode, p.gunPosX, p.gunPosY, p.crosshairStyle, p.weaponType, p.weaponAmmo, p.ammoZoom, p.credits,
                           p.coinState);
}

auto StateFields(const game::HudState& s) {
    return std::make_tuple(s.gameMode, s.letterbox, s.canShoot, s.weaponMode, s.torchCounter, s.blinkCounter, s.bootFrames, s.flags5900,
                           s.freePlay, s.perPlayerCredits, s.coinsPerCredit, s.coinsToContinue, s.language, s.continueCount,
                           PlayerFields(s.player[0]), PlayerFields(s.player[1]));
}

game::HudState Playing() {
    game::HudState s;
    s.gameMode = 6;
    s.letterbox = 2;
    s.canShoot = 1;
    s.torchCounter = 39;
    s.bootFrames = 300;
    for (int p = 0; p < 2; ++p) {
        auto& pl = s.player[p];
        pl.state = 5;
        pl.lives = static_cast<int16_t>(3 + p);
        pl.aimX = p ? 130.0f : -125.0f;
        pl.aimY = p ? -40.0f : 46.0f;
        pl.ammo = static_cast<int16_t>(6 - p);
        pl.reloadTimer = static_cast<int16_t>(119 + p);
        pl.inputKind = 0;
        pl.gunValid = 1;
        pl.gunFired = static_cast<int16_t>(p);
        pl.gunPosX = p ? 470 : 150;
        pl.gunPosY = p ? 280 : 140;
        pl.crosshairStyle = static_cast<int8_t>(p);
        pl.ammoZoom = 7.5f + p;
        pl.credits = 3 + p;
        pl.coinState = 2;
    }
    return s;
}

void InputProfile(game::HudPlayer& pl, int profile) {
    pl.deviceMode = 0;
    pl.inputKind = 0;
    pl.gunValid = 1;
    switch (profile) {
        case 1:
            pl.gunValid = 0;
            break;  // Pad cursor outside valid aim.
        case 2:
            pl.inputKind = 1;
            break;  // Non-gun device with gun input kind.
        case 3:
            pl.deviceMode = 5;
            pl.inputKind = 1;
            break;  // Native gun devices use raw position.
        case 4:
            pl.deviceMode = 6;
            pl.inputKind = 1;
            pl.gunValid = 0;
            break;
        case 5:
            pl.deviceMode = game::kPortWideGunMode;
            pl.inputKind = 1;
            pl.gunPosX = game::kPortWideGunBias - 450;
            pl.gunPosY = 37;
            break;
        case 6:
            pl.inputKind = -1;
            break;  // Preserve original inactive-device behavior.
        default:
            break;
    }
}

void Verify(const game::HudState& input, int player) {
    ++g_cases;
    const auto crosshair = game::HudCrosshair(input, player);
    Check(crosshair.size() <= 1, "documented crosshair prefix has at most one sprite");
    auto baseline = input, visibleState = input, hiddenState = input;
    const auto original = game::HudPlayerTask(baseline, player);
    const auto visible = play::PlayerHudSprites(visibleState, player, true);
    const auto hidden = play::PlayerHudSprites(hiddenState, player, false);
    Check(EqualSprites(visible, original), "visible HUD exactly matches original sprites and order");
    Check(StateFields(visibleState) == StateFields(baseline), "visible HUD exactly matches original state writes");

    const bool hasPrefix = original.size() >= crosshair.size();
    Check(hasPrefix, "original HUD contains its documented crosshair prefix");
    if (hasPrefix) {
        const std::vector<game::SpriteDef> prefix(original.begin(), original.begin() + crosshair.size());
        const std::vector<game::SpriteDef> remainder(original.begin() + crosshair.size(), original.end());
        Check(EqualSprites(prefix, crosshair), "crosshair is the original HUD prefix before any ammo state advancement");
        Check(EqualSprites(hidden, remainder), "hidden HUD removes only the aim sprite and preserves every other field/order");
    }
    Check(StateFields(hiddenState) == StateFields(baseline), "hidden HUD preserves complete original state writes");
    for (int p = 0; p < 2; ++p) {
        Check(hiddenState.player[p].reloadTimer == baseline.player[p].reloadTimer, "reload blink/hint timer matches for each player");
        Check(hiddenState.player[p].ammoZoom == baseline.player[p].ammoZoom, "ammo letterbox animation matches for each player");
        if (p != player)
            Check(PlayerFields(hiddenState.player[p]) == PlayerFields(input.player[p]), "other player's HUD state remains untouched");
    }
    Check(EqualSprites(game::HudCrosshair(input, player), crosshair), "direct menu/calibration crosshair drawing remains available");
}

bool HasId(const std::vector<game::SpriteDef>& sprites, int id) {
    for (const auto& sprite : sprites)
        if (sprite.id == id) return true;
    return false;
}

void VisibilityAndInputCases() {
    for (int p = 0; p < 2; ++p)
        for (int style = 0; style < 4; ++style)
            for (int profile = 0; profile < 7; ++profile) {
                auto s = Playing();
                s.player[p].crosshairStyle = static_cast<int8_t>(style);
                InputProfile(s.player[p], profile);
                Verify(s, p);
            }

    // P1, P2 and two simultaneous players must all retain their own non-aim HUD.
    for (int activeMask : {1, 2, 3})
        for (int style = 0; style < 4; ++style)
            for (int profile = 0; profile < 7; ++profile) {
                auto baseline = Playing();
                for (int p = 0; p < 2; ++p) {
                    baseline.player[p].crosshairStyle = static_cast<int8_t>(style);
                    InputProfile(baseline.player[p], profile);
                    baseline.player[p].empty = 1;
                    baseline.player[p].ammo = 0;
                    baseline.player[p].reloadTimer = 120;
                    if (!(activeMask & (1 << p))) baseline.player[p].state = 9;
                }
                auto visibleState = baseline, hiddenState = baseline;
                std::vector<game::SpriteDef> expectedVisible, expectedHidden, visible, hidden;
                for (int p = 0; p < 2; ++p) {
                    if (!(activeMask & (1 << p))) continue;
                    const auto count = game::HudCrosshair(baseline, p).size();
                    const auto original = game::HudPlayerTask(baseline, p);
                    expectedVisible.insert(expectedVisible.end(), original.begin(), original.end());
                    expectedHidden.insert(expectedHidden.end(), original.begin() + count, original.end());
                    const auto on = play::PlayerHudSprites(visibleState, p, true);
                    const auto off = play::PlayerHudSprites(hiddenState, p, false);
                    visible.insert(visible.end(), on.begin(), on.end());
                    hidden.insert(hidden.end(), off.begin(), off.end());
                }
                ++g_cases;
                Check(EqualSprites(visible, expectedVisible), "one/two-player visible HUD matches original task order");
                Check(EqualSprites(hidden, expectedHidden), "one/two-player hidden HUD retains both players' reload/lives HUD");
                Check(StateFields(visibleState) == StateFields(baseline), "one/two-player visible HUD advancement matches");
                Check(StateFields(hiddenState) == StateFields(baseline), "one/two-player hidden HUD advancement matches");
            }
}

void HudBoundaryCases() {
    for (int p = 0; p < 2; ++p)
        for (int mode : {0, 3, 4, 5, 6, 9, 12})
            for (int canShoot : {0, 1})
                for (int lives : {-1, 0, 1, 7})
                    for (int bars : {0, 1, 2, 3, 4, 5, 6})
                        for (int weaponMode : {0, 1, 2, 3})
                            for (int timer : {0, 44, 45, 59, 119, 120, 600, 601}) {
                                auto s = Playing();
                                s.gameMode = mode;
                                s.canShoot = canShoot;
                                s.letterbox = static_cast<int8_t>(bars);
                                s.weaponMode = weaponMode;
                                s.player[p].lives = static_cast<int16_t>(lives);
                                s.player[p].empty = 1;
                                s.player[p].ammo = 0;
                                s.player[p].reloadTimer = static_cast<int16_t>(timer);
                                Verify(s, p);
                            }

    for (int p = 0; p < 2; ++p)
        for (int type = 0; type < 8; ++type)
            for (int ammo : {0, 1, 6, 7, 12})
                for (int weaponAmmo : {-1, 0, 12})
                    for (int bars : {1, 2, 4}) {
                        auto s = Playing();
                        s.weaponMode = 1;
                        s.letterbox = static_cast<int8_t>(bars);
                        s.player[p].weaponType = static_cast<int8_t>(type);
                        s.player[p].weaponAmmo = static_cast<int8_t>(weaponAmmo);
                        s.player[p].ammo = static_cast<int16_t>(ammo);
                        Verify(s, p);
                    }
}

void EssentialHudAndCalibrationCases() {
    for (int p = 0; p < 2; ++p) {
        auto s = Playing();
        s.player[p].ammo = 0;
        s.player[p].empty = 1;
        s.player[p].reloadTimer = 120;
        for (int inputKind : {0, 1}) {
            s.player[p].inputKind = static_cast<int8_t>(inputKind);
            s.player[p].deviceMode = inputKind == 1 ? 6 : 0;
            auto hiddenState = s;
            const auto hidden = play::PlayerHudSprites(hiddenState, p, false);
            ++g_cases;
            Check(HasId(hidden, 0xA27), "RELOAD prompt stays visible with crosshairs hidden");
            Check(HasId(hidden, inputKind == 1 ? 0xA29 : 0xA28), "correct gun/pad reload hint stays visible");
            Check(HasId(hidden, p == 0 ? 0x5B9 : 0x5BA), "player's life label stays visible");
            Check(hiddenState.player[p].reloadTimer == 121, "hidden reload prompt still advances its timer");
        }
        s.player[p].empty = 0;
        s.player[p].ammo = 6;
        auto hiddenState = s;
        Check(HasId(play::PlayerHudSprites(hiddenState, p, false), 0xA74), "ammo bullets stay visible");

        // The gameplay preference must not rewrite the shared crosshair function:
        // calibration and pointer menus request that function directly.
        const auto directBefore = game::HudCrosshair(s, p);
        Check(directBefore.size() == 1, "calibration/menu crosshair is available before hidden gameplay rendering");
        for (int frame = 0; frame < 4; ++frame) (void)play::PlayerHudSprites(hiddenState, p, false);
        Check(EqualSprites(game::HudCrosshair(s, p), directBefore), "hidden gameplay leaves direct calibration/menu crosshair unchanged");
        for (int mode : {3, 4, 5, 9, 12}) {
            s.gameMode = mode;
            Verify(s, p);
            auto menuState = s;
            Check(EqualSprites(play::PlayerHudSprites(menuState, p, false), game::HudPlayerTask(s, p)),
                  "outside gameplay the preference preserves the original full player HUD");
        }
    }
}

}  // namespace

int main() {
    VisibilityAndInputCases();
    HudBoundaryCases();
    EssentialHudAndCalibrationCases();
    std::printf("Crosshair visibility: %d/%d checks passed across %d HUD cases\n", g_checks - g_failed, g_checks, g_cases);
    return g_failed ? 1 : 0;
}
