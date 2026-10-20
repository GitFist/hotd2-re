#include "model_renderer.h"

#include "game/crt_qsort.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace port {

namespace {

// Row-vector matrices (HLSL mul(v, M)), left-handed, matching the offline renderer's camera.
void Multiply(const float a[16], const float b[16], float out[16]) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[r * 4 + k] * b[k * 4 + c];
            out[r * 4 + c] = s;
        }
}

float MaxAxisScale(const float t[16]) {
    float s = 0;
    for (int r = 0; r < 3; ++r) s = std::max(s, std::sqrt(t[r * 4] * t[r * 4] + t[r * 4 + 1] * t[r * 4 + 1] + t[r * 4 + 2] * t[r * 4 + 2]));
    return s;
}

// Like IDirect3DDevice7::ComputeSphereVisibility's D3DSTATUS_CLIPINTERSECTION* bits (0xFFF000), which Mdl_Draw tests:
// true when the sphere lies completely outside one of the six clip planes (row-vector world->clip matrix).
bool SphereOutside(const float m[16], const float c[3], float r, bool meshTest) {
    // Mesh test (Mdl_Draw's ComputeSphereVisibility): HYPOTHESIS fitted to the captured pass-1 lists (the D3D runtime is
    // not in the exe). Rule 2 (default): a sphere that is not entirely in front of the near plane is only reported outside
    // when entirely behind it; otherwise the exact six-plane test. Matches 41/43 stage-demo and 95/95 title lists vs 4/23 for
    // the exact test. HOTD2_CULL_RULE=0 exact, 1 = only for centres behind the eye. Whole models (Stage_DrawSection's own
    // test 0x00406310) always use the exact test.
    static const int rule = getenv("HOTD2_CULL_RULE") ? atoi(getenv("HOTD2_CULL_RULE")) : 2;
    const float w = c[0] * m[3] + c[1] * m[7] + c[2] * m[11] + m[15];
    float col[4][4];  // col[j] = column j of m: (m[j], m[4+j], m[8+j], m[12+j])
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) col[j][i] = m[i * 4 + j];
    float planes[6][4];
    for (int i = 0; i < 4; ++i) {
        planes[0][i] = col[3][i] + col[0][i];  // left   w + x >= 0
        planes[1][i] = col[3][i] - col[0][i];  // right  w - x >= 0
        planes[2][i] = col[3][i] + col[1][i];  // bottom w + y >= 0
        planes[3][i] = col[3][i] - col[1][i];  // top    w - y >= 0
        planes[4][i] = col[2][i];              // near   z >= 0
        planes[5][i] = col[3][i] - col[2][i];  // far    w - z >= 0
    }
    for (int k = 0; k < 6; ++k) {
        const auto& p = planes[k];
        if (meshTest && rule == 1 && w < 0 && k != 4) continue;
        if (meshTest && rule == 2 && w - 0.8f < r && k != 4) continue;  // sphere not entirely in front of the near plane
        const float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (len <= 0) continue;
        if ((p[0] * c[0] + p[1] * c[1] + p[2] * c[2] + p[3]) / len < -r) return true;
    }
    return false;
}

// Mdl_ApplyPvrState 0x004A7780 / tables 0x00598B00 (zfunc), 0x00598AB0 / 0x00598AD0 (blend), 0x00598AF0 (address).
const int kZFunc[8] = { 1, 7, 3, 5, 4, 6, 2, 8 };
const int kSrc[8] = { 1, 2, 9, 10, 5, 6, 7, 8 };
const int kDst[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
const int kAddr[4] = { 1, 2, 3, 2 };

bool HasLayeredAlphaCards(const game::Model& model) {
    if (model.meshes.size() < 2) return false;
    for (const auto& mesh : model.meshes) {
        const uint32_t isp = mesh.material.Isp(), tsp = mesh.material.Tsp();
        if ((tsp & 0x180000) == 0x80000 || (tsp >> 29) != 4 || ((tsp >> 26) & 7) != 5 ||
            (isp >> 29) != 4 || (isp & 0x4000000) || mesh.strips.size() != 1) return false;
        const auto& strip = mesh.strips.front();
        if (strip.triangleList || strip.vertices.size() != 4) return false;
        const float z = strip.vertices.front().pos[2];
        for (const auto& v : strip.vertices)
            if (v.pos[2] != z || v.normal[0] != 0 || v.normal[1] != 0 || std::abs(v.normal[2]) < 0.99f)
                return false;
    }
    return true;
}

}  // namespace

bool ModelRenderer::Init(GpuDevice* device, std::string* error) {
    device_ = device;
    if (!device_) {
        if (error) *error = "no GPU device";
        return false;
    }
    return true;
}

GpuTexture ModelRenderer::TextureView(const RgbaImage* img, bool alphaWeighted) {
    if (!img) return 0;
    const auto key = std::make_pair(img, alphaWeighted);
    auto it = textures_.find(key);
    if (it != textures_.end()) return it->second;
    const GpuTexture t = device_->CreateTexture(*img, alphaWeighted);
    if (t) textures_[key] = t;  // failures are retried (like the D3D11 renderer did)
    return t;
}

void ModelRenderer::ClearCache() {
    batches_.clear();
    models_.clear(); dynModels_.clear(); textures_.clear();
    std::vector<game::ModelVertex>().swap(cacheVerts_);
    std::vector<game::ModelVertex>().swap(dynVerts_);
    cacheDirty_ = boundsValid_ = haveStatic_ = haveDynamic_ = dynUploadPending_ = false;
    device_->SetStaticVertices(nullptr, 0);
    device_->ClearTextures();
}

GpuState ModelRenderer::StateOf(bool blend, int src, int dst, int zfunc, bool zwrite, bool linear, int addrU, int addrV) {
    GpuState s;
    s.blendEnable = blend ? 1 : 0;
    s.srcBlend = static_cast<uint8_t>(src);
    s.dstBlend = static_cast<uint8_t>(dst);
    s.zfunc = static_cast<uint8_t>(zfunc);
    s.zwrite = zwrite ? 1 : 0;
    s.linear = linear ? 1 : 0;
    s.addrU = static_cast<uint8_t>(addrU);
    s.addrV = static_cast<uint8_t>(addrV);
    return s;
}

void ModelRenderer::SetScene(AssetStore& assets, const std::vector<const game::Model*>& models, const std::string& polName) {
    std::vector<SceneItem> items;
    for (const game::Model* m : models) {
        SceneItem it;
        it.model = m;
        it.polName = polName;
        items.push_back(it);
    }
    SetScene(assets, items);
}

const ModelRenderer::ModelGpu& ModelRenderer::CachedModel(AssetStore& assets, const game::Model* model, const std::string& polName) {
    auto key = std::make_pair(model, polName);
    auto it = models_.find(key);
    if (it != models_.end()) return it->second;
    ModelGpu& g = models_[key];
    BuildMeshes(assets, model, polName, g, cacheVerts_, false);
    cacheDirty_ = true;
    return g;
}

void ModelRenderer::BuildMeshes(AssetStore& assets, const game::Model* model, const std::string& polName, ModelGpu& g,
                                std::vector<game::ModelVertex>& verts, bool dynamic) {
    // Mdl_Draw 0x004A7EF0: each strip's control word sets D3DRENDERSTATE_CULLMODE = 0x00598B20[ctrl & 3] = {none, none,
    // CCW, CW} unless bit 0x80 (keep the current mode); the mode also stays set across strips. Strips are grouped into
    // runs of one cull mode (one draw each). The state carried over from the previous model is not modelled (a model
    // starts with "none").
    static const int kCull[4] = { 1, 1, 3, 2 };
    const bool cullOff = getenv("HOTD2_CULL_OFF") != nullptr;
    const bool layeredCards = HasLayeredAlphaCards(*model);
    int cull = 1;
    for (const game::ModelMesh& mesh : model->meshes) {
        Mesh m;
        m.dynamic = dynamic;
        m.layeredCards = layeredCards;
        if (layeredCards) m.cardZ = mesh.strips.front().vertices.front().pos[2];
        m.first = static_cast<uint32_t>(verts.size());
        std::vector<std::pair<uint32_t, int>> runs;  // first vertex, cull mode
        for (const game::ModelStrip& s : mesh.strips) {
            if (!(s.ctrl & 0x80)) cull = kCull[s.ctrl & 3];
            const int c = cullOff ? 1 : cull;
            if (runs.empty() || runs.back().second != c) runs.push_back({ static_cast<uint32_t>(verts.size()), c });
            game::Triangulate(s, verts);  // model space
        }
        m.count = static_cast<uint32_t>(verts.size()) - m.first;
        // Pass selection in Mdl_Draw 0x004A7EF0.
        const uint32_t isp = mesh.material.Isp(), tsp = mesh.material.Tsp();
        m.translucent = (tsp & 0x180000) != 0x80000;
        m.zfunc = kZFunc[isp >> 29];
        m.zwrite = ((isp >> 26) & 1) == 0;
        m.srcBlend = kSrc[tsp >> 29];
        m.dstBlend = kDst[(tsp >> 26) & 7];
        m.linear = (tsp & 0x6000) != 0;
        m.addrU = kAddr[(((tsp >> 3) & 0x8000) | (tsp & 0x10000)) >> 15];
        m.addrV = kAddr[(((tsp >> 3) & 0x4000) | (tsp & 0x8000)) >> 14];
        m.alphaFromTexture = ((tsp >> 6) & 3) == 1;
        m.fog = ((tsp >> 23) & 1) == 0;  // FOGENABLE = !bit 23
        for (int k = 0; k < 4; ++k) m.sphere[k] = mesh.material.Sphere(k);  // material bounding sphere (words 4-7)
        m.ambientScale = mesh.material.AmbientScale();
        if (const int n = mesh.material.SpecularExponent(); n > 0 && n < 31) {  // Mdl_Draw 0x004A8026: power = 1 << n
            for (int k = 0; k < 3; ++k) m.specular[k] = mesh.material.Specular(k);
            m.specular[3] = static_cast<float>(1 << n);
        }
        const float d[4] = { mesh.material.DiffuseR(), mesh.material.DiffuseG(), mesh.material.DiffuseB(), mesh.material.DiffuseA() };
        memcpy(m.diffuse, d, sizeof(d));
        if (mesh.material.Textured()) {
            m.tex = TextureView(assets.Texture(polName, mesh.material.TextureRef()), m.translucent);
            m.textured = true;
        }
        if (!m.count) continue;
        const uint32_t end = m.first + m.count;
        for (size_t r = 0; r < runs.size(); ++r) {  // one mesh per run of strips with the same cull mode
            Mesh part = m;
            part.first = runs[r].first;
            part.count = (r + 1 < runs.size() ? runs[r + 1].first : end) - part.first;
            part.cull = runs[r].second;
            part.firstRun = r == 0;
            if (part.count) g.meshes.push_back(part);
        }
    }
}

void ModelRenderer::SetScene(AssetStore& assets, const std::vector<SceneItem>& items) {
    batches_.clear();
    itemOrigin_.clear();
    itemSphere_.clear();
    itemCullable_.clear();
    itemPriority_.clear();
    itemAlpha_.clear();
    itemUnitSpecular_.clear();
    itemSortBackToFront_.clear();
    itemCullFlip_.clear();
    itemLight_.clear();
    itemWorld_.clear();
    stats_ = {};
    boundsValid_ = false;
    dynModels_.clear();
    dynVerts_.clear();
    for (const SceneItem& item : items) {
        const float* t = item.transform;
        const ModelGpu* gp;
        if (item.dynamicGeometry) {
            dynModels_.push_back(std::make_unique<ModelGpu>());
            BuildMeshes(assets, item.model, item.polName, *dynModels_.back(), dynVerts_, true);
            gp = dynModels_.back().get();
        } else {
            gp = &CachedModel(assets, item.model, item.polName);
        }
        const ModelGpu& g = *gp;
        const int itemIndex = static_cast<int>(itemOrigin_.size());
        const float* sc = item.model->sphere;
        float center[3];
        for (int k = 0; k < 3; ++k) center[k] = sc[0] * t[k] + sc[1] * t[4 + k] + sc[2] * t[8 + k] + t[12 + k];
        const float scale = MaxAxisScale(t);
        for (const Mesh& m : g.meshes) {
            Batch b;
            b.mesh = &m;
            b.item = itemIndex;
            b.noMeshCull = item.noMeshCull;
            memcpy(b.center, center, sizeof(center));
            for (int k = 0; k < 3; ++k)  // material bounding sphere centre -> world
                b.meshCenter[k] = m.sphere[0] * t[k] + m.sphere[1] * t[4 + k] + m.sphere[2] * t[8 + k] + t[12 + k];
            b.meshRadius = m.sphere[3] * scale;
            stats_.triangles += m.count / 3;
            if (m.firstRun) {
                ++stats_.meshes;
                if (m.textured) (m.tex ? stats_.texturedMeshes : stats_.missingTextures)++;
            }
            batches_.push_back(b);
        }
        itemOrigin_.push_back({ t[12], t[13], t[14] });
        itemSphere_.push_back({ center[0], center[1], center[2], sc[3] * scale });
        itemCullable_.push_back(item.cullable);
        itemPriority_.push_back(item.priority);
        itemAlpha_.push_back(item.alpha);
        itemUnitSpecular_.push_back(item.unitSpecular ? 1 : 0);
        itemSortBackToFront_.push_back(item.sortBackToFront ? 1 : 0);
        itemCullFlip_.push_back(item.cullFlip ? 1 : 0);
        itemLight_.push_back(item.light);
        std::array<float, 16> w;
        memcpy(w.data(), t, sizeof(float) * 16);
        itemWorld_.push_back(w);
    }
    if (!dynVerts_.empty()) {
        haveDynamic_ = true;
        dynUploadPending_ = true;
    }
    if (cacheDirty_ && !cacheVerts_.empty()) {
        device_->SetStaticVertices(cacheVerts_.data(), cacheVerts_.size());
        haveStatic_ = true;
        cacheDirty_ = false;
    }
}

void ModelRenderer::ComputeBounds() {
    // Framing for the model viewer: AABB centre of the transformed vertices and the farthest vertex from it.
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    auto each = [&](auto fn) {
        for (const Batch& b : batches_) {
            const float* t = itemWorld_[b.item].data();
            for (uint32_t i = b.mesh->first; i < b.mesh->first + b.mesh->count; ++i) {
                const float* p = cacheVerts_[i].pos;
                float v[3];
                for (int c = 0; c < 3; ++c) v[c] = p[0] * t[c] + p[1] * t[4 + c] + p[2] * t[8 + c] + t[12 + c];
                fn(v);
            }
        }
    };
    each([&](const float* v) {
        for (int i = 0; i < 3; ++i) {
            lo[i] = std::min(lo[i], v[i]);
            hi[i] = std::max(hi[i], v[i]);
        }
    });
    for (int i = 0; i < 3; ++i) center_[i] = (lo[i] + hi[i]) / 2;
    radius_ = 0;
    each([&](const float* v) {
        const float dx = v[0] - center_[0], dy = v[1] - center_[1], dz = v[2] - center_[2];
        radius_ = std::max(radius_, std::sqrt(dx * dx + dy * dy + dz * dz));
    });
    if (radius_ <= 0) radius_ = 1;
    boundsValid_ = true;
}

void ModelRenderer::AddModelDraw(GpuDrawList& list, const Mesh& m, const ModelConstants& fc, const GpuState& st) {
    GpuDraw d;
    d.pipeline = GpuDraw::Pipeline::Model;
    d.stream = m.dynamic ? GpuDraw::Stream::Dynamic : GpuDraw::Stream::Static;
    d.first = m.first;
    d.count = m.count;
    d.texture = m.tex;
    d.state = st;
    d.constants = static_cast<uint32_t>(list.model.size());
    list.model.push_back(fc);
    // Reverse clip depth on the CPU, before projection, so the float depth target has
    // useful precision on distant surfaces too. Subtracting z from w in the shader
    // would already have lost that precision. Culling and game material state stay conventional.
    auto& constants = list.model.back();
    float* matrix = constants.localCount[2] > 0.5f ? constants.projection : constants.viewProj;
    for (int row = 0; row < 4; ++row) matrix[row * 4 + 2] = matrix[row * 4 + 3] - matrix[row * 4 + 2];
    list.draws.push_back(d);
}

void ModelRenderer::Draw(int width, int height, const Camera& cam) {
    if (!haveStatic_) return;
    if (!boundsValid_) ComputeBounds();
    const float cy = std::cos(cam.yawDeg * 3.14159265f / 180), sy = std::sin(cam.yawDeg * 3.14159265f / 180);
    const float cp = std::cos(cam.pitchDeg * 3.14159265f / 180), sp = std::sin(cam.pitchDeg * 3.14159265f / 180);
    // view = translate(-center) * R, with R = Ry(yaw) * Rx(pitch) as in tools/pol_render.py, then push by dist.
    const float ry[16] = { cy, 0, sy, 0, 0, 1, 0, 0, -sy, 0, cy, 0, 0, 0, 0, 1 };
    const float rx[16] = { 1, 0, 0, 0, 0, cp, -sp, 0, 0, sp, cp, 0, 0, 0, 0, 1 };
    float r[16];
    Multiply(rx, ry, r);  // (v - c) @ Rx^T @ Ry^T == pol_render.py's (v - c) @ (Ry @ Rx)^T
    const float dist = radius_ * cam.distanceScale;
    const float t[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -center_[0], -center_[1], -center_[2], 1 };
    float view[16];
    Multiply(t, r, view);
    view[14] += dist;
    // Perspective matching the offline renderer: screen = size/2 + x * (1.1 * size) / z.
    const float zn = std::max(dist - radius_ * 1.5f, radius_ * 0.01f), zf = dist + radius_ * 1.5f;
    const float fy = 2.2f, fx = fy * static_cast<float>(height) / width;
    const float proj[16] = { fx, 0, 0, 0, 0, fy, 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0 };
    ModelConstants fc = {};
    Multiply(view, proj, fc.viewProj);
    float lv[3] = { 0.4f, 0.8f, 0.45f };
    const float ll = std::sqrt(lv[0] * lv[0] + lv[1] * lv[1] + lv[2] * lv[2]);
    // Light is fixed in view space in the reference; transform it back to world space (inverse rotation).
    for (int i = 0; i < 3; ++i) fc.lightDir[i] = (r[i * 4 + 0] * lv[0] + r[i * 4 + 1] * lv[1] + r[i * 4 + 2] * lv[2]) / ll;

    GpuDrawList list;
    list.width = width;
    list.height = height;
    const GpuState st = StateOf(false, 2, 1, 2, true, false, 1, 1);  // no blending, LESS + write, point / wrap
    for (const Batch& b : batches_) {
        if (b.mesh->dynamic) continue;  // dynamic geometry: DrawGame only
        // Match the offline reference renderer: texture colour only, untextured meshes grey (material diffuse unused for now).
        memcpy(fc.world, itemWorld_[b.item].data(), sizeof(fc.world));
        const float grey = b.mesh->tex ? 1.0f : 0.7f;
        fc.diffuse[0] = fc.diffuse[1] = fc.diffuse[2] = grey;
        fc.diffuse[3] = 1.0f;
        fc.flags[0] = b.mesh->tex ? 1.0f : 0.0f;
        fc.flags[2] = 0.5f;  // reference-renderer look: alpha test at 0.5, opaque output
        fc.flags[3] = 1.0f;
        AddModelDraw(list, *b.mesh, fc, st);
    }
    device_->Submit(list);
}

void ModelRenderer::DrawWithMatrix(int width, int height, const float viewProj[16], bool unlit) {
    if (!haveStatic_) return;
    ModelConstants fc = {};
    memcpy(fc.viewProj, viewProj, sizeof(fc.viewProj));
    fc.lightDir[1] = 1;
    fc.flags[1] = unlit ? 1.0f : 0.0f;
    GpuDrawList list;
    list.width = width;
    list.height = height;
    const GpuState st = StateOf(false, 2, 1, 2, true, false, 1, 1);
    for (const Batch& b : batches_) {
        if (b.mesh->dynamic) continue;  // dynamic geometry: DrawGame only
        memcpy(fc.world, itemWorld_[b.item].data(), sizeof(fc.world));
        const float grey = b.mesh->tex ? 1.0f : 0.7f;
        fc.diffuse[0] = fc.diffuse[1] = fc.diffuse[2] = grey;
        fc.diffuse[3] = 1.0f;
        fc.flags[0] = b.mesh->tex ? 1.0f : 0.0f;
        AddModelDraw(list, *b.mesh, fc, st);
    }
    device_->Submit(list);
}

void ModelRenderer::UploadDynamic() {
    if (!dynUploadPending_) return;
    device_->SetDynamicVertices(dynVerts_.data(), dynVerts_.size());
    dynUploadPending_ = false;
}

void ModelRenderer::AddSpriteDraw(GpuDrawList& list, int width, int height, const SpriteDraw& s, size_t index) {
    // Same decoding as Mdl_ApplyPvrState (see BuildMeshes).
    const bool translucent = (s.tsp & 0x180000) != 0x80000;
    SpriteConstants sc = {};
    sc.screen[0] = width / 640.0f;
    sc.screen[1] = height / 480.0f;
    sc.screen[2] = translucent ? 1.0f / 255.0f : -1.0f;
    const GpuTexture tex = TextureView(s.texture);
    sc.screen[3] = tex ? 1.0f : 0.0f;
    GpuDraw d;
    d.pipeline = GpuDraw::Pipeline::Sprite;
    d.stream = GpuDraw::Stream::Sprite;
    d.first = static_cast<uint32_t>(index * 4);
    d.count = 4;
    d.texture = tex;
    d.state = StateOf(translucent, kSrc[s.tsp >> 29], kDst[(s.tsp >> 26) & 7], kZFunc[s.isp >> 29], ((s.isp >> 26) & 1) == 0,
                      (s.tsp & 0x6000) != 0, kAddr[(((s.tsp >> 3) & 0x8000) | (s.tsp & 0x10000)) >> 15],
                      kAddr[(((s.tsp >> 3) & 0x4000) | (s.tsp & 0x8000)) >> 14]);
    d.constants = static_cast<uint32_t>(list.sprite.size());
    list.sprite.push_back(sc);
    list.draws.push_back(d);
}

namespace {
void UploadSprites(GpuDevice* device, const std::vector<ModelRenderer::SpriteDraw>& sprites) {
    std::vector<game::TLVertex> v(sprites.size() * 4);
    for (size_t i = 0; i < sprites.size(); ++i) memcpy(&v[i * 4], sprites[i].v, sizeof(sprites[i].v));
    device->SetSpriteVertices(v.data(), v.size());
}
}  // namespace

void ModelRenderer::DrawGame(int width, int height, const float viewProj[16], const Lighting* light, const Fog* fog,
                             const float* gameView, const std::vector<SpriteDraw>* sprites,
                             const std::vector<Lighting>* itemLights, const float* projection) {
    if (!haveStatic_ && !haveDynamic_ && (!sprites || sprites->empty())) return;  // sprites alone: e.g. the game-over letters
    ModelConstants fc = {};
    if (gameView && projection) {
        memcpy(fc.projection, projection, sizeof(fc.projection));
        fc.localCount[2] = 1;
    }
    // The light constants of one item (its own light when it has one, else the frame light).
    const Lighting* curLight = reinterpret_cast<const Lighting*>(1);  // none set yet
    auto setLight = [&](const Lighting* l) {
        if (l == curLight) return;
        curLight = l;
        for (int k = 0; k < 4; ++k) fc.litDir[k] = fc.litAmbient[k] = 0;
        for (int k = 0; k < 3; ++k) fc.litDiffuse[k] = fc.litSpecular[k] = 0;
        fc.localCount[0] = 0;
        if (!l || !l->enabled) return;
        for (int k = 0; k < 3; ++k) {
            fc.litDir[k] = l->dirToLight[k];
            fc.litDiffuse[k] = l->diffuse[k];
            fc.litSpecular[k] = l->diffuse[k];  // 0x004AA120: specular = diffuse
            fc.litAmbient[k] = l->ambient[k] + l->ambientRS[k];
        }
        fc.litDir[3] = 1;
        const int n = std::min(l->localCount, kMaxLocalLights);
        fc.localCount[0] = static_cast<float>(n);
        for (int i = 0; i < n; ++i) {
            const Lighting::Local& s = l->local[i];
            float* p = fc.local[i * 5];
            p[0] = s.pos[0], p[1] = s.pos[1], p[2] = s.pos[2], p[3] = s.range;
            p[4] = s.dir[0], p[5] = s.dir[1], p[6] = s.dir[2], p[7] = s.falloff;
            p[8] = s.diffuse[0], p[9] = s.diffuse[1], p[10] = s.diffuse[2], p[11] = static_cast<float>(s.type);
            p[12] = s.att[0], p[13] = s.att[1], p[14] = s.att[2], p[15] = 0;
            p[16] = std::cos(s.theta * 0.5f), p[17] = std::cos(s.phi * 0.5f), p[18] = p[19] = 0;
        }
    };
    auto itemLighting = [&](int item) -> const Lighting* {
        const int li = item >= 0 && item < static_cast<int>(itemLight_.size()) ? itemLight_[item] : -1;
        if (itemLights && li >= 0 && li < static_cast<int>(itemLights->size())) return &(*itemLights)[li];
        return light;
    };
    const bool anyLight = (light && light->enabled) || (itemLights && !itemLights->empty());
    setLight(light);
    // Normals: D3D7 transforms them by the inverse transpose of WORLD (x VIEW) and the game never sets NORMALIZENORMALS
    // (default FALSE), so scaled models get shorter / longer normals (mode 2; sweep vs the original's shots: 4.11 mean
    // block colour difference against 4.19 renormalised). HOTD2_NORMALS=0 renormalises, 1 = WORLD as is.
    static const float normalMode = getenv("HOTD2_NORMALS") ? static_cast<float>(atoi(getenv("HOTD2_NORMALS"))) : 2.0f;
    fc.localCount[1] = normalMode;
    if (gameView) {  // eye = -t R^T of the row-vector view matrix (local viewer, specular)
        for (int j = 0; j < 3; ++j)
            fc.eyePos[j] = -(gameView[12] * gameView[j * 4] + gameView[13] * gameView[j * 4 + 1] + gameView[14] * gameView[j * 4 + 2]);
    }
    static const bool noSpecular = getenv("HOTD2_NO_SPECULAR") != nullptr;
    if (fog && fog->enabled) {
        fc.fogParams[0] = fog->start;
        fc.fogParams[1] = fog->end;
        for (int k = 0; k < 3; ++k) fc.fogColor[k] = fog->color[k];
    }
    memcpy(fc.viewProj, viewProj, sizeof(fc.viewProj));
    fc.lightDir[1] = 1;
    fc.flags[1] = 1;  // unlit

    GpuDrawList out;
    out.width = width;
    out.height = height;
    UploadDynamic();
    auto drawBatch = [&](const Batch* bb, int pass) {
        const Mesh* b = bb->mesh;
        setLight(itemLighting(bb->item));
        memcpy(fc.world, itemWorld_[bb->item].data(), sizeof(fc.world));
        if (gameView && projection) Multiply(fc.world, gameView, fc.modelView);
        memcpy(fc.diffuse, b->diffuse, sizeof(fc.diffuse));
        const float itemAlpha = itemAlpha_[bb->item];
        if (itemAlpha >= 0) fc.diffuse[3] *= itemAlpha;  // Mdl_DrawAlt: material alpha * record alpha
        fc.litDiffuse[3] = b->ambientScale;
        fc.localCount[3] = itemUnitSpecular_[bb->item] ? 1.0f : 0.0f;
        memcpy(fc.matSpecular, b->specular, sizeof(fc.matSpecular));
        if (noSpecular) fc.matSpecular[3] = 0;
        if (!b->tex && !anyLight) fc.diffuse[0] = fc.diffuse[1] = fc.diffuse[2] = 0.7f;
        fc.flags[0] = b->tex ? 1.0f : 0.0f;
        fc.flags[2] = pass == 0 ? -1.0f : 1.0f / 255.0f;  // ALPHATESTENABLE only in pass 1; ALPHAREF 1, GREATEREQUAL
        // Shading 1 (texture alpha only, ALPHAOP SELECTARG1) except on the alpha path: Mdl_DrawAlt 0x004A8440 forces
        // TSP bit 7 (shading 2 / 3: ALPHAOP MODULATE, 0x004A7780), so the record's factor always reaches the alpha
        fc.flags[3] = b->alphaFromTexture && itemAlpha < 0 ? 2.0f : 1.0f;
        fc.fogParams[2] = fog && fog->enabled && b->fog ? 1.0f : 0.0f;
        const GpuState st = itemAlpha >= 0
                                 ? StateOf(true, 5, 6, b->zfunc, b->zwrite, b->linear, b->addrU, b->addrV)  // forced SRCALPHA/INVSRCALPHA
                                 : StateOf(pass == 1, b->srcBlend, b->dstBlend, b->zfunc, b->zwrite, b->linear, b->addrU, b->addrV);
        GpuState cs = st;
        static const bool noMips = getenv("HOTD2_NO_MIPS") != nullptr;
        cs.mipmapped = noMips ? 0 : 1;
        cs.cull = static_cast<uint8_t>(b->cull);
        // D3D culls by the screen winding, mirrored WORLD matrices included. The sky domes of list 0x0057A280 (6048,
        // 6049, 6307, 6309; Sky_Task 0x004132D0 draws them with S(1.2, 1.2, -1.2)) stay visible because Res_LoadModel
        // flips bit 0 of every strip control word when it loads one of them on its own (0x00418B68 -> 0x00419270):
        // CW <-> CCW. The sky 6047 (type 3) is not in the list and is culled completely (attract 8100 / 20700).
        if (itemCullFlip_[bb->item] && cs.cull >= 2) cs.cull = static_cast<uint8_t>(5 - cs.cull);
        AddModelDraw(out, *b, fc, cs);
    };

    // Culling (gameView only). Whole items: not submitted when the model's bounding sphere is completely outside
    // the frustum (HYPOTHESIS: matches the pass-1 lists, see notes). Meshes: Mdl_Draw skips a mesh of the current pass
    // when ComputeSphereVisibility reports it outside, and such a skipped mesh also defers the model to pass 1.
    std::vector<char> itemCulled(itemOrigin_.size(), 0), batchCulled(batches_.size(), 0);
    if (gameView) {
        for (size_t i = 0; i < itemSphere_.size(); ++i)
            itemCulled[i] = itemCullable_[i] && SphereOutside(viewProj, itemSphere_[i].data(), itemSphere_[i][3], false);
        for (size_t i = 0; i < batches_.size(); ++i)
            batchCulled[i] = !batches_[i].noMeshCull && SphereOutside(viewProj, batches_[i].meshCenter, batches_[i].meshRadius, true);
        if (getenv("HOTD2_DEBUG_CULL"))
            for (size_t i = 0; i < batches_.size(); ++i)
                if (batchCulled[i] && !itemCulled[batches_[i].item])
                    fprintf(stderr, "cull item %d batch %zu centre (%.1f %.1f %.1f) r %.1f viewZ %.1f translucent %d\n", batches_[i].item, i,
                            batches_[i].meshCenter[0], batches_[i].meshCenter[1], batches_[i].meshCenter[2], batches_[i].meshRadius,
                            gameView ? batches_[i].meshCenter[0] * gameView[2] + batches_[i].meshCenter[1] * gameView[6] + batches_[i].meshCenter[2] * gameView[10] + gameView[14] : 0.0f,
                            batches_[i].mesh->translucent ? 1 : 0);
    }
    // Pass 0 (0x004A7A10): opaque meshes in submission order.
    for (size_t i = 0; i < batches_.size(); ++i) {
        const Batch& b = batches_[i];
        if (b.mesh->translucent || batchCulled[i] || (b.item >= 0 && itemCulled[b.item])) continue;
        if (b.item >= 0 && itemAlpha_[b.item] >= 0) continue;  // alpha path: pass 1 only (hypothesis)
        drawBatch(&b, 0);
    }

    if (!gameView) {
        // No game view: approximate pass 1 per batch, back to front by clip w (model viewer).
        std::vector<const Batch*> translucent;
        for (const Batch& b : batches_)
            if (b.mesh->translucent) translucent.push_back(&b);
        auto depth = [&](const Batch* b) {
            const auto& w = itemWorld_[b->item];
            const float z = b->mesh->cardZ;
            const float plane[3] = {z * w[8] + w[12], z * w[9] + w[13], z * w[10] + w[14]};
            const float* center = b->mesh->layeredCards ? plane : b->center;
            return center[0] * viewProj[3] + center[1] * viewProj[7] + center[2] * viewProj[11] + viewProj[15];
        };
        std::stable_sort(translucent.begin(), translucent.end(), [&](const Batch* a, const Batch* b) { return depth(a) > depth(b); });
        for (const Batch* b : translucent) drawBatch(b, 1);
        if (sprites && !sprites->empty()) {
            UploadSprites(device_, *sprites);
            for (size_t i = 0; i < sprites->size(); ++i) AddSpriteDraw(out, width, height, (*sprites)[i], i);
        }
        device_->Submit(out);
        return;
    }

    // Pass 1 like Mdl_DrawPass1 0x004A88E0: one list of deferred models (Mdl_Submit 0x004A7E50: key 0x40000000 |
    // priority 8 (0x007E78BC), depth = min of the model-view z of the origin and of every translucent mesh's
    // sphere centre, as Mdl_Draw pass 0 computes it) and sprites (Spr_Submit: their own key/depth), inserted
    // models first, sorted with the CRT qsort and comparator 0x004A8A20.
    const float* V = gameView;
    auto viewZ = [&](const float p[3]) { return p[0] * V[2] + p[1] * V[6] + p[2] * V[10] + V[14]; };
    using Entry = Pass1Entry;
    std::vector<Entry> list;
    std::vector<std::vector<const Batch*>> itemBatches(itemOrigin_.size());  // translucent, visible: drawn in pass 1
    std::vector<float> itemDepth(itemOrigin_.size(), 0);
    std::vector<char> deferred(itemOrigin_.size(), 0);
    for (size_t i = 0; i < itemOrigin_.size(); ++i) itemDepth[i] = viewZ(itemOrigin_[i].data());
    for (size_t i = 0; i < batches_.size(); ++i) {
        const Batch& b = batches_[i];
        if (b.item < 0 || itemCulled[b.item]) continue;
        const bool alphaItem = itemAlpha_[b.item] >= 0;
        if (b.mesh->translucent || batchCulled[i] || alphaItem) {  // skipped in pass 0
            deferred[b.item] = 1;
            itemDepth[b.item] = std::min(itemDepth[b.item], viewZ(b.meshCenter));
            if ((b.mesh->translucent || alphaItem) && !batchCulled[i]) itemBatches[b.item].push_back(&b);
        }
    }
    for (size_t i = 0; i < itemOrigin_.size(); ++i)
        if (deferred[i]) list.push_back({ (itemAlpha_[i] >= 0 ? 0x60000000u : 0x40000000u) | itemPriority_[i], itemDepth[i], static_cast<int>(i), -1 });
    // Smoke and similar billboards contain overlapping alpha cards, sometimes authored
    // front first. Precise depth then hides rear cards and cuts holes through the plume.
    // Sort those cards within the model; keep its place in the mixed pass-1 list and
    // retain depth testing/writes so scenery still occludes the effect correctly.
    for (auto& cards : itemBatches)
        if (cards.size() > 1 && cards.front()->mesh->layeredCards) {
            auto planeDepth = [&](const Batch* b) {
                const auto& w = itemWorld_[b->item];
                const float z = b->mesh->cardZ;
                const float plane[3] = {z * w[8] + w[12], z * w[9] + w[13], z * w[10] + w[14]};
                return viewZ(plane);
            };
            std::stable_sort(cards.begin(), cards.end(), [&](const Batch* a, const Batch* b) {
                return planeDepth(a) < planeDepth(b);
            });
        }
    const bool haveSprites = sprites && !sprites->empty();
    if (haveSprites) {
        UploadSprites(device_, *sprites);
        for (size_t i = 0; i < sprites->size(); ++i) list.push_back({ (*sprites)[i].key, (*sprites)[i].depth, -1, static_cast<int>(i) });
    }
    game::CrtQsort(list.data(), list.size(),
                   [](const Entry& a, const Entry& b) { return game::ComparePass1(a.key, a.depth, b.key, b.depth); });
    // The original's unstable qsort can reverse equal-depth model layers whenever
    // an unrelated HUD sprite blinks. Emperor's core and reflective skin share
    // exact sort depths, so this changes large body patches between blue and grey.
    // Keep authored model submission order for ties. Leave sprite slots and their
    // original CRT tie order intact (menus depend on that compositing order).
    std::vector<Entry> tiedModels;
    for (size_t first = 0; first < list.size();) {
        size_t end = first + 1;
        while (end < list.size() && game::ComparePass1(list[first].key, list[first].depth,
                                                      list[end].key, list[end].depth) == 0) ++end;
        tiedModels.clear();
        if (end - first > 1)
            for (size_t i = first; i < end; ++i)
                if (list[i].item >= 0) tiedModels.push_back(list[i]);
        if (tiedModels.size() > 1) {
            std::sort(tiedModels.begin(), tiedModels.end(), [](const Entry& a, const Entry& b) { return a.item < b.item; });
            size_t next = 0;
            for (size_t i = first; i < end; ++i)
                if (list[i].item >= 0) list[i] = tiedModels[next++];
        }
        first = end;
    }
    // The original near-first list lets one graded-alpha effect write depth
    // before a rear effect contributes. Callers can opt overlapping effects
    // into far-first compositing without changing their depth/blend state or
    // any other model/sprite slot. Camera distance handles off-axis billboards
    // whose view-Z order can reverse while their overlapping planes do not.
    // Priorities remain separate; the original depth keys stay unchanged.
    std::array<std::vector<size_t>,16> alphaSlots;
    for (size_t i=0;i<list.size();++i) {
        const auto& e=list[i];
        if(e.item>=0 && itemSortBackToFront_[e.item]) alphaSlots[e.key&0xF].push_back(i);
    }
    for (const auto& slots:alphaSlots) if(slots.size()>1) {
        std::vector<Entry> effects;
        for(size_t i:slots)effects.push_back(list[i]);
        auto distanceSquared = [&](const Entry& e) {
            const auto& p=itemOrigin_[e.item];
            double distance=0;
            for(int k=0;k<3;++k) {
                const double d=static_cast<double>(p[k])-fc.eyePos[k];
                distance+=d*d;
            }
            return distance;
        };
        std::stable_sort(effects.begin(),effects.end(),[&](const Entry& a,const Entry& b){return distanceSquared(a)>distanceSquared(b);});
        for(size_t i=0;i<slots.size();++i)list[slots[i]]=effects[i];
    }
    lastPass1 = list;
    for (const Entry& e : list) {
        if (e.item >= 0) {
            for (const Batch* b : itemBatches[e.item]) drawBatch(b, 1);
        } else {
            AddSpriteDraw(out, width, height, (*sprites)[e.sprite], e.sprite);
        }
    }
    device_->Submit(out);
}

void ModelRenderer::DrawSprites(int width, int height, const std::vector<SpriteDraw>& sprites) {
    if (sprites.empty()) return;
    UploadSprites(device_, sprites);
    GpuDrawList out;
    out.width = width;
    out.height = height;
    for (size_t i = 0; i < sprites.size(); ++i) AddSpriteDraw(out, width, height, sprites[i], i);
    device_->Submit(out);
}

}  // namespace port
