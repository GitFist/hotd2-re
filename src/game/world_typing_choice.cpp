#include "game/world_internal.h"
#include "game/mstack.h"
#include <algorithm>
#include <cmath>

namespace game {
namespace {
void IdleChoice(Task&) {}

void DrawChoice(World::Impl& w) {
    const auto& choice=w.typingChapterChooser;
    const auto& status=w.typing.chapterChoice().status;
    // Source scene0 selects section4: tables5922FC/592334 resolve its
    // library model1289 (st1_02.bin), viewed from camera preset0 (0,17,0).
    // Combat is frozen here, so use its own view without changing the rail camera.
    const int model=w.LoadTypingModel(w.typingReference.SourceModel(0x1289));
    Mat4 view=Mat4::Identity();mx::Translate(view,0,-17,0);
    if (model) if (auto* draw=w.Draw(model,view,8,-1,false,-1,true)) {
        draw->fn=0x00493C60;draw->light.table=false;
        draw->light.pitch=9410;draw->light.yaw=39558;
        draw->light.intensity=.7f;
        for (float& color:draw->light.color) color=.6f;
    }
    // Source493B90/493C60: six cards orbit the authored screen-space centre.
    w.TypingSprite(0xB9A,320,80,1,.1f,0xA,0x00494280);
    w.TypingSprite(0xBD6,192,80,1,.05f,0,0x00494280);
    for (int chapter=0;chapter<6;++chapter) {
        const float angle=(choice.rotation-float(chapter)*60.f)*.017453292519943295f;
        const float cosine=std::cos(angle);
        const float x=320.f+std::sin(angle)*204.f, y=200.f+cosine*40.f;
        const float scale=1.f-(1.f-cosine)*.17749999463558197f;
        const float alpha=(cosine+1.f)*.5f;
        const auto sprite=[&](int id,float depth) {
            const size_t before=w.hudSprites.size();
            w.TypingSprite(id,x,y,alpha,depth,0xA,0x00493C60);
            if (w.hudSprites.size()!=before) w.hudSprites.back().sx=w.hudSprites.back().sy=scale;
        };
        sprite(0xE42+chapter,.1f-cosine*20.f+21.f);
        if (status[chapter]) sprite(status[chapter]==1?0xB9C:0xB92,.1f-cosine*20.f+20.5f);
    }
    w.TypingSprite(0xE26,320,384,1,.2f,0xA,0x00493C60);
    w.TypingSprite(0xB94+choice.selected,320,379,status[choice.selected]? .5f:1.f,.1f,0xA,0x00493C60);
}
}

bool StartTypingChapterChoice(World::Impl& w) {
    if (!w.cfg.portTyping || !w.typingReferenceActive || w.cfg.gameType!=0 ||
        w.cfg.eventMode<0 || w.cfg.eventMode>=5) return false;
    auto& chooser=w.typingChapterChooser;
    const auto& status=w.typing.chapterChoice().status;
    const auto first=std::find(status.begin(),status.end(),uint8_t(0));
    if (first==status.end()) return false;
    chooser={};
    chooser.active=true;
    chooser.selected=int(first-status.begin()); // Source493B90 selects the first available chapter.
    chooser.rotation=chooser.desired=float(chooser.selected)*60.f;
    chooser.savedFlags=w.flags5900;
    w.flags5900|=0x10;
    w.typingResultsActive=true;
    w.typing.syncTargets({});
    w.typing2.syncTargets({});
    w.typingCharacters.clear();
    w.typingCharacters2.clear();
    w.typingQuestion.clear(); w.typingQuestionSeconds=0;
    w.targets.clear(); w.targetCopy.clear();
    w.hud.menuText.clear(); w.hud.menuPanels.clear();
    w.NewTask<WTask>(IdleChoice,0x00493C60);
    w.attackers=1; // Source494770's chooser owns shared script counterA34D26.
    DrawChoice(w);
    return true;
}

bool TypingChapterChoiceFrame(World::Impl& w) {
    auto& chooser=w.typingChapterChooser;
    if (!chooser.active) return false;
    // Source494280 freezes combat while the selector owns the shared counter.
    // Its card rotation advances2.5degrees/tick; the first60ticks reject Enter.
    const uint32_t buttons=(w.pressed&0xffffu)|(w.pressed>>16);
    if (chooser.rotation==chooser.desired) {
        const int direction=buttons&0x40 ? 1 : buttons&0x80 ? -1 : 0;
        if (direction) {
            chooser.selected=(chooser.selected+6+direction)%6;
            chooser.desired+=float(direction)*60.f;
            w.TypingSound(0x003E16A9u);
        }
    }
    const float difference=chooser.desired-chooser.rotation;
    chooser.rotation+=std::clamp(difference,-2.5f,2.5f);
    --chooser.remaining;
    const bool confirm=(buttons&0xCu) && chooser.remaining<660 && chooser.rotation==chooser.desired;
    const bool timeout=chooser.remaining<0;
    if (confirm || timeout) {
        const auto& choices=w.typing.chapterChoice();
        if (timeout && !choices.canPlay(chooser.selected+1)) {
            for (int chapter=1;chapter<=6;++chapter) if (choices.canPlay(chapter)) {
                chooser.selected=chapter-1;break;
            }
        }
        if (choices.canPlay(chooser.selected+1)) {
            // Source4945A6 resumes the selected chapter at scene0, variant0.
            // Its first4F advances to that scene's authored intro variants.
            // ChainStage carries player/run state; StartStage resets its chapter buckets.
            w.nextEventMode=chooser.selected;
            w.runner->scene=0;
            w.subState=1;
            w.flags5900=chooser.savedFlags;
            w.attackers=0;
            w.typingResultsActive=false;
            chooser.active=false;
            w.TypingSound(0x003416A9u);
            return true;
        }
        w.TypingSound(0x001316A9u);
    }
    w.typingCharacters.clear();
    DrawChoice(w);
    return true;
}
}
