// Portable CPU oracle for the bounded native flash BSP. Optional argument:
// <game directory> [Naomi bank override] also stresses twelve native bursts.
// --fixture <file> replays the first failed runtime NSORT001 diagnostic input.
#include "render/naomi_flash_sort.h"
#include "render/asset_store.h"
#include "game/mstack.h"
#include "game/shoot_camera.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>

using Triangle=port::NaomiFlashTriangle;
static void Require(bool value,const char* why) {
    if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
static void RequireResult(bool value,const std::string& why) {Require(value,why.c_str());}
static game::ModelVertex Vertex(float x,float y,float z) {
    game::ModelVertex v{};v.pos[0]=x;v.pos[1]=y;v.pos[2]=z;
    // Deliberately affine attributes make every split independently checkable.
    v.normal[0]=x;v.normal[1]=y;v.normal[2]=z;
    v.uv[0]=x+1;v.uv[1]=2*y+.5f;return v;
}
static void Quad(std::vector<Triangle>& out,float center,float slope,uint32_t source,bool additive=false) {
    const game::ModelVertex v[]={Vertex(-.12f,-.11f,-1+slope*(-.12f-center)),
        Vertex(.12f,-.11f,-1+slope*(.12f-center)),Vertex(-.12f,.11f,-1+slope*(-.12f-center)),
        Vertex(.12f,.11f,-1+slope*(.12f-center))};
    for(auto indices:{std::array<int,3>{0,1,2},{2,1,3}}) {
        Triangle t;t.sourceDraw=source;t.sequence=out.size();t.additive=additive;
        for(int k=0;k<3;++k){t.vertices[k]=v[indices[k]];
            // Separate diffuse/specular payloads must survive a cut as affine
            // colors, without another byte quantization at generated points.
            for(int c=0;c<3;++c){t.vertices[k].color[c]=.37f+v[indices[k]].pos[c]*.23f;t.vertices[k].specular[c]=.19f-v[indices[k]].pos[c]*.17f;}}
        out.push_back(t);
    }
}
struct Hit {double depth;uint32_t source;uint64_t sequence;bool additive;};
struct CullContext {std::array<float,16> projection{};uint8_t cull=1;};
using CullContexts=std::map<uint32_t,CullContext>;
// Independent homogeneous determinant for original GPU facing. Unlike the
// production conservative early rejection, the ray oracle applies authored
// culling even to retained uncertain or eye-crossing triangles.
static bool ReferenceCulled(const Triangle& triangle,const CullContext& context) {
    if(context.cull!=2 && context.cull!=3)return false;
    long double p[3][3]{};
    for(int i=0;i<3;++i)for(int axis=0;axis<3;++axis) {
        const int column=axis==2?3:axis;p[i][axis]=context.projection[12+column];
        for(int k=0;k<3;++k)p[i][axis]+=static_cast<long double>(triangle.vertices[i].pos[k])*context.projection[k*4+column];
    }
    const long double determinant=p[0][0]*(p[1][1]*p[2][2]-p[1][2]*p[2][1])-
        p[0][1]*(p[1][0]*p[2][2]-p[1][2]*p[2][0])+p[0][2]*(p[1][0]*p[2][1]-p[1][1]*p[2][0]);
    return context.cull==2?determinant<0:determinant>0;
}
static bool Intersect(const Triangle& t,double x,double y,Hit* hit) {
    const double ray[]={x,y,-1};double e1[3],e2[3],s[3];
    for(int k=0;k<3;++k){e1[k]=double(t.vertices[1].pos[k])-t.vertices[0].pos[k];e2[k]=double(t.vertices[2].pos[k])-t.vertices[0].pos[k];s[k]=-double(t.vertices[0].pos[k]);}
    const double p[]={ray[1]*e2[2]-ray[2]*e2[1],ray[2]*e2[0]-ray[0]*e2[2],ray[0]*e2[1]-ray[1]*e2[0]};
    const double det=e1[0]*p[0]+e1[1]*p[1]+e1[2]*p[2];if(std::abs(det)<1e-15)return false;
    const double u=(s[0]*p[0]+s[1]*p[1]+s[2]*p[2])/det;
    const double q[]={s[1]*e1[2]-s[2]*e1[1],s[2]*e1[0]-s[0]*e1[2],s[0]*e1[1]-s[1]*e1[0]};
    const double v=(ray[0]*q[0]+ray[1]*q[1]+ray[2]*q[2])/det;
    const double depth=(e2[0]*q[0]+e2[1]*q[1]+e2[2]*q[2])/det;
    if(u< -1e-9 || v< -1e-9 || u+v>1+1e-9 || depth<=0)return false;
    *hit={depth,t.sourceDraw,t.sequence,t.additive};return true;
}
enum class SourceBlend {AlphaOver,AlphaAdd,OneAdd};
using SourceBlends=std::map<uint32_t,SourceBlend>;
static std::array<double,3> Composite(const std::vector<Hit>& hits,const SourceBlends* blends=nullptr) {
    std::array<double,3> color={.013,.021,.034};
    for(const auto& hit:hits) {
        const double alpha=.17+.013*(hit.source%7);
        const auto blend=blends?blends->at(hit.source):(hit.additive?SourceBlend::AlphaAdd:SourceBlend::AlphaOver);
        for(int k=0;k<3;++k) {
            const double source=.025+.007*((hit.source*3+k*7)%11);
            color[k]=source*(blend==SourceBlend::OneAdd?1:alpha)+color[k]*(blend==SourceBlend::AlphaOver?1-alpha:1);
        }
    }
    return color;
}
static size_t CheckRays(const std::vector<Triangle>& input,const std::vector<Triangle>& output,int width=17,double spanX=.2,double spanY=.18,const CullContexts* contexts=nullptr,const SourceBlends* blends=nullptr) {
    size_t checks=0;
    for(int iy=0;iy<width;++iy)for(int ix=0;ix<width;++ix) {
        // Avoid original quad diagonals and deliberately exercise both sides of
        // intersections rather than testing only a model-centroid ordering.
        const double x=((ix+.371)/width-.5)*spanX,y=((iy+.619)/width-.5)*spanY;
        std::vector<Hit> expected,actual;Hit hit;
        const auto visible=[&](const Triangle& t){return !contexts || !ReferenceCulled(t,contexts->at(t.sourceDraw));};
        for(const auto& t:input)if(visible(t)&&Intersect(t,x,y,&hit))expected.push_back(hit);
        for(const auto& t:output)if(visible(t)&&Intersect(t,x,y,&hit))actual.push_back(hit);
        std::stable_sort(expected.begin(),expected.end(),[](const Hit& a,const Hit& b) {
            return a.depth>b.depth;
        });
        // Resolve arithmetic-level coplanar ties in stable submission order.
        // A float-sized pointwise epsilon would wrongly tie distinct crossing
        // planes merely because this ray passes near their intersection.
        for(size_t first=0;first<expected.size();) {
            size_t end=first+1;
            while(end<expected.size() && expected[first].depth-expected[end].depth<1e-10)++end;
            std::stable_sort(expected.begin()+first,expected.begin()+end,[](const Hit& a,const Hit& b){return a.sequence<b.sequence;});
            first=end;
        }
        const auto e=Composite(expected,blends),a=Composite(actual,blends);
        for(int k=0;k<3;++k)if(std::abs(e[k]-a[k])>=2e-6) {
            std::fprintf(stderr,"ray mismatch: input=%zu output=%zu ix=%d iy=%d x=%.9f y=%.9f hits=%zu/%zu RGB %.9f,%.9f,%.9f vs %.9f,%.9f,%.9f\n",input.size(),output.size(),ix,iy,x,y,expected.size(),actual.size(),e[0],e[1],e[2],a[0],a[1],a[2]);
            const auto dump=[](const char* name,const std::vector<Hit>& hits){std::fprintf(stderr,"%s",name);for(size_t i=0;i<std::min<size_t>(hits.size(),32);++i)std::fprintf(stderr," %u:%llu@%.9f%s",hits[i].source,static_cast<unsigned long long>(hits[i].sequence),hits[i].depth,hits[i].additive?"+":"");std::fputc('\n',stderr);};
            dump("expected",expected);dump("actual",actual);
            Require(false,"BSP differs from independently depth-sorted per-ray compositing");
        }
        ++checks;
    }
    return checks;
}
static void ReplayFixture(const char* path) {
    auto* file=std::fopen(path,"rb");Require(file!=nullptr,"open native sort fixture");
    char magic[8];Require(std::fread(magic,1,8,file)==8,"missing native sort fixture version");
    const bool cullContext=std::memcmp(magic,"NSORT003",8)==0;
    const bool clipContext=cullContext || std::memcmp(magic,"NSORT002",8)==0;
    Require(clipContext || std::memcmp(magic,"NSORT001",8)==0,"unsupported native sort fixture version");
    const auto word=[&](unsigned bytes) {
        unsigned char data[8]{};Require(std::fread(data,1,bytes,file)==bytes,"truncated native sort fixture");
        uint64_t value=0;for(unsigned k=0;k<bytes;++k)value|=uint64_t(data[k])<<(8*k);return value;
    };
    const auto values=[&](float* data,size_t count) {
        for(size_t i=0;i<count;++i){const auto bits=uint32_t(word(4));std::memcpy(data+i,&bits,4);}
    };
    const auto count=word(4);Require(count<=8192,"native sort fixture exceeds input budget");
    CullContexts contexts;float guardX=0,guardY=0;
    if(clipContext) {
        const auto sources=word(4);Require(sources<=8192,"native sort fixture exceeds source budget");values(&guardX,1);values(&guardY,1);
        Require(std::isfinite(guardX)&&std::isfinite(guardY)&&guardX>=0&&guardY>=0,"invalid fixture guard band");
        for(size_t i=0;i<sources;++i) {
            const auto source=uint32_t(word(4));auto& context=contexts[source];values(context.projection.data(),16);
            if(cullContext){const auto cull=word(4);Require(cull<=3,"invalid fixture cull mode");context.cull=uint8_t(cull);}
        }
    }
    std::vector<Triangle> input(static_cast<size_t>(count)),output;std::string error;
    for(auto& triangle:input) {
        triangle.sourceDraw=uint32_t(word(4));triangle.sequence=word(8);
        const auto additive=word(4);Require(additive<=1,"invalid native sort fixture additive field");triangle.additive=additive!=0;
        for(auto& v:triangle.vertices) {
            values(v.pos,3);values(v.normal,3);values(v.uv,2);values(v.color,3);values(v.specular,3);
        }
    }
    Require(std::fgetc(file)==EOF,"native sort fixture has trailing data");std::fclose(file);
    std::vector<Triangle> visible;size_t outside=0,culled=0;std::array<size_t,4> modes{};
    for(const auto& triangle:input) {
        if(clipContext) {
            const auto found=contexts.find(triangle.sourceDraw);Require(found!=contexts.end(),"fixture projection source missing");
            const auto& context=found->second;++modes[context.cull];
            if(port::NaomiFlashOutsideProjection(triangle,context.projection.data(),guardX,guardY)){++outside;continue;}
            if(port::NaomiFlashCulledByWinding(triangle,context.projection.data(),context.cull,guardX,guardY)){++culled;continue;}
        }
        visible.push_back(triangle);
    }
    const auto start=std::chrono::steady_clock::now();
    RequireResult(port::SortNaomiFlashTriangles(visible,&output,&error),error);
    const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    const auto checks=CheckRays(input,output,23,1.,.75,cullContext?&contexts:nullptr);
    std::printf("PASS runtime fixture: %zu source / %zu retained -> %zu triangles, %.3f ms, %zu independent viewport ray checks; rejected %zu outside/%zu winding; cull1/2/3=%zu/%zu/%zu\n",input.size(),visible.size(),output.size(),ms,checks,outside,culled,modes[1],modes[2],modes[3]);
}
static void NativeStress(const char* game,const char* bank) {
    port::AssetStore assets;std::string error;
    RequireResult(assets.Open(game,&error),error);
    if(bank)RequireResult(assets.LoadNaomiVisuals(bank,&error),error);
    RequireResult(assets.SetNaomiGraphics(true,&error),error);
    std::vector<Triangle> input;
    for(int player=0;player<2;++player)for(int burst=0;burst<6;++burst) {
        const int ticks[]={8,6,4,2,1,0};const int tick=ticks[burst];
        const float x=float((burst-2)*13+player*7)/640.2079468f,y=float((burst%3-1)*11)/640.2079468f;
        int pitch=0,yaw=0;game::DirToAngles(x,y,-1,&pitch,&yaw);pitch=-pitch;yaw+=0x8000;
        game::Mat4 mv=game::Mat4::Identity();game::mx::Translate(mv,x,y,-1);game::mx::RotY(mv,yaw);game::mx::RotX(mv,pitch);game::mx::Scale(mv,.1f,.1f,.1f);
        for(int layer=0;layer<2;++layer) {
            game::Model model;std::string pol;
            const int id=(layer==0?0x175+player*10:(player?0xB84:0xB76))+tick;
            Require(assets.NaomiModelById(id,model,&pol),"native burst model mapping missing");
            auto transform=mv;if(layer)game::mx::Scale(transform,.5f,.5f,.5f);
            for(const auto& mesh:model.meshes)for(const auto& strip:mesh.strips) {
                std::vector<game::ModelVertex> vertices;game::Triangulate(strip,vertices);
                for(size_t i=0;i+2<vertices.size();i+=3) {
                    Triangle t;t.sourceDraw=uint32_t((player*6+burst)*2+layer);t.sequence=input.size();t.additive=layer==0;
                    for(int k=0;k<3;++k) {t.vertices[k]=vertices[i+k];game::mx::Transform(transform,vertices[i+k].pos,t.vertices[k].pos);game::mx::TransformDir(transform,vertices[i+k].normal,t.vertices[k].normal);}
                    input.push_back(t);
                }
            }
        }
    }
    const auto start=std::chrono::steady_clock::now();std::vector<Triangle> output;
    RequireResult(port::SortNaomiFlashTriangles(input,&output,&error),error);
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    const auto checks=CheckRays(input,output,23);
    std::printf("native twelve bursts: %zu -> %zu triangles, %.3f ms; %zu independent ray checks\n",input.size(),output.size(),elapsed,checks);
}
int main(int argc,char** argv) {
    if(argc==3 && std::strcmp(argv[1],"--fixture")==0){ReplayFixture(argv[2]);return 0;}
    std::vector<Triangle> crossing;Quad(crossing,0,0,0);Quad(crossing,.03124f,.03124f,1);
    std::vector<Triangle> sorted;std::string error;
    float projection[16]{};projection[0]=2;projection[5]=8.f/3;projection[11]=-1;projection[14]=1e-5f;
    auto offscreen=crossing[0];for(auto& v:offscreen.vertices)v.pos[0]+=2;
    Require(port::NaomiFlashOutsideProjection(offscreen,projection),"wholly offscreen source triangle was retained");
    auto covering=crossing[0];covering.vertices[0]=Vertex(-2,-2,-1);covering.vertices[1]=Vertex(2,-2,-1);covering.vertices[2]=Vertex(0,2,-1);
    Require(!port::NaomiFlashOutsideProjection(covering,projection),"triangle covering the viewport was culled because its vertices lie outside");
    auto behind=crossing[0];for(auto& v:behind.vertices)v.pos[2]=1;
    Require(port::NaomiFlashOutsideProjection(behind,projection),"wholly behind-eye source triangle was retained");
    auto boundary=crossing[0];for(auto& v:boundary.vertices)v.pos[0]=std::nextafter(.5f,1.f);
    Require(!port::NaomiFlashOutsideProjection(boundary,projection),"projection roundoff boundary was culled");
    for(int k=0;k<3;++k)boundary.vertices[k].pos[0]=.501f+(k==1?.001f:0.f);
    Require(port::NaomiFlashOutsideProjection(boundary,projection) && !port::NaomiFlashOutsideProjection(boundary,projection,2.f/640,2.f/480),"one-pixel temporal guard band was not retained");
    Require(!port::NaomiFlashCulledByWinding(crossing[0],projection,1) && !port::NaomiFlashCulledByWinding(crossing[0],projection,2) &&
        port::NaomiFlashCulledByWinding(crossing[0],projection,3),"native authored cull modes differ from screen winding");
    auto reversed=crossing[0];std::swap(reversed.vertices[1],reversed.vertices[2]);
    Require(port::NaomiFlashCulledByWinding(reversed,projection,2) && !port::NaomiFlashCulledByWinding(reversed,projection,3),"reversed authored cull mode was not honored");
    auto eyeCross=crossing[0];eyeCross.vertices[0].pos[2]=.1f;
    Require(!port::NaomiFlashCulledByWinding(eyeCross,projection,2) && !port::NaomiFlashCulledByWinding(eyeCross,projection,3),"eye-crossing triangle was classified by unsafe projected winding");
    auto thin=crossing[0];for(auto& v:thin.vertices)v.pos[1]=0;thin.vertices[2].pos[1]=1e-8f;
    Require(!port::NaomiFlashCulledByWinding(thin,projection,3,2.f/640,2.f/480),"subpixel-uncertain winding was rejected");
    auto clipReference=crossing;offscreen.sequence=clipReference.size();clipReference.push_back(offscreen);behind.sequence=clipReference.size();clipReference.push_back(behind);
    std::vector<Triangle> retained;for(const auto& t:clipReference)if(!port::NaomiFlashOutsideProjection(t,projection))retained.push_back(t);
    RequireResult(port::SortNaomiFlashTriangles(retained,&sorted,&error),error);
    CheckRays(clipReference,sorted,23,1.,.75);
    RequireResult(port::SortNaomiFlashTriangles(crossing,&sorted,&error),error);
    Require(sorted.size()>crossing.size(),"intersecting planes were not split");
    for(double x:{10./640.2079468,30./640.2079468}) {
        std::vector<Hit> hits;Hit hit;for(const auto& t:sorted)if(Intersect(t,x,.00731,&hit))hits.push_back(hit);
        Require(hits.size()==2,"cross-plane sample must retain both source layers");
        Require(hits[0].source==(x<.03124?1u:0u),"source order failed to reverse across the intersection");
        Require(hits[0].depth>=hits[1].depth,"cross-plane sample is not far to near");
    }
    for(const auto& t:sorted)for(const auto& v:t.vertices) {
        Require(std::abs(v.uv[0]-(v.pos[0]+1))<2e-6 && std::abs(v.uv[1]-(2*v.pos[1]+.5f))<2e-6,"split UV discontinuity");
        for(int k=0;k<3;++k){Require(std::abs(v.normal[k]-v.pos[k])<2e-6,"split normal interpolation differs");
            Require(std::abs(v.color[k]-(.37f+v.pos[k]*.23f))<2e-6,"split Gouraud color was re-packed or not interpolated");
            Require(std::abs(v.specular[k]-(.19f-v.pos[k]*.17f))<2e-6,"split specular color was re-packed or not interpolated");}
    }
    CheckRays(crossing,sorted);
    std::vector<Triangle> blendMix;Quad(blendMix,0,0,0);Quad(blendMix,.03124f,.03124f,1,true);Quad(blendMix,-.019f,-.046f,2,true);
    const SourceBlends blendKinds={{0,SourceBlend::AlphaOver},{1,SourceBlend::AlphaAdd},{2,SourceBlend::OneAdd}};
    RequireResult(port::SortNaomiFlashTriangles(blendMix,&sorted,&error),error);
    CheckRays(blendMix,sorted,23,.2,.18,nullptr,&blendKinds);
    // These parallel layers have different float reciprocal depths. A float-
    // sized plane epsilon used to call them coplanar and retain near-first
    // source order, even though the native pixel sorter distinguishes them.
    std::vector<Triangle> adjacent;Quad(adjacent,0,0,0);Quad(adjacent,0,0,1);
    const float nearDepth=-1000.f,farDepth=std::nextafter(nearDepth,-std::numeric_limits<float>::infinity());
    Require(1.f/nearDepth!=1.f/farDepth,"parallel layer fixture lost distinct native reciprocal depths");
    for(auto& t:adjacent)for(auto& v:t.vertices){v.pos[0]*=1000;v.pos[1]*=1000;v.pos[2]=t.sourceDraw?farDepth:nearDepth;}
    RequireResult(port::SortNaomiFlashTriangles(adjacent,&sorted,&error),error);
    Require(sorted.size()==4&&sorted[0].sourceDraw==1&&sorted.back().sourceDraw==0,"distinct one-ULP layers were classified coplanar");
    CheckRays(adjacent,sorted);
    auto coplanar=std::vector<Triangle>{crossing[0],crossing[0]};coplanar[0].sourceDraw=20;coplanar[0].sequence=20;coplanar[1].sourceDraw=10;coplanar[1].sequence=10;
    RequireResult(port::SortNaomiFlashTriangles(coplanar,&sorted,&error),error);
    Require(sorted.size()==2 && sorted[0].sourceDraw==10 && sorted[1].sourceDraw==20,"coplanar stable submission order changed");
    const auto previous=sorted;auto invalid=crossing;invalid[0].vertices[0].uv[0]=std::numeric_limits<float>::quiet_NaN();
    Require(!port::SortNaomiFlashTriangles(invalid,&sorted,&error) && sorted.size()==previous.size() && sorted[0].sourceDraw==10,"invalid input did not fail atomically");
    port::NaomiFlashSortLimits limits;limits.maxFragments=crossing.size();
    Require(!port::SortNaomiFlashTriangles(crossing,&sorted,&error,limits) && sorted[0].sourceDraw==10,"fragment bound did not fail atomically");
    limits={};limits.maxDepth=0;Require(!port::SortNaomiFlashTriangles(crossing,&sorted,&error,limits),"depth bound missing");
    auto additiveLeaf=crossing;for(auto& t:additiveLeaf)t.additive=true;
    RequireResult(port::SortNaomiFlashTriangles(additiveLeaf,&sorted,&error,limits),error);
    Require(sorted.size()==additiveLeaf.size(),"terminal additive leaf incorrectly consumed a partition depth");
    auto degenerate=crossing;Triangle zero;degenerate.push_back(zero);RequireResult(port::SortNaomiFlashTriangles(degenerate,&sorted,&error),error);CheckRays(crossing,sorted);
    // Every face plane of a convex shell supports the remaining geometry on
    // one side. Source-face-only BSP therefore needs one level per facet,
    // regardless of how many candidate faces are searched. A spatial median
    // must keep this exact painter problem within the existing depth budget.
    std::vector<Triangle> shell;
    for(int face=0;face<256;++face) {
        const double a=face*6.283185307179586/256,b=(face+1)*6.283185307179586/256;
        const auto va=Vertex(float(.12*std::sin(a)),-.11f,float(-1+.12*std::cos(a)));
        const auto vb=Vertex(float(.12*std::sin(b)),-.11f,float(-1+.12*std::cos(b)));
        auto vc=va,vd=vb;vc.pos[1]=vd.pos[1]=.11f;
        for(const auto& vertices:{std::array<game::ModelVertex,3>{va,vb,vc},std::array<game::ModelVertex,3>{vc,vb,vd}}) {
            Triangle t;t.sourceDraw=uint32_t(face);t.sequence=shell.size();
            for(int k=0;k<3;++k)t.vertices[k]=vertices[k];shell.push_back(t);
        }
    }
    limits={};limits.maxDepth=32;
    RequireResult(port::SortNaomiFlashTriangles(shell,&sorted,&error,limits),error);
    CheckRays(shell,sorted,23);
    // Maximum two-player ring capacity: twelve intersecting secondary planes,
    // each with twenty additive primary triangles. The additive leaf shortcut
    // preserves exact per-pixel composition while bounding partition work.
    std::vector<Triangle> stress;
    for(uint32_t burst=0;burst<12;++burst) {
        const float center=(float(burst)-5.5f)*.006f,slope=(float(burst)-5.5f)*.013f;
        Quad(stress,center,slope,burst*2+1);
        for(int i=0;i<10;++i)Quad(stress,center,slope+.008f*(i-4),burst*2,true);
    }
    const auto start=std::chrono::steady_clock::now();RequireResult(port::SortNaomiFlashTriangles(stress,&sorted,&error),error);
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    CheckRays(stress,sorted,23);std::printf("synthetic twelve bursts: %zu -> %zu triangles, %.3f ms\n",stress.size(),sorted.size(),elapsed);
    if(argc>1)NativeStress(argv[1],argc>2?argv[2]:nullptr);
    std::puts("PASS crossing flash planes, interpolated attributes, coplanar order, finite/budget guards and twelve bursts");
}
