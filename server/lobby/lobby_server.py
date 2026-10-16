#!/usr/bin/env python3
"""hotd2-re lobby server: rooms for the port's online two-player (src/port/net/netplay.h, notes/agents/netplay.md).

Room discovery and an authenticated two-player UDP relay. Clients try direct gameplay with relay fallback.
Build I adds flag 16: retain a joined room while either member sends authenticated traffic, and allow a
returning member to rebind its endpoint using its private token. Waiting rooms expire after 8 seconds;
legacy relay rooms require both members within 30 seconds; rejoin rooms require at least one.

Run: python3 lobby_server.py [--port 27020] [--bind 0.0.0.0]   (or the Dockerfile next to it; LOBBY_PORT)

Protocol (little-endian; every datagram starts with "H2LB", version 1, type):
  client -> server
    1 CREATE  build u32, flags u8 (1 private, 2 unlimited continues, 4 unlisted = peer to peer), local ip 4, local port u16
    2 REFRESH room u32, code u32                     (every ~2 s while hosting; answered with OK or PEER)
    3 LIST    build u32
    4 JOIN    room u32 (0 = by code only), code u32, build u32, local ip 4, local port u16
    5 LEAVE   room u32, code u32
    6 RELAY   room u32, sender token 16 bytes, H2NP game datagram (up to 60,042 bytes)
    7 RESUME  room u32, member token 16 bytes, build u32, local ip 4, local port u16
    8 END     same as RESUME; deliberately end a resumable match (either member)
  server -> client
    101 CREATED room u32, code u32
    102 ROOMS   count u8, then per room: room u32, flags u8, build u32
    103 JOINED  room u32, result u8 (0 ok, 1 no such room, 2 wrong code, 3 full, 4 other version),
                host public ip 4, port u16, host local ip 4, port u16
    104 PEER    (to the host) joiner public ip 4, port u16, joiner local ip 4, port u16
    105 OK      (refresh acknowledged, nobody joined yet)
    106 RELAYED room u32, recipient token 16 bytes, H2NP game datagram
    107 RESUMED room u32, result u8 (0 ok, 1 unavailable, 4 other build), role u8; token 16 only on success
  Relay rooms append the joiner's token to JOINED; PEER appends room u32 and the host's token.
  Rejoin rooms append one capability byte (16) after these tokens; old clients ignore the extra byte.
  They use H2NP version 3, types 1-12; legacy relay rooms still use version 2, types 1-6.
"""

import argparse
import logging
import secrets
import socket
import struct
import time

MAGIC = b"H2LB"
VERSION = 1
ROOM_TTL = 8.0
RELAY_TTL = 30.0
MAX_GAME_PACKET = 60064  # welcome/rejoin metadata include up to 60,000 save bytes
RELAY_PACKETS_PER_SECOND = 240.0
RELAY_PACKET_BURST = 480.0
RELAY_BYTES_PER_SECOND = 256 * 1024.0
RELAY_BYTE_BURST = 512 * 1024.0
MAX_ROOMS_LISTED = 24
MAX_ROOMS = 512
MAX_ROOMS_PER_IP = 16
MAX_TRACKED_IPS = 2048
PACKETS_PER_SECOND = 20.0
PACKET_BURST = 40.0

F_PRIVATE, F_UNLIMITED, F_UNLISTED, F_RELAY, F_REJOIN = 1, 2, 4, 8, 16
# the game protocol the relay forwards (netplay.cpp kVersion and its types: hello, welcome, input, bye, pong, keepalive)
GAME_VERSION = 2
GAME_TYPES = (1, 2, 3, 4, 5, 6)
REJOIN_GAME_TYPES = tuple(range(1, 13))


class Room:
    def __init__(self, room_id, code, build, flags, host_public, host_local):
        self.id = room_id
        self.code = code
        self.build = build
        self.flags = flags
        self.host_public = host_public  # (ip str, port)
        self.host_local = host_local
        self.joiner_public = None
        self.joiner_local = None
        self.last_refresh = time.monotonic()
        self.tokens = (secrets.token_bytes(16), secrets.token_bytes(16))
        self.relay_budget = [(RELAY_PACKET_BURST, RELAY_BYTE_BURST, self.last_refresh) for _ in range(2)]
        self.last_activity = [self.last_refresh, self.last_refresh]
        self.relay_packets = [0, 0]
        self.relay_bytes = [0, 0]


def pack_endpoint(ep):
    ip, port = ep if ep else ("0.0.0.0", 0)
    return socket.inet_aton(ip) + struct.pack("<H", port)


def unpack_endpoint(data, off):
    return socket.inet_ntoa(data[off:off + 4]), struct.unpack_from("<H", data, off + 4)[0]


class Server:
    def __init__(self, bind, port):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((bind, port))
        self.sock.settimeout(1.0)
        self.rooms = {}
        self.next_id = 1
        self.rate_limits = {}
        self.log = logging.getLogger("lobby")

    def header(self, t):
        return MAGIC + bytes([VERSION, t])

    def send(self, data, addr):
        self.sock.sendto(data, addr)

    def new_code(self):
        used = {r.code for r in self.rooms.values()}
        while True:
            c = 10000 + secrets.randbelow(90000)
            if c not in used:
                return c

    def expire(self):
        now = time.monotonic()
        dead = [k for k, r in self.rooms.items()
                if (now - (max(r.last_activity) if r.flags & F_REJOIN else min(r.last_activity)) > RELAY_TTL if r.flags & F_RELAY and r.joiner_public
                    else now - r.last_refresh > ROOM_TTL)]
        for k in dead:
            self.log.info("room %d expired (relayed packets %s, bytes %s)", k,
                          self.rooms[k].relay_packets, self.rooms[k].relay_bytes)
            del self.rooms[k]

    def allow_packet(self, ip):
        now = time.monotonic()
        if ip not in self.rate_limits:
            if len(self.rate_limits) >= MAX_TRACKED_IPS:
                self.rate_limits = {k: v for k, v in self.rate_limits.items() if now - v[1] < 60.0}
            if len(self.rate_limits) >= MAX_TRACKED_IPS:
                return False
            self.rate_limits[ip] = (PACKET_BURST, now)
        tokens, previous = self.rate_limits[ip]
        tokens = min(PACKET_BURST, tokens + (now - previous) * PACKETS_PER_SECOND)
        allowed = tokens >= 1.0
        self.rate_limits[ip] = (tokens - 1.0 if allowed else tokens, now)
        return allowed

    def peer_packet(self, room):
        # the joiner's endpoints (the direct path) and, for a relay room, the room and the host's token (the fallback)
        out = self.header(104) + pack_endpoint(room.joiner_public) + pack_endpoint(room.joiner_local)
        if room.flags & F_RELAY:
            out += struct.pack("<I", room.id) + room.tokens[0]
            if room.flags & F_REJOIN:
                out += bytes([F_REJOIN])  # capability acknowledgement; old clients ignore it
        return out

    def relay(self, body, addr):
        if not 26 <= len(body) <= 20 + MAX_GAME_PACKET:
            return
        room_id = struct.unpack_from("<I", body)[0]
        room = self.rooms.get(room_id)
        if not room or not room.flags & F_RELAY or room.joiner_public is None:
            return
        member = 0 if addr == room.host_public else 1 if addr == room.joiner_public else -1
        if member < 0 or not secrets.compare_digest(body[4:20], room.tokens[member]):
            return
        packet = body[20:]
        allowed_types = REJOIN_GAME_TYPES if room.flags & F_REJOIN else GAME_TYPES
        version = 3 if room.flags & F_REJOIN else GAME_VERSION
        if packet[:4] != b"H2NP" or packet[5] != version or packet[4] not in allowed_types:
            return
        now = time.monotonic()
        if now - (max(room.last_activity) if room.flags & F_REJOIN else min(room.last_activity)) > RELAY_TTL:
            self.expire()
            return
        packets, byte_budget, previous = room.relay_budget[member]
        packets = min(RELAY_PACKET_BURST, packets + (now - previous) * RELAY_PACKETS_PER_SECOND)
        byte_budget = min(RELAY_BYTE_BURST, byte_budget + (now - previous) * RELAY_BYTES_PER_SECOND)
        allowed = packets >= 1 and byte_budget >= len(body)
        room.relay_budget[member] = (packets - int(allowed), byte_budget - len(body) * int(allowed), now)
        if not allowed:
            return
        room.last_activity[member] = now
        target = room.joiner_public if member == 0 else room.host_public
        out = self.header(106) + struct.pack("<I", room.id) + room.tokens[1 - member] + packet
        self.send(out, target)
        room.relay_packets[member] += 1
        room.relay_bytes[member] += len(packet)
        if packet[4] == 4:  # repeat the goodbye; a rejoin room stays while either player is alive
            self.send(out, target)
            self.send(out, target)
            if not room.flags & F_REJOIN:
                self.log.info("room %d relay closed (packets %s, bytes %s)", room.id, room.relay_packets, room.relay_bytes)
                del self.rooms[room.id]

    def handle(self, data, addr):
        if len(data) < 6 or data[:4] != MAGIC or data[4] != VERSION:
            return
        if data[5] == 6:
            self.relay(data[6:], addr)  # separate authenticated budget: gameplay exceeds 20 packets/s
            return
        if not self.allow_packet(addr[0]):
            return
        # Do not depend on an idle socket or room-list requests for expiration.
        self.expire()
        t = data[5]
        body = data[6:]
        if t in (7, 8) and len(body) == 30:  # RESUME / END: room, token, build, local endpoint
            room_id = struct.unpack_from("<I", body)[0]
            room = self.rooms.get(room_id)
            member = -1
            if room and room.flags & F_REJOIN and room.joiner_public:
                for i in range(2):
                    if secrets.compare_digest(body[4:20], room.tokens[i]):
                        member = i
            # Never reveal a room's token or another player's endpoints to an invalid ticket.
            if member < 0:
                self.send(self.header(107) + struct.pack("<IBB", room_id, 1, 0), addr)
                return
            if struct.unpack_from("<I", body, 20)[0] != room.build:
                self.send(self.header(107) + struct.pack("<IBB", room_id, 4, member), addr)
                return
            if t == 8:
                del self.rooms[room_id]
                return
            local = unpack_endpoint(body, 24)
            if member == 0:
                room.host_public, room.host_local = addr, local
            else:
                room.joiner_public, room.joiner_local = addr, local
            room.last_activity[member] = time.monotonic()
            self.send(self.header(107) + struct.pack("<IBB", room_id, 0, member) + room.tokens[member], addr)
        elif t == 1 and len(body) >= 11:  # CREATE
            build, flags = struct.unpack_from("<IB", body, 0)
            local = unpack_endpoint(body, 5)
            # one room per host endpoint: a repeated CREATE (a lost reply) returns the same room
            for r in self.rooms.values():
                if r.host_public == addr and (r.joiner_public is None or r.flags & F_RELAY):
                    r.last_refresh = time.monotonic()
                    self.send(self.header(101) + struct.pack("<II", r.id, r.code), addr)
                    if r.joiner_public:
                        self.send(self.peer_packet(r), addr)
                    return
            if len(self.rooms) >= MAX_ROOMS or sum(r.host_public[0] == addr[0] for r in self.rooms.values()) >= MAX_ROOMS_PER_IP:
                return
            room = Room(self.next_id, self.new_code(), build, flags, addr, local)
            self.next_id += 1
            self.rooms[room.id] = room
            self.log.info("room %d created (flags %d, build %d)", room.id, flags, build)
            self.send(self.header(101) + struct.pack("<II", room.id, room.code), addr)
        elif t == 2 and len(body) >= 8:  # REFRESH
            room_id, code = struct.unpack_from("<II", body, 0)
            room = self.rooms.get(room_id)
            if not room or room.code != code or room.host_public != addr:
                return
            room.last_refresh = time.monotonic()
            if room.joiner_public:
                self.send(self.peer_packet(room), addr)
            else:
                self.send(self.header(105), addr)
        elif t == 3 and len(body) >= 4:  # LIST
            self.expire()
            out = self.header(102)
            rooms = [r for r in self.rooms.values() if not (r.flags & F_UNLISTED) and r.joiner_public is None]
            rooms.sort(key=lambda r: r.id)
            rooms = rooms[:MAX_ROOMS_LISTED]
            out += bytes([len(rooms)])
            for r in rooms:
                out += struct.pack("<IBI", r.id, r.flags, r.build)
            self.send(out, addr)
        elif t == 4 and len(body) >= 18:  # JOIN
            room_id, code, build = struct.unpack_from("<III", body, 0)
            local = unpack_endpoint(body, 12)
            self.expire()
            room = None
            if room_id:
                room = self.rooms.get(room_id)
            else:
                for r in self.rooms.values():
                    if r.code == code:
                        room = r
            result = 0
            if not room:
                result = 1
            elif (room.flags & (F_PRIVATE | F_UNLISTED)) and room.code != code:
                result = 2
            elif room.host_public == addr or (room.joiner_public is not None and room.joiner_public != addr):
                result = 3
            elif room.build != build:
                result = 4
            out = self.header(103) + struct.pack("<IB", room.id if room else 0, result)
            if result == 0:
                if room.joiner_public is None:
                    room.last_activity = [time.monotonic(), time.monotonic()]
                    self.log.info("room %d joined (%s)", room.id, "relay" if room.flags & F_RELAY else "direct")
                room.joiner_public = addr
                room.joiner_local = local
                out += pack_endpoint(room.host_public) + pack_endpoint(room.host_local)  # the direct path
                if room.flags & F_RELAY:
                    out += room.tokens[1]  # and the fallback through this server
                    if room.flags & F_REJOIN:
                        out += bytes([F_REJOIN])
                self.send(self.peer_packet(room), room.host_public)  # the host hears at once (and at its next refresh)
            else:
                out += pack_endpoint(None) + pack_endpoint(None)
            self.send(out, addr)
        elif t == 5 and len(body) >= 8:  # LEAVE
            room_id, code = struct.unpack_from("<II", body, 0)
            room = self.rooms.get(room_id)
            if room and room.code == code and room.host_public == addr:
                self.log.info("room %d closed", room_id)
                del self.rooms[room_id]

    def run(self):
        self.log.info("listening on %s", self.sock.getsockname())
        while True:
            try:
                data, addr = self.sock.recvfrom(65536)
            except socket.timeout:
                self.expire()
                continue
            except ConnectionResetError:
                continue
            try:
                self.handle(data, addr)
            except Exception:  # a bad packet never stops the service
                self.log.exception("bad packet from %s", addr)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=27020)
    ap.add_argument("--bind", default="0.0.0.0")
    a = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    Server(a.bind, a.port).run()


if __name__ == "__main__":
    main()
