#include "game/typing_rules.h"
#include <algorithm>

namespace game {

char TypingKeyCharacter(uint16_t scan, bool shift, TypingKeyboard keyboard) {
    if (scan >= 4 && scan <= 29) return char('a'+scan-4);
    if (scan >= 30 && scan <= 39) {
        constexpr char plain[] = "1234567890";
        constexpr char us[] = "!@#$%^&*()";
        constexpr char uk[] = "!\"\0$%^&*()"; // The UK pound sign is outside the original ASCII word set.
        return !shift ? plain[scan-30] : keyboard == TypingKeyboard::UK ? uk[scan-30] : us[scan-30];
    }
    if (scan >= 89 && scan <= 97) return char('1'+scan-89);
    switch (scan) {
    case 44: return ' ';
    case 45: return shift ? '_' : '-';
    case 46: return shift ? '+' : '=';
    case 47: return shift ? '{' : '[';
    case 48: return shift ? '}' : ']';
    case 49: case 50:
        return keyboard == TypingKeyboard::UK ? (shift ? '~' : '#') : (shift ? '|' : '\\');
    case 51: return shift ? ':' : ';';
    case 52: return shift ? (keyboard == TypingKeyboard::UK ? '@' : '"') : '\'';
    case 53: return shift ? (keyboard == TypingKeyboard::UK ? 0 : '~') : '`';
    case 54: return shift ? '<' : ',';
    case 55: return shift ? '>' : '.';
    case 56: return shift ? '?' : '/';
    case 84: return '/';
    case 85: return '*';
    case 86: return '-';
    case 87: return '+';
    case 98: return '0';
    case 99: return '.';
    case 100: return shift ? '|' : '\\';
    default: return 0;
    }
}

std::vector<int> TypingLifeOptions(bool fiveLivesUnlocked) {
    return fiveLivesUnlocked ? std::vector<int>{1, 3, 5} : std::vector<int>{1, 3};
}

std::vector<int> TypingContinueOptions(bool nineContinuesUnlocked, bool freePlayUnlocked) {
    std::vector<int> result{3, 5};
    if (nineContinuesUnlocked) result.push_back(9);
    if (freePlayUnlocked) result.push_back(0);
    return result;
}

unsigned TypingDifficultyPercent(TypingDifficulty difficulty) {
    constexpr unsigned percentages[] = {60, 80, 100, 110, 120};
    const int index = std::clamp(static_cast<int>(difficulty), 0, 4);
    return percentages[index];
}

TypingRank TypingRankForWord(uint16_t measure, uint32_t selectedFrames) {
    if (measure == 0) return TypingRank::E;
    // The PC game selects x87 precision 24. Preserve its float rounding at
    // boundaries instead of replacing this ratio with integer cross-products.
    const float weightedMeasure = float(measure) * 0.01f;
    const float elapsed = float(std::min(selectedFrames, kTypingMaximumSelectedFrames)) / weightedMeasure;
    constexpr float limits[] = {200, 260, 320, 380};
    for (unsigned index = 0; index < 4; ++index)
        if (elapsed <= limits[index]) return static_cast<TypingRank>(index);
    return TypingRank::E;
}

unsigned TypingBasePoints(uint16_t measure) {
    constexpr unsigned limits[] = {20, 30, 50, 70, 100, 120, 150};
    constexpr unsigned points[] = {10, 12, 15, 17, 20, 25, 30, 35};
    for (unsigned index = 0; index < 7; ++index)
        if (measure <= limits[index]) return points[index];
    return points[7];
}

unsigned TypingRankBonus(TypingRank rank) {
    constexpr unsigned bonuses[] = {35, 20, 15, 10, 5};
    const auto index = static_cast<unsigned>(rank);
    return index < 5 ? bonuses[index] : 0;
}

unsigned TypingAccuracyPercent(uint64_t correctKeys, uint64_t attemptedKeys) {
    if (attemptedKeys == 0) return 0;
    correctKeys = std::min(correctKeys, attemptedKeys);
    // Compare against integer percentage boundaries without multiplying the
    // wider session counters by 100 or introducing floating-point rounding.
    unsigned lower = 0, upper = 100;
    while (lower < upper) {
        const unsigned middle = (lower + upper + 1) / 2;
        const uint64_t required = (attemptedKeys / 100) * middle +
                                  ((attemptedKeys % 100) * middle + 99) / 100;
        if (correctKeys >= required) lower = middle;
        else upper = middle - 1;
    }
    return lower;
}

unsigned TypingAccuracyBonus(unsigned accuracyPercent) {
    if (accuracyPercent <= 80) return 0;
    if (accuracyPercent <= 85) return 100;
    if (accuracyPercent <= 90) return 200;
    if (accuracyPercent <= 93) return 300;
    if (accuracyPercent <= 95) return 400;
    if (accuracyPercent <= 97) return 600;
    if (accuracyPercent <= 99) return 800;
    return 1200;
}

TypingWordAward TypingWordPoints(uint16_t measure, uint32_t selectedFrames, bool projectile) {
    if (projectile) return {TypingRank::Unranked, 10, 0};
    const auto rank = TypingRankForWord(measure, selectedFrames);
    return {rank, TypingBasePoints(measure), TypingRankBonus(rank)};
}

void TypingScoreBuckets::addWord(const TypingWordAward& award) {
    basePoints += award.basePoints;
    const auto rank = static_cast<unsigned>(award.rank);
    if (rank < rankCounts.size()) ++rankCounts[rank];
}

void TypingScoreBuckets::adjustBase(int64_t points) {
    if (points >= 0) basePoints += static_cast<uint64_t>(points);
    else {
        const uint64_t deduction = uint64_t(-(points + 1)) + 1;
        basePoints -= std::min(basePoints, deduction);
    }
}

TypingChapterScore TypingChapterPoints(const TypingScoreBuckets& buckets,
                                     TypingDifficulty difficulty,
                                     uint64_t correctKeys, uint64_t attemptedKeys,
                                     uint32_t rescuedCivilians) {
    TypingChapterScore result;
    result.basePoints = buckets.basePoints * TypingDifficultyPercent(difficulty) / 100;
    for (unsigned index = 0; index < buckets.rankCounts.size(); ++index)
        result.rankBonus += uint64_t(buckets.rankCounts[index]) * TypingRankBonus(static_cast<TypingRank>(index));
    result.accuracyBonus = TypingAccuracyBonus(TypingAccuracyPercent(correctKeys, attemptedKeys));
    result.civilianBonus = uint64_t(rescuedCivilians) * 150;
    return result;
}

TypingLifeReward TypingAddLife(int currentLives) {
    const int lives = std::clamp(currentLives, 0, kTypingMaximumLives);
    if (lives == kTypingMaximumLives) return {lives, 150};
    return {lives + 1, 0};
}

void TypingPerfectLifeMeter::reset() { perfectStreak = 0; gauge = 0; threshold = 400; }
void TypingPerfectLifeMeter::mistype() { perfectStreak = 0; }

bool TypingPerfectLifeMeter::completeWord(bool wordHadMistake, bool typingEnabled) {
    const unsigned previous = perfectStreak;
    if (wordHadMistake) perfectStreak = 0;
    else if (typingEnabled) perfectStreak = std::min(perfectStreak + 1, 999u);
    if (previous == perfectStreak) return false;
    gauge += perfectStreak;
    if (gauge <= threshold) return false;
    gauge = 0;
    threshold += 100;
    return true;
}

void TypingAdaptiveRank::reset(int chapter, bool startingAtLaterChapter) {
    chapter_ = std::clamp(chapter, 1, 6);
    rank_ = startingAtLaterChapter && chapter_ > 1 ? 7 : 1;
    pendingDamage_ = 0;
    previousPlayers_ = 0;
    gradeProgress_ = 0;
    activeTicks_ = 1;
    bossMode_ = false;
}

void TypingAdaptiveRank::resetBoss(unsigned stars, unsigned activePlayers) {
    reset();
    constexpr int starts[] = {-3, -1, 1, 4, 8};
    rank_ = starts[std::min(stars, 4u)] + 4 * int(std::min(activePlayers, 2u));
    bossMode_ = true;
}

void TypingAdaptiveRank::setChapter(int chapter) {
    chapter_ = std::clamp(chapter, 1, 6);
    gradeProgress_ = 0;
    bossMode_ = false;
}

void TypingAdaptiveRank::tick(bool active, unsigned playerMask,
                             const std::array<int, 2>& lives, int bossStars) {
    if (!active) return;
    const int previousRank = rank_;
    const unsigned players = unsigned((playerMask & 1) != 0) + unsigned((playerMask & 2) != 0);
    if (previousPlayers_ != 0) rank_ += 4 * (int(players) - int(previousPlayers_));
    if (activeTicks_ % 1800 == 0) {
        ++rank_;
        const bool healthy = players == 2 ? lives[0] + lives[1] >= 6 :
                             ((playerMask & 1) ? lives[0] >= 4 :
                              ((playerMask & 2) && lives[1] >= 4));
        if (healthy) ++rank_;
    }
    rank_ = std::clamp(rank_ + pendingDamage_, 0, 15);
    pendingDamage_ = 0;
    if (bossStars >= 0) {
        constexpr int lower[] = {0, 4, 7, 10, 12};
        constexpr int upper[] = {4, 7, 10, 13, 15};
        const int stars = std::min(bossStars, 4);
        rank_ = std::clamp(rank_, lower[stars], upper[stars]);
        bossMode_ = true;
    }
    if (rank_ < previousRank) activeTicks_ = 0;
    previousPlayers_ = players;
    ++activeTicks_;
}

void TypingAdaptiveRank::word(TypingRank grade) {
    constexpr unsigned increments[] = {40, 30, 20, 20, 20};
    const auto index = static_cast<unsigned>(grade);
    if (index >= 5) return;
    gradeProgress_ += increments[index];
    if (gradeProgress_ >= 100) {
        ++rank_;
        gradeProgress_ = 0;
    }
}

void TypingAdaptiveRank::damage() { pendingDamage_ -= 2; }
void TypingAdaptiveRank::continueGame() { rank_ = std::max(rank_ - 1, 0); }

int TypingAdaptiveRank::wordBankRank() {
    if (!bossMode_) {
        if (chapter_ == 1) rank_ = std::min(rank_, 12);
        else if (chapter_ == 2) rank_ = std::min(rank_, 14);
    }
    return std::clamp(rank_, 0, 15);
}

} // namespace game
