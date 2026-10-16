// game::World - original mode (game type 1): the item select (actor type 0x6E, 0x00488820, spawned by st1evtbl scene 0
// variant 5), the players' items (0x009A2240 + p * 0x14: 0x0048A0D0 init, 0x00415FE0 apply, 0x0048A140 game setup).
// notes/agents/modes.md section 7; verified against build\scene_capture_modes_orig.txt.

#include <cstring>

#include "game/mstack.h"
#include "game/shoot_camera.h"
#include "game/world_modes.h"
#include "game/world_menu_pointer.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}
float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}
uint8_t B8(const ExeImage& exe, uint32_t va) {
    uint8_t b = 0;
    exe.Read(va, &b, 1);
    return b;
}

void Spr(World::Impl& w, int id, float x, float y, float depth, float sx = 1, float sy = 1, int rot = 0, uint32_t flags = 0,
         const float* tint = nullptr) {
    SpriteDef d;
    d.id = id;
    d.x = x;
    d.y = y;
    d.depth = depth;
    d.sx = sx;
    d.sy = sy;
    d.rot = rot;
    d.flags = flags;
    d.source = 0x00489830;
    if (tint) {
        d.tinted = true;
        d.tint[0] = tint[0];
        d.tint[1] = tint[1];
        d.tint[2] = tint[2];
    }
    w.hudSprites.push_back(d);
}

struct ItemSelect : WTask {  // 0x6E object
    int16_t step = 0;        // +0x1310
    int8_t pig = 0;          // +0x131C players in game when last seen
    int active[2] = {};      // +0x1320 / +0x1324 the player still chooses
    int state[2] = {};       // +0x1328 / +0x132C the player's state when last seen
    int mode[2] = {};        // +0x1330 / +0x1334 (0 the stock list, 1 the slots)
    int scroll = 0;          // +0x1338 (shared list scroll)
    int cam = 0;             // +0x133C camera path tick
    int row[2] = {};         // +0x1350 / +0x1354 cursor in the list
    int slotCur[2] = {};     // +0x1358 / +0x135C cursor on the slots (0, 1, 2 = OK)
    int warnTimer[2] = {};   // +0x1360 / +0x1364
    int warn[2] = {};        // +0x1368 / +0x136C ("cannot combine")
    int rep[2] = {};         // +0x11E (P1, short) / +0x121 (P2, byte): L / R held frames
    int repDir[2] = {};      // +0x11C / +0x120
    MenuPointerFocus pointer[2];
    int pointerItem[2] = {-1, -1};  // snapshot the item aimed at before either player's purchase changes the list
};

// 0x004896B0: the items in stock, in item order, -1 padded
void ListBuild(ModesState& m) {
    int n = 0;
    for (int i = 0; i < 0x21; ++i)
        if (0 < m.items[i]) m.itemList[n++] = static_cast<int8_t>(i);
    for (; n < 0x21; ++n) m.itemList[n] = -1;
}

int8_t& Inv(ModesState& m, int item) { return m.items[item < 0 || item > 32 ? 0 : item]; }

// 0x0056B014[cat(a) * 13 + cat(b)] (categories 0x0056AFF0): may the two items be combined
bool Compatible(World::Impl& w, int a, int b) {
    const int ca = static_cast<int8_t>(B8(w.exe, 0x0056AFF0 + static_cast<uint32_t>(a)));
    const int cb = static_cast<int8_t>(B8(w.exe, 0x0056AFF0 + static_cast<uint32_t>(b)));
    return B8(w.exe, 0x0056B014 + static_cast<uint32_t>(ca * 13 + cb)) != 0;
}

// 0x00489700: a player joining / leaving re-arms its column; going to two players returns every chosen item
void Players(World::Impl& w, ItemSelect& s) {
    ModesState& m = *w.modes;
    for (int p = 0; p < 2; ++p)
        if (s.state[p] != w.player[p].state) {
            s.active[p] = 1;
            s.state[p] = w.player[p].state;
        }
    if (w.playersInGame != s.pig && w.playersInGame == 2) {
        for (int p = 0; p < 2; ++p) {
            for (int k = 0; k < 2; ++k) {
                int8_t& sl = m.pitems[p].slot[k];
                if (sl != -1) {
                    ++Inv(m, sl);
                    sl = -1;
                }
            }
            s.mode[p] = s.row[p] = s.slotCur[p] = s.warnTimer[p] = s.warn[p] = 0;
        }
        ListBuild(m);
    }
    s.pig = static_cast<int8_t>(w.playersInGame);
}

int LastItemScroll(const ModesState& m) {
    int n = 0;
    while (n < 33 && m.itemList[n] >= 0) ++n;
    return n > 7 ? n - 7 : 0;
}

void ItemPointer(World::Impl& w, ItemSelect& s) {
    if (!w.cfg.portFeatures) return;
    const int visibleScroll = s.scroll;
    for (int p = 0; p < 2; ++p) {
        s.pointerItem[p] = -1;
        const auto& in = w.input[p];
        if (in.kind == 1 && in.fired) w.pressed &= ~(12u << (p * 16));
        if (w.player[p].state != 5 || !MenuAim(in)) continue;
        if (in.fired && kSelectBack.Hit(in.x, in.y)) { w.pressed |= 2u << (p * 16); continue; }
        if (!s.active[p] || s.warn[p]) continue;
        if (!s.pointer[p].Update(true, float(in.x), float(in.y), in.fired)) continue;
        if (in.fired && MenuRect{144, 32, 216, 72}.Hit(in.x, in.y)) {
            if (s.scroll > 0) { --s.scroll; w.Sound(0x100A9); }
            continue;
        }
        if (in.fired && MenuRect{144, 294, 216, 318}.Hit(in.x, in.y)) {
            if (s.scroll < LastItemScroll(*w.modes)) { ++s.scroll; w.Sound(0x100A9); }
            continue;
        }
        auto& it = w.modes->pitems[p];
        if (MenuRect{40, 74, 308, 294}.Hit(in.x, in.y)) {
            const int row = (in.y - 74) / 32;
            const int free = it.slot[0] == -1 ? 0 : w.playersInGame != 2 && it.slot[1] == -1 ? 1 : -1;
            const int item = w.modes->itemList[(in.fired ? visibleScroll : s.scroll) + row];
            if (free >= 0 && item >= 0) {
                s.mode[p] = 0; s.row[p] = row; s.slotCur[p] = free;
                if (in.fired) {
                    s.pointerItem[p] = item;
                    w.pressed |= 4u << (p * 16);
                }
            }
            continue;
        }
        const int slotY = w.playersInGame == 2 ? 114 + p * 128 : 159;
        const int endY = w.playersInGame == 2 ? 154 + p * 128 : 212;
        if (MenuRect{518, float(endY), 594, float(endY + 28)}.Hit(in.x, in.y)) {
            s.mode[p] = 1; s.slotCur[p] = 2;
            if (in.fired) w.pressed |= 8u << (p * 16);
        } else {
            const int slots = w.playersInGame == 2 ? 1 : 2;
            for (int k = 0; k < slots; ++k)
                if (MenuRect{364, float(slotY + k * 26), 594, float(slotY + (k + 1) * 26)}.Hit(in.x, in.y)) {
                    s.mode[p] = 1; s.slotCur[p] = k;
                    if (in.fired) w.pressed |= 4u << (p * 16);
                }
        }
    }
}

// 0x00489830: the screen
void Draw2D(World::Impl& w, ItemSelect& s) {
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    Spr(w, 0x5E6, 320.0f, 374.0f, 1.0f, 1, 1, 0, 10);
    Spr(w, 0x5E2, 126.0f, 24.0f, 1.0f);
    Spr(w, 0x5F3, 32.0f, 56.0f, 3.0f, Bits(0x3F99999A), 1.0f);
    for (int i = 0, y = 0x52; y < 0x132; y += 0x20, ++i) {
        const int item = m.itemList[(s.scroll + i) % 33];
        if (item == -1) continue;
        Spr(w, exe.S16(0x0059721C + static_cast<uint32_t>(item) * 2), 43.0f, static_cast<float>(y - 4), 1.0f);
        const int n = Inv(m, item);
        if (1 < n) {
            Spr(w, 0x5F2, 261.0f, static_cast<float>(y + 7), 1.0f, Bits(0x3F333333), Bits(0x3F333333));
            if (0 < n / 10) Spr(w, n / 10 + 0x5E8, 273.0f, static_cast<float>(y), 1.0f);
            Spr(w, n % 10 + 0x5E8, 287.0f, static_cast<float>(y), 1.0f);
        }
    }
    auto tintAt = [&](uint32_t va, float t[3]) {
        for (int k = 0; k < 3; ++k) t[k] = F32(exe, va + 4 * static_cast<uint32_t>(k));
    };
    float t[3];
    if (s.active[0] != 0 && s.mode[0] == 0) {
        tintAt(0x0056B0C0, t);
        Spr(w, 0x5F5, 40.0f, static_cast<float>(s.row[0] * 0x20 + 0x4A), 2.0f, Bits(0x3F99999A), 1.25f, 0, 0x2000, t);
    }
    if (s.active[1] != 0 && s.mode[1] == 0) {
        tintAt(0x0056B0CC, t);
        Spr(w, 0x5F5, 42.0f, static_cast<float>(s.row[1] * 0x20 + 0x4C), Bits(0x40066666), Bits(0x3F99999A), 1.25f, 0, 0x2000, t);
    }
    if (w.seedCounter % 0x3C < 0x2D) {
        tintAt(0x0056B0D8, t);
        if (0 < s.scroll) Spr(w, 0x5F7, 164.0f, 44.0f, 1.0f, 1, 1, 0, 0x2020, t);
        if (s.scroll < (w.cfg.portFeatures ? LastItemScroll(m) : 0x1A)) Spr(w, 0x5F7, 164.0f, 295.0f, 1.0f, 1, 1, 0, 0x2000, t);
    }
    const int pig = w.playersInGame;
    const float grey[3] = { Bits(0x3E99999A), Bits(0x3E99999A), Bits(0x3E99999A) };
    auto slotSprite = [&](int item, float x, float y, uint32_t f) {
        Spr(w, exe.S16(0x0059721C + static_cast<uint32_t>(item) * 2), x, y, 1.0f, 1, 1, 0, f, f ? grey : nullptr);
    };
    if (pig == 0 || pig == 1) {
        Spr(w, 0x5F8, 334.0f, 196.0f, 1.0f, 1, 1, 0, 10);
        Spr(w, 0x5E4, 456.0f, 78.0f, 1.0f, 1, 1, 0, 10);
        Spr(w, 0x5E5, 470.0f, 104.0f, 1.0f, 1, 1, 0, 10);
        const uint32_t f = s.active[pig] == 0 ? 0x2000u : 0u;
        Spr(w, pig * 2 + 0x5F4, 352.0f, 120.0f, 3.0f, 1, 1, 0, f, f ? grey : nullptr);
        const ModesState::PlayerItems& it = m.pitems[pig];
        if (it.slot[0] != -1) slotSprite(it.slot[0], 365.0f, 159.0f, f);
        if (it.slot[1] != -1) slotSprite(it.slot[1], 365.0f, 185.0f, f);
        if (s.active[pig] != 0 && s.mode[pig] == 1) {
            tintAt(0x0056B0C0 + static_cast<uint32_t>(pig) * 12, t);
            Spr(w, 0x5F5, 365.0f, static_cast<float>(s.slotCur[pig] * 0x1A + 0x9F), 2.0f, Bits(0x3F7851EC), 1.0f, 0, 0x2000, t);
        }
        Spr(w, 0x5E7, 526.0f, 212.0f, 1.0f, 1, 1, 0, f, f ? grey : nullptr);
    } else if (pig == 2) {
        Spr(w, 0x5F8, 333.0f, 142.0f, 1.0f, 1, 1, 0x1000, 10);
        Spr(w, 0x5F8, 333.0f, 260.0f, 1.0f, 1, 1, 0xF000, 10);
        Spr(w, 0x5E4, 460.0f, 48.0f, 1.0f, 1, 1, 0, 10);
        uint32_t f = s.active[0] == 0 ? 0x2000u : 0u;
        Spr(w, 0x5F4, 352.0f, 62.0f, 3.0f, 1, 1, 0, f, f ? grey : nullptr);
        if (m.pitems[0].slot[0] != -1) slotSprite(m.pitems[0].slot[0], 365.0f, 114.0f, f);
        if (s.active[0] != 0 && s.mode[0] == 1 && (s.slotCur[0] == 0 || s.slotCur[0] == 2)) {
            tintAt(0x0056B0C0, t);
            Spr(w, 0x5F5, 365.0f, s.slotCur[0] == 0 ? 114.0f : 153.0f, 2.0f, Bits(0x3F7851EC), 1.0f, 0, 0x2000, t);
        }
        Spr(w, 0x5E7, 526.0f, 154.0f, 1.0f, 1, 1, 0, f, f ? grey : nullptr);
        f = s.active[1] == 0 ? 0x2000u : 0u;
        Spr(w, 0x5F6, 352.0f, 190.0f, 3.0f, 1, 1, 0, f, f ? grey : nullptr);
        if (m.pitems[1].slot[0] != -1) slotSprite(m.pitems[1].slot[0], 365.0f, 242.0f, f);
        if (s.active[1] != 0 && s.mode[1] == 1 && (s.slotCur[1] == 0 || s.slotCur[1] == 2)) {
            tintAt(0x0056B0CC, t);
            Spr(w, 0x5F5, 365.0f, s.slotCur[1] == 0 ? 242.0f : 281.0f, 2.0f, Bits(0x3F7851EC), 1.0f, 0, 0x2000, t);
        }
        Spr(w, 0x5E7, 526.0f, 282.0f, 1.0f, 1, 1, 0, f, f ? grey : nullptr);
    }
}

// one player's column (P1 bits / P2 bits << 16)
void Column(World::Impl& w, ItemSelect& s, int p) {
    ModesState& m = *w.modes;
    ModesState::PlayerItems& it = m.pitems[p];
    const int sh = p * 16;
    const uint32_t pr = w.pressed >> sh, hd = w.held >> sh;
    const int single = p;  // the players-in-game value of this player alone
    if (s.warn[p] != 0) {
        if (0x3C < s.warnTimer[p]++) {
            s.warn[p] = 0;
            s.warnTimer[p] = 0;
        }
        float y;
        if (w.playersInGame == single) y = 122.0f;
        else if (w.playersInGame == 2) y = p == 0 ? 64.0f : 192.0f;
        else return;
        Spr(w, 0x5DF, 396.0f, y, Bits(0x3F666666));
        return;
    }
    if (pr & 0x80) {
        s.mode[p] = 1;
    } else if (pr & 0x40) {
        if (w.playersInGame == single) {
            if (it.slot[0] == -1) {
                s.slotCur[p] = 0;
                s.mode[p] = 0;
            } else if (it.slot[1] == -1) {
                s.slotCur[p] = 1;
                s.mode[p] = 0;
            }
        } else if (w.playersInGame == 2 && it.slot[0] == -1) {
            s.slotCur[p] = 0;
            s.mode[p] = 0;
        }
    }
    uint32_t sound = 0;
    if (s.mode[p] == 0) {
        if ((hd & 0x30) == 0) {
            s.rep[p] = 0;
            s.repDir[p] = 0;
        } else if (0x28 < s.rep[p]++) {
            s.rep[p] = 0;
            if (hd & 0x20) s.repDir[p] = 1;
            else if (hd & 0x10) s.repDir[p] = 2;
        }
        const bool tick = w.seedCounter % 3 == 1;
        if ((pr & 0x20) || (s.repDir[p] == 1 && tick)) {
            if (6 < ++s.row[p]) {
                s.row[p] = 6;
                if (0x19 < s.scroll) return;
                ++s.scroll;
            }
            sound = 0x100A9;
        } else if ((pr & 0x10) || (s.repDir[p] == 2 && tick)) {
            if (--s.row[p] < 0) {
                s.row[p] = 0;
                if (s.scroll < 1) return;
                --s.scroll;
            }
            sound = 0x100A9;
        } else if (pr & 4) {
            const int item = s.pointerItem[p] >= 0 ? s.pointerItem[p] : m.itemList[(s.scroll + s.row[p]) % 33];
            sound = 0x200A9;
            if (item != -1 && 0 < Inv(m, item)) {
                if (s.slotCur[p] == 0) {
                    it.slot[0] = static_cast<int8_t>(item);
                    w.Sound(0x300A9);
                    --Inv(m, item);
                    ListBuild(m);
                    if (w.playersInGame == 2) {
                        s.mode[p] = 1;
                        s.slotCur[p] = 2;
                    } else {
                        s.slotCur[p] = 1;
                    }
                    return;
                }
                if (s.slotCur[p] != 1) return;
                if (Compatible(w, item, it.slot[0])) {
                    it.slot[1] = static_cast<int8_t>(item);
                    w.Sound(0x300A9);
                    --Inv(m, item);
                    ListBuild(m);
                    s.mode[p] = 1;
                    s.slotCur[p] = 2;
                    return;
                }
                s.warnTimer[p] = 0;
                s.warn[p] = 1;
            }
        } else {
            return;
        }
    } else {
        s.rep[p] = 0;
        s.repDir[p] = 0;
        if (pr & 0x20) {
            if (++s.slotCur[p] == 1 && w.playersInGame == 2) s.slotCur[p] = 2;
            if (2 < s.slotCur[p]) s.slotCur[p] = 0;
            sound = 0x100A9;
        } else if (pr & 0x10) {
            if (--s.slotCur[p] == 1 && w.playersInGame == 2) s.slotCur[p] = 0;
            if (s.slotCur[p] < 0) s.slotCur[p] = 2;
            sound = 0x100A9;
        } else if ((pr & 4) && s.slotCur[p] != 2) {
            if (s.slotCur[p] == 0) {
                if (it.slot[0] != -1) {
                    ++Inv(m, it.slot[0]);
                    ListBuild(m);
                    if (w.playersInGame == 2 || it.slot[1] == -1) {
                        it.slot[0] = -1;
                        w.Sound(0x400A9);
                    } else {
                        it.slot[0] = it.slot[1];
                        it.slot[1] = -1;
                        w.Sound(0x400A9);
                        s.slotCur[p] = 1;
                    }
                    s.mode[p] = 0;
                }
            } else if (s.slotCur[p] == 1 && it.slot[1] != -1) {
                const int v = it.slot[1];
                it.slot[1] = -1;
                ++Inv(m, v);
                w.Sound(0x400A9);
                ListBuild(m);
                s.mode[p] = 0;
            }
            return;
        } else if ((pr & 8) && s.slotCur[p] == 2) {
            s.active[p] = 0;
            sound = 0x121A9;
        } else {
            return;
        }
    }
    w.Sound(sound);
}

// models 0x145F (the case) / 0x1460 (its lid, opening from camera tick 0x5A)
void CaseDraw(World::Impl& w, int cam) {
    Mat4 t = Mat4::Identity();
    mx::Translate(t, Bits(0xC40F0666), 0.0f, Bits(0x43833333));
    mx::RotY(t, 0x52D8);
    w.Draw(0x145F, t);
    mx::Translate(t, 0.0f, Bits(0x412522D1), Bits(0xC16B9D49));
    if (0x59 < cam) mx::RotX(t, (cam - 0x5A) * 0x100);
    w.Draw(0x1460, t);
}

// 0x00415FE0(p): the effect of the player's two items
void ItemsApply(World::Impl& w, int p) {
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    ModesState::PlayerItems& it = m.pitems[p];
    auto row = [&](int k, int off) { return B8(exe, 0x004EC928 + static_cast<uint32_t>(k * 8 + off)); };
    auto rowF = [&](int k) { return F32(exe, 0x004EC92C + static_cast<uint32_t>(k * 8)); };
    for (int i = 0; i < 2; ++i) {
        const int item = it.slot[i];
        switch (item) {
        case 0: case 1: case 2:
            it.weapon = static_cast<int8_t>(item + 1);
            for (int k = 0; k < 4; ++k) it.b8[k] = row(item + 1, k);
            it.damage = rowF(item + 1);
            break;
        case 3: case 4: case 5:
            it.damage = F32(exe, 0x004EC934 + static_cast<uint32_t>(item) * 8);
            it.b8[3] = B8(exe, 0x004EC933 + static_cast<uint32_t>(item) * 8);
            break;
        case 6:
            it.b8[1] = 4;
            it.damage = F32(exe, 0x004EC964);
            it.b8[3] = B8(exe, 0x004EC963);
            it.b8[2] = B8(exe, 0x004EC962);
            break;
        case 7: case 8: case 9: case 10:
            it.b8[0] = B8(exe, 0x004EC930 + static_cast<uint32_t>(item) * 8);
            break;
        case 0xB:
            it.weapon = 0xC;
            it.b8[0] = B8(exe, 0x004EC988);
            it.b8[2] = B8(exe, 0x004EC98A);
            break;
        case 0xC:
            it.weapon = 0xD;
            it.b8[2] = B8(exe, 0x004EC992);
            it.credits = 5;
            m.itemFlags[4] = 1;  // 0x009C88AC
            break;
        case 0x14:
            m.itemFlags[4] = 1;
            break;
        case 0xD:
            it.b8[1] = 5;
            it.b8[2] = B8(exe, 0x004EC99A);
            break;
        case 0xE: it.lives = 5; break;
        case 0xF:
            it.lives = 8;
            it.b5 = 8;
            break;
        case 0x10: it.credits = 2; break;
        case 0x11: it.credits = 5; break;
        case 0x12: it.credits = 10; break;
        case 0x13: it.credits = -1; break;
        case 0x15: m.itemFlags[0] = 1; break;  // 0x009C88A8
        case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B:
            it.character = static_cast<int8_t>(item - 0x14);
            break;
        case 0x1C: {
            int r = w.Rand(0x004160FB);
            r = r % 2;  // (signed remainder; rand() is never negative)
            it.character = static_cast<int8_t>(r + 8);
            break;
        }
        case 0x1D: m.itemFlags[1] = 1; break;
        case 0x1E: m.itemFlags[2] = 1; break;
        case 0x1F: m.itemFlags[3] = 1; break;
        case 0x20: it.b3 = 2; break;
        default: break;
        }
    }
    if (it.weapon == 0xC) {
        it.b8[0] = B8(exe, 0x004EC988);
        for (int i = 0; i < 2; ++i) {
            const int item = it.slot[i];
            if (6 < item && item < 10) it.b8[0] = static_cast<uint8_t>(it.b8[0] + B8(exe, 0x004EC930 + static_cast<uint32_t>(item) * 8) - 6);
            else if (item == 10) it.b8[0] = B8(exe, 0x004EC980);
        }
    }
    if (it.weapon == 0xD) {
        it.credits = 5;
        for (int i = 0; i < 2; ++i) {
            switch (it.slot[i]) {
            case 0x10: it.credits = static_cast<int8_t>(it.credits + 2); break;
            case 0x11: it.credits = static_cast<int8_t>(it.credits + 5); break;
            case 0x12: it.credits = static_cast<int8_t>(it.credits + 10); break;
            case 0x13: it.credits = -1; break;
            default: break;
            }
        }
    }
}

// 0x0048A140: the game after the item select (pause allowed, rank, the players' lives / ammo / counters from their
// items, credits)
void ItemsGameSetup(World::Impl& w) {
    ModesState& m = *w.modes;
    w.flags5900 = (w.flags5900 & ~0x38u) | 2;
    w.attackers = 0;
    w.rank = static_cast<int8_t>(w.exe.S16(0x005679F4 + static_cast<uint32_t>(w.cfg.difficulty)) & 0xFF);  // 0x00460770
    w.rankTimerOn = true;
    w.rankAdj = 0;
    w.rankTimer = 1;
    w.rank += w.players * 4;
    for (int p = 0; p < 2; ++p) {
        World::Impl::Player& P = w.player[p];
        const ModesState::PlayerItems& it = m.pitems[p];
        if (P.state != 5) continue;
        P.lives = P.livesShown = it.lives;
        P.ammo = static_cast<int16_t>(static_cast<int8_t>(it.b8[0]) == -1 ? 6 : static_cast<int8_t>(it.b8[0]));
        for (int k = 0; k < 4; ++k)  // 0x00416420
            m.pitems[p].b10[k] = B8(w.exe, 0x00579ED8 + static_cast<uint32_t>(it.weapon * 8 + k));
        P.streak = P.shots = P.hits = 0;
        if (it.b8[1] == 5) w.ProcessRequest({ 0x50, 0x109D });
        if (it.b8[1] == 3) w.ProcessRequest({ 0x52, 0x3D });
        if (it.b8[1] == 4) w.ProcessRequest({ 0x52, 0x41 });
        if (it.character != p) {
            // 0x0048A1FD: the body (player +0x78) gets the item character's skeleton 0x39 + c (c 8 / 9: 0x21 / 0x34) and a
            // new pose (0x00410440; its old block freed by 0x004A7510), rotation order 1, node callback 0x00416570, actor
            // slot 14 + p (0x009C88C0[+0x3C]; the slot 0x00410440 registers is released by 0x004092D0)
            if (EnemyTask* b = w.playerBody[p].get()) {
                const int c = it.character;
                b->anim.kind = static_cast<int16_t>(c <= 7 ? 0x39 + c : c == 8 ? 0x21 : 0x34);
                if (b->actorSlot >= 0 && b->actorSlot < 14) w.actorSlots[b->actorSlot] = nullptr;
                w.EnemyInitPose(*b);
                if (b->actorSlot >= 0 && b->actorSlot < 14) w.actorSlots[b->actorSlot] = nullptr;
                b->actorSlot = 14 + p;
                b->anim.rotOrder = 1;
            }
        }
    }
    if (m.pitems[0].credits == -1 || m.pitems[1].credits == -1) {  // 0x00407000: free play
        w.cfg.freePlay = true;
        w.cfg.credits = 0;
    } else {  // 0x00406FA0 twice: credits added (at most 99)
        for (int p = 0; p < 2; ++p) {
            w.cfg.credits += m.pitems[p].credits;
            if (99 < w.cfg.credits) w.cfg.credits = 99;
        }
    }
    // 0x0041D440, 0x004040A0, 0x00412FD0, flush 0x0041D970
    memset(w.komono40Intact, 1, sizeof(w.komono40Intact));  // 0x00463680
}

void TaskItemSelect(Task& tk) {
    World::Impl& w = *CurrentWorld();
    ItemSelect& s = tk.As<ItemSelect>();
    ModesState& m = *w.modes;
    CameraDirector& c = w.runner->camera;
    if (s.fn == 0x004895C0) {  // the choice is made: the game starts
        Draw2D(w, s);
        CaseDraw(w, s.cam);
        w.Sound(0x80000000);
        w.attackers = 0;
        ItemsApply(w, 0);
        ItemsApply(w, 1);
        ItemsGameSetup(w);
        w.runner->yield = false;  // 0x009C8EA0
        w.runner->variant = 1;    // 0x009A2BB0 + Evt_GetScript
        w.runner->script = w.mem.Script(w.runner->mode, w.runner->scene, 1);
        // 0x004015A0(section, 0): the section's resources released
        w.section = 0;  // 0x009A2224
        w.ts.KillCurrent();
        return;
    }
    bool camStep = false;
    if (s.step == 0) {
        // textures 0x156 / 0x1B5 / 0x15F, flush, 0x00413DA0
        w.ProcessRequest({ 0x54, 0x156 });
        w.ProcessRequest({ 0x54, 0x1B5 });
        w.ProcessRequest({ 0x54, 0x15F });
        w.front.dimCounter = 0;
        w.Sound(0x10000028);
        for (int i = 0; i < 33; ++i) m.items[i] = m.save.S8(SaveData::kItems + static_cast<uint32_t>(i));  // 0x009C9F3D -> 0x009C90C0
        w.flags5900 |= 0x18;
        m.a2bbc = 0;
        w.attackers = 1;
        s.pig = static_cast<int8_t>(w.playersInGame);
        s.cam = 0;
        s.scroll = 0;
        for (int p = 0; p < 2; ++p) {
            s.active[p] = w.player[p].state == 5 ? 1 : 0;
            s.state[p] = w.player[p].state;
            s.mode[p] = s.row[p] = s.slotCur[p] = s.warnTimer[p] = s.warn[p] = 0;
            s.rep[p] = s.repDir[p] = 0;
        }
        ItemsInit(w);  // 0x0048A0D0
        ListBuild(m);
        c.forceState6 = true;  // 0x009CA094
        s.step = 1;
        camStep = true;
    } else if (s.step == 1) {
        camStep = true;
    } else if (s.step == 2) {
        s.step = 3;
    }
    if (camStep) {
        if (w.pressed & 0x40004) s.cam = 0x8C;  // a trigger skips the camera
        CameraContext& x = c.ctx[c.activeCtx & 3];
        float pos[3], tg[3];
        int roll = 0;
        if (w.cameraBank.Evaluate(0x36, static_cast<float>(s.cam), pos, tg, &roll, false)) {  // Cam_Evaluate 0x004041E0
            memcpy(x.pos, pos, 12);
            memcpy(x.target, tg, 12);
        }
        ContextLookAngles(x, x.target, x.roll);  // 0x00403AC0
        if (s.cam == 0x5A) w.Sound(0x1700A9);
        if (0x8B < s.cam++) {
            c.forceState6 = false;
            ++s.step;
        }
    }
    ModesDimCount(w);  // 0x00413CC0
    CaseDraw(w, s.cam);
    Players(w, s);
    if (s.step < 3) return;
    ItemPointer(w, s);
    // port only (WorldConfig::portFeatures): back to the mode select with the selects' back mask 0x00589170 (reload /
    // Backspace), as the boss / lesson selects do; the original has no way out of the item select
    const uint32_t backMask = w.cfg.portNetworkRoom ? 0x20002 : w.exe.U32(0x00589170 + static_cast<uint32_t>(w.playersInGame) * 4);
    if (w.cfg.portFeatures && (w.pressed & backMask)) {
        w.Sound(0x500A9);
        w.FrontAfterStart();  // 0x00406FD0 credits, 0x0049DFD0, 0x0040AC10
        w.front.modeRequest = 4;
        w.ts.KillCurrent();
        return;
    }
    if (s.active[0] != 0) Column(w, s, 0);
    if (s.active[1] != 0) Column(w, s, 1);
    Draw2D(w, s);
    if (w.cfg.portFeatures) {
        uint32_t allowed = (w.player[0].state == 5 ? 4u : 0u) | (w.player[1].state == 5 ? 0x40000u : 0u);
        MenuButton(w, kSelectBack, "BACK", allowed);
        MenuSights(w, allowed);
    }
    if (s.active[0] == 0 && s.active[1] == 0) s.fn = 0x004895C0;
}

}  // namespace

// 0x0048A0D0: both players' items cleared (character = the player, lives 3, weapon 0 with the row 0 of 0x004EC928)
void ItemsInit(World::Impl& w) {
    if (!w.modes) return;
    ModesState& m = *w.modes;
    for (uint8_t& f : m.itemFlags) f = 0;
    for (int p = 0; p < 2; ++p) {
        ModesState::PlayerItems& it = m.pitems[p];
        it = ModesState::PlayerItems{};
        it.character = static_cast<int8_t>(p);
        for (int k = 0; k < 4; ++k) it.b8[k] = B8(w.exe, 0x004EC928 + static_cast<uint32_t>(k));
        it.damage = F32(w.exe, 0x004EC92C);
    }
}

bool ItemDamage(World::Impl& w, int p, float* factor, float* boost) {
    if (!w.modes || p < 0 || p > 1) return false;
    *factor = w.modes->pitems[p].damage;
    *boost = w.modes->itemFlags[1] == 1 ? F32(w.exe, 0x004C4CA0) : 0.0f;  // 0x009C88A9 (item 0x1D)
    return true;
}

// actor type 0x6E: the spawn task becomes the item select
bool SpawnItemSelect(World::Impl& w, Task& t, SpawnTask& s) {
    (void)s;
    if (RandomizerActive(w)) {
        // The chapter roll already equipped both slots without consuming the saved inventory.
        ItemsGameSetup(w);
        w.runner->yield = false;
        w.runner->variant = 1;
        w.runner->script = w.mem.Script(w.runner->mode, w.runner->scene, 1);
        w.section = 0;
        w.ts.KillCurrent();
    }
    auto d = std::make_unique<ItemSelect>();
    d->fn = 0x00488820;
    t.data = std::move(d);
    t.fn = TaskItemSelect;
    TaskItemSelect(t);
    return true;
}

bool OriginalItemsCompatible(World::Impl& w, int a, int b) {
    return a < 0 || b < 0 || (a < 33 && b < 33 && Compatible(w, a, b));
}

void OriginalSetLoadouts(World::Impl& w, const int slots[2][2]) {
    ItemsInit(w);
    for (int p = 0; p < 2; ++p) {
        auto& it = w.modes->pitems[p];
        for (int i = 0; i < 2; ++i) it.slot[i] = static_cast<int8_t>(slots[p][i]);
        ItemsApply(w, p);
        for (int k = 0; k < 4; ++k)
            it.b10[k] = B8(w.exe, 0x00579ED8 + static_cast<uint32_t>(it.weapon * 8 + k));
    }
}

}  // namespace game
