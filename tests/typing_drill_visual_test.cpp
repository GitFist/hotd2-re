#include "game/world_internal.h"
#include "game/world_nightshift.h"
#include "game/camera.h"
#include "game/model.h"
#include "game/prs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace {
void Require(bool value,const std::string& message) {
    if(!value) {std::fprintf(stderr,"FAIL: %s\n",message.c_str());std::exit(1);}
}

void Frame(game::World::Impl& world,uint32_t buttons=0) {
    const bool typing=world.cfg.portTyping,reference=world.typingReferenceActive;
    ++world.frame;
    world.draws.clear();world.hudSprites.clear();world.polFileLoads.clear();
    world.hud.menuPanels.clear();world.hud.menuText.clear();
    world.pressed=buttons;
    game::NightShiftMenuFrame(world);
    world.pressed=0;
    Require(world.cfg.portTyping==typing && world.typingReferenceActive==reference,
            "Drill presentation restores the caller's typing and reference-data scopes");
}
void Frames(game::World::Impl& world,int count) {
    for(int i=0;i<count;++i)Frame(world);
}
bool HasSourceSprite(const game::World::Impl& world,int sprite) {
    return std::any_of(world.hudSprites.begin(),world.hudSprites.end(),[&](const auto& draw) {
        return draw.id==0x4000+sprite;
    });
}
bool AtlasAt(const game::World::Impl& world,float x,float y) {
    return std::any_of(world.hudSprites.begin(),world.hudSprites.end(),[&](const auto& draw) {
        return draw.id==0x4E29 && draw.atlas && std::abs(draw.x-x)<.01f && std::abs(draw.y-y)<.01f && draw.alpha>0;
    });
}

bool Finite(const game::Mat4& matrix) {
    return std::all_of(std::begin(matrix.m),std::end(matrix.m),[](float value){return std::isfinite(value);});
}

void OriginalResult(game::TypingDrillController& drill,int exercise,int32_t metric,int grade) {
    Require(drill.beginOriginalAttempt(exercise),"original score fixture follows exercise unlocks");
    game::TypingDrillResult result;
    result.original=true;result.metric=metric;
    drill.recordOriginalResult(exercise,result);
    Require(drill.finished() && drill.result().original && drill.result().metric==metric &&
            drill.result().grade==grade && drill.result().passed,
            "source raw-score fixture receives its expected original grade");
}

struct ReferenceEnvironment {
    bool hadValue=false;
    std::string saved;
    explicit ReferenceEnvironment(const char* bank) {
        const char* prior=std::getenv("HOTD2_TYPING_REFERENCE");
        hadValue=prior!=nullptr;saved=prior?prior:"";
#ifdef _WIN32
        _putenv_s("HOTD2_TYPING_REFERENCE",bank);
#else
        setenv("HOTD2_TYPING_REFERENCE",bank,1);
#endif
    }
    ~ReferenceEnvironment() {
#ifdef _WIN32
        _putenv_s("HOTD2_TYPING_REFERENCE",saved.c_str());
#else
        if(hadValue)setenv("HOTD2_TYPING_REFERENCE",saved.c_str(),1);
        else unsetenv("HOTD2_TYPING_REFERENCE");
#endif
    }
};

void OriginalOnly(const game::World::Impl& world,const char* page) {
    Require(world.hud.menuPanels.empty() && world.hud.menuText.empty(),
            std::string(page)+" uses the original presentation without a modern panel fallback");
    Require(std::all_of(world.draws.begin(),world.draws.end(),[](const auto& draw) {
        return draw.model>0 && Finite(draw.world);
    }),std::string(page)+" submits finite, valid geometry");
}

bool Book(const game::World::Impl& world) {
    return std::count_if(world.draws.begin(),world.draws.end(),[](const auto& draw) {
        return draw.fn==0x00497820 && draw.viewSpace;
    })>=4;
}

struct BookPaper {
    int model=0;
    std::vector<std::array<float,3>> vertices;
};
std::array<BookPaper,2> SourceBookPaper(const game::TypingReference& reference) {
    std::array<BookPaper,2> papers;
    for(int side=0;side<2;++side) {
        auto& paper=papers[side];
        paper.model=reference.SourceModel(side?0x1571:0x156E);
        const auto* resource=reference.Resource(unsigned(reference.ModelFile(paper.model)),0);
        Require(resource!=nullptr,"source result-paper meshes have a prepared model resource");
        auto bytes=resource->bytes;
        if(bytes.size()>=8) {
            uint32_t decoded=0;std::memcpy(&decoded,bytes.data(),4);
            if(decoded>0 && decoded<(64u<<20)) {
                std::vector<uint8_t> expanded(decoded);
                Require(game::DecompressPrsSafe(bytes.data()+4,bytes.size()-4,expanded.data(),expanded.size())==int(decoded),
                        "decode the actual source result-paper mesh payload");
                bytes=std::move(expanded);
            }
        }
        const auto entry=std::find(resource->models.begin(),resource->models.end(),paper.model);
        const auto headers=game::ReadPolHeader(bytes.data(),bytes.size());
        const size_t index=size_t(entry-resource->models.begin());
        Require(entry!=resource->models.end() && index<headers.size(),"prepared source paper identity matches its mesh entry");
        game::Model model;std::string error;
        Require(game::ParseModel(bytes.data(),bytes.size(),headers[index],model,&error),error);
        for(const auto& mesh:model.meshes)for(const auto& strip:mesh.strips)for(const auto& vertex:strip.vertices)
            paper.vertices.push_back({vertex.pos[0],vertex.pos[1],vertex.pos[2]});
        Require(paper.vertices.size()>=3,"source paper contains real triangle geometry");
    }
    return papers;
}
void VisibleBookPaper(const std::vector<game::WorldDraw>& draws,const game::WorldCamera& camera,
                      const std::array<BookPaper,2>& papers) {
    auto projection=game::DeviceProjection();
    projection.m[10]=8000.f/(8000.f-.5f);projection.m[14]=-.5f*projection.m[10];
    for(const auto& paper:papers) {
        const auto found=std::find_if(draws.begin(),draws.end(),[&](const auto& draw) {
            return draw.fn==0x00497820 && draw.model==paper.model && draw.viewSpace;
        });
        Require(found!=draws.end(),"settled source spread submits both actual paper models");
        // PC4978A7 opens the front mesh at0x8000, not0. The latter exposes
        // its culled back even though a draw-call-only test still succeeds.
        Require(std::abs(found->world.m[0]+1)<.00001f && std::abs(found->world.m[10]+1)<.00001f &&
                std::abs(found->world.m[14]+17)<.00001f,"settled paper faces the camera at the source32768 rotation");
        const auto effective=found->world*camera.inverse*camera.view;
        for(int i=0;i<16;++i)
            Require(std::abs(effective.m[i]-found->world.m[i])<.01f,
                    "final front-end camera preserves the book's authored model-view transform");
        const auto clip=effective*game::DeviceView()*projection;
        for(const auto& vertex:paper.vertices) {
            std::array<float,4> point{};
            for(int i=0;i<4;++i)point[i]=vertex[0]*clip.m[i]+vertex[1]*clip.m[4+i]+vertex[2]*clip.m[8+i]+clip.m[12+i];
            Require(std::all_of(point.begin(),point.end(),[](float value){return std::isfinite(value);}) && point[3]>.5f &&
                    std::abs(point[0])<=point[3]*1.02f && std::abs(point[1])<=point[3]*1.02f &&
                    point[2]>=0 && point[2]<=point[3],
                    "actual source paper vertices remain inside the public camera's visible clip volume");
        }
    }
}

std::set<int> SourceSprites(const game::World::Impl& world) {
    std::set<int> result;
    for(const auto& sprite:world.hudSprites)
        if(world.typingReference.SourceSprite(sprite.id))result.insert(sprite.id);
    return result;
}

void LiveStep(game::World& world,uint32_t buttons=0,int x=630,int y=465,bool fired=false) {
    game::WorldInput input;
    input.kind=1;input.x=x;input.y=y;input.pressed=input.held=buttons;input.fired=fired;
    world.SetInput(0,input);world.SetInput(1,{});world.Step();
}
void Click(game::World& world,int x,int y) {LiveStep(world,4,x,y,true);LiveStep(world);}

void FrontendLifecycle(const std::filesystem::path& native,const char* bank,const std::array<BookPaper,2>& papers) {
    // The menu creates a fresh World on entry, so retain this through the
    // complete lifecycle rather than only the outer World's Load call.
    ReferenceEnvironment environment(bank);
    game::World live;
    std::string error;
    const bool loaded=live.Load(native.string(),0,&error);
    Require(loaded,error);
    game::WorldConfig config;
    config.portFeatures=config.portModernMenu=true;
    live.Boot(config);live.ForceMode(4,0x10000,0,0);
    for(int i=0;i<3;++i)LiveStep(live);
    const auto nativeCamera=live.Camera();
    Click(live,480,150);
    for(int i=0;i<48;++i)LiveStep(live);
    Require(live.Hud().typingHome && live.Hud().nightShiftPage==0,"public frontend switches to Typing Home");
    Click(live,100,211);
    for(int i=0;i<12;++i)LiveStep(live);
    Require(live.Hud().nightShiftPage==12,"public frontend opens the original Drill carousel");
    LiveStep(live,8);LiveStep(live);
    Require(live.PortTypingDrillActive(),"public frontend starts Speed 1 through the menu task");
    for(int frame=0;frame<600&&!live.TypingDrillGame().supervisorSerial;++frame)LiveStep(live);
    Require(live.TypingDrillGame().supervisorSerial!=0 &&
            live.TypingDrillGame().phase==game::TypingDrillGamePhase::Instructions,
            "menu entry starts the native child World's source instruction supervisor");
    for(int frame=0;frame<3;++frame)LiveStep(live);
    Require(live.TypingDrillGame().phase==game::TypingDrillGamePhase::Instructions && !live.PortTypingDrillPlaying(),
            "source instructions wait for confirmation before gameplay opens");
    LiveStep(live,4);
    Require(live.TypingDrillGame().phase==game::TypingDrillGamePhase::Ready && live.TypingDrillGame().readyTicks==0,
            "instruction confirmation begins the source Ready/Go phase");
    for(int frame=0;frame<159;++frame)LiveStep(live);
    Require(live.TypingDrillGame().phase==game::TypingDrillGamePhase::Ready &&
            live.TypingDrillGame().readyTicks==159 && !live.PortTypingDrillPlaying(),
            "actor input remains closed through the first159 Ready/Go ticks");
    LiveStep(live);
    Require(live.TypingDrillGame().phase==game::TypingDrillGamePhase::Running &&
            live.TypingDrillGame().readyTicks==160 && live.PortTypingDrillPlaying(),
            "the160th Ready/Go tick opens the actual child World gameplay");
    for(int frame=0;frame<3;++frame) {
        LiveStep(live);
        for(int axis=0;axis<3;++axis) {
            Require(std::abs(live.Events().camera.ctx[0].pos[axis]-nativeCamera.pos[axis])<.002f,
                    "original frontend camera context remains isolated from the final drill render override");
        }
        Require(Finite(live.Camera().view) && std::any_of(live.Draws().begin(),live.Draws().end(),[](const auto& draw) {
            return draw.cull && !draw.viewSpace;
        }),"complete frontend frame retains authored arena geometry under its final source camera");
    }
    const int beforePause=live.TypingDrillGame().elapsedTicks;
    const auto beforePauseDraws=live.Draws().size();
    live.PauseTypingDrill();LiveStep(live);
    for(int frame=0;frame<20;++frame)LiveStep(live);
    Require(live.TypingDrillGame().elapsedTicks==beforePause &&
            !live.PortTypingDrillPlaying() &&
            live.Draws().size()==beforePauseDraws &&
            std::any_of(live.Hud().sprites.begin(),live.Hud().sprites.end(),[](const auto& sprite){return sprite.id==0x8E7;}),
            "pause retains the native arena and original timer with its Pause sprite");
    live.PauseTypingDrill();LiveStep(live);
    Require(live.TypingDrillGame().elapsedTicks==beforePause+301,
            "resuming applies the PC five-second pause penalty and one live tick");
    LiveStep(live,2);LiveStep(live);
    Require(live.PortTypingDrillActive()&&live.PortTypingDrillPlaying(),
            "Escape cancels a word while the source exercise continues");
    live.ReturnToTypingDrillMenu();LiveStep(live); // Explicit Alt+F3 -> source selection.
    Require(!live.PortTypingDrillActive() && live.Hud().nightShiftPage==12,"public Back abandons practice to Drill selection");
    for(int tick=0;tick<12;++tick)LiveStep(live); // Complete the carousel's initial ten-tick lock.
    // Drive an actual child World through Finish into the parent result book.
    // The fresh carousel's fourth entry is Special1, which ends naturally on
    // the first axe hit and needs no injected result or extra loaded fixture.
    for(int item=0;item<3;++item){LiveStep(live,0x80);for(int tick=0;tick<12;++tick)LiveStep(live);}
    LiveStep(live,8);LiveStep(live);
    for(int tick=0;tick<600 && !live.TypingDrillGame().supervisorSerial;++tick)LiveStep(live);
    Require(live.TypingDrillGame().exercise==9 && live.TypingDrillGame().supervisorSerial!=0 &&
            live.TypingDrillGame().phase==game::TypingDrillGamePhase::Instructions,
            "public carousel starts source Special1 for a real result-book transition");
    LiveStep(live,4);
    Require(live.TypingDrillGame().phase==game::TypingDrillGamePhase::Ready,
            "source Special1 supervisor receives confirmation after its authored instruction task spawns");
    bool reachedBook=false;
    for(int tick=0;tick<2400 && !reachedBook;++tick) {
        LiveStep(live);
        reachedBook=std::any_of(live.Draws().begin(),live.Draws().end(),[](const auto& draw){return draw.fn==0x00497820;});
    }
    Require(reachedBook && !live.PortTypingDrillActive(),"finished child World yields its source result book to the public front end");
    for(int tick=0;tick<61;++tick)LiveStep(live);
    VisibleBookPaper(live.Draws(),live.Camera(),papers);
    Require(live.Lights().valid && live.Lights().fogStart==131070.f && live.Lights().fogEnd==131072.f,
            "source book transition resets both fog limits so carried arena fog cannot black out the paper");
    LiveStep(live,8);for(int tick=0;tick<60;++tick)LiveStep(live); // Statistics -> ranking.
    LiveStep(live,8);for(int tick=0;tick<61;++tick)LiveStep(live); // Ranking -> supplementary question.
    Require(std::any_of(live.Hud().sprites.begin(),live.Hud().sprites.end(),[](const auto& sprite){return sprite.id==0x4BC5;}),
            "unranked zero-hit course reaches the original supplementary question after its book closes");
    LiveStep(live,8);LiveStep(live); // Default No -> source selection.
    LiveStep(live,2);LiveStep(live); // Source selection -> Typing Home.
    Require(live.Hud().typingHome && live.Hud().nightShiftPage==0,"public Back leaves Drill for Typing Home");
    Click(live,480,150);
    for(int i=0;i<48;++i)LiveStep(live);
    Require(!live.Hud().typingHome && live.Hud().nightShiftPage==0,"public frontend returns to the native game menu");
    for(int axis=0;axis<3;++axis)
        Require(std::abs(live.Camera().pos[axis]-nativeCamera.pos[axis])<.002f,
                "leaving Drill releases the source render camera and restores the native menu camera");
}
}

int main(int argc,char** argv) {
    Require(argc==3,"usage: typing_drill_visual_test <native-game-directory-or-exe> <prepared-reference-bank>");
    std::filesystem::path native(argv[1]);
    if(native.has_extension())native=native.parent_path();
    game::World::Impl world;
    std::string error;
    world.gameDir=native.string();
    Require(world.exe.Load((native/"Hod2.exe").string()),"load native rendering tables");
    Require(world.typingReference.Load(argv[2],&error,false),error);
    Require(world.typingReference.Complete(),"prepared source data has no unresolved conversion fields");
    const auto papers=SourceBookPaper(world.typingReference);
    Require(world.enemyTables.Load(world.exe),"load native actor tables");
    world.motionFiles=game::ListMotionFiles(world.exe);
    world.runner=std::make_unique<game::EventRunner>(world.mem,world,&world.env,&world.exe);
    world.modelLoaded.assign(0x8000,0);
    world.residency=true;
    world.gameMode=4;
    world.cfg.portFeatures=world.cfg.portModernMenu=true;
    world.cfg.portTyping=world.typingReferenceActive=false;
    world.front.typingHome=true;world.front.nightShiftPage=12;
    world.env.ResetBlocks();world.LightScene();
    game::SetCurrentWorld(&world);

    Frame(world);
    OriginalOnly(world,"Drill selection");
    const auto firstSelection=SourceSprites(world);
    Require(firstSelection.size()>20 && std::any_of(world.draws.begin(),world.draws.end(),[](const auto& draw) {
        return draw.fn==0x00409560 && draw.node>=0;
    }),"selection combines original source carousel art with its animated 3D agent");
    for(int i=0;i<12;++i)Frame(world);
    Frame(world,0x20);
    for(int i=0;i<12;++i)Frame(world);
    Require(SourceSprites(world)!=firstSelection,"selection changes source artwork when the selected exercise changes");

    auto& drill=world.typingDrill.controller;
    constexpr std::array<int32_t,13> sourceAMetrics={3600,35,12000,100,45,7200,40,2700,45,128,70,36,16800};
    for(int exercise=0;exercise<12;++exercise)OriginalResult(drill,exercise,sourceAMetrics[size_t(exercise)],0);
    Require(drill.unlocked(12),"original grade fixtures unlock the final exam");
    OriginalResult(drill,0,5400,1); // Original A/B history supplies a connecting segment.

    // Retain the standalone draw-template coverage. Actual task ownership and
    // instruction/Ready timing are checked by FrontendLifecycle below.
    for(int exercise=0;exercise<13;++exercise) {
        Require(drill.start(exercise,0x54595045u+uint32_t(exercise)),"start an unlocked exercise for visual integration");
        world.typingDrill.view=1;world.typingDrill.visualView=-1;
        for(int frame=0;frame<4;++frame)Frame(world);
        OriginalOnly(world,"Drill exercise");
        Require(Finite(world.cam.view) && Finite(world.cam.inverse) &&
                std::any_of(world.draws.begin(),world.draws.end(),[](const auto& draw) {
                    return draw.cull && !draw.viewSpace;
                }),"each exercise submits its authored arena under a finite original camera");
        Require(std::any_of(world.draws.begin(),world.draws.end(),[](const auto& draw) {
            return !draw.cull && (draw.node>=0 || !draw.viewSpace);
        }),"each exercise renders authored actor or prop geometry in addition to the arena");
        Require(std::any_of(world.draws.begin(),world.draws.end(),[](const auto& draw) {
            return draw.viewSpace && draw.fn==0x004AF870;
        }),"each exercise renders the prepared 3D word poster and glyphs");
        Require(world.ts.Live().empty() && std::all_of(std::begin(world.actorSlots),std::end(world.actorSlots),[](const auto* actor) {
            return actor==nullptr;
        }),"draw-only exercise actors do not join the attract task graph or occupy gameplay slots");
        const unsigned words=drill.result().words;
        const std::string prompt(drill.prompt());
        Require(game::QueueTypingDrillText(world,prompt,false),"exercise poster retains the shared physical typing route");
        Frame(world);
        Require(drill.result().words==words+1,"typing the displayed prompt advances the native controller");
        OriginalOnly(world,"Drill exercise after typing");
    }

    Require(drill.beginOriginalAttempt(12),"source Final result fixture follows the real unlock predicate");
    game::TypingDrillResult finalResult;
    finalResult.metric=sourceAMetrics[12];finalResult.assignedWords=123;finalResult.completedWords=120;
    finalResult.correct=95;finalResult.errors=5;finalResult.reactionFrames=243;finalResult.reactions=4;
    finalResult.speedFrames=720;finalResult.speedCharacters=60;
    drill.recordOriginalResult(12,finalResult);
    Require(drill.result().original && drill.result().grade==0 && drill.recordRank()>=0,
            "ranked source A fixture requires original name entry");
    auto& ui=world.typingDrill;
    auto& book=ui.bookUi;
    ui.view=2;game::BeginTypingDrillResults(world);
    Frame(world,8);Frames(world,58);
    Require(ui.view==2 && book.phase==0 && book.ticks==60 && !ui.nameUi.active,
            "confirmation is ignored while the original result book opens through counter60");
    // Results enter with counter1; records below enter with counter0. Both
    // advance only after the source counter passes60, not on its60th value.
    Frames(world,2);
    Require(ui.view==2 && book.phase==1 && book.ticks==0 && std::abs(book.position-1)<.001f && Book(world),
            "the initial61-tick result window settles on its statistics spread");
    OriginalOnly(world,"Drill statistics spread");
    VisibleBookPaper(world.draws,world.cam,papers);
    Require(HasSourceSprite(world,0xAC3) && HasSourceSprite(world,0xE2D) &&
            AtlasAt(world,203,331) && AtlasAt(world,203,353) && AtlasAt(world,203,375) && AtlasAt(world,203,397) &&
            AtlasAt(world,565,162) && AtlasAt(world,565,184),
            "statistics spread has the source agent heading, grade highlight, numeric criteria and word counters");
    Require(std::none_of(world.hudSprites.begin(),world.hudSprites.end(),[](const auto& sprite) {
        return sprite.source==0x004A2B60;
    }),"grade history belongs to the later ranking spread");
    Require(!AtlasAt(world,40,105) && !AtlasAt(world,40,439),
            "original statistics book does not contain the native outcome caption or action footer");

    Frame(world,8);Frames(world,58);
    Require(ui.view==2 && book.phase==1 && book.ticks==59 && !ui.nameUi.active,
            "first confirmation turns the statistics page without opening name entry early");
    Frame(world);
    Require(book.phase==1 && book.ticks==60 && ui.view==2,"result page turn retains counter60");
    Frame(world);
    Require(book.phase==2 && book.ticks==0 && std::abs(book.position-2)<.001f && ui.view==2,
            "sixty elapsed turn ticks reveal the source ranking spread");
    Frame(world);
    Require(ui.view==4 && game::TypingDrillNameActive(world) && game::TypingDrillInputActive(world),
            "ranked attempts enter name editing only after the ranking spread opens");
    Frame(world);
    Require(Book(world) && !HasSourceSprite(world,0xBC6) && !HasSourceSprite(world,0xBCA),
            "original name editing occurs directly on the ranking book without an editing modal");
    Require(game::QueueTypingDrillText(world,"A1B2CX",false),"ranking name uses the shared text route");
    Frame(world);
    Require(ui.nameUi.name=="A1B2C" && drill.records(12)[drill.recordRank()].name[0]==0,
            "name editing enforces five characters and leaves the ranked record pending confirmation");
    Require(game::QueueTypingDrillText(world,"",true),"Backspace routes to the original name editor");
    Frame(world);
    Require(ui.nameUi.name=="A1B2","Backspace removes exactly the final entered character");
    game::QueueTypingDrillText(world,"D",false);Frame(world);
    Require(ui.nameUi.name=="A1B2D","name editing retains the first four characters after replacement");
    Frame(world,8);
    Require(ui.nameUi.confirmation==2 && HasSourceSprite(world,0xBCA) && HasSourceSprite(world,0xBC6),
            "Enter opens the original name confirmation with No selected");
    Frame(world,8);
    Require(ui.view==4 && ui.nameUi.confirmation==0 && ui.nameUi.name=="A1B2D" && !HasSourceSprite(world,0xBC6),
            "confirming No restores the unchanged in-book edit buffer");
    Frame(world,8);Frame(world,0x10);
    Require(ui.nameUi.confirmation==1,"Up selects Yes in the original name confirmation");
    Frame(world,8);
    Require(ui.view==2 && book.nameDone && !game::TypingDrillInputActive(world) &&
            std::string(drill.result().name.data())=="A1B2D" &&
            std::string(drill.records(12)[drill.recordRank()].name.data())=="A1B2D" && AtlasAt(world,512,326),
            "confirmed Yes stores the actual ranked name and returns input to the book");
    OriginalOnly(world,"Drill ranking spread after naming");
    Frame(world,8);Frames(world,59);
    Require(ui.view==2 && book.phase==2 && book.ticks==60 && !HasSourceSprite(world,0xBC5),
            "ranking confirmation closes the book before the supplementary question");
    Frame(world);Frame(world);
    Require(ui.view==2 && book.phase==4 && book.confirmation==2 && HasSourceSprite(world,0xBC5) &&
            HasSourceSprite(world,0xBC6) && !HasSourceSprite(world,0xBCA),
            "closed results lead to a separate supplementary question with No selected");
    Frame(world,8);
    Require(ui.view==0 && !game::TypingDrillInputActive(world),"default No returns from supplementary confirmation to Drill selection");

    ui.view=3;game::BeginTypingDrillRecords(world);
    Frame(world,0x80);Frames(world,59);
    Require(book.phase==0 && book.ticks==60 && book.recordTurn==0 && ui.record==0,
            "records ignore navigation throughout the60-counter opening");
    Frame(world);
    Require(book.phase==1 && book.ticks==0 && std::abs(book.position-1)<.001f,
            "records become interactive on opening tick61");
    OriginalOnly(world,"Drill records");
    Require(Book(world) && std::count_if(world.hudSprites.begin(),world.hudSprites.end(),[](const auto& sprite) {
        return sprite.source==0x004A2B60;
    })>=2 && std::any_of(world.hudSprites.begin(),world.hudSprites.end(),[](const auto& sprite) {
        return sprite.source==0x004A2CB0;
    }),"record book renders history points and a connecting segment from original A/B score fixtures");
    // Source5DD908 entries156/157 put the attempt digit at(142,150)
    // and its ordinal suffix one pixel higher, at(154,149).
    Require(AtlasAt(world,142,150) && AtlasAt(world,154,149),
            "records use the original attempt digit and raised ordinal-suffix anchors");
    Require(AtlasAt(world,105,327) && !AtlasAt(world,211,326),
            "records retain the original metric anchor and blank names for unnamed rows");
    Frame(world,0x10);Frame(world,0x20);
    Require(ui.record==0 && book.recordTurn==0,"Up and Down leave the original record spread unchanged");
    Frame(world,0x80);Frames(world,13);Frame(world,0x40);Frames(world,13);
    Require(book.recordTurn==1 && book.ticks==28 && ui.record==0,
            "Right begins a turn and opposite navigation cannot interrupt it");
    Frame(world);
    Require(book.ticks==29 && ui.record==0,"records retain the old two-page spread before tick30");
    Frame(world);
    Require(book.ticks==30 && book.recordPage==2 && ui.record==2,
            "record turn switches from source pages0/1 to2/3 at tick30");
    Frames(world,29);Frame(world,0x80);
    Require(book.recordTurn==0 && book.ticks==0 && ui.record==2 && std::abs(book.position-1)<.001f,
            "record turn ends at tick60 and ignores another navigation press on that final tick");
    Frame(world,0x10);Frame(world,0x20);
    Require(ui.record==2 && book.recordTurn==0,"Up and Down remain inactive after a turn");
    Frame(world,8);Frames(world,29);
    Require(book.ticks==30 && book.recordPage==4 && ui.record==7,
            "Enter turns forward by two source exercises across the native category mapping");
    Frames(world,30);Frame(world,0x40);Frames(world,29);
    Require(book.ticks==-30 && book.recordPage==2 && ui.record==2,
            "Left reverses by two source exercises at the reverse turn midpoint");
    Frames(world,30);
    Require(book.recordTurn==0 && ui.record==2,"reverse page turn settles after60 ticks");
    world.cfg.portTyping=world.typingReferenceActive=true;
    Frame(world);
    Require(Book(world),"already active typing scope also retains the source record book");
    Frame(world,2);Frames(world,60);
    Require(ui.view==3 && book.phase==2 && book.ticks==60,"record exit retains the closing book through counter60");
    Frame(world);
    Require(ui.view==0,"records return to Drill selection after the closing counter passes60");

    game::SetCurrentWorld(nullptr);
    FrontendLifecycle(native,argv[2],papers);
    std::puts("PASS: source Drill carousel, thirteen draw templates, animated books/name/record controls, and native child World instruction/Ready/render lifecycle");
}
