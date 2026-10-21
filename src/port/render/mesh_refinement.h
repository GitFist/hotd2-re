#pragma once
#include "game/model.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace port {
namespace mesh_refinement_detail {
using V=std::array<float,3>;
inline V Add(V a,V b){for(int k=0;k<3;++k)a[k]+=b[k];return a;}
inline V Scale(V a,float s){for(float& v:a)v*=s;return a;}
inline V Sub(V a,V b){return Add(a,Scale(b,-1));}
inline float Dot(V a,V b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline bool Unit(V* a){const float length=std::sqrt(Dot(*a,*a));if(!std::isfinite(length)||length<1e-8f)return false;*a=Scale(*a,1/length);return true;}
inline V Position(const game::ModelVertex& v){return {v.pos[0],v.pos[1],v.pos[2]};}
inline V Normal(const game::ModelVertex& v){return {v.normal[0],v.normal[1],v.normal[2]};}
using PointKey=std::array<uint32_t,3>;
inline PointKey Key(const game::ModelVertex& v){PointKey key{};for(int k=0;k<3;++k){float p=v.pos[k]==0?0:v.pos[k];std::memcpy(&key[k],&p,4);}return key;}
using EdgeKey=std::pair<PointKey,PointKey>;
struct Edge {game::ModelVertex a{},b{};V na{},nb{},ca{},cb{};int count=0;bool compatible=true;};
inline V LimitControl(V control,V linear,float maximum){V d=Sub(control,linear);const float length=std::sqrt(Dot(d,d));return length>maximum&&length>0?Add(linear,Scale(d,maximum/length)):control;}
}

// Cubic point-normal triangles on explicitly selected visual models only.
// Shared smooth edges use ONE canonical pair of control points. UV seams,
// hard normals, open boundaries and nonmanifold edges use straight controls,
// so independent material/cull runs cannot pull apart. Called after CPU skin,
// wave and model-edit deformation; gameplay geometry is never written.
inline bool RefinePnTriangles(const game::ModelVertex* source,size_t count,int quality,std::vector<game::ModelVertex>* output) {
    using namespace mesh_refinement_detail;
    if(!source||!output||!count||count%3||quality<1||quality>3)return false;
    const size_t divisions=static_cast<size_t>(quality+1),factor=divisions*divisions;
    if(count>2u*1024u*1024u/factor)return false; // bounded CPU/GPU expansion, no partial topology
    std::map<EdgeKey,Edge> edges;
    for(size_t k=0;k<count;++k){const auto& v=source[k];V n=Normal(v);if(!Unit(&n))return false;
        for(float x:v.pos)if(!std::isfinite(x))return false;
        for(float x:v.uv)if(!std::isfinite(x))return false;}
    auto edgeKey=[&](size_t a,size_t b){PointKey x=Key(source[a]),y=Key(source[b]);return x<y?EdgeKey{x,y}:EdgeKey{y,x};};
    for(size_t t=0;t<count;t+=3)for(int e=0;e<3;++e){size_t a=t+e,b=t+(e+1)%3;
        auto key=edgeKey(a,b);if(Key(source[a])!=key.first)std::swap(a,b);auto& edge=edges[key];
        V na=Normal(source[a]),nb=Normal(source[b]);Unit(&na);Unit(&nb);
        if(!edge.count){edge.a=source[a];edge.b=source[b];edge.na=na;edge.nb=nb;}
        else {V firstA=Normal(edge.a),firstB=Normal(edge.b);Unit(&firstA);Unit(&firstB);
            // Only tolerate float normalization noise. Even a deliberately
            // authored small normal discontinuity is a hard visual seam.
            if(Dot(firstA,na)<.99999f||Dot(firstB,nb)<.99999f)edge.compatible=false;
            for(int uv=0;uv<2;++uv)if(std::abs(edge.a.uv[uv]-source[a].uv[uv])>1e-5f||std::abs(edge.b.uv[uv]-source[b].uv[uv])>1e-5f)edge.compatible=false;
            edge.na=Add(edge.na,na);edge.nb=Add(edge.nb,nb);}
        ++edge.count;
    }
    for(auto& entry:edges){auto& e=entry.second;const V a=Position(e.a),b=Position(e.b),ab=Sub(b,a);const float length=std::sqrt(Dot(ab,ab));
        const V linearA=Scale(Add(Scale(a,2),b),1.f/3),linearB=Scale(Add(a,Scale(b,2)),1.f/3);
        e.ca=linearA;e.cb=linearB;
        if(e.count==2&&e.compatible&&length>1e-8f&&Unit(&e.na)&&Unit(&e.nb)){
            e.ca=LimitControl(Sub(linearA,Scale(e.na,Dot(ab,e.na)/3)),linearA,length*.15f);
            e.cb=LimitControl(Add(linearB,Scale(e.nb,Dot(ab,e.nb)/3)),linearB,length*.15f);}}
    std::vector<game::ModelVertex> refined;refined.reserve(count*factor);
    for(size_t t=0;t<count;t+=3){V p[3]={Position(source[t]),Position(source[t+1]),Position(source[t+2])},control[6];float maxEdge=0;
        for(int e=0;e<3;++e){const auto& edge=edges.at(edgeKey(t+e,t+(e+1)%3));const bool forward=Key(source[t+e])==Key(edge.a);
            control[e*2]=forward?edge.ca:edge.cb;control[e*2+1]=forward?edge.cb:edge.ca;
            const V d=Sub(p[e],p[(e+1)%3]);maxEdge=std::max(maxEdge,std::sqrt(Dot(d,d)));}
        V average{};for(V c:control)average=Add(average,Scale(c,1.f/6));const V center=Scale(Add(Add(p[0],p[1]),p[2]),1.f/3);
        const V interior=LimitControl(Add(average,Scale(Sub(average,center),.5f)),center,maxEdge*.15f);
        auto vertex=[&](size_t i,size_t j){const float u=float(i)/divisions,v=float(j)/divisions,w=1-u-v;
            V value=Add(Add(Scale(p[0],u*u*u),Scale(p[1],v*v*v)),Scale(p[2],w*w*w));
            const float weights[6]={3*u*u*v,3*u*v*v,3*v*v*w,3*v*w*w,3*w*w*u,3*w*u*u};
            for(int k=0;k<6;++k)value=Add(value,Scale(control[k],weights[k]));value=Add(value,Scale(interior,6*u*v*w));
            game::ModelVertex result{};V normal=Add(Add(Scale(Normal(source[t]),u),Scale(Normal(source[t+1]),v)),Scale(Normal(source[t+2]),w));
            if(!Unit(&normal))normal=Normal(source[t]);for(int k=0;k<3;++k){result.pos[k]=value[k];result.normal[k]=normal[k];}
            for(int k=0;k<2;++k)result.uv[k]=source[t].uv[k]*u+source[t+1].uv[k]*v+source[t+2].uv[k]*w;return result;};
        for(size_t i=0;i<divisions;++i)for(size_t j=0;j<divisions-i;++j){refined.push_back(vertex(i,j));refined.push_back(vertex(i+1,j));refined.push_back(vertex(i,j+1));
            if(j+1<divisions-i){refined.push_back(vertex(i+1,j));refined.push_back(vertex(i+1,j+1));refined.push_back(vertex(i,j+1));}}
    }
    *output=std::move(refined);return true;
}
} // namespace port
