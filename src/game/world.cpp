#include "game/world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>

#include "game/model.h"
#include "game/mstack.h"
#include "game/prs.h"
#include "game/world_internal.h"
#include "game/world_modes.h"
#include "game/world_prototype.h"

namespace game {

namespace {

namespace fs = std::filesystem;

thread_local World::Impl* g_cur = nullptr;  // the world whose tasks are running (task functions have no context)

std::vector<uint8_t> ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

float F32(const ExeImage& exe, uint32_t va) {
    float f = 0;
    exe.Read(va, &f, 4);
    return f;
}

// Task trampolines (Task_Run calls fn(task); the original passes the task block).
template <void (World::Impl::*M)(Task&)> void Tramp(Task& t) {
    static const bool dbg = getenv("WORLD_TRACE_TASKS") != nullptr;
    if (dbg) fprintf(stderr, "%d task %u fn %X\n", g_cur->frame, t.serial, t.data ? static_cast<WTask*>(t.data.get())->fn : 0);
    (g_cur->*M)(t);
}

int PropRand() { return g_cur->rng.Next(); }  // PropWorld::rand (sites set by the caller)

struct HangingTask : ActorTask {
    HangingProp p;
    const int* Rot64() const override { return p.rot; }
};
struct BreakableTask : ActorTask {
    BreakableProp p;
};
struct DecorTask : WTask {
    DecorKind45 p;
};
struct AnimatedTask : ActorTask {
    AnimatedProp p;
};
struct EffectTask : WTask {
    BillboardEffect e;
};
struct SparkTask : WTask {
    float pos[3] = {};  // +0x34
    int roll = 0;       // +0x44
    int model = 0;      // +0x48
    float scale = 0;    // +0x4C
};
struct Type33Task : WTask {
    SpawnRecord rec;
};

}  // namespace

World::Impl* CurrentWorld() { return g_cur; }
void SetCurrentWorld(World::Impl* w) { g_cur = w; }  // the front end (world_front.cpp) runs tasks outside Step()

// =====================================================================================================================
// Impl basics
// =====================================================================================================================

World::Impl::Impl() {
    rng.observer = this;
    // a freed actor still in the hit target lists stays readable (the original reads the freed block): the floor and
    // shot queries of the next frame walk the copy 0x005A3098
    ts.keepFreed = [this](const TaskData* d) {
        for (const Target& t : targets)
            if (static_cast<const TaskData*>(t.obj) == d) return true;
        for (const Target& t : targetCopy)
            if (static_cast<const TaskData*>(t.obj) == d) return true;
        return false;
    };
}

void World::Impl::OnRand(int value) {
    uint32_t site = 0;
    if (!sites.empty()) site = sites[siteIndex++ % sites.size()];
    randCalls.push_back({ site, CurrentSerial(), value });
}

void World::Impl::SetSites(std::initializer_list<uint32_t> s) {
    sites.assign(s.begin(), s.end());
    siteIndex = 0;
}

int World::Impl::Rand(uint32_t site) {
    SetSites({ site });
    return rng.Next();
}

uint32_t World::Impl::CurrentSerial() {
    Task* t = ts.Current();
    return (t && t != &ts.Root()) ? t->serial : 0;
}

WTask* World::Impl::CurrentTask() {
    Task* t = ts.Current();
    if (!t || t == &ts.Root() || !t->data) return nullptr;
    return static_cast<WTask*>(t->data.get());
}

WorldDraw* World::Impl::Draw(int model, const Mat4& world, uint8_t mode, float alpha, bool alt, int node, bool viewSpace,
                             bool cull) {
    if (model == 0) return nullptr;  // 0x00418560 / 0x004185A0 / 0x004185E0: id 0 draws nothing (only 0x0041EBB0(1.0))
    if (residency && !cull && model > 0 && model < static_cast<int>(modelLoaded.size()) && !modelLoaded[static_cast<size_t>(model)])
        return nullptr;  // Mdl_Draw 0x00418560: slot not loaded
    if (hordeBlackout) {  // (port: the Horde store's black page: only the store's own task draws)
        WTask* t = CurrentTask();
        if (!t || t->fn != 0x48000000) return nullptr;
    }
    WorldDraw d;
    d.model = model;
    d.world = world;
    d.drawMode = mode;
    d.alpha = alpha;
    d.alt = alt;
    d.node = node;
    d.viewSpace = viewSpace;
    d.cull = cull;
    if (drawMv && !cull) d.viewSpace = true;
    d.task = CurrentSerial();
    WTask* w = CurrentTask();
    d.fn = w ? w->fn : 0;
    d.light = light;
    d.light.table = alt;  // 0x004185E0 -> 0x004AA500: key bit 0x04000000 -> light table 0x004AA8B0
    d.envMap = envMapDraw;
    // Res_LoadModel's flip of the listed skies (0x00419270): slot state 4 = loaded on its own (8 = with its pol file)
    d.cullFlip = CullFlipModel(exe, model) &&
                 (!residency || model >= static_cast<int>(modelLoaded.size()) || modelLoaded[static_cast<size_t>(model)] == 4);
    if (!modelEdits.empty()) {  // the model's data as the original has edited it in memory (0x0040F960)
        auto it = modelEdits.find(model);
        if (it != modelEdits.end()) d.edit = it->second;
    }
    draws.push_back(d);
    return &draws.back();
}

void World::Impl::Note(const std::string& s) {
    if (std::find(notes.begin(), notes.end(), s) == notes.end()) {
        notes.push_back(s);
        static const bool show = getenv("HOTD2_NOTE_FRAMES") != nullptr;  // debug: the frame of a note's first time
        if (show) fprintf(stderr, "note at frame %d: %s\n", frame, s.c_str());
    }
}

void World::Impl::Sound(uint32_t id) { SoundCall(World::kPlayGameSound, id); }
void World::Impl::SoundCall(int kind, uint32_t id) {
    if (RandomizerActive(*this) && (kind == World::kStopAll ||
        (kind == World::kPlayGameSound && ((id >> 28) == 2 || id == 0x80000002u))))
        RandomizerClearSubtitles(*this);
    if (kind == World::kPlayGameSound) id = RandomizerVoiceSound(*this, id);
    sounds.push_back({ id, CurrentSerial(), kind, gameMode, cfg.gameType });
    // the current BGM 0x009C8FB8 (replayed when the pause ends): set by every BGM play (0x1xxxxxxx), cleared by
    // 0x80000000 and Snd_StopAll
    if (kind == World::kStopAll || (kind == World::kPlayGameSound && id == 0x80000000u)) currentBgm = 0;
    else if (kind == World::kPlayGameSound && (id >> 28) == 1) currentBgm = id;
}
void World::Impl::SoundRepeat(uint32_t id, int count, int interval) { SpawnSoundRepeat(id, count, interval); }  // 0x00435EE0 (world_st1b_boss.cpp)
void World::Impl::Bgm(uint32_t, uint32_t, uint32_t c, uint32_t) { Sound(0x80000000u), Sound(c); }  // Snd_EntryBgm 0x0041D450
void World::Impl::Error(const std::string& what) { Note("event runner: " + what); }
void World::Impl::Env(const EventOp& op) {
    if (op.opcode == 0x17) return StartLightTask(op);  // 0x0045F2A0 (world_st2_misc.cpp)
}

void World::Impl::Section(uint32_t opcode, int newSection, int) {
    if (opcode == 0x29) section = newSection;  // 0x00401630 (0x28 only preloads)
}

// Resource requests (ring 0x007DA220). Ops 0x58 / 0x59 / 0x5A process the queue at once; otherwise Game_Frame
// 0x0049E220 steps it a few times per frame (StepResources). Only motion-file residency is modelled: props draw only
// when their motion is resident (0x009A37E4[motion * 4] == 2).
void World::Impl::Resource(uint32_t opcode, uint32_t arg) {
    auto process = [&](const Request& r) { ProcessRequest(r); };
    if ((opcode == 0x52 || opcode == 0x53) && (arg == 0xBE || arg == 0xBF) && runner && runner->cfg.gameType == 1 && modes) {
        // 0x0045F500 / 0x0045F580: in game type 1 the agents' pols 0xBE / 0xBF are P1's / P2's item character's
        // (0x009A2242 + p * 0x14: c <= 7 pol 0xBE + c, 8 0xA6, 9 0xB9)
        const int c = modes->pitems[arg == 0xBE ? 0 : 1].character;
        if (c <= 7) arg = 0xBE + static_cast<uint32_t>(c);
        else if (c == 8) arg = 0xA6;
        else if (c == 9) arg = 0xB9;
        else return;
    }
    if (opcode >= 0x50 && opcode <= 0x57) {
        resQueue.push_back({ opcode, arg });
        if (opcode == 0x56) pendingMotionFiles.insert(static_cast<int>(arg));
        return;
    }
    if (opcode == 0x58) {  // Res_FlushRequests 0x0041D970
        for (const Request& r : resQueue) process(r);
        resQueue.clear();
    } else if (opcode == 0x59 || opcode == 0x5A) {  // 0x0041D9D0 (types < 8) / 0x0041DA70 (types 8..10)
        std::vector<Request> keep;
        for (const Request& r : resQueue) {
            const bool motionType = r.op >= 0x56;
            if ((opcode == 0x59) != motionType) process(r);
            else keep.push_back(r);
        }
        resQueue = keep;
    }
}

// Game_Frame 0x0049E220 calls 0x0041D500 -> 0x0041D550 5 or 10 times per frame (10 while frames are skipped, 0x007DEBAC):
// one step of the head request each (0x0041D5A0 -> handler 0x00588C20[type], sub-step +0xC; a pol file takes 4 + one
// per model, a texture file 2 + one per texture). Most steps run elsewhere: the frame pacing 0x004A5710 calls the idle
// callback 0x007DEB7C = 0x0049E730 (one more 0x0041D500 while > 1 ms remain) until the frame time is up, so the queue
// advances by wall-clock time: on the capture machine nearly every request completes in the frame it was queued in, but
// big loads spill into the next frames depending on the run (Q lines, build\scene_capture_draws3_res2.txt: 35 of 371
// loading frames cut mid-request, after 1..279 steps). Not deterministic: modelled as up to `steps` whole requests at
// the start of the next frame (complete-at-once matched more captures but not all: notes/agents/draws2.md).
void World::Impl::StepResources(int steps) {
    for (int i = 0; i < steps && !resQueue.empty(); ++i) {
        const Request r = resQueue.front();
        resQueue.erase(resQueue.begin());
        ProcessRequest(r);
    }
}

// One processed request: motion files (0x56) and model slots (0x50 / 0x51 model id, 0x52 / 0x53 pol file: the id list
// 0x004E794C[file], count 0x004E803C[file]; Res_LoadModel 0x00418820 / Res_LoadPolFile 0x00418C20 set the slot's
// loaded flags 0x009A66AC that Mdl_Draw 0x00418560 tests)
void World::Impl::ProcessRequest(const Request& r) {
    if (r.op == 0x54 || r.op == 0x55) {  // texture file load (type 6, 0x0041CCA0) / unload (type 7, 0x0041CD00)
        if (r.arg < texFiles.size()) {
            if (r.op == 0x54) {
                if (texFiles[r.arg] == 0) texFiles[r.arg] = 4;  // 0x0041C890: state 3 while loading, 4 at the end
            } else {
                texFiles[r.arg] = 0;
            }
        }
        return;
    }
    if (r.op == 0x56) {
        pendingMotionFiles.erase(static_cast<int>(r.arg));
        loadedMotionFiles.insert(static_cast<int>(r.arg));
        releasedMotionFiles.erase(static_cast<int>(r.arg));
        return;
    }
    if (r.op == 0x57) {  // motion file release: its motions' state 0x009A37E4[motion * 4] back to 0
        loadedMotionFiles.erase(static_cast<int>(r.arg));
        releasedMotionFiles.insert(static_cast<int>(r.arg));
        return;
    }
    // slot state (0x009A66AC & 0xE): 0 free, 4 loaded on its own (0x00418820), 8 loaded with its pol file (0x00418EC0).
    // A single load only fills a free slot; a single release (0x00418BA0) frees only a slot loaded on its own; the pol
    // file's release (0x00419020 / 0x004191A0) frees all of its slots.
    auto slot = [&](int id) -> uint8_t* {
        return id > 0 && id < static_cast<int>(modelLoaded.size()) ? &modelLoaded[static_cast<size_t>(id)] : nullptr;
    };
    if (r.op == 0x50 || r.op == 0x51) {
        if (uint8_t* s = slot(static_cast<int>(r.arg))) {
            if (r.op == 0x50 && *s == 0) *s = 4;
            else if (r.op == 0x51 && *s == 4) *s = 0;
        }
        if (r.op == 0x50) {  // the model's pol file (0x004E83B4[id]), for the port's texture prefetch
            uint16_t file = 0;
            if (exe.Read(0x004E83B4 + r.arg * 2, &file, 2)) polFileLoads.push_back(file);
        }
    } else if (r.op == 0x52 || r.op == 0x53) {
        const uint32_t file = r.arg;
        if (r.op == 0x52) polFileLoads.push_back(static_cast<int>(file));
        const uint32_t ids = exe.U32(0x004E794C + file * 4);
        const int count = exe.S16(0x004E803C + file * 2);
        for (int i = 0; ids != 0 && i < count; ++i)
            if (uint8_t* s = slot(exe.S16(ids + static_cast<uint32_t>(i) * 2))) {
                if (r.op == 0x53) *s = 0;
                else if (*s == 0) *s = 8;
            }
    }
}

bool CullFlipModel(const ExeImage& exe, int model) {
    for (uint32_t a = 0x0057A280; exe.InData(a); a += 2) {  // 6048, 6049, 6307, 6309, -1
        const int id = exe.S16(a);
        if (id == -1) return false;
        if (id == model) return true;
    }
    return false;
}

// 0x0041D510 -> 0x00418690: 0x00418720 clears every file state (0x1BD entries), then common.bin (1, Res_LoadCommon),
// scr_common.bin (0x147) and both blood files 0x0057A384 (0x1BA / 0x1BB; boot loads only 0x1BB, 0x009C7024 = 1) are
// set to 4: the files the boot loads for good (0x0040A920 -> 0x00418490 -> 0x0041CD70, refcount -1). The boot ends with
// 0x0041D510 too, so this is also the state after the boot.
void World::Impl::ResetTexFiles() {
    texFiles.assign(0x1BD, 0);
    for (uint32_t f : { 1u, 0x147u, exe.U32(0x0057A384), exe.U32(0x0057A388) })
        if (f < texFiles.size()) texFiles[f] = 4;
}

const std::vector<std::array<uint32_t, 8>>& World::Impl::InlineRecords(int id) {
    auto cached = inlineRecords.find(id);
    if (cached != inlineRecords.end()) return cached->second;
    std::vector<std::array<uint32_t, 8>>& out = inlineRecords[id];
    uint16_t file = 0;
    if (!exe.Read(0x004E83B4 + static_cast<uint32_t>(id) * 2, &file, 2)) return out;
    const std::string name = exe.CStr(exe.U32(0x004D0EF4 + file * 4u));
    const uint32_t list = exe.U32(0x004E794C + file * 4u);
    const int count = static_cast<uint16_t>(exe.S16(0x004E803C + file * 2u));
    int index = -1;
    for (int i = 0; i < count && index < 0; ++i)
        if (static_cast<uint16_t>(exe.S16(list + static_cast<uint32_t>(i) * 2)) == id) index = i;
    std::ifstream f(std::filesystem::path(gameDir) / "pol" / name, std::ios::binary);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), {});
    if (index < 0 || raw.size() < 8) return out;
    uint32_t want = 0;
    memcpy(&want, raw.data(), 4);
    std::vector<uint8_t> data = raw;  // as AssetStore::File: PRS when the size word fits, else the file as it is
    if (want > 0 && want < (64u << 20)) {
        std::vector<uint8_t> dec(want);
        if (DecompressPrsSafe(raw.data() + 4, raw.size() - 4, dec.data(), dec.size()) == static_cast<int>(want)) data = std::move(dec);
    }
    const auto entries = ReadPolHeader(data.data(), data.size());
    Model m;
    if (index >= static_cast<int>(entries.size()) || !ParseModel(data.data(), data.size(), entries[static_cast<size_t>(index)], m))
        return out;
    std::vector<uint32_t> offs;  // inline records = the distinct vertex offsets in file order (references point back)
    for (const ModelMesh& mesh : m.meshes)
        for (const ModelStrip& s : mesh.strips) offs.insert(offs.end(), s.offsets.begin(), s.offsets.end());
    std::sort(offs.begin(), offs.end());
    offs.erase(std::unique(offs.begin(), offs.end()), offs.end());
    for (uint32_t o : offs) {
        if (o + 32 > data.size()) break;
        std::array<uint32_t, 8> r;
        memcpy(r.data(), data.data() + o, 32);
        out.push_back(r);
    }
    return out;
}

// Motion state 0x009A37E4[motion * 4] == 2. Stage_Start 0x00460030 resets every state (0x0041D510 -> 0x00412BC0); a
// file becomes resident when the queue loads it (op 0x56: 0x00412C10 / 0x00412D90) or on demand (MotionTouch), and
// stops being resident at its release (op 0x57: 0x00412E40).
bool World::Impl::MotionResident(int motion) {
    const int file = static_cast<int8_t>(exe.S16(0x004E2C40 + motion) & 0xFF);
    return file < 0 || loadedMotionFiles.count(file) != 0;
}

// 0x00412F50 (motion frame data, called by the skeleton animation 0x00410920 / 0x00410BD0 / 0x00411C20 / ...): a motion
// whose state is not 2 has its whole file loaded at once (0x0041D860 queues it at the head after finishing the head
// request, 0x0041D810 processes it). The props test the state before they animate, so only animations load on demand.
void World::Impl::MotionTouch(int motion) {
    const int file = static_cast<int8_t>(exe.S16(0x004E2C40 + motion) & 0xFF);
    if (file < 0 || loadedMotionFiles.count(file) != 0) return;
    loadedMotionFiles.insert(file);
    releasedMotionFiles.erase(file);
    pendingMotionFiles.erase(file);  // a queued 0x56 of it finds it loaded (0x00412C10 returns at once)
}

const PropSkeleton& World::Impl::PropSkel(int type) {
    auto it = propSkeletons.find(type);
    if (it == propSkeletons.end()) {
        PropSkeleton s;
        s.Load(exe, type);
        it = propSkeletons.emplace(type, std::move(s)).first;
    }
    return it->second;
}

const Motion* World::Impl::PropMotion(int id) {
    auto it = propMotions.find(id);
    if (it == propMotions.end()) {
        Motion m;
        if (!LoadMotionById(exe, gameDir, id, m)) Note("prop motion " + std::to_string(id) + " not found");
        it = propMotions.emplace(id, std::move(m)).first;
    }
    return &it->second;
}

const Motion* World::Impl::EnemyMotion(int id, int nodes) {
    MotionTouch(id);  // the animation's fetch 0x00412F50
    auto key = std::make_pair(id, nodes);
    auto it = enemyMotions.find(key);
    if (it == enemyMotions.end()) {
        Motion found;
        for (const MotionFileEntry& f : motionFiles) {
            if (std::find(f.motionIds.begin(), f.motionIds.end(), static_cast<int16_t>(id)) == f.motionIds.end()) continue;
            const std::vector<uint8_t> file = ReadAll(fs::path(gameDir) / "mot" / f.name);
            for (Motion& m : SplitMotionFile(exe, f, file, nodes))
                if (m.id == id) found = std::move(m);
            break;
        }
        if (found.data.empty()) Note("enemy motion " + std::to_string(id) + " not found");
        it = enemyMotions.emplace(key, std::move(found)).first;
    }
    return &it->second;
}

const Skeleton& World::Impl::EnemySkeleton(int kind) {
    auto it = enemySkeletons.find(kind);
    if (it == enemySkeletons.end()) {
        Skeleton s;
        LoadSkeleton(exe, kind, s);
        it = enemySkeletons.emplace(kind, std::move(s)).first;
    }
    return it->second;
}

PropWorld World::Impl::PropState() {
    PropWorld w;
    memcpy(w.flags, runner->flags, sizeof(w.flags));
    w.stage = cfg.eventMode;
    w.stageSub = runner->scene;
    w.area = runner->variant;
    w.gameMode = cfg.gameType;  // 0x009CA08C
    w.altModels = altModels;
    w.camPath = runner->camera.camId;
    w.camTime = runner->camera.tick;
    w.camYaw = cam.yaw;
    w.groundY = ground;
    w.motionState = 2;
    memcpy(w.itemCounter, itemCounter, sizeof(itemCounter));
    w.rand = PropRand;
    return w;
}

void World::Impl::AfterPropUpdate(const PropWorld& w) {
    memcpy(itemCounter, w.itemCounter, sizeof(itemCounter));
    memcpy(runner->flags, w.flags, sizeof(w.flags));  // event flags set by props (hanging prop 0xA60: its trigger flag)
    for (uint32_t s : w.sounds) Sound(s);
}

// Hit_RegisterTarget 0x00405160
void World::Impl::RegisterTarget(ActorTask* a) {
    if (IsShotTarget(a->flags34, a->viewPos[2])) {
        if (a->flags34 & 0x10) {  // +0x150 = (model-view) x camera matrix 0x009A6040[cam] (0x004A92A0)
            Mat4 top = cam.inverse;
            mx::MulLeft(top, a->m150);
            a->m150 = top;
        }
        targets.push_back({ a, { a->sphere[0], a->sphere[1], a->sphere[2] } });
    }
}

// Eff_SpawnAt 0x00407340: block {pos, angles (pitch, yaw), scale -1, type, faceMode, arg}
void World::Impl::SpawnEffectAt(const float pos[3], int pitch, int yaw, int type, int faceMode, int arg) {
    EffectSpawn in;
    memcpy(in.pos, pos, 12);
    in.pitch = pitch;
    in.yaw = yaw;
    in.scale[0] = -1.0f;
    in.type = type;
    in.faceMode = faceMode;
    (void)arg;
    SpawnEffectBlock(in);
}

// Eff_SpawnScaled 0x00407BC0 (types 0x5A..0x5D)
void World::Impl::SpawnEffectScaled(const float pos[3], int type, int arg) {
    (void)arg;
    std::vector<BillboardEffect> out;
    BillboardEffect::SpawnScaled(pos, type, camState, cam.view, &out);
    for (const BillboardEffect& e : out) {
        EffectTask* t = NewTask<EffectTask>(Tramp<&Impl::TaskEffect>, fn::kEffect);
        t->e = e;
    }
}

// 0x004073B0 (+ Eff_PlaySound 0x00407950)
void World::Impl::SpawnEffectBlock(const EffectSpawn& in) {
    std::vector<BillboardEffect> out;
    if ((in.type & 0xFF) == 0x53) SetSites({ 0x00407610 });
    BillboardEffect::Spawn(in, camState, cam.view, rng, &out);
    for (const BillboardEffect& e : out) {
        EffectTask* t = NewTask<EffectTask>(Tramp<&Impl::TaskEffect>, fn::kEffect);
        t->e = e;
    }
    const uint32_t snd = EffectSound(in.type, cfg.eventMode);
    if (snd) Sound(snd);
}

// =====================================================================================================================
// Camera
// =====================================================================================================================

// The camera task (CameraDirector::Frame, incl. Cam_BuildViews 0x00401F40) leaves every context's view and camera
// matrix; the active one is the rendered view and the base of every task's matrix stack.
void World::Impl::UpdateCamera() {
    const CameraDirector& c = runner->camera;
    auto toCam = [&](int i) {
        const CameraContext& x = c.ctx[i & 3];
        WorldCamera w;
        w.ctx = i;
        memcpy(w.pos, x.pos, 12);
        w.pitch = x.pitch;
        w.yaw = x.yaw;
        w.roll = x.roll;
        memcpy(w.view.m, x.view, 64);
        memcpy(w.inverse.m, x.camera, 64);
        return w;
    };
    const WorldCamera c0 = toCam(0);
    view0 = c0.view;
    inverse0 = c0.inverse;
    cam = toCam(c.activeCtx);
    memcpy(camState.pos, cam.pos, 12);
    camState.pitch = cam.pitch;
    camState.yaw = cam.yaw;
    camState.roll = cam.roll;
}

// =====================================================================================================================
// Stage start (0x00426800 sub-state 0 -> Stage_Start 0x00460030 -> Task_InitRoot(0x00460710))
// =====================================================================================================================

void World::Impl::BeginFrame() {
    projCenterFrame[0] = projCenter[0];  // Proj_Setup 0x004184C0 at the frame start
    projCenterFrame[1] = projCenter[1];
    draws.clear();
    randCalls.clear();
    ops.clear();
    sounds.clear();
    notes.clear();
    sites.clear();
    for (auto& p : player) {
        p.firedThisFrame = false;
        p.contShown = -1;
    }
    contScreenShown = -1;
    hudEffects.clear();
    hudSprites.clear();
    hudSubtitles.clear();
    hud.menuScrollHints.clear();
    hudTitleSerial = 0;
    routeMapScroll = -1;
}

void World::Impl::StartStage(const WorldConfig& c) {
    prototype.reset();
    cfg = c;
    RandomizerStage(*this);  // roll before weapon effects and player bodies are initialized
    stageResults = {};
    horde.reset();  // port: a Horde game's state does not outlive its stage (HordeGameStart recreates it after this)
    hordeHoldRail = false;
    frame = c.startFrame;
    gameMode = c.gameMode;
    hud.screens.clear();  // a front-end screen's bitmaps (the network lobby's) do not outlive it (port: netplay start)
    for (int& w : modelWave) w = -1;  // the stage's models are loaded afresh
    seedCounter = c.seedCounter;
    BeginFrame();
    rankAdj = 0;
    rankTimer = 1;
    GunRecord guns[2] = { player[0].gun, player[1].gun };  // 0x009C8FC0: not part of the player struct
    const bool adv = gameMode == 10 || gameMode == 11;  // story sequences: root 0x0041FAB0, no Stage_Start (world_front.cpp)
    if (gameMode == 6) {
        if (!inFrontStep && !endingRoot) {  // (World::Boot: FrontStep already ran this frame's input; ending: Step did)
            // A game started without the front end: the shot inputs 0x009C8F70.. hold what earlier frames left. A gun
            // already connected (e.g. P2 pressed start on the title) has its position there, so its record is valid in
            // this frame even when only the other player's slot is latched (Game_UpdateShots 0x0040CE70).
            for (int p = 0; p < 2; ++p)
                if (input[p].kind == 1 && !input[p].offscreen) {
                    shotIn[p].on = !(input[p].x == 0xFFFF && input[p].y == 0xFFFF);
                    shotIn[p].x = static_cast<int16_t>(input[p].x);
                    shotIn[p].y = static_cast<int16_t>(input[p].y);
                }
            g_cur = this;
            InputFrame();  // Game_UpdateInput runs before the mode function in the Stage_Start frame too
            g_cur = nullptr;
        }
        guns[0] = player[0].gun;
        guns[1] = player[1].gun;
        // ---- 0x0045FEF0 (mode 6, sub-state 0): event mode 0 / scene 0 (tables loaded by Load), rank init
        // 0x00460770 (rank = 0x005679F4[difficulty], rank timer on), resets (0x009A5920, 0x009C89C0.., 0x009C8EC0..)
        rank = static_cast<int8_t>(exe.S16(0x005679F4 + cfg.difficulty) & 0xFF);
        rankTimerOn = true;
        memset(komono40Intact, 1, sizeof(komono40Intact));  // 0x00463680
        subState = 1;  // Stage_Start runs with sub-state 1; 2 afterwards
    } else {
        // ---- 0x00426800 (mode 5, sub-state 0)
        // 0x00460770: rank = 0x005679F4[demo ? 2 : difficulty]; 0x009A2C30 = 1, but 0x00426800 sets it back to 0.
        rank = static_cast<int8_t>(exe.S16(0x005679F4 + 2) & 0xFF);
        rankTimerOn = false;
        subState = 0;
    }
    // ---- Stage_Start 0x00460030
    frameCounter5C50 = 0;
    if (!adv && !endingRoot) rng.Seed(GameRandomSeed(GameIsDemoMode(gameMode, subState), seedCounter));  // Game_SeedRandom 0x004157D0
    seededSinceStart = false;                                                   // 0x009A26A4
    ts.Reset();                                                   // Mem_Init 0x004A7310
    civGhosts.clear();
    ts.SetNextSerial(cfg.firstSerial);
    resQueue.clear();
    // story modes 10 / 11 (0x0041F9B0 / 0x0041FB00): the same 0x0041D510 / 0x0041D540, pol 0x26, texture 0x17B and the
    // flush 0x0041D970 in their init; the attract demo (mode 5, 0x00426800) runs Stage_Start 0x00460030 too, which frees
    // the slots (0x0041D510 -> 0x00418690: common.bin's models stay) before its loads
    residency = gameMode == 6 || gameMode == 5 || adv;
    if (residency) {
        // HYPOTHESIS: the mode switch frees every model slot but common.bin's (0x0041D510 / 0x0041D540); Stage_Start
        // requests pol 0x26 (stage1.bin, event mode 0) and texture 0x17B, then flushes the queue (0x0041D970)
        modelLoaded.assign(0x4000, 0);
        ProcessRequest({ 0x52, 1 });
        if (cfg.eventMode == 0 || adv) ProcessRequest({ 0x52, 0x26 });
        if (cfg.gameType == 1 && modes) {  // game type 1: the players' weapon effects (0x009A2248 / 0x009A225C byte 1)
            const int e0 = static_cast<int8_t>(modes->pitems[0].b8[1]), e1 = static_cast<int8_t>(modes->pitems[1].b8[1]);
            if (e0 == 5 || e1 == 5) ProcessRequest({ 0x50, 0x109D });  // tracer model
            if (e0 == 3 || e1 == 3) ProcessRequest({ 0x52, 0x3D });     // weapon 3's impact (effect 0x53, models 0x125..)
            if (e0 == 4 || e1 == 4) ProcessRequest({ 0x52, 0x41 });
        }
        // texture files: Stage_Start 0x00460030 loads scr_load2 0x159 at once (0x0041D9D0), then 0x17B (subtitles), or
        // with 0x009C911E == 1 0x183 and the chapter texture 0x00592630[stage], flushed (0x0041D970); the story modes
        // load 0x17B (0x0041FA16 / 0x0041FB66)
        ResetTexFiles();
        if (!adv) ProcessRequest({ 0x54, 0x159 });
        if (adv || !runner || GlobalInt(0x009C911E, 0) != 1) {  // (the runner of the previous stage, if any)
            ProcessRequest({ 0x54, 0x17B });
        } else {
            ProcessRequest({ 0x54, 0x183 });
            if (const uint32_t t = exe.U32(0x00592630 + 4 * static_cast<uint32_t>(cfg.eventMode))) ProcessRequest({ 0x54, t });
        }
    }
    levelMeshes = false;
    coliLoaded = false;  // coli0 + coli<0x009A1A08 + 1> are (re)loaded for each stage (0x0048A3B0)
    pendingMotionFiles.clear();
    loadedMotionFiles.clear();  // 0x0041D510 -> 0x00412BC0: every motion state 0
    releasedMotionFiles.clear();
    {
        // 0x0045EDD0 -> 0x00412FD0 + flush 0x0041D970: the stage's motion file list (-1 ends), resident from the start:
        // game mode 6 with game type 2 (training) 0x004E223C[scene] (scene 3: 0x004E228C[difficulty 0x009C9118]), type
        // 3 (boss mode) 0x004E2224[stage], else 0x004E21F4[stage]
        uint32_t list = exe.U32(0x004E21F4 + 4 * static_cast<uint32_t>(cfg.eventMode));
        if (gameMode == 6 && cfg.gameType == 2) {
            const int diff = runner ? runner->cfg.playerVariant : 0;
            list = cfg.scene == 3 ? exe.U32(0x004E228C + 4 * static_cast<uint32_t>(diff))
                                  : exe.U32(0x004E223C + 4 * static_cast<uint32_t>(cfg.scene));
        } else if (gameMode == 6 && cfg.gameType == 3) {
            list = exe.U32(0x004E2224 + 4 * static_cast<uint32_t>(cfg.eventMode));
        }
        for (int n = 0; exe.InData(list) && n < 64; ++n, list += 2) {
            const int f = exe.S16(list);
            if (f == -1) break;
            loadedMotionFiles.insert(f);
        }
    }
    for (const auto& [m, uv] : waterUv) modelEdits.erase(m);  // the reloaded models: the file's data again
    waterUv.clear();
    targets.clear();
    targetCopy.clear();
    // ---- Task_InitRoot(0x00460710): every task is a child of the root, created in this order
    g_cur = this;
    {
        // Evt_Init 0x0045EBC0 (+ camera task init 0x00402260 via 0x00402300 below)
        NewTask<WTask>(Tramp<&Impl::TaskEvent>, fn::kEvtInit);
        runner = std::make_unique<EventRunner>(mem, *this, &env, &exe);
        EventRunner::Config rc;
        rc.gameMode = cfg.gameMode;
        rc.demoIndex = cfg.demoIndex;
        rc.gameType = cfg.gameType;  // 0x009CA08C (world_modes*.cpp)
        rc.route = 0;    // 0x009C8E84 / 0x009C8E80: updated from the players before every VM run (TaskEvent)
        rc.players = 0;
        runner->camera.SetCameraBank(&cameraBank);
        runner->Start(cfg.eventMode, cfg.scene, rc);
        if (cfg.gameType != 0) runner->globals[0x009CA08C] = static_cast<uint32_t>(cfg.gameType);  // GlobalInt readers
        // camera task init 0x00402260 -> 0x004021D0: Env_InitBlock 0x0041DBA0 on both env blocks (the env writes at
        // 0x00460250..0x0046032F, Environment::StageDefaults, did not run at any stage start of the captured game)
        env.ResetBlocks();
        lightDir0[0] = lightDir0[1] = 0;
        env.InitInterp();     // Env_InitInterp 0x0040AE60
        NewTask<WTask>(Tramp<&Impl::TaskEnv>, fn::kEnvInterp);  // 0x0040AD90 (world_light.cpp)
        NewTask<WTask>(Tramp<&Impl::TaskCamera>, fn::kCamera);          // 0x00402300
        NewTask<WTask>(Tramp<&Impl::TaskSky>, fn::kSky);                // Sky_Create 0x004134D0
        skyMode = 1;
        skyType = 0;
        sky = Sky{};
        if (gameMode == 10) sky.Create(cfg.storySkyYaw);  // type 0 keeps the task's leftover yaw (0x007C2118 = 0)
        NewTask<WTask>(Tramp<&Impl::TaskRail>, fn::kRail);  // 0x00414F20 (+ the player bodies 0x00416450)
        PlayerBodyInit(0);
        PlayerBodyInit(1);
        for (int p = 0; p < 2; ++p) {  // 0x00414ED0: task function 0x00579CD0[player state * 5]
            player[p] = Player{};
            player[p].gun = guns[p];
            // Game_ApplyState 0x0040E860 sets both states to 9 for modes other than 6 / 7
            player[p].state = static_cast<int16_t>(gameMode == 6 || gameMode == 7 ? cfg.playerState[p] : 9);
            player[p].inputKind = static_cast<int8_t>(input[p].kind);
            const uint32_t f = exe.U32(0x00579CD0 + 0x14 * static_cast<uint32_t>(player[p].state));
            Task* tk = ts.Create(Tramp<&Impl::TaskPlayer>, std::make_unique<PlayerTask>());
            PlayerTask& t = tk->As<PlayerTask>();
            t.fn = f;
            t.index = p;
            player[p].task = tk;  // +0x74
        }
        NewTask<WTask>(Tramp<&Impl::TaskPlayerCount>, fn::kPlayerCount);  // 0x00414FB0
        NewTask<WTask>(Tramp<&Impl::TaskLetterbox>, fn::kLetterbox);      // 0x00413950
        letterboxCounter = 0;
        if (!adv && !endingRoot) NewTask<WTask>(Tramp<&Impl::TaskLightTable>, fn::kAltModels);  // 0x004809D0 (not in the ending root 0x00431740)
        altModels = 0;
        flashlightOn = 0;  // 0x009C8D4C
        for (float& c : tableAmbient) c = 0.5f;  // 0x009C71D4..
        for (LightSlot& s : lightSlots) s = LightSlot{};
        NewTask<WTask>(Tramp<&Impl::TaskStageDraw>, fn::kStageDraw);  // 0x00401470
        section = 0;
        if (!endingRoot) {  // (the ending root 0x00431740 has no rain / lock-on / attack-order tasks: world_ending.cpp)
            SetSites({ 0x004138BB, 0x004138D9, 0x004138F6 });  // Rain_Init 0x004138A0
            rain.Init(rng);
        }
        rainFlag = 0;
        if (!endingRoot) NewTask<WTask>(Tramp<&Impl::TaskRain>, fn::kRain);
        if (!endingRoot) NewTask<WTask>(Tramp<&Impl::TaskLockOn>, fn::kLockOn);  // 0x00408D90
        lockOn.Reset();
        lockTargets.clear();
        nextLockTarget = 0;
        if (!endingRoot) NewTask<WTask>(Tramp<&Impl::TaskAttackOrder>, fn::kAttackOrder);  // 0x00409080
        attackList.clear();
        NewTask<WTask>(Tramp<&Impl::TaskCollision>, fn::kCollision);  // 0x00404480 -> 0x00405FE0 (world_collision.cpp)
        for (int w = 0; w < 2; ++w) {
            collList[w].clear();
            collHasPending[w] = false;
            collPending[w] = collEnd[w] = 0;
        }
        NewTask<WTask>(Tramp<&Impl::TaskHit>, fn::kHit);
        if (cfg.gameMode == 5) NewTask<WTask>(Tramp<&Impl::TaskDemoTimer>, fn::kDemoTimer);  // world_front.cpp
    }
    rankPrevPlayers = 0;
    skyScroll.clear();  // the stage's sky models are loaded fresh (renderer state)
    // ---- 0x0040C340 (rail reset)
    for (float& v : playerPos) v = 0;
    ground = 0;
    // ---- Stage_ResetState 0x0045EDD0
    attackers = liveEnemies = countA0E8 = 0;
    frameCounter2BAC = 0;  // 0x009A2BAC
    memset(runner->flags, 0, sizeof(runner->flags));
    letterbox = letterboxPrev = 5;
    skyMode = 0;
    rainFlag = 0;
    canShoot = 0;
    for (auto& s : actorSlots) s = nullptr;
    players = route = 0;
    playersInGame = 0;
    targetTaken[0] = targetTaken[1] = 0;
    attackSlot = 0;
    demoFlag = gameMode == 5 ? 1 : 0;  // 0x009C71C4 = (state == 5), Game_ApplyState 0x0040E860
    memset(itemCounter, 0, sizeof(itemCounter));
    camState = CameraState{};
    cam = WorldCamera{};
    shakeTimer = 0;  // 0x009C8E8C
    shake = 0;
    padClear = 1;    // 0x009A34DC
    skipRequest = false;
    skip2230 = 0;
    // 0x009A5900 in game mode 6: bit 1 (Game_ApplyState 0x0040E860) and bit 0 (set by the start press 0x00414FC0 on
    // the title; cleared by the camera task while a cutscene runs: World::Impl::TaskCamera)
    if (gameMode == 6) flags5900 = 3;
    paused = false;
    camPlayerWrites = 0;  // (a new event runner counts its camera's writes from 0)
    if (gameMode == 6) memset(routeRec, 0xFF, sizeof(routeRec));  // 0x0045FF5F
    routeCount = 0;                                               // Stage_ResetState: 0x009A5C30 = 0
    pauseHist[0] = pauseHist[1] = false;
    for (Player& P : player) {  // 0x0040D220 (from Stage_ResetState): both gun records' aim cleared
        P.gun.x = P.gun.y = 0;
        P.gun.fx = P.gun.fy = 0;
    }
    // Per-stage globals the stage code sets (pointers into the task heap Mem_Init just cleared, attack indices, fish /
    // bird tables): back to the values a fresh start has (verified by world_test --stage-start), so a stage reached
    // inside one run (ChainStage) or a restarted game sees the same state.
    pushObj = nullptr;
    actor33 = nullptr;
    obj5C34 = nullptr;
    obj9A2C88 = nullptr;
    for (auto& b : batList) b = nullptr;
    for (auto& b : birdTable) b = nullptr;
    batAttacker = 0;
    bat43Attacker = -1;
    bat43SegStep = 0;
    for (int& f : fishSlots) f = 0;
    waterLevel = 0;
    if (gameMode == 6) subState = 2;  // 0x0045FEF0: sub-state + 1 after Stage_Start
    if (cfg.carry.valid) ApplyCarry();  // a later stage of the game (world_st2.cpp)
    RandomizerStageReady(*this);
    UpdateLightsFrame();
    FillHud();
    g_cur = nullptr;
}

// =====================================================================================================================
// Frame
// =====================================================================================================================

// Game_Frame 0x0049E220 -> Game_Update 0x0040E730: counters, input, the mode function (0x004608A0), the mode switch
void World::Impl::Step() {
    if (frontEnd) {  // World::Boot: every game mode (world_front.cpp)
        FrontStep();
        return;
    }
    ++frame;
    BeginFrame();
    const bool over = gameMode == 7 && cfg.gameOver;
    if (gameMode != 5 && gameMode != 6 && !over) return;  // after the game over: not ported
    ++seedCounter;        // 0x009A32A0
    ++frameCounter5C50;   // 0x009A5C50
    ++frameCounter2BAC;   // 0x009A2BAC (0x0040E730)
    StepResources(cfg.resourceStepsPerFrame);
    g_cur = this;
    if (over) {
        InputFrame();
        PauseFrame();     // 0x0040E500 runs in every mode (history only: the toggle needs mode 6)
        GameOverFrame();  // 0x00460960 (world_gameover.cpp)
    } else if (gameMode == 6 && subState == 1 && nextEventMode >= 0) {
        ChainStage();  // the next stage inside the same run (world_st2.cpp); StartStage runs this frame's input
        g_cur = nullptr;
        return;
    } else if (gameMode == 6) {
        // Game_UpdateInput 0x0040E4D0 (AUTORELOAD 0x007C17AC assumed off). The demo's gun records are written by its
        // auto-gun (0x00415710), so mode 5 skips the device part.
        InputFrame();
        PauseFrame();  // 0x0040E500 (the pause toggle with the start button)
        if (!paused) Mode6Frame();  // Game_UpdateMode 0x004608A0 -> 0x0045FEE0 (skipped while paused)
    } else {
        // 0x00426800 sub-state 1 -> 0x004606D0: tasks, Game_UpdateRank 0x004607B0 (the rank timer 0x009C8A7C does not
        // count in the demo: 0x00426800 sets 0x009A2C30 = 0), hit timers
        StageFrame();
    }
    g_cur = nullptr;
    ts.FreeDead();
    UpdateLightsFrame();
    FillHud();
}

// =====================================================================================================================
// Stage tasks
// =====================================================================================================================

// 0x0045ECB0 (first run: switch to Evt_Run) / Evt_Run 0x0045ECC0
void World::Impl::TaskEvent(Task& t) {
    WTask& w = t.As<WTask>();
    if (w.fn == fn::kEvtInit) {
        w.fn = fn::kEvt;
        return;
    }
    const size_t before = runner->trace.size();
    runner->frame = frame;
    runner->cfg.route = route;      // 0x009C8E84 (ops 0x01..0x08)
    runner->cfg.players = players;  // 0x009C8E80 (op 0x12)
    runner->RunEvents();
    ApplySetupCallbacks();
    SyncPlayerDrawBit();
    auto global = [&](uint32_t a, uint32_t* out) {
        auto it = runner->globals.find(a);
        if (it == runner->globals.end()) return false;
        *out = it->second;
        return true;
    };
    for (size_t i = before; i < runner->trace.size(); ++i) {
        const auto& t0 = runner->trace[i];
        const WorldEventOp e{ t0.frame, t0.opcode, t0.addr };
        ops.push_back(e);
        uint32_t v = 0;
        switch (e.opcode) {
        case 0x2C:  // 0x0045FD90: skippable on -> 0x009A2230 = 0
            if (runner->skippable) skip2230 = 0;
            break;
        case 0x4D:  // 0x0045EEC0: Game_SeedRandom 0x004157D0 (mode 5: only the first 0x4D after the stage start)
            if (gameMode != 5 || !seededSinceStart) {
                rng.Seed(GameRandomSeed(GameIsDemoMode(gameMode, subState), seedCounter));
                seededSinceStart = true;
                demoFlag = gameMode == 5 ? 1 : 0;  // 0x009C71C4
            }
            frameCounter5C50 = 0;
            // 0x0045EFA3: route record 0x009A5920[event mode][0x009A5C30++] = scene, then 0xFF
            if (cfg.eventMode >= 0 && cfg.eventMode < 10 && routeCount < 15) {
                routeRec[cfg.eventMode][routeCount++] = static_cast<uint8_t>(runner->scene);
                routeRec[cfg.eventMode][routeCount] = 0xFF;
            }
            break;
        case 0x1A: if (global(0x009C8E58, &v)) memcpy(&ground, &v, 4); break;
        case 0x1B: if (global(0x009A2C34, &v)) skyType = static_cast<int>(v); break;
        case 0x1C: if (global(0x009CA090, &v)) skyMode = static_cast<int>(v); break;
        case 0x1D: if (global(0x009C8E50, &v)) rainFlag = static_cast<int>(v); break;
        case 0x1F: if (global(0x009CA0F4, &v)) letterbox = static_cast<int>(v & 0xFF); break;
        case 0x14: if (global(0x009A2BB4, &v)) altModels = static_cast<int>(v); break;
        case 0x15: if (global(0x009C8D4C, &v)) flashlightOn = static_cast<int>(v); break;  // 0x00480D00 (renderer)
        case 0x16:  // 0x00480D20: light-table ambient 0x009C71D4.. (renderer)
            for (int k = 0; k < 3; ++k)
                if (global(0x009C71D4 + 4 * k, &v)) memcpy(&tableAmbient[k], &v, 4);
            break;
        case 0x10: case 0x11: {  // list pointer 0x009A1A14 / 0x009A34D8, loaded by the task 0x00406010
            uint32_t list = 0, first = 0xFFFFFFFFu;
            if (global(e.opcode == 0x10 ? 0x009A1A14 : 0x009A34D8, &list)) CollListOp(e.opcode == 0x10 ? 0 : 1, list);
            if (global(e.opcode == 0x10 ? 0x009A1A14 : 0x009A34D8, &list) && mem.Read(list, &first) && first != 0xFFFFFFFFu) {
                levelMeshes = true;
            } else if (e.opcode == 0x10) {
                levelMeshes = false;
            }
            break;
        }
        default: break;
        }
    }
}

// Camera task 0x004022B0 (camera director in event_runner.*), then the context views
void World::Impl::TaskCamera(Task&) {
    runner->camera.evals.clear();
    camViewPrev = cam.view;  // the nodes' model-views of the last frame were built with it (0x004048A0)
    runner->CameraTask();
    ApplySetupCallbacks();
    SyncPlayerDrawBit();
    UpdateCamera();
    // 0x00401F40 (0x00402142..): light table mode on (0x004AA6D0(1)), the block-0 light direction from 0x009A3558 /
    // 0x009A355C with this frame's view (0x0040E0B0 -> 0x004AA0E0)
    lightDir0[0] = env.lightPitch;
    lightDir0[1] = env.lightYaw;
    light.pitch = lightDir0[0];
    light.yaw = lightDir0[1];
    // end of 0x00401F40 (0x00402197..0x004021C2): 0x009A5900 bit 0 = "players may join now" (0x00414FC0 / 0x00414690):
    // cleared while the camera runs a cutscene (mode 1) outside the demo, unless bits 0x30 are set (title card)
    if (runner->camera.modeCur == 1 && gameMode != 5 && (flags5900 & 0x30) == 0) flags5900 &= ~1u;
    else flags5900 |= 1;
}

// Sky_Task 0x004132D0
void World::Impl::TaskSky(Task&) {
    if (hordeBlackout) return;  // (port: the Horde store's black page)
    std::vector<Sky::Draw> d;
    if (!sky.Update(skyTable, skyType, skyMode, camState, &d)) SkyPanels();  // types 8 / 10 / 11 (world_st5_actors.cpp)
    for (const Sky::Draw& x : d) Draw(x.model, x.world);
}

// 0x00414F10 -> 0x0040C370 -> [0x009C7080]
void World::Impl::TaskRail(Task&) {
    runner->RailStep();
    memcpy(playerPos, runner->camera.playerPos, 12);  // 0x009C71E0
    if (runner->camera.rail == CameraDirector::Rail::Mode1Sub1) RailPlayersFromBodies();  // 0x0040C9C0
}

// Letterbox_Task 0x00413970 (state 0x009CA0F4, previous state 0x009C8E9C, counter +0x50)
void World::Impl::TaskLetterbox(Task&) {
    const float kTop = F32(exe, 0x004E30F8), kBottom = F32(exe, 0x004E30F4), kStep = F32(exe, 0x004E30FC);
    auto bar = [&](float y) {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, 0.0f, y, -1.0f);
        Draw(0x93E, m, 8, -1.0f, false, -1, true);
    };
    auto bars = [&] {
        bar(0.35f);   // 0x3EB33333
        bar(-0.35f);  // 0xBEB33333
    };
    if (letterboxPrev != letterbox) {
        if (letterbox == 3) letterboxCounter = 0x28;
        else if (letterbox == 1) letterboxCounter = 0;
    }
    switch (letterbox) {
    case 0:
        bars();
        letterbox = 4;
        canShoot = 1;
        letterboxPrev = 4;
        return;
    case 1: {
        canShoot = 1;
        const int c = letterboxCounter++;
        if (c == 0x28) {
            letterbox = 2;
            letterboxPrev = 2;
            return;
        }
        bar(static_cast<float>(letterboxCounter) * kStep + kTop);
        bar(kBottom - static_cast<float>(letterboxCounter) * kStep);
        break;
    }
    case 3: {
        const int c = letterboxCounter--;
        if (c == 0) {
            bars();
            letterbox = 4;
            canShoot = 0;
            letterboxPrev = 4;
            return;
        }
        bar(static_cast<float>(letterboxCounter) * kStep + kTop);
        bar(kBottom - static_cast<float>(letterboxCounter) * kStep);
        break;
    }
    case 4:
        if ((flags5900 & 0x30) == 0) bars();
        break;
    case 5:
        bars();
        letterbox = 4;
        canShoot = 0;
        letterboxPrev = 4;
        return;
    case 6:
        canShoot = 1;
        letterbox = 2;
        letterboxPrev = 2;
        return;
    case 7:
        letterbox = letterboxPrev;
        break;
    case 8: {
        Mat4 m = Mat4::Identity();
        mx::Translate(m, 0.0f, 0.0f, -1.0f);
        mx::Scale(m, 1.0f, 8.0f, 1.0f);
        Draw(0x93E, m, 8, -1.0f, false, -1, true);
        break;
    }
    default:
        break;
    }
    if (letterbox != 8) letterboxPrev = letterbox;
}

// Stage_DrawSection 0x00401260: every object of the current section; the per-object bounding-sphere cull
// (0x00419200 -> 0x00406310) is left to the renderer (cull = true). Draw mode 2 -> pass-1 priority 7.
// Nothing is drawn while 0x009A5900 & 0x20 is set (the stage title card 0x004342E0, the boss name cards): the
// original's chapter cards are black behind the sprites.
void World::Impl::TaskStageDraw(Task&) {
    if (flags5900 & 0x20) return;
    if (section < 0 || section >= static_cast<int>(sections.sections.size())) return;
    for (int o : sections.sections[section]) {
        if (o < 0 || o >= static_cast<int>(sections.objects.size())) continue;
        const auto [model, mode] = sections.objects[o];
        Mat4 m = Mat4::Identity();
        if (model == 0x1828 && (section == 2 || section == 3)) {  // two extra models, not culled
            Draw(0x13B2, m);
            Draw(0x13B0, m);
        }
        if (model == 0x1A56 && section == 8 && o == 0x16) continue;  // skipped (0x00401260)
        if (model == 0x1918 && cfg.eventMode == 9) mx::Translate(m, 0.0f, 9.0f, 0.0f);  // the ending (world_ending.cpp)
        if (mode == 2) Draw(model, m, 7, -1.0f, false, -1, false, true);
        else Draw(model, m, 8, -1.0f, mode == 1 && altModels != 0, -1, false, true);
    }
}

// Rain_Task 0x004136A0
void World::Impl::TaskRain(Task&) {
    if (rainFlag != 1 || hordeBlackout) return;  // (port: the Horde store's black page)
    SetSites({ 0x004136DD, 0x004136FB, 0x00413718 });
    rain.Update(rng, camState);
    for (const Mat4& w : rain.World()) Draw(Rain::kModel, w, 0xE, Rain::kAlpha);
}

// 0x00408DD0: lock-on slots 0x009A5EC0 rebuilt from the candidates registered by 0x00408EC0 (game::LockOnTable)
void World::Impl::TaskLockOn(Task&) { lockOn.Build(); }

// 0x004090B0: attack order of the actors registered by Actor_RegisterTarget 0x00409010 (sort 0x00409190 assumed
// to be the same stable radix sort as 0x00408F30)
void World::Impl::TaskAttackOrder(Task&) {
    std::stable_sort(attackList.begin(), attackList.end(),
                     [](const auto& a, const auto& b) { return (a.first & 0xFFFF) < (b.first & 0xFFFF); });
    c7310 = 0;
    for (size_t i = 0; i < attackList.size(); ++i) {
        EnemyTask* e = attackList[i].second;
        if (e->flags34 & 1) {
            e->c131D = static_cast<int8_t>(i);
            e->c131E = 0xE;
            if (e->anim.kind == 0xB && static_cast<int>(i) < e->attackLimit && (e->flags136C & 0x2000000)) c7310 = 1;
        }
    }
    int8_t n = 0;
    for (auto& [key, e] : attackList) {
        if (!(e->flags34 & 1) || (e->flags34 & 0x20000000)) continue;
        e->c131E = n++;
    }
    attackList.clear();
}

namespace {
struct HitEntry {
    int32_t key;
    ActorTask* obj;
    int node;        // -1: whole object
    uint32_t flags;  // entry +0x2C
    int level = -1;  // node collision-mesh hit (0x004048A0): its impact record
};
}  // namespace

// Hit task 0x004044A0 -> 0x00404570
void World::Impl::TaskHit(Task&) {
    for (int p = 0; p < 2; ++p) {
        hitFlag[p] = 0;
        Player& P = player[p];
        if (!P.pending) continue;
        std::vector<HitEntry> entries;
        std::vector<LevelShotHit> level;
        for (const Target& tg : targets) {
            ActorTask* a = tg.obj;
            if (a->flags34 & 0x10) {  // Hit_TestObjectMesh 0x00404A00 (world_collision.cpp)
                LevelShotHit h;
                if (ShotObjMesh(p, *a, h)) {
                    level.push_back(h);
                    entries.push_back({ h.key, a, static_cast<int>(level.size() - 1), a->flags34 | 0x50 });
                }
                continue;
            }
            // Hit_TestObjectSphere 0x00404630
            if (!ShotHitsSphere(P.ray, a->viewPos, a->radius)) continue;
            const int kind = a->SkeletonKind();
            const Skeleton* sk = kind >= 0 ? &EnemySkeleton(kind) : nullptr;
            if ((a->flags34 & 0x80) && sk && !sk->nodes.empty() && !sk->nodes[0].children.empty() &&
                !(a->flags34 & 0x8000)) {
                // Hit_TestSkeleton 0x00404700 / Hit_TestNodeRecursive 0x00404750 / Hit_TestNodeSphere 0x004047D0
                std::vector<int> stack;
                std::function<void(int)> visit = [&](int i) {
                    const SkeletonNode& d = sk->nodes[i];
                    EnemyNode* n = a->Node(d.index);
                    if (n && n->model != 0) {
                        if (!(n->flags & 0x10)) {
                            if (n->radius != 0.0f && ShotHitsSphere(P.ray, n->hit, n->radius))
                                entries.push_back({ HitSortKey(n->hit[2]), a, d.index, n->flags });
                        } else if (n->mesh != -1) {  // 0x004048A0 (world_st2c_enemy.cpp)
                            LevelShotHit h;
                            if (ShotNodeMesh(p, *n, h)) {
                                h.obj = a;
                                level.push_back(h);
                                entries.push_back({ h.key, a, d.index, n->flags | 0x50, static_cast<int>(level.size() - 1) });
                            }
                        }
                    }
                    for (int c : d.children) visit(c);
                };
                for (int c : sk->nodes[0].children) visit(c);
            } else {
                entries.push_back({ HitSortKey(a->viewPos[2]), a, -1, a->flags34 });
            }
        }
        // Hit_TestLevel 0x00404B80: level collision meshes (lists 0x009C7040 / 0x009CA0C0), world_collision.cpp
        const size_t firstLevel = level.size();
        ShotLevel(p, level);
        for (size_t i = firstLevel; i < level.size(); ++i) entries.push_back({ level[i].key, nullptr, static_cast<int>(i), 0x10 });
        P.pending = 0;
        if (entries.empty()) continue;
        // Hit_Deliver 0x00404DB0 (Hit_SortByDepth 0x00405080: stable radix sort on the key's low 16 bits)
        std::vector<std::pair<uint32_t, int32_t>> keys;
        for (size_t i = 0; i < entries.size(); ++i)
            keys.push_back({ static_cast<uint32_t>(entries[i].key), static_cast<int32_t>(i) });
        SortHitKeys(keys);
        const HitEntry& e = entries[keys[0].second];
        const uint32_t bit = 2u << p;
        if (!e.obj) {  // level hit: Hit_RecordImpact 0x00405260
            ShotImpact(p, level[static_cast<size_t>(e.node)]);
        } else if (!(e.flags & 0x20)) {
            if (!(e.flags & 0x10) || (e.flags & 0x40)) {
                e.obj->flags34 |= bit | 8;
                e.obj->hitNode[p] = 1;
            }
            if (e.flags & 0x10) ShotImpact(p, level[static_cast<size_t>(e.node)]);  // object mesh: Hit_RecordImpact
        } else {
            e.obj->flags34 |= bit | 8;
            EnemyNode* n = e.obj->Node(e.node);
            if (n) n->flags |= bit | 8;
            e.obj->hitNode[p] = static_cast<int8_t>(e.node);
            if (n && (n->flags & 0x10)) {
                if (e.level >= 0) ShotImpact(p, level[static_cast<size_t>(e.level)]);  // Hit_RecordImpact 0x00405260
                else Note("Hit_RecordImpact 0x00405260 (impact effect) without a mesh hit");
            }
        }
        // game type 1, weapon effect 3 (0x009A2248 + p * 0x14 + 1): effect 0x53 at the hit (a mesh hit: its point; a
        // sphere: the view-space centre pushed back by radius - 1, x the camera matrix)
        if ((cfg.gameType == 1 || HordeActive(*this)) && modes && static_cast<int8_t>(modes->pitems[p].b8[1]) == 3) {
            float at[3];
            if (e.flags & 0x10) {
                const int li = !e.obj ? e.node : (e.flags & 0x20) ? e.level : e.node;
                if (li >= 0 && li < static_cast<int>(level.size())) memcpy(at, level[static_cast<size_t>(li)].pos, 12);
                else memset(at, 0, 12);
            } else {
                const EnemyNode* n = (e.flags & 0x20) ? e.obj->Node(e.node) : nullptr;
                const float* c = n ? n->hit : e.obj->viewPos;
                const float r = n ? n->radius : e.obj->radius;
                const float v[3] = { c[0], c[1], (r - 1.0f) + c[2] };
                mx::Transform(cam.inverse, v, at);
            }
            SpawnEffectAt(at, 0, 0, 0x53, -1, p);  // 0x00407340
        }
        hitFlag[p] = 1;
    }
    targetCopy = targets;  // Hit_CopyTargets 0x00405360
    targets.clear();
}

// =====================================================================================================================
// Actors
// =====================================================================================================================

// Ops 0x09 / 0x0A / 0x0B / 0x0C: one actor task per record, function = actor type table 0x009A2280[type]
// (0x004088A0 / 0x00408990 / 0x00408A20 / 0x00408BC0); it first runs at the end of this frame.
void World::Impl::Spawn(uint32_t kind, uint32_t record) {
    SpawnRecord r;
    if (!r.Read(mem, record)) {
        // a record at the very end of the stage table (st1evtbl's boss-select record 0x0097E044, trnevtbl's 0x009774B4):
        // the original reads on into the table buffer past the file (zeros after a boot without a larger table)
        uint32_t w0 = 0;
        if (!mem.Read(record, &w0)) {
            Note("spawn record unreadable");
            return;
        }
        r = SpawnRecord{};
        r.addr = record;
        r.type = w0;
        uint32_t w = 0;
        if (mem.Read(record + 4, &w)) r.flags = w;
    }
    const uint32_t tfn = r.type < 0x70 ? actorType[r.type] : 0x0041EBB0;
    if (tfn == fn::kEnemyCreate) {
        if (RandomizerSpawn(*this, kind, r)) return;
        EnemyTask* e = NewTask<EnemyTask>(Tramp<&Impl::TaskEnemy>, fn::kEnemyCreate);
        e->lock = &lockTargets[nextLockTarget++];
        e->flags34 = r.flags | 1;  // 0x00408970
        memcpy(e->pos, r.pos, 12);
        memcpy(e->rot, r.rot, 12);
        e->hp = e->hpMax = static_cast<int16_t>(r.state);
        e->lo = static_cast<int16_t>(r.lo);
        e->record = r;
        if (kind == 0x09) {
            e->anim.kind = static_cast<int16_t>(static_cast<int8_t>(r.params[0]));
            e->variant = static_cast<int8_t>(r.params[1]);
        }
        if (kind == 0x0C) Note("op 0x0C enemy (params at +0x130C) not ported");
        return;
    }
    if (tfn == 0x0045CD60) {  // actor 0x18: an enemy riding the last vehicle (world_st3_vehicle.cpp)
        EnemyTask* e = SpawnVehicleEnemy(r);
        if (kind == 0x09) {
            e->anim.kind = static_cast<int16_t>(static_cast<int8_t>(r.params[0]));
            e->variant = static_cast<int8_t>(r.params[1]);
        }
        return;
    }
    if (tfn == 0x0048A3E0) {  // scripted civilian (world_civilian.cpp)
        CivTask* c = NewTask<CivTask>(Tramp<&Impl::TaskCivilian>, 0x0048A3E0);
        c->lock = &lockTargets[nextLockTarget++];
        c->flags34 = r.flags | 1;  // 0x00408970
        memcpy(c->pos, r.pos, 12);
        memcpy(c->rot, r.rot, 12);
        c->hp = c->hpMax = static_cast<int16_t>(r.state);
        c->lo = static_cast<int16_t>(r.lo);
        c->record = r;
        return;
    }
    SpawnTask* s = NewTask<SpawnTask>(Tramp<&Impl::TaskSpawn>, tfn);
    s->rec = r;
    s->op = kind;
}

// First run of a spawned (non-enemy) actor: the actor type function.
void World::Impl::TaskSpawn(Task& t) {
    SpawnTask& s = t.As<SpawnTask>();
    if (modes && SpawnModes(*this, t, s)) return;  // world_modes_boss.cpp: select screens 0x6C / 0x6D / 0x6E
    if (SpawnStory(*this, t, s)) return;           // world_modes_story.cpp: mode-11 title card, actor type 0x50
    if (SpawnStage6(t, s)) return;    // world_st6.cpp
    if (SpawnStage5(t, s)) return;    // world_st5_actors.cpp
    if (SpawnStage4(t, s)) return;    // world_st4_actors.cpp
    if (SpawnStage3(t, s)) return;    // world_st3_actors.cpp
    if (SpawnWater(*this, t, s)) return;  // world_st2_water.cpp: actor types 0x15 / 0x17
    if (SpawnStage2(t, s)) return;    // world_st2_actors.cpp
    if (SpawnRoutes(t, s)) return;    // world_routes.cpp
    if (SpawnScripted(t, s)) return;  // world_actors.cpp
    const SpawnRecord r = s.rec;
    PropWorld w = PropState();
    switch (r.type) {
    case 0x41: {  // 0x00461CD0: komono kind ctor table 0x00593580[kind]
        const int kind = static_cast<int8_t>(r.params[1]);
        if (kind == 4) {  // 0x00462E10
            BreakableTask* b = NewTask<BreakableTask>(Tramp<&Impl::TaskBreakable>, fn::kBreakable);
            SetSites({ 0x00462F47 });
            b->p.InitKind4(r, w, breakable);
            b->flags34 = b->p.flags;
            b->radius = b->p.hitRadius;
        } else if (kind == 45) {  // generic ctor 0x00461CF0 -> 0x005936BC[45]
            DecorTask* d = NewTask<DecorTask>(Tramp<&Impl::TaskDecor>, fn::kDecor);
            d->p.Init(r, w);
        } else if (!SpawnKomonoModes(*this, r, kind)) {  // kinds 73 / 75 / 76: world_modes_items.cpp
            Note("komono kind " + std::to_string(kind) + " not ported");
        }
        AfterPropUpdate(w);
        ts.KillCurrent();
    }
    case 0x44: {  // 0x00472B10: state table 0x00595AB8
        if (r.state == 1) {
            HangingTask* h = NewTask<HangingTask>(Tramp<&Impl::TaskHanging>, fn::kHanging);
            h->p.Init(r);
            h->flags34 = h->p.flags;
        } else if (r.state == 4) {  // 0x00472EB0: as state 1 (task 0x00473CF0), other parameter offsets and a scale
            HangingTask* h = NewTask<HangingTask>(Tramp<&Impl::TaskHanging>, fn::kHanging);
            h->p.Init(r);
            h->p.amp = r.ParamS32(0xC);
            h->p.phase = 0;
            h->p.triggerFlag = r.ParamS8(0x10);
            h->p.killFlag = r.ParamS8(0x11);
            for (int i = 0; i < 3; ++i) memcpy(&h->p.scale[i], &r.params[0x14 + 4 * i], 4);
            h->flags34 = h->p.flags;
        } else if (r.state == 3) {
            AnimatedTask* a = NewTask<AnimatedTask>(Tramp<&Impl::TaskAnimated>, fn::kAnimated);
            a->p.Init(r, w);
            a->flags34 = a->p.flags;
        } else if (r.state == 15) {
            BreakableProp tmp;
            SetSites({ 0x00473912 });
            if (tmp.InitState15(r, w, breakable)) {
                BreakableTask* b = NewTask<BreakableTask>(Tramp<&Impl::TaskBreakable>, fn::kBreakable);
                b->p = tmp;
                b->flags34 = b->p.flags;
                b->radius = b->p.hitRadius;
            }
        } else {
            Note("actor type 0x44 state " + std::to_string(r.state) + " not ported");
        }
        AfterPropUpdate(w);
        ts.KillCurrent();
    }
    case 0x33: {  // 0x00432FF0: function by state (+0x11C), actor slot 0x00409270
        if (r.state == 3) {
            s.fn = fn::kType33State3;
            t.fn = Tramp<&Impl::TaskType33>;
        } else {
            Note("actor type 0x33 state " + std::to_string(r.state) + " not ported");
            t.fn = Tramp<&Impl::TaskPlaceholder>;
        }
        for (auto& a : actorSlots)
            if (!a) {
                a = &s;
                break;
            }
        return;
    }
    case 0x10:  // 0x0048A3E0 -> 0x0048A920
        // created as CivTask by Spawn (world_civilian.cpp); not reached
        Note("actor type 0x10 reached the generic spawn path");
        t.fn = Tramp<&Impl::TaskPlaceholder>;
        return;
    default:
        Note("actor type " + std::to_string(r.type) + " not ported: placeholder task");
        t.fn = Tramp<&Impl::TaskPlaceholder>;
        return;
    }
}

// 0x00433AC0 (type 0x33 state 3): water splash 0x62 at the actor, then Actor_Destroy
void World::Impl::TaskType33(Task& t) {
    SpawnTask& s = t.As<SpawnTask>();
    SpawnEffectAt(s.rec.pos, 0, 0, 0x62, 1, -1);
    for (auto& a : actorSlots)
        if (a == &s) a = nullptr;
    ts.KillCurrent();
}

void World::Impl::TaskPlaceholder(Task&) {}

// Eff_Task 0x00407A70
void World::Impl::TaskEffect(Task& t) {
    EffectTask& e = t.As<EffectTask>();
    int model = 0;
    Mat4 w;
    const bool alive = e.e.Step(&model, &w);
    if (model != 0) Draw(model, w, 0xE);
    if (!alive) ts.KillCurrent();
}

// 0x004666B0(obj, player, scale): hit spark at the player's aim on the object's view depth (+0x78), z taken from
// the object's +0x1A4 (the original overwrites the transformed z with it)
void World::Impl::SpawnSpark(float viewZ, int p, float scale, float z) {
    SparkTask* s = NewTask<SparkTask>(Tramp<&Impl::TaskSpark>, fn::kSpark);
    s->model = 0xE25;
    s->scale = scale * F32(exe, 0x004C4CB8);
    s->roll = 0;
    const float fx = (player[p].aim[0] * viewZ) / focal, fy = (player[p].aim[1] * viewZ) / focal;
    const float local[3] = { -fx, -fy, viewZ };
    mx::Transform(cam.inverse, local, s->pos);
    s->pos[2] = z;
}

// Hit spark 0x004667A0 (created by 0x004666B0)
void World::Impl::TaskSpark(Task& t) {
    SparkTask& s = t.As<SparkTask>();
    if (++s.model > 0xE33) ts.KillCurrent();
    // push; T(pos); 0x004A9F70 (rotation = identity: faces the screen); RotZ(roll); Scale(s); draw; pop
    Mat4 m = cam.view;
    mx::Translate(m, s.pos[0], s.pos[1], s.pos[2]);
    mx::ClearRotation(m);
    mx::RotZ(m, s.roll);
    mx::Scale(m, s.scale, s.scale, s.scale);
    Draw(s.model, m, 8, -1.0f, false, -1, true);
}

// Hanging prop 0x00473CF0
// A hanging-prop task (0x00473CF0) created by another actor with a prepared block (actor 0x44 state 5, world_routes.cpp)
void World::Impl::SpawnHangingCopy(const HangingProp& p) {
    HangingTask* h = NewTask<HangingTask>(Tramp<&Impl::TaskHanging>, fn::kHanging);
    h->p = p;
    h->flags34 = h->p.flags;
}

void World::Impl::TaskHanging(Task& t) {
    HangingTask& h = t.As<HangingTask>();
    PropWorld w = PropState();
    h.p.flags |= h.flags34 & 0xE;
    const PropResult r = h.p.Update(w, hanging);
    AfterPropUpdate(w);
    if (r != PropResult::Alive) ts.KillCurrent();
    const PropDraw d = h.p.Draw(w);
    Draw(d.model, d.world, 8, -1.0f, d.alt);
    h.flags34 = h.p.flags;
    if (h.p.HitTarget()) {
        // collision-mesh target: +0x14C = the mesh, +0x150 = the draw's model-view (0x004A8CA0), made world by
        // Hit_RegisterTarget (shots 0x00404A00, actor pushes 0x004057F0)
        const HangingProp& p = h.p;
        Mat4 m = cam.view;
        mx::Translate(m, p.pos[0], p.pos[1], p.pos[2]);
        mx::RotY(m, p.baseRot[1]);
        mx::RotZ(m, p.rot[2]);
        mx::RotY(m, p.rot[1]);
        mx::RotX(m, p.rot[0]);
        if (p.model == 0x1817 || p.model == 0x1816) mx::Scale(m, 1.05f, 1.0f, 1.0f);  // 0x3F866666
        else mx::Scale(m, p.scale[0], p.scale[1], p.scale[2]);
        h.objMesh = p.hitId;
        h.m150 = m;
        RegisterTarget(&h);
    }
}

// Breakable prop 0x00465FB0
void World::Impl::TaskBreakable(Task& t) {
    BreakableTask& b = t.As<BreakableTask>();
    PropWorld w = PropState();
    const uint32_t shot = b.flags34 & 0xE;
    const size_t ev = b.p.events.size();
    SetSites({ 0 });  // 0x004650F0 shooter choice / shake: call sites not identified
    const PropResult r = b.p.Update(w, breakable, shot, MotionTicks(b.p.anim.motion));
    AfterPropUpdate(w);
    for (size_t i = ev; i < b.p.events.size(); ++i) {
        const std::string& e = b.p.events[i];
        if (e.rfind("hit spark", 0) == 0) {  // 0x004666B0(prop, player, 1.5)
            SpawnSpark(b.viewPos[2], (shot & 2) ? 0 : 1, 1.5f, b.p.pos[2]);
        } else if (e.rfind("score", 0) == 0) {  // 0x004650F0: +10 and a hit for the shooter
            const int p = (shot & 2) ? 0 : 1;
            AddScore(p, 10);
            if (GlobalInt(0x009A5C48, 0) == 0) ++player[p].hits;
        } else if (e == "effect 0x00465860") {  // 0x004650F0(flags, 0): a hit; aim effect 0x00465860 (world_st3_actors.cpp)
            const int p = (shot & 2) ? 0 : 1;
            if (GlobalInt(0x009A5C48, 0) == 0) ++player[p].hits;
            SpawnAimFx(b.viewPos[2], p, b.p.pos[2]);
        } else if (e.rfind("item drop slot ", 0) == 0 && runner->cfg.gameType == 1 && modes && b.p.field2A0 != -1) {
            // game type 1: an original-mode item (0x00467B90, slot = +0x2A0); 0x007DCD14 = 1 in stage 2 scene 4
            SetItemTaken(*this, cfg.eventMode == 2 && runner->scene == 4 ? 1 : 0);
            float pos[3] = { b.p.pos[0], b.p.pos[1], b.p.pos[2] };
            if (b.p.subKind == 2) pos[1] = pos[1] + 1.0f;
            SpawnItemAt(*this, pos, b.p.areaSeen, b.p.areaChanges, b.p.lifetime, b.p.field2A0);  // world_modes_items.cpp
        } else if (e.rfind("item drop slot ", 0) == 0) {  // 0x004723F0 / 0x00471BD0 / 0x004722A0 (world_scene1.cpp)
            DropItem(b.p, atoi(e.c_str() + 15));
            if (atoi(e.c_str() + 15) == 3) b.p.flags = (b.p.flags & ~0x80u) | 0x80080000u;  // 0x004722A0 on the prop
        } else {
            Note("breakable prop: " + e + " not ported");
        }
    }
    if (r != PropResult::Alive) ts.KillCurrent();
    const bool loaded = MotionResident(b.p.anim.motion);
    const Motion* m = PropMotion(b.p.anim.motion);
    SetSites({ 0 });  // model 0x10CE random scale (0x0040D9D0)
    for (const PropDraw& d : b.p.Draw(w, breakable, PropSkel(b.p.anim.skeleton), *m, loaded, cam.view)) {
        if (d.approx) Note("prop skeleton big-angle interpolation (0x00412750) approximated");
        Draw(d.model, d.world, 8, -1.0f, d.alt);
    }
    b.flags34 = b.p.flags;
    if (b.p.HitTarget()) {
        memcpy(b.viewPos, b.p.hitPoint, 12);
        b.radius = b.p.hitRadius;
        RegisterTarget(&b);
    }
}

// Komono kind 45 decoration 0x0046DAB0
void World::Impl::TaskDecor(Task& t) {
    DecorTask& d = t.As<DecorTask>();
    PropWorld w = PropState();
    const PropResult r = d.p.Update(w);
    AfterPropUpdate(w);
    if (r != PropResult::Alive) ts.KillCurrent();
    for (const PropDraw& x : d.p.Draw(exe)) Draw(x.model, x.world);
    if (r == PropResult::Alive)
        for (int k = 0; k < 4; ++k) modelWave[k] = d.p.WavePhase(k);  // the edited model data stays edited
}

// End of frame: the draws of the waved models get the phase their model data has now (WorldDraw::wavePhase)
void World::Impl::StampModelWaves() {
    for (WorldDraw& d : draws)
        if (d.model >= 0x1731 && d.model <= 0x1734) d.wavePhase = modelWave[d.model - 0x1731];
}

// Animated prop 0x00474120
void World::Impl::TaskAnimated(Task& t) {
    AnimatedTask& a = t.As<AnimatedTask>();
    PropWorld w = PropState();
    const PropResult r = a.p.Update(w, MotionTicks(a.p.anim.motion));
    AfterPropUpdate(w);
    if (r != PropResult::Alive) ts.KillCurrent();
    w.motionState = MotionResident(a.p.anim.motion) ? 2 : 0;
    Mat4 stored;
    const PropAnim before = a.p.anim;
    for (const PropDraw& d : a.p.Draw(w, PropSkel(a.p.anim.skeleton), *PropMotion(a.p.anim.motion), &stored))
        Draw(d.model, d.world, 8, -1.0f, d.alt);
    a.flags34 = a.p.flags;
    // +0x64.. = 0x0040E070's rotation entry (+0x2A0 - 1: the trigger flag's number, past the motion's own data in the
    // original: the shot normal of 0x00404A00 turns by it) - not modelled (0)
    if (w.motionState == 2) AnimatedPropMesh(a, a.p, before);  // +0x14C / +0x150 (world_st1b.cpp)
    if (a.p.hitId != -1 && w.motionState == 2) RegisterTarget(&a);
}

// =====================================================================================================================
// Load / public API
// =====================================================================================================================

World::World() : impl_(std::make_unique<Impl>()) {}
World::~World() = default;

bool World::Load(const std::string& gameDir, int eventMode, std::string* error) {
    Impl& w = *impl_;
    w.gameDir = gameDir;
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        return false;
    };
    if (!w.exe.Load((fs::path(gameDir) / "Hod2.exe").string())) return fail("cannot load Hod2.exe");
    // comevtbl.bin + the stage table 0x004D1C7C[0x00579928[mode]] (Evt_LoadCommonTable 0x00413070 /
    // Evt_LoadTable 0x00413160)
    const std::vector<uint8_t> common = ReadAll(fs::path(gameDir) / "evt" / "comevtbl.bin");
    if (common.empty()) return fail("cannot read evt/comevtbl.bin");
    w.mem.Load(EventMemory::kCommonBase, common);
    std::string err;
    if (!w.LoadStageTable(eventMode, &err)) return fail(err);
    w.skyTable = LoadSkyTable(w.exe);
    w.hanging.Load(w.exe);
    w.breakable.Load(w.exe);
    w.enemyTables.Load(w.exe);
    w.cameraBank.Load(w.exe, [&](const std::string& n) { return ReadAll(fs::path(gameDir) / "cam" / n); });
    w.motionFiles = ListMotionFiles(w.exe);
    // Actor_InitTypeTable 0x0040AC90: default 0x0041EBB0, then (type, fn) pairs from 0x00593358 until type < 0
    for (uint32_t& f : w.actorType) f = 0x0041EBB0;
    for (uint32_t a = 0x00593358;; a += 8) {
        const int32_t type = static_cast<int32_t>(w.exe.U32(a));
        if (type < 0 || type >= 0x70) break;
        w.actorType[type] = w.exe.U32(a + 4);
    }
    double halfFov = 0;
    w.exe.Read(kHalfFovVa, &halfFov, 8);
    w.focal = ShotFocalLength(halfFov);
    return true;
}

// Evt_LoadTable 0x00413160: the stage table 0x004D1C7C[0x00579928[mode]] and the stage's section objects
bool World::Impl::LoadStageTable(int eventMode, std::string* error, int gameType) {
    const int tableIndex = static_cast<int>(exe.U32(0x00579928 + 4 * eventMode));
    std::string table = exe.CStr(exe.U32(0x004D1C7C + 4 * tableIndex));
    if (table.empty()) table = "st1evtbl.bin";
    const std::vector<uint8_t> st = ReadAll(fs::path(gameDir) / "evt" / table);
    if (st.empty()) {
        if (error) *error = "cannot read evt/" + table;
        return false;
    }
    mem.Load(EventMemory::kStageBase, st);
    // Stage_SelectTables 0x004014C0: game type 1 (original mode) has its own section tables
    if (gameType < 0) gameType = frontEnd ? front.gameType : cfg.gameType;
    sections.Load(exe, eventMode, gameType == 1);
    NoteSectionPolFiles();
    return true;
}

// The section objects' pol files (0x004E83B4[model]) for the port's texture prefetch (WorldHud::polFileLoads): the
// stage / title geometry is drawn from the first frame on, not loaded through the resource queue.
void World::Impl::NoteSectionPolFiles() {
    for (const auto& [modelId, mode] : sections.objects) {
        uint16_t file = 0;
        if (modelId > 0 && exe.Read(0x004E83B4 + static_cast<uint32_t>(modelId) * 2, &file, 2) &&
            std::find(polFileLoads.begin(), polFileLoads.end(), static_cast<int>(file)) == polFileLoads.end())
            polFileLoads.push_back(file);
    }
}

bool World::StartStage(const WorldConfig& cfg, std::string* error) {
    // The in-game network lobby starts through this public entry point. TitleFrame replaced the
    // section table installed by Load(); an attract demo may also have replaced the event table.
    // Mirror GameStartFrame's Evt_LoadTable / Stage_SelectTables before creating the stage tasks.
    if (!impl_->LoadStageTable(cfg.eventMode, error, cfg.gameType)) return false;
    if (impl_->frontEnd) impl_->front.gameType = cfg.gameType;
    impl_->StartStage(cfg);
    return true;
}
bool World::StartNetworkGame(const WorldConfig& cfg, std::string* error) {
    World fresh;
    if (!fresh.Load(impl_->gameDir, cfg.eventMode, error)) return false;
    WorldConfig boot = cfg;
    boot.startFrame = 0;
    fresh.Boot(boot);
    if (cfg.portNetworkRoom) {
        auto& w = *fresh.impl_;
        w.frame = cfg.startFrame - 1;
        w.seedCounter = cfg.seedCounter - 1;
        w.front.modeRequest = 15;
        w.FrontApplyMode();
        w.FrontStep();
    } else {
        // Boot has applied the shared save. Preserve those gameplay settings while
        // retaining the requested start frame, seed, player slots and current credits.
        WorldConfig start = cfg;
        start.difficulty = fresh.impl_->cfg.difficulty;
        start.livesSetting = fresh.impl_->cfg.livesSetting;
        start.creditSetting = fresh.impl_->cfg.creditSetting;
        if (!fresh.StartStage(start, error)) return false;
    }
    impl_.swap(fresh.impl_);
    return true;
}
bool World::NetworkRoom() const { return impl_->cfg.portNetworkRoom; }
bool World::NetworkLobby() const { return NetworkRoom() && impl_->gameMode == 15; }
void World::LeaveNetworkRoom() {
    impl_->cfg.portNetworkRoom = false;
    RequestTitle();
}
void World::Step() {
    impl_->hud.polFileLoads.clear();  // this frame's pol file loads (FillHud appends)
    impl_->Step();
    if (impl_->modes) impl_->modes->clockOverride = -0x7FFFFFFF;  // SetWallClock: one frame
}
void World::SetWallClock(int count) {
    if (impl_->modes) impl_->modes->clockOverride = count;
}
bool World::QuitRequested() const { return impl_->front.quit; }
void World::SetPortVolumes(const int v[5]) {
    for (int k = 0; k < 5; ++k) {
        impl_->cfg.portVolumes[k] = v[k];
        if (impl_->modes && impl_->modes->optPortRows) impl_->modes->optVolume[k] = v[k];
    }
}
PortGameOptions World::GetPortGameOptions() const {
    PortGameOptions result;
    const auto& w = *impl_;
    if (!w.modes || !w.modes->save.Valid()) return result;
    const auto& save = w.modes->save;
    result.difficulty = std::clamp<int>(save.U8(SaveData::kDifficulty), 0, 4);
    result.lives = std::clamp<int>(save.U8(SaveData::kLives), 0, 4) + 1;
    result.credits = save.S8(SaveData::kCredits) == -1 ? 0 : std::clamp<int>(save.U8(SaveData::kCredits), 1, 9);
    for (int p = 0; p < 2; ++p)
        result.crosshair[p] = std::clamp<int>(save.U8(SaveData::kPlayerCfg + p * 0x7C), 0, 3);
    result.freePlayUnlocked = (save.U8(SaveData::kFlags) & 7) == 7;
    result.editable = w.cfg.portFeatures && w.frontEnd && !w.cfg.portNetworkRoom && !RandomizerActive(w) &&
                      (w.gameMode == 3 || w.gameMode == 4 || w.gameMode == 12);
    return result;
}
bool World::SetPortGameOptions(const PortGameOptions& options) {
    const auto current = GetPortGameOptions();
    if (!current.editable || options.difficulty < 0 || options.difficulty > 4 || options.lives < 1 || options.lives > 5 ||
        options.credits < 0 || options.credits > 9 || (options.credits == 0 && !current.freePlayUnlocked) ||
        options.crosshair[0] < 0 || options.crosshair[0] > 3 || options.crosshair[1] < 0 || options.crosshair[1] > 3)
        return false;
    auto& w = *impl_;
    auto& m = *w.modes;
    m.save.SetU8(SaveData::kDifficulty, static_cast<uint8_t>(options.difficulty));
    m.save.SetU8(SaveData::kLives, static_cast<uint8_t>(options.lives - 1));
    m.save.SetU8(SaveData::kCredits, options.credits == 0 ? 0xFF : static_cast<uint8_t>(options.credits));
    for (int p = 0; p < 2; ++p) {
        m.save.SetU8(SaveData::kPlayerCfg + p * 0x7C, static_cast<uint8_t>(options.crosshair[p]));
        m.optCross[p] = static_cast<int8_t>(options.crosshair[p]);
    }
    // Keep the original page's cached copies coherent for Classic/exactness tools without entering its mode.
    m.optDifficulty = static_cast<int8_t>(options.difficulty);
    m.optLives = static_cast<int8_t>(options.lives - 1);
    m.optCredits = static_cast<int8_t>(options.credits);
    m.Store(w);
    return true;
}
bool World::ResetPortGameOptions() {
    auto options = GetPortGameOptions();
    if (!options.editable) return false;
    // DefaultSettings also contains old device calibration. Read its defaults from a copy, then reset only the
    // preferences this page owns so an attached gun and the player's progression are never erased by this action.
    auto defaults = impl_->modes->save;
    defaults.DefaultSettings(impl_->exe);
    options.difficulty = defaults.U8(SaveData::kDifficulty);
    options.lives = defaults.U8(SaveData::kLives) + 1;
    options.credits = defaults.S8(SaveData::kCredits) == -1 ? 0 : defaults.U8(SaveData::kCredits);
    for (int p = 0; p < 2; ++p) options.crosshair[p] = defaults.U8(SaveData::kPlayerCfg + p * 0x7C);
    return SetPortGameOptions(options);
}
void World::SetPortModernMenu(bool on) {
    auto& w=*impl_;
    w.cfg.portModernMenu=on;
    if(w.gameMode==4) {
        for(size_t i=0;i<std::min<size_t>(6,w.front.screens.size());++i)w.front.screens[i].visible=!on;
        w.front.nightShiftPage=w.front.nightShiftRow=0;
    }
}
void World::RequestTitle() {
    Impl& w = *impl_;
    w.paused = false;  // 0x009A1A00 (the original never leaves a game while paused)
    w.pauseHist[0] = w.pauseHist[1] = false;
    if (w.frontEnd) {
        w.front.modeRequest = 3;
        w.FrontApplyMode();  // Game_ApplyState 0x0040E860, between frames (as ForceMode)
    } else {
        w.SetGameMode(3);
    }
}
void World::SetInput(int player, const WorldInput& in) {
    if (player < 0 || player > 1) return;
    impl_->input[player] = in;
    impl_->inputEdge[player] = true;
}
int World::GameMode() const { return impl_->gameMode; }
int World::Frame() const { return impl_->frame; }
void World::Civilians(int* rescued, int* met) const {
    *rescued = impl_->rescueTotal;
    *met = impl_->civTotal;
}
bool World::TestPoke(uint32_t addr, const std::vector<uint8_t>& bytes) {
    int32_t v = 0;
    for (size_t i = 0; i < bytes.size() && i < 4; ++i) v |= static_cast<int32_t>(bytes[i]) << (8 * i);
    if (bytes.size() == 2 && addr == 0x009CA0EC) impl_->rescueTotal = static_cast<int16_t>(v);
    else if (bytes.size() == 2 && addr == 0x009A21BA) impl_->civTotal = static_cast<int16_t>(v);
    else if (bytes.size() == 4 && (addr == 0x009A5C6C || addr == 0x009A5D9C)) impl_->player[addr == 0x009A5C6C ? 0 : 1].score = v;
    else return false;
    return true;
}
const WorldCamera& World::Camera() const { return impl_->cam; }
void World::ProjCenter(float* x, float* y) const {
    *x = impl_->projCenterFrame[0];
    *y = impl_->projCenterFrame[1];
}
WorldCamera World::ContextNow() const {
    WorldCamera w;
    if (!impl_->runner) return impl_->cam;
    const CameraDirector& c = impl_->runner->camera;
    const CameraContext& x = c.ctx[c.activeCtx & 3];
    w.ctx = c.activeCtx;
    memcpy(w.pos, x.pos, 12);
    w.pitch = x.pitch;
    w.yaw = x.yaw;
    w.roll = x.roll;
    return w;
}
const std::vector<WorldDraw>& World::Draws() const { return impl_->draws; }
const WorldHud& World::Hud() const { return impl_->hud; }
const std::vector<World::RandCall>& World::RandCalls() const { return impl_->randCalls; }
uint32_t World::RandState() const { return impl_->rng.state; }
std::vector<World::TaskInfo> World::Tasks() const {
    std::vector<TaskInfo> out;
    for (Task* t : impl_->ts.Live()) {
        const WTask* w = t->data ? static_cast<const WTask*>(t->data.get()) : nullptr;
        out.push_back({ t->serial, w ? w->fn : 0 });
    }
    return out;
}
const std::vector<WorldEventOp>& World::EventOps() const { return impl_->ops; }
std::vector<World::EnemyInfo> World::Enemies(bool civilians) const {
    std::vector<EnemyInfo> out;
    for (Task* t : impl_->ts.Live()) {
        if (!t->data) continue;
        const WTask* w = static_cast<const WTask*>(t->data.get());
        if (w->fn != fn::kEnemy && w->fn != fn::kEnemyCreate && !(civilians && (w->fn == 0x0048A920 || w->fn == 0x0048A3E0 || w->fn == 0x00449910))) continue;  // (+ actor 0x31: traces)
        const EnemyTask& e = static_cast<const EnemyTask&>(*w);
        EnemyInfo i{};
        i.serial = t->serial;
        i.kind = e.anim.kind;
        i.hp = e.hp;
        i.behaviour = e.behaviour;
        i.sub = e.sub;
        i.motion = e.anim.motion[0];
        i.counter = e.anim.counter[0];
        i.flags34 = e.flags34;
        i.flags136C = e.flags136C;
        memcpy(i.pos, e.pos, 12);
        memcpy(i.body, e.body, 12);
        memcpy(i.viewPos, e.viewPos, 12);
        memcpy(i.rot, e.rot, 12);
        i.tick = e.anim.tick[0];
        i.target = e.target;
        out.push_back(i);
    }
    return out;
}
int World::LiveEnemies() const { return impl_->liveEnemies; }
bool World::LockOnActive(int k) const { return k >= 0 && k < 16 && impl_->lockOn.on[k]; }
bool World::LockOnInfo(int k, float point[3], int* targetPlayer) const {
    if (k < 0 || k >= 16 || !impl_->lockOn.on[k] || !impl_->lockOn.obj[k]) return false;
    memcpy(point, impl_->lockOn.obj[k]->point, 12);
    *targetPlayer = impl_->lockOn.obj[k]->targetPlayer;
    return true;
}
int World::Attackers() const { return impl_->attackers; }
const std::vector<World::SoundEvent>& World::Sounds() const { return impl_->sounds; }
const std::vector<std::string>& World::Notes() const { return impl_->notes; }
const EventRunner& World::Events() const { return *impl_->runner; }
PrototypeStatus World::InspectPrototype() const { return PrototypeInspect(*impl_); }

}  // namespace game
