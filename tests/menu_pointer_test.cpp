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
static void ClassicExtras(game::World& w) {
 Menu(w,false);Step(w,80,382);
 Require(w.Hud().menuCursor==7&&w.Hud().nightShiftPage<0,"Classic Extras is selectable below Network");
 Step(w,630,465,12,true);Step(w);
 Require(w.Hud().nightShiftPage<0&&w.GameMode()==4,"blank shot cannot activate selected Classic Extras");
 Click(w,80,382);
 Require(w.Hud().nightShiftPage==9&&w.GameMode()==4&&w.Hud().screens.empty(),"Classic click opens shared Extras without joining a player");
 Step(w,630,465,8);
 Require(w.Hud().nightShiftPage==9&&w.GameMode()==4,"Classic Extras Enter opens its Music Player page");
 Step(w);Step(w,630,465,8);
 bool preview=false;for(const auto& event:w.Sounds())if(event.kind==game::World::kPreviewMusicStart)preview=true;
 Require(preview&&w.GameMode()==4,"Classic Extras Enter starts a preview without starting gameplay");
 Step(w,630,465,2);
 Require(w.Hud().nightShiftPage==9,"Music Player Back returns to Extras hub");
 Step(w);Step(w,630,465,2);
 Require(w.Hud().nightShiftPage<0&&w.Hud().screens.size()>=6&&w.Hud().menuPanels.empty(),"Classic Back restores the original tiles in the same frame");
 Step(w);Step(w,630,465,0x20);Step(w);
 Require(w.Hud().menuCursor==8,"Scene Select follows Extras in Classic keyboard order");
 Step(w,630,465,8);Step(w);Require(w.GameMode()==6&&w.Hud().gameType==3,"Classic Scene Select still starts its existing handler");
 Menu(w,false);
 for(int i=0;i<3;++i){Step(w,630,465,0x10);Step(w);}
 Require(w.Hud().menuCursor==7,"Classic keyboard navigation reaches Extras");
 Step(w,630,465,8);Step(w);Require(w.Hud().nightShiftPage==9&&w.GameMode()==4,"Classic Enter opens Extras");
 Step(w,630,465,2);Step(w);
 Step(w,630,465,0x20);Step(w);Step(w,630,465,0x20);Step(w);
 Require(w.Hud().menuCursor==9,"Classic Exit remains the last entry");
 Step(w,630,465,8);Require(w.QuitRequested(),"Classic Exit still quits");
 Menu(w,false);
 game::WorldInput second;second.kind=1;second.x=80;second.y=382;second.fired=true;second.held=second.pressed=12;
 w.SetInput(0,{});w.SetInput(1,second);w.Step();Step(w);
 Require(w.Hud().nightShiftPage==9&&w.GameMode()==4,"second gun can shoot Classic Extras without joining");
 Step(w,630,465,2);Step(w);
 game::WorldConfig original;w.Boot(original);w.ForceMode(4,0x10000,0,0);Step(w);Step(w);Step(w);
 Step(w,630,465,0x10);Step(w);
 Require(w.Hud().menuCursor==7,"non-port original menu retains its eight entries");
 Step(w,630,465,8);Require(w.QuitRequested(),"non-port original Exit remains index seven");
}
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
 Menu(w);Click(w,100,248);Require(w.Hud().nightShiftPage==3,"Leaderboard category");
 Click(w,80,150);Require(w.Hud().portLeaderboardShared&&w.Hud().portLeaderboardRequests==1,"online source requests shared records");
 Click(w,80,320);Require(w.Hud().portLeaderboardTable==3,"per-boss leaderboard");
 Click(w,589,100);Require(w.Hud().portLeaderboardTable==4,"next boss changes requested table");
 Click(w,80,350);Require(w.Hud().nightShiftPage==3&&w.Hud().portLeaderboardPublishRequests==0,"cannot publish without a new qualifying run");
 w.SetPortLeaderboardPublishable(true,"NEW RUN AVAILABLE");
 Click(w,80,350);Require(w.Hud().nightShiftPage==4&&w.Hud().portLeaderboardPublishRequests==0,"publish opens explicit online name confirmation");
 Click(w,500,386);Require(w.Hud().nightShiftPage==3&&w.Hud().portLeaderboardPublishRequests==0,"cancel never publishes");
 Click(w,80,350);w.EditPortLeaderboardName("NIGHTSHIFT12");Click(w,380,386);
 Require(w.Hud().nightShiftPage==3&&w.Hud().portLeaderboardPublishRequests==1&&w.Hud().portLeaderboardInitials=="NIGHTSHIFT12","only final Publish submits online name and fresh-run request");
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
 Menu(w);const int prior=w.Hud().portDisplayRequests;Click(w,100,359);Step(w);
 Require(w.GameMode()==4&&w.Hud().nightShiftPage==0&&w.Hud().portDisplayPage==0&&w.Hud().portDisplayRequests==prior+1,
         "Options opens the unified Display tab directly while preserving the Home background");
 for(int tick=0;tick<5;++tick)Step(w);
 Require(w.GameMode()==4&&w.Hud().portDisplayRequests==prior+1,"idle background ticks cannot reopen Options or join a player");
 Menu(w,false);Step(w,120,264);Require(w.Hud().menuCursor==0&&w.Hud().nightShiftPage<0,"Classic menu remains keyboard-driven");
 w.SetPortModernMenu(true);Step(w);Require(w.Hud().nightShiftPage==0&&w.Hud().screens.empty(),"Classic to Night Shift switch");
 w.SetPortModernMenu(false);Step(w);Require(w.Hud().nightShiftPage<0&&!w.Hud().screens.empty(),"Night Shift to Classic restores tiles");
 ClassicExtras(w);
 puts("PASS: Classic Extras pointer/keyboard routing and restoration; Night Shift routes, Boss time attack, Training, shared/boss leaderboard, existing network navigation, mixed input and Classic switching");
}
