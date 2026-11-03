#include "net/netplay.h"
#include "play/network_desync.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>

namespace {
void Require(bool ok,const char* why) {
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); }
}
}
int main(int argc,char** argv) {
    Require(argc==2||argc==3,"usage: network_desync_test <game dir> [isolated lobby port]");
    const bool relay=argc==3;
    game::World worlds[2];
    for(auto& world:worlds) { Require(world.Load(argv[1]),"load world");world.Boot({}); }
    port::net::Session host,guest;std::string error;host.SetBuild(998879);guest.SetBuild(998879);
    Require(host.Host(0,&error),error.c_str());
#ifdef _WIN32
    _putenv_s("HOTD2_NET_LOCALCODE","1");
#else
    setenv("HOTD2_NET_LOCALCODE","1",1);
#endif
    Require(host.QueryPublicEndpoint(&error),error.c_str());uint8_t ip[4];uint16_t port=0;
    Require(port::net::DecodeCode(host.LocalCode(),ip,&port),"decode loopback endpoint");
    port::net::SessionConfig cfg;cfg.startFrame=100;cfg.delay=1;host.SetConfig(cfg);host.SetDelayPolicy(1,false);
    if(relay) {
        for(auto* peer:{&host,&guest})Require(peer->SetLobbyServer("127.0.0.1",uint16_t(std::atoi(argv[2])),&error),error.c_str());
        Require(guest.OpenJoiner(0,&error),error.c_str());
    } else Require(guest.Join("127.0.0.1",port,&error),error.c_str());
    auto until=[&](auto ready,auto pump,const char* why) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!ready()&&std::chrono::steady_clock::now()<deadline) {pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        Require(ready(),why);
    };
    auto pump=[&] {host.Poll();guest.Poll();host.Send();guest.Send();};
    if(relay) {
        host.LobbyCreate(port::net::kRoomUnlimited,2);
        until([&]{return host.LobbyCreated();},pump,"isolated relay room created");
        guest.LobbyJoin(host.LobbyRoomId(),0);
    }
    until([&]{return guest.Connected();},pump,"guest connects");guest.PushLocal(101,{});
    until([&]{return host.Connected();},pump,"host connects");
    if(relay)Require(host.UsingRelay()&&guest.UsingRelay(),"test is using the actual relay");
    host.BeginHistory(0x1234);guest.BeginHistory(0x1234);
    constexpr uint32_t mismatch=120,later=130;
    // Guest already has newer matching state. Only the host's older mismatch is
    // delivered first; the guest must repeat that latched failure, not 'later'.
    host.ReportHash(mismatch,0x11111111);guest.ReportHash(mismatch,0x22222222);guest.ReportHash(later,0x33333333);
    host.PushLocal(102,{});
    until([&]{return guest.Desynced(nullptr,nullptr,nullptr);},[&]{host.Send();guest.Poll();},"guest detects the older mismatch");
    guest.Abandon(true);
    Require(!guest.SaveTicket((std::filesystem::temp_directory_path()/"hotd2-desync-test.bin").string()),"divergent match cannot create a reconnect ticket");
    const uint32_t end=guest.HistoryEnd();port::net::FrameInput empty[2];guest.RecordFrame(end,empty,0x1234);
    Require(guest.HistoryEnd()==end,"invalidated replay history cannot advance");
    host.ReportHash(later,0x33333333);
    guest.PushLocal(102,{});
    until([&]{return host.Desynced(nullptr,nullptr,nullptr);},pump,"halted peer repeats the mismatched frame despite newer matching hashes");
    play::NetworkDesync guards[2];port::net::Session* peers[]={&host,&guest};
    for(int p=0;p<2;++p) {
        uint32_t frame=0,ours=0,theirs=0;
        Require(peers[p]->Desynced(&frame,&ours,&theirs)&&frame==mismatch&&ours!=theirs,"both peers retain the original mismatch");
        Require(guards[p].Observe(true,frame),"persistent error opens once");
        const int frozen=worlds[p].Frame();
        for(int i=0;i<20;++i)Require(!guards[p].Advance([&]{worlds[p].Step();}),"no World::Step after divergence");
        Require(worlds[p].Frame()==frozen,"both simulation timelines remain halted");
    }
    play::MenuInput input;Require(!guards[0].ReturnRequested(input),"notice needs an explicit return action");
    input.pressed=8;Require(guards[0].ReturnRequested(input),"keyboard/controller confirm returns");
    input={};input.pressed=2;Require(guards[0].ReturnRequested(input),"back/Escape returns");
    input={};input.trigger=input.pointer=true;input.px=320;input.py=295;
    Require(guards[0].ReturnRequested(input),"pointer/gun activates return button");
    input.py=100;Require(!guards[0].ReturnRequested(input),"pointer outside return button is ignored");
    game::WorldHud hud;guards[0].ModernHud(hud);
    Require(!hud.menuPanels.empty()&&!hud.menuText.empty(),"persistent notice and return button are rendered");
    guest.Abandon();
    std::puts("PASS older DESYNC reaches both UDP peers despite newer matching hashes; both worlds halt and explicit return controls work");
}
