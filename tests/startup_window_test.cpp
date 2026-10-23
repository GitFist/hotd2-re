// Native visibility order matters: checking the final fullscreen state alone misses a startup title-bar flash.
#include "platform/platform.h"

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

static void Check(bool ok, const char* why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}

struct FirstShow {
    HWND hwnd = nullptr;
    WNDPROC previous = nullptr;
    int visibilityRequests = 0;
    LONG_PTR style = 0;
};
static FirstShow* watching = nullptr;
static constexpr const char* kStyleHints[] = {"SDL_BORDERLESS_WINDOWED_STYLE", "SDL_BORDERLESS_RESIZABLE_STYLE"};

static std::optional<std::string> CopyString(const char* value) {
    return value ? std::optional<std::string>(value) : std::nullopt;
}

static LRESULT CALLBACK WatchVisibility(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto& state = *watching;
    bool show = message == WM_SHOWWINDOW && wParam != 0;
    if (message == WM_WINDOWPOSCHANGING && lParam)
        show |= (reinterpret_cast<const WINDOWPOS*>(lParam)->flags & SWP_SHOWWINDOW) != 0;
    if (hwnd == state.hwnd && show && state.visibilityRequests++ == 0)
        state.style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    return CallWindowProcW(state.previous, hwnd, message, wParam, lParam);
}

static SDL_Window* FindTestWindow(const std::string& title) {
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    SDL_Window* found = nullptr;
    for (int i = 0; i < count; ++i)
        if (title == SDL_GetWindowTitle(windows[i])) found = windows[i];
    SDL_free(windows);
    return found;
}

static void CheckWindow(bool fullscreen) {
    std::string error;
    platform::Platform platform;
    platform::Options options;
    options.title = fullscreen ? "HOTD2 startup fullscreen test" : "HOTD2 startup windowed test";
    options.width = 1280;
    options.height = 960;
    options.hidden = true;
    options.input = platform::Input::Mouse;
    Check(platform.Init(options, &error), error.c_str());
    SDL_Window* window = FindTestWindow(options.title);
    Check(window != nullptr, "created window can be inspected through SDL");
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    Check(hwnd != nullptr, "created SDL window has a native handle");
    Check(!IsWindowVisible(hwnd) && (SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN), "Init never exposes the initial startup window");

    FirstShow observation;
    observation.hwnd = hwnd;
    watching = &observation;
    observation.previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WatchVisibility)));
    Check(observation.previous != nullptr, "native visibility observer installed");
    Check(platform.SetFullscreen(fullscreen), "requested startup display mode is accepted while hidden");
    Check(!IsWindowVisible(hwnd) && (SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN) && observation.visibilityRequests == 0,
          "pending fullscreen configuration never makes the initial window visible");

    const std::optional<std::string> hintsBefore[] = {CopyString(SDL_GetHint(kStyleHints[0])), CopyString(SDL_GetHint(kStyleHints[1]))};
    Check(platform.ShowWindow(&error), error.c_str());
    for (int i = 0; i < 2; ++i)
        Check(CopyString(SDL_GetHint(kStyleHints[i])) == hintsBefore[i], "first reveal restores every prior effective borderless hint");
    Check(observation.visibilityRequests > 0, "native first-show transition was observed synchronously");
    Check(IsWindowVisible(hwnd) && !(SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN), "ShowWindow reveals the window");
    const LONG_PTR decorations = WS_CAPTION | WS_THICKFRAME;
    RECT client{}, outer{};
    Check(GetClientRect(hwnd, &client) && GetWindowRect(hwnd, &outer), "native window dimensions are readable");
    if (fullscreen) {
        Check((observation.style & decorations) == 0, "first visible fullscreen transition has no caption or resize frame");
        Check(platform.IsFullscreen() && (GetWindowLongPtrW(hwnd, GWL_STYLE) & decorations) == 0,
              "revealed fullscreen window remains free of native decorations");
        SDL_Rect bounds{};
        Check(SDL_GetDisplayBounds(SDL_GetDisplayForWindow(window), &bounds), SDL_GetError());
        Check(client.right == bounds.w && client.bottom == bounds.h, "fullscreen client uses final display dimensions before GPU creation");
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        Check(GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor), "native monitor dimensions are readable");
        Check(EqualRect(&outer, &monitor.rcMonitor), "fullscreen outer rectangle exactly fills the native display");
    } else {
        Check(!platform.IsFullscreen() && (observation.style & decorations) == decorations,
              "ordinary windowed startup retains its native title bar and resize frame");
        Check(client.right == options.width && client.bottom == options.height, "windowed startup preserves requested client size");
    }
    const int requests = observation.visibilityRequests;
    Check(platform.ShowWindow(&error), error.c_str());
    RECT repeated{};
    Check(GetWindowRect(hwnd, &repeated) && EqualRect(&outer, &repeated) && observation.visibilityRequests == requests,
          "repeated ShowWindow is harmless and does not repeat native visibility transitions");
    for (int i = 0; i < 2; ++i)
        Check(CopyString(SDL_GetHint(kStyleHints[i])) == hintsBefore[i], "repeated reveal leaves borderless hints unchanged");
    if (fullscreen) {
        Check(platform.SetFullscreen(false), "leaving startup fullscreen succeeds");
        Check(!platform.IsFullscreen() && (GetWindowLongPtrW(hwnd, GWL_STYLE) & decorations) == decorations,
              "leaving fullscreen restores ordinary window decorations");
        Check(GetClientRect(hwnd, &client) && client.right == options.width && client.bottom == options.height,
              "leaving fullscreen restores the original windowed client size");
    }
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(observation.previous));
    watching = nullptr;
}
#endif

int main() {
#ifdef _WIN32
    std::string error;
    Check(SDL_WasInit(SDL_INIT_VIDEO) == 0, "test starts without SDL video");
    {
        platform::Platform headless;
        Check(headless.ShowWindow(&error), "uninitialized ShowWindow is a safe no-op");
        platform::Options options;
        options.window = false;
        Check(headless.Init(options, &error) && headless.ShowWindow(&error), "headless ShowWindow is a safe no-op");
        Check(SDL_WasInit(SDL_INIT_VIDEO) == 0, "headless ShowWindow does not initialize SDL video");
    }
    SDL_Environment* environment = SDL_GetEnvironment();
    Check(environment != nullptr, "SDL environment is available for isolated hint fixtures");
    std::optional<std::string> oldEnvironment[2], oldHints[2];
    for (int i = 0; i < 2; ++i) {
        oldEnvironment[i] = CopyString(SDL_GetEnvironmentVariable(environment, kStyleHints[i]));
        oldHints[i] = CopyString(SDL_GetHint(kStyleHints[i]));
        Check(SDL_UnsetEnvironmentVariable(environment, kStyleHints[i]), "remove inherited test hint environment");
        SDL_ResetHint(kStyleHints[i]);
        Check(SDL_GetHint(kStyleHints[i]) == nullptr, "fullscreen fixture begins with unset borderless hints");
    }
    CheckWindow(true);  // Unset hints must remain unset, preserving SDL defaults after the temporary override.
    for (const char* hint : kStyleHints) Check(SDL_SetHint(hint, "1"), "configure existing application hint");
    CheckWindow(false);  // Windowed reveal must not alter existing application values.
    for (const char* hint : kStyleHints) {
        SDL_ResetHint(hint);
        Check(SDL_SetEnvironmentVariable(environment, hint, "1", true), "configure environment-priority style hints");
    }
    Check(SDL_SetHintWithPriority(kStyleHints[1], "0", SDL_HINT_OVERRIDE), "configure an existing override above the environment");
    CheckWindow(true);  // Covers both an environment value and an explicit override disagreeing with its environment.
    for (int i = 0; i < 2; ++i) {
        if (oldEnvironment[i]) SDL_SetEnvironmentVariable(environment, kStyleHints[i], oldEnvironment[i]->c_str(), true);
        else SDL_UnsetEnvironmentVariable(environment, kStyleHints[i]);
        SDL_ResetHint(kStyleHints[i]);
        if (oldHints[i] && CopyString(SDL_GetHint(kStyleHints[i])) != oldHints[i])
            SDL_SetHintWithPriority(kStyleHints[i], oldHints[i]->c_str(), SDL_HINT_OVERRIDE);
    }
    std::puts("PASS native startup visibility: hidden configuration, first-show decorations, fullscreen dimensions, windowed restoration, hint restoration and headless isolation");
#else
    std::puts("SKIP native Windows startup visibility test");
#endif
}
