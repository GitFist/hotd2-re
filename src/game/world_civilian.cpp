// game::World - scripted civilians (actor type 0x10): create 0x0048A3E0, update 0x0048A920, the block script
// (0x0048B9E0 executes a block, 0x0048B1E0 waits for its end condition and looks ahead with 0x0048B760), turning to
// a point (0x0048C850 / 0x0048C990), motion changes (0x0048C310) and the civilian node callbacks (0x0048D1F0 draw,
// 0x0048D070 post-root). The skeleton itself is the enemy skeleton runtime (world_enemy.cpp).
//
// Script format (exe data, table 0x005702A8[params byte 1]): words {op, args...}. A block starts with op 0x2C (its
// end-condition flags) and runs until the next op > 0x2B (0x2C: the next block, 0x2D: end).

#include <algorithm>
#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"
#include "game/world_modes.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

float BitsF(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

// 3-word ops of 0x0048B760 / 0x0048B9E0
int OpWords(int op) {
    switch (op) {
    case 0: case 5: case 0xD: case 0x13: case 0x16: case 0x1A: case 0x1F: case 0x21: case 0x24: case 0x25: case 0x26:
        return 3;
    case 1:
        return 4;
    default:
        return 2;
    }
}

}  // namespace

// =====================================================================================================================
// Task
// =====================================================================================================================

uint32_t World::Impl::CivWord(uint32_t address) const {
    uint32_t value = 0;
    if (typingReferenceActive && (typingReference.CivilianWord(address, &value) || mem.Read(address, &value))) return value;
    return exe.U32(address);
}

float World::Impl::CivFloat(uint32_t address) const {
    const uint32_t bits = CivWord(address);
    float value = 0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void World::Impl::TaskCivilian(Task& t) {
    CivTask& c = t.As<CivTask>();
    if (c.fn == 0x0048A3E0) {
        actor8E4C = &c;  // 0x009C8E4C
        CivCreate(c);
        return;
    }
    if (c.fn == 0x0048A920) CivUpdate(c);
    else if (c.fn == 0x0048B140) CivOnVehicle(c);  // world_st3_vehicle.cpp
}

// 0x0048A3E0
void World::Impl::CivCreate(CivTask& c) {
    CivCtl& k = c.c;
    k = CivCtl{};
    k.trigPath = c.record.ParamS16(2);
    k.trigTime = c.record.ParamS16(4);
    c.lifeDelay = c.record.ParamS16(6);
    c.actorSlot = -1;
    c.slot120 = -1;
    c.civilian = true;
    c.anim.motion[0] = 0x294;
    c.anim.kind = static_cast<int16_t>(c.record.ParamS8(0));
    EnemyInitPose(c);  // Anim_InitEnemy 0x00410440 (+ actor slot 0x00409270)
    const uint32_t script = typingReferenceActive ? typingReference.CivilianScript(c.record.params[1]) :
                            exe.U32(0x005702A8 + 4 * static_cast<uint32_t>(c.record.params[1]));
    CivRunScript(c, script);
    c.anim.drawFlags |= 4;
    c.anim.rotOrder = 1;
    if (altModels != 0) c.flags38 |= 8;  // callback +0x1174 = 0x00412620
    switch (c.anim.kind) {
    case 0x20: case 0x23: k.attachSlot = 0; break;
    case 0x24: case 0x25: case 0x31: case 0x32: case 0x33: k.attachSlot = 4; break;
    case 0x26: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x38: k.attachSlot = 1; break;
    case 0x27: case 0x28: k.attachSlot = 2; break;
    case 0x2E: case 0x2F: case 0x30: k.attachSlot = 3; break;
    default: k.attachSlot = 5; break;
    }
    c.radius = c.anim.kind >= 0 && c.anim.kind < 0x48 ? F32(exe, 0x004C4D28 + 4 * c.anim.kind) : 0.0f;
    c.radius128 = 1.0f;
    c.target = -1;
    c.slot120 = -1;
    SyncLock(c);
    lockOn.AddDirect(c.lock);  // 0x00408E80
    c.slot120 = static_cast<int8_t>(c.lock->slot);
    ++countA0E8;
    if (!(c.c.flags & 0x8000000)) CountCivilian();  // 0x009A21BA / 0x009C9100[stage] (actor 0x64, world_st6.cpp)
    c.parts = c.record.ParamS32(8);  // 0x0045EBB0 (identity)
    // 0x00412440: part ids < 0x24 replace node models; ids > 0x11 also request their model (0x0041D5D0)
    if (c.parts) {
        for (uint32_t a = c.parts;; a += 2) {
            uint32_t w = 0;
            if (!mem.Read(a & ~3u, &w)) break;
            const int16_t id = static_cast<int16_t>((a & 2) ? (w >> 16) : (w & 0xFFFF));
            if (id < 0) break;
            if (id < 0x24) {
                const uint32_t def = exe.U32(0x004EC748 + 4 * id);
                const int node = static_cast<int>(exe.U32(def)) & 15;
                c.anim.nodes[node].model = static_cast<int32_t>(exe.U32(def + 4));
            }
            // 0x0041D5D0 (ids > 0x11): a load request for the part's model (queue entry type = its pol file
            // 0x004E83B4[model] != 0xE7; both types load: 0x00588C20[0/1] = 0x00418B80)
            if (id > 0x11 && residency) resQueue.push_back({ 0x50, exe.U32(exe.U32(0x004EC748 + 4 * id) + 4) });
        }
    }
    if (c.hp != 0) {  // +0x11C != 0: riding on another actor (0x0048B140)
        c.vehicle = static_cast<EnemyTask*>(obj9A2C88);  // 0x009A2C88 (world_st3_vehicle.cpp)
        c.fn = 0x0048B140;
    } else {
        c.fn = 0x0048A920;
    }
    k.children = c.record.ParamS16(0xC);
    if (k.children != 0) {  // child enemies (0x00408A20 per record P[0x10 + 4i]): parent +0x1394, +0x128 = 3.5
        k.child64 = nullptr;
        k.childList.clear();
        const int n = c.record.ParamS32(0xC);
        for (int i = 0; i < n && i < 12; ++i) {
            EnemyTask* ch = SpawnRecordEnemy(static_cast<uint32_t>(c.record.ParamS32(0x10 + 4 * i)));  // world_scene1.cpp
            if (!ch) continue;
            k.childList.push_back(ch);
            ch->parent = &c;
            ch->radius128 = 3.5f;
            if (!(k.flags & 0x8000000)) {  // hp by rank: 0x0056B0E4[rank] percent (game type 0)
                int idx = rank;
                if (runner->cfg.gameType == 2) {
                    // training: lesson 0 only, by the difficulty 0x009C9118 through {0, 0, 2, 4, 7}; difficulty 0: hp 1;
                    // other lessons keep the record's hp
                    static const int kIdx[5] = { 0, 0, 2, 4, 7 };
                    const int d = runner->cfg.playerVariant;
                    if (TrainLesson(*this) != 0) {
                        ch->hp = ch->hpMax;
                        continue;
                    }
                    if (d == 0) {
                        ch->hpMax = ch->hp = 1;
                        continue;
                    }
                    idx = kIdx[d < 0 || d > 4 ? 0 : d];
                }
                const int pct = exe.S16(0x0056B0E4 + static_cast<uint32_t>(idx)) & 0xFF;
                int16_t m = static_cast<int16_t>((pct * ch->hp) / 100);
                if (m < 1) m = 1;
                ch->hpMax = m;
                ch->hp = m;
            }
        }
    }
}

// 0x0048CA60: dead children (+0x34 & 0x4000000) leave the list; +0x6C = the killer of the last one
static void CivChildren(World::Impl& w, CivTask& c) {
    CivCtl& k = c.c;
    for (size_t i = 0; i < k.childList.size();) {
        EnemyTask* ch = k.childList[i];
        bool live = false;
        for (Task* t : w.ts.Live())
            if (t->data.get() == ch) live = true;
        if (live && !(ch->flags34 & 0x4000000)) {
            ++i;
            continue;
        }
        if (!live) w.Note("civilian child ended without the dead flag (the original reads freed memory)");
        k.scorePlayer = live ? ch->killer : k.scorePlayer;
        if (k.child64 == ch) k.child64 = nullptr;
        --k.children;
        k.childList.erase(k.childList.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

// 0x0048D140: the civilian's scream when its alternative script starts: by voice +0x81 (op 0x2A), else by skeleton
// kind; first 0x0041D480(2, 0x1100A0, 0) = Snd_Command(.., 0x1100A0): every SE channel 0..14 stopped and released
static void CivVoice(World::Impl& w, CivTask& c) {
    uint32_t id;
    int v = c.c.voice;
    if (v < 0 || v > 4) {
        switch (c.anim.kind) {
        case 0x20: case 0x23: case 0x2D: v = 4; break;
        case 0x21: case 0x22: v = 3; break;
        case 0x24: case 0x25: case 0x2E: case 0x31: case 0x32: case 0x33: v = 0; break;
        case 0x26: case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: v = 2; break;
        default: v = 1; break;
        }
    }
    static const uint32_t kIds[5] = { 0x2000001A, 0x20000012, 0x2000000B, 0x20000011, 0x2000000D };
    id = kIds[v];
    w.SoundCall(World::kCommand, 0x1100A0);
    w.Sound(id);
}

// 0x0048A920
void World::Impl::CivUpdate(CivTask& c) {
    actor8E4C = &c;  // 0x009C8E4C
    CivCtl& k = c.c;
    if (c.lock) c.slot120 = static_cast<int8_t>(c.lock->slot);
    CivChildren(*this, c);  // 0x0048CA60
    if (k.perFrameFn == 0x0048DAB0) {
        // 0x0048DAB0: dead -> no-op function; else turn (0x0048C990, step 0x80) towards the children's mean position
        if (c.flags34 & 0x4000000) {
            k.perFrameFn = 0x0041EBB0;
        } else if (k.children != 0) {
            float s[3] = { 0, 0, 0 };
            for (EnemyTask* ch : k.childList)
                for (int i = 0; i < 3; ++i) s[i] = s[i] + ch->pos[i];
            const float n = static_cast<float>(k.children);
            const float v[3] = { c.pos[0] - s[0] / n, c.pos[1] - s[1] / n, c.pos[2] - s[2] / n };
            const int a = CivLocalAngle(c, v);
            if (0x80 < a) c.rot[1] += 0x80;
            else if (a < -0x80) c.rot[1] -= 0x80;
            else c.rot[1] += a;
        }
    } else if (k.perFrameFn == 0x0048DA20) {
        // 0x0048DA20: fall (set by 0x0048D9F0): floor under pos + 0x004C43B0, y velocity += y acceleration, y += it;
        // at or below the floor: y = floor, +0x18 = 1 (condition 0x400), back to the no-op function
        int mat = 0;
        const float g = FloorHeight(c.pos[0], c.pos[1] + F32(exe, 0x004C43B0), c.pos[2], &mat);  // 0x00409D40
        c.vel[1] = c.acc[1] + c.vel[1];
        c.pos[1] = c.vel[1] + c.pos[1];
        if (g >= c.pos[1]) {  // (fcom: unordered keeps falling)
            c.pos[1] = g;
            k.c18 = 1;
            k.perFrameFn = 0x0041EBB0;
        }
    } else if (k.perFrameFn == 0x0048DBC0) {  // y += y velocity
        c.pos[1] = c.vel[1] + c.pos[1];
    } else if (k.perFrameFn == 0x0048DC10) {  // pos = Translate(pos) RotX RotZ RotY applied to the velocity
        Mat4 m = Mat4::Identity();
        mx::Translate(m, c.pos[0], c.pos[1], c.pos[2]);
        mx::RotX(m, c.rot[0]);
        mx::RotZ(m, c.rot[2]);
        mx::RotY(m, c.rot[1]);
        const float v[3] = { c.vel[0], c.vel[1], c.vel[2] };
        mx::Transform(m, v, c.pos);
    } else if (k.perFrameFn != 0x0041EBB0) {
        Note("civilian per-frame script function not ported");
    }
    if (k.path != 0) CivTurn(c);  // 0x0048C850
    if (k.moveTimer != 0) {
        --k.moveTimer;
        if (k.moveTimer == 0) {
            memcpy(c.pos, k.moveTarget, 12);
        } else {
            for (int i = 0; i < 3; ++i) c.pos[i] = c.vel[i] + c.pos[i];
        }
    }
    // 0x0041DC70 / 0x0041DCC0 (light, renderer); 0x00411090: skeleton + shadow 0x0040A590
    AnimDraw(c);
    EnemyShadow(c);
    // animation counter (game type 2: only once the lesson runs, flag 0xF1)
    if (runner->cfg.gameType != 2 || runner->flags[0xF1] != 0) {
        EnemyAnim& a = c.anim;
        const int16_t loops = k.loops;
        if (loops < 0) {
            ++a.counter[0];
        } else if (loops > 0) {
            if (k.loopTick == 0) {
                if (a.tick[0] < MotionTicks(a.motion[0])) {
                    ++a.counter[0];
                } else {
                    k.loops = static_cast<int16_t>(loops - 1);
                    if (k.loops != 0) ++a.counter[0];
                }
            } else if (a.tick[0] < k.loopTick) {
                ++a.counter[0];
            } else if (k.flags & 0x100) {
                k.loops = static_cast<int16_t>(loops - 1);
                if (k.loops != 0) a.counter[0] = 0;
            }
        }
    }
    if (CivWait(c)) CivRunScript(c, k.script);
    // shot while the alternative script is armed and the camera is shooting (0x009C6F08 == 2)
    const int modeCur = runner->camera.modeCur;
    auto toAlt = [&](uint32_t s, bool scoreFirst) {
        k.delay = -1;
        k.path = 0;
        k.soundList = 0;
        k.soundTimer = 0;
        CivRunScript(c, s);
        k.altScript = 0;
        k.jump = 0;
        if (scoreFirst) {  // killed by its enemy: both players lose 100 unless the block has flag 0x8000000
            if (!(k.flags & 0x8000000)) {
                AddScore(0, -100);
                AddScore(1, -100);
            }
        }
        CivVoice(*this, c);  // 0x0048D140
        if (k.lookMode != 0) k.lookMode = 6;
        if (runner->cfg.gameType == 2) runner->globals[0x009A2234] = 1;  // training: the lesson is failed
    };
    if (k.altScript == 0 || modeCur != 2) {
        c.flags34 &= ~0xEu;
    } else if (!(c.flags34 & 0x4000000)) {
        if (c.flags34 & 8) {
            // world position of node +0xAC (its model-view matrix x the camera matrix)
            float nodePos[3];
            {  // 0x004A9230 camera matrix, 0x004A92A0 node +0x28, 0x004A8CC0
                Mat4 top = cam.inverse;
                mx::MulLeft(top, c.anim.nodes[k.penaltyNode & 31].mv);
                mx::GetTranslation(top, nodePos);
            }
            int p;
            if ((c.flags34 & 6) == 2) p = 0;
            else if ((c.flags34 & 6) == 4) p = 1;
            else p = Rand(0) & 1;
            // 0x00415430(p, 0, 0, 1, -1): a life (hit timer ignored) and 100 points; then 100 more, streak 0, a hit,
            // the penalty mark 0x0048E080
            {
                Player& P = player[p];
                if (!(gameMode == 6 && P.state != 1 && (P.state < 4 || P.state > 5))) {
                    --P.lives;
                    rankAdj -= 2;
                    AddScore(p, -100);
                    if (runner->camera.modeCur != 2 && runner->camera.modeReq != 2 && P.lives < 1) P.lives = 1;
                }
                AddScore(p, -100);
                P.streak = 0;
                ++P.hits;
            }
            SpawnPenaltyMark(p, nodePos);  // world_scene1.cpp
            c.flags34 |= 0x4000000;
            toAlt(k.altScript2 != 0 ? k.altScript2 : k.altScript, false);
        }
    } else {
        toAlt(k.altScript, true);  // killed by its enemy
    }
    if (k.soundTimer != 0 && cfg.gameMode != 10) {
        if (--k.soundTimer == 0) {
            TypingSound(k.sound);
            if (k.soundList) {
                k.sound = CivWord(k.soundList);
                k.soundTimer = static_cast<int32_t>(CivWord(k.soundList + 4));
                k.soundList = CivWord(k.soundList + 8) == 0xFFFFFFFFu ? 0 : k.soundList + 8;
            }
        }
    }
    if (k.c2F != 0) --k.c2F;
    EnemyViewPos(c);  // 0x00409B70(4.0)
    // collision sphere +0x12C
    {
        auto nodeWorld = [&](int n, float out[3]) {  // camera matrix 0x009A6040[cam] (0x004A9230), x node +0x28 (0x004A92A0)
            Mat4 top = cam.inverse;
            mx::MulLeft(top, c.anim.nodes[n].mv);
            mx::GetTranslation(top, out);  // 0x004A8CC0
        };
        switch (k.sphereMode) {
        case 0: memcpy(c.sphere, c.pos, 12); break;
        case 1: nodeWorld(2, c.sphere); break;
        case 2: nodeWorld(1, c.sphere); break;
        case 3: {
            float a[3], b[3];
            nodeWorld(15, a);
            nodeWorld(12, b);
            for (int i = 0; i < 3; ++i) c.sphere[i] = (b[i] + a[i]) * 0.5f;
            break;
        }
        default: break;
        }
    }
    CivAttachments(c);  // 0x0048CD10 (world_st1b_civ.cpp)
    // lifetime
    const PropWorld w = PropState();
    auto destroy = [&] {
        if (!(k.flags2 & 1)) --countA0E8;
        ReleasePartModels(c.parts);  // 0x004124B0
        if (!k.childList.empty() && ts.Current() && ts.Current()->data.get() == &c)
            civGhosts.push_back(std::move(ts.Current()->data));  // children may still read +0x1394 (stale block)
        EnemyDestroy(c);
    };
    auto childFlags = [&] {  // 0x0048B0CE: a dead civilian's children lose 0x1000000 and get 0x136C | 1
        if (!(c.flags34 & 0x4000000)) return;
        for (EnemyTask* ch : k.childList) {
            ch->flags34 &= ~0x1000000u;
            ch->flags136C |= 1;
        }
    };
    if (GlobalInt(0x009A2230, 0) != 0 && !(k.flags & 0x20000000)) {
        k.lifeTimer = 1;
        childFlags();
        return;
    }
    if (k.lifeTimer == 0) {
        if (w.camPath == k.trigPath && w.camTime == k.trigTime) {
            k.lifeTimer = c.lifeDelay;
            childFlags();
            return;
        }
        if (!(k.flags & 0x2000000) || CivOnScreen(c) || modeCur == 2 || k.children != 0) {
            childFlags();
            return;
        }
        destroy();
    } else {
        if (--k.lifeTimer != 0) {
            childFlags();
            return;
        }
        if (k.children != 0) {
            k.lifeTimer = 1;
            childFlags();
            return;
        }
        destroy();
    }
}

// =====================================================================================================================
// Script
// =====================================================================================================================

// 0x0048B9E0: execute the block at addr up to the next op > 0x2B (stored as the next block)
bool World::Impl::CivRunScript(CivTask& c, uint32_t addr) {
    CivCtl& k = c.c;
    const uint32_t entry = k.flags;
    for (int guard = 0; guard < 4096; ++guard) {
        const int op = static_cast<int>(CivWord(addr));
        auto A = [&](int i) { return CivWord(addr + 4 * i); };
        uint32_t next = addr + 4 * OpWords(op);
        switch (op) {
        case 0:
        case 1:
            k.loops = static_cast<int16_t>(A(2));
            k.loopTick = 0;
            if (c.anim.motion[0] != static_cast<int>(A(1))) {
                c.anim.motion[0] = static_cast<int>(A(1));
                if (k.flags & 0x100000) c.anim.drawFlags |= 2;
                else c.anim.drawFlags &= ~2u;
                CivSetMotion(c, entry, op == 0 ? 0 : static_cast<int>(A(3)));
            }
            break;
        case 2: k.loopTick = static_cast<int32_t>(A(1)); break;
        case 3: k.blendN = static_cast<int16_t>(A(1)); break;
        case 4: k.waitTick = static_cast<int16_t>(A(1)); break;
        case 5:
            k.path = static_cast<int32_t>(A(1));
            k.radius = BitsF(A(2));
            if (k.path > 0)
                for (int i = 0; i < 3; ++i) k.target[i] = CivFloat(static_cast<uint32_t>(k.path) + 4 * i);
            break;
        case 6:
            k.path2 = A(1);
            for (int i = 0; i < 3; ++i) k.target[i] = CivFloat(k.path2 + 4 * i);
            break;
        case 7: {
            k.path = 1;
            const double r = static_cast<double>(static_cast<int32_t>(A(1))) * mx::kToRad;
            const double d = F32(exe, 0x004C43B0);
            k.target[0] = static_cast<float>(c.pos[0] - std::sin(r) * d);
            k.target[1] = c.pos[1];
            k.target[2] = static_cast<float>(c.pos[2] - std::cos(r) * d);
            break;
        }
        case 8: c.rot[1] = static_cast<int32_t>(A(1)); break;
        case 9: k.delay = static_cast<int16_t>(A(1)); break;
        case 0xA: k.cnt22 = static_cast<int16_t>(A(1)); break;
        case 0xB: k.cnt20 = static_cast<int16_t>(A(1)); break;
        case 0xC: k.cnt24 = static_cast<int16_t>(A(1)); break;
        case 0xD:
            k.waitPath = static_cast<int16_t>(A(1));
            k.waitTime = static_cast<int16_t>(A(2));
            break;
        case 0xE: k.altScript = A(1); break;
        case 0xF: k.altScript2 = A(1); break;
        case 0x10:
            if (A(1) == 0) {
                k.perFrameFn = 0x0041EBB0;
            } else if (A(1) == 0x0048DA90) {  // returns the next op; sets +0x5C = 0x0048DAB0 (look at the children)
                k.perFrameFn = 0x0048DAB0;
                k.c18 = 0;
            } else if (A(1) == 0x0048D9F0) {  // returns the next op; +0x5C = 0x0048DA20 (fall), y velocity 0, y acc
                k.perFrameFn = 0x0048DA20;
                c.vel[1] = 0.0f;
                c.acc[1] = BitsF(0xBCDF0123u);
            } else if (A(1) == 0x0048DBD0) {  // +0x5C = 0x0048DC10 (move by a local velocity), velocity = 3 words
                k.perFrameFn = 0x0048DC10;
                for (int i = 0; i < 3; ++i) c.vel[i] = BitsF(A(2 + i));
                next = addr + 20;
            } else if (A(1) == 0x0048DB90) {  // +0x5C = 0x0048DBC0 (rise), y velocity = 1 word, +0x34 | 0x80000
                k.perFrameFn = 0x0048DBC0;
                c.vel[1] = BitsF(A(2));
                c.flags34 |= 0x80000;
                next = addr + 12;
            } else {
                Note("civilian script function op 0x10 (" + std::to_string(A(1)) + ") not ported");
                k.script = addr;
                return false;
            }
            break;
        case 0x11: k.skip = static_cast<int16_t>(A(1)); break;
        case 0x12: k.lifeTimer = static_cast<int16_t>(A(1)); break;
        case 0x13: case 0x14: case 0x15:
            CivAttachOp(c, op, A(1), A(2));  // 0x0048CAE0 / 0x0048CB60 / 0x0048CBF0 (world_st1b_civ.cpp)
            break;
        case 0x16:
            k.radiusTarget = BitsF(A(1));
            k.radiusStep = (k.radiusTarget - c.radius128) / static_cast<float>(static_cast<int32_t>(A(2)));
            break;
        case 0x17: k.sphereMode = static_cast<uint8_t>(A(1)); break;
        case 0x18:
            for (int i = 0; i < 3; ++i) c.pos[i] = CivFloat(A(1) + 4 * i);
            for (int i = 0; i < 3; ++i) c.rot[i] = static_cast<int32_t>(CivWord(A(1) + 0xC + 4 * i));
            break;
        case 0x19: runner->camera.routeBranch = static_cast<int16_t>(A(1)); break;  // 0x009C88A4 (look table entry)
        case 0x1A:
            if (k.children != 0) {
                k.c2C = static_cast<int16_t>(A(1));
                k.c2E = static_cast<int16_t>(A(2));
            }
            break;
        case 0x1B:
            if (cfg.gameMode != 10) letterbox = static_cast<int8_t>(A(1));
            break;
        case 0x1C: runner->flags[A(1) & 0xFF] = 1; break;
        case 0x1D:
            if ((skip2230 != 0 && (k.flags & 0x20000000)) || k.lifeTimer == 0)
                TypingVoice(static_cast<int16_t>(A(1)));  // source/native dialogue selector follows the active bank
            break;
        case 0x1E: k.jump = A(1); break;
        case 0x1F: k.jump = GlobalInt(0x009A2226, front.menuCursor) == 1 ? A(2) : A(1); break;  // mode-select cursor 1 = original mode
        case 0x20: k.flagIdx = static_cast<int16_t>(A(1)); break;
        case 0x21:
            k.sound = A(1);
            k.soundTimer = static_cast<int32_t>(A(2));
            break;
        case 0x22: {
            const uint32_t l = A(1);
            if (l == 0) {
                k.soundList = 0;
            } else {
                k.sound = CivWord(l);
                k.soundTimer = static_cast<int32_t>(CivWord(l + 4));
                k.soundList = CivWord(l + 8) == 0xFFFFFFFFu ? 0 : l + 8;
            }
            break;
        }
        case 0x23:
            k.lookMode = static_cast<int32_t>(A(1));
            if (k.lookMode == 2 && k.children == 0) k.lookMode = 0;
            else if (k.lookMode == 2) k.lookObj = k.childList.empty() ? nullptr : k.childList[0];  // +0x90 = *(+0x60)
            break;
        case 0x24:
            k.lookMode = static_cast<int32_t>(A(1));
            k.lookTarget = A(2);
            break;
        case 0x25:
            k.faceTimer = static_cast<int32_t>(A(1));
            k.faceMode = static_cast<int32_t>(A(2));
            k.faceFrame = 0;
            break;
        case 0x26: {
            const int32_t p = static_cast<int32_t>(A(1));
            if (p < 1) memcpy(k.moveTarget, playerPos, 12);
            else
                for (int i = 0; i < 3; ++i) k.moveTarget[i] = CivFloat(static_cast<uint32_t>(p) + 4 * i);
            k.moveTimer = static_cast<int16_t>(A(2));
            for (int i = 0; i < 3; ++i) c.vel[i] = (k.moveTarget[i] - c.pos[i]) / static_cast<float>(k.moveTimer);
            break;
        }
        case 0x27: c.anim.scale = BitsF(A(1)); break;
        case 0x28: k.penaltyNode = static_cast<int16_t>(A(1)); break;
        case 0x29: c.flags34 |= A(1); break;
        case 0x2A: k.voice = static_cast<int8_t>(A(1)); break;
        case 0x2B:
            // game mode 6: +0xBC = word 1 (counted down per frame, 0x0048A920) and +0xC0 = addr + 8; nothing reads
            // either (every access in the exe: ctor clear 0x0048A592, countdown 0x0048AD97, these writes 0x0048C20F /
            // 0x0048C21F), so the op has no other effect
            if (cfg.gameMode == 6) k.c2F = static_cast<int16_t>(A(1));
            if (A(2) != 0) Note("civilian script op 0x2B with a non-zero second word (the original loops forever)");
            next = addr + 24;
            break;
        case 0x2C:
            k.flags = A(1) & ~0x4000000u;
            if (k.flags & 0x80000) {
                --countA0E8;
                k.flags2 |= 1;
            }
            if (k.flags & 0x4000) k.loopTick = 0;
            if (k.flags & 0x40000) c.flags34 &= ~0x10000u;
            else c.flags34 |= 0x10000;
            if (k.flags & 0x10000000) {
                // rescue bonus: 0x009CA0EC += 1, per-stage list 0x009C89C0 / 0x009C8EC0 (skeleton kinds), +400 for
                // the rescuer +0x6C (-1: both)
                ++rescueTotal;
                const int st = cfg.eventMode >= 0 && cfg.eventMode < 10 ? cfg.eventMode : 0;
                const int n = rescueCount[st]++;
                ResultRescue(k.scorePlayer);
                if (n >= 0 && n < 10) rescueKinds[st][n] = static_cast<int16_t>(c.anim.kind);
                if (k.scorePlayer == -1) {
                    AddScore(0, 400);
                    AddScore(1, 400);
                } else {
                    AddScore(k.scorePlayer, 400);
                }
                k.flags &= ~0x10000000u;
            }
            next = addr + 8;
            break;
        default:
            Note("civilian script op " + std::to_string(op) + " unknown");
            break;
        }
        addr = next;
        if (static_cast<int>(CivWord(addr)) > 0x2B) {
            k.script = addr;
            k.flags |= entry & 0x8000000;
            return true;
        }
    }
    return false;
}

// 0x0048B760: the state a block sets that its end condition reads (look-ahead), returns the block's end
uint32_t World::Impl::CivDryRun(CivTask& c, uint32_t addr) {
    CivCtl& k = c.c;
    for (int guard = 0; guard < 4096; ++guard) {
        const int op = static_cast<int>(CivWord(addr));
        auto A = [&](int i) { return CivWord(addr + 4 * i); };
        uint32_t next = addr + 8;
        switch (op) {
        case 0:
            k.loops = static_cast<int16_t>(A(2));
            c.anim.tick[0] = 0;
            next = addr + 12;
            break;
        case 0x13: case 0x16: case 0x1A: case 0x1F: case 0x21: case 0x24: case 0x25: case 0x26:
            next = addr + 12;
            break;
        case 1:
            k.loops = static_cast<int16_t>(A(2));
            c.anim.tick[0] = static_cast<int32_t>(A(3));
            next = addr + 16;
            break;
        case 4: k.waitTick = static_cast<int16_t>(A(1)); break;
        case 5:
            k.path = static_cast<int32_t>(A(1));
            k.radius = BitsF(A(2));
            if (k.path > 0)
                for (int i = 0; i < 3; ++i) k.target[i] = CivFloat(static_cast<uint32_t>(k.path) + 4 * i);
            next = addr + 12;
            break;
        case 6:
            k.path2 = A(1);
            for (int i = 0; i < 3; ++i) k.target[i] = CivFloat(k.path2 + 4 * i);
            break;
        case 7: {
            k.path = 1;
            const double r = static_cast<double>(static_cast<int32_t>(A(1))) * mx::kToRad;
            const double d = F32(exe, 0x004C43B0);
            k.target[0] = static_cast<float>(c.pos[0] - std::sin(r) * d);
            k.target[1] = c.pos[1];
            k.target[2] = static_cast<float>(c.pos[2] - std::cos(r) * d);
            break;
        }
        case 9: k.delay = static_cast<int16_t>(A(1)); break;
        case 0xA: k.cnt22 = static_cast<int16_t>(A(1)); break;
        case 0xB: k.cnt20 = static_cast<int16_t>(A(1)); break;
        case 0xC: k.cnt24 = static_cast<int16_t>(A(1)); break;
        case 0xD:
            k.waitPath = static_cast<int16_t>(A(1));
            k.waitTime = static_cast<int16_t>(A(2));
            next = addr + 12;
            break;
        case 0x10:
            if (A(1) == 0x0048DA90) {  // called (returns the next op), then +0x5C = 0x0041EBB0
                k.perFrameFn = 0x0041EBB0;
            } else if (A(1) == 0x0048D9F0) {  // its side effects stay (y velocity 0, y acc), then +0x5C = 0x0041EBB0
                c.vel[1] = 0.0f;
                c.acc[1] = BitsF(0xBCDF0123u);
                k.perFrameFn = 0x0041EBB0;
            } else if (A(1) == 0x0048DBD0) {  // side effects stay, then +0x5C = 0x0041EBB0
                for (int i = 0; i < 3; ++i) c.vel[i] = BitsF(A(2 + i));
                k.perFrameFn = 0x0041EBB0;
                next = addr + 20;
            } else if (A(1) == 0x0048DB90) {
                c.vel[1] = BitsF(A(2));
                c.flags34 |= 0x80000;
                k.perFrameFn = 0x0041EBB0;
                next = addr + 12;
            } else if (A(1) != 0) {
                Note("civilian script function op 0x10 (look-ahead) not ported");
                return addr + 8;
            }
            break;
        case 0x20: k.flagIdx = static_cast<int16_t>(A(1)); break;
        case 0x2B: next = A(2) == 0 ? addr + 24 : addr; break;
        case 0x2D: next = addr; break;
        default: break;
        }
        addr = next;
        if (static_cast<int>(CivWord(addr)) > 0x2B) return addr;
    }
    return addr;
}

// The players' point for path < 0 (-1: the players, -2: mirrored through the civilian)
void World::Impl::CivPlayerTarget(const CivTask& c, float out[3], bool fromTop) {
    if (c.c.path == -1) {
        out[0] = playerPos[0];
        out[2] = playerPos[2];
    } else {
        out[0] = (c.pos[0] + c.pos[0]) - playerPos[0];
        out[2] = (c.pos[2] + c.pos[2]) - playerPos[2];
    }
    out[1] = playerPos[1];
    if (c.fn == 0x0048B140 && c.vehicle) {
        // into the vehicle frame: inverse(0x004A8D20) of [top] T(vehicle) RotX RotZ RotY, the point through it. 0x0048C850
        // starts from identity; 0x0048B1E0 from the stack top as 0x0048B140 left it (the view and the vehicle ops
        // already applied: the ops twice on the view)
        const EnemyTask& v = *c.vehicle;
        Mat4 m = fromTop ? c.frameMv : Mat4::Identity();
        mx::Translate(m, v.pos[0], v.pos[1], v.pos[2]);
        mx::RotX(m, v.rot[0]);
        mx::RotZ(m, v.rot[2]);
        mx::RotY(m, v.rot[1]);
        const Mat4 inv = mx::Inverse(m);
        const float in[3] = { out[0], out[1], out[2] };
        mx::Transform(inv, in, out);
    } else if (c.fn == 0x0048B140) {
        Note("civilian on a vehicle without the vehicle: target in world space");
    }}

// identity; RotY(-ry); RotZ(-rz); RotX(-rx); v -> atan2(x, z) of the result
int World::Impl::CivLocalAngle(const EnemyTask& e, const float v[3]) {
    Mat4 m = Mat4::Identity();
    mx::RotY(m, -e.rot[1]);
    mx::RotZ(m, -e.rot[2]);
    mx::RotX(m, -e.rot[0]);
    float l[3];
    mx::Transform(m, v, l);
    return TruncToAngle(std::atan2(static_cast<double>(l[0]), static_cast<double>(l[2])));
}

// 0x0048B1E0: end condition of the current block; on success the following blocks whose conditions already hold
// are skipped (look-ahead 0x0048B760) and true is returned (the caller runs the block at k.script)
bool World::Impl::CivWait(CivTask& c) {
    CivCtl& k = c.c;
    const int16_t s0 = k.loops, s1 = k.cnt20, s2 = k.cnt22, s3 = k.cnt24, s4 = k.waitPath, s5 = k.waitTime,
                  s6 = k.waitTick, s7 = k.flagIdx;
    uint32_t f = k.flags;
    uint32_t cur = k.script;
    bool ret = false;
    const PropWorld w = PropState();
    const int modeCur = runner->camera.modeCur;
    while (((f & 0x40003FFF) != 0 || k.delay >= 0) && (f & 0x14000000) == 0) {
        const bool waiting =
            (!(f & 1) || k.cnt22 < attackers) && (!(f & 2) || k.cnt22 < liveEnemies) &&
            (!(f & 8) || k.cnt24 < countA0E8) && (!(f & 4) || k.cnt20 < k.children) &&
            (!(f & 0x100) || k.loops != 0) && (!(f & 0x200) || c.anim.tick[0] != k.waitTick) &&
            (!(f & 0x400) || k.c18 == 0) && !(f & 0x800) &&
            (!(f & 0x1000) || modeCur != 2 || (!runner->camera.aimedOnce && !runner->camera.settled)) &&  // 0x009C6F2F / 0x009C6F2D
            (!(f & 0x2000) || runner->flags[k.flagIdx & 0xFF] == 0);
        bool met = !waiting;
        if (waiting) {
            bool other = true;  // LAB_0048B52E
            if (f & 0x30) {
                float t[3];
                if (k.path < 0) CivPlayerTarget(c, t, true);
                else memcpy(t, k.target, 12);
                if (!(f & 0x10)) {
                    const float v[3] = { c.pos[0] - t[0], c.pos[1] - t[1], c.pos[2] - t[2] };
                    if (CivLocalAngle(c, v) == 0) {
                        k.path = 0;
                        met = true;
                        other = false;
                    }
                } else {
                    const float dz = c.pos[2] - t[2], dx = c.pos[0] - t[0];
                    const float d = std::sqrt(dz * dz + dx * dx);
                    if (!(k.radius < d)) {
                        if (k.path > 0 && k.radius <= 1.0f) {
                            c.pos[0] = t[0];
                            c.pos[2] = t[2];
                        }
                        k.path = 0;
                        met = true;
                        other = false;
                    }
                }
            }
            if (other) {
                bool done = false;
                if (f & 0x40) {
                    Mat4 m = Mat4::Identity();
                    mx::RotY(m, -c.rot[1]);
                    mx::RotZ(m, -c.rot[2]);
                    mx::RotX(m, -c.rot[0]);
                    const float v[3] = { k.target[0] - c.pos[0], k.target[1] - c.pos[1], k.target[2] - c.pos[2] };
                    float l[3];
                    mx::Transform(m, v, l);
                    if (0.0f < l[2]) {
                        k.path = 0;
                        done = true;
                    }
                }
                if (!done) {
                    if (((f & 0x80) && w.camPath == k.waitPath && w.camTime == k.waitTime) ||
                        ((f & 0x40000000) && players >= 1)) {
                        done = true;
                    } else {
                        const int16_t d = k.delay;
                        if (d < 0) break;
                        k.delay = static_cast<int16_t>(d - 1);
                        if (d != 0) break;
                        done = true;
                    }
                }
                met = done;
            }
        }
        if (!met) break;
        // LAB_0048B621
        k.delay = -1;
        ret = true;
        k.c18 = 0;
        uint32_t blk;
        if (k.jump == 0) {
            blk = cur;
            while (k.skip != 0) {
                blk = CivDryRun(c, blk);
                --k.skip;
            }
        } else {
            blk = k.jump;
            k.jump = 0;
        }
        f = CivWord(blk + 4);
        k.script = blk;
        cur = CivDryRun(c, blk);
        if (CivWord(cur) == 0x2D) break;
    }
    k.loops = s0;
    k.cnt20 = s1;
    k.cnt22 = s2;
    k.cnt24 = s3;
    k.waitPath = s4;
    k.waitTime = s5;
    k.waitTick = s6;
    k.flagIdx = s7;
    return ret;
}

// 0x0048C310: start the motion at anim +0x20 from tick `start` (blend over +0x0E frames unless flag 0x200000)
// Alignment (stage 1 auto-play capture, notes/agents/stage1b.md): flags 0x18000 turn the body (+0x68) by the yaw
// difference of the root + node 1 forward vectors (current angles vs the new motion's frame start / 2); 0x20000 moves
// the position so node 1 stays where it is (the original takes node 1's model-view matrix through the camera; the
// native node matrix is already world); 0x10000 / 0x8000 then set / rotate the node angles the blend starts from.
void World::Impl::CivSetMotion(CivTask& c, uint32_t entryFlags, int start) {
    CivCtl& k = c.c;
    const uint32_t f = k.flags;
    const int nodes = exe.S16(0x004E0724 + c.anim.kind * 2);
    const Motion* m = EnemyMotion(c.anim.motion[0], nodes);
    float root[3] = {};
    int16_t r0[3] = {}, r1[3] = {};
    if (m && !m->data.empty() && start / 2 < m->StoredFrames()) {  // 0x00412F50(kind, motion, start / 2)
        m->RootPosition(start / 2, root);
        m->Rotation(start / 2, 0, r0);
        m->Rotation(start / 2, 1, r1);
    } else if (f & 0x38000) {
        Note("civilian motion change: motion frame data missing (0x00412F50)");
    }
    EnemyNode& n0 = c.anim.nodes[0];
    EnemyNode& n1 = c.anim.nodes[1];
    int d = 0;
    if (f & 0x18000) {
        const float z1[3] = { 0.0f, 0.0f, 1.0f };
        float nv[3], ov[3];
        Mat4 a = Mat4::Identity();
        mx::RotZ(a, r0[2]);
        mx::RotY(a, r0[1]);
        mx::RotX(a, r0[0]);
        mx::RotZ(a, r1[2]);
        mx::RotY(a, r1[1]);
        mx::RotX(a, r1[0]);
        mx::Transform(a, z1, nv);
        a = Mat4::Identity();
        mx::RotZ(a, n0.cur[2]);
        mx::RotY(a, n0.cur[1]);
        mx::RotX(a, n0.cur[0]);
        mx::RotZ(a, n1.cur[2]);
        mx::RotY(a, n1.cur[1]);
        mx::RotX(a, n1.cur[0]);
        mx::Transform(a, z1, ov);
        auto ang = [](float x, float z) {
            const float v = static_cast<float>(std::atan2(static_cast<double>(x), static_cast<double>(z)) * mx::kToAngle);
            return static_cast<int>(static_cast<int16_t>(static_cast<int>(v)));
        };
        d = ang(ov[0], ov[2]) - ang(nv[0], nv[2]);
        c.rot[1] += d;
    }
    if (f & 0x20000) {
        const uint32_t sk = exe.U32(exe.U32(0x004E0430 + 4 * static_cast<uint32_t>(c.anim.kind)) + 0x18);
        const float s = c.anim.scale;
        float cur[3];
        {  // 0x004A9230(camera); 0x004A92A0(node 1 +0x130 = its model-view): node 1 x camera matrix (not exactly the
           // inverse of the view: like the body point +0x100)
            Mat4 q = cam.inverse;
            mx::MulLeft(q, n1.mv);
            mx::GetTranslation(q, cur);
        }
        Mat4 a = Mat4::Identity();
        if (c.fn == 0x0048B140 && c.vehicle) {  // on the vehicle: its frame first (the world delta is then added to the
            const EnemyTask& v = *c.vehicle;    // local position, as the original does)
            mx::Translate(a, v.pos[0], v.pos[1], v.pos[2]);
            mx::RotX(a, v.rot[0]);
            mx::RotZ(a, v.rot[2]);
            mx::RotY(a, v.rot[1]);
        } else if (c.fn == 0x0048B140) {
            Note("civilian motion change on a vehicle without the vehicle (0x0048C310)");
        }
        mx::Translate(a, c.pos[0], c.pos[1], c.pos[2]);
        mx::RotX(a, c.rot[0]);
        mx::RotZ(a, c.rot[2]);
        mx::RotY(a, c.rot[1]);
        mx::Translate(a, s * root[0], root[1] * s, root[2] * s);
        mx::RotZ(a, r0[2]);
        mx::RotY(a, r0[1]);
        mx::RotX(a, r0[0]);
        mx::Translate(a, F32(exe, sk + 4) * s, F32(exe, sk + 8) * s, F32(exe, sk + 0xC) * s);
        float nw[3];
        mx::GetTranslation(a, nw);
        c.pos[0] = (cur[0] - nw[0]) + c.pos[0];
        c.pos[1] = (cur[1] - nw[1]) + c.pos[1];
        c.pos[2] = (cur[2] - nw[2]) + c.pos[2];
        memcpy(c.anim.root, root, 12);
    }
    int n = 0;
    if (!(f & 0x200000)) {
        if (f & 0x8000) {
            Mat4 a = Mat4::Identity();
            mx::RotX(a, -n0.cur[0]);
            mx::RotY(a, -n0.cur[1]);
            mx::RotZ(a, -n0.cur[2]);
            mx::RotY(a, -d);
            mx::RotZ(a, n0.cur[2]);
            mx::RotY(a, n0.cur[1]);
            mx::RotX(a, n0.cur[0]);
            Mat4 b = a;
            mx::RotZ(b, n1.cur[2]);
            mx::RotY(b, n1.cur[1]);
            mx::RotX(b, n1.cur[0]);
            mx::MatrixToAngles(b, &n1.cur[0], &n1.cur[1], &n1.cur[2]);  // 0x004019E0
            EnemyNode& n9 = c.anim.nodes[9];
            mx::RotZ(a, n9.cur[2]);
            mx::RotY(a, n9.cur[1]);
            mx::RotX(a, n9.cur[0]);
            mx::MatrixToAngles(a, &n9.cur[0], &n9.cur[1], &n9.cur[2]);
        } else if (f & 0x10000) {
            n0.cur[0] = r0[0];
            n0.cur[1] = r0[1];
            n0.cur[2] = r0[2];
        }
        if (entryFlags & 0x100000) {
            c.anim.root[0] = root[0];
            c.anim.root[2] = root[2];
        }
        n = k.blendN;
    }
    AnimBlendTo(c, c.anim.motion[0], start, n);
}

// 0x0048C850 -> 0x0048C990(pos block, target, 0x100): turn towards the target by at most 0x100
void World::Impl::CivTurn(CivTask& c) {
    float t[3];
    if (c.c.path < 0) CivPlayerTarget(c, t, false);
    else memcpy(t, c.c.target, 12);
    const float v[3] = { c.pos[0] - t[0], c.pos[1] - t[1], c.pos[2] - t[2] };
    const int a = CivLocalAngle(c, v);
    const int step = 0x100;
    if (step < a) c.rot[1] += step;
    else if (a < -step) c.rot[1] -= step;
    else c.rot[1] += a;
}

// 0x0045CA60: the body point's bounding square overlaps the screen
bool World::Impl::CivOnScreen(const CivTask& c) {
    const float* v = c.bodyView;
    if (0.0f <= v[2]) return false;
    const float r = c.radius;
    const float ex = v[0] <= 0.0f ? -v[0] - r : r - v[0];
    const float ey = v[1] <= 0.0f ? -v[1] - r : r - v[1];
    const float s = focal / v[2];
    const float x1 = s * ex, y1 = s * ey;
    const float x2 = -((focal * v[0]) / v[2]);
    const float y2 = -((focal * v[1]) / v[2]);
    const float kx = F32(exe, 0x004C49CC), kx2 = F32(exe, 0x004D1D10), ky = F32(exe, 0x004C49C8), ky2 = F32(exe, 0x004C4D00);
    if ((x1 < kx || x2 < kx) && (kx2 < x1 || kx2 < x2) && (y1 < ky || y2 < ky)) {
        if (y1 <= ky2 && y2 <= ky2) return false;
        return true;
    }
    return false;
}

// =====================================================================================================================
// Skeleton callbacks
// =====================================================================================================================

// 0x0048D070: collision radius +0x128 towards ctl +0x78; level / object mesh push (0x004057F0) when flag 0x1000000
void World::Impl::CivPostRoot(EnemyTask& e) {
    CivTask& c = static_cast<CivTask&>(e);
    CivCtl& k = c.c;
    if (k.radiusTarget != c.radius128) {
        c.radius128 = c.radius128 + k.radiusStep;
        if (k.radiusStep > 0.0f) {
            if (c.radius128 > k.radiusTarget) c.radius128 = k.radiusTarget;
        } else if (k.radiusStep < 0.0f) {
            if (c.radius128 < k.radiusTarget) c.radius128 = k.radiusTarget;
        }
    }
    // 0x0048D0EA: pushed out of the collision meshes by the sphere +0x12C (last frame's) / +0x128
    if ((k.flags & 0x1000000) && MeshPush(c.sphere, c.radius128)) {
        const float f = cac4c;
        c.pos[0] = cac64[0] * f + c.pos[0];
        c.pos[1] = cac64[1] * f + c.pos[1];
        c.pos[2] = cac64[2] * f + c.pos[2];
    }
}

// 0x0048D1F0: node draw (node 2: face animation 0x0056B950 and look modes; hands: special-mode scale)
void World::Impl::CivDrawNode(EnemyTask& e, int idx, const Mat4& m) {
    CivTask& c = static_cast<CivTask&>(e);
    CivCtl& k = c.c;
    int model = c.anim.nodes[idx].model;
    const Mat4* drawMv = nullptr;  // the head turned by the look (model-view), else the node's own matrix
    if (idx == 2) {
        if (k.lookMode != 0 && CivHeadLook(c)) drawMv = &c.anim.nodes[2].mv;
        if (k.faceMode != 6) {
            const uint32_t tab = 0x0056B950 + static_cast<uint32_t>(k.faceMode) * 8;
            const int count = static_cast<int>(exe.U32(tab + 4));
            if (count > 0) {
                const int i = ((k.faceFrame % count) + count) % count;
                uint8_t b = 0;
                exe.Read(exe.U32(tab) + static_cast<uint32_t>(i), &b, 1);
                model += static_cast<int8_t>(b);
            }
            if (k.faceTimer != 0) {
                if (--k.faceTimer != 0) {
                    ++k.faceFrame;
                } else if (k.faceMode != 2) {
                    k.faceFrame = count - 1;
                } else {
                    k.faceMode = 3;
                    k.faceTimer = static_cast<int32_t>(exe.U32(0x0056B950 + 3 * 8 + 4));
                    k.faceFrame = 0;
                }
            }
        }
    }
    // game type 1 with item 0xC (0x009C88AC): the head scaled (1.5, 1, 1.5) (0x0048D888), the hands / feet (nodes 5 / 8 /
    // 0xC / 0xF) (2, 2, 2) (0x0048D24C) on the matrix stack top before the node's draw
    if (runner->cfg.gameType == 1 && modes && modes->itemFlags[4] != 0) {
        const bool head = idx == 2, limb = idx == 5 || idx == 8 || idx == 0xC || idx == 0xF;
        if (head || limb) {
            Mat4 s = drawMv ? *drawMv : m;
            if (head) mx::Scale(s, 1.5f, 1.0f, 1.5f);
            else mx::Scale(s, 2.0f, 2.0f, 2.0f);
            if (drawMv) Draw(model, s, 8, -1.0f, altModels != 0, idx, true);
            else Draw(model, s, 8, -1.0f, altModels != 0, idx);
            return;
        }
    }
    if (drawMv) Draw(model, *drawMv, 8, -1.0f, altModels != 0, idx, true);
    else Draw(model, m, 8, -1.0f, altModels != 0, idx);
}

// 0x0048D1F0 head look (modes +0x8C: 1 the players, 2 a child enemy's head, 3 point 0x009C6FA8, 4 a point, 6 back to
// the body's pose): the head's angles relative to node 1 approach (0x0048D9B0, 0x100 per frame) the direction to the
// target, clamped to pitch -0x2000..0x1800 / yaw +-0x3800; node 2's matrix (+0x28, model-view) is rebuilt from node
// 1's rotation, its own translation and the angles. Returns true when node 2's matrix was replaced.
bool World::Impl::CivHeadLook(CivTask& c) {
    CivCtl& k = c.c;
    EnemyNode& n1 = c.anim.nodes[1];
    EnemyNode& n2 = c.anim.nodes[2];
    const Mat4& C = cam.inverse;  // 0x009A6040 + context: the camera matrix
    float p[3], head[3], tgt[3];
    mx::GetTranslation(n2.mv, p);
    mx::Transform(C, p, head);
    memcpy(tgt, p, 12);
    switch (k.lookMode) {
    case 1:
        tgt[0] = playerPos[0];
        tgt[1] = playerPos[1] + 15.0f;  // 0x004C4398
        tgt[2] = playerPos[2];
        break;
    case 2: {
        EnemyTask* o = k.lookObj;
        bool live = false;
        if (o)
            for (Task* t : ts.Live())
                if (t->data.get() == o) live = true;
        if (k.children != 0 && (!live || (o->flags34 & 0x4000000)) && !k.childList.empty()) {
            o = k.lookObj = k.childList[0];
            live = true;
        }
        if (!o || !live) {
            Note("civilian head look mode 2: target freed (the original reads its memory)");
            k.lookMode = 6;
        } else if (!(o->flags34 & 1)) {
            k.lookMode = 6;
        } else {
            float q[3];
            mx::GetTranslation(o->anim.nodes[2].mv, q);  // +0x354: its node 2 matrix of its last draw
            mx::Transform(C, q, tgt);
        }
        break;
    }
    case 3:  // the camera's look-at point 0x009C6FA8 (written by 0x00403050 / the rail evaluation)
        memcpy(tgt, runner->camera.lookAt, 12);
        break;
    case 4: {
        uint32_t w[3] = {};
        for (int i = 0; i < 3; ++i)
            if (!mem.Read(k.lookTarget + 4 * i, &w[i])) w[i] = CivWord(k.lookTarget + 4 * i);
        memcpy(tgt, w, 12);
        break;
    }
    case 5: {  // a point in the civilian's own frame (on a vehicle: the vehicle's frame first)
        uint32_t w[3] = {};
        for (int i = 0; i < 3; ++i)
            if (!mem.Read(k.lookTarget + 4 * i, &w[i])) w[i] = CivWord(k.lookTarget + 4 * i);
        float q[3];
        memcpy(q, w, 12);
        Mat4 a = Mat4::Identity();
        if (c.fn == 0x0048B140 && c.vehicle) {
            const EnemyTask& v = *c.vehicle;
            mx::Translate(a, v.pos[0], v.pos[1], v.pos[2]);
            mx::RotX(a, v.rot[0]);
            mx::RotZ(a, v.rot[2]);
            mx::RotY(a, v.rot[1]);
        }
        mx::Translate(a, c.pos[0], c.pos[1], c.pos[2]);
        mx::RotX(a, c.rot[0]);
        mx::RotZ(a, c.rot[2]);
        mx::RotY(a, c.rot[1]);
        mx::Transform(a, q, tgt);
        break;
    }
    case 6: break;
    default:
        Note("civilian head look mode " + std::to_string(k.lookMode) + " (0x0048D1F0) not ported");
        return false;
    }
    // node 2 relative to node 1: inverse(node 1) x node 2 (0x004A8D20, 0x004A92A0), its angles (0x004018E0)
    Mat4 rel = mx::Inverse(n1.mv);
    mx::MulLeft(rel, n2.mv);
    int ax, ay, az;
    {
        const float back[3] = { 0, 0, 1 }, side[3] = { 1, 0, 0 };
        float d[3], v[3], w[3];
        mx::TransformDir(rel, back, d);
        mx::DirToAngles(d[0], d[1], d[2], &ax, &ay);
        mx::TransformDir(rel, side, v);
        Mat4 q = Mat4::Identity();
        mx::RotX(q, -ax);
        mx::RotY(q, -ay);
        mx::TransformDir(q, v, w);
        az = static_cast<int16_t>(static_cast<int>(static_cast<float>(std::atan2(static_cast<double>(w[1]), static_cast<double>(w[0])) * mx::kToAngle)));
    }
    int tp = ax, ty = ay;
    if (k.lookMode != 6) {
        Mat4 b = C;  // node 1 in the world (camera x node 1, 0x004A92A0), translation cleared (0x004A9FD0), inverted
        mx::MulLeft(b, n1.mv);
        b.m[12] = b.m[13] = b.m[14] = 0.0f;
        const Mat4 inv = mx::Inverse(b);
        const float dw[3] = { tgt[0] - head[0], tgt[1] - head[1], tgt[2] - head[2] };
        float l[3];
        mx::Transform(inv, dw, l);
        mx::DirToAngles(l[0], l[1] - 1.5f, l[2], &tp, &ty);  // 0x004C4CB8
    }
    if (static_cast<int16_t>(tp) > 0x1800) tp = 0x1800;
    else if (static_cast<int16_t>(tp) < -0x2000) tp = -0x2000;
    if (static_cast<int16_t>(ty) > 0x3800) ty = 0x3800;
    else if (static_cast<int16_t>(ty) < -0x3800) ty = -0x3800;
    auto approach = [](int32_t& v, int target) {  // 0x0048D9B0(&v, target, 0x100)
        int d = static_cast<uint16_t>(static_cast<int16_t>(target) - static_cast<int16_t>(v));
        if (d == 0) return;
        if (d > 0x8000) d -= 0x10000;
        if (d > 0x100) v += 0x100;
        else if (d < -0x100) v -= 0x100;
        else v += d;
    };
    approach(k.look94, tp - ax);
    approach(k.look98, ty - ay);
    approach(k.look9C, 0);
    if (k.look94 == 0 && k.look98 == 0 && k.look9C == 0) {
        k.lookMode = 0;
        return false;
    }
    Mat4 L = n1.mv;  // 0x004A9ED0: node 1's rotation rows, node 2's translation
    L.m[12] = p[0];
    L.m[13] = p[1];
    L.m[14] = p[2];
    mx::RotY(L, k.look98 + ay);
    mx::RotX(L, k.look94 + ax);
    mx::RotZ(L, k.look9C + az);
    n2.mv = L;  // 0x004A8CA0(node 2 +0x28)
    return true;
}

// 0x004124B0: the part models (ids > 0x11) of a part list in event memory released (0x0041D610: queue type 2 = op
// 0x51, which frees only a model loaded on its own, whoever else still uses it)
void World::Impl::ReleasePartModels(uint32_t parts) {
    for (uint32_t a = parts; a && residency; a += 2) {
        uint32_t wd = 0;
        if (!mem.Read(a & ~3u, &wd)) break;
        const int16_t id = static_cast<int16_t>((a & 2) ? (wd >> 16) : (wd & 0xFFFF));
        if (id < 0) break;
        if (id > 0x11) resQueue.push_back({ 0x51, exe.U32(exe.U32(0x004EC748 + 4 * static_cast<uint32_t>(id)) + 4) });
    }
}

// anim +0x1174 = 0x004124F0 (0x00412620 with the alternative models): part ids >= 0x24 drawn with their node
void World::Impl::DrawParts(EnemyTask& e) {
    for (uint32_t a = e.parts;; a += 2) {
        uint32_t w = 0;
        if (!mem.Read(a & ~3u, &w)) break;
        const int16_t id = static_cast<int16_t>((a & 2) ? (w >> 16) : (w & 0xFFFF));
        if (id < 0) break;
        if (id < 0x24) continue;
        const uint32_t def = exe.U32(0x004EC748 + 4 * id);
        const int node = static_cast<int>(exe.U32(def)) & 15;
        // 0x004A9230(node +0x28): the node's model-view matrix (a civilian's head as turned by the look); game type 1
        // with item 0xC (0x009C88AC): parts of node 2 scaled (1.5, 1, 1.5), of nodes 5 / 8 / 12 / 15 (2, 1, 2)
        Mat4 q = e.anim.nodes[node].mv;
        const int pn = static_cast<int>(exe.U32(def));
        if (runner->cfg.gameType == 1 && modes && modes->itemFlags[4] != 0) {
            if (pn == 2) mx::Scale(q, 1.5f, 1.0f, 1.5f);
            else if (pn == 5 || pn == 8 || pn == 12 || pn == 15) mx::Scale(q, 2.0f, 1.0f, 2.0f);
        }
        Draw(static_cast<int>(exe.U32(def + 4)), q, 8, -1.0f, (e.flags38 & 8) != 0, node, true);
    }
}

}  // namespace game
