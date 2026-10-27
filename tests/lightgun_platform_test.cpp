#include "platform/platform.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>

static void Check(bool ok, const char* why) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}

static void NativeCursorConfinement() {
    std::string error;
    POINT originalPointer{};
    GetCursorPos(&originalPointer);
    {
        platform::Platform platform;
        platform::Options options;
        options.title = "HOTD2 cursor confinement test";
        options.input = platform::Input::RawInput;
        options.guns = true;
        options.hidden = true;
        options.width = 640;
        options.height = 480;
        Check(platform.Init(options, &error), error.c_str());
        int count = 0;
        SDL_Window** windows = SDL_GetWindows(&count);
        SDL_Window* window = nullptr;
        for (int i = 0; i < count; ++i)
            if (options.title == SDL_GetWindowTitle(windows[i])) window = windows[i];
        SDL_free(windows);
        Check(window != nullptr, "native confinement window exists");
        const auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        auto pump = [&] { for (platform::Event event; platform.PollEvent(&event);) {} };
        int focusTransition = 0;
        auto focus = [&] {
            ++focusTransition;
            Check(SDL_RaiseWindow(window), SDL_GetError());
            const bool foregroundAccepted = SetForegroundWindow(hwnd) != 0;
            auto focused = [&] { return SDL_GetKeyboardFocus() == window && GetForegroundWindow() == hwnd; };
            for (int i = 0; i < 100 && !focused(); ++i) { pump(); SDL_Delay(10); }
            pump();
            if (!focused())
                std::fprintf(stderr, "focus transition %d: SetForegroundWindow=%d, SDL focus=%d, native foreground=%d\n",
                             focusTransition, foregroundAccepted, SDL_GetKeyboardFocus() == window, GetForegroundWindow() == hwnd);
            Check(focused(), "native cursor test requires foreground access; run from an active terminal");
        };
        auto confined = [&] {
            pump();
            RECT expected{}, actual{};
            Check(GetClientRect(hwnd, &expected), "native client rectangle is readable");
            MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&expected), 2);
            Check(GetClipCursor(&actual) && EqualRect(&actual, &expected), "Windows confines the cursor to the current client rectangle");
            Check(SDL_GetWindowMouseGrab(window), "SDL owns the active mouse grab");
            Check(!SDL_GetWindowRelativeMouseMode(window) && !SDL_GetWindowKeyboardGrab(window),
                  "cursor confinement preserves absolute gun input and desktop keyboard shortcuts");
        };
        auto released = [&] {
            RECT actual{};
            RECT desktop{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0};
            desktop.right = desktop.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
            desktop.bottom = desktop.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
            Check(GetClipCursor(&actual) && EqualRect(&actual, &desktop), "native cursor confinement is released");
            Check(!SDL_GetWindowMouseGrab(window), "SDL releases the active mouse grab");
        };
        Check(platform.SetCursorConfined(true) && platform.SetCursorConfined(false), "hidden-window pending grab can be cancelled");
        Check(platform.ShowWindow(&error), error.c_str());
        focus();
        released();
        for (int height : {480, 360, 274}) {
            Check(platform.SetWindowSize(640, height), "change window aspect ratio");
            Check(platform.SetCursorConfined(true), "confine in windowed mode");
            confined();
            Check(platform.SetCursorConfined(false), "release for a menu");
            released();
        }
        Check(platform.SetCursorConfined(true), "confine before display-mode changes");
        for (bool exclusive : {false, true}) {
            Check(platform.SetFullscreen(true, exclusive), "enter borderless/exclusive fullscreen");
            focus();
            confined();
            Check(platform.SetFullscreen(false), "restore windowed mode");
            focus();
            confined();
        }
        // Focus loss must release the native clip even before the game loop clears
        // its confinement request. SDL restores it only when this window regains focus.
        SDL_Window* other = SDL_CreateWindow("HOTD2 focus target", 100, 100, 0);
        Check(other != nullptr, SDL_GetError());
        Check(SDL_RaiseWindow(other), SDL_GetError());
        for (int i = 0; i < 100 && SDL_GetKeyboardFocus() != other; ++i) { pump(); SDL_Delay(10); }
        pump();
        Check(SDL_GetKeyboardFocus() == other, "second window receives focus");
        released();
        SDL_DestroyWindow(other);
        focus();
        confined();
        platform.SetGameImage({80, 0, 480, 274});
        auto& guns = platform.Lightguns();
        guns.Feed("1111111111111111", -.1f, .5f, false, false, 1, 0);
        Check(!platform.Guns()->Player(0).onScreen && (platform.Guns()->Player(0).pressed & input::kTrigger),
              "desktop confinement leaves raw offscreen reload reports intact");
        // Leave the clip enabled: destruction must return the desktop to normal.
    }
    RECT clip{};
    Check(GetClipCursor(&clip) && clip.right - clip.left == GetSystemMetrics(SM_CXVIRTUALSCREEN) &&
              clip.bottom - clip.top == GetSystemMetrics(SM_CYVIRTUALSCREEN), "teardown releases native cursor confinement");
    SetCursorPos(originalPointer.x, originalPointer.y);
    std::puts("PASS native cursor confinement: window aspects, borderless/exclusive fullscreen, focus release/restore and teardown");
}

int main(int argc, char** argv) {
    std::string error;
    {
        platform::Platform headless;
        Check(!headless.SetCursorVisible(false) && headless.CursorVisible(), "uninitialized cursor control is a no-op");
        Check(!headless.SetCursorConfined(true), "uninitialized cursor confinement is a no-op");
        platform::Options o;
        o.window = false;
        Check(headless.Init(o, &error), error.c_str());
        Check(!headless.SetCursorVisible(false) && headless.CursorVisible(), "headless cursor control is a no-op");
        Check(!headless.SetCursorConfined(true), "headless cursor confinement is a no-op");
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
            Check(!headless.SetCursorConfined(false), "headless confinement cannot affect a window");
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
    if (argc > 1 && std::strcmp(argv[1], "--native-cursor") == 0) NativeCursorConfinement();
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    puts("PASS hidden-window input switching, stable mapped aim, cursor transitions, headless isolation and teardown restoration");
}
