#include "render_pose.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace port {
namespace {
constexpr size_t kVertexBudget = 1u << 18; // < 24 MiB of pose/copy vertex payload
constexpr float kTeleportDistance = 256;
struct Srt { double scale[3], q[4], position[3]; };

bool Decompose(const game::Mat4& m, Srt* out) {
    for (float v : m.m) if (!std::isfinite(v)) return false;
    if (std::abs(m.m[3])>1e-5f || std::abs(m.m[7])>1e-5f ||
        std::abs(m.m[11])>1e-5f || std::abs(m.m[15]-1)>1e-5f) return false;
    double r[3][3];
    for (int i=0;i<3;++i) {
        double length=0;
        for (int j=0;j<3;++j) length+=double(m.m[i*4+j])*m.m[i*4+j];
        out->scale[i]=std::sqrt(length);
        if (out->scale[i]<1e-7 || out->scale[i]>1e7) return false;
        for (int j=0;j<3;++j) r[i][j]=m.m[i*4+j]/out->scale[i];
        out->position[i]=m.m[12+i];
    }
    // A hierarchy with nonuniform inherited scale can contain shear. Retain its
    // authored current matrix instead of silently deforming it into an SRT.
    for (int i=0;i<3;++i) for (int j=i+1;j<3;++j) {
        double dot=0; for (int k=0;k<3;++k) dot+=r[i][k]*r[j][k];
        if (std::abs(dot)>1e-3) return false;
    }
    const double determinant=r[0][0]*(r[1][1]*r[2][2]-r[1][2]*r[2][1])-
        r[0][1]*(r[1][0]*r[2][2]-r[1][2]*r[2][0])+
        r[0][2]*(r[1][0]*r[2][1]-r[1][1]*r[2][0]);
    if (std::abs(std::abs(determinant)-1)>1e-3) return false;
    if (determinant<0) {
        out->scale[0]=-out->scale[0]; for (double& v:r[0]) v=-v;
    }
    double* q=out->q;
    const double trace=r[0][0]+r[1][1]+r[2][2];
    if (trace>0) {
        const double s=2*std::sqrt(trace+1);
        q[3]=.25*s; q[0]=(r[2][1]-r[1][2])/s;
        q[1]=(r[0][2]-r[2][0])/s; q[2]=(r[1][0]-r[0][1])/s;
    } else {
        const int i=r[0][0]>r[1][1]?(r[0][0]>r[2][2]?0:2):(r[1][1]>r[2][2]?1:2);
        const int j=(i+1)%3,k=(i+2)%3;
        const double s=2*std::sqrt(std::max(0.0,1+r[i][i]-r[j][j]-r[k][k]));
        if (s<1e-10) return false;
        q[i]=.25*s; q[j]=(r[i][j]+r[j][i])/s;
        q[k]=(r[i][k]+r[k][i])/s; q[3]=(r[k][j]-r[j][k])/s;
    }
    double length=0; for (int i=0;i<4;++i) length+=q[i]*q[i];
    if (length<1e-10 || !std::isfinite(length)) return false;
    for (int i=0;i<4;++i) q[i]/=std::sqrt(length);
    return true;
}

double RotationDot(const Srt& a,const Srt& b) {
    double dot=0; for (int i=0;i<4;++i) dot+=a.q[i]*b.q[i];
    return std::abs(dot);
}
bool Nearby(const Srt& a,const Srt& b) {
    double distance=0;
    for (int i=0;i<3;++i) {
        const double delta=a.position[i]-b.position[i]; distance+=delta*delta;
        if (std::signbit(a.scale[i])!=std::signbit(b.scale[i])) return false;
    }
    return distance<=double(kTeleportDistance)*kTeleportDistance;
}

game::Mat4 Blend(const game::Mat4& a,const game::Mat4& b,const Srt& pa,const Srt& pb,float alpha) {
    if (alpha<=0) return a;
    if (alpha>=1 || std::memcmp(a.m,b.m,sizeof(a.m))==0) return b;
    double dot=0; for (int i=0;i<4;++i) dot+=pa.q[i]*pb.q[i];
    const double sign=dot<0?-1:1;
    dot=std::clamp(std::abs(dot),0.0,1.0);
    double wa=1-alpha,wb=alpha;
    if (dot<.9995) {
        const double angle=std::acos(dot), divisor=std::sin(angle);
        wa=std::sin((1-alpha)*angle)/divisor; wb=std::sin(alpha*angle)/divisor;
    }
    double q[4],length=0;
    for (int i=0;i<4;++i) {q[i]=wa*pa.q[i]+sign*wb*pb.q[i];length+=q[i]*q[i];}
    for (double& v:q) v/=std::sqrt(length);
    const double x=q[0],y=q[1],z=q[2],w=q[3];
    const double rotation[3][3]={{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},
        {2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},
        {2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}};
    game::Mat4 out=game::Mat4::Identity();
    for (int i=0;i<3;++i) {
        const double scale=pa.scale[i]+alpha*(pb.scale[i]-pa.scale[i]);
        for (int j=0;j<3;++j) out.m[i*4+j]=static_cast<float>(scale*rotation[i][j]);
        out.m[12+i]=static_cast<float>(pa.position[i]+alpha*(pb.position[i]-pa.position[i]));
    }
    return out;
}

game::Mat4 CameraInverse(const game::Mat4& m) {
    // Camera matrices are rigid, but use the affine inverse so finite inherited
    // scale is also handled safely. Decompose has already rejected degeneracy.
    const double a=m.m[0],b=m.m[1],c=m.m[2],d=m.m[4],e=m.m[5],f=m.m[6],g=m.m[8],h=m.m[9],i=m.m[10];
    const double det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    const double inverse[3][3]={{(e*i-f*h)/det,(c*h-b*i)/det,(b*f-c*e)/det},
        {(f*g-d*i)/det,(a*i-c*g)/det,(c*d-a*f)/det},
        {(d*h-e*g)/det,(b*g-a*h)/det,(a*e-b*d)/det}};
    game::Mat4 out=game::Mat4::Identity();
    for (int row=0;row<3;++row) for(int col=0;col<3;++col) out.m[row*4+col]=static_cast<float>(inverse[row][col]);
    for (int col=0;col<3;++col) out.m[12+col]=static_cast<float>(-(m.m[12]*inverse[0][col]+m.m[13]*inverse[1][col]+m.m[14]*inverse[2][col]));
    return out;
}

uint64_t Topology(const game::Model& model,size_t* count) {
    uint64_t hash=1469598103934665603ull;
    auto add=[&](uint64_t value){for(int i=0;i<8;++i){hash^=static_cast<uint8_t>(value);hash*=1099511628211ull;value>>=8;}};
    *count=0; add(model.meshes.size());
    for (const auto& mesh:model.meshes) {
        add(mesh.strips.size());
        for (const auto& strip:mesh.strips) {
            add(strip.ctrl); add(strip.triangleList); add(strip.vertices.size());add(strip.offsets.size());
            for (uint32_t offset:strip.offsets) add(offset);
            if(strip.vertices.size()>kVertexBudget-*count)return 0;
            *count+=strip.vertices.size();
        }
    }
    return hash;
}
MotionIdentity Group(MotionIdentity key) {key.occurrence=0;return key;}
bool SameScene(const TemporalSceneStamp& a,const TemporalSceneStamp& b) {return a.world==b.world&&a.camera==b.camera;}
} // namespace

void RenderPoseHistory::Set(bool enabled,float alpha) {
    if (enabled_!=enabled) Reset();
    enabled_=enabled;
    alpha_=std::isfinite(alpha)?std::clamp(alpha,0.f,1.f):1.f;
}
void RenderPoseHistory::Reset() {
    previous_=Snapshot{};current_=Snapshot{};renderModels_.clear();ready_=false;
}

RenderPoseHistory::Snapshot RenderPoseHistory::Capture(const std::vector<ModelRenderer::SceneItem>& items,
    const TemporalSceneStamp& stamp,const game::Mat4& cameraWorld,const game::Mat4& cameraView) const {
    Snapshot out;out.stamp=stamp;out.cameraWorld=cameraWorld;out.cameraView=cameraView;
    Srt camera;
    if(!Decompose(cameraWorld,&camera))return out;
    out.valid=true;
    size_t budget=kVertexBudget;
    for (const auto& item:items) {
        if(!item.motionTracked || !item.model || (item.motionIdentity.role&1))continue;
        ++out.groupCounts[Group(item.motionIdentity)];
        auto& pose=out.items[item.motionIdentity];std::memcpy(pose.transform.m,item.transform,sizeof(item.transform));
        if(!item.dynamicGeometry || item.motionReactive)continue;
        size_t count=0;pose.topology=Topology(*item.model,&count);
        if(!pose.topology || !count || count>budget){pose.topology=0;continue;}
        pose.vertices.reserve(count);
        for(const auto& mesh:item.model->meshes)for(const auto& strip:mesh.strips)for(const auto& vertex:strip.vertices){
            VertexPose v;std::memcpy(v.position,vertex.pos,sizeof(v.position));std::memcpy(v.normal,vertex.normal,sizeof(v.normal));pose.vertices.push_back(v);
        }
        budget-=count;
    }
    return out;
}

void RenderPoseHistory::Apply(std::vector<ModelRenderer::SceneItem>& items,const TemporalSceneStamp& stamp,
    const game::Mat4& cameraWorld,game::Mat4* renderView,float* renderPosition) {
    if(!enabled_ || !renderView)return;
    renderModels_.clear();
    // A repeated presentation of one tick must never replace either endpoint.
    if(!current_.valid || !SameScene(current_.stamp,stamp) || current_.stamp.simulationFrame!=stamp.simulationFrame) {
        Snapshot next=Capture(items,stamp,cameraWorld,*renderView);
        const bool consecutive=current_.valid && next.valid && SameScene(current_.stamp,stamp) &&
            current_.stamp.simulationFrame!=std::numeric_limits<int64_t>::max() &&
            stamp.simulationFrame==current_.stamp.simulationFrame+1;
        previous_=consecutive?std::move(current_):Snapshot{};
        current_=std::move(next);
    }
    Srt previousCamera,currentCamera;
    ready_=previous_.valid&&current_.valid&&Decompose(previous_.cameraWorld,&previousCamera)&&
        Decompose(current_.cameraWorld,&currentCamera)&&Nearby(previousCamera,currentCamera)&&
        RotationDot(previousCamera,currentCamera)>=.8660254037844386; // >60 degree camera turn is a cut
    if(!ready_ || alpha_>=1)return;
    const game::Mat4 nativeView=*renderView;
    const game::Mat4 renderCamera=Blend(previous_.cameraWorld,current_.cameraWorld,previousCamera,currentCamera,alpha_);
    if(alpha_<=0)*renderView=previous_.cameraView;
    else if(std::memcmp(previous_.cameraWorld.m,current_.cameraWorld.m,sizeof(cameraWorld.m))!=0)
        *renderView=CameraInverse(renderCamera);
    if(renderPosition)for(int k=0;k<3;++k)renderPosition[k]=renderCamera.m[12+k];
    for(auto& item:items) {
        if(!item.motionTracked || !item.model)continue;
        if(item.motionIdentity.role&1) {
            if(std::memcmp(renderCamera.m,cameraWorld.m,sizeof(cameraWorld.m))==0)continue;
            // A gun/overlay supplied in view space must remain immediate even
            // while the world camera is interpolated behind it.
            game::Mat4 world;std::memcpy(world.m,item.transform,sizeof(item.transform));
            const auto adjusted=world*nativeView*renderCamera;
            std::memcpy(item.transform,adjusted.m,sizeof(item.transform));continue;
        }
        const auto before=previous_.items.find(item.motionIdentity),after=current_.items.find(item.motionIdentity);
        if(before==previous_.items.end() || after==current_.items.end())continue;
        const auto group=Group(item.motionIdentity);
        const auto count=previous_.groupCounts.find(group),newCount=current_.groupCounts.find(group);
        if(count==previous_.groupCounts.end()||newCount==current_.groupCounts.end()||count->second!=newCount->second)continue;
        const auto& a=before->second;const auto& b=after->second;
        Srt pa,pb;if(!Decompose(a.transform,&pa)||!Decompose(b.transform,&pb)||!Nearby(pa,pb))continue;
        const auto transform=Blend(a.transform,b.transform,pa,pb,alpha_);std::memcpy(item.transform,transform.m,sizeof(item.transform));
        if(!item.dynamicGeometry||item.motionReactive||!a.topology||a.topology!=b.topology||a.vertices.size()!=b.vertices.size())continue;
        size_t countVertices=0;if(Topology(*item.model,&countVertices)!=b.topology||countVertices!=b.vertices.size())continue;
        auto model=std::make_unique<game::Model>(*item.model);size_t n=0;bool finite=true;
        for(auto& mesh:model->meshes)for(auto& strip:mesh.strips)for(auto& vertex:strip.vertices){
            const auto& va=a.vertices[n];const auto& vb=b.vertices[n++];
            double normal[3],la=0,lb=0,length=0;
            for(int k=0;k<3;++k){
                vertex.pos[k]=alpha_<=0?va.position[k]:va.position[k]+alpha_*(vb.position[k]-va.position[k]);
                la+=double(va.normal[k])*va.normal[k];lb+=double(vb.normal[k])*vb.normal[k];
                finite=finite&&std::isfinite(vertex.pos[k])&&std::isfinite(va.normal[k])&&std::isfinite(vb.normal[k]);
            }
            // Some original specular normals intentionally are not unit length.
            // Preserve their interpolated magnitude, with current normal at an
            // antipodal/zero direction rather than producing NaNs.
            const double magnitudeA=std::sqrt(la),magnitudeB=std::sqrt(lb);
            for(int k=0;k<3;++k){
                normal[k]=(magnitudeA>1e-8?va.normal[k]/magnitudeA:0)*(1-alpha_)+
                    (magnitudeB>1e-8?vb.normal[k]/magnitudeB:0)*alpha_;
                length+=normal[k]*normal[k];
            }
            if(alpha_<=0)std::memcpy(vertex.normal,va.normal,sizeof(vertex.normal));
            else if(length>1e-12){const double scale=(magnitudeA+alpha_*(magnitudeB-magnitudeA))/std::sqrt(length);
                for(int k=0;k<3;++k)vertex.normal[k]=static_cast<float>(normal[k]*scale);}
        }
        if(!finite)continue;
        item.model=model.get();item.dynamicGeometry=true;
        // Dynamic authored bounds need not contain the previous pose. Avoid
        // clipping an interpolated limb against a current-only mesh sphere.
        item.cullable=false;item.noMeshCull=true;
        renderModels_.push_back(std::move(model));
    }
}

} // namespace port
