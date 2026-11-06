// Native varying Gouraud/specular translucency: independently intersect original
// triangles per pixel, interpolate their already packed colors, and depth-sort.
#include "render/model_renderer.h"
#include "render/d3d11_device.h"
#include "game/anim.h"
#include "game/camera.h"
#ifdef HOTD2_TEST_D3D12
#include "render/d3d12_device.h"
#endif
#ifdef HOTD2_TEST_SDL_GPU
#include "render/sdl_gpu_device.h"
#include <SDL3/SDL.h>
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct Recorder final:port::GpuDevice {
    port::GraphicsOptions graphics;port::GpuDrawList last;std::vector<game::ModelVertex> vertices,dynamic;bool processing=false,nativeGpu=false;
    std::vector<port::GpuDrawList> submissions;
    const char* Name()const override{return "prelit-record";}
    port::GpuTexture CreateTexture(const port::RgbaImage&,bool)override{return 1;}
    void ClearTextures()override{}
    void SetStaticVertices(const game::ModelVertex* v,size_t n)override{vertices.clear();if(n)vertices.assign(v,v+n);}
    void SetDynamicVertices(const game::ModelVertex* v,size_t n)override{dynamic.clear();if(n)dynamic.assign(v,v+n);}
    void SetSpriteVertices(const game::TLVertex*,size_t)override{}
    bool BeginFrame(const float[4])override{return true;}void EndFrame()override{}
    void Submit(const port::GpuDrawList& list)override{last=list;submissions.push_back(list);}
    bool ReadPixels(std::vector<uint8_t>*)override{return false;}
    int TargetWidth()const override{return 640;}int TargetHeight()const override{return 480;}
    const port::GraphicsOptions& EffectiveGraphics()const override{return graphics;}
    bool RequiresSceneProcessing()const override{return processing;}
    bool SupportsNaomiGpuSort()const override{return nativeGpu;}
};
static game::Model Triangle(int id,int instruction,bool textured=true) {
    game::Model m;game::ModelMesh mesh;auto* w=mesh.material.words;
    w[0]=0x8200002c;w[1]=0x83000000;w[2]=0x94100000u|(instruction<<6);w[8]=textured?0:0xffffffff;w[9]=2;
    const float material[]={.62f,id?.67f:.53f,id?.31f:.77f,id?.69f:.44f,id?.83f:.29f};
    const float specular[]={id?.19f:.38f,id?.32f:.23f,id?.47f:.11f};
    std::memcpy(w+10,material,sizeof(material));std::memcpy(w+16,specular,sizeof(specular));
    game::ModelStrip strip;strip.ctrl=0x48;strip.triangleList=true;
    const float xy[3][2]={{-.55f,-.48f},{.55f,-.48f},{0,.6f}};
    const float normal[3][3]={{.1f,.05f,.76f},{.15f,.1f,.98f},{.2f,.1f,.91f}};
    for(int i=0;i<3;++i){game::ModelVertex v{};std::copy(xy[i],xy[i]+2,v.pos);v.pos[2]=(id?-.5f:.5f)*v.pos[0];
        std::copy(normal[(i+id)%3],normal[(i+id)%3]+3,v.normal);v.uv[0]=float(i==1);v.uv[1]=float(i==2);strip.vertices.push_back(v);}
    mesh.strips.push_back(strip);m.meshes.push_back(mesh);return m;
}
static double Byte(double x){return std::floor(std::clamp(x,0.,1.)*255);}
static double Dot(const double* a,const double* b){return (a[0]*b[0]+a[1]*b[1])+a[2]*b[2];}
struct Colors{double base[3]{},spec[3]{};};
static Colors OriginalVertex(const game::ModelVertex& v,const game::ModelMaterial& material,bool table,const port::ModelRenderer::Lighting& light) {
    Colors out;const double n[]={v.normal[0],v.normal[1],v.normal[2]};
    double amount[3]={1,1,1};
    if(table) {
        for(int k=0;k<3;++k)amount[k]=light.naomiTableAmbient[k]*material.AmbientScale();
        for(int j=0;j<light.localCount;++j) {
            const auto& l=light.local[j];double d[]={l.pos[0]-double(v.pos[0]),l.pos[1]-double(v.pos[1]),l.pos[2]+2.-double(v.pos[2])};
            const double d2=Dot(d,d),lambert=Dot(n,d);if(lambert<=0 || d2>=double(l.range)*l.range)continue;
            const double distance=std::sqrt(d2);double a=lambert/distance;
            if(l.type==2){double dir[]={l.dir[0],l.dir[1],l.dir[2]};const double length=std::sqrt(Dot(dir,dir));for(double& c:dir)c/=length;
                const double rho=-Dot(d,dir)/distance,cutoff=std::cos(double(l.theta));if(rho<cutoff)continue;a*=(rho-cutoff)/(1-cutoff);}
            a/=std::max(1.,(l.att[0]+l.att[1]*distance)+l.att[2]*d2);
            for(int k=0;k<3;++k)amount[k]+=l.diffuse[k]*a;
        }
    } else {
        const double intensity=Byte(double(light.naomiIntensity)*material.AmbientScale()+std::max(0.,n[2]));
        const double q=std::max(0.,2*n[2]*n[2]-1),spec=Byte(n[2]>0?std::pow(q,4):0);
        for(int k=0;k<3;++k){amount[k]=intensity;out.spec[k]=std::floor(Byte(material.Specular(k)*light.naomiColor[k])*spec/256)/255;}
    }
    const double face[]={material.DiffuseR(),material.DiffuseG(),material.DiffuseB()};
    for(int k=0;k<3;++k)out.base[k]=table?Byte(face[k]*light.naomiColor[k]*amount[k])/255:
        std::floor(Byte(face[k]*light.naomiColor[k])*amount[k]/256)/255;
    return out;
}
struct Hit{double depth,weight[3];int source;};
static bool Intersect(const game::Model& m,int source,const double ray[3],Hit* hit) {
    const auto& v=m.meshes[0].strips[0].vertices;double a[3],e1[3],e2[3],s[3];
    for(int k=0;k<3;++k){a[k]=v[0].pos[k]-(k==2?2.:0.);e1[k]=double(v[1].pos[k])-v[0].pos[k];e2[k]=double(v[2].pos[k])-v[0].pos[k];s[k]=-a[k];}
    const double p[]={ray[1]*e2[2]-ray[2]*e2[1],ray[2]*e2[0]-ray[0]*e2[2],ray[0]*e2[1]-ray[1]*e2[0]};
    const double determinant=Dot(e1,p);if(std::abs(determinant)<1e-15)return false;
    const double u=Dot(s,p)/determinant;
    const double q[]={s[1]*e1[2]-s[2]*e1[1],s[2]*e1[0]-s[0]*e1[2],s[0]*e1[1]-s[1]*e1[0]};
    const double vweight=Dot(ray,q)/determinant,depth=Dot(e2,q)/determinant;
    if(u<1e-5||vweight<1e-5||u+vweight>1-1e-5||depth<=0)return false;
    *hit={depth,{1-u-vweight,u,vweight},source};return true;
}
static double FogFactor(float start,float end,double depth) {
    const float far=end*.5f,near=start*.5f;uint32_t bits=0;std::memcpy(&bits,&far,4);
    const double density=std::ldexp(double(128+((bits>>16)&127))/128,int((bits>>23)&255)-126);
    const double z=std::clamp(density/depth,1.,255.9999),exponent=std::floor(std::log2(z));
    const double m=z*16/std::exp2(exponent)-16;const int index=int(m)+16*int(exponent);
    auto entry=[&](int i){return Byte((far*std::exp2(-double(i)/16)-near)/(far-near));};
    return (entry(index)*(1-(m-std::floor(m)))+entry(std::min(index+1,127))*(m-std::floor(m)))/255;
}
template<class T> static bool SameBytes(const std::vector<T>& a,const std::vector<T>& b) {
    return a.size()==b.size() && (a.empty() || std::memcmp(a.data(),b.data(),a.size()*sizeof(T))==0);
}
static bool SameDraw(const port::GpuDraw& a,const port::GpuDraw& b) {
    return a.pipeline==b.pipeline && a.stream==b.stream && a.first==b.first && a.count==b.count &&
        a.texture==b.texture && a.normalRoughnessTexture==b.normalRoughnessTexture && a.constants==b.constants &&
        a.reactive==b.reactive && a.hud==b.hud && a.naomiSortable==b.naomiSortable &&
        std::memcmp(&a.state,&b.state,sizeof(a.state))==0;
}
static void GpuPhaseContract() {
    Recorder device;port::ModelRenderer renderer;port::AssetStore assets;std::string error;
    Require(renderer.Init(&device,&error),error.c_str());
    auto a=Triangle(0,3),b=Triangle(1,3),opaque=Triangle(0,3),unknown=Triangle(0,3);
    opaque.meshes[0].material.words[0]&=~0x07000000u;opaque.meshes[0].material.words[2]=(opaque.meshes[0].material.words[2]&~0x180000u)|0x80000u;
    unknown.meshes[0].material.words[9]=0xfffffffeu; // unsupported original special kernel, never a native candidate
    const auto item=[](const game::Model* model){port::ModelRenderer::SceneItem i;i.model=model;i.transform[14]=-2;i.cullable=false;i.noMeshCull=true;return i;};
    const std::vector<port::ModelRenderer::SceneItem> base={item(&opaque),item(&a),item(&b)};
    port::RgbaImage image;image.width=image.height=1;image.pixels={255,255,255,255};
    const auto sprite=[&](uint32_t key,bool native,bool hud=true) {
        game::SpriteRecord rec;rec.key=key&0x80000000u;rec.depth=1;rec.x=320;rec.y=240;rec.sx=40;rec.sy=30;rec.color=0xffffffff;rec.flags=0xa;rec.u1=rec.v1=1;
        port::ModelRenderer::SpriteDraw s;s.texture=&image;s.key=key;s.depth=1;s.hud=hud;s.naomiAutosort=native;
        s.isp=game::SpriteIsp(rec);s.tsp=game::SpriteTsp(rec);game::SpriteQuad(rec,1,1,1.0001f,-.80008f,s.v);return s;
    };
    // Priorities1/2 intentionally put portHUD ahead of native models in the
    // original mixed list. The opaque original HUD must remain in pass0.
    const std::vector<port::ModelRenderer::SpriteDraw> hud={sprite(0x80000001u,false),sprite(0x80000002u,false),sprite(0,true)};
    const auto identity=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();
    port::ModelRenderer::Lighting light;light.enabled=true;light.naomiStandard=true;light.dirToLight[1]=0;light.dirToLight[2]=1;
    struct Snapshot {port::GpuDrawList list;std::vector<game::ModelVertex> vertices,dynamic;std::vector<port::GpuDrawList> submissions;std::vector<port::ModelRenderer::Pass1Entry> pass1;};
    const auto record=[&](const std::vector<port::ModelRenderer::SceneItem>& items,const std::vector<port::ModelRenderer::SpriteDraw>& sprites,bool capability,bool naomi=true,bool processing=false) {
        device.graphics.naomiGraphics=naomi;device.nativeGpu=capability;device.processing=processing;device.submissions.clear();
        renderer.ClearCache();renderer.SetScene(assets,items);renderer.DrawGame(640,480,projection.m,&light,nullptr,identity.m,&sprites,nullptr,projection.m);
        return Snapshot{device.last,device.vertices,device.dynamic,device.submissions,renderer.lastPass1};
    };
    const auto tagged=[](const port::GpuDrawList& list){return std::count_if(list.draws.begin(),list.draws.end(),[](const port::GpuDraw& d){return d.naomiSortable;});};
    const auto cpu=record(base,hud,false),gpu=record(base,hud,true);
    Require(cpu.list.opaqueDrawCount==2 && gpu.list.opaqueDrawCount==2,"native GPU phase moved the opaque model/original-HUD prefix");
    Require(tagged(gpu.list)==2 && tagged(cpu.list)==0 && gpu.list.draws.size()==6,"native GPU source phase tags/count differ");
    Require(gpu.dynamic.size()==6 && gpu.list.naomiPrelitVertices.empty(),"native GPU phase still split or prelit original source triangles on CPU");
    Require(cpu.dynamic.size()+cpu.list.naomiPrelitVertices.size()>gpu.dynamic.size()+6,"capability-false control did not exercise CPU crossing splits");
    for(size_t i=0;i<2;++i) {
        const auto& c=cpu.list.draws[i];const auto& g=gpu.list.draws[i];Require(SameDraw(c,g),"native GPU phase changed an opaque prefix draw");
        Require(std::memcmp(&cpu.list.model[c.constants],&gpu.list.model[g.constants],sizeof(port::ModelConstants))==0,"native GPU phase changed opaque prefix constants");
    }
    Require(gpu.pass1.size()>=4 && gpu.pass1[0].sprite==0 && gpu.pass1[1].sprite==1,"portHUD phase fixture did not actually precede native source models");
    for(int source=0;source<2;++source) {
        const auto& d=gpu.list.draws[2+source];const auto& model=source?b:a;
        Require(d.naomiSortable && d.pipeline==port::GpuDraw::Pipeline::Model && d.stream==port::GpuDraw::Stream::Static && d.count==3,"GPU source was replaced by split/prelit geometry");
        Require(gpu.list.model[d.constants].diffuse[0]==model.meshes[0].material.DiffuseR(),"GPU phase changed original native draw tie order");
        Require(std::memcmp(gpu.vertices.data()+d.first,model.meshes[0].strips[0].vertices.data(),3*sizeof(game::ModelVertex))==0,"GPU phase changed original within-draw triangle order/vertices");
        const auto& overlay=gpu.list.draws[4+source];Require(overlay.pipeline==port::GpuDraw::Pipeline::Sprite && overlay.hud && !overlay.naomiSortable && overlay.first==uint32_t(source*4),"portHUD overlays were not kept after native resolve in stable order");
    }
    const auto requireFallback=[&](const std::vector<port::ModelRenderer::SceneItem>& items,const std::vector<port::ModelRenderer::SpriteDraw>& sprites,bool naomi=true) {
        const auto before=record(items,sprites,false,naomi),after=record(items,sprites,true,naomi);
        Require(tagged(after.list)==0 && before.list.opaqueDrawCount==after.list.opaqueDrawCount && before.list.draws.size()==after.list.draws.size(),"unsafe/non-native phase bypassed the CPU path");
        for(size_t i=0;i<before.list.draws.size();++i)Require(SameDraw(before.list.draws[i],after.list.draws[i]),"capability changed fallback draw order/state");
        Require(SameBytes(before.list.model,after.list.model)&&SameBytes(before.list.sprite,after.list.sprite)&&
            SameBytes(before.list.naomiPrelitVertices,after.list.naomiPrelitVertices)&&SameBytes(before.vertices,after.vertices)&&SameBytes(before.dynamic,after.dynamic),"capability changed fallback constants or geometry");
        if(naomi)Require(after.dynamic.size()+after.list.naomiPrelitVertices.size()>12,"unsafe phase did not retain CPU crossing resolution");
    };
    auto withUnknown=base;withUnknown.push_back(item(&unknown));requireFallback(withUnknown,hud);
    auto sceneSprite=hud;sceneSprite.push_back(sprite(0x80000003u,false,false));requireFallback(base,sceneSprite);
    auto failedHud=hud;auto failed=sprite(0x80000003u,true);failed.v[3].rhw*=.5f;failedHud.push_back(failed);requireFallback(base,failedHud);
    requireFallback(base,hud,false); // PC graphics never tag native GPU sources
    auto processed=hud;processed.pop_back();processed.push_back(failed);
    const auto processing=record(base,processed,true,true,true);
    Require(processing.submissions.size()==2 && tagged(processing.submissions[0])==2 && processing.submissions[0].opaqueDrawCount==1,"HUD extraction removed/reindexed native GPU tags or the opaque boundary");
    Require(std::none_of(processing.submissions[0].draws.begin(),processing.submissions[0].draws.end(),[](const port::GpuDraw& d){return d.pipeline==port::GpuDraw::Pipeline::Sprite;}),"processed native scene retained extracted HUD sprites");
    Require(processing.submissions[1].draws.size()==3 && std::all_of(processing.submissions[1].draws.begin(),processing.submissions[1].draws.end(),[](const port::GpuDraw& d){return d.pipeline==port::GpuDraw::Pipeline::Sprite && d.hud && !d.naomiSortable;}),"processed HUD entered the native fragment phase");
    std::printf("PASS native GPU phase guards, opaque prefix, stable source/triangle order, HUD extraction and CPU/PC isolation\n");
}
int main(int argc,char** argv) {
    GpuPhaseContract();
    port::GpuDeviceDesc desc;std::string error;std::unique_ptr<port::GpuDevice> gpu;
#ifdef HOTD2_TEST_D3D12
    if(argc>1&&std::strcmp(argv[1],"d3d12")==0)gpu=port::CreateD3D12Device(desc,&error);else
#endif
#ifdef HOTD2_TEST_SDL_GPU
    if(argc>1&&std::strcmp(argv[1],"sdlgpu")==0){Require(SDL_InitSubSystem(SDL_INIT_VIDEO),SDL_GetError());gpu=port::CreateSdlGpuDevice(desc,&error);}else
#endif
        gpu=port::CreateD3D11Device(desc,&error);
    Require(gpu!=nullptr,error.c_str());Recorder capture;capture.graphics.naomiGraphics=true;
    port::ModelRenderer renderer;Require(renderer.Init(&capture,&error),error.c_str());port::AssetStore assets;
    const auto identity=game::Mat4::Identity(),projection=game::DeviceView()*game::DeviceProjection();
    port::RgbaImage images[2];port::GpuTexture textures[2];
    const std::array<uint8_t,4> texel[2]={{{182,91,47,142}},{{39,153,201,173}}};
    for(int i=0;i<2;++i){images[i].width=images[i].height=1;images[i].pixels.assign(texel[i].begin(),texel[i].end());textures[i]=gpu->CreateTexture(images[i]);Require(textures[i]!=0,"create Gouraud texture");}
    auto options=gpu->EffectiveGraphics();options.naomiGraphics=true;Require(gpu->ConfigureGraphics(options,&error),error.c_str());
    const float clear[]={.027f,.043f,.061f,1};size_t pixelsChecked=0,cases=0;
    for(bool table:{false,true})for(int instruction:{0,1,2,3})for(bool fogOn:{false,true})for(bool reverse:{false,true}) {
        auto a=Triangle(0,instruction),b=Triangle(1,instruction);game::Model* models[]={&a,&b};
        port::ModelRenderer::Lighting light;light.enabled=true;light.naomiStandard=!table;light.naomiTable=table;
        light.dirToLight[1]=0;light.dirToLight[2]=1;light.naomiIntensity=.137f;
        light.naomiColor[1]=.81f;light.naomiColor[2]=.73f;
        light.naomiTableAmbient[0]=.09f;light.naomiTableAmbient[1]=.13f;light.naomiTableAmbient[2]=.17f;
        light.localCount=1;auto& local=light.local[0];local.pos[0]=.27f;local.pos[1]=.14f;local.pos[2]=.6f;
        local.diffuse[0]=.47f;local.diffuse[1]=.31f;local.diffuse[2]=.19f;local.range=100;
        local.att[0]=.5f;local.att[1]=.13f;local.att[2]=.03f;
        // Alternate point and actor-spot TABLE paths, including a nonunit axis.
        if(table&&fogOn){local.type=2;local.theta=.65f;local.phi=0;local.dir[0]=.2f;local.dir[1]=-.1f;local.dir[2]=-2;}
        port::ModelRenderer::Fog fog;fog.enabled=fogOn;fog.start=0;fog.end=10;fog.color[0]=.12f;fog.color[1]=.18f;fog.color[2]=.24f;
        std::vector<port::ModelRenderer::SceneItem> items(2);
        for(int i=0;i<2;++i){auto& item=items[i];item.model=models[reverse?1-i:i];item.transform[14]=-2;item.cullable=false;item.noMeshCull=true;}
        renderer.ClearCache();renderer.SetScene(assets,items);renderer.DrawGame(640,480,projection.m,&light,&fog,identity.m,nullptr,nullptr,projection.m);
        auto list=capture.last;const bool varying=!table||instruction!=0;
        const bool split=list.naomiPrelitVertices.size()+capture.dynamic.size()>6;
        if(!split)std::fprintf(stderr,"split fixture table%d mode%d fog%d reverse%d prelit%zu dynamic%zu draws%zu\n",table,instruction,fogOn,reverse,list.naomiPrelitVertices.size(),capture.dynamic.size(),list.draws.size());
        Require(split,"native triangles did not enter split stream");
        // The texture is deliberately injected after recording. A TABLE DECAL
        // with this initially missing texture can validly use the prelit path
        // too; once textured it ignores face RGB, and TABLE has no specular.
        for(auto& d:list.draws){const bool pipeline=(d.pipeline==port::GpuDraw::Pipeline::NaomiPrelit&&d.stream==port::GpuDraw::Stream::NaomiPrelit);
            if(varying&&!pipeline)std::fprintf(stderr,"pipeline fixture table%d mode%d fog%d reverse%d pipeline%u stream%u\n",table,instruction,fogOn,reverse,unsigned(d.pipeline),unsigned(d.stream));
            Require(varying?pipeline:(pipeline||d.stream==port::GpuDraw::Stream::Dynamic),"varying native source lost its prelit pipeline");
            auto& c=list.model[d.constants];const int source=c.diffuse[3]<.6f?0:1;c.flags[0]=1;d.texture=textures[source];
            Require(d.state.zfunc==4&&!d.state.zwrite&&c.flags[2]<0,"prelit native list changed autosort depth state");}
        gpu->SetStaticVertices(capture.vertices.data(),capture.vertices.size());gpu->SetDynamicVertices(capture.dynamic.data(),capture.dynamic.size());
        Require(gpu->BeginFrame(clear),"begin Gouraud crossing frame");gpu->Submit(list);std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read Gouraud crossing frame");gpu->EndFrame();
        Colors vertexColors[2][3];for(int source=0;source<2;++source)for(int v=0;v<3;++v)vertexColors[source][v]=OriginalVertex(models[source]->meshes[0].strips[0].vertices[v],models[source]->meshes[0].material,table,light);
        for(int y:{213,229,246,263})for(int x:{273,291,308,331,349,367}) {
            const double ray[]={((x+.5)/320.-1)/projection.m[0],(1-(y+.5)/240.)/projection.m[5],-1};
            std::vector<Hit> hits;Hit h;for(int source=0;source<2;++source)if(Intersect(*models[source],source,ray,&h))hits.push_back(h);
            if(hits.size()!=2||std::abs(hits[0].depth-hits[1].depth)<1e-6)continue;
            std::stable_sort(hits.begin(),hits.end(),[](const Hit& l,const Hit& r){return l.depth>r.depth;});
            double expected[3]={clear[0],clear[1],clear[2]};
            for(const auto& hit:hits){const int source=hit.source;double base[3]{},spec[3]{};
                for(int k=0;k<3;++k)for(int v=0;v<3;++v){base[k]+=hit.weight[v]*vertexColors[source][v].base[k];spec[k]+=hit.weight[v]*vertexColors[source][v].spec[k];}
                const double textureAlpha=texel[source][3]/255.,faceAlpha=Byte(models[source]->meshes[0].material.DiffuseA())/255;
                const double alpha=instruction<2?textureAlpha:instruction==2?faceAlpha:textureAlpha*faceAlpha;
                for(int k=0;k<3;++k){const double texture=texel[source][k]/255.;double color=instruction==0?texture:instruction==2?base[k]*(1-textureAlpha)+texture*textureAlpha:base[k]*texture;
                    // The native LUT stores the fog amount (zero near the eye),
                    // unlike PC's linear retained-source-color coefficient.
                    color=std::clamp(color+spec[k],0.,1.);if(fogOn){const double factor=FogFactor(fog.start,fog.end,hit.depth);color=color*(1-factor)+fog.color[k]*factor;}
                    expected[k]=color*alpha+expected[k]*(1-alpha);}
            }
            for(int k=0;k<3;++k){const int actual=pixels[(y*640+x)*4+k],want=int(std::round(expected[k]*255));if(std::abs(actual-want)>2){
                std::fprintf(stderr,"table%d mode%d fog%d reverse%d pixel%d,%d channel%d actual%d expected%d\n",table,instruction,fogOn,reverse,x,y,k,actual,want);Require(false,"native varying colors/order differ from independent original-vertex per-ray oracle");}}
            ++pixelsChecked;
        }
        ++cases;
    }
    Require(pixelsChecked>500,"Gouraud crossing oracle did not cover enough independent rays");
    // Compare the optimized BSP submission against the unsplit original GPU
    // triangles. This independently checks each backend's actual front-face
    // convention for both authored windings and D3DCULL none/CW/CCW.
    std::vector<uint8_t> emptyPixels;
    Require(gpu->BeginFrame(clear),"begin authored cull clear");Require(gpu->ReadPixels(&emptyPixels),"read authored cull clear");gpu->EndFrame();
    size_t cullCases=0;
    for(int cull:{1,2,3})for(bool reverse:{false,true}) {
        auto model=Triangle(0,3,false);model.meshes[0].material.words[9]=0xffffffffu;
        auto& strip=model.meshes[0].strips[0];strip.ctrl=(strip.ctrl&~3u)|(cull==2?3:cull==3?2:0);
        if(reverse)std::swap(strip.vertices[1],strip.vertices[2]);
        port::ModelRenderer::SceneItem item;item.model=&model;item.transform[14]=-2;item.cullable=false;item.noMeshCull=true;
        renderer.ClearCache();renderer.SetScene(assets,{item});
        renderer.DrawGame(640,480,projection.m,nullptr,nullptr,identity.m,nullptr,nullptr,projection.m);
        const auto sorted=capture.last;Require(!sorted.model.empty()&&capture.vertices.size()==3,"authored cull source constants/vertices missing");
        auto original=sorted;original.draws.clear();original.naomiPrelitVertices.clear();
        port::GpuDraw draw;draw.pipeline=port::GpuDraw::Pipeline::Model;draw.stream=port::GpuDraw::Stream::Static;
        draw.first=0;draw.count=3;draw.constants=0;draw.state.cull=uint8_t(cull);
        draw.state.zfunc=4;draw.state.zwrite=0;draw.state.blendEnable=1;draw.state.srcBlend=5;draw.state.dstBlend=6;
        original.draws.push_back(draw);
        gpu->SetStaticVertices(capture.vertices.data(),capture.vertices.size());gpu->SetDynamicVertices(capture.dynamic.data(),capture.dynamic.size());
        std::vector<uint8_t> expected,actual;
        Require(gpu->BeginFrame(clear),"begin original authored cull");gpu->Submit(original);Require(gpu->ReadPixels(&expected),"read original authored cull");gpu->EndFrame();
        Require(gpu->BeginFrame(clear),"begin optimized authored cull");gpu->Submit(sorted);Require(gpu->ReadPixels(&actual),"read optimized authored cull");gpu->EndFrame();
        const bool visible=cull==1 || (cull==2?!reverse:reverse);
        if(actual!=expected || (expected!=emptyPixels)!=visible || sorted.draws.empty()==visible) {
            std::fprintf(stderr,"authored cull%d reverse%d draws%zu actualEqualsGPU%d GPUvisible%d expectedVisible%d\n",cull,reverse,sorted.draws.size(),actual==expected,expected!=emptyPixels,visible);
            Require(false,"native CPU winding rejection differs from original GPU culling");
        }
        ++cullCases;
    }
    size_t sourceAddCases=0,sourceAddRays=0;
    for(bool includeOver:{false,true})for(bool reverse:{false,true}) {
        std::array<game::Model,3> models={Triangle(0,3,false),Triangle(1,3,false),Triangle(0,3,false)};
        const float face[3][4]={{.5f,.125f,.25f,.0625f},{.375f,.0625f,.03125f,.125f},{.25f,.03125f,.0625f,.03125f}};
        for(int source=0;source<3;++source) {
            auto* material=models[source].meshes[0].material.words;
            material[2]=(material[2]&~0xfc000000u)|(source==0?0x94000000u:source==1?0x84000000u:0x24000000u);
            material[9]=0xffffffffu;std::memcpy(material+11,face[source],sizeof(face[source])); // word10 is Ambient
            std::fill(material+16,material+19,0u);
            const auto& decoded=models[source].meshes[0].material;
            Require(decoded.DiffuseA()==face[source][0] && decoded.DiffuseR()==face[source][1] && decoded.DiffuseG()==face[source][2] && decoded.DiffuseB()==face[source][3],"source-additive fixture did not encode its intended face RGBA");
        }
        for(auto& v:models[2].meshes[0].strips[0].vertices)v.pos[2]=.18f*v.pos[1]-.03f;
        std::vector<port::ModelRenderer::SceneItem> items;
        for(int source=includeOver?0:1;source<3;++source){port::ModelRenderer::SceneItem item;item.model=&models[source];item.transform[14]=-2;item.cullable=false;item.noMeshCull=true;items.push_back(item);}
        if(reverse)std::reverse(items.begin(),items.end());
        // Explicit native lighting avoids the model viewer's untextured,
        // unlit gray fallback. Kernel-1 has constant intensity1 and no specular.
        port::ModelRenderer::Lighting sourceLight;sourceLight.enabled=true;sourceLight.naomiStandard=true;
        renderer.ClearCache();renderer.SetScene(assets,items);renderer.DrawGame(640,480,projection.m,&sourceLight,nullptr,identity.m,nullptr,nullptr,projection.m);
        const auto list=capture.last;
        if(!includeOver){size_t vertices=0;for(const auto& d:list.draws)vertices+=d.count;Require(vertices==6,"ONE+ONE source created unnecessary BSP cutters among commuting additions");}
        gpu->SetStaticVertices(capture.vertices.data(),capture.vertices.size());gpu->SetDynamicVertices(capture.dynamic.data(),capture.dynamic.size());
        Require(gpu->BeginFrame(clear),"begin mixed source-only additions");gpu->Submit(list);std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read mixed source-only additions");gpu->EndFrame();
        for(int y:{213,229,246,263})for(int x:{273,291,308,331,349,367}) {
            const double ray[]={((x+.5)/320.-1)/projection.m[0],(1-(y+.5)/240.)/projection.m[5],-1};
            std::vector<Hit> hits;Hit hit;for(int source=includeOver?0:1;source<3;++source)if(Intersect(models[source],source,ray,&hit))hits.push_back(hit);
            if(hits.size()!=(includeOver?3u:2u))continue;
            std::stable_sort(hits.begin(),hits.end(),[](const Hit& a,const Hit& b){return a.depth>b.depth;});
            double expected[3]={clear[0],clear[1],clear[2]};
            for(const auto& h:hits)for(int k=0;k<3;++k) {
                // Original standard kernel: independently packed face/intensity
                // bytes, an eight-bit divisor, and separate packed face alpha.
                const double color=std::floor(Byte(face[h.source][k+1])*255/256)/255,alpha=Byte(face[h.source][0])/255;
                expected[k]=color*(h.source==2?1.:alpha)+expected[k]*(h.source==0?1-alpha:1.);
            }
            for(int k=0;k<3;++k)if(std::abs(int(pixels[(y*640+x)*4+k])-int(std::round(expected[k]*255)))>2) {
                std::fprintf(stderr,"source additive over%d reverse%d pixel%d,%d ch%d actual%u expected%.3f\n",includeOver,reverse,x,y,k,pixels[(y*640+x)*4+k],expected[k]*255);
                Require(false,"ONE+ONE/SRC_ALPHA+ONE/nonadditive order differs from original per-ray blending");
            }
            ++sourceAddRays;
        }
        ++sourceAddCases;
    }
    Require(sourceAddRays>50,"mixed source-only addition oracle lacks coverage");
    // Original screen-space sprites use the same native translucent list as
    // muzzle geometry. The additive flash crosses the sprite's depth plane,
    // so its order must reverse across the screen even though the sprite has
    // CPU priority F and the flash has priority C.
    size_t mixedChecks=0;
    for(bool fogOn:{false,true})for(uint8_t faceAlpha:{uint8_t(0),uint8_t(128)}) {
        auto flash=Triangle(0,0);auto* material=flash.meshes[0].material.words;
        material[2]=0x84000000u;material[9]=0xffffffffu;
        std::fill(material+16,material+19,0u);
        port::ModelRenderer::SceneItem item;item.model=&flash;item.priority=0xc;
        item.transform[14]=-1;item.cullable=false;item.noMeshCull=true;
        game::SpriteRecord rec;rec.key=0x80000000u;rec.depth=1;rec.x=320;rec.y=240;
        rec.sx=240;rec.sy=180;rec.color=(uint32_t(faceAlpha)<<24)|0x97e561;rec.flags=0xa|(fogOn?0x800:0);
        rec.u0=.123456f;rec.v0=.234567f;rec.u1=.901234f;rec.v1=.876543f;
        port::ModelRenderer::SpriteDraw sprite;sprite.texture=&images[1];sprite.key=rec.key|0xf;sprite.depth=rec.depth;
        sprite.isp=game::SpriteIsp(rec);sprite.tsp=game::SpriteTsp(rec);sprite.naomiAutosort=true;
        game::SpriteQuad(rec,1,1,1.0001f,-.80008f,sprite.v);
        std::vector<port::ModelRenderer::SpriteDraw> sprites={sprite};
        renderer.ClearCache();renderer.SetScene(assets,{item});
        port::ModelRenderer::Fog fog;fog.enabled=fogOn;fog.start=0;fog.end=10;fog.color[0]=.12f;fog.color[1]=.18f;fog.color[2]=.24f;
        renderer.DrawGame(640,480,projection.m,nullptr,&fog,identity.m,&sprites,nullptr,projection.m);
        auto list=capture.last;Require(renderer.lastPass1.size()==2,"mixed native source submissions missing");
        Require(list.draws.size()>2,"mixed sprite/flash depth crossing was not split");
        Require(capture.dynamic.size()>=6,"native sprite source quad missing");
        uint32_t bits=0;std::memcpy(&bits,&rec.u0,4);bits&=0xffff0000u;float uv16=0;std::memcpy(&uv16,&bits,4);
        Require(capture.dynamic[0].uv[0]==uv16,"native sprite UV16 was treated as full-float or normalized integer");
        for(auto& d:list.draws){Require(d.pipeline==port::GpuDraw::Pipeline::Model&&d.stream==port::GpuDraw::Stream::Dynamic,"native sprite retained separate 2D pipeline");
            auto& c=list.model[d.constants];c.flags[0]=1;d.texture=d.state.dstBlend==2?textures[0]:textures[1];
            Require(!d.state.zwrite&&c.flags[2]<0&&d.state.zfunc==4,"native sprite retained depth writes/alpha test");
            if(d.state.dstBlend==6)Require(d.state.addrU==1&&d.state.addrV==1,"native sprite lost authored wrap addressing");}
        gpu->SetStaticVertices(capture.vertices.data(),capture.vertices.size());gpu->SetDynamicVertices(capture.dynamic.data(),capture.dynamic.size());
        Require(gpu->BeginFrame(clear),"begin mixed sprite frame");gpu->Submit(list);std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read mixed sprite frame");gpu->EndFrame();
        auto rayHit=[&](int x,int y,Hit* hit){
            const double ray[]={((x+.5)/320.-1)/projection.m[0],(1-(y+.5)/240.)/projection.m[5],-1};
            auto original=flash;for(auto& v:original.meshes[0].strips[0].vertices)v.pos[2]+=1; // Intersect uses a -2 world origin.
            return Intersect(original,0,ray,hit);
        };
        for(int y:{215,239,260})for(int x:{240,270,310,340,370,400}) {
            Hit hit;if(!rayHit(x,y,&hit))continue;
            std::array<int,2> order=hit.depth>1?std::array<int,2>{0,1}:std::array<int,2>{1,0};
            double expected[3]={clear[0],clear[1],clear[2]};
            for(int source:order)for(int k=0;k<3;++k) {
                const double alpha=texel[source][3]/255.*(source?faceAlpha/255.:1.);
                const uint8_t tint[]={151,229,97};double color=texel[source][k]/255.*(source?tint[k]/255.:1.);
                if(fogOn){const double factor=FogFactor(fog.start,fog.end,source?1:hit.depth);color=color*(1-factor)+fog.color[k]*factor;}
                expected[k]=color*alpha+expected[k]*(source?1-alpha:1);
            }
            for(int k=0;k<3;++k){const int actual=pixels[(y*640+x)*4+k],want=int(std::round(std::clamp(expected[k],0.,1.)*255));
                if(std::abs(actual-want)>2){std::fprintf(stderr,"mixed fog%d alpha%u pixel%d,%d channel%d actual%d expected%d\n",fogOn,unsigned(faceAlpha),x,y,k,actual,want);Require(false,"native sprite/flash order differs from per-ray reference");}}
            ++mixedChecks;
        }
        // Appending an opaque probe behind both sources must still pass. This
        // independently tests actual depth behavior of the bridged sprite.
        auto probe=list;probe.draws.clear();probe.model.clear();probe.naomiPrelitVertices.clear();
        auto c=list.model[list.draws[0].constants];c.flags[3]=20;c.fogParams[2]=0;
        port::GpuDraw d=list.draws[0];d.texture=textures[0];d.state.blendEnable=0;d.state.zwrite=1;d.constants=0;
        auto merged=capture.dynamic;d.first=static_cast<uint32_t>(merged.size());d.count=6;
        for(int corner:{0,1,2,2,1,3}){game::ModelVertex v{};v.pos[0]=(corner&1)?.8f:-.8f;v.pos[1]=(corner&2)?.8f:-.8f;v.pos[2]=-1.5f;merged.push_back(v);}
        probe.model.push_back(c);probe.draws.push_back(d);gpu->SetDynamicVertices(merged.data(),merged.size());
        Require(gpu->BeginFrame(clear),"begin sprite depth probe");gpu->Submit(list);gpu->Submit(probe);Require(gpu->ReadPixels(&pixels),"read sprite depth probe");gpu->EndFrame();
        for(int k=0;k<3;++k)Require(pixels[(239*640+310)*4+k]==texel[0][k],"native sprite wrote translucent depth");
        // Explicit opt-in alone cannot change PC or projection-free UI paths.
        capture.graphics.naomiGraphics=false;renderer.SetScene(assets,{item});renderer.DrawGame(640,480,projection.m,nullptr,nullptr,identity.m,&sprites,nullptr,projection.m);
        Require(std::any_of(capture.last.draws.begin(),capture.last.draws.end(),[](const port::GpuDraw& draw){return draw.pipeline==port::GpuDraw::Pipeline::Sprite;}),"native sprite bridge leaked into PC");
        capture.graphics.naomiGraphics=true;renderer.DrawSprites(640,480,sprites);
        Require(capture.last.draws.size()==1&&capture.last.draws[0].pipeline==port::GpuDraw::Pipeline::Sprite,"native sprite bridge changed projection-free UI");
        capture.processing=true;renderer.SetScene(assets,{item});renderer.DrawGame(640,480,projection.m,nullptr,nullptr,identity.m,&sprites,nullptr,projection.m);
        Require(capture.last.draws.size()==1&&capture.last.draws[0].pipeline==port::GpuDraw::Pipeline::Sprite&&capture.last.draws[0].hud&&capture.last.model.empty(),"native HUD entered optional temporal/upscaling scene processing");
        capture.processing=false;
    }
    Require(mixedChecks>50,"mixed sprite/flash oracle missed both depth orders");
    {
        // Native raw TA screen XY maps directly through 2*x/640-1 into the
        // rasterizer. A fractional corner makes the PC-only +.5 shift visibly
        // wrong, instead of hiding it behind an integer point-filtered quad.
        port::RgbaImage grid;grid.width=grid.height=2;
        grid.pixels={255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255};
        const auto texture=gpu->CreateTexture(grid);Require(texture!=0,"create native sprite alignment grid");
        game::SpriteRecord rec;rec.key=0x80000000u;rec.depth=1;rec.x=260.25f;rec.y=225.25f;
        rec.sx=rec.sy=4;rec.color=0xffffffff;rec.flags=5;rec.u1=rec.v1=1;
        port::ModelRenderer::SpriteDraw sprite;sprite.texture=&grid;sprite.key=rec.key|15;sprite.depth=1;
        sprite.isp=game::SpriteIsp(rec);sprite.tsp=game::SpriteTsp(rec);sprite.naomiAutosort=true;
        game::SpriteQuad(rec,2,2,1.0001f,-.80008f,sprite.v);
        const std::vector<port::ModelRenderer::SpriteDraw> sprites={sprite};
        renderer.ClearCache();renderer.SetScene(assets,std::vector<port::ModelRenderer::SceneItem>{});
        renderer.DrawGame(640,480,projection.m,nullptr,nullptr,identity.m,&sprites,nullptr,projection.m);
        auto list=capture.last;Require(list.draws.size()==1,"native alignment sprite missing");
        list.draws[0].texture=texture;gpu->SetDynamicVertices(capture.dynamic.data(),capture.dynamic.size());
        Require(gpu->BeginFrame(clear),"begin native sprite alignment");gpu->Submit(list);std::vector<uint8_t> pixels;
        Require(gpu->ReadPixels(&pixels),"read native sprite alignment");gpu->EndFrame();
        for(int y:{225,228,229,232})for(int x:{260,263,264,267}) {
            const int tx=int(std::floor(((x+.5)-260.25)/8*2));
            // Original C1A04..1A14 default V inversion, independently matched
            // by all13 actual first-state TA sprites: top=v1,bottom=v0.
            const double v=(233.25-(y+.5))/8;
            const int ty=int(std::floor(v*2));
            for(int k=0;k<3;++k)if(pixels[(y*640+x)*4+k]!=grid.pixels[(ty*2+tx)*4+k]){
                std::fprintf(stderr,"sprite alignment pixel%d,%d channel%d actual%u expected%u\n",x,y,k,pixels[(y*640+x)*4+k],grid.pixels[(ty*2+tx)*4+k]);
                Require(false,"native sprite retained PC half-pixel offset or wrong source UV orientation");}
        }
    }
    for(bool translucentInFront:{false,true}) {
        // Original opaque sprites are list0, before translucent geometry, and
        // share its native depth map. Source alpha0/texture alpha<1 are ignored
        // by the native opaque sprite's forced ONE/ZERO+IgnoreTexAlpha path.
        auto nativeProjection=projection;nativeProjection.m[10]=-1;nativeProjection.m[14]=-9.999999747378752e-6f;
        std::vector<port::ModelRenderer::SpriteDraw> sprites(2);
        for(int i=0;i<2;++i){game::SpriteRecord rec;rec.key=i?0x80000000u:0;rec.depth=i?(translucentInFront?.5f:.7f):.6f;
            rec.x=320;rec.y=240;rec.sx=240;rec.sy=180;rec.flags=0xa;rec.color=i?0x80ffffff:0x00ffffff;rec.u1=rec.v1=1;
            auto& s=sprites[i];s.texture=&images[i];s.depth=rec.depth;s.key=rec.key|15;s.isp=game::SpriteIsp(rec);s.tsp=game::SpriteTsp(rec);s.naomiAutosort=true;
            game::SpriteQuad(rec,1,1,1.0001f,-.80008f,s.v);}
        renderer.ClearCache();renderer.SetScene(assets,std::vector<port::ModelRenderer::SceneItem>{});
        renderer.DrawGame(640,480,nativeProjection.m,nullptr,nullptr,identity.m,&sprites,nullptr,nativeProjection.m);
        auto list=capture.last;Require(list.opaqueDrawCount==1&&list.draws.size()==2,"native opaque sprite was deferred to translucent list");
        Require(!list.draws[0].state.blendEnable&&list.draws[0].state.zwrite&&list.model[list.draws[0].constants].diffuse[3]==1,"native opaque sprite lost authored depth/forced alpha");
        for(auto& d:list.draws)d.texture=d.state.blendEnable?textures[1]:textures[0];
        gpu->SetDynamicVertices(capture.dynamic.data(),capture.dynamic.size());Require(gpu->BeginFrame(clear),"begin native opaque sprite frame");gpu->Submit(list);
        std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read native opaque sprite frame");gpu->EndFrame();
        const double alpha=(173./255)*(128./255);
        for(int k=0;k<3;++k){const double expected=translucentInFront?texel[1][k]*alpha+texel[0][k]*(1-alpha):texel[0][k];
            Require(std::abs(int(pixels[(239*640+310)*4+k])-int(std::round(expected)))<=1,"native opaque/translucent sprite depth or IgnoreTexAlpha differs");}
    }
    std::printf("PASS %zu varying native STD/spec/TABLE/TSP/fog crossing cases, %zu per-ray pixels\n",cases,pixelsChecked);
    std::printf("PASS %zu authored cull/winding cases against unsplit original GPU triangles\n",cullCases);
    std::printf("PASS %zu mixed ONE+ONE/SRC_ALPHA+ONE/alpha-over cases; %zu per-ray pixels\n",sourceAddCases,sourceAddRays);
    std::printf("PASS native sprite/flash mixed ordering, UV16/wrap, no-depth-write and PC/UI isolation; %zu rays\n",mixedChecks);
}
