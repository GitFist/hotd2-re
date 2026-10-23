#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace play {

enum class ModelCategory { Agents, Civilians, Enemies, Bosses };

// Some creatures animate complete mesh frames instead of an EXE skeleton.
enum class ModelAssembly { Skeleton, Owl, AmbientInsect, Fish };

struct ModelSoundCue {
    int tick = 0; // Authored 60 Hz animation tick, not a stored motion frame.
    uint32_t soundId = 0;
    // Empty means a fixed cue. Otherwise this is the complete native one-shot
    // pool (including soundId), with one independent choice per occurrence.
    std::vector<uint32_t> variants;
};

struct ModelAnimation {
    std::string name;
    int motionId = -1;
    std::vector<ModelSoundCue> sounds;
    int companionMotionId = -1;
    int tickLength = 0; // Non-skeletal assemblies use an authored mesh-frame cycle.
};

struct ModelNodeOverride {
    int node = 0;
    uint32_t modelId = 0; // Zero hides the skeleton node's mesh.
    int frameCount = 1; // Consecutive mesh IDs, advanced once per 60 Hz tick.
};

struct ModelCatalogEntry {
    std::string name;
    ModelCategory category = ModelCategory::Enemies;
    int skeletonType = -1;
    // Civilian part definitions in 0x004EC748: { bone, model ID }. The head
    // replaces node 2; part1 / part2 are extra hair, hats or clothing meshes.
    int headPart = -1, part1 = -1, part2 = -1;
    std::vector<ModelAnimation> animations;
    std::vector<ModelNodeOverride> nodeOverrides;
    // Additive meshes attached directly to the indicated skeleton bone.
    // Uses the same record shape as overrides but does not replace a node.
    std::vector<ModelNodeOverride> extraMeshes;
    // Zeal's animated wings are a second skeleton attached at body node 1.
    int companionSkeletonType = -1, companionNode = 1;
    ModelAssembly assembly = ModelAssembly::Skeleton;
};

const char* ModelCategoryName(ModelCategory category);
const std::vector<ModelCatalogEntry>& ModelCatalog();

} // namespace play
