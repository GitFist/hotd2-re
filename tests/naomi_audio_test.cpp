#include "port/assets/naomi_audio.h"
#include "port/render/png_read.h"
#include "port/audio/audio_output.h"
#include "game/exe_image.h"
#include "game/sound_driver.h"
#include "game/world_sound.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstring>
#ifdef HOTD2_TEST_SDL
#include <SDL3/SDL.h>
#include "port/audio/sdl_audio_backend.h"
#endif
#ifdef _WIN32
#include "port/audio/xaudio_backend.h"
#endif

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)

struct Backend : game::SoundBackend {
    std::string path;
    bool playing = false, loop = false, rejectNative = false;
    game::SoundLoopRegion region{};
    bool Open(int, const std::string& p, bool) override { path=p; return !(rejectNative && p.starts_with("NaomiSound/")); }
    bool Duplicate(int,int) override { return false; }
    bool Play(int,bool l) override { loop=l;return playing=!(rejectNative && path.starts_with("NaomiSound/")); }
    bool SetLoopRegion(int,uint32_t a,uint32_t b,uint32_t rate) override { region={a,b,rate};return !(rejectNative && path.starts_with("NaomiSound/")); }
    void Pause(int ch) override { if(ch==game::kSndBgmChannel)playing=false; }
    void Stop(int ch) override { if(ch==game::kSndBgmChannel)playing=false; }
    void Release(int ch) override { if(ch==game::kSndBgmChannel){playing=false;region={};} }
    bool IsPlaying(int ch) override { return ch==game::kSndBgmChannel && playing; }
};

static void TestOutput(port::AudioOutput& output,const port::NaomiAudio& bank,const game::SoundTable& table,const char* directory) {
    CHECK(output.Init(directory));
    output.SetNaomiAudio(&bank);
    for(const auto& sound:bank.Sounds()) {
        const int ch=sound.index<20?game::kSndBgmChannel:1;
        CHECK(output.Open(ch,sound.path,ch==game::kSndBgmChannel));
        output.SetGain(ch,0.f);
        const auto& r=sound.region;
        if(r.endFrame) CHECK(output.SetLoopRegion(ch,r.startFrame,r.endFrame,r.sampleRate));
        CHECK(output.Play(ch,r.endFrame!=0));
        CHECK(output.IsPlaying(ch));
        output.Pause(ch);CHECK(!output.IsPlaying(ch));
        CHECK(output.Play(ch,r.endFrame!=0));
        output.Stop(ch);CHECK(!output.IsPlaying(ch));
        output.Release(ch);
    }
    game::SoundDriver driver(output);
    game::SoundDriver::Volumes muted{{0.f,0.f,0.f}};
    driver.SetVolumes(muted);
    game::World::SoundEvent event{};
    event.kind=game::World::kStrengthChainsawSound;event.id=0x4d17a9;event.gameMode=6;
    game::PlaySoundEvent(driver,table,event);
    CHECK(driver.LoopCount()==1 && output.IsPlaying(1));
    driver.StopLoopedSe();
    CHECK(driver.LoopCount()==0 && !output.IsPlaying(1));
    std::printf("Checked all native recordings through %s\n",output.Name());
}

int main(int argc,char** argv) {
    if (argc!=3) { std::puts("naomi_audio_test <game-dir> <prepared-bank>");return 2; }
    port::NaomiAudio bank;
    CHECK(!bank.Load(std::string(argv[2])+".missing"));
    CHECK(bank.Load(argv[2]));
    CHECK(bank.Sounds().size()==43);
    game::ExeImage exe;
    CHECK(exe.Load(std::string(argv[1])+"/Hod2.exe"));
    game::SoundTable table;
    CHECK(table.Load(exe));
    table.SetNaomiSound(&bank.Sounds());
    int music=0,effects=0;
    for (const auto& sound:bank.Sounds()) {
        std::vector<uint8_t> bytes;
        CHECK(bank.Read(sound.path,bytes));
        game::WavInfo wav;
        CHECK(game::ParseWavHeader(bytes.data(),bytes.size(),wav));
        CHECK(wav.sampleRate==44100 && wav.channels==2 && wav.bitsPerSample==16 && wav.dataOffset==44);
        CHECK(wav.dataSize+44==bytes.size());
        CHECK(!sound.region.endFrame || sound.region.endFrame<=wav.dataSize/4);
        std::vector<uint8_t> playback;
        CHECK(bank.ReadPlayback(sound.path, playback));
        CHECK(playback.size() == bytes.size() && std::memcmp(playback.data(), bytes.data(), 44) == 0);
        if (sound.index >= 20) {
            CHECK(playback == bytes); // native effect gains and pans remain exact
        } else {
            double rawEnergy = 0, playbackEnergy = 0;
            int peak = 0;
            bool stereo = true, silence = true;
            for (size_t at = 44; at + 4 <= bytes.size(); at += 4) {
                int16_t raw[2], balanced[2];
                std::memcpy(raw, bytes.data() + at, 4);
                std::memcpy(balanced, playback.data() + at, 4);
                for (int ch = 0; ch < 2; ++ch) {
                    rawEnergy += double(raw[ch]) * raw[ch];
                    playbackEnergy += double(balanced[ch]) * balanced[ch];
                    peak = std::max(peak, std::abs(int(balanced[ch])));
                    silence &= raw[ch] != 0 || balanced[ch] == 0;
                }
                // Shared gain for L/R, allowing half a PCM unit's rounding.
                stereo &= std::abs(double(raw[0])*balanced[1]-double(raw[1])*balanced[0]) <=
                          .51*(std::abs(int(raw[0]))+std::abs(int(raw[1])))+1;
            }
            const double boost = 10*std::log10(playbackEnergy/rawEnergy);
            CHECK(boost > 4.5 && boost < 6.1);
            CHECK(peak <= 32113 && stereo && silence);
            std::printf("Music %u: +%.2f dB, peak %.4f; identical length and loop positions\n", sound.index, boost, peak/32768.);
        }
        if (sound.index<20) {
            ++music;
            const uint32_t id=0x10000000|sound.index;
            const auto arcade=game::ResolveSound(table,id,{6,0});
            CHECK(arcade.path==sound.path);
            CHECK(arcade.loop==(sound.region.endFrame!=0));
            CHECK(arcade.region.startFrame==sound.region.startFrame);
            CHECK(game::ResolveSound(table,id,{7,0}).path==sound.path);
            CHECK(game::ResolveSound(table,id,{6,1}).path=="Sound\\BGM\\"+table.BgmA()[sound.index]);
            CHECK(game::ResolveSound(table,id).path=="Sound\\BGM\\"+table.BgmA()[sound.index]);
            Backend backend;
            game::SoundDriver driver(backend);
            driver.PlayGameSound(table,id,{6,0});
            CHECK(backend.path==sound.path && backend.loop==arcade.loop);
            driver.Command(0x2a0); CHECK(!backend.playing);
            driver.Command(0x3a0); CHECK(backend.playing && backend.loop==arcade.loop);
            driver.OnActivate(false); driver.OnActivate(true);
            CHECK(backend.loop==arcade.loop && backend.region.endFrame==sound.region.endFrame);
            backend.rejectNative=true;
            driver.PlayGameSound(table,id,{6,0});
            CHECK(backend.path==arcade.fallbackPath && backend.playing);
        } else {
            ++effects;
            const auto effect=game::ResolveSound(table,sound.index,{6,0});
            CHECK(effect.path==sound.path && effect.loop);
            CHECK(effect.region.startFrame==sound.region.startFrame && effect.region.endFrame==sound.region.endFrame);
        }
    }
    CHECK(music==17 && effects==26);
    CHECK(!game::ResolveSound(table,0x10000008,{6,0}).loop);
    CHECK(!game::ResolveSound(table,0x1000000c,{6,0}).loop);
    CHECK(!game::ResolveSound(table,0x1000000d,{6,0}).loop);
    // Same fire sample, different original sequence gains (115 vs 127).
    // Native mixed output measured 0.805; the two PC files were equally loud.
    auto energy=[&](uint32_t id){
        std::vector<uint8_t> wav;
        const auto* sound=table.FindNaomiSound(id);
        CHECK(sound && bank.Read(sound->path,wav));
        double sum=0;
        for(size_t frame=20000;frame<50000 && 44+4*frame+2<=wav.size();++frame) {
            int16_t sample;std::memcpy(&sample,wav.data()+44+4*frame,2);sum+=double(sample)*sample;
        }
        return sum;
    };
    const double fireGain=std::sqrt(energy(0x718a9)/energy(0x1aa9));
    CHECK(std::abs(fireGain-.805)<.002);
    CHECK(game::ResolveSound(table,0x1000001a).path=="NaomiSound/1.wav"); // standard Chapter 1 in Extras
    CHECK(game::ResolveSound(table,0x50000001,{6,0}).action==game::SoundAction::None); // Typing never aliases
    std::vector<uint8_t> rejected;
    CHECK(!bank.Read("NaomiSound/../../anything",rejected));
    CHECK(!bank.Read("NaomiSound/2.wav",rejected));
    // Exact-output decompression rejects corrupt checksums, truncated streams,
    // and output above the declared bound before allocating arbitrary expansion.
    const uint8_t compressed[]={120,156,203,72,205,201,201,7,0,6,44,2,21}; // zlib("hello")
    CHECK(port::ZlibInflateExact(compressed,sizeof(compressed),rejected,5));
    CHECK(!port::ZlibInflateExact(compressed,sizeof(compressed),rejected,4));
    CHECK(!port::ZlibInflateExact(compressed,sizeof(compressed)-1,rejected,5));
    auto corrupt=std::vector<uint8_t>(compressed,compressed+sizeof(compressed));corrupt.back()^=1;
    CHECK(!port::ZlibInflateExact(corrupt.data(),corrupt.size(),rejected,5));
    // Mutate only an owned copy: the shipped bank must survive failed validation.
    namespace fs=std::filesystem;
    const auto scratch=fs::temp_directory_path()/("hotd2-audio-bank-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(scratch);
    const auto copy=scratch/"invalid.bank";
    fs::copy_file(argv[2],copy);
    std::vector<char> header(1048);
    {std::ifstream file(copy,std::ios::binary);file.read(header.data(),header.size());}
    auto change=[&](int offset,uint32_t value){
        std::fstream file(copy,std::ios::binary|std::ios::in|std::ios::out);
        file.write(header.data(),header.size());file.seekp(offset);file.write(reinterpret_cast<char*>(&value),4);
    };
    port::NaomiAudio invalid;
    change(12,0xffffffff);CHECK(!invalid.Load(copy.string()));
    change(20,16);CHECK(!invalid.Load(copy.string())); // payload overlaps index
    change(36,0xffffffff);CHECK(!invalid.Load(copy.string())); // loop outside PCM
    change(16,1);CHECK(!invalid.Load(copy.string())); // missing/duplicate sound identity
    {std::fstream file(copy,std::ios::binary|std::ios::in|std::ios::out);
        file.write(header.data(),header.size());file.seekp(1048);file.put('\0');}
    CHECK(invalid.Load(copy.string()));
    CHECK(!invalid.Read(bank.Sounds()[0].path,rejected));
    fs::remove(copy);fs::remove(scratch);
#ifdef HOTD2_TEST_SDL
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
    {auto output=port::CreateSdlAudio();TestOutput(*output,bank,table,argv[1]);}
#endif
#ifdef _WIN32
    {port::XAudioBackend output;TestOutput(output,bank,table,argv[1]);}
#endif
    std::printf("%s Naomi audio: %d music cues, %d effects; routing, data, loops, transport and fallback\n",failures?"FAIL":"PASS",music,effects);
    return failures?1:0;
}
