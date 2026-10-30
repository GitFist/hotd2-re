#include "event.h"

#include <cstring>

namespace game {

namespace {

// Length kinds (from the handlers, see notes/agents/events.md):
//   Fixed      constant size
//   List       opcode + words up to and including a -1 terminator
//   Env20      0x20/0x24 Env_SetChannel 0x0040B3F0: sub 5/9 -> 20, sub 0..10 -> 12, other -> 8
//   EnvInterp  0x21/0x23/0x25/0x27 (0x0040B650 / 0x0040BA90): sub 0..10 -> 16, other -> 8
//   Block30    0x30: (n/16)*4 + 8 bytes, n = first argument (C division)
//   PerPlayer  0x49-0x4B: N -1 terminated lists, the selected one processed, then everything up to a -2 word
//   Invalid    0x00/0x2A/0x34/0x3C/0x4C: handler 0x0041EBB0 returns without advancing (the VM would spin)
enum class Len { Fixed, List, Env20, EnvInterp, Block30, PerPlayer, TypingMessage, Invalid };

struct OpInfo {
    const char* name;
    Len kind;
    uint32_t bytes;
};

// All 96 opcodes of the handler table 0x005931D8. Names with '?' are hypotheses.
const OpInfo kOps[117] = {
    /*00*/ { "invalid_00", Len::Invalid, 0 },          { "route1_spawn09", Len::List, 0 },
    /*02*/ { "route1_spawn0a", Len::List, 0 },         { "route1_spawn0b", Len::List, 0 },
    /*04*/ { "route1_spawn0c", Len::List, 0 },         { "route2_spawn09", Len::List, 0 },
    /*06*/ { "route2_spawn0a", Len::List, 0 },         { "route2_spawn0b", Len::List, 0 },
    /*08*/ { "route2_spawn0c", Len::List, 0 },         { "spawn09", Len::List, 0 },
    /*0A*/ { "spawn0a", Len::List, 0 },                { "spawn0b", Len::List, 0 },
    /*0C*/ { "spawn0c", Len::List, 0 },                { "spawn0b_noskip", Len::List, 0 },
    /*0E*/ { "set_table_2be0", Len::Fixed, 24 },       { "set_list_8e40", Len::List, 0 },
    /*10*/ { "set_ptr_1a14", Len::List, 0 },           { "set_ptr_34d8", Len::List, 0 },
    /*12*/ { "set_list_8e40_r2", Len::List, 0 },       { "set_params_13", Len::List, 0 },
    /*14*/ { "set_2bb4", Len::Fixed, 8 },              { "set_8d4c", Len::Fixed, 8 },
    /*16*/ { "set_71d4", Len::Fixed, 16 },             { "light_angle_task", Len::Fixed, 16 },
    /*18*/ { "light_angles", Len::Fixed, 12 },         { "light_angles_b1", Len::Fixed, 12 },
    /*1A*/ { "set_8e58", Len::Fixed, 8 },              { "set_2c34", Len::Fixed, 8 },
    /*1C*/ { "set_a090", Len::Fixed, 8 },              { "set_8e50", Len::Fixed, 8 },
    /*1E*/ { "set_8a78", Len::Fixed, 8 },              { "set_a0f4", Len::Fixed, 8 },
    /*20*/ { "env_set", Len::Env20, 0 },               { "env_interp", Len::EnvInterp, 0 },
    /*22*/ { "env_stop", Len::Fixed, 8 },              { "env_interp_frames", Len::EnvInterp, 0 },
    /*24*/ { "env_set_b1", Len::Env20, 0 },            { "env_interp_b1", Len::EnvInterp, 0 },
    /*26*/ { "env_stop_b1", Len::Fixed, 8 },           { "env_interp_frames_b1", Len::EnvInterp, 0 },
    /*28*/ { "section_preload", Len::Fixed, 8 },       { "section_switch", Len::Fixed, 8 },
    /*2A*/ { "invalid_2a", Len::Invalid, 0 },          { "player_bonus_2b?", Len::Fixed, 4 },
    /*2C*/ { "skippable", Len::Fixed, 8 },             { "voice", Len::Fixed, 8 },
    /*2E*/ { "sound_stop_if_skipping", Len::Fixed, 4 }, { "set_5c48", Len::Fixed, 8 },
    /*30*/ { "cam_queue", Len::Block30, 0 },           { "cam_start", Len::Fixed, 8 },
    /*32*/ { "cam_start_ready", Len::Fixed, 8 },       { "cam_set_state", Len::Fixed, 12 },
    /*34*/ { "invalid_34", Len::Invalid, 0 },          { "cam_roll", Len::Fixed, 8 },
    /*36*/ { "set_70f4", Len::Fixed, 8 },              { "rail_force", Len::Fixed, 8 },
    /*38*/ { "sound", Len::Fixed, 8 },                 { "sound_repeat", Len::Fixed, 16 },
    /*3A*/ { "sound_noskip", Len::Fixed, 8 },          { "sound_repeat_noskip", Len::Fixed, 16 },
    /*3C*/ { "invalid_3c", Len::Invalid, 0 },          { "nop16", Len::Fixed, 16 },
    /*3E*/ { "nop8", Len::Fixed, 8 },                  { "nop4", Len::Fixed, 4 },
    /*40*/ { "wait_queue", Len::Fixed, 4 },            { "wait_camera", Len::Fixed, 8 },
    /*42*/ { "wait_frames", Len::Fixed, 8 },           { "wait_7006", Len::Fixed, 8 },
    /*44*/ { "wait_enemies", Len::Fixed, 8 },          { "wait_flag", Len::Fixed, 8 },
    /*46*/ { "wait_a0e8", Len::Fixed, 8 },             { "wait_cam_ready", Len::Fixed, 4 },
    /*48*/ { "set_flag", Len::Fixed, 8 },              { "pp_load_pol", Len::PerPlayer, 0 },
    /*4A*/ { "pp_unload_pol", Len::PerPlayer, 0 },     { "pp_spawn0b", Len::PerPlayer, 0 },
    /*4C*/ { "invalid_4c", Len::Invalid, 0 },          { "stage_reset", Len::Fixed, 4 },
    /*4E*/ { "halt", Len::Fixed, 4 },                  { "next_variant", Len::Fixed, 4 },
    /*50*/ { "load_model", Len::Fixed, 8 },            { "unload_model", Len::Fixed, 8 },
    /*52*/ { "load_pol", Len::Fixed, 8 },              { "unload_pol", Len::Fixed, 8 },
    /*54*/ { "load_tex", Len::Fixed, 8 },              { "unload_tex", Len::Fixed, 8 },
    /*56*/ { "load_mot", Len::Fixed, 8 },              { "request_type9?", Len::Fixed, 8 },
    /*58*/ { "flush_all", Len::Fixed, 4 },             { "flush_lt8", Len::Fixed, 4 },
    /*5A*/ { "flush_8_10", Len::Fixed, 4 },            { "nop4_5b", Len::Fixed, 4 },
    /*5C*/ { "nop4_5c", Len::Fixed, 4 },               { "snd_load_pack", Len::Fixed, 12 },
    /*5E*/ { "snd_free_pack", Len::Fixed, 8 },         { "bgm", Len::Fixed, 20 },
    // supplied English PC TOTD reference table 0x0059D700. Decoding a command does not imply that its
    // runtime contract is implemented; unsupported commands fail explicitly.
    /*60*/ { "typing_challenge", Len::TypingMessage, 0 }, { "typing_words", Len::Fixed, 12 },
    /*62*/ { "typing_countdown", Len::Fixed, 12 },     { "typing_wait_challenge", Len::Fixed, 8 },
    /*64*/ { "typing_words_one_player", Len::Fixed, 12 }, { "typing_words_two_players", Len::Fixed, 12 },
    /*66*/ { "typing_props", Len::Fixed, 8 },         { "typing_clear_actor_slots", Len::Fixed, 4 },
    /*68*/ { "typing_word_limits", Len::Fixed, 12 },  { "typing_lesson", Len::Invalid, 0 },
    /*6A*/ { "typing_wait_lesson", Len::Fixed, 8 },    { "typing_wait_count", Len::Fixed, 8 },
    /*6C*/ { "typing_camera_one_player", Len::Block30, 0 }, { "typing_camera_two_players", Len::Block30, 0 },
    /*6E*/ { "typing_branch_one_player", Len::Fixed, 8 }, { "typing_branch_two_players", Len::Fixed, 8 },
    /*70*/ { "typing_checkpoint", Len::Fixed, 4 },    { "typing_retry_checkpoint", Len::Fixed, 4 },
    /*72*/ { "typing_lesson_objects", Len::List, 0 }, { "typing_mode_branch", Len::Fixed, 12 },
    /*74*/ { "typing_wait_result", Len::Fixed, 8 },
};

}  // namespace

const char* EventOpName(uint32_t opcode) { return opcode < 117 ? kOps[opcode].name : "unknown"; }

void EventMemory::Load(uint32_t base, const std::vector<uint8_t>& file) {
    size_t i = 0;
    for (; i + 4 <= file.size(); i += 4) {
        uint32_t w;
        memcpy(&w, &file[i], 4);
        if ((w & 0xFFF80000u) == 0x0CE80000u) w += 0xF3AC1A00u;  // Evt_Relocate
        words_[base + static_cast<uint32_t>(i)] = w;
    }
    if (i < file.size()) {
        // a file whose size is not a multiple of 4 (adv2evtbl.bin: 3542 bytes) is read over the memory: its last bytes
        // replace the low bytes of the word there, the rest keeps what was loaded before (the last record of adv2evtbl
        // has its s16 P[16] = 0 in those two bytes: story mode 11's actor 0x24 freezes at once, 0x00483030)
        uint32_t w = 0;
        Read(base + static_cast<uint32_t>(i), &w);
        memcpy(&w, &file[i], file.size() - i);
        words_[base + static_cast<uint32_t>(i)] = w;
    }
}

bool EventMemory::Read(uint32_t addr, uint32_t* out) const {
    auto it = words_.find(addr);
    if (it == words_.end()) return false;
    *out = it->second;
    return true;
}

bool EventMemory::SceneRoute(int mode, int word, int16_t* value) const {
    const auto found = sceneRoutes_.find(mode);
    if (word < 0 || found == sceneRoutes_.end() || static_cast<size_t>(word) >= found->second.size() / 2) return false;
    memcpy(value, found->second.data() + static_cast<size_t>(word) * 2, 2);
    return true;
}

uint32_t EventMemory::Script(int mode, int scene, int variant) const {
    uint32_t a = 0, b = 0, c = 0;
    if (!Read(kCommonBase + mode * 4, &a) || !a) return 0;
    if (!Read(a + scene * 4, &b) || !b) return 0;
    if (!Read(b + variant * 4, &c)) return 0;
    return c;
}

bool DecodeEvent(const EventMemory& mem, uint32_t addr, EventOp& out) {
    out = EventOp{};
    out.addr = addr;
    if (!mem.Read(addr, &out.opcode) || out.opcode >= (mem.TypingDialect() ? 117u : 96u)) return false;
    const OpInfo& info = kOps[out.opcode];
    out.name = info.name;
    uint32_t w = 0;
    switch (info.kind) {
    case Len::Invalid:
        return false;
    case Len::Fixed:
        out.length = info.bytes;
        break;
    case Len::List: {
        uint32_t n = 1;
        while (mem.Read(addr + 4 * n, &w) && w != 0xFFFFFFFFu) ++n;
        out.length = 4 * (n + 1);
        break;
    }
    case Len::PerPlayer: {
        uint32_t n = 1;
        while (mem.Read(addr + 4 * n, &w) && w != 0xFFFFFFFEu) ++n;
        out.length = 4 * (n + 1);
        break;
    }
    case Len::Env20:
        if (!mem.Read(addr + 4, &w)) return false;
        out.length = (w == 5 || w == 9) ? 20 : (w <= 10 ? 12 : 8);
        break;
    case Len::EnvInterp:
        if (!mem.Read(addr + 4, &w)) return false;
        out.length = w <= 10 ? 16 : 8;
        break;
    case Len::Block30: {
        if (!mem.Read(addr + 4, &w)) return false;
        const int32_t n = static_cast<int32_t>(w);
        if (n < 0) return false;
        out.length = static_cast<uint32_t>((n / 16) * 4 + 8);
        break;
    }
    case Len::TypingMessage: {
        // Four fixed operands, then (count, data pointer) pairs, terminated by
        // a count of -1; English PC reference handler 0x00420610.
        uint32_t n = 5;
        for (int guard = 0;; ++guard, n += 2) {
            if (guard >= 64 || !mem.Read(addr + n * 4, &w)) return false;
            if (w == 0xFFFFFFFFu) { out.length = (n + 1) * 4; break; }
        }
        break;
    }
    }
    for (uint32_t i = 1; i < out.length / 4; ++i) {
        mem.Read(addr + 4 * i, &w);
        out.args.push_back(w);
    }
    return true;
}

}  // namespace game
