#include "game/environment.h"

#include <algorithm>
#include <cstring>

namespace game {

namespace {

bool ReadFloat(const EventMemory& mem, uint32_t addr, float* out) {
    uint32_t w;
    if (!mem.Read(addr, &w)) return false;
    memcpy(out, &w, 4);
    return true;
}

// Channel c of a block as the interpolation handlers read it (fog colour channels: (float) int).
float ChannelValue(const EnvBlock& b, int c) {
    switch (c) {
    case 0: return b.fogNear;
    case 1: return b.fogFar;
    case 2: case 3: case 4: return static_cast<float>(b.fogColor[c - 2]);
    case 5: case 6: case 7: return b.lightColor[c - 5];
    default: return b.lightIntensity;
    }
}

// Channels a sub-op addresses (0x0040B650 / 0x0040BA90 / 0x0040C1F0): 0..4 and 6..8, 10 -> one; 5 -> fog r g b (2..4);
// 9 -> light r g b (5..7).
int SubChannels(uint32_t sub, int out[3]) {
    if (sub == 5) { out[0] = 2; out[1] = 3; out[2] = 4; return 3; }
    if (sub == 9) { out[0] = 5; out[1] = 6; out[2] = 7; return 3; }
    if (sub <= 4) { out[0] = static_cast<int>(sub); return 1; }
    if (sub >= 6 && sub <= 8) { out[0] = static_cast<int>(sub) - 1; return 1; }
    if (sub == 10) { out[0] = 8; return 1; }
    return 0;
}

}  // namespace

void Environment::StageDefaults() {
    fogColor[0] = 255;
    fogColor[1] = 0;
    fogColor[2] = 0;
    chan[0][3].value = 0;  // 0x009C8A14 / 0x009C8A24 / 0x009C8A04 (the fog colour records)
    chan[0][4].value = 0;
    chan[0][2].value = 255.0f;
    fogNear = chan[0][0].value = -600.0f;
    fogFar = chan[0][1].value = 1800.0f;
    const float c[3] = { 1.0f, 0.2f, 0.1f };  // 0x3F800000 / 0x3E4CCCCD / 0x3DCCCCCD
    for (int k = 0; k < 3; ++k) {
        lightColor[k] = chan[0][5 + k].value = c[k];
        block1.lightColor[k] = chan[1][5 + k].value = c[k];
    }
    block1.lightIntensity = chan[1][8].value = 1.0f;
}

void Environment::ResetBlocks() {
    static_cast<EnvBlock&>(*this) = EnvBlock{};
    block1 = EnvBlock{};
    hasLightAngles = false;
}

void Environment::InitInterp() {
    // Fog records (both blocks) from 0x007C1750 (BSS: zero), colour {0, 1.0, 0, 0}, intensity {0, 0.7, 0, 0}.
    for (int b = 0; b < 2; ++b) {
        for (int c = 0; c < 5; ++c) chan[b][c] = EnvChannel{};
        for (int c = 5; c < 8; ++c) chan[b][c] = EnvChannel{ 0, 1.0f, 0.0f, 0.0f };
        chan[b][8] = EnvChannel{ 0, 0.7f, 0.0f, 0.0f };
    }
}

void Environment::Step() {
    for (int b = 0; b < 2; ++b) {
        EnvBlock& blk = Block(b);
        for (int c = 0; c < 9; ++c) {
            EnvChannel& r = chan[b][c];
            if (r.active != 1) continue;  // 0x0040B0F0
            if (r.target - r.value >= 0.0f) {
                r.value = r.value + r.step;
                if (r.target <= r.value) {
                    r.active = 0;
                    r.value = r.target;
                }
            } else {
                r.value = -r.step + r.value;
                if (r.value <= r.target) {
                    r.active = 0;
                    r.value = r.target;
                }
            }
            switch (c) {
            case 0: blk.fogNear = r.value; break;
            case 1: blk.fogFar = r.value; break;
            case 2: case 3: case 4: blk.fogColor[c - 2] = static_cast<int>(r.value); break;  // __ftol
            case 5: case 6: case 7: blk.lightColor[c - 5] = r.value; break;
            default: blk.lightIntensity = r.value; break;
            }
        }
    }
}

bool Environment::Apply(const EventMemory& mem, const EventOp& op) {
    const auto& a = op.args;
    if ((op.opcode == 0x18 || op.opcode == 0x19) && a.size() >= 2) {  // 0x0045F240 / 0x0045F270 -> 0x0040E140
        EnvBlock& blk = Block(op.opcode == 0x19);
        blk.lightPitch = static_cast<int16_t>(a[0]);
        blk.lightYaw = static_cast<int16_t>(a[1]);
        if (op.opcode == 0x18) hasLightAngles = true;
        return true;
    }
    if (a.empty()) return false;
    const auto value = [&](size_t i, float* out) { return i < a.size() && ReadFloat(mem, a[i], out); };
    if (op.opcode == 0x20 || op.opcode == 0x24) {  // Env_SetChannel 0x0040B3F0 (block 0 / 1)
        const int b = op.opcode == 0x24;
        EnvBlock& blk = Block(b);
        EnvChannel* r = chan[b];
        auto setF = [&](size_t i, float* field, int c) {
            float v;
            if (!value(i, &v)) return false;
            *field = v;
            r[c].value = v;
            return true;
        };
        auto setI = [&](size_t i, int k) {
            if (a.size() <= i) return false;
            blk.fogColor[k] = static_cast<int>(a[i]);
            r[2 + k].value = static_cast<float>(blk.fogColor[k]);
            return true;
        };
        switch (a[0]) {
            case 0: return setF(1, &blk.fogNear, 0);
            case 1: return setF(1, &blk.fogFar, 1);
            case 2: return setI(1, 0);
            case 3: return setI(1, 1);
            case 4: return setI(1, 2);
            case 5: return setI(1, 0) && setI(2, 1) && setI(3, 2);
            case 6: return setF(1, &blk.lightColor[0], 5);
            case 7: return setF(1, &blk.lightColor[1], 6);
            case 8: return setF(1, &blk.lightColor[2], 7);
            case 9: return setF(1, &blk.lightColor[0], 5) && setF(2, &blk.lightColor[1], 6) && setF(3, &blk.lightColor[2], 7);
            case 10: return setF(1, &blk.lightIntensity, 8);
            default: return false;
        }
    }
    int chs[3];
    const int n = SubChannels(a[0], chs);
    if (op.opcode == 0x22 || op.opcode == 0x26) {  // 0x0040C1F0: stop
        for (int i = 0; i < n; ++i) chan[op.opcode == 0x26][chs[i]].active = 0;
        return n > 0;
    }
    if (op.opcode == 0x21 || op.opcode == 0x25 || op.opcode == 0x23 || op.opcode == 0x27) {
        const int b = op.opcode == 0x25 || op.opcode == 0x27;
        const bool frames = op.opcode == 0x23 || op.opcode == 0x27;  // 0x0040BA90; else 0x0040B650
        if (n == 0 || a.size() < 3) return false;
        const bool fogColour = chs[0] >= 2 && chs[0] <= 4;
        float target, second;
        if (fogColour) target = static_cast<float>(static_cast<int32_t>(a[1]));  // int literal (fild)
        else if (!value(1, &target)) return false;
        if (frames) second = static_cast<float>(static_cast<int32_t>(a[2]));     // frame count (fild)
        else if (!value(2, &second)) return false;                               // step (float through a pointer)
        for (int i = 0; i < n; ++i) {
            EnvChannel& r = chan[b][chs[i]];
            r.active = 1;
            r.value = ChannelValue(Block(b), chs[i]);
            r.target = target;
            r.step = second;
            if (frames && second != 0.0f) {
                float s = (r.target - r.value) / second;
                if (s < 0.0f) s = s * -1.0f;  // 0x004C4C64
                r.step = s;
            }
        }
        return true;
    }
    return false;
}

// 0x004ABDF0: both values doubled, the smaller one is FOGSTART.
float Environment::FogStart() const { return std::min(fogNear + fogNear, fogFar + fogFar); }
float Environment::FogEnd() const { return std::max(fogNear + fogNear, fogFar + fogFar); }

// 0x004AA120 (constants 0x00565DE4 = 1.4, 0x004C4D10 = 0.3, 0x00570F5C = 255).
void LightDiffuseOf(const float color[3], float intensity, float out[3]) {
    for (int k = 0; k < 3; ++k) out[k] = (intensity * color[k]) * 1.4f;
}

void LightAmbientOf(const float color[3], float out[3]) {
    for (int k = 0; k < 3; ++k) out[k] = color[k] * 0.3f;
}

uint32_t AmbientRenderStateOf(const float color[3], float intensity) {
    uint32_t v = 0xFF;
    for (int k = 0; k < 3; ++k) {
        const float c = intensity * color[k];
        v = (v << 8) | (static_cast<uint32_t>(static_cast<int>(c * 255.0f)) & 0xFF);  // __ftol truncates
    }
    return v;
}

void Environment::LightDiffuse(float out[3]) const { LightDiffuseOf(lightColor, lightIntensity, out); }
void Environment::LightAmbient(float out[3]) const { LightAmbientOf(lightColor, out); }
uint32_t Environment::AmbientRenderState() const { return AmbientRenderStateOf(lightColor, lightIntensity); }

}  // namespace game
