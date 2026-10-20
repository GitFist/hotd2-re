// CPU-only submission audit of the shipped layered-smoke frames. No GPU/window is created.
// The existing transparency GPU test checks pixels/occlusion; this checks asset and draw-path coverage.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>
#include "game/camera.h"
#include "render/model_renderer.h"

static void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

class CaptureGpu final : public port::GpuDevice {
public:
    port::GpuDrawList list;
    const char* Name() const override { return "CPU smoke capture"; }
    port::GpuTexture CreateTexture(const port::RgbaImage&, bool = false) override { return ++textures; }
    void ClearTextures() override { textures = 0; }
    void SetStaticVertices(const game::ModelVertex*, size_t) override {}
    void SetDynamicVertices(const game::ModelVertex*, size_t) override {}
    void SetSpriteVertices(const game::TLVertex*, size_t) override {}
    bool BeginFrame(const float*) override { return true; }
    void Submit(const port::GpuDrawList& value) override { list = value; }
    void EndFrame() override {}
    bool ReadPixels(std::vector<uint8_t>*) override { return false; }
    int TargetWidth() const override { return 640; }
    int TargetHeight() const override { return 480; }
private:
    uintptr_t textures = 0;
};

static float PlaneDepth(const game::ModelMesh& mesh, const game::Mat4& world, const game::Mat4& view) {
    // Transform the shared plane origin directly with double intermediates. Mesh sphere Y/X centres
    // deliberately do not participate: they do not determine which parallel card covers another.
    const double z = mesh.strips.front().vertices.front().pos[2];
    double p[3];
    for (int k = 0; k < 3; ++k) p[k] = z * world.m[8 + k] + world.m[12 + k];
    return static_cast<float>(p[0] * view.m[2] + p[1] * view.m[6] + p[2] * view.m[10] + view.m[14]);
}

static void Check(port::AssetStore& assets, const game::Model& model, const std::string& pol,
                  const game::Mat4& world, const game::Mat4& view, bool dynamic, bool alpha,
                  bool sortCards = true, int opaqueMesh = -1) {
    CaptureGpu gpu;
    port::ModelRenderer renderer;
    std::string error;
    Require(renderer.Init(&gpu, &error), error.c_str());
    port::ModelRenderer::SceneItem item;
    item.model = &model; item.polName = pol;
    item.dynamicGeometry = dynamic; item.cullable = false; item.noMeshCull = true;
    item.alpha = alpha ? .5f : -1.f;
    std::memcpy(item.transform, world.m, sizeof(item.transform));
    renderer.SetScene(assets, std::vector<port::ModelRenderer::SceneItem>{item});
    const auto projection = game::DeviceView() * game::DeviceProjection();
    const auto vp = view * projection;
    renderer.DrawGame(640, 480, vp.m, nullptr, nullptr, view.m, nullptr, nullptr, projection.m);
    std::vector<int> expected(model.meshes.size());
    std::iota(expected.begin(), expected.end(), 0);
    if (sortCards) {
        std::stable_sort(expected.begin(), expected.end(), [&](int a, int b) {
            return PlaneDepth(model.meshes[a], world, view) < PlaneDepth(model.meshes[b], world, view);
        });
    } else if (!alpha && opaqueMesh >= 0) {
        expected.erase(std::find(expected.begin(), expected.end(), opaqueMesh));
        expected.insert(expected.begin(), opaqueMesh);  // opaque pass precedes translucent submissions
    }
    Require(gpu.list.draws.size() == model.meshes.size(), "all real smoke cards retained");
    for (size_t i = 0; i < expected.size(); ++i) {
        const auto& draw = gpu.list.draws[i];
        const int mesh = expected[i];
        if (draw.first != static_cast<uint32_t>(mesh * 6))
            std::fprintf(stderr, "%s card %zu: mesh %u expected %d, dynamic %d alpha %d\n",
                         pol.c_str(), i, draw.first / 6, mesh, dynamic, alpha);
        Require(draw.first == static_cast<uint32_t>(mesh * 6) && draw.count == 6,
                "cards emitted in independently computed plane order");
        Require(draw.stream == (dynamic ? port::GpuDraw::Stream::Dynamic : port::GpuDraw::Stream::Static),
                "requested geometry stream retained");
        const bool blended = alpha || mesh != opaqueMesh;
        Require((draw.state.blendEnable != 0) == blended, "opaque/forced-alpha pass selection retained");
        if (blended) Require(draw.state.srcBlend == 5 && draw.state.dstBlend == 6, "source-alpha blending retained");
        Require(draw.state.zwrite && draw.state.zfunc == 4, "authored smoke depth testing/writes retained");
        const auto& constants = gpu.list.model[draw.constants];
        const float materialAlpha = model.meshes[mesh].material.DiffuseA() * (alpha ? .5f : 1.f);
        Require(std::abs(constants.diffuse[3] - materialAlpha) < 1e-6f, "material/draw alpha retained");
        Require(constants.localCount[2] == 1, "camera-space projection retained for every smoke path");
    }
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: render_smoke_assets_test <game directory>\n"); return 2; }
    port::AssetStore assets;
    std::string error;
    Require(assets.Open(argv[1], &error), error.c_str());
    std::vector<int> ids;
    for (auto range : {std::pair<int,int>{0xAB4,0xAB6}, {0xAC6,0xAC9}, {0xB3F,0xB65},
                       {0xDB4,0xDBB}, {0x1383,0x1383}, {0x1433,0x145A}, {0x1AAB,0x1AD2}})
        for (int id = range.first; id <= range.second; ++id) ids.push_back(id);
    Require(ids.size() == 135, "all layered smoke/effect animation IDs enumerated");
    int cases = 0;
    for (int id : ids) {
        game::Model model; std::string pol;
        Require(assets.ModelById(id, model, &pol), "shipped smoke/effect frame resolves");
        Require(model.meshes.size() >= 2, "layered animation retains multiple cards");
        for (const auto& mesh : model.meshes)
            Require(mesh.strips.size() == 1 && !mesh.strips[0].triangleList && mesh.strips[0].vertices.size() == 4,
                    "real smoke card topology changed");
        for (bool dynamic : {false,true}) for (bool alpha : {false,true}) for (int pose = 0; pose < 4; ++pose) {
            auto world = game::Mat4::RotateY(pose == 2 ? 32768 : 0) * game::Mat4::RotateZ(2048);
            if (pose == 3) for (int k = 8; k < 12; ++k) world.m[k] = -world.m[k]; // reflected local Z
            world.m[12] = 10000; world.m[14] = -12000;
            const auto view = game::Mat4::Translate(-10000, 0, 8000) * game::Mat4::RotateX(pose == 0 ? 0 : 512);
            Check(assets, model, pol, world, view, dynamic, alpha); ++cases;
        }
    }
    game::Model plume; std::string pol;
    Require(assets.ModelById(0x1AB8, plume, &pol), "Magician regression frame resolves");
    auto mixed = plume;
    mixed.meshes.push_back(plume.meshes.back());
    mixed.meshes.back().material.words[2] = 0x80000; // opaque card: entire model must keep authored order
    auto normalMismatch = plume;
    normalMismatch.meshes[0].strips[0].vertices[0].normal[0] = .25f;
    const auto world = game::Mat4::Translate(0, 0, -4000), view = game::Mat4::RotateX(512);
    for (bool dynamic : {false,true}) for (bool alpha : {false,true}) {
        Check(assets, mixed, pol, world, view, dynamic, alpha, false, static_cast<int>(mixed.meshes.size()-1)); ++cases;
        Check(assets, normalMismatch, pol, world, view, dynamic, alpha, false); ++cases;
    }
    std::printf("PASS %d CPU smoke submission cases across 135 shipped frames, cached/dynamic, alpha, mirrored and mixed materials\n", cases);
}
