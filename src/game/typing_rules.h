#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace game {

// Contracts verified against the supplied English PC reference executable.
// Word measures are the low word of the dictionary record, not character counts.
enum class TypingDifficulty { VeryEasy, Easy, Normal, Hard, VeryHard };
enum class TypingWordSize { Normal, Large };
enum class TypingKeyboard { US, UK };
enum class TypingRank { A, B, C, D, E, Unranked };
// USB keyboard usage codes (also SDL scancodes), independent of the OS layout.
char TypingKeyCharacter(uint16_t scanCode, bool shift, TypingKeyboard keyboard);

struct TypingRulesOptions {
    TypingDifficulty difficulty = TypingDifficulty::Normal;
    int startingLives = 3;
    int continues = 5; // Zero denotes unlocked Free Play.
    TypingWordSize wordSize = TypingWordSize::Normal;
    TypingKeyboard keyboard = TypingKeyboard::US;
    bool bloodEffects = true;
};

std::vector<int> TypingLifeOptions(bool fiveLivesUnlocked = false);
std::vector<int> TypingContinueOptions(bool nineContinuesUnlocked = false,
                                      bool freePlayUnlocked = false);
unsigned TypingDifficultyPercent(TypingDifficulty difficulty);
constexpr unsigned kTypingFramesPerSecond = 60;
constexpr unsigned kTypingMaximumSelectedFrames = 1800;
constexpr int kTypingMaximumLives = 5;

TypingRank TypingRankForWord(uint16_t measure, uint32_t selectedFrames);
unsigned TypingBasePoints(uint16_t measure);
unsigned TypingRankBonus(TypingRank rank);
unsigned TypingAccuracyPercent(uint64_t correctKeys, uint64_t attemptedKeys);
unsigned TypingAccuracyBonus(unsigned accuracyPercent);

struct TypingWordAward {
    TypingRank rank = TypingRank::Unranked;
    unsigned basePoints = 0;
    unsigned rankBonus = 0;
};

// Projectiles are unranked and award ten base points. Difficulty scaling is
// applied once to the accumulated base bucket, never separately to each word.
TypingWordAward TypingWordPoints(uint16_t measure, uint32_t selectedFrames,
                                bool projectile = false);

struct TypingScoreBuckets {
    uint64_t basePoints = 0;
    std::array<uint32_t, 5> rankCounts{};
    void addWord(const TypingWordAward& award);
    void adjustBase(int64_t points); // Continues (-150), items, bosses, excess lives.
};

struct TypingChapterScore {
    uint64_t basePoints = 0;
    uint64_t rankBonus = 0;
    uint64_t accuracyBonus = 0;
    uint64_t civilianBonus = 0;
    uint64_t total() const { return basePoints + rankBonus + accuracyBonus + civilianBonus; }
};

TypingChapterScore TypingChapterPoints(const TypingScoreBuckets& buckets,
                                     TypingDifficulty difficulty,
                                     uint64_t correctKeys, uint64_t attemptedKeys,
                                     uint32_t rescuedCivilians);

struct TypingLifeReward {
    int lives = 0;
    unsigned basePoints = 0;
};
TypingLifeReward TypingAddLife(int currentLives);

// The original gauge adds the new perfect streak when that streak changes.
// It awards on strictly exceeding the threshold and discards overflow. Earning
// a life preserves the streak, while continuing resets this whole state.
struct TypingPerfectLifeMeter {
    unsigned perfectStreak = 0;
    uint64_t gauge = 0;
    uint64_t threshold = 400;
    void reset();
    void mistype();
    bool completeWord(bool wordHadMistake = false, bool typingEnabled = true);
};

// Internal difficulty rank is separate from the A..E grade for one word and
// from the five user-selected difficulties. Chapters use the native range 1..6.
class TypingAdaptiveRank {
public:
    void reset(int chapter = 1, bool startingAtLaterChapter = false);
    void setChapter(int chapter); // Preserve rank when advancing an existing run.
    void resetBoss(unsigned stars, unsigned activePlayers = 1);
    // playerMask: bit 0 is player one, bit 1 is player two. Negative bossStars
    // selects story rules; Boss Mode stars use the range 0..4.
    void tick(bool active, unsigned playerMask, const std::array<int, 2>& lives,
              int bossStars = -1);
    void word(TypingRank grade);
    void damage();
    void continueGame();
    int rank() const { return rank_; }
    int wordBankRank(); // Applies the reference's chapter-1/2 caps in place.
    unsigned gradeProgress() const { return gradeProgress_; }
    uint64_t activeTicks() const { return activeTicks_; }

private:
    int rank_ = 1;
    int chapter_ = 1;
    int pendingDamage_ = 0;
    unsigned previousPlayers_ = 0;
    unsigned gradeProgress_ = 0;
    uint64_t activeTicks_ = 1;
    bool bossMode_ = false;
};

} // namespace game
