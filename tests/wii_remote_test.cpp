#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include "input/wii_remote.h"
#include "input/pointer_guns.h"
#include "play/wii_menu.h"
#include "play/keyboard.h"
#include "play/gun_input.h"

static void Check(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
using namespace input::wii;
struct Fake final:Backend {
    Snapshot snapshot;bool enabled=false,scanned=false;int disconnected=-1;
    void Enable(bool on,bool)override{enabled=on;}
    void Scan()override{scanned=true;}
    void Disconnect(int p)override{disconnected=p;snapshot.player[p]={};}
    void SwapPlayers()override{std::swap(snapshot.player[0],snapshot.player[1]);}
    Snapshot Poll()override{auto r=snapshot;for(auto& p:snapshot.player)p.pressed=0;return r;}
};
static std::array<uint8_t,22> Report(uint16_t buttons,int x=500,int y=380,bool track=true){
    std::array<uint8_t,22> r{};r[0]=0x33;r[1]=uint8_t(buttons>>8);r[2]=uint8_t(buttons);
    for(int i=6;i<18;++i)r[i]=0xff;
    if(track)for(int i=0;i<2;++i){int xx=x+(i?70:-70);r[6+i*3]=uint8_t(xx);r[7+i*3]=uint8_t(y);r[8+i*3]=uint8_t(((xx>>8)<<4)|((y>>8)<<6)|3);}
    return r;
}
static void MenuKey(play::WiiMenu& menu,input::WiiRemotes& source,uint32_t key){play::MenuInput mi;mi.pressed=key;menu.Update(mi,source);}
static bool HudText(const game::WorldHud& hud, const std::string& part) {
    for (const auto& text : hud.menuText)
        if (text.text.find(part) != std::string::npos) return true;
    return false;
}
static bool HudTarget(const game::WorldHud& hud, float x, float y) {
    for (const auto& panel : hud.menuPanels)
        if (std::abs((panel.left + panel.right) * .5f - x) < .001f && std::abs((panel.top + panel.bottom) * .5f - y) < .001f &&
            panel.right - panel.left >= 12 && panel.bottom - panel.top <= 8)
            return true;
    return false;
}
static void ModernMenuPresentation() {
    auto fake = std::make_unique<Fake>();
    auto* backend = fake.get();
    input::WiiRemotes source(nullptr, std::move(fake));
    source.settings.enabled = true;
    auto& remote = backend->snapshot.player[0];
    remote.connected = remote.reserved = remote.tracking = true;
    remote.battery = 72;
    remote.raw = {.3, .4};
    backend->snapshot.player[1].reserved = true;
    backend->snapshot.scanning = true;
    backend->snapshot.message = "Searching for a remote";
    source.Update();
    play::WiiMenu menu;
    game::WorldHud hud;
    hud.menuText.push_back({1, 2, "sentinel", 1, 0xFFFFFFFF});
    menu.ModernHud(source, hud);
    Check(hud.menuText.size() == 1 && HudText(hud, "sentinel"), "closed Wii page leaves the caller HUD alone");
    menu.Open();
    menu.ModernHud(source, hud);
    Check(hud.nightShiftPage == 10 && hud.nightShiftRow == 0 && HudText(hud, "WII REMOTES") && !HudText(hud, "sentinel"),
          "modern overview replaces prior HUD content");
    Check(HudText(hud, "Wii Remote support") && HudText(hud, "ON") && HudText(hud, "Automatic reconnect") && HudText(hud, "OFF"),
          "overview shows actual support and reconnect values");
    Check(HudText(hud, "72 percent") && HudText(hud, "Aim detected") && HudText(hud, "Disconnected - waiting"),
          "overview includes per-player battery, tracking and reserved-slot status");
    Check(HudText(hud, "SCANNING") && HudText(hud, "Searching for a remote") && HudText(hud, "DolphinBar") && HudText(hud, "SYNC"),
          "scan and pairing guidance remain visible");
    Check(HudText(hud, "ESC") && HudText(hud, "Back"), "overview retains keyboard and pointer Back controls");
    Check(HudTarget(hud, 192, 192), "overview draws the connected remote's calibrated aim preview");
    for (int row = 0; row < 7; ++row) {
        play::MenuInput point;
        point.pointer = true;
        point.px = 40;
        point.py = play::WiiMenu::RowY(row) + 18;
        menu.Update(point, source);
        menu.ModernHud(source, hud);
        bool selected = false;
        for (const auto& panel : hud.menuPanels)
            if (panel.color == 0xCC8F2026 && panel.left == 24 && panel.right == 616 && panel.top == play::WiiMenu::RowY(row) + 8 &&
                panel.bottom == play::WiiMenu::RowY(row) + 35)
                selected = true;
        Check(menu.Cursor() == row && hud.nightShiftRow == row && selected,
              "modern selection band matches the unchanged pointer hit rectangle");
    }
    menu.Open();
    for (int i = 0; i < 3; ++i) MenuKey(menu, source, 0x20);
    MenuKey(menu, source, 8);
    menu.ModernHud(source, hud);
    Check(menu.SelectedPlayer() == 0 && HudText(hud, "PLAYER 1") && HudText(hud, "Calibrate aim") && HudText(hud, "DEFAULT"),
          "player page shows calibration state");
    Check(HudText(hud, "Fire") && HudText(hud, "Reload") && HudText(hud, "Confirm") && HudText(hud, "Pause") && HudText(hud, "LIGHT"),
          "player page retains all existing control bindings and aim smoothing");
    Check(HudTarget(hud, 192, 192), "player setup keeps the calibrated aim preview visible");
    const auto savedButtons = source.settings.player[0].buttons;
    const auto savedCalibration = source.settings.player[0].calibration.h;
    const int cursor = menu.Cursor();
    const auto sourcePress = source.Status().player[0].pressed;
    menu.ModernHud(source, hud);
    menu.ModernHud(source, hud);
    Check(menu.Cursor() == cursor && source.settings.player[0].buttons == savedButtons &&
              source.settings.player[0].calibration.h == savedCalibration && source.Status().player[0].pressed == sourcePress,
          "rendering setup never changes navigation, bindings, aim or button history");

    MenuKey(menu, source, 8);
    menu.ModernHud(source, hud);
    Check(menu.CalibrationStep() == 0 && HudText(hud, "WII REMOTE CALIBRATION") && HudText(hud, "Press B") && HudText(hud, "CANCEL"),
          "calibration shows native B capture and usable cancel instructions");
    Check(HudTarget(hud, 96, 72), "first calibration marker uses the true top-left target coordinate");
    source.BeginFrame();
    remote.buttons = remote.pressed = 0;
    source.Update();
    menu.Update({}, source);
    remote.tracking = false;
    remote.buttons = remote.pressed = B;
    source.Update();
    menu.Update({}, source);
    menu.ModernHud(source, hud);
    Check(menu.CalibrationStep() == 0 && HudText(hud, "Both sensor lights must be visible"),
          "missing sensor lights keep the target and show the capture error");
    const std::array<Point, 4> samples{{{.25, .20}, {.75, .24}, {.78, .77}, {.22, .73}}};
    const Point targets[4]{{.15, .15}, {.85, .15}, {.85, .85}, {.15, .85}};
    for (int target = 0; target < 4; ++target) {
        source.BeginFrame();
        remote.tracking = true;
        remote.buttons = remote.pressed = 0;
        source.Update();
        menu.Update({}, source);
        menu.ModernHud(source, hud);
        Check(HudTarget(hud, float(targets[target].x * 640), float(targets[target].y * 480)) &&
                  HudText(hud, "TARGET " + std::to_string(target + 1) + " OF 4"),
              "each displayed target matches the calibration solver destination");
        for (const auto& panel : hud.menuPanels)
            if (panel.right - panel.left > 100)
                Check(!(panel.left < float(targets[target].x * 640) && panel.right > float(targets[target].x * 640) &&
                        panel.top < float(targets[target].y * 480) && panel.bottom > float(targets[target].y * 480)),
                      "calibration page chrome does not cover the target");
        remote.raw = samples[target];
        remote.buttons = remote.pressed = B;
        source.Update();
        menu.Update({}, source);
    }
    menu.ModernHud(source, hud);
    Check(menu.CalibrationStep() == -1 && source.settings.player[0].calibration.calibrated && HudText(hud, "SET") &&
              HudText(hud, "Calibration saved - test your aim"),
          "successful calibration returns to the modern player page with saved status");
    MenuKey(menu, source, 8);
    play::MenuInput cancel;
    cancel.start = true;
    cancel.pressed = 2;
    menu.Update(cancel, source);
    menu.ModernHud(source, hud);
    Check(menu.CalibrationStep() == -1 && HudText(hud, "Calibration cancelled"),
          "calibration cancellation notice is retained in modern presentation");
    MenuKey(menu, source, 8);
    remote.connected = false;
    source.BeginFrame();
    source.Update();
    menu.Update({}, source);
    menu.ModernHud(source, hud);
    Check(menu.CalibrationStep() == -1 && HudText(hud, "Remote disconnected - connect and retry"),
          "disconnect cancels calibration and displays the reason");
    menu.Update(cancel, source);
    backend->snapshot.scanning = false;
    backend->snapshot.message = "No Bluetooth adapter was found";
    source.Update();
    menu.ModernHud(source, hud);
    Check(HudText(hud, "No Bluetooth adapter was found") && HudText(hud, "P1 / Disconnected - waiting"),
          "connection errors remain readable on the overview");
    menu.Open();
    for (int i = 0; i < 4; ++i) MenuKey(menu, source, 0x20);
    MenuKey(menu, source, 8);
    MenuKey(menu, source, 8);
    menu.ModernHud(source, hud);
    Check(menu.SelectedPlayer() == 1 && HudText(hud, "PLAYER 2") && HudText(hud, "Connect this remote first"),
          "unconnected P2 setup shows its own header and calibration guidance");
    auto& second = backend->snapshot.player[1];
    second.connected = second.tracking = true;
    second.raw = {.5, .5};
    source.Update();
    MenuKey(menu, source, 8);
    menu.ModernHud(source, hud);
    bool cyan = false;
    for (const auto& panel : hud.menuPanels)
        if (panel.color == 0xFF54BCCF && panel.left == 94 && panel.top == 70) cyan = true;
    Check(menu.CalibrationStep() == 0 && HudText(hud, "PLAYER 2 / TARGET 1 OF 4") && cyan,
          "P2 calibration has the same exact target with its distinct center color");
}
int main(){
    ModernMenuPresentation();
    Remote r;
    auto report=Report(B|Up|Plus);Check(DecodeReport(report.data(),report.size(),r),"valid extended IR report");
    Check(r.buttons==(B|Up|Plus) && r.pressed==(B|Up|Plus) && r.tracking && r.dots==2,"buttons, edges and two IR lights");
    Check(std::abs(r.raw.x-(1-500./1023))<1e-6 && std::abs(r.raw.y-380./767)<1e-6,"IR coordinate high bits and horizontal mirror");
    r.pressed=0;DecodeReport(report.data(),report.size(),r);Check(!r.pressed,"held trigger is not a fresh press");
    for(size_t n=0;n<18;++n){Remote before=r;Check(!DecodeReport(report.data(),n,r),"truncated report refused");Check(r.buttons==before.buttons && r.tracking==before.tracking,"bad report preserves state");}
    auto lost=Report(0,500,380,false);DecodeReport(lost.data(),lost.size(),r);Check(!r.tracking && !r.buttons && r.dots==0,"lost IR and released buttons");
    uint8_t status[]{0x20,0,0,0,0,0,0x60};DecodeReport(status,sizeof(status),r);Check(r.battery==50,"status battery");
    uint8_t mode=3;auto write=WriteRegister(0xb00033,&mode,1);
    Check(write[0]==0x16 && write[1]==4 && write[2]==0xb0 && write[4]==0x33 && write[5]==1 && write[6]==3,"IR register output layout");
    Check(WriteRegister(0,&mode,17)[0]==0,"oversized register write refused");
    Calibration c;std::array<Point,4> samples{{{.25,.20},{.75,.24},{.78,.77},{.22,.73}}};
    Check(c.Fit(samples),"four-point perspective calibration");
    const Point targets[4]{{.15,.15},{.85,.15},{.85,.85},{.15,.85}};
    for(int i=0;i<4;++i){Point mapped;Check(c.Apply(samples[i],mapped) && std::abs(mapped.x-targets[i].x)<1e-6 && std::abs(mapped.y-targets[i].y)<1e-6,"calibration hits displayed targets");}
    Calibration before=c;auto invalid=samples;invalid[2]=invalid[1];Check(!c.Fit(invalid) && c.h==before.h,"bad calibration retains last valid settings");
    std::swap(invalid[0],invalid[2]);Check(!c.Fit(invalid),"crossed or collapsed calibration refused");
    Profile p;p.CycleBinding(0,1);Check(p.buttons[0]==A && p.buttons[1]==B,"duplicate binding swaps actions");
    auto fake=std::make_unique<Fake>();auto* backend=fake.get();input::PointerGuns mouse;
    mouse.OnButton(0,1,true,.2f,.3f);mouse.BeginFrame();
    input::WiiRemotes source(&mouse,std::move(fake));source.Connect();Check(backend->enabled && backend->scanned,"Connect enables native support and scan");
    auto& rr=backend->snapshot.player[0];rr.connected=rr.reserved=rr.tracking=true;rr.raw={.6,.4};rr.buttons=rr.pressed=B;
    source.Update();Check(source.Player(0).pressed==input::kTrigger && source.Player(1).connected,"native P1 and separate mouse P2");
    source.BeginFrame();source.Update();Check(!source.Player(0).pressed && source.Player(0).held==input::kTrigger,"held buttons survive frames without repeated shots");
    rr.buttons=rr.pressed=Plus|Up;source.Update();auto s=source.Player(0);
    Check(s.menuPressed==0x18 && !s.held,"D-pad and confirm separate from firing and pause");
    Check(play::WorldKeyboardBits(s.menuPressed,true,true)==0x14,"confirm uses existing keyboard/game semantics");
    source.BeginFrame();rr.buttons=rr.pressed=Home;source.Update();Check(source.Player(0).pressed==input::kStart,"Home pauses");
    source.BeginFrame();rr.buttons=0;rr.pressed=B;source.Update();Check(source.Player(0).pressed==input::kTrigger && !source.Player(0).held,"tap shorter than a game frame survives");
    source.BeginFrame();rr.tracking=false;source.Update();Check(!source.Player(0).onScreen,"missing IR cannot fire at a stale position");
    rr.connected=false;source.Update();Check(!source.Player(0).connected && source.Player(1).connected,"disconnected slot is reserved; P2 never shifts to P1");
    rr.connected=rr.tracking=true;rr.buttons=rr.pressed=0;source.Update();Check(source.Player(0).connected && !source.Player(0).pressed,"reconnect has no phantom shot");
    source.settings.player[0].calibration=c;source.SwapPlayers();source.Update();Check(source.Status().player[1].connected && source.settings.player[1].calibration.h==c.h,"player swap moves calibration with remote");
    source.SwapPlayers();source.Update();
    play::WiiMenu menu;
    for(int row=0;row<7;++row)for(int ink:{10,18,25}) {
        menu.Open();play::MenuInput pointer;pointer.pointer=true;pointer.px=40;pointer.py=play::WiiMenu::RowY(row)+float(ink);
        menu.Update(pointer,source);Check(menu.Cursor()==row,"pointer tracks visible setup text");
    }
    menu.Open();
    for(int i=0;i<3;++i)MenuKey(menu,source,0x20);MenuKey(menu,source,8);
    Check(menu.SelectedPlayer()==0,"player setup can be opened with keyboard or D-pad");
    MenuKey(menu,source,8);Check(menu.CalibrationStep()==0,"calibration starts");
    for(int i=0;i<4;++i){
        source.BeginFrame();rr.buttons=rr.pressed=0;source.Update();menu.Update({},source);
        rr.raw=samples[i];rr.buttons=rr.pressed=B;source.Update();
        play::MenuInput rebound;rebound.reload=i==1;rebound.start=i==2;
        menu.Update(rebound,source);
    }
    Check(menu.CalibrationStep()==-1 && source.settings.player[0].calibration.calibrated,"four fresh B presses finish calibration");
    MenuKey(menu,source,8);play::MenuInput escape;escape.start=true;escape.pressed=2;menu.Update(escape,source);
    Check(menu.CalibrationStep()==-1 && menu.IsOpen(),"Escape cancels calibration without closing setup");
    menu.Update(escape,source);Check(menu.SelectedPlayer()==-1,"Escape returns to remote list");
    menu.Update(escape,source);Check(!menu.IsOpen(),"Escape closes setup");
    // Only user preferences are persisted, never Bluetooth addresses, HID paths or button history.
    auto file=std::filesystem::temp_directory_path()/"hotd2-wii-settings-test.ini";
    source.settings.continuous=true;Check(source.settings.Save(file.string()),"settings save");
    Settings restored;Check(restored.Load(file.string()) && restored.enabled && restored.continuous && restored.player[0].calibration.calibrated,"settings round trip");
    {std::ofstream f(file);f<<"p1.buttons=4 4 4 4\np1.calibration=nan 0 0 0 1 0 0 0 1\np2.smoothing=100\n";}
    Check(restored.Load(file.string()) && restored.player[0].buttons==Profile{}.buttons && !restored.player[0].calibration.calibrated && restored.player[1].smoothing==2,"invalid settings fail to safe defaults");
    std::filesystem::remove(file);
    puts("PASS native Wii Remote reports, calibration, controls, reconnect slots, menus and settings");
}
