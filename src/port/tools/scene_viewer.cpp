// scene_viewer.exe <game dir> <scene file> [--time T] [--screenshot out.png] [--width W --height H] [--simple] [--lit] [--nofog] [--no2d] [--frame F] [--audio]
//                  [--renderer d3d11|sdlgpu] [--audio-backend sdl|xaudio2]
//
// Plays a scene (scenes/*.scene) with the port's camera path evaluation and the game's view/projection
// conventions. --screenshot renders camera time T off-screen and exits (used to compare with screenshots of the
// original captured by HOTD2_SCENECAP_SHOTS). Interactive: plays at 60 frames per second and loops;
// Space pauses, Left/Right step one frame (Shift: 30 frames), Esc quits.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "game/exe_image.h"
#include "game/sound_driver.h"
#include "game/sound_table.h"
#include "game/world_sound.h"
#include "platform/platform.h"
#include "render/asset_store.h"
#include "render/model_renderer.h"
#include "scene/scene.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: scene_viewer <game dir> <scene file> [--time T] [--screenshot out.png] [--width W --height H] [--lit] %s\n",
                platform::CommonUsage());
        return 2;
    }
    float time = 0;
    bool paused = false;
    std::string screenshot;
    int width = 640, height = 480;
    bool lit = false, simple = false, nofog = false, no2d = false, dumpPass1 = false, audio = false, dumpSounds = false;
    int frame = -1;  // game frame for 2D timing (PRESS START blink); default: the camera time
    platform::CommonOptions common;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (platform::ParseCommonArg(argc, argv, i, &common)) continue;
        if (a == "--time" && i + 1 < argc) time = static_cast<float>(atof(argv[++i]));
        else if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        else if (a == "--width" && i + 1 < argc) width = atoi(argv[++i]);
        else if (a == "--height" && i + 1 < argc) height = atoi(argv[++i]);
        else if (a == "--lit") lit = true;
        else if (a == "--nofog") nofog = true;  // scene fog off (comparison runs)
        else if (a == "--no2d") no2d = true;    // no sprite overlays
        else if (a == "--dump-pass1") dumpPass1 = true;  // print the pass-1 list like the capture's P/p lines
        else if (a == "--audio") audio = true;  // interactive: play the world's sound commands
        else if (a == "--dump-sounds") dumpSounds = true;  // print the world's sound ids up to --frame
        else if (a == "--frame" && i + 1 < argc) frame = atoi(argv[++i]);
        else if (a == "--simple") simple = true;  // no game material state (opaque, alpha test 0.5, point sampling)
    }
    static port::AssetStore assets;
    static port::ModelRenderer renderer;
    port::SceneDef def;
    port::Scene scene;
    std::string err;
    if (!assets.Open(argv[1], &err) || !def.Load(argv[2], &err) || !scene.Init(assets, def, &err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    const float clear[4] = { 0, 0, 0, 1 };
    const bool headless = !screenshot.empty();
    platform::Platform plat;
    platform::Options po;
    po.title = "hotd2-re scene viewer";
    po.width = width;
    po.height = height;
    po.window = !headless;
    if (!plat.Init(po, &err)) {
        fprintf(stderr, "platform: %s\n", err.c_str());
        return 1;
    }
    std::unique_ptr<port::GpuDevice> gpu = plat.CreateGpu(common.renderer, width, height, false, 1, &err);  // vsync: 60 Hz
    if (!gpu || !renderer.Init(gpu.get(), &err)) {
        fprintf(stderr, "device/renderer init failed %s\n", err.c_str());
        return 1;
    }

    if (headless) {
        const game::Mat4 vp = scene.Build(renderer, time, frame);
        if (dumpSounds) {
            printf("sounds");
            for (const auto& s : scene.TakeSounds()) printf(" %s%08X", s.kind ? (s.kind == 1 ? "stopall:" : s.kind == 2 ? "cmd:" : "slot:") : "", s.id);
            printf("\n");
            printf("skinned draws %lld alt draws %lld (last frame only)\n", scene.skinnedDraws_, scene.altDraws_);
        }
        gpu->BeginFrame(clear);
        if (simple) renderer.DrawWithMatrix(width, height, vp.m, !lit);
        else {
            const auto sprites = no2d ? std::vector<port::ModelRenderer::SpriteDraw>() : scene.BuildSprites(frame >= 0 ? frame : static_cast<int>(time));
            renderer.DrawGame(width, height, vp.m, &scene.lighting, nofog ? nullptr : &scene.fog, scene.view.m, &sprites, &scene.itemLights);
            if (dumpPass1) {
                printf("pass1 %zu", renderer.lastPass1.size());
                for (const auto& e : renderer.lastPass1) {
                    if (e.item >= 0) printf(" M:%d:%08X:%.7g", scene.ItemModelId(e.item), e.key, e.depth);
                    else printf(" S:%X:%08X:%.7g", sprites[e.sprite].textureId, e.key, e.depth);
                }
                printf("\n");
            }
        }
        std::vector<uint8_t> pixels;
        if (!gpu->ReadPixels(&pixels) || !platform::Platform::SavePng(screenshot, pixels, gpu->TargetWidth(), gpu->TargetHeight())) return 1;
        const auto& st = renderer.SceneStats();
        printf("%s: camera %d t=%g pos (%.3f %.3f %.3f) pitch %d yaw %d; %zu triangles, meshes textured %zu, missing %zu\n",
               screenshot.c_str(), def.cameraId, time, scene.pos[0], scene.pos[1], scene.pos[2], scene.pitch, scene.yaw,
               st.triangles, st.texturedMeshes, st.missingTextures);
        return 0;
    }

    // Sound (opt-in): the world's Snd_PlayGameSound ids through the ported SoundDriver.
    game::ExeImage sndExe;
    game::SoundTable sndTable;
    std::unique_ptr<port::AudioOutput> sndBackend;
    std::unique_ptr<game::SoundDriver> sndDriver;
    if (audio && sndExe.Load(std::string(argv[1]) + "/Hod2.exe") && sndTable.Load(sndExe) && (sndBackend = plat.CreateAudio(common.audio, argv[1], &err)))
        sndDriver = std::make_unique<game::SoundDriver>(*sndBackend);

    for (bool quit = false; !quit;) {
        for (platform::Event ev; plat.PollEvent(&ev);) {
            if (ev.type == platform::Event::Type::Quit) quit = true;
            if (ev.type != platform::Event::Type::KeyDown) continue;
            const float step = ev.shift ? 30.0f : 1.0f;
            if (ev.key == platform::Key::Escape) quit = true;
            if (ev.key == platform::Key::Space) paused = !paused;
            if (ev.key == platform::Key::Right) time += step;
            if (ev.key == platform::Key::Left) time = time > step ? time - step : 0;
        }
        if (quit) break;
        if (time > scene.Duration()) time = 0;
        const game::Mat4 vp = scene.Build(renderer, time, frame);
        gpu->BeginFrame(clear);
        if (simple) renderer.DrawWithMatrix(width, height, vp.m, !lit);
        else {
            const auto sprites = no2d ? std::vector<port::ModelRenderer::SpriteDraw>() : scene.BuildSprites(frame >= 0 ? frame++ : static_cast<int>(time));
            renderer.DrawGame(width, height, vp.m, &scene.lighting, nofog ? nullptr : &scene.fog, scene.view.m, &sprites, &scene.itemLights);
        }
        if (sndDriver)
            for (const auto& s : scene.TakeSounds()) game::PlaySoundEvent(*sndDriver, sndTable, s);
        gpu->EndFrame();  // vsync: one game frame per refresh at 60 Hz
        if (!paused) time += 1;
    }
    return 0;
}
