#pragma once

#include <string>
#include <vector>

#include "play/display_menu.h"

namespace play {

class UpdatePrompt {
public:
    enum class Action { None, Later, Download, Install };

    void Open(const std::string& currentVersion);
    void Close() { open_ = false; }
    bool IsOpen() const { return open_; }
    void ShowError(const std::string& message);
    Action Update(const MenuInput& in);
    void ModernHud(game::WorldHud& hud) const;

private:
    bool open_ = false, firstInput_ = false;
    int cursor_ = 0;
    std::string version_;
    std::vector<std::string> errorLines_;
    game::MenuPointerFocus pointer_;
};

}  // namespace play
