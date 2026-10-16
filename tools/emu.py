"""Run small, pure functions from Hod2.exe in a CPU emulator (Unicorn) to get exact game behavior.

Only for leaf functions that read static data (no Win32 calls). The exe's sections are mapped at their
VAs; each call gets a fresh stack and a return-address trap.

    from emu import ExeEmu
    emu = ExeEmu()
    bank = emu.call_cdecl(0x0041C9E0, file_index, tex_index)   # Tex_GetPaletteBank
"""
import pefile
from unicorn import UC_ARCH_X86, UC_HOOK_MEM_INVALID, UC_MODE_32, Uc, UcError
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP

STACK_BASE, STACK_SIZE = 0x00100000, 0x00010000
RET_TRAP = 0x00010000


class ExeEmu:
    def __init__(self, path="game/Hod2.exe"):
        pe = pefile.PE(path)
        base = pe.OPTIONAL_HEADER.ImageBase
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF
        self.uc.mem_map(base, image_size)
        self.uc.mem_write(base, pe.header)
        for s in pe.sections:
            self.uc.mem_write(base + s.VirtualAddress, s.get_data())
        self.uc.mem_map(STACK_BASE, STACK_SIZE)
        self.uc.mem_map(RET_TRAP, 0x1000)
        self.uc.mem_write(RET_TRAP, b"\xF4")  # hlt
        self.uc.hook_add(UC_HOOK_MEM_INVALID, self._bad_access)

    @staticmethod
    def _bad_access(uc, access, address, size, value, user):
        raise RuntimeError(f"emulated code touched unmapped memory at 0x{address:08X}")

    def call_cdecl(self, func, *args, max_insns=100000):
        sp = STACK_BASE + STACK_SIZE - 0x100
        frame = [RET_TRAP] + [a & 0xFFFFFFFF for a in args]
        for i, v in enumerate(frame):
            self.uc.mem_write(sp + 4 * i, v.to_bytes(4, "little"))
        self.uc.reg_write(UC_X86_REG_ESP, sp)
        try:
            self.uc.emu_start(func, RET_TRAP, count=max_insns)
        except UcError as e:
            raise RuntimeError(f"emulation of 0x{func:08X}{args} failed: {e}") from e
        return self.uc.reg_read(UC_X86_REG_EAX)
