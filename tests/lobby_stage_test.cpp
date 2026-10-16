// lobby_stage_test <game dir>
// A lobby start must replace the title's section table with the selected stage's geometry.
// Synchronization alone cannot catch this: both peers previously drew the same empty street.
#include <cstdio>
#include <vector>

#include "game/world.h"

static std::vector<int> StageModels(const game::World& world) {
    std::vector<int> models;
    for (const auto& draw : world.Draws())
        if (draw.fn == 0x00401260) models.push_back(draw.model);  // Stage_DrawSection
    return models;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: lobby_stage_test <game dir>\n");
        return 2;
    }
    game::World lobby;
    std::string error;
    if (!lobby.Load(argv[1], 0, &error)) {
        fprintf(stderr, "load: %s\n", error.c_str());
        return 2;
    }
    game::WorldConfig boot;
    boot.portFeatures = true;
    lobby.Boot(boot);
    while (lobby.Frame() < 650) lobby.Step();
    if (lobby.GameMode() != 3 || StageModels(lobby).empty()) {
        fprintf(stderr, "test setup did not reach the title's scenery\n");
        return 1;
    }
    // Include a second game after returning to the title; change the game type so the selected
    // section tables must come from the requested config rather than the previous menu state.
    for (int gameType : {0, 1}) {
        game::World fresh;
        // On the second run preload another chapter as well: starting stage 1 must restore its event data.
        if (!fresh.Load(argv[1], gameType == 1 ? 1 : 0, &error)) return 2;
        lobby.ForceMode(15, 100, 1, 0);
        lobby.Step();
        if (lobby.GameMode() != 15) return 1;
        game::WorldConfig start;
        start.gameMode = 6;
        start.eventMode = 0;
        start.scene = 0;
        start.gameType = gameType;
        start.startFrame = 901;
        start.seedCounter = 902;
        start.playerState[0] = start.playerState[1] = 0;
        if (!fresh.StartStage(start, &error) || !lobby.StartStage(start, &error)) {
            fprintf(stderr, "stage start: %s\n", error.c_str());
            return 1;
        }
        int sceneryFrames = 0;
        for (; fresh.Frame() <= 3000; fresh.Step(), lobby.Step()) {
            if (fresh.Frame() > start.startFrame && lobby.Hud().gameType != gameType) {
                fprintf(stderr, "FAIL: menu game type leaked into the new stage\n");
                return 1;
            }
            const auto expected = StageModels(fresh);
            const auto actual = StageModels(lobby);
            if (actual != expected) {
                fprintf(stderr, "FAIL game type %d frame %d: fresh stage has %zu scenery models, lobby has %zu\n",
                        gameType, fresh.Frame(), expected.size(), actual.size());
                return 1;
            }
            if (!expected.empty()) ++sceneryFrames;
        }
        if (sceneryFrames < 1000) {
            fprintf(stderr, "FAIL: stage geometry was absent for most of the opening\n");
            return 1;
        }
        printf("PASS game type %d: lobby scenery matches fresh stage across %d frames\n", gameType, sceneryFrames);
        lobby.RequestTitle();
        lobby.Step();
    }
    return 0;
}
