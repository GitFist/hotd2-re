#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "game/world_internal.h"
#include "game/world_modes.h"
#include "port/play/native_feedback.h"

namespace {
void Check(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
void Routing() {
    game::WorldFeedback events; events.shot[0] = true; events.damage[1] = true;
    game::WorldHud hud; hud.gameMode = 6;
    play::FeedbackContext context;
    auto output = play::RouteFeedback(events, hud, context);
    Check(output.shot[0] && output.damage[1] && !output.damage[0], "local players stay separate");
    context.online = true; context.onlinePlayer = 1;
    output = play::RouteFeedback(events, hud, context);
    Check(output.damage[0] && !output.shot[0] && !output.damage[1], "online P2 maps only its own events to physical P1");
    context.onlinePlayer = 0;
    output = play::RouteFeedback(events, hud, context);
    Check(output.shot[0] && !output.damage[0] && !output.damage[1], "remote damage cannot shake host's gun");
    for (int guard = 0; guard < 10; ++guard) {
        context = {}; hud = {}; hud.gameMode = 6;
        switch (guard) {
        case 0: context.active = false; break;
        case 1: context.headless = true; break;
        case 2: context.menu = true; break;
        case 3: context.replay = true; break;
        case 4: context.stalled = true; break;
        case 5: context.desynced = true; break;
        case 6: hud.paused = true; break;
        case 7: hud.stageResults.active = true; break;
        case 8: hud.gameMode = 5; break;
        case 9: hud.gameMode = 3; break;
        }
        output = play::RouteFeedback(events, hud, context);
        Check(!output.shot[0] && !output.damage[1], "noninteractive/replay frames produce no hardware events");
    }
    play::FeedbackRumble rumble;
    rumble.Damage(1); rumble.Shot(1.02);
    auto motors = rumble.Output(1.03, 50);
    Check(motors.low == 25000 && motors.high == 22500, "shot mixes with damage at selected strength");
    motors = rumble.Output(1.08, 50);
    Check(motors.low == 25000 && motors.high == 12500, "damage continues after shorter recoil pulse");
    Check(rumble.Output(1.2, 100).milliseconds == 0, "effects expire by wall clock");
    rumble.Shot(2); Check(rumble.Output(2, 0).high == 0, "off never vibrates");
    rumble.Stop(); Check(rumble.Output(2, 100).milliseconds == 0, "stop discards remaining pulses");
}
void AcceptedEvents(const char* dir) {
    game::World::Impl w;
    Check(w.exe.Load((std::filesystem::path(dir) / "Hod2.exe").string()), "load native tables");
    w.runner = std::make_unique<game::EventRunner>(w.mem,w,&w.env,&w.exe);
    w.runner->camera.modeCur = w.runner->camera.modeReq = 2;
    w.gameMode = 6; w.canShoot = 1;
    game::Task task; task.data = std::make_unique<game::PlayerTask>();
    task.As<game::PlayerTask>().index = 1;
    auto& p = w.player[1]; p.state = 5; p.lives = 3; p.ammo = 6;
    p.gun.fired = p.gun.valid = 1;
    w.PlayerTrigger(task);
    Check(w.feedback.shot[1] && !w.feedback.shot[0] && p.ammo == 5, "accepted P2 shot produces one P2 event");
    w.feedback = {}; p.ammo = 0;
    w.PlayerTrigger(task);
    Check(!w.feedback.shot[1], "dry fire produces no recoil event");
    p.ammo = 6; p.gun.valid = 0; p.inputKind = 1;
    w.PlayerTrigger(task);
    Check(!w.feedback.shot[1], "offscreen reload produces no recoil event");
    p.gun.valid = 1; w.canShoot = 0;
    w.PlayerTrigger(task);
    Check(!w.feedback.shot[1], "blocked shot produces no recoil event");
    w.PlayerTakeHit(1,0,0);
    Check(w.feedback.damage[1] && !w.feedback.damage[0] && p.lives == 2, "accepted P2 damage produces one P2 event");
    w.feedback = {};
    w.PlayerTakeHit(1,0,0);
    Check(!w.feedback.damage[1] && p.lives == 2, "invulnerability suppresses repeated damage feedback");
    p.hitTimer = 0; w.gameMode = 5;
    w.PlayerTakeHit(1,0,0);
    Check(!w.feedback.damage[1], "attract demo produces no damage event");
    w.gameMode=6; w.canShoot=1; w.modes=std::make_unique<game::ModesState>();
    auto& weapon=w.modes->pitems[1]; weapon.b8[0]=uint8_t(-1);
    p.ammo=6; p.gun.fired=p.gun.valid=1;
    game::SpecialTrigger(w,task);
    Check(w.feedback.shot[1] && p.ammo==6,"infinite-ammo special weapon produces recoil without ammo decrement");
    w.feedback={}; p.gun.fired=0; weapon.b10[1]=1; weapon.b10[3]=0;
    game::SpecialTrigger(w,task);
    Check(w.feedback.shot[1],"accepted automatic follow-up shot produces recoil without trigger edge");
    w.feedback={}; weapon.b10[3]=5;
    game::SpecialTrigger(w,task);
    Check(!w.feedback.shot[1],"special weapon cooldown produces no recoil");
    game::World world;
    std::string error;
    Check(world.Load(dir,0,&error),error.c_str());
    game::WorldConfig cfg; cfg.gameMode=6; cfg.scene=0; cfg.playerState[0]=0; cfg.playerState[1]=9;
    Check(world.StartStage(cfg,&error),error.c_str());
    bool fired = false;
    for(int frame=0;frame<3000;++frame) {
        game::WorldInput input; input.kind=1; input.x=320; input.y=240;
        if(world.Hud().canShoot && world.Hud().player[0].state==5) input.fired=true;
        world.SetInput(0,input); world.Step();
        if(world.Feedback().shot[0]) { fired=true; break; }
    }
    Check(fired,"live stage emits accepted gun event");
    world.SetInput(0,{});world.Step();
    Check(!world.Feedback().shot[0],"next public Step clears the previous shot event");
}
}
int main(int argc,char** argv) {
    Routing();
    if(argc>1) AcceptedEvents(argv[1]);
    std::puts("native_feedback_test: PASS");
}
