#include "net/netplay.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#pragma comment(lib, "ws2_32.lib")
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using SOCKET = int;
#endif

namespace port::net {

namespace {

constexpr uint8_t kHello = 1, kWelcome = 2, kInput = 3, kBye = 4, kPong = 5, kKeepalive = 6, kVersion = 3;
constexpr double kRelayHelloAfter = 1.0;   // s of direct hellos before the relay hellos start
constexpr double kPathSilence = 2.0;       // s without a packet on the current path: the other path takes over
constexpr double kKeepaliveEvery = 5.0;    // s between relay keepalives while playing directly
constexpr double kWelcomeWait = 1.5;       // s the host waits for a pong before sending the welcome without one
constexpr size_t kRecord = 14;
constexpr int kMaxRecords = 48;  // ~0.8 s of unacknowledged inputs per packet (resent until acknowledged)
const char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

void Put8(std::vector<uint8_t>& p, uint32_t v) { p.push_back(static_cast<uint8_t>(v)); }
void Put16(std::vector<uint8_t>& p, uint32_t v) {
    Put8(p, v);
    Put8(p, v >> 8);
}
void Put32(std::vector<uint8_t>& p, uint32_t v) {
    Put16(p, v);
    Put16(p, v >> 16);
}
uint32_t Get16(const uint8_t* d) { return d[0] | (d[1] << 8); }
uint32_t Get32(const uint8_t* d) { return Get16(d) | (Get16(d + 2) << 16); }

void PutRecord(std::vector<uint8_t>& p, const FrameInput& f) {
    Put8(p, f.present ? 1 : 0);
    Put8(p, f.command);
    Put8(p, static_cast<uint8_t>(static_cast<int8_t>(f.in.kind)));
    Put8(p, static_cast<uint8_t>(static_cast<int8_t>(f.in.deviceMode)));
    Put16(p, static_cast<uint16_t>(static_cast<int16_t>(f.in.x)));
    Put16(p, static_cast<uint16_t>(static_cast<int16_t>(f.in.y)));
    Put8(p, f.in.fired ? 1 : 0);
    Put8(p, f.in.offscreen ? 1 : 0);
    Put16(p, f.in.held & 0xFFFF);
    Put16(p, f.in.pressed & 0xFFFF);
}

FrameInput GetRecord(const uint8_t* d) {
    FrameInput f;
    f.present = d[0] != 0;
    f.command = d[1];
    f.in.kind = static_cast<int8_t>(d[2]);
    f.in.deviceMode = static_cast<int8_t>(d[3]);
    // 0xFFFF is the world's off-screen sentinel, compared before its int16 gun conversion.
    f.in.x = Get16(d + 4) == 0xFFFF ? 0xFFFF : static_cast<int16_t>(Get16(d + 4));
    f.in.y = Get16(d + 6) == 0xFFFF ? 0xFFFF : static_cast<int16_t>(Get16(d + 6));
    f.in.fired = d[8] != 0;
    f.in.offscreen = d[9] != 0;
    f.in.held = Get16(d + 10);
    f.in.pressed = Get16(d + 12);
    return f;
}

std::vector<uint8_t> Header(uint8_t type) {
    std::vector<uint8_t> p = { 'H', '2', 'N', 'P', type, kVersion };
    return p;
}

#ifdef _WIN32
struct WsaInit {
    WsaInit() {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    }
    ~WsaInit() { WSACleanup(); }
};
void CloseSock(uintptr_t s) { closesocket(static_cast<SOCKET>(s)); }
bool SetNonBlocking(uintptr_t s) {
    u_long on = 1;
    return ioctlsocket(static_cast<SOCKET>(s), FIONBIO, &on) == 0;
}
#else
struct WsaInit {};
void CloseSock(uintptr_t s) { close(static_cast<int>(s)); }
bool SetNonBlocking(uintptr_t s) { return fcntl(static_cast<int>(s), F_SETFL, fcntl(static_cast<int>(s), F_GETFL, 0) | O_NONBLOCK) == 0; }
#endif

bool LogOn() {
    static const bool on = getenv("HOTD2_NET_LOG") != nullptr;
    return on;
}

double LogSeconds() {
    static const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

void SleepMs(int ms) {
#ifdef _WIN32
    Sleep(static_cast<DWORD>(ms));
#else
    usleep(static_cast<useconds_t>(ms) * 1000);
#endif
}

}  // namespace

std::string EncodeCode(const uint8_t ip[4], uint16_t port) {
    uint8_t b[7] = { ip[0], ip[1], ip[2], ip[3], static_cast<uint8_t>(port >> 8), static_cast<uint8_t>(port), 0 };
    for (int i = 0; i < 6; ++i) b[6] = static_cast<uint8_t>(b[6] * 31 + b[i]);
    uint64_t v = 0;
    for (int i = 0; i < 7; ++i) v = (v << 8) | b[i];
    v <<= 4;  // 56 bits in 12 x 5 = 60
    std::string s;
    for (int i = 11; i >= 0; --i) s.push_back(kAlphabet[(v >> (i * 5)) & 31]);
    return s;
}

bool DecodeCode(const std::string& code, uint8_t ip[4], uint16_t* port) {
    uint64_t v = 0;
    int n = 0;
    for (char c : code) {
        if (c == ' ' || c == '-') continue;
        const char u = static_cast<char>(toupper(static_cast<unsigned char>(c)));
        const char* p = strchr(kAlphabet, u);
        if (!p || !u) return false;
        v = (v << 5) | static_cast<uint64_t>(p - kAlphabet);
        if (++n > 12) return false;
    }
    if (n != 12) return false;
    v >>= 4;
    uint8_t b[7];
    for (int i = 6; i >= 0; --i) {
        b[i] = static_cast<uint8_t>(v & 0xFF);
        v >>= 8;
    }
    uint8_t check = 0;
    for (int i = 0; i < 6; ++i) check = static_cast<uint8_t>(check * 31 + b[i]);
    if (check != b[6]) return false;
    memcpy(ip, b, 4);
    *port = static_cast<uint16_t>((b[4] << 8) | b[5]);
    return true;
}

Session::Session() {
    static WsaInit wsa;
    (void)wsa;
    lastHello_ = lastFromRemote_ = lastSend_ = Clock::now();
}

Session::~Session() {
    Close();
    for (const auto& cancellation : lobbyCancellations_) CloseSock(cancellation.socket);
}

bool Session::Open(uint16_t port, std::string* error) {
    const uintptr_t s = static_cast<uintptr_t>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (s == kNoSocket) {
        if (error) *error = "cannot create a UDP socket";
        return false;
    }
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(static_cast<SOCKET>(s), reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        if (error) *error = "cannot bind UDP port " + std::to_string(port);
        CloseSock(s);
        return false;
    }
    if (!SetNonBlocking(s)) {
        if (error) *error = "cannot make the socket non-blocking";
        CloseSock(s);
        return false;
    }
#ifdef _WIN32
    // A datagram to a port nobody listens on yet (the other side's socket is not open) comes back as ICMP port
    // unreachable, which Windows reports as WSAECONNRESET on this socket's recvfrom; off, or the lobby's early hellos
    // would hide the packets behind them.
    DWORD off = 0, bytes = 0;
    if (WSAIoctl(static_cast<SOCKET>(s), SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &bytes, nullptr, nullptr) != 0 && LogOn())
        fprintf(stderr, "net: SIO_UDP_CONNRESET failed, error %d\n", WSAGetLastError());
#endif
    sock_ = s;
    return true;
}

bool Session::Host(uint16_t port, std::string* error) {
    host_ = true;
    return Open(port, error);
}

bool Session::OpenJoiner(uint16_t port, std::string* error) {
    host_ = false;
    return Open(port, error);
}

bool Session::Join(const std::string& address, uint16_t port, std::string* error) {
    if (!OpenJoiner(0, error)) return false;
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(address.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        if (error) *error = "cannot resolve " + address;
        return false;
    }
    memcpy(peer_, res->ai_addr, std::min<size_t>(res->ai_addrlen, sizeof(peer_)));
    peerLen_ = static_cast<int>(res->ai_addrlen);
    freeaddrinfo(res);
    peerKnown_ = true;
    SendHello();
    return true;
}

bool Session::SetPeerCode(const std::string& code, std::string* error) {
    uint8_t ip[4];
    uint16_t port;
    if (!DecodeCode(code, ip, &port)) {
        if (error) *error = "not a valid code";
        return false;
    }
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    memcpy(&a.sin_addr, ip, 4);
    a.sin_port = htons(port);
    memcpy(peer_, &a, sizeof(a));
    peerLen_ = sizeof(a);
    peerKnown_ = true;
    SendHello();
    return true;
}

// One STUN binding request (RFC 5389) to server:port through our socket; the mapped address from the reply.
bool Session::StunOnce(const char* server, uint16_t port, uint8_t ip[4], uint16_t* mapped) {
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(server, std::to_string(port).c_str(), &hints, &res) != 0 || !res) return false;
    uint8_t req[20] = { 0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42 };
    std::random_device rd;
    for (int i = 8; i < 20; ++i) req[i] = static_cast<uint8_t>(rd());
    sendto(static_cast<SOCKET>(sock_), reinterpret_cast<const char*>(req), sizeof(req), 0, res->ai_addr, static_cast<int>(res->ai_addrlen));
    freeaddrinfo(res);
    for (int waited = 0; waited < 600; waited += 10) {
        uint8_t buf[1024];
        const int n = recvfrom(static_cast<SOCKET>(sock_), reinterpret_cast<char*>(buf), sizeof(buf), 0, nullptr, nullptr);
        if (n >= 20 && buf[0] == 0x01 && buf[1] == 0x01 && memcmp(buf + 8, req + 8, 12) == 0) {
            const size_t len = (buf[2] << 8) | buf[3];
            for (size_t i = 20; i + 4 <= std::min<size_t>(20 + len, static_cast<size_t>(n));) {
                const uint32_t at = (buf[i] << 8) | buf[i + 1], al = (buf[i + 2] << 8) | buf[i + 3];
                if (at == 0x0020 && al >= 8 && buf[i + 5] == 0x01) {  // XOR-MAPPED-ADDRESS, IPv4
                    *mapped = static_cast<uint16_t>(((buf[i + 6] << 8) | buf[i + 7]) ^ 0x2112);
                    const uint8_t cookie[4] = { 0x21, 0x12, 0xA4, 0x42 };
                    for (int k = 0; k < 4; ++k) ip[k] = buf[i + 8 + k] ^ cookie[k];
                    return true;
                }
                i += 4 + ((al + 3) & ~3u);
            }
            return false;
        }
        SleepMs(10);
    }
    return false;
}

bool Session::QueryPublicEndpoint(std::string* error) {
    if (sock_ == kNoSocket) {
        if (error) *error = "no socket";
        return false;
    }
    sockaddr_in local = {};
    socklen_t len = sizeof(local);
    getsockname(static_cast<SOCKET>(sock_), reinterpret_cast<sockaddr*>(&local), &len);
    if (getenv("HOTD2_NET_LOCALCODE")) {  // tests on one machine: this host, the bound port
        const uint8_t lo[4] = { 127, 0, 0, 1 };
        localCode_ = EncodeCode(lo, ntohs(local.sin_port));
        return true;
    }
    static const char* const kServers[2] = { "stun.l.google.com", "stun.cloudflare.com" };
    static const uint16_t kPorts[2] = { 19302, 3478 };
    for (int attempt = 0; attempt < 3; ++attempt)
        for (int s = 0; s < 2; ++s) {
            uint8_t ip[4];
            uint16_t port;
            if (StunOnce(kServers[s], kPorts[s], ip, &port)) {
                localCode_ = EncodeCode(ip, port);
                return true;
            }
        }
    if (error) *error = "no answer from the STUN servers (no internet?)";
    return false;
}

void Session::SendDirect(const std::vector<uint8_t>& p) {
    if (sock_ == kNoSocket || !peerKnown_) return;
    if (cutDirectAfter_ >= 0.0 && connected_ && std::chrono::duration<double>(Clock::now() - connectedAt_).count() > cutDirectAfter_) return;  // (tests)
    auto one = [&](const void* addr, int len) {
        const int r = sendto(static_cast<SOCKET>(sock_), reinterpret_cast<const char*>(p.data()), static_cast<int>(p.size()), 0,
                             static_cast<const sockaddr*>(addr), len);
        if (r < 0 && LogOn()) {
#ifdef _WIN32
            fprintf(stderr, "net: sendto failed, error %d\n", WSAGetLastError());
#else
            fprintf(stderr, "net: sendto failed\n");
#endif
        }
        ++stats_.packetsOut;
    };
    if (!connected_ && path_ == Path::None && candidates_.size() > 1) {  // every candidate until one answers
        for (const auto& c : candidates_) one(c.data(), sizeof(sockaddr_in));
    } else {
        one(peer_, peerLen_);
    }
}

void Session::SendRelayed(const std::vector<uint8_t>& p) {
    if (sock_ == kNoSocket || !RelayAvailable() || !lobbyKnown_) return;
    std::vector<uint8_t> envelope = { 'H', '2', 'L', 'B', 1, 6 };
    Put32(envelope, relayRoom_);
    envelope.insert(envelope.end(), relayToken_.begin(), relayToken_.end());
    envelope.insert(envelope.end(), p.begin(), p.end());
    sendto(static_cast<SOCKET>(sock_), reinterpret_cast<const char*>(envelope.data()), static_cast<int>(envelope.size()), 0,
           reinterpret_cast<const sockaddr*>(lobbyAddr_), sizeof(sockaddr_in));
    ++stats_.packetsOut;
}

// The current path; before one is fixed, both (the direct one at once, the relay from kRelayHelloAfter on); while
// the current one is silent (probing), both.
void Session::SendRaw(const std::vector<uint8_t>& p) {
    if (sock_ == kNoSocket) return;
    if (probing_) {
        SendDirect(p);
        SendRelayed(p);
        return;
    }
    // A HELLO/PONG may get through a one-way route even when the welcome (which includes the save) or inputs
    // cannot. Keep the fallback open until gameplay packets confirm it in both directions.
    if ((!connected_ || !peerInputSeen_) && path_ != Path::Relay) {
        SendDirect(p);
        if (RelayAvailable() && std::chrono::duration<double>(Clock::now() - helloStart_).count() >= kRelayHelloAfter)
            SendRelayed(p);
        return;
    }
    switch (path_) {
    case Path::Direct: SendDirect(p); break;
    case Path::Relay: SendRelayed(p); break;
    default:
        if (peerKnown_) SendDirect(p);
        if (RelayAvailable() && std::chrono::duration<double>(Clock::now() - helloStart_).count() >= kRelayHelloAfter) SendRelayed(p);
        break;
    }
}

uint32_t Session::NowMs() const { return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count()); }

void Session::SendPong(uint32_t stamp, bool viaRelay) {
    std::vector<uint8_t> p = Header(kPong);
    Put32(p, stamp);
    if (viaRelay) SendRelayed(p);
    else SendDirect(p);
}

// The host's input delay from the round trip: half of it in frames, rounded up, plus one for jitter; at least
// minDelay_. Without a measurement (no pong within kWelcomeWait) minDelay_ + 1.
void Session::DecideDelay() {
    if (!delayAuto_) return;
    int frames = minDelay_ + 1;
    if (rttMs_ >= 0.0) frames = static_cast<int>(std::ceil(rttMs_ / 2.0 / (1000.0 / 60.0))) + 1;
    config_.delay = static_cast<uint8_t>(std::clamp(frames, minDelay_, 10));
}

// ---- lobby server ----------------------------------------------------------------------------------------------

bool Session::SetLobbyServer(const std::string& host, uint16_t port, std::string* error) {
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        if (error) *error = "cannot resolve the lobby server " + host;
        lobbyKnown_ = false;
        return false;
    }
    memcpy(lobbyAddr_, res->ai_addr, std::min<size_t>(res->ai_addrlen, sizeof(lobbyAddr_)));
    freeaddrinfo(res);
    lobbyKnown_ = true;
    lobbyLastAnswer_ = Clock::now();
    return true;
}

void Session::SendToLobby(const std::vector<uint8_t>& p) {
    if (sock_ == kNoSocket || !lobbyKnown_) return;
    sendto(static_cast<SOCKET>(sock_), reinterpret_cast<const char*>(p.data()), static_cast<int>(p.size()), 0, reinterpret_cast<const sockaddr*>(lobbyAddr_),
           sizeof(sockaddr_in));
    lobbyLastSend_ = Clock::now();
}

// This machine's address towards the lobby server (the route's interface) and the socket's own port: the local
// candidate, for a player on the same LAN (whose router may not hairpin the public one).
void Session::LocalEndpoint(uint8_t ip[4], uint16_t* port) const {
    memset(ip, 0, 4);
    *port = 0;
    if (sock_ == kNoSocket) return;
    sockaddr_in local = {};
    socklen_t len = sizeof(local);
    getsockname(static_cast<SOCKET>(sock_), reinterpret_cast<sockaddr*>(&local), &len);
    *port = ntohs(local.sin_port);
    const SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe == INVALID_SOCKET) return;
    if (lobbyKnown_ && connect(probe, reinterpret_cast<const sockaddr*>(lobbyAddr_), sizeof(sockaddr_in)) == 0) {
        sockaddr_in a = {};
        socklen_t alen = sizeof(a);
        if (getsockname(probe, reinterpret_cast<sockaddr*>(&a), &alen) == 0) memcpy(ip, &a.sin_addr, 4);
    }
    closesocket(probe);
}

static std::vector<uint8_t> LobbyHeader(uint8_t type) {
    std::vector<uint8_t> p = { 'H', '2', 'L', 'B', 1, type };
    return p;
}

void Session::LobbyCreate(uint8_t flags, int difficulty) {
    if (difficulty != -1)
        flags = static_cast<uint8_t>((flags & ~kRoomDifficultyMask) | RoomDifficultyFlags(difficulty));
    flags |= kRoomRelay | kRoomRejoin;
    lobbyFlags_ = flags;
    lobbyHosting_ = true;
    lobbyCreating_ = true;
    lobbyLeavePending_ = false;
    lobbyRoom_ = lobbyCode_ = 0;
    std::vector<uint8_t> p = LobbyHeader(1);
    Put32(p, build_);
    Put8(p, flags);
    uint8_t ip[4];
    uint16_t port;
    LocalEndpoint(ip, &port);
    p.insert(p.end(), ip, ip + 4);
    Put16(p, port);
    SendToLobby(p);
}

void Session::LobbyRefresh() {
    if (!lobbyHosting_ || sock_ == kNoSocket) return;
    const double since = std::chrono::duration<double>(Clock::now() - lobbyLastSend_).count();
    if (lobbyCreating_) {
        if (since > 1.0) LobbyCreate(lobbyFlags_);  // the CREATE again (the server returns the same room)
        return;
    }
    if (since < 2.0) return;
    std::vector<uint8_t> p = LobbyHeader(2);
    Put32(p, lobbyRoom_);
    Put32(p, lobbyCode_);
    SendToLobby(p);
}

void Session::LobbyLeave() {
    if (lobbyHosting_) {
        lobbyLeavePending_ = true;
        lobbyLeaveRoom_ = lobbyRoom_;
        lobbyLeaveCode_ = lobbyCode_;
    }
    if (lobbyHosting_ && lobbyRoom_) {
        std::vector<uint8_t> p = LobbyHeader(5);
        Put32(p, lobbyRoom_);
        Put32(p, lobbyCode_);
        for (int attempt = 0; attempt < 3; ++attempt) SendToLobby(p);
    }
    lobbyHosting_ = lobbyCreating_ = false;
    lobbyRoom_ = lobbyCode_ = 0;
}

bool Session::LobbyCancellationPending() const {
    return std::any_of(lobbyCancellations_.begin(), lobbyCancellations_.end(),
                       [](const auto& c) { return c.room == 0; });
}

bool Session::IsCancelledLobbyRoom(uint32_t room) const {
    return room && std::any_of(lobbyCancellations_.begin(), lobbyCancellations_.end(),
                              [room](const auto& c) { return c.room == room; });
}

void Session::PollLobbyCleanup() {
    const auto now = Clock::now();
    for (auto it = lobbyCancellations_.begin(); it != lobbyCancellations_.end();) {
        if (std::chrono::duration<double>(now - it->started).count() >= 10.0) {
            CloseSock(it->socket);
            it = lobbyCancellations_.erase(it);
            continue;
        }
        // Keep the original endpoint alive just long enough to receive a late
        // CREATED. A new attempt uses another socket, so old replies cannot
        // attach their room to the new attempt or restart its keepalives.
        for (int packets = 0; packets < 64; ++packets) {
            uint8_t data[2048];
            sockaddr_storage from{};
            socklen_t length = sizeof(from);
            const int size = recvfrom(static_cast<SOCKET>(it->socket), reinterpret_cast<char*>(data), sizeof(data), 0,
                                      reinterpret_cast<sockaddr*>(&from), &length);
            if (size < 0) {
#ifdef _WIN32
                if (WSAGetLastError() == WSAECONNRESET) continue;
#endif
                break;
            }
            if (size >= 14 && length >= sizeof(sockaddr_in) &&
                memcmp(&from, it->server.data(), sizeof(sockaddr_in)) == 0 &&
                memcmp(data, "H2LB\1", 5) == 0 && data[5] == 101) {
                const uint32_t room = Get32(data + 6), code = Get32(data + 10);
                if (room && (!it->room || it->room == room)) {
                    it->room = room;
                    it->code = code;
                    it->lastSend = Clock::time_point{};
                }
            }
        }
        if (it->room && (it->lastSend == Clock::time_point{} ||
                        std::chrono::duration<double>(now - it->lastSend).count() >= .5)) {
            auto packet = LobbyHeader(5);
            Put32(packet, it->room);
            Put32(packet, it->code);
            sendto(static_cast<SOCKET>(it->socket), reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
                   reinterpret_cast<const sockaddr*>(it->server.data()), sizeof(sockaddr_in));
            it->lastSend = now;
        }
        ++it;
    }
}

void Session::LobbyList() {
    std::vector<uint8_t> p = LobbyHeader(3);
    Put32(p, build_);
    SendToLobby(p);
}

void Session::LobbyJoin(uint32_t room, uint32_t code) {
    if (!lobbyJoining_) lobbyJoinStarted_ = Clock::now();
    lobbyJoining_ = true;
    lobbyJoinRoom_ = room;
    lobbyJoinCode_ = code;
    lobbyJoinResult_ = JoinResult::Pending;
    std::vector<uint8_t> p = LobbyHeader(4);
    Put32(p, room);
    Put32(p, code);
    Put32(p, build_);
    uint8_t ip[4];
    uint16_t port;
    LocalEndpoint(ip, &port);
    p.insert(p.end(), ip, ip + 4);
    Put16(p, port);
    SendToLobby(p);
}

double Session::LobbySilence() const { return std::chrono::duration<double>(Clock::now() - lobbyLastAnswer_).count(); }

void Session::SetRelay(uint32_t room, const uint8_t* token) {
    if (!room || !lobbyKnown_) return;
    if (getenv("HOTD2_NET_NORELAY")) return;  // tests: the direct path only
    relayRoom_ = room;
    memcpy(relayToken_.data(), token, relayToken_.size());
    if (!peerKnown_) helloStart_ = Clock::now();  // (no direct endpoints: the relay hellos still wait their second)
}

void Session::SetPeerCandidates(const uint8_t pubIp[4], uint16_t pubPort, const uint8_t localIp[4], uint16_t localPort) {
    candidates_.clear();
    auto add = [&](const uint8_t ip[4], uint16_t port) {
        if (port == 0 || (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0)) return;
        sockaddr_in a = {};
        a.sin_family = AF_INET;
        memcpy(&a.sin_addr, ip, 4);
        a.sin_port = htons(port);
        std::array<uint8_t, 16> c = {};
        memcpy(c.data(), &a, sizeof(a));
        for (const auto& k : candidates_)
            if (k == c) return;
        candidates_.push_back(c);
    };
    if (!getenv("HOTD2_NET_NODIRECT")) {  // (tests: the relay only)
        add(pubIp, pubPort);
        add(localIp, localPort);
    }
    if (candidates_.empty()) return;
    memcpy(peer_, candidates_[0].data(), 16);  // the first one until a packet arrives from either
    peerLen_ = sizeof(sockaddr_in);
    peerKnown_ = true;
    helloStart_ = Clock::now();
    SendHello();
}

void Session::OnLobbyPacket(const uint8_t* d, size_t n) {
    if (n < 6 || d[4] != 1) return;
    lobbyLastAnswer_ = Clock::now();
    const uint8_t type = d[5];
    d += 6;
    n -= 6;
    switch (type) {
    case 101:  // CREATED
        if (n < 8 || !lobbyHosting_) break;
        lobbyRoom_ = Get32(d);
        lobbyCode_ = Get32(d + 4);
        lobbyCreating_ = false;
        if (LogOn()) fprintf(stderr, "net %.1f: lobby room %u code %u\n", LogSeconds(), lobbyRoom_, lobbyCode_);
        break;
    case 102: {  // ROOMS
        if (n < 1) break;
        const int count = d[0];
        if (1 + static_cast<size_t>(count) * 9 > n) break;
        lobbyRooms_.clear();
        for (int i = 0; i < count; ++i) {
            LobbyRoom r;
            r.id = Get32(d + 1 + i * 9);
            r.flags = d[5 + i * 9];
            r.build = Get32(d + 6 + i * 9);
            lobbyRooms_.push_back(r);
        }
        lobbyRoomsKnown_ = true;
        break;
    }
    case 103: {  // JOINED
        if (n < 5 + 12 || !lobbyJoining_) break;
        const uint8_t result = d[4];
        if (result > static_cast<uint8_t>(JoinResult::OtherVersion) ||
            (lobbyJoinRoom_ && Get32(d) != lobbyJoinRoom_ && result != static_cast<uint8_t>(JoinResult::NoRoom))) break;
        lobbyJoinResult_ = static_cast<JoinResult>(result);
        lobbyJoining_ = false;
        if (result == 0 && !peerKnown_) {
            // the host's endpoints (direct path) and, from a relay-capable server, the relay token (fallback path)
            uint8_t pubIp[4], localIp[4];
            memcpy(pubIp, d + 5, 4);
            memcpy(localIp, d + 11, 4);
            host_ = false;
            if (n >= 33) SetRelay(Get32(d), d + 17);
            rejoinCapable_ = n >= 34 && (d[33] & kRoomRejoin);
            SetPeerCandidates(pubIp, static_cast<uint16_t>(Get16(d + 9)), localIp, static_cast<uint16_t>(Get16(d + 15)));
            if (!peerKnown_ && RelayAvailable()) {  // (a server that gives no endpoints: relay only)
                memcpy(peer_, lobbyAddr_, sizeof(lobbyAddr_));
                peerLen_ = sizeof(sockaddr_in);
                peerKnown_ = true;
                path_ = Path::Relay;
                SendHello();
            }
        }
        if (LogOn()) fprintf(stderr, "net %.1f: lobby join result %d\n", LogSeconds(), result);
        break;
    }
    case 104: {  // PEER (the joiner's endpoints and, for a relay room, the room and the host's token), to the host
        if (n < 12 || !lobbyHosting_ || connected_) break;
        if (!lobbyCreating_ && n >= 32 && Get32(d + 12) != lobbyRoom_) break;
        if (!peerKnown_) {
            uint8_t pubIp[4], localIp[4];
            memcpy(pubIp, d, 4);
            memcpy(localIp, d + 6, 4);
            host_ = true;
            if (n >= 32 && (lobbyFlags_ & kRoomRelay)) SetRelay(Get32(d + 12), d + 16);
            rejoinCapable_ = n >= 33 && (d[32] & kRoomRejoin);
            SetPeerCandidates(pubIp, static_cast<uint16_t>(Get16(d + 4)), localIp, static_cast<uint16_t>(Get16(d + 10)));
            if (!peerKnown_ && RelayAvailable()) {
                memcpy(peer_, lobbyAddr_, sizeof(lobbyAddr_));
                peerLen_ = sizeof(sockaddr_in);
                peerKnown_ = true;
                path_ = Path::Relay;
                SendHello();
            }
            if (LogOn()) fprintf(stderr, "net %.1f: lobby peer arrived (direct candidates %d, relay %d)\n", LogSeconds(), static_cast<int>(candidates_.size()), RelayAvailable() ? 1 : 0);
        }
        break;
    }
    case 106:  // RELAYED: only the server may deliver an authenticated packet for this session
        if (n >= 26 && RelayAvailable() && Get32(d) == relayRoom_ && memcmp(d + 4, relayToken_.data(), relayToken_.size()) == 0)
            OnPeerPacket(d + 20, n - 20, lobbyAddr_, sizeof(sockaddr_in), true);
        break;
    case 107:  // ticket authentication / endpoint rebinding (from our configured server only)
        if (!resumingLobby_ || n < 6 || Get32(d) != relayRoom_) break;
        if (d[4] != 0) {
            FailRecovery(d[4] == 4 ? "THE MATCH USES ANOTHER VERSION" : "THAT MATCH HAS ENDED");
        } else if (n == 22 && d[5] == LocalPlayer() && memcmp(d + 6, relayToken_.data(), 16) == 0) {
            resumingLobby_ = false;
            lastRecoverySend_ = Clock::time_point{};
        }
        break;
    default: break;  // 105 OK
    }
}

// ---- peer ------------------------------------------------------------------------------------------------------

void Session::SendHello() {
    std::vector<uint8_t> p = Header(kHello);
    Put32(p, build_);
    Put32(p, NowMs());  // echoed by the pong: the round trip
    SendRaw(p);
    lastHello_ = Clock::now();
    if (LogOn()) {
        const sockaddr_in* a = reinterpret_cast<const sockaddr_in*>(peer_);
        fprintf(stderr, "net %.1f: hello -> port %d (%s, path %s, connected %d)\n", LogSeconds(), ntohs(a->sin_port), host_ ? "host" : "joiner", PathName(),
                connected_ ? 1 : 0);
    }
}

void Session::SendWelcome() {
    std::vector<uint8_t> p = Header(kWelcome);
    Put32(p, build_);
    Put32(p, static_cast<uint32_t>(config_.credits));
    Put32(p, config_.seed);
    Put32(p, config_.firstSerial);
    for (int volume : config_.portVolumes) Put32(p, static_cast<uint32_t>(volume));
    Put8(p, config_.flags);
    Put8(p, config_.delay);
    Put32(p, config_.startFrame);
    const size_t n = std::min<size_t>(config_.save.size(), 60000);
    Put16(p, static_cast<uint32_t>(n));
    p.insert(p.end(), config_.save.begin(), config_.save.begin() + static_cast<std::ptrdiff_t>(n));
    SendRaw(p);
}

void Session::Poll() {
    PollLobbyCleanup();
    if (sock_ == kNoSocket) return;
    for (;;) {
        uint8_t buf[65536];
        sockaddr_storage from = {};
        socklen_t fromLen = sizeof(from);
        const int n = recvfrom(static_cast<SOCKET>(sock_), reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n < 0) {
#ifdef _WIN32
            const int e = WSAGetLastError();
            if (e == WSAECONNRESET) continue;  // an ICMP unreachable for an earlier send: not data
            if (e != WSAEWOULDBLOCK && LogOn()) {
                static int lastLogged = 0;
                if (e != lastLogged) fprintf(stderr, "net: recvfrom error %d\n", e);
                lastLogged = e;
            }
#endif
            break;
        }
        if (n == 0) break;
        OnPacket(buf, static_cast<size_t>(n), &from, static_cast<int>(fromLen));
    }
    // hellos until connected: the joiner asks for the welcome; both sides' hellos open their routers for the other
    if (!connected_ && peerKnown_ && std::chrono::duration<double>(Clock::now() - lastHello_).count() > 0.25) SendHello();
    // the host's welcome waits for a pong (the delay) for at most kWelcomeWait after the first hello
    if (host_ && !connected_ && helloSeen_ && !welcomeReady_ && std::chrono::duration<double>(Clock::now() - firstHelloIn_).count() > kWelcomeWait) {
        welcomeReady_ = true;
        DecideDelay();
        SendWelcome();
    }
    // connected with both paths at hand: a keepalive on the other path every kKeepaliveEvery (keeps the relay room
    // and the NAT mappings alive); the current path silent for kPathSilence -> probing: everything goes on both
    // paths until a packet arrives on one of them, which then is the path (OnPeerPacket)
    if (connected_ && RelayAvailable() && !candidates_.empty()) {
        const Clock::time_point now = Clock::now();
        if (std::chrono::duration<double>(now - lastKeepalive_).count() > kKeepaliveEvery) {
            if (path_ == Path::Direct) SendRelayed(Header(kKeepalive));
            else SendDirect(Header(kKeepalive));
            lastKeepalive_ = now;
        }
        const Clock::time_point lastIn = path_ == Path::Direct ? lastDirectIn_ : lastRelayIn_;
        if (!probing_ && std::chrono::duration<double>(now - lastIn).count() > kPathSilence) {
            probing_ = true;
            sendPending_ = true;
            if (LogOn()) fprintf(stderr, "net %.1f: %s silent for %.0f s, probing both paths\n", LogSeconds(), PathName(), kPathSilence);
        }
    }
    // A stale directory entry may be accepted by the service while its host is
    // already gone. Bound both the unanswered request and the peer handshake
    // using real time, independently of rendering or the menu's frame count.
    if ((lobbyJoining_ || (lobbyJoinResult_ == JoinResult::Ok && !connected_)) &&
        std::chrono::duration<double>(Clock::now() - lobbyJoinStarted_).count() > (lobbyJoining_ ? 6.0 : 12.0)) {
        lobbyJoinResult_ = lobbyJoining_ ? JoinResult::NoAnswer : JoinResult::TimedOut;
        lobbyJoining_ = false;
        peerKnown_ = false;
        candidates_.clear();
        relayRoom_ = 0;
        relayToken_.fill(0);
    }
    // a lobby join without an answer yet is repeated every second
    if (lobbyJoining_ && std::chrono::duration<double>(Clock::now() - lobbyLastSend_).count() > 1.0) LobbyJoin(lobbyJoinRoom_, lobbyJoinCode_);
    PollRecovery();
}

void Session::OnPacket(const uint8_t* d, size_t n, const void* from, int fromLen) {
    if (n >= 6 && memcmp(d, "H2LB", 4) == 0) {  // the lobby server
        if (lobbyKnown_ && fromLen >= static_cast<int>(sizeof(sockaddr_in)) && memcmp(from, lobbyAddr_, sizeof(sockaddr_in)) == 0) OnLobbyPacket(d, n);
        return;
    }
    if (lobbyJoinResult_ == JoinResult::TimedOut || lobbyJoinResult_ == JoinResult::NoAnswer) return;
    // A waiting room's host socket takes no direct game data until the lobby named the other player (an unsolicited
    // hello cannot claim it); a joiner's socket none before the join was answered.
    if ((lobbyHosting_ || lobbyJoining_ || lobbyJoinResult_ == JoinResult::Ok) && candidates_.empty()) return;
    if (cutDirectAfter_ >= 0.0 && connected_ && std::chrono::duration<double>(Clock::now() - connectedAt_).count() > cutDirectAfter_) return;  // (tests)
    OnPeerPacket(d, n, from, fromLen, false);
}

void Session::OnPeerPacket(const uint8_t* d, size_t n, const void* from, int fromLen, bool viaRelay) {
    if (n < 6 || memcmp(d, "H2NP", 4) != 0 || d[5] != kVersion) return;
    const uint8_t type = d[4];
    // Recovery changes the simulation timeline: it is accepted only inside the authenticated relay envelope.
    if (type >= 7 && (!viaRelay || !CanRejoin() || type > 12)) return;
    if (!viaRelay && !candidates_.empty() && fromLen >= static_cast<int>(sizeof(sockaddr_in))) {
        // a packet from any candidate endpoint of the other side fixes which one is reachable
        bool known = false;
        for (const auto& c : candidates_)
            if (memcmp(c.data(), from, sizeof(sockaddr_in)) == 0) known = true;
        if (!known && host_ && !connected_) {  // the same address on another port (the joiner's router rewrote it)
            const sockaddr_in* a = static_cast<const sockaddr_in*>(from);
            for (const auto& c : candidates_)
                if (reinterpret_cast<const sockaddr_in*>(c.data())->sin_addr.s_addr == a->sin_addr.s_addr) known = true;
        }
        if (!known) return;  // somebody else
        if (memcmp(peer_, from, sizeof(sockaddr_in)) != 0) {
            memcpy(peer_, from, sizeof(sockaddr_in));
            peerLen_ = sizeof(sockaddr_in);
            if (LogOn()) fprintf(stderr, "net %.1f: direct peer fixed to port %d\n", LogSeconds(), ntohs(static_cast<const sockaddr_in*>(from)->sin_port));
        }
    } else if (!viaRelay) {
        if (host_ && !connected_ && type == kHello) {
            // console modes: the first hello's sender, or the same address on another port
            bool adopt = !peerKnown_;
            if (peerKnown_ && fromLen >= static_cast<int>(sizeof(sockaddr_in))) {
                const sockaddr_in* a = static_cast<const sockaddr_in*>(from);
                const sockaddr_in* b = reinterpret_cast<const sockaddr_in*>(peer_);
                adopt = a->sin_addr.s_addr == b->sin_addr.s_addr;
            }
            if (adopt) {
                memcpy(peer_, from, static_cast<size_t>(std::min<int>(fromLen, static_cast<int>(sizeof(peer_)))));
                peerLen_ = fromLen;
                peerKnown_ = true;
            }
        }
        if (!peerKnown_ || fromLen != peerLen_ || memcmp(peer_, from, static_cast<size_t>(fromLen)) != 0) return;  // somebody else
    }
    // the path this came on: the first one fixes the session's path; both stay watched for the hand-over
    const Clock::time_point now = Clock::now();
    if (viaRelay) {
        lastRelayIn_ = now;
        haveRelayIn_ = true;
    } else {
        lastDirectIn_ = now;
        haveDirectIn_ = true;
    }
    if (path_ == Path::None || (probing_ && type != kKeepalive)) {
        const Path was = path_;
        path_ = viaRelay ? Path::Relay : Path::Direct;
        probing_ = false;
        if (viaRelay && !peerKnown_) {
            memcpy(peer_, lobbyAddr_, sizeof(lobbyAddr_));
            peerLen_ = sizeof(sockaddr_in);
            peerKnown_ = true;
        }
        if (LogOn()) fprintf(stderr, "net %.1f: path %s: %s\n", LogSeconds(), was == Path::None ? "fixed" : "switched to", PathName());
    }
    if (LogOn()) {
        static int inputs = 0;
        const sockaddr_in* a = static_cast<const sockaddr_in*>(from);
        if ((type < 7 && type != kInput && type != kKeepalive && type != kPong) || (inputs++ % 300) == 0)
            fprintf(stderr, "net %.1f: packet type %d from port %d via %s (connected %d)\n", LogSeconds(), type, ntohs(a->sin_port), viaRelay ? "relay" : "direct",
                    connected_ ? 1 : 0);
    }
    ++stats_.packetsIn;
    lastFromRemote_ = now;
    d += 6;
    n -= 6;
    if (type >= 7) { OnRecoveryPacket(type, d, n); return; }
    switch (type) {
    case kHello:
        if (n >= 4) remoteBuild_ = Get32(d);
        if (n >= 8) SendPong(Get32(d + 4), viaRelay);
        if (host_ && !connected_) {
            if (!helloSeen_) {
                helloSeen_ = true;
                firstHelloIn_ = now;
            }
            // the delay from the best of a few pongs (one stray sample must not decide it), else after kWelcomeWait
            if (!welcomeReady_ && rttMs_ >= 0.0 && (pongs_ >= 3 || std::chrono::duration<double>(now - firstHelloIn_).count() > 0.75)) {
                welcomeReady_ = true;
                DecideDelay();
            }
            if (welcomeReady_) SendWelcome();  // (also to a different build, so that side can show the mismatch too)
        }
        break;
    case kPong:
        if (n >= 4) {
            const double rtt = static_cast<double>(static_cast<uint32_t>(NowMs() - Get32(d)));
            if (LogOn()) fprintf(stderr, "net %.1f: pong via %s, round trip %.0f ms\n", LogSeconds(), viaRelay ? "relay" : "direct", rtt);
            if (rtt < 10000.0 && (rttMs_ < 0.0 || rtt < rttMs_)) rttMs_ = rtt;
            ++pongs_;
        }
        break;
    case kKeepalive: break;
    case kWelcome: {
        if (host_ || n < 4 + 4 * 8 + 2 + 4 + 2) break;
        remoteBuild_ = Get32(d);
        d += 4;
        SessionConfig c;
        c.credits = static_cast<int32_t>(Get32(d));
        c.seed = Get32(d + 4);
        c.firstSerial = Get32(d + 8);
        for (int k = 0; k < 5; ++k) c.portVolumes[k] = static_cast<int32_t>(Get32(d + 12 + 4 * k));
        c.flags = d[32];
        c.delay = d[33];
        c.startFrame = Get32(d + 34);
        const size_t saveLen = Get16(d + 38);
        if (40 + saveLen > n - 4) break;
        c.save.assign(d + 40, d + 40 + saveLen);
        if (BuildMismatch()) break;  // never connected: the lobby says why
        // Prefer the route that delivered the complete welcome over an earlier small HELLO/PONG. Once the
        // relay rescued this handshake, a delayed direct duplicate must not send the guest back to a bad route.
        if (!peerInputSeen_ && (!connected_ || viaRelay)) {
            path_ = viaRelay ? Path::Relay : Path::Direct;
            probing_ = false;
        }
        if (!connected_) {
            config_ = c;  // the first welcome decides (repeats are identical)
            connectedAt_ = now;
        }
        connected_ = true;
        break;
    }
    case kInput: {
        if (recovery_ != Recovery::Normal && !resumeFrame_) break;  // stale input from the old timeline
        if (n < 23 || Get32(d + 2) != inputEpoch_) break;
        const int player = d[0];
        if (player != RemotePlayer()) break;
        const uint32_t ack = Get32(d + 6), hashFrame = Get32(d + 10), hash = Get32(d + 14), first = Get32(d + 18);
        const int count = d[22];
        if (count > kMaxRecords || 23 + static_cast<size_t>(count) * kRecord != n || first > UINT32_MAX - count) break;
        if (historyStarted_ && first > std::max({HistoryEnd(), advertisedEnd_, resumeFrame_}) + 1200) break;
        if (!peerInputSeen_) {
            if (BuildMismatch()) break;
            path_ = viaRelay ? Path::Relay : Path::Direct;
            probing_ = false;
            peerInputSeen_ = true;
        }
        if (host_ && !connected_ && !BuildMismatch()) {  // the joiner has the welcome and sent valid input
            connected_ = true;
            connectedAt_ = now;
        }
        if (ack > ackedByRemote_) ackedByRemote_ = ack;  // the other side needs nothing below ack
        for (int i = 0; i < count; ++i) {
            const uint32_t frame = first + static_cast<uint32_t>(i);
            if (remote_.count(frame)) continue;
            remote_[frame] = GetRecord(d + 23 + static_cast<size_t>(i) * kRecord);
            if (frame > remoteLatest_) remoteLatest_ = frame;
        }
        const uint32_t implicit = config_.startFrame + config_.delay;  // (frames below it are empty on both sides)
        if (remoteNext_ < implicit) remoteNext_ = implicit;
        while (remote_.count(remoteNext_)) ++remoteNext_;  // everything below remoteNext_ is here
        if (hashFrame != 0xFFFFFFFFu && recovery_ == Recovery::Normal && (!resumeFrame_ || hashFrame >= resumeFrame_)) CheckHash(hashFrame, hash);
        break;
    }
    case kBye: if (n == 4 && Get32(d) == inputEpoch_) peerLeft_ = true; break;
    default: break;
    }
}

void Session::PushLocal(uint32_t frame, const FrameInput& in) {
    if (CatchingUp() && (!resumeFrame_ || frame < resumeFrame_)) return;
    if (local_.count(frame)) return;
    local_[frame] = in;
    sendPending_ = true;
    if (!haveLocal_ || frame > localLatest_) localLatest_ = frame;
    haveLocal_ = true;
    // inputs the other side has acknowledged and that are older than a few seconds are gone
    if (local_.size() > 1200) {
        auto it = local_.begin();
        while (local_.size() > 1200 && it != local_.end() && it->first < ackedByRemote_) it = local_.erase(it);
    }
}

bool Session::LocalInput(uint32_t frame, FrameInput* out) const {
    auto it = local_.find(frame);
    if (it == local_.end()) return false;
    *out = it->second;
    return true;
}

bool Session::RemoteInput(uint32_t frame, FrameInput* out) const {
    if (PlayingAlone() && (!resumeFrame_ || frame < resumeFrame_)) {
        *out = FrameInput{};
        out->present = true;
        out->in.kind = 1;
        out->in.x = out->in.y = 0xFFFF;
        return true;
    }
    if (frame < config_.startFrame + config_.delay) {  // the first Delay() steps run before any input could be pushed
        *out = FrameInput();
        return true;
    }
    auto it = remote_.find(frame);
    if (it == remote_.end()) return false;
    *out = it->second;
    return true;
}

void Session::Send() {
    if (sock_ == kNoSocket || !peerKnown_ || !connected_) return;
    // once per new local input, else at most every 20 ms (the render loop runs faster than the game clock)
    const Clock::time_point now = Clock::now();
    if (!sendPending_ && std::chrono::duration<double>(now - lastSend_).count() < 0.02) return;
    sendPending_ = false;
    lastSend_ = now;
    if (PlayingAlone() && !resumeFrame_) {
        // Do not let an asymmetric outage feed solo-timeline inputs/hashes into a peer still in lockstep.
        // Recovery control invites it onto the surviving timeline before ordinary input resumes.
        SendRaw(Header(kKeepalive));
        return;
    }
    std::vector<uint8_t> p = Header(kInput);
    Put8(p, static_cast<uint32_t>(LocalPlayer()));
    Put8(p, config_.delay);
    Put32(p, inputEpoch_);
    // ack: the first frame still missing from the other side (everything below it has arrived)
    Put32(p, std::max(remoteNext_, config_.startFrame + config_.delay));
    if (hashes_.empty()) {
        Put32(p, 0xFFFFFFFFu);
        Put32(p, 0);
    } else {
        Put32(p, hashes_.rbegin()->first);
        Put32(p, hashes_.rbegin()->second);
    }
    // every local input the other side has not acknowledged (from its ack on), the newest kMaxRecords of them
    uint32_t first = 0;
    int count = 0;
    if (haveLocal_ && !local_.empty()) {
        first = std::max(ackedByRemote_, local_.begin()->first);
        if (localLatest_ + 1 > first + static_cast<uint32_t>(kMaxRecords)) first = localLatest_ + 1 - static_cast<uint32_t>(kMaxRecords);
        for (uint32_t f = first; f <= localLatest_ && count < kMaxRecords; ++f) {
            if (!local_.count(f)) break;
            ++count;
        }
    }
    Put32(p, first);
    Put8(p, static_cast<uint32_t>(count));
    for (int i = 0; i < count; ++i) PutRecord(p, local_.at(first + static_cast<uint32_t>(i)));
    SendRaw(p);
}

void Session::ReportHash(uint32_t frame, uint32_t hash) {
    hashes_[frame] = hash;
    if (hashes_.size() > 1800) hashes_.erase(hashes_.begin());
    for (size_t i = 0; i < pendingTheirs_.size();) {
        if (pendingTheirs_[i].first <= frame) {
            CheckHash(pendingTheirs_[i].first, pendingTheirs_[i].second);
            pendingTheirs_.erase(pendingTheirs_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void Session::CheckHash(uint32_t frame, uint32_t theirs) {
    if (desynced_) return;
    auto it = hashes_.find(frame);
    if (it == hashes_.end()) {
        if (hashes_.empty() || frame > hashes_.rbegin()->first) {  // ahead of us: checked when we get there
            if (pendingTheirs_.size() < 64) pendingTheirs_.emplace_back(frame, theirs);
        }
        return;
    }
    if (it->second != theirs) {
        desynced_ = true;
        desyncFrame_ = frame;
        desyncOurs_ = it->second;
        desyncTheirs_ = theirs;
    }
}

bool Session::Desynced(uint32_t* frame, uint32_t* ours, uint32_t* theirs) const {
    if (!desynced_) return false;
    if (frame) *frame = desyncFrame_;
    if (ours) *ours = desyncOurs_;
    if (theirs) *theirs = desyncTheirs_;
    return true;
}

double Session::SecondsSinceRemote() const { return std::chrono::duration<double>(Clock::now() - lastFromRemote_).count(); }

Session::Stats Session::GetStats() const {
    Stats s = stats_;
    s.remoteAck = ackedByRemote_;
    s.remoteLatest = remoteLatest_;
    return s;
}

void Session::Close() {
    if (sock_ == kNoSocket) return;
    if (peerKnown_) {
        auto bye = Header(kBye); Put32(bye, inputEpoch_);
        for (int k = 0; k < 3; ++k) SendRaw(bye);
    }
    if (!CanRejoin() || !historyStarted_) LobbyLeave();
    if (lobbyLeavePending_ && lobbyKnown_) {
        PollLobbyCleanup();
        constexpr size_t kMaxCancelledSockets = 16;
        if (lobbyCancellations_.size() == kMaxCancelledSockets) {
            CloseSock(lobbyCancellations_.front().socket);
            lobbyCancellations_.erase(lobbyCancellations_.begin());
        }
        LobbyCancellation cancellation;
        cancellation.socket = sock_;
        memcpy(cancellation.server.data(), lobbyAddr_, sizeof(lobbyAddr_));
        cancellation.room = lobbyLeaveRoom_;
        cancellation.code = lobbyLeaveCode_;
        cancellation.started = Clock::now();
        lobbyCancellations_.push_back(cancellation);
        sock_ = kNoSocket;
        lobbyLeavePending_ = false;
        PollLobbyCleanup();
        return;
    }
    CloseSock(sock_);
    sock_ = kNoSocket;
}

void Session::Reset() {
    if (LogOn()) fprintf(stderr, "net %.1f: reset (connected %d)\n", LogSeconds(), connected_ ? 1 : 0);
    Abandon();
    Close();
    host_ = connected_ = peerLeft_ = peerKnown_ = false;
    peerInputSeen_ = false;
    memset(peer_, 0, sizeof(peer_));
    peerLen_ = 0;
    localCode_.clear();
    remoteBuild_ = 0;
    candidates_.clear();
    relayRoom_ = 0;
    relayToken_.fill(0);
    path_ = Path::None;
    probing_ = false;
    cutDirectAfter_ = getenv("HOTD2_NET_CUTDIRECT") ? atof(getenv("HOTD2_NET_CUTDIRECT")) : -1.0;
    haveDirectIn_ = haveRelayIn_ = helloSeen_ = welcomeReady_ = false;
    rttMs_ = -1.0;
    pongs_ = 0;
    helloStart_ = lastDirectIn_ = lastRelayIn_ = lastKeepalive_ = firstHelloIn_ = Clock::now();
    lobbyHosting_ = lobbyCreating_ = lobbyJoining_ = lobbyRoomsKnown_ = false;
    lobbyLeavePending_ = false;
    lobbyLeaveRoom_ = lobbyLeaveCode_ = 0;
    lobbyRoom_ = lobbyCode_ = lobbyJoinRoom_ = lobbyJoinCode_ = 0;
    lobbyJoinResult_ = JoinResult::Pending;
    lobbyRooms_.clear();
    lobbyLastAnswer_ = Clock::now();
    lobbyJoinStarted_ = Clock::time_point{};
    config_ = SessionConfig();
    local_.clear();
    remote_.clear();
    localLatest_ = remoteNext_ = ackedByRemote_ = remoteLatest_ = 0;
    haveLocal_ = false;
    hashes_.clear();
    pendingTheirs_.clear();
    desynced_ = false;
    desyncFrame_ = desyncOurs_ = desyncTheirs_ = 0;
    lastHello_ = lastFromRemote_ = lastSend_ = Clock::now();
    sendPending_ = false;
    stats_ = Stats();
    recovery_ = Recovery::Normal;
    rejoinCapable_ = historyStarted_ = replayReset_ = waitingInfo_ = resumingLobby_ = recoveryFailed_ = resumeAcknowledged_ = false;
    recoveryNonce_ = resumeFrame_ = initialHash_ = advertisedEnd_ = replayedEnd_ = inputEpoch_ = 0;
    history_.clear();
    completedRecoveries_.clear();
    recoveryError_.clear();
    lastRecoverySend_ = recoveryStarted_ = lastHistoryReply_ = lastReplayProgress_ = Clock::time_point{};
}

namespace {
void PutConfig(std::vector<uint8_t>& p, const SessionConfig& c) {
    Put32(p, static_cast<uint32_t>(c.credits)); Put32(p, c.seed); Put32(p, c.firstSerial);
    for (int volume : c.portVolumes) Put32(p, static_cast<uint32_t>(volume));
    Put8(p, c.flags); Put8(p, c.delay); Put32(p, c.startFrame);
    Put16(p, static_cast<uint32_t>(c.save.size())); p.insert(p.end(), c.save.begin(), c.save.end());
}
bool ReadConfig(const uint8_t* d, size_t n, SessionConfig& c) {
    if (n < 40 || Get16(d + 38) > 60000 || n != 40 + Get16(d + 38) || d[33] < 1 || d[33] > 10) return false;
    c.credits = static_cast<int32_t>(Get32(d)); c.seed = Get32(d + 4); c.firstSerial = Get32(d + 8);
    for (int k = 0; k < 5; ++k) c.portVolumes[k] = static_cast<int32_t>(Get32(d + 12 + 4 * k));
    c.flags = d[32]; c.delay = d[33]; c.startFrame = Get32(d + 34);
    if (c.startFrame > 1000000) return false;
    c.save.assign(d + 40, d + n);
    return true;
}
std::vector<uint8_t> ReadTicket(const std::string& path, uint32_t build) {
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> p(46);
    if (!f.read(reinterpret_cast<char*>(p.data()), p.size()) || f.peek() != EOF ||
        memcmp(p.data(), "H2RJ\1", 5) || Get32(p.data() + 5) != build || p[9] > 1 || !Get32(p.data() + 10)) return {};
    sockaddr_in addr{}; memcpy(&addr, p.data() + 30, sizeof(addr));
    if (addr.sin_family != AF_INET || !addr.sin_port) return {};
    return p;
}
}

void Session::BeginHistory(uint32_t initialHash) {
    if (!CanRejoin()) return;
    historyStarted_ = true;
    initialHash_ = initialHash;
    history_.clear();
    replayedEnd_ = advertisedEnd_ = config_.startFrame;
}
bool Session::HasTicket(const std::string& path) const { return !ReadTicket(path, build_).empty(); }
bool Session::SaveTicket(const std::string& path) const {
    if (!CanRejoin()) return false;
    std::vector<uint8_t> p{'H','2','R','J',1};
    Put32(p, build_); Put8(p, LocalPlayer()); Put32(p, relayRoom_);
    p.insert(p.end(), relayToken_.begin(), relayToken_.end());
    p.insert(p.end(), lobbyAddr_, lobbyAddr_ + sizeof(lobbyAddr_));
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(p.data()), p.size()); f.flush();
    return f.good();
}
bool Session::LoadTicket(const std::string& path, std::string* error) {
    const auto p = ReadTicket(path, build_);
    if (p.empty()) { if (error) *error = "NO REJOINABLE MATCH FOR THIS VERSION"; return false; }
    Reset();
    if (!Open(0, error)) return false;
    host_ = p[9] == 0; relayRoom_ = Get32(p.data() + 10);
    memcpy(relayToken_.data(), p.data() + 14, 16); memcpy(lobbyAddr_, p.data() + 30, 16);
    lobbyKnown_ = peerKnown_ = rejoinCapable_ = true;
    memcpy(peer_, lobbyAddr_, 16); peerLen_ = sizeof(sockaddr_in); path_ = Path::Relay;
    RequestRecovery(); resumingLobby_ = true;
    SendResumeRequest();
    return true;
}
void Session::SendResumeRequest(uint8_t type) {
    auto p = LobbyHeader(type); Put32(p, relayRoom_);
    p.insert(p.end(), relayToken_.begin(), relayToken_.end()); Put32(p, build_);
    uint8_t ip[4]; uint16_t port; LocalEndpoint(ip, &port);
    p.insert(p.end(), ip, ip + 4); Put16(p, port); SendToLobby(p);
}
void Session::Abandon() {
    if (CanRejoin() && historyStarted_ && recovery_ != Recovery::Receiving) SendResumeRequest(8);
    historyStarted_ = false;
}
bool Session::PlayingAlone() const { return recovery_ == Recovery::Solo || recovery_ == Recovery::Sending; }
bool Session::CatchingUp() const { return recovery_ == Recovery::Receiving; }
void Session::ContinueAlone(uint32_t frame) {
    if (!CanRejoin() || !historyStarted_) return;
    if (history_.size() > frame - config_.startFrame) history_.resize(frame - config_.startFrame);
    recovery_ = Recovery::Solo; resumeFrame_ = 0; peerLeft_ = false;
    remote_.clear(); pendingTheirs_.clear(); ackedByRemote_ = frame;
    recoveryStarted_ = Clock::now(); lastRecoverySend_ = Clock::time_point{};
    printf("netplay: P%d disconnected at frame %u; P%d keeps playing, rejoin available\n", RemotePlayer() + 1, frame, LocalPlayer() + 1);
    fflush(stdout);
}
void Session::RequestRecovery() {
    recovery_ = Recovery::Receiving; waitingInfo_ = true; replayReset_ = false;
    resumeFrame_ = 0; recoveryFailed_ = false; recoveryError_.clear(); peerLeft_ = false;
    recoveryNonce_ = std::random_device{}(); if (!recoveryNonce_) recoveryNonce_ = 1;
    recoveryStarted_ = Clock::now(); lastRecoverySend_ = Clock::time_point{};
    path_ = Path::Relay; probing_ = false;
}
void Session::FailRecovery(const char* reason) {
    recoveryFailed_ = true; recoveryError_ = reason; resumingLobby_ = false;
    fprintf(stderr, "netplay: rejoin failed: %s\n", reason);
}
void Session::SendRecoveryInfo() {
    auto p = Header(8); Put32(p, recoveryNonce_); Put32(p, build_);
    Put32(p, initialHash_); Put32(p, HistoryEnd()); PutConfig(p, config_); SendRelayed(p);
}
void Session::ReplayStarted(uint32_t initialHash) {
    replayReset_ = false;
    if (initialHash != initialHash_) { FailRecovery("MATCH DATA DOES NOT MATCH"); return; }
    historyStarted_ = true; replayedEnd_ = config_.startFrame;
    lastReplayProgress_ = Clock::now();
    hashes_.clear(); pendingTheirs_.clear(); desynced_ = false;
    printf("netplay: restoring P%d from frame %u to %u\n", LocalPlayer() + 1, config_.startFrame, advertisedEnd_);
    fflush(stdout);
}
bool Session::ReplayFrame(uint32_t frame, FrameInput out[2], uint32_t* hash) const {
    if (!CatchingUp() || waitingInfo_ || replayReset_ || (resumeFrame_ && frame >= resumeFrame_)) return false;
    if (frame < config_.startFrame || frame >= HistoryEnd()) return false;
    const auto& r = history_[frame - config_.startFrame];
    out[0] = r.input[0]; out[1] = r.input[1]; *hash = r.hash;
    return true;
}
void Session::RecordFrame(uint32_t frame, const FrameInput in[2], uint32_t hash) {
    if (!historyStarted_) return;
    replayedEnd_ = frame + 1;
    while (remote_.size() > 1200 && remote_.begin()->first < frame) remote_.erase(remote_.begin());
    if (CatchingUp() && frame < HistoryEnd()) {
        lastReplayProgress_ = Clock::now();
        if (history_[frame - config_.startFrame].hash != hash) FailRecovery("REJOIN STATE CHECK FAILED");
        return;
    }
    if (frame != HistoryEnd() || history_.size() >= kMaxHistory) {
        // A bounded log must never silently replay incomplete history.
        historyStarted_ = false; rejoinCapable_ = false;
        fprintf(stderr, "netplay: rejoin history limit or discontinuity at %u\n", frame);
        return;
    }
    HistoryFrame r; r.input[0] = in[0]; r.input[1] = in[1]; r.hash = hash; history_.push_back(r);
    if (PlayingAlone()) ackedByRemote_ = std::max(ackedByRemote_, frame);  // bound old input buffers during solo play
}
void Session::PrepareResume(uint32_t frame) {
    inputEpoch_ = recoveryNonce_;
    resumeFrame_ = frame; remote_.clear(); pendingTheirs_.clear();
    remoteNext_ = ackedByRemote_ = frame; remoteLatest_ = 0;
    local_.erase(local_.lower_bound(frame), local_.end());
    if (!local_.empty()) localLatest_ = local_.rbegin()->first;
    haveLocal_ = !local_.empty(); peerLeft_ = false;
    // Resume through the relay so a changed public endpoint cannot use a stale direct candidate.
    path_ = Path::Relay; probing_ = false; candidates_.clear();
    memcpy(peer_, lobbyAddr_, 16); peerLen_ = sizeof(sockaddr_in); peerKnown_ = true;
}
bool Session::FinishRecovery(uint32_t frame) {
    if (!resumeFrame_ || frame < resumeFrame_ || recovery_ == Recovery::Normal) return false;
    FrameInput remote;
    if (!RemoteInput(frame, &remote) || !local_.count(frame)) return false;
    if (CatchingUp() && HistoryEnd() > frame) history_.resize(frame - config_.startFrame);
    recovery_ = Recovery::Normal; lastFromRemote_ = Clock::now(); peerLeft_ = false;
    completedRecoveries_.push_back(recoveryNonce_);
    if (completedRecoveries_.size() > 8) completedRecoveries_.erase(completedRecoveries_.begin());
    hashes_.clear(); pendingTheirs_.clear();
    printf("netplay: rejoined as P%d at frame %u; synchronized lockstep restored\n", LocalPlayer() + 1, frame);
    fflush(stdout); return true;
}
void Session::OnRecoveryPacket(uint8_t type, const uint8_t* d, size_t n) {
    if (type == 12) {  // survivor asks a still-running peer to resynchronize (partition recovery)
        if (n != 8 || Get32(d) != build_ || Get32(d + 4) != inputEpoch_) return;
        if (historyStarted_ && IsHost() && recovery_ == Recovery::Normal)
            ContinueAlone(HistoryEnd());
        if (historyStarted_ && !IsHost() &&
            (recovery_ == Recovery::Normal || recovery_ == Recovery::Solo)) RequestRecovery();
        return;
    }
    if (type == 7) {  // authenticated returner asks for the surviving timeline
        if (n != 8 || Get32(d + 4) != build_ || !historyStarted_ || CatchingUp()) return;
        const uint32_t nonce = Get32(d);
        if (std::find(completedRecoveries_.begin(), completedRecoveries_.end(), nonce) != completedRecoveries_.end()) return;
        if (recovery_ != Recovery::Sending || recoveryNonce_ != nonce) {
            recoveryNonce_ = nonce; recovery_ = Recovery::Sending; resumeFrame_ = 0; resumeAcknowledged_ = false;
            peerLeft_ = false; remote_.clear(); pendingTheirs_.clear();
            printf("netplay: returning P%d found; sending match history (%u frames)\n", RemotePlayer() + 1, static_cast<unsigned>(history_.size()));
            fflush(stdout);
        }
        SendRecoveryInfo(); return;
    }
    if (n < 4 || Get32(d) != recoveryNonce_) return;
    if (type == 8 && CatchingUp() && waitingInfo_) {
        SessionConfig c;
        if (n < 56 || Get32(d + 4) != build_ || !ReadConfig(d + 16, n - 16, c)) return;
        const uint32_t end = Get32(d + 12);
        if (end < c.startFrame || end - c.startFrame > kMaxHistory) return;
        config_ = c; initialHash_ = Get32(d + 8); advertisedEnd_ = end;
        history_.clear(); local_.clear(); remote_.clear(); haveLocal_ = false;
        localLatest_ = remoteNext_ = ackedByRemote_ = remoteLatest_ = 0;
        waitingInfo_ = false; replayReset_ = true; connected_ = true;
        connectedAt_ = Clock::now(); lastRecoverySend_ = Clock::time_point{};
    } else if (type == 9 && historyStarted_ && !CatchingUp() &&
               (recovery_ == Recovery::Sending || (recovery_ == Recovery::Normal && resumeFrame_)) && n == 8) {
        // The other client may still lack the final history packet even after its first future input
        // lets us resume. Keep serving that tail; otherwise one lost packet deadlocks both clients.
        // At most six MTU-sized packets per 50 ms: below the relay's authenticated packet/byte budget.
        const auto now = Clock::now();
        if (std::chrono::duration<double>(now - lastHistoryReply_).count() < 0.04) return;
        lastHistoryReply_ = now;
        uint32_t first = Get32(d + 4);
        const uint32_t end = resumeFrame_ ? std::min(HistoryEnd(), resumeFrame_) : HistoryEnd();
        if (first < config_.startFrame || first > end) return;
        for (int part = 0; part < 6; ++part) {
            const uint32_t count = std::min<uint32_t>(32, end - first);
            auto p = Header(10); Put32(p, recoveryNonce_); Put32(p, end); Put32(p, first); Put8(p, count);
            for (uint32_t i = 0; i < count; ++i) {
                const auto& r = history_[first + i - config_.startFrame];
                PutRecord(p, r.input[0]); PutRecord(p, r.input[1]); Put32(p, r.hash);
            }
            SendRelayed(p); first += count;
            if (first == end) break;
        }
    } else if (type == 10 && CatchingUp() && !waitingInfo_ && n >= 13) {
        const uint32_t end = Get32(d + 4), first = Get32(d + 8), count = d[12];
        if (count > 32 || n != 13 + count * 32 || end < config_.startFrame || end - config_.startFrame > kMaxHistory ||
            first != HistoryEnd() || first > end || count > end - first) return;
        advertisedEnd_ = std::max(advertisedEnd_, end);
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* r = d + 13 + i * 32;
            HistoryFrame f; f.input[0] = GetRecord(r); f.input[1] = GetRecord(r + 14); f.hash = Get32(r + 28);
            history_.push_back(f);
        }
    } else if (type == 11 && n == 13) {
        const uint8_t kind = d[4]; const uint32_t frame = Get32(d + 5), hash = Get32(d + 9);
        if (kind == 0 && recovery_ == Recovery::Sending && frame > config_.startFrame && frame <= HistoryEnd() &&
            HistoryEnd() - frame <= 90 && history_[frame - config_.startFrame - 1].hash == hash) {
            if (!resumeFrame_) { PrepareResume(HistoryEnd() + 120); resumeAcknowledged_ = false; }
            auto p = Header(11); Put32(p, recoveryNonce_); Put8(p, 1); Put32(p, resumeFrame_); Put32(p, 0); SendRelayed(p);
        } else if (kind == 1 && CatchingUp() && !waitingInfo_ && frame >= replayedEnd_ && frame <= advertisedEnd_ + 300) {
            if (!resumeFrame_) PrepareResume(frame);
            if (resumeFrame_ == frame) {
                auto p = Header(11); Put32(p, recoveryNonce_); Put8(p, 2); Put32(p, frame); Put32(p, 0); SendRelayed(p);
            }
        } else if (kind == 2 && recovery_ == Recovery::Sending && frame == resumeFrame_) resumeAcknowledged_ = true;
    }
}
void Session::PollRecovery() {
    if (!CanRejoin() || recoveryFailed_) return;
    const auto now = Clock::now();
    if (resumingLobby_) {
        if (std::chrono::duration<double>(now - lobbyLastSend_).count() > 1) SendResumeRequest();
        if (std::chrono::duration<double>(now - recoveryStarted_).count() > 10) FailRecovery("NO ANSWER FROM THE LOBBY SERVER");
        return;
    }
    // Also keep direct/solo sessions registered when the absent peer cannot answer.
    if (connected_ && std::chrono::duration<double>(now - lastKeepalive_).count() > 2) {
        SendRelayed(Header(kKeepalive)); lastKeepalive_ = now;
    }
    if (recovery_ == Recovery::Sending && SecondsSinceRemote() > 5) ContinueAlone(HistoryEnd());
    if (CatchingUp() && (SecondsSinceRemote() > 15 || (waitingInfo_ && std::chrono::duration<double>(now - recoveryStarted_).count() > 30))) {
        FailRecovery("THE OTHER PLAYER IS NO LONGER IN THE MATCH"); return;
    }
    if (CatchingUp() && !waitingInfo_ && !replayReset_ && std::chrono::duration<double>(now - lastReplayProgress_).count() > 15) {
        FailRecovery("MATCH RECOVERY TIMED OUT"); return;
    }
    const double interval = CatchingUp() && !waitingInfo_ ? 0.05 : 0.5;
    if (std::chrono::duration<double>(now - lastRecoverySend_).count() < interval) return;
    lastRecoverySend_ = now;
    if (recovery_ == Recovery::Solo) {
        // Refresh our authenticated endpoint too: Wi-Fi changes can change the public address/port in-place.
        if (std::chrono::duration<double>(now - lobbyLastSend_).count() > 2) SendResumeRequest();
        auto p = Header(12); Put32(p, build_); Put32(p, inputEpoch_); SendRelayed(p);
    } else if (CatchingUp()) {
        if (waitingInfo_) { auto p = Header(7); Put32(p, recoveryNonce_); Put32(p, build_); SendRelayed(p); }
        else if (!replayReset_) {
            auto p = Header(9); Put32(p, recoveryNonce_); Put32(p, HistoryEnd()); SendRelayed(p);
            if (replayedEnd_ > config_.startFrame && advertisedEnd_ <= replayedEnd_ + 60) {
                p = Header(11); Put32(p, recoveryNonce_); Put8(p, resumeFrame_ ? 2 : 0);
                Put32(p, resumeFrame_ ? resumeFrame_ : replayedEnd_);
                Put32(p, history_[replayedEnd_ - config_.startFrame - 1].hash); SendRelayed(p);
            }
        }
    } else if (recovery_ == Recovery::Sending && resumeFrame_ && !resumeAcknowledged_) {
        auto p = Header(11); Put32(p, recoveryNonce_); Put8(p, 1); Put32(p, resumeFrame_); Put32(p, 0); SendRelayed(p);
    }
}

}  // namespace port::net
