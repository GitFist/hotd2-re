#include "net/leaderboard.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <random>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using SOCKET = int;
#endif

namespace port::net {
namespace {
constexpr uintptr_t kNoSocket = static_cast<uintptr_t>(-1);
using Clock = std::chrono::steady_clock;
struct SocketRuntime {
#ifdef _WIN32
    bool ok = false;
    SocketRuntime() { WSADATA data; ok = WSAStartup(MAKEWORD(2,2),&data)==0; }
    ~SocketRuntime() { if (ok) WSACleanup(); }
#else
    bool ok = true;
#endif
};
void CloseSocket(uintptr_t socket) {
    if (socket==kNoSocket) return;
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(socket));
#else
    close(static_cast<int>(socket));
#endif
}
bool NonBlocking(uintptr_t socket) {
#ifdef _WIN32
    u_long on=1; return ioctlsocket(static_cast<SOCKET>(socket),FIONBIO,&on)==0;
#else
    return fcntl(static_cast<int>(socket),F_SETFL,fcntl(static_cast<int>(socket),F_GETFL,0)|O_NONBLOCK)==0;
#endif
}
void Put16(std::vector<uint8_t>& p,uint32_t value) { p.push_back(uint8_t(value)); p.push_back(uint8_t(value>>8)); }
void Put32(std::vector<uint8_t>& p,uint32_t value) { Put16(p,value); Put16(p,value>>16); }
uint16_t U16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1])<<8; }
uint32_t U32(const uint8_t* p) { return uint32_t(U16(p)) | uint32_t(U16(p+2))<<16; }
std::vector<uint8_t> Header(uint8_t kind,uint32_t request) {
    std::vector<uint8_t> p{'H','2','R','K',1,kind}; Put32(p,request); return p;
}
bool Valid(const LeaderboardSubmission& e) {
    if (e.table<0 || e.table>12 || e.name.size()!=3 || !e.score || e.score>2000000000 ||
        e.players<1 || e.players>2 || e.difficulty>(e.table<2?6:4) ||
        (e.table!=2 && e.rounds) || (e.table==2 && !e.rounds) || (e.table>=3 && (e.players!=1 || e.score>1296000))) return false;
    bool nonblank=false;
    for (char c:e.name) { if (!std::strchr("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-",c) || !c) return false; nonblank|=c!=' '; }
    return nonblank;
}
const char* SubmitError(uint8_t status) {
    switch(status) {
        case 2:return "RESULT IS NOT ELIGIBLE";
        case 3:return "SHARED RANKINGS NOT ENABLED";
        case 4:return "RESULT OUTSIDE TOP 1000";
        case 5:return "SERVICE BUSY; TRY LATER";
        default:return "INVALID RANKING RESPONSE";
    }
}
struct Resolution {
    std::atomic<int> state{0}; // release/acquire publishes address
    sockaddr_in address{};
};
} // namespace

struct LeaderboardClient::Impl {
    SocketRuntime runtime;
    std::shared_ptr<Resolution> resolution;
    uintptr_t socket=kNoSocket;
    uint32_t build=0,request=0;
    int table=0,operation=0,phase=0; // operation1=query2=submit; phase1=hello2=operation
    bool queryReady=false,submitReady=false,accepted=false;
    uint64_t revision=0;
    std::string error;
    std::vector<LeaderboardEntry> entries;
    LeaderboardSubmission submission;
    bool haveSubmission=false,submissionSent=false;
    std::array<uint8_t,16> run{};
    std::vector<uint8_t> outgoing;
    Clock::time_point started{},lastSent{};
    std::mt19937 random{std::random_device{}()};
    ~Impl() { CloseSocket(socket); }
    void Finish(const std::string& message={}) {
        error=message; queryReady=operation==1; submitReady=operation==2;
        operation=phase=0; ++revision;
    }
    void Start(int op) {
        operation=op;phase=1;queryReady=submitReady=accepted=false;error.clear();
        request=random(); if (!request) request=1;
        outgoing=Header(1,request);started=Clock::now();lastSent={};
    }
    bool OpenSocket() {
        if (socket!=kNoSocket) return true;
        if (!resolution || resolution->state.load(std::memory_order_acquire)<=0) return false;
        const SOCKET s=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if (s==static_cast<SOCKET>(-1)) { Finish("CANNOT OPEN RANKING CONNECTION");return false; }
        socket=static_cast<uintptr_t>(s);
        if (!NonBlocking(socket) || ::connect(s,reinterpret_cast<const sockaddr*>(&resolution->address),sizeof(sockaddr_in))!=0) {
            CloseSocket(socket);socket=kNoSocket;Finish("CANNOT OPEN RANKING CONNECTION");return false;
        }
        return true;
    }
    void WithCookie(const uint8_t* cookie) {
        outgoing=Header(operation==1?2:3,request);
        Put32(outgoing,build);outgoing.push_back(static_cast<uint8_t>(table));
        if (operation==2) {
            outgoing.insert(outgoing.end(),submission.name.begin(),submission.name.end());
            Put32(outgoing,submission.score);Put16(outgoing,submission.rounds);
            outgoing.push_back(submission.difficulty);outgoing.push_back(submission.players);
            outgoing.insert(outgoing.end(),run.begin(),run.end());
        }
        outgoing.insert(outgoing.end(),cookie,cookie+16);phase=2;lastSent={};
    }
};

LeaderboardClient::LeaderboardClient():impl_(std::make_unique<Impl>()) {}
LeaderboardClient::~LeaderboardClient()=default;
bool LeaderboardClient::Configure(const std::string& host,uint16_t port,uint32_t build,std::string* error) {
    auto& i=*impl_;
    if (host.empty() || host.size()>253 || !port || !build || !i.runtime.ok) {
        if (error) *error="Invalid ranking endpoint or socket initialization failed"; return false;
    }
    CloseSocket(i.socket);i.socket=kNoSocket;i.operation=i.phase=0;i.queryReady=i.submitReady=false;i.entries.clear();i.error.clear();
    i.build=build;i.submission={};i.run={};i.haveSubmission=i.submissionSent=false;i.resolution=std::make_shared<Resolution>();
    auto result=i.resolution;
    // The shared result outlives cancellation; shutdown never waits for OS DNS.
    std::thread([result,host,port] {
        SocketRuntime runtime;
        if (!runtime.ok) { result->state.store(-1,std::memory_order_release);return; }
        addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;
        addrinfo* addresses=nullptr;
        if (getaddrinfo(host.c_str(),nullptr,&hints,&addresses)!=0 || !addresses) {
            result->state.store(-1,std::memory_order_release);return;
        }
        result->address=*reinterpret_cast<const sockaddr_in*>(addresses->ai_addr);
        result->address.sin_port=htons(port);freeaddrinfo(addresses);
        result->state.store(1,std::memory_order_release);
    }).detach();
    return true;
}
void LeaderboardClient::Query(int table) {
    auto& i=*impl_;
    if (i.operation==2) return; // keep an explicitly published result's acknowledgement
    i.table=table;i.entries.clear();i.Start(1);
    if (table<0 || table>12 || !i.resolution) i.Finish("RANKING SERVICE NOT CONFIGURED");
}
bool LeaderboardClient::Submit(const LeaderboardSubmission& entry,std::string* error) {
    auto& i=*impl_;
    if (!Valid(entry) || !entry.completionToken || !i.resolution) { if (error) *error="Result is not eligible for this shared board";return false; }
    if (i.operation) { if (error) *error="A ranking request is already running";return false; }
    const auto& old=i.submission;
    const bool samePayload=old.table==entry.table && old.name==entry.name && old.score==entry.score &&
        old.rounds==entry.rounds && old.difficulty==entry.difficulty && old.players==entry.players;
    if(i.haveSubmission&&entry.completionToken<old.completionToken) {if(error)*error="RESULT IS FROM AN OLDER COMPLETED RUN";return false;}
    const bool sameRun=i.haveSubmission&&entry.completionToken==old.completionToken;
    if(sameRun&&i.submissionSent&&!samePayload) {if(error)*error="RUN ALREADY SENT AS "+old.name+"; RETRY THOSE INITIALS";return false;}
    i.table=entry.table;i.submission=entry;
    // A manual retry after an unavailable/lost acknowledgement uses the same
    // run ID, even after acceptance. Equal scores from a newer completion are
    // distinct runs. DNS/hello failures do not freeze an unsent payload.
    if (!sameRun) {
        for (auto& byte:i.run) byte=static_cast<uint8_t>(i.random());
        if(std::all_of(i.run.begin(),i.run.end(),[](uint8_t b){return b==0;}))i.run[0]=1;
        i.submissionSent=false;
    }
    i.haveSubmission=true;
    i.Start(2);return true;
}
void LeaderboardClient::Poll() {
    auto& i=*impl_;if (!i.operation) return;
    const auto now=Clock::now();
    if (now-i.started>std::chrono::seconds(6)) { i.Finish("RANKING SERVICE UNAVAILABLE");return; }
    if (!i.resolution || i.resolution->state.load(std::memory_order_acquire)<0) { i.Finish("RANKING ADDRESS UNAVAILABLE");return; }
    if (!i.OpenSocket()) return;
    if (i.lastSent==Clock::time_point{} || now-i.lastSent>=std::chrono::seconds(1)) {
        const int sent=::send(static_cast<SOCKET>(i.socket),reinterpret_cast<const char*>(i.outgoing.data()),static_cast<int>(i.outgoing.size()),0);
        if(i.operation==2&&i.phase==2&&sent==static_cast<int>(i.outgoing.size()))i.submissionSent=true;
        i.lastSent=now;
    }
    for (int n=0;n<16 && i.operation;++n) {
        uint8_t packet[256];
        const int count=::recv(static_cast<SOCKET>(i.socket),reinterpret_cast<char*>(packet),sizeof(packet),0);
        if (count<=0) break;
        if (count<10 || std::memcmp(packet,"H2RK\x01",5) || U32(packet+6)!=i.request) continue;
        if (packet[5]==101 && count==26 && i.phase==1) { i.WithCookie(packet+10);continue; }
        if (i.phase!=2) continue;
        if (packet[5]==102 && i.operation==1 && count>=17 && U32(packet+10)==i.build && packet[14]==i.table) {
            const uint8_t status=packet[15],rows=packet[16];
            if (rows>10 || count!=17+rows*11 || (status!=0 && status!=3 && status!=5) || (status && rows)) continue;
            std::vector<LeaderboardEntry> entries;
            bool valid=true;
            for (unsigned row=0;row<rows;++row) {
                const uint8_t* p=packet+17+row*11;
                LeaderboardSubmission e;e.table=i.table;e.name.assign(reinterpret_cast<const char*>(p),3);
                e.score=U32(p+3);e.rounds=U16(p+7);e.difficulty=p[9];e.players=p[10];
                if (!Valid(e)) { valid=false;break; }
                entries.push_back(e);
            }
            if (!valid) continue;
            i.entries=std::move(entries);i.Finish(status?SubmitError(status):"");
        } else if (packet[5]==103 && i.operation==2 && count==11 && packet[10]<=5) {
            i.accepted=packet[10]<=1;i.Finish(i.accepted?"":SubmitError(packet[10]));
        }
    }
}
bool LeaderboardClient::Busy() const { return impl_->operation!=0; }
bool LeaderboardClient::Ready() const { return impl_->queryReady; }
bool LeaderboardClient::SubmissionFinished() const { return impl_->submitReady; }
bool LeaderboardClient::LastSubmitAccepted() const { return impl_->accepted; }
uint64_t LeaderboardClient::Revision() const { return impl_->revision; }
int LeaderboardClient::Table() const { return impl_->table; }
const std::vector<LeaderboardEntry>& LeaderboardClient::Entries() const { return impl_->entries; }
const std::string& LeaderboardClient::Error() const { return impl_->error; }

} // namespace port::net
