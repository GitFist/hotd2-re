#include "port/play/attraction_audio.h"
#include "game/exe_image.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace {
void Require(bool value,const char* message) {
    if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
bool Near(float a,float b){return std::fabs(a-b)<0.00001f;}

struct Backend:game::SoundBackend {
    struct Channel{bool buffered=false,playing=false,loop=false;std::string path;bool operator==(const Channel&) const=default;};
    struct Played{int channel;bool loop;std::string path;bool operator==(const Played&) const=default;};
    std::array<Channel,game::kSndChannels> channels{};
    std::array<float,game::kSndChannels> gains{};
    std::vector<Played> played;
    bool Open(int channel,const std::string& path,bool) override {
        if(channels[channel].buffered)return false;
        channels[channel]={true,false,false,path};return true;
    }
    bool Duplicate(int from,int to) override {
        if(channels[to].buffered||!channels[from].buffered)return false;
        channels[to]=channels[from];channels[to].playing=false;return true;
    }
    bool Play(int channel,bool loop) override {
        auto& c=channels[channel];if(!c.buffered)return false;
        c.playing=true;c.loop=loop;played.push_back({channel,loop,c.path});return true;
    }
    void Pause(int channel) override{channels[channel].playing=false;}
    void Stop(int channel) override{channels[channel].playing=false;}
    void Release(int channel) override{channels[channel]={};}
    bool IsPlaying(int channel) override{return channels[channel].playing;}
    void SetGain(int channel,float gain) override{gains[channel]=gain;}
    void FinishOneShots(){for(auto& c:channels)if(!c.loop)c.playing=false;}
    bool Silent() const{for(const auto& c:channels)if(c.playing)return false;return true;}
};

void RequireSamePlayback(const Backend& actual,const Backend& direct,size_t first,int frame,int mode) {
    bool same=actual.played.size()==direct.played.size()&&actual.channels==direct.channels;
    if(same)for(size_t i=first;i<actual.played.size();++i)if(actual.played[i]!=direct.played[i]){same=false;break;}
    if(!same)std::fprintf(stderr,"audio mismatch at attraction frame %d, mode %d: %zu plays, direct %zu\n",
                         frame,mode,actual.played.size(),direct.played.size());
    Require(same,"audible attraction preserves direct sound-event playback, channel state and event order");
    for(size_t i=0;i<actual.gains.size();++i)
        Require(Near(actual.gains[i],direct.gains[i]),"all attraction channel gains match the independent sound driver");
}
}

int main(int argc,char** argv) {
    if(argc!=2){std::fprintf(stderr,"usage: attraction_audio_test <game-dir>\n");return 2;}
    game::ExeImage exe;game::SoundTable table;
    Require(exe.Load(std::string(argv[1])+"/Hod2.exe")&&table.Load(exe),"load original sound tables");

    constexpr float expectedMaster[]={0.f,.005623413f,.01f,.017782794f,.031622777f,.05623413f,
                                      .1f,.17782794f,.31622777f,.56234133f,1.f};
    for(int level=0;level<=10;++level) {
        const float gain=play::MasterVolumeGain(level);
        Require(Near(gain,expectedMaster[level]),"all eleven Master levels match the logarithmic scale");
        if(level>0)Require(gain>play::MasterVolumeGain(level-1),"Master steps are strictly increasing");
        if(level>1)Require(Near(20.f*std::log10(gain/play::MasterVolumeGain(level-1)),5.f),
                           "adjacent nonzero Master levels have equal 5 dB steps");
    }
    Require(play::MasterVolumeGain(0)==0&&play::MasterVolumeGain(-20)==0,
            "Master zero and negative values produce an exact mute");
    Require(play::MasterVolumeGain(10)==1&&play::MasterVolumeGain(30)==1,
            "Master ten and higher values stop at full scale");
    Require(Near(play::MasterVolumeGain(1),.005623413f)&&Near(play::MasterVolumeGain(5),.05623413f),
            "Master one is quiet and the midpoint remains well below full scale");
    auto volumes=play::GameAudioVolumes(5,8,6,4);
    Require(Near(volumes.gain[0],.044987306f)&&Near(volumes.gain[1],.033740479f)&&Near(volumes.gain[2],.022493653f),
            "logarithmic Master multiplies the existing linear category levels");
    const auto quieterDefaults=play::GameAudioVolumes(1,4,4,4);
    const auto fullCategoriesAtOne=play::GameAudioVolumes(1,10,10,10);
    constexpr float previousLinearMasterOne=.1f;
    for(int category=0;category<3;++category)
        Require(Near(quieterDefaults.gain[category],.002249365f)&&Near(fullCategoriesAtOne.gain[category],.005623413f)&&
                Near(quieterDefaults.gain[category],fullCategoriesAtOne.gain[category]*.4f)&&
                fullCategoriesAtOne.gain[category]<previousLinearMasterOne/10.f,
                "Master one is much quieter than the old linear 0.1 gain while categories remain proportional");
    const auto quieterAttraction=play::AttractionAudioVolumes(1,4);
    const auto fullAttractionAtOne=play::AttractionAudioVolumes(1,10);
    for(int category=0;category<3;++category)
        Require(Near(quieterAttraction.gain[category],.002249365f)&&Near(fullAttractionAtOne.gain[category],.005623413f)&&
                Near(quieterAttraction.gain[category],fullAttractionAtOne.gain[category]*.4f),
                "attraction uses the same Master curve with its own linear level");
    for(float gain:play::GameAudioVolumes(0,10,10,10).gain)Require(gain==0,"Master zero silences the game");
    for(float gain:play::GameAudioVolumes(1,0,0,0).gain)Require(gain==0,"category zero stays silent with Master at one");
    for(float gain:play::AttractionAudioVolumes(5,2).gain)Require(Near(gain,.011246826f),"attraction obeys Master independently");
    for(float gain:play::AttractionAudioVolumes(0,10).gain)Require(gain==0,"Master zero also silences attraction");
    for(float gain:play::AttractionAudioVolumes(10,0).gain)Require(gain==0,"attraction zero silences its entire mix");
    for(float gain:play::AttractionAudioVolumes(50,50).gain)Require(gain==1,"gains cannot exceed full scale");
    const auto clampedCategories=play::GameAudioVolumes(10,-3,20,4);
    Require(clampedCategories.gain[0]==0&&clampedCategories.gain[1]==1&&Near(clampedCategories.gain[2],.4f),
            "category bounds clamp without changing the linear scale");

    Backend liveBackend,previewBackend;
    game::SoundDriver liveDriver(liveBackend),previewDriver(previewBackend);
    liveDriver.SetVolumes(play::GameAudioVolumes(5,8,6,4));
    Require(liveDriver.PreviewMusic(table,0x1000001a,true),"start independent Extras recording");
    const auto livePath=liveBackend.channels[game::kSndBgmChannel].path;
    const size_t livePlays=liveBackend.played.size();
    liveDriver.SetVolumes(play::GameAudioVolumes(0,8,6,4));
    for(float gain:liveBackend.gains)Require(gain==0,"changing Master to zero immediately mutes every existing game channel");
    liveDriver.SetVolumes(play::GameAudioVolumes(5,8,6,4));
    Require(liveBackend.played.size()==livePlays&&liveBackend.channels[game::kSndBgmChannel].playing&&
            Near(liveBackend.gains[game::kSndBgmChannel],.044987306f),
            "raising Master restores the current recording's gain without restarting it");
    previewDriver.SetVolumes(play::AttractionAudioVolumes(5,2));
    play::AttractPreview preview;play::AttractionAudio audio;
    Require(preview.Step(argv[1],true,0),preview.Error().c_str());
    Require(!preview.WarmupSounds().empty()&&preview.Sounds().empty(),"initial scenery warmup captures audio separately");
    Require(preview.Get()&&preview.Get()->GameMode()==3,"attraction begins with the original title flythrough");
    audio.Update(previewDriver,table,preview,true);
    for(const auto& sound:previewBackend.played)
        Require(sound.channel==game::kSndBgmChannel||sound.loop,"title warmup never bursts old dialogue or gunshot one-shots");
    for(float gain:previewBackend.gains)Require(Near(gain,.011246826f),"attraction gain applies to every backend channel");
    const size_t startupPlays=previewBackend.played.size();
    audio.Update(previewDriver,table,preview,true);
    Require(previewBackend.played.size()==startupPlays,"render/update without a simulation tick cannot replay events");

    // Prime only persistent warmup state identically. Thereafter, direct playback of
    // every original event is an independent oracle for the attraction's mixer.
    Backend directBackend;game::SoundDriver directDriver(directBackend);
    directDriver.SetVolumes(play::AttractionAudioVolumes(5,2));
    play::AttractionAudio warmupAudio;warmupAudio.Update(directDriver,table,preview,true);
    RequireSamePlayback(previewBackend,directBackend,0,preview.Get()->Frame(),preview.Get()->GameMode());
    std::array<int,17> modePlays{},modeVoices{},modeMusic{},modeStops{};
    bool sawOneShot=false,sawOnceMusic=false,sawStory11Track=false,sawStory10Track=false;
    bool volumeExercised=false,completedCycle=false;
    std::set<std::string> music;
    int previousMode=preview.Get()->GameMode();
    for(int frame=0;frame<60000;++frame) {
        previewBackend.FinishOneShots();
        directBackend.FinishOneShots();
        Require(preview.Step(argv[1],true),"advance the natural title, tutorial, demo and story cycle");
        const size_t before=previewBackend.played.size();
        for(const auto& event:preview.Sounds()) {
            const size_t first=directBackend.played.size();
            game::PlaySoundEvent(directDriver,table,event);
            if(event.gameMode<0||event.gameMode>=int(modePlays.size()))continue;
            if(event.kind==game::World::kStopAll||event.kind==game::World::kStopSlot||
               (event.kind==game::World::kPlayGameSound&&(event.id>>28)==8))++modeStops[event.gameMode];
            for(size_t i=first;i<directBackend.played.size();++i) {
                const auto& sound=directBackend.played[i];
                ++modePlays[event.gameMode];
                if(sound.channel==game::kSndVoiceChannel)++modeVoices[event.gameMode];
                if(sound.channel==game::kSndBgmChannel) {
                    ++modeMusic[event.gameMode];
                    sawStory11Track|=event.gameMode==11&&event.id==0x10000014&&!sound.loop;
                    sawStory10Track|=event.gameMode==10&&event.id==0x10000008&&sound.loop;
                }
            }
        }
        audio.Update(previewDriver,table,preview,true);
        RequireSamePlayback(previewBackend,directBackend,before,preview.Get()->Frame(),preview.Get()->GameMode());
        for(size_t i=before;i<previewBackend.played.size();++i) {
            const auto& sound=previewBackend.played[i];
            if(sound.channel==game::kSndBgmChannel){music.insert(sound.path);sawOnceMusic|=!sound.loop;}
            else if(!sound.loop)sawOneShot=true;
        }
        const size_t after=previewBackend.played.size();
        audio.Update(previewDriver,table,preview,true);
        Require(previewBackend.played.size()==after,"rendering twice never repeats live tutorial, voice or story commands");
        if(!volumeExercised&&previewBackend.channels[game::kSndBgmChannel].playing) {
            const auto playing=previewBackend.channels;
            previewDriver.SetVolumes(play::AttractionAudioVolumes(0,2));
            directDriver.SetVolumes(play::AttractionAudioVolumes(0,2));
            for(float gain:previewBackend.gains)Require(gain==0,"Master zero immediately mutes existing attraction channels too");
            previewDriver.SetVolumes(play::AttractionAudioVolumes(5,2));
            directDriver.SetVolumes(play::AttractionAudioVolumes(5,2));
            Require(previewBackend.played.size()==after&&previewBackend.channels==playing,
                    "unmuting active attraction music restores gain without restarting any channel");
            for(float gain:previewBackend.gains)Require(Near(gain,.011246826f),"unmuted attraction restores the same master-scaled mix");
            volumeExercised=true;
        }
        const int mode=preview.Get()->GameMode();
        if(previousMode==10&&mode==3){completedCycle=true;break;}
        previousMode=mode;
    }
    Require(completedCycle,"natural attraction completes both story sequences and returns to the title");
    Require(modePlays[9]>0&&modeVoices[9]>0&&modeStops[9]>0,"how-to-play forwards original voice/effects and clears the title audio");
    Require(modePlays[5]>0&&modeMusic[5]>0,"gameplay demo keeps its original effects and music");
    Require(modePlays[11]>0&&modeMusic[11]>0&&modeStops[11]>0,"first story forwards its original soundtrack and stop commands");
    Require(modePlays[10]>0&&modeMusic[10]>0&&modeStops[10]>0,"second story forwards its original soundtrack and stop commands");
    // These original story sequences use BGM tracks rather than separate voice
    // events. Tutorial voices above still exercise the dedicated voice channel.
    Require(sawStory11Track&&sawStory10Track,"both stories preserve their original soundtrack IDs and once/loop behavior");
    Require(sawOneShot&&sawOnceMusic&&music.size()>=3,"full cycle plays effects, nonloop story music and distinct demo/story soundtracks");
    Require(volumeExercised,"volume changes were verified while real attraction music was playing");

    // Open Extras during the first story's one-shot BGM. Unlike discarded voice
    // lines and gunshots, that current soundtrack is eligible for restoration.
    bool atStoryMusic=false;
    for(int frame=0;frame<60000&&!atStoryMusic;++frame) {
        previewBackend.FinishOneShots();
        Require(preview.Step(argv[1],true),"advance to story music before opening Extras");
        audio.Update(previewDriver,table,preview,true);
        for(const auto& event:preview.Sounds())
            if(event.gameMode==11&&event.kind==game::World::kPlayGameSound&&event.id==0x10000014)atStoryMusic=true;
    }
    Require(atStoryMusic&&previewBackend.channels[game::kSndBgmChannel].playing&&
            !previewBackend.channels[game::kSndBgmChannel].loop,"Extras scenario begins during real nonloop story music");
    const auto storyMusicPath=previewBackend.channels[game::kSndBgmChannel].path;

    audio.Update(previewDriver,table,preview,false);
    Require(previewBackend.Silent(),"entering Extras immediately stops attraction channels");
    const size_t mutedPlays=previewBackend.played.size();
    for(int frame=0;frame<240;++frame) {
        Require(preview.Step(argv[1],true),"visual attraction continues behind Extras");
        audio.Update(previewDriver,table,preview,false);
    }
    Require(previewBackend.played.size()==mutedPlays,"muted attraction never starts background sounds behind Extras");
    Require(preview.Step(argv[1],true,0),"resume attraction with no new simulation tick");
    audio.Update(previewDriver,table,preview,true);
    Require(previewBackend.channels[game::kSndBgmChannel].playing&&
            previewBackend.channels[game::kSndBgmChannel].path==storyMusicPath&&
            !previewBackend.channels[game::kSndBgmChannel].loop,"leaving Extras restores current story music with its original nonloop behavior");
    for(size_t i=mutedPlays;i<previewBackend.played.size();++i)
        Require(previewBackend.played[i].channel==game::kSndBgmChannel||previewBackend.played[i].loop,
                "returning from Extras never replays discarded dialogue/shots");
    for(float gain:previewBackend.gains)Require(Near(gain,.011246826f),"Extras isolation preserves every attraction channel's volume");
    preview.Step(argv[1],false);
    audio.Update(previewDriver,table,preview,true);
    Require(previewBackend.Silent(),"leaving the menu stops the attraction even if caller's audible flag is true");
    Require(liveBackend.played.size()==livePlays&&liveBackend.channels[game::kSndBgmChannel].playing&&
            liveBackend.channels[game::kSndBgmChannel].path==livePath&&Near(liveBackend.gains[game::kSndBgmChannel],.044987306f),
            "attraction playback, muting and teardown cannot affect main-game/Extras audio or gains");
    std::puts("PASS attraction audio: complete title/tutorial/demo/story cycle, direct sound-event parity, voices and nonloop story music, Master and attraction gains, Extras isolation and teardown");
}
