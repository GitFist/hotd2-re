// Uses actual Session UDP clients and a separately started local lobby service.
#include "play/home_lobby_preview.h"
#include "game/world.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
static void Require(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static void Step(game::World& w,int x=630,int y=465,uint32_t pressed=0,bool shot=false) {
    game::WorldInput in;in.kind=1;in.x=x;in.y=y;in.held=in.pressed=pressed;in.fired=shot;w.SetInput(0,in);w.SetInput(1,{});w.Step();
}
static void Menu(game::World& w) {game::WorldConfig cfg;cfg.portFeatures=true;cfg.portModernMenu=true;w.Boot(cfg);w.ForceMode(4,0x10000,0,0);Step(w);Step(w);Step(w);Require(w.Hud().nightShiftPage==0,"open Night Shift Home");}
int main(int argc,char** argv) {
    if(argc!=3){std::fprintf(stderr,"usage: home_lobby_preview_test <local-port> <game-dir>\n");return 2;}
    const auto port=static_cast<uint16_t>(std::stoi(argv[1]));constexpr uint32_t build=987654;
    std::string error;port::net::Session old,privateHost,hidden,publicHost,extra,directory,joiner,privateJoiner;
    std::vector<port::net::Session*> all={&old,&privateHost,&hidden,&publicHost,&extra,&directory,&joiner,&privateJoiner};
    auto configure=[&](port::net::Session& s,uint32_t version,bool host){s.SetBuild(version);Require(host?s.Host(0,&error):s.OpenJoiner(0,&error),error.c_str());Require(s.SetLobbyServer("127.0.0.1",port,&error),error.c_str());};
    for(auto* s:all)configure(*s,s==&old?build-1:build,s==&old||s==&privateHost||s==&hidden||s==&publicHost||s==&extra);
    auto pump=[&]{for(auto* s:all){s->Poll();if(s->LobbyCreated())s->LobbyRefresh();}};
    auto until=[&](auto predicate,const char* why){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!predicate()&&std::chrono::steady_clock::now()<end){pump();std::this_thread::sleep_for(std::chrono::milliseconds(10));}Require(predicate(),why);};
    auto create=[&](port::net::Session& s,uint8_t flags){s.LobbyCreate(flags);until([&]{return s.LobbyCreated();},"create actual waiting room");};
    create(old,port::net::kRoomRelay);
    create(privateHost,port::net::kRoomRelay|port::net::kRoomPrivate);
    create(hidden,port::net::kRoomRelay|port::net::kRoomUnlisted);
    create(publicHost,port::net::kRoomRelay|port::net::kRoomUnlimited);
    create(extra,port::net::kRoomRelay);
    directory.LobbyList();until([&]{return directory.LobbyRoomsKnown();},"directory LIST response");
    const auto listed=directory.LobbyRooms();Require(listed.size()==4,"server did not hide unlisted room or omitted waiting room");
    const auto visible=play::HomeLobbyRooms(listed,build);
    Require(visible.size()==2&&visible[0].id==privateHost.LobbyRoomId()&&visible[1].id==publicHost.LobbyRoomId(),"Home must show first two compatible listed rooms, skipping old builds");
    Require(visible[0].privateRoom&&!visible[0].unlimited&&!visible[1].privateRoom&&visible[1].unlimited,"Home flags did not match real rooms");
    Require(!directory.PeerKnown()&&!directory.Connected()&&directory.LobbyJoinResult()==port::net::JoinResult::Pending,"read-only preview joined or configured a peer");
    auto malformed=listed;malformed.insert(malformed.begin(),{0,0,build});malformed.insert(malformed.begin(),{hidden.LobbyRoomId(),port::net::kRoomUnlisted,build});
    Require(play::HomeLobbyRooms(malformed,build)[0].id==visible[0].id,"client policy exposed an unlisted/zero-id room");
    Require(play::PlanHomeRoom(listed,privateHost.LobbyRoomId(),build)==play::HomeRoomAction::PrivateCode,"private preview must ask for code before joining");
    Require(play::PlanHomeRoom(listed,publicHost.LobbyRoomId(),build)==play::HomeRoomAction::JoinPublic,"public preview must enter existing public join flow");
    Require(play::PlanHomeRoom(listed,old.LobbyRoomId(),build)==play::HomeRoomAction::OtherVersion,"old build must be rejected before joining");
    Require(play::PlanHomeRoom(listed,hidden.LobbyRoomId(),build)==play::HomeRoomAction::Browse,"stale/missing room must safely return to Browse");
    game::World world;Require(world.Load(argv[2],0,&error),error.c_str());Menu(world);
    world.SetPortHomeRooms(visible,"WAITING FOR A PARTNER");Step(world,450,185);
    Require(world.Hud().nightShiftRow==6,"hover first real room");
    auto reversed=visible;std::swap(reversed[0],reversed[1]);world.SetPortHomeRooms(reversed,"REFRESHED");Step(world,450,185);
    Require(world.Hud().nightShiftRow==7,"directory refresh must retain selected room ID when ordering changes");
    Step(world,450,185,4,false);Step(world,450,185); // keyboard confirm retains selected room ID after reorder
    Require(world.GameMode()==15&&world.Hud().portLobbyAction==4&&world.Hud().portLobbyRoom==privateHost.LobbyRoomId(),"room confirmation routed a different refreshed room ID");
    Require(!directory.PeerKnown()&&!privateJoiner.PeerKnown(),"clicking private room prematurely joined before code entry");
    // Losing the selected room moves focus to Host; refreshing room count must
    // keep Host/Browse/P2P selected by action rather than its former row index.
    Menu(world);world.SetPortHomeRooms(visible,"WAITING");Step(world,450,185);
    world.SetPortHomeRooms({visible[1]},"REFRESHED");Step(world,450,185);Require(world.Hud().nightShiftRow==7,"removed room did not move focus safely to Host");
    for(int action=1;action<=3;++action){Menu(world);world.SetPortHomeRooms(visible,"WAITING");Step(world,450,298+(action-1)*40);Require(world.Hud().nightShiftRow==8+action-1,"Home network button hover");world.SetPortHomeRooms({},"EMPTY");Step(world,450,298+(action-1)*40);Require(world.Hud().nightShiftRow==6+action-1,"room count refresh changed selected network action");Step(world,450,298+(action-1)*40,4,true);Step(world);Require(world.GameMode()==15&&world.Hud().portLobbyAction==action,"Host/Browse/P2P button did not route existing page");}
    // Only deliberate existing join calls below are allowed to emit JOIN packets.
    joiner.LobbyJoin(publicHost.LobbyRoomId(),0);until([&]{return joiner.LobbyJoinResult()!=port::net::JoinResult::Pending;},"public room join result");Require(joiner.LobbyJoinResult()==port::net::JoinResult::Ok,"public room rejected explicit join");
    privateJoiner.LobbyJoin(privateHost.LobbyRoomId(),0);until([&]{return privateJoiner.LobbyJoinResult()!=port::net::JoinResult::Pending;},"private wrong-code result");Require(privateJoiner.LobbyJoinResult()==port::net::JoinResult::WrongCode,"private room joined without its code");
    privateJoiner.LobbyJoin(privateHost.LobbyRoomId(),privateHost.LobbyCode());until([&]{return privateJoiner.LobbyJoinResult()!=port::net::JoinResult::Pending;},"private authorized result");Require(privateJoiner.LobbyJoinResult()==port::net::JoinResult::Ok,"private room rejected explicit correct code");
    directory.LobbyList();until([&]{pump();return directory.LobbyRooms().size()==2;},"refresh excludes occupied rooms");
    const auto after=play::HomeLobbyRooms(directory.LobbyRooms(),build);Require(after.size()==1&&after[0].id==extra.LobbyRoomId(),"Home retained full/old/unlisted rooms after live refresh");
    Require(!directory.PeerKnown()&&!directory.Connected(),"directory acquired a gameplay peer during refresh");
    std::puts("PASS real local Home directory: visibility/build/limit policy, room-ID refresh, Host/Browse/P2P, private code and deliberate public joins");
}
