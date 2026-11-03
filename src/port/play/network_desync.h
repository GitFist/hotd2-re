#pragma once

#include "play/display_menu.h"

namespace play {

// A mismatched lockstep world must stop before another simulation tick. The
// socket stays alive behind this local error screen to deliver the final hashes.
class NetworkDesync {
public:
    bool Observe(bool mismatched, uint32_t frame) {
        if (!mismatched || active_) return false;
        active_ = true; frame_ = frame; return true;
    }
    bool Active() const { return active_; }
    uint32_t Frame() const { return frame_; }
    template<class Step> bool Advance(Step&& step) const {
        if (active_) return false;
        step(); return true;
    }
    bool ReturnRequested(const MenuInput& input) const {
        if (!active_) return false;
        return input.start || (input.pressed & 0xEu) || input.reload ||
            (input.trigger && input.pointer && input.px >= 156 && input.px <= 484 && input.py >= 274 && input.py <= 320);
    }
    void ModernHud(game::WorldHud& hud) const {
        if (!active_) return;
        hud.menuPanels.push_back({0,0,640,480,0xC0000000});
        hud.menuPanels.push_back({72,156,568,340,0xF00C1114});
        hud.menuPanels.push_back({72,156,568,159,0xFF92272D});
        hud.menuText.push_back({104,180,"ONLINE GAME OUT OF SYNC",.86f,0xFFF1EBDE});
        hud.menuText.push_back({104,223,"PLAY HAS STOPPED TO PREVENT DIFFERENT RESULTS.",.42f,0xFFBFC0B9});
        hud.menuText.push_back({104,246,"FRAME " + std::to_string(frame_),.40f,0xFFBFC0B9});
        hud.menuPanels.push_back({156,274,484,320,0xFF92272D});
        hud.menuText.push_back({186,287,"RETURN TO MAIN MENU",.64f,0xFFF1EBDE});
    }
    void Reset() { active_ = false; frame_ = 0; }
private:
    bool active_ = false;
    uint32_t frame_ = 0;
};
} // namespace play
