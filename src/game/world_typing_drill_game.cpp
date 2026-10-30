#include "game/world_typing_drill_game.h"
#include "game/world_internal.h"
#include "game/world_held.h"
#include "game/world_typing_drill_result_visuals.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>

namespace game {

// Private actor state adapters; their native task functions retain ownership
// of the original retreat/removal animation after the source6D transition.
bool TypingDrillFinishBat(ActorTask& actor);
bool TypingDrillFinishFrog(ActorTask& actor);

namespace {

constexpr std::array<int,13> kScenes={25,9,21,5,17,19,11,15,13,1,7,23,3};
constexpr std::array<int,13> kMenuIndex={0,1,2,6,7,8,3,4,5,9,10,11,12};
constexpr std::array<int,13> kInstructionOffset={10,11,12,4,5,6,0,1,2,7,8,9,3};
constexpr std::array<int,13> kIcons={0xEB2,0xEB3,0xEB4,0xEAA,0xEAB,0xEAC,0xEAF,0xEB0,0xEB1,0xEB6,0xEB7,0xEB8,0xEB5};
constexpr uint32_t kResult=0x009A2234; // Source A0C910, translated by the authored event importer.
constexpr std::string_view kSpeed3Characters="1234567890-^\\!\"#$%&'()=~|`{+*}<>?_,./;:]@[";

struct DrillTask : WTask {
    int finalPhase=0,goTicks=0,specialSelector=-1;
    int endingTicks=0,finishTicks=0;
    int wordPollFrame=-1;
    TypingDrillEndReason pendingEnd=TypingDrillEndReason::None;
    bool cleanupReady=false,penaltyPending=false;
    int penaltyState=0,penaltyCounter=0,penaltySeconds=0;
    int penaltyAdvancedFrame=-1;
    float penaltyAlpha=0;
    std::map<uint32_t,int> missFlash;
};

DrillTask* Supervisor(World::Impl& w);
bool SourceModelIn(World::Impl& w,int model,int first,int last);

void Sprite(World::Impl& w,int id,float x,float y,float depth=.1f,uint32_t flags=5,float sx=1,float sy=1,float alpha=1) {
    SpriteDef d;
    d.id=0x4000+id;d.x=x;d.y=y;d.depth=depth;d.flags=flags;
    d.sx=sx;d.sy=sy;d.alpha=alpha;d.source=0x004A6170;
    w.hudSprites.push_back(d);
}

void Number(World::Impl& w,int value,int digits,int first,float x,float y,float spacing) {
    value=std::max(0,value);
    int divisor=1;
    for(int i=1;i<digits;++i)divisor*=10;
    for(int i=0;i<digits;++i) {
        Sprite(w,first+(value/divisor)%10,x+i*spacing,y);
        divisor=std::max(1,divisor/10);
    }
}

// Source4A891C: the live exercise timer is a 60-Hz count, not a WPM clock.
void Timer(World::Impl& w,int ticks) {
    ticks=std::clamp(ticks,0,215999);
    Number(w,ticks/3600,2,0xE97,220,16,24);
    Sprite(w,0xEA5,268,16);
    Number(w,ticks%3600/60,2,0xE97,292,16,24);
    Sprite(w,0xEA6,340,16);
    Number(w,ticks%60*100/60,2,0xE97,364,16,24);
}

// Original counter variants7..12 (4A863F) and13..17 (4A873A).
void Count(World::Impl& w,int count,int icon,bool remaining=false) {
    count=std::clamp(count,0,999);
    if(remaining) {
        Sprite(w,0xE80,232,76);Number(w,count,3,0xE72,285,75,24);
        Sprite(w,0xE7F,365,75);Sprite(w,icon,382,75);
    } else {
        Number(w,count,3,0xE97,279,16,25);
        if(icon>=0){Sprite(w,0xEA4,362,34);Sprite(w,icon,380,34);}
    }
}

void Countdown(World::Impl& w,int frames) {
    // Counter18 has seconds art in place of the target multiplication mark.
    Sprite(w,0xE80,232,76);Sprite(w,0xE81,364,76);
    Number(w,std::max(0,(frames+59)/60),3,0xE72,285,75,24);
    if(frames>0&&frames<=121&&(frames+59)%60==0)w.TypingSound(0x000A00A9);
}

void Instructions(World::Impl& w) {
    const auto& s=w.typingDrillGame;
    const int offset=kInstructionOffset[size_t(s.exercise)];
    //4A5E3C calls49FEF0 before the instruction sheet, as in the carousel.
    for(int i=0;i<20;++i)Sprite(w,0xE83+i,float(i%5)*128,float(i/5)*128,200,0);
    // Source5E4AB8's {sprite,x,y} list; the title, icon and instruction
    // entries are patched by4A8240 for the selected original menu index.
    struct Item {int id,x,y;};
    const Item items[]={
        {0x2A5+offset,-40,69},{0x2B3,377,65},{kIcons[size_t(s.exercise)],377,65},
        {0x27B+offset,64,144},{0xB13,134,288},{0x29F,263,287},{0x26B,70,287},{0x263,193,417}
    };
    float depth=1;
    for(const auto& item:items)Sprite(w,item.id,float(item.x),float(item.y),depth++);
    Sprite(w,0x2B4,403,67,3.5f,5,25.5f,15.75f);
    Sprite(w,0x26C,34,135,3.5f,5,23,1);
    Sprite(w,0x28A,8,200,4.5f,5,78,9.75f);
    if(kMenuIndex[size_t(s.exercise)]<12) {
        Sprite(w,0x2B2,260,349,5.5f,5,15.625f,1);
        Sprite(w,0x4AC,511,295,6.5f);
    }
    DrawTypingDrillInstructionCriteria(w,s.exercise);
}

void WordPreference(World::Impl& w) {
    w.typing.setDrillSpecialKeyWeights(w.typingDrillGame.exercise==10||w.typingDrillGame.exercise==11);
    if(w.typingDrill.controller.supplementary())
        w.typing.setWordCharacterPreference(1,w.typingDrill.controller.supplementaryKeys());
    else if(w.typingDrillGame.exercise==2)
        w.typing.setWordCharacterPreference(2,kSpeed3Characters);
    else w.typing.setWordCharacterPreference(0,{});
}

void Clock(World::Impl& w) {
    auto& s=w.typingDrillGame;
    if(s.phase==TypingDrillGamePhase::Running)
        s.elapsedTicks=std::clamp(w.frame-s.runStartFrame,0,215999);
}

void End(World::Impl& w,TypingDrillEndReason reason,bool failed=false) {
    auto& s=w.typingDrillGame;
    if(s.finished)return;
    Clock(w);
    s.objectHits=std::clamp(int(w.player[w.typingPlayer].hits),0,999);
    //4A67E6..6805 corrects the final timer only when no attacker remains.
    if(s.exercise==7&&w.attackers==0)s.elapsedTicks=std::max(0,s.elapsedTicks-1);
    if(s.exercise==0) {
        int applied=s.penaltyTicks,remaining=0;
        if(auto* d=Supervisor(w)) {
            // The source samples its clock before98D0 advances the offset.
            // A quota reached on that same tick uses the earlier sample.
            if(d->penaltyAdvancedFrame==w.frame)applied-=60;
            remaining=d->penaltySeconds*60;
        }
        s.elapsedTicks=std::min(215999,s.elapsedTicks+applied);
        s.penaltyTicks=remaining; //4A6AA3 adds the still pending seconds.
    }
    s.failed=failed;s.endReason=reason;
    s.outcome=TypingDrillGameSnapshotResult(w);
    s.finished=true;s.phase=TypingDrillGamePhase::Ended;
    if(auto* d=Supervisor(w))d->fn=0x004A6D20;
    w.canShoot=0;w.typing.cancelSelection();
    w.typing.setWordCharacterPreference(0,{});
    if(w.runner) {
        auto& r=*w.runner;
        r.flags[0xF2]=1;
        if(failed)w.runner->globals[kResult]=1;
        else if(reason==TypingDrillEndReason::CourseComplete)w.runner->globals[kResult]=0;
        //4A6C2E..6CA5: drain the gameplay camera, then run the lesson's own
        // ending variant. Its F5 flag gates the results page.
        auto& c=r.camera;
        if(c.endTick!=c.playTick)c.endTick=c.playTick;
        c.SetMode(1,3);c.writeIdx=c.readIdx=0;c.cmd=CameraDirector::Cmd::Nop;
        c.pending=0;c.cmdState=1;c.forceState6=true;
        r.yield=false;r.variant=s.endVariant;r.script=s.endScript;
    }
    w.TypingSound(0x80000000);
}

DrillTask* Supervisor(World::Impl& w) {
    for(Task* t:w.ts.Live())if(t->serial==w.typingDrillGame.supervisorSerial)
        return dynamic_cast<DrillTask*>(t->data.get());
    return nullptr;
}

void Penalty(World::Impl& w,DrillTask& d) {
    //4A98D0: a hit first displays +5 seconds for90 ticks, then advances the
    // clock one second every three ticks while the caption fades out.
    if(d.penaltyState==0) {
        if(d.penaltyPending) {
            d.penaltyPending=false;d.penaltyState=1;d.penaltyAlpha=1;
            d.penaltyCounter=90;d.penaltySeconds=5;
        }
    } else if(d.penaltyState==1) {
        if(--d.penaltyCounter==0)d.penaltyState=2;
    } else {
        d.penaltyAlpha=d.penaltyAlpha>=0?d.penaltyAlpha-.05f:0;
        if(++d.penaltyCounter==3) {
            d.penaltyCounter=0;
            if(d.penaltySeconds>0) {
                w.typingDrillGame.penaltyTicks+=60;--d.penaltySeconds;
                d.penaltyAdvancedFrame=w.frame;
            }
            else {d.penaltyAlpha=0;d.penaltyState=0;}
        }
    }
    if(d.penaltyAlpha>=0) {
        Sprite(w,0xBA9,277,150,.1f,9,1,1,d.penaltyAlpha);
        Sprite(w,0xBA8,295,150,.1f,9,1,1,d.penaltyAlpha);
        Sprite(w,0xBAA,320,150,.1f,9,1,1,d.penaltyAlpha);
    }
}

void ReleaseWord(World::Impl& w,ActorTask& actor) {
    actor.typingFlags|=0x400;
    for(Task* t:w.ts.Live())if(t->data.get()==&actor) {
        w.typingCachedTargets.erase(t->serial);
        break;
    }
}

void FinishActors(World::Impl& w,DrillTask& d) {
    auto& s=w.typingDrillGame;
    const bool accuracy3=s.sourceScene==13&&w.runner&&w.runner->camera.tick<91;
    const bool force=!d.cleanupReady&&(s.sourceScene==1||s.sourceScene==3||
                     s.sourceScene==7||s.sourceScene==21||s.sourceScene==23);
    if(!accuracy3&&!force)return;
    w.TypingAttackTimer(2);
    if(force)w.player[w.typingPlayer].hitTimer=300;
    //4A72EB/4A73AC visit the14 registered source word-actor slots, including
    // actors whose active flag was cleared by their completed death motion.
    for(void* slot:w.actorSlots) {
        auto* actor=static_cast<ActorTask*>(slot);
        if(!actor||actor->typingFlags==0)continue;
        ReleaseWord(w,*actor);
        auto* enemy=dynamic_cast<EnemyTask*>(actor);
        if(accuracy3&&enemy&&enemy->anim.kind==6&&enemy->behaviour==2) {
            enemy->behaviour=3;enemy->target=int8_t(w.typingPlayer);enemy->sub=0;
        }
        if(!force||d.finishTicks<30)continue;
        if(TypingDrillFinishBat(*actor)||TypingDrillFinishFrog(*actor))continue;
        if(auto* held=dynamic_cast<HeldTask*>(actor)) {
            if(SourceModelIn(w,held->model,0x271,0x278))held->state=3;
            else if(SourceModelIn(w,held->model,0x207E,0x207E))held->state=6;
            continue;
        }
        if(!enemy)continue;
        //444F00 releases a living member of the shared approach group.
        if(enemy->c131B==1) {
            if(w.GroupCount()==1)w.TypingSound(enemy->anim.kind==2?0x004E17A9:0x002025A9);
            enemy->c131B=0;--w.GroupCount();
        }
        if(enemy->anim.kind==0x19) {
            if(enemy->behaviour!=6){enemy->behaviour=6;enemy->flags136C|=0x200000;}
        } else if(enemy->anim.kind==0x1D)enemy->behaviour=6;
        else if(enemy->behaviour!=6) {
            enemy->behaviour=10;enemy->flags136C|=0x80000000;enemy->sub=0;
        }
    }
}

void Finish(World::Impl& w,DrillTask& d) {
    auto& s=w.typingDrillGame;
    if(d.fn==0x004A6D20) {
        if(++d.endingTicks!=5)return;
        d.finishTicks=0;d.cleanupReady=false;w.flags5900|=0x10;
        w.TypingSound(0x10000025);
        if(w.runner)w.runner->flags[0xF4]=1;
        d.fn=0x004A7280;
        return;
    }
    ++d.finishTicks;
    Sprite(w,0xBD2,320,210,.1f,0xA);
    FinishActors(w,d);
    if(!d.cleanupReady&&w.runner&&w.runner->flags[0xF5]==1) {
        d.cleanupReady=true;w.flags5900|=0x20;
    }
    if(d.finishTicks>180&&d.cleanupReady) {
        s.resultsReady=true;
        if(w.typingDrill.controller.supplementary()&&w.runner)w.runner->flags[0xF6]=1;
    }
}

int StepRank(int value,const int* limits,const int* ranks,int count,int last) {
    for(int i=0;i<count;++i)if(value<=limits[i])return ranks[i];
    return last;
}

void Rank(World::Impl& w) {
    const auto& s=w.typingDrillGame;
    const int n=s.completedWords,h=s.objectHits;
    switch(s.exercise) {
    case 0:w.rank=n<=3?4:n<=7?6:n<=22?8:10;break; //4A6A2C
    case 8:w.rank=n<=12?2:n<=30?4:n<=60?6:n<=90?10:n<=120?12:15;
        if(n>210)w.TypingAttackTimer(0);break; //4A6714
    case 9: {
        constexpr int limits[]={16,32,48,64,80,96,112,128,144,160,172,184,196,208};
        constexpr int ranks[]={0,2,2,4,4,6,6,8,8,10,11,12,13,14};
        w.rank=StepRank(h,limits,ranks,14,15);break; //4A6215
    }
    case 10:w.rank=h<=9?6:h<=18?8:h<=30?10:h<=42?12:h<=50?6:h<=60?14:h<=70?15:h<=80?8:((h-80)%30<=20?15:8);
        if(h>80&&(h-80)%30<=20)w.TypingAttackTimer(0);break; //4A6551
    case 11:w.rank=n<=8?0:n<=16?4:n<=40?8:n<=50?10:n<=60?12:15;break; //4A6985
    case 12:w.rank=0;break;
    default:break;
    }
    w.typing.setDynamicRank(w.rank);
}

bool SourceModelIn(World::Impl& w,int model,int first,int last) {
    for(int source=first;source<=last;++source)
        if(model==w.typingReference.SourceModel(source))return true;
    return false;
}

int PendingAxes(World::Impl& w) {
    // Source4AA3A0 counts unthrown hand attachments and live axe props.
    int count=0;
    for(Task* task:w.ts.Live()) {
        if(auto* e=dynamic_cast<EnemyTask*>(task->data.get())) {
            if(e->behaviour==7&&(e->anim.kind==0x13||e->anim.kind==0x14)) {
                const int m5=w.typingReference.NativeEnemyModel(e->anim.kind,5,e->anim.nodes[5].model);
                const int m8=w.typingReference.NativeEnemyModel(e->anim.kind,8,e->anim.nodes[8].model);
                if(e->anim.kind==0x13) {
                    count+=SourceModelIn(w,m5,0x221F,0x2223);
                    count+=SourceModelIn(w,m8,0x2217,0x221B);
                } else {
                    count+=SourceModelIn(w,m5,0x2251,0x2254);
                    count+=SourceModelIn(w,m8,0x224A,0x224D);
                }
            }
        } else if(auto* held=dynamic_cast<HeldTask*>(task->data.get())) {
            if(held->state!=3)count+=SourceModelIn(w,held->model,0x271,0x278);
        }
    }
    return count;
}

void AxePacing(World::Impl& w) {
    if(!w.runner)return;
    uint32_t op=0,arg=0;
    if(!w.mem.Read(w.runner->script,&op)||!w.mem.Read(w.runner->script+4,&arg))return;
    const int hits=w.typingDrillGame.objectHits;
    // Source4AA290: the late axe patterns share authored camera holds.
    if(op==0x41&&int(arg)==w.runner->camera.tick) {
        if((arg==0x117||arg==0x1A3)&&hits>=160) {
            w.runner->script+=24;w.runner->yield=false;
        } else if(arg==0x123||arg==0x1AF) {
            const float step=hits<64?0.f:hits<160?20.f:40.f;
            for(Task* task:w.ts.Live())if(auto* e=dynamic_cast<EnemyTask*>(task->data.get())) {
                if(e->anim.kind==0x13)e->pos[0]+=step;
                else if(e->anim.kind==0x14)e->pos[0]-=step;
            }
        }
    } else if(op==0x42&&arg==255&&w.runner->waitCounter>60)
        w.runner->waitCounter=PendingAxes(w)?255:60;
}

void FinalCount(World::Impl& w,DrillTask& d) {
    auto& s=w.typingDrillGame;
    // Source4A6415..64D8 counts both ordinary words and the short-key waves.
    switch(d.finalPhase) {
    case 0:s.courseCount=s.completedWords;
        if(s.courseCount==4){s.lastEnemies=w.liveEnemies-1;d.finalPhase=1;}break;
    case 1:if(w.liveEnemies<s.lastEnemies)s.courseCount+=s.lastEnemies-w.liveEnemies;
        s.lastEnemies=w.liveEnemies;if(s.courseCount==8)d.finalPhase=2;break;
    case 2:s.courseCount=s.completedWords-8;
        if(s.courseCount==62){s.lastEnemies=w.liveEnemies-1;d.finalPhase=3;}break;
    case 3:if(s.courseCount==s.completedWords)d.finalPhase=0;
        else {if(w.liveEnemies<s.lastEnemies)s.courseCount+=s.lastEnemies-w.liveEnemies;s.lastEnemies=w.liveEnemies;}break;
    }
    s.courseCount=std::clamp(s.courseCount,0,999);
}

void Run(World::Impl& w,DrillTask& d) {
    auto& s=w.typingDrillGame;
    Clock(w);
    s.objectHits=std::clamp(int(w.player[w.typingPlayer].hits),0,999);
    s.completedWords=std::min(s.completedWords,999);
    s.barrelsPassed=std::min(s.barrelsPassed,999);
    Rank(w);
    const int status=w.GlobalInt(kResult,-1);
    switch(s.exercise) {
    case 0:Timer(w,s.elapsedTicks+s.penaltyTicks);Count(w,30-s.completedWords,0xE82,true);Penalty(w,d);
        if(s.completedWords>=30)End(w,TypingDrillEndReason::CourseComplete);break;
    case 1:Count(w,s.objectHits,-1);
        if(s.objectHits>70)w.TypingAttackTimer(1);
        if(s.objectHits>80)w.TypingAttackTimer(0);break;
    case 2:Count(w,10-s.completedWords,0xE82,true);
        if(s.completedWords>=10)End(w,TypingDrillEndReason::CourseComplete);break;
    case 3:Count(w,s.objectHits,0xEA2);Count(w,5-s.barrelsPassed,0xE7D,true);
        if(s.barrelsPassed>=5)End(w,TypingDrillEndReason::MissedTarget);break;
    case 4:Count(w,s.completedWords,0xEA7);
        if(status==1||status==2||d.pendingEnd==TypingDrillEndReason::MissedTarget) {
            if(status==2||d.pendingEnd==TypingDrillEndReason::MissedTarget)
                s.completedWords=std::max(0,s.completedWords-1);
            End(w,TypingDrillEndReason::MissedTarget);
        }break;
    case 5:Timer(w,s.elapsedTicks); //Coin432BA9 calls4A8580, the same original timer.
        if(status==1)End(w,TypingDrillEndReason::MissedTarget);break;
    case 6:Count(w,s.completedWords,0xEA7);Countdown(w,7200-s.elapsedTicks);
        if(s.elapsedTicks>=7200)End(w,TypingDrillEndReason::TimeUp);break;
    case 7:Timer(w,s.elapsedTicks);
        if(w.player[w.typingPlayer].lives>1)End(w,TypingDrillEndReason::CourseComplete);break;
    case 8:break; //4A6714 uses the original perfect gauge and mistake feedback.
    case 9:Count(w,s.objectHits,0xEA1);AxePacing(w);break;
    case 10:Count(w,s.objectHits,0xEA3);break;
    case 11: {
        Count(w,s.completedWords,0xEA7);
        if(w.player[w.typingPlayer].hits>0&&w.typing.selectedSerial()==0)++s.courseCount;
        w.player[w.typingPlayer].hits=0; //4A69E4..6A0A: completed-wave bookkeeping.
        uint32_t op=0,arg=0;
        if(w.runner&&w.typing.assignedWordCount()==w.typing.stats().completedWords&&
           w.mem.Read(w.runner->script,&op)&&w.mem.Read(w.runner->script+4,&arg)&&
           op==0x42&&arg==20&&w.runner->waitCounter==20) {
            const int n=s.completedWords;
            const int selector=n<=16?200:n<=24?300:n<=40?400:n<=50?500:n<=60?600:700;
            if(d.specialSelector!=selector){w.typing.setWordSelection(-1,selector);d.specialSelector=selector;}
        }
        break;
    }
    case 12:Timer(w,s.elapsedTicks);FinalCount(w,d);Count(w,100-s.courseCount,0xE82,true);
        if(s.courseCount>=100)End(w,TypingDrillEndReason::CourseComplete);break;
    }
    //4A6AD4 runs after the course-specific test. Accuracy2 observes the
    // awarded life first; every active lesson then restores its single life.
    if(w.player[w.typingPlayer].state==5)
        w.player[w.typingPlayer].lives=w.player[w.typingPlayer].livesShown=1;
    if(!s.finished&&d.pendingEnd!=TypingDrillEndReason::None)
        End(w,d.pendingEnd,d.pendingEnd==TypingDrillEndReason::Damage&&(s.exercise==2||s.exercise==12));
    if(!s.finished&&w.runner&&w.runner->flags[0xF2])
        End(w,status>0?TypingDrillEndReason::ScriptFailure:TypingDrillEndReason::CourseComplete,
            status!=0&&(s.exercise==2||s.exercise==12));
}

void Frame(Task& task) {
    auto& w=*CurrentWorld();auto& s=w.typingDrillGame;auto& d=task.As<DrillTask>();
    if(!TypingDrillGameActive(w)||s.resultsReady||w.paused)return;
    if(s.phase==TypingDrillGamePhase::Ended){Finish(w,d);return;}
    for(auto flash=d.missFlash.begin();flash!=d.missFlash.end();) {
        const bool ended=--flash->second<=0;
        for(Task* actorTask:w.ts.Live())if(actorTask->serial==flash->first)
            if(auto* actor=dynamic_cast<ActorTask*>(actorTask->data.get()))actor->typingFrameColor=ended?1.f:0.f;
        if(ended)flash=d.missFlash.erase(flash);else ++flash;
    }
    if(s.phase==TypingDrillGamePhase::Instructions) {
        const bool supplementary=w.typingDrill.controller.supplementary();
        if(!supplementary) {
            Instructions(w);
            if(!(w.pressed&(0xCu<<(16*w.typingPlayer))))return;
            w.TypingSound(0x000121A9);
        }
        s.phase=TypingDrillGamePhase::Ready;s.readyTicks=0;d.fn=0x004A6050;
        w.flags5900&=~0x10u;
        if(w.runner)w.runner->flags[0xF0]=1;
        WordPreference(w);
        return;
    }
    if(s.phase==TypingDrillGamePhase::Ready) {
        ++s.readyTicks;
        if(s.readyTicks==10)w.TypingSound(0x20000140);
        if(s.readyTicks>10&&s.readyTicks<90)Sprite(w,0xBD3,346,210,.1f,0xA);
        if(s.readyTicks==100)w.TypingSound(0x2000013A);
        if(s.readyTicks>100&&s.readyTicks<160)Sprite(w,0xBD0,346,210,.1f,0xA);
        if(s.readyTicks==160) {
            s.phase=TypingDrillGamePhase::Running;s.runStartFrame=w.frame;s.elapsedTicks=0;
            s.startingCorrect=w.typing.stats().correctCharacters;s.startingErrors=w.typing.stats().errors;
            w.rank=1+4*std::max(1,w.playersInGame);w.rankTimerOn=true;w.rankTimer=1;w.rankAdj=0;
            w.typing.setDynamicRank(w.rank);w.canShoot=1;d.fn=0x004A6170;
            if(w.runner){w.runner->flags[0xF1]=1;w.runner->globals[kResult]=uint32_t(-1);}
        }
        return;
    }
    if(s.phase==TypingDrillGamePhase::Running) {
        Run(w,d);
        if(s.exercise==2) {
            int sprite=0;float alpha=0,lastX=0;
            //4A9A35 visits the authored14 slots in order. Only the final
            // active non-countdown actor's x drives the attack-delay floor.
            for(void* slot:w.actorSlots)if(auto* e=dynamic_cast<EnemyTask*>(static_cast<ActorTask*>(slot))) {
                if(!(e->flags34&1)||e->typingFlags==0)continue;
                if(e->behaviour==14) {
                    const int ticks=e->counter1330;
                    if(ticks==179||ticks==119||ticks==59)w.TypingSound(0x000A00A9);
                    if(ticks>0){sprite=0xB8E+(ticks-1)/60;alpha=float((ticks-1)%60)/60;}
                    else d.goTicks=60;
                } else {
                    if(d.goTicks>0){sprite=0xBD0;alpha=float(--d.goTicks)/60;}
                    lastX=e->pos[0];
                }
            }
            if(lastX>435)w.TypingAttackTimer(3);
            if(sprite&&sprite<=0xB90)Sprite(w,sprite,320,150,1,0xA,1,1,alpha);
        }
    }
}

} // namespace

void ConfigureTypingDrillGame(World::Impl& w,int exercise) {
    w.typingDrillGame={};
    auto& s=w.typingDrillGame;
    s.active=true;s.exercise=std::clamp(exercise,0,12);s.sourceScene=kScenes[size_t(s.exercise)];
    s.phase=TypingDrillGamePhase::Instructions;
    w.typing.setPerfectLifeGauge(s.exercise==7,s.exercise==7||s.exercise==4?50:400);
    WordPreference(w);
}

bool SpawnTypingDrillGame(World::Impl& w,Task& task,SpawnTask& spawn) {
    auto& s=w.typingDrillGame;
    if(!TypingDrillGameActive(w)||spawn.rec.type!=0x6D)return false;
    auto data=std::make_unique<DrillTask>();data->fn=0x004A5E00;
    if(spawn.op!=0x0A){s.endScript=uint32_t(spawn.rec.ParamS32(0));s.endVariant=spawn.rec.ParamS16(4);}
    s.supervisorSerial=task.serial;s.phase=TypingDrillGamePhase::Instructions;
    w.player[w.typingPlayer].lives=w.player[w.typingPlayer].livesShown=1;
    w.player[w.typingPlayer].hits=0;w.flags5900|=0x10;w.canShoot=0;
    w.typing.setPerfectLifeGauge(s.exercise==7,s.exercise==7||s.exercise==4?50:400);
    task.data=std::move(data);task.fn=Frame;
    WordPreference(w);
    return true;
}

bool TypingDrillGameActive(const World::Impl& w){return w.cfg.portTyping&&w.cfg.eventMode==7&&w.cfg.gameType==2&&w.typingDrillGame.active;}
bool TypingDrillGameRunning(const World::Impl& w){return TypingDrillGameActive(w)&&w.typingDrillGame.phase==TypingDrillGamePhase::Running;}
bool TypingDrillGameFinished(const World::Impl& w){return TypingDrillGameActive(w)&&w.typingDrillGame.finished;}
bool TypingDrillGameResultsReady(const World::Impl& w){return TypingDrillGameActive(w)&&w.typingDrillGame.resultsReady;}

void TypingDrillGameBeforeTargets(World::Impl& w) {
    if(!TypingDrillGameActive(w))return;
    const int exercise=w.typingDrillGame.exercise;
    for(Task* task:w.ts.Live())if(auto* actor=dynamic_cast<ActorTask*>(task->data.get())) {
        if(!(actor->flags34&1)||actor->typingFlags==0)continue;
        if(exercise==4)actor->typingFlags|=0x8800; //4A6815 preserves civilian words and awards no attack.
        if(exercise==2) {
            actor->typingFlags|=0x4000;
            if(const auto* enemy=dynamic_cast<const EnemyTask*>(actor)) {
                if(enemy->behaviour==14)actor->typingFlags|=0x10200;
                else actor->typingFlags&=~0x10200u;
            }
        }
    }
}

void TypingDrillGameWordComplete(World::Impl& w,const TypingCompletion& completion) {
    if(!TypingDrillGameRunning(w))return;
    auto& s=w.typingDrillGame;
    // Source484120 counts only graded words; one-key hits have grade6 which
    // is folded to the no-grade bucket. Actor flag08000000 suppresses both.
    const unsigned grade=unsigned(completion.rank);
    if(grade<5){++s.completedWords;++s.rankCounts[grade];s.perfectPoints=s.completedWords;}
}

void TypingDrillGameMistype(World::Impl& w,uint32_t serial) {
    if(!TypingDrillGameRunning(w)||serial==0)return;
    auto& s=w.typingDrillGame;++s.mistypeCount;
    if(s.exercise==6||s.exercise==8) {
        s.mistypedTargets.insert(serial);
        for(Task* task:w.ts.Live())if(task->serial==serial)
            if(auto* actor=dynamic_cast<ActorTask*>(task->data.get())) {
                actor->typingFlags|=0x400;actor->typingFrameColor=0;
                if(s.exercise==6)actor->flags34|=0x08000000;
            }
        w.typingCachedTargets.erase(serial);w.typing.cancelSelection();
        if(s.exercise==8)if(auto* d=Supervisor(w))d->pendingEnd=TypingDrillEndReason::Mistype;
    } else if(s.exercise==7) {
        //4A97A0 flashes the target but keeps its word in Accuracy2.
        if(auto* supervisor=Supervisor(w))supervisor->missFlash[serial]=16;
    }
}

void TypingDrillGameDamage(World::Impl& w,int player) {
    if(!TypingDrillGameRunning(w)||player!=w.typingPlayer)return;
    auto& s=w.typingDrillGame;++s.damageCount;
    w.player[player].lives=std::max(1,int(w.player[player].lives));
    w.player[player].livesShown=std::max(1,int(w.player[player].livesShown));
    if(s.exercise==0){if(auto* d=Supervisor(w))d->penaltyPending=true;return;}
    //6D observes the cumulative accepted-damage counter on its next task
    // tick. Reflex2/3 are terminated by their authored actor result instead.
    if(s.exercise==1||s.exercise==2||s.exercise==8||s.exercise>=9)
        if(auto* d=Supervisor(w))d->pendingEnd=TypingDrillEndReason::Damage;
}

void TypingDrillGameCivilianHit(World::Impl& w) {
    if(!TypingDrillGameRunning(w)||w.typingDrillGame.exercise!=4)return;
    //4A6834 excludes the mistakenly completed citizen word on the next6D
    // tick, after the source actor has reported result2.
    if(w.runner)w.runner->globals[kResult]=2;
    if(auto* d=Supervisor(w))d->pendingEnd=TypingDrillEndReason::MissedTarget;
}

void TypingDrillGameTypedKey(World::Impl& w,unsigned char expected,bool error) {
    if(!TypingDrillGameRunning(w)||!error||expected==' '||expected>=249)return;
    //401BE6/4013DE add the error surcharge after the unconditional poll's
    // +1. Reusing noteOriginalKey(error=true) would add that base twice.
    auto weights=w.typingDrill.controller.originalKeyWeights();
    weights[expected]=uint16_t(std::min(65535u,unsigned(weights[expected])+20u));
    w.typingDrill.controller.setOriginalKeyWeights(weights);
}

void TypingDrillGameWordPoll(World::Impl& w) {
    if(!TypingDrillGameRunning(w)||w.paused||w.typingPlayer!=0)return;
    auto* d=Supervisor(w);
    if(!d||d->wordPollFrame==w.frame)return;
    d->wordPollFrame=w.frame;
    for(const auto& prompt:w.typing.prompts()) {
        const auto target=w.typingCachedTargets.find(prompt.serial);
        if(target==w.typingCachedTargets.end()||(target->second.first.flags&0x10000))continue;
        if(prompt.progress<prompt.text.size())
            w.typingDrill.controller.noteOriginalKey(static_cast<unsigned char>(prompt.text[prompt.progress]),false);
    }
}

TypingDrillResult TypingDrillGameSnapshotResult(const World::Impl& w) {
    const auto& s=w.typingDrillGame;const auto& stats=w.typing.stats();
    if(s.finished)return s.outcome;
    const auto& metrics=w.typing.runMetrics();
    TypingDrillResult result;
    result.assignedWords=w.typing.assignedBookWordCount();result.completedWords=stats.completedWords;
    result.reactionFrames=metrics.reactionFrames;result.reactions=metrics.reactions;
    result.speedFrames=metrics.speedFrames;result.speedCharacters=metrics.speedCharacters;
    result.original=true;result.failed=s.failed;result.words=unsigned(std::max(0,s.completedWords));
    result.correct=unsigned(std::min<uint64_t>(stats.correctCharacters-std::min(stats.correctCharacters,s.startingCorrect),std::numeric_limits<unsigned>::max()));
    result.errors=unsigned(std::min<uint64_t>(stats.errors-std::min(stats.errors,s.startingErrors),std::numeric_limits<unsigned>::max()));
    result.missed=unsigned(std::max(0,s.barrelsPassed));result.seconds=double(s.elapsedTicks+s.penaltyTicks)/60;
    switch(s.exercise) {
    case 0:result.metric=s.elapsedTicks+s.penaltyTicks;break;
    case 1:result.metric=int32_t(s.rankCounts[0]+s.rankCounts[1]);break;
    case 2: {
        result.metric=s.failed||!metrics.speedCharacters?0:int32_t(std::min<long double>(
            std::round(static_cast<long double>(metrics.speedFrames)*1000/metrics.speedCharacters),std::numeric_limits<int32_t>::max()));
        break;
    }
    case 3:case 9:case 10:result.metric=s.objectHits;break;
    case 5:case 12:result.metric=s.elapsedTicks;break;
    case 7:result.metric=s.elapsedTicks;break;
    default:result.metric=s.completedWords;break;
    }
    result.grade=TypingDrillOriginalGrade(s.exercise,result.metric,result.failed);
    result.evaluation=4-result.grade;result.passed=result.grade<=2;
    return result;
}

} // namespace game
