#pragma once

// HOTD2 gameplay 2D layer: HUD (crosshair, bullets, RELOAD prompt, life torches), credit / PRESS START lines,
// CONTINUE countdown, GAME OVER sprite effects, the route-map background, cutscene subtitles and the small
// attract-screen layouts. Each function returns the Spr_Draw definitions of one original drawing function in the
// original's submission order (see notes/agents/hud.md). The caller fills HudState from the game state; every field
// names the original address it mirrors. Verified against captures of the original (tests/hud_test.cpp).

#include <cstdint>
#include <string>
#include <vector>

#include "game/exe_image.h"
#include "game/sprite.h"

namespace game {

// Per-player inputs. Player struct 0x009A5C60 + p*0x130 unless noted.
struct HudPlayer {
    int16_t state = 9;          // +0x02 player state (4 continue, 5 playing, 9 idle; table 0x00579CD0)
    int16_t lives = 0;          // +0x06
    float aimX = 0, aimY = 0;   // +0x10 / +0x14 centred aim (x right, y up; pad crosshair path)
    int16_t ammo = 0;           // +0x1C
    int16_t empty = 0;          // +0x1E gun empty flag (RELOAD prompt)
    int16_t reloadTimer = 0;    // +0x20 RELOAD blink timer (written by Hud_DrawAmmo, see HudAmmoAdvance)
    int8_t inputKind = -1;      // +0x125 input kind: 1 gun, 0 pad / keyboard cursor, -1 none
    int16_t gunValid = 0;       // gun record 0x009C8FC0 + p*0x28, +0x10 (inside the calibrated range)
    int16_t gunFired = 0;       // gun record +0x14 (only gates the reload voice, not drawn)
    int deviceMode = 0;         // Input_GetDeviceModes 0x0041E260: 0x00588E24 (P1) / 0x007DC698 (P2); 5 / 6 = gun
                                // device (crosshair = raw gun position)
    int gunPosX = 0, gunPosY = 0;  // Input_GetGunPos 0x0041E280: 0x007DBD50 / 54 (P1), 0x007DBD80 / 84 (P2), ints
    int8_t crosshairStyle = 0;  // 0x009C9F60 + p*0x7C (index into the crosshair id table 0x00579F58)
    int8_t weaponType = 0;      // special weapons (mode 0x009CA08C == 1): 0x009A2240 + p*0x14, +7
    int8_t weaponAmmo = 0;      // 0x009A2240 + p*0x14, +8 (-1 = infinite)
    float ammoZoom = 0;         // 0x007C2120[p]: frames of letterbox opening (bullets zoom in; HudAmmoAdvance)
    uint32_t credits = 0;       // 0x009C8E60 + p*8 (credit count; slot p only when 0x009C8E74 != 0)
    int coinState = 0;          // 0x009C8E64 + p*8: 0 "INSERT COIN", 1 0x5BC, 2 "PRESS START"
};

struct HudState {
    int gameMode = 0;           // 0x009C8E98 (3 title, 5 attract demo, 6 game, 7 game over / route map, 9 how-to-play)
    int8_t letterbox = 2;       // 0x009CA0F4 letterbox state (1 opening, 2 open, 3 closing, 4 bars)
    int canShoot = 0;           // 0x009C8E00 shooting allowed
    int weaponMode = 0;         // 0x009CA08C (1 special weapons; 2 / 3 hide the credit lines in mode 6)
    uint32_t torchCounter = 0;  // 0x009A32A0 (life-torch animation, skip-prompt blink)
    int32_t blinkCounter = 0;   // 0x005A4D44 HUD blink counter (updated only by Hud_DrawPressStart, = boot frame
                                // counter 0x009A5C40 at its last update)
    uint32_t bootFrames = 0;    // 0x009A5C44 (PRESS START needs > 3)
    uint32_t flags5900 = 3;     // 0x009A5900 (set to 3 by Game_InitPlayers 0x0040A920): bit 0x2 idle-player credit
                                // line, bit 0x10 hides the cutscene skip prompt
    int freePlay = 0;           // 0x009C8E70 (1: "FREE PLAY")
    int perPlayerCredits = 0;   // 0x009C8E74
    int32_t coinsPerCredit = 1;    // 0x009C8E78 (> 1: alternate with the "n COINS" line, 0x00406AA0)
    int32_t coinsToContinue = 1;   // 0x009C8E7C (> 1: same for continuing players, 0x00406BC0)
    int16_t language = 0;       // 0x009C911E (1 selects image subtitles / message 0x8FF->2 variant)
    int32_t continueCount = 0;  // 0x009A2BB8 CONTINUE countdown (0x9FFF, -0x2D per frame; digit = count >> 12)
    HudPlayer player[2];
};

// --- Player HUD (Player_Task 0x00413E90, state 5) ---------------------------------------------------------------
// 0x004169C0 Hud_UpdateCrosshair: the crosshair definition player+0xE4 (id 0x00579F58[p*4 + style]).
std::vector<SpriteDef> HudCrosshair(const HudState& s, int p);
// 0x004177D0 Hud_DrawAmmo: bullets (0xA74 or the special-weapon icon 0x004ECA20), RELOAD 0xA27 and the hint
// 0xA28 / 0xA29. Drawing only; HudAmmoAdvance applies the function's state writes afterwards.
std::vector<SpriteDef> HudAmmo(const HudState& s, int p);
void HudAmmoAdvance(HudState& s, int p);
// 0x004174A0 Hud_DrawLives: label 0x5B9 / 0x5BA and torches 0x6C2.. / 0x6CA.. (letterbox 2), or the blinking skip
// prompt 0x5B8 (letterbox 4).
std::vector<SpriteDef> HudLives(const HudState& s, int p);
// Player_Task's 2D part in order: crosshair; unless mode 5 / 9: (lives > 0) ammo if canShoot, lives. Applies
// HudAmmoAdvance. Lives / state transitions are not modelled here.
std::vector<SpriteDef> HudPlayerTask(HudState& s, int p);

// --- Credits (0x00406CE0 Hud_DrawCreditLine) ---------------------------------------------------------------------
// Called by the idle player task 0x00414600 (state 9: flags5900 & 2 and mode 6) and the continue task 0x00414280
// (state 4). Message 0x5BB / 0x5BC / 0x8FF (0x004C4B10), "CREDIT" 0x22E + digits 0x59.. or "FREE PLAY" 0x332,
// blinking with blinkCounter.
std::vector<SpriteDef> HudCreditLine(const HudState& s, int p);
// 0x00414600: the idle (state 9) player's credit line, if drawn.
std::vector<SpriteDef> HudIdlePlayer(const HudState& s, int p);
// 0x00414690 (state 10, joined during a cutscene, waiting for flags5900 bit 0): blinking 0x8E9 at (50 / 430, 432)
// while torchCounter & 0x20.
std::vector<SpriteDef> HudWaitPlayer(const HudState& s, int p);
// 0x00414420 / 0x004144C0 (state 6, continue expired) outside mode 7: 0x00416900, "GAME OVER" 0x43E at
// (0x00579F70[p] = 45 / 365, 170), scale (0.6, 0.7).
std::vector<SpriteDef> HudOutPlayer(const HudState& s, int p);
// Game_UpdatePause 0x0040E500 while paused: "PAUSE" 0x8E7 (P1, x 160) / 0x8E8 (P2, x 480) at y 240, depth 0.6,
// flags 0x70A; player = 0x007C1792. (The dark overlay, model 0x93E at alpha 0.5, is a 3D draw.)
std::vector<SpriteDef> HudPause(int pausePlayer);

// "PRESS START BUTTON" (0x004067D0): visible = (mode table 0x004C4B90[mode] != 0, bootFrames > 3, blink & 0x20 == 0).
std::vector<SpriteDef> HudPressStart(const HudState& s);

// --- CONTINUE screen (task 0x00460530, 0x009C90A4 == 4) -----------------------------------------------------------
// "CONTINUE" 0x22C at (128, 200) and the digit 0x4F + (count >> 12) at (482, 188); count before this frame's -0x2D.
std::vector<SpriteDef> ContinueScreen(const HudState& s);

// Per-player CONTINUE (continue task 0x00414280 while the global screen is not up, 0x009C90A4 != 4): "CONTINUE"
// 0x22C (0x00416880) and the digit 0x4F + (player+0x68 >> 12) (0x004168C0), scale 0.6 x 0.8, registered with
// 0x0041C800 into retained layer 2 (0x0041C760: flags |= 0x700, head insertion) and drawn by 0x0041CF30 at the end
// of the frame: returned in that draw order (digit first). count = player+0x68 after this frame's -0x2D.
std::vector<SpriteDef> ContinuePlayer(int p, int32_t count);

// --- Sprite effect objects (0x00499C60 creates task 0x00499CE0) ---------------------------------------------------
// Used by GAME OVER (0x00460CD0) and other screens (creation sites 0x004998E3..0x00499C37). Each frame: draw (depth 1, flags 0xA), zoom step
// (mode 1-5), alpha step (mode 0-8), then removed when frame > duration (modes 0-8).
struct SpriteFx {
    int id = 0;
    float x = 0, y = 0, sx = 1, sy = 1, alpha = 1;
    int duration = 0, zoomMode = 0, alphaMode = 0;
    int frame = 0;      // +0x38
    float fade = 0;     // +0x5C: alpha / duration (alpha mode != 0) or alpha * 0.2
    static SpriteFx Create(int id, float x, float y, float sx, float sy, float alpha, int duration, int zoomMode, int alphaMode);
    SpriteDef Def() const;  // 0x00499F00 definition
    bool Step();            // the update after drawing; false = removed
};

// Ordered list of live effect objects (task creation order).
class SpriteFxList {
public:
    void Spawn(const SpriteFx& fx) { fx_.push_back(fx); }
    // Draws every object, then steps them (objects removed this frame were still drawn).
    std::vector<SpriteDef> Run();
    bool Empty() const { return fx_.empty(); }
    void Clear() { fx_.clear(); }

private:
    std::vector<SpriteFx> fx_;
};

// GAME OVER sequence (task 0x00460CD0, started by 0x00460CB0): spawns at its tick t (0..165) the "GAME OVER" fade
// 0x43A (t 0) and blood splats 0x43B-0x43D (t 120..140), final 0x43A (t 145). Call once per frame with t, then
// list.Run() (the new objects draw in the same frame).
void GameOverSpawn(int t, SpriteFxList& list);

// --- Chapter title card (actor task 0x00434100, phase +0x1312; drawing 0x00436AD0, state 0x007DCAA0..) ----------
// Two title halves (ids 0x007DCBA0.. per chapter 0x009A1A08 = 0..5, set at 0x004343C0..) zoom in with shadows,
// swap through the frames 46..85, then zoom out 116..170. Each Run() is one task frame (the card's counter
// 0x007DCAB0 starts at 0 on the task's first frame); language 1 adds the image 0x42B + chapter at (610, 86).
struct ChapterCard {
    int chapter = 0;
    int16_t counter = 0, phase = 0;         // 0x007DCAB0, 0x007DCABC
    float s0 = 1.5f, s0y = 1.5f, alpha = 1;  // 0x007DCAA8 / AC (first half scale), 0x007DCAB8 (fade)
    float sh0 = 4.5f, sh0y = 4.5f, sh0a = 1; // 0x007DCAC8 / CC, 0x007DCAD8 (first half shadow)
    float s1 = 2, s1y = 2;                  // 0x007DCAE8 / EC (second half)
    float sh1 = 6, sh1y = 6, sh1a = 1;      // 0x007DCB08 / 0C, 0x007DCB18
    float a2 = 0, a3 = 0;                   // 0x007DCB38, 0x007DCB58 (subtitle-line fade-ins)
    static ChapterCard Start(int chapter);  // 0x00436A30 + the id table
    std::vector<SpriteDef> Run(int16_t language);
    int Id(int k) const;                    // 0x007DCBA0 + 2k
};

// --- Route map background (task 0x00461180, mode 7) ------------------------------------------------------------
// 4 screens of 5x15 tiles (128x32), ids 0x005679FC[layer] + row*5 + col, y = scroll % 1920 + row*32 - layer*480,
// depth 120. scroll = 0x007DCCD8 (0 at start, +4 per frame while walking, 0x004615FC).
std::vector<SpriteDef> RouteMapBackground(int scroll);

// --- Subtitles (Snd_PlayVoiceLine 0x00435B80 -> task 0x00435AA0, glyph text 0x00436850) --------------------------
struct SubtitleTables {
    bool Load(const ExeImage& exe);
    int GroupForVoice(int voiceLine, int playersInGame, int gameMode) const;  // 0x0058B6B8 (-1: no subtitle)
    int Duration(int group) const;                 // 0x00589DA8[group].+2
    int Record(int group, int line) const;         // 0x005919A8[group*4 + line]
    float RecordX(int record) const;               // 0x0058BC68[record].+0
    std::string RecordText(int record) const;      // +4
    int RecordThreshold(int record) const;         // +0x3E: next line when countdown < threshold
    int16_t Glyph(int c) const;                    // 0x0055E054[c]
    int16_t ImageId(int group) const;              // 0x00589DA8[group].+0 (language 1)
    float ImageX(int group) const, ImageY(int group) const;
    int groupCount = 0, recordCount = 0;

private:
    const ExeImage* exe_ = nullptr;
};

struct Subtitle {
    int group = 0;
    int countdown = 0;  // +0x36
    int line = 0;       // +0x38
    static Subtitle Start(const SubtitleTables& t, int group) { return { group, t.Duration(group), 0 }; }
    // One task frame (0x00435AA0): countdown-1; false (removed, nothing drawn) at 0 or when paused
    // (0x009A2D74 / 0x009A2230 != 0); else advances the line and draws it.
    bool Run(const SubtitleTables& t, int16_t language, bool paused, std::vector<SpriteDef>& out);
};
// The drawing part of 0x00435AA0 for a subtitle at group / line (language 1: the group's image instead of text).
std::vector<SpriteDef> SubtitleSprites(const SubtitleTables& t, int group, int line, int16_t language);

// 0x00436850: a text line in glyph sprites (0x0055E054), scale 0.7, centred on 320 + x, lowercase baseline offsets.
std::vector<SpriteDef> TextLine(const SubtitleTables& t, float x, float y, const std::string& text);

// --- Attract layouts ---------------------------------------------------------------------------------------------
// 0x00496890 task (title -> game start, frames after START): AB1 at (1, 1), depth 100.
SpriteDef ModeSelectBackSprite();

// How-to-play screen (mode 9, script 0x00499740.., tasks below). counter = the task's own frame counter (+0x34,
// 0 on its first frame).
SpriteDef TutorialLabel();                                         // 0x0049AD70: 0x5B9 at (20, 420)
std::vector<SpriteDef> TutorialTorches(int counter, int lives);    // 0x0049AB80: 0x7DD160 torches at 52 + 32 i
std::vector<SpriteDef> TutorialBullets(int bullets);               // 0x0049AA00: 0x7DD150 bullets at 16 + 24 i
std::vector<SpriteDef> TutorialReload(int counter, int8_t p1InputKind);  // 0x0049AC40 (0x7DD158 == 1)
SpriteDef TutorialCaption(int id, float x, float y);               // 0x0049ADC0 (ids 0xA69..0xA73)

}  // namespace game
