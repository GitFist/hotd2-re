#include "port/play/menu_game_switch.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {
void Require(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
}
int main() {
    game::WorldHud hud;hud.nightShiftPage=0;
    auto logos=play::MenuGameLogos(hud);
    Require(logos.size()==1 && logos[0].id==0x4FD4,"HOTD2 Home switches through the clean Typing wordmark");
    Require(logos[0].x>=374 && logos[0].y>=108 && logos[0].x+logos[0].sx*512<=604.01f &&
            logos[0].y+logos[0].sy*320<=210.01f,"Typing button logo fits its hit rectangle with natural aspect");
    hud.typingHome=true;logos=play::MenuGameLogos(hud);
    Require(logos.size()==6 && logos[0].id==0x4FD4 && logos[0].x==22 && logos[0].y==14,
            "Typing Home has its top-left wordmark and a House of the Dead2 switch-back logo");
    for(int i=1;i<=5;++i)Require(logos[i].id==0xAB0+i,"switch-back consists only of the original House wordmark strips");
    const float white[]={1,1,1};
    game::TLVertex houseQuad[4];
    const float near=.001f,zScale=8000.8f/(8000.8f-near);
    game::SpriteQuad(game::MakeSpriteRecord(logos[1],0,white),128,512,zScale,-near*zScale,houseQuad);
    for(const auto& vertex:houseQuad)Require(vertex.z>=0&&vertex.z<=1,
        "House switch-back logo survives the native sprite near-plane projection");
    hud.nightShiftPage=11;logos=play::MenuGameLogos(hud);
    Require(logos.size()==1 && logos[0].id==0x4FD4,"Original popup retains the Typing heading without adding a hidden switch target");
    hud.nightShiftPage=12;Require(play::MenuGameLogos(hud).empty(),"Drill retains its own clear heading");
    hud.nightShiftPage=11;
    hud.menuPanels={{0,0,640,480,0x3503090Bu},{0,0,640,480,0xA8000000u},{84,84,556,416,0xF20C1114u}};
    hud.menuText={{38,170,"ORIGINAL",1,0xFFFFFFFFu},{104,107,"ORIGINAL MODE",.9f,0xFFFFFFFFu},{104,170,"DIFFICULTY",.6f,0xFFFFFFFFu}};
    game::WorldHud parent,prompt;
    Require(play::SplitOriginalPrompt(hud,parent,prompt) && parent.nightShiftPage==0 && parent.menuPanels.size()==1 &&
            parent.menuText.size()==1 && prompt.menuPanels.size()==2 && prompt.menuText.size()==2,
            "prompt composition separates its veil, window and text from parent logos/sidebar");
    std::vector<port::ModelRenderer::SpriteDraw> draws(1);
    draws[0].depth=.00001f;hud.menuSwitching=true;hud.menuSwitchFade=.5f;
    play::AppendMenuGameFade(hud,draws);
    Require(draws.size()==2 && draws.back().depth<draws.front().depth && !draws.back().texture,
            "transition covers every prior sprite including cursor and status overlays");
    const auto& fade=draws.back();
    Require(fade.v[0].x==0 && fade.v[0].y==0 && fade.v[3].x==640 && fade.v[3].y==480,
            "fade spans the entire image without centered wide-screen margins");
    for(const auto& v:fade.v)Require(v.diffuse==0x80000000u,"fade uses black with independently controlled opacity");
    hud.menuSwitchFade=1;draws.clear();play::AppendMenuGameFade(hud,draws);
    for(const auto& v:draws.back().v)Require(v.diffuse==0xFF000000u,"midpoint is completely black");
    hud.menuSwitchFade=std::numeric_limits<float>::quiet_NaN();play::AppendMenuGameFade(hud,draws);
    Require(draws.size()==1,"invalid fade does not generate invalid vertices");
    hud.menuSwitching=false;play::AppendMenuGameFade(hud,draws);Require(draws.size()==1,"ordinary menus have no fade overlay");
    std::puts("menu_game_switch_test: OK");
}
