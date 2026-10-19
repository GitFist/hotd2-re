// play.exe's pause menu (src/port/play/pause_menu.h): navigation with the pad bits and with a pointer, the volume
// rows, the display settings page (display_menu.h), the Return to Main Menu confirmation, and the settings file round
// trip. No game data needed.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "port/play/pause_menu.h"

namespace {

int g_checks = 0, g_failed = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        printf("FAIL: %s\n", what);
    }
}

using play::PauseMenu;
using Action = play::PauseMenu::Action;

PauseMenu::Input Pad(uint32_t bits) {
    PauseMenu::Input in;
    in.pressed = bits;
    return in;
}

PauseMenu::Input Point(float px, float py, bool trigger = false, bool reload = false) {
    PauseMenu::Input in;
    in.pointer = true;
    in.px = px;
    in.py = py;
    in.trigger = trigger;
    in.reload = reload;
    return in;
}

}  // namespace

int main() {
    play::Settings v;
    std::vector<uint32_t> snd;

    // keyboard: Down through every row wraps back to Resume, Up goes to the last row
    PauseMenu m;
    m.Open();
    Check(m.IsOpen() && m.Cursor() == PauseMenu::kResume, "opens on Resume");
    for (int i = 0; i < PauseMenu::kRowCount; ++i) m.Update(Pad(0x20), v, snd);
    Check(m.Cursor() == PauseMenu::kResume, "Down wraps");
    m.Update(Pad(0x10), v, snd);
    Check(m.Cursor() == PauseMenu::kReturn, "Up wraps to Return");
    Check(snd.size() == PauseMenu::kRowCount + 1, "a cursor sound per move");

    m.Open();
    for (int i = 0; i < PauseMenu::kWii; ++i) m.Update(Pad(0x20), v, snd);
    Check(m.Update(Pad(8), v, snd) == Action::WiiRemotes && m.IsOpen(), "Wii setup opens while the game remains paused");
    // volumes: Right / Left on the Music row, clamped to 0..10
    m.Open();
    m.Update(Pad(0x20), v, snd);
    Check(m.Cursor() == PauseMenu::kMusic, "Down to Music");
    Check(m.Update(Pad(0x80), v, snd) == Action::None && v.music == 10, "Right at 10 stays 10");
    m.Update(Pad(0x40), v, snd);
    m.Update(Pad(0x40), v, snd);
    Check(v.music == 8, "Left twice: 8");
    for (int i = 0; i < 20; ++i) m.Update(Pad(0x40), v, snd);
    Check(v.music == 0, "Left clamps at 0");
    Check(v.guns == 10 && v.sfx == 10, "other volumes untouched");

    // Enter on Resume; Backspace anywhere resumes
    m.Open();
    Check(m.Update(Pad(0x8), v, snd) == Action::Resume, "Enter on Resume resumes");
    m.Open();
    m.Update(Pad(0x20), v, snd);
    Check(m.Update(Pad(0x2), v, snd) == Action::Resume, "Backspace resumes");

    // Return to Main Menu: confirmation defaults to No; Enter on No returns to the menu; Yes returns to the title
    m.Open();
    m.Update(Pad(0x10), v, snd);  // Up from Resume = Return
    Check(m.Update(Pad(0x4), v, snd) == Action::None && m.Confirming() && !m.ConfirmYes(), "Return asks, No selected");
    Check(m.Update(Pad(0x8), v, snd) == Action::None && !m.Confirming(), "Enter on No goes back");
    m.Update(Pad(0x4), v, snd);
    m.Update(Pad(0x40), v, snd);
    Check(m.Confirming() && m.ConfirmYes(), "Left toggles to Yes");
    Check(m.Update(Pad(0x8), v, snd) == Action::ReturnToTitle, "Enter on Yes returns to the title");
    m.Open();
    m.Update(Pad(0x10), v, snd);
    m.Update(Pad(0x4), v, snd);
    Check(m.Update(Pad(0x2), v, snd) == Action::None && !m.Confirming() && m.IsOpen(), "Backspace cancels the confirmation");

    // pointer: aiming at a row highlights it; trigger on a volume row +1, reload -1; trigger on Return asks
    m.Open();
    v = play::Settings{};
    const float rowGuns = (PauseMenu::RowY(PauseMenu::kGuns) + 0.5f) * 24.0f;
    m.Update(Point(300.0f, rowGuns), v, snd);
    Check(m.Cursor() == PauseMenu::kGuns, "pointer highlights Gun Shots");
    m.Update(Point(300.0f, rowGuns, false, true), v, snd);
    Check(v.guns == 9, "reload on a volume row: -1");
    m.Update(Point(300.0f, rowGuns, true), v, snd);
    Check(v.guns == 10, "trigger on a volume row: +1");
    const float rowReturn = (PauseMenu::RowY(PauseMenu::kReturn) + 0.5f) * 24.0f;
    m.Update(Point(300.0f, rowReturn), v, snd);
    Check(m.Update(Point(300.0f, rowReturn, true), v, snd) == Action::None && m.Confirming(), "trigger on Return asks");
    const float rowChoice = 10.5f * 24.0f;
    m.Update(Point(12.0f * 16.0f + 20.0f, rowChoice), v, snd);
    Check(m.ConfirmYes(), "pointer over Yes");
    Check(m.Update(Point(12.0f * 16.0f + 20.0f, rowChoice, true), v, snd) == Action::ReturnToTitle, "trigger on Yes");
    Check(PauseMenu::RowAt(100.0f, 2.0f) == -1 && PauseMenu::ConfirmAt(0.0f, rowChoice) == -1, "nothing under the pointer elsewhere");

    // start button = resume
    m.Open();
    PauseMenu::Input st;
    st.start = true;
    Check(m.Update(st, v, snd) == Action::Resume, "start resumes");

    // the Display row opens the display page; its rows cycle their values; Back returns to the menu rows
    using play::DisplayMenu;
    m.Open();
    v = play::Settings{};
    for (int i = 0; i < 4; ++i) m.Update(Pad(0x20), v, snd);
    Check(m.Cursor() == PauseMenu::kDisplay, "Down x4 reaches Display");
    Check(m.Update(Pad(0x8), v, snd) == Action::None && m.DisplayOpen() && !m.MainRows(), "Enter on Display opens the page");
    Check(m.Display().Cursor() == DisplayMenu::kFullscreen, "page opens on Fullscreen");
    m.Update(Pad(0x80), v, snd);
    Check(v.display.fullscreen, "Right toggles Fullscreen on");
    Check(DisplayMenu::ValueText(v.display, DisplayMenu::kScale).empty(), "window scale hidden in fullscreen");
    m.Update(Pad(0x8), v, snd);
    Check(!v.display.fullscreen, "Enter on a value: the next one (Fullscreen off again)");
    m.Update(Pad(0x20), v, snd);  // Window Scale
    Check(m.Display().Cursor() == DisplayMenu::kScale && v.display.windowScale == 2, "Window Scale starts at 2x");
    m.Update(Pad(0x80), v, snd);
    Check(v.display.windowScale == 3, "Right: 3x");
    m.Update(Pad(0x80), v, snd);
    Check(v.display.windowScale == 0 && DisplayMenu::ValueText(v.display, DisplayMenu::kScale) == "Fit", "Right: Fit");
    m.Update(Pad(0x80), v, snd);
    Check(v.display.windowScale == 1, "Right wraps to 1x");
    m.Update(Pad(0x40), v, snd);
    Check(v.display.windowScale == 0, "Left wraps back to Fit");
    m.Update(Pad(0x20), v, snd);  // Aspect
    m.Update(Pad(0x80), v, snd);
    Check(v.display.stretch && DisplayMenu::ValueText(v.display, DisplayMenu::kAspect) == "Stretch", "Aspect: Stretch");
    m.Update(Pad(0x20), v, snd);  // CRT
    m.Update(Pad(0x40), v, snd);
    Check(v.display.crt == 3, "CRT: Left from Off wraps to Scanlines 3");
    m.Update(Pad(0x80), v, snd);
    Check(v.display.crt == 0, "CRT: Right wraps to Off");
    m.Update(Pad(0x20), v, snd);  // Render Scale
    Check(play::Settings{}.display.renderScale == 2, "default render scale 2x (the 2x window's own pixels)");
    v.display.renderScale = 1;  // the stepping is checked from 1x
    for (int i = 0; i < 3; ++i) m.Update(Pad(0x80), v, snd);
    Check(v.display.renderScale == 4 && DisplayMenu::ValueText(v.display, DisplayMenu::kRender) == "4x", "Render Scale: 4x");
    m.Update(Pad(0x80), v, snd);
    Check(v.display.renderScale == 1, "Render Scale wraps to 1x");
    m.Update(Pad(0x20), v, snd);  // Back
    Check(m.Display().Cursor() == DisplayMenu::kBack, "Down reaches Back");
    Check(m.Update(Pad(0x8), v, snd) == Action::None && !m.DisplayOpen() && m.IsOpen() && m.MainRows() && m.Cursor() == PauseMenu::kDisplay,
          "Enter on Back closes the page, the menu stays on Display");
    // Backspace on the page closes it too (not the whole menu); start on the page resumes
    m.Update(Pad(0x8), v, snd);
    Check(m.DisplayOpen(), "reopened");
    Check(m.Update(Pad(0x2), v, snd) == Action::None && !m.DisplayOpen() && m.IsOpen(), "Backspace closes the page only");
    m.Update(Pad(0x8), v, snd);
    Check(m.Update(st, v, snd) == Action::Resume && !m.DisplayOpen(), "start on the page resumes");
    // pointer on the page: aiming highlights, trigger = next value, reload = previous; trigger on Back closes
    m.Open();
    v = play::Settings{};
    const float rowDisplay = (PauseMenu::RowY(PauseMenu::kDisplay) + 0.5f) * 24.0f;
    m.Update(Point(300.0f, rowDisplay), v, snd);
    m.Update(Point(300.0f, rowDisplay, true), v, snd);
    Check(m.DisplayOpen(), "trigger on Display opens the page");
    const float rowRender = (DisplayMenu::RowY(DisplayMenu::kRender) + 0.5f) * 24.0f;
    m.Update(Point(300.0f, rowRender), v, snd);
    Check(m.Display().Cursor() == DisplayMenu::kRender, "pointer highlights Render Scale");
    v.display.renderScale = 1;  // the stepping is checked from 1x
    m.Update(Point(300.0f, rowRender, true), v, snd);
    Check(v.display.renderScale == 2, "trigger: 2x");
    m.Update(Point(300.0f, rowRender, false, true), v, snd);
    Check(v.display.renderScale == 1, "reload: back to 1x");
    const float rowBack = (DisplayMenu::RowY(DisplayMenu::kBack) + 0.5f) * 24.0f;
    m.Update(Point(300.0f, rowBack), v, snd);
    m.Update(Point(300.0f, rowBack, true), v, snd);
    Check(!m.DisplayOpen() && m.IsOpen(), "trigger on Back closes the page");
    Check(DisplayMenu::RowAt(100.0f, 2.0f) == -1, "nothing under the pointer at the heading");
    // the standalone page (the Options screen's Display Settings row): Update reports the close
    DisplayMenu page;
    page.Open();
    Check(page.IsOpen() && !page.Update(Pad(0x20), v.display, snd) && page.Update(Pad(0x2), v.display, snd) && !page.IsOpen(), "standalone page: Backspace closes");

    // Visible letter bottoms used to fall in the next row's overlapping hitbox.
    for (int r = 0; r < DisplayMenu::kRowCount; ++r)
        for (int ink : {13, 22, 30})
            Check(DisplayMenu::RowAt(40, DisplayMenu::RowY(r) * 24 + ink) == r, "display hitbox covers rendered letter body");
    for (int r = 0; r < PauseMenu::kRowCount; ++r)
        for (int ink : {13, 22, 30})
            Check(PauseMenu::RowAt(40, PauseMenu::RowY(r) * 24 + ink) == r, "pause hitbox covers rendered letter body");
    Check(PauseMenu::ConfirmAt(210, 10 * 24 + 30) == 1 && PauseMenu::ConfirmAt(400, 10 * 24 + 30) == 0,
          "Yes/No hitboxes include bottom of text");
    page.Open();
    v = play::Settings{};
    const float crtY = DisplayMenu::RowY(DisplayMenu::kCrt) * 24 + 22;
    page.Update(Point(40, crtY), v.display, snd);
    auto mixed = Point(40, crtY);
    mixed.pressed = 0x20;
    page.Update(mixed, v.display, snd);
    page.Update(Point(40, crtY), v.display, snd);
    Check(page.Cursor() == DisplayMenu::kRender, "resting pointer does not undo keyboard on display page");
    mixed.pressed = 8;
    page.Update(mixed, v.display, snd);
    Check(v.display.renderScale == 3 && v.display.crt == 0, "Enter changes keyboard-selected display value");
    page.Update(Point(41, crtY), v.display, snd);
    Check(page.Cursor() == DisplayMenu::kCrt, "movement inside same row restores display hover");
    page.Update(Point(360, crtY, true), v.display, snd);
    Check(v.display.crt == 3, "shoot display left arrow steps backwards");
    page.Update(Point(620, crtY, true), v.display, snd);
    Check(v.display.crt == 0, "shoot display right arrow steps forwards");
    const auto displayBeforeMiss = v.display;
    page.Update(Point(5, 450, true), v.display, snd);
    Check(page.IsOpen() && v.display == displayBeforeMiss, "blank-space shot does not change display settings");
    v.display.fullscreen = true;
    page.Update(Point(620, DisplayMenu::RowY(DisplayMenu::kScale) * 24 + 22, true), v.display, snd);
    Check(v.display.windowScale == 2, "disabled fullscreen window scale ignores shots");
    m.Open();
    v.music = 5;
    const float musicY = PauseMenu::RowY(PauseMenu::kMusic) * 24 + 22;
    m.Update(Point(520, musicY, true), v, snd);
    Check(v.music == 4, "shoot pause left arrow decreases volume");
    m.Update(Point(620, musicY, true), v, snd);
    Check(v.music == 5, "shoot pause right arrow increases volume");
    mixed = Point(620, musicY); mixed.pressed = 0x20;
    m.Update(mixed, v, snd);
    m.Update(Point(620, musicY), v, snd);
    Check(m.Cursor() == PauseMenu::kGuns, "resting pointer does not undo keyboard in pause menu");
    Check(m.Update(Point(5, 450, true), v, snd) == Action::None && !m.Confirming(), "blank shot leaves pause menu alone");

    // settings file round trip (volumes, input, display)
    const std::string path = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "\\hotd2re_settings_test.ini";
    play::Settings a;
    a.music = 3;
    a.guns = 7;
    a.sfx = 0;
    a.input = "mouse";
    a.display.fullscreen = true;
    a.display.windowScale = 0;
    a.display.stretch = true;
    a.display.crt = 2;
    a.display.renderScale = 3;
    Check(a.Save(path), "settings saved");
    play::Settings b;
    Check(b.Load(path) && b == a && b.input == "mouse", "settings loaded back (display included)");
    a.display.windowScale = 3;
    a.display.fullscreen = false;
    Check(a.Save(path), "settings saved again");
    Check(b.Load(path) && b.display.windowScale == 3 && !b.display.fullscreen, "scale=3 round trip");
    play::Settings c;
    Check(!c.Load(path + ".missing") && c == play::Settings{}, "missing file keeps the defaults");
    {  // an old file without display keys keeps the display defaults
        FILE* f = fopen(path.c_str(), "w");
        fputs("music=5\nguns=5\nsfx=5\n", f);
        fclose(f);
        play::Settings d;
        Check(d.Load(path) && d.music == 5 && d.display == play::DisplaySettings{}, "old settings file: display defaults");
    }
    remove(path.c_str());

    printf("%d / %d checks passed\n", g_checks - g_failed, g_checks);
    return g_failed == 0 ? 0 : 1;
}
