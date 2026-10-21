// Isolated optional scene passes. Run after the temporal resolve and before UI.
bool D3D11Device::EnsureSceneEffects(std::string* error) {
    if(!sceneEffectsPs_[0]||!sceneEffectsPs_[1]||!sceneEffectsPs_[2]||!reflectionSurfaceVs_||!reflectionSurfacePs_||!sceneEffectsCb_) {
        const char* entries[]={"reflectionps","fogps","dofps"};
        for(int k=0;k<3;++k){ComPtr<ID3DBlob> blob,diagnostic;
            if(FAILED(CompileShaderCached(kSceneEffectsShaderHlsl,sizeof(kSceneEffectsShaderHlsl)-1,"scene effects",nullptr,nullptr,entries[k],"ps_5_0",0,0,&blob,&diagnostic))||
               FAILED(device_->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&sceneEffectsPs_[k]))) {
                if(error)*error=diagnostic?static_cast<const char*>(diagnostic->GetBufferPointer()):"Scene effect pixel shader unavailable";return false;}}
        ComPtr<ID3DBlob> vs,ps,diagnostic;
        if(FAILED(CompileShaderCached(kReflectionSurfaceShaderHlsl,sizeof(kReflectionSurfaceShaderHlsl)-1,"reflection receivers",nullptr,nullptr,"vsmain","vs_5_0",0,0,&vs,&diagnostic))||
           FAILED(CompileShaderCached(kReflectionSurfaceShaderHlsl,sizeof(kReflectionSurfaceShaderHlsl)-1,"reflection receivers",nullptr,nullptr,"psmain","ps_5_0",0,0,&ps,&diagnostic))||
           FAILED(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&reflectionSurfaceVs_))||
           FAILED(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&reflectionSurfacePs_))) {
            if(error)*error=diagnostic?static_cast<const char*>(diagnostic->GetBufferPointer()):"Reflection receiver shader unavailable";return false;}
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(SceneEffectsConstants);cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(FAILED(device_->CreateBuffer(&cb,nullptr,&sceneEffectsCb_)))return false;
    }
    return EnsurePostTarget(sceneEffectsTargets_[0],width_,height_)&&EnsurePostTarget(sceneEffectsTargets_[1],width_,height_)&&
        EnsurePostTarget(reflectionSurface_,width_,height_,DXGI_FORMAT_R16G16B16A16_FLOAT);
}

void D3D11Device::DrawReflectionSurface(const GpuDrawList& list) {
    if(!graphics_.reflections||!opaqueReady_||!list.opaqueDrawCount||!EnsureSceneEffects())return;
    ID3D11ShaderResourceView* empty[4]={};ctx_->PSSetShaderResources(0,4,empty);
    ID3D11RenderTargetView* target=reflectionSurface_.rtv.Get();ctx_->OMSetRenderTargets(1,&target,opaqueDepthDsv_.Get());
    if(!reflectionSurfaceCleared_){const float clear[4]={};ctx_->ClearRenderTargetView(target,clear);reflectionSurfaceCleared_=true;}
    const D3D11_VIEWPORT viewport={0,0,float(width_),float(height_),0,1};ctx_->RSSetViewports(1,&viewport);
    ctx_->IASetInputLayout(layout_.Get());ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(reflectionSurfaceVs_.Get(),nullptr,0);ctx_->PSSetShader(reflectionSurfacePs_.Get(),nullptr,0);
    ID3D11Buffer* cb=constants_.Get();ctx_->VSSetConstantBuffers(0,1,&cb);ctx_->PSSetConstantBuffers(0,1,&cb);
    const float blend[4]={};ctx_->OMSetBlendState(nullptr,blend,0xffffffff);
    for(size_t k=0;k<list.opaqueDrawCount&&k<list.draws.size();++k){const auto& draw=list.draws[k];
        if(draw.pipeline!=GpuDraw::Pipeline::Model||draw.state.blendEnable||!draw.state.zwrite||draw.constants>=list.model.size()||list.model[draw.constants].localCount[2]<.5f)continue;
        ID3D11Buffer* vertices=draw.stream==GpuDraw::Stream::Static?staticVb_.Get():dynVb_.Get();if(!vertices)continue;
        const UINT stride=sizeof(game::ModelVertex),offset=0;ctx_->IASetVertexBuffers(0,1,&vertices,&stride,&offset);
        ctx_->UpdateSubresource(constants_.Get(),0,nullptr,&list.model[draw.constants],0,0);
        ctx_->RSSetState(rasterCull_[draw.state.cull>=1&&draw.state.cull<=3?draw.state.cull-1:0].Get());ctx_->OMSetDepthStencilState(DepthState(draw.state.zfunc,false),0);
        ID3D11ShaderResourceView* textures[]={draw.texture&&draw.texture<=textures_.size()?textures_[draw.texture-1].Get():nullptr,
            draw.normalRoughnessTexture&&draw.normalRoughnessTexture<=textures_.size()?textures_[draw.normalRoughnessTexture-1].Get():nullptr};
        ctx_->PSSetShaderResources(0,2,textures);auto* sampler=Sampler(draw.state.linear!=0,draw.state.addrU,draw.state.addrV,draw.state.mipmapped!=0);ctx_->PSSetSamplers(0,1,&sampler);ctx_->Draw(draw.count,draw.first);
    }
    ctx_->PSSetShaderResources(0,4,empty);target=rtv_.Get();ctx_->OMSetRenderTargets(1,&target,dsv_.Get());
}

bool D3D11Device::ProcessSceneEffects(std::string* error) {
    if(!ResolveTemporal(error))return false;
    if(sceneEffectsApplied_||!SceneEffectsEnabled(graphics_))return true;
    if(!frameActive_)return false;
    if(!opaqueReady_){sceneEffectsApplied_=true;return true;} // menus/clear-only scene: nothing to integrate
    if(!EnsureSceneEffects(error))return false;
    SceneEffectsConstants constants{};
    if(!MakeSceneEffectsConstants(width_,height_,sceneEffectsCamera_,sceneEffectsFrame_,shadowMapConstants_,graphics_,
        sceneEffectsJitter_,&constants))return true;
    sceneEffectsApplied_=true;
    const bool active[3]={graphics_.reflections>0&&reflectionSurfaceCleared_,graphics_.volumetricFog>0,constants.settings[2]>.5f};
    if(!active[0]&&!active[1]&&!active[2])return true;
    ResolveColor();ctx_->UpdateSubresource(sceneEffectsCb_.Get(),0,nullptr,&constants,0,0);
    const D3D11_VIEWPORT viewport={0,0,float(width_),float(height_),0,1};ctx_->RSSetViewports(1,&viewport);
    ctx_->IASetInputLayout(nullptr);ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx_->RSSetState(rasterCull_[0].Get());
    ctx_->VSSetShader(presentVs_.Get(),nullptr,0);ID3D11Buffer* cb=sceneEffectsCb_.Get();ctx_->PSSetConstantBuffers(0,1,&cb);
    const float blend[4]={};ctx_->OMSetBlendState(nullptr,blend,0xffffffff);ctx_->OMSetDepthStencilState(presentDepth_.Get(),0);
    ID3D11SamplerState* sampler=presentSampler_.Get();ctx_->PSSetSamplers(0,1,&sampler);
    ID3D11ShaderResourceView* source=colorSrv_.Get();int output=0;
    for(int k=0;k<3;++k){if(!active[k])continue;ID3D11RenderTargetView* target=sceneEffectsTargets_[output].rtv.Get();
        ctx_->OMSetRenderTargets(1,&target,nullptr);ctx_->PSSetShader(sceneEffectsPs_[k].Get(),nullptr,0);
        ID3D11ShaderResourceView* textures[]={source,opaqueDepthColor_.srv.Get(),reflectionSurface_.srv.Get(),shadowMapConstants_.parameters[3]>.5f?shadowMapColor_.srv.Get():nullptr};
        ctx_->PSSetShaderResources(0,4,textures);ctx_->Draw(3,0);ID3D11ShaderResourceView* empty[4]={};ctx_->PSSetShaderResources(0,4,empty);
        source=sceneEffectsTargets_[output].srv.Get();output^=1;
    }
    ID3D11RenderTargetView* target=rtv_.Get();ctx_->OMSetRenderTargets(1,&target,nullptr);ctx_->PSSetShader(taaCopyPs_.Get(),nullptr,0);
    ctx_->PSSetShaderResources(0,1,&source);ctx_->Draw(3,0);ID3D11ShaderResourceView* empty[4]={};ctx_->PSSetShaderResources(0,4,empty);
    ctx_->OMSetRenderTargets(1,&target,dsv_.Get());return true;
}
