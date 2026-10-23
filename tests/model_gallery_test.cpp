// Native gallery regression checks against the installed game's real assets.
// A recording GPU validates uploaded geometry without requiring a window/device.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "game/anim.h"
#include "game/menu_audio.h"
#include "game/skin.h"
#include "game/sound_table.h"
#include "play/model_catalog.h"
#include "play/model_gallery.h"
#include "play/model_gallery_audio.h"
#include "render/asset_store.h"
#include "render/model_renderer.h"

namespace {
void Require(bool ok, const std::string& why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why.c_str());
        std::exit(1);
    }
}

struct RecordingGpu final : port::GpuDevice {
    size_t staticVertices = 0, dynamicVertices = 0, modelDraws = 0;
    uint64_t nextTexture = 1;
    bool captureBounds = false;
    std::vector<game::ModelVertex> staticGeometry, dynamicGeometry;
    float projectedLow[2]={INFINITY,INFINITY},projectedHigh[2]={-INFINITY,-INFINITY};
    const char* Name() const override { return "gallery geometry recorder"; }
    port::GpuTexture CreateTexture(const port::RgbaImage&, bool = false) override { return nextTexture++; }
    void ClearTextures() override {}
    void SetStaticVertices(const game::ModelVertex* vertices, size_t count) override {
        Check(vertices, count); staticVertices = count;
        staticGeometry.clear(); if(count) staticGeometry.assign(vertices,vertices+count);
    }
    void SetDynamicVertices(const game::ModelVertex* vertices, size_t count) override {
        Check(vertices, count); dynamicVertices = count;
        dynamicGeometry.clear(); if(count) dynamicGeometry.assign(vertices,vertices+count);
    }
    void SetSpriteVertices(const game::TLVertex*, size_t) override {}
    bool BeginFrame(const float[4]) override {
        modelDraws = 0;
        std::fill(std::begin(projectedLow),std::end(projectedLow),INFINITY);
        std::fill(std::begin(projectedHigh),std::end(projectedHigh),-INFINITY);
        return true;
    }
    void Submit(const port::GpuDrawList& list) override {
        for (const auto& constants : list.model) {
            for (float value : constants.world) Require(std::isfinite(value), "nonfinite gallery model transform");
            for (float value : constants.viewProj) Require(std::isfinite(value), "nonfinite gallery camera transform");
        }
        for (const auto& draw : list.draws)
            if (draw.pipeline == port::GpuDraw::Pipeline::Model) {
                ++modelDraws;
                Require(draw.count > 0, "gallery submitted empty geometry");
                const size_t vertices = draw.stream == port::GpuDraw::Stream::Dynamic ? dynamicVertices : staticVertices;
                Require(size_t(draw.first) + draw.count <= vertices, "gallery draw escaped its vertex stream");
                if(captureBounds) {
                    const auto& geometry=draw.stream==port::GpuDraw::Stream::Dynamic?dynamicGeometry:staticGeometry;
                    const auto& constants=list.model[draw.constants];
                    for(size_t index=draw.first;index<size_t(draw.first)+draw.count;++index) {
                        float point[4]={geometry[index].pos[0],geometry[index].pos[1],geometry[index].pos[2],1};
                        auto transform=[&](const float* matrix) {
                            float result[4]={};
                            for(int column=0;column<4;++column)
                                for(int row=0;row<4;++row) result[column]+=point[row]*matrix[row*4+column];
                            std::copy(std::begin(result),std::end(result),point);
                        };
                        if(constants.localCount[2]>.5f) { transform(constants.modelView); transform(constants.projection); }
                        else { transform(constants.world); transform(constants.viewProj); }
                        Require(point[3]>0,"centered gallery subject escaped behind the camera");
                        for(int axis=0;axis<2;++axis) {
                            const float projected=point[axis]/point[3];
                            projectedLow[axis]=std::min(projectedLow[axis],projected);
                            projectedHigh[axis]=std::max(projectedHigh[axis],projected);
                        }
                    }
                }
            }
    }
    void EndFrame() override {}
    bool ReadPixels(std::vector<uint8_t>*) override { return false; }
    int TargetWidth() const override { return 640; }
    int TargetHeight() const override { return 480; }
    static void Check(const game::ModelVertex* vertices, size_t count) {
        for (size_t i = 0; i < count; ++i)
            for (float p : vertices[i].pos) Require(std::isfinite(p), "gallery uploaded a nonfinite vertex");
    }
};

void RequireCentered(const RecordingGpu& gpu,const std::string& subject,bool fit=true) {
    const float centerX=320.f+(gpu.projectedLow[0]+gpu.projectedHigh[0])*160.f;
    const float centerY=240.f-(gpu.projectedLow[1]+gpu.projectedHigh[1])*120.f;
    Require(std::isfinite(centerX) && std::isfinite(centerY) &&
                std::abs(centerX-320.f)<.1f && std::abs(centerY-200.f)<.1f,
            subject+": projected animation bounds are not centered at the shared (320,200) anchor ("+
                std::to_string(centerX)+","+std::to_string(centerY)+")");
    if(fit) {
        const float top=240.f-gpu.projectedHigh[1]*240.f,bottom=240.f-gpu.projectedLow[1]*240.f;
        Require(top>=80.f && bottom<=326.f,subject+": default framing overlaps the viewer's title or controls");
    }
}

std::map<int, game::Motion> RawMotions(port::AssetStore& assets) {
    std::map<int, game::Motion> result;
    for (const auto& file : game::ListMotionFiles(assets.Exe())) {
        if (file.motionIds.empty()) continue;
        const auto& bytes = assets.File("mot/" + file.name);
        if (bytes.empty()) continue;
        for (auto& motion : game::SplitMotionFile(assets.Exe(), file, bytes, 0)) result.emplace(motion.id, std::move(motion));
    }
    return result;
}

void CheckMotion(const std::map<int, game::Motion>& raw, int id, const game::Skeleton& skeleton, const std::string& name) {
    auto found = raw.find(id);
    Require(found != raw.end(), name + ": motion not present in installed files: " + std::to_string(id));
    auto motion = found->second;
    motion.nodes = skeleton.nodeCount;
    Require(motion.ticks > 0 && motion.SizeConsistent(), name + ": motion incompatible with skeleton: " + std::to_string(id));
    for (int tick : {0, motion.ticks / 2, motion.ticks - 1}) {
        const auto pose = game::EvaluatePose(skeleton, motion, tick);
        Require(pose.size() == skeleton.nodes.size(), name + ": incomplete pose");
        for (const auto& matrix : pose)
            for (float value : matrix.m) Require(std::isfinite(value), name + ": nonfinite animated pose");
    }
}

void CheckModel(port::AssetStore& assets, uint32_t id, const std::string& name) {
    if (!id) return;
    game::Model model;
    std::string pol;
    Require(assets.ModelById(id, model, &pol), name + ": missing model " + std::to_string(id));
    Require(!model.meshes.empty(), name + ": empty model " + std::to_string(id));
    for (const auto& mesh : model.meshes)
        for (const auto& strip : mesh.strips)
            RecordingGpu::Check(strip.vertices.data(), strip.vertices.size());
}

void CheckSkeleton(port::AssetStore& assets, const game::Skeleton& skeleton, const std::string& name) {
    Require(skeleton.nodeCount > 0 && !skeleton.nodes.empty(), name + ": empty skeleton");
    for (const auto& node : skeleton.nodes) CheckModel(assets, node.modelId, name);
    const auto& exe = assets.Exe();
    const uint32_t attachments = exe.U32(0x0052ED08 + 4 * skeleton.type);
    if (!exe.InData(attachments)) return;
    const uint32_t count = exe.U32(attachments), defs = exe.U32(attachments + 4);
    if (!count) return;
    Require(count <= 32 && exe.InData(defs), name + ": invalid skin table");
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t definition = exe.U32(defs + 4 * i);
        if (!definition) continue;
        if (skeleton.type == 0x17 && i > 0) { // The chameleon's tail records contain a rigid segment chain, not SkinDef.
            const uint32_t segments = exe.U32(definition + 0x10), count = exe.U32(definition + 0x14);
            Require(count <= 16 && exe.InData(segments), name + ": invalid tail chain");
            for (uint32_t j = 0; j < count; ++j) CheckModel(assets, exe.U32(segments + j * 0x10 + 4), name);
            continue;
        }
        game::SkinDef skin;
        Require(game::LoadSkinDef(exe, definition, skin), name + ": unreadable skin definition");
        game::Model model;
        std::string pol;
        Require(assets.ModelById(skin.model, model, &pol), name + ": missing skin model");
        Require(game::SkinOffsetBase(skin, model) != UINT32_MAX, name + ": skin offsets do not match model geometry");
    }
}

void CheckCreatureNames(port::AssetStore& assets) {
    // Pin identities to the actual shipped geometry bank, not just a plausible
    // label or animation rig. Several internal filenames use different names.
    struct Identity { int kind; const char* name; const char* bank; };
    const Identity identities[] = {
        {0x00, "Bob", "harold.bin"}, {0x01, "Mickey", "znassb.bin"},
        {0x02, "Max", "znchain.bin"}, {0x03, "Eric", "zndina.bin"},
        {0x04, "Goldman - Undead", "zngold.bin"}, {0x05, "Ricky", "znnick.bin"},
        {0x06, "Patrick", "znnickb.bin"}, {0x07, "David", "znsam2.bin"},
        {0x08, "Steve", "znsam2b.bin"}, {0x09, "Franklin - Phase Form", "znjikken1.bin"},
        {0x0A, "Peter", "znjoe.bin"}, {0x0B, "Kageo", "znkage.bin"},
        {0x0C, "Kageo - Brown", "znkager.bin"}, {0x0D, "Ken", "znkagex.bin"},
        {0x0E, "Gregory", "znken.bin"}, {0x0F, "Ebitan - Green", "znebi2.bin"},
        {0x10, "Ebitan - Brown", "znebi3.bin"}, {0x11, "Ebitan - Black", "znebi4.bin"},
        {0x12, "Franklin", "znele.bin"}, {0x13, "Johnny", "znonoo.bin"},
        {0x14, "Cecil", "znonoopa.bin"}, {0x16, "Ryan", "zsass.bin"},
        {0x17, "Keith", "zskamere.bin"}, {0x18, "Lance", "zslman.bin"},
        {0x19, "Randy", "zstin.bin"}, {0x1A, "Cat", "cat.bin"},
        {0x1B, "Bouere", "frog.bin"}, {0x1C, "Bouere - Gold", "frog_gold.bin"},
        {0x1D, "Murrer", "mol.bin"}, {0x1E, "Bat", "zabat.bin"},
    };
    const auto& entries = play::ModelCatalog();
    const auto enemyCount = std::count_if(entries.begin(), entries.end(), [](const auto& entry) {
        return entry.category == play::ModelCategory::Enemies;
    });
    Require(enemyCount == std::size(identities) + 5, "creature identity audit must cover every enemy and assembled variant");
    for (const auto& identity : identities) {
        const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
            return entry.category == play::ModelCategory::Enemies && entry.skeletonType == identity.kind;
        });
        Require(found != entries.end() && found->name == identity.name,
                "wrong creature name for skeleton " + std::to_string(identity.kind));
        game::Skeleton skeleton;
        Require(game::LoadSkeleton(assets.Exe(), identity.kind, skeleton), "missing named creature skeleton");
        const auto body = std::find_if(skeleton.nodes.begin(), skeleton.nodes.end(), [](const auto& node) {
            return node.modelId != 0;
        });
        game::Model model;
        std::string bank;
        Require(body != skeleton.nodes.end() && assets.ModelById(body->modelId, model, &bank) && bank == identity.bank,
                found->name + ": name mapped to a different shipped model bank");
    }

    auto named = [&](const char* name) -> const play::ModelCatalogEntry& {
        const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
            return entry.name == name && entry.category == play::ModelCategory::Enemies;
        });
        Require(found != entries.end(), std::string("missing creature variant: ") + name);
        return *found;
    };
    auto bank = [&](uint32_t id, const char* expected) {
        game::Model model;
        std::string actual;
        Require(assets.ModelById(id, model, &actual) && actual == expected,
                "creature assembly uses a different original model bank: " + std::to_string(id));
        CheckModel(assets, id, std::string(expected) + " assembly");
    };
    const auto& alternate = named("Murrer - Alternate");
    const uint32_t alternateModels[] = {0x1373, 0x1375, 0x1377, 0x1379, 0x137B, 0x137D, 0x136F, 0x1381, 0x137F};
    Require(alternate.skeletonType == 0x1D && alternate.nodeOverrides.size() == std::size(alternateModels),
            "alternate Murrer must use its complete second appearance");
    for (int slot = 1; slot <= static_cast<int>(std::size(alternateModels)); ++slot) {
        const auto replacement = std::find_if(alternate.nodeOverrides.begin(), alternate.nodeOverrides.end(),
                                              [=](const auto& node) { return node.node == slot; });
        Require(replacement != alternate.nodeOverrides.end() && replacement->modelId == alternateModels[slot - 1],
                "alternate Murrer is missing an original segment");
        bank(alternateModels[slot - 1], "mol.bin");
    }
    const auto& crawler = named("Kageo J - Crawler");
    Require(crawler.skeletonType == 0x0C && crawler.nodeOverrides.size() == 7,
            "crawler must use the severed Kageo appearance");
    for (int slot = 9; slot < 16; ++slot) {
        const auto replacement = std::find_if(crawler.nodeOverrides.begin(), crawler.nodeOverrides.end(),
                                              [=](const auto& node) { return node.node == slot; });
        Require(replacement != crawler.nodeOverrides.end() && replacement->modelId == (slot == 9 ? 0x1DA3u : 0u),
                "crawler retains a lower-body mesh or lacks its severed waist");
    }
    Require(std::any_of(crawler.animations.begin(), crawler.animations.end(), [](const auto& clip) {
        return clip.name == "Crawl" && clip.motionId == 0x41B;
    }), "crawler is missing its original crawling animation");
    const auto& bat = named("Bat");
    Require(bat.companionSkeletonType == 0x1F && bat.animations.size() == 1 &&
                bat.animations[0].motionId == 0x407 && bat.animations[0].companionMotionId == 0x406,
            "bat body and wings must retain their synchronized original rigs");
    bank(0x1B03, "zabat_wing.bin");
    Require(named("Owl").assembly == play::ModelAssembly::Owl,
            "owl requires its original mesh-frame assembly");
    Require(named("Moth").assembly == play::ModelAssembly::AmbientInsect,
            "moth requires its original two-frame assembly");
    Require(named("Fish").assembly == play::ModelAssembly::Fish,
            "fish requires its original swimming and shot mesh-frame assembly");
    // BatDraw assembles these independent original sequences and limbs; a
    // valid first body frame alone does not prove the whole owl can render.
    for (uint32_t id : {0xBBFu, 0xBC0u, 0xBF1u, 0xBF4u, 0xBF2u,
                        0xBF5u, 0xBF6u, 0xBBDu, 0xBBEu}) bank(id, "owl.bin");
    for (uint32_t id = 0xBC1; id < 0xBC1 + 30; ++id) bank(id, "owl.bin");
    for (uint32_t id = 0xBF8; id < 0xBF8 + 15; ++id) bank(id, "owl.bin");
    for (uint32_t start : {0xC08u, 0xC18u})
        for (uint32_t id = start; id < start + 15; ++id) bank(id, "owl.bin");
    bank(0x1CF, "komono_souko.bin"); bank(0x1D0, "komono_souko.bin");
    for (uint32_t id = 0x1156; id <= 0x1169; ++id) bank(id, "fish.bin");
    bank(0xB6F, "fish.bin"); bank(0xB70, "fish.bin");
}

void CheckNamedCharacters(port::AssetStore& assets) {
    // Identity evidence is separate from motion compatibility: Amy and Harry
    // share a rig, so every animation still passed when their names were swapped.
    // Original NPC spawn records identify the player banks; the distinctive
    // heads below were checked against the manual and native model captures.
    // Boss anatomy was checked against Sega's HOD2&3 Return boss guide and the
    // stage-specific spawn code. Kuarl and Zeal share a bank, not a body mesh.
    using Category = play::ModelCategory;
    struct Identity {
        const char* name;
        Category category;
        int kind, node;
        uint32_t model;
        const char* bank;
    };
    const Identity identities[] = {
        {"James Taylor", Category::Agents, 0x39, 2, 0x14BA, "player1.bin"},
        {"Gary Stewart", Category::Agents, 0x3A, 2, 0x14CA, "player2.bin"},
        {"Amy Crystal", Category::Agents, 0x3B, 2, 0x15AD, "player3.bin"},
        {"Harry Harris", Category::Agents, 0x3C, 2, 0x15BF, "player4.bin"},
        {"G", Category::Agents, 0x3E, 2, 0xC39, "hou.bin"},
        {"Thomas Rogan", Category::Agents, 0x3F, 2, 0x1265, "logan.bin"},
        {"Goldman", Category::Bosses, 0x3D, 2, 0xD27, "player_gold.bin"},
        {"Judgment - Kuarl", Category::Bosses, 0x44, 1, 0x298, "char_adv04.bin"},
        {"Judgment - Zeal", Category::Bosses, 0x45, 1, 0x2BB, "char_adv04.bin"},
        {"Hierophant", Category::Bosses, 0x47, 1, 0x31F, "boss2.bin"},
        {"Tower - Long Neck", Category::Bosses, 0x48, 24, 0x384, "boss3l.bin"},
        {"Tower - Short Neck", Category::Bosses, 0x49, 17, 0x391, "boss3.bin"},
        {"Strength", Category::Bosses, 0x4A, 2, 0x3CE, "boss4.bin"},
        {"Magician", Category::Bosses, 0x4B, 2, 0x53F, "boss5.bin"},
        {"Emperor", Category::Bosses, 0x4C, 2, 0x7BB, "boss6.bin"},
    };
    const auto& entries = play::ModelCatalog();
    const auto namedCount = std::count_if(entries.begin(), entries.end(), [](const auto& entry) {
        return entry.category == Category::Agents || entry.category == Category::Bosses;
    });
    Require(namedCount == std::size(identities), "identity audit must cover every agent and boss");
    for (const auto& identity : identities) {
        const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
            return entry.name == identity.name;
        });
        Require(found != entries.end() && found->category == identity.category && found->skeletonType == identity.kind,
                std::string(identity.name) + ": assigned the wrong character skeleton or category");
        game::Skeleton skeleton;
        Require(game::LoadSkeleton(assets.Exe(), found->skeletonType, skeleton), "missing named character skeleton");
        const auto node = std::find_if(skeleton.nodes.begin(), skeleton.nodes.end(), [&](const auto& candidate) {
            return candidate.index == identity.node;
        });
        game::Model model;
        std::string bank;
        Require(node != skeleton.nodes.end() && node->modelId == identity.model &&
                    assets.ModelById(node->modelId, model, &bank) && bank == identity.bank,
                std::string(identity.name) + ": distinctive geometry does not match the shipped character");
    }

    // Independent anchors in the original scene data: type-0x25 records use
    // byte +0x24 for the character kind. The adjacent firing scripts are Amy's
    // 0xA8DC and Harry's 0xA9A8, not the reversed former Horde-mode comments.
    const auto& stage2 = assets.File("evt/st2evtbl.bin");
    Require(stage2.size() > 0xA998 && stage2[0xA8A8 + 0x24] == 0x3B && stage2[0xA974 + 0x24] == 0x3C,
            "Amy/Harry identity anchors disagree with original scene records");
    for (const auto& entry : entries) {
        if (entry.category != Category::Agents || entry.name == "G") continue;
        const auto amy = std::find_if(entry.animations.begin(), entry.animations.end(), [](const auto& animation) {
            return animation.name == "Fire - Amy animation";
        });
        const auto harry = std::find_if(entry.animations.begin(), entry.animations.end(), [](const auto& animation) {
            return animation.name == "Fire - Harry animation";
        });
        Require(amy != entry.animations.end() && amy->motionId == 0x32D &&
                    harry != entry.animations.end() && harry->motionId == 0x355,
                entry.name + ": shared firing clips have swapped character labels");
    }
}

void CheckCivilianAppearances(port::AssetStore& assets) {
    // These previously misleading labels must follow the rendered, assembled
    // costume rather than the filename (hito_fem includes a male head/costume).
    // The four corrected outfits were checked in native gallery captures;
    // kind 0x38 additionally comes from the stage-6 no-parts spawn records.
    struct Appearance {
        int kind;
        const char* name;
        int head, part1, part2;
        const char* bank;
    };
    const Appearance appearances[] = {
        {0x24, "Man - Red Cap", 13, 41, 64, "hito_buru.bin"},
        {0x25, "Man - Tan Jacket", 9, 61, -1, "hito_fem.bin"},
        {0x2A, "Woman - Pink Jacket", 3, 45, -1, "hito_galjk.bin"},
        {0x2B, "Woman - Orange Jacket", 3, 45, -1, "hito_galjkaa.bin"},
        {0x38, "Woman - Black Dress", -1, -1, -1, "deka_musume.bin"},
    };
    const auto& entries = play::ModelCatalog();
    for (const auto& appearance : appearances) {
        const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
            return entry.category == play::ModelCategory::Civilians && entry.skeletonType == appearance.kind;
        });
        Require(found != entries.end() && found->name == appearance.name &&
                    found->headPart == appearance.head && found->part1 == appearance.part1 && found->part2 == appearance.part2,
                std::string(appearance.name) + ": label no longer matches its verified costume");
        game::Skeleton skeleton;
        Require(game::LoadSkeleton(assets.Exe(), found->skeletonType, skeleton), "missing civilian appearance skeleton");
        const auto torso = std::find_if(skeleton.nodes.begin(), skeleton.nodes.end(), [](const auto& node) {
            return node.index == 1;
        });
        game::Model model;
        std::string bank;
        Require(torso != skeleton.nodes.end() && assets.ModelById(torso->modelId, model, &bank) && bank == appearance.bank,
                std::string(appearance.name) + ": wrong original civilian model bank");
    }
}

void CheckCatalog(port::AssetStore& assets) {
    CheckCreatureNames(assets);
    CheckNamedCharacters(assets);
    CheckCivilianAppearances(assets);
    const auto& entries = play::ModelCatalog();
    const auto raw = RawMotions(assets);
    game::SoundTable sounds;
    Require(sounds.Load(assets.Exe()), "load sound table");
    auto checkSound = [&](uint32_t id, const std::string& name, bool variant) {
        const auto request = game::ResolveSound(sounds, id);
        if(request.stopLoopedSe) {
            Require(!variant && std::find(sounds.LoopOffIds().begin(),sounds.LoopOffIds().end(),id)!=sounds.LoopOffIds().end(),
                    name+": invalid loop-stop cue or randomized stop command");
            return;
        }
        Require(request.action == game::SoundAction::PlayFile, name + ": sound cue is not a playable recording");
        Require(!variant || !request.loop, name + ": random sample pool contains a sustained effect");
        std::string path = request.path;
        std::replace(path.begin(), path.end(), '\\', '/');
        Require(!assets.File(path).empty(), name + ": missing sound recording " + path);
    };
    auto checkCue = [&](const play::ModelSoundCue& cue, const std::string& name) {
        checkSound(cue.soundId,name,false);
        if(cue.variants.empty()) return;
        const std::set<uint32_t> variants(cue.variants.begin(),cue.variants.end());
        Require(variants.size()>1 && variants.size()==cue.variants.size(),name+": sample pool is trivial or repeats a member");
        Require(variants.count(cue.soundId)==1,name+": sample pool omits its primary sound");
        for(uint32_t id:cue.variants) checkSound(id,name,true);
    };
    std::set<play::ModelCategory> categories;
    std::set<std::string> names;
    bool goldman = false;
    size_t animationCount = 0, cueCount = 0;
    for (const auto& entry : entries) {
        Require(!entry.name.empty() && names.insert(entry.name).second, "empty or duplicate character name");
        categories.insert(entry.category);
        if (entry.name == "Goldman") {
            Require(entry.category == play::ModelCategory::Bosses, "Goldman must appear under Bosses");
            goldman = true;
        }
        if (entry.assembly != play::ModelAssembly::Skeleton) {
            Require(entry.skeletonType < 0 && entry.companionSkeletonType < 0,
                    entry.name + ": mesh-frame creature must not depend on an unrelated skeleton");
            Require(!entry.animations.empty(), entry.name + ": no mesh-frame animations");
            for (const auto& animation : entry.animations) {
                ++animationCount;
                Require(!animation.name.empty() && animation.motionId < 0 && animation.tickLength > 0,
                        entry.name + ": mesh-frame animation needs its own name and positive cycle length");
                for (const auto& cue : animation.sounds) {
                    ++cueCount;
                    Require(cue.tick >= 0 && cue.tick < animation.tickLength,
                            entry.name + ": sound cue outside mesh-frame animation");
                    checkCue(cue,entry.name);
                }
            }
            continue;
        }
        game::Skeleton skeleton, companion;
        Require(game::LoadSkeleton(assets.Exe(), entry.skeletonType, skeleton), entry.name + ": missing skeleton");
        CheckSkeleton(assets, skeleton, entry.name);
        auto hasNode = [&](int slot) {
            return std::any_of(skeleton.nodes.begin(), skeleton.nodes.end(), [=](const auto& n) { return n.index == slot; });
        };
        for (int part : {entry.headPart, entry.part1, entry.part2}) {
            if (part < 0) continue;
            const auto def = assets.Exe().U32(0x004EC748 + part * 4);
            Require(assets.Exe().InData(def), entry.name + ": invalid costume part");
            Require(hasNode(assets.Exe().U32(def) & 15), entry.name + ": costume attachment has no bone");
            CheckModel(assets, assets.Exe().U32(def + 4), entry.name);
        }
        for (const auto& replacement : entry.nodeOverrides) {
            Require(hasNode(replacement.node), entry.name + ": replacement has no bone");
            Require(replacement.frameCount > 0, entry.name + ": invalid animated mesh frame count");
            for (int frame = 0; frame < replacement.frameCount; ++frame)
                CheckModel(assets, replacement.modelId ? replacement.modelId + frame : 0, entry.name);
        }
        for (const auto& extra : entry.extraMeshes) {
            Require(hasNode(extra.node), entry.name + ": additional mesh has no bone");
            Require(extra.frameCount > 0, entry.name + ": invalid additional mesh frame count");
            for (int frame = 0; frame < extra.frameCount; ++frame) CheckModel(assets, extra.modelId + frame, entry.name);
        }
        if (entry.companionSkeletonType >= 0) {
            Require(hasNode(entry.companionNode), entry.name + ": companion attachment has no bone");
            Require(game::LoadSkeleton(assets.Exe(), entry.companionSkeletonType, companion), entry.name + ": missing companion skeleton");
            CheckSkeleton(assets, companion, entry.name);
        }
        Require(!entry.animations.empty(), entry.name + ": no animations");
        for (const auto& animation : entry.animations) {
            ++animationCount;
            Require(!animation.name.empty(), entry.name + ": unnamed animation");
            CheckMotion(raw, animation.motionId, skeleton, entry.name + " / " + animation.name);
            if (animation.companionMotionId >= 0) {
                Require(entry.companionSkeletonType >= 0, entry.name + ": companion motion has no skeleton");
                CheckMotion(raw, animation.companionMotionId, companion, entry.name + " companion");
            }
            for (const auto& cue : animation.sounds) {
                ++cueCount;
                Require(cue.tick >= 0 && cue.tick < raw.at(animation.motionId).ticks, entry.name + ": sound cue outside animation");
                checkCue(cue,entry.name);
            }
        }
    }
    Require(categories.size() == 4 && goldman, "all four categories and Goldman are required");
    Require(cueCount > 0, "gallery has no animation sounds");
    std::printf("Validated %zu characters, %zu animations and %zu authored sound cues against installed assets\n",
                entries.size(), animationCount, cueCount);
}

bool HasText(const play::ModelGallery& gallery, const std::string& text) {
    const auto labels = gallery.Text();
    return std::any_of(labels.begin(), labels.end(), [&](const auto& label) { return label.text == text; });
}

void CheckGalleryAssets(port::AssetStore& assets, play::ModelGallery& gallery, port::ModelRenderer& renderer,
                        RecordingGpu& gpu) {
    port::UiFont font; // Model draws do not require the optional system font atlas.
    const float black[4] = {0, 0, 0, 1};
    size_t poses = 0;
    const auto& catalog = play::ModelCatalog();
    gpu.captureBounds=true;
    gallery.Open();
    Require(gallery.IsOpen() && !gallery.IsViewing(), "opening gallery must display its browser");
    for (int index = 0; index < static_cast<int>(catalog.size()); ++index) {
        const auto& entry = catalog[index];
        Require(gallery.SelectModel(index), entry.name + ": cannot select model");
        Require(gallery.IsViewing(), entry.name + ": model selection did not enter viewing mode");
        Require(gallery.Prepare(assets, renderer), entry.name + ": " + gallery.Error());
        Require(gallery.LoadedPartCount() > 0, entry.name + ": no model parts loaded");
        if (entry.assembly == play::ModelAssembly::Owl)
            Require(gallery.LoadedPartCount() == 12, entry.name + ": missing body, face, wing or leg part");
        if (entry.assembly == play::ModelAssembly::AmbientInsect)
            Require(gallery.LoadedPartCount() == 1, entry.name + ": moth must load its complete body sequence");
        if (entry.assembly == play::ModelAssembly::Fish)
            Require(gallery.LoadedPartCount() == 3, entry.name + ": swimming and both shot forms must all load");
        for (play::ModelCategory category : {play::ModelCategory::Agents, play::ModelCategory::Civilians,
                                            play::ModelCategory::Enemies, play::ModelCategory::Bosses})
            Require(!HasText(gallery, play::ModelCategoryName(category)), "category list must disappear while viewing a model");
        for (int animation = 0; animation < static_cast<int>(entry.animations.size()); ++animation) {
            Require(gallery.SelectAnimation(animation), entry.name + ": cannot select animation");
            Require(gallery.Prepare(assets, renderer), entry.name + ": " + gallery.Error());
            Require(gallery.AnimationLength() > 0, entry.name + ": selected animation has no frames");
            for (int sample = 0; sample < 2; ++sample) {
                if (sample) {
                    for (int tick = 0; tick < gallery.AnimationLength() / 2; ++tick) gallery.Update({});
                    Require(gallery.Prepare(assets, renderer), entry.name + ": " + gallery.Error());
                }
                gpu.BeginFrame(black);
                gallery.Draw(renderer, font, 640, 480);
                Require(gpu.modelDraws > 0, entry.name + " / " + entry.animations[animation].name + ": model is not visible");
                RequireCentered(gpu,entry.name+" / "+entry.animations[animation].name);
                ++poses;
            }
        }
    }
    gpu.captureBounds=false;
    std::printf("Prepared and submitted %zu complete animated character poses\n", poses);
}

void KeyEvent(play::ModelGallery& gallery, char ch, bool down, platform::Key key = platform::Key::Unknown) {
    platform::Event event;
    event.type = down ? platform::Event::Type::KeyDown : platform::Event::Type::KeyUp;
    event.ch = ch; event.key = key;
    gallery.HandleEvent(event, 0, 0);
}
void Input(play::ModelGallery& gallery, uint32_t buttons) {
    play::MenuInput input;
    input.pressed = buttons;
    gallery.Update(input);
}
void Click(play::ModelGallery& gallery, float x, float y) {
    play::MenuInput input;
    input.pointer = input.trigger = true;
    input.px = x; input.py = y;
    gallery.Update(input);
}
void Mouse(play::ModelGallery& gallery, platform::Event::Type type, float x, float y, int button = 0, float wheel = 0) {
    platform::Event event;
    event.type = type; event.button = button; event.wheel = wheel;
    gallery.HandleEvent(event, x, y);
}

void CheckCentering(port::AssetStore& assets,port::ModelRenderer& renderer,RecordingGpu& gpu) {
    play::ModelGallery gallery;
    port::UiFont font;
    const float black[4]={0,0,0,1};
    const auto& catalog=play::ModelCatalog();
    gpu.captureBounds=true;
    for(const char* name:{"James Taylor","Murrer - Alternate","Kageo J - Crawler","Bat","Owl","Fish",
                         "Judgment - Zeal","Tower - Long Neck","Emperor"}) {
        const auto entry=std::find_if(catalog.begin(),catalog.end(),[&](const auto& model){return model.name==name;});
        Require(entry!=catalog.end(),std::string("missing framing subject: ")+name);
        Require(gallery.SelectModel(static_cast<int>(entry-catalog.begin())),"select framing subject");
        for(int animation:{0,static_cast<int>(entry->animations.size()/2),static_cast<int>(entry->animations.size()-1)}) {
            Require(gallery.SelectAnimation(animation) && gallery.Prepare(assets,renderer),"prepare framing subject");
            Mouse(gallery,platform::Event::Type::MouseDown,320,190,1);
            Mouse(gallery,platform::Event::Type::MouseMove,515,245);
            Mouse(gallery,platform::Event::Type::MouseUp,515,245,1);
            for(int target:{0,gallery.AnimationLength()/2,gallery.AnimationLength()-1}) {
                while(gallery.AnimationTick()<target) gallery.Update({});
                Require(gallery.Prepare(assets,renderer),"pose framing subject");
                gpu.BeginFrame(black); gallery.Draw(renderer,font,640,480);
                RequireCentered(gpu,std::string(name)+" after camera orbit and animation root travel");
            }
        }
    }
    Require(gallery.SelectModel(0) && gallery.Prepare(assets,renderer),"prepare clip position reset subject");
    KeyEvent(gallery,'w',true); KeyEvent(gallery,'d',true);
    for(int tick=0;tick<8;++tick) gallery.Update({});
    KeyEvent(gallery,'w',false); KeyEvent(gallery,'d',false);
    Mouse(gallery,platform::Event::Type::MouseWheel,320,190,0,1);
    const auto camera=gallery.Camera();
    Require(camera.height>0,"framing test needs an intentional vertical pan");
    Require(gallery.Prepare(assets,renderer),"prepare manually positioned subject");
    gpu.BeginFrame(black); gallery.Draw(renderer,font,640,480);
    const float movedCenterY=240.f-(gpu.projectedLow[1]+gpu.projectedHigh[1])*120.f;
    Require(movedCenterY>201.f,"automatic centering must preserve an intentional manual height adjustment");
    Require(gallery.SelectAnimation(1) && gallery.Prepare(assets,renderer),"switch differently framed animation");
    Require(gallery.Camera().height==0 && gallery.Camera().yaw==camera.yaw &&
                gallery.Camera().pitch==camera.pitch && gallery.Camera().zoom==camera.zoom,
            "clip changes must reset the position while retaining the chosen rotation and zoom");
    gpu.BeginFrame(black); gallery.Draw(renderer,font,640,480);
    RequireCentered(gpu,"new clip after manual positioning",false);
    gpu.captureBounds=false;
    std::puts("Validated shared screen centering through root travel, compound rigs, mesh frames, camera orbit and clip changes");
}

void CheckInteractions(port::AssetStore& assets, play::ModelGallery& gallery, port::ModelRenderer& renderer) {
    gallery.TakeFeedbackSounds();
    Require(gallery.SelectModel(0) && gallery.Prepare(assets, renderer), "prepare camera subject");
    Require(gallery.TakeFeedbackSounds().empty(), "programmatic model selection must not emit menu feedback");
    gallery.Update({}); // initial tick emits only its authored start cues
    gallery.TakeStopSounds(); gallery.TakeSounds();
    const auto initialCamera = gallery.Camera();
    const int initialTick = gallery.AnimationTick();
    KeyEvent(gallery, 'd', true);
    for (int i = 0; i < 6; ++i) gallery.Update({});
    KeyEvent(gallery, 'd', false);
    Require(gallery.Camera().yaw > initialCamera.yaw && gallery.Playing(), "D rotates while animation keeps playing");
    Require(gallery.AnimationTick() == (initialTick + 6) % gallery.AnimationLength(), "camera navigation interrupted animation time");
    Require(!gallery.TakeStopSounds(), "camera navigation stopped sound playback");
    KeyEvent(gallery, 'w', true);
    gallery.Update({});
    KeyEvent(gallery, 'w', false);
    Require(gallery.Camera().height > initialCamera.height, "W raises camera");
    const auto beforeArrow = gallery.Camera();
    Input(gallery, 0x40 | 0x20);
    Require(gallery.Camera().yaw < beforeArrow.yaw && gallery.Camera().height < beforeArrow.height, "arrow controls rotate and lower camera");
    const auto beforeDrag = gallery.Camera();
    Mouse(gallery, platform::Event::Type::MouseDown, 320, 200, 1);
    Mouse(gallery, platform::Event::Type::MouseMove, 355, 225);
    Mouse(gallery, platform::Event::Type::MouseUp, 355, 225, 1);
    Require(gallery.Camera().yaw != beforeDrag.yaw && gallery.Camera().pitch != beforeDrag.pitch, "mouse drag rotates and tilts model");
    Mouse(gallery, platform::Event::Type::MouseDown, 320, 200, 2);
    Input(gallery, 8); // Both mouse gun backends map their middle button to Start.
    Require(gallery.Playing(), "starting a middle-button camera drag toggled animation playback");
    Mouse(gallery, platform::Event::Type::MouseMove, 320, 220);
    Mouse(gallery, platform::Event::Type::MouseUp, 320, 220, 2);
    Require(gallery.Camera().height != beforeArrow.height, "middle drag changes camera height");
    const float beforeZoom = gallery.Camera().zoom;
    Mouse(gallery, platform::Event::Type::MouseWheel, 320, 200, 0, 1);
    Require(gallery.Camera().zoom < beforeZoom, "wheel zooms in");
    Mouse(gallery, platform::Event::Type::MouseWheel, 320, 200, 0, -2);
    Require(gallery.Camera().zoom > beforeZoom, "wheel zooms out");
    for (float wheel : {10000.f, -10000.f}) {
        Mouse(gallery, platform::Event::Type::MouseWheel, 320, 200, 0, wheel);
        Require(std::isfinite(gallery.Camera().zoom) && gallery.Camera().zoom > 0, "zoom remains finite and positive at input extremes");
    }
    KeyEvent(gallery, 'r', true); KeyEvent(gallery, 'r', false);
    Require(gallery.Camera().yaw == initialCamera.yaw && gallery.Camera().height == initialCamera.height &&
                gallery.Camera().zoom == initialCamera.zoom, "R restores camera framing");
    KeyEvent(gallery, 'd', true);
    platform::Event focus;
    focus.type = platform::Event::Type::Focus; focus.active = false;
    gallery.HandleEvent(focus, 0, 0);
    gallery.Update({});
    Require(gallery.Camera().yaw == initialCamera.yaw, "focus loss must release held camera controls");
    Require(!gallery.TakeStopSounds(), "camera dragging, scrolling or resetting must not stop animation sounds");
    const auto labels = gallery.Text();
    Require(std::any_of(labels.begin(), labels.end(), [](const auto& label) {
        return label.text == "CAMERA" && label.x >= 320 && label.y >= 320;
    }), "camera help belongs in the bottom-right corner");

    // A physical Space is delivered as both an event and a menu edge in play.exe.
    KeyEvent(gallery, ' ', true, platform::Key::Space); Input(gallery, 4);
    KeyEvent(gallery, ' ', false, platform::Key::Space);
    Require(!gallery.Playing(), "Space pauses exactly once");
    const int pausedTick = gallery.AnimationTick();
    for (int i = 0; i < 8; ++i) gallery.Update({});
    Require(gallery.AnimationTick() == pausedTick, "paused animation advanced");
    Click(gallery, 60, 416);
    Require(gallery.Playing(), "Play button resumes animation");
    Click(gallery, 150, 416);
    Require(!gallery.Playing(), "Pause button pauses animation");
    Click(gallery, 230, 416);
    Require(gallery.AnimationTick() == 0, "Reset button returns animation to its start");
    Input(gallery, 8);
    Require(gallery.Playing(), "Enter resumes animation");
    Require(!gallery.SelectModel(-1) && !gallery.SelectModel(static_cast<int>(play::ModelCatalog().size())), "invalid character selection accepted");
    Require(!gallery.SelectAnimation(-1) && !gallery.SelectAnimation(100000), "invalid animation selection accepted");

    const int selected = gallery.SelectedModel();
    Input(gallery, 2);
    Require(gallery.IsOpen() && !gallery.IsViewing(), "Back from model must restore browser");
    Require(gallery.TakeFeedbackSounds() == std::vector<uint32_t>{game::kOptionBackSound},
            "Back from a model must emit only sound 76");
    Require(gallery.TakeStopSounds(), "leaving a model must stop its sounds");
    for (play::ModelCategory category : {play::ModelCategory::Agents, play::ModelCategory::Civilians,
                                        play::ModelCategory::Enemies, play::ModelCategory::Bosses})
        Require(HasText(gallery, play::ModelCategoryName(category)), "browser must restore all four category labels");
    Input(gallery, 8);
    Require(gallery.IsViewing() && gallery.SelectedModel() == selected, "browser did not preserve model selection");
    Require(gallery.TakeFeedbackSounds() == std::vector<uint32_t>{game::kOptionSelectSound},
            "opening a model from the browser must emit only sound 74");
    Input(gallery, 2); Input(gallery, 2);
    Require(!gallery.IsOpen(), "second Back must return to Extras");
    Require(gallery.TakeFeedbackSounds() == std::vector<uint32_t>{game::kOptionBackSound, game::kOptionBackSound},
            "both gallery Back levels must emit sound 76");
    gallery.Open();
    Click(gallery, 90, 275); // Bosses category, first boss in its list.
    Require(!gallery.IsViewing(), "category click should only choose a category");
    Click(gallery, 300, 130);
    Require(gallery.IsViewing() && play::ModelCatalog()[gallery.SelectedModel()].category == play::ModelCategory::Bosses,
            "click/shoot did not open the selected boss");
    std::puts("Validated gallery camera, mouse/keyboard input, modal navigation and playback controls");
}

void CheckScrollHints() {
    play::ModelGallery gallery;
    auto inactive=[&]{const auto hint=gallery.ScrollHint();return !hint.above&&!hint.below;};
    Require(inactive(),"closed model gallery has no scroll cues");
    gallery.Open();
    Require(inactive(),"short Agents list has no scroll cues");
    Input(gallery,0x80);Input(gallery,0x80); // Enemies has more than one screen of models.
    auto hint=gallery.ScrollHint();
    Require(hint.left==216&&hint.top==116&&hint.right==616&&hint.bottom==411,
            "model scroll cue stays inside the model list and away from category controls");
    Require(!hint.above&&hint.below,"first model screen indicates only models below");
    play::MenuInput page;page.pageDelta=1;
    gallery.Update(page);
    hint=gallery.ScrollHint();
    Require(hint.above&&hint.below,"middle model screen indicates models above and below");
    page.pageDelta=100;gallery.Update(page);
    hint=gallery.ScrollHint();
    Require(hint.above&&!hint.below,"last model screen has no false downward cue");
    Input(gallery,8);
    Require(gallery.IsViewing()&&inactive(),"viewing an individual model hides the browser's scroll cues");
    Input(gallery,2);
    Require(gallery.IsOpen()&&!gallery.IsViewing()&&gallery.ScrollHint().above&&!gallery.ScrollHint().below,
            "Back from a model restores the browser's exact scroll position cues");
    page.pageDelta=-100;gallery.Update(page);
    Require(!gallery.ScrollHint().above&&gallery.ScrollHint().below,"returning to the first model clears the upper cue");
    gallery.Close();
    Require(inactive(),"closing a scrolled model browser hides both cues");
}

std::string LabelAt(const play::ModelGallery& gallery, float x, float y) {
    const auto text = gallery.Text();
    const auto found = std::find_if(text.begin(), text.end(), [=](const auto& label) {
        return label.x == x && label.y == y;
    });
    return found == text.end() ? std::string{} : found->text;
}

void CheckDraggableLists() {
    play::ModelGallery gallery;
    const auto& catalog = play::ModelCatalog();
    std::vector<int> enemies;
    for (int index = 0; index < static_cast<int>(catalog.size()); ++index)
        if (catalog[index].category == play::ModelCategory::Enemies) enemies.push_back(index);
    Require(enemies.size() > 18, "scroll regression needs multiple pages of enemies");
    auto firstModel = [&] { return LabelAt(gallery, 228, 121); };
    gallery.Open();
    Input(gallery, 0x80); Input(gallery, 0x80);
    Require(firstModel() == catalog[enemies.front()].name, "enemy browser starts at the first model");

    // The physical button also reaches Update as a gun trigger. A scrollbar
    // press must consume it, and dragging outside the track still clamps.
    Mouse(gallery, platform::Event::Type::MouseDown, 609, 125, 1);
    Click(gallery, 609, 125);
    Mouse(gallery, platform::Event::Type::MouseMove, 900, 1000);
    gallery.Update({});
    Require(!gallery.IsViewing() && gallery.SelectedModel() == -1,
            "dragging the model scrollbar opened a model");
    Require(firstModel() == catalog[enemies[enemies.size() - 9]].name,
            "model scrollbar drag cannot reach the final page");
    Mouse(gallery, platform::Event::Type::MouseUp, 900, 1000, 1);
    Mouse(gallery, platform::Event::Type::MouseMove, 609, 0);
    gallery.Update({});
    Require(firstModel() == catalog[enemies[enemies.size() - 9]].name,
            "releasing outside the model track did not end dragging");

    Mouse(gallery, platform::Event::Type::MouseDown, 609, 400, 1);
    Mouse(gallery, platform::Event::Type::MouseMove, 609, -100);
    Mouse(gallery, platform::Event::Type::MouseUp, 609, -100, 1);
    gallery.Update({});
    Require(firstModel() == catalog[enemies.front()].name, "model scrollbar drag cannot return to the first page");
    Mouse(gallery, platform::Event::Type::MouseDown, 609, 400, 1);
    Click(gallery, 609, 400);
    Mouse(gallery, platform::Event::Type::MouseUp, 609, 400, 1);
    gallery.Update({});
    Require(firstModel() == catalog[enemies[9]].name && !gallery.IsViewing(),
            "model track click must advance one page without selecting a model");

    const float modelThumbHeight = std::max(24.f, 295.f * 9 / enemies.size());
    const float modelThumbCenter = 116 + (295 - modelThumbHeight) * 9 / (enemies.size() - 9) + modelThumbHeight / 2;
    Mouse(gallery, platform::Event::Type::MouseDown, 609, modelThumbCenter, 1);
    platform::Event focus;
    focus.type = platform::Event::Type::Focus; focus.active = false;
    gallery.HandleEvent(focus, 0, 0);
    const auto afterFocus = firstModel();
    Mouse(gallery, platform::Event::Type::MouseMove, 609, 0);
    gallery.Update({});
    Require(firstModel() == afterFocus, "focus loss must cancel model scrollbar dragging");
    Mouse(gallery, platform::Event::Type::MouseWheel, 350, 200, 0, 1);
    gallery.Update({});
    Require(firstModel() != afterFocus, "mouse wheel stopped browsing models after dragging");

    // The selected animation remains separate from the animation list's
    // viewport: browsing a long list should keep the current model playing.
    Require(gallery.SelectModel(0), "select long animation-list subject");
    const auto& clips = catalog[0].animations;
    Require(clips.size() > 18, "animation scrollbar regression needs multiple pages");
    Click(gallery, 170, 374);
    auto firstAnimation = [&] { return LabelAt(gallery, 36, 119); };
    Require(firstAnimation() == clips.front().name, "animation picker did not open on its first row");
    const auto beforeCamera = gallery.Camera();
    const int beforeAnimation = gallery.SelectedAnimation();
    Mouse(gallery, platform::Event::Type::MouseDown, 309, 120, 1);
    Click(gallery, 309, 120);
    Mouse(gallery, platform::Event::Type::MouseMove, 1000, 1000);
    gallery.Update({});
    Require(firstAnimation() == clips[clips.size() - 9].name,
            "animation scrollbar drag cannot reach the final page");
    Require(gallery.SelectedAnimation() == beforeAnimation && gallery.Playing(),
            "animation scrollbar drag selected or paused a clip");
    Require(gallery.Camera().yaw == beforeCamera.yaw && gallery.Camera().pitch == beforeCamera.pitch,
            "animation scrollbar drag rotated the model");
    Mouse(gallery, platform::Event::Type::MouseUp, 1000, 1000, 1);
    Mouse(gallery, platform::Event::Type::MouseMove, 309, 0);
    gallery.Update({});
    Require(firstAnimation() == clips[clips.size() - 9].name,
            "releasing outside the animation track did not end dragging");
    const float beforeListWheelZoom = gallery.Camera().zoom;
    Mouse(gallery, platform::Event::Type::MouseWheel, 100, 200, 0, 1);
    gallery.Update({});
    Require(firstAnimation() != clips[clips.size() - 9].name && gallery.Camera().zoom == beforeListWheelZoom,
            "wheel over the animation list must scroll clips without zooming the camera");
    Mouse(gallery, platform::Event::Type::MouseWheel, 100, 200, 0, -1);
    gallery.Update({});
    Require(firstAnimation() == clips[clips.size() - 9].name, "animation list wheel cannot return to the last page");
    Click(gallery, 100, 125);
    Require(gallery.SelectedAnimation() == static_cast<int>(clips.size()) - 9,
            "clicking a scrolled animation row did not select the visible clip");
    Require(firstAnimation().empty(), "choosing an animation did not close its picker");
    Click(gallery, 170, 374);
    Mouse(gallery, platform::Event::Type::MouseDown, 309, 330, 1);
    Mouse(gallery, platform::Event::Type::MouseMove, 309, -100);
    Mouse(gallery, platform::Event::Type::MouseUp, 309, -100, 1);
    gallery.Update({});
    Require(firstAnimation() == clips.front().name, "animation scrollbar drag cannot return to the first page");
    std::puts("Validated independent model and animation scrollbar dragging, paging and pointer release");
}
void CheckSoundTiming(port::AssetStore& assets, play::ModelGallery& gallery, port::ModelRenderer& renderer) {
    const auto& entries = play::ModelCatalog();
    size_t clips=0, cues=0;
    gallery.SetMuted(false);
    for (int model = 0; model < static_cast<int>(entries.size()); ++model)
        for (int animation = 0; animation < static_cast<int>(entries[model].animations.size()); ++animation) {
            const auto& clip = entries[model].animations[animation];
            if (clip.sounds.empty()) continue;
            Require(gallery.SelectModel(model) && gallery.SelectAnimation(animation) && gallery.Prepare(assets, renderer),
                    "load sound timing subject");
            gallery.TakeSounds(); gallery.TakeStopSounds();
            std::vector<uint32_t> observed;
            const int length=gallery.AnimationLength();
            for (int frame = 0; frame < length*2; ++frame) {
                gallery.Update({});
                auto sounds = gallery.TakeSounds();
                std::vector<const play::ModelSoundCue*> expected;
                if(length>1 || frame==0)
                    for (const auto& cue : clip.sounds) if (cue.tick == frame%length) expected.push_back(&cue);
                Require(sounds.size() == expected.size(), entries[model].name + " / " + clip.name + ": cue emitted at wrong animation tick");
                for(size_t index=0;index<expected.size();++index) {
                    const auto& cue=*expected[index];
                    Require(cue.variants.empty() ? sounds[index]==cue.soundId :
                                std::find(cue.variants.begin(),cue.variants.end(),sounds[index])!=cue.variants.end(),
                            entries[model].name + " / " + clip.name + ": cue used a sound from another action or voice family");
                }
                observed.insert(observed.end(), sounds.begin(), sounds.end());
            }
            Require(observed.size() == clip.sounds.size()*(length>1?2:1), "animation skipped or repeated a cue within a cycle");
            Input(gallery, 8);
            const int paused = gallery.AnimationTick();
            for (int tick = 0; tick < 10; ++tick) gallery.Update({});
            Require(gallery.AnimationTick() == paused && gallery.TakeSounds().empty(), "paused animation emitted new sounds");
            ++clips; cues+=clip.sounds.size();
        }
    Require(clips>0, "no sound cue animation to validate");
    std::printf("Validated every cue in %zu sounding clips (%zu cues), repeats, held poses and paused playback\n",clips,cues);
}

void CheckSoundVariations(port::AssetStore& assets,port::ModelRenderer& renderer) {
    const auto& catalog=play::ModelCatalog();
    auto tablePool=[&](uint32_t address,int count) {
        std::vector<uint32_t> result;
        for(int index=0;index<count;++index) result.push_back(assets.Exe().U32(address+index*4));
        return result;
    };
    // EnemyVoice reads these original EXE tables, rather than one common
    // preview sample. Heavy and other zombies have separate attack pairs;
    // blood impacts vary independently of the fixed pain/death voice.
    const auto heavy=tablePool(0x005776A0,2),other=tablePool(0x005776A8,2),blood=tablePool(0x00577674,5);
    struct Subject {
        const char* name; int motion; std::vector<uint32_t> pool; uint32_t fixedVoice=0; int clipIndex=-1;
    };
    const Subject subjects[] = {
        {"Bob",0xFD,heavy}, {"Max",0xC2,heavy},
        {"Mickey",0xE,other}, {"Kageo",0x1A1,other},
        {"Bob",0x101,blood,assets.Exe().U32(0x00577688)},
        {"David",0x3C0,blood,assets.Exe().U32(0x0057768C)},
        {"Kageo",0x1A5,blood,assets.Exe().U32(0x00577694)},
        {"Murrer",0x219,{0x1D18A9,0x1E18A9},0x118A9},
        {"Owl",-1,{0x4217A9,0x4117A9},0,1},
    };
    auto indices=[&](const Subject& subject) {
        const auto model=std::find_if(catalog.begin(),catalog.end(),[&](const auto& entry){return entry.name==subject.name;});
        Require(model!=catalog.end(),std::string("missing sound variation model: ")+subject.name);
        const auto clip=subject.clipIndex>=0 ? model->animations.begin()+subject.clipIndex :
            std::find_if(model->animations.begin(),model->animations.end(),[&](const auto& animation){return animation.motionId==subject.motion;});
        Require(clip!=model->animations.end(),std::string("missing sound variation animation: ")+subject.name);
        return std::array<int,2>{static_cast<int>(model-catalog.begin()),static_cast<int>(clip-model->animations.begin())};
    };
    for(const auto& subject:subjects) {
        const auto selected=indices(subject);
        const auto& clip=catalog[selected[0]].animations[selected[1]];
        const std::set<uint32_t> native(subject.pool.begin(),subject.pool.end());
        int slot=0,variantSlot=-1;
        for(const auto& cue:clip.sounds) if(cue.tick==0) {
            if(std::set<uint32_t>(cue.variants.begin(),cue.variants.end())==native) variantSlot=slot;
            ++slot;
        }
        Require(variantSlot>=0,std::string(subject.name)+": animation does not use its complete original sample pool");
        play::ModelGallery gallery(0xC0FFEEu+selected[0]);
        Require(gallery.SelectModel(selected[0]) && gallery.SelectAnimation(selected[1]) && gallery.Prepare(assets,renderer),
                "prepare native sound variation subject");
        for(int phase=0;phase<2;++phase) {
            std::set<uint32_t> heard;
            uint32_t previous=0;
            bool repeated=false;
            for(int take=0;take<96;++take) {
                if(phase) {
                    if(take%2==0) Require(gallery.SelectModel(selected[0]),"reselect sound variation model");
                    Require(gallery.SelectAnimation(selected[1]) && gallery.Prepare(assets,renderer),"reselect sound variation animation");
                    gallery.Update({});
                } else Click(gallery,230,416); // Reset emits this clip's authored tick-zero cues.
                const auto emitted=gallery.TakeSounds();
                Require(emitted.size()==static_cast<size_t>(slot),std::string(subject.name)+": variant changed the number of simultaneous cues");
                Require(native.count(emitted[variantSlot])==1,std::string(subject.name)+": heard another action's or family's sample");
                if(subject.fixedVoice)
                    Require(std::find(emitted.begin(),emitted.end(),subject.fixedVoice)!=emitted.end(),
                            std::string(subject.name)+": varying the impact replaced the fixed voice or splash");
                repeated=repeated || (take>0 && emitted[variantSlot]==previous);
                previous=emitted[variantSlot];
                heard.insert(emitted[variantSlot]);
            }
            Require(heard==native,std::string(subject.name)+(phase ? ": selection restarted a fixed sound take" : ": Reset restarted a fixed sound take"));
            Require(repeated,std::string(subject.name)+": original independent choices were replaced by a forced alternating cycle");
        }
    }
    // Approach audio genuinely is fixed in the original game.
    const auto approach=indices({"Bob",0x10E,{}});
    play::ModelGallery waiting(123);
    Require(waiting.SelectModel(approach[0]) && waiting.SelectAnimation(approach[1]) && waiting.Prepare(assets,renderer),"prepare original approach cue");
    for(const auto& cue:catalog[approach[0]].animations[approach[1]].sounds)
        if(cue.soundId==0x1917A9) Require(cue.variants.empty(),"fixed approach groan acquired invented variants");
    for(int take=0;take<24;++take) {
        Click(waiting,230,416);
        Require(waiting.TakeSounds()==std::vector<uint32_t>{0x1917A9},"original fixed approach groan changed");
    }

    const auto attack=indices(subjects[0]);
    auto prepare=[&](play::ModelGallery& gallery) {
        Require(gallery.SelectModel(attack[0]) && gallery.SelectAnimation(attack[1]) && gallery.Prepare(assets,renderer),"prepare isolated random sound playback");
    };
    constexpr uint32_t seed=0x51A1D123;
    play::ModelGallery reference(seed),candidate(seed),busy(seed),quiet(seed);
    prepare(reference);
    std::vector<std::vector<uint32_t>> sequence;
    for(int take=0;take<32;++take) { Click(reference,230,416); sequence.push_back(reference.TakeSounds()); }
    prepare(candidate); prepare(busy);
    for(int take=0;take<32;++take) {
        for(int extra=0;extra<7;++extra) { Click(busy,230,416); busy.TakeSounds(); }
        Click(candidate,230,416);
        Require(candidate.TakeSounds()==sequence[take],"one viewer consumed another viewer's sound randomness");
    }
    quiet.SetMuted(true); prepare(quiet);
    for(int take=0;take<32;++take) {
        Click(quiet,230,416);
        for(int frame=0;frame<quiet.AnimationLength();++frame) quiet.Update({});
        Require(quiet.TakeSounds().empty(),"muted variations accumulated deferred sound cues");
    }
    quiet.SetMuted(false);
    Require(quiet.TakeSounds().empty(),"unmute replayed a historical random one-shot");
    Click(quiet,230,416);
    Require(quiet.TakeSounds()==sequence[0],"muted playback consumed sound variations");
    Input(quiet,8);
    for(int take=0;take<32;++take) {
        Click(quiet,230,416);
        for(int frame=0;frame<5;++frame) quiet.Update({});
        Require(quiet.TakeSounds().empty(),"paused variations accumulated deferred sound cues");
    }
    Input(quiet,8);
    Require(quiet.TakeSounds()==sequence[1],"paused playback consumed sound variations or reset its random stream");
    std::puts("Validated original attack families, five blood impacts, fixed voices, creature variants, Reset/selection variation and isolated muted/paused randomness");
}

struct GallerySoundBackend final:game::SoundBackend {
    struct Channel { bool buffered=false,playing=false,loop=false; std::string path; };
    std::array<Channel,game::kSndChannels> channels{};
    int plays=0;
    bool Open(int channel,const std::string& path,bool) override {
        if(channels[channel].buffered) return false;
        channels[channel]={true,false,false,path}; return true;
    }
    bool Duplicate(int from,int to) override {
        if(channels[to].buffered || !channels[from].buffered) return false;
        channels[to]=channels[from]; channels[to].playing=false; return true;
    }
    bool Play(int channel,bool loop) override {
        if(!channels[channel].buffered) return false;
        channels[channel].playing=true; channels[channel].loop=loop; ++plays; return true;
    }
    void Pause(int channel) override { channels[channel].playing=false; }
    void Stop(int channel) override { channels[channel].playing=false; }
    void Release(int channel) override { channels[channel]={}; }
    bool IsPlaying(int channel) override { return channels[channel].playing; }
    bool Silent() const {
        for(int ch=0;ch<=game::kSndVoiceChannel;++ch) if(channels[ch].playing) return false;
        return true;
    }
};

void CheckMute(port::AssetStore& assets,port::ModelRenderer& renderer) {
    play::ModelGallery gallery;
    game::SoundTable table; Require(table.Load(assets.Exe()),"load gallery sound table");
    GallerySoundBackend backend; game::SoundDriver driver(backend);
    const auto& catalog=play::ModelCatalog();
    int model=-1,animation=-1;
    for(int m=0;m<static_cast<int>(catalog.size()) && model<0;++m)
        for(int a=0;a<static_cast<int>(catalog[m].animations.size());++a)
            if(std::any_of(catalog[m].animations[a].sounds.begin(),catalog[m].animations[a].sounds.end(),
                           [](const auto& cue){return cue.tick==0;})) { model=m; animation=a; break; }
    Require(model>=0,"mute regression needs an audible clip-start cue");
    auto load=[&] {
        Require(gallery.SelectModel(model) && gallery.SelectAnimation(animation) && gallery.Prepare(assets,renderer),
                "load mute regression subject");
        play::ApplyModelGalleryAudio(gallery,&driver,table);
    };
    load(); Require(!gallery.Muted() && HasText(gallery,"MUTE"),"viewer starts with an available mute button");
    gallery.Update({}); play::ApplyModelGalleryAudio(gallery,&driver,table);
    Require(!backend.Silent(),"animation cue did not reach the sound driver");
    const int tick=gallery.AnimationTick(); const auto camera=gallery.Camera();
    Mouse(gallery,platform::Event::Type::MouseDown,480,40,1);
    Click(gallery,480,40);
    Mouse(gallery,platform::Event::Type::MouseMove,485,45);
    Mouse(gallery,platform::Event::Type::MouseUp,485,45,1);
    play::ApplyModelGalleryAudio(gallery,&driver,table);
    Require(gallery.Muted() && HasText(gallery,"UNMUTE") && gallery.Playing() && backend.Silent(),
            "mute must stop active sounds immediately without pausing animation");
    Require(gallery.Camera().yaw==camera.yaw && gallery.Camera().pitch==camera.pitch,"mute button started a camera drag");
    const int plays=backend.plays;
    for(int i=0;i<gallery.AnimationLength()*2+5;++i) { gallery.Update({}); play::ApplyModelGalleryAudio(gallery,&driver,table); }
    Require(backend.plays==plays && gallery.Playing(),"muted animation repeated its audio or stopped playing");
    Require(gallery.AnimationLength()==1 || gallery.AnimationTick()!=tick,"muted animation time stopped advancing");
    load(); gallery.Update({}); play::ApplyModelGalleryAudio(gallery,&driver,table);
    Require(gallery.Muted() && backend.Silent(),"model/animation selection discarded viewer mute");
    KeyEvent(gallery,'m',true); KeyEvent(gallery,'m',false);
    Require(!gallery.Muted() && HasText(gallery,"MUTE"),"M shortcut must toggle mute once");
    platform::Event repeat; repeat.type=platform::Event::Type::KeyDown; repeat.ch='m'; repeat.repeat=true;
    gallery.HandleEvent(repeat,0,0);
    Require(!gallery.Muted() && gallery.TakeSounds().empty(),"held M repeated its toggle or unmute replayed old one-shot cues");
    Click(gallery,230,416); play::ApplyModelGalleryAudio(gallery,&driver,table);
    Require(backend.plays>plays && !backend.Silent(),"unmute/reset failed to restore authored cues");
    // Queue a cue, then mute before the audio adapter drains it.
    load(); gallery.Update({}); gallery.SetMuted(true);
    const int queuedPlays=backend.plays;
    play::ApplyModelGalleryAudio(gallery,&driver,table);
    Require(backend.plays==queuedPlays && backend.Silent(),"mute leaked a cue queued in the same frame");
    gallery.Close(); gallery.Open();
    Require(gallery.Muted(),"viewer mute should persist while returning through Extras");

    // Native motor-loop flags are preserved and cannot accumulate on repeat.
    int loopModel=-1,loopAnimation=-1;
    for(int m=0;m<static_cast<int>(catalog.size()) && loopModel<0;++m)
        for(int a=0;a<static_cast<int>(catalog[m].animations.size());++a)
            if(std::any_of(catalog[m].animations[a].sounds.begin(),catalog[m].animations[a].sounds.end(),
                           [&](const auto& cue){return cue.tick==0 && game::ResolveSound(table,cue.soundId).loop;})) {
                loopModel=m; loopAnimation=a; break;
            }
    Require(loopModel>=0,"native sustained effects must be covered by gallery playback tests");
    if(loopModel>=0) {
        gallery.SetMuted(false);
        Require(gallery.SelectModel(loopModel) && gallery.SelectAnimation(loopAnimation) && gallery.Prepare(assets,renderer),
                "load native looping effect");
        for(int change=0;change<9;++change) {
            Require(gallery.SelectModel(loopModel) && gallery.SelectAnimation(loopAnimation) && gallery.Prepare(assets,renderer),
                    "reload native looping effect");
            gallery.Update({}); play::ApplyModelGalleryAudio(gallery,&driver,table);
            Require(driver.LoopCount()==1,"clip changes left stale sustained-effect tracking behind");
        }
        for(int i=0;i<gallery.AnimationLength()*3;++i) {
            gallery.Update({}); play::ApplyModelGalleryAudio(gallery,&driver,table);
            int loops=0; for(const auto& channel:backend.channels) if(channel.playing && channel.loop) ++loops;
            Require(loops==1,"gallery motor loop accumulated or lost its native loop flag");
        }
        while(gallery.AnimationTick()<gallery.AnimationLength()-1) {
            gallery.Update({}); play::ApplyModelGalleryAudio(gallery,&driver,table);
        }
        gallery.SetMuted(true); play::ApplyModelGalleryAudio(gallery,&driver,table);
        Require(backend.Silent(),"mute left a motor loop playing");
        gallery.SetMuted(false); play::ApplyModelGalleryAudio(gallery,&driver,table);
        Require(!backend.Silent(),"unmute failed to resume the active motor loop");
        Input(gallery,8); play::ApplyModelGalleryAudio(gallery,&driver,table);
        Require(backend.Silent(),"pause left a motor loop playing");
        Input(gallery,8); play::ApplyModelGalleryAudio(gallery,&driver,table);
        Require(!backend.Silent(),"resume failed to restore the active motor loop");
        int resumedLoops=0; for(const auto& channel:backend.channels) if(channel.playing && channel.loop) ++resumedLoops;
        Require(resumedLoops==1,"resuming on the loop boundary started duplicate motors");
    }
    std::puts("Validated viewer-only mute, immediate stop, pending cues, selection persistence, shortcut and loop lifetime");
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: model_gallery_test <game directory>\n"); return 2; }
    port::AssetStore assets;
    std::string error;
    Require(assets.Open(argv[1], &error), error);
    CheckCatalog(assets);
    RecordingGpu gpu;
    port::ModelRenderer renderer;
    Require(renderer.Init(&gpu, &error), error);
    play::ModelGallery gallery;
    CheckGalleryAssets(assets, gallery, renderer, gpu);
    CheckCentering(assets,renderer,gpu);
    CheckInteractions(assets, gallery, renderer);
    CheckScrollHints();
    CheckDraggableLists();
    CheckSoundTiming(assets, gallery, renderer);
    CheckSoundVariations(assets,renderer);
    CheckMute(assets,renderer);
    std::puts("PASS model gallery");
}
