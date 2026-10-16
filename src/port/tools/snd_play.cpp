// snd_play <game dir> <id> [<id> ...] [--ms N] [--state S --alt A] [--list] [--audio-backend sdl|xaudio2]
// Plays sound ids (hex, as passed to Snd_PlayGameSound 0x0041CFD0) through game::SoundDriver + SDL3 audio (or XAudio2), one after
// another, N ms each (default 3000). --state / --alt set 0x009C8E98 / 0x009CA08C (BGM table choice).
// --list prints the id -> file table instead.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "platform/platform.h"
#include "game/exe_image.h"
#include "game/sound_driver.h"
#include "game/sound_table.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: snd_play <game dir> <id> [<id> ...] [--ms N] [--state S --alt A] | --list\n");
        return 2;
    }
    const std::string dir = argv[1];
    std::vector<uint32_t> ids;
    int ms = 3000;
    bool list = false;
    game::SoundContext ctx;
    platform::CommonOptions common;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (platform::ParseCommonArg(argc, argv, i, &common)) continue;
        if (a == "--ms" && i + 1 < argc) ms = atoi(argv[++i]);
        else if (a == "--state" && i + 1 < argc) ctx.gameState = atoi(argv[++i]);
        else if (a == "--alt" && i + 1 < argc) ctx.altFlag = atoi(argv[++i]);
        else if (a == "--list") list = true;
        else ids.push_back(static_cast<uint32_t>(strtoul(a.c_str(), nullptr, 16)));
    }
    game::ExeImage exe;
    game::SoundTable table;
    if (!exe.Load(dir + "/Hod2.exe") || !table.Load(exe)) {
        fprintf(stderr, "cannot read the sound tables from %s\\Hod2.exe\n", dir.c_str());
        return 1;
    }
    if (list) {
        for (const auto& e : table.Se()) printf("%08X Sound\\SE\\%s\n", e.id, e.file.c_str());
        for (size_t i = 0; i < table.BgmA().size(); ++i)
            if (!table.BgmA()[i].empty()) printf("%08X Sound\\BGM\\%s\n", 0x10000000u | unsigned(i), table.BgmA()[i].c_str());
        for (size_t i = 0; i < table.Voices().size(); ++i)
            if (table.Voices()[i].number != -1) printf("%08X Sound\\Voice\\%s\n", 0x20000000u | unsigned(i), table.Voices()[i].file.c_str());
        return 0;
    }
    platform::Platform plat;
    std::string err;
    std::unique_ptr<port::AudioOutput> backend = plat.CreateAudio(common.audio, dir, &err);
    if (!backend) {
        fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    game::SoundDriver driver(*backend);
    for (uint32_t id : ids) {
        const game::SoundRequest r = game::ResolveSound(table, id, ctx);
        printf("%08X -> %s ch %d loop %d%s\n", id, r.path.empty() ? "(none)" : r.path.c_str(), r.channel, r.loop,
               r.stopLoopedSe ? " (stops looped SE)" : "");
        driver.PlayGameSound(table, id, ctx);
        plat.Delay(ms);
    }
    return 0;
}
