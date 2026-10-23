// Native GPU regression fixture for overflow cues. Uses actual Options state,
// the shipped font atlas and D3D11 readback; no original game assets are needed.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "platform/platform.h"
#include "play/display_menu.h"
#include "play/night_shift_menu.h"
#include "play/scroll_hint.h"
#include "render/ui_font.h"

namespace {
constexpr int kWidth = 1280, kHeight = 960;
void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
bool InEdge(float x, float y, const game::MenuScrollHint& h, bool top, float margin = 0) {
    const float height = std::min(18.f, (h.bottom - h.top) / 4.f);
    const float first = top ? h.top : h.bottom - height;
    const float last = top ? h.top + height : h.bottom;
    return x >= h.left - margin && x <= h.right + margin && y >= first - margin && y <= last + margin;
}
void Geometry(const game::MenuScrollHint& hint) {
    std::vector<port::ModelRenderer::SpriteDraw> cues;
    play::AppendMenuScrollHint(hint, cues);
    Require(cues.size() == 4u * (unsigned(hint.above) + unsigned(hint.below)), "four quads per overflowing edge, none otherwise");
    for (size_t i = 0; i < cues.size(); ++i) {
        const bool top = hint.above && i < 4;
        for (const auto& vertex : cues[i].v)
            Require(std::isfinite(vertex.x) && std::isfinite(vertex.y) && InEdge(vertex.x, vertex.y, hint, top),
                    "cue geometry must stay inside its list edge");
        if (i % 4 < 2) {
            const auto& v = cues[i].v;
            const unsigned a = v[0].diffuse >> 24, b = v[1].diffuse >> 24;
            Require(a != b && a == (v[2].diffuse >> 24) && b == (v[3].diffuse >> 24), "veil and haze need per-vertex alpha gradients");
            Require(top ? a > b && b == 0 : b > a && a == 0, "fade must become transparent toward list interior");
        }
    }
    // Widescreen uses the same centered canvas transform as the menus.
    std::vector<port::ModelRenderer::SpriteDraw> wide;
    play::AppendMenuScrollHint(hint, wide, 16.f / 9.f);
    Require(wide.size() == cues.size(), "widescreen must preserve the active edge count");
    for (size_t i = 0; i < cues.size(); ++i)
        for (int v = 0; v < 4; ++v)
            Require(std::abs(wide[i].v[v].x - port::CenteredViewportX(cues[i].v[v].x, 16.f / 9.f)) < .001f &&
                        wide[i].v[v].y == cues[i].v[v].y, "widescreen cue alignment must match list panels");
}

class Fixture {
public:
    explicit Fixture(std::filesystem::path output) : output_(std::move(output)) {
        std::filesystem::create_directories(output_);
        platform::Options options; options.window = false;
        std::string error;
        Require(platform_.Init(options, &error), error.c_str());
        gpu_ = platform_.CreateGpu(platform::Renderer::D3D11, kWidth, kHeight, false, 0, &error);
        Require(gpu_ != nullptr, error.c_str());
        Require(renderer_.Init(gpu_.get(), &error), error.c_str());
        Require(font_.Init() && font_.Available(), "native font atlas unavailable");
        index_.open(output_ / "index.txt");
        index_ << "Actual Options state and synthetic contrast fixture. Every capture is compared with the same GPU frame without hints.\n";
    }
    void Save(const std::string& name, const game::WorldHud& hud, const game::MenuScrollHint& hint, int opacity, bool gradient = false) {
        Geometry(hint);
        std::vector<port::ModelRenderer::SpriteDraw> sprites;
        play::AppendNightShiftPanels(hud, sprites, 4.f / 3.f, opacity);
        font_.Append(hud.menuText, sprites);
        const auto baseline = Render(sprites);
        play::AppendMenuScrollHint(hint, sprites);
        const auto pixels = Render(sprites);
        size_t changed[2] = {};
        for (int y = 0; y < kHeight; ++y) for (int x = 0; x < kWidth; ++x) {
            const size_t at = (size_t(y) * kWidth + x) * 4;
            bool different = false;
            for (int c = 0; c < 3; ++c) different |= pixels[at + c] != baseline[at + c];
            if (!different) continue;
            const float logicalX = (x + .5f) / 2, logicalY = (y + .5f) / 2;
            const bool top = hint.above && InEdge(logicalX, logicalY, hint, true, .5f);
            const bool bottom = hint.below && InEdge(logicalX, logicalY, hint, false, .5f);
            Require(top || bottom, "GPU cue changed pixels outside its list edge (one physical pixel raster tolerance)");
            if (top) ++changed[0];
            if (bottom) ++changed[1];
        }
        Require(hint.above ? changed[0] > 100 : changed[0] == 0, "GPU top cue presence must match actual overflow");
        Require(hint.below ? changed[1] > 100 : changed[1] == 0, "GPU bottom cue presence must match actual overflow");
        if (gradient) {
            auto delta = [&](float y) {
                int sum = 0;
                for (int x = int((hint.left + 20) * 2); x < int((hint.left + 60) * 2); ++x) {
                    const size_t at = (size_t(int(y * 2)) * kWidth + x) * 4;
                    for (int c = 0; c < 3; ++c) sum += std::abs(int(pixels[at + c]) - int(baseline[at + c]));
                }
                return sum;
            };
            Require(delta(hint.top + 1) > delta(hint.top + 16) * 2, "GPU top fade must weaken toward interior");
            Require(delta(hint.bottom - 2) > delta(hint.bottom - 17) * 2, "GPU bottom fade must weaken toward interior");
        }
        Require(platform::Platform::SavePng((output_ / (name + ".png")).string(), pixels, kWidth, kHeight), "write native cue capture");
        Require(platform::Platform::SavePng((output_ / (name + "-baseline.png")).string(), baseline, kWidth, kHeight), "write comparison baseline");
        index_ << name << ".png\t" << opacity << "% panels\ttop changed=" << changed[0] << "\tbottom changed=" << changed[1] << '\n';
        ++captures_;
    }
    int Count() const { return captures_; }
private:
    std::vector<uint8_t> Render(const std::vector<port::ModelRenderer::SpriteDraw>& sprites) {
        const float clear[] = {.32f, .43f, .55f, 1};
        Require(gpu_->BeginFrame(clear), "begin GPU scroll-cue frame");
        renderer_.DrawSprites(kWidth, kHeight, sprites);
        std::vector<uint8_t> pixels;
        Require(gpu_->ReadPixels(&pixels) && pixels.size() == size_t(kWidth) * kHeight * 4, "read complete GPU cue pixels");
        gpu_->EndFrame();
        return pixels;
    }
    std::filesystem::path output_;
    platform::Platform platform_;
    std::unique_ptr<port::GpuDevice> gpu_;
    port::ModelRenderer renderer_;
    port::UiFont font_;
    std::ofstream index_;
    int captures_ = 0;
};
} // namespace

int main(int argc, char** argv) {
    Require(argc == 2, "usage: scroll_hint_render_test <output directory>");
    Fixture render(argv[1]);
    play::DisplayMenu menu;
    play::DisplaySettings settings;
    menu.Open(); menu.SetPage(play::DisplayMenu::kGraphics);
    auto save = [&](const char* name, bool above, bool below, int opacity = 65) {
        const auto hint = menu.ScrollHint();
        Require(hint.above == above && hint.below == below, "actual Options overflow state differs from fixture expectation");
        game::WorldHud hud; menu.ModernHud(settings, hud);
        render.Save(name, hud, hint, opacity);
    };
    save("options-first", false, true);
    play::MenuInput wheel; wheel.scroll = -2;
    std::vector<uint32_t> sounds;
    menu.Update(wheel, settings, sounds);
    Require(menu.ScrollOffset() > 0 && sounds.empty(), "scroll real Options silently into middle");
    save("options-middle-transparent", true, true, 25);
    save("options-middle-opaque", true, true, 100);
    const auto& rows = play::DisplayMenu::Rows(play::DisplayMenu::kGraphics);
    auto last = std::find_if(rows.rbegin(), rows.rend(), [](int row) { return row != play::DisplayMenu::kBack; });
    Require(last != rows.rend(), "graphics list has rows");
    menu.FocusRow(*last);
    save("options-last", true, false);
    menu.SetPage(play::DisplayMenu::kSound);
    save("options-short-sound", false, false);
    menu.SetPage(play::DisplayMenu::kGraphics);
    play::MenuInput hide; hide.preview = true;
    menu.Update(hide, settings, sounds);
    Require(menu.UiHidden(), "hide Options UI using actual input");
    save("options-hidden", false, false);
    game::WorldHud contrast;
    contrast.nightShiftPage = 9;
    contrast.menuPanels = {{24, 100, 616, 410, 0xFF090D10}};
    contrast.menuText = {{40, 205, "17  Chapter 1 (Alternate)", .8f, 0xFFECE7DC},
                         {40, 255, "18  Chapter 2 (Alternate)", .8f, 0xFFECE7DC}};
    const game::MenuScrollHint both{24, 100, 616, 410, true, true};
    render.Save("label-contrast-transparent", contrast, both, 25, true);
    render.Save("label-contrast-opaque", contrast, both, 100, true);
    std::printf("PASS %d native scroll-cue captures plus baselines: real first/middle/last/short/hidden Options, edge-only pixels, alpha gradients and transparent/opaque contrast\n", render.Count());
}
