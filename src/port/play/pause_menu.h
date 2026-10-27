#pragma once

// play.exe's pause menu. Not in the original, whose pause (Game_UpdatePause 0x0040E500) shows only the "PAUSE" label
// over a dark overlay: this screen is drawn in the options screen's own look (game/menu_text.h: the background tiles
// 0x7A..0x8D, the 16 x 24 cell font 0x00487CA0, red for the selected line) while the world stays paused.
//   Rows: Resume, Options (the same complete category page as the main menu), Return to Main Menu (confirmation).
//   Keyboard: Up / Down move, Enter / Space select, Backspace back (= resume).
//   Pointing device: aiming at a row highlights it; trigger selects, reload = back; the
//   start button resumes, as in the original.
// Pure state machine (tests/pause_menu_test.cpp); the caller feeds the input, plays the returned sound ids, applies
// the volumes (game::SoundDriver::SetVolumes) and display settings, and acts on Resume / ReturnToTitle
// (World::RequestTitle).

#include <cstdint>
#include <string>
#include <vector>

#include "game/exe_image.h"
#include "game/sprite.h"
#include "port/play/display_menu.h"

namespace play {

// The port's settings, kept in a small text file next to the save file (hotd2re_settings.ini, "key=value" lines).
struct Settings {
    int music = 5, guns = 5, sfx = 5;  // 0..10 (gain = value / 10)
    int master = 5, attraction = 5;   // master scales all audio; attraction controls the menu's background loop
    // "input=auto|raw|mouse|touch": the gun source when the command line gives none (platform::ParseInput). "mouse" for
    // touchpads and other pointers whose clicks Windows synthesizes: raw input never sees those (no device handle).
    std::string input;
    // "fullscreen=0|1", "scale=1|2|3|fit", "aspect=4:3|stretch", "crt=0..3", "render=1..4"
    // (display_menu.h)
    DisplaySettings display;
    // "assets=<dir>": an optional asset pack (port::SetAssetDir; HOTD2_ASSET_DIR wins). notes/agents/remaster.md.
    std::string assets;
    // "lobby=<host:port>": the online lobby server (play.cpp kDefaultLobby when absent; HOTD2_LOBBY / --lobby win).
    std::string lobby;
    // Public leaderboard display name. Empty means first-launch setup is required.
    std::string username;
    bool Load(const std::string& path);   // a missing file or key keeps the default
    bool Save(const std::string& path) const;  // writes input= / assets= only when set
    bool operator==(const Settings& o) const {
        return music == o.music && guns == o.guns && sfx == o.sfx && master == o.master && attraction == o.attraction && display == o.display && username == o.username;
    }
    bool operator!=(const Settings& o) const { return !(*this == o); }
};
using VolumeSettings = Settings;  // the old name

class PauseMenu {
public:
    using Input = MenuInput;
    enum class Action { None, Resume, ReturnToTitle, WiiRemotes, Lightguns };
    enum Row { kResume = 0, kOptions, kReturn, kRowCount, kDisplay = kOptions };

    void Open();
    void Close() {
        open_ = false;
        display_.Close();
    }
    bool IsOpen() const { return open_; }

    // One game frame of the open menu. Navigation is silent; option interactions use Preview 74 and backing out uses
    // Preview 76. The display page, while open, takes the input (start still resumes).
    Action Update(const Input& in, Settings& s, std::vector<uint32_t>& sounds);
    // The menu's sprites: background, the original's 1P / 2P PAUSE sprite as the title, the rows (or the confirmation,
    // or the display page).
    void Sprites(const game::ExeImage& exe, int pausePlayer, const Settings& s, std::vector<game::SpriteDef>& out) const;
    // Theme is fixed by the caller at launch, so a pending preference cannot change pointer targets mid-session.
    void SetModern(bool modern) {
        modern_ = modern;
        display_.SetNightShiftActive(modern);
    }
    bool IsModern() const { return modern_; }
    void ModernHud(int pausePlayer, const Settings& s, game::WorldHud& hud) const;

    int Cursor() const { return cursor_; }
    bool Confirming() const { return confirm_; }
    bool ConfirmYes() const { return confirmYes_; }
    bool DisplayOpen() const { return display_.IsOpen(); }
    void SetNetworkRoom(bool on) { networkRoom_ = on; }
    const DisplayMenu& Display() const { return display_; }
    DisplayMenu& Display() { return display_; }
    // The three main actions are shown, rather than the confirmation or the options page.
    bool MainRows() const { return open_ && !confirm_ && !display_.IsOpen(); }

    // Layout in cells (16 x 24 px). RowAt / ConfirmAt: what a pointer at game pixel (px, py) is over, -1 = nothing.
    static float RowY(int row);
    static int RowAt(float px, float py);
    static int ConfirmAt(float px, float py);  // 1 Yes, 0 No
    // Night Shift uses logical-pixel panels instead of Classic's sprite-font cells.
    static float ModernRowTop(int row);
    static int ModernRowAt(float px, float py);
    static int ModernConfirmAt(float px, float py);  // 1 Return, 0 Cancel

private:
    bool open_ = false;
    bool modern_ = false;
    bool networkRoom_ = false;
    int cursor_ = kResume;
    bool confirm_ = false, confirmYes_ = false;
    game::MenuPointerFocus pointer_;
    DisplayMenu display_;
};

}  // namespace play
