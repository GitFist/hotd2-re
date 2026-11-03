#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace input::feedback {

// Append values when adding a family: these numbers are local saved preferences.
enum class GunType { Off = 0, OpenFire = 1, Gun4ir = 2, Blamcon = 3, Sinden = 4 };
constexpr int kSerialGunTypeCount = 4;
constexpr int kGunTypeCount = 5;
const char* GunTypeName(GunType type);

enum class GunCommand { Begin, Shot, Damage, Stop, End };
// Firmware owns each pulse's duration. No command holds a solenoid continuously on.
std::string EncodeGunCommand(GunType type, GunCommand command);

// A factory seam lets tests exercise the actual worker without touching hardware.
// Open/Write/Close are called only on the worker, never on the gameplay thread.
class SerialTransport {
public:
    virtual ~SerialTransport() = default;
    virtual bool Open(int comPort) = 0;
    virtual bool Write(std::string_view bytes) = 0;
    virtual void Close() = 0;
};
using SerialTransportFactory = std::function<std::unique_ptr<SerialTransport>()>;

class GunFeedback {
public:
    explicit GunFeedback(SerialTransportFactory factory = {});
    ~GunFeedback();
    GunFeedback(const GunFeedback&) = delete;
    GunFeedback& operator=(const GunFeedback&) = delete;

    // Serial output is off until configured AND active. Only COM1..COM256 are
    // accepted; a port cannot be assigned to both players. Sinden is a separate
    // driver adapter and is treated as Off here. Repeating an unchanged config
    // does not reconnect. A failed connection retries on config/reactivation.
    bool Configure(int localSlot, GunType type, int comPort);
    void SetActive(bool active);
    void Shot(int localSlot);
    void Damage(int localSlot);
    void Stop(); // discard queued pulses, release serial handoff asynchronously
    void Close(); // stop the worker and close both sessions (shutdown only)
    void Update() {} // worker-driven; retained for the normal device update loop
    std::string Status(int localSlot) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace input::feedback
