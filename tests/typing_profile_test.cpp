#include "game/typing_profile.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
void Require(bool condition,const char* message) {
    if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1); }
}
}
int main() {
    game::TypingChapterChoice choice;
    for (int chapter=1;chapter<=5;++chapter) Require(choice.canPlay(chapter),"fresh source chooser exposes chapters one through five");
    Require(!choice.canPlay(6),"fresh source chooser reserves the last chapter");
    choice.complete(3);
    Require(choice.status == std::array<uint8_t,6>{2,2,1,0,0,2},"finishing a later chapter retires earlier unplayed chapters");
    choice.complete(5);
    Require(choice.canPlay(6),"chapter five opens the source final chapter");

    game::TypingProfile profile;
    Require(game::TypingOriginalChapterCoins(1,3000,80,20,false)==31 &&
            game::TypingOriginalChapterCoins(6,3999,94,29,true)==1 &&
            game::TypingOriginalChapterCoins(0,9999,100,99,false)==0,
            "Original coins use the source chapter-specific score, accuracy, rank and continue conditions");
    Require(profile.storeOriginalCoins(1,31)==31 && profile.storeOriginalCoins(1,31)==0 &&
            profile.originalProgress().coins()==5 && profile.originalProgress().unlocks()==1,
            "Original chapter achievements are permanent unique coins, never repeatable farming rewards");
    Require(profile.storeOriginalCoins(2,31)==31 && profile.originalProgress().coins()==10 &&
            profile.originalProgress().unlocks()==9,"every five unique coins unlock the source cumulative reward flag");
    const auto& seeded=profile.scoreRecords();
    Require(seeded[0].points==5000 && seeded[7].points==4300 && seeded[0].name=="Shiino-man" &&
            seeded[7].name=="Melissa" && seeded[0].year==2000 && seeded[1].proficiency==0x11 &&
            profile.scoreRecords(true)[0].points==5000,
            "both source score tables begin with the authored eight registration records");
    game::TypingScoreRecord total;
    total.points=4300;
    Require(profile.scoreRank(total)==-1,"a tie with the eighth seeded score cannot qualify");
    total.points=5000;
    Require(profile.scoreRank(total)==1,"a tied score follows the equal source record");
    total.points=7000;total.year=2026;total.month=10;total.day=27;total.proficiency=0x21;
    total.name="TWELVE-CHARS-plus";total.age="123";total.occupation="TWELVE-CHARS-plus";
    total.comment=std::string(50,'X');
    Require(profile.storeScore(total)==0 && profile.scoreRecords()[0].name=="TWELVE-CHARS" &&
            profile.scoreRecords()[0].age=="12" && profile.scoreRecords()[0].occupation=="TWELVE-CHARS" &&
            profile.scoreRecords()[0].comment.size()==45 && profile.scoreRecords(true)[0].points==5000,
            "source score registration preserves its four field limits and independent mode tables");
    game::TypingFinalEvaluation skill;
    Require(profile.finalRank(skill)==0,"the source zero-filled skill table accepts a fresh minimum score");
    skill.proficiency=17;skill.grades={1,2,3,4,0};skill.chapters[2]={1,7,4321,61.25f};
    Require(profile.storeFinal(skill,"ABCDEextra")==0 && std::string(profile.finalRecords()[0].name.data())=="ABCDE",
            "skill records keep the original five-character name limit");
    Require(profile.finalRank(skill)==1,"a tied skill score ranks after the existing equal score");
    for (int i=1;i<8;++i) Require(profile.storeFinal(skill,"TIE")==i,"equal scores fill the table in insertion order");
    Require(profile.finalRank(skill)==-1 && profile.storeFinal(skill,"FULL")==-1,
            "an eighth-place tie does not enter the source skill table");
    ++skill.skillPoints;
    Require(profile.storeFinal(skill,"TOP")==0 && profile.finalRecords()[1].skillPoints==100 &&
            std::string(profile.finalRecords()[1].name.data())=="ABCDE",
            "a greater score shifts complete original skill records down one position");
    const auto first=profile.chapterResult(1,4000,false);
    Require(first.status==1 && first.points==4000 && first.ordinal==1 && first.deviation>50,
            "source chapter history starts with its fifty authored benchmark scores");
    for (unsigned i=0;i<80;++i) profile.chapterResult(1,5000+i,true);
    Require(profile.chapterResult(1,4000,false).ordinal==51,
            "user history fills fifty slots and preserves the original fifty benchmark records");
    const auto path=std::filesystem::temp_directory_path()/
        ("totd_profile_test_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".txt");
    Require(profile.save(path.string()),"typing-only profile saves independently of gun records");
    game::TypingProfile loaded;
    Require(loaded.load(path.string()) && loaded.chapterResult(1,4000,false).ordinal==51,
            "typing-only history survives a profile reload");
    Require(loaded.originalProgress().chapterCoins[0]==31 && loaded.originalProgress().chapterCoins[1]==31 &&
            loaded.originalProgress().coins()==10,"Original coin progress survives profile reload");
    const auto& restored=loaded.finalRecords()[0];
    Require(restored.skillPoints==101 && restored.proficiency==17 && restored.grades==skill.grades &&
            std::string(restored.name.data())=="TOP" && restored.chapters[2].status==1 &&
            restored.chapters[2].ordinal==7 && restored.chapters[2].points==4321 && restored.chapters[2].deviation==61.25f,
            "the separate profile preserves all original skill-record fields");
    const auto& restoredScore=loaded.scoreRecords()[0];
    Require(restoredScore.points==7000 && restoredScore.year==2026 && restoredScore.month==10 && restoredScore.day==27 &&
            restoredScore.proficiency==0x21 && restoredScore.name=="TWELVE-CHARS" && restoredScore.age=="12" &&
            restoredScore.occupation=="TWELVE-CHARS" && restoredScore.comment==std::string(45,'X'),
            "the separate profile preserves score registration fields and date");
    std::ifstream saved(path);
    std::ostringstream legacy,versionTwo;
    std::string line;
    for (int i=0;i<21 && std::getline(saved,line);++i) {
        if (i<13) legacy<<(i==0?"TOTD_PROFILE_1":line)<<'\n';
        versionTwo<<(i==0?"TOTD_PROFILE_2":line)<<'\n';
    }
    saved.close();
    { std::ofstream old(path);old<<legacy.str(); }
    game::TypingProfile upgraded;
    Require(upgraded.load(path.string()) && upgraded.chapterResult(1,4000,false).ordinal==51 &&
            upgraded.finalRecords()[0].skillPoints==0 && upgraded.scoreRecords()[0].points==5000 &&
            upgraded.originalProgress().coins()==0,
            "existing chapter histories load with the original empty skill-ranking defaults");
    { std::ofstream old(path);old<<versionTwo.str(); }
    game::TypingProfile upgradedTwo;
    Require(upgradedTwo.load(path.string()) && upgradedTwo.chapterResult(1,4000,false).ordinal==51 &&
            upgradedTwo.finalRecords()[0].skillPoints==101 && upgradedTwo.scoreRecords()[0].points==5000,
            "existing version-two skill and chapter records migrate with authored score defaults");
    std::filesystem::remove(path);

    Require(game::TypingPackedProficiency(27)==0 && game::TypingPackedProficiency(25)==0x11 &&
            game::TypingPackedProficiency(6)==0x40 && game::TypingPackedProficiency(0)==0x52,
            "source proficiency class and subgrade boundaries");
    Require(game::TypingConcentrationWeight("ABQW12?_%")==15,
            "concentration uses source rare-letter, punctuation and capped-digit weights");
    game::TypingRunMetrics metrics;
    metrics.words=20; metrics.speedCharacters=6000; metrics.speedFrames=36000;
    metrics.reactionFrames=120; metrics.reactions=2;
    metrics.concentrationBase=100; metrics.concentrationPoints=100;
    std::array<game::TypingChapterRecord,6> chapters{};
    chapters[0]={1,1,4000,50}; chapters[1]={1,1,5000,50};
    auto evaluation=game::TypingEvaluateRun(metrics,chapters,100,100,10,10,false);
    Require(evaluation.proficiency==28 && evaluation.grades==std::array<uint8_t,5>{0,0,0,1,0},
            "source five skill grades use verified speed, accuracy, reaction, judgment and concentration thresholds");
    const auto before=evaluation.skillPoints;
    const auto cleared=game::TypingEvaluateRun(metrics,chapters,100,100,10,10,true);
    Require(cleared.skillPoints>before,"the source clear bonus participates in skill points");
    metrics.words=4;
    evaluation=game::TypingEvaluateRun(metrics,chapters,100,100,10,10,false);
    Require(evaluation.grades==std::array<uint8_t,5>{3,3,3,4,3},"short runs receive the original skill-sample penalty");
    std::puts("typing_profile_test: source chapter selection, skill grades and separate history passed");
}
