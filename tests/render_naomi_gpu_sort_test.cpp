// Native fragment resolve versus the original unsplit fixed-function GPU draws.
// No game data or GUI is needed. A deliberately tiny allocator forces replay.
#include "render/d3d11_device.h"
#include "game/anim.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void Require(bool good,const char* why){if(!good){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static port::ModelConstants Constants(const std::array<float,4>& color,bool fog=false) {
    port::ModelConstants c{};const auto identity=game::Mat4::Identity();
    std::memcpy(c.world,identity.m,64);std::memcpy(c.modelView,identity.m,64);
    // Camera forward is -Z. Positive monotone reversed depth is .1/eyeDepth.
    c.projection[0]=c.projection[5]=1;c.projection[11]=-1;c.projection[14]=.1f;
    c.localCount[2]=1;c.flags[1]=1;c.flags[2]=-1;c.flags[3]=1;
    std::copy(color.begin(),color.end(),c.diffuse);
    if(fog){c.fogParams[0]=.4f;c.fogParams[1]=5;c.fogParams[2]=c.fogParams[3]=1;
        c.fogColor[0]=.2f;c.fogColor[1]=.3f;c.fogColor[2]=.1f;c.fogColor[3]=1;}
    return c;
}
static void Triangle(std::vector<game::ModelVertex>& vertices,float slope,float depth=1) {
    for(auto xy:{std::pair<float,float>{-.85f,-.8f},{.85f,-.8f},{0,.85f}}) {
        const float d=depth/(1+slope*xy.first);game::ModelVertex v{};
        v.pos[0]=xy.first*d;v.pos[1]=xy.second*d;v.pos[2]=-d;vertices.push_back(v);
    }
}
static port::GpuDraw Draw(uint32_t first,uint32_t constants,int src,int dst,bool sorted=true) {
    port::GpuDraw d;d.stream=port::GpuDraw::Stream::Dynamic;d.first=first;d.count=3;d.constants=constants;
    d.state.blendEnable=1;d.state.srcBlend=src;d.state.dstBlend=dst;d.state.zwrite=0;d.state.zfunc=4;
    d.naomiSortable=sorted;return d;
}
int main() {
    _putenv_s("HOTD2_TEST_NAOMI_GPU_NODES","32");
    port::GpuDeviceDesc descriptor;descriptor.width=descriptor.height=64;
    std::string error;auto gpu=port::CreateD3D11Device(descriptor,&error);Require(bool(gpu),error.c_str());
    auto graphics=gpu->EffectiveGraphics();graphics.naomiGraphics=true;
    // Failure after the first shader exists must not commit partial state or
    // make the next configuration report support with missing buffers.
    _putenv_s("HOTD2_TEST_NAOMI_INIT_FAILURE","after-gather");
    Require(gpu->ConfigureGraphics(graphics,&error),error.c_str());
    Require(!gpu->SupportsNaomiGpuSort(),"partial Native initialization advertised support");
    Require(gpu->NaomiSortError().find("HRESULT 0x8007000E")!=std::string::npos &&
        gpu->NaomiSortError().find("device reason 0x")!=std::string::npos,"Native initialization failure lost HRESULT/device reason");
    _putenv_s("HOTD2_TEST_NAOMI_INIT_FAILURE","");
    Require(gpu->ConfigureGraphics(graphics,&error),error.c_str());
    Require(gpu->NaomiSortError().empty(),"Native initialization could not recover on retry");
    Require(gpu->SupportsNaomiGpuSort(),gpu->NaomiSortError().empty()?"Native GPU sort unsupported":gpu->NaomiSortError().c_str());
    const float clear[]={.07f,.11f,.17f,.27f};
    auto render=[&](const port::GpuDrawList& list,const std::vector<game::ModelVertex>& vertices) {
        gpu->SetDynamicVertices(vertices.data(),vertices.size());Require(gpu->BeginFrame(clear),"begin Native GPU test");
        gpu->Submit(list);Require(gpu->NaomiSortError().empty(),gpu->NaomiSortError().c_str());
        std::vector<uint8_t> pixels;Require(gpu->ReadPixels(&pixels),"read Native GPU test");gpu->EndFrame();return pixels;
    };
    size_t cases=0,pixels=0;
    for(bool fog:{false,true})for(auto factors:{std::array<int,4>{5,6,5,6},{2,2,5,2},{9,1,5,6},{7,8,3,4},{6,2,8,2}})
        for(bool reverse:{false,true}) {
        std::vector<game::ModelVertex> vertices;Triangle(vertices,.2f);Triangle(vertices,-.2f);
        port::GpuDrawList original;original.width=original.height=64;
        original.model={Constants({.72f,.14f,.24f,.37f},fog),Constants({.13f,.81f,.32f,.63f},fog)};
        const auto a=Draw(0,0,factors[0],factors[1]),b=Draw(3,1,factors[2],factors[3]);
        original.draws=reverse?std::vector<port::GpuDraw>{b,a}:std::vector<port::GpuDraw>{a,b};
        const auto actual=render(original,vertices);
        auto left=original,right=original;left.draws={a,b};right.draws={b,a};
        for(auto& d:left.draws)d.naomiSortable=false;for(auto& d:right.draws)d.naomiSortable=false;
        const auto leftPixels=render(left,vertices),rightPixels=render(right,vertices);
        for(int y=4;y<60;++y)for(int x=4;x<60;++x){const auto& expected=x<32?leftPixels:rightPixels;
            for(int k=0;k<4;++k){const size_t at=(y*64+x)*4+k;if(std::abs(int(actual[at])-int(expected[at]))>1){
                std::fprintf(stderr,"fog%d reverse%d factors%d/%d,%d/%d pixel%d,%d channel%d actual%d expected%d\n",
                    fog,reverse,factors[0],factors[1],factors[2],factors[3],x,y,k,actual[at],expected[at]);
                auto dump=[&](const char* label,const std::vector<uint8_t>& image){const size_t p=(y*64+x)*4;
                    std::fprintf(stderr,"%s: %u %u %u %u\n",label,image[p],image[p+1],image[p+2],image[p+3]);};
                dump("sorted",actual);dump("AB reference",leftPixels);dump("BA reference",rightPixels);
                for(auto which:{a,b}){
                    auto one=original;which.naomiSortable=false;one.draws={which};dump(which.first?"B original blend":"A original blend",render(one,vertices));
                    one.draws[0].naomiSortable=true;dump(which.first?"B gathered blend":"A gathered blend",render(one,vertices));
                    one.draws[0].state.srcBlend=2;one.draws[0].state.dstBlend=1;
                    dump(which.first?"B gathered color":"A gathered color",render(one,vertices));
                    one.draws[0].naomiSortable=false;dump(which.first?"B original color":"A original color",render(one,vertices));
                }
                Require(false,"Native fragment resolve differs from unsplit original GPU blending");}}
            ++pixels;}
        ++cases;
    }
    // Equal reciprocal depths retain source draw order, including framebuffer alpha.
    std::vector<game::ModelVertex> flat;Triangle(flat,0);Triangle(flat,0);
    port::GpuDrawList ties;ties.width=ties.height=64;ties.model={Constants({.8f,.2f,.1f,.4f}),Constants({.1f,.6f,.3f,.6f})};
    ties.draws={Draw(0,0,5,6),Draw(3,1,5,6)};
    auto expectedTies=ties;for(auto& d:expectedTies.draws)d.naomiSortable=false;
    const auto equal=render(ties,flat),equalReference=render(expectedTies,flat);
    for(size_t i=0;i<equal.size();++i)Require(std::abs(int(equal[i])-int(equalReference[i]))<=1,"equal-depth source order changed");
    // Real opaque depth must reject the gathered fragments before UAV writes.
    auto occluded=ties;Triangle(flat,0,.5f);occluded.model.push_back(Constants({.2f,.3f,.9f,1}));
    auto wall=Draw(6,2,2,1,false);wall.state.blendEnable=0;wall.state.zwrite=1;
    occluded.draws.insert(occluded.draws.begin(),wall);occluded.opaqueDrawCount=1;
    auto occlusionReference=occluded;for(auto& d:occlusionReference.draws)d.naomiSortable=false;
    Require(render(occluded,flat)==render(occlusionReference,flat),"Native gather ignored opaque depth or wrote transparent depth");
    // Exercise the short-list fast paths and packed colors across mixed blends.
    for (uint32_t layers : {1u, 3u, 8u, 17u}) {
        std::vector<game::ModelVertex> vertices;
        port::GpuDrawList mixed; mixed.width = mixed.height = 64;
        for (uint32_t i = 0; i < layers; ++i) {
            Triangle(vertices, 0);
            mixed.model.push_back(Constants({float(i % 3) * .4f, .21f, .53f, .37f}, true));
            mixed.draws.push_back(Draw(i * 3, i, i % 2 ? 9 : 5, i % 2 ? 2 : 6));
        }
        auto reference = mixed;
        for (auto& draw : reference.draws) draw.naomiSortable = false;
        const auto actual = render(mixed, vertices), expected = render(reference, vertices);
        for (size_t i = 0; i < actual.size(); ++i)
            Require(std::abs(int(actual[i])-int(expected[i])) <= 1, "packed fragment color changed mixed blend/depth-tie results");
    }
    // More than128 layers, with source energy making any dropped half visible.
    std::vector<game::ModelVertex> manyVertices;port::GpuDrawList many;many.width=many.height=64;
    for(uint32_t i=0;i<260;++i){Triangle(manyVertices,0,1+float(i)*.003f);many.model.push_back(Constants({1.f/255,0,0,1}));many.draws.push_back(Draw(i*3,i,2,2));}
    auto manyReference=many;for(auto& d:manyReference.draws)d.naomiSortable=false;
    const auto dense=render(many,manyVertices),denseReference=render(manyReference,manyVertices);
    Require(dense==denseReference,"Native fragment list dropped layers or failed same-frame allocator replay");
    // Resize above both tile dimensions. Check every pixel, including the
    // crossing order change at x256 and scissor edges y128/y256. The reference
    // remains ordinary unsplit GPU draws with no tiled resolve.
    Require(gpu->ResizeTarget(512,384),"resize Native target above resolve tile size");
    std::vector<game::ModelVertex> tiledVertices;Triangle(tiledVertices,.2f);Triangle(tiledVertices,-.2f);
    port::GpuDrawList tiled;tiled.width=512;tiled.height=384;
    tiled.model={Constants({.72f,.14f,.24f,.37f},true),Constants({.13f,.81f,.32f,.63f},true)};
    const auto tiledA=Draw(0,0,6,2),tiledB=Draw(3,1,8,2);
    auto tileLeft=tiled,tileRight=tiled;tileLeft.draws={tiledA,tiledB};tileRight.draws={tiledB,tiledA};
    for(auto& d:tileLeft.draws)d.naomiSortable=false;for(auto& d:tileRight.draws)d.naomiSortable=false;
    const auto leftReference=render(tileLeft,tiledVertices),rightReference=render(tileRight,tiledVertices);
    for(bool reverse:{false,true}) {
        tiled.draws=reverse?std::vector<port::GpuDraw>{tiledB,tiledA}:std::vector<port::GpuDraw>{tiledA,tiledB};
        const auto image=render(tiled,tiledVertices);
        for(int y=0;y<tiled.height;++y)for(int x=0;x<tiled.width;++x)for(int k=0;k<4;++k) {
            const auto& expected=x<256?leftReference:rightReference;const size_t at=(y*tiled.width+x)*4+k;
            Require(std::abs(int(image[at])-int(expected[at]))<=1,"tiled Native resolve changed a pixel or left a scissor seam");
        }
    }
    Require(gpu->ResizeTarget(64,64),"downsize Native target after tiled resolve");
    Require(render(ties,flat)==equal,"Native resize/downsize retained stale heads, snapshot or scissor state");
    // Options and the legacy path remain unchanged when Native sorting is disabled.
    graphics.naomiGraphics=false;Require(gpu->ConfigureGraphics(graphics,&error),error.c_str());
    Require(!gpu->SupportsNaomiGpuSort(),"Native GPU path leaked into PC graphics");
    Require(render(expectedTies,flat)==equalReference,"Native GPU path changed subsequent ordinary GPU draws");
    _putenv_s("HOTD2_TEST_NAOMI_GPU_NODES","");
    std::printf("PASS %zu crossing Native GPU cases/%zu pixels, mixed/260-layer blends, forced allocator replay, initialization retry, tiled resize and PC isolation\n",cases,pixels);
}
