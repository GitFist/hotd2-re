#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "game/event_runner.h"
#include "game/world_internal.h"

namespace {
void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
std::vector<uint8_t> Words(std::initializer_list<uint32_t> words) {
    std::vector<uint8_t> bytes(words.size() * 4);
    size_t offset = 0;
    for (uint32_t word : words) { memcpy(bytes.data() + offset, &word, 4); offset += 4; }
    return bytes;
}
struct Host : game::EventHost {
    int players = 1, modeId = 0, enemies = 0, clears = 0;
    bool props = false;
    std::vector<std::pair<int, int>> selections;
    std::pair<int, int> limits;
    int LiveEnemies() override { return enemies; }
    void TypingWords(int group, int selector) override { selections.emplace_back(group, selector); }
    void TypingWordLimits(int maximum, int minimum) override { limits = {maximum, minimum}; }
    void TypingProps(bool value) override { props = value; }
    void TypingSceneClear() override { ++clears; props = false; }
    int TypingPlayers() override { return players; }
    int TypingModeId() override { return modeId; }
};

void WordCommands() {
    game::EventMemory memory;
    const uint32_t base = game::EventMemory::kStageBase;
    memory.Load(base, Words({0x61, 0, 25, 0x64, 1, 50, 0x65, 2, 75, 0x66, 1, 0x68, 500, 10, 0x4E}));
    game::EventOp decoded;
    Require(!game::DecodeEvent(memory, base, decoded), "gun scripts reject typing opcodes");
    memory.SetTypingDialect(true);
    Require(game::DecodeEvent(memory, base, decoded) && decoded.length == 12, "prepared word instruction decodes");
    Host host;
    game::EventRunner runner(memory, host, nullptr, nullptr);
    runner.script = base;
    runner.RunEvents();
    Require(!runner.error && runner.yield, "word commands reach the authored halt");
    Require(host.selections == std::vector<std::pair<int, int>>{{0, 25}, {1, 50}}, "player-count word selection is conditional");
    Require(host.props && host.limits == std::pair<int, int>{500, 10}, "word operands retain authored units");
}

void ConditionalBranches() {
    for (uint32_t argument : {0u, 1u, 100u, 101u}) {
        game::EventMemory memory;
        memory.SetTypingDialect(true);
        const uint32_t base = game::EventMemory::kStageBase;
        memory.Load(base, Words({0x73, argument, base + 24, 0x61, 3, 100, 0x4E}));
        Host host;
        game::EventRunner runner(memory, host, nullptr, nullptr);
        runner.script = base;
        runner.RunEvents();
        const bool expected = argument == 0 || argument == 101;
        Require(!runner.error && host.selections.size() == (expected ? 1u : 0u), "mode branches preserve their inverse form");
    }
}

void ClearWaits() {
    const uint32_t base = game::EventMemory::kStageBase;
    for (uint32_t op : {0x44u, 0x6Bu}) {
        game::EventMemory memory;
        memory.SetTypingDialect(true);
        memory.Load(base, Words({op, 0, 0x4E}));
        Host host;
        game::EventRunner runner(memory, host, nullptr, nullptr);
        runner.camera.settled = false;
        runner.script = base;
        runner.RunEvents();
        Require(runner.script == base && runner.yield, "reaching a wait ends the current frame");
        runner.RunEvents();
        if (op == 0x44) {
            Require(runner.script == base, "ordinary enemy clear still requires camera settling");
            runner.camera.settled = true;
            runner.RunEvents();
            Require(host.clears == 1, "clearing the last enemy clears the prop/selection window once");
        }
        Require(!runner.error && runner.script == base + 8, "typing clear wait has no extra gun-engine wait frame");
    }
}

void UnsupportedCommands() {
    game::EventMemory memory;
    memory.SetTypingDialect(true);
    const uint32_t base = game::EventMemory::kStageBase;
    memory.Load(base, Words({0x60, 0, 1, 2, 3, 2, base, 0xFFFFFFFF, 0x4E}));
    game::EventOp decoded;
    Require(game::DecodeEvent(memory, base, decoded) && decoded.length == 32, "challenge length includes terminated operand pairs");
    Host host;
    game::EventRunner runner(memory, host, nullptr, nullptr);
    runner.script = base;
    runner.RunEvents();
    Require(runner.error && runner.errorText.find("typing challenge") != std::string::npos, "a host without challenge support rejects its contract");
}

void ChallengeRewards() {
    auto reward = [](game::World::Impl& world, int roll) {
        uint32_t seed = 0;
        for (;; ++seed) {
            game::CrtRand rng(seed);
            if (rng.Next() % 100 == roll) break;
        }
        world.rng.Seed(seed);
        world.cfg.portTyping = true;
        world.cfg.eventMode = 2;
        world.player[0].state = 5;
        world.player[1].state = 9;
        world.typingChallenge.active = true;
        world.typingChallenge.state = 3;
        world.typingChallenge.success = true;
        world.typingChallenge.timer = 59;
        world.TypingChallengeStep();
    };
    game::World::Impl life;
    life.player[0].lives = 4;
    reward(life, 0);
    Require(life.player[0].lives == 5 && life.typing.stats().points == 0,
            "successful wave can award a life to a playing player");
    game::World::Impl full;
    full.player[0].lives = 5;
    reward(full, 0);
    Require(full.player[0].lives == 5 && full.typing.stats().points == 150,
            "wave life reward at the life cap gives original base points");
    game::World::Impl lower;
    lower.rank = 10;
    lower.player[0].lives = 5;
    reward(lower, 15);
    Require(lower.rank == 4 && lower.typingRankClears == 2, "wave difficulty item lowers rank for two clears");
    lower.TypingSceneClear();
    Require(lower.rank == 4 && lower.typingRankClears == 1, "first clear retains temporary difficulty");
    lower.TypingSceneClear();
    Require(lower.rank == 10 && lower.typingRankClears == 0, "second clear restores the saved rank");
    game::World::Impl score;
    score.player[0].lives = 5;
    reward(score, 40);
    Require(score.typing.stats().points == 100 && score.player[0].score == 0,
            "wave score item awards typing base points");
}

void ChallengeContinueGate() {
    game::World::Impl world;
    world.cfg.portTyping = true;
    world.gameMode = 6;
    world.subState = 4;
    world.player[0].state = 4;
    world.player[1].state = 9;
    world.typingChallenge.active = true;
    world.typingChallenge.state = 6;
    world.typingChallenge.inputBlocked = true;
    world.typingChallenge.finishFrames = 60;
    world.typingChallenge.keyActivity = true;
    world.TypingChallengeStep();
    Require(world.typingChallenge.finishFrames == 60, "a key pauses the cumulative wave-finish idle count");
    world.TypingChallengeStep();
    Require(world.typingChallenge.active && world.typingChallenge.inputBlocked && world.typingChallenge.finishFrames == 0,
            "continue screen holds the wave controller at its original completion gate");
    world.player[0].state = 9;
    world.TypingChallengeStep();
    Require(world.typingChallenge.finishFrames == 0, "no-player frames do not advance wave finish idle count");
}

void ChallengeDamageFails() {
    game::World::Impl world;
    world.cfg.portTyping = true;
    world.cfg.eventMode = 2;
    world.gameMode = 6;
    world.player[0].state = 5;
    world.player[0].lives = 5;
    world.typingChallenge.active = true;
    world.typingChallenge.state = 0;
    world.typingChallenge.shared = 1;
    world.runner = std::make_unique<game::EventRunner>(world.mem, world, nullptr, nullptr);
    world.runner->camera.modeCur = 2;
    world.PlayerTakeHit(0, 0, 0);
    Require(world.player[0].lives == 4 && world.typingChallenge.shared == 0,
            "an actual hit fails the wave even while lives remain above the wave-start value");
}

void SourceSpriteDepth() {
    game::SpriteDef def;
    def.depth = .1f; def.x = 320; def.y = 120; def.flags = 0xA;
    const float white[3] = {1,1,1};
    auto record = game::MakeSpriteRecord(def, 1, white);
    game::TLVertex vertices[4];
    game::TypingSpriteQuad(record, 40, 20, vertices);
    Require(std::abs(vertices[0].z - .01f) < .00001f && vertices[0].rhw == 1 &&
            vertices[0].x == 300 && vertices[0].y == 130,
            "original typing UI depth is visible and preserves its authored pixel placement");
    record.depth = 2;
    game::TypingSpriteQuad(record, 40, 20, vertices);
    Require(std::abs(vertices[0].z - .55f) < .00001f && vertices[0].rhw == 1,
            "original reward caption uses the source reciprocal depth branch");
}

void SpawnedAxeWords() {
    for (bool typing : {false, true}) {
        game::World::Impl world;
        world.cfg.portTyping = typing;
        world.gameMode = 6;
        world.canShoot = 1;
        world.player[0].state = 5;
        world.player[0].lives = 3;
        world.runner = std::make_unique<game::EventRunner>(world.mem, world, &world.env, &world.exe);
        game::EnemyTask johnny;
        johnny.anim.kind = 0x13;
        johnny.variant = 7;
        johnny.target = 0;
        johnny.anim.nodes[5].model = 0x1ECE;
        johnny.anim.nodes[5].hit[2] = -10.0f;
        world.SpawnThrownWeapon(johnny, 5);
        game::ActorTask* axe = nullptr;
        for (game::Task* task : world.ts.Live())
            if (auto* actor = dynamic_cast<game::ActorTask*>(task->data.get()); actor && actor->fn == 0x0045A4F0)
                axe = actor;
        Require(axe != nullptr, "actual Johnny throw creates the flying axe actor");
        if (!typing) {
            Require(axe->typingFlags == 0, "ordinary gun-game axes retain their native metadata");
            continue;
        }
        Require(axe->typingFlags == 0x38000A12u && axe->typingOriginType == 0x30,
                "spawned typing axe retains the source constructor's explicit word registration");
        axe->viewPos[2] = -10.0f;
        world.RegisterTarget(axe);
        world.TypingStep();
        const auto prompts = world.typing.prompts();
        Require(prompts.size() == 1 && prompts.front().projectile && prompts.front().text.size() == 1,
                "actual spawned axe receives a one-key projectile prompt through the world snapshot");
        world.typingCharacters = prompts.front().text;
        world.TypingStep();
        Require(world.typing.stats().completedWords == 1 && axe->typingRevision == 1 && (axe->flags34 & 8u),
                "typing the spawned axe key delivers its native shot-down transition");
    }
}
void DrillCheckpoints() {
    game::EventMemory memory;memory.SetTypingDialect(true);
    const uint32_t base=game::EventMemory::kStageBase;
    memory.Load(base,Words({0x70,0x42,1,0x71,0x4E}));
    Host host;game::EventRunner runner(memory,host,nullptr,nullptr);
    runner.script=base;runner.mode=7;runner.scene=1;runner.variant=2;runner.camera.routeBranch=3;
    runner.RunEvents();
    Require(!runner.error&&runner.typingCheckpoint.script==base+4,"Drill checkpoint resumes after its marker");
    runner.mode=2;runner.scene=8;runner.variant=4;runner.camera.routeBranch=1;
    runner.globals[123]=456;runner.script=base+12;runner.yield=false;
    runner.RunEvents();
    Require(!runner.error&&runner.mode==7&&runner.scene==1&&runner.variant==2&&runner.camera.routeBranch==3,
        "retry restores authored program identity and camera branch");
    Require(runner.globals[123]==456,"retry preserves live actor/timer globals rather than restoring a fake save state");
    runner.Start(7,1,{});
    Require(!runner.typingCheckpoint.script,"starting another stage clears the previous Drill checkpoint");
    game::EventRunner noCheckpoint(memory,host,nullptr,nullptr);
    noCheckpoint.script=base+12;noCheckpoint.RunEvents();
    Require(!noCheckpoint.error&&noCheckpoint.script==base+16,"retry without a checkpoint falls through safely");
}
} // namespace

int main() {
    WordCommands(); ConditionalBranches(); ClearWaits(); UnsupportedCommands(); ChallengeRewards(); ChallengeContinueGate(); ChallengeDamageFails(); SourceSpriteDepth(); SpawnedAxeWords(); DrillCheckpoints();
    std::puts("typing_event_test: OK");
}
