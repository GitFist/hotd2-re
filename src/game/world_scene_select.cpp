// game::World - the port's Scene Select (play.exe / hotd2.exe only, WorldConfig::portFeatures; nothing of this exists in
// the original, and it is off in the exactness tests):
//   - a ninth mode-select entry "Scene Select" (world_front.cpp TaskMenu, between Network and Exit), confirmed like Boss
//     mode (game type 3 with the arcade entry's credits, SceneSelectMenuConfirm);
//   - the select page: the boss select's own scene (st1evtbl scene 0x10: room, held camera preset, the agent 0x00497F80
//     with his speech bubble, BGM BOSS_MOD, the sounds and the carousel of 0x00425780) with the six chapters as its
//     entries, none locked (TaskSceneSelect replaces actor 0x6C's task 0x00425010 while Front::sceneSelect is set);
//   - the start: confirming a chapter runs the normal game start (game mode 6 sub-state 0 = GameStartFrame, as after
//     the mode select's Arcade entry: rank init, route record, civilian counters, lives from the setting, score 0, the
//     players who pressed start) at that stage's first scene (Front::sceneStart); the game then goes on through the
//     later stages and the ending exactly as a game that reached the stage.
// Text that the original has only as pre-rendered pictures (the title, the entry names, the bubble) is drawn with the
// glyph font of the subtitles / captions (0x0055E054, texture file 0x17B, loaded by every Stage_Start).

#include <cmath>
#include <cstring>
#include <string>

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

constexpr uint32_t kSource = 0x00425780;  // the sprites are tagged with the boss select's draw function (tests only)

void Spr(World::Impl& w, int id, float x, float y, float depth, float sx = 1, float sy = 1, int rot = 0, uint32_t flags = 0,
         const float* tint = nullptr, float alpha = 1.0f) {
    SpriteDef d;
    d.id = id;
    d.x = x;
    d.y = y;
    d.depth = depth;
    d.sx = sx;
    d.sy = sy;
    d.rot = rot;
    d.alpha = alpha;
    d.flags = flags;
    d.source = kSource;
    if (tint) {
        d.tinted = true;
        d.flags |= 0x2000;
        memcpy(d.tint, tint, sizeof(d.tint));
    }
    w.hudSprites.push_back(d);
}

// The subtitle / caption glyph font 0x0055E054[c] (hud.cpp TextLine: 16 px cells, lower case drawn as capitals here).
// x is the left edge, or the centre with `centred`; y the top of the capitals.
void GlyphText(World::Impl& w, float x, float y, float scale, const std::string& s, float depth, bool centred, const float* tint = nullptr) {
    const float adv = 16.0f * scale;
    if (centred) x = x - static_cast<float>(s.size()) * adv * 0.5f;
    for (char ch : s) {
        int c = static_cast<unsigned char>(ch);
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        if (c > 0 && c < 128) {
            const int id = w.exe.S16(0x0055E054 + static_cast<uint32_t>(c) * 2);
            if (id != 0) Spr(w, id, x, y, depth, scale, scale, 0, 0, tint);
        }
        x = x + adv;
    }
}

// pad masks per player count 0x009C7000 (the boss select's tables 0x00589140..)
uint32_t Mask(World::Impl& w, uint32_t table) {
    // The native tables contain only P1/P2 entries; online selections belong to the host,
    // including when both reserved players are active (playersInGame == 2).
    return w.exe.U32(table + static_cast<uint32_t>(w.cfg.portNetworkRoom ? 0 : w.playersInGame) * 4);
}
constexpr uint32_t kUp = 0x00589140, kDown = 0x00589148, kLeft = 0x00589150, kRight = 0x00589158, kTrig = 0x00589160,
                   kStart = 0x00589168, kBack = 0x00589170;

constexpr int kChapters = 6;
// the boss portraits 0x0055CBE4[k] stand for the chapters: the first boss of each stage (bosses 0, 1, 3, 5, 7, 8 of
// 0x0055CBBC = stages 0..5)
constexpr int kPortraitBoss[kChapters] = { 0, 1, 3, 5, 7, 8 };
// the chapter cards' titles (0x007DCBA0.. are pictures: scr_chapter_st<n>.bin, one texture file per chapter)
const char* const kChapterName[kChapters] = { "A PRELUDE", "MUDDY", "DARKNESS", "DESPAIR", "DAWN", "ORIGINAL SIN" };

struct SceneSelect : EnemyTask {
    int16_t step = 0;     // as the boss select's +0x1310: 0 init, 2..5 settling (confirm / back need > 4)
    int sel = 0;          // chapter under the cursor
    int prevSel = 0;
    struct Slot {         // the carousel (0x007DC818 layout: x, brightness, entry)
        float x = 0, a = 0;
        int16_t id = 0;
    } slots[kChapters];
    int16_t cur = 0, prev = 0, slide = 0, slideCount = 0;  // (0x007DC89A / 90 / 94 / 92: 1 left, 2 right, 10 frames)
    int8_t repeat = 0, repeatDir = 0;
};

// 0x00425BD0 without the grade marks: frame 0xAF1, the chapter's portrait tinted by the slot's brightness
void Entry(World::Impl& w, int k, float x, float bright) {
    const ExeImage& exe = w.exe;
    w.LightColor(bright, bright, bright);  // 0x004AA0A0 (the tint of flags-0x2000 sprites)
    Spr(w, 0xAF1, x + F32(exe, 0x0055CC50), 90.0f, 100.0f);
    const int boss = kPortraitBoss[k];
    uint32_t flags = 0x2000;
    float px = x + F32(exe, 0x0055CC4C);
    if (boss == 2 || boss == 4 || boss == 6) {  // (mirrored second portraits; not used by the chapter list)
        flags = 0x2010;
        px = px - F32(exe, 0x0055CC48);
    }
    const float tint[3] = { bright, bright, bright };
    Spr(w, static_cast<int>(exe.U32(0x0055CBE4 + static_cast<uint32_t>(boss) * 4)), px, 130.0f, 90.0f, 1, 1, 0, flags, tint);
}

// 0x00425780 for the chapter list: background, title, the carousel (slides 10 frames on a change: 21 px per frame,
// brightness +-0.07), cursor star, the entry's name, arrows, the agent's bubble with the page's text
void SceneSelectDraw(World::Impl& w, SceneSelect& b) {
    const ExeImage& exe = w.exe;
    int id = 0xADB;
    for (int y = 0; id < 0xAEF; y += 0x80)
        for (int x = 0; x < 0x280; x += 0x80) Spr(w, id++, static_cast<float>(x), static_cast<float>(y), 200.0f);
    GlyphText(w, 320.0f, 36.0f, 1.5f, "SCENE SELECT", 1.0f, true);  // (0xB30 "BOSS MODE" at (320, 54))
    if (b.slide == 1 || b.slide == 2) {
        ++b.slideCount;
        const float da = F32(exe, 0x0055CC44), dx = F32(exe, 0x0055CC40);
        // the window of the ring: six entries 0xD2 apart = 1260 px (the boss select's 10 entries: 2100, -420..1470)
        const float width = 1260.0f, lo = F32(exe, 0x0055CC3C), hi = lo + width;
        for (int k = 0; k < kChapters; ++k) {
            SceneSelect::Slot& s = b.slots[k];
            if (k == b.cur) s.a = s.a + da;
            if (k == b.prev) s.a = s.a - da;
            if (b.slide == 1) {
                s.x = s.x - dx;
                if (s.x < lo) s.x = s.x + width;
            } else {
                s.x = s.x + dx;
                if (hi < s.x) s.x = s.x - width;
            }
        }
        if (b.slideCount == 10) b.slide = b.slideCount = 0;
    }
    for (int k = 0; k < kChapters; ++k) Entry(w, b.slots[k].id, b.slots[k].x, b.slots[k].a);
    for (int c = 0; c < 3; ++c) w.light.color[c] = w.env.lightColor[c];
    {  // 0x004AA0A0(1, 1, 0.2): the cursor star pulses (x87 sin of 0x009A32A0 % 60)
        const float arg = static_cast<float>(static_cast<double>(static_cast<int>(w.seedCounter % 0x3C) * 0x444) * F64(exe, 0x004C4370));
        const float s = static_cast<float>(std::sin(static_cast<double>(arg)));
        const float sc = static_cast<float>(s * F32(exe, 0x004C4C88)) + static_cast<float>(F64(exe, 0x004ECB70));
        const float tint[3] = { 1.0f, 1.0f, Bits(0x3E4CCCCD) };
        Spr(w, 0xAEF, 322.0f, 194.0f, Bits(0x42006666), sc, sc, 0, 0x200A, tint);
    }
    {  // the entry's name (the boss names 0x0055CC0C[k] at (321, 100) are pictures): chapter number and the card's title
        const float yellow[3] = { 1.0f, Bits(0x3F59999A), Bits(0x3E4CCCCD) };
        GlyphText(w, 321.0f, 90.0f, 1.0f, "CHAPTER " + std::to_string(b.sel + 1), 65.0f, true, yellow);
        GlyphText(w, 321.0f, 270.0f, 0.8f, kChapterName[b.sel], 65.0f, true);
    }
    const uint32_t u = w.seedCounter % 0x3C;
    if (u < 0x2D) {
        const float alpha = u < 8 ? 1.0f : (F32(exe, 0x005308DC) - static_cast<float>(u)) * F32(exe, 0x005308D8);
        Spr(w, 0xAFF, 100.0f, 136.0f, 2.0f, 1, 1, 0, 0, nullptr, alpha);
        Spr(w, 0xAFF, 542.0f, 136.0f, 2.0f, 1, 1, 0, 0x17, nullptr, alpha);
    }
    // the bubble 0xB28 and, in place of the caption picture 0xB1B (195, 380), the page's text
    Spr(w, 0xB28, 144.0f, 365.0f, Bits(0x41F80000), 1, 1, 0, 9);
    const float textDepth = Bits(0x41F7D70A);
    GlyphText(w, 200.0f, 322.0f, 1.0f, "THIS IS THE SCENE SELECT.", textDepth, false);
    GlyphText(w, 200.0f, 350.0f, 1.0f, "CHOOSE THE CHAPTER TO", textDepth, false);
    GlyphText(w, 200.0f, 378.0f, 1.0f, "START THE GAME FROM.", textDepth, false);
}

void TaskSceneSelect(Task& tk) {
    World::Impl& w = *CurrentWorld();
    SceneSelect& b = tk.As<SceneSelect>();
    const ExeImage& exe = w.exe;
    if (b.step == 0) {
        // as 0x00425010's init: textures 0x14E / 0x16F / 0x173, pol 0xBF, model 0x1A4C, motion file 0x30, BGM BOSS_MOD
        for (const World::Impl::Request& r : { World::Impl::Request{ 0x54, 0x14E }, World::Impl::Request{ 0x54, 0x16F },
                                               World::Impl::Request{ 0x54, 0x173 }, World::Impl::Request{ 0x52, 0xBF },
                                               World::Impl::Request{ 0x50, 0x1A4C }, World::Impl::Request{ 0x56, 0x30 } })
            w.ProcessRequest(r);
        w.front.dimCounter = 0;
        w.Sound(0x10000024);
        b.actorSlot = -1;
        b.slot120 = -1;
        b.anim.kind = 0x3A;
        b.anim.motion[0] = 0x31A;
        w.EnemyInitPose(b);
        b.nodeCb = 0x00411050;
        b.postRootFn = [](World::Impl&, EnemyTask&) {};
        b.anim.rotOrder = 1;
        b.anim.nodes[5].model = 0x15A3;
        b.anim.drawFlags &= ~2u;
        w.attackers = 1;  // 0x009C7006: the scene script waits (op 0x43)
        w.flags5900 |= 0x18;
        b.sel = b.prevSel = 0;
        b.cur = 0;
        for (int i = 0; i < kChapters; ++i) {
            SceneSelect::Slot& s = b.slots[i];
            s.id = static_cast<int16_t>(i);
            const float x = static_cast<float>(i * 0xD2);
            s.x = x > F32(exe, 0x0055CC3C) + 1260.0f ? x - 1260.0f : x;  // ring window -420..840
            s.a = i == 0 ? 1.0f : 0.3f;
        }
        b.step = 2;
    } else if (b.step > 0 && b.step < 5) {
        ++b.step;
    }
    SelectPointer(w, Mask(w, kTrig), false, b.step > 4 && b.slide == 0);
    // L / R (or up / down) held 60 frames: auto repeat
    const uint32_t mR = Mask(w, kRight) | Mask(w, kDown), mL = Mask(w, kLeft) | Mask(w, kUp);
    if ((w.held & mR) == 0 && (w.held & mL) == 0) {
        b.repeat = 0;
        b.repeatDir = 0;
    } else if (++b.repeat == 0x3C) {
        b.repeat = 0;
        if (w.held & mR) b.repeatDir = 1;
        else if (w.held & mL) b.repeatDir = 2;
    }
    if (b.slide == 0) {
        if ((w.pressed & mR) || b.repeatDir == 1) {
            w.Sound(0xA9);
            if (kChapters - 1 < ++b.sel) b.sel = 0;
            b.slide = 1;
            b.prev = b.cur;
            if (kChapters - 1 < ++b.cur) b.cur = 0;
            b.slideCount = 0;
        } else if ((w.pressed & mL) || b.repeatDir == 2) {
            w.Sound(0xA9);
            if (--b.sel < 0) b.sel = kChapters - 1;
            b.slide = 2;
            b.prev = b.cur;
            if (--b.cur < 0) b.cur = kChapters - 1;
            b.slideCount = 0;
        }
    }
    // the agent: 0x31A (walk in) -> 0x31C (every entry is open: no 0x320 head shake)
    if (b.anim.motion[0] == 0x31A && b.anim.tick[0] == w.MotionTicks(0x31A) - 1) w.AnimBlendTo(b, 0x31C, 0, 2);
    SceneSelectDraw(w, b);
    SelectPointerDraw(w, Mask(w, kTrig));
    if ((w.pressed & (Mask(w, kTrig) | Mask(w, kStart))) && 4 < b.step) {
        w.Sound(0x80000000);  // the BGM stops (as the boss select's confirm)
        w.Sound(0x121A9);
        SceneSelectStart(w, b.sel);
        w.ts.KillCurrent();
        return;
    }
    if ((w.pressed & Mask(w, kBack)) && 4 < b.step) {  // back to the mode select, as the boss select
        w.Sound(0x500A9);
        w.front.sceneSelect = false;
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

void PortGlyphText(World::Impl& w, float x, float y, float scale, const std::string& s, float depth, bool centred, const float* tint) {
    GlyphText(w, x, y, scale, s, depth, centred, tint);
}
void PortSprite(World::Impl& w, int id, float x, float y, float depth, float sx, float sy, int rot, uint32_t flags, const float* tint,
                float alpha) {
    Spr(w, id, x, y, depth, sx, sy, rot, flags, tint, alpha);
}

// Actor 0x6C of st1evtbl scene 0x10 while the port's scene select is up: its task is the chapter list
bool SpawnSceneSelect(World::Impl& w, Task& t, SpawnTask& s) {
    if (!w.cfg.portFeatures || !w.front.sceneSelect) return false;
    auto d = std::make_unique<SceneSelect>();
    d->fn = 0x00425010;
    memcpy(d->pos, s.rec.pos, 12);
    memcpy(d->rot, s.rec.rot, 12);
    t.data = std::move(d);
    t.fn = TaskSceneSelect;
    TaskSceneSelect(t);
    return true;
}

// The mode select's Scene Select entry: Boss mode's game (type 3: st1evtbl scene 0x10, the select's room) with the
// Arcade entry's credits (0x00496B70(0): the setting + 1, or free play), so that the game started from the page has
// them; the player's task sees the same trigger / start and joins (Player_TryJoin -> Game_SetState(6))
void SceneSelectMenuConfirm(World::Impl& w) {
    w.front.menuConfirmed = true;
    const int credits = ModesCredits(w, 0);
    if (credits == -1) {
        w.cfg.freePlay = true;
        w.cfg.credits = 0;
    } else {
        w.cfg.freePlay = false;
        w.cfg.credits = credits;
    }
    w.front.gameType = 3;
    if (w.modes) w.modes->a2bbc = 0;
    w.front.cheatFlag = 0;
    w.front.sceneSelect = true;
    w.Sound(0x121A9);
}

// 0x009A2BC0 at a stage's Stage_Start in a normal game: 0 for stage 1 (the mode select leaves 0), then the value the
// previous stage's end leaves (0x0045F000 past the last scene: the scene table 0x00597890[stage - 1] row (count - 1)'s
// next scene): 0, 7, 0, 0, 0 for stages 2..6 (the auto-play captures' W lines at frames 14659, 29736, 44107, 56007,
// 70806; stage3.md: "scene 7" is the stage-3 opening's own index)
int SceneSelectFirstScene(int stage) {
    static const int kFirst[kChapters] = { 0, 0, 7, 0, 0, 0 };
    return stage >= 0 && stage < kChapters ? kFirst[stage] : 0;
}

// A chapter confirmed: the next frame is the normal game start (game mode 6 sub-state 0, GameStartFrame) of an
// arcade game (type 0) at that stage: the players in the select (joined with start / trigger) start as after the
// Arcade entry's confirm (state 0 = start pressed: row 0 of 0x00579CD0, lives from the setting, score 0), the others
// stay out (9); the credits are what the select left (the Arcade entry's, less the starts taken).
void SceneSelectStart(World::Impl& w, int stage) {
    w.front.sceneSelect = false;
    w.front.sceneStart = stage;
    w.front.gameType = 0;
    for (auto& P : w.player) P.state = (P.state == 9 || P.state == 6) ? 9 : 0;
    w.subState = 0;
}

}  // namespace game
