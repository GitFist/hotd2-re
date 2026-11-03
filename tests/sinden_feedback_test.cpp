#include "input/sinden_feedback.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace {
using namespace std::chrono_literals;
int checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
template<class Predicate> bool Wait(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 1500ms;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(2ms);
    }
    return true;
}

#ifdef _WIN32
// A real loopback TCP peer verifies the transport without starting the Sinden
// driver or touching attached hardware. It receives one command to EOF per
// connection, as required by this adapter's explicit framing strategy.
class FakeDriver {
public:
    explicit FakeDriver(int requestedPort = 0) {
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Check(listener_ != INVALID_SOCKET, "create fake driver socket");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<u_short>(requestedPort));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Check(bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "bind ephemeral loopback port");
        int size = sizeof(address);
        Check(getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size) == 0, "read fake driver port");
        port = ntohs(address.sin_port);
        Check(listen(listener_, 8) == 0, "listen for native feedback");
        worker_ = std::thread([this] {
            while (!stop_) {
                fd_set readable;
                FD_ZERO(&readable); FD_SET(listener_, &readable);
                timeval timeout{0, 10000};
                if (select(0, &readable, nullptr, nullptr, &timeout) <= 0) continue;
                const SOCKET client = accept(listener_, nullptr, nullptr);
                if (client == INVALID_SOCKET) continue;
                const int timeoutMs = 200;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                           reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
                std::string message;
                char buffer[64];
                int count;
                while ((count = recv(client, buffer, sizeof(buffer), 0)) > 0) message.append(buffer, count);
                closesocket(client);
                std::lock_guard lock(mutex_);
                commands_.push_back(message);
            }
        });
    }
    ~FakeDriver() { Close(); }
    void Close() {
        stop_ = true;
        if (worker_.joinable()) worker_.join();
        if (listener_ != INVALID_SOCKET) closesocket(listener_);
        listener_ = INVALID_SOCKET;
    }
    std::vector<std::string> Commands() const {
        std::lock_guard lock(mutex_);
        return commands_;
    }
    bool Has(const std::string& command) const {
        for (const auto& entry : Commands()) if (entry == command) return true;
        return false;
    }
    int port = 0;
private:
    SOCKET listener_ = INVALID_SOCKET;
    std::atomic<bool> stop_ = false;
    mutable std::mutex mutex_;
    std::vector<std::string> commands_;
    std::thread worker_;
};

void NativeTransport() {
    FakeDriver server;
    input::feedback::SindenFeedback feedback;
    feedback.Configure(0, 2, server.port);
    feedback.Shot(0);
    Check(feedback.Status(0) == "Waiting for game", "configuration alone leaves output inactive");
    feedback.SetActive(true);
    Check(Wait([&] { return server.Has("2K0"); }), "P1 correctly acquires explicitly assigned Lightgun B");
    Check(Wait([&] { return feedback.Status(0).starts_with("Ready"); }), "driver connection reports ready");
    Check(server.Commands() == std::vector<std::string>{"2K0"}, "inactive shot was discarded");
    feedback.Shot(0);
    Check(Wait([&] { return server.Has("2A"); }), "actual shot dispatches one finite pulse");
    feedback.Damage(0);
    feedback.Shot(-1);
    feedback.Shot(2);
    feedback.Stop();
    Check(Wait([&] { return server.Has("2S"); }), "stop restores the app's saved settings");
    Check(server.Commands() == std::vector<std::string>{"2K0", "2A", "2C", "2S"},
          "commands have separate connections and ordered stop/restore; damage has no false gunshot");
    feedback.Shot(0);
    feedback.Close();
    feedback.Close();
    Check(server.Commands().size() == 4, "no pulses or duplicate release after stop and close");
}

void AssignmentsAndLifecycle() {
    FakeDriver server;
    input::feedback::SindenFeedback feedback;
    feedback.Configure(0, 1, server.port);
    feedback.Configure(1, 1, server.port);
    Check(feedback.Status(1).find("already assigned") != std::string::npos, "duplicate physical gun rejected");
    feedback.Configure(1, 2, server.port);
    feedback.SetActive(true);
    Check(Wait([&] { return server.Commands().size() == 2; }), "both configured players acquire separate guns");
    Check(server.Commands() == std::vector<std::string>{"1K0", "2K0"}, "two-player acquisition uses exact gun IDs");
    feedback.Shot(1);
    Check(Wait([&] { return server.Has("2A"); }), "P2 shot reaches only its own gun");
    feedback.Configure(0, 0, server.port);
    Check(Wait([&] { return server.Has("1S"); }), "disabling a live assignment releases old gun");
    const auto began = std::chrono::steady_clock::now();
    feedback.Close();
    Check(std::chrono::steady_clock::now() - began < 1s, "worker shutdown is bounded");
    Check(Wait([&] { return server.Has("2C") && server.Has("2S"); }), "close releases remaining gun");
    for (const auto& message : server.Commands())
        Check(message.size() <= 3 && message != "1B" && message != "2B", "adapter never starts continuous recoil");
}

void MissingDriverAndValidation() {
    int port;
    {
        FakeDriver server;
        port = server.port;
    }
    input::feedback::SindenFeedback feedback;
    feedback.Configure(0, 3, port);
    Check(feedback.Status(0).find("Invalid") != std::string::npos, "reject invalid gun ID");
    feedback.Configure(0, 1, 65536);
    Check(feedback.Status(0).find("Invalid") != std::string::npos, "reject invalid TCP port");
    feedback.Configure(0, 1, port);
    const auto began = std::chrono::steady_clock::now();
    feedback.SetActive(true);
    for (int i = 0; i < 1000; ++i) feedback.Shot(0);
    Check(std::chrono::steady_clock::now() - began < 100ms, "game-thread calls do not wait for unavailable driver");
    Check(Wait([&] { return feedback.Status(0).find("unavailable") != std::string::npos; }), "missing server reports bounded failure");
    FakeDriver recovered(port);
    std::this_thread::sleep_for(200ms);
    Check(recovered.Commands().empty(), "failed server is not blindly retried or given old queued pulses");
    feedback.SetActive(false);
    feedback.SetActive(true);
    Check(Wait([&] { return recovered.Has("1K0"); }), "explicit activation change retries the server");
    Check(recovered.Commands() == std::vector<std::string>{"1K0"}, "retry discards stale shots");
    feedback.Close();
    Check(Wait([&] { return recovered.Has("1C") && recovered.Has("1S"); }), "recovered driver releases on close");
    Check(feedback.Status(-1) == "Invalid player", "invalid slot status is safe");
}

void SwappingAndConflictRecovery() {
    FakeDriver server;
    input::feedback::SindenFeedback feedback;
    feedback.Configure(0, 1, server.port);
    feedback.Configure(1, 2, server.port);
    feedback.SetActive(true);
    Check(Wait([&] { return feedback.Status(0).starts_with("Ready") && feedback.Status(1).starts_with("Ready"); }),
          "initial two-gun assignment is ready");
    feedback.Configure(0, 2, server.port);
    feedback.Configure(1, 1, server.port);
    Check(Wait([&] { return feedback.Status(0).starts_with("Ready") && feedback.Status(1).starts_with("Ready"); }),
          "swapping A/B automatically resolves the temporary duplicate");
    feedback.Shot(0);
    feedback.Shot(1);
    Check(Wait([&] { return server.Has("1A") && server.Has("2A"); }), "both swapped assignments can pulse");
    auto commands = server.Commands();
    for (const char gun : {'1', '2'}) {
        int lastRestore = -1, lastAcquire = -1;
        for (int i = 0; i < static_cast<int>(commands.size()); ++i) {
            if (commands[i] == std::string(1, gun) + "S") lastRestore = i;
            if (commands[i] == std::string(1, gun) + "K0") lastAcquire = i;
        }
        Check(lastRestore >= 0 && lastAcquire > lastRestore, "old gun ownership is restored before its new acquisition");
    }
    feedback.Configure(0, 1, server.port);
    Check(feedback.Status(1).find("already assigned") != std::string::npos, "live duplicate blocks P2 with P1 priority");
    feedback.Shot(1); // Must not survive the blocked assignment.
    feedback.Configure(0, 0, server.port);
    Check(Wait([&] { return feedback.Status(1).starts_with("Ready"); }), "disabling P1 automatically unblocks P2's retained request");
    feedback.Close();
    Check(Wait([&] {
        const auto all = server.Commands();
        return all.size() >= 2 && all[all.size() - 2] == "1C" && all.back() == "1S";
    }), "resolved assignment is released cleanly");
}
#endif
} // namespace

int main() {
#ifdef _WIN32
    WSADATA data{};
    Check(WSAStartup(MAKEWORD(2, 2), &data) == 0, "initialize fake-server sockets");
    NativeTransport();
    AssignmentsAndLifecycle();
    MissingDriverAndValidation();
    SwappingAndConflictRecovery();
    WSACleanup();
#else
    input::feedback::SindenFeedback feedback;
    Check(feedback.Status(0) == "Disabled", "native feedback defaults disabled");
    feedback.Close();
#endif
    std::printf("Sinden feedback: %d checks passed\n", checks);
}
