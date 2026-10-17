// game::World - the ending after the final boss: game mode 6 sub-state 7 (0x00431550: event mode 9, the ending root
// 0x00431740), sub-state 8 (0x00431660: the ending scenes until Stage_End), the ranking check (0x004157F0) and the
// name entry (sub-states 9 / 10: 0x00480D90 / 0x00481400), back to the title. Status and verification:
// notes/agents/stage6.md (world_test ... --prefix build\scene_capture_auto_st6.txt on scene_capture_auto_st6_end.txt).

#include <cmath>
#include <cstring>

#include "game/world_modes.h"
#include "game/world_st6.h"

namespace game {

using namespace st6;

namespace {

struct EndingState : TaskData {
    // the ranking table of game type 0 (0x009A3420: 10 entries {name[4], b4 class, b5 player, score, device mode}),
    // defaults 0x00530048 (0x0041C490(0)) for a world without the persistent data (world_test); a booted world uses the
    // loaded tables (ModesState::tables, 0x0041C490(1)) through Table()
    using Entry = RankEntry;
    Entry table[10];
    bool loaded = false;
    int16_t cls[2] = {};  // 0x007DCF70 score class per player (0x00481060)
    // ---- name entry (0x00480D90 / 0x00481400)
    int16_t slot[2] = { -1, -1 };  // 0x007DCF64: the player's ranking row (-1: none)
    int done[2] = {};              // 0x009A2C80
    int16_t len[2] = {};           // 0x007DCF48 / 0x007DCF86 letters entered
    int picked[2][3] = {};         // 0x007DCF74 / 0x007DCF30: letters entered (letter index)
    int16_t fade = 0;              // 0x007DCF6A (1: the time ran out)
    int fadeCount = 0x3C;          // 0x007DCF68
    float wave = 0;                // 0x007DCF6C
    float digitScale = 0.25f, alpha = 1.0f, digitStep = 0.2f;  // 0x007DCF50 / 54 / 58
    struct Letter {                // the 0x38-byte block of a letter task (0x00811160[i])
        float x = 0, y = 0, z = -5.0f;
        int rot = 0, spin = 0, idx = 0;
        float scale = 0.25f, glow = 0;
        uint8_t flags = 0;          // +0x34 (8 picked, 2 / 4 by P1 / P2)
    } letters[32];
    int cursor[2] = { 0, 7 };       // 0x00811148 / 0x00811154
    float cur[2][2] = {};           // 0x00811140 / 0x0081114C cursor point (the letter's x / y)
    float timerPos[3] = {};         // timer task block +0 / +4 / +8
    int timerFrames = 0, timerSecs = 0x3C;  // +0x0C / +0x10
};

EndingState& ES(World::Impl& w) {
    if (!w.ending) w.ending = std::make_shared<EndingState>();
    EndingState& e = static_cast<EndingState&>(*w.ending);
    if (!e.loaded) {
        for (int i = 0; i < 10; ++i) {
            const uint32_t a = 0x00530048 + static_cast<uint32_t>(i) * 0x10;
            const uint32_t n = w.exe.U32(a);
            memcpy(e.table[i].name, &n, 4);
            const uint32_t info = w.exe.U32(a + 4);
            e.table[i].b4 = static_cast<int8_t>(info & 0xFF);
            e.table[i].b5 = static_cast<int8_t>((info >> 8) & 0xFF);
            e.table[i].pad = static_cast<int16_t>(info >> 16);
            e.table[i].score = static_cast<int32_t>(w.exe.U32(a + 8));
            e.table[i].c = static_cast<int32_t>(w.exe.U32(a + 12));
        }
        e.loaded = true;
    }
    return e;
}

// 0x00480F70 (0x007DCF44): the ranking table of the game type (0 arcade, 1 original, 3 the boss's time table)
RankEntry* Table(World::Impl& w) {
    EndingState& e = ES(w);
    if (!w.modes) return e.table;
    return w.modes->tables[ModesTableIndex(w)];
}

}  // namespace

// 0x004157F0(p): -1 unless player p (not in state 9) reaches the last entry of the game type's ranking table (game
// types 0 / 1: score, 3: boss time, world_modes_boss.cpp); 0x007C211C (two players joined at once, 0x00414F40) blocks it
static int RankingSlot(World::Impl& w, int p) {
    if (RandomizerActive(w)) return -1;
    if (w.players == 2) return -1;  // 0x007C211C = 1 (0x00414F40 with two players)
    if (w.modes && w.runner->cfg.gameType == 3) return BossRankCheck(w, p);
    if (w.player[p].score < Table(w)[9].score) return -1;
    return w.player[p].state != 9 ? 1 : -1;
}

// 0x00436200(p, n): score class of player p (0: 80000 and more .. 7: below 24000)
static int ScoreClass(World::Impl& w, int p) {
    const int s = w.player[p & 1].score;
    if (79999 < s) return 0;
    if (71999 < s) return 1;
    if (63999 < s) return 2;
    if (55999 < s) return 3;
    if (45999 < s) return 4;
    if (35999 < s) return 5;
    return s < 24000 ? 7 : 6;
}

// Actor type 0x33 state 11 (0x00432FF0 -> 0x00434260, first run next frame): the ending music by score class (the
// top class: 0x1000000C, else 0x1000000D), then removed
static void TaskEndingBgm(Task& t) {
    World::Impl& w = *CurrentWorld();
    EnemyTask& e = t.As<EnemyTask>();
    bool top = false;
    if (w.players == 2) top = ScoreClass(w, 0) == 0 || ScoreClass(w, 1) == 0;
    else if (w.players == 1) top = ScoreClass(w, w.playersInGame) == 0;
    w.Sound(top ? 0x1000000Cu : 0x1000000Du);
    w.EnemyDestroy(e);  // 0x00409CC0
}

// 0x00432590(id, x, y, depth, sx, sy, alpha): Spr_Draw 0x0041C630 of a plain sprite
static void Sprite(World::Impl& w, int id, float x, float y, float depth, float sx, float sy, float alpha) {
    SpriteDef d;
    d.id = id;
    d.x = x;
    d.y = y;
    d.depth = depth;
    d.sx = sx;
    d.sy = sy;
    d.rot = 0;
    d.alpha = alpha;
    d.blend = -1;
    d.flags = 0;
    d.source = 0x00432590;  // (frontend_test: compared with the capture's sprites from 0x00432607)
    w.hudSprites.push_back(d);
}

// 0x004324B0(x, y, depth, value, digits, alpha): digits 0x8EA + 2d, 16 apart, leading zeros skipped (not the last)
static void Number(World::Impl& w, float x, float y, float depth, int value, int digits, float alpha) {
    int d[7] = {};
    for (int i = 0; i < digits && i < 7; ++i) {
        d[i] = value % 10;
        value /= 10;
    }
    bool seen = false;
    for (int i = digits - 1; i >= 0; --i) {
        if (d[i] != 0) seen = true;
        if (d[i] != 0 || seen || i == 0) Sprite(w, d[i] * 2 + 0x8EA, x, y, depth, 1.0f, 1.0f, alpha);
        x = x + 16.0f;
    }
}

namespace {

// Actor type 0x54 (0x00431780 -> 0x00431810): one page of the staff roll (record lo = page), sliding in from the
// side of the record's x, fading, life = record state frames
struct StaffTask : EnemyTask {
    int t = 0;            // +0x1330
    float slide = 0;      // +0x1348
    float x = 0;          // +0x134C
    int base = 0;         // +0x1350
    int life = 0;         // +0x1354
    float alpha = 0;      // +0x1370
    int16_t page = 0;     // +0x1316
};

void TaskStaff(Task& t) {  // 0x00431810
    World::Impl& w = *CurrentWorld();
    StaffTask& k = t.As<StaffTask>();
    const bool neg = k.pos[0] < 0.0f;
    const int set = w.exe.S16(0x0055DCC0 + static_cast<uint32_t>(k.page) * 2);
    k.base = w.exe.S16(0x0055DC34 + static_cast<uint32_t>(set) * 8);
    uint32_t list = w.exe.U32(0x0055DC30 + static_cast<uint32_t>(set) * 8);
    const int c = k.t;
    const float s8 = 8.0f, s16 = 16.0f;
    if (neg) {
        if (c < 0x14) {
            k.slide = k.slide + s8;
            k.x = k.x + s16;
        }
        if (k.life - 0x14 < c) {
            k.slide = k.slide - s8;
            k.x = k.x - s16;
        }
    } else {
        if (c < 0x14) {
            k.slide = k.slide - s8;
            k.x = k.x - s16;
        }
        if (k.life - 0x14 < c) {
            k.slide = k.slide + s8;
            k.x = k.x + s16;
        }
    }
    // 0x00419490(slide + 320, 240): the projection centre (renderer; used from the next frame on)
    w.projCenter[0] = k.slide + 320.0f;  // fadd 0x004C49CC
    w.projCenter[1] = 240.0f;
    if (k.page < 0xE) {
        Sprite(w, 0x331, k.x, 0.0f, 2.0f, 2.5f, 15.0f, 1.0f);
        for (int i = 0; i < 100; ++i, list += 4) {
            const int16_t sx = w.exe.S16(list), sy = w.exe.S16(list + 2);
            if (sx == -1) break;
            Sprite(w, k.base + i, static_cast<float>(sx) + k.x, static_cast<float>(sy), 1.0f, 1.0f, 1.0f, k.alpha);
        }
    }
    if (c == 0xDC || c == 0x1A4 || c == 0x26C || c == 0x334 || (c == 0x3FC && set == 9)) ++k.page;
    const int now = w.exe.S16(0x0055DCC0 + static_cast<uint32_t>(k.page) * 2);
    const float da = Bits(0x3D4CCCCD);  // 0x004C4C88
    if (0x13 < c && c < 0x28) k.alpha = k.alpha + da;
    for (int b : { 200, 400, 600, 800 }) {
        const int e = b == 200 ? 0xDC : b == 400 ? 0x1A4 : b == 600 ? 0x26C : 0x334;
        if (b <= c && c < e) k.alpha = k.alpha - da;
        if (e <= c && c < e + 0x14) k.alpha = k.alpha + da;
    }
    if (1000 <= c && c < 0x3FC && now == 9) k.alpha = k.alpha - da;
    if (0x3FC <= c && c < 0x410 && now == 10) k.alpha = k.alpha + da;
    if (0x3D3 < c && c < 1000 && now != 9) k.alpha = k.alpha - da;
    if (0x4AF < c && c < 0x4C4 && now == 10) k.alpha = k.alpha - da;
    --k.hp;
    k.t = c + 1;
    if (k.hp < 1) w.ts.KillCurrent();
}

// Actor type 0x56 (0x00431BF0 -> 0x00431C10): "THE END" sprite 0x250 fading in (+1/60 a frame), life = record state
struct TheEndTask : EnemyTask {
    int t = 0;          // +0x1330 (never counted)
    float alpha = 0;    // +0x1370
};

void TaskTheEnd(Task& t) {
    World::Impl& w = *CurrentWorld();
    TheEndTask& k = t.As<TheEndTask>();
    Sprite(w, 0x250, Bits(0x436E0000), Bits(0x43560000), 1.0f, 1.0f, 1.0f, k.alpha);
    if (k.t < 0x3C) k.alpha = k.alpha + Bits(0x3C888889);
    if (k.t == 0x3C) k.alpha = 1.0f;
    if (--k.hp < 1) w.ts.KillCurrent();
}

// Actor type 0x55 (0x00431C90 -> 0x00431D50): the results (rank emblem / class per player in state 7, the scores),
// over the turning backdrop (view-space models 0x93E / 0x15CE / 0x7ED / 0x15CF)
struct ResultTask : EnemyTask {
    int t = 0;           // +0x1320
    float fade = 0;      // +0x1340
    int p1 = 0, p2 = 0;  // +0x1354 / +0x1358
    int turn = 0;        // +0x135C
    int c1 = 0, c2 = 0;  // +0x1360 / +0x1364 score classes
    float alpha = 1.0f;  // +0x1370
};

void TaskResults(Task& t) {
    World::Impl& w = *CurrentWorld();
    ResultTask& k = t.As<ResultTask>();
    const int c = k.t;
    if (c == 0) k.turn = 0;
    if (0x1D < c && c < 0x3C) k.fade = k.fade + Bits(0x3D088889);
    if (c == 0x3C) k.fade = 1.0f;
    auto vs = [&](int model, float x, float y, float z, int ry, float s) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, x, y, z);
        if (ry) mx::RotY(m, ry);
        if (s != 1.0f) mx::Scale(m, s, s, s);
        w.Draw(model, m, 8, -1.0f, false, -1, true);
    };
    vs(0x93E, 0.0f, 0.0f, -20.0f, 0, 250.0f);
    if (k.t < 0x3C) {
        k.turn -= 0x222;
        if (k.turn < -0x8000) k.turn = -0x8000;
        // 0x00432420(turn): vertex wave of the backdrop (renderer)
        vs(0x15CE, 0.0f, 0.0f, -17.0f, ((0x3C - k.t) * 0x8000) / 0x3C, 1.0f);
        if (k.t < 0x3A) vs(0x15CE, 0.0f, 0.0f, -17.0f, std::min(((0x3A - k.t) * 0x8000) / 0x3C, 0x8000), 1.0f);
        if (k.t < 0x38) vs(0x15CE, 0.0f, 0.0f, -17.0f, ((0x38 - k.t) * 0x8000) / 0x3C, 1.0f);
        vs(0x7ED, 0.0f, 0.0f, -17.0f, (-0x10 - k.t) * 0x222, 1.0f);
    } else {
        vs(0x15CE, 0.0f, 0.0f, -17.0f, 0, 1.0f);
    }
    vs(0x15CF, 0.0f, 0.0f, -17.0f, 0, 1.0f);
    auto player = [&](int p, int cls, uint32_t emblemX, uint32_t emblemY, uint32_t badgeX, uint32_t badgeY, int badgeRot,
                      uint32_t classX, int model, int sprX, float scoreX) {
        const float inv = 1.0f - k.fade;
        float s = inv + inv + Bits(0x3E75C28F);
        Mat4 m = Mat4::Identity();
        mx::Translate(m, Bits(emblemX), Bits(emblemY), -5.0f);
        mx::Scale(m, s, s, s);
        w.Draw(model, m, 8, k.fade, false, -1, true);
        m = Mat4::Identity();
        mx::Translate(m, Bits(badgeX), Bits(badgeY), Bits(0xC09FAE14));
        mx::Scale(m, Bits(0x3EB851EC), Bits(0x3EB851EC), Bits(0x3EB851EC));
        mx::RotZ(m, badgeRot);
        w.Draw(0x15CD, m, 8, k.fade, false, -1, true);
        m = Mat4::Identity();
        mx::Translate(m, Bits(classX), static_cast<float>(F64(w.exe, 0x0055DCF8)) - static_cast<float>(cls) * static_cast<float>(F64(w.exe, 0x0055DD00)),
                      Bits(0xC09FAE14));
        s = inv + Bits(0x3E99999A);  // 0x004C4D10
        mx::Scale(m, s, s, s);
        w.Draw(cls + 0x15D0, m, 8, k.fade, false, -1, true);
        const float sy = inv + k.alpha;
        Sprite(w, cls * -2 + 0xAA5, static_cast<float>(w.exe.S16(0x0055DCEE - 2 * static_cast<uint32_t>(cls)) + sprX), 402.0f,
               1.0f, sy, sy, k.fade);
        Number(w, scoreX, 356.0f, 1.0f, w.player[p].score, 7, k.fade);
    };
    if (k.p1 == 1) player(0, k.c1, 0xBEFADF72, 0x3FA1476B, 0xBE9EB7CC, 0x3FBFFF93, 0x2C5, 0xBF970A3D, 0x15D8, 0, 128.0f);
    if (k.p2 == 1) player(1, k.c2, 0x3FEB8506, 0x3F9FFFE7, 0x3FD0A26E, 0x3FC8F545, 0x12BD, 0x3F9332A5, 0x15D9, 300, 430.0f);
    if (--k.hp < 1) {
        w.ts.KillCurrent();
        return;
    }
    ++k.t;
}

}  // namespace

bool World::Impl::SpawnEnding(Task& t, SpawnTask& s) {
    const SpawnRecord r = s.rec;
    if (r.type == 0x33 && r.state == 11) {
        auto d = st5::FromRecord<EnemyTask>(r, 0x00434260);
        st5::RegisterSlot(*this, *d);  // 0x00409270
        t.data = std::move(d);
        t.fn = TaskEndingBgm;
        return true;
    }
    if (r.type == 0x54) {  // 0x00431780 (0x009C911E: language 2 here, never 5)
        auto d = st5::FromRecord<StaffTask>(r, 0x00431810);
        d->life = d->hp;
        d->t = 0;
        d->page = static_cast<int16_t>(r.lo);
        d->slide = 0.0f;
        d->x = 0.0f <= d->pos[0] ? 640.0f : -320.0f;  // (+0x1344 = -240 / 560: unused)
        d->alpha = 0.0f;
        t.data = std::move(d);
        t.fn = TaskStaff;
        return true;
    }
    if (r.type == 0x56) {  // 0x00431BF0
        auto d = st5::FromRecord<TheEndTask>(r, 0x00431C10);
        d->alpha = 0.0f;
        d->t = 0;
        t.data = std::move(d);
        t.fn = TaskTheEnd;
        return true;
    }
    if (r.type == 0x55) {  // 0x00431C90
        auto d = st5::FromRecord<ResultTask>(r, 0x00431D50);
        EndingState& e = ES(*this);
        d->t = 0;
        d->fade = 0.0f;
        d->alpha = 1.0f;
        if (player[0].state == 7) {
            d->p1 = 1;
            d->c1 = ScoreClass(*this, 0);
            e.cls[0] = static_cast<int16_t>(d->c1);  // 0x00481060 -> 0x007DCF70
        }
        if (player[1].state == 7) {
            d->p2 = 1;
            d->c2 = ScoreClass(*this, 1);
            e.cls[1] = static_cast<int16_t>(d->c2);
        }
        t.data = std::move(d);
        t.fn = TaskResults;
        return true;
    }
    if (r.type == 0x65) {  // 0x00436140: route branch 0x009C88A4 by the score (top class 1, a round score 2)
        int& br = runner->camera.routeBranch;
        br = 0;
        int last = 0;
        bool top = false;
        if (players == 2) {
            last = (player[0].score + player[1].score) % 10;
            top = ScoreClass(*this, 0) == 0 || ScoreClass(*this, 1) == 0;
        } else {
            last = (playersInGame == 0 ? player[0].score : player[1].score) % 10;
            if (players == 1) top = ScoreClass(*this, playersInGame) == 0;
        }
        if (top) br = 1;
        else if (last == 0) br = 2;
        ts.KillCurrent();
        return true;
    }
    return false;
}

// Mode_Next 0x0040E960 from game mode 6 (0x004E1014[6] = 3, the title): a request committed at the end of the frame
// (World::Boot), or the switch itself for a World run by world_test
static void ModeNext(World::Impl& w) {
    if (w.frontEnd) w.front.modeRequest = 3;
    else w.SetGameMode(3);
}

// shared tail of 0x00431550 / 0x00431660: after Stage_End (0x009C71C0) the name entry (sub-state 9) when a player
// reaches the ranking, else Mode_Next 0x0040E960 (the title)
static void EndingCheck(World::Impl& w) {
    auto it = w.runner->globals.find(0x009C71C0);
    if (it != w.runner->globals.end() && it->second != 0) {
        if (RankingSlot(w, 0) == -1 && RankingSlot(w, 1) == -1) {
            w.cfg.freePlay = false;  // 0x00406FD0: credits 0x009C8E60.. and free play 0x009C8E70 cleared
            w.cfg.credits = 0;
            w.players = 0;
            w.route = 0;
            ++w.front.modeFrames;
            ModeNext(w);  // Mode_Next 0x0040E960 (committed at the end of the frame)
            return;
        }
        w.subState = 9;
    }
    ++w.front.modeFrames;
}

// ---- name entry ----------------------------------------------------------------------------------------------------
// view-space model with the name-entry alpha (0x004185A0(model, 0x007DCF54))
static void VsDraw(World::Impl& w, int model, float x, float y, float z, int ry, float s, float alpha, int rz = 0) {
    Mat4 m = Mat4::Identity();
    mx::Translate(m, x, y, z);
    if (ry) mx::RotY(m, ry);
    mx::Scale(m, s, s, s);
    if (rz) mx::RotZ(m, rz);
    w.Draw(model, m, 8, alpha, false, -1, true);
}

// 0x00482A00(x, y, z, scale, rot, letter): letter model 0x00569258[letter] (0x84: 0x141F turned over)
static void LetterDraw(World::Impl& w, EndingState& e, float x, float y, float z, float s, int rot, int letter) {
    const int model = w.exe.S16(0x00569258 + static_cast<uint32_t>(letter) * 2);
    if (model == 0x84) VsDraw(w, 0x141F, x, y, z, rot, s, e.alpha, 0x8000);
    else VsDraw(w, model, x, y, z, rot, s, e.alpha);
}

// 0x004811F0(p): the player's row in the ranking (rows below shift down), default name 0x00596B10
static void RankInsert(World::Impl& w, EndingState& e, int p) {
    RankEntry* t = Table(w);
    const int score = w.player[p].score;
    if (score < t[9].score) {
        e.slot[p] = -1;
        e.done[p] = 1;
        return;
    }
    int row = 9;
    while (row > 0 && !(score < t[row - 1].score)) {
        t[row] = t[row - 1];
        --row;
    }
    EndingState::Entry& n = t[row];
    n.score = score;
    n.b4 = static_cast<int8_t>(e.cls[p]);
    n.b5 = static_cast<int8_t>(p);
    n.c = w.input[p].deviceMode;  // 0x0041E260: the device modes of P1 (0x00588E24) / P2 (0x007DC698)
    e.slot[p] = static_cast<int16_t>(row);
    const uint32_t def = w.exe.U32(0x00596B10);
    memcpy(n.name, &def, 4);
}

// 0x00481300(p): the boss time 0x009C7060 into the boss's table (a lower time ranks higher; b4 = -1, b5 = the player)
static void BossTimeInsert(World::Impl& w, EndingState& e, int p) {
    RankEntry* t = Table(w);
    const int time = w.modes->bossTime;
    if (t[9].score < time) {
        e.slot[p] = -1;
        e.done[p] = 1;
        return;
    }
    int row = 9;
    while (row > 0 && !(t[row - 1].score < time)) {
        t[row] = t[row - 1];
        --row;
    }
    EndingState::Entry& n = t[row];
    n.score = time;
    n.b4 = -1;
    n.b5 = static_cast<int8_t>(p);
    n.c = w.input[p].deviceMode;  // 0x0041E260
    e.slot[p] = static_cast<int16_t>(row);
    const uint32_t def = w.exe.U32(0x00596B10);
    memcpy(n.name, &def, 4);
}

void TaskLetter(Task& t);  // 0x004825B0

struct LetterTask : ActorTask {
    int i = 0;
};

void TaskNamePanel(Task& t) {  // 0x00482310 (P1) / 0x00482460 (P2)
    World::Impl& w = *CurrentWorld();
    EndingState& e = ES(w);
    const int p = t.As<WTask>().fn == 0x00482310 ? 0 : 1;
    const float sx = p == 0 ? -1.0f : 1.0f;
    const float base = F32(w.exe, 0x00565EEC);
    for (int i = 0; i < e.len[p]; ++i) {
        const EndingState::Letter& l = e.letters[e.picked[p][i] & 31];
        LetterDraw(w, e, static_cast<float>(i) * Bits(0x3E99999A) + sx * base, Bits(0xBF99999A), l.z, l.scale, 0, l.idx);
    }
    if (p == 0) {
        VsDraw(w, 0x15D8, Bits(0xBFD9999A), -1.0f, Bits(0xC099999A), 0x8000, Bits(0x3E4CCCCD), e.alpha);
        VsDraw(w, w.exe.S16(0x005692C0 + static_cast<uint32_t>(e.slot[0]) * 2), Bits(0xBF733333), Bits(0xBF4CCCCD), Bits(0xC09B3333), 0,
               Bits(0x3EE66666), e.alpha);
    } else {
        VsDraw(w, 0x15D9, Bits(0x3F4CCCCC), -1.0f, Bits(0xC099EB85), 0x8000, Bits(0x3E4CCCCD), e.alpha);
        VsDraw(w, w.exe.S16(0x005692C0 + static_cast<uint32_t>(e.slot[1]) * 2), Bits(0x3FCCCCCC), Bits(0xBF4CCCCD), Bits(0xC09B3333), 0,
               Bits(0x3EE66666), e.alpha);
    }
    if (e.len[p] != 3 && e.done[p] == 0)
        LetterDraw(w, e, static_cast<float>(e.len[p]) * Bits(0x3E99999A) + sx * base, Bits(0xBF99999A), -5.0f, 0.25f, 0, 0x21);
    for (int i = 0; i < 3; ++i) LetterDraw(w, e, static_cast<float>(i) * Bits(0x3E99999A) + sx * base, -1.25f, Bits(0xC09FAE14), 0.25f, 0, 0x20);
}

// 0x00482AA0(x, y, z, value, digits) with the digit models 0x005691A0[char] (0x00482BB0)
static void TimerDigits(World::Impl& w, EndingState& e, float x, float y, float z, int value, int digits) {
    int d[7] = {};
    for (int i = 0; i < digits && i < 7; ++i) {
        d[i] = value % 10;
        value /= 10;
    }
    bool seen = false;
    for (int i = digits - 1; i >= 0; --i) {
        if (d[i] != 0) seen = true;
        if (d[i] != 0 || seen || i == 0) {
            const int model = w.exe.S16(0x005691A0 + static_cast<uint32_t>('0' + d[i]) * 2);
            if (model != 0) VsDraw(w, model, x, y, z, 0, e.digitScale, e.alpha);
        }
        x = e.digitStep + x;
    }
}

void TaskNameTimer(Task& t) {  // 0x00482230: 60 seconds; a trigger off the screen takes a second; 0 -> time up
    World::Impl& w = *CurrentWorld();
    EndingState& e = ES(w);
    (void)t;
    e.digitScale = 0.5f;
    e.digitStep = Bits(0x3ECCCCCD);
    const float x = e.timerSecs < 10 ? e.timerPos[0] - Bits(0x3E4CCCCD) : e.timerPos[0];
    TimerDigits(w, e, x, e.timerPos[1], e.timerPos[2], e.timerSecs, 2);
    e.digitScale = Bits(0x3E99999A);
    e.digitStep = Bits(0x3E4CCCCD);
    for (int p = 0; p < 2; ++p)
        if (e.slot[p] != -1 && w.player[p].gun.valid == 0 && w.player[p].gun.fired == 1 && e.done[p] == 0) --e.timerSecs;
    if (0x3B < ++e.timerFrames) {
        e.timerFrames = 0;
        --e.timerSecs;
    }
    if (e.timerSecs < 0) {
        e.timerSecs = 0;
        e.fade = 1;
    }
}

void TaskLetter(Task& t) {  // 0x004825B0
    World::Impl& w = *CurrentWorld();
    EndingState& e = ES(w);
    LetterTask& k = t.As<LetterTask>();
    EndingState::Letter& l = e.letters[k.i & 31];
    const bool gun0 = w.player[0].inputKind == 1, gun1 = w.player[1].inputKind == 1;  // +0x125 (1: a gun)
    if (gun0 && (k.flags34 & 2)) {
        k.flags34 &= ~0xAu;
        l.glow = Bits(0x3E99999A);
        l.flags |= 10;
    }
    if (gun1 && (k.flags34 & 4)) {
        k.flags34 &= ~0xCu;
        l.glow = Bits(0x3E99999A);
        l.flags |= 0xC;
    }
    if (l.flags & 8) {
        l.flags &= ~8;
        l.spin = 0x3200;  // 1.79366e-41 = 0x3200
        w.Sound(0x3E16A9);
        const int model = w.exe.S16(0x00569258 + static_cast<uint32_t>(l.idx) * 2);
        for (int p = 0; p < 2; ++p) {
            if (!(l.flags & (2 << p)) || e.slot[p] == -1) continue;
            if (model == 0x1415) {
                e.done[p] = 1;  // END
            } else if (model == 0x1430) {
                if (0 < e.len[p]) {
                    --e.len[p];
                    Table(w)[e.slot[p] & 15].name[e.len[p] & 3] = 0;
                }
            } else if (e.len[p] < 3) {
                e.picked[p][e.len[p]] = k.i;
                Table(w)[e.slot[p] & 15].name[e.len[p] & 3] = static_cast<char>(w.exe.U32(0x005692A0 + static_cast<uint32_t>(l.idx)) & 0xFF);
                ++e.len[p];
            }
        }
        l.flags &= ~6;
    }
    const float f = w.focal;
    const float sx = -((f * l.x) / l.z), sy = -((f * l.y) / l.z);
    float c0x = 0, c0y = 0, c1x = 0, c1y = 0;
    if (!gun0) {
        c0x = -((e.cur[0][0] * f) / l.z);
        c0y = -((e.cur[0][1] * f) / l.z);
    }
    if (!gun1) {
        c1x = -((e.cur[1][0] * f) / l.z);
        c1y = -((e.cur[1][1] * f) / l.z);
    }
    const float r = F32(w.exe, 0x004ECB84);
    auto near = [&](float ax, float ay) {
        const float dy = sy - ay, dx = sx - ax;
        return std::sqrt(dy * dy + dx * dx) <= r;
    };
    if ((near(c0x, c0y) && e.slot[0] != -1 && e.done[0] == 0) || (near(c1x, c1y) && e.slot[1] != -1 && e.done[1] == 0)) {
        l.glow = l.glow + Bits(0x3CA3D70A);
        if (Bits(0x3E99999A) < l.glow) l.glow = Bits(0x3E99999A);
    } else if (0.0f < l.glow) {
        l.glow = l.glow - Bits(0x3CA3D70A);
        if (l.glow < 0.0f) l.glow = 0.0f;
    }
    if (0 < l.spin) {
        l.rot += l.spin;
        l.spin -= 0x140;
        if (l.spin < 1) {
            l.spin = 0;
            l.rot = 0;
        }
    }
    LetterDraw(w, e, l.x, l.y, l.z, l.scale + l.glow, l.rot, l.idx);
    k.viewPos[0] = l.x;
    k.viewPos[1] = l.y;
    k.viewPos[2] = l.z;
    w.RegisterTarget(&k);  // 0x00405160
}

// 0x00480D90 (sub-state 9): Mem_Init, the ranking rows, the name-entry root 0x00482050, then 0x00481400 at once
static void NameEntryFrame(World::Impl& w);

static void NameEntryStart(World::Impl& w) {
    EndingState& e = ES(w);
    w.ts.Reset();  // Mem_Init 0x004A7310 (the serial counter continues)
    w.resQueue.clear();
    if (w.residency) {  // 0x0041D510 / 0x0041D540 (models freed), pol 0x79 / 0x7A, texture 0x168, models 0x38 / 0x7ED, flush
        w.modelLoaded.assign(0x4000, 0);
        w.ResetTexFiles();
        w.ProcessRequest({ 0x52, 1 });
        w.ProcessRequest({ 0x52, 0x79 });
        w.ProcessRequest({ 0x52, 0x7A });
        w.ProcessRequest({ 0x54, 0x168 });
        w.ProcessRequest({ 0x50, 0x38 });
        w.ProcessRequest({ 0x50, 0x7ED });
    }
    w.Bgm(0, 0xFFFFFFFFu, 0x10000006, 0);  // 0x0041D3D0 / 0x0041D450
    e.digitScale = 0.25f;  // 0x00481FE0
    e.alpha = 0.0f;        // 0x00482040(0)
    e.digitStep = 0.2f;
    w.flags5900 &= ~0x10u;
    e.len[0] = e.len[1] = 0;
    e.fade = 0;
    e.wave = 0;
    e.fadeCount = 0x3C;
    e.done[0] = e.done[1] = 0;
    const int gameType = w.runner->cfg.gameType;
    if (gameType == 3 && w.modes) {
        // boss mode: state 8 for a player whose time qualifies (0x004157F0), else 9; 0x00481180 inserts the time of a
        // single qualifying player (0x00481300); 0x009C8E00 = 1
        for (int p = 0; p < 2; ++p) w.player[p].state = static_cast<int16_t>(RankingSlot(w, p) != -1 ? 8 : 9);
        w.canShoot = 1;
        const int both = (w.player[1].state != 9 ? 2 : 0) | (w.player[0].state != 9 ? 1 : 0);
        if (both == 1) {
            BossTimeInsert(w, e, 0);
            e.slot[1] = -1;
            e.done[1] = 1;
        } else if (both == 2) {
            BossTimeInsert(w, e, 1);
            e.slot[0] = -1;
            e.done[0] = 1;
        }
    } else if (gameType < 2) {
        for (int p = 0; p < 2; ++p)
            if (w.player[p].state != 9) w.player[p].state = 8;
    }
    // 0x00481090 (game types 0 / 1)
    const int both = (w.player[1].state != 9 ? 2 : 0) | (w.player[0].state != 9 ? 1 : 0);
    if (gameType >= 2) {
    } else if (both == 2) {
        RankInsert(w, e, 1);
        e.slot[0] = -1;
        e.done[0] = 1;
    } else if (both == 3) {
        const int s0 = w.player[0].score, s1 = w.player[1].score;
        if (s1 < s0) {
            RankInsert(w, e, 0);
            RankInsert(w, e, 1);
        } else if (s0 < s1) {
            RankInsert(w, e, 1);
            RankInsert(w, e, 0);
        } else if (s0 < Table(w)[9].score) {
            e.done[0] = e.done[1] = 1;
            e.slot[0] = e.slot[1] = -1;
        } else if (Table(w)[8].score <= s0) {
            RankInsert(w, e, 1);
            RankInsert(w, e, 0);
        } else {
            RankInsert(w, e, 0);
            e.slot[1] = -1;
            e.done[1] = 1;
        }
    } else {
        RankInsert(w, e, 0);
        e.slot[1] = -1;
        e.done[1] = 1;
    }
    // Task_InitRoot(0x00482050): 0x00482070 letters / panels / timer, 0x00482C00 views, 0x00414ED0 players, 0x00404480
    SetCurrentWorld(&w);
    const float k05 = 0.5f;
    for (int i = 0; i < 32; ++i) {
        LetterTask* k = w.NewTask<LetterTask>(TaskLetter, 0x004825B0);  // block 0x1314 + 0x38
        k->i = i;
        k->radius = Bits(0x3E428F5C);
        EndingState::Letter& l = e.letters[i];
        l = EndingState::Letter{};
        const int c = i % 8;
        l.x = c < 4 ? static_cast<float>(c) * k05 - F32(w.exe, 0x005692F8) : static_cast<float>((i - 4) % 8) * k05 + F32(w.exe, 0x0055D1A0);
        l.y = F32(w.exe, 0x0055D2C0) - static_cast<float>(i / 8) * F32(w.exe, 0x005692F4);
        l.idx = i;
        l.z = -5.0f;
        l.scale = 0.25f;
    }
    if (e.slot[0] != -1) w.NewTask<WTask>(TaskNamePanel, 0x00482310);
    if (e.slot[1] != -1) w.NewTask<WTask>(TaskNamePanel, 0x00482460);
    w.NewTask<WTask>(TaskNameTimer, 0x00482230);
    e.timerPos[0] = Bits(0xBE4CCCCD);
    e.timerPos[1] = Bits(0xBF99999A);
    e.timerPos[2] = -5.0f;
    e.timerFrames = 0;
    e.timerSecs = 0x3C;
    e.cursor[0] = 0;
    e.cur[0][0] = e.letters[0].x;
    e.cur[0][1] = e.letters[0].y;
    e.cursor[1] = 7;
    e.cur[1][0] = e.letters[7].x;
    e.cur[1][1] = e.letters[7].y;
    w.NewTask<WTask>(WorldTramp<&World::Impl::TaskMapViews>, 0x00482C40);  // 0x00482C00
    w.CameraReset();  // 0x004021D0
    w.runner->camera.activeCtx = 0;
    for (int k = 0; k < 3; ++k) w.runner->camera.ctx[0].pos[k] = 0.0f;
    w.runner->camera.ctx[0].pitch = w.runner->camera.ctx[0].yaw = w.runner->camera.ctx[0].roll = 0;
    for (int p = 0; p < 2; ++p) {  // 0x00414ED0: task function 0x00579CD0[state * 5] (state 8: 0x00414540)
        Task* tk = w.ts.Create(WorldTramp<&World::Impl::TaskPlayer>, std::make_unique<PlayerTask>());
        PlayerTask& d = tk->As<PlayerTask>();
        d.fn = w.exe.U32(0x00579CD0 + 0x14 * static_cast<uint32_t>(w.player[p].state));
        d.index = p;
        w.player[p].task = tk;
    }
    w.NewTask<WTask>(WorldTramp<&World::Impl::TaskCollision>, fn::kCollision);  // 0x00404480
    w.NewTask<WTask>(WorldTramp<&World::Impl::TaskHit>, fn::kHit);
    w.FrontResetBlink();  // 0x004067A0
    for (World::Impl::Player& P : w.player) P.aim[0] = P.aim[1] = 1000.0f;
    w.front.modeFrames = 0;
    w.subState = 10;
    NameEntryFrame(w);
}

// 0x00481400 (sub-state 10)
static void NameEntryFrame(World::Impl& w) {
    EndingState& e = ES(w);
    SetCurrentWorld(&w);
    for (int p = 0; p < 2; ++p)
        if (w.input[p].deviceMode == 6) {  // 0x00416940: the crosshair at the device position
            SpriteDef d;
            d.id = w.exe.S16(0x00579F58 + static_cast<uint32_t>(p * 4) * 2);
            d.x = static_cast<float>(w.devicePos[p][0]);
            d.y = static_cast<float>(w.devicePos[p][1]);
            d.alpha = 1.0f;
            d.flags = 0xA;
            w.hudSprites.push_back(d);
        }
    w.ts.RunFrame();  // Task_RunFrom(0x007DCF40)
    const uint32_t mf = static_cast<uint32_t>(w.front.modeFrames);
    for (int p = 0; p < 2; ++p) {  // the cursor letters of pad players
        if (w.player[p].inputKind == 1 || e.slot[p] == -1 || e.done[p] != 0) continue;
        if (mf < 0x1E) continue;
        const float x = w.focal * e.cur[p][0] * F32(w.exe, 0x00564428) + 320.0f;
        const float y = 240.0f - w.focal * e.cur[p][1] * F32(w.exe, 0x00564428);
        const float a = mf < 0x3C ? static_cast<float>(mf - 0x1E) * F32(w.exe, 0x0055E1FC) : 1.0f;
        SpriteDef d;
        d.id = w.exe.S16(0x00596B00 + static_cast<uint32_t>(p) * 2);
        d.x = x;
        d.y = y;
        d.alpha = a;
        d.flags = 10;
        d.source = 0x0041B020;  // (frontend_test: the name-entry sprites)
        w.hudSprites.push_back(d);
    }
    // pad cursor moves (P1 bits 0x10..0x80 / 4 start / 2; P2 << 16): only for pad players
    for (int p = 0; p < 2; ++p) {
        if (w.player[p].inputKind == 1 || e.slot[p] == -1 || e.done[p] != 0) continue;
        const uint32_t b = p == 0 ? w.pressed : (w.pressed >> 16);
        int& c = e.cursor[p];
        auto move = [&](int to) {
            w.Sound(0x1200A9);
            c = to;
            e.cur[p][0] = e.letters[c].x;
            e.cur[p][1] = e.letters[c].y;
        };
        if (b & 0x80) move(c + 1 > 0x1F ? 0 : c + 1);
        if (b & 0x40) move(c - 1 < 0 ? 0x1F : c - 1);
        if (b & 0x20) move(c + 8 > 0x1F ? c - 0x18 : c + 8);
        if (b & 0x10) move(c - 8 < 0 ? c + 0x18 : c - 8);
        if (b & 4) {
            e.letters[c].flags |= 8 | (2 << p);
            if (!(e.len[p] < 2 || 0x1D < c)) move(0x1F);
        } else if (b & 2) {
            move(0x1E);
        }
    }
    {  // "NAME ENTRY" sprite 0x741, the backdrop 0x38, the rotating panels 0x1421 / 0x7ED (0x00482C50: vertex wave)
        SpriteDef d;
        d.id = 0x741;
        d.x = 328.0f;
        d.y = 48.0f;
        d.alpha = 1.0f;
        d.flags = 10;
        d.source = 0x00481400;
        w.hudSprites.push_back(d);
        Mat4 m = Mat4::Identity();
        mx::Translate(m, Bits(0xBFAB851F), Bits(0xBEF0A3D7), Bits(0xC19D999A));
        mx::RotY(m, 0x612);
        mx::RotX(m, 0x2E1A);
        mx::RotZ(m, 0x280);
        w.Draw(0x38, m, 8, -1.0f, false, -1, true);
        VsDraw(w, 0x1421, Bits(0xBC23D70A), 0.0f, -6.0f, 0, Bits(0x3EB33333), -1.0f);
    }
    if (0x1D < w.front.modeFrames && w.front.modeFrames < 0x3C)
        e.alpha = static_cast<float>(w.front.modeFrames - 0x1E) * F32(w.exe, 0x0055E1FC);
    if (w.front.modeFrames == 0x3C) e.alpha = 1.0f;
    if (w.front.modeFrames < 0x3C) {
        e.wave = e.wave - F32(w.exe, 0x005692F0);
        if (e.wave < F32(w.exe, 0x005692EC)) e.wave = -32768.0f;
        const int mfi = w.front.modeFrames;
        VsDraw(w, 0x1421, Bits(0x3C23D70A), 0.0f, -6.0f, mfi * -0x222, Bits(0x3EB33333), -1.0f);
        if (mfi < 0x38) VsDraw(w, 0x1421, Bits(0x3C23D70A), 0.0f, -6.0f, (-4 - mfi) * 0x222, Bits(0x3EB33333), -1.0f);
        if (mfi < 0x3A) VsDraw(w, 0x1421, Bits(0x3C23D70A), 0.0f, -6.0f, (-2 - mfi) * 0x222, Bits(0x3EB33333), -1.0f);
        VsDraw(w, 0x7ED, Bits(0x3C23D70A), 0.0f, -6.0f, (-0x10 - mfi) * 0x222, Bits(0x3EB33333), -1.0f);
    } else {
        VsDraw(w, 0x1421, Bits(0x3C23D70A), 0.0f, -6.0f, 0x8000, Bits(0x3EB33333), -1.0f);
    }
    ++w.front.modeFrames;
    if (e.fade != 0 || (e.done[0] == 1 && e.done[1] == 1)) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, 0.0f, 0.0f, Bits(0xBDCCCCCD));
        mx::Scale(m, 200.0f, 400.0f, 1.0f);
        w.Draw(0x93E, m, 8, static_cast<float>(0x3C - e.fadeCount) * F32(w.exe, 0x0055CB80), false, -1, true);
        --e.fadeCount;
    }
    if (e.fadeCount == 0) {
        for (int p = 0; p < 2; ++p) {
            // 0x00481F70: a name on the list 0x005692D4 (4-byte strings up to a 0xFF byte; strcmp 0x00481FB0) becomes
            // "HOD". (0x00481F60(p) = the slot; the original also looks at slot -1, the entry before the table)
            if (e.slot[p] == -1) continue;
            EndingState::Entry& n = Table(w)[e.slot[p] & 15];
            bool listed = false;
            for (uint32_t a = 0x005692D4; !listed; a += 4) {
                char s[5] = {};
                const uint32_t v = w.exe.U32(a);
                memcpy(s, &v, 4);
                if (static_cast<uint8_t>(s[0]) == 0xFF) break;
                char nm[5] = {};
                memcpy(nm, n.name, 4);
                listed = strcmp(nm, s) == 0;
            }
            if (listed) {
                n.name[0] = 'H';
                n.name[1] = 'O';
                n.name[2] = 'D';
                n.name[3] = 0;
            }
        }
        // 0x0041C610: the tables into the save block (0x0041C5C0) + Game_Save 0x004011F0
        if (w.modes) w.modes->Store(w);
        if (w.runner->cfg.gameType == 3 && w.modes) {
            // boss mode: players still entering (8) -> 0, 0x0049DFD0, Game_SetState(6): the boss select again
            for (auto& P : w.player)
                if (P.state == 8) P.state = 0;
            if (w.frontEnd) w.front.modeRequest = 6;
            else w.SetGameMode(6);
            return;
        }
        // (0x009C8FB4 / 0x009C8FB5: 0x0041EBB0, 0x0049DFD0); 0x00406FD0 (0x00481F15): credits cleared
        w.cfg.freePlay = false;
        w.cfg.credits = 0;
        ModeNext(w);  // Mode_Next 0x0040E960
    }
}

// player tasks of the name entry: 0x00414540 (waits for a ranking row) -> 0x00414570 (a gun player shoots letters)
void World::Impl::NameEntryPlayer(Task& t) {
    PlayerTask& pt = t.As<PlayerTask>();
    const int p = pt.index;
    if (pt.fn == 0x00414540) {
        if (RankingSlot(*this, p) == -1) return;
        pt.fn = 0x00414570;
    }
    EndingState& e = ES(*this);
    if (e.done[p] != 1 && player[p].inputKind == 1) {
        player[p].ammo = 6;  // +0x1C
        PlayerTrigger(t);    // 0x00414940
        PlayerFlashes(p);    // 0x00416B00
    }
}

void World::Impl::EndingFrame() {
    if (subState == 7) {  // 0x00431550
        WorldConfig c = cfg;
        c.eventMode = 9;
        nextEventMode = -1;
        std::string err;
        if (!LoadStageTable(9, &err)) Note("ending: " + err);  // Evt_LoadTable 0x00413160
        c.scene = players == 2 ? 2 : (playersInGame != 0 ? 1 : 0);
        c.startFrame = frame;
        c.seedCounter = seedCounter;
        c.firstSerial = ts.NextSerial();
        c.carry = MakeCarry();
        for (int p = 0; p < 2; ++p) {
            if (c.carry.player[p].state != 9) c.carry.player[p].state = 7;
            c.playerState[p] = c.carry.player[p].state;
        }
        // (resources: 0x0041D510 / 0x0041D540 / pol 0x183 + 0x17D or 0x17B, textures 0xC / 0x15, motions 0x26 / 0x25 /
        // 0x31: the model residency of the ending is not modelled)
        endingRoot = true;
        const uint32_t keep5900 = flags5900;
        StartStage(c);  // Mem_Init, Task_InitRoot(0x00431740), Stage_ResetState (world.cpp)
        endingRoot = false;
        SetCurrentWorld(this);  // (StartStage leaves no current world)
        flags5900 = (keep5900 & ~2u) | 0x10;
        front.modeFrames = 0;  // 0x009A5C44
        subState = 8;
        ts.RunFrame();  // Task_RunFrom(root)
        EndingCheck(*this);
        return;
    }
    if (subState == 8) {  // 0x00431660
        // Port: either player's fresh trigger/click or Start/Enter finishes the credits.
        // Follow the normal ending exit so a qualifying score still reaches name entry.
        if (cfg.portFeatures && (pressed & 0x000C000Cu)) {
            EndingState& e = ES(*this);
            for (int p = 0; p < 2; ++p) e.cls[p] = static_cast<int16_t>(ScoreClass(*this, p));
            projCenter[0] = 320.0f;
            projCenter[1] = 240.0f;
            SoundStopAll();
            StageEnd();
            EndingCheck(*this);
            return;
        }
        ts.RunFrame();
        EndingCheck(*this);
        return;
    }
    if (subState == 9) {
        NameEntryStart(*this);
        return;
    }
    NameEntryFrame(*this);
}

}  // namespace game
