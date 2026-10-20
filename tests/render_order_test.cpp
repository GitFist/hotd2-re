// Blinking HUD sprites must not reorder transparent surfaces with equal depth.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "game/camera.h"
#include "game/crt_qsort.h"
#include "render/model_renderer.h"
#include "render/d3d11_device.h"
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif

static void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

static game::Model Surface(bool red) {
    game::Model model;
    game::ModelMesh mesh;
    auto& w = mesh.material.words;
    w[1] = (4u << 29) | (1u << 26); // LESSEQUAL, no depth writes.
    w[2] = 0x94100000u; // SRCALPHA / INVSRCALPHA.
    w[8] = w[9] = 0xffffffffu;
    const float material[5] = {1, .5f, red ? 1.f : 0.f, 0, red ? 0.f : 1.f};
    std::memcpy(w + 10, material, sizeof(material));
    game::ModelStrip strip;
    for (auto p : {std::pair<float,float>{-120.f,-100.f}, {-120.f,100.f}, {120.f,-100.f}, {120.f,100.f}}) {
        game::ModelVertex v{};
        v.pos[0] = p.first; v.pos[1] = p.second; v.pos[2] = -400; v.normal[2] = 1;
        strip.vertices.push_back(v);
    }
    mesh.strips.push_back(strip); model.meshes.push_back(mesh);
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
    auto red = Surface(true), blue = Surface(false), green = Surface(false);
    const float greenRgb[3] = {0,1,0};
    std::memcpy(green.meshes[0].material.words + 12, greenRgb, sizeof(greenRgb));
    std::vector<port::ModelRenderer::SceneItem> items(7);
    items[1].model = &red; items[3].model = &blue; items[5].model = &green;
    for (int i : {0,2,4,6}) {
        items[i].model = &red;
        items[i].transform[12] = 10000;
        items[i].transform[14] = (i - 3) * 10.f;
    }
    for (auto& item : items) { item.cullable = false; item.noMeshCull = true; }
    port::ModelRenderer::Lighting light;
    light.enabled = true;
    for (int c = 0; c < 3; ++c) { light.ambient[c] = 1; light.diffuse[c] = 0; }
    const auto view = game::Mat4::Identity();
    const auto projection = game::DeviceView() * game::DeviceProjection();
    size_t changed = 0;
    std::vector<uint8_t> reference;
    for (bool depthWrite : {false,true}) {
    for (auto* model : {&red,&blue,&green})
        model->meshes[0].material.words[1] = (4u << 29) | (depthWrite ? 0u : 1u << 26);
    renderer.ClearCache();
    for (bool mixedPriority : {false,true}) {
    reference.clear();
    for (int count = 0; count <= 48; ++count) {
        std::vector<port::ModelRenderer::SpriteDraw> sprites(count);
        for (int i = 0; i < count; ++i) {
            auto& s = sprites[i];
            s.key = mixedPriority ? 0x80000008u : 0x8000000Fu;
            s.depth = mixedPriority ? 0.f : 1.f;
            s.isp = (4u << 29) | (1u << 26); s.tsp = 0x94100000u;
            s.v[0] = {800,0,.5f,1,0xffffffff,0,0,0};
            s.v[1] = {820,0,.5f,1,0xffffffff,0,0,0};
            s.v[2] = {800,20,.5f,1,0xffffffff,0,0,0};
            s.v[3] = {820,20,.5f,1,0xffffffff,0,0,0};
        }
        renderer.SetScene(assets, items);
        const float clear[4] = {0,0,0,1};
        Require(gpu->BeginFrame(clear), "begin frame");
        renderer.DrawGame(640,480,projection.m,&light,nullptr,view.m,&sprites,nullptr,projection.m);
        // Model stabilization must leave every original CRT-sorted sprite slot
        // unchanged, including ties between HUD elements.
        std::vector<port::ModelRenderer::Pass1Entry> expected;
        for (size_t i = 0; i < items.size(); ++i)
            expected.push_back({0x40000008u,items[i].transform[14],static_cast<int>(i),-1});
        for (int i = 0; i < count; ++i) expected.push_back({sprites[i].key,sprites[i].depth,-1,i});
        game::CrtQsort(expected.data(),expected.size(),[](const auto& a,const auto& b) {
            return game::ComparePass1(a.key,a.depth,b.key,b.depth);
        });
        Require(renderer.lastPass1.size() == expected.size(), "all models and sprites retained");
        for (size_t i=0;i<expected.size();++i) {
            Require(renderer.lastPass1[i].sprite == expected[i].sprite, "original HUD sprite slot/order changed");
            Require((renderer.lastPass1[i].key & 15u) == (expected[i].key & 15u) &&
                    renderer.lastPass1[i].depth == expected[i].depth, "priority/depth ordering changed");
        }
        std::vector<uint8_t> pixels;
        Require(gpu->ReadPixels(&pixels), "read pixels");
        gpu->EndFrame();
        if (reference.empty()) reference = pixels;
        for (size_t i=0;i<pixels.size();i+=4) changed += std::memcmp(pixels.data()+i,reference.data()+i,3)!=0;
    }
    }
    }
    std::printf("%s: %zu world pixels changed as unrelated sprite count varied 0..48\n",gpu->Name(),changed);
    Require(changed == 0, "HUD changes reordered equal-depth transparent world surfaces");
    const auto* rgb = reference.data() + (240 * 640 + 320) * 4;
    Require(std::abs(int(rgb[0])-32)<=1 && std::abs(int(rgb[1])-128)<=1 && std::abs(int(rgb[2])-64)<=1,
            "equal-depth surfaces preserve authored model submission order");
    puts("PASS transparent world order is independent of HUD sprite count, with and without depth writes");
}
