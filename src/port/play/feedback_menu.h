#pragma once

#include <array>
#include "input/feedback_settings.h"
#include "play/display_menu.h"

namespace play {

class FeedbackMenu {
public:
    enum Row { kPlayer, kController, kGun, kPort, kRecoil, kDamage, kTestShot, kTestDamage, kBack, kRowCount };
    enum class Action { None, TestShot, TestDamage };

    void Open() { open_ = true; player_ = cursor_ = 0; pointer_ = {}; notice_.clear(); }
    void Close() { open_ = false; }
    bool IsOpen() const { return open_; }
    Action Update(const MenuInput& in, input::feedback::Settings& settings, std::vector<uint32_t>* sounds = nullptr);
    void Sprites(const game::ExeImage& exe, const input::feedback::Settings& settings, std::vector<game::SpriteDef>& out) const;
    void ModernHud(const input::feedback::Settings& settings, game::WorldHud& hud) const;
    void SetStatus(int localSlot, const std::string& text) { if (localSlot >= 0 && localSlot < 2) status_[localSlot] = text; }
    void Notice(const std::string& text) { notice_ = text; }
    int SelectedPlayer() const { return player_; }
    int Cursor() const { return cursor_; }
    static float RowY(int row) { return 88.f + 27.f * row; }
    static int RowAt(float x, float y);

private:
    std::string Value(const input::feedback::Settings& settings, int row) const;
    bool open_ = false;
    int player_ = 0, cursor_ = 0;
    game::MenuPointerFocus pointer_;
    std::array<std::string, 2> status_;
    std::string notice_;
};

} // namespace play
