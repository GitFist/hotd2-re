// Real hidden-window swap-chain tests. No fullscreen/display-mode changes.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "platform/platform.h"

static void Require(bool ok,const char* why) {
    if(!ok) {std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
static void Pump(platform::Platform& platform) {
    platform::Event event;
    while(platform.PollEvent(&event)) {}
}
static bool Frame(platform::Platform& platform,port::GpuDevice& gpu) {
    Pump(platform);
    const float color[4]={.1f,.2f,.3f,1};
    const double started=platform.Seconds();
    const bool ready=gpu.BeginFrame(color);
    // The low-queue readiness check must not sleep behind presentation. A generous
    // bound allows CI scheduling noise, but catches a blocking wait on a hidden window.
    Require(!gpu.EffectiveGraphics().lowLatency || platform.Seconds()-started<.25,
            "low latency BeginFrame blocked instead of skipping a busy/minimized surface");
    if(!ready) return false;
    gpu.Submit(port::GpuDrawList{gpu.TargetWidth(),gpu.TargetHeight()});
    gpu.EndFrame();
    return true;
}

int main(int argc,char** argv) {
    const auto renderer=argc>1 && !std::strcmp(argv[1],"sdlgpu") ? platform::Renderer::SdlGpu : platform::Renderer::D3D11;
    std::string error;
    int rendered=0,skipped=0;
    bool tearing=false,lowQueue=false,hdr=false;
    {
        platform::Platform platform;
        platform::Options options;options.hidden=true;options.input=platform::Input::Mouse;
        options.width=320;options.height=240;options.title="HOTD2 presentation integration test";
        Require(platform.Init(options,&error),error.c_str());
        auto gpu=platform.CreateGpu(renderer,128,96,false,0,&error);
        Require(gpu!=nullptr,error.c_str());
        const auto caps=gpu->GetGraphicsCapabilities();
        tearing=caps.tearing;lowQueue=caps.lowLatency;hdr=caps.hdrOutput;
        Require(caps.maxAnisotropy>=1,"invalid capability report");
        auto original=gpu->EffectiveGraphics();
        auto hdrSetting=original;hdrSetting.hdrOutput=true;
        const bool hdrApplied=gpu->ConfigureGraphics(hdrSetting,&error);
        Require(hdrApplied==caps.hdrOutput,"HDR capability did not match apply result");
        if(hdrApplied) {
            Require(gpu->EffectiveGraphics().hdrOutput,"HDR request was silently ignored");
            Frame(platform,*gpu);
        } else Require(gpu->EffectiveGraphics()==original,"unsupported HDR changed accepted state");
        Require(gpu->ConfigureGraphics(original,&error),error.c_str());
        Require(!gpu->EffectiveGraphics().hdrOutput,"HDR did not restore SDR output");
        auto setting=original;setting.allowTearing=true;setting.syncInterval=0;
        const bool tearApplied=gpu->ConfigureGraphics(setting,&error);
        Require(tearApplied==caps.tearing,"tearing capability did not match apply result");
        if(tearApplied) Require(gpu->EffectiveGraphics().allowTearing && gpu->EffectiveGraphics().syncInterval==0,
                                "tearing request was silently ignored");
        else Require(gpu->EffectiveGraphics()==original,"unsupported tearing changed accepted state");
        Require(gpu->ConfigureGraphics(original,&error),error.c_str());
        setting=original;setting.lowLatency=true;
        const bool queueApplied=gpu->ConfigureGraphics(setting,&error);
        Require(queueApplied==caps.lowLatency,"low-queue capability did not match apply result");
        if(queueApplied) {
            Require(gpu->EffectiveGraphics().lowLatency,"low-queue request was silently ignored");
            const double start=platform.Seconds();
            for(int frame=0;frame<64;++frame) {
                if(Frame(platform,*gpu)) ++rendered;else ++skipped;
            }
            Require(platform.Seconds()-start<5,"hidden-window readiness loop stalled");
        } else Require(gpu->EffectiveGraphics()==original,"unsupported queue mode changed accepted state");
        setting=original;setting.syncInterval=1;
        Require(gpu->ConfigureGraphics(setting,&error),error.c_str());
        Require(gpu->EffectiveGraphics().syncInterval==1,"VSync selection not accepted");
        Frame(platform,*gpu);
        Require(platform.SetWindowSize(400,300),"resize hidden window");
        Pump(platform);
        int width=0,height=0;Require(gpu->WindowSizePixels(&width,&height),"query window pixel size");
        Require(width==400 && height==300,"resized window size was not reported");
        Require(gpu->ResizeTarget(160,120),"resize internal target");
        gpu->SetPresent({16,12,368,276,0});
        setting.syncInterval=0;setting.allowTearing=caps.tearing;setting.lowLatency=caps.lowLatency;
        Require(gpu->ConfigureGraphics(setting,&error),error.c_str());
        Frame(platform,*gpu); // validates resize retained immutable tearing/waitable creation flags
        setting=original;setting.lowLatency=false;setting.allowTearing=false;
        Require(gpu->ConfigureGraphics(setting,&error),error.c_str());
        Require(!gpu->EffectiveGraphics().lowLatency && !gpu->EffectiveGraphics().allowTearing,"presentation toggles did not turn Off");
        gpu.reset(); // release queued resources/wait handle before destroying their window
    }
    {
        platform::Platform platform;platform::Options options;options.window=false;
        Require(platform.Init(options,&error),error.c_str());
        auto gpu=platform.CreateGpu(renderer,64,64,false,0,&error);Require(gpu!=nullptr,error.c_str());
        const auto caps=gpu->GetGraphicsCapabilities();
        Require(!caps.tearing && !caps.lowLatency && !caps.hdrOutput,"headless target advertised window presentation features");
        const auto original=gpu->EffectiveGraphics();auto setting=original;setting.allowTearing=true;
        Require(!gpu->ConfigureGraphics(setting,&error) && gpu->EffectiveGraphics()==original,"headless tearing was accepted or mutated state");
        setting=original;setting.lowLatency=true;
        Require(!gpu->ConfigureGraphics(setting,&error) && gpu->EffectiveGraphics()==original,"headless low-queue mode was accepted or mutated state");
        setting=original;setting.hdrOutput=true;
        Require(!gpu->ConfigureGraphics(setting,&error) && gpu->EffectiveGraphics()==original,"headless HDR was accepted or mutated state");
        Frame(platform,*gpu);
    }
    std::printf("PASS %s hidden presentation: tearing %s, low queue %s, HDR %s, %d frames / %d nonblocking skips; VSync, resize, toggles, headless rejection, shutdown\n",
                platform::Name(renderer),tearing?"supported":"unavailable",lowQueue?"supported":"unavailable",hdr?"supported":"unavailable",rendered,skipped);
}
