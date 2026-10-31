#include "port/play/update_prompt.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
using Prompt = play::UpdatePrompt;
using Action = Prompt::Action;

void Check(bool ok, const char* why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}

play::MenuInput Key(uint32_t pressed) {
    play::MenuInput input;
    input.pressed = pressed;
    return input;
}

play::MenuInput ClickButton(const game::WorldHud& hud, const char* label) {
    const auto text = std::find_if(hud.menuText.begin(), hud.menuText.end(),
                                  [&](const auto& item) { return item.text == label; });
    Check(text != hud.menuText.end(), label);
    // Panels are painted back to front. Locate the button behind its visible label.
    const auto panel = std::find_if(hud.menuPanels.rbegin(), hud.menuPanels.rend(), [&](const auto& p) {
        return p.left <= text->x && text->x < p.right && p.top <= text->y && text->y < p.bottom;
    });
    Check(panel != hud.menuPanels.rend(), "button label has a rendered panel");
    play::MenuInput click;
    click.pointer = click.trigger = true;
    click.px = (panel->left + panel->right) * .5f;
    click.py = (panel->top + panel->bottom) * .5f;
    return click;
}

void PointerChoices() {
    struct Choice { const char* label; Action action; };
    const Choice choices[] = {
        {"LATER", Action::Later}, {"DOWNLOAD", Action::Download}, {"UPDATE NOW", Action::Install}
    };
    Prompt prompt;
    for (const auto& choice : choices) {
        prompt.Open("1.6");
        game::WorldHud hud;
        prompt.ModernHud(hud);
        const auto click = ClickButton(hud, choice.label);
        Check(prompt.Update(click) == choice.action, choice.label);
        Check(!prompt.IsOpen(), "first click dismisses the offer");
        Check(prompt.Update({}) == Action::None && prompt.Update(click) == Action::None,
              "later frames and repeated clicks cannot repeat the action");
        game::WorldHud nextFrame;
        prompt.ModernHud(nextFrame);
        Check(nextFrame.menuText.empty() && nextFrame.menuPanels.empty(), "dismissed prompt leaves the HUD");
    }
    prompt.Open("1.6");
    play::MenuInput outside;
    outside.pointer = outside.trigger = true;
    outside.pressed = 4;
    outside.px = 1;
    outside.py = 1;
    Check(prompt.Update(outside) == Action::None && prompt.IsOpen(), "outside click leaves the offer pending");
}

void KeyboardChoices() {
    Prompt prompt;
    prompt.Open("1.6");
    game::WorldHud hud;
    prompt.ModernHud(hud);
    auto resting = ClickButton(hud, "UPDATE NOW");
    resting.trigger = false;
    Check(prompt.Update(resting) == Action::None, "resting pointer does not accept an update");
    resting.pressed = 8; // Enter while the pointer has not moved since the offer opened.
    Check(prompt.Update(resting) == Action::Later && !prompt.IsOpen(), "Later is the default Enter action");

    prompt.Open("1.6");
    Check(prompt.Update(Key(0x80)) == Action::None && prompt.Update(Key(8)) == Action::Download,
          "Right then Enter chooses Download");
    prompt.Open("1.6");
    Check(prompt.Update(Key(0x40)) == Action::None && prompt.Update(Key(4)) == Action::Install,
          "Left wraps from Later to Update now, and controller confirm selects it");

    prompt.Open("1.6");
    prompt.Update(Key(0x40));
    play::MenuInput escape;
    escape.start = true; // The client maps the Escape key to MenuInput::start.
    Check(prompt.Update(escape) == Action::Later && !prompt.IsOpen(), "Escape dismisses even with Update now selected");
}

void ErrorDismissal() {
    Prompt prompt;
    prompt.ShowError("UPDATE HELPER FAILED");
    Check(prompt.IsOpen(), "an update failure opens a dismissible error");
    game::WorldHud hud;
    prompt.ModernHud(hud);
    Check(std::any_of(hud.menuText.begin(), hud.menuText.end(), [](const auto& line) {
        return line.text.find("UPDATE HELPER FAILED") != std::string::npos;
    }), "player can see the supplied failure reason");
    const auto click = ClickButton(hud, "OK");
    Check(prompt.Update(click) == Action::Later && !prompt.IsOpen(), "OK dismisses the error without retrying");
    Check(prompt.Update(click) == Action::None, "dismissing an error emits only one action");
}
} // namespace

int main() {
    PointerChoices();
    KeyboardChoices();
    ErrorDismissal();
    std::puts("PASS in-game update choices, dismissal and keyboard navigation");
}
