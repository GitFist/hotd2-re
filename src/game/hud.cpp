#include "game/hud.h"
#include "game/shooting.h"

#include <cstring>

namespace game {

namespace {

float F(uint32_t w) {
    float f;
    memcpy(&f, &w, 4);
    return f;
}

// Spr_DrawSimple 0x0041C6D0(id, x, y, depth, sx, sy, rot, flags): alpha 1, blend -1.
SpriteDef Simple(int id, float x, float y, float depth = 1.0f, float sx = 1.0f, float sy = 1.0f, uint32_t flags = 0) {
    SpriteDef d;
    d.id = id;
    d.x = x;
    d.y = y;
    d.depth = depth;
    d.sx = sx;
    d.sy = sy;
    d.rot = 0;
    d.alpha = 1.0f;
    d.blend = -1;
    d.flags = flags;
    return d;
}

const float kDepth098 = F(0x3F7AE148);  // 0.98 (digits / "x" of the compact HUD, skip prompt)

// The shared HUD definition 0x005A4CE8 (HUD_Init 0x004066D0: depth 0.9999, flags 5, alpha 1, blend -1).
SpriteDef HudDef(int id, float x, float y, float scale) {
    SpriteDef d = PressStartSprite();
    d.id = id;
    d.x = x;
    d.y = y;
    d.sx = d.sy = scale;
    return d;
}

// 0x00579F58: crosshair ids [p*4 + style].
const int16_t kCrosshairId[8] = { 0xAA8, 0xAAA, 0xAAB, 0xAAC, 0xAA9, 0xAAD, 0xAAE, 0xAAF };

// 0x004ECA20: special-weapon HUD icon {id, dx, dy} per weapon type (types 0..7).
struct WeaponIcon { int16_t id; float dx, dy; };
const WeaponIcon kWeaponIcon[8] = {
    { 0xA74, 0, 0 }, { 0xA79, 5, -12 }, { 0xA78, 0, -10 }, { 0xA76, 20, -18 },
    { 0xA74, 0, 0 }, { 0xA74, 0, 0 },   { 0xA74, 0, 0 },   { 0xA74, 0, 0 },
};

// 0x004C4B90: modes showing PRESS START (short per mode, stride 4).
bool PressStartMode(int mode) { return mode == 3 || mode == 5 || mode == 9 || mode == 10 || mode == 11; }

// 0x00577620: credit line origin per player.
const float kCreditX[2] = { 50.0f, 392.0f }, kCreditY = 412.0f;

// 0x004C4B10: credit message {dx, dy, scale, id} by CreditMessage().
struct CreditMsg { float dx, dy, scale; int id; };
const CreditMsg kCreditMsg[4] = {
    { 24, 0, F(0x3F59999A), 0x5BB }, { 0, 0, F(0x3F59999A), 0x5BC }, { 0, 0, F(0x3F59999A), 0 }, { -8, 0, F(0x3FB33333), 0x8FF } };
const float kScale085 = F(0x3F59999A);

uint32_t CreditCount(const HudState& s, int p) {  // 0x00406780
    return s.perPlayerCredits == 0 ? s.player[0].credits : s.player[p].credits;
}

// 0x00406D60(arg) with the credit slot 0x005A4D28 (= p if per-player credits, else 0).
int CreditMessage(const HudState& s, int slot, int arg) {
    const int v = s.freePlay == 1 ? 2 : s.player[slot].coinState;
    switch (v) {
        case 0: return 0;
        case 1: return (arg != 0 && s.language == 1 && (s.blinkCounter & 0x40)) ? 2 : 1;
        case 2: return 3;
        default: return arg;
    }
}

// 0x00406920(int x, int y, p): "CREDIT" + count (two digits, tens only if non-zero) or "FREE PLAY".
void CreditCountSprites(const HudState& s, int x, int y, int p, std::vector<SpriteDef>& out) {
    const int n = static_cast<int>(CreditCount(s, p));
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    if (s.freePlay == 1) {
        out.push_back(HudDef(0x332, 34.0f + fx, 15.0f + fy, kScale085));  // 0x004C4A50
        return;
    }
    // 0x004C4A40 {32, 15, 0.85, 0x22E}; digits 0x59 + d at +82 (0x004C4A60), then +12 (0x004C4A70).
    float cx = 32.0f + fx;
    const float cy = 15.0f + fy;
    out.push_back(HudDef(0x22E, cx, cy, kScale085));
    cx = 82.0f + cx;
    const int tens = (n / 10) % 10;
    if (tens != 0) out.push_back(HudDef(tens + 0x59, cx, cy, kScale085));
    cx = 12.0f + cx;
    out.push_back(HudDef(n % 10 + 0x59, cx, cy, kScale085));
}

// 0x004068A0(x, y, p): message + count.
void CreditLineSprites(const HudState& s, int slot, float x, float y, int p, std::vector<SpriteDef>& out) {
    const CreditMsg& m = kCreditMsg[CreditMessage(s, slot, 0)];
    out.push_back(HudDef(m.id, x + m.dx, y + m.dy, m.scale));
    CreditCountSprites(s, static_cast<int>(x), static_cast<int>(y), p, out);
}

// 0x00406B10 / 0x00406C30(x, y, p): "n COIN(S)" digit + text 0x230 / 0x22F, then the count.
void CoinLineSprites(const HudState& s, int32_t coins, int textId, float x, float y, int p, std::vector<SpriteDef>& out) {
    const float dx = x + 0.0f, dy = y + 0.0f;  // 0x004C4B50 / 0x004C4B70
    out.push_back(HudDef(coins % 10 + 0x59, dx, dy, kScale085));
    out.push_back(HudDef(textId, 16.0f + dx, 0.0f + dy, kScale085));  // 0x004C4B60 / 0x004C4B80
    CreditCountSprites(s, static_cast<int>(x), static_cast<int>(y), p, out);
}

}  // namespace

// --- Player HUD ---------------------------------------------------------------------------------------------

std::vector<SpriteDef> HudCrosshair(const HudState& s, int p) {
    const HudPlayer& pl = s.player[p];
    const bool gunDevice = pl.deviceMode == 6 || pl.deviceMode == 5 || pl.deviceMode == kPortWideGunMode;
    std::vector<SpriteDef> out;
    if (s.gameMode != 6) return out;
    if (!((pl.inputKind != 1 && pl.gunValid != 0) || gunDevice)) return out;
    if (pl.lives <= 0 || s.canShoot == 0) return out;
    // Definition player+0xE4: depth set to 1 here; sx/sy 1, rot 0, alpha 1, blend -1, flags 0xA from
    // Game_InitPlayers 0x0040A920 (anchor centre).
    SpriteDef d = Simple(kCrosshairId[p * 4 + (pl.crosshairStyle & 3)], 0, 0, 1.0f, 1.0f, 1.0f, 0xA);
    if (gunDevice) {
        d.x = static_cast<float>(pl.gunPosX);
        if(pl.deviceMode==kPortWideGunMode)d.x+=320-kPortWideGunBias;
        d.y = static_cast<float>(pl.gunPosY);
    } else {
        d.x = pl.aimX + 320.0f;  // 0x004C49CC
        d.y = 240.0f - pl.aimY;  // 0x004C49C8
    }
    out.push_back(d);
    return out;
}

std::vector<SpriteDef> HudAmmo(const HudState& s, int p) {
    const HudPlayer& pl = s.player[p];
    std::vector<SpriteDef> out;
    int16_t icon = 0xA74;
    float iconDx = 0, iconDy = 0;
    if (s.weaponMode == 1) {
        const WeaponIcon& w = kWeaponIcon[pl.weaponType & 7];
        icon = w.id;
        iconDx = w.dx;
        iconDy = w.dy;
    }
    const int8_t lb = s.letterbox;
    if (lb == 2 || lb == 1) {
        // Letterbox 1 (opening): scale = (40 - zoom) * 0.0125 + 1.0 (0x004ECB7C / 78 / 70), applied to everything.
        const bool zoom = lb == 1;
        // (x87 at 24-bit precision: every operation rounds to float, see shoot_camera.cpp.)
        const float scale = zoom ? (40.0f - pl.ammoZoom) * F(0x3C4CCCCD) + 1.0f : 1.0f;
        const float y = iconDy + 364.0f;  // 0x004ECB88
        const bool left = p == 0;
        const float xIcon = left ? 24.0f : 568.0f, xX = left ? 36.0f : 580.0f, xInf = left ? 50.0f : 594.0f;
        const float xTens = left ? 48.0f : 592.0f, xOnes = left ? 60.0f : 604.0f;
        bool bullets = true;
        if (s.weaponMode == 1) {
            if (pl.weaponAmmo == -1) {  // infinite: icon, "x", infinity sign 0x63
                out.push_back(Simple(icon, xIcon, y, 1.0f, scale, scale));
                out.push_back(Simple(0x65, xX, 372.0f, kDepth098, scale, scale));
                out.push_back(Simple(0x63, xInf, 373.0f, kDepth098, scale, scale));
                bullets = false;
            } else if (pl.ammo >= 7) {  // icon, "x", two digits
                out.push_back(Simple(icon, xIcon, y, 1.0f, scale, scale));
                out.push_back(Simple(0x65, xX, 372.0f, kDepth098, scale, scale));
                out.push_back(Simple(pl.ammo / 10 + 0x59, xTens, 372.0f, kDepth098, scale, scale));
                out.push_back(Simple(pl.ammo % 10 + 0x59, xOnes, 372.0f, kDepth098, scale, scale));
                bullets = false;
            }
        }
        if (bullets) {
            // Bullet spacing (dx + 24) * scale rounded to float (0x004ECB84); P1 from x 24, P2 from 592 leftwards.
            const float step = zoom ? (iconDx + 24.0f) * scale : iconDx + 24.0f;
            for (int i = 0; i < pl.ammo; ++i) {
                const float off = static_cast<float>(i) * step;
                const float x = left ? off + 24.0f : 592.0f - off;  // 0x004ECB80
                out.push_back(Simple(icon, x, y, 1.0f, scale, scale));
            }
        }
        // RELOAD (gun empty and shooting allowed): blinks 45 of 60 frames; the hint (0xA29 gun / 0xA28 other)
        // after 120 frames.
        if (pl.empty != 0 && s.canShoot != 0 && pl.reloadTimer % 60 < 45) {
            out.push_back(Simple(0xA27, left ? 24.0f : 432.0f, 260.0f, 1.0f, 1.5f, 1.0f));
            if (pl.reloadTimer >= 120) {
                const bool gun = pl.inputKind == 1;
                const float hx = left ? 16.0f : (gun ? 424.0f : 384.0f);
                out.push_back(Simple(gun ? 0xA29 : 0xA28, hx, 325.0f));
            }
        }
    }
    return out;
}

void HudAmmoAdvance(HudState& s, int p) {
    HudPlayer& pl = s.player[p];
    const int8_t lb = s.letterbox;
    if (lb == 1) pl.ammoZoom = pl.ammoZoom + 1.0f;  // 0x004C4380
    else pl.ammoZoom = 0;
    if ((lb == 2 || lb == 1) && pl.empty != 0 && s.canShoot != 0) {
        // (the reload voice 0x115A9 / 0x215A9 plays here when the timer is non-zero and the gun record is
        // valid and fired)
        const int16_t t = pl.reloadTimer;
        pl.reloadTimer = static_cast<int16_t>(t + 1);
        if (t > 600) pl.reloadTimer = 120;
    }
}

std::vector<SpriteDef> HudLives(const HudState& s, int p) {
    const HudPlayer& pl = s.player[p];
    std::vector<SpriteDef> out;
    const uint32_t c = s.torchCounter;
    if (s.letterbox == 2) {
        const bool left = p == 0;
        const int base = left ? 0x6C2 : 0x6CA;
        out.push_back(Simple(left ? 0x5B9 : 0x5BA, left ? 28.0f : 584.0f, 412.0f));
        if (s.weaponMode == 1 && pl.lives > 5) {  // torch, "x", digit
            out.push_back(Simple(static_cast<int>((c / 3) % 7) + base, left ? 60.0f : 524.0f, 412.0f));
            out.push_back(Simple(0x65, left ? 84.0f : 548.0f, 412.0f, kDepth098));
            out.push_back(Simple(pl.lives + 0x59, left ? 96.0f : 560.0f, 412.0f, kDepth098));
            return out;
        }
        for (int i = 0; i < pl.lives; ++i) {
            const int x = left ? 60 + 32 * i : 548 - 32 * i;
            out.push_back(Simple(static_cast<int>(((c + static_cast<uint32_t>(i) * 4) / 3) % 7) + base, static_cast<float>(x), 412.0f));
        }
    } else if (s.letterbox == 4 && (s.flags5900 & 0x10) == 0 && c % 60 < 45) {
        out.push_back(Simple(0x5B8, p == 0 ? 32.0f : 416.0f, 422.0f, kDepth098, 1.5f, 1.0f));
    }
    return out;
}

std::vector<SpriteDef> HudScore(const HudState& s, int p) {
    std::vector<SpriteDef> out;
    if (!s.showScores || s.gameMode != 6 || s.letterbox != 2 || s.canShoot == 0) return out;
    const HudPlayer& pl = s.player[p];
    if (pl.state != 5 || pl.lives <= 0) return out;

    const std::string digits = std::to_string(pl.score < 0 ? 0 : pl.score);
    // Keep the centre clear for boss health and Horde round text. Match the native credit glyph size;
    // right-anchor P2 so gaining another digit never pushes its score outside the safe screen margin.
    for (size_t i = 0; i < digits.size(); ++i) {
        const float x = p == 0 ? 24.0f + 12.0f * static_cast<float>(i)
                               : 616.0f - 12.0f * static_cast<float>(digits.size() - 1 - i);
        out.push_back(Simple(0x59 + digits[i] - '0', x, 8.0f, kDepth098, kScale085, kScale085, p == 0 ? 5 : 7));
    }
    return out;
}

std::vector<SpriteDef> HudPlayerTask(HudState& s, int p) {
    std::vector<SpriteDef> out = HudCrosshair(s, p);
    if (s.gameMode != 9 && s.gameMode != 5 && s.player[p].lives != 0) {
        if (s.canShoot != 0) {
            const auto a = HudAmmo(s, p);
            out.insert(out.end(), a.begin(), a.end());
            HudAmmoAdvance(s, p);
        }
        const auto l = HudLives(s, p);
        out.insert(out.end(), l.begin(), l.end());
    }
    const auto score = HudScore(s, p);
    out.insert(out.end(), score.begin(), score.end());
    return out;
}

// --- Credits ------------------------------------------------------------------------------------------------

std::vector<SpriteDef> HudCreditLine(const HudState& s, int p) {
    std::vector<SpriteDef> out;
    if (s.gameMode == 6 && s.weaponMode >= 2 && s.weaponMode <= 3) return out;
    const int slot = s.perPlayerCredits != 0 ? p : 0;  // 0x005A4D28
    int kind;
    if (s.player[p].state == 4) kind = static_cast<uint32_t>(s.coinsToContinue) > 1 ? 2 : 0;
    else kind = static_cast<uint32_t>(s.coinsPerCredit) > 1 ? 1 : 0;
    if (s.freePlay == 1) kind = 0;
    const float x = kCreditX[p], y = kCreditY;
    const int phase3 = (s.blinkCounter >> 5) % 3, phase6 = (s.blinkCounter >> 5) % 6;
    switch (kind) {  // table 0x00577640
        case 0:      // 0x00406860: hidden 1 of 3 32-frame phases
            if (phase3 != 2) CreditLineSprites(s, slot, x, y, p, out);
            break;
        case 1:  // 0x00406AA0: phases 0-1 credit, 3-4 coin line, 2 / 5 nothing
        case 2:  // 0x00406BC0
            if (phase6 == 3 || phase6 == 4) {
                if (kind == 1) CoinLineSprites(s, s.coinsPerCredit, 0x230, x, y, p, out);
                else CoinLineSprites(s, s.coinsToContinue, 0x22F, x, y, p, out);
            } else if (phase6 != 2 && phase6 != 5) {
                CreditLineSprites(s, slot, x, y, p, out);
            }
            break;
    }
    return out;
}

std::vector<SpriteDef> HudIdlePlayer(const HudState& s, int p) {
    if ((s.flags5900 & 2) && s.gameMode == 6 && s.player[p].state == 9) return HudCreditLine(s, p);
    return {};
}

// 0x00414690
std::vector<SpriteDef> HudWaitPlayer(const HudState& s, int p) {
    if (s.player[p].state != 10 || (s.flags5900 & 1) || !(s.torchCounter & 0x20)) return {};
    return { Simple(0x8E9, p ? 430.0f : 50.0f, 432.0f) };
}

// 0x0040E500 (paused)
std::vector<SpriteDef> HudPause(int pausePlayer) {
    return { Simple(pausePlayer ? 0x8E8 : 0x8E7, pausePlayer ? 480.0f : 160.0f, 240.0f, 0.6f, 1.0f, 1.0f, 0x70A) };
}

// 0x00416900 (from 0x00414420 / 0x004144C0)
std::vector<SpriteDef> HudOutPlayer(const HudState& s, int p) {
    if (s.player[p].state != 6 || s.gameMode == 7) return {};
    return { Simple(0x43E, p ? 365.0f : 45.0f, 170.0f, 1.0f, 0.6f, 0.7f) };
}

std::vector<SpriteDef> HudPressStart(const HudState& s) {
    if (PressStartMode(s.gameMode) && s.bootFrames > 3 && PressStartVisible(static_cast<uint32_t>(s.blinkCounter))) return { PressStartSprite() };
    return {};
}

// --- CONTINUE -------------------------------------------------------------------------------------------------

std::vector<SpriteDef> ContinueScreen(const HudState& s) {
    const int c = s.continueCount;
    const int digit = (c + ((c >> 31) & 0xFFF)) >> 12;  // cdq / and 0xFFF / sar 12
    return { Simple(0x22C, 128.0f, 200.0f), Simple(digit + 0x4F, 482.0f, 188.0f) };
}

std::vector<SpriteDef> ContinuePlayer(int p, int32_t count) {
    const float x = p == 0 ? 48.0f : 368.0f;  // 0x00579F68
    const float sx = F(0x3F19999A), sy = F(0x3F4CCCCD);
    const int digit = (count + ((count >> 31) & 0xFFF)) >> 12;
    return { Simple(digit + 0x4F, x + 212.0f, 160.0f, 1.0f, sx, sy, 0x700),  // 0x004ECB6C
             Simple(0x22C, x, 170.0f, 1.0f, sx, sy, 0x700) };
}

// --- Sprite effects -----------------------------------------------------------------------------------------

SpriteFx SpriteFx::Create(int id, float x, float y, float sx, float sy, float alpha, int duration, int zoomMode, int alphaMode) {
    SpriteFx f;
    f.id = id;
    f.x = x;
    f.y = y;
    f.sx = sx;
    f.sy = sy;
    f.alpha = alpha;
    f.duration = duration;
    f.zoomMode = zoomMode;
    f.alphaMode = alphaMode;
    f.frame = 0;
    f.fade = alphaMode == 0 ? alpha * F(0x3E4CCCCD)   // 0x00564428
                            : alpha / static_cast<float>(duration);
    return f;
}

SpriteDef SpriteFx::Def() const {
    // 0x00499F00(id, x, y, 1.0, sx, sy, alpha): rot 0, blend -1, flags 0xA.
    SpriteDef d = Simple(id, x, y, 1.0f, sx, sy, 0xA);
    d.alpha = alpha;
    return d;
}

bool SpriteFx::Step() {
    // fild duration; fdivr k (24-bit precision: the quotient and every sum are rounded to float).
    const float dur = static_cast<float>(duration);
    switch (zoomMode) {
        case 1: sx = sx - 1.0f / dur; sy = sy + 1.0f; break;
        case 2: { const float q = 1.0f / dur; sx = sx - q; sy = sy - q; break; }
        case 3: { const float q = 5.0f / dur; sx = sx - q; sy = sy - q; break; }  // 0x0055D2B4
        case 4: { const float q = 1.0f / dur; sx = sx + q; sy = sy + q; } [[fallthrough]];
        case 5: { const float q = 3.0f / dur; sx = sx + q; sy = sy + q; break; }  // 0x004C49C0
        default: break;
    }
    const float k01 = F(0x3DCCCCCD), k002 = F(0x3CA3D70A), k005 = F(0x3D4CCCCD), k60 = F(0x3C888889), k30 = F(0x3D088889);
    switch (alphaMode) {
        case 0:
            if (frame <= duration - 10) { ++frame; return true; }
            alpha = alpha - k01;
            break;
        case 1:
            alpha = alpha - fade;
            if (alpha < 0.0f) alpha = 0;
            break;
        case 2:
            if (frame < 50) alpha = alpha + k002;
            if (frame > duration - 20) alpha = alpha - k005;
            break;
        case 3:
        case 8: break;
        case 4:
            if (frame < 60) alpha = alpha + k60;
            if (frame > duration - 30) alpha = alpha - k30;
            break;
        case 5:
            if (frame < 10) alpha = alpha + k01;
            if (frame > duration - 10) alpha = alpha - k01;
            break;
        case 6:
            if (frame < 10) alpha = alpha + k01;
            break;
        case 7:
            if (frame > duration - 30) alpha = alpha - k30;
            break;
        default: ++frame; return true;  // never removed
    }
    if (frame > duration) return false;
    ++frame;
    return true;
}

std::vector<SpriteDef> SpriteFxList::Run() {
    std::vector<SpriteDef> out;
    std::vector<SpriteFx> live;
    for (auto& f : fx_) {
        out.push_back(f.Def());
        if (f.Step()) live.push_back(f);
    }
    fx_.swap(live);
    return out;
}

void GameOverSpawn(int t, SpriteFxList& list) {
    switch (t) {
        case 0: list.Spawn(SpriteFx::Create(0x43A, 320, 240, 1, 1, 0, 0x82, 0, 2)); break;    // "GAME OVER" fade-in
        case 0x78: list.Spawn(SpriteFx::Create(0x43B, 340, 260, 1, 1, 1, 0x14, 0, 0)); break;
        case 0x7D: list.Spawn(SpriteFx::Create(0x43C, 310, 250, 2, 1.5f, 1, 0x14, 0, 0)); break;
        case 0x82: list.Spawn(SpriteFx::Create(0x43D, 320, 230, 1, 1, 1, 0x14, 0, 0)); break;
        case 0x87: list.Spawn(SpriteFx::Create(0x43B, 290, 210, 1, 1, 1, 0x14, 0, 0)); break;
        case 0x8C: list.Spawn(SpriteFx::Create(0x43D, 350, 270, 1, 1, 1, 0x14, 0, 0)); break;
        case 0x91: list.Spawn(SpriteFx::Create(0x43A, 320, 240, 1, 1, 1, 0x14, 1, 1)); break;  // zoom-out fade
        default: break;
    }
}

// --- Chapter title card -------------------------------------------------------------------------------------

namespace {
// 0x004343C5..0x0043459B: ids 0x007DCBA0, A2, A4, A6, A8, AA, AC, AE per chapter.
const int16_t kCardIds[6][8] = {
    { 0x1FB, 0x1FC, 0x1FD, 0x1FE, 0x1FF, 0x200, 0x201, 0x202 }, { 0x203, 0x204, 0x205, 0x22A, 0x206, 0x207, 0x208, 0x209 },
    { 0x20A, 0x20B, 0x20C, 0x20D, 0x20E, 0x20F, 0x210, 0x211 }, { 0x212, 0x213, 0x214, 0x215, 0x216, 0x217, 0x218, 0x219 },
    { 0x21A, 0x21B, 0x21C, 0x21D, 0x21E, 0x21F, 0x220, 0x221 }, { 0x222, 0x223, 0x224, 0x225, 0x226, 0x227, 0x228, 0x229 } };
// 0x004345CB..0x004347C1: 0x00436AD0(x1, y1, x2, y2) per chapter.
const float kCardPos[6][4] = { { 190, 80, 400, 350 }, { 320, 80, 320, 350 }, { 350, 80, 190, 350 },
                               { 190, 80, 400, 350 }, { 320, 80, 320, 350 }, { 350, 80, 222, 350 } };
// 0x00499F00(id, x, y, 1.0, sx, sy, alpha): rot 0, blend -1, flags 0xA.
SpriteDef Fx(int id, float x, float y, float sx, float sy, float a) {
    SpriteDef d = Simple(id, x, y, 1.0f, sx, sy, 0xA);
    d.alpha = a;
    return d;
}
}  // namespace

ChapterCard ChapterCard::Start(int chapter) {
    ChapterCard c;
    c.chapter = chapter;
    return c;
}

int ChapterCard::Id(int k) const { return kCardIds[chapter][k]; }

std::vector<SpriteDef> ChapterCard::Run(int16_t language) {
    std::vector<SpriteDef> out;
    const float p1 = kCardPos[chapter][0], p2 = kCardPos[chapter][1], p3 = kCardPos[chapter][2], p4 = kCardPos[chapter][3];
    const float k30 = F(0x3D088889), k15 = F(0x3D888889), k025 = F(0x3CCCCCCD);  // 1/30, 1/15, 0.025
    const int ba0 = Id(0), ba2 = Id(1), ba4 = Id(2), ba6 = Id(3), ba8 = Id(4), baa = Id(5), bac = Id(6), bae = Id(7);
    const int c = counter;
    bool step = true;
    switch (phase) {
        case 0:  // zoom in (scales 1.5 / 4.5 / 2 / 6 shrinking to 1)
            if (c == 14) phase = 1;
            if (c <= 14) {
                s0 = s0 - k30; s0y = s0y - k30;
                sh0 = sh0 - F(0x3E6EEEEF); sh0y = sh0y - F(0x3E6EEEEF);
                s1 = s1 - k15; s1y = s1y - k15;
                sh1 = sh1 - F(0x3EAAAAAB); sh1y = sh1y - F(0x3EAAAAAB);
            }
            out.push_back(Fx(ba0, p1, p2, s0, s0y, 1));
            out.push_back(Fx(ba0, p1 + 10.0f, p2 + 10.0f, sh0, sh0y, 1));
            out.push_back(Fx(ba2, p3, p4, s1, s1y, 1));
            out.push_back(Fx(ba2, p3 + 10.0f, p4 + 10.0f, sh1, sh1y, 1));
            break;
        case 1:  // shadows grow and fade (15..45), sub-lines fade in (25..45)
            if (c >= 15 && c <= 45) {
                sh0 = sh0 + k30; sh0y = sh0y + k30; sh0a = sh0a - k30;
                if (c == 45) sh0a = 0;
                sh1 = sh1 + k30; sh1y = sh1y + k30; sh1a = sh1a - k30;
                if (c == 45) sh1a = 0;
            }
            if (c > 24 && c < 46) { a2 = a2 + k025; a3 = a3 + k025; }
            out.push_back(Fx(ba0, p1, p2, s0, s0y, 1));
            out.push_back(Fx(ba0, p1 + 10.0f, p2 + 10.0f, sh0, sh0y, sh0a));
            out.push_back(Fx(ba2, p3, p4, s1, s1y, 1));
            out.push_back(Fx(ba2, p3 + 10.0f, p4 + 10.0f, sh1, sh1y, sh1a));
            out.push_back(Fx(bae, p1 + 40.0f, p2 + 40.0f, 1, 1, a2));
            out.push_back(Fx(ba8, p3 + 50.0f, p4 - 30.0f, 1, 1, a3));
            if (c == 45) { phase = 2; counter = 46; step = false; }
            break;
        case 2:  // flicker between the variants, then fade (76..85)
            if (c < 51) {
                out.push_back(Fx(baa, p1, p2 + 20.0f, s0, s0y, 1));
                out.push_back(Fx(ba4, p3, p4 - 25.0f, s1, s1y, 1));
                out.push_back(Fx(bae, p1 + 40.0f, p2 + 40.0f, 1, 1, a2));
                out.push_back(Fx(ba8, p3 + 50.0f, p2 - 30.0f, 1, 1, a3));
            }
            if (c > 50 && c < 56) {
                out.push_back(Fx(baa, p1, p2 + 20.0f, s0, s0y, 1));
                out.push_back(Fx(ba4, p3, 325.0f, s1, s1y, 1));
                out.push_back(Fx(bac, p1 + 30.0f, p2 + 40.0f, 1, 1, 1));
                out.push_back(Fx(ba6, p3 + 20.0f, p4 - 5.0f, 1, 1, 1));
            }
            if (c > 55 && c < 61) {
                out.push_back(Fx(ba0, p1 + 20.0f, p2 + 30.0f, 1, 1, 1));
                out.push_back(Fx(ba2, p3 + 10.0f, p4 - 15.0f, 1, 1, 1));
                out.push_back(Fx(bac, p1 + 30.0f, p2 + 40.0f, 1, 1, 1));
                out.push_back(Fx(ba6, p3 + 20.0f, p4 - 5.0f, 1, 1, 1));
            }
            if (c > 60 && c < 66) {
                out.push_back(Fx(ba0, p1 + 20.0f, p2 + 30.0f, 1, 1, 1));
                out.push_back(Fx(ba2, p3 + 10.0f, p4 - 15.0f, 1, 1, 1));
                out.push_back(Fx(bac, p1, p2 + 30.0f, 1, 1, 0.5f));
                out.push_back(Fx(ba6, p3, p4 - 15.0f, 1, 1, 0.5f));
            }
            if (c > 65 && c < 71) {
                out.push_back(Fx(baa, p1, p2 + 30.0f, 1, 1, 1));
                out.push_back(Fx(ba4, p3, p4 - 15.0f, 1, 1, 1));
                out.push_back(Fx(bac, p1, p2 + 30.0f, 1, 1, 0.5f));
                out.push_back(Fx(ba6, p3, p4 - 15.0f, 1, 1, 0.5f));
            }
            if (c > 70 && c < 76) {
                const float s07 = F(0x3F333333);
                out.push_back(Fx(baa, p1, p2 + 30.0f, 1, 1, 1));
                out.push_back(Fx(ba4, p3, p4 - 15.0f, 1, 1, 1));
                out.push_back(Fx(bae, p1 + 3.0f, p2 + 33.0f, s07, s07, 1));
                out.push_back(Fx(ba8, p3 + 3.0f, p4 - 12.0f, s07, s07, 1));
            }
            if (c >= 76 && c <= 85) {
                const float s07 = F(0x3F333333);
                if (c == 76) alpha = 1.0f;
                out.push_back(Fx(ba0, p1 + 20.0f, p2 + 40.0f, 1, 1, alpha));
                out.push_back(Fx(ba2, p3 + 10.0f, p4 - 5.0f, 1, 1, alpha));
                out.push_back(Fx(bae, p1 + 3.0f, p2 + 33.0f, s07, s07, alpha));
                out.push_back(Fx(ba8, p3 + 3.0f, p4 - 12.0f, s07, s07, alpha));
                alpha = alpha - F(0x3DCCCCCD);
            }
            if (c == 85) { phase = 3; counter = 86; step = false; }
            break;
        case 3:  // hidden until 115
            if (c == 115) { phase = 4; s0 = s0y = 1; alpha = 1; counter = 116; step = false; }
            break;
        case 4:  // final: grow + fade (116..160), squash out (161..170)
            if (c > 115 && c < 161) {
                s0 = s0 + k15; s0y = s0y + k15; alpha = alpha - F(0x3CB60B61);
                const float inv = 1.0f - alpha;
                out.push_back(Fx(bac, p1 + 20.0f, p2 + 40.0f, s0, s0y, alpha));
                out.push_back(Fx(ba6, p3 + 10.0f, p4 - 5.0f, s0, s0y, alpha));
                out.push_back(Fx(bae, p1 + 10.0f, p2 + 30.0f, 1, 1, inv));
                out.push_back(Fx(ba8, p3, p4 - 20.0f, 1, 1, inv));
                if (c > 149) {
                    out.push_back(Fx(ba0, p1 + 10.0f, p2 + 30.0f, 1, 1, 1));
                    out.push_back(Fx(ba2, p3, p4 - 20.0f, 1, 1, 1));
                }
            }
            if (c >= 161 && c < 166) {
                if (c == 161) s0 = s0y = 1;
                out.push_back(Fx(bae, p1 + 10.0f, p2 + 30.0f, s0, s0y, 1));
                out.push_back(Fx(ba8, p3, p4 - 20.0f, s0, s0y, 1));
                out.push_back(Fx(ba0, p1 + 10.0f, p2 + 30.0f, 1, 1, 1));
                out.push_back(Fx(ba2, p3, p4 - 20.0f, 1, 1, 1));
                s0 = s0 - F(0x3DF5C290); s0y = s0y + F(0x3FCCCCCD);
            }
            if (c >= 166 && c <= 170) {
                if (c == 166) s0 = s0y = 1;
                out.push_back(Fx(ba0, p1 + 10.0f, p2 + 30.0f, s0, s0y, 1));
                out.push_back(Fx(ba2, p3, p4 - 20.0f, s0, s0y, 1));
                s0 = s0 - F(0x3E4AC083); s0y = s0y + F(0x404CCCCD);
            }
            break;
        default: break;
    }
    if (step) counter = static_cast<int16_t>(counter + 1);
    if (language == 1) out.push_back(Fx(0x42B + chapter, 610.0f, 86.0f, 1, 1, 1));  // 0x004345F4..
    return out;
}

// --- Route map ----------------------------------------------------------------------------------------------

std::vector<SpriteDef> RouteMapBackground(int scroll) {
    static const int16_t kBase[4] = { 0xCE, 0x119, 0x164, 0x1AF };  // 0x005679FC
    std::vector<SpriteDef> out;
    const int wrap = static_cast<int16_t>(scroll) % 0x780;
    for (int layer = 0; layer < 4; ++layer) {
        for (int row = 0; row < 15; ++row) {
            const int y = wrap + row * 32 - layer * 480;
            for (int col = 0; col < 5; ++col)
                out.push_back(Simple(kBase[layer] + row * 5 + col, static_cast<float>(col * 128), static_cast<float>(y), 120.0f));
        }
    }
    return out;
}

// --- Subtitles ----------------------------------------------------------------------------------------------

bool SubtitleTables::Load(const ExeImage& exe) {
    exe_ = &exe;
    groupCount = (0x0058B6B8 - 0x00589DA8) / 16;
    recordCount = (0x005919A8 - 0x0058BC68) / 64;
    return true;
}

int SubtitleTables::GroupForVoice(int voiceLine, int playersInGame, int gameMode) const {
    const int k = (gameMode == 10 || gameMode == 11) ? 0 : playersInGame;
    const int g = exe_->S16(0x0058B6B8 + (voiceLine * 3 + k) * 2);
    return g == 0 ? -1 : g;
}

int SubtitleTables::Duration(int group) const { return exe_->S16(0x00589DA8 + group * 16 + 2); }
int SubtitleTables::Record(int group, int line) const { return exe_->S16(0x005919A8 + (group * 4 + line) * 2); }
float SubtitleTables::RecordX(int record) const { return F(exe_->U32(0x0058BC68 + record * 64)); }
std::string SubtitleTables::RecordText(int record) const {
    char buf[60] = {};
    exe_->Read(0x0058BC68 + record * 64 + 4, buf, 58);
    return std::string(buf, strnlen(buf, 58));
}
int SubtitleTables::RecordThreshold(int record) const { return exe_->S16(0x0058BC68 + record * 64 + 0x3E); }
int16_t SubtitleTables::Glyph(int c) const { return exe_->S16(0x0055E054 + c * 2); }
int16_t SubtitleTables::ImageId(int group) const { return exe_->S16(0x00589DA8 + group * 16); }
float SubtitleTables::ImageX(int group) const { return F(exe_->U32(0x00589DA8 + group * 16 + 4)); }
float SubtitleTables::ImageY(int group) const { return F(exe_->U32(0x00589DA8 + group * 16 + 8)); }

std::vector<SpriteDef> TextLine(const SubtitleTables& t, float x0, float y, const std::string& text) {
    std::vector<SpriteDef> out;
    const float scale = F(0x3F333333);  // 0.7
    // x = 320 - len * 5.6 + x0 (0x004C49CC, 0x0055E19C), then +11.2 per character (0x0055E198).
    const int len = static_cast<int16_t>(text.size());
    float x = 320.0f - static_cast<float>(len) * F(0x40B33333);
    x = x + x0;
    for (char ch : text) {
        const int c = static_cast<signed char>(ch);
        if (c >= 'a' && c <= 'z') {
            float off;  // y offset below the capitals' line
            switch (c) {
                case 'a': case 'c': case 'e': case 'm': case 'n': case 'o': case 'r': case 's': case 'u': case 'v':
                case 'w': case 'x': case 'z': off = 3.0f; break;               // 0x004C49C0
                case 'b': case 'i': case 'l': off = 1.0f; break;               // 0x004C4380
                case 'f': case 'j': case 't': off = 2.0f; break;               // 0x004E30F0
                case 'g': off = 4.0f; break;                                   // 0x004C4CA0
                case 'p': case 'q': case 'y': off = 5.0f; break;               // 0x0055D2B4
                default: off = 0.0f; break;                                    // d h k (0x004C436C)
            }
            out.push_back(Simple(t.Glyph(c), x, off + y, 1.0f, scale, scale));
        } else if (c >= 'A' && c <= 'Z') {
            out.push_back(Simple(t.Glyph(c), x, y, 1.0f, scale, scale));
        } else if (c == '~') {
            out.push_back(Simple(0x62D, x, y, 1.0f, scale, scale));
        } else if (c >= 0) {
            const int16_t g = t.Glyph(c);
            if (g != 0) out.push_back(Simple(g, x, y, 1.0f, scale, scale));
        }
        x = x + F(0x41333333);  // 11.2 (0x0055E198)
    }
    return out;
}

bool Subtitle::Run(const SubtitleTables& t, int16_t language, bool paused, std::vector<SpriteDef>& out) {
    --countdown;
    if (countdown == 0 || paused) return false;
    if (language == 1) {
        if (t.ImageId(group) == 0) return false;
    } else if (static_cast<int16_t>(countdown) < t.RecordThreshold(t.Record(group, line))) {
        ++line;
    }
    const auto l = SubtitleSprites(t, group, line, language);
    out.insert(out.end(), l.begin(), l.end());
    return true;
}

std::vector<SpriteDef> SubtitleSprites(const SubtitleTables& t, int group, int line, int16_t language) {
    if (language == 1) {
        const int16_t id = t.ImageId(group);
        if (id == 0) return {};
        return { Simple(id, t.ImageX(group), t.ImageY(group), 1.0f, 1.0f, 1.0f, 7) };
    }
    const int r = t.Record(group, line);
    return TextLine(t, t.RecordX(r), 384.0f, t.RecordText(r));
}

// --- Attract layouts ------------------------------------------------------------------------------------------

SpriteDef ModeSelectBackSprite() { return Simple(0xAB1, 1.0f, 1.0f, 100.0f); }

SpriteDef TutorialLabel() { return Simple(0x5B9, 20.0f, 420.0f); }

std::vector<SpriteDef> TutorialTorches(int counter, int lives) {
    std::vector<SpriteDef> out;
    for (int i = 0; i < lives; ++i) out.push_back(Simple(((counter + i * 4) / 3) % 7 + 0x6C2, static_cast<float>(52 + 32 * i), 420.0f));
    return out;
}

std::vector<SpriteDef> TutorialBullets(int bullets) {
    std::vector<SpriteDef> out;
    for (int i = 0; i < bullets; ++i) out.push_back(Simple(0xA74, static_cast<float>(16 + 24 * i), 372.0f));
    return out;
}

std::vector<SpriteDef> TutorialReload(int counter, int8_t p1InputKind) {
    std::vector<SpriteDef> out;
    const float x = 24.0f, y = 260.0f;  // task +0x40 / +0x44
    if (counter % 60 < 45) out.push_back(Simple(0xA27, x, y, 1.0f, 1.5f, 1.0f));
    if (counter >= 120 && counter % 60 < 45) out.push_back(Simple(p1InputKind == 1 ? 0xA29 : 0xA28, x - 8.0f, y + 65.0f));  // 0x004C43A0 / 0x00569130
    return out;
}

SpriteDef TutorialCaption(int id, float x, float y) { return Simple(id, x, y); }

}  // namespace game
