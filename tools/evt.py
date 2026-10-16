"""HOTD2 event scripts (evt\\*.bin): loading/relocation, script selection, disassembly, trace validation.

Evidence: Evt_LoadCommonTable 0x00413070 / Evt_LoadTable 0x00413160 / Evt_Relocate 0x00413120,
Evt_GetScript 0x0045EB90 (comevtbl[mode][scene][variant]), Evt_Run 0x0045ECC0 (`table[*script]()` until the
yield flag 0x009C8EA0), opcode table 0x005931D8 (96 handlers), resource queue 0x0041D5D0.. (ring 0x007DA220).

Memory layout (PC addresses after relocation; DC 0x0CE8xxxx words get +0xF3AC1A00):
  comevtbl.bin  -> 0x00977200        stage table (0x00579928[mode] selects 0x004D1C7C[n]) -> 0x00977400

Opcode lengths are from the handlers and confirmed by the runtime trace (consecutive executed addresses).
Semantics marked (?) are hypotheses.

Usage: python tools/evt.py --trace build/scene_capture_events.txt     validate decoder vs a runtime trace
       python tools/evt.py --dis <hex address> [count]                 disassemble (needs the trace's stage table)
"""
import argparse
import struct
import sys

COMMON_BASE = 0x00977200
STAGE_BASE = 0x00977400
RELOC_MASK, RELOC_TAG, RELOC_ADD = 0xFFF80000, 0x0CE80000, 0xF3AC1A00

# All 96 opcodes (handler table 0x005931D8): name, length kind, note. Kinds: int = fixed bytes; "list" = -1
# terminated; "env20" = 0x20/0x24 (sub 5/9 -> 20, sub <= 10 -> 12, else 8); "envi" = 0x21/0x23/0x25/0x27
# (sub <= 10 -> 16, else 8); "block30"; "pp" = up to a -2 word (0x49-0x4B); "invalid" = handler never advances.
# Semantics: notes/agents/events.md. Names with '?' are hypotheses.
_NAMES = """
00 invalid_00 invalid | 01 route1_spawn09 list | 02 route1_spawn0a list | 03 route1_spawn0b list
04 route1_spawn0c list | 05 route2_spawn09 list | 06 route2_spawn0a list | 07 route2_spawn0b list
08 route2_spawn0c list | 09 spawn09 list | 0A spawn0a list | 0B spawn0b list | 0C spawn0c list
0D spawn0b_noskip list | 0E set_table_2be0 24 | 0F set_list_8e40 list | 10 set_ptr_1a14 list
11 set_ptr_34d8 list | 12 set_list_8e40_r2 list | 13 set_params_13 list | 14 set_2bb4 8 | 15 set_8d4c 8
16 set_71d4 16 | 17 light_angle_task 16 | 18 light_angles 12 | 19 light_angles_b1 12 | 1A set_8e58 8
1B set_2c34 8 | 1C set_a090 8 | 1D set_8e50 8 | 1E set_8a78 8 | 1F set_a0f4 8 | 20 env_set env20
21 env_interp envi | 22 env_stop 8 | 23 env_interp_frames envi | 24 env_set_b1 env20 | 25 env_interp_b1 envi
26 env_stop_b1 8 | 27 env_interp_frames_b1 envi | 28 section_preload 8 | 29 section_switch 8
2A invalid_2a invalid | 2B player_bonus_2b? 4 | 2C skippable 8 | 2D voice 8 | 2E sound_stop_if_skipping 4
2F set_5c48 8 | 30 cam_queue block30 | 31 cam_start 8 | 32 cam_start_ready 8 | 33 cam_set_state 12
34 invalid_34 invalid | 35 cam_roll 8 | 36 set_70f4 8 | 37 rail_force 8 | 38 sound 8 | 39 sound_repeat 16
3A sound_noskip 8 | 3B sound_repeat_noskip 16 | 3C invalid_3c invalid | 3D nop16 16 | 3E nop8 8 | 3F nop4 4
40 wait_queue 4 | 41 wait_camera 8 | 42 wait_frames 8 | 43 wait_7006 8 | 44 wait_enemies 8 | 45 wait_flag 8
46 wait_a0e8 8 | 47 wait_cam_ready 4 | 48 set_flag 8 | 49 pp_load_pol pp | 4A pp_unload_pol pp
4B pp_spawn0b pp | 4C invalid_4c invalid | 4D stage_reset 4 | 4E halt 4 | 4F next_variant 4 | 50 load_model 8
51 unload_model 8 | 52 load_pol 8 | 53 unload_pol 8 | 54 load_tex 8 | 55 unload_tex 8 | 56 load_mot 8
57 request_type9? 8 | 58 flush_all 4 | 59 flush_lt8 4 | 5A flush_8_10 4 | 5B nop4_5b 4 | 5C nop4_5c 4
5D snd_load_pack 12 | 5E snd_free_pack 8 | 5F bgm 20
"""
OPS = {}
for _item in _NAMES.replace("\n", " | ").split("|"):
    _p = _item.split()
    if _p:
        OPS[int(_p[0], 16)] = (_p[1], int(_p[2]) if _p[2].isdigit() else _p[2], "")

def reloc(w):
    return (w + RELOC_ADD) & 0xFFFFFFFF if (w & RELOC_MASK) == RELOC_TAG else w


class Memory:
    def __init__(self, stage_file):
        self.mem = {}
        for base, path in ((COMMON_BASE, "game/evt/comevtbl.bin"), (STAGE_BASE, f"game/evt/{stage_file}")):
            d = open(path, "rb").read()
            for i in range(len(d) // 4):
                self.mem[base + 4 * i] = reloc(struct.unpack_from("<I", d, 4 * i)[0])

    def u32(self, a):
        return self.mem.get(a)


def length(mem, addr):
    op = mem.u32(addr)
    if op not in OPS:
        return None
    kind = OPS[op][1]
    if isinstance(kind, int):
        return kind
    if kind in ("list", "pp"):
        end = 0xFFFFFFFF if kind == "list" else 0xFFFFFFFE
        n = 1
        while mem.u32(addr + 4 * n) not in (end, None):
            n += 1
        return 4 * (n + 1)
    if kind == "env20":
        sub = mem.u32(addr + 4)
        return 20 if sub in (5, 9) else (12 if sub is not None and sub <= 10 else 8)
    if kind == "envi":
        sub = mem.u32(addr + 4)
        return 16 if sub is not None and sub <= 10 else 8
    if kind == "invalid":
        return None
    if kind == "block30":
        n = mem.u32(addr + 4)
        n = n - (1 << 32) if n & 0x80000000 else n
        return int((n + ((n >> 31) & 15)) / 16) * 4 + 8 if n >= 0 else None
    return None


def disassemble(mem, addr, count):
    out = []
    for _ in range(count):
        op = mem.u32(addr)
        n = length(mem, addr)
        if n is None:
            out.append((addr, op, None, "UNKNOWN"))
            break
        args = [mem.u32(addr + 4 * i) for i in range(1, n // 4)]
        out.append((addr, op, args, OPS[op][0]))
        addr += n
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace")
    ap.add_argument("--stage", default="advevtbl.bin")
    ap.add_argument("--dis")
    ap.add_argument("count", nargs="?", type=int, default=40)
    args = ap.parse_args()
    mem = Memory(args.stage)
    if args.dis:
        for a, op, ar, name in disassemble(mem, int(args.dis, 16), args.count):
            print(f"{a:08X}  {op:02X} {name:18} {' '.join(f'{x:X}' for x in (ar or []))}")
    if args.trace:
        ev = [l.split() for l in open(args.trace) if l.startswith("E ")]
        ok_op = bad_op = ok_len = bad_len = unknown = 0
        resources = []
        for i, e in enumerate(ev):
            op, addr = int(e[2]), int(e[3], 16)
            if mem.u32(addr) != op:
                bad_op += 1
                continue
            ok_op += 1
            n = length(mem, addr)
            if n is None:
                unknown += 1
                continue
            if i + 1 < len(ev):
                d = int(ev[i + 1][3], 16) - addr
                if 0 < d <= 64:  # sequential (not a branch / repeat)
                    if d == n:
                        ok_len += 1
                    else:
                        bad_len += 1
                        print(f"  length mismatch op {op:02X} at {addr:08X}: decoder {n}, trace {d}")
            if 0x50 <= op <= 0x56:
                resources.append((int(e[1]), OPS[op][0], mem.u32(addr + 4)))
        print(f"trace events: {len(ev)}; opcode at traced address matches memory image: {ok_op} (mismatch {bad_op}); "
              f"length agrees with next sequential address: {ok_len} (mismatch {bad_len}); unknown-length opcodes: {unknown}")
        print("resource requests in execution order (frame, op, arg):")
        for r in resources:
            print("  ", r[0], r[1], r[2])


if __name__ == "__main__":
    main()
