// Included inside sdl_gpu_device.cpp's backend namespace.
bool SdlGpuDevice::EnsureShadowMap(int quality) {
    if(quality<1||quality>3)return false;const int size=1024<<(quality-1);
    if(shadowMapColor_.width==size&&shadowMapDepth_)return true;
    PostTarget color;if(!EnsurePostTarget(color,size,size,SDL_GPU_TEXTUREFORMAT_R32_FLOAT))return false;
    SDL_GPUTextureCreateInfo info{};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    info.usage=SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;info.width=info.height=size;info.layer_count_or_depth=info.num_levels=1;info.sample_count=SDL_GPU_SAMPLECOUNT_1;
    auto* depth=SDL_CreateGPUTexture(dev_,&info);
    if(!depth){SDL_ReleaseGPUTexture(dev_,color.texture);return false;}
    if(shadowMapColor_.texture)SDL_ReleaseGPUTexture(dev_,shadowMapColor_.texture);
    if(shadowMapDepth_)SDL_ReleaseGPUTexture(dev_,shadowMapDepth_);
    shadowMapColor_=color;shadowMapDepth_=depth;return true;
}

SDL_GPUGraphicsPipeline* SdlGpuDevice::ShadowMapPipeline(int cull) {
    cull=cull>=1&&cull<=3?cull:1;const int index=cull-1;if(shadowMapPipelines_[index])return shadowMapPipelines_[index];
    SDL_GPUVertexBufferDescription buffer{};buffer.slot=0;buffer.pitch=sizeof(game::ModelVertex);buffer.input_rate=SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[]={{0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,0},{1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,12},{2,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,24}};
    SDL_GPUColorTargetDescription color{};color.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=shadowMapVs_;info.fragment_shader=shadowMapPs_;
    info.vertex_input_state.vertex_buffer_descriptions=&buffer;info.vertex_input_state.num_vertex_buffers=1;info.vertex_input_state.vertex_attributes=attributes;info.vertex_input_state.num_vertex_attributes=3;
    info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
    info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.front_face=SDL_GPU_FRONTFACE_CLOCKWISE;info.rasterizer_state.enable_depth_clip=true;
    info.rasterizer_state.cull_mode=cull==1?SDL_GPU_CULLMODE_NONE:cull==2?SDL_GPU_CULLMODE_FRONT:SDL_GPU_CULLMODE_BACK;
    info.depth_stencil_state.enable_depth_test=info.depth_stencil_state.enable_depth_write=true;info.depth_stencil_state.compare_op=SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    info.target_info.color_target_descriptions=&color;info.target_info.num_color_targets=1;info.target_info.depth_stencil_format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;info.target_info.has_depth_stencil_target=true;
    shadowMapPipelines_[index]=SDL_CreateGPUGraphicsPipeline(dev_,&info);return shadowMapPipelines_[index];
}

void SdlGpuDevice::DrawShadowMap(const GpuDrawList& list) {
    shadowMapConstants_={};
    if((!graphics_.shadowMaps&&!graphics_.volumetricFog)||!list.shadowMap.valid||list.shadowCasters.empty()||!EnsureShadowMap(graphics_.shadowMaps?graphics_.shadowMaps:1))return;
    shadowMapConstants_=list.shadowMap.constants;
    SDL_GPUColorTargetInfo color{};color.texture=shadowMapColor_.texture;color.clear_color={1,1,1,1};color.load_op=SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};depth.texture=shadowMapDepth_;depth.clear_depth=1;depth.load_op=SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    auto* pass=SDL_BeginGPURenderPass(cmd_,&color,1,&depth);if(!pass)return;
    const SDL_GPUViewport viewport={0,0,float(shadowMapColor_.width),float(shadowMapColor_.height),0,1};SDL_SetGPUViewport(pass,&viewport);
    size_t drawn=0;
    for(const auto& draw:list.shadowCasters){
        if(draw.pipeline!=GpuDraw::Pipeline::Model||draw.state.blendEnable||!draw.state.zwrite||draw.constants>=list.model.size())continue;
        auto* vertices=draw.stream==GpuDraw::Stream::Static?staticVb_:draw.stream==GpuDraw::Stream::Dynamic?dynVb_:nullptr;
        auto* pipeline=ShadowMapPipeline(draw.state.cull);if(!vertices||!pipeline||list.model[draw.constants].flags[2]>=0)continue;
        SDL_BindGPUGraphicsPipeline(pass,pipeline);const SDL_GPUBufferBinding buffer={vertices,0};SDL_BindGPUVertexBuffers(pass,0,&buffer,1);
        SDL_PushGPUVertexUniformData(cmd_,0,&list.model[draw.constants],sizeof(ModelConstants));SDL_PushGPUVertexUniformData(cmd_,1,&shadowMapConstants_,sizeof(ShadowConstants));
        SDL_DrawGPUPrimitives(pass,draw.count,1,draw.first,0);++drawn;
    }
    SDL_EndGPURenderPass(pass);shadowMapConstants_.parameters[3]=drawn?1.f:0.f;
}
