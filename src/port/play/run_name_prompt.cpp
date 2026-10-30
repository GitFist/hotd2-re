#include "play/run_name_prompt.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace play {
namespace {
constexpr char kKeys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-";
constexpr size_t kNameLength = 12;
constexpr int kDelete = 40, kClear = 41, kPublish = 42, kSkip = 43;
constexpr uint32_t kText = 0xFFFFF4E5, kMuted = 0xFFB9AA91, kDisabled = 0xFF677072;
constexpr const char* kBoards[] = {"ARCADE", "ORIGINAL", "HORDE", "JUDGMENT", "HIEROPHANT A",
    "HIEROPHANT B", "TOWER A", "TOWER B", "STRENGTH A", "STRENGTH B", "MAGICIAN", "EMPEROR", "BOSS RUSH", "TYPING OF THE DEAD"};

std::string Trim(const std::string& text) {
    const auto first = text.find_first_not_of(' ');
    return first == std::string::npos ? std::string{} : text.substr(first, text.find_last_not_of(' ') - first + 1);
}

void Append(std::string& name, const std::string& text) {
    for (char c : text) {
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
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

std::string RunDetail(int table, uint32_t score, uint16_t rounds) {
    std::string detail = kBoards[table];
    if (table >= 3 && table <= 12) {
        char time[32];
        std::snprintf(time, sizeof(time), " / %u:%02u.%02u", score / 3600, (score / 60) % 60,
                      (score % 60) * 100 / 60);
        detail += time;
    } else {
        detail += " / SCORE " + std::to_string(score);
        if (table == 2) detail += " / ROUND " + std::to_string(rounds);
    }
    return detail;
}
}  // namespace

bool RunNamePrompt::Queue(uint64_t token, int table, uint32_t score, uint16_t rounds,
                          const std::string& initialName) {
    if (!token || token <= newestToken_ || table < 0 || table >= int(std::size(kBoards)) || !score) return false;
    newestToken_ = token_ = token;
    table_ = table;
    score_ = score;
    rounds_ = rounds;
    pending_ = true;
    open_ = submitting_ = accepted_ = attempted_ = false;
    error_.clear();
    cursor_ = 0;
    pointer_ = {};
    pointerVisible_ = typedFocus_ = false;
    if (!initialName.empty()) {
        name_.clear();
        Append(name_, initialName);
    }
    return true;
}

bool RunNamePrompt::Ready(const game::WorldHud& hud) {
    if (hud.stageResults.active) return false;
    return hud.gameMode == 3 || hud.gameMode == 4 || hud.gameMode == 15 ||
           (hud.gameMode == 6 && hud.gameType == 3 && hud.subState == 2 && (hud.flags5900 & 0x18) == 0x18);
}

bool RunNamePrompt::MaybeOpen(const game::WorldHud& hud, bool blocked) {
    if (!pending_ || blocked || !Ready(hud)) return false;
    pending_ = false;
    open_ = true;
    cursor_ = Trim(name_).empty() ? 0 : kPublish;
    pointer_ = {};
    pointerVisible_ = false;
    return true;
}

void RunNamePrompt::Edit(const std::string& text, bool backspace) {
    if (!open_ || attempted_) return;
    if (backspace && !name_.empty()) name_.pop_back();
    Append(name_, text);
    cursor_ = kPublish;
    typedFocus_ = true;
    error_.clear();
}

RunNamePrompt::Action RunNamePrompt::Update(const MenuInput& in) {
    if (!open_) return Action::None;
    pointerVisible_ = Hit(in, 0, 0, 640, 480);
    pointerX_ = in.px;
    pointerY_ = in.py;
    if ((in.pressed & 2) || in.reload || in.start) {
        open_ = false;
        return Action::Skip;
    }
    if (accepted_) {
        if ((!in.trigger && (in.pressed & 0xC)) || (in.trigger && Hit(in, 224, 340, 416, 384))) {
            open_ = false;
            return Action::Skip;
        }
        return Action::None;
    }
    int under = -1;
    for (int i = 0; i < 40; ++i) {
        const float x = 64 + (i % 10) * 52.f, y = 188 + (i / 10) * 40.f;
        if (Hit(in, x, y, x + 44, y + 32)) under = i;
    }
    for (int i = 0; i < 4; ++i)
        if (Hit(in, 64 + i * 130.f, 368, 186 + i * 130.f, 408)) under = 40 + i;
    // Typing before the first input sample must not let a resting pointer select
    // a key instead of Publish. Fresh movement or a trigger still restores aim.
    if (typedFocus_ && !pointer_.seen && in.pointer && !in.trigger) {
        pointer_.Update(in.pointer, in.px, in.py, false);
        typedFocus_ = false;
    } else {
        if (pointer_.Update(in.pointer, in.px, in.py, in.trigger) && under >= 0) cursor_ = under;
        if (in.pointer) typedFocus_ = false;
    }
    if (in.pressed & 0x40) cursor_ = (cursor_ + 43) % 44;
    if (in.pressed & 0x80) cursor_ = (cursor_ + 1) % 44;
    if (in.pressed & 0x10)
        cursor_ = cursor_ >= 40 ? 30 + (cursor_ - 40) * 3 : cursor_ >= 10 ? cursor_ - 10 : 40 + std::min(3, cursor_ / 3);
    if (in.pressed & 0x20)
        cursor_ = cursor_ >= 40 ? (cursor_ - 40) * 3 : cursor_ < 30 ? cursor_ + 10 : 40 + std::min(3, (cursor_ - 30) / 3);
    // A shot outside a button must not activate the keyboard-selected action.
    const int action = in.trigger ? under : (in.pressed & 0xC) ? cursor_ : -1;
    if (action == kSkip) {
        open_ = false;
        return Action::Skip;
    }
    if (submitting_) return Action::None;
    if (!attempted_ && action >= 0 && action < 40) Append(name_, std::string(1, kKeys[action]));
    else if (!attempted_ && action == kDelete && !name_.empty()) name_.pop_back();
    else if (!attempted_ && action == kClear) name_.clear();
    else if (action == kPublish && !Trim(name_).empty()) {
        name_ = Trim(name_);
        BeginSubmitting();
        return Action::Publish;
    }
    if (!attempted_ && action >= 0 && action <= kClear) error_.clear();
    return Action::None;
}

void RunNamePrompt::BeginSubmitting() {
    if (!open_ || accepted_) return;
    submitting_ = attempted_ = true;
    error_.clear();
}

void RunNamePrompt::FinishSubmission(bool accepted, const std::string& error) {
    if (!open_ || !submitting_) return;
    submitting_ = false;
    accepted_ = accepted;
    error_ = accepted ? std::string{} : error.empty() ? "COULD NOT PUBLISH. PLEASE RETRY." : error;
    cursor_ = kPublish;
}

void RunNamePrompt::Reset() {
    pending_ = open_ = submitting_ = accepted_ = attempted_ = false;
    token_ = 0;
    error_.clear();
    pointer_ = {};
    pointerVisible_ = typedFocus_ = false;
}

void RunNamePrompt::ModernHud(game::WorldHud& hud) const {
    if (!open_) return;
    hud.nightShiftPage = 4;
    hud.nightShiftRow = cursor_;
    hud.menuPanels = {{0, 0, 640, 480, 0xDA05090B}, {44, 62, 596, 432, 0xF50B1012},
                      {64, 118, 576, 120, 0xFF92272D}};
    hud.menuText.clear();
    hud.menuScrollHints.clear();
    Text(hud, 64, 76, accepted_ ? "RUN PUBLISHED" : "RUN COMPLETE", .94f);
    Text(hud, 64, 105, RunDetail(table_, score_, rounds_), .43f, kMuted);
    if (accepted_) {
        Text(hud, 80, 170, name_, 1.f);
        Text(hud, 80, 220, "YOUR RUN IS ON THE ONLINE LEADERBOARD", .52f);
        Button(hud, 224, 340, 416, 384, "DONE", true, .72f);
        Text(hud, 24, 453, "ENTER / SHOOT: CONTINUE   ESC / RELOAD: CLOSE", .43f, kMuted);
        if (pointerVisible_) Pointer(hud, pointerX_, pointerY_);
        return;
    }
    Text(hud, 474, 105, std::to_string(name_.size()) + " / 12", .43f, kMuted);
    hud.menuPanels.push_back({64, 128, 576, 168, 0xFF182225});
    Text(hud, 80, 136, name_.empty() ? "ENTER YOUR ONLINE NAME" : name_, .86f, name_.empty() ? 0xFF8B9698 : kText);
    for (int i = 0; i < 40; ++i) {
        const float x = 64 + (i % 10) * 52.f, y = 188 + (i / 10) * 40.f;
        Button(hud, x, y, x + 44, y + 32, kKeys[i] == ' ' ? "SP" : std::string(1, kKeys[i]), cursor_ == i, .65f, !attempted_);
    }
    const char* actions[] = {"DELETE", "CLEAR", error_.empty() ? "PUBLISH" : "RETRY", "SKIP"};
    for (int i = 0; i < 4; ++i) {
        const bool enabled = i == 3 || (i == 2 ? !submitting_ && !Trim(name_).empty() : !attempted_);
        Button(hud, 64 + i * 130.f, 368, 186 + i * 130.f, 408, actions[i], cursor_ == 40 + i, .6f, enabled);
    }
    if (submitting_) Text(hud, 64, 349, "PUBLISHING YOUR RUN...", .42f, kMuted);
    else if (!error_.empty()) Text(hud, 64, 349, error_.substr(0, 62), .42f, 0xFFFFC0A5);
    Text(hud, 64, 416, attempted_ ? "NAME SAVED FOR THIS RUN. RETRY OR SKIP." : "ONLINE: 1-12 CHARACTERS    LOCAL INITIALS: 3", .38f, kMuted);
    Text(hud, 24, 453, "TYPE A NAME OR SELECT KEYS   ENTER: SELECT   ESC / RELOAD: SKIP", .43f, kMuted);
    if (pointerVisible_) Pointer(hud, pointerX_, pointerY_);
}

}  // namespace play
