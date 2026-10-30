#include "game/world_typing_drill.h"
#include "game/world_internal.h"
#include "game/menu_text.h"
#include "game/world_typing_drill_visuals.h"
#include "game/world_typing_drill_scene.h"
#include "game/world_typing_drill_result_visuals.h"
#include "game/world_typing_drill_hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace game {
namespace {
constexpr uint32_t kText = 0xFFEDE9DF, kMuted = 0xFFAAAFA8, kGreen = 0xFF80E193;
void Panel(World::Impl& w,float l,float t,float r,float b,uint32_t color) { w.hud.menuPanels.push_back({l,t,r,b,color}); }
void Text(World::Impl& w,float x,float y,const std::string& text,float scale=.65f,uint32_t color=kText) {
    w.hud.menuText.push_back({x,y,text,scale,color});
    const size_t first=w.hudSprites.size();
    MenuTextScaled(w.exe,x,y-12*scale,text,0,scale,w.hudSprites);
    for(size_t i=first;i<w.hudSprites.size();++i) {
        auto& d=w.hudSprites[i];
        d.flags|=0x700; d.depth=.01f; d.tinted=true;
        d.tint[0]=((color>>16)&255)/255.f;d.tint[1]=((color>>8)&255)/255.f;d.tint[2]=(color&255)/255.f;
        d.alpha=((color>>24)&255)/255.f;
    }
}
bool Hit(const WorldInput& in,float l,float t,float r,float b) {
    return in.kind==1&&!in.offscreen&&in.x>=l&&in.x<r&&in.y>=t&&in.y<b;
}
bool Button(World::Impl& w,int id,int& selected,float l,float t,float r,float b,const std::string& label,bool enabled=true) {
    bool clicked=false;
    for(const auto& input:w.input) if(Hit(input,l,t,r,b)&&input.fired) { selected=id;clicked=true; }
    const bool active=selected==id;
    Panel(w,l,t,r,b,active?0xED8F2026:0xB512181B);
    if(active)Panel(w,l,t,l+3,b,0xFFF0DBBD);
    Text(w,l+10,t+9,label,.57f,enabled?(active?0xFFFFF4E5:kText):0xFF777D79);
    return clicked&&enabled;
}
std::string Number(double n,int digits=1) { char s[48];std::snprintf(s,sizeof(s),"%.*f",digits,n);return s; }
std::string Rules(const TypingDrillSpec& spec) {
    std::string rules=Number(spec.seconds,0)+" SEC  /  ";
    if(spec.quota)rules+=std::to_string(spec.quota)+" PROMPTS  /  ";
    if(spec.minimumWpm)rules+=Number(spec.minimumWpm,0)+" WPM  /  ";
    rules+=Number(spec.minimumAccuracy,0)+"% ACCURACY";
    if(spec.reactionLimit)rules+="  /  "+Number(spec.reactionLimit)+" SEC PER KEY";
    return rules;
}
void Begin(World::Impl& w,int exercise,bool supplementary=false) {
    auto& ui=w.typingDrill;
    if(w.typingReference.Available()) {
        const auto previousController=ui.controller;
        if(!ui.controller.beginOriginalAttempt(exercise,supplementary))return;
        ui.error.clear();
        auto game=std::make_unique<World>();
        World::Impl* previous=CurrentWorld();
        const bool started=game->Load(w.gameDir,0,&ui.error)&&
            game->StartTypingDrill(exercise,ui.controller,w.seedCounter,&ui.error,&w.cfg.typingOptions);
        SetCurrentWorld(previous);
        if(!started){ui.controller=previousController;ui.view=0;return;}
        ui.game=std::move(game);ui.view=1;ui.resultRow=0;ui.characters.clear();
        ui.cancel=ui.pauseRequest=false;ui.heldDraws.clear();ui.heldSprites.clear();
        w.SoundStopAll();if(!supplementary)w.TypingSound(0x000121A9u);return;
    }
    if(ui.controller.start(exercise,0x4452494Cu^uint32_t(w.frame),supplementary)) {
        ui.view=1;ui.resultRow=0;ui.characters.clear();
        w.Sound(0x121A9);
    }
}
bool OriginalSelection(World::Impl& w,uint32_t buttons) {
    if(!w.typingReference.Available())return false;
    auto& ui=w.typingDrill;
    constexpr int order[]={0,1,2,6,7,8,3,4,5,9,10,11,12,13};
    std::vector<int> available;
    for(int exercise:order)if(exercise==13||ui.controller.unlocked(exercise))available.push_back(exercise);
    auto at=std::find(available.begin(),available.end(),ui.selected);
    if(at==available.end()){ui.selected=available.front();at=available.begin();}
    int position=int(at-available.begin());
    int direction=(buttons&(0x40|0x10))?-1:(buttons&(0x80|0x20))?1:0;
    const uint32_t held=(w.held&0xfff)|((w.held>>16)&0xfff);
    const int heldDirection=(held&0x50u)?-1:(held&0xA0u)?1:0;
    if(heldDirection!=ui.cursorHeldDirection){ui.cursorHeldDirection=heldDirection;ui.cursorHeldTicks=0;}
    else if(heldDirection&&++ui.cursorHeldTicks>=60)direction=heldDirection;
    bool confirm=(buttons&0xc)!=0;
    for(const auto& input:w.input)if(input.kind==1&&input.fired) {
        confirm=false;
        if(Hit(input,90,132,202,238))direction=-1;
        else if(Hit(input,438,132,550,238))direction=1;
        else if(Hit(input,210,90,430,294))confirm=true;
    }
    const bool sliding=ui.visualSelected>=0&&uint64_t(std::max(0,w.frame))<ui.visualChangedFrame+10;
    if(direction&&!sliding) {
        position=(position+direction+int(available.size()))%int(available.size());
        ui.selected=available[position];
        w.TypingSound(0x000000A9u);
    }
    TypingDrillSelectionVisuals(w);
    if(!ui.error.empty())Text(w,24,448,"DRILL DATA ERROR: "+ui.error.substr(0,70),.4f,0xFFFFB9A8);
    if(buttons&2) {w.front.nightShiftPage=0;w.front.nightShiftRow=1;w.SoundStopAll();w.TypingSound(0x000500A9u);return true;}
    if(confirm&&!sliding) {
        if(ui.selected<13)Begin(w,ui.selected);
        else {ui.view=3;ui.record=0;w.TypingSound(0x000121A9u);BeginTypingDrillRecords(w);}
    }
    return true;
}
void Selection(World::Impl& w,uint32_t buttons) {
    auto& ui=w.typingDrill;
    if(OriginalSelection(w,buttons))return;
    if(buttons&0x10)ui.selected=(ui.selected+14)%15;
    if(buttons&0x20)ui.selected=(ui.selected+1)%15;
    if(buttons&0x40)ui.selected=(ui.selected+12)%15;
    if(buttons&0x80)ui.selected=(ui.selected+3)%15;
    bool confirm=(buttons&0xc)!=0;
    Text(w,26,78,"TYPING OF THE DEAD / DRILL",.95f);
    Text(w,26,110,"CLEAR EACH LEVEL TO UNLOCK THE NEXT. CLEAR ALL 12 FOR THE FINAL EXAM.",.43f,kMuted);
    const auto exercises=TypingDrillController::exercises();
    static constexpr const char* categories[]={"SPEED","REFLEX","ACCURACY","SPECIAL KEYS"};
    for(int i=0;i<12;++i) {
        const float x=24+(i%3)*202.f,y=143+(i/3)*43.f;
        std::string label=std::string(categories[i/3])+" "+std::to_string(i%3+1);
        if(ui.controller.clears()[i])label+=" +";
        if(!ui.controller.unlocked(i))label+=" [LOCKED]";
        confirm=Button(w,i,ui.selected,x,y,x+188,y+34,label,ui.controller.unlocked(i))||confirm;
    }
    confirm=Button(w,12,ui.selected,24,321,616,355,
        ui.controller.unlocked(12)?"FINAL EXAM":"FINAL EXAM [CLEAR ALL 12 DRILLS]",ui.controller.unlocked(12))||confirm;
    confirm=Button(w,13,ui.selected,24,365,314,399,"RECORDS")||confirm;
    confirm=Button(w,14,ui.selected,326,365,616,399,"BACK")||confirm;
    if(ui.selected<13)Text(w,26,414,Rules(exercises[ui.selected]),.43f,kMuted);
    if(buttons&2) {w.front.nightShiftPage=0;w.front.nightShiftRow=1;return;}
    if(confirm) {
        if(ui.selected<13)Begin(w,ui.selected);
        else if(ui.selected==13) {ui.view=3;ui.record=0;}
        else {w.front.nightShiftPage=0;w.front.nightShiftRow=1;}
    }
    Text(w,24,453,"ARROWS SELECT   ENTER START   ESC BACK     + = CLEARED",.43f,kMuted);
}
void Running(World::Impl& w,uint32_t buttons) {
    auto& ui=w.typingDrill;
    auto& drill=ui.controller;
    if(ui.game) {
        if(buttons&2) {
            const auto phase=ui.game->TypingDrillGame().phase;
            if(phase==TypingDrillGamePhase::Instructions) {
                ui.game.reset();drill.abandon();ui.characters.clear();ui.view=0;w.SoundStopAll();return;
            }
            if(phase==TypingDrillGamePhase::Running)ui.characters+='\x1b';
        }
        WorldInput input;
        input.pressed=buttons&0xc;input.held=input.pressed;
        if(input.pressed&8)input.pressed=input.held=4;
        if(ui.pauseRequest){input.pressed=input.held=8;ui.pauseRequest=false;}
        ui.game->SetInput(0,input);ui.game->SetInput(1,{});
        ui.game->PortTypingInput(ui.characters,ui.cancel);
        ui.characters.clear();ui.cancel=false;
        World::Impl* previous=CurrentWorld();
        ui.game->Step();
        SetCurrentWorld(previous);
        w.sounds.insert(w.sounds.end(),ui.game->Sounds().begin(),ui.game->Sounds().end());
        if(!ui.game->Hud().paused) {
            ui.heldDraws=ui.game->Draws();ui.heldSprites=ui.game->Hud().sprites;
        }
        if(ui.game->Events().error) {
            ui.error=ui.game->Events().errorText;ui.game.reset();drill.abandon();ui.view=0;return;
        }
        if(ui.game->TypingDrillGame().resultsReady) {
            w.PresentTypingDrillGame();
            const bool supplementary=drill.supplementary();
            drill.setOriginalKeyWeights(ui.game->TypingDrillKeyWeights());
            drill.recordOriginalResult(drill.exercise(),ui.game->TypingDrillOutcome(),supplementary);
            ui.game.reset();ui.view=supplementary?0:2;ui.resultRow=0;
            if(!supplementary)BeginTypingDrillResults(w);
        }
        return;
    }
    if(buttons&2) {drill.abandon();ui.characters.clear();ui.view=0;return;}
    // The front end advances at the same fixed sixty-Hz step as the campaign.
    drill.step(1.0/60.0);
    drill.type(ui.characters);
    ui.characters.clear();
    if(TypingDrillSceneVisuals(w)) {
        TypingDrillHudVisuals(w);
        DrawTypingDrillPrompt(w,drill.prompt(),drill.progress());
        if(drill.finished()){ui.view=2;ui.resultRow=0;}
        return;
    }
    Text(w,26,78,drill.specification().name,.95f);
    if(drill.supplementary())Text(w,26,110,"SUPPLEMENTARY / WEAK KEY PRACTICE",.50f,kGreen);
    else Text(w,26,110,Rules(drill.specification()),.43f,kMuted);
    Panel(w,24,147,616,313,0xED080D0F);
    Panel(w,24,147,616,150,0xFF46B858);
    Text(w,44,164,"TYPE THE PROMPT",.50f,kMuted);
    const auto prompt=drill.prompt();
    constexpr size_t width=42;
    const size_t line=drill.progress()/width;
    const size_t start=line*width;
    Text(w,44,214,std::string(prompt.substr(start,width)),.98f);
    const size_t done=std::min(drill.progress()-start,width);
    if(done)Text(w,44,214,std::string(prompt.substr(start,done)),.98f,kGreen);
    if(start+width<prompt.size())Text(w,44,252,std::string(prompt.substr(start+width,width)),.65f,kMuted);
    const auto& result=drill.result();
    Text(w,28,332,"TIME "+Number(std::ceil(drill.remaining()),0),.65f);
    Text(w,230,332,"PROMPTS "+std::to_string(result.words),.65f);
    Text(w,430,332,"ERRORS "+std::to_string(result.errors),.65f);
    Text(w,28,369,"WPM "+Number(result.wpm())+"    ACCURACY "+Number(result.accuracy())+"%",.65f);
    if(drill.specification().reactionLimit) {
        const float fraction=float(drill.reactionRemaining()/drill.specification().reactionLimit);
        Panel(w,28,404,612,413,0xFF392B2A);
        Panel(w,28,404,28+584*fraction,413,fraction>.3f?0xFF59C975:0xFFD74943);
        Text(w,430,369,"MISSED "+std::to_string(result.missed)+" / 3",.55f);
    }
    Text(w,24,453,"SPACES AND CAPITALIZATION ARE AUTOMATIC   BACKSPACE RESET WORD   ESC QUIT",.39f,kMuted);
    if(drill.finished()){ui.view=2;ui.resultRow=0;}
}
void Results(World::Impl& w,uint32_t buttons) {
    auto& ui=w.typingDrill;
    const auto& drill=ui.controller;
    const auto& result=drill.result();
    if(w.typingReference.Available()) {
        switch(TypingDrillResultsFrame(w,buttons)) {
        case TypingDrillBookAction::Name:BeginTypingDrillName(w);ui.view=4;break;
        case TypingDrillBookAction::Supplementary:Begin(w,drill.exercise(),true);break;
        case TypingDrillBookAction::Select:ui.view=0;break;
        default:break;
        }
        return;
    }
    if(buttons&0x10)ui.resultRow=(ui.resultRow+2)%3;
    if(buttons&0x20)ui.resultRow=(ui.resultRow+1)%3;
    bool confirm=(buttons&0xc)!=0;
    if(TypingDrillResultsVisuals(w)) {
        if(buttons&2){ui.view=0;return;}
        if(confirm) {
            if(ui.resultRow==0)Begin(w,drill.exercise());
            else if(ui.resultRow==1&&!drill.weakKeys().empty())Begin(w,drill.exercise(),true);
            else if(ui.resultRow==2)ui.view=0;
        }
        return;
    }
    Text(w,26,78,drill.specification().name,.95f);
    Text(w,28,119,result.passed?"DRILL CLEARED":"KEEP PRACTICING",.95f,result.passed?kGreen:kText);
    Text(w,28,166,"SKILL EVALUATION   "+std::to_string(result.evaluation),.80f);
    Text(w,28,206,"TYPING SPEED   "+Number(result.wpm())+" WPM",.65f);
    Text(w,28,239,"ACCURACY   "+Number(result.accuracy(),2)+"%",.65f);
    Text(w,28,272,"PROMPTS   "+std::to_string(result.words)+"     ERRORS   "+std::to_string(result.errors),.65f);
    const auto weak=drill.weakKeys();
    Text(w,28,307,weak.empty()?"NO WEAK KEYS RECORDED":"WEAK KEYS   "+weak,.58f,kMuted);
    if(drill.supplementary())Text(w,28,335,"SUPPLEMENTARY RESULTS DO NOT CHANGE MAIN EXERCISE RECORDS.",.43f,kMuted);
    confirm=Button(w,0,ui.resultRow,24,365,209,405,"RETRY")||confirm;
    confirm=Button(w,1,ui.resultRow,222,365,415,405,"SUPPLEMENTARY",!weak.empty())||confirm;
    confirm=Button(w,2,ui.resultRow,428,365,616,405,"DRILL SELECT")||confirm;
    if(buttons&2){ui.view=0;return;}
    if(confirm) {
        if(ui.resultRow==0)Begin(w,drill.exercise());
        else if(ui.resultRow==1&&!weak.empty())Begin(w,drill.exercise(),true);
        else if(ui.resultRow==2)ui.view=0;
    }
    Text(w,24,453,"ARROWS SELECT   ENTER CONFIRM   ESC DRILL SELECT",.43f,kMuted);
}
void Records(World::Impl& w,uint32_t buttons) {
    auto& ui=w.typingDrill;
    if(w.typingReference.Available()) {
        if(TypingDrillRecordsFrame(w,buttons)==TypingDrillBookAction::Select)ui.view=0;
        return;
    }
    constexpr int order[]={0,1,2,6,7,8,3,4,5,9,10,11,12};
    int position=w.typingReference.Available()?int(std::find(std::begin(order),std::end(order),ui.record)-std::begin(order)):ui.record;
    if(buttons&(0x10|0x40))position=(position+12)%13;
    if(buttons&(0x20|0x80|0xc))position=(position+1)%13;
    ui.record=w.typingReference.Available()?order[position]:position;
    if(buttons&2){ui.view=0;return;}
    if(TypingDrillRecordsVisuals(w))return;
    Text(w,26,78,"DRILL RECORDS",.95f);
    Text(w,26,116,TypingDrillController::exercises()[ui.record].name,.75f);
    const auto& table=ui.controller.records(ui.record);
    Text(w,28,157,"RANK    SKILL      WPM      ACCURACY      RESULT",.57f,kMuted);
    if(table.empty())Text(w,28,197,"NO RECORDS YET",.70f,kMuted);
    for(size_t i=0;i<table.size();++i) {
        const auto& r=table[i];const float y=191+30*float(i);
        Text(w,28,y,std::to_string(i+1),.65f);
        Text(w,126,y,std::to_string(r.evaluation),.65f);
        Text(w,227,y,Number(r.wpm()),.65f);
        Text(w,342,y,Number(r.accuracy(),2)+"%",.65f);
        Text(w,492,y,r.passed?"CLEAR":"TRY AGAIN",.55f,r.passed?kGreen:kMuted);
    }
    Text(w,28,354,"RECENT SKILL EVALUATIONS",.50f,kMuted);
    const auto& history=ui.controller.history(ui.record);
    for(size_t i=0;i<history.size();++i) {
        const float x=28+float(i)*57;
        Panel(w,x,435-55*history[i]/100.f,x+35,435,0xFF59B779);
        Text(w,x,371,std::to_string(history[i]),.40f,kMuted);
    }
    Text(w,24,453,"ARROWS / ENTER CHANGE EXERCISE   ESC DRILL SELECT",.43f,kMuted);
}
}

void TypingDrillMenuFrame(World::Impl& w) {
    auto& ui=w.typingDrill;
    ui.controller.setProfilePath(w.cfg.savePath.empty()?std::string{}:w.cfg.savePath+".typing.drill");
    const bool previousReference=w.typingReferenceActive;
    const bool previousTyping=w.cfg.portTyping;
    const bool original=w.typingReference.Available();
    if(original) {
        w.typingReferenceActive=true;
        w.cfg.portTyping=true;
        if(w.modelLoaded.size()<0x8000)w.modelLoaded.resize(0x8000);
        if(ui.visualView<0) {w.SoundStopAll();w.TypingSound(0x10000027u);}
    }
    if(ui.visualView!=ui.view) {
        ui.visualView=ui.view;ui.visualAge=0;
        if(ui.view==0){ui.visualSelected=-1;ui.visualDescription=false;ui.cursorHeldDirection=ui.cursorHeldTicks=0;}
    }
    else ++ui.visualAge;
    const uint32_t buttons=(w.pressed&0xfff)|((w.pressed>>16)&0xfff);
    switch(ui.view) {
    case 1:Running(w,buttons);break;
    case 2:Results(w,buttons);break;
    case 3:Records(w,buttons);break;
    case 4:if(TypingDrillNameFrame(w,buttons))ui.view=2;break;
    default:ui.view=0;Selection(w,buttons);break;
    }
    w.typingReferenceActive=previousReference;
    w.cfg.portTyping=previousTyping;
    w.pressed&=~0xc000c;
}
bool QueueTypingDrillText(World::Impl& w,const std::string& text,bool cancel) {
    if(w.gameMode!=4||w.front.nightShiftPage!=12)return false;
    if(w.typingDrill.view==4) {
        if(cancel)QueueTypingDrillName(w,"\b");
        else QueueTypingDrillName(w,text);
        return true;
    }
    if(w.typingDrill.view==1&&w.typingDrill.controller.running()) {
        if(cancel&&w.typingDrill.game)w.typingDrill.cancel=true;
        else if(cancel)w.typingDrill.controller.cancelWord();
        else for(unsigned char c:text)if(((c>=32&&c<127)||(c==27&&w.typingDrill.game))&&w.typingDrill.characters.size()<256)w.typingDrill.characters+=char(c);
    }
    return true;
}
bool TypingDrillInputActive(const World::Impl& w) {
    return w.gameMode==4&&w.front.nightShiftPage==12&&
        ((w.typingDrill.view==1&&w.typingDrill.controller.running())||TypingDrillNameActive(w));
}
} // namespace game
