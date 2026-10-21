#include "net/leaderboard.h"
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
    Require(argc>=2,"use leaderboard_client_integration.py to run this test against a temporary local server");
    const int port=std::atoi(argv[1]);Require(port>0 && port<65536,"invalid local test port");
    port::net::LeaderboardClient client;
    std::string error;
    Require(client.Configure("127.0.0.1",static_cast<uint16_t>(port),2026102001,&error),error.c_str());
    if (argc>2 && std::strcmp(argv[2],"unavailable")==0) {
        port::net::LeaderboardSubmission pending;pending.table=0;pending.name="AAA";pending.score=100;pending.completionToken=1;
        Require(client.Submit(pending,&error),"valid unavailable-service candidate did not start");Complete(client);
        Require(client.SubmissionFinished() && !client.Error().empty() && client.Entries().empty(),"unresponsive service did not finish with a clean error");
        pending.name="BBB";Require(client.Submit(pending,&error),"hello-only failure incorrectly froze an unsent candidate's initials");
        std::puts("PASS unavailable UDP service is bounded, fabricates no records and leaves unsent initials editable");return 0;
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
    entry.name="NEW";Require(!client.Submit(entry,&error)&&error.find("TST")!=std::string::npos,"sent candidate initials were changed without explaining the frozen payload");entry.name="TST";
    entry.completionToken=1;Require(!client.Submit(entry,&error),"older completed run was uploaded again");entry.completionToken=2;
    Require(client.Submit(entry,&error),"latest completion retry rejected");Complete(client);Require(client.LastSubmitAccepted(),"latest completion retry failed");
    client.Query(7);Complete(client);Require(client.Entries().size()==2,"repeated new run retry created a duplicate");
    client.Query(8);Complete(client);Require(client.Error().empty() && client.Entries().empty(),"query crossed boss board boundaries");
    entry.players=2;Require(!client.Submit(entry,&error),"two-player boss time was accepted");
    client.Query(99);Require(client.Ready() && !client.Error().empty(),"invalid table was not rejected immediately");
    std::puts("PASS local UDP DNS/cookie/query/manual-submit/results/board isolation/client validation");
}
