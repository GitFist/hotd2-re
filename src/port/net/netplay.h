#pragma once

// Two-player online play for play.exe / hotd2.exe (not in the original): deterministic lockstep over UDP.
//
// Both machines run the same World from the same start (the host's WorldConfig: credits, seed, serial, the port's
// options and its save file go over in the welcome) and exchange nothing but inputs: at step S each side applies its
// own input pushed at step S - delay and the other side's input for S, so a step never runs before both inputs are
// known (the side that is ahead waits: "stall"). Every packet carries all local inputs the other side has not
// acknowledged yet, so a lost packet costs nothing unless its replacement arrives after the frame needs it, and a
// running hash of the world (play.cpp WorldHash) rides along so a divergence is reported with its frame.
//
// In-game rooms have two paths to the other player: direct (the endpoints the lobby server saw and the local ones,
// hole-punched with hellos) and the lobby's authenticated relay (both players send to the server, which forwards
// H2NP datagrams unchanged). Hellos go out directly at once and through the relay from a second later; the first
// path a peer packet arrives on is provisional until the welcome and inputs get through. Relay fallback stays
// available during that handshake, including one-way direct connections. A connected path silent for two seconds
// hands over to the other - both sides apply the same rule, so they converge. A relay
// keepalive every five seconds keeps the room alive and the fallback warm while playing directly. The input delay
// is measured: hellos carry a timestamp the other side echoes (pong), and the host sets the delay from the round
// trip before it sends the welcome (SetDelayPolicy). The lockstep remains on the two PCs.
// Connecting: direct IP (the host binds a UDP port, the joiner sends hellos to it), or codes: each side asks a public
// STUN server for its public address (QueryPublicEndpoint) and shows it as a 12-letter code (LocalCode); each enters
// the other's (SetPeerCode); both then send hellos to each other, which opens most home routers for the other side's
// packets (hole punching; symmetric NATs defeat it: the host forwards the port then). The host answers hellos with the
// welcome; it counts the joiner as connected at its first input packet.
//
// Packet (little-endian; one datagram each): "H2NP", type, version 3, then
// Configuration schema changes require a new client build ID; relay transport stays version 3.
//   hello   (both, until connected): build u32
//   welcome (host -> joiner): build u32, credits i32, seed u32, serial u32, volumes 5 x i32, flags u8, delay u8,
//                             start frame u32, save u16 + bytes, host UTC seconds u32
//   input   (both): player u8, delay u8, recovery epoch u32, ack u32 (first missing frame), hash frame u32,
//                   hash u32, first frame u32, count u8, count x record (49 bytes: present u8, command u8, kind i8,
//                   device mode i8, x i16, y i16, fired u8, offscreen u8, held u16, pressed u16,
//                   text length u8, text32, cancel u8, F1..F4 u8)
//   bye     (both): recovery epoch u32
// Authenticated relay-only recovery types 7-12: request, initial config, history request, history chunk,
// ready/plan/ack, and recovery invite. See notes/agents/netplay.md for the state transitions and bounds.
// STUN: RFC 5389 binding request / XOR-MAPPED-ADDRESS.

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "game/world.h"

namespace port::net {

// One player's input for one step, as play.exe gives it to the world (present = SetInput is called at all: a player
// without a device never gets one, which the world tells from a pad player with no buttons).
struct FrameInput {
    bool present = false;
    game::WorldInput in;
    uint8_t command = 0;  // kCmd* bits: port actions outside the world's input (the pause menu's Return to Main Menu)
};
constexpr uint8_t kCmdReturnToTitle = 1;
constexpr uint8_t kCmdResume = 2;

// What the joiner's start takes from the host (the welcome).
struct SessionConfig {
    int32_t credits = -1;  // play.cpp --credits (-1 free play)
    uint32_t seed = 902, firstSerial = 0x10000;
    int32_t portVolumes[5] = { -1, -1, -1, 5, 5 };  // music, guns, effects, master, attraction (shared menu state)
    uint8_t flags = 0;                        // 1 portFeatures, 2 portModernMenu, 4 persistent shared room
    uint8_t delay = 3;                        // input delay in frames (the host decides)
    uint32_t startFrame = 0;                  // the world frame the lockstep starts at (0: from the boot)
    uint32_t startedAt = 0;                   // Host UTC seconds; deterministic Typing scorebook dates.
    std::vector<uint8_t> save;                // the host's save file (rankings, options); empty = none
};

// Codes: 4 bytes IPv4 + 2 bytes port + 1 check byte in a 32-letter alphabet (no I, O, 0, 1), 12 letters; shown in
// groups of four. Decoding ignores case, spaces and dashes. (The console --code mode; the game uses the lobby server.)
std::string EncodeCode(const uint8_t ip[4], uint16_t port);
bool DecodeCode(const std::string& code, uint8_t ip[4], uint16_t* port);

// The lobby server (server/lobby/lobby_server.py): rooms with a 5-digit code and a session relay.
// A distinct 128-bit token authenticates each member; endpoints are learned from outgoing packets to the server.
struct LobbyRoom {
    uint32_t id = 0;
    uint8_t flags = 0;  // Low five bits: kRoom*; high three bits: RoomDifficulty().
    uint32_t build = 0;
    bool typing = false;
};
constexpr uint8_t kRoomPrivate = 1, kRoomUnlimited = 2, kRoomUnlisted = 4, kRoomRelay = 8, kRoomRejoin = 16;
// The existing CREATE/ROOMS flags byte has three spare bits. Encoding 1..5
// advertises Very Easy..Very Hard; zero keeps older clients/rooms unknown.
// Reserved values 6/7 are also unknown, never guessed as a playable level.
constexpr uint8_t kRoomDifficultyMask = 0xE0;
constexpr uint8_t RoomDifficultyFlags(int difficulty) {
    return difficulty >= 0 && difficulty < 5 ? static_cast<uint8_t>((difficulty + 1) << 5) : 0;
}
constexpr int RoomDifficulty(uint8_t flags) {
    const int encoded = (flags & kRoomDifficultyMask) >> 5;
    return encoded >= 1 && encoded <= 5 ? encoded - 1 : -1;
}
// Values 0..4 are the existing wire results; timeouts are local client states.
enum class JoinResult { Pending = -1, Ok = 0, NoRoom = 1, WrongCode = 2, Full = 3, OtherVersion = 4, TimedOut = 5, NoAnswer = 6 };

class Session {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // The socket. Host: the given port (the joiner must reach it); joiner: port 0 = any.
    bool Host(uint16_t port, std::string* error);
    bool Join(const std::string& address, uint16_t port, std::string* error);  // direct IP: the host's address
    bool OpenJoiner(uint16_t port, std::string* error);                        // codes: the socket only, SetPeerCode next
    bool Active() const { return sock_ != kNoSocket; }
    bool IsHost() const { return host_; }
    int LocalPlayer() const { return host_ ? 0 : 1; }
    int RemotePlayer() const { return host_ ? 1 : 0; }

    // Codes. QueryPublicEndpoint asks the STUN servers (blocking, up to ~2 s); with HOTD2_NET_LOCALCODE set the code is
    // 127.0.0.1 and the bound port instead (tests on one machine). LocalCode is empty before a query.
    bool QueryPublicEndpoint(std::string* error);
    const std::string& LocalCode() const { return localCode_; }
    bool SetPeerCode(const std::string& code, std::string* error);
    bool PeerKnown() const { return peerKnown_; }
    enum class Path { None, Direct, Relay };
    bool RelayAvailable() const { return relayRoom_ != 0; }   // the lobby gave relay tokens
    bool UsingRelay() const { return path_ == Path::Relay; }  // the packets go through the server right now
    const char* PathName() const { return path_ == Path::Relay ? "server relay" : path_ == Path::Direct ? "direct connection" : "none"; }
    double RttMs() const { return rttMs_; }                   // the best measured round trip, -1 = none yet
    // The input delay: automatic = ceil(half the round trip in frames) + 1, at least minDelay (default); fixed = the
    // given frames. The host decides and sends it in the welcome.
    void SetDelayPolicy(int minDelay, bool automatic) {
        minDelay_ = minDelay;
        delayAuto_ = automatic;
    }

    // Lobby server. SetLobbyServer resolves the address (false: unresolvable). The Lobby* calls send through the open
    // socket (Host / OpenJoiner first); their replies arrive through Poll(). Requests are repeated every second until
    // answered; LobbySilence() is the time since the server last answered (the UI's "no answer" message).
    bool SetLobbyServer(const std::string& host, uint16_t port, std::string* error);
    bool LobbyConfigured() const { return lobbyKnown_; }
    // Host: a room -> LobbyRoomId / LobbyCode. Supplying a difficulty replaces
    // its flag bits; omitting it preserves encoded bits when CREATE is retried.
    void LobbyCreate(uint8_t flags, int difficulty = -1, bool typing = false);
    bool LobbyCreated() const { return lobbyRoom_ != 0 && lobbyHosting_; }
    uint32_t LobbyRoomId() const { return lobbyRoom_; }
    uint32_t LobbyCode() const { return lobbyCode_; }
    void LobbyRefresh();                                               // host: every loop iteration (keeps the room alive)
    void LobbyLeave();                                                 // host: the room is closed
    // Complete cancellation even when Back preceded CREATED. Retired sockets
    // only receive credentials and repeat LEAVE; they never refresh a room.
    void PollLobbyCleanup();
    bool LobbyCancellationPending() const;  // waiting for a cancelled CREATE's credentials
    bool IsCancelledLobbyRoom(uint32_t room) const;
    void LobbyList();                                                  // joiner: asks for the room list
    const std::vector<LobbyRoom>& LobbyRooms() const { return lobbyRooms_; }
    bool LobbyRoomsKnown() const { return lobbyRoomsKnown_; }
    void LobbyJoin(uint32_t room, uint32_t code);                      // joiner: room by id (0: by code only)
    JoinResult LobbyJoinResult() const { return lobbyJoinResult_; }
    double LobbySilence() const;

    void SetConfig(const SessionConfig& c) { config_ = c; }  // host, before the lobby
    const SessionConfig& Config() const { return config_; }   // joiner: the host's, once Connected()
    int Delay() const { return config_.delay; }
    // The program's build id goes into the hello / welcome: two different builds do not connect (the worlds could
    // differ), BuildMismatch() tells the lobby to say so. 0 = unknown.
    void SetBuild(uint32_t build) { build_ = build; }
    uint32_t RemoteBuild() const { return remoteBuild_; }
    bool BuildMismatch() const { return remoteBuild_ != 0 && remoteBuild_ != build_; }

    // Receive everything that has arrived; the hellos go out from here while not connected. Every loop iteration.
    void Poll();
    bool Connected() const { return connected_; }
    bool PeerLeft() const { return peerLeft_; }
    double SecondsSinceRemote() const;  // silence from the other side (connection trouble)

    // Rejoin keeps the existing player slots. The survivor records applied inputs while playing alone;
    // the returner replays them, verifies every frame, then both resume lockstep at an agreed future frame.
    bool CanRejoin() const { return rejoinCapable_ && RelayAvailable(); }
    void BeginHistory(uint32_t initialHash);
    bool SaveTicket(const std::string& path) const;
    bool LoadTicket(const std::string& path, std::string* error);
    bool HasTicket(const std::string& path) const;
    void Abandon();  // intentional end; Close instead suspends a resumable match
    void ContinueAlone(uint32_t frame);
    bool PlayingAlone() const;
    bool CatchingUp() const;
    bool ReplayPending() const { return replayReset_; }
    void ReplayStarted(uint32_t initialHash);
    bool ReplayFrame(uint32_t frame, FrameInput out[2], uint32_t* hash) const;
    void RecordFrame(uint32_t frame, const FrameInput in[2], uint32_t hash);
    bool RecoveryFailed() const { return recoveryFailed_; }
    const std::string& RecoveryError() const { return recoveryError_; }
    uint32_t HistoryEnd() const { return config_.startFrame + static_cast<uint32_t>(history_.size()); }
    uint32_t RecoveryTarget() const { return advertisedEnd_; }
    bool FinishRecovery(uint32_t frame);

    // Lockstep. frame = the step the input applies to (the current step + Delay()). Steps below start frame + Delay()
    // have no input on either side.
    void PushLocal(uint32_t frame, const FrameInput& in);         // once per step (a repeat is ignored)
    bool LocalInput(uint32_t frame, FrameInput* out) const;       // false: nothing pushed for it
    bool RemoteInput(uint32_t frame, FrameInput* out) const;      // false: not received yet -> stall
    void Send();                                                  // an input packet when due; every loop iteration
    // The world's hash after step frame; compared with the other side's for the same frame.
    void ReportHash(uint32_t frame, uint32_t hash);
    bool Desynced(uint32_t* frame, uint32_t* ours, uint32_t* theirs) const;

    struct Stats {
        uint32_t packetsIn = 0, packetsOut = 0, remoteAck = 0, remoteLatest = 0;
    };
    Stats GetStats() const;
    void Close();  // bye, socket closed
    void Reset();  // Close + everything back to the initial state (a new lobby attempt)

private:
    using Clock = std::chrono::steady_clock;
    static constexpr uintptr_t kNoSocket = ~uintptr_t(0);
    bool Open(uint16_t port, std::string* error);
    void SendRaw(const std::vector<uint8_t>& p);
    void SendHello();
    void SendWelcome();
    void OnPacket(const uint8_t* d, size_t n, const void* from, int fromLen);
    void OnPeerPacket(const uint8_t* d, size_t n, const void* from, int fromLen, bool viaRelay);
    void OnLobbyPacket(const uint8_t* d, size_t n);
    void SetRelay(uint32_t room, const uint8_t* token);
    void SendDirect(const std::vector<uint8_t>& p);   // to peer_ (every candidate while not connected)
    void SendRelayed(const std::vector<uint8_t>& p);  // in the relay envelope to the server
    void SendPong(uint32_t stamp, bool viaRelay);
    void DecideDelay();
    uint32_t NowMs() const;
    void CheckHash(uint32_t frame, uint32_t theirs);
    bool StunOnce(const char* server, uint16_t port, uint8_t ip[4], uint16_t* mapped);
    void SendToLobby(const std::vector<uint8_t>& p);
    void LocalEndpoint(uint8_t ip[4], uint16_t* port) const;
    // The other side's endpoint candidates (public and local, from the lobby server): hellos go to all of them, the
    // first packet back fixes peer_.
    void SetPeerCandidates(const uint8_t pubIp[4], uint16_t pubPort, const uint8_t localIp[4], uint16_t localPort);
    void PollRecovery();
    void OnRecoveryPacket(uint8_t type, const uint8_t* data, size_t size);
    void RequestRecovery();
    void SendResumeRequest(uint8_t type = 7);
    void SendRecoveryInfo();
    void PrepareResume(uint32_t frame);
    void FailRecovery(const char* reason);

    enum class Recovery { Normal, Solo, Sending, Receiving };
    struct HistoryFrame { FrameInput input[2]; uint32_t hash = 0; };
    static constexpr uint32_t kMaxHistory = 60 * 60 * 60 * 4;  // bounded four-hour match
    Recovery recovery_ = Recovery::Normal;
    bool rejoinCapable_ = false, historyStarted_ = false, replayReset_ = false, waitingInfo_ = false;
    bool resumingLobby_ = false, recoveryFailed_ = false, resumeAcknowledged_ = false;
    uint32_t recoveryNonce_ = 0, resumeFrame_ = 0, initialHash_ = 0, advertisedEnd_ = 0, replayedEnd_ = 0;
    uint32_t inputEpoch_ = 0;
    std::vector<uint32_t> completedRecoveries_;
    std::string recoveryError_;
    std::vector<HistoryFrame> history_;
    Clock::time_point lastRecoverySend_, recoveryStarted_, lastHistoryReply_, lastReplayProgress_;

    uintptr_t sock_ = kNoSocket;
    bool host_ = false, connected_ = false, peerLeft_ = false, peerKnown_ = false;
    bool peerInputSeen_ = false;  // handshake traffic alone cannot confirm a usable gameplay path
    uint8_t peer_[32] = {};  // sockaddr_in
    int peerLen_ = 0;
    std::vector<std::array<uint8_t, 16>> candidates_;  // sockaddr_in each, until the first packet fixes peer_
    std::string localCode_;
    // lobby
    bool lobbyKnown_ = false, lobbyHosting_ = false, lobbyRoomsKnown_ = false;
    uint8_t lobbyAddr_[16] = {};
    uint8_t lobbyFlags_ = 0;
    bool lobbyTyping_ = false;
    uint32_t relayRoom_ = 0;
    std::array<uint8_t, 16> relayToken_ = {};
    Path path_ = Path::None;
    bool probing_ = false;  // the current path went silent: everything goes on both paths until one answers
    Clock::time_point helloStart_, lastDirectIn_, lastRelayIn_, lastKeepalive_, firstHelloIn_, connectedAt_;
    bool haveDirectIn_ = false, haveRelayIn_ = false, helloSeen_ = false, welcomeReady_ = false;
    double cutDirectAfter_ = -1.0;  // tests (HOTD2_NET_CUTDIRECT=<s>): the direct path dies that long after connecting
    double rttMs_ = -1.0;
    int pongs_ = 0;
    int minDelay_ = 2;
    bool delayAuto_ = true;
    uint32_t lobbyRoom_ = 0, lobbyCode_ = 0, lobbyJoinRoom_ = 0, lobbyJoinCode_ = 0;
    bool lobbyCreating_ = false, lobbyJoining_ = false;
    bool lobbyLeavePending_ = false;
    uint32_t lobbyLeaveRoom_ = 0, lobbyLeaveCode_ = 0;
    struct LobbyCancellation {
        uintptr_t socket = kNoSocket;
        std::array<uint8_t, 16> server{};
        uint32_t room = 0, code = 0;
        Clock::time_point started{}, lastSend{};
    };
    std::vector<LobbyCancellation> lobbyCancellations_;
    JoinResult lobbyJoinResult_ = JoinResult::Pending;
    std::vector<LobbyRoom> lobbyRooms_;
    Clock::time_point lobbyLastSend_, lobbyLastAnswer_, lobbyJoinStarted_;
    uint32_t build_ = 0, remoteBuild_ = 0;
    SessionConfig config_;
    std::map<uint32_t, FrameInput> local_, remote_;
    uint32_t localLatest_ = 0, remoteNext_ = 0, ackedByRemote_ = 0, remoteLatest_ = 0;
    bool haveLocal_ = false;
    std::map<uint32_t, uint32_t> hashes_;  // ours by frame
    std::vector<std::pair<uint32_t, uint32_t>> pendingTheirs_;
    bool desynced_ = false;
    uint32_t desyncFrame_ = 0, desyncOurs_ = 0, desyncTheirs_ = 0;
    Clock::time_point lastHello_, lastFromRemote_, lastSend_;
    bool sendPending_ = false;
    Stats stats_;
};

}  // namespace port::net
