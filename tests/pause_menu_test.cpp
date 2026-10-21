// play.exe's pause menu (src/port/play/pause_menu.h): navigation with the pad bits and with a pointer, the volume
// rows, the display settings page (display_menu.h), the Return to Main Menu confirmation, and the settings file round
// trip. No game data needed.
#include "port/play/pause_menu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "port/play/display_pointer.h"
#include "port/play/keyboard.h"
#include "port/play/renderer_fallback.h"

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

bool Contains(const std::string& text, const std::string& part) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find(part) != std::string::npos;
}

// Compare everything outside the documented Image preset scope, including
// future settings added to other pages.
play::DisplaySettings WithoutImageChoices(play::DisplaySettings settings) {
    const port::GraphicsOptions defaults;
    auto& g = settings.graphics;
    g.msaa = defaults.msaa;
    g.postAA = defaults.postAA;
    g.temporalAA = defaults.temporalAA;
    g.anisotropy = defaults.anisotropy;
    g.textureFilter = defaults.textureFilter;
    g.outputResolutionHud = defaults.outputResolutionHud;
    g.sharpening = defaults.sharpening;
    g.alphaToCoverage = defaults.alphaToCoverage;
    g.downsample = defaults.downsample;
    return settings;
}

// Change every stored graphics/display field, including renderer requests that
// take effect at the next launch. Exact comparison below catches omitted pages.
play::DisplaySettings CustomizedGraphics(bool nightShift) {
    play::DisplaySettings d;
    d.fullscreen = d.exclusiveFullscreen = true;
    d.windowScale = 3;
    d.stretch = true;
    d.viewport = 3;
    d.redBlood = true;
    d.crt = 2;
    d.renderScale = 4;
    d.integerScale = true;
    d.frameCap = 144;
    d.monitor = 2;
    d.statistics = true;
    d.nightShift = nightShift;
    d.menuOpacity = 25;
    d.showCrosshairs = false;
    d.resolutionMode = 3;
    d.dynamicResolution = true;
    d.gpuBudgetFps = 144;
    d.renderer = 2;
    d.temporalUpscaler = 4;
    d.temporalQuality = 2;
    d.frameGeneration = 2;
    d.renderInterpolation = true;
    d.rayShadows = true;
    auto& g = d.graphics;
    g.msaa = 4;
    g.anisotropy = 16;
    g.textureFilter = 2;
    g.postAA = 3;
    g.temporalAA = g.outputResolutionHud = true;
    g.spatialUpscaler = 1;
    g.sharpening = 3;
    g.alphaToCoverage = true;
    g.syncInterval = 1;
    g.allowTearing = g.lowLatency = g.hdrOutput = true;
    g.hdrPaperWhite = 240;
    g.downsample = true;
    g.brightness = 65;
    g.gamma = 70;
    g.contrast = 75;
    g.saturation = 80;
    g.bloom = g.colorGrade = g.crtMask = g.crtCurvature = g.crtGlow = 2;
    g.perPixelLighting = g.materialMaps = true;
    g.softParticles = g.ambientOcclusion = g.shadowMaps = g.volumetricFog = g.reflections = g.cinematicFocus = g.meshRefinement = 2;
    return d;
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
    Check(m.Display().Cursor() == DisplayMenu::kFullscreen, "page opens on Display Mode");
    m.Update(Pad(0x80), v, snd);
    Check(v.display.fullscreen && !v.display.exclusiveFullscreen, "Display Mode: Windowed to Borderless");
    Check(DisplayMenu::ValueText(v.display, DisplayMenu::kScale).empty(), "window scale hidden in fullscreen");
    m.Update(Pad(0x8), v, snd);
    Check(v.display.fullscreen && v.display.exclusiveFullscreen, "Display Mode: Borderless to Fullscreen");
    m.Update(Pad(0x80), v, snd);
    Check(!v.display.fullscreen && !v.display.exclusiveFullscreen, "Display Mode: Fullscreen wraps to Windowed");
    m.Update(Pad(0x40), v, snd);
    Check(v.display.fullscreen && v.display.exclusiveFullscreen, "Display Mode reverses from Windowed to Fullscreen");
    m.Update(Pad(0x40), v, snd);
    Check(v.display.fullscreen && !v.display.exclusiveFullscreen, "Display Mode reverses from Fullscreen to Borderless");
    m.Update(Pad(0x40), v, snd);
    Check(!v.display.fullscreen, "Display Mode reverses from Borderless to Windowed");
    {
        play::DisplaySettings legacy;
        legacy.exclusiveFullscreen = true;
        Check(DisplayMenu::ValueText(legacy, DisplayMenu::kFullscreen) == "Windowed",
              "legacy exclusive preference does not imply an active fullscreen window");
        DisplayMenu::Step(legacy, DisplayMenu::kFullscreen, 1);
        Check(legacy.fullscreen && !legacy.exclusiveFullscreen, "Windowed advances to Borderless regardless of old preference");
        Check(DisplayMenu::ValueText(play::DisplaySettings{}, DisplayMenu::kAspect) == "Fit",
              "Image Fit does not repeat the scene aspect ratio");
    }
    m.Display().SetMonitors({"First monitor", "Second monitor"});
    m.Update(Pad(0x20), v, snd);  // Monitor
    Check(m.Display().Cursor() == DisplayMenu::kMonitor, "Display Mode is followed by the monitor choice");
    m.Update(Pad(0x80), v, snd);
    Check(v.display.monitor == 1, "monitor selection is available beside Display Mode");
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
    m.Update(Pad(0x20), v, snd);  // Aspect Ratio
    m.Update(Pad(0x80), v, snd);
    Check(m.Display().Cursor() == DisplayMenu::kViewport && v.display.viewport == 1 && !v.display.stretch,
          "Aspect Ratio selects 16:9 without stretching");
    m.Update(Pad(0x20), v, snd);  // Image Fit
    m.Update(Pad(0x80), v, snd);
    Check(v.display.stretch && v.display.viewport == 1 && DisplayMenu::ValueText(v.display, DisplayMenu::kAspect) == "Stretch",
          "Image Fit changes presentation without changing the scene aspect ratio");
    m.Update(Pad(0x20), v, snd);  // Integer Scaling
    Check(m.Display().Cursor() == DisplayMenu::kInteger, "Integer Scaling is beside its aspect and fit requirements");
    v.display.integerScale = true;
    m.Update(Pad(0x80), v, snd);
    Check(v.display.integerScale,
          "incompatible aspect/fit disables integer "
          "scaling without clearing its saved choice");
    m.Update(Pad(0x20), v, snd);  // Menu style
    Check(m.Display().Cursor() == DisplayMenu::kMenuStyle, "Display contains the menu-style choice");
    m.Update(Pad(0x80), v, snd);
    Check(!v.display.nightShift, "menu-style preference remains editable");
    m.Update(Pad(0x20), v, snd);  // Menu Opacity
    Check(m.Display().Cursor() == DisplayMenu::kMenuOpacity && v.display.menuOpacity == 65,
          "menu-background opacity has a readable, scene-visible default");
    for (int i = 0; i < 30; ++i) m.Update(Pad(0x40), v, snd);
    Check(v.display.menuOpacity == 25, "menu opacity clamps at the readable minimum");
    for (int i = 0; i < 30; ++i) m.Update(Pad(0x80), v, snd);
    Check(v.display.menuOpacity == 100, "menu opacity clamps at fully opaque backgrounds");
    m.Update(Pad(0x20), v, snd);  // Reset Graphics
    Check(m.Display().Cursor() == DisplayMenu::kResetGraphics && DisplayMenu::ValueText(v.display, DisplayMenu::kResetGraphics).empty(),
          "Display has a distinct Reset Graphics action before Back, not a cycling value");
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
    m.Display().SetPage(DisplayMenu::kResolution);
    const float rowRender = (DisplayMenu::RowY(DisplayMenu::kRender, DisplayMenu::kResolution) + 0.5f) * 24.0f;
    m.Update(Point(300.0f, rowRender), v, snd);
    Check(m.Display().Cursor() == DisplayMenu::kRender, "pointer highlights Render Scale");
    v.display.renderScale = 1;  // the stepping is checked from 1x
    m.Update(Point(300.0f, rowRender, true), v, snd);
    Check(v.display.renderScale == 2, "trigger: 2x");
    m.Update(Point(300.0f, rowRender, false, true), v, snd);
    Check(v.display.renderScale == 1, "reload: back to 1x");
    Check(play::Settings{}.display.renderScale == 2, "default fixed render scale remains 2x");
    v.display.renderScale = 1;
    for (int i = 0; i < 3; ++i) m.Update(Point(620, rowRender, true), v, snd);
    Check(v.display.renderScale == 4, "fixed render scale reaches 4x on Resolution");
    m.Update(Point(620, rowRender, true), v, snd);
    Check(v.display.renderScale == 1, "fixed render scale wraps to 1x");
    m.Display().SetPage(DisplayMenu::kColor);
    const float bloodY = DisplayMenu::RowY(DisplayMenu::kBlood, DisplayMenu::kColor) * 24 + 22;
    m.Update(Point(620, bloodY, true), v, snd);
    Check(v.display.redBlood, "shooting Blood Color enables red");
    m.Update(Point(620, bloodY, false, true), v, snd);
    Check(!v.display.redBlood, "reload toggles Blood Color to green");
    const float rowBack = (DisplayMenu::RowY(DisplayMenu::kBack, DisplayMenu::kColor) + 0.5f) * 24.0f;
    m.Update(Point(300.0f, rowBack), v, snd);
    m.Update(Point(300.0f, rowBack, true), v, snd);
    Check(!m.DisplayOpen() && m.IsOpen(), "trigger on Back closes the page");
    Check(DisplayMenu::RowAt(100.0f, 2.0f) == -1, "nothing under the pointer at the heading");
    // the standalone page (the Options screen's Display Settings row): Update reports the close
    DisplayMenu page;
    page.Open();
    Check(page.IsOpen() && !page.Update(Pad(0x20), v.display, snd) && page.Update(Pad(0x2), v.display, snd) && !page.IsOpen(),
          "standalone page: Backspace closes");

    // Visible letter bottoms used to fall in the next row's overlapping hitbox.
    for (int p = 0; p < DisplayMenu::kPageCount; ++p)
        for (int r : DisplayMenu::Rows(static_cast<DisplayMenu::Page>(p)))
            for (int ink : {13, 22, 30})
                Check(DisplayMenu::RowAt(40, DisplayMenu::RowY(r, static_cast<DisplayMenu::Page>(p)) * 24 + ink,
                                         static_cast<DisplayMenu::Page>(p)) == r,
                      "display hitbox covers rendered letter body on every page");
    for (int r = 0; r < PauseMenu::kRowCount; ++r)
        for (int ink : {13, 22, 30})
            Check(PauseMenu::RowAt(40, PauseMenu::RowY(r) * 24 + ink) == r, "pause hitbox covers rendered letter body");
    Check(PauseMenu::ConfirmAt(210, 10 * 24 + 30) == 1 && PauseMenu::ConfirmAt(400, 10 * 24 + 30) == 0,
          "Yes/No hitboxes include bottom of text");
    page.Open();
    page.SetPage(DisplayMenu::kRetro);
    v = play::Settings{};
    const float crtY = DisplayMenu::RowY(DisplayMenu::kCrt, DisplayMenu::kRetro) * 24 + 22;
    page.Update(Point(40, crtY), v.display, snd);
    auto mixed = Point(40, crtY);
    mixed.pressed = 0x20;
    page.Update(mixed, v.display, snd);
    page.Update(Point(40, crtY), v.display, snd);
    Check(page.Cursor() == DisplayMenu::kCrtMask, "resting pointer does not undo keyboard on CRT page");
    mixed.pressed = 8;
    page.Update(mixed, v.display, snd);
    Check(v.display.graphics.crtMask == 1 && v.display.crt == 0, "Enter changes the keyboard-selected CRT value");
    page.Update(Point(41, crtY), v.display, snd);
    Check(page.Cursor() == DisplayMenu::kCrt, "movement inside same row restores display hover");
    page.Update(Point(360, crtY, true), v.display, snd);
    Check(v.display.crt == 3, "shoot display left arrow steps backwards");
    page.Update(Point(620, crtY, true), v.display, snd);
    Check(v.display.crt == 0, "shoot display right arrow steps forwards");
    const auto displayBeforeMiss = v.display;
    page.Update(Point(5, 450, true), v.display, snd);
    Check(page.IsOpen() && v.display == displayBeforeMiss, "blank-space shot does not change display settings");
    page.SetPage(DisplayMenu::kDisplay);
    v.display.fullscreen = true;
    page.Update(Point(620, DisplayMenu::RowY(DisplayMenu::kScale) * 24 + 22, true), v.display, snd);
    Check(v.display.windowScale == 2, "disabled fullscreen window scale ignores shots");
    v.display.exclusiveFullscreen = true;
    page.Update(Point(620, DisplayMenu::RowY(DisplayMenu::kScale) * 24 + 22, true), v.display, snd);
    Check(v.display.windowScale == 2, "exclusive fullscreen also disables window scale");
    page.Update(Point(620, DisplayMenu::RowY(DisplayMenu::kFullscreen) * 24 + 22, true), v.display, snd);
    page.Update(Point(620, DisplayMenu::RowY(DisplayMenu::kScale) * 24 + 22, true), v.display, snd);
    Check(!v.display.fullscreen && v.display.windowScale == 3, "returning to Windowed restores window-scale editing");
    m.Open();
    v.music = 5;
    const float musicY = PauseMenu::RowY(PauseMenu::kMusic) * 24 + 22;
    m.Update(Point(520, musicY, true), v, snd);
    Check(v.music == 4, "shoot pause left arrow decreases volume");
    m.Update(Point(620, musicY, true), v, snd);
    Check(v.music == 5, "shoot pause right arrow increases volume");
    mixed = Point(620, musicY);
    mixed.pressed = 0x20;
    m.Update(mixed, v, snd);
    m.Update(Point(620, musicY), v, snd);
    Check(m.Cursor() == PauseMenu::kGuns, "resting pointer does not undo keyboard in pause menu");
    Check(m.Update(Point(5, 450, true), v, snd) == Action::None && !m.Confirming(), "blank shot leaves pause menu alone");

    for (bool inGame : {false, true})
        for (bool activePlayer : {false, true}) {
            const uint32_t regularKeys = 0xF8u | play::kEscapeKey;  // arrows, Enter and Escape
            const auto baseline = play::WorldKeyboardBits(regularKeys, inGame, activePlayer);
            Check(play::WorldKeyboardBits(regularKeys | play::kPreviewKey, inGame, activePlayer) == baseline &&
                      play::WorldKeyboardBits(play::kPreviewKey, inGame, activePlayer) == 0,
                  "local preview key never reaches gameplay input or changes normal controls in any player/menu state");
        }

    // Night Shift keeps the scene visible and uses its own panel rectangles for both drawing and interaction.
    {
        PauseMenu modern;
        modern.SetModern(true);
        modern.Open();
        play::Settings themed;
        themed.music = 5;
        themed.display.nightShift = false;  // A next-launch preference must not change this session's pointer layout.
        Check(modern.IsModern() && modern.Cursor() == PauseMenu::kResume, "launch-selected Night Shift pause remains active");
        for (int row = 0; row < PauseMenu::kRowCount; ++row) {
            const float top = PauseMenu::ModernRowTop(row);
            Check(PauseMenu::ModernRowAt(112, top) == row && PauseMenu::ModernRowAt(530, top + 24) == row,
                  "Night Shift pause hit targets cover each rendered row without overlapping its neighbor");
        }
        Check(PauseMenu::ModernRowAt(620, PauseMenu::ModernRowTop(PauseMenu::kMusic)) == -1 &&
                  PauseMenu::ModernRowAt(112, 65) == -1,
              "Classic pointer bounds and the Night Shift heading do not activate modern pause rows");
        const float modernMusicY = PauseMenu::ModernRowTop(PauseMenu::kMusic) + 12;
        modern.Update(Point(432, modernMusicY, true), themed, snd);
        Check(themed.music == 4 && modern.Cursor() == PauseMenu::kMusic, "modern volume left arrow lowers the selected volume");
        modern.Update(Point(518, modernMusicY, true), themed, snd);
        Check(themed.music == 5, "modern volume right arrow raises the selected volume");
        game::WorldHud themedHud;
        modern.ModernHud(1, themed, themedHud);
        Check(!themedHud.menuPanels.empty() &&
                  std::any_of(themedHud.menuText.begin(), themedHud.menuText.end(), [](const auto& t) { return t.text == "50%"; }),
              "Night Shift pause renders scene-visible panels and volume percentages");
        Check(std::any_of(themedHud.menuText.begin(), themedHud.menuText.end(), [](const auto& t) { return Contains(t.text, "player 2"); }),
              "modern pause identifies the player who paused");
        auto restingModern = Point(518, modernMusicY);
        restingModern.pressed = 0x20;
        modern.Update(restingModern, themed, snd);
        modern.Update(Point(518, modernMusicY), themed, snd);
        Check(modern.Cursor() == PauseMenu::kGuns, "resting pointer does not undo keyboard navigation in Night Shift pause");
        const auto beforeOutside = themed;
        modern.Update(Point(620, modernMusicY, true), themed, snd);
        Check(themed == beforeOutside, "shots outside the modern panel do not edit volume values");
        const float modernDisplayY = PauseMenu::ModernRowTop(PauseMenu::kDisplay) + 12;
        modern.Update(Point(200, modernDisplayY, true), themed, snd);
        Check(modern.DisplayOpen(), "modern graphics row opens the shared options page");
        modern.ModernHud(0, themed, themedHud);
        Check(std::any_of(themedHud.menuText.begin(), themedHud.menuText.end(), [](const auto& t) { return t.text == "OPTIONS"; }),
              "modern pause delegates its options view to the same Night Shift page");
        auto preview = Pad(0);
        preview.preview = true;
        const auto beforePreview = themed.display;
        const int previewCursor = modern.Display().Cursor();
        modern.Update(preview, themed, snd);
        Check(modern.Display().UiHidden(), "options preview hides the UI while the pause menu stays open");
        modern.Update(Pad(0xA0), themed, snd);  // Down + Right would normally navigate or edit.
        modern.Update(Point(620, 150, true), themed, snd);
        Check(modern.Display().UiHidden() && themed.display == beforePreview && modern.Display().Cursor() == previewCursor,
              "hidden options ignore navigation and value changes without losing their saved cursor");
        Check(modern.Update(st, themed, snd) == Action::None && modern.IsOpen() && modern.DisplayOpen() &&
                  !modern.Display().UiHidden(),
              "Start restores hidden options without resuming the game");
        Check(modern.Display().Cursor() == previewCursor, "restoring options returns to the exact selected row");
        modern.Update(preview, themed, snd);
        modern.Update(Pad(2), themed, snd);
        Check(modern.DisplayOpen() && !modern.Display().UiHidden(), "Back restores hidden options before it can leave the page");
        modern.Update(preview, themed, snd);
        Check(modern.Update(Pad(8), themed, snd) == Action::None && modern.DisplayOpen() && !modern.Display().UiHidden(),
              "Enter restores hidden options without activating the selected setting");
        modern.Update(preview, themed, snd);
        modern.Update(preview, themed, snd);
        Check(modern.DisplayOpen() && !modern.Display().UiHidden() && themed.display == beforePreview,
              "the preview shortcut also restores the UI without changing settings");
        modern.Update(Pad(2), themed, snd);
        Check(!modern.DisplayOpen() && modern.IsOpen(), "Back on visible options returns to modern pause");

        const float modernReturnY = PauseMenu::ModernRowTop(PauseMenu::kReturn) + 12;
        modern.Update(Point(200, modernReturnY, true), themed, snd);
        Check(modern.Confirming() && !modern.ConfirmYes(), "modern return confirmation starts on safe Cancel");
        Check(PauseMenu::ModernConfirmAt(200, 292) == 1 && PauseMenu::ModernConfirmAt(400, 292) == 0 &&
                  PauseMenu::ModernConfirmAt(320, 292) == -1,
              "modern confirmation hit targets match Return, Cancel and their gap");
        Check(modern.Update(Point(320, 292, true), themed, snd) == Action::None && modern.Confirming() && !modern.ConfirmYes(),
              "shooting the gap between confirmation buttons cannot leave the run");
        Check(modern.Update(Point(400, 292, true), themed, snd) == Action::None && !modern.Confirming(),
              "modern Cancel returns to pause without leaving the run");
        modern.SetNetworkRoom(true);
        modern.Update(Point(200, modernReturnY, true), themed, snd);
        modern.ModernHud(1, themed, themedHud);
        Check(std::any_of(themedHud.menuText.begin(), themedHud.menuText.end(), [](const auto& t) { return Contains(t.text, "online co-op"); }) &&
                  std::any_of(themedHud.menuText.begin(), themedHud.menuText.end(), [](const auto& t) {
                      return Contains(t.text, "both players") && Contains(t.text, "lobby");
                  }),
              "online pause explains that returning to the lobby affects both players");
        Check(modern.Update(Point(200, 292, true), themed, snd) == Action::ReturnToTitle,
              "modern confirmed Return uses the existing return-to-title/lobby action");
    }
    {
        DisplayMenu focus;
        focus.Open();
        play::DisplaySettings focusedSettings;
        auto stationaryPointer = Point(300, DisplayMenu::RowY(DisplayMenu::kFullscreen) * 24 + 22);
        focus.Update(stationaryPointer, focusedSettings, snd);
        auto keyboardMove = stationaryPointer;
        keyboardMove.pressed = 0x20;
        focus.Update(keyboardMove, focusedSettings, snd);
        const int keyboardCursor = focus.Cursor();
        auto togglePreview = stationaryPointer;
        togglePreview.preview = true;
        focus.Update(togglePreview, focusedSettings, snd);
        focus.Update(stationaryPointer, focusedSettings, snd);
        focus.Update(togglePreview, focusedSettings, snd);
        focus.Update(stationaryPointer, focusedSettings, snd);
        Check(!focus.UiHidden() && keyboardCursor == DisplayMenu::kMonitor && focus.Cursor() == keyboardCursor,
              "a stationary pointer cannot steal keyboard selection on the first visible frame after preview restoration");
    }

    // Gameplay sights are a local visibility preference, independent of renderer
    // capabilities and of menu/calibration pointer visibility.
    {
        DisplayMenu sights;
        play::DisplaySettings d;
        Check(d.showCrosshairs && DisplayMenu::ValueText(d, DisplayMenu::kCrosshairs) == "On",
              "gameplay crosshairs remain visible by default for existing players");
        sights.Open();
        sights.SetPage(DisplayMenu::kClarity);
        const auto& imageRows = DisplayMenu::Rows(DisplayMenu::kClarity);
        const auto sightRow = std::find(imageRows.begin(), imageRows.end(), DisplayMenu::kCrosshairs);
        Check(imageRows.size() == 10 && sightRow != imageRows.end() && sightRow != imageRows.begin() &&
                  *(sightRow - 1) == DisplayMenu::kIndependentUi,
              "Image contains Gameplay Crosshairs once, directly after Sharp HUD");
        for (size_t i = 0; i < imageRows.size() && sights.Cursor() != DisplayMenu::kCrosshairs; ++i)
            sights.Update(Pad(0x20), d, snd);
        Check(sights.Cursor() == DisplayMenu::kCrosshairs, "keyboard navigation reaches the gameplay crosshair row");
        sights.Update(Pad(0x80), d, snd);
        Check(!d.showCrosshairs && DisplayMenu::ValueText(d, DisplayMenu::kCrosshairs) == "Off" && sights.IsOpen(),
              "Right hides gameplay sights immediately without closing options or requiring a renderer capability");
        sights.Update(Pad(0x40), d, snd);
        Check(d.showCrosshairs, "Left restores gameplay crosshairs");
        sights.Update(Pad(8), d, snd);
        Check(!d.showCrosshairs, "Enter toggles gameplay sight visibility");
        const float sightY = DisplayMenu::RowY(DisplayMenu::kCrosshairs, DisplayMenu::kClarity) * 24 + 22;
        sights.Update(Point(620, sightY, true), d, snd);
        Check(d.showCrosshairs && sights.Cursor() == DisplayMenu::kCrosshairs,
              "a lightgun trigger on the displayed row restores gameplay sights");
        sights.Update(Point(620, sightY, false, true), d, snd);
        Check(!d.showCrosshairs && sights.IsOpen(), "reload on the row hides sights without exiting the page");
        const auto beforeHidden = d;
        auto preview = Pad(0);
        preview.preview = true;
        sights.Update(preview, d, snd);
        sights.Update(Pad(0x80), d, snd);
        sights.Update(Point(620, sightY, true), d, snd);
        Check(sights.UiHidden() && d == beforeHidden && sights.Cursor() == DisplayMenu::kCrosshairs,
              "Hide UI ignores hidden sight adjustments and preserves the selected row");
        sights.Update(preview, d, snd);
        Check(!sights.UiHidden() && !d.showCrosshairs, "Show UI preserves the gameplay sight preference");
        game::WorldHud sightHud;
        sights.ModernHud(d, sightHud);
        Check(std::any_of(sightHud.menuText.begin(), sightHud.menuText.end(), [](const auto& t) {
                  return t.text == "Gameplay Crosshairs" && t.color == 0xFFECE7DC;
              }),
              "crosshair control is enabled even when renderer capabilities are absent");
        const auto help = sights.RowHelp(d, DisplayMenu::kCrosshairs);
        Check(Contains(help, "both players") && Contains(help, "this computer") && Contains(help, "menu and calibration") &&
                  Contains(help, "aiming and shooting") && !Contains(help, "restart"),
              "sight help explains local scope, immediate behavior and visible setup pointers");
        for (int preset = 0; preset < 3; ++preset) {
            DisplayMenu::ApplyPreset(d, preset);
            Check(!d.showCrosshairs, "Original, Clean and Enhanced image presets preserve hidden gameplay sights");
        }
    }

    // A complete graphics reset is a confirmed action, separate from Image presets.
    // All checks use isolated settings and temporary files; no game data is needed.
    {
        Check(DisplayMenu::ResetChoiceAt(128, 278) == 1 && DisplayMenu::ResetChoiceAt(303, 315) == 1 &&
                  DisplayMenu::ResetChoiceAt(336, 278) == 0 && DisplayMenu::ResetChoiceAt(511, 315) == 0 &&
                  DisplayMenu::ResetChoiceAt(304, 295) == -1 && DisplayMenu::ResetChoiceAt(320, 295) == -1 &&
                  DisplayMenu::ResetChoiceAt(512, 295) == -1 && DisplayMenu::ResetChoiceAt(200, 316) == -1,
              "reset and cancel have separate bounded hit targets with an inert gap");
        const std::string resetPath = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "\\hotd2re_graphics_reset_test.ini";
        for (bool nextLaunchTheme : {false, true}) {
            DisplayMenu resetPage;
            resetPage.SetPacks({{"Original", "", "", true}, {"Custom visuals", "custom_visual_pack", "", true}}, "custom_visual_pack");
            play::Settings custom;
            custom.display = CustomizedGraphics(nextLaunchTheme);
            custom.music = 3;
            custom.guns = 7;
            custom.sfx = 0;
            custom.input = "touch";
            custom.assets = "custom_visual_pack";
            custom.lobby = "127.0.0.1:27910";
            const auto before = custom;
            resetPage.Open();
            resetPage.Update(Pad(0x10), custom.display, snd);  // Wrap to Back.
            resetPage.Update(Pad(0x10), custom.display, snd);  // Reset Graphics.
            Check(resetPage.Cursor() == DisplayMenu::kResetGraphics && !resetPage.ResetConfirming() && !resetPage.TakeGraphicsReset(),
                  "keyboard can reach Reset Graphics without opening or applying it");
            resetPage.Update(Pad(0x40), custom.display, snd);
            resetPage.Update(Pad(0x80), custom.display, snd);
            DisplayMenu::Step(custom.display, DisplayMenu::kResetGraphics, 1);
            DisplayMenu::Step(custom.display, DisplayMenu::kResetGraphics, -1);
            Check(!resetPage.ResetConfirming() && custom.display == before.display,
                  "Left and Right cannot reset graphics or open the action's confirmation");
            resetPage.Update(Pad(8), custom.display, snd);
            Check(resetPage.IsOpen() && resetPage.ResetConfirming() && !resetPage.ResetSelected() && custom.display == before.display,
                  "Enter opens a safe Cancel confirmation without changing any graphics choice");
            resetPage.Update(Pad(8), custom.display, snd);
            Check(resetPage.IsOpen() && !resetPage.ResetConfirming() && custom.display == before.display && !resetPage.TakeGraphicsReset(),
                  "Enter again on default Cancel leaves every graphics choice untouched");
            // The pointer can rest over the future Reset button while keyboard
            // navigation reaches the action; revealing a modal must not select it.
            auto restingResetPointer = Point(200, 295);
            resetPage.Update(restingResetPointer, custom.display, snd);
            for (size_t row = 0; row < DisplayMenu::Rows(DisplayMenu::kDisplay).size() &&
                                 resetPage.Cursor() != DisplayMenu::kResetGraphics; ++row) {
                restingResetPointer.pressed = 0x20;
                resetPage.Update(restingResetPointer, custom.display, snd);
            }
            restingResetPointer.pressed = 8;
            resetPage.Update(restingResetPointer, custom.display, snd);
            restingResetPointer.pressed = 0;
            resetPage.Update(restingResetPointer, custom.display, snd);
            Check(resetPage.ResetConfirming() && !resetPage.ResetSelected() && custom.display == before.display,
                  "a stationary pointer over the new Reset button cannot steal the modal's safe Cancel default");
            restingResetPointer.pressed = 8;
            resetPage.Update(restingResetPointer, custom.display, snd);
            Check(!resetPage.ResetConfirming() && custom.display == before.display && !resetPage.TakeGraphicsReset(),
                  "keyboard confirmation with that stationary pointer still cancels safely");
            const float resetY = DisplayMenu::RowY(DisplayMenu::kResetGraphics) * 24 + 22;
            resetPage.Update(Point(620, resetY, true), custom.display, snd);
            Check(resetPage.ResetConfirming() && !resetPage.ResetSelected() && custom.display == before.display,
                  "shooting the Reset Graphics row opens confirmation without click-through");
            auto hideAttempt = Pad(0);
            hideAttempt.preview = true;
            resetPage.Update(hideAttempt, custom.display, snd);
            resetPage.Update(Point(500, 443, true), custom.display, snd);
            resetPage.Update(Point(DisplayMenu::TabLeft(DisplayMenu::kColor) + 8, 90, true), custom.display, snd);
            resetPage.Update(Point(320, 295, true), custom.display, snd);
            Check(resetPage.ResetConfirming() && !resetPage.UiHidden() && resetPage.CurrentPage() == DisplayMenu::kDisplay &&
                      custom.display == before.display && !resetPage.TakeGraphicsReset(),
                  "confirmation blocks Hide UI, category clicks and blank-space shots without changing settings");
            resetPage.Update(Point(400, 295, true), custom.display, snd);
            Check(!resetPage.ResetConfirming() && resetPage.IsOpen() && custom.display == before.display,
                  "pointer Cancel returns to the graphics page with its exact choices");
            for (int cancellation : {0, 1, 2}) {
                resetPage.Update(Pad(4), custom.display, snd);
                auto cancel = Pad(cancellation == 0 ? 2 : 0);
                cancel.start = cancellation == 1;
                cancel.reload = cancellation == 2;
                Check(!resetPage.Update(cancel, custom.display, snd) && resetPage.IsOpen() && !resetPage.ResetConfirming() &&
                          custom.display == before.display && !resetPage.TakeGraphicsReset(),
                      "Back, Start and reload each cancel reset without leaving graphics or mutating saved choices");
            }
            resetPage.Update(Pad(8), custom.display, snd);
            game::WorldHud resetHud;
            resetPage.ModernHud(custom.display, resetHud);
            Check(!resetHud.menuPanels.empty() &&
                      std::any_of(resetHud.menuText.begin(), resetHud.menuText.end(),
                                  [](const auto& t) { return Contains(t.text, "reset"); }) &&
                      std::any_of(resetHud.menuText.begin(), resetHud.menuText.end(),
                                  [](const auto& t) { return Contains(t.text, "cancel"); }) &&
                      std::none_of(resetHud.menuText.begin(), resetHud.menuText.end(),
                                   [](const auto& t) { return Contains(t.text, "hide ui"); }),
                  "Night Shift confirmation displays its reset/cancel choices and removes the conflicting Hide UI control");
            game::ExeImage noData;
            std::vector<game::SpriteDef> resetSprites;
            resetPage.Sprites(noData, custom.display, resetSprites, true);
            Check(!resetSprites.empty() && std::all_of(resetSprites.begin(), resetSprites.end(),
                                                       [](const auto& sprite) {
                                                           return sprite.depth == .0002f && (sprite.flags & 0x700) == 0x700 &&
                                                                  sprite.id != 0xAFF;
                                                       }),
                  "Classic reset confirmation renders in front of the paused scene without value-adjustment arrows");
            resetPage.Update(Pad(0x40), custom.display, snd);
            Check(resetPage.ResetSelected() && custom.display == before.display, "choosing Reset does not apply it until confirmed");
            resetPage.Update(Pad(4), custom.display, snd);
            auto defaults = play::DisplaySettings{};
            defaults.nightShift = nextLaunchTheme;
            Check(!resetPage.ResetConfirming() && resetPage.IsOpen() && resetPage.Cursor() == DisplayMenu::kResetGraphics &&
                      custom.display == defaults,
                  "confirmed reset restores every graphics/display default while retaining the next-launch menu theme");
            std::string unexpectedPackChange;
            Check(custom.music == before.music && custom.guns == before.guns && custom.sfx == before.sfx && custom.input == before.input &&
                      custom.assets == before.assets && custom.lobby == before.lobby && resetPage.SelectedPackPath() == before.assets &&
                      !resetPage.TakePackChange(&unexpectedPackChange),
                  "graphics reset preserves volumes, input, selected visual pack and lobby address");
            Check(custom.display.renderer == 0 && custom.display.temporalUpscaler == 0 && custom.display.frameGeneration == 0 &&
                      !custom.display.rayShadows && !custom.display.dynamicResolution && custom.display.renderScale == 2 &&
                      custom.display.frameCap == 60 && custom.display.menuOpacity == 65 && custom.display.showCrosshairs,
                  "reset clears renderer/upscaling/frame-generation/ray requests and restores 2x/60 FPS with visible gameplay sights");
            Check(resetPage.TakeGraphicsReset() && !resetPage.TakeGraphicsReset(), "confirmed reset is acknowledged exactly once");
            play::Settings reloaded;
            Check(custom.Save(resetPath) && reloaded.Load(resetPath) && reloaded.display == defaults && reloaded.music == before.music &&
                      reloaded.guns == before.guns && reloaded.sfx == before.sfx && reloaded.input == before.input &&
                      reloaded.assets == before.assets && reloaded.lobby == before.lobby,
                  "all reset defaults and preserved non-graphics choices survive the next-launch settings round trip");
            resetPage.Update(Pad(8), custom.display, snd);
            resetPage.Update(Point(200, 295, true), custom.display, snd);
            Check(custom.display == defaults && resetPage.TakeGraphicsReset() && !resetPage.TakeGraphicsReset(),
                  "resetting already-default graphics is harmless and still produces one save/apply acknowledgment");
            resetPage.Update(Pad(8), custom.display, snd);
            resetPage.Close();
            Check(!resetPage.IsOpen() && !resetPage.ResetConfirming(), "Close clears an unconfirmed reset modal");
            resetPage.Open();
            Check(!resetPage.ResetConfirming() && !resetPage.UiHidden() && resetPage.Cursor() == DisplayMenu::kFullscreen,
                  "Open cannot revive a stale reset confirmation");
            resetPage.Update(Pad(0x10), custom.display, snd);
            resetPage.Update(Pad(0x10), custom.display, snd);
            resetPage.Update(Pad(8), custom.display, snd);
            resetPage.SetPage(DisplayMenu::kColor);
            Check(!resetPage.ResetConfirming() && custom.display == defaults,
                  "changing the graphics category explicitly clears unconfirmed reset state");
        }
        remove(resetPath.c_str());
        for (bool modernLayout : {false, true}) {
            PauseMenu nested;
            nested.SetModern(modernLayout);
            nested.Open();
            play::Settings running;
            running.display = CustomizedGraphics(!modernLayout);
            const auto runningBefore = running.display;
            for (int row = 0; row < PauseMenu::kDisplay; ++row) nested.Update(Pad(0x20), running, snd);
            nested.Update(Pad(8), running, snd);
            nested.Update(Pad(0x10), running, snd);
            nested.Update(Pad(0x10), running, snd);
            nested.Update(Pad(8), running, snd);
            Check(nested.DisplayOpen() && nested.Display().ResetConfirming(),
                  "graphics reset confirmation opens inside both Classic and Night Shift pause layouts");
            Check(nested.Update(st, running, snd) == Action::None && nested.IsOpen() && nested.DisplayOpen() &&
                      !nested.Display().ResetConfirming() && running.display == runningBefore,
                  "Start inside reset confirmation cancels it without resuming the paused game");
            nested.Update(Pad(8), running, snd);
            Check(nested.Update(Pad(2), running, snd) == Action::None && nested.IsOpen() && nested.DisplayOpen() &&
                      !nested.Display().ResetConfirming(),
                  "Back inside reset confirmation returns to graphics instead of closing the nested page");
            nested.Update(Pad(8), running, snd);
            nested.Update(Pad(0x80), running, snd);
            auto expected = play::DisplaySettings{};
            expected.nightShift = runningBefore.nightShift;
            Check(nested.Update(Pad(8), running, snd) == Action::None && nested.IsOpen() && nested.DisplayOpen() &&
                      running.display == expected && nested.Display().TakeGraphicsReset() && !nested.Display().TakeGraphicsReset(),
                  "confirming a nested graphics reset keeps gameplay paused and exposes one persistence event to the caller");
        }
    }

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
    a.display.redBlood = true;
    a.display.graphics.msaa = 4;
    a.display.graphics.anisotropy = 16;
    a.display.graphics.postAA = 1;
    a.display.graphics.gamma = 65;
    a.display.graphics.perPixelLighting = true;
    a.display.graphics.bloom = 1;
    a.display.graphics.allowTearing = true;
    a.display.graphics.lowLatency = true;
    a.display.frameCap = 144;
    a.display.integerScale = true;
    a.display.monitor = 2;
    a.display.exclusiveFullscreen = true;
    a.display.nightShift = false;
    a.display.viewport = 3;
    a.display.graphics.softParticles = 2;
    a.display.graphics.materialMaps = true;
    a.display.graphics.ambientOcclusion = 1;
    a.display.graphics.hdrOutput = true;
    a.display.graphics.hdrPaperWhite = 240;
    a.display.graphics.temporalAA = true;
    a.display.graphics.postAA = 3;
    a.display.graphics.shadowMaps = 1;
    a.display.graphics.volumetricFog = 1;
    a.display.graphics.reflections = 2;
    a.display.graphics.cinematicFocus = 3;
    a.display.graphics.meshRefinement = 2;
    a.display.graphics.crtMask = 2;
    a.display.graphics.crtCurvature = 1;
    a.display.graphics.crtGlow = 3;
    a.display.resolutionMode = 3;
    a.display.dynamicResolution = true;
    a.display.gpuBudgetFps = 144;
    a.display.renderer = 2;
    a.display.temporalUpscaler = 4;
    a.display.temporalQuality = 2;
    a.display.rayShadows = true;
    a.display.graphics.outputResolutionHud = true;
    a.display.renderInterpolation = true;
    a.display.frameGeneration = 2;
    a.display.menuOpacity = 80;
    a.display.showCrosshairs = false;
    Check(a.Save(path), "settings saved");
    play::Settings b;
    Check(b.Load(path) && b == a && b.input == "mouse", "settings loaded back (display included)");
    a.display.windowScale = 3;
    a.display.fullscreen = false;
    Check(a.Save(path), "settings saved again");
    Check(b.Load(path) && b.display.windowScale == 3 && !b.display.fullscreen && b.display.exclusiveFullscreen &&
              DisplayMenu::ValueText(b.display, DisplayMenu::kFullscreen) == "Windowed",
          "legacy exclusive preference survives a windowed settings round trip "
          "without changing the displayed mode");
    play::Settings c;
    Check(!c.Load(path + ".missing") && c == play::Settings{}, "missing file keeps the defaults");
    {  // an old file without display keys keeps the display defaults
        FILE* f = fopen(path.c_str(), "w");
        fputs("music=5\nguns=5\nsfx=5\n", f);
        fclose(f);
        play::Settings d;
        Check(d.Load(path) && d.music == 5 && d.display == play::DisplaySettings{}, "old settings file: display defaults");
    }
    {
        FILE* f = fopen(path.c_str(), "w");
        fputs(
            "fullscreen=1\nfullscreen_mode=exclusive\nscale=fit\naspect="
            "stretch\nviewport=3\ncrt=2\nrender=3\nblood=red\n",
            f);
        fclose(f);
        play::Settings oldDisplay;
        Check(oldDisplay.Load(path) && oldDisplay.display.fullscreen && oldDisplay.display.exclusiveFullscreen &&
                  oldDisplay.display.windowScale == 0 && oldDisplay.display.stretch && oldDisplay.display.viewport == 3 &&
                  oldDisplay.display.crt == 2 && oldDisplay.display.renderScale == 3 && oldDisplay.display.redBlood,
              "previous display keys retain their meanings after rows move and "
              "labels change");
        Check(DisplayMenu::ValueText(oldDisplay.display, DisplayMenu::kFullscreen) == "Fullscreen" &&
                  DisplayMenu::ValueText(oldDisplay.display, DisplayMenu::kViewport) == "32:9" &&
                  DisplayMenu::ValueText(oldDisplay.display, DisplayMenu::kAspect) == "Stretch",
              "legacy display choices have consistent modern values");
    }
    {
        FILE* f = fopen(path.c_str(), "w");
        fputs("blood=invalid\n", f);
        fclose(f);
        play::Settings d;
        d.display.redBlood = true;
        Check(d.Load(path) && d.display.redBlood, "invalid blood value keeps current choice");
    }
    {
        FILE* f = fopen(path.c_str(), "w");
        fputs("menu_opacity=24\nmenu_opacity=101\nmenu_opacity=invalid\n", f);
        fclose(f);
        play::Settings opacity;
        opacity.display.menuOpacity = 70;
        Check(opacity.Load(path) && opacity.display.menuOpacity == 70, "invalid opacity values retain the current readable preference");
        bool endpointsRoundTrip = true;
        for (int value : {25, 100}) {
            opacity.display.menuOpacity = value;
            play::Settings loadedOpacity;
            endpointsRoundTrip = endpointsRoundTrip && opacity.Save(path) && loadedOpacity.Load(path) &&
                                 loadedOpacity.display.menuOpacity == value;
        }
        Check(endpointsRoundTrip, "both supported opacity endpoints survive settings persistence");
    }
    {
        play::Settings sights;
        Check(sights.display.showCrosshairs, "missing legacy crosshair key defaults to visible gameplay sights");
        for (bool visible : {false, true}) {
            sights.display.showCrosshairs = visible;
            play::Settings loaded;
            loaded.display.showCrosshairs = !visible;
            Check(sights.Save(path) && loaded.Load(path) && loaded.display.showCrosshairs == visible,
                  "both crosshairs=0 and crosshairs=1 survive settings round trips");
        }
        for (const char* invalid : {"", "invalid", "2", "-1", "1junk", "0.0", "true"}) {
            FILE* f = fopen(path.c_str(), "w");
            fprintf(f, "crosshairs=%s\n", invalid);
            fclose(f);
            for (bool visible : {false, true}) {
                sights.display.showCrosshairs = visible;
                Check(sights.Load(path) && sights.display.showCrosshairs == visible,
                      "invalid crosshair values preserve either current visibility preference");
            }
        }
        FILE* f = fopen(path.c_str(), "w");
        fputs("music=5\n", f);
        fclose(f);
        play::Settings legacy;
        Check(legacy.Load(path) && legacy.display.showCrosshairs,
              "loading a legacy settings file without crosshairs keeps the default visible");
    }
    remove(path.c_str());

    page.Open();
    page.SetPage(DisplayMenu::kClarity);
    port::GraphicsCapabilities fullCaps;
    fullCaps.msaa2 = fullCaps.msaa4 = fullCaps.smaa = fullCaps.cmaa2 = fullCaps.temporalAA = true;
    fullCaps.outputResolutionHud = fullCaps.hdrOutput = fullCaps.tearing = fullCaps.lowLatency = true;
    fullCaps.gpuTiming = fullCaps.materialMaps = fullCaps.sceneEffects = true;
    fullCaps.maxAnisotropy = 16;
    page.SetGraphicsCapabilities(fullCaps);
    v.display = a.display;
    v.display.graphics.spatialUpscaler = 1;
    DisplayMenu::ApplyPreset(v.display, 0);
    const auto unrelatedBeforePresets = WithoutImageChoices(v.display);
    Check(DisplayMenu::PresetName(v.display, &fullCaps) == "Original",
          "unrelated color/lighting/CRT choices do not make Image preset Custom");
    page.Update(Pad(8), v.display, snd);
    Check(v.display.graphics.msaa == 4 && v.display.graphics.anisotropy == 16 && v.display.graphics.downsample,
          "Clean preset applies actual supported image features");
    Check(WithoutImageChoices(v.display) == unrelatedBeforePresets, "Clean preserves every setting outside the Image preset scope");
    page.Update(Pad(8), v.display, snd);
    Check(v.display.graphics.postAA == 2 && v.display.graphics.sharpening == 1 && v.display.graphics.outputResolutionHud,
          "Enhanced adds SMAA, gentle sharpening and a sharp HUD");
    Check(WithoutImageChoices(v.display) == unrelatedBeforePresets, "Enhanced preserves lighting, color, CRT, packs and presentation");
    page.Update(Pad(8), v.display, snd);
    Check(v.display.graphics.msaa == 1 && v.display.graphics.postAA == 0 && !v.display.graphics.temporalAA &&
              v.display.graphics.anisotropy == 1 && v.display.graphics.sharpening == 0 && !v.display.graphics.outputResolutionHud &&
              !v.display.graphics.downsample && WithoutImageChoices(v.display) == unrelatedBeforePresets,
          "Original restores only Image choices and downsampling");
    port::GraphicsCapabilities limitedCaps;
    limitedCaps.msaa2 = true;
    limitedCaps.maxAnisotropy = 4;
    page.SetGraphicsCapabilities(limitedCaps);
    page.Update(Pad(8), v.display, snd);
    Check(v.display.graphics.msaa == 2 && v.display.graphics.anisotropy == 4 && DisplayMenu::PresetName(v.display, &limitedCaps) == "Clean",
          "Clean adapts to the renderer without falsely reporting Custom");
    Check(WithoutImageChoices(v.display) == unrelatedBeforePresets, "capability-adapted presets preserve unrelated preferences");
    Check(!Contains(page.RowHelp(v.display, DisplayMenu::kPreset), "smaa"),
          "limited-renderer preset help does not promise unavailable SMAA");
    page.SetGraphicsCapabilities(fullCaps);
    page.SetPage(DisplayMenu::kPacks);
    page.SetPacks({{"Original", "", "", true}, {"Broken", "bad", "Invalid PNG", false}, {"2x", "pack", "", true}}, "");
    page.Update(Pad(8), v.display, snd);
    std::string chosen;
    Check(page.TakePackChange(&chosen) && chosen == "pack", "invalid pack is skipped without trapping the selector");
    Check(!page.TakePackChange(&chosen), "pack choice consumed once");
    {
        FILE* f = fopen(path.c_str(), "w");
        fputs("msaa=3\nanisotropy=7\npost_aa=99\ngamma=bad\nframe_cap=-1\nlow_latency=bad\n", f);
        fclose(f);
        play::Settings loaded;
        Check(loaded.Load(path) && loaded.display == play::DisplaySettings{}, "invalid graphics settings retain safe defaults");
        remove(path.c_str());
    }

    std::vector<int> rowOccurrences(DisplayMenu::kRowCount, 0);
    float previousTabRight = 0;
    for (int tab = 0; tab < DisplayMenu::kPageCount; ++tab) {
        const float left = DisplayMenu::TabLeft(tab), width = DisplayMenu::TabWidth(tab);
        Check(width > 0 && left >= previousTabRight && left + width <= 640, "tab labels have distinct visible hit areas");
        previousTabRight = left + width;
        Check(DisplayMenu::TabAt(left + width * .5f, 90) == tab, "every visible category tab has its matching target");
        const auto& rows = DisplayMenu::Rows(static_cast<DisplayMenu::Page>(tab));
        Check(rows.size() <= 10 && DisplayMenu::RowY(rows.back(), static_cast<DisplayMenu::Page>(tab)) * 24 + 30 < 381,
              "each category's last rendered row stays above the help area, including Display with reset");
        for (int row : rows)
            if (row != DisplayMenu::kBack) ++rowOccurrences[row];
    }
    Check(std::all_of(rowOccurrences.begin(), rowOccurrences.end(), [](int n) { return n <= 1; }),
          "each setting appears on only one options page");
    auto onPage = [](int row, DisplayMenu::Page tab) {
        const auto& rows = DisplayMenu::Rows(tab);
        return std::find(rows.begin(), rows.end(), row) != rows.end();
    };
    Check(onPage(DisplayMenu::kMonitor, DisplayMenu::kDisplay) && onPage(DisplayMenu::kInteger, DisplayMenu::kDisplay) &&
              onPage(DisplayMenu::kMenuStyle, DisplayMenu::kDisplay),
          "window, aspect-dependent scaling and menu style share Display");
    Check(onPage(DisplayMenu::kRender, DisplayMenu::kResolution) && onPage(DisplayMenu::kUpscaler, DisplayMenu::kResolution) &&
              onPage(DisplayMenu::kCrt, DisplayMenu::kRetro) && onPage(DisplayMenu::kCalibration, DisplayMenu::kColor),
          "render size, spatial upscaling, scanlines and calibration each have "
          "one logical home");
    Check(onPage(DisplayMenu::kRayShadows, DisplayMenu::kLighting) && onPage(DisplayMenu::kMaterialMaps, DisplayMenu::kPacks) &&
              onPage(DisplayMenu::kBlood, DisplayMenu::kColor) && onPage(DisplayMenu::kRenderInterpolation, DisplayMenu::kTemporal),
          "lighting, pack, color and motion settings are grouped by their effect");
    auto adjust = [&](DisplayMenu::Page tab, int row, bool reverse = false) {
        page.SetPage(tab);
        page.Update(Point(620, DisplayMenu::RowY(row, tab) * 24 + 22, !reverse, reverse), v.display, snd);
    };
    page.SetGraphicsCapabilities({});
    page.SetPage(DisplayMenu::kResolution);
    v = {};
    page.Update(Pad(0x20), v.display, snd);
    page.Update(Pad(0x20), v.display, snd);
    page.Update(Pad(8), v.display, snd);
    Check(!v.display.dynamicResolution, "unsupported GPU timing cannot enable dynamic resolution");
    port::GraphicsCapabilities timedCaps;
    timedCaps.gpuTiming = true;
    page.SetGraphicsCapabilities(timedCaps);
    page.SetTemporalInputActive(true);
    page.Update(Pad(8), v.display, snd);
    Check(!v.display.dynamicResolution, "active temporal reconstruction or rays block dynamic resolution");
    page.SetTemporalInputActive(false);
    page.Update(Pad(8), v.display, snd);
    Check(v.display.dynamicResolution, "timestamp-capable raster renderer allows dynamic resolution");
    v.display.dynamicResolution = false;
    v.display.gpuBudgetFps = 144;
    adjust(DisplayMenu::kResolution, DisplayMenu::kGpuBudget);
    Check(v.display.gpuBudgetFps == 144 && Contains(page.RowHelp(v.display, DisplayMenu::kGpuBudget), "dynamic"),
          "inactive dynamic-resolution target stays saved and explains its "
          "requirement");
    v.display.dynamicResolution = true;
    adjust(DisplayMenu::kResolution, DisplayMenu::kGpuBudget);
    Check(v.display.gpuBudgetFps == 60, "enabling dynamic resolution unlocks its FPS target");
    v.display.resolutionMode = 1;
    page.SetRenderDimensions(1920, 1080);
    const int savedRenderScale = v.display.renderScale;
    adjust(DisplayMenu::kResolution, DisplayMenu::kRender);
    Check(v.display.renderScale == savedRenderScale, "window-relative resolution leaves the saved fixed scale untouched");
    Check(Contains(page.RowHelp(v.display, DisplayMenu::kRender), "1920 x 1080"),
          "automatic render scale still explains its current internal pixel dimensions");

    page.SetGraphicsCapabilities(fullCaps);
    v = {};
    v.display.graphics.hdrPaperWhite = 240;
    adjust(DisplayMenu::kColor, DisplayMenu::kPaperWhite);
    Check(v.display.graphics.hdrPaperWhite == 240 && Contains(page.RowHelp(v.display, DisplayMenu::kPaperWhite), "hdr"),
          "paper white retains its choice and explains that HDR must be enabled");
    v.display.graphics.hdrOutput = true;
    adjust(DisplayMenu::kColor, DisplayMenu::kPaperWhite);
    Check(v.display.graphics.hdrPaperWhite == 260, "HDR enables reference-white editing");
    v.display.graphics.syncInterval = 1;
    v.display.graphics.allowTearing = true;
    adjust(DisplayMenu::kPerformance, DisplayMenu::kTearing);
    Check(v.display.graphics.allowTearing && Contains(page.RowHelp(v.display, DisplayMenu::kTearing), "vsync"),
          "VSync disables tearing adjustment without clearing the saved choice");
    v.display.graphics.syncInterval = 0;
    adjust(DisplayMenu::kPerformance, DisplayMenu::kTearing);
    Check(!v.display.graphics.allowTearing, "VSync Off enables tearing adjustment");

    v = {};
    v.display.temporalQuality = 2;
    adjust(DisplayMenu::kTemporal, DisplayMenu::kTemporalQuality);
    Check(v.display.temporalQuality == 2 && Contains(page.RowHelp(v.display, DisplayMenu::kTemporalQuality), "upscal"),
          "upscaling quality stays saved while upscaling is Off and explains the "
          "dependency");
    v.display.temporalUpscaler = 2;
    adjust(DisplayMenu::kTemporal, DisplayMenu::kTemporalQuality);
    Check(v.display.temporalQuality == 0, "FSR 2 quality skips unavailable Native AA when advancing");
    adjust(DisplayMenu::kTemporal, DisplayMenu::kTemporalQuality, true);
    Check(v.display.temporalQuality == 2, "FSR 2 quality skips unavailable Native AA when reversing");
    v.display.temporalUpscaler = 1;
    v.display.temporalQuality = 3;
    adjust(DisplayMenu::kTemporal, DisplayMenu::kTemporalUpscaler);
    Check(v.display.temporalUpscaler == 2 && v.display.temporalQuality != 3,
          "switching from native AA to FSR 2 cannot retain an unsupported "
          "quality request");
    for (int requestedRenderer : {0, 1}) {
        v.display.renderer = 2;
        v.display.temporalUpscaler = 4;
        v.display.frameGeneration = 2;
        v.display.rayShadows = true;
        adjust(DisplayMenu::kTemporal, DisplayMenu::kRenderer, requestedRenderer == 1);
        Check(v.display.renderer == requestedRenderer && v.display.temporalUpscaler == 0 && v.display.frameGeneration == 0 &&
                  !v.display.rayShadows,
              "selecting another renderer clears only requests that would silently "
              "force Direct3D 12 again");
    }

    v = {};
    v.display.frameCap = 144;
    v.display.renderInterpolation = true;
    page.SetFrameGenerationActive(true);
    adjust(DisplayMenu::kPerformance, DisplayMenu::kFrameCap);
    Check(v.display.frameCap == 144 && Contains(page.RowHelp(v.display, DisplayMenu::kFrameCap), "60"),
          "active frame generation locks actual rendering to 60 without "
          "overwriting the normal frame cap");
    game::WorldHud optionsHud;
    page.ModernHud(v.display, optionsHud);
    const float capTop = DisplayMenu::RowY(DisplayMenu::kFrameCap, DisplayMenu::kPerformance) * 24 + 10;
    Check(std::any_of(optionsHud.menuText.begin(), optionsHud.menuText.end(),
                      [&](const auto& text) { return text.x == 402 && text.y == capTop && Contains(text.text, "60"); }),
          "frame-cap row displays actual frame-generation pacing instead of its "
          "inactive saved cap");
    adjust(DisplayMenu::kTemporal, DisplayMenu::kRenderInterpolation);
    Check(v.display.renderInterpolation && Contains(page.RowHelp(v.display, DisplayMenu::kRenderInterpolation), "frame generation"),
          "active frame generation disables pose interpolation while preserving "
          "its preference");
    page.SetFrameGenerationActive(false);
    v.display.frameGeneration = 2;  // Pending restart; no generated frames are active yet.
    adjust(DisplayMenu::kPerformance, DisplayMenu::kFrameCap);
    Check(v.display.frameCap == 165,
          "a pending frame-generation choice does not "
          "pretend generation is already active");
    adjust(DisplayMenu::kTemporal, DisplayMenu::kRenderInterpolation);
    Check(!v.display.renderInterpolation, "inactive frame generation leaves high-rate pose interpolation editable");
    v.display.frameCap = 60;
    adjust(DisplayMenu::kTemporal, DisplayMenu::kRenderInterpolation);
    Check(!v.display.renderInterpolation,
          "pose interpolation explains and respects the need for rendering above "
          "60 FPS");

    v = {};
    page.SetActivePackName("Active visual pack");
    page.SetActivePackFeatures(0, 0);
    adjust(DisplayMenu::kPacks, DisplayMenu::kMaterialMaps);
    Check(!v.display.graphics.materialMaps && Contains(page.RowHelp(v.display, DisplayMenu::kMaterialMaps), "pack"),
          "material maps require matching content in the active pack");
    adjust(DisplayMenu::kPacks, DisplayMenu::kMeshRefinement);
    Check(v.display.graphics.meshRefinement == 0 && Contains(page.RowHelp(v.display, DisplayMenu::kMeshRefinement), "pack"),
          "mesh refinement requires selected models in the active pack");
    v.display.graphics.materialMaps = true;
    v.display.graphics.meshRefinement = 2;
    adjust(DisplayMenu::kPacks, DisplayMenu::kMaterialMaps);
    adjust(DisplayMenu::kPacks, DisplayMenu::kMeshRefinement);
    Check(v.display.graphics.materialMaps && v.display.graphics.meshRefinement == 2,
          "an inactive pack does not silently erase its saved visual preferences");
    page.SetActivePackFeatures(4, 2);
    adjust(DisplayMenu::kPacks, DisplayMenu::kMaterialMaps);
    adjust(DisplayMenu::kPacks, DisplayMenu::kMeshRefinement);
    Check(!v.display.graphics.materialMaps && v.display.graphics.meshRefinement == 3,
          "active pack content enables its material and mesh controls");
    auto lightingCaps = fullCaps;
    lightingCaps.shadowMaps = true;
    page.SetGraphicsCapabilities(lightingCaps);
    v.display.graphics.materialMaps = true;
    v.display.graphics.perPixelLighting = false;
    adjust(DisplayMenu::kLighting, DisplayMenu::kPerPixel);
    const bool lightingEnabledWithMaps = v.display.graphics.perPixelLighting;
    adjust(DisplayMenu::kLighting, DisplayMenu::kPerPixel);
    Check(lightingEnabledWithMaps && !v.display.graphics.perPixelLighting,
          "material-only pixel lighting leaves the global preference editable for unmapped surfaces");
    v.display.graphics.shadowMaps = 1;
    adjust(DisplayMenu::kLighting, DisplayMenu::kPerPixel);
    Check(!v.display.graphics.perPixelLighting && Contains(page.RowHelp(v.display, DisplayMenu::kPerPixel), "shadow"),
          "raster shadows explain automatic pixel lighting without overwriting its independent preference");

    v = {};
    page.SetPage(DisplayMenu::kLighting);
    const float rayY = DisplayMenu::RowY(DisplayMenu::kRayShadows, DisplayMenu::kLighting) * 24 + 22;
    page.SetRaytracingAvailable(false);
    page.Update(Point(620, rayY, true), v.display, snd);
    Check(!v.display.rayShadows, "missing native ray capability cannot enable shadows");
    v.display.rayShadows = true;
    page.Update(Point(620, rayY, true), v.display, snd);
    Check(!v.display.rayShadows, "unsupported saved ray request can be cleared after native renderer fallback");
    page.SetRaytracingAvailable(true);
    page.Update(Point(620, rayY, true), v.display, snd);
    Check(v.display.rayShadows && v.display.renderer == 2, "available ray shadows select the native renderer");
    page.Update(Point(620, rayY, true), v.display, snd);
    Check(!v.display.rayShadows, "ray shadows can be disabled again");
    v.display.graphics.crtCurvature = 2;
    page.SetPage(DisplayMenu::kTemporal);
    const float fgY = DisplayMenu::RowY(DisplayMenu::kFrameGeneration, DisplayMenu::kTemporal) * 24 + 22;
    page.Update(Point(620, fgY, true), v.display, snd);
    Check(v.display.frameGeneration == 1 && v.display.graphics.crtCurvature == 2,
          "pending DLSS frame generation preserves curvature until the provider actually starts");
    page.Update(Point(620, fgY, true), v.display, snd);
    Check(v.display.frameGeneration == 2 && v.display.graphics.crtCurvature == 2,
          "pending AMD frame generation preserves the active image's curvature too");
    page.Update(Point(620, fgY, true), v.display, snd);
    Check(v.display.frameGeneration == 0 && v.display.graphics.crtCurvature == 2,
          "cancelling a pending frame-generation choice leaves curvature unchanged");
    adjust(DisplayMenu::kColor, DisplayMenu::kCalibration);
    Check(page.CalibrationPattern() == 0, "calibration opens a full-image black-level reference");
    page.Update(Pad(0x10), v.display, snd);
    Check(v.display.graphics.brightness == 55, "black reference adjusts actual saved brightness");
    page.Update(Pad(2), v.display, snd);
    Check(page.CalibrationPattern() == -1 && page.IsOpen(), "calibration Back returns to settings without closing it");
    input::PlayerState pointer;
    pointer.connected = pointer.onScreen = true;
    pointer.x = .5f;
    pointer.y = .5f;
    auto aim = play::DisplayPointer(pointer, 3);
    Check(aim.onScreen && aim.x == .5f && aim.y == .5f, "curved pointer preserves center aim");
    pointer.x = pointer.y = 0;
    aim = play::DisplayPointer(pointer, 3);
    Check(!aim.onScreen && aim.x < 0 && aim.y < 0, "curved black corners become offscreen shots");
    Check(play::DisplayPointer(pointer, 0).x == pointer.x, "Off preserves the original local pointer");
    enum class FakeRenderer { Native, Optional };
    std::vector<FakeRenderer> attempts;
    bool fallbackUsed = false;
    std::string factoryError, factoryNotice;
    auto factory = [&](FakeRenderer choice, std::string* failure) -> std::unique_ptr<int> {
        attempts.push_back(choice);
        if (choice == FakeRenderer::Optional) {
            *failure = "controlled unsupported device";
            return {};
        }
        return std::make_unique<int>(42);
    };
    auto device = play::CreateRequestedRenderer(FakeRenderer::Optional, FakeRenderer::Native, true, factory, &fallbackUsed, &factoryError,
                                                &factoryNotice);
    Check(device && fallbackUsed && attempts == std::vector<FakeRenderer>{FakeRenderer::Optional, FakeRenderer::Native},
          "saved optional renderer failure creates the native fallback exactly once");
    Check(factoryError.empty() && factoryNotice.find("controlled unsupported device") != std::string::npos &&
              factoryNotice.find("saved choices retained") != std::string::npos,
          "startup fallback retains original reason and requested preferences");
    attempts.clear();
    device = play::CreateRequestedRenderer(FakeRenderer::Optional, FakeRenderer::Native, false, factory, &fallbackUsed, &factoryError,
                                           &factoryNotice);
    Check(!device && !fallbackUsed && attempts.size() == 1 && factoryError == "controlled unsupported device",
          "explicit CLI renderer/vendor failure remains fatal without silent fallback");
    attempts.clear();
    device = play::CreateRequestedRenderer(FakeRenderer::Native, FakeRenderer::Native, true, factory, &fallbackUsed, &factoryError,
                                           &factoryNotice);
    Check(device && !fallbackUsed && attempts.size() == 1, "working native startup does not create a second renderer");
    auto failBoth = [](FakeRenderer choice, std::string* failure) -> std::unique_ptr<int> {
        *failure = choice == FakeRenderer::Native ? "native unavailable" : "optional unavailable";
        return {};
    };
    device = play::CreateRequestedRenderer(FakeRenderer::Optional, FakeRenderer::Native, true, failBoth, &fallbackUsed, &factoryError,
                                           &factoryNotice);
    Check(!device && !fallbackUsed && factoryError.find("optional unavailable") != std::string::npos &&
              factoryError.find("native unavailable") != std::string::npos,
          "dual device failure reports both original and fallback errors");
    printf("%d / %d checks passed\n", g_checks - g_failed, g_checks);
    return g_failed == 0 ? 0 : 1;
}
