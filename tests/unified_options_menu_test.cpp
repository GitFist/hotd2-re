#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "game/menu_audio.h"
#include "port/play/display_menu.h"

namespace {
using Menu = play::DisplayMenu;
int checks = 0;
void Require(bool ok, const char* why) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}
play::MenuInput Key(uint32_t key) {
    play::MenuInput input;
    input.pressed = key;
    return input;
}
play::MenuInput Point(float x, float y, bool shot = false) {
    play::MenuInput input;
    input.pointer = true;
    input.px = x;
    input.py = y;
    input.trigger = shot;
    return input;
}
bool Text(const game::WorldHud& hud, const std::string& text) {
    return std::any_of(hud.menuText.begin(), hud.menuText.end(), [&](const auto& item) { return item.text == text; });
}
float Top(const Menu& menu, int row) { return menu.RowTop(row); }
void LayoutAndReachability(bool modern, bool naomi, bool typing) {
    Menu menu;
    menu.SetNightShiftActive(modern);
    play::DisplaySettings settings;
    settings.graphics.naomiGraphics = naomi;
    menu.SetGraphicsMode(naomi);
    Menu::OptionValues options; options.typing = typing; menu.SetOptionValues(options);
    std::vector<uint32_t> sounds;
    menu.Open();
    const char* names[] = {"Display", "Graphics", "Gameplay", "Sound", "Devices", "CRT"};
    Require(Menu::kPageCount == 6, "six requested categories including Sound");
    std::vector<int> count(Menu::kRowCount);
    for (int category = 0; category < Menu::kPageCount; ++category) {
        const auto page = static_cast<Menu::Page>(category);
        menu.SetPage(page);
        for (int row : Menu::Rows(page, naomi, typing)) {
            ++count[row];
            menu.FocusRow(row);
            const float top = Top(menu, row);
            const auto visible = menu.VisibleRows();
            Require(std::find(visible.begin(), visible.end(), row) != visible.end(), "every option scrolls into the real viewport");
            Require(row == Menu::kBack ? top >= 429.9f && top + 24 <= 454.1f : top >= 125.9f && top + 24 <= 420.1f,
                    "settings and fixed Back remain inside their reserved regions");
            Require(Menu::RowAt(600, top + 12, page, menu.ScrollOffset(), naomi, typing) == row,
                    "scrolled pointer hit test selects the visible row");
            Require(Menu::RowAt(600, 117, page, menu.ScrollOffset(), naomi, typing) == -1,
                    "group headings and category gap do not change a hidden value");
            game::WorldHud hud;
            menu.ModernHud(settings, hud);
            for (const char* name : names) Require(Text(hud, name), "all six category headers remain present while scrolling");
            Require(Text(hud, Menu::Label(row)), "focused option is rendered at every scroll position");
            Require(Text(hud, "Back"), "Back stays visible while scrolling");
            Require(!Text(hud, "Game Options") && !Text(hud, "Advanced") && !Text(hud, "Resolution") && !Text(hud, "Packs"),
                    "obsolete menu categories are absent");
            sounds.clear();
            menu.Update(Key(0x20), settings, sounds);
            Require(sounds.empty(), "keyboard option navigation is silent");
        }
        const float tabX = Menu::TabLeft(page) + 8;
        Require(Menu::TabAt(tabX, 90) == category, "all category tabs have disjoint hit regions");
        sounds.clear();
        menu.Update(Point(tabX, 90, true), settings, sounds);
        Require(menu.CurrentPage() == page && sounds.empty(), "category switching is direct and silent");
    }
    for (int row = 0; row < Menu::kRowCount; ++row) {
        const bool hidden = (naomi && std::find(Menu::Rows(Menu::kGraphics).begin(), Menu::Rows(Menu::kGraphics).end(), row) != Menu::Rows(Menu::kGraphics).end() &&
                             std::find(Menu::Rows(Menu::kGraphics, true).begin(), Menu::Rows(Menu::kGraphics, true).end(), row) == Menu::Rows(Menu::kGraphics, true).end()) ||
                            (typing && (row == Menu::kCrosshairP1 || row == Menu::kCrosshairP2 || row == Menu::kCrosshairs));
        Require(count[row] == (row == Menu::kBack ? Menu::kPageCount : hidden ? 0 : 1), "each applicable setting has one editable location");
    }
    menu.SetPage(Menu::kGraphics);
    const int first = menu.Cursor();
    menu.Update(Point(628, 404, true), settings, sounds);
    Require(menu.ScrollOffset() > 0 && menu.Cursor() != first, "lightgun users can scroll using on-screen down control");
    sounds.clear();
    auto wheel = Key(0);
    wheel.scroll = 100;
    menu.Update(wheel, settings, sounds);
    Require(menu.ScrollOffset() == 0 && sounds.empty(), "wheel clamps at the first setting without feedback");
    auto tab = Key(0);
    tab.pageDelta = 1;
    menu.Update(tab, settings, sounds);
    Require(menu.CurrentPage() == Menu::kGameplay && sounds.empty(), "Page Down moves to the next real category silently");
}
void ScrollHints(bool modern) {
    Menu menu;
    menu.SetNightShiftActive(modern);
    play::DisplaySettings settings;
    std::vector<uint32_t> sounds;
    auto inactive = [&] { const auto hint=menu.ScrollHint();return !hint.above&&!hint.below; };
    Require(inactive(), "closed Options never advertise hidden settings");
    menu.Open();
    menu.SetPage(Menu::kGraphics);
    auto hint=menu.ScrollHint();
    Require(hint.left==26&&hint.top==126&&hint.right==614&&hint.bottom==420,
            "Options scroll cue is restricted to the actual settings viewport");
    Require(!hint.above&&hint.below, "long Options category initially indicates only content below");
    auto wheel=Key(0);wheel.scroll=-1;
    menu.Update(wheel,settings,sounds);
    hint=menu.ScrollHint();
    Require(hint.above&&hint.below, "middle of a long Options category indicates both directions");
    wheel.scroll=-100;
    menu.Update(wheel,settings,sounds);
    hint=menu.ScrollHint();
    Require(hint.above&&!hint.below, "last Options rows indicate only content above");
    wheel.scroll=100;
    menu.Update(wheel,settings,sounds);
    hint=menu.ScrollHint();
    Require(!hint.above&&hint.below, "returning to the first row removes the upper cue");
    for(auto page:{Menu::kSound,Menu::kDevices,Menu::kRetro}) {
        menu.SetPage(page);
        Require(inactive(), "short Options categories do not imply hidden rows");
    }
    menu.SetPage(Menu::kDisplay);
    menu.FocusRow(Menu::kResetGraphics);
    Require(menu.ScrollHint().above, "reset row is reached through real scrolling");
    menu.Update(Key(8),settings,sounds);
    Require(menu.ResetConfirming()&&inactive(), "reset modal hides both content scroll cues");
    menu.Update(Key(2),settings,sounds);
    Require(!menu.ResetConfirming()&&menu.ScrollHint().above, "cancel restores the underlying scroll position cue");
    auto preview=Key(0);preview.preview=true;
    menu.Update(preview,settings,sounds);
    Require(menu.UiHidden()&&inactive(), "Hide UI also hides decorative scroll cues");
    menu.Update(preview,settings,sounds);
    Require(!menu.UiHidden()&&menu.ScrollHint().above, "Show UI restores the current scroll cue");
    menu.FocusRow(Menu::kCalibration);
    menu.Update(Key(8),settings,sounds);
    Require(menu.CalibrationPattern()>=0&&inactive(), "calibration reference image has no list scroll cue");
    menu.Update(Key(2),settings,sounds);
    Require(menu.CalibrationPattern()<0&&menu.ScrollHint().above, "calibration Back restores the scrolled Options cue");
    menu.Close();
    Require(inactive(), "closing a scrolled Options page clears its cues");
}
void CompactFooter(bool modern) {
    Menu menu;
    menu.SetNightShiftActive(modern);
    play::DisplaySettings settings;
    settings.nightShift = modern;
    settings.graphics.naomiGraphics = false;
    std::vector<uint32_t> sounds;
    menu.Open();
    const std::string status = "Gameplay settings saved for the next run.";
    menu.SetStatus(status);
    for (int category = 0; category < Menu::kPageCount; ++category) {
        const auto page = static_cast<Menu::Page>(category);
        menu.SetPage(page);
        for (int row : Menu::Rows(page)) {
            menu.FocusRow(row);
            game::WorldHud hud;
            menu.ModernHud(settings, hud);
            Require(Text(hud, status), "save and restart status is visible above the Options list");
            const auto help = menu.RowHelp(settings, row);
            Require(help.empty() || !Text(hud, help), "normal Options does not render per-row tooltips");
            int cornerHints = 0, categoryHints = 0;
            for (const auto& text : hud.menuText) {
                Require(text.text.find("UP/DOWN: SELECT") == std::string::npos &&
                            text.text.find("WHEEL: SCROLL") == std::string::npos &&
                            text.text.find("PAGE UP/DOWN: CATEGORY") == std::string::npos,
                        "Options removes the old navigation guidance lines");
                if (text.text == "Hide UI [H]") {
                    ++cornerHints;
                    Require(text.x == 558 && text.y == 462 && text.scale > 0 && text.scale <= .31f &&
                                Menu::PreviewButtonAt(text.x, text.y),
                            "Hide UI is small and inside its bottom-right click target");
                } else if (text.text == "LB/L1 / RB/R1: CATEGORY") {
                    ++categoryHints;
                    Require(text.x == 28 && text.y == 461 && text.scale > 0 && text.scale <= .42f,
                            "controller category hint stays compact at the bottom left");
                } else {
                    Require(text.y < 456, "the list footer contains no large instruction or tooltip block");
                }
            }
            Require(cornerHints == 1, "every Options category retains one compact Hide UI control");
            Require(categoryHints == 1, "every Options category retains one compact controller category hint");
        }
    }
    menu.SetPage(Menu::kGraphics);
    Require(menu.VisibleRows().size() == 11, "expanded Graphics viewport shows ten settings plus fixed Back at once");
    menu.SetPage(Menu::kGameplay);
    Require(menu.VisibleRows() == Menu::Rows(Menu::kGameplay) && menu.ScrollOffset() == 0 &&
                !menu.ScrollHint().above && !menu.ScrollHint().below,
            "every Gameplay setting and Back fit without scrolling");
    Require(Menu::PreviewButtonAt(550, 456) && Menu::PreviewButtonAt(629.9f, 475.9f) &&
                !Menu::PreviewButtonAt(549.9f, 466) && !Menu::PreviewButtonAt(630, 466) &&
                !Menu::PreviewButtonAt(590, 455.9f) && !Menu::PreviewButtonAt(590, 476) &&
                !Menu::PreviewButtonAt(500, 445),
            "compact preview click bounds match the corner panel and reject the former large target");
    Require(Menu::RowAt(600, 466, menu.CurrentPage(), menu.ScrollOffset()) == -1,
            "corner preview target cannot also activate a settings row or Back");
    const auto before = settings;
    menu.Update(Point(590, 466, true), settings, sounds);
    Require(menu.UiHidden() && settings == before && sounds == std::vector<uint32_t>{game::kOptionSelectSound},
            "the compact corner button hides Options without editing settings");
    game::WorldHud hidden;
    menu.ModernHud(settings, hidden);
    Require(hidden.menuText.size() == 1 && hidden.menuText.front().text == "Show UI [H]" &&
                hidden.menuText.front().x == 558 && hidden.menuText.front().y == 462 &&
                hidden.menuText.front().scale > 0 && hidden.menuText.front().scale <= .31f,
            "hidden Options leaves only the equally small corner Show UI hint");
    Require(hidden.menuPanels.size() == 1 && hidden.menuPanels.front().left == 550 &&
                hidden.menuPanels.front().top == 456 && hidden.menuPanels.front().right == 630 &&
                hidden.menuPanels.front().bottom == 476,
            "hidden Options corner panel matches its pointer bounds exactly");
    sounds.clear();
    menu.Update(Point(590, 466, true), settings, sounds);
    Require(!menu.UiHidden() && menu.IsOpen() && settings == before &&
                sounds == std::vector<uint32_t>{game::kOptionSelectSound},
            "the same compact corner target restores Options without backing out");
}
void ValuesAndActions() {
    Menu menu;
    play::DisplaySettings display;
    display.graphics.naomiGraphics = false; // this fixture exercises the PC blood preference
    Menu::OptionValues values;
    for (int& volume : values.audio) volume = 5;
    values.difficulty = 2;
    values.lives = 3;
    values.credits = 9;
    values.gameplayEditable = true;
    values.freePlayUnlocked = false;
    menu.SetOptionValues(values);
    menu.Open();
    std::vector<uint32_t> sounds;
    auto press = [&](uint32_t key) {
        sounds.clear();
        return menu.Update(Key(key), display, sounds);
    };
    menu.SetPage(Menu::kSound);
    for (const auto [row, index] : {std::pair{Menu::kMasterVolume, 3}, {Menu::kMusicVolume, 0}, {Menu::kGunVolume, 1},
                                  {Menu::kEffectsVolume, 2}, {Menu::kAttractionVolume, 4}}) {
        menu.FocusRow(row);
        press(0x80);
        Require(sounds == std::vector<uint32_t>{game::kOptionSelectSound}, "each actual sound edit emits one select 74");
        Menu::OptionValues changed;
        Require(menu.TakeOptionChange(&changed) && changed.audio[index] == 6, "each sound row edits its matching mixer bus");
        Require(!menu.TakeOptionChange(&changed), "audio edit is consumed once");
    }
    values = menu.OptionValuesState();
    values.audio[3] = 10;
    menu.SetOptionValues(values);
    menu.FocusRow(Menu::kMasterVolume);
    press(0x80);
    Require(sounds.empty(), "volume bounds stay silent");
    Menu::OptionValues change;
    Require(!menu.TakeOptionChange(&change), "volume bounds create no save request");
    menu.SetPage(Menu::kGameplay);
    menu.FocusRow(Menu::kDifficulty);
    press(0x80);
    Require(menu.TakeOptionChange(&change) && change.difficulty == 3, "difficulty remains a real saved gameplay edit");
    menu.FocusRow(Menu::kContinues);
    press(0x80);
    Require(menu.TakeOptionChange(&change) && change.credits == 0, "Unlimited is selectable without completion unlocks");
    { game::WorldHud hud; menu.ModernHud(display, hud); Require(Text(hud, "Unlimited"), "unlimited credits have a clear value label"); }
    press(0x80);
    Require(menu.TakeOptionChange(&change) && change.credits == 1, "Unlimited cycles forward to one credit");
    press(0x40);
    Require(menu.TakeOptionChange(&change) && change.credits == 0, "one credit cycles backward to Unlimited");
    press(0x40);
    Require(menu.TakeOptionChange(&change) && change.credits == 9, "Unlimited cycles backward to nine credits");
    values = menu.OptionValuesState();
    values.freePlayUnlocked = true;
    values.credits = 9;
    menu.SetOptionValues(values);
    press(0x80);
    Require(menu.TakeOptionChange(&change) && change.credits == 0, "existing Free Play availability still selects Unlimited");
    menu.FocusRow(Menu::kCrosshairP2);
    press(0x80);
    Require(menu.TakeOptionChange(&change) && change.crosshair[1] == 1 && change.crosshair[0] == 0,
            "player sights edit independently");
    values = menu.OptionValuesState();
    values.gameplayEditable = false;
    menu.SetOptionValues(values);
    for (int row : {Menu::kDifficulty, Menu::kLives, Menu::kContinues, Menu::kCrosshairP1, Menu::kCrosshairP2, Menu::kResetGameplay}) {
        menu.FocusRow(row);
        press(8);
        Require(sounds.empty() && !menu.TakeOptionChange(&change) && !menu.ResetConfirming(),
                "saved gameplay controls stay locked during a run or network room");
        Require(menu.RowHelp(display, row).find("locked") != std::string::npos, "locked gameplay settings explain when to change them");
    }
    menu.FocusRow(Menu::kCrosshairs);
    press(8);
    Require(!display.showCrosshairs && sounds == std::vector<uint32_t>{game::kOptionSelectSound},
            "local gameplay sight visibility remains editable during a run");
    menu.FocusRow(Menu::kBlood);
    Require(display.bloodColor == port::BloodColor::Red, "local blood appearance defaults to red");
    for (uint32_t key : {8u, 0x80u}) {
        for (const auto [color, label] : {std::pair{port::BloodColor::Green, "Green"}, {port::BloodColor::Blue, "Blue"},
                                         {port::BloodColor::Purple, "Purple"}, {port::BloodColor::Red, "Red"}}) {
            press(key);
            Require(display.bloodColor == color && Menu::ValueText(display, Menu::kBlood) == label &&
                        sounds == std::vector<uint32_t>{game::kOptionSelectSound},
                    "confirm and Right cycle every blood color forward with its label and one select sound during a run");
            game::WorldHud bloodHud;
            menu.ModernHud(display, bloodHud);
            Require(Text(bloodHud, label), "modern options show the selected blood color");
        }
    }
    for (auto color : {port::BloodColor::Purple, port::BloodColor::Blue, port::BloodColor::Green, port::BloodColor::Red}) {
        press(0x40);
        Require(display.bloodColor == color && sounds == std::vector<uint32_t>{game::kOptionSelectSound},
                "Left cycles all blood colors backward and wraps from red to purple");
    }
    const float bloodY = Top(menu, Menu::kBlood) + 12;
    sounds.clear();
    menu.Update(Point(360, bloodY, true), display, sounds);
    Require(display.bloodColor == port::BloodColor::Purple, "pointer left arrow wraps red to purple");
    menu.Update(Point(600, bloodY, true), display, sounds);
    Require(display.bloodColor == port::BloodColor::Red, "pointer right arrow wraps purple to red");
    auto reloadBlood = Point(500, bloodY);
    reloadBlood.reload = true;
    menu.Update(reloadBlood, display, sounds);
    Require(display.bloodColor == port::BloodColor::Purple, "reload cycles blood backward during a run");
    menu.SetPage(Menu::kDevices);
    for (const auto [row, action] : {std::pair{Menu::kLightguns, Menu::DeviceAction::Lightguns},
                                   {Menu::kWiiRemotes, Menu::DeviceAction::WiiRemotes}}) {
        menu.FocusRow(row);
        press(0x80);
        Require(sounds.empty() && menu.TakeDeviceAction() == Menu::DeviceAction::None,
                "moving left/right on a device action does not open it");
        press(8);
        Require(menu.TakeDeviceAction() == action && menu.TakeDeviceAction() == Menu::DeviceAction::None,
                "device action opens the intended page once");
        Require(sounds == std::vector<uint32_t>{game::kOptionSelectSound}, "opening a device page plays one select 74");
    }
    menu.SetPage(Menu::kDisplay);
    menu.FocusRow(Menu::kResetGraphics);
    display.renderScale = 4;
    display.nightShift = false;
    press(8);
    Require(menu.ResetConfirming() && !menu.ResetSelected(), "graphics reset starts on Cancel");
    press(0x40);
    press(8);
    Require(menu.TakeGraphicsReset() && display.renderScale == play::DisplaySettings{}.renderScale,
            "graphics reset restores rendering defaults");
    Require(display.bloodColor == port::BloodColor::Purple && !display.showCrosshairs && !display.nightShift,
            "graphics reset preserves Gameplay cosmetics and chosen menu theme");
    values.gameplayEditable = true;
    menu.SetOptionValues(values);
    menu.SetPage(Menu::kGameplay);
    menu.FocusRow(Menu::kResetGameplay);
    const auto before = display;
    press(8);
    game::WorldHud modal;
    menu.ModernHud(display, modal);
    Require(Text(modal, "RESET GAMEPLAY?") && !Text(modal, "RESET ALL GRAPHICS?"), "gameplay reset has its own scoped confirmation");
    press(0x40);
    press(8);
    Require(menu.TakeGameplayReset() && !menu.TakeGameplayReset() && display == before,
            "gameplay reset requests the world's authoritative defaults without resetting graphics");
    menu.FocusRow(Menu::kBack);
    Require(press(8) && sounds == std::vector<uint32_t>{game::kOptionBackSound}, "fixed Back closes with one 76");
}
void NaomiGraphicsChoices() {
    Menu menu;
    play::DisplaySettings d;
    Require(d.graphics.naomiGraphics && d.EffectiveBloodColor() == port::BloodColor::Red && !port::GraphicsOptions{}.naomiGraphics,
            "player graphics default to Naomi/Red without changing raw renderer defaults");
    d.graphics.naomiGraphics = false; // exercise a deliberate saved PC choice
    d.bloodColor = port::BloodColor::Purple;
    d.graphics.perPixelLighting = true;
    d.graphics.textureFilter = 2;
    d.graphics.anisotropy = 16;
    d.graphics.bloom = 2;
    d.graphics.msaa = 4;
    d.graphics.alphaToCoverage = true;
    d.graphics.postAA = 2;
    const auto pc = d;
    std::vector<uint32_t> sounds;
    menu.Open();
    menu.SetPage(Menu::kGraphics);
    menu.FocusRow(Menu::kGraphicsMode);
    menu.Update(Key(0x80), d, sounds);
    Require(d == pc && sounds.empty(), "missing Naomi content cannot enable a partial graphics mode");
    Require(menu.RowHelp(d, Menu::kGraphicsMode).find("unavailable") != std::string::npos,
            "missing Naomi content has a player-facing explanation");
    menu.SetNaomiAvailability(true);
    port::GraphicsCapabilities capabilities;capabilities.msaa2=capabilities.msaa4=capabilities.smaa=capabilities.cmaa2=true;
    menu.SetGraphicsCapabilities(capabilities);
    menu.Update(Key(0x80), d, sounds);
    Require(d.graphics.naomiGraphics && d.EffectiveBloodColor() == port::BloodColor::Red,
            "enabling Naomi uses its independent Red default");
    Require(d.bloodColor == pc.bloodColor && d.graphics.perPixelLighting && d.graphics.textureFilter == 2 && d.graphics.anisotropy == 16,
            "mode switch retains existing PC blood and scene preferences");
    for (int hidden : {Menu::kFiltering, Menu::kAniso, Menu::kMsaa, Menu::kCoverage, Menu::kPerPixel,
                       Menu::kSoftParticles, Menu::kAmbientOcclusion, Menu::kShadowMaps, Menu::kRayShadows,
                       Menu::kFog, Menu::kReflections, Menu::kCinematicFocus}) {
        const auto& rows = menu.CurrentRows();
        Require(std::find(rows.begin(), rows.end(), hidden) == rows.end(), "Naomi omits inapplicable PC scene controls");
        const int cursor = menu.Cursor(); menu.FocusRow(hidden);
        Require(menu.Cursor() == cursor, "hidden controls cannot receive keyboard focus");
    }
    Require(d.graphics.msaa == 4 && d.graphics.alphaToCoverage,
            "hiding controls retains PC preferences");
    menu.FocusRow(Menu::kPostAA);sounds.clear();menu.Update(Key(0x80),d,sounds);
    Require(d.graphics.postAA==3 && sounds==std::vector<uint32_t>{game::kOptionSelectSound},
            "Post AA remains editable in Naomi mode");
    menu.SetPage(Menu::kGameplay);
    menu.FocusRow(Menu::kBlood);
    menu.Update(Key(0x80), d, sounds);
    Require(d.naomiBloodColor == port::BloodColor::Green && d.bloodColor == port::BloodColor::Purple,
            "changing Naomi blood edits only the Naomi preference");
    Menu::Step(d, Menu::kGraphicsMode, 1);
    Require(!d.graphics.naomiGraphics && d.EffectiveBloodColor() == port::BloodColor::Purple,
            "PC blood returns immediately when switching back");
    Menu::Step(d, Menu::kGraphicsMode, 1);
    Require(d.EffectiveBloodColor() == port::BloodColor::Green, "Naomi blood choice returns on the next switch");
    Menu typingMenu;
    typingMenu.SetNaomiAvailability(true);
    typingMenu.SetGraphicsCapabilities(capabilities);
    typingMenu.Open();
    typingMenu.SetPage(Menu::kGraphics);
    typingMenu.FocusRow(Menu::kGraphicsMode);
    game::WorldHud hud;
    typingMenu.ModernHud(d, hud);
    Require(Text(hud, "Naomi"), "another game menu shows the shared Naomi choice");
    typingMenu.Update(Key(0x80), d, sounds);
    Require(!d.graphics.naomiGraphics && d.EffectiveBloodColor() == port::BloodColor::Purple,
            "Typing can select PC and restore that profile's saved blood");
    typingMenu.Update(Key(0x80), d, sounds);
    Require(d.graphics.naomiGraphics && d.EffectiveBloodColor() == port::BloodColor::Green,
            "Typing can select Naomi with its saved blood color");
    typingMenu.SetPage(Menu::kGameplay);
    typingMenu.FocusRow(Menu::kBlood);
    typingMenu.Update(Key(0x80), d, sounds);
    Require(d.bloodColor == port::BloodColor::Purple && d.naomiBloodColor == port::BloodColor::Blue,
            "Typing edits the selected profile's blood without replacing the PC preference");
    const auto requested = d.graphics;
    auto actual = port::ApplyGraphicsModePolicy(requested);
    Require(actual.msaa==1 && !actual.alphaToCoverage && actual.postAA==requested.postAA && requested.msaa==4 && requested.alphaToCoverage,
            "Naomi applies single-sample transparency without changing requested PC AA or postprocessing");
    actual.msaa = 2;
    const auto saved = play::PreserveNaomiPreferences(requested, actual);
    Require(saved.naomiGraphics && saved.perPixelLighting && saved.textureFilter == 2 && saved.anisotropy == 16 && saved.msaa == 4 && saved.alphaToCoverage,
            "Naomi policy writeback preserves the saved PC scene and MSAA/coverage choices");
    auto pcRequest=requested;pcRequest.naomiGraphics=false;
    Require(port::ApplyGraphicsModePolicy(pcRequest)==pcRequest,
            "the PC profile retains its requested MSAA/coverage and graphics settings");
    actual.naomiGraphics = false;
    Require(play::PreserveNaomiPreferences(requested, actual).naomiGraphics,
            "a temporary device fallback does not clear the saved graphics mode");
    menu.SetPage(Menu::kDisplay);
    menu.FocusRow(Menu::kResetGraphics);
    menu.Update(Key(8), d, sounds);
    menu.Update(Key(0x40), d, sounds);
    menu.Update(Key(8), d, sounds);
    Require(d.graphics.naomiGraphics && d.bloodColor == port::BloodColor::Purple && d.naomiBloodColor == port::BloodColor::Blue,
            "graphics reset restores Naomi defaults while retaining both Gameplay blood preferences");
}
void SavedGraphicsAndConsolidatedControls() {
    port::GraphicsOptions requested;
    requested.msaa = 4; requested.alphaToCoverage = true; requested.anisotropy = 16;
    requested.postAA = 2; requested.temporalAA = requested.outputResolutionHud = true;
    requested.spatialUpscaler = 1; requested.softParticles = requested.ambientOcclusion = requested.shadowMaps = 3;
    requested.materialMaps = true; requested.volumetricFog = requested.reflections = requested.cinematicFocus = 2;
    requested.hdrOutput = requested.allowTearing = requested.lowLatency = true;
    requested.bloom = 2; requested.gamma = 60;
    for (bool naomi : {false, true}) {
        requested.naomiGraphics = naomi;
        for (int samples : {1, 2, 4}) {
            port::GraphicsCapabilities limited;
            limited.msaa2 = samples >= 2; limited.msaa4 = samples == 4; limited.maxAnisotropy = samples;
            auto active = port::ApplyGraphicsModePolicy(play::SupportedGraphicsOptions(requested, limited));
            Require(active.msaa == (naomi ? 1 : samples) && active.postAA == 0 && !active.temporalAA && !active.shadowMaps,
                    "limited renderer receives only supported scene options");
            auto saved = play::PreserveGraphicsPreferences(requested, active, limited);
            Require(saved == requested, "renderer fallback preserves every saved request for the next renderer");
            active.bloom = 1;
            Require(play::PreserveGraphicsPreferences(requested, active, limited).bloom == 1,
                    "supported runtime changes still write back");
        }
    }
    play::DisplaySettings d; d.graphics.naomiGraphics = false;
    const char* choices[] = {"Off", "FSR 1", "DLSS", "FSR 2", "FSR 3", "XeSS"};
    for (int cycle = 0; cycle < 12; ++cycle) {
        Require(Menu::ValueText(d, Menu::kTemporalUpscaler) == choices[cycle % 6], "one upscaling control visits every provider");
        Require(!(d.graphics.spatialUpscaler && d.temporalUpscaler), "spatial and temporal upscalers cannot be selected together");
        if (d.temporalUpscaler) Require(d.renderer == 2, "temporal selection requests D3D12");
        if (d.graphics.spatialUpscaler) Require(d.renderer == 0, "FSR1 selection requests D3D11 after D3D12");
        Menu::Step(d, Menu::kTemporalUpscaler, 1);
    }
    d.graphics.crtGlow = 2;
    Menu::Step(d, Menu::kBloom, 1);
    Require(d.graphics.bloom == 3 && d.graphics.crtGlow == 0 && std::string(Menu::Label(Menu::kBloom)) == "Glow",
            "single glow adjustment replaces the old overlapping effect");
    Menu menu; menu.Open(); menu.SetPage(Menu::kGameplay);
    Menu::OptionValues values; values.typing = true; values.livesMask = (1u << 1) | (1u << 3);
    values.creditsMask = 1u | (1u << 3) | (1u << 5); menu.SetOptionValues(values);
    std::vector<uint32_t> sounds;
    menu.FocusRow(Menu::kLives); menu.Update(Key(0x80), d, sounds);
    Require(menu.OptionValuesState().lives == 1, "Typing lives skip values not offered by its original rules");
    menu.FocusRow(Menu::kContinues); menu.Update(Key(0x80), d, sounds);
    Require(menu.OptionValuesState().credits == 0, "Typing continues include Unlimited alongside its allowed numeric choices");
    menu.SetStatus("Restart required. Renderer choice applies on next launch.");
    game::WorldHud hud; menu.ModernHud(d, hud);
    Require(Text(hud, "Restart required. Renderer choice applies on next launch."), "restart notice is rendered");
    menu.SetStatus(std::string(250, 'W')); menu.ModernHud(d, hud);
    int notices = 0;
    for (const auto& text : hud.menuText) if (text.x == 210 && text.y >= 25 && text.y < 65) {
        ++notices; Require(text.x + text.text.size() * 16 * text.scale <= 614 && text.y + 24 * text.scale <= 65,
                           "long errors wrap and stay inside the notice area");
    }
    Require(notices == 2, "long status occupies at most two compact lines");
}
}  // namespace
int main() {
    for (bool modern : {false, true})
        for (bool naomi : {false, true})
            for (bool typing : {false, true}) LayoutAndReachability(modern, naomi, typing);
    ScrollHints(false);
    ScrollHints(true);
    CompactFooter(false);
    CompactFooter(true);
    ValuesAndActions();
    NaomiGraphicsChoices();
    SavedGraphicsAndConsolidatedControls();
    std::printf("PASS %d unified options checks: categories, scrolling, mixer buses, gameplay locks, reset scopes and devices\n", checks);
}
