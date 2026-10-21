bool SdlGpuDevice::WarmupGraphics(GraphicsWarmupStats* output,std::string* error) {
    if(cmd_){if(error)*error="Warm graphics states between frames";return false;}
    const auto start=std::chrono::steady_clock::now();
    auto resident=[&]{GraphicsWarmupStats result;
        result.pipelinesResident=static_cast<uint32_t>(pipelines_.size()+depthPipelines_.size()+motionPipelines_.size()+reactivePipelines_.size()+reflectionSurfacePipelines_.size());
        for(auto* pipeline:shadowMapPipelines_)if(pipeline)++result.pipelinesResident;
        for(auto* pipeline:aoPipelines_)if(pipeline)++result.pipelinesResident;
        result.samplersResident=static_cast<uint32_t>(samplers_.size());return result;};
    const auto before=resident();bool okay=true;
    const bool savedHud=hudReady_;hudReady_=false;
    const auto draws=CommonWarmupDrawStates();
    for(const auto& draw:draws){
        okay=(Pipeline(draw.pipeline,draw.state)!=nullptr)&&okay;
        if(graphics_.temporalAA)okay=(ReactivePipeline(draw.pipeline,draw.state)!=nullptr)&&okay;
        if(draw.pipeline==GpuDraw::Pipeline::Model&&!draw.state.blendEnable&&draw.state.zwrite){
            if(graphics_.alphaToCoverage&&graphics_.msaa>1)okay=(Pipeline(draw.pipeline,draw.state,true)!=nullptr)&&okay;
            if(graphics_.softParticles||graphics_.ambientOcclusion||graphics_.temporalAA||SceneEffectsEnabled(graphics_))okay=(DepthPipeline(draw.state)!=nullptr)&&okay;
            if(graphics_.temporalAA)okay=(MotionPipeline(draw.state)!=nullptr)&&okay;
            if(graphics_.reflections)okay=(ReflectionSurfacePipeline(draw.state)!=nullptr)&&okay;
            if(graphics_.shadowMaps||graphics_.volumetricFog)okay=(ShadowMapPipeline(draw.state.cull)!=nullptr)&&okay;
        }
    }
    if(graphics_.ambientOcclusion)okay=(AoPipeline()!=nullptr)&&okay;
    if(graphics_.outputResolutionHud){hudReady_=true;for(const auto& draw:draws)if(draw.pipeline==GpuDraw::Pipeline::Sprite)okay=(Pipeline(draw.pipeline,draw.state)!=nullptr)&&okay;}
    hudReady_=savedHud;
    for(int linear:{0,1})for(int u=1;u<=3;++u)for(int v=1;v<=3;++v)for(int mip:{0,1}){
        GpuState sampler;sampler.linear=static_cast<uint8_t>(linear);sampler.addrU=static_cast<uint8_t>(u);sampler.addrV=static_cast<uint8_t>(v);sampler.mipmapped=static_cast<uint8_t>(mip);
        okay=(Sampler(sampler)!=nullptr)&&okay;
    }
    auto result=resident();result.pipelinesCreated=result.pipelinesResident-before.pipelinesResident;
    result.samplersCreated=result.samplersResident-before.samplersResident;
    result.cpuMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    if(output)*output=result;
    if(!okay&&error)*error=std::string("Some common graphics pipelines could not be warmed: ")+SDL_GetError();
    return okay;
}
