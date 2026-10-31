#include "play/update_prompt.h"

#include <algorithm>

#include "play/device_menu_hud.h"

namespace play {
namespace {
struct Choice {
    float left;
    const char* label;
    UpdatePrompt::Action action;
};
constexpr Choice kChoices[] = {{64, "LATER", UpdatePrompt::Action::Later},
                               {240, "DOWNLOAD", UpdatePrompt::Action::Download},
                               {416, "UPDATE NOW", UpdatePrompt::Action::Install}};
constexpr float kTop = 302, kBottom = 344, kWidth = 160;
}  // namespace

void UpdatePrompt::Open(const std::string& currentVersion) {
    open_ = true;
    firstInput_ = true;
    pointer_ = {};
    cursor_ = 0;
    version_ = currentVersion;
    errorLines_.clear();
}

void UpdatePrompt::ShowError(const std::string& message) {
    Open({});
    // Leave room for the dismiss button even when Windows supplies a long path or diagnostic.
    constexpr size_t width = 54, maxLines = 6;
    size_t pos = message.find_first_not_of(" \r\t\n");
    while (pos != std::string::npos && errorLines_.size() < maxLines) {
        const size_t end = std::min(message.size(), pos + width);
        size_t cut = message.find_first_of("\r\n", pos);
        if (cut > end) {
            cut = end;
            if (end < message.size()) {
                const size_t space = message.find_last_of(" \t", end);
                if (space != std::string::npos && space > pos) cut = space;
            }
        }
        errorLines_.push_back(message.substr(pos, cut - pos));
        pos = message.find_first_not_of(" \r\t\n", cut);
    }
    if (pos != std::string::npos) errorLines_.back() += "...";
    if (errorLines_.empty()) errorLines_.push_back("The update could not start. Please try again later.");
}

UpdatePrompt::Action UpdatePrompt::Update(const MenuInput& in) {
    if (!open_) return Action::None;
    if ((in.pressed & 2) || in.reload || in.start) {
        Close();
        return Action::Later;
    }

    const int count = errorLines_.empty() ? 3 : 1;
    int under = -1;
    if (in.pointer && in.py >= kTop && in.py < kBottom) {
        for (int i = 0; i < count; ++i)
            if (in.px >= kChoices[i].left && in.px < kChoices[i].left + kWidth) under = i;
    }
    // A resting pointer on the opening frame must not replace the safe keyboard default.
    const bool aim = pointer_.Update(in.pointer, in.px, in.py, in.trigger);
    if (aim && under >= 0 && (!firstInput_ || in.trigger)) cursor_ = under;
    firstInput_ = false;
    if (in.pressed & 0x50) cursor_ = (cursor_ + count - 1) % count;
    if (in.pressed & 0xA0) cursor_ = (cursor_ + 1) % count;

    const int choice = in.trigger ? under : (in.pressed & 0xC) ? cursor_ : -1;
    if (choice < 0) return Action::None;
    Close();
    return kChoices[choice].action;
}

void UpdatePrompt::ModernHud(game::WorldHud& hud) const {
    if (!open_) return;
    const bool error = !errorLines_.empty();
    hud.nightShiftPage = 4;
    hud.nightShiftRow = cursor_;
    hud.menuPanels = {{0, 0, 640, 480, 0xDA05090B}, {44, 84, 596, 394, 0xF50B1012},
                      {64, 143, 576, 145, 0xFF92272D}};
    hud.menuText.clear();
    hud.menuScrollHints.clear();
    using device_ui::Text;
    Text(hud, 64, 101, error ? "UPDATE ERROR" : "UPDATE AVAILABLE", 1.0f);
    if (error) {
        float y = 166;
        for (const auto& line : errorLines_) {
            Text(hud, 64, y, line, .52f);
            y += 20;
        }
    } else {
        Text(hud, 64, 160, "CURRENT VERSION " + version_, .57f, device_ui::kMuted);
        Text(hud, 64, 198, "Update now closes the game and opens the updater.", .53f);
        Text(hud, 64, 221, "The game restarts with your saves, settings and packs.", .53f);
        Text(hud, 64, 259, "Download opens the release page for manual installation.", .51f);
    }
    for (int i = 0; i < (error ? 1 : 3); ++i) {
        const float x = kChoices[i].left;
        const bool selected = i == cursor_;
        hud.menuPanels.push_back({x, kTop, x + kWidth, kBottom, selected ? 0xED8F2026u : 0xCD1B2326u});
        if (selected) hud.menuPanels.push_back({x, kTop, x + 3, kBottom, 0xFFF0DBBD});
        Text(hud, x + 14, kTop + 10, error ? "OK" : kChoices[i].label, .65f);
    }
    Text(hud, 64, 360, error ? "ENTER / TRIGGER TO DISMISS" : "ARROWS TO CHOOSE   ENTER / TRIGGER TO SELECT   ESC FOR LATER",
         .42f, device_ui::kMuted);
    if (pointer_.seen && pointer_.x >= 0 && pointer_.x < 640 && pointer_.y >= 0 && pointer_.y < 480)
        device_ui::Target(hud, pointer_.x, pointer_.y);
}

}  // namespace play
