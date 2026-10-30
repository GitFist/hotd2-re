#include "play/menu_game_switch.h"
#include <algorithm>
#include <cmath>

namespace play {
namespace {
void TypingLogo(std::vector<game::SpriteDef>& out,float left,float top,float width,float height) {
    const float scale=std::min(width/512.f,height/320.f);
    game::SpriteDef logo;
    logo.id=0x4FD4; logo.x=left+(width-512*scale)*.5f; logo.y=top+(height-320*scale)*.5f;
    logo.sx=logo.sy=scale; logo.depth=.0001f; logo.flags=0x700;
    out.push_back(logo);
}
void HouseLogo(std::vector<game::SpriteDef>& out,float left,float top,float width,float height) {
    // Match visible artwork rather than the differently padded texture canvases:
    // Typing occupies (0,0)..(489,304); House occupies (9,0)..(590,349).
    const float typingScale=std::min(width/512.f,height/320.f);
    const float scale=typingScale*489.f/581.f;
    const float x=left+(width-512*typingScale)*.5f-9*scale;
    const float y=top+(height-320*typingScale)*.5f+(304*typingScale-349*scale)*.5f;
    const auto source=game::TitleLogoSprites();
    for(size_t i=0;i<5 && i<source.size();++i) {
        auto logo=source[i];
        logo.x=x+float(i)*128*scale; logo.y=y;
        logo.sx=logo.sy=scale; logo.depth=.01f; logo.flags|=0x700;
        out.push_back(logo);
    }
}
}
std::vector<game::SpriteDef> MenuGameLogos(const game::WorldHud& hud) {
    std::vector<game::SpriteDef> out;
    if(hud.nightShiftPage<0)return out;
    if(hud.typingHome && (hud.nightShiftPage==0 || hud.nightShiftPage==11))
        TypingLogo(out,22,14,216,135);
    else if(hud.typingHome && hud.nightShiftPage==3)
        TypingLogo(out,22,5,110,69);
    if(hud.nightShiftPage==0) {
        const bool focused=!hud.menuSwitching && hud.nightShiftRow==(hud.typingHome?5:7);
        const float scale=focused?1.05f:1.f;
        const float width=230*scale,height=102*scale;
        const float left=489-width*.5f,top=159-height*.5f;
        if(hud.typingHome) HouseLogo(out,left,top,width,height);
        else TypingLogo(out,left,top,width,height);
    }
    return out;
}
bool SplitOriginalPrompt(const game::WorldHud& hud,game::WorldHud& parent,game::WorldHud& prompt) {
    if(hud.nightShiftPage!=11)return false;
    const auto panel=std::find_if(hud.menuPanels.begin(),hud.menuPanels.end(),[](const auto& p){
        return p.left==0 && p.top==0 && p.right==640 && p.bottom==480 && p.color==0xA8000000u;
    });
    const auto text=std::find_if(hud.menuText.begin(),hud.menuText.end(),[](const auto& t){return t.text=="ORIGINAL MODE";});
    if(panel==hud.menuPanels.end() || text==hud.menuText.end())return false;
    parent.nightShiftPage=0;parent.typingHome=hud.typingHome;parent.nightShiftRow=0;
    parent.menuPanels.assign(hud.menuPanels.begin(),panel);parent.menuText.assign(hud.menuText.begin(),text);
    prompt.nightShiftPage=11;prompt.typingHome=hud.typingHome;prompt.nightShiftRow=hud.nightShiftRow;
    prompt.menuPanels.assign(panel,hud.menuPanels.end());prompt.menuText.assign(text,hud.menuText.end());
    return true;
}
void AppendMenuGameFade(const game::WorldHud& hud,std::vector<port::ModelRenderer::SpriteDraw>& out) {
    if(!hud.menuSwitching || !std::isfinite(hud.menuSwitchFade) || hud.menuSwitchFade<=0)return;
    const uint32_t alpha=uint32_t(std::lround(std::clamp(hud.menuSwitchFade,0.f,1.f)*255));
    port::ModelRenderer::SpriteDraw s{};
    s.depth=.0000001f; s.key=0x8000000F; s.isp=0xE4000000; s.tsp=0x94918000;
    const float x[]={0,0,640,640},y[]={0,480,0,480};
    for(int i=0;i<4;++i)s.v[i]={x[i],y[i],0,1,alpha<<24,0,float(i>=2),float(i&1)};
    out.push_back(s);
}
}
