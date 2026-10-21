#include "render/shadow_map.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
static void Require(bool b,const char* why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static void Identity(float* matrix){for(int k=0;k<16;++k)matrix[k]=k%5==0?1.f:0.f;}
static float Depth(const port::ShadowConstants& c,float z){return z*c.worldToLightClip[10]+c.worldToLightClip[14];}
int main(){
    float view[16],projection[16];Identity(view);Identity(projection);projection[11]=-1;projection[15]=0;
    const float light[3]={0,0,1},bounds[6]={-20,-20,-100,20,20,-90};port::ShadowMapFrame frame;
    for(int quality=1;quality<=3;++quality){
        Require(port::MakeShadowMapFrame(view,projection,light,bounds,quality,&frame),"finite directional shadow projection");
        Require(frame.valid&&frame.resolution==(1024<<(quality-1))&&frame.constants.parameters[3]==0,"shadow quality/readiness ownership");
        Require(Depth(frame.constants,-90)<Depth(frame.constants,-100),"nearer-to-light occluder must have smaller map depth");
        for(float z:{-100.f,-90.f})Require(Depth(frame.constants,z)>0&&Depth(frame.constants,z)<1,"caster bounds clipped out of light depth");
        for(float value:frame.constants.worldToLightClip)Require(std::isfinite(value),"nonfinite directional map matrix");
    }
    port::ShadowMapFrame first,next;Require(port::MakeShadowMapFrame(view,projection,light,bounds,1,&first),"first map");
    view[12]=.1f;Require(port::MakeShadowMapFrame(view,projection,light,bounds,1,&next),"subtexel camera movement");
    Require(first.constants.worldToLightClip[12]==next.constants.worldToLightClip[12]&&first.constants.worldToLightClip[13]==next.constants.worldToLightClip[13],"subtexel camera movement made shadow projection swim");
    const float vertical[3]={0,1,0};Require(port::MakeShadowMapFrame(view,projection,vertical,bounds,1,&next),"vertical light needs finite alternate up axis");
    const float zero[3]={0,0,0};Require(!port::MakeShadowMapFrame(view,projection,zero,bounds,1,&next),"zero directional light must disable map");
    float empty[6]={1,1,1,-1,-1,-1};Require(!port::MakeShadowMapFrame(view,projection,light,empty,1,&next),"empty caster bounds accepted");
    std::puts("PASS directional depth ordering, quality, texel stability, vertical/zero light handling, finite caster fit and backend readiness");
}
