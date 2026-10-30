// game::World - the game-over screen (game mode 7, 0x00460960) from the frame after the switch to the title switch.
// Spec and verification: notes/agents/twoplayer.md section 5. CONFIRMED = code read + frame-exact against
// build\scene_capture_play1full.txt / play2full.txt (task list, draws, camera); CODE = read, not exercised.
//
// Sub-states (0x009C90A4):
//   0  setup: Mem_Init, resources (pol 0x4D, motions 0x25..0x27; pol 0x4E queued), players' point / angles 0,
//      Task_InitRoot(0x00460BB0): camera task 0x00460E60 (camera path 0x1F), rail + bodies 0x00414F20, player tasks
//      0x00414ED0, count task 0x00414FB0; players in state 4 -> state 6 (0x00414420: death motion 0x004EC8B4[p]);
//      timer 0x009CA0F8 = 200
//   1  tasks; reload (P1 bit 2 / P2 0x20000) with timer < 0xA5 skips; timer 0 -> 2
//   2  Task_InitRoot(0x00460CA0): camera reset + views, "GAME OVER" text task 0x00460CD0; timer 0xB4
//   3  tasks; reload with timer < 0xAF skips; timer 0 -> 4 (game types 2 / 3: back to mode 6, not modelled)
//   4  map screen setup 0x00460F00 (route record 0x009A5920), Task_InitRoot(0x00460FF0)
//   5  map screen 0x004610F0: tasks; reload or the walk's end (0x007DCCE4 = -1) -> Game_SetState(3) (title)

#include <algorithm>
#include <cmath>
#include <cstring>

#include "game/mstack.h"
#include "game/world_internal.h"
#include "game/world_nightshift.h"
#include "game/typing_report.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

struct GoCameraTask : WTask {  // 0x00460E60 (task block 0x58)
    int tick = 0;              // +0x50
};
struct GoTextTask : WTask {    // 0x00460CD0 (block 0x38)
    int counter = 0;           // +0x34
};

}  // namespace

// Sprite effect task 0x00499CE0 (created by 0x00499C60): one 2D sprite with a scale / alpha animation; drawn through
// 0x00499F00 (HUD sprite, WorldHud::effects). The same update as the HUD port's game::SpriteFx (hud.h; the game library
// cannot use game_hud, which links it).
struct SpriteFxTask : WTask {
    WorldHud::Effect e;
    int counter = 0;           // +0x38
    int duration = 0;          // +0x3C
    int scaleMode = 0;         // +0x40
    int alphaMode = 0;         // +0x44
    float alphaStep = 0;       // +0x5C
};

// 0x00499C60(id, x, y, sx, sy, alpha, duration, scaleMode, alphaMode)
void World::Impl::SpawnSpriteFx(int id, float x, float y, float sx, float sy, float alpha, int duration, int scaleMode,
                                int alphaMode) {
    SpriteFxTask* t = NewTask<SpriteFxTask>(WorldTramp<&Impl::TaskSpriteFx>, 0x00499CE0);
    t->e.sprite = id;
    t->e.x = x;
    t->e.y = y;
    t->e.sx = sx;
    t->e.sy = sy;
    t->e.alpha = alpha;
    t->duration = duration;
    t->scaleMode = scaleMode;
    t->alphaMode = alphaMode;
    t->counter = 0;
    t->alphaStep = alphaMode == 0 ? alpha * F32(exe, 0x00564428) : alpha / static_cast<float>(duration);
}

// 0x00499CE0
void World::Impl::TaskSpriteFx(Task& t) {
    SpriteFxTask& s = t.As<SpriteFxTask>();
    WorldHud::Effect& e = s.e;
    hudEffects.push_back(e);  // 0x00499F00(id, x, y, 1.0, sx, sy, alpha)
    const float dur = static_cast<float>(s.duration);
    const float k380 = F32(exe, 0x004C4380), kcc8 = F32(exe, 0x004C4CC8);
    switch (s.scaleMode) {
    case 1:
        e.sx = e.sx - k380 / dur;
        e.sy = e.sy + k380;
        break;
    case 2: case 3: {
        const float f = (s.scaleMode == 2 ? k380 : F32(exe, 0x0055D2B4)) / dur;
        e.sx -= f;
        e.sy -= f;
        break;
    }
    case 4: {
        const float f = k380 / dur;
        e.sx = f + e.sx;
        e.sy = f + e.sy;
        [[fallthrough]];
    }
    case 5: {
        const float f = F32(exe, 0x004C49C0) / dur;
        e.sx = f + e.sx;
        e.sy = f + e.sy;
        break;
    }
    default: break;
    }
    const int c = s.counter, d = s.duration;
    bool kill = false;
    switch (s.alphaMode) {
    case 0:
        if (c <= d - 10) break;
        e.alpha -= kcc8;
        kill = c > d;
        break;
    case 1:
        e.alpha -= s.alphaStep;
        if (e.alpha < F32(exe, 0x004C436C)) e.alpha = 0;
        kill = d < c;
        break;
    case 2:
        if (c < 0x32) e.alpha += F32(exe, 0x004E3100);
        if (d - 0x14 < c) e.alpha -= F32(exe, 0x004C4C88);
        kill = c > d;
        break;
    case 3: case 8:
        kill = d < c;
        break;
    case 4:
        if (c < 0x3C) e.alpha += F32(exe, 0x0055DCF4);
        if (d - 0x1E < c) e.alpha -= F32(exe, 0x0055DD0C);
        kill = d < c;
        break;
    case 5:
        if (c < 10) e.alpha += kcc8;
        if (d - 10 < c) e.alpha -= kcc8;
        kill = d < c;
        break;
    case 6:
        if (c < 10) e.alpha += kcc8;
        kill = d < c;
        break;
    case 7:
        if (d - 0x1E < c) e.alpha -= F32(exe, 0x0055DD0C);
        kill = c > d;
        break;
    default: break;
    }
    if (kill) ts.KillCurrent();  // Task_Kill 0x004A7040
    ++s.counter;
}

// 0x004021D0: every context's position / target = 0x0059C990 (zeros), tick 0, 0x00403BA0, active context 0,
// Cam mode 0/0 (0x00403BD0: rail = 0x0041EBB0), 0x009C6F2C / 2D / 0x009C6FD8 cleared, lights 0x0041DBA0 (renderer)
void World::Impl::CameraReset() {
    CameraDirector& c = runner->camera;
    for (CameraContext& x : c.ctx) {  // 6 dwords from 0x0059C990 to +0xC0 (pos, pitch, yaw, roll) and to +0xD8 (target)
        for (int k = 0; k < 3; ++k) {
            x.pos[k] = F32(exe, 0x0059C990 + 4 * k);
            x.target[k] = F32(exe, 0x0059C990 + 4 * k);
        }
        x.pitch = static_cast<int>(exe.U32(0x0059C99C));
        x.yaw = static_cast<int>(exe.U32(0x0059C9A0));
        x.roll = static_cast<int>(exe.U32(0x0059C9A4));
    }
    c.tick = 0;
    c.ctx2Mode = 0;
    c.activeCtx = 0;
    c.SetMode(0, 0);
    c.returning = c.settled = false;
    c.lookTableOn = 0;
    // Env_InitBlock 0x0041DBA0 on both env blocks, then 0x0041DC70 / 0x0041DCC0 (renderer state, world_light.cpp)
    env.ResetBlocks();
    lightDir0[0] = lightDir0[1] = 0;
    LightScene();
}

// Cam_BuildViews 0x00401F40 (matrix part + the mode commit and 0x009A5900 bit 0 at its end)
void World::Impl::BuildViews() {
    CameraDirector& c = runner->camera;
    BuildContextViews(c.ctx, c.activeCtx & 3, shake);
    c.modeCur = c.modeReq;
    c.subCur = c.subReq;
    if (c.modeCur == 1 && gameMode != 5 && (flags5900 & 0x30) == 0) flags5900 &= ~1u;
    else flags5900 |= 1;
    UpdateCamera();
}

// 0x00460E60: game-over camera (camera path 0x1F at the task's tick, from 10)
void World::Impl::TaskGoCamera(Task& t) {
    GoCameraTask& g = t.As<GoCameraTask>();
    CameraDirector& c = runner->camera;
    c.tick = g.tick;  // 0x009A6110
    CameraContext& x = c.ctx[0];
    float pos[3], tg[3];
    int roll = 0;
    if (cameraBank.Evaluate(0x1F, static_cast<float>(c.tick), pos, tg, &roll, false)) {  // Cam_Evaluate 0x004041E0
        memcpy(x.pos, pos, 12);
        memcpy(x.target, tg, 12);
    }
    ++g.tick;
    ContextLookAngles(x, x.target, 0);  // 0x00403AC0
    BuildViews();
}

// 0x00460CD0: the "GAME OVER" letters (sprite effects 0x43A..0x43D), 0xA5 frames
void World::Impl::TaskGoText(Task& t) {
    GoTextTask& g = t.As<GoTextTask>();
    switch (g.counter) {
    case 0x00: SpawnSpriteFx(0x43A, 320.0f, 240.0f, 1.0f, 1.0f, 0.0f, 0x82, 0, 2); break;
    case 0x78: SpawnSpriteFx(0x43B, 340.0f, 260.0f, 1.0f, 1.0f, 1.0f, 0x14, 0, 0); break;
    case 0x7D: SpawnSpriteFx(0x43C, 310.0f, 250.0f, 2.0f, 1.5f, 1.0f, 0x14, 0, 0); break;
    case 0x82: SpawnSpriteFx(0x43D, 320.0f, 230.0f, 1.0f, 1.0f, 1.0f, 0x14, 0, 0); break;
    case 0x87: SpawnSpriteFx(0x43B, 290.0f, 210.0f, 1.0f, 1.0f, 1.0f, 0x14, 0, 0); break;
    case 0x8C: SpawnSpriteFx(0x43D, 350.0f, 270.0f, 1.0f, 1.0f, 1.0f, 0x14, 0, 0); break;
    case 0x91: SpawnSpriteFx(0x43A, 320.0f, 240.0f, 1.0f, 1.0f, 1.0f, 0x14, 1, 1); break;
    default: break;
    }
    if (g.counter == 0xA5) ts.KillCurrent();
    ++g.counter;
}

// 0x00414F20 (rail task + the player bodies 0x00416450), 0x00414ED0 (player tasks, function 0x00579CD0[state * 5]),
// 0x00414FB0 (count task) - the same block Stage_Start creates
void World::Impl::CreatePlayerTasks() {
    NewTask<WTask>(WorldTramp<&Impl::TaskRail>, fn::kRail);
    PlayerBodyInit(0);
    PlayerBodyInit(1);
    for (int p = 0; p < 2; ++p) {
        const uint32_t f = exe.U32(0x00579CD0 + 0x14 * static_cast<uint32_t>(player[p].state));
        Task* tk = ts.Create(WorldTramp<&Impl::TaskPlayer>, std::make_unique<PlayerTask>());
        PlayerTask& d = tk->As<PlayerTask>();
        d.fn = f;
        d.index = p;
        player[p].task = tk;  // +0x74
    }
    NewTask<WTask>(WorldTramp<&Impl::TaskPlayerCount>, fn::kPlayerCount);
}

// =====================================================================================================================
// Map screen (sub-states 4 / 5): the agents walk the route of the stages played over a scrolling map
// =====================================================================================================================

namespace {
struct MapAgentTask : EnemyTask {  // 0x004614C0 / 0x004618C0 (block 0x13F4)
    int endMotion = 0;             // +0x1350 motion blended to when the walk ends
    int xOffset = 0;               // +0x1354
    int timer = 0;                 // +0x1358
    int frames = 0;                // +0x1320
};
struct FootprintTask : WTask {     // 0x00461AC0 (block 0x48, created by 0x00461A90)
    int x = 0, y = 0;              // +0x34 / +0x38
    int counter = 0;               // +0x3C
    int mode = 0;                  // +0x40 (0: y + scroll, 1: scroll - y, 2: y)
};
}  // namespace

// 0x00460F00
void World::Impl::MapInit() {
    for (const Request& r : resQueue) ProcessRequest(r);  // Res_FlushRequests 0x0041D970
    resQueue.clear();
    // no route recorded (or the first stage's second entry is scene 0): the default route 0x0059351C (6 rows)
    if (routeRec[0][0] == 0xFF || routeRec[0][1] == 0) {
        for (int row = 0; row < 6; ++row)
            for (int i = 0; i < 16; ++i) {
                const uint8_t v = static_cast<uint8_t>(exe.S16(0x0059351C + static_cast<uint32_t>(row * 16 + i)) & 0xFF);
                routeRec[row][i] = v;
                if (v == 0xFF) break;
            }
    }
    MapState& m = map;
    m.scene = static_cast<int8_t>(routeRec[0][0]);  // 0x007DCCE8
    m.mapY = -0x92;                                 // 0x007DCCF4
    m.scroll = 0;                                   // 0x007DCCD8
    m.mapX = -0x10;                                 // 0x007DCCFE
    m.screenY = 0x92;                               // 0x007DCCFC
    m.dir = 8;                                      // 0x007DCCEA
    m.idx = m.step = m.stage = m.state = 0;         // 0x007DCCEE / EC / E6 / E4
    const uint32_t e = 0x00567A04 + static_cast<uint32_t>(m.scene * 0x18);
    m.targetX = exe.S16(e);                         // 0x007DCCF8
    m.targetY = exe.S16(e + 2);                     // 0x007DCCFA
    // Task_InitRoot(0x00460FF0)
    ts.FreeDead();
    NewTask<WTask>(WorldTramp<&Impl::TaskMapViews>, 0x00482C40);  // 0x004610D0: Cam_BuildViews thunk task
    CameraReset();
    runner->camera.ctx[0].pos[2] = -7.0f;  // 0x009A60C8
    CreatePlayerTasks();
    m.footprint = 0x14C2;  // 0x007DCCE0
    const bool out1 = player[0].state == 6, out2 = player[1].state == 6;
    auto agent = [&](int kind, int motion, int endMotion, int xOff) {  // 0x004613C0
        MapAgentTask* a = NewTask<MapAgentTask>(WorldTramp<&Impl::TaskMapAgent>, 0x004614C0);
        a->anim.kind = static_cast<int16_t>(kind);  // game type 1: the chosen character's skeleton
        a->anim.motion[0] = motion;
        EnemyInitPose(*a);  // 0x00410440
        a->nodeCb = 0x00411050;
        a->anim.rotOrder = 1;
        a->parts = 0;       // 0x00412440: no part list
        a->flags34 |= 0x80000;
        a->pos[0] = a->pos[1] = 0;
        a->pos[2] = -100.0f;
        a->rot[0] = 0x4000;
        a->rot[1] = a->rot[2] = 0;
        a->frames = 0;
        a->endMotion = endMotion;
        a->xOffset = xOff;
        a->timer = 0;
    };
    if (out1 == out2) {  // both out (or neither): agents 0x39 and 0x3A walk side by side
        agent(0x39, 0x35B, 0x338, 0x18);
        MapAgentTask* a = NewTask<MapAgentTask>(WorldTramp<&Impl::TaskMapAgent>, 0x004618C0);  // 0x004617F0
        a->anim.kind = 0x3A;
        a->anim.motion[0] = 0x358;
        EnemyInitPose(*a);
        a->nodeCb = 0x00411050;
        a->anim.rotOrder = 1;
        a->parts = 0;
        a->pos[0] = a->pos[1] = 0;
        a->flags34 |= 0x80000;
        a->pos[2] = -100.0f;
        a->rot[0] = 0x4000;
        a->anim.counter[0] = 0x10;
        // (both agents: CONFIRMED on build\scene_capture_2p_gameover.txt, notes/agents/twoplayer2.md)
    } else if (out1) {
        agent(0x39, 0x35B, 0x338, 0);
    } else {
        agent(0x3A, 0x358, 0x339, 0);
        m.footprint = 0x14D2;
    }
    NewTask<WTask>(WorldTramp<&Impl::TaskMapRoute>, 0x00461180);  // 0x00461160
    ++subState;
}

// 0x004610F0
void World::Impl::MapFrame() {
    ts.RunFrame();
    if (pressed & 0x20002) map.state = -1;  // reload: end
    if (map.state != -1) return;
    // players in state 6: 0x0041EBB0 (no-op); 0x00406FD0: 0x00407030(0) (free play 0x009C8E70 = 0, credits 0) and the
    // credit slots 0x009C8E60.. = 0, coin states 0x00407060 (the mode select sets them again for the next game);
    // 0x00413CB0 -> 0x0049DFD0, 0x0040E960: Game_SetState(0x004E1014[mode 7]) = 3 (title). 2p_gameover 11373.
    cfg.freePlay = false;
    cfg.credits = 0;
    SetGameMode(static_cast<int>(static_cast<int8_t>(exe.S16(0x004E1014 + 7) & 0xFF)));
}

// 0x00482C40: Cam_BuildViews every frame
void World::Impl::TaskMapViews(Task&) { BuildViews(); }

// 0x00461180: walks the route record (0x009A5920: one row of scenes per stage) through the path table 0x00567A04
// ({x, y} s16 points, 6 per (stage, scene), stage rows of 0x27 scenes, -1 ends) and draws the map tiles (sprites
// 0x005679FC[0..3] + 0..74, 5 x 15 tiles of 128 x 32 per map, scrolled by 0x007DCCD8)
void World::Impl::TaskMapRoute(Task&) {
    MapState& m = map;
    auto point = [&](int step) {
        return 0x00567A04 + static_cast<uint32_t>((step + (m.stage * 0x27 + m.scene) * 6) * 4);
    };
    for (;;) {
        if (m.mapX != m.targetX || m.mapY != m.targetY) break;
        ++m.step;
        bool next = true;
        if (exe.S16(point(m.step)) == -1) {
            m.step = 0;
            ++m.idx;
            m.scene = static_cast<int8_t>(routeRec[m.stage][m.idx & 15]);
            if (m.scene == -1) {
                next = false;
                if (m.stage != 5) {
                    ++m.stage;
                    m.idx = 0;
                    m.scene = static_cast<int8_t>(routeRec[m.stage][0]);
                    next = m.scene != -1;
                }
                if (!next) {
                    m.step = 0;
                    m.state = 1;
                    break;
                }
            }
        }
        if (m.stage == 4 && m.idx == 0 && m.step == 0) {
            for (int i = 0; i < 16; ++i)
                if (routeRec[3][i] == 0x19) {
                    m.step = 3;
                    break;
                }
        }
        m.targetX = exe.S16(point(m.step));
        m.targetY = exe.S16(point(m.step) + 2);
    }
    // map tiles 0x005679FC[0..3] + row * 5 + col (0x0041C6D0, depth 120): the HUD port's RouteMapBackground(scroll)
    routeMapScroll = m.scroll;
}

// 0x004614C0 (agent 1) / 0x004618C0 (agent 2, both players out)
void World::Impl::TaskMapAgent(Task& t) {
    MapAgentTask& a = t.As<MapAgentTask>();
    MapState& m = map;
    const bool second = a.fn == 0x004618C0;
    if (!second) {
        m.dir = 0;
        switch (m.state) {
        case 0:
            if (m.mapY != m.targetY) m.dir = m.targetY <= m.mapY ? 2 : 8;
            if (m.mapX != m.targetX) m.dir = m.targetX <= m.mapX ? 6 : 4;
            break;
        case 1:
            a.timer = 0;
            AnimBlendTo(a, a.endMotion, 0, 10);  // 0x004119A0
            m.state = 2;
            break;
        case 2: case 3:
            if (a.timer == 0x78) m.state = -1;
            ++a.timer;
            break;
        default: break;
        }
        switch (m.dir) {
        case 2:
            m.screenY += 4;
            m.mapY -= 4;
            a.rot[1] = 0x8000;
            break;
        case 4:
            m.mapX += 4;
            a.rot[1] = 0x4000;
            break;
        case 6:
            m.mapX -= 4;
            a.rot[1] = -0x4000;
            break;
        case 8:
            m.mapY += 4;
            if (m.mapY < 0x578 && m.screenY < 1) m.scroll += 4;
            else m.screenY -= 4;
            a.rot[1] = 0;
            break;
        default: break;
        }
        if (m.dir != 0) {  // 0x00461A90: a footprint
            int y, mode;
            if (m.mapY < 0x578) {
                if (m.screenY < 1) { y = m.scroll; mode = 1; }
                else { y = m.screenY; mode = 0; }
            } else {
                y = m.screenY;
                mode = 2;
            }
            FootprintTask* f = NewTask<FootprintTask>(WorldTramp<&Impl::TaskFootprint>, 0x00461AC0);
            f->x = m.mapX;
            f->y = y;
            f->mode = mode;
            f->counter = 0;
        }
    } else {
        if (m.state == 2) {
            AnimBlendTo(a, 0x339, 0, 10);
            m.state = 3;
        }
        switch (m.dir) {
        case 2: a.rot[1] = 0x8000; break;
        case 4: a.rot[1] = 0x4000; break;
        case 6: a.rot[1] = -0x4000; break;
        case 8: a.rot[1] = 0; break;
        default: break;
        }
    }
    const float k = F32(exe, 0x00564514);  // -100
    const int sx = second ? m.mapX - 0x18 : a.xOffset + m.mapX;
    const int sy = second ? m.screenY + 0x18 : m.screenY;
    const float fx = (static_cast<float>(sx) * k) / focal, fy = (static_cast<float>(sy) * k) / focal;
    {  // placed in front of the camera: (fx, fy, -100) x camera matrix
        const float local[3] = { fx, fy, -100.0f };
        mx::Transform(cam.inverse, local, a.pos);
    }
    // marker 0x145B under the agent (view space, pass-1 mode 0xF)
    Mat4 mk = Mat4::Identity();
    mx::Translate(mk, fx, fy, -99.5f);  // 0xC2C70000
    mx::RotX(mk, 0x4000);
    mx::Scale(mk, 3.0f, 3.0f, 3.0f);
    Draw(0x145B, mk, 0xF, -1.0f, false, -1, true);
    AnimDraw(a);  // 0x00411090 (flags 0x80000: no shadow; light 0x0041DC70 / 0x0041DCC0: renderer)
    ++a.anim.counter[0];
    ++a.frames;
}

// 0x00461AC0: a footprint (view space, scale 0.2, 1000 frames)
void World::Impl::TaskFootprint(Task& t) {
    FootprintTask& f = t.As<FootprintTask>();
    const float k = F32(exe, 0x00568FF4);  // -101
    int px = f.x, py = 0;
    bool draw = true;
    if (f.mode == 0) py = f.y + map.scroll;
    else if (f.mode == 1) py = map.scroll - f.y;
    else if (f.mode == 2) py = f.y;
    else draw = false;
    if (draw) {
        const float fx = (static_cast<float>(px) * k) / focal, fy = (static_cast<float>(py) * k) / focal;
        Mat4 m = Mat4::Identity();
        mx::Translate(m, fx, fy, -101.0f);
        mx::Scale(m, 0.2f, 0.2f, 0.2f);
        Draw(map.footprint, m, 8, -1.0f, false, -1, true);
    }
    if (++f.counter > 1000) ts.KillCurrent();
}

// Game mode 7, 0x00460960
void World::Impl::GameOverFrame() {
    if (cfg.portTyping && typingReferenceActive) {
        if (subState==0) StartTypingReport(*this,false);
        else TypingReportFrame(*this);
        return;
    }
    switch (subState) {
    case 0: {
        if(runner&&runner->cfg.gameType<2)
            PortLeaderboardComplete(*this,runner->cfg.gameType,0,std::min(cfg.eventMode,6));
        ts.Reset();  // Mem_Init 0x004A7310 (0x0041D510 / 0x0041D540: models freed, request ring cleared)
        resQueue.clear();
        if (residency) {
            // 0x0041D650(0x4D) (game type 1: per-character pol files), 0x0041D7D0(5), motions 0x26 / 0x27 / 0x25,
            // texture 0x155, flushed (0x0041D970); then texture 0x14F and pol 0x4E queued (game types other than 2 / 3)
            modelLoaded.assign(0x4000, 0);
            ResetTexFiles();
            ProcessRequest({ 0x52, 1 });
            ProcessRequest({ 0x52, 0x4D });
            for (int m : { 0x26, 0x27, 0x25 }) ProcessRequest({ 0x56, static_cast<uint32_t>(m) });
            ProcessRequest({ 0x54, 0x155 });
            if (runner->cfg.gameType < 2 || runner->cfg.gameType > 3) {
                resQueue.push_back({ 0x54, 0x14F });
                resQueue.push_back({ 0x52, 0x4E });
            }
        }
        pendingMotionFiles.clear();
        for (float& v : playerPos) v = 0;  // 0x009C71E0..0x009C71F4
        memset(runner->camera.playerPos, 0, 12);
        runner->camera.playerPitch = runner->camera.playerYaw = runner->camera.playerRoll = 0;
        targets.clear();
        targetCopy.clear();
        // Task_InitRoot(0x00460BB0)
        ts.FreeDead();
        {
            // 0x00460EC0: camera task, camera reset 0x004021D0, tick 10, first run at once
            Task* tk = ts.Create(WorldTramp<&Impl::TaskGoCamera>, std::make_unique<GoCameraTask>());
            GoCameraTask& g = tk->As<GoCameraTask>();
            g.fn = 0x00460E60;
            CameraReset();
            g.tick = 10;
            runner->camera.tick = 10;
            TaskGoCamera(*tk);
        }
        CreatePlayerTasks();
        for (Player& P : player)  // players still counting down -> out (0x00414420)
            if (P.state == 4 && P.task) PlayerSetState(*P.task, 6, true);
        goTimer = 200;  // 0x009CA0F8
        flags5900 &= ~2u;
        // 0x0041D350 / 0x0041D3A0(0, 0, -1) (sound stop; the latter a debug string), 0x0041CFD0(0x10000009) (game-over music)
        SoundStopAll();
        Sound(0x10000009);
        subState = 1;
        return;
    }
    case 1: {
        ts.RunFrame();  // 0x004A7240
        if (goTimer != 0xC6) {
            if (goTimer < 0xA5 && (pressed & 0x20002)) goTimer = 1;
        } else if (runner->cfg.gameType == 1) {
            ModesSaveItems(*this);  // the inventory 0x009C90C0 -> 0x009C9F3D, Game_Save 0x004011F0 (world_modes_items.cpp)
        }
        if (--goTimer == 0) subState = 2;
        return;
    }
    case 2: {
        // Task_InitRoot(0x00460CA0): 0x00460EF0 = camera reset + Cam_BuildViews, then the text task
        ts.FreeDead();
        CameraReset();
        BuildViews();
        GoTextTask* g = NewTask<GoTextTask>(WorldTramp<&Impl::TaskGoText>, 0x00460CD0);
        g->counter = 0;
        goTimer = 0xB4;
        subState = 3;
        return;
    }
    case 3: {
        ts.RunFrame();
        if (goTimer < 0xAF && (pressed & 0x20002)) goTimer = 1;
        if (--goTimer == 0) {
            // 0x009A3404 = 0xB0; game types 2 / 3: players in state 6 -> 0 (0x0041EBB0), 0x0049DFD0, Game_SetState(6)
            // (training: the lesson select, boss mode: the boss select)
            const int gt = runner->cfg.gameType;
            if (gt > 1 && gt < 4 && frontEnd) {
                goTimer = 0;
                for (Player& P : player)
                    if (P.state == 6) P.state = 0;
                front.modeRequest = 6;
                return;
            }
            subState = 4;
        }
        return;
    }
    case 4:
        MapInit();  // 0x00460F00
        return;
    case 5:
        MapFrame();  // 0x004610F0
        return;
    default:
        Note("game mode 7 sub-state " + std::to_string(subState) + " not ported");
        return;
    }
}

}  // namespace game
