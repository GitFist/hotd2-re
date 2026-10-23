#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>
#include "game/exe_image.h"
#include "game/menu_audio.h"
#include "game/sound_table.h"
#include "game/world.h"

static void Require(bool ok,const char* why) {
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
static std::string Lower(std::string text) {
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char ch){return char(std::tolower(ch));});return text;
}
static bool Text(const game::World& world,const std::string& text) {
    for(const auto& t:world.Hud().menuText)if(t.text==text)return true;return false;
}
static void CheckPages(const char* dir,const game::SoundTable& table,bool modern) {
    game::World w;Require(w.Load(dir),"load Extras World");game::WorldConfig cfg;cfg.portFeatures=true;cfg.portModernMenu=modern;
    w.Boot(cfg);w.ForceMode(4,0x10000,0,0);
    auto step=[&](uint32_t keys=0,int x=630,int y=465,bool fire=false,int player=0) {
        game::WorldInput in[2];in[0].kind=in[1].kind=1;in[0].x=in[1].x=630;in[0].y=in[1].y=465;
        in[player].x=x;in[player].y=y;in[player].fired=fire;in[player].held=in[player].pressed=keys;
        w.SetInput(0,in[0]);w.SetInput(1,in[1]);w.Step();Require(w.GameMode()==4,"Extras never joins a player or starts gameplay");
    };
    auto silent=[&]{
        if(!w.Sounds().empty())for(const auto& sound:w.Sounds())
            std::fprintf(stderr,"unexpected navigation sound: modern=%d frame=%d page=%d kind=%d id=%X\n",modern,w.Frame(),w.Hud().nightShiftPage,sound.kind,sound.id);
        Require(w.Sounds().empty(),"navigation cannot issue playback, stop or feedback commands");
    };
    auto idle=[&]{step();silent();};
    auto key=[&](uint32_t keys){step(keys);silent();idle();};
    auto click=[&](int x,int y,int player=0){step(4,x,y,true,player);};
    auto event=[&](int kind){const auto& s=w.Sounds();Require(s.size()==1&&s[0].kind==kind,"activation emits one requested command");return s[0].id;};
    auto select=[&]{Require(event(game::World::kOptionFeedback)==game::kOptionSelectSound,"opening a destination emits Preview 74 once");};
    auto back=[&]{const auto& s=w.Sounds();Require(s.size()==2&&s[0].kind==game::World::kPreviewStop&&s[1].kind==game::World::kOptionFeedback&&s[1].id==game::kOptionBackSound,"Back stops previews once with Preview 76");};
    auto hub=[&] {
        Require(w.Hud().nightShiftPage==9&&Text(w,"EXTRAS")&&Text(w,"MUSIC PLAYER")&&Text(w,"SOUND EFFECTS")&&Text(w,"MODEL VIEWER")&&Text(w,"BACK"),"Extras opens a three-destination hub plus Back");
        Require(!Text(w,"PLAY")&&!Text(w,"38 TRACKS"),"hub exposes no hidden music controls or split lists");
        Require(w.Hud().menuScrollHints.empty(),"leaving a media list clears its scroll cues on the hub");
        for(const auto& p:w.Hud().menuPanels)Require(!(p.left==319&&p.right==321),"removed split-library divider stays absent");
    };
    auto scrollHint=[&](bool fx,bool above,bool below) {
        Require(w.Hud().menuScrollHints.size()==1,"only the active media list supplies a scroll cue");
        const auto& hint=w.Hud().menuScrollHints[0];
        Require(hint.left==24&&hint.top==139&&hint.right==616&&hint.bottom==(fx?387:389),"media scroll cues follow the list bounds, excluding playback footer");
        Require(hint.above==above&&hint.below==below,"media scroll cues describe only reachable offscreen rows");
    };
    auto selectedName=[&](bool fx) {
        for(const auto& p:w.Hud().menuPanels)if(p.left==24&&p.right==616&&p.color==0xCC8F2026)
            for(const auto& t:w.Hud().menuText)if(t.x==32&&t.y==p.top+(fx?4:12))return t.text;
        return std::string{};
    };
    for(int i=0;i<3;++i)step();  // menu initialization legitimately stops old sounds and starts its own music
    if(modern) {
        float online=-1,extras=-1,options=-1;for(const auto& t:w.Hud().menuText){if(t.x>=100)continue;if(t.text=="ONLINE")online=t.y;if(t.text=="EXTRAS")extras=t.y;if(t.text=="OPTIONS")options=t.y;}
        Require(online>=0&&online<extras&&extras<options,"Home keeps Extras between Online and Options");
        game::WorldInput a,b;a.kind=b.kind=1;a.x=b.x=100;a.y=175;b.y=322;b.fired=true;b.held=b.pressed=4;
        w.SetInput(0,a);w.SetInput(1,b);w.Step();Require(w.GameMode()==4,"P2 enters Extras despite P1 resting on Play");
    } else {for(int i=0;i<7;++i){step(0x20);step();}step(8);}  // Classic main-menu navigation keeps its original cursor cue
    idle();hub();const int viewer=w.Hud().portModelViewerRequests;
    click(520,78);silent();idle();Require(w.Hud().portModelViewerRequests==viewer,"old top-right viewer target is inert");
    w.ScrollPortExtras(1000,1);idle();hub();
    click(100,162,1);select();idle();
    Require(Text(w,"MUSIC PLAYER")&&Text(w,"38 TRACKS")&&!Text(w,"SOUND EFFECTS")&&!Text(w,"MODEL VIEWER"),"Music Player opens its own page");
    scrollHint(false,false,true);
    int play=0,pause=0,reset=0;for(const auto& t:w.Hud().menuText){play+=t.text=="PLAY";pause+=t.text=="PAUSE";reset+=t.text=="RESET";}
    Require(play==6&&pause==6&&reset==6,"each visible song retains Play/Pause/Reset controls");
    std::set<std::string> installed,played,names;
    for(const auto& file:std::filesystem::directory_iterator(std::filesystem::path(dir)/"Sound/BGM"))
        if(file.is_regular_file()&&Lower(file.path().extension().string())==".wav")installed.insert(Lower(file.path().filename().string()));
    Require(installed.size()==38,"fixture contains all 38 soundtrack files");
    std::vector<std::string> musicNames,musicFiles;uint32_t firstMusic=0,lastMusic=0;
    w.ScrollPortExtras(-10000,1);idle();
    for(int track=0;track<38;++track) {
        scrollHint(false,track>=6,track<37);
        const auto name=selectedName(false);char number[8];std::snprintf(number,sizeof(number),"%02d  ",track+1);
        Require(!name.empty()&&name.find("...")==std::string::npos&&name.rfind(number,0)==0&&names.insert(name).second,"each numbered soundtrack/version name is unique and fully visible");
        musicNames.push_back(name);step(8);lastMusic=event(game::World::kPreviewMusicStart);if(track==0)firstMusic=lastMusic;
        const auto request=game::ResolveSound(table,lastMusic);Require(request.action==game::SoundAction::PlayFile&&request.channel==game::kSndBgmChannel,"all songs resolve to playable music");
        auto portable=request.path;std::replace(portable.begin(),portable.end(),'\\','/');const std::filesystem::path path(portable);
        Require(std::filesystem::is_regular_file(std::filesystem::path(dir)/path),"actual soundtrack file exists");
        const auto file=Lower(path.filename().string());Require(played.insert(file).second,"music entries never duplicate a recording");musicFiles.push_back(file);idle();if(track<37)key(0x20);
    }
    Require(played==installed,"all 38 installed songs remain reachable exactly once");
    Require(musicFiles[0]=="st1.wav"&&musicFiles[16]=="st1_ar.wav"&&musicFiles[1]=="st2.wav"&&musicFiles[17]=="st2_ar.wav","original preview numbers preserve Standard/Alternate versions");
    Require(musicNames[0]=="01  Chapter 1 (Standard)"&&musicNames[16]=="17  Chapter 1 (Alternate)","soundtrack version labels remain explicit");
    key(0x20);step(8);Require(event(game::World::kPreviewMusicStart)==lastMusic,"music clamps after the final track");idle();
    scrollHint(false,true,false);
    w.ScrollPortExtras(-10000,1);idle();click(80,152);Require(event(game::World::kPreviewMusicStart)==firstMusic,"song name restarts its own recording");idle();
    scrollHint(false,false,true);
    click(400,160);Require(event(game::World::kPreviewMusicPlay)==firstMusic,"Play resumes its song");idle();
    click(480,160);Require(event(game::World::kPreviewMusicPause)==firstMusic,"Pause targets its song");idle();
    click(565,160);Require(event(game::World::kPreviewMusicReset)==firstMusic,"Reset rewinds and stops its song");idle();
    click(80,152);event(game::World::kPreviewMusicStart);idle();
    for(int kind:{game::World::kPreviewMusicPlay,game::World::kPreviewMusicPause,game::World::kPreviewMusicReset}){key(0x80);step(8);Require(event(kind)==firstMusic,"keyboard reaches selected-song playback controls");idle();}
    click(565,416);silent();idle();Require(selectedName(false).rfind("07  ",0)==0,"Next pages only the open music list");
    scrollHint(false,true,true);
    click(455,416);silent();idle();Require(selectedName(false).rfind("01  ",0)==0,"Previous returns to first music page");
    scrollHint(false,false,true);
    step(2);back();idle();hub();
    click(100,214,1);select();idle();Require(Text(w,"SOUND EFFECTS")&&!Text(w,"MUSIC PLAYER")&&!Text(w,"PLAY"),"Sound Effects opens its own page without music controls");
    scrollHint(true,false,true);
    click(400,147,1);const auto firstEffect=event(game::World::kPreviewEffect);idle();
    auto request=game::ResolveSound(table,firstEffect);Require(request.action==game::SoundAction::PlayFile&&request.channel!=game::kSndBgmChannel,"effects exclude stop commands and music");
    w.ScrollPortExtras(10000,0);idle();step(8);const auto lastEffect=event(game::World::kPreviewEffect);idle();
    scrollHint(true,true,false);
    Require(lastEffect!=firstEffect,"last effects remain reachable");request=game::ResolveSound(table,lastEffect);Require(request.action==game::SoundAction::PlayFile&&request.channel!=game::kSndBgmChannel,"last effect resolves to non-music playback");
    w.ScrollPortExtras(-10000,0);idle();click(400,147);Require(event(game::World::kPreviewEffect)==firstEffect,"wheel targets active media page regardless of old column argument");idle();
    scrollHint(true,false,true);
    click(60,416);back();idle();hub();click(100,162);select();idle();Require(selectedName(false).rfind("01  ",0)==0,"music selection is independent of effects browsing");
    step(2);back();idle();hub();click(100,266,1);
    Require(w.Sounds().size()==2&&w.Sounds()[0].kind==game::World::kPreviewStop&&w.Sounds()[1].kind==game::World::kOptionFeedback&&w.Sounds()[1].id==game::kOptionSelectSound,"Model Viewer stops previews and selects once");
    Require(w.Hud().portModelViewerRequests==viewer+1,"P2 opens Model Viewer once");idle();hub();step(8);Require(w.Hud().portModelViewerRequests==viewer+2,"keyboard opens selected Model Viewer destination");idle();hub();
    step(2);back();idle();Require(w.Hud().nightShiftPage==(modern?0:-1),"hub Back returns to themed Home");
    if(modern)click(100,322);else step(8);idle();hub();
    click(100,318);back();idle();Require(w.Hud().nightShiftPage==(modern?0:-1),"visible hub Back follows the same hierarchy");
}
int main(int argc,char** argv) {
    if(argc!=2){std::fprintf(stderr,"usage: extras_test <game-dir>\n");return 2;}
    game::ExeImage exe;Require(exe.Load((std::filesystem::path(argv[1])/"Hod2.exe").string()),"load original sound metadata");
    game::SoundTable table;Require(table.Load(exe),"load recording catalog");CheckPages(argv[1],table,true);CheckPages(argv[1],table,false);
    std::puts("PASS Extras pages: three-destination hub, all 38 songs, complete names/versions, playback controls, independent effects, scroll cue boundaries, two-player input, level-aware Back and Model Viewer requests in both themes");
}
