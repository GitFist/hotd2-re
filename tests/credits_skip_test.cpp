#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "game/world_modes.h"
#include "game/world_config_io.h"

static void Require(bool ok, const char* why) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

static void Setup(game::World::Impl& w, const char* dir, int type, int player, bool room, int score) {
    w.gameDir = dir;
    Require(w.exe.Load((std::filesystem::path(dir) / "Hod2.exe").string()), "load ending tables");
    w.runner = std::make_unique<game::EventRunner>(w.mem, w, &w.env, &w.exe);
    w.modes = std::make_unique<game::ModesState>();
    for (auto& table : w.modes->tables) for (auto& row : table) row.score = 10000;
    w.cfg.portFeatures = true;
    w.cfg.portNetworkRoom = room;
    w.cfg.eventMode = 9;
    w.cfg.gameType = w.runner->cfg.gameType = type;
    w.front.gameType = type;
    w.frontEnd = true;
    w.gameMode = 6; w.subState = 8; w.flags5900 = 0x10;
    w.players = room ? 2 : 1; w.playersInGame = player;
    for (int p = 0; p < 2; ++p) {
        w.player[p].state = room || p == player ? 7 : 9;
        w.player[p].score = score;
    }
    w.projCenter[0] = 160; // a staff page has moved the camera projection
    w.projCenter[1] = 240;
}

static void Tick(game::World::Impl& w, int player, uint32_t pressed, uint32_t held = 0) {
    w.input[0] = w.input[1] = {};
    w.input[player].pressed = pressed; w.input[player].held = held | pressed;
    w.inputEdge[0] = w.inputEdge[1] = true;
    w.BeginFrame();
    w.InputFrame();
    game::SetCurrentWorld(&w);
    w.EndingFrame();
    w.FrontApplyMode();
    game::SetCurrentWorld(nullptr);
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) return 2;
    for (int type : {0, 1}) for (int player : {0, 1}) for (bool room : {false, true})
        for (uint32_t button : {4u, 8u}) for (int score : {0, 73000}) {
            game::World::Impl w;
            Setup(w, argv[1], type, player, room, score);
            Tick(w, player, 0, button);
            Require(w.gameMode == 6 && w.subState == 8, "held input does not skip credits");
            Tick(w, player, 2); // reload / Back is not the skip action
            Require(w.subState == 8, "unrelated input does not skip credits");
            Tick(w, player, button);
            Require(w.projCenter[0] == 320 && w.projCenter[1] == 240, "restore centered projection");
            Require(w.player[player].score == score, "skip preserves final score");
            if (room) Require(w.gameMode == 15 && w.cfg.portNetworkRoom, "either peer returns both players to room");
            else if (score == 0) Require(w.gameMode == 3, "nonqualifying score returns to title");
            else {
                Require(w.subState == 9, "qualifying score still reaches name entry");
                Tick(w, player, 0);
                Require(w.subState == 10, "name entry starts successfully");
                const auto& rank = w.modes->tables[type][0];
                Require(rank.score == score && rank.b4 == 1 && rank.b5 == player,
                        "ranking retains correct score class and player when results animation was skipped");
            }
        }
    game::World::Impl original;
    Setup(original, argv[1], 0, 0, false, 0);
    original.cfg.portFeatures = false;
    for (uint32_t button : {4u, 8u, 0xCu}) Tick(original, 0, button);
    Require(original.gameMode == 6 && original.subState == 8, "original exactness mode remains unskippable");
    // Rejoining peers replay the same inputs, including a guest's skip, to the same lobby state.
    game::World::Impl a, b;
    Setup(a, argv[1], 0, 0, true, 73000); Setup(b, argv[1], 0, 0, true, 73000);
    for (int frame = 0; frame < 121; ++frame) {
        for (auto* w : {&a, &b}) Tick(*w, 1, frame == 120 ? 4 : 0);
        Require(a.gameMode == b.gameMode && a.subState == b.subState && a.rng.state == b.rng.state
                && a.ts.NextSerial() == b.ts.NextSerial() && a.front.modeFrames == b.front.modeFrames,
                "credits skip and replay remain deterministic");
    }
    Require(a.gameMode == 15 && b.gameMode == 15, "both replayed peers return to lobby");
    puts("PASS credits skip: Arcade/Original, either player, click/trigger/Enter, held inputs, rankings, lobby and replay");
    if (argc == 4) {
        // Run the recorded final chapter through its real transition into the credits.
        game::World world;
        game::WorldConfig cfg;
        std::string error;
        Require(game::LoadWorldConfig(argv[2], &cfg, &error), error.c_str());
        cfg.portFeatures = true;
        Require(world.Load(argv[1], cfg.eventMode, &error), error.c_str());
        Require(world.StartStage(cfg, &error), error.c_str());
        struct Input { int frame, player; float x, y; unsigned buttons; };
        std::vector<Input> inputs;
        std::ifstream file(argv[3]); std::string line;
        while (std::getline(file, line)) {
            Input in{}; std::istringstream row(line);
            if (row >> in.frame >> in.player >> in.x >> in.y >> in.buttons) inputs.push_back(in);
        }
        Require(!inputs.empty(), "load final chapter replay");
        size_t pos = 0; float x[2] = {.5f,.5f}, y[2] = {.5f,.5f}; uint8_t buttons[2] = {};
        while (world.Hud().subState != 8 && world.Frame() < 105000) {
            uint8_t prev[2] = {buttons[0],buttons[1]};
            while (pos < inputs.size() && inputs[pos].frame <= world.Frame() + 1) {
                auto in = inputs[pos++]; x[in.player] = in.x; y[in.player] = in.y;
                buttons[in.player] = static_cast<uint8_t>(in.buttons);
            }
            for (int p = 0; p < 2; ++p) world.SetInput(p, game::MakeWorldInput(x[p],y[p],buttons[p],prev[p]));
            world.Step();
        }
        Require(world.Hud().subState == 8, "final boss flows into real credits");
        for (int f = 0; f < 180; ++f) {
            world.SetInput(0, {}); world.SetInput(1, {}); world.Step();
        }
        Require(world.Hud().subState == 8, "credits continue without input");
        const int score = world.Hud().player[0].score;
        world.SetInput(0, game::MakeWorldInput(.5f,.5f,1,0)); world.Step();
        Require(world.Hud().subState == 9, "shot skips real credits into qualified name entry");
        world.SetInput(0, {}); world.Step();
        Require(world.Hud().subState == 10 && world.Hud().player[0].score == score,
                "real name entry keeps the final score");
        printf("PASS final-chapter replay skipped credits at frame %d, score %d\n",world.Frame(),score);
    }
}
