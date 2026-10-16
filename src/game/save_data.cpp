// game::SaveData - the original's persistent block 0x009C9120 (save_data.h; notes/agents/modes.md section 2).

#include "game/save_data.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace game {

namespace {

// The scrambler key of 0x004A06F0 is built on its stack frame by byte stores (immediates and four byte registers):
// it is read from the exe by replaying those instructions (sub esp / push / mov r8, imm8 / xor r32, r32 / mov byte
// [esp + d], imm8 or r8), so no key bytes are part of the port. false: an unexpected instruction.
bool ScrambleKey(const ExeImage& exe, uint8_t key[176]) {
    memset(key, 0, 176);
    uint8_t code[1400] = {};
    exe.Read(0x004A06F0, code, sizeof(code));
    int depth = -1;          // bytes pushed after the frame's sub esp, 0xB0
    uint8_t reg[8] = {};     // al cl dl bl (ah..bh unused)
    size_t i = 0;
    auto store = [&](int disp, uint8_t v) {
        const int k = depth < 0 ? -1 : disp - depth;
        if (k >= 0 && k < 176) key[k] = v;
    };
    while (i + 8 < sizeof(code)) {
        const uint8_t op = code[i];
        if (op == 0x81 && code[i + 1] == 0xEC) {  // sub esp, imm32
            depth = 0;
            i += 6;
        } else if (op >= 0x50 && op <= 0x57) {  // push r32
            depth += 4;
            i += 1;
        } else if (op >= 0xB0 && op <= 0xB3) {  // mov r8, imm8
            reg[op - 0xB0] = code[i + 1];
            i += 2;
        } else if (op == 0x33 && (code[i + 1] & 0xC0) == 0xC0) {  // xor r32, r32
            const int r = code[i + 1] & 7;
            if (r < 4 && ((code[i + 1] >> 3) & 7) == r) reg[r] = 0;
            i += 2;
        } else if ((op == 0xC6 || op == 0x88) && (code[i + 1] & 7) == 4 && code[i + 2] == 0x24) {  // [esp + disp]
            const int mod = code[i + 1] >> 6, r = (code[i + 1] >> 3) & 7;
            int disp = 0;
            size_t n = 3;
            if (mod == 1) {
                disp = static_cast<int8_t>(code[i + 3]);
                n = 4;
            } else if (mod == 2) {
                int32_t d;
                memcpy(&d, &code[i + 3], 4);
                disp = d;
                n = 7;
            } else {
                return false;
            }
            if (op == 0xC6) {
                store(disp, code[i + n]);
                n += 1;
            } else {
                if (r > 3) return false;
                store(disp, reg[r]);
            }
            i += n;
        } else if (op == 0x3B && (code[i + 1] & 0xC0) == 0xC0) {  // cmp r32, r32
            i += 2;
        } else if (op == 0x8B && (code[i + 1] & 7) == 4 && code[i + 2] == 0x24) {  // mov r32, [esp + d] (the arguments)
            const int mod = code[i + 1] >> 6, r = (code[i + 1] >> 3) & 7;
            if (r < 4) reg[r] = 0;
            i += mod == 1 ? 4 : 7;
        } else {
            return depth >= 0;  // the loop (or another instruction): the key is complete
        }
    }
    return false;
}

uint8_t ExeU8(const ExeImage& exe, uint32_t va) {
    uint8_t v = 0;
    exe.Read(va, &v, 1);
    return v;
}

uint32_t ReadU32(const std::vector<uint8_t>& b, size_t off) {
    uint32_t v = 0;
    if (off + 4 <= b.size()) memcpy(&v, &b[off], 4);
    return v;
}

}  // namespace

const char* const SaveData::kFiles[4] = { "pol/bg_adv19.bin", "tex/scr_tod_itm_itamidome2.bin", "pol/komono_0.bin", "pol/tv2.bin" };

namespace {
bool Scramble(const ExeImage& exe, uint8_t* data, int size) {  // 0x004A06F0
    uint8_t key[176];
    if (!ScrambleKey(exe, key)) return false;
    int k = 0;
    for (int i = 0; i < size; ++i) {
        data[i] ^= static_cast<uint8_t>(key[k] * static_cast<uint8_t>(i) - 0x24);
        k = k < 0xAF ? k + 1 : 0;
    }
    return true;
}

uint32_t Checksum(const uint8_t* d, int n) {  // 0x004A06C0
    uint32_t s = 0;
    for (int i = 0; i < n; ++i) s -= d[i];
    return s;
}
}  // namespace

bool SaveData::LoadOriginal(const std::string& gameDir, const ExeImage& exe, std::string* why) {
    std::vector<uint8_t> b;
    for (const char* f : kFiles) {
        std::ifstream in(std::filesystem::path(gameDir) / f, std::ios::binary);
        std::vector<uint8_t> part((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!in && part.empty()) {
            if (why) *why = std::string("save file ") + f + " missing";
            Defaults(exe);
            return false;
        }
        part.resize(0x3DB);
        b.insert(b.end(), part.begin(), part.end());
    }
    b.resize(kSize);
    if (!Scramble(exe, b.data(), static_cast<int>(kSize))) {
        if (why) *why = "scrambler key not readable";
        Defaults(exe);
        return false;
    }
    if (ReadU32(b, 0x009CA068 - kBase) != Checksum(b.data(), 0xF48) || b[0x009CA05F - kBase] != 7) {
        if (why) *why = "checksum / magic mismatch (Game_Load 0x004A0B60 would use the defaults)";
        Defaults(exe);
        return false;
    }
    raw_ = std::move(b);
    return true;
}

void SaveData::DefaultSettings(const ExeImage& exe) {  // 0x00401130
    if (raw_.size() != kSize) raw_.assign(kSize, 0);
    SetU8(0x009C9F20, ExeU8(exe, 0x004C42A0));
    SetU8(0x009C9F25, ExeU8(exe, 0x004C42A2));
    SetU8(0x009C9F28, ExeU8(exe, 0x004C4368));
    SetU8(0x009C9F21, ExeU8(exe, 0x004C42A1));
    for (int p = 0; p < 2; ++p) {
        const uint32_t b = 0x009C9F64 + static_cast<uint32_t>(p) * 0x7C;
        SetU8(b - 4, ExeU8(exe, 0x004C42A3));
        SetS32(b, static_cast<int32_t>(exe.U32(0x004C42A4)));
        for (int k = 0; k < 0x14; ++k) SetS32(b + 4 + 4 * k, static_cast<int32_t>(exe.U32(0x004C42A8 + p * 0x50 + 4 * k)));
        SetS32(b + 0x54, static_cast<int32_t>(exe.U32(0x004C4348)));
        SetS32(b + 0x58, static_cast<int32_t>(exe.U32(0x004C434C)));
        for (int k = 0; k < 5; ++k) SetS32(b + 0x5C + 4 * k, static_cast<int32_t>(exe.U32(0x004C4350 + 4 * k)));
    }
}

void SaveData::Defaults(const ExeImage& exe) {  // 0x00401060
    raw_.assign(kSize, 0);
    // 0x0041C490(0): the default tables (exe 0x00530048 + t * 0xA0, in the order of 0x0057A354) -> 0x0041C5C0
    for (int t = 0; t < kTables; ++t)
        exe.Read(0x00530048 + static_cast<uint32_t>(t) * 0xA0, &raw_[kRankings - kBase + static_cast<uint32_t>(t) * 0xA0], 0xA0);
    SetU8(0x009C9F22, 1);
    SetU8(0x009C9F23, 1);
    SetU8(0x009C9F27, 1);
    SetU8(0x009C9F3D + 3, 1);  // starting items 3, 7, 16
    SetU8(0x009C9F44, 1);
    SetU8(0x009C9F4D, 1);
    SetU8(0x009CA05A, 0x90);
    SetU8(0x009CA05B, 0xC0);
    SetU8(0x009CA058, 7);
    SetU8(0x009CA05F, 7);
    DefaultSettings(exe);
}

int32_t SaveData::S32(uint32_t va) const {
    int32_t v = 0;
    if (In(va, 4)) memcpy(&v, &raw_[va - kBase], 4);
    return v;
}

void SaveData::SetS32(uint32_t va, int32_t v) {
    if (In(va, 4)) memcpy(&raw_[va - kBase], &v, 4);
}

RankEntry SaveData::Entry(int table, int i) const {
    RankEntry e;
    const uint32_t va = kRankings + static_cast<uint32_t>(table * 0xA0 + i * 0x10);
    if (!In(va, 16)) return e;
    memcpy(e.name, &raw_[va - kBase], 4);
    e.b4 = S8(va + 4);
    e.b5 = S8(va + 5);
    memcpy(&e.pad, &raw_[va - kBase + 6], 2);
    e.score = S32(va + 8);
    e.c = S32(va + 12);
    return e;
}

void SaveData::SetEntry(int table, int i, const RankEntry& e) {
    const uint32_t va = kRankings + static_cast<uint32_t>(table * 0xA0 + i * 0x10);
    if (!In(va, 16)) return;
    memcpy(&raw_[va - kBase], e.name, 4);
    SetU8(va + 4, static_cast<uint8_t>(e.b4));
    SetU8(va + 5, static_cast<uint8_t>(e.b5));
    memcpy(&raw_[va - kBase + 6], &e.pad, 2);
    SetS32(va + 8, e.score);
    SetS32(va + 12, e.c);
}

// The port's file: "HOTD2RE1" + the block (plain) + the checksum of 0x004A06C0
bool SaveData::LoadNative(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (b.size() != 8 + kSize + 4 || memcmp(b.data(), "HOTD2RE1", 8) != 0) return false;
    if (ReadU32(b, 8 + kSize) != Checksum(b.data() + 8, static_cast<int>(kSize))) return false;
    raw_.assign(b.begin() + 8, b.begin() + 8 + kSize);
    return true;
}

bool SaveData::SaveNative(const std::string& path) const {
    if (path.empty() || raw_.size() != kSize) return false;
    std::error_code ec;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write("HOTD2RE1", 8);
    out.write(reinterpret_cast<const char*>(raw_.data()), kSize);
    const uint32_t s = Checksum(raw_.data(), static_cast<int>(kSize));
    out.write(reinterpret_cast<const char*>(&s), 4);
    return static_cast<bool>(out);
}
std::vector<uint8_t> SaveData::NativeBytes() const {
    if (raw_.size() != kSize) return {};
    std::vector<uint8_t> out{'H','O','T','D','2','R','E','1'};
    out.insert(out.end(), raw_.begin(), raw_.end());
    const uint32_t sum = Checksum(raw_.data(), static_cast<int>(kSize));
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(sum >> (i * 8)));
    return out;
}

}  // namespace game
