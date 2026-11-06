import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from naomi_audio_source import segment_ticks, song_timing, vlq


def bank(loop=True):
    data = bytearray(192)
    data[:4] = b'DTPK'
    struct.pack_into('<I', data, 8, len(data))
    struct.pack_into('<I', data, 0x2c, 64)
    struct.pack_into('<HBB', data, 68, 8, 3, 0xa8)
    struct.pack_into('<I', data, 76, 16)
    data[80] = 0x40
    struct.pack_into('<III', data, 84, 32, 64 if loop else 0x91000040, 0x90000008)
    data[112:119] = bytes.fromhex('903c400aff2f00')
    data[144:151] = bytes.fromhex('903e4014ff2f00')
    return data


class NativeSequenceTiming(unittest.TestCase):
    def test_native_intro_and_loop(self):
        timing = song_timing(bank(), 0x3a8)
        self.assertEqual((timing['intro_ticks'], timing['loop_ticks']), (10, 20))
        self.assertTrue(timing['loop'])

    def test_one_shot_control(self):
        timing = song_timing(bank(False), 0x3a8)
        self.assertEqual(timing['intro_ticks'], 30)
        self.assertFalse(timing['loop'])

    def test_am2_pitch_bend_and_note_off_have_one_parameter(self):
        self.assertEqual(segment_ticks(bytes.fromhex('e040072009803c03ff2f00'), 0), 19)

    def test_running_notes_and_high_bit_zero_delta(self):
        self.assertEqual(segment_ticks(bytes.fromhex('903cc03e4014ff2f00'), 0), 20)

    def test_variable_delta(self):
        self.assertEqual(vlq(bytes.fromhex('8100'), 0), (128, 2))
        with self.assertRaises(ValueError): vlq(bytes.fromhex('ffffffff'), 0)

    def test_reject_unsupported_tempo_and_bad_loop(self):
        with self.assertRaises(ValueError): segment_ticks(bytes.fromhex('ff510307a120'), 0)
        data = bank()
        struct.pack_into('<I', data, 92, 0x9000000c)
        with self.assertRaises(ValueError): song_timing(data, 0x3a8)


if __name__ == '__main__':
    unittest.main()
