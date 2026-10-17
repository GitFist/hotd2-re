// Closely layered geometry must not change visibility when a scene is translated far from
// the origin. Reproduces the depth cancellation behind the Emperor rooftop's flickering.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "game/camera.h"
#include "render/model_renderer.h"
#include "render/d3d11_device.h"
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif

static void Require(bool ok, const char* why) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

static game::Model Layer(float depth, bool front) {
    game::Model model;
    game::ModelMesh mesh;
    auto& words = mesh.material.words;
    words[1] = 4u << 29; // LESSEQUAL, depth write
    words[2] = 0x80000; // opaque
    words[8] = 0xffffffff; // no assets needed
    const float color[4] = {1, front ? 0.f : 1.f, front ? 1.f : 0.f, 0}; // A,R,G,B
    std::memcpy(words + 11, color, sizeof(color));
    game::ModelStrip strip;
    for (auto p : {std::pair<float,float>{-180.f,-120.f}, {180.f,-120.f}, {-180.f,120.f}, {180.f,120.f}}) {
        game::ModelVertex v{};
        v.pos[0] = p.first; v.pos[1] = p.second;
        v.pos[2] = -400 + p.second * 1.5f + depth;
        v.normal[2] = 1;
        strip.vertices.push_back(v);
    }
    mesh.strips.push_back(strip);
    model.meshes.push_back(mesh);
    return model;
}

int main(int argc, char** argv) {
    std::string error;
    port::GpuDeviceDesc desc;
    std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_SDL_GPU
    if (argc > 1 && std::strcmp(argv[1], "sdlgpu") == 0) {
        Require(SDL_InitSubSystem(SDL_INIT_VIDEO), SDL_GetError());
        gpu = port::CreateSdlGpuDevice(desc, &error);
    } else
#endif
        gpu = port::CreateD3D11Device(desc, &error);
    Require(gpu != nullptr, error.c_str());
    port::ModelRenderer renderer;
    Require(renderer.Init(gpu.get(), &error), error.c_str());
    port::AssetStore assets;
    auto front = Layer(0.01f, true), back = Layer(0, false);
    std::vector<port::ModelRenderer::SceneItem> items(2);
    items[0].model = &front; items[1].model = &back;
    for (auto& item : items) { item.cullable = false; item.noMeshCull = true; }
    const auto projection = game::DeviceView() * game::DeviceProjection();
    port::ModelRenderer::Lighting light;
    light.enabled = true; light.dirToLight[1] = 0; light.dirToLight[2] = 1;
    auto render = [&](float origin, float pan, bool withSprites = false) {
        for (auto& item : items) item.transform[14] = origin;
        renderer.SetScene(assets, items);
        auto view = game::Mat4::Identity();
        view.m[12] = -pan; view.m[14] = -origin;
        auto vp = view * projection;
        const float clear[4] = {0,0,0,1};
        Require(gpu->BeginFrame(clear), "begin frame");
        renderer.DrawGame(640,480,vp.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
        if (withSprites) {
            std::vector<port::ModelRenderer::SpriteDraw> sprites(2);
            for (int i = 0; i < 2; ++i) {
                auto& sprite = sprites[i];
                sprite.isp = 4u << 29; sprite.tsp = 0x80000;
                const float x = 260.f + i * 80.f, z = i == 0 ? 0.999f : 0.5f;
                sprite.v[0] = {x,220,z,1,0xff0000ff,0,0,0};
                sprite.v[1] = {x+40,220,z,1,0xff0000ff,0,0,0};
                sprite.v[2] = {x,260,z,1,0xff0000ff,0,0,0};
                sprite.v[3] = {x+40,260,z,1,0xff0000ff,0,0,0};
            }
            renderer.DrawSprites(640,480,sprites);
        }
        std::vector<uint8_t> pixels;
        Require(gpu->ReadPixels(&pixels), "read pixels");
        gpu->EndFrame();
        return pixels;
    };
    size_t changed = 0;
    for (int frame = 0; frame < 12; ++frame) {
        const float pan = frame * 0.125f;
        const auto reference = render(0, pan);
        size_t visible = 0, bleeding = 0;
        for (size_t i = 0; i < reference.size(); i += 4) {
            visible += reference[i + 1] > 200 && reference[i] < 10;
            bleeding += reference[i] > 200 && reference[i + 1] < 10;
        }
        Require(visible > 50000, "front layer is visible in reference");
        if (bleeding >= 128) fprintf(stderr, "%zu rear-surface pixels bleed through frame %d\n", bleeding, frame);
        Require(bleeding < 128, "rear surface bleeds through the front layer");
        for (float origin : {-10000.f, 10000.f}) {
            const auto distant = render(origin, pan);
            Require(distant.size() == reference.size(), "matching image size");
            for (size_t i = 0; i < reference.size(); i += 4)
                changed += std::memcmp(reference.data() + i, distant.data() + i, 3) != 0;
        }
    }
    printf("%s: %zu pixels changed across 24 translated views\n", gpu->Name(), changed);
    Require(changed == 0, "world translation changed surface visibility");
    const auto mixed = render(-10000, 0, true);
    const size_t behind = (240 * 640 + 280) * 4, ahead = (240 * 640 + 360) * 4;
    Require(mixed[behind + 1] > 200 && mixed[behind + 2] < 10, "model occludes the rear sprite");
    Require(mixed[ahead + 2] > 200 && mixed[ahead + 1] < 10, "front sprite occludes the model");
    puts("PASS layered geometry remains stable through camera movement");
    puts("PASS model and sprite depth ordering");
}
