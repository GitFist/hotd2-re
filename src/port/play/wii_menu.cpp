#include "wii_menu.h"
#include "game/menu_audio.h"
#include "game/menu_text.h"
#include "play/device_menu_hud.h"
#include <algorithm>
#include <cmath>

namespace play {
namespace {
const char* mainLabels[]{"Wii Remote support","Connect remotes","Automatic reconnect","Player 1 setup","Player 2 setup","Swap players","Back"};
const char* playerLabels[]{"Calibrate aim","Fire","Reload","Confirm","Pause","Aim smoothing","Disconnect remote","Reset Player Setup","Back"};
const input::wii::Point targets[4]{{.15,.15},{.85,.15},{.85,.85},{.15,.85}};
void Text(const game::ExeImage& exe,float x,float y,const std::string& s,std::vector<game::SpriteDef>& out,bool selected=false,float scale=.8f){
    game::MenuTextScaled(exe,x,y,s,selected?0x110:0,scale,out);
}
void Cross(const game::ExeImage& exe,int player,float x,float y,std::vector<game::SpriteDef>& out){
    game::SpriteDef d;d.id=exe.S16(player?0x00579F60:0x00579F58);d.x=x;d.y=y;d.depth=.8f;d.flags=0xA;out.push_back(d);
}
std::string Status(const input::wii::Remote& r){
    if(!r.connected)return r.reserved?"Disconnected - waiting":"Not connected";
    return (r.battery>=0?std::to_string(r.battery)+" percent  ":"")+(r.tracking?std::string("Aim detected"):std::string("Point at sensor bar"));
}
void ModernText(game::WorldHud& hud, float x, float y, const std::string& value, float scale = .55f, uint32_t color = 0xFFECE7DC) {
    device_ui::Text(hud, x, y, value, scale, color);
}
void ModernLines(game::WorldHud& hud, float x, float y, const std::string& value, size_t columns = 76, int rows = 2) {
    size_t at = 0;
    for (int row = 0; row < rows && at < value.size(); ++row) {
        size_t end = std::min(value.size(), at + columns);
        if (end < value.size()) {
            const auto space = value.rfind(' ', end);
            if (space != std::string::npos && space > at) end = space;
        }
        std::string line = value.substr(at, end - at);
        if (row == rows - 1 && end < value.size() && line.size() > 3) line.replace(line.size() - 3, 3, "...");
        ModernText(hud, x, y + 15.f * row, line, .42f, 0xFFC1BAAC);
        at = end;
        while (at < value.size() && value[at] == ' ') ++at;
    }
}
}
bool WiiMenu::Update(const MenuInput& in,input::WiiRemotes& remotes,std::vector<uint32_t>* sounds){
    if(!open_)return false;
    auto sound=[&](uint32_t id){if(sounds)sounds->push_back(id);};
    bool back=in.start || (in.pressed&2) || in.reload;
    if(calibration_>=0){
        const auto& r=remotes.Status().player[player_];
        // B always captures a calibration target, even when rebound to Reload or Pause.
        const bool cancel=(in.pressed&2) || ((in.start || in.reload) && !(r.pressed&input::wii::B));
        if(cancel){calibration_=-1;notice_="Calibration cancelled";sound(game::kOptionBackSound);return false;}
        if(!r.connected){calibration_=-1;notice_="Remote disconnected - connect and retry";return false;}
        if(!(r.buttons&input::wii::B))waitRelease_=false;
        if(!waitRelease_ && (r.pressed&input::wii::B)){
            waitRelease_=true;
            if(!r.tracking){notice_="Both sensor lights must be visible";return false;}
            samples_[calibration_++]=r.raw;notice_.clear();
            sound(game::kOptionSelectSound);
            if(calibration_==4){
                bool ok=remotes.settings.player[player_].calibration.Fit(samples_);calibration_=-1;
                notice_=ok?"Calibration saved - test your aim":"Calibration failed - keep still and retry";
                return ok;
            }
        }
        return false;
    }
    if(back){if(player_>=0){player_=-1;cursor_=0;pointer_={};}else Close();sound(game::kOptionBackSound);return false;}
    int rows=player_<0?7:9,under=-1;
    if(in.pointer && in.px>=24 && in.px<616)for(int i=0;i<rows;++i)if(in.py>=RowY(i)+8 && in.py<RowY(i)+35)under=i;
    if(pointer_.Update(in.pointer,in.px,in.py,in.trigger) && under>=0)cursor_=under;
    if(in.pressed&0x10)cursor_=(cursor_+rows-1)%rows;
    if(in.pressed&0x20)cursor_=(cursor_+1)%rows;
    bool choose=(in.pressed&0xc) || (in.trigger && under==cursor_);
    int delta=(in.pressed&0x40)?-1:((in.pressed&0x80)||choose)?1:0;
    if(!delta)return false;
    const bool valueRow=player_<0?(cursor_==0||cursor_==2):(cursor_>=1&&cursor_<=5);
    if(valueRow||choose)sound((player_<0&&cursor_==6)||(player_>=0&&cursor_==8)?game::kOptionBackSound:game::kOptionSelectSound);
    notice_.clear();
    if(player_<0){
        switch(cursor_){
        case 0: remotes.settings.enabled=!remotes.settings.enabled;remotes.Configure();return true;
        case 1: if(choose){remotes.Connect();return true;}break;
        case 2: remotes.settings.continuous=!remotes.settings.continuous;remotes.Configure();return true;
        case 3:case 4: if(choose){player_=cursor_-3;cursor_=0;pointer_={};}break;
        case 5: if(choose){remotes.SwapPlayers();return true;}break;
        case 6: if(choose)Close();break;
        }
    }else{
        auto& profile=remotes.settings.player[player_];
        if(cursor_>=1 && cursor_<=4){profile.CycleBinding(cursor_-1,delta);return true;}
        switch(cursor_){
        case 0: if(choose){
            if(!remotes.Status().player[player_].connected)notice_="Connect this remote first";
            else{calibration_=0;waitRelease_=true;pointer_={};}
        }break;
        case 5:profile.smoothing=(profile.smoothing+delta+3)%3;return true;
        case 6:if(choose)remotes.Disconnect(player_);break;
        case 7:if(choose){profile={};notice_="Default buttons and aim restored";return true;}break;
        case 8:if(choose){player_=-1;cursor_=0;pointer_={};}break;
        }
    }
    return false;
}
void WiiMenu::Sprites(const game::ExeImage& exe,const input::WiiRemotes& remotes,std::vector<game::SpriteDef>& out)const{
    if(!open_)return;
    game::MenuBackground(out);
    Text(exe,32,24,player_<0?"Wii Remotes":"Wii Remote Player "+std::to_string(player_+1),out,false,1.f);
    const auto& status=remotes.Status();
    if(calibration_>=0){
        auto target=targets[calibration_];
        Cross(exe,0,float(target.x*640),float(target.y*480),out);
        Text(exe,64,207,"Aim at the cross and press B",out);
        Text(exe,64,232,"Target "+std::to_string(calibration_+1)+" of 4 - keep your position",out,false,.7f);
        Text(exe,32,404,notice_.empty()?"ESC cancels   Both sensor lights must stay visible":notice_,out,false,.65f);
        return;
    }
    if(player_<0){
        for(int i=0;i<7;++i){
            Text(exe,32,RowY(i),mainLabels[i],out,cursor_==i);
            if(i==0 || i==2)Text(exe,452,RowY(i),(i==0?remotes.settings.enabled:remotes.settings.continuous)?"ON":"OFF",out,cursor_==i);
        }
        Text(exe,32,290,"P1  "+Status(status.player[0]),out,false,.65f);
        Text(exe,32,312,"P2  "+Status(status.player[1]),out,false,.65f);
        Text(exe,32,350,status.message,out,false,.62f);
        Text(exe,32,376,"Bluetooth: red SYNC or 1 and 2",out,false,.7f);
        Text(exe,32,400,"DolphinBar: Mode 4, then SYNC on bar and remote",out,false,.65f);
    }else{
        Text(exe,32,57,Status(status.player[player_]),out,false,.65f);
        for(int i=0;i<9;++i){
            Text(exe,32,RowY(i),playerLabels[i],out,cursor_==i);
            std::string value;
            auto& profile=remotes.settings.player[player_];
            if(i==0)value=profile.calibration.calibrated?"SET":"DEFAULT";
            if(i>=1 && i<=4)value=input::wii::ButtonName(profile.buttons[i-1]);
            if(i==5)value=profile.smoothing==0?"OFF":profile.smoothing==1?"LIGHT":"STRONG";
            if(!value.empty())Text(exe,452,RowY(i),value,out,cursor_==i);
        }
        const auto& r=status.player[player_];
        std::string held;
        for (auto button : input::wii::Bindable) if (r.buttons & button) { if (!held.empty()) held += " "; held += input::wii::ButtonName(button); }
        Text(exe,32,350,"Buttons: "+(held.empty()?"none":held),out,false,.65f);
        Text(exe,32,378,notice_.empty()?"Point to test aim   Left or right changes a binding":notice_,out,false,.65f);
    }
    Text(exe,32,442,"ENTER selects   ESC goes back",out,false,.7f);
    // Aim preview uses each remote's calibrated game coordinates, never the Windows cursor.
    for(int p=0;p<2;++p)if(status.player[p].connected){auto s=remotes.Player(p);if(s.onScreen)Cross(exe,p,s.x*640,s.y*480,out);}
}

void WiiMenu::ModernHud(const input::WiiRemotes& remotes, game::WorldHud& hud) const {
    if (!open_) return;
    const auto& status = remotes.Status();
    if (calibration_ >= 0) {
        device_ui::Begin(hud, 10, "", "", false);
        // Keep every target corner free of the page chrome. These coordinates
        // are the exact destinations used by Calibration::Fit, not a preview.
        const auto target = targets[calibration_];
        const float x = float(target.x * 640), y = float(target.y * 480);
        hud.menuPanels = {{148, 166, 492, 332, 0xF4090D10}, {164, 197, 476, 199, 0xFF92272D}};
        device_ui::Target(hud, x, y, player_);
        ModernText(hud, 164, 176, "WII REMOTE CALIBRATION", .65f);
        ModernText(hud, 164, 210, "PLAYER " + std::to_string(player_ + 1) + " / TARGET " + std::to_string(calibration_ + 1) + " OF 4", .5f);
        ModernText(hud, 164, 237, "Aim at the cross. Press B.", .55f);
        ModernText(hud, 164, 259, "Release B between targets. Keep your position.", .4f, 0xFFC1BAAC);
        ModernLines(hud, 164, 278, notice_.empty() ? Status(status.player[player_]) : notice_, 48, 2);
        ModernText(hud, 164, 313, "ESC / BACK: CANCEL", .42f, 0xFFC1BAAC);
        ModernText(hud, x + 18, y - 10, std::to_string(calibration_ + 1), .5f);
        return;
    }
    device_ui::Begin(hud, 10, player_ < 0 ? "WII REMOTES" : "WII REMOTE / PLAYER " + std::to_string(player_ + 1),
                     player_ < 0 ? "NATIVE BLUETOOTH / DOLPHINBAR" : "CALIBRATION / BUTTONS / AIM");
    hud.nightShiftRow = cursor_;
    const int rows = player_ < 0 ? 7 : 9;
    for (int row = 0; row < rows; ++row) {
        const float y = RowY(row) + 10;
        std::string value;
        bool adjustable = false;
        if (player_ < 0) {
            if (row == 0 || row == 2) {
                value = (row == 0 ? remotes.settings.enabled : remotes.settings.continuous) ? "ON" : "OFF";
                adjustable = true;
            }
        } else {
            const auto& profile = remotes.settings.player[player_];
            if (row == 0) value = profile.calibration.calibrated ? "SET" : "DEFAULT";
            if (row >= 1 && row <= 4) {
                value = input::wii::ButtonName(profile.buttons[row - 1]);
                adjustable = true;
            }
            if (row == 5) {
                value = profile.smoothing == 0 ? "OFF" : profile.smoothing == 1 ? "LIGHT" : "STRONG";
                adjustable = true;
            }
        }
        device_ui::Row(hud, row, cursor_, player_ < 0 ? mainLabels[row] : playerLabels[row], value);
        if (row == cursor_ && adjustable) {
            ModernText(hud, 426, y, "<");
            ModernText(hud, 598, y, ">");
        }
    }
    if (player_ < 0) {
        ModernText(hud, 34, 300, "P1 / " + Status(status.player[0]), .48f, 0xFFC1BAAC);
        ModernText(hud, 34, 323, "P2 / " + Status(status.player[1]), .48f, 0xFFC1BAAC);
        const std::string message = status.scanning ? "SCANNING / " + status.message : status.message;
        ModernLines(hud, 34, 348, message);
        ModernText(hud, 34, 388, "Bluetooth: red SYNC or 1 and 2", .46f, 0xFFC1BAAC);
        ModernText(hud, 34, 408, "DolphinBar: Mode 4, then SYNC on bar and remote", .42f, 0xFFC1BAAC);
    } else {
        const auto& remote = status.player[player_];
        ModernText(hud, 34, 347, Status(remote), .48f, 0xFFC1BAAC);
        std::string held;
        for (auto button : input::wii::Bindable)
            if (remote.buttons & button) {
                if (!held.empty()) held += " ";
                held += input::wii::ButtonName(button);
            }
        ModernLines(hud, 34, 369, "Buttons: " + (held.empty() ? "none" : held));
        ModernLines(hud, 34, 401, notice_.empty() ? "Point to test aim. Left / right changes a binding." : notice_);
    }
    device_ui::Footer(hud, "SELECT / SHOOT: OPEN OR CHANGE   LEFT / RIGHT: ADJUST   ESC: BACK");
    // Keep the existing calibrated aim preview visible independently of the
    // gameplay-crosshair preference. Device setup is where users test their aim.
    for (int player = 0; player < 2; ++player)
        if (status.player[player].connected) {
            const auto aim = remotes.Player(player);
            if (aim.onScreen) device_ui::Target(hud, aim.x * 640, aim.y * 480, player);
        }
}
}  // namespace play
