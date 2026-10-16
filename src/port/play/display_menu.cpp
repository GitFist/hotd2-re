#include "port/play/display_menu.h"

#include "game/menu_text.h"

namespace play {

namespace {

constexpr float kCellH = 24.0f;   // 0x004ECB84
constexpr float kLabelX = 2.0f;   // cells (the options screen's lines start at cell 2)
constexpr float kValueX = 24.0f;  // cells
constexpr float kHeadingY = 2.0f;
constexpr uint32_t kSelected = 0x111;  // red, depth 0.9 (the options screen's selected line)
constexpr uint32_t kNormal = 1;
constexpr uint32_t kSndCursor = 0xA9, kSndConfirm = 0x121A9;

int Wrap(int v, int n) { return ((v % n) + n) % n; }

}  // namespace

void DisplayMenu::Open() {
    open_ = true;
    cursor_ = kFullscreen;
    pointer_ = {};
}

float DisplayMenu::RowY(int row) {
    switch (row) {
    case kFullscreen: return 5.0f;
    case kScale: return 6.0f;
    case kAspect: return 7.0f;
    case kCrt: return 8.0f;
    case kRender: return 9.0f;
    case kBack: return 11.0f;
    default: return 0.0f;
    }
}

int DisplayMenu::RowAt(float px, float py) {
    for (int r = 0; r < kRowCount; ++r)
        if (game::MenuTextRowHit(px, py, RowY(r))) return r;
    return -1;
}

const char* DisplayMenu::Label(int row) {
    static const char* const kLabels[kRowCount] = { "Fullscreen", "Window Scale", "Aspect", "CRT Filter", "Render Scale", "Back" };
    return row >= 0 && row < kRowCount ? kLabels[row] : "";
}

std::string DisplayMenu::ValueText(const DisplaySettings& d, int row) {
    switch (row) {
    case kFullscreen: return d.fullscreen ? "On" : "Off";
    case kScale:
        if (d.fullscreen) return "";  // the window scale means nothing in fullscreen
        return d.windowScale == 0 ? "Fit" : std::to_string(d.windowScale) + "x";
    case kAspect: return d.stretch ? "Stretch" : "4 by 3";
    case kCrt: return d.crt <= 0 ? "Off" : "Scanlines " + std::to_string(d.crt);
    case kRender: return std::to_string(d.renderScale) + "x";
    default: return "";
    }
}

void DisplayMenu::Step(DisplaySettings& d, int row, int delta) {
    switch (row) {
    case kFullscreen:
        if (delta % 2 != 0) d.fullscreen = !d.fullscreen;
        break;
    case kScale: {  // 1x, 2x, 3x, Fit (0)
        const int pos = d.windowScale == 0 ? 3 : d.windowScale - 1;
        const int np = Wrap(pos + delta, 4);
        d.windowScale = np == 3 ? 0 : np + 1;
        break;
    }
    case kAspect:
        if (delta % 2 != 0) d.stretch = !d.stretch;
        break;
    case kCrt: d.crt = Wrap(d.crt + delta, 4); break;
    case kRender: d.renderScale = Wrap(d.renderScale - 1 + delta, 4) + 1; break;
    default: break;
    }
}

bool DisplayMenu::Update(const MenuInput& in, DisplaySettings& d, std::vector<uint32_t>& sounds) {
    if (!open_) return false;
    const int under = in.pointer ? RowAt(in.px, in.py) : -1;
    if (pointer_.Update(in.pointer, in.px, in.py, in.trigger || in.reload) && under >= 0 && under != cursor_) {
        cursor_ = under;
        sounds.push_back(kSndCursor);
    }
    if (in.pressed & 0x10) {
        cursor_ = Wrap(cursor_ - 1, kRowCount);
        sounds.push_back(kSndCursor);
    }
    if (in.pressed & 0x20) {
        cursor_ = Wrap(cursor_ + 1, kRowCount);
        sounds.push_back(kSndCursor);
    }
    const bool value = cursor_ != kBack;
    const bool overCursor = under == cursor_;
    int delta = 0;
    if (in.pressed & 0x80) ++delta;
    if (in.pressed & 0x40) --delta;
    if (value) {
        if (in.pressed & 0xC) ++delta;  // Enter / Space on a value: the next one
        if (overCursor && in.trigger) delta += game::MenuPointerStep(in.px, 360);
        if (overCursor && in.reload) --delta;
    }
    if (value && delta != 0 && !(cursor_ == kScale && d.fullscreen)) {
        const DisplaySettings before = d;
        Step(d, cursor_, delta);
        if (d != before) sounds.push_back(kSndCursor);
    }
    const bool select = !value && ((in.pressed & 0xC) != 0 || (in.trigger && overCursor));
    const bool back = (in.pressed & 0x2) != 0 || (in.reload && !(value && overCursor));
    if (select || back) {
        sounds.push_back(kSndConfirm);
        open_ = false;
        return true;
    }
    return false;
}

void DisplayMenu::Sprites(const game::ExeImage& exe, const DisplaySettings& d, std::vector<game::SpriteDef>& out, bool overScene) const {
    if (!open_) return;
    const size_t first = out.size();
    if (!overScene) game::MenuBackground(out);
    game::MenuText(exe, 0.0f, kHeadingY, "Display Settings", 2, out);  // centred
    for (int r = 0; r < kRowCount; ++r) {
        const uint32_t flags = r == cursor_ ? kSelected : kNormal;
        game::MenuText(exe, kLabelX, RowY(r), Label(r), flags | (r == kScale && d.fullscreen ? 8 : 0), out);
        const std::string v = ValueText(d, r);
        if (!v.empty()) game::MenuText(exe, kValueX, RowY(r), v, flags, out);
    }
    if (cursor_ != kBack && !(cursor_ == kScale && d.fullscreen))
        game::MenuAdjustArrows(360, 620, RowY(cursor_) * kCellH + 22, out);
    game::MenuTextScaled(exe, 32, 340, "Click or shoot   Reload to reverse", 0, 0.8f, out);
    if (overScene)
        for (size_t i = first; i < out.size(); ++i) {
            out[i].depth = 0.0002f;
            out[i].flags |= 0x700;
        }
}

}  // namespace play
