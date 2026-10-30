// game::World - game mode 15, the network lobby (0x0049F380), on a machine without a network game: the port has no
// DirectPlay. The lobby itself is ported (GDI blits of tex\scr_etc000..003, table 0x00598148, as WorldHud::screens):
// JOIN SESSION / CREATE SESSION / EXIT, the IP address panel, PLEASE WAIT!, FAILED!!, CANCEL!!, and the way back to the
// mode select. A session never starts (states 7 and the game are not reachable). notes/agents/modes.md section 8.

#include <filesystem>

#include "game/world_modes.h"
#include "game/menu_text.h"
#include "game/menu_pointer.h"
#include "game/world_nightshift.h"

namespace game {

namespace {

// 0x0049F1E0(0, i): BitBlt of entry i of 0x00598148 (0x24 bytes: dst rect, src rect, surface) to the back buffer
void Blit(World::Impl& w, int i) {
    static const char* const kFiles[4] = { "tex\\scr_etc000.bin", "tex\\scr_etc001.bin", "tex\\scr_etc002.bin", "tex\\scr_etc003.bin" };
    const uint32_t e = 0x00598148 + static_cast<uint32_t>(i) * 0x24;
    const int surf = static_cast<int>(w.exe.U32(e + 0x20));
    if (surf < 0 || surf > 3) return;
    const int dl = static_cast<int>(w.exe.U32(e)), dt = static_cast<int>(w.exe.U32(e + 4));
    const int dr = static_cast<int>(w.exe.U32(e + 8)), db = static_cast<int>(w.exe.U32(e + 12));
    WorldHud::Screen s;
    s.file = kFiles[surf];
    s.x = static_cast<float>(dl);
    s.y = static_cast<float>(dt);
    s.w = static_cast<float>(dr - dl);
    s.h = static_cast<float>(db - dt);
    s.srcX = static_cast<int>(w.exe.U32(e + 16));
    s.srcY = static_cast<int>(w.exe.U32(e + 20));
    w.hud.screens.push_back(s);
}

constexpr uint32_t kUp = 0x10, kDown = 0x20, kOk = 0xC, kBack = 2;  // P1's bits of 0x009C9028 (the lobby reads P1 only)

// Port-only connected room. Its cursor and selection are world state, so lockstep and recovery
// replay apply the same host input. The connection and frame counter outlive individual games.
void RoomFrame(World::Impl& w) {
    auto& m = *w.modes;
    m.netState = 1;
    constexpr int difficultyRow = 7, rows = 9;
    constexpr float rowStart = 5.0f, rowStep = 1.0f;
    uint32_t buttons = w.pressed & 0xFFFF;
    const auto& gun = w.input[0];
    if (gun.kind == 1 && gun.x < 640 && gun.y < 480) {
        int row = -1;
        for (int i = 0; i < rows; ++i)
            if (MenuTextRowHit(float(gun.x), float(gun.y), rowStart + i * rowStep, 184, 600)) row = i;
        // Move on aim changes, not continuously: keyboard navigation must work with a parked mouse.
        if ((gun.x != m.netAimX || gun.y != m.netAimY || gun.fired) && row >= 0)
            w.front.menuCursor = row;
        if (gun.fired && row < 0) buttons &= ~kOk;
        m.netAimX = gun.x; m.netAimY = gun.y;
    }
    if (buttons & kUp) w.front.menuCursor = (w.front.menuCursor + rows - 1) % rows;
    if (buttons & kDown) w.front.menuCursor = (w.front.menuCursor + 1) % rows;
    if ((w.pressed & 0x20002) || ((buttons & kOk) && w.front.menuCursor == rows - 1)) {
        w.cfg.portNetworkRoom = false;
        w.front.modeRequest = 3;
        w.Sound(0x500A9);
        return;
    }
    if (w.front.menuCursor == difficultyRow && (buttons & (kOk | 0x40 | 0x80))) {
        const int step = buttons & 0x40 ? -1 : 1;
        w.cfg.difficulty = (w.cfg.difficulty + step + 5) % 5;
        // This room selection travels through the same P1 lockstep inputs as
        // mode selection. Update the shared save copy too, so mode setup's
        // Store cannot restore the old difficulty; do not save local preferences.
        m.save.SetU8(SaveData::kDifficulty, static_cast<uint8_t>(w.cfg.difficulty));
        w.Sound(0x3E16A9);
        w.Note("network room: host selected difficulty " + std::to_string(w.cfg.difficulty));
    } else if ((buttons & kOk) && w.front.menuCursor < difficultyRow) {
        const int choice = w.front.menuCursor;
        w.cfg.portTyping = choice == 6;
        w.cfg.typingOriginal = true;
        w.cfg.typingOptions.difficulty = TypingDifficulty(std::clamp(w.cfg.difficulty,0,4));
        w.front.sceneSelect = choice == 2;
        w.front.sceneStart = -1;
        w.front.hordeStart = choice == 4;
        w.front.randomizerStart = choice == 5;
        w.front.gameType = (choice == 1 || choice == 5) ? 1 : (choice == 2 || choice == 3) ? 3 : 0;
        m.a2bbc = 0;
        w.cfg.freePlay = w.cfg.creditSetting < 0;
        w.cfg.credits = w.cfg.freePlay ? 0 : w.cfg.creditSetting + 1;
        w.front.modeRequest = 6;
        // Both reserved slots start together; P2 need not press Start to join again.
        w.player[0].state = w.player[1].state = 0;
        w.Sound(0x121A9);
        w.Note("network room: host selected " + std::to_string(choice));
        return;
    }
    static const std::array<const char*, rows> labels = { "ARCADE", "ORIGINAL", "SCENE SELECTION", "BOSS MODE", "HORDE MODE", "RANDOMIZER", "TYPING ORIGINAL", "", "LEAVE ROOM" };
    const std::string difficultyName = w.exe.CStr(0x00569758 + static_cast<uint32_t>(w.cfg.difficulty) * 10);
    if (w.cfg.portFeatures && w.cfg.portModernMenu) {
        NightShiftNetworkRoomFrame(w, labels, difficultyName);
        return;
    }
    game::MenuBackground(w.hudSprites);
    auto text = [&](float x, float y, const char* s, uint32_t flags) {
        MenuText(w.exe, x, y, s, flags, w.hudSprites);
    };
    text(0, 1.7f, "MULTIPLAYER LOBBY", 2);
    text(0, 3.3f, "PLAYER 1 HOST", 0x42);
    const std::string difficulty = "DIFFICULTY: " + difficultyName;
    for (int i = 0; i < rows; ++i)
        text(12, rowStart + i * rowStep, i == difficultyRow ? difficulty.c_str() : labels[i], i == w.front.menuCursor ? 0x10 : 1);
    text(0, 14.0f, w.cfg.creditSetting < 0 ? "UNLIMITED CONTINUES ON" : "UNLIMITED CONTINUES OFF", 0x42);
    text(0, 15.2f, "HOST: LEFT/RIGHT OR ENTER TO CHANGE", 0x42);
    text(0, 16.5f, "ESC LEAVES ROOM", 0x42);
}

}  // namespace

// 0x0049F380 (0x009C7019 = 0 every frame: the frame's 3D render is off while the lobby is up)
void NetFrame(World::Impl& w) {
    ModesState& m = *w.modes;
    w.hud.screens.clear();
    if (w.cfg.portNetworkRoom) { RoomFrame(w); return; }
    if (m.netState != 0) {
        Blit(w, 0xD);  // background (scr_etc002)
        Blit(w, 0xE);  // the gun strip on the right (scr_etc001)
        Blit(w, 0);    // NETWORK CONNECTION
    }
    int next = -1;
    switch (m.netState) {
    case 0: {
        // 0x0049F610: the DirectPlay object (0x004A0C40), the font, the four bitmaps; -1 (state 8) only if one of
        // scr_etc000..002 cannot be loaded
        bool ok = true;
        for (const char* f : { "tex/scr_etc000.bin", "tex/scr_etc001.bin", "tex/scr_etc002.bin" })
            ok = ok && std::filesystem::exists(std::filesystem::path(w.gameDir) / f);
        m.netSub = 0;
        if (!ok) {
            m.netState = 8;
            return;
        }
        m.netState = 1;  // (0x009C6F00 / 0x009A6000 blocks, 0x009A2D78, 0x009A5C38 / 3C, 0x009CA094 cleared)
        return;
    }
    case 1: {  // 0x0049FF40: the menu
        if (w.cfg.portFeatures) {
            // Port only: play.exe draws its own lobby (Create Room / Browse Lobby / Peer to Peer, src/port/tools/play.cpp
            // Lobby) over this screen's background and title and takes the input; the world only leaves on back,
            // which play.exe lets through from its top page (a connection leaves mode 15 through StartStage).
            if (w.pressed & kBack) {
                w.Sound(0x500A9);
                next = 8;
            }
            break;
        }
        static const int kOn[3] = { 5, 4, 6 }, kOff[3] = { 2, 1, 3 };
        for (int i = 0; i < 3; ++i) Blit(w, i == m.netCursor ? kOn[i] : kOff[i]);
        if ((w.pressed & kUp) && 0 < m.netCursor) {
            w.Sound(0x3E16A9);
            --m.netCursor;
        }
        if ((w.pressed & kDown) && m.netCursor < 2) {
            w.Sound(0x3E16A9);
            ++m.netCursor;
        }
        if (w.pressed & kOk) {
            if (m.netCursor == 0) {
                m.netCursor = 0;
                w.Sound(0x3416A9);
                next = 4;  // JOIN SESSION
                break;
            }
            if (m.netCursor == 1) {
                m.netCursor = 0;
                w.Sound(0x3416A9);
                next = 3;  // CREATE SESSION
                break;
            }
            m.netCursor = 0;  // EXIT
            w.Sound(0x500A9);
            next = 8;
            break;
        }
        if (w.pressed & kBack) {
            w.Sound(0x500A9);
            next = 8;
        }
        break;
    }
    case 3:
        // 0x0049FA20: Initialize the TCP/IP service provider (0x004A0FC0) and open a session "houseordead" (0x004A0E60):
        // the port has no DirectPlay, so the session cannot be opened -> 9 (FAILED!!). (The original on a machine
        // without the DirectPlay feature: Windows' "enable DirectPlay" prompt, then a crash when it is declined.)
        if (w.cfg.portFeatures) {
            // Port only: play.exe hosts an online session while the world holds this state (WorldHud::netState 3,
            // src/port/net/netplay.h; the codes and the status are play.exe's text over this screen); back returns
            // to the menu, a connection leaves mode 15 through play.exe's StartStage.
            Blit(w, 7);  // PLEASE WAIT!
            if (w.pressed & kBack) {
                w.Sound(0x500A9);
                next = 1;
            }
            break;
        }
        next = 9;
        break;
    case 4:  // 0x0049FA80: JOIN SESSION
        if (m.netSub == 0) {
            // an edit control for the address (hidden until the next step)
            ++m.netSub;
            break;
        }
        Blit(w, 9);     // INPUT IP ADDRESS
        Blit(w, 0x1A);  // the field's underline (the typed text 0x0049FDB0 is drawn with the font slots 0xF..0x19)
        if (w.cfg.portFeatures) {  // port only: play.exe joins an online session (the other player's code typed over this)
            if (w.pressed & kBack) {
                w.Sound(0x500A9);
                next = 1;
            }
            break;
        }
        if (w.pressed & kOk) {
            // The port has no address field: confirming stands for an address with no session behind it (the original:
            // an empty field only plays 0x500A9; a typed address that answers nothing ends in 9 = FAILED!!)
            w.Sound(0x3416A9);
            next = 9;
            break;
        }
        if (w.pressed & kBack) {
            w.Sound(0x500A9);
            next = 1;
        }
        break;
    case 6:  // 0x0049F780 (waiting for the other player): not reachable without a session
        if (w.pressed & kBack) {
            w.Sound(0x500A9);
            next = 10;
            break;
        }
        Blit(w, 7);  // PLEASE WAIT!
        break;
    case 8:
        // 0x0049F740 (bitmaps released), 0x0049F040 (DirectPlay closed); game mode 4 written directly (no Game_SetState)
        m.netState = 0;
        m.netSub = 0;
        w.hud.screens.clear();
        w.gameMode = 4;
        w.subState = 0;
        return;
    case 9:  // 0x0049F1C0: FAILED!! until any button
        Blit(w, 8);
        if (w.pressed & 0xE) next = 8;
        break;
    case 10:  // 0x0049F360: CANCEL!! until any button
        Blit(w, 10);
        if (w.pressed & 0xE) next = 8;
        break;
    default:
        break;
    }
    if (next >= 0) {
        m.netSub = 0;
        m.netState = next;
    }
}

}  // namespace game
