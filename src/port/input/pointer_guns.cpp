#include "pointer_guns.h"

namespace input {

namespace {
uint8_t ButtonBit(int button) {
    return button == 1 ? kTrigger : button == 3 ? kReload : button == 2 ? kStart : 0;
}
void Press(PlayerState& p, uint8_t bit, bool down) {
    if (down) {
        p.pressed |= bit & ~p.held;
        p.held |= bit;
    } else {
        p.released |= bit & p.held;
        p.held &= ~bit;
    }
}
}  // namespace

int PointerGuns::PlayerOf(uint32_t device, bool join) {
    if (!multiDevice) device = 0;
    auto it = devicePlayer_.find(device);
    if (it != devicePlayer_.end()) return it->second;
    if (!join) return -1;
    for (int i = 0; i < kMaxPlayers; ++i) {
        if (!players_[i].connected) {
            players_[i] = PlayerState{};
            players_[i].connected = true;
            devicePlayer_[device] = i;
            return i;
        }
    }
    return -1;
}

void PointerGuns::OnMotion(uint32_t device, float nx, float ny) {
    const int p = PlayerOf(device, false);
    if (p < 0) return;
    players_[p].x = nx;
    players_[p].y = ny;
    players_[p].onScreen = nx >= 0 && nx < 1 && ny >= 0 && ny < 1;
}

void PointerGuns::OnButton(uint32_t device, int button, bool down, float nx, float ny) {
    const uint8_t bit = ButtonBit(button);
    if (!bit) return;
    const int p = PlayerOf(device, down && bit == kTrigger);  // unassigned devices join on their first trigger pull
    if (p < 0) return;
    OnMotion(device, nx, ny);
    Press(players_[p], bit, down);
}

void PointerGuns::OnStartKey(bool down) {
    int p = devicePlayer_.count(0) ? devicePlayer_[0] : -1;
    if (p < 0 && down) {
        if (players_[0].connected) return;  // P1 is another device
        players_[0] = PlayerState{};
        players_[0].connected = true;
        devicePlayer_[0] = p = 0;
    }
    if (p >= 0) Press(players_[p], kStart, down);
}

PlayerState PointerGuns::Player(int index) const {
    PlayerState ps = players_[index];
    if (!ps.connected || multiDevice || !pointerQuery) return ps;
    float nx, ny;
    if (pointerQuery(&nx, &ny)) {
        ps.x = nx;
        ps.y = ny;
        ps.onScreen = nx >= 0 && nx < 1 && ny >= 0 && ny < 1;
    }
    return ps;
}

void PointerGuns::BeginFrame() {
    for (PlayerState& p : players_) p.pressed = p.released = 0;
}

}  // namespace input
