// game::World - stage-4 actors (st4evtbl.bin). Each function names the original it ports; status and the verification
// (world_test --stage-start 44107 on build\scene_capture_auto_st4.txt) are in notes/agents/stage4.md.

#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"
#include "game/world_st5.h"

namespace game {

namespace {

// Actor_Destroy 0x00409CC0 for a non-enemy actor object
void DestroyActor(World::Impl& w, ActorTask& a) {
    a.flags34 = (a.flags34 & ~1u) | 0x80018000u;
    for (World::Impl::Target& tg : w.targetCopy)
        if (tg.obj == &a) tg.obj = nullptr;  // 0x00405220
    w.ts.KillCurrent();
}

// Komono kinds 75 (generic ctor 0x00461CF0 -> task 0x004710C0) and 74 (-> 0x00470E20). Outside game type 1 (0x009CA08C) its first run sets
// event flag 0x14 (0x009C7214) and ends; the game type 1 target (model 0xA6B on camera path 0x178) is not ported.
struct Komono75Task : ActorTask {
    int flag = 0x14;
};

void TaskKomono75(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono75Task& k = t.As<Komono75Task>();
    // outside game type 1 (0x009CA08C): event flag 0x14 (kind 75, 0x009C7214) / 0x13 (kind 74, 0x009C7213)
    w.runner->flags[k.flag] = 1;
    DestroyActor(w, k);
}

// Actor type 0x26 state 3 (0x0048E290 -> 0x0048F050; stage-4 scenes 12..14): model 0x185B on camera path 0x173 (start
// for cameras 0xAE / 0xAF / 0xB2, the camera tick clamped to the path end for 0xB0) or 0x174 (0xB1); ends on event flag 0xE0
struct Actor26St3Task : EnemyTask {};

void TaskActor26St3(Task& t) {
    World::Impl& w = *CurrentWorld();
    Actor26St3Task& a = t.As<Actor26St3Task>();
    if (w.runner->flags[0xE0] == 1) {
        w.ts.KillCurrent();  // Task_Kill 0x004A7040
        return;
    }
    auto place = [&](int path, float tm) {
        float p[3];
        int ang[3];
        if (st5::EvalPath(w, path, tm, p, ang)) {  // 0x004042D0
            memcpy(a.pos, p, 12);
            for (int k = 0; k < 3; ++k) a.rot[k] = ang[k];
        }
    };
    const int cam = w.runner->camera.camId;  // 0x009A2D78
    if (cam == 0xAE || cam == 0xAF || cam == 0xB2) {
        place(0x173, 0.0f);
    } else if (cam == 0xB0 || cam == 0xB1) {
        const int path = cam == 0xB0 ? 0x173 : 0x174;
        int tm = static_cast<int>(w.exe.U32(0x00576D38 + static_cast<uint32_t>(path) * 4));
        if (w.runner->camera.tick <= tm) tm = w.runner->camera.tick;  // 0x009A6110
        place(path, static_cast<float>(tm));
    }
    World::Impl::DrawMvScope mvScope(w);  // drawn from the model-view chain
    Mat4 m = w.cam.view;
    mx::Translate(m, a.pos[0], a.pos[1], a.pos[2]);
    mx::RotZ(m, a.rot[2]);
    mx::RotY(m, a.rot[1]);
    mx::RotX(m, a.rot[0]);
    w.Draw(0x185B, m);
}

float F32At(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

// Komono kind 56 (generic ctor 0x00461CF0 case 0x38 -> task 0x0046F090; stage-4 scene 9): a lamp (model 0x1866, turned
// 0x6B00) with a hanging part (0x10D3, scale 1.1, at (4.8, -0.55, -10.5)); a target of radius 1.5 at (pos.x + 4.8,
// pos.y - 0.55, pos.z - 10.5). Shot: event flag 0x0E, the shooter's hit (0x004650F0(flags, 0)), sound 0xF16A9, spark
// 0.75; the part then falls (speed -= 0.02722 per frame). Event flag 5: sound 0x2116A9 and a 60-frame swing (angles from
// the table 0x00595B00 added to the record's x / z angles); game type 1 in scene 9: route 2 (not modelled here).
struct Komono56Task : ActorTask {
    float pos[3] = {};        // +0x40
    int rot[3] = {};          // +0x64
    float target[3] = {};     // +0x19C (record position until the first frame)
    float drop = 0;           // +0x1AC
    float vy = 0;             // +0x1C4
    int recRot[3] = {};       // +0x1CC
    int8_t phase = 0;         // +0x192
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    int16_t life = 0;         // +0x11C
    int swing = 0;            // +0x2A8
};

void TaskKomono56(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono56Task& k = t.As<Komono56Task>();
    const ExeImage& exe = w.exe;
    if (w.runner->variant != k.area) {  // 0x009A2BB0
        if (k.life < ++k.areaCount) {
            DestroyActor(w, k);
            return;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if ((k.flags34 & 8) && !(k.flags34 & 0x40000000)) {
        w.runner->flags[0x0E] = 1;  // 0x009C720E
        const uint32_t f = k.flags34;
        int p = (f & 2) && (f & 4) ? (w.Rand(0x00465106) & 1) : ((f & 2) ? 0 : 1);  // 0x004650F0(flags, 0)
        if (w.GlobalInt(0x009A5C48, 0) == 0) ++w.player[p].hits;
        w.Sound(0xF16A9);
        k.flags34 = f | 0x40000000;
        w.SpawnSpark(k.viewPos[2], (f & 2) == 0 ? 1 : 0, 0.75f, k.target[2]);  // 0x004666B0(obj, p, 0.75)
        k.swing = 0;
    }
    if (k.flags34 & 0x40000000) {
        k.vy = k.vy - F32At(exe, 0x0055CB10);
        k.drop = k.vy + k.drop;
    }
    if (w.runner->flags[0x05] == 1 && k.phase == 0) {
        k.phase = 1;
        w.Sound(0x2116A9);  // (+ 0x00420810(4, 0x14): no-op)
        if (w.runner->cfg.gameType == 1 && w.runner->scene == 9) w.runner->camera.routeBranch = 2;
    }
    if (k.phase == 1 && k.swing < 0x3C) {
        const uint32_t tab = exe.U32(0x005960B4) + static_cast<uint32_t>(k.swing) * 6;
        k.rot[2] = exe.S16(tab + 4) + k.recRot[2];
        k.rot[0] = exe.S16(tab) + k.recRot[0];
        k.rot[1] = exe.S16(tab + 2);
        ++k.swing;
    }
    {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotY(m, 0x6B00);
        mx::RotZ(m, k.rot[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotX(m, k.rot[0]);
        w.Draw(0x1866, m);
    }
    {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::Translate(m, F32At(exe, 0x00569118), k.drop - F32At(exe, 0x005643D0), -10.5f);
        mx::RotY(m, 0x12B00);
        const float s = 1.1f;  // 0x3F8CCCCD
        mx::Scale(m, s, s, s);
        w.Draw(0x10D3, m);
    }
    k.target[0] = k.pos[0] + F32At(exe, 0x00569118);
    k.target[1] = k.pos[1] - F32At(exe, 0x005643D0);
    k.target[2] = k.pos[2] - F32At(exe, 0x0056901C);
    mx::Transform(w.cam.view, k.target, k.viewPos);  // 0x004A8A80 on the view
    w.RegisterTarget(&k);                              // 0x00405160
}

// Komono kind 78 (generic ctor, task 0x00471BA0; stage-4 scene 12): a model load request (0x0041D5D0) of 0x1A60 (game
// type 0; else 0xA6C), then Task_Kill 0x004A7040
void TaskKomono78(Task& t) {
    World::Impl& w = *CurrentWorld();
    (void)t;
    if (w.residency) w.resQueue.push_back({ 0x50, w.runner->cfg.gameType == 0 ? 0x1A60u : 0xA6Cu });
    w.ts.KillCurrent();
}

}  // namespace

// Skeleton 0x17 (actor 0x31) attachments (0x004EDAEC[0x17]): 0 = 0x00419EA0 (the skin path 0x00419E50 like the other
// kinds) drawn by 0x00419B40 translucent with +0x138C (Mdl_SelectAlt 0x00418620 with the alternative models); 1 = the
// tail 0x0041A020 (set up by 0x00419EB0 when 0x009C7A78 == 4): 8 segments {node, model} (0x0052EA38) whose vertices
// follow the previous segment (not modelled), each drawn with its node matrix and the same alpha
void World::Impl::AnimAttach17(EnemyTask& e, int i, uint32_t def, uint32_t tab) {
    EnemyAnim& a = e.anim;
    const bool alt = (e.flags38 & 8) && altModels != 0;
    const float alpha = e.Alpha138C();
    if (i == 0) {
        const int model = static_cast<int>(exe.U32(def));
        const int baseNode = static_cast<int>(exe.U32(tab + 0x10)) & 15;
        WorldDraw* d = Draw(model, a.nodes[baseNode].mv, 8, alpha, alt, baseNode, true);
        if (!d) return;
        d->skinCount = 4;
        for (int k = 0; k < 4; ++k) d->skin[k] = a.nodes[static_cast<int>(exe.U32(tab + 4 * k)) & 15].mv;
        return;
    }
    const uint32_t segs = exe.U32(def + 0x10);
    const int n = static_cast<int>(exe.U32(def + 0x14));
    for (int k = 0; k < n && k < 16; ++k) {
        const int node = static_cast<int>(exe.U32(segs + 0x10 * k)) & 31;
        Draw(static_cast<int>(exe.U32(segs + 0x10 * k + 4)), a.nodes[node].mv, 8, alpha, alt, node, true);
    }
}

bool World::Impl::SpawnStage4(Task& t, SpawnTask& s) {
    (void)t;
    const SpawnRecord r = s.rec;
    switch (r.type) {
    case 0x26:  // 0x0048E290 case 3 (the state function runs at once)
        if (r.state != 3) return false;
        {
            auto d = st5::FromRecord<Actor26St3Task>(r, 0x0048F050);
            t.data = std::move(d);
            t.fn = TaskActor26St3;
            TaskActor26St3(t);
        }
        return true;
    case 0x19:  // the stage-4 boss (0x004917E0, world_st4_boss.cpp)
        return SpawnBoss4(t, s);
    case 0x41:  // komono (0x00461CD0: ctor table 0x00593580[kind])
        if ((r.ParamS8(1) == 75 || r.ParamS8(1) == 74) && runner->cfg.gameType != 1) {  // (game type 1: world_modes_items.cpp)
            // kind 74: 0x00470E20 (game type 1: a breakable target, 3 hits, item 0x00467B90; not ported)
            const bool k74 = r.ParamS8(1) == 74;
            Komono75Task* k = NewTask<Komono75Task>(TaskKomono75, k74 ? 0x00470E20 : 0x004710C0);
            k->flags34 = 0x80000001u;
            k->flag = k74 ? 0x13 : 0x14;
            ts.KillCurrent();
            return true;
        }
        if (r.ParamS8(1) == 56) {  // generic ctor 0x00461CF0 case 0x38 (the task first runs next frame)
            Komono56Task* k = NewTask<Komono56Task>(TaskKomono56, 0x0046F090);
            memcpy(k->pos, r.pos, 12);
            memcpy(k->target, r.pos, 12);
            memcpy(k->recRot, r.rot, 12);
            k->rot[1] = r.rot[1];
            k->life = static_cast<int16_t>(r.state);
            k->area = static_cast<int8_t>(runner->variant);
            k->flags34 = 0x80000001u;
            k->radius = 1.5f;
            ts.KillCurrent();
            return true;
        }
        if (r.ParamS8(1) == 78) {
            Komono75Task* k = NewTask<Komono75Task>(TaskKomono78, 0x00471BA0);
            k->flags34 = 0x80000001u;
            ts.KillCurrent();
            return true;
        }
        return false;
    default:
        return false;
    }
}

}  // namespace game
