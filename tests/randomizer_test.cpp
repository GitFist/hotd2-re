#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <set>
#include "game/world_modes.h"
#include "game/sound_table.h"

static void Require(bool ok, const char* why) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static void Input(game::World& w, int p, uint32_t buttons = 0) {
    game::WorldInput in;
    in.pressed = in.held = buttons;
    w.SetInput(p, in);
}
static void Start(game::World& w, const char* dir, int seed) {
    Require(w.Load(dir), "load world");
    game::WorldConfig cfg;
    cfg.portFeatures = cfg.portModernMenu = cfg.portNetworkRoom = true;
    cfg.gameMode = 6; cfg.gameOver = true;
    cfg.startFrame = 901; cfg.seedCounter = seed; cfg.creditSetting = -1;
    Require(w.StartNetworkGame(cfg), "start room");
    for (int i = 0; i < 5; ++i) { Input(w, 0, 0x20); w.Step(); Input(w, 0); w.Step(); }
    Input(w, 0, 4); w.Step(); Input(w, 0); w.Step();
}
// Optional real-client replay for visual QA: leave scenes unskipped and aim only at hostile hit targets.
static void RecordScenes(const char* dir, const char* path) {
    game::World w;
    Require(w.Load(dir), "load scene replay");
    game::WorldConfig cfg;
    cfg.portFeatures = cfg.portModernMenu = true;
    cfg.gameOver = true; cfg.startFrame = 0; cfg.creditSetting = -1;
    w.Boot(cfg);
    std::ofstream out(path);
    Require(out.good(), "open scene replay output");
    uint8_t previous = 0;
    std::set<std::string> costumes, lines;
    for (int frame = 1; frame <= 18000; ++frame) {
        float x = .5f, y = .45f; int buttons = 0;
        if (frame == 700) buttons = 4;
        if (frame >= 800 && frame <= 830) { x = .15f; y = .83f; buttons = frame == 830 ? 1 : 0; }
        if (w.GameMode() == 6 && frame > 2400) {
            bool targetFound = false;
            for (const auto& target : w.ShotTargets()) {
                if (target.civilian) continue;
                for (const auto& node : target.nodes) {
                    if (node[2] >= -1) continue;
                    const int sx = static_cast<int>(320 + node[0] * w.Focal() / -node[2]);
                    const int sy = static_cast<int>(240 - node[1] * w.Focal() / -node[2]);
                    if (sx < 0 || sx >= 640 || sy < 0 || sy >= 480 || w.PreviewShot(0, sx, sy).serial != target.serial) continue;
                    x = sx / 640.0f; y = sy / 480.0f; targetFound = true; break;
                }
                if (targetFound) break;
            }
            if (w.Hud().player[0].state == 4) buttons = frame % 10 == 0 ? 4 : 0;
            else if (targetFound && w.Hud().canShoot) buttons = w.Hud().player[0].ammo == 0 ? 2 : frame % 4 == 0 ? 1 : 0;
        }
        out << frame << " 0 " << x << ' ' << y << ' ' << buttons << '\n';
        w.SetInput(0, game::MakeWorldInput(x, y, buttons, previous)); Input(w, 1);
        previous = static_cast<uint8_t>(buttons); w.Step();
        for (const auto& note : w.Notes()) {
            if (note.find("randomizer: costume") == 0) {
                if (costumes.insert(note).second) printf("Scene replay frame %d: %s\n", frame, note.c_str());
            }
            if (note.find("randomizer: dialogue") == 0) lines.insert(note);
        }
    }
    Require(!costumes.empty() && lines.size() > 5, "unskipped campaign scenes reach cosmetic characters and dialogue");
    printf("PASS scene replay: %zu costume swaps, %zu dialogue swaps\n", costumes.size(), lines.size());
}
int main(int argc, char** argv) {
    if (argc == 3) { RecordScenes(argv[1], argv[2]); return 0; }
    if (argc != 2) return 2;
    game::World::Impl w;
    w.gameDir = argv[1];
    Require(w.exe.Load((std::filesystem::path(argv[1]) / "Hod2.exe").string()), "tables");
    w.cfg.portFeatures = true; w.cfg.gameType = 1; w.cfg.gameMode = w.gameMode = 6;
    game::ModesBoot(w);
    w.modelLoaded.assign(0x4000, 0);
    w.seedCounter = 12345;
    game::RandomizerBegin(w, true);
    Require(w.modes->randomizer.voiceLines.size() > 300, "full voiced dialogue pool");
    game::SoundTable soundTable;
    Require(soundTable.Load(w.exe), "sound table");
    for (int line : w.modes->randomizer.voiceLines) {
        const auto sound = w.exe.U32(0x00589DA8 + line * 16 + 12);
        const auto request = game::ResolveSound(soundTable, sound);
        const auto file = std::filesystem::path(argv[1]) / request.path;
        std::ifstream input(file, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        game::WavInfo info;
        Require(!bytes.empty() && game::ParseWavHeader(bytes.data(), bytes.size(), info) && info.dataSize > 0,
                "every replacement dialogue WAV exists and is readable");
    }
    std::set<int> voices, costumes;
    for (int seed = 1; seed <= 64; ++seed) {
        w.seedCounter = seed; game::RandomizerBegin(w, true);
        const auto beforeHash = game::RandomizerHash(w);
        const auto beforeRng = w.rng.State();
        for (int line = 1; line < 401; ++line) {
            const auto original = w.exe.U32(0x00589DA8 + line * 16 + 12);
            const int changed = game::RandomizerVoiceLine(w, line);
            if ((original >> 28) != 2) { Require(changed == line, "silent text is unchanged"); continue; }
            voices.insert(changed);
            const auto replacement = game::RandomizerVoiceSound(w, original);
            Require(replacement != original && replacement == w.exe.U32(0x00589DA8 + changed * 16 + 12),
                    "replacement audio and subtitles always describe the same line");
            w.sounds.clear(); w.SoundCall(game::World::kPlayGameSound, original);
            Require(w.sounds.size() == 1 && w.sounds[0].id == replacement, "real audio dispatch uses randomized sound");
            Require(game::RandomizerVoiceLine(w, line) == changed, "repeated dialogue keeps its run mapping");
        }
        for (int kind = 0x20; kind <= 0x43; ++kind) {
            if (kind == 0x41) continue;
            const int changed = game::RandomizerCharacterKind(w, kind);
            Require(changed != kind, "scene character appearance changes");
            costumes.insert(changed);
            const auto& original = w.EnemySkeleton(kind);
            const auto& target = w.EnemySkeleton(changed);
            Require(target.nodeCount == 16 && target.nodes.size() == original.nodes.size(), "compatible costume rig");
            for (size_t i = 0; i < target.nodes.size(); ++i)
                Require(target.nodes[i].index == original.nodes[i].index && target.nodes[i].parent == original.nodes[i].parent,
                        "same bone identities and hierarchy preserve scene animation");
            game::EnemyTask actor;
            actor.anim.kind = static_cast<int16_t>(kind); actor.anim.drawFlags = 1;
            actor.anim.nodes[0].mv = game::Mat4::Identity();
            actor.civilian = true; actor.hp = 50; actor.behaviour = 23;
            for (auto& node : actor.anim.nodes) { node.cur[0] = 512; node.cur[1] = 1024; }
            const auto originalAnim = actor.anim;
            w.draws.clear(); w.Draw(1, game::Mat4::Identity()); w.Draw(2, game::Mat4::Identity());
            game::RandomizerCharacterDraw(w, actor, 1);
            Require(w.draws.size() > 10 && w.draws[0].model == 1, "costume retains unrelated scene draws");
            bool skin = false;
            for (size_t i = 1; i < w.draws.size(); ++i) {
                const auto& draw = w.draws[i];
                if (draw.model >= static_cast<int>(w.modelLoaded.size()) || !w.modelLoaded[draw.model])
                    fprintf(stderr, "costume seed %d kind %X -> %X missing model %X node %d\n", seed, kind, changed, draw.model, draw.node);
                Require(draw.model < static_cast<int>(w.modelLoaded.size()) && w.modelLoaded[draw.model], "costume models resident");
                for (float v : draw.world.m) Require(std::isfinite(v), "finite retargeted pose");
                skin |= draw.skinCount == 4;
            }
            const uint32_t attachments = w.exe.U32(0x0052ED08 + 4 * changed);
            const int attachmentCount = static_cast<int>(w.exe.U32(attachments));
            Require(attachmentCount <= 2, "common costume skin table has at most two attachments");
            bool hasSkin = false;
            for (int i = 0; i < attachmentCount; ++i) hasSkin |= w.exe.U32(w.exe.U32(attachments + 4) + 4 * i) != 0;
            Require(!hasSkin || skin, "costume includes its skinned joints");
            Require(!memcmp(&actor.anim, &originalAnim, sizeof(originalAnim)) && actor.civilian && actor.hp == 50 &&
                    actor.behaviour == 23, "appearance does not change actor animation, collision or civilian role");
            actor.bodyOf = 0;
            const auto size = w.draws.size();
            game::RandomizerCharacterDraw(w, actor, 0);
            Require(w.draws.size() == size, "active player body is excluded");
        }
        for (int kind : {0, 7, 0x41, 0x44, 0x45, 0x53})
            Require(game::RandomizerCharacterKind(w, kind) == kind, "combat actors and incompatible rigs are excluded");
        for (uint32_t sound : {0u, 0x000100A0u, 0x10000001u, 0x80000002u})
            Require(game::RandomizerVoiceSound(w, sound) == sound, "music, effects and stop commands stay unchanged");
        Require(game::RandomizerHash(w) == beforeHash && w.rng.State() == beforeRng, "cosmetics never advance gameplay RNG");
    }
    printf("Cosmetic coverage: %zu voice lines, %zu costumes\n", voices.size(), costumes.size());
    Require(voices.size() > 300 && costumes.size() == 35, "voice and complete human/zombie appearance diversity");
    game::RandomizerBegin(w, false);
    Require(game::RandomizerVoiceLine(w, 3) == 3 && game::RandomizerVoiceSound(w, 0x2000006D) == 0x2000006D &&
            game::RandomizerCharacterKind(w, 0x39) == 0x39, "cosmetics disabled outside Randomizer");
    printf("PASS dialogue assets, matched audio/subtitles, cosmetic diversity, compatible rigs, unchanged actor state and RNG\n");
    std::set<int> weapons, bonuses, enemies, counts;
    for (int seed = 1; seed <= 128; ++seed) {
        w.seedCounter = seed;
        game::RandomizerBegin(w, true);
        for (int stage = 0; stage < 6; ++stage) {
            int previous[2] = {w.modes->randomizer.slots[0][0], w.modes->randomizer.slots[1][0]};
            w.cfg.eventMode = stage;
            game::RandomizerStage(w);
            auto& r = w.modes->randomizer;
            for (int p = 0; p < 2; ++p) {
                Require(stage == 0 || r.slots[p][0] != previous[p], "new chapter changes both weapons");
                Require(game::OriginalItemsCompatible(w, r.slots[p][0], r.slots[p][1]), "compatible equipment");
                Require(game::ItemWeapon(w, p) == r.slots[p][0] + 1, "real Original weapon effects");
                weapons.insert(r.slots[p][0]); bonuses.insert(r.slots[p][1]);
            }
            const auto hash = game::RandomizerHash(w);
            game::RandomizerStage(w);
            Require(game::RandomizerHash(w) == hash, "same chapter does not reroll");
        }
        w.ts.Reset(); w.cfg.eventMode = 0;
        game::SpawnRecord record;
        record.type = 0x30; record.state = 100; record.params[0] = 7; record.params[2] = 1;
        Require(game::RandomizerSpawn(w, 9, record), "ordinary encounter is randomized");
        auto tasks = w.ts.Live(); int n = 0;
        for (auto* task : tasks) if (auto* e = dynamic_cast<game::EnemyTask*>(task->data.get())) {
            ++n; enemies.insert(e->record.params[0]); Require(e->hp > 0, "replacement health");
            if (e->anim.kind == 2)
                Require(w.CollAt(static_cast<uint32_t>(e->P(4)), 4) != nullptr, "chainsaw hand collision mesh is resident");
        }
        counts.insert(n); Require(n >= 1 && n <= 3, "bounded encounter count");
        record.params[2] = 23;
        const auto hash = game::RandomizerHash(w);
        Require(!game::RandomizerSpawn(w, 9, record) && game::RandomizerHash(w) == hash, "scripted lunge stays intact");
        record.params[2] = 1; record.type = 0x10;
        Require(!game::RandomizerSpawn(w, 9, record), "civilian stays intact");
    }
    Require(weapons.size() == 4 && bonuses.size() > 8 && enemies.size() == 6 && counts.size() == 3,
            "seed diversity covers weapons, items, enemy kinds and group sizes");
    int inventory[33];
    const auto save = w.modes->save.Raw();
    for (int i = 0; i < 33; ++i) inventory[i] = w.modes->items[i];
    w.player[0].score = 12345; w.player[0].lives = 2; w.player[1].ammo = 2;
    const int credits = w.cfg.credits;
    Require(game::RandomizerCollect(w, 0, 1), "pickup applied");
    Require(game::ItemWeapon(w, 0) == 2 && w.player[0].score == 12345 && w.player[0].lives == 2 &&
            w.player[1].ammo == 2 && w.cfg.credits == credits, "pickup preserves scores, health, partner ammo and credits");
    game::ItemsFinalBoss(w); game::ModesSaveItems(w);
    Require(w.modes->save.Raw() == save, "save block and unlocks stay unchanged");
    for (int i = 0; i < 33; ++i) Require(w.modes->items[i] == inventory[i], "random rewards do not alter saved inventory");
    game::RandomizerBegin(w, false);
    Require(!game::RandomizerActive(w) && !game::RandomizerHash(w), "leaving resets mode");
    printf("PASS 128 seeds, six chapter rolls, compatible equipment, encounter diversity, safe exclusions and inventory isolation\n");

    game::World a, b;
    Start(a, argv[1], 12345); Start(b, argv[1], 12345);
    int encounters = 0, maxEnemies = 0;
    uint8_t previousButtons[2] = {};
    for (int f = 0; f < 9000; ++f) {
        float x = .5f, y = .45f;
        for (const auto& target : a.ShotTargets()) {
            if (target.civilian || target.fn != game::fn::kEnemy) continue;
            bool found = false;
            for (const auto& node : target.nodes) {
                if (node[2] >= -1) continue;
                const int sx = static_cast<int>(320 + node[0] * a.Focal() / -node[2]);
                const int sy = static_cast<int>(240 - node[1] * a.Focal() / -node[2]);
                if (sx < 0 || sx >= 640 || sy < 0 || sy >= 480) continue;
                if (a.PreviewShot(0, sx, sy).serial != target.serial) continue;
                x = sx / 640.0f; y = sy / 480.0f; found = true; break;
            }
            if (found) break;
        }
        for (const auto& note : a.Notes())
            Require(note != "collision mesh address outside the loaded files", "preview uses valid randomized enemy hit meshes");
        game::WorldInput inputs[2];
        for (int p = 0; p < 2; ++p) {
            const int buttons = a.Hud().player[p].state == 4 ? (f % 10 == 0 ? 4 : 0) :
                (a.Hud().player[p].ammo == 0 || f % 60 == 0) ? 2 : f % 4 == 0 ? 1 : 0;
            inputs[p] = game::MakeWorldInput(x, y, buttons, previousButtons[p]);
            previousButtons[p] = static_cast<uint8_t>(buttons);
        }
        // Same inputs through two independent games, as lockstep and rejoin history replay do.
        for (auto* world : {&a, &b}) {
            world->SetInput(0, inputs[0]); world->SetInput(1, inputs[1]);
            world->Step();
        }
        for (const auto& note : a.Notes()) {
            if (note.find("randomizer: encounter") == 0) ++encounters;
            Require(note != "collision mesh address outside the loaded files", "shots use valid randomized enemy hit meshes");
        }
        maxEnemies = std::max(maxEnemies, a.LiveEnemies());
        if (a.Hud().randomizerSeed == 0 || a.Hud().randomizerHash != b.Hud().randomizerHash || a.RandState() != b.RandState())
            fprintf(stderr, "replay frame %d mode %d/%d seed %u/%u hash %X/%X rng %X/%X\n", f, a.GameMode(), b.GameMode(),
                    a.Hud().randomizerSeed, b.Hud().randomizerSeed, a.Hud().randomizerHash, b.Hud().randomizerHash, a.RandState(), b.RandState());
        Require(a.Hud().randomizerSeed != 0 && a.Hud().randomizerHash == b.Hud().randomizerHash &&
                a.RandState() == b.RandState(), "randomized replay state matches");
        Require(a.Draws().size() == b.Draws().size(), "replay draw count");
        Require(a.Sounds().size() == b.Sounds().size(), "replay sound count");
        for (size_t i = 0; i < a.Sounds().size(); ++i)
            Require(a.Sounds()[i].id == b.Sounds()[i].id && a.Sounds()[i].kind == b.Sounds()[i].kind, "replay dialogue audio");
        Require(a.Hud().subtitles.size() == b.Hud().subtitles.size(), "replay subtitle count");
        Require(a.Hud().subtitles.size() <= 1, "interrupted randomized dialogue does not stack captions");
        for (size_t i = 0; i < a.Hud().subtitles.size(); ++i)
            Require(a.Hud().subtitles[i].group == b.Hud().subtitles[i].group &&
                    a.Hud().subtitles[i].line == b.Hud().subtitles[i].line, "replay subtitle content");
        for (size_t i = 0; i < a.Draws().size(); ++i)
            Require(a.Draws()[i].model == b.Draws()[i].model &&
                    !memcmp(a.Draws()[i].world.m, b.Draws()[i].world.m, sizeof(a.Draws()[i].world.m)), "replay draw matrices");
    }
    Require(encounters > 0, "real campaign reaches randomized encounters");
    // Exercise each chapter's real resource and player initialization, beyond isolated roll tests.
    for (int stage = 1; stage < 6; ++stage) {
        game::WorldConfig next;
        next.portFeatures = next.portModernMenu = next.portNetworkRoom = true;
        next.gameMode = 6; next.gameType = 1; next.eventMode = stage;
        next.scene = game::SceneSelectFirstScene(stage);
        next.startFrame = a.Frame() + 1; next.seedCounter = next.startFrame;
        next.playerState[0] = next.playerState[1] = 0;
        next.freePlay = true; next.creditSetting = -1;
        const auto seed = a.Hud().randomizerSeed;
        Require(a.StartStage(next) && b.StartStage(next), "later randomized chapter loads");
        int sceneryFrames = 0;
        for (int f = 0; f < 600; ++f) {
            for (auto* world : {&a, &b}) {
                Input(*world, 0, f % 90 == 0 ? 2 : 0); Input(*world, 1); world->Step();
            }
            Require(a.Hud().randomizerSeed == seed && a.Hud().randomizerHash == b.Hud().randomizerHash &&
                    a.RandState() == b.RandState(), "later chapter replay and run seed");
            for (const auto& draw : a.Draws()) if (draw.fn == 0x00401260) { ++sceneryFrames; break; }
        }
        Require(sceneryFrames > 0, "later chapter scenery loads");
        printf("PASS chapter %d: 600 matched frames, %d scenery frames\n", stage + 1, sceneryFrames);
    }
    a.RequestTitle(); Input(a, 0); Input(a, 1); a.Step();
    Require(a.NetworkLobby() && a.Hud().randomizerSeed == 0, "return to shared lobby clears randomizer");
    printf("PASS 9000 matched multiplayer/recovery replay frames, %d encounters, peak %d enemies, return to lobby\n", encounters, maxEnemies);
}
