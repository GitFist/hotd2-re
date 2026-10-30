#include "scene.h"
#include "render/viewport.h"
#include "prototype_model.h"

#include "game/crt_qsort.h"
#include "game/props.h"
#include "game/skin.h"
#include "game/sprite.h"
#include "game/stage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace port {

namespace {

// Applies a game::ModelEdit to a copy of the model. Inline vertex k = the k-th distinct vertex record in file order
// (references point back at earlier inline records, so the sorted distinct offsets are the inline records in order;
// 0x0040F960 counts them the same way while walking the strips).
std::vector<uint32_t> InlineOffsets(const game::Model& m) {
    std::vector<uint32_t> offs;
    for (const game::ModelMesh& mesh : m.meshes)
        for (const game::ModelStrip& s : mesh.strips) offs.insert(offs.end(), s.offsets.begin(), s.offsets.end());
    std::sort(offs.begin(), offs.end());
    offs.erase(std::unique(offs.begin(), offs.end()), offs.end());
    return offs;
}

// Normal of inline vertex k of a model (file data), nullptr if out of range.
const float* InlineNormal(const game::Model& m, int k) {
    const std::vector<uint32_t> offs = InlineOffsets(m);
    if (k < 0 || static_cast<size_t>(k) >= offs.size()) return nullptr;
    for (const game::ModelMesh& mesh : m.meshes)
        for (const game::ModelStrip& s : mesh.strips)
            for (size_t i = 0; i < s.vertices.size() && i < s.offsets.size(); ++i)
                if (s.offsets[i] == offs[static_cast<size_t>(k)]) return s.vertices[i].normal;
    return nullptr;
}

// srcModel(id): the model as loaded from its file (for ModelEdit::normalX), nullptr if unknown
template <class SrcModel>
void ApplyModelEdit(const game::ModelEdit& e, game::Model& m, SrcModel srcModel) {
    if (e.pageBend != INT32_MIN) {
        // 0x004759C0 in the game's 24-bit x87 precision (fsin unrounded)
        const double s = 9.587379924285257e-05;  // 0x004C4370
        const double sa = std::sin(static_cast<double>(static_cast<float>(static_cast<double>(e.pageBend) * s)));
        const float sa4 = static_cast<float>(sa * 4.0);  // 0x004C4CA8
        for (game::ModelMesh& mesh : m.meshes)
            for (game::ModelStrip& st : mesh.strips)
                for (game::ModelVertex& v : st.vertices) {
                    uint32_t xb;
                    memcpy(&xb, &v.pos[0], 4);
                    if (!(xb & 1)) continue;
                    const int k = static_cast<int>(static_cast<float>(v.pos[0] * 2058.112060546875f));  // 0x0055DD14, _ftol
                    const double sk = std::sin(static_cast<double>(static_cast<float>(static_cast<double>(k) * s)));
                    const float p = static_cast<float>(static_cast<double>(sa4) * sk);
                    v.pos[2] = static_cast<float>(4.8000001697801054e-05 - static_cast<double>(p));  // 0x00569190
                }
    }
    if (!e.normalX.empty()) {
        const std::vector<uint32_t> offs = InlineOffsets(m);
        for (const game::ModelEdit::NormalX& n : e.normalX) {
            if (n.index < 0 || static_cast<size_t>(n.index) >= offs.size()) continue;
            uint32_t bits = 0;  // srcModel 0: the zero-initialised cache
            if (n.srcModel != 0) {
                const game::Model* src = srcModel(n.srcModel);
                const float* nrm = src ? InlineNormal(*src, n.srcIndex) : nullptr;
                if (!nrm) continue;
                memcpy(&bits, &nrm[2], 4);
            }
            bits |= 1;
            float x;
            memcpy(&x, &bits, 4);
            for (game::ModelMesh& mesh : m.meshes)
                for (game::ModelStrip& s : mesh.strips)
                    for (size_t i = 0; i < s.vertices.size() && i < s.offsets.size(); ++i)
                        if (s.offsets[i] == offs[static_cast<size_t>(n.index)]) s.vertices[i].normal[0] = x;
        }
    }
    if (!e.vertices.empty()) {
        const std::vector<uint32_t> offs = InlineOffsets(m);
        std::vector<std::pair<uint32_t, const float*>> moved;
        for (const game::ModelEdit::Vertex& v : e.vertices)
            if (v.index >= 0 && static_cast<size_t>(v.index) < offs.size()) moved.push_back({ offs[static_cast<size_t>(v.index)], v.pos });
        for (game::ModelMesh& mesh : m.meshes)
            for (game::ModelStrip& s : mesh.strips)
                for (size_t i = 0; i < s.vertices.size() && i < s.offsets.size(); ++i)
                    for (const auto& [o, p] : moved)
                        if (s.offsets[i] == o) memcpy(s.vertices[i].pos, p, sizeof(float) * 3);
    }
    if (e.diffuse >= 0.0f) {
        uint32_t bits;
        memcpy(&bits, &e.diffuse, 4);
        for (game::ModelMesh& mesh : m.meshes)
            for (int k = 12; k <= 14; ++k) mesh.material.words[k] = bits;
    }
    if (e.diffuseRgb)
        for (game::ModelMesh& mesh : m.meshes)
            for (int channel = 0; channel < 3; ++channel)
                memcpy(&mesh.material.words[12 + channel], &e.color[channel], sizeof(float));
    if(e.clearSpecular)
        for(game::ModelMesh& mesh:m.meshes)mesh.material.words[9]=0;
    if (e.tspOr)
        for (game::ModelMesh& mesh : m.meshes) mesh.material.words[2] |= e.tspOr;
    if (!e.uvs.empty()) {
        const std::vector<uint32_t> offs = InlineOffsets(m);
        std::unordered_map<uint32_t, const float*> at;
        for (const game::ModelEdit::Uv& u : e.uvs)
            if (u.index >= 0 && static_cast<size_t>(u.index) < offs.size()) at[offs[static_cast<size_t>(u.index)]] = u.uv;
        for (game::ModelMesh& mesh : m.meshes)
            for (game::ModelStrip& s : mesh.strips)
                for (size_t i = 0; i < s.vertices.size() && i < s.offsets.size(); ++i) {
                    auto it = at.find(s.offsets[i]);
                    if (it != at.end()) memcpy(s.vertices[i].uv, it->second, sizeof(float) * 2);
                }
    }
}

}  // namespace

bool SceneDef::Load(const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    for (std::string line; std::getline(f, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string kind;
        ss >> kind;
        if (kind == "camera") {
            ss >> cameraId;
        } else if (kind == "stage") {
            std::string sec;
            ss >> stage >> sec >> section;
        } else if (kind == "light") {
            std::string env;
            ss >> env >> lightPitch >> lightYaw;
            hasLight = true;
        } else if (kind == "env") {
            std::string from, to;
            ss >> envTable;
            if (envTable == "default") continue;  // environment defaults, no script ops, no fog
            ss >> from >> to;
            envFrom = static_cast<uint32_t>(std::stoul(from, nullptr, 0));
            envTo = static_cast<uint32_t>(std::stoul(to, nullptr, 0));
        } else if (kind == "overlay") {
            std::string what, key;
            ss >> what;
            if (what == "title") overlayTitle = true;
            else if (what == "press_start") {
                overlayPressStart = true;
                if (ss >> key && key == "phase") ss >> pressStartPhase;
            }
        } else if (kind == "light_color") {
            ss >> lightDiffuse >> lightAmbient >> ambientRS;
        } else if (kind == "static") {
            int id;
            ss >> id;
            statics.push_back(id);
        } else if (kind == "world") {
            useWorld = true;
            std::string first;
            ss >> first;
            if (first == "external") externalWorld = true;  // the program owns and steps the world (play)
            else {
                worldEventMode = std::stoi(first);
                ss >> worldStart >> worldSerial;
            }
        } else if (kind == "sky_task") {
            ss >> skyType >> skyFrame0;
            if (ss >> skyYaw0) skyYaw0Set = true;
        } else if (kind == "sky") {
            Sky s{};
            ss >> s.modelId;
            for (std::string key; ss >> key;) {
                if (key == "scale") ss >> s.scale;
                else if (key == "yaw") ss >> s.yaw0;
                else if (key == "yaw_per_frame") ss >> s.yawPerFrame;
            }
            skies.push_back(s);
        }
    }
    return cameraId >= 0 || useWorld;
}

bool Scene::Init(AssetStore& assets, const SceneDef& def, std::string* error) {
    renderPose_.Reset();
    assets_ = &assets;
    def_ = def;
    auto load = [&](int id, std::vector<Item>& out) {
        Item it;
        if (!assets.ModelById(id, it.model, &it.polName)) {
            if (error) *error = "cannot load model " + std::to_string(id);
            return false;
        }
        out.push_back(std::move(it));
        return true;
    };
    std::vector<int> statics = def.statics;
    staticPriority_.assign(statics.size(), 8u);
    if (def.stage >= 0) {
        game::StageSections sections;
        if (!sections.Load(assets.Exe(), def.stage)) {
            if (error) *error = "cannot load stage section tables";
            return false;
        }
        for (int o : sections.sections.at(def.section)) {
            statics.push_back(sections.objects.at(o).first);
            staticPriority_.push_back(sections.objects.at(o).second == 2 ? 7u : 8u);  // Stage_DrawSection draw mode 2
        }
    }
    for (int id : statics)
        if (!load(id, statics_)) return false;
    staticIds_ = statics;
    for (const auto& s : def.skies)
        if (!load(s.modelId, skies_)) return false;
    if (def.skyType >= 0) {
        skyTable_ = game::LoadSkyTable(assets.Exe());
        if (def.skyType >= static_cast<int>(skyTable_.size())) {
            if (error) *error = "sky type out of range";
            return false;
        }
        const auto& e = skyTable_[def.skyType];
        for (int id : { static_cast<int>(e.model), static_cast<int>(e.model2) }) {
            if (id == 0) continue;
            std::vector<Item> tmp;
            if (!load(id, tmp)) return false;
            skyModels_[id] = std::move(tmp[0]);
        }
    }

    if (def.useWorld && !def.externalWorld) {
        world_ = std::make_unique<game::World>();
        if (!world_->Load(assets.Dir().string(), def.worldEventMode, error)) return false;
    }
    for (const auto& f : game::ListCameraFiles(assets.Exe())) {
        for (size_t k = 0; k < f.cameraIds.size(); ++k) {
            if (f.cameraIds[k] != def.cameraId) continue;
            camera_.Load(assets.File("cam/" + f.name), static_cast<int>(f.cameraIds.size()));
            cameraIndex_ = static_cast<int>(k);
        }
    }
    if (cameraIndex_ < 0 && !def.useWorld) {
        if (error) *error = "camera " + std::to_string(def.cameraId) + " not found";
        return false;
    }
    duration_ = cameraIndex_ >= 0 ? camera_.Duration(cameraIndex_) : 100000.0f;
    int lightPitch = def.lightPitch, lightYaw = def.lightYaw;
    bool hasLight = def.hasLight;
    if (!def.envTable.empty()) {
        // Environment from the event script: run the ops 0x18 / 0x20 of [envFrom, envTo) in sequence.
        game::EventMemory mem;
        mem.Load(game::EventMemory::kCommonBase, assets.File("evt/comevtbl.bin"));
        mem.Load(game::EventMemory::kStageBase, assets.File("evt/" + def.envTable));
        game::Environment env;
        game::EventOp op;
        const bool script = def.envTable != "default";
        for (uint32_t a = def.envFrom; script && a < def.envTo; a += op.length) {
            if (!game::DecodeEvent(mem, a, op) || op.length == 0) {
                if (error) *error = "cannot decode event at " + std::to_string(a);
                return false;
            }
            env.Apply(mem, op);
        }
        hasLight = env.hasLightAngles || !script;  // the title lights with angles 0, 0 (captured)
        lightPitch = env.lightPitch;
        lightYaw = env.lightYaw;
        float d[3], amb[3];
        env.LightDiffuse(d);
        env.LightAmbient(amb);
        const uint32_t rs = env.AmbientRenderState();
        for (int k = 0; k < 3; ++k) lightColor_[k] = env.lightColor[k];
        for (int k = 0; k < 3; ++k) {
            lighting.diffuse[k] = d[k];
            lighting.ambient[k] = amb[k];
            lighting.ambientRS[k] = ((rs >> (16 - 8 * k)) & 0xFF) / 255.0f;
            fog.color[k] = env.fogColor[k] / 255.0f;
        }
        // env +0x38 != 0 in both captures (writer not traced). Without a script, fog is left off: the title ran with
        // FOGSTART 131070 (env near 65535 / far 65536, writer not traced), i.e. no visible fog.
        fog.enabled = script;
        fog.start = env.FogStart();
        fog.end = env.FogEnd();
    } else {
        for (int k = 0; k < 3; ++k) {
            lighting.diffuse[k] = def.lightDiffuse;
            lighting.ambient[k] = def.lightAmbient;
            lighting.ambientRS[k] = def.ambientRS;
        }
    }
    if (hasLight) {
        // 0x0040E0B0: environment direction = (0,0,1) * Rx(pitch) * Ry(yaw); 0x004AA0E0 sets the D3D light
        // direction to its negation, so the direction toward the light is the environment direction itself.
        const game::Mat4 r = game::Mat4::RotateX(lightPitch) * game::Mat4::RotateY(lightYaw);
        lighting.enabled = true;
        for (int k = 0; k < 3; ++k) lighting.dirToLight[k] = r.m[8 + k];
    }
    return true;
}

bool Scene::PrepareWorld(ModelRenderer& renderer, const game::World& world) {
    const int mode = world.GameMode(), chapter = world.Hud().titleChapter;
    const bool changed = mode != cacheMode_ || chapter != cacheChapter_ || world.Frame() < cacheFrame_;
    cacheMode_ = mode; cacheChapter_ = chapter; cacheFrame_ = world.Frame();
    if (!changed) return false;
    renderPose_.Reset();
    renderer.ClearCache();
    worldModels_.clear(); uvScroll_.clear(); skinned_.clear(); missingModels_.clear();
    assets_->ClearCache();
    return true;
}

bool Scene::BuildWorld(ModelRenderer& renderer, float t, int frame, game::Mat4* viewProj) {
    const int target = frame >= 0 ? frame : def_.worldStart + static_cast<int>(t);
    if (target < def_.worldStart) return false;
    if (worldFrame_ < 0 || target < worldFrame_) {
        game::WorldConfig cfg;
        cfg.eventMode = def_.worldEventMode;
        cfg.firstSerial = def_.worldSerial;
        cfg.startFrame = def_.worldStart;
        world_->StartStage(cfg);
        worldFrame_ = def_.worldStart;
        pendingSounds_.clear();
        for (const auto& s : world_->Sounds()) pendingSounds_.push_back(s);
    }
    while (worldFrame_ < target) {
        world_->Step();
        ++worldFrame_;
        for (const auto& s : world_->Sounds()) pendingSounds_.push_back(s);
    }
    *viewProj = BuildFromWorld(renderer, *world_);
    return true;
}

ModelRenderer::Lighting Scene::MakeLighting(const game::WorldLight& l, const game::WorldLights& frame) {
    ModelRenderer::Lighting out;
    out.enabled = true;
    if (!l.table) {
        // 0x004AA120: AMBIENT trunc(255 i c), light 0 directional, diffuse 1.4 i c, ambient 0.3 c; direction toward the
        // light = (0, 0, 1) Rx(pitch) Ry(yaw) (0x0040E0B0, negated by 0x004AA0E0 into the D3D direction)
        float d[3], a[3];
        game::LightDiffuseOf(l.color, l.intensity, d);
        game::LightAmbientOf(l.color, a);
        const uint32_t rs = game::AmbientRenderStateOf(l.color, l.intensity);
        const game::Mat4 r = game::Mat4::RotateX(l.pitch) * game::Mat4::RotateY(l.yaw);
        for (int k = 0; k < 3; ++k) {
            out.diffuse[k] = d[k];
            out.ambient[k] = a[k];
            out.ambientRS[k] = ((rs >> (16 - 8 * k)) & 0xFF) / 255.0f;
            out.dirToLight[k] = r.m[8 + k];
        }
        return out;
    }
    // 0x004AA8B0: AMBIENT 0x007E8138 (0x004AA720: trunc(255 v)), only the table's lights
    for (int k = 0; k < 3; ++k) {
        out.diffuse[k] = out.ambient[k] = 0;
        out.ambientRS[k] = static_cast<float>(static_cast<int>(frame.tableAmbient[k] * 255.0f) & 0xFF) / 255.0f;
    }
    for (const auto& s : frame.table) {
        if (out.localCount >= kMaxLocalLights || (s.type != 1 && s.type != 2)) continue;  // (no directional slots seen)
        ModelRenderer::Lighting::Local& o = out.local[out.localCount++];
        o.type = s.type;
        for (int k = 0; k < 3; ++k) {
            o.diffuse[k] = s.diffuse[k];
            o.pos[k] = s.pos[k];
            o.dir[k] = s.dir[k];
            o.att[k] = s.att[k];
        }
        o.range = s.range;
        o.falloff = s.falloff;
        o.theta = s.theta;
        o.phi = s.phi;
    }
    return out;
}

game::Mat4 Scene::BuildFromWorld(ModelRenderer& renderer, const game::World& world, const std::vector<game::WorldDraw>* drawsOverride) {
    const game::WorldCamera& cam = world.Camera();
    const auto& events = world.Events();
    TemporalSceneStamp temporal;
    temporal.world=reinterpret_cast<uintptr_t>(&world);
    temporal.simulationFrame=world.Frame();
    temporal.camera={world.GameMode(),events.mode,events.scene,events.variant,
                     events.camera.camId,events.camera.activeCtx,events.camera.modeCur,events.camera.subCur};
    renderer.SetTemporalScene(temporal);
    renderer.SetCinematicEffectsAllowed(world.GameMode()==6&&world.Hud().skippable&&!world.Hud().canShoot&&!world.Hud().paused);
    view = cam.view;
    for (int k = 0; k < 3; ++k) pos[k] = cam.pos[k];
    pitch = cam.pitch;
    yaw = cam.yaw;
    // The world's environment (env task 0x0040AD90, camera task light direction): fog and the frame light; every item
    // gets the light its submission snapshotted (identical lights share an entry).
    const game::WorldLights& fl = world.Lights();
    itemLights.clear();
    std::vector<game::WorldLight> lightKeys;
    auto lightIndex = [&](const game::WorldLight& l) {
        for (size_t i = 0; i < lightKeys.size(); ++i) {
            const game::WorldLight& k = lightKeys[i];
            if (k.table == l.table && (l.table || (k.pitch == l.pitch && k.yaw == l.yaw && k.intensity == l.intensity &&
                                                   k.color[0] == l.color[0] && k.color[1] == l.color[1] && k.color[2] == l.color[2])))
                return static_cast<int>(i);
        }
        lightKeys.push_back(l);
        itemLights.push_back(MakeLighting(l, fl));
        return static_cast<int>(itemLights.size()) - 1;
    };
    if (fl.valid && worldEnv) {
        const game::Environment& env = world.Env();
        game::WorldLight frameLight;
        frameLight.pitch = env.lightPitch;
        frameLight.yaw = env.lightYaw;
        frameLight.intensity = env.lightIntensity;
        for (int k = 0; k < 3; ++k) frameLight.color[k] = env.lightColor[k];
        lighting = MakeLighting(frameLight, fl);
        for (int k = 0; k < 3; ++k) {
            fog.color[k] = fl.fogColor[k] / 255.0f;
            lightColor_[k] = env.lightColor[k];
        }
        fog.enabled = fl.fogOn;
        fog.start = fl.fogStart;
        fog.end = fl.fogEnd;
    }
    std::vector<ModelRenderer::SceneItem> items;
    itemIds_.clear();
    skinned_.clear();
    if (skinDefs_.empty()) skinDefs_ = game::SkinDefsByModel(assets_->Exe());
    std::map<int, game::Model*> waved;  // models 0x1731..0x1734 with this frame's vertex wave
    std::map<MotionIdentity,uint32_t> motionOccurrences;
    for (const game::WorldDraw& d : drawsOverride ? *drawsOverride : world.Draws()) {
        auto it = worldModels_.find(d.model);
        if (it == worldModels_.end()) {
            Item m;
            if (!PrototypeModel(*assets_, d.model, m.model, m.polName) && !assets_->ModelById(d.model, m.model, &m.polName)) {
                missingModels_.push_back(d.model);
                worldModels_[d.model] = Item{};  // remember the miss (empty model)
                continue;
            }
            it = worldModels_.emplace(d.model, std::move(m)).first;
        }
        if (it->second.model.meshes.empty()) continue;
        ModelRenderer::SceneItem si;
        // Task serial + model/node/role identify actual submissions; an unrelated
        // blinking sprite or a removed enemy never shifts another actor's history.
        si.motionTracked=true;
        si.motionIdentity.task=d.task; si.motionIdentity.model=static_cast<uint32_t>(d.model);
        si.motionIdentity.node=static_cast<uint32_t>(d.node);
        si.motionIdentity.role=(d.viewSpace?1u:0u)|(d.alt?2u:0u)|(d.cullFlip?4u:0u)|(static_cast<uint32_t>(d.drawMode)<<8);
        si.motionIdentity.occurrence=motionOccurrences[si.motionIdentity]++;
        si.motionReactive=d.envMap || d.uvScroll>=0;
        // Sky_Task 0x004132D0, camera overlays, authored special shadows and
        // effect-priority models must not become solid environmental occluders.
        si.shadowCaster=!d.viewSpace && d.fn!=0x004132D0 && !d.cullFlip && d.drawMode<=8;
        si.model = &it->second.model;
        si.polName = it->second.polName;
        // viewSpace draws hand over the model-view matrix: world = model-view * camera matrix (inverse view).
        game::Mat4 mv = d.world;
        if(d.viewSpace&&d.model==0x93E&&ViewportXScale(viewportAspect_)!=1.f)
            for(int row=0;row<4;++row)mv.m[row*4]/=ViewportXScale(viewportAspect_);
        if (d.viewSpace && d.model == 0x93E && d.alpha >= 0 && mv.m[14] < 0 && mv.m[14] > -0.8f) {
            // nearer than the port's fixed near plane (0.8): the original moves its own near plane for such draws (the
            // pause overlay: 1e-5 by 0x004194C0). Scaling about the eye keeps the projected image: z to -1.
            const float k = -1.0f / mv.m[14];
            for (int i = 0; i < 16; ++i)
                if (i % 4 != 3) mv.m[i] *= k;
        }
        const game::Mat4 w = d.viewSpace ? mv * cam.inverse : d.world;
        memcpy(si.transform, w.m, sizeof(si.transform));
        si.cullable = d.cull;
        si.priority = d.drawMode;
        si.alpha = d.alpha;
        si.unitSpecular = d.unitSpecular;
        si.authoredMinification=world.Hud().nightShiftPage==12;
        si.sortBackToFront = d.sortBackToFront;
        si.cullFlip = d.cullFlip;
        // the pause overlay (above): scaled to the near plane, its material sphere misses the frustum test; never cull it
        si.noMeshCull = d.viewSpace && d.model == 0x93E && d.alpha >= 0;
        if (d.skinCount > 0) {
            // Skinned attachment: deform a copy (game::SkinDeform, as 0x004198B0 / 0x00419980 do before drawing).
            ++skinnedDraws_;
            const int skinModel = d.skinModel != 0 ? d.skinModel : d.model;
            auto def = skinDefs_.find(skinModel);
            bool twin = false;
            if (def == skinDefs_.end()) {
                // skeleton 0x4C attachments 0..5: the twin 0x004EDA52[i] takes the deformation of 0x004EDA50[i] with the
                // normals negated (0x0041A520 / 0x0041A580: the twin sits 0x82C0 bytes further in boss6.bin)
                for (uint32_t i = 0; i < 6 && !twin; ++i)
                    if (assets_->Exe().S16(0x004EDA52 + i * 4) == skinModel) {
                        def = skinDefs_.find(assets_->Exe().S16(0x004EDA50 + i * 4));
                        twin = def != skinDefs_.end();
                    }
            }
            if (def != skinDefs_.end()) {
                game::SkinDef sd;
                if (game::LoadSkinDef(assets_->Exe(), def->second, sd)) {
                    skinned_.push_back(std::make_unique<game::Model>(it->second.model));
                    game::SkinDeform(sd, skinned_.back()->blockStart, d.world, d.skin, d.skinCount, *skinned_.back(), twin);
                    si.model = skinned_.back().get();
                    si.dynamicGeometry = true;
                }
            }
        }
        static const bool waveOff = getenv("HOTD2_WAVE_OFF") != nullptr;  // comparisons: draw the models unedited
        if (d.wavePhase >= 0 && d.skinCount == 0 && !waveOff) {
            // Decoration vertex wave (0x0046DAB0 edits the loaded model; game::DecorWave on one copy per frame)
            game::Model*& wm = waved[d.model];
            if (!wm) {
                skinned_.push_back(std::make_unique<game::Model>(it->second.model));
                if (!game::DecorWave(*skinned_.back(), d.model - 0x1731, d.wavePhase)) {
                    static bool warned = false;
                    if (!warned) fprintf(stderr, "scene: model %d has a non-reference strip (vertex wave walk differs)\n", d.model);
                    warned = true;
                }
                wm = skinned_.back().get();
            }
            si.model = wm;
            si.dynamicGeometry = true;
        }
        static const bool scrollOff = getenv("HOTD2_SKYSCROLL_OFF") != nullptr;
        if (d.uvScroll >= 0 && !scrollOff) {
            // Sky panel uv scroll 0x00413560 (24-bit x87): per run every inline v -= 0.005 (0x004E30E0); the run after
            // one result reached <= -2.995 (0x004E30DC) also adds 3 (0x004C49C0); stored with bit 0 set
            UvScrollState& u = uvScroll_[d.model];
            if (!u.model || u.steps > d.uvScroll) {
                u = UvScrollState{};
                u.model = std::make_unique<game::Model>(it->second.model);
                const std::vector<uint32_t> offs = InlineOffsets(*u.model);
                u.v.assign(offs.size(), 0.0f);
                u.uses.assign(offs.size(), {});
                for (game::ModelMesh& mesh : u.model->meshes)
                    for (game::ModelStrip& s : mesh.strips)
                        for (size_t i = 0; i < s.vertices.size() && i < s.offsets.size(); ++i) {
                            const size_t k = static_cast<size_t>(std::lower_bound(offs.begin(), offs.end(), s.offsets[i]) - offs.begin());
                            u.v[k] = s.vertices[i].uv[1];
                            u.uses[k].push_back(&s.vertices[i]);
                        }
            }
            for (; u.steps < d.uvScroll; ++u.steps) {
                const bool wrap = u.wrap;
                u.wrap = false;
                for (float& v : u.v) {
                    float x = v - 0.004999999888241291f;
                    if (x <= -2.994999885559082f) u.wrap = true;
                    if (wrap) x = x + 3.0f;
                    uint32_t b;
                    memcpy(&b, &x, 4);
                    b |= 1;
                    memcpy(&v, &b, 4);
                }
            }
            for (size_t k = 0; k < u.v.size(); ++k)
                for (game::ModelVertex* p : u.uses[k]) p->uv[1] = u.v[k];
            si.model = u.model.get();
            si.dynamicGeometry = true;
        }
        if (d.edit) {
            // In-memory model edits (bat / leech segments, 0x0040F960): seam vertices moved, material diffuse set.
            skinned_.push_back(std::make_unique<game::Model>(*si.model));
            ApplyModelEdit(*d.edit, *skinned_.back(), [&](int id) -> const game::Model* {
                auto src = worldModels_.find(id);
                if (src == worldModels_.end()) {
                    Item m;
                    if (!assets_->ModelById(id, m.model, &m.polName)) return nullptr;
                    src = worldModels_.emplace(id, std::move(m)).first;
                }
                return src->second.model.meshes.empty() ? nullptr : &src->second.model;
            });
            si.model = skinned_.back().get();
            si.dynamicGeometry = true;
        }
        if (d.envMap) {
            // 0x004AA400 (via 0x00418660): the uv of the inline vertices of the strips with ctrl 0x100 rewritten from the
            // normals and the matrix-stack top (the model-view): u = (1 - n.col0) * 0.5, v = (n.col1 + 1) * 0.5
            const game::Mat4 m = d.viewSpace ? mv : d.world * cam.view;
            if (!(si.dynamicGeometry && !skinned_.empty() && si.model == skinned_.back().get()))
                skinned_.push_back(std::make_unique<game::Model>(*si.model));
            game::Model& em = *skinned_.back();
            std::vector<uint32_t> offs;
            for (const game::ModelMesh& mesh : em.meshes)
                for (const game::ModelStrip& s : mesh.strips)
                    if (s.ctrl & 0x100) offs.insert(offs.end(), s.offsets.begin(), s.offsets.end());
            std::sort(offs.begin(), offs.end());
            const float* t = m.m;
            for (game::ModelMesh& mesh : em.meshes)
                for (game::ModelStrip& s : mesh.strips)
                    for (size_t i = 0; i < s.vertices.size() && i < s.offsets.size(); ++i) {
                        if (!std::binary_search(offs.begin(), offs.end(), s.offsets[i])) continue;
                        const float* n = s.vertices[i].normal;
                        s.vertices[i].uv[0] = (1.0f - ((t[0] * n[0] + t[8] * n[2]) + t[4] * n[1])) * 0.5f;  // 0x004C43AC
                        s.vertices[i].uv[1] = (((t[1] * n[0] + t[9] * n[2]) + t[5] * n[1]) + 1.0f) * 0.5f;
                    }
            si.model = &em;
            si.dynamicGeometry = true;
        }
        if (d.alt) ++altDraws_;  // alt select 0x004185E0: the light table (models: same id)
        if (fl.valid && worldEnv) si.light = lightIndex(d.light);
        items.push_back(si);
        itemIds_.push_back(d.model);
    }
    // Simulation matrices, input rays, source models and materials remain
    // current. Only the optional render copies/camera use the completed pair.
    if (drawsOverride || world.Hud().paused) renderPose_.Reset();
    else renderPose_.Apply(items, temporal, cam.inverse, &view, pos);
    renderer.SetScene(*assets_, items);
    // Proj_Setup 0x004184C0: the projection translated by the centre 0x009C7300 / 0x009C7304 (post-projection, row
    // vectors: x_clip += dx * w)
    float cx = 320.0f, cy = 240.0f;
    world.ProjCenter(&cx, &cy);
    game::Mat4 shift = game::Mat4::Identity();
    shift.m[12] = cx * 0.0031250000465661287f - 1.0f;  // 0x004ED1DC
    shift.m[13] = 1.0f - cy * 0.004166666883975267f;   // 0x004ED1D8
    auto deviceProjection = game::DeviceProjection();
    if (world.PortTypingActive() || world.PortTypingReportActive() || world.Hud().nightShiftPage==12) {
        // English PC487710 ->4C18C0: the same FOV, with near .5 / far8000.
        // The authored30.2-depth book lettering must lie in front of its
        // seventeen-unit pages; the gun game's .8 near plane reverses them.
        deviceProjection.m[10] = 8000.f / (8000.f - .5f);
        deviceProjection.m[14] = -.5f * deviceProjection.m[10];
    }
    projection = game::DeviceView() * deviceProjection * shift;
    projection = HorPlusProjection(projection,viewportAspect_);
    return view * projection;
}

game::Mat4 Scene::Build(ModelRenderer& renderer, float t, int frame) {
    if (world_) {
        game::Mat4 vp;
        if (BuildWorld(renderer, t, frame, &vp)) return vp;
    }    int roll = 0;
    camera_.Evaluate(cameraIndex_, t, pos, target, &roll);
    game::LookAngles(pos, target, &pitch, &yaw);
    view = game::ViewMatrix(pos, pitch, yaw, 0);  // roll flag was 0 in every capture

    std::vector<ModelRenderer::SceneItem> items;
    std::vector<int> skyIds;
    if (def_.skyType >= 0) {
        // Sky_Task 0x004132D0 via game::Sky: one update from the type's start, then the yaw set for this frame.
        const auto& e = skyTable_[def_.skyType];
        const int f = frame >= 0 ? frame : def_.skyFrame0 + static_cast<int>(t);
        const int yaw = (def_.skyYaw0Set ? def_.skyYaw0 : e.yawInit) + e.yawStep * (f - def_.skyFrame0);
        game::Mat4 rot = game::Mat4::RotateY(yaw);
        const game::Mat4 tr = game::Mat4::Translate(pos[0], e.yOffset + pos[1], pos[2]);
        const game::Mat4 s = game::ScaleMatrix(1.2f, 1.2f, -1.2f);
        const std::pair<int, game::Mat4> draws[2] = { { e.model, s * rot * tr }, { e.model2, tr } };
        for (const auto& [id, w] : draws) {
            auto it = skyModels_.find(id);
            if (id == 0 || it == skyModels_.end()) continue;
            ModelRenderer::SceneItem si;
            si.model = &it->second.model;
            si.polName = it->second.polName;
            si.cullable = false;
            si.cullFlip = game::CullFlipModel(assets_->Exe(), id);  // the skies are loaded on their own
            memcpy(si.transform, w.m, sizeof(si.transform));
            items.push_back(si);
            skyIds.push_back(id);
        }
    }    for (size_t i = 0; i < skies_.size(); ++i) {
        const auto& s = def_.skies[i];
        const int a = static_cast<int>(s.yaw0 + s.yawPerFrame * t);
        const game::Mat4 m = game::Mat4::RotateY(a) * game::Mat4::Translate(0, 0, 0);
        game::Mat4 w = m;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) w.m[r * 4 + c] *= s.scale;
        w.m[12] = pos[0];
        w.m[13] = pos[1];
        w.m[14] = pos[2];
        ModelRenderer::SceneItem it;
        it.model = &skies_[i].model;
        it.cullable = false;
        it.cullFlip = game::CullFlipModel(assets_->Exe(), s.modelId);
        it.polName = skies_[i].polName;
        memcpy(it.transform, w.m, sizeof(it.transform));
        items.push_back(it);
    }
    for (size_t i = 0; i < statics_.size(); ++i) {
        const auto& st = statics_[i];
        ModelRenderer::SceneItem it;
        it.model = &st.model;
        it.polName = st.polName;
        it.priority = staticPriority_[i];
        items.push_back(it);
    }
    itemIds_ = skyIds;
    for (const auto& s : def_.skies) itemIds_.push_back(s.modelId);
    for (int id : staticIds_) itemIds_.push_back(id);
    renderer.SetScene(*assets_, items);
    projection = game::DeviceView() * game::DeviceProjection();
    return view * projection;
}

std::vector<ModelRenderer::SpriteDraw> Scene::BuildSprites(int frame) {
    std::vector<game::SpriteDef> defs;
    // Insertion order as captured in the pass-1 lists: title layout task first, then the HUD's PRESS START.
    if (def_.overlayTitle)
        for (const auto& d : game::TitleLogoSprites()) defs.push_back(d);
    if (def_.overlayPressStart && game::PressStartVisible(static_cast<uint32_t>(frame + def_.pressStartPhase)))
        defs.push_back(game::PressStartSprite());
    return MakeSpriteDraws(defs);
}

std::vector<ModelRenderer::SpriteDraw> Scene::MakeSpriteDraws(const std::vector<game::SpriteDef>& defs, float zNear,
                                                             const std::vector<uint8_t>* texFiles) {
    game::SpriteTables tables;
    tables.Load(assets_->Exe());
    struct Item {
        game::SpriteRecord rec;
        int file;
        bool typing;
    };
    std::vector<Item> items;
    for (const auto& d : defs) {
        int file = 0;
        uint32_t tex = 0;
        if (d.id >= 0x4000) {
            if (!assets_->SourceSprite(d.id, &file, &tex)) continue;
        } else {
            file = tables.TextureFile(d.id);
            tex = tables.TextureId(d.id);
        }
        if (texFiles && file >= 0 && file < static_cast<int>(texFiles->size()) && (*texFiles)[static_cast<size_t>(file)] != 4)
            continue;  // Spr_Draw 0x0041C630: the sprite's texture file is not loaded (0x009C7328[file * 12] != 4)
        items.push_back({ game::MakeSpriteRecord(d, tex, d.tinted ? d.tint : lightColor_), file, d.id >= 0x4000 });
    }
    // Submission order; ModelRenderer::DrawGame sorts them into the pass-1 list together with the translucent models.
    std::vector<ModelRenderer::SpriteDraw> out;
    for (const auto& it : items) {
        ModelRenderer::SpriteDraw s;
        s.texture = assets_->TextureById(it.file, it.rec.texture & 0x7FFFFFFF);
        if (!s.texture) continue;
        // z = f / (f - n) - n f / ((f - n) depth): near 0.8 gives (1.0001, -0.80008), as captured in play.
        const float zA = zNear == 0.8f ? 1.0001f : 8000.8f / (8000.8f - zNear);
        const float zB = zNear == 0.8f ? -0.80008f : -zNear * zA;
        if (it.typing) game::TypingSpriteQuad(it.rec, s.texture->LogicalWidth(), s.texture->LogicalHeight(), s.v);
        else game::SpriteQuad(it.rec, s.texture->LogicalWidth(), s.texture->LogicalHeight(), zA, zB, s.v);  // the game's size (asset packs resize)
        if(ViewportXScale(viewportAspect_)!=1.f)
            for(auto& v:s.v)v.x=CenteredViewportX(v.x,viewportAspect_);
        s.isp = game::SpriteIsp(it.rec);
        s.key = it.rec.key | 0xF;  // list priority 0x007E78B8 (0xF in every captured list)
        s.depth = it.rec.depth;
        s.textureId = it.rec.texture;
        s.tsp = game::SpriteTsp(it.rec);
        out.push_back(s);
    }
    return out;
}

void Scene::SetViewportAspect(float aspect) {viewportAspect_=ViewportAspect(aspect);}

}  // namespace port
