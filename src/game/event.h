#pragma once

// HOTD2 event scripts (evt\*.bin), see notes/formats.md. From Evt_LoadCommonTable 0x00413070, Evt_LoadTable
// 0x00413160, Evt_Relocate 0x00413120, Evt_GetScript 0x0045EB90, Evt_Run 0x0045ECC0 and the opcode handler
// table 0x005931D8. Validated against a runtime trace of the original (tests/evt_test.cpp).

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace game {

// Script memory as the game lays it out: comevtbl.bin at 0x00977200, the stage table at 0x00977400, with
// Dreamcast pointers (0x0CE8xxxx) relocated by +0xF3AC1A00 (Evt_Relocate).
class EventMemory {
public:
    static constexpr uint32_t kCommonBase = 0x00977200;
    static constexpr uint32_t kStageBase = 0x00977400;
    void Load(uint32_t base, const std::vector<uint8_t>& file);
    bool Read(uint32_t addr, uint32_t* out) const;

    // comevtbl[mode][scene][variant] like Evt_GetScript (0 if a level is null).
    uint32_t Script(int mode, int scene, int variant) const;

private:
    std::map<uint32_t, uint32_t> words_;
};

struct EventOp {
    uint32_t addr = 0;
    uint32_t opcode = 0;
    uint32_t length = 0;            // bytes, 0 if unknown
    std::vector<uint32_t> args;     // words after the opcode
    const char* name = "unknown";
};

// Decodes one instruction (length rules from the handlers; confirmed by the runtime trace). Covers all 96
// opcodes; returns false for the five invalid ones (handler 0x0041EBB0 never advances) and on unreadable memory.
// 0x4E (halt) and 0x4F (next variant) decode as 4 bytes but never fall through at run time.
bool DecodeEvent(const EventMemory& mem, uint32_t addr, EventOp& out);

// Name of an opcode (notes/agents/events.md), "unknown" if >= 96.
const char* EventOpName(uint32_t opcode);

}  // namespace game
