// play.exe <game dir> [--audio] [--width W --height H] [--script file --screenshot out.png --frame F]
//          [--renderer d3d11|sdlgpu] [--input auto|raw|mouse|touch] [--audio-backend sdl|xaudio2]
//          [--no-update-check]
//
// The game from boot on the native world runtime (World::Boot, notes/agents/frontend.md): boot logos, title, attract
// loop, START -> mode select -> game (game mode 6, notes/agents/gameplay.md) -> game over -> title. --direct starts a game
// at once instead (the old behaviour: a new game after each game over; --serial / --seed / --credits apply to it).
//   input   - a gun source of the platform layer (src/port/platform, notes/agents/platform.md). Default on Windows: one
//             raw-input device per player (src/port/input; first trigger pull joins P1, the second device P2;
//             left button = trigger, right = reload, middle = start); --input mouse (SDL system pointer, P1) or touch
//             (tap = shot, outside the image / second finger / long press = reload). Converted like the hook does for
//             the original (game::MakeWorldInput: 640x480 game pixels, off-screen / reload = off-screen shot) and fed
//             with World::SetInput before every World::Step;
//   render  - the world's draws (Scene::BuildFromWorld: skinning, culling, pass-1 list) + the gameplay HUD
//             (game/hud.*, from World::Hud) through ModelRenderer::DrawGame (GPU backend: D3D11 or SDL_GPU),
//             lighting / fog from scenes/st1_play.scene;
//   sound   - hotd2.exe enables sound by default; play.exe accepts --audio for SDL3 audio (or XAudio2).
// Headless check: --script replays an input script in the hook's format (<boot frame> <player> <x> <y> <buttons>,
// frames counted from boot like the capture's F numbers = world frame) and --screenshot saves --frame.
// Esc pauses/resumes or backs out of menus; quit through the window close button or the mode select's EXIT.
// The trigger also works as start for an inactive player (title, demos, mode select: confirm).
// Keyboard: Up / Down move the cursor, Enter confirms or starts an inactive player. A second device joins as P2
// with its first trigger pull; start (middle button) while playing pauses.
// Physical two-device behaviour is NOT verified (pending a person with two devices).

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>  // MessageBoxA (no game folder found)
#endif

#include "game/exe_image.h"
#include "game/hud.h"
#include "game/menu_pointer.h"
#include "game/menu_text.h"
#include "game/save_data.h"
#include "game/sound_driver.h"
#include "game/sound_table.h"
#include "game/world.h"
#include "game/world_config_io.h"
#include "game/world_sound.h"
#include "platform/fixed_clock.h"
#include "platform/present_clock.h"
#include "port/net/leaderboard.h"
#include "port/play/cursor_visibility.h"
#include "port/play/display_pointer.h"
#include "port/play/display_viewport.h"
#include "port/play/gameplay_hud.h"
#include "port/play/gun_input.h"
#include "port/play/home_lobby_preview.h"
#include "port/play/attraction_audio.h"
#include "port/play/keyboard.h"
#include "port/play/lightgun_menu.h"
#include "port/play/local_leaderboard.h"
#include "port/play/night_shift_menu.h"
#include "port/play/model_gallery.h"
#include "port/play/model_gallery_audio.h"
#include "port/play/pack_preview.h"
#include "port/play/pause_menu.h"
#include "port/play/renderer_fallback.h"
#include "port/play/run_name_prompt.h"
#include "port/play/scroll_hint.h"
#include "port/play/widescreen.h"
#include "port/play/wii_menu.h"
#include "render/display_calibration.h"
#include "render/frame_statistics.h"
#include "render/gpu_budget.h"
#include "render/hud_composition.h"
#include "render/output_resolution.h"
#include "render/ui_font.h"
#ifdef _WIN32
#include "port/play/reconstruction_fallback.h"
#include "render/dlss_frame_generation.h"
#include "render/dlss_super_resolution.h"
#include "render/fsr_frame_generation.h"
#include "render/fsr_super_resolution.h"
#include "render/native_d3d12_frame_generation.h"
#include "render/native_d3d12_raytracing.h"
#include "render/xess_super_resolution.h"
#endif
#include "assets/asset_override.h"
#include "input/focused_guns.h"
#include "game/stage.h"
#include "net/netplay.h"
#include "platform/platform.h"
#include "render/asset_store.h"
#include "render/front_screens.h"
#include "render/model_renderer.h"
#include "scene/scene.h"
#include "update/update_check.h"
#include "update/update_install.h"

namespace {

// Keyboard for P1's pad bits (0x009C9028): Up 0x10 / Down 0x20 / Left 0x40 / Right 0x80 (mode select, the boss /
// lesson / item selects, name entry), Enter = start 0x8, Space = confirm 0x4, Backspace = reload / back 0x2 (the
// selects' back mask 0x00589170, the rankings' exit)
uint32_t g_keys = 0;
uint32_t KeyBit(platform::Key k) {
    switch (k) {
        case platform::Key::Up:
            return 0x10u;
        case platform::Key::Down:
            return 0x20u;
        case platform::Key::Left:
            return 0x40u;
        case platform::Key::Right:
            return 0x80u;
        case platform::Key::Enter:
            return 0x8u;
        case platform::Key::Escape:
            return play::kEscapeKey;
        case platform::Key::Space:
            return 0x4u;
        case platform::Key::Backspace:
            return 0x2u;
        case platform::Key::F8:
            return 0x400u;
        case platform::Key::G:
            return 0x800u;
        case platform::Key::H:
            return play::kPreviewKey;
        default:
            return 0u;
    }
}

// Netplay: the world's state after a step, hashed for the desync check (net/netplay.h): frame, rand state, game mode,
// the players' state / lives / score and every draw's model and matrix (the positions of everything on screen).
uint32_t WorldHash(const game::World& w) {
    uint32_t h = 2166136261u;
    auto mix = [&](const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 16777619u;
        }
    };
    const int frame = w.Frame(), mode = w.GameMode();
    const uint32_t rnd = w.RandState();
    mix(&frame, 4);
    mix(&mode, 4);
    mix(&rnd, 4);
    const auto& hud = w.Hud();
    mix(&hud.randomizerSeed, sizeof(hud.randomizerSeed));
    mix(&hud.randomizerHash, sizeof(hud.randomizerHash));
    mix(&hud.menuCursor, sizeof(hud.menuCursor));
    mix(&hud.gameType, sizeof(hud.gameType));
    mix(&hud.paused, sizeof(hud.paused));
    const auto& results = hud.stageResults;
    for (int v : {int(results.active), int(results.shown), int(results.dismiss), results.chapter, results.frames, results.civilians,
                  results.lifeBonus})
        mix(&v, sizeof(v));
    for (const auto& p : results.player)
        for (int v : {int(p.participated), p.score, p.shots, p.hits, p.headshots, p.rescues, p.damage, p.continues}) mix(&v, sizeof(v));
    for (int p = 0; p < 2; ++p) {
        const auto& pl = w.Hud().player[p];
        mix(&pl.state, 4);
        mix(&pl.lives, 4);
        mix(&pl.score, 4);
    }
    for (const auto& d : w.Draws()) {
        mix(&d.model, 4);
        mix(d.world.m, sizeof(d.world.m));
        mix(&d.alpha, 4);
        mix(&d.node, 4);
    }
    return h;
}

// The build: shown on the Network screen and exchanged in the netplay handshake (two different builds could run
// different worlds, so they do not connect). Bump both on every change that affects the game or the protocol.
// Presentation-only updates can change the displayed label while retaining multiplayer compatibility.
#if INTPTR_MAX == INT64_MAX
constexpr uint32_t kBuildId = 2026102300;
const char* const kBuildName = "2026 10 23 A";
#else
constexpr uint32_t kBuildId = 2026102301;
const char* const kBuildName = "2026 10 23 NS32 A";
#endif
// Release ordering is architecture-independent; keep this in step with build labels.
const char* const kUpdateBuildName = "2026 10 23 A";
const char* const kRejoinTicket = "hotd2re_rejoin.bin";

// The lobby server (server/lobby): "host:port"; hotd2re_settings.ini lobby=, HOTD2_LOBBY or --lobby override it.
const char* const kDefaultLobby = "64.23.186.88:27020";

#ifdef _WIN32
struct DlssRuntime {
    port::DlssSuperResolution processor;
    port::FsrSuperResolution fsr;
    port::XessSuperResolution xess;
    int family = 1;  // 1 NVIDIA, 2 AMD, 3 Intel
    bool active = false;
    int quality = 0, outputW = 0, outputH = 0;
    std::string error, pendingFailure;
    static bool Process(const port::NativeD3D12FrameInterop& frame, port::NativeD3D12FrameOutput* output, void* user, std::string* error) {
        auto& runtime = *static_cast<DlssRuntime*>(user);
        runtime.error.clear();
        if (!runtime.pendingFailure.empty()) {
            runtime.error = runtime.pendingFailure;
            if (error) *error = runtime.error;
            return false;
        }
        if (!frame.motionValid) return true;  // A purely 2D screen has no temporal scene to reconstruct.
        const bool okay = runtime.family == 3   ? runtime.xess.Evaluate(frame, output, &runtime.error)
                          : runtime.family == 2 ? runtime.fsr.Evaluate(frame, output, &runtime.error)
                                                : runtime.processor.Evaluate(frame, output, &runtime.error);
        if (!okay) {
            if (runtime.error.empty()) runtime.error = "The reconstruction SDK did not produce a valid frame";
            runtime.pendingFailure = runtime.error;
        }
        if (error) *error = runtime.error;
        return okay;
    }
    bool Size(int width, int height, int* inputW, int* inputH, std::string* notice) {
        const bool configured = family == 3   ? xess.Configure(width, height, quality, inputW, inputH, notice)
                                : family == 2 ? fsr.Configure(width, height, quality, inputW, inputH, notice)
                                              : processor.Configure(width, height, quality, inputW, inputH, notice);
        if (!configured) return false;
        outputW = width;
        outputH = height;
        return true;
    }
};
struct FrameGenerationRuntime {
    port::DlssFrameGeneration processor;
    port::FsrFrameGeneration fsr;
    int family = 1;
    bool active = false;
    std::string error;
    static bool Process(const port::NativeD3D12FinalFrame& frame, port::NativeD3D12GeneratedFrames* result, void* user,
                        std::string* error) {
        auto& runtime = *static_cast<FrameGenerationRuntime*>(user);
        runtime.error.clear();
        if (!frame.raster.motionValid || !frame.raster.camera.valid || frame.paused) {
            if (error) *error = frame.paused ? "Paused: rendered frames only" : "Waiting for an actual moving scene: rendered frames only";
            return false;
        }
        if (runtime.family == 2) {
            const bool okay = runtime.fsr.Evaluate(frame, result, &runtime.error);
            if (error) *error = runtime.error;
            return okay;
        }
        port::DlssFrameGenerationInput input;
        input.raster = frame.raster;
        input.camera = frame.raster.camera;
        input.finalColor = frame.color;
        input.hudless = frame.hudless;
        input.finalColorState = frame.colorState;
        input.hudlessState = frame.hudlessState;
        input.paused = frame.paused;
        port::DlssFrameGenerationOutput output;
        const bool okay = runtime.processor.Evaluate(input, &output, &runtime.error);
        if (okay) {
            result->generated = output.generated;
            result->retained = output.retained;
            result->disableInterpolation = output.disableInterpolation;
            result->colorState = output.colorState;
            result->flagState = output.flagState;
            result->reset = output.reset;
        }
        if (error) *error = runtime.error;
        return okay;
    }
};
#endif
float HaltonJitter(uint64_t index, int base) {
    float value = 0, fraction = 1.f / base;
    for (; index; index /= base, fraction /= base) value += fraction * float(index % base);
    return value - .5f;
}
double ThreadCpuMilliseconds() {
#ifdef _WIN32
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) {
        ULARGE_INTEGER k{}, u{};
        k.LowPart = kernel.dwLowDateTime;
        k.HighPart = kernel.dwHighDateTime;
        u.LowPart = user.dwLowDateTime;
        u.HighPart = user.dwHighDateTime;
        return (k.QuadPart + u.QuadPart) / 10000.0;
    }
#endif
    return 0;
}
port::GraphicsOptions SupportedGraphicsOptions(port::GraphicsOptions g, const port::GraphicsCapabilities& caps) {
    if (g.msaa == 4 && !caps.msaa4) g.msaa = caps.msaa2 ? 2 : 1;
    if (g.msaa == 2 && !caps.msaa2) g.msaa = 1;
    if (!caps.msaa2 && !caps.msaa4) g.alphaToCoverage = false;
    int af = 1;
    for (int value : {2, 4, 8, 16})
        if (value <= caps.maxAnisotropy && value <= g.anisotropy) af = value;
    g.anisotropy = af;
    if ((g.postAA == 2 && !caps.smaa) || (g.postAA == 3 && !caps.cmaa2)) g.postAA = 0;
    if (!caps.temporalAA) g.temporalAA = false;
    if (!caps.spatialFSR1) g.spatialUpscaler = 0;
    if (!caps.outputResolutionHud) g.outputResolutionHud = false;
    if (!caps.softParticles) g.softParticles = 0;
    if (!caps.materialMaps) g.materialMaps = false;
    if (!caps.ambientOcclusion) g.ambientOcclusion = 0;
    if (!caps.shadowMaps) g.shadowMaps = 0;
    if (!caps.sceneEffects) g.volumetricFog = g.reflections = g.cinematicFocus = 0;
    if (!caps.hdrOutput) g.hdrOutput = false;
    if (!caps.tearing) g.allowTearing = false;
    if (!caps.lowLatency) g.lowLatency = false;
    return g;
}

// The in-game online lobby over the world's Network screen (mode 15 state 1 with WorldConfig::portFeatures:
// background and title only, world_modes_net.cpp): play.exe's pages and their state.
//   Menu:   Create Room / Browse Lobby / Peer to Peer / Back
//   Create: Unlimited Continues on / off, Private Room on / off, Start -> Waiting (the room's number, its 5-digit
//           code when private, "waiting for a player")
//   Browse: the server's public and private rooms (newest last), a private one asks for its code (Code page)
//   P2P:    Host (an unlisted room: the code is the only way in) / Join (the Code page)
//   Connecting / Error: the join's answer, the hole punch, what went wrong.
// Input: up / down (keyboard, pad, or a pointer over a row), left / right toggle an option, Enter / Space / trigger
// confirm, Backspace / reload back (on the Menu page it leaves the screen, which the world handles).
struct Lobby {
    enum Page { kOff, kMenu, kCreate, kWaiting, kBrowse, kCode, kP2P, kConnecting, kRejoin, kError };
    Page page = kOff;
    int cursor = 0;
    bool unlimited = true, priv = false;  // Create Room's options
    bool p2p = false;                     // the Waiting / Code page came from Peer to Peer
    uint32_t joinRoom = 0;                // Browse: the chosen room (0 = by code only)
    std::string code;                     // the typed digits (Code page)
    std::string message;                  // Error page
    double pageSeconds = 0;               // time on the page (timeouts, the list's refresh)
    double lastList = -10;
    bool announced = false;  // Waiting: the room's number and code were printed (tests read them)
    bool canRejoin = false;
    game::MenuPointerFocus pointer;
    int pointerKey = -1;

    static constexpr float kRowX = 17.0f, kRowY0 = 5.0f, kRowPitch = 1.25f;   // menu rows, cells (the widest row ends at x 39)
    static constexpr float kListX = 2.0f, kListY0 = 3.5f, kListPitch = 1.0f;  // Browse rows
    int RowCount(size_t rooms) const {
        switch (page) {
            case kMenu:
                return canRejoin ? 5 : 4;
            case kCreate:
                return 4;
            case kP2P:
                return 3;
            case kBrowse:
                return static_cast<int>(std::min<size_t>(rooms, 12)) + 1;
            default:
                return 0;
        }
    }
    // the row under game pixel y on this page, -1 = none
    int RowAt(float px, float py, size_t rooms, bool modern = false) const {
        const bool list = page == kBrowse;
        const int n = RowCount(rooms);
        for (int r = 0; r < n; ++r)
            if (game::MenuTextRowHit(px, py, (list ? kListY0 : kRowY0) + r * (list ? kListPitch : kRowPitch),
                                     modern ? 26.0f
                                     : list ? 24.0f
                                            : 264.0f,
                                     636))
                return r;
        return -1;
    }
    static int KeyAt(float x, float y) {
        // Two rows of five digits, then Delete / Back. Same positions as LobbySprites.
        for (int i = 0; i < 10; ++i)
            if (game::MenuTextRowHit(x, y, 11.0f + (i / 5) * 1.5f, 184.0f + i % 5 * 56, 232.0f + i % 5 * 56)) return i;
        if (game::MenuTextRowHit(x, y, 14.0f, 176, 304)) return 10;
        if (game::MenuTextRowHit(x, y, 14.0f, 352, 464)) return 11;
        return -1;
    }
};

std::string RoomLabel(const port::net::LobbyRoom& r, uint32_t build) {
    std::string s = "ROOM " + std::to_string(r.id);
    s.append(s.size() < 10 ? 10 - s.size() : 1, ' ');
    s += (r.flags & port::net::kRoomPrivate) ? "PRIVATE  " : "PUBLIC   ";
    static constexpr const char* levels[] = {"VERY EASY", "EASY", "NORMAL", "HARD", "VERY HARD"};
    const int difficulty = port::net::RoomDifficulty(r.flags);
    s += difficulty >= 0 ? levels[difficulty] : "UNKNOWN";
    s += "  ";
    s += (r.flags & port::net::kRoomUnlimited) ? "UNLIMITED" : "3 CONTINUES";
    if (r.build != build) s += "  OTHER VER";
    return s;
}

// The pages' text over the world's lobby screen (menu font, cells of 16 x 24 px; white, red = selected, grey = a
// note). Over the bitmap screens (TL quads at z 0) as the modern menu's text: flags 0x700, the default depth.
void LobbySprites(const game::ExeImage& exe, const Lobby& l, const port::net::Session& net, std::vector<game::SpriteDef>& out) {
    const size_t first = out.size();
    auto row = [&](int i, const std::string& text, bool list = false) {
        const float x = list ? Lobby::kListX : Lobby::kRowX;
        const float y = (list ? Lobby::kListY0 : Lobby::kRowY0) + static_cast<float>(i) * (list ? Lobby::kListPitch : Lobby::kRowPitch);
        const uint32_t flags = i == l.cursor ? 0x10u : 1u;
        if (list) {
            // Full difficulty names and long room IDs must fit the Classic menu too.
            const float width = game::MenuTextAdvance(exe, 1) * static_cast<float>(text.size());
            const float scale = width > 580 ? 580 / width : 1;
            game::MenuTextScaled(exe, x * 16, y * 24 + (1 - scale) * 22, text, flags, scale, out);
        } else {
            game::MenuText(exe, x, y, text, flags, out);
        }
    };
    auto note = [&](float y, const std::string& text, uint32_t flags = 0x40u) {
        game::MenuText(exe, 0.0f, y, text, flags | 2u, out);
    };  // centred
    switch (l.page) {
        case Lobby::kMenu:
            row(0, "CREATE ROOM");
            row(1, "BROWSE LOBBY");
            row(2, "PEER TO PEER");
            if (l.canRejoin) row(3, "REJOIN GAME");
            row(l.canRejoin ? 4 : 3, "BACK");
            break;
        case Lobby::kCreate:
            row(0, std::string("UNLIMITED CONTINUES ") + (l.unlimited ? "ON" : "OFF"));
            row(1, std::string("PRIVATE ROOM ") + (l.priv ? "ON" : "OFF"));
            row(2, "START");
            row(3, "BACK");
            note(16.5f, "LEFT AND RIGHT CHANGE AN OPTION");
            break;
        case Lobby::kP2P:
            row(0, "HOST");
            row(1, "JOIN");
            row(2, "BACK");
            note(16.5f, "THE HOST GETS A 5 DIGIT CODE FOR THE JOINER");
            break;
        case Lobby::kWaiting: {
            const bool created = net.LobbyCreated();
            game::MenuText(exe, Lobby::kRowX, 5.0f, created ? "ROOM " + std::to_string(net.LobbyRoomId()) : "REGISTERING THE ROOM...", 1,
                           out);
            if (created && (l.priv || l.p2p)) {
                game::MenuText(exe, Lobby::kRowX, 6.5f, "CODE", 0x40, out);
                game::MenuText(exe, Lobby::kRowX + 5.0f, 6.5f, std::to_string(net.LobbyCode()), 1, out);
            }
            note(16.5f, net.PeerKnown() ? "PLAYER FOUND - CONNECTING..." : (created ? "WAITING FOR A PLAYER" : "..."));
            note(17.5f, "CLOSE ROOM", l.pointerKey == 11 ? 0x10 : 1);
            break;
        }
        case Lobby::kBrowse: {
            const auto& rooms = net.LobbyRooms();
            const int shown = static_cast<int>(std::min<size_t>(rooms.size(), 12));
            for (int i = 0; i < shown; ++i) row(i, RoomLabel(rooms[static_cast<size_t>(i)], kBuildId), true);
            row(shown, "BACK", true);
            if (!net.LobbyRoomsKnown())
                note(17.0f, "ASKING THE LOBBY SERVER...");
            else if (rooms.empty())
                note(17.0f, "NO ROOMS YET - CREATE ONE OR WAIT");
            break;
        }
        case Lobby::kCode: {
            note(5.0f, l.joinRoom ? "ROOM " + std::to_string(l.joinRoom) + " IS PRIVATE" : "PEER TO PEER", 1);
            note(6.5f, "ENTER THE HOSTS 5 DIGIT CODE");
            std::string shown = l.code;
            while (shown.size() < 5) shown += '_';
            std::string spaced;
            for (char c : shown) spaced += std::string(1, c) + ' ';
            note(8.5f, spaced, 0x10);
            for (int i = 0; i < 10; ++i)
                game::MenuText(exe, 12.0f + (i % 5) * 3.5f, 11.0f + (i / 5) * 1.5f, std::to_string((i + 1) % 10),
                               l.pointerKey == i ? 0x10 : 1, out);
            game::MenuText(exe, 11.5f, 14.0f, "DELETE", l.pointerKey == 10 ? 0x10 : 1, out);
            game::MenuText(exe, 22.5f, 14.0f, "BACK", l.pointerKey == 11 ? 0x10 : 1, out);
            note(17.5f, "BACKSPACE DELETES");
            break;
        }
        case Lobby::kConnecting:
            note(16.5f,
                 net.LobbyJoinResult() == port::net::JoinResult::Pending ? "ASKING THE LOBBY SERVER..." : "CONNECTING TO THE HOST...");
            note(17.5f, "CANCEL", l.pointerKey == 11 ? 0x10 : 1);
            break;
        case Lobby::kRejoin:
            note(16.5f, "FINDING YOUR MATCH...");
            note(17.5f, "CANCEL", l.pointerKey == 11 ? 0x10 : 1);
            break;
        case Lobby::kError:
            note(16.5f, l.message, 0x10);
            note(17.5f, "PRESS A BUTTON");
            break;
        default:
            break;
    }
    game::MenuText(exe, 1.0f, 18.6f, std::string("VERSION ") + kBuildName, 0x40, out);  // both players need the same one
    for (size_t i = first; i < out.size(); ++i) out[i].flags |= 0x700;
}

game::WorldHud ModernLobbyHud(const Lobby& l, const port::net::Session& net) {
    game::WorldHud h;
    h.nightShiftPage = 6;
    h.menuPanels = {{0, 0, 640, 480, 0xDB090D10}, {26, 65, 614, 67, 0xFF92272D}, {0, 440, 640, 480, 0xF0090D10}};
    auto text = [&](float x, float y, const std::string& value, float size = .62f, uint32_t color = 0xFFECE7DC) {
        h.menuText.push_back({x, y, value, size, color});
    };
    auto row = [&](int i, const std::string& value, bool list = false) {
        const float top = ((list ? Lobby::kListY0 : Lobby::kRowY0) + i * (list ? Lobby::kListPitch : Lobby::kRowPitch)) * 24 + 10;
        if (i == l.cursor) h.menuPanels.push_back({26, top - 2, 614, top + 23, 0xDD8F2026});
        text(34, top, value, list ? .52f : .67f);
    };
    auto note = [&](float row, const std::string& value, uint32_t color = 0xFFBFB8AA) { text(28, row * 24, value, .50f, color); };
    const char* title = l.page == Lobby::kCreate    ? "HOST ONLINE"
                        : l.page == Lobby::kBrowse  ? "ONLINE LOBBIES"
                        : l.page == Lobby::kP2P     ? "PEER TO PEER"
                        : l.page == Lobby::kCode    ? "JOIN WITH CODE"
                        : l.page == Lobby::kWaiting ? "WAITING ROOM"
                                                    : "ONLINE";
    text(26, 20, title, 1.2f);
    switch (l.page) {
        case Lobby::kMenu:
            row(0, "HOST ONLINE");
            row(1, "BROWSE LOBBY");
            row(2, "PEER TO PEER");
            if (l.canRejoin) row(3, "REJOIN GAME");
            row(l.canRejoin ? 4 : 3, "BACK");
            break;
        case Lobby::kCreate:
            row(0, std::string("UNLIMITED CONTINUES   ") + (l.unlimited ? "ON" : "OFF"));
            row(1, std::string("PRIVATE ROOM   ") + (l.priv ? "ON" : "OFF"));
            row(2, "CREATE ROOM");
            row(3, "BACK");
            note(16.5f, "LEFT / RIGHT CHANGE A RULE. CREATE ROOM STARTS HOSTING.");
            break;
        case Lobby::kP2P:
            row(0, "HOST WITH CODE");
            row(1, "JOIN WITH CODE");
            row(2, "BACK");
            note(16.5f, "THE HOST GETS A FIVE-DIGIT CODE TO SHARE.");
            break;
        case Lobby::kWaiting:
            note(5, net.LobbyCreated() ? "ROOM " + std::to_string(net.LobbyRoomId()) : "REGISTERING ROOM...", 0xFFECE7DC);
            if (net.LobbyCreated() && (l.priv || l.p2p)) text(28, 160, "CODE  " + std::to_string(net.LobbyCode()), 1.2f);
            note(16.5f, net.PeerKnown() ? "PLAYER FOUND / CONNECTING..." : "WAITING FOR A PARTNER");
            note(17.5f, "CLOSE ROOM", l.pointerKey == 11 ? 0xFFFFBDB3 : 0xFFECE7DC);
            break;
        case Lobby::kBrowse: {
            const int shown = static_cast<int>(std::min<size_t>(net.LobbyRooms().size(), 12));
            for (int i = 0; i < shown; ++i) row(i, RoomLabel(net.LobbyRooms()[i], kBuildId), true);
            row(shown, "BACK", true);
            note(17, !net.LobbyRoomsKnown()     ? "CHECKING WAITING ROOMS..."
                     : net.LobbyRooms().empty() ? "NO WAITING ROOMS. HOST ONLINE TO CREATE ONE."
                                                : "SELECT A ROOM TO JOIN. PRIVATE ROOMS REQUIRE A CODE.");
            break;
        }
        case Lobby::kCode: {
            note(5, l.joinRoom ? "ROOM " + std::to_string(l.joinRoom) + " IS PRIVATE" : "ENTER THE HOST'S FIVE-DIGIT CODE", 0xFFECE7DC);
            std::string code = l.code;
            while (code.size() < 5) code += '_';
            std::string spaced;
            for (char c : code) spaced += std::string(1, c) + ' ';
            text(194, 204, spaced, 1.2f, 0xFFFFBDB3);
            for (int i = 0; i < 10; ++i)
                text(192.f + (i % 5) * 56, 274.f + (i / 5) * 36, std::to_string((i + 1) % 10), .7f,
                     l.pointerKey == i ? 0xFFFFBDB3 : 0xFFECE7DC);
            text(184, 346, "DELETE", .62f, l.pointerKey == 10 ? 0xFFFFBDB3 : 0xFFECE7DC);
            text(360, 346, "BACK", .62f, l.pointerKey == 11 ? 0xFFFFBDB3 : 0xFFECE7DC);
            note(17.5f, "TYPE A CODE OR SELECT THE DIGITS. BACKSPACE DELETES.");
            break;
        }
        case Lobby::kConnecting:
            note(16.5f,
                 net.LobbyJoinResult() == port::net::JoinResult::Pending ? "ASKING THE LOBBY SERVICE..." : "CONNECTING TO THE HOST...");
            note(17.5f, "CANCEL");
            break;
        case Lobby::kRejoin:
            note(16.5f, "FINDING YOUR MATCH...");
            note(17.5f, "CANCEL");
            break;
        case Lobby::kError:
            note(16.5f, l.message, 0xFFFFBDB3);
            note(17.5f, "PRESS A BUTTON TO RETURN");
            break;
        default:
            break;
    }
    text(28, 452, std::string("BUILD ") + kBuildName, .42f, 0xFFBFB8AA);
    return h;
}

struct ScriptLine {
    int frame, player;
    float x, y;
    uint8_t buttons;
};

std::vector<ScriptLine> LoadScript(const std::string& path) {
    std::vector<ScriptLine> out;
    std::ifstream f(path);
    for (std::string line; std::getline(f, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        ScriptLine s{};
        unsigned b = 0;
        if (ss >> s.frame >> s.player >> s.x >> s.y >> b) {
            s.buttons = static_cast<uint8_t>(b);
            out.push_back(s);
        }
    }
    return out;
}

// World::Hud -> game::HudState (the HUD layouts read the original's globals; see game/hud.h for each field).
game::HudState MakeHudState(const game::World& world, const game::WorldInput in[2], int modeAge, uint32_t blink, bool freePlay) {
    const auto& h = world.Hud();
    const auto options = world.GetPortGameOptions();
    game::HudState s;
    s.gameMode = h.gameMode;
    if (h.prototype) s.weaponMode = 1;
    s.letterbox = static_cast<int8_t>(h.letterbox);
    s.canShoot = h.canShoot ? 1 : 0;
    s.torchCounter = h.frameCounter;
    s.blinkCounter = static_cast<int32_t>(blink);  // 0x005A4D44: held while paused (hud_test: credit lines exact)
    s.bootFrames = static_cast<uint32_t>(modeAge);
    s.flags5900 = h.flags5900;
    // credit line (0x00406CE0): free play, or the shared credits 0x009C8E60 and the coin state 0x00407060 sets: 0 none,
    // 1 fewer than a start costs (0x009C8E78), 2 enough ("PRESS START")
    s.freePlay = freePlay ? 1 : 0;
    s.continueCount = h.continueShown;  // as the CONTINUE screen task drew it this frame (-1: not drawn)
    for (int p = 0; p < 2; ++p) {
        const auto& w = h.player[p];
        auto& d = s.player[p];
        d.state = static_cast<int16_t>(w.state);
        d.lives = static_cast<int16_t>(w.lives);
        d.aimX = w.aim[0];
        d.aimY = w.aim[1];
        d.ammo = static_cast<int16_t>(w.ammo);
        d.empty = w.empty ? 1 : 0;
        d.reloadTimer = static_cast<int16_t>(w.reloadBlink);
        d.inputKind = static_cast<int8_t>(w.inputKind);
        d.crosshairStyle = static_cast<int8_t>(options.crosshair[p]);
        d.gunValid = w.gunValid ? 1 : 0;
        d.gunFired = w.fired ? 1 : 0;
        d.deviceMode = in[p].deviceMode;
        d.gunPosX = in[p].x;
        d.gunPosY = in[p].y;
        d.credits = static_cast<uint32_t>(h.credits);
        d.coinState = freePlay ? 2 : h.credits == 0 ? 0 : 2;  // (a start costs 0x009C8E78 = 1: state 1 never shows)
    }
    return s;
}

std::vector<game::SpriteDef> HudSprites(game::HudState& s, const game::WorldHud& h, bool showCrosshairs) {
    std::vector<game::SpriteDef> out;
    if (h.paused) return out;  // Game_UpdatePause 0x0040E500 skips Game_UpdateMode: no task runs
    auto add = [&](const std::vector<game::SpriteDef>& v) { out.insert(out.end(), v.begin(), v.end()); };
    for (int p = 0; p < 2; ++p) {
        const int state = s.player[p].state;
        if (state == 5)
            add(play::PlayerHudSprites(s, p, showCrosshairs));
        else if (state == 9 && !(h.gameMode == 6 && (h.flags5900 & 0x18) == 0x18))
            add(game::HudIdlePlayer(s, p));
        else if (state == 4)
            add(game::HudCreditLine(s, p));
        else if (state == 10)
            add(game::HudWaitPlayer(s, p));
        else if (state == 6)
            add(game::HudOutPlayer(s, p));
    }
    // CONTINUE screen 0x00460530: only on the frames that task ran (mode-6 sub-state 4). 0x009A2BB8 keeps its value
    // after a continue, so gating on the countdown left "CONTINUE? n" on screen for the rest of the game.
    if (s.continueCount >= 0) add(game::ContinueScreen(s));
    // per-player CONTINUE (0x00414280 -> 0x00416880 / 0x004168C0, retained layer 2: drawn at the end of the frame,
    // head insertion: the later player task's sprites first)
    for (int p = 1; p >= 0; --p)
        if (h.player[p].continueShown >= 0) add(game::ContinuePlayer(p, h.player[p].continueShown));
    return out;
}

// The world's own 2D in game mode 7 / pause (notes/agents/twoplayer.md): route map tiles, "GAME OVER" sprite effects
// (0x00499F00: rot 0, blend -1, flags 0xA), and "PAUSE" (0x0040E500: 0x8E7 / 0x8E8 at (160 / 480, 240), depth 0.6,
// flags 0x70A). The pause overlay model 0x93E (alpha 0.5) is not drawn.
game::SubtitleTables g_subtitles;            // loaded in main
std::vector<game::SpriteDef> g_cardSprites;  // the stage title card of the last game frame

// The stage title card (actor 0x60): one ChapterCard::Run() per game frame the world's card task ran
void StepTitleCard(const game::WorldHud& h) {
    static game::ChapterCard card;
    static uint32_t serial = 0;
    g_cardSprites.clear();
    if (h.titleSerial == 0) return;
    if (h.titleSerial != serial) card = game::ChapterCard::Start(h.titleChapter);
    serial = h.titleSerial;
    g_cardSprites = card.Run(0);
}

std::vector<game::SpriteDef> WorldSprites(const game::WorldHud& h, bool includeTitleCard = true) {
    std::vector<game::SpriteDef> out;
    if (includeTitleCard) out.insert(out.end(), g_cardSprites.begin(), g_cardSprites.end());
    for (const auto& l : h.subtitles) {  // voice-line subtitles (task 0x00435AA0, glyphs 0x00436850)
        const auto v = game::SubtitleSprites(g_subtitles, l.group, l.line, 0);
        out.insert(out.end(), v.begin(), v.end());
    }
    if (h.routeMapScroll >= 0) {
        const auto map = game::RouteMapBackground(h.routeMapScroll);
        out.insert(out.end(), map.begin(), map.end());
    }
    for (const auto& e : h.effects) {
        game::SpriteDef d;
        d.id = e.sprite;
        d.x = e.x;
        d.y = e.y;
        d.depth = 1.0f;
        d.sx = e.sx;
        d.sy = e.sy;
        d.alpha = e.alpha;
        d.blend = -1;
        d.flags = 0xA;
        out.push_back(d);
    }
    out.insert(out.end(), h.sprites.begin(), h.sprites.end());  // boss bar / name card / stage-clear screen
    if (h.paused) {  // (not drawn while play.exe's pause menu is up: that frame is the menu alone)
        const auto label = game::HudPause(h.pausePlayer);
        out.insert(out.end(), label.begin(), label.end());
    }
    return out;
}

}  // namespace

// Without a game directory argument (e.g. started by double-click): the project folder is found by walking up from
// the executable to a directory with game\Hod2.exe; it becomes the working directory (scenes\ are relative to it).
std::string FindGameDir(const char* argv0) {
    std::error_code ec;
    std::filesystem::path p = std::filesystem::absolute(argv0, ec).parent_path();
    for (int up = 0; up < 8 && !p.empty(); ++up, p = p.parent_path()) {
        if (std::filesystem::exists(p / "game" / "Hod2.exe", ec)) {
            std::filesystem::current_path(p, ec);
            return "game";
        }
        if (p == p.parent_path()) break;
    }
    return {};
}

int main(int argc, char** argv) {
#ifdef PLAY_GUI
    // Recover before loading optional runtime DLLs or touching save/settings files.
    std::string recoveryError;
    const auto recovery = port::update::RecoverInterruptedUpdate(&recoveryError);
    if (recovery == port::update::Recovery::Started) return 0;
    if (recovery == port::update::Recovery::Failed) {
        MessageBoxA(nullptr, ("The interrupted update needs to be restored before the game can start.\n\n" + recoveryError +
            "\n\nYour saves and settings have been kept. Please try launching the game again.").c_str(),
            "HOTD2RE - Update recovery", MB_OK | MB_ICONERROR);
        return 1;
    }
#endif
#if defined(_WIN32) && defined(PLAY_GUI)
    // hotd2.exe (GUI subsystem): no console of its own. Started from a terminal, print there unless the output is
    // already redirected (a file or pipe handed over by the caller).
    if (GetFileType(GetStdHandle(STD_ERROR_HANDLE)) == FILE_TYPE_UNKNOWN && AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
#endif
    std::string dir;
    int firstOpt = 2;
#ifdef PLAY_GUI
    bool audio = true;  // Player launches keep sound enabled with custom game paths and options.
#else
    bool audio = false;  // Command-line tools opt in with --audio, apart from normal no-path launches below.
#endif
    if (argc < 2 || argv[1][0] == '-') {
        dir = FindGameDir(argv[0]);
        firstOpt = 1;
        audio = audio || argc < 2 || (argc == 2 && std::string(argv[1]) == "--no-update-check");
        if (dir.empty()) {
            fprintf(stderr,
                    "usage: play <game dir> [--audio] [--width W --height H] [--script file --screenshot out.png --frame F] %s\n"
                    "(no game folder found next to or above the executable)\n",
                    platform::CommonUsage());
#ifdef _WIN32
            MessageBoxA(nullptr, "Cannot find the game folder (game\\Hod2.exe) above play.exe.\nStart it as: play.exe <game dir>",
                        "hotd2-re play", MB_OK | MB_ICONERROR);
#endif
            return 2;
        }
    } else {
        dir = argv[1];
    }
    int width = 960, height = 720, shotFrame = -1, runFrames = 0, shotEvery = 0, shotFrom = 0, shotPhase = 0;
    int captureRenderWidth = 0, captureRenderHeight = 0;
    bool captureRenderSizeGiven = false;
    uint32_t seed = 902;
    uint32_t firstSerial = 0x10000;  // the play captures: 65551 (history-dependent)
    int credits = -1;                // -1: free play; else that many credits (the play captures: 5)
    bool direct = false;             // --direct: start a game at once (no boot / title / mode select)
    std::string configPath;          // --config: a mid-game stage start (WorldConfig + carry from world_test --save-config)
    std::string settingsOverride;    // explicit isolated settings file, also usable by screenshot comparisons
    int p2mode = 6;  // --p2mode M: P2's device mode for --script (the hook's captures: 0, calibration path; world_test --p2mode)
    std::string scriptPath, screenshot, scenePath = "scenes/st1_play.scene", savePath = "hotd2re_save.bin";
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> savePatch;
    bool trace = false;  // --trace: one line per game mode / sub-state change and the world's notes (headless checks)
    std::string bloodOverride;
    bool noHud = false;        // --no-hud: 3D only (clean screenshots)
    bool pace = false;         // --pace: a headless run steps at 60 Hz instead of as fast as it can (netplay tests)
    bool checkUpdates = true;  // --no-update-check: skip the launch-time release check
    int dlssQuality = -1;
    int temporalVendor = 1, fsrMajor = 3;
    std::string dlssDirectory;
    int frameGeneration = 0;
    bool rayShadows = false;
    std::string frameGenerationDirectory, frameGenerationCaptures;
    // Online two-player (net/netplay.h): --host [port] waits for a joiner on that UDP port (default 27015), --join
    // <address[:port]> connects to a host; the host is P1. --net-delay N: input delay in frames (host; default 3).
    // --code: codes instead of addresses (both sides show their code and enter the other's; --peer <code> gives it on
    // the command line, else it is read from the console); --port N: the joiner's own UDP port (any by default).
    bool netHostMode = false, netJoinMode = false, netCode = false;
    std::string netJoin, netPeerCode, netLobby;  // --lobby host:port: the lobby server
    int netPort = 27015, netDelay = 3, netBindPort = 0;
    bool netDelayGiven = false;
    platform::CommonOptions common;
    for (int i = firstOpt; i < argc; ++i) {
        const std::string a = argv[i];
        if (platform::ParseCommonArg(argc, argv, i, &common)) continue;
        if (a == "--audio")
            audio = true;
        else if (a == "--width" && i + 1 < argc)
            width = atoi(argv[++i]);
        else if (a == "--height" && i + 1 < argc)
            height = atoi(argv[++i]);
        else if (a == "--render-width" && i + 1 < argc) {
            captureRenderWidth = atoi(argv[++i]);
            captureRenderSizeGiven = true;
        } else if (a == "--render-height" && i + 1 < argc) {
            captureRenderHeight = atoi(argv[++i]);
            captureRenderSizeGiven = true;
        }
        else if (a == "--script" && i + 1 < argc)
            scriptPath = argv[++i];
        else if (a == "--screenshot" && i + 1 < argc)
            screenshot = argv[++i];
        else if (a == "--frame" && i + 1 < argc)
            shotFrame = atoi(argv[++i]);
        else if (a == "--every" && i + 1 < argc)
            shotEvery = atoi(argv[++i]);  // with a %d in --screenshot: every N frames up to --frame
        else if (a == "--every-from" && i + 1 < argc)
            shotFrom = atoi(argv[++i]);  // --every: no shots before frame F
        else if (a == "--every-phase" && i + 1 < argc)
            shotPhase = atoi(argv[++i]);  // --every: frames with frame % N == P
        else if (a == "--scene" && i + 1 < argc)
            scenePath = argv[++i];
        else if (a == "--seed" && i + 1 < argc)
            seed = static_cast<uint32_t>(atoi(argv[++i]));
        else if (a == "--run-frames" && i + 1 < argc)
            runFrames = atoi(argv[++i]);  // interactive smoke test: quit after N game frames
        else if (a == "--serial" && i + 1 < argc)
            firstSerial = static_cast<uint32_t>(atoi(argv[++i]));
        else if (a == "--credits" && i + 1 < argc)
            credits = atoi(argv[++i]);
        else if (a == "--direct")
            direct = true;
        else if (a == "--config" && i + 1 < argc)
            configPath = argv[++i];  // world_test --save-config: start at that stage
        else if (a == "--settings" && i + 1 < argc)
            settingsOverride = argv[++i];
        else if ((a == "--dlss-sdk" || a == "--fsr-sdk" || a == "--xess-sdk") && i + 1 < argc)
            dlssDirectory = argv[++i];
        else if (a == "--frame-generation" || a == "--dlss-fg" || a == "--fsr-fg") {
            frameGeneration = a == "--fsr-fg" ? 2 : 1;
            common.renderer = platform::Renderer::D3D12;
        } else if (a == "--ray-shadows") {
            rayShadows = true;
            common.renderer = platform::Renderer::D3D12;
        } else if (a == "--frame-generation-sdk" && i + 1 < argc)
            frameGenerationDirectory = argv[++i];
        else if (a == "--frame-generation-captures" && i + 1 < argc)
            frameGenerationCaptures = argv[++i];
        else if (a == "--fsr-version" && i + 1 < argc) {
            fsrMajor = atoi(argv[++i]);
            if (fsrMajor < 2 || fsrMajor > 4) {
                std::fprintf(stderr, "--fsr-version expects 2, 3 or 4\n");
                return 2;
            }
        } else if ((a == "--dlss" || a == "--fsr" || a == "--xess") && i + 1 < argc) {
            temporalVendor = a == "--xess" ? 3 : a == "--fsr" ? 2 : 1;
            const std::string quality = argv[++i];
            dlssQuality = quality == "quality"                          ? 0
                          : quality == "balanced"                       ? 1
                          : quality == "performance"                    ? 2
                          : quality == "dlaa" || quality == "native-aa" ? 3
                                                                        : -1;
            if (dlssQuality < 0) {
                std::fprintf(stderr, "Temporal upscale expects quality, balanced, performance or native-aa\n");
                return 2;
            }
            common.renderer = platform::Renderer::D3D12;
        } else if (a == "--p2mode" && i + 1 < argc)
            p2mode = atoi(argv[++i]);
        else if (a == "--save" && i + 1 < argc)
            savePath = argv[++i];  // the port's own save file (headless: none unless given)
        else if (a == "--trace")
            trace = true;
        else if (a == "--blood" && i + 1 < argc)
            bloodOverride = argv[++i];
        else if (a == "--no-hud")
            noHud = true;  // screenshots: the 3D scene only (no HUD / world 2D / bitmap screens)
        else if (a == "--no-update-check")
            checkUpdates = false;
        else if (a == "--host") {
            netHostMode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') netPort = atoi(argv[++i]);
        } else if (a == "--join") {
            netJoinMode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                netJoin = argv[++i];
                if (const size_t c = netJoin.rfind(':'); c != std::string::npos) {
                    netPort = atoi(netJoin.c_str() + c + 1);
                    netJoin.resize(c);
                }
            }
        } else if (a == "--code")
            netCode = true;
        else if (a == "--pace")
            pace = true;
        else if (a == "--peer" && i + 1 < argc)
            netPeerCode = argv[++i];
        else if (a == "--lobby" && i + 1 < argc)
            netLobby = argv[++i];
        else if (a == "--port" && i + 1 < argc)
            netBindPort = atoi(argv[++i]);
        else if (a == "--net-delay" && i + 1 < argc) {  // pins the input delay (else measured from the round trip, at least 2)
            netDelay = std::clamp(atoi(argv[++i]), 1, 30);
            netDelayGiven = true;
        } else if (a == "--save-patch" && i + 1 < argc) {  // <va>:<hex>[,...] into the loaded save block (tests, as frontend_test)
            for (std::string s = argv[++i]; !s.empty();) {
                const size_t c = s.find(','), colon = s.find(':');
                const std::string one = s.substr(0, c);
                if (colon != std::string::npos && colon < one.size()) {
                    std::vector<uint8_t> b;
                    const std::string hex = one.substr(colon + 1);
                    for (size_t k = 0; k + 1 < hex.size(); k += 2)
                        b.push_back(static_cast<uint8_t>(std::stoul(hex.substr(k, 2), nullptr, 16)));
                    savePatch.emplace_back(static_cast<uint32_t>(std::stoul(one.substr(0, colon), nullptr, 16)), b);
                }
                s = c == std::string::npos ? std::string() : s.substr(c + 1);
            }
        }
    }
    // Diagnostic captures can hold output size fixed while varying the actual
    // scene target. Ordinary screenshot and interactive sizing stays unchanged.
    if (captureRenderSizeGiven && (screenshot.empty() || !port::ValidHudOutputSize(captureRenderWidth, captureRenderHeight))) {
        std::fprintf(stderr, "--render-width and --render-height require a screenshot and a valid paired render size\n");
        return 2;
    }
    // The port's settings (volumes, display, and "input=" for the gun source: a touchpad's clicks are synthesized by
    // Windows and never reach raw input, so such players set input=mouse). --input / --width / --height / --renderer on
    // the command line win for this run. Interactive runs only: headless screenshots keep the defaults (1x, no filter,
    // the --width x --height target as before).
    play::Settings volumes;
    const std::string settingsPath = settingsOverride.empty() ? "hotd2re_settings.ini" : settingsOverride;
    bool sizeGiven = false;
    for (int k = firstOpt; k < argc; ++k) sizeGiven = sizeGiven || std::string(argv[k]) == "--width" || std::string(argv[k]) == "--height";
    if ((screenshot.empty() || !settingsOverride.empty()) && volumes.Load(settingsPath) && !volumes.input.empty()) {
        bool inputGiven = false;
        for (int k = firstOpt; k < argc; ++k) inputGiven = inputGiven || std::string(argv[k]) == "--input";
        if (!inputGiven && !platform::ParseInput(volumes.input, &common.input))
            fprintf(stderr, "%s: unknown input \"%s\" (auto | raw | mouse | touch)\n", settingsPath.c_str(), volumes.input.c_str());
    }
    // Menu style is a next-launch choice. Keep World, background textures and
    // port overlays on one theme for this entire process, including local restarts.
    const bool activeNightShift = volumes.display.nightShift;
    bool rendererGiven = false;
    for (int k = firstOpt; k < argc; ++k) rendererGiven |= std::string(argv[k]) == "--renderer";
    const bool explicitRuntimeRenderer = rendererGiven || dlssQuality >= 0 || frameGeneration || rayShadows;
    if (!rendererGiven && dlssQuality < 0 && !frameGeneration && !rayShadows) {
        common.renderer = volumes.display.renderer == 2   ? platform::Renderer::D3D12
                          : volumes.display.renderer == 1 ? platform::Renderer::SdlGpu
                                                          : platform::Renderer::D3D11;
        if (volumes.display.temporalUpscaler) {
            dlssQuality = volumes.display.temporalQuality;
            temporalVendor = volumes.display.temporalUpscaler == 4 ? 3 : volumes.display.temporalUpscaler >= 2 ? 2 : 1;
            fsrMajor = volumes.display.temporalUpscaler == 2 ? 2 : 3;
            common.renderer = platform::Renderer::D3D12;
        }
        frameGeneration = volumes.display.frameGeneration;
        if (frameGeneration) common.renderer = platform::Renderer::D3D12;
        rayShadows = volumes.display.rayShadows;
        if (rayShadows) common.renderer = platform::Renderer::D3D12;
    }
    if (screenshot.empty() && !sizeGiven && volumes.display.windowScale > 0) {  // the window scale setting (fit: sized after Init)
        width = 640 * volumes.display.windowScale;
        height = 480 * volumes.display.windowScale;
    }
    if (!volumes.assets.empty()) port::SetAssetDir(volumes.assets);  // optional asset pack (assets= line; HOTD2_ASSET_DIR wins)
    if (!bloodOverride.empty()) {
        if (bloodOverride != "red" && bloodOverride != "green") {
            fprintf(stderr, "--blood must be red or green\n");
            return 1;
        }
        volumes.display.redBlood = bloodOverride == "red";
    }
    static port::AssetStore assets;
    static port::ModelRenderer renderer;
    port::SceneDef def;
    port::Scene scene;
    std::string err;
    if (!assets.Open(dir, &err) || !def.Load(scenePath, &err) || !scene.Init(assets, def, &err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    assets.SetRedBlood(volumes.display.redBlood);
    g_subtitles.Load(assets.Exe());
    if (!port::AssetDir().empty() && screenshot.empty()) {
        // Asset pack: the front end's textures decode during the boot logos (10 s of idle): the title flythrough's
        // section objects (the title tables 0x00576AEC / 0x00576B04, TitleFrame), the title and menu sprite files.
        const game::ExeImage& exe = assets.Exe();
        for (int i = 0; i < 12; ++i) {
            const int o = exe.S16(0x00576AEC + static_cast<uint32_t>(i * 2));
            if (o < 0) break;
            const int modelId = exe.S16(0x00576B04 + static_cast<uint32_t>(o * 4));
            uint16_t file = 0;
            if (modelId > 0 && exe.Read(0x004E83B4 + static_cast<uint32_t>(modelId) * 2, &file, 2))
                assets.PrefetchPolFile(exe.CStr(exe.U32(0x004D0EF4 + static_cast<uint32_t>(file) * 4)));
        }
        for (const char* f : {"scr_title.bin", "scr_etc_1.bin", "scr_common.bin", "scr_opt_moji05.bin", "common.bin"})
            assets.PrefetchTexFile(f);
    }
    game::WorldConfig loaded;  // --config: read first (the World loads that stage's table)
    if (!configPath.empty() && !game::LoadWorldConfig(configPath, &loaded, &err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    game::World world;
    if (!world.Load(dir, configPath.empty() ? 0 : loaded.eventMode, &err)) {
        fprintf(stderr, "world: %s\n", err.c_str());
        return 1;
    }
    game::WorldConfig cfg;
    cfg.eventMode = 0;  // st1evtbl.bin
    cfg.scene = 0;      // 0x009A2BC0 at the start of a game (play captures)
    cfg.gameMode = 6;
    cfg.playerState[0] = 0;  // P1 pressed start
    cfg.playerState[1] = 9;
    // 0x009A32A0 (frames since boot; any value gives a valid game). Defaults = the play captures (game start at boot
    // frame 901, seed counter 902), so --script replays match them; --seed overrides.
    cfg.seedCounter = seed;
    cfg.firstSerial = firstSerial;
    if (credits >= 0) {
        cfg.freePlay = false;
        cfg.credits = credits;
    }
    cfg.startFrame = 901;
    cfg.gameOver = true;        // run the game-over screen and route map (mode 7) to the title, then restart
    if (!configPath.empty()) {  // frames, seed, serial, players and carry as the capture's stage start (direct start)
        cfg = loaded;
        cfg.gameOver = true;
        direct = true;
    }
    const std::vector<ScriptLine> script = scriptPath.empty() ? std::vector<ScriptLine>() : LoadScript(scriptPath);
    size_t scriptPos = 0;
    // Test-only keyboard edges through the same menu/input path as real keys (frame, held key bits).
    std::vector<std::pair<int, uint32_t>> keyScript;
    size_t keyScriptPos = 0;
    if (const char* path = getenv("HOTD2_PLAY_KEYS")) {
        std::ifstream f(path);
        int frame;
        uint32_t bits;
        while (f >> frame >> bits) keyScript.emplace_back(frame, bits);
    }
    // Netplay (set up below, once the window exists): the local player's input is captured here and applied Delay()
    // steps later together with the other side's; a script then only drives the local player.
    bool netplay = netHostMode || netJoinMode;           // (also turned on by the in-game lobby when it connects)
    if (netJoinMode && netJoin.empty()) netCode = true;  // --join without an address: codes
    port::net::Session net;
    port::net::Session homeDirectory;
    homeDirectory.SetBuild(kBuildId);
    bool homeDirectoryOpen = false;
    double homeDirectoryRefresh = 0;
    std::vector<port::net::LobbyRoom> homeRooms;
    net.SetBuild(kBuildId);
    net.SetDelayPolicy(netDelayGiven ? netDelay : 2, !netDelayGiven);  // measured from the round trip unless --net-delay pins it
    Lobby lobby;
    port::net::LeaderboardClient rankings;
    {
        // the lobby server: --lobby, HOTD2_LOBBY, the settings' lobby=, else the default (server/lobby/README.md)
        std::string lobbyAddr =
            !netLobby.empty() ? netLobby
                              : (getenv("HOTD2_LOBBY") ? getenv("HOTD2_LOBBY") : (!volumes.lobby.empty() ? volumes.lobby : kDefaultLobby));
        uint16_t lobbyPort = 27020;
        if (const size_t c = lobbyAddr.rfind(':'); c != std::string::npos) {
            lobbyPort = static_cast<uint16_t>(atoi(lobbyAddr.c_str() + c + 1));
            lobbyAddr.resize(c);
        }
        if (!net.SetLobbyServer(lobbyAddr, lobbyPort, &err)) fprintf(stderr, "netplay: %s (the in-game lobby is off)\n", err.c_str());
        homeDirectory.SetLobbyServer(lobbyAddr, lobbyPort, nullptr);
        if (!rankings.Configure(lobbyAddr, lobbyPort, port::net::kLeaderboardCompatibilityId, &err)) fprintf(stderr, "rankings: %s\n", err.c_str());
    }
    int netLocal = netHostMode ? 0 : 1, netRemote = netHostMode ? 1 : 0;
    // Player identity belongs to the game, not to the connection. A surviving joiner must keep P2.
    bool netPlayerAssigned = netplay;
    auto deviceForPlayer = [&](int p) { return netPlayerAssigned ? (p == netLocal ? 0 : -1) : p; };
    auto keyboardPlayer = [&] { return netPlayerAssigned ? netLocal : 0; };
    int netVolumes[5] = {-1, -1, -1, 5, 5};  // joiner: the host's Options-screen volume rows (world state)
    bool netVolumesSet = false;
    uint8_t netCmd = 0;  // kCmd* for the next pushed input (the pause menu's Return to Main Menu)
    long long netStalls = 0;
    bool netDesyncReported = false;
    uint32_t blink = 0;
    uint8_t scriptButtons[2] = {}, prevButtons[2] = {};
    float scriptX[2] = {0.5f, 0.5f}, scriptY[2] = {0.5f, 0.5f};
    bool scriptJoined[2] = {};
    // Script input of frame fr (as world_test / the hook's ApplyScript): every line up to fr applies; several lines in
    // one frame OR their button edges.
    game::WorldInput in[2];
    auto scriptFrame = [&](int fr) {
        uint8_t pressedNow[2] = {};
        while (scriptPos < script.size() && script[scriptPos].frame <= fr) {
            const ScriptLine& s = script[scriptPos++];
            if (netPlayerAssigned && s.player != netLocal) continue;  // remote input is never a local device, even after disconnect
            pressedNow[s.player] |= static_cast<uint8_t>(s.buttons & ~scriptButtons[s.player]);
            scriptX[s.player] = s.x;
            scriptY[s.player] = s.y;
            scriptButtons[s.player] = static_cast<uint8_t>(s.buttons);
            scriptJoined[s.player] = true;
        }
        for (int q = 0; q < 2; ++q)
            if (scriptJoined[q] && deviceForPlayer(q) >= 0)
                in[q] =
                    game::MakeWorldInput(scriptX[q], scriptY[q], scriptButtons[q], static_cast<uint8_t>(scriptButtons[q] & ~pressedNow[q]));
        if (scriptJoined[1] && deviceForPlayer(1) >= 0) in[1].deviceMode = p2mode;
    };
    auto startGame = [&] {
        scriptPos = 0;
        if (!script.empty()) {  // the Stage_Start frame reads input too
            scriptFrame(direct ? cfg.startFrame : 0);
            for (int q = 0; q < 2; ++q) world.SetInput(q, in[q]);
        }
        if (direct) {
            cfg.portStageResults = screenshot.empty() || getenv("HOTD2_PLAY_MENU") || getenv("HOTD2_STAGE_RESULTS");
            return world.StartStage(cfg, &err);
        }
        game::WorldConfig boot;  // World::Boot: frame 0; playable menus load saved rules, native captures use explicit defaults.
        boot.creditSetting = credits >= 0 ? credits - 1 : -1;
        boot.startFrame = 0;
        boot.gameOver = true;
        boot.portStageResults = getenv("HOTD2_STAGE_RESULTS") != nullptr;  // capture the page with original-menu replay inputs
        if (screenshot.empty() ||
            (getenv("HOTD2_PLAY_MENU") && !getenv("HOTD2_PLAY_MENU_NATIVE_BOOT"))) {  // native boot for matched replay captures
            boot.portVolumes[0] = netVolumesSet ? netVolumes[0] : volumes.music;      // the options screen's volume rows
            boot.portVolumes[1] = netVolumesSet ? netVolumes[1] : volumes.guns;       // (netplay joiner: the host's)
            boot.portVolumes[2] = netVolumesSet ? netVolumes[2] : volumes.sfx;
            boot.portVolumes[3] = netVolumesSet ? netVolumes[3] : volumes.master;
            boot.portVolumes[4] = netVolumesSet ? netVolumes[4] : volumes.attraction;
            boot.portFeatures = true;  // back button on the item select, Scene Select
            boot.portModernMenu = activeNightShift;
            boot.portStageResults = true;
        }
        // Playable sessions use the saved gameplay rules from the first run after launch.
        // Native replay captures keep their explicit WorldConfig settings.
        boot.applySaveSettings = boot.portFeatures;
        // the port's own save (rankings, grades, items, options); the original's files are only read when it is absent
        bool saveGiven = false;
        for (int k = firstOpt; k < argc; ++k) saveGiven = saveGiven || std::string(argv[k]) == "--save";
        if (screenshot.empty() || saveGiven) boot.savePath = savePath;
        boot.savePatch = savePatch;
        world.Boot(boot);
        return true;
    };
    auto startNetworkGame = [&] {
        const auto& c = net.Config();
        // Replays must start from identical persistent data, independent of either machine's menu history.
        const auto scratchSave =
            std::filesystem::temp_directory_path() / ("hotd2-network-" + std::to_string(GetCurrentProcessId()) + ".bin");
        {
            std::ofstream sf(scratchSave, std::ios::binary | std::ios::trunc);
            sf.write(reinterpret_cast<const char*>(c.save.data()), c.save.size());
            if (!sf) {
                err = "cannot prepare the network save";
                return false;
            }
        }
        game::WorldConfig g;
        g.eventMode = 0;
        g.scene = 0;
        g.gameMode = 6;
        g.gameOver = true;
        g.seedCounter = c.seed;
        g.firstSerial = c.firstSerial;
        g.startFrame = static_cast<int>(c.startFrame);
        g.freePlay = c.credits < 0;
        g.credits = c.credits < 0 ? 0 : c.credits;
        g.creditSetting = c.credits < 0 ? -1 : c.credits - 1;
        g.playerState[0] = g.playerState[1] = 0;
        g.portFeatures = (c.flags & 1) != 0;
        g.portModernMenu = (c.flags & 2) != 0;
        g.portNetworkRoom = (c.flags & 4) != 0;
        g.applySaveSettings = g.portFeatures;
        g.portStageResults = true;
        for (int k = 0; k < 5; ++k) g.portVolumes[k] = c.portVolumes[k];
        g.savePath = scratchSave.string();
        if (!world.StartNetworkGame(g, &err)) return false;
        keyScriptPos = 0;
        g_keys = 0;
        scriptPos = 0;
        while (scriptPos < script.size() && script[scriptPos].frame <= static_cast<int>(c.startFrame)) ++scriptPos;
        for (int p = 0; p < 2; ++p) {
            in[p] = game::WorldInput{};
            scriptButtons[p] = prevButtons[p] = 0;
            scriptJoined[p] = false;
        }
        return true;
    };
    port::FrontScreens screens(dir);
    // the modern main menu's background (the baked labels in-painted out of the tiles): with the port's features only
    if (screenshot.empty() || (getenv("HOTD2_PLAY_MENU") && !getenv("HOTD2_PLAY_MENU_NATIVE_BOOT")))
        screens.SetModernMenu(activeNightShift, true);
    if (screenshot.empty() && !port::AssetDir().empty())  // the mode select's six tiles (the pack's are 1024 x 1024 PNGs)
        for (const char* t : {"tex\\hod2title00.bin", "tex\\hod2title01.bin", "tex\\hod2title02.bin", "tex\\hod2title03.bin",
                              "tex\\hod2title10.bin", "tex\\hod2title11.bin"})
            screens.Warm(t);

    // Platform: window + gun source (none for headless screenshots), GPU device (swap chain or off-screen target).
    const bool headless = !screenshot.empty();
    platform::Platform plat;
    platform::Options po;
    po.title = std::string(netplay ? (netHostMode ? "hotd2-re play (host, P1)" : "hotd2-re play (joined, P2)") : "hotd2-re play") + " - " +
               kBuildName;
    po.width = width;
    po.height = height;
    po.window = !headless;
    po.hidden = true;  // apply saved fullscreen before SDL exposes the initial decorated window
    po.input = common.input;
    po.guns = true;
    if (!plat.Init(po, &err)) {
        fprintf(stderr, "platform: %s\n", err.c_str());
        return 1;
    }
    // Display settings (notes/agents/display.md): the frame is rendered at the internal resolution (Render Scale x
    // 640x480; headless: the --width x --height target as before) and presented into the window's image rectangle
    // (Aspect: 4:3 with bars, or stretched) with the CRT filter; the window is sized by Window Scale (1x..3x, Fit = the
    // usable desktop) or fullscreen (borderless desktop). The gun aim is mapped over the image rectangle.
    auto fitWindow = [&] {  // Window Scale "Fit": the largest 4:3 client area of the display
        int dw, dh;
        if (!plat.UsableDisplaySize(&dw, &dh)) return;
        const input::ImageRect r = input::FitImage(dw, dh, input::Aspect::Pillarbox43);
        plat.SetWindowSize(r.w, r.h);
    };
    if (!headless) {
        if (volumes.display.monitor > 0 && !plat.MoveToDisplay(volumes.display.monitor - 1)) volumes.display.monitor = 0;
        if (!sizeGiven && volumes.display.windowScale == 0) fitWindow();
        if (volumes.display.fullscreen && !plat.SetFullscreen(true, volumes.display.exclusiveFullscreen)) {
            volumes.display.fullscreen = false;
            fprintf(stderr, "Fullscreen is unavailable; using a window\n");
        }
    }
    // Showing applies SDL's pending fullscreen state without exposing the windowed frame. Do this before sizing
    // the renderer: hidden windows still report their windowed dimensions and SDL_GPU cannot acquire their swap chain.
    if (!plat.ShowWindow(&err)) {
        fprintf(stderr, "platform: %s\n", err.c_str());
        return 1;
    }
    int initialWindowW = width, initialWindowH = height;
    if (!headless) plat.ClientSize(&initialWindowW, &initialWindowH);
    float viewportAspect = play::DisplayViewportAspect(volumes.display.viewport, initialWindowW, initialWindowH);
    const auto initialImage =
        play::DisplayImageRect(initialWindowW, initialWindowH, viewportAspect, volumes.display.stretch, volumes.display.integerScale);
    const int authoredH = headless ? (captureRenderSizeGiven ? captureRenderHeight : height) : 480 * volumes.display.renderScale;
    const int authoredW = headless ? (captureRenderSizeGiven ? captureRenderWidth : width)
                                  : static_cast<int>(std::lround(authoredH * viewportAspect));
    const auto initialSize = port::ChooseOutputResolution(static_cast<port::OutputResolution>(volumes.display.resolutionMode), authoredW,
                                                          authoredH, initialImage.w, initialImage.h);
    const int renderH = initialSize.height, renderW = initialSize.width;
    std::string displayNotice;
    bool startupRendererFallback = false;
    auto gpu = play::CreateRequestedRenderer(
        common.renderer, platform::DefaultRenderer(), !explicitRuntimeRenderer,
        [&](platform::Renderer choice, std::string* failure) { return plat.CreateGpu(choice, renderW, renderH, false, 0, failure); },
        &startupRendererFallback, &err, &displayNotice);
    if (!gpu || !renderer.Init(gpu.get(), &err)) {
        fprintf(stderr, "renderer: %s\n", err.c_str());
        return 1;
    }
    const std::string startupRendererNotice = displayNotice;
    if (startupRendererFallback) {
        common.renderer = platform::DefaultRenderer();
        dlssQuality = -1;
        frameGeneration = 0;
        rayShadows = false;
    }
    if (plat.CurrentInput() == platform::Input::RawInput) volumes.display.graphics.crtCurvature = 0;
    const auto compatible = SupportedGraphicsOptions(volumes.display.graphics, gpu->GetGraphicsCapabilities());
    if (compatible != volumes.display.graphics) {
        volumes.display.graphics = compatible;
        displayNotice = "Unsupported options are disabled on the active renderer. Other image settings are retained.";
    }
    if (headless) {
        volumes.display.graphics.syncInterval = 0;
        volumes.display.graphics.allowTearing = false;
        volumes.display.graphics.lowLatency = false;
    }
    if (!gpu->ConfigureGraphics(volumes.display.graphics, &err)) {
        displayNotice = err;
        fprintf(stderr, "Graphics settings: %s; previous settings retained\n", err.c_str());
    }
    volumes.display.graphics = gpu->EffectiveGraphics();
    auto warmGraphics = [&] {
        if (!gpu->GetGraphicsCapabilities().pipelineWarmup) return;
        port::GraphicsWarmupStats stats;
        std::string warmError;
        if (!gpu->WarmupGraphics(&stats, &warmError)) {
            displayNotice = "Graphics warmup: " + warmError;
            return;
        }
        if (trace)
            std::printf("Graphics warmup: %u pipelines, %u states, %u samplers created in %.2f CPU ms\n", stats.pipelinesCreated,
                        stats.stateObjectsCreated, stats.samplersCreated, stats.cpuMilliseconds);
    };
    warmGraphics();
    scene.SetViewportAspect(viewportAspect);
    bool rayAvailable = false, rayActive = false;
#ifdef _WIN32
    if (rayShadows && !port::ConfigureNativeD3D12Raytracing(*gpu, true, &displayNotice))
        std::fprintf(stderr, "Ray shadows unavailable: %s\n", displayNotice.c_str());
    const auto rayInfo = port::GetNativeD3D12RaytracingInfo(*gpu);
    rayAvailable = rayInfo.supported && rayInfo.compilerAvailable;
    rayActive = rayInfo.enabled;
    if (!startupRendererFallback) volumes.display.rayShadows = rayActive;
    DlssRuntime dlss;
    FrameGenerationRuntime generatedFrames;
    if (dlssQuality >= 0) {
        dlss.quality = dlssQuality;
        dlss.family = temporalVendor;
        if (dlssDirectory.empty()) dlssDirectory = std::filesystem::absolute(argv[0]).parent_path().string();
        int iw = 0, ih = 0;
        auto* nativeDevice = port::GetNativeD3D12Device(*gpu);
        const bool initialized = temporalVendor == 3   ? dlss.xess.Initialize(nativeDevice, dlssDirectory, &displayNotice)
                                 : temporalVendor == 2 ? dlss.fsr.Initialize(nativeDevice, dlssDirectory, fsrMajor, &displayNotice)
                                                       : dlss.processor.Initialize(nativeDevice, dlssDirectory, &displayNotice);
        if (initialized && dlss.Size(renderW, renderH, &iw, &ih, &displayNotice) && gpu->ResizeTarget(iw, ih)) {
            dlss.active = port::SetNativeD3D12FrameCallback(*gpu, DlssRuntime::Process, &dlss);
            std::printf("%s enabled: %dx%d -> %dx%d, HUD composed afterward\n",
                        temporalVendor == 3   ? "XeSS"
                        : temporalVendor == 2 ? "FSR"
                                              : "DLSS",
                        iw, ih, renderW, renderH);
        } else {
            if (displayNotice.empty()) displayNotice = "Temporal upscale target allocation failed";
            std::fprintf(stderr, "Temporal upscale unavailable: %s\n", displayNotice.c_str());
        }
    }
    if (frameGeneration) {
        if (frameGenerationDirectory.empty()) frameGenerationDirectory = std::filesystem::absolute(argv[0]).parent_path().string();
        generatedFrames.family = frameGeneration;
        const bool initialized =
            frameGeneration == 2
                ? generatedFrames.fsr.Initialize(port::GetNativeD3D12Device(*gpu), frameGenerationDirectory, &displayNotice)
                : generatedFrames.processor.Initialize(port::GetNativeD3D12Device(*gpu), frameGenerationDirectory, &displayNotice);
        // A saved request may fail the SDK/GPU checks. Flatten only after those
        // succeed, and restore the previous picture if callback registration fails.
        // Raw gun input has already applied its independent flat-image policy.
        const auto beforeGeneration = gpu->EffectiveGraphics();
        bool graphicsReady = initialized;
        if (initialized && beforeGeneration.crtCurvature) {
            auto flatGraphics = beforeGeneration;
            flatGraphics.crtCurvature = 0;
            graphicsReady = gpu->ConfigureGraphics(flatGraphics, &displayNotice);
        }
        generatedFrames.active = graphicsReady && port::SetNativeD3D12FinalFrameCallback(
                                                     *gpu, FrameGenerationRuntime::Process, &generatedFrames, &displayNotice);
        if (generatedFrames.active)
            volumes.display.graphics = gpu->EffectiveGraphics();
        else if (graphicsReady && beforeGeneration.crtCurvature) {
            std::string restoreError;
            if (!gpu->ConfigureGraphics(beforeGeneration, &restoreError))
                displayNotice += " CRT restoration: " + restoreError;
        }
        if (!generatedFrames.active)
            std::fprintf(stderr, "Frame generation unavailable: %s\n", displayNotice.c_str());
        else
            std::printf("Frame generation enabled: 60 Hz rendered input, generated/retained display pacing\n");
    }
#endif
    auto frameGenerationActive = [&] {
#ifdef _WIN32
        return generatedFrames.active;
#else
        return false;
#endif
    };
    auto temporalUpscaleActive = [&] {
#ifdef _WIN32
        return dlss.active;
#else
        return false;
#endif
    };
    auto dynamicResolutionAvailable = [&](const port::GraphicsCapabilities& capabilities) {
        return capabilities.gpuTiming && !temporalUpscaleActive() && !rayActive;
    };
    if (volumes.display.dynamicResolution && !dynamicResolutionAvailable(gpu->GetGraphicsCapabilities()))
        displayNotice = "Dynamic resolution is inactive with the current renderer / reconstruction mode. Saved choice retained.";
    port::UiFont uiFont;
    uiFont.Init();
    play::PackPreview packPreview;
    // Version discovery never waits on the game thread. Automated and direct gameplay launches stay deterministic.
    port::update::UpdateCheck updates;
    if (checkUpdates && !headless && !direct && !netplay && runFrames == 0 && scriptPath.empty() && !getenv("HOTD2_PLAY_KEYS") &&
        !getenv("HOTD2_PLAY_MENU") && !getenv("HOTD2_WII_MENU") && !getenv("HOTD2_LIGHTGUN_MENU"))
        updates.Start(kUpdateBuildName);
    std::string availableUpdate;
    auto& lightguns = plat.Lightguns();
    const std::string lightgunSettingsPath = "hotd2re_lightguns.ini";
    if (!headless) lightguns.settings.Load(lightgunSettingsPath);
    input::WiiRemotes wii(plat.Guns());
    const std::string wiiSettingsPath = "hotd2re_wii.ini";
    if (!headless) {
        wii.settings.Load(wiiSettingsPath);
        wii.Configure();
    }
    input::FocusedGuns focusedGuns(wii);
    input::GunSource* guns = headless ? nullptr : &focusedGuns;
    if (!headless) printf("play: renderer %s, input %s\n", gpu->Name(), guns ? guns->Name() : "none");
    play::DisplaySettings appliedDisplay = volumes.display;
    port::GpuBudgetController gpuBudget;
    gpuBudget.Configure(1000.0 / volumes.display.gpuBudgetFps);
    bool timingEnabled = false;
    bool dynamicResolutionSuspended = false, dynamicResolutionScaled = false;
    double latestGpuMs = 0;
    bool resetTemporalHistory = false;
    port::RenderSize lastBaseSize{renderW, renderH};
    auto desiredRenderSize = [&](float aspect) {
        int ww = width, wh = height;
        if (!headless) gpu->WindowSizePixels(&ww, &wh);
        const auto image = play::DisplayImageRect(ww, wh, aspect, volumes.display.stretch, volumes.display.integerScale);
        const int ah = headless ? (captureRenderSizeGiven ? captureRenderHeight : height) : 480 * volumes.display.renderScale;
        const int aw = headless ? (captureRenderSizeGiven ? captureRenderWidth : width) : static_cast<int>(std::lround(ah * aspect));
        return port::ChooseOutputResolution(static_cast<port::OutputResolution>(volumes.display.resolutionMode), aw, ah, image.w, image.h);
    };
#ifdef _WIN32
    auto disableReconstruction = [&](int outputWidth, int outputHeight, const std::string& reason) {
        const auto fallback = play::RestoreNativeReconstruction(*gpu, outputWidth, outputHeight, reason);
        if (fallback.detached) {
            dlss.active = false;
            dlss.pendingFailure = reason;
            resetTemporalHistory = true;
            latestGpuMs = 0;
            gpuBudget.Reset();
        }
        displayNotice = fallback.notice;
    };
#endif
    auto resizeRender = [&](port::RenderSize size) {
        int rw = size.width, rh = size.height;
#ifdef _WIN32
        if (dlss.active) {
            const int previousW = dlss.outputW, previousH = dlss.outputH;
            const bool configured = dlss.Size(rw, rh, &rw, &rh, &displayNotice);
            if (configured && ((rw == gpu->TargetWidth() && rh == gpu->TargetHeight()) || gpu->ResizeTarget(rw, rh))) return true;
            const std::string failure = displayNotice.empty() ? "Resolution allocation failed" : displayNotice;
            int restoreW = 0, restoreH = 0;
            std::string restoreError;
            if (!dlss.Size(previousW, previousH, &restoreW, &restoreH, &restoreError)) {
                disableReconstruction(previousW, previousH, failure + ". Previous SDK configuration could not be restored: " + restoreError);
            } else {
                displayNotice = failure;
                resetTemporalHistory = true;
            }
            return false;
        }
#endif
        return (rw == gpu->TargetWidth() && rh == gpu->TargetHeight()) || gpu->ResizeTarget(rw, rh);
    };
    auto applyDisplay = [&] {  // after a change on a display page: window / fullscreen / internal resolution
        play::DisplaySettings& d = volumes.display;
        displayNotice.clear();
        // Windows HDR and the current monitor can change between frames. Do not
        // let an unrelated edit submit a now-unsupported stale HDR request.
        if (d.graphics.hdrOutput && !gpu->GetGraphicsCapabilities().hdrOutput) {
            d.graphics.hdrOutput = false;
            displayNotice = "HDR switched to SDR because HDR is unavailable on the current display.";
        }
        if (d.rayShadows != appliedDisplay.rayShadows) {
#ifdef _WIN32
            // A requested saved ray mode may be inactive after backend fallback.
            // Clearing that preference must remain possible without a DXR device.
            if ((d.rayShadows || rayActive) && !port::ConfigureNativeD3D12Raytracing(*gpu, d.rayShadows, &displayNotice))
                d.rayShadows = appliedDisplay.rayShadows;
            rayActive = port::GetNativeD3D12RaytracingInfo(*gpu).enabled;
            resetTemporalHistory = true;
#endif
            latestGpuMs = 0;
            gpuBudget.Reset();
        }
        if (plat.CurrentInput() == platform::Input::RawInput && d.graphics.crtCurvature) {
            d.graphics.crtCurvature = 0;
            displayNotice = "Curvature is disabled for raw light-gun input.";
        }
        if (d.graphics != appliedDisplay.graphics) {
            std::string graphicsError;
            if (!gpu->ConfigureGraphics(d.graphics, &graphicsError))
                displayNotice = graphicsError;
            else
                warmGraphics();
            d.graphics = gpu->EffectiveGraphics();
        }
        if (d.redBlood != appliedDisplay.redBlood) {
            assets.SetRedBlood(d.redBlood);
            renderer.ClearCache();  // cached model meshes retain GPU texture handles
            if (!headless && !volumes.Save(settingsPath)) fprintf(stderr, "Blood color setting could not be saved\n");
        }
        if (d.gpuBudgetFps != appliedDisplay.gpuBudgetFps) gpuBudget.Configure(1000.0 / d.gpuBudgetFps);
        if (d.dynamicResolution != appliedDisplay.dynamicResolution || d.gpuBudgetFps != appliedDisplay.gpuBudgetFps)
            dynamicResolutionSuspended = false;
        if (d.dynamicResolution && !dynamicResolutionAvailable(gpu->GetGraphicsCapabilities()))
            displayNotice = "Dynamic resolution is inactive: GPU timestamps and stable native dimensions are required. Saved choice retained.";
        if (d.renderer != appliedDisplay.renderer || d.temporalUpscaler != appliedDisplay.temporalUpscaler ||
            d.temporalQuality != appliedDisplay.temporalQuality || d.frameGeneration != appliedDisplay.frameGeneration)
            displayNotice = "Reconstruction / renderer choice saved. Restart to apply.";
        if (d.nightShift != appliedDisplay.nightShift)
            displayNotice = d.nightShift == activeNightShift ? "Menu style matches the active menu."
                                                            : "Menu style saved. Restart the game to apply.";
        if (headless) {
            appliedDisplay = d;
            return;
        }
        if (d.monitor != appliedDisplay.monitor && d.monitor > 0) {
            if (plat.IsFullscreen()) plat.SetFullscreen(false);
            if (!plat.MoveToDisplay(d.monitor - 1)) {
                d.monitor = appliedDisplay.monitor;
                displayNotice = "Display unavailable; current monitor retained.";
            }
            if (d.fullscreen) plat.SetFullscreen(true, d.exclusiveFullscreen);
        }
        if (d.fullscreen != appliedDisplay.fullscreen || d.exclusiveFullscreen != appliedDisplay.exclusiveFullscreen) {
            if (!plat.SetFullscreen(d.fullscreen, d.exclusiveFullscreen)) {
                d.fullscreen = appliedDisplay.fullscreen;
                d.exclusiveFullscreen = appliedDisplay.exclusiveFullscreen;
                plat.SetFullscreen(d.fullscreen, d.exclusiveFullscreen);
                displayNotice = "Fullscreen change failed; previous mode retained.";
            }
        }
        if (!d.fullscreen && (d.windowScale != appliedDisplay.windowScale || d.fullscreen != appliedDisplay.fullscreen)) {
            if (d.windowScale == 0)
                fitWindow();
            else
                plat.SetWindowSize(640 * d.windowScale, 480 * d.windowScale);
        }
        int ww, wh;
        plat.ClientSize(&ww, &wh);
        const float requestedAspect = play::DisplayViewportAspect(d.viewport, ww, wh);
        const auto requestedSize = desiredRenderSize(requestedAspect);
        if (requestedSize != lastBaseSize || requestedAspect != viewportAspect || d.dynamicResolution != appliedDisplay.dynamicResolution) {
            if (resizeRender(requestedSize)) {
                viewportAspect = requestedAspect;
                scene.SetViewportAspect(viewportAspect);
                lastBaseSize = requestedSize;
                gpuBudget.Reset();
                dynamicResolutionSuspended = dynamicResolutionScaled = false;
            } else {
                d.renderScale = appliedDisplay.renderScale;
                d.viewport = appliedDisplay.viewport;
                d.resolutionMode = appliedDisplay.resolutionMode;
                if (displayNotice.empty()) displayNotice = "Resolution unavailable; previous size retained.";
            }
        }
        appliedDisplay = d;
    };
    auto saveGraphicsReset = [&](play::DisplayMenu& page) {
        // Save after live device/window fallbacks have settled, even if the
        // user closes the application without leaving the graphics page.
        if (!volumes.Save(settingsPath)) {
            page.SetStatus("Graphics reset, but settings could not be saved. Check folder access.");
            return;
        }
        play::DisplaySettings defaults;
        defaults.nightShift = volumes.display.nightShift;  // keep the chosen menu theme
        const bool restart = common.renderer != platform::Renderer::D3D11 || temporalUpscaleActive() || frameGenerationActive();
        if (volumes.display != defaults)
            page.SetStatus("Reset saved with device adjustments." + std::string(restart ? " Restart to finish." : ""));
        else
            page.SetStatus(restart ? "Graphics defaults saved. Restart to finish applying them." : "Graphics defaults restored and saved.");
    };
    auto presentMapping = [&] {  // every presented frame: the image rectangle of the current window size
        int ww, wh;
        if (!gpu->WindowSizePixels(&ww, &wh)) return;
        const input::ImageRect r = play::DisplayImageRect(ww, wh, viewportAspect, volumes.display.stretch, volumes.display.integerScale);
        port::PresentParams pp;
        pp.x = r.x;
        pp.y = r.y;
        pp.w = r.w;
        pp.h = r.h;
        pp.crt = volumes.display.crt;
        gpu->SetPresent(pp);
        plat.SetGameImage(r);
    };
    if (!headless)
        presentMapping();
    else {
        port::PresentParams present;
        present.w = width;
        present.h = height;
        present.crt = volumes.display.crt;
        gpu->SetPresent(present);
    }

    game::ExeImage sndExe;
    game::SoundTable sndTable;
    std::unique_ptr<port::AudioOutput> sndBackend;
    std::unique_ptr<game::SoundDriver> sndDriver;
    if (audio && !headless && sndExe.Load(dir + "/Hod2.exe") && sndTable.Load(sndExe) &&
        (sndBackend = plat.CreateAudio(common.audio, dir, &err)))
        sndDriver = std::make_unique<game::SoundDriver>(*sndBackend);
    if (audio && !headless && !sndDriver) fprintf(stderr, "audio: %s\n", err.empty() ? "cannot read the sound tables" : err.c_str());

    // The menu scenery has its own sound channels: gameplay and Extras commands must never stop or replace them.
    std::unique_ptr<port::AudioOutput> attractionBackend;
    std::unique_ptr<game::SoundDriver> attractionDriver;
    if (sndDriver && activeNightShift) {
        attractionBackend = plat.CreateAudio(common.audio, dir, &err);
        if (attractionBackend) attractionDriver = std::make_unique<game::SoundDriver>(*attractionBackend);
        else fprintf(stderr, "attraction audio: %s\n", err.c_str());
    }

    // The port's pause menu (src/port/play/pause_menu.h; not in the original): volumes per category, return to the
    // title. The settings live next to the port's save file (the working directory). Interactive runs only: scripted /
    // headless runs keep the original's pause (the label; start resumes).
    auto applyVolumes = [&] {
        const int pv[5] = {volumes.music, volumes.guns, volumes.sfx, volumes.master, volumes.attraction};
        world.SetPortVolumes(pv);  // the options screen's rows follow
        if (sndDriver) sndDriver->SetVolumes(play::GameAudioVolumes(volumes.master, volumes.music, volumes.guns, volumes.sfx));
        if (attractionDriver) attractionDriver->SetVolumes(play::AttractionAudioVolumes(volumes.master, volumes.attraction));
    };
    applyVolumes();
    play::PauseMenu menu;
    menu.SetModern(activeNightShift && uiFont.Available());
    bool menuClosing = false;                     // Return to Main Menu chosen: no more menu input until the world reports the pause lifted
    std::vector<game::WorldDraw> lastSceneDraws;  // the last unpaused frame's draws: behind the pause menu's display page
    const bool menuEnabled = (!headless && script.empty()) || getenv("HOTD2_PLAY_MENU") != nullptr;  // debug: also with scripts / headless
    uint32_t menuPrevKeys = 0;
    // The display settings page over the Options screen (its port-only "Display Settings" row, WorldHud::
    // portDisplayRequests): play.exe draws the page as the whole frame and takes the input while it is open.
    play::LightgunMenu lightgunPage;
    int lightgunRequestsSeen = 0;
    if (getenv("HOTD2_LIGHTGUN_MENU")) lightgunPage.Open();
    play::WiiMenu wiiPage;
    int wiiRequestsSeen = 0;
    play::MenuInput wiiMouse;
    if (getenv("HOTD2_WII_MENU")) wiiPage.Open();  // isolated screenshot/test entry
    play::DisplayMenu optionsPage;
    std::vector<std::string> monitorNames;
    for (const auto& monitor : plat.Displays())
        monitorNames.push_back(monitor.name + " / " + std::to_string(monitor.width) + "x" + std::to_string(monitor.height) + " / " +
                               std::to_string(static_cast<int>(monitor.refresh + .5f)) + " Hz");
    optionsPage.SetMonitors(monitorNames);
    menu.Display().SetMonitors(monitorNames);
    auto gpuCapabilities = gpu->GetGraphicsCapabilities();
    optionsPage.SetGraphicsCapabilities(gpuCapabilities);
    optionsPage.SetNightShiftActive(activeNightShift && uiFont.Available());
    menu.Display().SetGraphicsCapabilities(gpuCapabilities);
    optionsPage.SetFrameGenerationActive(frameGenerationActive());
    menu.Display().SetFrameGenerationActive(frameGenerationActive());
    optionsPage.SetRaytracingAvailable(rayAvailable);
    menu.Display().SetRaytracingAvailable(rayAvailable);
    if (!startupRendererNotice.empty())
        displayNotice = startupRendererNotice + (displayNotice.empty() || displayNotice == startupRendererNotice ? "" : " " + displayNotice);
    const std::string initialGraphicsStatus = displayNotice.empty() ? std::string("Active renderer: ") + gpu->Name() : displayNotice;
    optionsPage.SetStatus(initialGraphicsStatus);
    menu.Display().SetStatus(initialGraphicsStatus);
    auto refreshLiveGraphics = [&] {
        gpuCapabilities = gpu->GetGraphicsCapabilities();
        optionsPage.SetGraphicsCapabilities(gpuCapabilities);
        menu.Display().SetGraphicsCapabilities(gpuCapabilities);
        // Only HDR can fall back automatically inside BeginFrame. Reconcile that
        // field alone; saved color controls and next-launch choices stay intact.
        const bool effectiveHdr = gpu->EffectiveGraphics().hdrOutput;
        if (effectiveHdr != appliedDisplay.graphics.hdrOutput) {
            volumes.display.graphics.hdrOutput = effectiveHdr;
            appliedDisplay.graphics.hdrOutput = effectiveHdr;
            displayNotice = effectiveHdr ? "HDR output is active on the current display."
                                         : "HDR switched to SDR because HDR is unavailable on the current display.";
            optionsPage.SetStatus(displayNotice);
            menu.Display().SetStatus(displayNotice);
            std::fprintf(stderr, "Graphics: %s\n", displayNotice.c_str());
        }
    };
    std::vector<play::DisplayMenu::PackChoice> packChoices{{"Original", "", "Original game textures. Applies on next launch.", true}};
    std::vector<std::filesystem::path> packRoots{std::filesystem::path(dir).parent_path() / "remaster",
                                                 std::filesystem::path(dir).parent_path() / "packs",
                                                 std::filesystem::current_path() / "packs"};
    if (!volumes.assets.empty()) packRoots.emplace_back(volumes.assets);
    for (const auto& pack : port::DiscoverTexturePacks(packRoots))
        packChoices.push_back({pack.name, pack.path.string(),
                               pack.valid ? std::to_string(pack.textures) + " textures / " + std::to_string(pack.frontScreens) +
                                                " screens / " + std::to_string(pack.materialMaps) + " materials / " +
                                                std::to_string(pack.refinedModels) + " refined models." +
                                                (pack.hasAudio ? " Includes audio replacements." : " Visual pack.") + " Restart to apply."
                                          : pack.error,
                               pack.valid});
    // Retain the configured spelling so opening a page never silently changes its selection.
    if (!volumes.assets.empty())
        for (auto& pack : packChoices) {
            std::error_code ec;
            if (!pack.path.empty() && std::filesystem::equivalent(pack.path, volumes.assets, ec) && !ec) pack.path = volumes.assets;
        }
    optionsPage.SetPacks(packChoices, volumes.assets);
    menu.Display().SetPacks(packChoices, volumes.assets);
    // This metadata follows actual asset lookups (and their environment override),
    // not the selector that saves a different pack for the next process launch.
    const auto activePack = assets.ActivePackInfo();
    const size_t activeMaterialMaps = activePack.valid ? activePack.materialMaps : 0;
    const size_t activeRefinedModels = activePack.valid ? activePack.refinedModels : 0;
    const char* assetPackOverride = getenv("HOTD2_ASSET_DIR");
    const bool activePackOverridden = assetPackOverride && *assetPackOverride;
    const std::string activePackName = activePack.name + (activePackOverridden ? " (override)" : "");
    optionsPage.SetActivePackFeatures(activeMaterialMaps, activeRefinedModels);
    menu.Display().SetActivePackFeatures(activeMaterialMaps, activeRefinedModels);
    optionsPage.SetActivePackName(activePackName);
    menu.Display().SetActivePackName(activePackName);
    auto applyPackChoice = [&](play::DisplayMenu& page) {
        std::string chosen;
        if (page.TakePackChange(&chosen)) {
            volumes.assets = chosen;
            // Both entry points edit the same next-launch choice. Active pack
            // metadata and asset lookups remain fixed for this process.
            optionsPage.SetPacks(packChoices, volumes.assets);
            menu.Display().SetPacks(packChoices, volumes.assets);
            if (!headless && !volumes.Save(settingsPath))
                page.SetStatus("Could not save texture pack choice.");
            else
                page.SetStatus(activePackOverridden ? "Saved. HOTD2_ASSET_DIR still overrides this choice."
                                                         : "Texture pack saved. Restart the game to apply.");
        }
    };
    // Every entry point edits the same saved values. The page holds only a UI snapshot;
    // consume its changes once, then refresh from the world and the local mixer settings.
    auto bindOptionValues = [&](play::DisplayMenu& page) {
        play::DisplayMenu::OptionValues values;
        values.audio[0] = volumes.music;
        values.audio[1] = volumes.guns;
        values.audio[2] = volumes.sfx;
        values.audio[3] = volumes.master;
        values.audio[4] = volumes.attraction;
        const auto saved = world.GetPortGameOptions();
        values.difficulty = saved.difficulty;
        values.lives = saved.lives;
        values.credits = saved.credits;
        values.crosshair[0] = saved.crosshair[0];
        values.crosshair[1] = saved.crosshair[1];
        values.freePlayUnlocked = saved.freePlayUnlocked;
        values.gameplayEditable = saved.editable;
        page.SetOptionValues(values);
    };
    auto applyOptionValues = [&](play::DisplayMenu& page) {
        play::DisplayMenu::OptionValues values;
        if (page.TakeOptionChange(&values)) {
            const bool audioChanged = values.audio[0] != volumes.music || values.audio[1] != volumes.guns ||
                                      values.audio[2] != volumes.sfx || values.audio[3] != volumes.master ||
                                      values.audio[4] != volumes.attraction;
            if (audioChanged) {
                volumes.music = values.audio[0];
                volumes.guns = values.audio[1];
                volumes.sfx = values.audio[2];
                volumes.master = values.audio[3];
                volumes.attraction = values.audio[4];
                applyVolumes();
            }
            auto saved = world.GetPortGameOptions();
            const bool gameplayChanged = values.difficulty != saved.difficulty || values.lives != saved.lives ||
                                         values.credits != saved.credits || values.crosshair[0] != saved.crosshair[0] ||
                                         values.crosshair[1] != saved.crosshair[1];
            if (gameplayChanged) {
                saved.difficulty = values.difficulty;
                saved.lives = values.lives;
                saved.credits = values.credits;
                saved.crosshair[0] = values.crosshair[0];
                saved.crosshair[1] = values.crosshair[1];
                page.SetStatus(world.SetPortGameOptions(saved) ? "Gameplay settings saved for the next run."
                                                              : "Gameplay settings are unavailable during a run or online session.");
            }
        }
        if (page.TakeGameplayReset()) {
            if (world.ResetPortGameOptions()) {
                volumes.display.redBlood = play::DisplaySettings{}.redBlood;
                volumes.display.showCrosshairs = true;
                page.SetStatus("Gameplay defaults restored. Records, unlocks and device calibration are preserved.");
            } else {
                page.SetStatus("Gameplay defaults could not be restored during a run or online session.");
            }
        }
        bindOptionValues(page);
    };
    bindOptionValues(optionsPage);
    bindOptionValues(menu.Display());
    int displayRequestsSeen = 0;
    int modelViewerRequestsSeen = 0;
    play::ModelGallery modelGallery;
    play::AttractPreview attract;
    play::AttractionAudio attractionAudio;
    play::LocalHordeLeaderboard hordeRecords;
    const std::string hordeRecordsPath = "hotd2re_horde_records.txt";
    if (!headless) hordeRecords.Load(hordeRecordsPath);
    world.SetPortHordeLeaderboard(hordeRecords.Entries());
    int leaderboardRequestsSeen = 0, publishRequestsSeen = 0, lobbyRequestsSeen = 0, lobbyShortcut = 0;
    uint64_t rankingsRevision = 0, completedRunSequence = 0;
    port::net::LeaderboardSubmission pendingRecord;
    bool pendingRecordValid = false, publishingRecord = false;
    play::RunNamePrompt runNamePrompt;
    uint32_t promptPrevKeys = 0;
    bool promptInputConsumed = false;
    uint64_t pendingCompletionToken = 0, publishingCompletionToken = 0;
    int previousRunMode = world.GameMode();
    bool previousBossSelector = false;
    bool active = true;  // focus gates local input and the system cursor, never the game clock or audio
    int optionsScroll = 0, optionsTabDelta = 0;
    // One frame of menu input from the keyboard edges and the pointing device (the given player's device first).
    auto menuInput = [&](uint32_t pressedKeys, int firstPlayer) {
        play::MenuInput mi;
        if (!active) return mi;
        mi.pressed = pressedKeys & ~(play::kEscapeKey | play::kPreviewKey);
        mi.start = (pressedKeys & play::kEscapeKey) != 0;
        mi.preview = (pressedKeys & play::kPreviewKey) != 0;
        mi.scroll = optionsScroll;
        mi.pageDelta = optionsTabDelta;
        for (int q = 0; q < 2 && guns; ++q) {
            const int dev = deviceForPlayer((q + firstPlayer) % 2);
            if (dev < 0) continue;
            const input::PlayerState ps = play::DisplayPointer(guns->Player(dev), volumes.display.graphics.crtCurvature);
            if (!ps.connected) continue;
            mi.pointer = ps.onScreen;
            mi.px = play::MenuPointerX(ps.x, viewportAspect) * 640.0f;
            mi.py = ps.y * 480.0f;
            mi.trigger = (ps.pressed & input::kTrigger) != 0;
            mi.reload = (ps.pressed & input::kReload) != 0;
            mi.start = mi.start || (ps.pressed & input::kStart) != 0;
            mi.pressed |= ps.menuPressed;
            break;
        }
        return mi;
    };

    auto updateRunNamePrompt = [&](const play::MenuInput& mi) {
        if (runNamePrompt.Update(mi) != play::RunNamePrompt::Action::Publish) return;
        if (pendingRecordValid && pendingRecord.completionToken == runNamePrompt.Token() && !publishingRecord) {
            pendingRecord.name = runNamePrompt.Name();
            std::string submitError;
            if (rankings.Submit(pendingRecord, &submitError)) {
                publishingRecord = true;
                publishingCompletionToken = pendingRecord.completionToken;
                runNamePrompt.BeginSubmitting();
            } else runNamePrompt.FinishSubmission(false, submitError);
        } else runNamePrompt.FinishSubmission(false, publishingRecord ? "A RESULT IS STILL BEING SENT" : "THIS RUN IS NO LONGER AVAILABLE");
    };
    const float clear[4] = {0, 0, 0, 1};
    if (netplay) {
        // Lobby (direct IP for now): the host waits for the joiner's hello and answers with the session config; the
        // joiner boots from that config (credits, seed, serial, the volume rows, the host's save file) so both worlds
        // start identical. The window is up but blank meanwhile (the in-game lobby screen is a later step).
        if (netHostMode) {
            if (!net.Host(static_cast<uint16_t>(netPort), &err)) {
                fprintf(stderr, "netplay: %s\n", err.c_str());
                return 1;
            }
            port::net::SessionConfig c;
            c.credits = credits;
            c.seed = seed;
            c.firstSerial = firstSerial;
            c.portVolumes[0] = volumes.music;
            c.portVolumes[1] = volumes.guns;
            c.portVolumes[2] = volumes.sfx;
            c.portVolumes[3] = volumes.master;
            c.portVolumes[4] = volumes.attraction;
            c.flags = 3;
            c.delay = static_cast<uint8_t>(netDelay);
            game::SaveData matchSave;
            if (!matchSave.LoadNative(savePath)) matchSave.LoadOriginal(dir, assets.Exe());
            c.save = matchSave.NativeBytes();
            net.SetConfig(c);
            printf("netplay: hosting on UDP port %d, waiting for a player to join...\n", netPort);
        } else if (netCode) {
            if (!net.OpenJoiner(static_cast<uint16_t>(netBindPort), &err)) {
                fprintf(stderr, "netplay: %s\n", err.c_str());
                return 1;
            }
        } else {
            if (!net.Join(netJoin, static_cast<uint16_t>(netPort), &err)) {
                fprintf(stderr, "netplay: %s\n", err.c_str());
                return 1;
            }
            printf("netplay: joining %s:%d...\n", netJoin.c_str(), netPort);
        }
        if (netCode) {  // codes: ours from a STUN query, the other player's typed (or --peer)
            if (!net.QueryPublicEndpoint(&err)) {
                fprintf(stderr, "netplay: %s\n", err.c_str());
                return 1;
            }
            const std::string& lc = net.LocalCode();
            printf("netplay: your code is %s %s %s\n", lc.substr(0, 4).c_str(), lc.substr(4, 4).c_str(), lc.substr(8, 4).c_str());
            fflush(stdout);
            std::string peer = netPeerCode;
            if (peer.empty()) {
                printf("netplay: enter the other player's code: ");
                fflush(stdout);
                std::getline(std::cin, peer);
            }
            if (!net.SetPeerCode(peer, &err)) {
                fprintf(stderr, "netplay: %s\n", err.c_str());
                return 1;
            }
            printf("netplay: connecting...\n");
        }
        fflush(stdout);
        for (bool waiting = true; waiting;) {
            for (platform::Event ev; plat.PollEvent(&ev);) {
                if (ev.type == platform::Event::Type::Quit) return 0;
                if (ev.type == platform::Event::Type::Focus) active = ev.active;
            }
            net.Poll();
            if (net.BuildMismatch()) {
                fprintf(stderr, "netplay: the other player runs build %u, this is %u: both need the same version\n", net.RemoteBuild(),
                        kBuildId);
                return 1;
            }
            if (net.Connected())
                waiting = false;
            else
                plat.Delay(5);
        }
        if (!netHostMode) {
            const port::net::SessionConfig& c = net.Config();
            credits = c.credits;
            seed = c.seed;
            firstSerial = c.firstSerial;
            cfg.seedCounter = seed;
            cfg.firstSerial = firstSerial;
            for (int k = 0; k < 5; ++k) netVolumes[k] = c.portVolumes[k];
            netVolumesSet = true;
            savePath = "hotd2re_save_net.bin";  // the host's save, this side's copy (rankings written here go nowhere)
            std::ofstream sf(savePath, std::ios::binary | std::ios::trunc);
            sf.write(reinterpret_cast<const char*>(c.save.data()), static_cast<std::streamsize>(c.save.size()));
            if (c.save.empty()) savePath.clear();
        }
        printf("netplay: connected as P%d, input delay %d frames\n", netLocal + 1, net.Delay());
        fflush(stdout);
    }
    if (!startGame()) {
        fprintf(stderr, "world: %s\n", err.c_str());
        return 1;
    }
    int modeAge = 0;
    // Fixed 60 Hz game clock (the original runs one game frame per 1/60 s); rendering follows the display.
    platform::FixedClock clock;
    platform::PresentClock presentClock;
    clock.Reset(plat.Seconds());
    const double runStart = plat.Seconds();
    long long gameFrames = 0, presented = 0;
    uint64_t temporalFrameId = 0;
    double lastRenderSeconds = 0;
    int lastRenderWorldFrame = -1;
    double perfStart = runStart;
    int perfFrames = 0;
    std::string perfLabel = "60 Hz game / measuring presentation";
    std::string perfDetail, perfVendor;
    port::FrameStatistics frameStatistics;
    int perfWorldFrame = world.Frame(), perfCpuFrames = 0;
    double lastDeliveredSeconds = 0, perfCpuMs = 0;
    uint64_t perfGenerated = 0, perfRetained = 0;
    std::vector<game::World::SoundEvent> sounds;
    bool quit = false;   // window closed or the mode select's EXIT (World::QuitRequested)
    // Typed letters / digits and Backspace presses for the lobby's code field, kept until a game step consumes them
    // (the render loop runs faster than the game clock: most iterations have no step, and a key event that arrived
    // in one of those must not be lost - nor, for Backspace, reach the world as "back" instead).
    std::string typed;
    bool typedBackspace = false;
    bool replayMuted = false;
    std::string networkNotice;
    double networkNoticeUntil = 0;
    bool resultsPresented = false;
    double resultsPresentedAt = 0;
    port::RgbaImage calibrationImage;
    int calibrationKind = -1;
    auto invalidateHomeDirectory = [&] {
        homeDirectory.Reset();
        homeDirectoryOpen = false;
        homeRooms.clear();
        world.SetPortHomeRooms({}, "CHECKING WAITING ROOMS...");
    };
    auto updateSystemCursor = [&] {
        const play::CursorContext context{active, menu.IsOpen() || optionsPage.IsOpen() || wiiPage.IsOpen() || lightgunPage.IsOpen() || modelGallery.IsOpen() || runNamePrompt.IsOpen(),
                                          lobby.page != Lobby::kOff || world.NetworkLobby(), world.PortHordeRun().active};
        plat.SetCursorVisible(play::SystemCursorVisible(world.Hud(), context));
    };
    for (;;) {
        int steps = 1;
        bool netStalled = false;
        net.PollLobbyCleanup();  // cancellation continues after leaving the lobby screen
        const bool showHomeRooms = !netplay && world.Hud().nightShiftPage == 0 && (!headless || getenv("HOTD2_TEST_HOME_DIRECTORY"));
        if (showHomeRooms && !net.LobbyCancellationPending()) {
            if (!homeDirectoryOpen) {
                homeDirectoryOpen = homeDirectory.OpenJoiner(0, nullptr);
                if (homeDirectoryOpen) {
                    homeDirectory.LobbyList();
                    homeDirectoryRefresh = plat.Seconds();
                }
            }
            homeDirectory.Poll();
            if (homeDirectoryOpen && plat.Seconds() - homeDirectoryRefresh >= 2) {
                homeDirectory.LobbyList();
                homeDirectoryRefresh = plat.Seconds();
            }
            std::vector<game::PortHomeRoom> visible;
            if (homeDirectory.LobbyRoomsKnown()) {
                homeRooms = homeDirectory.LobbyRooms();
                play::PruneCancelledHomeRooms(homeRooms, net);
                visible = play::HomeLobbyRooms(homeRooms, kBuildId);
            }
            const std::string status = !homeDirectoryOpen || homeDirectory.LobbySilence() > 6 ? "LOBBY SERVICE UNAVAILABLE"
                                       : homeDirectory.LobbyRoomsKnown()
                                           ? (visible.empty() ? "NO COMPATIBLE WAITING ROOMS" : "WAITING FOR A PARTNER")
                                           : "CHECKING WAITING ROOMS...";
            world.SetPortHomeRooms(visible, status);
        } else if (homeDirectoryOpen || !homeRooms.empty() || showHomeRooms) {
            // A cancelled CREATE may not have returned its ID yet. Wait for
            // cleanup before accepting a fresh list; closing the old directory
            // socket also discards in-flight replies from the previous page.
            invalidateHomeDirectory();
        }
        rankings.Poll();
        if (rankings.Revision() != rankingsRevision) {
            rankingsRevision = rankings.Revision();
            if (rankings.Ready()) {
                std::vector<game::SharedLeaderboardEntry> entries;
                for (const auto& entry : rankings.Entries())
                    entries.push_back({entry.name, static_cast<int>(std::min<uint32_t>(entry.score, 2147483647u)), entry.rounds,
                                       entry.difficulty, entry.players});
                world.SetPortSharedLeaderboard(rankings.Table(), entries,
                                               rankings.Error().empty() ? "COMMUNITY RUNS / UNVERIFIED" : rankings.Error());
            }
        }
        if (publishingRecord && rankings.SubmissionFinished()) {
            publishingRecord = false;
            if (runNamePrompt.Token() == publishingCompletionToken)
                runNamePrompt.FinishSubmission(rankings.LastSubmitAccepted(), rankings.Error());
            if (pendingRecord.completionToken != publishingCompletionToken)
                world.SetPortLeaderboardPublishable(pendingRecordValid, "NEW RUN READY / PUBLISH WHEN YOU CHOOSE");
            else if (rankings.LastSubmitAccepted()) {
                pendingRecordValid = false;
                world.SetPortLeaderboardPublishable(false, "RUN PUBLISHED / COMMUNITY RECORD");
            } else
                world.SetPortLeaderboardPublishable(pendingRecordValid, rankings.Error());
            if (world.Hud().portLeaderboardShared) rankings.Query(world.Hud().portLeaderboardTable);
        }
        if (netplay) {
            net.Poll();
            // (10 s, as the console connections below: a stage start with the asset pack or a big internal resolution
            // can keep a machine silent for several seconds, which is a stall for lockstep, not a departure)
            if (net.CanRejoin() && !net.CatchingUp() && !net.PlayingAlone() && (net.PeerLeft() || net.SecondsSinceRemote() > 10.0)) {
                net.ContinueAlone(static_cast<uint32_t>(world.Frame()));
            }
            if (net.ReplayPending()) {
                world.SetPortLeaderboardRecording(false);
                hordeRecords.DiscardPending();
                pendingRecordValid = false;
                world.SetPortLeaderboardPublishable(false);
                runNamePrompt.Reset();
                if (sndDriver) sndDriver->StopAll();
                if (!startNetworkGame()) {
                    fprintf(stderr, "rejoin: %s\n", err.c_str());
                    return 1;
                }
                net.ReplayStarted(WorldHash(world));
                menu.Close();
                modeAge = 0;
                clock.Reset(plat.Seconds());
            }
            if (net.RecoveryFailed()) {
                networkNotice = net.RecoveryError();
                networkNoticeUntil = plat.Seconds() + 8;
                fprintf(stderr, "rejoin: %s\n", net.RecoveryError().c_str());
                net.Reset();
                netplay = false;
                world.LeaveNetworkRoom();
            }
            // Old servers / console connections have no reserved slot or replay service.
            if (!net.CanRejoin() && (net.PeerLeft() || net.SecondsSinceRemote() > 10.0)) {
                printf("netplay: %s at frame %d; continuing alone as P%d (device 0 -> P%d)\n",
                       net.PeerLeft() ? "the other player left" : "no packets from the other player for 10 s", world.Frame(), netLocal + 1,
                       keyboardPlayer() + 1);
                fflush(stdout);
                netplay = false;
                net.Reset();
                // Release the departed player's buttons and park their gun off-screen. Leaving their
                // last input installed can preserve held keys or a visible crosshair indefinitely.
                in[netRemote] = game::WorldInput{};
                in[netRemote].kind = 1;
                in[netRemote].x = in[netRemote].y = 0xFFFF;
                world.SetInput(netRemote, in[netRemote]);
                scriptJoined[netRemote] = false;
                prevButtons[netRemote] = 0;
            }
        }
        if (!netplay && netPlayerAssigned && world.GameMode() == 3) {
            // Returning to the title ends this game's slot assignment; a new local game uses normal devices.
            netPlayerAssigned = false;
            for (int p = 0; p < 2; ++p) {
                in[p] = game::WorldInput{};
                world.SetInput(p, in[p]);
                scriptJoined[p] = false;
                prevButtons[p] = 0;
            }
        }
        if (!headless) {
            for (platform::Event ev; plat.PollEvent(&ev);) {
                using T = platform::Event::Type;
                if (ev.type == T::Quit) quit = true;
                if (active && runNamePrompt.IsOpen() && ev.type == T::KeyDown &&
                    (ev.key == platform::Key::Backspace || (ev.ch >= 32 && ev.ch < 127))) {
                    const char ch = ev.shift && ev.ch == '-' ? '_' : ev.ch;
                    runNamePrompt.Edit(ev.key == platform::Key::Backspace ? "" : std::string(1, ch),
                                       ev.key == platform::Key::Backspace);
                    g_keys &= ~KeyBit(ev.key);
                    continue;
                }
                if (modelGallery.IsOpen() && (active || ev.type == T::Focus || ev.type == T::KeyUp)) {
                    int ww, wh;
                    plat.ClientSize(&ww, &wh);
                    const auto image = play::DisplayImageRect(ww, wh, viewportAspect, volumes.display.stretch, volumes.display.integerScale);
                    float x = -1, y = -1;
                    input::WindowToImage(ev.x, ev.y, image, &x, &y);
                    port::CrtWarpUV(&x, &y, volumes.display.graphics.crtCurvature);
                    modelGallery.HandleEvent(ev, play::MenuPointerX(x, viewportAspect) * 640, y * 480);
                }
                const bool optionsInput = (optionsPage.IsOpen() || (menu.IsOpen() && menu.DisplayOpen())) &&
                                          !wiiPage.IsOpen() && !lightgunPage.IsOpen();
                if (active && optionsInput && ev.type == T::MouseWheel && ev.wheel != 0) {
                    optionsScroll = std::clamp(optionsScroll + (ev.wheel > 0 ? 1 : -1), -8, 8);
                } else if (active && !netplay && !optionsInput && !modelGallery.IsOpen() && ev.type == T::MouseWheel && ev.wheel != 0 &&
                           world.Hud().nightShiftPage == 9) {
                    // Wheel scrolling only changes library focus; it never emits an audio command.
                    world.ScrollPortExtras(ev.wheel > 0 ? -3 : 3);
                }
                if (active && optionsInput && ev.type == T::KeyDown && !ev.repeat &&
                    (ev.key == platform::Key::PageUp || ev.key == platform::Key::PageDown))
                    optionsTabDelta += ev.key == platform::Key::PageUp ? -1 : 1;
                if (active && (wiiPage.IsOpen() || lightgunPage.IsOpen()) && (ev.type == T::MouseMove || ev.type == T::MouseDown)) {
                    int ww, wh;
                    plat.ClientSize(&ww, &wh);
                    const auto image =
                        play::DisplayImageRect(ww, wh, viewportAspect, volumes.display.stretch, volumes.display.integerScale);
                    float x, y;
                    wiiMouse.pointer = input::WindowToImage(ev.x, ev.y, image, &x, &y);
                    port::CrtWarpUV(&x, &y, volumes.display.graphics.crtCurvature);
                    wiiMouse.pointer = wiiMouse.pointer && x >= 0 && x <= 1 && y >= 0 && y <= 1;
                    wiiMouse.px = play::MenuPointerX(x, viewportAspect) * 640;
                    wiiMouse.py = y * 480;
                    if (ev.type == T::MouseDown) {
                        wiiMouse.trigger |= ev.button == 1;
                        wiiMouse.reload |= ev.button == 3;
                    }
                }
                if (active && !netplay && !optionsInput && !wiiPage.IsOpen() && !lightgunPage.IsOpen() &&
                    !modelGallery.IsOpen() && world.GameMode() == 4 && world.Hud().nightShiftPage == 4 && ev.type == T::KeyDown) {
                    // Name typing owns printable keys, including Space/H/P, which otherwise trigger game actions.
                    if (ev.key == platform::Key::Backspace || (ev.ch >= 32 && ev.ch < 127)) {
                        const char ch = ev.shift && ev.ch == '-' ? '_' : ev.ch;
                        world.EditPortLeaderboardName(ev.key == platform::Key::Backspace ? "" : std::string(1, ch),
                                                      ev.key == platform::Key::Backspace);
                        g_keys &= ~KeyBit(ev.key);
                        continue;
                    }
                }
                if (active && ev.type == T::KeyDown) g_keys |= KeyBit(ev.key);
                if (ev.type == T::KeyUp) g_keys &= ~KeyBit(ev.key);
                if (active && ev.type == T::KeyDown && !ev.repeat && ev.ch && isalnum(static_cast<unsigned char>(ev.ch)))
                    typed += static_cast<char>(toupper(ev.ch));
                if (active && ev.type == T::KeyDown && !ev.repeat && ev.key == platform::Key::Backspace) typedBackspace = true;
                if (ev.type == T::Focus && ev.active != active) {
                    // Keep the simulation and sound running in the background. Focus only controls local input;
                    // pausing is an explicit player action through Start / Escape.
                    active = ev.active;
                    printf("play: %s at game frame %d (manual pause %s)\n", active ? "active" : "inactive (background play)", world.Frame(),
                           world.Hud().paused ? "on" : "off");
                    fflush(stdout);
                    if (!active) {
                        g_keys = menuPrevKeys = 0;
                        typed.clear();
                        typedBackspace = false;
                        wiiMouse = {};
                        optionsScroll = optionsTabDelta = 0;
                        lightguns.ReleaseButtons();
                    }  // key-ups are not seen while inactive
                }
            }
            if (quit) break;
            // A saved curved mouse picture must not remain active after switching to raw gun input.
            const bool curvatureAvailable = plat.CurrentInput() != platform::Input::RawInput;
            optionsPage.SetCurvatureAvailable(curvatureAvailable);
            menu.Display().SetCurvatureAvailable(curvatureAvailable);
            optionsPage.SetFrameGenerationActive(frameGenerationActive());
            menu.Display().SetFrameGenerationActive(frameGenerationActive());
            if (!curvatureAvailable && volumes.display.graphics.crtCurvature) applyDisplay();
            int windowW = width, windowH = height;
            plat.ClientSize(&windowW, &windowH);
            if (play::DisplayViewportAspect(volumes.display.viewport, windowW, windowH) != viewportAspect ||
                desiredRenderSize(viewportAspect) != lastBaseSize)
                applyDisplay();
            wii.Update();
            focusedGuns.UpdateFocus(active);  // drain background edges after polling; held buttons wait for release
            if (netplay && runNamePrompt.IsOpen()) {
                // Name entry is local UI, so it must remain usable while the peer's game clock waits.
                auto mi = menuInput(g_keys & ~promptPrevKeys, netLocal);
                mi.start = active && ((g_keys & ~promptPrevKeys) & play::kEscapeKey) != 0;
                const int dev = deviceForPlayer(netLocal);
                if (active && guns && dev >= 0 && (guns->Player(dev).pressed & input::kStart)) mi.pressed |= 8;
                if (mi.trigger && !mi.pointer) { mi.trigger = false; mi.reload = true; }
                updateRunNamePrompt(mi);
                promptInputConsumed = true;
                if (guns) guns->BeginFrame();
            }
            promptPrevKeys = g_keys;
            updateSystemCursor();
            if (availableUpdate.empty()) availableUpdate = updates.TakeAvailable();
            if (active && !availableUpdate.empty() && !netplay && lobby.page == Lobby::kOff &&
                (world.GameMode() == 3 || world.GameMode() == 4) && !menu.IsOpen() && !optionsPage.IsOpen() && !wiiPage.IsOpen() &&
                !lightgunPage.IsOpen() && !modelGallery.IsOpen() && !runNamePrompt.IsOpen()) {
                // Wait for a local front-end screen so a late response cannot interrupt a match or online lobby.
                if (sndDriver) sndDriver->OnActivate(false);
                if (attractionDriver) attractionAudio.Update(*attractionDriver, sndTable, attract, false);
                if (plat.NotifyUpdate(kBuildName, availableUpdate)) {
                    std::string updateError;
                    if (port::update::StartUpdate(availableUpdate, &updateError)) quit = true;
                    else plat.NotifyUpdateError(updateError);
                }
                // The modal can leave input and focus events queued. Preserve window
                // close/activation, then discard its key/mouse edges before resuming.
                for (platform::Event ev; plat.PollEvent(&ev);) {
                    if (ev.type == platform::Event::Type::Quit) quit = true;
                    if (ev.type == platform::Event::Type::Focus) active = ev.active;
                }
                availableUpdate.clear();
                if (quit) break;
                if (sndDriver) sndDriver->OnActivate(true);
                g_keys = menuPrevKeys = 0;
                typed.clear();
                typedBackspace = false;
                lightguns.ReleaseButtons();
                wii.Update();  // drain remote button edges accumulated while the modal was open
                if (guns) guns->BeginFrame();
                for (int p = 0; p < 2; ++p) {
                    const int dev = deviceForPlayer(p);
                    const auto ps = dev >= 0 && guns ? guns->Player(dev) : input::PlayerState{};
                    play::DevicePlayer held;
                    held.held = ps.held;
                    play::DeviceWorldInput(held, world.Hud().player[p].state, prevButtons[p]);
                }
                clock.Reset(plat.Seconds());
                continue;  // resume with a fresh clock and cleared dialog input
            }
            steps = clock.Advance(plat.Seconds());
            gameFrames += steps;
            if (runFrames > 0 && gameFrames >= runFrames) {
                const double t = plat.Seconds() - runStart;
                printf("play: %lld game frames in %.3f s (%.2f per s), %lld frames presented\n", gameFrames, t, gameFrames / t, presented);
                break;
            }
        } else if (pace) {  // --pace: headless at the game clock (netplay tests: a lobby waits in real time)
            steps = clock.Advance(plat.Seconds());
            if (steps == 0) {
                plat.Delay(1);
                continue;
            }
        }
        sounds.clear();
        if (sndDriver && net.CatchingUp() != replayMuted) {
            replayMuted = net.CatchingUp();
            if (replayMuted) {
                game::SoundDriver::Volumes silent;
                for (float& gain : silent.gain) gain = 0;
                sndDriver->SetVolumes(silent);
            } else
                applyVolumes();
        }
        if (netplay && net.CatchingUp()) steps = std::max(steps, 120);
        const double replaySliceStart = plat.Seconds();
        for (int step = 0; step < steps; ++step) {
            if (netplay && net.CatchingUp() && step > 0 && plat.Seconds() - replaySliceStart > 0.012) break;
            // --- input for this game frame ---
            const int frame = world.Frame();
            // Netplay: this step needs the other side's input for it; without it the step waits (the loop keeps
            // receiving and sending meanwhile). Our own input for this step was pushed Delay() steps ago.
            port::net::FrameInput netLocalIn, netRemoteIn;
            port::net::FrameInput replayInputs[2];
            uint32_t replayHash = 0;
            bool replayStep = false;
            bool netHaveLocal = false, netPresent[2] = {};
            if (netplay) {
                if (net.FinishRecovery(static_cast<uint32_t>(frame))) {
                    clock.Reset(plat.Seconds());
                    sounds.clear();
                    applyVolumes();
                    steps = step + 1;
                }
                replayStep = net.ReplayFrame(static_cast<uint32_t>(frame), replayInputs, &replayHash);
                if (!replayStep && (net.CatchingUp() || !net.RemoteInput(static_cast<uint32_t>(frame), &netRemoteIn))) {
                    netStalled = true;
                    ++netStalls;
                    gameFrames -= steps - step;  // (--run-frames counts completed steps)
                    break;
                }
                netHaveLocal = net.LocalInput(static_cast<uint32_t>(frame), &netLocalIn);
                if (replayStep) {
                    netLocalIn = replayInputs[netLocal];
                    netRemoteIn = replayInputs[netRemote];
                    netHaveLocal = true;
                }
            }
            // The pause menu opens with the world's pause (Game_UpdatePause 0x0040E500) and takes the input while it is
            // open; Resume feeds the world the start press the original resumes with.
            bool menuWasOpen = false, menuInjectStart = false, wiiOpenedThisFrame = false, lightgunOpenedThisFrame = false;
            const bool namePromptWasOpen = runNamePrompt.IsOpen() || promptInputConsumed;
            if (!keyScript.empty())
                while (keyScriptPos < keyScript.size() && keyScript[keyScriptPos].first <= frame + 1)
                    g_keys = keyScript[keyScriptPos++].second;
            const uint32_t scriptedKeyEdges = g_keys & ~menuPrevKeys;
            int menuPausePlayer = 0;
            if (menuEnabled && !net.CatchingUp() && !namePromptWasOpen) {
                const game::WorldHud& hh = world.Hud();
                if (hh.paused && !menu.IsOpen()) {
                    menu.Open();
                    menu.SetNetworkRoom(world.NetworkRoom());
                    if (const char* dbg = getenv("HOTD2_PLAY_MENU");
                        dbg && std::string(dbg) == "display") {  // debug: straight to the display page (screenshots)
                        std::vector<uint32_t> ignore;
                        play::PauseMenu::Input step;
                        for (int k = 0; k < play::PauseMenu::kDisplay; ++k) {
                            step.pressed = 0x20;
                            menu.Update(step, volumes, ignore);
                        }
                        step.pressed = 0x8;
                        menu.Update(step, volumes, ignore);
                    }
                }
                if (!hh.paused && menu.IsOpen()) {
                    menu.Close();
                    menuClosing = false;
                }
                menuWasOpen = menu.IsOpen();
                menuPausePlayer = hh.pausePlayer;
                if (menuWasOpen && !menuClosing && !wiiPage.IsOpen() && !lightgunPage.IsOpen()) {
                    const play::MenuInput mi = menuInput(g_keys & ~menuPrevKeys, hh.pausePlayer);  // the pausing player's device first
                    std::vector<uint32_t> menuSounds;
                    const play::Settings before = volumes;
                    const bool pageWasOpen = menu.DisplayOpen();
                    bindOptionValues(menu.Display());
                    const play::PauseMenu::Action act = menu.Update(mi, volumes, menuSounds);
                    applyOptionValues(menu.Display());
                    optionsScroll = optionsTabDelta = 0;
                    const bool graphicsReset = menu.Display().TakeGraphicsReset();
                    if (volumes.display != before.display) {
                        applyDisplay();
                        menu.Display().SetStatus(displayNotice);
                    }
                    applyPackChoice(menu.Display());
                    if (graphicsReset) saveGraphicsReset(menu.Display());
                    if (sndDriver)
                        for (uint32_t id : menuSounds) game::PlayOptionSound(*sndDriver, sndTable, id);
                    if (volumes != before || act != play::PauseMenu::Action::None || (pageWasOpen && !menu.DisplayOpen()))
                        volumes.Save(settingsPath);  // leaving the menu / page keeps the settings
                    const auto deviceAction = menu.Display().TakeDeviceAction();
                    if (deviceAction == play::DisplayMenu::DeviceAction::WiiRemotes) {
                        wiiPage.Open();
                        wii.BeginFrame();
                        wiiOpenedThisFrame = true;
                    }
                    if (deviceAction == play::DisplayMenu::DeviceAction::Lightguns) {
                        lightgunPage.Open();
                        wii.BeginFrame();
                        lightgunOpenedThisFrame = true;
                    }
                    if (act == play::PauseMenu::Action::Resume) {
                        if (netplay) {
                            netCmd |= port::net::kCmdResume;
                            menuClosing = true;
                        } else
                            menuInjectStart = true;
                    }
                    if (act == play::PauseMenu::Action::ReturnToTitle) {
                        if (netplay) {
                            netCmd |= port::net::kCmdReturnToTitle;  // both worlds leave at the same step (applied below)
                            menuClosing = true;
                        } else {
                            // the menu stays up for the title's init frame (Hud().paused is refreshed by that Step)
                            menuClosing = true;
                            if (sndDriver) sndDriver->StopAll();  // Snd_StopAll 0x0041D350, as every mode init does
                            world.RequestTitle();
                        }
                    }
                }
            }
            // The display page over the Options screen: opened when the world reports a new request; while open it
            // takes the keys and the device buttons (the world gets no input, so the screen stays as it is).
            bool pageWasOpen = wiiPage.IsOpen() || lightgunPage.IsOpen();
            if ((active || headless) && menuEnabled && wiiPage.IsOpen() && !wiiOpenedThisFrame) {
                auto mi = menuInput(g_keys & ~menuPrevKeys, 0);
                if ((g_keys & ~menuPrevKeys) & play::kEscapeKey) mi.pressed |= 2;
                for (int dev = 0; dev < 2; ++dev) {
                    const auto ps = play::DisplayPointer(focusedGuns.Player(dev), volumes.display.graphics.crtCurvature);
                    if (ps.connected && (ps.pressed || ps.menuPressed)) {
                        mi.pointer = ps.onScreen;
                        mi.px = play::MenuPointerX(ps.x, viewportAspect) * 640;
                        mi.py = ps.y * 480;
                        mi.trigger = (ps.pressed & input::kTrigger) != 0;
                        mi.reload = (ps.pressed & input::kReload) != 0;
                        mi.start |= (ps.pressed & input::kStart) != 0;
                        mi.pressed |= ps.menuPressed;
                    }
                }
                if (wiiMouse.pointer) {
                    mi.pointer = true;
                    mi.px = wiiMouse.px;
                    mi.py = wiiMouse.py;
                    mi.trigger |= wiiMouse.trigger;
                    mi.reload |= wiiMouse.reload;
                }
                std::vector<uint32_t> deviceSounds;
                if (wiiPage.Update(mi, wii, &deviceSounds) && !wii.settings.Save(wiiSettingsPath))
                    fprintf(stderr, "Wii Remote settings could not be saved\n");
                if (sndDriver)
                    for (uint32_t id : deviceSounds) game::PlayOptionSound(*sndDriver, sndTable, id);
                wiiMouse = {};
            }
            if ((active || headless) && menuEnabled && lightgunPage.IsOpen() && !lightgunOpenedThisFrame) {
                auto mi = menuInput(g_keys & ~menuPrevKeys, 0);
                if ((g_keys & ~menuPrevKeys) & play::kEscapeKey) mi.pressed |= 2;
                bool deviceAction = false;
                for (int dev = 0; dev < 2; ++dev) {
                    const auto ps = play::DisplayPointer(focusedGuns.Player(dev), volumes.display.graphics.crtCurvature);
                    if (ps.connected && (ps.pressed || ps.menuPressed)) {
                        deviceAction = true;
                        mi.pointer = ps.onScreen;
                        mi.px = play::MenuPointerX(ps.x, viewportAspect) * 640;
                        mi.py = ps.y * 480;
                        mi.trigger = (ps.pressed & input::kTrigger) != 0;
                        mi.reload = (ps.pressed & input::kReload) != 0;
                        mi.start |= (ps.pressed & input::kStart) != 0;
                        mi.pressed |= ps.menuPressed;
                    }
                }
                if (!deviceAction && wiiMouse.pointer) {
                    mi.pointer = true;
                    mi.px = wiiMouse.px;
                    mi.py = wiiMouse.py;
                    mi.trigger |= wiiMouse.trigger;
                    mi.reload |= wiiMouse.reload;
                }
                std::vector<uint32_t> deviceSounds;
                auto act = lightgunPage.Update(mi, lightguns, plat.CurrentInput() == platform::Input::RawInput, &deviceSounds);
                if (sndDriver)
                    for (uint32_t id : deviceSounds) game::PlayOptionSound(*sndDriver, sndTable, id);
                using LA = play::LightgunMenu::Action;
                if (act == LA::GunMode || act == LA::MouseMode) {
                    auto mode = act == LA::GunMode ? platform::Input::RawInput : platform::Input::Mouse;
                    std::string inputError;
                    if (plat.SetInput(mode, &inputError)) {
                        if (wii.settings.enabled) {
                            wii.settings.enabled = false;
                            wii.Configure();
                            if (!wii.settings.Save(wiiSettingsPath)) fprintf(stderr, "Wii Remote settings could not be saved\n");
                        }
                        volumes.input = act == LA::GunMode ? "raw" : "mouse";
                        if (!volumes.Save(settingsPath))
                            lightgunPage.Notice("Could not save input mode - check folder access");
                        else
                            lightgunPage.Notice(act == LA::GunMode ? "Assign each gun to a player" : "Mouse input enabled");
                    } else
                        lightgunPage.Notice(inputError);
                }
                if (act == LA::Save && !lightguns.settings.Save(lightgunSettingsPath))
                    lightgunPage.Notice("Could not save gun settings - check folder access");
                wiiMouse = {};
            }
            if (menuEnabled && optionsPage.IsOpen() && !pageWasOpen && !wiiPage.IsOpen() && !lightgunPage.IsOpen()) {
                pageWasOpen = true;
                const play::MenuInput mi = menuInput(g_keys & ~menuPrevKeys, 0);
                std::vector<uint32_t> pageSounds;
                const play::Settings before = volumes;
                bindOptionValues(optionsPage);
                const bool closed = optionsPage.Update(mi, volumes.display, pageSounds);
                applyOptionValues(optionsPage);
                optionsScroll = optionsTabDelta = 0;
                const bool graphicsReset = optionsPage.TakeGraphicsReset();
                if (volumes.display != before.display) {
                    applyDisplay();
                    optionsPage.SetStatus(displayNotice);
                }
                applyPackChoice(optionsPage);
                if (graphicsReset) saveGraphicsReset(optionsPage);
                if (sndDriver)
                    for (uint32_t id : pageSounds) game::PlayOptionSound(*sndDriver, sndTable, id);
                if ((closed || volumes != before) && !volumes.Save(settingsPath))
                    optionsPage.SetStatus("Could not save local options - check folder access.");
                const auto deviceAction = optionsPage.TakeDeviceAction();
                if (deviceAction == play::DisplayMenu::DeviceAction::WiiRemotes) {
                    wiiPage.Open();
                    wii.BeginFrame();
                }
                if (deviceAction == play::DisplayMenu::DeviceAction::Lightguns) {
                    lightgunPage.Open();
                    wii.BeginFrame();
                }
            }
            if (!optionsPage.IsOpen() && !menu.DisplayOpen()) optionsScroll = optionsTabDelta = 0;
            menuPrevKeys = g_keys;
            if (!script.empty() && !(netplay && !keyScript.empty())) scriptFrame(frame + 1);
            for (int p = 0; p < 2; ++p) {
                const int kb = keyboardPlayer(), dev = deviceForPlayer(p);
                if (dev < 0) continue;
                if (!script.empty()) {
                    if (!scriptJoined[p]) continue;
                } else if (!headless) {
                    // keyboard (P1 pad bits 0x10 / 0x20 / start 0x8), edges since the last game frame. Netplay: this
                    // machine's keyboard and first device belong to the local player, whichever index that is; the
                    // other player's input comes over the network.
                    static uint32_t prevKeys = 0;
                    const uint32_t rawKeys = p == kb ? g_keys : 0;
                    const bool inGame = world.GameMode() == 6 && !world.Hud().stageResults.active && (world.Hud().flags5900 & 0x18) == 0;
                    const bool activePlayer = world.Hud().player[p].state == 5;
                    const uint32_t keys = play::WorldKeyboardBits(rawKeys, inGame, activePlayer);
                    const uint32_t keysPressed = play::WorldKeyboardBits(rawKeys & ~prevKeys, inGame, activePlayer);
                    if (p == kb) prevKeys = g_keys;
                    // The gun source resolves the aim (a raw-input relative mouse aims at the system pointer, like the
                    // original's mouse mode Input_GetMouseClientPos 0x0041EA80; only the first such player owns it).
                    const input::PlayerState ps =
                        play::DisplayPointer(guns ? guns->Player(dev) : input::PlayerState{}, volumes.display.graphics.crtCurvature);
                    if (!ps.connected) {
                        if (p != kb) continue;
                        in[p] = game::WorldInput{};  // pad / keyboard player (input kind 0)
                        in[p].held = keys;
                        in[p].pressed = keysPressed;
                    } else {
                        // Buttons held or pressed since the last game frame; a player who is not in the game (state 9)
                        // joins with the trigger as well as with start (play::DeviceWorldInput, tests/play_join_test.cpp).
                        play::DevicePlayer dp;
                        dp.connected = true;
                        dp.x = inGame && !menuWasOpen && !pageWasOpen ? ps.x : play::MenuPointerX(ps.x, viewportAspect);
                        dp.y = ps.y;
                        dp.onScreen = ps.onScreen;
                        dp.held = ps.held;
                        dp.pressed = ps.pressed;
                        in[p] = play::DeviceWorldInput(dp, world.Hud().player[p].state, prevButtons[p]);
                        if (inGame && !menuWasOpen && !pageWasOpen) in[p] = play::AspectedWorldInput(in[p], ps.x, ps.y, viewportAspect);
                        in[p].held |= keys | play::WorldKeyboardBits(ps.menuHeld, inGame, activePlayer);
                        in[p].pressed |= keysPressed | play::WorldKeyboardBits(ps.menuPressed, inGame, activePlayer);
                    }
                    if (menuWasOpen) {  // the pause menu took this frame's input (the world ignores all but start while paused)
                        in[p].held = in[p].pressed = 0;
                        in[p].fired = in[p].offscreen = false;
                        if (menuInjectStart && p == menuPausePlayer) in[p].pressed = 0x8;  // device record +0x10 start
                    }
                    if (pageWasOpen) {  // the display page over the Options screen took this frame's input
                        in[p].held = in[p].pressed = 0;
                        in[p].fired = in[p].offscreen = false;
                    }
                }
                netPresent[p] = true;
            }
            if (netplay && !keyScript.empty()) {
                const int p = keyboardPlayer();
                const bool inGame = world.GameMode() == 6 && !world.Hud().stageResults.active && (world.Hud().flags5900 & 0x18) == 0;
                in[p] = game::WorldInput{};
                in[p].held = play::WorldKeyboardBits(g_keys, inGame, world.Hud().player[p].state == 5);
                in[p].pressed = play::WorldKeyboardBits(scriptedKeyEdges, inGame, world.Hud().player[p].state == 5);
                if (menuWasOpen || pageWasOpen) in[p].held = in[p].pressed = 0;
                netPresent[p] = true;
            }
            if (!netplay && headless && !keyScript.empty() && !menuWasOpen && !pageWasOpen) {
                const int p = keyboardPlayer();
                const bool playing = world.GameMode() == 6;
                in[p].held |= play::WorldKeyboardBits(g_keys, playing, world.Hud().player[p].state == 5);
                in[p].pressed |= play::WorldKeyboardBits(scriptedKeyEdges, playing, world.Hud().player[p].state == 5);
            }
            if (!headless && !active && script.empty()) {
                // Keep sending neutral local input while the world and remote player advance. Desktop clicks and
                // held controller buttons must not shoot, pause, join or operate a background menu.
                for (int p = 0; p < 2; ++p) {
                    if (netplay && p != netLocal) continue;
                    in[p].held = in[p].pressed = 0;
                    in[p].fired = in[p].offscreen = false;
                    in[p].x = in[p].y = 0xFFFF;  // native menus must not hover under the desktop pointer
                    prevButtons[p] = 0;
                    netPresent[p] = true;
                }
            }
            if (namePromptWasOpen) {
                play::MenuInput mi;
                mi.start = active && (scriptedKeyEdges & play::kEscapeKey) != 0;
                int pointer = -1;
                for (int p = 0; p < 2; ++p) {
                    if (netplay && p != netLocal) continue;
                    mi.pressed |= in[p].pressed & ~0xCu;
                    if (!in[p].fired) mi.pressed |= in[p].pressed & 0xC;
                    mi.trigger |= in[p].fired && !in[p].offscreen;
                    mi.reload |= in[p].offscreen || (in[p].pressed & 2) != 0;
                    if (in[p].kind == 1 && in[p].x < 640 && in[p].y < 480 && (pointer < 0 || in[p].fired)) pointer = p;
                }
                if (pointer >= 0) { mi.pointer = true; mi.px = in[pointer].x; mi.py = in[pointer].y; }
                if (!netplay) updateRunNamePrompt(mi);
                // Only newly captured local input is consumed; recorded peer inputs stay untouched.
                for (int p = 0; p < 2; ++p) {
                    if (netplay && p != netLocal) continue;
                    in[p].held = in[p].pressed = 0;
                    in[p].fired = in[p].offscreen = false;
                    in[p].x = in[p].y = 0xFFFF;
                    netPresent[p] = true;
                }
                typed.clear(); typedBackspace = false;
                // Clear only after the normal key/button history has consumed the closing input.
                // During a peer stall this latch survives even if Skip has already closed the prompt.
                promptInputConsumed = false;
            }
            if (modelGallery.IsOpen()) {
                play::MenuInput mi;
                mi.pressed = (in[0].pressed | in[1].pressed) & ~0xC;
                mi.start = (scriptedKeyEdges & play::kEscapeKey) != 0;
                // A shot takes priority over a resting pointer, including a second player's gun.
                int pointer = -1;
                for (int p = 0; p < 2; ++p) {
                    if (in[p].kind == 1 && in[p].x < 640 && in[p].y < 480) {
                        if (pointer < 0 || in[p].fired) pointer = p;
                    }
                    mi.trigger |= in[p].fired && !in[p].offscreen;
                    mi.reload |= in[p].offscreen || (in[p].pressed & 2) != 0;
                    if (!in[p].fired) mi.pressed |= in[p].pressed & 0xC;
                }
                if (pointer >= 0) {
                    mi.pointer = true;
                    mi.px = in[pointer].x;
                    mi.py = in[pointer].y;
                }
                modelGallery.Update(mi);
                modelGallery.Prepare(assets, renderer);
                // All modal input, including scripted shots, ends here.
                for (int p = 0; p < 2; ++p) {
                    in[p].held = in[p].pressed = 0;
                    in[p].fired = in[p].offscreen = false;
                    in[p].x = in[p].y = 0xFFFF;
                }
                if (!modelGallery.IsOpen()) renderer.ClearCache();
            }
            // The in-game online lobby: the world's Network screen (mode 15 state 1 with the port's hooks) with
            // play.exe's pages over it; it takes this machine's input (in[0]: keyboard, pad, the first device) and
            // lets only the top page's back through to the world.
            if (menuEnabled && !netplay) {
                const int ns = world.Hud().netState;
                if (ns == 1 && lobby.page == Lobby::kOff) {
                    lobby = Lobby();
                    lobby.page = Lobby::kMenu;
                    net.Reset();
                    invalidateHomeDirectory();
                    lobby.canRejoin = net.HasTicket(kRejoinTicket);
                    if (world.Hud().portLobbyRequests != lobbyRequestsSeen) {
                        lobbyRequestsSeen = world.Hud().portLobbyRequests;
                        lobbyShortcut = world.Hud().portLobbyAction;
                    }
                } else if (ns != 1 && lobby.page != Lobby::kOff) {  // the screen was left (back on the Menu page)
                    net.LobbyLeave();
                    net.Reset();
                    invalidateHomeDirectory();
                    lobby = Lobby();
                }
            }
            bool lobbyStarted = false;
            if (lobby.page != Lobby::kOff) {
                auto sound = [&](uint32_t id) {
                    if (sndDriver) sndDriver->PlayGameSound(sndTable, id, game::SoundContext{15, 0});
                };
                auto openSocket = [&](bool host) {
                    net.Reset();
                    bool ok = host ? (net.Host(static_cast<uint16_t>(netPort), nullptr) || net.Host(0, &err))
                                   : net.OpenJoiner(static_cast<uint16_t>(netBindPort), &err);
                    if (ok && !net.LobbyConfigured()) {
                        ok = false;
                        err = "NO LOBBY SERVER CONFIGURED";
                    }
                    if (!ok) {
                        lobby.page = Lobby::kError;
                        lobby.message = err.find("LOBBY") != std::string::npos ? err : "CANNOT OPEN A NETWORK PORT";
                        fprintf(stderr, "netplay: %s\n", err.c_str());
                    }
                    return ok;
                };
                auto hostRoom = [&](uint8_t flags) {  // the host's config goes into the welcome
                    if (!openSocket(true)) return;
                    port::net::SessionConfig c;
                    c.credits = (flags & port::net::kRoomUnlimited) ? -1 : 4;  // free play, or 3 continues
                    c.seed = seed;
                    c.firstSerial = firstSerial;
                    c.portVolumes[0] = volumes.music;
                    c.portVolumes[1] = volumes.guns;
                    c.portVolumes[2] = volumes.sfx;
                    c.portVolumes[3] = volumes.master;
                    c.portVolumes[4] = volumes.attraction;
                    c.flags = 7;  // port features, modern menus, persistent shared room
                    c.delay = static_cast<uint8_t>(netDelay);
                    c.startFrame = static_cast<uint32_t>(cfg.startFrame);
                    game::SaveData matchSave;
                    if (!matchSave.LoadNative(savePath)) matchSave.LoadOriginal(dir, assets.Exe());
                    c.save = matchSave.NativeBytes();
                    net.SetConfig(c);
                    net.LobbyCreate(flags, matchSave.U8(game::SaveData::kDifficulty) % 5);
                    lobby.page = Lobby::kWaiting;
                    lobby.pageSeconds = 0;
                };
                auto goTo = [&](Lobby::Page p) {
                    lobby.page = p;
                    lobby.cursor = 0;
                    lobby.pageSeconds = 0;
                    lobby.pointer.seen = false;
                    lobby.pointerKey = -1;
                };
                lobby.pageSeconds += 1.0 / 60.0;
                net.Poll();
                net.LobbyRefresh();
                const uint32_t pressed = netPresent[0] ? in[0].pressed : 0;  // (a stale in[0] from an earlier step is not an edge)
                const bool up = pressed & 0x10, down = pressed & 0x20, left = pressed & 0x40, right = pressed & 0x80;
                bool confirm = pressed & 0xC, back = pressed & 0x2;
                bool codeBack = false;
                bool backToWorld = false;  // the Menu page's back leaves the screen
                const size_t roomCount = net.LobbyRooms().size();
                const int rows = lobby.RowCount(roomCount);
                // Use the applied input, so mouse, guns, touch and scripted checks share the same hit testing.
                const auto& pointer = in[0];
                const bool aimed = netPresent[0] && pointer.kind == 1 && !pointer.offscreen && pointer.x < 640 && pointer.y < 480;
                const bool shot = netPresent[0] && pointer.kind == 1 && pointer.fired && !pointer.offscreen;
                const bool modernLobby = activeNightShift && uiFont.Available();
                const int r = aimed ? lobby.RowAt(float(pointer.x), float(pointer.y), roomCount, modernLobby) : -1;
                if (lobby.pointer.Update(aimed, float(pointer.x), float(pointer.y), shot)) {
                    if (r >= 0 && r != lobby.cursor) {
                        lobby.cursor = r;
                        sound(0xA9);
                    }
                }
                lobby.pointerKey = aimed && lobby.page == Lobby::kCode ? Lobby::KeyAt(float(pointer.x), float(pointer.y)) : -1;
                const bool cancelPage = lobby.page == Lobby::kWaiting || lobby.page == Lobby::kConnecting || lobby.page == Lobby::kRejoin;
                if (cancelPage && aimed &&
                    game::MenuTextRowHit(float(pointer.x), float(pointer.y), 17.5f, modernLobby ? 26 : 224, modernLobby ? 636 : 416))
                    lobby.pointerKey = 11;
                if (shot) {
                    confirm = (rows && r >= 0) || lobby.page == Lobby::kError;
                    if (lobby.page == Lobby::kCode) {
                        if (lobby.pointerKey >= 0 && lobby.pointerKey < 10)
                            typed += char('0' + (lobby.pointerKey + 1) % 10);
                        else if (lobby.pointerKey == 10)
                            typedBackspace = true;
                        else if (lobby.pointerKey == 11)
                            codeBack = true;
                    } else if (cancelPage && lobby.pointerKey == 11)
                        back = true;
                }
                if (rows && (up || down)) {
                    lobby.cursor = (lobby.cursor + (down ? 1 : rows - 1)) % rows;
                    sound(0xA9);
                }
                if (lobbyShortcut == 4 && lobby.page == Lobby::kMenu) {
                    lobbyShortcut = 0;
                    const uint32_t roomId = world.Hud().portLobbyRoom;
                    const auto action = play::PlanHomeRoom(homeRooms, roomId, kBuildId);
                    if (openSocket(false)) {
                        if (action == play::HomeRoomAction::Browse) {
                            goTo(Lobby::kBrowse);
                            net.LobbyList();
                        } else if (action == play::HomeRoomAction::OtherVersion) {
                            goTo(Lobby::kError);
                            lobby.message = "THAT ROOM RUNS ANOTHER VERSION";
                        } else if (action == play::HomeRoomAction::PrivateCode) {
                            lobby.joinRoom = roomId;
                            lobby.code.clear();
                            goTo(Lobby::kCode);
                        } else {
                            net.LobbyJoin(roomId, 0);
                            goTo(Lobby::kConnecting);
                        }
                    }
                }
                if (lobbyShortcut >= 1 && lobbyShortcut <= 3 && lobby.page == Lobby::kMenu) {
                    lobby.cursor = lobbyShortcut - 1;
                    confirm = true;
                    lobbyShortcut = 0;
                }
                switch (lobby.page) {
                    case Lobby::kMenu:
                        if (confirm) {
                            sound(0x121A9);
                            if (lobby.cursor == 0)
                                goTo(Lobby::kCreate);
                            else if (lobby.cursor == 1) {
                                if (openSocket(false)) {
                                    goTo(Lobby::kBrowse);
                                    net.LobbyList();
                                    lobby.lastList = 0;
                                }
                            } else if (lobby.cursor == 2)
                                goTo(Lobby::kP2P);
                            else if (lobby.cursor == 3 && lobby.canRejoin) {
                                if (net.LoadTicket(kRejoinTicket, &err))
                                    goTo(Lobby::kRejoin);
                                else {
                                    lobby.message = err;
                                    goTo(Lobby::kError);
                                }
                            } else
                                backToWorld = true;
                        }
                        if (back) backToWorld = true;
                        break;
                    case Lobby::kCreate:
                        if ((left || right || confirm) && lobby.cursor < 2) {
                            (lobby.cursor == 0 ? lobby.unlimited : lobby.priv) ^= true;
                            sound(0xA9);
                        } else if (confirm && lobby.cursor == 2) {
                            sound(0x121A9);
                            lobby.p2p = false;
                            hostRoom(static_cast<uint8_t>((lobby.unlimited ? port::net::kRoomUnlimited : 0) |
                                                          (lobby.priv ? port::net::kRoomPrivate : 0)));
                        } else if ((confirm && lobby.cursor == 3) || back) {
                            sound(0x500A9);
                            goTo(Lobby::kMenu);
                        }
                        break;
                    case Lobby::kP2P:
                        if (confirm && lobby.cursor == 0) {
                            sound(0x121A9);
                            lobby.p2p = true;
                            hostRoom(port::net::kRoomUnlisted | port::net::kRoomUnlimited);
                        } else if (confirm && lobby.cursor == 1) {
                            sound(0x121A9);
                            if (openSocket(false)) {
                                lobby.p2p = true;
                                lobby.joinRoom = 0;
                                lobby.code.clear();
                                goTo(Lobby::kCode);
                            }
                        } else if ((confirm && lobby.cursor == 2) || back) {
                            sound(0x500A9);
                            goTo(Lobby::kMenu);
                        }
                        break;
                    case Lobby::kWaiting:
                        if (back) {
                            sound(0x500A9);
                            net.LobbyLeave();
                            net.Reset();
                            invalidateHomeDirectory();
                            goTo(lobby.p2p ? Lobby::kP2P : Lobby::kMenu);
                        } else if (!net.LobbyCreated() && net.LobbySilence() > 6.0) {
                            net.LobbyLeave();
                            net.Reset();
                            invalidateHomeDirectory();
                            lobby.message = "NO ANSWER FROM THE LOBBY SERVER";
                            goTo(Lobby::kError);
                        } else if (net.LobbyCreated() && !lobby.announced) {
                            lobby.announced = true;
                            printf("netplay: room %u code %u\n", net.LobbyRoomId(), net.LobbyCode());
                            fflush(stdout);
                        }
                        break;
                    case Lobby::kBrowse:
                        if (lobby.pageSeconds - lobby.lastList >= 2.0) {
                            net.LobbyList();
                            lobby.lastList = lobby.pageSeconds;
                        }
                        if (lobby.cursor >= rows) lobby.cursor = rows - 1;
                        if (back || (confirm && lobby.cursor == rows - 1)) {
                            sound(0x500A9);
                            net.Reset();
                            goTo(Lobby::kMenu);
                        } else if (confirm) {
                            const port::net::LobbyRoom& r = net.LobbyRooms()[static_cast<size_t>(lobby.cursor)];
                            sound(0x121A9);
                            lobby.p2p = false;
                            if (r.build != kBuildId) {
                                lobby.message = "THAT ROOM RUNS ANOTHER VERSION";
                                goTo(Lobby::kError);
                            } else if (r.flags & port::net::kRoomPrivate) {
                                lobby.joinRoom = r.id;
                                lobby.code.clear();
                                goTo(Lobby::kCode);
                            } else {
                                net.LobbyJoin(r.id, 0);
                                goTo(Lobby::kConnecting);
                            }
                        } else if (!net.LobbyRoomsKnown() && net.LobbySilence() > 6.0) {
                            net.Reset();
                            lobby.message = "NO ANSWER FROM THE LOBBY SERVER";
                            goTo(Lobby::kError);
                        }
                        break;
                    case Lobby::kCode:
                        if (codeBack) {
                            sound(0x500A9);
                            goTo(lobby.p2p ? Lobby::kP2P : Lobby::kBrowse);
                            if (!lobby.p2p) net.LobbyList();
                            break;
                        }
                        if (typedBackspace || back) {
                            if (!lobby.code.empty()) {
                                lobby.code.pop_back();
                                sound(0xA9);
                            } else if (back) {
                                sound(0x500A9);
                                goTo(lobby.p2p ? Lobby::kP2P : Lobby::kBrowse);
                                if (!lobby.p2p) net.LobbyList();
                            }
                        }
                        if (lobby.page == Lobby::kCode) {
                            if (const char* pc = getenv("HOTD2_NET_PEERCODE"); pc && lobby.code.empty() && lobby.pageSeconds < 0.05)
                                typed = pc;  // tests
                            for (char ch : typed)
                                if (isdigit(static_cast<unsigned char>(ch)) && lobby.code.size() < 5) {
                                    lobby.code += ch;
                                    sound(0xA9);
                                }
                            if (lobby.code.size() == 5) {
                                sound(0x121A9);
                                net.LobbyJoin(lobby.joinRoom, static_cast<uint32_t>(atoi(lobby.code.c_str())));
                                goTo(Lobby::kConnecting);
                            }
                        }
                        break;
                    case Lobby::kConnecting: {
                        const port::net::JoinResult jr = net.LobbyJoinResult();
                        const char* why = nullptr;
                        if (jr == port::net::JoinResult::NoRoom)
                            why = lobby.code.empty() ? "THAT ROOM IS NO LONGER AVAILABLE" : "NO ROOM WITH THAT CODE";
                        else if (jr == port::net::JoinResult::WrongCode)
                            why = "WRONG CODE";
                        else if (jr == port::net::JoinResult::Full)
                            why = "THAT ROOM IS FULL";
                        else if (jr == port::net::JoinResult::OtherVersion)
                            why = "THE HOST RUNS ANOTHER VERSION";
                        else if (jr == port::net::JoinResult::NoAnswer ||
                                 (jr == port::net::JoinResult::Pending && net.LobbySilence() > 6.0))
                            why = "NO ANSWER FROM THE LOBBY SERVER";
                        else if (jr == port::net::JoinResult::TimedOut)
                            why = "THE HOST IS NO LONGER AVAILABLE";
                        else if (net.BuildMismatch())
                            why = "THE HOST RUNS ANOTHER VERSION";
                        if (back) {
                            sound(0x500A9);
                            net.Reset();
                            invalidateHomeDirectory();
                            goTo(lobby.p2p ? Lobby::kP2P : Lobby::kMenu);
                        } else if (why) {
                            net.Reset();
                            invalidateHomeDirectory();
                            lobby.message = why;
                            goTo(Lobby::kError);
                        }
                        break;
                    }
                    case Lobby::kError:
                        if (confirm || back) {
                            sound(0x500A9);
                            goTo(Lobby::kMenu);
                        }
                        break;
                    case Lobby::kRejoin:
                        if (back) {
                            net.Reset();
                            goTo(Lobby::kMenu);
                        } else if (net.RecoveryFailed()) {
                            lobby.message = net.RecoveryError();
                            net.Reset();
                            goTo(Lobby::kError);
                        }
                        break;
                    default:
                        break;
                }
                typed.clear();  // consumed by this step
                typedBackspace = false;
                if (!backToWorld) {  // the world only acts on back, and only from the Menu page
                    in[0].pressed &= ~2u;
                    in[0].held &= ~2u;
                } else {
                    net.LobbyLeave();
                    net.Reset();
                    invalidateHomeDirectory();
                    in[0].pressed |= 2u;
                }
                if (net.BuildMismatch() && lobby.page == Lobby::kWaiting) {
                    net.LobbyLeave();
                    net.Reset();
                    lobby.message = "THE OTHER PLAYER RUNS ANOTHER VERSION";
                    goTo(Lobby::kError);
                }
                if (net.Connected()) {
                    // both sides start the same two-player game now (the host's config; the joiner's world takes it
                    // from the welcome): lockstep from its first frame
                    netplay = true;
                    netLocal = net.LocalPlayer();
                    netRemote = net.RemotePlayer();
                    netPlayerAssigned = true;
                    if (!net.RelayAvailable()) net.LobbyLeave();  // a relay room stays for the fallback (keepalives keep it)
                    if (sndDriver) sndDriver->StopAll();
                    if (!startNetworkGame()) {
                        fprintf(stderr, "netplay stage: %s\n", err.c_str());
                        net.LobbyLeave();
                        return 1;
                    }
                    if (net.CatchingUp())
                        net.ReplayStarted(WorldHash(world));
                    else
                        net.BeginHistory(WorldHash(world));
                    if (net.CanRejoin() && !net.SaveTicket(kRejoinTicket)) fprintf(stderr, "netplay: could not save the rejoin ticket\n");
                    modeAge = 0;
                    lobby = Lobby();
                    printf("netplay: connected as P%d (lobby), input delay %d frames, game from frame %d, via %s, round trip %.0f ms\n",
                           netLocal + 1, net.Delay(), world.Frame(), net.PathName(), net.RttMs());
                    fflush(stdout);
                    lobbyStarted = true;
                }
            } else {
                typed.clear();  // no field to type into: the keys of this step are spent
                typedBackspace = false;
            }
            if (lobbyStarted) break;  // the next iteration runs the game's first step in lockstep
            if (!world.Hud().stageResults.active)
                resultsPresented = false;
            else if (!headless && (!resultsPresented || plat.Seconds() - resultsPresentedAt < 3.0)) {
                // Start the local minimum after the first completed render, including texture loading.
                // Gate newly captured input, never recorded/replayed input shared by both worlds.
                for (auto& local : in) {
                    local.pressed &= ~0xCu;
                    local.fired = local.offscreen = false;
                }
            }
            if (!netplay)
                for (int p = 0; p < 2; ++p)
                    if (netPresent[p]) world.SetInput(p, in[p]);
            if (guns) guns->BeginFrame();
            if (netplay) {
                port::net::FrameInput fi;
                fi.present = netPresent[netLocal];
                fi.in = in[netLocal];
                fi.command = netCmd;
                netCmd = 0;
                net.PushLocal(static_cast<uint32_t>(frame) + static_cast<uint32_t>(net.Delay()), fi);
                if (net.PlayingAlone() && menuInjectStart && menuPausePlayer == netRemote) {
                    // The remaining player may resume a pause left by the disconnected player.
                    netRemoteIn.present = true;
                    netRemoteIn.in.pressed |= 0x8;
                }
                if (netHaveLocal && netLocalIn.present) world.SetInput(netLocal, netLocalIn.in);
                if (netRemoteIn.present) {
                    world.SetInput(netRemote, netRemoteIn.in);
                    // The HUD reads the raw gun coordinates from in[], not the world's calibrated aim.
                    // Keep the local cursor immediate, and show the remote cursor from its applied input.
                    in[netRemote] = netRemoteIn.in;
                }
                if ((netLocalIn.command | netRemoteIn.command) & port::net::kCmdReturnToTitle) {
                    menuClosing = true;
                    if (sndDriver) sndDriver->StopAll();
                    world.RequestTitle();
                } else if (((netLocalIn.command | netRemoteIn.command) & port::net::kCmdResume) && world.Hud().paused) {
                    const int pauser = world.Hud().pausePlayer;
                    auto resume = pauser == netLocal ? netLocalIn.in : netRemoteIn.in;
                    resume.pressed = 8;
                    resume.held = 0;
                    resume.fired = resume.offscreen = false;
                    world.SetInput(pauser, resume);
                }
            }
            // --- one game frame ---
            // A local end-of-run modal holds the finished screen. Connected rooms
            // must keep advancing and sending neutral input while each player types.
            if (namePromptWasOpen && !netplay) continue;
            world.SetPortLeaderboardRecording(!headless && script.empty() && keyScript.empty() && !net.CatchingUp() &&
                                              !net.ReplayPending() && !replayStep);
            world.Step();
            if (netplay) {
                const uint32_t hash = WorldHash(world);
                port::net::FrameInput applied[2];
                applied[netLocal] = netLocalIn;
                applied[netRemote] = netRemoteIn;
                net.RecordFrame(static_cast<uint32_t>(frame), applied, hash);
                if (net.RecoveryFailed()) break;
                net.ReportHash(static_cast<uint32_t>(world.Frame()), hash);
                uint32_t df, ours, theirs;
                if (!netDesyncReported && net.Desynced(&df, &ours, &theirs)) {
                    printf("netplay: DESYNC at frame %u (ours %08X, theirs %08X)\n", df, ours, theirs);
                    fflush(stdout);
                    netDesyncReported = true;
                }
                if (world.GameMode() == 3 && !net.CatchingUp()) {
                    net.Abandon();
                    net.Reset();
                    netplay = false;
                    std::error_code ignore;
                    std::filesystem::remove(kRejoinTicket, ignore);
                }
            }
            if (trace) {
                static int lastMode = -1, lastSub = -1;
                const auto& h = world.Hud();
                if (h.gameMode != lastMode || h.subState != lastSub)
                    printf("trace frame %d: game mode %d sub-state %d (game type %d, menu cursor %d, P1 state %d)\n", world.Frame(),
                           h.gameMode, h.subState, h.gameType, h.menuCursor, h.player[0].state);
                lastMode = h.gameMode;
                lastSub = h.subState;
                for (const auto& n : world.Notes()) printf("trace frame %d note: %s\n", world.Frame(), n.c_str());
            }
            if (static FILE* zlog = getenv("HOTD2_PLAY_ZLOG") ? fopen(getenv("HOTD2_PLAY_ZLOG"), "w") : nullptr; zlog) {
                const auto& cm = world.Camera();
                fprintf(zlog, "%d %08X %d %d R %.9g %.9g %.9g %d %d %d\n", world.Frame(), world.RandState(), world.Hud().player[0].score,
                        world.GameMode(), cm.playerPos[0], cm.playerPos[1], cm.playerPos[2], cm.playerPitch, cm.playerYaw, cm.playerRoll);
                if (getenv("HOTD2_PLAY_KLOG") && world.Frame() >= 14659) {
                    fprintf(zlog, "K");
                    for (const auto& ti : world.Tasks()) fprintf(zlog, " %u:%X", ti.serial, ti.fn);
                    fprintf(zlog, "\n");
                }
            }
            // debug: game mode / sub-state and both players (state, lives, continue timer) whenever they change, to plan
            // the two-player continue / game-over captures (notes/agents/twoplayer2.md)
            if (static FILE* stlog = getenv("HOTD2_PLAY_STATELOG") ? fopen(getenv("HOTD2_PLAY_STATELOG"), "w") : nullptr; stlog) {
                const auto& h = world.Hud();
                char line[160];
                snprintf(line, sizeof(line), "%d %d %d %d %d %d %d %d", world.GameMode(), h.subState, h.player[0].state, h.player[0].lives,
                         h.player[1].state, h.player[1].lives, h.player[0].continueTimer >> 12, h.player[1].continueTimer >> 12);
                static std::string last;
                if (last != line) {
                    fprintf(stlog, "%d %s\n", world.Frame(), line);
                    fflush(stlog);
                    last = line;
                }
            }
            if (static FILE* elog = getenv("HOTD2_PLAY_ENVLOG") ? fopen(getenv("HOTD2_PLAY_ENVLOG"), "w") : nullptr; elog) {
                // the env blocks as the hook's V lines print them (HOTD2_SCENECAP_ENV; tools/env_compare.py)
                const game::Environment& e = world.Env();
                fprintf(elog, "V %d %d", world.Frame(), world.GameMode());
                for (int b = 0; b < 2; ++b) {
                    const game::EnvBlock& k = e.Block(b);
                    fprintf(elog, " | %d %d %d %d %d %.7g %.7g %d %.7g %.7g %.7g %.7g", k.lightPitch, k.lightYaw, k.fogColor[0],
                            k.fogColor[1], k.fogColor[2], k.fogNear, k.fogFar, 0, k.lightColor[0], k.lightColor[1], k.lightColor[2],
                            k.lightIntensity);
                }
                const game::WorldLights& l = world.Lights();
                fprintf(elog, " | %.7g %.7g %.7g | %zu", l.tableAmbient[0], l.tableAmbient[1], l.tableAmbient[2], l.table.size());
                for (const auto& s : l.table)
                    fprintf(elog, " %d %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g", s.type, s.diffuse[0], s.diffuse[1], s.diffuse[2],
                            s.pos[0], s.pos[1], s.pos[2], s.dir[0], s.dir[1], s.dir[2]);
                fputc('\n', elog);
            }
            StepTitleCard(world.Hud());
            if (static FILE* tlog = getenv("HOTD2_PLAY_TEXLOG") ? fopen(getenv("HOTD2_PLAY_TEXLOG"), "w") : nullptr; tlog) {
                // debug: every frame's sprites that Spr_Draw skips (texture file not loaded): "<frame> <sprite id> <file>"
                static game::SpriteTables tables;
                static bool loaded = (tables.Load(assets.Exe()), true);
                (void)loaded;
                game::HudState ls =
                    MakeHudState(world, in, modeAge, world.Hud().paused ? blink : world.Hud().frameCounter, credits < 0);
                std::vector<game::SpriteDef> all = WorldSprites(world.Hud());
                const auto hud = HudSprites(ls, world.Hud(), volumes.display.showCrosshairs);
                all.insert(all.end(), hud.begin(), hud.end());
                const auto& tf = world.Hud().texFiles;
                for (const auto& d : all)
                    if (const int f = tables.TextureFile(d.id);
                        f >= 0 && f < static_cast<int>(tf.size()) && tf[static_cast<size_t>(f)] != 4)
                        fprintf(tlog, "%d %X %X\n", world.Frame(), d.id, f);
            }
            ++modeAge;
            const bool bossSelector = world.GameMode() == 6 && world.Hud().gameType == 3 &&
                                      world.Hud().subState == 2 && (world.Hud().flags5900 & 0x18) == 0x18;
            if (world.GameMode() == 6 && (previousRunMode != 6 || (previousBossSelector && !bossSelector))) {
                pendingRecordValid = false;
                world.SetPortLeaderboardPublishable(false);
                runNamePrompt.Reset();
            }
            previousRunMode = world.GameMode();
            previousBossSelector = bossSelector;
            const auto& leaderboardHud = world.Hud();
            if (leaderboardHud.portLeaderboardRequests != leaderboardRequestsSeen) {
                leaderboardRequestsSeen = leaderboardHud.portLeaderboardRequests;
                if (leaderboardHud.portLeaderboardShared && leaderboardHud.nightShiftPage >= 0) {
                    world.SetPortSharedLeaderboard(leaderboardHud.portLeaderboardTable, {}, "CONNECTING TO RANKINGS...");
                    rankings.Query(leaderboardHud.portLeaderboardTable);
                }
            }
            if (leaderboardHud.portLeaderboardPublishRequests != publishRequestsSeen) {
                publishRequestsSeen = leaderboardHud.portLeaderboardPublishRequests;
                if (pendingRecordValid && !publishingRecord) {
                    pendingRecord.name = leaderboardHud.portLeaderboardInitials;
                    std::string submitError;
                    if (rankings.Submit(pendingRecord, &submitError)) {
                        publishingRecord = true;
                        publishingCompletionToken = pendingRecord.completionToken;
                    } else
                        world.SetPortLeaderboardPublishable(true, submitError);
                }
            }
            const auto complete = world.PortLeaderboardRun();
            if (complete.sequence != completedRunSequence) {
                completedRunSequence = complete.sequence;
                if (complete.eligible && !headless && script.empty() && keyScript.empty() && !net.CatchingUp()) {
                    pendingRecord = {};
                    pendingRecord.completionToken = ++pendingCompletionToken;
                    pendingRecord.table = complete.table;
                    pendingRecord.score = complete.table < 2 && netPlayerAssigned ? complete.playerScores[netLocal] : complete.score;
                    pendingRecord.rounds = complete.rounds;
                    pendingRecord.difficulty = complete.difficulty;
                    pendingRecord.players = complete.players;
                    pendingRecordValid = pendingRecord.score > 0;
                    world.SetPortLeaderboardPublishable(pendingRecordValid, "NEW RUN READY / ONLINE NAME AFTER RESULTS");
                    if (pendingRecordValid)
                        runNamePrompt.Queue(pendingRecord.completionToken, pendingRecord.table, pendingRecord.score,
                                            pendingRecord.rounds, world.Hud().portLeaderboardInitials);
                }
            }
            if (!headless && script.empty() && keyScript.empty() && !net.CatchingUp() && hordeRecords.Observe(world.PortHordeRun())) {
                hordeRecords.Save(hordeRecordsPath);
                world.SetPortHordeLeaderboard(hordeRecords.Entries());
                if (!hordeRecords.Entries().empty()) {
                    const auto& entry = hordeRecords.LastCompleted();
                    pendingRecord = {};
                    pendingRecord.completionToken = ++pendingCompletionToken;
                    pendingRecord.table = 2;
                    pendingRecord.score = entry.score;
                    pendingRecord.rounds = entry.rounds;
                    pendingRecord.difficulty = entry.difficulty;
                    pendingRecord.players = entry.players;
                    pendingRecordValid = entry.rounds > 0 && entry.rounds <= 65535 && entry.score > 0;
                    world.SetPortLeaderboardPublishable(pendingRecordValid, "HORDE RUN READY / ONLINE NAME AFTER RESULTS");
                    if (pendingRecordValid)
                        runNamePrompt.Queue(pendingRecord.completionToken, pendingRecord.table, pendingRecord.score,
                                            pendingRecord.rounds, world.Hud().portLeaderboardInitials);
                }
            }
            if (runNamePrompt.MaybeOpen(world.Hud(), headless || !script.empty() || !keyScript.empty() || net.CatchingUp() ||
                                    net.ReplayPending() || menu.IsOpen() || optionsPage.IsOpen() || wiiPage.IsOpen() ||
                                    lightgunPage.IsOpen() || modelGallery.IsOpen() || publishingRecord)) promptPrevKeys = g_keys;
            attract.Step(dir, activeNightShift && (world.Hud().nightShiftPage >= 0 || lobby.page != Lobby::kOff) && !netplay && !modelGallery.IsOpen());
            if (attractionDriver)
                attractionAudio.Update(*attractionDriver, sndTable, attract, attract.Get() && world.Hud().nightShiftPage != 9);
            {
                // Asset pack: the overrides of a texture file the game has just started loading (WorldHud::texFiles
                // 0 -> 3 / 4, Res_LoadTexFile) are decoded on worker threads from now on, ahead of their first draw.
                static std::vector<uint8_t> prevTexFiles;
                const game::World& renderedWorld = attract.Get() ? *attract.Get() : world;
                if (scene.PrepareWorld(renderer, renderedWorld)) prevTexFiles.clear();
                const auto& tf = renderedWorld.Hud().texFiles;
                for (size_t f = 0; f < tf.size(); ++f)
                    if (tf[f] != 0 && (f >= prevTexFiles.size() || prevTexFiles[f] == 0)) assets.PrefetchFile(static_cast<int>(f));
                prevTexFiles = tf;
                // the mode select: the usual next stop is arcade stage 1, whose section files, agents and the stage's
                // own file decode while the player is choosing (the stage-start frame draws them at once otherwise)
                static bool stage1Prefetched = false;
                if (!stage1Prefetched && world.GameMode() == 4 && !port::AssetDir().empty()) {
                    stage1Prefetched = true;
                    game::StageSections s1;
                    if (s1.Load(assets.Exe(), 0, false))
                        for (const auto& [modelId, mode] : s1.objects) {
                            uint16_t file = 0;
                            if (modelId > 0 && assets.Exe().Read(0x004E83B4 + static_cast<uint32_t>(modelId) * 2, &file, 2))
                                assets.PrefetchPolFile(assets.Exe().CStr(assets.Exe().U32(0x004D0EF4 + static_cast<uint32_t>(file) * 4)));
                        }
                    for (const char* f : {"st1_1.bin", "char_adv00.bin", "char_adv04.bin", "player1.bin", "player2.bin", "scr_load2.bin"})
                        assets.PrefetchTexFile(f);
                }
                // the model textures come with their pol files (op 0x50 / 0x52: pol file name table 0x004D0EF4)
                for (const int file : renderedWorld.Hud().polFileLoads) {
                    const std::string pol = assets.Exe().CStr(assets.Exe().U32(0x004D0EF4 + static_cast<uint32_t>(file) * 4));
                    if (static const bool log = getenv("HOTD2_PLAY_TIMING") != nullptr; log)
                        fprintf(stderr, "pol load frame %d: file %d %s -> tex %s\n", world.Frame(), file, pol.c_str(),
                                assets.TextureFileFor(pol).c_str());
                    if (!pol.empty()) assets.PrefetchPolFile(pol);
                }
                // and the decoded ones get their GPU textures a few per frame, ahead of the first draw
                for (const port::RgbaImage* img : assets.DrainPrefetched(4)) renderer.WarmTexture(img);
            }
            if (static const bool timing = getenv("HOTD2_PLAY_TIMING") != nullptr; timing) {
                // debug: frames that took longer than 30 ms of wall time since the previous one (hitches), with the
                // asset pack's inline decode count so a texture decode on the main thread shows as the cause
                static auto last = std::chrono::steady_clock::now();
                static int lastInline = 0;
                static size_t lastGpu = 0;
                const auto now = std::chrono::steady_clock::now();
                const double ms = std::chrono::duration<double, std::milli>(now - last).count();
                const int inl = assets.OverridesDecodedInline();
                const size_t gpu = renderer.TextureCount();
                if (ms > 30.0 && modeAge > 1) {
                    printf("slow frame %d: %.1f ms (mode %d, inline texture decodes %d, prefetched %d, gpu textures +%d, draws %d)\n",
                           world.Frame(), ms, world.GameMode(), inl - lastInline, assets.OverridesPrefetched(),
                           static_cast<int>(gpu - lastGpu), static_cast<int>(world.Draws().size()));
                    fflush(stdout);
                }
                last = now;
                lastInline = inl;
                lastGpu = gpu;
            }
            if (const int* pv = world.Hud().portVolumes;
                pv[0] >= 0 && (pv[0] != volumes.music || pv[1] != volumes.guns || pv[2] != volumes.sfx ||
                               pv[3] != volumes.master || pv[4] != volumes.attraction)) {
                volumes.music = pv[0];  // changed on the options screen
                volumes.guns = pv[1];
                volumes.sfx = pv[2];
                volumes.master = pv[3];
                volumes.attraction = pv[4];
                applyVolumes();
                volumes.Save(settingsPath);
            }
            if (const int req = world.Hud().portModelViewerRequests; req != modelViewerRequestsSeen) {
                if (req > modelViewerRequestsSeen && menuEnabled && !netplay) modelGallery.Open();
                modelViewerRequestsSeen = req;
            }
            if (const int req = world.Hud().portDisplayRequests; req != displayRequestsSeen) {  // the options screen's Display Settings row
                if (req > displayRequestsSeen && menuEnabled) {
                    bindOptionValues(optionsPage);
                    optionsPage.Open();
                    if (world.Hud().portDisplayPage >= 0 && world.Hud().portDisplayPage < play::DisplayMenu::kPageCount)
                        optionsPage.SetPage(static_cast<play::DisplayMenu::Page>(world.Hud().portDisplayPage));
                }
                displayRequestsSeen = req;
            }
            if (const int req = world.Hud().portLightgunRequests; req != lightgunRequestsSeen) {
                if (req > lightgunRequestsSeen && menuEnabled) {
                    lightgunPage.Open();
                    wii.BeginFrame();
                }
                lightgunRequestsSeen = req;
            }
            if (const int req = world.Hud().portWiiRequests; req != wiiRequestsSeen) {
                if (req > wiiRequestsSeen && menuEnabled) {
                    wiiPage.Open();
                    wii.BeginFrame();
                }
                wiiRequestsSeen = req;
            }
            if (world.QuitRequested()) quit = true;
            if (direct && world.GameMode() != 6 && world.GameMode() != 7) {  // the game-over sequence reached the title: new game
                if (!startGame()) {
                    fprintf(stderr, "world: %s\n", err.c_str());
                    return 1;
                }
                modeAge = 0;
            }
            for (const auto& s : world.Sounds()) sounds.push_back(s);
            // debug: every sound-layer call, "<frame> <kind> <id> <serial> <game mode> <game type>" (kind 0 = the
            // captures' U lines)
            if (static FILE* slog = getenv("HOTD2_PLAY_SOUNDLOG") ? fopen(getenv("HOTD2_PLAY_SOUNDLOG"), "w") : nullptr; slog)
                for (const auto& s : world.Sounds())
                    fprintf(slog, "%d %d %X %u %d %d\n", world.Frame(), s.kind, s.id, s.serial, s.gameMode, s.gameType);
        }
        if (netplay) {
            net.Send();  // every iteration: the unacknowledged inputs (and the latest hash) go out even while waiting
            if (netStalled) {
                plat.Delay(1);
                // Still render connection/rejoin status while a missing input stalls the world.
            }
        }
        if (sndDriver)  // in order, each with the game mode / type it was issued in (BGM table), stops and commands too
            for (const auto& s : sounds) game::PlaySoundEvent(*sndDriver, sndTable, s);
        play::ApplyModelGalleryAudio(modelGallery,sndDriver.get(),sndTable);
        // Gate transitions even when a network wait or menu action skipped the simulation tick above.
        if (attractionDriver)
            attractionAudio.Update(*attractionDriver, sndTable, attract,
                activeNightShift && !netplay && world.Hud().nightShiftPage != 9 &&
                (world.Hud().nightShiftPage >= 0 || lobby.page != Lobby::kOff));
        updateSystemCursor();  // pause/resume and menus apply before presentation throttling
        // --- render ---
        // Capture before render throttling/headless skips, so pausing always has
        // the latest scene to inspect while graphics options change.
        if (world.GameMode() == 6 && !world.Hud().paused && !world.Draws().empty()) lastSceneDraws = world.Draws();
        const bool everyShot =
            headless && shotEvery > 0 && world.Frame() % shotEvery == shotPhase && world.Frame() >= shotFrom && world.Frame() < shotFrame;
        if (headless && world.Frame() < shotFrame && !everyShot) continue;
        int renderFrameCap = volumes.display.frameCap;
#ifdef _WIN32
        if (generatedFrames.active) renderFrameCap = 60;
#endif
        if (!headless && !presentClock.Due(plat.Seconds(), renderFrameCap)) {
            plat.Delay(1);
            continue;
        }
        const double renderCpuStarted = (volumes.display.statistics || trace) ? ThreadCpuMilliseconds() : 0;
        const bool activeBudgetScene = world.GameMode() == 6 && !world.Hud().paused && !world.Hud().stageResults.active && !menu.IsOpen() &&
                                       !optionsPage.IsOpen() && !netStalled && !net.CatchingUp() && !net.ReplayPending();
        const bool dynamicRequestedAndAvailable = volumes.display.dynamicResolution && dynamicResolutionAvailable(gpuCapabilities);
        const bool requestTiming =
            gpuCapabilities.gpuTiming && !rayActive && (volumes.display.statistics || trace || dynamicRequestedAndAvailable);
        if (!requestTiming) latestGpuMs = 0;
        if (requestTiming != timingEnabled) timingEnabled = gpu->SetGpuTimingEnabled(requestTiming, nullptr) && requestTiming;
        const bool effectiveDynamicResolution = dynamicRequestedAndAvailable && timingEnabled && !dynamicResolutionSuspended;
        if (!effectiveDynamicResolution) {
            gpuBudget.Reset();
            // Lost timestamps/support must not strand a previously scaled scene.
            // Allocation failures keep their existing transactional rollback;
            // retry the controller only after an explicit budget/base/DRS edit.
            if (dynamicResolutionScaled && !dynamicResolutionSuspended) {
                if (!resizeRender(lastBaseSize)) {
                    dynamicResolutionSuspended = true;
                    displayNotice = "Dynamic resolution inactive; base size could not be restored. Previous size retained.";
                    optionsPage.SetStatus(displayNotice);
                    menu.Display().SetStatus(displayNotice);
                }
                dynamicResolutionScaled = false;
            }
        }
        port::GpuTimingSample timing;
        bool resizedBudget = false;
        while (gpu->PollGpuTiming(&timing)) {
            if (!rayActive && timing.valid && timing.width == gpu->TargetWidth() && timing.height == gpu->TargetHeight())
                latestGpuMs = timing.milliseconds;
            if (activeBudgetScene && effectiveDynamicResolution &&
                gpuBudget.Observe(timing, gpu->TargetWidth(), gpu->TargetHeight()))
                resizedBudget = true;
        }
        if (!activeBudgetScene) gpuBudget.Reset(gpuBudget.Scale());
        if (resizedBudget) {
            const auto size = port::ChooseOutputResolution(port::OutputResolution::Original, lastBaseSize.width, lastBaseSize.height, 0, 0,
                                                           gpuBudget.Scale());
            const int previousWidth = gpu->TargetWidth(), previousHeight = gpu->TargetHeight();
            if (!resizeRender(size)) {
                dynamicResolutionSuspended = true;
                displayNotice = "Dynamic resolution suspended after allocation failure. Previous size and saved choice retained.";
                optionsPage.SetStatus(displayNotice);
                menu.Display().SetStatus(displayNotice);
            } else {
                dynamicResolutionScaled = gpu->TargetWidth() != lastBaseSize.width || gpu->TargetHeight() != lastBaseSize.height;
                if (trace && (gpu->TargetWidth() != previousWidth || gpu->TargetHeight() != previousHeight))
                    std::printf("Dynamic resolution frame %d: %dx%d -> %dx%d, scale %.3f, GPU %.3f ms, budget %.3f ms\n", world.Frame(),
                                previousWidth, previousHeight, gpu->TargetWidth(), gpu->TargetHeight(), gpuBudget.Scale(), latestGpuMs,
                                1000.0 / volumes.display.gpuBudgetFps);
            }
        }
        optionsPage.SetRenderDimensions(gpu->TargetWidth(), gpu->TargetHeight());
        menu.Display().SetRenderDimensions(gpu->TargetWidth(), gpu->TargetHeight());
        optionsPage.SetTemporalInputActive(temporalUpscaleActive() || rayActive);
        menu.Display().SetTemporalInputActive(temporalUpscaleActive() || rayActive);
        if (!world.Hud().paused) blink = world.Hud().frameCounter;
        bool interpolate =
            volumes.display.renderInterpolation && activeBudgetScene && !headless && (renderFrameCap == 0 || renderFrameCap > 60);
#ifdef _WIN32
        interpolate &= !generatedFrames.active;
#endif
        scene.SetRenderInterpolation(interpolate, static_cast<float>(clock.Alpha()));
        game::Mat4 vp;
        std::vector<port::ModelRenderer::SpriteDraw> sprites;
        auto buildAttractBackground = [&] {
            const game::World& preview = *attract.Get();
            vp = scene.BuildFromWorld(renderer, preview);
            if (noHud) return;
            // Tutorial instructions, story pictures / caption effects, and dialogue belong to
            // the preview World. The live game's chapter-card animation must not leak into it.
            auto defs = WorldSprites(preview.Hud(), false);
            std::erase_if(defs, [&](const game::SpriteDef& definition) {
                if (definition.id == 0x900) return true;  // original PRESS START prompt
                // Keep the modern menu's logo and text as the only title overlay.
                return preview.GameMode() == 3 &&
                       ((definition.id >= 0xAB1 && definition.id <= 0xAB5) || definition.id == 0x8FE ||
                        definition.id == 0xA5A || definition.id == 0x231);
            });
            static const bool noTexRes = getenv("HOTD2_NO_TEXRES") != nullptr;
            const auto overlay = scene.MakeSpriteDraws(defs, .8f, noTexRes ? nullptr : &preview.Hud().texFiles);
            sprites.insert(sprites.end(), overlay.begin(), overlay.end());
        };
        const bool previewHidden = optionsPage.IsOpen() ? optionsPage.UiHidden() : menu.DisplayOpen() ? menu.Display().UiHidden() : false;
        auto appendMenuCrosshairs = [&] {
            if (previewHidden) return;
            auto hs = MakeHudState(world, in, modeAge, blink, credits < 0);
            std::vector<game::SpriteDef> defs;
            for (int p = 0; p < 2; ++p)
                if (in[p].kind == 1 && (netplay || (deviceForPlayer(p) >= 0 && guns && guns->Player(deviceForPlayer(p)).connected))) {
                    auto cross = game::HudCrosshair(hs, p);
                    for (auto& c : cross) {
                        c.depth = 0.0002f;
                        c.flags |= 0x700;
                    }
                    defs.insert(defs.end(), cross.begin(), cross.end());
                }
            auto crosshairs = scene.MakeSpriteDraws(defs, 0.0001f, nullptr);
            // Keep aiming cursors above decorative list fades without changing their projection.
            for (auto& crosshair : crosshairs) crosshair.depth = .00002f;
            sprites.insert(sprites.end(), crosshairs.begin(), crosshairs.end());
        };
        const int pattern = optionsPage.IsOpen() ? optionsPage.CalibrationPattern()
                            : menu.DisplayOpen() ? menu.Display().CalibrationPattern()
                                                 : -1;
        if (runNamePrompt.IsOpen()) {
            renderer.SetScene(assets, {});
            vp = game::Mat4::Identity();
            game::WorldHud nameHud;
            runNamePrompt.ModernHud(nameHud);
            play::AppendNightShiftPanels(nameHud, sprites, viewportAspect);
            if (uiFont.Available()) uiFont.Append(nameHud.menuText, sprites, port::ViewportXScale(viewportAspect));
            else {
                std::vector<game::SpriteDef> text;
                for (const auto& line : nameHud.menuText)
                    game::MenuTextScaled(assets.Exe(), line.x, line.y - 12 * line.scale, line.text, 0, line.scale, text);
                auto draws = scene.MakeSpriteDraws(text, .0001f, nullptr);
                sprites.insert(sprites.end(), draws.begin(), draws.end());
            }
        } else if (modelGallery.IsOpen()) {
            modelGallery.Prepare(assets, renderer);
            vp = game::Mat4::Identity();
        } else if (pattern >= 0) {
            renderer.SetScene(assets, {});
            vp = game::Mat4::Identity();
            int cw = gpu->TargetWidth(), ch = gpu->TargetHeight();
#ifdef _WIN32
            if (dlss.active) {
                cw = dlss.outputW;
                ch = dlss.outputH;
            }
#endif
            if (calibrationKind != pattern || calibrationImage.width != cw || calibrationImage.height != ch) {
                renderer.ClearCache();
                calibrationImage = port::MakeDisplayCalibration(cw, ch, static_cast<port::DisplayCalibration>(pattern));
                calibrationKind = pattern;
            }
            port::ModelRenderer::SpriteDraw patternSprite{};
            // Calibration pixels describe scene rendering; their controls use
            // the independent menu pass like the rest of Options.
            patternSprite.hud = false;
            patternSprite.texture = &calibrationImage;
            patternSprite.isp = 0xE4000000;
            patternSprite.tsp = 0x94918000;
            patternSprite.depth = 1;
            patternSprite.v[0] = {0, 0, 0, 1, 0xffffffff, 0, 0, 0};
            patternSprite.v[1] = {0, 480, 0, 1, 0xffffffff, 0, 0, 1};
            patternSprite.v[2] = {640, 0, 0, 1, 0xffffffff, 0, 1, 0};
            patternSprite.v[3] = {640, 480, 0, 1, 0xffffffff, 0, 1, 1};
            sprites.push_back(patternSprite);
            game::WorldHud patternHud;
            (optionsPage.IsOpen() ? optionsPage : menu.Display()).ModernHud(volumes.display, patternHud);
            play::AppendNightShiftPanels(patternHud, sprites, viewportAspect);
            uiFont.Append(patternHud.menuText, sprites, port::ViewportXScale(viewportAspect));
        } else if (uiFont.Available() && activeNightShift && (lightgunPage.IsOpen() || wiiPage.IsOpen())) {
            if (world.Hud().paused && !lastSceneDraws.empty())
                vp = scene.BuildFromWorld(renderer, world, &lastSceneDraws);
            else if (attract.Get())
                buildAttractBackground();
            else if (world.GameMode() == 6)
                vp = scene.BuildFromWorld(renderer, world);
            else {
                renderer.SetScene(assets, {});
                vp = game::Mat4::Identity();
            }
            game::WorldHud deviceHud;
            if (lightgunPage.IsOpen())
                lightgunPage.ModernHud(lightguns, plat.CurrentInput() == platform::Input::RawInput, deviceHud);
            else
                wiiPage.ModernHud(wii, deviceHud);
            // Calibration targets and the gun-test sights must retain full contrast.
            const bool aimCanvas = lightgunPage.IsOpen() ? lightgunPage.CalibrationTarget() >= 0 || lightgunPage.Testing()
                                                        : wiiPage.CalibrationStep() >= 0;
            play::AppendNightShiftPanels(deviceHud, sprites, viewportAspect, aimCanvas ? 100 : volumes.display.menuOpacity);
            uiFont.Append(deviceHud.menuText, sprites, port::ViewportXScale(viewportAspect));
        } else if (world.Hud().stageResults.active) {
            renderer.SetScene(assets, {});
            vp = game::Mat4::Identity();
            std::vector<game::SpriteDef> results;
            const int visibleFrames =
                headless ? world.Hud().stageResults.frames
                         : std::min(world.Hud().stageResults.frames,
                                    resultsPresented ? static_cast<int>(std::min(3.0, plat.Seconds() - resultsPresentedAt) * 60) : 0);
            game::StageResultsSprites(assets.Exe(), world.Hud().stageResults, visibleFrames, results);
            sprites = scene.MakeSpriteDraws(results, 0.001f, nullptr);
        } else if (lobby.page != Lobby::kOff && activeNightShift && uiFont.Available()) {
            if (attract.Get())
                buildAttractBackground();
            else {
                renderer.SetScene(assets, {});
                vp = game::Mat4::Identity();
            }
            const auto lobbyHud = ModernLobbyHud(lobby, net);
            play::AppendNightShiftPanels(lobbyHud, sprites, viewportAspect, volumes.display.menuOpacity);
            uiFont.Append(lobbyHud.menuText, sprites, port::ViewportXScale(viewportAspect));
        } else if (world.NetworkLobby()) {
            // A connected lobby is a complete 2D screen, independent of the last stage's
            // camera, near plane, models and texture residency (as with the pause menu).
            renderer.SetScene(assets, {});
            vp = game::Mat4::Identity();
            sprites = scene.MakeSpriteDraws(world.Hud().sprites, 0.001f, nullptr);
        } else if (lightgunPage.IsOpen()) {
            renderer.SetScene(assets, {});
            vp = game::Mat4::Identity();
            std::vector<game::SpriteDef> md;
            lightgunPage.Sprites(assets.Exe(), lightguns, plat.CurrentInput() == platform::Input::RawInput, md);
            sprites = scene.MakeSpriteDraws(md, 0.001f, nullptr);
        } else if (wiiPage.IsOpen()) {
            renderer.SetScene(assets, {});
            vp = game::Mat4::Identity();
            std::vector<game::SpriteDef> md;
            wiiPage.Sprites(assets.Exe(), wii, md);
            sprites = scene.MakeSpriteDraws(md, 0.001f, nullptr);
        } else if (uiFont.Available() && activeNightShift && (optionsPage.IsOpen() || (menu.IsOpen() && menu.DisplayOpen()))) {
            if (world.Hud().paused && !lastSceneDraws.empty())
                vp = scene.BuildFromWorld(renderer, world, &lastSceneDraws);
            else if (attract.Get())
                buildAttractBackground();
            else {
                renderer.SetScene(assets, {});
                vp = game::Mat4::Identity();
            }
            game::WorldHud displayHud;
            auto& displayPage = optionsPage.IsOpen() ? optionsPage : menu.Display();
            displayPage.ModernHud(volumes.display, displayHud);
            if (!displayPage.UiHidden() && displayPage.CurrentPage() == play::DisplayMenu::kPacks) {
                packPreview.Select(displayPage.SelectedPackPath());
                const auto& info = packPreview.Info();
                const float left = packPreview.Image() ? 340.f : 34.f;
                auto detail = [&](float y, const std::string& text) {
                    if (!text.empty()) displayHud.menuText.push_back({left, y, text.substr(0, 40), .44f, 0xFFC1BAAC});
                };
                detail(244, info.caption);
                if (info.original)
                    detail(264, "No visual pack selected for next launch.");
                else {
                    detail(264, info.name);
                    if (!info.author.empty()) detail(284, "By " + info.author);
                    if (!info.version.empty()) detail(304, "Version " + info.version);
                }
                detail(324, info.message);
            }
            play::AppendNightShiftPanels(displayHud, sprites, viewportAspect, volumes.display.menuOpacity);
            if (!displayPage.UiHidden() && displayPage.CurrentPage() == play::DisplayMenu::kPacks)
                packPreview.AppendSprites(sprites, 30, 240, 280, 86, port::ViewportXScale(viewportAspect));
            uiFont.Append(displayHud.menuText, sprites, port::ViewportXScale(viewportAspect));
            play::AppendMenuScrollHint(displayPage.ScrollHint(), sprites, viewportAspect);
            appendMenuCrosshairs();
        } else if (menu.IsOpen() && menu.IsModern() && uiFont.Available()) {
            if (!lastSceneDraws.empty())
                vp = scene.BuildFromWorld(renderer, world, &lastSceneDraws);
            else {
                renderer.SetScene(assets, {});
                vp = game::Mat4::Identity();
            }
            game::WorldHud pauseHud;
            menu.ModernHud(world.Hud().pausePlayer, volumes, pauseHud);
            play::AppendNightShiftPanels(pauseHud, sprites, viewportAspect, volumes.display.menuOpacity);
            uiFont.Append(pauseHud.menuText, sprites, port::ViewportXScale(viewportAspect));
            appendMenuCrosshairs();
        } else if (menu.IsOpen() && menu.DisplayOpen() && !lastSceneDraws.empty()) {
            // The display page over the paused game: the last unpaused frame's draws again, undimmed (the original's
            // pause overlay 0x93E, the world's only draw while paused, is left out so the scene shows as it will play),
            // the page's rows on top without the tiled background, so scale / aspect / CRT / render-scale changes show
            // at once.
            vp = scene.BuildFromWorld(renderer, world, &lastSceneDraws);
            std::vector<game::SpriteDef> md;
            menu.Display().Sprites(assets.Exe(), volumes.display, md, true);
            sprites = scene.MakeSpriteDraws(md, 0.0001f, nullptr);  // nearer than the overlay's 0.0005: drawn after it
            play::AppendMenuScrollHint(menu.Display().ScrollHint(), sprites, viewportAspect);
            appendMenuCrosshairs();
        } else if (menu.IsOpen() || optionsPage.IsOpen()) {
            // The pause menu (or the display page over the Options screen) is the whole frame, as the options screen is
            // in the original: no world draws (the pause's dark overlay 0x93E sits in front of everything and would hide
            // the menu) and no HUD; its sprites come from the options screen's files, outside the world's residency filter.
            renderer.SetScene(assets, {});
            vp = game::Mat4::Identity();
            std::vector<game::SpriteDef> md;
            if (menu.IsOpen()) {
                menu.Sprites(assets.Exe(), world.Hud().pausePlayer, volumes, md);
            } else {
                optionsPage.Sprites(assets.Exe(), volumes.display, md);
            }
            sprites =
                scene.MakeSpriteDraws(md, menu.IsOpen() ? 0.001f : 0.8f, nullptr);  // the paused near plane (the label's depth is 0.6)
            if (menu.IsOpen() && menu.DisplayOpen())
                play::AppendMenuScrollHint(menu.Display().ScrollHint(), sprites, viewportAspect);
            else if (optionsPage.IsOpen())
                play::AppendMenuScrollHint(optionsPage.ScrollHint(), sprites, viewportAspect);
            appendMenuCrosshairs();
        } else if (world.Hud().nightShiftPage >= 0 && lobby.page == Lobby::kOff) {
            if (attract.Get())
                buildAttractBackground();
            else {
                renderer.SetScene(assets, {});
                vp = game::Mat4::Identity();
            }
            if (!noHud) {
                play::AppendNightShiftPanels(world.Hud(), sprites, viewportAspect, volumes.display.menuOpacity);
                std::vector<game::SpriteDef> menuDefs;
                for (const auto& definition : world.Hud().sprites)
                    if (!uiFont.Available() || (definition.id >= 0xAB1 && definition.id <= 0xAB5) ||
                        (world.Hud().nightShiftPage == 7 && definition.id >= 0xAA8 && definition.id <= 0xAAF))
                        menuDefs.push_back(definition);
                const auto menuSprites = scene.MakeSpriteDraws(menuDefs, .001f, nullptr);
                sprites.insert(sprites.end(), menuSprites.begin(), menuSprites.end());
                if (uiFont.Available()) uiFont.Append(world.Hud().menuText, sprites, port::ViewportXScale(viewportAspect));
                for (const auto& hint : world.Hud().menuScrollHints)
                    play::AppendMenuScrollHint(hint, sprites, viewportAspect);
                if (!world.Hud().menuScrollHints.empty()) appendMenuCrosshairs();
            }
        } else {
            vp = scene.BuildFromWorld(renderer, world);
            game::HudState hs = MakeHudState(world, in, modeAge, blink, credits < 0);
            std::vector<game::SpriteDef> defs = noHud ? std::vector<game::SpriteDef>() : WorldSprites(world.Hud());
            const auto hud = noHud ? std::vector<game::SpriteDef>() : HudSprites(hs, world.Hud(), volumes.display.showCrosshairs);
            defs.insert(defs.end(), hud.begin(), hud.end());
            if (headless && getenv("HOTD2_PLAY_AIMLOG")) {
                // Record the actual crosshair sprites submitted to the renderer (including intermediate screenshots).
                for (int p = 0; p < 2; ++p)
                    for (const auto& cross : game::HudCrosshair(hs, p))
                        for (const auto& d : hud)
                            if (d.id == cross.id)
                                printf("crosshair: frame %d P%d id %X x %.0f y %.0f keyboard P%d device %d\n", world.Frame(), p + 1, d.id,
                                       d.x, d.y, keyboardPlayer() + 1, deviceForPlayer(p));
                fflush(stdout);
            }
            if (lobby.page != Lobby::kOff) LobbySprites(assets.Exe(), lobby, net, defs);  // the online lobby's pages
            // Game_UpdatePause 0x0040E500 sets the near plane to 0.001 (0x004194C0) before the PAUSE label
            if (!noHud) sprites = screens.Draws(world.Hud());                   // front-end bitmap screens first
            static const bool noTexRes = getenv("HOTD2_NO_TEXRES") != nullptr;  // comparisons: every sprite file loaded
            const auto spr = scene.MakeSpriteDraws(defs, world.Hud().paused ? 0.001f : 0.8f, noTexRes ? nullptr : &world.Hud().texFiles);
            sprites.insert(sprites.end(), spr.begin(), spr.end());
            if (!noHud && lobby.page == Lobby::kOff) {
                for (const auto& hint : world.Hud().menuScrollHints)
                    play::AppendMenuScrollHint(hint, sprites, viewportAspect);
                if (!world.Hud().menuScrollHints.empty()) appendMenuCrosshairs();
            }
        }
        if (!modelGallery.IsOpen() && !previewHidden && ((!networkNotice.empty() && plat.Seconds() < networkNoticeUntil) ||
                               (netplay && (net.PlayingAlone() || net.CatchingUp() || net.SecondsSinceRemote() > 1)))) {
            std::vector<game::SpriteDef> status;
            std::string message;
            if (!networkNotice.empty() && plat.Seconds() < networkNoticeUntil)
                message = networkNotice;
            else if (net.CatchingUp()) {
                const uint32_t base = net.Config().startFrame, target = net.RecoveryTarget();
                const uint32_t progress = target > base ? std::min<uint32_t>(100, (world.Frame() - base) * 100 / (target - base)) : 0;
                message = "REJOINING AS P" + std::to_string(netLocal + 1) + "  " + std::to_string(progress) + "%";
            } else if (net.PlayingAlone())
                message = "P" + std::to_string(netRemote + 1) + " DISCONNECTED - CAN REJOIN";
            else
                message = "CONNECTION INTERRUPTED...";
            game::MenuText(assets.Exe(), 0, 17.5f, message, 3, status);
            for (auto& s : status) s.flags |= 0x700;
            const auto overlay = scene.MakeSpriteDraws(status, 0.0001f, nullptr);
            sprites.insert(sprites.end(), overlay.begin(), overlay.end());
        }
        if (volumes.display.statistics && !noHud && !previewHidden && !modelGallery.IsOpen()) {
            std::vector<game::SpriteDef> stats;
            game::MenuTextScaled(assets.Exe(), 12, 10, perfLabel, 0, .5f, stats);
            game::MenuTextScaled(assets.Exe(), 12, 27, perfDetail, 0, .42f, stats);
            game::MenuTextScaled(assets.Exe(), 12, 43, netStalled ? "NETWORK WAIT" : perfVendor, 0, .42f, stats);
            for (auto& s : stats) {
                s.flags |= 0x700;
                s.depth = .00001f;
            }
            const auto overlay = scene.MakeSpriteDraws(stats, .00001f, nullptr);
            sprites.insert(sprites.end(), overlay.begin(), overlay.end());
        }
#ifdef _WIN32
        if (dlss.active && (!dlss.pendingFailure.empty() || !gpu->HudCompositionError().empty())) {
            const std::string failure = dlss.pendingFailure.empty() ? gpu->HudCompositionError() : dlss.pendingFailure;
            disableReconstruction(dlss.outputW, dlss.outputH, failure);
            optionsPage.SetStatus(displayNotice);
            menu.Display().SetStatus(displayNotice);
            optionsPage.SetRenderDimensions(gpu->TargetWidth(), gpu->TargetHeight());
            menu.Display().SetRenderDimensions(gpu->TargetWidth(), gpu->TargetHeight());
            std::fprintf(stderr, "Temporal upscale: %s\n", displayNotice.c_str());
        }
#endif
        if (!headless) presentMapping();  // the window may have been resized / toggled; the settings may have changed
        const bool outputResolutionMenu = !previewHidden &&
            (menu.IsOpen() || optionsPage.IsOpen() || lightgunPage.IsOpen() || wiiPage.IsOpen() || modelGallery.IsOpen() || runNamePrompt.IsOpen() ||
             world.Hud().nightShiftPage >= 0 || !world.Hud().menuText.empty() || world.GameMode() == 12 ||
             world.NetworkLobby() || lobby.page != Lobby::kOff);
        // Menu readability is independent of scene resolution and the saved
        // gameplay Sharp HUD preference. Hide UI still previews the game alone.
        gpu->SetMenuOverlay(outputResolutionMenu);
        const bool beganFrame = gpu->BeginFrame(clear);
        // BeginFrame may switch HDR off even when it cannot start a drawable
        // frame (for example while minimized or waiting for presentation).
        refreshLiveGraphics();
        if (!beganFrame) {
            if (!headless) plat.Delay(1);
            continue;
        }
        port::TemporalFrameInfo temporal;
        const double renderSeconds = plat.Seconds();
        temporal.frameTimeMs = lastRenderWorldFrame < 0 ? 1000.f / 60.f
                               : headless               ? std::max(1, world.Frame() - lastRenderWorldFrame) * (1000.f / 60.f)
                                                        : static_cast<float>(std::max(.001, (renderSeconds - lastRenderSeconds) * 1000));
        temporal.reset = resetTemporalHistory || temporal.frameTimeMs > 250;
        resetTemporalHistory = false;
        lastRenderSeconds = renderSeconds;
        lastRenderWorldFrame = world.Frame();
        bool temporalEnabled = volumes.display.graphics.temporalAA;
#ifdef _WIN32
        temporalEnabled |= dlss.active || generatedFrames.active;
        if (generatedFrames.active)
            port::SetNativeD3D12FrameGenerationPaused(*gpu, world.Hud().paused || menu.IsOpen() || optionsPage.IsOpen() || runNamePrompt.IsOpen());
#endif
        if (temporalEnabled) {
            temporal.enabled = true;
            temporal.frameId = ++temporalFrameId;
            int phases = 8;
#ifdef _WIN32
            if (dlss.active) {
                const float ratio = float(dlss.outputW) / gpu->TargetWidth();
                phases = std::clamp(int(std::lround(8 * ratio * ratio)), 8, 64);
            }
#endif
            bool jitter = volumes.display.graphics.temporalAA;
#ifdef _WIN32
            jitter |= dlss.active;
#endif
            if (jitter) {
                const uint64_t phase = (temporal.frameId - 1) % phases + 1;
                temporal.jitterPixels[0] = HaltonJitter(phase, 2);
                temporal.jitterPixels[1] = HaltonJitter(phase, 3);
            }
        }
        renderer.SetTemporalFrame(temporal);
        if (modelGallery.IsOpen())
            modelGallery.Draw(renderer, uiFont, gpu->TargetWidth(), gpu->TargetHeight(), viewportAspect);
        else
            renderer.DrawGame(gpu->TargetWidth(), gpu->TargetHeight(), vp.m, &scene.lighting, &scene.fog, scene.view.m, &sprites,
                              &scene.itemLights, scene.projection.m);
        if (!gpu->HudCompositionError().empty() && gpu->HudCompositionError() != displayNotice) {
            displayNotice = gpu->HudCompositionError();
            optionsPage.SetStatus(displayNotice);
            menu.Display().SetStatus(displayNotice);
            std::fprintf(stderr, "HUD composition: %s\n", displayNotice.c_str());
        }
#ifdef _WIN32
        if (dlss.active && !dlss.error.empty() && dlss.error != displayNotice) {
            displayNotice = dlss.error;
            std::fprintf(stderr, "Temporal upscale: %s\n", displayNotice.c_str());
        }
        if (!generatedFrames.error.empty() && generatedFrames.error != displayNotice) {
            displayNotice = generatedFrames.error;
            std::fprintf(stderr, "Frame generation: %s\n", displayNotice.c_str());
        }
#endif
        auto finishFrame = [&] {
            gpu->EndFrame();
#ifdef _WIN32
            if (headless && generatedFrames.active && !frameGenerationCaptures.empty()) {
                std::vector<uint8_t> generated, retained;
                int w = 0, h = 0;
                uint32_t disabled = 1;
                std::string captureError;
                if (port::ReadNativeD3D12GeneratedFrames(*gpu, &generated, &retained, &w, &h, &disabled, &captureError)) {
                    std::error_code ec;
                    std::filesystem::create_directories(frameGenerationCaptures, ec);
                    const auto stem = std::filesystem::path(frameGenerationCaptures) / ("frame-" + std::to_string(world.Frame()));
                    if (!platform::Platform::SavePng(stem.string() + "-generated.png", generated, w, h) ||
                        !platform::Platform::SavePng(stem.string() + "-retained.png", retained, w, h))
                        return false;
                    std::printf("Frame generation frame %d: %dx%d actual output pair, disabled %u\n", world.Frame(), w, h, disabled);
                } else if (trace)
                    std::fprintf(stderr, "Frame generation capture: %s\n", captureError.c_str());
            }
#endif
            return true;
        };
        if (world.Hud().stageResults.active && !resultsPresented) {
            resultsPresented = true;
            resultsPresentedAt = plat.Seconds();
        }
        if (headless) {
            std::vector<uint8_t> pixels;
            char name[512];
            snprintf(name, sizeof(name), screenshot.c_str(), world.Frame());  // "%d" in the name: the frame
            int captureW = gpu->TargetWidth(), captureH = gpu->TargetHeight();
            if (volumes.display.graphics.outputResolutionHud || outputResolutionMenu || captureRenderSizeGiven) {
                captureW = width;
                captureH = height;
            }
            bool read = false;
#ifdef _WIN32
            if (dlss.active) {
                captureW = dlss.outputW;
                captureH = dlss.outputH;
                read = gpu->ReadPresentation(captureW, captureH, &pixels);
            } else
#endif
                read = gpu->ReadPresentation(captureW, captureH, &pixels);
            if (!read && volumes.display.graphics == port::GraphicsOptions{} && volumes.display.crt == 0) read = gpu->ReadPixels(&pixels);
            if (!read || !platform::Platform::SavePng(name, pixels, captureW, captureH)) {
                fprintf(stderr, "failed to write %s\n", name);
                return 1;
            }
            if (captureRenderSizeGiven) {
                std::printf("render capture: scene %dx%d; output %dx%d; menu overlay %s; sharp HUD %s\n",
                            gpu->TargetWidth(), gpu->TargetHeight(), captureW, captureH, outputResolutionMenu ? "on" : "off",
                            volumes.display.graphics.outputResolutionHud ? "on" : "off");
            }
            if (!keyScript.empty()) {
                printf("checkpoint frame %d mode %d paused %d type %d rank %d difficulty %d\n", world.Frame(), world.GameMode(),
                       world.Hud().paused ? 1 : 0, world.Hud().gameType, world.Hud().rank, world.GetPortGameOptions().difficulty);
                fflush(stdout);
            }
            if (everyShot) {
                if (!finishFrame()) return 1;
                continue;
            }
            const auto& h = world.Hud();
            const size_t stageModels =
                static_cast<size_t>(std::count_if(world.Draws().begin(), world.Draws().end(),
                                                  [](const game::WorldDraw& draw) { return draw.fn == 0x00401260; }));  // Stage_DrawSection
            printf(
                "%s: frame %d mode %d P1 state %d lives %d ammo %d score %d; effects %zu route map %d draws %zu sprites %zu paused %d "
                "title %u stage-models %zu\n",
                screenshot.c_str(), world.Frame(), h.gameMode, h.player[0].state, h.player[0].lives, h.player[0].ammo, h.player[0].score,
                h.effects.size(), h.routeMapScroll, world.Draws().size(), sprites.size(), h.paused ? 1 : 0, h.titleSerial, stageModels);
            if (netplay) printf("netplay: final state hash %08X at frame %d, local P%d\n", WorldHash(world), world.Frame(), netLocal + 1);
            if (getenv("HOTD2_PLAY_DEBUG"))
                for (const auto& d : world.Draws())
                    if (d.drawMode != 8 || d.alpha >= 0 || d.viewSpace || getenv("HOTD2_PLAY_DEBUG_ALL"))
                        printf("  draw model %d task %u fn %08X mode %X alpha %.3f view %d\n", d.model, d.task, d.fn, d.drawMode, d.alpha,
                               d.viewSpace ? 1 : 0);
            if (getenv("HOTD2_PLAY_DEBUG"))
                for (const auto& s : sprites)
                    for (int k = 0; k < 4; ++k)
                        printf("  sprite tex %08X key %08X v%d %.1f %.1f z %.4f rhw %.4f col %08X uv %.2f %.2f\n", s.textureId, s.key, k,
                               s.v[k].x, s.v[k].y, s.v[k].z, s.v[k].rhw, s.v[k].diffuse, s.v[k].u, s.v[k].v);
            if (!finishFrame()) return 1;
            return 0;
        }
        if (!finishFrame()) return 1;  // only presentation can wait; gameplay uses the fixed 60 Hz clock
        if (!headless) {
            ++perfFrames;
            const double delivered = plat.Seconds();
            if (lastDeliveredSeconds > 0) frameStatistics.Observe((delivered - lastDeliveredSeconds) * 1000);
            lastDeliveredSeconds = delivered;
            if (renderCpuStarted > 0) {
                perfCpuMs += std::max(0., ThreadCpuMilliseconds() - renderCpuStarted);
                ++perfCpuFrames;
            }
            const double elapsed = delivered - perfStart;
            if (elapsed >= .5) {
                char label[180];
                snprintf(label, sizeof(label), "Game %.1f Hz / Render %.1f FPS", std::max(0, world.Frame() - perfWorldFrame) / elapsed,
                         perfFrames / elapsed);
                perfLabel = label;
                char gpuText[40];
                if (latestGpuMs > 0)
                    snprintf(gpuText, sizeof(gpuText), "GPU %.2f ms", latestGpuMs);
                else
                    snprintf(gpuText, sizeof(gpuText), "GPU unavailable");
                snprintf(label, sizeof(label), "P95 %.2f / P99 %.2f ms / %s / CPU thread %.2f ms", frameStatistics.Percentile(.95),
                         frameStatistics.Percentile(.99), gpuText, perfCpuFrames ? perfCpuMs / perfCpuFrames : 0);
                perfDetail = label;
                perfVendor.clear();
#ifdef _WIN32
                if (generatedFrames.active) {
                    const auto fg = port::GetNativeD3D12FrameGenerationStats(*gpu);
                    snprintf(label, sizeof(label), "FG %.1f generated/s / %.1f retained/s",
                             (fg.generatedPresented - perfGenerated) / elapsed, (fg.retainedPresented - perfRetained) / elapsed);
                    perfVendor = !fg.lastPresentationEligible ? fg.status.substr(0, 78) : label;
                    perfGenerated = fg.generatedPresented;
                    perfRetained = fg.retainedPresented;
                }
#endif
                if (trace)
                    std::printf("Performance frame %d: %s / %s / %s\n", world.Frame(), perfLabel.c_str(), perfDetail.c_str(),
                                perfVendor.empty() ? "FG Off" : perfVendor.c_str());
                perfStart = delivered;
                perfWorldFrame = world.Frame();
                perfCpuMs = 0;
                perfCpuFrames = 0;
                perfFrames = 0;
            }
        }
        ++presented;
    }
    if (!headless && hordeRecords.Finish()) hordeRecords.Save(hordeRecordsPath);
    if (netplay) {
        const port::net::Session::Stats st = net.GetStats();
        uint32_t df = 0;
        const auto& hh = world.Hud();
        printf(
            "netplay: %lld stalled iterations, packets in %u out %u, remote latest frame %u, %s; frame %d mode %d P1 state %d score %d P2 "
            "state %d score %d\n",
            netStalls, st.packetsIn, st.packetsOut, st.remoteLatest, net.Desynced(&df, nullptr, nullptr) ? "DESYNCED" : "in sync",
            world.Frame(), world.GameMode(), hh.player[0].state, hh.player[0].score, hh.player[1].state, hh.player[1].score);
        fflush(stdout);
        net.Close();
    }
    return 0;
}

#if defined(_WIN32) && defined(PLAY_GUI)
// hotd2.exe: the GUI subsystem's entry point, forwarding to main (the CRT's __argc / __argv are the parsed command
// line). Linking the GUI exe with /ENTRY:mainCRTStartup instead made Windows Defender's heuristics flag it.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return main(__argc, __argv); }
#endif
