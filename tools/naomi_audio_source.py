"""Verified HOTD2 Naomi audio source and AM2 sequence timing (offline only)."""
from pathlib import Path
import hashlib
import re
import struct
import zipfile

from import_naomi_visuals import MPR_SHA1, PROGRAM_SHA1, iso

RATE = 44100
# The original ARM driver advances its millisecond sequence clock every 44 AICA
# samples. This also reproduces the measured 127.708753 s / 61.574059 s loops.
SAMPLES_PER_TICK = 44
u32 = lambda data, offset: struct.unpack_from('<I', data, offset)[0]

# PC music index, original sound-test bank loader entry, native command, bank.
# CLEAR's normal game song is 05/A8, not the short 01/05/A8 sound-test jingle.
MUSIC = (
    (0, 1, 0x0000a8, 'HOD2DAT_Pack0.bin', 'chapter2'),
    (1, 2, 0x0003a8, 'HOD2DAT_BGM02.bin', 'chapter1'),
    (3, 3, 0x0005a8, 'HOD2DAT_CLEAR.bin', 'clear'),
    (5, 4, 0x000aa8, 'HOD2DAT_BOSS_PACK.bin', 'boss'),
    (6, 5, 0x000ba8, 'HOD2DAT_DEAD_PACK.bin', 'name'),
    (8, 6, 0x0009a8, 'HOD2DAT_ADVA_PACK.bin', 'attract'),
    (9, 7, 0x0020a9, 'HOD2DAT_Game_Over.bin', 'gameover'),
    (10, 8, 0x0001a8, 'HOD2DAT_BGM03.bin', 'chapter5_boss1'),
    (11, 9, 0x000da8, 'HOD2DAT_FROM_ACT.bin', 'chapter6_boss1'),
    (12, 10, 0x000fa8, 'HOD2DAT_ENDING.bin', 'ending_long'),
    (13, 10, 0x010fa8, 'HOD2DAT_ENDING.bin', 'ending_short'),
    (14, 11, 0x0001a8, 'HOD2DAT_ST5_BOSS.bin', 'chapter5_boss2'),
    (15, 12, 0x0000a8, 'HOD2DAT_ST6_BOSS.bin', 'chapter6_boss2'),
    (16, 13, 0x000ea8, 'HOD2DAT_STAGE2_PACK.bin', 'chapter4'),
    (17, 14, 0x0010a8, 'HOD2DAT_STAGE3_PACK.bin', 'chapter3'),
    (18, 15, 0x0011a8, 'HOD2DAT_STAGE4_PACK.bin', 'chapter5'),
    (19, 16, 0x0012a8, 'HOD2DAT_STAGE6_PACK.bin', 'chapter6'),
)

# Only waveform-verified PC/Naomi identities: other banks have shifted IDs.
EFFECTS = (
    (0x1aa9, 252, 'HOD2DAT_STAGE3_SE.bin'),
    (0x25a9, 356, 'HOD2DAT_STAGE6_SE.bin'),
    (0x21aa9, 253, 'HOD2DAT_STAGE3_SE.bin'),
    (0x225a9, 357, 'HOD2DAT_STAGE6_SE.bin'),
    (0x234a9, 91, 'HOD2DAT_STAGE_COM.bin'),
    (0x423a9, 313, 'HOD2DAT_STAGE5_SE.bin'),
    (0x425a9, 358, 'HOD2DAT_STAGE6_SE.bin'),
    (0x718a9, 181, 'HOD2DAT_STAGE1_SE.bin'),
    (0x723a9, 315, 'HOD2DAT_STAGE5_SE.bin'),
    (0xb23a9, 318, 'HOD2DAT_STAGE5_SE.bin'),
    (0xb25a9, 364, 'HOD2DAT_STAGE6_SE.bin'),
    (0xf19a9, 226, 'HOD2DAT_STAGE2_SE.bin'),
    (0x171aa9, 260, 'HOD2DAT_STAGE3_SE.bin'),
    (0x191aa9, 261, 'HOD2DAT_STAGE3_SE.bin'),
    (0x1a1ba9, 286, 'HOD2DAT_STAGE4_SE.bin'),
    (0x1b23a9, 331, 'HOD2DAT_STAGE5_SE.bin'),
    (0x1b25a9, 379, 'HOD2DAT_STAGE6_SE.bin'),
    (0x1c1aa9, 263, 'HOD2DAT_STAGE3_SE.bin'),
    (0x1f23a9, 334, 'HOD2DAT_STAGE5_SE.bin'),
    (0x1f25a9, 382, 'HOD2DAT_STAGE6_SE.bin'),
    (0x221aa9, 268, 'HOD2DAT_STAGE3_SE.bin'),
    (0x3919a9, 250, 'HOD2DAT_STAGE2_SE.bin'),
    (0x3925a9, 407, 'HOD2DAT_STAGE6_SE.bin'),
    (0x3b19a9, 251, 'HOD2DAT_STAGE2_SE.bin'),
    (0x4d17a9, 168, 'HOD2DAT_STAGE_COM2.bin'),
    (0x4f17a9, 169, 'HOD2DAT_STAGE_COM2.bin'),
)

def effect_plan(banks):
    out = []
    for command, selector, bank in EFFECTS:
        data = banks[bank]
        at = compositions(data)[command]
        seq = data[at:at + 6]
        if seq[0] != 0xc0 or seq[1] & 0xfc != 0xdc or seq[4] & 0xf0 != 0x80 or seq[5] != 0xff:
            raise ValueError('Unexpected effect sequence')
        spi = u32(data, 0x30) + 0x50 + (seq[2] + ((seq[4] & 15) << 7)) * 0x40
        sample = u32(data, 0x3c) + 4 + data[spi + 2] * 16
        packed, start, end = struct.unpack_from('<IHH', data, sample)
        coarse = struct.unpack_from('b', data, spi + 10)[0]
        if not packed & 0x2000000 or not start < end or data[spi + 9] != 255 or coarse not in (0, -12, -24):
            raise ValueError('Unverified sample looping/pitch')
        scale = 1 << (-coarse // 12)
        intro = start * scale
        loop = (end - start) * scale
        out.append(dict(index=command, selector=selector, command=command, bank=bank,
                        name=f'sfx_{command:x}', source_sha256=hashlib.sha256(data).hexdigest(),
                        loop=True, intro_frames=intro, loop_frames=loop,
                        duration=(intro + 3 * loop) / RATE + 5))
    return out

def load_rom(path):
    with zipfile.ZipFile(path) as archive:
        program = archive.read('epr-21585.ic22')
        if hashlib.sha1(program).hexdigest() != PROGRAM_SHA1:
            raise ValueError('Expected the verified USA cartridge program')
        chips = []
        for number in range(15, 21):
            names = [n for n in archive.namelist() if re.search(r'\.ic' + str(number) + r's?$', n)]
            if len(names) != 1:
                raise ValueError(f'Missing/ambiguous sound cartridge chip IC{number}')
            data = archive.read(names[0])
            if len(data) != 0x800000 or hashlib.sha1(data).hexdigest() != MPR_SHA1[number - 1]:
                raise ValueError(f'Unexpected sound cartridge chip IC{number}')
            chips.append(data)
    return program, iso(b''.join(chips), 0)

def compositions(data):
    if data[:4] != b'DTPK' or u32(data, 8) != len(data):
        raise ValueError('Invalid DTPK bank')
    seq = u32(data, 0x2c)
    out = {}
    for i in range(u32(data, seq) + 1):
        offset, bank, kind = struct.unpack_from('<HBB', data, seq + 4 + 4 * i)
        group = seq + offset
        for song in range(u32(data, group) + 1):
            out[(song << 16) | (bank << 8) | kind] = seq + u32(data, group + 4 + 4 * song)
    return out

def vlq(data, pos):
    value = 0
    for _ in range(4):
        byte = data[pos]
        pos += 1
        value = value * 128 + (byte & 127)
        if not byte & 128:
            return value, pos
    raise ValueError('Invalid AM2 sequence delta')

def segment_ticks(data, pos):
    """AM2 MIDI: note-off/pitch bend have one value; final data bit 7 skips delta."""
    status = 0
    ticks = 0
    for _ in range(100000):
        if data[pos] & 128:
            status = data[pos]
            pos += 1
        if status == 255:
            kind = data[pos]
            count, pos = vlq(data, pos + 1)
            if kind == 0x2f and count == 0:
                return ticks
            # Fail closed on tempo/meta changes this importer has not verified.
            raise ValueError(f'Unsupported MIDI meta event {kind:#x}')
        count = {8: 1, 9: 2, 10: 2, 11: 2, 12: 1, 13: 1, 14: 1}.get(status >> 4)
        if count is None or pos + count > len(data):
            raise ValueError('Unsupported/truncated MIDI event')
        last = data[pos + count - 1]
        pos += count
        if not last & 128:
            delta, pos = vlq(data, pos)
            ticks += delta
        if ticks > 600000:
            raise ValueError('Unexpected music length')
    raise ValueError('Unterminated music segment')

def song_timing(data, command):
    start = compositions(data)[command]
    if data[start] != 0x40:
        if command == 0x20a9:
            return dict(loop=False, intro_ticks=0, loop_ticks=0, duration=12.0)
        raise ValueError('Expected an internal AICA song')
    timeline = {}
    ticks = 0
    pos = 4
    for _ in range(64):
        timeline[pos] = ticks
        word = u32(data, start + pos)
        opcode = word >> 24
        if opcode == 0x90:
            target = word & 0xffffff
            if target not in timeline or target >= pos or ticks <= timeline[target]:
                raise ValueError('Invalid song loop')
            intro = timeline[target]
            loop = ticks - intro
            return dict(loop=True, intro_ticks=intro, loop_ticks=loop,
                        duration=(intro + 2 * loop) * SAMPLES_PER_TICK / RATE + 5)
        if opcode == 0x80:
            if word not in (0x80a00012, 0x80a01900):
                raise ValueError('Unexpected song initialization command')
        elif opcode in (0, 0x91):
            ticks += segment_ticks(data, start + (word & 0xffffff))
            if opcode == 0x91:
                return dict(loop=False, intro_ticks=ticks, loop_ticks=0,
                            duration=ticks * SAMPLES_PER_TICK / RATE + 5)
        else:
            raise ValueError(f'Unsupported song command {word:#x}')
        pos += 4
    raise ValueError('Unterminated song control list')

def music_plan(banks):
    return [dict(index=index, selector=selector, command=command, bank=bank, name=name,
                 source_sha256=hashlib.sha256(banks[bank]).hexdigest(), **song_timing(banks[bank], command))
            for index, selector, command, bank, name in MUSIC]
