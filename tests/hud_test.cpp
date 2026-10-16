// hud_test <game dir> <capture with S and W lines (HOTD2_SCENECAP_SPRITES=1 + gameplay W dump)> [--verbose N]
// Validates game/hud.* against the ORIGINAL game's Spr_Draw definitions. Each captured S line is assigned to a
// layout by its caller return addresses; per frame and layout, the definitions produced by the port from the
// frame's game state (W lines; see BuildState for every field's source and timing) must equal the captured ones
// exactly and in order (id, x, y, depth, sx, sy, rot, alpha, blend, flags; words 6-9 are not read by Spr_Draw).
// Layouts whose drawing task has no state in the capture are started at their first captured frame (spawn
// detection) and then simulated; those that need values missing from W use documented defaults (capture requests
// in notes/agents/hud.md).

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "game/exe_image.h"
#include "game/hud.h"
#include "game/sprite.h"

namespace {

struct SLine {
    uint32_t w[14];
    std::vector<uint32_t> callers;
    game::SpriteDef def;
};

struct Frame {
    int number = -1;
    std::vector<SLine> s;
    std::map<std::string, std::vector<uint8_t>> w;  // W key -> little-endian bytes
    bool hasW = false;
};

std::vector<uint8_t> HexBytes(const std::string& h) {
    std::vector<uint8_t> b(h.size() / 2);
    for (size_t i = 0; i < b.size(); ++i) b[i] = static_cast<uint8_t>(std::stoul(h.substr(i * 2, 2), nullptr, 16));
    return b;
}

template <class T>
T Get(const std::vector<uint8_t>& b, size_t off) {
    T v{};
    if (off + sizeof(T) <= b.size()) memcpy(&v, b.data() + off, sizeof(T));
    return v;
}

bool SameDef(const game::SpriteDef& a, const game::SpriteDef& b) {
    return a.id == b.id && a.x == b.x && a.y == b.y && a.depth == b.depth && a.sx == b.sx && a.sy == b.sy && a.rot == b.rot &&
           a.alpha == b.alpha && a.blend == b.blend && a.flags == b.flags;
}

std::string DefStr(const game::SpriteDef& d) {
    char b[200];
    snprintf(b, sizeof(b), "id %X (%.9g,%.9g) d %.9g s %.9g,%.9g rot %d a %.9g bl %d fl %X", d.id, d.x, d.y, d.depth, d.sx, d.sy, d.rot, d.alpha,
             d.blend, d.flags);
    return b;
}

// Layout of a captured S line from its caller return addresses (outermost meaningful one).
enum Layout {
    kNone, kCrosshair, kAmmo, kLives, kCredit, kPressStart, kContinue, kSprFxGameOver, kSprFxOther, kRouteMap, kSubtitle,
    kTitle, kModeSelect, kTutLabel, kTutTorch, kTutBullet, kTutReload, kTutCaption, kRetained, kWait, kOut, kPause, kUnknown, kCount
};
const char* kLayoutName[kCount] = { "-", "crosshair 0x004169C0", "ammo/reload 0x004177D0", "lives 0x004174A0", "credit 0x00406CE0", "press start 0x004067D0",
                                    "continue 0x00460530", "game over fx 0x00460CD0", "chapter card 0x00436AD0", "route map 0x00461180",
                                    "subtitle 0x00435AA0", "title logo 0x004960A0", "mode select 0x00496890", "tutorial label 0x0049AD70",
                                    "tutorial torches 0x0049AB80", "tutorial bullets 0x0049AA00", "tutorial reload 0x0049AC40",
                                    "tutorial caption 0x0049ADC0", "player continue 0x00414280 (retained)", "player wait 0x00414690",
                                    "player out 0x00416900", "pause 0x0040E500", "unknown" };

Layout Classify(const SLine& s) {
    const auto& c = s.callers;
    if (c.empty()) return kUnknown;
    const uint32_t a = c[0], b = c.size() > 1 ? c[1] : 0;
    if (a == 0x00416AE7) return kCrosshair;
    if (a >= 0x00406860 && a < 0x00406CE0) return a == 0x00406851 ? kPressStart : kCredit;
    if (a == 0x00406851) return kPressStart;
    if (a == 0x00499F7D) return (s.def.id >= 0x43A && s.def.id <= 0x43D) ? kSprFxGameOver : kSprFxOther;
    if (a == 0x0041CF48) return kRetained;
    if (a == 0x0041C74D) {  // Spr_DrawSimple
        if (b >= 0x004177D0 && b < 0x00418147) return kAmmo;
        if (b >= 0x004174A0 && b < 0x004177C5) return kLives;
        if (b == 0x00460621 || b == 0x00460658) return kContinue;
        if (b == 0x00461358) return kRouteMap;
        if (b == 0x0043699A || b == 0x00435B18) return kSubtitle;
        if (b >= 0x004960A0 && b < 0x00496180) return kTitle;
        if (b == 0x00496A84) return kModeSelect;
        if (b == 0x0049AD97) return kTutLabel;
        if (b == 0x0049ABE0) return kTutTorch;
        if (b == 0x0049AA3F) return kTutBullet;
        if (b == 0x0049AC88 || b == 0x0049ACFB) return kTutReload;
        if (b == 0x0049ADE7) return kTutCaption;
        if (b == 0x0041471A) return kWait;
        if (b == 0x0041692E) return kOut;
        if (b == 0x0040E6D2) return kPause;
    }
    return kUnknown;
}

struct Stats {
    int match = 0, differ = 0, defs = 0;
    int shown = 0;
    std::map<std::string, int> firstDiff;
};

int g_verbose = 3;

void Compare(Stats& st, Layout l, int frame, const std::vector<game::SpriteDef>& mine, const std::vector<const SLine*>& cap) {
    if (mine.empty() && cap.empty()) return;
    bool ok = mine.size() == cap.size();
    size_t bad = 0;
    for (size_t i = 0; ok && i < mine.size(); ++i)
        if (!SameDef(mine[i], cap[i]->def)) { ok = false; bad = i; }
    if (!ok && mine.size() != cap.size()) bad = std::min(mine.size(), cap.size());
    st.defs += static_cast<int>(cap.size());
    if (ok) { ++st.match; return; }
    ++st.differ;
    if (st.shown++ < g_verbose) {
        printf("  [%s] frame %d: port %zu defs, capture %zu\n", kLayoutName[l], frame, mine.size(), cap.size());
        if (bad < mine.size()) printf("    port[%zu]    %s\n", bad, DefStr(mine[bad]).c_str());
        if (bad < cap.size()) printf("    capture[%zu] %s\n", bad, DefStr(cap[bad]->def).c_str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: hud_test <game dir> <capture> [--verbose N]\n");
        return 2;
    }
    for (int i = 3; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "--verbose")) g_verbose = atoi(argv[i + 1]);
    game::ExeImage exe;
    if (!exe.Load(std::string(argv[1]) + "/Hod2.exe")) return 2;
    game::SubtitleTables subs;
    subs.Load(exe);

    // --- unit checks of static data the port embeds (tables read from the exe) ---
    int unitBad = 0;
    {
        const int16_t xh[8] = { 0xAA8, 0xAAA, 0xAAB, 0xAAC, 0xAA9, 0xAAD, 0xAAE, 0xAAF };
        for (int i = 0; i < 8; ++i) unitBad += exe.S16(0x00579F58 + i * 2) != xh[i];
        for (int m = 0; m < 16; ++m) {
            const bool on = exe.S16(0x004C4B90 + m * 4) != 0;
            game::HudState s;
            s.gameMode = m;
            s.bootFrames = 10;
            s.blinkCounter = 0;
            unitBad += on != !game::HudPressStart(s).empty();
        }
        const auto map = game::RouteMapBackground(0);
        for (int l = 0; l < 4; ++l) unitBad += map[l * 75].id != exe.S16(0x005679FC + l * 2);
        if (unitBad) printf("UNIT: %d static-table mismatches\n", unitBad);
    }

    std::ifstream cap(argv[2]);
    if (!cap) return 2;

    Stats stats[kCount];
    std::map<uint32_t, int> unknownCallers;

    // Persistent simulated state across frames.
    game::HudState sim;            // carries ammoZoom
    Frame prev, cur;
    bool continueActive = false;
    int32_t continueCount = 0;
    bool goActive = false;
    int goT = 0;
    game::SpriteFxList goList;
    int lastSubFrame = -10;
    std::vector<game::Subtitle> subCands;
    int tutTorchStart = -1, tutReloadStart = -1, tutReloadLast = -100, tutTorchLast = -100;
    int modeValue = -1, modeSince = 0;
    bool cardActive = false;
    int cardFrames = 0;
    game::ChapterCard card;
    int stateSince[2] = { 0, 0 }, stateValue[2] = { -1, -1 };  // frame at which W's player state last changed
    int frames = 0, framesW = 0;
    int32_t blinkHeld = -1;  // 0x005A4D44 frozen by a pause (-1: not paused)

    auto processFrame = [&](Frame& f, const Frame& pf) {
        ++frames;
        std::vector<const SLine*> byLayout[kCount];
        for (const auto& s : f.s) {
            const Layout l = Classify(s);
            byLayout[l].push_back(&s);
            if (l == kUnknown) ++unknownCallers[s.callers.empty() ? 0 : (s.callers.size() > 1 ? s.callers[1] : s.callers[0])];
        }
        const int n = f.number;

        // --- attract layouts that need no state ---
        Compare(stats[kTitle], kTitle, n, byLayout[kTitle].empty() ? std::vector<game::SpriteDef>{} : game::TitleLogoSprites(), byLayout[kTitle]);
        Compare(stats[kModeSelect], kModeSelect, n,
                byLayout[kModeSelect].empty() ? std::vector<game::SpriteDef>{} : std::vector<game::SpriteDef>{ game::ModeSelectBackSprite() },
                byLayout[kModeSelect]);

        // Mode age: frames since W's mode value changed. 0x009A5C44 (the mode's frame counter, reset by the mode
        // init one frame after the switch) is not captured: modelled as age - 1.
        if (f.hasW) {
            const int m = Get<int16_t>(f.w["9C8E98"], 0);
            if (m != modeValue) modeValue = m, modeSince = n;
        }
        const int modeAge = n - modeSince;

        // PRESS START: blink counter = boot frame counter after this frame's increment (W 9A5C40, = frame + 1).
        {
            game::HudState s;
            // Mode at the start of the frame (the new mode's tasks start a frame after W shows it).
            s.gameMode = pf.hasW ? Get<int16_t>(pf.w.at("9C8E98"), 0) : f.hasW ? Get<int16_t>(f.w["9C8E98"], 0) : 3;
            s.bootFrames = !f.hasW ? 10 : f.w.count("9A5C44") ? Get<uint32_t>(f.w["9A5C44"], 0) : static_cast<uint32_t>(std::max(modeAge - 1, 0));
            s.blinkCounter = f.hasW ? Get<int32_t>(f.w["9A5C40"], 0) : n + 1;
            // Before W lines exist (boot / title) the mode is unknown: only check visibility on frames with the logo.
            std::vector<game::SpriteDef> mine;
            if (f.hasW || !byLayout[kTitle].empty()) mine = game::HudPressStart(s);
            else if (!byLayout[kPressStart].empty()) mine = game::HudPressStart(s);  // demo frames: blink only
            Compare(stats[kPressStart], kPressStart, n, mine, byLayout[kPressStart]);
        }

        if (f.hasW && pf.hasW) {
            ++framesW;
            // --- BuildState: HudState as the player tasks see it in frame n ---
            game::HudState s = sim;
            auto& W = f.w;
            const auto& PW = pf.w;
            // Mode, letterbox state and the can-shoot flag as the player tasks see them: the previous frame's values
            // (Letterbox_Task runs after the player tasks; mode changes are applied later in the frame).
            s.gameMode = Get<int16_t>(PW.at("9C8E98"), 0);
            s.canShoot = Get<int32_t>(PW.at("9C8E00"), 0);  // written only by Letterbox_Task
            {
                // Letterbox state: Letterbox_Task's own transitions (0/3/5 -> 4, 1/6 -> 2) happen after the player
                // tasks (previous frame's value), event commands (-> 1, 3, and the one-frame 0 / 5 / 6) before them.
                const int8_t now = static_cast<int8_t>(Get<uint8_t>(W["9CA0F4"], 0)), was = static_cast<int8_t>(Get<uint8_t>(PW.at("9CA0F4"), 0));
                s.letterbox = was;
                if ((now == 1 || now == 3) && now != was) s.letterbox = now;
                else if (now == 4 && was != 4 && was != 3 && was != 0 && was != 5) s.letterbox = 5;
                else if (now == 2 && was != 2 && was != 1 && was != 6) s.letterbox = 6;
            }
            s.weaponMode = W.count("9CA08C") ? Get<int32_t>(W["9CA08C"], 0) : 0;  // not captured: normal weapons
            // 0x009A32A0 (incremented by Game_Update before the tasks); captures without it: 0x009A5C40, equal to it in
            // every capture that has both.
            s.torchCounter = Get<uint32_t>(W.count("9A32A0") ? W["9A32A0"] : W["9A5C40"], 0);
            // 0x005A4D44 = boot counter at the previous frame's PRESS START; held while paused (the mode update is skipped).
            s.blinkCounter = blinkHeld >= 0 ? blinkHeld : Get<int32_t>(PW.at("9A5C40"), 0);
            s.bootFrames = 10;
            s.flags5900 = W.count("9A5900") ? Get<uint32_t>(W["9A5900"], 0) : 3;  // Game_InitPlayers sets 3
            s.freePlay = W.count("9C8E70") ? Get<int32_t>(W["9C8E70"], 0) : 0;
            s.perPlayerCredits = W.count("9C8E74") ? Get<int32_t>(W["9C8E74"], 0) : 0;
            s.coinsPerCredit = W.count("9C8E78") ? Get<int32_t>(W["9C8E78"], 0) : 1;
            s.coinsToContinue = W.count("9C8E7C") ? Get<int32_t>(W["9C8E7C"], 0) : 1;
            const auto& gun = W["9C8FC0"];
            const bool tasksAlive = Get<int16_t>(W["9C8E98"], 0) != 6 || modeAge >= 2;
            // Paused (0x009A1A00 == 1, not captured: seen as the PAUSE label): Game_UpdatePause 0x0040E500 draws the label and
            // Game_UpdateMode (the tasks) is skipped. The label's player (0x007C1792, not captured) is taken from its id.
            const bool paused = !byLayout[kPause].empty();
            if (!paused) blinkHeld = -1;
            else if (blinkHeld < 0) blinkHeld = s.blinkCounter;
            Compare(stats[kPause], kPause, n, paused ? game::HudPause(byLayout[kPause][0]->def.id == 0x8E8 ? 1 : 0) : std::vector<game::SpriteDef>{}, byLayout[kPause]);
            bool runTask[2];
            for (int p = 0; p < 2; ++p) {
                const auto& pb = W[p ? "9A5D90" : "9A5C60"];
                const auto& ppb = PW.at(p ? "9A5D90" : "9A5C60");
                game::HudPlayer& pl = s.player[p];
                const int16_t stateNow = Get<int16_t>(pb, 2), statePrev = Get<int16_t>(ppb, 2);
                // Player_Task runs for the state at the start of the frame; the player tasks are (re)created at
                // stage start (0x00460710) and first run two frames after the switch to mode 6.
                runTask[p] = statePrev == 5 && tasksAlive && !paused;
                pl.state = stateNow;
                // Lives as the HUD saw them: end of the previous frame (enemy hits come after the player tasks);
                // mode 5 forces 1.
                pl.lives = s.gameMode == 5 ? 1 : Get<int16_t>(ppb, 6);
                pl.aimX = Get<float>(pb, 0x10);
                pl.aimY = Get<float>(pb, 0x14);
                pl.ammo = Get<int16_t>(pb, 0x1C);    // trigger (same task, before the HUD) already applied
                pl.empty = Get<int16_t>(pb, 0x1E);
                pl.inputKind = static_cast<int8_t>(Get<uint8_t>(pb, 0x125));
                // Reload timer: the HUD increments it after drawing (wraps 601 -> 120).
                const int16_t tNow = Get<int16_t>(pb, 0x20), tPrev = Get<int16_t>(ppb, 0x20);
                const bool adv = runTask[p] && pl.lives > 0 && s.canShoot && (s.letterbox == 1 || s.letterbox == 2) && pl.empty && s.gameMode != 5 && s.gameMode != 9;
                pl.reloadTimer = adv ? ((tNow == 120 && tPrev > 600) ? tPrev : static_cast<int16_t>(tNow - 1)) : tNow;
                pl.gunValid = Get<int16_t>(gun, p * 0x28 + 0x10);
                pl.gunFired = Get<int16_t>(gun, p * 0x28 + 0x14);
                // Not captured: gun device (raw input launcher = mode 6), crosshair style 0; Input_GetGunPos = the
                // record's raw position (u16).
                pl.deviceMode = 6;
                pl.crosshairStyle = 0;
                pl.gunPosX = Get<uint16_t>(gun, p * 0x28 + 0);
                pl.gunPosY = Get<uint16_t>(gun, p * 0x28 + 2);
                pl.credits = W.count("9C8E60") ? Get<uint32_t>(W["9C8E60"], p * 8) : 5;   // not captured: 5 (as drawn)
                pl.coinState = W.count("9C8E60") ? Get<int32_t>(W["9C8E60"], p * 8 + 4) : 2;
            }

            // Player tasks (P1, P2) in task order: crosshair / ammo / lives (state 5), credit line (states 4 and 9).
            std::vector<game::SpriteDef> xh, am, lv, cr, wt, ot;
            for (int p = 0; p < 2; ++p) {
                if (runTask[p]) {
                    auto a = game::HudCrosshair(s, p);
                    xh.insert(xh.end(), a.begin(), a.end());
                    if (s.gameMode != 9 && s.gameMode != 5 && s.player[p].lives != 0) {
                        if (s.canShoot) {
                            auto b = game::HudAmmo(s, p);
                            am.insert(am.end(), b.begin(), b.end());
                            game::HudAmmoAdvance(s, p);
                        }
                        auto c = game::HudLives(s, p);
                        lv.insert(lv.end(), c.begin(), c.end());
                    }
                } else {
                    s.player[p].ammoZoom = 0;  // (only Hud_DrawAmmo touches it; reset when it runs again in state 2)
                }
                const int16_t st = Get<int16_t>(pf.w.at(p ? "9A5D90" : "9A5C60"), 2);
                if (!tasksAlive || paused) {
                } else if (st == 9) {
                    auto c = game::HudIdlePlayer(s, p);
                    cr.insert(cr.end(), c.begin(), c.end());
                } else if (st == 4 && s.player[p].state == 4 && n - stateSince[p] >= 2) {
                    // (state 4's first task frame is the init 0x00414200, which only switches to 0x00414280)
                    auto c = game::HudCreditLine(s, p);  // continue task 0x00414280 (while still continuing)
                    cr.insert(cr.end(), c.begin(), c.end());
                } else if ((st == 10 && n - stateSince[p] >= 2) || st == 6) {  // (state 10's first frame: init 0x00414680)
                    game::HudState ws = s;
                    ws.player[p].state = st;
                    auto c = st == 10 ? game::HudWaitPlayer(ws, p) : game::HudOutPlayer(ws, p);
                    (st == 10 ? wt : ot).insert((st == 10 ? wt : ot).end(), c.begin(), c.end());
                }
            }
            Compare(stats[kCrosshair], kCrosshair, n, xh, byLayout[kCrosshair]);
            Compare(stats[kAmmo], kAmmo, n, am, byLayout[kAmmo]);
            Compare(stats[kLives], kLives, n, lv, byLayout[kLives]);
            Compare(stats[kCredit], kCredit, n, cr, byLayout[kCredit]);
            Compare(stats[kWait], kWait, n, wt, byLayout[kWait]);
            Compare(stats[kOut], kOut, n, ot, byLayout[kOut]);
            for (int p = 0; p < 2; ++p) sim.player[p].ammoZoom = s.player[p].ammoZoom;
            for (int p = 0; p < 2; ++p) {
                const int16_t stNow = Get<int16_t>(W[p ? "9A5D90" : "9A5C60"], 2);
                if (stNow != stateValue[p]) stateValue[p] = stNow, stateSince[p] = n;
            }

            // Per-player CONTINUE (retained list, drawn at the end of the frame): continuing players past the
            // state-4 init frame while the global screen is not up (0x009C90A4 != 4; here: before the first frame
            // of the global screen). Count = player+0x68 of this frame.
            {
                std::vector<game::SpriteDef> pc;
                for (int p = 0; p < 2; ++p)
                    if (!paused && !continueActive && byLayout[kContinue].empty() && s.player[p].state == 4 && Get<int16_t>(pf.w.at(p ? "9A5D90" : "9A5C60"), 2) == 4 && n - stateSince[p] >= 2) {
                        auto v = game::ContinuePlayer(p, Get<int32_t>(W[p ? "9A5D90" : "9A5C60"], 0x68));
                        pc.insert(pc.begin(), v.begin(), v.end());  // later registrations are drawn first
                    }
                Compare(stats[kRetained], kRetained, n, pc, byLayout[kRetained]);
            }

            // CONTINUE: started at its first captured frame (0x9FFF), then simulated: the P1 / P2 button (0x009C9028
            // bits 4 / 0x40000; = the gun record's fired flag with the gun on screen; off-screen shots are reloads) jumps to the end of the current second.
            if (!byLayout[kContinue].empty() && !continueActive) {
                continueActive = true;
                continueCount = 0x9FFF;
            }
            if (continueActive) {
                if (W.count("9A2BB8")) continueCount = Get<int32_t>(W["9A2BB8"], 0) + 0x2D;  // captured (value after -0x2D)
                bool press = false;
                for (int p = 0; p < 2; ++p) press = press || (s.player[p].state == 4 && s.player[p].gunFired && s.player[p].gunValid);
                if (press && continueCount < 0x8000) continueCount = (continueCount & ~0xFFF) + 1;
                game::HudState cs = s;
                cs.continueCount = continueCount;
                Compare(stats[kContinue], kContinue, n, game::ContinueScreen(cs), byLayout[kContinue]);
                // Ends when the countdown runs out (Mode_Request(7)) or a player continued (0x009C90A4 = 2; seen
                // here as a continuing player back in state 5).
                continueCount -= 0x2D;
                bool joined = false;
                for (int p = 0; p < 2; ++p) joined = joined || (Get<int16_t>(PW.at(p ? "9A5D90" : "9A5C60"), 2) == 4 && s.player[p].state == 5);
                if (continueCount <= 0 || joined) continueActive = false;
            }
        }

        // GAME OVER effects: started at the first frame showing 0x43A with alpha 0 (tick 0), then simulated.
        {
            const auto& c = byLayout[kSprFxGameOver];
            if (!goActive && !c.empty() && c[0]->def.id == 0x43A && c[0]->def.alpha == 0) {
                goActive = true;
                goT = 0;
                goList.Clear();
            }
            // A mode switch (7 -> 3) kills every task, effective in the frame after W shows the new mode.
            if (goActive && pf.hasW && Get<int16_t>(pf.w.at("9C8E98"), 0) != 7) goActive = false;
            if (goActive && f.s.empty()) {
                // A frame without any sprite (route-map load, 4671 in play2): the tasks did not run.
            } else if (goActive) {
                if (goT <= 165) game::GameOverSpawn(goT, goList);
                ++goT;
                Compare(stats[kSprFxGameOver], kSprFxGameOver, n, goList.Run(), c);
                if (goT > 165 && goList.Empty()) goActive = false;
            } else {
                Compare(stats[kSprFxGameOver], kSprFxGameOver, n, {}, c);
            }
        }

        // Chapter card: started at its first captured frame (chapter from the first id), then simulated.
        {
            const auto& c = byLayout[kSprFxOther];
            const int16_t language = (f.hasW && f.w.count("9C911E")) ? Get<int16_t>(f.w["9C911E"], 0) : 0;
            if (!cardActive && !c.empty()) {
                cardFrames = 0;
                for (int ch = 0; ch < 6; ++ch)
                    if (game::ChapterCard::Start(ch).Id(0) <= c[0]->def.id && c[0]->def.id <= game::ChapterCard::Start(ch).Id(7) + (ch == 1 ? 0x21 : 0)) {
                        card = game::ChapterCard::Start(ch);
                        cardActive = true;
                        break;
                    }
            }
            if (cardActive) {
                Compare(stats[kSprFxOther], kSprFxOther, n, card.Run(language), c);
                ++cardFrames;
                // Skip (0x00434802): P1 START (0x009C9028 bit 2) once the countdown 180 is below 160, P2 START
                // (bit 0x20000) at any time. Not captured: taken as an off-screen trigger (raw position 0xFFFF).
                bool skip = false;
                if (f.hasW) {
                    const auto& gun = f.w["9C8FC0"];
                    for (int p = 0; p < 2; ++p)
                        if (Get<int16_t>(gun, p * 0x28 + 0x14) && Get<uint16_t>(gun, p * 0x28) == 0xFFFF && (p == 1 || cardFrames > 20)) skip = true;
                }
                if (card.counter > 171 || skip) cardActive = false;
            } else {
                Compare(stats[kSprFxOther], kSprFxOther, n, {}, c);
            }
        }

        // Route map: scroll 0x007DCCD8 (W if captured, else 0 = the value at the task's start).
        if (!byLayout[kRouteMap].empty()) {
            const int scroll = (f.hasW && f.w.count("7DCCD8")) ? Get<int16_t>(f.w["7DCCD8"], 0) : 0;
            Compare(stats[kRouteMap], kRouteMap, n, game::RouteMapBackground(scroll), byLayout[kRouteMap]);
        }

        // Subtitles: a run starts at the first frame with glyphs; candidates = groups whose first frame renders the
        // same; then every candidate is simulated (countdown, line changes, end) and eliminated on mismatch.
        {
            const auto& c = byLayout[kSubtitle];
            const int16_t language = (f.hasW && f.w.count("9C911E")) ? Get<int16_t>(f.w["9C911E"], 0) : 0;
            // Kill flags 0x009A2D74 (cutscene skip) / 0x009A2230: not captured yet (a skip ends the subtitle early).
            const bool paused = f.hasW && ((f.w.count("9A2D74") && Get<int32_t>(f.w["9A2D74"], 0)) || (f.w.count("9A2230") && Get<int32_t>(f.w["9A2230"], 0)));
            if (!c.empty() && lastSubFrame != n - 1) {
                subCands.clear();
                for (int g = 1; g < subs.groupCount; ++g) {
                    if (subs.Duration(g) <= 1) continue;
                    // A group's first line can be empty (e.g. group 7 waits 60 frames): its first visible frame is
                    // the first non-empty run.
                    game::Subtitle t = game::Subtitle::Start(subs, g), before = t;
                    std::vector<game::SpriteDef> out;
                    while (out.empty()) {
                        before = t;
                        if (!t.Run(subs, language, false, out)) break;
                    }
                    bool same = !out.empty() && out.size() == c.size();
                    for (size_t i = 0; same && i < out.size(); ++i) same = SameDef(out[i], c[i]->def);
                    if (same) subCands.push_back(before);
                }
                if (subCands.empty()) printf("  [subtitle] frame %d: no group renders the captured line\n", n);
            }
            if (!subCands.empty()) {
                std::vector<game::Subtitle> keep;
                std::vector<game::SpriteDef> firstOut;
                bool any = false;
                for (size_t k = 0; k < subCands.size(); ++k) {
                    game::Subtitle t = subCands[k];
                    std::vector<game::SpriteDef> out;
                    const bool alive = t.Run(subs, language, paused, out);
                    bool same = out.size() == c.size();
                    for (size_t i = 0; same && i < out.size(); ++i) same = SameDef(out[i], c[i]->def);
                    if (k == 0) firstOut = out;
                    if (same) {
                        if (!any) firstOut = out;
                        any = true;
                        if (alive) keep.push_back(t);
                    }
                }
                Compare(stats[kSubtitle], kSubtitle, n, firstOut, c);
                subCands.swap(keep);
            } else {
                Compare(stats[kSubtitle], kSubtitle, n, {}, c);
            }
            if (!c.empty()) lastSubFrame = n;
        }

        // Tutorial (mode 9): task counters from the first frame of each run; bullet / life counts and captions are
        // taken from the capture (structure check only: 0x007DD150 / 0x007DD160 are not captured).
        {
            const auto& lb = byLayout[kTutLabel];
            Compare(stats[kTutLabel], kTutLabel, n, lb.empty() ? std::vector<game::SpriteDef>{} : std::vector<game::SpriteDef>{ game::TutorialLabel() }, lb);
            const auto& tt = byLayout[kTutTorch];
            if (!tt.empty()) {
                if (tutTorchLast != n - 1) tutTorchStart = n;
                tutTorchLast = n;
                Compare(stats[kTutTorch], kTutTorch, n, game::TutorialTorches(n - tutTorchStart, static_cast<int>(tt.size())), tt);
            }
            const auto& tb = byLayout[kTutBullet];
            if (!tb.empty()) Compare(stats[kTutBullet], kTutBullet, n, game::TutorialBullets(static_cast<int>(tb.size())), tb);
            const auto& tr = byLayout[kTutReload];
            if (!tr.empty() || (tutReloadStart >= 0 && n - tutReloadLast <= 16)) {
                if (!tr.empty() && n - tutReloadLast > 16) tutReloadStart = n;  // blink gap: 15 frames
                if (!tr.empty()) tutReloadLast = n;
                const int8_t kind = (f.hasW) ? static_cast<int8_t>(Get<uint8_t>(f.w["9A5C60"], 0x125)) : 0;
                // The task draws while 0x007DD158 == 1 and ends at 2 (W value of this frame when captured).
                const bool on = !(f.hasW && f.w.count("7DD158")) || Get<int32_t>(f.w["7DD158"], 0) == 1;
                Compare(stats[kTutReload], kTutReload, n, on ? game::TutorialReload(n - tutReloadStart, kind) : std::vector<game::SpriteDef>{}, tr);
            }
            const auto& tc = byLayout[kTutCaption];
            std::vector<game::SpriteDef> caps;
            for (auto* x : tc) caps.push_back(game::TutorialCaption(x->def.id, x->def.x, x->def.y));
            Compare(stats[kTutCaption], kTutCaption, n, caps, tc);
        }
        for (Layout l : { kUnknown }) stats[l].defs += static_cast<int>(byLayout[l].size()), stats[l].differ += !byLayout[l].empty();
    };

    for (std::string line; std::getline(cap, line);) {
        if (line.size() < 2) continue;
        switch (line[0]) {
            case 'S': {
                std::istringstream ss(line.substr(2));
                SLine s;
                for (auto& x : s.w) {
                    std::string t;
                    ss >> t;
                    x = static_cast<uint32_t>(std::stoul(t, nullptr, 16));
                }
                for (std::string t; ss >> t;)
                    if (t[0] == '@') s.callers.push_back(static_cast<uint32_t>(std::stoul(t.substr(1), nullptr, 16)));
                s.def = game::SpriteDef::FromWords(s.w);
                cur.s.push_back(std::move(s));
                break;
            }
            case 'W': {
                std::istringstream ss(line.substr(2));
                for (std::string t; ss >> t;) {
                    const auto c = t.find(':');
                    if (c == std::string::npos) continue;
                    cur.w[t.substr(0, c)] = HexBytes(t.substr(c + 1));
                }
                cur.hasW = true;
                break;
            }
            case 'F': {
                cur.number = std::stoi(line.substr(2));
                processFrame(cur, prev);
                prev = std::move(cur);
                prev.s.clear();
                cur = Frame();
                break;
            }
        }
    }

    printf("frames %d (with W pairs %d)\n", frames, framesW);
    printf("%-44s %8s %8s %8s\n", "layout", "match", "differ", "defs");
    int bad = unitBad;
    for (int l = kCrosshair; l < kCount; ++l) {
        const auto& st = stats[l];
        if (st.match + st.differ == 0) continue;
        const bool unported = l == kUnknown;
        printf("%-44s %8d %8d %8d%s\n", kLayoutName[l], st.match, st.differ, st.defs, unported ? "  (frames present, not compared)" : "");
        if (!unported) bad += st.differ;
    }
    if (!unknownCallers.empty()) {
        printf("unknown callers:");
        for (auto& [a, c] : unknownCallers) printf(" %08X:%d", a, c);
        printf("\n");
    }
    return bad == 0 ? 0 : 1;
}
