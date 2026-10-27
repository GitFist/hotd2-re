#include "play/score_submission_queue.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

namespace {
void Require(bool ok,const char* reason) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",reason);std::exit(1);}}
std::vector<char> Read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};
}
void Write(const std::filesystem::path& path,const std::vector<char>& bytes) {
    std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(bytes.data(),bytes.size());
    Require(bool(file),"fixture write failed");
}
void FixChecksum(std::vector<char>& bytes) {
    uint64_t value=14695981039346656037ull;
    for(size_t n=0;n<bytes.size()-8;++n){value^=uint8_t(bytes[n]);value*=1099511628211ull;}
    for(size_t n=0;n<8;++n)bytes[bytes.size()-8+n]=char(value>>(n*8));
}
port::net::LeaderboardSubmission Entry(const std::string& name,uint32_t score=123) {
    port::net::LeaderboardSubmission entry;entry.name=name;entry.score=score;return entry;
}
void StorageTests() {
    const auto id=port::net::NewLeaderboardRunId();std::string suffix;
    for(auto b:id)suffix+=std::to_string(b)+"-";
    const auto dir=std::filesystem::temp_directory_path()/("hotd2-score-queue-"+suffix);
    std::filesystem::create_directory(dir);
    const auto path=dir/"scores.txt";
    std::vector<char> saved;
    {
        play::ScoreSubmissionQueue queue;
        Require(queue.Load(path.string()),"missing queue did not initialize");
        play::ScoreSubmissionQueue concurrent;
        Require(!concurrent.Load(path.string()),"second client acquired the same score queue");
        auto entry=Entry("OLD NAME");Require(queue.Enqueue(entry),"first completion not queued");
        entry.name="NEW_NAME";Require(queue.Enqueue(entry),"second completion not queued");
        Require(queue.PendingCount()==2,"equal scores from distinct completions were collapsed");
        saved=Read(path);
        Require(saved.size()==20+2*39,"saved format has an unexpected length");
        Require(!std::equal(saved.begin()+12,saved.begin()+28,saved.begin()+51),"independent runs reused an ID");
        Require(std::string(saved.begin()+30,saved.begin()+38)=="OLD NAME","rename changed the first queued name");
    }
    {
        play::ScoreSubmissionQueue queue;Require(queue.Load(path.string())&&queue.PendingCount()==2,"restart lost completed runs");
        Require(Read(path)==saved,"loading rewrote run IDs or payloads");
    }
    auto damaged=saved;damaged[30]^=1;Write(path,damaged);
    {
        play::ScoreSubmissionQueue queue;
        Require(!queue.Load(path.string())&&!queue.Enqueue(Entry("NEW")),"corrupt saved queue was overwritten");
        Require(Read(path)==damaged,"corrupt queue was not preserved for recovery");
    }
    damaged=saved;std::copy_n(damaged.begin()+12,16,damaged.begin()+51);FixChecksum(damaged);Write(path,damaged);
    {play::ScoreSubmissionQueue queue;Require(!queue.Load(path.string()),"duplicate persisted IDs were accepted");}
    damaged.resize(15);Write(path,damaged);
    {play::ScoreSubmissionQueue queue;Require(!queue.Load(path.string()),"truncated queue was accepted");}
    Write(path,saved);
    {
        play::ScoreSubmissionQueue queue;Require(queue.Load(path.string()),"valid queue did not recover");
        std::filesystem::create_directory(path.string()+".tmp"); // makes the atomic staging write fail
        Require(queue.Enqueue(Entry("DISK_RETRY")),"queue discarded ownership on a temporary disk failure");
        Require(queue.Status().find("CANNOT SAVE")!=std::string::npos&&Read(path)==saved,"failed persistence modified prior scores");
        port::net::LeaderboardClient client;
        queue.Tick(client);
        Require(!client.Busy(),"unsaved result was transmitted");
        std::filesystem::remove(path.string()+".tmp");
        queue.Tick(client,play::ScoreSubmissionQueue::Clock::now()+std::chrono::seconds(10));
        Require(queue.PendingCount()==3&&Read(path)!=saved,"disk recovery did not save owned runs");
    }
    {play::ScoreSubmissionQueue queue;Require(queue.Load(path.string())&&queue.PendingCount()==3,"recovered score did not survive restart");}
    std::filesystem::remove_all(dir);
    std::puts("PASS persistent score queue: names/IDs, restart, exclusive ownership, corruption and disk-failure recovery");
}
void NetworkTests(int port,const std::filesystem::path& dir,const std::string& phase) {
    port::net::LeaderboardClient client;std::string error;
    Require(client.Configure("127.0.0.1",uint16_t(port),port::net::kLeaderboardCompatibilityId,&error),"client configure failed");
    const auto path=dir/(phase=="lost"||phase=="resume"?"lost.txt":"rejected.txt");
    {
        play::ScoreSubmissionQueue queue;Require(queue.Load(path.string()),"network queue load failed");
        if(phase=="lost")Require(queue.Enqueue(Entry("LOST_ACK",111)),"lost-ack run not queued");
        else if(phase=="resume") {
            Require(queue.PendingCount()==1,"lost-ack run did not persist across process restart");
            Require(queue.Enqueue(Entry("RENAMED",222)),"renamed new run not queued");
        } else {
            Require(queue.Enqueue(Entry("REJECTED"))&&queue.Enqueue(Entry("CUTOFF"))&&queue.Enqueue(Entry("BUSY",333)),"terminal/busy fixtures not queued");
        }
        const auto start=play::ScoreSubmissionQueue::Clock::now();
        size_t polls=0;
        while(play::ScoreSubmissionQueue::Clock::now()-start<std::chrono::seconds(9)) {
            queue.Tick(client,play::ScoreSubmissionQueue::Clock::now()+std::chrono::seconds(phase=="lost"?0:polls++));
            if(phase=="lost") {
                if(queue.Status().find("WILL SYNC")!=std::string::npos)break;
            } else if(queue.PendingCount()==0)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(phase=="lost")Require(queue.PendingCount()==1&&queue.Status().find("WILL SYNC")!=std::string::npos,"lost acknowledgement discarded the queued run");
        else if(phase=="resume")Require(queue.PendingCount()==0&&queue.Status()=="SCORE SUBMITTED","restart retry did not complete both runs");
        else Require(queue.PendingCount()==0&&queue.RejectedCount()==2,"terminal responses retried forever or transient response was discarded");
    }
    if(phase=="terminal") {
        play::ScoreSubmissionQueue queue;
        Require(queue.Load(path.string())&&queue.PendingCount()==0&&queue.RejectedCount()==2,"terminal results did not persist for review");
        queue.Tick(client);
        Require(!client.Busy()&&queue.Status().find("REJECTED")!=std::string::npos,"terminal records retried or became invisible on restart");
    }
    std::printf("PASS score queue network phase: %s\n",phase.c_str());
}
} // namespace
int main(int argc,char** argv) {
    if(argc==1){StorageTests();return 0;}
    Require(argc==4,"usage: score_submission_queue_test <local-port> <temporary-directory> <lost|resume|terminal>");
    const int port=std::atoi(argv[1]);Require(port>0&&port<65536,"invalid test port");
    NetworkTests(port,argv[2],argv[3]);
}
