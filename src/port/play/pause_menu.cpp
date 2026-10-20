#include "port/play/pause_menu.h"

#include <algorithm>
#include <fstream>

#include "game/menu_text.h"

namespace play {

namespace {

constexpr float kCellH = 24.0f;    // 0x004ECB84
constexpr float kLabelX = 2.0f;    // cells (the options screen's lines start at cell 2)
constexpr float kConfirmY = 8.0f;  // the question
constexpr float kChoiceY = 10.0f;  // Yes / No
constexpr float kYesX = 12.0f, kNoX = 24.0f;
constexpr uint32_t kSelected = 0x111;  // red, depth 0.9 (the options screen's selected line)
constexpr uint32_t kNormal = 1;
constexpr uint32_t kSndCursor = 0xA9, kSndConfirm = 0x121A9;

const char* const kLabels[play::PauseMenu::kRowCount] = { "Resume", "Music", "Gun Shots", "Sound Effects", "Display", "Wii Remotes", "Light Guns", "Return to Main Menu" };

}  // namespace

bool Settings::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    for (std::string line; std::getline(f, line);) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        while (!val.empty() && (val.back() == '\r' || val.back() == ' ')) val.pop_back();
        const int v = std::clamp(atoi(val.c_str()), 0, 10);
        if (key == "music") music = v;
        else if (key == "guns") guns = v;
        else if (key == "sfx") sfx = v;
        else if (key == "input") input = val;
        else if (key == "fullscreen") display.fullscreen = v != 0;
        else if (key == "scale") display.windowScale = val == "fit" ? 0 : std::clamp(v, 1, 3);
        else if (key == "aspect") display.stretch = val == "stretch";
        else if (key == "crt") display.crt = std::clamp(v, 0, 3);
        else if (key == "render") display.renderScale = std::clamp(v, 1, 4);
        else if (key == "blood" && (val == "red" || val == "green")) display.redBlood = val == "red";
        else if (key == "assets") assets = val;
        else if (key == "lobby") lobby = val;
    }
    return true;
}

bool Settings::Save(const std::string& path) const {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << "music=" << music << "\nguns=" << guns << "\nsfx=" << sfx << "\n";
    if (!input.empty()) f << "input=" << input << "\n";
    f << "fullscreen=" << (display.fullscreen ? 1 : 0) << "\n";
    if (display.windowScale == 0) f << "scale=fit\n";
    else f << "scale=" << display.windowScale << "\n";
    f << "aspect=" << (display.stretch ? "stretch" : "4:3") << "\ncrt=" << display.crt << "\nrender=" << display.renderScale << "\n";
    f << "blood=" << (display.redBlood ? "red" : "green") << "\n";
    if (!assets.empty()) f << "assets=" << assets << "\n";
    if (!lobby.empty()) f << "lobby=" << lobby << "\n";
    return static_cast<bool>(f);
}

void PauseMenu::Open() {
    open_ = true;
    cursor_ = kResume;
    confirm_ = confirmYes_ = false;
    pointer_ = {};
    display_.Close();
}

float PauseMenu::RowY(int row) {
    switch (row) {
    case kResume: return 5.0f;
    case kMusic: return 7.0f;
    case kGuns: return 8.0f;
    case kSfx: return 9.0f;
    case kDisplay: return 11.0f;
    case kWii: return 12.0f;
    case kLightguns: return 13.0f;
    case kReturn: return 14.0f;
    default: return 0.0f;
    }
}

int PauseMenu::RowAt(float px, float py) {
    for (int r = 0; r < kRowCount; ++r)
        if (game::MenuTextRowHit(px, py, RowY(r))) return r;
    return -1;
}

int PauseMenu::ConfirmAt(float px, float py) {
    if (!game::MenuTextRowHit(px, py, kChoiceY)) return -1;
    if (px >= (kYesX - 2.0f) * 16.0f && px < (kYesX + 5.0f) * 16.0f) return 1;
    if (px >= (kNoX - 2.0f) * 16.0f && px < (kNoX + 4.0f) * 16.0f) return 0;
    return -1;
}

int* PauseMenu::Volume(Settings& s, int row) {
    switch (row) {
    case kMusic: return &s.music;
    case kGuns: return &s.guns;
    case kSfx: return &s.sfx;
    default: return nullptr;
    }
}

PauseMenu::Action PauseMenu::Update(const Input& in, Settings& s, std::vector<uint32_t>& sounds) {
    if (!open_) return Action::None;
    if (in.start) {  // the original resumes with the pause button
        sounds.push_back(kSndConfirm);
        display_.Close();
        return Action::Resume;
    }
    if (display_.IsOpen()) {  // the display settings page takes the input; closing it returns to the rows
        display_.Update(in, s.display, sounds);
        return Action::None;
    }
    if (!confirm_) {
        const int under = in.pointer ? RowAt(in.px, in.py) : -1;
        if (pointer_.Update(in.pointer, in.px, in.py, in.trigger || in.reload) && under >= 0 && under != cursor_) {
            cursor_ = under;
            sounds.push_back(kSndCursor);
        }
        if (in.pressed & 0x10) {
            cursor_ = (cursor_ + kRowCount - 1) % kRowCount;
            sounds.push_back(kSndCursor);
        }
        if (in.pressed & 0x20) {
            cursor_ = (cursor_ + 1) % kRowCount;
            sounds.push_back(kSndCursor);
        }
        int* vol = Volume(s, cursor_);
        const bool overCursor = under == cursor_;
        int d = 0;
        if (in.pressed & 0x80) ++d;
        if (in.pressed & 0x40) --d;
        if (vol && overCursor) {
            if (in.trigger) d += game::MenuPointerStep(in.px, 520);
            if (in.reload) --d;
        }
        if (vol && d != 0) {
            const int nv = std::clamp(*vol + d, 0, 10);
            if (nv != *vol) {
                *vol = nv;
                sounds.push_back(kSndCursor);
            }
        }
        const bool select = (in.pressed & 0xC) != 0 || (in.trigger && overCursor && !vol);
        const bool back = (in.pressed & 0x2) != 0 || (in.reload && !vol);
        if (back) {
            sounds.push_back(kSndConfirm);
            return Action::Resume;
        }
        if (select) {
            if (cursor_ == kResume) {
                sounds.push_back(kSndConfirm);
                return Action::Resume;
            }
            if (cursor_ == kWii) return Action::WiiRemotes;
            if (cursor_ == kLightguns) return Action::Lightguns;
            if (cursor_ == kDisplay) {
                display_.Open();
                sounds.push_back(kSndConfirm);
            }
            if (cursor_ == kReturn) {
                confirm_ = true;
                confirmYes_ = false;  // No is the safe default
                pointer_ = {};
                sounds.push_back(kSndConfirm);
            }
        }
        return Action::None;
    }
    // the confirmation: Yes / No
    const int under = in.pointer ? ConfirmAt(in.px, in.py) : -1;
    if (pointer_.Update(in.pointer, in.px, in.py, in.trigger || in.reload) && under >= 0 && (under == 1) != confirmYes_) {
        confirmYes_ = under == 1;
        sounds.push_back(kSndCursor);
    }
    if (in.pressed & 0xF0) {
        confirmYes_ = !confirmYes_;
        sounds.push_back(kSndCursor);
    }
    const bool select = (in.pressed & 0xC) != 0 || (in.trigger && under == (confirmYes_ ? 1 : 0));
    const bool back = (in.pressed & 0x2) != 0 || in.reload;
    if (back) {
        confirm_ = false;
        sounds.push_back(kSndCursor);
        return Action::None;
    }
    if (select) {
        sounds.push_back(kSndConfirm);
        if (confirmYes_) return Action::ReturnToTitle;
        confirm_ = false;
    }
    return Action::None;
}

void PauseMenu::Sprites(const game::ExeImage& exe, int pausePlayer, const Settings& s, std::vector<game::SpriteDef>& out) const {
    if (!open_) return;
    if (display_.IsOpen()) {  // the display settings page is the whole screen (its own background and heading)
        display_.Sprites(exe, s.display, out);
        return;
    }
    game::MenuBackground(out);
    // the original's pause label (0x0040E500: 0x8E7 P1 / 0x8E8 P2, depth 0.6, flags 0x70A) as the title, where the
    // options screen has its title
    game::SpriteDef t;
    t.id = pausePlayer == 1 ? 0x8E8 : 0x8E7;
    t.x = 320.0f;
    t.y = 56.0f;
    t.depth = 0.6f;
    t.flags = 0x70A;
    out.push_back(t);
    if (!confirm_) {
        for (int r = 0; r < kRowCount; ++r)
            game::MenuText(exe, kLabelX, RowY(r), r == kReturn && networkRoom_ ? "Return to Lobby" : kLabels[r], r == cursor_ ? kSelected : kNormal, out);
        if (cursor_ >= kMusic && cursor_ <= kSfx)
            game::MenuAdjustArrows(520, 620, RowY(cursor_) * kCellH + 22, out);
        game::MenuTextScaled(exe, 32, 370, "Click or shoot   Reload to reverse", 0, 0.8f, out);
        // (the volume values are drawn by the caller with game::MenuNum at cell x 35, the options screen's number column)
    } else {
        game::MenuText(exe, 0.0f, kConfirmY, networkRoom_ ? "Return both players to Lobby?" : "Return to the Main Menu?", 2, out);
        game::MenuText(exe, kYesX, kChoiceY, "Yes", confirmYes_ ? kSelected : kNormal, out);
        game::MenuText(exe, kNoX, kChoiceY, "No", confirmYes_ ? kNormal : kSelected, out);
    }
}

}  // namespace play
