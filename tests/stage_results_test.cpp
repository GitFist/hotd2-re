#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "game/world_internal.h"

static void Require(bool ok, const char* why) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

static void CheckRescueScene(const char* gameDir, int stage, int gameType, bool rescued) {
    game::World::Impl w;
    w.gameDir = gameDir;
    Require(w.exe.Load((std::filesystem::path(gameDir) / "Hod2.exe").string()), "load rescue tables");
    w.runner = std::make_unique<game::EventRunner>(w.mem, w, &w.env, &w.exe);
    w.cfg.portStageResults = true;
    w.cfg.eventMode = stage;
    w.cfg.gameType = w.runner->cfg.gameType = gameType;
    w.gameMode = 6;
    w.subState = 2;
    const uint32_t list = w.exe.U32(0x0055DF50 + stage * 4);
    int count = 0;
    while (count < 10 && w.exe.S16(list + count * 20) != -1) {
        w.rescueKinds[stage][count] = w.exe.S16(list + count * 20);
        ++count;
    }
    Require(count > 0, "chapter has a rescue-scene cast");
    w.rescueCount[stage] = rescued ? count : 0;
    for (auto& p : w.player) { p.state = 5; p.lives = 1; }
    game::SetCurrentWorld(&w);
    w.ts.Create([](game::Task& t) {
        game::SpawnTask s;
        s.rec.type = 0x61;
        game::CurrentWorld()->SpawnStageClear(t, s);
    });
    w.ts.RunFrame();
    Require(w.stageResults.active && w.runner->flags[0xFE] == 0, "score page holds rescue scene open");
    const int bonus = w.stageResults.lifeBonus;
    for (int f = 0; f < 220; ++f) w.Mode6Frame();
    Require(w.player[0].lives == 1 && w.player[1].lives == 1, "score page does not award lives early");
    w.stageResults.dismiss = true;
    for (int f = 0; f < 420; ++f) {
        w.BeginFrame();
        w.Mode6Frame();
        int cast = 0;
        for (auto* task : w.ts.Live())
            if (task->data && static_cast<game::WTask*>(task->data.get())->fn == 0x00435760) ++cast;
        Require(cast == count, "rescued cast or zero-rescue fallback stays present");
        Require(w.runner->flags[0xFE] == (f == 419), "rescue scene finishes after its full animation");
    }
    for (const auto& p : w.player)
        Require(p.lives == std::min(5, 1 + bonus), "native life bonus is applied exactly once to each player");
    game::SetCurrentWorld(nullptr);
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    for (int stage = 0; stage < 5; ++stage)
        for (int gameType : {0, 1})
            for (bool rescued : {false, true}) CheckRescueScene(argv[1], stage, gameType, rescued);
    // Exercise the actual stage-end boundary for every chapter, including the ending and boss route.
    game::World::Impl w;
    Require(w.exe.Load((std::filesystem::path(argv[1]) / "Hod2.exe").string()), "load tables");
    w.runner = std::make_unique<game::EventRunner>(w.mem, w, &w.env, &w.exe);
    for (int stage = 0; stage < 6; ++stage) {
        w.cfg.portStageResults = true; w.cfg.eventMode = stage; w.cfg.gameType = 0;
        w.gameMode = 6; w.subState = 2; w.stageResults = {};
        w.player[0].state = w.player[1].state = 5;
        w.player[0].shots = 10; w.player[0].hits = 7;
        w.rescueCount[stage] = 3;
        w.StageEnd();
        Require(w.stageResults.active && w.stageResults.chapter == stage + 1, "all chapters open results");
        Require(w.stageResults.player[0].shots == 10 && w.stageResults.player[0].hits == 7, "snapshot stage accuracy");
        w.BeginStageResults();
        for (int f = 1; f < 180; ++f) {
            w.pressed = 0xC000C;
            w.Mode6Frame();
            Require(w.stageResults.active && !w.stageResults.dismiss && w.subState == 5, "early click/shot/Enter blocked");
        }
        // Holding the earlier button supplies no new edge when the minimum expires.
        w.pressed = 0;
        for (int f = 0; f < 60; ++f) w.Mode6Frame();
        Require(w.stageResults.active && !w.stageResults.dismiss, "no timeout or buffered early skip");
        w.pressed = stage % 2 ? 0x80000 : 4;
        w.Mode6Frame();
        Require(w.stageResults.dismiss, "P1 shot and P2 Enter can continue");
        w.Mode6Frame();
        Require(!w.stageResults.active && w.subState == 6, "dismiss resumes stage boundary");
        w.Mode6Frame();
        Require(w.subState == (stage == 5 ? 7 : 1), "next chapter or ending preserved");
        w.BeginStageResults();
        Require(!w.stageResults.active, "one results page per stage");
    }
    w.stageResults = {}; w.cfg.gameType = 3; w.cfg.eventMode = 0; w.subState = 2;
    w.StageEnd();
    Require(w.stageResults.active, "completed boss route has results");
    w.stageResults = {}; w.cfg.gameType = 2; w.cfg.eventMode = 6;
    w.StageEnd();
    Require(!w.stageResults.active, "training keeps its own results");

    // A real stage-1 clear shows the new totals, then the original rescued-civilian scene.
    struct Input { int frame, player; float x, y; int buttons; };
    std::vector<Input> inputs;
    std::ifstream file(argv[2]);
    std::string line;
    while (std::getline(file, line)) {
        Input i{}; std::istringstream row(line);
        if (row >> i.frame >> i.player >> i.x >> i.y >> i.buttons) inputs.push_back(i);
    }
    Require(!inputs.empty(), "load stage replay");
    game::World game;
    std::string error;
    Require(game.Load(argv[1], 0, &error), error.c_str());
    game::WorldConfig cfg;
    cfg.portStageResults = true; cfg.gameOver = true; cfg.creditSetting = 5;
    game.SetInput(0, game::MakeWorldInput(.5f, .5f, 0, 0));
    game.Boot(cfg);
    size_t pos = 0;
    float x[2] = {.5f, .5f}, y[2] = {.5f, .5f};
    uint8_t buttons[2] = {};
    while (!game.Hud().stageResults.active && game.Frame() < 20000) {
        uint8_t prev[2] = {buttons[0], buttons[1]};
        while (pos < inputs.size() && inputs[pos].frame <= game.Frame() + 1) {
            const auto& i = inputs[pos++]; x[i.player] = i.x; y[i.player] = i.y; buttons[i.player] = i.buttons;
        }
        game.SetInput(0, game::MakeWorldInput(x[0], y[0], buttons[0], prev[0]));
        game.Step();
    }
    const auto result = game.Hud().stageResults;
    printf("Replay state: mode %d sub %d scene %d player %d lives %d score %d\n", game.GameMode(), game.Hud().subState,
           game.Events().scene, game.Hud().player[0].state, game.Hud().player[0].lives, game.Hud().player[0].score);
    printf("Stage replay at frame %d: chapter %d shots %d hits %d headshots %d rescued %d bonus %d score %d\n",
           game.Frame(), result.chapter, result.player[0].shots, result.player[0].hits, result.player[0].headshots,
           result.civilians, result.lifeBonus, result.player[0].score);
    Require(result.active && result.chapter == 1, "real stage clear opens results");
    Require(result.civilians > 0, "replay rescued civilians");
    Require(result.player[0].participated && !result.player[1].participated, "solo player columns");
    Require(result.player[0].shots > 0 && result.player[0].hits > 0 && result.player[0].headshots > 0, "real combat totals");
    const int lives = game.Hud().player[0].lives;
    // Recovery rebuilds the results and counters from the same inputs, not from local menu time.
    game::World replay;
    Require(replay.Load(argv[1], 0, &error), error.c_str());
    replay.SetInput(0, game::MakeWorldInput(.5f, .5f, 0, 0));
    replay.Boot(cfg);
    size_t replayPos = 0;
    float rx = .5f, ry = .5f;
    uint8_t rb = 0;
    while (replay.Frame() < game.Frame()) {
        const uint8_t previous = rb;
        while (replayPos < inputs.size() && inputs[replayPos].frame <= replay.Frame() + 1) {
            const auto& i = inputs[replayPos++];
            if (i.player == 0) { rx = i.x; ry = i.y; rb = i.buttons; }
        }
        replay.SetInput(0, game::MakeWorldInput(rx, ry, rb, previous));
        replay.Step();
    }
    const auto& recovered = replay.Hud().stageResults;
    Require(recovered.active && recovered.player[0].headshots == result.player[0].headshots &&
            recovered.player[0].score == result.player[0].score && recovered.civilians == result.civilians &&
            recovered.frames == result.frames && replay.RandState() == game.RandState(), "replay restores results and totals");
    for (int f = 0; f < 220; ++f) { game.SetInput(0, {}); game.SetInput(1, {}); game.Step(); }
    Require(game.Hud().stageResults.active && game.Hud().player[0].lives == lives, "hold does not repeat life bonus");
    game::WorldInput confirm; confirm.pressed = confirm.held = 8;
    game.SetInput(1, confirm); game.Step();
    bool sawRescueScene = false, sawRescueDraws = false, sawLifeBonus = false;
    for (int f = 0; f < 600; ++f) {
        game.SetInput(0, {}); game.SetInput(1, {}); game.Step();
        int civilians = 0;
        for (const auto& task : game.Tasks()) if (task.fn == 0x00435760) ++civilians;
        if (civilians) {
            Require(!game.Hud().stageResults.active, "rescue scene follows the score page");
            Require(civilians == result.civilians, "all rescued civilians appear");
            sawRescueScene = true;
            for (const auto& draw : game.Draws()) if (draw.fn == 0x00435760) sawRescueDraws = true;
            const int expectedLives = std::min(5, lives + result.lifeBonus);
            Require(game.Hud().player[0].lives == lives || game.Hud().player[0].lives == expectedLives,
                    "life bonus is awarded once during the rescue scene");
            if (game.Hud().player[0].lives == expectedLives) sawLifeBonus = true;
        }
    }
    Require(sawRescueScene && sawRescueDraws, "rescued civilians are spawned and drawn after the score page");
    Require(sawLifeBonus, "rescue scene gives the life bonus");
    Require(game.Hud().titleChapter == 1 && !game.Hud().stageResults.active, "real next chapter loads after skip");
    Require(!game.Hud().stageResults.shown && game.Hud().stageResults.player[0].headshots == 0,
            "next stage starts fresh totals");
    for (int f = 0; f < 220; ++f) { replay.SetInput(0, {}); replay.SetInput(1, {}); replay.Step(); }
    replay.SetInput(1, confirm); replay.Step();
    for (int f = 0; f < 600; ++f) { replay.SetInput(0, {}); replay.SetInput(1, {}); replay.Step(); }
    Require(replay.Frame() == game.Frame() && replay.RandState() == game.RandState() &&
            replay.Hud().player[0].score == game.Hud().player[0].score && replay.Hud().titleChapter == game.Hud().titleChapter,
            "replay remains synchronized after guest confirmation and chapter load");
    puts("PASS stage results: all chapters, skip delay, both players, rescued civilians, awards and real next-stage load");
}
