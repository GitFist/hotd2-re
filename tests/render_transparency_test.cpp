// Smoke plumes are overlapping alpha cards authored front first. Their rear layers
// must remain visible with precise depth, and solid scenery must still occlude them.
#include <cmath>
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
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

static game::ModelMesh Card(float depth, float r, float g, float b, bool opaque = false, float y = 0) {
    game::ModelMesh mesh;
    auto& w = mesh.material.words;
    w[1] = 4u << 29; // LESSEQUAL, depth write: the original smoke material.
    w[2] = opaque ? 0x80000u : 0x94100000u; // SRCALPHA / INVSRCALPHA.
    w[8] = 0xffffffffu;
    w[9] = 0xffffffffu;
    const float center[4] = {0, y, depth, 500};
    const float material[5] = {1, opaque ? 1.f : 0.5f, r, g, b};
    std::memcpy(w + 4, center, sizeof(center));
    std::memcpy(w + 10, material, sizeof(material));
    game::ModelStrip strip;
    for (auto p : {std::pair<float, float>{-400, -300}, {-400, 300}, {400, -300}, {400, 300}}) {
        game::ModelVertex v{};
        v.pos[0] = p.first; v.pos[1] = p.second + y; v.pos[2] = depth;
        v.normal[2] = 1;
        strip.vertices.push_back(v);
    }
    mesh.strips.push_back(strip);
    return mesh;
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
    game::Model frontFirst, rearFirst, scenery;
    frontFirst.meshes = {Card(-2283, 1, 0, 0, false, 100), Card(-2286, 0, 0, 1)};
    rearFirst.meshes = {frontFirst.meshes[1], frontFirst.meshes[0]};
    scenery.meshes = {Card(-2200, 0, 1, 0, true)};
    port::ModelRenderer::Lighting light;
    light.enabled = true;
    for (int c = 0; c < 3; ++c) { light.ambient[c] = 1; light.diffuse[c] = 0; }
    const auto projection = game::DeviceView() * game::DeviceProjection();
    int cases = 0;
    for (auto* model : {&frontFirst, &rearFirst})
        for (float origin : {0.f, -10000.f, 10000.f})
            for (int pitch : {-512, 0, 512})
            for (bool occluded : {false, true}) {
                std::vector<port::ModelRenderer::SceneItem> items(occluded ? 2 : 1);
                items[0].model = model;
                if (occluded) items[1].model = &scenery;
                for (auto& item : items) {
                    item.cullable = false; item.noMeshCull = true; item.transform[14] = origin;
                }
                renderer.SetScene(assets, items);
                const auto view = game::Mat4::Translate(0, 0, -origin) * game::Mat4::RotateX(pitch);
                const auto vp = view * projection;
                const float clear[4] = {0, 0, 0, 1};
                Require(gpu->BeginFrame(clear), "begin frame");
                renderer.DrawGame(640, 480, vp.m, &light, nullptr, view.m, nullptr, nullptr, projection.m);
                std::vector<uint8_t> pixels;
                Require(gpu->ReadPixels(&pixels), "read pixels");
                gpu->EndFrame();
                const auto* rgb = pixels.data() + (240 * 640 + 320) * 4;
                const int expected[3] = {occluded ? 0 : 128, occluded ? 255 : 0, occluded ? 0 : 64};
                for (int c = 0; c < 3; ++c) {
                    if (std::abs(static_cast<int>(rgb[c]) - expected[c]) > 1)
                        std::fprintf(stderr, "%s: observed RGB %u %u %u; expected %d %d %d\n",
                                     gpu->Name(), rgb[0], rgb[1], rgb[2], expected[0], expected[1], expected[2]);
                    Require(std::abs(static_cast<int>(rgb[c]) - expected[c]) <= 1,
                            "transparent layers blend completely and respect opaque depth");
                }
                ++cases;
            }
    std::printf("PASS %s: %d layered transparency cases\n", gpu->Name(), cases);
}
