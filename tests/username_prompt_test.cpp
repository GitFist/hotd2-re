#include "port/play/username_prompt.h"
#include "port/play/pause_menu.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {
using Prompt = play::UsernamePrompt;
using Action = Prompt::Action;
int checks = 0;
void Require(bool ok, const char* why) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}
play::MenuInput Key(uint32_t key) {
    play::MenuInput in;
    in.pressed = key;
    return in;
}
play::MenuInput Click(float x, float y) {
    play::MenuInput in;
    in.pointer = in.trigger = true;
    in.px = x;
    in.py = y;
    return in;
}
bool Text(const game::WorldHud& hud, const std::string& text) {
    return std::any_of(hud.menuText.begin(), hud.menuText.end(), [&](const auto& item) { return item.text == text; });
}
void SetupAndRename() {
    Prompt prompt;
    Require(!prompt.IsOpen() && prompt.Update(Key(8)) == Action::None, "closed modal ignores input");
    prompt.Open("", true);
    for (auto in : {Key(2), Click(500, 386)})
        Require(prompt.Update(in) == Action::None && prompt.IsOpen(), "first setup cannot cancel");
    auto in = Key(0); in.start = true;
    Require(prompt.Update(in) == Action::None && prompt.IsOpen(), "start cannot bypass first setup");
    in.start = false; in.reload = true;
    Require(prompt.Update(in) == Action::None && prompt.IsOpen(), "reload cannot bypass first setup");
    Require(prompt.Update(Click(380, 386)) == Action::None && prompt.IsOpen(), "blank username cannot save");
    prompt.Edit("   ");
    Require(prompt.Update(Key(8)) == Action::None, "all-space username cannot save");
    prompt.Update(Click(250, 386));
    prompt.Edit("  night_1-  ");
    Require(prompt.Update(Key(8)) == Action::Save && prompt.Name() == "NIGHT_1-" && prompt.IsOpen(),
            "save trims and normalizes but waits for durable settings write");
    prompt.SetError("COULD NOT SAVE USERNAME. PLEASE RETRY.");
    game::WorldHud hud;
    prompt.ModernHud(hud);
    Require(Text(hud, "COULD NOT SAVE USERNAME. PLEASE RETRY.") && !Text(hud, "CANCEL"),
            "failed persistence remains visible without bypassing required setup");
    Require(prompt.Update(Key(8)) == Action::Save, "failed saving can be retried");
    prompt.Close();
    Require(!prompt.IsOpen(), "caller closes only after saving succeeds");
    prompt.Open("NIGHT_1-", false);
    prompt.Edit("2", true);
    Require(prompt.Name() == "NIGHT_12", "rename starts with saved name and supports backspace");
    Require(prompt.Update(Key(2)) == Action::Cancel && !prompt.IsOpen(), "renaming may be cancelled");
    prompt.Open("NIGHT_1-", false);
    Require(prompt.Name() == "NIGHT_1-", "cancelled rename does not alter saved starting name");
    Require(prompt.Update(Click(500, 386)) == Action::Cancel, "pointer can cancel renaming");
    prompt.Open("", true);
    prompt.Edit("abcdefghijklmnop!@#$\n");
    Require(prompt.Name() == "ABCDEFGHIJKL", "typed input filters unsupported characters and caps at twelve");
    prompt.Open("", true);
    prompt.Edit("a!b@c#d$");
    Require(prompt.Name() == "ABCD", "unsupported input never enters the name");
}
void PointerAndKeyboard() {
    Prompt prompt;
    prompt.Open("", true);
    constexpr char keys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-";
    for (int i = 0; i < 40; ++i) {
        prompt.Update(Click(250, 386));
        const float x = 70 + (i % 10) * 52.f, y = 196 + (i / 10) * 40.f;
        Require(prompt.Update(Click(x, y)) == Action::None && prompt.Name() == std::string(1, keys[i]),
                "every virtual keyboard cell inserts its visible character");
    }
    prompt.Open("", true);
    prompt.Update(Key(8));
    prompt.Update(Key(0x80));
    prompt.Update(Key(8));
    prompt.Update(Key(0x20));
    prompt.Update(Key(8));
    Require(prompt.Name() == "ABL", "arrow keys navigate the virtual keyboard");
    prompt.Edit("X");
    Require(prompt.Update(Click(630, 465)) == Action::None, "blank click cannot trigger the selected Save action");
    Require(prompt.Update(Key(8)) == Action::Save, "typed input selects Save for Enter");
    prompt.Open("", true);
    prompt.Edit("TYPED");
    auto aim = Click(70, 196); aim.trigger = false; aim.pressed = 8;
    Require(prompt.Update(aim) == Action::Save && prompt.Name() == "TYPED", "initial resting aim cannot steal typed Enter-to-save");
}
void PresentationAndSettings() {
    Prompt prompt;
    prompt.Open("TWELVE_CHARS", true);
    game::WorldHud hud;
    hud.gameMode = 6;
    hud.menuText.push_back({0, 0, "OLD MENU", 1, 0});
    prompt.ModernHud(hud);
    Require(hud.gameMode == 6 && !Text(hud, "OLD MENU") && Text(hud, "12 / 12") &&
                Text(hud, "SCORES ARE AUTOMATICALLY SHARED ON THE PUBLIC LEADERBOARD"),
            "setup explains public automatic submission without mutating simulation state");
    Require(Text(hud, "SAVE") && !Text(hud, "PUBLISH") && !Text(hud, "CANCEL"), "setup has a Save action and no Publish or Cancel");
    prompt.Open("TWELVE_CHARS", false);
    prompt.ModernHud(hud);
    Require(Text(hud, "CANCEL") && Text(hud, "YOUR PREVIOUS AND QUEUED SCORES KEEP THEIR ORIGINAL NAME"),
            "rename explains existing records and permits cancellation");
    for (const auto& invalid : {std::string{}, std::string(13, 'A'), std::string("\nA"), std::string("A\0B", 3),
                               std::string("A=B"), std::string("   ")})
        Require(play::NormalizeUsername(invalid).empty() && !play::ValidUsername(invalid), "invalid stored names require setup");
    Require(play::NormalizeUsername(" name.1_2- ") == "NAME.1_2-" && !play::ValidUsername("name"),
            "saved names normalize case/whitespace and validation requires canonical form");
    const auto dir = std::filesystem::temp_directory_path() /
                     ("hotd2-username-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    const auto path = (dir / "settings.ini").string();
    play::Settings saved, restored;
    saved.username = "TWELVE_CHARS";
    saved.music = 7;
    Require(saved.Save(path) && restored.Load(path) && saved == restored, "username and existing settings survive restart");
    saved.username = "NEW_NAME";
    Require(saved != restored && saved.Save(path) && restored.Load(path) && restored.username == "NEW_NAME", "rename changes settings and persists");
    saved.username = "BAD\nNAME";
    Require(!saved.Save(path) && restored.Load(path) && restored.username == "NEW_NAME", "invalid rename cannot overwrite valid settings");
    Require(!restored.Save((dir / "missing" / "settings.ini").string()), "unwritable path reports saving failure");
    for (const char* content : {"music=3\n", "username=INVALID!\n", "username=   \n", "username=ABCDEFGHIJKLM\n"}) {
        std::ofstream(path) << content;
        play::Settings loaded;
        Require(loaded.Load(path) && loaded.username.empty(), "legacy or invalid stored username requests first setup");
    }
    std::ofstream(path) << "username=  lower_case  \n";
    play::Settings normalized;
    Require(normalized.Load(path) && normalized.username == "LOWER_CASE", "safe saved names normalize without losing identity");
    std::filesystem::remove_all(dir);
}
void GameplayEntry() {
    play::DisplayMenu menu;
    play::DisplaySettings settings;
    play::DisplayMenu::OptionValues values;
    values.username = "TEST_PLAYER";
    values.gameplayEditable = false;
    menu.SetOptionValues(values);
    menu.Open();
    menu.SetPage(play::DisplayMenu::kGameplay);
    menu.FocusRow(play::DisplayMenu::kUsername);
    game::WorldHud hud;
    menu.ModernHud(settings, hud);
    Require(Text(hud, "Username") && Text(hud, "TEST_PLAYER"), "Gameplay displays saved username");
    std::vector<uint32_t> sounds;
    menu.Update(Key(8), settings, sounds);
    Require(menu.TakeUsernameEdit() && !menu.TakeUsernameEdit() && menu.IsOpen(), "username action is available during a run and consumed once");
    menu.Update(Key(0x80), settings, sounds);
    Require(!menu.TakeUsernameEdit(), "left/right do not silently change a username");
    const float y = play::DisplayMenu::RowY(play::DisplayMenu::kUsername, menu.CurrentPage(), menu.ScrollOffset()) * 24 + 12;
    menu.Update(Click(300, y), settings, sounds);
    Require(menu.TakeUsernameEdit(), "lightgun selection opens the username editor");
    Require(!menu.TakeOptionChange(&values), "opening username does not change gameplay or volume values");
}
}  // namespace
int main() {
    SetupAndRename();
    PointerAndKeyboard();
    PresentationAndSettings();
    GameplayEntry();
    std::printf("PASS username setup, rename, settings persistence and Gameplay entry: %d checks\n", checks);
}
