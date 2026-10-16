# Online two-player (port only: play.exe / hotd2.exe)

Not in the original (its Network entry is a DirectPlay modem / LAN lobby nobody can run). The port's version is
deterministic lockstep: both machines run the same World from the same start and exchange nothing but inputs; a
small lobby server brings the two together. Code: `src/port/net/netplay.{h,cpp}` (lib `port_net`: the session,
the lobby client, codes, STUN), `server/lobby/` (the lobby server, Python, Docker), the integration in
`src/port/tools/play.cpp` (`Lobby` pages, `LobbySprites`, the `netplay` blocks, `WorldHash`), the world's hook in
`src/game/world_modes_net.cpp` (mode 15 state 1 holds with `cfg.portFeatures`) and `WorldHud::netState`. No original
code is involved; nothing for symbols.csv.

## Build I: rejoin while the other player keeps playing (2026-10-08)

Network > **Rejoin Game** appears when this folder has a valid `hotd2re_rejoin.bin` ticket for the current build.
Closing/crashing either client reserves its original player slot. After a goodbye, or 10 seconds without packets (a stage start with the asset pack or a big internal resolution keeps a machine silent for seconds: a stall, not a departure),
the survivor releases the absent player's buttons and keeps stepping with that gun parked off-screen. A running
client can recover automatically; after restarting, use Rejoin Game in the same folder. No room code is needed.
Rejoin needs the server and one surviving client still in the match. Both clients closing, a server restart, or
the match returning to title ends recovery. An absent player can lose lives; rejoin restores the current state.
When both clients play independently during a network partition, P1's history wins and P2 catches up to it.

Implementation:
- New games use `World::StartNetworkGame`: fresh world + consistent boot/save + stage load, independent of which
  menus each client visited. The host snapshots its valid native save or fresh defaults; each process uses its
  own scratch save in the OS temporary folder. The player's normal save is never overwritten by replay.
- Both clients retain actual applied input pairs and the post-step world hash from the start (bounded to four
  hours). The returning client receives that history, replays without rendering each step, and verifies every
  hash, including the initial world. Replay audio is muted. The survivor continues throughout catch-up.
- H2NP v3 adds a recovery epoch to input and goodbye packets. Old epochs cannot supply inputs after rejoining.
  Recovery control/data is accepted only through the authenticated relay, never a direct UDP packet.
- Types 7/8 request/provide initial config (nonce, build, initial hash, current end frame, original SessionConfig).
  Types 9/10 request/return contiguous input history: at most 6 packets of 32 records per 50 ms; each record is
  2 x 14-byte input plus a 4-byte world hash. Missing/out-of-order chunks are requested again. Counts, lengths,
  nonce, build, frame bounds and hashes are checked before application.
- Type 11 ready/plan/ack agrees on a frame 120 steps ahead after a verified catch-up checkpoint. Both capture
  inputs for that frame before reaching it; normal lockstep resumes once those inputs arrive. Completed transfer
  nonces are remembered so delayed recovery requests cannot restart an already completed handover.
- Type 12 invites a live peer to resynchronize after an outage. Its epoch excludes stale invitations. After
  recovery gameplay uses the relay because the peer may have changed public IP/port; player identities do not change.
- Lobby flag 16 negotiates support in JOINED/PEER. RESUME authenticates the private member token/build and rebinds
  its endpoint. END deliberately releases the room. I rooms expire 30 seconds after *both* members stop sending;
  legacy rooms still require both members and close on goodbye. A full room is never joinable with its room code.
- The local ticket stores build/role/room/token/server address only, no game data. It is never packaged. Match
  completion clears it. UI reports progress and failure; failure never silently accepts a divergent state.

Service checks: `python tests/lobby_service_test.py` (14). Actual client tests: `tools/netplay_rejoin_smoke.py`
with `--departed join --relay --loss`, `--departed host --crash`, and `--outage --relay --loss`. Each verifies
continued solo progress, original role, replay/lockstep synchronization and matching final state hashes.
The older sections below document the earlier builds; I ends the session on return to title rather than
keeping both front ends linked after a match.

## How to play (2026-10-07; original lobby flow)

Mode select > Network:
- **Create Room**: Unlimited Continues on / off (off = 3 continues), Private Room on / off, Start. The room shows its
  number and, when private, its 5-digit code; "waiting for a player". Backspace closes it.
- **Browse Lobby**: the server's rooms, newest last, `ROOM n  PUBLIC / PRIVATE  UNLIMITED / 3 CONTINUES` (and
  `OTHER VERSION` for a room of another build, which cannot be joined). A private room asks for its code.
- **Peer to Peer**: Host gets a 5-digit code (an unlisted room: the code is the only way in); Join asks for it.
Up / down, left / right (the options), Enter / Space / trigger confirm, Backspace / reload back; a pointer over a row
selects it. When the two are connected, both machines start the same two-player arcade game from stage 1 (the host =
P1); after it (game over -> title) the two front ends stay in lockstep (the host drives the menus) until one quits.
Both players need the same version (the bottom of the Network screen); different builds do not connect.

Lobby server: `kDefaultLobby` in play.cpp (64.23.186.88:27020, the DigitalOcean VPS; UDP), overridden by
`lobby=host:port` in hotd2re_settings.ini, `HOTD2_LOBBY` or `--lobby`. Deploy: `server/lobby/README.md`.

## 1. Lockstep

- Session frames = world frames. At step S both sides apply their own input pushed at step S - delay and the other
  side's input for S (`Session::PushLocal(S + delay)`, `LocalInput(S)`, `RemoteInput(S)`); a step whose remote input
  has not arrived waits (`netStalled`: the loop keeps receiving and sending; `--run-frames` counts completed steps).
  Steps below start frame + delay have no input on either side. The side that is ahead stalls; the side behind always
  has what it needs, so no deadlock.
- A record is the WorldInput play.cpp would give the world plus `present` and a command byte (`kCmdReturnToTitle`: the
  pause menu's Return to Main Menu, applied by both worlds at the same step). The pause itself is the start button.
- Every packet carries all local inputs from the other side's ack on (newest 48), the ack, the latest frame hash;
  sent once per new local input, else every 20 ms.
- Desync check: `WorldHash` after every step (frame, rand state, game mode, players' state / lives / score, every
  draw's model + matrix + alpha + node); the first mismatch prints "netplay: DESYNC at frame N".
- Start: the in-game lobby StartStage()s both worlds with the host's `SessionConfig` (credits from the room's
  continues option, seed, serial, the Options screen's volume rows, delay, start frame 901, the host's save file ->
  `hotd2re_save_net.bin` on the joiner); the console `--host` / `--join` boot both worlds from frame 0 instead.
- The other side gone (bye, 10 s of silence): the session ends, the local game plays on with the other player's
  character idle. The lobby and a session keep stepping while the window is inactive. On the joiner this machine's
  first device and keyboard are player 2. `--script` drives the local player only.

## 2. Connecting

The following transport description records E; the Build F section below describes the current direct-first client.

- Lobby server (`server/lobby/lobby_server.py`, protocol in its docstring; client `Session::Lobby*`): CREATE /
  REFRESH (every 2 s, the room expires after 8 s without) / LIST / JOIN / LEAVE over the game's own UDP socket, so
  the server sees each client's outgoing endpoint. Build E (`2026100705`) sets room flag 8 for all in-game rooms.
  JOINED appends a 16-byte joiner token; PEER appends room ID and a separate host token. Endpoint fields are zero.
  `SetRelay` pins the peer to the server; `SendRaw` wraps each H2NP datagram in H2LB type 6 with room ID/token.
  The server authenticates token + source endpoint and forwards type 106 using the recipient's token.
  `OnPacket` rejects unwrapped game packets on lobby sockets; only the authenticated envelope reaches
  `OnPeerPacket`. No direct fallback or hole punch occurs. The room stays alive through gameplay instead of
  calling LobbyLeave on game start; goodbye/Close releases it. The server expires a session if either member
  stops sending for 30 seconds. Existing client peer loss detection remains 10 seconds. Legacy D rooms are
  still supported by the server, but cannot mix with E. Console direct IP/STUN modes remain available.
- Build id (`kBuildId`) in the hello / welcome and in the rooms: other builds show as such and refuse.
- Windows: `SIO_UDP_CONNRESET` off and WSAECONNRESET skipped (hellos into a port that is not open yet come back as
  ICMP and would poison recvfrom).
- Console only: direct IP (`--host [port]` / `--join <ip[:port]>`) and the 12-letter STUN codes (`--code`, `--peer`);
  the game uses the lobby.
- `HOTD2_NET_PEERCODE=<5 digits>`: typed into the Code page at once (tests); `HOTD2_NET_LOG=1`: hellos / packets /
  lobby answers on stderr with timestamps; `--pace`: a headless run at 60 Hz (a lobby waits in real time).

## 3. The in-game lobby

World: mode 15 state 1 holds with `cfg.portFeatures` (background and title only; back -> the mode select) and
`WorldHud::netState` reports it. play.cpp (`Lobby`, pages kMenu / kCreate / kWaiting / kBrowse / kCode / kP2P /
kConnecting / kError) runs after the input loop on in[0]'s pressed bits (keyboard, pad, device; the pointer's row),
swallows back except on the Menu page, and on `Connected()` StartStage()s and switches to lockstep. Errors: no answer
from the server (6 s), no such room / wrong code / room full / other version (the join's answer), the server
connection failing (30 s), the other build. `LobbySprites`: rows at cell x 21 from y 5 (pitch 1.25), the Browse list at x 9
from y 3.5, notes centred at y 16.5 / 17.5, the version at y 19 (flags 0x700, the default depth).

## Checks

- Relay E (2026-10-07): `python tests/lobby_service_test.py` (9 checks) and
  `python tools/netplay_relay_smoke.py public private p2p` (both native clients reach frame 3000, no desync,
  first hellos/welcome and ~1/31 input packets deliberately dropped). Test files are written to temporary folders.
  Two copies of the complete E package also reached frame 6000 through `64.23.186.88:27020`, matching simulation
  state with no reported desync. Package and verification records: `build/dist-relay-20261007/`.
  The VPS runs `hotd2-lobby:relay-20261007e1`; MHFU was not restarted. No separate-household test of E yet.

- Flows over loopback with a local server (`server/lobby/lobby_server.py --port 27021`): scratchpad
  `lobbyflow_test.py public|private|wrongcode|p2p` = a windowed host (`build\script_lobby_host.txt` /
  `_host_private.txt` / `_p2p_host.txt`) and a paced headless joiner (`build\script_lobby_join_browse.txt` /
  `_p2p_join.txt`, `--port 27016`, the code from HOTD2_NET_PEERCODE read off the host's "netplay: room N code C"
  line). 2026-10-07: public, private and peer-to-peer all connect, StartStage at 901, both players playing to the end
  of the run; wrong code -> the join's WrongCode answer and the Error page.
- Lockstep over loopback (`--host` / `--join 127.0.0.1`, the 2P auto-play script): 6000 frames in sync, 1 stall.
- Two real machines (2026-10-07, the STUN-code version): connected across two home networks; the input bugs found
  there (the joiner's device, dropped keystrokes, the lobby freezing when inactive) are fixed.

## Not handled yet

Continuous adjustment of input delay after connection (F measures it before the welcome); rollback; reconnection;
spectators; the Network screen while already connected (the lobby does
not reopen); the joiner cannot drive the menus between games; room names (rooms show their number).

## Build F (2026-10-07): direct first, relay as fallback; measured input delay

The current client is build H; see the scenery and input corrections below. The F transport/server remain compatible.

- The lobby's JOINED / PEER carry both the other side's endpoints (public and local) and, for a relay room, the
  relay token (server: `peer_packet`, JOIN; `GAME_TYPES` now 1..6). The session hellos the endpoints at once and the
  relay from a second later (`SendRaw`, `Path::None`); the first path a peer packet arrives on is the session's path
  (`OnPeerPacket`), direct when it works. Connected, a keepalive goes on the other path every 5 s (keeps the relay
  room alive and the NAT mapping warm); when the current path is silent for 2 s the session sends on both paths
  (`probing_`) and the next packet's path wins - both sides apply the same rule, so they converge. Test hooks:
  `HOTD2_NET_NODIRECT=1` (relay only), `HOTD2_NET_NORELAY=1` (direct only), `HOTD2_NET_CUTDIRECT=<s>` (this side's
  direct path dies that long after connecting). The room is left alone at the game start when a relay token exists
  (keepalives keep it); an F client against the E server gets no endpoints and plays through the relay.
- Input delay: hellos carry a millisecond stamp, the other side echoes it (kPong 5); the host takes the best of a
  few pongs (3, or 0.75 s after the first hello; at most 1.5 s without any) and sets delay = ceil(half the round
  trip in frames) + 1, at least 2, at most 10 (`DecideDelay`), before the welcome. `--net-delay N` pins it instead.
  The connect line prints the path and the round trip.
- Checks (`lobbyflow_test.py` with the path argument): public with both paths -> direct, 15 ms, delay 2; relay only
  -> relay, delay 2; direct only; Peer to Peer; wrong code refused; `cutdirect` -> both sides "probing both paths"
  2 s after the cut, "switched to: server relay", the game to the end of the run, no desync. Service tests 9/9.
- F deployment and complete package: `server/lobby/README.md` records `hybrid-20261007f1` on the VPS and the
  complete `build/dist-f/hotd2re-multiplayer-complete.zip`. `tools/verify_complete_dist.py` verifies the package
  hashes and tests the native clients against the default public service, including relay fallback after a
  direct-path cut 40 seconds into play. Full results are in `build/dist-f/verification.json`.


## Build G (2026-10-07): restore stage scenery on lobby start

The title screen overwrites the stage section table. The in-game lobby called the public
`World::StartStage` without the `LoadStageTable` step used by `GameStartFrame`, leaving title
geometry in the gameplay world. Later street sections therefore had no buildings or ground;
enemies, props, rain and the sky still worked. F's synchronized-client tests missed this because
both peers had the same broken scenery.

The public stage-start entry now reloads the requested event and section tables, explicitly
using the new game's type, before creating its tasks. Missing stage data is reported as an
error. The internal transitions used by the exactness tests keep their existing loading order.
Both players need G (build ID 2026100707); the deployed F lobby/relay needs no update.

Validation: `lobby_stage_test game` reproduces the pre-fix title-table leak and checks the opening
through frame 3000 against a fresh stage, including a second start and a preloaded different chapter.
Arcade and Original mode pass (1917 and 2096 scenery frames). `frontend_test` and `world_test` with
`scene_capture_play1full.txt`, `script_p1_start.txt`, and `--to 3000` both match the original,
including all 250207 compared draw matrices. The ZIP verifier also requires actual stage-model
submissions at gameplay frame 3000 for direct and relay sessions; synchronization alone no longer
passes that check. Run `python tools/verify_complete_dist.py build/dist-g/hotd2re-multiplayer-complete.zip --cases direct relay`
and inspect both peers' screenshots; the results record is beside the ZIP.

The complete G ZIP passed both cases against the deployed VPS: two local clients reached gameplay
frame 3000 without desync, with two stage-section models on each side. All four screenshots were
visually checked: buildings and ground render correctly. ZIP CRCs and 2047 file hashes pass.
`build/dist-g/hotd2re-build-g-update.zip` (2,060,914 bytes) contains the identical tested executable
for updating existing installations without replacing settings/saves or downloading the assets again.
Separate-household G testing remains a user playtest; these checks used two instances on this PC.


## Build H: remote crosshairs and player identity after disconnect

The remote inputs reached `World::SetInput`, but never updated play.cpp's raw input array used by
`MakeHudState`. The remote crosshair therefore stayed at its last menu/local position even while
the peer was aiming and shooting elsewhere. H updates that display input from each applied remote
record, retaining immediate local cursor feedback. This also covers the pause/display overlays.

Device ownership now outlives the network connection. The joiner keeps device 0 and the keyboard
on P2 if the host leaves; the host keeps P1 if the joiner leaves. The departed gun's held buttons
are cleared and its cursor parked off-screen. Ordinary local device assignment returns upon
reaching the title. Pause/menu device lookup uses the same ownership mapping.

`python tools/netplay_input_smoke.py` launches isolated temporary client pairs with a private local
lobby service. The direct and forced-relay cases check both actual crosshair sprite positions at
frames 2800 and 3000, with different aim positions for each player at each checkpoint. Separate
host-leaves and join-leaves cases exit one client at 2800, verify the survivor's continued movement
at 3000, its keyboard/device mapping, and that the departed crosshair is off-screen. All four cases
pass with no desync. Screenshots were also visually checked for moving red/blue cursors and the
surviving blue P2 cursor after the host exits. `HOTD2_PLAY_AIMLOG=1` enables the headless sprite log.
Build ID 2026100708 / Network label 2026 10 07 H; both players need H. No server changes.


## Build J: shared mode-selection rooms and Escape pause

Build 2026 10 08 J (ID 2026100802) keeps a connected pair in a shared room until the host
chooses Arcade, Original, Scene Selection, or Boss Mode. The chapter and boss selectors use
P1's controls; Original retains each player's item selection. Return to Lobby from either
pause menu and native selector Back actions bring both worlds back to the same room.
Automatic title/main-menu transitions also return to the room. Leave Room ends the session.

The room lives in deterministic world state, enabled by SessionConfig.flags bit 4 and
WorldConfig.portNetworkRoom. Its input history and frame counter continue through mode
changes, so build I recovery can replay lobby choices and multiple games without a new
ticket or handshake. No server wire-format change or redeployment is required. Both clients
must use J; older clients are refused by the existing build check.

Keyboard Escape is separate from Enter: it pauses/resumes gameplay or backs out of a selector,
and no longer quits the application. Enter confirms, or starts/continues an inactive player.
The window close button and main-menu Exit still quit. A synchronized resume command lets
either player resume, including when the original pauser has disconnected. Pause menus say
Return to Lobby in an online room and confirm that both players will return.

network_room_test covers host authority, idle-room behavior, all four modes, Chapter 3 launch,
boss launch, another Arcade game, player slots, pause/return, visible menu texture residency,
and 1300 frames of deterministic replay across room/game transitions. The native selection
tables have only P1/P2 entries: online chapter/boss navigation explicitly uses P1 rather than
indexing past them when both players are active. Original's Back accepts either player.
The room's continue setting survives persistent-record saves.

tools/netplay_room_smoke.py runs actual clients with scripted keyboard edges, screenshots,
and equal final world hashes. HOTD2_PLAY_KEYS is its test-only frame/key-mask input file.
The direct run verified all modes, P2's inability to choose, pausing from P2, resuming from the
other client, Enter confirmation, Return to Lobby, and a second Arcade launch. Final server,
rejoin and package verification records are saved alongside the build output.

Final client verification used the deployed I server at 64.23.186.88 with relay forced: P2 was
crashed after the second lobby visit while P1 kept playing, then rejoined during Boss Mode.
Both reached the next Arcade game at frame 4950 with world hash 9251EF75. Lobby text pixels
and identical room images after separate games were checked, including after rejoining.
The capture verifier permits skipped screenshot frames during fast catch-up; world history
still verifies every replayed frame. No desync occurred. Server tests: 14/14; pause checks:
59/59; single-player original comparison: all 250207 draw matrices match through frame 3000.
All network tests used two clients on this PC, including those relayed through the VPS.

## Build K: chapter results

The deterministic world now owns per-stage player statistics and a results page. Either player's
fresh confirmation after the three-second minimum advances both worlds together. All result
fields are hashed and restored by input-history replay; the three-second local render gate only
filters newly captured input. Build ID 2026100803 refuses mixed J/K sessions. No server change.
See stage-results.md for reward preservation, input rules, original-asset rendering and tests.
