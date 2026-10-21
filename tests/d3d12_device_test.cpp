// Real native D3D12 raster/readback parity with the established D3D11 backend.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "render/asset_store.h"
#include "render/d3d11_device.h"
#include "render/d3d12_device.h"
#include "render/native_d3d12_interop.h"
#include "port/play/reconstruction_fallback.h"
#include <wrl/client.h>

static void Require(bool okay, const char* message) { if (!okay) { fprintf(stderr,"FAIL: %s\n",message); std::exit(1); } }
static void Identity(float* m) { std::fill(m,m+16,0.f);m[0]=m[5]=m[10]=m[15]=1; }
static port::ModelConstants Constants(float r=1,float g=1,float b=1,float a=1) {
    port::ModelConstants c{};Identity(c.world);Identity(c.viewProj);
    c.diffuse[0]=r;c.diffuse[1]=g;c.diffuse[2]=b;c.diffuse[3]=a;c.flags[1]=1;c.flags[2]=-1;return c;
}
static std::array<game::ModelVertex,6> Quad(float z=.5f) {
    std::array<game::ModelVertex,6> v{};
    const float xy[6][2]={{-.75f,-.75f},{.75f,-.75f},{-.75f,.75f},{-.75f,.75f},{.75f,-.75f},{.75f,.75f}};
    for(int i=0;i<6;++i){v[i].pos[0]=xy[i][0];v[i].pos[1]=xy[i][1];v[i].pos[2]=z;v[i].normal[2]=1;v[i].uv[0]=xy[i][0]*.5f+.5f;v[i].uv[1]=.5f-xy[i][1]*.5f;}return v;
}
static std::vector<uint8_t> Render(port::GpuDevice& gpu,const port::GpuDrawList& list,bool present=false) {
    const float black[4]={0,0,0,1};Require(gpu.BeginFrame(black),"BeginFrame");gpu.Submit(list);
    std::vector<uint8_t> image;Require(present?gpu.ReadPresentation(128,96,&image):gpu.ReadPixels(&image),"read actual native pixels");gpu.EndFrame();return image;
}
static void Compare(const std::vector<uint8_t>& a,const std::vector<uint8_t>& b,const char* name,int tolerance=1,size_t allowedOutliers=0,int outlierLimit=255) {
    Require(a.size()==b.size(),"parity image dimensions");int maximum=0;size_t changed=0;
    for(size_t i=0;i<a.size();++i){const int d=std::abs(int(a[i])-int(b[i]));maximum=std::max(maximum,d);if(d>tolerance)++changed;}
    if(changed>allowedOutliers||maximum>outlierLimit){fprintf(stderr,"%s: %zu bytes outside tolerance, maximum %d\n",name,changed,maximum);std::exit(1);}
    printf("%s: parity within %d byte values (%zu localized outliers, maximum %d)\n",name,tolerance,changed,maximum);
}
static bool Interop(const port::NativeD3D12FrameInterop& f,port::NativeD3D12FrameOutput*,void* context,std::string*) {
    Require(f.device&&f.queue&&f.commands&&f.color&&f.depth,"interop exposes genuine D3D12 objects");
    Require(f.width==64&&f.height==48&&f.frame>0,"interop real frame dimensions");
    Require(f.color->GetDesc().Width==64&&f.depth->GetDesc().Height==48,"interop resource sizes match metadata");
    ++*static_cast<int*>(context);return true;
}
struct ExtensionOutput {
    Microsoft::WRL::ComPtr<ID3D12Resource> color;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv;
    bool fail = false, invalidOutput = false;
    int calls = 0;
};
static bool OutputInterop(const port::NativeD3D12FrameInterop& f,port::NativeD3D12FrameOutput* output,void* user,std::string* error) {
    auto& context=*static_cast<ExtensionOutput*>(user);
    ++context.calls;
    if(context.fail){if(error)*error="intentional extension failure";return false;}
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=128;desc.Height=96;
    desc.DepthOrArraySize=desc.MipLevels=1;desc.Format=context.invalidOutput?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    Require(SUCCEEDED(f.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&context.color))),"extension creates genuine larger output");
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
    Require(SUCCEEDED(f.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&context.rtv))),"extension output RTV");
    const auto rtv=context.rtv->GetCPUDescriptorHandleForHeapStart();f.device->CreateRenderTargetView(context.color.Get(),nullptr,rtv);
    const float blue[4]={0,.25f,1,1};f.commands->ClearRenderTargetView(rtv,blue,0,nullptr);
    output->color=context.color.Get();output->state=D3D12_RESOURCE_STATE_RENDER_TARGET;return true;
}
int main() {
    port::GpuDeviceDesc desc;desc.width=64;desc.height=48;std::string error;
    auto native=port::CreateD3D12Device(desc,&error);Require(native!=nullptr,error.c_str());
    auto reference=port::CreateD3D11Device(desc,&error);Require(reference!=nullptr,error.c_str());
    Require(!std::strcmp(native->Name(),"d3d12"),"backend is explicitly native D3D12");
    port::GpuDrawList list;list.width=64;list.height=48;list.model.push_back(Constants(.8f,.2f,.1f));
    port::GpuDraw draw;draw.count=6;list.draws.push_back(draw);const auto quad=Quad();
    native->SetStaticVertices(quad.data(),quad.size());reference->SetStaticVertices(quad.data(),quad.size());
    const auto original=Render(*native,list);Compare(original,Render(*reference,list),"opaque model");
    Require(original[(24*64+32)*4]>150,"actual model reaches target");
    for(int cull:{1,2,3}){list.draws[0].state.cull=cull;Compare(Render(*native,list),Render(*reference,list),"cull winding");}list.draws[0].state.cull=1;
    auto nearQuad=Quad(.2f);std::vector<game::ModelVertex> combined(quad.begin(),quad.end());combined.insert(combined.end(),nearQuad.begin(),nearQuad.end());
    native->SetStaticVertices(combined.data(),combined.size());reference->SetStaticVertices(combined.data(),combined.size());
    list.model.push_back(Constants(.1f,.7f,.3f));auto second=draw;second.first=6;second.constants=1;list.draws.push_back(second);
    Compare(Render(*native,list),Render(*reference,list),"reversed depth near over far");
    list.draws[1].state.blendEnable=1;list.draws[1].state.srcBlend=5;list.draws[1].state.dstBlend=6;list.model[1].diffuse[3]=.5f;
    Compare(Render(*native,list),Render(*reference,list),"source alpha blending");
    list.draws.resize(1);list.model[0]=Constants();
    port::RgbaImage image;image.width=image.height=16;image.pixels.resize(16*16*4);
    for(int y=0;y<16;++y)for(int x=0;x<16;++x){auto* p=image.pixels.data()+(y*16+x)*4;p[0]=x*17;p[1]=y*17;p[2]=(x+y)%2?255:0;p[3]=255;}
    const auto nt=native->CreateTexture(image),rt=reference->CreateTexture(image);Require(nt&&rt,"immutable mip texture upload");
    list.model[0].flags[0]=1;list.draws[0].texture=nt;auto rlist=list;rlist.draws[0].texture=rt;
    for(bool linear:{false,true})for(bool mips:{false,true}){list.draws[0].state.linear=rlist.draws[0].state.linear=linear;list.draws[0].state.mipmapped=rlist.draws[0].state.mipmapped=mips;Compare(Render(*native,list),Render(*reference,rlist),"texture filter and mip chain");}
    // Dynamic/sprite streams are consumed at Submit, including replacements after an earlier submission.
    native->SetDynamicVertices(quad.data(),quad.size());reference->SetDynamicVertices(quad.data(),quad.size());
    list.draws[0].stream=rlist.draws[0].stream=port::GpuDraw::Stream::Dynamic;
    Compare(Render(*native,list),Render(*reference,rlist),"dynamic vertex stream");
    std::array<game::TLVertex,4> sprite{};
    const float xy[4][2]={{120,100},{500,100},{120,380},{500,380}};
    for(int i=0;i<4;++i){sprite[i].x=xy[i][0];sprite[i].y=xy[i][1];sprite[i].z=.2f;sprite[i].rhw=1;sprite[i].diffuse=0xff2a79c1;}
    native->SetSpriteVertices(sprite.data(),sprite.size());reference->SetSpriteVertices(sprite.data(),sprite.size());
    port::GpuDrawList sl;sl.width=64;sl.height=48;port::SpriteConstants sc{};sc.screen[2]=-1;sl.sprite.push_back(sc);
    port::GpuDraw sd;sd.pipeline=port::GpuDraw::Pipeline::Sprite;sd.stream=port::GpuDraw::Stream::Sprite;sd.count=4;sl.draws.push_back(sd);
    Compare(Render(*native,sl),Render(*reference,sl),"sprite ARGB swizzle");
    port::GraphicsOptions options;options.postAA=1;options.sharpening=2;options.brightness=63;options.colorGrade=1;
    Require(native->ConfigureGraphics(options,&error),error.c_str());Require(reference->ConfigureGraphics(options,&error),error.c_str());
    // Different SM5.0/5.1 compilers can select opposite FXAA contrast branches
    // at an exactly equal threshold. Bound this to one output pixel and 16/255;
    // all basic raster/filter/sprite cases above retain strict byte tolerance.
    Compare(Render(*native,list,true),Render(*reference,rlist,true),"real scaled presentation/color/FXAA",2,4,16);
    for(int unsupported:{0,1,2,3,4,5,6}){auto rejected=options;switch(unsupported){case 0:rejected.msaa=4;break;case 1:rejected.postAA=2;break;case 2:rejected.spatialUpscaler=1;break;case 3:rejected.softParticles=1;break;case 4:rejected.ambientOcclusion=1;break;case 5:rejected.alphaToCoverage=true;break;case 6:rejected.materialMaps=true;break;}
        Require(!native->ConfigureGraphics(rejected,&error),"unsupported option rejected");Require(native->EffectiveGraphics()==options,"unsupported option rejection is transactional");}
    options={};Require(native->ConfigureGraphics(options,&error),error.c_str());Require(reference->ConfigureGraphics(options,&error),error.c_str());
    int calls=0;Require(port::SetNativeD3D12FrameCallback(*native,Interop,&calls),"attach native callback");Require(!port::SetNativeD3D12FrameCallback(*reference,Interop,&calls),"never pretend another renderer is native D3D12");
    const float clear[4]={0,0,0,1};Require(native->BeginFrame(clear),"begin interop frame");Require(!native->ConfigureGraphics(options,&error),"reject config during frame");
    native->Submit(list);Require(port::InvokeNativeD3D12FrameCallback(*native,&error),error.c_str());
    std::vector<uint8_t> before,after;Require(native->ReadPixels(&before),"first same-frame readback");Require(native->ReadPixels(&after),"second same-frame readback");Require(before==after,"readback continuation preserves current frame");native->EndFrame();Require(calls==1,"interop only runs explicitly");
    ExtensionOutput output;Require(port::SetNativeD3D12FrameCallback(*native,OutputInterop,&output),"attach output extension");
    Require(native->BeginFrame(clear),"begin substituted output");native->Submit(list);Require(native->ProcessScene(&error),error.c_str());
    native->Submit(sl);std::vector<uint8_t> substituted;Require(native->ReadPresentation(128,96,&substituted),"read substituted output with HUD");native->EndFrame();
    Require(substituted[2]>250&&substituted[0]<2,"actual extension output is presented");
    const auto center=(48*128+64)*4;Require(std::abs(int(substituted[center])-42)<2&&std::abs(int(substituted[center+1])-121)<2,"HUD renders at output resolution after extension");
    output.fail=true;Require(native->BeginFrame(clear),"begin extension failure fallback");native->Submit(list);
    Require(!native->ProcessScene()&&native->HudCompositionError()=="intentional extension failure","automatic scene callback failure is observable without an error argument");
    Require(native->ReadPixels(&after),"original scene survives extension failure");native->EndFrame();Require(after==before,"extension failure retains original raster scene");
    // A successful SDK callback can still produce a resource the compositor
    // cannot consume. It must surface the fault through the same runtime path.
    output.fail=false;output.invalidOutput=true;Require(native->BeginFrame(clear),"begin invalid extension output");native->Submit(list);
    Require(!native->ProcessScene()&&native->HudCompositionError().find("unsupported output texture")!=std::string::npos,"compositor validation failure is observable without an error argument");
    Require(native->ReadPixels(&after),"native color survives invalid extension output");native->EndFrame();Require(after==before,"invalid SDK output preserves native scene");
    const int failedCalls=output.calls;
    auto fallback=play::RestoreNativeReconstruction(*native,32768,96,"injected allocation/SDK failure");
    Require(fallback.detached&&!fallback.outputRestored&&native->TargetWidth()==64&&native->TargetHeight()==48,"failed full-output allocation retains the previous valid native target");
    Require(fallback.notice.find("until restart")!=std::string::npos&&fallback.notice.find("64x48")!=std::string::npos,"native fallback explicitly distinguishes persistent SDK failure and retained target dimensions");
    Require(Render(*native,list)==before&&output.calls==failedCalls&&native->HudCompositionError().empty(),"failed reconstruction is detached and is not retried on the next native frame");
    fallback=play::RestoreNativeReconstruction(*native,128,96,"injected reconstruction failure");
    Require(fallback.detached&&fallback.outputRestored&&native->TargetWidth()==128&&native->TargetHeight()==96,"reconstruction recovery restores actual output resolution between frames");
    Require(reference->ResizeTarget(128,96),"native fallback reference size");
    list.width=rlist.width=128;list.height=rlist.height=96;
    Compare(Render(*native,list),Render(*reference,rlist),"reconstruction failure full-resolution native recovery");
    Require(native->ResizeTarget(96,64)&&native->TargetWidth()==96&&native->TargetHeight()==64,"native target resize");
    native->ClearTextures();Require(native->CreateTexture(image)!=0,"texture cache clear permits new uploads");
    HWND window=CreateWindowW(L"STATIC",L"Native D3D12 test",WS_OVERLAPPEDWINDOW,0,0,360,280,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Require(window!=nullptr,"create hidden native swap-chain test window");
    auto windowDesc=desc;windowDesc.offscreen=false;windowDesc.nativeWindow=window;windowDesc.followWindowSize=true;
    auto windowGpu=port::CreateD3D12Device(windowDesc,&error);Require(windowGpu!=nullptr,error.c_str());
    int ww=0,wh=0;Require(windowGpu->WindowSizePixels(&ww,&wh)&&ww>0&&wh>0,"native client size");
    Require(windowGpu->BeginFrame(clear),"real HWND swap-chain frame");windowGpu->EndFrame();
    Require(windowGpu->TargetWidth()==ww&&windowGpu->TargetHeight()==wh,"follow-window target uses actual client size");
    SetWindowPos(window,nullptr,0,0,440,340,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    Require(windowGpu->WindowSizePixels(&ww,&wh),"resized native client size");
    Require(windowGpu->BeginFrame(clear),"real flip swap chain resizes");windowGpu->EndFrame();
    Require(windowGpu->TargetWidth()==ww&&windowGpu->TargetHeight()==wh,"resized internal target follows client");
    windowGpu.reset();DestroyWindow(window);
    puts("PASS: genuine native D3D12 raster, streams, texture mips, reversed depth, sprites, presentation, transactional options and typed interop");
}
