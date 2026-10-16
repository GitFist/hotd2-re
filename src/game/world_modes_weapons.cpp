// game::World - the trigger of game type 1 (original mode): Player_TriggerSpecial 0x00414B90 with the special weapons
// (weapon rows 0x00579ED8, the items' magazine / effect / sound bytes 0x009A2248 + p * 0x14), its reload 0x00414E40, the
// game-type-1 extras of Shot_SpawnTracer 0x00416F70 (flash types 3 / 4, tracer type 5, the third effect ring
// 0x009A2700) and their part of 0x00416B00. Weapon 0 (no weapon item) plays like Player_Trigger 0x00414940 with the item
// magazine. notes/agents/modes.md section 8; captures build\scene_capture_modes_weapon1..3.txt.

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

uint8_t Row(const ExeImage& exe, int weapon, int k) {  // 0x00579ED8[weapon * 8 + k]
    uint8_t v = 0;
    exe.Read(0x00579ED8 + static_cast<uint32_t>(weapon * 8 + k), &v, 1);
    return v;
}

// 0x00416420: the weapon row's bytes 0..3 -> +0x10..+0x13
void RowReset(World::Impl& w, ModesState::PlayerItems& it) {
    for (int k = 0; k < 4; ++k) it.b10[k] = Row(w.exe, it.weapon, k);
}

// Shot_SpawnTracer 0x00416F70 in game type 1: flash (type by +9: 3 / 4 / else 0), tracer (type 5 at speed 0x004C43A4
// for +9 == 5, else 20), then the ring index moves on and the third ring's slot (the new index) gets the extra effect
void SpawnTracer(World::Impl& w, int p) {
    World::Impl::Player& P = w.player[p];
    ModesState& m = *w.modes;
    const int fx = static_cast<int8_t>(m.pitems[p].b8[1]);
    const float vx = P.aim[0] / w.focal, vy = P.aim[1] / w.focal;
    World::Impl::Player::Flash& f = P.flash[P.ring];
    f.active = true;
    f.counter = 0;
    f.pos[0] = vx;
    f.pos[1] = vy;
    f.pos[2] = -1.0f;
    mx::DirToAngles(vx, vy, -1.0f, &f.pitch, &f.yaw);  // 0x004016B0
    f.yaw += 0x8000;
    f.pitch = -f.pitch;
    f.type = fx == 3 ? 3 : fx == 4 ? 4 : 0;
    World::Impl::Player::Tracer& tr = P.tracer[P.ring];
    tr.active = true;
    tr.counter = 0;
    const float local[3] = { vx, vy, -1.0f };
    mx::Transform(w.cam.inverse, local, tr.pos);
    const float d[3] = { tr.pos[0] - w.cam.pos[0], tr.pos[1] - w.cam.pos[1], tr.pos[2] - w.cam.pos[2] };
    const float len = static_cast<float>(std::sqrt(static_cast<double>(d[2]) * d[2] + static_cast<double>(d[1]) * d[1] +
                                                   static_cast<double>(d[0]) * d[0]));  // 0x004AAA00
    const bool slow = fx == 5;
    const float speed = slow ? F32(w.exe, 0x004C43A4) : F32(w.exe, 0x004C4C8C);
    for (int k = 0; k < 3; ++k) tr.vel[k] = (d[k] / len) * speed;
    tr.type = slow ? 5 : 0;
    if (++P.ring > 5) P.ring = 0;
    if (fx == 0) return;
    ModesState::Extra& e = m.extras[p][P.ring];
    e.active = true;
    e.counter = 0;
    if (fx == 1) {
        e.type = 1;
    } else if (fx == 3) {
        e.type = 3;
    } else if (fx == 4) {
        e.pos[0] = vx;
        e.pos[1] = vy;
        e.pos[2] = -1.0f;
        mx::DirToAngles(vx, vy, -1.0f, &e.pitch, &e.yaw);
        e.yaw += 0x8000;
        e.pitch = -e.pitch;
        e.spin = w.Rand(0x0041729D) % 0xFFFF;
        e.type = 4;
    } else {
        e.type = 0;
    }
}

}  // namespace

// 0x00414E40: the reload of game type 1 (weapon 2 only when empty): the weapon row, the item magazine (-1 stays -1),
// the item's reload sound 0x004EC9C0[+0xA] (else 0x3E16A9)
void SpecialReload(World::Impl& w, int p) {
    World::Impl::Player& P = w.player[p];
    ModesState::PlayerItems& it = w.modes->pitems[p];
    if (it.weapon == 2 && P.empty != 1) return;
    RowReset(w, it);
    P.ammo = static_cast<int16_t>(static_cast<int8_t>(it.b8[0]));
    P.empty = 0;
    if (w.canShoot == 0) return;
    const uint32_t s = w.exe.U32(0x004EC9C0 + 4 * static_cast<uint32_t>(static_cast<int8_t>(it.b8[2])));
    w.Sound(s != 0 ? s : 0x3E16A9u);
}

// 0x00414770 (Player_Start) in game type 1: the magazine 0x009A5C7C = the item magazine (+8), the weapon row 0x00416420;
// rows 1 / 3: the item lives (+4) as lives / shown lives (0x00416340 / 0x004163D0), row 3 first loses the items' effects
// (0x00416240); rows 1..3: magazine -1 -> 6
void ItemsPlayerStart(World::Impl& w, int p, int row) {
    World::Impl::Player& P = w.player[p];
    ModesState& m = *w.modes;
    ModesState::PlayerItems& it = m.pitems[p];
    P.ammo = static_cast<int16_t>(static_cast<int8_t>(it.b8[0]));
    RowReset(w, it);  // 0x00416420
    if (row < 1 || row > 3) return;
    if (row == 3 && !RandomizerActive(w)) {  // Randomizer reserves a full loadout for a late-joining player too
        for (int i = 0; i < 2; ++i) {
            const int item = it.slot[i];
            auto plain = [&] {  // weapon 0, the row 0 bytes 0x004EC928 / factor 0x004EC92C
                it.weapon = 0;
                for (int k = 0; k < 4; ++k) it.b8[k] = static_cast<uint8_t>(w.exe.U32(0x004EC928) >> (8 * k));
                it.damage = F32(w.exe, 0x004EC92C);
            };
            switch (item) {
            case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 0xB: case 0xD:
                plain();
                break;
            case 0xC:
                plain();
                m.itemFlags[4] = 1;  // 0x009C88AC (falls through to case 0x14)
                break;
            case 0x14: m.itemFlags[4] = 1; break;
            case 0xE: case 0xF:
                it.lives = 3;
                it.b5 = 5;
                break;
            case 0x15: m.itemFlags[0] = 1; break;
            case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C:
                it.character = static_cast<int8_t>(p);
                break;
            case 0x1D: m.itemFlags[1] = 1; break;
            case 0x1E: m.itemFlags[2] = 1; break;
            case 0x1F: m.itemFlags[3] = 1; break;
            case 0x20: it.b3 = 2; break;
            default: break;
            }
        }
    }
    if (row != 2) P.lives = P.livesShown = it.lives;
    P.ammo = static_cast<int8_t>(it.b8[0]) != -1 ? static_cast<int16_t>(static_cast<int8_t>(it.b8[0])) : 6;
}

// Player_TriggerSpecial 0x00414B90 (+ Player_UpdateAim 0x00415710)
void SpecialTrigger(World::Impl& w, Task& t) {
    const int p = t.As<PlayerTask>().index;
    World::Impl::Player& P = w.player[p];
    ModesState& m = *w.modes;
    ModesState::PlayerItems& it = m.pitems[p];
    const ExeImage& exe = w.exe;
    if (w.demoFlag == 1) {
        w.SetSites({ w.attackers != 0 ? 0x00415740u : 0x0041572Eu, 0x00415757u, 0x0041576Bu });
        const DemoShot s = DemoAutoGun(w.rng, p, w.attackers != 0);
        ApplyDemoShot(s, P.gun);
    }
    P.pending = 0;
    P.aim[0] = static_cast<float>(P.gun.x);
    P.aim[1] = static_cast<float>(P.gun.y);
    // +0x13: frames until the next shot
    const int8_t was = static_cast<int8_t>(it.b10[3]);
    it.b10[3] = static_cast<uint8_t>(was - 1);
    if (static_cast<int8_t>(was - 1) < 1) it.b10[3] = 0;
    const int8_t magazine = static_cast<int8_t>(it.b8[0]);
    bool fire = !(0 < static_cast<int8_t>(it.b10[3]) || (P.gun.fired == 0 && it.b10[1] != 1));
    if (fire && (P.gun.valid == 0 || P.ammo == 0)) {
        if (P.inputKind == 1 && P.gun.valid == 0 && P.ammo != magazine) SpecialReload(w, p);
        fire = false;
    }
    if (fire && w.canShoot == 0) fire = false;
    if (fire) {
        bool spend = w.gameMode != 5 && magazine != -1;  // (infinite ammo 0x009C9FD9 + p * 0x7C assumed off)
        if (spend) {
            const int wt = it.weapon;
            if (wt == 1) {  // three-shot burst: one round per burst (+0x12 counts the burst down)
                it.b10[1] = Row(exe, 1, 5);
                it.b10[3] = Row(exe, 1, 7);
                const int8_t c = static_cast<int8_t>(it.b10[2]);
                it.b10[2] = static_cast<uint8_t>(c - 1);
                if (0 < static_cast<int8_t>(c - 1)) {
                    spend = false;
                } else {
                    it.b10[1] = Row(exe, 1, 1);
                    it.b10[2] = Row(exe, 1, 6);
                }
            } else {
                it.b10[1] = Row(exe, wt, 5);
                it.b10[3] = Row(exe, wt, 7);
            }
            if (spend) --P.ammo;
        }
        if (P.ammo == 0) {
            it.b10[1] = 0;
            P.empty = 1;
            P.reloadBlink = 0;
        }
        P.pending = 1;  // 0x009A5C78 + p * 0x130
        P.firedThisFrame = true;
        if (w.GlobalInt(0x009A5C48, 0) == 0) ++P.shots;
        if (it.weapon == 1 && it.b10[2] != 2) {  // the burst's later shots spread (+-63 x, +-47 y)
            const int sx = w.Rand(0x00414CE9) & 1;
            const int ax = w.Rand(0x00414D03) & 0x3F;
            const float x0 = P.aim[0];
            const int sy = w.Rand(0x00414D26) & 1;
            const int ay = w.Rand(0x00414D40) % 0x30;
            P.aim[0] = static_cast<float>((sx * -2 + 1) * ax) + x0;
            P.aim[1] = static_cast<float>((sy * -2 + 1) * ay) + P.aim[1];
        }
        P.ray = MakeShotRay(P.aim[0], P.aim[1], w.focal);  // Shot_BuildRay 0x00406110
        SpawnTracer(w, p);                                 // 0x00416F70
        const uint32_t s = exe.U32(0x004EC9A0 + 4 * static_cast<uint32_t>(static_cast<int8_t>(it.b8[2])));
        w.Sound(s != 0 ? s : exe.U32(0x004EC8BC + 4 * static_cast<uint32_t>(p)));
    }
    // pad reload button (0x009C9028 & 0x009C9F6C[p][kind]) for input kind 0: pads are not driven
    if (w.canShoot == 0 && w.runner->skippable) {  // cutscene skip request (as 0x00414940)
        const uint32_t mask = p == 0 ? 2u : 0x20000u;
        if (w.pressed & mask) w.skipRequest = true;
    }
}

// 0x00416B00, game type 1: the third ring's slot i (type 4: models 0xA6F + count, scale 0.05, in view space; 24 frames)
void SpecialExtraDraw(World::Impl& w, int p, int i) {
    ModesState::Extra& e = w.modes->extras[p][i];
    if (e.type == 4) {
        if (0x17 < e.counter) e.active = false;
        if (e.active) {
            Mat4 m = Mat4::Identity();
            mx::Translate(m, e.pos[0], e.pos[1], e.pos[2]);
            mx::RotY(m, e.yaw);
            mx::RotX(m, e.pitch);
            mx::RotZ(m, e.spin);
            const float s = 0.05f;  // 0x3D4CCCCD
            mx::Scale(m, s, s, s);
            w.Draw(e.counter + 0xA6F, m, 0xC, -1.0f, false, -1, true);
        }
    }
    ++e.counter;
}

// 0x00416B00: tracer type 5 (weapon effect 5): camera path 0x194 at count % 24 around the tracer, model 0x109D
void SpecialTracer5Draw(World::Impl& w, const float pos[3], int counter) {
    Mat4 m = w.cam.view;
    mx::Translate(m, pos[0], pos[1], pos[2]);
    float pp[3];
    int a[3] = {};
    float tg[3] = {};
    int roll = 0;
    if (!w.cameraBank.Evaluate(0x194, static_cast<float>(counter % 0x18), pp, tg, &roll, false)) return;  // 0x004042D0
    for (int k = 0; k < 3; ++k) a[k] = static_cast<int>(tg[k]);
    mx::Translate(m, pp[0], pp[1], pp[2]);
    mx::ClearRotation(m);  // 0x004A9F70
    mx::RotZ(m, a[2]);
    mx::RotY(m, a[1]);
    mx::RotX(m, a[0]);
    w.Draw(0x109D, m, 0xC, -1.0f, false, -1, true);
}

}  // namespace game
