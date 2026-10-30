#include "game/typing_boss_rules.h"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}

int main() {
    Require(game::TypingJudgmentWordDamage() == 40 && game::TypingJudgmentWordDamage(true) == 35 &&
            game::TypingHierophantWordDamage() == 30 && game::TypingTowerSwimmingWordDamage() == 24,
            "Judgment, Hierophant and swimming Tower use original fixed word damage");
    Require(game::TypingJudgmentFirstPhaseStep()==.3f && game::TypingJudgmentFirstPath(0)==1 &&
            game::TypingJudgmentFirstPath(20)==1 && game::TypingJudgmentFirstPath(10)==17 &&
            game::TypingJudgmentFirstPath(30)==17 && game::TypingJudgmentSecondPath(9)==0 &&
            game::TypingJudgmentSecondPath(17)==1 && game::TypingJudgmentSecondPath(40)==0 &&
            game::TypingJudgmentSecondPath(41)==1 && game::TypingJudgmentSecondPathGroup(19)==0,
            "Judgment uses its original slower route selection, including immediate post-attack rank lookup");
    int pathWhole=0; float pathFraction=0;
    for (int i=0;i<3;++i) game::TypingJudgmentAdvancePathClock(pathWhole,pathFraction,0);
    Require(pathWhole==0 && pathFraction==1.0f,"Judgment fractional clock carries strictly after one, not at equality");
    const float nextPath=game::TypingJudgmentAdvancePathClock(pathWhole,pathFraction,0);
    Require(pathWhole==1 && nextPath>1.3333f && nextPath<1.3334f,
            "Judgment rank-zero lunge advances one third of an authored path tick per frame");
    Require(game::TypingMagicianWordDamage(6) == 15 && game::TypingMagicianWordDamage(9) == 15 &&
            game::TypingMagicianWordDamage(10) == 20 && game::TypingMagicianWordDamage(11) == 20,
            "Magician changes fixed damage in its final phase");
    const auto low=game::TypingMagicianTimingForRank(0), high=game::TypingMagicianTimingForRank(15);
    Require(low.move==30 && low.hold==15 && low.repeats==9 && low.charge==90 && low.projectile==400 &&
            high.move==30 && high.hold==15 && high.repeats==3 && high.charge==90 && high.projectile==280,
            "Magician uses supplied PC hop, charge and word-projectile windows at both rank endpoints");
    Require(game::TypingMagicianTimingForRank(3).repeats==9 && game::TypingMagicianTimingForRank(4).repeats==7 &&
            game::TypingMagicianTimingForRank(6).repeats==7 && game::TypingMagicianTimingForRank(7).repeats==6 &&
            game::TypingMagicianTimingForRank(8).repeats==6 && game::TypingMagicianTimingForRank(9).repeats==5 &&
            game::TypingMagicianTimingForRank(12).repeats==4 && game::TypingMagicianTimingForRank(14).repeats==3,
            "Magician repetition boundaries follow all five authored rank bands");
    Require(game::TypingMagicianPhaseBehaviour(0)==8 && game::TypingMagicianPhaseBehaviour(2)==8 &&
            game::TypingMagicianPhaseBehaviour(3)==8 && game::TypingMagicianPhaseBehaviour(4)==10 &&
            game::TypingMagicianPhaseThreshold(300,3)==135 && game::TypingMagicianPhaseThreshold(309,0)==255,
            "Magician keeps four hop phases and preserves the PC integer HP division before its 45-percent threshold");
    std::array<game::TypingWordRecord, 11> bank{};
    bank[0] = {95, 0, "Question?", {}};
    for (unsigned index = 1; index <= 8; ++index) bank[index] = {25, 0, "Correct", {}};
    bank[9] = {20, 0, "Wrong", {}};
    bank[10] = {20, 0, "Other wrong", {}};
    game::TypingQuizChallenge quiz;
    Require(quiz.begin(bank, {9, 1, 10}) && quiz.valid() && quiz.correctChoice() == 1,
            "one of three answer heads uses a correct record");
    Require(quiz.question().text == "Question?" && !quiz.acceptingAnswers() &&
            quiz.answer(1) == game::TypingQuizOutcome::None, "question reveal precedes answer input");
    quiz.revealComplete(8);
    Require(quiz.remainingFrames() == 135 && quiz.answer(1) == game::TypingQuizOutcome::Correct,
            "correct complete answer resolves quiz");
    Require(quiz.answer(0) == game::TypingQuizOutcome::None && quiz.tick() == game::TypingQuizOutcome::None,
            "resolved quiz cannot produce another outcome");
    Require(quiz.begin(bank, {8, 9, 10}), "eighth correct record remains in correct pool");
    quiz.revealComplete(8);
    Require(quiz.answer(1) == game::TypingQuizOutcome::Wrong, "wrong complete answer schedules failure");
    Require(quiz.begin(bank, {9, 10, 8}), "correct answer can appear at third head");
    quiz.revealComplete(8);
    Require(quiz.tick(134) == game::TypingQuizOutcome::None && quiz.remainingFrames() == 1 &&
            quiz.tick() == game::TypingQuizOutcome::Timeout, "expiry occurs at frame boundary");
    Require(!quiz.begin(bank, {1, 2, 9}) && !quiz.valid(), "multiple correct heads rejected");
    Require(!quiz.begin(bank, {9, 10, 0}) && !quiz.valid(), "question is not a selectable answer");
    Require(game::TypingQuizTimeFrames(20, 0) == 203 && game::TypingQuizTimeFrames(25, 15) == 80,
            "quiz timing preserves PC float precision and truncation");
    Require(game::TypingTowerStartingLife() == 180 && game::TypingTowerCorrectDamage() == 30 &&
            game::TypingTowerStartingLife(true) == 150 && game::TypingTowerCorrectDamage(true) == 21,
            "Tower HP and answer damage follow selected variant");
    Require(game::TypingStrengthTimeFrames(25, 3) == 99 && game::TypingStrengthTimeFrames(25, 15) == 45,
            "Strength line timing preserves the supplied PC float table");
    Require(game::TypingStrengthSwingWindow(0x8000, 55) && !game::TypingStrengthSwingWindow(0x7800, 55) &&
            !game::TypingStrengthSwingWindow(0x8800, 55) && !game::TypingStrengthSwingWindow(0x8000, 40) &&
            !game::TypingStrengthSwingWindow(0x8000, 70), "Strength approach uses strict yaw and distance windows");
    Require(game::TypingStrengthTrailingCameraTick(100, false) == 50 &&
            game::TypingStrengthTrailingCameraTick(100, true) == 75 &&
            game::TypingStrengthTrailingCameraTick(20, true) == 0, "timeout moves the trailing sample 25 camera ticks nearer");
    Require(game::TypingStrengthNextCameraTick(100, false, false) == 100.95f &&
            game::TypingStrengthNextCameraTick(100, true, false) == 101.1f &&
            game::TypingStrengthNextCameraTick(100, true, true) == 100 &&
            game::TypingStrengthNextCameraTick(1350, false, false) == 389 &&
            game::TypingStrengthNextCameraTick(1350, true, false) == 396,
            "Strength camera advances at its arena rate, holds for flinch, and wraps after tick 1350");
    int strengthLife = 100;
    strengthLife = game::TypingStrengthLifeAfterStory(strengthLife, 100);
    Require(strengthLife == 66, "one completed four-line story removes 34 percent of maximum HP");
    strengthLife = game::TypingStrengthLifeAfterStory(strengthLife, 100);
    Require(strengthLife == 32 && game::TypingStrengthLifeAfterStory(strengthLife, 100) == 0,
            "three entire stories defeat Strength");
    game::TypingStrengthReveal storyReveal;
    for (unsigned frame = 0; frame < 253; ++frame) storyReveal.tick();
    Require(!storyReveal.available(), "Strength line zero waits through startup, bounce and rise");
    storyReveal.tick();
    Require(storyReveal.available() && storyReveal.line() == 0 && !storyReveal.completeLine(),
            "Strength first line is available at task tick 254");
    for (unsigned frame = 0; frame < 132; ++frame) storyReveal.tick();
    Require(!storyReveal.available(), "an immediate completion retains the next poster's entire bounce");
    storyReveal.tick();
    Require(storyReveal.available() && storyReveal.line() == 1, "next line includes 61 bounce, 12 rise and 60 hold ticks");
    for (unsigned frame = 0; frame < 61; ++frame) storyReveal.tick();
    Require(!storyReveal.completeLine(), "typing overlaps the following poster's bounce");
    for (unsigned frame = 0; frame < 71; ++frame) storyReveal.tick();
    Require(!storyReveal.available(), "finished parallel bounce still requires rise and hold");
    storyReveal.tick();
    Require(storyReveal.available() && storyReveal.line() == 2, "later completed bounce leaves 72 ticks");
    storyReveal.completeLine();
    for (unsigned frame = 0; frame < 133; ++frame) storyReveal.tick();
    Require(storyReveal.completeLine() && !storyReveal.available(), "only completing the fourth revealed line finishes the story");
    const game::TypingWordRecord emperorChoice{127, 1, "First choice, weight one", {}};
    Require(game::TypingEmperorAnswerWeight(emperorChoice) == 1 && game::TypingEmperorAnswerDamage() == 22 &&
            game::TypingEmperorAnswerDamage(true) == 31, "Emperor answer weight differs from position and damage");
    Require(game::TypingEmperorNetAnswerDamage() == 46 && game::TypingEmperorNetAnswerDamage(true) == 45,
            "Emperor applies its two following boss-hit passes after direct answer damage");
    Require(game::TypingEmperorFinalPathStep(0)==.1f && game::TypingEmperorFinalPathStep(4)==.05f &&
            game::TypingEmperorFinalPathStep(7)==.1f && game::TypingEmperorFinalHitQuota()==1,
            "Emperor uses the PC final orbit speed and one-answer interruption quota");
    Require(game::TypingEmperorChargeFrames(0)==370 && game::TypingEmperorChargeFrames(8)==260 &&
            game::TypingEmperorChargeFrames(15)==190 && game::TypingEmperorChargeHitQuota(1)==1 &&
            game::TypingEmperorChargeHitQuota(2)==1,
            "Emperor's long-word charge uses its PC typing window and one-word co-op interruption");
    Require(game::TypingEmperorFirstPhasePattern(2u<<4)==1 && game::TypingEmperorFirstPhasePattern(3u<<4)==2 &&
            game::TypingEmperorFirstPhasePattern(5u<<4)==2 && game::TypingEmperorFirstPhasePattern(6u<<4)==3 &&
            game::TypingEmperorFirstPhasePattern(9u<<4)==3,
            "Emperor's typing pattern distribution is independent of the gun aggression table");
    Require(game::TypingEmperorEndingBranch(0) == 1 && game::TypingEmperorEndingBranch(2) == 1 &&
            game::TypingEmperorEndingBranch(3) == 2 && game::TypingEmperorEndingBranch(4) == 2 &&
            game::TypingEmperorEndingBranch(5) == 0, "Emperor ending condition boundaries");
    std::puts("typing_boss_rules_test: OK");
}
