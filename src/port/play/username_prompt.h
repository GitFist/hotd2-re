#pragma once

#include <string>

#include "game/menu_pointer.h"
#include "play/display_menu.h"

namespace play {

// Canonical leaderboard display name. Invalid saved values require setup again.
std::string NormalizeUsername(const std::string& text);
bool ValidUsername(const std::string& text);

// Local modal, shared by first-launch setup and Gameplay > Username.
// Save leaves the modal open until the caller has saved Settings successfully.
class UsernamePrompt {
public:
    enum class Action { None, Save, Cancel };
    void Open(const std::string& initialName, bool required);
    void Close() { open_ = false; }
    bool IsOpen() const { return open_; }
    bool Required() const { return required_; }
    const std::string& Name() const { return name_; }
    void Edit(const std::string& text, bool backspace = false);
    Action Update(const MenuInput& in);
    void SetError(const std::string& error) { error_ = error; }
    void ModernHud(game::WorldHud& hud) const;

private:
    int cursor_ = 0;
    bool open_ = false, required_ = false, pointerVisible_ = false, typedFocus_ = false;
    float pointerX_ = 0, pointerY_ = 0;
    std::string name_, error_;
    game::MenuPointerFocus pointer_;
};

}  // namespace play
