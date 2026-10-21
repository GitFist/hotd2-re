#pragma once
#include <string>
namespace port {
#include "third_party/cmaa2_embedded.inc"
inline std::string CmaaShaderSource(bool sdl=false) {
    std::string source="#define CMAA2_UAV_STORE_TYPED 1\n#define CMAA2_UAV_STORE_TYPED_UNORM_FLOAT 1\n#define CMAA2_UAV_STORE_CONVERT_TO_SRGB 1\n#define CMAA2_EDGE_UNORM 0\n#define CMAA2_STATIC_QUALITY_PRESET 2\n";
    for(const char* chunk:kCmaaLibraryChunks)source+=chunk;
    if(sdl) {
        const char* oldRegisters[]={"register( u0 )","register( u1 )","register( u2 )","register( u3 )","register( u4 )","register( u5 )","register( u6 )","register( u7 )","register( t0 )"};
        const char* newRegisters[]={"register(u0,space1)","register(u1,space1)","register(u3,space1)","register(u4,space1)","register(u5,space1)","register(u2,space1)","register(u6,space1)","register(u7,space1)","register(t0,space0)"};
        for(int slot=0;slot<9;++slot){size_t at=0;while((at=source.find(oldRegisters[slot],at))!=std::string::npos){source.replace(at,std::string(oldRegisters[slot]).size(),newRegisters[slot]);at+=std::string(newRegisters[slot]).size();}}
    }
    return source;
}
} // namespace port
