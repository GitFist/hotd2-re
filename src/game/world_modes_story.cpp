// game::World - actors of the story sequences (game modes 10 / 11): the mode-11 title card (actor 0x60 -> 0x00434DA0)
// and the 2D caption actor type 0x50 (0x004997E0 -> 0x004998A0 / 0x00499A80, sprite-effect tasks 0x00499C60).
// notes/agents/modes.md section 8; verified with frontend_test --resync on build\scene_capture_attract_loop.txt.

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

// Actor 0x60 in game mode 11 (0x004342E0 -> 0x00434DA0, same block): 200 frames of a 5 x 15 tile picture (Spr_DrawSimple
// 0x0041C6D0, depth 2); the tile set cycles with 0x009A32A0 % 10 (table 0x0055DD64, frames 60..72 of the count 0x0055DD70);
// at the end event flag 0xF8, 0x0041D710(0x192), 0x009A5900 &= ~0x20, Task_Kill
struct StoryTitle : WTask {
    int16_t timer = 0;  // +0x11C
    int16_t step = 0;   // +0x1312
};

void TaskStoryTitle(Task& tk) {
    World::Impl& w = *CurrentWorld();
    StoryTitle& t = tk.As<StoryTitle>();
    if (t.step == 0) {
        t.timer = 200;
        t.step = 1;
    }
    if (t.step == 1) {
        const uint32_t pick = static_cast<uint32_t>(static_cast<int32_t>(w.seedCounter) % 10) & ~1u;  // byte offset
        const uint32_t table = (t.timer < 0x49 && 0x3B < t.timer) ? 0x0055DD70 : 0x0055DD64;
        const int base = w.exe.S16(table + pick);
        for (int row = 0, k = 0; row < 0x1E0; row += 0x20, k += 5)
            for (int col = 0, c = 0; col < 0x280; col += 0x80, ++c) {
                SpriteDef d;
                d.id = base + k + c;
                d.x = static_cast<float>(col);
                d.y = static_cast<float>(row);
                d.depth = 2.0f;
                d.source = 0x00434DA0;
                w.hudSprites.push_back(d);
            }
    }
    if (--t.timer < 1) {
        w.runner->flags[0xF8] = 1;  // 0x009C72F8
        // 0x0041D710(0x192): the picture's texture released (resources are not modelled here)
        w.flags5900 &= ~0x20u;
        w.ts.KillCurrent();
    }
}

// Actor type 0x50 (0x004997E0): a 0x18-byte block {kind = params dword 0, base sprite id, count, state, 1.0, 1.0}; kinds
// 0x0C..0x0F (bases 0x20 / 0x24 / 0x28 / 0x2C) run 0x004998A0, kind 0x10 (base 0x1C) 0x00499A80. Each spawns sprite
// effects (0x00499C60) at counts 0 / 0x78 / 0x7D / 0x82 / 0x87 / 0x8C / 0x91 around the record position (+0x40 / +0x44)
// and ends after count 0xA0.
struct Caption : WTask {
    int kind = 0;     // block +0
    int base = 0;     // block +4
    int count = 0;    // block +8
    int state = 0;    // block +0xC (record state, +0x11C)
    float x = 0, y = 0;
};

void TaskCaption(Task& tk) {
    World::Impl& w = *CurrentWorld();
    Caption& c = tk.As<Caption>();
    const ExeImage& exe = w.exe;
    const float x = c.x, y = c.y, a = F32(exe, 0x004C4C8C), b = F32(exe, 0x004C43A4), d = F32(exe, 0x0055CCD4);
    const int id = c.base;
    if (c.fn == 0x004998A0) {
        switch (c.count) {
        case 0: w.SpawnSpriteFx(id, x, y, 1.0f, 1.0f, 0.0f, 0x82, 0, 2); break;
        case 0x78: w.SpawnSpriteFx(id + 1, x + a, y + a, 1.0f, 1.0f, 1.0f, 0x14, 0, 0); break;
        case 0x7D: w.SpawnSpriteFx(id + 2, x - b, y + b, 2.0f, 1.5f, 1.0f, 0x14, 0, 0); break;
        case 0x82: w.SpawnSpriteFx(id + 3, x, y - b, 1.0f, 1.0f, 1.0f, 0x14, 0, 0); break;
        case 0x87: w.SpawnSpriteFx(id + 1, x - d, y - d, 1.0f, 1.0f, 1.0f, 0x14, 0, 0); break;
        case 0x8C: w.SpawnSpriteFx(id + 3, x + d, y + d, 1.0f, 1.0f, 1.0f, 0x14, 0, 0); break;
        case 0x91: w.SpawnSpriteFx(id, x, y, 1.0f, 1.0f, 1.0f, 0x14, 1, 1); break;
        default: break;
        }
    } else {  // 0x00499A80
        switch (c.count) {
        case 0: w.SpawnSpriteFx(id + 3, x, y, 1.0f, 1.0f, 0.0f, 0x82, 0, 2); break;
        case 0x78: w.SpawnSpriteFx(id, x + a, y + a, 1.0f, 1.0f, 1.0f, 0x14, 0, 0); break;
        case 0x7D: w.SpawnSpriteFx(id + 1, x - b, y + b, 1.0f, 1.0f, 1.0f, 0x14, 0, 0); break;
        case 0x82: w.SpawnSpriteFx(id + 2, x, y - a, 1.0f, 1.0f, 1.0f, 0x14, 0, 0); break;
        case 0x87: w.SpawnSpriteFx(id, x + b, y + a, 1.0f, 1.0f, 1.0f, 0x14, 0, 0); break;
        case 0x8C: w.SpawnSpriteFx(id + 1, x - a, y - b, 1.0f, 1.0f, 1.0f, 0x14, 0, 0); break;
        case 0x91: w.SpawnSpriteFx(id + 3, x, y, 1.0f, 1.0f, 1.0f, 0x14, 1, 1); break;
        default: break;
        }
    }
    if (0xA0 < ++c.count) w.ts.KillCurrent();
}

// Actor 0x26 state 0 (0x0048E290 -> 0x0048E350, story mode 10): the players' car parked at (-64.02, 0, 137.48),
// its lights and parts, and a sign facing the camera (models 0x1433.. / 0x1AAB.. by the frame count, 0xB67 + count % 8);
// removed by event flag 0xE0
struct ParkedCar : WTask {
    float pos[3] = {};
    int rot[3] = {};
    int16_t step = 0;  // +0x1312
    int count = 0;     // +0x1320
};

float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

void TaskParkedCar(Task& tk) {
    World::Impl& w = *CurrentWorld();
    ParkedCar& c = tk.As<ParkedCar>();
    if (w.runner->flags[0xE0] == 1) {
        w.ts.KillCurrent();
        return;
    }
    if (c.step == 0) {
        c.pos[0] = Bits(0xC2801838);
        c.pos[1] = 0.0f;
        c.pos[2] = Bits(0x43097B02);
        c.rot[0] = 0xCB0;
        c.rot[1] = 0x4E81;
        c.rot[2] = 0xD92;
        ++c.step;
    }
    Mat4 base = Mat4::Identity();
    mx::Translate(base, c.pos[0], c.pos[1], c.pos[2]);
    mx::RotY(base, c.rot[1]);
    w.Draw(0x1579, base);
    w.Draw(0x157E, base);
    Mat4 m = base;
    mx::Translate(m, 0.0f, Bits(0x412BD2F2), Bits(0x41366F00));
    mx::RotX(m, -0x1C00);
    mx::RotZ(m, 0x4000);
    mx::RotX(m, 0x1C00);
    w.Draw(0x8CC, m);
    m = base;
    mx::Translate(m, Bits(0xC1211F8A), Bits(0x40C5AC71), Bits(0x40DB35A8));
    w.Draw(0x157C, m, 0xC);  // 0x004A79F0(0xC)
    w.Draw(0x157D, m, 0xC);
    m = base;
    mx::Translate(m, Bits(0x41213B64), Bits(0x40C6C083), Bits(0x40DA9100));
    w.Draw(0x157A, m);
    w.Draw(0x157B, m);
    m = base;
    mx::Translate(m, 0.0f, Bits(0x4076617C), Bits(0x4181CA8C));
    w.Draw(0x157F, m);
    m = base;
    mx::Translate(m, 0.0f, Bits(0x4076617C), Bits(0xC14AC56D));
    w.Draw(0x1580, m);
    ++c.count;
    int pitch = 0, yaw = 0;
    mx::DirToAngles(w.cam.pos[0] - c.pos[0], 0.0f, w.cam.pos[2] - c.pos[2], &pitch, &yaw);  // 0x004016B0
    m = Mat4::Identity();
    mx::Translate(m, c.pos[0], c.pos[1] + F32(w.exe, 0x0055D2B4), c.pos[2]);
    mx::RotY(m, yaw);
    w.Draw(c.count < 0x28 ? c.count % 0x28 + 0x1433 : c.count % 0x28 + 0x1AAB, m);
    mx::Translate(m, 0.0f, 0.0f, 5.0f);
    mx::Scale(m, 5.0f, 5.0f, 5.0f);
    w.Draw(c.count % 8 + 0xB67, m);
}

}  // namespace

// The story-mode actors (false: another actor / mode)
bool SpawnStory(World::Impl& w, Task& t, SpawnTask& s) {
    const SpawnRecord& r = s.rec;
    if (r.type == 0x60 && w.gameMode == 11 && w.runner->cfg.gameType != 3) {
        w.flags5900 |= 0x20;  // 0x004342E0
        auto d = std::make_unique<StoryTitle>();
        d->fn = 0x00434DA0;
        t.data = std::move(d);
        t.fn = TaskStoryTitle;
        TaskStoryTitle(t);
        return true;
    }
    if (r.type == 0x26 && r.state == 0) {  // 0x0048E290 -> 0x0048E350 (runs at once)
        auto d = std::make_unique<ParkedCar>();
        d->fn = 0x0048E350;
        t.data = std::move(d);
        t.fn = TaskParkedCar;
        TaskParkedCar(t);
        return true;
    }
    if (r.type == 0x50) {  // 0x004997E0: only sets the block and the function (first update next frame)
        auto d = std::make_unique<Caption>();
        d->kind = r.ParamS32(0);
        d->state = static_cast<int16_t>(r.state);
        d->x = r.pos[0];
        d->y = r.pos[1];
        switch (d->kind) {
        case 0xC: d->base = 0x20; break;
        case 0xD: d->base = 0x24; break;
        case 0xE: d->base = 0x28; break;
        case 0xF: d->base = 0x2C; break;
        case 0x10: d->base = 0x1C; break;
        default:
            // the original keeps 0x004997E0 as the function (a new block every frame): not reached by the captures
            w.Note("actor type 0x50 kind " + std::to_string(d->kind) + " (0x004997E0 default) not ported");
            t.fn = [](Task&) {};
            return true;
        }
        d->fn = d->kind == 0x10 ? 0x00499A80 : 0x004998A0;
        t.data = std::move(d);
        t.fn = TaskCaption;
        return true;
    }
    return false;
}

}  // namespace game
