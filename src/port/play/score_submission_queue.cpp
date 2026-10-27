#include "play/score_submission_queue.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace play {
namespace {
constexpr size_t kMaxEntries=10000,kRecordBytes=39;
constexpr char kHeader[]="H2SQ001\n";
void Put(std::vector<uint8_t>& bytes,uint64_t value,size_t count) {
    while(count--){bytes.push_back(uint8_t(value));value>>=8;}
}
uint64_t Read(const std::vector<uint8_t>& bytes,size_t& offset,size_t count) {
    uint64_t value=0;
    for(size_t n=0;n<count;++n)value|=uint64_t(bytes[offset++])<<(n*8);
    return value;
}
uint64_t Checksum(const std::vector<uint8_t>& bytes,size_t count) {
    uint64_t value=14695981039346656037ull;
    for(size_t n=0;n<count;++n){value^=bytes[n];value*=1099511628211ull;}
    return value;
}
bool WriteAtomically(const std::string& path,const std::vector<uint8_t>& bytes) {
    const auto destination=std::filesystem::path(path),temp=std::filesystem::path(path+".tmp");
    bool ok=false;
#ifdef _WIN32
    const HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;
    ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size();
    if(ok)ok=FlushFileBuffers(file)!=0;
    if(!CloseHandle(file))ok=false;
    if(ok)ok=MoveFileExW(temp.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    FILE* file=std::fopen(temp.c_str(),"wb");
    if(!file)return false;
    ok=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size()&&std::fflush(file)==0&&fsync(fileno(file))==0;
    if(std::fclose(file)!=0)ok=false;
    if(ok)ok=std::rename(temp.c_str(),destination.c_str())==0;
    if(ok) {
        auto parent=destination.parent_path();if(parent.empty())parent=".";
        const int dir=open(parent.c_str(),O_RDONLY);
        if(dir<0)ok=false;
        else {ok=fsync(dir)==0;close(dir);}
    }
#endif
    if(!ok){std::error_code ignored;std::filesystem::remove(temp,ignored);}
    return ok;
}
} // namespace

ScoreSubmissionQueue::~ScoreSubmissionQueue() {
    if(lease_==-1)return;
#ifdef _WIN32
    CloseHandle(reinterpret_cast<HANDLE>(lease_));
#else
    close(static_cast<int>(lease_));
#endif
}
void ScoreSubmissionQueue::SetStatus(const std::string& status) {
    if(status_!=status){status_=status;++revision_;}
}
bool ScoreSubmissionQueue::Load(const std::string& path) {
    // Loading is startup-only: never replace an active queue or an owned result.
    if(lease_!=-1||inFlight_||dirty_||!entries_.empty()||!rejected_.empty())return false;
    path_=path;writable_=false;failures_=0;nextAttempt_=nextSave_={};
    if(path.empty()){SetStatus("SCORE QUEUE PATH IS NOT CONFIGURED");return false;}
    const auto lockPath=std::filesystem::path(path+".lock");
#ifdef _WIN32
    const HANDLE lease=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(lease!=INVALID_HANDLE_VALUE)lease_=reinterpret_cast<intptr_t>(lease);
#else
    const int lease=open(lockPath.c_str(),O_RDWR|O_CREAT,0600);
    if(lease>=0) {
        if(flock(lease,LOCK_EX|LOCK_NB)==0)lease_=lease;
        else close(lease);
    }
#endif
    if(lease_==-1){SetStatus("SCORE QUEUE LOCKED OR NOT WRITABLE");return false;}
    std::error_code ec;
    const bool exists=std::filesystem::exists(path,ec);
    if(ec){SetStatus("CANNOT READ SAVED SCORE QUEUE");return false;}
    if(!exists){writable_=true;SetStatus("");return true;}
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    const auto size=file?file.tellg():std::streampos(-1);
    if(size<20||size>std::streamoff(20+kMaxEntries*kRecordBytes)) {
        SetStatus("CANNOT READ SAVED SCORE QUEUE");return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))) {
        SetStatus("CANNOT READ SAVED SCORE QUEUE");return false;
    }
    size_t offset=8;
    const auto count=Read(bytes,offset,4);
    size_t checksumOffset=bytes.size()-8;
    if(!std::equal(bytes.begin(),bytes.begin()+8,kHeader)||count>kMaxEntries||
       bytes.size()!=20+count*kRecordBytes||Read(bytes,checksumOffset,8)!=Checksum(bytes,bytes.size()-8)) {
        SetStatus("SAVED SCORE QUEUE NEEDS ATTENTION");return false;
    }
    std::deque<port::net::LeaderboardSubmission> loaded;
    std::deque<Rejected> rejected;
    std::set<std::array<uint8_t,16>> ids;
    for(size_t n=0;n<count;++n) {
        port::net::LeaderboardSubmission entry;
        std::copy_n(bytes.begin()+offset,16,entry.runId.begin());offset+=16;
        entry.table=int(Read(bytes,offset,1));const auto length=Read(bytes,offset,1);
        if(length<1||length>12){SetStatus("SAVED SCORE QUEUE NEEDS ATTENTION");return false;}
        entry.name.assign(reinterpret_cast<const char*>(bytes.data()+offset),static_cast<size_t>(length));
        const bool padding=std::all_of(bytes.begin()+offset+length,bytes.begin()+offset+12,[](uint8_t b){return b==0;});
        offset+=12;
        entry.score=uint32_t(Read(bytes,offset,4));entry.rounds=uint16_t(Read(bytes,offset,2));
        entry.difficulty=uint8_t(Read(bytes,offset,1));entry.players=uint8_t(Read(bytes,offset,1));
        const uint8_t status=uint8_t(Read(bytes,offset,1));
        if(!padding||!port::net::ValidLeaderboardSubmission(entry)||
           (status!=0&&status!=2&&status!=4)||
           std::all_of(entry.runId.begin(),entry.runId.end(),[](uint8_t b){return b==0;})||!ids.insert(entry.runId).second) {
            SetStatus("SAVED SCORE QUEUE NEEDS ATTENTION");return false;
        }
        if(status)rejected.push_back({std::move(entry),status});
        else loaded.push_back(std::move(entry));
    }
    entries_=std::move(loaded);rejected_=std::move(rejected);writable_=true;++revision_;
    SetStatus(!entries_.empty()?"SCORE SAVED / WAITING TO SYNC":!rejected_.empty()?"REJECTED SCORE SAVED FOR REVIEW":"");return true;
}
bool ScoreSubmissionQueue::Save() {
    if(!writable_)return false;
    std::vector<uint8_t> bytes(kHeader,kHeader+8);
    Put(bytes,entries_.size()+rejected_.size(),4);
    const auto append=[&](const port::net::LeaderboardSubmission& entry,uint8_t status) {
        bytes.insert(bytes.end(),entry.runId.begin(),entry.runId.end());
        Put(bytes,entry.table,1);Put(bytes,entry.name.size(),1);
        bytes.insert(bytes.end(),entry.name.begin(),entry.name.end());bytes.insert(bytes.end(),12-entry.name.size(),0);
        Put(bytes,entry.score,4);Put(bytes,entry.rounds,2);Put(bytes,entry.difficulty,1);Put(bytes,entry.players,1);
        Put(bytes,status,1);
    };
    for(const auto& entry:entries_)append(entry,0);
    for(const auto& rejected:rejected_)append(rejected.entry,rejected.status);
    Put(bytes,Checksum(bytes,bytes.size()),8);
    if(!WriteAtomically(path_,bytes)){SetStatus("CANNOT SAVE SCORE / WILL RETRY");return false;}
    dirty_=false;return true;
}
bool ScoreSubmissionQueue::Enqueue(const port::net::LeaderboardSubmission& input) {
    if(!writable_)return false;
    if(!port::net::ValidLeaderboardSubmission(input)){SetStatus("SCORE NOT ELIGIBLE");return false;}
    if(entries_.size()+rejected_.size()>=kMaxEntries){SetStatus("SAVED SCORE QUEUE IS FULL");return false;}
    auto entry=input;
    // Caller tokens and IDs are not identities for new completions. The queue
    // creates one ID and owns it through every retry and application restart.
    do {entry.runId=port::net::NewLeaderboardRunId();}
    while(std::any_of(entries_.begin(),entries_.end(),[&](const auto& old){return old.runId==entry.runId;})||
          std::any_of(rejected_.begin(),rejected_.end(),[&](const auto& old){return old.entry.runId==entry.runId;}));
    entry.completionToken=0;
    entries_.push_back(std::move(entry));dirty_=true;++revision_;
    if(Save())SetStatus("SCORE SAVED / WAITING TO SYNC");
    return true;
}
void ScoreSubmissionQueue::Tick(port::net::LeaderboardClient& client,Clock::time_point now) {
    client.Poll();
    if(!writable_)return;
    if(inFlight_&&client.SubmissionFinished()) {
        inFlight_=false;
        if(client.LastSubmitAccepted()||!client.LastSubmitRetryable()) {
            const std::string result=client.LastSubmitAccepted()?"SCORE SUBMITTED":client.Error();
            if(!client.LastSubmitAccepted())rejected_.push_back({entries_.front(),client.LastSubmitStatus()});
            entries_.pop_front();dirty_=true;failures_=0;++revision_;
            nextAttempt_=now+std::chrono::seconds(10); // respect the server's write budget during backlog sync
            if(Save())SetStatus(!client.LastSubmitAccepted()?result+" / SAVED FOR REVIEW":
                               entries_.empty()?result:"SCORE SUBMITTED / SYNCING SAVED RUNS");
        } else {
            failures_=std::min(failures_+1,5u);
            nextAttempt_=now+std::chrono::seconds(10u<<(failures_-1));
            SetStatus("SCORE SAVED / WILL SYNC WHEN ONLINE");
        }
    }
    if(dirty_) {
        if(now<nextSave_)return;
        nextSave_=now+std::chrono::seconds(5);
        if(!Save())return;
        SetStatus(!entries_.empty()?"SCORE SAVED / WAITING TO SYNC":
                  !rejected_.empty()?"REJECTED SCORE SAVED FOR REVIEW":"SCORE SUBMITTED");
    }
    if(entries_.empty()||inFlight_||client.Busy()||now<nextAttempt_)return;
    std::string error;
    if(client.Submit(entries_.front(),&error)) {
        inFlight_=true;SetStatus("SUBMITTING SCORE...");
    } else {
        nextAttempt_=now+std::chrono::seconds(10);
        SetStatus("SCORE SAVED / WILL SYNC WHEN ONLINE");
    }
}
} // namespace play
