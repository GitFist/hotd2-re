#include "platform/platform.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

static void Check(bool ok, const char* why) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}

int main() {
    std::string error;
    {
        platform::Platform headless;
        Check(!headless.SetCursorVisible(false) && headless.CursorVisible(), "uninitialized cursor control is a no-op");
        platform::Options o;
        o.window = false;
        Check(headless.Init(o, &error), error.c_str());
        Check(!headless.SetCursorVisible(false) && headless.CursorVisible(), "headless cursor control is a no-op");
        Check(SDL_WasInit(SDL_INIT_VIDEO) == 0, "headless cursor calls do not initialize SDL video");
    }

    // Keep video initialized independently so SDL's actual cursor state can be checked after Platform teardown.
    Check(SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS), SDL_GetError());
    Check(SDL_ShowCursor(), SDL_GetError());
    {
        platform::Platform platform;
        platform::Options o;
        o.input = platform::Input::Mouse;
        o.guns = true;
        o.hidden = true;
        o.width = 640;
        o.height = 480;
        Check(platform.Init(o, &error), error.c_str());
        auto* source = platform.Guns();
        Check(source != nullptr, "window gun source created");
        Check(platform.CursorVisible() && SDL_CursorVisible(), "window starts with a visible system cursor");
        for (int i = 0; i < 3; ++i) {
            Check(platform.SetCursorVisible(false) && !platform.CursorVisible() && !SDL_CursorVisible(), "system cursor hides");
            Check(platform.SetCursorVisible(false) && !SDL_CursorVisible(), "repeated hide preserves hidden state");
            Check(platform.SetCursorVisible(true) && platform.CursorVisible() && SDL_CursorVisible(), "system cursor restores");
            Check(platform.SetCursorVisible(true) && SDL_CursorVisible(), "repeated show preserves visible state");
        }

        platform.SetGameImage({80, 0, 480, 480});
        Check(platform.SetCursorVisible(false), "hide before input switch");
        Check(platform.SetInput(platform::Input::RawInput, &error), error.c_str());
        Check(platform.Guns() == source && platform.CurrentInput() == platform::Input::RawInput,
              "runtime switch preserves wrapper pointer");
        Check(!platform.CursorVisible(), "light-gun switch preserves hidden cursor");
        auto& guns = platform.Lightguns();
        guns.Feed("1111111111111111", .3125, .4, true, false, 1, 0);
        Check(source->Player(0).connected && std::abs(source->Player(0).x - .25) < .001, "hidden cursor preserves mapped raw aim");

        // A headless capture in the same process must not change another window's cursor, even on destruction.
        {
            platform::Platform headless;
            platform::Options offscreen;
            offscreen.window = false;
            Check(headless.Init(offscreen, &error), error.c_str());
            Check(!headless.SetCursorVisible(true) && !SDL_CursorVisible(), "headless show cannot affect a window cursor");
            Check(!headless.SetCursorVisible(false) && !SDL_CursorVisible(), "headless hide cannot affect a window cursor");
        }
        Check(!SDL_CursorVisible(), "headless teardown leaves window cursor alone");
        Check(platform.SetCursorVisible(true), "restore before switching to mouse");
        Check(platform.SetInput(platform::Input::Mouse, &error), error.c_str());
        Check(platform.Guns() == source && !source->Player(0).connected, "mouse mode clears raw device buttons");
        Check(platform.CursorVisible(), "mouse switch preserves visible cursor");
        Check(platform.SetInput(platform::Input::RawInput, &error), error.c_str());
        guns.Feed("1111111111111111", .5, .5, true, false, 1, 0);
        Check(source->Player(0).connected, "second raw registration works");
        Check(!platform.SetInput(platform::Input::Touch, &error) && platform.CurrentInput() == platform::Input::RawInput,
              "unsupported switch leaves active input intact");
        Check(platform.SetCursorVisible(false) && !SDL_CursorVisible(), "hide for teardown restoration check");
    }
    Check(SDL_CursorVisible(), "window teardown restores hidden system cursor before releasing video");
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    puts("PASS hidden-window input switching, stable mapped aim, cursor transitions, headless isolation and teardown restoration");
}
