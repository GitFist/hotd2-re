#include "port/play/attraction_audio.h"
#include "game/exe_image.h"
#include "game/typing_reference.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>

namespace {
void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct Backend:game::SoundBackend {
    struct Channel {bool buffered=false,playing=false;std::string path;};
    std::array<Channel,game::kSndChannels> channels;
    unsigned stops=0;
    bool Open(int c,const std::string& path,bool)override {channels[c]={true,false,path};return true;}
    bool Duplicate(int from,int to)override {channels[to]=channels[from];channels[to].playing=false;return true;}
    bool Play(int c,bool)override {channels[c].playing=channels[c].buffered;return channels[c].buffered;}
    void Pause(int c)override{channels[c].playing=false;}
    void Stop(int c)override{channels[c].playing=false;++stops;}
    void Release(int c)override{channels[c]={};}
    bool IsPlaying(int c)override{return channels[c].playing;}
    bool Silent()const {for(const auto& c:channels)if(c.playing)return false;return true;}
};
}
int main(int argc,char** argv) {
    if(argc<2){std::fprintf(stderr,"usage: typing_attract_test <native-game-dir>\n");return 2;}
    const std::string dir=argv[1];
    const char* bank=std::getenv("HOTD2_TYPING_REFERENCE");
    Require(bank&&*bank,"supply the prepared Typing bank");
    game::TypingReference reference;std::string error;
    Require(reference.Load(bank,&error,false),error.c_str());
    game::ExeImage exe;game::SoundTable table;
    Require(exe.Load((std::filesystem::path(dir)/"Hod2.exe").string())&&table.Load(exe),"load attraction sound tables");
    table.SetTypingReference(&reference);
    Backend backend;game::SoundDriver driver(backend);play::AttractionAudio audio;
    game::World live,control;
    Require(live.Load(dir)&&control.Load(dir),"load paired live worlds");
    game::WorldConfig cfg;cfg.portFeatures=cfg.portModernMenu=true;
    live.Boot(cfg);control.Boot(cfg);
    live.ForceMode(4,0x10000,0,0);control.ForceMode(4,0x10000,0,0);
    live.Step();control.Step();
    const auto liveRng=live.RandState();const int liveFrame=live.Frame();
    play::AttractPreview preview;
    Require(preview.Step(dir,true,0),preview.Error().c_str());
    const auto* normal=preview.Get();
    Require(normal&&!preview.Typing()&&normal->GameMode()==3,"normal Home retains its original title attraction");
    const auto normalRng=normal->RandState();const int normalFrame=normal->Frame();
    audio.Update(driver,table,preview,true);
    const uint32_t normalBgm=driver.CurrentBgm();
    const std::string normalMusic=backend.channels[game::kSndBgmChannel].path;
    bool geometry[3]={},prompts[3]={},typed[3]={},sourceCamera[3]={};
    std::set<uint32_t> bgm;
    for(int tick=0;tick<1536*3;++tick) {
        Require(preview.Step(dir,true,1,true),preview.Error().c_str());
        Require(preview.Typing()&&preview.Get()&&preview.Get()!=normal,"Typing Home uses an independent Typing world");
        const auto& world=*preview.Get();
        const int index=tick/1536;
        const int stages[]={0,1,3},scenes[]={4,28,5};
        if(world.GameMode()!=6||world.Events().mode!=stages[index])
            std::fprintf(stderr,"typing attract tick %d: game %d stage %d scene %d expected stage %d\n",tick,world.GameMode(),world.Events().mode,world.Events().scene,stages[index]);
        Require(world.GameMode()==6&&world.Events().mode==stages[index],"authored demos run in original stage order for1536frames each");
        if(tick%1536==0)Require(world.Events().scene==scenes[index]&&world.Events().cfg.gameMode==5&&world.Events().variant==0,
            "each Typing demo uses its actual source scene and demonstration variant");
        Require(world.PortTypingActive()&&world.Hud().player[0].shots==0,"Typing attraction uses words instead of gunfire");
        geometry[index]|=!world.Draws().empty();
        prompts[index]|=!world.PortTypingPrompts().empty();
        typed[index]|=world.PortTypingStats().completedWords>0;
        sourceCamera[index]|=world.Events().camera.camId>=0x4000;
        for(const auto& events:{preview.WarmupSounds(),preview.Sounds()})for(const auto& sound:events)
            if(sound.kind==game::World::kPlayGameSound&&(sound.id&game::kTypingSoundFlag)&&
               ((sound.id&~game::kTypingSoundFlag)>>28)==1)bgm.insert(sound.id&~game::kTypingSoundFlag);
        audio.Update(driver,table,preview,true);
        Require(normal->Frame()==normalFrame&&normal->RandState()==normalRng,"Typing preview pauses the normal attraction without changing its cycle");
        Require(live.Frame()==liveFrame&&live.RandState()==liveRng,"attraction cannot advance live gameplay RNG or frame state");
    }
    for(int i=0;i<3;++i) {
        if(!geometry[i]||!prompts[i]||!typed[i]||!sourceCamera[i])std::fprintf(stderr,"Typing demo %d: geometry %d prompts %d typed %d source-camera %d\n",i,geometry[i],prompts[i],typed[i],sourceCamera[i]);
        Require(geometry[i]&&prompts[i]&&typed[i]&&sourceCamera[i],"all three authored Typing demos display source cameras/geometry, real prompts and automatic typing");
    }
    Require(bgm.count(0x10000001)&&bgm.count(0x10000000)&&bgm.count(0x10000010),"all three Typing demos request their original source music IDs");
    Require(preview.Step(dir,true,1,true)&&preview.Get()->Events().mode==0,"Typing attraction wraps its source demo list");
    const auto typingFrame=preview.Get()->Frame();
    Require(preview.Step(dir,true,0,false)&&preview.Get()==normal&&preview.Get()->Frame()==normalFrame,"returning to normal Home resumes the retained original attraction");
    audio.Update(driver,table,preview,true);
    Require(driver.CurrentBgm()==normalBgm&&backend.channels[game::kSndBgmChannel].path==normalMusic,"switching previews restores normal attraction music without Typing audio leakage");
    Require(preview.Step(dir,true,0,true)&&preview.Get()->Frame()==typingFrame,"returning to Typing Home resumes its retained demonstration");
    audio.Update(driver,table,preview,true);
    Require(preview.Step(dir,false,1,true)&&!preview.Get(),"leaving the menu hides Typing attraction");
    audio.Update(driver,table,preview,true);
    Require(backend.Silent(),"leaving Typing Home stops only its isolated attraction audio");
    live.Step();control.Step();
    Require(live.RandState()==control.RandState()&&live.Frame()==control.Frame(),"live gameplay resumes identically after all preview scenes");
    std::puts("PASS: three authored Typing demonstrations, source words/music, automatic typing, normal-cycle preservation and isolated audio");
}
