// Actual UDP Session cancellation, including delayed CREATED / lost LEAVE.
// Run through lobby_lifecycle_integration.py, which uses loopback services only.
#include "play/home_lobby_preview.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

using port::net::JoinResult;
using port::net::Session;
using Clock = std::chrono::steady_clock;
static void Require(bool ok,const char* why) {
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); }
}
int main(int argc,char** argv) {
    Require(argc==3,"usage: lobby_lifecycle_test <loopback-service-port> <silent-port>");
    constexpr uint32_t build=987654;
    const auto port=static_cast<uint16_t>(std::stoi(argv[1]));
    std::string error;
    Session host,directory,joiner,absentHost,silent;
    auto configure=[&](Session& s,bool hosting) {
        s.SetBuild(build);
        Require(s.SetLobbyServer("127.0.0.1",port,&error),error.c_str());
        Require(hosting?s.Host(0,&error):s.OpenJoiner(0,&error),error.c_str());
    };
    configure(host,true);configure(directory,false);configure(joiner,false);
    auto pump=[&] { host.PollLobbyCleanup();directory.Poll();joiner.Poll(); };
    auto until=[&](auto predicate,double seconds,const char* why) {
        const auto end=Clock::now()+std::chrono::duration<double>(seconds);
        while (!predicate() && Clock::now()<end) { pump();std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        Require(predicate(),why);
    };
    auto freshList=[&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
        directory.Reset();Require(directory.OpenJoiner(0,&error),error.c_str());directory.LobbyList();
        until([&]{return directory.LobbyRoomsKnown();},2,"fresh directory reply");
        return directory.LobbyRooms();
    };
    host.LobbyCreate(0);
    until([&]{host.Poll();return host.LobbyCreated();},2,"known room CREATED");
    const uint32_t room=host.LobbyRoomId();
    auto stale=freshList();Require(stale.size()==1&&stale[0].id==room,"cache initial hosted room");
    // The harness holds this second list response until after cancellation.
    directory.LobbyList();std::this_thread::sleep_for(std::chrono::milliseconds(50));
    host.LobbyLeave();host.Reset();
    Require(!host.Active()&&!host.LobbyCreated()&&!host.PeerKnown(),"Back left an active gameplay session");
    Require(host.IsCancelledLobbyRoom(room),"known cancellation lost its room identity");
    play::PruneCancelledHomeRooms(stale,host);
    Require(stale.empty()&&play::PlanHomeRoom(stale,room,build)==play::HomeRoomAction::Browse,
            "cancelled cached/listed room can still be selected");
    until([&]{return freshList().empty();},3,"lost initial LEAVE was not retried before room TTL");
    // Keep polling after the delayed old LIST arrives: its retired socket must
    // not overwrite the fresh empty cache or start another room.
    const auto stableUntil=Clock::now()+std::chrono::seconds(1);
    while(Clock::now()<stableUntil){pump();host.LobbyRefresh();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    Require(directory.LobbyRooms().empty()&&!host.Active(),"old LIST or refresh resurrected cancelled room");
    joiner.LobbyJoin(room,0);
    until([&]{return joiner.LobbyJoinResult()!=JoinResult::Pending;},2,"stale room join hung Connecting");
    Require(joiner.LobbyJoinResult()==JoinResult::NoRoom&&!joiner.Connected(),"cancelled host still accepts a stale-room join");

    // Back before CREATED: retain only the old cancellation socket, receive the
    // delayed credentials there, and close that room without refreshing it.
    Require(host.Host(0,&error),error.c_str());host.LobbyCreate(0);host.Reset();
    Require(!host.Active()&&host.LobbyCancellationPending(),"pending CREATE cancellation discarded its late reply socket");
    uint32_t pendingRoom=0;
    until([&]{auto rooms=freshList();if(!rooms.empty())pendingRoom=rooms[0].id;return pendingRoom!=0;},2,"observe delayed-CREATED room before cleanup");
    until([&]{return !host.LobbyCancellationPending()&&host.IsCancelledLobbyRoom(pendingRoom);},2,"late CREATED did not complete cancellation");
    host.Reset(); // Another menu transition must not discard pending LEAVE retries.
    until([&]{return freshList().empty();},2,"late CREATED room was not deleted");

    // A service can accept an old listing while its host has gone away. The
    // client must stop waiting on a real-time bound, even with service replies.
    configure(absentHost,true);absentHost.LobbyCreate(0);
    until([&]{absentHost.Poll();return absentHost.LobbyCreated();},2,"unresponsive-host room setup");
    joiner.Reset();Require(joiner.OpenJoiner(0,&error),error.c_str());joiner.LobbyJoin(absentHost.LobbyRoomId(),0);
    until([&]{return joiner.LobbyJoinResult()==JoinResult::Ok;},2,"service did not accept absent-host join");
    const auto accepted=Clock::now();
    until([&]{return joiner.LobbyJoinResult()==JoinResult::TimedOut;},13,"accepted stale host stayed Connecting indefinitely");
    Require(std::chrono::duration<double>(Clock::now()-accepted).count()<12.5&&!joiner.Connected()&&!joiner.PeerKnown(),
            "accepted stale-host timeout retained a gameplay peer");
    Require(!host.IsCancelledLobbyRoom(room)&&!host.IsCancelledLobbyRoom(pendingRoom)&&!host.LobbyCancellationPending(),
            "retired cancellation sockets outlived their bounded lifetime");

    configure(silent,false);Require(silent.SetLobbyServer("127.0.0.1",static_cast<uint16_t>(std::stoi(argv[2])),&error),error.c_str());
    silent.LobbyJoin(12345,0);
    const auto noAnswer=Clock::now();
    until([&]{silent.Poll();return silent.LobbyJoinResult()==JoinResult::NoAnswer;},7,"unanswered join retries had no total deadline");
    Require(std::chrono::duration<double>(Clock::now()-noAnswer).count()<6.5&&!silent.PeerKnown(),"unanswered join timeout exceeded bound");
    std::puts("PASS cancellation: delayed CREATED, dropped initial LEAVE, stale LIST/cache, no orphan refresh, absent-host and silent-service deadlines");
}
