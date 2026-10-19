#pragma once

// Native Wii Remote input. Protocol/aim logic is independent of Windows and testable
// with recorded HID reports. No Dolphin or mouse-emulation dependency.
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include "input/player_state.h"
#include "input/aim_calibration.h"

namespace input {
namespace wii {
constexpr uint16_t Two=0x0001, One=0x0002, B=0x0004, A=0x0008, Minus=0x0010,
                   Home=0x0080, Left=0x0100, Right=0x0200, Down=0x0400, Up=0x0800, Plus=0x1000;
constexpr std::array<uint16_t, 7> Bindable{B,A,Plus,Home,One,Two,Minus};
const char* ButtonName(uint16_t button);
using Point = input::AimPoint;
using Calibration = input::AimCalibration;
struct Profile {
    // Fire, reload, confirm, pause. Assignment is exclusive: changing a binding swaps duplicates.
    std::array<uint16_t,4> buttons{B,A,Plus,Home};
    Calibration calibration;
    int smoothing=1;  // 0 off, 1 light, 2 strong
    void CycleBinding(int action, int direction);
};
struct Settings {
    bool enabled=false, continuous=false;
    std::array<Profile,2> player;
    bool Load(const std::string& file);
    bool Save(const std::string& file) const;
};
struct Remote {
    bool connected=false, reserved=false, tracking=false;
    uint16_t buttons=0, pressed=0;
    int battery=-1, dots=0;
    Point raw{0.5,0.5};
};
struct Snapshot {
    std::array<Remote,2> player;
    bool scanning=false;
    std::string message="Select Connect and press the red SYNC button";
};
// Accepts HID report IDs (without Bluetooth a1 prefix); rejects truncated/unknown reports.
bool DecodeReport(const uint8_t* bytes, size_t size, Remote& remote);
std::array<uint8_t,22> WriteRegister(uint32_t address, const uint8_t* bytes, size_t size);
class Backend {
public:
    virtual ~Backend()=default;
    virtual void Enable(bool enabled, bool continuous)=0;
    virtual void Scan()=0;
    virtual void Disconnect(int player)=0;
    virtual void SwapPlayers()=0;
    virtual Snapshot Poll()=0; // transfers pending button edges to the game thread
};
std::unique_ptr<Backend> CreateBackend();
} // namespace wii

class WiiRemotes final : public GunSource {
public:
    explicit WiiRemotes(GunSource* fallback, std::unique_ptr<wii::Backend> backend=wii::CreateBackend());
    wii::Settings settings;
    void Configure();
    void Connect();
    void Disconnect(int player);
    void SwapPlayers();
    void Update();
    const wii::Snapshot& Status() const { return snapshot_; }
    const char* Name() const override { return "native Wii Remote / pointer"; }
    PlayerState Player(int player) const override;
    void BeginFrame() override;
private:
    GunSource* fallback_;
    std::unique_ptr<wii::Backend> backend_;
    wii::Snapshot snapshot_;
    std::array<PlayerState,2> players_{};
    std::array<bool,2> hadTracking_{};
};
} // namespace input
