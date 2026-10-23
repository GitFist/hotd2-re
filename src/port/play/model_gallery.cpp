#include "model_gallery.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <utility>

#include "game/anim.h"
#include "game/menu_audio.h"
#include "game/menu_text.h"
#include "game/mstack.h"
#include "game/skin.h"
#include "game/sound_table.h"
#include "play/scroll_hint.h"

namespace play {
namespace {
constexpr uint32_t kWhite = 0xFFF3F0E5, kMuted = 0xFFAAA79F, kRed = 0xFFF26556;
constexpr int kVisible = 9;
bool Inside(float x, float y, float l, float t, float r, float b) { return x >= l && x < r && y >= t && y < b; }
int Wrap(int value, int size) { return size ? (value % size + size) % size : 0; }

struct ListBar {
    float left, top, right, bottom;
    int count, first;
    int Max() const { return std::max(0,count-kVisible); }
    float Height() const { return Max()?std::max(24.f,(bottom-top)*kVisible/count):bottom-top; }
    float Thumb() const { return top+(Max()?(bottom-top-Height())*first/Max():0.f); }
    bool Hit(float x,float y) const { return Inside(x,y,left,top,right,bottom); }
    int At(float y,float grab) const {
        const float travel=bottom-top-Height();
        return travel>0?std::clamp(static_cast<int>(std::lround((y-top-grab)*Max()/travel)),0,Max()):0;
    }
};

void Panel(std::vector<port::ModelRenderer::SpriteDraw>& out, float l, float t, float r, float b,
           uint32_t color, float horizontalScale) {
    port::ModelRenderer::SpriteDraw d{};
    l = 320 + (l - 320) * horizontalScale; r = 320 + (r - 320) * horizontalScale;
    d.depth = .02f - static_cast<float>(out.size()) * .000001f;
    d.isp = 0xE4000000; d.tsp = 0x94918000;
    d.v[0] = {l,t,0,1,color,0,0,0}; d.v[1] = {l,b,0,1,color,0,0,1};
    d.v[2] = {r,t,0,1,color,0,1,0}; d.v[3] = {r,b,0,1,color,0,1,1};
    out.push_back(d);
}

// These authored pelvis meshes are replaced by their deformed skin submission.
bool PelvisSkin(uint32_t model) {
    for (uint32_t id : {0xE3Cu,0xE4Du,0xEA2u,0xEB6u,0xEC6u,0xED6u,0xEF6u,0xF06u,0xF83u,0x15B0u})
        if (model == id) return true;
    return false;
}
game::Mat4 CameraRotation(const ModelGallery::CameraState& camera) {
    constexpr float radians=3.14159265f/180.f;
    const float cy=std::cos(camera.yaw*radians),sy=std::sin(camera.yaw*radians);
    const float cp=std::cos(camera.pitch*radians),sp=std::sin(camera.pitch*radians);
    const game::Mat4 ry{{cy,0,sy,0,0,1,0,0,-sy,0,cy,0,0,0,0,1}};
    const game::Mat4 rx{{1,0,0,0,0,cp,-sp,0,0,sp,cp,0,0,0,0,1}};
    return rx*ry;
}

float FloatBits(uint32_t bits) {
    float value; std::memcpy(&value,&bits,sizeof(value)); return value;
}

// Murrer's segmented body uses Anim_DrawSkeleton's second walker. Its child
// angles lag the ordinary walker by half a stored frame; its root does not.
std::vector<game::Mat4> LeechPose(const game::Skeleton& skeleton,const game::Motion& motion,int tick) {
    std::vector<game::Mat4> matrices(skeleton.nodes.size(),game::Mat4::Identity());
    if(skeleton.nodes.empty() || !motion.SizeConsistent()) return matrices;
    const int frame=tick/2;
    for(size_t n=0;n<skeleton.nodes.size();++n) {
        const auto& node=skeleton.nodes[n];
        game::Mat4 matrix=n?matrices[node.parent]:game::Mat4::Identity();
        if(n) game::mx::Translate(matrix,node.offset[0],node.offset[1],node.offset[2]);
        else {
            float root[3]; motion.RootPosition(frame,root);
            if((tick&1) && frame+1<motion.StoredFrames()) {
                float next[3]; motion.RootPosition(frame+1,next);
                for(int k=0;k<3;++k) root[k]+=(next[k]-root[k])*.5f;
            }
            game::mx::Translate(matrix,root[0],root[1],root[2]);
        }
        int16_t stored[3]; motion.Rotation(frame,node.index,stored);
        int angle[3]={stored[0],stored[1],stored[2]};
        if(n && tick>0 && !(tick&1)) {
            int16_t previous[3]; motion.Rotation(frame-1,node.index,previous);
            for(int k=0;k<3;++k) {
                int delta=static_cast<uint16_t>(stored[k]-previous[k]);
                if(delta>0x8000) delta-=0x10000;
                angle[k]=previous[k]+delta/2;
            }
        }
        game::mx::RotZ(matrix,angle[2]); game::mx::RotY(matrix,angle[1]); game::mx::RotX(matrix,angle[0]);
        matrices[n]=matrix;
    }
    return matrices;
}
} // namespace

struct ModelGallery::Impl {
    struct Rig {
        game::Skeleton skeleton;
        game::Motion motion;
        std::array<game::Mat4, 64> pose;
    };
    struct Part {
        game::Model original, posed;
        std::vector<game::Model> frames;
        std::vector<uint32_t> inlineOffsets;
        std::string pol;
        int rig = 0, bone = 0, skinCount = 0;
        int skinBones[4] = {};
        game::SkinDef skin;
        uint32_t skinBase = 0;
        bool twin = false;
        bool envMap = false;
        bool emperorCore = false;
    };
    bool open = false, viewing = false, dirty = true, animationDirty = true, ready = false;
    bool playing = true, stopSounds = false, cueAtStart = true, frameBounds = true;
    bool muted = false;
    bool stopLoops = false;
    bool middleClick = false;
    int category = 0, row = 0, top = 0, selected = -1, animation = 0, tick = 0;
    bool categoryFocus = false, backFocus = false;
    int hovered = -1, drag = 0, wheel = 0;
    int scrollbarDrag = 0, animationTop = 0;
    float scrollbarGrab = 0;
    bool consumePointer = false, animationList = false;
    float mouseX = 0, mouseY = 0;
    bool held[8] = {}; // left/right/up/down/zoom in/out/pitch up/down
    CameraState camera;
    game::MenuPointerFocus pointer;
    std::string error;
    std::vector<uint32_t> sounds, feedbackSounds;
    std::mt19937 soundRandom;
    std::vector<Part> parts;
    std::vector<Rig> rigs;
    std::vector<game::MotionFileEntry> motionFiles;
    port::AssetStore* assets = nullptr;
    game::SoundTable soundTable;
    bool soundTableReady = false;
    std::vector<port::ModelRenderer::SceneItem> scene;
    float center[3] = {}, radius = 1;

    const ModelCatalogEntry* Entry() const {
        return selected >= 0 && selected < static_cast<int>(ModelCatalog().size()) ? &ModelCatalog()[selected] : nullptr;
    }
    const ModelAnimation* Animation() const {
        const auto* entry = Entry();
        return entry && animation >= 0 && animation < static_cast<int>(entry->animations.size()) ? &entry->animations[animation] : nullptr;
    }
    std::vector<int> Entries() const {
        std::vector<int> result;
        for (size_t i = 0; i < ModelCatalog().size(); ++i)
            if (static_cast<int>(ModelCatalog()[i].category) == category) result.push_back(static_cast<int>(i));
        return result;
    }
    void ScrollIntoView() {
        const int count = static_cast<int>(Entries().size());
        row = std::clamp(row, 0, std::max(0,count-1));
        if (row < top) top = row;
        if (row >= top+kVisible) top = row-kVisible+1;
        top = std::clamp(top,0,std::max(0,count-kVisible));
    }
    void Category(int value) { category = Wrap(value,4); row = top = 0; backFocus = false; }
    ListBar ModelBar() const { return {602,116,616,411,static_cast<int>(Entries().size()),top}; }
    ListBar AnimationBar() const { return {302,113,316,338,Entry()?static_cast<int>(Entry()->animations.size()):0,animationTop}; }
    void ScrollList(int list,int first) {
        if(list==1) {
            top=std::clamp(first,0,ModelBar().Max());
            row=std::clamp(row,top,std::min(top+kVisible-1,static_cast<int>(Entries().size())-1));
            categoryFocus=backFocus=false;
        } else animationTop=std::clamp(first,0,AnimationBar().Max());
    }
    void ShowAnimation() {
        if(animation<animationTop) animationTop=animation;
        if(animation>=animationTop+kVisible) animationTop=animation-kVisible+1;
        animationTop=std::clamp(animationTop,0,AnimationBar().Max());
    }
    void Stop() { sounds.clear(); stopSounds = true; stopLoops=false; }
    void Back() {
        Stop(); drag = scrollbarDrag = 0; middleClick=animationList=false; std::fill(std::begin(held),std::end(held),false);
        feedbackSounds.push_back(game::kOptionBackSound);
        if (viewing) { viewing = false; playing = false; ready = false; dirty = true; }
        else open = false;
        pointer = {}; hovered = -1;
    }
    int ControlAt(float x, float y) const {
        if (Inside(x,y,536,24,616,56)) return 0;
        if (Inside(x,y,24,358,60,390)) return 1;
        if (Inside(x,y,280,358,316,390)) return 2;
        if (Inside(x,y,24,401,104,434)) return 3;
        if (Inside(x,y,112,401,192,434)) return 4;
        if (Inside(x,y,200,401,280,434)) return 5;
        if (Inside(x,y,68,358,280,390)) return 6;
        if (Inside(x,y,432,24,528,56)) return 7;
        return -1;
    }
    void Cue() {
        const auto* a = Animation();
        if (!a || muted) return;
        for (const auto& c : a->sounds) if (c.tick == tick) {
            if(c.variants.empty()) sounds.push_back(c.soundId);
            else sounds.push_back(c.variants[std::uniform_int_distribution<size_t>(0,c.variants.size()-1)(soundRandom)]);
        }
    }
    void ResumeLoops() {
        if(!open || !viewing || muted || !ready || cueAtStart || !assets || !Animation()) return;
        if(!soundTableReady) soundTableReady=soundTable.Load(assets->Exe());
        if(!soundTableReady) return;
        int stoppedAt=-1;
        for(const auto& cue:Animation()->sounds)
            if(cue.tick<=tick && game::ResolveSound(soundTable,cue.soundId).stopLoopedSe) stoppedAt=std::max(stoppedAt,cue.tick);
        for(const auto& cue:Animation()->sounds)
            if(cue.tick<=tick && cue.tick>stoppedAt && game::ResolveSound(soundTable,cue.soundId).loop &&
               std::find(sounds.begin(),sounds.end(),cue.soundId)==sounds.end()) sounds.push_back(cue.soundId);
    }
    bool LoadMotion(port::AssetStore& assets, int id, Rig& rig) {
        rig.motion = {};
        for (const auto& file : motionFiles) {
            if (std::find(file.motionIds.begin(),file.motionIds.end(),id) == file.motionIds.end()) continue;
            for (auto& m : game::SplitMotionFile(assets.Exe(),file,assets.File("mot/"+file.name),rig.skeleton.nodeCount))
                if (m.id == id) { rig.motion = std::move(m); break; }
            break;
        }
        if (rig.motion.id != id || rig.motion.ticks <= 0 || !rig.motion.SizeConsistent()) {
            error = "This animation could not be loaded.";
            return false;
        }
        return true;
    }
    bool AddPart(port::AssetStore& assets, int rigIndex, int bone, uint32_t model,
                 uint32_t skinDef = 0, int count = 0, const int* skinBones = nullptr, bool twin = false, int frameCount = 1) {
        if (!model || bone < 0 || bone >= 64) return false;
        Part p;
        p.rig = rigIndex; p.bone = bone; p.twin = twin;
        p.envMap=rigs[rigIndex].skeleton.type==0x4C;
        if (!assets.ModelById(model,p.original,&p.pol)) return false;
        for(const auto& mesh:p.original.meshes) for(const auto& strip:mesh.strips)
            p.inlineOffsets.insert(p.inlineOffsets.end(),strip.offsets.begin(),strip.offsets.end());
        std::sort(p.inlineOffsets.begin(),p.inlineOffsets.end());
        p.inlineOffsets.erase(std::unique(p.inlineOffsets.begin(),p.inlineOffsets.end()),p.inlineOffsets.end());
        if (skinDef && game::LoadSkinDef(assets.Exe(),skinDef,p.skin)) {
            p.skinCount = count;
            std::copy(skinBones,skinBones+count,p.skinBones);
            p.skinBase = twin ? p.original.blockStart : game::SkinOffsetBase(p.skin,p.original);
            if (p.skinBase == UINT32_MAX) p.skinBase = p.original.blockStart;
        }
        p.posed = p.original;
        if(frameCount>1) {
            p.frames.reserve(frameCount);
            p.frames.push_back(p.original);
            for(int i=1;i<frameCount;++i) {
                game::Model frame;
                if(!assets.ModelById(model+i,frame,nullptr)) break;
                p.frames.push_back(std::move(frame));
            }
        }
        parts.push_back(std::move(p));
        return true;
    }
    void LoadParts(port::AssetStore& assets, int rigIndex, const ModelCatalogEntry* entry) {
        const auto& exe = assets.Exe();
        const auto& skel = rigs[rigIndex].skeleton;
        auto partDef = [&](int part) { return exe.U32(0x004EC748 + 4*part); };
        for (const auto& n : skel.nodes) {
            uint32_t model = n.modelId;
            int frameCount = 1;
            if (entry) {
                if (n.index == 2 && entry->headPart >= 0) model = exe.U32(partDef(entry->headPart)+4);
                for (const auto& change : entry->nodeOverrides) if (change.node == n.index) {
                    model = change.modelId; frameCount = change.frameCount;
                }
            }
            if (n.index == 9 && PelvisSkin(model)) continue;
            if(AddPart(assets,rigIndex,n.index,model,0,0,nullptr,false,frameCount) && skel.type==0x1D && n.index==7) {
                // BatSkelDraw state 6 replaces the jaw-bearing end segment
                // with the matching severed mesh for this appearance.
                game::Model shot;
                if(assets.ModelById(model==0x136F?0x1370:0x1371,shot,nullptr)) {
                    parts.back().frames={parts.back().original,std::move(shot)};
                }
            }
        }
        const int kind = skel.type;
        const uint32_t at = exe.U32(0x0052ED08 + 4*kind);
        if (exe.InData(at)) {
            const int count = static_cast<int>(exe.U32(at));
            const uint32_t defs = exe.U32(at+4);
            uint32_t table = 0x004ED1E0;
            int nodes = 4;
            switch (kind) {
            case 0xE: table=0x004ED348; break;
            case 0x1F: case 0x46: case 0x4E: table=0x004ED4B0; nodes=2; break;
            case 0x41: table=0x004ED780; nodes=2; break;
            case 0x4C: table=0x004ED618; break;
            case 0x53: table=0x004ED8E8; nodes=2; break;
            default: break;
            }
            for (int i=0; i<count && i<32 && exe.InData(defs); ++i) {
                const uint32_t def = exe.U32(defs+4*i);
                if (!exe.InData(def)) continue;
                if (kind==0x17 && i>0) {
                    const uint32_t segs=exe.U32(def+0x10);
                    const int n=static_cast<int>(exe.U32(def+0x14));
                    for(int k=0; k<n && k<16 && exe.InData(segs); ++k)
                        AddPart(assets,rigIndex,exe.U32(segs+0x10*k)&31,exe.U32(segs+0x10*k+4));
                    continue;
                }
                const uint32_t row = table+0x14*i;
                const int bone = exe.U32(row+0x10)&15;
                int skinBones[4]{};
                for(int s=0;s<nodes;++s) skinBones[s]=exe.U32(row+(nodes==2?8*s:4*s))&15;
                if (kind==0x4C && i<6) {
                    AddPart(assets,rigIndex,bone,exe.S16(0x004EDA50+4*i),def,nodes,skinBones);
                    AddPart(assets,rigIndex,bone,exe.S16(0x004EDA52+4*i),def,nodes,skinBones,true);
                } else AddPart(assets,rigIndex,bone,exe.U32(def),def,nodes,skinBones);
            }
        }
        if (entry) {
            for(int part : {entry->part1,entry->part2}) if(part>=0) {
                const uint32_t def=partDef(part);
                AddPart(assets,rigIndex,exe.U32(def)&31,exe.U32(def+4));
            }
            for(const auto& mesh:entry->extraMeshes)
                AddPart(assets,rigIndex,mesh.node,mesh.modelId,0,0,nullptr,false,mesh.frameCount);
            if(kind==0x4C && AddPart(assets,rigIndex,1,0x791)) {
                // Actor2DNode's always-visible rotating core has its own local
                // transform and ordinary UVs, unlike the reflective shell.
                parts.back().emperorCore=true; parts.back().envMap=false;
            }
        }
    }
    void LoadAssembly(port::AssetStore& assets,const ModelCatalogEntry& entry) {
        if(entry.assembly==ModelAssembly::AmbientInsect) {
            AddPart(assets,0,0,0x1CF,0,0,nullptr,false,2);
            return;
        }
        if(entry.assembly==ModelAssembly::Fish) {
            // Actor 0x51 (DrawFish) swaps complete mesh frames, including
            // distinct shot forms above and below the water.
            AddPart(assets,0,0,0x1156,0,0,nullptr,false,20);
            AddPart(assets,0,1,0xB6F); AddPart(assets,0,2,0xB70);
            return;
        }
        // Actor 0x43 is assembled by BatDraw (0x00447C20), independently of
        // the game's skeleton table. Keep its authored body, face and feet
        // mesh sequences together with the articulated wings and legs.
        AddPart(assets,0,0,0xBBF,0,0,nullptr,false,2);
        AddPart(assets,0,1,0xBC1,0,0,nullptr,false,30);
        AddPart(assets,0,2,0xBF8,0,0,nullptr,false,15);
        AddPart(assets,0,3,0xBF1);
        AddPart(assets,0,4,0xBF4);
        AddPart(assets,0,5,0xBF2);
        for(int side=0;side<2;++side) {
            AddPart(assets,0,6+side*3,side?0xBF5:0xBF6);
            AddPart(assets,0,7+side*3,side?0xBBD:0xBBE);
            AddPart(assets,0,8+side*3,side?0xC18:0xC08,0,0,nullptr,false,15);
        }
    }
    void PoseAssembly() {
        auto& pose=rigs[0].pose;
        std::fill(pose.begin(),pose.end(),game::Mat4::Identity());
        if(Entry()->assembly==ModelAssembly::AmbientInsect) return;
        if(Entry()->assembly==ModelAssembly::Fish) {
            for(int bone=0;bone<3;++bone) game::mx::Scale(pose[bone],.3f,.3f,.3f);
            return;
        }
        auto body=game::Mat4::Identity();
        game::mx::RotZ(body,0x4007); game::mx::RotY(body,0xDEC8); game::mx::RotX(body,0x3FF9);
        pose[0]=pose[1]=body;
        auto head=body;
        game::mx::Translate(head,FloatBits(0xBF13FE5D),FloatBits(0xBED74BC7),FloatBits(0x3B6BEDFA));
        game::mx::RotZ(head,0x8863); game::mx::RotY(head,0xFFC0); game::mx::RotX(head,0xFFF2);
        pose[2]=head;
        auto wing=body;
        game::mx::Translate(wing,FloatBits(0x3FE89D49),0,0);
        pose[3]=wing;
        for(int side=0;side<2;++side) {
            auto tip=wing;
            game::mx::Translate(tip,FloatBits(side?0x3DBDD97F:0x3DB7B4A2),
                FloatBits(side?0x3EDF8A09:0x3EDF212D),FloatBits(0xBC449BA6));
            game::mx::RotY(tip,0xC041); game::mx::RotX(tip,side?0x4EE0:0xD494);
            pose[4+side]=tip;
            auto leg=game::Mat4::Identity();
            game::mx::Translate(leg,FloatBits(side?0xBEAA0903:0x3EAA0903),FloatBits(0xBDAB367A),FloatBits(0xBBD4FDF4));
            game::mx::RotZ(leg,0xC000); game::mx::RotY(leg,0x3C0); game::mx::RotX(leg,0xC000);
            pose[6+side*3]=leg;
            game::mx::Translate(leg,FloatBits(0x3F2A4A8C),0,0); game::mx::RotZ(leg,0xDB13);
            pose[7+side*3]=leg;
            game::mx::Translate(leg,FloatBits(0x3F2E5604),0,0);
            pose[8+side*3]=leg;
        }
    }
    void DeformLeech(Part& part,const std::array<game::Mat4,64>& pose) {
        if(part.bone<1 || part.bone>7 || !this->assets) return;
        const auto& exe=this->assets->Exe();
        const uint32_t record=0x004E1D48+part.bone*0x40;
        auto s8=[&](uint32_t at) { int8_t value=-1; exe.Read(at,&value,1); return value; };
        float points[128][3]={};
        bool available[128]={};
        auto transform=[&](uint32_t data,int reference,uint32_t list) {
            if(reference<0 || reference>=10 || !exe.InData(data)) return;
            auto matrix=game::mx::Inverse(pose[part.bone]);
            game::mx::MulLeft(matrix,pose[reference]);
            for(int n=0;n<64;++n) {
                const int index=s8(list+n);
                if(index==-1) break;
                if(index<0) continue;
                float point[3]; if(!exe.Read(data+index*12,point,12)) continue;
                game::mx::Transform(matrix,point,points[index]);
                available[index]=true;
            }
        };
        transform(exe.U32(record),s8(record+4),record+0x29);
        if(part.bone==7) transform(exe.U32(0x004E1F08),s8(0x004E1F4C),0x004E1F39);
        part.posed=part.original;
        int moved=0;
        for(size_t index=0;index<part.inlineOffsets.size() && index<64;++index) {
            if(s8(record+6+static_cast<uint32_t>(index))!=1) continue;
            const int point=s8(record+0x29+moved++);
            if(point<0 || !available[point]) continue;
            for(auto& mesh:part.posed.meshes) for(auto& strip:mesh.strips)
                for(size_t v=0;v<strip.vertices.size() && v<strip.offsets.size();++v)
                    if(strip.offsets[v]==part.inlineOffsets[index]) std::copy(points[point],points[point]+3,strip.vertices[v].pos);
        }
    }
    bool Load(port::AssetStore& assets) {
        ready = false; error.clear();
        const auto* entry = Entry();
        if (!entry || !Animation()) { error="No animations are available for this model."; return false; }
        if (motionFiles.empty()) motionFiles=game::ListMotionFiles(assets.Exe());
        if (dirty) {
            scene.clear(); parts.clear(); rigs.clear();
            rigs.resize(entry->companionSkeletonType>=0?2:1);
            if(entry->assembly!=ModelAssembly::Skeleton) LoadAssembly(assets,*entry);
            else {
                if (!game::LoadSkeleton(assets.Exe(),entry->skeletonType,rigs[0].skeleton) ||
                    (rigs.size()>1 && !game::LoadSkeleton(assets.Exe(),entry->companionSkeletonType,rigs[1].skeleton))) {
                    error="This model could not be loaded."; return false;
                }
                LoadParts(assets,0,entry);
                if(rigs.size()>1) LoadParts(assets,1,nullptr);
            }
            if(parts.empty()) { error="No model geometry was found."; return false; }
        }
        if(entry->assembly!=ModelAssembly::Skeleton) {
            rigs[0].motion={}; rigs[0].motion.ticks=std::max(1,Animation()->tickLength);
        } else {
            if (!LoadMotion(assets,Animation()->motionId,rigs[0])) return false;
            if (rigs.size()>1 && !LoadMotion(assets,Animation()->companionMotionId,rigs[1])) return false;
        }
        dirty = animationDirty = false; ready = true;
        return true;
    }
    void Pose() {
        const auto* entry=Entry();
        const bool assembly=entry->assembly!=ModelAssembly::Skeleton;
        if(assembly) PoseAssembly();
        for(size_t r=0;r<rigs.size();++r) {
            if(assembly) break;
            auto& rig=rigs[r];
            std::fill(rig.pose.begin(),rig.pose.end(),game::Mat4::Identity());
            const int motionTick=std::min(tick,rig.motion.ticks-1);
            auto matrices=rig.skeleton.type==0x1D?LeechPose(rig.skeleton,rig.motion,motionTick):
                game::EvaluatePose(rig.skeleton,rig.motion,motionTick);
            // Root travel belongs to the stage, not the viewer. Remove it on
            // all three axes, including jumps and falls authored far above or
            // below the origin; the articulated pose itself remains intact.
            const float root[3]={matrices[0].m[12],matrices[0].m[13],matrices[0].m[14]};
            for(size_t n=0;n<matrices.size();++n) {
                for(int axis=0;axis<3;++axis) matrices[n].m[12+axis]-=root[axis];
                if(entry->skeletonType==0x1E) {
                    if(!r) {
                        auto scale=game::Mat4::Identity(); game::mx::Scale(scale,.6f,.6f,.6f);
                        matrices[n]=matrices[n]*scale;
                    } else {
                        auto anchor=rigs[0].pose[entry->companionNode&63]; game::mx::Translate(anchor,0,1,2);
                        auto object=game::Mat4::Translate(anchor.m[12],anchor.m[13],anchor.m[14]);
                        game::mx::RotY(object,0x8000); game::mx::RotX(object,0xE800); game::mx::Scale(object,.7f,.7f,.7f);
                        matrices[n]=matrices[n]*object;
                    }
                } else if(r>0) matrices[n]=matrices[n]*rigs[0].pose[Entry()->companionNode&63];
                const int bone=rig.skeleton.nodes[n].index;
                if(bone>=0 && bone<64) rig.pose[bone]=matrices[n];
            }
        }
        scene.clear(); scene.reserve(parts.size());
        for(auto& part:parts) {
            const bool owl=entry->assembly==ModelAssembly::Owl,dead=owl && animation>0;
            const bool leech=rigs[part.rig].skeleton.type==0x1D;
            const bool shotLeech=leech && Animation()->motionId==0x219;
            if(shotLeech && part.bone>=8) continue;
            if(entry->assembly==ModelAssembly::Fish && part.bone!=animation) continue;
            if(dead && part.bone>=3 && part.bone<=5) continue;
            const auto& pose=rigs[part.rig].pose;
            port::ModelRenderer::SceneItem item;
            item.model=&part.original; item.polName=part.pol; item.noMeshCull=true; item.cullable=false;
            // Native game rendering includes DeviceView's Z reflection. Our
            // positive-Z orbit camera has the opposite handedness, so retain
            // the authored visible faces by reversing their cull winding.
            item.cullFlip=true;
            if(entry->assembly==ModelAssembly::Fish) item.unitSpecular=true;
            if(!part.frames.empty()) item.model=&part.frames[static_cast<size_t>(tick)%part.frames.size()];
            if(owl && !part.frames.empty()) {
                int frame=tick%static_cast<int>(part.frames.size());
                if(part.bone==0) frame=dead?1:0;
                if(part.bone==1 && dead) frame=18;
                if(part.bone==2) { frame=tick%30; if(frame>14) frame=30-frame; frame=dead?0:std::min(frame,14); }
                item.model=&part.frames[frame];
            }
            std::copy(pose[part.bone].m,pose[part.bone].m+16,item.transform);
            if(leech) {
                item.unitSpecular=true;
                if(part.bone==7 && shotLeech && part.frames.size()>1) item.model=&part.frames[1];
                else {
                    DeformLeech(part,pose);
                    if(part.bone>=1 && part.bone<=7) { item.model=&part.posed; item.dynamicGeometry=true; }
                }
            }
            if(part.emperorCore) {
                const uint32_t xb=0x4013F972,yb=0x3DE0AA65;
                float x,y; std::memcpy(&x,&xb,4); std::memcpy(&y,&yb,4);
                const auto core=game::Mat4::RotateZ((tick*65536/60)&0xFFFF)*game::Mat4::RotateX(0x4000)*
                    game::Mat4::Translate(x,y,0)*pose[part.bone];
                std::copy(core.m,core.m+16,item.transform);
            }
            if(part.skinCount) {
                part.posed=part.original;
                game::Mat4 nodes[4];
                for(int k=0;k<part.skinCount;++k) nodes[k]=pose[part.skinBones[k]];
                game::SkinDeform(part.skin,part.skinBase,pose[part.bone],nodes,part.skinCount,part.posed,part.twin);
                item.model=&part.posed; item.dynamicGeometry=true;
            }
            if(part.envMap) {
                // The Emperor's surface is authored as a reflection map. Its
                // strip UVs are rebuilt from deformed normals (Actor2DNode /
                // AttachDraw4C), otherwise the two shell surfaces look torn.
                if(!part.skinCount) part.posed=*item.model;
                const auto modelView=pose[part.bone]*CameraRotation(camera);
                const float* m=modelView.m;
                std::vector<uint32_t> offsets;
                for(const auto& mesh:part.posed.meshes) for(const auto& strip:mesh.strips)
                    if(strip.ctrl&0x100) offsets.insert(offsets.end(),strip.offsets.begin(),strip.offsets.end());
                std::sort(offsets.begin(),offsets.end());
                for(auto& mesh:part.posed.meshes) for(auto& strip:mesh.strips)
                    for(size_t v=0;v<strip.vertices.size() && v<strip.offsets.size();++v) {
                        if(!std::binary_search(offsets.begin(),offsets.end(),strip.offsets[v])) continue;
                        auto& vertex=strip.vertices[v]; const float* n=vertex.normal;
                        vertex.uv[0]=(1.f-((m[0]*n[0]+m[8]*n[2])+m[4]*n[1]))*.5f;
                        vertex.uv[1]=(((m[1]*n[0]+m[9]*n[2])+m[5]*n[1])+1.f)*.5f;
                    }
                item.model=&part.posed; item.dynamicGeometry=true; item.alpha=1.f;
            }
            scene.push_back(item);
        }
        // Animated meshes, severed forms and motion-specific poses can have
        // very different bounds. Follow the visible geometry every frame so
        // each clip shares the same screen anchor, rather than inheriting the
        // first clip's off-center framing. Keep a stable scale within a clip,
        // expanding its frame only when a later pose needs more room.
        {
            float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
            for(const auto& item:scene) for(const auto& mesh:item.model->meshes) for(const auto& strip:mesh.strips)
                for(const auto& vertex:strip.vertices) for(int axis=0;axis<3;++axis) {
                    const float p=vertex.pos[0]*item.transform[axis]+vertex.pos[1]*item.transform[4+axis]+
                        vertex.pos[2]*item.transform[8+axis]+item.transform[12+axis];
                    low[axis]=std::min(low[axis],p); high[axis]=std::max(high[axis],p);
                }
            float sum=0;
            for(int axis=0;axis<3;++axis) { center[axis]=(high[axis]+low[axis])*.5f; sum+=(high[axis]-low[axis])*(high[axis]-low[axis]); }
            const float posedRadius=std::max(.01f,std::sqrt(sum)*.5f);
            radius=frameBounds?posedRadius:std::max(radius,posedRadius);
            frameBounds=false;
        }
    }
};

ModelGallery::ModelGallery():ModelGallery(std::random_device{}()) {}
ModelGallery::ModelGallery(uint32_t soundSeed):impl_(std::make_unique<Impl>()) { impl_->soundRandom.seed(soundSeed); }
ModelGallery::~ModelGallery()=default;
void ModelGallery::Open() {
    auto& p=*impl_; p.open=true; p.viewing=false; p.dirty=true; p.ready=false; p.error.clear();
    p.pointer={}; p.hovered=-1; p.drag=p.scrollbarDrag=0; p.wheel=0; p.backFocus=false; p.middleClick=false;
    p.animationList=p.consumePointer=false;
    std::fill(std::begin(p.held),std::end(p.held),false); p.Stop(); p.feedbackSounds.clear();
}
void ModelGallery::Close() { impl_->Stop(); impl_->open=false; impl_->viewing=false; impl_->drag=impl_->scrollbarDrag=0; impl_->animationList=false; }
bool ModelGallery::IsOpen() const { return impl_->open; }
bool ModelGallery::IsViewing() const { return impl_->open && impl_->viewing; }
game::MenuScrollHint ModelGallery::ScrollHint() const {
    game::MenuScrollHint hint{216,116,616,411};
    const auto& p=*impl_;
    if(p.open&&!p.viewing) {
        hint.above=p.top>0;
        hint.below=p.top+kVisible<static_cast<int>(p.Entries().size());
    }
    return hint;
}
bool ModelGallery::SelectModel(int index) {
    if(index<0 || index>=static_cast<int>(ModelCatalog().size())) return false;
    auto& p=*impl_; p.Stop(); p.open=p.viewing=true; p.selected=index; p.animation=p.tick=0;
    p.category=static_cast<int>(ModelCatalog()[index].category);
    const auto rows=p.Entries(); p.row=static_cast<int>(std::find(rows.begin(),rows.end(),index)-rows.begin()); p.ScrollIntoView();
    p.camera={}; p.dirty=p.animationDirty=p.frameBounds=p.cueAtStart=true; p.ready=false; p.playing=true;
    p.animationTop=0; p.animationList=false; p.scrollbarDrag=0;
    p.pointer={}; p.hovered=-1; p.error.clear(); return true;
}
bool ModelGallery::SelectAnimation(int index) {
    auto& p=*impl_; const auto* entry=p.Entry();
    if(!entry || index<0 || index>=static_cast<int>(entry->animations.size())) return false;
    p.Stop(); p.animation=index; p.tick=0; p.animationDirty=p.frameBounds=p.cueAtStart=true; p.ready=false; p.playing=true;
    p.camera.height=0;
    p.ShowAnimation(); return true;
}
void ModelGallery::HandleEvent(const platform::Event& ev,float x,float y) {
    auto& p=*impl_; if(!p.open) return;
    using T=platform::Event::Type; using K=platform::Key;
    if(ev.type==T::Focus && !ev.active) { std::fill(std::begin(p.held),std::end(p.held),false); p.drag=p.scrollbarDrag=0; }
    if(ev.type==T::KeyDown || ev.type==T::KeyUp) {
        const bool down=ev.type==T::KeyDown;
        int key=-1;
        if(ev.key==K::Left || ev.ch=='a') key=0;
        if(ev.key==K::Right || ev.ch=='d') key=1;
        if(ev.key==K::Up || ev.ch=='w') key=2;
        if(ev.key==K::Down || ev.ch=='s') key=3;
        if(ev.ch=='e' || ev.ch=='+' || ev.ch=='=') key=4;
        if(ev.ch=='q' || ev.ch=='-') key=5;
        if(ev.ch=='i') key=6;
        if(ev.ch=='k') key=7;
        if(key>=0) p.held[key]=down;
        if(down && !ev.repeat && p.viewing) {
            if(ev.ch=='r') p.camera={};
            if(ev.ch=='m') SetMuted(!p.muted);
            if(ev.key==K::PageUp || ev.ch=='[') SelectAnimation(Wrap(p.animation-1,static_cast<int>(p.Entry()->animations.size())));
            if(ev.key==K::PageDown || ev.ch==']') SelectAnimation(Wrap(p.animation+1,static_cast<int>(p.Entry()->animations.size())));
        }
    }
    // The mouse gun also emits a trigger edge; consume it so a scrollbar drag
    // never opens a model or changes playback. Capture survives leaving the list.
    if(ev.type==T::MouseDown && ev.button==1) {
        const int list=!p.viewing?1:(p.animationList?2:0);
        const auto bar=list==1?p.ModelBar():p.AnimationBar();
        if(list && bar.Hit(x,y)) {
            p.consumePointer=true; p.drag=0;
            if(bar.Max()) {
                if(y>=bar.Thumb() && y<bar.Thumb()+bar.Height()) {
                    p.scrollbarDrag=list; p.scrollbarGrab=y-bar.Thumb();
                } else {
                    p.ScrollList(list,bar.first+(y<bar.Thumb()?-kVisible:kVisible));
                    p.scrollbarDrag=-list;
                }
            }
            return;
        }
    }
    if(ev.type==T::MouseMove && p.scrollbarDrag) {
        if(p.scrollbarDrag>0) {
            const auto bar=p.scrollbarDrag==1?p.ModelBar():p.AnimationBar();
            p.ScrollList(p.scrollbarDrag,bar.At(y,p.scrollbarGrab));
        }
        return;
    }
    if(ev.type==T::MouseUp && ev.button==1 && p.scrollbarDrag) {
        p.scrollbarDrag=0; p.consumePointer=true; return;
    }
    if(ev.type==T::MouseDown && p.viewing && ev.button==2) p.middleClick=true;
    const bool overAnimations=p.animationList && Inside(x,y,24,113,316,338);
    if(ev.type==T::MouseDown && p.viewing && (ev.button==1 || ev.button==2) && p.ControlAt(x,y)<0 && !overAnimations) { p.drag=ev.button; p.mouseX=x; p.mouseY=y; }
    if(ev.type==T::MouseUp && ev.button==p.drag) p.drag=0;
    if(ev.type==T::MouseMove && p.drag && p.viewing) {
        const float dx=x-p.mouseX,dy=y-p.mouseY;
        if(p.drag==1) { p.camera.yaw+=dx*.6f; p.camera.pitch=std::clamp(p.camera.pitch+dy*.4f,-80.f,80.f); }
        if(p.drag==2) p.camera.height=std::clamp(p.camera.height+dy*.006f,-2.f,2.f);
        p.mouseX=x; p.mouseY=y;
    }
    if(ev.type==T::MouseWheel && ev.wheel!=0) {
        if(p.viewing && !(overAnimations || (p.animationList && p.ControlAt(x,y)==6))) p.camera.zoom=std::clamp(p.camera.zoom*std::pow(.88f,ev.wheel),.5f,10.f);
        else p.wheel+=ev.wheel>0?1:-1;
    }
}
void ModelGallery::Update(const MenuInput& in) {
    auto& p=*impl_; if(!p.open) return;
    const bool pointerConsumed=std::exchange(p.consumePointer,false) || p.scrollbarDrag!=0;
    if(p.animationList && ((in.pressed&2) || in.reload || in.start)) { p.animationList=false; p.scrollbarDrag=0; return; }
    if((in.pressed&2) || in.reload || in.start) { p.Back(); return; }
    if(p.scrollbarDrag && !p.viewing) return;
    // The gun adapter maps the middle mouse button to Start/Enter. A camera
    // drag must consume that edge without changing animation playback.
    const bool mouseStart=std::exchange(p.middleClick,false);
    const bool confirm=!pointerConsumed && (in.pressed&(mouseStart?4:12))!=0;
    if(!p.viewing) {
        const bool pointed=!pointerConsumed && p.pointer.Update(in.pointer,in.px,in.py,in.trigger);
        if(pointed) {
            p.backFocus=Inside(in.px,in.py,24,423,116,455);
            for(int c=0;c<4;++c) if(Inside(in.px,in.py,24,116+c*48,184,154+c*48)) {
                if(p.category!=c) p.Category(c); p.categoryFocus=true;
            }
            const auto rows=p.Entries();
            for(int r=0;r<kVisible && p.top+r<static_cast<int>(rows.size());++r)
                if(Inside(in.px,in.py,216,116+r*33,598,147+r*33)) { p.row=p.top+r; p.categoryFocus=false; }
        }
        if(in.pressed&0x40) { p.Category(p.category-1); p.categoryFocus=false; }
        if(in.pressed&0x80) { p.Category(p.category+1); p.categoryFocus=false; }
        const int vertical=(in.pressed&0x20?1:0)-(in.pressed&0x10?1:0);
        if(vertical) {
            p.backFocus=false;
            if(p.categoryFocus) p.Category(p.category+vertical);
            else p.row=Wrap(p.row+vertical,static_cast<int>(p.Entries().size()));
        }
        if(in.pageDelta) { p.row+=in.pageDelta*kVisible; p.backFocus=false; }
        const int scroll=in.scroll+p.wheel; p.wheel=0;
        if(scroll) { p.row-=scroll; p.backFocus=false; }
        p.ScrollIntoView();
        if(confirm || (in.trigger && !pointerConsumed)) {
            if(p.backFocus) { p.Back(); return; }
            const auto rows=p.Entries();
            if(in.trigger) {
                bool hit=false;
                for(int r=0;r<kVisible && p.top+r<static_cast<int>(rows.size());++r)
                    hit=hit || Inside(in.px,in.py,216,116+r*33,598,147+r*33);
                if(!hit) return;
            }
            if(!rows.empty() && SelectModel(rows[p.row])) p.feedbackSounds.push_back(game::kOptionSelectSound);
        }
        return;
    }
    p.hovered=in.pointer?p.ControlAt(in.px,in.py):-1;
    if(p.animationList) {
        const int scroll=in.scroll+std::exchange(p.wheel,0);
        if(scroll) p.ScrollList(2,p.animationTop-scroll);
        if(in.trigger && !pointerConsumed) {
            if(Inside(in.px,in.py,24,113,298,338)) {
                const int row=static_cast<int>((in.py-113)/25)+p.animationTop;
                if(row<static_cast<int>(p.Entry()->animations.size())) {
                    SelectAnimation(row); p.animationList=false;
                    p.feedbackSounds.push_back(game::kOptionSelectSound);
                }
                return;
            }
            if(p.AnimationBar().Hit(in.px,in.py)) return;
            p.animationList=false;
            if(p.hovered==6) return;
        }
    }
    if(in.trigger && !pointerConsumed) {
        switch(p.hovered) {
        case 0: p.Back(); return;
        case 1: SelectAnimation(Wrap(p.animation-1,static_cast<int>(p.Entry()->animations.size()))); break;
        case 2: SelectAnimation(Wrap(p.animation+1,static_cast<int>(p.Entry()->animations.size()))); break;
        case 3: if(!p.playing) { p.playing=true; p.ResumeLoops(); } break;
        case 4: p.playing=false; p.Stop(); break;
        case 5: p.Stop(); p.tick=0; p.cueAtStart=p.frameBounds=true; p.camera.height=0; break;
        case 6: p.animationList=true; p.ShowAnimation(); break;
        case 7: SetMuted(!p.muted); break;
        default: break;
        }
    }
    if(confirm) { p.playing=!p.playing; if(!p.playing) p.Stop(); else p.ResumeLoops(); }
    const float yaw=((p.held[1]?1.f:0.f)-(p.held[0]?1.f:0.f));
    const float vertical=((p.held[2]?1.f:0.f)-(p.held[3]?1.f:0.f));
    p.camera.yaw+=yaw*1.6f;
    p.camera.height=std::clamp(p.camera.height+vertical*.018f,-2.f,2.f);
    if(!p.held[0] && !p.held[1]) p.camera.yaw+=(in.pressed&0x80?8.f:0.f)-(in.pressed&0x40?8.f:0.f);
    if(!p.held[2] && !p.held[3]) p.camera.height=std::clamp(p.camera.height+(in.pressed&0x10?.08f:0.f)-(in.pressed&0x20?.08f:0.f),-2.f,2.f);
    p.camera.zoom=std::clamp(p.camera.zoom*std::pow(.98f,(p.held[4]?1.f:0.f)-(p.held[5]?1.f:0.f)),.5f,10.f);
    p.camera.pitch=std::clamp(p.camera.pitch+(p.held[6]?1.f:0.f)-(p.held[7]?1.f:0.f),-80.f,80.f);
    if(p.ready && p.playing) {
        if(p.cueAtStart) { p.Cue(); p.cueAtStart=false; }
        // A single-frame shot/resting pose is a held image, not a 60 Hz sound
        // loop. Its associated cue plays once on entry or Reset.
        else if(p.rigs[0].motion.ticks>1) {
            p.tick=(p.tick+1)%p.rigs[0].motion.ticks;
            if(!p.tick) p.stopLoops=true;
            p.Cue();
        }
    }
}
std::vector<uint32_t> ModelGallery::TakeSounds() { std::vector<uint32_t> result; result.swap(impl_->sounds); return result; }
std::vector<uint32_t> ModelGallery::TakeFeedbackSounds() { std::vector<uint32_t> result; result.swap(impl_->feedbackSounds); return result; }
bool ModelGallery::TakeStopSounds() { return std::exchange(impl_->stopSounds,false); }
bool ModelGallery::TakeStopLoops() { return std::exchange(impl_->stopLoops,false); }
bool ModelGallery::Prepare(port::AssetStore& assets,port::ModelRenderer& renderer) {
    auto& p=*impl_;
    p.assets=&assets;
    if(p.dirty) renderer.ClearCache();
    if(!p.open || !p.viewing) { renderer.SetScene(assets,std::vector<port::ModelRenderer::SceneItem>{}); p.dirty=false; return true; }
    if((p.dirty || p.animationDirty) && !p.Load(assets)) {
        p.dirty=p.animationDirty=false; renderer.SetScene(assets,std::vector<port::ModelRenderer::SceneItem>{}); return false;
    }
    if(!p.ready) return false;
    p.Pose(); renderer.SetScene(assets,p.scene); return true;
}
std::vector<game::WorldHud::MenuText> ModelGallery::Text() const {
    const auto& p=*impl_;
    std::vector<game::WorldHud::MenuText> text;
    auto add=[&](float x,float y,std::string s,float size=.6f,uint32_t color=kWhite) { text.push_back({x,y,std::move(s),size,color}); };
    if(!p.viewing) {
        add(24,25,"MODEL VIEWER",1.4f); add(24,76,"CHOOSE A CATEGORY AND A MODEL",.54f,kMuted);
        for(int c=0;c<4;++c) add(36,123+c*48,ModelCategoryName(static_cast<ModelCategory>(c)),.7f,c==p.category?kWhite:kMuted);
        const auto rows=p.Entries();
        for(int r=0;r<kVisible && p.top+r<static_cast<int>(rows.size());++r)
            add(228,121+r*33,ModelCatalog()[rows[p.top+r]].name,.64f,p.top+r==p.row?kWhite:kMuted);
        add(24,431,"BACK",.6f); add(216,431,"ARROWS  BROWSE   ENTER / SHOOT  VIEW",.46f,kMuted);
        add(216,453,std::to_string(std::min(p.top+1,static_cast<int>(rows.size())))+"-"+
            std::to_string(std::min(p.top+kVisible,static_cast<int>(rows.size())))+" / "+std::to_string(rows.size())+"    WHEEL  SCROLL",.43f,kMuted);
    } else {
        add(24,26,p.Entry()?p.Entry()->name:"MODEL VIEWER",.9f); add(551,33,"BACK",.55f);
        add(444,33,p.muted?"UNMUTE":"MUTE",.5f,p.muted?kRed:kWhite);
        add(436,64,"M  TOGGLE SOUND",.3f,kMuted);
        add(24,336,"ANIMATION",.45f,kMuted);
        add(36,365,"<",.6f); add(292,365,">",.6f);
        add(69,365,p.Animation()?p.Animation()->name:"UNAVAILABLE",.53f);
        add(42,410,"PLAY",.53f,p.playing?kWhite:kMuted); add(126,410,"PAUSE",.53f,p.playing?kMuted:kWhite); add(214,410,"RESET",.53f);
        if(p.ready) add(24,445,std::to_string(p.animation+1)+" / "+std::to_string(p.Entry()->animations.size())+
            "    "+std::to_string(p.tick)+" / "+std::to_string(p.rigs[0].motion.ticks)+" TICKS",.4f,kMuted);
        add(356,355,"CAMERA",.46f,kWhite);
        add(356,376,"LEFT DRAG  ROTATE / TILT",.43f,kMuted);
        add(356,392,"MIDDLE DRAG  RAISE / LOWER",.43f,kMuted);
        add(356,408,"A / D, LEFT / RIGHT  ROTATE",.43f,kMuted);
        add(356,424,"W / S, UP / DOWN  HEIGHT",.43f,kMuted);
        add(356,440,"WHEEL, Q / E  ZOOM     R  RESET",.43f,kMuted);
        add(24,466,"ENTER  PLAY / PAUSE    PAGE UP / DOWN  ANIMATION    ESC  BACK",.38f,kMuted);
        add(238,339,"CHOOSE CLIP",.3f,kMuted);
        if(p.animationList && p.Entry()) {
            const auto& clips=p.Entry()->animations;
            for(int r=0;r<kVisible && p.animationTop+r<static_cast<int>(clips.size());++r)
                add(36,119+r*25,clips[p.animationTop+r].name,.48f,p.animationTop+r==p.animation?kWhite:kMuted);
        }
        if(!p.error.empty()) add(80,225,p.error,.65f,kRed);
    }
    return text;
}
void ModelGallery::Draw(port::ModelRenderer& renderer,const port::UiFont& font,int width,int height,float aspect) const {
    const auto& p=*impl_;
    renderer.SetCinematicEffectsAllowed(false);
    std::vector<port::ModelRenderer::SpriteDraw> sprites;
    const float scale=(4.f/3.f)/std::max(.1f,aspect);
    auto scrollbar=[&](const ListBar& bar,int list) {
        Panel(sprites,bar.left,bar.top,bar.right,bar.bottom,0xFF20201E,scale);
        const float thumb=bar.Thumb(),height=bar.Height();
        Panel(sprites,bar.left+2,thumb,bar.right-2,thumb+height,
              !bar.Max()?0xFF45413A:(p.scrollbarDrag==list?0xFFF3F0E5:0xFFAA8876),scale);
        if(bar.Max()) for(float y:{-.5f,2.5f})
            Panel(sprites,bar.left+4,thumb+height*.5f+y,bar.right-4,thumb+height*.5f+y+1,0xFF45413A,scale);
    };
    if(!p.viewing) {
        for(int c=0;c<4;++c) Panel(sprites,24,116+c*48,184,154+c*48,c==p.category?0xFF57201B:0xFF171717,scale);
        Panel(sprites,200,116,201,413,0xFF45413A,scale);
        const auto rows=p.Entries();
        for(int r=0;r<kVisible && p.top+r<static_cast<int>(rows.size());++r)
            Panel(sprites,216,116+r*33,598,147+r*33,p.top+r==p.row?0xFF57201B:0xFF101010,scale);
        scrollbar(p.ModelBar(),1);
        Panel(sprites,24,423,116,455,p.backFocus?0xFF57201B:0xFF171717,scale);
    } else {
        const float boxes[][4]={{536,24,616,56},{24,358,60,390},{280,358,316,390},{24,401,104,434},{112,401,192,434},{200,401,280,434},{68,358,280,390},{432,24,528,56}};
        for(int i=0;i<8;++i) Panel(sprites,boxes[i][0],boxes[i][1],boxes[i][2],boxes[i][3],i==p.hovered?0xE657201B:0xD0171717,scale);
        if(p.animationList && p.Entry()) {
            Panel(sprites,24,113,316,338,0xF5101010,scale);
            if(p.animation>=p.animationTop && p.animation<p.animationTop+kVisible)
                Panel(sprites,24,113+(p.animation-p.animationTop)*25,298,138+(p.animation-p.animationTop)*25,0xFF57201B,scale);
            scrollbar(p.AnimationBar(),2);
        }
    }
    const auto text=Text();
    if(font.Available()) font.Append(text,sprites,scale);
    else if(p.assets) {
        game::SpriteTables tables;
        tables.Load(p.assets->Exe());
        for(const auto& line:text) {
            std::vector<game::SpriteDef> glyphs;
            game::MenuTextScaled(p.assets->Exe(),line.x,line.y-12*line.scale,line.text,0,line.scale,glyphs);
            for(auto& glyph:glyphs) {
                glyph.flags|=0x700; glyph.depth=.005f; glyph.tinted=true;
                glyph.tint[0]=((line.color>>16)&255)/255.f;
                glyph.tint[1]=((line.color>>8)&255)/255.f;
                glyph.tint[2]=(line.color&255)/255.f;
                glyph.alpha=((line.color>>24)&255)/255.f;
                const uint32_t id=tables.TextureId(glyph.id);
                const auto* texture=p.assets->TextureById(tables.TextureFile(glyph.id),id&0x7FFFFFFF);
                if(!texture) continue;
                const auto record=game::MakeSpriteRecord(glyph,id,glyph.tint);
                port::ModelRenderer::SpriteDraw draw;
                draw.texture=texture; draw.textureId=id; draw.depth=record.depth;
                draw.isp=game::SpriteIsp(record); draw.tsp=game::SpriteTsp(record); draw.key=record.key|0xF;
                game::SpriteQuad(record,texture->LogicalWidth(),texture->LogicalHeight(),1.0001f,-.80008f,draw.v);
                for(auto& vertex:draw.v) vertex.x=320+(vertex.x-320)*scale;
                auto shadow=draw; shadow.depth+=.001f;
                const float offset=std::clamp(line.scale*1.75f,1.25f,2.75f);
                for(auto& vertex:shadow.v) {
                    vertex.x+=offset*scale; vertex.y+=offset;
                    vertex.diffuse=(((line.color>>24)*230/255)<<24);
                }
                sprites.push_back(shadow); sprites.push_back(draw);
            }
        }
    }
    AppendMenuScrollHint(ScrollHint(),sprites,aspect);
    game::Mat4 view=game::Mat4::Identity(),projection=game::Mat4::Identity();
    if(p.viewing && p.ready) {
        view=game::Mat4::Translate(-p.center[0],-p.center[1],-p.center[2])*CameraRotation(p.camera);
        // At the default zoom the complete model fits above the controls.
        // The bounding sphere makes that fit independent of the orbit angle.
        view.m[14]+=p.radius*p.camera.zoom*(4.8f/3.1f);
        const float nearPlane=p.radius*.01f,farPlane=p.radius*24;
        const float fy=2.2f,fx=fy*static_cast<float>(height)/std::max(1,width);
        projection={{fx,0,0,0,0,fy,0,0,0,0,farPlane/(farPlane-nearPlane),1,0,0,-nearPlane*farPlane/(farPlane-nearPlane),0}};
        // Center the actual perspective silhouette, not just its world-space
        // bounding box. Long limbs and an orbit can move the projected center
        // even when the world-space pivot is correct. Every model and clip
        // uses logical (320,200), with user-requested height applied afterward.
        float low[2]={INFINITY,INFINITY},high[2]={-INFINITY,-INFINITY};
        for(const auto& item:p.scene) {
            game::Mat4 model; std::copy(item.transform,item.transform+16,model.m);
            const auto modelView=model*view;
            for(const auto& mesh:item.model->meshes) for(const auto& strip:mesh.strips)
                for(const auto& vertex:strip.vertices) {
                    float point[3]; game::mx::Transform(modelView,vertex.pos,point);
                    if(point[2]<=nearPlane) continue;
                    const float projected[2]={fx*point[0]/point[2],fy*point[1]/point[2]};
                    for(int axis=0;axis<2;++axis) {
                        low[axis]=std::min(low[axis],projected[axis]);
                        high[axis]=std::max(high[axis],projected[axis]);
                    }
                }
        }
        if(std::isfinite(low[0]) && std::isfinite(low[1])) {
            projection.m[8]=-(low[0]+high[0])*.5f;
            projection.m[9]=1.f/6.f-(low[1]+high[1])*.5f;
        }
        view.m[13]-=p.camera.height*p.radius;
    }
    const auto vp=view*projection;
    port::ModelRenderer::Lighting light;
    light.enabled=true; light.dirToLight[0]=.35f; light.dirToLight[1]=.65f; light.dirToLight[2]=-.68f;
    for(int i=0;i<3;++i) { light.diffuse[i]=.8f; light.ambientRS[i]=.48f; }
    renderer.DrawGame(width,height,vp.m,&light,nullptr,view.m,&sprites,nullptr,projection.m);
}
int ModelGallery::SelectedModel() const { return impl_->selected; }
int ModelGallery::SelectedAnimation() const { return impl_->animation; }
int ModelGallery::AnimationTick() const { return impl_->tick; }
int ModelGallery::AnimationLength() const { return impl_->ready?impl_->rigs[0].motion.ticks:0; }
bool ModelGallery::Playing() const { return impl_->playing; }
bool ModelGallery::Muted() const { return impl_->muted; }
void ModelGallery::SetMuted(bool muted) {
    auto& p=*impl_;
    if(p.muted==muted) return;
    p.muted=muted;
    if(muted) p.Stop(); // Also drains cues queued earlier in the same frame.
    else if(p.ready && p.playing && p.rigs[0].motion.ticks==1) p.cueAtStart=true;
    else if(p.playing) p.ResumeLoops();
}
int ModelGallery::LoadedPartCount() const { return static_cast<int>(impl_->parts.size()); }
const ModelGallery::CameraState& ModelGallery::Camera() const { return impl_->camera; }
const std::string& ModelGallery::Error() const { return impl_->error; }

} // namespace play
