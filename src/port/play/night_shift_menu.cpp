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
    enabled_=enabled;
    if(!enabled||failed_)return !failed_;
    if(!world_) {
        auto next=std::make_unique<game::World>();
        if(!next->Load(gameDir,0,&error_)){failed_=true;return false;}
        game::WorldConfig c;
        c.savePath.clear(); c.saveDir=gameDir; c.applySaveSettings=false;
        c.portFeatures=false; c.portModernMenu=false; c.portNetworkRoom=false;
        next->Boot(c);
        next->ForceMode(5,0x10000,0,0);
        world_=std::move(next);
        // Advance past the stage resource setup so the first menu frame has scenery.
        // This changes only this preview World's RNG, task serials and frame counters.
        for(int i=0;i<90;++i)world_->Step();
    }
    for(int i=0;i<std::clamp(ticks,0,8);++i) {
        if(world_->GameMode()!=5) {
            // DemoFrame already advances the demo index before its mode switch. Use
            // that authoritative value; never derive it from the live menu's clock.
            demo_=world_->Hud().demoIndex;
            world_->ForceMode(5,0x10000,0,demo_);
        }
        world_->Step();
    }
    return true;
}
} // namespace play
