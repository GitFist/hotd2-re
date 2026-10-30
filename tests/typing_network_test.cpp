// Loopback Session transport regression. Run with typing_network_integration.py.
#include "net/netplay.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using port::net::FrameInput;
using port::net::Session;
using Clock = std::chrono::steady_clock;
static void Require(bool ok,const char* why) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1); } }
static FrameInput Input(int player,int step) {
    FrameInput input;input.present=true;input.in.kind=0;input.in.deviceMode=6;
    input.in.x=120+player*300;input.in.y=200+step;
    input.in.typingText=player==0?"Aa1;:!? z":"P2 AbC,./";
    input.in.typingText+=char('A'+step%26);
    if (step==2) input.in.typingText=std::string(32,player?'B':'A');
    if (step==3) input.in.typingText="first\x1bnext";
    input.in.typingCancel=step==4;
    input.in.typingItems=uint8_t(1u<<(step%4));
    if (step==6) input.command=port::net::kCmdResume;
    return input;
}
static bool Same(const FrameInput& a,const FrameInput& b) {
    return a.present==b.present&&a.command==b.command&&a.in.kind==b.in.kind&&a.in.deviceMode==b.in.deviceMode&&
        a.in.x==b.in.x&&a.in.y==b.in.y&&a.in.held==b.in.held&&a.in.pressed==b.in.pressed&&
        a.in.fired==b.in.fired&&a.in.offscreen==b.in.offscreen&&a.in.typingText==b.in.typingText&&
        a.in.typingCancel==b.in.typingCancel&&a.in.typingItems==b.in.typingItems;
}
int main(int argc,char** argv) {
    Require(argc==3,"usage: typing_network_test <loopback-service-port> <ticket-path>");
    const int port=std::stoi(argv[1]);Require(port>0&&port<65536,"invalid loopback port");
    constexpr uint32_t build=998877,initialHash=0x12345678;
    Session host,guest;std::string error;
    for (Session* session:{&host,&guest}) {
        session->SetBuild(build);
        Require(session->SetLobbyServer("127.0.0.1",uint16_t(port),&error),error.c_str());
    }
    Require(host.Host(0,&error),error.c_str());Require(guest.OpenJoiner(0,&error),error.c_str());
    port::net::SessionConfig config;config.flags=15;config.startFrame=100;config.delay=3;config.seed=17;config.startedAt=1793203200;
    config.save.resize(60000);
    for (size_t i=0;i<config.save.size();++i) config.save[i]=uint8_t(i*17+3);
    host.SetConfig(config);host.SetDelayPolicy(3,false);
    const uint32_t first=config.startFrame+config.delay;
    bool guestSeeded=false,hostSeeded=false;
    auto pump=[&] {
        host.LobbyRefresh();host.Poll();guest.Poll();
        if (guest.Connected()&&!guestSeeded) { guest.PushLocal(first,Input(1,0));guestSeeded=true; }
        if (host.Connected()&&!hostSeeded) { host.PushLocal(first,Input(0,0));hostSeeded=true; }
        host.Send();guest.Send();
    };
    auto until=[&](auto ready,auto poll,const char* why) {
        const auto deadline=Clock::now()+std::chrono::seconds(7);
        while (!ready()&&Clock::now()<deadline) { poll();std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        Require(ready(),why);
    };
    host.LobbyCreate(port::net::kRoomUnlimited,2,true);
    until([&]{return host.LobbyCreated();},pump,"typing room was not created");
    guest.LobbyList();
    until([&]{return guest.LobbyRoomsKnown();},pump,"typing room list was not received");
    Require(guest.LobbyRooms().size()==1&&guest.LobbyRooms()[0].typing&&
        port::net::RoomDifficulty(guest.LobbyRooms()[0].flags)==2,"typing room mode/difficulty metadata changed");
    guest.LobbyJoin(host.LobbyRoomId(),0);
    until([&]{return host.Connected()&&guest.Connected();},pump,"typing peers did not connect");
    Require(guest.Config().flags==15&&guest.Delay()==3&&guest.Config().startedAt==config.startedAt&&guest.Config().save==config.save,
        "typing session configuration, full save or host UTC start time was not shared");
    host.BeginHistory(initialHash);guest.BeginHistory(initialHash);
    std::vector<std::array<FrameInput,2>> expected;
    auto remember=[&](uint32_t frame,const FrameInput inputs[2]) {
        expected.push_back({inputs[0],inputs[1]});
        host.RecordFrame(frame,inputs,initialHash^frame);guest.RecordFrame(frame,inputs,initialHash^frame);
    };
    for (uint32_t frame=config.startFrame;frame<first;++frame) {
        FrameInput inputs[2],remote;
        Require(host.RemoteInput(frame,&remote)&&!remote.present&&remote.in.typingText.empty(),"input arrived before the configured delay");
        remember(frame,inputs);
    }
    for (int step=0;step<32;++step) {
        const uint32_t frame=first+step;FrameInput inputs[2]={Input(0,step),Input(1,step)},fromHost,fromGuest;
        FrameInput raw[2]={inputs[0],inputs[1]};
        if (step==5) for (int p=0;p<2;++p) {
            raw[p].in.typingText=std::string(40,p?'R':'Q');
            inputs[p].in.typingText=std::string(32,p?'R':'Q');
        }
        host.PushLocal(frame,raw[0]);
        if (step==10) { pump();Require(!host.RemoteInput(frame,&fromGuest),"lockstep accepted a missing remote typing frame"); }
        guest.PushLocal(frame,raw[1]);
        until([&]{return guest.RemoteInput(frame,&fromHost)&&host.RemoteInput(frame,&fromGuest);},pump,"typing input retransmission stalled");
        Require(Same(fromHost,inputs[0])&&Same(fromGuest,inputs[1]),"ordered P1/P2 text, cancel or F1-F4 changed over UDP");
        FrameInput duplicate=Input(0,step);duplicate.in.typingText="DUPLICATE";host.PushLocal(frame,duplicate);
        FrameInput local;Require(host.LocalInput(frame,&local)&&Same(local,inputs[0]),"repeated local frame overwrote ordered text");
        remember(frame,inputs);
        host.ReportHash(frame,initialHash^frame);guest.ReportHash(frame,initialHash^frame);
    }
    Require(host.CanRejoin()&&guest.CanRejoin()&&guest.SaveTicket(argv[2]),"typing session did not retain a reconnect ticket");
    guest.Close();host.ContinueAlone(host.HistoryEnd());
    for (int step=0;step<20;++step) {
        const uint32_t frame=host.HistoryEnd();FrameInput inputs[2]={Input(0,step+40),{}};
        inputs[1].present=true;inputs[1].in.kind=1;inputs[1].in.x=inputs[1].in.y=0xFFFF;
        expected.push_back({inputs[0],inputs[1]});host.RecordFrame(frame,inputs,initialHash^frame);
    }
    const uint32_t end=host.HistoryEnd();Session returning;returning.SetBuild(build);
    Require(returning.LoadTicket(argv[2],&error),error.c_str());
    auto recoveryPump=[&] { host.Poll();returning.Poll();host.Send();returning.Send(); };
    until([&]{return returning.ReplayPending()||returning.RecoveryFailed();},recoveryPump,"typing reconnect did not receive its initial state");
    Require(returning.Config().startedAt==config.startedAt&&returning.Config().save==config.save,
        "typing reconnect lost the host UTC start time or full save");
    Require(!returning.RecoveryFailed(),returning.RecoveryError().c_str());returning.ReplayStarted(initialHash);
    until([&]{return returning.HistoryEnd()>=end||returning.RecoveryFailed();},recoveryPump,"typing reconnect history did not arrive");
    Require(!returning.RecoveryFailed(),returning.RecoveryError().c_str());
    for (uint32_t frame=config.startFrame;frame<end;++frame) {
        FrameInput replay[2];uint32_t hash=0;
        Require(returning.ReplayFrame(frame,replay,&hash)&&hash==(initialHash^frame)&&
            Same(replay[0],expected[frame-config.startFrame][0])&&Same(replay[1],expected[frame-config.startFrame][1]),
            "reconnect history changed either player's text, cancel, F1-F4 or frame hash");
        returning.RecordFrame(frame,replay,hash);
    }
    host.Abandon();returning.Close();host.Close();
    std::puts("PASS Typing UDP: separate players, ordered/max32 text, cancel, F1-F4, delayed/missing frames, retries, and reconnect history");
}
