#include "render/render_pose.h"
#include "game/mstack.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Close(float a,float b,float tolerance=1e-4f){return std::abs(a-b)<tolerance;}
static game::Model Model(){
    game::Model model;game::ModelMesh mesh;game::ModelStrip strip;
    for(int k=0;k<3;++k){game::ModelVertex vertex{};vertex.pos[0]=float(k);vertex.normal[2]=2;vertex.uv[0]=float(k)*.25f;strip.vertices.push_back(vertex);strip.offsets.push_back(k*32);}
    mesh.strips.push_back(strip);model.meshes.push_back(mesh);return model;
}
using Item=port::ModelRenderer::SceneItem;
static Item Make(const game::Model* model,uint32_t task){Item item;item.model=model;item.motionTracked=true;item.motionIdentity.task=task;item.motionIdentity.model=1;return item;}
static void Matrix(Item* item,const game::Mat4& matrix){std::memcpy(item->transform,matrix.m,sizeof(item->transform));}

int main(){
    game::Model model=Model();const auto original=model;
    Item a=Make(&model,10),b=Make(&model,20),overlay=Make(&model,30);overlay.motionIdentity.role=1;
    port::TemporalSceneStamp stamp;stamp.world=123;stamp.simulationFrame=100;
    const auto identity=game::Mat4::Identity();auto view=identity;
    port::RenderPoseHistory history;
    std::vector<Item> items={a,b,overlay};const auto saved=items;
    history.Apply(items,stamp,identity,&view);
    for(size_t i=0;i<items.size();++i)Require(items[i].model==saved[i].model&&std::memcmp(items[i].transform,saved[i].transform,64)==0,"default Off changed original pointer or transform");
    Require(!history.Ready()&&std::memcmp(view.m,identity.m,64)==0,"Off changed camera or claimed a history pair");
    history.Set(true,.5f);history.Apply(items,stamp,identity,&view);Require(!history.Ready(),"one tick claimed interpolation readiness");
    ++stamp.simulationFrame;
    Matrix(&a,game::Mat4::RotateZ(16384)*game::Mat4::Translate(20,0,0));b.transform[12]=40;
    const auto camera=game::Mat4::Translate(10,0,0);view=game::mx::Inverse(camera);
    Matrix(&overlay,game::Mat4::Translate(3,4,-20)*camera);
    items={b,overlay,a};history.Apply(items,stamp,camera,&view);
    Require(history.Ready(),"consecutive ticks did not produce a pose pair");
    Require(Close(items[0].transform[12],20)&&Close(items[2].transform[12],10),"reordered actors followed submission position");
    Require(Close(items[2].transform[0],std::sqrt(.5f))&&Close(items[2].transform[1],std::sqrt(.5f)),"rotation was not quaternion interpolated");
    Require(Close(std::hypot(items[2].transform[0],items[2].transform[1]),1),"rotating actor shrank as with raw matrix interpolation");
    Require(Close(view.m[12],-5),"camera was not interpolated independently of World");
    game::Mat4 overlayWorld;std::memcpy(overlayWorld.m,items[1].transform,64);const auto overlayView=overlayWorld*view;
    Require(Close(overlayView.m[12],3)&&Close(overlayView.m[13],4)&&Close(overlayView.m[14],-20),"view-space overlay acquired interpolated camera latency");
    // The same completed simulation pair can be presented at several phases.
    history.Set(true,.25f);view=game::mx::Inverse(camera);items={a,b};history.Apply(items,stamp,camera,&view);
    Require(Close(items[0].transform[12],5)&&Close(items[1].transform[12],10),"repeated presentation replaced an interpolation endpoint");
    history.Set(true,1);view=game::mx::Inverse(camera);items={a,b};history.Apply(items,stamp,camera,&view);
    Require(std::memcmp(items[0].transform,a.transform,64)==0&&std::memcmp(view.m,game::mx::Inverse(camera).m,64)==0,"alpha one altered exact current tick");
    // Fully CPU-deformed copies interpolate their positions and normal direction,
    // keeping current UVs/materials and the authored nonunit normal magnitude.
    history.Reset();history.Set(true,.5f);stamp.simulationFrame=200;
    a=Make(&model,10);a.dynamicGeometry=true;items={a};view=identity;history.Apply(items,stamp,identity,&view);
    for(auto& vertex:model.meshes[0].strips[0].vertices){vertex.pos[0]+=8;vertex.normal[0]=4;vertex.normal[2]=0;vertex.uv[0]+=.1f;}
    ++stamp.simulationFrame;items={a};view=identity;history.Apply(items,stamp,identity,&view);
    Require(items[0].model!=&model&&items[0].dynamicGeometry,"deformed render pose did not own a dynamic copy");
    const auto& vertex=items[0].model->meshes[0].strips[0].vertices[0];
    Require(Close(vertex.pos[0],4)&&Close(std::hypot(vertex.normal[0],vertex.normal[2]),3),"deformed midpoint or original normal magnitude is wrong");
    Require(vertex.uv[0]==model.meshes[0].strips[0].vertices[0].uv[0]&&model.meshes[0].strips[0].vertices[0].pos[0]==8,"interpolation changed source assets or delayed UV animation");
    history.Set(true,.25f);items={a};view=identity;history.Apply(items,stamp,identity,&view);
    Require(Close(items[0].model->meshes[0].strips[0].vertices[0].pos[0],2),"deformation endpoints chased a previous presented pose");
    model.meshes[0].strips[0].ctrl^=0x40;items={a};view=identity;history.Apply(items,stamp,identity,&view);
    Require(items[0].model==&model,"changed topology inherited an unrelated previous vertex pose");
    // Reactive texture rewrites retain current geometry. New actors do not fade
    // in from a removed actor, and changed duplicate counts invalidate the group.
    a.motionReactive=true;items={a};view=identity;history.Apply(items,stamp,identity,&view);Require(items[0].model==&model,"reactive geometry was interpolated");
    Item spawned=Make(&model,999);spawned.transform[12]=77;items={spawned};view=identity;history.Apply(items,stamp,identity,&view);Require(items[0].transform[12]==77,"new task inherited another actor pose");
    history.Reset();history.Set(true,.5f);a=Make(&model,10);items={a};view=identity;history.Apply(items,stamp,identity,&view);
    ++stamp.simulationFrame;a.transform[12]=10;b=a;b.motionIdentity.occurrence=1;b.transform[12]=30;items={a,b};view=identity;history.Apply(items,stamp,identity,&view);
    Require(items[0].transform[12]==10&&items[1].transform[12]==30,"duplicate count change shifted stable actor correspondence");
    // Gaps, rollback, camera script changes, teleports, turns and world changes
    // discard interpolation immediately instead of easing across a scene cut.
    for(int kind=0;kind<6;++kind){
        history.Reset();history.Set(true,.5f);stamp.world=123;stamp.camera={};stamp.simulationFrame=300;items={a};view=identity;history.Apply(items,stamp,identity,&view);
        ++stamp.simulationFrame;auto nextCamera=identity;
        if(kind==0)++stamp.simulationFrame;if(kind==1)stamp.simulationFrame-=2;if(kind==2)++stamp.camera[4];
        if(kind==3)nextCamera=game::Mat4::Translate(1000,0,0);if(kind==4)nextCamera=game::Mat4::RotateY(20000);if(kind==5)++stamp.world;
        items={a};view=game::mx::Inverse(nextCamera);history.Apply(items,stamp,nextCamera,&view);Require(!history.Ready(),"scene discontinuity retained an old interpolation pair");
    }
    // A single actor teleport/shear/scale reflection change is local to it.
    history.Reset();history.Set(true,.5f);stamp.simulationFrame=400;a=Make(&model,10);b=Make(&model,20);items={a,b};view=identity;history.Apply(items,stamp,identity,&view);
    ++stamp.simulationFrame;a.transform[12]=1000;b.transform[12]=20;items={a,b};view=identity;history.Apply(items,stamp,identity,&view);
    Require(items[0].transform[12]==1000&&Close(items[1].transform[12],10),"one teleport invalidated all actors or was interpolated");
    // Nonuniform and reflected SRTs keep their scales; a real shear or a sign
    // change is retained as authored rather than collapsed at the midpoint.
    auto sampleTransform=[&](const game::Mat4& from,const game::Mat4& to){
        history.Reset();history.Set(true,.5f);stamp.simulationFrame=500;
        a=Make(&model,10);Matrix(&a,from);items={a};view=identity;history.Apply(items,stamp,identity,&view);
        ++stamp.simulationFrame;Matrix(&a,to);items={a};view=identity;history.Apply(items,stamp,identity,&view);
        game::Mat4 result;std::memcpy(result.m,items[0].transform,64);return result;
    };
    auto scaleA=identity,scaleB=game::Mat4::RotateY(16384);
    game::mx::Scale(scaleA,-2,3,4);game::mx::Scale(scaleB,-4,5,6);
    const auto reflected=sampleTransform(scaleA,scaleB);
    Require(Close(std::hypot(reflected.m[0],reflected.m[2]),3)&&Close(std::abs(reflected.m[5]),4)&&Close(std::hypot(reflected.m[8],reflected.m[10]),5),"nonuniform reflected SRT lost signed scale");
    auto shear=identity;shear.m[1]=.25f;
    const auto retained=sampleTransform(identity,shear);Require(std::memcmp(retained.m,shear.m,64)==0,"sheared matrix was silently approximated as a rotation");
    auto reflection=identity;reflection.m[0]=-1;
    const auto signChange=sampleTransform(identity,reflection);Require(std::memcmp(signChange.m,reflection.m,64)==0,"reflection sign change collapsed geometry through zero");
    const auto shortest=sampleTransform(game::Mat4::RotateZ(32000),game::Mat4::RotateZ(-32000));
    Require(shortest.m[0]<-.999f&&std::abs(shortest.m[1])<1e-4f,"quaternion interpolation chose the long rotational path");
    history.Set(false,.5f);items={a,b};view=identity;history.Apply(items,stamp,identity,&view);
    Require(!history.Ready()&&items[0].model==a.model&&std::memcmp(items[0].transform,a.transform,64)==0,"disabling failed exact native restoration");
    history.Set(true,std::numeric_limits<float>::quiet_NaN());view=identity;history.Apply(items,stamp,identity,&view);Require(!history.Ready(),"re-enabling reused stale history");
    Require(original.meshes[0].strips[0].vertices[0].pos[0]==0,"fixture baseline unexpectedly changed");
    std::puts("PASS render-only tick-pair interpolation: quaternion transforms, camera/overlay separation, CPU deformation, stable tasks, repeated phases, topology/cuts and exact Off");
}
