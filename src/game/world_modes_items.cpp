// game::World - original mode (game type 1) pickups: the stage items (generic komono kinds 70 / 71 / 72, task
// 0x004675A0), the items dropped by shot props (0x00466B40 -> task 0x00466BE0), the item a komono-69 target leaves
// (0x00470500 -> 0x00467B90), the pickup caption (0x00475E40 -> task 0x00475D00) and the item pick 0x004629C0.
// notes/agents/modes.md section 7; verified against build\scene_capture_modes_orig.txt.

#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_modes.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}
double F64(const ExeImage& exe, uint32_t va) {
    double d = 0;
    exe.Read(va, &d, 8);
    return d;
}
float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}
int8_t S8(const ExeImage& exe, uint32_t va) {
    int8_t b = 0;
    exe.Read(va, &b, 1);
    return b;
}

// the 0x378-byte komono block of an item
struct ItemTask : ActorTask {
    int8_t state = 0;              // +0x192
    int8_t slot = 0;               // +0x194
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    float pos[3] = {};             // +0x19C
    float baseY = 0;               // +0x1AC (kind 71: the bobbing centre)
    int rot[3] = {};               // +0x1CC / +0x1D0 / +0x1D4
    int sway[2] = {};              // +0x1D8 / +0x1E0 (kind 71)
    int phaseStep = 0, phase = 0;  // +0x1DC / +0x1E8 (kind 71)
    int16_t model = 0, model2 = 0; // +0x28C / +0x28E
    int16_t item = -1;             // +0x290
    int counter = 0;               // +0x2A0 frames since taken
    int digits = 0;                // +0x2A4 score digit models (0x116A / 0x119C: the shooter)
    float vy = 0;                  // +0x2C0 (kind 71: the bob amplitude)
    float scale = 1.0f;            // +0x2C4
    int16_t life = 0;              // +0x11C
};

struct CaptionTask : WTask {  // 0x00475D00
    int counter = 0;           // +0x2A0
    int16_t sprite = 0;        // +0x28C
};

// 0x007DCD14: an item was just taken / placed (blocks the stage items' hits while set)
uint8_t& Taken(World::Impl& w) { return w.modes->itemTaken; }

void Destroy(World::Impl& w, ActorTask& a) {  // Actor_Destroy 0x00409CC0
    a.flags34 = (a.flags34 & ~1u) | 0x80018000u;
    for (World::Impl::Target& tg : w.targetCopy)
        if (tg.obj == &a) tg.obj = nullptr;
    w.ts.KillCurrent();
}

// 0x00466640: the item's lifetime in areas (+0x11C areas), and stage 2's flag 0x77
void AreaLife(World::Impl& w, ItemTask& k) {
    if (w.cfg.eventMode == 1 && w.runner->flags[0x77] != 0) Destroy(w, k);
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) Destroy(w, k);
        k.area = static_cast<int8_t>(w.runner->variant);
    }
}

// 0x004650F0(flags, 0): the shooter (rand when both gun bits), a hit
int Shooter(World::Impl& w, uint32_t flags) {
    int p;
    if ((flags & 2) && (flags & 4)) p = w.Rand(0x00465106) & 1;
    else p = (flags & 2) ? 0 : 1;
    if (w.GlobalInt(0x009A5C48, 0) == 0) ++w.player[p].hits;
    return p;
}

void TaskCaption(Task& t) {  // 0x00475D00: the item's name for 150 frames (fading from 135)
    World::Impl& w = *CurrentWorld();
    CaptionTask& c = t.As<CaptionTask>();
    ModesState& m = *w.modes;
    ++c.counter;
    if (c.counter < 0x97 && (w.cfg.eventMode != 5 || m.captions < 2)) {
        SpriteDef d;
        d.id = c.sprite;
        d.x = 320.0f;
        d.y = 370.0f;
        d.alpha = c.counter < 0x87 ? 1.0f : static_cast<float>(0x96 - c.counter) * F32(w.exe, 0x00569198);
        d.flags = 0xB;
        // (list priority 0x004A79F0(0xF): renderer)
        w.hudSprites.push_back(d);
        d.id = 0x5E0;
        d.flags = 9;
        w.hudSprites.push_back(d);
        return;
    }
    --m.captions;
    w.ts.KillCurrent();
}

// taken: the inventory +1, the caption, sound, the score digits of the shooter
void Take(World::Impl& w, ItemTask& k, uint32_t randSite) {  // randSite: 0x0046773D (0x004675A0) / 0x00466CB5 (0x00466BE0)
    ModesState& m = *w.modes;
    const int player = Shooter(w, k.flags34);
    k.flags34 |= 0x40000000;
    if (!RandomizerCollect(w, player, k.item) && 0 <= k.item && k.item < 33 && m.items[k.item] < 99) ++m.items[k.item];
    CaptionTask* c = w.NewTask<CaptionTask>(TaskCaption, 0x00475D00);  // 0x00475E40
    c->counter = 0;
    c->sprite = w.exe.S16(0x005957E0 + static_cast<uint32_t>(k.item) * 12);
    ++m.captions;
    w.Sound(0x3616A9);
    k.counter = 1;
    const uint32_t g = k.flags34 & 2;
    if (g == 0 || (k.flags34 & 4) == 0) k.digits = g == 0 ? 0x119C : 0x116A;
    else k.digits = (w.Rand(randSite) & 1) * 0x32 + 0x116A;
}

// the item model(s), fading after 25 frames, and the score digits after the hit
void ItemDraw(World::Impl& w, ItemTask& k, bool rotZYX, float model2Dz) {
    const ExeImage& exe = w.exe;
    const float alpha = k.counter < 0x19 ? -1.0f : F32(exe, 0x004C4380) - static_cast<float>(k.counter) * F32(exe, 0x004E3100);
    if (k.model != -1) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        if (rotZYX) {
            mx::RotZ(m, k.rot[2]);
            mx::RotY(m, k.rot[1]);
            mx::RotX(m, k.rot[0]);
        } else {
            mx::RotY(m, k.rot[1]);
            mx::RotZ(m, k.rot[2]);
        }
        mx::Scale(m, k.scale, k.scale, k.scale);
        w.Draw(k.model, m, 8, alpha);
    }
    if (k.model2 != -1) {
        Mat4 m = w.cam.view;
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::ClearRotation(m);  // 0x004A9F70
        if (model2Dz != 0.0f) mx::Translate(m, 0.0f, 0.0f, model2Dz);
        mx::Scale(m, k.scale, k.scale, k.scale);
        w.Draw(k.model2, m, 8, alpha, false, -1, true);
    }
    if (0 < k.counter) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotY(m, k.rot[1]);
        w.Draw(k.digits - 1 + k.counter, m);
    }
}

void Register(World::Impl& w, ItemTask& k) {
    const float p[3] = { k.pos[0], static_cast<float>(k.pos[1] + F64(w.exe, 0x0055D7D0)), k.pos[2] };
    mx::Transform(w.cam.view, p, k.viewPos);
    w.RegisterTarget(&k);  // 0x00405160
}

// yaw towards the camera: atan2(x - cam x, z - cam z) in engine units + 0x8000
int FaceCamera(World::Impl& w, const float pos[3]) {
    const CameraContext& c = w.runner->camera.ctx[w.runner->camera.activeCtx & 3];
    const float dx = static_cast<float>(pos[0] - c.pos[0]), dz = static_cast<float>(pos[2] - c.pos[2]);
    const float a = static_cast<float>(std::atan2(static_cast<double>(dx), static_cast<double>(dz)));  // fpatan (24-bit)
    return static_cast<int16_t>(static_cast<int>(static_cast<float>(static_cast<double>(a) * F64(w.exe, 0x004C4378)))) + 0x8000;
}

// 0x004675A0: a stage item (generic kinds 70 / 71 / 72 and the komono-69 drop)
void TaskStageItem(Task& t) {
    World::Impl& w = *CurrentWorld();
    ItemTask& k = t.As<ItemTask>();
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    if (w.runner->cfg.gameType != 1) Destroy(w, k);
    AreaLife(w, k);
    if (k.item == -1) Destroy(w, k);
    const int stage = w.cfg.eventMode, scene = w.runner->scene;
    if (stage == 2) {
        if (k.slot == 5 && scene == 9) Destroy(w, k);
    } else if (stage == 4 && scene == 3 && w.runner->flags[0x0B] != 0) {
        Destroy(w, k);
    }
    if (0 < k.counter && 0x31 < ++k.counter) Destroy(w, k);
    bool hits = true;
    if (stage == 2 && scene == 4) {
        if (w.runner->flags[0x13] != 0) w.runner->camera.routeBranch = stage;  // 0x009C88A4
        hits = Taken(w) == 0;
    } else if (stage == 2 && scene == 10 && 0x3C < w.runner->camera.tick) {
        Taken(w) = 0;
    } else {
        hits = Taken(w) == 0;
    }
    if (hits && !(k.flags34 & 0x40000000) && (k.flags34 & 8) && (stage != 1 || scene != 1)) Take(w, k, 0x0046773D);
    const uint32_t f = k.flags34;
    k.flags34 = f & ~8u;
    if (!(f & 0x200000)) {
        if (k.model == 0x109F) k.rot[1] = FaceCamera(w, k.pos);
        else k.rot[1] += 0x400;
    } else {  // kind 71: bobbing and swaying
        const int ph = k.phase;
        const uint32_t np = static_cast<uint32_t>(ph + k.phaseStep);
        k.phase = static_cast<int>(np);
        const float s = static_cast<float>(std::sin(static_cast<double>(static_cast<float>(static_cast<double>(ph) * F64(exe, 0x004C4370)))));
        k.pos[1] = s * k.vy + k.baseY;
        if ((np & 0xFFFF) == 0) {
            int r = w.Rand(0x00467867) & 1;
            k.sway[0] = (r * -0x100 + 0x90) - w.Rand(0x00467882) % 0x21;
            r = w.Rand(0x00467897) & 1;
            k.sway[1] = (r * -0x100 + 0x90) - w.Rand(0x004678B2) % 0x21;
            k.vy = static_cast<float>(w.Rand(0x004678C7) % 0x33) * F32(exe, 0x004D5464) + F32(exe, 0x004C4C58);
        }
        const int a = k.sway[0] - (k.rot[0] + k.sway[0]) / 0x30;
        k.sway[0] = a;
        const int b = k.sway[1] - (k.sway[1] + k.rot[2]) / 0x30;
        k.sway[1] = b;
        k.rot[2] = b + k.rot[2];
        k.rot[0] = a + k.rot[0];
        k.rot[1] += 0x400;
    }
    (void)m;
    ItemDraw(w, k, true, 1.5f);
    Register(w, k);
}

// 0x00466BE0: an item dropped by a shot prop (falls, bounces once, lies on the floor 0x009C8E58)
void TaskDropItem(Task& t) {
    World::Impl& w = *CurrentWorld();
    ItemTask& k = t.As<ItemTask>();
    const ExeImage& exe = w.exe;
    AreaLife(w, k);
    if (0 < k.counter && 0x31 < ++k.counter) Destroy(w, k);
    if (k.item == -1) Destroy(w, k);
    if (!(k.flags34 & 0x40000000) && (k.flags34 & 8)) Take(w, k, 0x00466CB5);
    const float floor = w.ground;  // 0x009C8E58
    if (k.state == 0) {
        k.pos[1] = k.pos[1] + k.vy;
        k.vy = k.vy - F32(exe, 0x0055D2CC);
        if (k.pos[1] < floor + F32(exe, 0x004C4380)) {
            k.vy = Bits(0x3F333333);
            k.state = 1;
            k.pos[1] = floor + F32(exe, 0x004C4380);
        }
    } else if (k.state == 1) {
        k.pos[0] = k.pos[0] + F32(exe, 0x004C4C88);
        k.pos[2] = k.pos[2] + F32(exe, 0x0055CB50);
        k.pos[1] = k.pos[1] + k.vy;
        k.vy = k.vy - F32(exe, 0x0055D2CC);
        if (k.pos[1] <= floor) {
            k.pos[1] = floor;
            k.rot[0] = k.rot[2] = 0;
            k.state = 2;
            if (k.item == 0x10 || k.item == 0x11) k.rot[1] += 0x8000;
        }
    }
    k.rot[1] += 0x400;
    ItemDraw(w, k, false, 0.0f);
    {  // the shadow 0x10D0 on the floor
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], floor + F32(exe, 0x004D1D24), k.pos[2]);
        mx::Scale(m, 3.0f, 1.0f, 3.0f);
        w.Draw(0x10D0, m);
    }
    Register(w, k);
}

// komono 69 (0x00470500 in game type 1): a hanging target (models 0x13F8 / 0x13F7); shot: it falls (and with event
// flag 0x23 on route 1 it leaves an item 0x00467B90)
struct Komono69Task : ItemTask {
    int dropped = 0;  // +0x2A0
    float fall = 0;   // +0x1C4
};

void TaskKomono69(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono69Task& k = t.As<Komono69Task>();
    const ExeImage& exe = w.exe;
    if (w.runner->cfg.gameType != 1) Destroy(w, k);
    const int scene = w.runner->scene;
    if (scene == 8 || scene == 3) Destroy(w, k);
    CameraDirector& c = w.runner->camera;
    if (w.runner->flags[0x23] == 1 && c.routeBranch == 1 && 0 < k.state) {
        c.routeBranch = 2;
        k.dropped = 1;
        k.life = 2;
        const float keep[3] = { k.pos[0], k.pos[1], k.pos[2] };
        k.pos[0] = Bits(0xC30CB333);
        k.pos[1] = 3.0f;
        k.pos[2] = Bits(0xC3A4599A);
        SpawnItemAt(w, k.pos, k.area, k.areaCount, k.life, k.dropped);  // 0x00467B90
        Taken(w) = 0;
        memcpy(k.pos, keep, 12);
    }
    if (k.state == 0) {
        if (k.flags34 & 8) {
            const int p = Shooter(w, k.flags34);
            (void)p;
            w.Sound(0x1D16A9);
            w.SpawnSmoke(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, k.pos[2]);  // 0x00465860
            k.state = 1;
            k.fall = Bits(0xBE4CCCCD);
        }
    } else if (k.state == 1) {
        const float f = static_cast<float>(k.fall - F64(exe, 0x00569140));
        k.fall = f;
        k.rot[0] -= 0x100;
        k.rot[2] -= 0x100;
        k.pos[1] = f + k.pos[1];
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    w.Draw(0x13F8, m);
    Mat4 m2 = Mat4::Identity();
    mx::Translate(m2, Bits(0xC30A5687), Bits(0x41CC0553), Bits(0xC3A17FDF));
    w.Draw(0x13F7, m2);
    const float p[3] = { k.pos[0], k.pos[1] + F32(exe, 0x004C4CB8), k.pos[2] };
    mx::Transform(w.cam.view, p, k.viewPos);
    w.RegisterTarget(&k);
}

// 0x00470750 (generic komono kind 72, game type 1; stage-2 scene 16): an item thrown up when camera 0x4E reaches tick
// 0x276 with event flag 0x12 (else removed): +0x1C4 = 1.2 falling by float 0x00569148 per frame (spinning 0x400),
// removed once below its start height while falling faster than double 0x00564770; shot: taken (inventory, caption,
// sound, the shooter's digits), then only drawn
struct Item72Task : ItemTask {
    float v1C4 = 0;  // +0x1C4
};

void TaskItem72(Task& t) {
    World::Impl& w = *CurrentWorld();
    Item72Task& k = t.As<Item72Task>();
    const ExeImage& exe = w.exe;
    if (w.runner->cfg.gameType != 1) {
        Destroy(w, k);
        return;
    }
    if (0 < k.counter && 0x31 < ++k.counter) {
        Destroy(w, k);
        return;
    }
    if (k.item == -1) {
        Destroy(w, k);
        return;
    }
    if (k.state == 0) {
        if (w.runner->camera.camId == 0x4E && w.runner->camera.tick == 0x276) {  // 0x009A2D78 / 0x009A6110[ctx]
            if (w.runner->flags[0x12] == 0) {
                Destroy(w, k);
                return;
            }
            k.state = 1;
            k.v1C4 = Bits(0x3F99999A);
            k.baseY = k.pos[1];
        }
    } else if (k.state == 1) {
        const uint32_t f = k.flags34;
        if (!(f & 0x40000000) && (f & 8)) {
            k.state = 2;
            const int player = Shooter(w, f);  // 0x004650F0(flags, 0)
            k.flags34 |= 0x40000000;
            ItemInventoryCaption(w, k.item, player, true);  // already randomized when spawned
            w.Sound(0x3616A9);
            k.counter = 1;
            const uint32_t g = k.flags34 & 2;
            if (g == 0 || !(k.flags34 & 4)) k.digits = g == 0 ? 0x119C : 0x116A;
            else k.digits = (w.Rand(0x00470856) & 1) * 0x32 + 0x116A;
        }
        // x87 at 53 bits: the difference and the sum are not rounded until stored
        const double d = static_cast<double>(k.v1C4) - static_cast<double>(F32(exe, 0x00569148));
        k.rot[1] += 0x400;
        k.v1C4 = static_cast<float>(d);
        const float y = static_cast<float>(d + static_cast<double>(k.pos[1]));
        k.pos[1] = y;
        if (d < F64(exe, 0x00564770) && y < k.baseY) {
            Destroy(w, k);
            return;
        }
        const float p[3] = { k.pos[0], static_cast<float>(static_cast<double>(y) + F64(exe, 0x0055D7D0)), k.pos[2] };
        mx::Transform(w.cam.view, p, k.viewPos);
        w.RegisterTarget(&k);  // 0x00405160
    }
    if (0 < k.state) {
        const float alpha = k.counter < 0x19 ? -1.0f : F32(exe, 0x004C4380) - static_cast<float>(k.counter) * F32(exe, 0x004E3100);
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotZ(m, k.rot[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotX(m, k.rot[0]);
        mx::Scale(m, k.scale, k.scale, k.scale);
        w.Draw(k.model, m, 8, alpha);
        if (k.model != -1) {  // (the original tests +0x28C, then draws +0x28E)
            Mat4 v = w.cam.view;
            mx::Translate(v, k.pos[0], k.pos[1], k.pos[2]);
            mx::ClearRotation(v);  // 0x004A9F70
            mx::Translate(v, 0.0f, 0.0f, 1.5f);
            mx::Scale(v, k.scale, k.scale, k.scale);
            if (k.model2 != -1) w.Draw(k.model2, v, 8, alpha, false, -1, true);
        }
        if (0 < k.counter) {
            Mat4 q = Mat4::Identity();
            mx::Translate(q, k.pos[0], k.pos[1], k.pos[2]);
            mx::RotY(q, k.rot[1]);
            w.Draw(k.digits - 1 + k.counter, q);
        }
    }
}
}  // namespace

// 0x004629C0(obj, slot): the item of a slot by the stage's table 0x00595AA0[stage] (8-byte rows: 4 items, 3
// thresholds, the modulus) and one rand; models / scale from 0x005957D8 + item * 12
void ItemPick(World::Impl& w, int slot, int16_t* item, int16_t* model, int16_t* model2, float* scale, uint32_t randSite) {
    const ExeImage& exe = w.exe;
    const uint32_t row = w.exe.U32(0x00595AA0 + static_cast<uint32_t>(w.cfg.eventMode) * 4) + static_cast<uint32_t>(slot) * 8;
    const int r = RandomizerActive(w) ? 0 : w.Rand(randSite);  // original RNG untouched outside the port mode
    const int mod = S8(exe, row + 7);
    int i = 0;
    while (i < 3 && S8(exe, row + 4 + static_cast<uint32_t>(i)) <= (mod ? r % mod : 0)) ++i;
    const int it = RandomizerActive(w) ? RandomizerPickItem(w) : S8(exe, row + static_cast<uint32_t>(i));
    *item = static_cast<int16_t>(it);
    if (it == -1) {
        *model = *model2 = 0;
        *scale = 1.0f;
        return;
    }
    const uint32_t d = 0x005957D8 + static_cast<uint32_t>(it) * 12;
    *model = exe.S16(d);
    *model2 = exe.S16(d + 2);
    *scale = F32(exe, d + 4);
    if (RandomizerActive(w)) {
        for (int mdl : {*model, *model2}) if (mdl > 0) w.ProcessRequest({0x50, static_cast<uint32_t>(mdl)});
        const int sprite = exe.S16(d + 8);
        if (sprite > 0) w.ProcessRequest({0x54, exe.U32(0x0057A5BC + sprite * 4)});
    }
}

// 0x00467B90(obj): a stage item (task 0x004675A0) at the object's position (+0x19C), slot = its +0x2A0, area / area
// count / life = its +0x196 / +0x197 / +0x11C, facing the camera
void SpawnItemAt(World::Impl& w, const float pos[3], int area, int areaCount, int life, int slot) {
    ItemTask* k = w.NewTask<ItemTask>(TaskStageItem, 0x004675A0);
    k->flags34 = 0x80000001u;
    k->slot = static_cast<int8_t>(slot);
    memcpy(k->pos, pos, 12);
    k->rot[1] = FaceCamera(w, k->pos);
    k->areaCount = static_cast<int8_t>(areaCount);
    k->area = static_cast<int8_t>(area);
    k->life = static_cast<int16_t>(life);
    k->radius = 3.0f;
    ItemPick(w, k->slot, &k->item, &k->model, &k->model2, &k->scale);
    w.modes->captions = 0;  // 0x007DCD04
}

// generic komono kinds 70 / 71 / 72 in game type 1 (0x00461CF0 cases 0x46..0x48 -> 0x004675A0)
void SpawnStageItem(World::Impl& w, const SpawnRecord& r) {
    const int kind = r.ParamS8(1);
    ItemTask* k = kind == 0x48 ? w.NewTask<Item72Task>(TaskItem72, 0x00470750) : w.NewTask<ItemTask>(TaskStageItem, 0x004675A0);
    memcpy(k->pos, r.pos, 12);
    memcpy(k->rot, r.rot, 12);
    k->model = static_cast<int16_t>(r.state);
    k->area = static_cast<int8_t>(w.runner->variant);
    k->areaCount = 0;
    k->flags34 = 0x80000001u;
    k->life = static_cast<int16_t>(r.state);
    k->radius = 3.0f;
    k->slot = r.ParamS8(0);
    ItemPick(w, k->slot, &k->item, &k->model, &k->model2, &k->scale);
    if (kind == 0x46 && w.cfg.eventMode == 4 && k->slot == 1) k->radius = k->radius + k->radius;
    if (kind == 0x47) {
        k->baseY = r.pos[1];
        int a = w.Rand(0x004627AD) & 1;
        int b = w.Rand(0x004627CB);
        k->phaseStep = 0x200;
        k->sway[0] = (a * -0xA0 + 0x60) - b % 0x21;
        a = w.Rand(0x004627EA) & 1;
        b = w.Rand(0x00462808);
        k->vy = 1.5f;
        k->sway[1] = (a * -0xA0 + 0x60) - b % 0x21;
        k->rot[0] = k->rot[2] = 0;
        k->flags34 |= 0x200000;
    }
    Taken(w) = 0;
}

// 0x00466B40(y, slot, src): an item falling from a shot prop (task 0x00466BE0)
void SpawnDropItem(World::Impl& w, float y, int slot, const float pos[3], int area, int areaCount, int life) {
    ItemTask* k = w.NewTask<ItemTask>(TaskDropItem, 0x00466BE0);
    k->flags34 = 0x80000001u;
    k->life = static_cast<int16_t>(life + 2);
    k->pos[0] = pos[0];
    k->pos[1] = y;
    k->pos[2] = static_cast<float>(pos[2] + F64(w.exe, 0x004ECB70));
    k->areaCount = static_cast<int8_t>(areaCount);
    k->area = static_cast<int8_t>(area);
    k->radius = 3.0f;
    ItemPick(w, slot, &k->item, &k->model, &k->model2, &k->scale);
}

// 0x004699C0(y, slot, link): the item the komono-24 chain drops in game type 1 (task 0x00466BE0, as 0x00466B40 but the
// link's own life / x (+0x194) / z (+0x19C) / area counters, the item picked inline with the rand at 0x00469A5D)
void SpawnChainItem(World::Impl& w, float y, int slot, const float x0z2[3], int area, int areaCount, int life) {
    ItemTask* k = w.NewTask<ItemTask>(TaskDropItem, 0x00466BE0);
    k->flags34 = 0x80000001u;
    k->life = static_cast<int16_t>(life);
    k->pos[0] = x0z2[0];
    k->pos[1] = y;
    k->pos[2] = x0z2[2];
    k->areaCount = static_cast<int8_t>(areaCount);
    k->area = static_cast<int8_t>(area);
    k->radius = 3.0f;
    ItemPick(w, slot, &k->item, &k->model, &k->model2, &k->scale, 0x00469A5D);
}

// game over in game type 1 (0x00460960 at 0xC6): the inventory into the save block, Game_Save
void ModesSaveItems(World::Impl& w) {
    if (w.modes) w.modes->Store(w);  // (Store copies the inventory 0x009C90C0 -> 0x009C9F3D with the rest)
}

// 0x00428400 (the final boss down) in game type 1: save flag 0x009C9F5E | 1, item 0x18 + 1 (copied to the save
// 0x009C9F55), 0x009C9F5F = 1
void ItemsFinalBoss(World::Impl& w) {
    if (RandomizerActive(w)) return;
    if (!w.modes) return;
    ModesState& m = *w.modes;
    m.save.SetU8(SaveData::kFlags, static_cast<uint8_t>(m.save.U8(SaveData::kFlags) | 1));
    if (m.items[0x18] < 99) {
        ++m.items[0x18];
        m.save.SetU8(SaveData::kItems + 0x18, static_cast<uint8_t>(m.items[0x18]));
    }
    m.save.SetU8(0x009C9F5F, 1);
}

bool ItemFlag(World::Impl& w, int i) { return w.modes && i >= 0 && i < 5 && w.modes->itemFlags[i] != 0; }

int ItemWeapon(World::Impl& w, int p) {
    if (!w.modes || p < 0 || p > 1) return 0;
    return w.modes->pitems[p].weapon;
}

int ItemMagazine(World::Impl& w, int p) {
    if (!w.modes || p < 0 || p > 1) return 6;
    return static_cast<int8_t>(w.modes->pitems[p].b8[0]);
}

int ItemLivesMax(World::Impl& w, int p) {
    if (!w.modes || p < 0 || p > 1) return 5;
    return w.modes->pitems[p].b5;
}

void SetItemTaken(World::Impl& w, int v) {
    if (w.modes) w.modes->itemTaken = static_cast<uint8_t>(v);
}

// 0x0048DEE0: holds the item's texture file (scr_item_*) for 150 frames, then releases it (0x0041D710 = request type
// 7, op 0x55: the caption's sprite is no longer drawn, texFiles) and ends
struct ItemHoldTask : WTask {
    int counter = 0x96;  // +0x34
    int model = 0;       // +0x38: texture file 0x0056B0F4[kind * 4]
};

void TaskItemHold(Task& t) {
    World::Impl& w = *CurrentWorld();
    ItemHoldTask& k = t.As<ItemHoldTask>();
    if (k.counter-- == 0) {
        // (applied at once rather than queued: the port's queue timing model, draws2.md, is left as it was)
        w.ProcessRequest({ 0x55, static_cast<uint32_t>(k.model) });
        w.ts.KillCurrent();
    }
}

// 0x0048DD60's hand-over (a civilian's carried item, game type 1): inventory 0x009C90C0[kind] + 1 (below 99), the
// caption 0x00475E40(0x0056B0F6[kind * 4]) and the residency task 0x0048DEE0 (texture file 0x0056B0F4[kind * 4])
void CivItemGiven(World::Impl& w, int kind) {
    if (RandomizerActive(w)) kind = RandomizerPickItem(w);
    if (!w.modes || kind < 0 || kind >= 33) {
        w.Note("civilian item 0x0048DD60 kind " + std::to_string(kind) + " outside game type 1 data");
        return;
    }
    ModesState& m = *w.modes;
    if (!RandomizerCollect(w, -1, kind) && m.items[kind] < 99) ++m.items[kind];
    CaptionTask* c = w.NewTask<CaptionTask>(TaskCaption, 0x00475D00);  // 0x00475E40
    c->counter = 0;
    c->sprite = w.exe.S16(0x0056B0F6 + static_cast<uint32_t>(kind) * 4);
    ++m.captions;
    ItemHoldTask* h = w.NewTask<ItemHoldTask>(TaskItemHold, 0x0048DEE0);
    h->model = w.exe.S16(0x0056B0F4 + static_cast<uint32_t>(kind) * 4);
}

// game type 1: an item taken from a prop (komono 43 0x0046CEA0): the inventory 0x009C90C0[item] + 1 below 99 and the
// caption 0x00475E40 (sprite 0x005957E0[item * 12])
void ItemInventoryCaption(World::Impl& w, int item, int player, bool randomized) {
    if (RandomizerActive(w) && !randomized) item = RandomizerPickItem(w);
    if (!w.modes || item < 0 || item >= 33) {
        w.Note("item " + std::to_string(item) + " taken outside game type 1 data");
        return;
    }
    ModesState& m = *w.modes;
    if (!RandomizerCollect(w, player, item) && m.items[item] < 99) ++m.items[item];
    CaptionTask* c = w.NewTask<CaptionTask>(TaskCaption, 0x00475D00);
    c->counter = 0;
    c->sprite = w.exe.S16(0x005957E0 + static_cast<uint32_t>(item) * 12);
    if (RandomizerActive(w)) w.ProcessRequest({0x54, w.exe.U32(0x0057A5BC + c->sprite * 4)});
    ++m.captions;
}
// 0x00461C70(item): one player: the player in game 0x009C7000 holds it in slot 0 or 1; two players: P1's or P2's
// slot 0 (signed bytes: -1 matches an empty slot)
bool PlayerHasItem(World::Impl& w, int item) {
    if (!w.modes) return false;
    const auto& pi = w.modes->pitems;
    if (w.players == 1) {
        const auto& p = pi[w.playersInGame & 1];
        return p.slot[0] == item || p.slot[1] == item;
    }
    if (w.players == 2) return pi[0].slot[0] == item || pi[1].slot[0] == item;
    return false;
}

void Prop17Created(World::Impl& w) {
    if (w.modes) w.modes->prop17Shot = 0;
}

// 0x00474F30 in game type 1 (after the kill / stage checks): unshot, a hit while the player holds one of the record's
// items (P byte 0x20 = -1: any; or once any of them was shot, 0x009A26EC) starts the swing (sound 0x2116A9 the first
// time); shot: the swing table 0x005960B4[+0x194] {x, y, z} x 60 frames, and when its event flag (+0x2A0) is set in
// stage 1 scene 4 / stage 2 scenes 1, 3, 12 / stage 5 scene 4 the route branch 0x009C88A4 = 2; stage 5 scene 4 then
// blinks it away after 0x78 frames; stage 3 scenes 2 / 4: an item at a fixed point (0x00467B90, slot 2 / 4) and the
// event flag 0x15 0x4C frames after flag 0x18
bool Prop17GameType1(World::Impl& w, ActorTask& a, Prop17Type1& s, float pos[3], int rot[3]) {
    if (!w.modes) {
        w.Note("type 0x44 state 17 game type 1 without the modes state (0x00474F30)");
        return true;
    }
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    auto flag = [&](int i) -> int {
        if (i < 0 || i > 0xFF) {
            w.Note("type 0x44 state 17 reads event flag " + std::to_string(i) + " (0x00474F30)");
            return 0;
        }
        return w.runner->flags[i];
    };
    if (s.shot == 0) {
        bool hit = false;
        if (a.flags34 & 8) {
            hit = s.items[0] == -1 || PlayerHasItem(w, s.items[0]) || PlayerHasItem(w, s.items[1]) ||
                  PlayerHasItem(w, s.items[2]) || PlayerHasItem(w, s.items[3]);
        }
        if (hit || m.prop17Shot != 0) {
            s.shot = 1;
            if (m.prop17Shot == 0) {
                w.Sound(0x2116A9);  // + 0x00420810(3, 0x14): an empty function
                m.prop17Shot = 1;
            }
        }
        return true;
    }
    if (s.shot != 1) return true;
    if (s.t2A8 < 0x3C) {
        const uint32_t e = exe.U32(0x005960B4 + 4 * static_cast<uint32_t>(static_cast<int>(s.kind))) + static_cast<uint32_t>(s.t2A8) * 6;
        const int ax = exe.S16(e);
        int ay = exe.S16(e + 2);
        rot[2] = exe.S16(e + 4) + 0;  // + +0x1D4 (never set: 0)
        if (s.c1DC < 1) {
            rot[0] = 0 - ax;          // +0x1CC (0) - x
            ay = -ay;
        } else {
            rot[0] = 0 + ax;
        }
        rot[1] = ay;
        ++s.t2A8;
    }
    const int em = w.cfg.eventMode, sc = w.runner->scene;
    bool toStage5 = true;  // 0x004750B1
    if (s.flag2A0 >= 0 && flag(s.flag2A0) == 1) {
        bool set = false;
        if (em == 0) {
            if (sc != 4) return true;
            set = true;
        } else if (em == 1) {
            if (sc != 1 && sc != 3 && sc != 0xC) return true;
            set = true;
        } else if (em == 4) {
            if (sc != 4) return true;
            set = true;
        } else {
            toStage5 = false;  // 0x00475107
        }
        if (set) {
            w.runner->camera.routeBranch = 2;  // 0x009C88A4
            s.flag2A0 = -1;
        }
    }
    if (toStage5 && em == 4) {
        if (sc != 4 || flag(s.flag2A0) == 0) return true;
        if (0x78 < s.t2AC++) return false;  // Actor_Destroy
    }
    if (em != 2) return true;  // 0x00475107
    auto spawnItem = [&](int slot, uint32_t x, uint32_t y, uint32_t z) {
        s.flag2A0 = slot;
        s.life = 2;
        float p[3];
        memcpy(&p[0], &x, 4);
        memcpy(&p[1], &y, 4);
        memcpy(&p[2], &z, 4);
        SpawnItemAt(w, p, 0, 0, s.life, s.flag2A0);  // 0x00467B90 (area / area count +0x196 / +0x197: 0)
        m.itemTaken = 0;                                // 0x007DCD14
        s.flag2A0 = -1;
        s.t2B0 = 1;
    };
    if (sc == 2) {
        if (s.t2B0 == 0) {
            spawnItem(2, 0xC3C94CCD, 0xC1800000, 0xC5468800);
        } else {
            if (w.runner->flags[0x18] == 0) return true;
            if (0x4C < s.t2B0++) w.runner->flags[0x15] = 1;
        }
    }
    if (sc == 4 && s.t2B0 == 0) spawnItem(4, 0xC479F333, 0xC1BE6666, 0xC5484B33);
    (void)pos;
    return true;
}

namespace {

// Generic komono kinds 73 / 75 / 76 (0x00461CF0 -> 0x00470B70 / 0x004710C0 / 0x00471330): game-type-1 targets. Outside
// game type 1 kinds 75 / 76 remove themselves on their first run (75 sets event flag 0x14 first); kind 73 is removed in
// the attract demo (game mode 5) and otherwise stands at the start of camera path 0x179 (model 0x1871) as a target.
struct KomonoTask : ItemTask {
    int kind = 0;
    int c2A4 = 0;            // +0x2A4 (kind 75: area changes)
    int t2A8 = 0;            // +0x2A8 (kind 76: swing frame)
    int r1E4 = 0, r1E8 = 0;  // +0x1E4 / +0x1E8 (kind 76 slot 1: the second part's angles)
    int8_t hits199 = 3;      // +0x199 (kind 74: shots left)
    float v1C4 = 0;          // +0x1C4 (kind 74: vertical speed)
};

// 0x004710C0 in game type 1 (stage 4 scene 2): a target hanging at camera path 0x178 (model 0xA6B, drawn at the path's
// point +0x2C0, hit sphere at the record's position); shot: score / spark 1.5, sounds 0xE16A9 / 0x391BA9, the item
// 0x00467B90 (slot +0x2A0 = 1) at (142, -59.8, -888.7), then it runs down the path (+1 per frame) until float
// 0x0056914C: state 2, 0x007DCD14 = 1, event flag 0x14, sound 0x3A1BA9. Two area changes unshot: flag 0x14 too.
void Komono75(World::Impl& w, KomonoTask& k) {
    const ExeImage& exe = w.exe;
    if (w.runner->variant != k.area) {
        if (++k.c2A4 == 2 && k.state == 0) w.runner->flags[0x14] = 1;  // 0x009C7214
        if (k.life < ++k.areaCount) {
            Destroy(w, k);
            return;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if ((k.flags34 & 8) && k.state == 0) {
        const int p = Shooter(w, k.flags34);  // 0x004650F0(flags, 0)
        (void)p;
        w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 1.5f, k.pos[2]);  // 0x004666B0(obj, p, 1.5)
        w.Sound(0xE16A9);
        w.Sound(0x391BA9);
        k.counter = 1;  // +0x2A0
        k.state = 1;
        const float at[3] = { Bits(0x430E0000), Bits(0xC26F3333), Bits(0xC45E2CCD) };
        SpawnItemAt(w, at, k.area, k.areaCount, k.life, k.counter);  // 0x00467B90
        Taken(w) = 0;
    }
    if (k.state == 1) {
        const float v = k.vy + F32(exe, 0x004C4380);
        k.vy = v;
        if (F32(exe, 0x0056914C) <= v) {
            k.state = 2;
            Taken(w) = 1;
            w.runner->flags[0x14] = 1;
            w.Sound(0x3A1BA9);
        }
    }
    float pp[3] = {}, tg[3] = {};
    int roll = 0;
    w.cameraBank.Evaluate(0x178, k.vy, pp, tg, &roll, false);  // 0x004042D0(0x178, +0x2C0)
    Mat4 m = Mat4::Identity();
    mx::Translate(m, pp[0], pp[1], pp[2]);
    mx::RotZ(m, static_cast<int>(tg[2]));
    mx::RotY(m, static_cast<int>(tg[1]));
    mx::RotX(m, static_cast<int>(tg[0]));
    w.Draw(0xA6B, m);
    mx::Transform(w.cam.view, k.pos, k.viewPos);
    w.RegisterTarget(&k);
}

// 0x00471330 in game type 1 (stage 4 scenes 5 / 14): a hanging board (slot +0x194 = P byte 0: 0 model 0xA6D, 1 the
// two-part 0xA67 / 0xA68), with a hook model 0x10D3 until shot; shot (score, spark 0.75, sound 0xF16A9) it counts in
// scene 5 for slot 1, in scene 14 while a player holds item 0, 2 or 0xB: route branch 2 (0x009C88A4), sound 0x2116A9,
// then the swing table 0x005960B4[0] for 60 frames
void Komono76(World::Impl& w, KomonoTask& k) {
    const ExeImage& exe = w.exe;
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) {
            Destroy(w, k);
            return;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    const int scene = w.runner->scene;
    if ((k.flags34 & 8) && k.state == 0) {
        k.flags34 &= ~8u;
        Shooter(w, k.flags34);
        w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 0.75f, k.pos[2]);  // 0x004666B0(obj, p, 0.75)
        w.Sound(0xF16A9);
        if ((k.slot == 1 && scene == 5) ||
            (scene == 0xE && (PlayerHasItem(w, 0) || PlayerHasItem(w, 2) || PlayerHasItem(w, 0xB)))) {
            k.state = 1;
            w.runner->camera.routeBranch = 2;  // 0x009C88A4
            w.Sound(0x2116A9);
            k.flags34 |= 0x40000000;
        }
    }
    k.flags34 &= ~6u;
    if (k.state == 1 && k.t2A8 < 0x3C) {
        const uint32_t e = exe.U32(0x005960B4) + static_cast<uint32_t>(k.t2A8) * 6;
        k.rot[2] += exe.S16(e + 4);
        k.rot[0] -= exe.S16(e);
        k.rot[1] = -exe.S16(e + 2);
        k.r1E4 += exe.S16(e + 4);
        k.r1E8 = exe.S16(e + 2);
        ++k.t2A8;
    }
    if (k.slot == 0) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
        mx::RotZ(m, k.rot[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotX(m, k.rot[0]);
        w.Draw(0xA6D, m);
        if (k.state == 0) {
            mx::Translate(m, -2.5f, -30.0f, -17.5f);
            mx::RotY(m, 0x4000);
            mx::Scale(m, Bits(0x3F99999A), Bits(0x3F99999A), Bits(0x3F99999A));
            w.Draw(0x10D3, m);
        }
        const float p[3] = { static_cast<float>(k.pos[0] - F64(exe, 0x00569160)), static_cast<float>(k.pos[1] - F64(exe, 0x00569158)),
                             static_cast<float>(k.pos[2] - F64(exe, 0x00569150)) };
        mx::Transform(w.cam.view, p, k.viewPos);
    } else if (k.slot == 1) {
        Mat4 a = Mat4::Identity();
        mx::Translate(a, Bits(0xC3C582D1), Bits(0x410D6C8B), Bits(0xC4022093));
        mx::RotY(a, -0x2168);
        mx::RotZ(a, k.rot[2]);
        mx::RotY(a, k.r1E8);
        mx::RotX(a, k.r1E4);
        Mat4 h = a;
        mx::Translate(h, 12.5f, 23.5f, 0.5f);
        float hit[3];
        mx::GetTranslation(h, hit);
        w.Draw(0xA67, a);
        if (k.state == 0) w.Draw(0x10D3, h);
        Mat4 b = Mat4::Identity();
        mx::Translate(b, Bits(0xC3BCC560), Bits(0x410D6C8B), Bits(0xC3FAD7CF));
        mx::RotY(b, -0x2168);
        mx::RotZ(b, k.rot[2]);
        mx::RotY(b, k.rot[1]);
        mx::RotX(b, k.rot[0]);
        w.Draw(0xA68, b);
        mx::Transform(w.cam.view, hit, k.viewPos);
    }
    w.RegisterTarget(&k);  // 0x00405160
}

// 0x00470E20 (generic komono kind 74; stage 4 scene 4): outside game type 1 event flag 0x13 and removed; a target
// (model 0xA64) that drops the item 0x00467B90 (slot +0x2A0 = 2) at (-286.6, -17.3, -302.5) on its third hit (+0x199),
// then falls (+0x1C4 from -0.1 by float 0x00569098, pitching up to 0x4000); in scene 5 with event flag 0x15: flag
// 0x13 at once when unshot, else after 0x32 frames
void Komono74(World::Impl& w, KomonoTask& k) {
    const ExeImage& exe = w.exe;
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) {
            Destroy(w, k);
            return;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    if (w.runner->cfg.gameType != 1) {
        w.runner->flags[0x13] = 1;  // 0x009C7213
        Destroy(w, k);
        return;
    }
    const int scene = w.runner->scene;
    if (k.state == 1) {
        if (scene == 5 && w.runner->flags[0x15] != 0 && 0x32 < ++k.c2A4) w.runner->flags[0x13] = 1;
    } else if (k.state == 0 && scene == 5 && w.runner->flags[0x15] != 0) {
        w.runner->flags[0x13] = 1;
    }
    if ((k.flags34 & 8) && k.state == 0) {
        k.flags34 &= ~8u;
        Shooter(w, k.flags34);
        w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 1.5f, k.pos[2]);  // 0x004666B0(obj, p, 1.5)
        w.Sound(0xE16A9);
        if (--k.hits199 < 1) {
            k.counter = 2;  // +0x2A0: the item slot
            const float at[3] = { Bits(0xC38F4CCD), Bits(0xC18A6666), Bits(0xC3974000) };
            SpawnItemAt(w, at, k.area, k.areaCount, k.life, k.counter);  // 0x00467B90
            Taken(w) = 0;
            k.state = 1;
            k.v1C4 = Bits(0xBDCCCCCD);
        }
    }
    k.flags34 &= ~6u;
    if (k.state == 1) {
        const double d = static_cast<double>(k.v1C4) - static_cast<double>(F32(exe, 0x00569098));
        k.v1C4 = static_cast<float>(d);
        k.pos[1] = static_cast<float>(d + static_cast<double>(k.pos[1]));
        if (k.rot[0] < 0x4000) k.rot[0] += 0x200;
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    w.Draw(0xA64, m);
    const double y = (static_cast<double>(k.radius) * static_cast<double>(F32(exe, 0x004C43AC)) + static_cast<double>(k.pos[1])) -
                     static_cast<double>(F32(exe, 0x004E30F0));
    const float p[3] = { k.pos[0], static_cast<float>(y), k.pos[2] };
    mx::Transform(w.cam.view, p, k.viewPos);
    w.RegisterTarget(&k);
}
void TaskKomonoModes(Task& t) {
    World::Impl& w = *CurrentWorld();
    KomonoTask& k = t.As<KomonoTask>();
    const int gt = w.runner->cfg.gameType;
    if (k.kind == 74) {
        Komono74(w, k);
        return;
    }
    if (k.kind == 76 || k.kind == 75) {
        if (gt != 1) {
            if (k.kind == 75) w.runner->flags[0x14] = 1;  // 0x009C7214
            Destroy(w, k);
            return;
        }
        if (k.kind == 75) Komono75(w, k);
        else Komono76(w, k);
        return;
    }
    // kind 73 (0x00470B70)
    if (w.gameMode == 5) {
        Destroy(w, k);
        return;
    }
    if (w.runner->variant != k.area) {
        if (k.life < ++k.areaCount) {
            w.ts.KillCurrent();  // Task_Kill 0x004A7040
            return;
        }
        k.area = static_cast<int8_t>(w.runner->variant);
    }
    // game type 1, stage-4 scene 7 with event flag 0x12: a shot scores (spark 1.5, sound 0xE16A9); while a player holds
    // item 5, 6 or 0xC the first one sends it down camera path 0x179 (+0x2C0 from 0, +1 per frame: sound 0x371BA9 at
    // 0x46, state 2 at 0x69) and sets the route branch 2 (sound 0x381BA9)
    if (gt == 1 && w.runner->scene == 7 && w.runner->flags[0x12] != 0 && (k.flags34 & 8)) {
        Shooter(w, k.flags34);  // 0x004650F0
        k.flags34 &= ~8u;
        w.Sound(0xE16A9);
        w.SpawnSpark(k.viewPos[2], (k.flags34 & 2) == 0 ? 1 : 0, 1.5f, k.pos[2]);  // 0x004666B0(obj, p, 1.5)
        if (k.state == 0 && (PlayerHasItem(w, 5) || PlayerHasItem(w, 6) || PlayerHasItem(w, 0xC))) {
            k.state = 1;
            k.vy = 0.0f;
            w.runner->camera.routeBranch = 2;  // 0x009C88A4
            w.Sound(0x381BA9);
        }
    }
    k.flags34 &= ~6u;
    float tg[3] = {};
    int roll = 0;
    if (w.cameraBank.Evaluate(0x179, k.vy, k.pos, tg, &roll, false)) {  // 0x004042D0(0x179, +0x2C0)
        for (int i = 0; i < 3; ++i) k.rot[i] = static_cast<int>(tg[i]);
    }
    if (k.state == 1) {
        k.vy = k.vy + F32(w.exe, 0x004C4380);
        const int f = static_cast<int>(k.vy);  // _ftol
        if (f == 0x46) w.Sound(0x371BA9);       // + 0x00420810(5, 0x14): empty
        if (f == 0x69) k.state = 2;
    }
    Mat4 m = Mat4::Identity();
    mx::Translate(m, k.pos[0], k.pos[1], k.pos[2]);  // (+0x1C8 = 0 added to z)
    mx::RotZ(m, k.rot[2]);
    mx::RotY(m, k.rot[1]);
    mx::RotX(m, k.rot[0]);
    w.Draw(0x1871, m, 8, -1.0f, w.altModels != 0);
    const float p[3] = { k.pos[0], k.pos[1] + F32(w.exe, 0x004C43A0), k.pos[2] };
    mx::Transform(w.cam.view, p, k.viewPos);  // 0x004A8A80 on the camera
    w.RegisterTarget(&k);                     // 0x00405160
}

}  // namespace

// 0x00461CF0 for the generic komono kinds 73 / 75 / 76 (false: another kind)
bool SpawnKomonoModes(World::Impl& w, const SpawnRecord& r, int kind) {
    if (kind == 68) {  // 0x00463E50 (world_st1b.cpp)
        SpawnCreatureKomono68(w, r);
        return true;
    }
    if (kind == 67) {  // 0x00461CF0 case 0x43 (world_modes_train.cpp)
        SpawnKomono67(w, r);
        return true;
    }
    if (kind != 73 && kind != 74 && kind != 75 && kind != 76) return false;
    static const uint32_t kFn[4] = { 0x00470B70, 0x00470E20, 0x004710C0, 0x00471330 };
    KomonoTask* k = w.NewTask<KomonoTask>(TaskKomonoModes, kFn[kind - 73]);
    k->kind = kind;
    memcpy(k->pos, r.pos, 12);
    memcpy(k->rot, r.rot, 12);
    k->model = static_cast<int16_t>(r.state);
    k->life = static_cast<int16_t>(r.state);
    k->area = static_cast<int8_t>(w.runner->variant);
    k->flags34 = 0x80000001u;
    k->radius = kind == 73 ? 12.0f : kind == 74 ? 9.0f : 3.0f;  // +0x124 (cases 0x49 / 0x4A / 0x4B / 0x4C)
    if (kind == 74) k->hits199 = 3;  // +0x199 (case 0x4A)
    if (kind == 76) k->slot = r.ParamS8(0);  // +0x194 (case 0x4C)
    return true;
}

// komono 69 in game type 1
void SpawnKomono69(World::Impl& w, const SpawnRecord& r) {
    Komono69Task* k = w.NewTask<Komono69Task>(TaskKomono69, 0x00470500);
    memcpy(k->pos, r.pos, 12);
    memcpy(k->rot, r.rot, 12);
    k->model = static_cast<int16_t>(r.state);
    k->area = static_cast<int8_t>(w.runner->variant);
    k->life = static_cast<int16_t>(r.state);
    k->flags34 = 0x80000001u;
    k->radius = 4.0f;
}

}  // namespace game
