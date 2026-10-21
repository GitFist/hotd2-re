// Optional scene effects use single-sample intermediates, then write the result
// to the existing main target before HUD submission (including under MSAA).
bool SdlGpuDevice::EnsureSceneEffects(std::string* error) {
    if(!EnsurePostTarget(sceneEffectsTargets_[0],width_,height_)||!EnsurePostTarget(sceneEffectsTargets_[1],width_,height_)||
       !EnsurePostTarget(reflectionSurface_,width_,height_,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT))return false;
    for(int k=0;k<3;++k){if(sceneEffectsPipelines_[k])continue;
        SDL_GPUColorTargetDescription target{};target.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=presentVs_;info.fragment_shader=sceneEffectsPs_[k];
        info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
        info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;info.target_info.color_target_descriptions=&target;info.target_info.num_color_targets=1;
        sceneEffectsPipelines_[k]=SDL_CreateGPUGraphicsPipeline(dev_,&info);if(!sceneEffectsPipelines_[k]){if(error)*error=SDL_GetError();return false;}}
    return true;
}

SDL_GPUGraphicsPipeline* SdlGpuDevice::ReflectionSurfacePipeline(const GpuState& state) {
    const int key=state.zfunc|(state.cull<<4);auto found=reflectionSurfacePipelines_.find(key);if(found!=reflectionSurfacePipelines_.end())return found->second;
    SDL_GPUVertexBufferDescription buffer{};buffer.slot=0;buffer.pitch=sizeof(game::ModelVertex);buffer.input_rate=SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[]={{0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,0},{1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,12},{2,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,24}};
    SDL_GPUColorTargetDescription color{};color.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=reflectionSurfaceVs_;info.fragment_shader=reflectionSurfacePs_;
    info.vertex_input_state.vertex_buffer_descriptions=&buffer;info.vertex_input_state.num_vertex_buffers=1;info.vertex_input_state.vertex_attributes=attributes;info.vertex_input_state.num_vertex_attributes=3;
    info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
    info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.front_face=SDL_GPU_FRONTFACE_CLOCKWISE;info.rasterizer_state.enable_depth_clip=true;
    info.rasterizer_state.cull_mode=state.cull<=1?SDL_GPU_CULLMODE_NONE:state.cull==2?SDL_GPU_CULLMODE_FRONT:SDL_GPU_CULLMODE_BACK;
    info.depth_stencil_state.enable_depth_test=true;info.depth_stencil_state.enable_depth_write=false;
    info.depth_stencil_state.compare_op=static_cast<SDL_GPUCompareOp>(ReversedDepthCompare(state.zfunc));
    info.target_info.color_target_descriptions=&color;info.target_info.num_color_targets=1;info.target_info.depth_stencil_format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;info.target_info.has_depth_stencil_target=true;
    auto* pipeline=SDL_CreateGPUGraphicsPipeline(dev_,&info);if(pipeline)reflectionSurfacePipelines_[key]=pipeline;return pipeline;
}

void SdlGpuDevice::DrawReflectionSurface(const GpuDrawList& list) {
    if(!graphics_.reflections||!opaqueReady_||!list.opaqueDrawCount||!EnsureSceneEffects())return;
    SDL_GPUColorTargetInfo color{};color.texture=reflectionSurface_.texture;color.load_op=reflectionSurfaceCleared_?SDL_GPU_LOADOP_LOAD:SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};depth.texture=opaqueDepth_;depth.load_op=SDL_GPU_LOADOP_LOAD;depth.store_op=SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    auto* pass=SDL_BeginGPURenderPass(cmd_,&color,1,&depth);if(!pass)return;reflectionSurfaceCleared_=true;
    const SDL_GPUViewport viewport={0,0,float(width_),float(height_),0,1};SDL_SetGPUViewport(pass,&viewport);
    for(size_t k=0;k<list.opaqueDrawCount&&k<list.draws.size();++k){const auto& draw=list.draws[k];
        if(draw.pipeline!=GpuDraw::Pipeline::Model||draw.state.blendEnable||!draw.state.zwrite||draw.constants>=list.model.size()||list.model[draw.constants].localCount[2]<.5f)continue;
        auto* vertices=draw.stream==GpuDraw::Stream::Static?staticVb_:dynVb_;auto* pipeline=ReflectionSurfacePipeline(draw.state);if(!vertices||!pipeline)continue;
        SDL_BindGPUGraphicsPipeline(pass,pipeline);const SDL_GPUBufferBinding buffer={vertices,0};SDL_BindGPUVertexBuffers(pass,0,&buffer,1);
        SDL_PushGPUVertexUniformData(cmd_,0,&list.model[draw.constants],sizeof(ModelConstants));SDL_PushGPUFragmentUniformData(cmd_,0,&list.model[draw.constants],sizeof(ModelConstants));
        const auto sampler=Sampler(draw.state);const SDL_GPUTextureSamplerBinding textures[]={{draw.texture&&draw.texture<=textures_.size()?textures_[draw.texture-1]:white_,sampler},
            {draw.normalRoughnessTexture&&draw.normalRoughnessTexture<=textures_.size()?textures_[draw.normalRoughnessTexture-1]:white_,sampler}};
        SDL_BindGPUFragmentSamplers(pass,0,textures,2);SDL_DrawGPUPrimitives(pass,draw.count,1,draw.first,0);
    }
    SDL_EndGPURenderPass(pass);
}

bool SdlGpuDevice::ProcessSceneEffects(std::string* error) {
    if(!ResolveTemporal(error))return false;
    if(sceneEffectsApplied_||!SceneEffectsEnabled(graphics_))return true;
    if(!cmd_)return false;
    if(!opaqueReady_){sceneEffectsApplied_=true;return true;} // menus/clear-only scene: nothing to integrate
    if(!EnsureSceneEffects(error))return false;
    SceneEffectsConstants constants{};
    if(!MakeSceneEffectsConstants(width_,height_,sceneEffectsCamera_,sceneEffectsFrame_,shadowMapConstants_,graphics_,
        sceneEffectsJitter_,&constants))return true;
    sceneEffectsApplied_=true;
    const bool active[3]={graphics_.reflections>0&&reflectionSurfaceCleared_,graphics_.volumetricFog>0,constants.settings[2]>.5f};
    if(!active[0]&&!active[1]&&!active[2])return true;
    auto* copyPipeline=TaaCopyPipeline(graphics_.msaa);if(!copyPipeline){if(error)*error=SDL_GetError();return false;}
    SDL_GPUTexture* source=color_;int output=0;const SDL_GPUViewport viewport={0,0,float(width_),float(height_),0,1};
    for(int k=0;k<3;++k){if(!active[k])continue;
        SDL_GPUColorTargetInfo target{};target.texture=sceneEffectsTargets_[output].texture;target.load_op=SDL_GPU_LOADOP_DONT_CARE;target.store_op=SDL_GPU_STOREOP_STORE;
        auto* pass=SDL_BeginGPURenderPass(cmd_,&target,1,nullptr);if(!pass)return false;
        SDL_SetGPUViewport(pass,&viewport);SDL_BindGPUGraphicsPipeline(pass,sceneEffectsPipelines_[k]);
        const SDL_GPUTextureSamplerBinding textures[]={{source,presentSampler_},{opaqueDepthColor_.texture,postPointSampler_},{reflectionSurface_.texture,postPointSampler_},
            {shadowMapConstants_.parameters[3]>.5f?shadowMapColor_.texture:white_,postPointSampler_}};
        SDL_BindGPUFragmentSamplers(pass,0,textures,4);SDL_PushGPUFragmentUniformData(cmd_,0,&constants,sizeof(constants));SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
        source=sceneEffectsTargets_[output].texture;output^=1;
    }
    SDL_GPUColorTargetInfo target{};target.texture=multisampleColor_?multisampleColor_:color_;target.load_op=SDL_GPU_LOADOP_LOAD;
    target.store_op=multisampleColor_?SDL_GPU_STOREOP_RESOLVE_AND_STORE:SDL_GPU_STOREOP_STORE;target.resolve_texture=multisampleColor_?color_:nullptr;
    auto* pass=SDL_BeginGPURenderPass(cmd_,&target,1,nullptr);if(!pass)return false;
    SDL_SetGPUViewport(pass,&viewport);SDL_BindGPUGraphicsPipeline(pass,copyPipeline);
    const SDL_GPUTextureSamplerBinding resolved={source,presentSampler_};SDL_BindGPUFragmentSamplers(pass,0,&resolved,1);SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);return true;
}
