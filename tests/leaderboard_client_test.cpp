#include "net/leaderboard.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

static void Require(bool ok,const char* reason) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1); } }
static void Complete(port::net::LeaderboardClient& client) {
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while (client.Busy() && std::chrono::steady_clock::now()<until) {
        client.Poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(!client.Busy(),"client did not finish within its bounded timeout");
}
int main(int argc,char** argv) {
    if(argc==5 && std::strcmp(argv[1],"--read-only")==0) {
        const int port=std::atoi(argv[3]);Require(port>0&&port<65536,"invalid query port");
        const auto build=static_cast<uint32_t>(std::strtoul(argv[4],nullptr,10));
        port::net::LeaderboardClient client;std::string error;
        Require(client.Configure(argv[2],static_cast<uint16_t>(port),build,&error),error.c_str());
        for(int board=0;board<13;++board) {
            client.Query(board);Complete(client);
            Require(client.Ready()&&client.Error().empty(),client.Error().c_str());
            std::printf("PASS read-only board %d: %zu records\n",board,client.Entries().size());
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
        }
        return 0;
    }
    Require(argc>=2,"use leaderboard_client_integration.py to run this test against a temporary local server");
    const int port=std::atoi(argv[1]);Require(port>0 && port<65536,"invalid local test port");
    port::net::LeaderboardClient client;
    std::string error;
    Require(client.Configure("127.0.0.1",static_cast<uint16_t>(port),2026102001,&error),error.c_str());
    if (argc>2 && std::strcmp(argv[2],"unavailable")==0) {
        port::net::LeaderboardSubmission pending;pending.table=0;pending.name="AAA";pending.score=100;pending.completionToken=1;
        Require(client.Submit(pending,&error),"valid unavailable-service candidate did not start");Complete(client);
        Require(client.SubmissionFinished() && !client.Error().empty() && client.Entries().empty(),"unresponsive service did not finish with a clean error");
        pending.name="LONG_NAME-12";Require(client.Submit(pending,&error),"hello-only failure incorrectly froze an unsent candidate's name");
        std::puts("PASS unavailable UDP service is bounded, fabricates no records and leaves unsent names editable");return 0;
    }
    client.Query(7);Complete(client);
    Require(client.Ready() && client.Error().empty() && client.Entries().empty(),"fresh board query failed");
    port::net::LeaderboardSubmission entry;
    entry.table=7;entry.name="TST";entry.score=12345;entry.difficulty=2;
    Require(!client.Submit(entry,&error),"submission without a new-run token accepted");entry.completionToken=1;
    Require(client.Submit(entry,&error),error.c_str());Complete(client);
    Require(client.SubmissionFinished() && client.LastSubmitAccepted() && client.Error().empty(),"explicit result submission failed");
    client.Query(7);Complete(client);
    Require(client.Ready() && client.Error().empty() && client.Entries().size()==1,"submitted result did not round-trip");
    const auto& result=client.Entries()[0];
    Require(result.name=="TST" && result.score==12345 && result.difficulty==2 && result.players==1,"result metadata changed in transit");
    Require(client.Submit(entry,&error),"same completion retry rejected");Complete(client);Require(client.LastSubmitAccepted(),"same completion did not retain run identity");
    client.Query(7);Complete(client);Require(client.Entries().size()==1,"retry after acceptance uploaded a second copy");
    entry.completionToken=2;Require(client.Submit(entry,&error),"different equal-valued run rejected");Complete(client);Require(client.LastSubmitAccepted(),"new equal-valued completion was not accepted");
    client.Query(7);Complete(client);Require(client.Entries().size()==2,"independent equal-valued run was deduplicated as an old run");
    entry.name="NEW";Require(!client.Submit(entry,&error)&&error.find("TST")!=std::string::npos,"sent candidate name was changed without explaining the frozen payload");entry.name="TST";
    entry.completionToken=1;Require(!client.Submit(entry,&error),"older completed run was uploaded again");entry.completionToken=2;
    Require(client.Submit(entry,&error),"latest completion retry rejected");Complete(client);Require(client.LastSubmitAccepted(),"latest completion retry failed");
    client.Query(7);Complete(client);Require(client.Entries().size()==2,"repeated new run retry created a duplicate");
    entry.completionToken=3;entry.name="LONG_NAME-12";entry.score=12346;
    Require(entry.name.size()==port::net::kLeaderboardMaxNameLength,"test must exercise all twelve name bytes");
    Require(client.Submit(entry,&error),"twelve-character name was rejected");Complete(client);Require(client.LastSubmitAccepted(),"long name submission failed");
    client.Query(7);Complete(client);
    auto longResult=std::find_if(client.Entries().begin(),client.Entries().end(),[&](const auto& row){return row.name==entry.name;});
    Require(client.Error().empty()&&client.Entries().size()==3&&longResult!=client.Entries().end()&&longResult->score==entry.score,
            "exact twelve-character name did not round-trip beside legacy three-character names");
    Require(client.Submit(entry,&error),"long-name completion retry rejected");Complete(client);Require(client.LastSubmitAccepted(),"long-name retry failed");
    client.Query(7);Complete(client);Require(client.Entries().size()==3,"long-name retry created a duplicate");
    const std::string frozen=entry.name;entry.name="OTHER_NAME";
    Require(!client.Submit(entry,&error)&&error.find(frozen)!=std::string::npos,"retry did not preserve the full original name");
    entry.name="Z";entry.completionToken=4;entry.score=12347;
    Require(client.Submit(entry,&error),"one-character name was rejected");Complete(client);Require(client.LastSubmitAccepted(),"short-name submission failed");
    client.Query(7);Complete(client);
    Require(client.Entries().size()==4&&std::any_of(client.Entries().begin(),client.Entries().end(),[](const auto& row){return row.name=="Z";}),
            "null-padded short name did not round-trip without padding");
    entry.completionToken=5;
    for(const auto& invalid:std::vector<std::string>{"",std::string(13,'A')," ",std::string(12,' '),"lowercase","BAD/NAME",std::string("A\0B",3),std::string(1,char(0xFF))}) {
        entry.name=invalid;
        Require(!client.Submit(entry,&error)&&!client.Busy(),"invalid empty/overlong/blank/lowercase/unsupported name reached the network");
    }
    entry.name="TST";
    client.Query(9);Complete(client);
    Require(client.Error().empty()&&client.Entries().size()==10,"maximum 217-byte response did not decode all ten rows");
    for(size_t row=0;row<client.Entries().size();++row)
        Require(client.Entries()[row].name=="LONG_NAME-0"+std::to_string(row),"maximum-size response truncated or shifted a twelve-character name");
    client.Query(8);Complete(client);Require(client.Error().empty() && client.Entries().empty(),"query crossed boss board boundaries");
    entry.players=2;Require(!client.Submit(entry,&error),"two-player boss time was accepted");
    client.Query(99);Require(client.Ready() && !client.Error().empty(),"invalid table was not rejected immediately");
    std::puts("PASS local UDP v2: 1/3/12-character names, frozen retry payloads, no duplicate runs, invalid-name rejection, maximum response and malformed packet validation");
}
