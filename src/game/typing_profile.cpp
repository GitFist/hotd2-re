#include "game/typing_profile.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>

namespace game {
namespace {
uint8_t Penalized(int grade, uint64_t words) {
    return static_cast<uint8_t>(std::min(4, grade + (words < 5 ? 3 : words < 10 ? 2 : words < 15 ? 1 : 0)));
}
std::string EnglishText(std::string_view input, size_t maximum) {
    std::string result;
    for (unsigned char c:input) {
        if (c>=32 && c<=126) result+=char(c);
        if (result.size()==maximum) break;
    }
    return result;
}
}

uint8_t TypingPackedProficiency(unsigned value) {
    constexpr unsigned limits[] = {27,25,23,21,19,17,15,13,11,9,7,6,5,4,3};
    constexpr uint8_t packed[] = {0,0x11,0x10,0x12,0x21,0x20,0x22,0x31,0x30,0x32,0x41,0x40,0x42,0x51,0x50};
    for (unsigned i=0;i<std::size(limits);++i) if (value >= limits[i]) return packed[i];
    return 0x52;
}

unsigned TypingProficiency(const TypingRunMetrics& m) {
    const float duration = std::max(1.0f, float(m.speedFrames) * 2.7777778086601757e-05f);
    const int speed = static_cast<int>((float(m.speedCharacters) / duration) / 4.699999809265137f);
    return static_cast<unsigned>(std::clamp(int(float(speed) * .02222222276031971f),0,999));
}

unsigned TypingConcentrationWeight(std::string_view text) {
    constexpr std::string_view letters = "\\.,?!_%[]'QWPZXV";
    constexpr unsigned weights[] = {2,2,2,3,3,3,3,2,2,3,1,1,1,1,1,1};
    unsigned digits = 0, total = 0;
    for (unsigned char c : text) {
        if (c >= 'a' && c <= 'z') c -= 'a'-'A';
        if (c >= '0' && c <= '9') digits += 2;
        else if (const auto at=letters.find(char(c));at != std::string_view::npos) total += weights[at];
    }
    return total + std::min(digits,4u);
}

TypingFinalEvaluation TypingEvaluateRun(const TypingRunMetrics& m,
    const std::array<TypingChapterRecord,6>& chapters, uint64_t correct, uint64_t attempted,
    unsigned civilians, unsigned rescued, bool cleared) {
    TypingFinalEvaluation e;
    e.chapters = chapters;
    e.proficiency = static_cast<uint8_t>(TypingProficiency(m));
    const unsigned complete = static_cast<unsigned>(std::count_if(chapters.begin(),chapters.end(),
        [](const auto& chapter) { return chapter.status == 1; }));
    e.grades[0] = Penalized(std::max(0,int(TypingPackedProficiency(e.proficiency) >> 4)-1),m.words);
    const auto accuracy = TypingAccuracyPercent(correct,attempted);
    e.grades[1] = Penalized(accuracy >= 98 ? 0 : accuracy >= 95 ? 1 : accuracy >= 90 ? 2 : accuracy >= 80 ? 3 : 4,m.words);
    int reaction = 4;
    if (m.reactions) {
        const uint64_t delay = m.reactionFrames / m.reactions;
        reaction = delay <= 60 ? 0 : delay <= 90 ? 1 : delay <= 120 ? 2 : delay <= 150 ? 3 : 4;
        if (delay <= 150 && m.smallThreatHits >= 2*complete) ++reaction;
    }
    e.grades[2] = Penalized(reaction,m.words);
    int judgment = civilians ? int(uint64_t(rescued)*100/civilians) : 0;
    if (m.propWords) judgment += int(float(m.completedProps*100/m.propWords) * 1.2000000476837158f);
    judgment -= int(5*m.otherTargetHits / std::max(complete,1u));
    e.grades[3] = Penalized(judgment >= 120 ? 0 : judgment >= 100 ? 1 : judgment >= 70 ? 2 : judgment >= 50 ? 3 : 4,m.words);
    const uint64_t concentration = m.concentrationBase ? m.concentrationPoints*100/m.concentrationBase : 0;
    e.grades[4] = Penalized(concentration >= 91 ? 0 : concentration >= 81 ? 1 : concentration >= 61 ? 2 : concentration >= 41 ? 3 : 4,m.words);

    // Source47DDF4..DF81 weights the two largest chapter results, applies the
    // completion award, then the five skill multipliers at PC float precision.
    constexpr unsigned chapterWeights[] = {10,13,18,22,28,35};
    std::array<int,6> weighted{};
    for (unsigned i=0;i<weighted.size();++i) if (chapters[i].status)
        weighted[i] = int((float(chapterWeights[i]) * .1f * float(chapters[i].points)) * 500.0f);
    std::sort(weighted.begin(),weighted.end(),std::greater<int>());
    float points = float(weighted[0] + weighted[1]);
    if (complete >= 2) points *= 1.2000000476837158f;
    if (cleared) points += 2000000.0f;
    constexpr unsigned gradeWeights[] = {20,15,10,8,6}, skillWeights[] = {15,13,11,10,10};
    for (unsigned i=0;i<5;++i) points *= (float(gradeWeights[e.grades[i]])*.1f) * (float(skillWeights[i])*.1f);
    points *= .0001f;
    e.skillPoints = points >= 100000000.0f ? 99999999u : static_cast<uint32_t>(std::max(points,100.0f));
    return e;
}

bool TypingChapterChoice::canPlay(int chapter) const {
    return chapter >= 1 && chapter <= 6 && status[chapter-1] == 0;
}
void TypingChapterChoice::complete(int chapter) {
    if (chapter < 1 || chapter > 6) return;
    for (int i=0;i<chapter-1;++i) if (status[i] == 0) status[i] = 2;
    status[chapter-1] = 1;
    if (chapter == 5) status[5] = 0;
}

TypingProfile::TypingProfile() {
    // Source4A0F40 clears all eight records when creating a fresh profile.
    for (auto& record:final_) { record.skillPoints=0;record.grades={}; }
    // Source47CEC0 seeds both story/original score tables identically, keeping
    // them independent of the eight zero-filled skill-evaluation records.
    const std::array<TypingScoreRecord,8> scores = {{
        {5000,2000,0,0x00,3,7,"Shiino-man","18","Florist","Oooooh! What a splendid game! I had a blast! "},
        {4900,1999,1,0x11,11,3,"Sally","26","Scientist","I've yet to    unleash my     full potential."},
        {4800,1999,0,0x10,12,13,"David","31","Rock star","Wow man. Like  that was cool, Like...Yeah!   "},
        {4700,1999,0,0x12,12,13,"Martin","37","Accountant","It was a scary game. I almost wet myself.    "},
        {4600,1999,1,0x20,12,13,"Parkinson","28","Pro-surfer","AaaaaaaaaaaaaaaRrrrrrrrrrrrrrrggggghhhhhhhhh!"},
        {4500,2000,1,0x21,1,1,"John","42","Tap dancer","It reminds     me of House of the Dead 2...  "},
        {4400,1999,0,0x22,12,13,"Jenny","16","Sega fan","Sega rules.    Period.                       "},
        {4300,1999,0,0x31,12,13,"Melissa","26","Secretary","\"Type or Die!\" It's the only  way for me!!   "}
    }};
    scores_[0]=scores_[1]=scores;
    // Source48FDC0 +5BBF20: fifty seeded records remain in slots0..49, while
    // user results cycle through slots50..99 after the count reaches100.
    constexpr unsigned best[] = {4100,5500,4600,4100,5000,4900};
    constexpr unsigned maximum[] = {4000,5400,4500,4000,4900,4800};
    for (unsigned chapter=0;chapter<history_.size();++chapter) {
        auto& history=history_[chapter];
        history.best=static_cast<uint16_t>(best[chapter]);
        for (unsigned i=0;i<50;++i) history.scores[i]=static_cast<uint16_t>(1000.0f + float(i) * (float(maximum[chapter]-1000) * .02f));
    }
}

TypingChapterRecord TypingProfile::chapterResult(int chapter, uint64_t points, bool store) {
    TypingChapterRecord r;
    if (chapter < 1 || chapter > 6) return r;
    auto& h=history_[chapter-1];
    r.status=1;
    r.points=static_cast<uint16_t>(std::min<uint64_t>(points,9999));
    unsigned ordinal=1;
    for (const auto prior:h.scores) if (prior > r.points) ++ordinal;
    if (h.count < 100) ordinal = ordinal*100/(h.count+1);
    r.ordinal=static_cast<uint8_t>(std::min(ordinal,99u));
    float sum=float(r.points), squares=float(r.points)*float(r.points);
    for (unsigned i=0;i<h.count;++i) {
        sum += h.scores[i];
        squares += float(h.scores[i])*float(h.scores[i]);
    }
    const float mean=sum/float(h.count+1);
    const float deviation=std::sqrt(std::max(0.0f,squares/float(h.count+1)-mean*mean));
    r.deviation=deviation <= 0 ? 50.0f : (float(r.points)-mean)/deviation*10.0f+50.0f;
    if (store) {
        h.best=std::max(h.best,r.points);
        h.count=std::min<uint16_t>(100,h.count+1);
        h.scores[h.next]=r.points;
        if (++h.next >= 100) h.next=50;
    }
    return r;
}

unsigned TypingProfile::chapterBest(int chapter) const {
    return chapter >= 1 && chapter <= 6 ? history_[chapter-1].best : 0;
}
float TypingProfile::chapterMean(int chapter, uint64_t points) const {
    if (chapter < 1 || chapter > 6) return 0;
    const auto& h=history_[chapter-1];
    float sum=float(std::min<uint64_t>(points,9999));
    for (unsigned i=0;i<h.count;++i) sum += h.scores[i];
    return sum/float(h.count+1);
}

int TypingProfile::finalRank(const TypingFinalEvaluation& evaluation) const {
    for (unsigned i=0;i<final_.size();++i) if (evaluation.skillPoints > final_[i].skillPoints) return int(i);
    return -1;
}
int TypingProfile::storeFinal(const TypingFinalEvaluation& evaluation, std::string_view name) {
    const int at=finalRank(evaluation);
    if (at < 0) return -1;
    for (int i=int(final_.size())-1;i>at;--i) final_[i]=final_[i-1];
    auto& record=final_[at];
    record=evaluation;
    record.name={};
    size_t count=0;
    for (unsigned char c:name) {
        if (c >= 32 && c <= 126) record.name[count++]=char(c);
        if (count == 5) break;
    }
    return at;
}

int TypingProfile::scoreRank(const TypingScoreRecord& record, bool original) const {
    const auto& table=scoreRecords(original);
    for (unsigned i=0;i<table.size();++i) if (record.points>table[i].points) return int(i);
    return -1;
}
int TypingProfile::storeScore(const TypingScoreRecord& candidate, bool original) {
    const int at=scoreRank(candidate,original);
    if (at<0) return -1;
    auto& table=scores_[original?1:0];
    for (int i=int(table.size())-1;i>at;--i) table[i]=table[i-1];
    auto& record=table[at];
    record=candidate;
    record.name=EnglishText(record.name,12);
    record.age=EnglishText(record.age,2);
    record.occupation=EnglishText(record.occupation,12);
    record.comment=EnglishText(record.comment,45);
    return at;
}

bool TypingProfile::load(const std::string& path) {
    std::ifstream in(path);
    std::string magic;
    if (!(in>>magic) || (magic != "TOTD_PROFILE_1" && magic != "TOTD_PROFILE_2" && magic != "TOTD_PROFILE_3" && magic != "TOTD_PROFILE_4")) return false;
    auto next=history_;
    for (auto& h:next) {
        unsigned count=0,index=0,best=0;
        if (!(in>>count>>index>>best) || count<50 || count>100 || index<50 || index>=100 || best>9999) return false;
        h.count=static_cast<uint16_t>(count);h.next=static_cast<uint16_t>(index);h.best=static_cast<uint16_t>(best);
        for (auto& score:h.scores) { unsigned value=0;if (!(in>>value) || value>9999) return false;score=static_cast<uint16_t>(value); }
    }
    auto finals=final_;
    if (magic != "TOTD_PROFILE_1") for (auto& record:finals) {
        unsigned original=0,player=0,proficiency=0;
        if (!(in>>original>>player>>proficiency) || original>1 || player>1 || proficiency>255) return false;
        record.originalMode=uint8_t(original);record.player=uint8_t(player);record.proficiency=uint8_t(proficiency);
        for (auto& grade:record.grades) { unsigned value=0;if (!(in>>value) || value>4) return false;grade=uint8_t(value); }
        std::string name;
        if (!(in>>record.skillPoints>>std::quoted(name)) || record.skillPoints>99999999 || name.size()>5) return false;
        record.name={};std::copy(name.begin(),name.end(),record.name.begin());
        for (auto& chapter:record.chapters) {
            unsigned status=0,ordinal=0,points=0;
            if (!(in>>status>>ordinal>>points>>chapter.deviation) || status>2 || ordinal>99 || points>9999 ||
                !std::isfinite(chapter.deviation)) return false;
            chapter.status=uint8_t(status);chapter.ordinal=uint8_t(ordinal);chapter.points=uint16_t(points);
        }
    }
    auto scores=scores_;
    if (magic == "TOTD_PROFILE_3" || magic == "TOTD_PROFILE_4") for (auto& table:scores) for (auto& record:table) {
        unsigned points=0,year=0,player=0,proficiency=0,month=0,day=0;
        if (!(in>>points>>year>>player>>proficiency>>month>>day>>std::quoted(record.name)>>std::quoted(record.age)>>
              std::quoted(record.occupation)>>std::quoted(record.comment)) || points>65535 || year>65535 ||
            player>1 || proficiency>255 || month>12 || day>31 || record.name.size()>12 || record.age.size()>2 ||
            record.occupation.size()>12 || record.comment.size()>45) return false;
        record.points=uint16_t(points);record.year=uint16_t(year);record.player=uint8_t(player);
        record.proficiency=uint8_t(proficiency);record.month=uint8_t(month);record.day=uint8_t(day);
    }
    TypingOriginalProgress original;
    if (magic == "TOTD_PROFILE_4") for (auto& coin:original.chapterCoins) {
        unsigned mask=0;
        if (!(in>>mask) || mask>31) return false;
        coin=uint8_t(mask);
    }
    history_=next;final_=finals;scores_=std::move(scores);original_=original;
    return true;
}
bool TypingProfile::save(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return false;
    out<<"TOTD_PROFILE_4\n";
    for (const auto& h:history_) {
        out<<h.count<<' '<<h.next<<' '<<h.best<<'\n';
        for (auto score:h.scores) out<<score<<' ';
        out<<'\n';
    }
    out<<std::setprecision(9);
    for (const auto& record:final_) {
        out<<unsigned(record.originalMode)<<' '<<unsigned(record.player)<<' '<<unsigned(record.proficiency);
        for (auto grade:record.grades) out<<' '<<unsigned(grade);
        out<<' '<<record.skillPoints<<' '<<std::quoted(std::string(record.name.data()));
        for (const auto& chapter:record.chapters)
            out<<' '<<unsigned(chapter.status)<<' '<<unsigned(chapter.ordinal)<<' '<<chapter.points<<' '<<chapter.deviation;
        out<<'\n';
    }
    for (const auto& table:scores_) for (const auto& record:table)
        out<<record.points<<' '<<record.year<<' '<<unsigned(record.player)<<' '<<unsigned(record.proficiency)<<' '<<
            unsigned(record.month)<<' '<<unsigned(record.day)<<' '<<std::quoted(record.name)<<' '<<std::quoted(record.age)<<' '<<
            std::quoted(record.occupation)<<' '<<std::quoted(record.comment)<<'\n';
    for (uint8_t coins:original_.chapterCoins) out<<unsigned(coins)<<' ';
    out<<'\n';
    return bool(out);
}

uint8_t TypingProfile::storeOriginalCoins(int chapter,uint8_t earned) {
    if (chapter<1 || chapter>6) return 0;
    auto& current=original_.chapterCoins[size_t(chapter-1)];
    const uint8_t added=uint8_t((earned&31u)&~current);
    current|=added;
    return added;
}

} // namespace game
