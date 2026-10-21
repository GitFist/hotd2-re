bool D3D11Device::WarmupGraphics(GraphicsWarmupStats* output,std::string* error) {
    if(frameActive_){if(error)*error="Warm graphics states between frames";return false;}
    const auto start=std::chrono::steady_clock::now();
    auto resident=[&]{GraphicsWarmupStats result;
        for(const auto& item:blends_)if(item.second)++result.stateObjectsResident;
        for(const auto& item:depths_)if(item.second)++result.stateObjectsResident;
        for(const auto& item:samplers_)if(item.second)++result.samplersResident;
        return result;};
    const auto before=resident();bool okay=true;
    for(int depth=1;depth<=8;++depth)for(int write:{0,1})okay=(DepthState(depth,write!=0)!=nullptr)&&okay;
    for(const auto& draw:CommonWarmupDrawStates())okay=(BlendState(draw.state.blendEnable!=0,draw.state.srcBlend,draw.state.dstBlend)!=nullptr)&&okay;
    // Native opaque TSP records retain ZERO/ZERO factors even with blending
    // disabled; keep that real descriptor warm without changing the Off cache.
    okay=(BlendState(false,1,1)!=nullptr)&&okay;
    if(graphics_.alphaToCoverage&&graphics_.msaa>1)okay=(BlendState(false,2,1,true)!=nullptr)&&okay;
    for(int linear:{0,1})for(int u=1;u<=3;++u)for(int v=1;v<=3;++v)for(int mip:{0,1})
        okay=(Sampler(linear!=0,u,v,mip!=0)!=nullptr)&&okay;
    auto result=resident();result.stateObjectsCreated=result.stateObjectsResident-before.stateObjectsResident;
    result.samplersCreated=result.samplersResident-before.samplersResident;
    result.cpuMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    if(output)*output=result;
    if(!okay&&error)*error="Some common graphics states could not be warmed; lazy creation remains available";
    return okay;
}
