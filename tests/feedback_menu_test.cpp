#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "game/menu_audio.h"
#include "port/play/feedback_menu.h"
#ifdef _WIN32
#include "port/render/ui_font.h"
#endif

namespace {
using Menu = play::FeedbackMenu;
using Settings = input::feedback::Settings;
using GunType = input::feedback::GunType;
int checks = 0;
void Check(bool ok, const char* why) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
play::MenuInput Key(uint32_t key) { play::MenuInput in; in.pressed = key; return in; }
play::MenuInput Point(int row, bool click = false, float x = 500) {
    play::MenuInput in;
    in.pointer = true; in.trigger = click; in.px = x; in.py = Menu::RowY(row) + 20;
    return in;
}
bool Text(const game::WorldHud& hud, const std::string& value) {
    return std::any_of(hud.menuText.begin(), hud.menuText.end(), [&](const auto& text) { return text.text == value; });
}

void DevicesEntry() {
    using Display = play::DisplayMenu;
    Display menu;
    play::DisplaySettings settings;
    std::vector<uint32_t> sounds;
    menu.Open(); menu.SetPage(Display::kDevices); menu.FocusRow(Display::kFeedback);
    Check(Display::Rows(Display::kDevices) == std::vector<int>{Display::kLightguns, Display::kWiiRemotes, Display::kControllers, Display::kFeedback, Display::kBack},
          "Feedback appears after Controllers in Devices");
    menu.Update(Key(0x80), settings, sounds);
    Check(menu.TakeDeviceAction() == Display::DeviceAction::None && sounds.empty(), "right arrow does not open Feedback");
    menu.Update(Key(8), settings, sounds);
    Check(menu.TakeDeviceAction() == Display::DeviceAction::Feedback && menu.TakeDeviceAction() == Display::DeviceAction::None,
          "confirm requests Feedback exactly once");
    Check(sounds == std::vector<uint32_t>{game::kOptionSelectSound}, "Feedback action plays one selection sound");
    game::WorldHud hud;
    menu.ModernHud(settings, hud);
    Check(Text(hud, "Feedback") && Text(hud, "Open"), "Devices renders the Feedback action");
    play::MenuInput pointer;
    pointer.pointer = pointer.trigger = true; pointer.px = 100;
    pointer.py = Display::RowY(Display::kFeedback, Display::kDevices) * 24 + 22;
    menu.Update(pointer, settings, sounds);
    Check(menu.TakeDeviceAction() == Display::DeviceAction::Feedback, "pointer opens the same Feedback action");
}

void MenuEditing() {
    Settings settings;
    Menu menu;
    menu.Open();
    std::vector<uint32_t> sounds;
    auto key = [&](uint32_t bit) { return menu.Update(Key(bit), settings, &sounds); };
    Check(menu.SelectedPlayer() == 0 && menu.Cursor() == Menu::kPlayer, "Feedback starts on local Player 1");
    key(0x20);
    Check(menu.Cursor() == Menu::kController && sounds.empty(), "row navigation is silent");
    Check(key(8) == Menu::Action::None && settings.player[0].controllerStrength == 25 && settings.player[1].controllerStrength == 0,
          "enabling controller feedback edits P1 without a test event");
    for (int n = 0; n < 8; ++n) key(0x80);
    Check(settings.player[0].controllerStrength == 100, "controller strength clamps at 100");
    for (int n = 0; n < 8; ++n) key(0x40);
    Check(settings.player[0].controllerStrength == 0, "controller strength can be disabled");
    key(0x20); key(8);
    Check(settings.player[0].gun == GunType::OpenFire, "first native gun profile is OpenFIRE");
    key(0x40);
    Check(settings.player[0].gun == GunType::Off, "left returns native gun support to Off");
    key(0x40);
    Check(settings.player[0].gun == GunType::Sinden, "native profile list wraps to Sinden");
    key(0x20); key(8);
    Check(settings.player[0].sindenGun == 2 && settings.player[0].port == 0, "Sinden Driver gun changes A/B without changing serial COM port");
    menu.Update(Point(Menu::kGun, true, 300), settings);
    Check(settings.player[0].gun == GunType::Blamcon, "pointer left arrow cycles gun backwards");
    auto scroll = Point(Menu::kPort); scroll.scroll = 7;
    Check(menu.Update(scroll, settings) == Menu::Action::None && settings.player[0].port == 7,
          "mouse wheel selects a COM number without a hardware test");
    scroll.scroll = 1000; menu.Update(scroll, settings);
    Check(settings.player[0].port == 256, "COM selector clamps to 256");
    scroll.scroll = -1000; menu.Update(scroll, settings);
    Check(settings.player[0].port == 0, "COM selector can return to Not set");
    menu.Update(Point(Menu::kRecoil, true), settings);
    menu.Update(Point(Menu::kDamage, true), settings);
    Check(!settings.player[0].recoil && !settings.player[0].damage, "gun recoil and damage have independent toggles");
    const auto p1 = settings.player[0];
    menu.Update(Point(Menu::kPlayer, true), settings);
    menu.Update(Point(Menu::kController, true), settings);
    Check(menu.SelectedPlayer() == 1 && settings.player[1].controllerStrength == 25 && settings.player[0] == p1,
          "Player selector edits only the chosen local player");
    menu.Update(Point(Menu::kTestShot), settings);
    Check(key(0x80) == Menu::Action::None && key(0x40) == Menu::Action::None,
          "arrows cannot accidentally fire a hardware test");
    const auto saved = settings;
    Check(key(8) == Menu::Action::TestShot && menu.SelectedPlayer() == 1 && settings == saved,
          "explicit shot test reports selected player without editing preferences");
    Check(menu.Update({}, settings) == Menu::Action::None, "test does not repeat on an empty frame");
    Check(menu.Update(Point(Menu::kTestDamage, true), settings) == Menu::Action::TestDamage,
          "pointer explicitly requests a damage test");
    menu.Update(Point(Menu::kBack, true), settings);
    Check(!menu.IsOpen() && menu.Update(Key(8), settings) == Menu::Action::None, "Back closes and closed menu ignores confirmation");
    menu.Open(); key(2); Check(!menu.IsOpen(), "Escape/controller Back closes Feedback");
    Check(Menu::RowAt(10, 100) == -1 && Menu::RowAt(50, 360) == -1, "status text and outside margins cannot activate rows");
}

void SettingsFile() {
    Settings settings;
    Check(settings.player[0].gun == GunType::Off && !settings.player[0].controllerStrength && settings.player[0].port == 0 &&
              settings.player[0].sindenGun == 1 && settings.player[1].sindenGun == 2,
          "native feedback defaults Off with distinct Sinden driver guns");
    settings.player[0].controllerStrength = 75; settings.player[0].gun = GunType::Gun4ir; settings.player[0].port = 37;
    settings.player[0].recoil = false;
    settings.player[1].controllerStrength = 100; settings.player[1].gun = GunType::Sinden; settings.player[1].sindenPort = 13005;
    settings.player[1].damage = false;
    const auto path = std::filesystem::temp_directory_path() / ("hotd2-feedback-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ini");
    Check(settings.Save(path.string()), "native feedback preferences save");
    Settings loaded;
    Check(loaded.Load(path.string()) && loaded == settings, "both independent profiles round-trip all native feedback fields");
    const auto before = loaded;
    Check(!loaded.Load(path.string() + ".missing") && loaded == before, "missing settings file preserves current preferences");
    {
        std::ofstream invalid(path);
        invalid << "player1.controller_strength=999\nplayer1.gun=257\nplayer1.port=-5\nplayer1.sinden_port=65536\n"
                   "player1.recoil=0\nplayer1.damage=9\nplayer2.controller_strength=61\nplayer2.gun=3trash\nplayer2.port=999\n"
                   "player2.sinden_gun=7\nplayer2.damage=0\nplayer2.recoil=999999999999999999999999999\n";
    }
    Check(loaded.Load(path.string()), "malformed preference entries are handled");
    Check(loaded.player[0].controllerStrength == 100 && loaded.player[0].gun == GunType::Off && loaded.player[0].port == 0 &&
              loaded.player[0].sindenPort == 13000 && !loaded.player[0].recoil && loaded.player[0].damage,
          "out-of-range values normalize while invalid booleans use safe defaults");
    Check(loaded.player[1].controllerStrength == 50 && loaded.player[1].gun == GunType::Off && loaded.player[1].port == 256 &&
              loaded.player[1].sindenGun == 2 && !loaded.player[1].damage && loaded.player[1].recoil,
          "trailing text, overflowing numbers and invalid driver gun cannot corrupt P2 settings");
    std::error_code error;
    Check(std::filesystem::remove(path, error), "remove the single temporary settings file");
}

void HudContent() {
    Menu menu;
    Settings settings;
    menu.Open();
    menu.SetStatus(0, "OpenFIRE / COM7 connected");
    menu.SetStatus(1, "Sinden driver unavailable");
    menu.Notice("Could not save feedback settings");
    game::WorldHud hud;
    menu.ModernHud(settings, hud);
    Check(Text(hud, "FEEDBACK") && Text(hud, "Player 1") && Text(hud, "Controller rumble") && Text(hud, "Not set"),
          "Feedback page displays native preferences");
    Check(Text(hud, "P1 / OpenFIRE / COM7 connected") && Text(hud, "P2 / Sinden driver unavailable") && Text(hud, "Could not save feedback settings"),
          "caller status for both physical slots and save errors are visible");
    Check(Text(hud, "Online play uses local Player 1 feedback."), "online physical feedback mapping is explained");
    settings.player[0].gun = GunType::Sinden;
    menu.ModernHud(settings, hud);
    Check(Text(hud, "Driver gun") && Text(hud, "A / Gun 1"), "Sinden replaces COM selector with its driver gun");
    for (const auto& panel : hud.menuPanels)
        Check(panel.left >= 0 && panel.top >= 0 && panel.right <= 640 && panel.bottom <= 480 && panel.left < panel.right && panel.top < panel.bottom,
              "Feedback menu panels stay within the logical canvas");
    for (const auto& text : hud.menuText)
        Check(std::isfinite(text.x) && std::isfinite(text.y) && std::isfinite(text.scale) && text.scale > 0 && text.x >= 0 && text.y >= 0 &&
                  text.x + float(text.text.size()) * 16 * text.scale < 640 && text.y + 24 * text.scale < 480,
              "Feedback text remains within the logical canvas");
}

void NativeGlyphBounds() {
#ifdef _WIN32
    port::UiFont font;
    Check(font.Init() && font.Available(), "native system fonts initialize for Feedback bounds checks");
    Menu menu;
    Settings settings;
    menu.Open();
    menu.SetStatus(0, "Controller 100% / OpenFIRE COM256: waiting for game");
    menu.SetStatus(1, "Controller 100% / Sinden driver gun B: connection unavailable");
    menu.Notice(std::string(90, 'W'));
    settings.player[0].controllerStrength = settings.player[1].controllerStrength = 100;
    settings.player[0].port = settings.player[1].port = 256;
    for (int slot = 0; slot < 2; ++slot) {
        for (int type = 0; type < input::feedback::kGunTypeCount; ++type) {
            settings.player[slot].gun = static_cast<GunType>(type);
            game::WorldHud hud;
            menu.ModernHud(settings, hud);
            for (const auto& text : hud.menuText) {
                std::vector<port::ModelRenderer::SpriteDraw> glyphs;
                font.Append({text}, glyphs);
                Check(!glyphs.empty(), "Feedback text produces native glyphs");
                bool onCanvas = true, labelFits = true, valueFits = true;
                for (const auto& glyph : glyphs) for (const auto& vertex : glyph.v) {
                    onCanvas &= vertex.x >= 0 && vertex.x <= 640 && vertex.y >= 0 && vertex.y <= 480;
                    labelFits &= vertex.x < 306;
                    valueFits &= vertex.x > 325 && vertex.x < 594;
                }
                if (!onCanvas) std::fprintf(stderr, "Native glyph overflow: %s\n", text.text.c_str());
                Check(onCanvas, "native Feedback glyphs and shadows remain on canvas");
                if (text.x == 34 && text.y >= 98 && text.y <= 314)
                    Check(labelFits, "Feedback row labels do not overlap value arrows");
                if (text.x == 330)
                    Check(valueFits, "Feedback row values do not overlap navigation arrows");
            }
        }
        menu.Update(Point(Menu::kPlayer, true), settings);
    }
#endif
}
}

int main() {
    DevicesEntry(); MenuEditing(); SettingsFile(); HudContent(); NativeGlyphBounds();
    std::printf("PASS native Feedback menu and preferences: %d checks\n", checks);
}
