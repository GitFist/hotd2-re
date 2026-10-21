#include "game/world_nightshift.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include "game/menu_text.h"
#include "game/world_modes.h"

namespace game {
namespace {
struct Row { const char* label; int action; };
enum { kPlay=100, kPractice, kLeaderboard, kOnline, kOptions, kExit, kHome, kHorde, kRandomizer, kScene,
       kHost, kBrowse, kPeer, kArcadeBoard, kOriginalBoard, kHordeBoard, kBossBoard, kBossPrev, kBossNext, kBoardSource, kPublish,
       kDisplay, kImage, kLighting, kSystem, kPacks, kGameOptions, kHomeRoom };
constexpr Row kHomeRows[]={{"PLAY",kPlay},{"PRACTICE",kPractice},{"LEADERBOARD",kLeaderboard},{"ONLINE",kOnline},{"OPTIONS",kOptions},{"EXIT",kExit}};
constexpr Row kPlayRows[]={{"ARCADE",0},{"ORIGINAL",1},{"BOSS MODE",3},{"HORDE MODE",kHorde},{"RANDOMIZER",kRandomizer},{"BACK",kHome}};
constexpr Row kPracticeRows[]={{"TRAINING",2},{"SCENE SELECT",kScene},{"BACK",kHome}};
constexpr Row kBoardRows[]={{"SOURCE: LOCAL",kBoardSource},{"ARCADE",kArcadeBoard},{"ORIGINAL",kOriginalBoard},{"HORDE",kHordeBoard},{"BOSS MODE",kBossBoard},{"PUBLISH NEW RUN",kPublish},{"BACK",kHome}};
constexpr Row kOptionsRows[]={{"DISPLAY",kDisplay},{"IMAGE CLARITY",kImage},{"LIGHTING",kLighting},{"SYSTEM",kSystem},{"TEXTURE PACKS",kPacks},{"GAME OPTIONS",kGameOptions},{"BACK",kHome}};
constexpr const char* kBoss[] = {"JUDGMENT","HIEROPHANT A","HIEROPHANT B","TOWER A","TOWER B","STRENGTH A","STRENGTH B","MAGICIAN","EMPEROR","BOSS RUSH"};

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
    auto page=[&](int p) { f.nightShiftPage=p; f.nightShiftRow=0; w.Sound(0x121A9); };
    switch(action) {
    case kPlay: page(1); return;
    case kPractice: page(2); return;
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
        const int i=f.nightShiftRow-6;
        if(i<0||i>=int(f.homeRooms.size()))return;
        f.portLobbyRoom=f.homeRooms[i].id;f.portLobbyAction=4;++f.portLobbyRequests;
        ModesMenuConfirm(w,6);return;
    }
    case kOptions: page(5); return;
    case kDisplay: case kImage: case kLighting: case kSystem: case kPacks:
        f.nightShiftDisplayPage=action-kDisplay;++f.nightShiftDisplayRequests;return;
    case kGameOptions: f.nightShiftReturnPage=5;ModesMenuConfirm(w,5);return;
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
    Text(w,218,152,"NO",.55f,0xFFB9AA91);
    Text(w,256,152,table==2?(w.front.nightShiftShared?"NAME RD":"ROUND"):"NAME",.55f,0xFFB9AA91);
    Text(w,360,152,table>=3?"TIME M S CS":"SCORE",table>=3?.43f:.55f,0xFFB9AA91);
    Text(w,505,152,table>=3||table==2?"LEVEL":"STAGE",.55f,0xFFB9AA91);
    if(table==2||w.front.nightShiftShared)Text(w,570,152,"PL",.55f,0xFFB9AA91);
    const bool shared=w.front.nightShiftShared;
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
            Text(w,256,y,table==2?e.name.substr(0,3)+" "+Score(e.rounds):e.name.substr(0,9),table==2?.55f:.62f);
            Text(w,360,y,table>=3?Time(e.score):Score(e.score),.62f);
            Text(w,505,y,Score(e.difficulty+1),.62f);
            Text(w,574,y,Score(std::clamp(e.players,1,2)),.62f);
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

void PublishPage(World::Impl& w) {
    auto& f=w.front;
    const uint32_t buttons=(w.pressed&0xFFF)|((w.pressed>>16)&0xFFF);
    auto move=[&](int dir){f.publishCursor=(f.publishCursor+5+dir)%5;};
    if(buttons&0x40)move(-1);
    if(buttons&0x80)move(1);
    if((buttons&0x30)&&f.publishCursor<3) {
        char& c=f.leaderboardInitials[f.publishCursor];
        const int n=(c-'A'+26+((buttons&0x10)?1:-1))%26;c=char('A'+n);
    } else if(buttons&0x10)move(-1);
    else if(buttons&0x20)move(1);
    int action=-1;
    for(int p=0;p<2;++p) {
        const auto& in=w.input[p];if(in.kind!=1||!in.fired)continue;
        for(int i=0;i<3;++i) {
            const float x=220+i*68.f;
            if(Hit(in,x,176,x+52,204)){f.publishCursor=i;f.leaderboardInitials[i]=char('A'+(f.leaderboardInitials[i]-'A'+1)%26);}
            if(Hit(in,x,215,x+52,267))f.publishCursor=i;
            if(Hit(in,x,278,x+52,306)){f.publishCursor=i;f.leaderboardInitials[i]=char('A'+(f.leaderboardInitials[i]-'A'+25)%26);}
        }
        if(Hit(in,202,337,438,374)){f.publishCursor=3;action=3;}
        if(Hit(in,202,385,438,422)){f.publishCursor=4;action=4;}
    }
    if(buttons&2)action=4;
    else if((buttons&0xC)&&!w.input[0].fired&&!w.input[1].fired)action=f.publishCursor;
    if(action>=0&&action<3)move(1);
    else if(action==3&&f.leaderboardPublishable) {
        ++f.portLeaderboardPublishRequests;f.leaderboardPublishable=false;
        f.nightShiftPage=3;f.nightShiftRow=5;
    } else if(action==4){f.nightShiftPage=3;f.nightShiftRow=5;}
    w.pressed&=~0xC000C;
    Panel(w,176,83,464,432,0xED0B1012);
    Text(w,202,102,"PUBLISH NEW RUN",.95f);
    Text(w,202,136,"CHOOSE YOUR INITIALS",.62f,0xFFB9AA91);
    Text(w,202,160,"ONLINE / UNVERIFIED",.49f,0xFFA6ACA8);
    for(int i=0;i<3;++i) {
        const float x=220+i*68.f;
        Button(w,x,176,x+52,204,"UP",false,.47f);
        Panel(w,x,215,x+52,267,f.publishCursor==i?0xFF8F2026:0xFF182225);
        Text(w,x+15,221,std::string(1,f.leaderboardInitials[i]),1.3f);
        Button(w,x,278,x+52,306,"DN",false,.47f);
    }
    Button(w,202,337,438,374,"PUBLISH",f.publishCursor==3,.84f);
    Button(w,202,385,438,422,"CANCEL",f.publishCursor==4,.84f);
    Text(w,24,453,"ARROWS CHANGE INITIALS   ENTER SELECT   ESC / RELOAD BACK",.48f,0xFFB2B3AB);
}
} // namespace

void NightShiftMenuFrame(World::Impl& w) {
    auto& f=w.front;
    w.hud.menuPanels.clear();
    w.hud.menuText.clear();
    const size_t spriteStart=w.hudSprites.size();
    // Subtle continuous shading keeps the original attract scenes visible behind the UI.
    Panel(w,0,0,640,480,0x3503090B);
    for(int x=0;x<400;x+=16)Panel(w,float(x),0,float(x+16),480,(uint32_t(std::max(0,190-x/3))<<24)|0x05090B);
    Panel(w,0,440,640,480,0xC8090D10);
    Logo(w,f.nightShiftPage!=0);
    if(f.nightShiftPage==4) {PublishPage(w);LoadSprites(w,spriteStart);return;}
    const Row* rows=kHomeRows; int count=6;
    float left=24,right=322,top=158,pitch=42,height=36,scale=1.05f;
    if(f.nightShiftPage==1) {rows=kPlayRows;Text(w,26,83,"PLAY",1.45f);top=136;pitch=45;height=40;}
    else if(f.nightShiftPage==2) {rows=kPracticeRows;count=3;Text(w,26,83,"PRACTICE",1.45f);top=140;pitch=52;height=44;}
    else if(f.nightShiftPage==3) {rows=kBoardRows;count=7;Text(w,26,78,"LEADERBOARD",.81f);top=130;pitch=40;height=35;right=187;scale=.60f;}
    else if(f.nightShiftPage==5) {rows=kOptionsRows;count=7;Text(w,26,83,"OPTIONS",1.45f);top=130;pitch=40;height=35;scale=.87f;}
    const int roomCount=f.nightShiftPage==0?int(f.homeRooms.size()):0;
    const int total=count+(f.nightShiftPage==0?3+roomCount:0);
    f.nightShiftRow=std::clamp(f.nightShiftRow,0,total-1);
    uint32_t buttons=(w.pressed&0xFFF)|((w.pressed>>16)&0xFFF);
    bool pointerShot=false, pointerHit=false;
    int pointAction=-1;
    for(int q=0;q<2;++q) {
        const auto& in=w.input[(f.startPlayer+q)&1];
        if(in.kind!=1)continue;
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
        break;
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
            Text(w,378,y+28,room.unlimited?"UNLIMITED":"3 CONTINUES",.43f,0xFFC3C6BD);
        }
        const char* names[]={"HOST ONLINE","BROWSE LOBBY","PEER TO PEER"};
        for(int i=0;i<3;++i)Button(w,366,282+i*40,612,317+i*40,names[i],f.nightShiftRow==count+roomCount+i,.73f);
    } else if(f.nightShiftPage==1) {
        const char* help[]={"THE ORIGINAL ARCADE CAMPAIGN","CAMPAIGN WITH ITEMS AND LOADOUTS","TIME ATTACK AGAINST EACH BOSS","SURVIVE WAVES AND UPGRADE BETWEEN ROUNDS","A RANDOMIZED ORIGINAL MODE RUN","RETURN TO THE MAIN MENU"};
        Text(w,350,198,"SELECT MODE",.64f,0xFFBBAA91);
        std::string description=help[std::clamp(f.nightShiftRow,0,5)];
        // Wrap at whole words within the right column, using the real menu font's advance.
        size_t start=0;int line=0;
        while(start<description.size()) {
            size_t end=std::min(start+23,description.size());
            if(end<description.size()) {auto space=description.rfind(' ',end);if(space>start)end=space;}
            Text(w,350,235+line*25,description.substr(start,end-start),.65f);
            start=end;while(start<description.size()&&description[start]==' ')++start;++line;
        }
    } else if(f.nightShiftPage==2) {
        Text(w,350,203,"TRAINING LESSONS",.64f,0xFFBBAA91);
        Text(w,350,239,"OR CHOOSE A CHAPTER",.65f);
    } else if(f.nightShiftPage==3) {
        Leaderboard(w);
        if(f.nightShiftRow==5)Text(w,24,421,(f.leaderboardPublishStatus.empty()?"COMPLETE A NEW RUN FIRST":f.leaderboardPublishStatus).substr(0,50),.45f,0xFFB9AA91);
    } else if(f.nightShiftPage==5) {
        const char* help[]={"FULLSCREEN AND WINDOWED DISPLAY", "ANTI-ALIASING AND TEXTURE FILTERING", "LIGHTING AND COLOR PRESENTATION", "RENDERER AND FRAME DELIVERY", "CHOOSE AN INSTALLED TEXTURE PACK", "EXISTING GAME RULES AND SETUP", "RETURN TO THE MAIN MENU"};
        Text(w,350,195,"OPTIONS",.64f,0xFFBBAA91);
        std::string description=help[std::clamp(f.nightShiftRow,0,6)];
        size_t start=0;int line=0;
        while(start<description.size()) {
            size_t end=std::min(start+23,description.size());
            if(end<description.size()){auto space=description.rfind(' ',end);if(space>start)end=space;}
            Text(w,350,230+line*25,description.substr(start,end-start),.65f);
            start=end;while(start<description.size()&&description[start]==' ')++start;++line;
        }
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
    Text(w,28,80,"GAME RULES / SOUND PREVIEW / DEVICE SETUP",.43f,0xFFB9AA91);
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
        e.name.resize(std::min<size_t>(9,e.name.size()));
        for(char& c:e.name)if(c<32||c>126)c='?';
    }
    f.sharedLeaderboardStatus=status.substr(0,32);
}
void World::SetPortLeaderboardPublishable(bool on,const std::string& status) {
    impl_->front.leaderboardPublishable=on;
    impl_->front.leaderboardPublishStatus=status.substr(0,50);
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
    const int old=f.nightShiftRow-6;
    const int oldRoomCount=int(f.homeRooms.size());
    const int action=f.nightShiftPage==0&&old>=oldRoomCount&&old<oldRoomCount+3?old-oldRoomCount:-1;
    const uint32_t selected=f.nightShiftPage==0&&old>=0&&old<int(f.homeRooms.size())?f.homeRooms[old].id:0;
    f.homeRooms.clear();
    for(const auto& r:rooms)if(r.id&&f.homeRooms.size()<2)f.homeRooms.push_back(r);
    f.homeRoomsStatus=status.substr(0,29);
    if(selected) {
        auto it=std::find_if(f.homeRooms.begin(),f.homeRooms.end(),[&](const auto& r){return r.id==selected;});
        f.nightShiftRow=6+int(it-f.homeRooms.begin());  // missing selection safely moves to Host
    } else if(action>=0)f.nightShiftRow=6+int(f.homeRooms.size())+action;
}
} // namespace game
