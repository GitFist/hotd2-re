#include "render/mesh_refinement.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
static void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static std::vector<game::ModelVertex> Octahedron(){
    const float p[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const int triangles[8][3]={{0,2,4},{2,1,4},{1,3,4},{3,0,4},{2,0,5},{1,2,5},{3,1,5},{0,3,5}};
    std::vector<game::ModelVertex> result;for(const auto& tri:triangles)for(int index:tri){game::ModelVertex v{};std::copy(p[index],p[index]+3,v.pos);std::copy(p[index],p[index]+3,v.normal);result.push_back(v);}return result;
}
int main(){
    const auto source=Octahedron(),unchanged=source;
    for(int quality:{1,2,3}){std::vector<game::ModelVertex> refined;Require(port::RefinePnTriangles(source.data(),source.size(),quality,&refined),"refine selected smooth model");
        Require(refined.size()==source.size()*(quality+1)*(quality+1),"subdivision density wrong");float maximum=0;
        for(const auto& v:refined){const float sum=std::abs(v.pos[0])+std::abs(v.pos[1])+std::abs(v.pos[2]);maximum=std::max(maximum,sum);
            for(float x:v.pos)Require(std::isfinite(x)&&std::abs(x)<=1.001f,"nonfinite/unbounded curved geometry");}
        Require(maximum>1.1f&&maximum<1.5f,"smooth shared edges did not produce a bounded curved silhouette");
        // Every repeated tessellated edge position has a counterpart in the
        // adjacent original face; canonical controls avoid face-specific cracks.
        for(size_t face=0;face<8;++face)for(size_t k=face*3*(quality+1)*(quality+1);k<(face+1)*3*(quality+1)*(quality+1);++k){const auto& v=refined[k];
            if(std::abs(v.pos[2])>1e-6f||std::abs(v.pos[0])<.01f||std::abs(v.pos[1])<.01f)continue;
            bool matched=false;for(size_t j=0;j<refined.size();++j){if(j>=face*3*(quality+1)*(quality+1)&&j<(face+1)*3*(quality+1)*(quality+1))continue;
                float d=0;for(int axis=0;axis<3;++axis)d+=std::abs(v.pos[axis]-refined[j].pos[axis]);if(d<1e-6f){matched=true;break;}}
            Require(matched,"smooth shared edge developed a crack");}
    }
    Require(std::memcmp(source.data(),unchanged.data(),source.size()*sizeof(game::ModelVertex))==0,"render refinement mutated source/hit geometry");
    auto seam=source;for(size_t k=12;k<seam.size();++k)seam[k].uv[0]=1;std::vector<game::ModelVertex> refined;
    Require(port::RefinePnTriangles(seam.data(),seam.size(),1,&refined),"refine UV seam fixture");
    bool foundSeamMidpoint=false;for(const auto& v:refined)if(std::abs(v.pos[2])<1e-6f&&std::abs(v.pos[0]-.5f)<1e-6f&&std::abs(v.pos[1]-.5f)<1e-6f)foundSeamMidpoint=true;
    Require(foundSeamMidpoint,"UV seam edge was rounded apart");
    auto slightCrease=source;
    for(int k=0;k<3;++k){const float x=slightCrease[k].normal[0],y=slightCrease[k].normal[1];slightCrease[k].normal[0]=x*std::cos(.05f)-y*std::sin(.05f);slightCrease[k].normal[1]=x*std::sin(.05f)+y*std::cos(.05f);}
    Require(port::RefinePnTriangles(slightCrease.data(),slightCrease.size(),1,&refined),"refine small authored crease fixture");
    bool foundCreaseMidpoint=false;for(const auto& v:refined)if(std::abs(v.pos[2])<1e-6f&&std::abs(v.pos[0]-.5f)<1e-6f&&std::abs(v.pos[1]-.5f)<1e-6f)foundCreaseMidpoint=true;
    Require(foundCreaseMidpoint,"small deliberately authored hard-normal crease was smoothed");
    auto hard=source;for(size_t t=0;t<hard.size();t+=3){auto a=port::mesh_refinement_detail::Position(hard[t]),b=port::mesh_refinement_detail::Position(hard[t+1]),c=port::mesh_refinement_detail::Position(hard[t+2]);
        auto ab=port::mesh_refinement_detail::Sub(b,a),ac=port::mesh_refinement_detail::Sub(c,a);port::mesh_refinement_detail::V normal={ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]};port::mesh_refinement_detail::Unit(&normal);
        for(int v=0;v<3;++v)std::copy(normal.begin(),normal.end(),hard[t+v].normal);}
    Require(port::RefinePnTriangles(hard.data(),hard.size(),2,&refined),"refine hard-normal fixture");for(const auto& v:refined)Require(std::abs(std::abs(v.pos[0])+std::abs(v.pos[1])+std::abs(v.pos[2])-1)<2e-6f,"hard-normal faces rounded across a crease");
    auto invalid=source;invalid[0].pos[0]=std::numeric_limits<float>::quiet_NaN();const auto before=refined;
    Require(!port::RefinePnTriangles(invalid.data(),invalid.size(),1,&refined)&&refined.size()==before.size(),"invalid geometry was partially emitted");
    Require(!port::RefinePnTriangles(source.data(),source.size(),0,&refined),"Off refined geometry");
    Require(!port::RefinePnTriangles(source.data(),3u*1024u*1024u,3,&refined),"expansion budget not bounded");
    std::puts("PASS mesh refinement: curved silhouette, canonical shared edges, UV/hard-normal seams, source identity and bounded validation");
}
