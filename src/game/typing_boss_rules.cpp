#include "game/typing_boss_rules.h"
#include <algorithm>
#include <bit>

namespace game {

unsigned TypingEmperorChargeFrames(int rank) {
    constexpr unsigned frames[] = {370,360,350,340,330,290,280,270,260,250,240,230,220,210,200,190};
    return frames[std::clamp(rank,0,15)]; // Source4DB1B0.
}

int TypingJudgmentFirstPath(int tableIndex) {
    // Source58AC10: its near/far halves intentionally contain the same routes.
    constexpr int paths[] = {1,3,4,9,10,11,1,3,4,9,17,18,19,23,24,25,17,18,19,23,
                            1,3,4,9,10,11,1,3,4,9,17,18,19,23,24,25,17,18,19,23};
    return paths[std::clamp(tableIndex,0,39)];
}

int TypingJudgmentSecondPath(int tableIndex) {
    // Source58AD20: typing uses the two long approach paths.
    constexpr int paths[] = {0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,1,1,1,
        0,0,0,0,0,1,1,1,1,1, 0,0,0,1,1,1,1,1,1,1, 0,1,1,1,1,1,1,1,1,1};
    return paths[std::clamp(tableIndex,0,49)];
}

int TypingJudgmentSecondPathGroup(int aggression) {
    // A failed approach adds four before the next selection. The original
    // reads the four zero entries immediately beyond its sixteen-rank table.
    constexpr int groups[] = {0,0,0,0,1,1,1,2,2,2,3,3,3,4,4,4,0,0,0,0};
    return groups[std::clamp(aggression,0,19)];
}

float TypingJudgmentAdvancePathClock(int& wholeTicks, float& fraction, int rank) {
    // Source4B41EC / 5E6098: round each stored float and carry strictly above 1.
    constexpr uint32_t steps[] = {0x3eaaaaab,0x3ebda12f,0x3ec4ec4f,0x3ecccccd,
        0x3ed55555,0x3ede9bd4,0x3ee8ba2e,0x3ef3cf3e,0x3f000000,0x3f06bca2,
        0x3f0e38e4,0x3f169696,0x3f200000,0x3f2aaaab,0x3f36db6e,0x3f44ec4f};
    fraction += std::bit_cast<float>(steps[std::clamp(rank,0,15)]);
    if (fraction > 1.0f) { fraction -= 1.0f; ++wholeTicks; }
    return float(wholeTicks)+fraction;
}

TypingMagicianTiming TypingMagicianTimingForRank(int rank) {
    constexpr int repeats[] = {9,9,9,9,7,7,7,6,6,5,5,5,4,4,3,3};
    constexpr int flight[] = {400,390,390,380,380,370,370,360,360,350,340,330,320,310,300,280};
    const int row = std::clamp(rank,0,15);
    return {30,15,repeats[row],90,flight[row]};
}

float TypingMagicianPhaseThreshold(int maximumLife, int phase) {
    constexpr float multipliers[] = {8.5f,7.5f,6.0f,4.5f,0.0f,-1.0f};
    return float(maximumLife / 10) * multipliers[std::clamp(phase,0,5)];
}

unsigned TypingQuizTimeFrames(uint16_t correctAnswerMeasure, int adaptiveRank) {
    constexpr unsigned percentages[] = {170, 150, 140, 130, 120, 115, 110, 105,
                                         100, 95, 90, 88, 86, 84, 82, 60};
    const float factor = float(percentages[std::clamp(adaptiveRank, 0, 15)]) * 0.01f;
    const float weightedTime = float(correctAnswerMeasure) * 3.0f + 60.0f;
    return static_cast<unsigned>(weightedTime * factor);
}

unsigned TypingStrengthTimeFrames(uint16_t lineMeasure, int adaptiveRank) {
    constexpr uint32_t multipliers[] = {0x4089999a, 0x40866667, 0x40833334, 0x407fffff,
        0x40733333, 0x40666666, 0x40600000, 0x40599999, 0x404ccccd, 0x40400000,
        0x40333333, 0x40200000, 0x400ccccd, 0x3ff33332, 0x3fe66666, 0x3fe66666};
    return static_cast<unsigned>(float(lineMeasure) * std::bit_cast<float>(multipliers[std::clamp(adaptiveRank, 0, 15)]));
}

int TypingStrengthLifeAfterStory(int currentLife, int maximumLife) {
    const float damage = float(maximumLife) * 0.34f;
    return std::max(0, static_cast<int>(float(currentLife) - damage));
}

bool TypingStrengthSwingWindow(uint16_t relativeYaw, float distance) {
    return relativeYaw > 0x7800 && relativeYaw < 0x8800 && distance > 40.0f && distance < 70.0f;
}

float TypingStrengthTrailingCameraTick(float currentTick, bool approaching) {
    return std::max(0.0f, currentTick - (approaching ? 25.0f : 50.0f));
}

float TypingStrengthNextCameraTick(float currentTick, bool secondArena, bool holdForFlinch) {
    const float next = currentTick + (holdForFlinch ? 0.0f : secondArena ? 1.1f : 0.95f);
    return next > 1350.0f ? (secondArena ? 396.0f : 389.0f) : next;
}

void TypingStrengthReveal::tick() {
    if (finished_) return;
    if (wait_) {
        if (--wait_ == 0) nextBounce_ = 61;
    } else if (nextBounce_) --nextBounce_;
}

bool TypingStrengthReveal::completeLine() {
    if (!available()) return false;
    if (line_ == 3) { finished_ = true; return true; }
    ++line_;
    wait_ = nextBounce_ + 12 + 60;
    nextBounce_ = 0;
    return false;
}

bool TypingQuizChallenge::begin(std::span<const TypingWordRecord> records,
                                const std::array<uint32_t, 3>& answerRecordIndices) {
    *this = {};
    if (records.size() <= 9) return false;
    unsigned correctAnswers = 0;
    for (size_t index = 0; index < answerRecordIndices.size(); ++index) {
        const uint32_t record = answerRecordIndices[index];
        if (record == 0 || record >= records.size()) return false;
        if (record <= 8) { ++correctAnswers; correctChoice_ = index; }
        answers_[index] = records[record];
    }
    if (correctAnswers != 1) return false;
    question_ = records.front();
    valid_ = true;
    return true;
}

void TypingQuizChallenge::revealComplete(int adaptiveRank) {
    if (!valid_ || outcome_ != TypingQuizOutcome::None || accepting_) return;
    remainingFrames_ = TypingQuizTimeFrames(answers_[correctChoice_].measure, adaptiveRank);
    accepting_ = true;
}

TypingQuizOutcome TypingQuizChallenge::answer(size_t choice) {
    if (!accepting_ || choice >= answers_.size()) return TypingQuizOutcome::None;
    outcome_ = choice == correctChoice_ ? TypingQuizOutcome::Correct : TypingQuizOutcome::Wrong;
    accepting_ = false;
    return outcome_;
}

TypingQuizOutcome TypingQuizChallenge::tick(unsigned activeFrames) {
    if (!accepting_ || activeFrames == 0) return TypingQuizOutcome::None;
    if (activeFrames < remainingFrames_) remainingFrames_ -= activeFrames;
    else {
        remainingFrames_ = 0;
        accepting_ = false;
        outcome_ = TypingQuizOutcome::Timeout;
        return outcome_;
    }
    return TypingQuizOutcome::None;
}

} // namespace game
