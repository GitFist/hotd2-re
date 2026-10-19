#include "lightguns.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace input {
const char* GunButtonName(uint8_t b) {
    switch(b){case 1:return "LEFT";case 2:return "RIGHT";case 4:return "MIDDLE";case 8:return "BUTTON 4";case 16:return "BUTTON 5";default:return "NONE";}
}
void LightgunProfile::Bind(int action,uint8_t button) {
    if(action<0 || action>=4 || std::find(kGunButtons.begin(),kGunButtons.end(),button)==kGunButtons.end())return;
    auto old=buttons[action];
    for(int i=0;i<4;++i)if(i!=action && buttons[i]==button)buttons[i]=old;
    buttons[action]=button;
}
bool LightgunSettings::Load(const std::string& file) {
    std::ifstream f(file);if(!f)return false;
    LightgunSettings loaded;
    for(std::string line;std::getline(f,line);){
        auto eq=line.find('=');if(eq==std::string::npos)continue;
        auto key=line.substr(0,eq);std::istringstream v(line.substr(eq+1));int n=0;
        if(key=="manual" && v>>n)loaded.manual=n==1;
        for(int p=0;p<2;++p){
            auto prefix="p"+std::to_string(p+1)+".";auto& s=loaded.player[p];
            if(key==prefix+"device"){
                std::string id;v>>id;
                if(id.size()==16 && id.find_first_not_of("0123456789abcdef")==std::string::npos)s.device=id;
            }
            if(key==prefix+"buttons"){
                auto b=s.buttons;bool ok=true;
                for(auto& x:b){if(!(v>>n)||std::find(kGunButtons.begin(),kGunButtons.end(),n)==kGunButtons.end())ok=false;x=uint8_t(n);}
                for(int i=0;i<4;++i)for(int j=0;j<i;++j)if(b[i]==b[j])ok=false;
                if(ok)s.buttons=b;
            }
            if(key==prefix+"calibration"){
                AimCalibration c;bool ok=true;
                for(auto& x:c.h)if(!(v>>x)||!std::isfinite(x)||std::abs(x)>1000)ok=false;
                auto& h=c.h;double det=h[0]*(h[4]*h[8]-h[5]*h[7])-h[1]*(h[3]*h[8]-h[5]*h[6])+h[2]*(h[3]*h[7]-h[4]*h[6]);
                if(ok&&std::abs(det)>1e-8){c.calibrated=true;s.calibration=c;}
            }
        }
    }
    if(!loaded.player[0].device.empty()&&loaded.player[0].device==loaded.player[1].device)loaded.player[1]={};
    *this=loaded;return true;
}
bool LightgunSettings::Save(const std::string& file) const {
    std::ofstream f(file,std::ios::trunc);if(!f)return false;
    f<<"manual="<<manual<<'\n'<<std::setprecision(12);
    for(int p=0;p<2;++p){auto prefix="p"+std::to_string(p+1)+".";const auto& s=player[p];
        f<<prefix<<"device="<<s.device<<'\n'<<prefix<<"buttons=";for(auto b:s.buttons)f<<int(b)<<' ';f<<'\n';
        if(s.calibration.calibrated){f<<prefix<<"calibration=";for(auto x:s.calibration.h)f<<x<<' ';f<<'\n';}
    }
    f.flush();return bool(f);
}
void Lightguns::Feed(const std::string& id,float x,float y,bool inside,bool relative,uint8_t down,uint8_t up){
    if(id.empty()||!std::isfinite(x)||!std::isfinite(y))return;
    auto& r=devices_[id];const bool reconnect=!r.connected;
    r.connected=true;r.aim={x,y};r.onScreen=inside;r.relative=relative;r.sequence=++sequence_;
    down&=31;up&=31;
    uint8_t edge=down&~r.held;
    r.pressed|=edge;r.released|=up&r.held;r.held=(r.held|down)&~up;r.suppressed&=~up;
    // An explicitly assigned disconnected gun must not fire on the first reconnect report.
    if(reconnect && settings.manual && (id==settings.player[0].device||id==settings.player[1].device)){r.pressed=0;r.suppressed=r.held;}
    if(assigning_>=0 && r.sequence>assignAfter_ && edge){
        if(!settings.manual){settings.player={};settings.manual=true;}
        const int p=assigning_,other=1-p;
        if(settings.player[other].device==id)std::swap(settings.player[p],settings.player[other]);
        else if(settings.player[p].device!=id){settings.player[p]={};settings.player[p].device=id;}
        for(auto button:kGunButtons)if(edge&button){settings.player[p].Bind(0,button);break;}
        assigning_=-1;assigned_=true;ReleaseButtons();return;
    }
    if(!settings.manual && assigning_<0 && (edge&1)){
        if(settings.player[0].device!=id && settings.player[1].device!=id)
            for(auto& p:settings.player)if(p.device.empty()){p.device=id;break;}
    }
}
void Lightguns::RemoveDevice(const std::string& id){devices_.erase(id);}
void Lightguns::ClearLive(){devices_.clear();CancelAssign();assigned_=false;}
void Lightguns::SetImage(float x,float y,float w,float h){if(w>0&&h>0)image_={x,y,w,h};}
GunReading Lightguns::Reading(int p)const{
    if(p<0||p>=2)return {};
    auto it=devices_.find(settings.player[p].device);if(it==devices_.end())return {};
    auto r=it->second;r.aim.x=(r.aim.x-image_[0])/image_[2];r.aim.y=(r.aim.y-image_[1])/image_[3];
    r.onScreen=r.onScreen&&r.aim.x>=0&&r.aim.x<1&&r.aim.y>=0&&r.aim.y<1;return r;
}
PlayerState Lightguns::Player(int p)const{
    auto r=Reading(p);if(!r.connected)return {};
    const auto& s=settings.player[p];PlayerState out;out.connected=true;out.relative=r.relative;
    AimPoint aim;bool valid=s.calibration.Apply(r.aim,aim);
    out.onScreen=valid&&r.onScreen&&aim.x>=0&&aim.x<1&&aim.y>=0&&aim.y<1;
    if(valid){out.x=float(aim.x);out.y=float(aim.y);}
    auto map=[&](uint8_t b){return uint8_t((b&s.buttons[0]?kTrigger:0)|(b&s.buttons[1]?kReload:0)|(b&s.buttons[3]?kStart:0));};
    out.held=map(r.held&~r.suppressed);out.pressed=map(r.pressed&~r.suppressed);out.released=map(r.released);
    out.menuHeld=(r.held&~r.suppressed)&s.buttons[2]?8:0;out.menuPressed=(r.pressed&~r.suppressed)&s.buttons[2]?8:0;
    return out;
}
void Lightguns::BeginFrame(){for(auto& [id,r]:devices_)r.pressed=r.released=0;}
void Lightguns::ReleaseButtons(){for(auto& [id,r]:devices_){r.pressed=r.released=0;r.suppressed|=r.held;}}
void Lightguns::BeginAssign(int p){if(p>=0&&p<2){assigning_=p;assignAfter_=sequence_;assigned_=false;BeginFrame();}}
bool Lightguns::TakeAssigned(){bool a=assigned_;assigned_=false;return a;}
void Lightguns::SwapPlayers(){std::swap(settings.player[0],settings.player[1]);settings.manual=true;CancelAssign();ReleaseButtons();}
void Lightguns::ClearAssignments(){settings.player={};settings.manual=true;CancelAssign();ReleaseButtons();}
void Lightguns::ForgetPlayer(int p){if(p>=0&&p<2){settings.player[p]={};settings.manual=true;ReleaseButtons();}}
int Lightguns::ConnectedCount()const{return int(devices_.size());}
}
