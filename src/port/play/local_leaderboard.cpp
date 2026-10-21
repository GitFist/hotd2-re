#include "play/local_leaderboard.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

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
