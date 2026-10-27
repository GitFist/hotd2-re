#include "play/username_prompt.h"

#include <algorithm>
#include <cstring>

namespace play {
namespace {
constexpr char kKeys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-";
static_assert(sizeof(kKeys) == 41);  // forty selectable characters, followed by the terminator
constexpr size_t kNameLength = 12;
constexpr int kDelete = 40, kClear = 41, kSave = 42, kCancel = 43;
constexpr uint32_t kText = 0xFFFFF4E5, kMuted = 0xFFB9AA91, kDisabled = 0xFF677072;

char Upper(char c) { return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c; }
void Append(std::string& name, const std::string& text) {
    for (char c : text) {
        c = Upper(c);
        if (c && std::strchr(kKeys, c) && name.size() < kNameLength) name += c;
    }
}
void Text(game::WorldHud& hud, float x, float y, const std::string& text, float scale, uint32_t color = kText) {
    hud.menuText.push_back({x, y, text, scale, color});
}
void Button(game::WorldHud& hud, float x, float y, float right, float bottom,
            const std::string& text, bool selected, float scale, bool enabled = true) {
    hud.menuPanels.push_back({x, y, right, bottom, selected && enabled ? 0xED8F2026u : 0x75101315u});
    if (selected && enabled) hud.menuPanels.push_back({x, y, x + 3, bottom, 0xFFF0DBBD});
    Text(hud, x + 14, y + (bottom - y - 20 * scale) * .5f, text, scale,
         !enabled ? kDisabled : selected ? kText : 0xFFD1CCC2);
}
bool Hit(const MenuInput& in, float x, float y, float right, float bottom) {
    return in.pointer && in.px >= x && in.px < right && in.py >= y && in.py < bottom;
}
void Pointer(game::WorldHud& hud, float x, float y) {
    hud.menuPanels.insert(hud.menuPanels.end(), {
        {x - 12, y - 3, x - 3, y + 3, 0xFF000000}, {x + 3, y - 3, x + 12, y + 3, 0xFF000000},
        {x - 3, y - 12, x + 3, y - 3, 0xFF000000}, {x - 3, y + 3, x + 3, y + 12, 0xFF000000},
        {x - 10, y - 1, x - 4, y + 1, kText}, {x + 4, y - 1, x + 10, y + 1, kText},
        {x - 1, y - 10, x + 1, y - 4, kText}, {x - 1, y + 4, x + 1, y + 10, kText}});
}
}  // namespace

std::string NormalizeUsername(const std::string& text) {
    const auto first = text.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    std::string name = text.substr(first, text.find_last_not_of(' ') - first + 1);
    if (name.size() > kNameLength) return {};
    for (char& c : name) {
        c = Upper(c);
        if (!c || !std::strchr(kKeys, c)) return {};
    }
    return name;
}
bool ValidUsername(const std::string& text) { return !text.empty() && NormalizeUsername(text) == text; }

void UsernamePrompt::Open(const std::string& initialName, bool required) {
    open_ = true;
    required_ = required;
    name_ = NormalizeUsername(initialName);
    error_.clear();
    cursor_ = name_.empty() ? 0 : kSave;
    pointer_ = {};
    pointerVisible_ = typedFocus_ = false;
}
void UsernamePrompt::Edit(const std::string& text, bool backspace) {
    if (!open_) return;
    if (backspace && !name_.empty()) name_.pop_back();
    Append(name_, text);
    cursor_ = kSave;
    typedFocus_ = true;
    error_.clear();
}
UsernamePrompt::Action UsernamePrompt::Update(const MenuInput& in) {
    if (!open_) return Action::None;
    pointerVisible_ = Hit(in, 0, 0, 640, 480);
    pointerX_ = in.px;
    pointerY_ = in.py;
    if ((in.pressed & 2) || in.reload || in.start) {
        if (required_) return Action::None;
        Close();
        return Action::Cancel;
    }
    int under = -1;
    for (int i = 0; i < 40; ++i) {
        const float x = 64 + (i % 10) * 52.f, y = 188 + (i / 10) * 40.f;
        if (Hit(in, x, y, x + 44, y + 32)) under = i;
    }
    for (int i = 0; i < 4; ++i)
        if (Hit(in, 64 + i * 130.f, 368, 186 + i * 130.f, 408)) under = 40 + i;
    // Typing retains Enter-to-save when the first pointer sample is resting over a key.
    if (typedFocus_ && !pointer_.seen && in.pointer && !in.trigger) {
        pointer_.Update(in.pointer, in.px, in.py, false);
        typedFocus_ = false;
    } else {
        if (pointer_.Update(in.pointer, in.px, in.py, in.trigger) && under >= 0) cursor_ = under;
        if (in.pointer) typedFocus_ = false;
    }
    const int count = required_ ? 43 : 44;
    if (in.pressed & 0x40) cursor_ = (cursor_ + count - 1) % count;
    if (in.pressed & 0x80) cursor_ = (cursor_ + 1) % count;
    if (in.pressed & 0x10)
        cursor_ = cursor_ >= 40 ? 30 + (cursor_ - 40) * 3 : cursor_ >= 10 ? cursor_ - 10 : 40 + std::min(count - 41, cursor_ / 3);
    if (in.pressed & 0x20)
        cursor_ = cursor_ >= 40 ? (cursor_ - 40) * 3 : cursor_ < 30 ? cursor_ + 10 : 40 + std::min(count - 41, (cursor_ - 30) / 3);
    // Clicking outside a control must not activate the keyboard-selected action.
    const int action = in.trigger ? under : (in.pressed & 0xC) ? cursor_ : -1;
    if (action == kCancel && !required_) {
        Close();
        return Action::Cancel;
    }
    if (action >= 0 && action < 40) Append(name_, std::string(1, kKeys[action]));
    else if (action == kDelete && !name_.empty()) name_.pop_back();
    else if (action == kClear) name_.clear();
    else if (action == kSave) {
        const std::string name = NormalizeUsername(name_);
        if (!name.empty()) {
            name_ = name;
            return Action::Save;
        }
        error_ = "ENTER A USERNAME BEFORE CONTINUING";
    }
    if (action >= 0 && action <= kClear) error_.clear();
    return Action::None;
}
void UsernamePrompt::ModernHud(game::WorldHud& hud) const {
    if (!open_) return;
    hud.nightShiftPage = 4;
    hud.nightShiftRow = cursor_;
    hud.menuPanels = {{0, 0, 640, 480, 0xDA05090B}, {44, 54, 596, 440, 0xF50B1012},
                      {64, 118, 576, 120, 0xFF92272D}};
    hud.menuText.clear();
    hud.menuScrollHints.clear();
    Text(hud, 64, 69, required_ ? "CHOOSE YOUR USERNAME" : "CHANGE USERNAME", .86f);
    Text(hud, 64, 101, "SCORES ARE AUTOMATICALLY SHARED ON THE PUBLIC LEADERBOARD", .37f, kMuted);
    hud.menuPanels.push_back({64, 128, 576, 168, 0xFF182225});
    Text(hud, 80, 136, name_.empty() ? "ENTER YOUR USERNAME" : name_, .86f, name_.empty() ? 0xFF8B9698 : kText);
    Text(hud, 510, 174, std::to_string(name_.size()) + " / 12", .38f, kMuted);
    for (int i = 0; i < 40; ++i) {
        const float x = 64 + (i % 10) * 52.f, y = 188 + (i / 10) * 40.f;
        Button(hud, x, y, x + 44, y + 32, kKeys[i] == ' ' ? "SP" : std::string(1, kKeys[i]), cursor_ == i, .65f);
    }
    const char* actions[] = {"DELETE", "CLEAR", "SAVE", "CANCEL"};
    for (int i = 0; i < (required_ ? 3 : 4); ++i)
        Button(hud, 64 + i * 130.f, 368, 186 + i * 130.f, 408, actions[i], cursor_ == 40 + i, .6f,
               i != 2 || !NormalizeUsername(name_).empty());
    if (!error_.empty()) Text(hud, 64, 349, error_.substr(0, 62), .42f, 0xFFFFC0A5);
    Text(hud, 64, 416, required_ ? "CHANGE IT ANYTIME IN SETTINGS > GAMEPLAY > USERNAME"
                                : "YOUR PREVIOUS AND QUEUED SCORES KEEP THEIR ORIGINAL NAME", .36f, kMuted);
    Text(hud, 24, 453, required_ ? "TYPE OR SELECT KEYS   ENTER: SELECT / SAVE"
                                : "TYPE OR SELECT KEYS   ENTER: SELECT / SAVE   ESC: CANCEL", .43f, kMuted);
    if (pointerVisible_) Pointer(hud, pointerX_, pointerY_);
}

}  // namespace play
