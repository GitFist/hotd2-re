#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include "game/typing_rules.h"
#include "game/typing_original.h"

namespace game {

// Source492950..2997: these are chapter results, separate from chooser status.
struct TypingChapterRecord {
    uint8_t status = 0, ordinal = 0; // No Clear / Clear / Game Over; percentile 1..99.
    uint16_t points = 0;
    float deviation = 0;
};
static_assert(sizeof(TypingChapterRecord) == 8);

struct TypingRunMetrics {
    uint64_t words = 0, speedFrames = 0, speedCharacters = 0;
    uint64_t reactionFrames = 0, reactions = 0;
    uint64_t smallThreatHits = 0, propWords = 0, completedProps = 0, otherTargetHits = 0;
    uint64_t concentrationBase = 0, concentrationPoints = 0;
};

struct TypingFinalEvaluation {
    uint8_t originalMode = 0, player = 0, proficiency = 0;
    std::array<uint8_t,5> grades = {4,4,4,4,4};
    std::array<char,8> name{};
    uint32_t skillPoints = 100;
    std::array<TypingChapterRecord,6> chapters;
};

// Source489180's separate160-byte score ranking records. Registration strings
// use the English PC's12/2/12/45character limits; proficiency is already packed.
struct TypingScoreRecord {
    uint16_t points = 0, year = 0;
    uint8_t player = 0, proficiency = 0, month = 0, day = 0;
    std::string name, age, occupation, comment;
};

uint8_t TypingPackedProficiency(unsigned value); // Source40A800, class/subgrade nibbles.
unsigned TypingProficiency(const TypingRunMetrics& metrics); // Source40A730/40A7C0.
unsigned TypingConcentrationWeight(std::string_view text); // Source484510.
TypingFinalEvaluation TypingEvaluateRun(const TypingRunMetrics& metrics,
    const std::array<TypingChapterRecord,6>& chapters, uint64_t correct, uint64_t attempted,
    unsigned totalCivilians, unsigned rescuedCivilians, bool cleared);

// Source494970's chooser starts with the final chapter reserved. Completing a
// later chapter retires earlier unplayed chapters; completing five opens six.
struct TypingChapterChoice {
    std::array<uint8_t,6> status = {0,0,0,0,0,2};
    bool canPlay(int chapter) const;
    void complete(int chapter);
};

class TypingProfile {
public:
    TypingProfile();
    TypingChapterRecord chapterResult(int chapter, uint64_t points, bool store);
    unsigned chapterBest(int chapter) const;
    float chapterMean(int chapter, uint64_t points) const;
    int finalRank(const TypingFinalEvaluation& evaluation) const;
    int storeFinal(const TypingFinalEvaluation& evaluation, std::string_view name);
    const auto& finalRecords() const { return final_; }
    int scoreRank(const TypingScoreRecord& record, bool original = false) const;
    int storeScore(const TypingScoreRecord& record, bool original = false);
    const auto& scoreRecords(bool original = false) const { return scores_[original?1:0]; }
    const TypingOriginalProgress& originalProgress() const { return original_; }
    uint8_t storeOriginalCoins(int chapter, uint8_t earned);
    bool load(const std::string& path);
    bool save(const std::string& path) const;
private:
    struct History {
        std::array<uint16_t,100> scores{};
        uint16_t count = 50, next = 50, best = 0;
    };
    std::array<History,6> history_;
    std::array<TypingFinalEvaluation,8> final_;
    std::array<std::array<TypingScoreRecord,8>,2> scores_;
    TypingOriginalProgress original_;
};

} // namespace game
