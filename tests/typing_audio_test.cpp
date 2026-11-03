#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "game/world_internal.h"
#include "game/sound_table.h"

namespace game {
void TypingWordCancelAudio(World::Impl&, bool hadSelection);
void TypingKeyAudio(World::Impl&, bool mistake);
}
namespace {
void Require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}

void CheckCombatKeys(game::World::Impl& world) {
    constexpr uint32_t p1Shot = 0x003416A9u, p2Shot = 0x003316A9u;
    constexpr uint32_t p1Miss = 0x005517A9u, p2Miss = 0x005617A9u;
    world.gameMode = 6;
    world.canShoot = 1;
    world.player[0].state = 5;
    world.player[1].state = 9;
    auto step = [&](const char* p1, const char* p2, std::initializer_list<uint32_t> expected) {
        world.sounds.clear();
        world.typingCharacters = p1;
        world.typingCharacters2 = p2;
        ++world.frame;
        world.TypingStep();
        if (world.sounds.size() != expected.size()) {
            std::fprintf(stderr, "FAIL: frame %u expected %zu key sounds, got %zu\n",
                         static_cast<unsigned>(world.frame), expected.size(), world.sounds.size());
            std::exit(1);
        }
        size_t i = 0;
        for (uint32_t id : expected) {
            const auto& event = world.sounds[i++];
            Require(event.kind == game::World::kPlayGameSound && event.id == (game::kTypingSoundFlag | id),
                    "correct letters shoot and wrong letters play the active player's miss sound, in order");
        }
    };
    step("", "", {});
    step("X", "", {}); // No shootable target.
    auto actor = std::make_unique<game::ActorTask>();
    actor->typingOriginType = 0x41;
    actor->typingFlags = 8;
    actor->viewPos[2] = -100;
    world.RegisterTarget(actor.get());
    world.ts.Create([](game::Task&) {}, std::move(actor));
    step("", "", {});
    Require(world.typingCachedTargets.size() == 1, "sound fixture has a shootable actor");
    const game::TypingWordRecord word{30, 0, "ABCADE", {}};
    auto target = world.typingCachedTargets.begin()->second.first;
    target.forcedWord = &word;
    world.typing.reset();
    world.typing.syncTargets({target});
    step("\r\t", "", {});
    step("X", "", {p1Miss}); // A wrong first letter gives feedback without selecting the word.
    step("AXB", "", {p1Shot, p1Miss, p1Shot});
    world.player[1].state = 5;
    step("", "", {});
    step("", "XAXXB", {p2Miss, p2Shot, p2Miss, p2Miss, p2Shot});
    step("C", "C", {p1Shot, p2Shot});
    world.cfg.typingOriginal = true;
    Require(world.typing2.original().immediate(13), "P2 collects the Original mode Hamburger");
    step("A", "AX", {p2Miss}); // Both players' weapon sounds are muted, but errors remain audible.
    world.typing2.original().clearSourceFlags(32);
    step("D", "", {p1Shot});
    step("E", "", {p1Shot}); // Finishing this flag-8 target plays exactly one shot.
    Require(world.typing.stats().completedWords == 1, "the final key completed the target");
    for (uint32_t id : {p1Shot, p2Shot, p1Miss, p2Miss}) {
        const auto* recording = world.typingReference.Sound(id);
        Require(recording && !recording->path.empty(), "the typing sound bank maps every key cue to a recording");
        if (recording->path.rfind("TypingSound/", 0) == 0) {
            const auto* bytes = world.typingReference.SoundData(recording->path);
            Require(bytes && bytes->size() > 44, "embedded key audio has a WAV payload");
        }
    }
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
    world.typingReferenceActive = false;
    game::TypingWordCancelAudio(world,true);
    world.typingReferenceActive = true;
    world.cfg.portTyping = false;
    game::TypingWordCancelAudio(world,true);
    Require(world.sounds.empty(),"typing cancellation stays silent in native gun mode and without a reference");
    world.cfg.portTyping = true;
    world.typingReferenceActive = false;
    game::TypingKeyAudio(world, false);
    game::TypingKeyAudio(world, true);
    Require(world.sounds.size() == 2 && world.sounds[0].id == 0x003416A9u && world.sounds[1].id == 0x000200A9u,
            "base-game assets supply a gunshot and an error sound when the typing bank is absent");
    world.sounds.clear();
    if (const char* path=std::getenv("HOTD2_TYPING_REFERENCE"); path && *path) {
        std::string error;
        Require(world.typingReference.Load(path,&error),error.c_str());
        world.typingReferenceActive=true;
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
        world.runner->skipping = false;
        CheckCombatKeys(world);
        game::SetCurrentWorld(nullptr);
    }
    std::puts("typing_audio_test: OK");
}
