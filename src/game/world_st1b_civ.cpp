// game::World - civilian attachments (script ops 0x13 / 0x14 / 0x15, per frame 0x0048CD10): items carried on a node
// (records of 31 words in exe data: {node, model, kind, rot x, rot y, rot z, function, 6 x {x, y, z, scale} by the
// civilian's attachment slot +0x82}); the record's function runs every frame and hands the item over when the script
// block's flags (+0x00) contain the entry's mask (life 0x0048DCC0, other items 0x0048DD60). See notes/agents/stage1b.md.

#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

// Task 0x0048DFE0 (0x0048DF10(player)): the "P1" / "P2" label (0x1256 / 0x1257) of a life item, in view space,
// 120 frames, fading in its last 6
struct LifeLabelTask : WTask {
    float pos[3] = {};  // +0x40
    int16_t model = 0;  // +0x1F4
    float scale = 0;    // c+0x00
    int counter = 0;    // c+0x04
};

void TaskLifeLabel(Task& t) {
    World::Impl& w = *CurrentWorld();
    LifeLabelTask& k = t.As<LifeLabelTask>();
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::Scale(m, k.scale, k.scale, k.scale);
    const float n = static_cast<float>(k.counter);
    if (n < F32(w.exe, 0x0055E1C8)) w.Draw(k.model, m, 8, n * F32(w.exe, 0x00564544), false, -1, true);  // 0x004185A0
    else w.Draw(k.model, m, 8, -1.0f, false, -1, true);
    if (--k.counter == 0) w.ts.KillCurrent();
}

// 0x0048DF10(player)
void SpawnLifeLabel(World::Impl& w, int p) {
    LifeLabelTask* k = w.NewTask<LifeLabelTask>(TaskLifeLabel, 0x0048DFE0);
    k->pos[2] = -1.0f;
    k->pos[1] = F32(w.exe, 0x0056B17C) / w.focal;
    k->scale = (k->pos[2] * F32(w.exe, 0x0056B178)) / w.focal;
    k->counter = 0x78;
    if (w.route == 1) k->pos[0] = 0.0f;
    else k->pos[0] = (k->pos[2] * F32(w.exe, p == 0 ? 0x00565E00 : 0x00565E04)) / w.focal;
    k->model = p == 0 ? 0x1256 : 0x1257;
}

// 0x00415630: a life below the maximum 0x009A2440 (5), else 300 points; true = life given
bool LifeUpItem(World::Impl& w, int p) {
    if (p < 0 || p > 1) return false;
    const int cap = w.runner->cfg.gameType == 1 ? ItemLivesMax(w, p) : 5;  // game type 1: items +5 (0x009A2245)
    if (w.player[p].lives >= cap) {
        w.AddScore(p, 300);  // 0x004156C0
        return false;
    }
    ++w.player[p].lives;
    return true;
}

// The extra model of an attachment kind (the switch of 0x0048CD10 / 0x0048CBF0 / 0x0048DD60), 0: none
int AttachExtraModel(int kind) {
    switch (kind) {
    case 3: return 0x10A5;
    case 4: return 0x10A7;
    case 5: return 0x10A9;
    case 6: return 0x10A3;
    case 7: return 0x107F;
    case 8: return 0x1081;
    case 9: return 0x1083;
    case 10: return 0x107D;
    case 0xE: return 0x1098;
    case 0xF: return 0x1099;
    case 0x10: return 0x108C;
    case 0x12: return 0x108B;
    default: return 0;
    }
}

}  // namespace

// ops 0x13 (record, mask) / 0x14 (mask: record chosen by 0x15) / 0x15 (weighted record list {weight, record}, -1 ends)
void World::Impl::CivAttachOp(CivTask& c, int op, uint32_t a1, uint32_t a2) {
    CivCtl& k = c.c;
    if (op == 0x13) {  // 0x0048CAE0
        k.attachList.push_back({ a1, a2 });
        ++k.attachCount;
    } else if (op == 0x14) {  // 0x0048CB60
        k.attachList.push_back({ k.attachChosen, a1 });
        ++k.attachCount;
    } else if (op == 0x15) {  // 0x0048CBF0
        int total = 0;
        for (uint32_t q = a1; static_cast<int32_t>(exe.U32(q)) != -1; q += 8) total += static_cast<int32_t>(exe.U32(q));
        const int r = total != 0 ? Rand(0x0048CC12) % total : 0;
        uint32_t q = a1 + 4;
        for (int v = r - static_cast<int32_t>(exe.U32(a1)); -1 < v; q += 8) v -= static_cast<int32_t>(exe.U32(q + 4));
        k.attachChosen = exe.U32(q);
        // 0x0041D5D0: load requests of the kind's extra model and the item's model
        if (residency) {
            if (const int extra = AttachExtraModel(static_cast<int32_t>(exe.U32(k.attachChosen + 8))))
                resQueue.push_back({ 0x50, static_cast<uint32_t>(extra) });
            resQueue.push_back({ 0x50, exe.U32(k.attachChosen + 4) });
        }
        // 0x0041D6D0: its texture file 0x0056B0F4[kind * 4]
        // (scr_item_*: texFiles; applied at once, the port's queue timing model is left as it was)
        const uint32_t kind = exe.U32(k.attachChosen + 8);
        if (kind < 33) ProcessRequest({ 0x54, static_cast<uint32_t>(exe.S16(0x0056B0F4 + kind * 4)) });
    }
}

// 0x0048CD10: draw the attachments on their nodes and run their functions; collected ones are removed
void World::Impl::CivAttachments(CivTask& c) {
    CivCtl& k = c.c;
    if (k.attachCount == 0) return;
    const int slot = k.attachSlot;
    bool removed = false;
    for (size_t i = 0; i < k.attachList.size(); ++i) {
        const uint32_t r = k.attachList[i].first;
        const int node = static_cast<int32_t>(exe.U32(r));
        const int model = static_cast<int32_t>(exe.U32(r + 4)), kind = static_cast<int32_t>(exe.U32(r + 8));
        const uint32_t s = r + 0x1C + static_cast<uint32_t>(slot) * 16;
        const float sc = F32(exe, s + 12);
        Mat4 m = c.anim.nodes[node & 31].world;
        mx::RotX(m, static_cast<int32_t>(exe.U32(r + 0xC)));
        mx::RotZ(m, static_cast<int32_t>(exe.U32(r + 0x14)));
        mx::RotY(m, static_cast<int32_t>(exe.U32(r + 0x10)));
        mx::Translate(m, F32(exe, s), F32(exe, s + 4), F32(exe, s + 8));
        if (sc != 1.0f) mx::Scale(m, sc, sc, sc);
        Draw(model, m);
        int extra = 0;
        float lift = 0;
        switch (kind) {
        case 3: Draw(0x10A5, m); break;
        case 4: Draw(0x10A7, m); break;
        case 5: Draw(0x10A9, m); break;
        case 6: Draw(0x10A3, m); break;
        case 7: extra = 0x107F; lift = 6.5f; break;
        case 8: extra = 0x1081; lift = 6.5f; break;
        case 9: extra = 0x1083; lift = 6.5f; break;
        case 10: extra = 0x107D; lift = 6.5f; break;
        case 0xE: extra = 0x1098; lift = 2.0f; break;
        case 0xF: extra = 0x1099; lift = 2.0f; break;
        case 0x10: extra = 0x108C; lift = 2.0f; break;
        case 0x12: extra = 0x108B; lift = 2.0f; break;
        default: break;
        }
        if (extra != 0) {  // camera-facing in view space (0x004A9E10 on the model-view stack)
            Mat4 q = m;
            mx::Translate(q, 0.0f, lift, 0.0f);
            float tw[3], tv[3];
            mx::GetTranslation(q, tw);
            mx::Transform(cam.view, tw, tv);
            const float f = sc * c.anim.scale;
            Mat4 v = Mat4::Identity();
            mx::Translate(v, tv[0], tv[1], tv[2]);
            mx::Scale(v, f, f, f);
            mx::Translate(v, 0.0f, -lift, 0.0f);
            Draw(extra, v, 8, -1.0f, false, -1, true);
        }
        const uint32_t fn = exe.U32(r + 0x18);
        const uint32_t mask = k.attachList[i].second;
        if (fn == 0x0048DCC0 || fn == 0x0048DD60) {
            if (mask & k.flags) {
                k.flags = (~mask & k.flags) | 0x400000;
                if (players == 1) {
                    if (k.scorePlayer != playersInGame) k.scorePlayer = static_cast<int16_t>(playersInGame);
                } else if (k.scorePlayer == -1) {
                    k.scorePlayer = static_cast<int16_t>(Rand(fn == 0x0048DCC0 ? 0x0048DD1B : 0x0048DDBB) & 1);
                }
                if (fn == 0x0048DCC0) {
                    if (LifeUpItem(*this, k.scorePlayer)) SpawnLifeLabel(*this, k.scorePlayer);
                } else {
                    if (residency) {  // 0x0041D610: the extra model and the item's model released
                        if (const int extra = AttachExtraModel(kind)) resQueue.push_back({ 0x51, static_cast<uint32_t>(extra) });
                        resQueue.push_back({ 0x51, static_cast<uint32_t>(model) });
                    }
                    CivItemGiven(*this, kind);  // 0x0048DD60 (world_modes_items.cpp)
                }
            }
        } else if (fn != 0x0041EBB0) {
            Note("civilian attachment function " + std::to_string(fn) + " (0x0048CD10) not ported");
        }
        if (k.flags & 0x400000) {
            removed = true;
            k.flags &= ~0x400000u;
            k.attachList[i].first = 0;
            --k.attachCount;
        }
    }
    if (removed) {
        std::vector<std::pair<uint32_t, uint32_t>> keep;
        for (const auto& e : k.attachList)
            if (e.first != 0) keep.push_back(e);
        k.attachList = std::move(keep);
    }
}

}  // namespace game
