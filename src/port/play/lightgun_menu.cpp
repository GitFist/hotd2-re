#include "lightgun_menu.h"
#include "game/menu_text.h"

namespace play {
namespace {
const char* mainLabels[]{"Input mode","Assign Player 1","Assign Player 2","Player 1 settings","Player 2 settings","Swap players","Clear assignments","Back"};
const char* playerLabels[]{"Calibrate aim","Fire","Reload","Confirm","Pause","Test both guns","Reset calibration","Remove assignment","Back"};
const input::AimPoint targets[4]{{.15,.15},{.85,.15},{.85,.85},{.15,.85}};
void Text(const game::ExeImage& exe,float x,float y,const std::string& s,std::vector<game::SpriteDef>& out,bool selected=false,float scale=.8f){
    game::MenuTextScaled(exe,x,y,s,selected?0x110:0,scale,out);
}
void Cross(const game::ExeImage& exe,int p,float x,float y,std::vector<game::SpriteDef>& out){
    game::SpriteDef d;d.id=exe.S16(p?0x00579F60:0x00579F58);d.x=x;d.y=y;d.depth=.8f;d.flags=0xA;out.push_back(d);
}
std::string Status(const input::Lightguns& guns,int p){
    if(guns.settings.player[p].device.empty())return "Not assigned";
    auto r=guns.Reading(p);if(!r.connected)return "Disconnected - slot reserved";
    return r.relative?"Mouse device ready":"Light gun ready";
}
}
LightgunMenu::Action LightgunMenu::Update(const MenuInput& in,input::Lightguns& guns,bool gunMode){
    if(!open_)return Action::None;
    if(testing_){
        if(in.pressed&2){testing_=false;guns.ReleaseButtons();return Action::None;}
        for(int p=0;p<2;++p)if(guns.Player(p).pressed&input::kTrigger)++shots_[p];
        return Action::None;
    }
    // Assignment, calibration and bindings only accept fresh physical input. Keyboard
    // Escape always cancels, even if the chosen physical button used to mean Back.
    if(guns.Assigning()>=0){
        if(in.pressed&2){guns.CancelAssign();notice_="Assignment cancelled";}
        return Action::None;
    }
    if(guns.TakeAssigned()){notice_="Gun assigned - open player settings to test";return Action::Save;}
    if(calibration_>=0||binding_>=0){
        if(in.pressed&2){calibration_=binding_=-1;notice_="Cancelled - previous settings kept";guns.ReleaseButtons();return Action::None;}
        auto r=guns.Reading(player_);
        if(!r.connected){calibration_=binding_=-1;notice_="Gun disconnected - reconnect and retry";return Action::None;}
        if(binding_>=0){
            if(r.sequence>captureAfter_&&r.pressed){
                for(auto b:input::kGunButtons)if(r.pressed&b){guns.settings.player[player_].Bind(binding_,b);break;}
                binding_=-1;guns.settings.manual=true;guns.ReleaseButtons();notice_="Button saved";return Action::Save;
            }
        }else{
            auto fire=guns.settings.player[player_].buttons[0];
            if(!(r.held&fire))waitRelease_=false;
            if(!waitRelease_&&r.sequence>captureAfter_&&(r.pressed&fire)){
                waitRelease_=true;
                if(!r.onScreen){notice_="Aim inside the game image";return Action::None;}
                samples_[calibration_++]=r.aim;
                if(calibration_==4){bool ok=guns.settings.player[player_].calibration.Fit(samples_);calibration_=-1;
                    notice_=ok?"Calibration saved - test your aim":"Calibration failed - retry all four targets";
                    if(ok)guns.settings.manual=true;guns.ReleaseButtons();return ok?Action::Save:Action::None;}
            }
        }
        return Action::None;
    }
    if(in.start||in.reload||(in.pressed&2)){
        if(player_>=0){player_=-1;cursor_=0;pointer_={};}else Close(guns);
        return Action::None;
    }
    int under=-1,rows=player_<0?8:9;
    if(in.pointer&&in.px>=24&&in.px<616)for(int i=0;i<rows;++i)if(in.py>=RowY(i)+8&&in.py<RowY(i)+35)under=i;
    if(pointer_.Update(in.pointer,in.px,in.py,in.trigger)&&under>=0)cursor_=under;
    if(in.pressed&0x10)cursor_=(cursor_+rows-1)%rows;
    if(in.pressed&0x20)cursor_=(cursor_+1)%rows;
    bool choose=(in.pressed&0xc)||(in.trigger&&under==cursor_);
    if(player_<0&&cursor_==0&&(choose||(in.pressed&0xc0)))return gunMode?Action::MouseMode:Action::GunMode;
    if(!choose)return Action::None;
    notice_.clear();
    if(player_<0){
        if(cursor_==1||cursor_==2){
            if(!gunMode){notice_="Select LIGHT GUNS input mode first";return Action::None;}
            guns.BeginAssign(cursor_-1);
        }else if(cursor_==3||cursor_==4){player_=cursor_-3;cursor_=0;pointer_={};}
        else if(cursor_==5){guns.SwapPlayers();notice_="Players and their settings swapped";return Action::Save;}
        else if(cursor_==6){guns.ClearAssignments();notice_="Assignments cleared - assign each gun again";return Action::Save;}
        else if(cursor_==7)Close(guns);
    }else{
        auto r=guns.Reading(player_);
        if(cursor_<=4){
            if(!gunMode||!r.connected){notice_="Assign and connect this gun first";return Action::None;}
            captureAfter_=r.sequence;guns.BeginFrame();
            if(cursor_==0){calibration_=0;waitRelease_=(r.held&guns.settings.player[player_].buttons[0])!=0;}
            else binding_=cursor_-1;
        }else if(cursor_==5){testing_=true;shots_={};guns.ReleaseButtons();}
        else if(cursor_==6){guns.settings.player[player_].calibration={};if(r.connected)guns.settings.manual=true;notice_="Default aim restored";return Action::Save;}
        else if(cursor_==7){guns.ForgetPlayer(player_);notice_="Assignment removed";return Action::Save;}
        else {player_=-1;cursor_=0;pointer_={};}
    }
    return Action::None;
}
void LightgunMenu::Sprites(const game::ExeImage& exe,const input::Lightguns& guns,bool gunMode,std::vector<game::SpriteDef>& out)const{
    if(!open_)return;
    game::MenuBackground(out);
    if(testing_){
        Text(exe,32,24,"Light Gun Test",out,false,1);
        for(int p=0;p<2;++p){
            auto s=guns.Player(p);Text(exe,32,65+28.f*p,"P"+std::to_string(p+1)+"  Shots: "+std::to_string(shots_[p])+"  "+Status(guns,p),out,false,.65f);
            if(s.connected&&s.onScreen)Cross(exe,p,s.x*640,s.y*480,out);
        }
        Text(exe,32,390,"Aim and shoot anywhere to test both guns",out,false,.7f);
        Text(exe,32,442,"ESC returns to setup",out,false,.7f);return;
    }
    Text(exe,32,24,player_<0?"Light Guns":"Light Gun Player "+std::to_string(player_+1),out,false,1);
    if(guns.Assigning()>=0){
        Text(exe,32,150,"Pull the trigger on Player "+std::to_string(guns.Assigning()+1)+" gun",out);
        Text(exe,32,195,"Only use the gun you want to assign",out,false,.75f);
        Text(exe,32,230,"ESC cancels",out);return;
    }
    if(calibration_>=0){
        auto t=targets[calibration_];Cross(exe,player_,float(t.x*640),float(t.y*480),out);
        Text(exe,64,207,"Aim at the cross and pull the trigger",out,false,.75f);
        Text(exe,64,235,"Target "+std::to_string(calibration_+1)+" of 4",out);
        Text(exe,32,404,notice_.empty()?"Keep your position   ESC cancels":notice_,out,false,.65f);return;
    }
    if(binding_>=0){
        Text(exe,32,155,"Press the gun button for "+std::string(playerLabels[binding_+1]),out);
        Text(exe,32,200,"ESC cancels   Mouse buttons 1 to 5 supported",out,false,.65f);return;
    }
    for(int i=0;i<(player_<0?8:9);++i){
        Text(exe,32,RowY(i),player_<0?mainLabels[i]:playerLabels[i],out,cursor_==i);
        std::string value;
        if(player_<0&&i==0)value=gunMode?"LIGHT GUNS":"MOUSE";
        if(player_>=0){auto& s=guns.settings.player[player_];
            if(i==0)value=s.calibration.calibrated?"SET":"DEFAULT";
            if(i>=1&&i<=4)value=input::GunButtonName(s.buttons[i-1]);
        }
        if(!value.empty())Text(exe,440,RowY(i),value,out,cursor_==i,.7f);
    }
    if(player_<0){
        Text(exe,32,320,"P1  "+Status(guns,0),out,false,.65f);
        Text(exe,32,343,"P2  "+Status(guns,1),out,false,.65f);
        Text(exe,32,379,notice_.empty()?"Choose LIGHT GUNS, then assign each player":notice_,out,false,.65f);
        Text(exe,32,406,"Sinden users: keep the Sinden border enabled",out,false,.65f);
    }else{
        auto r=guns.Reading(player_);std::string buttons;
        for(auto b:input::kGunButtons)if(r.held&b){if(!buttons.empty())buttons+=" ";buttons+=input::GunButtonName(b);}
        Text(exe,32,57,Status(guns,player_),out,false,.65f);
        Text(exe,32,346,"Buttons: "+(buttons.empty()?"none":buttons),out,false,.65f);
        Text(exe,32,369,notice_.empty()?"Select Test both guns for target practice":notice_,out,false,.65f);
        Text(exe,32,390,"ENTER selects a button to remap",out,false,.7f);
    }
    Text(exe,32,442,"ENTER selects   ESC goes back",out,false,.7f);
    if(gunMode)for(int p=0;p<2;++p){auto s=guns.Player(p);if(s.connected&&s.onScreen)Cross(exe,p,s.x*640,s.y*480,out);}
}
}
