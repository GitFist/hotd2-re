// D3D7 fixed-function lighting with NORMALIZENORMALS disabled. A native D3D7 HAL
// ProcessVertices probe gives specular RGB 20 for material 0.2, light 0.4 and power
// 32 at N.H = 1, 2 and 1/0.3: the dot product is clamped before exponentiation.
// Diffuse is different: its N.L is not upper-clamped (RGB 20, 41 and 68).
// The game's scaled actors can produce these normals longer than unit length.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "game/anim.h"
#include "render/model_renderer.h"
#include "render/d3d11_device.h"
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif

static void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

static game::Model Plane(float normal, bool specular) {
    game::Model model;
    game::ModelMesh mesh;
    auto& words = mesh.material.words;
    words[1] = 4u << 29; // LESSEQUAL, depth write
    words[2] = 0x80000; // opaque
    words[8] = 0xffffffff; // no texture
    words[9] = specular ? 5 : 0; // power 32, or no specular
    const float diffuse[4] = {1, specular ? 0.f : .2f, specular ? 0.f : .2f, specular ? 0.f : .2f};
    std::memcpy(words + 11, diffuse, sizeof(diffuse));
    const float sheen[3] = {.2f,.2f,.2f};
    std::memcpy(words + 16, sheen, sizeof(sheen));
    game::ModelStrip strip;
    for (auto p : {std::pair<float,float>{-.01f,-.01f}, {.01f,-.01f}, {-.01f,.01f}, {.01f,.01f}}) {
        game::ModelVertex v{};
        v.pos[0] = p.first; v.pos[1] = p.second;
        v.normal[2] = normal;
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
    const auto view = game::Mat4::Identity();
    auto projection = game::Mat4::Identity();
    projection.m[0] = projection.m[5] = 100;
    projection.m[10] = 0; projection.m[14] = .5f;
    port::ModelRenderer::Lighting light;
    light.enabled = true; light.dirToLight[1] = 0; light.dirToLight[2] = 1;
    for (float& c : light.diffuse) c = .4f;
    for (bool specular : {true,false}) for (float scale : {1.f,.3f}) for (float normal : {.5f,1.f,2.f}) {
        auto plane = Plane(normal, specular);
        port::ModelRenderer::SceneItem item;
        item.model = &plane; item.cullable = false; item.noMeshCull = true;
        item.transform[0] = item.transform[5] = item.transform[10] = scale;
        item.transform[14] = -1;
        renderer.ClearCache();
        renderer.SetScene(assets, {item});
        const float clear[4] = {0,0,0,1};
        Require(gpu->BeginFrame(clear), "begin frame");
        renderer.DrawGame(640,480,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
        std::vector<uint8_t> pixels;
        Require(gpu->ReadPixels(&pixels), "read pixels");
        gpu->EndFrame();
        const float dot = normal / scale;
        const float factor = specular ? std::pow(std::min(1.f,dot),32.f) : dot;
        const int expected = static_cast<int>(std::round(std::min(1.f,.08f*factor)*255));
        const size_t center = (240*640+320)*4;
        std::printf("%s %s scale %.1f normal %.1f: RGB %u,%u,%u expected %d\n",
                    gpu->Name(),specular ? "specular" : "diffuse",scale,normal,
                    pixels[center],pixels[center+1],pixels[center+2],expected);
        for (int c=0;c<3;++c) Require(std::abs(static_cast<int>(pixels[center+c])-expected)<=1,
                                      "unnormalized lighting differs from D3D7");
    }

    // Fish keep the native diffuse response but opt into a unit specular normal.
    // At scale .3, this unit source normal has legacy N.H > 1, despite pointing
    // far from the highlight. Normalizing just the specular input removes that
    // plateau without dimming diffuse light or suppressing an aligned glint.
    auto angled = Plane(.4f, true);
    auto aligned = Plane(1.f, true);
    auto diffuseOnly = Plane(.4f, false);
    for (auto* model : {&angled,&diffuseOnly})
        for (auto& vertex : model->meshes[0].strips[0].vertices)
            vertex.normal[0] = std::sqrt(1.f - .4f * .4f);
    auto render = [&](const std::vector<port::ModelRenderer::SceneItem>& items) {
        renderer.ClearCache();
        renderer.SetScene(assets, items);
        const float clear[4] = {0,0,0,1};
        Require(gpu->BeginFrame(clear), "begin opted-specular frame");
        renderer.DrawGame(640,480,projection.m,&light,nullptr,view.m,nullptr,nullptr,projection.m);
        std::vector<uint8_t> pixels;
        Require(gpu->ReadPixels(&pixels), "read opted-specular pixels");
        gpu->EndFrame();
        return pixels;
    };
    auto requireRgb = [&](const std::vector<uint8_t>& pixels, int x, int expected, const char* why) {
        const size_t at = (240 * 640 + x) * 4;
        for (int c=0;c<3;++c) Require(std::abs(static_cast<int>(pixels[at+c])-expected)<=1,why);
    };
    port::ModelRenderer::SceneItem fish;
    fish.cullable = false; fish.noMeshCull = true;
    fish.transform[0] = fish.transform[5] = fish.transform[10] = .3f;
    fish.transform[14] = -1;
    for (bool opted : {false,true}) {
        fish.unitSpecular = opted;
        fish.model = &angled;
        requireRgb(render({fish}),320,opted ? 0 : 20,"opted specular normal did not remove the scaled-normal plateau");
        fish.model = &aligned;
        requireRgb(render({fish}),320,20,"opted specular normal suppressed an aligned highlight");
        fish.model = &diffuseOnly;
        requireRgb(render({fish}),320,27,"specular opt-in changed native diffuse lighting");
    }
    // Each item must reset the opt-in, including true -> false within one draw
    // list and across successive scene submissions.
    auto left = fish, right = fish;
    left.model = right.model = &angled;
    left.transform[12] = -.005f; right.transform[12] = .005f;
    for (bool firstOpted : {true,false}) {
        left.unitSpecular = firstOpted; right.unitSpecular = !firstOpted;
        const auto pixels = render({left,right});
        requireRgb(pixels,160,firstOpted ? 0 : 20,"left item's specular opt-in leaked across scenes");
        requireRgb(pixels,480,firstOpted ? 20 : 0,"specular opt-in leaked between items");
    }
    std::puts("PASS bounded native specular and fish-only unit specular; diffuse and unrelated draws unchanged");
}
