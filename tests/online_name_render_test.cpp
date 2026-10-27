// Native-font/GPU captures from actual World menu events and local fixture records.
// No network requests or save writes; original assets supply the World's tables.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "game/world.h"
#include "platform/platform.h"
#include "play/night_shift_menu.h"
#include "play/username_prompt.h"
#include "render/ui_font.h"

namespace {
constexpr int kWidth = 1280, kHeight = 960;
void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
void Tick(game::World& world, uint32_t buttons = 0, bool click = false, int x = 0, int y = 0) {
    game::WorldInput input; input.kind = click ? 1 : 0; input.pressed = input.held = buttons;
    input.fired = click; input.x = x; input.y = y;
    world.SetInput(0, input); world.SetInput(1, {}); world.Step();
}
void Click(game::World& world, int x, int y) { Tick(world, 4, true, x, y); Tick(world); }
struct Bounds { float left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY; };

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
        report_.open(output_ / "index.txt");
        report_ << "Actual World menu/HUD captures; neutral backdrop, native fonts, local synthetic records; no network or save writes.\n";
    }
    Bounds Measure(const game::WorldHud::MenuText& text) const {
        std::vector<port::ModelRenderer::SpriteDraw> glyphs;
        font_.Append({text}, glyphs);
        Bounds bounds;
        for (const auto& glyph : glyphs) for (const auto& v : glyph.v) {
            bounds.left = std::min(bounds.left, v.x); bounds.top = std::min(bounds.top, v.y);
            bounds.right = std::max(bounds.right, v.x); bounds.bottom = std::max(bounds.bottom, v.y);
        }
        return bounds;
    }
    void Save(const std::string& name, const game::WorldHud& hud, bool board = false, bool nameField = true) {
        Require(hud.nightShiftPage >= 0 && !hud.menuText.empty(), "actual menu HUD missing");
        for (const auto& text : hud.menuText) {
            const auto b = Measure(text);
            if (b.left < 0 || b.top < 0 || b.right > 640 || b.bottom > 480) {
                std::fprintf(stderr, "%s: text outside canvas: %s [%.1f %.1f %.1f %.1f]\n", name.c_str(), text.text.c_str(), b.left, b.top, b.right, b.bottom);
                Require(false, "menu text must stay inside canvas");
            }
        }
        std::vector<port::ModelRenderer::SpriteDraw> sprites;
        play::AppendNightShiftPanels(hud, sprites, 4.f / 3.f, 65);
        font_.Append(hud.menuText, sprites);
        const float clear[] = {.22f, .28f, .32f, 1};
        Require(gpu_->BeginFrame(clear), "begin native name capture");
        renderer_.DrawSprites(kWidth, kHeight, sprites);
        std::vector<uint8_t> pixels;
        Require(gpu_->ReadPixels(&pixels), "read native name capture"); gpu_->EndFrame();
        Require(platform::Platform::SavePng((output_ / (name + ".png")).string(), pixels, kWidth, kHeight), "save native name capture");
        report_ << name << ".png\t" << hud.menuText.size() << " text commands\n"; report_.flush();
        if (board) {
            for (int row = -1; row < 10; ++row) {
                const float y = row < 0 ? 152.f : 181.f + row * 23;
                std::vector<const game::WorldHud::MenuText*> columns;
                for (const auto& text : hud.menuText) if (text.y == y && text.x >= 218) columns.push_back(&text);
                std::sort(columns.begin(), columns.end(), [](const auto* a, const auto* b) { return a->x < b->x; });
                Require(columns.size() >= 5, "shared leaderboard lost a data column");
                for (size_t i = 0; i + 1 < columns.size(); ++i) {
                    const auto a = Measure(*columns[i]), b = Measure(*columns[i + 1]);
                    if (a.right + 2 > b.left) {
                        std::fprintf(stderr, "%s row %d: '%s' ends %.2f; '%s' starts %.2f\n", name.c_str(), row,
                                     columns[i]->text.c_str(), a.right, columns[i + 1]->text.c_str(), b.left);
                        Require(false, "leaderboard glyphs and shadows must leave a gap between columns");
                    }
                    Require(a.bottom < y + 23, "leaderboard glyphs must not overlap the next row");
                }
            }
        } else if (nameField) {
            bool field = false;
            for (const auto& text : hud.menuText) if (text.x == 80 && text.y == 136) {
                const auto b = Measure(text); field = true;
                Require(b.left >= 64 && b.top >= 128 && b.right <= 576 && b.bottom <= 168, "name or placeholder exceeds its input field");
            }
            Require(field, "username dialog missing editable name field");
        }
        ++captures_;
    }
    int Count() const { return captures_; }
private:
    std::filesystem::path output_;
    platform::Platform platform_;
    std::unique_ptr<port::GpuDevice> gpu_;
    port::ModelRenderer renderer_;
    port::UiFont font_;
    std::ofstream report_;
    int captures_ = 0;
};
} // namespace

int main(int argc, char** argv) {
    Require(argc == 3, "usage: online_name_render_test <game directory> <output directory>");
    Fixture render(argv[2]);
    game::World world; Require(world.Load(argv[1]), "load actual game tables for menu fixture");
    game::WorldConfig config; config.portFeatures = config.portModernMenu = true; config.savePath.clear();
    world.Boot(config); world.ForceMode(4, 0x10000, 0, 0);
    for (int i = 0; i < 3; ++i) Tick(world);
    Click(world, 100, 260); Require(world.Hud().nightShiftPage == 3, "Home event opens leaderboard");
    world.SetPortLeaderboardPublishable(false, "SCORE SAVED / WILL SYNC WHEN ONLINE"); Tick(world);
    render.Save("leaderboard-offline-status", world.Hud(), false, false);
    Require(world.Hud().portLeaderboardPublishRequests == 0, "capture fixture must never request publication");
    Click(world, 100, 145); Require(world.Hud().portLeaderboardShared, "source button selects shared view");
    for (int table : {0, 2, 3}) {
        if (table == 2) Click(world, 100, 270);
        if (table == 3) Click(world, 100, 310);
        Require(world.Hud().portLeaderboardTable == table, "board selection follows actual menu event");
        std::vector<game::SharedLeaderboardEntry> entries;
        for (int row = 0; row < 10; ++row)
            entries.push_back({"WWWWWWWWWWWW", table >= 3 ? 1296000 : 2000000000, table == 2 ? 65535 : 0,
                               table < 2 ? 6 : 4, table >= 3 ? 1 : 2});
        world.SetPortSharedLeaderboard(table, entries, "LOCAL TEST FIXTURE"); Tick(world);
        int names = 0; for (const auto& text : world.Hud().menuText) if (text.text == "WWWWWWWWWWWW") ++names;
        Require(names == 10, "all ten full names remain visible without abbreviation");
        render.Save(table == 0 ? "shared-arcade-max" : table == 2 ? "shared-horde-max" : "shared-boss-max", world.Hud(), true);
    }
    play::UsernamePrompt prompt;
    prompt.Open({},true);
    game::WorldHud modal; prompt.ModernHud(modal);render.Save("username-first-launch-empty",modal);
    prompt.Edit("WWWWWWWWWWWW");
    prompt.ModernHud(modal);render.Save("username-first-launch-full",modal);
    play::MenuInput save; save.pressed=8;
    Require(prompt.Update(save)==play::UsernamePrompt::Action::Save,"filled name can be saved");
    prompt.SetError("COULD NOT SAVE USERNAME. PLEASE RETRY.");
    prompt.ModernHud(modal);render.Save("username-save-error",modal);
    prompt.Open("NIGHTSHIFT12",false);
    prompt.ModernHud(modal);render.Save("username-change",modal);
    std::printf("PASS %d native captures: first-launch and rename dialogs, save errors, offline status, maximum leaderboard values and glyph bounds\n",render.Count());
}
