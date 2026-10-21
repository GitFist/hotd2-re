#pragma once
#include "gpu_device.h"
#include <utility>

namespace port {
struct WarmupDrawState { GpuDraw::Pipeline pipeline;GpuState state; };
inline std::vector<WarmupDrawState> CommonWarmupDrawStates() {
    std::vector<WarmupDrawState> states;states.reserve(39);
    for(int cull=1;cull<=3;++cull){
        GpuState opaque;opaque.cull=static_cast<uint8_t>(cull);
        states.push_back({GpuDraw::Pipeline::Model,opaque});
        for(auto blend:{std::pair<int,int>{5,6},{5,2},{2,6}})for(int write:{0,1}){
            auto alpha=opaque;alpha.blendEnable=1;alpha.srcBlend=static_cast<uint8_t>(blend.first);
            alpha.dstBlend=static_cast<uint8_t>(blend.second);alpha.zwrite=static_cast<uint8_t>(write);
            states.push_back({GpuDraw::Pipeline::Model,alpha});
        }
    }
    for(auto blend:{std::pair<int,int>{9,1},{1,3}}){
        GpuState shadow;shadow.blendEnable=1;shadow.srcBlend=static_cast<uint8_t>(blend.first);
        shadow.dstBlend=static_cast<uint8_t>(blend.second);shadow.zwrite=0;
        states.push_back({GpuDraw::Pipeline::Model,shadow});
    }
    for(int depth:{4,8})for(int write:{0,1})for(int blend=0;blend<4;++blend){
        GpuState sprite;sprite.zfunc=static_cast<uint8_t>(depth);sprite.zwrite=static_cast<uint8_t>(write);
        sprite.addrU=sprite.addrV=3;
        if(blend){sprite.blendEnable=1;sprite.srcBlend=blend==3?2:5;sprite.dstBlend=blend==2?2:6;}
        states.push_back({GpuDraw::Pipeline::Sprite,sprite});
    }
    return states;
}
} // namespace port
