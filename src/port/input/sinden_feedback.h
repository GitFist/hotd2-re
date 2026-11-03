#pragma once

#include <memory>
#include <string>

namespace input::feedback {

// Native recoil through the Sinden Windows driver's documented TCP server.
// Defaults are disabled. All socket work is on a worker; calls below only queue
// finite pulses. localSlot is the physical player on this PC, not the net slot.
class SindenFeedback {
public:
    SindenFeedback();
    ~SindenFeedback();
    SindenFeedback(const SindenFeedback&) = delete;
    SindenFeedback& operator=(const SindenFeedback&) = delete;

    // driverGun: 0 disabled, 1 Lightgun A, 2 Lightgun B. Duplicate assignments
    // to the same server block P2 until resolved. Settings cancel queued pulses.
    void Configure(int localSlot, int driverGun, int tcpPort = 13000);
    void SetActive(bool active);
    void Shot(int localSlot);
    void Damage(int localSlot); // Sinden has no separate damage/rumble motor.
    void Stop();
    void Close();
    std::string Status(int localSlot) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace input::feedback
