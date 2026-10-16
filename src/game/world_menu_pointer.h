#pragma once

// Port-only pointer controls shared by the original selection screens. Actions feed the original handlers,
// retaining unlocks, inventory checks and player masks. A shot consumes its synthetic Start edge as well.
#include "game/menu_pointer.h"
#include "game/menu_text.h"
#include "game/world_modes.h"

namespace game {
struct MenuRect {
    float left, top, right, bottom;
    bool Hit(float x, float y) const { return x >= left && x < right && y >= top && y < bottom; }
};
inline constexpr MenuRect kSelectBack{16, 442, 144, 478}, kSelectStart{488, 442, 632, 478};
inline bool MenuAim(const WorldInput& in) { return in.kind == 1 && !in.offscreen && in.x < 640 && in.y < 480; }
inline int MenuHover(const World::Impl& w, MenuRect r, uint32_t allowed = 0x40004) {
    for (int p = 0; p < 2; ++p)
        if ((allowed & (0xFFFFu << (p * 16))) && MenuAim(w.input[p]) && r.Hit(w.input[p].x, w.input[p].y)) return p;
    return -1;
}
inline void MenuLoadSprites(World::Impl& w, size_t first) {
    for (size_t i = first; i < w.hudSprites.size(); ++i) {
        const uint32_t file = w.exe.U32(0x0057A5BC + uint32_t(w.hudSprites[i].id) * 4);
        if (file < w.texFiles.size() && w.texFiles[file] != 4) w.ProcessRequest({0x54, file});
    }
}
inline void MenuButton(World::Impl& w, MenuRect r, const char* text, uint32_t allowed = 0x40004) {
    const size_t first = w.hudSprites.size();
    MenuTextScaled(w.exe, r.left + 8, r.top - 8, text, MenuHover(w, r, allowed) >= 0 ? 0x10 : 1, 1, w.hudSprites);
    MenuLoadSprites(w, first);
}
inline void MenuSights(World::Impl& w, uint32_t allowed = 0x40004) {
    const size_t first = w.hudSprites.size();
    for (int p = 0; p < 2; ++p) {
        if (!(allowed & (0xFFFFu << (p * 16))) || !MenuAim(w.input[p])) continue;
        SpriteDef d;
        d.id = w.exe.S16(0x00579F58 + p * 8);
        d.x = float(w.input[p].x); d.y = float(w.input[p].y);
        d.flags = 0x70A; d.depth = 0.81f; d.sx = d.sy = 0.7f;
        w.hudSprites.push_back(d);
    }
    MenuLoadSprites(w, first);
}
// The broad side-card areas include the large original arrows. The middle card starts the selected entry.
inline void SelectPointer(World::Impl& w, uint32_t allowed, bool difficulty, bool ready) {
    if (!w.cfg.portFeatures) return;
    for (int p = 0; p < 2; ++p) {
        const auto& in = w.input[p];
        if (!(allowed & (0xFFFFu << (p * 16))) || in.kind != 1 || !in.fired) continue;
        w.pressed &= ~(12u << (p * 16));
        if (!MenuAim(in) || !ready) continue;
        uint32_t action = 0;
        if (kSelectBack.Hit(in.x, in.y)) action = 2;
        else if (kSelectStart.Hit(in.x, in.y) || MenuRect{216, 90, 428, 292}.Hit(in.x, in.y)) action = 4;
        else if (MenuRect{8, 100, 210, 290}.Hit(in.x, in.y)) action = 0x40;
        else if (MenuRect{434, 100, 632, 290}.Hit(in.x, in.y)) action = 0x80;
        else if (difficulty && MenuRect{172, 442, 208, 478}.Hit(in.x, in.y)) action = 0x20;
        else if (difficulty && MenuRect{376, 442, 412, 478}.Hit(in.x, in.y)) action = 0x10;
        w.pressed |= action << (p * 16);
    }
}
inline void SelectPointerDraw(World::Impl& w, uint32_t allowed, int difficulty = -1) {
    if (!w.cfg.portFeatures) return;
    const size_t first = w.hudSprites.size();
    MenuButton(w, kSelectBack, "BACK", allowed);
    MenuButton(w, kSelectStart, "START", allowed);
    if (difficulty >= 0) {
        MenuTextScaled(w.exe, 224, 434, "LEVEL " + std::to_string(difficulty + 1), 1, 1, w.hudSprites);
        MenuAdjustArrows(190, 394, 459, w.hudSprites);
    }
    MenuSights(w, allowed);
    MenuLoadSprites(w, first);
}
} // namespace game
