// game::World - game mode 12: the options screen (0x004869E0). Root 0x00486BF0: the cursor task 0x00486C10 and the
// list task 0x00486DA0 (background 0x00488090(0), title 0xB31, eleven lines 0x005696E0 {x, y, label} each followed by
// its handler 0x005696E4[i]: difficulty, lives, credits, (hidden colour), crosshairs, calibration, sound test, voice
// test, controller, default, exit). Values are written into the save block (0x009C9F20..); EXIT saves (Game_Save
// 0x004011F0, which also applies them: 0x0040AB50) - into the port's own save file. Calibration (0x004882D0) and the
// controller screen (0x00485F90) are not ported. Verified against build\scene_capture_modes_options.txt
// (notes/agents/modes.md section 5).
// Port only (play.exe, WorldConfig::portVolumes >= 0; off in the exactness tests): rows 11..13 / 15..16 (volumes) and 14
// (Display Settings, whose page play.exe draws itself: ModesState::optDisplayRequests -> WorldHud::portDisplayRequests)
// after Wii Remotes in the port menu; audio previews live in Extras.

#include <algorithm>
#include <cstring>

#include "game/menu_text.h"
#include "game/menu_pointer.h"
#include "game/menu_audio.h"
#include "game/world_modes.h"
#include "game/world_nightshift.h"

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

void OptionSound(World::Impl& w, uint32_t original, uint32_t feedback = kOptionSelectSound) {
    if (w.cfg.portFeatures) w.SoundCall(World::kOptionFeedback, feedback);
    else w.Sound(original);
}

struct OptTask : WTask {
    MenuPointerFocus pointer;
    int pointerPlayer = -1;
};

// The players' input permissions (0x0041E260 device modes, player +0x124 / +0x125)
struct Allow {
    bool controller = false;  // item 8 shown: a gun player whose device mode is not 6
    bool calibrate = false;   // item 5 bright: a player without a gun
    bool previews = true;     // original sound tests; the native port moves them to Extras
};
Allow Permissions(World::Impl& w) {
    Allow a;
    if (w.cfg.portFeatures) { a.calibrate = a.controller = true; a.previews = false; return a; } // the calibration row opens native Wii Remote setup
    const int m0 = w.input[0].deviceMode, m1 = w.input[1].deviceMode;
    if ((w.player[0].inputKind == 1 && m0 != 6) || (w.player[1].inputKind == 1 && m1 != 6)) a.controller = true;
    if (w.player[0].inputKind == 0 || w.player[1].inputKind == 0) a.calibrate = true;
    return a;
}

// The lines in their visual order: the original's 0..10, with five volume rows and Display Settings
// between the sound tests and the controller line when play.exe enables them (ModesState::optPortRows).
constexpr int kPortRows = 6;
constexpr int kPortRowOrder[kPortRows] = {15, 11, 12, 13, 16, 14};  // master, categories, attraction, graphics
int VolumeIndex(int row) { return row >= 11 && row <= 13 ? row - 11 : row == 15 ? 3 : row == 16 ? 4 : -1; }
int PortRowPosition(int row) {
    for (int p = 0; p < kPortRows; ++p) if (kPortRowOrder[p] == row) return p;
    return -1;
}
int OptRows(const ModesState& m, int out[11 + kPortRows]) {
    int n = 0;
    for (int i = 0; i < 8; ++i) out[n++] = i;
    if (m.optPortRows)
        for (int i : kPortRowOrder) out[n++] = i;
    for (int i = 8; i < 11; ++i) out[n++] = i;
    return n;
}

// Lines the cursor skips: 3 (hidden colour) unless 0x007DD030, 8 (controller) without a gun controller, 5
// (calibration) without a pad player.
bool OptSkipped(const ModesState& m, const Allow& a, int i) {
    return (i == 3 && m.optHidden == 0) || (i == 8 && !a.controller) || (i == 5 && !a.calibrate) ||
           ((i == 6 || i == 7) && !a.previews);
}

bool ModernOptions(const World::Impl& w) { return w.cfg.portFeatures && w.cfg.portModernMenu; }
std::vector<int> ModernOptionIds(const World::Impl& w,const Allow& a) {
    int rows[11+kPortRows];
    const int count=OptRows(*w.modes,rows);
    std::vector<int> visible;
    for(int k=0;k<count;++k)if(!OptSkipped(*w.modes,a,rows[k]))visible.push_back(rows[k]);
    return visible;
}

float OptRowY(const World::Impl& w, const Allow& a, int i) {
    const ModesState& m = *w.modes;
    const int previewRows = w.cfg.portFeatures && i >= 8 && i != 10 ? 2 : 0;
    if (i >= 11) return static_cast<float>(ItemY(w.exe, 8) + m.optScroll + PortRowPosition(i) - previewRows);
    if (i == 9) return static_cast<float>(ItemY(w.exe, a.controller ? 9 : 8) + m.optScroll + (m.optPortRows ? kPortRows : 0) - previewRows);
    return static_cast<float>(ItemY(w.exe, i) + (i < 4 ? 0 : m.optScroll) + (i == 8 && m.optPortRows ? kPortRows : 0) - previewRows);
}

int OptRowAt(const World::Impl& w, const Allow& a, int x, int y) {
    if(ModernOptions(w)) {
        if(x<26||x>=614)return -1;
        const auto rows=ModernOptionIds(w,a);
        for(size_t i=0;i<rows.size();++i) {
            const float top=kNightShiftOptionsTop+float(i)*NightShiftOptionsPitch(rows.size());
            if(y>=top-1&&y<top+NightShiftOptionsHeight(rows.size()))return rows[i];
        }
        return -1;
    }
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

bool OptValue(int i) { return i <= 3 || VolumeIndex(i) >= 0; }
float OptPreviousX(const World::Impl& w,int i) { return ModernOptions(w)?360.f:i == 6 || i == 7 ? 484.0f : 440.0f; }

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
        m.optCursor = static_cast<int8_t>(row);
    }
    task.pointerPlayer = p;
    if (row < 0 || (!in.fired && !reload)) return;
    if (OptValue(row) || row == 4 || row == 6 || row == 7) {
        if ((row == 6 || row == 7) && in.fired && in.x < OptPreviousX(w,row) - 16) {
            w.pressed |= 4;  // shoot the sound-test label to play its selected recording
            return;
        }
        const int delta = reload ? -1 : row == 4 ? 1 : MenuPointerStep(float(in.x), OptPreviousX(w,row));
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
        if (!w.cfg.portFeatures) w.Sound(0xA9);
        OptMove(m, a, -1);
    }
    if (Pressed(w, 0x200020)) {
        if (!w.cfg.portFeatures) w.Sound(0xA9);
        OptMove(m, a, 1);
    }
    if (w.cfg.portFeatures && Pressed(w, 0x20002)) {  // keyboard Back / Escape saves just like the Exit button
        m.optCursor = 10;
        w.pressed |= 4;
    }
    if (!w.cfg.portFeatures && m.optCursor != was && 5 < was && was < 8) {  // left the sound / voice test: sounds stopped
        w.Sound(0x80000000);
        w.Sound(0x80000002);
        w.Sound(0x80000001);
        if (!w.cfg.portFeatures) w.Sound(0xA9);
    }
    if (w.cfg.portFeatures && OptValue(m.optCursor) && Pressed(w, 0xC000C)) w.pressed |= 0x80;
}

// A value line: R +1, L -1. Port menus use the option-select cue; original exactness keeps 0xA9.
int Step(World::Impl& w, bool feedback = true) {
    int d = 0;
    if (Pressed(w, 0x800080)) {
        if (!w.cfg.portFeatures) w.Sound(0xA9);
        ++d;
    }
    if (Pressed(w, 0x400040)) {
        if (!w.cfg.portFeatures) w.Sound(0xA9);
        --d;
    }
    if (w.cfg.portFeatures && feedback && d != 0) w.SoundCall(World::kOptionFeedback, kOptionSelectSound);
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
                OptionSound(w, 0xA9);
                ++m.optCross[0];
            }
            if (Pressed(w, 0x40)) {
                OptionSound(w, 0xA9);
                --m.optCross[0];
            }
        }
        if (p2) {
            if (Pressed(w, 0x800000)) {
                OptionSound(w, 0xA9);
                ++m.optCross[1];
            }
            if (Pressed(w, 0x400000)) {
                OptionSound(w, 0xA9);
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
            OptionSound(w, 0x3016A9);
            if (w.cfg.portFeatures) ++m.optWiiRequests;
            else w.Note("options: gun calibration screen (0x004882D0) not ported");
        }
        return;
    case 6: case 7: {  // 0x00487910 sound test (0..0x2EE, 0x00569798 {id, bgm}) / 0x00487A50 voice test (0..0x12, 0x005970C4)
        if (w.cfg.portFeatures) return;  // native previews are available only on Extras
        const float y = static_cast<float>(m.optScroll + ItemY(exe, i));
        Text(w, 32.0f, y, exe.CStr(0x00597204), 1);
        Num(w, 35.0f, y, i == 6 ? m.optBgm : m.optVoice);
        if (m.optCursor != i) return;
        Repeat(w, m.optRepeat);
        int d = 0;
        if (Pressed(w, 0x800080) || m.optRepeat == 0x1E) ++d;
        if (Pressed(w, 0x400040) || m.optRepeat == -0x1E) --d;
        if (w.cfg.portFeatures && d != 0) w.SoundCall(World::kOptionFeedback, kOptionSelectSound);
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
            OptionSound(w, 0x3016A9);
            if (w.cfg.portFeatures) ++m.optLightgunRequests;
            else w.Note("options: controller screen (0x00485F90) not ported");
        }
        return;
    case 11: case 12: case 13: case 15: case 16: {  // port only: volume 0..10 (L / R)
        if (!m.optPortRows) return;
        const int index = VolumeIndex(i);
        const float y = OptRowY(w, Permissions(w), i);
        Num(w, 35.0f, y, m.optVolume[index]);
        if (m.optCursor != i) return;
        const int previous = m.optVolume[index];
        m.optVolume[index] = std::clamp(previous + Step(w, false), 0, 10);
        if (w.cfg.portFeatures && m.optVolume[index] != previous)
            w.SoundCall(World::kOptionFeedback, kOptionSelectSound);
        return;
    }
    case 14:  // port only: Display Settings - play.exe shows its own page (the sub-screen sound of items 5 / 8)
        if (m.optPortRows && m.optCursor == 14 && Pressed(w, 0xC000C)) {
            OptionSound(w, 0x3016A9);
            ++m.optDisplayRequests;
        }
        return;
    case 9:  // 0x00487B80 default: 0x00401130, the copies reloaded
        if (m.optCursor == 9 && Pressed(w, 0x4000C)) {
            if (w.cfg.portFeatures) w.SoundCall(World::kOptionFeedback, kOptionSelectSound);
            if (m.optPortRows) {
                for (int& v : m.optVolume) v = 5;
            }
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
            OptionSound(w, 0x121A9, kOptionBackSound);
            m.Store(w);
            w.subState = 2;
        }
        return;
    default: return;
    }
}

void ModernOptionsList(World::Impl& w,const Allow& a) {
    const auto& m=*w.modes;
    std::vector<NightShiftOptionRow> rows;
    for(int id:ModernOptionIds(w,a)) {
        NightShiftOptionRow row;
        row.id=id;
        row.adjustable=OptValue(id)||id==6||id==7;
        switch(id) {
        case 0: row.label="Difficulty";row.value=w.exe.CStr(0x00569758+uint32_t(m.optDifficulty)*10);break;
        case 1: row.label="Starting Life";row.value=std::to_string(m.optLives+1);break;
        case 2: row.label="Continues";row.value=m.optCredits?std::to_string(m.optCredits):"Free Play";break;
        case 3: row.label="Blood Color";row.value=m.optSound?"Green":"Red";break;
        case 4: {
            const bool p1=(w.player[0].inputKind==0&&w.player[0].deviceKind!=-1)||w.input[0].deviceMode==6;
            const bool p2=(w.player[1].inputKind==0&&w.player[1].deviceKind!=-1)||w.input[1].deviceMode==6;
            row.label="Crosshair Style";row.value=p1?"P1 "+std::to_string(m.optCross[0]+1):"P1 --";
            row.secondValue=p2?"P2 "+std::to_string(m.optCross[1]+1):"P2 --";row.secondEnabled=p2;break;
        }
        case 5: row.label="Wii Remotes";row.value="Open";break;
        case 6: row.label="Sound Preview";row.value=std::to_string(m.optBgm);break;
        case 7: row.label="Music Preview";row.value=std::to_string(m.optVoice);break;
        case 8: row.label="Light Guns";row.value="Open";break;
        case 9: row.label="Reset Game Settings";row.value="Reset";break;
        case 10: row.label="SAVE + BACK";break;
        case 11: row.label="Music Volume";row.value=std::to_string(m.optVolume[0])+" / 10";break;
        case 12: row.label="Gun Shots Volume";row.value=std::to_string(m.optVolume[1])+" / 10";break;
        case 13: row.label="Effects + Dialogue";row.value=std::to_string(m.optVolume[2])+" / 10";break;
        case 14: row.label="Graphics Options";row.value="Open";break;
        case 15: row.label="Master Volume";row.value=std::to_string(m.optVolume[3])+" / 10";break;
        case 16: row.label="Attraction Volume";row.value=std::to_string(m.optVolume[4])+" / 10";break;
        }
        rows.push_back(std::move(row));
    }
    const char* help="Select a setting. Changes are saved when you choose Save + Back or press Escape.";
    switch(m.optCursor) {
    case 0:help="Original difficulty rules, from Very Easy to Very Hard.";break;
    case 1:help="Starting life segments for each agent. Continue rules are set separately.";break;
    case 2:help="Continues for a new Arcade run. Free Play appears after the original unlock.";break;
    case 4:help="Shoot either player's value to cycle its sight. Each player's left/right controls keep their own style.";break;
    case 5:help="Open the existing Wii Remote assignment, calibration and button setup.";break;
    case 6:case 7:help="Left/right chooses a recording. Space or shooting its name plays it; arrows choose without playback.";break;
    case 8:help="Open the existing light-gun assignment, calibration and button setup.";break;
    case 9:help="Resets game rules, crosshair/controller settings and all volumes. All volumes return to 5.";break;
    case 10:help="Save the existing game settings and return to the previous menu.";break;
    case 11:help="Music playback volume, from 0 to 10. Default: 4.";break;
    case 12:help="Gunshot sample volume, from 0 to 10. Default: 4.";break;
    case 13:help="Effects and recorded dialogue share this volume, from 0 to 10. Default: 4.";break;
    case 14:help="Open display, image clarity, lighting and the other graphics categories.";break;
    case 15:help="Overall volume for gameplay, menus, previews and the attraction loop, from 0 to 10. Default: 1.";break;
    case 16:help="Background attraction-loop volume, from 0 to 10. Default: 4. Master Volume also applies.";break;
    }
    NightShiftGameOptionsFrame(w,rows,m.optCursor,help);
    // Keep the original sight art visible beside the modern per-player labels.
    const auto ids=ModernOptionIds(w,a);
    const auto cross=std::find(ids.begin(),ids.end(),4);
    if(cross!=ids.end())for(int p=0;p<2;++p) {
        const int id=w.exe.S16(0x00579F58+p*8+uint32_t(m.optCross[p])*2);
        const uint32_t file=w.exe.U32(0x0057A5BC+uint32_t(id)*4),texture=w.exe.U32(0x0057D448+uint32_t(id)*4);
        float scale=.5f;
        const auto descriptors=w.exe.U32(0x0055B9B8+file*4);
        const int count=w.exe.S16(0x0055C0A8+file*2);
        for(int i=0;i<count&&i<4096;++i)if(w.exe.U32(descriptors+i*16+12)==texture) {
            const int size=std::max(w.exe.S16(descriptors+i*16),w.exe.S16(descriptors+i*16+2));
            if(size>0)scale=std::min(1.f,16.f/size);break;
        }
        const bool enabled=(w.player[p].inputKind==0&&w.player[p].deviceKind!=-1)||w.input[p].deviceMode==6;
        const float y=kNightShiftOptionsTop+float(cross-ids.begin())*NightShiftOptionsPitch(ids.size())+10;
        Spr(w,id,p?598.f:492.f,y,.009f,scale,scale,0,enabled?1.f:.5f,0x70A,Tint{});
        if(file<w.texFiles.size()&&w.texFiles[file]!=4)w.ProcessRequest({0x54,file});
    }
}

// 0x00486DA0: the list
void TaskOptList(Task&) {
    World::Impl& w = *CurrentWorld();
    ModesState& m = *w.modes;
    const ExeImage& exe = w.exe;
    const Allow a = Permissions(w);
    const size_t spriteStart=w.hudSprites.size();
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
    static const char* const kPortLabels[kPortRows] = { "Music Volume", "Gun Shots Volume", "Effects + Dialogue", "Graphics Options", "Master Volume", "Attraction Volume" };
    auto portRows = [&] {
        if (!m.optPortRows) return;
        for (int r = 11; r < 11 + kPortRows; ++r) {
            Text(w, static_cast<float>(ItemX(exe, 8)), OptRowY(w, a, r), kPortLabels[r - 11],
                 m.optCursor == r ? 0x111 : 1);
            Handler(w, r);
        }
    };
    for (int i = 0; i < 11; ++i) {
        if (w.cfg.portFeatures && (i == 6 || i == 7)) {
            if (i == 7) portRows();
            continue;
        }
        if (i < 10) {
            const float x = static_cast<float>(ItemX(exe, i));
            const float y = OptRowY(w, a, i);
            std::string label = ItemText(exe, i);
            if (w.cfg.portFeatures) {
                if (i == 5) label = "Wii Remotes";
                else if (i == 8) label = "Light Guns";
                else if (i == 9) label = "Reset Game Settings";
            }
            if (m.optCursor == i) {
                if (i == 3) {
                    if (m.optHidden != 0) Text(w, x, static_cast<float>(ItemY(exe, 3)), label, 0x111);
                } else if (i == 9) {
                    Text(w, x, OptRowY(w, a, 9), label, 0x111);
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
                case 9: Text(w, x, OptRowY(w, a, 9), label, 1); break;
                default: Text(w, x, y, label, 1); break;
                }
            }
            if (i == 7) portRows();
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
        if (OptValue(m.optCursor))
            MenuAdjustArrows(OptPreviousX(w,m.optCursor), 620, OptRowY(w, a, m.optCursor) * 24 + 22, w.hudSprites);
        if (!m.optPortRows) {
            const char* hint = m.optCursor == 4 ? "Shoot a player sight to change it" : "Click or shoot   Reload to reverse";
            MenuTextScaled(exe, 32, 380, hint, 0, 0.8f, w.hudSprites);
        }
    }
    if(ModernOptions(w)) {
        // Retain the exact original handler order, sounds and save writes above;
        // replace only the completed list's presentation for the Night Shift theme.
        w.hudSprites.resize(spriteStart);
        ModernOptionsList(w,a);
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
        for (int k = 0; k < 5; ++k) m.optVolume[k] = std::clamp(w.cfg.portVolumes[k], 0, 10);
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
