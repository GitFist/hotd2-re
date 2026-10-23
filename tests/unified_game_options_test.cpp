// The unified port menu edits the existing native gameplay save without ever entering original mode 12.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>

#include "game/save_data.h"
#include "game/world.h"

namespace {
void Require(bool ok, const char* why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}
void SameSaveExceptPreferences(const game::SaveData& before, const game::SaveData& after) {
    const std::set<uint32_t> owned = {game::SaveData::kDifficulty, game::SaveData::kLives, game::SaveData::kCredits,
                                     game::SaveData::kPlayerCfg, game::SaveData::kPlayerCfg + 0x7C};
    for (uint32_t va = game::SaveData::kBase; va < game::SaveData::kBase + game::SaveData::kSize; ++va)
        if (!owned.count(va)) Require(before.U8(va) == after.U8(va), "gameplay edits preserve all other save bytes");
}
void Step(game::World& world, uint32_t buttons = 0, uint32_t secondButtons = 0) {
    game::WorldInput input;
    input.kind = 1;
    input.x = 630;
    input.y = 465;
    input.held = input.pressed = buttons;
    world.SetInput(0, input);
    input.held = input.pressed = secondButtons;
    world.SetInput(1, secondButtons ? input : game::WorldInput{});
    world.Step();
}

void CheckStartedRules(const game::World& world, int players, int rank, int lives, const char* context) {
    const auto& hud = world.Hud();
    bool ok = world.GameMode() == 6 && hud.gameType == 0 && hud.rank == rank;
    for (int p = 0; p < players; ++p)
        ok = ok && hud.player[p].state == 5 && hud.player[p].lives == lives;
    if (players == 1) ok = ok && hud.player[1].state == 9;
    if (!ok)
        std::fprintf(stderr, "%s: mode=%d type=%d rank=%d (expected %d), players=%d/%d lives=%d/%d (expected %d)\n",
                     context, world.GameMode(), hud.gameType, hud.rank, rank, hud.player[0].state,
                     hud.player[1].state, hud.player[0].lives, hud.player[1].lives, lives);
    Require(ok, context);
}

void StartLocalArcade(game::World& world, int players) {
    world.ForceMode(4, 0x10000, 0, 0);
    for (int i = 0; i < 3; ++i) Step(world);
    Step(world, 8, players == 2 ? 8 : 0);
    Step(world);
    Step(world);
}

void FreshBootRules(const char* gameDir, const game::ExeImage& exe, const std::filesystem::path& scratch) {
    // These are the original initial gameplay ranks after one/two players join, before
    // any hit, kill or timed rank growth. Checking the live HUD catches settings that
    // load into the options page but never reach the stage's configuration.
    const int soloRank[] = {1, 3, 5, 8, 12};
    const int pairRank[] = {5, 7, 9, 12, 15};
    const auto savePath = scratch / "fresh-boot-rules.bin";
    game::SaveData saved;
    saved.Defaults(exe);
    saved.SetU8(game::SaveData::kLives, 4);  // five lives, deliberately different from WorldConfig's three
    saved.SetU8(game::SaveData::kCredits, 9);
    game::WorldConfig config;
    config.portFeatures = true;
    config.applySaveSettings = true;
    config.savePath = savePath.string();
    std::string error;

    for (int difficulty = 0; difficulty < 5; ++difficulty) {
        saved.SetU8(game::SaveData::kDifficulty, static_cast<uint8_t>(difficulty));
        Require(saved.SaveNative(savePath.string()), "write fresh-boot difficulty fixture");
        for (int players : {1, 2}) {
            game::World local;
            Require(local.Load(gameDir, 0, &error), error.c_str());
            local.Boot(config);  // no options edits or Store call may apply the settings for this test
            StartLocalArcade(local, players);
            CheckStartedRules(local, players, players == 1 ? soloRank[difficulty] : pairRank[difficulty], 5,
                              "fresh local Arcade applies saved gameplay difficulty and lives");
            Require(local.Hud().credits == 10 - players, "fresh local Arcade applies saved continue allowance");
        }

        game::World online;
        Require(online.Load(gameDir, 0, &error), error.c_str());
        auto direct = config;
        direct.gameMode = 6;
        direct.gameType = 0;
        direct.scene = 0;
        direct.gameOver = true;
        direct.playerState[0] = direct.playerState[1] = 0;
        direct.startFrame = 901;
        direct.seedCounter = 902;
        direct.freePlay = false;
        direct.credits = 7;
        Require(online.StartNetworkGame(direct, &error), error.c_str());
        Require(!online.NetworkRoom() && online.Frame() == 901, "direct online start preserves the requested timeline");
        Step(online);
        Step(online);
        CheckStartedRules(online, 2, pairRank[difficulty], 5,
                          "fresh direct online start keeps saved rules after its boot-to-stage transition");
        Require(online.Hud().credits == 7, "direct online start retains explicitly supplied current credits");
    }

    // A demo uses its own fixed difficulty internally; returning through the actual
    // title/menu input path must still launch the player's saved Very Hard setting.
    game::World afterDemo;
    Require(afterDemo.Load(gameDir, 0, &error), error.c_str());
    afterDemo.Boot(config);
    afterDemo.ForceMode(5, 0x10000, 0, 0);
    for (int i = 0; i < 5; ++i) Step(afterDemo);
    Require(afterDemo.GameMode() == 5 && afterDemo.Draws().size() > 5, "actual attract demo starts before saved-rule regression");
    Step(afterDemo, 8);
    Step(afterDemo);
    Require(afterDemo.GameMode() == 3, "start exits attract demo to title");
    for (int i = 0; i < 5; ++i) Step(afterDemo);
    Step(afterDemo, 8);
    Step(afterDemo);
    Require(afterDemo.GameMode() == 4, "title start enters the mode select");
    for (int i = 0; i < 3; ++i) Step(afterDemo);
    Step(afterDemo, 8);
    Step(afterDemo);
    Step(afterDemo);
    CheckStartedRules(afterDemo, 1, soloRank[4], 5, "attract demo and title preserve saved rules for the next actual game");

    // Exactness tools deliberately opt out, even when the options page reads a save.
    game::World explicitRules;
    Require(explicitRules.Load(gameDir, 0, &error), error.c_str());
    auto exact = config;
    exact.applySaveSettings = false;
    exact.difficulty = 1;
    exact.livesSetting = 2;
    exact.creditSetting = 3;
    explicitRules.Boot(exact);
    Require(explicitRules.GetPortGameOptions().difficulty == 4 && explicitRules.GetPortGameOptions().lives == 5,
            "opt-out fixture still loads differing saved menu values");
    StartLocalArcade(explicitRules, 1);
    CheckStartedRules(explicitRules, 1, soloRank[1], 2, "explicit save-settings opt-out preserves caller gameplay rules");
    Require(explicitRules.Hud().credits == 3, "explicit save-settings opt-out preserves caller continue allowance");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto scratch = (argc == 3 ? std::filesystem::path(argv[2]) : std::filesystem::temp_directory_path()) /
                         ("hotd2-unified-options-" + std::to_string(id));
    std::filesystem::create_directories(scratch);
    const auto savePath = scratch / "gameplay-save.bin";
    game::World world;
    Require(!world.GetPortGameOptions().editable && !world.SetPortGameOptions({}) && !world.ResetPortGameOptions(),
            "unloaded world cannot edit or reset settings");
    std::string error;
    Require(world.Load(argv[1], 0, &error), error.c_str());
    game::ExeImage exe;
    Require(exe.Load((std::filesystem::path(argv[1]) / "Hod2.exe").string()), "load original settings defaults");
    game::SaveData before;
    before.Defaults(exe);
    before.SetU8(game::SaveData::kFlags, 0xA0);
    before.SetU8(game::SaveData::kTrainGrades + 3, 4);
    before.SetU8(game::SaveData::kBossGrades + 5, 3);
    before.SetU8(game::SaveData::kItems + 9, 2);
    before.SetS32(game::SaveData::kPlayerCfg + 4, 1234567);
    before.SetS32(game::SaveData::kPlayerCfg + 0x7C + 4, -7654321);
    auto record = before.Entry(2, 3);
    record.name[0] = 'T'; record.name[1] = 'S'; record.name[2] = 'T'; record.name[3] = 0;
    record.score = 31337;
    before.SetEntry(2, 3, record);
    Require(before.SaveNative(savePath.string()), "create isolated existing progression save");
    game::WorldConfig config;
    config.portFeatures = true;
    config.applySaveSettings = true;
    config.savePath = savePath.string();
    world.Boot(config);
    world.ForceMode(4, 0x10000, 0, 0);
    for (int i = 0; i < 3; ++i) Step(world);
    auto options = world.GetPortGameOptions();
    Require(options.editable && !options.freePlayUnlocked, "idle local menu exposes locked Free Play accurately");
    const auto initial = options;
    auto invalid = options;
    invalid.credits = 0;
    invalid.freePlayUnlocked = true;
    invalid.editable = true;
    Require(!world.SetPortGameOptions(invalid), "caller metadata cannot bypass Free Play unlock");
    invalid = options; invalid.difficulty = 5;
    Require(!world.SetPortGameOptions(invalid), "invalid difficulty is rejected");
    invalid = options; invalid.lives = 0;
    Require(!world.SetPortGameOptions(invalid), "invalid lives are rejected");
    invalid = options; invalid.credits = 10;
    Require(!world.SetPortGameOptions(invalid), "invalid continues are rejected");
    invalid = options; invalid.crosshair[1] = 4;
    Require(!world.SetPortGameOptions(invalid), "invalid player-two style is rejected");
    game::SaveData after;
    Require(after.LoadNative(savePath.string()) && after.Raw() == before.Raw(), "invalid edits are atomic and never save");
    options.difficulty = 4;
    options.lives = 5;
    options.credits = 9;
    options.crosshair[0] = 2;
    options.crosshair[1] = 3;
    options.editable = options.freePlayUnlocked = false;  // output-only values do not suppress a valid edit
    Require(world.SetPortGameOptions(options) && world.GameMode() == 4, "valid gameplay edits stay in the current menu");
    const auto edited = world.GetPortGameOptions();
    Require(edited.difficulty == 4 && edited.lives == 5 && edited.credits == 9 &&
            edited.crosshair[0] == 2 && edited.crosshair[1] == 3, "readback includes all five edited fields");
    Require(after.LoadNative(savePath.string()), "gameplay edits persist through the native save");
    SameSaveExceptPreferences(before, after);
    Require(after.U8(game::SaveData::kLives) == 4 && after.U8(game::SaveData::kCredits) == 9,
            "visible life count maps to original save index; continues preserve original encoding");
    Step(world, 8);
    Step(world);
    Step(world);  // first stage tick initializes the player's saved life count
    Require(world.GameMode() == 6 && world.Hud().player[0].lives == 5, "next actual Arcade start uses the edited life count");
    Require(!world.GetPortGameOptions().editable && !world.SetPortGameOptions(initial) && !world.ResetPortGameOptions(),
            "in-progress game cannot change or reset saved gameplay settings");
    game::SaveData activeSave;
    Require(activeSave.LoadNative(savePath.string()) && activeSave.Raw() == after.Raw(), "blocked active-game edits never write");
    world.Boot(config);
    world.ForceMode(4, 0x10000, 0, 0);
    Require(world.GetPortGameOptions().difficulty == 4 && world.GetPortGameOptions().crosshair[1] == 3,
            "fresh front-end boot reads the edited settings from disk");
    Require(world.ResetPortGameOptions(), "reset gameplay preferences succeeds in idle menu");
    game::SaveData defaults = before;
    defaults.DefaultSettings(exe);
    const auto reset = world.GetPortGameOptions();
    Require(reset.difficulty == defaults.U8(game::SaveData::kDifficulty) &&
            reset.lives == defaults.U8(game::SaveData::kLives) + 1 &&
            reset.credits == defaults.U8(game::SaveData::kCredits) &&
            reset.crosshair[0] == defaults.U8(game::SaveData::kPlayerCfg) &&
            reset.crosshair[1] == defaults.U8(game::SaveData::kPlayerCfg + 0x7C), "reset uses the actual executable defaults");
    Require(after.LoadNative(savePath.string()), "reset is saved");
    SameSaveExceptPreferences(before, after);
    config.portNetworkRoom = true;
    world.Boot(config); world.ForceMode(4, 0x10000, 0, 0);
    Require(!world.GetPortGameOptions().editable && !world.SetPortGameOptions(initial) && !world.ResetPortGameOptions(),
            "connected room owns gameplay rules; local settings cannot alter lockstep state");
    config.portNetworkRoom = false;
    config.portFeatures = false;
    world.Boot(config); world.ForceMode(4, 0x10000, 0, 0);
    Require(!world.GetPortGameOptions().editable && !world.SetPortGameOptions(initial) && !world.ResetPortGameOptions(),
            "original exactness mode does not expose native edits");
    config.portFeatures = true;
    config.savePatch = {{game::SaveData::kFlags, {0xA7}}};
    world.Boot(config); world.ForceMode(4, 0x10000, 0, 0);
    options = world.GetPortGameOptions();
    Require(options.freePlayUnlocked, "existing completion flags unlock Free Play");
    options.credits = 0;
    Require(world.SetPortGameOptions(options) && world.GetPortGameOptions().credits == 0,
            "unlocked Free Play is accepted and reads back");
    Require(after.LoadNative(savePath.string()) && after.U8(game::SaveData::kCredits) == 0xFF &&
            after.U8(game::SaveData::kFlags) == 0xA7, "Free Play uses the original sentinel and preserves all flag bits");
    world.ForceMode(7, 0x10000, 0, 0);
    Require(!world.GetPortGameOptions().editable, "game-over flow remains locked until returning to the menu");
    FreshBootRules(argv[1], exe, scratch);
    std::printf("PASS unified gameplay bridge: validation, all five fresh local/online gameplay difficulties, saved lives/continues, demo return, explicit opt-out, native persistence, safe defaults, progression/calibration preservation and lockstep guards\n");
    return 0;
}
