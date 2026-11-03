#include "input/native_gun_feedback.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace {
using namespace input::feedback;
using namespace std::chrono_literals;
int failures = 0, checks = 0;
void Check(bool ok, const char* text) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", text); }
}
template<class Predicate> bool Wait(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + 1500ms;
    while (std::chrono::steady_clock::now() < end) {
        if (predicate()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}
struct Record { int port; std::string bytes; };
struct MockState {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<Record> records;
    std::map<int, int> opens, closes;
    bool failOpen = false, failWrite = false, blockShot = false, shotBlocked = false;
    int Count(int port, const std::string& bytes) {
        std::lock_guard lock(mutex);
        int count = 0;
        for (const auto& r : records) if (r.port == port && r.bytes == bytes) ++count;
        return count;
    }
    int Opens(int port) { std::lock_guard lock(mutex); return opens[port]; }
    int Closes(int port) { std::lock_guard lock(mutex); return closes[port]; }
};
class MockTransport final : public SerialTransport {
public:
    explicit MockTransport(std::shared_ptr<MockState> state) : state_(std::move(state)) {}
    bool Open(int port) override {
        std::lock_guard lock(state_->mutex); port_ = port; ++state_->opens[port];
        return !state_->failOpen;
    }
    bool Write(std::string_view bytes) override {
        std::unique_lock lock(state_->mutex);
        state_->records.push_back({port_, std::string(bytes)});
        if (state_->blockShot && bytes == EncodeGunCommand(GunType::OpenFire, GunCommand::Shot)) {
            state_->shotBlocked = true; state_->changed.notify_all();
            state_->changed.wait(lock, [&] { return !state_->blockShot; });
        }
        return !state_->failWrite;
    }
    void Close() override { std::lock_guard lock(state_->mutex); ++state_->closes[port_]; }
private:
    std::shared_ptr<MockState> state_;
    int port_ = 0;
};
SerialTransportFactory Factory(const std::shared_ptr<MockState>& state) {
    return [state] { return std::make_unique<MockTransport>(state); };
}

void Protocols() {
    for (auto type : {GunType::Off, GunType::Sinden})
        for (auto command : {GunCommand::Begin, GunCommand::Shot, GunCommand::Damage, GunCommand::Stop, GunCommand::End})
            Check(EncodeGunCommand(type, command).empty(), "non-serial adapters never produce serial bytes");
    for (auto type : {GunType::OpenFire, GunType::Gun4ir}) {
        Check(EncodeGunCommand(type, GunCommand::Begin) == "S6\nF0x0\nF1x0\n", "serial takeover leaves recoil/rumble off");
        Check(EncodeGunCommand(type, GunCommand::Shot) == "F0x2x0\n", "firmware recoil pulse cannot accumulate a backlog");
        Check(EncodeGunCommand(type, GunCommand::Damage) == "F1x2x0\n", "damage is a firmware rumble pulse");
        Check(EncodeGunCommand(type, GunCommand::End) == "F0x0\nF1x0\nE\n", "cleanup stops both motors and releases handoff");
    }
    Check(EncodeGunCommand(GunType::Blamcon, GunCommand::Begin) == "SM.0.1+SM.1.1+FB.0.0+FB.1.0+", "Blamcon takeover only controls recoil and rumble");
    Check(EncodeGunCommand(GunType::Blamcon, GunCommand::Shot) == "FB.0.1+", "Blamcon recoil uses its documented single pulse");
    Check(EncodeGunCommand(GunType::Blamcon, GunCommand::Damage) == "FB.1.1+", "Blamcon damage uses rumble pulse");
    Check(EncodeGunCommand(GunType::Blamcon, GunCommand::End) == "FB.0.0+FB.1.0+SM.0.0+SM.1.0+", "Blamcon cleanup terminates commands and restores handoff");
}

void DisabledAndInvalid() {
    auto state = std::make_shared<MockState>(); GunFeedback feedback(Factory(state));
    feedback.SetActive(true); feedback.Shot(0); feedback.Damage(1);
    Check(feedback.Status(0) == "Off", "feedback defaults off");
    Check(!feedback.Configure(-1, GunType::OpenFire, 1) && !feedback.Configure(2, GunType::Blamcon, 1), "invalid player slots rejected");
    Check(!feedback.Configure(0, GunType::OpenFire, 0) && !feedback.Configure(1, GunType::Gun4ir, 257), "invalid ports rejected");
    feedback.Shot(0); feedback.Damage(1); feedback.Close();
    Check(state->Opens(0) == 0 && state->Opens(257) == 0, "invalid/default configuration cannot open ports");
}

void Sessions() {
    auto state = std::make_shared<MockState>(); GunFeedback feedback(Factory(state));
    Check(feedback.Configure(0, GunType::OpenFire, 1) && feedback.Configure(1, GunType::Blamcon, 2), "separate player ports configured");
    feedback.Shot(0); feedback.Damage(1);
    Check(state->Opens(1) == 0 && state->Opens(2) == 0, "configuration alone does not acquire hardware");
    feedback.SetActive(true);
    Check(Wait([&] { return feedback.Status(0) == "Ready" && feedback.Status(1) == "Ready"; }), "both native sessions become ready");
    feedback.Shot(0); feedback.Damage(1);
    Check(Wait([&] { return state->Count(1, "F0x2x0\n") == 1 && state->Count(2, "FB.1.1+") == 1; }), "shot and damage dispatch to their respective local players");
    Check(state->Count(2, "FB.0.1+") == 0 && state->Count(1, "F1x2x0\n") == 0, "events do not leak to another player");
    for (int n = 0; n < 100; ++n) feedback.Shot(0);
    feedback.Configure(0, GunType::OpenFire, 1); feedback.SetActive(true);
    std::this_thread::sleep_for(20ms);
    Check(state->Count(1, "F0x2x0\n") == 1 && state->Opens(1) == 1, "rapid duplicate shots are dropped and unchanged configuration never reconnects");
    Check(feedback.Configure(1, GunType::Gun4ir, 3), "gun family and port can change");
    Check(Wait([&] { return feedback.Status(1) == "Ready" && state->Opens(3) == 1; }), "replacement port opens");
    Check(state->Count(2, EncodeGunCommand(GunType::Blamcon, GunCommand::End)) == 1 && state->Closes(2) == 1,
          "old device releases feedback before replacement");
    feedback.Damage(1);
    Check(Wait([&] { return state->Count(3, "F1x2x0\n") == 1; }), "GUN4IR receives native firmware pulse");
    Check(!feedback.Configure(1, GunType::Gun4ir, 1), "duplicate COM assignment rejected");
    Check(Wait([&] { return state->Closes(3) == 1; }), "invalid reassignment closes old session");
    Check(state->Opens(1) == 1, "duplicate assignment does not open a second handle");
    feedback.Configure(0, GunType::OpenFire, 4);
    Check(Wait([&] { return feedback.Status(0) == "Ready" && feedback.Status(1) == "Ready" && state->Opens(4) == 1 && state->Opens(1) == 2; }),
          "moving P1 automatically unblocks P2 without repeating P2 configuration");
    feedback.Stop();
    Check(Wait([&] { return feedback.Status(0) == "Inactive"; }), "stop releases the active session");
    const int shots = state->Count(4, "F0x2x0\n"); feedback.Shot(0); feedback.Damage(0);
    feedback.Close();
    Check(state->Count(4, "F0x2x0\n") == shots && state->Closes(4) == 1, "no queued inactive pulse survives shutdown");
    Check(state->Count(4, EncodeGunCommand(GunType::OpenFire, GunCommand::End)) == 1, "stop sends explicit off and end commands");
}

void FailureAndReactivation() {
    auto state = std::make_shared<MockState>(); state->failOpen = true;
    GunFeedback feedback(Factory(state)); feedback.Configure(0, GunType::OpenFire, 256); feedback.SetActive(true);
    Check(Wait([&] { return feedback.Status(0) == "COM256 unavailable"; }), "open failure is visible");
    for (int i = 0; i < 30; ++i) { feedback.Configure(0, GunType::OpenFire, 256); feedback.Shot(0); feedback.Update(); }
    Check(state->Opens(256) == 1, "failed connections do not retry every frame");
    { std::lock_guard lock(state->mutex); state->failOpen = false; }
    feedback.Stop(); feedback.SetActive(true);
    Check(Wait([&] { return feedback.Status(0) == "Ready"; }), "explicit reactivation retries connection");
    { std::lock_guard lock(state->mutex); state->failWrite = true; }
    feedback.Shot(0);
    Check(Wait([&] { return feedback.Status(0) == "Connection lost"; }), "disconnect/write failure closes device and reports status");
    Check(state->Opens(256) == 2, "disconnect never causes an automatic retry loop");
    feedback.Close();
}

void NonBlockingAndStaleQueue() {
    auto state = std::make_shared<MockState>(); GunFeedback feedback(Factory(state));
    feedback.Configure(0, GunType::OpenFire, 1); feedback.SetActive(true);
    Check(Wait([&] { return feedback.Status(0) == "Ready"; }), "mock session ready for blocked-I/O test");
    { std::lock_guard lock(state->mutex); state->blockShot = true; }
    feedback.Shot(0);
    Check(Wait([&] { std::lock_guard lock(state->mutex); return state->shotBlocked; }), "mock I/O is blocked only on worker");
    const auto before = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) feedback.Shot(0);
    feedback.Stop(); feedback.Configure(0, GunType::OpenFire, 2); feedback.SetActive(true);
    Check(std::chrono::steady_clock::now() - before < 100ms, "stop/configure/event APIs remain responsive during blocked device I/O");
    { std::lock_guard lock(state->mutex); state->blockShot = false; }
    state->changed.notify_all();
    Check(Wait([&] { return feedback.Status(0) == "Ready" && state->Opens(2) == 1; }), "new session opens after stale write completes");
    Check(state->Count(1, "F0x2x0\n") == 1 && state->Count(2, "F0x2x0\n") == 0, "queued old-session shots are discarded across stop/reconfigure");
    feedback.Close();
    Check(state->Closes(1) == 1 && state->Closes(2) == 1, "shutdown releases every opened transport");
}
} // namespace

int main() {
    Protocols(); DisabledAndInvalid(); Sessions(); FailureAndReactivation(); NonBlockingAndStaleQueue();
    std::printf("native gun feedback: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
