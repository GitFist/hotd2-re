#include <cstdio>
#include <cstdlib>

#include "game/world_internal.h"
#include "game/sound_table.h"

namespace game {
void TypingWordCancelAudio(World::Impl&, bool hadSelection);
void TypingWordCompletionAudio(World::Impl&, uint32_t actorFlags, int player);
}
namespace {
void Require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
}
int main() {
    game::World::Impl world;
    world.cfg.portTyping = true;
    world.typingReferenceActive = true;
    game::TypingWordCancelAudio(world,false);
    Require(world.sounds.empty(),"Esc without a selected word is silent");
    game::TypingWordCancelAudio(world,true);
    Require(world.sounds.size() == 1 && world.sounds.back().id == (game::kTypingSoundFlag|0x003E16A9u),
            "selected-word cancellation emits the original namespaced reload cue");
    world.sounds.clear();
    for (uint32_t flags : {0u,1u,2u,4u,9u,10u,12u,15u})
        game::TypingWordCompletionAudio(world,flags,0);
    Require(world.sounds.empty(),"ordinary completions and higher-priority actor branches add no generic cue");
    game::TypingWordCompletionAudio(world,0x08000008u,0);
    game::TypingWordCompletionAudio(world,8,1);
    Require(world.sounds.size() == 2 && world.sounds[0].id == (game::kTypingSoundFlag|0x003416A9u) &&
            world.sounds[1].id == (game::kTypingSoundFlag|0x003316A9u),
            "flag8 word completion uses the original distinct player cues");
    world.sounds.clear();
    world.typingReferenceActive = false;
    game::TypingWordCancelAudio(world,true);
    game::TypingWordCompletionAudio(world,8,0);
    world.typingReferenceActive = true;
    world.cfg.portTyping = false;
    game::TypingWordCancelAudio(world,true);
    game::TypingWordCompletionAudio(world,8,0);
    Require(world.sounds.empty(),"typing audio helpers leave native gun mode and inactive references unchanged");
    if (const char* path=std::getenv("HOTD2_TYPING_REFERENCE"); path && *path) {
        std::string error;
        Require(world.typingReference.Load(path,&error),error.c_str());
        world.cfg.portTyping=true;
        world.gameMode=6;
        world.playersInGame=0;
        world.runner=std::make_unique<game::EventRunner>(world.mem,world,&world.env,&world.exe);
        game::SetCurrentWorld(&world);
        int voice=-1,duration=0;
        for (int id=0;id<512;++id)
            if (const auto* record=world.typingReference.Dialogue(id,0); record && record->duration>1) {
                voice=id; duration=record->duration; break;
            }
        Require(voice>=0,"prepared source dialogue fixture exists");
        world.TypingVoice(voice);
        for (int frame=0;frame<duration;++frame) {
            world.hudSprites.clear();
            world.ts.RunFrame();
            world.ts.FreeDead();
        }
        Require(world.hudSprites.empty() && world.ts.Live().empty(),
                "expired source voice task stops before submitting a final subtitle frame");
        world.TypingVoice(voice);
        world.runner->skipping=true;
        world.hudSprites.clear();
        world.ts.RunFrame();
        world.ts.FreeDead();
        Require(world.hudSprites.empty() && world.ts.Live().empty(),
                "cutscene skip removes source subtitles in the same frame");
        game::SetCurrentWorld(nullptr);
    }
    std::puts("typing_audio_test: OK");
}
