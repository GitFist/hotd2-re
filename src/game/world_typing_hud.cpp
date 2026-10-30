#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string_view>

#include "game/mstack.h"
#include "game/world_internal.h"

namespace game {
namespace {

// Supplied English PC data table 0x0058A130, ASCII 32..127. These are raw
// reference model identities; SourceModel resolves prepared/canonical ids.
constexpr std::array<int, 96> kGlyphModels = {
    0,7056,7063,7335,7061,7338,7051,7052,7330,7331,7053,0,7058,7075,7062,7383,
    7038,7039,7041,7043,7044,7045,7046,7047,7048,7049,7059,0,0,0,0,7072,
    7054,7050,7055,7057,7060,7065,7067,7068,7069,7076,7100,7101,7205,7329,7334,7336,
    7337,7361,7362,7369,7398,7399,7412,7413,7414,7415,7417,7102,7416,7103,0,7411,
    0,7370,7372,7373,7374,7375,7376,7377,7378,7379,7380,7381,7382,7384,7385,7386,
    7387,7388,7389,7390,7391,7392,7393,7394,7395,7396,7397,0,0,0,0,0
};

struct LetterSize { float width, left; };

// 0x004AF640 after the PC's ASCII-to-full-width lookup 0x00452FC0.
LetterSize Letter(unsigned char c) {
    if (c == ' ' || c == ',' || c == '.' || c == '\'') return {.4f, 0};
    if (c == 'I') return {.9f, .2f};
    if (c < 'a' || c > 'z') return {.9f, 0};
    static constexpr std::array<LetterSize, 26> lower = {{
        {.8f,0},{.8f,0},{.8f,0},{.8f,0},{.8f,0},{.8f,0},{.8f,0},{.8f,0},
        {.6f,.2f},{.65f,.2f},{.8f,0},{.6f,.2f},{.9f,-.1f},{.8f,0},{.85f,0},
        {.8f,0},{.8f,0},{.7f,0},{.8f,0},{.8f,0},{.8f,0},{.8f,0},{.9f,0},
        {.8f,0},{.8f,0},{.8f,0}
    }};
    return lower[c - 'a'];
}

float TextWidth(std::string_view text) {
    float width = text.empty() ? 0 : Letter(static_cast<unsigned char>(text.front())).left;
    for (unsigned char c : text) {
        const auto letter = Letter(c);
        width += letter.width - letter.left;
    }
    return width;
}

std::shared_ptr<const ModelEdit> Tint(float r, float g, float b,bool clearSpecular=false) {
    auto edit = std::make_shared<ModelEdit>();
    edit->diffuseRgb = true;
    edit->color[0] = r; edit->color[1] = g; edit->color[2] = b;
    edit->clearSpecular=clearSpecular;
    return edit;
}

struct Poster {
    float x = 0, y = 0, z = -30, scale = 1;
    int cells = 2;
};

void SourceLight(WorldDraw& draw, int pitch, int yaw, float brightness = 1) {
    // 0x0046CDF0 builds (0,0,1)*Rx(pitch)*Ry(yaw), then transforms it by
    // the camera view for 0x004C1B70. WorldLight stores that same direction
    // in world space. Keep the current intensity, as the source helpers do.
    draw.light.table = false;
    draw.light.pitch = pitch;
    draw.light.yaw = yaw;
    for (float& color : draw.light.color) color = brightness;
}

// 0x004AE950 / 0x004AEBC0: retain the projected anchor at the near limit,
// normalize the poster's projected size, then clamp it to the original margins.
Poster ClampPoster(Poster p, bool large, float focal) {
    if (p.z > -10) {
        const float ratio = -10 / p.z;
        p.x *= ratio; p.y *= ratio; p.z = -10;
    }
    const float far = large ? -27.f : -32.f;
    const float near = large ? -23.f : -28.f;
    if (p.z < far) p.scale *= p.z / far;
    if (p.z > near) p.scale *= p.z / near;
    const float depth = p.z / focal;
    if (p.cells > (large ? 23 : 25)) p.x = 0;
    else {
        const float edge = (270.f - 8.f * float(p.cells)) * -depth;
        p.x = std::clamp(p.x, -edge, edge);
    }
    const float edgeY = -150.f * depth;
    p.y = std::clamp(p.y, -edgeY, edgeY);
    return p;
}

Poster Place(const float view[3], int cells, bool large, float focal, float scale = 1) {
    return ClampPoster({view[0], view[1], view[2] + 5, scale, cells}, large, focal);
}

bool FlatTypingMaterial(int raw) {
    // PC4AEA86..4AEAAD applies4AEB40 to these two exact source tables.
    // It clears exponent word9, preserving stage and actor highlights.
    static constexpr int ui[]={0x1CCB,0x1CAB,0x1CAC,0x1CAD,0x1CAE,0x1CAF,0x1CB0,0x1CB1,
        0x1CB2,0x1CB3,0x1CB4,0x1CB5,0x1CB6,0x1CB7,0x1CB8,0x1CB9,0x1CBA,0x1CBB,
        0x1CBC,0x1CBD,0x1CBE,0x1CBF,0x1B80,0x1B82,0x1CA5};
    static constexpr int font[]={0x1B7E,0x1B7F,0x1B81,0x1B83,0x1B84,0x1B85,0x1B86,0x1B87,
        0x1B88,0x1B89,0x1B8A,0x1B8F,0x1B90,0x1B91,0x1B92,0x1B94,0x1B96,0x1B99,
        0x1B9B,0x1B9C,0x1B9D,0x1CA4,0x1BA0,0x1BA4,0x1BBC,0x1BBD,0x1BBE,0x1BBF,
        0x1C25,0x1CA1,0x1CA6,0x1CA8,0x1CA9,0x1CAA,0x1CC1,0x1CC2,0x1CC9,0x1CE6,
        0x1CE7,0x1CF3,0x1CF4,0x1CF5,0x1CF6,0x1CF7,0x1CF8,0x1CF9,0x1CCA,0x1CCC,
        0x1CCD,0x1CCE,0x1CCF,0x1CD0,0x1CD1,0x1CD2,0x1CD3,0x1CD4,0x1CD5,0x1CD6,
        0x1CD8,0x1CD9,0x1CDA,0x1CDB,0x1CDC,0x1CDD,0x1CDE,0x1CDF,0x1CE0,0x1CE1,
        0x1CE2,0x1CE3,0x1CE4,0x1CE5,0x1B8C,0x1CC3,0x1BA3,0x1B8B,0x1B8D,0x1B8E,
        0x1B93,0x1B95,0x1B97,0x1CA2,0x1CA3,0x1CA7,0x1CC0,0x1CD7};
    return std::find(std::begin(ui),std::end(ui),raw)!=std::end(ui)||
           std::find(std::begin(font),std::end(font),raw)!=std::end(font);
}

int PreparedModel(World::Impl& w, int raw) {
    const int model = w.typingReference.SourceModel(raw);
    if (model <= 0) return 0;
    if(FlatTypingMaterial(raw)) {
        auto& current=w.modelEdits[model];
        if(!current||!current->clearSpecular) {
            auto edit=current?std::make_shared<ModelEdit>(*current):std::make_shared<ModelEdit>();
            edit->clearSpecular=true;current=std::move(edit);
        }
    }
    if (model >= int(w.modelLoaded.size()) || !w.modelLoaded[model]) {
        int file = w.typingReference.ModelFile(model);
        if (file < 0 && model < 0x4000 && w.exe.InData(0x004E83B4 + uint32_t(model) * 2))
            file = w.exe.S16(0x004E83B4 + uint32_t(model) * 2);
        if (file >= 0) w.ProcessRequest({0x52, uint32_t(file)});
    }
    return model;
}

void Model(World::Impl& w, int raw, const Poster& p, float x, float y, float sx, float sy,
           const std::shared_ptr<const ModelEdit>& tint = {}, uint32_t source = 0x004AF870, float alpha = -1) {
    const int model = PreparedModel(w, raw);
    if (model <= 0) return;
    Mat4 matrix = Mat4::Identity();
    mx::Translate(matrix, x * .1f, y * .1f, p.z * .1f);
    mx::Scale(matrix, p.scale * sx * .1f, p.scale * sy * .1f, p.scale * .1f);
    if (auto* draw = w.Draw(model, matrix, 8, alpha, false, -1, true)) {
        if(tint) {
            if(draw->edit&&draw->edit->clearSpecular&&!tint->clearSpecular) {
                auto edit=std::make_shared<ModelEdit>(*tint);edit->clearSpecular=true;draw->edit=std::move(edit);
            } else draw->edit=tint;
        }
        draw->fn = source;
        SourceLight(*draw, 0x2000, 0x2000);
    }
}

void Frame(World::Impl& w, const Poster& p, bool selected, bool compact, float color) {
    static const auto fill = Tint(0, 0, 0);
    // Source4AEE31..EBA interpolates the authored warning colour. Selection
    // ownership is shown by the pointers independently of this progress value.
    color = std::clamp(color, 0.f, 1.f);
    const auto tint = color == 0 ? Tint(1, w.frameCounter2BAC & 4 ? 1.f : 0.f, 0) :
                      color <= .5f ? Tint(1, color * 2, 0) : Tint((1 - color) * 2, 1, 0);
    const float height = compact ? 1.3f : 1.f;
    const float half = float(p.cells - 2) * p.scale * .5f;
    const float left = p.x - half, right = p.x + half;
    const float top = p.y - p.scale * (compact ? 1.65f : 1.f);
    const float middle = top + height * p.scale;
    const float bottom = middle + height * p.scale;
    const float horizontal = float(p.cells - 1) * .5f - .03f;
    // Nine authored pieces, 0x004AEDA0. Initializer4AEA1C..2F sets the
    // fifth model's diffuse RGB to black; its on-disk material is white.
    Model(w, 0x129E, p, left, top, .25f, .25f, tint);
    Model(w, 0x1373, p, p.x, top, horizontal, .25f, tint);
    Model(w, 0x170C, p, right, top, .25f, .25f, tint);
    Model(w, 0x12A3, p, left, middle, .25f, height + .25f, tint);
    Model(w, 0x19D, p, p.x, middle, float(p.cells) * .21f - .14f,
          (height + .25f) * .5f + .025f, fill);
    Model(w, 0x1738, p, right, middle, .25f, height + .25f, tint);
    Model(w, 0x12C5, p, left, bottom, .25f, .25f, tint);
    Model(w, 0x13DD, p, p.x, bottom, horizontal, .25f, tint);
    Model(w, 0x1769, p, right, bottom, .25f, .25f, tint);
    if (selected) {
        // Source4AF3F0/4AF130: P1 selects with four red rotating pointers;
        // the shared frame remains yellow. Green identifies P2's pointers.
        static const auto red = Tint(1, 0, 0,true);
        const int arrow = PreparedModel(w, 0x1CCB);
        const float dx = (float(p.cells) + .2f) * p.scale * .5f;
        const float dy = p.scale * 2.1f * (compact ? 1.3f : 1.f);
        const int rotation = int(uint32_t(w.frameCounter2BAC) * 1092u);
        for (int corner = 0; corner < 4 && arrow > 0; ++corner) {
            Mat4 matrix = Mat4::Identity();
            mx::Translate(matrix, (p.x + (corner < 2 ? -dx : dx)) * .1f,
                          (p.y + ((corner & 1) ? -dy : dy)) * .1f, p.z * .1f);
            static constexpr int angles[] = {0xA000, 0xE000, 0x6000, 0x2000};
            mx::RotZ(matrix, angles[corner]);
            mx::RotY(matrix, rotation);
            mx::Scale(matrix, p.scale * .075f, p.scale * .075f, p.scale * .075f);
            if (auto* draw = w.Draw(arrow, matrix, 8, -1, false, -1, true)) {
                draw->edit = w.typingPlayer ? Tint(0,1,0,true) : red;
                draw->fn = 0x004AF130;
                SourceLight(*draw, 0x2000, 0x2000);
            }
        }
    }
}

void Text(World::Impl& w, Poster p, std::string_view text, size_t progress,
          float y, bool reference, float vertical = 1, bool centred = true, uint32_t source = 0x004AF870, float alpha = 1) {
    static const auto yellow = Tint(1, 1, 0,true);
    static const auto white = Tint(1, 1, 1,true);
    const float firstLeft = text.empty() ? 0 : Letter(static_cast<unsigned char>(text.front())).left;
    float x = p.x + (centred ? (firstLeft - TextWidth(text)) * .5f * p.scale * .75f : 0);
    p.z += .01f;
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]) & 0x7F;
        auto letter = Letter(c);
        if (!centred && c == 'I') letter.left = 0; // original one-key I special case, 0x004AF5C5
        x -= letter.left * p.scale * .75f;
        if (i >= progress && c >= 32 && c < 128) {
            float drop = 0;
            if (c == 'g' || c == 'p' || c == 'q' || c == 'y') drop = -.2f;
            else if (c == 'j') drop = -.1f;
            Model(w, kGlyphModels[c - 32], p, x, y + drop * vertical * p.scale,
                  1, vertical, reference ? yellow : white, source, alpha == 1 ? -1.f : alpha);
        }
        x += letter.width * p.scale * .75f;
    }
}

void HudModel(World::Impl& w, int raw, float x, float y, float sx, float sy, uint32_t source,
              bool lightPulse = false, int pulseOffset = 0) {
    const int model = PreparedModel(w, raw);
    if (model <= 0 || sx <= 0 || sy <= 0) return;
    // 0x004AC040 and 0x004AC470 have identical projected size. The latter
    // uses a nearer submission and animates the light, rather than the glyph.
    const float positionScale = lightPulse ? .1f : .2f;
    Mat4 matrix = Mat4::Identity();
    mx::Translate(matrix, x * positionScale, y * positionScale, -30 * positionScale);
    mx::Scale(matrix, sx * .4f * positionScale, sy * .4f * positionScale, .4f * positionScale);
    if (auto* draw = w.Draw(model, matrix, 8, 1, false, -1, true)) {
        draw->fn = source;
        if (lightPulse) SourceLight(*draw, ((w.typingHud.perfectFrames + pulseOffset) << 10) - 0x2000, 0);
        else SourceLight(*draw, 0x2000, 0x2000);
    }
}

void AdvanceHud(World::Impl& w) {
    auto& hud = w.typingHud;
    if (hud.lastFrame == w.frame) return;
    hud.lastFrame = w.frame;
    const auto& stats = w.typing.stats();
    if (stats.perfectStreak != hud.previousStreak) {
        hud.previousStreak = stats.perfectStreak;
        hud.brokenStreak = stats.perfectStreak == 0;
        hud.perfectFrames = hud.brokenStreak ? 80 : 16;
        if (!hud.brokenStreak) w.TypingSound(0x003616A9u);
    }
    if (hud.perfectFrames > 0 && --hud.perfectFrames == 0) hud.brokenStreak = false;
    if (stats.lifeThreshold > hud.previousThreshold) {
        hud.lifeAwardAge = 0;
        hud.displayedGauge = 0;
    }
    hud.previousThreshold = stats.lifeThreshold;
    if (w.frameCounter2BAC & 2) {
        if (hud.displayedGauge < stats.lifeGauge) ++hud.displayedGauge;
        else if (hud.displayedGauge > stats.lifeGauge) --hud.displayedGauge;
    }
}

void LifeHud(World::Impl& w) {
    // 0x004871F0: authored labels and staggered seven-frame heart animation.
    for (int player = 0; player < 2; ++player) {
        if (w.player[player].state != 5 && w.player[player].state != 4) continue;
        w.TypingSprite(player ? 0x62C : 0x62B, player ? 584.f : 28.f, 412, 1, 1, 0, 0x004871F0);
        for (int life = 0; life < w.player[player].lives; ++life) {
            const int phase = ((w.frameCounter2BAC + life * 4) / 3) % 7;
            w.TypingSprite((player ? 0x73E : 0x736) + phase,
                           player ? 548.f - life * 32 : 60.f + life * 32, 412, 1, 1, 0, 0x004871F0);
        }
    }
}

void DrillPlayerHud(World::Impl& w) {
    // PC4871F0/4873AC suppress this routine in the other training scenes.
    switch (w.typingDrillGame.sourceScene) {
    case 1: case 3: case 7: case 9: case 13: case 21: case 23: break;
    default: return;
    }
    const int player = w.typingPlayer;
    if (w.player[player].state != 5 && w.player[player].state != 4) return;
    w.TypingSprite(player ? 0x62C : 0x62B, player ? 584.f : 28.f,
                   412, 1, 1, 0, 0x004871F0);
    if (w.player[player].lives > 0) {
        // The6D supervisor keeps one life in these courses; the source
        // life icon uses the same seven frames as the campaign routine.
        const int phase = (w.frameCounter2BAC / 3) % 7;
        w.TypingSprite((player ? 0x73E : 0x736) + phase, player ? 548.f : 60.f,
                       412, 1, 1, 0, 0x004871F0);
    }
}

void ContinueHud(World::Impl& w) {
    const auto digit = [](int count) { return (count + ((count >> 31) & 0xFFF)) >> 12; };
    if (w.subState == 4) {
        // Source 0x004514E9 / 0x0045151E: the shared CONTINUE screen.
        w.TypingSprite(0x242, 128, 200, 1, 1, 0, 0x004514E9);
        w.TypingSprite(0x5F + digit(w.contCountdown), 482, 188, 1, 1, 0, 0x0045151E);
        return;
    }
    for (int player = 0; player < 2; ++player) {
        const int count = w.player[player].contShown;
        if (count < 0) continue;
        const float x = player ? 368.f : 48.f;
        // Source 0x00486AC0 / 0x00486B00: one player's smaller prompt while
        // the other player continues playing. The native tasks own the timer.
        const auto submit = [&](int raw, float sx, float sy, uint32_t source) {
            const size_t before = w.hudSprites.size();
            w.TypingSprite(raw, sx, sy, 1, 1, 0, source);
            if (w.hudSprites.size() != before) {
                w.hudSprites.back().sx = .6f;
                w.hudSprites.back().sy = .8f;
            }
        };
        submit(0x242, x, 170, 0x00486AC0);
        submit(0x5F + digit(count), x + 212, 160, 0x00486B00);
    }
}

void PerfectHud(World::Impl& w, bool threeDigits = false, bool lifeGauge = true) {
    if (w.player[w.typingPlayer].state != 5 && w.player[w.typingPlayer].state != 4) return;
    auto& hud = w.typingHud;
    AdvanceHud(w);
    const auto& stats = w.typing.stats();
    const float unit = -30 / w.focal;
    const float x = unit * (w.typingPlayer ? -224 : 224), y = unit * -132;
    if (hud.brokenStreak) {
        if (hud.perfectFrames & 8) HudModel(w, 0x1CA5, x, y, 1, 1, 0x004AC150);
    } else {
        const unsigned count = std::min(threeDigits ? 999u : 99u, stats.perfectStreak);
        const bool pulse = hud.perfectFrames != 0;
        const uint32_t source = pulse ? 0x004AC470 : 0x004AC150;
        HudModel(w, 0x1CAB, x, y, 1, 1, source, pulse);
        if (threeDigits) {
            // PC4AC246..397: Accuracy3 keeps three digits. The first two
            // use the authored offsets in their models; only the last moves.
            HudModel(w, 0x1CAC + int(count / 100), x, y, 1, 1, source, pulse, 4);
            HudModel(w, 0x1CB6 + int(count / 10 % 10), x, y, 1, 1, source, pulse, 8);
            HudModel(w, 0x1CB6 + int(count % 10), x + 1.149999976158142f, y,
                     1, 1, source, pulse, 12); // source float5895D0
        } else {
            HudModel(w, 0x1CAC + int(count / 10), x, y, 1, 1, source, pulse, 4);
            HudModel(w, 0x1CB6 + int(count % 10), x, y, 1, 1, source, pulse, 8);
        }
    }
    if (!lifeGauge) return;
    const float gaugeX = unit * (w.typingPlayer ? -10 : 310), gaugeY = unit * -154;
    HudModel(w, 0x1C28, gaugeX, gaugeY, 1, 1, 0x004AC720);
    if (stats.lifeThreshold != 0) {
        // The source's empty immediate fill is negative/degenerate and has no
        // visible span. Do not submit a singular transform for either empty fill.
        const float actual = stats.lifeGauge ? float(stats.lifeGauge - 1) / float(stats.lifeThreshold) : 0;
        const float trailing = float(hud.displayedGauge) / float(stats.lifeThreshold);
        HudModel(w, 0x1C27, gaugeX, gaugeY, actual, 1, 0x004AC720);
        HudModel(w, 0x1C26, gaugeX, gaugeY, trailing, 1, 0x004AC720);
    }
    if (hud.lifeAwardAge >= 0 && hud.lifeAwardAge <= 50) {
        const int model = PreparedModel(w, 0x11B3 + hud.lifeAwardAge);
        Mat4 matrix = Mat4::Identity();
        mx::Translate(matrix, w.typingPlayer ? 1.8790001f : -1.8790001f, 1.597f, -5.1200004f);
        mx::RotZ(matrix, 0x8000);
        mx::Scale(matrix, .1f, .1f, .1f);
        if (model > 0) if (auto* draw = w.Draw(model, matrix, 8, 1, false, -1, true)) draw->fn = 0x004AC5A0;
        if (hud.lifeAwardAge == 15) w.TypingSound(0x003B17A9u);
        if (++hud.lifeAwardAge > 50) hud.lifeAwardAge = -1;
    }
}

void QuestionHud(World::Impl& w) {
    if (w.typingQuestion.empty() || !w.typingHud.questionVisible || w.player[0].state != 5) return;
    // 0x004B0720 / 0x004AFC10. Gameplay owns the existing reveal counter; this
    // is only its presentation, including the same -960..0 slide coordinate.
    const float unit = -30 / w.focal;
    Poster p{unit * float(w.typingHud.questionSlide), unit * 130, -30, 1, 2};
    Text(w, p, w.typingQuestion, 0, p.y - .4f, true, 1, true, 0x004AFC10);
    w.TypingSprite(0x97B, 320, 360, 1, 31, 0xA, 0x004B0720);
}

bool CurrentAnchor(World::Impl& w, uint32_t serial, uint32_t token, std::array<float, 2>& anchor) {
    const auto position = w.typingPositions.find({serial, token});
    if (position != w.typingPositions.end()) { anchor = position->second; return true; }
    const auto target = w.typingCachedTargets.find(serial);
    if (target == w.typingCachedTargets.end() || target->second.first.token != token) return false;
    const float* view = target->second.second.view;
    if (!std::isfinite(view[0]) || !std::isfinite(view[1]) || !std::isfinite(view[2]) || view[2] >= -.1f) return false;
    anchor = {320 - w.focal * view[0] / view[2], 240 + w.focal * view[1] / view[2]};
    return true;
}

void GradeModel(World::Impl& w, int raw, const Poster& poster, float sx, float sy, float alpha) {
    const int model = PreparedModel(w, raw);
    if (model <= 0 || sx < 0 || sy <= 0) return; // Age90 intentionally submits zero-width geometry.
    Mat4 matrix = Mat4::Identity();
    mx::Translate(matrix, poster.x * .2f, poster.y * .2f, poster.z * .2f);
    mx::Scale(matrix, poster.scale * sx * .08f, poster.scale * sy * .08f, poster.scale * .08f);
    if (auto* draw = w.Draw(model, matrix, 8, alpha, false, -1, true)) {
        draw->fn = 0x004AC960;
        SourceLight(*draw, 0x2000, 0x2000);
    }
}

struct WordGlassTask : WTask {
    World::Impl* owner=nullptr;
    uint32_t epoch=0,serial=0;
    std::array<float,3> position{},velocity{},rotation{},spin{};
    float size=1;
    int model=0,age=0,lastFrame=-1;
};

bool GlassFrame(World::Impl& w,WordGlassTask& glass) {
    // PC40A370 submits ages1..60. Position and Euler rotation advance once
    // per task tick; the final transparent submission precedes task removal.
    if(glass.lastFrame!=w.frame) {
        glass.lastFrame=w.frame;
        if(++glass.age>60)return false;
        glass.velocity[1]-=.009999999776482582f;
        for(int axis=0;axis<3;++axis) {
            glass.position[axis]+=glass.velocity[axis];
            glass.rotation[axis]+=glass.spin[axis];
        }
    }
    float scale=glass.size;
    const bool large=w.cfg.typingOptions.wordSize==TypingWordSize::Large;
    const float far=large?-27.f:-32.f,near=large?-23.f:-28.f;
    if(glass.position[2]<far)scale*=glass.position[2]/far;
    if(glass.position[2]>near)scale*=glass.position[2]/near;
    const int model=PreparedModel(w,glass.model);
    if(model<=0)return true;
    Mat4 matrix=Mat4::Identity();
    mx::Translate(matrix,glass.position[0],glass.position[1],glass.position[2]);
    mx::RotX(matrix,int(glass.rotation[0]));
    mx::RotY(matrix,int(glass.rotation[1]));
    mx::RotZ(matrix,int(glass.rotation[2]));
    mx::Scale(matrix,scale,scale,scale);
    const float age=glass.age*.01666666753590107f;
    if(auto* draw=w.Draw(model,matrix,8,1-age*age,false,-1,true)) {
        draw->fn=0x0040A370;draw->task=glass.serial;
        SourceLight(*draw,0x2000,0x2000);
    }
    return true;
}

void GlassTaskFrame(Task& task) {
    auto& glass=task.As<WordGlassTask>();
    auto& w=*glass.owner;
    if(glass.epoch!=w.typingEpoch || !w.cfg.portTyping || !w.typingReferenceActive)
        w.ts.KillCurrent();
    if(w.paused)return;
    if(!GlassFrame(w,glass))w.ts.KillCurrent();
}

void ShatterPoster(World::Impl& w,const std::array<float,3>& anchor,int cells) {
    if(cells<=0 || w.focal<=0)return;
    // PC40A150 uses the word post's frozen XYZ, the4AE950 near clamp and
    // 4AEBC0's original English margins. Its grid keeps the unadjusted .95
    // size; each moving shard applies its own depth normalization afterward.
    std::array<float,3> origin=anchor;
    if(origin[2]>-10) {
        const float ratio=-10/origin[2];
        origin[0]*=ratio;origin[1]*=ratio;origin[2]=-10;
    }
    constexpr float size=.949999988079071f;
    const float depth=origin[2]/w.focal;
    const bool large=w.cfg.typingOptions.wordSize==TypingWordSize::Large;
    if(cells>(large?23:25))origin[0]=0;
    else {
        const float edge=(270-8*cells*size)*-depth;
        origin[0]=std::clamp(origin[0],-edge,edge);
    }
    origin[1]=std::clamp(origin[1],150*depth,-150*depth);
    for(int row=1;row>=-2;--row)for(int column=0;column<cells;++column) {
        auto data=std::make_unique<WordGlassTask>();
        auto& glass=*data;
        glass.fn=0x0040A370;glass.owner=&w;glass.epoch=w.typingEpoch;
        const int horizontal=(cells-1)/2-column;
        const int velocityColumn=cells/2-column;
        glass.position={origin[0]+horizontal*size*.75f,origin[1]+row*size*.75f,origin[2]};
        glass.size=(1-(w.Rand(0x0040A22F)%25)*.009999999776482582f)*size;
        glass.velocity[0]=velocityColumn*.05000000074505806f;
        glass.velocity[1]=(w.Rand(0x0040A264)%5)*.07000000029802322f;
        glass.velocity[2]=(w.Rand(0x0040A282)%5)*-.05000000074505806f;
        glass.spin[0]=float((w.Rand(0x0040A2A0)%20)*8192/90);
        glass.spin[1]=float((w.Rand(0x0040A2D0)%80)*8192/90);
        glass.spin[2]=float((w.Rand(0x0040A300)%20)*8192/90);
        glass.model=0xD56+w.Rand(0x0040A330)%5;
        Task* task=w.ts.Create(GlassTaskFrame,std::move(data));
        glass.serial=task->serial;
        GlassFrame(w,glass); // A source sibling created on completion runs in that same frame.
    }
}

void FeedbackHud(World::Impl& w) {
    auto& hud = w.typingHud;
    for (auto entry = hud.mistakes.begin(); entry != hud.mistakes.end();) {
        auto& mistake = entry->second;
        if (mistake.lastFrame != w.frame) {
            if (mistake.lastFrame >= 0) ++mistake.age;
            mistake.lastFrame = w.frame;
        }
        if (mistake.age >= 16) { entry = hud.mistakes.erase(entry); continue; }
        CurrentAnchor(w, mistake.serial, mistake.token, mistake.anchor);
        if (w.frameCounter2BAC & 4) {
            float y = mistake.anchor[1];
            if (w.runner && w.runner->scene == 0xF) y -= 48; // source 0x004A986C
            w.TypingSprite(0xBD1, mistake.anchor[0], y, 1, 1, 0xA, 0x004A9800);
        }
        ++entry;
    }
    for (auto entry = hud.completions.begin(); entry != hud.completions.end();) {
        auto& complete = *entry;
        if (complete.lastFrame != w.frame) {
            if (complete.lastFrame >= 0) ++complete.age;
            complete.lastFrame = w.frame;
        }
        if (complete.age > 90) { entry = hud.completions.erase(entry); continue; }
        // 0x0040A8C0 / 0x0040ABBC: frozen word anchor, then 0x004AC960
        // performs its own eight-cell placement. Ages 0..90 are submitted.
        auto view = complete.view;
        view[2] += .01f;
        Poster poster = Place(view.data(), 8, w.cfg.typingOptions.wordSize == TypingWordSize::Large, w.focal, 1.2f);
        const float alpha = 1.5f - float(complete.age) * .011111111380159855f;
        float sx = 1, sy = 1;
        const int remaining = 90 - complete.age;
        if (remaining < 15) {
            sx = float(remaining) * .06666667014360428f;
            sy = 1 + 15.f / (float(remaining) + .01f);
        }
        Poster backdrop = poster;
        backdrop.scale *= .75f;
        backdrop.z -= .01f;
        GradeModel(w, 0x1B80, backdrop, sx, sy, alpha);
        const unsigned grade = static_cast<unsigned>(complete.grade);
        if (grade < 5) {
            GradeModel(w, 0x1CC3, poster, sx, sy, alpha);
            GradeModel(w, 0x1CC4 + int(grade), poster, sx, sy, alpha);
        }
        ++entry;
    }
}

using PosterState = World::Impl::TypingHudPresentation::PosterState;

void MovePoster(PosterState& poster, const std::array<float, 3>& target, uint32_t flags, int frame) {
    if (poster.lastFrame == frame) return;
    if (poster.lastFrame < 0 || (flags & 0x00508000u)) poster.view = target;
    const float z = target[2] + 5;
    const float correction = poster.view[2] == 0 ? 1 : 1 + (z - poster.view[2]) / z;
    for (int axis = 0; axis < 2; ++axis) {
        const float delta = target[axis] - poster.view[axis];
        if (std::abs(delta) > 10)
            poster.view[axis] += (delta < 0 ? -1.f : 1.f) * (std::abs(delta) - 10);
        poster.view[axis] = (poster.view[axis] + (target[axis] - poster.view[axis]) * .03333333507180214f) * correction;
    }
    poster.view[2] = z;
    poster.lastFrame = frame;
}

struct WordVisual {
    std::array<float, 3>* view;
    std::string_view text;
    size_t progress = 0;
    uint32_t flags = 0, source = 0x004AF870;
    bool selected = false;
    int cells = 2;
    float alpha = 1;
    float frameColor = 1;
};

void SpreadPosters(std::vector<WordVisual>& words, float focal) {
    // 0x004AD7F0 works in task order, updating the first word of each pair.
    for (size_t i = 0; i < words.size(); ++i) {
        if (words[i].flags & 0x02000000u) continue; // the source's separate word-poster renderer
        auto& a = *words[i].view;
        for (size_t j = 0; j < words.size(); ++j) {
            if (i == j) continue;
            auto& b = *words[j].view;
            if (a[2] == 0 || b[2] == 0) continue;
            for (auto* view : {&a, &b}) {
                const float depth = (*view)[2] / focal;
                (*view)[0] = std::clamp((*view)[0], 270.f * depth, -270.f * depth);
                (*view)[1] = std::clamp((*view)[1], 150.f * depth, -150.f * depth);
            }
            const float ay = focal * a[1] / a[2], by = focal * b[1] / b[2];
            const float dy = ay - by;
            if (std::abs(dy) >= 96) continue;
            const float dx = focal * a[0] / a[2] - focal * b[0] / b[2];
            if (std::abs(dx) >= float(words[i].cells) * 25 && words[i].cells <= 8) continue;
            float depth = a[2] / focal;
            if (ay <= by) depth = -depth;
            a[1] += (96.f - std::abs(dy)) * depth * .2f;
        }
    }
}

float DistanceColor(World::Impl& w, uint32_t serial, const float view[3], uint32_t flags, float overrideColor) {
    if (flags & 0x04000000u) return overrideColor;
    float body[3];
    bool found = false;
    for (Task* task : w.ts.Live()) if (task->serial == serial && task->alive) {
        if (auto* enemy = dynamic_cast<EnemyTask*>(task->data.get())) std::copy_n(enemy->body, 3, body);
        else mx::Transform(w.cam.inverse, view, body);
        found = true;
        break;
    }
    if (!found) return .5f; // component fixtures have no native actor or distance
    const float dx = body[0] - w.playerPos[0], dz = body[2] - w.playerPos[2];
    const float distance = std::sqrt(dx * dx + dz * dz);
    if (distance <= 20) return 0;
    if (distance >= 60) return 1;
    // Source4B1972 stores4B2680's result in word+48, the frame colour,
    // leaving both glyph lines opaque.
    return (distance - 20) * .025f;
}

void DrawWord(World::Impl& w, const WordVisual& word, bool compact) {
    const auto& view = *word.view;
    Poster p = ClampPoster({view[0], view[1], view[2], 1, word.cells},
                                w.cfg.typingOptions.wordSize == TypingWordSize::Large, w.focal);
    if (compact) p.y += (w.typingPlayer ? -1.f : 1.f) * 1.8f;
    const bool twoRows = compact && word.text.size() > 1;
    const size_t firstDraw = w.draws.size();
    Frame(w, p, word.selected, twoRows, word.frameColor);
    if (word.text.size() > 1) {
        Text(w, p, word.text, 0, p.y + p.scale * .13f, true, 1, true, word.source, word.alpha);
        Text(w, p, word.text, word.progress, p.y - p.scale * (twoRows ? 1.04f : 1.3f), false,
             twoRows ? .8f : 1.f, true, word.source, word.alpha);
    } else {
        Poster key = p;
        key.scale *= 1.2f;
        Text(w, key, word.text, word.progress, p.y - p.scale * .65f, false, 1, false, word.source, word.alpha);
    }
    for (size_t i = firstDraw; i < w.draws.size(); ++i) {
        w.draws[i].fn = word.source;
        if (word.flags & 0x00010000u) SourceLight(w.draws[i], 0x2000, 0x2000, .1f);
    }
}

} // namespace

void TypingStrengthPostersHud(World::Impl& w, uint32_t owner, uint32_t generation,
                             const std::array<TypingWordRecord, 4>& words, unsigned line,
                             bool available, int deadline, int bossState) {
    auto& story = w.typingHud.story;
    if (story.owner != owner || story.generation != generation) {
        story = {};
        story.owner = owner; story.generation = generation;
        const float unit = -40 / w.focal;
        for (unsigned i = 0; i < 4; ++i) {
            story.lines[i].word = words[i];
            story.lines[i].view = {unit * -200, unit * 130, -40};
        }
    }
    story.active = true; story.line = line; story.available = available;
    story.deadline = deadline; story.bossState = bossState;
    if (story.lastFrame == w.frame) return;
    story.lastFrame = w.frame;
    ++story.age;
    const float top = (-40 / w.focal) * 35;
    for (unsigned i = 0; i < 4; ++i) {
        auto& poster = story.lines[i];
        if (i < line) { poster.phase = 4; poster.enabled = false; continue; }
        if (poster.phase >= 4) continue;
        if (i == 0 && story.age > 180 && bossState != 20) poster.enabled = true;
        if (!poster.enabled) continue;
        if (poster.phase == 0) poster.phase = 1;
        if (poster.phase == 1) {
            poster.velocity = std::clamp(float(double(poster.velocity) - .1), -2.f, 2.f);
            poster.view[0] += poster.velocity;
            if (poster.view[0] < 0) { poster.view[0] = 0; poster.velocity *= -.6f; }
            if (++poster.bounce > 60) { poster.view[0] = 0; poster.phase = 2; }
            continue;
        }
        if (poster.phase != 2) continue;
        if (i && story.lines[i-1].phase < 4) continue;
        if (poster.view[1] < top) { poster.view[1] = float(double(poster.view[1]) + .5); continue; }
        if (i && ++poster.hold < 60) continue;
        poster.phase = 3;
        if (i + 1 < 4) story.lines[i+1].enabled = true;
    }
}

void TypingWordMistakeHud(World::Impl& w, uint32_t serial) {
    if (!w.typingReferenceActive || !serial) return;
    const auto target = w.typingCachedTargets.find(serial);
    if (target == w.typingCachedTargets.end()) return;
    World::Impl::TypingHudPresentation::Mistake mistake;
    mistake.serial = serial;
    mistake.token = target->second.first.token;
    if (!CurrentAnchor(w, serial, mistake.token, mistake.anchor)) return;
    w.typingHud.mistakes[serial] = mistake;
    w.TypingSound(0x005517A9u); // P1's selected-word wrong-key cue, 0x004B1FF5.
}

void TypingWordCompleteHud(World::Impl& w, const TypingCompletion& done,std::string_view text) {
    if (!w.typingReferenceActive) return;
    const auto target = w.typingCachedTargets.find(done.serial);
    if (target == w.typingCachedTargets.end() || target->second.first.token != done.token) return;
    const float* view = target->second.second.view;
    if (!std::isfinite(view[0]) || !std::isfinite(view[1]) || !std::isfinite(view[2]) || view[2] >= -5) return;
    World::Impl::TypingHudPresentation::Complete complete;
    std::copy_n(view, 3, complete.view.begin());
    const auto poster = w.typingHud.posters.find({done.serial, done.token});
    if (poster != w.typingHud.posters.end()) {
        complete.view = poster->second.view;
        complete.view[2] -= 5; // Feedback's Place adds the source depth offset again.
    }
    if(w.cfg.portTyping && !text.empty()) {
        auto anchor=complete.view;
        anchor[2]+=5;
        ShatterPoster(w,anchor,int(TextWidth(text)+2.3f));
    }
    complete.grade = done.visualRank;
    w.typingHud.completions.push_back(complete);
    auto& story = w.typingHud.story;
    if (story.active && story.owner == done.serial && story.line < 4) {
        story.lines[story.line].phase = 4;
        story.lines[story.line].enabled = false;
    }
}

void TypingWordCompleteHud(World::Impl& w,const TypingCompletion& done) {
    TypingWordCompleteHud(w,done,{});
}

void DrawTypingDrillPrompt(World::Impl& w,std::string_view text,size_t progress) {
    if(!w.typingReferenceActive || text.empty())return;
    const int cells=std::max(3,int(TextWidth(text)+2.3f));
    const auto& ui=w.typingDrill;
    const float fallback[]={0,-3,-35};
    const float* anchor=ui.visualAnchorValid?ui.visualAnchor.data():fallback;
    Poster poster=Place(anchor,cells,false,w.focal);
    Frame(w,poster,true,false,0);
    const float vertical=poster.scale;
    Text(w,poster,text,0,poster.y+.25f*vertical,true);
    Text(w,poster,text,std::min(progress,text.size()),poster.y-.75f*vertical,false);
}

void TypingWordHud(World::Impl& w) {
    if (!w.typingReferenceActive || w.typingReference.SourceModel(0x129E) <= 0 || w.focal <= 0) return;
    if (w.gameMode == 6 && !w.paused && !w.typingResultsActive && !w.stageResults.shown && !TypingDrillGameActive(w)) {
        if (w.typingPlayer == 0) { LifeHud(w); ContinueHud(w); }
        PerfectHud(w);
        if (w.typingPlayer == 0) QuestionHud(w);
    }
    if (w.gameMode == 6 && w.subState == 2 && !w.paused &&
        TypingDrillGameActive(w) &&
        (TypingDrillGameRunning(w) || TypingDrillGameFinished(w)) && !(w.flags5900 & 0x18)) {
        // PC4AD719..75E: training scene15 has PERFECT and LIFE; scene13
        // has only the three-digit PERFECT counter. The source flags also
        // hide these during Instructions and the Finish presentation; the
        // source Ready/Go presentation precedes this gameplay HUD.
        const int scene = w.typingDrillGame.sourceScene;
        DrillPlayerHud(w);
        if (scene == 15 || scene == 13) PerfectHud(w, scene == 13, scene == 15);
    }
    if (w.gameMode == 6 && !w.paused) FeedbackHud(w);
    auto& hud = w.typingHud;
    for (auto poster = hud.posters.begin(); poster != hud.posters.end();) {
        const auto cached = w.typingCachedTargets.find(poster->first.first);
        if (cached == w.typingCachedTargets.end() || cached->second.first.token != poster->first.second)
            poster = hud.posters.erase(poster);
        else ++poster;
    }
    if (!w.typingWasReady) return;
    const bool compact = ((w.player[0].state == 5 || w.player[0].state == 4) &&
                          (w.player[1].state == 5 || w.player[1].state == 4)) ||
                         ((w.player[0].state == 7 || w.player[0].state == 4) &&
                          (w.player[1].state == 7 || w.player[1].state == 4)) || w.gameMode == 5;
    const auto prompts = w.typing.prompts();
    std::vector<WordVisual> words;
    bool storyPrompt = false;
    for (const auto& prompt : prompts) {
        const auto cached = w.typingCachedTargets.find(prompt.serial);
        if (cached == w.typingCachedTargets.end() || cached->second.first.token != prompt.token) continue;
        std::array<float, 3> target;
        std::copy_n(cached->second.second.view, 3, target.begin());
        float frameColor = 1;
        // Source4B1A3C follows the actor's body point at10C..114. A hit
        // node still determines the recipient of a completed word, while its
        // torso point positions the poster independently of that node.
        for (Task* task : w.ts.Live()) {
            if (task->serial != prompt.serial || !task->alive) continue;
            if (const auto* actor = dynamic_cast<const ActorTask*>(task->data.get()))
                frameColor = actor->typingFrameColor;
            if (const auto* enemy = dynamic_cast<const EnemyTask*>(task->data.get());
                !cached->second.second.boss && enemy && (enemy->anim.drawFlags & 1) && enemy->bodyView[2] < -.1f)
                std::copy_n(enemy->bodyView, 3, target.begin());
            break;
        }
        // Source4B19AB..1A14 applies authored screen-space poster offsets.
        const uint32_t flags = cached->second.first.flags;
        if (flags & 0x800u) target[1] += 64.f / w.focal * target[2];
        if (flags & 0x1000u) target[1] -= 96.f / w.focal * target[2];
        if (flags & 0x2000u) target[1] -= 48.f / w.focal * target[2];
        frameColor = DistanceColor(w, prompt.serial, target.data(), flags, frameColor);
        if (hud.story.active && hud.story.owner == prompt.serial && hud.story.line < 4) {
            target = hud.story.lines[hud.story.line].view;
            storyPrompt = true;
            if (hud.story.deadline >= 0)
                frameColor = hud.story.bossState != 4 && hud.story.bossState != 20 && hud.story.deadline > 0 ? 0.f :
                             std::min(1.f, float(hud.story.deadline) * .0033333334140479565f);
        }
        if (!std::isfinite(target[0]) || !std::isfinite(target[1]) || !std::isfinite(target[2]) || target[2] >= -5) continue;
        const int cells = int(TextWidth(prompt.text) + 2.3f); // 0x004B06F0
        auto& poster = hud.posters[{prompt.serial, prompt.token}];
        MovePoster(poster, target, cached->second.first.flags, w.frame);
        if (TypingDrillGameActive(w) && (flags & 0x4000u)) {
            // PC4AD545..59B fixes these posts in view space after their
            // ordinary actor tracking. Speed3 uses the lower authored y.
            const float y = w.typingDrillGame.sourceScene == 21 ? 256.f : 100.f;
            poster.view = {0, (-40.f / w.focal) * y, -40.f};
        }
        WordVisual word{&poster.view, prompt.text};
        word.progress = prompt.progress; word.flags = cached->second.first.flags;
        word.selected = prompt.selected;
        word.cells = cells; word.frameColor = frameColor;
        words.push_back(word);
    }
    std::array<std::array<float, 3>, 4> previews;
    if (hud.story.active) for (unsigned i = 0; i < 4; ++i) {
        const auto& line = hud.story.lines[i];
        if (!line.enabled || line.phase >= 4 || (storyPrompt && i == hud.story.line)) continue;
        const float z = line.view[2] + 5;
        const float correction = 1 + (z - line.view[2]) / z;
        previews[i] = {line.view[0] * correction, line.view[1] * correction, z};
        WordVisual word{&previews[i], line.word.text};
        word.flags = 0x34120008u;
        if (line.phase != 3 || i != hud.story.line || !hud.story.available) word.flags |= 0x10000u;
        if (hud.story.deadline >= 0)
            word.frameColor = hud.story.bossState != 4 && hud.story.bossState != 20 && hud.story.deadline > 0 ? 0.f :
                              std::min(1.f, float(hud.story.deadline) * .0033333334140479565f);
        word.cells = int(TextWidth(word.text) + 2.3f); word.source = 0x0048B190;
        words.push_back(word);
    }
    if (hud.postersFrame != w.frame) {
        SpreadPosters(words, w.focal);
        hud.postersFrame = w.frame;
    }
    for (const auto& word : words) DrawWord(w, word, compact);
}

} // namespace game
