#pragma once

#include <memory>
#include <string>
#include "game/world.h"
#include "render/model_renderer.h"

namespace play {
// Add BEFORE the menu's text/logo SpriteDraws. The helper assigns unique depths so the
// original unstable sprite qsort cannot reverse overlapping panel layers.
void AppendNightShiftPanels(const game::WorldHud& hud,std::vector<port::ModelRenderer::SpriteDraw>& out,float aspect=4.f/3.f,int opacity=100);

// The original standby gameplay demos, running in their own isolated, silent World.
// Advance only for local menu ticks; never pass its input, state, sound calls or save
// configuration into the actual game or the network lockstep world.
class AttractPreview {
public:
    bool Step(const std::string& gameDir,bool enabled,int ticks=1);
    const game::World* Get() const { return enabled_?world_.get():nullptr; }
    const std::string& Error() const { return error_; }
private:
    std::unique_ptr<game::World> world_;
    std::string error_;
    int demo_=0;
    bool enabled_=false,failed_=false;
};
} // namespace play
