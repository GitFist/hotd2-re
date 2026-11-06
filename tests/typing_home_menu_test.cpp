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
    { std::ofstream legacy(file.string()+".typing.ini"); legacy << "blood=0\n"; }
    game::World world;Home(world,argv[1],config);
    const auto hotdRules = world.GetPortGameOptions();
    for(const char* label:{"PLAY","PRACTICE","LEADERBOARD","ONLINE","EXTRAS","OPTIONS","EXIT"})
        Require(Text(world,label),"HOTD2 sidebar retains its existing entries");
    for(const char* label:{"HOST ONLINE","BROWSE LOBBY","PEER TO PEER"})
        Require(!Text(world,label),"Home has no retired network shortcut buttons");
    Click(world,100,174);
    Require(world.Hud().nightShiftPage==1&&!Text(world,"TYPING OF THE DEAD"),"Typing is removed from HOTD2 Play");
    Step(world,2);Step(world);
    world.SetPortHomeRooms({{11111,false,true},{22222,true,false}},"WAITING");Step(world);
    Switch(world,true);
    const auto initialTyping = world.GetPortGameOptions();
    game::World arcade;Home(arcade,argv[1],config);Switch(arcade,true);
    Click(arcade,100,174);
    Require(Text(arcade,"ARCADE MODE"),"Arcade opens its own launch dialog");
    Click(arcade,450,375);
    for(int frame=0;frame<1800;++frame)Step(arcade);
    Require(arcade.PortTypingActive() && arcade.Hud().canShoot &&
            std::none_of(arcade.Draws().begin(),arcade.Draws().end(),[](const auto& draw){return draw.fn==0x004ACB40;}),
            "Home launches actual Arcade gameplay without Original inventory");
    Require(initialTyping.typing && initialTyping.editable && world.PortTypingOptions().bloodEffects,
        "shared Options loads Typing settings directly and ignores the retired hidden blood toggle");
    for(const char* label:{"ARCADE","ORIGINAL","DRILL","LEADERBOARD","OPTIONS","EXIT"})
        Require(Text(world,label),"Typing Home has all six sidebar entries");
    for(const char* label:{"PLAY","PRACTICE","ONLINE","EXTRAS"})
        Require(!Text(world,label),"Typing Home contains only its own sidebar");
    Step(world,0,450,299);
    Require(world.Hud().nightShiftRow==7,"Typing room cards follow the six sidebar entries and game switch");
    world.SetPortHomeRooms({{22222,true,false},{11111,false,true}},"REFRESHED");Step(world,0,450,299);
    Require(world.Hud().nightShiftRow==8,"Typing directory refresh preserves the selected room identity");
    world.SetPortHomeRooms({},"EMPTY");Step(world,0,450,299);
    Require(world.Hud().nightShiftRow==6,"disappearing Typing room returns focus safely to the game switch");
    Click(world,100,211);
    Require(world.Hud().nightShiftPage==11&&Text(world,"ORIGINAL MODE"),"Original opens a modal over Typing Home");
    for(const char* label:{"DIFFICULTY","LIFE","CONTINUES","WORD SIZE","KEYBOARD TYPE","START"})
        Require(Text(world,label),"Original modal has its five settings and Start button");
    for(const char* label:{"START CHAPTER","BLOOD EFFECTS","SOUND TEST SE","SOUND TEST MUSIC","DISPLAY ADJUSTMENT","DRILL MODE"})
        Require(!Text(world,label),"Original modal has no retired extra settings");
    Click(world,450,165);Click(world,450,204);Click(world,450,243);
    Require(world.PortTypingOptions().continues==0&&Text(world,"UNLIMITED"),"Typing popup offers Unlimited before any unlocks");
    Click(world,450,243);Click(world,450,282);Click(world,450,321);
    const auto selected=world.PortTypingOptions();
    Require(selected.difficulty==game::TypingDifficulty::Hard&&selected.startingLives==1&&selected.continues==3&&
        selected.wordSize==game::TypingWordSize::Large&&selected.keyboard==game::TypingKeyboard::UK,
        "all five popup settings use their actual adjustment paths");
    Step(world,2);Step(world);
    Require(world.Hud().typingHome&&world.Hud().nightShiftPage==0,"Escape closes only the Original modal");
    Click(world,100,248);Require(world.Hud().nightShiftPage==12,"Typing Home opens Drill directly");
    Step(world,2);Step(world);Require(world.Hud().typingHome&&world.Hud().nightShiftPage==0,"Drill returns to Typing Home");
    Click(world,100,285);
    Require(world.Hud().nightShiftPage==3&&world.Hud().portLeaderboardTable==13,"Typing Home opens the Typing leaderboard");
    Require(Text(world,"SOURCE: LOCAL")&&Text(world,"BACK")&&!Text(world,"ARCADE")&&!Text(world,"BOSS MODE"),
        "Typing leaderboard has only its source selector and Back");
    const auto requests=world.Hud().portLeaderboardRequests;Click(world,100,146);
    Require(world.Hud().portLeaderboardShared&&world.Hud().portLeaderboardTable==13&&world.Hud().portLeaderboardRequests>requests,
        "Typing leaderboard source toggle requests its own online board");
    Step(world,2);Step(world);
    const auto optionsRequests=world.Hud().portDisplayRequests;Click(world,100,322);
    Require(world.Hud().typingHome&&world.Hud().portDisplayRequests==optionsRequests+1,"Typing Options uses the existing shared Options UI");
    auto shared = world.GetPortGameOptions();
    Require(shared.typing && shared.editable && shared.difficulty == int(selected.difficulty) &&
            shared.lives == selected.startingLives && shared.credits == selected.continues,
            "shared Gameplay reads actual Typing rules instead of HOTD preferences");
    auto invalid = shared; invalid.lives = 2;
    Require(!world.SetPortGameOptions(invalid), "Typing rejects life counts unavailable in Original mode");
    invalid = shared; invalid.lives = 5;
    Require(!world.SetPortGameOptions(invalid), "shared Gameplay cannot bypass the five-lives unlock");
    invalid = shared; invalid.credits = 9;
    Require(!world.SetPortGameOptions(invalid), "shared Gameplay cannot bypass the nine-continues unlock");
    auto noLimit = shared; noLimit.credits = 0;
    Require(world.SetPortGameOptions(noLimit), "shared Typing Gameplay allows Unlimited without progression");
    game::World noLimitReload; Home(noLimitReload,argv[1],config); Switch(noLimitReload,true);
    Require(noLimitReload.GetPortGameOptions().credits == 0, "Typing Unlimited survives a fresh load without unlocks");
    Click(noLimitReload,100,211);
    Require(Text(noLimitReload,"UNLIMITED") && noLimitReload.PortTypingOptions().continues == 0,
            "opening Original preserves the shared Unlimited preference");
    Click(noLimitReload,450,375);
    for(int frame=0;frame<20&&noLimitReload.GameMode()!=6;++frame)Step(noLimitReload);
    for(int frame=0;frame<3;++frame)Step(noLimitReload);  // initialize the joined player after the stage transition
    Require(noLimitReload.GameMode()==6 && noLimitReload.PortTypingActive() && noLimitReload.Hud().credits==0 &&
            noLimitReload.Hud().player[0].state==5, "Typing starts with Unlimited on a profile without completion unlocks");
    auto changed = shared; changed.difficulty = 4; changed.lives = 3; changed.credits = 5;
    Require(world.SetPortGameOptions(changed), "shared Gameplay persists Typing rules");
    const auto rules = world.PortTypingOptions();
    Require(rules.difficulty == game::TypingDifficulty::VeryHard && rules.startingLives == 3 && rules.continues == 5 &&
            rules.wordSize == selected.wordSize && rules.keyboard == selected.keyboard && rules.bloodEffects,
            "shared changes reach actual Typing options and preserve word size, keyboard and blood");
    game::World changedReload; Home(changedReload,argv[1],config); Switch(changedReload,true);
    const auto fresh = changedReload.GetPortGameOptions();
    Require(fresh.typing && fresh.difficulty == 4 && fresh.lives == 3 && fresh.credits == 5 && changedReload.PortTypingOptions().bloodEffects,
            "shared Typing changes survive restarting before ever opening Original settings");
    Click(world,100,211);
    Require(Text(world,"VERY HARD") && world.PortTypingOptions().startingLives == 3,
            "Original popup reflects changes from shared Gameplay");
    Step(world,2);Step(world);
    Require(world.ResetPortGameOptions() && world.PortTypingOptions().difficulty == game::TypingDifficulty::Normal &&
            world.PortTypingOptions().wordSize == selected.wordSize && world.PortTypingOptions().keyboard == selected.keyboard,
            "shared reset restores Typing rules without touching its keyboard or word size");
    Require(world.SetPortGameOptions(shared), "restore selected Typing rules for the remaining campaign checks");
    { std::ifstream persisted(file.string()+".typing.ini"); const std::string contents((std::istreambuf_iterator<char>(persisted)),{});
      Require(contents.find("blood=") == std::string::npos, "saving migrates away the retired blood preference"); }
    world.SetPortModernMenu(false);Step(world);
    Require(world.Hud().typingHome&&world.Hud().nightShiftPage==0,"Typing Home retains Night Shift with the Classic preference");
    Switch(world,false);
    Require(!world.Hud().typingHome&&world.Hud().nightShiftPage<0&&!world.Hud().sprites.empty(),
        "reverse switch completes and restores the saved Classic presentation");
    world.SetPortModernMenu(true);Step(world);
    Require(Text(world,"PLAY")&&!Text(world,"DRILL"),"reverse game switch restores HOTD2 Home");
    const auto hotdAfter = world.GetPortGameOptions();
    Require(!hotdAfter.typing && hotdAfter.difficulty == hotdRules.difficulty && hotdAfter.lives == hotdRules.lives &&
            hotdAfter.credits == hotdRules.credits && hotdAfter.crosshair[0] == hotdRules.crosshair[0] && hotdAfter.crosshair[1] == hotdRules.crosshair[1],
            "Typing edits and resets leave HOTD gameplay preferences unchanged");
    game::TypingProfile progress;
    for(int chapter=1;chapter<=4;++chapter)progress.storeOriginalCoins(chapter,31);
    Require(progress.save(file.string()+".typing.profile"),"save earned progress that unlocks Free Play");
    game::World restored;Home(restored,argv[1],config);Switch(restored,true);
    const auto unlocked = restored.GetPortGameOptions();
    Require(unlocked.freePlayUnlocked && (unlocked.livesMask & (1u << 5)) && (unlocked.creditsMask & (1u << 9)),
            "shared Gameplay exposes earned Typing unlocks before opening Original");
    Click(restored,100,211);
    const auto loaded=restored.PortTypingOptions();
    Require(loaded.difficulty==selected.difficulty&&loaded.startingLives==selected.startingLives&&loaded.continues==selected.continues&&
        loaded.wordSize==selected.wordSize&&loaded.keyboard==selected.keyboard,"all five settings survive a fresh World/profile load");
    Step(restored,0x40,450,243);Step(restored,0,450,243);
    Require(restored.PortTypingOptions().continues==0&&Text(restored,"UNLIMITED"),"zero-continue choice displays Unlimited");
    game::World unlimited;Home(unlimited,argv[1],config);Switch(unlimited,true);Click(unlimited,100,211);
    Require(unlimited.PortTypingOptions().continues==0&&Text(unlimited,"UNLIMITED"),"Unlimited survives a fresh saved-setting load");
    Click(unlimited,450,375);
    for(int frame=0;frame<20&&unlimited.GameMode()!=6;++frame)Step(unlimited);
    Require(unlimited.GameMode()==6&&unlimited.PortTypingActive()&&unlimited.Hud().credits==0,
        "Start below the settings launches the actual Typing campaign with unlimited credits");
    const auto running = unlimited.GetPortGameOptions();
    Require(!running.editable && !unlimited.SetPortGameOptions(shared) && !unlimited.ResetPortGameOptions(),
            "Typing rules remain locked during a running campaign");
    game::WorldConfig blockedConfig = config; blockedConfig.savePath += ".blocked";
    std::filesystem::create_directory(blockedConfig.savePath+".typing.ini");
    game::World blocked; Home(blocked,argv[1],blockedConfig); Switch(blocked,true);
    auto failedChange = blocked.GetPortGameOptions(); failedChange.difficulty = 4;
    Require(!blocked.SetPortGameOptions(failedChange) && blocked.PortTypingOptions().difficulty == game::TypingDifficulty::Normal,
            "a failed Typing save reports failure and restores the live preference");
    std::error_code ignored;
    for(const char* suffix:{"", ".typing.ini", ".typing.profile", ".typing.drill"})
        std::filesystem::remove(blockedConfig.savePath+suffix,ignored);
    for(const char* suffix:{"",".typing.ini",".typing.profile",".typing.drill"})std::filesystem::remove(file.string()+suffix,ignored);
    std::puts("PASS Typing Home: eased two-way switch, sidebar/lobbies, five saved modal settings, Drill, own leaderboard, shared Options and actual Start");
}
