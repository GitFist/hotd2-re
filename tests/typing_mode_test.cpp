#include "game/typing_mode.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
game::TypingTarget Target(uint32_t serial, bool projectile = false) {
    game::TypingTarget result;
    result.serial = serial;
    result.token = 1;
    result.projectile = projectile;
    return result;
}
char Fold(char c) { return c >= 'A' && c <= 'Z' ? char(c+32) : c; }
std::string Input(std::string_view text) {
    std::string result;
    for (char c : text) if (c != ' ') result += Fold(c);
    return result;
}

void Catalog() {
    const auto banks = game::TypingWordBanks();
    Require(banks.size() == 488, "all original file banks are embedded");
    size_t count = 0;
    for (const auto& bank : banks) {
        const auto words = game::TypingWords(bank);
        Require(!words.empty(), "no empty dictionary bank");
        count += words.size();
        for (const auto& word : words) {
            Require(!word.text.empty() && word.measure > 0, "word measures and display strings survive import");
            Require(std::all_of(word.text.begin(), word.text.end(), [](unsigned char c) {
                return c >= 32 && c <= 126;
            }), "all source text is supported ASCII");
            Require(std::count_if(word.glyphs.begin(), word.glyphs.end(), [](unsigned char c) {
                return c != 0xff;
            }) == word.text.size(), "original emphasis markers preserve glyph-to-text correspondence");
        }
    }
    Require(count == 14061, "14,061 source file records retain original bank ordering");
    const auto quiz = game::TypingWords(*game::FindTypingWordBank("ZE01L101"));
    Require(quiz[0].text == "Which is a State in the US?" && quiz[0].measure == 95 &&
            quiz[1].text == "Alabama" && quiz[9].text == "Organ", "quiz question and answer ordering preserved");
    const auto phrase = game::TypingWords(*game::FindTypingWordBank("ZE04L101"));
    Require(phrase[0].text == "The quick brown fox jumped over the" && phrase[0].measure == 133 &&
            phrase[3].text == "Seriously kicked the fox's ass.", "original phrase chain and punctuation preserved");
    Require(game::TypingWordBankLevel(100, 1) == 1 && game::TypingWordBankLevel(25, 1) == 0 &&
            game::TypingWordBankLevel(100, 0) == 0 && game::TypingWordBankLevel(2000, 15) == 19,
            "reference selector conversion uses internal rank and original upper cap");
}

void SelectionAndScoring() {
    game::TypingModeController mode(123);
    mode.syncTargets({Target(1), Target(2), Target(3)});
    const auto prompts = mode.prompts();
    const auto bank = game::TypingWords(*game::FindTypingWordBank(0, 1));
    std::set<char> initials;
    for (const auto& p : prompts) {
        initials.insert(Fold(p.text.front()));
        Require(std::any_of(bank.begin(), bank.end(), [&](const auto& w) {
            return w.text == p.text && w.measure == p.measure && w.flags == p.flags;
        }), "every ordinary prompt comes from the selected original bank");
    }
    Require(initials.size() == prompts.size(), "live target initials do not collide");
    char unused = 'a';
    while (initials.count(unused)) ++unused;
    mode.typeCharacters(std::string(1,unused));
    Require(mode.stats().errors == 0 && mode.stats().correctCharacters == 0,
            "unmatched first key does not count as an attempt");
    const auto chosen = prompts.front();
    Require(chosen.text.size() > 1, "selected word exercises partial input");
    mode.typeCharacters(chosen.text.substr(0,1));
    mode.update(1, true);
    const char wrong = Fold(chosen.text[1]) == 'a' ? 'b' : 'a';
    mode.typeCharacters(std::string(1,wrong));
    Require(mode.stats().errors == 1 && mode.prompts()[0].progress == 1,
            "selected word errors do not advance progress");
    mode.cancelSelection();
    Require(mode.selectedSerial() == 0 && mode.prompts()[0].progress == 0,
            "Escape releases selection and resets word progress");
    mode.typeCharacters(chosen.text.substr(0,1));
    mode.update(double(chosen.measure) / 60, true);
    const auto completion = mode.typeCharacters(Input(chosen.text.substr(1)));
    Require(completion.size() == 1 && completion[0].rank == game::TypingRank::A &&
            completion[0].points == game::TypingBasePoints(chosen.measure)+35,
            "stored measure and selected frames determine reference A grade and score");
    Require(mode.stats().perfectStreak == 1 && mode.stats().errors == 1,
            "Escape starts a clean perfect-word attempt while retaining the earlier error total");
    mode.syncTargets({Target(1),Target(2),Target(3)});
    Require(mode.prompts().size() == 2, "same live target cannot complete twice");
    mode.syncTargets({});
    Require(mode.prompts().empty() && mode.selectedSerial() == 0, "target removal clears selection");
}

void PauseAndProjectiles() {
    game::TypingModeController a(7), b(7);
    std::vector<game::TypingTarget> volley;
    for (uint32_t i = 1; i <= 14; ++i) volley.push_back(Target(i,true));
    a.syncTargets(volley);
    b.syncTargets(volley);
    const auto prompts = a.prompts();
    std::set<char> initials;
    for (size_t i = 0; i < prompts.size(); ++i) {
        const auto& p = prompts[i];
        initials.insert(Fold(p.text.front()));
        Require(p.text == b.prompts()[i].text && p.measure == 5 && p.text.size() == 1,
                "reaction words use original built-in letters and reproducible stream");
    }
    Require(initials.size() == 14, "original target capacity supports fourteen distinct initials");
    a.update(2, false);
    Require(a.typeCharacters(prompts.front().text,false).empty() && a.stats().activeSeconds == 0,
            "paused frames reject time and text");
    for (const auto& p : prompts) {
        const auto done = a.typeCharacters(Input(p.text));
        Require(done.size() == 1 && done[0].points == 10 && done[0].rank == game::TypingRank::Unranked,
                "projectile completion is unranked and awards ten base points");
    }
    Require(a.stats().completedWords == 14 && a.stats().perfectStreak == 14 && a.stats().lifeGauge == 105,
            "perfect gauge adds successive streak values");
}

void MagicianAccuracyReset() {
    const game::TypingWordRecord hello{20, 0, "hello", {}}, beta{20, 0, "beta", {}}, gamma{20, 0, "gamma", {}};
    auto magician = Target(1), other = Target(2), cleared = Target(3);
    magician.flags = 0x00200000u;
    magician.forcedWord = &hello;
    other.forcedWord = &beta;
    cleared.forcedWord = &gamma;
    game::TypingModeController mode(9);
    mode.syncTargets({magician, other, cleared});
    Require(mode.typeCharacters("gamma").size() == 1, "completed word precedes global accuracy reset");
    const auto points = mode.stats().points;
    mode.typeCharacters("h?");
    Require(mode.selectedSerial() == 0 && mode.prompts().empty() && mode.stats().errors == 1 &&
            mode.stats().perfectStreak == 0, "Magician wrong key discards all live words and selections");
    Require(mode.lastMistypeSerial() == 1, "mistake feedback retains its actor after Magician clears the selection");
    mode.typeCharacters("");
    Require(mode.lastMistypeSerial() == 0, "idle input cannot replay an earlier mistake effect");
    mode.syncTargets({magician, other, cleared});
    const auto prompts = mode.prompts();
    Require(prompts.size() == 2 && prompts[0].progress == 0 && prompts[1].progress == 0 &&
            mode.stats().completedWords == 1 && mode.stats().points == points,
            "next snapshot restores fresh active words without reviving completed targets or awarding points");
}

void OriginalBankSelectors() {
    game::TypingModeController mode(11);
    mode.setDynamicRank(15);
    mode.setWordLimits(100, 100);
    mode.setWordSelection(-1, 600);
    mode.syncTargets({Target(1)});
    const auto unadjusted = mode.prompts().front();
    const auto expected = game::TypingWords(*game::FindTypingWordBank(0, 6));
    Require(std::any_of(expected.begin(), expected.end(), [&](const auto& record) {
        return record.text == unadjusted.text && record.measure == unadjusted.measure;
    }), "raw group minus one bypasses dynamic rank and word limits");
    mode.setRandomSource([] { return 9u; });
    mode.setWordSelection(2, -2);
    mode.syncTargets({Target(1)});
    const auto randomBank = mode.prompts().front();
    const auto tenth = game::TypingWords(*game::FindTypingWordBank(2, 10));
    Require(std::any_of(tenth.begin(), tenth.end(), [&](const auto& record) {
        return record.text == randomBank.text && record.measure == randomBank.measure;
    }), "minus-two selector selects a one-based random bank once");
}

void ChapterAccounting() {
    game::TypingModeController mode(13);
    const game::TypingWordRecord alpha{20, 0, "alpha", {}}, beta{20, 0, "beta", {}};
    auto target = Target(1);
    target.forcedWord = &alpha;
    mode.syncTargets({target});
    Require(mode.typeCharacters("alpha").size() == 1, "chapter word completed");
    mode.addBasePoints(100);
    const auto& score = mode.finishChapter(game::TypingDifficulty::VeryEasy, 2);
    Require(score.basePoints == 66 && score.rankBonus == 35 && score.accuracyBonus == 1200 &&
            score.civilianBonus == 300 && mode.stats().points == score.total(),
            "chapter finalization scales the accumulated base bucket once and adds the original bonuses");
    const uint64_t first = mode.stats().points;
    Require(mode.chapterRecords()[0].status == 1 && mode.chapterOrdinal() > 0 &&
            mode.chapterBest() >= score.total() && mode.chapterMean() > 0,
            "chapter finalization retains original per-chapter ordinal, best and comparison mean");
    mode.finishChapter(game::TypingDifficulty::VeryHard, 99);
    Require(mode.stats().points == first, "chapter score is finalized at most once");
    mode.beginChapter();
    mode.setChapter(2);
    Require(mode.scoreBuckets().basePoints == 0 && mode.chapterStats().completedWords == 0 &&
            mode.stats().completedWords == 1 && mode.stats().perfectStreak == 1 && mode.stats().points == first,
            "a new chapter clears its buckets and preserves cumulative run statistics and the perfect meter");
    target.token = 2;
    target.forcedWord = &beta;
    mode.syncTargets({target});
    Require(mode.typeCharacters("beta").size() == 1, "second chapter word completed");
    mode.setDifficulty(game::TypingDifficulty::Normal);
    Require(mode.currentChapterPoints(3)==1395,
            "source total-score ranking uses current chapter buckets and only its own rescued civilians");
    const auto& second = mode.finishChapter(game::TypingDifficulty::VeryHard, 0);
    Require(second.basePoints == 12 && second.rankBonus == 35 && second.accuracyBonus == 1200 &&
            mode.stats().points == first + second.total() && mode.stats().completedWords == 2 &&
            mode.chapterStats().correctCharacters == 4,
            "chapter accuracy and difficulty do not reuse prior chapter counters");
    Require(mode.currentChapterPoints(2)==second.total() && mode.currentChapterPoints(2)!=mode.stats().points,
            "finalized chapter score remains distinct from cumulative run points for score qualification");
    Require(mode.chapterRecords()[0].status == 1 && mode.chapterRecords()[1].status == 1,
            "final book keeps each completed chapter's result across chapter transitions");
    mode.reset();
    Require(mode.stats().points == 0 && mode.stats().completedWords == 0 && !mode.chapterFinished(),
            "new run reset clears cumulative and chapter results");
}

void NoWordScoreFlag() {
    game::TypingModeController mode(17);
    const game::TypingWordRecord orb{30, 0, "orb", {}};
    auto target = Target(1);
    target.flags = 0x38000008u;
    target.forcedWord = &orb;
    mode.syncTargets({target});
    const auto completed = mode.typeCharacters("orb");
    Require(completed.size() == 1 && completed[0].points == 0 && completed[0].rank == game::TypingRank::Unranked &&
            mode.scoreBuckets().basePoints == 0 && mode.stats().points == 0,
            "original flag08000000 suppresses word points and adaptive rank awards");
    Require(completed[0].visualRank == game::TypingRank::A,
            "an unscored ordinary word still reports its original visual grade");
    Require(mode.stats().correctCharacters == 3 && mode.stats().completedWords == 1 &&
            mode.stats().perfectStreak == 1 && mode.stats().lifeGauge == 1,
            "unscored boss word still completes and participates in typing accuracy and perfect meter");
}

void OriginalActorWordFlags() {
    game::TypingModeController mode(19);
    auto target = Target(1);
    target.flags = 0x30000014u;
    mode.syncTargets({target});
    Require(mode.prompts().front().projectile && mode.prompts().front().text.size() == 1,
            "original flag10 chooses one-key unranked words");
    target.token = 2;
    target.flags = 0x30000024u;
    target.projectile = true; // The adapter's old flying-object guess must not override reference metadata.
    mode.syncTargets({target});
    const auto shortWord = mode.prompts().front();
    const auto shorts = game::TypingWords(*game::FindTypingBuiltinBank("SHORT"));
    Require(!shortWord.projectile && std::any_of(shorts.begin(), shorts.end(), [&](const auto& record) {
        return record.text == shortWord.text && record.measure == shortWord.measure;
    }), "original flag20 chooses the distinct ranked short-word bank");
    target.token = 3;
    target.flags = 0x30000044u;
    mode.syncTargets({target});
    const auto fifth = mode.prompts().front();
    const auto fifthBank = game::TypingWords(*game::FindTypingBuiltinBank("ABBREVIATIONS")).first(29);
    Require(!fifth.projectile && std::any_of(fifthBank.begin(), fifthBank.end(), [&](const auto& record) {
        return record.text == fifth.text && record.measure == fifth.measure;
    }), "original actor flag40 chooses type5 abbreviations in its authored index range");
}

void OriginalPickerRanges() {
    const auto bank = game::TypingWords(*game::FindTypingWordBank(0, 1));
    game::TypingModeController mode;
    mode.setRandomSource([] { return 0u; });
    auto target = Target(1);
    target.flags = 0x01000000u;
    mode.syncTargets({target});
    Require(mode.prompts().front().text == bank[1].text,
            "actor flag01000000 picks from original indices one through eight");
    target.token = 2;
    target.flags = 0x00800000u;
    mode.syncTargets({target});
    Require(mode.prompts().front().text == bank[9].text,
            "actor flag00800000 skips the first nine entries in its current bank");

    game::TypingModeController fixed;
    fixed.setRandomSource([] { return 0u; });
    fixed.setWordSelection(2, -2);
    target.flags = 0x01000040u;
    fixed.syncTargets({target});
    const auto questions = game::TypingWords(*game::FindTypingWordBank(2, 1));
    Require(fixed.prompts().front().text == questions.front().text,
            "fixed banks preserve actor index-range priority without switching to abbreviations");

    game::TypingModeController repeated;
    unsigned calls = 0;
    repeated.setRandomSource([&] { return ++calls <= 2 ? 0u : 1u; });
    repeated.setWordSelection(2, -2);
    repeated.setWordSelection(2, -2);
    Require(calls == 3, "random-bank selection rejects indices in the shared sixteen-pick history");
    repeated.syncTargets({Target(1)});
    const auto secondBank = game::TypingWords(*game::FindTypingWordBank(2, 2));
    Require(std::any_of(secondBank.begin(),secondBank.end(),[&](const auto& word) {
                return word.text == repeated.prompts().front().text;
            }), "repeated random-bank selection uses the next accepted original bank");
}

void OriginalSelectionTiming() {
    const game::TypingWordRecord pair{25, 0, "ab", {}};
    auto target = Target(1);
    target.forcedWord = &pair;
    game::TypingModeController boundary;
    boundary.syncTargets({target});
    boundary.typeCharacters("a");
    boundary.update(49.0 / 60.0, true);
    Require(boundary.typeCharacters("b").front().rank == game::TypingRank::A,
            "the first selected frame participates in the inclusive fifty-frame A boundary");
    target.token = 2;
    boundary.syncTargets({target});
    boundary.typeCharacters("a");
    boundary.update(50.0 / 60.0, true);
    Require(boundary.typeCharacters("b").front().rank == game::TypingRank::B,
            "fifty further frames after selection produce the original fifty-one-frame B grade");

    const game::TypingWordRecord single{4, 0, "I", {}};
    target.token = 3;
    target.forcedWord = &single;
    boundary.syncTargets({target});
    const auto completion = boundary.typeCharacters("i");
    Require(completion.size() == 1 && completion.front().rank == game::TypingRank::Unranked &&
            completion.front().visualRank == game::TypingRank::Unranked && completion.front().points == 10,
            "ordinary single-character dictionary words keep base points without a speed grade or bonus");
}

void OriginalRunMetrics() {
    const game::TypingWordRecord word{25,0,"QW",{}};
    auto target=Target(1);
    target.forcedWord=&word;
    target.flags=0x00400008u;
    game::TypingModeController mode;
    mode.setFrameCounter(100);
    mode.syncTargets({target});
    mode.noteDamage(true,true);
    Require(mode.runMetrics().smallThreatHits==1 && mode.runMetrics().otherTargetHits==0,
            "an accepted hit without a selected word is not an off-target judgment penalty");
    mode.setFrameCounter(160);
    mode.typeCharacters("q");
    mode.noteDamage(false,true);
    mode.update(49.0/60.0,true);
    mode.typeCharacters("w");
    const auto& metrics=mode.runMetrics();
    Require(metrics.words==1 && metrics.speedCharacters==1 && metrics.speedFrames==50 &&
            metrics.reactionFrames==60 && metrics.reactions==1 && metrics.otherTargetHits==1,
            "source run metrics keep speed and first-selection reaction separate from active play time");
    Require(metrics.propWords==1 && metrics.completedProps==1 && metrics.concentrationBase==10 &&
            metrics.concentrationPoints==10,
            "source prop ratio and rare-letter concentration weights follow actual assignment and completion");
}

void SelectedWordOwnsInput() {
    game::TypingModeController mode(1);
    const game::TypingWordRecord phrase{40, 0, "Luxury wig", {}};
    const game::TypingWordRecord key{5, 0, "x", {}};
    auto ordinary = Target(1), projectile = Target(2, true);
    ordinary.forcedWord = &phrase;
    projectile.forcedWord = &key;
    mode.syncTargets({ordinary, projectile});
    const auto completed = mode.typeCharacters("Luxury wig");
    Require(completed.size() == 1 && completed.front().serial == 1 && mode.stats().errors == 0,
            "selected original phrase owns every key despite a matching one-key actor");
    Require(mode.prompts().size() == 1 && mode.prompts().front().serial == 2 &&
            mode.typeCharacters("x").size() == 1,
            "the one-key actor is selectable after the phrase completes");
}

void HiddenTargetRetention() {
    game::TypingModeController mode(23);
    const game::TypingWordRecord alpha{20, 0, "alpha", {}}, beta{20, 0, "beta", {}};
    auto first = Target(1), second = Target(2);
    first.forcedWord = &alpha;
    second.forcedWord = &beta;
    second.enabled = false;
    mode.syncTargets({first, second});
    Require(mode.prompts().size() == 1, "a hidden actor does not allocate its first word");
    mode.typeCharacters("al");
    first.enabled = false;
    mode.syncTargets({first, second});
    const auto errors = mode.stats().errors;
    mode.update(2.0, true);
    Require(mode.prompts().empty() && mode.typeCharacters("wrong").empty() && mode.stats().errors == errors &&
            mode.selectedSerial() == 1 && mode.stats().activeSeconds == 0,
            "hidden selection retains progress and rejects input without errors or time");
    first.enabled = true;
    mode.syncTargets({first, second});
    const auto restored = mode.prompts().front();
    Require(restored.text == "alpha" && restored.progress == 2 && restored.selected,
            "returning actor restores the same word and partial selection");
    mode.syncTargets({second});
    Require(mode.prompts().empty() && mode.selectedSerial() == 0,
            "an actor leaving the live snapshot releases its word and selection");
    second.enabled = true;
    mode.syncTargets({second});
    Require(mode.prompts().size() == 1 && mode.prompts().front().text == "beta",
            "first eligibility allocates the previously hidden actor's word");
}

void VisibleInputGate() {
    game::TypingModeController mode(23);
    const game::TypingWordRecord word{20, 0, "alpha", {}};
    auto target = Target(1);
    target.forcedWord = &word;
    target.flags = 0x10000u;
    mode.syncTargets({target});
    Require(mode.prompts().size() == 1 && mode.typeCharacters("alpha").empty() &&
            mode.stats().correctCharacters == 0 && mode.stats().errors == 0 && mode.selectedSerial() == 0,
            "original flag10000 displays its word without accepting a selection or characters");
    target.flags = 0;
    mode.syncTargets({target});
    mode.typeCharacters("al");
    target.flags = 0x10000u;
    mode.syncTargets({target});
    Require(mode.typeCharacters("wrong").empty() && mode.prompts().front().progress == 2 &&
            mode.prompts().front().selected && mode.stats().errors == 0,
            "closing an input window preserves visible partial progress without counting hidden input");
    target.flags = 0;
    mode.syncTargets({target});
    Require(mode.assignedWordCount() == 1 && mode.typeCharacters("pha").size() == 1,
            "opening the input window resumes the same word");
}

void OriginalItems() {
    using Item=game::TypingOriginalItem;
    game::TypingModeController mode(29);
    mode.setOriginalMode(true);
    Require(mode.originalMode() && mode.finalEvaluation(0,0,false).originalMode==1,
            "Original campaign and its final evaluation carry the Original category");
    auto& items=mode.original();
    Require(!items.collect(9) && items.collect(17) && items.collect(18) && items.collect(19) && items.collect(20),
            "only source Original stocked-item identities enter the function-key boxes");
    Require(items.slots()==std::array<Item,4>{Item::GoldTranquilizer,Item::HandOfGod,Item::MolotovCocktail,Item::SulphuricAcid},
            "stocked pickups fill the four source slots from left to right");
    Require(items.collect(21) && items.slots()==std::array<Item,4>{Item::HandOfGod,Item::MolotovCocktail,Item::SulphuricAcid,Item::PainKiller},
            "a fifth item discards the oldest and shifts remaining slots as source454313");
    Require(items.use(1,false)==Item::None && items.slots()[1]==Item::MolotovCocktail,
            "Molotov remains stocked when no enemy can receive its hit");
    Require(items.use(0,true)==Item::HandOfGod && items.slots()[0]==Item::None && items.anyKeyCorrect(),
            "F1 consumes its own slot while retaining other slot positions");
    Require(items.use(3,true)==Item::None,"timed item effects cannot overlap");
    const game::TypingWordRecord word{20,0,"alpha",{}};
    auto target=Target(1);target.forcedWord=&word;
    mode.syncTargets({target});
    Require(mode.typeCharacters("zzzzz").size()==1 && mode.stats().correctCharacters==5 && mode.stats().errors==0,
            "Hand of God selects a real word and accepts arbitrary keys for its real character count");
    mode.update(20,false);
    Require(items.remainingFrames()==900,"paused or blocked typing preserves the fifteen-second item timer");
    mode.update(14,true);
    Require(items.anyKeyCorrect() && items.remainingFrames()==60,"Hand of God runs for nine hundred active frames");
    mode.update(1,true);
    Require(!items.anyKeyCorrect() && items.use(3,true)==Item::PainKiller && items.invulnerable(),
            "after Hand of God expires the next stocked timed item can activate");
    mode.update(15,true);
    Require(!items.invulnerable(),"Pain Killer ends after nine hundred active frames");
    items.reset(8);
    Require(items.slots()[0]==Item::MolotovCocktail && items.slots()[1]==Item::MolotovCocktail,
            "the ten-coin source unlock starts a run with two Molotov cocktails");
    items.reset(24);
    Require(items.slots()[0]==Item::HandOfGod && items.slots()[1]==Item::MolotovCocktail &&
            items.slots()[2]==Item::MolotovCocktail,"the thirty-coin unlock reserves F1 for Hand of God");
    items.use(0,true);items.updateFrames(900);
    Require(items.slots()[0]==Item::HandOfGod,"thirty-coin Hand of God replenishes after its effect ends");
    mode.continueGame();
    Require(items.usedContinue(),"continuing disqualifies the current chapter no-continue coin");
    mode.beginChapter();
    Require(!items.usedContinue(),"the next chapter starts a fresh no-continue condition");
}

void OriginalDictionaries() {
    game::TypingModeController mode(31);
    mode.setOriginalMode(true);
    mode.setRandomSource([]{return 0u;});
    Require(mode.collectOriginalItem(6),"the source genre dictionary activates its own Original flag");
    mode.setWordSelection(0,100);
    const auto words=game::TypingWords(*game::FindTypingWordBank(7,1));
    for (size_t i=0;i<words.size();++i) {
        mode.syncTargets({Target(uint32_t(i+1))});
        Require(mode.prompts().size()==1 && mode.prompts()[0].text==words[i].text,
                "genre dictionary walks one authored genre bank in source order");
        if (i==3) { mode.originalSceneClear();mode.setWordSelection(0,100); }
    }
    mode.syncTargets({Target(99)});
    Require(!mode.original().genreDictionary(),"genre dictionary expires once its selected bank is exhausted");
    Require(mode.collectOriginalItem(11),"the source alien dictionary activates its temporary flag");
    mode.setWordSelection(0,100);
    mode.syncTargets({Target(100)});
    const auto alien=game::TypingWords(*game::FindTypingWordBank(5,1));
    Require(std::any_of(alien.begin(),alien.end(),[&](const auto& word){return word.text==mode.prompts()[0].text;}),
            "alien dictionary uses the supplied original alien word bank");
    mode.originalSceneClear();
    Require(mode.original().alienDictionary(),"alien dictionary survives the first authored clear");
    mode.originalSceneClear();
    Require(!mode.original().alienDictionary(),"alien dictionary expires on its second authored clear");
    mode.collectOriginalItem(14);mode.collectOriginalItem(15);
    Require((mode.original().sourceFlags()&0xC0)==0x80,"human and zombie presentation flags exclude one another");
    Require(game::TypingOriginalCharacterKind(64,0x39)==7 && game::TypingOriginalCharacterKind(64,0x3A)==8 &&
            game::TypingOriginalCharacterKind(128,0x39)==0x34 && game::TypingOriginalCharacterKind(128,0x3A)==0x21 &&
            game::TypingOriginalCharacterKind(64,0x28)==0x28,
            "source costumes replace the two agent skeletons with authored zombie or citizen identities");
    Require(game::TypingOriginalNodeScale(2,0,false,2)==std::array<float,3>{2,2,2} &&
            game::TypingOriginalNodeScale(2,14,false,2)==std::array<float,3>{1.8f,1.8f,1} &&
            game::TypingOriginalNodeScale(2,0x21,true,2)==std::array<float,3>{1.5f,1,1.5f} &&
            game::TypingOriginalNodeScale(2,0x21,true,5)==std::array<float,3>{2,2,2} &&
            game::TypingOriginalNodeScale(0,0,false,2)==std::array<float,3>{1,1,1},
            "Meat uses source enemy-head and civilian-head/limb scale transforms and leaves normal draws unchanged");
}

void BookAssignmentAccounting() {
    game::TypingModeController mode(42);
    auto key=Target(1,true);
    mode.syncTargets({key});
    Require(mode.assignedWordCount()==1 && mode.assignedBookWordCount()==0,
            "a builtin key post leaves the source file dictionary pending and its book assignment count zero");
    const auto keyText=mode.prompts().front().text;
    Require(mode.typeCharacters(Input(keyText)).size()==1 && mode.stats().completedWords==1,
            "a fully typed one-key post still increments the source completed-word event");
    mode.syncTargets({Target(2)});
    Require(mode.assignedWordCount()==2 && mode.assignedBookWordCount()==1,
            "an ordinary file-bank lookup resolves the pending dictionary before incrementing its book count");
    const auto wordText=mode.prompts().front().text;
    Require(mode.typeCharacters(Input(wordText)).size()==1 && mode.stats().completedWords==2,
            "separate book assignment accounting leaves native completion totals unchanged");
    mode.syncTargets({Target(3,true)});
    Require(mode.assignedWordCount()==3 && mode.assignedBookWordCount()==2,
            "the source book gate follows file readiness rather than blanket exclusion of all one-key words");
    mode.setWordSelection(1,100);
    mode.syncTargets({Target(4,true)});
    Require(mode.assignedWordCount()==4 && mode.assignedBookWordCount()==2,
            "changing dictionary files restores the pending guard even when the next post uses builtin keys");
    mode.syncTargets({Target(5)});
    Require(mode.assignedWordCount()==5 && mode.assignedBookWordCount()==3,
            "the changed file dictionary becomes countable when first resolved");
    mode.setWordSelection(1,100);
    mode.syncTargets({Target(6,true)});
    Require(mode.assignedBookWordCount()==4,
            "reselecting the same cached file preserves the original resolved-file state");
    mode.reset();
    auto shortWord=Target(7);shortWord.flags=0x20;
    auto abbreviation=Target(8);abbreviation.flags=0x40;
    mode.syncTargets({Target(9,true),shortWord,abbreviation});
    Require(mode.assignedWordCount()==3 && mode.assignedBookWordCount()==0 && mode.stats().completedWords==0,
            "reset clears both totals and builtin short/abbreviation/key banks leave the new file unresolved");
}
}

int main() {
    Catalog();
    SelectionAndScoring();
    PauseAndProjectiles();
    MagicianAccuracyReset();
    OriginalBankSelectors();
    ChapterAccounting();
    NoWordScoreFlag();
    OriginalActorWordFlags();
    OriginalPickerRanges();
    OriginalSelectionTiming();
    OriginalRunMetrics();
    SelectedWordOwnsInput();
    HiddenTargetRetention();
    VisibleInputGate();
    OriginalItems();
    OriginalDictionaries();
    BookAssignmentAccounting();
    std::puts("typing_mode_test: original catalog, input, scoring and lifecycle passed");
}
