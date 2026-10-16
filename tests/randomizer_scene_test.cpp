#include <cstdio>
#include <cstdlib>
#include <set>
#include <exception>
#include "game/world.h"
#include "game/world_modes.h"
#include "assets/asset_override.h"
#include "scene/scene.h"
#include "render/d3d11_device.h"
#include <fstream>

static void Require(bool ok, const char* why) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static void Input(game::World& w, uint32_t buttons = 0) {
    game::WorldInput in; in.pressed = in.held = buttons;
    w.SetInput(0, in); w.SetInput(1, {});
}
int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) return 2;
    std::set_terminate([] {
        try { if (auto e = std::current_exception()) std::rethrow_exception(e); }
        catch (const std::exception& e) { fprintf(stderr, "Unhandled exception: %s\n", e.what()); }
        std::_Exit(3);
    });
    const bool keepCache = std::getenv("HOTD2_TEST_KEEP_CACHE") != nullptr; // reproduce the previous memory failure
    port::SetAssetDir(argv[2]);
    port::AssetStore assets;
    std::string error;
    Require(assets.Open(argv[1], &error), "open assets");
    port::GpuDeviceDesc desc;
    auto gpu = port::CreateD3D11Device(desc, &error);
    Require(gpu != nullptr, error.c_str());
    port::ModelRenderer renderer;
    Require(renderer.Init(gpu.get(), &error), "initialize renderer");
    port::Scene scene; port::SceneDef def; def.useWorld = def.externalWorld = true;
    Require(scene.Init(assets, def, &error), "initialize scene");
    game::World world;
    Require(world.Load(argv[1]), "load world");
    game::WorldConfig cfg;
    cfg.portFeatures = cfg.portModernMenu = cfg.portNetworkRoom = true;
    cfg.gameMode = 6; cfg.gameOver = true;
    cfg.startFrame = 901; cfg.seedCounter = argc == 5 ? std::atoi(argv[4]) : 12345; cfg.creditSetting = -1;
    Require(world.StartNetworkGame(cfg), "start room");
    for (int i = 0; i < 5; ++i) { Input(world, 0x20); world.Step(); Input(world); world.Step(); }
    Input(world, 4); world.Step(); Input(world); world.Step();
    Require(world.Hud().randomizerSeed != 0, "Randomizer selected");
    cfg.portNetworkRoom = false; cfg.gameType = 1; cfg.freePlay = true;
    cfg.playerState[0] = cfg.playerState[1] = 0;
    std::set<int> prefetched;
    bool deskCostume = false, deskVoice = false;
    for (int stage = argc == 5 ? 2 : 0; stage < (argc == 5 ? 3 : 6); ++stage) {
        cfg.eventMode = stage; cfg.scene = game::SceneSelectFirstScene(stage);
        cfg.startFrame = world.Frame() + 1; cfg.seedCounter = cfg.startFrame;
        Require(world.StartStage(cfg), "start stage");
        const int frames = argc == 5 ? 650 : stage == 2 ? 1800 : 1200;
        for (int frame = 0; frame < frames; ++frame) {
            for (const auto& note : world.Notes()) {
                deskCostume |= note.find("randomizer: costume 61 -> ") == 0;
                if (stage == 2) deskVoice |= note.find("randomizer: dialogue ") == 0;
            }
            if (!keepCache && scene.PrepareWorld(renderer, world)) {
                Require(renderer.TextureCount() == 0, "old GPU handles retired before assets");
                prefetched.clear();
            }
            for (int file : world.Hud().polFileLoads) {
                const std::string pol = assets.Exe().CStr(assets.Exe().U32(0x004D0EF4 + file * 4));
                if (!pol.empty()) assets.PrefetchPolFile(pol);
            }
            for (size_t file = 0; file < world.Hud().texFiles.size(); ++file)
                if (world.Hud().texFiles[file] && prefetched.insert(static_cast<int>(file)).second)
                    assets.PrefetchFile(static_cast<int>(file));
            for (auto* img : assets.DrainPrefetched(4)) renderer.WarmTexture(img);
            auto vp = scene.BuildFromWorld(renderer, world);
            auto sprites = scene.MakeSpriteDraws(world.Hud().sprites);
            const float clear[4] = {0,0,0,1};
            gpu->BeginFrame(clear);
            renderer.DrawGame(640,480,vp.m,&scene.lighting,&scene.fog,scene.view.m,&sprites,&scene.itemLights);
            if (stage == 2 && frame == 200) {
                std::vector<uint8_t> rgba;
                Require(gpu->ReadPixels(&rgba), "read desk frame");
                std::ofstream out(argv[3], std::ios::binary);
                out << "P6\n640 480\n255\n";
                for (size_t i = 0; i < rgba.size(); i += 4) out.write(reinterpret_cast<const char*>(rgba.data() + i), 3);
                Require(out.good(), "write desk frame");
            }
            gpu->EndFrame();
            if (frame % 300 == 0) { printf("stage %d frame %d textures %zu\n",stage+1,frame,renderer.TextureCount()); fflush(stdout); }
            Input(world); world.Step();
        }
    }
    Require(deskCostume && deskVoice, "Goldman's desk scene runs with randomized actor and voice");
    puts("PASS rendered chapter transition and desk conversation");
}
