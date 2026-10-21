// Included inside d3d11_device.cpp's backend namespace. Directional shadow
// resources are separate from main color/depth, including under MSAA.
bool D3D11Device::EnsureShadowMap(int quality) {
    if(quality<1||quality>3)return false;
    if(!shadowMapVs_||!shadowMapPs_) {
        ComPtr<ID3DBlob> vs,ps,error;
        if(FAILED(D3DCompile(kShadowMapShaderHlsl,sizeof(kShadowMapShaderHlsl)-1,"shadow",nullptr,nullptr,"vsmain","vs_5_0",0,0,&vs,&error))||
           FAILED(D3DCompile(kShadowMapShaderHlsl,sizeof(kShadowMapShaderHlsl)-1,"shadow",nullptr,nullptr,"psmain","ps_5_0",0,0,&ps,&error)))return false;
        if(FAILED(device_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&shadowMapVs_))||
           FAILED(device_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&shadowMapPs_)))return false;
    }
    const int size=1024<<(quality-1);
    if(shadowMapColor_.width==size&&shadowMapDepth_)return true;
    PostTarget color;if(!EnsurePostTarget(color,size,size,DXGI_FORMAT_R32_FLOAT))return false;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=size;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_D32_FLOAT;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depth;ComPtr<ID3D11DepthStencilView> view;
    if(FAILED(device_->CreateTexture2D(&desc,nullptr,&depth))||FAILED(device_->CreateDepthStencilView(depth.Get(),nullptr,&view)))return false;
    shadowMapColor_=std::move(color);shadowMapDepth_=std::move(depth);shadowMapDsv_=std::move(view);return true;
}

void D3D11Device::DrawShadowMap(const GpuDrawList& list) {
    shadowMapConstants_={};
    if((!graphics_.shadowMaps&&!graphics_.volumetricFog)||!list.shadowMap.valid||list.shadowCasters.empty()||!EnsureShadowMap(graphics_.shadowMaps?graphics_.shadowMaps:1)) {
        ctx_->UpdateSubresource(shadowMapCb_.Get(),0,nullptr,&shadowMapConstants_,0,0);return;
    }
    shadowMapConstants_=list.shadowMap.constants;
    ID3D11ShaderResourceView* empty=nullptr;ctx_->PSSetShaderResources(3,1,&empty);
    ID3D11RenderTargetView* target=shadowMapColor_.rtv.Get();ctx_->OMSetRenderTargets(1,&target,shadowMapDsv_.Get());
    const float clear[4]={1,1,1,1};ctx_->ClearRenderTargetView(target,clear);ctx_->ClearDepthStencilView(shadowMapDsv_.Get(),D3D11_CLEAR_DEPTH,1,0);
    const D3D11_VIEWPORT viewport={0,0,float(shadowMapColor_.width),float(shadowMapColor_.height),0,1};ctx_->RSSetViewports(1,&viewport);
    ctx_->IASetInputLayout(layout_.Get());ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(shadowMapVs_.Get(),nullptr,0);ctx_->PSSetShader(shadowMapPs_.Get(),nullptr,0);
    ctx_->UpdateSubresource(shadowMapCb_.Get(),0,nullptr,&shadowMapConstants_,0,0);
    ID3D11Buffer* buffers[]={constants_.Get(),shadowMapCb_.Get()};ctx_->VSSetConstantBuffers(0,2,buffers);
    const float blend[4]={};ctx_->OMSetBlendState(BlendState(false,2,1),blend,0xffffffff);ctx_->OMSetDepthStencilState(DepthState(7,true),0);
    size_t drawn=0;
    for(const auto& draw:list.shadowCasters) {
        if(draw.pipeline!=GpuDraw::Pipeline::Model||draw.state.blendEnable||!draw.state.zwrite||draw.constants>=list.model.size())continue;
        ID3D11Buffer* vertices=draw.stream==GpuDraw::Stream::Static?staticVb_.Get():draw.stream==GpuDraw::Stream::Dynamic?dynVb_.Get():nullptr;
        if(!vertices||list.model[draw.constants].flags[2]>=0)continue;
        const UINT stride=sizeof(game::ModelVertex),offset=0;ctx_->IASetVertexBuffers(0,1,&vertices,&stride,&offset);
        const int cull=draw.state.cull>=1&&draw.state.cull<=3?draw.state.cull-1:0;ctx_->RSSetState(rasterCull_[cull].Get());
        ctx_->UpdateSubresource(constants_.Get(),0,nullptr,&list.model[draw.constants],0,0);ctx_->Draw(draw.count,draw.first);++drawn;
    }
    shadowMapConstants_.parameters[3]=drawn?1.f:0.f;ctx_->UpdateSubresource(shadowMapCb_.Get(),0,nullptr,&shadowMapConstants_,0,0);
    target=rtv_.Get();ctx_->OMSetRenderTargets(1,&target,dsv_.Get());
}
