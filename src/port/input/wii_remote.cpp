#include "wii_remote.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace input::wii {
const char* ButtonName(uint16_t b) {
    switch(b) { case A:return "A"; case B:return "B"; case Plus:return "PLUS"; case Minus:return "MINUS";
        case Home:return "HOME"; case One:return "1"; case Two:return "2"; default:return "?"; }
}
bool Calibration::Apply(Point p, Point& out) const {
    double z=h[6]*p.x+h[7]*p.y+h[8];
    if (!std::isfinite(z) || std::abs(z)<1e-8) return false;
    out={(h[0]*p.x+h[1]*p.y+h[2])/z,(h[3]*p.x+h[4]*p.y+h[5])/z};
    return std::isfinite(out.x) && std::isfinite(out.y);
}
bool Calibration::Fit(const std::array<Point,4>& s) {
    const std::array<Point,4> target{{{.15,.15},{.85,.15},{.85,.85},{.15,.85}}};
    // Reject repeated, crossed or nearly collinear samples before solving the homography.
    double sign=0;
    for(int i=0;i<4;++i) {
        auto a=s[i],b=s[(i+1)%4],c=s[(i+2)%4];
        double cross=(b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x);
        if(!std::isfinite(cross) || std::abs(cross)<.002 || (sign && cross*sign<0)) return false;
        sign=cross;
    }
    double m[8][9]{};
    for(int i=0;i<4;++i) {
        double x=s[i].x,y=s[i].y,u=target[i].x,v=target[i].y;
        double a[9]{x,y,1,0,0,0,-u*x,-u*y,u},b[9]{0,0,0,x,y,1,-v*x,-v*y,v};
        std::copy(a,a+9,m[i*2]); std::copy(b,b+9,m[i*2+1]);
    }
    for(int col=0;col<8;++col) {
        int pivot=col;
        for(int r=col+1;r<8;++r) if(std::abs(m[r][col])>std::abs(m[pivot][col])) pivot=r;
        if(std::abs(m[pivot][col])<1e-9) return false;
        for(int j=0;j<9;++j) std::swap(m[col][j],m[pivot][j]);
        double v=m[col][col]; for(int j=col;j<9;++j) m[col][j]/=v;
        for(int r=0;r<8;++r) if(r!=col) {v=m[r][col];for(int j=col;j<9;++j)m[r][j]-=v*m[col][j];}
    }
    Calibration next;
    for(int i=0;i<8;++i) {next.h[i]=m[i][8];if(!std::isfinite(next.h[i]) || std::abs(next.h[i])>1000)return false;}
    next.calibrated=true;
    *this=next; return true;
}
void Profile::CycleBinding(int action,int direction) {
    if(action<0 || action>=4) return;
    auto it=std::find(Bindable.begin(),Bindable.end(),buttons[action]);
    int index=it==Bindable.end()?0:static_cast<int>(it-Bindable.begin());
    uint16_t next=Bindable[(index+direction+7)%7],old=buttons[action];
    for(int i=0;i<4;++i) if(i!=action && buttons[i]==next) buttons[i]=old;
    buttons[action]=next;
}
bool Settings::Load(const std::string& file) {
    std::ifstream f(file); if(!f)return false;
    Settings loaded;
    for(std::string line;std::getline(f,line);) {
        auto pos=line.find('=');if(pos==std::string::npos)continue;
        std::string key=line.substr(0,pos);std::istringstream v(line.substr(pos+1));
        int number=0;
        if(key=="enabled" && v>>number)loaded.enabled=number==1;
        else if(key=="continuous" && v>>number)loaded.continuous=number==1;
        for(int p=0;p<2;++p) {
            auto prefix="p"+std::to_string(p+1)+".";auto& profile=loaded.player[p];
            if(key==prefix+"smoothing" && v>>number)profile.smoothing=std::clamp(number,0,2);
            if(key==prefix+"buttons") {
                auto binding=profile.buttons;bool valid=true;
                for(auto& b:binding) {if(!(v>>number) || std::find(Bindable.begin(),Bindable.end(),number)==Bindable.end())valid=false; b=static_cast<uint16_t>(number);}
                for(int i=0;i<4;++i)for(int j=0;j<i;++j)if(binding[i]==binding[j])valid=false;
                if(valid)profile.buttons=binding;
            }
            if(key==prefix+"calibration") {
                Calibration c;bool valid=true;
                for(double& x:c.h)if(!(v>>x) || !std::isfinite(x) || std::abs(x)>1000)valid=false;
                double det=c.h[0]*(c.h[4]*c.h[8]-c.h[5]*c.h[7])-c.h[1]*(c.h[3]*c.h[8]-c.h[5]*c.h[6])+c.h[2]*(c.h[3]*c.h[7]-c.h[4]*c.h[6]);
                if(valid && std::abs(det)>1e-8){c.calibrated=true;profile.calibration=c;}
            }
        }
    }
    *this=loaded;return true;
}
bool Settings::Save(const std::string& file) const {
    std::ofstream f(file,std::ios::trunc);if(!f)return false;
    f<<"enabled="<<enabled<<"\ncontinuous="<<continuous<<'\n'<<std::setprecision(12);
    for(int p=0;p<2;++p) {
        auto prefix="p"+std::to_string(p+1)+".";auto& profile=player[p];
        f<<prefix<<"smoothing="<<profile.smoothing<<'\n'<<prefix<<"buttons=";
        for(auto b:profile.buttons)f<<b<<' ';f<<'\n';
        if(profile.calibration.calibrated){f<<prefix<<"calibration=";for(auto x:profile.calibration.h)f<<x<<' ';f<<'\n';}
    }
    f.flush();return bool(f);
}
bool DecodeReport(const uint8_t* b,size_t n,Remote& r) {
    if(!b || n<3)return false;
    size_t required=0;
    switch(b[0]){case 0x20:required=7;break;case 0x21:required=22;break;case 0x22:required=5;break;
        case 0x30:required=3;break;case 0x31:required=6;break;case 0x33:required=18;break;default:return false;}
    if(n<required)return false;
    uint16_t buttons=static_cast<uint16_t>(((b[1]<<8)|b[2])&0x1f9f);
    r.pressed|=buttons & ~r.buttons;r.buttons=buttons;
    if(b[0]==0x20)r.battery=std::clamp(int(b[6])*100/0xc0,0,100);
    if(b[0]==0x33) {
        std::array<Point,4> dots;int count=0;
        for(int i=0;i<4;++i) {
            const auto* d=b+6+i*3;int x=d[0]+((d[2]&0x30)<<4),y=d[1]+((d[2]&0xc0)<<2);
            if(x<1023 && y<768)dots[count++]={double(x),double(y)};
        }
        r.dots=count;r.tracking=count>=2;
        if(r.tracking) {
            // Choose the widest pair; the two LED clusters can each appear as multiple blobs.
            int left=0,right=1;double best=-1;
            for(int i=0;i<count;++i)for(int j=i+1;j<count;++j){double dx=dots[i].x-dots[j].x,dy=dots[i].y-dots[j].y,d=dx*dx+dy*dy;if(d>best){best=d;left=i;right=j;}}
            if(best<16){r.tracking=false;return true;}
            r.raw={1.0-(dots[left].x+dots[right].x)/(2*1023.0),(dots[left].y+dots[right].y)/(2*767.0)};
        }
    }
    return true;
}
std::array<uint8_t,22> WriteRegister(uint32_t addr,const uint8_t* data,size_t n) {
    std::array<uint8_t,22> b{};if(n>16 || !n || !data)return b;
    b[0]=0x16;b[1]=0x04;b[2]=uint8_t(addr>>16);b[3]=uint8_t(addr>>8);b[4]=uint8_t(addr);b[5]=uint8_t(n);
    std::copy(data,data+n,b.begin()+6);return b;
}
#ifndef _WIN32
class Unavailable final:public Backend {
public:void Enable(bool,bool)override{} void Scan()override{} void Disconnect(int)override{} void SwapPlayers()override{}
Snapshot Poll()override{Snapshot s;s.message="Native Wii Remote connections require Windows";return s;}
};
std::unique_ptr<Backend> CreateBackend(){return std::make_unique<Unavailable>();}
#endif
} // namespace input::wii

namespace input {
WiiRemotes::WiiRemotes(GunSource* f,std::unique_ptr<wii::Backend> b):fallback_(f),backend_(std::move(b)){}
void WiiRemotes::Configure(){backend_->Enable(settings.enabled,settings.continuous);}
void WiiRemotes::Connect(){settings.enabled=true;Configure();backend_->Scan();}
void WiiRemotes::Disconnect(int p){backend_->Disconnect(p);}
void WiiRemotes::SwapPlayers(){std::swap(settings.player[0],settings.player[1]);backend_->SwapPlayers();players_={};hadTracking_={};}
void WiiRemotes::Update(){
    auto fresh=backend_->Poll();
    for(int p=0;p<2;++p){
        fresh.player[p].pressed|=snapshot_.player[p].pressed;snapshot_.player[p]=fresh.player[p];
        auto& r=snapshot_.player[p];auto& s=players_[p];const auto& profile=settings.player[p];
        if(!settings.enabled || !r.connected){s={};hadTracking_[p]=false;continue;}
        s.connected=true;s.relative=false;
        uint8_t held=(r.buttons&profile.buttons[0]?kTrigger:0)|(r.buttons&profile.buttons[1]?kReload:0)|(r.buttons&profile.buttons[3]?kStart:0);
        uint8_t edge=(r.pressed&profile.buttons[0]?kTrigger:0)|(r.pressed&profile.buttons[1]?kReload:0)|(r.pressed&profile.buttons[3]?kStart:0);
        s.pressed|=edge;s.released|=s.held&~held;s.held=held;
        auto pad=[&](uint16_t b){return uint32_t((b&wii::Up?0x10:0)|(b&wii::Down?0x20:0)|(b&wii::Left?0x40:0)|(b&wii::Right?0x80:0)|(b&profile.buttons[2]?8:0));};
        s.menuHeld=pad(r.buttons);s.menuPressed|=pad(r.pressed);
        wii::Point aim;
        bool tracked=r.tracking&&profile.calibration.Apply(r.raw,aim);
        s.onScreen=tracked&&aim.x>=0&&aim.x<1&&aim.y>=0&&aim.y<1;
        if(tracked){float a=profile.smoothing==0?1.f:profile.smoothing==1?.65f:.35f;if(!hadTracking_[p])a=1;
            s.x+=a*(float(aim.x)-s.x);s.y+=a*(float(aim.y)-s.y);}
        hadTracking_[p]=tracked;
    }
    snapshot_.message=fresh.message;snapshot_.scanning=fresh.scanning;
}
PlayerState WiiRemotes::Player(int p) const {
    if(p<0 || p>=2)return {};
    if(settings.enabled && snapshot_.player[p].reserved)return players_[p];
    int fallbackIndex=p;
    if(settings.enabled)for(int i=0;i<p;++i)if(snapshot_.player[i].reserved)--fallbackIndex;
    return fallback_?fallback_->Player(fallbackIndex):PlayerState{};
}
void WiiRemotes::BeginFrame(){
    for(int p=0;p<2;++p){players_[p].pressed=players_[p].released=0;players_[p].menuPressed=0;snapshot_.player[p].pressed=0;}
    if(fallback_)fallback_->BeginFrame();
}
} // namespace input
