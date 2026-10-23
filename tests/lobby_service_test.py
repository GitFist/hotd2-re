"""Focused checks for the deployed lobby's room lifecycle and request limits."""
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("hotd2_lobby", Path(__file__).resolve().parents[1] / "server/lobby/lobby_server.py")
lobby = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lobby)


class LobbyServiceTest(unittest.TestCase):
    def setUp(self):
        self.server = lobby.Server("127.0.0.1", 0)
        self.sent = []
        self.server.send = lambda data, address: self.sent.append((data, address))
        self.host = ("127.0.0.1", 30001)
        self.guest = ("127.0.0.1", 30002)

    def tearDown(self):
        self.server.sock.close()

    def request(self, kind, body, address):
        self.server.handle(self.server.header(kind) + body, address)

    def create(self, address=None, flags=0, build=2026100704):
        address = address or self.host
        self.request(1, struct.pack("<IB", build, flags) + lobby.pack_endpoint(address), address)
        return next(r for r in self.server.rooms.values() if r.host_public == address)

    def test_private_join_and_owner_controls(self):
        room = self.create(flags=lobby.F_PRIVATE)
        def join(code, build=2026100704):
            self.request(4, struct.pack("<III", room.id, code, build) + lobby.pack_endpoint(self.guest), self.guest)
            return self.sent[-1][0][10]
        self.assertEqual(join(room.code + 1), 2)
        self.assertEqual(join(room.code, 1), 4)
        body = struct.pack("<II", room.id, room.code)
        last_refresh = room.last_refresh
        self.request(2, body, self.guest)
        self.request(5, body, self.guest)
        self.assertIn(room.id, self.server.rooms)
        self.assertEqual(room.host_public, self.host)
        self.assertEqual(room.last_refresh, last_refresh)
        self.assertEqual(join(room.code), 0)
        self.assertEqual(self.sent[-2][0][5], 104)
        self.request(2, body, self.host)
        self.assertEqual(self.sent[-1][0][5], 104)
        self.request(5, body, self.host)
        self.assertNotIn(room.id, self.server.rooms)

    def test_expiration_during_create_traffic(self):
        old = self.create()
        old.last_refresh -= lobby.ROOM_TTL + 1
        self.create(self.guest)
        self.assertNotIn(old.id, self.server.rooms)

    def test_room_caps_and_retransmission(self):
        with patch.object(lobby, "MAX_ROOMS", 2), patch.object(lobby, "MAX_ROOMS_PER_IP", 1):
            first = self.create()
            self.assertEqual(self.create().id, first.id)
            self.request(1, struct.pack("<IB", 1, 0) + lobby.pack_endpoint(self.guest), self.guest)
            self.assertEqual(len(self.server.rooms), 1)
            self.create(("127.0.0.2", 30003))
            third = ("127.0.0.3", 30004)
            self.request(1, struct.pack("<IB", 1, 0) + lobby.pack_endpoint(third), third)
            self.assertEqual(len(self.server.rooms), 2)

    def test_packet_budget_is_bounded(self):
        with patch.object(lobby.time, "monotonic", return_value=100.0):
            for _ in range(int(lobby.PACKET_BURST)):
                self.assertTrue(self.server.allow_packet("127.0.0.1"))
            self.assertFalse(self.server.allow_packet("127.0.0.1"))
        with patch.object(lobby.time, "monotonic", return_value=101.0):
            self.assertTrue(self.server.allow_packet("127.0.0.1"))

    def test_difficulty_metadata_survives_create_retry_and_list(self):
        # The client encodes difficulty + 1 in the high three flags bits. Zero
        # means an older host did not advertise a difficulty. The existing server
        # must preserve the byte without changing packet layout or room policy.
        for difficulty_code in range(6):
            for policy in (0, lobby.F_PRIVATE, lobby.F_UNLIMITED, lobby.F_UNLISTED,
                           lobby.F_RELAY | lobby.F_REJOIN):
                with self.subTest(difficulty_code=difficulty_code, policy=policy):
                    self.server.rooms.clear()
                    self.server.rate_limits.clear()
                    self.sent.clear()
                    flags = policy | (difficulty_code << 5)
                    room = self.create(flags=flags)
                    original_code = room.code
                    self.assertEqual(room.flags, flags)
                    self.assertIs(self.create(flags=flags), room)
                    self.assertEqual(len(self.server.rooms), 1)
                    self.assertEqual(room.code, original_code)
                    self.assertEqual(room.flags, flags)
                    self.assertEqual(self.sent[-1][0], self.server.header(101) +
                                     struct.pack('<II', room.id, original_code))
                    self.request(2, struct.pack('<II', room.id, room.code), self.host)
                    self.assertEqual(self.sent[-1][0], self.server.header(105))
                    self.request(3, struct.pack('<I', room.build), self.guest)
                    listed = self.sent[-1][0]
                    expected = self.server.header(102)
                    if policy & lobby.F_UNLISTED:
                        expected += b'\0'
                    else:
                        expected += b'\1' + struct.pack('<IBI', room.id, flags, room.build)
                    self.assertEqual(listed, expected)
                    self.assertIsNone(room.joiner_public, 'reading metadata must not join a room')

    def test_difficulty_metadata_does_not_change_private_join_or_relay_rules(self):
        for difficulty_code in range(6):
            with self.subTest(difficulty_code=difficulty_code):
                self.server.rooms.clear()
                self.server.rate_limits.clear()
                self.sent.clear()
                flags = lobby.F_PRIVATE | lobby.F_RELAY | lobby.F_REJOIN | (difficulty_code << 5)
                room = self.create(flags=flags)

                def join(code, build):
                    self.request(4, struct.pack('<III', room.id, code, build) +
                                 lobby.pack_endpoint(self.guest), self.guest)
                    return self.sent[-1][0]

                self.assertEqual(join(room.code + 1, room.build)[10], 2)
                self.assertEqual(join(room.code, room.build + 1)[10], 4)
                self.assertIsNone(room.joiner_public)
                accepted = join(room.code, room.build)
                self.assertEqual(accepted[10], 0)
                self.assertEqual(accepted[23:39], room.tokens[1])
                self.assertEqual(accepted[39:], bytes([lobby.F_REJOIN]))
                self.assertEqual(self.sent[-2][0][22:38], room.tokens[0])
                self.assertEqual(self.sent[-2][0][38:], bytes([lobby.F_REJOIN]))
                self.assertEqual(room.flags, flags)
                tokens = room.tokens
                self.assertIs(self.create(flags=flags), room)
                self.assertEqual(self.sent[-1][0], self.server.peer_packet(room))
                self.assertEqual(join(room.code, room.build), accepted)
                self.assertEqual(room.tokens, tokens)
                self.assertEqual(room.flags, flags)
                self.sent.clear()
                packet = b'H2NP\x03\x03'
                self.send_relay(room, 0, packet, token=b'x' * 16)
                self.assertEqual(self.sent, [], 'difficulty metadata must not bypass relay authentication')
                self.send_relay(room, 0, packet)
                expected = self.server.header(106) + struct.pack('<I', room.id) + room.tokens[1] + packet
                self.assertEqual(self.sent, [(expected, self.guest)])
                self.request(3, struct.pack('<I', room.build), self.guest)
                self.assertEqual(self.sent[-1][0], self.server.header(102) + b'\0',
                                 'difficulty metadata must not expose occupied rooms')

    def relay_room(self, flags=0):
        room = self.create(flags=flags | lobby.F_RELAY, build=2026100705)
        self.request(4, struct.pack("<III", room.id, room.code, room.build) + lobby.pack_endpoint(self.guest), self.guest)
        self.assertEqual(self.sent[-2][0][6:18], lobby.pack_endpoint(self.guest) * 2)  # the joiner's endpoints to the host
        self.assertEqual(self.sent[-2][0][22:38], room.tokens[0])
        self.assertEqual(self.sent[-1][0][11:23], lobby.pack_endpoint(self.host) * 2)  # the host's to the joiner
        self.assertEqual(self.sent[-1][0][23:39], room.tokens[1])
        self.assertNotEqual(room.tokens[0], room.tokens[1])
        self.sent.clear()
        return room

    def send_relay(self, room, member, packet, address=None, token=None):
        endpoint = self.host if member == 0 else self.guest
        self.request(6, struct.pack("<I", room.id) + (token or room.tokens[member]) + packet, address or endpoint)

    def test_relay_all_room_modes_and_full_welcome(self):
        for flags in (0, lobby.F_PRIVATE, lobby.F_UNLISTED):
            with self.subTest(flags=flags):
                self.server.rooms.clear()
                room = self.relay_room(flags)
                for member in (0, 1):
                    packet = b"H2NP\x02\x02" + b"x" * (lobby.MAX_GAME_PACKET - 6)
                    self.send_relay(room, member, packet)
                    expected = self.server.header(106) + struct.pack("<I", room.id) + room.tokens[1 - member] + packet
                    self.assertEqual(self.sent[-1], (expected, self.guest if member == 0 else self.host))

    def resume(self, room, member, endpoint, token=None, build=None, kind=7):
        self.request(kind, struct.pack('<I', room.id) + (token or room.tokens[member]) +
                     struct.pack('<I', room.build if build is None else build) + lobby.pack_endpoint(endpoint), endpoint)

    def test_rejoin_reserves_both_slots_while_one_member_plays(self):
        room = self.relay_room(lobby.F_REJOIN)
        start = room.last_activity[0]
        room.last_activity[1] = start - 600
        with patch.object(lobby.time, 'monotonic', return_value=start + 10):
            self.server.expire()
            self.assertIn(room.id, self.server.rooms)
            self.send_relay(room, 0, b'H2NP\x06\x03')
        with patch.object(lobby.time, 'monotonic', return_value=start + 41):
            self.server.expire()
            self.assertNotIn(room.id, self.server.rooms)

    def test_rejoin_authenticates_role_and_rebinds_changed_address(self):
        for flags in (0, lobby.F_PRIVATE, lobby.F_UNLISTED):
            with self.subTest(flags=flags):
                self.server.rooms.clear()
                room = self.relay_room(flags | lobby.F_REJOIN)
                for member in (0, 1):
                    endpoint = (f'127.0.0.{member + 3}', 44000 + member)
                    self.resume(room, member, endpoint)
                    response = self.sent[-1][0]
                    self.assertEqual(response[5], 107)
                    self.assertEqual(response[10:12], bytes([0, member]))
                    self.assertEqual(response[12:], room.tokens[member])
                    self.assertEqual(room.host_public if member == 0 else room.joiner_public, endpoint)
                    before = len(self.sent)
                    self.send_relay(room, member, b'H2NP\x06\x03')  # previous endpoint no longer owns the slot
                    self.assertEqual(len(self.sent), before)
                    self.send_relay(room, member, b'H2NP\x06\x03', address=endpoint)
                    self.assertEqual(len(self.sent), before + 1)

    def test_rejoin_rejects_bad_ticket_build_and_code_only_takeover(self):
        room = self.relay_room(lobby.F_REJOIN)
        stranger = ('127.0.0.4', 41000)
        for token, build, result in ((b'x' * 16, room.build, 1), (room.tokens[1], 0, 4)):
            self.resume(room, 1, stranger, token=token, build=build)
            self.assertEqual(self.sent[-1][0][10], result)
            self.assertEqual(len(self.sent[-1][0]), 12)
            self.assertEqual(room.joiner_public, self.guest)
        self.request(4, struct.pack('<III', room.id, room.code, room.build) + lobby.pack_endpoint(stranger), stranger)
        self.assertEqual(self.sent[-1][0][10], 3)
        self.resume(room, 1, stranger, token=b'x' * 16, kind=8)
        self.assertIn(room.id, self.server.rooms)

    def test_rejoin_goodbye_suspends_explicit_end_closes(self):
        room = self.relay_room(lobby.F_REJOIN)
        self.send_relay(room, 0, b'H2NP\x04\x03')
        self.assertIn(room.id, self.server.rooms)
        self.resume(room, 1, self.guest, kind=8)
        self.assertNotIn(room.id, self.server.rooms)

    def test_recovery_packets_are_scoped_to_new_protocol_rooms(self):
        room = self.relay_room(lobby.F_REJOIN)
        before = len(self.sent)
        self.send_relay(room, 0, b'H2NP\x0A\x02' + b'x' * 40)
        self.assertEqual(len(self.sent), before)
        self.send_relay(room, 0, b'H2NP\x0A\x03' + b'x' * 40)
        self.assertEqual(len(self.sent), before + 1)

    def test_relay_rejects_unknown_endpoint_token_size_and_nested_packets(self):
        room = self.relay_room()
        hello = b"H2NP\x01\x02" + struct.pack("<I", room.build)
        for address, token in ((self.guest, room.tokens[0]), (("127.0.0.2", 30003), room.tokens[0]),
                               (self.host, room.tokens[1]), (self.host, b"x" * 16)):
            self.send_relay(room, 0, hello, address, token)
        for packet in (b"H2NP", b"H2NP\x09\x02", b"H2NP\x01\x01", b"H2LB\x01\x06" + hello,
                       b"H2NP\x02\x02" + b"x" * lobby.MAX_GAME_PACKET):
            self.send_relay(room, 0, packet)
        self.assertEqual(self.sent, [])
        self.assertEqual(room.relay_packets, [0, 0])

    def test_relay_lifetime_requires_both_members_and_is_independent_of_refresh(self):
        room = self.relay_room()
        hello = b"H2NP\x01\x02" + struct.pack("<I", room.build)
        start = room.last_refresh
        with patch.object(lobby.time, "monotonic", return_value=start + lobby.ROOM_TTL + 1):
            self.send_relay(room, 0, hello)
            self.send_relay(room, 1, hello)
            self.server.expire()
            self.assertIn(room.id, self.server.rooms)
        with patch.object(lobby.time, "monotonic", return_value=start + lobby.ROOM_TTL + lobby.RELAY_TTL + 2):
            self.send_relay(room, 0, hello)
            self.assertNotIn(room.id, self.server.rooms)

    def test_relay_budget_supports_gameplay_but_is_bounded(self):
        room = self.relay_room()
        now = room.last_refresh
        with patch.object(lobby.time, "monotonic", return_value=now):
            for _ in range(int(lobby.RELAY_PACKET_BURST) + 20):
                self.send_relay(room, 0, b"H2NP\x03\x02")
            self.assertEqual(len(self.sent), lobby.RELAY_PACKET_BURST)
        with patch.object(lobby.time, "monotonic", return_value=now + 1):
            self.send_relay(room, 0, b"H2NP\x03\x02")
            self.assertEqual(len(self.sent), lobby.RELAY_PACKET_BURST + 1)

    def test_lost_match_replies_are_repeatable_and_bye_closes_relay(self):
        room = self.relay_room()
        tokens = room.tokens
        self.request(1, struct.pack("<IB", room.build, room.flags) + lobby.pack_endpoint(self.host), self.host)
        self.assertEqual(len(self.server.rooms), 1)
        self.assertEqual(self.sent[-1][0][5], 104)
        self.request(4, struct.pack("<III", room.id, room.code, room.build) + lobby.pack_endpoint(self.guest), self.guest)
        self.assertEqual(room.tokens, tokens)
        self.sent.clear()
        self.send_relay(room, 1, b"H2NP\x04\x02")
        self.assertEqual(len(self.sent), 3)
        self.assertTrue(all(address == self.host for _, address in self.sent))
        self.assertNotIn(room.id, self.server.rooms)


if __name__ == "__main__":
    unittest.main()
