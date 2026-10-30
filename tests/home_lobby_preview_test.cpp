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
static bool RoomRules(const game::World& w,int row,const std::string& expected) {
    for(const auto& text:w.Hud().menuText)
        if(text.x==378&&text.y==308+row*50&&text.text==expected)return true;
    return false;
}
int main(int argc,char** argv) {
    if(argc!=3){std::fprintf(stderr,"usage: home_lobby_preview_test <local-port> <game-dir>\n");return 2;}
    const auto port=static_cast<uint16_t>(std::stoi(argv[1]));constexpr uint32_t build=987654;
    // Difficulty occupies only the reserved high bits; privacy/relay/continue
    // flags must survive every level. Zero and future codes remain unknown.
    for(int difficulty=0;difficulty<5;++difficulty) {
        const auto flags=uint8_t(0x1F|port::net::RoomDifficultyFlags(difficulty));
        Require((flags&0x1F)==0x1F&&port::net::RoomDifficulty(flags)==difficulty,"difficulty roundtrip preserves all existing room flags");
    }
    Require(port::net::RoomDifficultyFlags(-1)==0&&port::net::RoomDifficultyFlags(5)==0,
            "invalid difficulty cannot advertise an invented level");
    for(uint8_t code:{uint8_t(0),uint8_t(6),uint8_t(7)})
        Require(port::net::RoomDifficulty(uint8_t(0x1F|(code<<5)))==-1,"legacy/reserved difficulty advertisements stay unknown");
    std::string error;port::net::Session old,privateHost,hidden,publicHost,extra,directory,joiner,privateJoiner;
    std::vector<port::net::Session*> all={&old,&privateHost,&hidden,&publicHost,&extra,&directory,&joiner,&privateJoiner};
    auto configure=[&](port::net::Session& s,uint32_t version,bool host){s.SetBuild(version);Require(host?s.Host(0,&error):s.OpenJoiner(0,&error),error.c_str());Require(s.SetLobbyServer("127.0.0.1",port,&error),error.c_str());};
    for(auto* s:all)configure(*s,s==&old?build-1:build,s==&old||s==&privateHost||s==&hidden||s==&publicHost||s==&extra);
    auto pump=[&]{for(auto* s:all){s->Poll();if(s->LobbyCreated())s->LobbyRefresh();}};
    auto until=[&](auto predicate,const char* why){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!predicate()&&std::chrono::steady_clock::now()<end){pump();std::this_thread::sleep_for(std::chrono::milliseconds(10));}Require(predicate(),why);};
    auto create=[&](port::net::Session& s,uint8_t flags,int difficulty=-1){s.LobbyCreate(flags,difficulty);until([&]{return s.LobbyCreated();},"create actual waiting room");};
    create(old,port::net::kRoomRelay);
    create(privateHost,port::net::kRoomRelay|port::net::kRoomPrivate,0);
    create(hidden,port::net::kRoomRelay|port::net::kRoomUnlisted);
    create(publicHost,port::net::kRoomRelay|port::net::kRoomUnlimited|port::net::RoomDifficultyFlags(1),4);
    create(extra,port::net::kRoomRelay|port::net::RoomDifficultyFlags(2));
    directory.LobbyList();until([&]{return directory.LobbyRoomsKnown();},"directory LIST response");
    const auto listed=directory.LobbyRooms();Require(listed.size()==4,"server did not hide unlisted room or omitted waiting room");
    const auto visible=play::HomeLobbyRooms(listed,build);
    Require(visible.size()==2&&visible[0].id==privateHost.LobbyRoomId()&&visible[1].id==publicHost.LobbyRoomId(),"Home must show first two compatible listed rooms, skipping old builds");
    Require(visible[0].privateRoom&&!visible[0].unlimited&&!visible[1].privateRoom&&visible[1].unlimited,"Home flags did not match real rooms");
    Require(visible[0].difficulty==0&&visible[1].difficulty==4,"actual directory roundtrip preserves each waiting room's difficulty");
    Require(!directory.PeerKnown()&&!directory.Connected()&&directory.LobbyJoinResult()==port::net::JoinResult::Pending,"read-only preview joined or configured a peer");
    auto malformed=listed;malformed.insert(malformed.begin(),{0,0,build});malformed.insert(malformed.begin(),{hidden.LobbyRoomId(),port::net::kRoomUnlisted,build});
    Require(play::HomeLobbyRooms(malformed,build)[0].id==visible[0].id,"client policy exposed an unlisted/zero-id room");
    Require(play::PlanHomeRoom(listed,privateHost.LobbyRoomId(),build)==play::HomeRoomAction::PrivateCode,"private preview must ask for code before joining");
    Require(play::PlanHomeRoom(listed,publicHost.LobbyRoomId(),build)==play::HomeRoomAction::JoinPublic,"public preview must enter existing public join flow");
    Require(play::PlanHomeRoom(listed,old.LobbyRoomId(),build)==play::HomeRoomAction::OtherVersion,"old build must be rejected before joining");
    Require(play::PlanHomeRoom(listed,hidden.LobbyRoomId(),build)==play::HomeRoomAction::Browse,"stale/missing room must safely return to Browse");
    game::World world;Require(world.Load(argv[2],0,&error),error.c_str());Menu(world);
    // Exercise all labels through the same directory-to-HUD path, including a
    // legacy host and reserved future codes rather than silently calling them Normal.
    const char* levels[]={"VERY EASY","EASY","NORMAL","HARD","VERY HARD"};
    for(int difficulty=0;difficulty<5;++difficulty) {
        const uint8_t flags=port::net::kRoomRelay|port::net::RoomDifficultyFlags(difficulty);
        const auto room=play::HomeLobbyRooms({{123,flags,build}},build);
        Require(room.size()==1&&room[0].difficulty==difficulty,"Home adapter decodes every advertised difficulty");
        world.SetPortHomeRooms(room,"WAITING");Step(world);
        Require(RoomRules(world,0,std::string(levels[difficulty])+" / 3 CONTINUES"),"Home shows the full advertised difficulty with the continue rule");
    }
    for(uint8_t code:{uint8_t(0),uint8_t(6),uint8_t(7)}) {
        const auto room=play::HomeLobbyRooms({{123,uint8_t(port::net::kRoomUnlimited|(code<<5)),build}},build);
        Require(room.size()==1&&room[0].difficulty==-1,"Home adapter preserves unknown difficulty");
        world.SetPortHomeRooms(room,"WAITING");Step(world);
        Require(RoomRules(world,0,"UNKNOWN / UNLIMITED"),"Home identifies unknown legacy/future difficulty without hiding continue rules");
    }
    const game::PortHomeRoom legacy{123,false,true,build};
    Require(legacy.difficulty==-1,"existing aggregate callers default to unknown difficulty");
    world.SetPortHomeRooms(visible,"WAITING FOR A PARTNER");Step(world,450,299);
    Require(world.Hud().nightShiftRow==8,"hover first real room after the game switch button");
    Require(RoomRules(world,0,"VERY EASY / 3 CONTINUES")&&RoomRules(world,1,"VERY HARD / UNLIMITED"),
            "Home displays the actual private and public room difficulties before joining");
    auto reversed=visible;std::swap(reversed[0],reversed[1]);world.SetPortHomeRooms(reversed,"REFRESHED");Step(world,450,299);
    Require(world.Hud().nightShiftRow==9,"directory refresh must retain selected room ID when ordering changes");
    Require(RoomRules(world,0,"VERY HARD / UNLIMITED")&&RoomRules(world,1,"VERY EASY / 3 CONTINUES"),
            "refreshed labels stay attached to their room IDs");
    Step(world,450,299,4,false);Step(world,450,299); // keyboard confirm retains selected room ID after reorder
    Require(world.GameMode()==15&&world.Hud().portLobbyAction==4&&world.Hud().portLobbyRoom==privateHost.LobbyRoomId(),"room confirmation routed a different refreshed room ID");
    Require(!directory.PeerKnown()&&!privateJoiner.PeerKnown(),"clicking private room prematurely joined before code entry");
    // Losing the selected room moves focus safely to the game switch. Directory
    // changes must preserve that action as the number of lobby cards changes.
    Menu(world);world.SetPortHomeRooms(visible,"WAITING");Step(world,450,299);
    world.SetPortHomeRooms({visible[1]},"REFRESHED");Step(world,450,299);Require(world.Hud().nightShiftRow==7,"removed room did not move focus safely to the game switch");
    world.SetPortHomeRooms({},"EMPTY");Step(world,450,299);
    Require(world.Hud().nightShiftRow==7,"room count refresh changed game switch focus");
    for(const auto& text:world.Hud().menuText)Require(text.text!="HOST ONLINE"&&text.text!="BROWSE LOBBY"&&text.text!="PEER TO PEER",
        "retired Home network shortcuts remain visible");
    // Only deliberate existing join calls below are allowed to emit JOIN packets.
    joiner.LobbyJoin(publicHost.LobbyRoomId(),0);until([&]{return joiner.LobbyJoinResult()!=port::net::JoinResult::Pending;},"public room join result");Require(joiner.LobbyJoinResult()==port::net::JoinResult::Ok,"public room rejected explicit join");
    privateJoiner.LobbyJoin(privateHost.LobbyRoomId(),0);until([&]{return privateJoiner.LobbyJoinResult()!=port::net::JoinResult::Pending;},"private wrong-code result");Require(privateJoiner.LobbyJoinResult()==port::net::JoinResult::WrongCode,"private room joined without its code");
    privateJoiner.LobbyJoin(privateHost.LobbyRoomId(),privateHost.LobbyCode());until([&]{return privateJoiner.LobbyJoinResult()!=port::net::JoinResult::Pending;},"private authorized result");Require(privateJoiner.LobbyJoinResult()==port::net::JoinResult::Ok,"private room rejected explicit correct code");
    directory.LobbyList();until([&]{pump();return directory.LobbyRooms().size()==2;},"refresh excludes occupied rooms");
    const auto after=play::HomeLobbyRooms(directory.LobbyRooms(),build);Require(after.size()==1&&after[0].id==extra.LobbyRoomId(),"Home retained full/old/unlisted rooms after live refresh");
    Require(after[0].difficulty==2,"replacement waiting room retains its own Normal difficulty");
    Menu(world);world.SetPortHomeRooms(after,"WAITING");Step(world);
    Require(RoomRules(world,0,"NORMAL / 3 CONTINUES"),"live room replacement updates the displayed difficulty");
    Require(!directory.PeerKnown()&&!directory.Connected(),"directory acquired a gameplay peer during refresh");
    std::puts("PASS real local Home directory: advertised difficulty, visibility/build policy, game-switch focus, room-ID refresh, private code and deliberate public joins");
}
