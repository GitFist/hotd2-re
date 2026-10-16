// game::World - the front end: the game from boot (World::Boot). Game_Update 0x0040E730 with the mode dispatch
// Game_UpdateMode 0x004608A0 for every game mode, the mode switch (Game_SetState 0x0040E850 / Game_ApplyState
// 0x0040E860 / Game_NextMode 0x0040E960), and the modes outside a stage:
//   16  boot logos 0x0041E1F0 (PC bitmaps "segalogor", "empireBlack": 301 frames each, fade in / hold / out)
//    3  title 0x00495EB0 (camera path 0x1D flythrough of stage-1 section objects, logo, PRESS START; 480 frames)
//    4  mode select 0x00496200 (task 0x00496960, bitmap menu; a confirmed entry 0..3 lets the player join -> mode 6)
//    5  attract demo 0x00426800 (demo table 0x00589828, demo timer 0x00426960; the stage itself is World::StartStage)
//    9  how-to-play 0x004996C0 (script task 0x00499FE0, camera path 0x1E, scripted zombies; 1699 frames)
//   10 / 11  story sequences 0x0041F9B0 / 0x0041FB00 (advevtbl / adv2evtbl event scripts until Stage_End)
// The stage modes reuse StartStage / StageFrame / Mode6Frame / GameOverFrame. Spec and verification:
// notes/agents/frontend.md (tests/frontend_test.cpp against the boot / attract / play captures).

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

#include "game/menu_text.h"
#include "game/mstack.h"
#include "game/shoot_camera.h"
#include "game/world_internal.h"
#include "game/world_modes.h"
#include "game/world_prototype.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

// 0x004E1014: next game mode per mode (Game_NextMode 0x0040E960; the three language rows of 0x005798F0 are the same
// table): 0 -> 16, 3 -> 9, 5 -> 11, 6 -> 3, 7 -> 3, 9 -> 5, 10 -> 3, 11 -> 10
int NextMode(const ExeImage& exe, int mode) { return static_cast<int8_t>(exe.S16(0x004E1014 + static_cast<uint32_t>(mode)) & 0xFF); }

struct MenuTask : WTask {   // 0x00496960 (block 0x44)
    uint32_t angle = 0;     // +0x34 (cursor * -0x10000 / 6) & 0xFFFF
    int anim = 0;           // +0x38 (8 after a move)
    int dir = 0;            // +0x3C (-1 up, 1 down)
    int prev = 0;           // +0x40 entry before the move
    int pointerRow = -2;    // port: -1 outside the list, -2 no pointer sample yet
    int pointerX = 0, pointerY = 0;
};

// Port only (WorldConfig::portModernMenu): the modern mode select's list. Eleven entries in the cursor's order, left-aligned
// under the HOUSE OF THE DEAD 2 logo (top-left of the background, to y ~135), options-screen font (game/menu_text.h)
// scaled kModernScale, rows kModernPitch apart from y kModernY0: 148 .. 418
// (visible capital letters end at y 458), x kModernX. Clear of the logo strip sprite 0xAB1, "Presented by SEGA" (x >= 480, y >= 390)
// and the copyright line (x >= 410, y >= 440).
constexpr float kModernX = 24.0f, kModernY0 = 148.0f, kModernPitch = 27.0f, kModernScale = 1.25f;
constexpr int kModernEntries = 11;
const char* const kModernEntry[kModernEntries] = { "Arcade mode", "Original mode", "Training mode", "Boss mode", "Rankings",
                                                   "Options",     "Network",       "Scene Select",  "Horde Mode", "Randomizer", "Exit" };

// The font's 32-pixel textures have transparent space above the letters: capitals occupy roughly rows 12..32.
// Centre each hit band on the visible letters, not the texture origin (which selected the next option while aiming
// at the lower half of a label). Keep adjacent bands disjoint, with one cell of horizontal padding on either side.
int ModernMenuRowAt(const ExeImage& exe, int x, int y) {
    const float adv = MenuTextAdvance(exe, kModernScale);
    const float top = kModernY0 + 22.0f * kModernScale - kModernPitch * 0.5f;
    const float fy = static_cast<float>(y) - top;
    if (fy < 0.0f) return -1;
    const int row = static_cast<int>(fy / kModernPitch);
    if (row >= kModernEntries) return -1;
    const float w = static_cast<float>(strlen(kModernEntry[row])) * adv;
    const float fx = static_cast<float>(x);
    if (fx < kModernX - adv || fx >= kModernX + w + adv) return -1;
    return row;
}

}  // namespace

// =====================================================================================================================
// Boot / Game_Update
// =====================================================================================================================

void World::Boot(const WorldConfig& c) {
    Impl& w = *impl_;
    w.cfg = c;
    w.cfg.carry.valid = false;
    w.frontEnd = true;
    w.frame = c.startFrame - 1;  // FrontStep counts this frame
    w.gameMode = 0x10;           // Game_Init: game mode 16, sub-state 0
    w.subState = 0;
    w.seedCounter = 0;
    w.front = Impl::Front{};
    w.front.demoIndex = c.demoIndex;
    w.ts.Reset();
    w.ts.SetNextSerial(c.firstSerial);
    // the camera director outlives the stages (the title / mode select / how-to-play cameras use it too)
    w.runner = std::make_unique<EventRunner>(w.mem, w, &w.env, &w.exe);
    w.runner->camera.SetCameraBank(&w.cameraBank);
    for (auto& P : w.player) P.state = 9;
    // the captures' title frames: no credits, not free play (0x009C8E60 / 0x009C8E70 = 0)
    w.cfg.credits = 0;
    w.cfg.freePlay = false;
    w.FrontStep();
    ModesBoot(w);  // the persistent data (Game_Init's Game_Load 0x004A0B60; world_modes_menu.cpp); notes kept in frame 0
}

void World::ForceMode(int mode, uint32_t nextSerial, uint32_t randState, int demoIndex) {
    Impl& w = *impl_;
    w.front.modeRequest = -1;
    w.resQueue.clear();
    w.SetGameMode(mode);
    w.front.menuConfirmed = false;
    w.ts.SetNextSerial(nextSerial);
    w.rng.state = randState;
    w.front.demoIndex = demoIndex;
}

// Game_Update 0x0040E730 (the AUTORELOAD joins 0x007C17AC are off)
void World::Impl::FrontStep() {
    ++frame;
    BeginFrame();
    ++seedCounter;       // 0x009A32A0
    ++frameCounter5C50;  // 0x009A5C50
    ++frameCounter2BAC;   // 0x009A2BAC (0x0040E730)
    StepResources(cfg.resourceStepsPerFrame);
    struct CurGuard {  // task functions find their world through CurrentWorld()
        CurGuard(World::Impl* w) { SetCurrentWorld(w); }
        ~CurGuard() { SetCurrentWorld(nullptr); }
    } guard(this);
    inFrontStep = true;
    // Game_UpdateInput 0x0040E4D0 (Input_Poll, 0x0040D590: 0x009A5C40 + 1, buttons, shots, gun records). The demo's gun
    // records are written by its auto-gun (0x00415710): mode 5 takes only the buttons (World::Step does the same).
    if (gameMode == 5) FrontButtons();
    else InputFrame();
    ++front.bootFrames;
    PauseFrame();  // 0x0040E500 (the toggle needs mode 6)
    if (!paused) {
        if (!PrototypeTryStart(*this)) {
            if (PrototypeActive(*this)) PrototypeFrame(*this);
            else FrontModeFunction();  // Game_UpdateMode 0x004608A0
        }
        FrontPressStart();    // its tail 0x004067D0
    }
    FrontApplyMode();   // Game_ApplyState 0x0040E860
    FrontStartCheck();  // 0x004043C0
    inFrontStep = false;
    ts.FreeDead();
    UpdateLightsFrame();
    FillHud();
    hud.modeFrames = front.modeFrames;
    hud.bootFrames = front.bootFrames;
    hud.demoIndex = front.demoIndex;
    hud.menuCursor = front.menuCursor;
    hud.gameType = front.gameType;
}

// Game_UpdateInput's button part (0x0040D590 -> 0x0040D6C0 / 0x0040D850): held 0x009C9034 / pressed 0x009C9028
void World::Impl::FrontButtons() {
    WorldInput in[2] = { input[0], input[1] };
    for (int p = 0; p < 2; ++p) {
        if (!inputEdge[p]) in[p].pressed = 0;
        inputEdge[p] = false;
    }
    held = (in[0].held & 0xFFF) | ((in[1].held & 0xFFF) << 16);
    pressed = (in[0].pressed & 0xFFF) | ((in[1].pressed & 0xFFF) << 16);
}

// Game_UpdateMode 0x004608A0 (Actor_InitTypeTable 0x0040AC90 on a sub-state-0 frame: the table is static)
void World::Impl::FrontModeFunction() {
    switch (gameMode) {
    case 0x10: BootFrame(); break;
    case 3: TitleFrame(); break;
    case 4: MenuFrame(); break;
    case 5: DemoFrame(); break;
    case 6:
        if (subState == 0) {
            GameStartFrame();
        } else if (subState == 1 && nextEventMode >= 0) {
            ChainStage();  // the next stage (world_st2.cpp)
            SetCurrentWorld(this);
        } else {
            const bool count = subState == 2;  // 0x004601D0 ends with 0x009A5C44 + 1
            Mode6Frame();
            if (count) ++front.modeFrames;
        }
        break;
    case 7: GameOverFrame(); break;
    case 9: TutorialFrame(); break;
    case 10: case 11: AdvFrame(); break;
    default:
        // 8 ranking 0x0041A5E0, 12 options 0x004869E0, 15 network 0x0049F380 (world_modes*.cpp)
        if (ModesFrame(*this)) break;
        Note("game mode " + std::to_string(gameMode) + " not ported");
        break;
    }
}

// 0x004067D0: the blink counter follows the boot frame counter 0x009A5C40 (0x005A4D44 += its change since the last
// call; 0x004067A0 resets both to 0), then "PRESS START BUTTON" in the modes of the table 0x004C4B90
void World::Impl::FrontPressStart() {
    const int32_t now = static_cast<int32_t>(front.bootFrames);
    front.blink += now - front.blinkLast;
    front.blinkLast = now;
    if (exe.S16(0x004C4B90 + 4 * static_cast<uint32_t>(gameMode)) != 0 && front.modeFrames > 3 && (front.blink & 0x20) == 0)
        hudSprites.push_back(PressStartSprite());  // definition 0x005A4CE8 via Spr_Draw 0x0041C630
}

void World::Impl::FrontResetBlink() { front.blink = front.blinkLast = 0; }  // 0x004067A0 (+ HUD flags 0x005A4D38..)

// Game_ApplyState 0x0040E860: a requested mode is committed at the end of the frame (Mem_Init, model slots freed,
// 0x009A5900 = (& ~0x38) | 2, players / route 0, hit shakes off, 0x009A21C0 = 0, sub-state 0, players' state 9 outside
// modes 6 / 7)
void World::Impl::FrontApplyMode() {
    if (front.modeRequest < 0) return;
    // Connected players stay in the room after a game or a mode's Back action.
    const int m = cfg.portNetworkRoom && (front.modeRequest == 3 || front.modeRequest == 4) ? 15 : front.modeRequest;
    front.modeRequest = -1;
    resQueue.clear();
    SetGameMode(m);
    front.menuConfirmed = false;
    if (cfg.portNetworkRoom && m == 15) {
        residency = false;  // the lobby's font/background are not restricted to the previous stage's textures
        paused = pauseHist[0] = pauseHist[1] = false;
        front.sceneSelect = false;
        front.sceneStart = -1;
        front.gameType = 0;
        front.menuCursor = 0;
        front.modeFrames = 0;
        FrontScreensFree();
        if (modes) modes->netState = 0;
    }
}

// 0x00404460: start (P1 0x8 / P2 0x80000) in 0x009C9028; 0x009A3790 = the player (P1 when both)
static bool StartPressed(World::Impl& w) {
    if ((w.pressed & 0x80008) == 0) return false;
    w.front.startPlayer = static_cast<int>(~(w.pressed >> 3) & 1);
    return true;
}

// 0x0040AC10: sound 0x004ABEF0(0, 0, 0), 0x009A5C44 = 0, credits cleared (0x00406FD0 -> 0x00407030(0)), 0x009C8FB4..B6
// = 0, pause state reset 0x0040E980
void World::Impl::FrontAfterStart() {
    front.modeFrames = 0;
    cfg.freePlay = false;
    cfg.credits = 0;
}

// 0x004043C0 (after Game_ApplyState, so its request is committed at the end of the next frame)
void World::Impl::FrontStartCheck() {
    switch (gameMode) {
    case 3:
        if (front.modeFrames > 2 && StartPressed(*this)) {
            Sound(0x121A9);
            FrontAfterStart();
            front.menuCursor = 0;  // 0x009A2226
            front.modeRequest = 4;
        }
        break;
    case 5: case 9: case 10: case 11:
        if (front.modeFrames > 2 && StartPressed(*this)) {
            Sound(0x121A9);
            FrontAfterStart();
            front.modeRequest = 3;
        }
        break;
    default: break;
    }
}

// =====================================================================================================================
// 2D bitmap screens (0x0049E950 / 0x0049EB30 / 0x0049EAD0 / 0x0049EAF0 / 0x0049ED70)
// =====================================================================================================================

// 0x0049E950(file, x, y, w, h, z, u, v, alpha): the next slot, if the bitmap loads (LoadImageA, 0x0049E9F0)
int World::Impl::FrontScreenAdd(uint32_t nameVa, int x, int y, int w, int h, float z, float u, float v, float alpha) {
    if (front.screens.size() >= 0x40) return 0;
    std::string name = exe.CStr(nameVa);
    std::replace(name.begin(), name.end(), '\\', '/');
    if (!std::filesystem::exists(std::filesystem::path(gameDir) / name)) {
        Note("front-end bitmap " + name + " missing (slot not added, like 0x0049E950)");
        return 0;
    }
    FrontScreen s;
    s.file = exe.CStr(nameVa);
    s.x = static_cast<float>(x);
    s.y = static_cast<float>(y);
    s.w = static_cast<float>(w);
    s.h = static_cast<float>(h);
    s.z = z;
    s.u = u;
    s.v = v;
    s.alpha = alpha;
    s.visible = false;
    front.screens.push_back(s);
    return 1;
}

void World::Impl::FrontScreensFree() { front.screens.clear(); }  // 0x0049ED70

// 0x0049EAF0 (opaque slots at once) + 0x0049EB50 (alpha < 1 at render time): every visible slot is drawn this frame
static void ScreensDraw(World::Impl& w) {
    w.hud.screens.clear();
    for (const auto& s : w.front.screens) {
        if (!s.visible) continue;
        WorldHud::Screen o;
        o.file = s.file;
        o.x = s.x;
        o.y = s.y;
        o.w = s.w;
        o.h = s.h;
        o.z = s.z;
        o.u = s.u;
        o.v = s.v;
        o.alpha = s.alpha;
        w.hud.screens.push_back(o);
    }
}

// StartStage from a mode function: the tracer records 0x009A2460 are never reset by the original (their roll keeps
// turning +0x1000 per frame from one stage / demo to the next); StartStage's player reset would clear them
void World::Impl::FrontStartStage(const WorldConfig& c) {
    int rolls[2][6];
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < 6; ++i) rolls[p][i] = player[p].tracer[i].roll;
    const std::vector<World::SoundEvent> keep = sounds;  // StartStage begins a frame: keep this frame's sound calls
    StartStage(c);
    sounds.insert(sounds.begin(), keep.begin(), keep.end());
    SetCurrentWorld(this);
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < 6; ++i) player[p].tracer[i].roll = rolls[p][i];
}

// =====================================================================================================================
// Game mode 16: boot logos (0x0041E1F0)
// =====================================================================================================================

void World::Impl::BootFrame() {
    auto logo = [&](uint32_t names) {  // 0x0041DD60 / 0x0041DFA0: six tiles, z 0.1 (0x3DCCCCCD), all visible
        front.bootCounter = 0;
        front.bootFade = 0;
        FrontScreensFree();
        const float z = F32(exe, 0x004C4CC8);  // 0.1
        static const int kTile[6][4] = { { 0, 0, 0x100, 0x100 }, { 0x100, 0, 0x100, 0x100 }, { 0, 0x100, 0x100, 0x100 },
                                         { 0x100, 0x100, 0x100, 0x100 }, { 0x200, 0, 0x80, 0x100 }, { 0x200, 0x100, 0x80, 0x100 } };
        for (int i = 0; i < 6; ++i)
            FrontScreenAdd(names - 0x18 * static_cast<uint32_t>(i), kTile[i][0], kTile[i][1], kTile[i][2], kTile[i][3], z,
                           i < 4 ? 1.0f : 0.5f, 1.0f, 1.0f);
        for (auto& s : front.screens) s.visible = true;  // 0x0049EB30(0..5, 1)
        ++subState;
    };
    auto fade = [&] {  // 0x0041DEF0 / 0x0041E130: +0.01 per frame for 100 frames, hold 100, -0.01 (24-bit float)
        const float step = F32(exe, 0x004C4CC0);
        float a;
        if (front.bootCounter < 100) {
            front.bootFade = front.bootFade + step;
            if (F32(exe, 0x004C4380) < front.bootFade) front.bootFade = 1.0f;
            a = front.bootFade;
        } else if (front.bootCounter < 200) {
            a = 1.0f;
        } else {
            front.bootFade = front.bootFade - step;
            if (front.bootFade < F32(exe, 0x004C436C)) front.bootFade = 0.0f;
            a = front.bootFade;
        }
        for (auto& s : front.screens) s.alpha = a;  // 0x0049EAD0 (every slot)
        ScreensDraw(*this);                          // 0x0049EAF0
        if (300 < ++front.bootCounter) ++subState;
        // Port only (WorldConfig::portFeatures): either player's trigger / reload / start press (bits 4 / 2 / 8; the
        // keyboard's Space / Backspace / Enter arrive as the same bits) ends this logo at once. The original has no
        // skip: the next sub-state frees the screens and shows the next logo (or the title). hud.screens is only
        // rewritten by ScreensDraw (the natural path ends with every tile at alpha 0): the skipped tiles go now.
        else if (cfg.portFeatures && (pressed & 0x000E000Eu)) {
            ++subState;
            hud.screens.clear();
        }
    };
    switch (subState) {
    case 0: logo(0x00588D44); break;  // "tex\segalogor_00.bin" .. "_11" (0x00588D44, D2C, D14, CFC, CE4, CCC)
    case 1: fade(); break;
    case 2: FrontScreensFree(); ++subState; break;  // thunk 0x0049ED70
    case 3: logo(0x00588DD4); break;  // "tex\empireBlack_00.bin" .. (0x00588DD4 .. 0x00588D5C)
    case 4: fade(); break;
    case 5:  // 0x0041E1D0: straight to the title (no Game_SetState)
        gameMode = 3;
        subState = 0;
        FrontScreensFree();
        break;
    default: break;
    }
}

// =====================================================================================================================
// Game mode 3: title (0x00495EB0)
// =====================================================================================================================

void World::Impl::TitleFrame() {
    if (subState == 0) {
        // 0x0041D350 (sounds), Mem_Init, 0x0049DFD0, model slots freed, resources (stage 3 request, models 0x17A0 /
        // 0x1AFF / 0x17FB / 0x17CB / 0x17CD, pols 0x16D / 0x154), flush, 0x004067A0
        SoundStopAll();
        ts.Reset();
        resQueue.clear();
        residency = false;  // the mode's init loads its own models (Mdl_Draw residency is tracked in game mode 6)
        FrontResetBlink();
        memset(front.cheat, 0, sizeof(front.cheat));  // 0x009A34E0..0x009A34EF
        front.cheatLen = 0;
        front.cheatFlag = 0;
        // Task_InitRoot(0x00496060)
        NewTask<WTask>(WorldTramp<&Impl::TaskTitleCamera>, 0x004961A0);  // 0x00496180
        CameraReset();
        runner->camera.tick = 0;
        CreatePlayerTasks();  // 0x00414F20, 0x00414ED0, 0x00414FB0
        NewTask<WTask>(WorldTramp<&Impl::TaskSky>, fn::kSky);  // Sky_Create 0x004134D0
        skyMode = 1;
        skyType = 0;
        sky.Create(cfg.titleSkyYaw);
        NewTask<WTask>(WorldTramp<&Impl::TaskStageDraw>, fn::kStageDraw);  // 0x00401470: title tables 0x00576AEC / 0x00576B04
        section = 0;
        altModels = 0;
        sections.sections.clear();
        sections.objects.clear();
        {
            std::vector<int> list;
            for (int i = 0; i < 12; ++i) {
                const int v = exe.S16(0x00576AEC + static_cast<uint32_t>(i * 2));
                if (v < 0) break;
                list.push_back(v);
            }
            int maxObj = -1;
            for (int o : list) maxObj = std::max(maxObj, o);
            sections.sections.push_back(list);
            for (int o = 0; o <= maxObj; ++o)
                sections.objects.emplace_back(exe.S16(0x00576B04 + static_cast<uint32_t>(o * 4)), exe.S16(0x00576B06 + static_cast<uint32_t>(o * 4)));
            NoteSectionPolFiles();  // port: texture prefetch
        }
        NewTask<WTask>(WorldTramp<&Impl::TaskTitleLogo>, 0x004960A0);  // 0x00496090
        Sound(0x15A9);
        front.modeFrames = 0;
        ++subState;
        return;
    }
    if (subState != 1) return;
    ts.RunFrame();
    // the cheat: L (0x40 / 0x400000) / R (0x80 / 0x800000) presses against "LLRRLR" (0x005978C0) -> 0x009C87FC = 7
    if (front.cheatLen <= 14) {
        int c = 0;
        if ((pressed & 0x40) || (pressed & 0x400000)) c = 'L';
        else if ((pressed & 0x80) || (pressed & 0x800000)) c = 'R';
        if (c) {
            front.cheat[front.cheatLen++] = static_cast<uint8_t>(c);
            front.cheat[front.cheatLen] = 0;
        }
        if (exe.CStr(0x005978C0) == std::string(reinterpret_cast<const char*>(front.cheat))) front.cheatFlag = 7;
    }
    if (0x1DF < ++front.modeFrames) front.modeRequest = NextMode(exe, gameMode);  // thunk 0x0049DFD0, 0x0040E960
}

// 0x004961A0: camera path 0x1D at tick + 1, look angles, Cam_BuildViews
void World::Impl::TaskTitleCamera(Task&) {
    CameraDirector& c = runner->camera;
    ++c.tick;  // 0x009A6110
    CameraContext& x = c.ctx[0];
    float pos[3], tg[3];
    int roll = 0;
    if (cameraBank.Evaluate(0x1D, static_cast<float>(c.tick), pos, tg, &roll, c.rollEnable != 0)) {  // Cam_Evaluate 0x004041E0
        memcpy(x.pos, pos, 12);
        memcpy(x.target, tg, 12);
    }
    ContextLookAngles(x, x.target, roll);  // 0x00403AC0
    BuildViews();                          // 0x00401F40
}

// Title_DrawLogo 0x004960A0 (list priority 0xF via 0x004A7A00)
void World::Impl::TaskTitleLogo(Task&) {
    const auto v = TitleLogoSprites();
    hudSprites.insert(hudSprites.end(), v.begin(), v.end());
}

// =====================================================================================================================
// Game mode 4: mode select (0x00496200)
// =====================================================================================================================

void World::Impl::MenuFrame() {
    if (subState == 0) {
        // 0x0041D350, Mem_Init, 0x0049DFD0, model slots, pols 0x16D / 0x73, flush, 0x00413DA0 (dim counter 0)
        SoundStopAll();
        ts.Reset();
        resQueue.clear();
        residency = false;  // the mode's init loads its own models (Mdl_Draw residency is tracked in game mode 6)
        front.dimCounter = 0;
        // Task_InitRoot(0x004962C0): 0x00496890 (selector), 0x00496850 (views), players, 0x00496840 (screens)
        {
            MenuTask* m = NewTask<MenuTask>(WorldTramp<&Impl::TaskMenu>, 0x00496960);
            m->angle = static_cast<uint32_t>((front.menuCursor * -0x10000) / 6) & 0xFFFF;
            m->anim = 0;
            m->dir = 0;
            m->prev = front.menuCursor;
            // (0x005978FC / 0x00597918: per-entry scale / alpha table of the selected entry: not drawn by this version)
        }
        NewTask<WTask>(WorldTramp<&Impl::TaskMapViews>, 0x00482C40);  // 0x00496850: Cam_BuildViews task
        CameraReset();
        runner->camera.tick = 0;
        for (int k = 0; k < 3; ++k) runner->camera.ctx[0].pos[k] = runner->camera.ctx[0].target[k] = 0;
        CreatePlayerTasks();
        NewTask<WTask>(WorldTramp<&Impl::TaskScreens>, 0x00496840);
        FrontScreensFree();
        // background "tex\hod2title00..11" (z 0.5), then the 8 labels (3 tiles of 64 each, z 0): arcade, original,
        // training, boss, ranking, option, network, exit
        static const int kTile[6][4] = { { 0, 0, 0x100, 0x100 }, { 0x100, 0, 0x100, 0x100 }, { 0, 0x100, 0x100, 0x100 },
                                         { 0x100, 0x100, 0x100, 0x100 }, { 0x200, 0, 0x80, 0x100 }, { 0x200, 0x100, 0x80, 0x100 } };
        for (int i = 0; i < 6; ++i)
            FrontScreenAdd(0x00597B60 - 0x14 * static_cast<uint32_t>(i), kTile[i][0], kTile[i][1], kTile[i][2], kTile[i][3], 0.5f,
                           i < 4 ? 1.0f : 0.5f, 1.0f, 1.0f);
        for (auto& s : front.screens) s.visible = true;
        static const uint32_t kLabel[8] = { 0x00597AE8, 0x00597AAC, 0x00597A70, 0x00597A38, 0x00597A04, 0x005979C8, 0x0059798C, 0x00597954 };
        static const uint32_t kStride[8] = { 0x14, 0x14, 0x14, 0x10, 0x14, 0x14, 0x14, 0x10 };
        static const int kY[8] = { 0x20, 0x50, 0x90, 0xB0, 0xF0, 0x120, 0x150, 0x180 };
        for (int e = 0; e < 8; ++e)
            for (int k = 0; k < 3; ++k)
                FrontScreenAdd(kLabel[e] - kStride[e] * static_cast<uint32_t>(k), 0xE0 + 0x40 * k, kY[e], 0x40, 0x40, 0.0f, 1.0f, 1.0f, 1.0f);
        front.startPlayer = 0;  // 0x009A3790
        front.gameType = 0;     // 0x009CA08C
        front.sceneSelect = false;  // (port: the Scene Select page is left)
        front.sceneStart = -1;
        front.hordeStart = false;
        front.randomizerStart = false;
        Sound(0x15A9);
        front.modeFrames = 0;
        ++subState;
        return;
    }
    if (subState != 1) return;
    ts.RunFrame();
    // 0x00413CC0: after 18000 frames without input the screen dims (model 0x93E, alpha up to 0.6)
    if (held == 0 && pressed == 0) {  // HYPOTHESIS: 0x009C9020 / 0x009A37A0 = any button / key held
        if (18000 < ++front.dimCounter) {
            Note("mode-select dimming 0x00413CC0 (model 0x93E) not drawn");
            if (front.dimCounter > 0x468C) front.dimCounter = 0x468C;
        }
    } else {
        front.dimCounter = 0;
    }
    ++front.modeFrames;
}

// 0x00496960: cursor up / down (0x005978C8 / 0x005978D0 per player 0x009A3790; the keyboard masks with 0x009A37A8 are
// not driven), the selected entry's label visible, the logo strip, confirm (0x005978D8: trigger or start)
void World::Impl::TaskMenu(Task& t) {
    MenuTask& m = t.As<MenuTask>();
    const uint32_t p = static_cast<uint32_t>(front.startPlayer) * 4;
    // Port only (WorldConfig::portFeatures): a ninth entry "Scene Select" between Network and Exit (entry 7; Exit
    // becomes 8). It has no bitmap label (the background bitmap carries the original's eight): it is drawn as text in
    // the options screen's font (game/menu_text.h), grey like the unselected labels, white when selected.
    const bool portEntry = cfg.portFeatures;
    // Port only (WorldConfig::portModernMenu): the modern list. A gun / mouse player (WorldInput kind 1; the start
    // player's device first) moving or clicking over a row selects it, with sound only when the selection changes; the
    // keyboard / pad masks below keep working and the confirm masks are the original's. Port entries include Horde
    // Mode (8) and Randomizer (9); Exit is last.
    const bool modern = portEntry && cfg.portModernMenu;
    const int last = modern ? kModernEntries - 1 : portEntry ? 8 : 7;
    bool pointerMiss = false;
    if (modern) {
        int row = -1;
        const WorldInput* pointer = nullptr;
        for (int q = 0; q < 2; ++q) {
            const WorldInput& in = input[(front.startPlayer + q) & 1];
            if (in.kind != 1) continue;
            pointer = &in;
            row = ModernMenuRowAt(exe, in.x, in.y);
            break;
        }
        // Resting aim must not undo keyboard navigation. Moving within the same row or explicitly clicking it,
        // however, must select the label under the pointer even after the keyboard selected something else.
        const bool moved = pointer && m.pointerRow != -2 &&
                           (row != m.pointerRow || pointer->x != m.pointerX || pointer->y != m.pointerY);
        if (pointer && (moved || pointer->fired)) {
            if (row >= 0 && row != front.menuCursor) {
                m.prev = front.menuCursor;
                Sound(0x3E16A9);
                m.dir = row > front.menuCursor ? 1 : -1;
                front.menuCursor = row;
                m.anim = 8;
            }
        }
        pointerMiss = pointer && pointer->fired && row < 0;
        m.pointerRow = pointer ? row : -2;
        if (pointer) { m.pointerX = pointer->x; m.pointerY = pointer->y; }
    }
    if (pressed & exe.U32(0x005978C8 + p)) {
        m.prev = front.menuCursor;
        Sound(0x3E16A9);
        if (--front.menuCursor < 0) front.menuCursor = last;
        m.dir = -1;
        m.anim = 8;
    } else if (pressed & exe.U32(0x005978D0 + p)) {
        m.prev = front.menuCursor;
        Sound(0x3E16A9);
        if (last < ++front.menuCursor) front.menuCursor = 0;
        m.dir = 1;
        m.anim = 8;
    }
    for (size_t i = 6; i < 0x20 && i < front.screens.size(); ++i) front.screens[i].visible = false;  // 0x0049EB30(i, 0)
    const bool onPortEntry = portEntry && front.menuCursor == 7;
    const bool onHordeEntry = modern && front.menuCursor == 8;
    const int label = portEntry && front.menuCursor >= 8 ? 7 : front.menuCursor;  // the bitmap label group (exit = 7)
    const size_t k = static_cast<size_t>((label + 2) * 3);
    if (!onPortEntry && !modern)  // (modern: the highlight label tiles are never shown; the list below has the entries)
        for (size_t i = k; i < k + 3 && i < front.screens.size(); ++i) front.screens[i].visible = true;
    if (modern) {
        const size_t first = hudSprites.size();
        for (int e = 0; e < kModernEntries; ++e)  // grey (0x40: tint 0.6) unselected, white selected, like the original's labels
            MenuTextScaled(exe, kModernX, kModernY0 + kModernPitch * static_cast<float>(e), kModernEntry[e],
                           e == front.menuCursor ? 0u : 0x40u, kModernScale, hudSprites);
        // the bitmap screens are TL quads at z 0 (0x0049EAF0), nearer than any sprite depth: no depth test, like them
        for (size_t i = first; i < hudSprites.size(); ++i) hudSprites[i].flags |= 0x700;
    } else if (portEntry) {  // below Exit (its label tiles end at y 448), left of the copyright line: cells (13.5, 18) = (216, 432) px
        const size_t first = hudSprites.size();
        MenuText(exe, 13.5f, 18.0f, "Scene Select", onPortEntry ? 0u : 0x40u, hudSprites);
        for (size_t i = first; i < hudSprites.size(); ++i) hudSprites[i].flags |= 0x700;
    }
    // 0x004A79E0(1); Spr_DrawSimple(0xAB1, 1, 1, 100, 1, 1, 0, 0); 0x004A79E0(0)
    SpriteDef d;
    d.id = 0xAB1;
    d.x = 1.0f;
    d.y = 1.0f;
    d.depth = 100.0f;
    hudSprites.push_back(d);
    if (pointerMiss || (pressed & exe.U32(0x005978D8 + p)) == 0) return;
    const int e = front.menuCursor;
    if (e == last) {  // 0x0049E930: the game quits
        front.quit = true;
        return;
    }
    FrontScreensFree();  // 0x0049ED70, 0x00413CB0 (-> 0x0049DFD0)
    if (onPortEntry) {  // port: the Scene Select page (world_scene_select.cpp)
        SceneSelectMenuConfirm(*this);
        return;
    }
    if (onHordeEntry) {  // port: the Horde mode (world_horde.cpp)
        HordeMenuConfirm(*this);
        return;
    }
    if (modern && e == 9) {
        ModesMenuConfirm(*this, 1);
        front.randomizerStart = true;
        return;
    }
    if (e != 0) {  // entries 1..6: game types 1..3 / ranking / options / network (world_modes_menu.cpp)
        ModesMenuConfirm(*this, e);
        return;
    }
    // entry 0: 0x009A21C0 = 1, credits 0x00406F60(0x00496B70(0)) = setting + 1 (free play for -1), game type 0
    front.menuConfirmed = true;
    if (cfg.creditSetting == -1) {
        cfg.freePlay = true;  // 0x00407000
        cfg.credits = 0;
    } else {
        cfg.freePlay = false;  // 0x00407030(0), then every slot = setting + 1
        cfg.credits = cfg.creditSetting + 1;
    }
    front.gameType = 0;
    Sound(0x121A9);
    // the player's task (0x00414600, later in the list) sees the same trigger / start: Player_TryJoin -> Game_SetState(6)
}

// 0x00496840 -> 0x0049EAF0
void World::Impl::TaskScreens(Task&) { ScreensDraw(*this); }

// =====================================================================================================================
// Game mode 5: attract demo (0x00426800)
// =====================================================================================================================

void World::Impl::DemoFrame() {
    if (subState == 0) {
        // demo table 0x00589828 (20 bytes): {s16 event mode, s16 scene, s16 frames, s16 key offset, s16 bgm, s16 -,
        // s32 bgm id, char* key file}; a -1 event mode wraps to entry 0
        SoundStopAll();  // 0x0041D350 first
        if (exe.S16(0x00589828 + static_cast<uint32_t>(front.demoIndex * 0x14)) == -1) front.demoIndex = 0;
        const uint32_t e = 0x00589828 + static_cast<uint32_t>(front.demoIndex * 0x14);
        front.gameType = 0;
        WorldConfig c = cfg;
        c.gameType = 0;
        c.gameMode = 5;
        c.eventMode = exe.S16(e);
        c.scene = exe.S16(e + 2);
        c.demoIndex = front.demoIndex;
        c.startFrame = frame;
        c.seedCounter = seedCounter;
        c.firstSerial = ts.NextSerial();
        c.playerState[0] = c.playerState[1] = 9;
        c.carry.valid = false;
        std::string err;
        if (!LoadStageTable(c.eventMode, &err)) Note("attract demo: " + err);  // Evt_LoadTable 0x00413160
        front.demoActive = 1;                 // 0x007DC90C
        front.demoFrames = exe.S16(e + 4);    // 0x007DC910
        memset(komono40Intact, 1, sizeof(komono40Intact));  // 0x00463680
        // rank init 0x00460770 + 0x009C8A7C = 1 / 0x009A2C30 = 0, Stage_Start(.., 1): StartStage (mode 5)
        FrontStartStage(c);
        // 0x00426980: the demo's input playback from etc\<key file> (etc\ is not in the installed files; the demos'
        // shots come from the auto-gun 0x00415710 and the rand stream, World)
        FrontResetBlink();
        front.modeFrames = 0;
        subState = 1;
        return;
    }
    if (subState != 1) return;
    StageFrame();  // 0x004606D0
    if (++front.modeFrames == 2) {
        const uint32_t e = 0x00589828 + static_cast<uint32_t>(front.demoIndex * 0x14);
        Bgm(static_cast<uint32_t>(exe.S16(e + 8)), 0xFFFFFFFFu, exe.U32(e + 0xC), 0);  // 0x0041D450
    }
    if (gameMode == 5 && front.demoActive == 0) {
        ++front.demoIndex;
        front.modeRequest = NextMode(exe, gameMode);
    }
}

// 0x00426960: the demo timer (last stage task)
void World::Impl::TaskDemoTimer(Task&) {
    if (--front.demoFrames < 4) front.demoActive = 0;
}

// =====================================================================================================================
// Game mode 6 sub-state 0 (0x0045FEF0): the game start after the mode select
// =====================================================================================================================

void World::Impl::GameStartFrame() {
    // game type 0: event mode 0, scene 0 (types 2 / 3 are refused by the mode select); 0x0048A0D0, rank init, 0x00463680,
    // route record cleared, per-game counters: the mode-6 branch of StartStage
    WorldConfig c = cfg;
    c.gameMode = 6;
    c.eventMode = 0;
    c.scene = 0;
    c.gameType = front.gameType;  // 0x009CA08C: 2 training (stage 6 trnevtbl, scene 0x009A2BBC), 3 boss (scene 0x10)
    if (c.gameType == 2) {
        c.eventMode = 6;
        c.scene = modes ? modes->a2bbc : 0;
    } else if (c.gameType == 3) {
        c.scene = 0x10;
    }
    if (front.sceneStart >= 0) {  // port only: the Scene Select's chapter (an arcade game at a later stage's first scene)
        c.gameType = 0;
        c.eventMode = front.sceneStart;
        c.scene = SceneSelectFirstScene(front.sceneStart);
        front.sceneStart = -1;
    }
    const bool horde = front.hordeStart;  // port only: the Horde mode (an arcade game at stage 1's street, world_horde.cpp)
    if (horde) {
        c.gameType = 0;
        c.eventMode = 0;
        c.scene = 0;
        front.hordeStart = false;
    }
    c.startFrame = frame;
    c.seedCounter = seedCounter;
    c.firstSerial = ts.NextSerial();
    for (int p = 0; p < 2; ++p) c.playerState[p] = player[p].state;
    c.carry.valid = false;
    std::string err;
    if (!LoadStageTable(c.eventMode, &err)) Note("game start: " + err);
    ItemsInit(*this);  // 0x0048A0D0: the players' items (world_modes_orig.cpp)
    RandomizerBegin(*this, front.randomizerStart && c.gameType == 1);
    front.randomizerStart = false;
    FrontStartStage(c);
    if (horde) HordeGameStart(*this);
}

// =====================================================================================================================
// Game modes 10 / 11: story sequences (0x0041F9B0 / 0x0041FB00)
// =====================================================================================================================

void World::Impl::AdvFrame() {
    if (subState == 0) {
        // 0x009CA08C = 0, event mode 10 / 11 (advevtbl / adv2evtbl) + Evt_LoadTable, scene 0, sounds, Mem_Init, model
        // slots, 0x0049DFD0, pol 0x26, texture 0x17B, stage requests, 0x00412FD0, flush, 0x00463680, Task_InitRoot
        // (0x0041FAB0: the stage tasks without 0x004809D0 and the demo timer; no Stage_Start, so no srand here),
        // 0x004067A0, Stage_ResetState; then the tasks run in this frame
        SoundStopAll();  // 0x0041D350
        front.gameType = 0;
        WorldConfig c = cfg;
        c.gameType = 0;
        c.gameMode = gameMode;
        c.eventMode = gameMode;
        c.scene = 0;
        c.startFrame = frame;
        c.seedCounter = seedCounter;
        c.firstSerial = ts.NextSerial();
        c.playerState[0] = c.playerState[1] = 9;
        c.carry.valid = false;
        std::string err;
        if (!LoadStageTable(c.eventMode, &err)) Note("story sequence: " + err);
        memset(komono40Intact, 1, sizeof(komono40Intact));
        const int keepRank = rank;  // 0x00460770 is not called
        FrontStartStage(c);
        rank = keepRank;
        FrontResetBlink();
        front.modeFrames = 0;
        subState = 1;
    } else if (subState != 1) {
        return;
    }
    ts.RunFrame();  // Task_RunFrom 0x004A7240 (no rank update: not 0x004606D0)
    ++front.modeFrames;
    auto it = runner->globals.find(0x009C71C0);  // Stage_End 0x0045ED90 at the end of the mode's scene list
    if (it != runner->globals.end() && it->second != 0) front.modeRequest = NextMode(exe, gameMode);
}

}  // namespace game
