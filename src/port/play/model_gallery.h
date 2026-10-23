#pragma once

#include <memory>
#include <string>
#include <vector>

#include "play/display_menu.h"
#include "play/model_catalog.h"
#include "platform/platform.h"
#include "render/model_renderer.h"
#include "render/ui_font.h"

namespace play {

// A local Extras modal. It never advances or changes the game World.
class ModelGallery {
public:
    struct CameraState { float yaw = 25, pitch = 8, zoom = 3.1f, height = 0; };
    ModelGallery();
    explicit ModelGallery(uint32_t soundSeed); // Reproducible viewer audio; independent of gameplay RNG.
    ~ModelGallery();
    ModelGallery(const ModelGallery&) = delete;
    ModelGallery& operator=(const ModelGallery&) = delete;

    void Open();
    void Close();
    bool IsOpen() const;
    bool IsViewing() const;
    game::MenuScrollHint ScrollHint() const;
    // Mouse coordinates are on the same logical 640 x 480 canvas as MenuInput.
    // Scroll thumbs capture native pointer events; selection and playback buttons
    // activate through Update only, avoiding duplicate mouse/gun shots.
    void HandleEvent(const platform::Event& event, float logicalX, float logicalY);
    void Update(const MenuInput& input); // one 60 Hz tick
    std::vector<uint32_t> TakeSounds();
    std::vector<uint32_t> TakeFeedbackSounds();  // UI feedback stays separate from authored animation cues.
    bool TakeStopSounds();
    bool TakeStopLoops(); // A new animation cycle ends sustained effects from the previous cycle.

    // Prepare before BeginFrame; Draw replaces the normal world draw. The caller
    // clears the target to black. The shared renderer is cleared on model changes.
    bool Prepare(port::AssetStore& assets, port::ModelRenderer& renderer);
    void Draw(port::ModelRenderer& renderer, const port::UiFont& font, int width, int height,
              float aspect = 4.f / 3.f) const;

    // Explicit selection also supports asset validation without simulated input.
    bool SelectModel(int catalogIndex);
    bool SelectAnimation(int animationIndex);
    int SelectedModel() const;
    int SelectedAnimation() const;
    int AnimationTick() const;
    int AnimationLength() const;
    bool Playing() const;
    bool Muted() const;
    void SetMuted(bool muted); // Viewer-only; keeps playback time and game volume settings.
    int LoadedPartCount() const;
    const CameraState& Camera() const;
    const std::string& Error() const;
    std::vector<game::WorldHud::MenuText> Text() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace play
