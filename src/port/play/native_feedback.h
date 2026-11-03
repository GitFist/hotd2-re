#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "game/world.h"

namespace play {

struct FeedbackContext {
    bool active = true, headless = false, menu = false, replay = false, stalled = false, desynced = false;
    bool online = false;
    int onlinePlayer = 0;
    bool Allows(const game::WorldHud& hud) const {
        return active && !headless && !menu && !replay && !stalled && !desynced &&
               hud.gameMode == 6 && !hud.paused && !hud.stageResults.active;
    }
};

// Network player two still uses this PC's physical player-one device. Remote
// events and replay/catch-up frames must never reach a local feedback device.
inline game::WorldFeedback RouteFeedback(const game::WorldFeedback& events, const game::WorldHud& hud,
                                         const FeedbackContext& context) {
    if (!context.Allows(hud)) return {};
    if (!context.online) return events;
    game::WorldFeedback local;
    if (context.onlinePlayer >= 0 && context.onlinePlayer < 2) {
        local.shot[0] = events.shot[context.onlinePlayer];
        local.damage[0] = events.damage[context.onlinePlayer];
    }
    return local;
}

struct RumbleOutput {
    uint16_t low = 0, high = 0;
    uint32_t milliseconds = 0;
    bool operator==(const RumbleOutput&) const = default;
};

// Wall-clock deadlines keep effects finite even when simulation is paused.
// A shot during damage adds a short sharp pulse without truncating the hit.
class FeedbackRumble {
public:
    void Shot(double now) { shotUntil_ = now + .045; }
    void Damage(double now) { damageUntil_ = now + .180; }
    void Stop() { shotUntil_ = damageUntil_ = 0; }
    RumbleOutput Output(double now, int strength) const {
        strength = std::clamp(strength, 0, 100);
        const bool shot = now < shotUntil_, damage = now < damageUntil_;
        if (!strength || (!shot && !damage)) return {};
        RumbleOutput output;
        output.low = damage ? uint16_t(50000 * strength / 100) : 0;
        output.high = uint16_t((shot ? 45000 : 25000) * strength / 100);
        // Wake at the next transition, then recompute the mixed motor state.
        double until = shot && damage ? std::min(shotUntil_, damageUntil_) : shot ? shotUntil_ : damageUntil_;
        output.milliseconds = uint32_t(std::clamp((until - now) * 1000.0 + 1.0, 1.0, 181.0));
        return output;
    }
private:
    double shotUntil_ = 0, damageUntil_ = 0;
};

} // namespace play
