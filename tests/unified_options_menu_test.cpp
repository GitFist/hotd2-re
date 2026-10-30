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
float Top(const Menu& menu, int row) { return Menu::RowY(row, menu.CurrentPage(), menu.ScrollOffset()) * 24 + 10; }
void LayoutAndReachability(bool modern) {
    Menu menu;
    menu.SetNightShiftActive(modern);
    play::DisplaySettings settings;
    std::vector<uint32_t> sounds;
    menu.Open();
    const char* names[] = {"Display", "Graphics", "Gameplay", "Sound", "Devices", "CRT", "Packs"};
    Require(Menu::kPageCount == 7, "seven requested categories including Sound");
    std::vector<int> count(Menu::kRowCount);
    for (int category = 0; category < Menu::kPageCount; ++category) {
        const auto page = static_cast<Menu::Page>(category);
        menu.SetPage(page);
        for (int row : Menu::Rows(page)) {
            ++count[row];
            menu.FocusRow(row);
            const float top = Top(menu, row);
            const auto visible = menu.VisibleRows();
            Require(std::find(visible.begin(), visible.end(), row) != visible.end(), "every option scrolls into the real viewport");
            Require(row == Menu::kBack ? top >= 429.9f && top + 24 <= 454.1f : top >= 125.9f && top + 24 <= 420.1f,
                    "settings and fixed Back remain inside their reserved regions");
            Require(Menu::RowAt(600, top + 12, page, menu.ScrollOffset()) == row,
                    "scrolled pointer hit test selects the visible row");
            Require(Menu::RowAt(600, 117, page, menu.ScrollOffset()) == -1,
                    "group headings and category gap do not change a hidden value");
            game::WorldHud hud;
            menu.ModernHud(settings, hud);
            for (const char* name : names) Require(Text(hud, name), "all seven category headers remain present while scrolling");
            Require(Text(hud, Menu::Label(row)), "focused option is rendered at every scroll position");
            Require(Text(hud, "Back"), "Back stays visible while scrolling");
            Require(!Text(hud, "Game Options") && !Text(hud, "Advanced") && !Text(hud, "Resolution"),
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
    for (int row = 0; row < Menu::kRowCount; ++row)
        Require(count[row] == (row == Menu::kBack ? Menu::kPageCount : 1), "each setting has one category and one editable location");
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
    for(auto page:{Menu::kSound,Menu::kDevices,Menu::kRetro,Menu::kPacks}) {
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
            Require(!Text(hud, status), "saved-setting status no longer occupies Options list space");
            const auto help = menu.RowHelp(settings, row);
            Require(help.empty() || !Text(hud, help), "normal Options does not render per-row tooltips");
            int cornerHints = 0;
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
                } else {
                    Require(text.y < 456, "the list footer contains no large instruction or tooltip block");
                }
            }
            Require(cornerHints == 1, "every Options category retains one compact Hide UI control");
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
    Require(menu.TakeOptionChange(&change) && change.credits == 1, "locked Free Play cannot be selected by wrapping credits");
    values = menu.OptionValuesState();
    values.freePlayUnlocked = true;
    values.credits = 9;
    menu.SetOptionValues(values);
    press(0x80);
    Require(menu.TakeOptionChange(&change) && change.credits == 0, "unlocked Free Play is selectable");
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
}  // namespace
int main() {
    LayoutAndReachability(false);
    LayoutAndReachability(true);
    ScrollHints(false);
    ScrollHints(true);
    CompactFooter(false);
    CompactFooter(true);
    ValuesAndActions();
    std::printf("PASS %d unified options checks: categories, scrolling, mixer buses, gameplay locks, reset scopes and devices\n", checks);
}
