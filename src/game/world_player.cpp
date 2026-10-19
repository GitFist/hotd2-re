// game::World - players and game mode 6 (a game started from the title): device input -> gun records, the player
// task states (join, play, continue, out), trigger / reload / ammo, damage to players, score, the hit shake, the
// game-mode-6 sub-states (play, continue countdown) up to the switch to game mode 7 (game over).
// Spec and verification: notes/agents/gameplay.md. CONFIRMED = code read + frame-exact against the scripted captures
// (build\scene_capture_play1full.txt / play2full.txt); CODE = read in code, not exercised by the captures.

#include <algorithm>
#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"
#include "game/world_modes.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

constexpr uint32_t kStateTable = 0x00579CD0;  // 0x14 bytes per player state: {task fn, not playing, ?, ?, ready}
constexpr uint32_t kRowTable = 0x00579DE8;    // 0x18 bytes per init row: {flags, state, hit timer, cb7C, cb80, fn}

}  // namespace

// The hook's slot conversion (src/hook/gun_state.cpp ComputeGunSlot with the stretch aspect, a 640x480 game image):
// trigger pressed -> fired (off screen: an off-screen shot instead); reload pressed -> off-screen shot.
WorldInput MakeWorldInput(float nx, float ny, uint8_t buttons, uint8_t prevButtons) {
    auto bits = [](uint8_t b) {
        uint32_t r = 0;
        if (b & 1) r |= 4;  // trigger
        if (b & 2) r |= 2;  // reload
        if (b & 4) r |= 8;  // start
        // the scripts' d-pad (hook ScriptPadBits): 8 up 0x10, 16 down 0x20, 32 left 0x40, 64 right 0x80
        if (b & 8) r |= 0x10;
        if (b & 16) r |= 0x20;
        if (b & 32) r |= 0x40;
        if (b & 64) r |= 0x80;
        return r;
    };
    WorldInput in;
    in.kind = 1;
    in.held = bits(buttons);
    in.pressed = bits(static_cast<uint8_t>(buttons & ~prevButtons));
    const bool onScreen = nx >= 0.0f && nx <= 1.0f && ny >= 0.0f && ny <= 1.0f;
    in.x = std::clamp(static_cast<int>(std::floor(std::clamp(nx, 0.0f, 1.0f) * 640.0f)), 0, 639);
    in.y = std::clamp(static_cast<int>(std::floor(std::clamp(ny, 0.0f, 1.0f) * 480.0f)), 0, 479);
    in.fired = (in.pressed & 4) != 0;
    in.offscreen = (in.pressed & 2) != 0;
    if (in.fired && !onScreen) {
        in.fired = false;
        in.offscreen = true;
    }
    if (in.fired && in.offscreen) in.fired = false;
    return in;
}

// =====================================================================================================================
// Input (Game_UpdateInput 0x0040E4D0: Input_Poll + hook writes, 0x0040D590, 0x0040D8B0, 0x0040D430, 0x0040D680,
// Gun_UpdateRecords 0x0040CBB0)
// =====================================================================================================================

void World::Impl::InputFrame() {
    // Input_Poll (the hook writes the slot state of each driven player); edges last one frame
    WorldInput in[2] = { input[0], input[1] };
    for (int p = 0; p < 2; ++p) {
        if (!inputEdge[p]) {
            in[p].fired = in[p].offscreen = false;
            in[p].pressed = 0;
        }
        inputEdge[p] = false;
        if (in[p].kind == 1) {
            devicePos[p][0] = in[p].x;
            devicePos[p][1] = in[p].y;
        }
        player[p].inputKind = static_cast<int8_t>(in[p].kind);  // 0x0040D8B0 (device descriptors)
    }
    // 0x0040D590 -> 0x0040D6C0: buttons 0x009C9034 (P1 bits 0..11, P2 << 16) and 0x0040D850 edges
    held = (in[0].held & 0xFFF) | ((in[1].held & 0xFFF) << 16);
    pressed = (in[0].pressed & 0xFFF) | ((in[1].pressed & 0xFFF) << 16);
    // -> Game_UpdateShots 0x0040CE70 (Input_LatchShots 0x0041EB30: the last slot that fired / shot off screen)
    int latched = -1;
    for (int p = 0; p < 2; ++p) {
        if (in[p].kind != 1) continue;
        if (in[p].offscreen) {
            devicePos[p][0] = devicePos[p][1] = 0xFFFF;
            latched = p;
        } else if (in[p].fired) {
            latched = p;
        }
    }
    auto setShot = [&](int p) {
        ShotIn& s = shotIn[p];
        s.on = !(devicePos[p][0] == 0xFFFF && devicePos[p][1] == 0xFFFF);
        s.x = static_cast<int16_t>(devicePos[p][0]);
        s.y = static_cast<int16_t>(devicePos[p][1]);
    };
    if (latched == -1) {
        for (int p = 0; p < 2; ++p) setShot(p);
    } else {
        shotIn[latched].fired = 1;
        setShot(latched);
    }
    // mode 6: players in the continue state also "fire" with the continue button (mask 0x009C9F68 & 0x009C903C;
    // not driven here)
    // Gun_UpdateRecords 0x0040CBB0
    for (int p = 0; p < 2; ++p) {
        Player& P = player[p];
        GunRecord& g = P.gun;
        if (P.inputKind == 0) {
            // pad cursor 0x0040CFE0 (no pad driven: the cursor stays); valid = 1, fired = pad trigger (not driven)
            if (padClear != 0) {  // 0x0040D220 clears BOTH records' aim until the letterbox has opened once
                if (canShoot != 0) padClear = 0;
                for (Player& q : player) {
                    q.gun.x = q.gun.y = 0;
                    q.gun.fx = q.gun.fy = 0;
                }
            }
            g.valid = 1;
            g.fired = 0;
        } else if (P.inputKind == 1) {
            ShotIn& s = shotIn[p];
            g.rawX = s.x;
            g.rawY = s.y;
            g.onScreen = s.on;
            g.fired = s.fired;
            s.fired = 0;
            if (s.on == 0) {
                g.x = g.y = 0;
                g.valid = 0;
            } else {
                bool valid = false;
                if (in[p].deviceMode == 6) {
                    GunCalibrateMode6(s.x, s.y, &g.x, &g.y, &valid);
                } else {
                    // 0x0040CCA4: the player's calibration (gun record +0x16 x / +0x1E y, WorldConfig::gunCal) to the
                    // tables 0x004D1CF0 / 0x004D1CF8 (0x009C911C == 1: 0x004D1D00 / 0x004D1D08, assumed 0), clamped
                    // like 0x0040D3E0
                    static const int16_t outX[3] = { -264, 0, 264 }, outY[3] = { 192, 0, -192 };
                    const int cx = GunCalibrate(s.x, cfg.gunCal[p], outX, 640), cy = GunCalibrate(s.y, cfg.gunCal[p] + 4, outY, 640);
                    const bool okX = cx >= -320 && cx <= 319, okY = cy >= -240 && cy <= 239;
                    g.x = static_cast<int16_t>(okX ? cx : 0);
                    g.y = static_cast<int16_t>(okY ? cy : 0);
                    valid = okX && okY;
                }
                g.valid = valid ? 1 : 0;
                g.fx = static_cast<float>(g.x);
                g.fy = static_cast<float>(g.y);
            }
        }
    }
}

// 0x0040E500 (from Game_Update 0x0040E730 after the input; 0x009C90A0 reset path 0x0040E980 not modelled): the pause.
// While paused (0x009A1A00 = 1) Game_UpdateMode is skipped (no task runs, no rand), the input still runs. The toggle
// needs the pause state unchanged for the two previous frames (0x007C1798 / 0x007C179A).
void World::Impl::PauseFrame() {
    if (stageResults.active) return;  // Enter / device Start confirms the results instead
    const bool one = pauseHist[0], two = pauseHist[1];
    pauseHist[1] = pauseHist[0];
    pauseHist[0] = paused;
    bool toggle = false;
    if (one == paused && two == paused) {  // 0x0040E9C0
        if (!paused) {
            // 0x0040EAB0: game mode 6, sub-state 2, 0x009A5900 & 0x1C == 0
            if (gameMode == 6 && subState == 2 && (flags5900 & 0x1C) == 0) {
                if ((pressed & 8) && player[0].state == 5) {
                    pausePlayer = 0;
                    toggle = true;
                } else if ((pressed & 0x80000) && player[1].state == 5) {
                    pausePlayer = 1;
                    toggle = true;
                }
                // a gun device lost (0x009CA100 / 0x009CA104 vs 0x009C6FF8 / 0x009C6FFC) also pauses: not modelled
            }
        } else {
            toggle = (pausePlayer == 0 && (pressed & 8)) || (pausePlayer == 1 && (pressed & 0x80000));
        }
    }
    if (toggle) {
        // pause: 0x004A56E0, Snd_StopSlot 0x0041D4F0(0x009C90A8 = 0: BGM / 0x009C90AC = 1: voice), 0x0049DF80; resume:
        // 0x0041D940(0x1B8), Snd_PlayGameSound(0x009C8FB8: the current BGM, from its start), 0x004A56F0, 0x0049DFA0
        // (timer side effects, not modelled)
        paused = !paused;
        if (paused) {
            SoundCall(World::kStopSlot, 0);
            SoundCall(World::kStopSlot, 1);
        } else {
            Sound(currentBgm);
        }
    }
    // while paused: the dark overlay (near plane 1e-5 0x004194C0, identity view-space matrix, T(0, 0, -0.0005),
    // model 0x93E through the alpha path 0x004185A0 with 0.5, near plane back to 0.001; no task) and the "PAUSE" sprite
    // 0x8E7 (P1) / 0x8E8 (P2) at x 160 / 480, y 240 (HUD, WorldHud::paused)
    if (paused) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, 0.0f, 0.0f, -0.0005f);  // 0xBA03126F
        Draw(0x93E, m, 8, 0.5f, false, -1, true);
    }
    if (paused)  // then every paused frame: Snd_Command(1..7, 0x1100A0) (SE channels stopped)
        for (int i = 1; i < 8; ++i) SoundCall(World::kCommand, 0x1100A0);
}

// =====================================================================================================================
// Game state helpers
// =====================================================================================================================

// 0x00413280: modes without player control (demo, title / attract scenes) or mode 6 sub-state 8
bool World::Impl::NonInteractive() const {
    switch (gameMode) {
    case 5: case 9: case 10: case 11: return true;
    case 6: return subState == 8;
    default: return false;
    }
}

int World::Impl::CreditOf(int) const { return cfg.credits; }  // 0x00406780 (shared credits: 0x009C8E74 == 0)

// 0x00406ED0 (0x00406F00): free play, or enough credits to start or continue
bool World::Impl::CanContinue() const {
    if (cfg.freePlay) return true;
    return !(cfg.credits < cfg.coinsStart && cfg.credits < cfg.coinsContinue);
}

// 0x00406DC0 -> 0x00406E10: take the credits of a start / continue
bool World::Impl::UseCredit(int, bool cont) {
    // mode 4: only in the frame the mode select confirmed a game (0x009A21C0, world_front.cpp)
    if (gameMode != 6 && !(gameMode == 4 && front.menuConfirmed)) return false;
    if (cfg.freePlay) return true;
    const int cost = cont ? cfg.coinsContinue : cfg.coinsStart;
    if (cfg.credits < cost) return false;
    cfg.credits -= cost;
    return true;
}

// 0x007DCCA4 (top of Evt_Run 0x0045ECC0): a player in state 5 with lives, or a non-interactive mode
bool World::Impl::GameRunning() {
    if ((player[0].lives >= 1 && player[0].state == 5) || (player[1].lives >= 1 && player[1].state == 5)) return true;
    return NonInteractive();
}

// op 0x32 (0x0045F900): every player is either in a "ready" state (0x00579CE0[state * 5] != 0) or has lives
bool World::Impl::PlayersReady() {
    for (const Player& P : player)
        if (exe.U32(kStateTable + 0x10 + 0x14 * static_cast<uint32_t>(P.state)) == 0 && P.lives <= 0) return false;
    return true;
}

// Player_AddScore 0x004156C0 (mode 1 doubling for weapon 2 not modelled: game type 0)
void World::Impl::AddScore(int p, int points) {
    if (p < 0 || p > 1) return;
    const int before = player[p].score;
    player[p].score += points;
    if (player[p].score < 0) player[p].score = 0;
    if (cfg.portStageResults && gameMode == 6 && !stageResults.shown)
        stageResults.player[p].score += player[p].score - before;
}

// Player_TakeHit 0x00415300(player, flag, kind)
void World::Impl::PlayerTakeHit(int p, int flag, int kind) {
    if (p == -1 || p < 0 || p > 1) return;
    Player& P = player[p];
    // 0x004153E0: in mode 6 only players in state 1, 4 or 5 can be hit
    if (gameMode == 6 && P.state != 1 && (P.state < 4 || P.state > 5)) return;
    if (P.hitTimer != 0 || gameMode == 5) return;
    // 0x009C9FD8 + p * 0x7C: infinite lives (Hod2.ini cheat) assumed off
    --P.lives;
    if (cfg.portStageResults && !stageResults.shown) ++stageResults.player[p].damage;
    rankAdj -= 2;
    AddScore(p, -100);
    if (flag != 0) {
        P.hitShake = 1;
        P.hitKind = static_cast<int16_t>(kind);
    }
    P.hitTimer = 0x5A;
    // the last life is only taken while the camera mode (0x009C6F08 / 0x009C6F0C) is 2 (gameplay)
    if (runner->camera.modeCur != 2 && runner->camera.modeReq != 2 && P.lives < 1) P.lives = 1;
}

// Player_HitShake 0x00415270 (from task 0x00414F40): 48 frames of vertical view shake after a hit with flag 1
void World::Impl::HitShake() {
    if (player[0].hitShake == 1) {
        player[0].hitShake = 0;
        shakeTimer = 0x30;
    }
    if (player[1].hitShake == 1) {
        player[1].hitShake = 0;
        shakeTimer = 0x30;
    }
    if (--shakeTimer > 0) {
        // fild(n * 0x1800) * (2pi / 65536) [double 0x004C4370]; fcos; * (fild n * float 0x004EC8CC); __ftol
        // (24-bit FPU: the products are rounded to float; fcos itself is not affected by the precision control)
        double k2pi = 0;
        exe.Read(0x004C4370, &k2pi, 8);
        const float a = static_cast<float>(static_cast<double>(shakeTimer * 0x1800) * k2pi);
        const float k = static_cast<float>(shakeTimer) * F32(exe, 0x004EC8CC);
        const float v = static_cast<float>(std::cos(static_cast<double>(a)) * static_cast<double>(k));
        shake = static_cast<int>(v);
        return;
    }
    shakeTimer = 0;
    shake = 0;
}

// Game_UpdateRank 0x004607B0
void World::Impl::UpdateRank() {
    int s3 = rank + (players - rankPrevPlayers) * 4;
    int s4 = s3;
    if (rankTimer % 0x708 == 0) {
        s4 = s3 + 1;
        bool two = false;
        if (playersInGame == 0) two = player[0].lives >= 4;
        else if (playersInGame == 1) two = player[1].lives >= 4;
        else two = playersInGame == 2 && player[0].lives + player[1].lives >= 7;
        if (two) s4 = s3 + 2;
    }
    s4 += rankAdj;
    rankAdj = 0;
    s4 = std::clamp(s4, 0, 15);
    if (s4 < rank) rankTimer = 0;
    rankPrevPlayers = players;
    rank = s4;
}

// 0x004606D0: one stage frame (task run, rank, rank timer, per-player hit timers)
void World::Impl::StageFrame() {
    ts.RunFrame();  // Task_RunFrom 0x004A7240
    UpdateRank();
    if (rankTimerOn) ++rankTimer;
    for (Player& P : player)
        if (--P.hitTimer < 0) P.hitTimer = 0;
}

// Game_SetState 0x0040E850 (0x007C17A0) applied at the end of the frame by Game_ApplyState 0x0040E860
void World::Impl::SetGameMode(int m) {
    if (m != 6 && prototype) { prototype.reset(); animLight = 1; }
    if (m == 3 || m == 4 || m == 15) RandomizerBegin(*this, false);
    stageResults = {};
    // Mem_Init 0x004A7310: every task is freed (the game-over screen of mode 7 is not ported)
    ts.Reset();
    demoFlag = m == 5 ? 1 : 0;
    flags5900 = (flags5900 & ~0x38u) | 2;
    route = players = 0;
    rankPrevPlayers = 0;
    player[0].hitShake = player[1].hitShake = 0;
    shakeTimer = 0;
    gameMode = m;
    subState = 0;
    projCenter[0] = 320.0f;  // 0x0040E8A8: 0x00419490(320, 240)
    projCenter[1] = 240.0f;
    if (m < 6 || m > 7) player[0].state = player[1].state = 9;
}

// 0x0045FEE0: game mode 6, sub-state table 0x005679A8
void World::Impl::Mode6Frame() {
    if (StageResultsFrame()) return;
    auto notPlaying = [&](int p) { return exe.U32(kStateTable + 4 + 0x14 * static_cast<uint32_t>(player[p].state)) != 0; };
    switch (subState) {
    case 2:  // 0x004601D0
        if (notPlaying(0) && notPlaying(1)) {
            if (CanContinue()) subState = 3;  // game type 0 (types 2 / 3 go to 0xB)
            else subState = 0xB;
        }
        StageFrame();
        return;
    case 3:  // 0x004604E0
        StageFrame();
        contCountdown = 0x9FFF;
        contCredit[0] = CreditOf(0);
        contCredit[1] = CreditOf(1);
        contCredit[2] = contCredit[3] = 0;
        subState = 4;
        return;
    case 4: {  // 0x00460530
        StageFrame();
        if (contCredit[0] != CreditOf(0) || contCredit[1] != CreditOf(1) || contCredit[2] != 0 || contCredit[3] != 0) {
            contCredit[0] = CreditOf(0);
            contCredit[1] = CreditOf(1);
            contCredit[2] = contCredit[3] = 0;
            contCountdown = 0x9FFF;
        }
        uint32_t b = player[0].state == 4 ? (pressed & 4) : 0;
        if (player[1].state == 4) b |= pressed & 0x40000;
        if (b != 0 && contCountdown < 0x8000) contCountdown = (contCountdown & ~0xFFF) + 1;
        // sprites 0x22C ("CONTINUE") and the digit (contCountdown >> 12) + 0x4F, every frame this task runs (also the
        // frame that leaves sub-state 4): HUD (WorldHud::continueShown). 0x009A2BB8 itself is not reset by a continue.
        contScreenShown = contCountdown;
        if (!(notPlaying(0) && notPlaying(1))) {
            subState = 2;
            for (Player& P : player)  // 0x004141D0(0) / (1): a player still in state 4 restarts its own countdown
                if (P.state == 4 && P.task && P.task->data) P.task->As<PlayerTask>().fn = fn::kPlayerContRestart;
        } else {
            contCountdown -= 0x2D;
            if (contCountdown < 1) {
                route88 = route;
                SetGameMode(7);
            }
        }
        return;
    }
    case 5: case 6: case 1:  // stage end (world_st1b_boss.cpp)
        Mode6StageEnd();
        return;
    case 7: case 8: case 9: case 0xA:  // the ending, name entry (world_ending.cpp)
        EndingFrame();
        return;
    case 0xB: case 0xC:  // 0x00460250 / 0x00460350: no credits left: two more stage frames, then game mode 7
        if (subState == 0xB) {
            // (0x00460250 also resets the fog / light colours 0x009A3564.. and 0x009C89E4..: renderer state)
            goOverCounter = 0;  // 0x007DCCD4
            ++subState;
        }
        StageFrame();
        if (1 < ++goOverCounter) {
            route88 = route;
            SetGameMode(7);
        }
        return;
    default:
        Note("game mode 6 sub-state " + std::to_string(subState) + " not ported");
        StageFrame();
        return;
    }
}

// =====================================================================================================================
// Player tasks
// =====================================================================================================================

// 0x00415080
void World::Impl::PlayerSetState(Task& t, int state, bool setFn) {
    PlayerTask& pt = t.As<PlayerTask>();
    player[pt.index].state = static_cast<int16_t>(state);
    if (setFn) pt.fn = exe.U32(kStateTable + 0x14 * static_cast<uint32_t>(state));
}

// 0x00414770(task, row): player (re)start from row 0x00579DE8[row]; then the row's task function runs at once
void World::Impl::PlayerInit(Task& t, int row) {
    PlayerTask& pt = t.As<PlayerTask>();
    const int p = pt.index;
    Player& P = player[p];
    if (cfg.portStageResults && gameMode == 6) stageResults.player[p].participated = true;
    const uint32_t r = kRowTable + 0x18 * static_cast<uint32_t>(row);
    const uint32_t flags = exe.U32(r);
    const int gt = runner ? runner->cfg.gameType : 0;
    if ((flags & 1) || (1 < gt && gt < 4)) {  // game types 0 / 1 by the row (types 2 / 3 always)
        P.lives = static_cast<int16_t>(cfg.livesSetting);
        P.livesShown = P.lives;
    }
    if (flags & 4) P.score = 0;
    // the row's callbacks 0x004150C0 / 0x004150E0 and 0x00415920 / 0x00415940 only set +0x7C = 0x00415120 / 0x00415180
    // and +0x80 = 0x00415970 / 0x00415980 on their first call (the row function below runs them at once): no body
    // placement / draw in the init frame (two players in the original-mode item select: P1 is placed with route 2)
    P.cb7C = exe.U32(r + 0xC);
    P.cb80 = exe.U32(r + 0x10);
    if (flags & 0x10) ++players;  // 0x009C8E80
    if (flags & 0x20) ++route;    // 0x009C8E84
    P.overlay.active = 0;
    P.overlay.timer = 0;
    P.hitTimer = static_cast<int>(exe.U32(r + 8));  // 0x009C8E08[p]
    P.contTimer = 0;
    P.aim[1] = 0;
    P.aim[0] = 0;
    P.empty = 0;
    P.reloadBlink = 0;
    for (auto& f : P.flash) f.active = false;
    for (auto& tr : P.tracer) tr.active = false;
    P.ring = 0;
    P.ammo = 6;
    if (runner->cfg.gameType == 1 && modes) ItemsPlayerStart(*this, p, row);  // the item magazine / weapon row / lives
    PlayerSetState(t, static_cast<int>(exe.U32(r + 4)), false);
    pt.fn = exe.U32(r + 0x14);  // 0x00413E90
    PlayerMain(t);
}

void World::Impl::TaskPlayer(Task& t) {
    PlayerTask& pt = t.As<PlayerTask>();
    const int p = pt.index;
    Player& P = player[p];
    switch (pt.fn) {
    case fn::kPlayerInit:  // 0x004145C0
        if (gameMode != 5) {
            pt.fn = fn::kPlayerIdle;
            PlayerIdle(t);  // 0x004145F0 -> 0x00414600
            return;
        }
        PlayerInit(t, 4);  // 0x00414720
        return;
    case fn::kPlayerJoin:  // 0x00413DB0: 0x009C6EF0 = 0, 0x007C211C = 0
        PlayerInit(t, 0);
        return;
    case fn::kPlayerIdle:
        PlayerIdle(t);
        return;
    case fn::kPlayer:
        PlayerMain(t);
        return;
    case fn::kPlayerContInit:  // 0x00414200
        P.contTimer = 0x9FFF;
        P.credit0 = CreditOf(0);
        P.credit1 = CreditOf(1);
        P.credit2 = P.credit3 = 0;
        pt.fn = fn::kPlayerCont;
        return;
    case fn::kPlayerCont:
        PlayerContinue(t);
        return;
    case fn::kPlayerContRestart:  // 0x00414250
        P.contTimer = 0x9FFF;
        pt.fn = fn::kPlayerCont;
        PlayerContinue(t);
        return;
    case fn::kPlayerRejoin:  // 0x00413DE0: continue accepted
        if (cfg.portStageResults && !stageResults.shown) ++stageResults.player[p].continues;
        if (P.overlay.active != 0) {  // 0x004172E0
            P.overlay.active = 0;
            P.overlay.timer = 0;
        }
        AddScore(p, 1);  // the continue count shows in the score's last digit
        if (--rank < 0) rank = 0;
        PlayerInit(t, 1);
        return;
    case fn::kPlayerRow2:  // 0x00413E40
        PlayerInit(t, 2);
        return;
    case fn::kPlayerJoinLate:  // 0x00413E50: joined during the game (0x009C6EF0 = 0)
        P.streak = 0;
        P.shots = 0;
        P.hits = 0;
        PlayerInit(t, 3);
        return;
    case fn::kPlayerWaitInit:  // 0x00414680
        pt.fn = fn::kPlayerWait;
        return;
    case fn::kPlayerWait:  // 0x00414690
        if (flags5900 & 1) {
            PlayerSetState(t, P.nextState, true);
            TaskPlayer(t);  // the state's function runs at once
            return;
        }
        // blinking "please wait" sprite 0x8E9 at x 50 / 430, y 432 while (0x009A32A0 & 0x20): HUD (state 10)
        return;
    case fn::kPlayerOut:  // 0x00414420
        PlayerOutStart(t);
        return;
    case fn::kPlayerOut2:  // 0x004144C0
        PlayerOut(t);
        return;
    case fn::kPlayerNone:
        return;
    case 0x00414540: case 0x00414570:  // state 8: the name entry (world_ending.cpp)
        NameEntryPlayer(t);
        return;
    case 0x00414520:  // state 7
        pt.fn = fn::kPlayerNone;
        return;
    default:
        Note("player task function " + std::to_string(pt.fn) + " not ported");
        return;
    }
}

// Player_Task 0x00413E90
void World::Impl::PlayerMain(Task& t) {
    PlayerTask& pt = t.As<PlayerTask>();
    const int p = pt.index;
    Player& P = player[p];
    PlayerCb80(t);  // +0x80: body placement
    PlayerCb7C(t);  // 0x00415100 -> +0x7C: body draw / damage overlay
    if (gameMode == 5) P.lives = 1;
    const int gt = runner ? runner->cfg.gameType : 0;
    if (P.lives > 0) {
        // game type 1: 0x00414B90 (the special weapons, world_modes_weapons.cpp), else 0x00414940
        if ((gt == 1 || HordeSpecialWeapon(*this, p)) && modes) SpecialTrigger(*this, t);  // (port: Horde mode's bought weapons)
        else PlayerTrigger(t);
    } else {
        P.lives = 0;
    }
    PlayerCrosshair(t);  // 0x004169C0
    if (gt != 2 || runner->flags[0xF2] == 0) PlayerFlashes(p);  // 0x00416B00 (training: not after the lesson's end)
    if (gameMode != 9 && gameMode != 5) {
        if (P.lives == 0) {
            if (gt == 2) {  // training: out of lives = the lesson is failed
                runner->globals[0x009A2234] = 1;
            } else {
                --players;
                PlayerSetState(t, 4, true);  // -> 0x00414200
            }
        } else {
            if (canShoot != 0) PlayerAmmoHud(t);  // 0x004177D0
            // 0x004174A0: lives HUD (sprites only)
            P.livesShown = P.lives;
        }
    }
    PlayerOverlay(t);  // 0x00417300
    if (NonInteractive()) PlayerIdle(t);  // 0x00414600
    // 0x00413FB0: only when 0x009C87FC == 7 in mode 6 (not seen)
}

// =====================================================================================================================
// Player bodies (player +0x78): skeleton objects of the two agents, seen in the intro cutscenes
// =====================================================================================================================

// 0x00416450 (from the rail task creation 0x00414F20): object 0x13F4, skeleton 0x00579F50[p], motion 0x004EC8A4[p],
// Anim_InitEnemy, rotation order 1, node callback 0x00416570; actor slot 14 + p (0x009C88F8[p])
void World::Impl::PlayerBodyInit(int p) {
    auto b = std::make_unique<EnemyTask>();
    b->bodyOf = static_cast<int8_t>(p);
    b->anim.kind = exe.S16(0x00579F50 + static_cast<uint32_t>(p) * 4);
    if (runner && runner->cfg.gameType == 1 && modes && modes->pitems[p].character != p) {
        // 0x0041649F: game type 1, an item character: skeleton 0x39 + c (c 8 / 9: 0x21 / 0x34)
        const int c = modes->pitems[p].character;
        b->anim.kind = static_cast<int16_t>(c <= 7 ? 0x39 + c : c == 8 ? 0x21 : 0x34);
    }
    b->anim.motion[0] = static_cast<int>(exe.U32(0x004EC8A4 + static_cast<uint32_t>(p) * 4));
    EnemyInitPose(*b);  // 0x00410440 (+ an actor slot, released at once by 0x00416450)
    if (b->actorSlot >= 0 && b->actorSlot < 14) actorSlots[b->actorSlot] = nullptr;
    b->actorSlot = 14 + p;
    b->anim.rotOrder = 1;
    playerBody[p] = std::move(b);
}

// 0x00416810(player task, k): the gun model of node 5, 0x004EC9E0[c * 3 + k] with c = the player, or in game type 1 the
// character 0x009A2242 + p * 0x14 when it differs (item characters)
static int BodyGunModel(World::Impl& w, int p, int k) {
    int c = p;
    if (w.runner->cfg.gameType == 1 && w.modes && w.modes->pitems[p & 1].character != p) c = w.modes->pitems[p & 1].character;
    return w.exe.S16(0x004EC9E0 + static_cast<uint32_t>(c * 3 + k) * 2);
}

// Player +0x80 callback (set by the init rows 0x00579DE8, the camera sub-mode setups 0x00576C14 and camera command
// 0x12 = 0x00579E90[arg])
void World::Impl::PlayerCb80(Task& t) {
    const int p = t.As<PlayerTask>().index;
    Player& P = player[p];
    EnemyTask* b = playerBody[p].get();
    if (!b) return;
    const CameraDirector& c = runner->camera;
    switch (P.cb80) {
    case 0: case 0x0041EBB0:
        return;
    case 0x00415920:  // the init rows' one-shots: the callback for the next frames only
        P.cb80 = 0x00415970;
        return;
    case 0x00415940:
        P.cb80 = 0x00415980;
        return;
    case 0x00415960: case 0x00415970: case 0x00415980: case 0x00415990: {  // -> 0x004159A0: at the players' rail
        Mat4 m = Mat4::Identity();                                         // position, route offset 0x00579E90
        mx::Translate(m, playerPos[0], playerPos[1], playerPos[2]);
        mx::RotZ(m, c.playerRoll);  // 0x009C71F4
        mx::RotY(m, c.playerYaw);   // 0x009C71F0
        mx::RotX(m, c.playerPitch); // 0x009C71EC
        const float local[3] = { F32(exe, 0x00579E90 + static_cast<uint32_t>(p + route * 2) * 4), 0.0f, 0.0f };
        mx::Transform(m, local, b->pos);
        b->rot[0] = c.playerPitch;
        b->rot[1] = c.playerYaw;
        b->rot[2] = c.playerRoll;
        return;
    }
    case 0x00415B60:  // intro start: body drawn, walk motion, gun model on node 5 (0x00416810), then 0x00415BD0
        P.flags12C |= 1;
        AnimSetMotion(*b, (players == 2 && p != 0) ? 0x34A : 0x322);
        b->anim.nodes[5].model = BodyGunModel(*this, p, 0);  // 0x00416810(t, 0)
        P.cb80 = 0x00415BD0;
        return;
    case 0x00415BD0: {  // intro: beside the car on paths 0xFD (camera 0x20) / 0xFE (camera 0x21 up to tick 0x104)
        const int cam = c.camId, tick = c.tick;
        float x = b->pos[0], z = b->pos[2];
        int yaw = b->rot[1];
        int path = -1;
        if (cam == 0x20) path = 0xFD;
        else if (cam == 0x21 && tick <= 0x104) path = 0xFE;
        else if (cam != 0x21 && cam != 0x22) {
            Note("player body intro placement 0x00415BD0 with camera " + std::to_string(cam) + " (stack garbage in the original)");
        }
        if (path >= 0) {
            float pos[3] = {}, tg[3] = {};
            int roll = 0;
            if (cameraBank.Evaluate(path, static_cast<float>(tick), pos, tg, &roll, false)) {
                const int pyaw = static_cast<int>(tg[1]);
                Mat4 m = Mat4::Identity();
                mx::Translate(m, pos[0], pos[1], pos[2]);
                mx::RotY(m, pyaw);
                const float local[3] = { F32(exe, 0x004EC8D8 + static_cast<uint32_t>(p + players * 2) * 4), 0.0f,
                                         0.239000007f };  // 0x3E74BC6A
                float out[3];
                mx::Transform(m, local, out);
                x = out[0];
                z = out[2];
                yaw = pyaw + 0x8000;
            }
        }
        b->pos[0] = x;
        b->pos[1] = 0.0f;
        b->pos[2] = z;
        b->rot[0] = 0;
        b->rot[2] = 0;
        b->rot[1] = yaw;
        if (cam == 0x22 && tick == 0xC) {
            AnimBlendTo(*b, (players == 2 && p != 0) ? 0x334 : 0x319, 5, 0);
            P.cb80 = 0x00415E00;
        }
        return;
    }
    case 0x00415E00: {  // hold the last frame
        const int len = MotionTicks(b->anim.motion[0]);
        if (b->anim.counter[0] == len - 1) b->anim.counter[0] = len - 2;
        return;
    }
    case 0x00415E40: {  // stage-2 cutscene: the body at 0x004EC8F0[p - 2 + players * 2], drawn, its motion, the gun
        const uint32_t row = 0x004EC8F0 + static_cast<uint32_t>(p - 2 + players * 2) * 0xC;
        for (int k = 0; k < 3; ++k) b->pos[k] = F32(exe, row + 4 * static_cast<uint32_t>(k));
        b->rot[0] = 0;
        b->rot[2] = 0;
        P.flags12C |= 1;  // 0x009A5D8C
        const bool first = players == 2 && p == 0;
        b->rot[1] = first ? 0xF8E3 : 0xEE05;
        AnimSetMotion(*b, exe.S16(0x004EC91C + static_cast<uint32_t>(p + players * 2) * 2));  // 0x00411930
        P.cb80 = first ? 0x00415F60 : 0x00415F90;
        b->anim.nodes[5].model = BodyGunModel(*this, p, 1);  // 0x00416810(task, 1): the gun model on node 5 (+0x4DC)
        return;
    }
    case 0x00415F60:  // hold the first frame
        b->anim.counter[0] = 0;
        return;
    case 0x00415F90: {  // the first frame up to camera tick 0x28, then hold the last one
        if (c.tick <= 0x28) b->anim.counter[0] = 0;
        const int len = MotionTicks(b->anim.motion[0]);  // 0x004E07D0[motion]
        if (len - 1 <= b->anim.counter[0]) b->anim.counter[0] = len - 2;
        return;
    }
    default:
        Note("player +0x80 callback " + std::to_string(P.cb80) + " not ported");
        return;
    }
}

// Bit 0 of the players' +0x12C words (0x009A5D8C / 0x009A5EBC, "draw the body") is also written by the camera
// (CameraDirector::playerFlags: op 0x31 / 0x32 / 0x4D and camera command 0x10 0x00403830 write it for both players).
// World keeps one copy per player: every camera write (counted by CameraDirector::playerFlagsWrites) is applied to
// both players, as the original's writes go straight to both words.
void World::Impl::SyncPlayerDrawBit() {
    if (!runner) return;
    if (runner->camera.playerFlagsWrites == camPlayerWrites) return;
    camPlayerWrites = runner->camera.playerFlagsWrites;
    const uint32_t b = runner->camera.playerFlags & 1u;
    for (Player& P : player) P.flags12C = (P.flags12C & ~1u) | b;
}

// Player +0x7C callback: 0x00415120 draws the body while +0x12C bit 0 is set and advances its animation counter;
// 0x00415180 starts the damage overlay of a hit (0x00417440)
void World::Impl::PlayerCb7C(Task& t) {
    const int p = t.As<PlayerTask>().index;
    Player& P = player[p];
    if (P.cb7C == 0x004150C0 || P.cb7C == 0x004150E0) {  // the init rows' one-shots (0x004150C0 / 0x004150E0)
        P.cb7C = P.cb7C == 0x004150C0 ? 0x00415120 : 0x00415180;
        return;
    }
    if (P.cb7C == 0x00415120) {
        EnemyTask* b = playerBody[p].get();
        if (!b) return;
        if (P.flags12C & 1) {  // 0x00411090 (light 0x0041DC70 / 0x0041DCC0: renderer)
            AnimDraw(*b);
            EnemyShadow(*b);  // 0x0040A590
        }
        ++b->anim.counter[0];
        return;
    }
    if (P.cb7C == 0x004151D0) {  // state 6: the body's death motion in game mode 7, then 0x004151B0 (no draw)
        EnemyTask* b = playerBody[p].get();
        if (gameMode != 7 || !b) return;
        animLight = 0;  // (no light calls here: the light globals as they are)
        AnimDraw(*b);  // 0x00411090
        animLight = 1;
        EnemyShadow(*b);
        int& counter = b->anim.counter[0];
        if (counter == MotionTicks(b->anim.motion[0]) - 1) {
            P.cb7C = 0x004151B0;
            return;
        }
        // 0x009A6110: the game-over camera's tick; two players: per-player start ticks 0x004EC8C4
        const int tick = runner->camera.tick;
        if (route88 == 2) {
            if (static_cast<int>(exe.U32(0x004EC8C4 + static_cast<uint32_t>(p) * 4)) <= tick) ++counter;
        } else if (tick > 0x3B) {
            ++counter;
        }
        return;
    }
    if (P.cb7C == 0x00415180 && P.hitShake != 0) {  // -> 0x00417440(task, +0x72)
        Player::Overlay& o = P.overlay;
        if (o.active == 0) {
            o.active = 1;
            o.timer = 0x3C;
            o.route = route;
            o.x = F32(exe, 0x004ECB54 + static_cast<uint32_t>(p + route * 2) * 4);
            o.kind = P.hitKind;
            Sound(exe.U32(0x00579FD8 + static_cast<uint32_t>(P.hitKind) * 4));
        }
    }
}

// 0x00416570: body node: its model, plus props of motions 0x32A / 0x356 and kind 0x3F
void World::Impl::PlayerBodyNode(EnemyTask& e, int idx, const Mat4& m) {
    if (runner->cfg.gameType == 1 && modes && modes->itemFlags[4] == 1 && (idx == 2 || idx == 5 || idx == 8 || idx == 0xC || idx == 0xF)) {
        Mat4 n = m;  // game type 1 with item 0xC (0x009C88AC): the head and the hands / feet scaled (x / z)
        const float s = idx == 2 ? 1.5f : 2.0f;
        mx::Scale(n, s, 1.0f, s);
        Draw(e.anim.nodes[idx].model, n, 8, -1.0f, false, idx);
    } else {
        Draw(e.anim.nodes[idx].model, m, 8, -1.0f, false, idx);
    }
    const int motion = e.anim.motion[0], tick = e.anim.tick[0];
    auto bits = [](uint32_t u) {
        float f;
        memcpy(&f, &u, 4);
        return f;
    };
    if (motion == 0x32A) {
        if (idx == 5 && tick > 0x28) {
            Mat4 n = m;
            mx::Translate(n, bits(0x3F666666), bits(0xBFB33333), bits(0x3F19999A));
            mx::RotZ(n, 0x8000);
            mx::RotY(n, 0x1000);
            const float s = bits(0x3E19999A);
            mx::Scale(n, s, s, s);
            Draw(0x7ED, n, 8, -1.0f, false, idx);
        }
    } else if (motion == 0x356 && idx == 8) {  // the gun 0x10D2: lying on the floor, then in the hand from tick 0x37
        const float s = bits(0x3ECCCCCD);
        if (tick < 0x37) {
            Mat4 n = cam.view;  // 0x004A9230(the context's view): placed in the world
            mx::Translate(n, bits(0xC4363333), bits(0x42100A3D), bits(0xC4A4F99A));
            mx::RotY(n, e.rot[1]);
            mx::Translate(n, bits(0xBF6FDF3B), 0.0f, bits(0xC0FDB08A));
            mx::RotZ(n, 0xFB8D);
            mx::RotY(n, 0xF476);
            mx::RotX(n, 0x4133);
            mx::Scale(n, s, s, s);
            Draw(0x10D2, n, 8, -1.0f, false, idx, true);
        } else {
            Mat4 n = m;
            mx::Translate(n, bits(0x3DCCCCCD), bits(0xBFC00000), bits(0x3F000000));
            mx::RotZ(n, 64000);
            mx::RotY(n, 0x9C00);
            mx::RotX(n, 0x1700);
            mx::Scale(n, s, s, s);
            Draw(0x10D2, n, 8, -1.0f, false, idx);
        }
    }
    if (e.anim.kind == 0x3F && idx == 1) Draw(0x125D, m, 8, -1.0f, false, idx);
}

// Rail 0x0040C9C0 (camera sub-mode 1/1): the players' position / angles from the body of the player in game
void World::Impl::RailPlayersFromBodies() {
    if (players != 1) {  // 0 or 2 players: the midpoint of the two bodies, angles halfway (P1 + half the difference)
        const EnemyTask* a = playerBody[0].get();
        const EnemyTask* b = playerBody[1].get();
        if (!a || !b) return;
        // 0x00401E90(a, b, 1, 1) = (1/2) * b + (1/2) * a, 24-bit FPU
        for (int k = 0; k < 3; ++k) playerPos[k] = 0.5f * a->pos[k] + 0.5f * b->pos[k];
        // 0x00401EC0(a, b, 1, 1): d = (b - a) & 0xFFFF; d <= 0x7FFF ? a + d / 2 : a - (0x10000 - d) / 2
        auto angle = [](int x, int y) {
            const int d = (y - x) & 0xFFFF;
            return d <= 0x7FFF ? x + d / 2 : x - (0x10000 - d) / 2;
        };
        memcpy(runner->camera.playerPos, playerPos, 12);
        runner->camera.playerPitch = angle(a->rot[0], b->rot[0]);  // 0x009C71EC..F4
        runner->camera.playerYaw = angle(a->rot[1], b->rot[1]);
        runner->camera.playerRoll = angle(a->rot[2], b->rot[2]);
        return;
    }
    const int p = playersInGame == 1 ? 1 : 0;
    const EnemyTask* b = playerBody[p].get();
    if (!b) return;
    memcpy(playerPos, b->pos, 12);
    memcpy(runner->camera.playerPos, b->pos, 12);
    runner->camera.playerPitch = b->rot[0];  // 0x009C71EC..F4
    runner->camera.playerYaw = b->rot[1];
    runner->camera.playerRoll = b->rot[2];
}

// Player_Trigger 0x00414940 (+ Player_UpdateAim 0x00415710)
void World::Impl::PlayerTrigger(Task& t) {
    PlayerTask& pt = t.As<PlayerTask>();
    const int p = pt.index;
    Player& P = player[p];
    if (demoFlag == 1) {
        SetSites({ attackers != 0 ? 0x00415740u : 0x0041572Eu, 0x00415757u, 0x0041576Bu });
        const DemoShot s = DemoAutoGun(rng, p, attackers != 0);
        ApplyDemoShot(s, P.gun);
    }
    P.pending = 0;
    P.aim[0] = static_cast<float>(P.gun.x);
    P.aim[1] = static_cast<float>(P.gun.y);
    if (P.gun.fired) {
        if (P.gun.valid == 0 || P.ammo == 0) {
            // off-screen shot of a gun with a partly empty magazine: reload
            const bool itemMag = runner->cfg.gameType == 1 || HordeActive(*this);  // (port: the Horde mode's bought magazines)
            const int full = itemMag ? ItemMagazine(*this, p) : 6;  // game type 1: the item magazine
            if (P.inputKind == 1 && P.gun.valid == 0 && P.ammo != full) PlayerReload(p);
        } else if (canShoot) {
            // infinite ammo 0x009C9FD9 + p * 0x7C assumed off; game type 1: item magazine -1 = infinite
            if (gameMode != 5 && !((runner->cfg.gameType == 1 || HordeActive(*this)) && ItemMagazine(*this, p) == -1)) --P.ammo;
            if (P.ammo == 0) {
                P.empty = 1;
                P.reloadBlink = 0;
            }
            P.pending = 1;  // 0x009A5C78 + p * 0x130
            P.firedThisFrame = true;
            if (GlobalInt(0x009A5C48, 0) == 0) ++P.shots;
            P.ray = MakeShotRay(P.aim[0], P.aim[1], focal);  // Shot_BuildRay 0x00406110
            {  // Shot_SpawnTracer 0x00416F70 (weapon 0: no 0x009CA08C == 1 extras)
                const float vx = P.aim[0] / focal, vy = P.aim[1] / focal;
                Player::Flash& f = P.flash[P.ring];
                f.active = true;
                f.counter = 0;
                f.pos[0] = vx;
                f.pos[1] = vy;
                f.pos[2] = -1.0f;
                mx::DirToAngles(vx, vy, -1.0f, &f.pitch, &f.yaw);
                f.yaw += 0x8000;
                f.pitch = -f.pitch;
                f.type = 0;
                Player::Tracer& tr = P.tracer[P.ring];
                tr.active = true;
                tr.counter = 0;
                const float local[3] = { vx, vy, -1.0f };
                mx::Transform(cam.inverse, local, tr.pos);
                float d[3] = { tr.pos[0] - cam.pos[0], tr.pos[1] - cam.pos[1], tr.pos[2] - cam.pos[2] };
                const float len = static_cast<float>(std::sqrt(static_cast<double>(d[2]) * d[2] + static_cast<double>(d[1]) * d[1] +
                                                               static_cast<double>(d[0]) * d[0]));  // 0x004AAA00
                for (int k = 0; k < 3; ++k) tr.vel[k] = (d[k] / len) * 20.0f;  // 0x004C4C8C
                tr.type = 0;
                if (++P.ring > 5) P.ring = 0;
            }
            Sound(exe.U32(0x004EC8BC + 4 * p));  // gun sound
        }
    }
    // pad reload button (0x009C9028 & 0x009C9F6C[p][kind]) for input kind 0: pads are not driven
    // cutscene skip request: reload button (P1 bit 2, P2 0x20000) while the letterbox is closed and the scene is
    // skippable (0x009A2D7C); the skip watcher actor 0x00435F40 acts on it
    if (canShoot == 0 && runner->skippable) {
        const uint32_t mask = p == 0 ? 2u : 0x20000u;
        if (pressed & mask) skipRequest = true;
    }
}

// Player_Reload 0x00414B30
void World::Impl::PlayerReload(int p) {
    const int mag = runner->cfg.gameType == 1 || HordeActive(*this) ? ItemMagazine(*this, p) : -1;  // game type 1: the item magazine
    player[p].ammo = static_cast<int16_t>(mag != -1 ? mag : 6);
    player[p].empty = 0;
    if (canShoot != 0) Sound(0x3E16A9);
}

// 0x004169C0: crosshair sprite (HUD) for gun device modes 5 / 6 or a pad cursor
void World::Impl::PlayerCrosshair(Task& t) {
    const int p = t.As<PlayerTask>().index;
    Player& P = player[p];
    const int mode = input[p].kind == 1 ? input[p].deviceMode : 0;
    const bool gunMode = mode == 5 || mode == 6;
    P.crosshairOn = false;
    if (gameMode == 6 && ((P.inputKind != 1 && P.gun.valid != 0) || gunMode) && P.lives > 0 && canShoot != 0) {
        P.crosshairOn = true;
        if (gunMode) {  // Input_GetGunPos 0x0041E280: the latched device position
            P.crosshair[0] = static_cast<float>(devicePos[p][0]);
            P.crosshair[1] = static_cast<float>(devicePos[p][1]);
        } else {
            P.crosshair[0] = P.aim[0] + 320.0f;
            P.crosshair[1] = 240.0f - P.aim[1];
        }
    }
}

// 0x004177D0 (state part; the ammo / RELOAD sprites are HUD): reload prompt counter and the empty-gun click
void World::Impl::PlayerAmmoHud(Task& t) {
    const int p = t.As<PlayerTask>().index;
    Player& P = player[p];
    if (letterbox == 2) P.ammoAnim = 0;
    else if (letterbox == 1) P.ammoAnim += F32(exe, 0x004C4380);
    else P.ammoAnim = 0;
    if ((letterbox != 2 && letterbox != 1) || P.empty == 0 || canShoot == 0) return;
    if (P.reloadBlink != 0 && P.gun.fired != 0 && P.gun.valid != 0)
        Sound(P.reloadBlink < 0x78 || P.inputKind != 1 ? 0x115A9u : 0x215A9u);
    const int16_t v = P.reloadBlink++;
    if (v > 600) P.reloadBlink = 0x78;
}

// 0x00417300: damage overlay model (rows 1 / 3 only) and the hurt voice 5 frames after the overlay starts
void World::Impl::PlayerOverlay(Task& t) {
    const int p = t.As<PlayerTask>().index;
    Player& P = player[p];
    Player::Overlay& o = P.overlay;
    if (o.active != 0) {
        if (--o.timer == 0 || o.route < route) o.active = 0;  // 0x004172E0
        if (o.active != 0) {
            const float s = F32(exe, 0x004ECB30 + static_cast<uint32_t>(o.kind) * 4);
            Mat4 m = Mat4::Identity();
            mx::Translate(m, F32(exe, 0x004ECAD8 + static_cast<uint32_t>(o.kind) * 8) + o.x,
                          F32(exe, 0x004ECADC + static_cast<uint32_t>(o.kind) * 8), -1.02f);
            mx::Scale(m, s, s, s);
            Draw(static_cast<int>(exe.U32(0x00579F7C + static_cast<uint32_t>(o.route + o.kind * 2) * 4)), m, 10, -1.0f,
                 false, -1, true);
        }
    }
    if (o.timer == 0x37) {
        const int r = Rand(p == 0 ? 0x004173DEu : 0x00417411u);
        if (p == 0) Sound((r & 1) ? 0x1EA9u : 0x11EA9u);
        else Sound((r & 1) ? 0x11DA9u : 0x1DA9u);
    }
}

// 0x00414600 (idle / non-interactive): start button check, prompts (HUD)
void World::Impl::PlayerIdle(Task& t) {
    PlayerStartCheck(t, gameMode != 6 ? 0 : 3);
    // state 3: 0x009C6EF0 = 1; state 9 in mode 6: 0x00406CE0 (PRESS START / INSERT COIN prompt, HUD)
}

// 0x00414FC0(task, new state): join / continue with the start button
bool World::Impl::PlayerStartCheck(Task& t, int newState) {
    PlayerTask& pt = t.As<PlayerTask>();
    const int p = pt.index;
    Player& P = player[p];
    // 0x00413230: start button (mode 4: start or trigger)
    const uint32_t start = gameMode == 4 ? (p == 0 ? pressed & 0xC : pressed & 0xC0000) : (p == 0 ? pressed & 8 : pressed & 0x80000);
    bool joined = false;
    if (start != 0 && gameMode != 7 && (flags5900 & 2) && UseCredit(p, P.state == 4)) {
        joined = true;
        if (gameMode != 6) {  // mode select: Game_SetState(6), 0x009A5900 |= 1, 0x0049DFD0 (world_front.cpp)
            if (frontEnd) front.modeRequest = 6;
            else Note("start press outside game mode 6 (0x00414FC0 -> Game_SetState(6)) not ported");
            flags5900 |= 1;
        }
        if ((flags5900 & 1) == 0) {  // a cutscene runs: wait in state 10 (0x00414690), then take newState
            P.nextState = static_cast<int16_t>(newState);
            newState = 10;
        }
        PlayerSetState(t, newState, true);
    }
    P.flags12C &= ~2u;
    return joined;
}

// 0x00414280: continue countdown of a player (state 4)
void World::Impl::PlayerContinue(Task& t) {
    PlayerTask& pt = t.As<PlayerTask>();
    const int p = pt.index;
    Player& P = player[p];
    PlayerOverlay(t);
    if (P.credit0 != CreditOf(0) || P.credit1 != CreditOf(1) || P.credit2 != 0 || P.credit3 != 0) {
        P.credit0 = CreditOf(0);
        P.credit1 = CreditOf(1);
        P.contTimer = 0x9FFF;
        P.credit2 = P.credit3 = 0;
    }
    if (!CanContinue()) P.contTimer = 1;
    bool expired = false;
    if (subState != 4) {
        if (P.gun.fired != 0 && P.contTimer < 0x8000) P.contTimer = (P.contTimer & ~0xFFF) + 1;
        P.contTimer -= 0x2D;
        expired = P.contTimer < 1;
    }
    // 0x00413FB0(p): not seen
    const bool joined = PlayerStartCheck(t, 1);
    if (!joined && expired) {
        P.contTimer = 0;
        --route;
        PlayerSetState(t, 6, true);  // -> 0x00414420 (not ported)
    }
    // 0x004143A5: the per-player CONTINUE 0x00416880 and digit 0x004168C0 (contTimer >> 12) when the player neither
    // joined nor ran out, in game mode 6 (not training / boss mode), a continue is possible, and the whole-game CONTINUE
    // screen (sub-state 4) is not up: HUD (WorldHud::Player::continueShown). The credit line 0x00406CE0(p, 1): HUD.
    const bool trainingOrBoss = cfg.gameType > 1 && cfg.gameType < 4;
    if (!joined && !expired && gameMode == 6 && !trainingOrBoss && CanContinue() && subState != 4) P.contShown = P.contTimer;
}

// 0x00414420: state 6 (out: the continue countdown ran out, or game mode 7 started with the player in state 4)
void World::Impl::PlayerOutStart(Task& t) {
    PlayerTask& pt = t.As<PlayerTask>();
    const int p = pt.index;
    Player& P = player[p];
    EnemyTask* b = playerBody[p].get();
    // mode != 7: 0x00416900 "GAME OVER" sprite 0x43E at x 0x00579F70[p], y 170 (HUD)
    P.cb7C = 0x004151D0;
    if (gameMode == 7 && b) {
        AnimSetMotion(*b, static_cast<int>(exe.U32(0x004EC8B4 + static_cast<uint32_t>(p) * 4)));  // 0x00411930
        b->anim.nodes[5].model = BodyGunModel(*this, p, 1);  // 0x00416810(t, 1)
    }
    if (b) {  // 0x00415A80: the body beside the players' point (route 0x009C8E88 offsets 0x00579EA8), angles 0
        const CameraDirector& c = runner->camera;
        Mat4 m = Mat4::Identity();
        mx::Translate(m, playerPos[0], playerPos[1], playerPos[2]);
        mx::RotZ(m, c.playerRoll);
        mx::RotY(m, c.playerYaw);
        mx::RotX(m, c.playerPitch);
        const uint32_t e = 0x00579EA8 + static_cast<uint32_t>((p - 2 + route88 * 2) * 0xC);
        const float local[3] = { F32(exe, e), 0.0f, F32(exe, e + 8) };
        mx::Transform(m, local, b->pos);
        b->rot[0] = b->rot[1] = b->rot[2] = 0;
    }
    P.outTimer = 0x78;
    P.flags12C &= ~2u;
    pt.fn = fn::kPlayerOut2;
    PlayerOut(t);
}

// 0x004144C0: state 6 per frame
void World::Impl::PlayerOut(Task& t) {
    PlayerTask& pt = t.As<PlayerTask>();
    const int p = pt.index;
    Player& P = player[p];
    if (gameMode == 7) {  // 0x00415100 -> +0x7C
        PlayerCb7C(t);
        return;
    }
    if (--P.outTimer == 0) {
        PlayerSetState(t, 9, true);  // -> 0x004145C0
        return;
    }
    // 0x00416900: "GAME OVER" sprite (HUD)
}

// 0x00414F40: players in game 0x009C7000 (after Player_HitShake 0x00415270)
void World::Impl::TaskPlayerCount(Task&) {
    HitShake();
    if (players == 0) playersInGame = -1;
    else if (players == 1) playersInGame = (player[0].state == 5 || player[0].state == 7) ? 0 : 1;
    else if (players == 2) playersInGame = 2;
}

// 0x00416B00: muzzle flashes (view space) and tracers (billboards), pass-1 mode 0xC
void World::Impl::PlayerFlashes(int p) {
    Player& P = player[p];
    const int m1 = exe.S16(0x00579F78 + 2 * p), m2 = exe.S16(0x00579F7C + 2 * p);
    for (int i = 0; i < 6; ++i) {
        Player::Flash& f = P.flash[i];
        if (f.counter > 8) f.active = false;
        if (f.active && f.type != 3) {
            Mat4 m = Mat4::Identity();
            mx::Translate(m, f.pos[0], f.pos[1], f.pos[2]);
            mx::RotY(m, f.yaw);
            mx::RotX(m, f.pitch);
            mx::Scale(m, 0.1f, 0.1f, 0.1f);
            Draw(m1 + f.counter, m, 0xC, -1.0f, false, -1, true);
            const float k = f.type == 4 ? 0.75f : 0.5f;
            mx::Scale(m, k, k, k);
            Draw(m2 + f.counter, m, 0xC, -1.0f, false, -1, true);
        }
        ++f.counter;
        Player::Tracer& tr = P.tracer[i];
        if (tr.counter > 0x3B || (tr.counter == 1 && hitFlag[p] != 0)) tr.active = false;
        if (tr.active) {
            for (int k = 0; k < 3; ++k) tr.pos[k] = tr.vel[k] + tr.pos[k];
            if (tr.type == 5) {
                if (modes) SpecialTracer5Draw(*this, tr.pos, tr.counter);  // world_modes_weapons.cpp
            } else {
                Mat4 m = cam.view;
                mx::Translate(m, tr.pos[0], tr.pos[1], tr.pos[2]);
                mx::ClearRotation(m);
                tr.roll += 0x1000;
                mx::RotZ(m, tr.roll);
                Draw(m2 + 2, m, 0xC, -1.0f, false, -1, true);
            }
        }
        ++tr.counter;
        if (modes && runner && (runner->cfg.gameType == 1 || HordeActive(*this))) SpecialExtraDraw(*this, p, i);  // the third ring (game type 1)
    }
}

void World::Impl::FillHud() {
    StampModelWaves();
    memcpy(cam.playerPos, playerPos, 12);
    if (runner) {
        cam.playerPitch = runner->camera.playerPitch;
        cam.playerYaw = runner->camera.playerYaw;
        cam.playerRoll = runner->camera.playerRoll;
    }
    hud.letterbox = letterbox;
    hud.canShoot = canShoot != 0;
    hud.letterboxCounter = letterboxCounter;
    hud.gameMode = gameMode;
    hud.subState = subState;
    hud.continueTimer = contCountdown;
    hud.continueShown = contScreenShown;
    hud.rank = rank;
    hud.shake = shake;
    hud.skippable = runner && runner->skippable;
    hud.skipping = runner && runner->skipping;
    hud.frameCounter = seedCounter;
    hud.flags5900 = flags5900;
    hud.paused = paused;
    hud.pausePlayer = pausePlayer;
    for (int k = 0; k < 3; ++k) hud.portVolumes[k] = modes && modes->optPortRows ? modes->optVolume[k] : -1;  // port only
    hud.portDisplayRequests = modes ? modes->optDisplayRequests : 0;
    hud.portLightgunRequests = modes ? modes->optLightgunRequests : 0;
    hud.portWiiRequests = modes ? modes->optWiiRequests : 0;
    hud.netState = modes && gameMode == 15 ? modes->netState : 0;
    hud.stageResults = stageResults;
    hud.randomizerSeed = RandomizerActive(*this) ? modes->randomizer.seed : 0;
    hud.randomizerHash = RandomizerHash(*this);
    hud.prototype = (prototype != nullptr) && gameMode == 6;
    hud.gameOverTimer = goTimer;
    hud.credits = cfg.credits;
    hud.effects = hudEffects;
    hud.sprites = hudSprites;
    if (residency) hud.texFiles = texFiles;
    else hud.texFiles.clear();
    // appended: FillHud can run more than once in a Step (a mode switch), World::Step clears the list first
    hud.polFileLoads.insert(hud.polFileLoads.end(), polFileLoads.begin(), polFileLoads.end());
    polFileLoads.clear();
    hud.subtitles = hudSubtitles;
    hud.titleSerial = hudTitleSerial;
    hud.titleChapter = cfg.eventMode;
    hud.routeMapScroll = routeMapScroll;
    for (int p = 0; p < 2; ++p) {
        const Player& P = player[p];
        WorldHud::Player& h = hud.player[p];
        h.aim[0] = P.aim[0];
        h.aim[1] = P.aim[1];
        h.fired = P.firedThisFrame;
        h.shots = P.shots;
        h.state = P.state;
        h.lives = P.lives;
        h.livesShown = P.livesShown;
        h.score = P.score;
        h.ammo = P.ammo;
        h.empty = P.empty != 0;
        h.reloadBlink = P.reloadBlink;
        h.hits = P.hits;
        h.continueTimer = P.contTimer;
        h.continueShown = P.contShown;
        h.hitTimer = P.hitTimer;
        h.inputKind = P.inputKind;
        h.crosshair = P.crosshairOn;
        h.crosshairPos[0] = P.crosshair[0];
        h.crosshairPos[1] = P.crosshair[1];
        h.gunX = P.gun.x;
        h.gunY = P.gun.y;
        h.gunValid = P.gun.valid != 0;
    }
}

}  // namespace game
