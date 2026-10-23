#pragma once

// game::World - the native world runtime: one deterministic per-frame simulation of a stage, built from the ported
// subsystems (task system, CRT rand stream, event VM + camera director, props, enemies, demo auto-gun + hit system,
// rain / sky / billboard effects). See notes/agents/world.md for the stage start, the frame order, what is ported and
// how it is verified (tests/world_test.cpp, frame by frame against a capture of the original).
//
// Frame model (Game_Frame 0x0049E220 -> Game_Update 0x0040E730 -> mode 5 0x00426800 -> 0x004606D0):
//   - StartStage() is the frame in which the mode's init calls Stage_Start 0x00460030: srand, Mem_Init, the stage
//     tasks are created by Task_InitRoot(0x00460710) (no task runs in that frame), rail reset, Stage_ResetState.
//   - Step() is one later frame: the resource queue is stepped, then every task runs once in creation order
//     (Task_RunFrom 0x004A7240), then Game_UpdateRank 0x004607B0 and the per-player timers, then Task_FreeDead.
//
// Renderer interface: after StartStage()/Step(), Draws() lists every model the original submits in that frame, in
// submission order, with its WORLD matrix (the engine draws world * view; Camera().view is that view), the pass-1
// draw mode in effect (0x004A79F0) and the draw path (alpha / alt model). Stage section objects are listed with
// cull = true (the original frustum-culls them per object, Stage_DrawSection 0x00401260; the renderer does that).
// Hud() carries the 2D-relevant state (letterbox, aim, shots); sprites themselves are drawn by the port's sprite code.
//
// Game mode 6 (a game started from the title, notes/agents/gameplay.md): StartStage() with cfg.gameMode = 6 is mode 6
// sub-state 0 (0x0045FEF0 -> Stage_Start); SetInput() feeds the players' guns before each Step(); the players join from
// cfg.playerState (0 = start pressed); Step() runs sub-states 2 (play), 3 / 4 (continue countdown) and stops at the
// switch to game mode 7 (game over), or with cfg.gameOver runs the game-over screen to the title (mode 3). Two players,
// joining / continuing / pausing with start and the game over: notes/agents/twoplayer.md.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/anim.h"          // Mat4
#include "game/menu_scroll.h"
#include "game/stage_results.h"
#include "game/event_runner.h"  // EventRunner::Exec
#include "game/sprite.h"        // SpriteDef

namespace game {

// The light a submission is drawn with (renderer state, no game logic): Mdl_Submit 0x004A7E50 snapshots the light
// globals 0x007E7998.. (colour, intensity, direction; set by 0x004AA0A0 / 0x004AA070 / 0x004AA0E0) and builds the D3D
// light from them (0x004AA120); alternative-model submits (key bit 0x04000000, 0x004AA500) use the light table instead
// (0x004AA8B0, World::Lights().table).
struct WorldLight {
    bool table = false;       // light table 0x004AA8B0 (alt draws) instead of the single directional light
    float color[3] = { 1.0f, 1.0f, 1.0f };  // 0x007E7998.. (0x004AA0A0)
    float intensity = 0.7f;                 // 0x007E79A4 (0x004AA070)
    // direction toward the light = (0, 0, 1) * Rx(pitch) * Ry(yaw) in world space (0x0040E0B0 then 0x004AA0E0 with
    // the view of the camera context; the renderer works in world space)
    int pitch = 0, yaw = 0;
};

// Edits the original makes to a model's data in memory before drawing it (the global model, so they persist until
// the next edit): the bat / leech skeleton draw 0x0040F960 moves the seam vertices of each body segment onto the
// neighbouring segment and sets the diffuse colour of every material. Applied by the renderer to a copy of the model.
struct ModelEdit {
    struct Vertex {
        int index = 0;      // k-th inline 32-byte vertex record of the model (file order; references follow it)
        float pos[3] = {};  // new model-space position (x has its inline flag bit 0 set, as the original writes it)
    };
    std::vector<Vertex> vertices;
    float diffuse = -1.0f;  // >= 0: material words 12..14 (diffuse R, G, B) of every material = this value
    // 0x0040F960 seam-normal cache: normal x of inline vertex `index` = the bits of normal z of inline vertex
    // `srcIndex` of model `srcModel` (as loaded from the file), | 1 (the code writes all three cached components to x)
    struct NormalX {
        int index = 0;
        int srcModel = 0;
        int srcIndex = 0;
    };
    std::vector<NormalX> normalX;
    // 0x004759C0(angle) (book page 0x7EE of the boss name cards): z of every inline vertex (flag bit 0 of x set) =
    // 4.8e-5 - sin(angle) * 4 * sin(ftol(x * 2058.112)) (angles in 1/65536 turns); INT_MIN: none
    int pageBend = INT32_MIN;
    // uv of inline vertex `index` (the stored words: v carries the flag bit 0 like the original writes it); the water
    // surfaces of komono kind 1 (0x0046E3A0) move them every frame
    struct Uv {
        int index = 0;
        float uv[2] = {};
    };
    std::vector<Uv> uvs;
    uint32_t tspOr = 0;  // OR-ed into every material's TSP word (0x0046E3A0: 0x2000, bilinear filtering)
};

// One model submission.
struct WorldDraw {
    int model = 0;            // global model id (0x00418560 / 0x004185A0 / 0x004185E0 argument)
    Mat4 world;               // world matrix (row vectors); model-view = world * Camera().view
    bool viewSpace = false;   // true: `world` is already the model-view matrix (letterbox bars, hit sparks)
    uint32_t task = 0;        // serial of the task that submitted it (the capture's "^serial")
    uint32_t fn = 0;          // original task function of that task
    uint8_t drawMode = 8;     // pass-1 mode 0x004A79F0 in effect: 8 normal, 7 stage priority, 0xD shadow, 0xE effect
    float alpha = -1.0f;      // >= 0: translucent path 0x004185A0 with this factor (rain 0.5, fading enemies)
    bool alt = false;         // 0x004185E0 (Mdl_SelectAlt) instead of 0x00418560
    bool cull = false;        // stage section object: frustum-culled by the renderer (0x00401260 culls per object)
    int node = -1;            // skeleton node index for skeleton draws (enemies), else -1
    // Skinned enemy attachment (0x00419520 / 0x00419980): the model's vertex groups are moved by these node world
    // matrices relative to `world` (the base node) before drawing; the renderer applies the deformation.
    int skinCount = 0;
    Mat4 skin[4];
    // >= 0: models 0x1731..0x1734 carry the vertex wave of the decoration 0x0046DAB0 with this phase (game::DecorWave;
    // the original edits the loaded model, so every draw of the frame shows the frame's last edit)
    int wavePhase = -1;
    // >= 0: the sky panel uv scroll 0x00413560 (types 8 / 10 / 11) has run this many times on the loaded model: every
    // inline vertex v -= 0.005 per run (+3 on the run after one reached <= -2.995), stored with bit 0 set
    int uvScroll = -1;
    WorldLight light;         // the light in effect at the submission
    // 0x00418660 -> 0x004AA400 ran right before: the uv of the inline vertices of the strips with ctrl 0x100 were
    // rewritten in memory from the model-view normals (sphere map: u = (1 - nx) / 2, v = (ny + 1) / 2)
    bool envMap = false;
    // Rendering correction for scaled fish/leech bodies: keep specular highlight width
    // independent of the inverse-transpose normal's length (legacy PC artifact).
    bool unitSpecular = false;
    // Tall car-smoke sheets must blend far to near without moving unrelated world/HUD draws.
    bool sortBackToFront = false;
    // The model's strip control words have bit 0 flipped in memory (cull mode CW <-> CCW): a model of list 0x0057A280
    // (the sky domes 6048 / 6049 / 6307 / 6309) loaded on its own (op 0x50: Res_LoadModel 0x00418B68 -> 0x00419270;
    // a pol-file load does not flip). Where the world does not track the slots, a listed model counts as flipped.
    bool cullFlip = false;
    std::shared_ptr<const ModelEdit> edit;  // the model's in-memory edits at submission time (nullptr: none)
};

class ExeImage;
struct PrototypeStatus;
// Model ids of list 0x0057A280 (s16, -1 ends): Res_LoadModel's single load flips their cull bits (0x00419270) and
// turns their fog off (0x00419300, TSP | 0x800000; the four listed skies have it off in the file already).
bool CullFlipModel(const ExeImage& exe, int model);

// Frame-level render state of the world (no game logic): the environment (fog, scene light) and the light table.
struct WorldLights {
    bool valid = false;       // the world filled it (after StartStage / Step / Boot)
    bool fogOn = false;       // fog applies (materials with FOGENABLE)
    int fogColor[3] = { 0, 0, 0 };          // env block 0 +0x24.. (FOGCOLOR, 0x0040D5B0)
    float fogStart = 0, fogEnd = 0;         // FOGSTART / FOGEND (0x004ABDF0 on env +0x30 / +0x34)
    // Light table (0x004AA8B0, filled every frame by the task 0x00480970 when 0x009A2BB4 != 0): AMBIENT from
    // 0x009C71D4.. (0x004AA720; 0x0057A28C = 0.5 when the task does not set it) and the enabled slots of
    // 0x009A1A20 (D3DLIGHT7, world space; the engine moves them to view space with the frame's view, 0x004AA780).
    float tableAmbient[3] = { 0.5f, 0.5f, 0.5f };
    struct Light {
        int type = 1;                    // D3DLIGHTTYPE: 1 point, 2 spot, 3 directional
        float diffuse[3] = { 1, 1, 1 };
        float pos[3] = {}, dir[3] = { 0, 0, 1 };
        float range = 0, falloff = 1, att[3] = { 1, 0, 0 }, theta = 0, phi = 0;
    };
    std::vector<Light> table;
};

// The active camera context (0x009A6000 + 0x009C6F00 * 0x1A4) after the camera task.
struct WorldCamera {
    int ctx = 0;                    // 0x009C6F00
    float pos[3] = {};              // +0xC0
    int pitch = 0, yaw = 0, roll = 0;  // +0xCC / +0xD0 / +0xD4
    Mat4 view = Mat4::Identity();   // +0x00
    Mat4 inverse = Mat4::Identity();  // +0x40 (camera matrix)
    float playerPos[3] = {};        // 0x009C71E0 players' position (rail step / gameplay camera start; two players
                                    // in camera sub-mode 1/1: the midpoint of the two bodies, 0x0040C9C0)
    int playerPitch = 0, playerYaw = 0, playerRoll = 0;  // 0x009C71EC / 0x009C71F0 / 0x009C71F4
};

// One event-op handler call (the capture's E line).
struct WorldEventOp {
    int frame;
    uint32_t opcode, addr;
};

// Per-frame player state for the HUD (the gameplay HUD sprites are drawn by the port's HUD code from these values;
// the original draws them inside the player task 0x00413E90: crosshair 0x004169C0, ammo 0x004177D0, lives
// 0x004174A0, continue / game-over prompts 0x00414280 / 0x00460530). Filled at the end of StartStage() / Step().
// Port-only local Horde results. Kept apart from the original twelve ranking tables.
struct HordeLeaderboardEntry {
    int rounds = 0, score = 0, players = 1, difficulty = 2;
};
struct HordeRunSnapshot {
    bool active = false, debug = false;
    int rounds = 0, score = 0, players = 0, difficulty = 2;
};
struct SharedLeaderboardEntry {
    std::string name;
    int score = 0, rounds = 0, difficulty = 2, players = 1;
};
struct LeaderboardRunSnapshot {
    uint64_t sequence = 0;
    int table = -1, score = 0, rounds = 0, difficulty = 0, players = 1;
    int playerScores[2] = {};
    bool eligible = false;
};
struct PortHomeRoom {
    uint32_t id = 0;
    bool privateRoom = false, unlimited = true;
    uint32_t build = 0;
    int difficulty = -1;  // 0..4; older directory advertisements do not supply a difficulty
};

struct WorldHud {
    // Native Night Shift front end (640 x 480 logical coordinates). No live service data here.
    struct MenuPanel { float left, top, right, bottom; uint32_t color; };
    std::vector<MenuPanel> menuPanels;
    struct MenuText { float x, y; std::string text; float scale; uint32_t color; };
    std::vector<MenuText> menuText;
    std::vector<MenuScrollHint> menuScrollHints;
    int nightShiftPage = -1;  // -1 off; 0 Home, 1 Play, 2 Practice, 3 Leaderboard, 4 Publish online name, 5 Options, 6 Online, 7 Game Options, 8 Pause, 9 Extras
    int nightShiftRow = 0;
    int portLobbyRequests = 0, portLobbyAction = 0;  // existing network pages: 1 Create, 2 Browse, 3 Peer
    uint32_t portLobbyRoom = 0;  // action 4: a real waiting room selected on Home
    int portLeaderboardRequests = 0, portLeaderboardTable = 0;
    bool portLeaderboardShared = false;
    int portLeaderboardPublishRequests = 0;
    std::string portLeaderboardInitials;  // online display name, 1..12 characters; native save initials stay separate
    int letterbox = 0;        // 0x009CA0F4 after the letterbox task (2 open; 1 opening; 4 bars)
    bool canShoot = false;    // 0x009C8E00 (letterbox open: shots count, ammo / reload HUD shown)
    int letterboxCounter = 0; // letterbox task +0x50 (0..40 while opening / closing)
    int gameMode = 5;         // 0x009C8E98: 5 attract demo, 6 game, 7 game over (World stops there)
    int subState = 0;         // 0x009C90A4 (mode 6: 2 playing, 3/4 continue countdown of the whole game)
    int continueTimer = 0;    // 0x009A2BB8 (mode 6 sub-state 4): 0x9FFF -> 0 by 0x2D per frame; digit = value >> 12
    int continueShown = -1;   // the CONTINUE screen task 0x00460530 ran this frame: 0x009A2BB8 as it drew it (before the
                              // -0x2D; hud.h ContinueScreen); -1: not drawn (0x009A2BB8 keeps its value after a continue)
    int rank = 0;             // 0x009C8E96 (0..15)
    int shake = 0;            // 0x009CA0E4 vertical view offset after a hit (Player_HitShake 0x00415270)
    bool skippable = false;   // 0x009A2D7C: the running cutscene can be skipped (reload / off-screen shot)
    bool skipping = false;    // 0x009A2D74
    uint32_t frameCounter = 0;  // 0x009A32A0 (the HUD animates the life torches with it: 0x004174A0)
    uint32_t flags5900 = 3;     // 0x009A5900 (bit 0: no cutscene, players join at once; bit 1: credit lines / join)
    // ---- two players / continue / game over (notes/agents/twoplayer.md) ----
    bool paused = false;      // 0x009A1A00: start pressed by a playing player (0x0040E500); no task ran this frame.
                              // The original draws a dark overlay (model 0x93E, alpha 0.5) and "PAUSE" 0x8E7 / 0x8E8
    int pausePlayer = 0;      // 0x007C1792: who paused (sprite 0x8E7 at x 160 for P1, 0x8E8 at x 480 for P2, y 240)
    int portVolumes[5] = { -1, -1, -1, -1, -1 };  // port only: Music, Guns, Effects, Master, Attraction; -1 = none
    int netState = 0;         // game mode 15 (the network lobby, world_modes_net.cpp): its state (1 menu, 3 create, 4 join ...)
    StageResults stageResults;
    uint32_t randomizerSeed = 0, randomizerHash = 0;  // port-only lockstep/recovery checks
    bool prototype = false;
    int portLightgunRequests = 0; // opens the local light-gun setup page
    int portDisplayPage = -1;  // Night Shift: 0 Display, 1 Image, 2 Lighting, 3 System, 4 Packs
    int portWiiRequests = 0; // opens the local native Wii Remote setup page
    int portModelViewerRequests = 0; // Extras opens the local model gallery
    int portDisplayRequests = 0;  // port only: the options screen's "Display Settings" row confirmed that many times (play.exe
                                  // shows its display page over the screen while the row count grows)
    int credits = 0;          // 0x009C8E60 (shared credits; a join / continue without free play takes coinsStart /
                              // coinsContinue, 0x00406E10)
    int gameOverTimer = 0;    // 0x009CA0F8 in game mode 7 (counts down; sub-state 1: 200 frames, sub-state 3: 180)
    // 2D sprites the world's own tasks submit this frame (game mode 7: the "GAME OVER" letters 0x43A..0x43D of the
    // sprite-effect tasks 0x00499CE0, drawn by 0x00499F00 = hud.h SpriteFx::Def), in submission order. x / y: screen
    // pixels (640x480) as the original passes them.
    struct Effect {
        int sprite = 0;
        float x = 0, y = 0;
        float sx = 1.0f, sy = 1.0f;
        float alpha = 1.0f;
    };
    std::vector<Effect> effects;
    // Other 2D sprites world tasks submitted this frame (Spr_DrawSimple 0x0041C6D0 / Spr_Draw 0x0041C630 / the retained
    // list 0x0041C760, flags | 0x700), in submission order: boss health bar, boss name card, stage-clear screen.
    std::vector<SpriteDef> sprites;
    // Texture-file states 0x009C7328 (index = file, 4 = loaded) where the world tracks them (game modes 5 / 6 / 10 / 11);
    // empty elsewhere (every file counts as loaded). Spr_Draw 0x0041C630 skips a sprite whose file is not loaded.
    std::vector<uint8_t> texFiles;
    // Pol files (index into 0x004D0EF4) whose load the resource queue processed this frame: a whole file (op 0x52) or
    // one of its models (op 0x50, Res_LoadModel). The model textures live in the texture file of the same name; the port
    // decodes an asset pack's versions of them ahead of the first draw (play.cpp, AssetStore::PrefetchPolFile).
    std::vector<int> polFileLoads;
    // Subtitles of the live voice-line tasks 0x00435AA0 after this frame's update, in task order: {group (voice line
    // 0x0058B6B8), line (+0x38)}; the HUD draws them (game::SubtitleSprites).
    struct SubtitleLine {
        int group = 0, line = 0;
    };
    std::vector<SubtitleLine> subtitles;
    // The stage title card task (actor 0x60, 0x004342E0) ran this frame: its serial (a new serial starts a new card)
    // and the chapter (0x009A1A08); the HUD draws it (game::ChapterCard, 0x00436AD0). titleSerial 0: none.
    uint32_t titleSerial = 0;
    int titleChapter = 0;
    // >= 0: the route map screen (game mode 7 sub-state 5, task 0x00461180) ran this frame with this scroll 0x007DCCD8;
    // its tiles are hud.h RouteMapBackground(routeMapScroll) (drawn before the task's 3D agent / footprints)
    int routeMapScroll = -1;
    // ---- front end (World::Boot; notes/agents/frontend.md) ----
    int modeFrames = 0;       // 0x009A5C44: frames since the game mode's init (PRESS START needs > 3)
    uint32_t bootFrames = 0;  // 0x009A5C40: frames since boot (Game_UpdateInput); the PRESS START blink counter
                              // 0x005A4D44 follows it (0x004067D0) except while paused
    int demoIndex = 0;        // 0x009A3400: next attract demo (table 0x00589828)
    int menuCursor = 0;       // 0x009A2226: mode-select entry (0 arcade, 1 original, 2 training, 3 boss, 4 ranking,
                              // 5 option, 6 network, 7 exit; with WorldConfig::portFeatures 7 scene select, 8 exit)
    int gameType = 0;         // 0x009CA08C
    // 2D bitmap screens (0x0049E950 slots 0x007DDAA8 + i * 0x28; drawn by 0x0049EAF0 / 0x0049EB50 as one textured
    // TL quad each): the PC-only boot logos, the mode-select background and labels. file = tex\ name (a plain BMP, not in
    // the texture tables); x, y, w, h in 640x480 pixels; z = the quad's depth; u / v = the texture coordinates of the
    // bottom-right corner (0x80-wide tiles use half of a 256-wide bitmap); alpha = vertex alpha (fades). Only the
    // visible slots, in slot order. srcX >= 0: a GDI BitBlt copy (the network lobby 0x0049F1E0) of the w x h pixels at
    // (srcX, srcY) of the bitmap, unscaled (u / v unused).
    struct Screen {
        std::string file;
        float x = 0, y = 0, w = 0, h = 0, z = 0, u = 1, v = 1, alpha = 1;
        int srcX = -1, srcY = 0;
    };
    std::vector<Screen> screens;
    struct Player {
        float aim[2] = {};    // player +0x10 / +0x14 (centred gun space, x right, y up)
        bool fired = false;   // a shot left the gun this frame (pending 0x009A5C78 set)
        int shots = 0;        // shots fired (+0x24)
        int state = 9;        // +0x02: 0 joining, 5 playing, 4 continue countdown, 6 out, 9 not joined
        int lives = 0;        // +0x06 (torches)
        int livesShown = 0;   // +0x08 (copied from +0x06 after the HUD draw)
        int score = 0;        // +0x0C
        int ammo = 0;         // +0x1C (6 = full)
        bool empty = false;   // +0x1E (out of ammo: "RELOAD" prompt)
        int reloadBlink = 0;  // +0x20 (frames since the gun ran empty; prompt blinks while value % 60 < 45)
        int hits = 0;         // +0x26
        int continueTimer = 0;  // +0x68 (state 4: 0x9FFF -> 0 by 0x2D per frame; digit = value >> 12)
        int continueShown = -1; // per-player CONTINUE drawn this frame (0x00414280 -> 0x00416880 / 0x004168C0; not
                                // while the whole-game CONTINUE screen is up): +0x68 as drawn (hud.h ContinuePlayer); -1 none
        int hitTimer = 0;     // 0x009C8E08[p]: invulnerability after a hit / at the start (90 frames)
        int inputKind = -1;   // +0x125: 1 gun, 0 pad / keyboard cursor, -1 none
        bool crosshair = false;  // 0x004169C0 draws the crosshair this frame (mode 6, lives, can shoot)
        float crosshairPos[2] = {};  // +0xE8 / +0xEC: crosshair in 640x480 screen pixels
        int16_t gunX = 0, gunY = 0;  // gun record +4 / +6 (calibrated, centred)
        bool gunValid = false;  // gun record +0x10
    } player[2];
};

// Per-player device input of one frame, as the hook / port input layer writes it for a gun slot (Input_Poll mode 5/6
// writes, then Input_LatchShots 0x0041EB30 / Game_UpdateShots 0x0040CE70 / Gun_UpdateRecords 0x0040CBB0 run inside
// the world). Players without a gun device keep kind 0 (pad / keyboard cursor, not driven) or -1 (no device).
struct WorldInput {
    int kind = 0;             // player +0x125: 1 gun, 0 pad / keyboard, -1 none
    int deviceMode = 6;       // 0x00588E24 / 0x007DC698: 6 = fixed calibration (Hod2.ini default), else per-player
    int x = 0, y = 0;         // gun aim in 640x480 game pixels (0x007DBD50 + slot * 8)
    bool fired = false;       // 0x007DC120[slot]: trigger pressed on screen this frame
    bool offscreen = false;   // 0x007DB7C8[slot]: off-screen shot (trigger off screen or reload button)
    uint32_t held = 0, pressed = 0;  // device buttons (2 reload, 4 trigger, 8 start): device record +8 / +0x10
};
// The hook's scripted / raw-input conversion (src/hook/gun_state.cpp ComputeGunSlot, stretch aspect): a normalized aim
// (0..1, origin top-left; outside = off screen) and the held buttons (1 trigger, 2 reload, 4 start; scripted d-pad 8 up,
// 16 down, 32 left, 64 right = pad bits 0x10 / 0x20 / 0x40 / 0x80) of this frame and the previous one.
WorldInput MakeWorldInput(float nx, float ny, uint8_t buttons, uint8_t prevButtons);

// State a stage inherits from the previous one when it starts in the middle of a game (game mode 6 sub-state 1,
// 0x0045FFF0 -> Stage_Start 0x00460030): the globals Stage_Start / Stage_ResetState do not reset. Filled by a caller
// that starts the world at a later stage (world_test --stage-start reads it from the capture's last frame before the
// stage start); notes/agents/stage2.md lists what is carried and how it was verified. Applied by world_st2.cpp.
struct WorldCarry {
    bool valid = false;
    struct Player {           // player struct 0x009A5C60 / 0x009A5D90 before the stage start
        int state = 9;        // +0x02 (0x0045FFF0: 5 -> 2, the task starts at row 2 = 0x00413E40)
        int lives = 0, livesShown = 0;  // +0x06 / +0x08
        int score = 0;        // +0x0C
        int credit[4] = {};   // +0x2C..+0x38 credit snapshot
        int nextState = 0;    // +0x48
        uint32_t flags12C = 0;  // +0x12C
        // kept through the Stage_Start frame (the row-2 init 0x00414770 resets them one frame later)
        float aim[2] = {};    // +0x10 / +0x14
        int ammo = 6, empty = 0, reloadBlink = 0;  // +0x1C / +0x1E / +0x20
        int outTimer = 0;     // +0x28
        int contTimer = 0;    // +0x68
        float crosshair[2] = {};  // +0xE8 / +0xEC
        int hitTimer = -1;    // 0x009C8E08[p] (-1: not known)
    } player[2];
    int rank = 0;             // 0x009C8E96 (rank init 0x00460770 only runs in sub-state 0)
    int rankTimer = 1;        // 0x009C8A7C
    bool rankTimerOn = true;  // 0x009A2C30
    int players = 0;          // 0x009C8E80 (player rows 0 / 3 count it; row 2 does not)
    int rankPrevPlayers = 0;  // 0x009C8E82
    int route = 0;            // 0x009C8E84
    bool haveFlags5900 = false;
    uint32_t flags5900 = 3;   // 0x009A5900
    bool haveRouteRec = false;
    uint8_t routeRec[10][16] = {};  // 0x009A5920 (scenes played per stage; only sub-state 0 clears it)
    bool haveTracers = false;
    int tracerRoll[2][6] = {};      // 0x009A2460 + (p * 6 + i) * 0x30 + 0x18: tracer roll (never reset; +0x1000 per frame)
    bool haveKomono40 = false;
    uint8_t komono40[12] = {};      // 0x007DCDB8: komono-40 variant-1 pieces not yet broken (0x00463680 at the game start)
    // civilians (stage 6: actor 0x64 compares the rescues with the civilians met; the ending lists the rescues)
    bool haveCivilians = false;
    int rescueTotal = 0, civTotal = 0;  // 0x009CA0EC / 0x009A21BA
    int16_t rescueCount[10] = {};      // 0x009C89C0
    int16_t rescueKinds[10][10] = {};  // 0x009C8EC0
    int16_t civCount[10] = {};         // 0x009C9100
};

struct WorldConfig {
    int eventMode = 0;        // 0x009A1A08: stage table (0 = st1evtbl.bin)
    int scene = 4;            // 0x009A2BC0
    int gameMode = 5;         // 0x009C8E98 (5 = attract demo, 6 = game started from the title)
    int demoIndex = 0;        // 0x009A3400
    int difficulty = 2;       // 0x009C8E94 (the demo forces index 2 where it matters)
    uint32_t firstSerial = 0x10000;  // task serial counter 0x00598AA8 when the stage starts (history-dependent)
    int startFrame = 0;       // frame number of the StartStage frame (labels only)
    int resourceStepsPerFrame = 10;  // Game_Frame 0x0049E220 steps the resource queue 5 or 10 times per frame
    // ---- game mode 6 ----
    uint32_t seedCounter = 0; // 0x009A32A0 during the StartStage frame (Game_SeedRandom 0x004157D0 seeds with it;
                              // it counts game frames since boot, so it is history-dependent like the serial)
    int playerState[2] = { 9, 9 };  // 0x009A5C62 / 0x009A5D92 before the start (0 = start pressed, 9 = not joined)
    int livesSetting = 3;     // 0x009A34C4 (Hod2.ini lives)
    bool freePlay = true;     // 0x009C8E70 == 1 (continues need no credit)
    int credits = 0;          // 0x009C8E60
    int coinsStart = 1, coinsContinue = 1;  // 0x009C8E78 / 0x009C8E7C
    // Per-player gun calibration (gun record +0x16: x {p0, p1, p2, offset}, +0x1E: y {...}), used for a gun whose
    // device mode (WorldInput::deviceMode) is not 6. Default = the values the 2p captures show ({0, 320, 640} /
    // {0, 240, 480}: raw pixels map linearly to the centred range).
    int16_t gunCal[2][8] = { { 0, 320, 640, 0, 0, 240, 480, 0 }, { 0, 320, 640, 0, 0, 240, 480, 0 } };
    // false (default, the old behaviour): Step() stops at the switch to game mode 7. true: Step() also runs the
    // game-over screen (mode 7, 0x00460960: "GAME OVER", the map of the route played) until the switch to the title
    // (GameMode() == 3); Step() does nothing after that.
    bool gameOver = false;
    // valid: a later stage of a game (mode 6 sub-state 1) instead of the game start (sub-state 0)
    WorldCarry carry;
    // ---- front end (World::Boot; notes/agents/frontend.md) ----
    // 0x009C9F25: the credits the mode select gives a game (0x00496B70 -> 0x00406F60: setting + 1 credits; -1 = free
    // play, 0x00407000). Hod2.ini; play.exe uses free play. The 2p captures fit 5 (6 credits, one taken by the start).
    int creditSetting = -1;
    // Sky_Create 0x004134D0 leaves the sky task's yaw (+0x48) uninitialised; the title starts from leftover memory. The
    // first title of a boot: 180687044 (+ 12 per frame from its first task frame) reproduces the captured title frames
    // (HYPOTHESIS, scenes/title_cam29.scene); later titles reuse it (their leftover differs: not modelled).
    int titleSkyYaw = 180687044;
    // The same leftover in story mode 10 (root 0x0041FAB0), whose sky stays type 0 for its first 200 frames: both
    // story-mode-10 runs of the attract capture fit 683979520 (0x28C4B300; any value with the same float-rounded angles
    // fits as well: the angle 0x004A9AE0 rounds to 2^-7 rad there, one step every 6..7 frames)
    int storySkyYaw = 683979520;
    // ---- game types / menu modes (notes/agents/modes.md) ----
    int gameType = 0;              // 0x009CA08C for StartStage (0 arcade, 1 original, 2 training, 3 boss); the front end sets it
    std::string saveDir;           // directory with the original's save files (read only; "" = the game directory)
    std::string savePath;          // the port's own save file ("" = nothing is written; it is read first when present)
    bool applySaveSettings = false;  // Boot: lives / difficulty / credits from the save (0x0040AB50), as the original
    // Test setup: bytes written into the loaded save block (original addresses 0x009C9120..) before Boot copies it out;
    // the counterpart of the capture hook's HOTD2_POKE (e.g. training grades 0x009C9F29)
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> savePatch;
    // Port only (play.exe): the options screen (game mode 12) gets Music / Gun Shots / Sound Effects / Master / Attraction
    // 0..10, starting from these values; -1 = no such rows (the original's screen, as the exactness tests expect).
    int portVolumes[5] = { -1, -1, -1, 5, 5 };
    // Port only (play.exe): behaviour the original lacks, off in the exactness tests: a back button (the selects' back
    // mask 0x00589170: reload / Backspace) on the original-mode item select, back to the mode select; the mode select's
    // "Scene Select" entry (before Exit) and its page, an arcade game started at a chosen chapter
    // (world_scene_select.cpp); the boot logos (SEGA, Empire) each skipped by a trigger / reload / start press
    // (world_front.cpp BootFrame).
    bool portFeatures = false;
    // Port only, with portFeatures: Night Shift Home / Play / Practice / Leaderboard,
    // using the original logo and glyphs with full-row mouse/gun and keyboard input.
    // play.exe renders a separate original attract World behind this menu, with its own volume.
    // false preserves the original bitmap menu and exactness-test behavior.
    bool portModernMenu = false;
    // A connected online room: host-owned mode selection, retained between games and during rejoin replay.
    bool portNetworkRoom = false;
    bool portStageResults = false;  // chapter totals and a skippable results page; off in original-exactness tests
};

// Save-backed gameplay controls exposed to the port's unified Options tabs. Lives are the visible 1..5 value;
// credits == 0 means the existing unlock-gated Free Play choice. Availability is output-only metadata.
struct PortGameOptions {
    int difficulty = 2;
    int lives = 3;
    int credits = 5;
    int crosshair[2] = {0, 0};
    bool freePlayUnlocked = false;
    bool editable = false;
};

class World {
public:
    World();
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Loads Hod2.exe tables, comevtbl.bin + the stage event table, camera files (game directory layout).
    bool Load(const std::string& gameDir, int eventMode = 0, std::string* error = nullptr);

    // Load the requested stage's event/section tables, then run the Stage_Start frame. Safe after
    // Boot/title/lobby use too: those modes replace the section tables. False if the stage data is missing.
    bool StartStage(const WorldConfig& cfg, std::string* error = nullptr);
    void Step();                              // one following frame
    // The whole game from boot (notes/agents/frontend.md): frame 0 of Game_Frame in game mode 16 (boot logos). Every
    // following Step() is one Game_Update 0x0040E730 for whatever mode the game is in: boot logos -> title (3) ->
    // how-to-play (9) -> attract demo (5) -> story sequences (11, 10) -> title ..., START -> mode select (4) -> game (6)
    // -> game over (7) -> title. Input comes from SetInput in every mode (buttons; mode 5 ignores the guns).
    void Boot(const WorldConfig& cfg);
    // A repeatable network start, independent of the menus visited before joining. Also used for rejoin replay.
    bool StartNetworkGame(const WorldConfig& cfg, std::string* error = nullptr);
    bool NetworkRoom() const;
    bool NetworkLobby() const;
    void LeaveNetworkRoom();
    // The mode select's EXIT entry was confirmed (0x0049E930: the original quits).
    bool QuitRequested() const;
    // play.exe's pause menu "Return to Main Menu" (no counterpart in the original, which leaves a game only through the
    // game over): lifts the pause and commits game mode 3 (the title, Mode_Next 0x0040E960's target from mode 6) right
    // away, as ForceMode does; the next Step() runs the title's init frame. A world without the front end (StartStage)
    // only gets the mode switch. The caller stops the sounds (Snd_StopAll 0x0041D350) itself.
    void RequestTitle();
    // Port only: the current volume settings (0..10 each) for the options screen's volume rows (WorldConfig::portVolumes),
    // e.g. after the pause menu changed them.
    void SetPortVolumes(const int v[5]);
    // These touch only gameplay preference bytes in the existing save; scores, unlocks and device calibration stay
    // intact. Edits are available in idle local front-end menus, and apply to the next game. False = blocked/invalid.
    PortGameOptions GetPortGameOptions() const;
    bool SetPortGameOptions(const PortGameOptions& options);
    bool ResetPortGameOptions();
    // Port only: WorldConfig::portModernMenu after Boot (the display settings page changed it); the mode select's
    // task reads it every frame.
    void SetPortModernMenu(bool on);
    void SetPortHordeLeaderboard(const std::vector<HordeLeaderboardEntry>& entries);
    HordeRunSnapshot PortHordeRun() const;
    void SetPortSharedLeaderboard(int table, const std::vector<SharedLeaderboardEntry>& entries, const std::string& status);
    void SetPortLeaderboardPublishable(bool on, const std::string& status = "");
    void EditPortLeaderboardName(const std::string& text, bool backspace = false);  // active online name dialog only
    void SetPortLeaderboardRecording(bool on);  // port disables this for scripts, replay and debug runs
    LeaderboardRunSnapshot PortLeaderboardRun() const;
    // The port supplies only compatible real waiting rooms; at most two are displayed.
    void SetPortHomeRooms(const std::vector<PortHomeRoom>& rooms, const std::string& status);
    void ScrollPortExtras(int rows, int column = -1);  // local wheel input; consumed once by the Extras page
    // Test hook (frontend_test --resync): commit game mode `mode` now, as Game_ApplyState would at the end of this
    // frame, with the task serial counter, CRT rand state and demo index the capture shows, so that a mode can be
    // compared on its own after an earlier mode drifted.
    void ForceMode(int mode, uint32_t nextSerial, uint32_t randState, int demoIndex);
    // Device input for the NEXT Step() (held until changed; edge fields are consumed by that step). Mode 5 ignores it.
    void SetInput(int player, const WorldInput& in);
    // The original's wall-clock timer 0x0049DF20 (GetTickCount since 0x0049DF00, in 1/60 s: the lessons' time and the
    // boss-mode fight time) for the NEXT Step() only (tests: the capture's value of that frame). Without it the timer
    // counts game frames.
    void SetWallClock(int count);
    // Test setup (the hook's HOTD2_POKE in game memory, applied before the NEXT Step()): bytes (little endian) at an
    // original address. Supported: 0x009CA0EC / 0x009A21BA (s16 civilians rescued / met), 0x009A5C6C / 0x009A5D9C (s32
    // player score): the route checks of actor 0x64. Returns false for other addresses / sizes.
    bool TestPoke(uint32_t addr, const std::vector<uint8_t>& bytes);
    // Game mode 0x009C8E98 after the last frame: 7 = game over (the original replaced the stage tasks by the game-over
    // screen in that frame; Step() does nothing more).
    int GameMode() const;

    int Frame() const;
    void Civilians(int* rescued, int* met) const;  // 0x009CA0EC / 0x009A21BA (actor 0x64's route check)
    const WorldCamera& Camera() const;
    // Screen point (640 x 480) the projection is centred on for this frame's draws: 0x00419490 sets 0x009C7300 /
    // 0x009C7304 (NDC x = px / 320 - 1, y = 1 - py / 240), Proj_Setup 0x004184C0 translates the projection by it at
    // the frame start. (320, 240) except during the staff roll (0x00431810: slide + 320); reset by a mode change.
    void ProjCenter(float* x, float* y) const;
    // The active context's position / angles as they are now (Camera() is the snapshot taken when the views were built;
    // a context can change after that in the same frame, e.g. the map screen setup). view / inverse are not filled.
    WorldCamera ContextNow() const;
    const std::vector<WorldDraw>& Draws() const;
    const WorldHud& Hud() const;
    // Fog, the env blocks and the light table after the last frame (renderer state).
    const WorldLights& Lights() const;
    const Environment& Env() const;

    // ---- state for verification / tools ----
    struct RandCall {
        uint32_t site;    // return address of the original call (0 = site not known)
        uint32_t serial;  // task running (0 = none)
        int value;
    };
    const std::vector<RandCall>& RandCalls() const;  // this frame, in order
    uint32_t RandState() const;
    struct TaskInfo {
        uint32_t serial, fn;
    };
    std::vector<TaskInfo> Tasks() const;                     // live tasks in execution order (the capture's K line)
    const std::vector<WorldEventOp>& EventOps() const;  // event ops executed this frame (E lines)
    struct EnemyInfo {
        uint32_t serial;
        int kind, hp, behaviour, sub, motion, counter;
        uint32_t flags34, flags136C;
        float pos[3];
        float body[3];     // +0x100 world body point (after the +4 of 0x00409B70)
        float viewPos[3];  // +0x70
        int rot[3];        // +0x64
        int tick;          // +0x19C
        int target;        // +0x121 target player (-1 none)
    };
    std::vector<EnemyInfo> Enemies(bool civilians = false) const;  // civilians: also actor type 0x10 (0x0048A920)
    // Route planning (world_plan.cpp, tests/world_test --plan): the shot targets registered this frame (what the next
    // frame's hit task tests) and a preview of player p's shot at gun pixel (x, y) (mode-6 calibration) against them:
    // the object the hit task would pick (nearest entry), without side effects on the simulation.
    struct ShotTargetInfo {
        uint32_t fn = 0, serial = 0, flags = 0;
        float view[3] = {};
        float radius = 0;
        bool civilian = false;
        std::vector<std::array<float, 4>> nodes;  // skeleton targets: view hit point + radius of the shootable nodes
    };
    struct ShotPreview {
        bool any = false;       // something is hit
        bool level = false;     // level geometry
        uint32_t serial = 0, fn = 0;
        int node = -1;
        bool civilian = false;
    };
    std::vector<ShotTargetInfo> ShotTargets() const;
    ShotPreview PreviewShot(int p, int x, int y);
    float Focal() const;
    int LiveEnemies() const;  // 0x009C904A
    bool LockOnActive(int k) const;  // lock-on slot k (0x009A5EC0 + 8k) in use
    // debugging: slot k's object point (+0x100) and target player (+0x121); false when the slot is free
    bool LockOnInfo(int k, float point[3], int* targetPlayer) const;
    int Attackers() const;    // 0x009C7006
    // What the sound layer is asked to do, in order. kind 0 is Snd_PlayGameSound 0x0041CFD0(id) (the captures' U
    // lines); the others are the direct calls that bypass it (game::SoundDriver has one method for each).
    enum SoundKind : int {
        kPlayGameSound = 0,
        kStopAll = 1,    // Snd_StopAll 0x0041D350 (mode / scene changes)
        kCommand = 2,    // Snd_Command 0x004ABF80(_, id, _) (pause, civilian scream)
        kStopSlot = 3,   // Snd_StopSlot 0x0041D4F0(id) (pause)
        kOptionFeedback = 4,  // Port-only UI feedback; survives menu scene changes, separate from sound previews.
        kPreviewMusicStart = 5,
        kPreviewMusicPlay = 6,
        kPreviewMusicPause = 7,
        kPreviewMusicReset = 8,
        kPreviewEffect = 9,
        kPreviewStop = 10,
    };
    struct SoundEvent {
        uint32_t id;
        uint32_t serial;
        int kind = kPlayGameSound;
        int gameMode = 0;  // 0x009C8E98 when issued (with gameType: Snd_PlayGameSound's BGM table, sound_table.h)
        int gameType = 0;  // 0x009CA08C
    };
    const std::vector<SoundEvent>& Sounds() const;  // sound commands issued this frame
    // Unported code paths reached this frame (each a short text naming the original function).
    const std::vector<std::string>& Notes() const;
    const EventRunner& Events() const;
    PrototypeStatus InspectPrototype() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace game
