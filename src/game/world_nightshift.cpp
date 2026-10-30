#include "game/world_nightshift.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include "game/menu_text.h"
#include "game/menu_audio.h"
#include "game/sound_table.h"
#include "game/world_modes.h"

namespace game {
namespace {
struct Row { const char* label; int action; };
enum { kPlay=100, kPractice, kLeaderboard, kOnline, kOptions, kExit, kHome, kHorde, kRandomizer, kScene,
       kHost, kBrowse, kPeer, kArcadeBoard, kOriginalBoard, kHordeBoard, kBossBoard, kBossPrev, kBossNext, kBoardSource, kPublish,
       kHomeRoom, kExtras, kTyping, kTypingStart, kTypingBoard, kTypingDrill, kSwitchGame };
constexpr Row kHomeRows[]={{"PLAY",kPlay},{"PRACTICE",kPractice},{"LEADERBOARD",kLeaderboard},{"ONLINE",kOnline},{"EXTRAS",kExtras},{"OPTIONS",kOptions},{"EXIT",kExit}};
constexpr int kHomeCount = int(std::size(kHomeRows));
constexpr Row kTypingHomeRows[]={{"ORIGINAL",kTyping},{"DRILL",kTypingDrill},{"LEADERBOARD",kLeaderboard},{"OPTIONS",kOptions},{"EXIT",kExit}};
constexpr Row kPlayRows[]={{"ARCADE",0},{"ORIGINAL",1},{"CHALLENGES",2},{"BOSS MODE",3},{"HORDE MODE",kHorde},{"RANDOMIZER",kRandomizer},{"BACK",kHome}};
constexpr Row kPracticeRows[]={{"SCENE SELECT",kScene},{"BACK",kHome}};
constexpr Row kBoardRows[]={{"SOURCE: LOCAL",kBoardSource},{"ARCADE",kArcadeBoard},{"ORIGINAL",kOriginalBoard},{"HORDE",kHordeBoard},{"BOSS MODE",kBossBoard},{"TYPING OF DEAD",kTypingBoard},{"BACK",kHome}};
constexpr Row kTypingBoardRows[]={{"SOURCE: LOCAL",kBoardSource},{"BACK",kHome}};
constexpr int kTypingBoardTable = 13;
constexpr bool TimeBoard(int table) { return table>=3 && table<=12; }
constexpr const char* kBoss[] = {"JUDGMENT","HIEROPHANT A","HIEROPHANT B","TOWER A","TOWER B","STRENGTH A","STRENGTH B","MAGICIAN","EMPEROR","BOSS RUSH"};
constexpr size_t kOnlineNameLimit = 12;
int HomeCount(const World::Impl& w) { return w.front.typingHome?int(std::size(kTypingHomeRows)):kHomeCount; }

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
        if(w.hudSprites[i].id>=0x4000)continue;
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
void TypingSettings(World::Impl& w, bool save) {
    if (w.cfg.savePath.empty()) return;
    const std::filesystem::path path = w.cfg.savePath+".typing.ini";
    auto& o = w.cfg.typingOptions;
    if (save) {
        std::ofstream out(path);
        if (out) out << "difficulty=" << int(o.difficulty) << "\nlife=" << o.startingLives
                     << "\ncontinue=" << o.continues << "\nword_size=" << int(o.wordSize)
                     << "\nkeyboard=" << int(o.keyboard) << '\n';
        return;
    }
    std::ifstream in(path);
    std::string line;
    while (std::getline(in,line)) {
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        int value = -1;
        try { value = std::stoi(line.substr(equal+1)); } catch (...) { continue; }
        const auto key = line.substr(0,equal);
        if (key == "difficulty") o.difficulty = TypingDifficulty(std::clamp(value,0,4));
        else if (key == "life" && (value == 1 || value == 3 || value == 5)) o.startingLives = value;
        else if (key == "continue" && (value == -1 || value == 0 || value == 3 || value == 5 || value == 9)) o.continues = value<0?0:value;
        else if (key == "word_size") o.wordSize = value == 1 ? TypingWordSize::Large : TypingWordSize::Normal;
        else if (key == "keyboard") o.keyboard = value == 0 ? TypingKeyboard::US : TypingKeyboard::UK;
        else if (key == "blood") o.bloodEffects = value != 0;
    }
}
void Confirm(World::Impl& w,int action) {
    auto& f=w.front;
    auto page=[&](int p) {
        const bool optionsBack=(f.nightShiftPage==5||f.nightShiftPage==9)&&p==0;
        if(f.nightShiftPage==9&&p!=9)w.SoundCall(World::kPreviewStop);
        if(f.nightShiftPage==11&&p!=11) { w.SoundStopAll(); f.sceneStart=-1; }
        f.nightShiftPage=p; f.nightShiftRow=0;
        if(p==5||p==9||optionsBack) w.SoundCall(World::kOptionFeedback, optionsBack?kOptionBackSound:kOptionSelectSound);
        else w.Sound(0x121A9);
    };
    switch(action) {
    case kPlay: page(1); return;
    case kSwitchGame:
        if (f.menuSwitchAge<0) {
            f.menuSwitchTargetTyping=!f.typingHome;
            f.menuSwitchAge=0;
            w.Sound(0x121A9);
        }
        return;
    case kTyping:
        if (!f.typingOptionsLoaded) { TypingSettings(w,false); f.typingOptionsLoaded = true; }
        w.cfg.typingOriginal = true; w.ResetTyping();
        f.sceneStart=-1;
        page(11); return;
    case kTypingDrill:
        w.typingDrill.view=0; w.typingDrill.characters.clear();
        w.typingDrill.visualView=-1;
        w.typingDrill.visualSelected=w.typingDrill.visualPrevious=-1;
        page(12); return;
    case kPractice: page(2); return;
    case kExtras:
        w.SoundStopAll();page(9);f.extrasPage=f.extrasColumn=f.extrasControl=0;
        for(int p=0;p<2;++p){f.extrasAimX[p]=w.input[p].x;f.extrasAimY[p]=w.input[p].y;}
        return;
    case kLeaderboard: if(f.typingHome)f.nightShiftTable=kTypingBoardTable; page(3); if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kHome: page(0); return;
    case kExit: f.quit=true; return;
    case kArcadeBoard: f.nightShiftTable=0; if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kOriginalBoard: f.nightShiftTable=1; if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kHordeBoard: f.nightShiftTable=2; if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kBossBoard: if(!TimeBoard(f.nightShiftTable))f.nightShiftTable=3; if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kTypingBoard: f.nightShiftTable=kTypingBoardTable; if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kBossPrev: f.nightShiftTable=3+(f.nightShiftTable-3+9)%10; if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kBossNext: f.nightShiftTable=3+(f.nightShiftTable-3+1)%10; if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kBoardSource: f.nightShiftShared=!f.nightShiftShared; if(f.nightShiftShared)++f.portLeaderboardRequests; return;
    case kPublish: return;  // Reserved legacy action; completed runs now submit in the local client.
    case kOnline: case kHost: case kBrowse: case kPeer:
        f.portLobbyRoom=0;
        f.portLobbyAction=action==kHost?1:action==kBrowse?2:action==kPeer?3:0;
        ++f.portLobbyRequests;
        ModesMenuConfirm(w,6); return;
    case kHomeRoom: {
        const int i=f.nightShiftRow-HomeCount(w)-1;
        if(i<0||i>=int(f.homeRooms.size()))return;
        f.portLobbyRoom=f.homeRooms[i].id;f.portLobbyAction=4;++f.portLobbyRequests;
        ModesMenuConfirm(w,6);return;
    }
    case kOptions: ModesMenuConfirm(w,5);return;
    default: break;
    }
    // Keep the same player-join path, credits and unlocks as the original mode handlers.
    w.cfg.portTyping=action==kTypingStart;
    w.cfg.typingOriginal=true;
    w.ResetTyping();
    if(action==kTypingStart) {
        w.SoundStopAll();
        if (!w.typingPreviousOptions.valid) {
            auto& saved = w.typingPreviousOptions;
            saved.valid = true; saved.difficulty = w.cfg.difficulty; saved.lives = w.cfg.livesSetting;
            saved.creditSetting = w.cfg.creditSetting; saved.credits = w.cfg.credits; saved.freePlay = w.cfg.freePlay;
        }
        w.cfg.difficulty = int(w.cfg.typingOptions.difficulty);
        w.cfg.livesSetting = w.cfg.typingOptions.startingLives;
        w.cfg.creditSetting = w.cfg.typingOptions.continues==0?-1:w.cfg.typingOptions.continues;
        action=0;
    }
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
void TypingOptionsPage(World::Impl& w) {
    auto& f = w.front;
    auto& o = w.cfg.typingOptions;
    const auto unlocks = w.typing.originalProgress().unlocks();
    const auto lives = TypingLifeOptions((unlocks&1)!=0);
    const auto continues = TypingContinueOptions((unlocks&2)!=0,(unlocks&32)!=0);
    if (std::find(lives.begin(),lives.end(),o.startingLives)==lives.end()) o.startingLives=3;
    if (std::find(continues.begin(),continues.end(),o.continues)==continues.end()) o.continues=3;
    auto cycle = [](const std::vector<int>& choices,int value,int step) {
        const auto at=std::find(choices.begin(),choices.end(),value);
        const int index=at==choices.end()?0:int(at-choices.begin());
        return choices[(index+int(choices.size())+step)%choices.size()];
    };
    constexpr int count = 6;
    constexpr float top=149, pitch=39, height=32;
    f.nightShiftRow=std::clamp(f.nightShiftRow,0,count-1);
    const uint32_t buttons = (w.pressed&0xfff)|((w.pressed>>16)&0xfff);
    if (buttons&0x10) f.nightShiftRow = (f.nightShiftRow+count-1)%count;
    if (buttons&0x20) f.nightShiftRow = (f.nightShiftRow+1)%count;
    bool confirm = (buttons&0xc) != 0;
    for (const auto& input : w.input) if (input.kind == 1) {
        const bool moved = input.x != f.nightShiftAimX || input.y != f.nightShiftAimY;
        f.nightShiftAimX = input.x; f.nightShiftAimY = input.y;
        for (int row = 0; row < count; ++row) {
            const float y=row==5?361.f:top+row*pitch;
            if (!Hit(input,96,y,544,y+height)) continue;
            if (moved || input.fired) f.nightShiftRow = row;
            if (input.fired) confirm = true;
        }
        if (input.fired&&Hit(input,512,98,544,128)) { Confirm(w,kHome);w.pressed&=~0xc000c;return; }
    }
    if (buttons&2) { w.pressed &= ~0xc000c; Confirm(w,kHome); return; }
    if (confirm && f.nightShiftRow == 5) { Confirm(w,kTypingStart); return; }
    w.pressed &= ~0xc000c;
    const int change = buttons&0x40 ? -1 : (buttons&0x80 || confirm) ? 1 : 0;
    if (change) {
        switch (f.nightShiftRow) {
        case 0: o.difficulty = TypingDifficulty((int(o.difficulty)+5+change)%5); break;
        case 1: o.startingLives = cycle(lives,o.startingLives,change); break;
        case 2: o.continues = cycle(continues,o.continues,change); break;
        case 3: o.wordSize = o.wordSize == TypingWordSize::Normal ? TypingWordSize::Large : TypingWordSize::Normal; break;
        case 4: o.keyboard = o.keyboard == TypingKeyboard::US ? TypingKeyboard::UK : TypingKeyboard::US; break;
        default: break;
        }
        TypingSettings(w,true);
    }
    static constexpr const char* difficulties[] = {"VERY EASY","EASY","NORMAL","HARD","VERY HARD"};
    const std::string values[] = {difficulties[std::clamp(int(o.difficulty),0,4)],std::to_string(o.startingLives),
        o.continues==0?"FREE PLAY":std::to_string(o.continues),o.wordSize == TypingWordSize::Large ? "LARGE" : "NORMAL",
        o.keyboard == TypingKeyboard::US ? "US" : "UK"};
    const char* names[] = {"DIFFICULTY","LIFE","CONTINUES","WORD SIZE","KEYBOARD TYPE"};
    Panel(w,0,0,640,480,0xA8000000);
    Panel(w,84,84,556,416,0xF20C1114);
    Panel(w,84,84,556,87,0xFF92272D);
    Text(w,104,107,"ORIGINAL MODE",.90f);
    Button(w,512,98,544,128,"X",false,.59f);
    for (int row = 0; row < 5; ++row) {
        const float y=top+row*pitch;
        Button(w,96,y,544,y+height,names[row],f.nightShiftRow==row,.60f);
        Text(w,366,y+9,values[row],.55f);
        if (f.nightShiftRow==row) {Text(w,344,y+9,"<",.55f);Text(w,525,y+9,">",.55f);}
    }
    Button(w,96,361,544,393,"START",f.nightShiftRow==5,.78f);
    Text(w,104,402,"ARROWS ADJUST   ENTER CONFIRM   ESC CLOSE",.35f,0xffb2b3ab);
}
std::string Score(int n) { char s[32]; snprintf(s,sizeof(s),"%d",std::max(0,n));return s; }
std::string Time(int n) { char s[32]; n=std::max(n,0);snprintf(s,sizeof(s),"%02dM%02dS%02d",n/3600,(n/60)%60,(n%60)*100/60);return s; }
void Leaderboard(World::Impl& w) {
    const int table=std::clamp(w.front.nightShiftTable,0,kTypingBoardTable);
    const bool timed=TimeBoard(table),typing=table==kTypingBoardTable;
    Panel(w,201,78,618,429,0xE10B1012);
    const char* title=table==0?"ARCADE":table==1?"ORIGINAL":table==2?"HORDE":typing?"TYPING OF THE DEAD":kBoss[table-3];
    Text(w,216,88,title,typing?.78f:.95f);
    Text(w,216,116,w.front.nightShiftShared?"ONLINE LEADERBOARD":"LOCAL LEADERBOARD",.48f,0xFFA6ACA8);
    if(timed) {
        Button(w,535,86,566,115,"<",false,.65f);
        Button(w,574,86,605,115,">",false,.65f);
    }
    Panel(w,216,143,603,144,0xFF726B58);
    const bool shared=w.front.nightShiftShared;
    Text(w,218,152,"NO",.55f,0xFFB9AA91);
    if(shared) {
        Text(w,250,152,"NAME",.50f,0xFFB9AA91);
        if(table==2)Text(w,390,152,"RD",.45f,0xFFB9AA91);
        Text(w,table==2?436:398,152,timed?"TIME M S CS":"SCORE",timed?.40f:.50f,0xFFB9AA91);
        Text(w,532,152,timed||table==2?"LEVEL":"STAGE",.45f,0xFFB9AA91);
        Text(w,584,152,"PL",.45f,0xFFB9AA91);
    } else {
        Text(w,256,152,table==2?"ROUND":"NAME",.55f,0xFFB9AA91);
        Text(w,360,152,timed?"TIME M S CS":"SCORE",timed?.43f:.55f,0xFFB9AA91);
        Text(w,505,152,timed||table==2?"LEVEL":"STAGE",.55f,0xFFB9AA91);
        if(table==2||typing)Text(w,570,152,"PL",.55f,0xFFB9AA91);
    }
    if(shared&&(w.front.sharedTable!=table||w.front.sharedLeaderboard.empty())) {
        std::string status=w.front.sharedTable==table?w.front.sharedLeaderboardStatus:"LOADING...";
        if(status.empty()||status=="COMMUNITY RUNS / UNVERIFIED")status="NO ONLINE RECORDS YET";
        Text(w,234,221,status.substr(0,32),.62f);
    } else if(typing&&w.front.typingLeaderboard.empty()&&!shared) {
        Text(w,234,221,"NO TYPING RUNS SAVED",.66f);
        Text(w,234,252,"PLAY ORIGINAL TO SET A RECORD",.49f,0xFFA6ACA8);
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
            Text(w,table==2?436:398,y,timed?Time(e.score):Score(e.score),table==2?.45f:.55f);
            Text(w,532,y,Score(e.difficulty+1),.55f);
            Text(w,584,y,Score(std::clamp(e.players,1,2)),.55f);
        } else if(typing) {
            if(i>=int(w.front.typingLeaderboard.size()))break;
            const auto& e=w.front.typingLeaderboard[i];
            Text(w,256,y,e.name.substr(0,kOnlineNameLimit),.49f);
            Text(w,360,y,Score(e.score),.62f);
            Text(w,505,y,Score(e.difficulty+1),.62f);
            Text(w,574,y,Score(e.players),.62f);
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
            Text(w,360,y,timed?Time(e.score):Score(e.score),.62f);
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

} // namespace

void NightShiftMenuFrame(World::Impl& w) {
    auto& f=w.front;
    // Older snapshots may still refer to the retired manual-publish page.
    if(f.nightShiftPage==4) {f.nightShiftPage=3;f.nightShiftRow=0;}
    if (f.menuSwitchAge>=0) {
        ++f.menuSwitchAge;
        if (f.menuSwitchAge==24) {
            f.typingHome=f.menuSwitchTargetTyping;
            f.nightShiftPage=f.nightShiftRow=0;
            f.nightShiftAimX=f.nightShiftAimY=-1;
        }
        if (f.menuSwitchAge>=48) f.menuSwitchAge=-1;
    }
    const bool switching=f.menuSwitchAge>=0;
    const bool modal=f.nightShiftPage==11;
    const int page=modal?0:f.nightShiftPage;
    w.hud.menuPanels.clear();
    w.hud.menuText.clear();
    const size_t spriteStart=w.hudSprites.size();
    if(f.nightShiftPage==12) {TypingDrillMenuFrame(w);LoadSprites(w,spriteStart);return;}
    // Subtle uniform shading keeps the original attract scenes visible behind the UI.
    Panel(w,0,0,640,480,0x3503090B);
    Panel(w,0,440,640,480,0xC8090D10);
    if (!f.typingHome) Logo(w,page!=0);
    if(f.nightShiftPage==9) {ExtrasPage(w);LoadSprites(w,spriteStart);return;}
    const Row* rows=f.typingHome?kTypingHomeRows:kHomeRows; int count=HomeCount(w);
    float left=24,right=322,top=158,pitch=37,height=32,scale=1.05f;
    if(page==1) {rows=kPlayRows;count=int(std::size(kPlayRows));Text(w,26,83,"PLAY",1.45f);top=136;pitch=37;height=32;}
    else if(page==2) {rows=kPracticeRows;count=int(std::size(kPracticeRows));Text(w,26,83,"PRACTICE",1.45f);top=140;pitch=52;height=44;}
    else if(page==3) {
        rows=f.typingHome?kTypingBoardRows:kBoardRows;
        count=f.typingHome?int(std::size(kTypingBoardRows)):int(std::size(kBoardRows));
        if(f.typingHome)f.nightShiftTable=kTypingBoardTable;
        Text(w,26,78,"LEADERBOARD",.81f);top=130;pitch=40;height=35;right=187;scale=.60f;
    }
    const int roomCount=page==0?int(f.homeRooms.size()):0;
    const int total=count+(page==0?1+roomCount:0);
    if (!modal) f.nightShiftRow=std::clamp(f.nightShiftRow,0,total-1);
    uint32_t buttons=switching||modal?0:(w.pressed&0xFFF)|((w.pressed>>16)&0xFFF);
    if(switching)w.pressed&=~0x0fff0fff;
    bool pointerShot=false, pointerHit=false;
    int pointAction=-1;
    int pointerPlayer=-1;
    for(int q=0;!switching&&!modal&&q<2;++q) {
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
        if(page==0) {
            if(Hit(in,366,104,612,214))row=count;
            for(int i=0;i<roomCount;++i)if(Hit(in,366,280+i*50,612,324+i*50))row=count+1+i;
        }
        const bool moved=in.x!=f.nightShiftAimX||in.y!=f.nightShiftAimY;
        if((moved||in.fired)&&row>=0)f.nightShiftRow=row;
        else if(moved&&page==0&&f.nightShiftRow==count)f.nightShiftRow=0;
        if(in.fired) {
            pointerShot=true;pointerHit=row>=0;
            if(page==3&&TimeBoard(f.nightShiftTable)) {
                if(Hit(in,535,86,566,115)){pointAction=kBossPrev;pointerHit=true;}
                if(Hit(in,574,86,605,115)){pointAction=kBossNext;pointerHit=true;}
            }
        }
        f.nightShiftAimX=in.x;f.nightShiftAimY=in.y;
    }
    if(buttons&0x10)f.nightShiftRow=(f.nightShiftRow+total-1)%total;
    if(buttons&0x20)f.nightShiftRow=(f.nightShiftRow+1)%total;
    if(page==3&&TimeBoard(f.nightShiftTable)) {
        if(buttons&0x40)Confirm(w,kBossPrev);
        if(buttons&0x80)Confirm(w,kBossNext);
    }
    if(buttons&2) { if(page!=0)Confirm(w,kHome); w.pressed&=~0xC000C; }
    else if((buttons&0xC)&&(!pointerShot||pointerHit)) {
        const int rail=f.nightShiftRow-count;
        const int action=pointAction>=0?pointAction:f.nightShiftRow<count?rows[f.nightShiftRow].action:
            rail==0?kSwitchGame:kHomeRoom;
        const int oldPage=f.nightShiftPage;
        Confirm(w,action);
        // A menu click must not join a player except when a gameplay mode was confirmed.
        if(!f.menuConfirmed)w.pressed&=~0xC000C;
        if(oldPage!=f.nightShiftPage)return;
    } else if(pointerShot&&!pointerHit) w.pressed&=~0xC000C;
    for(int i=0;i<count;++i) {
        const bool boardSel=page==3&&!f.typingHome&&((i>0&&i<4&&f.nightShiftTable==i-1)||
            (i==4&&TimeBoard(f.nightShiftTable))||(i==5&&f.nightShiftTable==kTypingBoardTable));
        const char* label=page==3&&i==0?(f.nightShiftShared?"SOURCE: ONLINE":"SOURCE: LOCAL"):rows[i].label;
        Button(w,left,top+i*pitch,right,top+i*pitch+height,label,modal?i==0:f.nightShiftRow==i,scale,boardSel);
    }
    if(page==0) {
        Panel(w,366,272,612,273,0xFF726B58);
        Text(w,366,250,"ONLINE LOBBIES",.72f);
        if(roomCount==0)Text(w,378,303,f.homeRoomsStatus.substr(0,29),.48f,0xFFB2B3AB);
        for(int i=0;i<roomCount;++i) {
            const auto& room=f.homeRooms[i];const float y=280+i*50.f;
            Panel(w,366,y,612,y+44,!modal&&f.nightShiftRow==count+1+i?0xED8F2026:0x75101315);
            Text(w,378,y+6,std::string(room.typing?"TYPING ":"ROOM ")+std::to_string(room.id),room.id>9999999?.48f:.60f);
            Text(w,535,y+8,room.privateRoom?"PRIVATE":"PUBLIC",.43f,0xFFD4BA96);
            static constexpr const char* difficulty[]={"VERY EASY","EASY","NORMAL","HARD","VERY HARD"};
            const char* level=room.difficulty>=0&&room.difficulty<5?difficulty[room.difficulty]:"UNKNOWN";
            Text(w,378,y+28,std::string(level)+" / "+(room.unlimited?"UNLIMITED":"3 CONTINUES"),.43f,0xFFC3C6BD);
        }
    } else if(page==1) {
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
    } else if(page==2) {
        Text(w,350,203,"SCENE SELECT",.64f,0xFFBBAA91);
        Text(w,350,239,"REPLAY A CHAPTER",.65f);
    } else if(page==3) {
        Leaderboard(w);
        Text(w,24,466,(f.leaderboardPublishStatus.empty()?"ELIGIBLE RUNS SUBMIT AUTOMATICALLY":f.leaderboardPublishStatus).substr(0,50),.40f,0xFFB9AA91);
    }
    Text(w,24,453,"ARROWS SELECT   ENTER / SHOOT CONFIRM   ESC / RELOAD BACK",.48f,0xFFB2B3AB);
    if(modal)TypingOptionsPage(w);
    LoadSprites(w,spriteStart);
}

void NightShiftNetworkRoomFrame(World::Impl& w,const std::array<const char*,9>& labels,
                                const std::string& difficulty) {
    w.hud.menuPanels.clear();
    w.hud.menuText.clear();
    const size_t first=w.hudSprites.size();
    // The classic font table pads short difficulty names to a fixed column.
    const auto firstLetter=difficulty.find_first_not_of(' ');
    const std::string difficultyLabel=firstLetter==std::string::npos?"":difficulty.substr(firstLetter);
    Panel(w,0,0,640,480,0xEC090D10);
    Panel(w,0,0,640,94,0x6505090B);
    Panel(w,26,65,614,67,0xFF92272D);
    Panel(w,0,440,640,480,0xC8090D10);
    Text(w,26,20,"MULTIPLAYER LOBBY",1.2f);
    Text(w,28,80,"CONNECTED / SELECT A GAME MODE",.43f,0xFFB9AA91);

    Panel(w,26,130,164,339,0x95101315);
    Panel(w,26,130,164,132,0xFF92272D);
    Text(w,40,146,"YOUR PARTY",.48f,0xFFB9AA91);
    Text(w,40,183,"PLAYER 1",.68f);
    Text(w,40,207,"HOST",.46f,0xFFB9AA91);
    Panel(w,40,240,150,241,0x354E514E);
    Text(w,40,263,"PLAYER 2",.68f);
    Text(w,40,287,"CONNECTED",.46f,0xFFB9AA91);

    // Match the existing lockstep pointer bands exactly, including their gaps.
    for(size_t i=0;i<labels.size();++i) {
        const float top=(5.f+float(i))*24.f+10.f;
        const bool selected=int(i)==w.front.menuCursor;
        Button(w,184,top,600,top+23,i==7?"DIFFICULTY":labels[i],selected,.67f);
        if(i==7) {
            Text(w,420,top+5,difficultyLabel,.67f,selected?0xFFFFF4E5:0xFFD1CCC2);
            if(selected) {
                Text(w,395,top+5,"<",.67f);
                Text(w,580,top+5,">",.67f);
            }
        }
    }
    Panel(w,184,365,600,419,0x75101315);
    Text(w,198,378,"UNLIMITED CONTINUES",.53f,0xFFB9AA91);
    Text(w,546,375,w.cfg.creditSetting<0?"ON":"OFF",.67f);
    Text(w,198,402,"PLAYER 1 CHOOSES THE MODE AND DIFFICULTY",.40f,0xFFB2B3AB);
    Text(w,28,454,"HOST: ARROWS SELECT / ADJUST   ENTER / SHOOT CONFIRM   ESC LEAVES ROOM",.40f,0xFFB2B3AB);
    LoadSprites(w,first);
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
void World::SetPortTypingLeaderboard(const std::vector<SharedLeaderboardEntry>& entries) {
    impl_->front.typingLeaderboard.assign(entries.begin(),entries.begin()+std::min<size_t>(10,entries.size()));
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
    (void)on;  // Legacy API: only the local client's automatic-submission status is displayed.
    impl_->front.leaderboardPublishable=false;
    impl_->front.leaderboardPublishStatus=status.substr(0,50);
}
void World::EditPortLeaderboardName(const std::string&,bool) {
    // Kept for compatibility with callers and snapshots from the manual-publish UI.
    // The saved username is local presentation state, outside the deterministic World.
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
    const bool typing=table==kTypingBoardTable;
    if(!w.portLeaderboardRecording||!w.portRunOriginEligible||w.portRunRecorded||!w.frontEnd||!w.cfg.portFeatures||
       (w.cfg.portTyping?(!typing||!w.cfg.typingOriginal):typing)||w.front.sceneSelect||
       (typing?w.front.sceneStart>0:w.front.sceneStart>=0)||w.prototype||RandomizerActive(w)||HordeActive(w))return;
    if(table<0||table>kTypingBoardTable||table==2)return;
    const int participants=std::clamp(std::max({w.players,w.portRunParticipants,
        int(w.portRunJoined[0])+int(w.portRunJoined[1])}),1,2);
    if(TimeBoard(table)&&(score<=0||participants!=1))return;
    LeaderboardRunSnapshot next;
    next.sequence=w.portLeaderboardRun.sequence+1;
    next.table=table; next.players=participants;
    next.difficulty=std::clamp(stageOrDifficulty,0,table<2||typing?6:4);
    for(int p=0;p<2;++p)next.playerScores[p]=!w.portRunJoined[p]?0:typing?
        int(std::min<uint64_t>(2000000000,w.TypingController(p).runPoints(unsigned(std::max(0,w.rescueTotal))))):std::max(0,w.player[p].score);
    next.score=table<2||typing?std::max(next.playerScores[0],next.playerScores[1]):score;
    if(next.score<=0)return;
    next.eligible=true;
    w.portLeaderboardRun=next;
    w.portRunRecorded=true;
}
void World::SetPortLeaderboardRecording(bool on) {impl_->portLeaderboardRecording=on;}
LeaderboardRunSnapshot World::PortLeaderboardRun() const {return impl_->portLeaderboardRun;}
void World::SetPortHomeRooms(const std::vector<PortHomeRoom>& rooms,const std::string& status) {
    auto& f=impl_->front;
    const int count=HomeCount(*impl_);
    const int old=f.nightShiftRow-count-1;
    const bool selectedSwitch=f.nightShiftPage==0&&f.nightShiftRow==count;
    const uint32_t selected=f.nightShiftPage==0&&old>=0&&old<int(f.homeRooms.size())?f.homeRooms[old].id:0;
    f.homeRooms.clear();
    for(const auto& r:rooms)if(r.id&&f.homeRooms.size()<2)f.homeRooms.push_back(r);
    f.homeRoomsStatus=status.substr(0,29);
    if(selected) {
        auto it=std::find_if(f.homeRooms.begin(),f.homeRooms.end(),[&](const auto& r){return r.id==selected;});
        f.nightShiftRow=it==f.homeRooms.end()?count:count+1+int(it-f.homeRooms.begin());
    } else if(selectedSwitch)f.nightShiftRow=count;
}
void World::ScrollPortExtras(int rows,int column) {
    auto& f=impl_->front;
    if(!impl_->cfg.portFeatures||impl_->gameMode!=4||f.nightShiftPage!=9||f.extrasPage==0)return;
    f.extrasWheel=std::clamp(f.extrasWheel+rows,-1000,1000);
    f.extrasWheelColumn=-1;  // The open media page owns the wheel; a hidden list cannot receive it.
    (void)column;
}
} // namespace game
