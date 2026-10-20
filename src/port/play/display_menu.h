#pragma once

// play.exe's display settings page (not in the original), drawn in the options screen's look (game/menu_text.h) and
// shown from two places: the pause menu's "Display" row (pause_menu.h) and the main menu's Options screen, whose
// port-only "Display Settings" row (world_modes_options.cpp row 14) asks play.exe to show this page over it
// (WorldHud::portDisplayRequests). Pure state machine (tests/pause_menu_test.cpp); the caller applies the settings
// (notes/agents/display.md): window / fullscreen through the platform layer, the present mapping and the internal
// resolution through the GPU device.
//   Rows: Fullscreen (On / Off), Window Scale (1x / 2x / 3x of 640x480, Fit = the usable desktop; windowed only),
//   Aspect (4:3 pillarboxed / Stretch), CRT Filter (Off, Scanlines 1..3), Render Scale (1x..4x: the 3D's internal
//   resolution; the 2D stays in 640x480 space), Back.
//   Keyboard: Up / Down move, Left / Right change a value, Enter / Space on a value = next value, on Back = close,
//   Backspace = close. Pointer: aiming at a row highlights it; trigger = next value / Back, reload = previous value /
//   close. The start button is left to the caller (the pause menu resumes with it).

#include <cstdint>
#include <string>
#include <vector>

#include "game/exe_image.h"
#include "game/sprite.h"
#include "game/menu_pointer.h"

namespace play {

struct DisplaySettings {
    bool fullscreen = false;
    int windowScale = 2;   // 1..3: that multiple of 640x480 as the windowed client size; 0: fit the usable desktop
    bool stretch = false;  // false: the 4:3 image centred with bars; true: stretched to the window
    bool redBlood = false; // local cosmetic choice: false = green, true = red
    int crt = 0;           // 0 off, 1..3 scanline strength
    int renderScale = 2;   // internal resolution of the frame: 1..4 x 640x480 (1 = the original; default 2 = the 2x window's
                           // own pixels, at least as sharp as the earlier 960x720 default)
    bool operator==(const DisplaySettings& o) const {
        return fullscreen == o.fullscreen && windowScale == o.windowScale && stretch == o.stretch && crt == o.crt && renderScale == o.renderScale && redBlood == o.redBlood;
    }
    bool operator!=(const DisplaySettings& o) const { return !(*this == o); }
};

// Input of one frame for the port's menus (the pause menu and this page).
struct MenuInput {
    uint32_t pressed = 0;  // pad bits that went down this frame: 0x10 up, 0x20 down, 0x40 left, 0x80 right, 0x8 enter,
                           // 0x4 confirm, 0x2 back
    bool pointer = false;  // a pointing device aims at (px, py), 640 x 480 game pixels
    float px = 0, py = 0;
    bool trigger = false;  // the device's trigger went down this frame
    bool reload = false;   // the reload button went down
    bool start = false;    // the start button went down (the original's resume)
};

class DisplayMenu {
public:
    enum Row { kFullscreen = 0, kScale, kAspect, kCrt, kRender, kBlood, kBack, kRowCount };

    void Open();
    void Close() { open_ = false; }
    bool IsOpen() const { return open_; }

    // One frame of the open page: edits d in place; sounds: SE ids to play (0xA9 cursor, 0x121A9 confirm). Returns true
    // when the page was closed (Back / back button).
    bool Update(const MenuInput& in, DisplaySettings& d, std::vector<uint32_t>& sounds);
    // The page's sprites: the menu background, the heading, the rows with their values (red = selected).
    // overScene: over the paused game instead of the tiled background (play.exe: the settings show live): no background
    // tiles, and the sprites sit at depth 0.0002 with depth func "always", in front of the pause overlay 0x93E (0.0005).
    void Sprites(const game::ExeImage& exe, const DisplaySettings& d, std::vector<game::SpriteDef>& out, bool overScene = false) const;

    int Cursor() const { return cursor_; }

    // Layout in cells (16 x 24 px); RowAt: the row under a pointer at game pixel (px, py), -1 = none.
    static float RowY(int row);
    static int RowAt(float px, float py);
    static const char* Label(int row);
    static std::string ValueText(const DisplaySettings& d, int row);
    // Cycles the value of a row by d steps (wrapping): 1x / 2x / 3x / Fit, Off / On, ...; no-op for Back.
    static void Step(DisplaySettings& d, int row, int delta);

private:
    bool open_ = false;
    int cursor_ = kFullscreen;
    game::MenuPointerFocus pointer_;
};

}  // namespace play
