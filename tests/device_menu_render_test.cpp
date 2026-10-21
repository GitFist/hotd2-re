// Native UI visual fixture: real menu state machines, font atlas and GPU panels.
// Synthetic device reports avoid requiring paired hardware or original assets.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>

#include "platform/platform.h"
#include "play/lightgun_menu.h"
#include "play/night_shift_menu.h"
#include "play/wii_menu.h"
#include "render/ui_font.h"

namespace {

void Require(bool ok, const char* why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}

struct FakeWii final : input::wii::Backend {
    input::wii::Snapshot snapshot;
    void Enable(bool, bool) override {}
    void Scan() override { snapshot.scanning = true; }
    void Disconnect(int p) override { snapshot.player[p] = {}; }
    void SwapPlayers() override { std::swap(snapshot.player[0], snapshot.player[1]); }
    input::wii::Snapshot Poll() override {
        auto result = snapshot;
        for (auto& player : snapshot.player) player.pressed = 0;
        return result;
    }
};

struct WiiFixture {
    FakeWii* backend;
    input::WiiRemotes remotes;
    play::WiiMenu menu;
    WiiFixture() : backend(new FakeWii), remotes(nullptr, std::unique_ptr<input::wii::Backend>(backend)) {
        remotes.settings.enabled = true;
        for (int p = 0; p < 2; ++p) {
            auto& remote = backend->snapshot.player[p];
            remote.connected = remote.reserved = remote.tracking = true;
            remote.battery = p ? 41 : 82;
            remote.raw = p ? input::wii::Point{.88, .92} : input::wii::Point{.95, .95};
        }
        backend->snapshot.message = "Two remotes connected";
        remotes.Update();
        menu.Open();
    }
    void Key(uint32_t key) {
        play::MenuInput in;
        in.pressed = key;
        menu.Update(in, remotes);
    }
    void Player(int p) {
        for (int row = 0; row < 3 + p; ++row) Key(0x20);
        Key(8);
    }
    game::WorldHud Hud() const {
        game::WorldHud hud;
        menu.ModernHud(remotes, hud);
        return hud;
    }
};

const std::string ids[] = {"1111111111111111", "2222222222222222"};
void Feed(input::Lightguns& guns, int p, double x = .95, double y = .95, uint8_t down = 0, uint8_t up = 0, bool onScreen = true) {
    guns.Feed(ids[p], x, y, onScreen, false, down, up);
}

struct GunFixture {
    input::Lightguns guns;
    play::LightgunMenu menu;
    GunFixture() {
        guns.settings.manual = true;
        for (int p = 0; p < 2; ++p) {
            guns.settings.player[p].device = ids[p];
            Feed(guns, p, p ? .88 : .95, p ? .92 : .95);
        }
        menu.Open();
    }
    void Row(int row) {
        play::MenuInput in;
        in.pointer = in.trigger = true;
        in.px = 50;
        in.py = play::LightgunMenu::RowY(row) + 20;
        menu.Update(in, guns, true);
    }
    game::WorldHud Hud(bool gunMode = true) const {
        game::WorldHud hud;
        menu.ModernHud(guns, gunMode, hud);
        return hud;
    }
};

class Renderer {
   public:
    explicit Renderer(std::filesystem::path output) : output_(std::move(output)) {
        std::filesystem::create_directories(output_);
        platform::Options options;
        options.window = false;
        std::string error;
        Require(platform_.Init(options, &error), error.c_str());
        gpu_ = platform_.CreateGpu(platform::Renderer::D3D11, 1280, 960, false, 0, &error);
        Require(gpu_ != nullptr, error.c_str());
        Require(renderer_.Init(gpu_.get(), &error), error.c_str());
        Require(font_.Init() && font_.Available(), "native system font atlas unavailable");
        index_.open(output_ / "index.txt");
        index_ << "Native GPU UI fixture; synthetic devices, neutral backdrop; no hardware or gameplay proof.\n";
    }
    void Save(const std::string& name, const game::WorldHud& hud, int opacity = 65) {
        Require(hud.nightShiftPage >= 0 && !hud.menuText.empty(), "modern page missing panels/text");
        for (const auto& p : hud.menuPanels)
            Require(std::isfinite(p.left) && std::isfinite(p.top) && p.left >= 0 && p.top >= 0 && p.right <= 640 && p.bottom <= 480 &&
                        p.left < p.right && p.top < p.bottom,
                    "device panel exceeds logical canvas");
        for (const auto& text : hud.menuText) {
            Require(text.x >= 0 && text.x < 640 && text.y >= 0 && text.y < 468 && text.scale > 0, "device text anchor off canvas");
            std::vector<port::ModelRenderer::SpriteDraw> glyphs;
            font_.Append({text}, glyphs);
            for (const auto& glyph : glyphs)
                for (const auto& vertex : glyph.v)
                    if (vertex.x < 0 || vertex.x > 640 || vertex.y < 0 || vertex.y > 480) {
                        std::fprintf(stderr, "%s: text exceeds native canvas: %s\n", name.c_str(), text.text.c_str());
                        Require(false, "font geometry off canvas");
                    }
        }
        std::vector<port::ModelRenderer::SpriteDraw> sprites;
        play::AppendNightShiftPanels(hud, sprites, 4.f / 3.f, opacity);
        font_.Append(hud.menuText, sprites);
        const float background[] = {.24f, .27f, .31f, 1};
        Require(gpu_->BeginFrame(background), "begin offscreen UI frame");
        renderer_.DrawSprites(1280, 960, sprites);
        std::vector<uint8_t> pixels;
        Require(gpu_->ReadPixels(&pixels), "read native UI pixels");
        gpu_->EndFrame();
        Require(platform::Platform::SavePng((output_ / (name + ".png")).string(), pixels, 1280, 960), "save native UI PNG");
        index_ << name << ".png\t" << hud.menuText.size() << " text commands\t" << opacity << "% panels\n";
        ++captures_;
    }
    int Count() const { return captures_; }

   private:
    std::filesystem::path output_;
    platform::Platform platform_;
    std::unique_ptr<port::GpuDevice> gpu_;
    port::ModelRenderer renderer_;
    port::UiFont font_;
    std::ofstream index_;
    int captures_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    Require(argc == 2, "usage: device_menu_render_test <output directory>");
    Renderer render(argv[1]);
    {
        WiiFixture f;
        render.Save("wii-overview", f.Hud());
        render.Save("wii-opacity-25", f.Hud(), 25);
        render.Save("wii-opacity-100", f.Hud(), 100);
        f.backend->snapshot.player[1].connected = false;
        f.backend->snapshot.player[1].reserved = true;
        f.backend->snapshot.scanning = true;
        f.backend->snapshot.message = "Scanning for remotes. Press SYNC or hold 1 and 2 on each remote.";
        f.remotes.Update();
        render.Save("wii-scanning-disconnected", f.Hud());
    }
    for (int p = 0; p < 2; ++p) {
        WiiFixture f;
        f.Player(p);
        render.Save("wii-player-" + std::to_string(p + 1), f.Hud());
    }
    {
        WiiFixture f;
        f.Player(0);
        f.Key(8);
        const input::wii::Point corners[] = {{.2, .2}, {.8, .2}, {.8, .8}, {.2, .8}};
        for (int target = 0; target < 4; ++target) {
            Require(f.menu.CalibrationStep() == target, "Wii calibration target state");
            render.Save("wii-calibration-" + std::to_string(target + 1), f.Hud(), 100);
            auto& r = f.backend->snapshot.player[0];
            f.remotes.BeginFrame();
            r.buttons = r.pressed = 0;
            f.remotes.Update();
            f.menu.Update({}, f.remotes);
            r.raw = corners[target];
            r.buttons = r.pressed = input::wii::B;
            f.remotes.Update();
            f.menu.Update({}, f.remotes);
        }
        Require(f.menu.CalibrationStep() == -1, "Wii calibration completes");
        render.Save("wii-calibration-saved", f.Hud());
        f.backend->snapshot.player[0].connected = false;
        f.remotes.Update();
        f.Key(8);
        render.Save("wii-disconnected-notice", f.Hud());
    }
    {
        GunFixture f;
        render.Save("guns-overview", f.Hud());
        render.Save("guns-mouse-mode", f.Hud(false));
        f.Row(1);
        render.Save("guns-assignment", f.Hud());
    }
    for (int p = 0; p < 2; ++p) {
        GunFixture f;
        f.Row(3 + p);
        render.Save("guns-player-" + std::to_string(p + 1), f.Hud());
        f.Row(1);
        render.Save("guns-binding-player-" + std::to_string(p + 1), f.Hud());
    }
    {
        GunFixture f;
        f.Row(3);
        f.Row(0);
        const input::AimPoint corners[] = {{.2, .2}, {.8, .2}, {.8, .8}, {.2, .8}};
        for (int target = 0; target < 4; ++target) {
            Require(f.menu.CalibrationTarget() == target, "lightgun calibration target state");
            render.Save("guns-calibration-" + std::to_string(target + 1), f.Hud(), 100);
            Feed(f.guns, 0, corners[target].x, corners[target].y, 0, 1);
            f.guns.BeginFrame();
            f.menu.Update({}, f.guns, true);
            Feed(f.guns, 0, corners[target].x, corners[target].y, 1);
            f.menu.Update({}, f.guns, true);
        }
        Require(f.menu.CalibrationTarget() == -1, "lightgun calibration completes");
        Feed(f.guns, 0, .95, .95, 0, 1);
        f.guns.BeginFrame();
        render.Save("guns-calibration-saved", f.Hud());
        f.Row(5);
        Feed(f.guns, 0, .25, .45, 1);
        Feed(f.guns, 1, .75, .55, 1);
        f.menu.Update({}, f.guns, true);
        Require(f.menu.TestShots(0) == 1 && f.menu.TestShots(1) == 1, "two-player test counts");
        render.Save("guns-test-both", f.Hud(), 100);
        f.guns.BeginFrame();
        Feed(f.guns, 1, -.1, .5, 0, 1, false);
        f.guns.RemoveDevice(ids[0]);
        render.Save("guns-test-offscreen-disconnected", f.Hud(), 100);
    }
    {
        GunFixture f;
        f.Row(3);
        f.guns.RemoveDevice(ids[0]);
        f.Row(0);
        render.Save("guns-disconnected-notice", f.Hud());
        f.menu.Notice("Could not save input mode - check folder access");
        render.Save("guns-save-error", f.Hud());
    }
    std::printf("PASS %d native device-menu captures; canvas bounds, four-target progression and two-player test counts\n", render.Count());
}
