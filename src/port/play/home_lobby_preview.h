#pragma once

// Read-only Home directory policy, shared by play.exe and its local-service
// integration test. A preview never opens a peer connection or joins a room.
#include "net/netplay.h"
#include <algorithm>

namespace play {
// A directory reply can predate the host's Back action. Keep cancelled IDs out
// of both the visible rows and the cache used to route a room click.
inline void PruneCancelledHomeRooms(std::vector<port::net::LobbyRoom>& rooms,const port::net::Session& session) {
    std::erase_if(rooms,[&](const auto& room){return session.IsCancelledLobbyRoom(room.id);});
}
inline std::vector<game::PortHomeRoom> HomeLobbyRooms(const std::vector<port::net::LobbyRoom>& rooms,uint32_t build) {
    std::vector<game::PortHomeRoom> out;
    for(const auto& room:rooms) {
        if(!room.id || room.build!=build || (room.flags&port::net::kRoomUnlisted))continue;
        if(std::any_of(out.begin(),out.end(),[&](const auto& r){return r.id==room.id;}))continue;
        out.push_back({room.id,(room.flags&port::net::kRoomPrivate)!=0,(room.flags&port::net::kRoomUnlimited)!=0,
                       room.build,port::net::RoomDifficulty(room.flags),room.typing});
        if(out.size()==2)break;
    }
    return out;
}
enum class HomeRoomAction { Browse, OtherVersion, PrivateCode, JoinPublic };
inline HomeRoomAction PlanHomeRoom(const std::vector<port::net::LobbyRoom>& rooms,uint32_t id,uint32_t build) {
    auto found=std::find_if(rooms.begin(),rooms.end(),[&](const auto& room){return room.id==id&&id!=0;});
    if(found==rooms.end() || (found->flags&port::net::kRoomUnlisted))return HomeRoomAction::Browse;
    if(found->build!=build)return HomeRoomAction::OtherVersion;
    return found->flags&port::net::kRoomPrivate?HomeRoomAction::PrivateCode:HomeRoomAction::JoinPublic;
}
} // namespace play
