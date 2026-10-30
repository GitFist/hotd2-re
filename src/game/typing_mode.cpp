#include "game/typing_mode.h"
#include <algorithm>
#include <cmath>

namespace game {
namespace {
char Fold(char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; }
bool Typable(unsigned char c) { return c >= 33 && c <= 126; }
}

double TypingStats::accuracy() const {
    return TypingAccuracyPercent(correctCharacters, correctCharacters + errors);
}
double TypingStats::wpm() const {
    return activeSeconds > 0.0 ? double(correctCharacters) * 12.0 / activeSeconds : 0.0;
}

TypingModeController::TypingModeController(uint32_t seed) { reset(seed); }
void TypingModeController::reset(uint32_t seed) {
    entries_.clear();
    stats_ = {};
    beginChapter();
    perfect_.reset();
    perfectLifeGaugeEnabled_ = true;
    metrics_ = {};
    chapters_ = {};
    choice_ = {};
    chapterBest_ = chapterRescues_ = {};
    chapterMean_ = {};
    difficulty_ = TypingDifficulty::Normal;
    reactionStart_ = 0;
    reactionArmed_ = frameMistake_ = false;
    selectedSerial_ = 0;
    lastMistypeSerial_ = 0;
    random_ = seed;
    wordGroup_ = 0;
    wordSelector_ = 100;
    dynamicRank_ = 1;
    fixedWordLevel_ = false;
    actorWordOverrides_ = true;
    frameCounter_ = 0;
    chapter_ = 1;
    maximumNewWords_ = static_cast<size_t>(-1);
    assignedWords_ = 0;
    assignedBookWords_ = 0;
    bookDictionaryPending_ = true;
    selectedWordLevel_ = 1;
    minimumWord_ = 0;
    maximumWord_ = 200;
    characterPreferenceMode_ = 0;
    characterPreferenceKeys_.clear();
    drillSpecialKeyWeights_ = false;
    recentWords_.fill(-1);
    recentIndex_ = 0;
    original_.reset(profile_.originalProgress().unlocks());
    originalGenreBank_=nullptr;originalGenreIndex_=0;
}
void TypingModeController::setOriginalMode(bool enabled) {
    originalMode_=enabled;
    original_.reset(enabled?profile_.originalProgress().unlocks():0);
    originalGenreBank_=nullptr;originalGenreIndex_=0;
}
bool TypingModeController::collectOriginalItem(int item) {
    if (!originalMode_) return false;
    if (original_.collect(item)) return true;
    if (item==6) { originalGenreBank_=nullptr;originalGenreIndex_=0; }
    return original_.immediate(item);
}
uint32_t TypingModeController::nextRandom() {
    if (randomSource_) return randomSource_();
    random_ = random_ * 214013u + 2531011u;
    return (random_ >> 16) & 0x7fffu;
}
int TypingModeController::randomWordIndex(int count, int first) {
    int index = 0;
    for (int attempt = 0; attempt < 16; ++attempt) {
        index = first + int(nextRandom() % uint32_t(count));
        if (std::find(recentWords_.begin(), recentWords_.end(), index) == recentWords_.end()) break;
    }
    recentIndex_ = (recentIndex_ + 1) % recentWords_.size();
    recentWords_[recentIndex_] = index;
    return index;
}
void TypingModeController::setWordSelection(int group, int selector) {
    const int previousGroup=wordGroup_;
    const int previousLevel=fixedWordLevel_?selectedWordLevel_:wordSelector_>=0?
        TypingWordBankLevel(wordSelector_,dynamicRank_,minimumWord_,maximumWord_):1;
    const bool genre=originalMode_ && group==0 && !original_.alienDictionary() && original_.genreDictionary();
    if (genre && originalGenreBank_) {
        wordGroup_=7;wordSelector_=-2;fixedWordLevel_=true;
        selectedWordLevel_=originalGenreBank_->level;
        entries_.clear();selectedSerial_=0;
        return;
    }
    if (originalMode_ && group==0 && original_.alienDictionary()) { group=5;selector=-2; }
    else if (genre) { group=7;selector=-2; }
    const bool unadjusted = group == -1 || group == 0xffff;
    wordGroup_ = unadjusted ? 0 : group;
    wordSelector_ = selector == -1 ? 100 : selector;
    fixedWordLevel_ = false;
    if (selector != -2) actorWordOverrides_ = true;
    else if (wordGroup_ == 2 || wordGroup_ == 9 || wordGroup_ == 10) actorWordOverrides_ = false;
    if (selector == -2) {
        // A random bank is selected once for the whole assignment, rather than
        // separately for each live word. Alphabet-mode special-key banks use 1.
        fixedWordLevel_ = true;
        selectedWordLevel_ = 1;
        if (wordGroup_ == 7) {
            const auto levels = TypingBonusBankLevels(chapter_);
            if (!levels.empty()) selectedWordLevel_ = levels[nextRandom() % levels.size()];
        } else if (wordGroup_ != 9 && wordGroup_ != 10) {
            int count = 0;
            for (const auto& bank : TypingWordBanks()) if (bank.group == wordGroup_) ++count;
            // Source4B023A uses the same sixteen-index history as word picks.
            if (count > 0) selectedWordLevel_ += randomWordIndex(count);
        }
    } else if (unadjusted) {
        const float value = float(wordSelector_) * 0.1f;
        selectedWordLevel_ = value <= 3.0f ? 0 : std::clamp(int((value + 5.0f) * 0.1f), 0, 19);
        fixedWordLevel_ = true;
    }
    entries_.clear();
    selectedSerial_ = 0;
    if (genre) {
        if (originalGenreBank_) selectedWordLevel_=originalGenreBank_->level;
        else originalGenreBank_=FindTypingWordBank(7,selectedWordLevel_);
    }
    const int level=fixedWordLevel_?selectedWordLevel_:wordSelector_>=0?
        TypingWordBankLevel(wordSelector_,dynamicRank_,minimumWord_,maximumWord_):1;
    // Source4B2B80 reloads only when its file identity changes.4B2C2E
    // leaves that file pending until an ordinary file-bank lookup resolves it.
    if(wordGroup_!=previousGroup || level!=previousLevel)bookDictionaryPending_=true;
}
void TypingModeController::setWordLimits(int maximum, int minimum) {
    maximumWord_ = std::max(0, maximum / 10);
    minimumWord_ = std::clamp(minimum / 10, 0, maximumWord_);
}
void TypingModeController::setWordCharacterPreference(int mode, std::string_view keys) {
    if(mode!=0&&keys.size()>=48)return; // Source4B0630 leaves the previous setting intact.
    characterPreferenceMode_ = mode == 1 || mode == 2 ? mode : 0;
    characterPreferenceKeys_.assign(keys);
}
void TypingModeController::setPerfectLifeGauge(bool enabled, uint64_t threshold) {
    perfectLifeGaugeEnabled_ = enabled;
    perfect_.threshold = threshold;
    stats_.lifeThreshold = threshold;
}
void TypingModeController::continueGame() {
    if (originalMode_) original_.noteContinue();
    perfect_.reset();
    stats_.perfectStreak = 0;
    stats_.lifeGauge = 0;
    stats_.lifeThreshold = 400;
    addBasePoints(-150);
    cancelSelection();
}
void TypingModeController::addBasePoints(int64_t points) {
    const auto before = score_.basePoints;
    score_.adjustBase(points);
    if (score_.basePoints >= before) {
        stats_.points += score_.basePoints-before;
        chapterStats_.points += score_.basePoints-before;
    } else {
        stats_.points -= std::min(stats_.points, before-score_.basePoints);
        chapterStats_.points -= std::min(chapterStats_.points, before-score_.basePoints);
    }
}

void TypingModeController::beginChapter() {
    score_ = {};
    chapterStats_ = {};
    chapterScore_ = {};
    chapterFinished_ = false;
    original_.beginChapter();
}

const TypingChapterScore& TypingModeController::finishChapter(TypingDifficulty difficulty, uint32_t rescuedCivilians) {
    if (chapterFinished_) return chapterScore_;
    chapterScore_ = TypingChapterPoints(score_, difficulty, chapterStats_.correctCharacters,
                                      chapterStats_.correctCharacters + chapterStats_.errors, rescuedCivilians);
    const uint64_t provisional = chapterStats_.points;
    chapterStats_.points = chapterScore_.total();
    if (chapterStats_.points >= provisional) stats_.points += chapterStats_.points - provisional;
    else stats_.points -= std::min(stats_.points, provisional - chapterStats_.points);
    chapterFinished_ = true;
    difficulty_ = difficulty;
    if (originalMode_) {
        const uint8_t earned=TypingOriginalChapterCoins(chapter_,chapterScore_.total(),
            TypingAccuracyPercent(chapterStats_.correctCharacters,chapterStats_.correctCharacters+chapterStats_.errors),
            score_.rankCounts[0],original_.usedContinue());
        original_.setNewlyEarnedCoins(profile_.storeOriginalCoins(chapter_,earned));
    }
    if (chapter_ >= 1 && chapter_ <= 6) {
        chapterMean_[chapter_-1] = profile_.chapterMean(chapter_,chapterStats_.points);
        chapters_[chapter_-1] = profile_.chapterResult(chapter_,chapterStats_.points,true);
        chapterBest_[chapter_-1] = profile_.chapterBest(chapter_);
        chapterRescues_[chapter_-1] = rescuedCivilians;
        choice_.complete(chapter_);
        if (!profilePath_.empty()) profile_.save(profilePath_);
    }
    return chapterScore_;
}

void TypingModeController::setProfilePath(std::string path) {
    if (profilePath_ == path) return;
    profilePath_ = std::move(path);
    profile_ = TypingProfile{};
    if (!profilePath_.empty()) profile_.load(profilePath_);
}
void TypingModeController::noteDamage(bool smallThreat, bool otherTarget) {
    metrics_.smallThreatHits += smallThreat;
    metrics_.otherTargetHits += otherTarget && selectedSerial_ != 0;
}
int TypingModeController::storeFinal(const TypingFinalEvaluation& evaluation, std::string_view name) {
    const int rank=profile_.storeFinal(evaluation,name);
    if (rank >= 0 && !profilePath_.empty()) profile_.save(profilePath_);
    return rank;
}
uint64_t TypingModeController::currentChapterPoints(unsigned rescuedCivilians) const {
    if (chapterFinished_) return chapterScore_.total();
    unsigned previousRescues=0;
    for (unsigned rescued:chapterRescues_) previousRescues+=rescued;
    return TypingChapterPoints(score_,difficulty_,chapterStats_.correctCharacters,
        chapterStats_.correctCharacters+chapterStats_.errors,
        rescuedCivilians-std::min(rescuedCivilians,previousRescues)).total();
}
int TypingModeController::storeScore(const TypingScoreRecord& record, bool original) {
    const int rank=profile_.storeScore(record,original);
    if (rank>=0 && !profilePath_.empty()) profile_.save(profilePath_);
    return rank;
}
unsigned TypingModeController::chapterOrdinal() const {
    return chapter_ >= 1 && chapter_ <= 6 ? chapters_[chapter_-1].ordinal : 0;
}
unsigned TypingModeController::chapterBest() const {
    return chapter_ >= 1 && chapter_ <= 6 ? chapterBest_[chapter_-1] : 0;
}
float TypingModeController::chapterMean() const {
    return chapter_ >= 1 && chapter_ <= 6 ? chapterMean_[chapter_-1] : 0;
}
TypingFinalEvaluation TypingModeController::finalEvaluation(unsigned totalCivilians, unsigned rescuedCivilians, bool cleared) const {
    auto chapters = chapters_;
    if (!chapterFinished_ && chapter_ >= 1 && chapter_ <= 6) {
        auto& current=chapters[chapter_-1];
        current.status=2;
        current.points=static_cast<uint16_t>(std::min<uint64_t>(9999,currentChapterPoints(rescuedCivilians)));
    }
    auto evaluation=TypingEvaluateRun(metrics_,chapters,stats_.correctCharacters,
        stats_.correctCharacters+stats_.errors,totalCivilians,rescuedCivilians,cleared);
    evaluation.originalMode=originalMode_;
    return evaluation;
}

const TypingWordRecord& TypingModeController::assignWord(const TypingTarget& target,
                                                       const std::vector<Entry>& retained) {
    std::span<const TypingWordRecord> words;
    int first = 0, end = 0;
    const uint32_t actorFlags = actorWordOverrides_ ? target.flags : 0;
    const bool oneKey = actorWordOverrides_ && ((actorFlags & 0x10u) || (target.flags == 0 && target.projectile));
    if (!oneKey && originalMode_ && original_.genreDictionary() && originalGenreBank_ && wordGroup_==7) {
        const auto genre=TypingWords(*originalGenreBank_);
        if (originalGenreIndex_<genre.size()) {
            bookDictionaryPending_=false;
            return genre[originalGenreIndex_++];
        }
        original_.finishGenreDictionary();originalGenreBank_=nullptr;originalGenreIndex_=0;
        wordGroup_=0;wordSelector_=100;fixedWordLevel_=false;
    }
    if (oneKey)
        words = TypingWords(*FindTypingBuiltinBank("KEYS"));
    else if (actorFlags & 0x20u) words = TypingWords(*FindTypingBuiltinBank("SHORT"));
    else if (actorFlags & 0x1C0u) words = TypingWords(*FindTypingBuiltinBank("ABBREVIATIONS"));
    else {
        const int level = fixedWordLevel_ ? selectedWordLevel_ : wordSelector_ >= 0
            ? TypingWordBankLevel(wordSelector_, dynamicRank_, minimumWord_, maximumWord_)
            : 1;
        if (level == 0) words = TypingWords(*FindTypingBuiltinBank("SHORT"));
        else if (const auto* bank = FindTypingWordBank(wordGroup_, level)) {
            words = TypingWords(*bank);
            bookDictionaryPending_=false; // Source4B29EC/4B2C60 file-record fixup.
        } else {
            words = TypingWords(*FindTypingWordBank(0, 1));
            bookDictionaryPending_=false;
        }
    }
    // Source4B0B7F..BF1 applies authored index ranges after choosing the word
    // type, including when a fixed question bank suppresses actor type changes.
    end = static_cast<int>(words.size());
    if (target.flags & 0x40u) end = std::min(end, 29);
    else if (target.flags & 0x80u) { first = 28; end = std::min(end, 45); }
    else if (target.flags & 0x100u) first = 44;
    else if (target.flags & 0x01000000u) { first = 1; end = std::min(end, 9); }
    else if (target.flags & 0x00800000u) first = 9;
    first = std::min(first, end-1);
    const auto available = [&](const TypingWordRecord& candidate) {
        if (candidate.text.empty()) return false;
        return std::none_of(retained.begin(), retained.end(), [&](const Entry& e) {
            return !e.complete && !e.text.empty() && Fold(e.text.front()) == Fold(candidate.text.front());
        });
    };
    if (oneKey && drillSpecialKeyWeights_) {
        // Source4B0CCA..D4A: Drill scenes7/23 use the original 75-way
        // alphabet/number/punctuation mixture. The symbol ranges overlap.
        const unsigned pick=nextRandom()%75u;
        const TypingBuiltinBank* alternate=nullptr;
        size_t index=0;
        if(pick>=26&&pick<44) {alternate=FindTypingBuiltinBank("NUMBER_KEYS");index=(pick-26)/2;}
        else if(pick>=44&&pick<54) {
            index=(pick-44)/2;
            if(index==0)alternate=FindTypingBuiltinBank("ZERO_KEY");
            else {alternate=FindTypingBuiltinBank("SYMBOL_KEYS");--index;}
        } else if(pick>=54) {alternate=FindTypingBuiltinBank("SYMBOL_KEYS");index=(pick-54)/3+2;}
        if(alternate) {
            const auto alternatives=TypingWords(*alternate);
            if(index<alternatives.size()&&available(alternatives[index]))return alternatives[index];
        }
    } else if (oneKey && (nextRandom() & 1u) == 0) {
        const TypingBuiltinBank* alternate = nullptr;
        size_t index = 0;
        if (dynamicRank_ < 7) {
            alternate = FindTypingBuiltinBank("HOME_KEYS");
            index = frameCounter_ & 1u;
        } else if (dynamicRank_ > 13 && nextRandom() % 4u == 0) {
            alternate = FindTypingBuiltinBank("NUMBER_KEYS");
            index = nextRandom() % 9u;
        }
        if (alternate) {
            const auto alternatives = TypingWords(*alternate);
            if (index < alternatives.size() && available(alternatives[index])) return alternatives[index];
        }
    }
    // Source4B0DC5..E70: the supplementary weak-key list and Speed3's
    // punctuation list bias the ordinary bank picker. Each candidate still
    // uses the same sixteen-index history and first-letter conflict check.
    if (characterPreferenceMode_ != 0) {
        for (int attempt = 0; attempt < 2 * (end-first); ++attempt) {
            const auto& candidate = words[randomWordIndex(end-first, first)];
            const bool contains = candidate.text.find_first_of(characterPreferenceKeys_) != std::string_view::npos;
            if ((characterPreferenceMode_ == 1 ? contains : !contains) && available(candidate)) return candidate;
        }
    }
    const TypingWordRecord* candidate = &words.front();
    for (int attempt = 0; attempt < 16; ++attempt) {
        candidate = &words[randomWordIndex(end-first, first)];
        if (available(*candidate)) return *candidate;
    }
    // The original exhaustion scan includes the prefix below the random range.
    for (int index = end - 1; index >= 0; --index)
        if (available(words[index])) return words[index];
    return *candidate;
}

void TypingModeController::syncTargets(const std::vector<TypingTarget>& targets) {
    auto ordered = targets;
    for (auto& target : ordered)
        if (target.flags != 0) target.projectile = (target.flags & 0x10u) != 0;
    std::vector<Entry> next;
    next.reserve(ordered.size());
    for (const auto& target : ordered) {
        if (target.serial == 0 || std::any_of(next.begin(), next.end(), [&](const Entry& e) {
            return e.target.serial == target.serial;
        })) continue;
        const auto old = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) {
            return e.target.serial == target.serial && e.target.token == target.token;
        });
        if (old != entries_.end()) {
            next.push_back(*old);
            next.back().target = target;
        } else next.push_back({target});
    }
    size_t assigned = 0;
    for (auto& entry : next) if (entry.text.empty() && entry.target.enabled && assigned < maximumNewWords_) {
        const auto& word = entry.target.forcedWord ? *entry.target.forcedWord : assignWord(entry.target, next);
        entry.text = word.text;
        entry.measure = word.measure;
        entry.flags = word.flags;
        ++assigned;
        ++assignedWords_;
        if(!bookDictionaryPending_)++assignedBookWords_; // Source4B0B20..2F.
        if (entry.target.flags & 0x00400000u) ++metrics_.propWords;
        reactionStart_ = std::max(frameCounter_,1u);
        reactionArmed_ = true;
    }
    if (std::none_of(next.begin(), next.end(), [&](const Entry& e) {
        return e.target.serial == selectedSerial_ && !e.complete && e.progress > 0;
    })) selectedSerial_ = 0;
    entries_ = std::move(next);
}

void TypingModeController::update(double seconds, bool ready) {
    frameMistake_ = false; // Source4A97DB consumes the pending mistake indicator each frame.
    if (!ready || !std::isfinite(seconds) || seconds <= 0.0) return;
    if (originalMode_) original_.updateSeconds(seconds);
    if (std::any_of(entries_.begin(), entries_.end(), [](const Entry& e) { return !e.complete && e.target.enabled; })) {
        stats_.activeSeconds += seconds;
        chapterStats_.activeSeconds += seconds;
    }
    for (auto& entry : entries_) {
        if (entry.complete || !entry.target.enabled || entry.target.serial != selectedSerial_) continue;
        entry.frameRemainder += seconds * kTypingFramesPerSecond;
        const auto frames = uint32_t(std::min(entry.frameRemainder, double(kTypingMaximumSelectedFrames)));
        entry.frameRemainder -= frames;
        entry.selectedFrames = std::min(kTypingMaximumSelectedFrames, entry.selectedFrames + frames);
    }
}

std::vector<TypingCompletion> TypingModeController::typeCharacters(std::string_view characters, bool ready) {
    std::vector<TypingCompletion> completed;
    lastMistypeSerial_ = 0;
    if (!ready) return completed;
    for (const unsigned char character : characters) {
        if (character == 27) { cancelSelection(); continue; }
        if (!Typable(character)) continue;
        const char typed = Fold(char(character));
        const bool hand=originalMode_ && original_.anyKeyCorrect();
        // Source4B17EB leaves flag10000 words visible while their input window
        // is closed. Neither a new selection nor an existing one consumes keys.
        if (std::any_of(entries_.begin(), entries_.end(), [&](const Entry& entry) {
            return entry.target.serial == selectedSerial_ && !entry.complete &&
                   (!entry.target.enabled || (entry.target.flags & 0x10000u));
        })) continue;
        auto selected = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) {
            return e.target.enabled && !(e.target.flags & 0x10000u) && e.target.serial == selectedSerial_ && !e.complete;
        });
        if (selected == entries_.end()) {
            selected = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) {
                return e.target.enabled && !(e.target.flags & 0x10000u) && !e.complete && !e.text.empty() && (hand || Fold(e.text.front()) == typed);
            });
            if (selected == entries_.end()) continue;
            selectedSerial_ = selected->target.serial;
            // Source4B1CFA clears this attempt's mistake bits, then4B1EAD
            // counts the initial selection frame before evaluating its key.
            selected->hadMistake = false;
            selected->selectedFrames = 1;
            selected->frameRemainder = 0;
            if (reactionArmed_) {
                metrics_.reactionFrames += frameCounter_ >= reactionStart_ ? frameCounter_-reactionStart_ : 0;
                ++metrics_.reactions;
                reactionArmed_ = false;
            }
        }
        const bool mistake=!hand&&Fold(selected->text[selected->progress])!=typed;
        if(keyObserver_)keyObserver_(static_cast<unsigned char>(selected->text[selected->progress]),mistake);
        if (mistake) {
            lastMistypeSerial_ = selected->target.serial;
            ++stats_.errors;
            ++chapterStats_.errors;
            selected->hadMistake = true;
            frameMistake_ = true;
            perfect_.mistype();
            stats_.perfectStreak = perfect_.perfectStreak;
            if (selected->target.flags & 0x00200000u) {
                // Magician's accuracy challenge discards every active word and
                // selection. The next target snapshot assigns fresh words.
                for (auto& entry : entries_) if (!entry.complete) {
                    entry.text.clear();
                    entry.progress = 0;
                    entry.selectedFrames = 0;
                    entry.frameRemainder = 0;
                    entry.hadMistake = false;
                    entry.measure = entry.flags = 0;
                }
                selectedSerial_ = 0;
            }
            continue;
        }
        ++stats_.correctCharacters;
        ++chapterStats_.correctCharacters;
        ++selected->progress;
        while (selected->progress < selected->text.size() && selected->text[selected->progress] == ' ')
            ++selected->progress;
        if (selected->progress == selected->text.size()) {
            selected->complete = true;
            selectedSerial_ = 0;
            auto measuredAward = TypingWordPoints(selected->measure, selected->selectedFrames, selected->target.projectile);
            if (selected->text.size() < 2) {
                // Source40A9A2: a single-character dictionary entry has no
                // speed grade, even when it lacks the one-key actor flag.
                measuredAward.rank = TypingRank::Unranked;
                measuredAward.rankBonus = 0;
            }
            if (measuredAward.rank != TypingRank::Unranked) {
                metrics_.speedFrames += selected->selectedFrames;
                metrics_.speedCharacters += selected->text.size()-1;
            }
            const auto award = selected->target.flags & 0x08000000u
                ? TypingWordAward{TypingRank::Unranked, 0, 0}
                : measuredAward;
            score_.addWord(award);
            if (!(selected->target.flags & 0x08000000u)) {
                ++metrics_.words;
                const auto grade = static_cast<unsigned>(measuredAward.rank);
                if (grade < 5) {
                    constexpr unsigned rankWeights[] = {4,3,2,1,1};
                    const unsigned weight=TypingConcentrationWeight(selected->text);
                    metrics_.concentrationBase += weight*5;
                    metrics_.concentrationPoints += weight*(rankWeights[grade]+!frameMistake_);
                }
            }
            if (selected->target.flags & 0x00400000u) ++metrics_.completedProps;
            const auto points = award.basePoints + award.rankBonus;
            stats_.points += points;
            chapterStats_.points += points;
            ++stats_.completedWords;
            ++chapterStats_.completedWords;
            bool lifeReward = false;
            if (perfectLifeGaugeEnabled_) lifeReward = perfect_.completeWord(selected->hadMistake);
            else {
                // PC4B25F0 tracks PERFECT words in every course, while the
                // separately called4AC720 owns gauge accumulation and lives.
                perfect_.perfectStreak = selected->hadMistake ? 0 : std::min(perfect_.perfectStreak + 1, 999u);
            }
            stats_.perfectStreak = perfect_.perfectStreak;
            stats_.lifeGauge = perfect_.gauge;
            stats_.lifeThreshold = perfect_.threshold;
            completed.push_back({selected->target.serial, selected->target.token, points, award.rank, lifeReward, measuredAward.rank});
        }
    }
    return completed;
}

void TypingModeController::cancelSelection() {
    for (auto& e : entries_) if (e.target.serial == selectedSerial_ && !e.complete) {
        e.progress = 0;
        e.selectedFrames = 0;
        e.frameRemainder = 0;
    }
    selectedSerial_ = 0;
}
std::vector<TypingPrompt> TypingModeController::prompts() const {
    std::vector<TypingPrompt> result;
    result.reserve(entries_.size());
    for (const auto& e : entries_) if (e.target.enabled && !e.complete && !e.text.empty())
        result.push_back({e.target.serial, e.target.token, e.text, e.progress,
                          e.target.serial == selectedSerial_, e.target.boss, e.target.projectile,
                          e.measure, e.flags});
    return result;
}
} // namespace game
