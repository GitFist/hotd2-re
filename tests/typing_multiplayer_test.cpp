#include "game/world_internal.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace {
void Require(bool ok,const char* why) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
std::string Input(std::string_view text,size_t progress=0) {
    std::string result;
    for(size_t i=progress;i<text.size();++i)if(text[i]!=' ')result+=text[i];
    return result;
}
void Step(game::World& world,const std::array<game::WorldInput,2>& inputs={}) {
    world.SetInput(0,inputs[0]);world.SetInput(1,inputs[1]);world.Step();
}
game::WorldConfig Config(bool second=true) {
    game::WorldConfig c;
    c.portFeatures=c.portTyping=c.typingOriginal=true;
    c.eventMode=0;c.scene=0;c.gameMode=6;c.gameType=0;
    c.playerState[0]=0;c.playerState[1]=second?0:9;
    c.seedCounter=12345;c.freePlay=true;c.livesSetting=5;
    return c;
}
void Start(game::World& w,const char* dir,bool second=true) {
    std::string error;const auto cfg=Config(second);
    Require(w.Load(dir,0,&error),error.c_str());w.Boot(cfg);
    Require(w.StartStage(cfg,&error),error.c_str());
}
void CutsceneCommand(const char* dir, int skipper) {
    game::World world;
    Start(world, dir, skipper == 1);
    auto type = [&](std::string_view p1, std::string_view p2 = {}) {
        std::array<game::WorldInput,2> inputs;
        inputs[0].typingText = p1;
        inputs[1].typingText = p2;
        Step(world, inputs);
    };
    auto sceneOpen = [&] { return world.Hud().skippable && !world.Hud().canShoot; };
    for (int frame = 0; frame < 2400 && !sceneOpen(); ++frame) type("sk", "sk");
    Require(sceneOpen(), "Chapter 1 opens a scripted cutscene skip window");
    Require(world.Hud().typingCanSkip, "the Typing skip hint opens with the cutscene");
    std::printf("Chapter 1 cutscene accepts SKIP at frame %d for P%d\n", world.Frame(), skipper + 1);
    type("ip", "ip");
    Require(!world.Hud().skipping, "letters before the cutscene cannot complete SKIP");
    if (skipper == 1) {
        type("sk", "ip");
        Require(!world.Hud().skipping, "two players cannot pool parts of SKIP");
        type("x");
    } else {
        type("", "SKIP");
        Require(!world.Hud().skipping, "an inactive second player cannot skip");
    }
    auto command = [&](std::string_view letters) {
        if (skipper == 0) type(letters); else type("", letters);
    };
    command("sKx");
    command("ip");
    Require(!world.Hud().skipping, "a mistyped command does not skip the scene");
    command("S");
    command("ki");
    Require(sceneOpen() && !world.Hud().skipping, "partial SKIP leaves the cutscene playing");
    command("P");
    for (int i = 0; i < 3 && !world.Hud().skipping; ++i) Step(world);
    Require(world.Hud().skipping, "either active player can skip the actual Chapter 1 cutscene");
    Require(!world.Hud().typingCanSkip, "the hint disappears once the cutscene is being skipped");
    for (int p = 0; p < 2; ++p) {
        const auto stats = world.PortTypingStats(p);
        Require(stats.errors == 0 && stats.correctCharacters == 0, "cutscene input does not count as combat typing");
    }
    if (skipper != 0) return;
    for (int i = 0; i < 4000; ++i) {
        const auto prompts = world.PortTypingPrompts(0);
        const auto selected = std::find_if(prompts.begin(), prompts.end(), [](const auto& p) { return p.selected; });
        if (selected != prompts.end()) {
            const auto before = world.PortTypingStats(0);
            type("SKIP");
            const auto after = world.PortTypingStats(0);
            Require(!world.Hud().skipping &&
                    after.correctCharacters + after.errors > before.correctCharacters + before.errors,
                    "SKIP typed during combat is evaluated against the selected enemy word");
            return;
        }
        const auto target = std::find_if(prompts.begin(), prompts.end(), [](const auto& p) { return !p.waiting; });
        if (target != prompts.end()) type(target->text.substr(0, 1)); else Step(world);
    }
    Require(false, "skipping the intro resumes normal Chapter 1 typing combat");
}
void Same(const game::World& a,const game::World& b) {
    Require(a.Frame()==b.Frame()&&a.GameMode()==b.GameMode()&&a.RandState()==b.RandState(),"replay frame, mode and RNG stay equal");
    Require(a.Events().script==b.Events().script&&a.Events().scene==b.Events().scene&&
        a.Events().camera.camId==b.Events().camera.camId&&a.Events().camera.tick==b.Events().camera.tick,"replay event and camera state stay equal");
    for(int p=0;p<2;++p) {
        const auto as=a.PortTypingStats(p),bs=b.PortTypingStats(p);
        Require(as.correctCharacters==bs.correctCharacters&&as.errors==bs.errors&&as.completedWords==bs.completedWords&&
            as.points==bs.points&&as.perfectStreak==bs.perfectStreak&&as.lifeGauge==bs.lifeGauge,"both player typing controllers replay deterministically");
        const auto& ap=a.Hud().player[p];const auto& bp=b.Hud().player[p];
        Require(ap.state==bp.state&&ap.lives==bp.lives&&ap.score==bp.score&&ap.hits==bp.hits&&ap.shots==bp.shots,"both player native damage and score state replay deterministically");
        const auto av=a.PortTypingPrompts(p),bv=b.PortTypingPrompts(p);
        Require(av.size()==bv.size(),"replay exposes the same per-player prompt count");
        for(size_t i=0;i<av.size();++i)Require(av[i].serial==bv[i].serial&&av[i].token==bv[i].token&&av[i].text==bv[i].text&&
            av[i].progress==bv[i].progress&&av[i].selected==bv[i].selected,"replay exposes the same words, selections and progress");
    }
    const auto ae=a.Enemies(true),be=b.Enemies(true);
    Require(ae.size()==be.size(),"replay has the same live enemy count");
    for(size_t i=0;i<ae.size();++i)Require(ae[i].serial==be[i].serial&&ae[i].hp==be[i].hp&&ae[i].behaviour==be[i].behaviour&&
        ae[i].sub==be[i].sub&&ae[i].flags34==be[i].flags34&&ae[i].target==be[i].target,"replay preserves enemy damage, state and target ownership");
}
void OwnedAxe(bool simultaneous) {
    game::World::Impl w;
    w.cfg.portTyping=true;w.gameMode=6;w.canShoot=1;
    for(auto& p:w.player){p.state=5;p.lives=3;}
    w.runner=std::make_unique<game::EventRunner>(w.mem,w,&w.env,&w.exe);
    game::EnemyTask johnny;johnny.anim.kind=0x13;johnny.variant=7;johnny.target=1;
    johnny.anim.nodes[5].model=0x1ECE;johnny.anim.nodes[5].hit[2]=-10;
    w.SpawnThrownWeapon(johnny,5);
    game::ActorTask* axe=nullptr;
    for(auto* t:w.ts.Live())if(auto* a=dynamic_cast<game::ActorTask*>(t->data.get());a&&a->fn==0x0045A4F0)axe=a;
    Require(axe!=nullptr,"real Johnny spawn creates an axe");
    axe->viewPos[2]=-10;w.RegisterTarget(axe);w.TypingStep();
    Require(w.typing.prompts().size()==1&&w.typing2.prompts().size()==1,"both controllers receive a registered projectile");
    const auto first=w.typing.prompts().front(),second=w.typing2.prompts().front();
    Require(first.serial==second.serial&&first.token==second.token&&first.text==second.text,
            "one projectile presents the identical word to P1 and P2");
    w.typingCharacters2=Input(second.text);
    if(simultaneous)w.typingCharacters=w.typingCharacters2;
    w.TypingStep();
    const auto p1=w.typing.stats().completedWords,p2=w.typing2.stats().completedWords;
    Require(p1+p2==1,"one projectile awards one completion, even for simultaneous matching input");
    if(!simultaneous)Require(p2==1,"P2 projectile completion leaves P1 statistics unchanged");
    const int winner=p2?1:0;
    Require((axe->flags34&6u)==(2u<<winner)&&axe->typingPlayer==winner&&axe->hitNode[winner]==1,
            "only the winner delivers the native player hit flag and node");
    Require(w.typingPlayer==0,"P2 processing restores controller context");
}
void JoinAndContinue(const char* dir) {
    game::World::Impl w;
    Require(w.exe.Load((std::filesystem::path(dir)/"Hod2.exe").string()),"join fixture loads native player state tables");
    w.cfg.portTyping=true;w.cfg.typingOriginal=true;w.cfg.livesSetting=3;
    w.cfg.freePlay=true;w.gameMode=6;w.flags5900=3;w.subState=2;
    w.runner=std::make_unique<game::EventRunner>(w.mem,w,&w.env,&w.exe);
    w.runner->camera.modeCur=2;
    w.ResetTyping();w.player[0].state=5;w.player[0].lives=3;w.player[1].state=9;
    game::Task task;task.data=std::make_unique<game::PlayerTask>();
    auto& data=task.As<game::PlayerTask>();data.index=1;data.fn=game::fn::kPlayerIdle;
    w.pressed=0x80000;w.TaskPlayer(task);w.pressed=0;
    Require(w.player[1].state==3,"P2 Start enters the real late-join state");
    w.TaskPlayer(task);
    Require(w.player[1].state==5&&w.player[1].lives==3,"late join initializes P2 as playing with configured lives");
    w.typing.addBasePoints(700);w.typing2.addBasePoints(1000);
    const auto before0=w.typing.stats().points,before1=w.typing2.stats().points;
    w.player[1].state=4;w.player[1].lives=0;data.fn=game::fn::kPlayerContInit;
    w.TaskPlayer(task);w.pressed=0x80000;w.TaskPlayer(task);w.pressed=0;
    Require(w.player[1].state==1,"P2 Start accepts its native Continue prompt");
    w.TaskPlayer(task);
    Require(w.player[1].state==5&&w.player[1].lives==3,"P2 Continue resumes with configured lives");
    Require(w.typing.stats().points==before0&&w.typing2.stats().points+150==before1,"P2 Continue charges only P2 typing score");
}
void CampaignAndReplay(const char* dir) {
    game::World a,b;Start(a,dir);Start(b,dir);
    bool selected=false,cancelled=false,sawBoth=false;
    uint64_t beforePartialP1=0;
    unsigned completions[2]={};
    for(int frame=0;frame<7000&&(completions[0]<3||completions[1]<3||!cancelled);++frame) {
        std::array<game::WorldInput,2> inputs;
        const auto p0=a.PortTypingPrompts(0),p1=a.PortTypingPrompts(1);
        sawBoth|=!p0.empty()&&!p1.empty();
        if(!selected) {
            const auto prompt=std::find_if(p1.begin(),p1.end(),[](const auto& p){return !p.waiting&&!p.projectile&&Input(p.text).size()>2;});
            if(prompt!=p1.end()){
                inputs[1].typingText=Input(prompt->text).substr(0,1);selected=true;
                beforePartialP1=a.PortTypingStats(0).correctCharacters;
            } else {
                // The source intro contains one-key flying threats before the
                // first ordinary word. Play them so the fixture reaches that encounter.
                const int p=frame&1;const auto& prompts=p?p1:p0;
                const auto first=std::find_if(prompts.begin(),prompts.end(),[](const auto& target){return !target.waiting;});
                if(first!=prompts.end())inputs[p].typingText=Input(first->text,first->progress);
            }
        } else if(!cancelled) {
            const auto selectedP2=std::find_if(p1.begin(),p1.end(),[](const auto& p){return p.selected&&p.progress>0;});
            Require(selectedP2!=p1.end(),"P2 can select and partially type an actual campaign word");
            const auto sameP1=std::find_if(p0.begin(),p0.end(),[&](const auto& p){
                return p.serial==selectedP2->serial&&p.token==selectedP2->token;
            });
            Require(sameP1!=p0.end()&&sameP1->text==selectedP2->text&&sameP1->progress==0,
                    "the same campaign enemy has the same word and independent progress on both controllers");
            Require(a.PortTypingStats(0).correctCharacters==beforePartialP1,"P2 partial typing does not consume P1 characters");
            inputs[1].typingCancel=true;cancelled=true;
        } else {
            const int p=frame&1;
            const auto& prompts=p?p1:p0;
            const auto chosen=std::find_if(prompts.begin(),prompts.end(),[](const auto& prompt){return !prompt.waiting;});
            if(chosen!=prompts.end())inputs[p].typingText=Input(chosen->text,chosen->progress);
        }
        const auto before0=a.PortTypingStats(0),before1=a.PortTypingStats(1);
        for(int p=0;p<2;++p)if(a.Hud().player[p].state==4)inputs[p].pressed=inputs[p].held=8;
        Step(a,inputs);Step(b,inputs);Same(a,b);
        for(int p=0;p<2;++p) {
            const auto before=p?before1:before0,after=a.PortTypingStats(p);
            completions[p]+=unsigned(after.completedWords-before.completedWords);
            if(inputs[p].typingText.empty()&&!inputs[p].typingCancel)
                Require(after.completedWords==before.completedWords,"each player's words advance only from that player's input");
            Require(a.Hud().player[p].shots==0,"two-player typing never fires native gunshots");
        }
        if(inputs[1].typingCancel) {
            const auto prompts=a.PortTypingPrompts(1);
            Require(std::none_of(prompts.begin(),prompts.end(),[](const auto& p){return p.selected;}),"P2 cancellation clears its selection");
        }
    }
    if(!(sawBoth&&selected&&cancelled&&completions[0]>=3&&completions[1]>=3))
        std::fprintf(stderr,"co-op fixture: frame %d camera %X/%d P1 %u words (%llu errors, state %d), P2 %u words (%llu errors, state %d), selection %d/%d both %d\n",
            a.Frame(),a.Events().camera.camId,a.Events().camera.tick,completions[0],static_cast<unsigned long long>(a.PortTypingStats(0).errors),a.Hud().player[0].state,
            completions[1],static_cast<unsigned long long>(a.PortTypingStats(1).errors),a.Hud().player[1].state,selected,cancelled,sawBoth);
    Require(sawBoth&&selected&&cancelled&&completions[0]>=3&&completions[1]>=3,"both players independently finish real campaign words");
    Require(a.Hud().player[0].score>0&&a.Hud().player[1].score>0,"both campaign players receive their own native score");
    game::World late;Start(late,dir,false);
    for(int i=0;i<4000&&late.PortTypingPrompts(0).empty();++i)Step(late);
    Require(!late.PortTypingPrompts(0).empty(),"late-join fixture reaches an active authored encounter");
    std::array<game::WorldInput,2> join;join[1].held=join[1].pressed=8;Step(late,join);
    bool sharedAfterJoin=false;
    for(int i=0;i<600&&!sharedAfterJoin;++i) {
        Step(late);
        if(late.Hud().player[1].state!=5)continue;
        const auto host=late.PortTypingPrompts(0);
        for(const auto& guest:late.PortTypingPrompts(1)) {
            const auto same=std::find_if(host.begin(),host.end(),[&](const auto& p){
                return p.serial==guest.serial&&p.token==guest.token;
            });
            if(same==host.end())continue;
            Require(same->text==guest.text,"late join reuses the active target's word");
            sharedAfterJoin=true;
        }
    }
    Require(sharedAfterJoin,"P2 joins the live campaign and sees the same encounter as P1");
}
}
int main(int argc,char** argv) {
    if(argc<2){std::fprintf(stderr,"usage: typing_multiplayer_test <native-game-dir>\n");return 2;}
    CutsceneCommand(argv[1],0);CutsceneCommand(argv[1],1);
    OwnedAxe(false);OwnedAxe(true);JoinAndContinue(argv[1]);CampaignAndReplay(argv[1]);
    std::puts("PASS: shared campaign words, independent progress, single hit ownership, cancellation, late join, Continue and deterministic replay");
}
