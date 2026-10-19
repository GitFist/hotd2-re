// game::World - game mode 12: the options screen (0x004869E0). Root 0x00486BF0: the cursor task 0x00486C10 and the
// list task 0x00486DA0 (background 0x00488090(0), title 0xB31, eleven lines 0x005696E0 {x, y, label} each followed by
// its handler 0x005696E4[i]: difficulty, lives, credits, (hidden colour), crosshairs, calibration, sound test, voice
// test, controller, default, exit). Values are written into the save block (0x009C9F20..); EXIT saves (Game_Save
// 0x004011F0, which also applies them: 0x0040AB50) - into the port's own save file. Calibration (0x004882D0) and the
// controller screen (0x00485F90) are not ported. Verified against build\scene_capture_modes_options.txt
// (notes/agents/modes.md section 5).
// Port only (play.exe, WorldConfig::portVolumes >= 0; off in the exactness tests): rows 11..13 (volumes) and 14
// (Display Settings, whose page play.exe draws itself: ModesState::optDisplayRequests -> WorldHud::portDisplayRequests)
// between the sound tests and the controller line.

#include <algorithm>
#include <cstring>

#include "game/menu_text.h"
#include "game/menu_pointer.h"
#include "game/world_modes.h"

namespace game {

namespace {

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}
double F64(const ExeImage& exe, uint32_t va) {
    double d = 0;
    exe.Read(va, &d, 8);
    return d;
}

struct Tint {
    float r = 1, g = 1, b = 1;  // 0x004AA0A0 light colour for flags-0x2000 sprites
};

// 0x00488000(id, x, y, depth, sx, sy, rot, alpha, flags): skipped for id < 0
void Spr(World::Impl& w, int id, float x, float y, float depth, float sx, float sy, int rot, float alpha, uint32_t flags,
         const Tint& t) {
    if (id < 0) return;
    SpriteDef d;
    d.id = id;
    d.x = x;
    d.y = y;
    d.depth = depth;
    d.sx = sx;
    d.sy = sy;
    d.rot = rot;
    d.alpha = alpha;
    d.blend = -1;
    d.flags = flags;
    if (flags & 0x2000) {
        d.tinted = true;
        d.tint[0] = t.r;
        d.tint[1] = t.g;
        d.tint[2] = t.b;
    }
    w.hudSprites.push_back(d);
}

// 0x00487CA0(x, y, text, flags) and 0x00487EE0(x, y, value): game/menu_text.h (shared with the port's pause menu)
void Text(World::Impl& w, float x, float y, const std::string& s, uint32_t flags) { MenuText(w.exe, x, y, s, flags, w.hudSprites); }
void Num(World::Impl& w, float x, float y, int v) { MenuNum(w.exe, x, y, v, w.hudSprites); }

int ItemX(const ExeImage& exe, int i) { return static_cast<int8_t>(exe.U32(exe.U32(0x005696E0 + i * 8)) & 0xFF); }
int ItemY(const ExeImage& exe, int i) { return static_cast<int8_t>((exe.U32(exe.U32(0x005696E0 + i * 8)) >> 8) & 0xFF); }
std::string ItemText(const ExeImage& exe, int i) { return exe.CStr(exe.U32(exe.U32(0x005696E0 + i * 8) + 4)); }

// 0x00488200(mask): a pad bit pressed (the keyboard 0x009A37A8 is not driven)
bool Pressed(World::Impl& w, uint32_t mask) { return (w.pressed & mask) != 0; }

struct OptTask : WTask {
    MenuPointerFocus pointer;
    int pointerPlayer = -1;
};

// The players' input permissions (0x0041E260 device modes, player +0x124 / +0x125)
struct Allow {
    bool controller = false;  // item 8 shown: a gun player whose device mode is not 6
    bool calibrate = false;   // item 5 bright: a player without a gun
};
Allow Permissions(World::Impl& w) {
    Allow a;
    if (w.cfg.portFeatures) { a.calibrate = a.controller = true; return a; } // the calibration row opens native Wii Remote setup
    const int m0 = w.input[0].deviceMode, m1 = w.input[1].deviceMode;
    if ((w.player[0].inputKind == 1 && m0 != 6) || (w.player[1].inputKind == 1 && m1 != 6)) a.controller = true;
    if (w.player[0].inputKind == 0 || w.player[1].inputKind == 0) a.calibrate = true;
    return a;
}

// The lines in their visual order: the original's 0..10, with the port's rows 11..14 (three volumes, Display Settings)
// between the sound tests and the controller line when play.exe enables them (ModesState::optPortRows).
constexpr int kPortRows = 4;  // 11..13 volumes, 14 display settings
int OptRows(const ModesState& m, int out[11 + kPortRows]) {
    int n = 0;
    for (int i = 0; i < 8; ++i) out[n++] = i;
    if (m.optPortRows)
        for (int i = 11; i < 11 + kPortRows; ++i) out[n++] = i;
    for (int i = 8; i < 11; ++i) out[n++] = i;
    return n;
}

// Lines the cursor skips: 3 (hidden colour) unless 0x007DD030, 8 (controller) without a gun controller, 5
// (calibration) without a pad player.
bool OptSkipped(const ModesState& m, const Allow& a, int i) {
    return (i == 3 && m.optHidden == 0) || (i == 8 && !a.controller) || (i == 5 && !a.calibrate);
}

float OptRowY(const World::Impl& w, const Allow& a, int i) {
    const ModesState& m = *w.modes;
    if (i >= 11) return static_cast<float>(ItemY(w.exe, 8) + m.optScroll + i - 11);
    if (i == 9) return static_cast<float>(ItemY(w.exe, a.controller ? 9 : 8) + m.optScroll + (m.optPortRows ? kPortRows : 0));
    return static_cast<float>(ItemY(w.exe, i) + (i < 4 ? 0 : m.optScroll) + (i == 8 && m.optPortRows ? kPortRows : 0));
}

int OptRowAt(const World::Impl& w, const Allow& a, int x, int y) {
    const float exitY = ItemY(w.exe, 10) * 24.0f - F32(w.exe, 0x004C4CA0);
    if (x >= 256 && x < 384 && y >= exitY - 32 && y < exitY + 32) return 10;
    int rows[11 + kPortRows];
    const int n = OptRows(*w.modes, rows);
    for (int k = 0; k < n; ++k) {
        const int i = rows[k];
        if (i != 10 && !OptSkipped(*w.modes, a, i) && MenuTextRowHit(float(x), float(y), OptRowY(w, a, i))) return i;
    }
    return -1;
}

bool OptValue(int i) { return i <= 3 || (i >= 11 && i <= 13); }
float OptPreviousX(int i) { return i == 6 || i == 7 ? 484.0f : 440.0f; }

// Translate pointer actions to the same input edges as the original handlers. Consume the gun's trigger/start
// pair first: clicks outside rows must not confirm a stale selection or also activate a different action.
void OptPointer(World::Impl& w, OptTask& task, const Allow& a) {
    ModesState& m = *w.modes;
    int p = -1;
    for (int q = 0; q < 2; ++q) {
        const auto& in = w.input[q];
        if (in.kind != 1) continue;
        if (p < 0) p = q;
        if (in.fired || in.offscreen) {
            p = q;
            w.pressed &= ~(0xEu << (q * 16));
        }
    }
    if (p < 0) { task.pointer.seen = false; return; }
    const auto& in = w.input[p];
    const bool reload = in.offscreen && (in.pressed & 2) != 0;
    const int row = OptRowAt(w, a, in.x, in.y);
    if (task.pointer.Update(true, float(in.x), float(in.y), in.fired || reload || p != task.pointerPlayer) && row >= 0) {
        if (m.optCursor != row) w.Sound(0xA9);
        m.optCursor = static_cast<int8_t>(row);
    }
    task.pointerPlayer = p;
    if (row < 0 || (!in.fired && !reload)) return;
    if (OptValue(row) || row == 4 || row == 6 || row == 7) {
        if ((row == 6 || row == 7) && in.fired && in.x < OptPreviousX(row) - 16) {
            w.pressed |= 4;  // shoot the sound-test label to play its selected recording
            return;
        }
        const int delta = reload ? -1 : row == 4 ? 1 : MenuPointerStep(float(in.x), OptPreviousX(row));
        const int player = row == 4 ? (in.x >= 540 ? 1 : 0) : p;
        w.pressed |= (delta > 0 ? 0x80u : 0x40u) << (player * 16);
    } else if (in.fired) {
        w.pressed |= 4;  // Default, Display Settings or Exit
    }
}

// 0x00486C10: cursor up / down (lines 3, 5, 8 skipped when hidden). The original steps +-1 and re-steps once past a
// skipped line; no two skipped lines are adjacent, so a walk over the ordered list is the same thing.
void OptMove(ModesState& m, const Allow& a, int d) {
    int rows[11 + kPortRows];
    const int n = OptRows(m, rows);
    int pos = 0;
    for (int k = 0; k < n; ++k)
        if (rows[k] == m.optCursor) pos = k;
    do {
        pos = (pos + d + n) % n;
    } while (OptSkipped(m, a, rows[pos]));
    m.optCursor = static_cast<int8_t>(rows[pos]);
}

void TaskOptCursor(Task& task) {
    World::Impl& w = *CurrentWorld();
    ModesState& m = *w.modes;
    const int8_t was = m.optCursor;
    const Allow a = Permissions(w);
    if (w.cfg.portFeatures) OptPointer(w, task.As<OptTask>(), a);
    if (Pressed(w, 0x100010)) {
        w.Sound(0xA9);
        OptMove(m, a, -1);
    }
    if (Pressed(w, 0x200020)) {
        w.Sound(0xA9);
        OptMove(m, a, 1);
    }
    if (m.optCursor != was && 5 < was && was < 8) {  // left the sound / voice test: sounds stopped
        w.Sound(0x80000000);
        w.Sound(0x80000002);
        w.Sound(0x80000001);
        w.Sound(0xA9);
    }
    if (w.cfg.portFeatures && OptValue(m.optCursor) && Pressed(w, 0xC000C)) w.pressed |= 0x80;
    if (w.cfg.portFeatures && Pressed(w, 0x20002)) {  // keyboard Back / Escape saves just like the Exit button
        m.optCursor = 10;
        w.pressed |= 4;
    }
}

// a value line: R +1, L -1 (sound 0xA9)
int Step(World::Impl& w) {
    int d = 0;
    if (Pressed(w, 0x800080)) {
        w.Sound(0xA9);
        ++d;
    }
    if (Pressed(w, 0x400040)) {
        w.Sound(0xA9);
        --d;
    }
    return d;
}

// 0x00487C40: L / R held (0x009C9020): the sound tests' auto repeat counter 0x009C8E36 (+-30)
void Repeat(World::Impl& w, int16_t& c) {
    if (w.held & 0x800080) {
        if (c < 0x1E) ++c;
    } else if (w.held & 0x400040) {
        if (-0x1E < c) --c;
    } else {
        c = 0;
    }
}

void Handler(World::Impl& w, int i) {
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    SaveData& s = m.save;
    switch (i) {
    case 0:  // 0x00487320 difficulty "Very Easy".. (0x00569758, 10 each)
        Text(w, 29.0f, static_cast<float>(ItemY(exe, 0)), exe.CStr(0x00569758 + static_cast<uint32_t>(m.optDifficulty) * 10), 1);
        if (m.optCursor == 0) {
            m.optDifficulty = static_cast<int8_t>(m.optDifficulty + Step(w));
            if (4 < m.optDifficulty) m.optDifficulty = 0;
            if (m.optDifficulty < 0) m.optDifficulty = 4;
            s.SetU8(SaveData::kDifficulty, static_cast<uint8_t>(m.optDifficulty));
        }
        return;
    case 1:  // 0x004873E0 lives "1".."5" (0x0056973A)
        Text(w, 37.0f, static_cast<float>(ItemY(exe, 1)), exe.CStr(0x0056973A + static_cast<uint32_t>(m.optLives) * 2), 1);
        if (m.optCursor == 1) {
            m.optLives = static_cast<int8_t>(m.optLives + Step(w));
            if (4 < m.optLives) m.optLives = 0;
            if (m.optLives < 0) m.optLives = 4;
            s.SetU8(SaveData::kLives, static_cast<uint8_t>(m.optLives));
        }
        return;
    case 2: {  // 0x004874A0 credits: 1..9, "Free Play" only once unlocked (0x009C9F5E & 7 == 7)
        const bool freeOk = (s.U8(SaveData::kFlags) & 7) == 7;
        if (m.optCredits == 0) Text(w, 29.0f, static_cast<float>(ItemY(exe, 2)), exe.CStr(0x005971F8), 1);
        else Text(w, 37.0f, static_cast<float>(ItemY(exe, 2)), exe.CStr(0x00569738 + static_cast<uint32_t>(m.optCredits) * 2), 1);
        if (m.optCursor != 2) return;
        m.optCredits = static_cast<int8_t>(m.optCredits + Step(w));
        if (freeOk) {
            if (9 < m.optCredits) m.optCredits = 0;
            if (m.optCredits < 0) m.optCredits = 9;
        } else {
            if (9 < m.optCredits) m.optCredits = 1;
            if (m.optCredits < 1) m.optCredits = 9;
        }
        s.SetU8(SaveData::kCredits, m.optCredits == 0 ? 0xFF : static_cast<uint8_t>(m.optCredits));
        return;
    }
    case 3:  // 0x00487250 (only with 0x007DD030): "  Red" / "Green" -> 0x009C9F22
        if (m.optHidden == 0) return;
        Text(w, 33.0f, static_cast<float>(ItemY(exe, 3)), exe.CStr(0x0056974C + static_cast<uint32_t>(m.optSound) * 6), 1);
        if (m.optCursor == 3) {
            m.optSound = static_cast<int8_t>(m.optSound + Step(w));
            if (1 < m.optSound) m.optSound = 0;
            if (m.optSound < 0) m.optSound = 1;
            s.SetU8(0x009C9F22, static_cast<uint8_t>(m.optSound));
        }
        return;
    case 4: {  // 0x004875D0 crosshairs (0x00579F58 / 0x00579F60 styles 0..3)
        const bool p1 = (w.player[0].inputKind == 0 && w.player[0].deviceKind != -1) || w.input[0].deviceMode == 6;
        const bool p2 = (w.player[1].inputKind == 0 && w.player[1].deviceKind != -1) || w.input[1].deviceMode == 6;
        const float row = (static_cast<float>(m.optScroll + ItemY(exe, 4))) * 24.0f;
        const Tint t;
        Spr(w, 0x5B9, 464.0f, row + F32(exe, 0x004C43A4), 1, 1, 0.7f, 0, p1 ? 1.0f : 0.5f, 0, t);
        Spr(w, exe.S16(0x00579F58 + static_cast<uint32_t>(m.optCross[0]) * 2), 506.0f, row + F32(exe, 0x005644FC), 1, 1, 1, 0, p1 ? 1.0f : 0.5f, 0, t);
        Spr(w, 0x5BA, 544.0f, row + F32(exe, 0x004C43A4), 1, 1, 0.7f, 0, p2 ? 1.0f : 0.5f, 0, t);
        Spr(w, exe.S16(0x00579F60 + static_cast<uint32_t>(m.optCross[1]) * 2), 584.0f, row + F32(exe, 0x005644FC), 1, 1, 1, 0, p2 ? 1.0f : 0.5f, 0, t);
        if (m.optCursor != 4) return;
        if (p1) {
            if (Pressed(w, 0x80)) {
                w.Sound(0xA9);
                ++m.optCross[0];
            }
            if (Pressed(w, 0x40)) {
                w.Sound(0xA9);
                --m.optCross[0];
            }
        }
        if (p2) {
            if (Pressed(w, 0x800000)) {
                w.Sound(0xA9);
                ++m.optCross[1];
            }
            if (Pressed(w, 0x400000)) {
                w.Sound(0xA9);
                --m.optCross[1];
            }
        }
        for (int8_t& c : m.optCross) {
            if (3 < c) c = 0;
            if (c < 0) c = 3;
        }
        s.SetU8(SaveData::kPlayerCfg, static_cast<uint8_t>(m.optCross[0]));
        s.SetU8(SaveData::kPlayerCfg + 0x7C, static_cast<uint8_t>(m.optCross[1]));
        return;
    }
    case 5:  // 0x004878E0 calibration (0x004882D0)
        if (m.optCursor == 5 && Pressed(w, 0xC000C)) {
            w.Sound(0x3016A9);
            if (w.cfg.portFeatures) ++m.optWiiRequests;
            else w.Note("options: gun calibration screen (0x004882D0) not ported");
        }
        return;
    case 6: case 7: {  // 0x00487910 sound test (0..0x2EE, 0x00569798 {id, bgm}) / 0x00487A50 voice test (0..0x12, 0x005970C4)
        const float y = static_cast<float>(m.optScroll + ItemY(exe, i));
        Text(w, 32.0f, y, exe.CStr(0x00597204), 1);
        Num(w, 35.0f, y, i == 6 ? m.optBgm : m.optVoice);
        if (m.optCursor != i) return;
        Repeat(w, m.optRepeat);
        int d = 0;
        if (Pressed(w, 0x800080) || m.optRepeat == 0x1E) ++d;
        if (Pressed(w, 0x400040) || m.optRepeat == -0x1E) --d;
        if (i == 6) {
            m.optBgm = static_cast<int16_t>(m.optBgm + d);
            if (m.optBgm < 0x2EF) {
                if (m.optBgm < 0) m.optBgm = 0x2EE;
            } else {
                m.optBgm = 0;
            }
            if (Pressed(w, 0x40004)) {
                // (0x0041D3A0(0x0056979C[n * 2], 0, 3): the BGM file of the entry, when not -1)
                w.Sound(0x80000002);
                w.Sound(0x80000001);
                w.Sound(exe.U32(0x00569798 + static_cast<uint32_t>(m.optBgm) * 8));
            }
        } else {
            m.optVoice = static_cast<int8_t>(m.optVoice + d);
            if (m.optVoice < 0x13) {
                if (m.optVoice < 0) m.optVoice = 0x12;
            } else {
                m.optVoice = 0;
            }
            if (Pressed(w, 0x40004)) w.Sound(exe.U32(0x005970C4 + static_cast<uint32_t>(m.optVoice) * 4));
        }
        return;
    }
    case 8:  // 0x00487B50 controller setup (0x00485F90)
        if (m.optCursor == 8 && Pressed(w, 0xC000C)) {
            w.Sound(0x3016A9);
            if (w.cfg.portFeatures) ++m.optLightgunRequests;
            else w.Note("options: controller screen (0x00485F90) not ported");
        }
        return;
    case 11: case 12: case 13: {  // port only: Music / Gun Shots / Sound Effects volume 0..10 (L / R), in the controller line's slot
        if (!m.optPortRows) return;
        const float y = static_cast<float>(m.optScroll + ItemY(exe, 8) + (i - 11));
        Num(w, 35.0f, y, m.optVolume[i - 11]);
        if (m.optCursor != i) return;
        m.optVolume[i - 11] = std::clamp(m.optVolume[i - 11] + Step(w), 0, 10);
        return;
    }
    case 14:  // port only: Display Settings - play.exe shows its own page (the sub-screen sound of items 5 / 8)
        if (m.optPortRows && m.optCursor == 14 && Pressed(w, 0xC000C)) {
            w.Sound(0x3016A9);
            ++m.optDisplayRequests;
        }
        return;
    case 9:  // 0x00487B80 default: 0x00401130, the copies reloaded
        if (m.optCursor == 9 && Pressed(w, 0x4000C)) {
            if (m.optPortRows)
                for (int& v : m.optVolume) v = 10;  // port: the volume rows too
            s.DefaultSettings(exe);
            m.optDifficulty = static_cast<int8_t>(s.U8(SaveData::kDifficulty));
            m.optLives = static_cast<int8_t>(s.U8(SaveData::kLives));
            m.optCredits = s.S8(SaveData::kCredits) == -1 ? 0 : s.S8(SaveData::kCredits);
            m.optCross[0] = static_cast<int8_t>(s.U8(SaveData::kPlayerCfg));
            m.optCross[1] = static_cast<int8_t>(s.U8(SaveData::kPlayerCfg + 0x7C));
        }
        return;
    case 10:  // 0x00487C00 exit: sound, Game_Save 0x004011F0 (applies the settings, 0x0040AB50), sub-state 2
        if (m.optCursor == 10 && Pressed(w, 0xC000C)) {
            w.Sound(0x121A9);
            m.Store(w);
            w.subState = 2;
        }
        return;
    default: return;
    }
}

// 0x00486DA0: the list
void TaskOptList(Task&) {
    World::Impl& w = *CurrentWorld();
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    const Allow a = Permissions(w);
    // 0x00413CC0 (dimming: counted by the mode function), 0x00488090(0): tiles 0x7A..0x8D at depth 200
    MenuBackground(w.hudSprites);
    {
        SpriteDef d;  // Spr_DrawSimple(0xB31, 344, 16, 1, 1, 1, 0, 6)
        d.id = 0xB31;
        d.x = 344.0f;
        d.y = 16.0f;
        d.flags = 6;
        w.hudSprites.push_back(d);
    }
    const float sc = static_cast<float>(m.optScroll);
    // port: the volume / display rows take the controller line's slot; the controller / default lines move down four cells
    const float shift = m.optPortRows ? static_cast<float>(kPortRows) : 0.0f;
    static const char* const kPortLabels[kPortRows] = { "Music Volume", "Gun Shots Volume", "Sound Effects Volume", "Display Settings" };
    for (int i = 0; i < 11; ++i) {
        if (i < 10) {
            const float x = static_cast<float>(ItemX(exe, i));
            const float y = static_cast<float>(ItemY(exe, i)) + (i < 3 ? 0.0f : sc) + (i == 8 || i == 9 ? shift : 0.0f);
            const std::string label = i == 5 && w.cfg.portFeatures ? "Wii Remotes" : i == 8 && w.cfg.portFeatures ? "Light Guns" : ItemText(exe, i);
            if (m.optCursor == i) {
                if (i == 3) {
                    if (m.optHidden != 0) Text(w, x, static_cast<float>(ItemY(exe, 3)), label, 0x111);
                } else if (i == 9) {
                    Text(w, x, static_cast<float>(a.controller ? ItemY(exe, 9) : ItemY(exe, 8)) + sc + shift, label, 0x111);
                } else {
                    Text(w, x, y, label, 0x111);
                }
            } else {
                switch (i) {
                case 3:
                    if (m.optHidden != 0) Text(w, x, static_cast<float>(ItemY(exe, 3)), label, 1);
                    break;
                case 5: Text(w, x, y, label, a.calibrate ? 1 : 9); break;
                case 8:
                    if (a.controller) Text(w, x, y, label, 1);
                    break;
                case 9: Text(w, x, static_cast<float>(a.controller ? ItemY(exe, 9) : ItemY(exe, 8)) + sc + shift, label, 1); break;
                default: Text(w, x, y, label, 1); break;
                }
            }
            if (i == 7 && m.optPortRows) {  // port: the volume rows and Display Settings after the sound tests
                for (int r = 11; r < 11 + kPortRows; ++r) {
                    Text(w, static_cast<float>(ItemX(exe, 8)), static_cast<float>(ItemY(exe, 8)) + sc + static_cast<float>(r - 11), kPortLabels[r - 11],
                         m.optCursor == r ? 0x111 : 1);
                    Handler(w, r);
                }
            }
        } else {
            SpriteDef d;  // the exit sign, red when selected
            d.id = 0x803;
            d.x = 320.0f;
            d.y = static_cast<float>(ItemY(exe, 10) * 0x18) - F32(exe, 0x004C4CA0);
            d.flags = 0x200A;
            d.tinted = true;
            if (m.optCursor == 10) {
                d.tint[1] = d.tint[2] = 0.0f;
            }
            w.hudSprites.push_back(d);
        }
        Handler(w, i);
    }
    if (w.cfg.portFeatures) {
        if (OptValue(m.optCursor) || m.optCursor == 6 || m.optCursor == 7)
            MenuAdjustArrows(OptPreviousX(m.optCursor), 620, OptRowY(w, a, m.optCursor) * 24 + 22, w.hudSprites);
        const char* hint = m.optCursor == 6 || m.optCursor == 7 ? "Shoot arrows to choose   Name to play" :
                           m.optCursor == 4 ? "Shoot a player sight to change it" : "Click or shoot   Reload to reverse";
        MenuTextScaled(exe, 32, 380, hint, 0, 0.8f, w.hudSprites);
    }
}

}  // namespace

// Game mode 12 (0x004869E0)
void OptionsFrame(World::Impl& w) {
    ModesState& m = *w.modes;
    const SaveData& s = m.save;
    if (w.subState == 0) {
        // sounds, 0x0041EBB0(0x10, 0x10), Mem_Init, pols 0x1B7 / 0x150 / 0x15E / 0x14A / 0x14B / 0x14C, flush,
        // Task_InitRoot(0x00486BF0), the copies of the saved settings
        w.SoundStopAll();  // 0x0041D350
        w.ts.Reset();
        w.resQueue.clear();
        w.residency = false;
        w.NewTask<OptTask>(TaskOptCursor, 0x00486C10);
        w.NewTask<OptTask>(TaskOptList, 0x00486DA0);
        m.optKind[0] = w.player[0].inputKind;  // 0x007DD01C / 0x007DD020
        m.optKind[1] = w.player[1].inputKind;
        m.optLives = static_cast<int8_t>(s.U8(SaveData::kLives));
        m.optCursor = 0;
        m.optDifficulty = static_cast<int8_t>(s.U8(SaveData::kDifficulty));
        m.optCredits = s.S8(SaveData::kCredits) == -1 ? 0 : s.S8(SaveData::kCredits);
        m.optSound = static_cast<int8_t>(s.U8(0x009C9F22));
        m.optCross[0] = static_cast<int8_t>(s.U8(SaveData::kPlayerCfg));
        m.optCross[1] = static_cast<int8_t>(s.U8(SaveData::kPlayerCfg + 0x7C));
        m.optBgm = 0;
        m.optVoice = 0;
        m.optRepeat = 0;
        m.optHidden = 0;
        m.optPortRows = w.cfg.portVolumes[0] >= 0;  // port: the volume / display rows (play.exe), from the current settings
        for (int k = 0; k < 3; ++k) m.optVolume[k] = std::clamp(w.cfg.portVolumes[k], 0, 10);
        m.optDisplayRequests = 0;
        m.optWiiRequests = 0;
        m.optLightgunRequests = 0;
        m.optScroll = -1;  // 0x007DD028 = 0xFFFF
        w.front.dimCounter = 0;
        w.subState = 1;
        return;
    }
    if (w.subState == 1) {  // 0x00486B80: a player's input kind changed -> sounds off, cursor to EXIT; the tasks
        if (w.player[0].inputKind != m.optKind[0] || w.player[1].inputKind != m.optKind[1]) {
            w.Sound(0x80000000);
            w.Sound(0x80000002);
            w.Sound(0x80000001);
            m.optCursor = 10;
        }
        m.optKind[0] = w.player[0].inputKind;
        m.optKind[1] = w.player[1].inputKind;
        w.ts.RunFrame();
        if (w.held == 0 && w.pressed == 0) {  // 0x00413CC0 (called by the list task)
            if (18000 < ++w.front.dimCounter && w.front.dimCounter > 0x468C) w.front.dimCounter = 0x468C;
        } else {
            w.front.dimCounter = 0;
        }
        return;
    }
    if (w.subState == 2) {  // 0x00406FD0 (credits cleared), 0x0049DFD0, 0x0040AC10, Game_SetState(4)
        w.FrontAfterStart();
        w.front.modeRequest = 4;
    }
}

}  // namespace game
