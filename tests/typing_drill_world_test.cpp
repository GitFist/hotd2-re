#include "game/world_internal.h"
#include "game/world_nightshift.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
void Require(bool ok,const char* why) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
bool HasText(const game::World::Impl& w,const std::string& text) {
    return std::any_of(w.hud.menuText.begin(),w.hud.menuText.end(),[&](const auto& draw){return draw.text==text;});
}
void Frame(game::World::Impl& w,uint32_t keys=0) {
    w.pressed=keys;w.hudSprites.clear();game::NightShiftMenuFrame(w);w.pressed=0;
}
}
int main(int argc,char** argv) {
    game::World::Impl w;
    w.gameMode=4;w.cfg.portFeatures=w.cfg.portModernMenu=true;
    if(argc>1)w.cfg.savePath=argv[1];
    w.front.nightShiftRow=7;Frame(w,8);
    for(int frame=0;frame<48;++frame)Frame(w);
    Require(w.front.typingHome&&w.front.nightShiftPage==0,"game switch enters Typing Home");
    w.front.nightShiftRow=1;Frame(w,8);
    Require(w.front.nightShiftPage==12,"Typing Home Drill entry opens its selection");
    Frame(w);
    Require(HasText(w,"TYPING OF THE DEAD / DRILL")&&!game::TypingDrillInputActive(w),"selection menu presents Drill without consuming typing input");
    w.typingDrill.selected=3;Frame(w,8);
    Require(game::TypingDrillInputActive(w),"starting Reflex enables physical-key typing route");
    auto& drill=w.typingDrill.controller;
    const size_t legacyRecordsBefore=drill.legacyRecords(3).size();
    Require(game::QueueTypingDrillText(w,"z",false)&&!w.typingDrill.characters.empty(),"world typing queue accepts keys in Drill");
    w.typingDrill.characters.clear();
    Require(game::QueueTypingDrillText(w,std::string(drill.prompt()),false),"text uses the shared world input queue");
    Frame(w);
    Require(drill.result().words>0||drill.progress()>0,"front-end frame consumes the queued typing");
    // This asset-free front-end fixture does not simulate an authored scene.
    // Feed its real raw Reflex1 outcome through the same completion API as the child World.
    game::TypingDrillResult outcome;
    outcome.metric=40;outcome.words=40;outcome.correct=80;outcome.errors=2;outcome.seconds=60;
    drill.recordOriginalResult(3,outcome);
    w.typingDrill.view=2;w.typingDrill.resultRow=0;
    Require(drill.finished()&&drill.result().original&&drill.result().grade==2&&drill.result().passed&&
            drill.clears()[3]&&drill.unlocked(4),"original C-grade Reflex result unlocks its next level");
    Require(std::any_of(drill.records(3).begin(),drill.records(3).end(),[](const auto& row) {
                return row.original&&row.metric==40&&row.grade==2;
            })&&drill.history(3).front()==2&&drill.legacyRecords(3).size()==legacyRecordsBefore,
            "world completion keeps original metric and grade history separate from legacy scores");
    Frame(w);
    Require(HasText(w,"DRILL CLEARED")&&HasText(w,"RETRY")&&!game::TypingDrillInputActive(w),"results show outcome and return keys to menu handling");
    Frame(w,8);
    Require(game::TypingDrillInputActive(w)&&drill.result().words==0,"Enter retries with fresh exercise counters");
    Frame(w,2);
    Require(w.typingDrill.view==0&&!game::TypingDrillInputActive(w),"Escape abandons practice to drill selection");
    w.typingDrill.selected=13;Frame(w,8);w.typingDrill.record=3;Frame(w);
    Require(HasText(w,"DRILL RECORDS")&&HasText(w,"REFLEX 1"),"record browser displays the completed exercise");
    Frame(w,2);Frame(w,2);
    Require(w.front.typingHome&&w.front.nightShiftPage==0,"Escape returns from records through Drill selection to Typing Home");
    if(argc>1) {
        game::World::Impl restored;
        restored.gameMode=4;restored.cfg.savePath=argv[1];restored.front.nightShiftPage=12;
        Frame(restored);
        const auto& restoredDrill=restored.typingDrill.controller;
        Require(restoredDrill.clears()[3]&&restoredDrill.unlocked(4)&&
                std::any_of(restoredDrill.records(3).begin(),restoredDrill.records(3).end(),[](const auto& row) {
                    return row.original&&row.metric==40&&row.grade==2;
                })&&restoredDrill.legacyRecords(3).size()==legacyRecordsBefore,
                "world Drill page loads isolated original raw results and C-grade progression");
    }
    Require(!game::QueueTypingDrillText(w,"a",false),"leaving Drill releases shared input queue");
    std::puts("PASS: Drill front-end routing, keyboard queue, original raw results, retry, records and saved progression");
}
