#include "input/lightguns.h"
#include "game/menu_audio.h"
#include "play/lightgun_menu.h"
#include "play/pause_menu.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include "input/raw_input.h"
#endif

static void Check(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Near(double a,double b){return std::abs(a-b)<.001;}
static const std::string a="1111111111111111",b="2222222222222222",mouse="3333333333333333";
static void Feed(input::Lightguns& g,const std::string& id,float x=.5,float y=.5,int down=0,int up=0){g.Feed(id,x,y,true,false,uint8_t(down),uint8_t(up));}
static play::MenuInput Click(int row){play::MenuInput in;in.pointer=true;in.px=60;in.py=play::LightgunMenu::RowY(row)+20;in.trigger=true;return in;}
static game::WorldHud Modern(const play::LightgunMenu& menu,const input::Lightguns& guns,bool gunMode=true){
    game::WorldHud hud;menu.ModernHud(guns,gunMode,hud);return hud;
}
static bool HasText(const game::WorldHud& hud,const std::string& value){
    for(const auto& line:hud.menuText)if(line.text.find(value)!=std::string::npos)return true;
    return false;
}
static bool HasTarget(const game::WorldHud& hud,double x,double y){
    for(const auto& panel:hud.menuPanels)if(panel.color==0xFFFFFFFF&&Near((panel.left+panel.right)*.5,x)&&Near((panel.top+panel.bottom)*.5,y))return true;
    return false;
}
static void MenuAudio(){
    input::Lightguns guns;
    Feed(guns,a,.2,.3,1);Feed(guns,a,.2,.3,0,1);guns.BeginFrame();
    play::LightgunMenu page;page.Open();
    std::vector<uint32_t> sounds;
    auto cue=[&](uint32_t expected,const char* why){Check(sounds==std::vector<uint32_t>{expected},why);sounds.clear();};
    auto quiet=[&](const char* why){Check(sounds.empty(),why);};
    play::MenuInput down;down.pressed=0x20;
    page.Update({},guns,true,&sounds);page.Update(down,guns,true,&sounds);
    auto hover=Click(3);hover.trigger=false;page.Update(hover,guns,true,&sounds);
    quiet("light-gun idle, keyboard row movement and pointer hover are silent");
    page.Update(Click(3),guns,true,&sounds);cue(game::kOptionSelectSound,"opening light-gun player setup emits exactly sound preview 74");
    play::MenuInput left;left.pressed=0x40;
    page.Update(left,guns,true,&sounds);quiet("left on a light-gun action row is not a selection");
    play::MenuInput esc;esc.pressed=2;
    page.Update(esc,guns,true,&sounds);cue(game::kOptionBackSound,"Escape from light-gun player setup emits exactly sound preview 76");
    auto mode=Click(0);mode.trigger=false;page.Update(mode,guns,true,&sounds);
    page.Update(left,guns,true,&sounds);cue(game::kOptionSelectSound,"changing the light-gun input mode emits sound preview 74");
    page.Update(Click(1),guns,true,&sounds);cue(game::kOptionSelectSound,"starting physical gun assignment emits select once");
    page.Update({},guns,true,&sounds);quiet("waiting for a gun assignment is silent");
    page.Update(esc,guns,true,&sounds);cue(game::kOptionBackSound,"cancelling gun assignment emits back once");
    page.Update(Click(3),guns,true,&sounds);cue(game::kOptionSelectSound,"reopening player setup emits select once");
    page.Update(Click(0),guns,true,&sounds);cue(game::kOptionSelectSound,"opening gun calibration emits select once");
    Feed(guns,a,.2,.2,1);page.Update({},guns,true,&sounds);
    cue(game::kOptionSelectSound,"a valid fresh gun calibration target emits select once");
    page.Update({},guns,true,&sounds);quiet("holding a gun calibration trigger does not repeat sound");
    page.Update(esc,guns,true,&sounds);cue(game::kOptionBackSound,"cancelling gun calibration emits back once");
    Feed(guns,a,.2,.2,0,1);guns.BeginFrame();
    page.Update(Click(1),guns,true,&sounds);cue(game::kOptionSelectSound,"opening physical binding capture emits select once");
    page.Update(esc,guns,true,&sounds);cue(game::kOptionBackSound,"cancelling gun binding capture emits back once");
    page.Update(Click(1),guns,true,&sounds);cue(game::kOptionSelectSound,"reopening binding capture emits select once");
    Feed(guns,a,.4,.4,16);page.Update({},guns,true,&sounds);
    cue(game::kOptionSelectSound,"saving a fresh physical binding emits select once");
    Feed(guns,a,.4,.4,0,16);guns.BeginFrame();
    page.Update(Click(5),guns,true,&sounds);cue(game::kOptionSelectSound,"entering the gun test emits select once");
    Feed(guns,a,.4,.4,16);page.Update({},guns,true,&sounds);
    quiet("live gun test shots do not produce extra menu select sounds");
    page.Update(esc,guns,true,&sounds);cue(game::kOptionBackSound,"leaving the gun test emits back once");
    page.Update(Click(8),guns,true,&sounds);cue(game::kOptionBackSound,"player Back row uses the back sound");
    page.Update(Click(7),guns,true,&sounds);cue(game::kOptionBackSound,"overview Back row uses the back sound");
    page.Update(esc,guns,true,&sounds);quiet("closed light-gun menu emits no sound");
    Feed(guns,a,.4,.4,0,16);guns.BeginFrame();page.Open();
    page.Update(Click(1),guns,true,&sounds);cue(game::kOptionSelectSound,"restarting gun assignment emits select once");
    Feed(guns,a,.4,.4,1);page.Update({},guns,true,&sounds);
    cue(game::kOptionSelectSound,"confirmed physical assignment emits select once");
    page.Update({},guns,true,&sounds);quiet("physical assignment acknowledgement does not repeat");
}
int main(){
    MenuAudio();
    using A=play::LightgunMenu::Action;
    input::Lightguns g;Feed(g,a,.2,.3,1);Feed(g,b,.7,.8,1);
    Check(g.Player(0).connected&&g.Player(1).connected,"legacy trigger order assigns two guns");
    Check(Near(g.Player(0).x,.2)&&Near(g.Player(1).x,.7),"independent aim");
    Feed(g,a,.2,.3,0,1);Feed(g,b,.7,.8,0,1);g.BeginFrame();
    g.BeginAssign(1);Feed(g,b,.7,.8,1);
    Check(g.TakeAssigned()&&g.settings.manual&&g.settings.player[1].device==b&&g.settings.player[0].device.empty(),"manual setup clears transient auto assignments");
    Check(!g.Player(1).held&&!g.Player(1).pressed,"assignment trigger is consumed until release");
    Feed(g,b,.7,.8,0,1);g.BeginAssign(0);Feed(g,a,.2,.3,1);Check(g.TakeAssigned(),"assign P1");
    Feed(g,a,.2,.3,0,1);g.BeginFrame();Feed(g,mouse,.4,.4,1);
    Check(g.settings.player[0].device==a&&g.settings.player[1].device==b,"ordinary mouse cannot steal assigned slots");
    g.RemoveDevice(a);Check(!g.Player(0).connected&&g.Player(1).connected,"P2 remains P2 on P1 disconnect");
    Feed(g,a,.2,.3,1);Check(g.Player(0).connected&&!g.Player(0).pressed&&!g.Player(0).held,"reconnect does not fire held trigger");
    Feed(g,a,.2,.3,0,1);g.BeginFrame();Feed(g,a,.2,.3,1);Feed(g,a,.2,.3,0,1);
    Check(g.Player(0).pressed&input::kTrigger,"short tap retained between game frames");
    g.BeginFrame();g.settings.player[0].Bind(0,2);
    Check(g.settings.player[0].buttons[1]==1,"duplicate button mappings swap");
    Feed(g,a,.2,.3,2);Check(g.Player(0).pressed&input::kTrigger,"remapped physical button fires");
    Feed(g,a,.2,.3,0,2);g.BeginFrame();
    g.SetImage(.125,0,.75,1);Feed(g,a,.125,.5);
    Check(Near(g.Reading(0).aim.x,0),"pillarbox maps to game image");
    Feed(g,a,.05,.5);Check(!g.Player(0).onScreen,"letterbox/pillarbox remains off screen");g.SetImage(0,0,1,1);
    std::array<input::AimPoint,4> points{{{.2,.2},{.8,.2},{.8,.8},{.2,.8}}};
    Check(g.settings.player[0].calibration.Fit(points),"four target calibration");Feed(g,a,.2,.2);
    Check(Near(g.Player(0).x,.15)&&Near(g.Player(0).y,.15),"calibrated aim applied");
    auto old=g.settings.player[0].calibration.h;points.fill({.5,.5});
    Check(!g.settings.player[0].calibration.Fit(points)&&old==g.settings.player[0].calibration.h,"degenerate calibration preserves previous result");
    g.SwapPlayers();Check(g.settings.player[1].device==a&&g.settings.player[1].buttons[0]==2&&g.settings.player[1].calibration.calibrated,"swapping players moves whole profile");g.SwapPlayers();
    g.BeginAssign(1);g.CancelAssign();Feed(g,mouse,.1,.1,0,1);Feed(g,mouse,.1,.1,1);
    Check(g.settings.player[1].device==b,"cancelled capture cannot assign next click");
    auto path=std::filesystem::temp_directory_path()/"hotd2re-lightgun-test.ini";
    Check(g.settings.Save(path.string()),"save profiles");input::LightgunSettings settings;
    Check(settings.Load(path.string())&&settings.manual&&settings.player[0].device==a&&settings.player[0].buttons[0]==2,"restore assignments and bindings");
    for(int i=0;i<9;++i)Check(Near(settings.player[0].calibration.h[i],old[i]),"restore calibration to subpixel precision");
    {std::ofstream f(path);f<<"manual=1\np1.device=bad path\np1.buttons=1 1 1 1\np1.calibration=0 0 0 0 0 0 0 0 0\n";}
    Check(settings.Load(path.string())&&settings.player[0].device.empty()&&settings.player[0].buttons[1]==2&&!settings.player[0].calibration.calibrated,"malformed preferences keep safe defaults");std::filesystem::remove(path);
    play::LightgunMenu menu;menu.Open();
    auto modern=Modern(menu,g,false);
    Check(modern.nightShiftPage==9&&HasText(modern,"LIGHT GUNS")&&HasText(modern,"Mouse")&&HasText(modern,"P1 /")&&HasText(modern,"P2 /"),"Night Shift overview shows input mode and both player slots");
    Check(HasText(modern,"Sinden border")&&HasText(modern,"ESC / RELOAD: BACK"),"Night Shift overview preserves border and return guidance");
    Check(menu.Update(Click(0),g,false)==A::GunMode,"mode selection requests runtime switch");
    menu.Update(Click(1),g,true);Check(g.Assigning()==0,"mouse opens assignment");
    modern=Modern(menu,g);
    Check(HasText(modern,"ASSIGN PLAYER 1")&&HasText(modern,"PULL THE TRIGGER")&&HasText(modern,"CANCEL ASSIGNMENT")&&!HasText(modern,"Swap players"),"Night Shift assignment has fresh-trigger and cancel guidance without active menu rows");
    play::MenuInput esc;esc.pressed=2;menu.Update(esc,g,true);Check(g.Assigning()<0,"Escape cancels assignment");
    Check(HasText(Modern(menu,g),"Assignment cancelled"),"Night Shift preserves assignment cancellation status");
    menu.Update(Click(3),g,true);Check(menu.Player()==0,"open player settings");
    modern=Modern(menu,g);
    Check(HasText(modern,"LIGHT GUN / PLAYER 1")&&HasText(modern,"Fire")&&HasText(modern,input::GunButtonName(g.settings.player[0].buttons[0]))&&HasText(modern,"Set"),"Night Shift player settings show current calibration and physical mappings");
    g.BeginFrame();menu.Update(Click(0),g,true);
    Check(menu.CalibrationTarget()==0,"open calibration");
    const input::AimPoint targets[4]{{.2,.2},{.8,.2},{.8,.8},{.2,.8}};
    for(int i=0;i<4;++i){
        modern=Modern(menu,g);
        const double tx=(i==0||i==3)?96:544,ty=i<2?72:408;
        Check(HasTarget(modern,tx,ty)&&HasText(modern,"target "+std::to_string(i+1)+" of 4"),"Night Shift calibration displays each actual corner target in sequence");
        bool clearCorner=true;for(const auto& panel:modern.menuPanels)if(panel.left<tx-14&&panel.right>tx+14&&panel.top<ty-14&&panel.bottom>ty+14)clearCorner=false;
        Check(clearCorner&&HasText(modern,"KEEP PREVIOUS CALIBRATION"),"Night Shift calibration leaves target corners unobscured and preserves cancel guidance");
        Feed(g,a,float(targets[i].x),float(targets[i].y),0,2);g.BeginFrame();menu.Update({},g,true);
        Feed(g,a,float(targets[i].x),float(targets[i].y),2);play::MenuInput physicalRight;physicalRight.reload=true;
        auto result=menu.Update(physicalRight,g,true);if(i==3)Check(result==A::Save,"calibration saves after four remapped trigger presses");g.BeginFrame();
    }
    Feed(g,a,.5,.5,0,2);menu.Update(Click(1),g,true);Feed(g,b,.5,.5,8);g.BeginFrame();
    modern=Modern(menu,g);
    Check(HasText(modern,"BIND PLAYER 1")&&HasText(modern,"Fire")&&HasText(modern,"Only this player's gun")&&HasText(modern,"KEEP PREVIOUS BUTTON"),"Night Shift binding identifies action, player and safe cancellation");
    Check(menu.Capturing(),"other player's button cannot change P1 mapping");
    Feed(g,a,.5,.5,16);Check(menu.Update({},g,true)==A::Save&&g.settings.player[0].buttons[0]==16,"capture selected player's fifth button");
    Feed(g,a,.5,.5,0,16);g.BeginFrame();menu.Update(Click(5),g,true);Check(menu.Testing(),"open safe test page");
    Feed(g,a,.2,.3,16);Feed(g,b,.7,.8,1);menu.Update({},g,true);
    Check(menu.TestShots(0)==1&&menu.TestShots(1)==1,"test counts each player's trigger separately");
    modern=Modern(menu,g);
    Check(HasText(modern,"LIGHT GUN TEST")&&HasText(modern,"PLAYER 1 / SHOTS: 1")&&HasText(modern,"PLAYER 2 / SHOTS: 1"),"Night Shift test displays separate live counters");
    for(int p=0;p<2;++p){const auto aim=g.Player(p);Check(HasTarget(modern,aim.x*640,aim.y*480),"Night Shift test aim uses each player's calibrated game coordinates");}
    g.BeginFrame();menu.Update({},g,true);Check(menu.TestShots(0)==1,"held trigger not counted twice");
    menu.Update(esc,g,true);Check(!menu.Testing()&&menu.IsOpen(),"test returns to setup");
    for(int row=0;row<9;++row)for(int dy:{9,20,34}){
        menu.Open();menu.Update(Click(3),g,true);auto hover=Click(row);hover.trigger=false;hover.py=menu.RowY(row)+dy;menu.Update(hover,g,true);
        Check(menu.Cursor()==row,"all player menu visible row hit areas align");
        modern=Modern(menu,g);bool selected=false;
        for(const auto& panel:modern.menuPanels)if(panel.color==0xCC8F2026&&Near(panel.top,menu.RowY(row)+8)&&Near(panel.bottom,menu.RowY(row)+35))selected=true;
        Check(selected,"Night Shift focus band matches every existing player row hit area");
    }
    {
        input::Lightguns preview;Feed(preview,a,.4,.4,1);Feed(preview,a,.4,.4,0,1);preview.BeginFrame();
        play::LightgunMenu page;page.Open();page.Update(Click(1),preview,false);
        Check(HasText(Modern(page,preview,false),"Select LIGHT GUNS input mode first"),"Night Shift keeps unavailable-mode error visible");
        page.Notice("Could not save gun settings - check folder access");
        Check(HasText(Modern(page,preview),"Could not save gun settings"),"Night Shift keeps save error visible");
        page.Update(Click(3),preview,true);page.Update(Click(5),preview,true);
        preview.Feed(a,-.2,.4,false,false,0,0);modern=Modern(page,preview);
        Check(HasText(modern,"OFF SCREEN")&&!HasTarget(modern,preview.Player(0).x*640,preview.Player(0).y*480),"Night Shift test reports offscreen aim without a false target");
        preview.RemoveDevice(a);modern=Modern(page,preview);
        Check(HasText(modern,"Disconnected - slot reserved"),"Night Shift preserves reserved disconnected player status");
        Feed(preview,a,.4,.4);modern=Modern(page,preview);
        Check(HasText(modern,"Light gun ready")&&HasText(modern,"ON SCREEN"),"Night Shift test updates reconnect status");
        const auto before=preview.Reading(0);const int cursor=page.Cursor();page.ModernHud(preview,true,modern);
        Check(before.sequence==preview.Reading(0).sequence&&before.pressed==preview.Reading(0).pressed&&cursor==page.Cursor(),"Night Shift presentation does not consume input or change menu state");
        page.Close(preview);game::WorldHud closed;page.ModernHud(preview,true,closed);Check(closed.nightShiftPage<0&&closed.menuText.empty()&&closed.menuPanels.empty(),"closed Night Shift device page emits no presentation");
    }
    input::Lightguns automatic;Feed(automatic,a,.5,.5,1);Feed(automatic,a,.5,.5,0,1);
    play::LightgunMenu configure;configure.Open();configure.Update(Click(3),automatic,true);configure.Update(Click(1),automatic,true);
    Feed(automatic,a,.5,.5,16);Check(configure.Update({},automatic,true)==A::Save&&automatic.settings.manual,"customizing an automatic assignment makes it persistent");
    automatic.BeginAssign(1);Feed(automatic,b,.5,.5,1);
    Check(automatic.settings.player[0].device==a&&automatic.settings.player[0].buttons[0]==16,"assigning second gun preserves customized first gun");
    play::PauseMenu pause;play::Settings volume;std::vector<uint32_t> sounds;pause.Open();play::MenuInput down;down.pressed=0x20;
    pause.Update(down,volume,sounds);
    play::MenuInput enter;enter.pressed=8;pause.Update(enter,volume,sounds);
    Check(pause.DisplayOpen()&&pause.IsOpen(),"pause opens shared Options without resuming");
    pause.Display().SetPage(play::DisplayMenu::kDevices);
    for(int row:play::DisplayMenu::Rows(play::DisplayMenu::kDevices))
        if(std::string(play::DisplayMenu::Label(row))=="Light Guns")pause.Display().FocusRow(row);
    pause.Update(enter,volume,sounds);
    Check(pause.Display().TakeDeviceAction()==play::DisplayMenu::DeviceAction::Lightguns&&pause.IsOpen(),
          "shared Devices category opens lightgun setup while the game remains paused");
#ifdef _WIN32
    input::Lightguns native;input::RawInput raw;raw.setup=&native;
    raw.deviceKeyProvider=[](HANDLE h){return h==reinterpret_cast<HANDLE>(1)?a:b;};
    raw.geometryProvider=[](){input::Geometry geo;geo.client=geo.primary=geo.virtualDesk={0,0,1000,1000};return geo;};
    RAWMOUSE report{};report.usFlags=MOUSE_MOVE_ABSOLUTE;report.lLastX=16384;report.lLastY=32768;report.usButtonFlags=RI_MOUSE_LEFT_BUTTON_DOWN;
    Check(!raw.ProcessMouse(nullptr,report),"synthetic zero device rejected");
    raw.ProcessMouse(reinterpret_cast<HANDLE>(1),report);
    Check(Near(native.Player(0).x,.25)&&Near(native.Player(0).y,.5),"actual RawInput adapter forwards absolute aim");
    report.lLastX=49152;raw.ProcessMouse(reinterpret_cast<HANDLE>(2),report);Check(Near(native.Player(1).x,.75),"RawInput adapter keeps two devices separate");
    native.BeginFrame();report.usFlags=0;report.lLastX=report.lLastY=0;report.usButtonFlags=RI_MOUSE_BUTTON_4_DOWN;raw.ProcessMouse(reinterpret_cast<HANDLE>(1),report);
    Check(native.Player(0).menuPressed==8&&Near(native.Player(0).x,.25),"button-only report preserves absolute aim and confirm mapping");
    raw.RemoveDevice(reinterpret_cast<HANDLE>(1));Check(!native.Player(0).connected&&native.Player(1).connected,"native removal preserves P2");
#endif
    puts("PASS light-gun assignment, two-player input, reconnect, mappings, calibration, persistence and setup menus");
}
