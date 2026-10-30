#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game {

enum class TypingDrillKind { Speed, Reflex, Accuracy, SpecialKeys, FinalExam };

struct TypingDrillSpec {
    const char* name;
    TypingDrillKind kind;
    int level;
    double seconds;
    unsigned quota;
    double minimumAccuracy, minimumWpm, reactionLimit;
};

struct TypingDrillResult {
    unsigned words = 0, correct = 0, errors = 0, missed = 0;
    double seconds = 0, reactionSeconds = 0;
    int evaluation = 0;
    bool passed = false;
    int32_t metric = 0; // Original raw count, 60-Hz frames, or milliframes per letter.
    int grade = 4; // Original A..E = 0..4.
    bool original = false, failed = false;
    std::array<char,6> name{};
    // Immediate source-book statistics only; original ranking records persist
    // their metric/name and deliberately do not serialize this runtime payload.
    uint64_t assignedWords=0,completedWords=0;
    uint64_t reactionFrames=0,reactions=0,speedFrames=0,speedCharacters=0;
    double accuracy() const;
    double wpm() const;
    double reaction() const;
};

// The exercise argument uses the controller's category order throughout.
int TypingDrillOriginalGrade(int exercise,int32_t metric,bool failed=false);
int TypingDrillOriginalUnit(int exercise); // Zombies, Barrels, Times, Axes, Frogs, frames, milliframes/letter.
bool TypingDrillOriginalLowerIsBetter(int exercise);
struct TypingDrillWeakKey {
    unsigned char key = 0;
    unsigned weight = 0, percent = 0;
};

// The authored Drill scene reports through recordOriginalResult. The generic
// start/step/type harness is retained for old fixtures; its scores remain legacy.
class TypingDrillController {
public:
    static std::span<const TypingDrillSpec> exercises();
    bool unlocked(int exercise) const;
    bool start(int exercise, uint32_t seed = 0x4452494Cu, bool supplementary = false);
    bool beginOriginalAttempt(int exercise,bool supplementary=false);
    void recordOriginalResult(int exercise,TypingDrillResult result,bool supplementary=false);
    bool setOriginalRecordName(std::string_view name);
    void step(double seconds);
    void type(std::string_view characters);
    void cancelWord();
    void abandon();
    bool running() const { return running_; }
    bool finished() const { return finished_; }
    bool supplementary() const { return supplementary_; }
    int exercise() const { return exercise_; }
    const TypingDrillSpec& specification() const;
    std::string_view prompt() const { return prompt_; }
    size_t progress() const { return progress_; }
    double remaining() const;
    double reactionRemaining() const;
    const TypingDrillResult& result() const { return result_; }
    int recordRank() const { return recordRank_; } // Zero-based; -1 when the current attempt did not enter the top five.
    const std::array<bool,13>& clears() const { return clears_; }
    const std::vector<TypingDrillResult>& records(int exercise) const;
    const std::vector<int>& history(int exercise) const;
    const std::vector<TypingDrillResult>& legacyRecords(int exercise) const;
    const std::vector<int>& legacyHistory(int exercise) const;
    const std::array<bool,13>& legacyClears() const { return legacyClears_; }
    const std::array<int,13>& bestGrades() const { return bestGrades_; }
    const std::array<unsigned,13>& attempts() const { return attempts_; }
    std::string weakKeys() const;
    std::vector<TypingDrillWeakKey> weakKeyStats() const;
    void noteOriginalKey(unsigned char expected,bool error);
    const std::array<uint16_t,256>& originalKeyWeights() const { return weak_; }
    void setOriginalKeyWeights(const std::array<uint16_t,256>& weights) { weak_=weights;weak_[32]=0; }
    std::string_view supplementaryKeys() const { return supplementaryKeys_; }
    void setProfilePath(std::string path);
    bool save() const;

private:
    int exercise_ = 0;
    bool running_ = false, finished_ = false, supplementary_ = false;
    uint32_t random_ = 1;
    std::string prompt_, profilePath_;
    size_t progress_ = 0;
    double promptAge_ = 0;
    TypingDrillResult result_;
    int recordRank_ = -1;
    std::array<bool,13> clears_{};
    std::array<int,13> bestGrades_{{4,4,4,4,4,4,4,4,4,4,4,4,4}};
    std::array<unsigned,13> attempts_{};
    std::array<std::vector<TypingDrillResult>,13> records_;
    std::array<std::vector<int>,13> history_;
    std::array<bool,13> legacyClears_{};
    std::array<std::vector<TypingDrillResult>,13> legacyRecords_;
    std::array<std::vector<int>,13> legacyHistory_;
    std::array<uint16_t,256> weak_{};
    std::string supplementaryKeys_;
    uint32_t nextRandom();
    void nextPrompt();
    void finish();
    void load();
};
} // namespace game
