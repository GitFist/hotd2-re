// Three-action pause menu, unified scrolling Options, safe resets, capability dependencies and settings persistence.
// No game data needed; the save-backed gameplay bridge has its own integration test.
#include "port/play/pause_menu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "game/menu_audio.h"
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
    d.bloodColor = nightShift ? port::BloodColor::Purple : port::BloodColor::Blue;
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

void Focus(play::DisplayMenu& menu, int row) {
    for (int p = 0; p < play::DisplayMenu::kPageCount; ++p) {
        const auto page = static_cast<play::DisplayMenu::Page>(p);
        const auto& rows = play::DisplayMenu::Rows(page);
        if (std::find(rows.begin(), rows.end(), row) != rows.end()) {
            menu.SetPage(page);
            menu.FocusRow(row);
            return;
        }
    }
    Check(false, "setting has an actual category");
}
float PointerY(const play::DisplayMenu& menu, int row) {
    return play::DisplayMenu::RowY(row, menu.CurrentPage(), menu.ScrollOffset()) * 24 + 22;
}

void CheckOptionSounds() {
    using play::DisplayMenu;
    for (bool modern : {false, true}) {
        PauseMenu pause;
        pause.SetModern(modern);
        pause.Open();
        play::Settings settings;
        std::vector<uint32_t> sounds;
        auto input = [&](const PauseMenu::Input& in) {
            sounds.clear();
            return pause.Update(in, settings, sounds);
        };
        auto selected = [&] { return sounds == std::vector<uint32_t>{game::kOptionSelectSound}; };
        auto backed = [&] { return sounds == std::vector<uint32_t>{game::kOptionBackSound}; };
        auto y = [&](int row) { return modern ? PauseMenu::ModernRowTop(row) + 12 : PauseMenu::RowY(row) * 24 + 22; };
        Check(PauseMenu::kRowCount == 3 && pause.Cursor() == PauseMenu::kResume, "pause opens on Resume with three actions");
        for (int row = 0; row < PauseMenu::kRowCount; ++row) {
            input(Pad(0x20));
            Check(sounds.empty(), "pause keyboard navigation is silent");
            input(Point(300, y(row)));
            Check(pause.Cursor() == row && sounds.empty(), "pause pointer navigation matches each visible row and stays silent");
            Check(modern ? PauseMenu::ModernRowAt(112, y(row)) == row : PauseMenu::RowAt(40, y(row)) == row,
                  "both pause layouts retain matching visible hit targets");
        }
        Check(input(Point(5, 450, true)) == Action::None && !pause.Confirming(), "blank pause shot cannot choose an action");
        input(Point(300, y(PauseMenu::kOptions), true));
        Check(pause.DisplayOpen() && selected(), "pause Options opens the shared categories with one select cue");
        input(Pad(2));
        Check(!pause.DisplayOpen() && pause.IsOpen() && backed(), "Options Back returns to pause with one back cue");
        input(Point(300, y(PauseMenu::kReturn), true));
        Check(pause.Confirming() && !pause.ConfirmYes() && selected(), "return confirmation opens on safe Cancel");
        input(Pad(0x40));
        Check(pause.ConfirmYes() && sounds.empty(), "confirmation choice navigation stays silent");
        input(Pad(2));
        Check(!pause.Confirming() && backed(), "Back cancels the return confirmation");
        input(Point(300, y(PauseMenu::kReturn), true));
        input(Pad(8));
        Check(!pause.Confirming() && backed(), "confirming safe Cancel uses the back cue");
        input(Point(300, y(PauseMenu::kReturn), true));
        input(Pad(0x40));
        Check(input(Pad(8)) == Action::ReturnToTitle && selected(), "confirmed return uses existing title/lobby action");
        pause.Open();
        Check(input(Pad(8)) == Action::Resume && backed(), "Resume uses the back cue");
        pause.Open();
        Check(input(Pad(2)) == Action::Resume && backed(), "pause Back resumes");
        pause.Open();
        Check(input(Point(5, 450, false, true)) == Action::Resume && backed(), "pause reload resumes");
        pause.Open();
        input(Point(300, y(PauseMenu::kOptions), true));
        auto preview = Pad(0); preview.preview = true;
        input(preview);
        const auto beforeHidden = settings;
        input(Pad(0xA0));
        Check(pause.Display().UiHidden() && settings == beforeHidden, "hidden shared options ignore edits");
        auto start = Pad(0); start.start = true;
        Check(input(start) == Action::None && !pause.Display().UiHidden() && pause.DisplayOpen() && backed(),
              "Start first restores hidden options rather than resuming");
        Check(input(start) == Action::Resume && !pause.DisplayOpen() && backed(), "Start resumes from visible shared options");
        pause.Open(); pause.SetNetworkRoom(true);
        input(Point(300, y(PauseMenu::kReturn), true));
        game::WorldHud hud;
        pause.ModernHud(1, settings, hud);
        Check(std::any_of(hud.menuText.begin(), hud.menuText.end(), [](const auto& text) {
                  return Contains(text.text, "both players") && Contains(text.text, "lobby");
              }), "online return confirmation explains that both players return to the lobby");
    }
}

void CheckGraphicsReset() {
    using play::DisplayMenu;
    Check(DisplayMenu::ResetChoiceAt(128, 278) == 1 && DisplayMenu::ResetChoiceAt(303, 315) == 1 &&
              DisplayMenu::ResetChoiceAt(336, 278) == 0 && DisplayMenu::ResetChoiceAt(511, 315) == 0 &&
              DisplayMenu::ResetChoiceAt(320, 295) == -1 && DisplayMenu::ResetChoiceAt(512, 295) == -1,
          "reset/cancel targets are bounded and separated by an inert gap");
    for (bool theme : {false, true}) {
        DisplayMenu menu;
        menu.SetNightShiftActive(theme);
        menu.SetPacks({{"Original", "", "", true}, {"Custom", "custom_visual_pack", "", true}}, "custom_visual_pack");
        play::Settings settings;
        settings.display = CustomizedGraphics(theme);
        settings.master = 2; settings.music = 3; settings.guns = 7; settings.sfx = 0; settings.attraction = 4;
        settings.input = "touch"; settings.assets = "custom_visual_pack"; settings.lobby = "127.0.0.1:27910";
        const auto before = settings;
        std::vector<uint32_t> sounds;
        auto input = [&](const PauseMenu::Input& in) { sounds.clear(); return menu.Update(in, settings.display, sounds); };
        auto open = [&] { Focus(menu, DisplayMenu::kResetGraphics); input(Pad(8)); };
        menu.Open();
        Focus(menu, DisplayMenu::kResetGraphics);
        input(Pad(0x40)); input(Pad(0x80));
        Check(!menu.ResetConfirming() && settings == before, "Left/Right cannot open or apply graphics reset");
        open();
        Check(menu.ResetConfirming() && !menu.ResetSelected() && settings == before, "reset opens safely on Cancel");
        input(Pad(8));
        Check(!menu.ResetConfirming() && !menu.TakeGraphicsReset() && settings == before, "Enter on default Cancel preserves every setting");
        for (auto cancel : {Pad(2), Pad(0)}) {
            if (!cancel.pressed) cancel.start = true;
            open(); input(cancel);
            Check(!menu.ResetConfirming() && menu.IsOpen() && settings == before && !menu.TakeGraphicsReset(),
                  "Back and Start cancel graphics reset before closing or resuming");
        }
        open();
        auto preview = Pad(0); preview.preview = true;
        input(preview);
        auto category = Pad(0); category.pageDelta = 1;
        input(category); input(Point(320, 295, true));
        Check(menu.ResetConfirming() && !menu.UiHidden() && settings == before, "reset modal blocks preview, category changes and gap shots");
        input(Pad(0x40)); input(Pad(8));
        auto expected = play::DisplaySettings{};
        expected.nightShift = before.display.nightShift;
        expected.bloodColor = before.display.bloodColor;
        expected.showCrosshairs = before.display.showCrosshairs;
        Check(settings.display == expected && menu.TakeGraphicsReset() && !menu.TakeGraphicsReset(),
              "confirmed graphics reset restores all graphics and preserves gameplay cosmetics/menu theme");
        Check(settings.master == before.master && settings.music == before.music && settings.guns == before.guns &&
                  settings.sfx == before.sfx && settings.attraction == before.attraction && settings.input == before.input &&
                  settings.assets == before.assets && settings.lobby == before.lobby && menu.SelectedPackPath() == before.assets,
              "graphics reset preserves all audio, input, pack and network preferences");
        open(); input(Pad(0x40)); input(Pad(8));
        Check(settings.display == expected && menu.TakeGraphicsReset(), "reset at defaults still emits a persistence request");
        open(); menu.Close(); menu.Open();
        Check(!menu.ResetConfirming() && !menu.UiHidden(), "closing and reopening clears pending modal/preview state");
    }
}
}  // namespace

int main() {
    using play::DisplayMenu;
    play::Settings v;
    std::vector<uint32_t> snd;
    DisplayMenu page;
    Check(v.master == 5 && v.music == 5 && v.guns == 5 && v.sfx == 5 && v.attraction == 5,
          "all five audio levels default to five");
    Check(v.display.bloodColor == port::BloodColor::Red, "blood color still defaults to red");
    const bool typingIntro=play::TypingGameplayKeyboard(true,false,2,false);
    Check(typingIntro && play::WorldKeyboardBits(play::kEscapeKey,false,false,typingIntro)==8 &&
          play::WorldKeyboardBits(8,false,false,typingIntro)==4,
          "typing intro Escape pauses before automatic join while Enter stays a confirmation");
    Check(!play::TypingGameplayKeyboard(true,true,2,false) &&
          !play::TypingGameplayKeyboard(true,false,2,true) &&
          !play::TypingGameplayKeyboard(true,false,6,false) &&
          play::WorldKeyboardBits(play::kEscapeKey,false,true)==2,
          "typing reports, chapter results and chapter choice retain menu Back instead of gameplay pause");
    for (bool inGame : {false, true})
        for (bool activePlayer : {false, true}) {
            const uint32_t regularKeys = 0xF8u | play::kEscapeKey;
            const auto baseline = play::WorldKeyboardBits(regularKeys, inGame, activePlayer);
            Check(play::WorldKeyboardBits(regularKeys | play::kPreviewKey, inGame, activePlayer) == baseline &&
                      play::WorldKeyboardBits(play::kPreviewKey, inGame, activePlayer) == 0,
                  "preview shortcut never reaches gameplay input");
        }
    CheckGraphicsReset();
    // settings file round trip (volumes, input, display)
    const std::string path = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "\\hotd2re_settings_test.ini";
    {
        play::Settings defaults, restored;
        restored.master = restored.music = restored.guns = restored.sfx = restored.attraction = 10;
        Check(defaults.Save(path) && restored.Load(path) && restored.master == 5 && restored.music == 5 &&
                  restored.guns == 5 && restored.sfx == 5 && restored.attraction == 5,
              "all five audio defaults survive saving and reopening settings");
    }
    play::Settings a;
    a.master = 8;
    a.attraction = 4;
    a.music = 3;
    a.guns = 7;
    a.sfx = 0;
    a.input = "mouse";
    a.display.fullscreen = true;
    a.display.windowScale = 0;
    a.display.stretch = true;
    a.display.crt = 2;
    a.display.renderScale = 3;
    a.display.bloodColor = port::BloodColor::Purple;
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
        fputs("music=3\nguns=7\nsfx=0\n", f);
        fclose(f);
        play::Settings d;
        Check(d.Load(path) && d.music == 3 && d.guns == 7 && d.sfx == 0 && d.master == 5 && d.attraction == 5 &&
                  d.display == play::DisplaySettings{},
              "old settings file keeps saved categories and defaults missing master and attraction to five");
    }
    {
        FILE* f = fopen(path.c_str(), "w");
        fputs("master=99\nattraction=-3\n", f);
        fclose(f);
        play::Settings audio;
        Check(audio.Load(path) && audio.master == 10 && audio.attraction == 0, "added settings clamp numeric values to zero through ten");
        for (const char* invalid : {"", "invalid", "1junk", "2.0"}) {
            f = fopen(path.c_str(), "w");
            fprintf(f, "master=%s\nattraction=%s\n", invalid, invalid);
            fclose(f);
            audio.master = 7;
            audio.attraction = 2;
            Check(audio.Load(path) && audio.master == 7 && audio.attraction == 2,
                  "malformed audio settings preserve current master and attraction preferences");
        }
        for (int level : {0, 2, 10}) {
            audio.master = level;
            audio.attraction = level;
            play::Settings loaded;
            Check(audio.Save(path) && loaded.Load(path) && loaded.master == level && loaded.attraction == level,
                  "master and attraction levels survive saving and reopening settings");
        }
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
                  oldDisplay.display.crt == 2 && oldDisplay.display.renderScale == 3 &&
                  oldDisplay.display.bloodColor == port::BloodColor::Red,
              "previous display keys retain their meanings after rows move and "
              "labels change");
        Check(DisplayMenu::ValueText(oldDisplay.display, DisplayMenu::kFullscreen) == "Fullscreen" &&
                  DisplayMenu::ValueText(oldDisplay.display, DisplayMenu::kViewport) == "32:9" &&
                  DisplayMenu::ValueText(oldDisplay.display, DisplayMenu::kAspect) == "Stretch",
              "legacy display choices have consistent modern values");
    }
    for (const auto [color, name] : {std::pair{port::BloodColor::Red, "red"}, {port::BloodColor::Green, "green"},
                                    {port::BloodColor::Blue, "blue"}, {port::BloodColor::Purple, "purple"}}) {
        play::Settings saved, loaded;
        saved.display.bloodColor = color;
        loaded.display.bloodColor = color == port::BloodColor::Red ? port::BloodColor::Green : port::BloodColor::Red;
        Check(saved.Save(path) && loaded.Load(path) && loaded.display.bloodColor == color,
              "each blood color survives saving and reopening settings");
        FILE* f = fopen(path.c_str(), "w");
        fprintf(f, "blood=%s\n", name);
        fclose(f);
        loaded.display.bloodColor = color == port::BloodColor::Red ? port::BloodColor::Green : port::BloodColor::Red;
        Check(loaded.Load(path) && loaded.display.bloodColor == color,
              "blood key accepts the existing red/green values and new blue/purple values");
        for (const char* invalid : {"invalid", "", "Blue", "purplejunk", "1"}) {
            f = fopen(path.c_str(), "w");
            fprintf(f, "blood=%s\n", invalid);
            fclose(f);
            Check(loaded.Load(path) && loaded.display.bloodColor == color, "invalid blood value keeps each current choice");
        }
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
        fputs("music=7\n", f);
        fclose(f);
        play::Settings legacy;
        Check(legacy.Load(path) && legacy.display.showCrosshairs && legacy.music == 7 && legacy.master == 5 &&
                  legacy.guns == 5 && legacy.sfx == 5 && legacy.attraction == 5,
              "legacy files preserve present values and default every missing audio level to five");
    }
    remove(path.c_str());

    page.Open();
    page.SetPage(DisplayMenu::kGraphics);
    page.FocusRow(DisplayMenu::kPreset);
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
        page.SetPage(static_cast<DisplayMenu::Page>(tab));
        for (int row : rows) {
            page.FocusRow(row);
            const auto visible = page.VisibleRows();
            Check(std::find(visible.begin(), visible.end(), row) != visible.end(), "every category row can scroll fully into view");
            const float y = PointerY(page, row);
            Check(DisplayMenu::RowAt(40, y, page.CurrentPage(), page.ScrollOffset()) == row,
                  "scrolling keeps visible setting text and pointer targets aligned");
        }
        for (int row : rows)
            if (row != DisplayMenu::kBack) ++rowOccurrences[row];
    }
    bool eachSettingOnce = true;
    for (int row = 0; row < DisplayMenu::kRowCount; ++row)
        if (row != DisplayMenu::kBack && rowOccurrences[row] != 1) eachSettingOnce = false;
    Check(eachSettingOnce, "every setting appears on exactly one options page");
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
              onPage(DisplayMenu::kBlood, DisplayMenu::kGameplay) && onPage(DisplayMenu::kRenderInterpolation, DisplayMenu::kTemporal),
          "lighting, pack, color and motion settings are grouped by their effect");
    auto adjust = [&](DisplayMenu::Page, int row, bool reverse = false) {
        Focus(page, row);
        page.Update(Point(600, PointerY(page, row), !reverse, reverse), v.display, snd);
    };
    page.SetGraphicsCapabilities({});
    page.SetPage(DisplayMenu::kResolution);
    v = {};
    page.FocusRow(DisplayMenu::kDynamicResolution);
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
    const float capTop = DisplayMenu::RowY(DisplayMenu::kFrameCap, page.CurrentPage(), page.ScrollOffset()) * 24 + 10;
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
    page.FocusRow(DisplayMenu::kRayShadows);
    const float rayY = PointerY(page, DisplayMenu::kRayShadows);
    page.SetRaytracingAvailable(false);
    page.Update(Point(600, rayY, true), v.display, snd);
    Check(!v.display.rayShadows, "missing native ray capability cannot enable shadows");
    v.display.rayShadows = true;
    page.Update(Point(600, rayY, true), v.display, snd);
    Check(!v.display.rayShadows, "unsupported saved ray request can be cleared after native renderer fallback");
    page.SetRaytracingAvailable(true);
    page.Update(Point(600, rayY, true), v.display, snd);
    Check(v.display.rayShadows && v.display.renderer == 2, "available ray shadows select the native renderer");
    page.Update(Point(600, rayY, true), v.display, snd);
    Check(!v.display.rayShadows, "ray shadows can be disabled again");
    v.display.graphics.crtCurvature = 2;
    page.SetPage(DisplayMenu::kTemporal);
    page.FocusRow(DisplayMenu::kFrameGeneration);
    const float fgY = PointerY(page, DisplayMenu::kFrameGeneration);
    page.Update(Point(600, fgY, true), v.display, snd);
    Check(v.display.frameGeneration == 1 && v.display.graphics.crtCurvature == 2,
          "pending DLSS frame generation preserves curvature until the provider actually starts");
    page.Update(Point(600, fgY, true), v.display, snd);
    Check(v.display.frameGeneration == 2 && v.display.graphics.crtCurvature == 2,
          "pending AMD frame generation preserves the active image's curvature too");
    page.Update(Point(600, fgY, true), v.display, snd);
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
    CheckOptionSounds();
    printf("%d / %d checks passed\n", g_checks - g_failed, g_checks);
    return g_failed == 0 ? 0 : 1;
}
