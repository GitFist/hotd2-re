// Native controller settings captures: real menu, system fonts and D3D11 panels.
// Synthetic connection state needs neither paired controllers nor game assets.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "platform/platform.h"
#include "play/controller_menu.h"
#include "play/night_shift_menu.h"
#include "render/ui_font.h"

namespace {
constexpr int kWidth = 1280, kHeight = 960;

void Require(bool ok, const char* why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}

void Key(play::ControllerMenu& menu, input::GamepadGuns& pads, uint32_t key) {
    play::MenuInput in;
    in.pressed = key;
    Require(!menu.Update(in, pads), "controller fixture unexpectedly closed");
}

struct Bounds {
    float left = std::numeric_limits<float>::infinity();
    float top = std::numeric_limits<float>::infinity();
    float right = -std::numeric_limits<float>::infinity();
    float bottom = -std::numeric_limits<float>::infinity();
};

class Renderer {
public:
    explicit Renderer(std::filesystem::path output) : output_(std::move(output)) {
        std::filesystem::create_directories(output_);
        platform::Options options;
        options.window = false;
        std::string error;
        Require(platform_.Init(options, &error), error.c_str());
        gpu_ = platform_.CreateGpu(platform::Renderer::D3D11, kWidth, kHeight, false, 0, &error);
        Require(gpu_ != nullptr, error.c_str());
        Require(renderer_.Init(gpu_.get(), &error), error.c_str());
        Require(font_.Init() && font_.Available(), "native system font atlas unavailable");
    }

    void Save(const std::string& name, const play::ControllerMenu& menu, const input::GamepadGuns& pads) {
        game::WorldHud hud;
        menu.ModernHud(pads, hud);
        Require(hud.nightShiftPage >= 0 && !hud.menuText.empty(), "controller page missing panels or text");
        for (const auto& panel : hud.menuPanels)
            Require(std::isfinite(panel.left) && std::isfinite(panel.top) && std::isfinite(panel.right) &&
                        std::isfinite(panel.bottom) && panel.left >= 0 && panel.top >= 0 && panel.right <= 640 &&
                        panel.bottom <= 480 && panel.left < panel.right && panel.top < panel.bottom,
                    "controller panel exceeds logical canvas");

        for (const auto& text : hud.menuText) CheckBounds(name, text, 0, 0, 640, 480);
        const auto& profile = pads.settings.player[menu.SelectedPlayer()];
        const std::string values[]{
            "Player " + std::to_string(menu.SelectedPlayer() + 1),
            std::to_string(profile.sensitivity) + "%",
            std::to_string(profile.deadzone) + "%",
            input::gamepad::BindingName(profile.bindings[0]),
            input::gamepad::BindingName(profile.bindings[1]),
            input::gamepad::BindingName(profile.bindings[2])
        };
        const char* labels[]{"Player", "Aim sensitivity", "Stick deadzone", "Fire", "Reload", "Pause", "Restore defaults", "Back"};
        for (int row = 0; row < play::ControllerMenu::kRowCount; ++row) {
            const float top = play::ControllerMenu::RowY(row) + 8;
            const float bottom = play::ControllerMenu::RowY(row) + 35;
            const auto& label = Find(hud, 34, top + 2);
            Require(label.text == labels[row], "controller row label missing or incorrect");
            CheckBounds(name, label, 24, top, 280, bottom);
            if (row < play::ControllerMenu::kDefaults) {
                const auto& value = Find(hud, 308, top + 2);
                Require(value.text == values[row], "controller row shows another player's settings");
                CheckBounds(name, value, 306, top, 592, bottom);
            }
        }
        Require(Find(hud, 26, 20).text == "CONTROLLERS", "controller page title missing");
        Require(Find(hud, 34, 338).text == "P1: Connected    P2: Not connected", "controller connection states missing");

        std::vector<port::ModelRenderer::SpriteDraw> sprites;
        play::AppendNightShiftPanels(hud, sprites, 4.f / 3.f, 65);
        font_.Append(hud.menuText, sprites);
        const float background[]{.24f, .27f, .31f, 1};
        Require(gpu_->BeginFrame(background), "begin offscreen controller frame");
        renderer_.DrawSprites(kWidth, kHeight, sprites);
        std::vector<uint8_t> pixels;
        Require(gpu_->ReadPixels(&pixels), "read native controller pixels");
        gpu_->EndFrame();
        Require(pixels.size() == static_cast<size_t>(kWidth) * kHeight * 4, "controller capture dimensions incorrect");
        Require(platform::Platform::SavePng((output_ / (name + ".png")).string(), pixels, kWidth, kHeight),
                "save native controller PNG");
    }

private:
    const game::WorldHud::MenuText& Find(const game::WorldHud& hud, float x, float y) const {
        const auto found = std::find_if(hud.menuText.begin(), hud.menuText.end(), [=](const auto& text) {
            return text.x == x && text.y == y;
        });
        Require(found != hud.menuText.end(), "expected controller text command missing");
        return *found;
    }

    void CheckBounds(const std::string& name, const game::WorldHud::MenuText& text,
                     float left, float top, float right, float bottom) const {
        Require(std::isfinite(text.x) && std::isfinite(text.y) && std::isfinite(text.scale) && text.scale > 0,
                "invalid controller text command");
        std::vector<port::ModelRenderer::SpriteDraw> glyphs;
        font_.Append({text}, glyphs);
        Require(!glyphs.empty(), "controller text has no native glyph geometry");
        Bounds bounds;
        for (const auto& glyph : glyphs) for (const auto& vertex : glyph.v) {
            bounds.left = std::min(bounds.left, vertex.x);
            bounds.top = std::min(bounds.top, vertex.y);
            bounds.right = std::max(bounds.right, vertex.x);
            bounds.bottom = std::max(bounds.bottom, vertex.y);
        }
        if (bounds.left < left || bounds.top < top || bounds.right > right || bounds.bottom > bottom) {
            std::fprintf(stderr, "%s: native glyphs exceed [%.1f %.1f %.1f %.1f]: %s [%.1f %.1f %.1f %.1f]\n",
                         name.c_str(), left, top, right, bottom, text.text.c_str(),
                         bounds.left, bounds.top, bounds.right, bounds.bottom);
            Require(false, "controller glyphs or shadows exceed their rectangle");
        }
    }

    std::filesystem::path output_;
    platform::Platform platform_;
    std::unique_ptr<port::GpuDevice> gpu_;
    port::ModelRenderer renderer_;
    port::UiFont font_;
};
} // namespace

int main(int argc, char** argv) {
    Require(argc <= 2, "usage: controller_menu_render_test [output directory]");
    Renderer renderer(argc == 2 ? std::filesystem::path(argv[1]) : std::filesystem::path("build") / "controllers-ui");
    input::GamepadGuns pads;
    pads.SetConnected(0, true);
    play::ControllerMenu menu;
    menu.Open();
    Require(menu.SelectedPlayer() == 0, "controller page must open on Player 1");
    renderer.Save("controllers-player-1-default", menu, pads);

    auto& custom = pads.settings.player[1];
    custom.sensitivity = 175;
    custom.deadzone = 8;
    custom.bindings = {
        input::gamepad::ControlMask(input::gamepad::Control::RightShoulder),
        input::gamepad::ControlMask(input::gamepad::Control::East) |
            input::gamepad::ControlMask(input::gamepad::Control::West) |
            input::gamepad::ControlMask(input::gamepad::Control::LeftTrigger),
        input::gamepad::ControlMask(input::gamepad::Control::North)
    };
    Key(menu, pads, 0x80);
    Require(menu.SelectedPlayer() == 1, "menu input must select Player 2");
    for (int row = 0; row < play::ControllerMenu::kFire; ++row) Key(menu, pads, 0x20);
    Require(menu.Cursor() == play::ControllerMenu::kFire, "menu navigation must select Fire");
    renderer.Save("controllers-player-2-custom", menu, pads);
    std::printf("PASS 2 native controller captures; player values, connection states and glyph bounds\n");
}
