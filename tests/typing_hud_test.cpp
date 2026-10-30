#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "game/world_internal.h"
#include "game/typing_results.h"
#include "game/sound_table.h"
#include "game/mstack.h"

namespace game {
void TypingWordHud(World::Impl& world);
void TypingWordMistakeHud(World::Impl& world, uint32_t serial);
void TypingWordCompleteHud(World::Impl& world, const TypingCompletion& done);
void TypingWordCompleteHud(World::Impl& world, const TypingCompletion& done,std::string_view text);
void TypingStrengthPostersHud(World::Impl&, uint32_t, uint32_t,
                             const std::array<TypingWordRecord, 4>&, unsigned, bool, int, int);
}

namespace {
void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

void ContinuePresentation(const char* path) {
    game::World::Impl world;
    std::string error;
    Require(world.typingReference.Load(path, &error), error.c_str());
    world.typingReferenceActive = world.cfg.portTyping = true;
    world.gameMode = 6;
    world.subState = 4;
    world.player[0].state = 4;
    world.player[1].state = 9;
    world.contCountdown = 0x9FFF;
    const auto sprite = [&](int raw, uint32_t source) -> const game::SpriteDef* {
        const auto found = std::find_if(world.hudSprites.begin(), world.hudSprites.end(), [&](const auto& draw) {
            return draw.id == 0x4000 + raw && draw.source == source;
        });
        return found == world.hudSprites.end() ? nullptr : &*found;
    };
    game::TypingWordHud(world);
    const auto* label = sprite(0x242, 0x004514E9);
    const auto* nine = sprite(0x68, 0x0045151E);
    Require(label && label->x == 128 && label->y == 200 && label->depth == 1 && label->flags == 0 &&
            nine && nine->x == 482 && nine->y == 188,
            "prepared typing HUD displays the original shared CONTINUE label and countdown while input is not ready");
    world.contCountdown = 0x8FFF;
    world.hudSprites.clear(); world.draws.clear();
    game::TypingWordHud(world);
    Require(sprite(0x67, 0x0045151E) && !sprite(0x68, 0x0045151E),
            "typing Continue digit follows the existing native countdown without a separate timer");
    world.paused = true;
    world.hudSprites.clear(); world.draws.clear();
    game::TypingWordHud(world);
    Require(!sprite(0x242, 0x004514E9), "pausing hides the typing Continue presentation");
    world.paused = false;
    world.subState = 2;
    world.player[0].state = 5;
    world.player[0].lives = 3;
    world.hudSprites.clear(); world.draws.clear();
    game::TypingWordHud(world);
    Require(!sprite(0x242, 0x004514E9) && !sprite(0x67, 0x0045151E),
            "resuming play removes the shared typing Continue label and countdown");
    world.player[1].state = 4;
    world.player[1].contShown = 0x7FFF;
    world.hudSprites.clear(); world.draws.clear();
    game::TypingWordHud(world);
    label = sprite(0x242, 0x00486AC0);
    const auto* seven = sprite(0x66, 0x00486B00);
    Require(label && label->x == 368 && label->y == 170 && label->sx == .6f && label->sy == .8f &&
            seven && seven->x == 580 && seven->y == 160 && seven->sx == .6f && seven->sy == .8f &&
            !sprite(0x242, 0x004514E9),
            "one continuing player uses the original smaller per-player label and countdown");
}

void DrillHudPhases(const char* path) {
    game::World::Impl world;
    std::string error;
    Require(world.typingReference.Load(path, &error), error.c_str());
    world.typingReferenceActive = world.cfg.portTyping = true;
    world.cfg.eventMode = 7; world.cfg.gameType = 2;
    world.gameMode = 6; world.subState = 2;
    world.player[0].state = 5; world.player[0].lives = 1;
    world.typingDrillGame.active = true;
    const auto model = [&](int raw) {
        const int id = world.typingReference.SourceModel(raw);
        return std::any_of(world.draws.begin(), world.draws.end(), [&](const auto& draw) { return draw.model == id; });
    };
    const auto badge = [&] {
        return std::any_of(world.hudSprites.begin(), world.hudSprites.end(), [](const auto& sprite) {
            return sprite.id == 0x4000 + 0x62B;
        });
    };
    for (const int scene : {1, 13, 15}) {
        world.typingDrillGame.sourceScene = scene;
        for (const auto phase : {game::TypingDrillGamePhase::Instructions, game::TypingDrillGamePhase::Ready}) {
            world.typingDrillGame.phase = phase;
            world.draws.clear(); world.hudSprites.clear();
            game::TypingWordHud(world);
            Require(!badge() && !model(0x1CAB) && !model(0x1C28),
                    "source Drill Ready/Go hides player, PERFECT and LIFE HUD even when native flags are already clear");
        }
        world.typingDrillGame.phase = game::TypingDrillGamePhase::Running;
        world.draws.clear(); world.hudSprites.clear();
        game::TypingWordHud(world);
        Require(badge() == (scene != 15) && model(0x1CAB) == (scene != 1) && model(0x1C28) == (scene == 15),
                "Running restores only the original course's player, PERFECT and zero-point LIFE frame");
    }
}
}

int main(int argc, char** argv) {
    const char* path = argc >= 2 ? argv[1] : std::getenv("HOTD2_TYPING_REFERENCE");
    Require(path && *path, "supply the prepared PC reference bank");
    ContinuePresentation(path);
    DrillHudPhases(path);
    game::World::Impl world;
    std::string error;
    Require(world.typingReference.Load(path, &error), error.c_str());
    Require(world.typingReference.SourceModel(0x129E) > 0, "reference resolves the authored poster frame");
    world.typingReferenceActive = true;
    world.cfg.portTyping = true;
    world.typingWasReady = true;
    world.player[0].state = 5;
    world.player[1].state = 2;
    const game::TypingWordRecord word{50, 0, "A big fish.", {}};
    game::TypingTarget target;
    target.serial = 1; target.token = 17; target.forcedWord = &word;
    game::TypingTargetSpec descriptor;
    descriptor.view[0] = 0; descriptor.view[1] = 0; descriptor.view[2] = -100;
    world.typingCachedTargets.emplace(1, std::make_pair(target, descriptor));
    world.typing.syncTargets({target});
    game::TypingWordHud(world);
    Require(world.draws.size() == 27, "nine poster pieces plus both source glyph lines; spaces have no model");
    const int a = world.typingReference.SourceModel(7050);
    const int b = world.typingReference.SourceModel(7372);
    const auto count = [&](int model) {
        return std::count_if(world.draws.begin(), world.draws.end(), [&](const auto& draw) { return draw.model == model; });
    };
    Require(count(a) == 2 && count(b) == 2, "reference and typing lines initially contain every glyph");
    const int fill = world.typingReference.SourceModel(0x19D);
    Require(std::any_of(world.draws.begin(), world.draws.end(), [&](const auto& draw) {
                return draw.model == fill && draw.edit && draw.edit->diffuseRgb &&
                       draw.edit->color[0] == 0 && draw.edit->color[1] == 0 && draw.edit->color[2] == 0;
            }), "source poster initializer makes the white on-disk fill black");
    Require(world.draws[0].edit && world.draws[0].edit->diffuseRgb &&
            world.draws[0].edit->color[0] == 1 && world.draws[0].edit->color[1] == 1 &&
            world.draws[0].edit->color[2] == 0, "idle poster uses the source yellow material tint");
    const float normalScale = world.draws[0].world.m[0];
    world.draws.clear();
    world.typing.typeCharacters("A b");
    game::TypingWordHud(world);
    Require(world.draws.size() == 29 && count(a) == 1 && count(b) == 1,
            "completed letters leave the white typing line while the yellow reference remains");
    Require(world.draws[0].edit->color[0] == 1 && world.draws[0].edit->color[1] == 1 &&
            world.draws[0].edit->color[2] == 0, "P1 selection retains the source yellow frame");
    const int arrow = world.typingReference.SourceModel(0x1CCB);
    Require(count(arrow) == 4 && std::all_of(world.draws.begin(), world.draws.end(), [&](const auto& draw) {
                return draw.model != arrow || (draw.edit && draw.edit->color[0] == 1 &&
                                               draw.edit->color[1] == 0 && draw.edit->color[2] == 0);
            }), "P1 selection uses the original four red model pointers");
    for (const auto& draw : world.draws) {
        Require(draw.viewSpace && draw.fn == 0x004AF870 && draw.world.m[14] < -.8f,
                "source word models use a valid view-space HUD projection");
        for (float value : draw.world.m) Require(std::isfinite(value), "poster and glyph transforms remain finite");
    }
    world.draws.clear();
    world.cfg.typingOptions.wordSize = game::TypingWordSize::Large;
    game::TypingWordHud(world);
    Require(std::abs(world.draws[0].world.m[0] / normalScale - 32.f/27.f) < .00001f,
            "Large uses the PC depth-size limits rather than an arbitrary font multiplier");
    target.flags = 0x00010000u;
    world.typingCachedTargets.at(1).first.flags = target.flags;
    world.typing.syncTargets({target});
    world.draws.clear();
    game::TypingWordHud(world);
    Require(world.draws.size() == 29 && count(a) == 1 && count(b) == 1 &&
            std::all_of(world.draws.begin(), world.draws.end(), [](const auto& draw) {
                return draw.light.color[0] == .1f && draw.light.color[1] == .1f && draw.light.color[2] == .1f &&
                       draw.light.pitch == 0x2000 && draw.light.yaw == 0x2000;
            }), "closed actor word preserves its letters and uses the source ten-percent light color");
    target.flags = 0;
    world.typingCachedTargets.at(1).first.flags = 0;
    world.typing.syncTargets({target});
    world.draws.clear();
    game::TypingWordHud(world);
    Require(std::all_of(world.draws.begin(), world.draws.end(), [](const auto& draw) {
        return draw.light.color[0] == 1 && draw.light.color[1] == 1 && draw.light.color[2] == 1;
    }), "reopened source word restores its light without resetting progress");

    // Source UI is independent of readiness for a new word, including the life
    // row on continue screens. These are actual prepared model/sprite draws.
    world.gameMode = 6;
    world.typingWasReady = false;
    world.player[0].lives = 3;
    world.frame = world.frameCounter2BAC = 3;
    world.draws.clear(); world.hudSprites.clear();
    game::TypingWordHud(world);
    Require(world.hudSprites.size() == 4 && world.hudSprites[0].id == 0x4000 + 0x62B &&
            world.hudSprites[0].x == 28 && world.hudSprites[0].y == 412 && world.hudSprites[0].flags == 0,
            "source life label remains at its authored bottom-left position");
    Require(world.hudSprites[1].id == 0x4000 + 0x737 && world.hudSprites[1].x == 60 &&
            world.hudSprites[2].id == 0x4000 + 0x738 && world.hudSprites[2].x == 92,
            "source hearts use the seven-frame, staggered three-tick animation");
    const auto modelDraw = [&](int raw, uint32_t source = 0u) -> const game::WorldDraw* {
        const int model = world.typingReference.SourceModel(raw);
        const auto found = std::find_if(world.draws.begin(), world.draws.end(), [&](const auto& draw) {
            return draw.model == model && (!source || draw.fn == source);
        });
        return found == world.draws.end() ? nullptr : &*found;
    };
    const auto* gauge = modelDraw(0x1C28, 0x004AC720);
    Require(gauge && std::abs(gauge->world.m[12] - (-30.f/world.focal)*310.f*.2f) < .00001f &&
            gauge->world.m[14] == -6 && std::abs(gauge->world.m[0]-.08f) < .00001f,
            "perfect meter uses the original view-space origin and projected size");
    const auto* steadyCounter = modelDraw(0x1CAB, 0x004AC150);
    Require(steadyCounter && steadyCounter->world.m[14] == -6, "steady perfect counter uses the original HUD depth");
    const float steadyRatio = steadyCounter->world.m[0] / steadyCounter->world.m[14];
    world.typing.typeCharacters("ig fish.");
    world.frame = world.frameCounter2BAC = 4;
    world.draws.clear(); world.hudSprites.clear();
    game::TypingWordHud(world);
    const auto* gainedCounter = modelDraw(0x1CAB, 0x004AC470);
    Require(world.typingHud.perfectFrames == 15 && !world.typingHud.brokenStreak && gainedCounter &&
            std::abs(gainedCounter->world.m[0] / gainedCounter->world.m[14] - steadyRatio) < .000001f,
            "streak gain starts the source sixteen-tick phase without an invented size bounce");
    const auto* gainedTens = modelDraw(0x1CAC, 0x004AC470);
    const auto* gainedUnits = modelDraw(0x1CB7, 0x004AC470);
    Require(gainedTens && gainedUnits && gainedCounter->light.pitch == (15<<10)-0x2000 &&
            gainedTens->light.pitch == (19<<10)-0x2000 && gainedUnits->light.pitch == (23<<10)-0x2000 &&
            gainedCounter->light.yaw == 0 && gainedTens->light.yaw == 0 && gainedUnits->light.yaw == 0,
            "perfect label, tens and units use the source staggered light directions");
    for (int i = 0; i < 15; ++i) {
        ++world.frame; ++world.frameCounter2BAC;
        world.draws.clear(); world.hudSprites.clear();
        game::TypingWordHud(world);
    }
    const auto* settledCounter = modelDraw(0x1CAB, 0x004AC150);
    Require(world.typingHud.perfectFrames == 0 && settledCounter &&
            settledCounter->light.pitch == 0x2000 && settledCounter->light.yaw == 0x2000,
            "the sixteen-tick perfect pulse returns to the source steady light direction");
    Require(std::any_of(world.sounds.begin(), world.sounds.end(), [](const auto& sound) {
        return sound.id == (0x003616A9u | 0x40000000u);
    }), "streak gain emits the original prepared sound cue");
    ++target.token;
    world.typing.syncTargets({target});
    world.typing.typeCharacters("Ax");
    ++world.frame; ++world.frameCounter2BAC;
    world.draws.clear(); world.hudSprites.clear();
    game::TypingWordHud(world);
    Require(world.typingHud.brokenStreak && world.typingHud.perfectFrames == 79 && modelDraw(0x1CA5),
            "a broken streak starts the original eighty-tick blinking feedback");

    world.typing.reset();
    const game::TypingWordRecord key{4, 0, "A", {}};
    target.forcedWord = &key;
    for (int i = 0; i < 28; ++i) {
        ++target.token;
        world.typing.syncTargets({target});
        world.typing.typeCharacters("A");
    }
    ++world.frame; ++world.frameCounter2BAC;
    world.draws.clear(); world.hudSprites.clear();
    game::TypingWordHud(world);
    Require(modelDraw(0x11B3, 0x004AC5A0) && world.typingHud.lifeAwardAge == 1,
            "a meter award begins the original fifty-one-model life animation");
    for (int i = 0; i < 15; ++i) {
        ++world.frame; ++world.frameCounter2BAC;
        world.draws.clear(); world.hudSprites.clear();
        game::TypingWordHud(world);
    }
    Require(std::any_of(world.sounds.begin(), world.sounds.end(), [](const auto& sound) {
        return sound.id == (0x003B17A9u | 0x40000000u);
    }), "life animation emits its source cue on frame fifteen");

    world.typingQuestion = "A";
    world.typingHud.questionSlide = -952;
    world.typingHud.questionVisible = false;
    ++world.frame;
    world.draws.clear(); world.hudSprites.clear();
    game::TypingWordHud(world);
    Require(!modelDraw(7050, 0x004AFC10), "question stays hidden until the existing head readiness gate opens");
    world.typingHud.questionVisible = true;
    ++world.frame;
    world.draws.clear(); world.hudSprites.clear();
    game::TypingWordHud(world);
    const auto* slidingQuestion = modelDraw(7050, 0x004AFC10);
    Require(slidingQuestion && std::any_of(world.hudSprites.begin(), world.hudSprites.end(), [](const auto& sprite) {
        return sprite.id == 0x4000+0x97B && sprite.x == 320 && sprite.y == 360 &&
               sprite.depth == 31 && sprite.flags == 0xA;
    }), "question draws the original bottom banner and model glyphs");
    const float slidingX = slidingQuestion->world.m[12];
    world.typingHud.questionSlide = 0;
    ++world.frame;
    world.draws.clear(); world.hudSprites.clear();
    game::TypingWordHud(world);
    const auto* centeredQuestion = modelDraw(7050, 0x004AFC10);
    Require(centeredQuestion && std::abs(slidingX - centeredQuestion->world.m[12] -
            (-30.f/world.focal)*-952.f*.1f) < .00001f,
            "question slide follows the existing source -960 to zero coordinate without changing its timer");

    world.typingQuestion.clear();
    game::TypingTarget feedbackTarget;
    feedbackTarget.serial = 55; feedbackTarget.token = 99;
    game::TypingTargetSpec feedbackDescriptor;
    feedbackDescriptor.view[0] = 12;
    feedbackDescriptor.view[1] = -6;
    feedbackDescriptor.view[2] = -120;
    world.typingCachedTargets.emplace(55, std::make_pair(feedbackTarget, feedbackDescriptor));
    world.typingPositions[{55, 99}] = {127, 222};
    world.frame = world.frameCounter2BAC = 100;
    game::TypingWordMistakeHud(world, 55);
    world.draws.clear(); world.hudSprites.clear();
    game::TypingWordHud(world);
    Require(world.typingHud.mistakes.size() == 1 && std::any_of(world.hudSprites.begin(), world.hudSprites.end(), [](const auto& sprite) {
        return sprite.id == 0x4000+0xBD1 && sprite.x == 127 && sprite.y == 222 && sprite.flags == 0xA;
    }), "mistype hook snapshots the current word anchor for the original error glyph");
    world.typingCachedTargets.erase(55);
    world.typingPositions.erase({55, 99});
    for (int age = 1; age <= 16; ++age) {
        ++world.frame; ++world.frameCounter2BAC;
        world.draws.clear(); world.hudSprites.clear();
        game::TypingWordHud(world);
        const bool errorGlyph = std::any_of(world.hudSprites.begin(), world.hudSprites.end(), [](const auto& sprite) {
            return sprite.id == 0x4000+0xBD1;
        });
        Require(errorGlyph == (age < 16 && (world.frameCounter2BAC & 4)),
                "source error glyph blinks on frame bit4 for exactly sixteen ticks after a word reset");
    }
    Require(world.typingHud.mistakes.empty(), "expired source error presentation releases its anchor");

    world.typingCachedTargets.emplace(55, std::make_pair(feedbackTarget, feedbackDescriptor));
    game::TypingCompletion done;
    done.serial = 55; done.token = 99;
    done.rank = game::TypingRank::Unranked;
    done.visualRank = game::TypingRank::B;
    game::TypingWordCompleteHud(world, done);
    Require(world.typingHud.completions.size() == 1 && world.typingHud.completions.front().view[0] == 12,
            "completion hook freezes the validated target's view-space anchor");
    world.typingCachedTargets.erase(55); // Original actor can die before the feedback task ends.
    world.cfg.typingOptions.wordSize = game::TypingWordSize::Normal;
    ++world.frame; ++world.frameCounter2BAC;
    world.draws.clear(); world.hudSprites.clear();
    game::TypingWordHud(world);
    const auto* grade = modelDraw(0x1CC5, 0x004AC960);
    const auto* backdrop = modelDraw(0x1B80, 0x004AC960);
    Require(grade && backdrop && modelDraw(0x1CC3, 0x004AC960) && grade->alpha == 1.5f &&
            std::abs(backdrop->world.m[0] / grade->world.m[0] - .75f) < .000001f,
            "source completion uses the visual grade independently of score suppression and authored backdrop scale");
    const float gradeScaleX = grade->world.m[0], gradeScaleY = grade->world.m[5];
    const float gradeX = grade->world.m[12], gradeY = grade->world.m[13];
    for (int age = 1; age <= 91; ++age) {
        ++world.frame; ++world.frameCounter2BAC;
        world.draws.clear(); world.hudSprites.clear();
        game::TypingWordHud(world);
        const auto* current = modelDraw(0x1CC5, 0x004AC960);
        Require(bool(current) == (age <= 90), "completion submits source ages zero through ninety inclusive");
        if (!current) continue;
        Require(current->world.m[12] == gradeX && current->world.m[13] == gradeY &&
                std::abs(current->alpha - (1.5f-float(age)*.011111111380159855f)) < .000001f,
                "completion keeps its frozen anchor and exact source alpha curve");
        if (age == 76) {
            Require(std::abs(current->world.m[0] / gradeScaleX - 14.f/15.f) < .000001f &&
                    std::abs(current->world.m[5] / gradeScaleY - (1+15.f/14.01f)) < .000001f,
                    "last fourteen frames follow the authored horizontal collapse and vertical stretch");
        }
        if (age == 90) Require(current->world.m[0] == 0 && std::isfinite(current->world.m[5]),
                               "final source frame has finite, zero-width completion geometry");
    }
    Require(world.typingHud.completions.empty(), "completed source feedback task releases its snapshot after age ninety");

    // PC40A150 makes four rows per authored poster cell. "AB" has four
    // cells (4B06F0), and each40A370 shard consumes the seven original RNG
    // parameters before its first same-frame movement/submission.
    feedbackDescriptor.view[0]=2;feedbackDescriptor.view[1]=-1;feedbackDescriptor.view[2]=-35;
    world.typingCachedTargets.emplace(55,std::make_pair(feedbackTarget,feedbackDescriptor));
    world.typingHud.posters[{55,99}].view={2,-1,-30};
    world.rng.Seed(42);world.randCalls.clear();
    game::CrtRand expected(42);
    std::array<int,7> parameters;
    for(int& value:parameters)value=expected.Next();
    expected.Skip(15*7);
    world.draws.clear();world.hudSprites.clear();
    game::TypingWordCompleteHud(world,done,"AB");
    const auto shardCount=[&] {
        return std::count_if(world.draws.begin(),world.draws.end(),[](const auto& draw){return draw.fn==0x0040A370;});
    };
    const auto shardTasks=[&] {
        const auto tasks=world.ts.Live();
        return std::count_if(tasks.begin(),tasks.end(),[](const auto* task) {
            const auto* state=dynamic_cast<const game::WTask*>(task->data.get());
            return state && state->fn==0x0040A370;
        });
    };
    Require(shardCount()==16 && shardTasks()==16 && world.randCalls.size()==16*7 && world.rng.State()==expected.State(),
            "source completion creates its4-by-4 glass grid using exactly seven original random values per shard");
    constexpr std::array<uint32_t,7> glassSites={0x0040A22F,0x0040A264,0x0040A282,0x0040A2A0,0x0040A2D0,0x0040A300,0x0040A330};
    for(size_t i=0;i<glassSites.size();++i)
        Require(world.randCalls[i].site==glassSites[i] && world.randCalls[i].value==parameters[i],
                "glass initialization preserves the original parameter and RNG-call order");
    const auto firstGlass=world.draws.front();
    const float velocityY=(parameters[1]%5)*.07000000029802322f;
    const float velocityZ=(parameters[2]%5)*-.05000000074505806f;
    Require(firstGlass.fn==0x0040A370 && firstGlass.task!=0 && firstGlass.viewSpace &&
            firstGlass.model==world.typingReference.SourceModel(0xD56+parameters[6]%5) &&
            std::abs(firstGlass.world.m[12]-2.8125f)<.00001f &&
            std::abs(firstGlass.world.m[13]-(-.2875f+velocityY-.01f))<.00001f &&
            std::abs(firstGlass.world.m[14]-(-30+velocityZ))<.00001f &&
            std::abs(firstGlass.alpha-(1-1.f/3600))<.000001f,
            "first glass shard uses its source mesh, frozen poster grid, authored velocity and squared alpha curve");
    // The source's signed0xB60B60B7 multiply/shift divides by90. With
    // seed42 its first three angular increments are1456,273,1729.
    auto firstGlassMatrix=game::Mat4::Identity();
    game::mx::Translate(firstGlassMatrix,2.8125f,-.2975f,-30.2f);
    game::mx::RotX(firstGlassMatrix,1456);game::mx::RotY(firstGlassMatrix,273);game::mx::RotZ(firstGlassMatrix,1729);
    game::mx::Scale(firstGlassMatrix,.95f,.95f,.95f);
    for(int component=0;component<16;++component)
        Require(std::abs(firstGlass.world.m[component]-firstGlassMatrix.m[component])<.00001f,
                "seed42 glass matches independently decoded PC spin rates, rotation order and mesh size");
    world.typingCachedTargets.erase(55);world.typingHud.posters.erase({55,99});
    world.paused=true;++world.frame;
    world.draws.clear();world.ts.RunFrame();
    Require(shardCount()==0 && shardTasks()==16,"pause suspends original glass task motion without discarding its state");
    world.paused=false;++world.frame;
    world.draws.clear();world.ts.RunFrame();
    Require(shardCount()==16 && std::abs(world.draws.front().world.m[12]-(firstGlass.world.m[12]+.1f))<.00001f &&
            std::abs(world.draws.front().world.m[13]-(firstGlass.world.m[13]+velocityY-.02f))<.00001f &&
            std::abs(world.draws.front().alpha-(1-4.f/3600))<.000001f,
            "glass resumes with only its next gravity tick after the original word and actor disappear");
    for(int age=3;age<=60;++age) {
        ++world.frame;world.draws.clear();world.ts.RunFrame();
        Require(shardCount()==16,"source glass submits all shards through age60 inclusive");
        for(const auto& draw:world.draws)if(draw.fn==0x0040A370) {
            const float elapsed=age/60.f;
            Require(std::abs(draw.alpha-(1-elapsed*elapsed))<.000001f,"source glass uses its sixty-frame squared fade");
            for(float value:draw.world.m)Require(std::isfinite(value),"moving glass matrices remain finite");
        }
    }
    ++world.frame;world.draws.clear();world.ts.RunFrame();world.ts.FreeDead();
    Require(shardCount()==0 && shardTasks()==0,"source glass tasks expire after their final transparent age60 submission");
    world.typingHud.completions.clear();

    world.gameMode = 5; // Exercise word-only presentation without the gameplay life row.
    world.typing.reset(); world.typingCachedTargets.clear(); world.typingPositions.clear();
    world.typingHud.posters.clear(); world.typingHud.story = {};
    world.typingWasReady = true;
    auto enemy = std::make_unique<game::EnemyTask>();
    enemy->body[2] = 40;
    auto* enemyPtr = enemy.get();
    auto* actorTask = world.ts.Create([](game::Task&) {}, std::move(enemy));
    game::TypingTarget moving;
    moving.serial = actorTask->serial; moving.token = 700; moving.forcedWord = &word;
    game::TypingTargetSpec movingSpec;
    movingSpec.view[2] = -100;
    world.typingCachedTargets.emplace(moving.serial, std::make_pair(moving, movingSpec));
    world.typing.syncTargets({moving});
    const auto redraw = [&] {
        ++world.frame; ++world.frameCounter2BAC;
        world.draws.clear(); world.hudSprites.clear();
        game::TypingWordHud(world);
    };
    redraw();
    const auto movingKey = std::make_pair(moving.serial, moving.token);
    Require(world.typingHud.posters.at(movingKey).view[2] == -95,
            "persistent source poster stores the original five-unit depth offset");
    Require(world.draws[0].edit->color[0] == 1 && world.draws[0].edit->color[1] == 1 &&
            std::all_of(world.draws.begin(), world.draws.end(), [](const auto& draw) { return draw.alpha == -1; }),
            "source forty-unit warning is yellow while both glyph rows remain opaque");
    world.typingCachedTargets.at(moving.serial).second.view[0] = 20;
    redraw();
    Require(std::abs(world.typingHud.posters.at(movingKey).view[0] - (10.f+10.f/30.f)) < .00001f,
            "source poster limits residual movement to ten units then eases it by one thirtieth");
    const auto hiddenPose = world.typingHud.posters.at(movingKey).view;
    moving.enabled = false;
    world.typing.syncTargets({moving});
    redraw();
    Require(world.typingHud.posters.at(movingKey).view == hiddenPose,
            "hidden word retains its exact poster position without advancing it");
    moving.enabled = true;
    world.typingCachedTargets.at(moving.serial).second.view[0] = 30;
    world.typing.syncTargets({moving});
    redraw();
    Require(std::abs(world.typingHud.posters.at(movingKey).view[0] - (20.f+10.f/30.f)) < .00001f,
            "returning word continues easing from its retained position");
    const float oldX = world.typingHud.posters.at(movingKey).view[0];
    world.typingCachedTargets.at(moving.serial).second.view[2] = -200;
    redraw();
    Require(std::abs(world.typingHud.posters.at(movingKey).view[0] -
            (oldX+(30-oldX)*.03333333507180214f)*(1+(-195.f+95.f)/-195.f)) < .00001f,
            "source poster applies its original depth-change correction");
    enemyPtr->body[2] = 20;
    redraw();
    Require(world.draws[0].edit->color[0] == 1 &&
            world.draws[0].edit->color[1] == (world.frameCounter2BAC & 4 ? 1.f : 0.f) &&
            std::all_of(world.draws.begin(), world.draws.end(), [](const auto& draw) { return draw.alpha == -1; }),
            "source near-distance warning flashes without fading the typed word");
    moving.flags = 0x04100000u;
    world.typingCachedTargets.at(moving.serial).first.flags = moving.flags;
    world.typing.syncTargets({moving});
    redraw();
    Require(std::abs(world.typingHud.posters.at(movingKey).view[0] - 30.f*(1+5.f/-195.f)) < .00001f &&
            std::all_of(world.draws.begin(), world.draws.end(), [](const auto& draw) { return draw.alpha == -1; }),
            "source direct-position and distance-warning bypass flags affect presentation only");
    Require(world.typing.assignedWordCount() == 1 && world.typing.stats().completedWords == 0 &&
            world.typing.stats().correctCharacters == 0,
            "poster motion and opacity do not create words or consume input");

    auto second = std::make_unique<game::EnemyTask>();
    auto* secondTask = world.ts.Create([](game::Task&) {}, std::move(second));
    game::TypingWordRecord another{40, 0, "Bug", {}};
    game::TypingTarget other = moving;
    other.serial = secondTask->serial; other.token = 701; other.forcedWord = &another;
    movingSpec.view[0] = 0; movingSpec.view[1] = 0; movingSpec.view[2] = -100;
    world.typingCachedTargets.at(moving.serial).second = movingSpec;
    world.typingCachedTargets.emplace(other.serial, std::make_pair(other, movingSpec));
    world.typing.syncTargets({moving, other});
    redraw();
    const auto firstSpread = world.typingHud.posters.at(movingKey).view;
    const auto secondSpread = world.typingHud.posters.at({other.serial, other.token}).view;
    Require(std::abs(world.focal*firstSpread[1]/firstSpread[2] + 19.2f) < .0001f &&
            std::abs(world.focal*secondSpread[1]/secondSpread[2] - 15.36f) < .0001f,
            "overlapping source posters spread in task order by twenty percent of the ninety-six-pixel gap");
    world.typingCachedTargets.clear();
    world.typing.syncTargets({});
    redraw();
    Require(world.typingHud.posters.empty(), "ended word identities release persistent poster state");

    // Source4B1A3C uses the torso point rather than the selected hit node.
    enemyPtr->anim.drawFlags = 1;
    enemyPtr->bodyView[0] = 12; enemyPtr->bodyView[1] = -20; enemyPtr->bodyView[2] = -100;
    movingSpec.view[0] = 12; movingSpec.view[1] = 50; movingSpec.view[2] = -100;
    moving.flags = 0;
    world.typingCachedTargets.emplace(moving.serial, std::make_pair(moving, movingSpec));
    world.typing.syncTargets({moving});
    redraw();
    const float depthCorrection = 1 + 5.f / -95.f;
    Require(std::abs(world.typingHud.posters.at(movingKey).view[1] - -20.f*depthCorrection) < .0001f,
            "ordinary source poster follows the torso below the head's independent hit point");
    world.typingHud.posters.clear();
    moving.flags = 0x800u;
    world.typingCachedTargets.at(moving.serial).first.flags = moving.flags;
    world.typing.syncTargets({moving});
    redraw();
    Require(std::abs(world.typingHud.posters.at(movingKey).view[1] -
                    (-20.f + 64.f/world.focal * -100.f)*depthCorrection) < .0001f,
            "source poster applies the authored flag800 screen offset before easing");
    enemyPtr->typingFrameColor = 1;
    moving.flags |= 0x04000000u;
    world.typingCachedTargets.at(moving.serial).first.flags = moving.flags;
    redraw();
    Require(world.draws[0].edit->color[0] == 0 && world.draws[0].edit->color[1] == 1,
            "source boss progress drives frame colour independently of selection");
    enemyPtr->typingFrameColor = 0;
    redraw();
    Require(world.draws[0].edit->color[0] == 1 &&
            world.draws[0].edit->color[1] == (world.frameCounter2BAC & 4 ? 1.f : 0.f),
            "expired source frame progress flashes red/yellow on the original frame bit");
    enemyPtr->typingFrameColor = .5f;
    world.typingCachedTargets.clear(); world.typing.syncTargets({});
    enemyPtr->anim.drawFlags = 0;

    world.typing.reset();
    const std::array<game::TypingWordRecord, 4> storyWords = {{{20,0,"One",{}}, {20,0,"Two",{}},
                                                           {20,0,"Three",{}}, {20,0,"Four",{}}}};
    for (int age = 1; age <= 254; ++age) {
        ++world.frame; ++world.frameCounter2BAC;
        game::TypingStrengthPostersHud(world, 777, 1, storyWords, 0, age == 254, -1, 4);
        world.draws.clear(); world.hudSprites.clear();
        game::TypingWordHud(world);
        if (age == 180) Require(!world.typingHud.story.lines[0].enabled, "Strength preview waits the source one-hundred-eighty ticks");
        if (age == 181) Require(world.typingHud.story.lines[0].phase == 1 &&
                               world.typingHud.story.lines[0].bounce == 1 &&
                               std::any_of(world.draws.begin(),world.draws.end(),[](const auto& draw) {
                                   return draw.fn == 0x0048B190 && draw.light.color[0] == .1f;
                               }), "Strength begins its original closed-word bounce before accepting input");
        if (age == 241) Require(world.typingHud.story.lines[0].phase == 2 && world.typingHud.story.lines[0].view[0] == 0,
                               "Strength finishes the source sixty-one-tick horizontal bounce at center");
    }
    Require(world.typingHud.story.lines[0].phase == 3 && world.typingHud.story.lines[1].phase == 1 &&
            world.typingHud.story.lines[1].bounce == 1,
            "Strength reveals the first line and starts the next poster's overlapping bounce at tick254");
    for (int age = 0; age < 200 && world.typingHud.story.lines[1].phase != 3; ++age) {
        ++world.frame; ++world.frameCounter2BAC;
        game::TypingStrengthPostersHud(world, 777, 1, storyWords, 1, false, -1, 4);
    }
    Require(world.typingHud.story.lines[1].phase == 3 && world.typingHud.story.lines[1].hold == 60,
            "later Strength preview waits for the preceding line and its source sixty-tick hold");
    ++world.frame; ++world.frameCounter2BAC;
    game::TypingStrengthPostersHud(world, 777, 1, storyWords, 1, true, 150, 4);
    world.draws.clear(); world.hudSprites.clear();
    game::TypingWordHud(world);
    Require(std::any_of(world.draws.begin(),world.draws.end(),[](const auto& draw) {
                return draw.fn == 0x0048B190 && draw.edit && draw.edit->diffuseRgb &&
                       draw.edit->color[0] == 1 && draw.edit->color[1] == 1 && draw.edit->color[2] == 0;
            }) && std::all_of(world.draws.begin(),world.draws.end(),[](const auto& draw) { return draw.alpha == -1; }) &&
            world.typing.assignedWordCount() == 0 && world.typing.stats().completedWords == 0,
            "Strength's deadline changes the frame warning without fading its glyphs or creating words");
    world.ResetTyping();
    Require(world.typingHud.posters.empty() && !world.typingHud.story.active,
            "ResetTyping clears persistent poster and independent story presentation state");
    world.draws.clear();
    world.typingReferenceActive = false;
    game::TypingWordHud(world);
    Require(world.draws.empty(), "source model HUD is inactive without the prepared reference");

    world.typingReferenceActive = true;
    world.residency = true;
    const char* gameDir = argc >= 3 ? argv[2] : std::getenv("HOTD2_GAME_DIR");
    Require(gameDir && *gameDir, "boss-card animation fixture needs typing_hud_test <prepared-bank> <HOD2-game-dir> (or HOTD2_GAME_DIR)");
    Require(world.exe.Load((std::filesystem::path(gameDir)/"Hod2.exe").string()), "load original native scalar tables for the boss-card animation fixture");
    world.modelLoaded.assign(0x8000,0);
    Require(world.typingReference.ApplyStage(0,world.mem,world.cameraBank,&error),error.c_str());
    world.runner = std::make_unique<game::EventRunner>(world.mem,world,&world.env,&world.exe);
    struct CardFixture { uint32_t record; int flag, camera, model, sprite; };
    const CardFixture cards[] = {
        {0x570EC8,2,0x40,0x18D1,0xCF}, {0x5966B8,9,0x75,0x18D5,0xD0},
        {0x5966F8,9,0x79,0x18D5,0xD0}, {0x5972F8,30,0xC5,0x1927,0xD2},
        {0x597338,30,0xCD,0x1927,0xD2}, {0x596AC0,22,0xE6,0x195E,0xD3},
        {0x5898C8,50,0xF4,0x19DA,0xD4}
    };
    game::SetCurrentWorld(&world);
    world.gameMode = 6;
    world.typing.reset();
    game::TypingTarget damageSelection;
    damageSelection.serial = 0xDEADBEEFu; damageSelection.token = 1; damageSelection.forcedWord = &word;
    world.typing.syncTargets({damageSelection});
    world.typing.typeCharacters("A");
    world.player[0].state = 5; world.player[0].lives = 3; world.player[0].hitTimer = 0;
    auto damageActor = std::make_unique<game::EnemyTask>();
    damageActor->typingFlags = 0x10;
    world.ts.Create([](game::Task&) {
        auto& state = *game::CurrentWorld();
        state.PlayerTakeHit(0, 0, 0);
        state.PlayerTakeHit(0, 0, 0); // The same-frame second hit is invulnerable.
    }, std::move(damageActor));
    world.ts.RunFrame();
    Require(world.player[0].lives == 2 && world.typing.runMetrics().smallThreatHits == 1 &&
            world.typing.runMetrics().otherTargetHits == 1,
            "only accepted P1 damage records source small-threat and other-selected-word penalties");
    world.ts.Reset(); world.typing.reset(); world.player[0].hitTimer = 0;
    for (const auto& card : cards) {
        world.ts.Reset(); world.resQueue.clear();
        std::fill(std::begin(world.runner->flags),std::end(world.runner->flags),0);
        world.runner->flags[card.flag] = 1;
        world.SpawnNameCard(card.record);
        for (int frame=0;frame<90;++frame) {
            world.draws.clear(); world.hudSprites.clear();
            world.StepResources(16);
            world.ts.RunFrame();
        }
        const int title = world.typingReference.SourceModel(card.model);
        Require(world.modelLoaded[title] != 0 && std::any_of(world.draws.begin(),world.draws.end(),[&](const auto& draw) {
                    return draw.model == title && draw.viewSpace;
                }), "every source boss card makes its exact title model resident and draws it");
        for (int sprite : {card.sprite,card.sprite+14}) {
            const auto* data = world.typingReference.SourceSprite(0x4000+sprite);
            Require(data && world.texFiles[data->file] == 4 &&
                    std::any_of(world.hudSprites.begin(),world.hudSprites.end(),[&](const auto& draw) {
                        return draw.id == 0x4000+sprite && draw.alpha > 0;
                    }), "source boss card title sprites use their prepared resident textures");
        }
        float position[3] = {}, targetPosition[3] = {};
        int roll = 0;
        Require(world.cameraBank.Evaluate(0x4000+card.camera,89,position,targetPosition,&roll,false),
                "source boss card camera curve exists");
        for (int axis=0;axis<3;++axis)
            Require(world.runner->camera.ctx[0].pos[axis] == position[axis],
                    "boss card follows its exact prepared camera path");
    }
    world.ts.Reset(); world.hudSprites.clear();
    world.SpawnBossBar(320,35);
    world.ts.RunFrame();
    Require(world.hudSprites.size() == 4 && std::all_of(world.hudSprites.begin(),world.hudSprites.end(),[](const auto& sprite) {
                return sprite.id >= 0x40C9 && sprite.id <= 0x40CC && sprite.y == 43 && sprite.sy == .8f;
            }) && world.hudSprites.back().sx == .72f,
            "source health bar uses its authored sprites, smaller scale and eight-pixel y offset");
    world.ts.Reset(); world.hudSprites.clear(); world.typingReferenceActive = false;
    world.SpawnBossBar(320,35);
    world.ts.RunFrame();
    Require(world.hudSprites.size() == 4 && std::all_of(world.hudSprites.begin(),world.hudSprites.end(),[](const auto& sprite) {
                return sprite.id >= 0xB5 && sprite.id <= 0xB8 && sprite.y == 35;
            }), "native gun health-bar sprites and position are unchanged");
    game::SetCurrentWorld(nullptr);

    world.cfg.portTyping = true;
    world.typingReferenceActive = true;
    world.typing.reset();
    world.typing.addBasePoints(1234);
    world.typing.finishChapter(game::TypingDifficulty::Normal,1);
    world.cfg.eventMode = 0;
    auto resultAt = [&](int phase,int age) {
        world.hudSprites.clear(); world.sounds.clear();
        game::DrawTypingChapterResults(world,phase,(phase==1 ? 247 : 86)-age,1);
    };
    auto hasResult = [&](float x,float y) {
        return std::any_of(world.hudSprites.begin(),world.hudSprites.end(),[&](const auto& sprite) {
            return sprite.source==0x00495010 && sprite.x==x && sprite.y==y;
        });
    };
    resultAt(1,0);
    Require(world.hud.menuPanels.empty() && world.hud.menuText.empty() && hasResult(96,28) && hasResult(270,412),
            "chapter results use the authored source atlas header and base counter, without a modern text panel");
    Require(!hasResult(291,364),"rescue counter waits for its original reveal delay");
    resultAt(1,34);
    Require(!hasResult(291,364),"rescue counter stays hidden for all thirty-five initial ticks");
    resultAt(1,35);
    Require(hasResult(291,364),"rescue counter appears on the source thirty-fifth tick");
    resultAt(1,70);
    Require(hasResult(280,316),"accuracy reveals after the original two thirty-five-tick categories");
    resultAt(2,54);
    Require(!hasResult(462,103),"chapter total waits for source decoration and hold timings");
    resultAt(2,55);
    Require(hasResult(462,103) && std::any_of(world.sounds.begin(),world.sounds.end(),[](const auto& sound) {
                return sound.id==(game::kTypingSoundFlag|0x000716A9u);
            }),"chapter total and its original cue appear together at the source fifty-fifth tick");
    const auto header = std::find_if(world.hudSprites.begin(),world.hudSprites.end(),[](const auto& sprite) {
        return sprite.source==0x00495010 && sprite.x==96 && sprite.y==28;
    });
    Require(header!=world.hudSprites.end() && header->atlas,"source result header retains its atlas subrectangle");
    const float light[3]={1,1,1};
    const auto atlas=game::MakeSpriteRecord(*header,0,light);
    Require(atlas.u0==header->uv[0] && atlas.v0==header->uv[1] && atlas.u1==header->uv[2] && atlas.v1==header->uv[3],
            "sprite submission preserves authored result-atlas UVs");
    game::SpriteDef ordinary;
    const auto ordinaryRecord=game::MakeSpriteRecord(ordinary,0,light);
    Require(ordinaryRecord.u0==0 && ordinaryRecord.v0==0 && ordinaryRecord.u1==1 && ordinaryRecord.v1==1,
            "ordinary gun-mode sprites retain their full-texture UVs");
    std::puts("typing_hud_test: OK");
}
