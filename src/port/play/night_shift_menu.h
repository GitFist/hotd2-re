#pragma once

#include <memory>
#include <string>
#include "game/world.h"
#include "render/model_renderer.h"

namespace play {
// Add BEFORE the menu's text/logo SpriteDraws. The helper assigns unique depths so the
// original unstable sprite qsort cannot reverse overlapping panel layers.
void AppendNightShiftPanels(const game::WorldHud& hud,std::vector<port::ModelRenderer::SpriteDraw>& out,float aspect=4.f/3.f,int opacity=100);

// The full original attract cycle (title, how-to-play, gameplay demos and stories),
// running in its own isolated World.
// Advance only for local menu ticks. Sounds go to a separate attraction mixer; never
// pass its input, state or save configuration into the actual game/network world.
class AttractPreview {
public:
    bool Step(const std::string& gameDir,bool enabled,int ticks=1);
    const game::World* Get() const { return enabled_?world_.get():nullptr; }
    const std::string& Error() const { return error_; }
    // Events from every advanced tick, in order. Warmup events establish persistent
    // audio state only: old gunshots/dialogue must not all play on the first frame.
    const std::vector<game::World::SoundEvent>& Sounds() const { return sounds_; }
    const std::vector<game::World::SoundEvent>& WarmupSounds() const { return warmupSounds_; }
    uint64_t SoundRevision() const { return soundRevision_; }
private:
    std::unique_ptr<game::World> world_;
    std::string error_;
    std::vector<game::World::SoundEvent> sounds_,warmupSounds_;
    uint64_t soundRevision_=0;
    bool enabled_=false,failed_=false;
};
} // namespace play
