#pragma once

// Optional interpolation of completed simulation poses. This class owns only
// render copies; it never changes World, asset models, hit geometry or replay
// state. The previous/current pair advances at simulation ticks, not presents.
#include "model_renderer.h"
#include "game/anim.h"
#include <map>
#include <memory>

namespace port {

class RenderPoseHistory {
public:
    // Alpha is the fixed-clock accumulator fraction [0,1]. Using the previous
    // and current completed tick adds up to one 60 Hz tick of visual latency.
    // Disable during pauses, loading, replay catch-up and network stalls.
    void Set(bool enabled, float alpha);
    void Reset();
    bool Enabled() const { return enabled_; }
    bool Ready() const { return ready_; }

    // renderView and optional renderPosition initially contain the exact native
    // camera. Off returns without changing any item, pointer or float.
    void Apply(std::vector<ModelRenderer::SceneItem>& items,
               const TemporalSceneStamp& stamp, const game::Mat4& cameraWorld,
               game::Mat4* renderView, float* renderPosition = nullptr);

private:
    struct VertexPose { float position[3], normal[3]; };
    struct ItemPose {
        game::Mat4 transform;
        uint64_t topology = 0;
        std::vector<VertexPose> vertices;
    };
    struct Snapshot {
        bool valid = false;
        TemporalSceneStamp stamp;
        game::Mat4 cameraWorld, cameraView;
        std::map<MotionIdentity, ItemPose> items;
        std::map<MotionIdentity, size_t> groupCounts;
    };
    Snapshot Capture(const std::vector<ModelRenderer::SceneItem>& items,
                     const TemporalSceneStamp& stamp, const game::Mat4& cameraWorld,
                     const game::Mat4& cameraView) const;
    bool enabled_ = false, ready_ = false;
    float alpha_ = 1;
    Snapshot previous_, current_;
    std::vector<std::unique_ptr<game::Model>> renderModels_;
};

} // namespace port
