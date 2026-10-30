#include "play/local_leaderboard.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include "net/leaderboard.h"

namespace play {
namespace {
bool Valid(const game::HordeLeaderboardEntry& r) {
    return r.rounds>=0&&r.rounds<=1000000&&r.score>=0&&r.score<=2000000000&&r.players>=1&&r.players<=2&&r.difficulty>=0&&r.difficulty<=4;
}
void Sort(std::vector<game::HordeLeaderboardEntry>& rows) {
    std::stable_sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.rounds!=b.rounds?a.rounds>b.rounds:a.score>b.score;});
    if(rows.size()>10)rows.resize(10);
}
} // namespace
bool LocalTypingLeaderboard::Load(const std::string& path) {
    std::ifstream file(path); std::string line;
    if (!std::getline(file,line) || line!="HOTD2-TYPING-1") return false;
    std::vector<game::SharedLeaderboardEntry> rows;
    while (std::getline(file,line)) {
        if (rows.size()>=10 || line.size()>128) return false;
        std::istringstream in(line); port::net::LeaderboardSubmission row; std::string extra;
        unsigned difficulty=0,players=0;
        row.table=port::net::kTypingLeaderboardTable;
        if (!(in>>std::quoted(row.name)>>row.score>>difficulty>>players) || (in>>extra) || difficulty>6 || players>2) return false;
        row.difficulty=uint8_t(difficulty); row.players=uint8_t(players);
        if (!port::net::ValidLeaderboardSubmission(row)) return false;
        rows.push_back({row.name,int(row.score),0,int(difficulty),int(players)});
    }
    if (!file.eof()) return false;
    std::stable_sort(rows.begin(),rows.end(),[](const auto& a,const auto& b) { return a.score>b.score; });
    entries_=std::move(rows); return true;
}
bool LocalTypingLeaderboard::Save(const std::string& path) const {
    if (path.empty()) return false;
    const auto destination=std::filesystem::path(path),temp=std::filesystem::path(path+".tmp");
    {
        std::ofstream file(temp,std::ios::trunc); file<<"HOTD2-TYPING-1\n";
        for (const auto& row:entries_) file<<std::quoted(row.name)<<' '<<row.score<<' '<<row.difficulty<<' '<<row.players<<'\n';
        file.close(); if (!file) return false;
    }
    std::error_code ec;
    std::filesystem::copy_file(temp,destination,std::filesystem::copy_options::overwrite_existing,ec);
    if (ec) return false;
    std::filesystem::remove(temp,ec); return true;
}
bool LocalTypingLeaderboard::Observe(const game::LeaderboardRunSnapshot& run,const std::string& name) {
    if (!run.sequence || run.sequence<=sequence_) return false;
    sequence_=run.sequence;
    if (!run.eligible || run.table!=port::net::kTypingLeaderboardTable ||
        run.players<1 || run.players>2 || run.difficulty<0 || run.difficulty>6) return false;
    port::net::LeaderboardSubmission row; row.table=run.table; row.name=name;
    row.score=uint32_t(std::max(0,run.score)); row.players=uint8_t(run.players); row.difficulty=uint8_t(run.difficulty);
    if (!port::net::ValidLeaderboardSubmission(row)) return false;
    entries_.push_back({row.name,int(row.score),0,run.difficulty,run.players});
    std::stable_sort(entries_.begin(),entries_.end(),[](const auto& a,const auto& b) { return a.score>b.score; });
    if (entries_.size()>10) entries_.resize(10);
    return true;
}
bool LocalHordeLeaderboard::Load(const std::string& path) {
    std::ifstream f(path); std::string line;
    if(!std::getline(f,line)||line!="HOTD2-HORDE-1")return false;
    std::vector<game::HordeLeaderboardEntry> rows;
    while(std::getline(f,line)) {
        if(rows.size()>=10||line.size()>128)return false;
        std::istringstream in(line);game::HordeLeaderboardEntry r;std::string extra;
        if(!(in>>r.rounds>>r.score>>r.players>>r.difficulty)||(in>>extra)||!Valid(r))return false;
        rows.push_back(r);
    }
    if(!f.eof())return false;
    Sort(rows);entries_=std::move(rows);return true;
}
bool LocalHordeLeaderboard::Save(const std::string& path) const {
    if(path.empty())return false;
    const auto destination=std::filesystem::path(path),temp=std::filesystem::path(path+".tmp");
    {
        std::ofstream f(temp,std::ios::trunc);
        f<<"HOTD2-HORDE-1\n";
        for(const auto& r:entries_)f<<r.rounds<<' '<<r.score<<' '<<r.players<<' '<<r.difficulty<<'\n';
        f.close();if(!f)return false;
    }
    std::error_code ec;
    // copy_file's overwrite works on Windows too; a failed replacement retains the
    // temporary file and reports failure rather than deleting the previous scores.
    std::filesystem::copy_file(temp,destination,std::filesystem::copy_options::overwrite_existing,ec);
    if(ec)return false;
    std::filesystem::remove(temp,ec);return true;
}
bool LocalHordeLeaderboard::Observe(const game::HordeRunSnapshot& run) {
    if(!run.active)return Finish();
    if(!active_){active_=true;debug_=run.debug;pending_={};pending_.players=1;pending_.difficulty=run.difficulty;}
    debug_|=run.debug;
    pending_.rounds=std::max(pending_.rounds,run.rounds);
    pending_.score=std::max(pending_.score,run.score);
    pending_.players=std::max(pending_.players,run.players);
    return false;
}
bool LocalHordeLeaderboard::Finish() {
    if(!active_)return false;
    active_=false;
    if(debug_||!Valid(pending_)||(pending_.rounds==0&&pending_.score==0))return false;
    lastCompleted_=pending_;
    entries_.push_back(pending_);Sort(entries_);return true;
}
} // namespace play
