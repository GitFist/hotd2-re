#pragma once

#include "input/gamepad_guns.h"
#include "play/display_menu.h"

namespace play {

// Local controller preferences; the caller applies and saves changed profiles.
class ControllerMenu {
public:
    enum Row { kPlayer, kSensitivity, kDeadzone, kFire, kReload, kPause, kDefaults, kBack, kRowCount };

    void Open() { open_ = true; player_ = cursor_ = 0; pointer_ = {}; notice_.clear(); }
    void Close() { open_ = false; }
    bool IsOpen() const { return open_; }
    // Returns true only when Back closes the page. Gameplay mappings never affect menu controls.
    bool Update(const MenuInput& in, input::GamepadGuns& pads, std::vector<uint32_t>* sounds = nullptr);
    void Sprites(const game::ExeImage& exe, const input::GamepadGuns& pads, std::vector<game::SpriteDef>& out) const;
    void ModernHud(const input::GamepadGuns& pads, game::WorldHud& hud) const;
    void Notice(const std::string& text) { notice_ = text; }
    int Cursor() const { return cursor_; }
    int SelectedPlayer() const { return player_; }
    static float RowY(int row) { return 88.f + 27.f * row; }
    static int RowAt(float x, float y);

private:
    std::string Value(const input::GamepadGuns& pads, int row) const;
    std::string Status(const input::GamepadGuns& pads) const;
    bool open_ = false;
    int player_ = 0, cursor_ = 0;
    game::MenuPointerFocus pointer_;
    std::string notice_;
};

} // namespace play
