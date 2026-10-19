#pragma once
#include "aim_calibration.h"
#include "player_state.h"
#include <array>
#include <map>
#include <string>

namespace input {
// Mouse button bits: left, right, middle, X1, X2. Distinct from game action bits.
constexpr std::array<uint8_t,5> kGunButtons{1,2,4,8,16};
const char* GunButtonName(uint8_t button);
struct LightgunProfile {
    std::string device; // Opaque local identifier; never a device path or display name.
    std::array<uint8_t,4> buttons{1,2,8,4}; // Fire, Reload, Confirm, Pause
    AimCalibration calibration;
    void Bind(int action,uint8_t button);
};
struct LightgunSettings {
    bool manual=false;
    std::array<LightgunProfile,2> player;
    bool Load(const std::string& file);
    bool Save(const std::string& file) const;
};
struct GunReading {
    bool connected=false, onScreen=false, relative=false;
    AimPoint aim{.5,.5};
    uint8_t held=0, pressed=0, released=0, suppressed=0;
    uint64_t sequence=0;
};
// Device-independent state machine. Windows supplies per-device reports; tests use
// synthetic reports. All coordinates exposed to callers are over the game image.
class Lightguns final : public GunSource {
public:
    LightgunSettings settings;
    const char* Name() const override { return "light guns"; }
    PlayerState Player(int player) const override;
    void BeginFrame() override;
    void Feed(const std::string& device,float x,float y,bool inside,bool relative,uint8_t down,uint8_t up);
    void RemoveDevice(const std::string& device);
    void ClearLive();
    void SetImage(float x,float y,float width,float height);
    GunReading Reading(int player) const;
    void BeginAssign(int player);
    void CancelAssign() { assigning_=-1; }
    int Assigning() const { return assigning_; }
    bool TakeAssigned();
    void SwapPlayers();
    void ClearAssignments();
    void ForgetPlayer(int player);
    void ReleaseButtons();
    int ConnectedCount() const;
private:
    std::map<std::string,GunReading> devices_;
    std::array<float,4> image_{0,0,1,1};
    int assigning_=-1;
    bool assigned_=false;
    uint64_t sequence_=0, assignAfter_=0;
};
}
