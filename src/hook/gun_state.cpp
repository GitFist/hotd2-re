#include "gun_state.h"

namespace hook {

namespace {
uint32_t ToGameBits(uint8_t b) {
    uint32_t r = 0;
    if (b & input::kTrigger) r |= kBitTrigger;
    if (b & input::kReload)  r |= kBitReload;
    if (b & input::kStart)   r |= kBitStart;
    return r;
}
}  // namespace

GunSlotState ComputeGunSlot(const input::PlayerState& p, uint32_t& prevHeld, const AimConfig& cfg) {
    GunSlotState s;
    s.held = ToGameBits(p.held);
    s.pressed = ToGameBits(p.pressed);
    s.released = prevHeld & ~s.held;
    prevHeld = s.held;

    float gx, gy;
    const bool inImage = input::ApplyAspect(p.x, p.y, cfg.displayW, cfg.displayH, cfg.aspect, &gx, &gy);
    const bool onScreen = p.onScreen && inImage;
    input::NormalizedToGame(gx, gy, cfg.gameW, cfg.gameH, &s.x, &s.y);

    // Mode 5: pressed trigger -> fired, pressed reload -> off-screen; both -> off-screen only.
    // Like a real light gun, the trigger while aimed off the image is an off-screen (reload) shot.
    s.fired = (s.pressed & kBitTrigger) ? 1 : 0;
    s.offscreen = (s.pressed & kBitReload) ? 1 : 0;
    if (s.fired && !onScreen) {
        s.fired = 0;
        s.offscreen = 1;
    }
    if (s.fired && s.offscreen) {
        s.fired = 0;
    }
    return s;
}

}  // namespace hook
