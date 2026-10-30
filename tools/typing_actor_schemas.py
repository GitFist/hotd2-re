"""Authored actor parameter fields consumed by the native actor implementations.

Offsets below are bytes in the event record, not guessed integer-ID matches.
Scalar kinds, flags, timings, coordinates, skeletons and table-row selectors stay
unchanged. Script commands are converted by their own operand schemas.
"""
import struct
from typing_enemy_schemas import convert_enemy

CAMERA_NAMESPACE = 0x4000  # authored actor camera fields include signed 16-bit IDs
CIVILIAN_NAMESPACE = 0x02000000


def prepare_civilian_bank(source, maps, motions, issue):
    """Convert the reference's data-only civilian programs and their local data."""
    base, end, count = 0x50E400, 0x512F30, 67
    data = bytearray(source.image[base-source.base:end-source.base])
    roots = [source.u32(0x512F30 + index * 4) for index in range(count)]
    three = {0,5,0xD,0x13,0x16,0x1A,0x1F,0x21,0x24,0x25,0x26}
    callbacks = {0:0, 0x482520:0x48D9F0, 0x4825C0:0x48DA90,
                 0x4826A0:0x48DB90, 0x4826E0:0x48DBD0}
    seen = set()
    commands = {}

    def put(address, value):
        if not base <= address <= end - 4:
            raise ValueError(f"civilian program operand outside bank: {address:08X}")
        struct.pack_into("<I", data, address-base, value & 0xFFFFFFFF)

    def pointer(address):
        value = source.u32(address)
        if base <= value < end:
            put(address, CIVILIAN_NAMESPACE + value - base)
        elif (value & 0xFFF80000) == 0x0CE80000:
            put(address, value + 0xF3AC1A00)
        elif value not in (0, 1, 0xFFFFFFFE, 0xFFFFFFFF):
            issue("civilian_data_pointer", address=f"{address:08X}", value=value)

    # Alternate and jump programs are not entries in the builtin root table.
    # They contain the same motion/camera/pointer operands as the main program.
    pending = list(enumerate(roots))
    pending_at = 0
    while pending_at < len(pending):
        index, root = pending[pending_at]
        pending_at += 1
        address = root
        for _ in range(4096):
            if address in seen:
                break
            if not base <= address <= end - 8:
                issue("civilian_program_bounds", index=index, address=f"{address:08X}")
                break
            seen.add(address)
            op = source.u32(address)
            if op > 0x2D:
                issue("civilian_program_opcode", index=index, address=f"{address:08X}", opcode=op)
                break
            length = 16 if op == 1 else 12 if op in three else 8
            if op in (0, 1):
                motion = source.u32(address + 4)
                if motion in motions:
                    put(address + 4, motions[motion])
                else:
                    issue("civilian_program_motion", index=index, address=f"{address+4:08X}", value=motion)
            elif op == 0xD:
                camera = source.u32(address + 4)
                if camera != 0xFFFFFFFF:
                    put(address + 4, CAMERA_NAMESPACE + camera)
            elif op == 0x10:
                callback = source.u32(address + 4)
                if callback in callbacks:
                    put(address + 4, callbacks[callback])
                else:
                    issue("civilian_program_callback", index=index, address=f"{address+4:08X}", value=callback)
                length = 12 if callback == 0x4826A0 else 20 if callback == 0x4826E0 else 8
            elif op in (5, 6, 0xE, 0xF, 0x18, 0x1E, 0x22, 0x26):
                if op in (0xE, 0xF, 0x1E):
                    target = source.u32(address + 4)
                    if target:
                        pending.append((index, target))
                pointer(address + 4)
            elif op == 0x1F:
                for operand in (4, 8):
                    target = source.u32(address + operand)
                    if target:
                        pending.append((index, target))
                pointer(address + 4)
                pointer(address + 8)
            elif op == 0x24:
                pointer(address + 8)
            elif op == 0x2B:
                length = 24
            if address + length > end:
                issue("civilian_program_bounds", index=index, address=f"{address:08X}", length=length)
                break
            commands[address] = (op, length)
            if op == 0x2D:
                break
            address += length
        else:
            issue("civilian_program_limit", index=index)
    child_behaviors = {}
    for index, root in enumerate(roots):
        address, choices = root, set()
        for _ in range(4096):
            command = commands.get(address)
            if command is None or command[0] == 0x2D:
                break
            if command[0] == 0x1A:
                choices.add(source.u32(address + 4))
            address += command[1]
        if len(choices) == 1:
            child_behaviors[index] = choices.pop()
        elif choices:
            issue("civilian_trigger_choice", index=index, behaviors=sorted(choices))
    maps["civilian_child_behavior"] = child_behaviors
    maps["civilian_script"] = {index:index for index in range(count)}
    return bytes(data), [CIVILIAN_NAMESPACE + address-base for address in roots]


def convert_actor(mem, record, actor, location, remap, maps, motions, issue):
    try:
        return _convert_actor(mem, record, actor, location, remap, maps, motions, issue)
    except (ValueError, struct.error) as error:
        issue("actor_record_bounds", **location, actor=f"{actor:02X}",
              address=f"{record:08X}", detail=str(error))
        return True


def _convert_actor(mem, record, actor, location, remap, maps, motions, issue):
    converted = getattr(mem, "_typing_actor_fields", None)
    if converted is None:
        converted = mem._typing_actor_fields = set()
    key = ("record", record)
    if key in converted:
        return True
    converted.add(key)

    def read16(address):
        data, off = mem.region(address, 2)
        return struct.unpack_from("<h", data, off)[0]

    def byte(address):
        data, off = mem.region(address, 1)
        return data[off]

    def field(address, mapping, description, width=4):
        field_key = (description, address)
        if field_key in converted:
            return
        converted.add(field_key)
        old = read16(address) if width == 2 else mem.u32(address)
        if old is None:
            raise ValueError(f"missing {description} operand at {address:08X}")
        if old in (-1, 0xFFFFFFFF):
            return
        if width == 4:
            remap(mem, address, mapping, description, location)
        elif old in mapping:
            new = mapping[old]
            if not 0 <= new <= 0x7FFF:
                issue("actor_field_overflow", **location, actor=f"{actor:02X}",
                      address=f"{address:08X}", field=description, value=new)
                return
            data, off = mem.region(address, 2)
            struct.pack_into("<h", data, off, new)
        else:
            issue("unmapped_resource", **location, actor=f"{actor:02X}",
                  address=f"{address:08X}", field=description, value=old)

    def camera(address, width=2):
        value = read16(address) if width == 2 else mem.u32(address)
        if value is None:
            raise ValueError(f"missing camera operand at {address:08X}")
        if value >= 0 and value != 0xFFFFFFFF:
            field(address, {value: CAMERA_NAMESPACE + value}, "actor_camera", width)

    def collision(address):
        value = mem.u32(address)
        if value is None:
            raise ValueError(f"missing collision operand at {address:08X}")
        if value == 0xFFFFFFFF:
            return
        if "collision" in maps:
            field(address, maps["collision"], "collision_mesh")
        else:
            issue("collision_address_mapping", **location, actor=f"{actor:02X}",
                  address=f"{address:08X}", value=value)

    def npc_script(start):
        pending = [start]
        while pending:
            address = pending.pop()
            for _ in range(4096):
                script_key = ("npc_command", address)
                if script_key in converted:
                    break
                converted.add(script_key)
                try:
                    op, sub = read16(address), read16(address + 2)
                except ValueError:
                    issue("npc_script_pointer", **location, address=f"{address:08X}")
                    break
                if op == -1 or op == 0x12:
                    break
                if op in (2, 3):
                    field(address + 4, motions, "npc_motion", 2)
                if (op in (0, 1, 4) and sub == 1) or (op == 0xB and sub in (1, 2)):
                    camera(address + 4)
                if op == 0xF:
                    pending.append(mem.u32(address + 4))
                    break
                if not 0 <= op <= 0x11:
                    issue("npc_script_command", **location, opcode=op, address=f"{address:08X}")
                    break
                length = 16 if op in (7, 8) or (op == 4 and sub == 4) else 8
                address += length
            else:
                issue("npc_script_limit", **location, address=f"{start:08X}")

    # Reference-only game-flow actors have compact records (as little as eight
    # bytes). Their constructors consume globals/state selectors, with no
    # authored asset operands; reading a full position/parameter record here
    # would run into the following record or the end of the common table.
    if actor in {0x54, 0x55, 0x56, 0x60, 0x61, 0x63, 0x64, 0x65, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D}:
        return True

    p = record + 0x24
    state_word = mem.u32(record + 0x20)
    if state_word is None or mem.u32(record + 4) is None:
        issue("actor_record_bounds", **location, actor=f"{actor:02X}",
              address=f"{record:08X}", required_bytes=36)
        return True
    state = state_word >> 16
    flags = mem.u32(record + 4)
    if actor == 0x11:
        # Frog: skeleton, initial motion, camera/tick, screen edge angle, then
        # a signed-short command stream. Only wait commands carry more ids;
        # the remaining command operands are angles or scalar state selectors.
        field(p + 2, motions, "frog_motion", 2)
        camera(p + 4)
        command = p + 10
        for _ in range(128):
            op = read16(command)
            if op == -1:
                return True
            if not 0 <= op <= 8:
                issue("frog_command", **location, opcode=op, address=f"{command:08X}")
                return True
            if op == 0:
                camera(command + 2)
                field(command + 6, motions, "frog_wait_motion", 2)
                command += 8
            else:
                command += 4 if op == 3 else 2
        issue("frog_command_limit", **location, address=f"{p+10:08X}")
        return True
    if actor == 0x1B:
        # The PC spawner count means the highest alternative index, then
        # pointers to two consecutive -1-terminated actor lists (enemy/civilian).
        count = mem.u32(p)
        if count is None or count > 15:
            issue("drill_spawner_count", **location, address=f"{p:08X}", value=count)
            return True
        for choice in range(count + 1):
            address = mem.u32(p + 4 + choice * 4)
            if not address:
                issue("drill_spawner_list", **location, address=f"{p+4+choice*4:08X}")
                continue
            for _list in range(2):
                for _ in range(64):
                    child = mem.u32(address)
                    address += 4
                    if child == 0xFFFFFFFF:
                        break
                    child_type = mem.u32(child) if child else None
                    if child_type is None or not convert_actor(mem, child, child_type, location, remap, maps, motions, issue):
                        issue("drill_spawner_actor", **location, address=f"{child or 0:08X}", actor=child_type)
                else:
                    issue("drill_spawner_list_limit", **location, address=f"{address:08X}")
        return True
    if actor == 0x41:
        kind = byte(p + 1)
        # These constructors use record.state as an explicit model ID. Other
        # komono states are lifetimes, branch selectors or table-row indices.
        if kind in {5, 12, 31, 33, 51, 53, 54, 57, 69, 73, 74, 75, 76}:
            field(record + 0x22, maps["model"], "komono_model", 2)
        return 0 <= kind <= 78
    if actor == 0x44:
        if state in {0, 12}:
            field(p + 4, maps["model"], "prop_model")
        elif state in {1, 4, 5, 6, 7, 9, 10, 11, 13, 14, 17}:
            field(p + 4, maps["model"], "prop_model", 2)
        if state in {0, 1, 2, 3, 4, 5, 6, 7, 11, 12, 13, 17}:
            collision(p + 8)
        return 0 <= state <= 17
    if actor == 0x25:
        # world_npc.cpp: parts-list pointer P8, script pointer P12; the latter
        # is an s16 command stream with explicit motion and camera operands.
        if not (flags & 0x02000000):
            camera(p + 2)
        script = mem.u32(p + 12)
        if script:
            npc_script(script)
        return True
    if actor == 0x14:
        camera(p + 0x30)  # Hierophant removal camera; the following s16 is its tick.
        return True
    if actor == 0x19:
        camera(p + 0x40)  # Strength removal camera; preserve its adjacent tick.
        return True
    if actor == 0x10:
        # Civilian params contain a builtin script INDEX, not a motion ID.
        # Matching/importing that exe-resident bank is a separate contract.
        camera(p + 2)
        script_index = byte(p + 1)
        if "civilian_script" in maps and script_index in maps["civilian_script"]:
            data, off = mem.region(p + 1, 1)
            data[off] = maps["civilian_script"][script_index]
        else:
            issue("civilian_builtin_script", **location, index=script_index,
                  address=f"{record:08X}")
        # Child enemy records are explicit pointers after the child count P12.
        count = mem.u32(p + 12)
        if count is None:
            raise ValueError(f"missing civilian child count at {p+12:08X}")
        if count <= 12:
            for i in range(count):
                child = mem.u32(p + 16 + i * 4)
                if child is None:
                    raise ValueError(f"missing civilian child pointer at {p+16+i*4:08X}")
                if child:
                    child_type = mem.u32(child)
                    if child_type is None:
                        raise ValueError(f"missing civilian child record at {child:08X}")
                    initial = maps.get("civilian_child_behavior", {}).get(script_index)
                    if initial is not None:
                        initial_scripts = getattr(mem, "_typing_enemy_initial_script", None)
                        if initial_scripts is None:
                            initial_scripts = mem._typing_enemy_initial_script = {}
                        initial_scripts[child] = initial
                    convert_actor(mem, child, child_type, location, remap, maps, motions, issue)
        else:
            issue("civilian_child_count", **location, address=f"{p+12:08X}", value=count)
        return True
    if actor == 0x31:
        behavior = byte(p + 2)
        if behavior in {19, 23, 28}:
            field(p + 4, motions, "leaper_motion")
        return 0 <= behavior <= 34
    if actor == 0x20:
        if read16(p + 6) != 0:
            field(p + 6, motions, "background_motion", 2)
        camera(p + 2)
        return True
    if actor == 0x24:
        field(p + 10, motions, "background_motion", 2)
        if not (flags & 0x02000000):
            camera(p + 6)
        return True
    if actor == 0x22:
        field(p + 2, motions, "rider_motion", 2)
        if byte(p + 1) in {1, 2}:
            camera(p + 6)
            child = mem.u32(p + 16)
            if child:
                child_type = mem.u32(child)
                if child_type != 0x23:
                    issue("rider_mount_record", **location, address=f"{child:08X}", actor=child_type)
                else:
                    convert_actor(mem, child, child_type, location, remap, maps, motions, issue)
        return True
    if actor == 0x23:
        field(p + 2, motions, "mount_motion", 2)
        camera(p + 6)
        return True
    if actor == 0x13:
        field(p, maps["model"], "vehicle_model", 2)
        return True
    if actor == 0x18:
        camera(p + 12)
        return True
    if actor == 0x29:
        camera(p)
        return 0 <= state <= 2
    if actor == 0x12:
        field(p, maps["model"], "held_model", 2)
        collision(p + 4)
        camera(p + 10)
        return True
    if actor == 0x33:
        if state in {1, 2, 4, 99}:
            field(p, maps["model"], "scripted_model")
        if state in {1, 4}:
            collision(p + 4)
        if state == 1:
            camera(p + 12, 4)
        return 0 <= state <= 13 or state == 99
    if actor == 0x30:  # recursively referenced children of civilian records
        return convert_enemy(mem, record, location, remap, maps, motions, issue)
    # These actors' authored parameters select native skeleton/behavior rows,
    # route variants, counts, coordinates, timings, event flags or captions;
    # their model/motion choices are in the native actor implementation.
    return actor in {0x11, 0x14, 0x15, 0x16, 0x17, 0x19, 0x21, 0x26, 0x27,
                     0x28, 0x2A, 0x2B, 0x2D, 0x32, 0x40, 0x42, 0x43,
                     0x45, 0x46, 0x51, 0x52, 0x53}
