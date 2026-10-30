"""Typed motion fields of generic enemy records and their authored motion lists.

The layouts are consumed by world_enemy_beh.cpp and world_st*_enemy.cpp. No
coordinate, timing, loop count, flag or table selector is treated as a motion id.
"""
import struct


def convert_enemy(mem, record, location, remap, maps, motions, issue):
    fields = getattr(mem, "_typing_enemy_fields", None)
    if fields is None:
        fields = mem._typing_enemy_fields = set()
    data, offset = mem.region(record + 0x24, 4)
    kind, behavior, following = data[offset], data[offset+2], data[offset+3]
    p = record + 0x24

    def s16(address):
        data, offset = mem.region(address, 2)
        return struct.unpack_from("<h", data, offset)[0]

    def motion(address, width=4):
        key = ("motion", address, width)
        if key in fields:
            return
        fields.add(key)
        value = s16(address) if width == 2 else mem.u32(address)
        if value in (None, -1, 0xFFFFFFFF) or value == 0:
            return
        if width == 4:
            remap(mem, address, motions, "enemy_motion", location)
        elif value in motions and 0 <= motions[value] <= 0x7FFF:
            data, offset = mem.region(address, 2)
            struct.pack_into("<h", data, offset, motions[value])
        else:
            issue("unmapped_resource", **location, actor="30", field="enemy_motion",
                  value=value, address=f"{address:08X}")

    def entries(address, stride=8, flag_entries=False):
        for _ in range(512):
            key = ("list", address, stride)
            if key in fields:
                return
            fields.add(key)
            value = s16(address)
            if value < 1:
                return
            tick = s16(address+6)
            motion(address, 2)
            address += stride
            if flag_entries and tick >= 0:
                # Behavior 36 hands its next entry to behavior 35 after an
                # entry with a nonnegative tick; the latter uses eight bytes.
                entries(address)
                return
        issue("enemy_motion_list_limit", **location, address=f"{address:08X}")

    def scripted(b, address):
        if address in (None, 0, 0xFFFFFFFF):
            return
        if b == 34:
            # distance float, u16 flag, motion/start, then behavior-35 entries.
            motion(address+6, 2)
            entries(address+10)
        elif b == 35:
            entries(address)
        elif b == 36:
            entries(address, 10, True)
        elif b == 37:
            # Twelve scalar words define the held object's type, state, pose,
            # spin and velocity; animation entries begin at byte 0x30.
            entries(address+0x30)
        elif b == 38:
            entries(address+12)
        elif b in (40, 41):
            motion(address+12, 2)
            entries(address+16)
        elif b == 43:
            pass  # loops/tick only; all motion choices are native constants

    motion_fields = {17: (4,), 18: (4,), 19: (4,), 20: (4,), 21: (4,),
                     23: (8,), 24: (20, 24), 27: (8,), 31: (8, 16)}
    no_motion = set(range(16)) | {25, 26, 29, 30, 32, 33, 43, 45, 49}
    for field in motion_fields.get(behavior, ()):
        motion(p+field)
    list_behaviors = {34, 35, 36, 37, 38, 40, 41, 43}
    if behavior in list_behaviors:
        scripted(behavior, mem.u32(p + (8 if behavior == following else 4)))
    if following in list_behaviors and following != behavior:
        scripted(following, mem.u32(p+8))
    if behavior == 39:
        # The civilian's command 0x1A chooses the initial script behavior. This
        # is a control-flow dependency, not something inferable from list bytes.
        initial = getattr(mem, "_typing_enemy_initial_script", {}).get(record)
        if initial in list_behaviors:
            scripted(initial, mem.u32(p+4))
        elif initial == 49:
            pass  # civilian command kills the hidden child immediately
        else:
            issue("enemy_trigger_behavior", **location, actor="30", address=f"{record:08X}")
    elif behavior not in no_motion and behavior not in motion_fields and behavior not in list_behaviors:
        issue("enemy_parameter_schema", **location, actor="30", behavior=behavior,
              address=f"{record:08X}")
    if kind in (2, 3):
        key = ("collision", p+16)
        if key not in fields:
            fields.add(key)
            remap(mem, p+16, maps.get("collision", {}), "enemy_collision", location)
    return True
