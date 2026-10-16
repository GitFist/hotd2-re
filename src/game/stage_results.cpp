#include "game/stage_results.h"
#include <algorithm>

namespace game {
void StageResultsSprites(const ExeImage& exe, const StageResults& r, int visibleFrames, std::vector<SpriteDef>& out) {
    MenuBackground(out);
    auto text = [&](float x, float y, const std::string& value, float scale = .75f, uint32_t flags = 0) {
        MenuTextScaled(exe, x, y, value, flags, scale, out);
    };
    auto center = [&](float y, const std::string& value, float scale, uint32_t flags = 0) {
        text(320 - value.size() * MenuTextAdvance(exe, scale) / 2, y, value, scale, flags);
    };
    center(30, "CHAPTER " + std::to_string(r.chapter) + " RESULTS", 1);
    center(66, "STAGE CLEAR", .7f, 0x40);
    text(276, 111, "PLAYER 1", .75f, 0x10);
    const size_t blueStart = out.size();
    text(458, 111, "PLAYER 2");
    for (size_t i = blueStart; i < out.size(); ++i) {
        out[i].tint[0] = .3f; out[i].tint[1] = .7f; out[i].tint[2] = 1;
    }
    const char* labels[] = {"STAGE SCORE", "SHOTS FIRED", "ACCURACY", "HEADSHOTS", "RESCUE CREDIT", "HITS TAKEN", "CONTINUES"};
    for (int row = 0; row < 7; ++row) {
        const float y = 148.0f + 29.0f * row;
        text(42, y, labels[row], .7f);
        for (int p = 0; p < 2; ++p) {
            const auto& s = r.player[p];
            const int values[] = {s.score, s.shots, s.shots ? std::clamp(s.hits * 100 / s.shots, 0, 100) : 0,
                                  s.headshots, s.rescues, s.damage, s.continues};
            std::string value = s.participated ? std::to_string(values[row]) : "NA";
            if (s.participated && row == 2) value += " PCT";  // original menu font has no percent glyph
            text(324.0f + p * 182 - value.size() * MenuTextAdvance(exe, .75f) / 2, y, value);
        }
    }
    const int total = (r.player[0].participated ? r.player[0].score : 0) +
                      (r.player[1].participated ? r.player[1].score : 0);
    center(363, "TEAM SCORE " + std::to_string(total) + "   RESCUED " + std::to_string(r.civilians), .7f);
    center(391, "LIFE BONUS " + std::to_string(r.lifeBonus), .65f, 0x40);
    const int remaining = std::max(0, StageResults::kMinimumFrames - visibleFrames);
    center(435, remaining ? "CONTINUE IN " + std::to_string((remaining + 59) / 60)
                          : "CLICK SHOOT OR ENTER TO CONTINUE", .7f, 0x40);
}
}  // namespace game
