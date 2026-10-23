// Output UI has a fresh single-sample depth attachment; input scene history,
// world sprites and transparent models remain at the internal resolution.
bool SdlGpuDevice::EnsureHudTargets(std::string* error) {
    int width=present_.w,height=present_.h;
    if(width<=0||height<=0){width=width_;height=height_;WindowSizePixels(&width,&height);}
    if(!ValidHudOutputSize(width,height)){if(error)*error="HUD output requires dimensions up to 8192 and at most 16 million pixels";return false;}
    if(!hudCopyPipeline_){
        SDL_GPUColorTargetDescription color{};color.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=presentVs_;info.fragment_shader=presentPs_;
        info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
        info.target_info.color_target_descriptions=&color;info.target_info.num_color_targets=1;
        info.target_info.has_depth_stencil_target=true;info.target_info.depth_stencil_format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        hudCopyPipeline_=SDL_CreateGPUGraphicsPipeline(dev_,&info);
        if(!hudCopyPipeline_){if(error)*error=SDL_GetError();return false;}
    }
    if(hudColor_.texture&&hudDepth_&&hudColor_.width==width&&hudColor_.height==height)return true;
    PostTarget color;SDL_GPUTextureCreateInfo depth{};depth.type=SDL_GPU_TEXTURETYPE_2D;depth.format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    depth.usage=SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;depth.width=width;depth.height=height;depth.layer_count_or_depth=1;
    depth.num_levels=1;depth.sample_count=SDL_GPU_SAMPLECOUNT_1;
    if(!EnsurePostTarget(color,width,height)){if(error)*error=SDL_GetError();return false;}
    auto* depthTexture=SDL_CreateGPUTexture(dev_,&depth);
    if(!depthTexture){SDL_ReleaseGPUTexture(dev_,color.texture);if(error)*error="Output-resolution HUD allocation failed; internal HUD retained";return false;}
    if(hudColor_.texture)SDL_ReleaseGPUTexture(dev_,hudColor_.texture);
    if(hudDepth_)SDL_ReleaseGPUTexture(dev_,hudDepth_);
    hudColor_=color;hudDepth_=depthTexture;return true;
}

bool SdlGpuDevice::ComposeHudScene(std::string* error) {
    if(hudReady_)return true;
    if(!cmd_||!EnsureHudTargets(error))return false;
    int width=0,height=0;bool fsr=false;
    auto* source=PostSource(hudColor_.width,hudColor_.height,&width,&height,&fsr);
    SDL_GPUColorTargetInfo target{};target.texture=hudColor_.texture;target.load_op=SDL_GPU_LOADOP_DONT_CARE;target.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};depth.texture=hudDepth_;depth.clear_depth=0;depth.load_op=SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
    auto* pass=SDL_BeginGPURenderPass(cmd_,&target,1,&depth);
    if(!pass){if(error)*error=SDL_GetError();return false;}
    const SDL_GPUViewport viewport={0,0,float(hudColor_.width),float(hudColor_.height),0,1};SDL_SetGPUViewport(pass,&viewport);
    SDL_BindGPUGraphicsPipeline(pass,hudCopyPipeline_);
    const SDL_GPUTextureSamplerBinding texture={source,presentSampler_};SDL_BindGPUFragmentSamplers(pass,0,&texture,1);
    const auto constants=MakePresentConstants(width,height,hudColor_.width,hudColor_.height,0,HudSceneGraphics(graphics_,fsr));
    SDL_PushGPUFragmentUniformData(cmd_,0,&constants,sizeof(constants));SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
    hudReady_=true;needClear_=false;return true;
}

bool SdlGpuDevice::ProcessScene(std::string* error) {
    if(hudReady_)return true;
    std::string diagnostic;bool okay=ProcessSceneEffects(&diagnostic);
    if(okay&&(graphics_.outputResolutionHud||menuOverlay_))okay=ComposeHudScene(&diagnostic);
    if(!okay){if(error)*error=diagnostic;
        if(graphics_.outputResolutionHud||menuOverlay_)hudError_=diagnostic.empty()?"Output-resolution HUD unavailable; internal HUD retained":diagnostic;}
    return okay;
}
