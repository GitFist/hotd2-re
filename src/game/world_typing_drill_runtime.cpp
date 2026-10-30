#include "game/world_internal.h"
#include "game/world_typing_drill_game.h"

#include <algorithm>

namespace game {
bool World::StartTypingDrill(int exercise,const TypingDrillController& profile,uint32_t seed,std::string* error,
                            const TypingRulesOptions* options) {
    struct CurrentGuard {Impl* prior=CurrentWorld();~CurrentGuard(){SetCurrentWorld(prior);}} currentGuard;
    if(exercise<0||exercise>=13) {
        if(error)*error="Invalid original Drill exercise";
        return false;
    }
    if(!impl_->typingReference.Available()) {
        if(error)*error="The prepared original Drill data is missing";
        return false;
    }
    static constexpr int scenes[]={25,9,21,5,17,19,11,15,13,1,7,23,3};
    WorldConfig boot;
    boot.savePath.clear();boot.applySaveSettings=false;
    Boot(boot);
    impl_->typingDrill.controller=profile;
    ConfigureTypingDrillGame(*impl_,exercise);
    WorldConfig config;
    config.eventMode=7;config.scene=scenes[exercise];config.gameMode=6;config.gameType=2;
    config.portTyping=true;config.typingOriginal=false;
    config.portFeatures=config.portStageResults=false;config.gameOver=false;
    config.freePlay=true;config.creditSetting=-1;config.livesSetting=5;
    config.playerState[0]=0;config.playerState[1]=9;config.seedCounter=seed;
    config.savePath.clear();config.applySaveSettings=false;
    if(options)config.typingOptions=*options;
    impl_->frontEnd=false;
    if(!StartStage(config,error))return false;
    impl_->hud.gameType=config.gameType;
    impl_->typing.setKeyObserver([world=impl_.get()](unsigned char expected,bool mistake) {
        TypingDrillGameTypedKey(*world,expected,mistake);
    });
    return true;
}
const TypingDrillGameState& World::TypingDrillGame() const {
    return impl_->typingDrill.game?impl_->typingDrill.game->TypingDrillGame():impl_->typingDrillGame;
}
TypingDrillResult World::TypingDrillOutcome() const {
    return impl_->typingDrill.game?impl_->typingDrill.game->TypingDrillOutcome():TypingDrillGameSnapshotResult(*impl_);
}
const std::array<uint16_t,256>& World::TypingDrillKeyWeights() const {
    return impl_->typingDrill.controller.originalKeyWeights();
}
bool World::PortTypingDrillPlaying() const {
    const auto& game=impl_->typingDrill.game;
    return game&&!game->impl_->paused&&TypingDrillGameRunning(*game->impl_);
}
void World::PauseTypingDrill() {
    auto& ui=impl_->typingDrill;
    if(!ui.game)return;
    const auto& game=*ui.game->impl_;
    if(game.paused||(game.gameMode==6&&game.subState==2&&!(game.flags5900&0x1C)&&game.player[0].state==5))
        ui.pauseRequest=true;
}
void World::ReturnToTypingDrillMenu() {
    auto& ui=impl_->typingDrill;
    ui.game.reset();ui.controller.abandon();ui.characters.clear();ui.nameUi={};ui.view=0;
    impl_->SoundStopAll();
}

void World::Impl::PresentTypingDrillGame() {
    if(gameMode!=4||front.nightShiftPage!=12||typingDrill.view!=1||!typingDrill.game)return;
    const auto& game=*typingDrill.game->impl_;
    draws=game.paused?typingDrill.heldDraws:game.draws;
    hudSprites=game.paused?typingDrill.heldSprites:game.hudSprites;
    if(game.paused) {
        SpriteDef pause;pause.id=0x8E7;pause.x=160;pause.y=240;pause.depth=0.6f;pause.flags=0x70A;
        hudSprites.push_back(pause);
    }
    cam=game.cam;view0=game.view0;inverse0=game.inverse0;camState=game.camState;
    env=game.env;lights=game.lights;light=game.light;
    texFiles=game.texFiles;polFileLoads=game.hud.polFileLoads;
    projCenterFrame[0]=game.projCenterFrame[0];projCenterFrame[1]=game.projCenterFrame[1];
    hudEffects=game.hud.effects;hudSubtitles=game.hud.subtitles;
}
} // namespace game
