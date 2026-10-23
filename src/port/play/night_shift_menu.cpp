#include "play/night_shift_menu.h"
#include "render/viewport.h"

#include <algorithm>

namespace play {
void AppendNightShiftPanels(const game::WorldHud& hud,std::vector<port::ModelRenderer::SpriteDraw>& out,float aspect,int opacity) {
    if(hud.nightShiftPage<0)return;
    size_t index=0;
    for(const auto& p:hud.menuPanels) {
        port::ModelRenderer::SpriteDraw s{};
        s.depth=.1f-float(index++)*.0001f;
        s.key=0x8000000F; s.isp=0xE4000000; s.tsp=0x94918000;
        const float x[]={p.left,p.left,p.right,p.right};
        const float y[]={p.top,p.bottom,p.top,p.bottom};
        const uint32_t alpha=((p.color>>24)*std::clamp(opacity,25,100)+50)/100;
        const uint32_t color=(p.color&0x00ffffff)|(alpha<<24);
        for(int i=0;i<4;++i)s.v[i]={x[i],y[i],0,1,color,0,float(i>=2),float(i&1)};
        if(!(p.left<=0&&p.right>=640))for(auto& v:s.v)v.x=port::CenteredViewportX(v.x,aspect);
        out.push_back(s);
    }
}

bool AttractPreview::Step(const std::string& gameDir,bool enabled,int ticks) {
    ++soundRevision_;
    sounds_.clear();
    warmupSounds_.clear();
    enabled_=enabled;
    if(!enabled||failed_)return !failed_;
    if(!world_) {
        auto next=std::make_unique<game::World>();
        if(!next->Load(gameDir,0,&error_)){failed_=true;return false;}
        game::WorldConfig c;
        c.savePath.clear(); c.saveDir=gameDir; c.applySaveSettings=false;
        c.portFeatures=false; c.portModernMenu=false; c.portNetworkRoom=false;
        next->Boot(c);
        next->ForceMode(3,0x10000,0,0);
        world_=std::move(next);
        // Advance past title resource setup so the first menu frame has scenery.
        // This changes only this preview World's RNG, task serials and frame counters.
        for(int i=0;i<90;++i) {
            world_->Step();
            const auto& events=world_->Sounds();
            warmupSounds_.insert(warmupSounds_.end(),events.begin(),events.end());
        }
    }
    for(int i=0;i<std::clamp(ticks,0,8);++i) {
        // Keep the original title -> tutorial -> demo -> story -> title sequence.
        // Its own front end advances / wraps the demo index without menu input.
        world_->Step();
        const auto& events=world_->Sounds();
        sounds_.insert(sounds_.end(),events.begin(),events.end());
    }
    return true;
}
} // namespace play
