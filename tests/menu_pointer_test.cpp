// Native Night Shift routing, mixed mouse/keyboard input and preserved Classic menu.
#include <cstdio>
#include <cstdlib>
#include <string>
#include "game/world.h"
static void Require(bool ok,const char* why) {if(!ok){fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static void Step(game::World& w,int x=630,int y=465,uint32_t pressed=0,bool shot=false) {
 game::WorldInput in;in.kind=1;in.x=x;in.y=y;in.held=in.pressed=pressed;in.fired=shot;
 w.SetInput(0,in);w.SetInput(1,{});w.Step();
}
static void Menu(game::World& w,bool modern=true) {
 game::WorldConfig c;c.portFeatures=true;c.portModernMenu=modern;w.Boot(c);
 w.ForceMode(4,0x10000,0,0);Step(w);Step(w);Step(w);
 Require(w.GameMode()==4,"enter mode select");
}
static void Click(game::World& w,int x,int y) {Step(w,x,y,4,true);Step(w,x,y);}
int main(int argc,char** argv) {
 if(argc!=2)return 2;game::World w;std::string error;
 Require(w.Load(argv[1],0,&error),error.c_str());Menu(w);
 Require(w.Hud().nightShiftPage==0&&!w.Hud().menuPanels.empty(),"Night Shift home and native panels");
 Require(w.Hud().screens.empty(),"no static menu tiles over attract background");
 Step(w,100,165);Require(w.Hud().nightShiftRow==0,"full row hover selects Play");
 Step(w,100,165,0x20);Step(w,100,165);
 Require(w.Hud().nightShiftRow==1,"stationary aim preserves keyboard movement");
 Step(w,101,165);Require(w.Hud().nightShiftRow==0,"moving aim reselects row");
 Click(w,100,180);Require(w.Hud().nightShiftPage==1&&w.GameMode()==4,"Play is a category");
 Click(w,100,245);Step(w);Require(w.GameMode()==6&&w.Hud().gameType==3,"Boss time attack starts from Play");
 Menu(w);Click(w,100,218);Require(w.Hud().nightShiftPage==2,"Practice category");
 Click(w,100,160);Step(w);Require(w.GameMode()==6&&w.Hud().gameType==2,"Training starts from Practice");
 Menu(w);Click(w,100,264);Require(w.Hud().nightShiftPage==3,"Leaderboard category");
 Click(w,80,150);Require(w.Hud().portLeaderboardShared&&w.Hud().portLeaderboardRequests==1,"online source requests shared records");
 Click(w,80,320);Require(w.Hud().portLeaderboardTable==3,"per-boss leaderboard");
 Click(w,589,100);Require(w.Hud().portLeaderboardTable==4,"next boss changes requested table");
 Click(w,80,350);Require(w.Hud().nightShiftPage==3&&w.Hud().portLeaderboardPublishRequests==0,"cannot publish without a new qualifying run");
 w.SetPortLeaderboardPublishable(true,"NEW RUN AVAILABLE");
 Click(w,80,350);Require(w.Hud().nightShiftPage==4&&w.Hud().portLeaderboardPublishRequests==0,"publish opens explicit initials confirmation");
 Click(w,320,400);Require(w.Hud().nightShiftPage==3&&w.Hud().portLeaderboardPublishRequests==0,"cancel never publishes");
 Click(w,80,350);Click(w,320,355);
 Require(w.Hud().nightShiftPage==3&&w.Hud().portLeaderboardPublishRequests==1&&w.Hud().portLeaderboardInitials=="AMS","only final Publish submits initials and fresh-run request");
 Step(w,630,465,2);Step(w);Require(w.Hud().nightShiftPage==0,"Back returns Home");
 for(int action=1;action<=3;++action) {
  Menu(w);Click(w,450,298+(action-1)*40);Step(w);
  Require(w.GameMode()==15&&w.Hud().portLobbyAction==action,"Home rail routes existing Host/Browse/Peer page");
 }
 Menu(w);w.SetPortHomeRooms({{1042,false,true,0},{1047,true,false,0}},"WAITING ROOMS");Step(w);
 Click(w,450,185);Step(w);
 Require(w.GameMode()==15&&w.Hud().portLobbyAction==4&&w.Hud().portLobbyRoom==1042,"real Home waiting room opens its existing join flow");
 Menu(w);Step(w,630,420,4,true);Step(w);
 Require(!w.QuitRequested()&&w.GameMode()==4,"blank-space shot does not activate current row");
 Menu(w);Click(w,100,345);Step(w);Require(w.GameMode()==4&&w.Hud().nightShiftPage==5,"native Options landing page");
 for(int page=0;page<5;++page) {
  const int prior=w.Hud().portDisplayRequests;Click(w,100,148+page*40);
  Require(w.GameMode()==4&&w.Hud().nightShiftPage==5&&w.Hud().portDisplayPage==page&&w.Hud().portDisplayRequests==prior+1,"native category opens existing local display page without leaving Options");
 }
 Click(w,100,350);Step(w);Require(w.GameMode()==12,"existing Game Options remains available");
 w.ForceMode(4,0x10000,0,0);Step(w);Step(w);
 Require(w.Hud().nightShiftPage==5,"legacy Game Options returns to native Options landing page");
 Menu(w,false);Step(w,120,264);Require(w.Hud().menuCursor==0&&w.Hud().nightShiftPage<0,"Classic menu remains keyboard-driven");
 w.SetPortModernMenu(true);Step(w);Require(w.Hud().nightShiftPage==0&&w.Hud().screens.empty(),"Classic to Night Shift switch");
 w.SetPortModernMenu(false);Step(w);Require(w.Hud().nightShiftPage<0&&!w.Hud().screens.empty(),"Night Shift to Classic restores tiles");
 puts("PASS: Night Shift routes, Boss time attack, Training, shared/boss leaderboard, existing network navigation, mixed input and Classic switching");
}
