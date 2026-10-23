// Actual loopback handshake: added volume settings must not shift or corrupt the save payload.
#include "net/netplay.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

static void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
int main() {
#ifdef _WIN32
    _putenv_s("HOTD2_NET_LOCALCODE", "1");
#else
    setenv("HOTD2_NET_LOCALCODE", "1", 1);
#endif
    port::net::Session host, joiner;
    host.SetBuild(2026102102); joiner.SetBuild(2026102102);
    std::string error;
    Require(host.Host(0, &error), error.c_str());
    port::net::SessionConfig sent;
    sent.credits = 3; sent.seed = 119; sent.firstSerial = 0x10009;
    const int volumes[] = {3, 7, 0, 6, 2};
    for (int i = 0; i < 5; ++i) sent.portVolumes[i] = volumes[i];
    sent.flags = 7; sent.delay = 4; sent.startFrame = 901;
    sent.save = {0, 17, 255, 2, 31, 8, 0};
    host.SetConfig(sent); host.SetDelayPolicy(4, false);
    Require(host.QueryPublicEndpoint(&error), error.c_str());
    Require(joiner.OpenJoiner(0, &error), error.c_str());
    Require(joiner.SetPeerCode(host.LocalCode(), &error), error.c_str());
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!joiner.Connected() && std::chrono::steady_clock::now() < end) {
        host.Poll(); joiner.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Require(joiner.Connected(), "loopback handshake connects");
    const auto& received = joiner.Config();
    for (int i = 0; i < 5; ++i) Require(received.portVolumes[i] == volumes[i], "all five volumes survive handshake");
    Require(received.credits == sent.credits && received.seed == sent.seed && received.firstSerial == sent.firstSerial,
            "game configuration preserved");
    Require(received.flags == sent.flags && received.delay == sent.delay && received.startFrame == sent.startFrame &&
            received.save == sent.save, "trailing fields and save bytes preserved");
    std::puts("PASS loopback audio settings: all five volumes and complete game/save configuration");
}
