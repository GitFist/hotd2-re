#include "game/typing_drill.h"
#include "game/typing_words.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace game {
namespace {
constexpr TypingDrillSpec kExercises[] = {
    {"TYPING SPEED 1", TypingDrillKind::Speed, 1, 60, 0, 90, 20, 0},
    {"TYPING SPEED 2", TypingDrillKind::Speed, 2, 60, 0, 90, 30, 0},
    {"TYPING SPEED 3", TypingDrillKind::Speed, 3, 60, 0, 90, 40, 0},
    {"REFLEX 1", TypingDrillKind::Reflex, 1, 120, 30, 90, 0, 2.5},
    {"REFLEX 2", TypingDrillKind::Reflex, 2, 120, 30, 90, 0, 2.0},
    {"REFLEX 3", TypingDrillKind::Reflex, 3, 120, 30, 90, 0, 1.5},
    {"ACCURACY 1", TypingDrillKind::Accuracy, 1, 180, 10, 95, 0, 0},
    {"ACCURACY 2", TypingDrillKind::Accuracy, 2, 180, 15, 98, 0, 0},
    {"ACCURACY 3", TypingDrillKind::Accuracy, 3, 180, 20, 100, 0, 0},
    {"SPECIAL KEYS 1", TypingDrillKind::SpecialKeys, 1, 120, 20, 90, 0, 0},
    {"SPECIAL KEYS 2", TypingDrillKind::SpecialKeys, 2, 120, 20, 95, 0, 0},
    {"SPECIAL KEYS 3", TypingDrillKind::SpecialKeys, 3, 120, 20, 98, 0, 0},
    {"FINAL EXAM", TypingDrillKind::FinalExam, 3, 120, 0, 98, 40, 0}
};
char Fold(unsigned char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a'-'A')) : char(c); }
bool LegacyBetter(const TypingDrillResult& a, const TypingDrillResult& b) {
    if (a.evaluation != b.evaluation) return a.evaluation > b.evaluation;
    if (a.accuracy() != b.accuracy()) return a.accuracy() > b.accuracy();
    return a.wpm() > b.wpm();
}
bool ValidLegacy(const TypingDrillResult& r) {
    return r.words <= 100000 && r.correct <= 1000000 && r.errors <= 1000000 && r.missed <= 100000 &&
        std::isfinite(r.seconds) && r.seconds >= 0 && r.seconds <= 180 &&
        std::isfinite(r.reactionSeconds) && r.reactionSeconds >= 0 && r.reactionSeconds <= 180 &&
        r.evaluation >= 0 && r.evaluation <= 100;
}
bool NameCharacter(unsigned char c) {
    return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
        std::string_view("\\.,?!_[]-% '$#@\"&/():*").find(char(c))!=std::string_view::npos;
}
struct OriginalRule {
    int unit;
    bool lower;
    std::array<int32_t,4> threshold;
};
// Source grade branches 0x4A6D20, in native controller order. The source
// display table 0x589298 uses seconds; grading compares exact 60-Hz ticks.
// Speed3 and the Final have no successful E branch in the executable.
constexpr int32_t kAny=std::numeric_limits<int32_t>::max();
constexpr std::array<OriginalRule,13> kOriginalRules = {{
    {5,true, {3600,5400,7800,12000}},
    {2,false,{35,15,6,3}},
    {6,true, {12000,18000,24000,kAny}},
    {1,false,{100,60,40,20}},
    {0,false,{45,30,20,10}},
    {5,false,{7200,3600,2700,1800}},
    {0,false,{40,25,15,10}},
    {5,true, {2700,5400,7200,10800}},
    {2,false,{45,30,15,10}},
    {3,false,{128,64,32,17}},
    {4,false,{70,50,30,18}},
    {0,false,{36,24,16,8}},
    {5,true, {16800,19200,21600,kAny}},
}};
bool ValidOriginal(int exercise,const TypingDrillResult& r) {
    const auto end=std::find(r.name.begin(),r.name.end(),'\0');
    return r.name[5]=='\0' && std::all_of(r.name.begin(),end,NameCharacter) &&
        std::all_of(end,r.name.end(),[](char c) { return c==0; }) &&
        r.original && r.metric>=0 && r.grade==TypingDrillOriginalGrade(exercise,r.metric,r.failed) &&
        r.words<=1000000 && r.correct<=10000000 && r.errors<=10000000 && r.missed<=1000000 &&
        std::isfinite(r.seconds) && r.seconds>=0 && r.seconds<=3600 &&
        std::isfinite(r.reactionSeconds) && r.reactionSeconds>=0 && r.reactionSeconds<=3600 &&
        (!r.failed || exercise!=2 || r.metric==0);
}
void WriteLegacyRow(std::ostream& out,const TypingDrillResult& r) {
    out<<r.words<<' '<<r.correct<<' '<<r.errors<<' '<<r.missed<<' '<<r.seconds<<' '
       <<r.reactionSeconds<<' '<<r.evaluation<<' '<<r.passed;
}
bool ReadLegacyRow(std::istream& in,TypingDrillResult& r) {
    return bool(in>>r.words>>r.correct>>r.errors>>r.missed>>r.seconds>>r.reactionSeconds>>r.evaluation>>r.passed);
}
}

int TypingDrillOriginalGrade(int exercise,int32_t metric,bool failed) {
    if (exercise<0 || exercise>=int(kOriginalRules.size()) || metric<0) return 4;
    if (failed && (exercise==2 || exercise==12)) return 4;
    const auto& rule=kOriginalRules[exercise];
    for (int grade=0;grade<4;++grade)
        if (rule.lower?metric<=rule.threshold[grade]:metric>=rule.threshold[grade]) return grade;
    return 4;
}
int TypingDrillOriginalUnit(int exercise) {
    return exercise>=0 && exercise<int(kOriginalRules.size())?kOriginalRules[exercise].unit:0;
}
bool TypingDrillOriginalLowerIsBetter(int exercise) {
    return exercise>=0 && exercise<int(kOriginalRules.size()) && kOriginalRules[exercise].lower;
}
double TypingDrillResult::accuracy() const { return correct + errors ? 100.0 * correct / (correct + errors) : 0; }
double TypingDrillResult::wpm() const { return seconds > 0 ? correct * 12.0 / seconds : 0; }
double TypingDrillResult::reaction() const { return words ? reactionSeconds / words : 0; }
std::span<const TypingDrillSpec> TypingDrillController::exercises() { return kExercises; }
const TypingDrillSpec& TypingDrillController::specification() const { return kExercises[exercise_]; }
bool TypingDrillController::unlocked(int exercise) const {
    if (exercise < 0 || exercise >= 13) return false;
    if (exercise == 12) return clears_[2] && clears_[5] && clears_[8] && clears_[11];
    return exercise % 3 == 0 || clears_[exercise - 1];
}
uint32_t TypingDrillController::nextRandom() { random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5; return random_; }
bool TypingDrillController::start(int exercise, uint32_t seed, bool supplementary) {
    if (!unlocked(exercise)) return false;
    supplementaryKeys_ = supplementary ? weakKeys() : std::string{};
    supplementary_ = supplementary;
    exercise_ = exercise;
    random_ = seed ? seed : 1;
    running_ = true; finished_ = false;
    result_ = {};
    recordRank_=-1;
    weak_.fill(0);
    nextPrompt();
    return true;
}
bool TypingDrillController::beginOriginalAttempt(int exercise,bool supplementary) {
    if (!unlocked(exercise)) return false;
    supplementaryKeys_=supplementary?weakKeys():std::string{};
    supplementary_=supplementary;
    exercise_=exercise;
    running_=true; finished_=false;
    result_={}; result_.original=true;
    recordRank_=-1;
    prompt_.clear(); progress_=0; promptAge_=0;
    weak_.fill(0);
    return true;
}
void TypingDrillController::recordOriginalResult(int exercise,TypingDrillResult result,bool supplementary) {
    if (exercise<0 || exercise>=13 || result.metric<0) return;
    result.original=true;
    if (result.failed && exercise==2) result.metric=0;
    result.grade=TypingDrillOriginalGrade(exercise,result.metric,result.failed);
    result.passed=result.grade<=2;
    result.evaluation=0; // The old native score is never an original metric.
    if (!ValidOriginal(exercise,result)) return;
    exercise_=exercise; supplementary_=supplementary;
    result_=result; running_=false; finished_=true;
    recordRank_=-1;
    prompt_.clear(); progress_=0;
    if (supplementary) return; // Original supplementary scenes do not grade or persist a main attempt.
    bestGrades_[exercise]=std::min(bestGrades_[exercise],result.grade);
    clears_[exercise]=bestGrades_[exercise]<=2;
    auto& table=records_[exercise];
    // Source0x4A7610: zero is the empty-record sentinel. Failed Final attempts
    // are ineligible; equal raw values follow existing records without a tie-break.
    if (result.metric>0 && !(exercise==12 && result.failed)) {
        const bool lower=TypingDrillOriginalLowerIsBetter(exercise);
        const auto at=std::find_if(table.begin(),table.end(),[&](const auto& row) {
            return lower?result.metric<row.metric:result.metric>row.metric;
        });
        const int place=int(at-table.begin());
        if (place<5) recordRank_=place;
        table.insert(at,result);
        if (table.size()>5) table.resize(5);
    }
    auto& history=history_[exercise];
    history.insert(history.begin(),result.grade);
    if (history.size()>30) history.resize(30);
    attempts_[exercise]=std::min(99u,attempts_[exercise]+1);
    save();
}
bool TypingDrillController::setOriginalRecordName(std::string_view name) {
    if (!result_.original || supplementary_ || recordRank_<0 || name.size()>5 ||
        !std::all_of(name.begin(),name.end(),NameCharacter)) return false;
    auto& table=records_[exercise_];
    if (recordRank_>=int(table.size()) || table[recordRank_].metric!=result_.metric) return false;
    const auto previous=table[recordRank_].name;
    table[recordRank_].name={};
    std::copy(name.begin(),name.end(),table[recordRank_].name.begin());
    result_.name=table[recordRank_].name;
    if (save()) return true;
    table[recordRank_].name=previous;
    result_.name=previous;
    return false;
}
void TypingDrillController::nextPrompt() {
    const auto& spec = specification();
    std::vector<std::string_view> candidates;
    auto add = [&](std::span<const TypingWordRecord> words) {
        for (const auto& word : words) {
            if (word.text.empty()) continue;
            if (!supplementaryKeys_.empty() && std::none_of(word.text.begin(),word.text.end(),[&](unsigned char c) {
                return supplementaryKeys_.find(Fold(c)) != std::string::npos;
            })) continue;
            candidates.push_back(word.text);
        }
    };
    if (spec.kind == TypingDrillKind::Reflex) {
        if (const auto* bank = FindTypingBuiltinBank("KEYS")) add(TypingWords(*bank));
    } else if (spec.kind == TypingDrillKind::SpecialKeys) {
        if (const auto* bank = FindTypingBuiltinBank("SYMBOL_KEYS")) add(TypingWords(*bank));
        if (spec.level >= 2) if (const auto* bank = FindTypingBuiltinBank("NUMBER_KEYS")) add(TypingWords(*bank));
    } else {
        const int level = spec.kind == TypingDrillKind::FinalExam ? 8 : 1 + (spec.level - 1) * 3;
        if (const auto* bank = FindTypingWordBank(0,level)) add(TypingWords(*bank));
    }
    // A weak punctuation key may be absent from a particular word-level bank.
    // Keep supplementary practice focused on that exact key in that case.
    if (candidates.empty()) {
        prompt_ = supplementaryKeys_.empty() ? "f" : supplementaryKeys_.substr(nextRandom()%supplementaryKeys_.size(),1);
    } else {
        prompt_ = candidates[nextRandom()%candidates.size()];
        if (spec.kind == TypingDrillKind::SpecialKeys && spec.level >= 2) {
            const int count = spec.level == 2 ? 3 : 5;
            for (int i=1;i<count;++i) prompt_ += candidates[nextRandom()%candidates.size()];
        }
    }
    progress_ = 0;
    promptAge_ = 0;
}
void TypingDrillController::step(double seconds) {
    if (!running_ || !std::isfinite(seconds) || seconds <= 0) return;
    const auto& spec = specification();
    const double used = std::min(seconds,std::max(0.0,spec.seconds-result_.seconds));
    result_.seconds += used;
    promptAge_ += used;
    if (spec.reactionLimit > 0 && promptAge_ > spec.reactionLimit) {
        ++result_.missed;
        if (!prompt_.empty()) ++weak_[static_cast<unsigned char>(Fold(prompt_[progress_]))];
        nextPrompt();
    }
    if (result_.seconds >= spec.seconds) finish();
}
void TypingDrillController::type(std::string_view characters) {
    if (!running_) return;
    for (unsigned char character : characters) {
        if (!running_) break;
        if (character < 33 || character > 126) continue;
        while (progress_ < prompt_.size() && prompt_[progress_] == ' ') ++progress_;
        if (progress_ >= prompt_.size()) continue;
        const unsigned char expected = static_cast<unsigned char>(Fold(prompt_[progress_]));
        if (Fold(character) != char(expected)) {
            ++result_.errors;
            ++weak_[expected];
            continue;
        }
        ++result_.correct;
        ++progress_;
        while (progress_ < prompt_.size() && prompt_[progress_] == ' ') ++progress_;
        if (progress_ == prompt_.size()) {
            ++result_.words;
            result_.reactionSeconds += promptAge_;
            if (specification().quota && result_.words >= specification().quota) finish();
            else nextPrompt();
        }
    }
}
void TypingDrillController::cancelWord() { if (running_) progress_ = 0; }
void TypingDrillController::abandon() { running_ = finished_ = false; prompt_.clear(); }
double TypingDrillController::remaining() const { return std::max(0.0,specification().seconds-result_.seconds); }
double TypingDrillController::reactionRemaining() const { return std::max(0.0,specification().reactionLimit-promptAge_); }
void TypingDrillController::finish() {
    if (!running_) return;
    running_ = false; finished_ = true;
    const auto& spec = specification();
    const bool quota = !spec.quota || result_.words >= spec.quota;
    const bool accurate = result_.accuracy() + 1e-9 >= spec.minimumAccuracy;
    const bool fast = result_.wpm() + 1e-9 >= spec.minimumWpm;
    const bool responsive = spec.reactionLimit == 0 || result_.missed <= 3;
    result_.passed = quota && accurate && fast && responsive && result_.words > 0;
    double ratio = result_.accuracy()/std::max(1.0,spec.minimumAccuracy);
    if (spec.minimumWpm) ratio = std::min(ratio,result_.wpm()/spec.minimumWpm);
    if (spec.quota) ratio = std::min(ratio,double(result_.words)/spec.quota);
    if (spec.reactionLimit) ratio = std::min(ratio,1.0-std::min(1.0,result_.missed/30.0));
    result_.evaluation = std::clamp(int(std::lround(ratio*80)),0,100);
    if (supplementary_) return; // Supplementary practice cannot unlock or replace main exercise records.
    legacyClears_[exercise_] = legacyClears_[exercise_] || result_.passed;
    auto& table = legacyRecords_[exercise_];
    table.push_back(result_);
    std::stable_sort(table.begin(),table.end(),LegacyBetter);
    if (table.size() > 5) table.resize(5);
    auto& history = legacyHistory_[exercise_];
    history.push_back(result_.evaluation);
    if (history.size() > 10) history.erase(history.begin());
    save();
}
const std::vector<TypingDrillResult>& TypingDrillController::records(int exercise) const {
    static const std::vector<TypingDrillResult> empty;
    return exercise >= 0 && exercise < 13 ? records_[exercise] : empty;
}
const std::vector<int>& TypingDrillController::history(int exercise) const {
    static const std::vector<int> empty;
    return exercise >= 0 && exercise < 13 ? history_[exercise] : empty;
}
const std::vector<TypingDrillResult>& TypingDrillController::legacyRecords(int exercise) const {
    static const std::vector<TypingDrillResult> empty;
    return exercise>=0 && exercise<13?legacyRecords_[exercise]:empty;
}
const std::vector<int>& TypingDrillController::legacyHistory(int exercise) const {
    static const std::vector<int> empty;
    return exercise>=0 && exercise<13?legacyHistory_[exercise]:empty;
}
void TypingDrillController::noteOriginalKey(unsigned char expected,bool error) {
    // Source0x4021A0: one unit per expected-key attempt, twenty more on error.
    if (expected==32 || expected>=249) return;
    weak_[expected]=uint16_t(std::min(65535u,unsigned(weak_[expected])+(error?21u:1u)));
}
std::vector<TypingDrillWeakKey> TypingDrillController::weakKeyStats() const {
    std::vector<TypingDrillWeakKey> keys;
    for (unsigned i=0;i<249;++i) if (i!=32 && weak_[i]) keys.push_back({static_cast<unsigned char>(i),weak_[i],0});
    std::stable_sort(keys.begin(),keys.end(),[](const auto& a,const auto& b) { return a.weight>b.weight; });
    if (keys.size()>5) keys.resize(5);
    unsigned total=0;
    for (const auto& key:keys) total+=key.weight;
    for (auto& key:keys) key.percent=key.weight*100/total;
    return keys;
}
std::string TypingDrillController::weakKeys() const {
    std::string keys;
    for (const auto& key:weakKeyStats()) keys+=char(key.key);
    return keys;
}
void TypingDrillController::setProfilePath(std::string path) {
    if (path==profilePath_) return;
    profilePath_=std::move(path);
    clears_.fill(false); bestGrades_.fill(4); attempts_.fill(0);
    legacyClears_.fill(false);
    for (auto& records:records_) records.clear();
    for (auto& history:history_) history.clear();
    for (auto& records:legacyRecords_) records.clear();
    for (auto& history:legacyHistory_) history.clear();
    load();
}
bool TypingDrillController::save() const {
    if (profilePath_.empty()) return true;
    const std::filesystem::path destination(profilePath_);
    const std::filesystem::path temporary(profilePath_+".tmp");
    std::ofstream out(temporary,std::ios::trunc);
    if (!out) return false;
    out<<"HOTD2RE_TYPING_DRILL_2\n"<<std::setprecision(std::numeric_limits<double>::max_digits10);
    for (int i=0;i<13;++i) {
        out<<i<<' '<<bestGrades_[i]<<' '<<attempts_[i]<<' '<<records_[i].size()<<' '<<history_[i].size()<<'\n';
        for (const auto& r:records_[i]) {
            WriteLegacyRow(out,r);
            out<<' '<<r.metric<<' '<<r.grade<<' '<<r.failed<<' '<<std::quoted(std::string(r.name.data()))<<'\n';
        }
        for (int grade:history_[i]) out<<grade<<' ';
        out<<'\n';
    }
    // Preserve invented v1 scores as legacy data, including their old unlock
    // flags. They never participate in original grades, records or progression.
    out<<"LEGACY_V1\n";
    for (int i=0;i<13;++i) {
        out<<i<<' '<<legacyClears_[i]<<' '<<legacyRecords_[i].size()<<' '<<legacyHistory_[i].size()<<'\n';
        for (const auto& r:legacyRecords_[i]) { WriteLegacyRow(out,r);out<<'\n'; }
        for (int score:legacyHistory_[i]) out<<score<<' ';
        out<<'\n';
    }
    out.flush();
    if (!out) return false;
    out.close();
    if (!out) return false;
    std::ifstream previous(destination);
    std::string magic;
    const bool migrating=bool(std::getline(previous,magic)) && magic=="HOTD2RE_TYPING_DRILL_1";
    previous.close();
    std::error_code error;
    if (migrating) {
        const std::filesystem::path backup(profilePath_+".legacy-v1");
        if (!std::filesystem::exists(backup,error)) {
            error.clear();
            if (!std::filesystem::copy_file(destination,backup,std::filesystem::copy_options::none,error)) return false;
        } else if (error) return false;
    }
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::filesystem::rename(temporary,destination,error);
    return !error;
#endif
}
void TypingDrillController::load() {
    std::ifstream in(profilePath_);
    std::string magic;
    if (!std::getline(in,magic) || (magic!="HOTD2RE_TYPING_DRILL_1" && magic!="HOTD2RE_TYPING_DRILL_2")) return;
    decltype(records_) originals,legacy;
    decltype(history_) originalHistory,legacyHistory;
    decltype(legacyClears_) legacyClears{};
    decltype(bestGrades_) best;
    best.fill(4);
    decltype(attempts_) attempts{};
    if (magic=="HOTD2RE_TYPING_DRILL_2") {
        for (int i=0;i<13;++i) {
            int id=-1,grade=-1;
            unsigned count=0,ages=0,played=0;
            if (!(in>>id>>grade>>played>>count>>ages) || id!=i || grade<0 || grade>4 || played>99 ||
                count>5 || ages>30 || played<ages) return;
            best[i]=grade; attempts[i]=played;
            for (unsigned j=0;j<count;++j) {
                TypingDrillResult r;
                r.original=true;
                std::string name;
                if (!ReadLegacyRow(in,r) || !(in>>r.metric>>r.grade>>r.failed>>std::quoted(name)) || name.size()>5) return;
                std::copy(name.begin(),name.end(),r.name.begin());
                if (!ValidOriginal(i,r) ||
                    r.metric==0 || r.passed!=(r.grade<=2) || (i==12 && r.failed)) return;
                if (!originals[i].empty()) {
                    const int32_t previous=originals[i].back().metric;
                    if (TypingDrillOriginalLowerIsBetter(i)?r.metric<previous:r.metric>previous) return;
                }
                originals[i].push_back(r);
            }
            for (unsigned j=0;j<ages;++j) {
                int value=-1;
                if (!(in>>value) || value<0 || value>4) return;
                originalHistory[i].push_back(value);
            }
        }
        if (!(in>>magic) || magic!="LEGACY_V1") return;
    }
    for (int i=0;i<13;++i) {
        int id=-1,clear=-1;
        size_t count=0,ages=0;
        if (!(in>>id>>clear>>count>>ages) || id!=i || clear<0 || clear>1 || count>5 || ages>10) return;
        legacyClears[i]=clear!=0;
        for (size_t j=0;j<count;++j) {
            TypingDrillResult r;
            if (!ReadLegacyRow(in,r) || !ValidLegacy(r)) return;
            legacy[i].push_back(r);
        }
        for (size_t j=0;j<ages;++j) {
            int score=-1;
            if (!(in>>score) || score<0 || score>100) return;
            legacyHistory[i].push_back(score);
        }
    }
    in>>std::ws;
    if (!in.eof()) return;
    records_=std::move(originals); history_=std::move(originalHistory);
    legacyRecords_=std::move(legacy); legacyHistory_=std::move(legacyHistory);
    legacyClears_=legacyClears; bestGrades_=best; attempts_=attempts;
    for (int i=0;i<13;++i) clears_[i]=bestGrades_[i]<=2;
}
} // namespace game
