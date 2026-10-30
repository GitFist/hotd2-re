#include "game/typing_drill.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>

namespace {
void Require(bool condition,const char* message) {
    if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1); }
}
game::TypingDrillResult Result(int32_t metric,unsigned marker=1,bool failed=false) {
    game::TypingDrillResult result;
    result.metric=metric;result.words=marker;result.correct=10;result.seconds=1;
    result.failed=failed;
    return result;
}
void GradingBoundaries() {
    // Independent expected boundaries from the PC grade branches0x4A6D20;
    // times are60-Hz ticks, Speed3 is1000*mean ticks per letter.
    struct Boundary { bool lower;int unit;std::array<int32_t,4> threshold; };
    constexpr int32_t any=std::numeric_limits<int32_t>::max();
    constexpr std::array<Boundary,13> source={{
        {true,5,{3600,5400,7800,12000}}, {false,2,{35,15,6,3}},
        {true,6,{12000,18000,24000,any}}, {false,1,{100,60,40,20}},
        {false,0,{45,30,20,10}}, {false,5,{7200,3600,2700,1800}},
        {false,0,{40,25,15,10}}, {true,5,{2700,5400,7200,10800}},
        {false,2,{45,30,15,10}}, {false,3,{128,64,32,17}},
        {false,4,{70,50,30,18}}, {false,0,{36,24,16,8}},
        {true,5,{16800,19200,21600,any}}
    }};
    for (int exercise=0;exercise<13;++exercise) {
        const auto& row=source[exercise];
        Require(game::TypingDrillOriginalUnit(exercise)==row.unit,"original result units match each trial");
        Require(game::TypingDrillOriginalLowerIsBetter(exercise)==row.lower,"original ranking direction matches each trial");
        for (int grade=0;grade<4;++grade) {
            const int32_t boundary=row.threshold[grade];
            Require(game::TypingDrillOriginalGrade(exercise,boundary)==grade,"exact grade boundary is inclusive");
            if (boundary!=any) {
                const int32_t worse=boundary+(row.lower?1:-1);
                Require(game::TypingDrillOriginalGrade(exercise,worse)==grade+1,"one raw unit beyond boundary changes grade");
            }
        }
    }
    Require(game::TypingDrillOriginalGrade(2,12000,true)==4 && game::TypingDrillOriginalGrade(12,16800,true)==4,
            "Speed3 and Final explicit failure sentinels force E");
    Require(game::TypingDrillOriginalGrade(9,128,true)==0,"a stopped count trial retains its earned grade");
    Require(game::TypingDrillOriginalGrade(-1,10)==4 && game::TypingDrillOriginalGrade(0,-1)==4,"invalid score inputs are rejected");
}
void Progression() {
    game::TypingDrillController drill;
    for (int i=0;i<12;++i) Require(drill.unlocked(i)==(i%3==0),"only first trial of each category is initially unlocked");
    Require(!drill.unlocked(12),"Final starts locked");
    drill.recordOriginalResult(0,Result(12000));
    Require(drill.result().grade==3 && !drill.clears()[0] && !drill.unlocked(1),"D does not unlock next trial");
    drill.recordOriginalResult(0,Result(7800));
    Require(drill.clears()[0] && drill.unlocked(1),"C unlocks next trial");
    drill.recordOriginalResult(0,Result(13000));
    Require(drill.bestGrades()[0]==2 && drill.unlocked(1),"a worse result cannot erase best proficiency");
    game::TypingDrillController final;
    const int32_t thirdLevelC[]={24000,2700,15,16};
    for (int group=0;group<4;++group) {
        final.recordOriginalResult(group*3+2,Result(thirdLevelC[group]));
        Require(final.unlocked(12)==(group==3),"Final requires C or better in all four level-three trials");
    }
    Require(!final.clears()[0],"Final predicate uses stored level-three proficiency rather than invented twelve-clear flag");
    Require(final.beginOriginalAttempt(12),"unlocked authored Final can start without generic word generation");
    Require(final.running() && final.prompt().empty() && final.result().original,"original start keeps generic prompt harness inactive");
}
void OriginalRecords() {
    game::TypingDrillController drill;
    for (unsigned i=0;i<5;++i) {
        drill.recordOriginalResult(1,Result(20,i));
        Require(drill.recordRank()==int(i),"strict equal values follow existing records");
    }
    auto equal=Result(20,99);equal.correct=999;equal.errors=0;
    drill.recordOriginalResult(1,equal);
    Require(drill.recordRank()==-1 && drill.records(1).size()==5,"sixth equal result cannot displace existing top five");
    Require(!drill.setOriginalRecordName("LOST"),"unranked attempts cannot rename an existing tied record");
    for (unsigned i=0;i<5;++i) Require(drill.records(1)[i].words==i,"accuracy and word statistics never reorder tied raw scores");
    drill.recordOriginalResult(1,Result(21,77));
    Require(drill.recordRank()==0 && drill.records(1).front().metric==21,"greater count ranks higher");
    Require(drill.setOriginalRecordName("RANK1") && std::string(drill.result().name.data())=="RANK1" &&
            std::string(drill.records(1).front().name.data())=="RANK1","five-character name updates the current ranked row");
    Require(!drill.setOriginalRecordName("TOOLONG") && !drill.setOriginalRecordName("A\nB"),
            "name setter rejects overflow and unsupported control characters");
    drill.recordOriginalResult(0,Result(5400));
    drill.recordOriginalResult(0,Result(3600));
    Require(drill.records(0).front().metric==3600,"lower completion time ranks higher");
    drill.recordOriginalResult(12,Result(20000,1,true));
    Require(drill.records(12).empty() && drill.history(12).front()==4,"failed Final keeps E history without entering records");
    drill.recordOriginalResult(2,Result(12000,1,true));
    Require(drill.result().metric==0 && drill.result().grade==4 && drill.records(2).empty(),"failed Speed3 uses original zero metric and E grade");
    drill.recordOriginalResult(9,Result(128,1,true));
    Require(drill.result().grade==0 && drill.records(9).size()==1,"count trials rank their earned count after stopping");
    for (int i=0;i<110;++i) drill.recordOriginalResult(11,Result(i%2?36:8));
    Require(drill.history(11).size()==30 && drill.attempts()[11]==99,"source history stores thirty grades and saturates attempts at99");
    Require(drill.history(11)[0]==0 && drill.history(11)[1]==3,"grade history is newest first");
    const auto count=drill.records(11).size(),history=drill.history(11).size();
    const auto attempts=drill.attempts()[11];
    drill.recordOriginalResult(11,Result(100),true);
    Require(drill.records(11).size()==count && drill.history(11).size()==history && drill.attempts()[11]==attempts,
            "supplementary scenes do not change main records, history or proficiency");
}
void WeakKeys() {
    game::TypingDrillController drill;
    drill.noteOriginalKey('b',true);
    drill.noteOriginalKey('a',true);
    drill.noteOriginalKey('a',false);
    drill.noteOriginalKey(' ',true);
    drill.noteOriginalKey(249,true);
    auto stats=drill.weakKeyStats();
    Require(stats.size()==2 && stats[0].key=='a' && stats[0].weight==22 && stats[1].weight==21,
            "weakness uses expected-key frequency plus twenty additional units per mistake");
    Require(stats[0].percent==51 && stats[1].percent==48,"weak-key share truncates against the top-five total");
    Require(drill.beginOriginalAttempt(0,true) && drill.supplementaryKeys()=="ab" && drill.weakKeys().empty(),
            "supplementary start captures exact weak keys before resetting attempt counters");
    for (unsigned char c='f';c>='a';--c) drill.noteOriginalKey(c,false);
    stats=drill.weakKeyStats();
    Require(drill.weakKeys()=="abcde" && stats[0].percent==20,"equal burdens use original ascending key order and top-five denominator");
    for (int i=0;i<4000;++i) drill.noteOriginalKey('x',true);
    Require(drill.weakKeyStats().front().weight==65535,"original key burden saturates at sixteen-bit maximum");
    game::TypingDrillController copied;
    copied.setOriginalKeyWeights(drill.originalKeyWeights());
    Require(copied.weakKeys()==drill.weakKeys(),"child scene weakness data transfers without reweighting");
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
void WriteV1(const std::filesystem::path& path) {
    std::ofstream out(path);
    out<<"HOTD2RE_TYPING_DRILL_1\n";
    for (int i=0;i<13;++i) {
        out<<i<<" 1 "<<(i==0?1:0)<<" "<<(i==0?1:0)<<'\n';
        if (i==0) out<<"7 35 1 0 12.5 4.5 88 1\n88\n";
        else out<<'\n';
    }
}
void Persistence(const std::filesystem::path& path) {
    WriteV1(path);
    const std::string original=Read(path);
    game::TypingDrillController migrated;
    migrated.setProfilePath(path.string());
    Require(migrated.records(0).empty() && migrated.history(0).empty() && !migrated.clears()[0] && !migrated.unlocked(1),
            "v1 invented scores and pass flags never become original results or unlocks");
    Require(migrated.legacyRecords(0).size()==1 && migrated.legacyRecords(0)[0].evaluation==88 && migrated.legacyClears()[12],
            "v1 records, statistics and progression flags remain available as legacy data");
    Require(migrated.save(),"v1 migration writes v2 profile");
    Require(Read(path.string()+".legacy-v1")==original,"migration keeps an exact original v1 backup");
    migrated.recordOriginalResult(0,Result(7800));
    Require(migrated.setOriginalRecordName("A\"\\B"),"supported quote and slash characters can be saved in a record name");
    game::TypingDrillController restored;
    restored.setProfilePath(path.string());
    Require(restored.records(0).size()==1 && restored.records(0)[0].metric==7800 && restored.records(0)[0].original &&
            restored.bestGrades()[0]==2 && restored.unlocked(1) && restored.attempts()[0]==1 && restored.history(0)[0]==2,
            "original raw result, grade, attempts, history and proficiency round trip");
    Require(std::string(restored.records(0)[0].name.data())=="A\"\\B","original record name escapes round trip without changing its bytes");
    Require(restored.legacyRecords(0).size()==1 && restored.legacyRecords(0)[0].seconds==12.5,
            "subsequent original results preserve separate legacy records");
    Require(Read(path.string()+".legacy-v1")==original,"later saves never replace the migration backup");
    const std::string before=Read(path);
    const std::filesystem::path temporary(path.string()+".tmp");
    std::filesystem::create_directory(temporary);
    Require(!restored.save() && Read(path)==before,"failed temporary writer cannot truncate player's current profile");
    std::filesystem::remove(temporary);
    restored.recordOriginalResult(0,Result(7000));
    Require(restored.setOriginalRecordName("FIRST"),"new ranked name stores before failed-write test");
    const std::string named=Read(path);
    std::filesystem::create_directory(temporary);
    Require(!restored.setOriginalRecordName("OTHER") && std::string(restored.result().name.data())=="FIRST" && Read(path)==named,
            "failed name save keeps the prior in-memory and on-disk name for retry");
    std::filesystem::remove(temporary);
    std::ofstream broken(path,std::ios::trunc);
    broken<<"HOTD2RE_TYPING_DRILL_2\n0 0 1 1 1\n1 1 0 0 1 0 0 1 -1 0 0\n";
    broken.close();
    game::TypingDrillController malformed;
    malformed.setProfilePath(path.string());
    Require(!malformed.clears()[0] && malformed.records(0).empty() && malformed.legacyRecords(0).empty(),
            "malformed v2 profile is ignored without partial state");
}
}
int main(int argc,char** argv) {
    GradingBoundaries();Progression();OriginalRecords();WeakKeys();
    // The caller supplies an isolated scratch profile; never open a player's save.
    if (argc>1) Persistence(argv[1]);
    std::puts("PASS: original Drill grades, units, progression, records, weakness and safe legacy migration");
}
