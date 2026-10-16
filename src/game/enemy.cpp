#include "enemy.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

float AsFloat(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// VC6 __ftol: truncation toward zero.
int Ftol(float v) { return static_cast<int>(v); }

}  // namespace

bool EnemyTables::Load(const ExeImage& exe) {
    for (int i = 0; i < kBehaviourCount; ++i) behaviour[i] = exe.U32(0x00592AE8 + 4 * i);
    for (int i = 0; i < 5; ++i) hooks[i] = exe.U32(0x00592BC0 + 4 * i);
    for (int i = 0; i < 5; ++i) hpBonus[i] = exe.S16(0x005776B0 + 4 * i);
    for (int i = 0; i < 5; ++i) exe.Read(0x005679F4 + i, &initialRank[i], 1);
    for (int i = 0; i < 0x48; ++i) kindRadius[i] = F32(exe, 0x004C4D28 + 4 * i);
    exe.Read(0x004C4D18, partBit.data(), partBit.size());
    kinds.assign(64, {});
    for (int k = 0; k < 64; ++k) {
        Kind& kd = kinds[k];
        kd.nodeSlots = exe.S16(0x004E0724 + 2 * k);
        if (kd.nodeSlots <= 0 || kd.nodeSlots > 64) { kd.nodeSlots = 0; continue; }
        // Rows are indexed slot*6 + stage (+1 for the "next" entry); one spare row covers stage overflow reads.
        const int entries = (kd.nodeSlots + 1) * 6 + 1;
        const uint32_t nodeTab = exe.U32(0x004C7160 + 4 * k);
        const uint32_t dmgTab = exe.U32(0x004C8350 + 4 * k);
        const uint32_t rankTab = exe.U32(0x004D0D84 + 4 * k);
        if (exe.InData(nodeTab)) {
            kd.nodeTable.resize(entries);
            exe.Read(nodeTab, kd.nodeTable.data(), entries * 2);
        }
        if (exe.InData(dmgTab)) {
            kd.baseDamage.resize(entries);
            exe.Read(dmgTab, kd.baseDamage.data(), entries * 2);
        }
        if (rankTab != 0 && exe.InData(rankTab)) {
            kd.rankMod.resize((kd.nodeSlots + 1) * 16);
            exe.Read(rankTab, kd.rankMod.data(), kd.rankMod.size());
        }
    }
    return behaviour[23] == 0x00457B50;
}

// 0x0040A8B0
int EnemyInitialHp(const EnemyTables& t, int recordHp, int difficulty, int gameMode) {
    const int idx = gameMode == 5 ? 2 : difficulty;
    int hp = static_cast<int16_t>(recordHp + t.hpBonus[idx]);
    if (hp < 1) hp = 1;
    if (hp > 300) hp = 300;
    return hp;
}

// 0x00409870
int EnemyRankDamageMod(const EnemyTables& t, int kind, int slot, int rank) {
    if (kind < 0 || kind >= static_cast<int>(t.kinds.size())) return 0;
    const auto& m = t.kinds[kind].rankMod;
    if (m.empty()) return 0;
    if (kind == 0x17 && slot > 0xF) slot = 0;
    const size_t i = static_cast<size_t>(slot) * 16 + rank;
    return i < m.size() ? m[i] : 0;
}

// 0x00409430 (Enemy_ApplyShot). Hit type, damage, node stage/flags, kill flag and score; see the header.
EnemyShotResult EnemyApplyShot(const EnemyTables& t, EnemyShotState& s, int slot, int gameMode, int rank,
                               CrtRand* rng) {
    EnemyShotResult r;
    const EnemyTables::Kind& kd = t.kinds[s.kind];
    EnemyNodeState& node = s.nodes[slot];
    auto tab = [&](const std::vector<int16_t>& v, int i) -> int16_t {
        return i >= 0 && i < static_cast<int>(v.size()) ? v[i] : 0;
    };
    const int idx = node.stage + slot * 6;
    const int16_t swapModel = tab(kd.nodeTable, idx);   // sVar2
    const int16_t next = tab(kd.nodeTable, idx + 1);    // sVar3
    int16_t damage = static_cast<int16_t>(tab(kd.baseDamage, idx));
    if (gameMode != 6) s.flags34 |= 0xE00;  // 0x200 blocks wound-model swaps (0x004098E0)
    damage = static_cast<int16_t>(damage + EnemyRankDamageMod(t, s.kind, slot, rank));
    if (damage < 0) damage = 0;
    if (s.itemDamage) {  // game type 1
        if (s.damageFactor == -1.0f) {
            damage = s.hp;
        } else {
            float f = static_cast<float>(damage) * s.damageFactor;
            if (s.damageBoost != 0.0f) f = f * s.damageBoost;
            damage = static_cast<int16_t>(static_cast<int>(f));  // _ftol 0x004ACF50
        }
    }

    auto applyDamage = [&] {  // 0x004098C0
        if ((s.flags34 & 0x1000) == 0) {
            s.hp = static_cast<int16_t>(s.hp - damage);
            r.damage = damage;
        }
    };
    auto setModel = [&](EnemyNodeState& n, int model, int partSlot) {  // 0x004098E0 (wound table 0x004099A0 not modelled)
        const int16_t nextEntry = tab(kd.nodeTable, n.stage + partSlot * 6 + 1);
        if (s.flags34 & 0x200) return;
        n.model = model;
        r.modelSwapped = true;
        if ((nextEntry == 0 || nextEntry == 1) && partSlot >= 0 && partSlot < static_cast<int>(t.partBit.size()))
            s.severMask = static_cast<uint8_t>(s.severMask | (1u << (t.partBit[partSlot] & 31)));
    };

    r.type = EnemyHitType::None;
    if (next == 0) {
        if (swapModel == 2) {
            r.type = EnemyHitType::Deflect;
        } else {
            r.type = EnemyHitType::Plain;
            applyDamage();
            if (slot == 1) {
                if (s.hp < 1 && (s.flags34 & 0x400) == 0) {
                    r.type = EnemyHitType::Sever;
                    setModel(node, swapModel, 1);
                    node.flags |= 0x80000000u;
                }
            } else if ((node.flags & 0x80000000u) == 0 && swapModel != 0) {
                r.type = EnemyHitType::Wound;
                setModel(node, swapModel, slot);
                ++node.stage;
                node.flags |= 0x80000000u;
            }
        }
    } else if (next == 1) {
        if ((s.flags34 & 0x400) == 0 && (s.kind != 0xC || (s.flags136C & 0x80) != 0 || slot < 9)) {
            r.type = EnemyHitType::Plain;
            applyDamage();
            if ((node.flags & 0x80000000u) == 0) {
                r.type = EnemyHitType::Sever;
                setModel(node, swapModel, slot);
                node.flags |= 0x80000000u;
            }
        } else {
            r.type = EnemyHitType::Plain;
            applyDamage();
        }
    } else if (next == 2) {
        r.type = EnemyHitType::Deflect;
    } else {
        r.type = EnemyHitType::Wound;
        applyDamage();
        bool skip = false;
        if (s.hp > 0 && slot == 1) {
            int n = 0;
            while (tab(kd.nodeTable, 6 + n) > 2) ++n;  // wound stages of the torso row
            skip = n <= node.stage + 1;
        }
        if (!skip) {
            setModel(node, swapModel, slot);
            ++node.stage;
        }
    }
    if (s.flags34 & 0x800) r.type = EnemyHitType::None;
    bool alreadyDead = (s.flags34 & 0x4000000) != 0;
    if (alreadyDead && r.type == EnemyHitType::Plain) r.type = EnemyHitType::None;
    if (!alreadyDead && s.hp < 1) {
        if (gameMode == 6 && s.kind != 3 && s.kind != 0x12 && s.kind != 0x18 && slot == 2 && s.nodes.size() > 2 &&
            s.nodes[2].stage < 2 && r.type != EnemyHitType::Deflect && rng) {
            ++r.randCalls;
            // rand() & 3 == 0 -> decapitation (0x00407200 / 0x0040A130 / 0x004098E0(node 2, 0, 2)): done by the caller
            r.decapitate = (rng->Next() & 3) == 0;
        }
        s.flags34 |= 0x4000000;
        r.killed = true;
        r.score += 0x50;
    }
    // Score: 10 per non-head hit (not for deflects), head hits 0x78 + a per-player combo (not modelled).
    if (slot != 2) {
        if (r.type != EnemyHitType::Deflect) r.score += 10;
    } else {
        r.score += 0x78;
    }
    return r;
}

// 0x00454E00. The index into each pair is (r % 17) >> 4, i.e. 1 only when r % 17 == 16.
namespace {
// 0x3D9..0x3E0: 0x0059301C + (motion * 2 - 0x7B2) * 4 (values checked against the exe by enemy_test)
constexpr int kHoldTicks[][2] = {
    {104, 98}, {96, 84}, {76, 75}, {160, 152}, {83, 80}, {83, 82}, {94, 91}, {73, 70},  // 0x3D9..0x3E0
};
constexpr int kHoldOther[][3] = {  // motion, ticks: 0x0059305C, 0x00593064, 0x0059307C, 0x00593074, 0x0059306C
    {0x3F8, 68, 65}, {0x1DF, 149, 153}, {0x3F7, 65, 72}, {0x404, 64, 62}, {0x41A, 43, 41},
};
}  // namespace

bool EnemyCorpseHoldUsesRand(int motion) {
    if (motion >= 0x3D9 && motion <= 0x3E0) return true;
    for (const auto& h : kHoldOther)
        if (h[0] == motion) return true;
    return false;
}

int EnemyCorpseHoldTick(int motion, int randValue) {
    const int pick = (randValue % 17) >> 4;
    if (motion >= 0x3D9 && motion <= 0x3E0) return kHoldTicks[motion - 0x3D9][pick];
    for (const auto& h : kHoldOther)
        if (h[0] == motion) return h[1 + pick];
    return -1;
}

// 0x004111A0 (no blend)
MotionTick EnemyMotionTick(int counter, int ticks) {
    MotionTick m;
    m.tick = counter % (ticks + 1);
    m.frame = m.tick / 2;
    m.nextFrame = m.frame;
    if (m.tick % 2 == 1) {
        if (m.tick != ticks) {
            m.interpolate = true;
            m.nextFrame = m.frame + 1 > ticks / 2 ? 0 : m.frame + 1;
        } else {
            m.frame = m.frame + 1;
            m.nextFrame = m.frame;
        }
    }
    m.ended = m.tick >= ticks;
    m.nearEnd = m.tick >= ticks - 1;
    return m;
}

// 0x004560B0 (fixed-motion branches)
EnemyDeathMotionChoice EnemyDeathMotion(int kind, int variant, uint32_t flags34, uint32_t flags1368,
                                        uint32_t flags136C, int randValue) {
    EnemyDeathMotionChoice c;
    if (variant == 4) {
        if ((flags136C & 0x2000000) == 0 || (flags136C & 0x8000000) == 0) {
            c.usesRand = true;
            c.motion = (randValue & 1) ? 0x41A : 0x404;
        } else {
            c.motion = 0x3DA;
            c.startTick = 0x19;
        }
        return c;
    }
    if (variant < 5 || variant > 6) {
        if (flags34 & 0x1000000) { c.motion = 0x3F9; return c; }
        if (flags1368 & 8) { c.motion = 0x1AC; return c; }
        if (flags1368 & 0x10) { c.motion = 0x1A5; return c; }
        if (flags1368 & 0x40) { c.motion = 0x279; return c; }
        if (flags1368 & 0x80) { c.motion = 0x229; return c; }
        if (kind >= 0xF && kind <= 0x11 && (flags136C & 0x100000)) { c.motion = 0x3DB; return c; }
        return c;  // kind 10 / default: walk-dependent (0x00456220), not decoded
    }
    if ((flags136C & 7) == 0) c.motion = 0x3DB;
    return c;
}

// 0x004550E0 state 0
EnemyDeathJumpOffset EnemyDeathJumpTarget(int variant, const float view[3], const int* r) {
    EnemyDeathJumpOffset o{};
    float extra = 0.0f, zoff = 0.0f, div = 1.0f;
    int k = 0;
    if (variant == 4) {
        k = 2;  // sign + %5 for a rotation kick of obj+0x64 (applied by the caller)
        zoff = view[2] <= -25.0f ? 1.5f : -5.0f;
    } else if (variant == 5) {
        zoff = -7.0f;
    } else if (variant == 6) {
        extra = 7.0f;
        zoff = -7.0f;
    } else {
        div = 4.0f;
        zoff = view[2] <= -25.0f ? 1.5f : -5.0f;
    }
    const int sign = 1 - 2 * (r[k] & 1);
    o.view[0] = (static_cast<float>(sign) * 0.5f) / div + view[0];
    o.view[1] = view[1] - (static_cast<float>(r[k + 1] % 5 + 1) * 0.5f + extra + 3.0f) / div;
    o.view[2] = zoff / div + view[2];
    o.divisor = div;
    o.randCalls = k + 2;
    return o;
}

// 0x0044DB50
void EnemyJump::Setup(const float from[3], const float to[3], uint32_t flags136C, uint32_t flags34) {
    for (int i = 0; i < 3; ++i) { start[i] = from[i]; target[i] = to[i]; }
    counter = 0;
    int minT = 0xF;
    if ((flags136C & 0x2000000) != 0 && (flags34 & 0x44000000) == 0) minT = 10;
    const float dx = from[0] - to[0], dz = from[2] - to[2];
    const float dist = std::sqrt(dx * dx + dz * dz);
    duration = Ftol(dist / (30.0f / static_cast<float>(minT)));
    if (duration < minT) duration = minT;
}

// 0x0044DDE0 (constants 0x00565E1C = +0.027222, 0x00565E20 = -0.027222)
bool EnemyJump::Step(float vel[3]) {
    const int n = counter;
    ++counter;
    if (n >= duration) return false;
    const float g = AsFloat(0x3CDF0123u);  // 0.027222222
    vel[0] = (target[0] - start[0]) / static_cast<float>(duration);
    const float dy = target[1] - start[1];
    vel[1] = static_cast<float>(counter) * -g +
             (static_cast<float>(duration * duration) * g + dy + dy) / static_cast<float>(duration * 2);
    vel[2] = (target[2] - start[2]) / static_cast<float>(duration);
    return true;
}

float EnemyFallGravity(int variant) {
    return (variant < 4 || variant > 5) ? AsFloat(0xBD16872Bu) : AsFloat(0xBCDF0123u);
}

EnemyFallResult EnemyFallStep(float& y, float& vy, float gravity, float floorY, int fallFrames) {
    vy += gravity;
    if (floorY < y + vy) return EnemyFallResult::Falling;
    y = floorY;
    if (fallFrames < 0x78) {
        const float a = vy < 0.0f ? -vy : vy;
        if (a > 0.15f) {
            vy *= -0.25f;
            return EnemyFallResult::Bounce;
        }
    }
    vy = 0.0f;
    return EnemyFallResult::Landed;
}

int EnemyPickTargetSingleAttacker(int randValue, const bool slotTaken[2]) {
    const int p = randValue & 1;
    return slotTaken[p] ? ~p : p;
}

}  // namespace game
