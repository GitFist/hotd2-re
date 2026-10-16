// game::World - game mode 8: the ranking screen (0x0041A5E0). Root 0x0041B0B0: views task 0x00482C40 + camera reset,
// the player tasks, the table task 0x0041B110 (background 0x0041A810, titles 0x0041AC20, side labels / arrows 0x0041B8C0,
// two pages of ten rows sliding on L / R) and the agent 0x0041BDC0 (skeleton 0x3B). START / reload: back to the mode
// select. Verified against build\scene_capture_modes_rank.txt (notes/agents/modes.md section 4).

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
float S16f(const ExeImage& exe, uint32_t va) { return static_cast<float>(exe.S16(va)); }
float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

struct RankTask : WTask {   // 0x0041B110 block (0x54)
    int16_t page = 0;        // +0x34
    int16_t anim = 0;        // +0x36 (1 slide left after R, 2 slide right after L)
    float fade = 1.0f;       // +0x38 page picture fade
    int16_t bg = 0;          // +0x3C picture state
    int16_t bgCount = 0;     // +0x3E
    int16_t prev = 0;        // +0x40
    int16_t pictIn = 0;      // +0x42
    int16_t pictOut = 0;     // +0x44
    int16_t c46 = 0, c48 = 0;
    float arrowL = 1.0f, arrowR = 1.0f;  // +0x4C / +0x50
};

struct RankAgent : EnemyTask {  // 0x0041BDC0 object (0x13F4)
    int state = 0;           // +0x1350
    int count = 0;           // +0x1334
    float lift = 0;          // +0x1370
    float scale = 0.3f;      // +0x1374 (0x3E99999A)
};

// Spr_DrawSimple 0x0041C6D0(id, x, y, depth, sx, sy, rot, flags)
SpriteDef Simple(int id, float x, float y, float depth, float sx, float sy, int rot, uint32_t flags) {
    SpriteDef d;
    d.id = id;
    d.x = x;
    d.y = y;
    d.depth = depth;
    d.sx = sx;
    d.sy = sy;
    d.rot = rot;
    d.alpha = 1.0f;
    d.blend = -1;
    d.flags = flags;
    return d;
}
// 0x00432590(id, x, y, depth, sx, sy, alpha) / 0x0041B020(.., alpha, flags)
SpriteDef Alpha(int id, float x, float y, float depth, float sx, float sy, float alpha, uint32_t flags = 0) {
    SpriteDef d = Simple(id, x, y, depth, sx, sy, 0, flags);
    d.alpha = alpha;
    return d;
}

}  // namespace

// 0x0041C400(char, x, y, depth, sx, sy, alpha): glyph 0x00530818[char] (0 = none)
static void Glyph(World::Impl& w, int c, float x, float y, float depth, float alpha) {
    const int id = w.exe.S16(0x00530818 + static_cast<uint32_t>(c) * 2);
    if (id != 0) w.hudSprites.push_back(Alpha(id, x, y, depth, 1.0f, 1.0f, alpha));
}

// 0x0041C270(x, y, depth, value, digits, alpha): leading zeros blank (the last digit always drawn), 24 px apart
static void Number(World::Impl& w, float x, float y, float depth, int value, int digits, float alpha) {
    int d[7] = {};
    for (int i = 0; i < digits && i < 7; ++i) {
        d[i] = value % 10;
        value /= 10;
    }
    bool seen = false;
    const float step = F32(w.exe, 0x004ECB84);
    for (int i = digits - 1; i >= 0; --i) {
        if (d[i] != 0) seen = true;
        if (d[i] != 0 || seen || i == 0) Glyph(w, d[i] + 0x30, x, y, depth, alpha);
        x = x + step;
    }
}

// 0x0041C360: two digits, both drawn
static void Two(World::Impl& w, float x, float y, float depth, int value, float alpha) {
    const int tens = (value / 10) % 10, units = value % 10;
    const float step = F32(w.exe, 0x004ECB84);
    Glyph(w, tens + 0x30, x, y, depth, alpha);
    Glyph(w, units + 0x30, x + step, y, depth, alpha);
}

// 0x0041C210: a name, one glyph per character
static void Name(World::Impl& w, float x, float y, float depth, const char* s, float alpha) {
    const float step = F32(w.exe, 0x004ECB84);
    for (int i = 0; i < 4 && s[i]; ++i) {
        Glyph(w, static_cast<int8_t>(s[i]), x, y, depth, alpha);
        x = x + step;
    }
}

// 0x0041C090: a time in frames as m ' s " cc (wrapped at 60 minutes)
static void Time(World::Impl& w, float x, float y, float depth, int t, float alpha) {
    const int hi = static_cast<int>((static_cast<int64_t>(t) * 0x64A7C8C7) >> 32) - t;
    t = t + ((hi >> 0x11) - (hi >> 0x1F)) * 0x34BC0;
    const int rest = t % 0xE10;
    Two(w, x, y, depth, t / 0xE10, alpha);
    const float y2 = y + F32(w.exe, 0x005308F8);
    w.hudSprites.push_back(Simple(0xB7C, x + F32(w.exe, 0x005308F4), y2, depth, 0.6f, 0.6f, 0, 0));
    Two(w, x + F32(w.exe, 0x005308F0), y, depth, rest / 0x3C, alpha);
    w.hudSprites.push_back(Simple(0xB86, x + F32(w.exe, 0x005308EC), y2, depth, 0.6f, 0.6f, 0, 0));
    Two(w, x + F32(w.exe, 0x005308E8), y, depth, ((rest % 0x3C) * 100) / 0x3C, alpha);
}

// 0x0041A810: background tiles (list priority 8), the page picture (dark overlay model 0x93E + 4 x 5 tiles) fading
// between pages of different pictures (0x005307C8 groups, 0x005307E0 first tile)
static void RankBackground(World::Impl& w, RankTask& t) {
    const ExeImage& exe = w.exe;
    int id = 0x66;
    for (int y = 0; id < 0x7A; y += 0x80)
        for (int x = 0; x < 0x280; x += 0x80) w.hudSprites.push_back(Simple(id++, static_cast<float>(x), static_cast<float>(y), 101.0f, 1, 1, 0, 0));
    auto picture = [&](int page, float z, float depth, float alpha) {
        Mat4 m = Mat4::Identity();  // 0x004A9880 / 0x004A9E10: view space
        mx::Translate(m, 0.0f, 0.0f, z);
        mx::Scale(m, 200.0f, 400.0f, 1.0f);
        w.Draw(0x93E, m, 8, alpha, false, -1, true);  // 0x004185A0
        const int base = exe.S16(0x005307E0 + static_cast<uint32_t>(page) * 2);
        for (int r = 0, k = 0; r < 4; ++r)
            for (int c = 0; c < 5; ++c, ++k)
                w.hudSprites.push_back(Alpha(base + k, static_cast<float>(c * 0x80), static_cast<float>(r * 0x80), depth, 1, 1, t.fade));
    };
    auto group = [&](int page) { return exe.S16(0x005307C8 + static_cast<uint32_t>(page) * 2); };
    switch (t.bg) {
    case 0:
        if (exe.S16(0x005307E0 + static_cast<uint32_t>(t.page) * 2) != -1) {
            const float a = F32(exe, 0x005308D0);  // 0.4
            const float keep = t.fade;
            t.fade = 1.0f;
            picture(t.page, -50.0f, 100.0f, a);
            t.fade = keep;
        }
        return;
    case 1:
        if (group(t.prev) != group(t.page)) {
            if (group(t.prev) != -1) t.pictOut = 1;
            if (group(t.page) != -1) t.pictIn = 1;
        }
        t.bg = 2;
        t.bgCount = 0;
        t.fade = 1.0f;
        [[fallthrough]];
    case 2:
        if (t.pictOut == 1) picture(t.prev, -49.0f, 99.0f, t.fade * F32(exe, 0x005308D0));
        t.fade = t.fade - F32(exe, 0x004C4C88);
        if (t.fade < 0.0f) t.fade = 0.0f;
        if (++t.bgCount == 0x14) {
            t.bgCount = 0;
            t.bg = 3;
            t.pictOut = 0;
        }
        return;
    case 3:
        if (t.pictIn == 1) picture(t.page, -50.0f, 100.0f, t.fade * F32(exe, 0x005308D0));
        t.fade = t.fade + F32(exe, 0x004C4C88);
        if (1.0f < t.fade) t.fade = 1.0f;
        if (++t.bgCount == 0x14) {
            t.bg = 0;
            t.pictIn = 0;
        }
        return;
    default: return;
    }
}

// 0x0041AC20: page title and column headings
static void RankTitles(World::Impl& w, RankTask& t) {
    const ExeImage& exe = w.exe;
    const int p = t.page;
    if (p == 0 || p == 1) {
        w.hudSprites.push_back(Alpha(p == 0 ? 0x98D : 0xA1F, S16f(exe, 0x0057A2EE) + 320.0f, S16f(exe, 0x0057A306) + 32.0f, 45.0f, 1, 1, 1, 10));
    } else {
        w.hudSprites.push_back(Alpha(0xA1A, S16f(exe, 0x0057A31E) + 240.0f, S16f(exe, 0x0057A336) + 32.0f, 45.0f, 1, 1, 1, 10));
        w.hudSprites.push_back(Alpha(exe.S16(0x00530810 + static_cast<uint32_t>(p) * 2), S16f(exe, 0x0057A320) + 500.0f,
                                     S16f(exe, 0x0057A338) + 64.0f, 45.0f, 0.8f, 0.8f, 1, 0));
        const int id2 = exe.S16(0x00530828 + static_cast<uint32_t>(p) * 2);
        if (id2 != -1)
            w.hudSprites.push_back(Alpha(id2, S16f(exe, 0x00530840 + static_cast<uint32_t>(p) * 2) + 500.0f, S16f(exe, 0x0057A338) + 64.0f, 45.0f,
                                         0.8f, 0.8f, 1, 0));
    }
    if (p > 1 && p < 12) {
        const float y = S16f(exe, 0x0057A32E) + 96.0f;
        w.hudSprites.push_back(Alpha(0xA23, S16f(exe, 0x0057A318) + 320.0f, y, 45.0f, 1, 1, 1));
        w.hudSprites.push_back(Alpha(0xA25, S16f(exe, 0x0057A31A) + 480.0f, y, 45.0f, 1, 1, 1));
        w.hudSprites.push_back(Alpha(0xA26, S16f(exe, 0x0057A31C) + 520.0f, y, 45.0f, 1, 1, 1));
    } else {
        const float y = S16f(exe, 0x0057A2FE) + 96.0f;
        w.hudSprites.push_back(Alpha(0xA22, S16f(exe, 0x0057A2E6) + 144.0f, y, 45.0f, 1, 1, 1));
        w.hudSprites.push_back(Alpha(0xA23, S16f(exe, 0x0057A2E8) + 320.0f, y, 45.0f, 1, 1, 1));
        w.hudSprites.push_back(Alpha(0xA24, S16f(exe, 0x0057A2EA) + 480.0f, y, 45.0f, 1, 1, 1));
        w.hudSprites.push_back(Alpha(0xA26, S16f(exe, 0x0057A2EC) + 520.0f, y, 45.0f, 1, 1, 1));
    }
}

// 0x0041B8C0: the neighbouring pages' labels and the blinking arrows (grown to 1.5 by a page change)
static void RankArrows(World::Impl& w, RankTask& t, bool allPages) {
    const ExeImage& exe = w.exe;
    int a = (t.page + 0xB) % 0xC, b = (t.page + 1) % 0xC;
    if (!allPages) {
        if (a > 9) a = 9;
        if (b > 9) b = 0;
    }
    w.hudSprites.push_back(Simple(exe.S16(0x005307F8 + static_cast<uint32_t>(a) * 2), S16f(exe, 0x0057A33C) + 24.0f,
                                  S16f(exe, 0x0057A344) + 415.0f, 29.0f, 1, 1, 0, 0));
    w.hudSprites.push_back(Simple(exe.S16(0x005307F8 + static_cast<uint32_t>(b) * 2), S16f(exe, 0x0057A340) + 379.0f,
                                  S16f(exe, 0x0057A348) + 415.0f, 29.0f, 1, 1, 0, 0));
    const uint32_t u = w.seedCounter % 0x3C;  // 0x009A32A0
    auto blink = [&](float s, float x, float y, uint32_t flags) {
        float alpha;
        if (s <= 1.0f) {
            if (u >= 0x2D) return false;
            alpha = u < 8 ? 1.0f : (F32(exe, 0x005308DC) - static_cast<float>(u)) * F32(exe, 0x005308D8);
        } else {
            alpha = 1.0f;
        }
        w.hudSprites.push_back(Alpha(0xA20, x, y, 29.0f, s, s, alpha, flags));
        return true;
    };
    blink(t.arrowL, S16f(exe, 0x0057A33E) - 24.0f, S16f(exe, 0x0057A346) + 415.0f, 0x1A);
    blink(t.arrowR, S16f(exe, 0x0057A342) + 411.0f, S16f(exe, 0x0057A34A) + 415.0f, 10);
    if (t.anim == 0) {
        if (1.0f < t.arrowL) t.arrowL = t.arrowL - F32(exe, 0x004C4C88);
        if (1.0f < t.arrowR) t.arrowR = t.arrowR - F32(exe, 0x004C4C88);
    } else if (t.anim == 1) {
        t.arrowR = 1.5f;
    } else if (t.anim == 2) {
        t.arrowL = 1.5f;
    }
    ++t.c46;
}

// 0x0041B110: the table task
static void TaskRankTable(Task& tk) {
    World::Impl& w = *CurrentWorld();
    RankTask& t = tk.As<RankTask>();
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    RankBackground(w, t);
    RankTitles(w, t);
    RankArrows(w, t, m.rankAllPages);
    for (int k = 0; k < 2; ++k)
        for (int i = 0; i < 10; ++i) {
            const ModesState::RankRow& r = m.rows[k][i];
            const RankEntry& e = m.tables[r.table][r.entry];
            float yy;
            float xo;
            if (r.page < 2 || r.page > 11) {
                yy = S16f(exe, 0x0057A2F4) + r.y;
                Number(w, S16f(exe, 0x0057A2DC) + r.x, yy, r.depth, i + 1, 2, r.alpha);
                w.hudSprites.push_back(Alpha(e.b4 * 2 + 0xA4B, S16f(exe, 0x0057A2DE) + r.x, yy, r.depth, 1, 1, r.alpha));  // 0x0041C050
                Name(w, S16f(exe, 0x0057A2E0) + r.x, yy, r.depth, e.name, r.alpha);
                Number(w, S16f(exe, 0x0057A2E2) + r.x, yy, r.depth, e.score, 7, r.alpha);
                xo = S16f(exe, 0x0057A2E4);
            } else {
                yy = S16f(exe, 0x0057A324) + r.y;
                Number(w, S16f(exe, 0x0057A30C) + r.x, yy, r.depth, i + 1, 2, r.alpha);
                Name(w, S16f(exe, 0x0057A310) + r.x, yy, r.depth, e.name, r.alpha);
                Time(w, S16f(exe, 0x0057A312) + r.x, yy, r.depth, e.score, r.alpha);
                xo = S16f(exe, 0x0057A314);
            }
            w.hudSprites.push_back(Alpha(exe.S16(0x00530040 + static_cast<uint32_t>(e.b5) * 2), xo + r.x, yy, r.depth, 1, 1, r.alpha));
        }
    // L / R held: auto repeat after 60 frames (0x009C9020 held; the keyboard 0x009A37A0 is not driven)
    if ((w.held & 0xC000C0) == 0) {
        m.rankRepeat = 0;
        m.rankRepeatDir = 0;
    } else if (++m.rankRepeat == 0x3C) {
        m.rankRepeat = 0;
        if (w.held & 0x800080) m.rankRepeatDir = 1;
        else if (w.held & 0x400040) m.rankRepeatDir = 2;
    }
    auto group = [&](int page) { return exe.S16(0x005307C8 + static_cast<uint32_t>(page) * 2); };
    auto relayout = [&](int dir) {  // dir +1: the new page comes from the right
        for (int k = 0; k < 2; ++k) {
            int16_t delay = 0;
            for (int i = 0; i < 10; ++i, ++delay) {
                ModesState::RankRow& r = m.rows[k][i];
                r.x = static_cast<float>(k * (dir > 0 ? 0x280 : -0x280) + 0x20);
                r.y = static_cast<float>(0x90 + i * 0x20);
                r.depth = 45.0f;
                r.vx = dir > 0 ? -2.0f : 2.0f;
                r.delay = static_cast<int16_t>(delay * 2);
                r.targetX = dir > 0 ? r.x - F32(exe, 0x005308D4) : r.x + F32(exe, 0x005308D4);
                const int page = k == 0 ? t.prev : t.page;
                r.page = static_cast<int16_t>(page);
                r.table = page;
                r.entry = i;
            }
        }
    };
    switch (t.anim) {
    case 0:
        if ((w.pressed & 0x800080) || m.rankRepeatDir == 1) {
            w.Sound(0x1100A9);
            t.prev = t.page;
            if (!m.rankAllPages && t.page == 9) t.page = 0;
            else ++t.page;
            if (t.page == 0xC) t.page = 0;
            if (group(t.prev) != group(t.page)) t.bg = 1;
            t.anim = 1;
            relayout(1);
        } else if ((w.pressed & 0x400040) || m.rankRepeatDir == 2) {
            w.Sound(0x1100A9);
            t.prev = t.page;
            if (!m.rankAllPages && t.page == 0) t.page = 9;
            else --t.page;
            if (t.page < 0) t.page = 0xB;
            if (group(t.prev) != group(t.page)) t.bg = 1;
            t.anim = 2;
            relayout(-1);
        }
        return;
    case 1: case 2: {
        const float acc = F32(exe, 0x004E30F0);
        for (int k = 0; k < 2; ++k)
            for (int i = 0; i < 10; ++i) {
                ModesState::RankRow& r = m.rows[k][i];
                const int16_t d = r.delay;
                r.delay = static_cast<int16_t>(d - 1);
                if (d >= 1) continue;
                r.x = r.vx + r.x;
                if (t.anim == 1) {
                    r.vx = r.vx - acc;
                    if (r.x <= r.targetX) {
                        r.vx = 0;
                        r.x = r.targetX;
                    }
                    if (m.rows[1][9].x <= m.rows[1][9].targetX) t.anim = 0;
                } else {
                    r.vx = r.vx + acc;
                    if (r.targetX <= r.x) {
                        r.vx = 0;
                        r.x = r.targetX;
                    }
                    if (m.rows[1][9].targetX <= m.rows[1][9].x) t.anim = 0;
                }
            }
        return;
    }
    default: return;
    }
}

// 0x0041BDC0: the agent (skeleton 0x3B) walks in, a speech sprite grows; the trigger sends him off (0x31C)
static void TaskRankAgent(Task& tk) {
    World::Impl& w = *CurrentWorld();
    RankAgent& a = tk.As<RankAgent>();
    if ((w.pressed & 0x40004) && a.count < 0xF0) {
        a.count = 0xF0;
        a.state = 3;
        w.AnimBlendTo(a, 0x31C, 0, 8);
    }
    if (0x1E0 < a.count) {  // 0x0041D690(0xC0), the task ends
        w.ts.KillCurrent();
        return;
    }
    Mat4 m = w.cam.inverse;  // 0x009A6040 + context: the camera matrix
    mx::Translate(m, -9.0f, static_cast<float>(F64(w.exe, 0x005308E0)) - a.lift, Bits(0xC1CD47AE));
    mx::RotX(m, 0);
    mx::RotZ(m, 0xFD80);
    mx::RotY(m, 0x8E88);
    mx::GetTranslation(m, a.pos);
    MatrixToAnglesYAxis(m, &a.rot[0], &a.rot[1], &a.rot[2]);  // 0x00401AE0
    w.AnimDraw(a);       // 0x00411090: 0x004110D0 + shadow 0x0040A590
    w.EnemyShadow(a);
    ++a.anim.counter[0];
    if (a.anim.motion[0] == 0x31A) {
        if (a.anim.tick[0] == w.MotionTicks(0x31A) - 1) {
            w.AnimBlendTo(a, 0x31D, 0, 2);
            a.state = 1;
        }
    } else if (a.anim.motion[0] == 0x31D && a.anim.tick[0] == w.MotionTicks(0x31D) - 1) {
        w.AnimBlendTo(a, 0x31C, 0, 2);
    }
    if (a.state == 1) {
        w.hudSprites.push_back(Simple(0xB59, 144.0f, 365.0f, 21.0f, a.scale, a.scale, 0, 9));
        a.scale = a.scale + F32(w.exe, 0x004C4CC8);
        if (1.0f <= a.scale) a.state = 2;
    } else if (a.state == 2) {
        w.hudSprites.push_back(Simple(0xB1A, 202.0f, 376.0f, Bits(0x41A7D70A), 1, 1, 0, 9));
        w.hudSprites.push_back(Simple(0xB59, 144.0f, 365.0f, 21.0f, 1, 1, 0, 9));
        if (a.count == 0xF0) a.state = 3;
    }
    if (0xF0 < ++a.count) a.lift = a.lift + static_cast<float>(F64(w.exe, 0x004C4C98));
}

// Game mode 8 (0x0041A5E0)
void RankFrame(World::Impl& w) {
    ModesState& m = *w.modes;
    if (w.subState == 0) {
        // sounds off (0x0041D350), Mem_Init, model slots (0x0041D510 / 0x0041D540), 0x0041EBB0, 0x0049DFD0, pols 0x168 /
        // 0x169 / 0x149 / 0x167, 0x0041D650(0xC0), motion file 0x30, flush, 0x00413DA0, BGM 0x10000026 (RANK_MOD)
        w.SoundStopAll();
        w.ts.Reset();
        w.resQueue.clear();
        w.residency = false;
        w.front.dimCounter = 0;
        for (auto& s : w.actorSlots) s = nullptr;
        w.Sound(0x10000026);
        for (int k = 0; k < 2; ++k)
            for (int i = 0; i < 10; ++i) {
                ModesState::RankRow& r = m.rows[k][i];
                r = ModesState::RankRow{};
                r.x = static_cast<float>(k * 0x280 + 0x20);
                r.y = static_cast<float>(0x90 + i * 0x20);
                r.table = k == 0 ? 0 : 1;  // 0x009A3420 / 0x009C7120
                r.entry = i;
            }
        int graded = 0;
        for (int i = 0; i < 8; ++i)
            if (m.bossGrades[i] != 0) ++graded;
        m.rankAllPages = 7 < graded;  // 0x007C216D
        // Task_InitRoot(0x0041B0B0): 0x0041C1F0 (views task 0x00482C40, camera reset 0x004021D0), players, table, agent
        w.NewTask<WTask>(WorldTramp<&World::Impl::TaskMapViews>, 0x00482C40);
        w.CameraReset();
        w.runner->camera.tick = 0;
        for (int k = 0; k < 3; ++k) w.runner->camera.ctx[0].pos[k] = w.runner->camera.ctx[0].target[k] = 0;
        w.CreatePlayerTasks();
        w.NewTask<RankTask>(TaskRankTable, 0x0041B110);
        {
            Task* tk = w.ts.Create(TaskRankAgent, std::make_unique<RankAgent>());
            RankAgent& a = tk->As<RankAgent>();
            a.fn = 0x0041BDC0;
            a.actorSlot = -1;
            a.slot120 = -1;
            a.anim.kind = 0x3B;
            a.anim.motion[0] = 0x31A;
            w.EnemyInitPose(a);  // 0x00410440 (also registers an actor slot)
            a.nodeCb = 0x00411050;
            a.postRootFn = [](World::Impl&, EnemyTask&) {};  // 0x00420810
            a.anim.rotOrder = 1;
            a.anim.drawFlags &= ~2u;
        }
        w.front.modeFrames = 0;
        w.subState = 1;
        return;
    }
    if (w.subState != 1) return;
    if (w.cfg.portFeatures) {
        for (int p = 0; p < 2; ++p) {
            const auto& in = w.input[p];
            if (in.kind != 1 || !in.fired) continue;
            w.pressed &= ~(12u << (p * 16));
            if (!MenuAim(in)) continue;
            const uint32_t action = MenuRect{8, 410, 270, 442}.Hit(in.x, in.y) ? 0x40 :
                                    MenuRect{370, 410, 632, 442}.Hit(in.x, in.y) ? 0x80 :
                                    MenuRect{272, 442, 368, 478}.Hit(in.x, in.y) ? 2 : 0;
            w.pressed |= action << (p * 16);
        }
    }
    w.ts.RunFrame();
    if (w.cfg.portFeatures) {
        MenuButton(w, {272, 442, 368, 478}, "BACK");
        MenuSights(w);
    }
    ++w.front.modeFrames;
    if (w.pressed & 0xA000A) {  // start / reload: 0x0040AC10, Game_SetState(4), resources freed
        w.Sound(0x500A9);
        w.FrontAfterStart();
        w.front.modeRequest = 4;
    }
    // 0x00413CC0: the dimming counter (as the mode select)
    if (w.held == 0 && w.pressed == 0) {
        if (18000 < ++w.front.dimCounter && w.front.dimCounter > 0x468C) w.front.dimCounter = 0x468C;
    } else {
        w.front.dimCounter = 0;
    }
}

}  // namespace game
