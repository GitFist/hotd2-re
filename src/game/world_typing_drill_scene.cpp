#include "game/world_typing_drill_scene.h"
#include "game/world_internal.h"
#include "game/mstack.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <span>

namespace game {
namespace {

struct ActorPose {
    uint32_t source;
    int kind, variant, motion;
    uint32_t flags;
    float pos[3];
    int rot[3];
};
struct PropPose {
    uint32_t source;
    int model;
    float pos[3];
    int rot[3];
    float scale;
};
#include "game/world_typing_drill_scene_data.inc"

struct Scene {
    int sourceScene, camera;
    float cameraTick;
    std::array<int, 7> models;
};
// PC menu scene table 0x5891F8, reordered to the native controller's category order.
// Original section tables: 0x5922FC[7] / 0x592334[7]. These native model identities
// are byte-exact decoded aliases established by the reference model importer.
constexpr Scene kScenes[] = {
    {25,0x117,21,{0x1803,0x1744,0x17D3,0x180C}},
    {9,0x111,0,{0x17F3,0x17F1}},
    {21,0xF0,389,{0x1924,0x1919}},
    {5,0x10E,1,{0x180A,0x181C,0x17F3}},
    {17,0x116,0,{0x18A2}},
    {19,0x10D,0,{0x1811,0x1814}},
    {11,0x37,300,{0x17CB,0x17CD,0x17F7}},
    {15,0x115,0,{0x18A2}},
    {13,0x118,0,{0x17CD,0x17CE,0x17D1,0x17D2,0x1803}},
    {1,0x119,0,{0x18A2}},
    {7,0x39,165,{0x17CB,0x17CD,0x17F7}},
    {23,0x5E,190,{0x17C3,0x1808,0x1A47,0x1818,0x181B,0x17F4,0x1819}},
    {3,0x11D,180,{0x1741}},
};

CameraBank* Curves(World::Impl& w) {
    // ApplyStage installs only data. Use an isolated memory/bank, keeping the
    // attract scene's runner and camera program untouched.
    static CameraBank bank;
    if (!bank.Has(0x4117)) {
        EventMemory unused;
        std::string error;
        if (!w.typingReference.ApplyStage(0,unused,bank,&error)) return nullptr;
    }
    return &bank;
}

void Prepare(World::Impl& w,int model) {
    int file=w.typingReference.ModelFile(model);
    if(file<0&&model>0&&model<0x4000)file=w.exe.S16(0x004E83B4+uint32_t(model)*2);
    if(file>=0&&std::find(w.polFileLoads.begin(),w.polFileLoads.end(),file)==w.polFileLoads.end())
        w.ProcessRequest({0x52,uint32_t(file)});
}

void Anchor(World::Impl& w,const float pos[3]) {
    float view[3];
    mx::Transform(w.cam.view,pos,view);
    if(view[2]>=-1)return;
    std::copy(view,view+3,w.typingDrill.visualAnchor.begin());
    w.typingDrill.visualAnchorValid=true;
}

void Camera(World::Impl& w,const CameraBank& bank,int id,float tick) {
    float pos[3],target[3];int roll=0;
    if(!bank.Evaluate(0x4000+id,tick,pos,target,&roll,false))return;
    w.cam={};
    std::copy(pos,pos+3,w.cam.pos);
    LookAngles(pos,target,&w.cam.pitch,&w.cam.yaw);
    w.cam.view=ViewMatrix(pos,w.cam.pitch,w.cam.yaw,0);
    w.cam.inverse=mx::Inverse(w.cam.view);
    w.view0=w.cam.view;w.inverse0=w.cam.inverse;
    std::copy(pos,pos+3,w.camState.pos);
    w.camState.pitch=w.cam.pitch;w.camState.yaw=w.cam.yaw;w.camState.roll=0;
}

void PlainNode(World::Impl& w,EnemyTask& e,int node,const Mat4& matrix) {
    w.Draw(e.anim.nodes[node].model,matrix,8,-1,false,node,true);
}

void Actor(World::Impl& w,const ActorPose& pose,int tick,bool anchor=true) {
    EnemyTask actor;
    actor.flags34=pose.flags|0x40000u;
    actor.anim.kind=int16_t(pose.kind);actor.variant=pose.variant;
    // Behaviors 14/15 use the authored kind/variant walking or running clip.
    actor.anim.motion[0]=pose.motion?pose.motion:w.EnemyMotionSet(actor,2+((pose.flags>>27)&1));
    if(!pose.motion&&pose.kind>=0x16&&pose.kind<=0x19) {
        const uint32_t row=w.exe.U32(0x005929F0+pose.variant*4);
        actor.anim.motion[0]=int(w.exe.U32(row+8+((pose.flags>>27)&1)*4));
    }
    w.EnemyInitPose(actor);
    if(actor.actorSlot>=0)w.actorSlots[actor.actorSlot]=nullptr;
    actor.actorSlot=-1;actor.nodeFn=PlainNode;
    actor.anim.drawFlags=1;actor.anim.rotOrder=1;
    if(pose.kind==0x44)actor.anim.nodes[2].model=0; // Standalone Kuarl mount has no rider node.
    std::copy(pose.pos,pose.pos+3,actor.pos);
    std::copy(pose.rot,pose.rot+3,actor.rot);
    actor.anim.counter[0]=tick;
    const size_t first=w.draws.size();
    w.AnimDraw(actor);
    for(size_t i=first;i<w.draws.size();++i)Prepare(w,w.draws[i].model);
    if(anchor) {
        float point[3]={actor.pos[0],actor.pos[1]+10,actor.pos[2]};
        if(actor.anim.nodes[2].used)for(int c=0;c<3;++c)point[c]=actor.anim.nodes[2].world.m[12+c];
        Anchor(w,point);
    }
}

void Prop(World::Impl& w,const PropPose& pose,bool anchor=false) {
    const int model=w.typingReference.SourceModel(pose.model);
    if(model<=0)return;
    Prepare(w,model);
    Mat4 matrix=Mat4::Identity();
    mx::Translate(matrix,pose.pos[0],pose.pos[1],pose.pos[2]);
    mx::RotX(matrix,pose.rot[0]);mx::RotY(matrix,pose.rot[1]);mx::RotZ(matrix,pose.rot[2]);
    mx::Scale(matrix,pose.scale,pose.scale,pose.scale);
    w.Draw(model,matrix);
    if(anchor)Anchor(w,pose.pos);
}

void Boards(World::Impl& w,const CameraBank& bank,int tick,unsigned words) {
    // Source board paths 0x5B2230, barrel counts 0x4F4448, local offsets
    // 0x5B2238, and model ids 0x454E13 / 0x454E1E. The PC version has 4/5/3
    // targets, unlike the gun game's three-barrel variant.
    static constexpr int count[]={4,5,3};
    static constexpr int8_t offsets[3][5][3]={
        {{10,32,-6},{-8,32,23},{7,32,53},{0,39,85},{1,1,1}},
        {{-14,51,-38},{11,32,-12},{-10,32,18},{8,32,44},{1,39,72}},
        {{7,44,17},{-3,44,-30},{-8,50,-68},{1,1,1},{1,1,1}}
    };
    for(int i=0;i<3;++i) {
        float pos[3],angles[3];int roll=0;
        if(!bank.Evaluate(0x41B8+i,float(tick%600),pos,angles,&roll,false))continue;
        Mat4 board=Mat4::Identity();
        mx::Translate(board,pos[0],pos[1],pos[2]);
        mx::RotY(board,i==0?0x4000:0xC000);
        const int model=w.typingReference.SourceModel(i==2?0x1AE9:0x1AEA);
        Prepare(w,model);
        Mat4 scaled=board;mx::Scale(scaled,2.5f,2.5f,2.5f);w.Draw(model,scaled);
        for(int b=0;b<count[i];++b) {
            if((words%unsigned(count[i]))>unsigned(b))continue;
            float local[3]={offsets[i][b][0]*.25f,offsets[i][b][1]*.25f,offsets[i][b][2]*.25f};
            float world[3];mx::Transform(board,local,world);
            PropPose barrel{0,0x1AC3,{world[0],world[1],world[2]},{0,0,0},1};
            Prop(w,barrel,!w.typingDrill.visualAnchorValid);
        }
    }
}

} // namespace

void RestoreTypingDrillCamera(World::Impl& w) {
    if(w.gameMode!=4||w.front.nightShiftPage!=12)return;
    if(w.typingDrill.game) {
        w.cam=w.typingDrill.game->Camera();w.view0=w.cam.view;w.inverse0=w.cam.inverse;
        return;
    }
    if(w.typingDrill.view!=1||w.typingDrill.visualView!=1||!w.typingReference.Available())return;
    CameraBank* bank=Curves(w);
    if(!bank)return;
    const int exercise=std::clamp(w.typingDrill.controller.exercise(),0,12);
    const Scene& scene=kScenes[exercise];
    Camera(w,*bank,scene.camera,exercise==9?float(1+w.typingDrill.visualAge%89):scene.cameraTick);
}

bool TypingDrillSceneVisuals(World::Impl& w) {
    auto& ui=w.typingDrill;
    ui.visualAnchorValid=false;
    if(!w.typingReference.Available())return false;
    CameraBank* bank=Curves(w);
    if(!bank)return false;
    const int exercise=std::clamp(ui.controller.exercise(),0,12);
    const Scene& scene=kScenes[exercise];
    const auto& environment=kEnvironment[exercise];
    const unsigned words=ui.controller.result().words;
    const int tick=std::max(0,ui.visualAge);
    Camera(w,*bank,scene.camera,exercise==9?float(1+tick%89):scene.cameraTick);
    w.draws.clear();
    World::Impl::NativeModelDrawScope modelScope(w,-1);
    const bool residency=w.residency;
    w.residency=false; // Independent draw-only actors do not enter the attract resource queue.
    w.env.lightPitch=environment.pitch;w.env.lightYaw=environment.yaw;
    w.env.fogNear=environment.nearFog;w.env.fogFar=environment.farFog;
    std::copy(environment.color,environment.color+3,w.env.lightColor);
    std::copy(environment.fog,environment.fog+3,w.env.fogColor);
    w.env.lightIntensity=environment.intensity;
    w.env.block1=static_cast<const EnvBlock&>(w.env);
    w.env.block1.lightIntensity=1;
    w.LightScene();
    for(int model:scene.models)if(model) {
        Prepare(w,model);
        w.Draw(model,Mat4::Identity(),8,-1,false,-1,false,true);
    }
    const auto& authored=kSceneActors[exercise];
    for(const auto& prop:authored.props)Prop(w,prop,exercise==5);
    if(exercise==1) {
        // Source mount placement 0x48A549; source motion 0x3B8 is native 0x38D.
        Actor(w,{0,0x44,2,0x38D,0,{35,23,-1839},{0,0x4000,0}},tick);
    } else if(exercise==3)Boards(w,*bank,tick,words);
    else if(exercise==7) {
        // Source hanging-target object path; camera 0x115, section 11.
        float pos[3],angles[3];int roll=0;
        if(bank->Evaluate(0x41BC,float(80+tick%140),pos,angles,&roll,false))
            Actor(w,{0xCEBCF00,7,0,0x3E6,0,{pos[0],pos[1],pos[2]},
                     {int(angles[0]),int(angles[1]),int(angles[2])}},tick);
    } else if(!authored.actors.empty()) {
        const size_t first=words%authored.actors.size();
        Actor(w,authored.actors[first],tick);
    }
    w.residency=residency;
    return true;
}
} // namespace game
