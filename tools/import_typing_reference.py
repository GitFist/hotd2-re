"""Prepare supplied English PC TOTD reference campaign data for the native engine, offline.

The executable is input to this tool only. The prepared bank contains data, never
executable code. Conversion is deliberately schema based: an integer that happens
to equal a motion/model id is not rewritten without knowing its field's meaning.
The JSON report identifies every unresolved conversion; incomplete banks cannot be
activated by the native loader.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys

import pefile

sys.path.insert(0, str(Path(__file__).parent))
import evt
from typing_actor_schemas import prepare_civilian_bank
from typing_actor_schemas import convert_actor
from typing_model_bindings import prepare_model_bindings
from typing_name_font import prepare_name_font

SOURCE_COMMON = 0x0CEB5800
SOURCE_STAGE = 0x0CEB5A00
CAMERA_NAMESPACE = 0x4000
MOTION_NAMESPACE = 0x4000
MODEL_NAMESPACE = 0x4000  # model fields also occur as signed 16-bit parameters
FILE_NAMESPACE = 0x10000
SPRITE_NAMESPACE = 0x4000

# Additional lengths verified against the English PC reference handler table at 0x59D700.
# 0x60 has four fixed arguments followed by (count, pointer) pairs, then -1.
EXTRA_OPS = {
    0x60: ("typing_challenge", "message"),
    0x61: ("typing_words", 12),
    0x62: ("typing_countdown", 12),
    0x63: ("typing_wait_challenge", 8),
    0x64: ("typing_words_one_player", 12),
    0x65: ("typing_words_two_players", 12),
    0x66: ("typing_props", 8),
    0x67: ("typing_clear_actor_slots", 4),
    0x68: ("typing_word_limits", 12),
    0x69: ("typing_lesson", None),
    0x6A: ("typing_wait_lesson", 8),
    0x6B: ("typing_wait_count", 8),
    0x6C: ("typing_camera_block_player1", "block30"),
    0x6D: ("typing_camera_block_player2", "block30"),
    0x6E: ("typing_branch_player1", 8),
    0x6F: ("typing_branch_player2", 8),
    0x70: ("typing_checkpoint", 4),
    0x71: ("typing_retry_checkpoint", 4),
    0x72: ("typing_lesson_objects", "list"),
    0x73: ("typing_mode_branch", 12),
    0x74: ("typing_wait_result", 8),
}
SUPPORTED_EXTRA = {0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x6B, 0x70, 0x71, 0x73}


class Executable:
    def __init__(self, path: Path):
        self.path = path
        self.image = pefile.PE(str(path)).get_memory_mapped_image()
        self.base = 0x400000

    def read(self, va: int, fmt: str):
        return struct.unpack_from(fmt, self.image, va - self.base)

    def u32(self, va: int) -> int:
        return self.read(va, "<I")[0]

    def i16(self, va: int) -> int:
        return self.read(va, "<h")[0]

    def string(self, va: int) -> str:
        if not self.base <= va < self.base + len(self.image):
            return ""
        raw = self.image[va-self.base:va-self.base+128].split(b"\0", 1)[0]
        return raw.decode("ascii", errors="replace")

    def names(self, table: int, maximum=1024):
        out = {}
        misses = 0
        for i in range(1, maximum):
            name = self.string(self.u32(table + i*4))
            if name.endswith(".bin"):
                out[i] = name
                misses = 0
            else:
                misses += 1
                if misses == 8:
                    break
        return out


def prepare_audio(source, native, source_root, native_root):
    """Identity reuse is byte-exact; changed channel formats retain source WAVs."""
    def clean(path):
        return "/".join(part for part in path.replace("\\", "/").lower().split("/") if part)
    def text(exe, address, size):
        return exe.image[address-exe.base:address-exe.base+size].split(b"\0", 1)[0].decode("ascii")
    source_files = {clean(str(p.relative_to(source_root/"Sound"))): p
                    for p in (source_root/"Sound").rglob("*") if p.is_file()}
    native_files = {clean(str(p.relative_to(native_root/"Sound"))): p
                    for p in (native_root/"Sound").rglob("*") if p.is_file()}
    native_hashes = {}
    for name, path in native_files.items():
        native_hashes.setdefault(hashlib.sha256(path.read_bytes()).digest(), name)
    loops, offs = [], []
    for base, values in ((0x5CE9C8, loops), (0x5CEA80, offs)):
        for i in range(128):
            value = source.u32(base+i*4)
            if value == 0xFFFFFFFF: break
            values.append(value)
        else: raise ValueError("unterminated original sound loop table")
    entries, files, source_names = [], {}, {}
    def entry(sound, name, flags):
        name = clean(name)
        if not name:
            entries.append((sound, flags, "")); return
        if sound >> 28 in (0,2): source_names.setdefault(name,sound)
        path = source_files.get(name)
        if path is None:
            if not name.endswith("_off.wav"):
                raise ValueError(f"missing original sound payload: {name}")
            output = "TypingSound/"+name  # stop command still runs before the absent OFF file
        else:
            raw = path.read_bytes()
            digest = hashlib.sha256(raw).digest()
            alias = native_hashes.get(digest)
            if alias is not None:
                output = "Sound/"+str(native_files[alias].relative_to(native_root/"Sound")).replace("\\", "/")
            else:
                output = "TypingSound/"+name
                files[output] = raw
        entries.append((sound, flags, output))
    # 332 authored file records. The following data is the loop table, not a
    # sound record; the source PC binary has no 0xFFFF sentinel in this slot.
    for i in range(332):
        address = 0x5CA658+i*52
        sound = source.u32(address)
        entry(sound, "SE/"+text(source, address+4, 48), (1 if sound in loops else 0) | (2 if sound in offs else 0))
    for i in range(744):
        address = 0x5C3DB8+i*36
        entry(0x20000000+i, "" if source.i16(address+2) == -1 else "Voice/"+text(source, address+4, 32), 0)
    for base, count, alternate in ((0x5C3CC0,42,False),(0x5C3D68,20,True)):
        for i in range(count):
            sound = 0x10000000+i
            pointer = source.u32(base+i*4)
            entry(sound | (0x80000000 if alternate else 0), "" if not pointer else "BGM/"+source.string(pointer),
                  0 if sound in (0x10000009,0x10000025) else 1)
    native_map = {}
    for base, count, stride, category in ((0x5845F8,324,52,0),(0x580448,467,36,2)):
        for i in range(count):
            address = base+i*stride
            if category == 2 and native.i16(address+2) == -1: continue
            name = clean(("SE/" if category == 0 else "Voice/")+text(native,address+4,stride-4))
            if name in source_names:
                native_map[native.u32(address) if category == 0 else 0x20000000+i] = source_names[name]
    for i in range(41): native_map[0x10000000+i] = 0x10000000+i
    # Snd_PlayVoiceLine 0x004234C0 uses this independent group/player selector.
    choices = source.image[0x5958F8-source.base:0x595EB0-source.base]
    dialogue = source.image[0x593FD8-source.base:0x5958F8-source.base]
    records = source.image[0x595EB0-source.base:0x59BC70-source.base]
    segments = source.image[0x59BC70-source.base:0x59BC70+402*8-source.base]
    glyphs = source.image[0x4DBCF8-source.base:0x4DBCF8+128*2-source.base]
    return entries, files, (choices, dialogue, records, segments, glyphs), native_map


class EventFile:
    def __init__(self, common: bytes, stage: bytes):
        self.common = bytearray(common)
        self.stage = bytearray(stage)

    def region(self, address: int, size=4):
        data, base = (self.stage, SOURCE_STAGE) if address >= SOURCE_STAGE else (self.common, SOURCE_COMMON)
        offset = address-base
        if offset < 0 or offset+size > len(data):
            raise ValueError(f"event pointer {address:08X} outside data")
        return data, offset

    def u32(self, address: int):
        try:
            data, offset = self.region(address)
            return struct.unpack_from("<I", data, offset)[0]
        except ValueError:
            return None

    def write(self, address: int, value: int):
        data, offset = self.region(address)
        struct.pack_into("<I", data, offset, value & 0xFFFFFFFF)

    def length(self, address: int):
        op = self.u32(address)
        if op not in EXTRA_OPS:
            return evt.length(self, address)
        kind = EXTRA_OPS[op][1]
        if isinstance(kind, int):
            return kind
        if kind == "message":
            p = address+20
            for _ in range(64):
                count = self.u32(p)
                if count is None:
                    return None
                if count == 0xFFFFFFFF:
                    return p-address+4
                p += 8
            return None
        if kind == "list":
            for i in range(1, 4096):
                value = self.u32(address+i*4)
                if value is None:
                    return None
                if value == 0xFFFFFFFF:
                    return (i+1)*4
        if kind == "block30":
            value = self.u32(address+4)
            if value is not None and value < 0x80000000:
                return value//16*4+8
        return None

    def scripts(self):
        for scene in range(256):
            variants = self.u32(SOURCE_STAGE+scene*4)
            if variants in (None, 0xFFFFFFFE):
                break
            if variants == 0xFFFFFFFF:
                # A chapter boundary is followed by ending/Boss Mode scenes.
                # Only -2 terminates the entire file's scene directory.
                continue
            if not SOURCE_COMMON <= variants < SOURCE_STAGE+len(self.stage):
                raise ValueError(f"invalid scene {scene} variant table {variants:08X}")
            for variant in range(128):
                script = self.u32(variants+variant*4)
                if script in (None, 0xFFFFFFFF):
                    break
                if script:
                    yield scene, variant, script


def motion_data(root: Path, exe: Executable, source: bool):
    names, ids, counts, ticks = ((0x4F09A0, 0x500B78, 0x500C40, 0x4FE640) if source else
                                  (0x4D1B00, 0x4E2B14, 0x4E2BDC, 0x4E07D0))
    out = {}
    for index, name in exe.names(names, 50).items():
        count = exe.i16(counts+index*2)
        motion_ids = exe.read(exe.u32(ids+index*4), "<"+"h"*count)
        raw = (root/"mot"/name).read_bytes()
        offsets = struct.unpack_from("<"+"I"*count, raw)
        boundaries = sorted(set(offsets) | {len(raw)})
        ends = dict(zip(boundaries, boundaries[1:]))
        out[name] = [(mid, exe.i16(ticks+mid*2), raw[start:ends[start]].removesuffix(b"\xff"*4))
                     for mid, start in zip(motion_ids, offsets)]
    return out


def source_palette(exe, file, index):
    """Prepare the uploaded palette index, including the PC loader's bank offset."""
    # Model texture files 1..355 use 0x4BEAF0 (callers 0x487D84/0x48816C/
    # 0x4882B0), without a selected bank: 0x4BE668 uses default bank 0.
    # Sprite files begin with scr_common.bin at 356. Their loader calls
    # 0x492B20, which adds one to the inner 0x492B40 selection rule.
    return 0 if file < 0x164 else source_palette_rule(exe, file, index) + 1


def source_palette_rule(exe, file, index):
    """Inner 0x492B40 rule; its return is not itself the uploaded bank index."""
    if not 0x164 <= file <= 0x222:
        return 0
    category = exe.image[0x492D84-exe.base+file-0x164]
    rule = exe.u32(0x492D10+category*4)
    constants = {0x492B63:1,0x492B69:33,0x492B7C:10,0x492B82:11,
                 0x492C4E:20,0x492C54:27,0x492C67:26,0x492CE1:48,
                 0x492CE7:52,0x492CED:53,0x492CF3:54,0x492CF9:55,
                 0x492CFF:56,0x492D05:57,0x492D0B:0,0x492CC7:45}
    if rule in constants:
        return constants[rule]
    tables = {0x492B6F:0x5BC008,0x492BAD:0x5BBFEC,
              0x492C0A:0x5BBFE4,0x492C5A:0x5BBF88}
    if rule in tables:
        return exe.image[tables[rule]-exe.base+index]
    if rule == 0x492B88: return 13 if index > 109 else 43
    if rule == 0x492B9C: return 16 if index == 17 else 0
    if rule == 0x492BBA: return 16 if index == 23 else 0
    if rule == 0x492BCB: return 19 if index <= 13 else 0
    if rule == 0x492BDC: return 16 if index <= 6 else 0
    if rule == 0x492BED: return 18 if index in (3,5,6) else 0
    if rule == 0x492C17:
        return 49 if index == 1 else 50 if 44 <= index <= 56 else 51 if 3 <= index <= 15 else 0
    if rule == 0x492C6D: return min(32,28+index//75)
    if rule == 0x492CCD: return 47 if 43 <= index <= 56 else 0
    raise ValueError(f"unsupported palette rule {rule:08X}")


def prepare_wordmark(source, root):
    """Keep the original title lettering while removing its separate character.

    The five title strips are stored bottom-up. The character's body is a
    separate alpha component; its hand touches the wordmark's shadow. Remove
    that component and the verified hand-only alpha rectangles. Every retained
    ARGB4444 texel is unchanged from scr_title.bin.
    """
    from prs import decompress
    from tex_export import decode_texture

    file = 428
    if source.string(source.u32(0x4F0108+file*4)) != "scr_title.bin":
        raise ValueError("unexpected English PC title resource")
    packed = (root/"tex"/"scr_title.bin").read_bytes()
    raw = decompress(packed[4:])
    if len(raw) != struct.unpack_from("<I", packed)[0]:
        raise ValueError("invalid original title texture stream")
    descriptors = source.u32(0x5872C8+file*4)
    pixels = [0] * (640*512)
    for strip in range(5):
        width, height, fmt, offset, texture = source.read(descriptors+(86+strip)*16, "<hhIII")
        if (width, height, fmt, texture) != (128, 512, 0x902, 5236+strip):
            raise ValueError("unexpected English PC logo strip descriptor")
        _, _, texels = decode_texture(raw, {"w":width, "h":height, "fmt":fmt, "offset":offset})
        for y in range(512):
            at = y*640+strip*128
            pixels[at:at+128] = texels[(511-y)*128:(512-y)*128]

    # A source character pixel lies beyond the right edge of the wordmark.
    # Flood its alpha component instead of erasing an arbitrary rectangle,
    # which would also cut the trademark and lettering's drop shadow.
    seed = next((y*640+x for y in range(512) for x in range(512,640)
                 if pixels[y*640+x] >> 12), None)
    if seed is None:
        raise ValueError("original title character component not found")
    pending = [seed]
    removed = {seed}
    for pixel in pending:
        x, y = pixel%640, pixel//640
        for dx, dy in ((-1,0),(1,0),(0,-1),(0,1),(-1,-1),(1,-1),(-1,1),(1,1)):
            nx, ny = x+dx, y+dy
            index = ny*640+nx
            if 0 <= nx < 640 and 0 <= ny < 512 and index not in removed and pixels[index] >> 12:
                removed.add(index)
                pending.append(index)
    bounds = (min(i%640 for i in removed), min(i//640 for i in removed),
              max(i%640 for i in removed)+1, max(i//640 for i in removed)+1)
    if len(removed) != 19619 or bounds != (434,1,589,317):
        raise ValueError("unexpected English PC character alpha component")
    for pixel in removed:
        pixels[pixel] = 0
    hand_pixels = 0
    hand_rectangles = ((386,55,487,85),(428,85,487,94),(464,94,487,101))
    for left, top, right, bottom in hand_rectangles:
        for y in range(top,bottom):
            for x in range(left,right):
                index=y*640+x
                hand_pixels+=bool(pixels[index]>>12)
                pixels[index]=0
    if hand_pixels != 1489:
        raise ValueError("unexpected English PC attached-hand alpha pixels")
    visible = [i for i, pixel in enumerate(pixels) if pixel >> 12]
    bounds = (min(i%640 for i in visible), min(i//640 for i in visible),
              max(i%640 for i in visible)+1, max(i//640 for i in visible)+1)
    if bounds != (9,17,498,321):
        raise ValueError("unexpected English PC wordmark bounds")
    width, height = 512, 320
    output = bytearray(width*height*2)
    for pixel in visible:
        x, y = pixel%640-9, pixel//640-17
        struct.pack_into("<H", output, ((height-1-y)*width+x)*2, pixels[pixel])
    encoded = struct.pack("<I", len(output)) + b"".join(
        b"\xff"+output[i:i+8] for i in range(0,len(output),8)) + b"\x02\x00\x00"
    descriptor = struct.pack("<hhIII", width, height, 0x902, 0, 0x70000001)
    resource = (1,0x10301,"typing_wordmark.bin",encoded,(),descriptor,b"\0",b"")
    metadata = {"sprite":0x4FD4,"file":0x10301,"texture":0x70000001,
                "width":width,"height":height,"source_file":"scr_title.bin",
                "source_textures":[5236,5237,5238,5239,5240],
                "source_wordmark_bounds":list(bounds),"removed_character_pixels":len(removed),
                "removed_attached_hand_pixels":hand_pixels,"hand_mask_rectangles":hand_rectangles,
                "prepared_texels_sha256":hashlib.sha256(output).hexdigest()}
    return resource, (0x10301,0x70000001), metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="supplied English PC TOTD reference directory (Tod_e.exe)")
    parser.add_argument("native", type=Path, help="original HOD2 data directory (Hod2.exe)")
    parser.add_argument("output", type=Path, help="prepared bank output; never an executable")
    parser.add_argument("--report", type=Path, help="conversion coverage and actionable unresolved fields")
    parser.add_argument("--model-map", type=Path, required=True,
                        help="exact decoded identities from the typing_model_map build target")
    args = parser.parse_args()
    source, native = Executable(args.source/"Tod_e.exe"), Executable(args.native/"Hod2.exe")
    issues = []
    issue_keys = set()

    def issue(kind, **fields):
        row = {"kind": kind, **fields}
        key = json.dumps(row, sort_keys=True)
        if key not in issue_keys:
            issue_keys.add(key)
            issues.append(row)

    maps = {}
    extra_files = {}
    for name, st, nt in [("pol", 0x4EFB78, 0x4D0EF4), ("tex", 0x4F0108, 0x4D1410),
                          ("motion_file", 0x4F09A0, 0x4D1B00)]:
        source_names, native_names = source.names(st), native.names(nt)
        if name == "pol":
            source_names = source.names(st, (0x4F0108-st)//4)
            native_names = native.names(nt, (0x4D1410-nt)//4)
        elif name == "tex":
            source_names = source.names(st, (0x4F09A0-st)//4)
            native_names = native.names(nt, (0x4D1B00-nt)//4)
        elif name == "motion_file":
            source_names = source.names(st, (0x4F0A68-st)//4)
            native_names = native.names(nt, (0x4D1BC8-nt)//4)
        reverse = {value: key for key, value in native_names.items()}
        maps[name] = {key: reverse[value] for key, value in source_names.items() if value in reverse}
        if name in ("pol", "tex"):
            for key, value in source_names.items():
                if key not in maps[name]:
                    maps[name][key] = FILE_NAMESPACE+key
                    extra_files[(name, key)] = value
        maps[name][0] = 0

    # Global model ids are independent of resource file ids. The supplied map
    # establishes exact decoded geometry/material identity, never ordinal reuse.
    maps["model"] = {0: 0}
    model_identity = json.loads(args.model_map.read_text(encoding="utf-8"))["models"]
    source_model_files = {}
    source_pol = source.names(0x4EFB78, (0x4F0108-0x4EFB78)//4)
    for source_file in source_pol:
        native_file = maps["pol"].get(source_file)
        source_count = source.i16(0x508F20+source_file*2)
        native_count = native.i16(0x4E803C+native_file*2) if native_file is not None and native_file < FILE_NAMESPACE else 0
        if not 0 <= source_count < 4096 or not 0 <= native_count < 4096:
            raise ValueError("invalid model id table count")
        source_ids = source.read(source.u32(0x508688+source_file*4), "<"+"h"*source_count)
        native_ids = native.read(native.u32(0x4E794C+native_file*4), "<"+"h"*native_count) if native_count else ()
        for index, source_id in enumerate(source_ids):
            if source_id <= 0:
                continue
            source_model_files[source_id] = source_file
            if str(source_id) not in model_identity:
                raise ValueError(f"model identity missing for {source_id}")
            maps["model"][source_id] = int(model_identity[str(source_id)])
            if maps["model"][source_id] >= MODEL_NAMESPACE:
                maps["pol"][source_file] = FILE_NAMESPACE+source_file
                extra_files[("pol", source_file)] = source_pol[source_file]

    enemy_model_bindings = prepare_model_bindings(source, native, maps["model"])

    # Additional geometry also needs its own source texture bank, even when a
    # bank of the same name exists in HOD2. Resource aliases keep native lookups
    # isolated from these prepared assets.
    source_textures = source.names(0x4F0108, (0x4F09A0-0x4F0108)//4)
    texture_ids = {name:index for index,name in source_textures.items()}
    # Spr_Draw 0x004930B0 bounds ids at 0xF74, then reads file/id tables
    # 0x005BC050/0x005BFE20. Keep source banks separate even where their names
    # also occur in HOD2: texture ids and palette selection are independently
    # authored, and equal filenames do not establish pixel identity.
    sprites = []
    for sprite in range(0xF74):
        file = source.u32(0x5BC050+sprite*4)
        texture = source.u32(0x5BFE20+sprite*4)
        if file not in source_textures:
            raise ValueError(f"sprite {sprite} references unknown source texture file {file}")
        count = source.i16(0x587B60+file*2)
        start = source.u32(0x5872C8+file*4)
        if not any(source.u32(start+i*16+12) == (texture & 0x7FFFFFFF) for i in range(count)):
            raise ValueError(f"sprite {sprite} has no matching source texture descriptor")
        extra_files[("tex", file)] = source_textures[file]
        maps["tex"][file] = FILE_NAMESPACE+file
        sprites.append((FILE_NAMESPACE+file, texture))
    for (kind, _), name in list(extra_files.items()):
        if kind != "pol": continue
        texture_name = name[4:] if name.startswith("pol_") else name
        if texture_name in texture_ids:
            index = texture_ids[texture_name]
            extra_files[("tex", index)] = texture_name

    native_motions, source_motions = motion_data(args.native, native, False), motion_data(args.source, source, True)
    motions, extra_motions, motion_matches = {}, [], 0
    for name, entries in source_motions.items():
        for mid, ticks, payload in entries:
            matches = [(ni, nt) for ni, nt, np in native_motions.get(name, []) if np == payload and nt == ticks]
            if matches:
                motions[mid] = matches[0][0]
                motion_matches += 1
            else:
                mapped = MOTION_NAMESPACE+mid
                motions[mid] = mapped
                extra_motions.append((mapped, ticks, payload))

    def remap(mem, address, mapping, description, location):
        old = mem.u32(address)
        if old in (0xFFFFFFFF, 0xFFFFFFFE):
            return
        if old in mapping:
            mem.write(address, mapping[old])
        else:
            issue("unmapped_resource", **location, field=description, value=old, address=f"{address:08X}")

    civilian_data, civilian_scripts = prepare_civilian_bank(source, maps, motions, issue)
    common = (args.source/"evt/comevtbl.bin").read_bytes()
    stages = []
    opcode_counts, actor_counts = Counter(), Counter()
    script_count = 0
    # Complete, direct integer motion fields understood by the existing native
    # behavior implementations. Motion-list behaviors are reported separately.
    generic_motion_fields = {18: (4,), 23: (8,), 24: (20, 24)}
    generic_no_motion = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 33, 49}
    # The PC's drill programs use mode 7. Its modes 6 and 8 are the separate
    # tutorial/lesson programs, not dependencies of trnevtbl's thirteen games.
    stage_files = [(mode, f"st{mode+1}evtbl.bin") for mode in range(6)] + [(7, "trnevtbl.bin"), (9, "endevtbl.bin")]
    for mode, event_file in stage_files:
        mem = EventFile(common, (args.source/"evt"/event_file).read_bytes())
        collision_ranges = []
        collision_files = [("coli0.bin", 0x0CECD800)]
        if mode < 6:
            collision_files.append((f"coli{mode+1}.bin", 0x0CECF000))
        elif mode == 7:
            # Source actor 6D (0x4A5F99) loads coli1 for scenes 11/13
            # and coli2 for scenes 3/23 into the same chapter collision arena.
            # Both files are byte-identical to the native data. Other mode-7
            # scenes do not reload collision data and contain no new collider.
            collision_files.extend([(name, 0x0CECF000) for name in ("coli1.bin", "coli2.bin")])
        for name, base in collision_files:
            source_collision = (args.source/"coli"/name).read_bytes()
            if source_collision != (args.native/"coli"/name).read_bytes():
                issue("collision_file_changed", chapter=mode+1, file=name)
            else:
                collision_ranges.append((base, base+len(source_collision)))
        maps["collision"] = {}
        for data in (mem.common, mem.stage):
            for (value,) in struct.iter_unpack("<I", data[:len(data)//4*4]):
                if any(start <= value < end for start, end in collision_ranges):
                    maps["collision"][value] = value
        visited, records = set(), {}
        for scene, variant, start in mem.scripts():
            script_count += 1
            location = {"chapter": mode+1, "scene": scene, "variant": variant}
            pending = [start]
            while pending:
                address = pending.pop()
                for _ in range(4096):
                    if address in visited:
                        break
                    visited.add(address)
                    op = mem.u32(address)
                    length = mem.length(address)
                    if length is None:
                        issue("undecodable_opcode", **location, opcode=op, address=f"{address:08X}")
                        break
                    opcode_counts[op] += 1
                    if op >= 0x60 and op not in SUPPORTED_EXTRA:
                        issue("unsupported_opcode", **location, opcode=f"{op:02X}",
                              name=EXTRA_OPS[op][0], address=f"{address:08X}")
                    # Resource ids are file identities, independent of numbering.
                    if op in (0x52, 0x53, 0x54, 0x55, 0x56, 0x57):
                        kind = "pol" if op < 0x54 else "tex" if op < 0x56 else "motion_file"
                        remap(mem, address+4, maps[kind], kind, location)
                    elif op in (0x50, 0x51):
                        remap(mem, address+4, maps["model"], "model", location)
                    elif op in (0x49, 0x4A):
                        for p in range(address+4, address+length, 4):
                            remap(mem, p, maps["pol"], "pol", location)
                    elif op in (0x30, 0x6C, 0x6D):
                        block = mem.u32(address+4)
                        if block == 0x40:
                            p = address+16
                            mem.write(p, CAMERA_NAMESPACE+mem.u32(p))
                        elif block == 0x60:
                            for p in range(address+12, address+length, 8):
                                mem.write(p, CAMERA_NAMESPACE+mem.u32(p))
                        elif block == 0x20:
                            pose = mem.u32(address+12)
                            source_pose = 0x593158-source.base+pose*24
                            native_pose = 0x576CF0-native.base+pose*24
                            if source.image[source_pose:source_pose+24] != native.image[native_pose:native_pose+24]:
                                issue("camera_preset_schema", **location, address=f"{address:08X}", pose=pose)
                    elif op == 0x73:
                        pending.append(mem.u32(address+8))
                    if op == 0x60:
                        # Each weighted challenge entry names a real spawn0b
                        # record; its motion/list fields need the same typed
                        # conversion as an ordinary event spawn.
                        for p in range(address+20, address+length-4, 8):
                            records.setdefault(mem.u32(p+4), location)
                    if op in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0x4B):
                        for p in range(address+4, address+length, 4):
                            record = mem.u32(p)
                            if record not in (0xFFFFFFFF, 0xFFFFFFFE):
                                records.setdefault(record, location)
                    if op in (0x4E, 0x4F):
                        break
                    address += length
        for record, location in records.items():
            actor = mem.u32(record)
            actor_counts[actor] += 1
            try:
                if convert_actor(mem, record, actor, location, remap, maps, motions, issue):
                    continue
            except (ValueError, TypeError, struct.error) as error:
                issue("actor_record_bounds", **location, actor=f"{actor:02X}",
                      address=f"{record:08X}", detail=str(error))
                continue
            if actor == 0x30:
                data, offset = mem.region(record+36, 4)
                behavior = data[offset+2]
                for field in generic_motion_fields.get(behavior, ()):
                    remap(mem, record+36+field, motions, "enemy_motion", location)
                if behavior not in generic_no_motion and behavior not in generic_motion_fields:
                    issue("enemy_parameter_schema", **location, actor=f"{actor:02X}", behavior=behavior,
                          address=f"{record:08X}")
            else:
                issue("actor_parameter_schema", **location, actor=f"{actor:02X}",
                      address=f"{record:08X}",
                      parameters=[mem.u32(record+36+i*4) for i in range(12)])
        scene_count = sum(1 for _ in mem.scripts())
        # Count scenes, rather than variants, for the eight-byte routing rows.
        scene_count = 1 + max(scene for scene, _, _ in mem.scripts())
        # The port keeps its native world ending id (9). The PC reference
        # starts that same event table in VM mode 11; its mode 9 is the common
        # stage-end halt and must remain intact in comevtbl.
        source_mode = 11 if mode == 9 else mode
        route_address = source.u32(0x5E45B0+source_mode*4)-source.base
        routes = source.image[route_address:route_address+(scene_count+1)*8]
        stages.append((mode, bytes(mem.common), bytes(mem.stage), routes))

    # Source cameras occupy a separate id namespace. Native hardcoded boss paths
    # keep their native ids; source event blocks use the exact imported curves.
    cameras = []
    for index, name in source.names(0x4F0A68, 24).items():
        count = source.i16(0x4DACA0+index*2)
        ids = source.read(source.u32(0x4DAC40+index*4), "<"+"h"*count)
        raw = (args.source/"cam"/name).read_bytes()
        cameras.append(([CAMERA_NAMESPACE+mid for mid in ids], raw))

    resources = []
    for (kind, index), name in sorted(extra_files.items()):
        models, textures, texture_palettes, canonical = (), b"", b"", b""
        if kind == "pol":
            count = source.i16(0x508F20+index*2)
            ids = source.read(source.u32(0x508688+index*4), "<"+"h"*count)
            models = tuple(maps["model"].get(mid, 0) for mid in ids)
            canonical = bytes(source.i16(0x509370+mid*2) == index for mid in ids)
        else:
            count = source.i16(0x587B60+index*2)
            start = source.u32(0x5872C8+index*4)-source.base
            textures = source.image[start:start+count*16]
            if count < 0 or len(textures) != count*16:
                raise ValueError(f"invalid texture metadata: {name}")
            texture_palettes = bytes(source_palette(source, index, i) for i in range(count))
        alias = "pol_typing_"+name[4:] if kind == "pol" and name.startswith("pol_") else "typing_"+name
        resources.append((0 if kind == "pol" else 1, FILE_NAMESPACE+index, alias,
                          (args.source/kind/name).read_bytes(), models, textures, texture_palettes, canonical))
    name_font, name_glyphs = prepare_name_font(source,args.source)
    resources.append(name_font)
    sprites.extend(name_glyphs)
    if len(sprites) != 0xFD4:
        raise ValueError("prepared wordmark sprite identity changed")
    wordmark, wordmark_sprite, branding = prepare_wordmark(source,args.source)
    resources.append(wordmark)
    sprites.append(wordmark_sprite)
    palettes = []
    address = 0x5B48E0
    while source.i16(address+2) == 16 and source.u32(address+8) > 0:
        start = source.u32(address+4)-source.base
        palettes.append(source.image[start:start+32])
        address += 12
    audio, audio_files, dialogue, native_audio_map = prepare_audio(source, native, args.source, args.native)

    report = {
        "format": "TOTDREF1", "complete": not issues,
        "source_executable_sha256": hashlib.sha256((args.source/"Tod_e.exe").read_bytes()).hexdigest(),
        "native_executable_sha256": hashlib.sha256((args.native/"Hod2.exe").read_bytes()).hexdigest(),
        "model_identity_sha256": hashlib.sha256(args.model_map.read_bytes()).hexdigest(),
        "scripts": script_count, "chapters": 6,
        "opcodes": {f"{key:02X}": count for key, count in sorted(opcode_counts.items())},
        "actors": {f"{key:02X}": count for key, count in sorted(actor_counts.items())},
        "motion_payload_and_duration_matches": motion_matches,
        "additional_motion_clips": len(extra_motions),
        "additional_resources": len(resources), "palette_banks": len(palettes), "source_sprites": len(sprites),
        "branding": branding,
        "sound_ids": len(audio), "additional_sound_payloads": len(audio_files),
        "enemy_model_bindings": len(enemy_model_bindings),
        "resource_mappings": {name: {str(key): value for key, value in sorted(mapping.items())}
                              for name, mapping in maps.items() if name != "collision"},
        "issues": issues,
    }
    out = bytearray(b"TOTDREF1")
    def word(value):
        out.extend(struct.pack("<I", value & 0xFFFFFFFF))
    def blob(data):
        word(len(data)); out.extend(data)
    word(5); word(not issues)
    word(len(issues)); word(len(stages)); word(len(cameras)); word(len(motions)); word(len(extra_motions))
    word(len(resources)); word(len(palettes))
    for row in issues:
        blob(json.dumps(row, sort_keys=True).encode("utf-8"))
    blob(common)
    for mode, stage_common, data, routes in stages:
        word(mode); blob(stage_common); blob(data); blob(routes)
    for ids, data in cameras:
        word(len(ids))
        for mid in ids: word(mid)
        blob(data)
    for old, mapped in sorted(motions.items()):
        word(old); word(mapped)
    for mapped, ticks, payload in extra_motions:
        word(mapped); word(ticks); blob(payload)
    for kind, file_id, name, data, models, textures, texture_palettes, canonical in resources:
        word(kind); word(file_id); blob(name.encode("ascii")); blob(data)
        word(len(models))
        for model in models: word(model)
        blob(textures)
        blob(texture_palettes)
        blob(canonical)
    for palette in palettes: out.extend(palette)
    word(0x02000000); blob(civilian_data); word(len(civilian_scripts))
    for address in civilian_scripts: word(address)
    word(len(sprites))
    for file, texture in sprites: word(file); word(texture)
    # Challenge rewards use the same three item presentations as 0x0040ADA0.
    word(3)
    for item in (1, 9, 4):
        word(item); word(maps["model"][source.u32(0x592770+item*4)])
        word(SPRITE_NAMESPACE+source.u32(0x592718+item*4))
        out.extend(source.image[0x5927C8+item*12-source.base:0x5927C8+(item+1)*12-source.base])
    word(len(maps["model"]))
    for old, mapped in sorted(maps["model"].items()): word(old); word(mapped)
    word(len(audio))
    for sound, flags, path in audio:
        word(sound); word(flags); blob(path.encode("ascii"))
    word(len(native_audio_map))
    for old, mapped in sorted(native_audio_map.items()): word(old); word(mapped)
    word(len(audio_files))
    for path, data in sorted(audio_files.items()): blob(path.encode("ascii")); blob(data)
    for data in dialogue: blob(data)
    word(len(enemy_model_bindings))
    for kind, node, native_model, prepared_model in enemy_model_bindings:
        word(kind); word(node); word(native_model); word(prepared_model)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(out)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key not in ("issues", "resource_mappings")}, indent=2))
    print("Unresolved conversions:", dict(Counter(row["kind"] for row in issues)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
