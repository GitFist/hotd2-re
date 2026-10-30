// Port-native typing simulation. Target handles are task serials plus damage-window
// tokens. Completions resolve live actors and validate native damage windows.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <string_view>

#include "game/mstack.h"
#include "game/world_held.h"
#include "game/world_internal.h"
#include "game/typing_boss_rules.h"
#include "game/typing_report.h"
#include "game/typing_results.h"
#include "game/sound_table.h"
#include "game/world_typing_original.h"

namespace game {

bool TypingPrivateBat(World::Impl&, ActorTask&, TypingTargetSpec&, bool complete);
bool TypingDrillProp(World::Impl&, ActorTask&, TypingTargetSpec&, bool complete);
void TypingWordHud(World::Impl&);
void TypingWordMistakeHud(World::Impl&, uint32_t serial);
void TypingWordCompleteHud(World::Impl&, const TypingCompletion&,std::string_view text);
void TypingWordCancelAudio(World::Impl&, bool hadSelection);
void TypingWordCompletionAudio(World::Impl&, uint32_t actorFlags, int player);
void TypingBoss5Mistype(World::Impl&, uint32_t serial, int player);

void TypingDeliver(ActorTask& a, int node) {
    const auto* world = CurrentWorld();
    if (world && world->cfg.portTyping && world->cfg.eventMode == 7 && world->cfg.gameType == 2) {
        // PC4B2180/4B22AF/4B2339: the completed word removes the
        // non-damaging key-hit marker. Flag2 retains its last key's node.
        if ((a.typingFlags & 2u) && a.hitNode[a.typingPlayer] > 0) node = a.hitNode[a.typingPlayer];
        a.flags34 &= ~0x1000u;
    }
    const uint32_t bit = 1u << (a.typingPlayer + 1);
    a.flags34 |= bit | 8u;
    a.hitNode[a.typingPlayer] = static_cast<int8_t>(node);
    if (EnemyNode* n = a.Node(node)) n->flags |= bit | 8u;
    a.typingStrike = true;
    ++a.typingRevision;
}

float TypingDamage(ActorTask& a, float base) {
    if (!a.typingStrike) return base;
    a.typingStrike = false;
    // A phrase is three ordinary hits, still consumed by the boss's own phase,
    // interrupt, score and death logic; it cannot directly change boss states.
    return base * 3.0f;
}

namespace {

using Adapter = bool (*)(World::Impl&, ActorTask&, TypingTargetSpec&, bool);
constexpr Adapter kBossAdapters[] = {TypingPrivateBat, TypingDrillProp, TypingNativeProp, TypingBoss1, TypingBoss2, TypingBoss3,
                                    TypingBoss4, TypingBoss5, TypingBoss6};

bool Ready(const World::Impl& w) {
    if(TypingDrillGameActive(w)&&!TypingDrillGameRunning(w))return false;
    return w.cfg.portTyping && w.gameMode == 6 && !w.paused && !w.typingResultsActive &&
           !w.typingChallenge.inputBlocked && w.canShoot != 0 &&
           !w.stageResults.shown && w.player[w.typingPlayer].state == 5;
}

bool Visible(World::Impl& w, const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]) &&
           v[2] < -0.1f && w.OnScreen(v);
}

// TOTD 0x004B26F0: optional prop words use the authored 640x480 rectangle
// and event-opcode 0x66's gate, independently of gun-target registration.
bool PropVisible(World::Impl& w, const float v[3]) {
    if (!w.typingPropsEnabled || !std::isfinite(v[0]) || !std::isfinite(v[1]) ||
        !std::isfinite(v[2]) || v[2] >= -0.1f) return false;
    const float x = w.focal * v[0] / v[2];
    const float y = w.focal * v[1] / v[2];
    return x >= -320.0f && x <= 320.0f && y >= -240.0f && y <= 240.0f;
}

// PC4B2520: distribute accepted letters across the original body/limb
// pairs. The node's100 bit records which member has already been used.
int DrillLetterNode(World::Impl& w, ActorTask& actor) {
    if (actor.typingFlags & 0x40000u) return 1;
    auto* enemy = dynamic_cast<EnemyTask*>(&actor);
    if (actor.typingFlags & 0x80000u) return enemy && enemy->c131F == 2 ? 17 : 24;
    if (actor.typingFlags & 0x100000u) return 2;
    if (!(actor.typingFlags & 0x10000000u) || !enemy) return 1;
    const double dx = double(enemy->pos[0]) - w.playerPos[0];
    const double dz = double(enemy->pos[2]) - w.playerPos[2];
    const int choices = std::sqrt(dx * dx + dz * dz) > 28.0 && enemy->behaviour != 4 ? 5 : 3;
    static constexpr int pairs[][2] = {{2,1},{3,4},{6,7},{10,11},{13,14}};
    const auto& pair = pairs[w.Rand(0x004B257B) % choices];
    int node = pair[1];
    if (const auto* part = actor.Node(node); part && (part->flags & 0x100u)) node = pair[0];
    if (enemy->anim.kind == 10 && node == 1) node = 2;
    if (auto* part = actor.Node(node)) part->flags |= 0x100u;
    return node;
}

void DrillLetterHit(World::Impl& w, bool mistake) {
    if (mistake || !w.cfg.portTyping || w.cfg.eventMode != 7 || w.cfg.gameType != 2 ||
        !TypingDrillGameRunning(w)) return;
    const uint32_t serial = w.typing.selectedSerial();
    for (Task* task : w.ts.Live()) {
        if (!task->alive || task->serial != serial || !task->data) continue;
        auto* actor = dynamic_cast<ActorTask*>(task->data.get());
        if (!actor || !(actor->typingFlags & 3u)) return;
        // PC4B20DA..2172 /4B21CE..2264: every accepted key produces
        // native hit effects/reactions without HP damage or a new word.
        const int node = DrillLetterNode(w, *actor);
        actor->typingPlayer = w.typingPlayer;
        actor->hitNode[w.typingPlayer] = static_cast<int8_t>(node);
        actor->flags34 |= (1u << (w.typingPlayer + 1)) | 0x1008u;
        if (actor->typingFlags & 0x20000000u)
            if (auto* part = actor->Node(node)) part->flags |= 8u;
        return;
    }
}

uint32_t Token(const World::Impl& w, const ActorTask& a, const TypingTargetSpec& s) {
    return (s.token * 65599u) ^ (a.typingRevision * 0x9E3779B9u) ^ (w.typingEpoch * 0x85EBCA6Bu);
}

bool Describe(World::Impl& w, ActorTask& a, TypingTargetSpec& s, bool complete) {
    s = TypingTargetSpec{};
    // PC441ACE creates the big enemy's hit proxy directly, without a word
    // post. Its owning enemy supplies the single original typing target.
    if(w.typingReferenceActive)if(const auto* enemy=dynamic_cast<const EnemyTask*>(&a))
        if(enemy->anim.kind==9&&enemy->parent13A4)return false;
    if (a.typingStrike && a.typingPlayer != w.typingPlayer) return false;
    if (complete) a.typingPlayer = w.typingPlayer;
    if (w.typingReferenceActive && a.typingOriginType != 0xFFFFFFFFu && a.typingFlags == 0) return false;
    // Source4B16F2..1743 always routes both prop styles through4B26F0,
    // while flag200 explicitly bypasses that projection/script gate.
    if (w.typingReferenceActive) {
        if ((a.typingFlags & 0x00408000u) && !(a.typingFlags & 0x200u) &&
            !PropVisible(w, a.viewPos)) return false;
    } else if ((a.typingFlags & 0x00400000u) && !w.typingPropsEnabled) return false;
    // Source4B1719 sends hidden words through the same exception path as an
    // event camera: flag200 forces them, or prop-style words use4B26F0.
    if (w.typingReferenceActive &&
        ((a.typingFlags & 0x400u) || (w.runner && w.runner->camera.modeCur == 1)) &&
        !(a.typingFlags & 0x200u) &&
        (!(a.typingFlags & 0x00408000u) || !PropVisible(w, a.viewPos))) return false;
    for (Adapter adapter : kBossAdapters) {
        if (!adapter(w, a, s, false)) continue;
        if (s.boss && !s.retain) return false; // private creature descriptor / defeated boss
        if (!w.typingReferenceActive && (a.typingFlags & 0x00400000u) && !PropVisible(w, s.view)) return false;
        if (s.node >= 0 && !Visible(w, s.view)) s.node = -1;
        if (!s.retain && s.node < 0) return false;
        if (complete) {
            if (s.node < 0) return false;
            TypingTargetSpec delivered;
            adapter(w, a, delivered, true);
            return delivered.node >= 0;
        }
        if (s.retain && (!std::isfinite(s.view[0]) || !std::isfinite(s.view[1]) ||
                         !std::isfinite(s.view[2]) || s.view[2] >= -0.1f)) {
            // Keep the assigned phrase when the boss briefly passes behind the
            // camera. Its completed strike still waits for a visible weak point.
            memcpy(s.view, a.viewPos, 12);
            if (!std::isfinite(s.view[0]) || !std::isfinite(s.view[1]) ||
                !std::isfinite(s.view[2]) || s.view[2] >= -0.1f) {
                s.view[0] = s.view[1] = 0.0f;
                s.view[2] = -100.0f;
            }
        }
        return true;
    }
    // Native hidden/death flags precede the actor-specific eligibility below.
    if (a.flags34 & 0x04008100u) return false;
    if (auto* e = dynamic_cast<EnemyTask*>(&a)) {
        // These native sphere targets have one-hit callbacks and no HP or
        // EnemyAnim draw-state contract (birds, fish and flying weapons).
        const bool sphereThreat = e->fn == 0x0042E230 || e->fn == 0x0042E9D0 ||
                                  e->fn == 0x0042ED50 || e->fn == 0x0042FCA0 ||
                                  e->fn == 0x00431000 || e->fn == 0x00438730 || e->fn == 0x004460C0 || e->fn == 0x0043E880 ||
                                  e->fn == 0x0045A4F0 || e->fn == 0x00450780;
        if (sphereThreat) {
            if (e->typingRevision != 0 || !Visible(w, e->viewPos)) return false;
            s.node = 1;
            s.danger = true;
            s.projectile = e->fn == 0x0045A4F0 || e->fn == 0x00450780 || e->fn == 0x0043E880;
            s.token = 0x730000u | (e->fn & 0xFFFFu);
            memcpy(s.view, e->viewPos, 12);
            if (complete) {
                TypingDeliver(a, 1);
                a.typingStrike = false;
            }
            return true;
        }
        if (e->civilian || dynamic_cast<CivTask*>(e) || e->bodyOf >= 0 || e->hp < 1 ||
            !(e->anim.drawFlags & 1) || dynamic_cast<Actor22Task*>(e)) return false;
        s.token = 0x700000u | (static_cast<uint32_t>(e->anim.kind) << 8);
        s.danger = e->target >= 0;
        // Choose a real, currently shootable node instead of assuming every
        // skeleton has the common zombie's node 2.
        const auto choose = [&](int i) {
            const EnemyNode* n = e->Node(i);
            if (!n || n->model == 0 || !(n->flags & 0x20) || (n->radius <= 0.0f && n->mesh == -1) || !Visible(w, n->hit)) return false;
            s.node = i;
            memcpy(s.view, n->hit, 12);
            return true;
        };
        if (!choose(2) && !choose(1)) {
            for (int i = 3; i < 32 && s.node < 0; ++i) choose(i);
        }
        if (s.node < 0 && !(a.flags34 & 0x80)) {
            s.node = 1;
            memcpy(s.view, a.viewPos, 12);
        }
        if (s.node < 0 || !Visible(w, s.view)) return false;
        if (complete) {
            // Custom birds/fish/throwables use their own one-hit HP handler.
            // Ordinary zombies consume typingStrike inside EnemyCollectShots.
            const bool drillLeaper=w.cfg.portTyping&&w.cfg.eventMode==7&&w.cfg.gameType==2&&e->typingOriginType==0x31;
            if (!drillLeaper && e->fn != fn::kEnemy && e->fn != 0x00453290 && e->fn != 0x0045CD60) e->hp = 1;
            TypingDeliver(a, s.node);
        }
        return true;
    }
    if (auto* held = dynamic_cast<HeldTask*>(&a)) {
        if (w.typingReferenceActive && held->typingOriginType == 0x30 && held->typingFlags == 0) return false;
        if (held->hp < 1) return false;
        s.node = 1;
        s.danger = held->state != 10;
        s.projectile = s.danger;
        s.token = 0x710000u | static_cast<uint32_t>(held->state);
        memcpy(s.view, held->viewPos, 12);
        if (!Visible(w, s.view)) return false;
        if (complete) {
            held->hp = 1;
            TypingDeliver(a, s.node);
            a.typingStrike = false;
        }
        return true;
    }
    // The supplied English PC reference gives words only to explicitly registered actors. A gun hit
    // callback alone does not qualify scenery, books, lamps or loose debris.
    if (a.typingOriginType != 0x41 || a.typingFlags == 0) return false;
    // Native barrels have two intact states; other one-hit props receive one word.
    const uint32_t words = a.fn == 0x0046A580 ? 2u : 1u;
    if (a.typingRevision >= words) return false;
    // Registered collision-only level meshes have no native hit recipient.
    if ((a.flags34 & 0x10) && !(a.flags34 & 0x60)) return false;
    s.node = 1;
    s.token = 0x720000u | (a.fn & 0xFFFFu);
    memcpy(s.view, a.viewPos, 12);
    if (a.flags34 & 0x10) {
        float world[3];
        mx::GetTranslation(a.m150, world); // Hit_Register has already made this a world matrix.
        mx::Transform(w.cam.view, world, s.view);
    }
    if (!Visible(w, s.view)) return false;
    if (!w.typingReferenceActive && (a.typingFlags & 0x00400000u) && !PropVisible(w, s.view)) return false;
    if (complete) {
        TypingDeliver(a, s.node);
        a.typingStrike = false;
    }
    return true;
}

} // namespace

void World::Impl::SetTypingSpawnOrigin(ActorTask& actor, const SpawnRecord& record) {
    actor.typingOriginType = record.type;
    actor.typingOriginKind = record.type == 0x41 ? record.ParamS8(1) : -1;
    actor.typingFlags = 0;
    actor.typingWordStyle = static_cast<int16_t>(record.state);
    if (const auto* source = dynamic_cast<const SpawnTask*>(CurrentTask()))
        if (source->op == 0x0A) actor.typingWordStyle = static_cast<int16_t>(record.flags);
    if (record.type == 0x41) {
        actor.typingPickupItem = record.ParamS8(0);
        // Reference tables 0x005B00D8 (word flags), 0x005AFF98
        // (generic task), and the constructor's source P0 != 0 check.
        // Kinds 0 and 37 have per-member conditions in their own constructors.
        if (record.ParamS8(0) == 0) return;
        switch (actor.typingOriginKind) {
        case 4:  if (actor.fn == fn::kBreakable) actor.typingFlags = 0x00400018u; break;
        case 14: if (actor.fn == 0x00468180) actor.typingFlags = 8; break;
        case 19: if (actor.fn == 0x00468F00) actor.typingFlags = 8; break;
        case 25: if (actor.fn == 0x00469AE0) actor.typingFlags = 8; break;
        case 34: if (actor.fn == 0x0046A580) actor.typingFlags = 0x00400018u; break;
        case 43: if (actor.fn == 0x0046CEA0) actor.typingFlags = 0x00400018u; break;
        case 56: if (actor.fn == 0x0046F090) actor.typingFlags = 8; break;
        default: break;
        }
        return;
    }
    // Preserve the original event actor's metadata without treating its helper
    // tasks as scenery targets. Native enemy/boss adapters still validate them.
    // Reference table 0x0059D4F8, indexed by event actor type.
    switch (record.type) {
    case 0x10: case 0x19: actor.typingFlags = 0x414; break;
    case 0x11: actor.typingFlags = 0x14; break;
    case 0x14: actor.typingFlags = 0x04040004; break;
    case 0x18: case 0x30: actor.typingFlags = 1; break;
    case 0x21: case 0x31: actor.typingFlags = 4; break;
    case 0x22: actor.typingFlags = 0x04000804; break;
    case 0x2D: actor.typingFlags = 0x04000C04; break;
    case 0x32: actor.typingFlags = 0x04204004; break;
    case 0x42: case 0x46: actor.typingFlags = 0x18; break;
    case 0x45: actor.typingFlags = 0x04080004; break;
    default: break;
    }
    if (actor.typingFlags != 0)
        actor.typingFlags |= typingReferenceActive && record.type == 0x10 ? 0x20000000u : 0x30000000u;
    if (typingReferenceActive && (record.type == 0x30 || record.type == 0x10)) {
        // PC441878..1909 /47EF30..EFDE consume authored header flags
        // into the separate word flags. Civilian20 selects a full word;
        // generic-enemy20 hides its post (e.g. the axe throwers' bodies).
        if (record.flags & 0x20u) {
            if (record.type == 0x10) actor.typingFlags = (actor.typingFlags & ~0x410u) | 8u;
            else actor.typingFlags |= 0x400u;
        }
        if (record.flags & 0x10000000u) actor.typingFlags |= 0x40u;
        else if (record.flags & 0x20000000u) actor.typingFlags |= 0x80u;
        else if (record.flags & 0x40000000u) actor.typingFlags |= 0x100u;
        if (record.flags & 0x80000000u) actor.typingFlags |= 0x04008000u;
    }
    if (record.type == 0x30 && record.ParamS8(1) == 4) actor.typingFlags |= 0x1000u;
}

void World::Impl::InitTypingSpawnOrigin(ActorTask& actor) {
    const auto* source = dynamic_cast<const SpawnTask*>(CurrentTask());
    if (source) SetTypingSpawnOrigin(actor, source->rec);
}

void World::Impl::TypingAttackTimer(int operation) {
    if (!cfg.portTyping) return;
    // Supplied English PC reference table 0x005E5320: difficulty rows, native rank columns.
    static constexpr int delays[5][16] = {
        {570,540,510,480,450,450,450,420,420,420,390,390,360,360,330,300},
        {480,480,450,420,420,420,420,390,390,360,360,330,330,300,270,240},
        {390,390,360,330,300,270,270,270,270,240,240,210,210,180,180,150},
        {360,330,300,270,270,240,240,240,240,240,210,210,180,180,150,150},
        {330,300,270,240,240,210,210,210,210,180,180,180,150,150,120,120}
    };
    switch (operation) {
    case 0: typingAttackDelay = 0; break;
    case 1: typingAttackDelay = std::max(0, typingAttackDelay - 1); break;
    case 2: {
        // TOTD 0x004376D0 applies the chapter rank cap as it reads the rank.
        if (cfg.gameType != 3 && cfg.eventMode == 0) rank = std::min(rank, 12);
        if (cfg.gameType != 3 && cfg.eventMode == 1) rank = std::min(rank, 14);
        const int difficulty = cfg.gameType == 2 || cfg.gameType == 3 ? 2 : std::clamp(cfg.difficulty, 0, 4);
        typingAttackDelay = delays[difficulty][std::clamp(rank, 0, 15)];
        break;
    }
    case 3: typingAttackDelay = std::max(120, typingAttackDelay); break;
    default: break;
    }
}

void World::Impl::ResetTyping() {
    typing.setProfilePath(cfg.portNetworkRoom || cfg.networkStartedAt || cfg.savePath.empty() ? std::string{} : cfg.savePath+".typing.profile");
    typing.reset();
    typing.setOriginalMode(cfg.typingOriginal);
    typing2.setProfilePath(cfg.portNetworkRoom || cfg.networkStartedAt || cfg.savePath.empty() ? std::string{} : cfg.savePath+".typing.p2.profile");
    typing2.reset();
    typing2.setOriginalMode(cfg.typingOriginal);
    typingItemKeys[0] = typingItemKeys[1] = 0;
    typingCharacters2.clear(); typingCancel2 = typingWasReady2 = false;
    typingPositions2.clear(); typingCachedTargets2.clear(); typingPendingBoss2.clear();
    typingHud2 = {}; typingGradeProgress2 = 0; typingPlayer = 0;
    typingCharacters.clear();
    typingCancel = false;
    typingPositions.clear();
    typingCachedTargets.clear();
    typingPendingBoss.clear();
    typingWasReady = false;
    typingAttackDelay = 0;
    typingResultsActive = false;
    typingChapterChooser = {};
    typingGradeProgress = 0;
    typingQuestion.clear();
    typingQuestionSeconds = 0;
    typingEmperorAnswerWeight = 0;
    typingChallenge = {};
    typingHud = {};
    typingSavedRank = typingRankClears = 0;
    ++typingEpoch;
}
void World::Impl::RestoreTypingGameOptions() {
    if (!typingPreviousOptions.valid) return;
    cfg.difficulty = typingPreviousOptions.difficulty;
    cfg.livesSetting = typingPreviousOptions.lives;
    cfg.creditSetting = typingPreviousOptions.creditSetting;
    cfg.credits = typingPreviousOptions.credits;
    cfg.freePlay = typingPreviousOptions.freePlay;
    typingPreviousOptions.valid = false;
}

void World::Impl::TypingStep() {
    if (!cfg.portTyping) return;
    if (gameMode == 6 && !paused) TypingAttackTimer(1);
    TypingPlayerStep();
    if (player[1].state != 9 || typingWasReady2) {
        typingHud2.story = typingHud.story;
        SwapTypingPlayer();
        TypingPlayerStep();
        SwapTypingPlayer();
    }
    TypingOriginalHud(*this);
}

void World::Impl::SwapTypingPlayer() {
    using std::swap;
    swap(typing, typing2); swap(typingCharacters, typingCharacters2);
    swap(typingCancel, typingCancel2); swap(typingWasReady, typingWasReady2);
    swap(typingPositions, typingPositions2); swap(typingCachedTargets, typingCachedTargets2);
    swap(typingPendingBoss, typingPendingBoss2); swap(typingHud, typingHud2);
    swap(typingGradeProgress, typingGradeProgress2);
    typingPlayer ^= 1;
}

void World::Impl::TypingPlayerStep() {
    TypingDrillGameBeforeTargets(*this);
    if (!cfg.portTyping) return;
    if (TypingDrillGameActive(*this)) typing.setKeyObserver([this](unsigned char expected, bool mistake) {
        TypingDrillGameTypedKey(*this, expected, mistake);
        DrillLetterHit(*this, mistake);
    });
    typing.setRandomSource([this] { return static_cast<uint32_t>(Rand(0x004B05B0)); });
    typing.setFrameCounter(frameCounter2BAC);
    typing.setDifficulty(static_cast<TypingDifficulty>(std::clamp(cfg.difficulty,0,4)));
    typing.setMaximumNewWordsPerSync(1);
    const bool ready = Ready(*this);
    if (typingCancel || !ready || !typingWasReady) {
        if (typingCancel && ready) TypingWordCancelAudio(*this, typing.selectedSerial() != 0);
        typing.cancelSelection();
        typingCharacters.clear();
        typingCancel = false;
    }
    typingWasReady = ready;
    typing.setDynamicRank(std::min(rank, cfg.eventMode == 0 ? 12 : cfg.eventMode == 1 ? 14 : 15));
    if (!ready) {
        typingItemKeys[typingPlayer] = 0;
        typingPositions.clear();
        TypingWordHud(*this);
        return;
    }

    // Never dereference the previous frame's registration pointers until they
    // have been matched to a live task. Native hit lists can retain freed blocks.
    std::map<ActorTask*, uint32_t> live;
    std::map<uint32_t, ActorTask*> bySerial;
    std::vector<std::pair<uint32_t, ActorTask*>> actorOrder;
    for (Task* t : ts.Live()) {
        if (!t->alive || !t->serial || !t->data) continue;
        if (auto* a = dynamic_cast<ActorTask*>(t->data.get())) {
            live.emplace(a, t->serial);
            bySerial.emplace(t->serial, a);
            actorOrder.emplace_back(t->serial, a);
        }
    }
    // Source4B1623: closing the authored prop window explicitly cancels its
    // selected word, freeing the keyboard for the remaining enemy words.
    if (typingReferenceActive && !typingPropsEnabled) {
        const auto active = bySerial.find(typing.selectedSerial());
        if (active != bySerial.end() && (active->second->typingFlags & 0x00408000u)) {
            TypingWordCancelAudio(*this, true);
            typing.cancelSelection();
            typingCharacters.clear();
        }
    }
    // Completed boss phrases wait across closed jaws, shields and movement.
    // Every retry resolves the serial and validates the current native window.
    for (auto pending = typingPendingBoss.begin(); pending != typingPendingBoss.end();) {
        const auto found = bySerial.find(pending->first);
        TypingTargetSpec current;
        if (found == bySerial.end()) {
            pending = typingPendingBoss.erase(pending);
        } else if (!Describe(*this, *found->second, current, false)) {
            // An event camera or a temporarily hidden word does not cancel an
            // already completed phrase. Validate its phase once it returns.
            ++pending;
        } else if (!current.retain || Token(*this, *found->second, current) != pending->second) {
            pending = typingPendingBoss.erase(pending);
        } else if (current.node >= 0 && Describe(*this, *found->second, current, true)) {
            pending = typingPendingBoss.erase(pending);
        } else {
            ++pending;
        }
    }
    std::set<ActorTask*> registered;
    for (const Target& t : targets) {
        const auto found = live.find(t.obj);
        if (found != live.end()) registered.insert(t.obj);
    }
    // Word-post storage outlives temporary eligibility loss. Retain its text,
    // progress and slot until the actor or its damage-window identity changes.
    for (auto cached = typingCachedTargets.begin(); cached != typingCachedTargets.end();) {
        const auto actor = bySerial.find(cached->first);
        if (actor == bySerial.end() || Token(*this, *actor->second, cached->second.second) != cached->second.first.token)
            cached = typingCachedTargets.erase(cached);
        else ++cached;
    }
    const uint32_t selected = typing.selectedSerial();
    const uint64_t beforeWords = typing.assignedWordCount();
    std::vector<TypingTarget> snapshot;
    typingPositions.clear();
    for (const auto& entry : actorOrder) {
        const uint32_t serial = entry.first;
        ActorTask& actor = *entry.second;
        TypingTargetSpec spec;
        const bool eligible = Describe(*this, actor, spec, false) && (registered.count(&actor) || spec.retain);
        auto cached = typingCachedTargets.find(serial);
        if (eligible) {
            TypingTarget target;
            target.serial = serial;
            target.token = Token(*this, actor, spec);
            target.chapter = std::clamp(cfg.eventMode + 1, 1, 6);
            target.boss = spec.boss;
            if (actor.typingOriginType == 0x30)
                if (const auto* enemy = dynamic_cast<const EnemyTask*>(&actor)) actor.typingWordStyle = enemy->hp;
            if (actor.typingFlags != 0 && typing.actorWordOverridesEnabled()) {
                if (actor.typingWordStyle == 1) actor.typingFlags |= 0x14u;
                if (actor.typingWordStyle == 2) actor.typingFlags |= 0x24u;
            }
            target.projectile = (actor.typingFlags & 0x10u) != 0;
            target.flags = actor.typingFlags;
            target.forcedWord = spec.word;
            if (cached != typingCachedTargets.end() && cached->second.first.token != target.token) {
                typingCachedTargets.erase(cached);
                cached = typingCachedTargets.end();
            }
            if (cached == typingCachedTargets.end()) {
                if (typingCachedTargets.size() >= 14) continue;
                cached = typingCachedTargets.emplace(serial, std::make_pair(target, spec)).first;
            } else cached->second = std::make_pair(target, spec);
            const float x = 320.0f - focal * spec.view[0] / spec.view[2];
            const float y = 240.0f + focal * spec.view[1] / spec.view[2];
            typingPositions.emplace(std::make_pair(serial, target.token), std::array<float,2>{x, y});
        } else if (cached != typingCachedTargets.end()) cached->second.first.enabled = false;
        if (cached != typingCachedTargets.end()) snapshot.push_back(cached->second.first);
    }
    // The reference's global frame stamp permits one new word in task order.
    // Hidden entries keep their slots but never allocate their first word.
    typing.syncTargets(snapshot);
    typing.update(1.0 / 60.0, ready);
    if(ready)TypingDrillGameWordPoll(*this);
    if (typing.assignedWordCount() != beforeWords) TypingAttackTimer(2);
    // The actor tasks run after TaskHit and can end a selected damage phase
    // between display and the next input batch. Drop that stale batch instead
    // of charging errors against the newly assigned phase's phrase.
    if (selected != 0 && typing.selectedSerial() != selected) typingCharacters.clear();
    if (cfg.typingOriginal) {
        const auto usable = [&](const auto& entry) {
            return entry.second.second.node >= 0 && !entry.second.second.boss && entry.second.first.enabled;
        };
        const bool hasTarget = std::any_of(typingCachedTargets.begin(),typingCachedTargets.end(),usable);
        for (unsigned slot = 0; slot < 4; ++slot) if (typingItemKeys[typingPlayer] & (1u << slot)) {
            const auto item = typing.original().use(slot,hasTarget);
            if (item == TypingOriginalItem::GoldTranquilizer) {
                typingSavedRank = rank; typingRankClears += 2; rank = std::max(0,rank-6);
            } else if (item == TypingOriginalItem::MolotovCocktail || item == TypingOriginalItem::SulphuricAcid) {
                for (const auto& entry : typingCachedTargets) {
                    if (!usable(entry)) continue;
                    const auto actor = bySerial.find(entry.first);
                    TypingTargetSpec spec;
                    if (actor != bySerial.end() && Describe(*this,*actor->second,spec,true)) {
                        if (item == TypingOriginalItem::SulphuricAcid) break;
                    }
                }
                typing.cancelSelection(); typingCharacters.clear();
            }
        }
    }
    typingItemKeys[typingPlayer] = 0;

    auto deliverBatch = [&](std::string_view text) {
        // Completion removes its prompt from the controller. Preserve the
        // authored word width for40A150's glass grid before accepting input.
        const auto priorPrompts=typingReferenceActive?typing.prompts():std::vector<TypingPrompt>{};
        const auto completed = typing.typeCharacters(text, true);
        if (typing.lastMistypeSerial()) {
            TypingDrillGameMistype(*this,typing.lastMistypeSerial());
            TypingBoss5Mistype(*this, typing.lastMistypeSerial(), typingPlayer);
            TypingWordMistakeHud(*this, typing.lastMistypeSerial());
        }
        for (const TypingCompletion& done : completed) {
            const auto found = bySerial.find(done.serial);
            if (found == bySerial.end()) continue;
            ActorTask& actor = *found->second;
            TypingTargetSpec current;
            if (!Describe(*this, actor, current, false) || Token(*this, actor, current) != done.token) continue;
            TypingDrillGameWordComplete(*this,done);
            TypingWordCompletionAudio(*this, actor.typingFlags, typingPlayer);
            TypingAttackTimer(2);  // original completed-word paths 0x004B1E60
            if (current.retain && current.node < 0) {
                typingPendingBoss[done.serial] = done.token;
            } else if (!Describe(*this, actor, current, true)) {
                continue;
            }
            if (actor.typingPickupItem != 0) {
                TypingOriginalPickup(*this,typingPlayer,actor.typingPickupItem);
                actor.typingPickupItem = 0;
            }
            AddScore(typingPlayer, static_cast<int>(std::min<uint64_t>(done.points, 100000)));
            constexpr unsigned rankProgress[] = {40,30,20,20,20};
            const unsigned grade = static_cast<unsigned>(done.rank);
            if (grade < 5) {
                typingGradeProgress += rankProgress[grade];
                if (typingGradeProgress >= 100) {
                    rank = std::min(15, rank+1);
                    typingGradeProgress = 0;
                }
            }
            if (done.lifeReward) {
                const auto reward = TypingAddLife(player[typingPlayer].lives);
                player[typingPlayer].lives = reward.lives;
                if (reward.basePoints) typing.addBasePoints(reward.basePoints);
            }
            const auto prompt=std::find_if(priorPrompts.begin(),priorPrompts.end(),[&](const auto& candidate) {
                return candidate.serial==done.serial && candidate.token==done.token;
            });
            TypingWordCompleteHud(*this,done,prompt==priorPrompts.end()?std::string_view{}:std::string_view(prompt->text));
        }
    };
    auto deliver=[&](std::string_view text) {
        if(!TypingDrillGameActive(*this)){deliverBatch(text);return;}
        for(size_t i=0;i<text.size()&&TypingDrillGameRunning(*this);++i) {
            deliverBatch(text.substr(i,1));
            if(typing.lastMistypeSerial())break;
        }
    };
    size_t first = 0;
    for (size_t i = 0; i < typingCharacters.size(); ++i) {
        if (typingCharacters[i] != '\x1b') continue;
        deliver(std::string_view(typingCharacters).substr(first, i - first));
        TypingWordCancelAudio(*this, typing.selectedSerial() != 0);
        typing.cancelSelection();
        first = i + 1;
    }
    deliver(std::string_view(typingCharacters).substr(first));
    typingCharacters.clear();
    TypingWordHud(*this);
}

bool World::PortTypingActive() const { return impl_->cfg.portTyping && impl_->gameMode == 6; }

bool World::StartTypingAttract(int demoIndex, std::string* error) {
    // English PC demo table59390C: authored stage/scene pairs,1536 ticks each.
    static constexpr int stages[] = {0,1,3}, scenes[] = {4,28,5};
    static constexpr uint32_t music[] = {0x10000001u,0x10000000u,0x10000010u};
    const int index = ((demoIndex%3)+3)%3;
    if (!impl_->typingReference.Available()) {
        if (error) *error = "The prepared Typing bank is required for its attraction preview.";
        return false;
    }
    if (!impl_->LoadStageTable(stages[index],error,0,1)) return false;
    WorldConfig c;
    c.eventMode=stages[index]; c.scene=scenes[index]; c.gameMode=6;
    c.portTyping=true; c.typingOriginal=false; c.playerState[0]=0; c.playerState[1]=9;
    c.freePlay=true; c.creditSetting=-1; c.livesSetting=5;
    c.seedCounter=0; c.demoIndex=index;
    impl_->frontEnd=false; impl_->typingAttractDemo=index;
    impl_->StartStage(c);
    impl_->rank=7; impl_->rankTimerOn=false;
    impl_->sounds.clear(); impl_->Sound(music[index]|kTypingSoundFlag);
    return true;
}
bool World::PortTypingNameActive() const { return TypingReportNameActive(*impl_)||TypingDrillNameActive(*impl_); }
bool World::PortTypingReportActive() const { return TypingReportActive(*impl_); }
bool World::PortTypingDrillActive() const { return TypingDrillInputActive(*impl_); }

void World::PortTypingInput(const std::string& text, bool cancel, int player) {
    if (player < 0 || player > 1) return;
    if (player == 0 && QueueTypingDrillText(*impl_,text,cancel)) return;
    if (player == 1) impl_->SwapTypingPlayer();
    struct Restore { Impl* w; int player; ~Restore() { if (player == 1) w->SwapTypingPlayer(); } } restore{impl_.get(),player};
    if (player == 0 && QueueTypingReportText(*impl_, text)) return;
    if (PortTypingActive() && (cancel || !text.empty())) impl_->typingChallenge.keyActivity = true;
    if (cancel) {
        impl_->typingCharacters.clear();
        impl_->typingCancel = true;
        return;
    }
    if (!Ready(*impl_)) {
        impl_->typingCharacters.clear();
        return;
    }
    if (impl_->typing.selectedSerial() != 0 && text.find('\x1b') == std::string::npos) {
        const auto shown = PortTypingPrompts(player);
        const bool selectionShown = std::any_of(shown.begin(), shown.end(), [&](const auto& prompt) {
            return prompt.serial == impl_->typing.selectedSerial() && prompt.selected && !prompt.waiting;
        });
        if (!selectionShown) {
            // Actor tasks can hide a selected word after the input snapshot.
            // A key aimed at another visible word must not become a mistake
            // if that earlier selection reappears during the next update.
            impl_->typingCharacters.clear();
            return;
        }
    }
    constexpr size_t kMaxQueued = 256;
    const size_t room = kMaxQueued - std::min(kMaxQueued, impl_->typingCharacters.size());
    impl_->typingCharacters.append(text, 0, std::min(room, text.size()));
}

std::vector<PortTypingPrompt> World::PortTypingPrompts(int player) const {
    std::vector<PortTypingPrompt> result;
    if (player < 0 || player > 1 || !PortTypingActive() || impl_->player[player].state != 5 || impl_->paused) return result;
    const bool currentPlayer = player == impl_->typingPlayer;
    const auto& controller = impl_->TypingController(player);
    const auto& positions = currentPlayer ? impl_->typingPositions : impl_->typingPositions2;
    const auto& pendingBoss = currentPlayer ? impl_->typingPendingBoss : impl_->typingPendingBoss2;
    std::map<uint32_t, ActorTask*> live;
    for (Task* t : impl_->ts.Live()) {
        if (t->alive && t->serial && t->data)
            if (auto* actor = dynamic_cast<ActorTask*>(t->data.get())) live.emplace(t->serial, actor);
    }
    const auto current = [&](uint32_t serial, uint32_t token) {
        const auto task = live.find(serial);
        TypingTargetSpec spec;
        return task != live.end() && Describe(*impl_, *task->second, spec, false) && Token(*impl_, *task->second, spec) == token;
    };
    for (const TypingPrompt& p : controller.prompts()) {
        if (!current(p.serial, p.token)) continue;
        const auto at = positions.find({p.serial, p.token});
        if (at == positions.end()) continue;
        PortTypingPrompt view;
        static_cast<TypingPrompt&>(view) = p;
        view.x = at->second[0];
        view.y = at->second[1];
        result.push_back(std::move(view));
    }
    for (const auto& pending : pendingBoss) {
        if (!current(pending.first, pending.second)) continue;
        const auto at = positions.find({pending.first, pending.second});
        if (at == positions.end()) continue;
        PortTypingPrompt view;
        view.serial = pending.first;
        view.token = pending.second;
        view.text = "READY - WAITING FOR OPENING";
        view.boss = true;
        view.waiting = true;
        view.x = at->second[0];
        view.y = at->second[1];
        result.push_back(std::move(view));
    }
    return result;
}

TypingStats World::PortTypingStats(int player) const { return impl_->TypingController(std::clamp(player,0,1)).stats(); }
std::string World::PortTypingQuestion() const { return impl_->typingQuestion; }
float World::PortTypingQuestionSeconds() const { return impl_->typingQuestionSeconds; }
TypingRulesOptions World::PortTypingOptions() const { return impl_->cfg.typingOptions; }

namespace {
struct TypingResultTask : SpawnTask {
    int phase = 0, remaining = 0;
    float alpha = 0;
};
}

bool World::Impl::SpawnTypingFlow(Task& task, SpawnTask& spawn) {
    if (!cfg.portTyping || !typingReferenceActive) return false;
    const SpawnRecord record = spawn.rec;
    if (record.type == 0x64) {
        const int state = spawn.op == 0x0A ? static_cast<int16_t>(record.flags) : static_cast<int16_t>(record.state);
        int& branch = runner->camera.routeBranch;
        if (state == 0) branch = static_cast<int16_t>(rescueTotal) == static_cast<int16_t>(civTotal) ? 1 : 0;
        else if (state == 1) branch = 0;
        else if (state == 2) {
            branch = typing.stats().points < 100000 ? 1 : 0;
        }
        ts.KillCurrent();
        return true;
    }
    if (record.type == 0x65) {
        // 0x00423A20 returns normally: unlike HOD2's same-numbered actor it
        // remains live and recomputes the weighted ending branch every frame.
        spawn.fn = 0x00423A20;
        task.fn = WorldTramp<&Impl::TaskTypingFlow>;
        TaskTypingFlow(task);
        return true;
    }
    if (record.type == 0x61) {
        auto result = std::make_unique<TypingResultTask>();
        result->rec = record;
        result->op = spawn.op;
        result->fn = 0x00492860;
        const int chapter = std::clamp(cfg.eventMode, 0, 9);
        typing.finishChapter(static_cast<TypingDifficulty>(std::clamp(cfg.difficulty, 0, 4)),
                             static_cast<uint32_t>(std::max<int>(0, rescueCount[chapter])));
        if (player[1].state != 9) typing2.finishChapter(static_cast<TypingDifficulty>(std::clamp(cfg.difficulty,0,4)),
                             static_cast<uint32_t>(std::max<int>(0,rescueCount[chapter])));
        typingResultsActive = true;
        stageResults.shown = true;
        typing.syncTargets({});
        typingCharacters.clear();
        flags5900 |= 0x10;
        attackers = 2;  // Original result initializer491F56 writes shared counterA34D26.
        Sound(0x80000000u);
        task.data = std::move(result);
        task.fn = WorldTramp<&Impl::TaskTypingFlow>;
        TaskTypingFlow(task);
        return true;
    }
    if (record.type == 0x68 || record.type == 0x69 || record.type == 0x6A || record.type == 0x6B) {
        Note("original typing lesson actor " + std::to_string(record.type) + " requires its lesson mode");
        ts.KillCurrent();
        return true;
    }
    return false;
}

void World::Impl::TaskTypingFlow(Task& task) {
    if (task.As<SpawnTask>().rec.type == 0x65) {
        runner->camera.routeBranch = static_cast<int>(TypingEmperorEndingBranch(typingEmperorAnswerWeight));
        return;
    }
    auto& result = task.As<TypingResultTask>();
    // One-player reference driver492860: fades, category reveals, total,
    // grade presentation, optional ranking entry, then the closing frames.
    // The port's typing mode does not enter the original persistent rankings.
    switch (result.phase) {
    case 0:
        result.alpha += 0.02f;
        if (result.alpha >= 1.0f) {
            result.alpha = 1.0f;
            result.phase = 1;
            result.remaining = 247; // category delays215 + final readiness frame + 31-frame tail
            TypingSound(0x10000003u);
        }
        break;
    case 1:
        if (--result.remaining == 0) { result.phase = 2; result.remaining = 86; }
        break;
    case 2:
        if (--result.remaining == 0) { result.phase = 3; result.remaining = 407; }
        break;
    case 3:
        if (--result.remaining == 0) { result.phase = 4; attackers = 1; }
        break;
    case 4: result.phase = 6; result.remaining = 3; break;
    case 6:
        if (--result.remaining == 0) result.phase = 7;
        break;
    case 7:
        result.alpha -= 0.02f;
        if (result.alpha < 0) { result.alpha = 0; result.phase = 8; }
        break;
    case 8: result.phase = 9; break;
    case 9: result.phase = 10; break;
    case 10:
        flags5900 &= ~0x10u;
        runner->flags[0xFE] = 1;
        attackers = 0;
        typingResultsActive = false;
        hud.menuPanels.clear();
        hud.menuText.clear();
        ts.KillCurrent();
        return;
    default: return;
    }
    DrawTypingChapterResults(*this,result.phase,result.remaining,result.alpha);
}

} // namespace game
