#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "game/typing_report.h"
#include "game/typing_score_ui.h"
#include "game/world_internal.h"

namespace {
void Require(bool value,const char* message) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
}
int main(int argc,char** argv) {
    const char* bank=argc>=2 ? argv[1] : std::getenv("HOTD2_TYPING_REFERENCE");
    const char* gameDir=argc>=3 ? argv[2] : std::getenv("HOTD2_GAME_DIR");
    Require(bank && gameDir,"supply prepared reference bank and native game directory");
    game::World::Impl world;
    std::string error;
    Require(world.typingReference.Load(bank,&error),error.c_str());
    Require(world.exe.Load((std::filesystem::path(gameDir)/"Hod2.exe").string()),"load native scalar/asset tables");
    world.cfg.portTyping=world.typingReferenceActive=world.residency=true;
    world.cfg.typingOriginal=false; // Preserve coverage of the source's legacy report/name book.
    world.gameMode=7;
    world.runner=std::make_unique<game::EventRunner>(world.mem,world,&world.env,&world.exe);
    world.modelLoaded.assign(0x8000,0);
    world.typing.reset();
    world.cam.inverse=game::Mat4::RotateY(0x8000);
    game::SetCurrentWorld(&world);
    game::StartTypingReport(world,false);
    Require(world.typingResultsActive && world.subState==10 && world.typing.prompts().empty(),
            "source report owns the post-run flow without leaving typing targets active");
    auto frame=[&](uint32_t buttons=0) {
        world.draws.clear(); world.hudSprites.clear();
        world.pressed=buttons;
        game::TypingReportFrame(world);
    };
    for (int i=0;i<60;++i) frame(8);
    Require(world.typingResultsActive && world.gameMode==7,
            "confirmation during the source sixty-one-tick opening cannot dismiss the report");
    frame();
    for (int raw:{0x2A5,0x2A6,0x156F,0x156B}) {
        const int model=world.typingReference.SourceModel(raw);
        Require(std::any_of(world.draws.begin(),world.draws.end(),[&](const auto& draw) {
            return draw.model==model && draw.viewSpace && draw.fn==0x00497820 && draw.world.m[14]==-17;
        }),"report submits both original covers and pages at their authored view-space position");
    }
    auto hasAt=[&](float x,float y) {
        return std::any_of(world.hudSprites.begin(),world.hudSprites.end(),[&](const auto& sprite) {
            return sprite.source==0x004A19F0 && sprite.x==x && sprite.y==y && sprite.atlas;
        });
    };
    Require(hasAt(156,151) && hasAt(478,185) && hasAt(478,231),
            "report uses source skill grades plus Game Over and No Clear chapter art");
    Require(hasAt(39,80) && std::any_of(world.hudSprites.begin(),world.hudSprites.end(),[](const auto& sprite) {
        return sprite.id==0x4AC4 && sprite.x==106 && sprite.y==34;
    }),"report retains the source AC-mode caption and agent result heading");
    Require(world.hud.menuPanels.empty() && world.hud.menuText.empty(),
            "report does not substitute a modern text panel");
    const int graph=world.typingReference.SourceModel(0x156C);
    const auto graphDraw=std::find_if(world.draws.begin(),world.draws.end(),[&](const auto& draw) {
        return draw.model==graph && draw.fn==0x004A2A00;
    });
    Require(graphDraw!=world.draws.end() && graphDraw->edit && graphDraw->edit->vertices.size()==12 &&
            std::all_of(graphDraw->edit->vertices.begin(),graphDraw->edit->vertices.end(),[](const auto& vertex) {
                return std::isfinite(vertex.pos[0]) && std::isfinite(vertex.pos[1]);
            }),"five-axis source skill graph updates its twelve independent inline vertices");
    Require(graphDraw->light.intensity==1 && std::abs(graphDraw->light.yaw)==0x8000 &&
            graphDraw->light.pitch==0 && graphDraw->light.color[0]==1,
            "report headlight follows the view-space graph independently of the prior stage camera");
    frame(8);
    Require(game::TypingReportNameActive(world),"first qualifying skill score opens original typing name entry");
    game::QueueTypingReportText(world,"Abcdef"); frame();
    game::QueueTypingReportText(world,"\b"); frame();
    game::QueueTypingReportText(world,"Z"); frame();
    frame(8); // Return first selects the source's default No confirmation.
    Require(world.typing.finalRecords()[0].skillPoints==0,"name entry waits for explicit Yes before storing a record");
    frame(0x10); frame(8);
    Require(!game::TypingReportNameActive(world) &&
            std::string(world.typing.finalRecords()[0].name.data())=="AbcdZ",
            "source five-character name entry retains case, supports deletion and stores only after Yes");
    for (int i=0;i<60;++i) frame();
    Require(world.gameMode==7,"report retains the original closing-book animation after confirmation");
    for (int i=0;i<8;++i) frame();
    Require(world.gameMode==7 && game::TypingReportActive(world),
            "closing the result report proceeds to the original separate score-ranking book");
    for (int i=0;i<61;++i) frame();
    game::QueueTypingReportText(world,"\x1b"); frame();
    for (int i=0;i<62 && world.gameMode==7;++i) frame();
    Require(world.gameMode==3 && !world.typingResultsActive,"Back closes the score book and returns to title");
    world.typing.addBasePoints(6000);
    world.cfg.typingOriginal=true;
    game::TypingScoreUi scoreUi;
    game::StartTypingScoreUi(world,scoreUi,world.typing.finalEvaluation(0,0,false));
    Require(scoreUi.phase==game::TypingScoreUi::Profile,"a qualifying chapter score opens original profile registration");
    game::QueueTypingScoreUiText(scoreUi,"RankTesterXXmore\t2x4\tPLAYER\tPC parity\t");
    world.pressed=0; game::TypingScoreUiFrame(world,scoreUi);
    Require(scoreUi.fields[0]=="RankTesterXX" && scoreUi.fields[1]=="24" && scoreUi.field==4,
            "source profile enforces name12, numeric age2, occupation12 and comment45 limits with Tab navigation");
    world.pressed=8; game::TypingScoreUiFrame(world,scoreUi);
    Require(scoreUi.phase==game::TypingScoreUi::Opening && world.typing.scoreRecords(true)[0].name=="RankTesterXX" &&
            world.typing.scoreRecords(true)[0].occupation=="PLAYER" && world.typing.scoreRecords(true)[0].comment=="PC parity",
            "profile END stores all four fields in the distinct original score table");
    world.cfg.portNetworkRoom=true; world.cfg.networkStartedAt=1704067200u;
    game::StartTypingScoreUi(world,scoreUi,world.typing.finalEvaluation(0,0,false));
    Require(scoreUi.record.year==2024 && scoreUi.record.month==1 && scoreUi.record.day==1,
            "network scorebook date uses shared host UTC, independent of local clock and timezone");
    game::SetCurrentWorld(nullptr);
    std::puts("typing_report_test: OK");
}
