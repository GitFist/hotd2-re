#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <functional>
#include "game/typing_rules.h"
#include "game/typing_words.h"
#include "game/typing_profile.h"

namespace game {

// A serial identifies one live target. Change token whenever its damage window
// changes, so a word started against an earlier phase cannot hit the new phase.
// Serial zero is reserved for "no selection". Chapter is the native range 1..6.
struct TypingTarget {
    uint32_t serial = 0, token = 0;
    int chapter = 1;
    bool boss = false;
    float urgency = 0.0f, distance = 0.0f;
    bool projectile = false; // Original flag0x10 selects one-key, unranked prompts.
    uint32_t flags = 0; // Original actor typing flags, distinct from dictionary flags.
    const TypingWordRecord* forcedWord = nullptr; // Boss answer/story records selected by their challenge.
    bool enabled = true; // Existing words retain progress while their actor cannot currently receive input.
};

struct TypingPrompt {
    uint32_t serial = 0, token = 0;
    std::string text;
    size_t progress = 0; // ASCII byte offset, including automatically skipped spaces.
    bool selected = false, boss = false;
    bool projectile = false;
    uint16_t measure = 0, flags = 0;
};

struct TypingCompletion {
    uint32_t serial = 0, token = 0;
    uint64_t points = 0;
    TypingRank rank = TypingRank::Unranked;
    bool lifeReward = false;
    TypingRank visualRank = TypingRank::Unranked;
};

struct TypingStats {
    uint64_t correctCharacters = 0, errors = 0, completedWords = 0, points = 0;
    double activeSeconds = 0.0;
    unsigned perfectStreak = 0;
    uint64_t lifeGauge = 0, lifeThreshold = 400;
    double accuracy() const; // Integer percentage, 0..100; no attempts returns zero.
    double wpm() const; // Five successfully typed characters make one standard word.
};

// Pure port-native typing rules. No world RNG, input API, game data, or clock is
// used here; the caller supplies target snapshots, ordered text, and active time.
class TypingModeController {
public:
    static constexpr uint32_t kDefaultSeed = 0x4E535459u;
    explicit TypingModeController(uint32_t seed = kDefaultSeed);
    void reset(uint32_t seed = kDefaultSeed);
    void syncTargets(const std::vector<TypingTarget>& targets);
    void update(double seconds, bool ready);
    std::vector<TypingCompletion> typeCharacters(std::string_view characters, bool ready = true);
    void setKeyObserver(std::function<void(unsigned char,bool)> observer) {keyObserver_=std::move(observer);}
    void cancelSelection();
    void setWordSelection(int group, int selector);
    void setWordLimits(int maximum, int minimum);
    // Source4B0610: mode1 prefers words containing an exact character from
    // keys; mode2 prefers words without any. Exhaustion falls back normally.
    void setWordCharacterPreference(int mode, std::string_view keys);
    void setDrillSpecialKeyWeights(bool enabled) { drillSpecialKeyWeights_ = enabled; }
    // PC4AC670 changes the training threshold; PC4AD743 advances its life
    // gauge only in Accuracy2. The perfect-word streak remains independent.
    void setPerfectLifeGauge(bool enabled, uint64_t threshold = 400);
    void setDynamicRank(int rank) { dynamicRank_ = rank; }
    void setChapter(int chapter) { chapter_ = chapter; }
    void setDifficulty(TypingDifficulty difficulty) { difficulty_ = difficulty; }
    void setProfilePath(std::string path);
    void noteDamage(bool smallThreat, bool otherTarget);
    TypingFinalEvaluation finalEvaluation(unsigned totalCivilians, unsigned rescuedCivilians, bool cleared) const;
    const auto& chapterRecords() const { return chapters_; }
    const TypingChapterChoice& chapterChoice() const { return choice_; }
    unsigned chapterOrdinal() const;
    unsigned chapterBest() const;
    float chapterMean() const;
    const TypingRunMetrics& runMetrics() const { return metrics_; }
    int finalRank(const TypingFinalEvaluation& evaluation) const { return profile_.finalRank(evaluation); }
    int storeFinal(const TypingFinalEvaluation& evaluation, std::string_view name);
    const auto& finalRecords() const { return profile_.finalRecords(); }
    uint64_t currentChapterPoints(unsigned rescuedCivilians) const;
    uint64_t runPoints(unsigned rescuedCivilians) const {
        return stats_.points-std::min(stats_.points,chapterStats_.points)+currentChapterPoints(rescuedCivilians);
    }
    void setOriginalMode(bool enabled);
    bool originalMode() const { return originalMode_; }
    TypingOriginalState& original() { return original_; }
    const TypingOriginalState& original() const { return original_; }
    const TypingOriginalProgress& originalProgress() const { return profile_.originalProgress(); }
    bool collectOriginalItem(int item);
    void originalSceneClear() { original_.sceneClear(); }
    int scoreRank(const TypingScoreRecord& record, bool original = false) const { return profile_.scoreRank(record,original); }
    int storeScore(const TypingScoreRecord& record, bool original = false);
    const auto& scoreRecords(bool original = false) const { return profile_.scoreRecords(original); }
    void setFrameCounter(uint32_t frame) { frameCounter_ = frame; }
    void setMaximumNewWordsPerSync(size_t count) { maximumNewWords_ = count; }
    uint64_t assignedWordCount() const { return assignedWords_; }
    // PC4B0B29 excludes assignments while the selected file dictionary is
    // unresolved. Builtin one-key/short banks do not resolve that file.
    uint64_t assignedBookWordCount() const { return assignedBookWords_; }
    bool actorWordOverridesEnabled() const { return actorWordOverrides_; }
    void continueGame();
    void addBasePoints(int64_t points);
    void beginChapter();
    const TypingChapterScore& finishChapter(TypingDifficulty difficulty, uint32_t rescuedCivilians);
    void setRandomSource(std::function<uint32_t()> source) { randomSource_ = std::move(source); }
    const TypingScoreBuckets& scoreBuckets() const { return score_; }
    std::vector<TypingPrompt> prompts() const;
    const TypingStats& stats() const { return stats_; }
    const TypingStats& chapterStats() const { return chapterStats_; }
    const TypingChapterScore& chapterScore() const { return chapterScore_; }
    bool chapterFinished() const { return chapterFinished_; }
    uint32_t selectedSerial() const { return selectedSerial_; }
    uint32_t lastMistypeSerial() const { return lastMistypeSerial_; }

private:
    struct Entry {
        TypingTarget target;
        std::string text;
        size_t progress = 0;
        bool complete = false;
        uint16_t measure = 0, flags = 0;
        uint32_t selectedFrames = 0;
        double frameRemainder = 0;
        bool hadMistake = false;
    };
    std::vector<Entry> entries_;
    TypingStats stats_;
    TypingStats chapterStats_;
    TypingChapterScore chapterScore_;
    bool chapterFinished_ = false;
    uint32_t random_ = kDefaultSeed;
    uint32_t selectedSerial_ = 0;
    uint32_t lastMistypeSerial_ = 0;
    int wordGroup_ = 0, wordSelector_ = 100, dynamicRank_ = 1;
    bool fixedWordLevel_ = false;
    bool actorWordOverrides_ = true;
    uint32_t frameCounter_ = 0;
    int chapter_ = 1;
    size_t maximumNewWords_ = static_cast<size_t>(-1);
    uint64_t assignedWords_ = 0;
    uint64_t assignedBookWords_ = 0;
    bool bookDictionaryPending_ = true;
    int selectedWordLevel_ = 1;
    int minimumWord_ = 0, maximumWord_ = 200;
    int characterPreferenceMode_ = 0;
    std::string characterPreferenceKeys_;
    bool drillSpecialKeyWeights_ = false;
    std::array<int, 16> recentWords_{};
    size_t recentIndex_ = 0;
    std::function<uint32_t()> randomSource_;
    std::function<void(unsigned char,bool)> keyObserver_;
    TypingScoreBuckets score_;
    TypingPerfectLifeMeter perfect_;
    bool perfectLifeGaugeEnabled_ = true;
    TypingRunMetrics metrics_;
    std::array<TypingChapterRecord,6> chapters_{};
    TypingChapterChoice choice_;
    std::array<unsigned,6> chapterBest_{}, chapterRescues_{};
    std::array<float,6> chapterMean_{};
    TypingProfile profile_;
    std::string profilePath_;
    TypingDifficulty difficulty_ = TypingDifficulty::Normal;
    bool originalMode_ = false;
    TypingOriginalState original_;
    const TypingWordBank* originalGenreBank_ = nullptr;
    size_t originalGenreIndex_ = 0;
    uint32_t reactionStart_ = 0;
    bool reactionArmed_ = false, frameMistake_ = false;
    uint32_t nextRandom();
    const TypingWordRecord& assignWord(const TypingTarget& target, const std::vector<Entry>& retained);
    int randomWordIndex(int count, int first = 0);
};

} // namespace game
