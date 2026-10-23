#include <cstdio>
#include <cstdlib>
#include <string>

#include "play/run_name_prompt.h"

namespace {
using Prompt = play::RunNamePrompt;
using Action = Prompt::Action;

void Require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

play::MenuInput Key(uint32_t pressed) { play::MenuInput in; in.pressed = pressed; return in; }
play::MenuInput Click(float x, float y) {
    play::MenuInput in;
    in.pointer = in.trigger = true;
    in.px = x; in.py = y; in.pressed = 4;
    return in;
}
bool HasText(const game::WorldHud& hud, const std::string& text) {
    for (const auto& item : hud.menuText) if (item.text == text) return true;
    return false;
}
game::WorldHud Front(int mode = 4) { game::WorldHud hud; hud.gameMode = mode; return hud; }
void Open(Prompt& prompt, uint64_t token = 1, const std::string& initial = {}) {
    Require(prompt.Queue(token, 0, 12345, 0, initial), "queue a fresh eligible run");
    Require(prompt.MaybeOpen(Front()), "open after the result flow finishes");
}

void Readiness() {
    Prompt prompt;
    Require(!prompt.Queue(0, 0, 100, 0) && !prompt.Queue(1, -1, 100, 0) &&
            !prompt.Queue(1, 13, 100, 0) && !prompt.Queue(1, 0, 0, 0), "reject invalid records");
    Require(!prompt.MaybeOpen(Front()), "no run means no automatic prompt");
    Require(prompt.Queue(7, 0, 100, 0), "queue after rejecting invalid records");
    for (int mode = 0; mode <= 17; ++mode) {
        auto hud = Front(mode);
        const bool expected = mode == 3 || mode == 4 || mode == 15;
        Require(Prompt::Ready(hud) == expected, "only title, menus and lobby are ready in normal modes");
        hud.stageResults.active = true;
        Require(!Prompt::Ready(hud), "active stage results always defer the prompt");
    }
    auto boss = Front(6);
    boss.gameType = 3; boss.subState = 2; boss.flags5900 = 0x18;
    Require(Prompt::Ready(boss), "boss selector is ready after a completed time trial");
    boss.flags5900 = 8;
    Require(!Prompt::Ready(boss), "a boss result transition is not the selector");
    boss.flags5900 = 0x18; boss.subState = 3;
    Require(!Prompt::Ready(boss), "the boss continue prompt is never intercepted");
    boss.subState = 2; boss.gameType = 0;
    Require(!Prompt::Ready(boss), "Arcade gameplay cannot open the prompt");
    Require(!prompt.MaybeOpen(Front(7)), "queue waits through game over and initials");
    Require(!prompt.MaybeOpen(Front(), true), "another local menu may defer opening");
    Require(prompt.MaybeOpen(Front(3)), "deferred prompt opens on the title for Classic menus too");
    Require(!prompt.MaybeOpen(Front()), "opening consumes the queue once");
    Require(prompt.Update(Key(2)) == Action::Skip && !prompt.IsOpen(), "Back skips without a publication");
    Require(!prompt.Queue(7, 0, 100, 0) && !prompt.Queue(6, 0, 100, 0), "old completion snapshots cannot rearm");
    Require(!prompt.MaybeOpen(Front()), "a skipped record stays dismissed");
    Require(prompt.Queue(8, 0, 100, 0), "a later completed run arms independently");
    prompt.Reset();
    Require(prompt.Token() == 0 && !prompt.MaybeOpen(Front()) && !prompt.Queue(8, 0, 100, 0),
            "reset discards the pending record and rejects its stale snapshot");
    std::puts("PASS completion readiness, defer, once per token and reset");
}

void Editing() {
    Prompt prompt;
    prompt.Edit("CLOSED");
    Require(prompt.Name().empty(), "typing outside the prompt is ignored");
    Open(prompt);
    Require(prompt.Update(Click(380, 386)) == Action::None, "blank names cannot publish");
    prompt.Edit("   ");
    Require(prompt.Update(Key(8)) == Action::None, "spaces alone cannot publish");
    prompt.Update(Click(250, 386));
    prompt.Edit(std::string("a.z_9 -!?\t\n") + char(0) + "\xC3\xA9");
    Require(prompt.Name() == "A.Z_9 -", "typing normalizes allowed ASCII and rejects controls and unsupported characters");
    prompt.Edit({}, true);
    Require(prompt.Name() == "A.Z_9 ", "Backspace removes one character");
    prompt.Update(Click(120, 386));
    Require(prompt.Name() == "A.Z_9", "Delete removes one character");
    prompt.Update(Click(250, 386));
    prompt.Edit({}, true);
    prompt.Update(Click(120, 386));
    Require(prompt.Name().empty(), "delete is safe at zero characters");
    prompt.Edit("abcdefghijklmnop");
    Require(prompt.Name() == "ABCDEFGHIJKL", "typed names stop at twelve characters");
    prompt.Update(Click(80, 200));
    Require(prompt.Name() == "ABCDEFGHIJKL", "pointer entry has the same length limit");
    prompt.Update(Click(250, 386));
    prompt.Edit("  a.b_9-  ");
    Require(prompt.Update(Click(630, 465)) == Action::None, "an empty-space shot cannot publish the selected action");
    Require(prompt.Update(Key(8)) == Action::Publish && prompt.Name() == "A.B_9-", "Enter publishes a trimmed typed name");
    Require(prompt.Update(Key(8)) == Action::None && prompt.Update(Click(380, 386)) == Action::None,
            "publishing immediately prevents double submissions");
    prompt.Edit("BUSY", true);
    prompt.Update(Click(250, 386));
    Require(prompt.Name() == "A.B_9-", "in-flight input cannot change the name or payload");
    std::puts("PASS twelve-character typing, validation, trimming and duplicate-submit guard");
}

void PointerAndPad() {
    Prompt prompt;
    Open(prompt);
    const std::string keys = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-";
    for (size_t i = 0; i < keys.size(); ++i) {
        prompt.Update(Click(250, 386));
        prompt.Update(Click(80 + int(i % 10) * 52, 200 + int(i / 10) * 40));
        Require(prompt.Name() == keys.substr(i, 1), "every on-screen key enters the intended character");
    }
    prompt.Update(Click(250, 386));
    auto aim = Click(80, 200);
    prompt.Update(aim);
    aim.trigger = false; aim.pressed = 0x80;
    prompt.Update(aim);
    aim.pressed = 8;
    prompt.Update(aim);
    Require(prompt.Name() == "AB", "a stationary pointer does not undo controller navigation");
    prompt.Update(Key(0x20)); prompt.Update(Key(8));
    prompt.Update(Key(0x40)); prompt.Update(Key(4));
    prompt.Update(Key(0x10)); prompt.Update(Key(8));
    Require(prompt.Name() == "ABLKA", "pad moves through all four directions of the key grid");
    prompt.Update(Click(250, 386));
    prompt.Update(Key(0x10)); prompt.Update(Key(4));
    Require(prompt.Name() == "7", "Up maps Clear to the last key row");
    prompt.Update(Key(0x20)); prompt.Update(Key(4));
    Require(prompt.Name().empty(), "Down returns to Clear");
    auto outside = Click(80, 200); outside.pointer = false;
    prompt.Update(outside);
    Require(prompt.Name().empty(), "offscreen trigger cannot enter a key");
    Require(prompt.Update(Click(500, 386)) == Action::Skip && !prompt.IsOpen(), "on-screen Skip dismisses");
    Open(prompt, 2);
    prompt.Update(Key(0x40));
    Require(prompt.Update(Key(8)) == Action::Skip, "Left wraps from the first key to Skip");
    Open(prompt, 3);
    play::MenuInput reload; reload.reload = true;
    Require(prompt.Update(reload) == Action::Skip, "gun reload skips");
    std::puts("PASS keyboard/controller geometry, all forty keys, pointer focus and Skip");
}

void SubmissionLifecycle() {
    Prompt prompt;
    Open(prompt, 1, "long_name-12");
    Require(prompt.Name() == "LONG_NAME-12" && prompt.Update(Key(8)) == Action::Publish,
            "a remembered name may be published immediately");
    prompt.BeginSubmitting();
    prompt.FinishSubmission(false, "SERVER UNAVAILABLE");
    Require(prompt.IsOpen() && prompt.Name() == "LONG_NAME-12", "failure keeps the name and prompt");
    game::WorldHud hud;
    prompt.ModernHud(hud);
    Require(HasText(hud, "SERVER UNAVAILABLE") && HasText(hud, "RETRY"), "failure gives a visible Retry action");
    prompt.Edit("Z", true);
    prompt.Update(Click(250, 386));
    Require(prompt.Name() == "LONG_NAME-12", "retry preserves the server's immutable submission payload");
    Require(prompt.Update(Click(380, 386)) == Action::Publish, "Retry requests the same record explicitly");
    prompt.FinishSubmission(true);
    Require(prompt.IsOpen() && !prompt.MaybeOpen(Front()), "accepted record remains in one confirmation dialog");
    prompt.ModernHud(hud);
    Require(HasText(hud, "RUN PUBLISHED") && HasText(hud, "DONE") && !HasText(hud, "RETRY"), "success confirms publication");
    Require(prompt.Update(Click(630, 465)) == Action::None, "blank click does not dismiss the success confirmation");
    Require(prompt.Update(Click(300, 360)) == Action::Skip && !prompt.IsOpen(), "Done closes without another publication");
    Require(!prompt.MaybeOpen(Front()) && !prompt.Queue(1, 0, 100, 0), "success does not reopen on later snapshots");
    Open(prompt, 2);
    Require(prompt.Name() == "LONG_NAME-12", "a later run remembers the online name independently");
    prompt.Update(Key(8));
    Require(prompt.Update(Click(500, 386)) == Action::Skip, "busy prompt can be dismissed");
    prompt.FinishSubmission(true);
    Require(!prompt.IsOpen(), "a late acknowledgement does not reopen a skipped dialog");
    Open(prompt, 3);
    Require(prompt.Queue(4, 0, 9000, 0) && !prompt.IsOpen(), "a new token supersedes a stale open dialog");
    Require(prompt.MaybeOpen(Front()) && prompt.Token() == 4, "replacement opens with the new token only");
    prompt.Reset();
    prompt.FinishSubmission(false, "OLD ERROR");
    Require(!prompt.IsOpen() && !prompt.MaybeOpen(Front()), "reset ignores late results and clears pending state");
    std::puts("PASS retry, immutable payload, success, late acknowledgement and cancellation");
}

void Presentation() {
    Prompt prompt;
    Require(prompt.Queue(1, 2, 2147483647u, 65535, "TWELVE_CHARS") && prompt.MaybeOpen(Front()), "queue Horde summary");
    game::WorldHud hud;
    hud.gameMode = 6; hud.gameType = 1; hud.subState = 2; hud.portLeaderboardInitials = "OLD";
    hud.menuText.push_back({0, 0, "OLD MENU", 1, 0});
    hud.menuScrollHints.push_back({1, 2, 3, 4, true, true});
    prompt.ModernHud(hud);
    Require(HasText(hud, "RUN COMPLETE") && HasText(hud, "HORDE / SCORE 2147483647 / ROUND 65535") &&
            HasText(hud, "12 / 12") && !HasText(hud, "OLD MENU") && hud.menuScrollHints.empty(),
            "summary, complete name counter and clean modal chrome are visible");
    Require(hud.gameMode == 6 && hud.gameType == 1 && hud.subState == 2 && hud.portLeaderboardInitials == "OLD",
            "the local overlay leaves simulation and native name state unchanged");
    Require(prompt.Queue(2, 12, 7362, 0) && prompt.MaybeOpen(Front()), "queue Boss Rush summary");
    prompt.ModernHud(hud);
    Require(HasText(hud, "BOSS RUSH / 2:02.70"), "boss results display elapsed frame time");
    prompt.Update(Key(8));
    prompt.ModernHud(hud);
    Require(HasText(hud, "PUBLISHING YOUR RUN..."), "request activity is visible");
    const size_t noPointerPanels = hud.menuPanels.size();
    auto aim = Click(600, 440); aim.trigger = false; aim.pressed = 0;
    prompt.Update(aim); prompt.ModernHud(hud);
    Require(hud.menuPanels.size() == noPointerPanels + 8 && hud.menuPanels.back().bottom == 450,
            "a visible pointer gets four outlined arms on top of the modal panels");
    aim.px = 640;
    prompt.Update(aim); prompt.ModernHud(hud);
    Require(hud.menuPanels.size() == noPointerPanels, "the pointer hides outside the game image");
    aim.px = 600; aim.pointer = false;
    prompt.Update(aim); prompt.ModernHud(hud);
    Require(hud.menuPanels.size() == noPointerPanels, "an absent pointer has no reticle");
    Prompt typed;
    Open(typed);
    typed.Edit("TYPED");
    aim.pointer = true; aim.px = 80; aim.py = 200; aim.pressed = 8;
    Require(typed.Update(aim) == Action::Publish && typed.Name() == "TYPED",
            "first-sample resting aim cannot replace the typed Enter-to-publish selection");
    std::puts("PASS score/time summary, modal replacement and presentation-only state");
}
}  // namespace

int main() {
    Readiness();
    Editing();
    PointerAndPad();
    SubmissionLifecycle();
    Presentation();
    return 0;
}
