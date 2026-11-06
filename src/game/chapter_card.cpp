#include "game/hud.h"
#include <cstring>

namespace game {
namespace { float F(uint32_t value) { float result; std::memcpy(&result,&value,4); return result; } }

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
    SpriteDef d; d.id=id; d.x=x; d.y=y; d.depth=1; d.sx=sx; d.sy=sy; d.flags=0xA;
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

} // namespace game
