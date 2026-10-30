// Actual World menu navigation: game switch, Typing Home and saved Original popup.
#include "game/world.h"
#include "game/typing_profile.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
void Require(bool ok,const char* why) {if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
void Step(game::World& world,uint32_t keys=0,int x=630,int y=465,bool fired=false) {
    game::WorldInput input;input.kind=1;input.x=x;input.y=y;input.pressed=input.held=keys;input.fired=fired;
    world.SetInput(0,input);world.SetInput(1,{});world.Step();
}
void Click(game::World& world,int x,int y) {Step(world,4,x,y,true);Step(world,0,x,y);}
bool Text(const game::World& world,const std::string& value) {
    return std::any_of(world.Hud().menuText.begin(),world.Hud().menuText.end(),[&](const auto& text){return text.text==value;});
}
void Switch(game::World& world,bool typing) {
    Step(world,4,480,150,true);
    Require(world.Hud().menuSwitching&&world.Hud().menuSwitchFade==0&&world.Hud().typingHome!=typing,
        "game switch starts its fade before replacing the menu");
    for(int frame=1;frame<24;++frame) {
        Step(world,8);
        Require(world.Hud().typingHome!=typing&&world.Hud().nightShiftPage==0,"transition blocks confirmations before its midpoint");
        if(frame==6)Require(std::abs(world.Hud().menuSwitchFade-.15625f)<.00001f,"switch fade uses smoothstep easing");
    }
    Step(world,8);
    Require(world.Hud().typingHome==typing&&world.Hud().menuSwitchFade==1,"menu theme changes only at full black");
    for(int frame=25;frame<48;++frame) {
        Step(world,8);
        Require(world.Hud().typingHome==typing&&world.Hud().nightShiftPage==0,"transition blocks confirmations while fading in");
    }
    Step(world);
    Require(!world.Hud().menuSwitching&&world.Hud().menuSwitchFade==0,"game switch finishes after 48 frames");
}
void Home(game::World& world,const char* game,const game::WorldConfig& config) {
    std::string error;Require(world.Load(game,0,&error),error.c_str());world.Boot(config);world.ForceMode(4,0x10000,0,0);
    for(int frame=0;frame<3;++frame)Step(world);
}
}
int main(int argc,char** argv) {
    Require(argc==2,"usage: typing_home_menu_test <game-dir>");
    Require(std::getenv("HOTD2_TYPING_REFERENCE")!=nullptr,"set HOTD2_TYPING_REFERENCE for the Typing menu test");
    const auto file=std::filesystem::temp_directory_path()/
        ("hotd2-typing-menu-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sav");
    game::WorldConfig config;config.portFeatures=config.portModernMenu=true;config.savePath=file.string();
    game::World world;Home(world,argv[1],config);
    for(const char* label:{"PLAY","PRACTICE","LEADERBOARD","ONLINE","EXTRAS","OPTIONS","EXIT"})
        Require(Text(world,label),"HOTD2 sidebar retains its existing entries");
    for(const char* label:{"HOST ONLINE","BROWSE LOBBY","PEER TO PEER"})
        Require(!Text(world,label),"Home has no retired network shortcut buttons");
    Click(world,100,174);
    Require(world.Hud().nightShiftPage==1&&!Text(world,"TYPING OF THE DEAD"),"Typing is removed from HOTD2 Play");
    Step(world,2);Step(world);
    world.SetPortHomeRooms({{11111,false,true},{22222,true,false}},"WAITING");Step(world);
    Switch(world,true);
    for(const char* label:{"ORIGINAL","DRILL","LEADERBOARD","OPTIONS","EXIT"})
        Require(Text(world,label),"Typing Home has all five requested sidebar entries");
    for(const char* label:{"PLAY","PRACTICE","ONLINE","EXTRAS","ARCADE"})
        Require(!Text(world,label),"Typing Home contains only its own sidebar");
    Step(world,0,450,299);
    Require(world.Hud().nightShiftRow==6,"Typing room cards follow the five sidebar entries and game switch");
    world.SetPortHomeRooms({{22222,true,false},{11111,false,true}},"REFRESHED");Step(world,0,450,299);
    Require(world.Hud().nightShiftRow==7,"Typing directory refresh preserves the selected room identity");
    world.SetPortHomeRooms({},"EMPTY");Step(world,0,450,299);
    Require(world.Hud().nightShiftRow==5,"disappearing Typing room returns focus safely to the game switch");
    Click(world,100,174);
    Require(world.Hud().nightShiftPage==11&&Text(world,"ORIGINAL MODE"),"Original opens a modal over Typing Home");
    for(const char* label:{"DIFFICULTY","LIFE","CONTINUES","WORD SIZE","KEYBOARD TYPE","START"})
        Require(Text(world,label),"Original modal has its five settings and Start button");
    for(const char* label:{"START CHAPTER","BLOOD EFFECTS","SOUND TEST SE","SOUND TEST MUSIC","DISPLAY ADJUSTMENT","DRILL MODE"})
        Require(!Text(world,label),"Original modal has no retired extra settings");
    Click(world,450,165);Click(world,450,204);Click(world,450,243);Click(world,450,282);Click(world,450,321);
    const auto selected=world.PortTypingOptions();
    Require(selected.difficulty==game::TypingDifficulty::Hard&&selected.startingLives==1&&selected.continues==3&&
        selected.wordSize==game::TypingWordSize::Large&&selected.keyboard==game::TypingKeyboard::UK,
        "all five popup settings use their actual adjustment paths");
    Step(world,2);Step(world);
    Require(world.Hud().typingHome&&world.Hud().nightShiftPage==0,"Escape closes only the Original modal");
    Click(world,100,211);Require(world.Hud().nightShiftPage==12,"Typing Home opens Drill directly");
    Step(world,2);Step(world);Require(world.Hud().typingHome&&world.Hud().nightShiftPage==0,"Drill returns to Typing Home");
    Click(world,100,248);
    Require(world.Hud().nightShiftPage==3&&world.Hud().portLeaderboardTable==13,"Typing Home opens the Typing leaderboard");
    Require(Text(world,"SOURCE: LOCAL")&&Text(world,"BACK")&&!Text(world,"ARCADE")&&!Text(world,"BOSS MODE"),
        "Typing leaderboard has only its source selector and Back");
    const auto requests=world.Hud().portLeaderboardRequests;Click(world,100,146);
    Require(world.Hud().portLeaderboardShared&&world.Hud().portLeaderboardTable==13&&world.Hud().portLeaderboardRequests>requests,
        "Typing leaderboard source toggle requests its own online board");
    Step(world,2);Step(world);
    const auto optionsRequests=world.Hud().portDisplayRequests;Click(world,100,285);
    Require(world.Hud().typingHome&&world.Hud().portDisplayRequests==optionsRequests+1,"Typing Options uses the existing shared Options UI");
    world.SetPortModernMenu(false);Step(world);
    Require(world.Hud().typingHome&&world.Hud().nightShiftPage==0,"Typing Home retains Night Shift with the Classic preference");
    Switch(world,false);
    Require(!world.Hud().typingHome&&world.Hud().nightShiftPage<0&&!world.Hud().sprites.empty(),
        "reverse switch completes and restores the saved Classic presentation");
    world.SetPortModernMenu(true);Step(world);
    Require(Text(world,"PLAY")&&!Text(world,"DRILL"),"reverse game switch restores HOTD2 Home");
    game::TypingProfile progress;
    for(int chapter=1;chapter<=4;++chapter)progress.storeOriginalCoins(chapter,31);
    Require(progress.save(file.string()+".typing.profile"),"save earned progress that unlocks Free Play");
    game::World restored;Home(restored,argv[1],config);Switch(restored,true);Click(restored,100,174);
    const auto loaded=restored.PortTypingOptions();
    Require(loaded.difficulty==selected.difficulty&&loaded.startingLives==selected.startingLives&&loaded.continues==selected.continues&&
        loaded.wordSize==selected.wordSize&&loaded.keyboard==selected.keyboard,"all five settings survive a fresh World/profile load");
    Step(restored,0x40,450,243);Step(restored,0,450,243);
    Require(restored.PortTypingOptions().continues==0&&Text(restored,"FREE PLAY"),"unlocked zero-continue choice displays Free Play");
    game::World unlimited;Home(unlimited,argv[1],config);Switch(unlimited,true);Click(unlimited,100,174);
    Require(unlimited.PortTypingOptions().continues==0&&Text(unlimited,"FREE PLAY"),"unlocked Free Play survives a fresh saved-setting load");
    Click(unlimited,450,375);
    for(int frame=0;frame<20&&unlimited.GameMode()!=6;++frame)Step(unlimited);
    Require(unlimited.GameMode()==6&&unlimited.PortTypingActive()&&unlimited.Hud().credits==0,
        "Start below the settings launches the actual Typing campaign with unlimited credits");
    std::error_code ignored;
    for(const char* suffix:{"",".typing.ini",".typing.profile",".typing.drill"})std::filesystem::remove(file.string()+suffix,ignored);
    std::puts("PASS Typing Home: eased two-way switch, sidebar/lobbies, five saved modal settings, Drill, own leaderboard, shared Options and actual Start");
}
