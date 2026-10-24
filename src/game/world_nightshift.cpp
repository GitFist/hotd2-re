#include "game/world_nightshift.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include "game/menu_text.h"
#include "game/menu_audio.h"
#include "game/world_modes.h"

namespace game {
namespace {
struct Row { const char* label; int action; };
enum { kPlay=100, kPractice, kLeaderboard, kOnline, kOptions, kExit, kHome, kHorde, kRandomizer, kScene,
       kHost, kBrowse, kPeer, kArcadeBoard, kOriginalBoard, kHordeBoard, kBossBoard, kBossPrev, kBossNext, kBoardSource, kPublish,
       kHomeRoom, kExtras };
constexpr Row kHomeRows[]={{"PLAY",kPlay},{"PRACTICE",kPractice},{"LEADERBOARD",kLeaderboard},{"ONLINE",kOnline},{"EXTRAS",kExtras},{"OPTIONS",kOptions},{"EXIT",kExit}};
constexpr int kHomeCount = int(std::size(kHomeRows));
constexpr Row kPlayRows[]={{"ARCADE",0},{"ORIGINAL",1},{"CHALLENGES",2},{"BOSS MODE",3},{"HORDE MODE",kHorde},{"RANDOMIZER",kRandomizer},{"BACK",kHome}};
constexpr Row kPracticeRows[]={{"SCENE SELECT",kScene},{"BACK",kHome}};
constexpr Row kBoardRows[]={{"SOURCE: LOCAL",kBoardSource},{"ARCADE",kArcadeBoard},{"ORIGINAL",kOriginalBoard},{"HORDE",kHordeBoard},{"BOSS MODE",kBossBoard},{"PUBLISH NEW RUN",kPublish},{"BACK",kHome}};
constexpr const char* kBoss[] = {"JUDGMENT","HIEROPHANT A","HIEROPHANT B","TOWER A","TOWER B","STRENGTH A","STRENGTH B","MAGICIAN","EMPEROR","BOSS RUSH"};
constexpr char kOnlineNameKeys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-";
constexpr size_t kOnlineNameLimit = 12;
std::string TrimOnlineName(const std::string& name) {
    const auto first=name.find_first_not_of(' ');
    if(first==std::string::npos)return {};
    return name.substr(first,name.find_last_not_of(' ')-first+1);
}
void EditOnlineName(std::string& name,const std::string& text,bool backspace) {
    if(backspace&&!name.empty())name.pop_back();
    for(char c:text) {
        if(c>='a'&&c<='z')c=char(c-'a'+'A');
        if(c&&std::strchr(kOnlineNameKeys,c)&&name.size()<kOnlineNameLimit)name+=c;
    }
}

void Panel(World::Impl& w,float l,float t,float r,float b,uint32_t color) { w.hud.menuPanels.push_back({l,t,r,b,color}); }
void Text(World::Impl& w,float x,float y,const std::string& s,float scale=0.72f,uint32_t color=0xFFEDE9DF) {
    w.hud.menuText.push_back({x,y,s,scale,color});
    const size_t first=w.hudSprites.size();
    MenuTextScaled(w.exe,x,y-12*scale,s,0,scale,w.hudSprites);
    for(size_t i=first;i<w.hudSprites.size();++i) {
        auto& d=w.hudSprites[i];
        d.flags|=0x700; d.depth=0.01f; d.tinted=true;
        d.tint[0]=((color>>16)&255)/255.f; d.tint[1]=((color>>8)&255)/255.f; d.tint[2]=(color&255)/255.f;
        d.alpha=((color>>24)&255)/255.f;
    }
    // Match the native font's black shadow when system fonts are unavailable.
    const size_t end=w.hudSprites.size();
    const float offset=std::clamp(scale*1.75f,1.25f,2.75f);
    for(size_t i=first;i<end;++i) {
        auto shadow=w.hudSprites[i];
        shadow.x+=offset;shadow.y+=offset;shadow.depth=.012f;
        shadow.tint[0]=shadow.tint[1]=shadow.tint[2]=0;
        shadow.alpha*=230.f/255.f;
        w.hudSprites.push_back(shadow);
    }
}
void Button(World::Impl& w,float l,float t,float r,float b,const char* label,bool selected,float scale=0.92f,bool current=false) {
    if(selected) { Panel(w,l,t,r,b,0xED8F2026); Panel(w,l,t,l+3,b,0xFFF0DBBD); }
    else Panel(w,l,t,r,b,0x75101315);
    if(current&&!selected)Panel(w,l+14,b-4,r-14,b-2,0xFFB9AA91);
    Text(w,l+14,t+(b-t-20*scale)*.5f,label,scale,selected?0xFFFFF4E5:current?0xFFCFBC9F:0xFFD1CCC2);
}
bool Hit(const WorldInput& in,float l,float t,float r,float b) {
    return in.kind==1&&!in.offscreen&&in.x>=l&&in.x<r&&in.y>=t&&in.y<b;
}
void LoadSprites(World::Impl& w,size_t first) {
    for(size_t i=first;i<w.hudSprites.size();++i) {
        const auto file=w.exe.U32(0x0057A5BC+uint32_t(w.hudSprites[i].id)*4);
        if(file<w.texFiles.size()&&w.texFiles[file]!=4)w.ProcessRequest({0x54,file});
    }
}
void Logo(World::Impl& w,bool small) {
    const auto logo=TitleLogoSprites();
    const float scale=small?.18f:.36f;
    for(size_t i=0;i<5&&i<logo.size();++i) {
        auto d=logo[i]; d.x=22+(d.x-36)*scale; d.y=small?15.f:25.f; d.sx=d.sy=scale;
        d.depth=.01f; d.flags|=0x700; w.hudSprites.push_back(d);
    }
}
void Confirm(World::Impl& w,int action) {
    auto& f=w.front;
    auto page=[&](int p) {
        const bool optionsBack=(f.nightShiftPage==5||f.nightShiftPage==9)&&p==0;
        if(f.nightShiftPage==9&&p!=9)w.SoundCall(World::kPreviewStop);
        f.nightShiftPage=p; f.nightShiftRow=0;
        if(p==5||p==9||optionsBack) w.SoundCall(World::kOptionFeedback, optionsBack?kOptionBackSound:kOptionSelectSound);
        else w.Sound(0x121A9);
    };
    switch(action) {
    case kPlay: page(1); return;
    case kPractice: page(2); return;
    case kExtras:
        w.SoundStopAll();page(9);f.extrasPage=f.extrasColumn=f.extrasControl=0;
        for(int p=0;p<2;++p){f.extrasAimX[p]=w.input[p].x;f.extrasAimY[p]=w.input[p].y;}
        return;
    case kLeaderboard: page(3); if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kHome: page(0); return;
    case kExit: f.quit=true; return;
    case kArcadeBoard: f.nightShiftTable=0; if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kOriginalBoard: f.nightShiftTable=1; if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kHordeBoard: f.nightShiftTable=2; if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kBossBoard: if(f.nightShiftTable<3)f.nightShiftTable=3; if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kBossPrev: f.nightShiftTable=3+(f.nightShiftTable-3+9)%10; if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kBossNext: f.nightShiftTable=3+(f.nightShiftTable-3+1)%10; if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kBoardSource: f.nightShiftShared=!f.nightShiftShared; if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kPublish: if(f.leaderboardPublishable) {f.publishCursor=0;page(4);} return;
    case kOnline: case kHost: case kBrowse: case kPeer:
        f.portLobbyRoom=0;
        f.portLobbyAction=action==kHost?1:action==kBrowse?2:action==kPeer?3:0;
        ++f.portLobbyRequests;
        ModesMenuConfirm(w,6); return;
    case kHomeRoom: {
        const int i=f.nightShiftRow-kHomeCount;
        if(i<0||i>=int(f.homeRooms.size()))return;
        f.portLobbyRoom=f.homeRooms[i].id;f.portLobbyAction=4;++f.portLobbyRequests;
        ModesMenuConfirm(w,6);return;
    }
    case kOptions: ModesMenuConfirm(w,5);return;
    default: break;
    }
    // Keep the same player-join path, credits and unlocks as the original mode handlers.
    f.menuCursor=action>=0&&action<=3?action:0;
    w.FrontScreensFree();
    if(action==kScene) { SceneSelectMenuConfirm(w); return; }
    if(action==kHorde) { HordeMenuConfirm(w); return; }
    if(action==kRandomizer) { ModesMenuConfirm(w,1); f.randomizerStart=true; return; }
    if(action!=0) { ModesMenuConfirm(w,action); return; }
    f.menuConfirmed=true; f.gameType=0;
    w.cfg.freePlay=w.cfg.creditSetting<0;
    w.cfg.credits=w.cfg.freePlay?0:w.cfg.creditSetting+1;
    w.Sound(0x121A9);
}
std::string Score(int n) { char s[32]; snprintf(s,sizeof(s),"%d",std::max(0,n));return s; }
std::string Time(int n) { char s[32]; n=std::max(n,0);snprintf(s,sizeof(s),"%02dM%02dS%02d",n/3600,(n/60)%60,(n%60)*100/60);return s; }
void Leaderboard(World::Impl& w) {
    const int table=std::clamp(w.front.nightShiftTable,0,12);
    Panel(w,201,78,618,429,0xE10B1012);
    const char* title=table==0?"ARCADE":table==1?"ORIGINAL":table==2?"HORDE":kBoss[table-3];
    Text(w,216,88,title,.95f);
    Text(w,216,116,w.front.nightShiftShared?"ONLINE LEADERBOARD":"LOCAL LEADERBOARD",.48f,0xFFA6ACA8);
    if(table>=3) {
        Button(w,535,86,566,115,"<",false,.65f);
        Button(w,574,86,605,115,">",false,.65f);
    }
    Panel(w,216,143,603,144,0xFF726B58);
    const bool shared=w.front.nightShiftShared;
    Text(w,218,152,"NO",.55f,0xFFB9AA91);
    if(shared) {
        Text(w,250,152,"NAME",.50f,0xFFB9AA91);
        if(table==2)Text(w,390,152,"RD",.45f,0xFFB9AA91);
        Text(w,table==2?436:398,152,table>=3?"TIME M S CS":"SCORE",table>=3?.40f:.50f,0xFFB9AA91);
        Text(w,532,152,table>=3||table==2?"LEVEL":"STAGE",.45f,0xFFB9AA91);
        Text(w,584,152,"PL",.45f,0xFFB9AA91);
    } else {
        Text(w,256,152,table==2?"ROUND":"NAME",.55f,0xFFB9AA91);
        Text(w,360,152,table>=3?"TIME M S CS":"SCORE",table>=3?.43f:.55f,0xFFB9AA91);
        Text(w,505,152,table>=3||table==2?"LEVEL":"STAGE",.55f,0xFFB9AA91);
        if(table==2)Text(w,570,152,"PL",.55f,0xFFB9AA91);
    }
    if(shared&&(w.front.sharedTable!=table||w.front.sharedLeaderboard.empty())) {
        std::string status=w.front.sharedTable==table?w.front.sharedLeaderboardStatus:"LOADING...";
        if(status.empty()||status=="COMMUNITY RUNS / UNVERIFIED")status="NO ONLINE RECORDS YET";
        Text(w,234,221,status.substr(0,32),.62f);
    } else if(table==2&&w.front.hordeLeaderboard.empty()&&!shared) {
        Text(w,234,221,"NO HORDE RUNS SAVED",.66f);
        Text(w,234,252,"PLAY HORDE TO SET A RECORD",.52f,0xFFA6ACA8);
    } else for(int i=0;i<10;++i) {
        const float y=181+i*23.f;
        Panel(w,216,y+20,603,y+21,0x251B282A);
        Text(w,218,y,Score(i+1),.62f);
        if(shared) {
            if(i>=int(w.front.sharedLeaderboard.size()))break;
            const auto& e=w.front.sharedLeaderboard[i];
            Text(w,250,y,e.name.substr(0,kOnlineNameLimit),.52f);
            if(table==2)Text(w,390,y,Score(e.rounds),.45f);
            Text(w,table==2?436:398,y,table>=3?Time(e.score):Score(e.score),table==2?.45f:.55f);
            Text(w,532,y,Score(e.difficulty+1),.55f);
            Text(w,584,y,Score(std::clamp(e.players,1,2)),.55f);
        } else if(table==2) {
            if(i>=int(w.front.hordeLeaderboard.size()))break;
            const auto& e=w.front.hordeLeaderboard[i];
            Text(w,256,y,Score(e.rounds),.62f);
            Text(w,360,y,Score(e.score),.62f);
            Text(w,505,y,Score(e.difficulty+1),.62f);
            Text(w,574,y,Score(e.players),.62f);
        } else {
            const int nativeTable=table<2?table:table-1;
            const auto& e=w.modes->tables[nativeTable][i];
            std::string name;
            for(int c=0;c<3&&e.name[c];++c)name+=e.name[c]>=32&&e.name[c]<127?e.name[c]:'?';
            if(name.find_first_not_of(' ')==std::string::npos)name="---";
            Text(w,256,y,name,.62f);
            Text(w,360,y,table>=3?Time(e.score):Score(e.score),.62f);
            Text(w,505,y,Score(int(e.b5)+1),.62f);
        }
    }
    if(shared)Text(w,216,416,"COMMUNITY RUNS / UNVERIFIED",.43f,0xFFB9AA91);
}

// The original music-preview table has 38 songs after its Stop entry, although
// the original selector wraps at 18. Retain those numbers and name both versions
// explicitly. Effects retain their original preview numbers and file names.
void ExtrasCatalog(World::Impl& w) {
    auto& f=w.front;
    if(f.extrasLoaded)return;
    f.extrasLoaded=true;
    auto name=[](const std::string& file) {
        auto stem=std::filesystem::path(file).stem().string();
        std::replace(stem.begin(),stem.end(),'_',' ');
        return stem;
    };
    auto exists=[&](const char* folder,const std::string& file) {
        std::error_code ec;
        return !file.empty()&&std::filesystem::is_regular_file(std::filesystem::path(w.gameDir)/"Sound"/folder/file,ec);
    };
    static constexpr const char* musicNames[]={
        "Chapter 1 (Standard)","Chapter 2 (Standard)","Chapter 3 (Standard)",
        "Chapter 4 (Standard)","Chapter 5 (Standard)","Chapter 6 (Standard)",
        "Boss Battle (Standard)","Chapter 5 Boss 1 (Standard)","Chapter 5 Boss 2 (Standard)",
        "Chapter 6 Boss 1 (Standard)","Chapter 6 Boss 2 (Standard)","Chapter Clear (Standard)",
        "Long Ending (Standard)","Short Ending (Standard)","Name Entry (Standard)","Game Over",
        "Chapter 1 (Alternate)","Chapter 2 (Alternate)","Chapter 3 (Alternate)",
        "Chapter 4 (Alternate)","Chapter 5 (Alternate)","Chapter 6 (Alternate)",
        "Boss Battle (Alternate)","Chapter 5 Boss 1 (Alternate)","Chapter 5 Boss 2 (Alternate)",
        "Chapter 6 Boss 1 (Alternate)","Chapter 6 Boss 2 (Alternate)","Chapter Clear (Alternate)",
        "Long Ending (Alternate)","Short Ending (Alternate)","Name Entry (Alternate)","Attract Mode",
        "Boss Mode","Rankings","Challenges","Item Selection","Clear Jingle","House of the Dead 1 Intro"
    };
    for(size_t i=0;i<std::size(musicNames);++i) {
        const uint32_t id=w.exe.U32(0x005970C4+4*uint32_t(i+1));
        if((id>>28)!=1||(id&0xFFF)>=41)continue;
        const auto ptr=w.exe.U32(0x00580354+4*(id&0xFFF));
        const auto file=ptr?w.exe.CStr(ptr):std::string{};
        if(exists("BGM",file)) {
            char number[8];snprintf(number,sizeof(number),"%02u  ",unsigned(i+1));
            f.extrasMusic.push_back({id,number+std::string(musicNames[i])});
        }
    }
    std::unordered_map<uint32_t,std::string> effects;
    for(int i=0;i<1000;++i) {
        const uint32_t addr=0x005845F8+i*0x34,id=w.exe.U32(addr);
        if(id==0xFFFF)break;
        effects.emplace(id,w.exe.CStr(addr+4,48));
    }
    for(int i=0;i<751;++i) {
        const uint32_t id=w.exe.U32(0x00569798+i*8);
        std::string file;
        const char* folder="SE";
        if((id>>28)==0&&id!=0&&id!=0x100A0) {
            const auto it=effects.find(id);
            if(it!=effects.end())file=it->second;
        } else if((id>>28)==2&&(id&0xFFF)<467) {
            const uint32_t addr=0x00580448+(id&0xFFF)*0x24;
            if(w.exe.S16(addr+2)!=-1)file=w.exe.CStr(addr+4,32);
            folder="Voice";
        }
        if(exists(folder,file)) {
            char number[12];snprintf(number,sizeof(number),"%03d  ",i);
            f.extrasEffects.push_back({id,number+name(file)});
        }
    }
}

void ExtrasHub(World::Impl& w) {
    auto& f=w.front;
    constexpr const char* names[]={"MUSIC PLAYER","SOUND EFFECTS","MODEL VIEWER","BACK"};
    constexpr const char* descriptions[]={"PLAY THE COMPLETE SOUNDTRACK", "BROWSE EFFECTS AND VOICE RECORDINGS",
                                           "VIEW CHARACTERS AND THEIR ANIMATIONS", "RETURN TO THE MAIN MENU"};
    constexpr float top=140, pitch=52, height=44;
    int action=-1;
    bool shot=false;
    for(int p=0;p<2;++p) {
        const auto& in=w.input[p];
        if(in.kind!=1)continue;
        const bool moved=in.x!=f.extrasAimX[p]||in.y!=f.extrasAimY[p];
        f.extrasAimX[p]=in.x;f.extrasAimY[p]=in.y;
        shot|=in.fired;
        if(!moved&&!in.fired)continue;
        for(int row=0;row<4;++row)if(Hit(in,24,top+row*pitch,322,top+row*pitch+height)) {
            f.extrasColumn=row;
            if(in.fired)action=row;
        }
        if(in.fired)break;
    }
    const uint32_t buttons=(w.pressed&0xFFF)|((w.pressed>>16)&0xFFF);
    if(buttons&0x10)f.extrasColumn=(f.extrasColumn+3)%4;
    if(buttons&0x20)f.extrasColumn=(f.extrasColumn+1)%4;
    if(buttons&2)action=3;
    else if((buttons&0xC)&&!shot)action=f.extrasColumn;
    f.extrasWheel=0;f.extrasWheelColumn=-1;
    w.pressed&=~0xC000C;  // the menu's trigger must not join a player
    if(action==3){Confirm(w,kHome);return;}
    if(action==2) {
        ++f.modelViewerRequests;
        w.SoundCall(World::kPreviewStop);
        w.SoundCall(World::kOptionFeedback,kOptionSelectSound);
    } else if(action>=0&&action<2) {
        f.extrasPage=action+1;
        f.extrasControl=0;
        w.SoundCall(World::kOptionFeedback,kOptionSelectSound);
        return;
    }
    Text(w,26,83,"EXTRAS",1.45f);
    for(int row=0;row<4;++row)
        Button(w,24,top+row*pitch,322,top+row*pitch+height,names[row],f.extrasColumn==row,.87f);
    Text(w,350,195,"EXTRAS",.64f,0xFFBBAA91);
    const std::string description=descriptions[std::clamp(f.extrasColumn,0,3)];
    size_t start=0;int line=0;
    while(start<description.size()) {
        size_t end=std::min(start+23,description.size());
        if(end<description.size()){auto space=description.rfind(' ',end);if(space>start)end=space;}
        Text(w,350,230+line*25,description.substr(start,end-start),.65f);
        start=end;while(start<description.size()&&description[start]==' ')++start;++line;
    }
    Text(w,24,453,"ARROWS SELECT   ENTER / SHOOT CONFIRM   ESC / RELOAD BACK",.48f,0xFFB2B3AB);
}

void ExtrasPage(World::Impl& w) {
    auto& f=w.front;
    if(f.extrasPage==0){ExtrasHub(w);return;}
    ExtrasCatalog(w);
    const int col=f.extrasPage==2?1:0;
    const auto& records=col?f.extrasEffects:f.extrasMusic;
    const int size=int(records.size()),visible=col?10:6;
    const float pitch=col?25.f:42.f;
    auto reveal=[&] {
        f.extrasRow[col]=std::clamp(f.extrasRow[col],0,std::max(0,size-1));
        if(f.extrasRow[col]<f.extrasTop[col])f.extrasTop[col]=f.extrasRow[col];
        if(f.extrasRow[col]>=f.extrasTop[col]+visible)f.extrasTop[col]=f.extrasRow[col]-visible+1;
        f.extrasTop[col]=std::clamp(f.extrasTop[col],0,std::max(0,size-visible));
    };
    auto move=[&](int delta) {f.extrasControl=0;f.extrasRow[col]+=delta;reveal();};
    // 0: row/name, 1: resume/play, 2: pause, 3: reset, 4: Back, 5/6: previous/next page.
    int action=-1;
    bool shot=false;
    for(int p=0;p<2;++p) {
        const auto& in=w.input[p];
        if(in.kind!=1)continue;
        const bool moved=in.x!=f.extrasAimX[p]||in.y!=f.extrasAimY[p];
        f.extrasAimX[p]=in.x;f.extrasAimY[p]=in.y;
        shot|=in.fired;
        if(!moved&&!in.fired)continue;
        if(Hit(in,24,402,112,430)){f.extrasControl=4;if(in.fired)action=4;}
        if(Hit(in,420,402,508,430)){f.extrasControl=5;if(in.fired)action=5;}
        if(Hit(in,514,402,616,430)){f.extrasControl=6;if(in.fired)action=6;}
        for(int row=0;row<visible&&f.extrasTop[col]+row<size;++row) {
            const float y=139+row*pitch;
            if(!Hit(in,24,y,616,y+pitch-2))continue;
            f.extrasRow[col]=f.extrasTop[col]+row;f.extrasControl=0;
            if(!col&&in.x>=372) {
                if(in.x<448&&in.y>=y+8&&in.y<y+35)f.extrasControl=1;
                else if(in.x>=456&&in.x<532&&in.y>=y+8&&in.y<y+35)f.extrasControl=2;
                else if(in.x>=540&&in.x<616&&in.y>=y+8&&in.y<y+35)f.extrasControl=3;
                else continue;
            }
            if(in.fired)action=f.extrasControl;
        }
        if(in.fired)break;
    }
    if(f.extrasWheel)move(f.extrasWheel);
    f.extrasWheel=0;f.extrasWheelColumn=-1;
    const uint32_t buttons=(w.pressed&0xFFF)|((w.pressed>>16)&0xFFF);
    if(buttons&0x10)move(-1);
    if(buttons&0x20)move(1);
    const int controls[]={0,1,2,3,4,5,6};
    const int effectsControls[]={0,4,5,6};
    const int* choices=col?effectsControls:controls;
    const int count=col?4:7;
    auto changeControl=[&](int delta) {
        const int* current=std::find(choices,choices+count,f.extrasControl);
        const int index=current==choices+count?0:int(current-choices);
        f.extrasControl=choices[(index+count+delta)%count];
    };
    if(buttons&0x40)changeControl(-1);
    if(buttons&0x80)changeControl(1);
    if(buttons&2)action=4;
    else if((buttons&0xC)&&!shot)action=f.extrasControl;
    w.pressed&=~0xC000C;
    if(action==4) {
        w.SoundCall(World::kPreviewStop);
        w.SoundCall(World::kOptionFeedback,kOptionBackSound);
        f.extrasPage=0;f.extrasColumn=col;f.extrasControl=0;
        return;
    }
    if(action==5)move(-visible);
    else if(action==6)move(visible);
    else if(action>=0&&action<=3&&size) {
        if(col)w.SoundCall(World::kPreviewEffect,records[f.extrasRow[col]].id);
        else {
            const int kind[]={World::kPreviewMusicStart,World::kPreviewMusicPlay,World::kPreviewMusicPause,World::kPreviewMusicReset};
            w.SoundCall(kind[action],records[f.extrasRow[col]].id);
        }
    }
    reveal();
    w.hud.menuScrollHints.push_back({24,139,616,139+visible*pitch-2,
                                     f.extrasTop[col]>0,f.extrasTop[col]+visible<size});
    Text(w,24,64,col?"SOUND EFFECTS":"MUSIC PLAYER",1.2f);
    Text(w,24,109,col?"EFFECTS + VOICE RECORDINGS":"THE COMPLETE SOUNDTRACK",.55f,0xFFB9AA91);
    Text(w,516,115,std::to_string(size)+(col?" FILES":" TRACKS"),.4f,0xFFB9AA91);
    Panel(w,24,132,616,133,0xFF726B58);
    if(records.empty())Text(w,32,150,"NO RECORDINGS FOUND",.62f);
    for(int row=0;row<visible&&f.extrasTop[col]+row<size;++row) {
        const int index=f.extrasTop[col]+row;
        const float y=139+row*pitch;
        const bool selected=f.extrasRow[col]==index;
        Panel(w,24,y,616,y+pitch-2,selected?0xCC8F2026:0x75101315);
        if(selected)Panel(w,24,y,27,y+pitch-2,0xFFF0DBBD);
        Text(w,32,y+(col?4:12),records[index].name,col?.53f:.55f);
        if(!col) {
            const char* labels[]={"PLAY","PAUSE","RESET"};
            for(int b=0;b<3;++b)
                Button(w,372+b*84.f,y+8,448+b*84.f,y+35,labels[b],selected&&f.extrasControl==b+1,.43f);
        }
    }
    Button(w,24,402,112,430,"BACK",f.extrasControl==4,.55f);
    Text(w,138,410,std::to_string(size?f.extrasRow[col]+1:0)+" / "+std::to_string(size),.44f,0xFFB9AA91);
    Button(w,420,402,508,430,"< PREV",f.extrasControl==5,.42f);
    Button(w,514,402,616,430,"NEXT >",f.extrasControl==6,.42f);
    Text(w,24,448,"ARROWS / WHEEL BROWSE   ENTER / SHOOT PLAY   ESC / RELOAD BACK",.43f,0xFFB2B3AB);
    Text(w,24,465,col?"EACH RECORDING PLAYS ONCE":"PLAY RESUMES   NAME RESTARTS   RESET REWINDS + STOPS   EACH TRACK PLAYS ONCE",.35f,0xFFB9AA91);
}

void PublishPage(World::Impl& w) {
    auto& f=w.front;
    const uint32_t buttons=(w.pressed&0xFFF)|((w.pressed>>16)&0xFFF);
    // Forty character keys, followed by Delete, Clear, Publish and Cancel.
    constexpr int erase=40,clear=41,publish=42,cancel=43;
    auto& cursor=f.publishCursor;
    cursor=std::clamp(cursor,0,cancel);
    if(buttons&0x40)cursor=(cursor+43)%44;
    if(buttons&0x80)cursor=(cursor+1)%44;
    if(buttons&0x10)cursor=cursor>=40?30+(cursor-40)*3:cursor>=10?cursor-10:40+std::min(3,cursor/3);
    if(buttons&0x20)cursor=cursor>=40?(cursor-40)*3:cursor<30?cursor+10:40+std::min(3,(cursor-30)/3);
    int action=-1;
    for(int p=0;p<2;++p) {
        const auto& in=w.input[p];if(in.kind!=1||!in.fired)continue;
        for(int i=0;i<40;++i) {
            const float x=64+(i%10)*52.f,y=188+(i/10)*40.f;
            if(Hit(in,x,y,x+44,y+32))action=i;
        }
        for(int i=0;i<4;++i)if(Hit(in,64+i*130.f,368,186+i*130.f,408))action=40+i;
    }
    if(buttons&2)action=cancel;
    else if((buttons&0xC)&&!w.input[0].fired&&!w.input[1].fired)action=cursor;
    if(action>=0)cursor=action;
    if(action>=0&&action<40)EditOnlineName(f.leaderboardInitials,std::string(1,kOnlineNameKeys[action]),false);
    else if(action==erase)EditOnlineName(f.leaderboardInitials,{},true);
    else if(action==clear)f.leaderboardInitials.clear();
    else if(action==publish&&f.leaderboardPublishable&&!TrimOnlineName(f.leaderboardInitials).empty()) {
        f.leaderboardInitials=TrimOnlineName(f.leaderboardInitials);
        ++f.portLeaderboardPublishRequests;f.leaderboardPublishable=false;
        f.nightShiftPage=3;f.nightShiftRow=5;
    } else if(action==cancel){f.nightShiftPage=3;f.nightShiftRow=5;}
    w.pressed&=~0xC000C;
    Panel(w,44,74,596,432,0xED0B1012);
    Text(w,64,86,"ONLINE NAME",1.f);
    Text(w,474,102,std::to_string(f.leaderboardInitials.size())+" / 12",.5f,0xFFB9AA91);
    Panel(w,64,128,576,168,0xFF182225);
    Text(w,80,136,f.leaderboardInitials.empty()?"ENTER YOUR NAME":f.leaderboardInitials,.86f,
         f.leaderboardInitials.empty()?0xFF8B9698:0xFFFFF4E5);
    for(int i=0;i<40;++i) {
        const float x=64+(i%10)*52.f,y=188+(i/10)*40.f;
        const std::string label=kOnlineNameKeys[i]==' '?"SP":std::string(1,kOnlineNameKeys[i]);
        Button(w,x,y,x+44,y+32,label.c_str(),cursor==i,.65f);
    }
    const char* actions[]={"DELETE","CLEAR","PUBLISH","CANCEL"};
    for(int i=0;i<4;++i) {
        const bool enabled=i!=2||(f.leaderboardPublishable&&!TrimOnlineName(f.leaderboardInitials).empty());
        if(enabled)Button(w,64+i*130.f,368,186+i*130.f,408,actions[i],cursor==40+i,.6f);
        else {
            Panel(w,64+i*130.f,368,186+i*130.f,408,0x75101315);
            Text(w,78+i*130.f,382,actions[i],.6f,0xFF677072);
        }
    }
    Text(w,64,416,"ONLINE: 1-12 CHARACTERS    LOCAL INITIALS: 3",.38f,0xFFB9AA91);
    Text(w,24,453,"TYPE A NAME OR SELECT KEYS   ENTER: SELECT   ESC / RELOAD: BACK",.43f,0xFFB2B3AB);
}
} // namespace

void NightShiftMenuFrame(World::Impl& w) {
    auto& f=w.front;
    w.hud.menuPanels.clear();
    w.hud.menuText.clear();
    const size_t spriteStart=w.hudSprites.size();
    // Subtle uniform shading keeps the original attract scenes visible behind the UI.
    Panel(w,0,0,640,480,0x3503090B);
    Panel(w,0,440,640,480,0xC8090D10);
    Logo(w,f.nightShiftPage!=0);
    if(f.nightShiftPage==4) {PublishPage(w);LoadSprites(w,spriteStart);return;}
    if(f.nightShiftPage==9) {ExtrasPage(w);LoadSprites(w,spriteStart);return;}
    const Row* rows=kHomeRows; int count=kHomeCount;
    float left=24,right=322,top=158,pitch=37,height=32,scale=1.05f;
    if(f.nightShiftPage==1) {rows=kPlayRows;count=int(std::size(kPlayRows));Text(w,26,83,"PLAY",1.45f);top=136;pitch=40;height=35;}
    else if(f.nightShiftPage==2) {rows=kPracticeRows;count=int(std::size(kPracticeRows));Text(w,26,83,"PRACTICE",1.45f);top=140;pitch=52;height=44;}
    else if(f.nightShiftPage==3) {rows=kBoardRows;count=7;Text(w,26,78,"LEADERBOARD",.81f);top=130;pitch=40;height=35;right=187;scale=.60f;}
    const int roomCount=f.nightShiftPage==0?int(f.homeRooms.size()):0;
    const int total=count+(f.nightShiftPage==0?3+roomCount:0);
    f.nightShiftRow=std::clamp(f.nightShiftRow,0,total-1);
    uint32_t buttons=(w.pressed&0xFFF)|((w.pressed>>16)&0xFFF);
    bool pointerShot=false, pointerHit=false;
    int pointAction=-1;
    int pointerPlayer=-1;
    for(int q=0;q<2;++q) {
        const int player=(f.startPlayer+q)&1;
        const auto& in=w.input[player];
        if(in.kind!=1)continue;
        if(pointerPlayer<0||in.fired)pointerPlayer=player;
        if(in.fired)break;
    }
    if(pointerPlayer>=0) {
        const auto& in=w.input[pointerPlayer];
        int row=-1;
        for(int i=0;i<count;++i)if(Hit(in,left,top+i*pitch,right,top+i*pitch+height))row=i;
        if(f.nightShiftPage==0) {
            for(int i=0;i<roomCount;++i)if(Hit(in,366,166+i*50,612,210+i*50))row=count+i;
            for(int i=0;i<3;++i)if(Hit(in,366,282+i*40,612,317+i*40))row=count+roomCount+i;
        }
        const bool moved=in.x!=f.nightShiftAimX||in.y!=f.nightShiftAimY;
        if((moved||in.fired)&&row>=0)f.nightShiftRow=row;
        if(in.fired) {
            pointerShot=true;pointerHit=row>=0;
            if(f.nightShiftPage==3&&f.nightShiftTable>=3) {
                if(Hit(in,535,86,566,115)){pointAction=kBossPrev;pointerHit=true;}
                if(Hit(in,574,86,605,115)){pointAction=kBossNext;pointerHit=true;}
            }
        }
        f.nightShiftAimX=in.x;f.nightShiftAimY=in.y;
    }
    if(buttons&0x10)f.nightShiftRow=(f.nightShiftRow+total-1)%total;
    if(buttons&0x20)f.nightShiftRow=(f.nightShiftRow+1)%total;
    if(f.nightShiftPage==3&&f.nightShiftTable>=3) {
        if(buttons&0x40)Confirm(w,kBossPrev);
        if(buttons&0x80)Confirm(w,kBossNext);
    }
    if(buttons&2) { if(f.nightShiftPage!=0)Confirm(w,kHome); w.pressed&=~0xC000C; }
    else if((buttons&0xC)&&(!pointerShot||pointerHit)) {
        const int rail=f.nightShiftRow-count;
        const int action=pointAction>=0?pointAction:f.nightShiftRow<count?rows[f.nightShiftRow].action:
            rail<roomCount?kHomeRoom:rail==roomCount?kHost:rail==roomCount+1?kBrowse:kPeer;
        const int oldPage=f.nightShiftPage;
        Confirm(w,action);
        // A menu click must not join a player except when a gameplay mode was confirmed.
        if(!f.menuConfirmed)w.pressed&=~0xC000C;
        if(oldPage!=f.nightShiftPage)return;
    } else if(pointerShot&&!pointerHit) w.pressed&=~0xC000C;
    for(int i=0;i<count;++i) {
        const bool boardSel=f.nightShiftPage==3&&i>0&&i<5&&(f.nightShiftTable==i-1||(i==4&&f.nightShiftTable>=3));
        const char* label=f.nightShiftPage==3&&i==0?(f.nightShiftShared?"SOURCE: ONLINE":"SOURCE: LOCAL"):rows[i].label;
        const bool disabled=f.nightShiftPage==3&&i==5&&!f.leaderboardPublishable;
        Button(w,left,top+i*pitch,right,top+i*pitch+height,label,!disabled&&f.nightShiftRow==i,scale,boardSel);
    }
    if(f.nightShiftPage==0) {
        Panel(w,366,158,612,159,0xFF726B58);
        Text(w,366,136,"ONLINE",.87f);
        if(roomCount==0)Text(w,378,205,f.homeRoomsStatus.substr(0,29),.48f,0xFFB2B3AB);
        for(int i=0;i<roomCount;++i) {
            const auto& room=f.homeRooms[i];const float y=166+i*50.f;
            Panel(w,366,y,612,y+44,f.nightShiftRow==count+i?0xED8F2026:0x75101315);
            Text(w,378,y+6,"ROOM "+std::to_string(room.id),room.id>9999999?.52f:.67f);
            Text(w,535,y+8,room.privateRoom?"PRIVATE":"PUBLIC",.43f,0xFFD4BA96);
            static constexpr const char* difficulty[]={"VERY EASY","EASY","NORMAL","HARD","VERY HARD"};
            const char* level=room.difficulty>=0&&room.difficulty<5?difficulty[room.difficulty]:"UNKNOWN";
            Text(w,378,y+28,std::string(level)+" / "+(room.unlimited?"UNLIMITED":"3 CONTINUES"),.43f,0xFFC3C6BD);
        }
        const char* names[]={"HOST ONLINE","BROWSE LOBBY","PEER TO PEER"};
        for(int i=0;i<3;++i)Button(w,366,282+i*40,612,317+i*40,names[i],f.nightShiftRow==count+roomCount+i,.73f);
    } else if(f.nightShiftPage==1) {
        const char* help[]={"THE ORIGINAL ARCADE CAMPAIGN","CAMPAIGN WITH ITEMS AND LOADOUTS","TEN AIM AND SURVIVAL CHALLENGES","TIME ATTACK AGAINST EACH BOSS","SURVIVE WAVES AND UPGRADE BETWEEN ROUNDS","A RANDOMIZED ORIGINAL MODE RUN","RETURN TO THE MAIN MENU"};
        Text(w,350,198,"SELECT MODE",.64f,0xFFBBAA91);
        std::string description=help[std::clamp(f.nightShiftRow,0,int(std::size(help))-1)];
        // Wrap at whole words within the right column, using the real menu font's advance.
        size_t start=0;int line=0;
        while(start<description.size()) {
            size_t end=std::min(start+23,description.size());
            if(end<description.size()) {auto space=description.rfind(' ',end);if(space>start)end=space;}
            Text(w,350,235+line*25,description.substr(start,end-start),.65f);
            start=end;while(start<description.size()&&description[start]==' ')++start;++line;
        }
    } else if(f.nightShiftPage==2) {
        Text(w,350,203,"SCENE SELECT",.64f,0xFFBBAA91);
        Text(w,350,239,"REPLAY A CHAPTER",.65f);
    } else if(f.nightShiftPage==3) {
        Leaderboard(w);
        if(f.nightShiftRow==5)Text(w,24,421,(f.leaderboardPublishStatus.empty()?"COMPLETE A NEW RUN FIRST":f.leaderboardPublishStatus).substr(0,50),.45f,0xFFB9AA91);
    }
    Text(w,24,453,"ARROWS SELECT   ENTER / SHOOT CONFIRM   ESC / RELOAD BACK",.48f,0xFFB2B3AB);
    LoadSprites(w,spriteStart);
}

void NightShiftGameOptionsFrame(World::Impl& w,const std::vector<NightShiftOptionRow>& rows,int cursor,
                                const std::string& help) {
    w.hud.menuPanels.clear();
    w.hud.menuText.clear();
    const size_t first=w.hudSprites.size();
    Panel(w,0,0,640,480,0xEC090D10);
    Panel(w,0,0,640,94,0x6505090B);
    Panel(w,26,65,614,67,0xFF92272D);
    Panel(w,0,440,640,480,0xC8090D10);
    Text(w,26,20,"GAME OPTIONS",1.25f);
    Text(w,28,80,"GAME RULES / AUDIO VOLUME / DEVICE SETUP",.43f,0xFFB9AA91);
    for(size_t i=0;i<rows.size();++i) {
        const auto& row=rows[i];
        const float y=kNightShiftOptionsTop+float(i)*NightShiftOptionsPitch(rows.size());
        const float height=NightShiftOptionsHeight(rows.size());
        if(row.id==cursor) {
            Panel(w,26,y-1,614,y+height,0xCC8F2026);
            Panel(w,26,y-1,29,y+height,0xFFF0DBBD);
        } else Panel(w,26,y+height,614,y+height+1,0x354E514E);
        Text(w,34,y+2,row.label,.51f,row.enabled?0xFFECE7DC:0xFF797D79);
        Text(w,row.secondValue.empty()?416.f:420.f,y+2,row.value,.51f,row.enabled?0xFFECE7DC:0xFF797D79);
        if(!row.secondValue.empty())Text(w,548,y+2,row.secondValue,.51f,row.secondEnabled?0xFFECE7DC:0xFF797D79);
        if(row.id==cursor&&row.adjustable&&row.enabled) {
            Text(w,358,y+2,"<",.51f);
            Text(w,600,y+2,">",.51f);
        }
    }
    size_t split=help.size()>84?help.rfind(' ',84):help.size();
    if(split==std::string::npos)split=84;
    Text(w,28,410,help.substr(0,split),.42f,0xFFC1BAAC);
    if(split<help.size())Text(w,28,426,help.substr(split+1),.42f,0xFFC1BAAC);
    Text(w,28,454,"ARROWS SELECT / ADJUST   ENTER / SHOOT CONFIRM   ESC / RELOAD SAVE + BACK",.40f,0xFFB2B3AB);
    LoadSprites(w,first);
}

void World::SetPortHordeLeaderboard(const std::vector<HordeLeaderboardEntry>& entries) {
    impl_->front.hordeLeaderboard.assign(entries.begin(),entries.begin()+std::min<size_t>(10,entries.size()));
}
void World::SetPortSharedLeaderboard(int table,const std::vector<SharedLeaderboardEntry>& entries,const std::string& status) {
    auto& f=impl_->front;
    f.sharedTable=table;
    f.sharedLeaderboard.assign(entries.begin(),entries.begin()+std::min<size_t>(10,entries.size()));
    for(auto& e:f.sharedLeaderboard) {
        e.name.resize(std::min(kOnlineNameLimit,e.name.size()));
        for(char& c:e.name)if(c<32||c>126)c='?';
    }
    f.sharedLeaderboardStatus=status.substr(0,32);
}
void World::SetPortLeaderboardPublishable(bool on,const std::string& status) {
    impl_->front.leaderboardPublishable=on;
    impl_->front.leaderboardPublishStatus=status.substr(0,50);
}
void World::EditPortLeaderboardName(const std::string& text,bool backspace) {
    auto& w=*impl_;
    if(!w.cfg.portFeatures||w.gameMode!=4||w.front.nightShiftPage!=4)return;
    EditOnlineName(w.front.leaderboardInitials,text,backspace);
    w.front.publishCursor=42;  // typing followed by Enter publishes the completed name
}
void PortLeaderboardBegin(World::Impl& w) {
    w.portRunRecorded=false;
    for(int p=0;p<2;++p) {
        // A fresh game can inherit already joined agents from the item/boss
        // selector. Continue/out/idle slots belong to the previous game.
        const int state=w.player[p].state;
        w.portRunJoined[p]=state>=0&&state<9&&state!=4&&state!=6;
    }
    w.portRunParticipants=int(w.portRunJoined[0])+int(w.portRunJoined[1]);
}
void PortLeaderboardPlayerJoined(World::Impl& w,int player) {
    if(w.gameMode!=6||player<0||player>1)return;
    w.portRunJoined[player]=true;
    w.portRunParticipants=std::max({w.portRunParticipants,std::clamp(w.players,0,2),
                                  int(w.portRunJoined[0])+int(w.portRunJoined[1])});
}
void PortLeaderboardComplete(World::Impl& w,int table,int score,int stageOrDifficulty) {
    if(!w.portLeaderboardRecording||!w.portRunOriginEligible||w.portRunRecorded||!w.frontEnd||!w.cfg.portFeatures||
       w.front.sceneSelect||w.front.sceneStart>=0||w.prototype||RandomizerActive(w)||HordeActive(w))return;
    if(table<0||table>12||table==2)return;
    const int participants=std::clamp(std::max({w.players,w.portRunParticipants,
        int(w.portRunJoined[0])+int(w.portRunJoined[1])}),1,2);
    if(table>=3&&(score<=0||participants!=1))return;
    LeaderboardRunSnapshot next;
    next.sequence=w.portLeaderboardRun.sequence+1;
    next.table=table; next.players=participants;
    next.difficulty=std::clamp(stageOrDifficulty,0,table<2?6:4);
    for(int p=0;p<2;++p)next.playerScores[p]=w.portRunJoined[p]?std::max(0,w.player[p].score):0;
    next.score=table<2?std::max(next.playerScores[0],next.playerScores[1]):score;
    if(next.score<=0)return;
    next.eligible=true;
    w.portLeaderboardRun=next;
    w.portRunRecorded=true;
}
void World::SetPortLeaderboardRecording(bool on) {impl_->portLeaderboardRecording=on;}
LeaderboardRunSnapshot World::PortLeaderboardRun() const {return impl_->portLeaderboardRun;}
void World::SetPortHomeRooms(const std::vector<PortHomeRoom>& rooms,const std::string& status) {
    auto& f=impl_->front;
    const int old=f.nightShiftRow-kHomeCount;
    const int oldRoomCount=int(f.homeRooms.size());
    const int action=f.nightShiftPage==0&&old>=oldRoomCount&&old<oldRoomCount+3?old-oldRoomCount:-1;
    const uint32_t selected=f.nightShiftPage==0&&old>=0&&old<int(f.homeRooms.size())?f.homeRooms[old].id:0;
    f.homeRooms.clear();
    for(const auto& r:rooms)if(r.id&&f.homeRooms.size()<2)f.homeRooms.push_back(r);
    f.homeRoomsStatus=status.substr(0,29);
    if(selected) {
        auto it=std::find_if(f.homeRooms.begin(),f.homeRooms.end(),[&](const auto& r){return r.id==selected;});
        f.nightShiftRow=kHomeCount+int(it-f.homeRooms.begin());  // missing selection safely moves to Host
    } else if(action>=0)f.nightShiftRow=kHomeCount+int(f.homeRooms.size())+action;
}
void World::ScrollPortExtras(int rows,int column) {
    auto& f=impl_->front;
    if(!impl_->cfg.portFeatures||impl_->gameMode!=4||f.nightShiftPage!=9||f.extrasPage==0)return;
    f.extrasWheel=std::clamp(f.extrasWheel+rows,-1000,1000);
    f.extrasWheelColumn=-1;  // The open media page owns the wheel; a hidden list cannot receive it.
    (void)column;
}
} // namespace game
