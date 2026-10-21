// System-cursor presentation policy only. No SDL window, game assets or input
// devices are required; native cursor changes are tested separately.
#include <cstdio>

#include "port/play/cursor_visibility.h"

namespace {

int checks = 0, failures = 0;

void Check(bool ok, const char* why) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", why);
    }
}

game::WorldHud Playing() {
    game::WorldHud hud;
    hud.gameMode = 6;
    hud.subState = 2;
    hud.canShoot = true;
    hud.player[0].state = 5;
    hud.player[0].lives = 3;
    hud.player[0].inputKind = 1;
    hud.player[0].crosshair = true;
    return hud;
}

void PlayAndCinematics() {
    play::CursorContext context;
    auto hud = Playing();
    Check(!play::SystemCursorVisible(hud, context), "ordinary gameplay hides the system cursor");

    // Letterboxing and shooting permission change throughout ordinary gameplay
    // and cutscenes. Neither should cause the system '+' to reappear.
    for (int box : {0, 1, 2, 4}) {
        for (bool canShoot : {false, true}) {
            hud.letterbox = box;
            hud.canShoot = canShoot;
            hud.skippable = !canShoot;
            hud.flags5900 = canShoot ? 3u : 2u;
            Check(!play::SystemCursorVisible(hud, context), "cutscene/letterbox transitions keep the cursor hidden");
        }
    }

    hud = Playing();
    hud.canShoot = false;
    hud.flags5900 = 0x23;
    Check(!play::SystemCursorVisible(hud, context), "chapter and boss title cards do not expose the cursor");
    context.hordeActive = true;
    hud.flags5900 = 3;
    Check(!play::SystemCursorVisible(hud, context), "Horde fighting hides the cursor");
    hud.flags5900 = 0x23;
    Check(play::SystemCursorVisible(hud, context), "Horde shop restores the pointer despite being in game mode 6");
    context.hordeActive = false;
    Check(!play::SystemCursorVisible(hud, context), "a title card is not mistaken for a Horde shop");
}

void DeviceAndSightIndependence() {
    play::CursorContext context;
    auto hud = Playing();
    // These are logical HUD/device states, not claims of physical-device tests.
    // Mouse/raw guns/touch/Wii share the gun-kind path. The system pointer must
    // stay hidden even when no gameplay sight was submitted for a player.
    for (int inputKind : {-1, 0, 1}) {
        for (bool sights : {false, true}) {
            for (int player : {0, 1}) {
                hud.player[player].inputKind = inputKind;
                hud.player[player].crosshair = sights;
                hud.player[player].crosshairPos[0] = 320;
                hud.player[player].crosshairPos[1] = 240;
                Check(!play::SystemCursorVisible(hud, context), "system pointer hiding is independent of input kind and game sights");
            }
        }
    }

    hud = Playing();
    hud.player[0].state = 4;
    hud.player[0].continueShown = 0x9000;
    hud.player[1].state = 5;
    hud.player[1].lives = 2;
    Check(!play::SystemCursorVisible(hud, context), "P1 continue does not expose a pointer over P2 gameplay");
    hud.player[0].state = 5;
    hud.player[0].continueShown = -1;
    hud.player[1].state = 4;
    hud.player[1].continueShown = 0x8000;
    Check(!play::SystemCursorVisible(hud, context), "P2 continue does not expose a pointer over P1 gameplay");
    hud.player[1].state = 9;
    Check(!play::SystemCursorVisible(hud, context), "an idle player does not expose the system pointer");
}

void InteractiveScreens() {
    play::CursorContext context;
    auto hud = Playing();
    for (int mode : {3, 4, 5, 7, 8, 9, 10, 11, 12, 15, 16}) {
        hud.gameMode = mode;
        Check(play::SystemCursorVisible(hud, context), "front end, attract and non-play modes restore the cursor");
    }
    hud = Playing();
    for (int state : {0, 1, 3, 4, 5, 6, 8, 9, 10, 11}) {
        hud.subState = state;
        Check(play::SystemCursorVisible(hud, context), "whole-game continue, stage transitions and name entry restore the cursor");
    }
    hud = Playing();
    for (unsigned flags : {0x0Bu, 0x13u, 0x1Bu}) {
        hud.flags5900 = flags;
        Check(play::SystemCursorVisible(hud, context),
              "Original inventory, Boss/Scene Select and Training/result pages restore the cursor");
    }
    hud = Playing();
    hud.stageResults.active = true;
    Check(play::SystemCursorVisible(hud, context), "the port stage-results page restores the cursor");
    hud.stageResults.active = false;
    hud.stageResults.shown = true;
    Check(!play::SystemCursorVisible(hud, context), "an already-dismissed results page does not leave the cursor visible");

    hud = Playing();
    for (int page : {0, 1, 2, 3, 4, 5, 6, 7, 8}) {
        hud.nightShiftPage = page;
        Check(play::SystemCursorVisible(hud, context), "a visible Night Shift page restores the cursor");
    }
}

void OverlaysFocusAndTransitions() {
    play::CursorContext context;
    auto hud = Playing();
    hud.paused = true;
    Check(play::SystemCursorVisible(hud, context), "pause restores the cursor before the pause page opens");
    hud.paused = false;
    Check(!play::SystemCursorVisible(hud, context), "resume hides the cursor again");
    context.menuOpen = true;
    Check(play::SystemCursorVisible(hud, context), "graphics, lightgun or Wii setup overlays restore the pointer");
    context.menuOpen = false;
    context.lobbyOpen = true;
    Check(play::SystemCursorVisible(hud, context), "the local lobby restores the pointer independently of world mode");
    context.lobbyOpen = false;
    context.active = false;
    Check(play::SystemCursorVisible(hud, context), "focus loss restores the desktop cursor");
    context.active = true;
    Check(!play::SystemCursorVisible(hud, context), "refocusing live play hides the cursor again");
    hud.paused = true;
    context.active = false;
    Check(play::SystemCursorVisible(hud, context), "an inactive pause screen stays visible");
    context.active = true;
    Check(play::SystemCursorVisible(hud, context), "refocusing a paused game retains a visible pointer");
}

void PresentationOnly() {
    auto hud = Playing();
    hud.player[0].score = 12345;
    hud.player[0].ammo = 4;
    hud.player[0].fired = true;
    hud.player[0].aim[0] = 13.5f;
    hud.player[0].aim[1] = -7.25f;
    hud.player[0].gunX = 46;
    hud.player[0].gunY = -38;
    hud.player[0].gunValid = true;
    hud.menuText.push_back({12, 24, "sentinel", 1, 0xFFFFFFFF});
    const auto before = hud;
    const play::CursorContext context;
    for (int repeat = 0; repeat < 20; ++repeat) (void)play::SystemCursorVisible(hud, context);
    Check(hud.gameMode == before.gameMode && hud.subState == before.subState && hud.flags5900 == before.flags5900 &&
              hud.paused == before.paused && hud.canShoot == before.canShoot,
          "visibility queries preserve world presentation flags");
    Check(hud.player[0].state == before.player[0].state && hud.player[0].lives == before.player[0].lives &&
              hud.player[0].score == before.player[0].score && hud.player[0].ammo == before.player[0].ammo &&
              hud.player[0].fired == before.player[0].fired,
          "visibility queries preserve shooting, score, lives and ammo");
    Check(hud.player[0].aim[0] == before.player[0].aim[0] && hud.player[0].aim[1] == before.player[0].aim[1] &&
              hud.player[0].gunX == before.player[0].gunX && hud.player[0].gunY == before.player[0].gunY &&
              hud.player[0].gunValid == before.player[0].gunValid && hud.player[0].crosshair == before.player[0].crosshair,
          "visibility queries preserve calibrated aim and gameplay sights");
    Check(hud.menuText.size() == before.menuText.size() && hud.menuText[0].text == before.menuText[0].text,
          "visibility queries preserve menu content");
}

}  // namespace

int main() {
    PlayAndCinematics();
    DeviceAndSightIndependence();
    InteractiveScreens();
    OverlaysFocusAndTransitions();
    PresentationOnly();
    std::printf("System cursor policy: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
