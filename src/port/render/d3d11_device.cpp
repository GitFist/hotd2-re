#include "d3d11_device.h"

#include <windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cstring>
#include <map>
#include <vector>

#include "asset_store.h"
#include "hlsl_shaders.h"
#include "texture_mips.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace port {

namespace {

using Microsoft::WRL::ComPtr;

D3D11_BLEND ToD3D11Blend(int d3d7) {
    switch (d3d7) {
    case 1: return D3D11_BLEND_ZERO;
    case 2: return D3D11_BLEND_ONE;
    case 3: return D3D11_BLEND_SRC_COLOR;
    case 4: return D3D11_BLEND_INV_SRC_COLOR;
    case 5: return D3D11_BLEND_SRC_ALPHA;
    case 6: return D3D11_BLEND_INV_SRC_ALPHA;
    case 7: return D3D11_BLEND_DEST_ALPHA;
    case 8: return D3D11_BLEND_INV_DEST_ALPHA;
    case 9: return D3D11_BLEND_DEST_COLOR;
    case 10: return D3D11_BLEND_INV_DEST_COLOR;
    default: return D3D11_BLEND_ONE;
    }
}

D3D11_TEXTURE_ADDRESS_MODE ToD3D11Address(int d3d7) {
    return d3d7 == 2 ? D3D11_TEXTURE_ADDRESS_MIRROR : d3d7 == 3 ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
}

class D3D11Device final : public GpuDevice {
public:
    bool Init(const GpuDeviceDesc& desc, std::string* error);
    const char* Name() const override { return "d3d11"; }
    GpuTexture CreateTexture(const RgbaImage& img, bool alphaWeighted = false) override;
    void ClearTextures() override {
        ID3D11ShaderResourceView* empty = nullptr;
        ctx_->PSSetShaderResources(0, 1, &empty);
        textures_.clear();
        ctx_->Flush();
    }
    void SetStaticVertices(const game::ModelVertex* v, size_t count) override;
    void SetDynamicVertices(const game::ModelVertex* v, size_t count) override;
    void SetSpriteVertices(const game::TLVertex* v, size_t count) override;
    bool BeginFrame(const float clear[4]) override;
    void Submit(const GpuDrawList& list) override;
    void EndFrame() override;
    bool ReadPixels(std::vector<uint8_t>* rgba) override;
    int TargetWidth() const override { return width_; }
    int TargetHeight() const override { return height_; }
    void SetPresent(const PresentParams& p) override { present_ = p; }
    bool ResizeTarget(int w, int h) override;
    bool WindowSizePixels(int* w, int* h) const override;

private:
    bool CreatePipelines(std::string* error);
    bool CreateOffscreen();
    bool CreateBackBuffer(int w, int h);
    void PresentFrame();
    ID3D11BlendState* BlendState(bool enable, int src, int dst);
    ID3D11DepthStencilState* DepthState(int zfunc, bool zwrite);
    ID3D11SamplerState* Sampler(bool linear, int addrU, int addrV, bool mipmapped);
    void BindPipeline(GpuDraw::Pipeline p, int width, int height);

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> ctx_;
    HWND hwnd_ = nullptr;
    ComPtr<IDXGISwapChain> swap_;
    bool offscreen_ = true, follow_ = false;
    int sync_ = 0;
    int width_ = 0, height_ = 0;  // the render target (internal resolution)
    // The frame is always rendered into color_ / depth_ (off-screen, width_ x height_). Headless: staging_ reads it
    // back. Window: the present pass (presentVs_ / presentPs_, hlsl_shaders.h kPresentShaderHlsl) scales it into
    // present_'s rectangle of the swap chain's back buffer (backRtv_, backW_ x backH_ = the client area).
    ComPtr<ID3D11Texture2D> color_, depth_, staging_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11DepthStencilView> dsv_;
    ComPtr<ID3D11ShaderResourceView> colorSrv_;
    ComPtr<ID3D11RenderTargetView> backRtv_;
    int backW_ = 0, backH_ = 0;
    PresentParams present_;
    ComPtr<ID3D11VertexShader> presentVs_;
    ComPtr<ID3D11PixelShader> presentPs_;
    ComPtr<ID3D11Buffer> presentCb_;
    ComPtr<ID3D11SamplerState> presentSampler_;
    ComPtr<ID3D11DepthStencilState> presentDepth_;

    ComPtr<ID3D11VertexShader> vs_, spriteVs_;
    ComPtr<ID3D11PixelShader> ps_, spritePs_;
    ComPtr<ID3D11InputLayout> layout_, spriteLayout_;
    ComPtr<ID3D11Buffer> constants_, spriteCb_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11RasterizerState> rasterCull_[3];  // D3DCULL none / CW / CCW
    ComPtr<ID3D11Buffer> staticVb_, dynVb_, spriteVb_;
    UINT dynVbCapacity_ = 0, spriteVbCapacity_ = 0;  // bytes / vertices
    std::map<int, ComPtr<ID3D11BlendState>> blends_;
    std::map<int, ComPtr<ID3D11DepthStencilState>> depths_;
    std::map<int, ComPtr<ID3D11SamplerState>> samplers_;
    std::vector<ComPtr<ID3D11ShaderResourceView>> textures_;  // handle = index + 1
};

bool D3D11Device::Init(const GpuDeviceDesc& desc, std::string* error) {
    offscreen_ = desc.offscreen || !desc.nativeWindow;
    follow_ = desc.followWindowSize;
    sync_ = desc.syncInterval;
    width_ = desc.width;
    height_ = desc.height;
    if (offscreen_) {
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &ctx_)) &&
            FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &ctx_))) {
            if (error) *error = "D3D11CreateDevice failed";
            return false;
        }
    } else {
        hwnd_ = static_cast<HWND>(desc.nativeWindow);
        RECT cr;
        GetClientRect(hwnd_, &cr);
        backW_ = cr.right > 0 ? cr.right : 1;
        backH_ = cr.bottom > 0 ? cr.bottom : 1;
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = backW_;  // the client area; the frame is scaled into it by the present pass
        sd.BufferDesc.Height = backH_;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd_;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &swap_,
                                                 &device_, nullptr, &ctx_))) {
            if (error) *error = "D3D11CreateDeviceAndSwapChain failed";
            return false;
        }
        if (!CreateBackBuffer(backW_, backH_)) {
            if (error) *error = "back buffer view creation failed";
            return false;
        }
        if (follow_) {  // the render target follows the client area (sized in BeginFrame)
            width_ = backW_;
            height_ = backH_;
        }
    }
    if (!CreateOffscreen()) {
        if (error) *error = "render target creation failed";
        return false;
    }
    return CreatePipelines(error);
}

bool D3D11Device::CreateOffscreen() {
    color_.Reset();
    depth_.Reset();
    staging_.Reset();
    rtv_.Reset();
    dsv_.Reset();
    colorSrv_.Reset();
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = width_;
    td.Height = height_;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;  // sampled by the present pass (window)
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &color_))) return false;
    device_->CreateRenderTargetView(color_.Get(), nullptr, &rtv_);
    device_->CreateShaderResourceView(color_.Get(), nullptr, &colorSrv_);
    if (offscreen_) {  // headless: the readback copy
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        device_->CreateTexture2D(&td, nullptr, &staging_);
        td.Usage = D3D11_USAGE_DEFAULT;
        td.CPUAccessFlags = 0;
    }
    td.Format = DXGI_FORMAT_D32_FLOAT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    device_->CreateTexture2D(&td, nullptr, &depth_);
    device_->CreateDepthStencilView(depth_.Get(), nullptr, &dsv_);
    return rtv_ && dsv_ && colorSrv_ && (!offscreen_ || staging_);
}

bool D3D11Device::CreateBackBuffer(int w, int h) {
    backRtv_.Reset();
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
    device_->CreateRenderTargetView(back.Get(), nullptr, &backRtv_);
    backW_ = w;
    backH_ = h;
    return backRtv_ != nullptr;
}

bool D3D11Device::ResizeTarget(int w, int h) {
    if (follow_ || w <= 0 || h <= 0) return false;
    if (w == width_ && h == height_) return true;
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
    width_ = w;
    height_ = h;
    return CreateOffscreen();
}

bool D3D11Device::WindowSizePixels(int* w, int* h) const {
    if (offscreen_ || !hwnd_) return false;
    RECT cr;
    GetClientRect(hwnd_, &cr);
    *w = cr.right > 0 ? cr.right : 1;
    *h = cr.bottom > 0 ? cr.bottom : 1;
    return true;
}


bool D3D11Device::CreatePipelines(std::string* error) {
    ComPtr<ID3DBlob> vsb, psb, err;
    if (FAILED(D3DCompile(kModelShaderHlsl, sizeof(kModelShaderHlsl) - 1, "model", nullptr, nullptr, "vsmain", "vs_5_0", 0, 0, &vsb, &err)) ||
        FAILED(D3DCompile(kModelShaderHlsl, sizeof(kModelShaderHlsl) - 1, "model", nullptr, nullptr, "psmain", "ps_5_0", 0, 0, &psb, &err))) {
        if (error) *error = err ? static_cast<const char*>(err->GetBufferPointer()) : "shader compile failed";
        return false;
    }
    device_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_);
    device_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_);
    const D3D11_INPUT_ELEMENT_DESC il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    device_->CreateInputLayout(il, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout_);
    D3D11_BUFFER_DESC cb = { sizeof(ModelConstants), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER };
    device_->CreateBuffer(&cb, nullptr, &constants_);

    ComPtr<ID3DBlob> svs, sps;
    if (FAILED(D3DCompile(kSpriteShaderHlsl, sizeof(kSpriteShaderHlsl) - 1, "sprite", nullptr, nullptr, "vsmain", "vs_5_0", 0, 0, &svs, &err)) ||
        FAILED(D3DCompile(kSpriteShaderHlsl, sizeof(kSpriteShaderHlsl) - 1, "sprite", nullptr, nullptr, "psmain", "ps_5_0", 0, 0, &sps, &err))) {
        if (error) *error = err ? static_cast<const char*>(err->GetBufferPointer()) : "sprite shader compile failed";
        return false;
    }
    device_->CreateVertexShader(svs->GetBufferPointer(), svs->GetBufferSize(), nullptr, &spriteVs_);
    device_->CreatePixelShader(sps->GetBufferPointer(), sps->GetBufferSize(), nullptr, &spritePs_);
    const D3D11_INPUT_ELEMENT_DESC sil[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },  // ARGB dword
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    device_->CreateInputLayout(sil, 3, svs->GetBufferPointer(), svs->GetBufferSize(), &spriteLayout_);
    D3D11_BUFFER_DESC scb = { sizeof(SpriteConstants), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER };
    device_->CreateBuffer(&scb, nullptr, &spriteCb_);

    // D3DCULL 1 none / 2 CW / 3 CCW (screen-space winding; D3D11 front faces are clockwise, so D3DCULL_CW culls front
    // faces; checked on the name-entry room, whose walls enclose the camera)
    const D3D11_CULL_MODE modes[3] = { D3D11_CULL_NONE, D3D11_CULL_FRONT, D3D11_CULL_BACK };
    for (int i = 0; i < 3; ++i) {
        D3D11_RASTERIZER_DESC desc = {};
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = modes[i];
        desc.DepthClipEnable = TRUE;
        device_->CreateRasterizerState(&desc, &rasterCull_[i]);
    }
    raster_ = rasterCull_[0];
    if (!offscreen_) {  // the present pass (window)
        ComPtr<ID3DBlob> pvs, pps;
        if (FAILED(D3DCompile(kPresentShaderHlsl, sizeof(kPresentShaderHlsl) - 1, "present", nullptr, nullptr, "vsmain", "vs_5_0", 0, 0, &pvs, &err)) ||
            FAILED(D3DCompile(kPresentShaderHlsl, sizeof(kPresentShaderHlsl) - 1, "present", nullptr, nullptr, "psmain", "ps_5_0", 0, 0, &pps, &err))) {
            if (error) *error = err ? static_cast<const char*>(err->GetBufferPointer()) : "present shader compile failed";
            return false;
        }
        device_->CreateVertexShader(pvs->GetBufferPointer(), pvs->GetBufferSize(), nullptr, &presentVs_);
        device_->CreatePixelShader(pps->GetBufferPointer(), pps->GetBufferSize(), nullptr, &presentPs_);
        D3D11_BUFFER_DESC pcb = { 32, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER };
        device_->CreateBuffer(&pcb, nullptr, &presentCb_);
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        device_->CreateSamplerState(&sd, &presentSampler_);
        D3D11_DEPTH_STENCIL_DESC dd = {};
        dd.DepthEnable = FALSE;
        device_->CreateDepthStencilState(&dd, &presentDepth_);
        if (!presentVs_ || !presentPs_ || !presentCb_ || !presentSampler_ || !presentDepth_) {
            if (error) *error = "present pipeline creation failed";
            return false;
        }
    }
    return vs_ && ps_ && layout_ && constants_ && spriteVs_ && spritePs_ && spriteLayout_ && spriteCb_;
}

GpuTexture D3D11Device::CreateTexture(const RgbaImage& img, bool alphaWeighted) {
    if (img.width <= 0 || img.height <= 0 || img.pixels.size() != static_cast<size_t>(img.width) * img.height * 4) return 0;
    const auto mips = BuildTextureMips(img, alphaWeighted);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = img.width;
    td.Height = img.height;
    td.MipLevels = static_cast<UINT>(mips.size() + 1);
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> init;
    init.push_back({img.pixels.data(), static_cast<UINT>(img.width * 4), 0});
    for (const auto& mip : mips) init.push_back({mip.pixels.data(), static_cast<UINT>(mip.width * 4), 0});
    ComPtr<ID3D11Texture2D> t;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device_->CreateTexture2D(&td, init.data(), &t)) || FAILED(device_->CreateShaderResourceView(t.Get(), nullptr, &srv))) return 0;
    textures_.push_back(srv);
    return textures_.size();
}

void D3D11Device::SetStaticVertices(const game::ModelVertex* v, size_t count) {
    staticVb_.Reset();
    if (!count) return;
    D3D11_BUFFER_DESC bd = { static_cast<UINT>(count * sizeof(game::ModelVertex)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER };
    D3D11_SUBRESOURCE_DATA init = { v };
    device_->CreateBuffer(&bd, &init, &staticVb_);
}

void D3D11Device::SetDynamicVertices(const game::ModelVertex* v, size_t count) {
    if (!count) return;
    const UINT bytes = static_cast<UINT>(count * sizeof(game::ModelVertex));
    if (bytes > dynVbCapacity_) {
        dynVb_.Reset();
        D3D11_BUFFER_DESC bd = { bytes * 2, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE };
        if (SUCCEEDED(device_->CreateBuffer(&bd, nullptr, &dynVb_))) dynVbCapacity_ = bytes * 2;
    }
    if (!dynVb_) return;
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ctx_->Map(dynVb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, v, bytes);
        ctx_->Unmap(dynVb_.Get(), 0);
    }
}

void D3D11Device::SetSpriteVertices(const game::TLVertex* v, size_t count) {
    if (!count) return;
    const UINT need = static_cast<UINT>(count);
    if (need > spriteVbCapacity_) {
        D3D11_BUFFER_DESC bd = { need * static_cast<UINT>(sizeof(game::TLVertex)), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER,
                                 D3D11_CPU_ACCESS_WRITE };
        spriteVb_.Reset();
        spriteVbCapacity_ = 0;
        if (FAILED(device_->CreateBuffer(&bd, nullptr, &spriteVb_))) return;
        spriteVbCapacity_ = need;
    }
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx_->Map(spriteVb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, v, count * sizeof(game::TLVertex));
    ctx_->Unmap(spriteVb_.Get(), 0);
}

ID3D11BlendState* D3D11Device::BlendState(bool enable, int src, int dst) {
    const int key = (enable ? 1 : 0) | (src << 1) | (dst << 8);
    auto& s = blends_[key];
    if (!s) {
        D3D11_BLEND_DESC bd = {};
        auto& rt = bd.RenderTarget[0];
        rt.BlendEnable = enable;
        rt.SrcBlend = rt.SrcBlendAlpha = ToD3D11Blend(src);
        rt.DestBlend = rt.DestBlendAlpha = ToD3D11Blend(dst);
        // Alpha factors may not use colour blends in D3D11; map them to their alpha counterparts.
        auto alphaOnly = [](D3D11_BLEND b) {
            if (b == D3D11_BLEND_SRC_COLOR) return D3D11_BLEND_SRC_ALPHA;
            if (b == D3D11_BLEND_INV_SRC_COLOR) return D3D11_BLEND_INV_SRC_ALPHA;
            if (b == D3D11_BLEND_DEST_COLOR) return D3D11_BLEND_DEST_ALPHA;
            if (b == D3D11_BLEND_INV_DEST_COLOR) return D3D11_BLEND_INV_DEST_ALPHA;
            return b;
        };
        rt.SrcBlendAlpha = alphaOnly(rt.SrcBlendAlpha);
        rt.DestBlendAlpha = alphaOnly(rt.DestBlendAlpha);
        rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device_->CreateBlendState(&bd, &s);
    }
    return s.Get();
}

ID3D11DepthStencilState* D3D11Device::DepthState(int zfunc, bool zwrite) {
    const int key = zfunc | (zwrite ? 16 : 0);
    auto& s = depths_[key];
    if (!s) {
        D3D11_DEPTH_STENCIL_DESC dd = {};
        dd.DepthEnable = TRUE;
        dd.DepthWriteMask = zwrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        dd.DepthFunc = static_cast<D3D11_COMPARISON_FUNC>(ReversedDepthCompare(zfunc));
        device_->CreateDepthStencilState(&dd, &s);
    }
    return s.Get();
}

ID3D11SamplerState* D3D11Device::Sampler(bool linear, int addrU, int addrV, bool mipmapped) {
    const int key = (linear ? 1 : 0) | (addrU << 1) | (addrV << 4) | (mipmapped ? 1 << 8 : 0);
    auto& s = samplers_[key];
    if (!s) {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = mipmapped ? (linear ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR)
                              : (linear ? D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_POINT);
        sd.AddressU = ToD3D11Address(addrU);
        sd.AddressV = ToD3D11Address(addrV);
        sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.MaxLOD = mipmapped ? D3D11_FLOAT32_MAX : 0;
        device_->CreateSamplerState(&sd, &s);
    }
    return s.Get();
}

void D3D11Device::BindPipeline(GpuDraw::Pipeline p, int width, int height) {
    D3D11_VIEWPORT vp = { 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1 };
    ctx_->RSSetViewports(1, &vp);
    ctx_->RSSetState(raster_.Get());
    const bool model = p == GpuDraw::Pipeline::Model;
    ctx_->IASetInputLayout(model ? layout_.Get() : spriteLayout_.Get());
    ctx_->IASetPrimitiveTopology(model ? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx_->VSSetShader(model ? vs_.Get() : spriteVs_.Get(), nullptr, 0);
    ctx_->PSSetShader(model ? ps_.Get() : spritePs_.Get(), nullptr, 0);
    ID3D11Buffer* cb = model ? constants_.Get() : spriteCb_.Get();
    ctx_->VSSetConstantBuffers(0, 1, &cb);
    ctx_->PSSetConstantBuffers(0, 1, &cb);
}

void D3D11Device::Submit(const GpuDrawList& list) {
    const float blendFactor[4] = { 0, 0, 0, 0 };
    bool bound = false;
    GpuDraw::Pipeline pipeline = GpuDraw::Pipeline::Model;
    bool streamBound = false;
    GpuDraw::Stream stream = GpuDraw::Stream::Static;
    for (const GpuDraw& d : list.draws) {
        if (!bound || d.pipeline != pipeline) {
            BindPipeline(d.pipeline, list.width, list.height);
            bound = true;
            pipeline = d.pipeline;
            streamBound = false;
        }
        if (!streamBound || d.stream != stream) {
            ID3D11Buffer* vb = d.stream == GpuDraw::Stream::Static ? staticVb_.Get() : d.stream == GpuDraw::Stream::Dynamic ? dynVb_.Get() : spriteVb_.Get();
            const UINT stride = d.stream == GpuDraw::Stream::Sprite ? sizeof(game::TLVertex) : sizeof(game::ModelVertex), offset = 0;
            ctx_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            streamBound = true;
            stream = d.stream;
        }
        if (d.pipeline == GpuDraw::Pipeline::Model) ctx_->UpdateSubresource(constants_.Get(), 0, nullptr, &list.model[d.constants], 0, 0);
        else ctx_->UpdateSubresource(spriteCb_.Get(), 0, nullptr, &list.sprite[d.constants], 0, 0);
        const GpuState& s = d.state;
        ctx_->OMSetBlendState(BlendState(s.blendEnable != 0, s.srcBlend, s.dstBlend), blendFactor, 0xFFFFFFFF);
        ctx_->RSSetState(rasterCull_[(s.cull >= 1 && s.cull <= 3) ? s.cull - 1 : 0].Get());
        ctx_->OMSetDepthStencilState(DepthState(s.zfunc, s.zwrite != 0), 0);
        ID3D11SamplerState* smp = Sampler(s.linear != 0, s.addrU, s.addrV, s.mipmapped != 0);
        ctx_->PSSetSamplers(0, 1, &smp);
        ID3D11ShaderResourceView* srv = d.texture && d.texture <= textures_.size() ? textures_[d.texture - 1].Get() : nullptr;
        ctx_->PSSetShaderResources(0, 1, &srv);
        ctx_->Draw(d.count, d.first);
    }
    ctx_->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
}

bool D3D11Device::BeginFrame(const float clear[4]) {
    if (!offscreen_ && follow_) {  // the render target follows the client area
        int w, h;
        WindowSizePixels(&w, &h);
        if (w != width_ || h != height_) {
            ctx_->OMSetRenderTargets(0, nullptr, nullptr);
            width_ = w;
            height_ = h;
            CreateOffscreen();
        }
    }
    ID3D11RenderTargetView* rt = rtv_.Get();
    ctx_->OMSetRenderTargets(1, &rt, dsv_.Get());
    ctx_->ClearRenderTargetView(rt, clear);
    ctx_->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
    return rtv_ && dsv_;
}

// The present pass: the swap chain's back buffer (resized with the window) cleared black, the frame scaled into the
// image rectangle with the shared present shader.
void D3D11Device::PresentFrame() {
    int w, h;
    WindowSizePixels(&w, &h);
    if (w != backW_ || h != backH_) {
        ctx_->OMSetRenderTargets(0, nullptr, nullptr);
        backRtv_.Reset();
        swap_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
        CreateBackBuffer(w, h);
    }
    if (!backRtv_) return;
    ID3D11RenderTargetView* rt = backRtv_.Get();
    ctx_->OMSetRenderTargets(1, &rt, nullptr);
    const float black[4] = { 0, 0, 0, 1 };
    ctx_->ClearRenderTargetView(rt, black);
    PresentParams p = present_;
    if (p.w <= 0 || p.h <= 0) {
        p.x = p.y = 0;
        p.w = backW_;
        p.h = backH_;
    }
    D3D11_VIEWPORT vp = { static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.w), static_cast<float>(p.h), 0, 1 };
    ctx_->RSSetViewports(1, &vp);
    ctx_->RSSetState(rasterCull_[0].Get());
    const float cb[8] = { static_cast<float>(width_), static_cast<float>(height_), 1.0f / width_, 1.0f / height_,
                          static_cast<float>(p.w), static_cast<float>(p.h), static_cast<float>(p.crt), 480.0f };
    ctx_->UpdateSubresource(presentCb_.Get(), 0, nullptr, cb, 0, 0);
    ctx_->IASetInputLayout(nullptr);
    ID3D11Buffer* nullVb = nullptr;
    const UINT zero = 0;
    ctx_->IASetVertexBuffers(0, 1, &nullVb, &zero, &zero);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(presentVs_.Get(), nullptr, 0);
    ctx_->PSSetShader(presentPs_.Get(), nullptr, 0);
    ID3D11Buffer* pcb = presentCb_.Get();
    ctx_->PSSetConstantBuffers(0, 1, &pcb);
    ID3D11SamplerState* smp = presentSampler_.Get();
    ctx_->PSSetSamplers(0, 1, &smp);
    ID3D11ShaderResourceView* srv = colorSrv_.Get();
    ctx_->PSSetShaderResources(0, 1, &srv);
    const float blendFactor[4] = { 0, 0, 0, 0 };
    ctx_->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
    ctx_->OMSetDepthStencilState(presentDepth_.Get(), 0);
    ctx_->Draw(3, 0);
    srv = nullptr;
    ctx_->PSSetShaderResources(0, 1, &srv);  // the render target is bound again by the next BeginFrame
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);
}

void D3D11Device::EndFrame() {
    if (!swap_) return;
    PresentFrame();
    swap_->Present(sync_, 0);
}

bool D3D11Device::ReadPixels(std::vector<uint8_t>* rgba) {
    if (!staging_) return false;
    ctx_->CopyResource(staging_.Get(), color_.Get());
    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(ctx_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
    rgba->resize(static_cast<size_t>(width_) * height_ * 4);
    for (int y = 0; y < height_; ++y)
        memcpy(&(*rgba)[static_cast<size_t>(y) * width_ * 4], static_cast<uint8_t*>(map.pData) + y * map.RowPitch, width_ * 4);
    ctx_->Unmap(staging_.Get(), 0);
    return true;
}

}  // namespace

std::unique_ptr<GpuDevice> CreateD3D11Device(const GpuDeviceDesc& desc, std::string* error) {
    auto d = std::make_unique<D3D11Device>();
    if (!d->Init(desc, error)) return nullptr;
    return d;
}

}  // namespace port
