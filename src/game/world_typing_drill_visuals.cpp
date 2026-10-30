#include "game/world_typing_drill_visuals.h"
#include "game/world_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace game {
namespace {

// Original data tables at 0x00589214 (icons), 0x0058924C (labels),
// and 0x005894D8 (description sprite, x, y). The native controller's
// category order differs from this authored menu order.
struct Entry {
    int exercise, icon, label, description;
    float descriptionY;
};
constexpr std::array<Entry,14> kEntries = {{
    {0, 0xEB2, 0x278, 0x269, 372},
    {1, 0xEB3, 0x279, 0x269, 372},
    {2, 0xEB4, 0x27A, 0x269, 372},
    {6, 0xEAF, 0x26D, 0x267, 372},
    {7, 0xEB0, 0x26E, 0x267, 372},
    {8, 0xEB1, 0x26F, 0x267, 372},
    {3, 0xEAA, 0x272, 0x265, 372},
    {4, 0xEAB, 0x273, 0x265, 372},
    {5, 0xEAC, 0x274, 0x265, 372},
    {9, 0xEB6, 0x275, 0x26A, 372},
    {10,0xEB7, 0x276, 0x26A, 372},
    {11,0xEB8, 0x277, 0x26A, 372},
    {12,0xEB5, 0x270, 0x268, 360},
    {13,0xEAE, 0x271, 0x266, 372},
}};

void Sprite(World::Impl& w, int source, float x, float y, float depth,
            uint32_t flags=0, float scale=1, float brightness=1) {
    SpriteDef d;
    d.id=0x4000+source;
    d.x=x; d.y=y; d.depth=depth;
    d.sx=d.sy=scale;
    d.flags=flags;
    d.source=0x004A7CD0;
    if(flags&0x2000) {
        d.tinted=true;
        d.tint[0]=d.tint[1]=d.tint[2]=brightness;
    }
    w.hudSprites.push_back(d);
}

float WrappedOffset(float offset, int count) {
    const float span=210.f*count;
    while(offset < -420.f) offset+=span;
    while(offset > 210.f*(count-2)) offset-=span;
    return offset;
}

void Agent(World::Impl& w, int age) {
    // Source 0x004A57EE / 0x00409560. The offline importer verifies that
    // skeleton 0x39 and its attachment deformation data match the native
    // hierarchy. Source trn.bin motions 0x343 / 0x345 are byte-identical to
    // native 0x31A / 0x31C; its model bindings retain the original PC artwork.
    EnemyTask agent;
    agent.fn=0x00409560;
    agent.anim.kind=0x39;
    agent.anim.drawFlags=1;
    agent.anim.rotOrder=1;
    agent.nodeCb=0x00411050;
    agent.framed=true;
    agent.frame=w.cam.inverse;
    agent.frameMv=Mat4::Identity();
    agent.pos[0]=-10; agent.pos[1]=-22; agent.pos[2]=-27;
    agent.rot[2]=0xFD80; agent.rot[1]=0x8E88;
    const Skeleton& skeleton=w.EnemySkeleton(0x39);
    auto resident=[&](int model) {
        if(model>0 && size_t(model)<w.modelLoaded.size() && !w.modelLoaded[size_t(model)])
            w.ProcessRequest({0x50,uint32_t(model)});
    };
    for(const auto& node:skeleton.nodes) {
        if(node.index<0 || node.index>=int(agent.anim.nodes.size())) continue;
        auto& slot=agent.anim.nodes[size_t(node.index)];
        slot.model=int(node.modelId);
        slot.used=true;
        resident(w.typingReference.NativeEnemyModel(0x39,node.index,slot.model));
    }
    // Source init replaces node5's hand with the pointing-hand mesh.
    agent.anim.nodes[5].model=w.typingReference.SourceModel(0x1619);
    resident(agent.anim.nodes[5].model);
    const uint32_t attachment=w.exe.U32(0x0052ED08+0x39*4);
    if(w.exe.InData(attachment)) {
        const uint32_t definitions=w.exe.U32(attachment+4);
        for(uint32_t i=0;i<w.exe.U32(attachment) && i<32;++i) {
            const uint32_t definition=w.exe.U32(definitions+4*i);
            if(definition && w.exe.InData(definition))
                resident(w.typingReference.NativeEnemyModel(0x39,-1,int(w.exe.U32(definition))));
        }
    }
    w.AnimSetMotion(agent,age<50?0x31A:0x31C);
    agent.anim.counter[0]=age<50?age:std::max(0,age-52);
    if(age>=50 && age<=52) {
        // The source switches at intro tick49, with a two-tick blend request
        // (three interpolation steps in the existing native animation system).
        const int slots=w.exe.S16(0x004E0724+0x39*2);
        const Motion* intro=w.EnemyMotion(0x31A,slots);
        if(intro && intro->SizeConsistent()) {
            float next[3];
            intro->RootPosition(24,agent.anim.root);
            intro->RootPosition(25,next);
            for(int k=0;k<3;++k) agent.anim.root[k]+=(next[k]-agent.anim.root[k])*.5f;
            for(int slot=0;slot<slots && slot<int(agent.anim.nodes.size());++slot) {
                int16_t from[3],to[3];
                intro->Rotation(24,slot,from); intro->Rotation(25,slot,to);
                for(int k=0;k<3;++k) {
                    const int delta=int16_t(uint16_t(to[k]-from[k]));
                    agent.anim.nodes[size_t(slot)].cur[k]=from[k]+delta/2;
                }
            }
            agent.anim.counter[0]=50;
            w.AnimBlendTo(agent,0x31C,0,2);
            agent.anim.counter[0]=age;
        }
    }
    const size_t first=w.draws.size();
    w.AnimDraw(agent);
    for(size_t i=first;i<w.draws.size();++i) w.draws[i].fn=0x00409560;
}

} // namespace

bool TypingDrillSelectionVisuals(World::Impl& w) {
    if(!w.typingReference.Available() || !w.typingReference.SourceSprite(0x4EC2)) return false;
    auto& ui=w.typingDrill;
    std::vector<const Entry*> available;
    for(const auto& entry:kEntries)
        if(entry.exercise==13 || ui.controller.unlocked(entry.exercise)) available.push_back(&entry);
    auto position=[&](int exercise) {
        for(size_t i=0;i<available.size();++i)
            if(available[i]->exercise==exercise) return int(i);
        return -1;
    };
    int selected=position(ui.selected);
    if(selected<0) selected=0;
    const Entry& entry=*available[selected];
    const uint64_t frame=uint64_t(std::max(0,w.frame));
    if(ui.visualSelected<0) {
        ui.visualSelected=entry.exercise;
        ui.visualPrevious=entry.exercise;
        ui.visualChangedFrame=frame;
    } else if(ui.visualSelected!=entry.exercise) {
        ui.visualPrevious=ui.visualSelected;
        ui.visualSelected=entry.exercise;
        ui.visualChangedFrame=frame;
        ui.visualDescription=true;
    }

    // Background helper 0x0049FEF0: five columns of 128-pixel tiles.
    for(int i=0;i<20;++i) Sprite(w,0xE83+i,float(i%5)*128,float(i/5)*128,200);
    Sprite(w,0xEC2,320,54,1,0xA);

    const int count=int(available.size());
    const int previous=position(ui.visualPrevious);
    const float phase=std::min(1.f,float(frame-ui.visualChangedFrame+1)/10.f);
    int direction=selected-previous;
    if(direction>count/2) direction-=count;
    if(direction<-(count/2)) direction+=count;
    const bool sliding=previous>=0 && previous!=selected && phase<1;
    for(int i=0;i<count;++i) {
        float offset=WrappedOffset(210.f*(i-selected),count);
        float brightness=i==selected?1.f:.3f;
        if(sliding) {
            offset=WrappedOffset(WrappedOffset(210.f*(i-previous),count)-210.f*direction*phase,count);
            if(i==previous) brightness=1.f-.7f*phase;
            else if(i==selected) brightness=.3f+.7f*phase;
        }
        // Item helper 0x004A80A0 clips the right-hand cards before submission.
        if(offset+216>740) continue;
        Sprite(w,0xEA9,216+offset,90,100);
        Sprite(w,available[i]->icon,192+offset,131,90,0x2000,1,brightness);
    }

    const float pulse=1.f+.05f*std::sin(float(frame%60)*6.2831853071795864769f/60.f);
    Sprite(w,0xEA8,322,194,32.1f,0x200A,pulse);
    // 0x004A7EB4 sets the original target's light to (1, 1, .2).
    w.hudSprites.back().tint[2]=.2f;
    Sprite(w,entry.label,260,84,65);

    // Original arrows: fully visible for eight ticks, then fade until tick45.
    const int tick=int(frame%60);
    if(tick<45) {
        const float alpha=tick<8?1.f:float(45-tick)/45.f;
        Sprite(w,0xEBA,100,136,2);
        w.hudSprites.back().alpha=alpha;
        Sprite(w,0xEBA,542,136,2,0x17);
        w.hudSprites.back().alpha=alpha;
    }

    // Source 0x004A8140 shows the general introduction for three seconds,
    // then the selected drill's description. Both balloons use original art.
    if(!ui.visualDescription&&ui.visualAge<=180) {
        Sprite(w,0x264,206,392,.98f,9);
        Sprite(w,0xEBF,144,432,1,9);
    } else {
        Sprite(w,entry.description,210,entry.descriptionY,30.98f,9);
        Sprite(w,0xEBE,144,365,31,9);
    }
    Agent(w,ui.visualDescription?int(frame-ui.visualChangedFrame):ui.visualAge);
    return true;
}

} // namespace game
