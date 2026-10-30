#include "game/typing_rules.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

void CheckReferenceOptions() {
    using game::TypingKeyboard;
    Require(game::TypingKeyCharacter(31,true,TypingKeyboard::US) == '@' &&
            game::TypingKeyCharacter(31,true,TypingKeyboard::UK) == '"' &&
            game::TypingKeyCharacter(52,true,TypingKeyboard::US) == '"' &&
            game::TypingKeyCharacter(52,true,TypingKeyboard::UK) == '@', "physical US/UK punctuation layouts");
    Require(game::TypingKeyCharacter(4,false,TypingKeyboard::UK) == 'a' &&
            game::TypingKeyCharacter(100,true,TypingKeyboard::UK) == '|' &&
            game::TypingKeyCharacter(49,false,TypingKeyboard::UK) == '#' &&
            game::TypingKeyCharacter(89,false,TypingKeyboard::US) == '1', "letter, ISO and keypad input");
    const game::TypingRulesOptions options;
    Require(options.difficulty == game::TypingDifficulty::Normal && options.startingLives == 3 && options.continues == 5,
            "PC default difficulty, lives, continues");
    Require(options.keyboard == game::TypingKeyboard::US && options.wordSize == game::TypingWordSize::Normal && options.bloodEffects,
            "supplied PC executable default keyboard, word size, blood");
    Require(game::TypingLifeOptions() == std::vector<int>({1, 3}) &&
            game::TypingLifeOptions(true) == std::vector<int>({1, 3, 5}), "life option unlock");
    Require(game::TypingContinueOptions() == std::vector<int>({3, 5}) &&
            game::TypingContinueOptions(false, true) == std::vector<int>({3, 5, 0}) &&
            game::TypingContinueOptions(true, true) == std::vector<int>({3, 5, 9, 0}), "continue option unlocks");
}

void CheckWordAwards() {
    using game::TypingRank;
    // A dictionary measure of 25 is deliberately unrelated to a word length.
    Require(game::TypingRankForWord(25, 50) == TypingRank::A && game::TypingRankForWord(25, 51) == TypingRank::B,
            "A boundary uses selected frames and dictionary measure");
    Require(game::TypingRankForWord(25, 65) == TypingRank::B && game::TypingRankForWord(25, 66) == TypingRank::C,
            "B boundary");
    Require(game::TypingRankForWord(25, 80) == TypingRank::C && game::TypingRankForWord(25, 81) == TypingRank::D,
            "C boundary");
    Require(game::TypingRankForWord(25, 95) == TypingRank::D && game::TypingRankForWord(25, 96) == TypingRank::E,
            "D boundary");
    Require(game::TypingRankForWord(5, 13) == TypingRank::C, "PC float precision affects an exact mathematical B boundary");
    Require(game::TypingRankForWord(1000, 9000) == TypingRank::A, "selected timer saturates at 1800 frames");
    Require(game::TypingBasePoints(20) == 10 && game::TypingBasePoints(21) == 12 &&
            game::TypingBasePoints(30) == 12 && game::TypingBasePoints(31) == 15 &&
            game::TypingBasePoints(50) == 15 && game::TypingBasePoints(51) == 17 &&
            game::TypingBasePoints(70) == 17 && game::TypingBasePoints(71) == 20 &&
            game::TypingBasePoints(100) == 20 && game::TypingBasePoints(101) == 25 &&
            game::TypingBasePoints(120) == 25 && game::TypingBasePoints(121) == 30 &&
            game::TypingBasePoints(150) == 30 && game::TypingBasePoints(151) == 35, "base point boundaries");
    const auto projectile = game::TypingWordPoints(151, 0, true);
    Require(projectile.rank == TypingRank::Unranked && projectile.basePoints == 10 && projectile.rankBonus == 0,
            "projectile awards ten base points without a word rank");
}

void CheckChapterAwards() {
    Require(game::TypingAccuracyPercent(0, 0) == 0 && game::TypingAccuracyPercent(81, 100) == 81 &&
            game::TypingAccuracyPercent(989, 1000) == 98, "integer accuracy, including untouched chapter");
    Require(game::TypingAccuracyBonus(80) == 0 && game::TypingAccuracyBonus(81) == 100 &&
            game::TypingAccuracyBonus(85) == 100 && game::TypingAccuracyBonus(86) == 200 &&
            game::TypingAccuracyBonus(91) == 300 && game::TypingAccuracyBonus(94) == 400 &&
            game::TypingAccuracyBonus(96) == 600 && game::TypingAccuracyBonus(98) == 800 &&
            game::TypingAccuracyBonus(100) == 1200, "PC accuracy bonus table (81 to 85 percent is 100)");
    game::TypingScoreBuckets scores;
    scores.addWord(game::TypingWordPoints(25, 50)); // 12 base, 35 rank.
    scores.addWord(game::TypingWordPoints(25, 96)); // 12 base, 5 rank.
    scores.addWord(game::TypingWordPoints(0, 0, true)); // 10 base, unranked.
    const auto easy = game::TypingChapterPoints(scores, game::TypingDifficulty::VeryEasy, 98, 100, 2);
    Require(easy.basePoints == 20 && easy.rankBonus == 40 && easy.accuracyBonus == 800 &&
            easy.civilianBonus == 300 && easy.total() == 1160,
            "difficulty scales accumulated base bucket once, leaving bonuses unchanged");
    scores.adjustBase(-150);
    Require(scores.basePoints == 0, "continue penalty clamps base bucket at zero");
    scores.adjustBase(150);
    Require(game::TypingChapterPoints(scores, game::TypingDifficulty::VeryHard, 0, 0, 0).basePoints == 180,
            "excess life award shares difficulty-scaled base bucket");
}

void CheckPerfectLives() {
    game::TypingPerfectLifeMeter meter;
    for (unsigned word = 1; word <= 27; ++word) Require(!meter.completeWord(), "first life not awarded before word 28");
    Require(meter.gauge == 378 && meter.completeWord() && meter.perfectStreak == 28 &&
            meter.gauge == 0 && meter.threshold == 500, "first life preserves streak and discards overflow");
    for (unsigned word = 29; word <= 42; ++word) Require(!meter.completeWord(), "second life before strict threshold");
    Require(meter.gauge == 497 && meter.completeWord() && meter.perfectStreak == 43 && meter.threshold == 600,
            "next gauge uses continuing perfect streak");
    meter.mistype();
    Require(meter.perfectStreak == 0 && !meter.completeWord(true) && meter.gauge == 0, "miss resets streak; imperfect completion adds no gauge");
    meter.threshold = 400;
    meter.gauge = 399;
    Require(!meter.completeWord() && meter.gauge == 400, "exact gauge threshold does not award a life");
    Require(meter.completeWord() && meter.gauge == 0, "strictly exceeding threshold awards");
    meter.reset();
    Require(meter.perfectStreak == 0 && meter.gauge == 0 && meter.threshold == 400, "continue resets meter difficulty");
    meter.perfectStreak = 999;
    Require(!meter.completeWord() && meter.gauge == 0, "capped unchanged streak does not add gauge");
    Require(game::TypingAddLife(4).lives == 5 && game::TypingAddLife(4).basePoints == 0 &&
            game::TypingAddLife(5).lives == 5 && game::TypingAddLife(5).basePoints == 150, "life cap converts excess life into base points");
}

void CheckAdaptiveRank() {
    game::TypingAdaptiveRank rank;
    rank.reset(1);
    rank.tick(true, 1, {3, 3});
    Require(rank.rank() == 1, "initial player does not add a join bonus");
    rank.word(game::TypingRank::A);
    rank.word(game::TypingRank::A);
    rank.word(game::TypingRank::A);
    Require(rank.rank() == 2 && rank.gradeProgress() == 0, "three As raise rank and discard excess adaptation points");
    for (int word = 0; word < 5; ++word) rank.word(game::TypingRank::E);
    Require(rank.rank() == 3, "even E grades contribute to adaptation");
    rank.word(game::TypingRank::Unranked);
    Require(rank.gradeProgress() == 0, "projectile does not contribute a word grade");
    rank.tick(true, 3, {3, 3});
    Require(rank.rank() == 7, "second joining player raises rank by four");
    rank.damage();
    rank.tick(true, 3, {3, 3});
    Require(rank.rank() == 5 && rank.activeTicks() == 1, "damage lowers rank two and restarts adaptation clock");
    rank.continueGame();
    Require(rank.rank() == 4, "continue immediately lowers rank one");
    rank.tick(true, 1, {3, 3});
    Require(rank.rank() == 0, "departing player lowers rank four, with zero floor");
    rank.reset(3, true);
    Require(rank.rank() == 7, "starting a later chapter uses rank seven");
    for (unsigned frame = 0; frame < 1799; ++frame) rank.tick(true, 1, {4, 0});
    Require(rank.rank() == 7, "timed adaptation waits thirty active seconds");
    rank.tick(false, 1, {4, 0});
    Require(rank.rank() == 7 && rank.activeTicks() == 1800, "paused time does not advance adaptation");
    rank.tick(true, 1, {4, 0});
    Require(rank.rank() == 9, "healthy solo player receives two ranks at thirty seconds");
    rank.reset(1);
    for (unsigned word = 0; word < 60; ++word) rank.word(game::TypingRank::A);
    Require(rank.wordBankRank() == 12 && rank.rank() == 12, "chapter-one dictionary lookup caps stored rank at twelve");
    rank.resetBoss(4, 1);
    Require(rank.rank() == 12, "five-star boss starting rank");
    rank.damage();
    rank.tick(true, 1, {3, 0}, 4);
    Require(rank.rank() == 12, "boss star floor applies after damage");
}
} // namespace

int main() {
    CheckReferenceOptions();
    CheckWordAwards();
    CheckChapterAwards();
    CheckPerfectLives();
    CheckAdaptiveRank();
    std::puts("typing_rules_test: OK");
}
