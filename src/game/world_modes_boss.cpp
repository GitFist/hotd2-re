// game::World - boss mode (game type 3): the boss select (actor type 0x6C, task 0x00425010, spawned by st1evtbl
// scene 0x10 variant 0), the game setup after the choice (0x00425E90), the boss records (0x00425F40) and the boss-mode
// stage end (0x004603B0). notes/agents/modes.md section 6; verified against build\scene_capture_modes_boss0.txt.

#include <algorithm>
#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_modes.h"
#include "game/world_menu_pointer.h"
#include "game/world_nightshift.h"

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

SpriteDef Simple(int id, float x, float y, float depth, float sx, float sy, int rot, uint32_t flags) {
    SpriteDef d;
    d.id = id;
    d.x = x;
    d.y = y;
    d.depth = depth;
    d.sx = sx;
    d.sy = sy;
    d.rot = rot;
    d.flags = flags;
    return d;
}

}  // namespace

// The select screens' agent (0x00497F80, shared by the boss / training selects): camera matrix x T(-10, -22, -27) x
// RotX(0) RotZ(0xFD80) RotY(0x8E88) gives position and angles; drawn with its shadow (0x00411090)
void SelectAgentDraw(World::Impl& w, EnemyTask& a) {
    Mat4 m = w.cam.inverse;
    mx::Translate(m, -10.0f, -22.0f, -27.0f);
    mx::RotX(m, 0);
    mx::RotZ(m, 0xFD80);
    mx::RotY(m, 0x8E88);
    mx::GetTranslation(m, a.pos);
    MatrixToAnglesYAxis(m, &a.rot[0], &a.rot[1], &a.rot[2]);
    // 0x0041DC10(0x009A5C2C, context pitch / yaw, 0x009A5C20..): env block 1 from the camera (renderer state)
    w.LightCustom(w.env.block1.lightIntensity, w.cam.pitch, w.cam.yaw, w.env.block1.lightColor);
    w.animLight = 0;
    w.AnimDraw(a);
    w.animLight = 1;
    w.EnemyShadow(a);
    w.LightScene();  // 0x0041DCC0
    ++a.anim.counter[0];
}

// 0x00413CC0: the screens' dimming counter
void ModesDimCount(World::Impl& w) {
    if (w.held == 0 && w.pressed == 0) {
        if (18000 < ++w.front.dimCounter && w.front.dimCounter > 0x468C) w.front.dimCounter = 0x468C;
    } else {
        w.front.dimCounter = 0;
    }
}

namespace {

struct BossSelect : EnemyTask {  // 0x00425010 object (0x13F4)
    int16_t step = 0;            // +0x1310
    int sel = 0;                 // +0x1350
    int prevSel = 0;             // +0x136C
    int diff = 0;                // +0x1354 difficulty chosen (<= the boss's grade, < 5)
    int open = 7;                // +0x1330 last selectable entry (9 once all eight grades are set)
};

// 0x00425DF0: a grade mark (model 0x1A4C, view space, translucent alpha), turning on the current entry
void GradeMark(World::Impl& w, int entry, int i, float x, int spin, float alpha) {
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    const float k = F32(exe, 0x0055CC58);
    Mat4 t = Mat4::Identity();
    mx::Translate(t, ((x - F32(exe, 0x0055CC54)) / w.focal) * k, (static_cast<float>(i * 0x20 - 2) / w.focal) * k, -36.0f);
    if (entry != m.bossCur) spin = 0;
    mx::RotY(t, static_cast<int16_t>(spin));
    w.Draw(0x1A4C, t, 8, alpha, false, -1, true);  // 0x004185A0
}

// the grade-5 marks' light 0x0041DC10(0x009A378C, 0x009A3558, 0x009A355C, 1.0, 0, 0)
void RedLight(World::Impl& w) {
    const float red[3] = { 1.0f, 0.0f, 0.0f };
    w.LightCustom(w.env.lightIntensity, w.env.lightPitch, w.env.lightYaw, red);
}

// 0x00425BD0: one carousel entry (frame 0xAF1, portrait 0x0055CBE4[k] tinted by its brightness, grade marks)
void BossEntry(World::Impl& w, BossSelect& b, int k, float x, float bright) {
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    w.LightColor(bright, bright, bright);  // 0x004AA0A0: the portrait tint and the grade marks' light colour
    w.hudSprites.push_back(Simple(0xAF1, x + F32(exe, 0x0055CC50), 90.0f, 100.0f, 1, 1, 0, 0));
    if (b.open < k) return;
    uint32_t flags = 0x2000;
    float px = x + F32(exe, 0x0055CC4C);
    if (k == 2 || k == 4 || k == 6) {  // the second boss of a stage: the first one's portrait, mirrored
        flags = 0x2010;
        px = px - F32(exe, 0x0055CC48);
    }
    SpriteDef d = Simple(static_cast<int>(exe.U32(0x0055CBE4 + static_cast<uint32_t>(k) * 4)), px, 130.0f, 90.0f, 1, 1, 0, flags);
    d.tinted = true;
    d.tint[0] = d.tint[1] = d.tint[2] = bright;
    w.hudSprites.push_back(d);
    const int grade = m.bossGrades[k];
    if (grade < 0) return;
    if (k == m.bossCur) {
        m.bossSpin = static_cast<int16_t>(m.bossSpin + 0x1FE);
        for (int i = 0; i < grade + 1; ++i) {
            if (grade == 5) RedLight(w);  // 0x0041DC10(0x009A378C, 0x009A3558, 0x009A355C, 1, 0, 0)
            if (i < b.diff + 1) GradeMark(w, k, i, x, m.bossSpin, 1.0f);
            else if (i < 5) GradeMark(w, k, i, x, 0, 0.3f);
            if (grade == 5) w.LightScene();  // 0x0041DCC0
        }
    } else {
        for (int i = 0; i < grade + 1; ++i) {
            if (grade == 5) RedLight(w);
            if (i < 5) GradeMark(w, k, i, x, m.bossSpin, 1.0f);
            if (grade == 5) w.LightScene();
        }
    }
}

// 0x00425780: background, title, carousel (slides 10 frames on a change), cursor star, boss name, arrows, captions
void BossSelectDraw(World::Impl& w, BossSelect& b) {
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    int id = 0xADB;
    for (int y = 0; id < 0xAEF; y += 0x80)
        for (int x = 0; x < 0x280; x += 0x80) w.hudSprites.push_back(Simple(id++, static_cast<float>(x), static_cast<float>(y), 200.0f, 1, 1, 0, 0));
    w.hudSprites.push_back(Simple(0xB30, 320.0f, 54.0f, 1.0f, 1, 1, 0, 10));
    if (m.bossAnim == 1 || m.bossAnim == 2) {
        ++m.bossAnimCount;
        const float da = F32(exe, 0x0055CC44), dx = F32(exe, 0x0055CC40);
        for (int k = 0; k < 10; ++k) {
            ModesState::Slot& s = m.bossSlots[k];
            if (k == m.bossCur) s.a = s.a + da;
            if (k == m.bossPrev) s.a = s.a - da;
            if (m.bossAnim == 1) {
                s.x = s.x - dx;
                if (s.x < F32(exe, 0x0055CC3C)) s.x = s.x + F32(exe, 0x0055CC34);
            } else {
                s.x = s.x + dx;
                if (F32(exe, 0x0055CC38) < s.x) s.x = s.x - F32(exe, 0x0055CC34);
            }
        }
        if (m.bossAnimCount == 10) m.bossAnim = m.bossAnimCount = 0;
    }
    for (int k = 0; k < 10; ++k) BossEntry(w, b, m.bossSlots[k].id, m.bossSlots[k].x, m.bossSlots[k].a);
    // 0x004AA0A0(1, 1, 0.2) for the star, then back to 0x009A3780.. (the env light colour)
    for (int c = 0; c < 3; ++c) w.light.color[c] = w.env.lightColor[c];
    {  // 0x004AA0A0(1, 1, 0.2): the cursor star pulses (x87 sin of 0x009A32A0 % 60)
        // the FPU runs at 24-bit precision (Direct3D): every x87 step rounds to a float
        const float arg = static_cast<float>(static_cast<double>(static_cast<int>(w.seedCounter % 0x3C) * 0x444) * F64(exe, 0x004C4370));
        const float s = static_cast<float>(std::sin(static_cast<double>(arg)));
        const float sc = static_cast<float>(s * F32(exe, 0x004C4C88)) + static_cast<float>(F64(exe, 0x004ECB70));
        SpriteDef d = Simple(0xAEF, 322.0f, 194.0f, Bits(0x42006666), sc, sc, 0, 0x200A);
        d.tinted = true;
        d.tint[2] = Bits(0x3E4CCCCD);
        w.hudSprites.push_back(d);
    }
    if (b.sel <= b.open)
        w.hudSprites.push_back(Simple(static_cast<int>(exe.U32(0x0055CC0C + static_cast<uint32_t>(b.sel) * 4)), 321.0f, 100.0f, 65.0f, 1, 1, 0, 10));
    const uint32_t u = w.seedCounter % 0x3C;
    if (u < 0x2D) {
        const float alpha = u < 8 ? 1.0f : (F32(exe, 0x005308DC) - static_cast<float>(u)) * F32(exe, 0x005308D8);
        SpriteDef l = Simple(0xAFF, 100.0f, 136.0f, 2.0f, 1, 1, 0, 0);
        l.alpha = alpha;
        w.hudSprites.push_back(l);
        SpriteDef r = Simple(0xAFF, 542.0f, 136.0f, 2.0f, 1, 1, 0, 0x17);
        r.alpha = alpha;
        w.hudSprites.push_back(r);
    }
    w.hudSprites.push_back(Simple(0xB28, 144.0f, 365.0f, 31.0f, 1, 1, 0, 9));
    if (b.open < b.sel) w.hudSprites.push_back(Simple(0xB1C, 222.0f, 360.0f, Bits(0x41F7D70A), 1, 1, 0, 9));
    else w.hudSprites.push_back(Simple(0xB1B, 195.0f, 380.0f, Bits(0x41F7D70A), 1, 1, 0, 9));
}

// pad masks per player 0x009C7000 (tables 0x00589140..)
uint32_t Mask(World::Impl& w, uint32_t table) {
    return w.exe.U32(table + static_cast<uint32_t>(w.cfg.portNetworkRoom ? 0 : w.playersInGame) * 4);
}

void TaskBossSelect(Task& tk) {
    World::Impl& w = *CurrentWorld();
    BossSelect& b = tk.As<BossSelect>();
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    if (b.step == 0) {
        // textures 0x14E / 0x16F / 0x173, pol 0xBF, model 0x1A4C, motion file 0x30, flush, 0x00413DA0, BGM BOSS_MOD
        for (const World::Impl::Request& r : { World::Impl::Request{ 0x54, 0x14E }, World::Impl::Request{ 0x54, 0x16F },
                                               World::Impl::Request{ 0x54, 0x173 }, World::Impl::Request{ 0x52, 0xBF },
                                               World::Impl::Request{ 0x50, 0x1A4C }, World::Impl::Request{ 0x56, 0x30 } })
            w.ProcessRequest(r);  // queued and flushed at once (0x0041D970)
        w.front.dimCounter = 0;
        w.Sound(0x10000024);
        b.actorSlot = -1;
        b.slot120 = -1;
        b.anim.kind = 0x3A;
        b.anim.motion[0] = 0x31A;
        w.EnemyInitPose(b);  // 0x00410440
        b.nodeCb = 0x00411050;
        b.postRootFn = [](World::Impl&, EnemyTask&) {};
        b.anim.rotOrder = 1;
        b.anim.nodes[5].model = 0x15A3;  // +0x4DC
        b.anim.drawFlags &= ~2u;
        w.attackers = 1;  // 0x009C7006: the scene script waits (op 0x43)
        if (m.a2bbc == 1) {
            b.sel = b.prevSel = 9;
            m.bossIndex = 9;
        } else {
            b.sel = b.prevSel = m.bossIndex;
        }
        m.a2bbc = 0;
        w.flags5900 |= 0x18;
        m.bossRecord = 0;  // 0x009A34C0
        b.diff = m.bossGrades[b.sel];
        if (b.diff == 5) b.diff = 4;
        int graded = 0;
        for (int i = 0; i < 8; ++i)
            if (m.bossGrades[i] != 0) ++graded;
        b.open = graded < 8 ? 7 : 9;
        m.bossCur = static_cast<int16_t>(m.bossIndex);
        m.bossRepeat = 0;
        m.bossRepeatDir = 0;
        m.bossAnim = m.bossAnimCount = 0;
        m.bossPrev = 0;
        m.bossSpin = 0;
        for (int i = 0; i < 10; ++i) {
            const int k = (m.bossIndex + i) % 10;
            ModesState::Slot& s = m.bossSlots[k];
            s.id = static_cast<int16_t>(k);
            const float x = static_cast<float>(i * 0xD2);
            s.x = F32(exe, 0x0055CC38) < x ? x - F32(exe, 0x0055CC34) : x;
            s.a = i == 0 ? 1.0f : 0.3f;
        }
        b.step = 2;
    } else if (b.step > 0 && b.step < 5) {
        ++b.step;
    }
    SelectPointer(w, Mask(w, 0x00589160), true, b.step > 4 && m.bossAnim == 0);
    // L / R held 60 frames: auto repeat (0x009C9020 held)
    const uint32_t mR = Mask(w, 0x00589158), mL = Mask(w, 0x00589150);
    if ((w.held & mR) == 0 && (w.held & mL) == 0) {
        m.bossRepeat = 0;
        m.bossRepeatDir = 0;
    } else if (++m.bossRepeat == 0x3C) {
        m.bossRepeat = 0;
        if (w.held & mR) m.bossRepeatDir = 1;
        else if (w.held & mL) m.bossRepeatDir = 2;
    }
    auto setDiff = [&] {
        b.diff = m.bossGrades[b.sel];
        if (b.diff == 5) b.diff = 4;
    };
    if (m.bossAnim == 0) {
        if ((w.pressed & mR) || m.bossRepeatDir == 1) {
            w.Sound(0xA9);
            if (9 < ++b.sel) b.sel = 0;
            setDiff();
            m.bossAnim = 1;
            m.bossPrev = m.bossCur;
            if (9 < ++m.bossCur) m.bossCur = 0;
            m.bossSpin = 0;
            m.bossAnimCount = 0;
        } else if ((w.pressed & mL) || m.bossRepeatDir == 2) {
            w.Sound(0xA9);
            if (--b.sel < 0) b.sel = 9;
            setDiff();
            m.bossAnim = 2;
            m.bossPrev = m.bossCur;
            if (--m.bossCur < 0) m.bossCur = 9;
            m.bossSpin = 0;
            m.bossAnimCount = 0;
        }
    }
    if (b.sel <= b.open) {  // difficulty up / down within the boss's grade
        if (w.pressed & Mask(w, 0x00589140)) {
            if (b.diff < m.bossGrades[b.sel] && b.diff < 4) {
                w.Sound(0x1000A9);
                ++b.diff;
            }
        } else if ((w.pressed & Mask(w, 0x00589148)) && 0 < b.diff) {
            w.Sound(0xF00A9);
            --b.diff;
        }
    }
    // the agent's motion: 0x31A (walk in) -> 0x31C, 0x320 (shake head at a locked entry) -> 0x321
    if (b.sel == b.prevSel) {
        if (b.anim.motion[0] == 0x31A && b.anim.tick[0] == w.MotionTicks(0x31A) - 1) w.AnimBlendTo(b, 0x31C, 0, 2);
        else if (b.anim.motion[0] == 0x320 && b.anim.tick[0] == w.MotionTicks(0x320) - 1) w.AnimBlendTo(b, 0x321, 0, 2);
    } else if (b.open < b.prevSel) {
        w.AnimSetMotion(b, b.sel <= b.open ? 0x31A : 0x320);
    } else if (b.open < b.sel) {
        w.AnimSetMotion(b, 0x320);
    }
    {
        const size_t first = w.hudSprites.size();
        BossSelectDraw(w, b);
        for (size_t i = first; i < w.hudSprites.size(); ++i) w.hudSprites[i].source = 0x00425780;
    }
    SelectPointerDraw(w, Mask(w, 0x00589160), b.diff);
    if ((w.pressed & (Mask(w, 0x00589160) | Mask(w, 0x00589168))) && 4 < b.step) {
        if (b.sel <= b.open) {
            w.Sound(0x80000000);
            w.Sound(0x121A9);
            const int stage = exe.S16(0x0055CBBC + static_cast<uint32_t>(b.sel) * 4);
            const int scene = exe.S16(0x0055CBBE + static_cast<uint32_t>(b.sel) * 4);
            m.bossIndex = b.sel;
            m.bossDiff = b.diff;  // 0x009C8FB0
            if (b.sel == 9) {
                m.bossIndex = 0;
                m.a2bbc = 1;  // boss rush
            }
            BossGameSetup(w);  // 0x00425E90
            w.runner->camera.args[0] = 1;  // 0x009A6184: the select's held camera preset ends next camera frame
            w.runner->yield = false;       // 0x009C8EA0: the VM's wait op restarts (one more frame)
            if (stage != 0 || scene != 0x10) BossSwitchScene(w, stage, scene);
            w.ts.KillCurrent();
            return;
        }
        w.Sound(0x200A9);
    }
    if ((w.pressed & Mask(w, 0x00589170)) && 4 < b.step) {  // back to the mode select
        w.Sound(0x500A9);
        w.FrontAfterStart();  // 0x00406FD0 credits, 0x0049DFD0, 0x0040AC10
        w.front.modeRequest = 4;
        w.ts.KillCurrent();
        return;
    }
    ModesDimCount(w);
    SelectAgentDraw(w, b);
    b.prevSel = b.sel;
}

}  // namespace

// 0x00425E90: the game after a boss choice (pause allowed again, records cleared, rank from the chosen difficulty)
void BossGameSetup(World::Impl& w) {
    PortLeaderboardBegin(w);
    ModesState& m = *w.modes;
    w.flags5900 = (w.flags5900 & ~0x38u) | 2;
    w.bossActive = 0;  // 0x009CA0EA
    m.bossTime = 0;    // 0x009C7060
    w.attackers = 0;
    w.rankTimerOn = true;
    w.rank = static_cast<int8_t>(w.exe.S16(0x005679F4 + static_cast<uint32_t>(m.bossDiff)) & 0xFF) + w.players * 4;
    w.rankAdj = 0;
    w.rankTimer = 1;
    for (auto& P : w.player) {
        P.streak = 0;
        P.shots = 0;
        P.hits = 0;
    }
    // 0x0041D440, 0x004040A0, 0x00412FD0, flush 0x0041D970
    memset(w.komono40Intact, 1, sizeof(w.komono40Intact));  // 0x00463680
    w.padClear = 1;
    w.canShoot = 0;
    for (auto& P : w.player) {  // 0x0040D220
        P.gun.x = P.gun.y = 0;
        P.gun.fx = P.gun.fy = 0;
    }
}

void BossRecords(World::Impl& w) {  // 0x00425F40
    if (!w.modes || w.runner->cfg.gameType != 3) return;
    ModesState& m = *w.modes;
    int rec = 0;
    m.bossRecord = 0;
    auto fives = [&] {
        int n = 0;
        for (int8_t g : m.bossGrades) n += g == 5;
        return n;
    };
    const int before = fives();
    if (m.a2bbc == 0) {
        if (!(m.bossDiff < m.bossGrades[m.bossIndex]) && m.bossDiff <= 4) {
            ++m.bossGrades[m.bossIndex];
            rec = 1;
        }
    } else if (m.a2bbc == 1 && m.bossIndex == 8 && !(m.bossDiff < m.bossGrades[9]) && m.bossDiff <= 4) {
        ++m.bossGrades[9];
        rec = 1;
    }
    const int after = fives();
    if (after != before && after == 10) {  // every boss at the top grade: three items
        for (int i : { 6, 0x13, 0x1D })
            if (m.items[i] < 99) ++m.items[i];
        ++rec;
    }
    // grades / items copied into the save block (0x009C9F33.., 0x009C9F3D..): ModesState::Store
    int graded = 0;
    for (int8_t g : m.bossGrades) graded += 0 < g;
    if (graded == 10) {
        m.save.SetU8(SaveData::kFlags, static_cast<uint8_t>(m.save.U8(SaveData::kFlags) | 4));
        ++rec;
    }
    if (rec != 0) m.bossRecord = 1;
}

// 0x004157F0(p) in boss mode: the player qualifies for the boss's time table (its 10th time not beaten: -1)
int BossRankCheck(World::Impl& w, int p) {
    ModesState& m = *w.modes;
    int t = m.bossIndex + 2;  // 0x009A32C0.. = tables 2.. of 0x0057A354
    if (m.bossIndex == 8 && m.a2bbc != 0) t = 11;
    else if (m.bossIndex >= 9) t = 11;
    if (w.players == 2) return -1;  // 0x007C211C
    if (m.tables[t][9].score < m.bossTime) return -1;
    return w.player[p].state != 9 ? 1 : -1;
}

// 0x00480F70: the name entry's table (0x007DCF44) as an index of 0x0057A354 (game type 0: 0, 1: 1, 3: 2 + boss, the
// rush 11; note: unlike 0x004157F0 boss 8 is table 10 here even in the rush, where the stage end has already moved
// 0x009A2268 on to 9)
int ModesTableIndex(World::Impl& w) {
    const int gt = w.runner ? w.runner->cfg.gameType : 0;
    if (gt == 1) return 1;
    if (gt == 3 && w.modes) {
        const int b = w.modes->bossIndex;
        return b >= 0 && b <= 9 ? b + 2 : 2;
    }
    return 0;
}

// 0x00425010 for a boss outside stage 1: the stage changes inside the running stage tasks (0x009A1A08 / 0x009A2BC0,
// Evt_LoadTable 0x00413160, Evt_GetScript 0x0045EB90 with the current variant, the section released and 0,
// Stage_SelectTables 0x004014C0, the collision files 0x0048A3B0); then the stage's texture 0x00592630[stage]
void BossSwitchScene(World::Impl& w, int stage, int scene) {
    std::string err;
    if (!w.LoadStageTable(stage, &err)) {
        w.Note("boss mode: " + err);
        return;
    }
    w.cfg.eventMode = stage;
    w.runner->mode = stage;
    w.runner->scene = scene;
    w.runner->script = w.mem.Script(stage, scene, w.runner->variant);
    w.section = 0;           // 0x004015A0(section, 0) + 0x009A2224 = 0
    w.coliLoaded = false;    // 0x0048A3B0: coli0 + coli<stage + 1> again
    const uint32_t tex = w.exe.U32(0x00592630 + static_cast<uint32_t>(stage) * 4);
    if (tex != 0) w.ProcessRequest({ 0x54, tex });
}

namespace {

struct BossTitle : WTask {   // actor 0x60 in boss mode: 0x004342E0 -> 0x00434920 (+0x1312 step, +0x1330 count)
    int16_t step = 0;
    int count = 0;
};

// 0x00434920: the boss picture until the stage's event flag (0x0055DD5C), then the fight timer (0x009C7060: the
// original measures wall-clock time, GetTickCount 0x0049DF00 / 0x0049DF20 x 0.06; here game frames), blinking after the
// boss is down
void TaskBossTitle(Task& tk) {
    World::Impl& w = *CurrentWorld();
    BossTitle& t = tk.As<BossTitle>();
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    const int stage = w.runner->mode;
    switch (t.step) {
    case 0:
        w.ProcessRequest({ 0x54, static_cast<uint32_t>(stage + 0x160) });  // + flush
        w.rankAdj = 0;
        w.rankTimerOn = true;
        w.rankTimer = 1;
        w.rank = static_cast<int8_t>(exe.S16(0x005679F4 + static_cast<uint32_t>(m.bossDiff)) & 0xFF) + w.players * 4;
        t.step = 1;
        [[fallthrough]];
    case 1: {
        const int base = exe.S16(0x0055DD50 + static_cast<uint32_t>(stage) * 2);
        for (int r = 0, k = 0; r < 4; ++r)
            for (int c = 0; c < 5; ++c, ++k) {
                SpriteDef d = Simple(base + k, static_cast<float>(c * 0x80), static_cast<float>(r * 0x80), 2.0f, 1, 1, 0, 0);
                d.source = 0x00434920;
                w.hudSprites.push_back(d);
            }
        if (w.runner->flags[exe.U32(0x0055DD5C + static_cast<uint32_t>(stage)) & 0xFF] == 1) ++t.step;
        break;
    }
    case 2:  // texture 0x18C + stage released
        w.flags5900 &= ~0x20u;
        ++t.step;
        break;
    case 3:
        if (w.bossActive == 0) break;
        t.step = 4;
        m.timerStart = w.frame;  // 0x0049DF00
        m.timerBase = 0;
        if (m.a2bbc == 1 && m.bossIndex != 0) m.timerBase = m.bossTime;  // 0x0049DF70 (boss rush: the time goes on)
        [[fallthrough]];
    case 4:
        if (w.bossActive == 0) {
            ++t.step;
            t.count = 0;
        } else {
            m.bossTime = m.Clock(w.frame);  // 0x0049DF20
        }
        break;
    case 5:
        if (0x78 < t.count++) t.step = 6;
        break;
    default: break;
    }
    if ((2 < t.step && t.step != 5) || (t.step == 5 && t.count % 0x1E < 0x14)) {
        const int v = m.bossTime;
        const int hi = static_cast<int>((static_cast<int64_t>(v) * 0x64A7C8C7) >> 32) - v;
        int f = v + ((hi >> 0x11) - (hi >> 0x1F)) * 0x34BC0;
        const int mins = f / 0xE10;
        f %= 0xE10;
        const int secs = f / 0x3C, cs = ((f % 0x3C) * 100) / 0x3C;
        auto dig = [&](int id, float x, float y, float sy) {
            SpriteDef d = Simple(id, x, y, Bits(0x3F4CCCCD), 1, sy, 0, 10);
            d.source = 0x00434920;
            w.hudSprites.push_back(d);
        };
        if (0 < mins / 10) dig(mins / 10 + 0xACF, 220.0f, 420.0f, 1.0f);
        dig(mins % 10 + 0xACF, 248.0f, 420.0f, 1.0f);
        dig(0x63B, 270.0f, 438.0f, 0.6f);
        dig(secs / 10 + 0xACF, 292.0f, 420.0f, 1.0f);
        dig(secs % 10 + 0xACF, 320.0f, 420.0f, 1.0f);
        dig(0x643, 342.0f, 438.0f, 0.6f);
        dig(cs / 10 + 0xACF, 364.0f, 420.0f, 1.0f);
        dig(cs % 10 + 0xACF, 392.0f, 420.0f, 1.0f);
    }
}

}  // namespace

bool SpawnModes(World::Impl& w, Task& t, SpawnTask& s) {
    switch (s.rec.type) {
    case 0x60: {  // title card in boss mode (0x004342E0 -> 0x00434920)
        if (w.runner->cfg.gameType != 3) return false;
        w.flags5900 |= 0x20;
        auto d = std::make_unique<BossTitle>();
        d->fn = 0x00434920;
        t.data = std::move(d);
        t.fn = TaskBossTitle;
        TaskBossTitle(t);
        return true;
    }
    case 0x6E:  // original mode: the item select (world_modes_orig.cpp)
        if (w.runner->cfg.gameType != 1) return false;
        return SpawnItemSelect(w, t, s);
    case 0x23:  // training lesson 4: the mount alone (variant 2, world_st1b_boss.cpp)
        if (w.runner->cfg.gameType != 2) return false;
        return SpawnMount23(w, t, s.rec);
    case 0x1A: case 0x1B:  // training: the lessons' spawners (world_modes_train.cpp)
        if (w.runner->cfg.gameType != 2) return false;
        return SpawnLessonActor(w, t, s);
    case 0x6D:  // training: lesson select / lesson flow (world_modes_train.cpp)
        return SpawnTrain(w, t, s);
    case 0x6C: {  // boss select: the spawn task becomes it (same block)
        if (SpawnSceneSelect(w, t, s)) return true;  // port only: the Scene Select page in the select's room
        auto d = std::make_unique<BossSelect>();
        d->fn = 0x00425010;
        memcpy(d->pos, s.rec.pos, 12);
        memcpy(d->rot, s.rec.rot, 12);
        t.data = std::move(d);
        t.fn = TaskBossSelect;
        TaskBossSelect(t);
        return true;
    }
    default:
        return false;
    }
}

}  // namespace game
