#include "naomi_flash_sort.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace port {
namespace {

// Keep geometry/attributes in double through every partition. Rounding a new
// intersection to a GPU float before another classification required a broad
// float epsilon, which incorrectly tied distinct authored layers one float ULP
// apart. Both children share one double cut and are rounded only when emitted.
struct Vertex {
    double pos[3]{},normal[3]{},uv[2]{},color[3]{},specular[3]{};
    Vertex()=default;
    Vertex(const NaomiFlashVertex& v) {
        for(int k=0;k<3;++k){pos[k]=v.pos[k];normal[k]=v.normal[k];color[k]=v.color[k];specular[k]=v.specular[k];}
        for(int k=0;k<2;++k)uv[k]=v.uv[k];
    }
    NaomiFlashVertex Pack()const {
        NaomiFlashVertex v{};
        for(int k=0;k<3;++k){v.pos[k]=float(pos[k]);v.normal[k]=float(normal[k]);v.color[k]=float(color[k]);v.specular[k]=float(specular[k]);}
        for(int k=0;k<2;++k)v.uv[k]=float(uv[k]);
        return v;
    }
};
struct Plane {
    double n[3]{}, d = 0, epsilon = 0;
    double Distance(const Vertex& v) const {
        return ((n[0]*v.pos[0] + n[1]*v.pos[1]) + n[2]*v.pos[2]) + d;
    }
};
struct Polygon {
    std::vector<Vertex> vertices;
    Plane sourcePlane;
    uint32_t source = 0;
    uint64_t sequence = 0;
    size_t original = 0;
    bool additive = false;
};
struct Node {
    Plane plane;
    std::vector<Polygon> coplanar;
    std::unique_ptr<Node> front, back;
    bool leaf = false;
};
struct Partition {
    Plane plane;
    size_t original = std::numeric_limits<size_t>::max();
};

template<class V> bool Finite(const V& v) {
    for (auto x : v.pos) if (!std::isfinite(x)) return false;
    for (auto x : v.normal) if (!std::isfinite(x)) return false;
    for (auto x : v.uv) if (!std::isfinite(x)) return false;
    for (auto x : v.color) if (!std::isfinite(x)) return false;
    for (auto x : v.specular) if (!std::isfinite(x)) return false;
    return true;
}

bool MakePlane(const Polygon& p, Plane* result) {
    if (p.vertices.size()<3) return false;
    const auto& a = p.vertices[0];
    double longest = 0, best[3]{};
    for (size_t i=1; i+1<p.vertices.size(); ++i) {
        double u[3],v[3];
        for (int k=0;k<3;++k) {
            u[k]=double(p.vertices[i].pos[k])-a.pos[k];
            v[k]=double(p.vertices[i+1].pos[k])-a.pos[k];
        }
        const double cross[3]={u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
        const double length=std::hypot(cross[0],cross[1],cross[2]);
        if (length>longest) {longest=length;std::copy(cross,cross+3,best);}
    }
    if (!(longest>1e-20) || !std::isfinite(longest)) return false;
    Plane plane;
    for (int k=0;k<3;++k) plane.n[k]=best[k]/longest;
    plane.d=-((plane.n[0]*a.pos[0]+plane.n[1]*a.pos[1])+plane.n[2]*a.pos[2]);
    double scale=1;
    for (const auto& v:p.vertices) for (double x:v.pos) scale=std::max(scale,std::abs(x));
    // Arithmetic-level coplanarity only: two different source float depths
    // must remain distinct even when separated by a single representable ULP.
    plane.epsilon=64*std::numeric_limits<double>::epsilon()*scale;
    *result=plane;
    return true;
}

bool TriangleHasArea(const Vertex& a,const Vertex& b,const Vertex& c) {
    double u[3],v[3];
    for(int k=0;k<3;++k){u[k]=b.pos[k]-a.pos[k];v[k]=c.pos[k]-a.pos[k];}
    const double cross[3]={u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
    const double length=std::hypot(cross[0],cross[1],cross[2]);
    return length>1e-20 && std::isfinite(length);
}

int Side(double d, double epsilon) { return d>epsilon?1:d< -epsilon?-1:0; }
int Classify(const Polygon& p, const Plane& plane) {
    bool front=false,back=false;
    for (const auto& v:p.vertices) {
        const int s=Side(plane.Distance(v),plane.epsilon);
        front=front || s>0;back=back || s<0;
    }
    return front?(back?2:1):(back?-1:0);
}

Vertex Interpolate(const Vertex& a,const Vertex& b,double t) {
    Vertex v{};
    for(int k=0;k<3;++k) {
        v.pos[k]=a.pos[k]+(b.pos[k]-a.pos[k])*t;
        v.normal[k]=a.normal[k]+(b.normal[k]-a.normal[k])*t;
        v.color[k]=a.color[k]+(b.color[k]-a.color[k])*t;
        v.specular[k]=a.specular[k]+(b.specular[k]-a.specular[k])*t;
    }
    for(int k=0;k<2;++k)v.uv[k]=a.uv[k]+(b.uv[k]-a.uv[k])*t;
    return v;
}
bool SamePosition(const Vertex& a,const Vertex& b) {
    return a.pos[0]==b.pos[0] && a.pos[1]==b.pos[1] && a.pos[2]==b.pos[2];
}
void Append(std::vector<Vertex>& vertices,const Vertex& v) {
    if(vertices.empty() || !SamePosition(vertices.back(),v))vertices.push_back(v);
}
void Close(std::vector<Vertex>& v) {
    if(v.size()>1 && SamePosition(v.front(),v.back()))v.pop_back();
}

class Sorter {
public:
    explicit Sorter(const NaomiFlashSortLimits& limits):limits_(limits) {}
    const char* error = nullptr;
    size_t fragments = 0;

    bool Build(std::vector<Polygon> polygons,unsigned depth,std::unique_ptr<Node>& out) {
        if(polygons.empty())return true;
        auto node=std::make_unique<Node>();
        if(std::none_of(polygons.begin(),polygons.end(),[](const Polygon& p){return !p.additive;})) {
            node->leaf=true;node->coplanar=std::move(polygons);
            StableOrder(node->coplanar);out=std::move(node);return true;
        }
        // The bound limits partition work, not a terminal additive leaf. A
        // leaf needs no further sorting because all its RGB additions commute.
        if(depth>=limits_.maxDepth)return Fail("Naomi flash BSP depth budget exceeded");
        const auto splitter=Choose(polygons,depth);
        // A cut fragment still lies in its original triangle's plane. Fitting
        // a new plane through a very thin generated fragment amplifies roundoff
        // and can repeatedly split that fragment against itself.
        node->plane=splitter.plane;
        const size_t planeSource=splitter.original;
        std::vector<Polygon> front,back;
        for(auto& p:polygons) {
            switch(p.original==planeSource?0:Classify(p,node->plane)) {
            case 0:node->coplanar.push_back(std::move(p));break;
            case 1:front.push_back(std::move(p));break;
            case -1:back.push_back(std::move(p));break;
            default: {
                if(fragments>limits_.maxFragments || limits_.maxFragments-fragments<2)
                    return Fail("Naomi flash BSP fragment budget exceeded");
                Polygon f,b;f.source=b.source=p.source;f.sequence=b.sequence=p.sequence;f.original=b.original=p.original;
                f.sourcePlane=b.sourcePlane=p.sourcePlane;
                f.additive=b.additive=p.additive;
                // A plane cuts a convex polygon into children with at most
                // one additional vertex. Avoid growing both buffers repeatedly.
                f.vertices.reserve(p.vertices.size()+1);b.vertices.reserve(p.vertices.size()+1);
                for(size_t i=0;i<p.vertices.size();++i) {
                    const auto& a=p.vertices[i];const auto& next=p.vertices[(i+1)%p.vertices.size()];
                    const double da=node->plane.Distance(a),db=node->plane.Distance(next);
                    const int sa=Side(da,node->plane.epsilon),sb=Side(db,node->plane.epsilon);
                    auto edge=a;
                    // Both children must share one stored boundary. Merely
                    // calling a nearby float point "on plane" can otherwise
                    // leave a thin overlap that double-blends a split edge.
                    if(sa==0)for(int k=0;k<3;++k)edge.pos[k]=a.pos[k]-node->plane.n[k]*da;
                    if(sa>=0)Append(f.vertices,edge);
                    if(sa<=0)Append(b.vertices,edge);
                    if(sa*sb<0) {
                        const auto v=Interpolate(a,next,std::clamp(da/(da-db),0.0,1.0));
                        if(!Finite(v))return Fail("Naomi flash BSP produced a nonfinite intersection");
                        Append(f.vertices,v);Append(b.vertices,v);
                    }
                }
                Close(f.vertices);Close(b.vertices);fragments+=2;
                Plane unused;
                if(MakePlane(f,&unused))front.push_back(std::move(f));
                if(MakePlane(b,&unused))back.push_back(std::move(b));
                break;
            }
            }
        }
        StableOrder(node->coplanar);
        if(!Build(std::move(front),depth+1,node->front) || !Build(std::move(back),depth+1,node->back))return false;
        out=std::move(node);return true;
    }

    bool Emit(const Node* node,std::vector<NaomiFlashTriangle>& output) {
        if(!node)return true;
        // The eye's signed plane distance is d. Draw the opposite half first.
        const bool eyeFront=node->plane.d>=0;
        if(!node->leaf && !Emit(eyeFront?node->back.get():node->front.get(),output))return false;
        for(const auto& p:node->coplanar)for(size_t i=1;i+1<p.vertices.size();++i) {
            // Same cross/hypot threshold as MakePlane on a triangle, without
            // allocating a temporary polygon for every emitted GPU triangle.
            if(!TriangleHasArea(p.vertices[0],p.vertices[i],p.vertices[i+1]))continue;
            if(output.size()>=limits_.maxOutputTriangles)return Fail("Naomi flash BSP triangle budget exceeded");
            NaomiFlashTriangle t;t.sourceDraw=p.source;t.sequence=p.sequence;t.additive=p.additive;
            t.vertices[0]=p.vertices[0].Pack();t.vertices[1]=p.vertices[i].Pack();t.vertices[2]=p.vertices[i+1].Pack();output.push_back(t);
        }
        return node->leaf || Emit(eyeFront?node->front.get():node->back.get(),output);
    }
private:
    const NaomiFlashSortLimits& limits_;
    bool Fail(const char* reason){error=reason;return false;}
    static void StableOrder(std::vector<Polygon>& polygons) {
        std::stable_sort(polygons.begin(),polygons.end(),[](const Polygon& a,const Polygon& b) {
            return a.sequence!=b.sequence?a.sequence<b.sequence:a.original<b.original;
        });
    }
    Partition Choose(const std::vector<Polygon>& polygons,unsigned depth) {
        Partition best;uint64_t bestCost=std::numeric_limits<uint64_t>::max();
        size_t bestWorst=polygons.size(),nonadditive=0;
        const auto measure=[&](const Partition& candidate,size_t* worst) {
            uint64_t front=0,back=0,split=0;size_t nf=0,nb=0;
            for(const auto& p:polygons) {
                const int side=p.original==candidate.original?0:Classify(p,candidate.plane);
                if(side==2)++split;else if(side>0)++front;else if(side<0)++back;
                if(!p.additive){if(side>0)++nf;if(side<0 || side==2)++nb;}
            }
            *worst=std::max(nf,nb);
            return split*8+(front>back?front-back:back-front);
        };
        // A bounded, deterministic sample balances tree depth against the cost
        // of splitting. All classifications use the exact candidate plane.
        std::vector<size_t> candidates;
        for(size_t i=0;i<polygons.size();++i)if(!polygons[i].additive)candidates.push_back(i);
        nonadditive=candidates.size();
        const size_t samples=std::min<size_t>(24,candidates.size());
        for(size_t k=0;k<samples;++k) {
            const size_t at=candidates[k*candidates.size()/samples];
            const Partition candidate{polygons[at].sourcePlane,polygons[at].original};
            size_t worst=0;const uint64_t cost=measure(candidate,&worst);
            if(cost<bestCost){bestCost=cost;best=candidate;bestWorst=worst;}
        }
        // A convex translucent shell is a counterexample to source-face-only
        // splitting: every face is a support plane, so even exhaustive face
        // selection produces a linear chain. Spatial BSP planes are equally
        // valid painter partitions. Use one only when it guarantees useful
        // progress that the sampled source plane cannot provide; unchanged
        // fragment/triangle/depth budgets still apply to the exact cuts.
        // Each source partition removes at least one nonadditive polygon from
        // a branch. If those polygons fit in the remaining levels, spatial
        // balancing cannot improve the termination bound and only adds cuts.
        // Keep cheap source partitions until half the depth budget is used.
        if(depth>=limits_.maxDepth/2 && nonadditive>limits_.maxDepth-depth &&
           nonadditive>=16 && bestWorst>nonadditive*7/8) {
            double scale=1;
            for(const auto& p:polygons)for(const auto& v:p.vertices)for(double x:v.pos)scale=std::max(scale,std::abs(x));
            uint64_t spatialCost=std::numeric_limits<uint64_t>::max();
            for(int axis=0;axis<3;++axis) {
                std::vector<double> centers;centers.reserve(nonadditive);
                for(size_t at:candidates) {
                    double sum=0;for(const auto& v:polygons[at].vertices)sum+=v.pos[axis];
                    centers.push_back(sum/polygons[at].vertices.size());
                }
                std::sort(centers.begin(),centers.end());
                for(size_t quarter=1;quarter<=3;++quarter) {
                    Partition candidate;candidate.plane.n[axis]=1;
                    candidate.plane.d=-centers[quarter*centers.size()/4];
                    candidate.plane.epsilon=64*std::numeric_limits<double>::epsilon()*scale;
                    size_t worst=0;const uint64_t cost=measure(candidate,&worst);
                    if(worst<=nonadditive*7/8 && cost<spatialCost) {
                        spatialCost=cost;best=candidate;
                    }
                }
            }
        }
        return best;
    }
};

} // namespace

bool NaomiFlashOutsideProjection(const NaomiFlashTriangle& triangle,const float projection[16],float guardX,float guardY) {
    if(!projection || !std::isfinite(guardX) || !std::isfinite(guardY) || guardX<0 || guardY<0)return false;
    for(int k=0;k<16;++k)if(!std::isfinite(projection[k]))return false;
    for(const auto& v:triangle.vertices)for(float x:v.pos)if(!std::isfinite(x))return false;
    for(int plane=0;plane<6;++plane) {
        double coefficients[4];
        for(int row=0;row<4;++row) {
            if(plane<4)coefficients[row]=double(projection[row*4+3])*(1+(plane<2?double(guardX):double(guardY)))+
                (plane%2?-1:1)*double(projection[row*4+plane/2]);
            else if(plane==4)coefficients[row]=projection[row*4+2];
            else coefficients[row]=double(projection[row*4+3])-projection[row*4+2];
        }
        bool outside=true;
        for(const auto& v:triangle.vertices) {
            double distance=coefficients[3],magnitude=std::abs(coefficients[3]);
            for(int k=0;k<3;++k){const double term=coefficients[k]*v.pos[k];distance+=term;magnitude+=std::abs(term);}
            // Enclose float dot/FMA evaluation and denormal flushing. A point
            // on the boundary remains eligible even if the CPU/GPU associates
            // projection arithmetic differently.
            const double roundoff=16*std::numeric_limits<float>::epsilon()*magnitude+16*std::numeric_limits<float>::min();
            if(!(distance< -roundoff)){outside=false;break;}
        }
        if(outside)return true;
    }
    return false;
}

bool NaomiFlashCulledByWinding(const NaomiFlashTriangle& triangle,const float projection[16],uint8_t cull,float pixelX,float pixelY) {
    if((cull!=2 && cull!=3) || !projection || !std::isfinite(pixelX) || !std::isfinite(pixelY) || pixelX<0 || pixelY<0)return false;
    for(int k=0;k<16;++k)if(!std::isfinite(projection[k]))return false;
    double xy[3][2],error[3][2];
    for(int i=0;i<3;++i) {
        const auto& v=triangle.vertices[i];for(float x:v.pos)if(!std::isfinite(x))return false;
        double clip[3],epsilon[3];
        for(int axis=0;axis<3;++axis) {
            const int column=axis==2?3:axis;
            double value=projection[12+column],magnitude=std::abs(value);
            for(int k=0;k<3;++k){const double term=double(v.pos[k])*projection[k*4+column];value+=term;magnitude+=std::abs(term);}
            clip[axis]=value;epsilon[axis]=16*std::numeric_limits<float>::epsilon()*magnitude+16*std::numeric_limits<float>::min();
        }
        if(!(clip[2]>epsilon[2]))return false;
        for(int axis=0;axis<2;++axis) {
            xy[i][axis]=clip[axis]/clip[2];
            error[i][axis]=(epsilon[axis]+std::abs(xy[i][axis])*epsilon[2])/(clip[2]-epsilon[2]);
            // Conservatively enclose at least a 1/16-pixel raster-coordinate
            // grid, in addition to the projected float arithmetic error.
            error[i][axis]+=(axis==0?double(pixelX):double(pixelY))/16;
        }
    }
    const double ux=xy[1][0]-xy[0][0],uy=xy[1][1]-xy[0][1];
    const double vx=xy[2][0]-xy[0][0],vy=xy[2][1]-xy[0][1];
    const double eux=error[1][0]+error[0][0],euy=error[1][1]+error[0][1];
    const double evx=error[2][0]+error[0][0],evy=error[2][1]+error[0][1];
    const double determinant=ux*vy-uy*vx;
    const double uncertainty=std::abs(ux)*evy+std::abs(vy)*eux+eux*evy+
        std::abs(uy)*evx+std::abs(vx)*euy+euy*evx+
        64*std::numeric_limits<double>::epsilon()*(std::abs(ux*vy)+std::abs(uy*vx));
    // The viewport flips NDC Y. Negative NDC area is clockwise on screen.
    return cull==2?determinant< -uncertainty:determinant>uncertainty;
}

bool SortNaomiFlashTriangles(const std::vector<NaomiFlashTriangle>& input,
                            std::vector<NaomiFlashTriangle>* output,std::string* error,
                            const NaomiFlashSortLimits& limits) {
    auto fail=[&](const char* message){if(error)*error=message;return false;};
    if(!output)return fail("Naomi flash BSP output is null");
    if(input.size()>limits.maxInputTriangles || input.size()>limits.maxFragments)
        return fail("Naomi flash BSP input budget exceeded");
    std::vector<Polygon> polygons;polygons.reserve(input.size());
    for(size_t i=0;i<input.size();++i) {
        const auto& t=input[i];
        for(const auto& v:t.vertices)if(!Finite(v))return fail("Naomi flash BSP input is nonfinite");
        Polygon p;p.vertices.assign(t.vertices,t.vertices+3);p.source=t.sourceDraw;p.sequence=t.sequence;p.original=i;p.additive=t.additive;
        if(MakePlane(p,&p.sourcePlane))polygons.push_back(std::move(p));
    }
    Sorter sorter(limits);sorter.fragments=polygons.size();std::unique_ptr<Node> root;
    if(!sorter.Build(std::move(polygons),0,root))return fail(sorter.error);
    std::vector<NaomiFlashTriangle> result;
    if(!sorter.Emit(root.get(),result))return fail(sorter.error);
    *output=std::move(result);if(error)error->clear();return true;
}

} // namespace port
