// Independent output-resolution UI. The default path never enters these passes.
bool D3D11Device::EnsureHudTargets(std::string* error) {
    int width=present_.w,height=present_.h;
    if(width<=0||height<=0){width=width_;height=height_;WindowSizePixels(&width,&height);}
    if(!ValidHudOutputSize(width,height)){if(error)*error="HUD output requires dimensions up to 8192 and at most 16 million pixels";return false;}
    if(hudColor_.texture&&hudDepth_&&hudColor_.width==width&&hudColor_.height==height)return true;
    PostTarget color;
    ComPtr<ID3D11Texture2D> depth;
    ComPtr<ID3D11DepthStencilView> depthView;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_D32_FLOAT;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    if(!EnsurePostTarget(color,width,height)||FAILED(device_->CreateTexture2D(&desc,nullptr,&depth))||
       FAILED(device_->CreateDepthStencilView(depth.Get(),nullptr,&depthView))){
        if(error)*error="Output-resolution HUD allocation failed; internal HUD retained";return false;
    }
    hudColor_=std::move(color);hudDepth_=depth;hudDsv_=depthView;return true;
}

bool D3D11Device::ComposeHudScene(std::string* error) {
    if(hudReady_)return true;
    if(!frameActive_||!EnsureHudTargets(error))return false;
    int width=0,height=0;bool fsr=false;
    auto* source=PostSource(hudColor_.width,hudColor_.height,&width,&height,&fsr);
    ID3D11RenderTargetView* target=hudColor_.rtv.Get();ctx_->OMSetRenderTargets(1,&target,hudDsv_.Get());
    ctx_->ClearDepthStencilView(hudDsv_.Get(),D3D11_CLEAR_DEPTH,0,0);
    const D3D11_VIEWPORT viewport={0,0,float(hudColor_.width),float(hudColor_.height),0,1};ctx_->RSSetViewports(1,&viewport);
    ctx_->RSSetState(rasterCull_[0].Get());ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* empty=nullptr;const UINT zero=0;ctx_->IASetVertexBuffers(0,1,&empty,&zero,&zero);
    ctx_->VSSetShader(presentVs_.Get(),nullptr,0);ctx_->PSSetShader(presentPs_.Get(),nullptr,0);
    const auto constants=MakePresentConstants(width,height,hudColor_.width,hudColor_.height,0,HudSceneGraphics(graphics_,fsr));
    ctx_->UpdateSubresource(presentCb_.Get(),0,nullptr,&constants,0,0);
    ID3D11Buffer* buffer=presentCb_.Get();ctx_->PSSetConstantBuffers(0,1,&buffer);
    auto* sampler=presentSampler_.Get();ctx_->PSSetSamplers(0,1,&sampler);ctx_->PSSetShaderResources(0,1,&source);
    const float blend[4]={};ctx_->OMSetBlendState(nullptr,blend,0xffffffff);ctx_->OMSetDepthStencilState(presentDepth_.Get(),0);
    ctx_->Draw(3,0);ID3D11ShaderResourceView* nullResource=nullptr;ctx_->PSSetShaderResources(0,1,&nullResource);
    hudReady_=true;return true;
}

bool D3D11Device::ProcessScene(std::string* error) {
    if(hudReady_)return true;
    std::string diagnostic;
    bool okay=ProcessSceneEffects(&diagnostic);
    if(okay&&graphics_.outputResolutionHud)okay=ComposeHudScene(&diagnostic);
    if(!okay){
        if(error)*error=diagnostic;
        if(graphics_.outputResolutionHud)hudError_=diagnostic.empty()?"Output-resolution HUD unavailable; internal HUD retained":diagnostic;
        ID3D11RenderTargetView* target=rtv_.Get();ctx_->OMSetRenderTargets(1,&target,dsv_.Get());
    }
    return okay;
}
