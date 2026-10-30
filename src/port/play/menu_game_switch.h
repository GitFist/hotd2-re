#pragma once
#include "game/world.h"
#include "render/model_renderer.h"

namespace play {
// Original wordmarks, independently of either game's character artwork.
std::vector<game::SpriteDef> MenuGameLogos(const game::WorldHud& hud);
// Keep the modal above its dimmed parent, including parent text and logos.
bool SplitOriginalPrompt(const game::WorldHud& hud,game::WorldHud& parent,game::WorldHud& prompt);
// Last overlay: covers the complete image, independent of menu opacity/aspect.
void AppendMenuGameFade(const game::WorldHud& hud,std::vector<port::ModelRenderer::SpriteDraw>& out);
}
