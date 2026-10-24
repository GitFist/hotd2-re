// game::World - training (game type 2): actor type 0x6D (0x00496BA0), spawned by trnevtbl scene 0 (state 0: the lesson
// select 0x00496DE0) and by each lesson scene (state 1: the lesson flow 0x00497360 instructions / 0x00497640 READY-GO /
// 0x00497760 run / 0x00497A70 grading / 0x00497C30 passed / 0x00497DB0 failed). notes/agents/modes.md section 7;
// verified against build\scene_capture_modes_train0.txt / _train01.txt.

#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_modes.h"
#include "game/world_menu_pointer.h"

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

// Spr_DrawSimple 0x0041C6D0(id, x, y, depth, sx, sy, rot, flags) (+ the tint 0x004AA0A0 for flags 0x2000)
void Spr(World::Impl& w, uint32_t src, int id, float x, float y, float depth, float sx = 1, float sy = 1, int rot = 0,
         uint32_t flags = 0, const float* tint = nullptr) {
    SpriteDef d;
    d.id = id;
    d.x = x;
    d.y = y;
    d.depth = depth;
    d.sx = sx;
    d.sy = sy;
    d.rot = rot;
    d.flags = flags;
    d.source = src;
    if (tint) {
        d.tinted = true;
        d.tint[0] = tint[0];
        d.tint[1] = tint[1];
        d.tint[2] = tint[2];
    }
    w.hudSprites.push_back(d);
}

// 0x0041B020(id, x, y, depth, sx, sy, alpha, flags)
void SprA(World::Impl& w, int id, float x, float y, float depth, float alpha, uint32_t flags) {
    SpriteDef d;
    d.id = id;
    d.x = x;
    d.y = y;
    d.depth = depth;
    d.alpha = alpha;
    d.flags = flags;
    d.source = 0x0041B020;
    w.hudSprites.push_back(d);
}

struct TrainTask : EnemyTask {  // 0x6D object (0x13F4)
    int16_t step = 0;     // +0x1310
    int f1320 = 0;        // +0x1320 (select: a move was made; caption 0 shows the lesson name)
    int caption = 0;      // +0x1324 caption mode of 0x004986F0 (0 select, 1 instructions, 2 passed, 3 failed)
    int wait = 0;         // +0x132C (the result screen takes a button)
    int count = 0;        // +0x1330 (select: last open lesson; lesson: frame counter)
    int idle = 0;         // +0x133C (select: frames before the first move)
    int sel = 0;          // +0x1350
    int diff = 0;         // +0x1354 (select: difficulty; failed: 0 retry / 1 back)
    int prevSel = 0;      // +0x136C
    uint32_t endScript = 0;  // +0x1390 params: the script after the lesson (u32) and its variant (s16 +4)
    int endVariant = 0;
    MenuPointerFocus pointer[2];
};

// pad masks per player 0x009C7000 (tables 0x00597B74..)
uint32_t Mask(World::Impl& w, uint32_t table) { return w.exe.U32(table + static_cast<uint32_t>(w.playersInGame) * 4); }
constexpr uint32_t kUp = 0x00597B74, kDown = 0x00597B7C, kLeft = 0x00597B84, kRight = 0x00597B8C;
constexpr uint32_t kTrig = 0x00597B94, kStart = 0x00597B9C, kBack = 0x00597BA4;

int8_t& Grade(ModesState& m, int lesson) { return m.trainGrades[lesson < 0 || lesson > 9 ? 0 : lesson]; }

// Names follow the original select/instruction captions at 0x00570AE8 / 0x00570B60.
// Keep the lesson indices unchanged: they also identify the saved grades and scene scripts.
constexpr const char* kChallengeNames[] = {
    "CIVILIAN RESCUE", "30-SHOT CLEAR", "BARREL BLITZ", "ONE-SHOT FROG", "RAPID-FIRE BOSS",
    "ENEMY SWEEP", "DISTANT TARGETS", "CAR DEFENSE", "FRIEND OR FOE", "COIN JUGGLE"
};

void CenteredTitle(World::Impl& w, const char* text, float y, float scale) {
    const float x = 320.0f - static_cast<float>(std::strlen(text)) * MenuTextAdvance(w.exe, scale) * 0.5f;
    const size_t first = w.hudSprites.size();
    MenuTextScaled(w.exe, x, y, text, 1, scale, w.hudSprites);
    MenuLoadSprites(w, first);
}

void ChallengeName(World::Impl& w, int lesson) {
    if (lesson >= 0 && lesson < 10) CenteredTitle(w, kChallengeNames[lesson], 78.0f, 0.85f);
}

void SetFlag(World::Impl& w, int i, int v) { w.runner->flags[i & 0xFF] = static_cast<uint8_t>(v); }
int Kill2234(World::Impl& w) { return static_cast<int16_t>(w.GlobalInt(0x009A2234, 0) & 0xFFFF); }
void SetKill2234(World::Impl& w, int v) { w.runner->globals[0x009A2234] = static_cast<uint32_t>(static_cast<int16_t>(v)); }

// 0x00460770: rank init (0x005679F4[difficulty]), rank timer on
void RankInit(World::Impl& w) {
    w.rank = static_cast<int8_t>(w.exe.S16(0x005679F4 + static_cast<uint32_t>(w.cfg.difficulty)) & 0xFF);
    w.rankTimerOn = true;
    w.rankAdj = 0;
    w.rankTimer = 1;
}

// 0x00498650: a grade mark (model 0x1A4C), turning on the current entry
void Mark(World::Impl& w, int entry, int i, float x, int spin, float alpha) {
    const ExeImage& exe = w.exe;
    const float k = F32(exe, 0x0055CC58);
    Mat4 t = Mat4::Identity();
    mx::Translate(t, ((x - F32(exe, 0x0055CC54)) / w.focal) * k, (static_cast<float>(i * 0x20 - 2) / w.focal) * k, -36.0f);
    if (entry != w.modes->trainCur) spin = 0;
    mx::RotY(t, static_cast<int16_t>(spin));
    w.Draw(0x1A4C, t, 8, alpha, false, -1, true);  // 0x004185A0
}

// 0x00498490: one carousel entry (frame 0xAF1, lesson picture 0x00570A8C[k] tinted by its brightness, grade marks)
void Entry(World::Impl& w, TrainTask& b, int k, float x, float bright) {
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    const float tint[3] = { bright, bright, bright };
    const float fx = x + F32(exe, 0x0055CC50);
    w.LightColor(bright, bright, bright);  // 0x004AA0A0: the picture tint and the grade marks' light colour
    Spr(w, 0x00498490, 0xAF1, fx, 90.0f, 100.0f);
    if (b.count < k) return;
    Spr(w, 0x00498490, static_cast<int>(exe.U32(0x00570A8C + static_cast<uint32_t>(k) * 4)), fx, 90.0f, 90.0f, 1, 1, 0, 0x2000, tint);
    const int grade = Grade(m, k);
    if (grade < 0) return;
    // grade 5: the marks in the light 0x0041DC10(0x009A378C, 0x009A3558, 0x009A355C, 1, 0, 0), then 0x0041DCC0
    const float red[3] = { 1.0f, 0.0f, 0.0f };
    auto mark = [&](int i, int spin, float alpha) {
        if (grade == 5) w.LightCustom(w.env.lightIntensity, w.env.lightPitch, w.env.lightYaw, red);
        Mark(w, k, i, x, spin, alpha);
        if (grade == 5) w.LightScene();
    };
    if (k == m.trainCur) {
        m.trainSpin = static_cast<int16_t>(m.trainSpin + 0x1FE);
        for (int i = 0; i < grade + 1; ++i) {
            if (i < b.diff + 1) mark(i, m.trainSpin, 1.0f);
            else if (i < 5) mark(i, 0, 0.3f);
        }
    } else {
        for (int i = 0; i < grade + 1; ++i)
            if (i < 5) mark(i, w.modes->trainSpin, 1.0f);
    }
}

// 0x00498090: background, title, carousel (slides 10 frames on a change), cursor star, lesson name, arrows
void SelectDraw(World::Impl& w, TrainTask& b) {
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    int id = 0xADB;
    for (int y = 0; id < 0xAEF; y += 0x80)
        for (int x = 0; x < 0x280; x += 0x80) Spr(w, 0x00498090, id++, static_cast<float>(x), static_cast<float>(y), 200.0f);
    if (w.cfg.portFeatures) CenteredTitle(w, "CHALLENGE MODE", 30.0f, 1.25f);
    else Spr(w, 0x00498090, 0xB38, 320.0f, 54.0f, 1.0f, 1, 1, 0, 10);
    if (m.trainAnim == 1 || m.trainAnim == 2) {
        ++m.trainAnimCount;
        const float da = F32(exe, 0x0055CC44), dx = F32(exe, 0x0055CC40);
        for (int k = 0; k < 10; ++k) {
            ModesState::Slot& s = m.trainSlots[k];
            if (k == m.trainCur) s.a = s.a + da;
            if (k == m.trainPrev) s.a = s.a - da;
            if (m.trainAnim == 1) {
                s.x = s.x - dx;
                if (s.x < F32(exe, 0x0055CC3C)) s.x = s.x + F32(exe, 0x0055CC34);
            } else {
                s.x = s.x + dx;
                if (F32(exe, 0x0055CC38) < s.x) s.x = s.x - F32(exe, 0x0055CC34);
            }
        }
        if (m.trainAnimCount == 10) m.trainAnim = m.trainAnimCount = 0;
    }
    for (int k = 0; k < 10; ++k) Entry(w, b, m.trainSlots[k].id, m.trainSlots[k].x, m.trainSlots[k].a);
    for (int c = 0; c < 3; ++c) w.light.color[c] = w.env.lightColor[c];  // 0x004AA0A0(0x009A3780..) after the star
    {  // 0x004AA0A0(1, 1, 0.2): the cursor star pulses (x87 sin of 0x009A32A0 % 60, 24-bit rounding)
        const float arg = static_cast<float>(static_cast<double>(static_cast<int>(w.seedCounter % 0x3C) * 0x444) * F64(exe, 0x004C4370));
        const float s = static_cast<float>(std::sin(static_cast<double>(arg)));
        const float sc = static_cast<float>(s * F32(exe, 0x004C4C88)) + F32(exe, 0x004C4380);
        const float tint[3] = { 1.0f, 1.0f, Bits(0x3E4CCCCD) };
        Spr(w, 0x00498090, 0xAEF, 322.0f, 194.0f, Bits(0x42006666), sc, sc, 0, 0x200A, tint);
    }
    if (b.sel <= b.count) {
        if (w.cfg.portFeatures) ChallengeName(w, b.sel);
        else {
            Spr(w, 0x00498090, 0xAFE, 260.0f, 84.0f, 65.0f);
            Spr(w, 0x00498090, b.sel + 0xAB7, 365.0f, 84.0f, 65.0f);
        }
    }
    const uint32_t u = w.seedCounter % 0x3C;
    if (u < 0x2D) {
        const float alpha = u < 8 ? 1.0f : (F32(exe, 0x005308DC) - static_cast<float>(u)) * F32(exe, 0x005308D8);
        SprA(w, 0xAFF, 100.0f, 136.0f, 2.0f, alpha, 0);
        SprA(w, 0xAFF, 542.0f, 136.0f, 2.0f, alpha, 0x17);
    }
}

// 0x004986F0: the caption box by mode +0x1324
void Caption(World::Impl& w, TrainTask& b) {
    const ExeImage& exe = w.exe;
    ModesState& m = *w.modes;
    const uint32_t src = 0x004986F0;
    const float z = Bits(0x41F7D70A), z2 = Bits(0x41F80000);
    switch (b.caption) {
    case 0:
        if (b.f1320 != 0) {
            if (b.count < b.sel) {
                Spr(w, src, 0xB1C, 222.0f, 360.0f, z, 1, 1, 0, 9);
            } else {
                const uint32_t e = 0x00570AE8 + static_cast<uint32_t>(b.sel) * 12;
                Spr(w, src, exe.S16(e), F32(exe, e + 4), F32(exe, e + 8), z, 1, 1, 0, 9);
            }
            Spr(w, src, 0xB28, 144.0f, 365.0f, z2, 1, 1, 0, 9);
            return;
        }
        Spr(w, src, 0xB19, 232.0f, 397.0f, z, 1, 1, 0, 9);
        Spr(w, src, 0xB29, 144.0f, 432.0f, z2, 1, 1, 0, 9);
        if (0xB4 < b.idle++) b.f1320 = 1;
        break;
    case 1: {
        const uint32_t e = 0x00570B60 + static_cast<uint32_t>(b.sel) * 12;
        Spr(w, src, exe.S16(e), F32(exe, e + 4), F32(exe, e + 8), z, 1, 1, 0, 9);
        Spr(w, src, 0xB29, 150.0f, 284.0f, z2, 1, 1, 0, 0x29);
        const uint32_t d = static_cast<uint32_t>(m.lessonDiff) * 2;
        if (b.sel == 3) {
            Spr(w, src, exe.S16(0x00570AB4 + d) / 0x3C + 0xB1E, 303.0f, 347.0f, z);
            return;
        }
        int v;
        float x, y;
        if (b.sel == 6) {
            v = exe.S16(0x00570AC0 + d);
            Spr(w, src, v / 600 + 0xB1E, 290.0f, 331.0f, z);
            x = 303.0f;
            y = 331.0f;
        } else if (b.sel == 9) {
            v = exe.S16(0x00570AD8 + d);
            Spr(w, src, v / 600 + 0xB1E, 381.0f, 324.0f, z);
            x = 394.0f;
            y = 324.0f;
        } else {
            return;
        }
        Spr(w, src, (v / 0x3C) % 10 + 0xB1E, x, y, z);
        return;
    }
    case 2:
        if (Grade(m, m.lesson) == 5) Spr(w, src, 0xB1D, 278.0f, 380.0f, z, 1, 1, 0, 9);
        else Spr(w, src, 0xB06, 228.0f, 342.0f, z, 1, 1, 0, 9);
        Spr(w, src, 0xB29, 150.0f, 284.0f, z2, 1, 1, 0, 0x29);
        return;
    case 3: {
        Spr(w, src, 0xB05, 256.0f, 328.0f, z, 1, 1, 0, 9);
        const float on[3] = { 1.0f, Bits(0x3F19999A), Bits(0x3E4CCCCD) }, off[3] = { Bits(0x3ECCCCCD), Bits(0x3ECCCCCD), Bits(0x3ECCCCCD) };
        const float son = 1.0f, soff = Bits(0x3F333333);
        const bool yes = b.diff == 0;
        Spr(w, src, 0xB01, 342.0f, 382.0f, z, yes ? son : soff, yes ? son : soff, 0, 0x200A, yes ? on : off);
        Spr(w, src, 0xB2E, 510.0f, 382.0f, z, yes ? soff : son, yes ? soff : son, 0, 0x200A, yes ? off : on);
        Spr(w, src, 0xB29, 150.0f, 284.0f, z2, 1, 1, 0, 0x29);
        return;
    }
    default: break;
    }
}

// lesson picture and name (0x00497360 / 0x00497440 / 0x00497C30 / 0x00497DB0)
void LessonCard(World::Impl& w, TrainTask& b, uint32_t src, bool name) {
    Spr(w, src, 0xAF1, 216.0f, 90.0f, 50.0f);
    Spr(w, src, static_cast<int>(w.exe.U32(0x00570A8C + static_cast<uint32_t>(b.sel) * 4)), 216.0f, 90.0f, 40.0f);
    if (w.cfg.portFeatures) {
        ChallengeName(w, b.sel);
        return;
    }
    if (!name) return;
    Spr(w, src, 0xAFE, 260.0f, 80.0f, 2.0f);
    Spr(w, src, b.sel + 0xAB7, 365.0f, 80.0f, 2.0f);
}

// 0x00498C10(limit, counter): the time left (digits 0xACF.. tinted green -> yellow -> red, pulsing in the last 3 s)
void TimeLeft(World::Impl& w, int limit, int counter) {
    const ExeImage& exe = w.exe;
    const int d = limit - counter;
    if (d == 0) w.Sound(0x2000003F);
    else if (d == 0x3C || d == 0x78 || d == 0xB4) w.Sound(0x1800A9);
    Spr(w, 0x00498C10, 0xB00, 344.0f, 32.0f, 1.0f);
    float s = 1.0f;
    if (d < 0xB5) s = static_cast<float>(d % 0x3C) * F32(exe, 0x0055CB80) + F32(exe, 0x004C4380);
    const int secs = (d + 0x3B) / 0x3C;
    float g, bl;
    if (d < 0x3C) g = Bits(0x3E4CCCCD), bl = Bits(0x3E4CCCCD);
    else if (d < 0x78) g = Bits(0x3F333333), bl = Bits(0x3E4CCCCD);
    else g = 1.0f, bl = d < 0xB4 ? Bits(0x3E4CCCCD) : 1.0f;
    const float tint[3] = { 1.0f, g, bl };
    Spr(w, 0x00498C10, secs / 10 + 0xACF, 327.0f, 64.0f, 1.0f, s, s, 0, 0x200B, tint);
    Spr(w, 0x00498C10, secs % 10 + 0xACF, 320.0f, 64.0f, 1.0f, s, s, 0, 0x2009, tint);
}

// 0x00498E60: the game after a lesson choice (pause allowed again, rank, players' counters)
void LessonSetup(World::Impl& w) {
    w.flags5900 = (w.flags5900 & ~0x38u) | 2;
    w.attackers = 0;
    RankInit(w);
    w.rank += w.players * 4;
    for (auto& P : w.player) {
        P.streak = 0;
        P.shots = 0;
        P.hits = 0;
    }
    // 0x0041D440, 0x00412FD0, flush 0x0041D970
    memset(w.komono40Intact, 1, sizeof(w.komono40Intact));  // 0x00463680
    // 0x00405F90: 0x009A1A14 / 0x009A34D8 (op 0x10 / 0x11 list pointers), 0x009C8A80, 0x009C8E14, 0x009CA0C0[8] /
    // 0x009C7040[8] = -1, two bytes of 0x009C701C
    w.runner->globals[0x009A1A14] = 0;
    w.runner->globals[0x009A34D8] = 0;
}

// 0x00498ED0: the lesson's grade (+1 when the difficulty reached it), three items when all ten reach 5, the save flags
void LessonGrade(World::Impl& w) {
    ModesState& m = *w.modes;
    int changed = 0;
    m.trainSave = false;
    auto fives = [&] {
        int n = 0;
        for (int8_t g : m.trainGrades) n += g == 5;
        return n;
    };
    const int before = fives();
    if (!(m.lessonDiff < Grade(m, m.lesson)) && m.lessonDiff < 5) {
        changed = 1;
        ++Grade(m, m.lesson);
    }
    const int after = fives();
    if (after != before && after == 10) {
        for (int i : { 6, 10, 0x1E })
            if (m.items[i] < 99) ++m.items[i];
        ++changed;
    }
    int graded = 0;
    for (int8_t g : m.trainGrades) graded += 0 < g;
    if (graded == 10) {
        m.save.SetU8(SaveData::kFlags, static_cast<uint8_t>(m.save.U8(SaveData::kFlags) | 2));
        ++changed;
    }
    if (changed) m.trainSave = true;
}

void AgentMotionEnd(World::Impl& w, TrainTask& b, int from, int to) {
    if (b.anim.motion[0] == from && b.anim.tick[0] == w.MotionTicks(from) - 1) w.AnimBlendTo(b, to, 0, 2);
}

// 0x00496DE0: the lesson select
void SelectFrame(World::Impl& w, TrainTask& b) {
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    if (b.step == 0) {
        w.Sound(0x10000027);
        int last = 6;
        while (last >= 0 && m.trainGrades[last] == 0) --last;
        b.count = last + 1;
        int n = 0;
        for (int i = 0; i < 8; ++i) n += 1 < m.trainGrades[i];
        if (7 < n) b.count = 8;
        n = 0;
        for (int i = 0; i < 9; ++i) n += 2 < m.trainGrades[i];
        if (8 < n) b.count = 9;
        b.step = 2;
    } else if (b.step > 0 && b.step < 5) {
        ++b.step;
    }
    SelectPointer(w, Mask(w, kTrig), true, b.step > 4 && m.trainAnim == 0);
    // L / R held 60 frames: auto repeat (0x009C9020 held)
    const uint32_t mL = Mask(w, kLeft), mR = Mask(w, kRight);
    if ((w.held & mL) == 0 && (w.held & mR) == 0) {
        m.trainRepeat = 0;
        m.trainRepeatDir = 0;
    } else if (++m.trainRepeat == 0x3C) {
        m.trainRepeat = 0;
        if (w.held & mR) m.trainRepeatDir = 1;
        else if (w.held & mL) m.trainRepeatDir = 2;
    }
    auto setDiff = [&] {
        b.diff = Grade(m, b.sel);
        if (b.diff == 5) b.diff = 4;
    };
    if (m.trainAnim == 0) {
        bool moved = false;
        if ((w.pressed & mR) || m.trainRepeatDir == 1) {
            w.Sound(0xA9);
            if (9 < ++b.sel) b.sel = 0;
            setDiff();
            m.trainAnim = 1;
            m.trainPrev = m.trainCur;
            if (9 < ++m.trainCur) m.trainCur = 0;
            moved = true;
        } else if ((w.pressed & mL) || m.trainRepeatDir == 2) {
            w.Sound(0xA9);
            if (--b.sel < 0) b.sel = 9;
            setDiff();
            m.trainPrev = m.trainCur;
            if (--m.trainCur < 0) m.trainCur = 9;
            m.trainAnim = 2;
            moved = true;
        }
        if (moved) {
            m.trainAnimCount = 0;
            m.trainSpin = 0;
        }
    }
    if (b.sel <= b.count) {  // difficulty up / down within the lesson's grade
        if (w.pressed & Mask(w, kUp)) {
            if (b.diff < Grade(m, b.sel) && b.diff < 4) {
                w.Sound(0x1000A9);
                ++b.diff;
            }
        } else if ((w.pressed & Mask(w, kDown)) && 0 < b.diff) {
            w.Sound(0xF00A9);
            --b.diff;
        }
    }
    // the agent: 0x31A (walk in) -> 0x31C, 0x320 (shake head at a locked entry) -> 0x321; a change restarts it
    if (b.sel == b.prevSel) {
        if (b.anim.motion[0] == 0x31A && b.anim.tick[0] == w.MotionTicks(0x31A) - 1) w.AnimBlendTo(b, 0x31C, 0, 2);
        else if (b.anim.motion[0] == 0x320 && b.anim.tick[0] == w.MotionTicks(0x320) - 1) w.AnimBlendTo(b, 0x321, 0, 2);
    } else {
        w.AnimSetMotion(b, b.count < b.sel ? 0x320 : 0x31A);
        b.f1320 = 1;
    }
    SelectDraw(w, b);
    Caption(w, b);
    SelectPointerDraw(w, Mask(w, kTrig), b.diff);
    if ((w.pressed & (Mask(w, kTrig) | Mask(w, kStart))) && 4 < b.step) {
        if (b.sel <= b.count) {
            w.Sound(0x80000000);
            w.Sound(0x121A9);
            const int scene = exe.S16(0x00570A78 + static_cast<uint32_t>(b.sel) * 2);
            m.a2bbc = scene;
            w.runner->globals[0x009A2BBC] = static_cast<uint32_t>(scene);
            m.lesson = b.sel;
            m.lessonDiff = b.diff;
            w.runner->cfg.playerVariant = b.diff;  // 0x009C9118 (ops 0x49..0x4B)
            m.trainSave = false;
            // textures 0x16E / 0x16F / 0x173 / 0x171, pol 0xBE, model 0x1A4C released
            LessonSetup(w);  // 0x00498E60
            w.runner->camera.args[0] = 1;  // 0x009A6184: the select's held camera preset ends
            w.runner->yield = false;       // 0x009C8EA0
            w.runner->scene = scene;       // 0x009A2BC0 + Evt_GetScript 0x0045EB90
            w.runner->script = w.mem.Script(w.runner->mode, scene, w.runner->variant);
            w.ts.KillCurrent();
            return;
        }
        w.Sound(0x200A9);
    }
    if ((w.pressed & Mask(w, kBack)) && 4 < b.step) {  // back to the mode select
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

void TaskTrain(Task& tk) {
    World::Impl& w = *CurrentWorld();
    TrainTask& b = tk.As<TrainTask>();
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    switch (b.fn) {
    case 0x00496DE0:
        SelectFrame(w, b);
        return;
    case 0x00497360:  // lesson start
        w.padClear = 1;  // 0x009A34DC
        w.canShoot = 0;
        for (auto& P : w.player) {  // 0x0040D220
            P.gun.x = P.gun.y = 0;
            P.gun.fx = P.gun.fy = 0;
        }
        LessonCard(w, b, 0x00497360, true);
        b.fn = 0x00497440;
        return;
    case 0x00497440: {  // instructions: trigger / start = GO, back = the lesson select
        if (w.cfg.portFeatures) {
            for (int p = 0; p < 2; ++p) {
                const auto& in = w.input[p];
                if (in.kind != 1 || !in.fired || !(Mask(w, kTrig) & (0xFFFFu << (p * 16)))) continue;
                w.pressed &= ~(12u << (p * 16));
                if (!MenuAim(in)) continue;
                if (kSelectBack.Hit(in.x, in.y)) w.pressed |= 2u << (p * 16);
                else if (kSelectStart.Hit(in.x, in.y)) w.pressed |= 4u << (p * 16);
            }
        }
        AgentMotionEnd(w, b, 0x31A, 0x31C);
        LessonCard(w, b, 0x00497440, true);
        Caption(w, b);
        SelectPointerDraw(w, Mask(w, kTrig));
        if ((w.pressed & (Mask(w, kTrig) | Mask(w, kStart))) == 0) {
            if (w.pressed & Mask(w, kBack)) {
                w.Sound(0x500A9);
                w.nextEventMode = 6;  // Evt_LoadTable, scene 0, variant 0, sub-state 1 (0x0049DFD0)
                w.runner->scene = 0;
                w.runner->variant = 0;
                w.subState = 1;
            }
            SelectAgentDraw(w, b);
            return;
        }
        w.Sound(0x80000000);
        const int scene = w.runner->scene;
        for (auto& P : w.player) {
            if (P.state != 5) continue;
            if (scene == 3) P.lives = P.livesShown = 2;
            else if (scene == 9) P.lives = P.livesShown = 1;
        }
        SetFlag(w, 0xF0, 1);  // 0x009C72F0
        w.flags5900 &= ~0x10u;
        b.count = 0;
        b.fn = 0x00497640;
        return;
    }
    case 0x00497640:  // READY (0xB2C) / GO (0xB2A)
        ++b.count;
        if (b.count == 10) w.Sound(0x2000003D);
        if (10 < b.count && b.count < 0x5A) Spr(w, 0x00497640, 0xB2C, 346.0f, 210.0f, 1.0f, 1, 1, 0, 10);
        if (b.count == 100) w.Sound(0x2000003C);
        if (100 < b.count && b.count < 0xA0) Spr(w, 0x00497640, 0xB2A, 346.0f, 210.0f, 1.0f, 1, 1, 0, 10);
        if (b.count == 0xA0) {
            b.count = 0;
            w.flags5900 &= ~0x10u;
            RankInit(w);  // 0x00460770
            SetKill2234(w, -1);
            w.runner->globals[0x009A244A] = 0;  // 0x009A244A (kill counter, counted by actor 0x20's death in world_actors.cpp)
            SetFlag(w, 0xF1, 1);
            w.rank += w.players * 4;
            m.timerStart = w.frame;  // 0x0049DF00 (GetTickCount; here game frames)
            m.timerBase = 0;
            b.fn = 0x00497760;
        }
        return;
    case 0x00497760: {  // the lesson runs until its condition
        b.count = b.sel == 7 ? b.count + 1 : m.Clock(w.frame);  // 0x0049DF20
        bool other = w.runner->flags[0xF2] != 0;
        bool end = false;
        const uint32_t d = static_cast<uint32_t>(m.lessonDiff) * 2;
        switch (b.sel) {
        case 0: case 5: case 8:
            end = Kill2234(w) == 1 || other;
            break;
        case 1: {
            int shots = 0;
            for (auto& P : w.player) shots += P.shots;
            Spr(w, 0x00497760, (0x1E - shots) / 10 + 0xACF, 292.0f, 32.0f, 1.0f);
            Spr(w, 0x00497760, (0x1E - shots) % 10 + 0xACF, 320.0f, 32.0f, 1.0f);
            Spr(w, 0x00497760, 0xB2B, 344.0f, 32.0f, 1.0f);
            if (0x1D < shots) w.canShoot = 0;
            end = Kill2234(w) == 1 || other;
            break;
        }
        case 2:
            TimeLeft(w, 600, b.count);
            end = !(b.count < 600);
            break;
        case 3: {
            const int lim = exe.S16(0x00570AB4 + d);
            TimeLeft(w, lim, b.count);
            for (auto& P : w.player)
                if (0 < P.shots) other = true;
            end = b.count == lim || other;
            break;
        }
        case 4:
            end = Kill2234(w) == 1 || other;
            break;
        case 6: {
            const int lim = exe.S16(0x00570AC0 + d);
            TimeLeft(w, lim, b.count);
            end = !(b.count < lim);
            break;
        }
        case 7:
            other = Kill2234(w) == 1 || other;
            end = b.count == exe.S16(0x00570ACC + d) || other;
            break;
        case 9:
            end = Kill2234(w) == 1 || other;
            break;
        default:
            end = other;
            break;
        }
        if (!end) return;
        if (w.runner->flags[0xF2] == 0) SetFlag(w, 0xF2, 1);
        CameraDirector& c = w.runner->camera;
        if (c.endTick != c.playTick) c.endTick = c.playTick;  // 0x009A6148
        w.Sound(0x80000000);
        c.SetMode(1, 3);  // 0x00403BD0
        c.writeIdx = c.readIdx = 0;  // 0x009CA108 / 0x009A34D0
        c.cmd = CameraDirector::Cmd::Nop;  // 0x009A610C = 0x0041EBB0
        c.pending = 0;
        c.cmdState = 1;
        c.forceState6 = true;  // 0x009CA094
        w.runner->yield = false;
        w.runner->variant = b.endVariant;  // 0x009A2BB0
        w.runner->script = b.endScript;    // 0x009C7108
        b.count = 0;
        b.fn = 0x00497A70;
        return;
    }
    case 0x00497A70: {  // grading after 5 frames
        if (++b.count != 5) return;
        int v = Kill2234(w);
        switch (b.sel) {
        case 0: case 1: case 5: case 8: case 9:
            if (v != 1) v = 0;
            break;
        case 2: {
            int hits = 0;
            for (auto& P : w.player) hits += P.hits;
            v = hits < 0x10;
            break;
        }
        case 3: {
            int n = 0;
            for (auto& P : w.player) n += 0 < P.hits;
            v = n == 0;
            break;
        }
        case 4:
            if (v == 1) w.bossBar = -1.0f;  // 0x009C8E10
            else v = 0;
            break;
        case 6:
            v = w.GlobalInt(0x009A244A, 0) < 8;
            break;
        case 7:
            if (v != 1) v = w.GlobalInt(0x009A244A, 0) < 8;
            break;
        default: break;
        }
        SetKill2234(w, v);
        b.wait = 1;
        b.count = 0;
        w.flags5900 |= 0x10;
        if (v == 1) {  // failed: retry / back
            b.diff = 0;
            b.caption = 3;
            w.AnimSetMotion(b, 0x320);
            b.fn = 0x00497DB0;
            return;
        }
        w.Sound(0x10000025);
        LessonGrade(w);  // 0x00498ED0
        b.caption = 2;
        w.AnimSetMotion(b, 0x31E);
        b.anim.nodes[5].model = 0x1590;  // +0x4DC
        b.fn = 0x00497C30;
        return;
    }
    case 0x00497C30:    // passed
    case 0x00497DB0: {  // failed
        const bool passed = b.fn == 0x00497C30;
        if (0x19 < ++b.count) {
            Spr(w, b.fn, passed ? 0xB2D : 0xB02, 320.0f, 214.0f, Bits(0x3F8CCCCD), 1, 1, 0x600, 10);
            LessonCard(w, b, b.fn, false);
        }
        if (static_cast<double>(b.count) == F64(exe, 0x00570BD8)) w.Sound(passed ? 0x2000003Eu : 0x2000003Bu);
        if (b.count <= 0x3C) return;
        if (passed) {
            Caption(w, b);
            AgentMotionEnd(w, b, 0x31E, 0x31F);
            if (b.wait == 1 && (w.pressed & (Mask(w, kTrig) | Mask(w, kStart) | Mask(w, kBack)))) {
                b.wait = 0;
                m.a2bbc = 0;
                w.runner->globals[0x009A2BBC] = 0;
                SetFlag(w, 0xF4, 1);
                if (m.trainSave) m.Store(w);  // Game_Save 0x004011F0
            }
        } else {
            AgentMotionEnd(w, b, 0x320, 0x321);
            if (w.cfg.portFeatures && b.wait == 1) {
                for (int p = 0; p < 2; ++p) {
                    if (!(Mask(w, kTrig) & (0xFFFFu << (p * 16)))) continue;
                    const auto& in = w.input[p];
                    if (in.kind == 1 && in.fired) w.pressed &= ~(12u << (p * 16));
                    const int choice = MenuRect{270, 357, 414, 407}.Hit(in.x, in.y) ? 0 :
                                       MenuRect{438, 357, 598, 407}.Hit(in.x, in.y) ? 1 : -1;
                    if (b.pointer[p].Update(MenuAim(in), float(in.x), float(in.y), in.fired) && choice >= 0) b.diff = choice;
                    if (MenuAim(in) && in.fired && choice >= 0) w.pressed |= 4u << (p * 16);
                }
                MenuSights(w, Mask(w, kTrig));
            }
            if (w.pressed & Mask(w, kLeft)) b.diff = 0;
            else if (w.pressed & Mask(w, kRight)) b.diff = 1;
            Caption(w, b);
            if (b.wait == 1) {
                if (w.pressed & (Mask(w, kTrig) | Mask(w, kStart))) {
                    if (b.diff == 1) {
                        m.a2bbc = 0;
                        w.runner->globals[0x009A2BBC] = 0;
                    }
                    b.wait = 0;
                    SetFlag(w, 0xF4, 1);
                }
                if (w.pressed & Mask(w, kBack)) {
                    b.wait = 0;
                    m.a2bbc = 0;
                    w.runner->globals[0x009A2BBC] = 0;
                    SetFlag(w, 0xF4, 1);
                }
            }
        }
        SelectAgentDraw(w, b);
        return;
    }
    default:
        return;
    }
}

// ---- lesson actors 0x1A (0x00498FF0 -> 0x004991B0) / 0x1B (0x00499420 -> 0x00499530) ----

// the object just created by World::Impl::Spawn (the newest live task)
ActorTask* Newest(World::Impl& w) {
    Task* best = nullptr;
    for (Task* t : w.ts.Live())
        if (t->data && (!best || best->serial < t->serial)) best = t;
    return best ? static_cast<ActorTask*>(static_cast<WTask*>(best->data.get())) : nullptr;
}

// 0x00408A20 (op 0x0B style) / 0x00408BC0 (op 0x0C style) of a record, the created object
EnemyTask* CreateFromRecord(World::Impl& w, uint32_t record, bool civStyle) {
    const uint32_t serial = w.ts.NextSerial();
    w.Spawn(civStyle ? 0x0C : 0x0B, record);
    if (w.ts.NextSerial() == serial) return nullptr;
    return dynamic_cast<EnemyTask*>(Newest(w));
}

bool Alive(World::Impl& w, const ActorTask* a) {
    if (!a) return false;
    for (Task* t : w.ts.Live())
        if (t->data.get() == a) return true;
    return false;
}

uint32_t Word(World::Impl& w, uint32_t a) {
    uint32_t v = 0;
    w.mem.Read(a, &v);
    return v;
}

// 0x0045BC10(rot, a, b): b - a turned by -rot (Y, Z, X) lies in front (z > 0)
bool InFront(const int rot[3], const float a[3], const float b[3]) {
    Mat4 m = Mat4::Identity();
    mx::RotY(m, -rot[1]);
    mx::RotZ(m, -rot[2]);
    mx::RotX(m, -rot[0]);
    const float d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    float o[3];
    mx::Transform(m, d, o);
    return 0.0f < o[2];  // _DAT_004C436C
}

struct Lesson1A : EnemyTask {  // the 0x38-byte block +0x1310
    float point[3] = {};       // +0x00
    int count = 0;             // +0x0C spawned
    int civRun = 0;            // +0x10 civilians in a row
    int left = 0;              // +0x14 still to come
    int civLeft = 0;           // +0x18
    int enemyLeft = 0;         // +0x1C
    uint32_t listA = 0, listB = 0;  // +0x20 / +0x24 {record, resource list} pairs
    std::vector<int> orderA, orderB;  // +0x28 / +0x2C
    int state = 0;             // +0x30
    int timer = 0;             // +0x34
};

void TaskLesson1A(Task& t) {  // 0x004991B0: one enemy or civilian at a time, in a shuffled order
    World::Impl& w = *CurrentWorld();
    Lesson1A& a = t.As<Lesson1A>();
    const ExeImage& exe = w.exe;
    auto spawnEnemy = [&] {  // 0x004993F0
        --a.enemyLeft;
        if (0 <= a.enemyLeft && a.enemyLeft < static_cast<int>(a.orderA.size()))
            CreateFromRecord(w, Word(w, a.listA + static_cast<uint32_t>(a.orderA[a.enemyLeft]) * 8), false);
        a.civRun = 0;
    };
    auto spawnCiv = [&] {  // 0x004993C0
        --a.civLeft;
        if (0 <= a.civLeft && a.civLeft < static_cast<int>(a.orderB.size()))
            CreateFromRecord(w, Word(w, a.listB + static_cast<uint32_t>(a.orderB[a.civLeft]) * 8), true);
        ++a.civRun;
    };
    auto release = [&](uint32_t list) {  // 0x0041D690 for each pol of the list (ring type 4)
        for (uint32_t v; (v = Word(w, list)) != 0xFFFFFFFFu; list += 4) w.resQueue.push_back({ 0x53, v });
    };
    switch (a.state) {
    case 0:
        if (a.timer-- != 0) break;
        if (a.count == 0 || a.civLeft == 0) {
            spawnEnemy();
        } else if ((a.civLeft < 3 || a.civLeft + 1 < a.left) && 0 < a.enemyLeft) {
            if (1 < a.civRun || w.Rand(0x0049920D) % 3 != 0) spawnEnemy();
            else spawnCiv();
        } else {
            spawnCiv();
        }
        --a.left;
        ++a.count;
        a.state = 1;
        break;
    case 1:
        if (w.actor8E4C) a.state = 2;
        break;
    case 2: {
        if (w.liveEnemies == 0 && w.countA0E8 == 0) {
            if (a.left != 0) {
                a.state = 0;
                a.timer = static_cast<uint8_t>(exe.U32(0x00597BD4 + static_cast<uint32_t>(w.modes->lessonDiff)) & 0xFF);
            } else {
                a.state = 3;
            }
            break;
        }
        ActorTask* x = w.actor8E4C;
        if (!Alive(w, x)) break;  // (the original reads the last updated actor's block)
        const EnemyTask* xe = dynamic_cast<const EnemyTask*>(x);
        if (!xe) break;
        if (!(x->flags34 & 0x10000)) {
            if (InFront(a.rot, xe->pos, a.pos)) {
                if (w.liveEnemies == 0) {
                    release(Word(w, a.listB + 4 + static_cast<uint32_t>(a.civLeft) * 8));
                } else {
                    release(Word(w, a.listA + 4 + static_cast<uint32_t>(a.enemyLeft) * 8));
                    w.runner->globals[0x009A2234] = 1;  // an enemy got through: the lesson is failed
                    a.state = 4;
                }
            }
        } else if (InFront(a.rot, xe->pos, a.point)) {
            x->flags34 &= ~0x10000u;
        }
        break;
    }
    case 3:
        if (w.attackers == 0 && w.countA0E8 == 0) {
            w.runner->flags[0x02] = 1;  // 0x009C7202
            w.ts.KillCurrent();
        }
        break;
    default:
        w.ts.KillCurrent();
    }
    w.actor8E4C = nullptr;
}

struct Lesson1B : EnemyTask {
    int16_t until = 0;  // +0x11C camera tick
};

// 0x004995B0: the time left (minutes digit, the mark, two digits of hundredths)
void TimeDigits(World::Impl& w, int t) {
    const int cs = ((t % 0x3C) * 100) / 0x3C;
    Spr(w, 0x004995B0, (t / 0x3C) % 10 + 0xACF, 280.0f, 40.0f, Bits(0x3F4CCCCD), 1, 1, 0, 10);
    Spr(w, 0x004995B0, 0x643, 302.0f, 58.0f, Bits(0x3F4CCCCD), 1, Bits(0x3F19999A), 0, 10);
    Spr(w, 0x004995B0, cs / 10 + 0xACF, 324.0f, 40.0f, Bits(0x3F4CCCCD), 1, 1, 0, 10);
    Spr(w, 0x004995B0, cs % 10 + 0xACF, 352.0f, 40.0f, Bits(0x3F4CCCCD), 1, 1, 0, 10);
}

void TaskLesson1B(Task& t) {  // 0x00499530: the time left until camera tick +0x11C; enemies still up then: failed
    World::Impl& w = *CurrentWorld();
    Lesson1B& a = t.As<Lesson1B>();
    const int fail = static_cast<int16_t>(w.GlobalInt(0x009A2234, 0) & 0xFFFF);
    if (w.runner->camera.tick < a.until) {
        if (fail != 1) {
            const int limit = static_cast<uint8_t>(w.exe.U32(0x00597BDC + static_cast<uint32_t>(w.modes->lessonDiff)) & 0xFF);
            int v = w.runner->waitCounter;  // 0x009C8904
            if (v < 0) v = limit;
            else if (limit < v) w.runner->waitCounter = v = limit;
            TimeDigits(w, v);
            return;
        }
    } else if (0 < w.liveEnemies) {
        w.runner->globals[0x009A2234] = 1;
    }
    w.ts.KillCurrent();
}

}  // namespace

// actor types 0x1A / 0x1B (trnevtbl): the lesson's spawners
bool SpawnLessonActor(World::Impl& w, Task& t, SpawnTask& s) {
    const SpawnRecord& r = s.rec;
    const uint32_t params = r.addr + 0x24;  // +0x130C (op 0x0C) / +0x1390 (op 0x0B): the record's parameter block
    if (r.type == 0x1A) {  // 0x00498FF0
        auto d = std::make_unique<Lesson1A>();
        Lesson1A& a = *d;
        memcpy(a.pos, r.pos, 12);
        memcpy(a.rot, r.rot, 12);
        a.listA = params;
        int nA = 0;
        while (Word(w, a.listA + static_cast<uint32_t>(nA) * 8) != 0xFFFFFFFFu && nA < 256) ++nA;
        a.listB = a.listA + 8 + static_cast<uint32_t>(nA) * 8;
        int nB = 0;
        while (Word(w, a.listB + static_cast<uint32_t>(nB) * 8) != 0xFFFFFFFFu && nB < 256) ++nB;
        a.enemyLeft = nA;
        a.civLeft = nB;
        a.left = nA + nB;
        const uint32_t pt = a.listB + 8 + static_cast<uint32_t>(nB) * 8;
        for (int k = 0; k < 3; ++k) {
            const uint32_t v = Word(w, pt + 4 * static_cast<uint32_t>(k));
            memcpy(&a.point[k], &v, 4);
        }
        a.orderA.resize(static_cast<size_t>(nA));
        for (int i = 0; i < nA; ++i) a.orderA[i] = i;
        if (1 < nA)
            for (int i = 1; i < nA; ++i) {
                const int k = w.Rand(0x004990C0) % (nA - 1);
                std::swap(a.orderA[i - 1], a.orderA[k]);
            }
        a.orderB.resize(static_cast<size_t>(nB));
        for (int i = 0; i < nB; ++i) a.orderB[i] = i;
        if (1 < nB)
            for (int i = 1; i <= nB; ++i) {
                const int k = w.Rand(0x00499120) % nB;
                std::swap(a.orderB[i - 1], a.orderB[k]);
            }
        // the pol files of the enemies' resource lists (0x0041D650, ring type 3), all but the first one in the order
        for (int i = nA - 2; i >= 0; --i)
            for (uint32_t l = Word(w, a.listA + static_cast<uint32_t>(a.orderA[i]) * 8 + 4), v;
                 (v = Word(w, l)) != 0xFFFFFFFFu; l += 4)
                w.resQueue.push_back({ 0x52, v });
        a.state = 0;
        a.timer = 0;
        w.rank = static_cast<uint8_t>(w.exe.U32(0x00597BCC + static_cast<uint32_t>(w.modes->lessonDiff)) & 0xFF);  // 0x009C8E96
        w.actor8E4C = nullptr;
        d->fn = 0x004991B0;
        t.data = std::move(d);
        t.fn = TaskLesson1A;
        return true;
    }
    if (r.type == 0x1B) {  // 0x00499420
        auto d = std::make_unique<Lesson1B>();
        Lesson1B& a = *d;
        a.until = static_cast<int16_t>(r.state);
        d->fn = 0x00499530;
        t.data = std::move(d);
        t.fn = TaskLesson1B;
        if (static_cast<int16_t>(w.GlobalInt(0x009A2234, 0) & 0xFFFF) == 1) {
            w.ts.KillCurrent();
            return true;
        }
        const uint32_t n = Word(w, params);
        int pick = 0;
        if (n != 0) pick = w.Rand(0x00499452) % static_cast<int>(n + 1);
        uint32_t list = Word(w, params + 4 + static_cast<uint32_t>(pick) * 4);
        std::vector<EnemyTask*> objs;
        for (uint32_t v; (v = Word(w, list)) != 0xFFFFFFFFu && objs.size() < 16; list += 4) objs.push_back(CreateFromRecord(w, v, false));
        list += 4;
        for (uint32_t v; (v = Word(w, list)) != 0xFFFFFFFFu && objs.size() < 16; list += 4) objs.push_back(CreateFromRecord(w, v, true));
        const int cnt = static_cast<int>(objs.size());
        for (int i = 0; i < cnt; ++i) {  // the positions shuffled
            const int k = w.Rand(0x004994BA) % cnt;
            EnemyTask* x = objs[i];
            EnemyTask* y = objs[k];
            if (!x || !y) continue;
            float keep[3];
            memcpy(keep, x->pos, 12);
            memcpy(x->pos, y->pos, 12);
            memcpy(y->pos, keep, 12);
        }
        TaskLesson1B(t);
        return true;
    }
    return false;
}

int TrainLesson(World::Impl& w) { return w.modes ? w.modes->lesson : 0; }

// 0x004033B0 / 0x00403510 in game type 2, lesson 3: the camera of the difficulty 0x004C4384[0x009C9118]
int World::Impl::SegmentCamera(int camId, bool singlePose) {
    if (!modes || runner->cfg.gameType != 2 || modes->lesson != 3) return camId;
    if (singlePose && camId != 0x1A2) return camId;
    return static_cast<int>(exe.U32(0x004C4384 + static_cast<uint32_t>(modes->lessonDiff) * 4));
}

// 0x00453B30 -> 0x00453AE0: the node model only (alt model / the actor's alpha as 0x004534A0 does)
void TrainPlainNode(World::Impl& w, EnemyTask& e, int idx, const Mat4& m) {
    const int model = e.anim.nodes[idx].model;
    if ((e.flags136C & 0x20) && w.altModels != 0) w.Draw(model, m, 8, -1.0f, true, idx);
    else if (e.flags1368 & 0x20) w.Draw(model, m, 8, e.alpha138C, false, idx);
    else w.Draw(model, m, 8, -1.0f, false, idx);
}

// 0x00496BA0: the spawn task becomes the 0x6D actor (record state 0: the select, 1: a lesson)
bool SpawnTrain(World::Impl& w, Task& t, SpawnTask& s) {
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    auto d = std::make_unique<TrainTask>();
    TrainTask& b = *d;
    // op 0x0A (0x00408990) records are {type, s16 state}; op 0x0B (0x004088A0 / 0x00408A20) full records with the
    // params pointer +0x1390 (the script after the lesson and its variant)
    const bool shortRec = s.op == 0x0A;
    if (!shortRec) {
        memcpy(b.pos, s.rec.pos, 12);
        memcpy(b.rot, s.rec.rot, 12);
        b.endScript = static_cast<uint32_t>(s.rec.ParamS32(0));
        b.endVariant = s.rec.ParamS16(4);
    }
    // textures 0x16E / 0x173, pol 0xBE, motion file 0x30, flush
    for (const World::Impl::Request& r : { World::Impl::Request{ 0x54, 0x16E }, World::Impl::Request{ 0x54, 0x173 },
                                           World::Impl::Request{ 0x52, 0xBE }, World::Impl::Request{ 0x56, 0x30 } })
        w.ProcessRequest(r);
    b.actorSlot = -1;
    b.slot120 = -1;
    b.anim.kind = 0x39;
    b.anim.motion[0] = 0x31A;
    w.EnemyInitPose(b);  // 0x00410440
    b.nodeCb = 0x00411050;
    b.postRootFn = [](World::Impl&, EnemyTask&) {};
    b.anim.rotOrder = 1;
    b.anim.nodes[5].model = 0x158F;  // +0x4DC
    b.anim.drawFlags &= ~2u;
    m.trainCur = static_cast<int16_t>(m.lesson);
    m.trainRepeat = 0;
    m.trainRepeatDir = 0;
    m.trainAnim = 0;
    m.trainPrev = 0;
    m.trainAnimCount = 0;
    m.trainSpin = 0;
    for (int i = 0; i < 10; ++i) {
        const int k = (i + m.lesson) % 10;
        ModesState::Slot& sl = m.trainSlots[k];
        sl.id = static_cast<int16_t>(k);
        const float x = static_cast<float>(i * 0xD2);
        sl.x = F32(exe, 0x0055CC38) < x ? x - F32(exe, 0x0055CC34) : x;
        sl.a = i == 0 ? 1.0f : Bits(0x3E99999A);
    }
    const int state = shortRec ? static_cast<int16_t>(s.rec.flags & 0xFFFF) : static_cast<int16_t>(s.rec.state);
    t.data = std::move(d);
    t.fn = TaskTrain;
    if (state == 0) {
        // textures 0x16F / 0x171, model 0x1A4C, flush, 0x00413DA0
        w.ProcessRequest({ 0x54, 0x16F });
        w.ProcessRequest({ 0x54, 0x171 });
        w.ProcessRequest({ 0x50, 0x1A4C });
        w.front.dimCounter = 0;
        w.flags5900 |= 0x10;
        ++w.attackers;  // 0x009C7006: the scene script waits (op 0x43)
        b.f1320 = 0;
        b.caption = 0;
        b.count = 0;
        b.idle = 0;
        b.sel = m.lesson;
        b.diff = Grade(m, b.sel);
        if (b.diff == 5) b.diff = 4;
        b.prevSel = 0;
        b.fn = 0x00496DE0;
        TaskTrain(t);
        return true;
    }
    if (state == 1) {
        w.ProcessRequest({ 0x54, 0x170 });
        w.ProcessRequest({ 0x54, 0x172 });
        w.flags5900 |= 0x10;
        b.f1320 = 1;
        b.caption = 1;
        b.sel = m.lesson;
        b.fn = 0x00497360;
        TaskTrain(t);
        b.fn = 0x00497360;  // (0x00496BA0 sets the function after the call: 0x00497360 runs once more)
        return true;
    }
    w.Note("actor 0x6D state " + std::to_string(state) + " (0x00496BA0) not ported; rec " + std::to_string(s.rec.addr) + " lo " + std::to_string(s.rec.lo) + " type " + std::to_string(s.rec.type) + " flags " + std::to_string(s.rec.flags));
    return true;
}

// 0x00498C10 for the vehicle motion of lesson 9 (world_st3_vehicle.cpp)
void TrainTimeLeft(World::Impl& w, int limit, int counter) { TimeLeft(w, limit, counter); }

// =====================================================================================================================
// Komono kind 67 (generic ctor 0x00461CF0 case 0x43, training lesson 2): a swinging board (model 0x1A36, index 2:
// 0x1A35) carried along camera path 0x00595778[index] (0x196..0x198), with three barrels on it (task 0x004702E0,
// positions 0x00595780[index][3] * 0.25). The barrels are one-shot training barrels (skeleton 6, model 0x1A0F) or,
// for the middle one and by the difficulty 0x009C9118, normal two-shot barrels.
// =====================================================================================================================

namespace {

struct Komono67Task : ActorTask {  // 0x378 block (board and barrels)
    int8_t area = 0, areaCount = 0;  // +0x196 / +0x197
    int8_t group = 0;                // +0x194 (a broken barrel: 0x63)
    float pos[3] = {};               // +0x19C
    int rot[3] = {};                 // +0x1CC / +0x1D0 / +0x1D4
    int swayX = 0, swayZ = 0;        // +0x1D8 / +0x1E0
    int16_t model = 0;               // +0x28C
    int16_t idx = 0;                 // +0x290 board index
    int swing = 0;                   // +0x298
    float t = 0;                     // +0x2C0 path time
    int16_t hp = 0;                  // +0x11C
    int skel = 0;                    // +0x324
    float wpos[3] = {};              // +0x40 (barrels: world position)
    Mat4 mv = Mat4::Identity();      // +0x2E4 the last model-view
};

// 0x007DCD08[index]: the boards. A board removed by the area change keeps its block readable (the barrels read the
// freed block in the same frame), so the barrels read this copy.
struct Board67 {
    Mat4 mv = Mat4::Identity();
    int rotY = 0;
};
Board67 g_board67[3];

int8_t TabS8(const ExeImage& exe, uint32_t va) {
    int8_t b = 0;
    exe.Read(va, &b, 1);
    return b;
}

void Kill67(World::Impl& w, Komono67Task& k) {  // Actor_Destroy 0x00409CC0
    k.flags34 = (k.flags34 & ~1u) | 0x80018000u;
    for (World::Impl::Target& tg : w.targetCopy)
        if (tg.obj == &k) tg.obj = nullptr;
    w.ts.KillCurrent();
}

// 0x00470080
void TaskBoard67(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono67Task& k = t.As<Komono67Task>();
    const ExeImage& exe = w.exe;
    if (w.runner->variant != k.area) {
        ++k.areaCount;
        w.ts.KillCurrent();  // Task_Kill 0x004A7040
        return;
    }
    float tg[3] = {};
    int roll = 0;
    float p[3] = { k.pos[0], k.pos[1], k.pos[2] };
    if (!w.cameraBank.Evaluate(exe.S16(0x00595778 + 2 * static_cast<uint32_t>(k.idx)), k.t, p, tg, &roll, false))
        w.Note("komono 67: camera path not evaluated (0x004042D0)");
    k.pos[0] = p[0];
    k.pos[1] = p[1];
    k.pos[2] = p[2];
    k.t = k.t + 1.0f;  // 0x004C4380
    k.swing += 0x180;
    k.swayX += 0x100;
    k.swayZ += 0x100;
    const float c = F32(exe, 0x004C4D08);
    const double ang = static_cast<double>(static_cast<float>(static_cast<double>(k.swing) * mx::kToRad));
    const float sx = static_cast<float>(std::sin(ang) * c);
    const float cz = static_cast<float>(std::cos(ang) * c);
    const double amp = F64(exe, 0x00569138);  // 640
    const double ax = static_cast<double>(static_cast<float>(static_cast<double>(k.swayX) * mx::kToRad));
    k.rot[0] = static_cast<int>(static_cast<float>(std::sin(ax) * amp));  // __ftol
    const double az = static_cast<double>(static_cast<float>(static_cast<double>(k.swayZ) * mx::kToRad));
    k.rot[2] = static_cast<int>(static_cast<float>(std::cos(az) * amp));
    Mat4 r = Mat4::Identity();
    mx::RotY(r, k.rot[1]);
    mx::RotZ(r, k.rot[2]);
    mx::RotX(r, k.rot[0]);
    const float v[3] = { sx, 0.0f, cz };
    float off[3];
    mx::Transform(r, v, off);
    auto build = [&](Mat4 m) {
        mx::Translate(m, sx + k.pos[0], k.pos[1], cz + k.pos[2]);
        mx::RotY(m, k.rot[1]);
        mx::RotZ(m, k.rot[2]);
        mx::RotX(m, k.rot[0]);
        mx::Translate(m, -off[0], -off[1], -off[2]);
        mx::Scale(m, 2.5f, 2.5f, 2.5f);
        return m;
    };
    w.Draw(k.model, build(Mat4::Identity()));
    Mat4 mv = build(w.cam.view);
    mx::Scale(mv, 0.4f, 0.4f, 0.4f);
    k.mv = mv;
    if (k.idx >= 0 && k.idx < 3) {
        g_board67[k.idx].mv = mv;
        g_board67[k.idx].rotY = k.rot[1];
    }
}

// 0x004702E0
void TaskBarrel67(Task& t) {
    World::Impl& w = *CurrentWorld();
    Komono67Task& k = t.As<Komono67Task>();
    const Board67& b = g_board67[k.idx >= 0 && k.idx < 3 ? k.idx : 0];
    if (w.runner->variant != k.area) {
        ++k.areaCount;
        w.ts.KillCurrent();
        return;
    }
    const uint32_t f = k.flags34;
    if (f & 8) {
        w.SpawnSmoke(k.viewPos[2], (f & 2) ? 0 : 1, k.pos[2]);  // 0x00465860
        if (k.hp == 1) {
            w.Sound(0x1A16A9);
            k.group = 0x63;
            SpawnBarrelPiecesFrom(w, k.idx, k.group, k.skel, k.mv);  // 0x00465170
            int p;                                                   // 0x004650F0(flags, 0)
            if ((f & 2) && (f & 4)) p = w.Rand(0x00465106) & 1;
            else p = (f & 2) ? 0 : 1;
            if (w.GlobalInt(0x009A5C48, 0) == 0 || w.runner->cfg.gameType == 2) ++w.player[p].hits;
            Kill67(w, k);
            return;
        }
        if (k.hp == 2) {
            k.flags34 &= ~8u;
            w.Sound(0x1A16A9);
            --k.hp;
            k.model = 0x19E6;
            k.rot[1] = b.rotY + 0x8000;
        }
    }
    k.flags34 &= ~6u;
    Mat4 top = w.cam.inverse;  // 0x004A9230(camera matrix 0x009A6040)
    mx::MulLeft(top, b.mv);    // x the board's model-view
    mx::Translate(top, k.pos[0], k.pos[1], k.pos[2]);
    mx::RotZ(top, k.rot[2]);
    mx::RotY(top, k.rot[1]);
    mx::RotX(top, k.rot[0]);
    mx::GetTranslation(top, k.wpos);  // +0x40
    w.Draw(k.model, top);
    Mat4 mv = w.cam.view;
    mx::MulLeft(mv, top);
    k.mv = mv;
    const float p[3] = { k.wpos[0], k.wpos[1] + F32(w.exe, 0x0055D2B4), k.wpos[2] };
    mx::Transform(w.cam.view, p, k.viewPos);
    w.RegisterTarget(&k);  // 0x00405160 (+0x32C == 0)
}

}  // namespace

// 0x00461CF0 case 0x43
void SpawnKomono67(World::Impl& w, const SpawnRecord& r) {
    const ExeImage& exe = w.exe;
    Komono67Task* k = w.NewTask<Komono67Task>(TaskBoard67, 0x00470080);
    memcpy(k->pos, r.pos, 12);
    memcpy(k->rot, r.rot, 12);
    k->area = static_cast<int8_t>(w.runner->variant);
    k->hp = static_cast<int16_t>(r.state);
    k->flags34 = 0x80000001u;
    k->swing = w.Rand(0x0046252F) & 0xFFFF;  // +0x298
    k->swayX = w.Rand(0x00462548) & 0xFFFF;  // +0x1D8
    k->swayZ = w.Rand(0x00462561) & 0xFFFF;  // +0x1E0
    const int idx = r.ParamS8(0);             // +0x1F4
    k->rot[1] = idx == 0 ? 0x4000 : 0xC000;
    k->idx = static_cast<int16_t>(idx);
    k->model = idx == 2 ? 0x1A35 : 0x1A36;
    if (idx < 0 || idx > 2) {
        w.Note("komono 67 index " + std::to_string(idx) + " out of the table");
        return;
    }
    g_board67[idx] = Board67{};
    const float s = F32(exe, 0x004C4C58);
    const int d = w.runner->cfg.playerVariant;  // 0x009C9118
    for (int i = 0; i < 3; ++i) {
        Komono67Task* c = w.NewTask<Komono67Task>(TaskBarrel67, 0x004702E0);
        c->area = static_cast<int8_t>(w.runner->variant);
        c->idx = static_cast<int16_t>(idx);
        const uint32_t e = 0x00595780 + static_cast<uint32_t>(i + idx * 3) * 3;
        c->pos[0] = static_cast<float>(TabS8(exe, e)) * s;
        c->pos[1] = static_cast<float>(TabS8(exe, e + 1)) * s;
        c->pos[2] = static_cast<float>(TabS8(exe, e + 2)) * s;
        c->radius = 5.0f;
        c->flags34 = 1;
        const bool normal = i == 1 || (d == 1 && ((idx == 1 && i == 2) || idx == 2)) || (d == 2 && i == 0) || d > 2;
        c->hp = normal ? 2 : 1;
        c->skel = normal ? 0 : 6;
        c->model = normal ? 0x19E8 : 0x1A0F;
    }
}

}  // namespace game
