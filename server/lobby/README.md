# hotd2-re lobby server

Room discovery and a two-player gameplay relay on one UDP port. Build `2026 10 08 I` tries direct gameplay
first and uses the VPS relay automatically when needed, for all three in-game room options. Players do not
need to configure router port forwarding. The lobby is needed to find a player; the relay must stay available
when carrying the game or providing its backup path. The deterministic simulation runs on the PCs. Input
delay is measured at connection time, and an unresponsive path triggers attempts on both paths after 2 s.
Protocol: `lobby_server.py` docstring; client: `src/port/net/netplay.{h,cpp}`. I adds authenticated rejoining while the remaining
player keeps playing. Recovery transfers the surviving client's input history through the relay, verifies it,
and restores lockstep and the original P1/P2 slots. It does not save running worlds on the server.

## Deploy (Docker)

    cd server/lobby
    docker compose -p hotd2-lobby up --build -d
    docker compose -p hotd2-lobby ps

Compose publishes UDP 27020 by default. Set `LOBBY_PORT` to change the external port; the container still listens
on 27020. The host/provider firewall must allow the selected UDP port. Logs: `docker logs hotd2-lobby`.
The container runs without root privileges, with a read-only filesystem, bounded logs, a 96 MiB memory limit,
half a CPU, and a UDP health check. Use a dedicated Compose project for this service.

The service caps rooms at 512 overall / 16 per source IP and limits lobby request bursts. Rooms expire during
active traffic as well as idle periods. Only the registered host endpoint can refresh or close a waiting room.
Room codes and relay tokens are not logged. Waiting rooms expire after 8 seconds; matched relay rooms require
authenticated traffic from both players within 30 seconds in legacy rooms. I's rooms (flag 16) stay alive while
either player sends authenticated traffic within 30 seconds. Each member has a random 128-bit token bound to its
source endpoint. Only that room's other member can receive its game packets; arbitrary destinations and nested
relay requests are rejected. Per-member gameplay budgets allow 240 packets/s and 256 KiB/s, with bounded bursts,
separate from the 20/s per-IP lobby budget. The 60,064-byte maximum accommodates welcome/rejoin save metadata.
On goodbye the server forwards three copies; legacy rooms close, while I rooms reserve the departed slot.
RESUME (type 7) authenticates the member token and build before changing its endpoint; a room code cannot take
over a reserved slot. END (type 8, same authenticated body) closes the match deliberately. Recovery packets are
allowed only for rejoin rooms using H2NP version 3; older rooms keep H2NP version 2. Close/expiry logs include counts.
Legacy D clients retain their old direct room transport, and different game builds cannot join each other.

## Clients

The game's default lobby address is in `src/port/tools/play.cpp` (`kDefaultLobby`); players override it with
`lobby=host:port` in `hotd2re_settings.ini`, the `HOTD2_LOBBY` environment variable or `--lobby host:port`.

## Local test

    python3 server/lobby/lobby_server.py --port 27020
    HOTD2_LOBBY=127.0.0.1:27020 ... (see notes/agents/netplay.md)

Focused service checks from the repository root: `python tests/lobby_service_test.py`.
Native game checks with deliberate handshake/input packet loss: `python tools/netplay_relay_smoke.py`.
Rejoin checks: `python tools/netplay_rejoin_smoke.py --departed join --relay --loss`,
`--departed host --crash`, and `--outage --relay --loss`. Each uses isolated clients and a local server.


Restarting this service loses its in-memory rooms and rejoin tokens. Deploy updates between matches.
