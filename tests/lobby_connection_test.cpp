// Real Session handshake and input exchange through the loopback fault harness.
// Run with lobby_connection_integration.py; no game assets or public services.
#include "net/netplay.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using port::net::FrameInput;
using port::net::Session;
using Clock = std::chrono::steady_clock;

static void Require(bool ok, const char* reason) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", reason); std::exit(1); }
}

static FrameInput Input(int player, int step) {
    FrameInput result;
    result.present = true;
    result.in.kind = 1;
    result.in.deviceMode = 6;
    result.in.x = static_cast<uint16_t>(100 + player * 300 + step * 7);
    result.in.y = static_cast<uint16_t>(80 + player * 180 + step * 3);
    result.in.fired = step % 2 == player;
    result.in.held = result.in.fired ? 4 : 0;
    result.in.pressed = result.in.held;
    return result;
}

static bool Same(const FrameInput& actual, const FrameInput& expected) {
    return actual.present == expected.present && actual.command == expected.command &&
           actual.in.kind == expected.in.kind && actual.in.deviceMode == expected.in.deviceMode &&
           actual.in.x == expected.in.x && actual.in.y == expected.in.y &&
           actual.in.fired == expected.in.fired && actual.in.offscreen == expected.in.offscreen &&
           actual.in.held == expected.in.held && actual.in.pressed == expected.in.pressed;
}

int main(int argc, char** argv) {
    Require(argc == 3, "usage: lobby_connection_test <loopback-service-port> <case>");
    const int port = std::stoi(argv[1]);
    Require(port > 0 && port < 65536, "invalid loopback service port");
    const std::string scenario = argv[2];
    constexpr uint32_t build = 987655;
    Session host, guest;
    std::string error;
    for (Session* session : {&host, &guest}) {
        session->SetBuild(build);
        Require(session->SetLobbyServer("127.0.0.1", static_cast<uint16_t>(port), &error), error.c_str());
    }
    Require(host.Host(0, &error), error.c_str());
    Require(guest.OpenJoiner(0, &error), error.c_str());
    port::net::SessionConfig config;
    config.credits = 3; config.seed = 119; config.firstSerial = 0x10009;
    config.flags = 7; config.delay = 3; config.startFrame = 901;
    for (int i = 0; i < 5; ++i) config.portVolumes[i] = i + 2;
    config.save.resize(60000);
    for (size_t i = 0; i < config.save.size(); ++i) config.save[i] = static_cast<uint8_t>(i * 17 + 3);
    host.SetConfig(config);
    host.SetDelayPolicy(3, false);
    bool guestSeeded = false, hostSeeded = false;
    const uint32_t first = config.startFrame + config.delay;
    auto pump = [&] {
        host.LobbyRefresh();
        host.Poll(); guest.Poll();
        // The host becomes Connected only after the welcomed guest sends input.
        if (guest.Connected() && !guestSeeded) { guest.PushLocal(first, Input(1, 0)); guestSeeded = true; }
        if (host.Connected() && !hostSeeded) { host.PushLocal(first, Input(0, 0)); hostSeeded = true; }
        host.Send(); guest.Send();
    };
    auto until = [&](auto ready, double seconds, const char* reason) {
        const auto deadline = Clock::now() + std::chrono::duration<double>(seconds);
        while (!ready() && Clock::now() < deadline) {
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!ready()) {
            std::fprintf(stderr, "%s: host connected=%d peer=%d path=%s; guest connected=%d peer=%d path=%s join=%d\n",
                         scenario.c_str(), host.Connected(), host.PeerKnown(), host.PathName(), guest.Connected(),
                         guest.PeerKnown(), guest.PathName(), static_cast<int>(guest.LobbyJoinResult()));
            Require(false, reason);
        }
    };
    host.LobbyCreate(port::net::kRoomRelay | port::net::kRoomRejoin);
    until([&] { return host.LobbyCreated(); }, 2, "room creation did not finish");
    guest.LobbyJoin(host.LobbyRoomId(), 0);
    const auto started = Clock::now();
    until([&] { return host.Connected() && guest.Connected(); }, 6, "both players did not finish the lobby handshake");
    Require(!host.BuildMismatch() && !guest.BuildMismatch(), "matching client builds became incompatible");
    Require(host.RelayAvailable() && guest.RelayAvailable(), "lobby did not retain both fallback credentials");
    const auto& received = guest.Config();
    Require(received.credits == config.credits && received.seed == config.seed && received.firstSerial == config.firstSerial &&
                received.flags == config.flags && received.delay == config.delay && received.startFrame == config.startFrame &&
                received.save == config.save, "WELCOME game/save payload was lost or corrupted");
    for (int i = 0; i < 5; ++i) Require(received.portVolumes[i] == config.portVolumes[i], "WELCOME volume payload changed");
    for (int step = 0; step < 8; ++step) {
        const uint32_t frame = first + step;
        host.PushLocal(frame, Input(0, step)); guest.PushLocal(frame, Input(1, step));
        FrameInput fromHost, fromGuest;
        until([&] { return host.RemoteInput(frame, &fromGuest) && guest.RemoteInput(frame, &fromHost); }, 3,
              "connected players stopped exchanging inputs");
        Require(Same(fromHost, Input(0, step)) && Same(fromGuest, Input(1, step)), "player inputs changed in transport");
        host.ReportHash(frame, 0x12345678u + frame); guest.ReportHash(frame, 0x12345678u + frame);
    }
    const auto drainUntil = Clock::now() + std::chrono::milliseconds(80);
    while (Clock::now() < drainUntil) { pump(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    uint32_t frame = 0, ours = 0, theirs = 0;
    Require(!host.Desynced(&frame, &ours, &theirs) && !guest.Desynced(&frame, &ours, &theirs), "matching input hashes diverged");
    if (scenario == "direct")
        Require(!host.UsingRelay() && !guest.UsingRelay(), "healthy direct connectivity unnecessarily selected relay");
    std::printf("PASS %s: both connected, complete 60000-byte save, 8 bidirectional inputs; host=%s guest=%s (%.2fs)\n",
                scenario.c_str(), host.PathName(), guest.PathName(), std::chrono::duration<double>(Clock::now() - started).count());
}
