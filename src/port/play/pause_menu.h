#pragma once

// play.exe's pause menu. Not in the original, whose pause (Game_UpdatePause 0x0040E500) shows only the "PAUSE" label
// over a dark overlay: this screen is drawn in the options screen's own look (game/menu_text.h: the background tiles
// 0x7A..0x8D, the 16 x 24 cell font 0x00487CA0, red for the selected line) while the world stays paused.
//   Rows: Resume, Music, Gun Shots, Sound Effects (volumes 0..10), Display (opens the display settings page,
//   display_menu.h), Return to Main Menu (Yes / No confirmation).
//   Keyboard: Up / Down move, Left / Right change a volume, Enter / Space select, Backspace back (= resume).
//   Pointing device: aiming at a row highlights it; trigger selects (on a volume row: +1), reload = -1 / back; the
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
    int music = 10, guns = 10, sfx = 10;  // 0..10 (gain = value / 10)
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
    bool Load(const std::string& path);   // a missing file or key keeps the default
    bool Save(const std::string& path) const;  // writes input= / assets= only when set
    bool operator==(const Settings& o) const { return music == o.music && guns == o.guns && sfx == o.sfx && display == o.display; }
    bool operator!=(const Settings& o) const { return !(*this == o); }
};
using VolumeSettings = Settings;  // the old name

class PauseMenu {
public:
    using Input = MenuInput;
    enum class Action { None, Resume, ReturnToTitle, WiiRemotes };
    enum Row { kResume = 0, kMusic, kGuns, kSfx, kDisplay, kWii, kReturn, kRowCount };

    void Open();
    void Close() {
        open_ = false;
        display_.Close();
    }
    bool IsOpen() const { return open_; }

    // One game frame of the open menu. sounds: SE ids to play (0xA9 cursor, 0x121A9 confirm). The display page, while
    // open, takes the input (start still resumes).
    Action Update(const Input& in, Settings& s, std::vector<uint32_t>& sounds);
    // The menu's sprites: background, the original's 1P / 2P PAUSE sprite as the title, the rows (or the confirmation,
    // or the display page).
    void Sprites(const game::ExeImage& exe, int pausePlayer, const Settings& s, std::vector<game::SpriteDef>& out) const;

    int Cursor() const { return cursor_; }
    bool Confirming() const { return confirm_; }
    bool ConfirmYes() const { return confirmYes_; }
    bool DisplayOpen() const { return display_.IsOpen(); }
    void SetNetworkRoom(bool on) { networkRoom_ = on; }
    const DisplayMenu& Display() const { return display_; }
    // The main rows (volume values are drawn by the caller with game::MenuNum) are shown: not the confirmation / page.
    bool MainRows() const { return open_ && !confirm_ && !display_.IsOpen(); }

    // Layout in cells (16 x 24 px). RowAt / ConfirmAt: what a pointer at game pixel (px, py) is over, -1 = nothing.
    static float RowY(int row);
    static int RowAt(float px, float py);
    static int ConfirmAt(float px, float py);  // 1 Yes, 0 No

private:
    static int* Volume(Settings& s, int row);  // the row's value, nullptr for the other rows

    bool open_ = false;
    bool networkRoom_ = false;
    int cursor_ = kResume;
    bool confirm_ = false, confirmYes_ = false;
    game::MenuPointerFocus pointer_;
    DisplayMenu display_;
};

}  // namespace play
