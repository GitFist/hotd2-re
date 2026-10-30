#include "game/world.h"
#include "game/world_typing_drill_game.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {
using game::TypingDrillGamePhase;
using game::TypingDrillEndReason;

struct ReferenceEnvironment {
    bool hadValue=false;
    std::string saved;
    explicit ReferenceEnvironment(const char* bank) {
        const char* prior=std::getenv("HOTD2_TYPING_REFERENCE");
        hadValue=prior!=nullptr;saved=prior?prior:"";
#ifdef _WIN32
        _putenv_s("HOTD2_TYPING_REFERENCE",bank);
#else
        setenv("HOTD2_TYPING_REFERENCE",bank,1);
#endif
    }
    ~ReferenceEnvironment() {
#ifdef _WIN32
        _putenv_s("HOTD2_TYPING_REFERENCE",saved.c_str());
#else
        if(hadValue)setenv("HOTD2_TYPING_REFERENCE",saved.c_str(),1);
        else unsetenv("HOTD2_TYPING_REFERENCE");
#endif
    }
};

struct Run {
    game::World& world;
    int exercise;
    bool eligible=false,boundTarget=false,movingTarget=false,removedTarget=false;
    bool checkpoint=false,retry=false,stageGeometry=false,leaper65=false,leaper30=false;
    bool partialHit=false,partialHpPreserved=false;
    uint64_t maximumWords=0;
    std::set<uint32_t> wordActors,completedActors;
    std::map<std::tuple<uint32_t,int,int>,std::array<float,3>> firstPose;
    std::deque<std::string> evidence;
    int priorVariant=-1,priorPhase=-1;

    [[noreturn]] void Fail(const std::string& message) const {
        const auto& s=world.TypingDrillGame();const auto& e=world.Events();
        std::fprintf(stderr,"FAIL exercise %d (%s): %s\nframe=%d phase=%d scene=%d variant=%d script=%08X camera=%04X:%d live=%d words=%d hits=%d misses=%d reason=%d\n",
            exercise,game::TypingDrillController::exercises()[size_t(exercise)].name,message.c_str(),world.Frame(),int(s.phase),
            e.scene,e.variant,e.script,e.camera.camId,e.camera.tick,world.LiveEnemies(),s.completedWords,s.objectHits,s.barrelsPassed,int(s.endReason));
        const auto stats=world.PortTypingStats();
        std::fprintf(stderr,"courseCount=%d failed=%d\n",s.courseCount,s.failed);
        std::fprintf(stderr,"input correct=%llu errors=%llu words=%llu checkpoint=%d retry=%d moving=%d removed=%d\n",
            static_cast<unsigned long long>(stats.correctCharacters),static_cast<unsigned long long>(stats.errors),
            static_cast<unsigned long long>(stats.completedWords),checkpoint,retry,movingTarget,removedTarget);
        for(const auto& line:evidence)std::fprintf(stderr,"  %s\n",line.c_str());
        for(const auto& p:world.PortTypingPrompts())
            std::fprintf(stderr,"  prompt %u token=%08X progress=%zu selected=%d waiting=%d flags=%08X text=%s\n",p.serial,p.token,p.progress,p.selected,p.waiting,p.flags,p.text.c_str());
        const auto enemies=world.Enemies(true);
        for(size_t i=0;i<std::min<size_t>(enemies.size(),5);++i) {
            const auto& a=enemies[i];
            std::fprintf(stderr,"  actor %u kind=%d hp=%d behaviour=%d/%d motion=%04X:%d pos=%.2f,%.2f,%.2f\n",
                a.serial,a.kind,a.hp,a.behaviour,a.sub,a.motion,a.tick,a.pos[0],a.pos[1],a.pos[2]);
        }
        std::fflush(stderr);std::exit(1);
    }
    void Require(bool value,const std::string& message) const {if(!value)Fail(message);}
    void Note(std::string value) {
        if(evidence.empty()||evidence.back()!=value) {
            evidence.push_back(std::move(value));
            if(evidence.size()>10)evidence.pop_front();
        }
    }
    void Observe(uint32_t completingSerial=0,uint64_t beforeWords=0) {
        const auto& s=world.TypingDrillGame();const auto& e=world.Events();
        Require(!e.error,"event VM: "+e.errorText);
        Require(!e.camera.error,"authored camera program has an unsupported command");
        for(const auto& note:world.Notes()) {
            Note(note);
            Require(note.find("not ported")==std::string::npos&&note.find("not evaluated")==std::string::npos&&
                    note.find("unreadable")==std::string::npos&&note.find("event runner:")==std::string::npos,
                    "authored runtime reached an unresolved operation: "+note);
        }
        Require(world.GameMode()==6&&world.Hud().gameType==2,"source Drill remains in its isolated native game type");
        Require(world.Hud().player[0].shots==0&&!world.Hud().player[0].fired,
                "physical typing and instruction confirmation cannot emit a gun shot");
        Require(world.Hud().menuPanels.empty()&&world.Hud().menuText.empty(),"real exercise has no modern controller-panel fallback");
        for(float value:world.Camera().view.m)Require(std::isfinite(value),"source camera matrix remains finite");
        if(e.variant!=priorVariant||int(s.phase)!=priorPhase) {
            char text[160];std::snprintf(text,sizeof(text),"frame %d phase %d variant %d camera %04X:%d words %d",world.Frame(),int(s.phase),e.variant,e.camera.camId,e.camera.tick,s.completedWords);
            Note(text);priorVariant=e.variant;priorPhase=int(s.phase);
        }
        for(const auto& op:world.EventOps()) {
            checkpoint|=op.opcode==0x70;retry|=op.opcode==0x71;
        }
        if(checkpoint)Require(e.typingCheckpoint.script!=0,"checkpoint stores an executable data address");
        std::set<uint32_t> live,leapers;
        for(const auto& task:world.Tasks()) {
            live.insert(task.serial);
            if(task.fn==0x00449910)leapers.insert(task.serial);
        }
        if(exercise==12)for(const auto& actor:world.Enemies(true))if(leapers.count(actor.serial)) {
            leaper65|=actor.hp==65;
            leaper30|=actor.hp==30;
        }
        const auto stats=world.PortTypingStats();
        maximumWords=std::max(maximumWords,stats.completedWords);
        if(completingSerial&&stats.completedWords>beforeWords)completedActors.insert(completingSerial);
        for(uint32_t serial:completedActors)if(!live.count(serial))removedTarget=true;
        for(const auto& prompt:world.PortTypingPrompts()) {
            if(prompt.waiting||(prompt.flags&0x10000u)||prompt.progress>=prompt.text.size())continue;
            eligible=true;wordActors.insert(prompt.serial);
            boundTarget|=live.count(prompt.serial)!=0;
        }
        for(const auto& draw:world.Draws()) {
            stageGeometry|=draw.cull&&!draw.viewSpace;
            if(draw.cull||!wordActors.count(draw.task)||(draw.viewSpace&&draw.node<0))continue;
            std::array<float,3> point={draw.world.m[12],draw.world.m[13],draw.world.m[14]};
            if(draw.viewSpace) {
                // AnimDraw submits its real skeleton nodes as model-view
                // matrices. Recover their world origins before comparing;
                // HUD/poster submissions have no node and are excluded above.
                const auto& inverse=world.Camera().inverse;
                std::array<float,3> worldPoint{};
                for(int axis=0;axis<3;++axis)
                    worldPoint[size_t(axis)]=point[0]*inverse.m[axis]+point[1]*inverse.m[4+axis]+
                        point[2]*inverse.m[8+axis]+inverse.m[12+axis];
                point=worldPoint;
            }
            for(float v:point)Require(std::isfinite(v),"authored target geometry remains finite");
            const auto key=std::make_tuple(draw.task,draw.model,draw.node);
            const auto [at,inserted]=firstPose.emplace(key,point);
            if(!inserted) {
                float distance=0;
                for(int axis=0;axis<3;++axis)distance+=std::abs(point[size_t(axis)]-at->second[size_t(axis)]);
                if(distance>.02f)movingTarget=true;
            }
        }
    }
    void Step(std::string text={},uint32_t buttons=0,uint32_t serial=0) {
        const auto before=world.PortTypingStats().completedWords;
        const auto beforeCorrect=world.PortTypingStats().correctCharacters;
        int partialHp=-1;
        // Prompt.flags belongs to the dictionary entry. Identify a native
        // ordinary zombie by its task, whose source post has per-key hits.
        const auto tasks=world.Tasks();
        const bool ordinary=std::any_of(tasks.begin(),tasks.end(),[&](const auto& task) {
            return task.serial==serial&&task.fn==0x004533F0;
        });
        if(text.size()==1&&serial&&ordinary)for(const auto& prompt:world.PortTypingPrompts()) {
            if(prompt.serial!=serial||prompt.progress>=prompt.text.size()||
               text[0]!=prompt.text[prompt.progress])continue;
            size_t next=prompt.progress+1;
            while(next<prompt.text.size()&&prompt.text[next]==' ')++next;
            if(next>=prompt.text.size())continue;
            for(const auto& actor:world.Enemies())if(actor.serial==serial)partialHp=actor.hp;
        }
        game::WorldInput input;
        input.kind=0;input.x=320;input.y=240;
        input.pressed=input.held=buttons;input.typingText=std::move(text);
        world.SetInput(0,input);world.SetInput(1,{});world.Step();
        if(partialHp>0&&world.PortTypingStats().correctCharacters>beforeCorrect&&
           world.PortTypingStats().completedWords==before) {
            for(const auto& actor:world.Enemies())if(actor.serial==serial) {
                Require(actor.hp==partialHp,"an accepted partial key cannot damage its native actor's HP");
                partialHpPreserved=true;
                partialHit|=(actor.flags34&0x1000u)!=0;
            }
        }
        Observe(serial,before);
    }
};

char Fold(unsigned char value) {return value>='A'&&value<='Z'?char(value+'a'-'A'):char(value);}

const game::PortTypingPrompt* Choose(const std::vector<game::PortTypingPrompt>& prompts,const std::set<uint32_t>& enemies,bool enemyOnly) {
    const auto usable=[&](const auto& p) {
        return !p.waiting&&!(p.flags&0x10000u)&&p.progress<p.text.size()&&(!enemyOnly||enemies.count(p.serial));
    };
    for(const auto& p:prompts)if(p.selected&&usable(p))return &p;
    for(const auto& p:prompts) {
        if(!usable(p))continue;
        // Native selection is by the first key. Do not accidentally select a
        // citizen with the same initial while testing the enemy in Reflex2.
        if(enemyOnly&&std::any_of(prompts.begin(),prompts.end(),[&](const auto& other) {
            return !enemies.count(other.serial)&&!other.waiting&&other.progress<other.text.size()&&
                Fold(other.text[other.progress])==Fold(p.text[p.progress]);
        }))continue;
        return &p;
    }
    return nullptr;
}

void Exercise(game::World& world,const game::TypingDrillController& fixture,int exercise) {
    auto profile=fixture;
    Run run{world,exercise};
    run.Require(profile.beginOriginalAttempt(exercise),"profile unlocks a real original attempt");
    std::string error;
    const bool started=world.StartTypingDrill(exercise,profile,0x44524C00u+unsigned(exercise),&error);
    run.Require(started,"public StartTypingDrill: "+error);
    run.Observe();
    run.Require(world.Events().mode==7&&world.Events().cfg.gameType==2&&world.PortTypingActive(),
                "public entry loads the original mode7 VM and native typing actors");
    run.Require(world.TypingDrillGame().exercise==exercise&&world.Events().scene==world.TypingDrillGame().sourceScene,
                "supervisor and authored scene agree on the selected exercise");
    for(int ticks=0;ticks<600&&!world.TypingDrillGame().supervisorSerial;++ticks)run.Step();
    run.Require(world.TypingDrillGame().supervisorSerial!=0&&world.TypingDrillGame().phase==TypingDrillGamePhase::Instructions,
                "source actor6D owns an actual instruction phase");
    for(int ticks=0;ticks<3;++ticks)run.Step();
    run.Require(world.TypingDrillGame().phase==TypingDrillGamePhase::Instructions&&world.PortTypingPrompts().empty(),
                "instructions wait for confirmation and block actor typing");
    run.Step({},4);run.Step();
    run.Require(world.TypingDrillGame().phase==TypingDrillGamePhase::Ready,"physical confirmation enters source READY/GO timing");
    int ready=0;
    while(world.TypingDrillGame().phase==TypingDrillGamePhase::Ready&&ready<170){run.Step();++ready;}
    run.Require(world.TypingDrillGame().phase==TypingDrillGamePhase::Running&&world.TypingDrillGame().readyTicks==160,
                "source READY/GO lasts160 ticks before opening the actor input window");
    run.Require(world.Events().flags[0xF0]&&world.Events().flags[0xF1],"native supervisor releases the authored scene through F0/F1");

    const bool quota=exercise==0||exercise==2||exercise==7||exercise==12;
    const int limit=exercise==12?24000:exercise==6?7800:12000;
    int firstPromptAt=-1;
    bool injectedError=false;
    for(int frame=0;frame<limit&&!world.TypingDrillGame().finished;++frame) {
        const auto prompts=world.PortTypingPrompts();
        std::set<uint32_t> enemySerials;
        if(exercise==4)for(const auto& actor:world.Enemies())enemySerials.insert(actor.serial);
        const auto* prompt=Choose(prompts,enemySerials,exercise==4);
        if(prompt&&firstPromptAt<0)firstPromptAt=frame;
        const bool warmed=firstPromptAt>=0&&frame-firstPromptAt>=24;
        bool keepTyping=quota||world.PortTypingStats().completedWords<3;
        // Keep the real boards alive long enough to exercise retry71, then
        // let five barrels pass their authored boundary to terminate the game.
        if(exercise==3&&!run.retry)keepTyping=true;
        std::string key;
        uint32_t serial=0;
        if(prompt&&warmed&&frame%2==0&&keepTyping) {
            serial=prompt->serial;
            if((exercise==6||exercise==8)&&prompt->selected&&prompt->progress>0&&!injectedError) {
                char wrong='~';
                for(int candidate=33;candidate<127;++candidate) {
                    const bool used=std::any_of(prompts.begin(),prompts.end(),[&](const auto& p) {
                        return p.progress<p.text.size()&&Fold(p.text[p.progress])==Fold(static_cast<unsigned char>(candidate));
                    });
                    if(!used){wrong=char(candidate);break;}
                }
                key.assign(1,wrong);injectedError=true;
            } else key.assign(1,prompt->text[prompt->progress]);
        }
        run.Step(std::move(key),0,serial);
    }
    const auto& state=world.TypingDrillGame();const auto result=world.TypingDrillOutcome();
    run.Require(state.finished&&state.phase==TypingDrillGamePhase::Ended,"authored quota, timeout, attack or miss finishes within the frame bound");
    run.Require(state.endReason!=TypingDrillEndReason::None&&world.Events().flags[0xF2],"native completion closes the authored course through F2");
    run.Require(run.eligible&&run.boundTarget&&run.stageGeometry,"source stage produces eligible words bound to live native actor tasks");
    run.Require(run.movingTarget,"an actual word target moves or animates in world space");
    run.Require(result.original&&std::isfinite(result.seconds)&&result.seconds>0&&result.metric>=0,
                "outcome is the original course metric from the real running state");
    if(exercise!=8)run.Require(run.maximumWords>0,"real physical typing completes at least one authored actor word");
    if(exercise==0||exercise==2||exercise==4||exercise==6||exercise==7||exercise==12)
        run.Require(run.removedTarget,"a completed word reaches native actor destruction rather than only changing a controller count");
    switch(exercise) {
    case 0:
        run.Require(run.partialHit&&run.partialHpPreserved,"Speed1 delivers native partial-key feedback while preserving HP until word completion");
        run.Require(state.completedWords==30&&state.endReason==TypingDrillEndReason::CourseComplete,"Speed1 clears exactly30 real enemy words");break;
    case 1:run.Require(state.objectHits>0&&state.damageCount>0&&state.endReason==TypingDrillEndReason::Damage,"Speed2 staggers the live mount before its real attack ends the drill");break;
    case 2:run.Require(state.completedWords==10&&!state.failed&&state.endReason==TypingDrillEndReason::CourseComplete,"Speed3 clears all ten authored targets");break;
    case 3:run.Require(state.objectHits>0&&state.barrelsPassed>=5&&state.endReason==TypingDrillEndReason::MissedTarget&&run.retry&&
                result.assignedWords==0,"Reflex1 retries its checkpoint, counts no file words, and ends on five actual missed barrels");break;
    case 4:run.Require(state.reflexWaves>0&&state.endReason==TypingDrillEndReason::MissedTarget,"Reflex2 executes native shuffled waves and ends when a real wave is missed");break;
    case 5:run.Require(state.endReason==TypingDrillEndReason::MissedTarget,"Reflex3 ends when its moving coin falls under gravity");break;
    case 6:run.Require(injectedError&&result.errors==1&&state.mistypeCount==1&&state.completedWords>=3&&
                state.elapsedTicks==7200&&state.endReason==TypingDrillEndReason::TimeUp,
                "Accuracy1 discards a mistyped target, continues with other zombies, and uses its exact120second source clock");break;
    case 7:run.Require(state.endReason==TypingDrillEndReason::CourseComplete&&state.completedWords==10&&
                world.PortTypingStats().perfectStreak==10&&world.PortTypingStats().lifeThreshold==150&&
                world.Hud().player[0].lives==1,"Accuracy2 clears its50-point gauge on the tenth perfect word, then restores one life");break;
    case 8:run.Require(injectedError&&result.errors==1&&state.endReason==TypingDrillEndReason::Mistype&&
                std::accumulate(world.TypingDrillKeyWeights().begin(),world.TypingDrillKeyWeights().end(),uint64_t(0))>0,
                "Accuracy3's actual mistype ends the drill and records its physical weak key");break;
    case 9:case 10:case 11:run.Require(state.endReason==TypingDrillEndReason::Damage&&state.damageCount>0,"SpecialKeys ends through an authored enemy attack after input stops");break;
    case 12:
        run.Require(run.leaper65&&run.leaper30,"FinalExam's100HP leapers retain two nonlethal35HP word hits before dying");
        run.Require(state.courseCount>=100&&!state.failed&&state.endReason==TypingDrillEndReason::CourseComplete,"FinalExam reaches its real100target authored course quota");break;
    default:break;
    }
    run.Require(!state.resultsReady,"the metric latch precedes the source Finish and authored cleanup interval");
    int finishing=0;bool finishCaption=false;
    while(!world.TypingDrillGame().resultsReady&&finishing<1800) {
        run.Step();++finishing;
        for(const auto& sprite:world.Hud().sprites)finishCaption|=sprite.id==0x4BD2;
    }
    const auto after=world.TypingDrillOutcome();
    run.Require(world.TypingDrillGame().resultsReady&&finishing>=181&&finishCaption,
                "source Finish caption and authored cleanup complete before opening the result book");
    run.Require(after.metric==result.metric&&after.correct==result.correct&&after.errors==result.errors&&
                after.assignedWords==result.assignedWords&&after.completedWords==result.completedWords,
                "result counters remain frozen throughout the original Finish and camera cleanup");
    std::printf("PASS drill %2d: ticks=%5d words=%3d hits=%3d reason=%d finish=%d checkpoint=%d/%d\n",exercise,state.elapsedTicks,state.completedWords,state.objectHits,int(state.endReason),finishing,run.checkpoint,run.retry);
    std::fflush(stdout);
}
}

int main(int argc,char** argv) {
    if(argc<3||argc>4) {
        std::fprintf(stderr,"usage: typing_drill_runtime_test <native-game-directory-or-exe> <prepared-reference-bank> [exercise0..12]\n");
        return 2;
    }
    std::filesystem::path native(argv[1]);if(native.has_extension())native=native.parent_path();
    game::World world;std::string error;
    {
        ReferenceEnvironment environment(argv[2]);
        if(!world.Load(native.string(),0,&error)){std::fprintf(stderr,"FAIL public World.Load: %s\n",error.c_str());return 1;}
    }
    game::TypingDrillController fixture;
    for(int exercise=0;exercise<12;++exercise) {
        game::TypingDrillResult cleared;cleared.original=true;
        cleared.metric=game::TypingDrillOriginalLowerIsBetter(exercise)?1:100000;
        fixture.recordOriginalResult(exercise,cleared);
        if(!fixture.clears()[size_t(exercise)]){std::fprintf(stderr,"FAIL original profile fixture exercise%d\n",exercise);return 1;}
    }
    if(!fixture.unlocked(12)){std::fprintf(stderr,"FAIL original FinalExam unlock fixture\n");return 1;}
    if(argc==4) {
        const int exercise=std::atoi(argv[3]);
        if(exercise<0||exercise>12)return 2;
        Exercise(world,fixture,exercise);
    } else for(int exercise=0;exercise<13;++exercise)Exercise(world,fixture,exercise);
    std::puts("typing_drill_runtime_test: OK");
}
