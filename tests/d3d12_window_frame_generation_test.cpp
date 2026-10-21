// Real native window submission with the actual optional AMD interpolation SDK.
// Pixel correctness is covered separately by fsr_frame_generation_test.
#include "game/camera.h"
#include "render/d3d12_device.h"
#include "render/d3d12_frame_pacer.h"
#include "render/fsr_frame_generation.h"
#include "render/model_renderer.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
static void Require(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}
struct Window {HWND handle=nullptr;~Window(){if(handle)DestroyWindow(handle);}};
static game::Model Card(float z,float sx,float sy,float red,float green){
    game::Model model;game::ModelMesh mesh;auto& w=mesh.material.words;w[1]=4u<<29;w[2]=0x80000;w[8]=w[9]=0xffffffff;
    const float material[5]={1,1,red,green,.2f};memcpy(w+10,material,sizeof(material));game::ModelStrip strip;
    for(auto xy:{std::pair<float,float>{-sx,-sy},{-sx,sy},{sx,-sy},{sx,sy}}){game::ModelVertex v{};v.pos[0]=xy.first;v.pos[1]=xy.second;v.pos[2]=z;v.normal[2]=1;strip.vertices.push_back(v);}
    mesh.strips.push_back(strip);model.meshes.push_back(mesh);return model;
}
struct Adapter {port::FsrFrameGeneration fg;std::string failure;int calls=0;
    static bool Invoke(const port::NativeD3D12FinalFrame& frame,port::NativeD3D12GeneratedFrames* out,void* user,std::string* error){auto& a=*static_cast<Adapter*>(user);if(!a.fg.Evaluate(frame,out,error)){a.failure=error?*error:"AMD evaluation failed";return false;}++a.calls;return true;}};
static int Run(const char* directory){
    for(int n=1;n<=4;++n)Require(port::FrameGenerationVsyncInterval(120.f*n)==n,"exact display rate selects correct DXGI interval");
    Require(port::FrameGenerationVsyncInterval(119.88f)==1&&port::FrameGenerationVsyncInterval(239.76f)==2,"fractional NTSC-compatible rates retain exact cadence");
    for(float hz:{0.f,60.f,100.f,144.f,165.f,180.f,200.f,600.f})Require(port::FrameGenerationVsyncInterval(hz)==0,"incompatible refresh cannot silently halve simulation/render cadence");
    Window window;window.handle=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"Frame generation GPU validation",WS_POPUP,16,16,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);Require(window.handle!=nullptr,"create nonactivating validation window");
    std::string error;port::GpuDeviceDesc desc;desc.width=640;desc.height=480;desc.offscreen=false;desc.nativeWindow=window.handle;auto gpu=port::CreateD3D12Device(desc,&error);Require(bool(gpu),error);
    Adapter adapter;if(!adapter.fg.Initialize(port::GetNativeD3D12Device(*gpu),directory,&error)){printf("SKIP actual window AMD frame generation: %s\n",error.c_str());return 0;}
    Require(port::SetNativeD3D12FinalFrameCallback(*gpu,Adapter::Invoke,&adapter,&error),error);port::ModelRenderer renderer;Require(renderer.Init(gpu.get(),&error),error);port::AssetStore assets;
    const auto wall=Card(-100,120,90,.15f,.3f),actor=Card(-90,12,20,.9f,.2f);auto item=[](const game::Model* m,uint32_t id){port::ModelRenderer::SceneItem i;i.model=m;i.motionTracked=true;i.motionIdentity.task=id;i.motionIdentity.model=id;i.noMeshCull=true;i.cullable=false;return i;};auto W=item(&wall,1),A=item(&actor,2);
    const auto view=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();port::ModelRenderer::Lighting light;light.enabled=true;for(int k=0;k<3;++k){light.ambient[k]=1;light.diffuse[k]=0;}
    port::ModelRenderer::SpriteDraw hud;hud.isp=4u<<29;hud.tsp=0x80000;hud.hud=true;hud.v[0]={24,24,0,1,0xffc03070,0,0,0};hud.v[1]={104,24,0,1,0xffc03070,0,1,0};hud.v[2]={24,104,0,1,0xffc03070,0,0,1};hud.v[3]={104,104,0,1,0xffc03070,0,1,1};const std::vector<port::ModelRenderer::SpriteDraw> sprites={hud};
    SetWindowPos(window.handle,HWND_TOPMOST,16,16,640,480,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    uint64_t expectedGenerated=0;double minimumPair=1e9;auto previous=std::chrono::steady_clock::now();
    for(int frame=0;frame<16;++frame){
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        const auto start=std::chrono::steady_clock::now();A.transform[12]=-24+frame*3.f;renderer.SetScene(assets,{W,A});port::TemporalFrameInfo temporal;temporal.enabled=true;temporal.frameId=frame;temporal.reset=frame==0||frame==8;temporal.frameTimeMs=frame?std::chrono::duration<float,std::milli>(start-previous).count():1000.f/60;previous=start;renderer.SetTemporalFrame(temporal);
        Require(port::SetNativeD3D12FrameGenerationPaused(*gpu,frame==9),"set real pause suppression");const auto before=port::GetNativeD3D12FrameGenerationStats(*gpu);const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin real window FG frame");renderer.DrawGame(640,480,projection.m,&light,nullptr,view.m,&sprites,nullptr,projection.m);gpu->EndFrame();const auto after=port::GetNativeD3D12FrameGenerationStats(*gpu);
        Require(adapter.calls==frame+1,adapter.failure.empty()?after.status:adapter.failure);Require(after.retainedPresented==before.retainedPresented+1,"retained Present must return S_OK; occluded submissions do not count");
        if(after.lastDisabled||after.lastReset){Require(after.generatedPresented==before.generatedPresented,"reset/pause/gap cannot submit generated frame");}
        else {++expectedGenerated;Require(after.generatedPresented==before.generatedPresented+1,"eligible real generated Present returns S_OK");Require(after.lastPairIntervalMs>=8.1,"unsynchronized actual pair retains the120Hz half interval");minimumPair=std::min(minimumPair,after.lastPairIntervalMs);}
        if(frame==0||frame==8||frame==9)Require(after.lastReset&&after.generatedPresented==before.generatedPresented,"explicit cut/pause suppresses generated presentation");
        std::this_thread::sleep_until(start+std::chrono::duration<double>(1.0/60));
    }
    auto stats=port::GetNativeD3D12FrameGenerationStats(*gpu);Require(expectedGenerated>=10&&stats.generatedPresented==expectedGenerated&&stats.retainedPresented==16,"real window produced both successful output presents without fabricated counts");
    printf("Actual window unsync: %llu generated + %llu retained S_OK presents; minimum pair interval %.3fms; display %.1fHz\n",static_cast<unsigned long long>(stats.generatedPresented),static_cast<unsigned long long>(stats.retainedPresented),minimumPair,stats.outputRefreshHz);
    auto options=gpu->EffectiveGraphics();options.syncInterval=1;Require(gpu->ConfigureGraphics(options,&error),error);const auto before=stats;
    for(int k=0;k<3;++k){const auto start=std::chrono::steady_clock::now();A.transform[12]+=3;renderer.SetScene(assets,{W,A});port::TemporalFrameInfo temporal;temporal.enabled=true;temporal.frameId=16+k;temporal.frameTimeMs=1000.f/60;renderer.SetTemporalFrame(temporal);Require(port::SetNativeD3D12FrameGenerationPaused(*gpu,false),"resume VSync validation");const float clear[4]={0,0,0,1};Require(gpu->BeginFrame(clear),"begin actual VSync policy frame");renderer.DrawGame(640,480,projection.m,&light,nullptr,view.m,&sprites,nullptr,projection.m);gpu->EndFrame();std::this_thread::sleep_until(start+std::chrono::duration<double>(1.0/60));}
    stats=port::GetNativeD3D12FrameGenerationStats(*gpu);Require(stats.retainedPresented==before.retainedPresented+3,"VSync retains actual rendered frames");const int interval=port::FrameGenerationVsyncInterval(stats.outputRefreshHz);
    if(!interval)Require(stats.generatedPresented==before.generatedPresented&&!stats.lastPresentationEligible&&stats.status.find("requires")!=std::string::npos,"incompatible actual VSync display explicitly retains rendered frames only");
    else Require(stats.generatedPresented>before.generatedPresented&&stats.lastPresentationEligible,"compatible actual VSync display accepts generated pair with exact interval");
    printf("Actual window VSync: %.1fHz, interval%d, generated delta%llu, retained delta%llu\n",stats.outputRefreshHz,interval,static_cast<unsigned long long>(stats.generatedPresented-before.generatedPresented),static_cast<unsigned long long>(stats.retainedPresented-before.retainedPresented));
    puts("PASS: actual native AMD generated/retained S_OK window submissions, half-interval pacing, reset suppression and refresh-compatible VSync policy");return 0;
}
int main(int argc,char** argv){try{Require(argc==2,"usage: d3d12_window_frame_generation_test <signed-AMD-DLL-directory>");return Run(argv[1]);}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
