"""Typed-field conversion checks; no original game files are needed."""
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))
from typing_enemy_schemas import convert_enemy


class Memory:
    base = 0x0CEB5A00
    def __init__(self):
        self.data = bytearray(512)
    def region(self, address, size=4):
        offset = address-self.base
        if offset < 0 or offset+size > len(self.data):
            raise ValueError("outside fixture")
        return self.data, offset
    def u32(self, address):
        return struct.unpack_from("<I", self.data, address-self.base)[0]
    def write(self, address, value):
        struct.pack_into("<I", self.data, address-self.base, value & 0xFFFFFFFF)
    def short(self, address):
        return struct.unpack_from("<h", self.data, address-self.base)[0]


class EnemySchemaTests(unittest.TestCase):
    def setUp(self):
        self.mem = Memory()
        self.record = self.mem.base
        self.list = self.mem.base+128
        self.motions = {100: 200, 101: 201, 102: 202}
        self.issues = []
    def convert(self, behavior):
        self.mem.data[36:40] = bytes([7, 0, behavior, 1])
        self.mem.write(self.record+40, self.list)
        def remap(mem, address, mapping, description, location):
            mem.write(address, mapping[mem.u32(address)])
        convert_enemy(self.mem, self.record, {}, remap, {}, self.motions,
                      lambda kind, **fields: self.issues.append((kind, fields)))
        self.assertFalse(self.issues)
    def test_distance_header_and_motion_list_leave_flags_and_ticks(self):
        struct.pack_into("<fhhh", self.mem.data, 128, 6.0, 101, 100, 3)
        struct.pack_into("<4h", self.mem.data, 138, 101, 0, 2, 100)
        struct.pack_into("<h", self.mem.data, 146, -1)
        self.convert(34)
        self.assertEqual(struct.unpack_from("<f", self.mem.data, 128)[0], 6.0)
        self.assertEqual(self.mem.short(self.list+4), 101)
        self.assertEqual(self.mem.short(self.list+6), 200)
        self.assertEqual(self.mem.short(self.list+10), 201)
        self.assertEqual(self.mem.short(self.list+16), 100)
    def test_flag_entries_preserve_event_flag_and_change_stride_after_attack(self):
        struct.pack_into("<5h", self.mem.data, 128, 100, 0, 1, -1, 101)
        struct.pack_into("<5h", self.mem.data, 138, 101, 0, 1, 30, 102)
        struct.pack_into("<4h", self.mem.data, 148, 102, 0, 1, 40)
        struct.pack_into("<h", self.mem.data, 156, -1)
        self.convert(36)
        self.assertEqual(self.mem.short(self.list), 200)
        self.assertEqual(self.mem.short(self.list+8), 101)
        self.assertEqual(self.mem.short(self.list+10), 201)
        self.assertEqual(self.mem.short(self.list+18), 102)
        self.assertEqual(self.mem.short(self.list+20), 202)


if __name__ == "__main__":
    unittest.main()
