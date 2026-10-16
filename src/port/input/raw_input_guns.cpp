#include "raw_input_guns.h"

namespace input {

PlayerState RawInputGuns::Player(int index) const {
    PlayerState ps = raw_.Player(index);
    if (!ps.connected) return ps;
    const bool ownsPointer = ps.relative && (index == 0 || !raw_.Player(0).relative);
    if (ownsPointer) {
        POINT pt;
        RECT rc;
        GetCursorPos(&pt);
        ScreenToClient(hwnd_, &pt);
        GetClientRect(hwnd_, &rc);
        ps.x = rc.right > 0 ? static_cast<float>(pt.x) / rc.right : 0.5f;
        ps.y = rc.bottom > 0 ? static_cast<float>(pt.y) / rc.bottom : 0.5f;
        ps.onScreen = ps.x >= 0 && ps.x < 1 && ps.y >= 0 && ps.y < 1;
    }
    return ps;
}

}  // namespace input
