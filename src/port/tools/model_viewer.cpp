// model_viewer.exe <game dir> <pol file> <model index|all> [--screenshot out.png] [--yaw Y] [--pitch P] [--size N]
// model_viewer.exe <game dir> --skeleton T --motion M [--tick K] [--screenshot ...]   (posed character)
//
// Renders HOTD2 models with the port's native loaders (src/game) and renderer (src/port/render; --renderer d3d11|sdlgpu).
// --screenshot renders off-screen, writes a PNG and exits (deterministic; used for verification).
// Interactive: drag with the left mouse button to orbit, wheel to zoom, PgUp/PgDn to step models,
// A to show all models of the file, Esc to quit. Character mode: Left/Right step ticks, Space plays the motion.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game/anim.h"
#include "platform/platform.h"
#include "render/asset_store.h"
#include "render/model_renderer.h"

namespace {

struct Options {
    platform::CommonOptions common;
    std::string gameDir, polFile, model = "all", screenshot;
    int skeleton = -1, motion = -1, tick = 0;
    float yaw = 35, pitch = 25;
    int size = 512;
};

struct Viewer {
    port::AssetStore assets;
    port::ModelRenderer renderer;
    std::vector<game::Model> models;
    std::string polName;
    int current = -1;  // -1 = all
    port::Camera cam;

    // Character mode
    bool character = false, playing = false;
    game::Skeleton skel;
    game::Motion motion;
    std::vector<game::Model> parts;
    std::vector<std::string> partPol;
    std::vector<int> partNode;
    int tick = 0;
    void ShowPose() {
        const auto mats = game::EvaluatePose(skel, motion, tick);
        std::vector<port::ModelRenderer::SceneItem> items;
        for (size_t i = 0; i < parts.size(); ++i) {
            port::ModelRenderer::SceneItem it;
            it.model = &parts[i];
            it.polName = partPol[i];
            memcpy(it.transform, mats[partNode[i]].m, sizeof(it.transform));
            items.push_back(it);
        }
        renderer.SetScene(assets, items);
    }

    void Show(int index) {
        current = index;
        std::vector<const game::Model*> pick;
        for (size_t i = 0; i < models.size(); ++i)
            if (index < 0 || static_cast<int>(i) == index) pick.push_back(&models[i]);
        renderer.SetScene(assets, pick, polName);
    }
};

bool ParseArgs(int argc, char** argv, Options& o) {
    if (argc < 3) return false;
    o.gameDir = argv[1];
    int i = 2;
    if (argv[2][0] != '-') {
        o.polFile = argv[i++];
        if (argc > i && argv[i][0] != '-') o.model = argv[i++];
    }
    for (; i < argc; ++i) {
        const std::string a = argv[i];
        if (platform::ParseCommonArg(argc, argv, i, &o.common)) continue;
        if (a == "--screenshot" && i + 1 < argc) o.screenshot = argv[++i];
        else if (a == "--yaw" && i + 1 < argc) o.yaw = static_cast<float>(atof(argv[++i]));
        else if (a == "--pitch" && i + 1 < argc) o.pitch = static_cast<float>(atof(argv[++i]));
        else if (a == "--size" && i + 1 < argc) o.size = atoi(argv[++i]);
        else if (a == "--skeleton" && i + 1 < argc) o.skeleton = atoi(argv[++i]);
        else if (a == "--motion" && i + 1 < argc) o.motion = atoi(argv[++i]);
        else if (a == "--tick" && i + 1 < argc) o.tick = atoi(argv[++i]);
        else return false;
    }
    return !o.polFile.empty() || (o.skeleton >= 0 && o.motion >= 0);
}

// Loads skeleton, motion and the part model of every node. Returns false with a message on failure.
bool LoadCharacter(Viewer& v, const Options& o, std::string& err) {
    const game::ExeImage& exe = v.assets.Exe();
    if (!game::LoadSkeleton(exe, o.skeleton, v.skel)) { err = "cannot load skeleton"; return false; }
    for (const auto& f : game::ListMotionFiles(exe)) {
        for (const auto& m : game::SplitMotionFile(exe, f, v.assets.File("mot/" + f.name), v.skel.nodeCount))
            if (m.id == o.motion) v.motion = m;
        if (v.motion.id == o.motion) break;
    }
    if (v.motion.id != o.motion) { err = "motion not found"; return false; }
    if (!v.motion.SizeConsistent()) { err = "motion size does not match the skeleton's node count"; return false; }
    for (size_t i = 0; i < v.skel.nodes.size(); ++i) {
        if (!v.skel.nodes[i].modelId) continue;
        game::Model m;
        std::string pol;
        if (!v.assets.ModelById(v.skel.nodes[i].modelId, m, &pol)) continue;
        v.parts.push_back(std::move(m));
        v.partPol.push_back(pol);
        v.partNode.push_back(static_cast<int>(i));
    }
    v.character = true;
    v.tick = o.tick & ~1;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!ParseArgs(argc, argv, opt)) {
        fprintf(stderr, "usage: model_viewer <game dir> <pol file> [model|all] [options]\n"
                        "       model_viewer <game dir> --skeleton T --motion M [--tick K] [options]\n"
                        "options: --screenshot out.png --yaw Y --pitch P --size N %s\n", platform::CommonUsage());
        return 2;
    }
    static Viewer viewer;
    std::string err;
    if (!viewer.assets.Open(opt.gameDir, &err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    if (opt.skeleton >= 0) {
        if (!LoadCharacter(viewer, opt, err)) {
            fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
    } else {
        int failures = 0;
        viewer.models = viewer.assets.LoadPolFile(opt.polFile, &failures);
        if (viewer.models.empty()) {
            fprintf(stderr, "no models in pol/%s (parse failures: %d)\n", opt.polFile.c_str(), failures);
            return 1;
        }
        viewer.polName = opt.polFile;
    }
    viewer.cam.yawDeg = opt.yaw;
    viewer.cam.pitchDeg = opt.pitch;
    const int pick = opt.model == "all" ? -1 : atoi(opt.model.c_str());

    const float clear[4] = { 0.18f, 0.2f, 0.26f, 1 };
    const bool headless = !opt.screenshot.empty();
    platform::Platform plat;
    platform::Options po;
    po.title = "hotd2-re model viewer";
    po.width = po.height = opt.size;
    po.window = !headless;
    if (!plat.Init(po, &err)) {
        fprintf(stderr, "platform: %s\n", err.c_str());
        return 1;
    }
    // Interactive: the back buffer follows the window size; vsync (one animation tick per presented frame).
    std::unique_ptr<port::GpuDevice> gpu = plat.CreateGpu(opt.common.renderer, opt.size, opt.size, !headless, 1, &err);
    if (!gpu || !viewer.renderer.Init(gpu.get(), &err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    if (viewer.character) viewer.ShowPose();
    else viewer.Show(pick);

    if (headless) {
        gpu->BeginFrame(clear);
        viewer.renderer.Draw(opt.size, opt.size, viewer.cam);
        std::vector<uint8_t> pixels;
        if (!gpu->ReadPixels(&pixels) || !platform::Platform::SavePng(opt.screenshot, pixels, gpu->TargetWidth(), gpu->TargetHeight())) {
            fprintf(stderr, "failed to write %s\n", opt.screenshot.c_str());
            return 1;
        }
        const auto& st = viewer.renderer.SceneStats();
        printf("%s: %zu triangles; meshes textured %zu, untextured %zu, texture missing %zu\n", opt.screenshot.c_str(),
               st.triangles, st.texturedMeshes, st.meshes - st.texturedMeshes - st.missingTextures, st.missingTextures);
        return 0;
    }

    bool dragging = false, quit = false;
    float dragX = 0, dragY = 0;
    while (!quit) {
        for (platform::Event ev; plat.PollEvent(&ev);) {
            using T = platform::Event::Type;
            using K = platform::Key;
            switch (ev.type) {
            case T::Quit: quit = true; break;
            case T::MouseDown:
                if (ev.button == 1) {
                    dragging = true;
                    dragX = ev.x;
                    dragY = ev.y;
                }
                break;
            case T::MouseUp:
                if (ev.button == 1) dragging = false;
                break;
            case T::MouseMove:
                if (dragging) {
                    viewer.cam.yawDeg += (ev.x - dragX) * 0.5f;
                    viewer.cam.pitchDeg += (ev.y - dragY) * 0.5f;
                    dragX = ev.x;
                    dragY = ev.y;
                }
                break;
            case T::MouseWheel:
                if (ev.wheel != 0) viewer.cam.distanceScale *= ev.wheel > 0 ? 0.9f : 1.1f;
                break;
            case T::KeyDown:
                if (ev.key == K::Escape) quit = true;
                if (!viewer.character && (ev.key == K::PageDown || ev.key == K::PageUp)) {
                    const int n = static_cast<int>(viewer.models.size());
                    int c = viewer.current < 0 ? 0 : viewer.current + (ev.key == K::PageDown ? 1 : -1);
                    viewer.Show(((c % n) + n) % n);
                }
                if (ev.key == K::A && !viewer.character) viewer.Show(-1);
                if (viewer.character && (ev.key == K::Left || ev.key == K::Right)) {
                    const int len = viewer.motion.ticks;
                    viewer.tick = ((viewer.tick + (ev.key == K::Right ? 2 : -2)) % len + len) % len & ~1;
                    viewer.ShowPose();
                }
                if (viewer.character && ev.key == K::Space) viewer.playing = !viewer.playing;
                break;
            default: break;
            }
        }
        if (quit) break;
        if (viewer.character && viewer.playing) {  // one engine tick per presented frame (60 Hz vsync)
            viewer.tick = (viewer.tick + 2) % viewer.motion.ticks & ~1;
            viewer.ShowPose();
        }
        gpu->BeginFrame(clear);
        viewer.renderer.Draw(gpu->TargetWidth(), gpu->TargetHeight(), viewer.cam);
        gpu->EndFrame();
    }
    return 0;
}