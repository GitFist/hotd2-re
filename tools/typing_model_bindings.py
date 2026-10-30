"""Typed model bindings for native animation code and the supplied PC reference.

Pairs come from identical skeleton slots, wound stages, attachment entries and
verified boss model cycles. POL list order is never used to infer identity.
"""
from __future__ import annotations

import struct


def prepare_model_bindings(source, native, model_map):
    """Return (kind, bone, native model, prepared model) data records.

    Bone -1 applies to the kind's explicit dynamic/attachment model references.
    Native model values stay in animation state, preserving its comparisons.
    """
    converted = {int(key): int(value) for key, value in model_map.items()}
    rows = {}

    def mapped(model):
        if model == 0:
            return 0
        if model not in converted:
            raise ValueError(f"skeleton model {model:04X} has no prepared identity")
        return converted[model]

    def bind(kind, node, old, new):
        if old <= 2 or old == new:
            return
        key = (kind, node, old)
        if key in rows and rows[key] != new:
            raise ValueError(f"conflicting semantic model binding {key}: {rows[key]:04X}/{new:04X}")
        rows[key] = new

    def skin(exe, definition):
        # LoadSkinDef's decoded data, excluding executable addresses and the
        # model id. These are the values SkinDeform uses at render time.
        def data(address, count):
            offset = address - exe.base
            if offset < 0 or count < 0 or offset + count > len(exe.image):
                raise ValueError("attachment skin data outside executable")
            return exe.image[offset:offset+count]

        vt = exe.u32(definition+4)
        offset, size = exe.u32(vt+12), exe.u32(vt+16)
        a, b = exe.i16(vt+20), exe.i16(vt+22)
        stride = a*2 + b*4
        if stride <= 0:
            return None  # the same empty dummy definition occurs in both inputs
        if size > 0x100000 or a < 0 or b < 0:
            raise ValueError("invalid attachment vertex layout")
        count = size // stride
        offsets = []
        for vertex in range(count):
            row = []
            for occurrence in range(b):
                value = exe.u32(vt+offset+vertex*stride+a*2+occurrence*4)
                if value == 0xFFFFFFFF:
                    break
                row.append(value)
            offsets.append(tuple(row))
        records = []
        for record in range(4):
            address = definition+8+record*12
            entries = exe.u32(address)
            if entries > 0x10000:
                raise ValueError("invalid attachment source-vector count")
            records.append((entries, data(exe.u32(address+4), entries*24) if entries else b"",
                            data(exe.u32(address+8), count) if entries else b""))
        return tuple(offsets), tuple(records)

    def skeleton(exe, table, kind):
        nodes, visited = [], set()

        def visit(address, parent, depth):
            if address in visited or depth > 64 or len(nodes) > 512:
                raise ValueError(f"invalid skeleton {kind:02X} hierarchy")
            visited.add(address)
            raw = exe.image[address-exe.base:address-exe.base+24]
            if len(raw) != 24:
                raise ValueError(f"skeleton {kind:02X} node outside executable data")
            model, = struct.unpack_from("<I", raw)
            bone, count = struct.unpack_from("<hh", raw, 20)
            if count < 0 or count > 32:
                raise ValueError(f"invalid skeleton {kind:02X} child count")
            index = len(nodes)
            nodes.append((model, bone, parent, raw[4:]))
            for child in range(count):
                visit(exe.u32(address+24+child*4), index, depth+1)

        visit(exe.u32(table+kind*4), -1, 0)
        return nodes

    for kind in range(86):
        source_nodes = skeleton(source, 0x4FE298, kind)
        native_nodes = skeleton(native, 0x4E0430, kind)
        if source.i16(0x4FE590+kind*2) != native.i16(0x4E0724+kind*2) or len(source_nodes) != len(native_nodes):
            raise ValueError(f"skeleton {kind:02X} rotation-slot count changed")
        for source_node, native_node in zip(source_nodes, native_nodes):
            sm, bone, parent, shape = source_node
            nm, native_bone, native_parent, native_shape = native_node
            if (bone, parent, shape) != (native_bone, native_parent, native_shape):
                raise ValueError(f"skeleton {kind:02X} bone {bone} structure changed")
            bind(kind, bone, nm, mapped(sm))

        # The attachment entry order and count agree for every shared kind.
        # Their source vertex/group data also match all 103 usable native defs.
        source_block = source.u32(0x555E58+kind*4)
        native_block = native.u32(0x52ED08+kind*4)
        count = source.u32(source_block)
        if count != native.u32(native_block) or count > 32:
            raise ValueError(f"skeleton {kind:02X} attachment count changed")
        for entry in range(count):
            sp = source.u32(source.u32(source_block+4)+entry*4)
            np = native.u32(native.u32(native_block+4)+entry*4)
            if bool(sp) != bool(np):
                raise ValueError(f"skeleton {kind:02X} attachment {entry} presence changed")
            if sp:
                if skin(source, sp) != skin(native, np):
                    raise ValueError(f"skeleton {kind:02X} attachment {entry} deformation data changed")
                bind(kind, -1, native.u32(np), mapped(source.u32(sp)))

    # Enemy_ApplyShot 0x00436300/0x00409430 index these arrays by bone*6+stage.
    # Values 0/1/2 are control fields; only corresponding model fields are bound.
    for kind in range(64):
        sp = source.u32(0x4E4888+kind*4)
        np = native.u32(0x4C7160+kind*4)
        for field in range(native.i16(0x4E0724+kind*2)*6):
            sm = source.i16(sp+field*2)
            nm = native.i16(np+field*2)
            if sm > 2 and nm > 2:
                bind(kind, field//6, nm, mapped(sm))
            elif (sm > 2) != (nm > 2):
                raise ValueError(f"skeleton {kind:02X} wound field {field} changed roles")

    # Strength's head damage table has nine model stages. Node 5 cycles the
    # saw blades 475..478 (source48AF19/48C06D), and node 8 swaps hand models.
    for stage in range(9):
        bind(0x4A, 2, native.i16(0x5709A0+stage*2), mapped(source.i16(0x5142E8+stage*2)))
    for old, sm in ((0x441,0x472),(0x442,0x473)):
        bind(0x4A, 8, old, mapped(sm))
    for frame in range(4):
        bind(0x4A, 5, 0x444+frame, mapped(0x475+frame))

    # Hierophant's two verified dynamic model spans use the same semantic
    # body/frame order in their source and native state machines.
    for old, sm, count in ((0x2CB,0x2FC,0x23),(0x2EE,0x31F,0x28)):
        for frame in range(count):
            bind(0x48, -1, old+frame, mapped(sm+frame))

    # Actor25's face callback (source47A49C..47A667 / native485260)
    # substitutes talking/blinking models after the skeleton lookup. The
    # protagonist face banks differ, so their native frame ids cannot be used
    # merely because the neutral skeleton model has already been bound.
    for kind, old, sm, table, native_table in (
        (0x36,0x149D,0x1519,0x5B3E2C,0x596C90),
        (0x39,0x14B3,0x152F,0x5B3E1C,0x596C80),
        (0x3A,0x14C3,0x153F,0x5B3E1C,0x596C80),
        (0x3B,0x14DA,0x1556,0x5B3E1C,0x596C80),
        (0x3C,0x14E1,0x155D,0x5B3E1C,0x596C80),
        (0x3D,0xD28,0xD64,0x5B3E1C,0x596C80),
        (0x3E,0xC3A,0xC71,0x5B3E1C,0x596C80),
        (0x3F,0x1273,0x12BE,0x5B3E1C,0x596C80),
        (0x39,0x14BB,0x1537,0x5B3E1C,0x596C80),
        (0x3A,0x14CB,0x1547,0x5B3E1C,0x596C80),
        (0x3B,0x14D3,0x154F,0x5B3E1C,0x596C80),
    ):
        source_frames = source.read(table,"<13b")
        native_frames = native.read(native_table,"<13b")
        if source_frames != native_frames:
            raise ValueError(f"actor25 face sequence changed for kind {kind:02X}")
        for frame in set(source_frames):
            bind(kind,2,old+frame,mapped(sm+frame))

    return [(kind, node, old, new) for (kind, node, old), new in sorted(rows.items())]
