#include <algorithm>

#include "game/world_internal.h"
#include "game/mstack.h"

namespace game {
namespace {
void ChallengeGlyphs(World::Impl& world, int count, float alpha, uint32_t source) {
    static constexpr int ids[7] = {3513,3512,3516,3516,3512,3515,3514};
    static constexpr float x[7] = {209,251,283,320,350,388,434};
    for (int i = 0; i < count; ++i) world.TypingSprite(ids[i], x[i], 120, alpha, .1f, 0xA, source);
}
void ChallengeHud(World::Impl& world) {
    const auto& challenge = world.typingChallenge;
    if (world.gameMode != 5 && world.gameMode != 6) return;
    if (challenge.deadline != -1) {
        const int seconds = std::max(0, (challenge.deadline + 60) / 60);
        if (seconds / 10 != 0) world.TypingSprite(2974 + std::clamp(seconds / 10, 0, 9), 300, 65, 1, .1f, 0xA, 0x00495D50);
        world.TypingSprite(2974 + seconds % 10, 340, 65, 1, .1f, 0xA, 0x00495D50);
    }
    if (challenge.quota != 0) {
        const int target = challenge.instructionTarget;
        world.TypingSprite(3647, 244, 120, 1, .1f, 9, 0x00495D50);
        if (target >= 10) world.TypingSprite(3554 + std::clamp(target / 10, 0, 9), 312, 120, 1, .1f, 0xA, 0x00495D50);
        world.TypingSprite(3554 + target % 10, 332, 120, 1, .1f, 0xA, 0x00495D50);
        world.TypingSprite(3643, 353, 120, 1, .1f, 9, 0x00495D50);
        world.TypingSprite(3646, 380, 120, 1, .1f, 9, 0x00495D50);
    }
}
void ChallengeTask(Task&) {
    auto& world = *CurrentWorld();
    const bool showHud = world.typingChallenge.state <= 1;
    world.TypingChallengeStep();
    if (showHud) ChallengeHud(world);
}
void CountdownTask(Task&) {
    auto& world = *CurrentWorld();
    auto& challenge = world.typingChallenge;
    const int before = challenge.instructionAge;
    const int fadeStart = 90 + challenge.instructionFrames;
    const float glyphAlpha = std::clamp(1.0f - .1f * float(std::max(0, before - fadeStart)), 0.0f, 1.0f);
    if (before >= 20) ChallengeGlyphs(world, std::min(7, 1 + (before - 20) / 10), glyphAlpha, 0x00495B60);
    static constexpr int posters[7] = {2930,2932,3551,3570,2936,2938,3568};
    if (challenge.instruction >= 0 && challenge.instruction < 7) {
        const float alpha = std::min(glyphAlpha, std::clamp(.1f * float(before - 90), 0.0f, 1.0f));
        const int poster = posters[challenge.instruction];
        world.TypingSprite(poster, 192, 250, alpha, .1f, 0xA, 0x00495B60);
        world.TypingSprite(poster + 1, 448, 250, alpha, .1f, 0xA, 0x00495B60);
    }
    const int age = ++challenge.instructionAge;
    if (age >= 22 && age <= 82 && (age - 22) % 10 == 0) world.TypingSound(0x000836A9u);
    if (age == 90) world.TypingSound(0x20000146u);
    if (age >= 90 + challenge.instructionFrames + 10) {
        challenge.instruction = -1;
        world.ts.KillCurrent();
    }
}

struct RewardTask : WTask { int item = 0, age = 0; };
void RewardPresentation(Task& task) {
    auto& world = *CurrentWorld();
    auto& presentation = task.As<RewardTask>();
    const auto* reward = world.typingReference.Reward(presentation.item);
    if (!world.typingReferenceActive || !reward) { world.ts.KillCurrent(); return; }
    // 0x0040ADA0: the item icon rotates for 120 frames in view space. Source
    // projection A0D1F0 is the same focal length used by the native camera.
    const int file = world.typingReference.ModelFile(reward->model);
    if (file >= 0 && (reward->model >= int(world.modelLoaded.size()) || !world.modelLoaded[reward->model]))
        world.ProcessRequest({0x52, uint32_t(file)});
    Mat4 matrix = Mat4::Identity();
    mx::Translate(matrix, -reward->x / world.focal, -(reward->y - 160.0f) / world.focal, -1);
    mx::RotY(matrix, presentation.age * 364 - 21840);
    const float scale = reward->scale * .03f;
    mx::Scale(matrix, scale, scale, scale);
    world.Draw(reward->model, matrix, 8, 1, false, -1, true);
    world.TypingSprite(reward->sprite - 0x4000, 320, 202, 1, 2, 0xA, 0x0040ADA0);
    if (++presentation.age > 120) world.ts.KillCurrent();
}

void ChallengeReward(World::Impl& world) {
    // 0x00466BF0: a single 0..99 draw, weighted by the lowest life count of
    // currently playing players. The three original reward ids are 1, 9, 4.
    static constexpr int weights[6][2] = {{70, 20}, {70, 20}, {50, 30}, {30, 40}, {20, 30}, {10, 20}};
    int lives = 5;
    for (const auto& player : world.player) if (player.state == 5) lives = std::min(lives, int(player.lives));
    lives = std::clamp(lives, 0, 5);
    const int draw = world.Rand(0x00466C4B) % 100;
    const int reward = draw < weights[lives][0] ? 1 : draw < weights[lives][0] + weights[lives][1] ? 9 : 4;
    world.typingChallenge.reward = reward;
    auto* presentation = world.NewTask<RewardTask>(RewardPresentation, 0x0040B070);
    presentation->item = reward;
    // 0x00454140 saves the rank even when this particular reward is life/score.
    world.typingSavedRank = world.rank;
    if (reward == 9) {
        world.typingRankClears += 2;
        world.rank = std::max(0, world.rank - 6);
    } else {
        for (auto& player : world.player) if (player.state == 5) {
            if (reward == 1) {
                const auto life = TypingAddLife(player.lives);
                player.lives = static_cast<int16_t>(life.lives);
                if (life.basePoints) world.typing.addBasePoints(life.basePoints);
            } else {
                world.typing.addBasePoints(100);
            }
        }
    }
    world.typingChallenge.rewardPending = false;
}
} // namespace

void World::Impl::TypingSprite(int rawId, float x, float y, float alpha, float depth, uint32_t flags, uint32_t source) {
    if (!typingReferenceActive) return;
    const auto* sprite = typingReference.SourceSprite(0x4000 + rawId);
    if (!sprite) return;
    if (sprite->file >= texFiles.size() || texFiles[sprite->file] != 4) ProcessRequest({0x54, sprite->file});
    SpriteDef draw;
    draw.id = 0x4000 + rawId; draw.x = x; draw.y = y; draw.alpha = alpha;
    draw.depth = depth; draw.flags = flags; draw.source = source;
    hudSprites.push_back(draw);
}

bool World::Impl::TypingChallenge(const EventOp& op) {
    if (!cfg.portTyping || op.args.size() < 5 || typingChallenge.active) return false;
    TypingChallengeCtl challenge;
    challenge.active = true;
    challenge.shared = 1;
    challenge.deadline = static_cast<int32_t>(op.args[0]);
    challenge.interval = static_cast<int32_t>(op.args[1]);
    challenge.maximum = static_cast<int32_t>(op.args[2]);
    challenge.quota = static_cast<int32_t>(op.args[3]);
    challenge.timer = challenge.interval - 1;
    challenge.side = Rand(0x00420695) & 1;
    challenge.startingErrors = typing.stats().errors;
    challenge.instruction = typingChallenge.instruction;
    challenge.instructionFrames = typingChallenge.instructionFrames;
    challenge.instructionAge = typingChallenge.instructionAge;
    challenge.instructionTarget = typingChallenge.instructionTarget;
    if (challenge.interval <= 0 || challenge.maximum <= 0 || challenge.maximum > 14 || challenge.quota < 0) return false;
    for (size_t i = 4; i + 1 < op.args.size() && op.args[i] != 0xFFFFFFFFu; i += 2) {
        const uint32_t weight = op.args[i], record = op.args[i + 1];
        if (!weight || weight > 10 || challenge.weighted.size() + weight > 10) return false;
        const int index = static_cast<int>(challenge.records.size());
        challenge.records.push_back(record);
        for (uint32_t k = 0; k < weight; ++k) challenge.weighted.push_back(index);
    }
    if (challenge.weighted.size() != 10) return false;
    typingChallenge = std::move(challenge);
    ++attackers; // the original challenge controller itself holds one attacker count
    NewTask<WTask>(ChallengeTask, 0x00495FA0);
    return true;
}

void World::Impl::TypingCountdown(int instruction, int frames) {
    static constexpr int targets[7] = {10,10,8,0,8,0,3};
    typingChallenge.instruction = instruction;
    typingChallenge.instructionFrames = frames;
    typingChallenge.instructionAge = 0;
    typingChallenge.instructionTarget = instruction >= 0 && instruction < 7 ? targets[instruction] : 0;
    NewTask<WTask>(CountdownTask, 0x00495B60);
}

int World::Impl::TypingChallengeStatus() {
    auto& challenge = typingChallenge;
    if (challenge.active && challenge.state <= 1 && cfg.eventMode == 4 &&
        typing.stats().errors > challenge.startingErrors) challenge.shared = 0;
    return challenge.shared;
}

void World::Impl::TypingClearActorSlots() { std::fill(std::begin(actorSlots), std::end(actorSlots), nullptr); }

void World::Impl::TypingChallengeStep() {
    auto& challenge = typingChallenge;
    if (!challenge.active) { ts.KillCurrent(); return; }
    TypingChallengeStatus();
    auto result = [&](bool success) {
        challenge.state = success ? 3 : 2;
        challenge.success = success;
        challenge.timer = 0;
        if (success) return; // success presentation starts on the next task frame
        challenge.inputBlocked = true;
        typing.cancelSelection();
        typingCharacters.clear();
        TypingAttackTimer(2);
    };
    if (challenge.state == 0) {
        if (challenge.shared == 0) { result(false); return; }
        if (cfg.eventMode == 3 && runner->scene == 0 && liveEnemies == 2) challenge.timer = challenge.interval - 1;
        if ((cfg.eventMode == 3 && (runner->scene == 1 || runner->scene == 4) && attackers >= 4) ||
            (cfg.eventMode == 1 && runner->scene == 3 && attackers >= 3)) challenge.timer = challenge.interval - 2;
        const bool freeSlot = std::any_of(std::begin(actorSlots), std::end(actorSlots), [](const auto* actor) { return actor == nullptr; });
        if (freeSlot && ++challenge.timer == challenge.interval) {
            if (liveEnemies < challenge.maximum) {
                challenge.timer = 0;
                static constexpr int intervals[4] = {90, 70, 50, 30};
                challenge.interval = intervals[std::clamp(rank / 4, 0, 3)];
                int choice = Rand(0x00496238) % 10;
                if (cfg.eventMode == 3 && runner->scene == 0) {
                    const int side = ++challenge.side & 3;
                    if (side == 0) choice = 0;
                    else if (side == 1) choice = 2 + (Rand(0x004962C1) & 3);
                    else if (side == 2) choice = 1;
                    else choice = 6 + (Rand(0x004962E4) & 3);
                } else if (cfg.eventMode == 3 && runner->scene == 3) {
                    choice = (++challenge.side & 1) * 5 + Rand(0x00496314) % 5;
                } else if (cfg.eventMode == 5 && runner->scene == 3) {
                    choice = (++challenge.side & 1) * 5;
                }
                Spawn(0x0B, challenge.records[static_cast<size_t>(challenge.weighted[choice])]);
                if (challenge.quota != 0) {
                    // Counted waves return immediately after spawning. Their
                    // quota/deadline transition is evaluated on the next frame.
                    ++challenge.spawned;
                    return;
                }
            } else {
                --challenge.timer;
            }
        }
        if (challenge.quota != 0 && challenge.spawned == challenge.quota) {
            challenge.state = 1;
            challenge.timer = 0;
        }
        if (challenge.deadline != -1 && challenge.deadline-- == 0) {
            result(challenge.quota == 0 || challenge.spawned >= challenge.quota);
        }
        return;
    }
    if (challenge.state == 1) {
        if (challenge.deadline != -1 && --challenge.deadline == 0) { result(false); return; }
        if (challenge.shared == 0) { result(false); return; }
        if (liveEnemies == 0) { challenge.state = 3; challenge.success = true; challenge.timer = 0; }
        return;
    }
    if (challenge.state == 2 || challenge.state == 3) {
        challenge.shared = 0;
        challenge.inputBlocked = true;
        TypingAttackTimer(2);
        if (++challenge.timer == 60) {
            TypingSound(challenge.success ? 0x20000147u : 0x20000137u);
            if (challenge.success && cfg.eventMode < 6) ChallengeReward(*this);
        }
        if (challenge.timer == 180 || (!challenge.success && (gameMode != 6 || subState == 4))) {
            challenge.state = 6; challenge.finishFrames = 0; challenge.keyActivity = false;
        } else if (cfg.eventMode != 6) {
            ChallengeGlyphs(*this, 7, 1, challenge.success ? 0x004966A0 : 0x00496510);
            if (challenge.timer > 60) TypingSprite(challenge.success ? 3051 : 3052, 320, 146, 1, .1f, 0xA,
                                                   challenge.success ? 0x004966A0 : 0x00496510);
        }
        return;
    }
    if (challenge.state != 6) return;
    const bool playersPresent = std::any_of(std::begin(player), std::end(player), [](const auto& player) {
        return player.state == 0 || player.state == 1 || player.state == 3 || player.state == 4 || player.state == 5;
    });
    if (!playersPresent) { challenge.keyActivity = false; return; }
    if (!challenge.keyActivity) ++challenge.finishFrames;
    challenge.keyActivity = false;
    if (challenge.finishFrames > 60) {
        // 0x004B1370 resets the accumulated idle count before the caller checks
        // its mode/continue gates. A held continue screen cannot release it.
        challenge.finishFrames = 0;
        if ((gameMode != 5 && gameMode != 6) || subState == 4) return;
        challenge.inputBlocked = false;
        challenge.active = false;
        --attackers;
        ts.KillCurrent();
    }
}

} // namespace game
