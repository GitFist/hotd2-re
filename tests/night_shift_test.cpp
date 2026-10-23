#include <cstdio>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "game/world.h"
#include "game/world_nightshift.h"
#include "game/world_internal.h"
#include "game/save_data.h"
#include "game/menu_audio.h"
#include "play/night_shift_menu.h"
#include "play/local_leaderboard.h"
static void Require(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static void CompletionGuards() {
 game::World::Impl w;w.frontEnd=true;w.cfg.portFeatures=true;w.players=1;w.player[0].state=5;w.player[1].state=9;w.portRunOriginEligible=true;
 game::PortLeaderboardBegin(w);
 game::PortLeaderboardComplete(w,3,1200,2);Require(w.portLeaderboardRun.sequence==0,"recording is opt-in at port boundary");
 w.portLeaderboardRecording=true;w.portRunOriginEligible=false;w.cfg.eventMode=5;w.player[0].score=1000;
 game::PortLeaderboardComplete(w,0,0,6);Require(w.portLeaderboardRun.sequence==0,"chapter-six Practice completion remains excluded after temporary selection flags clear");
 w.portRunOriginEligible=true;w.front.sceneSelect=true;
 game::PortLeaderboardComplete(w,3,1200,2);Require(w.portLeaderboardRun.sequence==0,"chapter-selected run excluded");
 w.front.sceneSelect=false;w.players=2;
 game::PortLeaderboardComplete(w,3,1200,2);Require(w.portLeaderboardRun.sequence==0,"two-player boss time excluded");
 w.portRunParticipants=2;w.players=1;
 game::PortLeaderboardComplete(w,3,1200,2);Require(w.portLeaderboardRun.sequence==0,"two-player boss remains excluded after one player dies");
 w.portRunParticipants=1;game::PortLeaderboardComplete(w,3,1200,2);
 Require(w.portLeaderboardRun.sequence==1&&w.portLeaderboardRun.score==1200&&w.portLeaderboardRun.table==3,"native boss time snapshot");
 game::PortLeaderboardComplete(w,3,1200,2);Require(w.portLeaderboardRun.sequence==1,"completion cannot duplicate a single run");
 w.portRunRecorded=false;w.player[0].score=900;w.player[1].state=9;w.player[1].score=90000;w.players=0;w.portRunParticipants=2;
 game::PortLeaderboardComplete(w,0,0,5);Require(w.portLeaderboardRun.sequence==2&&w.portLeaderboardRun.score==900,"unjoined player's old score cannot leak into a new run");
 Require(w.portLeaderboardRun.players==2,"game-over preserves two-player participation after both players die");
 w.portRunRecorded=false;w.portRunJoined[1]=true;w.player[0].state=9;w.player[0].score=91000;w.player[1].state=5;w.player[1].score=800;
 game::PortLeaderboardComplete(w,1,0,6);
 Require(w.portLeaderboardRun.sequence==3&&w.portLeaderboardRun.score==91000&&w.portLeaderboardRun.playerScores[0]==91000&&w.portLeaderboardRun.playerScores[1]==800,
         "joined agent retains its valid score after its out timer reaches state9 while partner continues");
 w.player[0].state=5;w.player[0].score=100;w.player[1].state=9;game::PortLeaderboardBegin(w);
 Require(w.portRunJoined[0]&&!w.portRunJoined[1]&&w.portRunParticipants==1&&!w.portRunRecorded,"new game clears previous joined history and unjoined stale score");
 w.gameMode=6;w.player[0].state=9;w.player[1].state=5;w.players=1;game::PortLeaderboardPlayerJoined(w,1);
 Require(w.portRunParticipants==2&&w.portRunJoined[0]&&w.portRunJoined[1],"sequential agent joins count both participants even without simultaneous two-player state");
 game::PortLeaderboardComplete(w,3,1200,2);Require(w.portLeaderboardRun.sequence==3,"boss record excludes sequential two-agent participation");
 w.player[0].state=6;w.player[1].state=5;game::PortLeaderboardBegin(w);
 Require(!w.portRunJoined[0]&&w.portRunJoined[1]&&w.portRunParticipants==1,"new boss excludes an expired previous-run out slot");
}
static void BossReplay(const char* dir,const char* script) {
 struct Event {int frame,p;float x,y;int buttons;};std::vector<Event> events;
 std::ifstream file(script);std::string line;while(std::getline(file,line)) {
  Event e{};std::istringstream in(line);if(in>>e.frame>>e.p>>e.x>>e.y>>e.buttons)events.push_back(e);
 }
 Require(!events.empty(),"read actual boss replay");
 game::World w;Require(w.Load(dir),"load boss replay World");game::WorldConfig c;c.portFeatures=true;w.Boot(c);w.SetPortLeaderboardRecording(true);
 size_t next=0;float x[2]={.5f,.5f},y[2]={.5f,.5f};uint8_t buttons[2]={},previous[2]={};
 for(int frame=1;frame<4000;++frame) {
  while(next<events.size()&&events[next].frame<=frame) {const auto& e=events[next++];if(e.p>=0&&e.p<2){x[e.p]=e.x;y[e.p]=e.y;buttons[e.p]=uint8_t(e.buttons);}}
  for(int p=0;p<2;++p){w.SetInput(p,game::MakeWorldInput(x[p],y[p],buttons[p],previous[p]));previous[p]=buttons[p];}
  w.Step();
 }
 const auto r=w.PortLeaderboardRun();
 Require(r.eligible&&r.sequence==1&&r.table==3&&r.score>0&&r.players==1,"actual Judgment victory records one frozen new boss time");
 w.Boot(c);Require(w.PortLeaderboardRun().sequence==r.sequence,"completion sequence and snapshot survive a frontend Boot");
}
static void DirectOptionsAudio(const char* dir) {
 for(bool modern : {false,true}) {
  game::World world;Require(world.Load(dir),"load direct Options sound flow");
  game::WorldConfig cfg;cfg.portFeatures=true;cfg.portModernMenu=modern;
  world.Boot(cfg);world.ForceMode(4,0x10000,0,0);
  auto step=[&](uint32_t keys=0,bool click=false) {
   game::WorldInput in;in.kind=click?1:0;in.x=100;in.y=359;in.fired=click;in.pressed=in.held=click?4:keys;
   world.SetInput(0,in);world.SetInput(1,{});world.Step();
  };
  for(int i=0;i<3;++i)step();
  if(!modern)for(int i=0;i<5;++i){step(0x20);step();}
  const int requests=world.Hud().portDisplayRequests;
  step(modern?0:8,modern);
  const auto& sounds=world.Sounds();
  Require(sounds.size()==1&&sounds[0].kind==game::World::kOptionFeedback&&sounds[0].id==game::kOptionSelectSound,
          "Options entry emits exactly one protected Preview 74 in either theme");
  Require(world.GameMode()==4&&world.Hud().portDisplayRequests==requests+1&&world.Hud().portDisplayPage==0,
          "main Options opens the Display tab directly without entering mode12");
  Require(world.Hud().nightShiftPage!=(5)&&!world.QuitRequested(),"Options never visits the removed intermediate hub");
  for(const auto& item:world.Hud().menuText)Require(item.text!="GAME OPTIONS","main Options exposes no second Game Options page");
  step();
  Require(world.GameMode()==4&&world.Hud().portDisplayRequests==requests+1&&world.Sounds().empty(),
          "one Options selection cannot reopen repeatedly or join a player");
 }
 puts("PASS direct Options entry: both themes skip the hub and mode12 with one Preview 74");
}

static void LeaderboardPresentation(const char* dir) {
 game::World w;Require(w.Load(dir),"load leaderboard presentation World");game::WorldConfig c;c.portFeatures=c.portModernMenu=true;
 // Two real blank-name encodings, plus a valid name; scores/stages are untouched.
 c.savePatch={{0x009C97A0,{0,0,0,0}},{0x009C97B0,{' ',' ',' ',0}},{0x009C97C0,{'A','B','C',0}}};
 w.Boot(c);w.ForceMode(4,0x10000,0,0);
 auto step=[&](uint32_t pressed=0,bool click=false){game::WorldInput in;in.pressed=in.held=pressed;if(click){in.kind=1;in.x=100;in.y=260;in.fired=true;}w.SetInput(0,in);w.SetInput(1,{});w.Step();};
 for(int i=0;i<3;++i)step();step(4,true);step();
 Require(w.Hud().nightShiftPage==3&&w.Hud().nightShiftRow==0&&w.Hud().portLeaderboardTable==0,"Home opens leaderboard with source focus and Arcade current");
 auto focused=[&](int row){int red=0;bool match=false;for(const auto& p:w.Hud().menuPanels)if(p.left==24&&p.right==187&&p.color==0xED8F2026){++red;match=p.top==130+40*row;}Require(red==1&&match,"exactly one red rail button must follow keyboard focus");};
 auto currentRule=[&](int row){for(const auto& p:w.Hud().menuPanels)if(p.left==38&&p.right==173&&p.top==161+40*row&&p.color==0xFFB9AA91)return true;return false;};
 auto name=[&](int row){for(const auto& t:w.Hud().menuText)if(t.x==256&&t.y==181+23*row)return t.text;return std::string{};};
 auto values=[&]{std::vector<std::string> result;for(const auto& t:w.Hud().menuText)if((t.x==360||t.x==505)&&t.y>=181&&t.y<=400)result.push_back(t.text);return result;};
 focused(0);Require(currentRule(1),"current board needs a subtle rule distinct from the focused red source button");
 Require(name(0)=="---"&&name(1)=="---"&&name(2)=="ABC","empty/space initials need a display-only placeholder while valid initials stay intact");
 const auto originalValues=values();Require(originalValues.size()==20,"all score/stage cells retained");
 step(0x20);step();focused(1);step(0x20);step();focused(2);
 Require(w.Hud().portLeaderboardTable==0&&currentRule(1)&&!currentRule(2)&&values()==originalValues,"moving focus must not change the current board or its records");
 step(8);step();focused(2);Require(w.Hud().portLeaderboardTable==1&&!currentRule(1),"confirmation changes the actual board without a second red focus");
 step(0x10);step();focused(1);Require(currentRule(2),"Original remains current when keyboard focus returns to Arcade");
 step(8);step();Require(w.Hud().portLeaderboardTable==0&&values()==originalValues&&name(0)=="---"&&name(2)=="ABC","board navigation must preserve scores/stages and native initials");
 puts("PASS leaderboard presentation: one focus, distinct current board, empty initials and unchanged records");
}
static void HomePresentation(const char* dir) {
 game::World world;Require(world.Load(dir),"load Home panel presentation World");
 game::WorldConfig cfg;cfg.portFeatures=cfg.portModernMenu=true;world.Boot(cfg);world.ForceMode(4,0x10000,0,0);
 for(int i=0;i<3;++i){world.SetInput(0,{});world.SetInput(1,{});world.Step();}
 world.SetPortHomeRooms({{12345,false,false}},"WAITING FOR A PARTNER");world.Step();
 bool left=false,right=false;
 for(const auto& panel:world.Hud().menuPanels) {
  Require(!(panel.left==352&&panel.top==122&&panel.right==624&&panel.bottom==416),"Home online area has no enclosing dark box");
  if(panel.left==24&&panel.top==195&&panel.color==0x75101315)left=true;
  if(panel.left==366&&panel.top==166&&panel.color==0x75101315)right=true;
 }
 Require(left&&right,"Home room card and left navigation retain matching transparency");
 puts("PASS Home panel presentation with direct tabbed Options");
}
int main(int argc,char** argv) {
 if(argc<2||argc>3)return 2;
 CompletionGuards();
 DirectOptionsAudio(argv[1]);
 LeaderboardPresentation(argv[1]);
 HomePresentation(argv[1]);
 play::LocalHordeLeaderboard scores;
 game::HordeRunSnapshot run;run.active=true;run.rounds=3;run.score=500;run.players=1;
 Require(!scores.Observe(run),"live run is not finalized");
 run.rounds=4;run.score=700;run.players=2;scores.Observe(run);
 Require(scores.Observe({})&&scores.Entries().size()==1,"end records exactly one run");
 Require(scores.Entries()[0].rounds==4&&scores.Entries()[0].score==700&&scores.Entries()[0].players==2,"peak progress preserved");
 Require(!scores.Observe({}),"no duplicate on idle ticks");
 run.rounds=1;run.score=90;scores.Observe(run);scores.Observe({});
 Require(scores.Entries()[0].score==700&&scores.LastCompleted().score==90,"publishing uses the new run even below the personal best");
 run.debug=true;scores.Observe(run);Require(!scores.Finish()&&scores.Entries().size()==2,"debug runs excluded");
 const auto file=std::filesystem::temp_directory_path()/"hotd2-nightshift-horde-test.txt";
 Require(scores.Save(file.string()),"write independent Horde sidecar");
 play::LocalHordeLeaderboard loaded;Require(loaded.Load(file.string())&&loaded.Entries().size()==2,"round-trip sidecar");
 {std::ofstream f(file);f<<"HOTD2-HORDE-1\n-2 500 8 0\n";}
 Require(!loaded.Load(file.string())&&loaded.Entries().size()==2,"invalid sidecar cannot replace loaded records");
 std::error_code ec;std::filesystem::remove(file,ec);
 game::World live,control;Require(live.Load(argv[1])&&control.Load(argv[1]),"load paired Worlds");
 game::WorldConfig c;c.portFeatures=true;c.portModernMenu=true;
 live.Boot(c);control.Boot(c);live.ForceMode(4,0x10000,0,0);control.ForceMode(4,0x10000,0,0);
 live.Step();control.Step();live.Step();control.Step();
 const auto rand=live.RandState();const int frame=live.Frame();
 play::AttractPreview preview;
 Require(preview.Step(argv[1],true),preview.Error().c_str());
 Require(preview.Get()&&preview.Get()->GameMode()==3&&!preview.Get()->Draws().empty(),"full attract cycle starts with moving title scenery");
 const int cycle[]={3,9,5,11,10};
 int transition=0,previousMode=3;
 bool seenGeometry[5]={},seenSprites[5]={};
 for(int i=0;i<45000&&transition<15;++i) {
  Require(preview.Step(argv[1],true),"full original attract cycle advances");
  const auto& background=*preview.Get();
  const int mode=background.GameMode();
  if(mode!=previousMode) {
   ++transition;
   if(transition<=5)std::printf("Attract frame %d: mode %d, demo %d\n",background.Frame(),mode,background.Hud().demoIndex);
   Require(mode==cycle[transition%5],"attract preserves title, tutorial, gameplay and both story scenes in order");
   if(mode==5)Require(background.Hud().demoIndex==(transition/5)%3,"each full cycle advances to the next original gameplay demo");
   previousMode=mode;
  }
  const int part=transition%5;
  seenGeometry[part]|=!background.Draws().empty();
  seenSprites[part]|=!background.Hud().sprites.empty();
  Require(live.Hud().nightShiftPage==0&&live.GameMode()==4,"background scene changes leave the Home menu open");
 }
 Require(transition==15,"all three gameplay demos finish their complete attract cycles and return to title");
 for(int part=0;part<5;++part)Require(seenGeometry[part],"every attract scene submits live world geometry");
 Require(seenSprites[1],"how-to-play retains its instructional sprites");
 bool wrappedDemo=false;
 for(int i=0;i<2400&&!wrappedDemo;++i) {
  Require(preview.Step(argv[1],true),"attract cycle continues after all demos");
  if(preview.Get()->GameMode()==5&&preview.Get()->Hud().subState==1) {
   Require(preview.Get()->Hud().demoIndex==0,"original demo list wraps after the third full cycle");
   wrappedDemo=true;
  }
 }
 Require(wrappedDemo,"next tutorial completes and the first demo starts again");
 Require(live.RandState()==rand&&live.Frame()==frame,"preview never advances live RNG or frame counter");
 live.Step();control.Step();Require(live.RandState()==control.RandState()&&live.Frame()==control.Frame(),"live world resumes identically after preview");
 const int previewFrame=preview.Get()->Frame();preview.Step(argv[1],false);Require(!preview.Get(),"disabled preview hidden");
 preview.Step(argv[1],true,0);Require(preview.Get()->Frame()==previewFrame,"disabled preview does not run");
 std::vector<port::ModelRenderer::SpriteDraw> panels;
 play::AppendNightShiftPanels(live.Hud(),panels);
 Require(!panels.empty(),"native menu panels are rendered");
 for(size_t i=1;i<panels.size();++i)Require(panels[i].depth<panels[i-1].depth,"panel depths preserve intended compositing");
 const auto textLayout=live.Hud().menuText;
 std::vector<port::ModelRenderer::SpriteDraw> wide;
 play::AppendNightShiftPanels(live.Hud(),wide,16.f/9.f,100);
 for(int opacity:{0,25,65,100,200}) {
  std::vector<port::ModelRenderer::SpriteDraw> scaled;
  play::AppendNightShiftPanels(live.Hud(),scaled,16.f/9.f,opacity);
  Require(scaled.size()==wide.size(),"opacity must preserve every panel at a wide viewport");
  for(size_t i=0;i<scaled.size();++i) {
   Require(scaled[i].depth==wide[i].depth&&scaled[i].key==wide[i].key&&scaled[i].isp==wide[i].isp&&scaled[i].tsp==wide[i].tsp,
           "opacity must preserve ordering and panel pipeline state");
   if(i)Require(scaled[i].depth<scaled[i-1].depth,"transparent panel layers retain deterministic compositing order");
   for(int k=0;k<4;++k) {
    const auto& a=scaled[i].v[k];const auto& b=wide[i].v[k];
    Require(a.x==b.x&&a.y==b.y&&a.z==b.z&&a.rhw==b.rhw&&a.u==b.u&&a.v==b.v&&a.specular==b.specular,
            "opacity must not move, clip or remap panel coverage");
    Require((a.diffuse&0x00ffffff)==(b.diffuse&0x00ffffff),"opacity must preserve panel RGB");
    const auto expected=((live.Hud().menuPanels[i].color>>24)*std::clamp(opacity,25,100)+50)/100;
    Require((a.diffuse>>24)==expected,"all four panel vertices must use bounded rounded opacity");
   }
  }
 }
 Require(live.Hud().menuText.size()==textLayout.size(),"opacity must preserve all menu text");
 for(size_t i=0;i<textLayout.size();++i) {
  const auto& a=live.Hud().menuText[i];const auto& b=textLayout[i];
  Require(a.x==b.x&&a.y==b.y&&a.text==b.text&&a.scale==b.scale&&a.color==b.color,"opacity must preserve full-opacity text and its layout");
 }
 if(argc==3)BossReplay(argv[1],argv[2]);
 puts("PASS: local Horde recording, corruption rejection, actual attract replay, isolated live RNG/frame state and ordered native panels");
}
