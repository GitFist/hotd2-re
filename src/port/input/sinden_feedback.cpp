#include "sinden_feedback.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace input::feedback {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto kIoTimeout = std::chrono::milliseconds(80);
constexpr auto kPulseInterval = std::chrono::milliseconds(80);
constexpr auto kPulseExpiry = std::chrono::milliseconds(150);

bool ValidSlot(int slot) { return slot >= 0 && slot < 2; }

#ifdef _WIN32
// RecoilTcpServerReadme.txt in the official v2.08b driver defines ASCII
// commands, but only says commands must be separate messages. It defines no
// delimiter/length prefix. One command per connection with a send half-close
// gives each command its own stream; this framing strategy is an inference and
// still needs verification with physical Sinden hardware/driver.
bool Command(int port, int gun, const char* command) {
    const SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) return false;
    struct CloseSocket {
        SOCKET socket;
        ~CloseSocket() { closesocket(socket); }
    } close{socket};
    u_long nonblocking = 1;
    if (ioctlsocket(socket, FIONBIO, &nonblocking) != 0) return false;
    const int noDelay = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
               reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) return false;
        fd_set writable, failed;
        FD_ZERO(&writable); FD_SET(socket, &writable);
        FD_ZERO(&failed); FD_SET(socket, &failed);
        timeval timeout{0, static_cast<long>(kIoTimeout.count() * 1000)};
        if (select(0, nullptr, &writable, &failed, &timeout) <= 0 || FD_ISSET(socket, &failed)) return false;
        int error = 0, size = sizeof(error);
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) != 0 || error) return false;
    }
    const std::string message = char('0' + gun) + std::string(command);
    // Commands here are only 2-3 bytes. Do not block or queue additional writes
    // when the driver cannot accept the complete command immediately.
    if (send(socket, message.data(), static_cast<int>(message.size()), 0) != static_cast<int>(message.size())) return false;
    return shutdown(socket, SD_SEND) == 0;
}
#else
bool Command(int, int, const char*) { return false; }
#endif
} // namespace

struct SindenFeedback::Impl {
    struct Slot {
        int gun = 0, port = 13000;
        uint64_t version = 1;
        bool pending = false, blocked = false, invalid = false;
        Clock::time_point queued{};
        std::string status = "Disabled";
    };
    struct Owned {
        int gun = 0, port = 13000;
        uint64_t version = 0, attempted = 0;
        Clock::time_point nextPulse{};
    };
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::array<Slot, 2> slots;
    bool active = false, closing = false;
    std::thread worker;

    Impl() : worker([this] { Run(); }) {}

    void Status(int p, uint64_t version, const char* text) {
        std::lock_guard lock(mutex);
        if (slots[p].version == version) slots[p].status = text;
    }

    static void Release(Owned& owned) {
        if (!owned.gun) return;
        // Never use continuous recoil commands. C stops an existing repeat;
        // S restores the user's app settings, including trigger recoil.
        Command(owned.port, owned.gun, "C");
        Command(owned.port, owned.gun, "S");
        owned.gun = 0;
    }

    void Run() {
#ifdef _WIN32
        WSADATA data{};
        const bool socketsReady = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        const bool socketsReady = false;
#endif
        std::array<Owned, 2> owned;
        for (;;) {
            std::array<Slot, 2> desired;
            bool enabled, ending;
            {
                std::unique_lock lock(mutex);
                if (!active && !closing && !owned[0].gun && !owned[1].gun)
                    changed.wait(lock, [&] { return active || closing; });
                else
                    changed.wait_for(lock, std::chrono::milliseconds(10));
                desired = slots;
                enabled = active;
                ending = closing;
            }
            // Release every obsolete assignment before acquiring any new one.
            // Otherwise swapping A/B could restore one gun's trigger settings
            // after the other physical slot had already acquired it.
            for (int p = 0; p < 2; ++p) {
                auto& current = owned[p];
                const auto& target = desired[p];
                if (current.gun && (ending || !enabled || target.version != current.version)) Release(current);
            }
            for (int p = 0; p < 2; ++p) {
                auto& current = owned[p];
                const auto& target = desired[p];
                if (ending || !enabled || !target.gun || target.blocked) continue;
                if (!current.gun && current.attempted != target.version) {
                    current.attempted = target.version;
                    Status(p, target.version, "Connecting to Sinden driver");
                    if (socketsReady && Command(target.port, target.gun, "K0")) {
                        current.gun = target.gun;
                        current.port = target.port;
                        current.version = target.version;
                        current.nextPulse = Clock::now();
                        Status(p, target.version, "Ready - Sinden driver");
                    } else {
                        Status(p, target.version, "Sinden server unavailable - toggle feedback to retry");
                    }
                }
                if (!current.gun) continue;
                bool pulse = false;
                {
                    std::lock_guard lock(mutex);
                    auto& pending = slots[p];
                    if (active && !closing && !pending.blocked && pending.version == current.version && pending.pending) {
                        const auto now = Clock::now();
                        if (now - pending.queued > kPulseExpiry) pending.pending = false;
                        else if (now >= current.nextPulse) {
                            pulse = true;
                            pending.pending = false;
                            current.nextPulse = now + kPulseInterval;
                        }
                    }
                }
                if (pulse && !Command(current.port, current.gun, "A")) {
                    const auto version = current.version;
                    Release(current);
                    Status(p, version, "Sinden connection lost - toggle feedback to retry");
                }
            }
            if (ending) break;
        }
#ifdef _WIN32
        if (socketsReady) WSACleanup();
#endif
    }
};

SindenFeedback::SindenFeedback() : impl_(std::make_unique<Impl>()) {}
SindenFeedback::~SindenFeedback() { Close(); }

void SindenFeedback::Configure(int p, int gun, int port) {
    if (!ValidSlot(p)) return;
    std::lock_guard lock(impl_->mutex);
    if (impl_->closing) return;
    auto& slot = impl_->slots[p];
    const bool invalid = gun < 0 || gun > 2 || port < 1 || port > 65535;
    const int nextGun = invalid ? 0 : gun;
    const int nextPort = invalid ? 13000 : port;
    if (slot.gun != nextGun || slot.port != nextPort || slot.invalid != invalid) {
        slot.gun = nextGun;
        slot.port = nextPort;
        slot.invalid = invalid;
        slot.pending = false;
        ++slot.version;
        slot.status = invalid ? "Invalid Sinden gun or TCP port" : !nextGun ? "Disabled" :
            impl_->active ? "Connecting to Sinden driver" : "Waiting for game";
    }
    // Retain the requested assignment, so a conflict resolves automatically
    // when the other slot changes. P1 consistently wins duplicate assignments.
    for (int i = 0; i < 2; ++i) {
        auto& candidate = impl_->slots[i];
        const auto& first = impl_->slots[0];
        const bool blocked = i == 1 && candidate.gun && candidate.gun == first.gun && candidate.port == first.port;
        if (candidate.blocked != blocked) {
            candidate.blocked = blocked;
            candidate.pending = false;
            ++candidate.version;
            candidate.status = blocked ? "Sinden gun already assigned to Player 1" : !candidate.gun ? "Disabled" :
                impl_->active ? "Connecting to Sinden driver" : "Waiting for game";
        }
        if (candidate.invalid) candidate.status = "Invalid Sinden gun or TCP port";
        else if (candidate.blocked) candidate.status = "Sinden gun already assigned to Player 1";
    }
    impl_->changed.notify_one();
}

void SindenFeedback::SetActive(bool active) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->closing || impl_->active == active) return;
    impl_->active = active;
    for (auto& slot : impl_->slots) {
        ++slot.version;
        slot.pending = false;
        slot.status = slot.invalid ? "Invalid Sinden gun or TCP port" : slot.blocked ? "Sinden gun already assigned to Player 1" :
            !slot.gun ? "Disabled" : active ? "Connecting to Sinden driver" : "Waiting for game";
    }
    impl_->changed.notify_one();
}

void SindenFeedback::Shot(int p) {
    if (!ValidSlot(p)) return;
    std::lock_guard lock(impl_->mutex);
    auto& slot = impl_->slots[p];
    if (impl_->closing || !impl_->active || !slot.gun || slot.blocked || slot.pending) return;
    slot.pending = true;
    slot.queued = Clock::now();
    impl_->changed.notify_one();
}

void SindenFeedback::Damage(int) {}
void SindenFeedback::Stop() { SetActive(false); }
void SindenFeedback::Close() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->closing = true;
        for (auto& slot : impl_->slots) slot.pending = false;
    }
    impl_->changed.notify_one();
    if (impl_->worker.joinable()) impl_->worker.join();
}
std::string SindenFeedback::Status(int p) const {
    if (!ValidSlot(p)) return "Invalid player";
    std::lock_guard lock(impl_->mutex);
    return impl_->slots[p].status;
}

} // namespace input::feedback
