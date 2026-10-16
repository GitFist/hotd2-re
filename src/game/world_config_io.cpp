#include "game/world_config_io.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <type_traits>

namespace game {

namespace {

static_assert(std::is_trivially_copyable<WorldCarry>::value, "WorldCarry is written as raw bytes");

std::string Hex(const void* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) {
        s += d[b[i] >> 4];
        s += d[b[i] & 15];
    }
    return s;
}

bool Unhex(const std::string& s, void* p, size_t n) {
    if (s.size() != n * 2) return false;
    uint8_t* b = static_cast<uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(std::stoul(s.substr(i * 2, 2), nullptr, 16));
    return true;
}

}  // namespace

bool SaveWorldConfig(const WorldConfig& c, const std::string& path) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    fprintf(f, "# hotd2-re WorldConfig (world_test --save-config)\n");
    fprintf(f, "eventMode %d\nscene %d\ngameMode %d\ndemoIndex %d\ndifficulty %d\nfirstSerial %u\nstartFrame %d\n", c.eventMode,
            c.scene, c.gameMode, c.demoIndex, c.difficulty, c.firstSerial, c.startFrame);
    fprintf(f, "resourceStepsPerFrame %d\nseedCounter %u\nplayerState %d %d\nlivesSetting %d\nfreePlay %d\ncredits %d\n",
            c.resourceStepsPerFrame, c.seedCounter, c.playerState[0], c.playerState[1], c.livesSetting, c.freePlay ? 1 : 0,
            c.credits);
    fprintf(f, "coins %d %d\ncreditSetting %d\ntitleSkyYaw %d\ngameType %d\n", c.coinsStart, c.coinsContinue, c.creditSetting,
            c.titleSkyYaw, c.gameType);
    fprintf(f, "gunCal %s\n", Hex(c.gunCal, sizeof(c.gunCal)).c_str());
    fprintf(f, "carry %u %s\n", static_cast<unsigned>(sizeof(WorldCarry)), Hex(&c.carry, sizeof(WorldCarry)).c_str());
    fclose(f);
    return true;
}

bool LoadWorldConfig(const std::string& path, WorldConfig* c, std::string* err) {
    std::ifstream in(path);
    if (!in) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream s(line);
        std::string k;
        s >> k;
        int a = 0, b = 0;
        unsigned u = 0;
        if (k == "eventMode") s >> c->eventMode;
        else if (k == "scene") s >> c->scene;
        else if (k == "gameMode") s >> c->gameMode;
        else if (k == "demoIndex") s >> c->demoIndex;
        else if (k == "difficulty") s >> c->difficulty;
        else if (k == "firstSerial") s >> c->firstSerial;
        else if (k == "startFrame") s >> c->startFrame;
        else if (k == "resourceStepsPerFrame") s >> c->resourceStepsPerFrame;
        else if (k == "seedCounter") s >> c->seedCounter;
        else if (k == "playerState") s >> c->playerState[0] >> c->playerState[1];
        else if (k == "livesSetting") s >> c->livesSetting;
        else if (k == "freePlay") {
            s >> a;
            c->freePlay = a != 0;
        } else if (k == "credits") s >> c->credits;
        else if (k == "coins") {
            s >> a >> b;
            c->coinsStart = a;
            c->coinsContinue = b;
        } else if (k == "creditSetting") s >> c->creditSetting;
        else if (k == "titleSkyYaw") s >> c->titleSkyYaw;
        else if (k == "gameType") s >> c->gameType;
        else if (k == "gunCal") {
            std::string h;
            s >> h;
            if (!Unhex(h, c->gunCal, sizeof(c->gunCal))) {
                if (err) *err = "bad gunCal";
                return false;
            }
        } else if (k == "carry") {
            std::string h;
            s >> u >> h;
            if (u != sizeof(WorldCarry) || !Unhex(h, &c->carry, sizeof(WorldCarry))) {
                if (err) *err = "carry size differs (rebuild world_test and play.exe from the same source)";
                return false;
            }
        }
    }
    return true;
}

}  // namespace game
