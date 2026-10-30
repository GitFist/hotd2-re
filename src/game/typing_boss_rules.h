#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "game/typing_words.h"

namespace game {

// Tower's ZE01 banks contain record zero (question), eight correct answers,
// then incorrect answers. Three heads (0, 2, 4) show one correct answer and
// two incorrect answers. The caller supplies the selected record indices so
// its shared dictionary RNG and initial-letter reservation remain authoritative.
enum class TypingQuizOutcome { None, Correct, Wrong, Timeout };

constexpr unsigned TypingJudgmentWordDamage(bool cooperative = false) { return cooperative ? 35 : 40; }
constexpr float TypingJudgmentFirstPhaseStep() { return 0.3f; }
int TypingJudgmentFirstPath(int tableIndex);
int TypingJudgmentSecondPath(int tableIndex);
int TypingJudgmentSecondPathGroup(int aggression);
float TypingJudgmentAdvancePathClock(int& wholeTicks, float& fraction, int rank);
constexpr unsigned TypingHierophantWordDamage() { return 30; }
constexpr unsigned TypingTowerSwimmingWordDamage() { return 24; }
constexpr unsigned TypingMagicianWordDamage(int behaviour) { return behaviour >= 10 ? 20 : 15; }
// Supplied English PC tables 5B33FC, 5B34D8 and 5B35E8. These are
// authored typing windows, independent of the gun game's attack tables.
struct TypingMagicianTiming { int move, hold, repeats, charge, projectile; };
TypingMagicianTiming TypingMagicianTimingForRank(int rank);
float TypingMagicianPhaseThreshold(int maximumLife, int phase);
constexpr int TypingMagicianPhaseBehaviour(int phase) { return phase < 4 ? 8 : phase == 4 ? 10 : -1; }

unsigned TypingQuizTimeFrames(uint16_t correctAnswerMeasure, int adaptiveRank);
constexpr std::array<unsigned, 3> kTypingQuizHeadIndices{0, 2, 4};

class TypingQuizChallenge {
public:
    bool begin(std::span<const TypingWordRecord> records,
               const std::array<uint32_t, 3>& answerRecordIndices);
    const TypingWordRecord& question() const { return question_; }
    const std::array<TypingWordRecord, 3>& answers() const { return answers_; }
    size_t correctChoice() const { return correctChoice_; }
    bool valid() const { return valid_; }
    bool acceptingAnswers() const { return accepting_; }
    unsigned remainingFrames() const { return remainingFrames_; }
    TypingQuizOutcome outcome() const { return outcome_; }
    void revealComplete(int adaptiveRank);
    TypingQuizOutcome answer(size_t choice);
    TypingQuizOutcome tick(unsigned activeFrames = 1);

private:
    TypingWordRecord question_{};
    std::array<TypingWordRecord, 3> answers_{};
    size_t correctChoice_ = 0;
    bool valid_ = false, accepting_ = false;
    unsigned remainingFrames_ = 0;
    TypingQuizOutcome outcome_ = TypingQuizOutcome::None;
};

// Correct answers subtract HP, independent of the A..E typing grade. Incorrect
// answers and expiry schedule the head's attack; damage occurs in that attack.
constexpr unsigned TypingTowerStartingLife(bool variantTwo = false) { return variantTwo ? 150 : 180; }
constexpr unsigned TypingTowerCorrectDamage(bool variantTwo = false) { return variantTwo ? 21 : 30; }

// Strength assigns one four-record ZE04 story in order. Only its fourth
// completed line changes HP; an expired line leads to the native swing.
unsigned TypingStrengthTimeFrames(uint16_t lineMeasure, int adaptiveRank);
int TypingStrengthLifeAfterStory(int currentLife, int maximumLife);
bool TypingStrengthSwingWindow(uint16_t relativeYaw, float distance);
float TypingStrengthTrailingCameraTick(float currentTick, bool approaching);
float TypingStrengthNextCameraTick(float currentTick, bool secondArena, bool holdForFlinch);

// The next poster bounces in while the current line can be typed. Its rise
// waits for the preceding line's completion, followed by a sixty-frame hold.
class TypingStrengthReveal {
public:
    void reset() { line_ = 0; wait_ = 254; nextBounce_ = 0; finished_ = false; }
    void tick();
    bool completeLine(); // True only when the fourth available line completes.
    bool available() const { return wait_ == 0 && !finished_; }
    unsigned line() const { return line_; }
private:
    unsigned line_ = 0, wait_ = 254, nextBounce_ = 0;
    bool finished_ = false;
};

// Emperor uses ZE06 record zero as the question and records 1..3 as answers.
// Every answer damages it; the dictionary flags, rather than choice position,
// accumulate the value used by the ending script's three-way condition.
constexpr unsigned TypingEmperorAnswerDamage(bool cooperative = false) { return cooperative ? 31 : 22; }
// Supplied English PC final orbit table4DB1F8: the fourth-index path is slower,
// and one answered word suffices to interrupt either solo or co-op approaches.
constexpr float TypingEmperorFinalPathStep(int place) { return place==4 ? .05f : .1f; }
constexpr unsigned TypingEmperorFinalHitQuota() { return 1; }
unsigned TypingEmperorChargeFrames(int rank);
constexpr unsigned TypingEmperorChargeHitQuota(unsigned players) { return players ? 1 : 0; }
constexpr unsigned TypingEmperorFirstPhasePattern(unsigned randomValue) {
    const unsigned index=(randomValue>>4)%10;
    return index<3 ? 1 : index<6 ? 2 : 3;
}
// The completed word sets the boss hit flag. Its following update applies two
// further 12-point (solo) or 7-point (co-op) hits, including for dialogue answers.
constexpr unsigned TypingEmperorWordDamage(bool cooperative = false) { return cooperative ? 14 : 24; }
constexpr unsigned TypingEmperorNetAnswerDamage(bool cooperative = false) {
    return TypingEmperorAnswerDamage(cooperative) + TypingEmperorWordDamage(cooperative);
}
constexpr unsigned TypingEmperorAnswerWeight(const TypingWordRecord& answer) { return answer.flags & 0xffu; }
constexpr unsigned TypingEmperorEndingBranch(uint64_t accumulatedAnswerWeight) {
    return accumulatedAnswerWeight > 4 ? 0 : accumulatedAnswerWeight <= 2 ? 1 : 2;
}

} // namespace game
