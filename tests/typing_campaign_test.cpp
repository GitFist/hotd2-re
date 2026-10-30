// typing_campaign_test <game dir> [--campaign|--two-player|--human-campaign|--ending|--bosses|--human-bosses|--menu|--lifecycle|--gun|--miss-recovery]
// Real stage scripts, enemies and boss damage windows; all campaign damage comes
// through the same queued text API used by the local keyboard frontend.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "game/exe_image.h"
#include "game/world.h"
#include "game/world_internal.h"
#include "port/play/keyboard.h"
#include "route_planner.h"

namespace {

void Require(bool ok, const char* why, const game::World* world = nullptr) {
    if (ok) return;
    std::fprintf(stderr, "FAIL: %s", why);
    if (world) {
        const auto& h = world->Hud();
        const auto stats = world->PortTypingStats();
        std::fprintf(stderr, " (frame %d mode %d/%d chapter %d scene %d player %d lives %d live %d words %llu errors %llu)",
                     world->Frame(), world->GameMode(), h.subState, world->Events().mode + 1,
                     world->Events().scene, h.player[0].state, h.player[0].lives, world->LiveEnemies(),
                     static_cast<unsigned long long>(stats.completedWords), static_cast<unsigned long long>(stats.errors));
        const auto& event = world->Events();
        const auto& camera = event.camera;
        std::fprintf(stderr, "\n  event ip %08X op %02X yield %d wait %d halted %d error [%s] attackers %d"
                             " camera %X tick %d/%d remaining %d mode %d/%d state %d cmd %d pending %d settled %d aimed %d",
                     event.script, event.trace.empty() ? 0u : event.trace.back().opcode, event.yield,
                     event.waitCounter, event.halted, event.errorText.c_str(), world->Attackers(),
                     camera.camId, camera.tick, camera.endTick, camera.ticksRemaining,
                     camera.modeCur, camera.subCur, camera.state, static_cast<int>(camera.cmd),
                     camera.pending, camera.settled, camera.aimedOnce);
        for (const auto& e : world->Enemies(true))
            std::fprintf(stderr, "\n  enemy %u kind %X hp %d state %d/%d motion %X tick %d flags %08X/%08X view %.2f %.2f %.2f", e.serial,
                         e.kind, e.hp, e.behaviour, e.sub, e.motion, e.tick, e.flags34, e.flags136C,
                         e.viewPos[0], e.viewPos[1], e.viewPos[2]);
        const auto prompts = world->PortTypingPrompts();
        const auto tasks = world->Tasks();
        const auto targets = world->ShotTargets();
        std::map<uint32_t, std::pair<unsigned, uint32_t>> taskCounts;
        for (const auto& task : tasks) {
            auto& count = taskCounts[task.fn];
            if (count.first++ == 0) count.second = task.serial;
        }
        unsigned shownFunctions = 0;
        for (const auto& [function, count] : taskCounts) {
            if (shownFunctions++ >= 64) break;
            std::fprintf(stderr, "\n  live task fn %08X count %u first serial %u", function, count.first, count.second);
        }
        std::fprintf(stderr, "\n  prompts %zu; native shot targets %zu", prompts.size(), targets.size());
        for (const auto& p : prompts) {
            const auto task = std::find_if(tasks.begin(), tasks.end(), [&](const auto& t) { return t.serial == p.serial; });
            std::fprintf(stderr, "\n  prompt %u token %08X fn %08X progress %zu/%zu selected %d boss %d projectile %d waiting %d text [%s]",
                         p.serial, p.token, task == tasks.end() ? 0u : task->fn, p.progress, p.text.size(),
                         p.selected, p.boss, p.projectile, p.waiting, p.text.c_str());
            for (const auto& t : targets) if (t.serial == p.serial)
                std::fprintf(stderr, "\n    shot target fn %08X flags %08X radius %.2f nodes %zu view %.2f %.2f %.2f",
                             t.fn, t.flags, t.radius, t.nodes.size(), t.view[0], t.view[1], t.view[2]);
        }
        int unprompted = 0;
        for (const auto& t : targets) {
            if (std::any_of(prompts.begin(), prompts.end(), [&](const auto& p) { return p.serial == t.serial; })) continue;
            if (++unprompted > 16) continue;
            std::fprintf(stderr, "\n  unprompted shot target %u fn %08X flags %08X radius %.2f nodes %zu view %.2f %.2f %.2f",
                         t.serial, t.fn, t.flags, t.radius, t.nodes.size(), t.view[0], t.view[1], t.view[2]);
        }
        if (unprompted > 16) std::fprintf(stderr, "\n  ... %d more unprompted shot targets", unprompted - 16);
    }
    std::fputc('\n', stderr);
    std::exit(1);
}

game::WorldConfig Config(bool typing = true) {
    game::WorldConfig c;
    c.eventMode = 0;
    c.scene = 0;
    c.gameMode = 6;
    c.gameType = 0;
    c.portFeatures = true;
    c.portTyping = typing;
    c.playerState[0] = 0;
    c.seedCounter = 12345;
    c.freePlay = true;
    c.livesSetting = 5;
    return c;
}

void Start(game::World& world, const char* dir, const game::WorldConfig& cfg) {
    std::string error;
    Require(world.Load(dir, cfg.eventMode, &error), error.c_str());
    world.Boot(cfg);  // Persistent native mode data; no writable save path.
    Require(world.StartStage(cfg, &error), error.c_str());
}

void Step(game::World& world, uint32_t buttons = 0, uint32_t secondButtons = 0) {
    game::WorldInput keyboard;
    keyboard.held = keyboard.pressed = buttons;
    world.SetInput(0, keyboard);
    game::WorldInput second;
    second.held = second.pressed = secondButtons;
    world.SetInput(1, second);
    world.Step();
}

void NoGunRecord(const game::World& world) {
    for (const auto& p : world.Hud().player)
        Require(p.shots == 0 && !p.fired, "typing never fires or counts a native gunshot", &world);
    const auto record = world.PortLeaderboardRun();
    Require((!record.eligible && record.sequence == 0 && world.Hud().portLeaderboardPublishRequests == 0) ||
            (record.eligible && record.table == 13), "typing records remain isolated from shooting leaderboards", &world);
}

const game::PortTypingPrompt* Chosen(const std::vector<game::PortTypingPrompt>& prompts, const game::World* world = nullptr) {
    const auto selected = std::find_if(prompts.begin(), prompts.end(), [](const auto& p) { return p.selected && !p.waiting; });
    if (selected != prompts.end()) return &*selected;
    const auto projectile = std::find_if(prompts.begin(), prompts.end(), [](const auto& p) { return p.projectile && !p.waiting; });
    if (projectile != prompts.end()) return &*projectile;
    if (world && !world->PortTypingQuestion().empty()) {
        bool quizQuestion=false;
        for (const auto& bank : game::TypingWordBanks()) {
            if (bank.group != 3) continue;
            const auto words = game::TypingWords(bank);
            if (words.empty() || words[0].text != world->PortTypingQuestion()) continue;
            quizQuestion=true;
            for (const auto& prompt : prompts) for (size_t answer = 1; answer <= 8 && answer < words.size(); ++answer)
                if (!prompt.waiting && prompt.text == words[answer].text) return &prompt;
        }
        // PC4B0AC0 allocates one new word per frame. The correct head's
        // answer may arrive after an incorrect one; perfect test input waits
        // for that answer instead of submitting the fallback wrong choice.
        if (quizQuestion) return nullptr;
    }
    const auto available = std::find_if(prompts.begin(), prompts.end(), [](const auto& p) { return !p.waiting; });
    return available != prompts.end() ? &*available : nullptr;
}

struct TypedStep {
    bool boss = false;
    uint64_t completed = 0;
};

TypedStep TypeStep(game::World& world, int frame, bool continueAllowed = true, bool humanRate = false,
                   int player = 0, bool twoPlayer = false) {
    const auto prompts = world.PortTypingPrompts(player);
    const auto* prompt = Chosen(prompts, &world);
    const auto before = world.PortTypingStats(player);
    const auto question = world.PortTypingQuestion();
    const bool submit = prompt && (!humanRate || frame % 6 == 0);
    TypedStep result;
    if (prompt) {
        Require(!prompt->text.empty() && prompt->progress < prompt->text.size() && prompt->serial != 0,
                "live prompt has a bounded unfinished word and real target identity", &world);
        Require(std::isfinite(prompt->x) && std::isfinite(prompt->y), "prompt projection stays finite", &world);
        result.boss = prompt->boss;
        if (submit) world.PortTypingInput(prompt->text.substr(prompt->progress, humanRate ? 1 : std::string::npos),false,player);
    }
    const auto& h = world.Hud();
    const bool typingName = world.PortTypingNameActive();
    const bool continuing = continueAllowed && h.player[0].state != 5 &&
                            (h.subState == 3 || h.subState == 4 || h.player[0].state == 4);
    const bool confirmResults = h.stageResults.active && !h.stageResults.dismiss &&
                                h.stageResults.frames >= game::StageResults::kMinimumFrames;
    unsigned pendingContinues = 0;
    bool typingReport = false;
    bool typingChapterChoice = false;
    bool typingProfile=false,typingScoreBook=false;
    for (const auto& task : world.Tasks())
        if (task.fn == game::fn::kPlayerRejoin) ++pendingContinues;
        else if (task.fn == 0x0047DFC0) typingReport = true;
        else if (task.fn == 0x00493C60) typingChapterChoice = true;
        else if (task.fn == 0x004888F0) typingProfile=true;
        else if (task.fn == 0x004A14C0) typingScoreBook=true;
    if (frame%8==0) {
        if (typingProfile) world.PortTypingInput("TEST\t20\tAGENT\tDONE\t");
        else if (typingScoreBook) world.PortTypingInput("\x1b");
        else if (typingName) world.PortTypingInput("TEST");
    }
    const uint32_t nameButtons=typingName && frame%8==0 ? (typingProfile || frame%16!=0 ? 8u : 0x10u) : 0u;
    const bool secondContinuing = twoPlayer && continueAllowed && h.player[1].state != 5 &&
                                  (h.subState == 3 || h.subState == 4 || h.player[1].state == 4);
    Step(world, nameButtons ? nameButtons : confirmResults || ((continuing || typingReport || typingChapterChoice) && frame % 8 == 0) ? 8 : 0,
         secondContinuing && frame % 8 == 0 ? 8 : 0);
    const auto after = world.PortTypingStats(player);
    if (humanRate)
        Require(after.correctCharacters >= before.correctCharacters &&
                after.correctCharacters - before.correctCharacters <= (submit ? 1u : 0u),
                "human-rate campaign consumes at most one character every six native frames", &world);
    if (after.errors > before.errors) {
        std::fprintf(stderr, "typing error at frame %d scene %d: serial %u progress %zu text [%s] question [%s], errors %llu -> %llu\n",
                     world.Frame(), world.Events().scene, prompt ? prompt->serial : 0u,
                     prompt ? prompt->progress : 0u, prompt ? prompt->text.c_str() : "", question.c_str(),
                     static_cast<unsigned long long>(before.errors), static_cast<unsigned long long>(after.errors));
        for (const auto& op : world.EventOps())
            std::fprintf(stderr, "  transition event %08X op %02X\n", op.addr, op.opcode);
        for (const auto& p : world.PortTypingPrompts(player))
            std::fprintf(stderr, "  current prompt %u token %08X progress %zu selected %d text [%s]\n",
                         p.serial, p.token, p.progress, p.selected, p.text.c_str());
    }
    const uint64_t continueCost = uint64_t(pendingContinues) * 150;
    const bool scorePreserved = after.points >= before.points ||
                                (continueAllowed && before.points-after.points <= continueCost);
    if (after.correctCharacters < before.correctCharacters || after.completedWords < before.completedWords ||
        !scorePreserved || after.activeSeconds < before.activeSeconds)
        std::fprintf(stderr, "typing stats changed: characters %llu -> %llu words %llu -> %llu points %llu -> %llu seconds %.9f -> %.9f\n",
                     static_cast<unsigned long long>(before.correctCharacters), static_cast<unsigned long long>(after.correctCharacters),
                     static_cast<unsigned long long>(before.completedWords), static_cast<unsigned long long>(after.completedWords),
                     static_cast<unsigned long long>(before.points), static_cast<unsigned long long>(after.points),
                     before.activeSeconds, after.activeSeconds);
    Require(after.correctCharacters >= before.correctCharacters && after.completedWords >= before.completedWords &&
            scorePreserved && after.activeSeconds >= before.activeSeconds,
            "native transitions preserve typing statistics and apply only the authored continue cost", &world);
    result.completed = after.completedWords - before.completedWords;
    NoGunRecord(world);
    return result;
}

constexpr uint32_t kBossTask[6] = {0x0049B620, 0x00476150, 0x004209B0,
                                    0x004919D0, 0x0047C960, 0x00426C30};

void StartBoss(game::World& world, const char* dir, const game::ExeImage& exe, int selector) {
    auto cfg = Config();
    cfg.scene = 0x10;
    cfg.gameType = 3;
    Start(world, dir, cfg);
    const auto card = [&](int index) {
        const int model = static_cast<int>(exe.U32(0x0055CC0C + uint32_t(index) * 4));
        return std::any_of(world.Hud().sprites.begin(), world.Hud().sprites.end(), [&](const auto& s) {
            return s.id == model && s.x == 321.0f && s.y == 100.0f;
        });
    };
    for (int frame = 0; frame < 2000 && (!card(0) || world.Hud().player[0].state != 5); ++frame) Step(world);
    Require(card(0) && world.Hud().player[0].state == 5, "native boss selector opens with its joined player", &world);
    // Selecting through the actual task initializes bossIndex, chosen grade,
    // fight timer, rank, and BossGameSetup exactly as the player-facing flow.
    for (int index = 1; index <= selector; ++index) {
        Step(world, exe.U32(0x00589158));
        for (int frame = 0; frame < 12; ++frame) Step(world);
        Require(card(index), "native boss carousel selects the requested authored fight", &world);
    }
    Step(world, exe.U32(0x00589168));
    const uint32_t address = 0x0055CBBC + uint32_t(selector) * 4;
    Require(world.Events().mode == exe.S16(address) && world.Events().scene == exe.S16(address + 2),
            "native boss confirmation enters the original selector's chapter and scene", &world);
    Require(world.PortTypingActive(), "native boss confirmation preserves typing mode", &world);
    NoGunRecord(world);
}

bool BossStageFinished(const game::World& world) {
    const auto stageEnd = world.Events().globals.find(0x009C71C0);
    const int sub = world.Hud().subState;
    // StageEnd is called by the actual event script after the fight. A dead
    // boss's corpse may keep its task until the later return to the selector.
    return stageEnd != world.Events().globals.end() && stageEnd->second == 1 && world.LiveEnemies() == 0 &&
           world.GameMode() == 6 && (sub == 5 || sub == 6 || sub == 1 || sub == 9);
}

struct ChapterEvidence {
    uint64_t ordinaryWords = 0, bossWords = 0;
    std::set<uint32_t> bossSerials, livingEnemies, deadEnemies;
    bool resultShown = false, resultDismissed = false;
};

void Observe(game::World& world, ChapterEvidence& evidence, int chapter) {
    const auto& result = world.Hud().stageResults;
    if (result.active && result.chapter == chapter + 1) {
        evidence.resultShown = true;
        evidence.resultDismissed |= result.dismiss;
    }
    const auto tasks=world.Tasks();
    const bool sourceResult = std::any_of(tasks.begin(),tasks.end(),[](const auto& task) {
        return task.fn==0x00492860;
    }) && std::any_of(world.Hud().sprites.begin(),world.Hud().sprites.end(),[](const auto& sprite) {
        return sprite.source==0x00495010 && sprite.id==0x4240 && sprite.alpha>0;
    });
    if (sourceResult) evidence.resultShown = true;
    else if (evidence.resultShown && !result.active) evidence.resultDismissed = true;
    for (const auto& task : world.Tasks())
        if (task.fn == kBossTask[chapter]) evidence.bossSerials.insert(task.serial);
    for (const auto& e : world.Enemies()) {
        if (e.hp > 0) evidence.livingEnemies.insert(e.serial);
        else if (evidence.livingEnemies.count(e.serial)) evidence.deadEnemies.insert(e.serial);
    }
}

void CheckChapter(const ChapterEvidence& e, int chapter, const game::World& world) {
    Require(e.ordinaryWords > 0, "every chapter clears actual ordinary typing targets", &world);
    Require(e.bossWords > 0 && !e.bossSerials.empty(), "every chapter types against its actual native boss", &world);
    Require(!e.deadEnemies.empty(), "typed campaign damage reaches an actual native enemy death", &world);
    // The PC scripts post actor61 after chapters1..5. Chapter6 goes directly
    // to the ending; it has no authored chapter-results actor or confirmation.
    if (chapter < 5)
        Require(e.resultShown && e.resultDismissed, "authored chapter results page appears and finishes", &world);
    std::printf("chapter %d: %llu ordinary words, %llu boss phrases, %zu enemy deaths\n", chapter + 1,
                static_cast<unsigned long long>(e.ordinaryWords), static_cast<unsigned long long>(e.bossWords),
                e.deadEnemies.size());
}

int SourceEnding(game::World& world, bool twoPlayer = false) {
    const auto completed = world.PortTypingStats();
    const auto secondCompleted = world.PortTypingStats(1);
    std::set<int> scenes;
    bool sourceCamera = false, sourceNoop = false;
    for (int frame = 0; frame < 30000; ++frame) {
        if (world.GameMode() == 3) {
            Require(world.GameMode() == 3 && scenes.size() > 1 && sourceCamera && sourceNoop,
                    "authored source ending advances through its scenes and returns to title without gun ranking", &world);
            NoGunRecord(world);
            return frame;
        }
        Require(world.GameMode()==6 && world.Hud().subState>=8 && world.Hud().subState<=10,
                "source ending proceeds through its report flow before returning to title",&world);
        const auto& event = world.Events();
        if (world.Hud().subState==8)
            Require(event.mode == 11 && !event.halted && event.errorText.empty(),
                    "source ending runs in its original VM mode rather than the common halt", &world);
        scenes.insert(event.scene);
        sourceCamera |= event.camera.camId >= 0x4000;
        for (const auto& task : world.Tasks()) sourceNoop |= task.fn == 0x004BD820;
        const auto stats = world.PortTypingStats();
        Require(stats.points == completed.points && stats.correctCharacters == completed.correctCharacters &&
                stats.errors == completed.errors && stats.completedWords == completed.completedWords,
                "the ending preserves the finalized chapter-six typing score and totals", &world);
        if (twoPlayer) {
            const auto second = world.PortTypingStats(1);
            Require(second.points == secondCompleted.points && second.correctCharacters == secondCompleted.correctCharacters &&
                    second.errors == secondCompleted.errors && second.completedWords == secondCompleted.completedWords,
                    "the ending preserves P2's independent finalized typing totals", &world);
        }
        NoGunRecord(world);
        const bool name=world.PortTypingNameActive();
        bool report=false,profile=false,scoreBook=false;
        for (const auto& task:world.Tasks()) {
            report|=task.fn==0x0047DFC0;
            profile|=task.fn==0x004888F0;
            scoreBook|=task.fn==0x004A14C0;
        }
        if (frame%8==0) {
            if (profile) world.PortTypingInput("TEST\t20\tAGENT\tDONE\t");
            else if (scoreBook) world.PortTypingInput("\x1b");
            else if (name) world.PortTypingInput("TEST");
        }
        const uint32_t buttons=name && frame%8==0 ? (profile || frame%16!=0 ? 8u : 0x10u) :
                               report && frame%8==0 ? 8u : 0u;
        Step(world,buttons);
    }
    Require(false, "authored source ending completes within its bounded camera and dialogue sequence", &world);
    return 0;
}

void Campaign(const char* dir, int firstChapter = 0, bool humanRate = false, bool twoPlayer = false) {
    game::World world;
    auto cfg = Config();
    cfg.eventMode = firstChapter;
    if (twoPlayer) cfg.playerState[1] = 0;
    cfg.portStageResults = true;
    Start(world, dir, cfg);
    Require(world.PortTypingActive(), "native typing campaign is active after stage start", &world);
    std::array<ChapterEvidence, 6> chapters;
    std::array<uint64_t,2> playerWords{}, playerBossWords{};
    int chapter = firstChapter;
    const int frameLimit = humanRate ? 300000 : 150000;
    for (int frame = 0; frame < frameLimit; ++frame) {
        const int mode = world.Events().mode;
        if ((mode == 9 || mode == 11) && world.Hud().subState == 8) {
            Require(chapter == 5, "the completed sixth chapter loads the authored ending", &world);
            CheckChapter(chapters[5], 5, world);
            const auto stats = world.PortTypingStats();
            Require(stats.correctCharacters > 0 && stats.completedWords > 0 && stats.points > 0 &&
                    stats.activeSeconds > 0 && std::isfinite(stats.wpm()) && stats.wpm() > 0,
                    "completed campaign retains useful typing score, accuracy and speed", &world);
            Require(stats.errors == 0 && stats.accuracy() == 100.0,
                    "a perfect typist is not charged errors at damage-window or chapter boundaries", &world);
            if (twoPlayer) {
                const auto second=world.PortTypingStats(1);
                Require(playerWords[0]>0&&playerWords[1]>0&&playerBossWords[0]>0&&playerBossWords[1]>0&&
                        second.completedWords>0&&second.correctCharacters>0&&second.points>0&&second.errors==0&&
                        second.accuracy()==100.0&&std::isfinite(second.wpm())&&second.wpm()>0,
                        "both cooperative players finish ordinary words and boss phrases with independent score and accuracy",&world);
                std::printf("co-op totals: P1 %llu words (%llu boss), P2 %llu words (%llu boss)\n",
                            static_cast<unsigned long long>(playerWords[0]),static_cast<unsigned long long>(playerBossWords[0]),
                            static_cast<unsigned long long>(playerWords[1]),static_cast<unsigned long long>(playerBossWords[1]));
            }
            NoGunRecord(world);
            const int endingFrames = mode == 11 ? SourceEnding(world,twoPlayer) : 0;
            std::printf("PASS %s%styping campaign chapters %d..6: native bosses, actual ending, %llu words in %d frames (+%d ending)\n",
                        twoPlayer ? "two-player " : "",humanRate ? "human-rate " : "", firstChapter + 1,
                        static_cast<unsigned long long>(stats.completedWords), frame, endingFrames);
            return;
        }
        Require(world.GameMode() == 6 && mode >= 0 && mode < 6,
                "perfect typist remains in the six-chapter campaign until its ending", &world);
        if (mode != chapter) {
            Require(mode == chapter + 1, "campaign advances exactly one native chapter after boss defeat", &world);
            CheckChapter(chapters[chapter], chapter, world);
            chapter = mode;
        }
        Observe(world, chapters[chapter], chapter);
        const int player=twoPlayer ? (frame&1) : 0;
        const auto typed = TypeStep(world, frame, true, humanRate,player,twoPlayer);
        playerWords[player]+=typed.completed;
        if (typed.boss) playerBossWords[player]+=typed.completed;
        (typed.boss ? chapters[chapter].bossWords : chapters[chapter].ordinaryWords) += typed.completed;
    }
    Require(false, "perfect typist completes all six chapters within the bounded campaign", &world);
}

void AxeMissRecovery(const char* dir) {
    game::World world;
    auto cfg=Config();
    Start(world,dir,cfg);
    std::set<uint32_t> axePair;
    int missedAt=-1, resumedAt=-1, livesBefore=0;
    bool wrongKey=false;
    for (int frame=0;frame<35000;++frame) {
        const auto enemies=world.Enemies();
        if (world.Events().scene==4) {
            for (const auto& enemy:enemies)
                if (enemy.kind==0x13 && enemy.hp>0) axePair.insert(enemy.serial);
            if (axePair.size()>=2 && missedAt<0) {
                missedAt=frame;
                livesBefore=world.Hud().player[0].lives;
            }
        }
        if (missedAt>=0) {
            bool alive=false;
            for (const auto& enemy:enemies) if (axePair.count(enemy.serial) && enemy.hp>0) {
                alive=true;
            }
            if (!alive && axePair.size()>=2 && resumedAt>=0) {
                Require(world.Hud().player[0].lives<livesBefore,"missed axe volley reaches actual player damage",&world);
                Require(wrongKey && world.PortTypingStats().errors>0,"axe recovery includes a real partial word and typing mistake",&world);
                for (int tick=0;tick<1800 && world.Events().camera.camId==0x403A && world.Events().camera.tick<=0x5A;++tick)
                    TypeStep(world,frame+tick);
                Require(world.Events().camera.camId!=0x403A || world.Events().camera.tick>0x5A,
                        "defeated axe throwers release the original route gate",&world);
                std::puts("PASS missed axe volley recovers and the chapter route advances");
                return;
            }
            if (resumedAt<0 && (!alive || frame-missedAt>=1800)) {
                resumedAt=frame;
            }
            Require(world.Hud().player[0].state==5,"missed axe volley leaves the player alive to recover",&world);
            Require(resumedAt<0 || frame-resumedAt<3000,"axe encounter clears after typing resumes instead of leaving standing enemies",&world);
            if (resumedAt<0) {
                if (!wrongKey) {
                    const auto prompts=world.PortTypingPrompts();
                    const auto prompt=std::find_if(prompts.begin(),prompts.end(),[&](const auto& p) {
                        return axePair.count(p.serial)!=0 && !p.waiting && p.text.size()>1;
                    });
                    if (prompt!=prompts.end()) {
                        std::string input=prompt->text.substr(0,1);
                        input+=(prompt->text[1]=='a' || prompt->text[1]=='A')?'b':'a';
                        world.PortTypingInput(input);
                        wrongKey=true;
                    }
                }
                Step(world);
                continue;
            }
        }
        TypeStep(world,frame);
    }
    Require(false,"chapter one reaches and recovers from its authored axe-pair encounter",&world);
}

void AxeContinueRecovery(const char* dir) {
    game::World world;
    auto cfg=Config();
    cfg.livesSetting=3;
    cfg.typingOptions.startingLives=3;
    Start(world,dir,cfg);
    bool missing=false,continued=false;
    int continueFrames=0;
    for (int frame=0;frame<18000;++frame) {
        if (!missing && world.Events().camera.camId==0x403A) missing=true;
        if (!missing) { TypeStep(world,frame); continue; }
        const auto& hud=world.Hud();
        if (hud.player[0].state==4 || continueFrames) {
            ++continueFrames;
            if (continueFrames<=30) { Step(world); continue; }
            if (continueFrames==31)
                Require(std::any_of(hud.sprites.begin(),hud.sprites.end(),[](const auto& sprite) { return sprite.id==0x4242; }),
                        "default-life axe failure displays the source Continue prompt instead of an unexplained frozen encounter",&world);
            const bool inGame=world.GameMode()==6 && !hud.stageResults.active &&
                              !world.PortTypingReportActive() && (hud.flags5900&0x18)==0;
            const bool typingGameplay=play::TypingGameplayKeyboard(world.PortTypingActive(),world.PortTypingReportActive(),hud.subState,hud.stageResults.shown);
            const auto enter=play::WorldKeyboardBits(8,inGame,hud.player[0].state==5,typingGameplay);
            Step(world,continueFrames%4==1?enter:0);
            if (world.Hud().player[0].state==5 && world.Hud().player[0].lives>0) { continued=true; break; }
            Require(continueFrames<120,"real keyboard Enter continues after missed axes exhaust the default lives",&world);
        } else Step(world);
    }
    Require(continued,"missed axes reach continue and the actual keyboard mapping restarts play",&world);
    std::puts("PASS default-life axe failure accepts keyboard Enter to continue");
}

void EndingRoute(const char* dir) {
    game::World world;
    auto cfg = Config();
    cfg.eventMode = 5;
    cfg.scene = 13; // Authored chapter-six epilogue after the Emperor fight.
    Start(world, dir, cfg);
    for (int frame = 0; frame < 10000; ++frame) {
        if (world.Events().mode == 11 && world.Hud().subState == 8) {
            const int endingFrames = SourceEnding(world);
            std::printf("PASS source ending: authored chapter-six transition and %d ending frames\n", endingFrames);
            return;
        }
        Require(world.GameMode() == 6 && world.Events().mode == 5 && world.Events().errorText.empty(),
                "authored chapter-six epilogue enters the prepared ending root", &world);
        NoGunRecord(world);
        Step(world);
    }
    Require(false, "authored chapter-six epilogue reaches the ending within its bounded dialogue sequence", &world);
}

void BossVariants(const char* dir) {
    game::ExeImage exe;
    Require(exe.Load((std::filesystem::path(dir) / "Hod2.exe").string()), "read authored boss selector scene table");
    // The full campaign follows one route. These authored boss-mode entries
    // additionally exercise both arenas of Hierophant, Tower and Strength.
    for (int selector : {1, 2, 3, 4, 5, 6}) {
        const uint32_t address = 0x0055CBBC + uint32_t(selector) * 4;
        auto cfg = Config();
        cfg.eventMode = exe.S16(address);
        cfg.scene = exe.S16(address + 2);
        cfg.gameType = 3;
        Require(cfg.eventMode == (selector <= 2 ? 1 : selector <= 4 ? 2 : 3), "boss fixture uses the original selector's chapter");
        game::World world;
        StartBoss(world, dir, exe, selector);
        std::set<uint32_t> bossSerials;
        uint64_t bossWords = 0;
        bool completed = false, joined = false;
        for (int frame = 0; frame < 18000; ++frame) {
            for (const auto& task : world.Tasks()) if (task.fn == kBossTask[cfg.eventMode]) {
                bossSerials.insert(task.serial);
            }
            const auto& hud = world.Hud();
            joined |= hud.player[0].state == 5;
            if (!bossSerials.empty() && BossStageFinished(world)) {
                Require(bossWords > 0, "alternate boss is defeated through typing completions", &world);
                Require(world.PortTypingStats().errors == 0, "perfect typing survives alternate boss phase changes", &world);
                NoGunRecord(world);
                std::printf("PASS typing boss selector %d: chapter %d scene %d, %llu phrases, %d frames\n",
                            selector, cfg.eventMode + 1, cfg.scene, static_cast<unsigned long long>(bossWords), frame);
                completed = true;
                break;
            }
            Require(world.GameMode() == 6 && (!joined || hud.player[0].lives > 0),
                    "alternate boss remains playable with the configured native life count", &world);
            const auto typed = TypeStep(world, frame, false);
            if (typed.boss) bossWords += typed.completed;
        }
        Require(completed, "alternate Hierophant/Tower fight reaches its native stage-end transition", &world);
    }
}

void HumanRateBosses(const char* dir) {
    game::ExeImage exe;
    Require(exe.Load((std::filesystem::path(dir) / "Hod2.exe").string()), "read human-rate boss fixtures");
    uint64_t totalWaitingFrames = 0;
    // Ten characters per second, with a deliberate two-second interruption
    // after the first boss character. Real attack/defense cycles keep running.
    for (int selector : {1, 4, 7}) {
        const uint32_t address = 0x0055CBBC + uint32_t(selector) * 4;
        auto cfg = Config();
        cfg.eventMode = exe.S16(address);
        cfg.scene = exe.S16(address + 2);
        cfg.gameType = 3;
        game::World world;
        StartBoss(world, dir, exe, selector);
        bool sawBoss = false, completed = false, heldPartial = false, joined = false;
        uint64_t bossWords = 0, waitingFrames = 0;
        int holdUntil = -1;
        game::PortTypingPrompt interrupted;
        for (int frame = 0; frame < 30000; ++frame) {
            bool bossAlive = false;
            for (const auto& task : world.Tasks()) if (task.fn == kBossTask[cfg.eventMode]) bossAlive = true;
            sawBoss |= bossAlive;
            const auto& hud = world.Hud();
            joined |= hud.player[0].state == 5;
            if (sawBoss && BossStageFinished(world)) {
                Require(heldPartial && frame >= holdUntil && bossWords > 0,
                        "human-rate boss defeat preserves a partial phrase through the deliberate interruption", &world);
                Require(world.PortTypingStats().errors == 0, "human-rate native phase transitions do not charge typing errors", &world);
                NoGunRecord(world);
                std::printf("PASS human-rate boss selector %d: %llu phrases, %llu waiting frames, %d frames\n",
                            selector, static_cast<unsigned long long>(bossWords),
                            static_cast<unsigned long long>(waitingFrames), frame);
                completed = true;
                break;
            }
            Require(world.GameMode() == 6 && (!joined || hud.player[0].lives > 0),
                    "human-rate typing survives the actual boss fight with five native lives", &world);
            const auto prompts = world.PortTypingPrompts();
            bool waiting = false;
            for (const auto& p : prompts) if (p.waiting) {
                Require(p.boss, "only completed boss phrases wait for native vulnerability", &world);
                waiting = true;
            }
            if (waiting) ++waitingFrames;
            const bool holding = heldPartial && frame < holdUntil;
            if (holding) {
                const auto kept = std::find_if(prompts.begin(), prompts.end(), [&](const auto& p) {
                    return p.serial == interrupted.serial;
                });
                Require(kept != prompts.end() && !kept->waiting && kept->selected && kept->token == interrupted.token &&
                        kept->text == interrupted.text && kept->progress == interrupted.progress,
                        "live boss keeps the selected text, identity and progress while its native cycles run", &world);
            }
            const auto* prompt = Chosen(prompts, &world);
            const auto before = world.PortTypingStats();
            uint32_t livingMagician=0;
            if (selector==7) for (const auto& target:world.ShotTargets())
                if (target.fn==kBossTask[4] && !(target.flags&0x04000000u)) livingMagician=target.serial;
            const bool typed = prompt && (!holding || prompt->projectile) && frame % 6 == 0;
            bool typedBoss = false;
            uint32_t firstBossSerial = 0;
            if (typed) {
                Require(prompt->progress < prompt->text.size(), "human-rate prompt has a next character", &world);
                typedBoss = prompt->boss;
                if (typedBoss && !heldPartial) firstBossSerial = prompt->serial;
                world.PortTypingInput(prompt->text.substr(prompt->progress, 1));
            }
            Step(world);
            const auto after = world.PortTypingStats();
            Require(after.correctCharacters >= before.correctCharacters && after.correctCharacters - before.correctCharacters <= (typed ? 1u : 0u),
                    "human typist submits at most one character every six native frames", &world);
            Require(after.completedWords >= before.completedWords && after.completedWords - before.completedWords <= (typed ? 1u : 0u),
                    "a waiting phrase earns completion once and never repeats without new text", &world);
            if (!typed) {
                bool magicianDefeated=false;
                if (livingMagician) for (const auto& target:world.ShotTargets())
                    if (target.serial==livingMagician && (target.flags&0x04000000u)) magicianDefeated=true;
                // PC46D76B..46D799 awards1300 base points when the native
                // damage callback sets Magician's death bit. A completed
                // phrase can reach that callback on a later, input-free tick.
                const uint64_t nativeAward=magicianDefeated?1300:0;
                Require(after.points == before.points+nativeAward,
                        "waiting cannot duplicate word points and only native Magician defeat awards its1300 bonus", &world);
            }
            if (typedBoss) bossWords += after.completedWords - before.completedWords;
            if (firstBossSerial && after.correctCharacters > before.correctCharacters) {
                const auto afterPrompts = world.PortTypingPrompts();
                const auto selected = std::find_if(afterPrompts.begin(), afterPrompts.end(), [&](const auto& p) {
                    return p.serial == firstBossSerial && p.selected && !p.waiting;
                });
                Require(selected != afterPrompts.end() && selected->progress == 1,
                        "human first character starts a persistent boss phrase", &world);
                interrupted = *selected;
                heldPartial = true;
                holdUntil = frame + 121;
            }
            NoGunRecord(world);
        }
        Require(completed, "human-rate typing defeats the actual Hierophant, Tower or Magician fight", &world);
        totalWaitingFrames += waitingFrames;
    }
    Require(totalWaitingFrames > 0, "human-rate completed phrases actually wait through closed native damage windows");
}

game::PortTypingPrompt FirstPrompt(game::World& world) {
    for (int frame = 0; frame < 4000; ++frame) {
        const auto prompts = world.PortTypingPrompts();
        if (const auto* prompt = Chosen(prompts, &world)) return *prompt;
        Step(world);
    }
    Require(false, "opening encounter exposes a real typing prompt", &world);
    return {};
}

void MenuFlow(const char* dir) {
    game::World world;
    std::string error;
    Require(world.Load(dir, 0, &error), error.c_str());
    game::WorldConfig cfg;
    cfg.portFeatures = cfg.portModernMenu = cfg.portStageResults = true;
    world.Boot(cfg);
    for (int frame = 0; frame < 1200 && world.GameMode() != 3; ++frame) Step(world);
    const auto openHome = [&] {
        Require(world.GameMode() == 3, "native frontend reaches the title", &world);
        for (int frame = 0; frame < 4; ++frame) Step(world);
        Step(world, 8);
        for (int frame = 0; frame < 4; ++frame) Step(world);
        Require(world.GameMode() == 4 && world.Hud().nightShiftPage == 0,
                "native title Start opens Night Shift Home", &world);
    };
    openHome();
    for (int row=0;row<7;++row) {Step(world,0x20);Step(world);}
    Step(world,8);
    for(int frame=0;frame<48;++frame)Step(world);
    Require(world.Hud().typingHome&&world.Hud().nightShiftPage==0,"game switch enters Typing Home",&world);
    Step(world, 8);
    Step(world);
    Require(world.GameMode() == 4 && world.Hud().nightShiftPage == 11,
            "Typing Home Original opens its five-setting modal", &world);
    const auto typingOptions = world.PortTypingOptions();
    Require(typingOptions.startingLives == 3 && typingOptions.continues == 5 &&
            typingOptions.keyboard == game::TypingKeyboard::US, "typing options start at supplied PC defaults", &world);
    for (int row=0;row<5;++row) {Step(world,0x20);Step(world);}
    Step(world, 8);
    bool sawGameStart = world.GameMode() == 6 && world.Hud().subState == 0;
    for (int frame = 0; frame < 4000; ++frame) {
        sawGameStart |= world.GameMode() == 6 && world.Hud().subState == 0;
        if (world.GameMode() == 6 && world.Hud().subState == 2 && !world.PortTypingPrompts().empty()) break;
        Step(world);
    }
    Require(sawGameStart && world.GameMode() == 6 && world.Hud().subState == 2 && world.PortTypingActive(),
            "typing menu choice survives the actual mode-6 GameStartFrame", &world);
    const auto prompt = FirstPrompt(world);
    world.PortTypingInput(prompt.text);
    Step(world);
    Require(world.PortTypingStats().completedWords == 1, "menu-launched typing exposes and completes a real enemy word", &world);
    NoGunRecord(world);

    world.RequestTitle();
    Require(!world.PortTypingActive() && world.PortTypingPrompts().empty(), "return to title ends the menu-launched typing run", &world);
    Step(world);
    Require(world.GameMode()==4&&world.Hud().typingHome&&world.Hud().nightShiftPage==0,
            "return from Typing gameplay restores Typing Home",&world);
    for (int row=0;row<5;++row) {Step(world,0x20);Step(world);}
    Step(world,8);
    for(int frame=0;frame<48;++frame)Step(world);
    Require(!world.Hud().typingHome,"game switch returns to HOTD2 Home",&world);
    Step(world,8);Step(world);
    Require(world.Hud().nightShiftPage==1&&world.Hud().nightShiftRow==0,
            "native Home keyboard confirmation opens Play with Arcade focused",&world);
    Require(std::none_of(world.Hud().menuText.begin(),world.Hud().menuText.end(),[](const auto& text) {
        return text.text=="TYPING OF THE DEAD";
    }),"HOTD2 Play no longer contains Typing",&world);
    Step(world, 8);  // Play's first row is Arcade.
    sawGameStart = world.GameMode() == 6 && world.Hud().subState == 0;
    for (int frame = 0; frame < 4000; ++frame) {
        sawGameStart |= world.GameMode() == 6 && world.Hud().subState == 0;
        if (world.GameMode() == 6 && world.Hud().subState == 2 && !world.Enemies().empty() && world.Hud().canShoot) break;
        Step(world);
    }
    Require(sawGameStart && world.GameMode() == 6 && world.Hud().subState == 2 && !world.Enemies().empty() &&
            !world.PortTypingActive() && world.PortTypingPrompts().empty(),
            "subsequent native Arcade selection starts actual enemies with typing disabled", &world);
    const auto stats = world.PortTypingStats();
    Require(stats.correctCharacters == 0 && stats.errors == 0 && stats.completedWords == 0 && stats.points == 0 &&
            stats.activeSeconds == 0, "Arcade selection clears the completed typing run's state", &world);
    std::puts("PASS typing menu: native boot, game switch, Typing Original modal, actual word, Typing Home return and Arcade reset");
}

void Lifecycle(const char* dir) {
    game::World world;
    const auto cfg = Config();
    Start(world, dir, cfg);
    const auto introScript=world.Events().script;
    const auto introRandom=world.RandState();
    const uint32_t escape=play::WorldKeyboardBits(play::kEscapeKey,false,false,
        play::TypingGameplayKeyboard(world.PortTypingActive(),world.PortTypingReportActive(),
                                     world.Hud().subState,world.Hud().stageResults.shown));
    Step(world,escape);
    Require(world.Hud().paused,"Escape pauses the actual typing intro before its player joins",&world);
    for (int frame=0;frame<8;++frame) Step(world);
    Require(world.Events().script==introScript && world.RandState()==introRandom &&
            world.PortTypingStats().correctCharacters==0,
            "intro pause freezes its authored events and RNG without consuming typing input",&world);
    Step(world,escape);
    Require(!world.Hud().paused,"Escape resumes the actual typing intro",&world);
    auto prompt = FirstPrompt(world);
    world.PortTypingInput(prompt.text.substr(0, 1));
    Step(world);
    auto prompts = world.PortTypingPrompts();
    auto selected = std::find_if(prompts.begin(), prompts.end(), [](const auto& p) { return p.selected; });
    Require(selected != prompts.end() && selected->serial == prompt.serial && selected->progress == 1,
            "queued first character selects its live native enemy", &world);
    world.PortTypingInput("", true);
    Step(world);
    prompts = world.PortTypingPrompts();
    for (const auto& p : prompts)
        Require(!p.selected && p.progress == 0, "cancel releases selection and resets partial progress", &world);

    prompt = FirstPrompt(world);
    world.PortTypingInput(prompt.text.substr(0, 1));
    Step(world);
    Step(world, 8);
    Require(world.Hud().paused, "native Start pauses the typing encounter", &world);
    const auto paused = world.PortTypingStats();
    world.PortTypingInput(prompt.text);
    for (int frame = 0; frame < 8; ++frame) Step(world);
    const auto afterPause = world.PortTypingStats();
    Require(afterPause.correctCharacters == paused.correctCharacters && afterPause.completedWords == paused.completedWords &&
            afterPause.errors == paused.errors && afterPause.activeSeconds == paused.activeSeconds,
            "pause rejects text without advancing progress, accuracy or typing time", &world);
    Step(world, 8);
    Require(!world.Hud().paused, "native Start resumes the typing encounter", &world);
    Require(world.PortTypingStats().completedWords == paused.completedWords &&
            world.PortTypingStats().correctCharacters == paused.correctCharacters,
            "text submitted during pause cannot execute on resume", &world);
    prompts = world.PortTypingPrompts();
    selected = std::find_if(prompts.begin(), prompts.end(), [](const auto& p) { return p.selected; });
    Require(selected != prompts.end() && selected->serial == prompt.serial && selected->progress == 1,
            "pause and resume preserve the selected live word", &world);

    world.PortTypingInput(selected->text.substr(selected->progress));
    world.RequestTitle();
    Require(!world.PortTypingActive() && world.PortTypingPrompts().empty(), "return to title removes live typing targets", &world);
    world.PortTypingInput("stale title text");
    std::string error;
    Require(world.StartStage(cfg, &error), error.c_str());
    const auto fresh = world.PortTypingStats();
    Require(fresh.correctCharacters == 0 && fresh.errors == 0 && fresh.completedWords == 0 && fresh.points == 0 &&
            fresh.activeSeconds == 0, "restart resets all typing statistics and queued text", &world);
    prompt = FirstPrompt(world);
    Require(world.PortTypingStats().correctCharacters == 0 && world.PortTypingStats().completedWords == 0,
            "old run and title text cannot hit an enemy in the restarted campaign", &world);
    world.PortTypingInput(prompt.text);
    Step(world);
    Require(world.PortTypingStats().completedWords == 1, "restarted campaign accepts fresh typing normally", &world);
    NoGunRecord(world);
    std::puts("PASS typing lifecycle: actual enemy selection, cancel, native pause, title and clean restart");
}

void GunRegression(const char* dir) {
    game::World ignored, control;
    const auto cfg = Config(false);
    Start(ignored, dir, cfg);
    Start(control, dir, cfg);
    plan::Planner planner;
    planner.Init();
    std::set<uint32_t> living;
    bool killed = false;
    for (int frame = 0; frame < 5000; ++frame) {
        const uint8_t previous = planner.held;
        plan::Line input{};
        planner.Decide(ignored, frame + 1, &input);
        const auto gun = game::MakeWorldInput(planner.x, planner.y, planner.held, previous);
        ignored.PortTypingInput("ignored words 123!", frame % 2 == 0);
        ignored.SetInput(0, gun);
        control.SetInput(0, gun);
        ignored.Step();
        control.Step();
        Require(!ignored.PortTypingActive() && ignored.PortTypingPrompts().empty(),
                "ordinary gun mode exposes no typing targets", &ignored);
        const auto stats = ignored.PortTypingStats();
        Require(stats.correctCharacters == 0 && stats.errors == 0 && stats.completedWords == 0 && stats.points == 0 &&
                stats.activeSeconds == 0, "ordinary gun mode ignores queued typing and cancel", &ignored);
        const auto& a = ignored.Hud().player[0];
        const auto& b = control.Hud().player[0];
        Require(ignored.RandState() == control.RandState() && ignored.LiveEnemies() == control.LiveEnemies() &&
                ignored.Events().mode == control.Events().mode && ignored.Events().scene == control.Events().scene &&
                a.shots == b.shots && a.hits == b.hits && a.ammo == b.ammo && a.score == b.score && a.lives == b.lives,
                "ignored typing preserves native shooting, score, life, route and RNG behavior", &ignored);
        const auto enemies = ignored.Enemies();
        const auto expected = control.Enemies();
        Require(enemies.size() == expected.size(), "ignored typing preserves the native enemy cast", &ignored);
        for (size_t i = 0; i < enemies.size(); ++i) {
            const auto& e = enemies[i];
            const auto& f = expected[i];
            Require(e.serial == f.serial && e.hp == f.hp && e.behaviour == f.behaviour && e.sub == f.sub &&
                    e.motion == f.motion && e.tick == f.tick && e.pos[0] == f.pos[0] && e.pos[1] == f.pos[1] &&
                    e.pos[2] == f.pos[2], "ignored typing preserves native enemy damage, movement and animation", &ignored);
            if (e.hp > 0) living.insert(e.serial);
            else if (living.count(e.serial)) killed = true;
        }
        if (killed && a.shots > 0 && a.hits > 0) {
            std::printf("PASS ordinary shooting regression: actual gun kill, %d shots, %d hits, identical no-text control\n",
                        a.shots, a.hits);
            return;
        }
    }
    Require(false, "ordinary native gun still fires, hits and kills an actual enemy", &ignored);
}

void Eligibility() {
    const auto introPause=[](bool typing,bool modal) {
        game::World::Impl intro;
        intro.cfg.portTyping=typing;intro.gameMode=6;intro.subState=2;
        intro.flags5900=0x10;intro.typingResultsActive=modal;
        intro.player[0].state=5;intro.pressed=8;
        intro.PauseFrame();
        return intro.paused;
    };
    Require(introPause(true,false),"typing intro cards cannot block Escape pause");
    Require(!introPause(true,true),"typing result modals keep their existing pause exclusion");
    Require(!introPause(false,false),"ordinary shooting retains its original intro-card pause gate");
    const auto ready = [](game::World::Impl& w) {
        w.cfg.portTyping = true;
        w.gameMode = 6;
        w.canShoot = 1;
        w.player[0].state = 5;
        w.focal = 640.0f;
    };
    const auto add = [](game::World::Impl& w, uint32_t fn, uint32_t type, int kind, int p0) {
        auto* actor = w.NewTask<game::ActorTask>(nullptr, fn);
        game::SpawnRecord record;
        record.type = type;
        record.params[0] = static_cast<uint8_t>(p0);
        record.params[1] = static_cast<uint8_t>(kind);
        w.SetTypingSpawnOrigin(*actor, record);
        actor->flags34 = 1;
        actor->radius = 3.0f;
        actor->viewPos[2] = -100.0f;
        w.targets.push_back({actor, {0.0f, 0.0f, 0.0f}});
        return actor;
    };
    game::World::Impl w;
    ready(w);
    // All fixtures are native registered collision targets. The two barrels
    // share the same recipient class/function and differ only in event origin.
    auto* barrel = add(w, 0x0046A580, 0x41, 34, 1);
    auto* direct = add(w, 0x0046A580, 0x44, 34, 1);
    auto* disabled = add(w, 0x0046A580, 0x41, 34, 0);
    auto* scenery = add(w, 0x0046E6E0, 0x41, 49, 1);
    Require(barrel->typingFlags == 0x00400018u && direct->typingFlags == 0 &&
            disabled->typingFlags == 0 && scenery->typingFlags == 0,
            "original origin/P0 gate distinguishes identical barrels and unregistered scenery");
    game::ActorTask metadata;
    game::SpawnRecord enemyRecord;
    enemyRecord.type = 0x30;
    enemyRecord.state = 2;
    enemyRecord.params[1] = 4;
    w.SetTypingSpawnOrigin(metadata, enemyRecord);
    Require(metadata.typingFlags == 0x30001001u && metadata.typingWordStyle == 2,
            "event factories preserve the full default mask, special variant bit and separate common word style");
    w.TypingStep();
    Require(w.typing.prompts().empty(), "scripted prop gate initially hides every optional prop word");
    w.typingPropsEnabled = true;
    w.TypingStep();
    Require(w.typing.prompts().size() == 1,
            "prop window exposes only the explicitly registered barrel, never direct/scenery/P0-zero actors");
    barrel->viewPos[0] = 51.0f;  // 326.4 pixels beyond the authored right edge.
    w.TypingStep();
    Require(w.typing.prompts().empty(), "optional prop words obey the original 640-pixel rectangle");
    barrel->viewPos[0] = 0;
    w.TypingStep();
    Require(w.typing.prompts().size() == 1, "returning authored prop becomes available again");
    w.typingPropsEnabled = false;
    w.TypingStep();
    Require(w.typing.prompts().empty(), "closing the scripted prop window removes its words");

    game::World::Impl propWindow;
    ready(propWindow);
    propWindow.typingReferenceActive = true;
    auto* optionalWord = add(propWindow, 0x00468180, 0x41, 14, 0);
    optionalWord->typingFlags = 0x00400008u;
    propWindow.typingPropsEnabled = true;
    propWindow.TypingStep();
    const auto propPhrase = propWindow.typing.prompts().front();
    Require(propPhrase.text.size() > 1, "forced-cancel fixture exposes a complete prop phrase");
    propWindow.typingCharacters = propPhrase.text.substr(0, 1);
    propWindow.TypingStep();
    Require(propWindow.typing.selectedSerial() != 0, "prop phrase has an active partial selection");
    propWindow.typingPropsEnabled = false;
    propWindow.typingCharacters = "stale";
    propWindow.TypingStep();
    Require(propWindow.typing.selectedSerial() == 0 && propWindow.typingCharacters.empty() &&
            propWindow.typing.stats().errors == 0,
            "closing a selected prop window releases the keyboard and discards its queued input");

    game::World::Impl returning;
    ready(returning);
    auto* returningActor = add(returning, 0x00468180, 0x41, 14, 1);
    returning.TypingStep();
    const auto originalWord = returning.typing.prompts().front();
    Require(originalWord.text.size() > 1, "retention fixture is an original multi-character word");
    returning.typingCharacters = originalWord.text.substr(0, 1);
    returning.TypingStep();
    Require(returning.typing.prompts().front().progress == 1, "native actor's original word is partly selected");
    returningActor->viewPos[0] = 1000;
    returning.TypingStep();
    const auto hiddenErrors = returning.typing.stats().errors;
    returning.typingCharacters = "wrong";
    returning.TypingStep();
    Require(returning.typing.prompts().empty() && returning.typing.stats().errors == hiddenErrors,
            "offscreen native actor retains its word without accepting hidden input");
    returningActor->viewPos[0] = 0;
    returning.TypingStep();
    const auto restoredWord = returning.typing.prompts().front();
    Require(restoredWord.text == originalWord.text && restoredWord.progress == 1 && restoredWord.selected,
            "returning native actor keeps the identical original word and progress");
    returning.ts.Live().front()->alive = false;
    returning.TypingStep();
    Require(returning.typing.prompts().empty() && returning.typingCachedTargets.empty(),
            "ended native actor releases its cached word slot");

    game::World::Impl hiddenWord;
    ready(hiddenWord);
    hiddenWord.typingReferenceActive = true;
    auto* hiddenActor = add(hiddenWord, 0x00468180, 0x41, 14, 1);
    hiddenActor->typingFlags |= 0x400u;
    hiddenWord.TypingStep();
    Require(hiddenWord.typing.prompts().empty() && hiddenWord.typing.assignedWordCount() == 0,
            "original flag400 hides the word before its first allocation");
    hiddenActor->typingFlags &= ~0x400u;
    hiddenWord.TypingStep();
    const auto startedWord = hiddenWord.typing.prompts().front();
    hiddenWord.typingCharacters = startedWord.text.substr(0, 1);
    hiddenWord.TypingStep();
    hiddenActor->typingFlags |= 0x400u;
    hiddenWord.TypingStep();
    hiddenWord.typingCharacters = "wrong";
    hiddenWord.TypingStep();
    Require(hiddenWord.typing.prompts().empty() && hiddenWord.typing.stats().errors == 0,
            "flag400 retains a selected word while rejecting hidden input");
    hiddenActor->typingFlags &= ~0x400u;
    hiddenWord.TypingStep();
    const auto resumedWord = hiddenWord.typing.prompts().front();
    Require(resumedWord.text == startedWord.text && resumedWord.progress == 1 && resumedWord.selected &&
            hiddenWord.typing.assignedWordCount() == 1,
            "clearing flag400 restores the identical word/progress without another allocation");
    hiddenActor->typingFlags |= 0x600u;
    hiddenWord.TypingStep();
    Require(hiddenWord.typing.prompts().size() == 1,
            "original flag200 overrides the flag400 visibility gate");
    for (const uint32_t propStyle : {0x8000u, 0x00400000u}) {
        hiddenActor->typingFlags = 8u | propStyle;
        hiddenWord.typingPropsEnabled = false;
        hiddenWord.TypingStep();
        Require(hiddenWord.typing.prompts().empty(), "unhidden prop-style words require the authored prop gate too");
        hiddenWord.typingPropsEnabled = true;
        hiddenWord.TypingStep();
        Require(hiddenWord.typing.prompts().size() == 1, "opening the prop gate restores either original prop style");
        hiddenActor->viewPos[0] = 51;
        hiddenWord.TypingStep();
        Require(hiddenWord.typing.prompts().empty(), "both prop styles obey the original projection rectangle");
        hiddenActor->viewPos[0] = 0;
        hiddenActor->typingFlags |= 0x200u;
        hiddenWord.typingPropsEnabled = false;
        hiddenWord.TypingStep();
        Require(hiddenWord.typing.prompts().size() == 1, "flag200 explicitly overrides the authored prop gate");
        hiddenActor->typingFlags = 8u | 0x400u | propStyle;
        hiddenWord.typingPropsEnabled = false;
        hiddenWord.TypingStep();
        Require(hiddenWord.typing.prompts().empty(), "hidden prop-style word still requires the authored prop gate");
        hiddenWord.typingPropsEnabled = true;
        hiddenWord.TypingStep();
        Require(hiddenWord.typing.prompts().size() == 1,
                "prop-style exception permits flag400 through the original projection/prop gate");
    }

    game::World::Impl crowd;
    ready(crowd);
    for (int i = 0; i < 16; ++i) add(crowd, 0x00468180, 0x41, 14, 1);
    crowd.TypingStep();
    Require(crowd.typing.prompts().size() == 1, "original task frame stamp assigns one new word per frame");
    for (int i = 1; i < 14; ++i) crowd.TypingStep();
    Require(crowd.typing.prompts().size() == 14, "original fourteen word slots replace the arbitrary eight-target cap");
    const int delay = crowd.typingAttackDelay;
    Require(delay == 390 && !crowd.TypingAttackReady(), "new word starts the original normal-difficulty/rank-zero grace delay");
    game::EnemyTask ordinary;
    ordinary.target = 0;
    Require(crowd.EnemyPickTarget(ordinary) == 0 && ordinary.target == -1,
            "ordinary native target acquisition waits for typing grace without beginning an attack");
    crowd.TypingStep();
    Require(crowd.typingAttackDelay == delay - 1, "existing words do not continually reset the grace counter");
    crowd.TypingAttackTimer(0);
    Require(crowd.TypingAttackReady(), "authored forced attacks clear the original grace gate");
    crowd.TypingAttackTimer(3);
    Require(crowd.typingAttackDelay == 120, "leaper hit reactions enforce the original minimum grace");
    const int firstRank[5] = {570,480,390,360,330};
    const int lastRank[5] = {300,240,150,150,120};
    crowd.cfg.eventMode = 2;
    for (int difficulty = 0; difficulty < 5; ++difficulty) {
        crowd.cfg.difficulty = difficulty;
        crowd.rank = 0;
        crowd.TypingAttackTimer(2);
        Require(crowd.typingAttackDelay == firstRank[difficulty], "five original difficulty grace rows at rank zero");
        crowd.rank = 15;
        crowd.TypingAttackTimer(2);
        Require(crowd.typingAttackDelay == lastRank[difficulty], "five original difficulty grace rows at rank fifteen");
    }
    game::World::Impl flow;
    ready(flow);
    flow.typingReferenceActive = true;
    flow.runner = std::make_unique<game::EventRunner>(flow.mem, flow, &flow.env, &flow.exe);
    auto* cinematic = add(flow, 0x00468180, 0x41, 14, 1);
    flow.runner->camera.modeCur = 1;
    flow.TypingStep();
    Require(flow.typing.prompts().empty(), "event camera suppresses ordinary word actors until gameplay begins");
    cinematic->typingFlags |= 0x200u;
    flow.TypingStep();
    Require(flow.typing.prompts().size() == 1, "explicit original flag200 permits a word during an event camera");
    flow.ts.Reset();
    flow.targets.clear();
    flow.typingCachedTargets.clear();
    flow.typing.syncTargets({});
    flow.runner->camera.modeCur = 0;
    flow.typingCharacters = "previous word";
    flow.TypingWords(0, 100);
    Require(flow.typingCharacters.empty(), "original word-bank command clears queued text for the previous words");
    flow.runner->scene = 8;
    game::SpawnRecord batRecord;
    batRecord.type = 0x40;
    flow.SpawnBatGroup(batRecord, 1);
    Require(flow.ts.Live().size() == 4, "source chapter-one bat groups contain four word actors");
    for (const auto* task : flow.ts.Live()) {
        const auto* actor = dynamic_cast<const game::ActorTask*>(task->data.get());
        Require(actor && actor->typingFlags == 0x30000018u && actor->typingWordStyle == 1,
                "every private bat factory preserves its original one-key word-post flags");
    }
    flow.ts.Reset();
    for (const uint32_t type : {0x54u, 0x55u, 0x56u}) {
        auto* task = flow.ts.Create(nullptr, std::make_unique<game::SpawnTask>());
        auto& spawn = task->As<game::SpawnTask>();
        spawn.rec.type = type;
        Require(flow.SpawnEnding(*task, spawn) && spawn.fn == 0x004BD820,
                "source ending staff/result/THE END actors dispatch to their original no-op");
    }
    flow.ts.RunFrame();
    Require(flow.ts.Live().size() == 3, "source ending no-op actors retain their original task lifetime");
    flow.ts.Reset();
    const auto spawnFlow = [&](uint32_t type, int state) {
        auto* spawn = flow.NewTask<game::SpawnTask>([](game::Task& task) { game::CurrentWorld()->TaskSpawn(task); }, 0);
        spawn->rec.type = type;
        spawn->rec.flags = static_cast<uint32_t>(state);
        spawn->op = 0x0A;
    };
    game::SetCurrentWorld(&flow);
    flow.rescueTotal = flow.civTotal = 3;
    spawnFlow(0x64, 0);
    flow.ts.RunFrame();
    Require(flow.runner->camera.routeBranch == 1 && flow.ts.Live().empty(),
            "source actor64 short record checks matching rescue totals and ends");
    flow.cfg.eventMode = 9;
    spawnFlow(0x65, 0);
    flow.ts.RunFrame();
    Require(flow.runner->camera.routeBranch == 1 && flow.ts.Live().size() == 1,
            "source actor65 remains live after computing the weighted ending");
    flow.typingEmperorAnswerWeight = 3;
    flow.ts.RunFrame();
    Require(flow.runner->camera.routeBranch == 2, "source actor65 recomputes middle weighted branch");
    flow.typingEmperorAnswerWeight = 5;
    flow.ts.RunFrame();
    Require(flow.runner->camera.routeBranch == 0, "source actor65 recomputes high weighted branch");
    flow.ts.Reset();
    flow.cfg.eventMode = 0;
    spawnFlow(0x61, 0);
    flow.ts.RunFrame();
    Require(flow.typingResultsActive && flow.Count7006() == 2 && flow.typing.chapterFinished(),
            "original result actor owns the shared script-blocking counter and finalizes its chapter once");
    bool gradePhase = false;
    int resultFrames = 1;
    while (flow.typingResultsActive && resultFrames < 1200) {
        flow.ts.RunFrame();
        ++resultFrames;
        gradePhase = gradePhase || flow.Count7006() == 1;
    }
    Require(gradePhase && resultFrames == 849 && !flow.typingResultsActive && flow.Count7006() == 0 &&
            flow.runner->flags[0xFE] == 1 && flow.ts.Live().empty(),
            "one-player original result phases finish automatically and release the authored event gate");
    const auto beforeChoice=flow.typing.stats();
    flow.cfg.gameType=0;
    flow.subState=6;flow.stageEndCounter=1;
    flow.Mode6StageEnd();
    Require(flow.typingChapterChooser.active && flow.typingChapterChooser.selected==1 &&
            flow.typing.chapterChoice().status==std::array<uint8_t,6>{1,0,0,0,0,2},
            "source chapter clear returns to the first available chapter in the chooser");
    flow.pressed=8;
    for (int tick=0;tick<60;++tick) flow.Mode6Frame();
    Require(flow.typingChapterChooser.active,"source chapter chooser rejects Enter during its first sixty ticks");
    flow.pressed=0x80;flow.Mode6Frame();
    flow.pressed=8;
    for (int tick=0;tick<61;++tick) flow.Mode6Frame();
    Require(flow.typingChapterChooser.active && flow.typingChapterChooser.selected==0,
            "a completed chapter remains visible but rejects confirmation");
    flow.pressed=0x40;flow.Mode6Frame();
    flow.pressed=0;
    for (int tick=0;tick<24;++tick) flow.Mode6Frame();
    flow.pressed=8;flow.Mode6Frame();
    Require(!flow.typingChapterChooser.active && flow.nextEventMode==1 && flow.runner->scene==0 && flow.subState==1 &&
            flow.typing.stats().points==beforeChoice.points && flow.typing.stats().completedWords==beforeChoice.completedWords,
            "source chooser confirmation carries the run into the selected chapter without resetting typing totals");
    flow.cfg.eventMode = 5;
    flow.cfg.difficulty = 2;
    flow.typing.setChapter(6);
    flow.typing.beginChapter();
    const auto beforeFinal = flow.typing.stats().points;
    flow.typing.addBasePoints(100);
    flow.rescueCount[5] = 2;
    flow.StageEnd();
    Require(flow.typing.chapterFinished() && flow.typing.chapterScore().total() == 400 &&
            flow.typing.stats().points == beforeFinal + 400,
            "last chapter awards original bonuses even without a results actor");
    flow.StageEnd();
    Require(flow.typing.stats().points == beforeFinal + 400, "repeated stage-end cannot duplicate final bonuses");
    game::SetCurrentWorld(nullptr);
    std::puts("PASS typing eligibility: original spawn/P0 metadata, scenery exclusion, scripted prop window, fourteen slots and source flow");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "usage: typing_campaign_test <game dir> [--campaign|--two-player|--human-campaign|--ending|--bosses|--human-bosses|--menu|--lifecycle|--gun|--eligibility|--miss-recovery]\n");
        return 2;
    }
    const std::string only = argc == 3 ? argv[2] : "";
    const bool fromChapter = only.size() == 15 && only.starts_with("--from-chapter") && only.back() >= '1' && only.back() <= '6';
    if (!only.empty() && !fromChapter && only != "--campaign" && only != "--two-player" && only != "--human-campaign" && only != "--ending" && only != "--bosses" && only != "--human-bosses" && only != "--menu" && only != "--lifecycle" && only != "--gun" && only != "--eligibility" && only != "--miss-recovery") return 2;
    if (only == "--two-player") { Campaign(argv[1],0,false,true); return 0; }
    if (only == "--miss-recovery") { AxeMissRecovery(argv[1]); AxeContinueRecovery(argv[1]); return 0; }
    if (fromChapter) { Campaign(argv[1],only.back()-'1'); return 0; }
    if (only == "--human-campaign") { Campaign(argv[1],0,true); return 0; }
    if (only == "--ending") { EndingRoute(argv[1]); return 0; }
    if (only.empty() || only == "--eligibility") Eligibility();
    if (only.empty() || only == "--menu") MenuFlow(argv[1]);
    if (only.empty() || only == "--lifecycle") Lifecycle(argv[1]);
    if (only.empty() || only == "--gun") GunRegression(argv[1]);
    if (only.empty() || only == "--campaign") Campaign(argv[1]);
    if (only.empty() || only == "--bosses") BossVariants(argv[1]);
    if (only.empty() || only == "--human-bosses") HumanRateBosses(argv[1]);
    std::puts("typing_campaign_test: OK");
}
