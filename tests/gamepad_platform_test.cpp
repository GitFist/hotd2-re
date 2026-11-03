#include "platform/platform.h"
#include "input/focused_guns.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

int checks = 0;
void Check(bool ok, const char* why) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s (%s)\n", why, SDL_GetError());
        std::exit(1);
    }
}

void Pump(platform::Platform& platform) {
    for (platform::Event event; platform.PollEvent(&event);) {}
}

bool Neutral(const input::PlayerState& state) {
    return !state.held && !state.pressed && !state.released && !state.menuHeld && !state.menuPressed;
}

// SDL's virtual driver generates its standard gamepad mapping from these masks.
// Its trigger input is the full joystick range; SDL maps that to 0..32767.
struct VirtualPad {
    SDL_JoystickID id = 0;
    SDL_Joystick* joystick = nullptr;
    int rumbleCalls = 0;
    Uint16 lowRumble = 0, highRumble = 0;

    static bool SDLCALL Rumble(void* userdata, Uint16 low, Uint16 high) {
        auto& pad = *static_cast<VirtualPad*>(userdata);
        ++pad.rumbleCalls;
        pad.lowRumble = low;
        pad.highRumble = high;
        return true;
    }

    void Attach(const char* name, Uint16 vendor, Uint16 product, SDL_GamepadType type) {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.vendor_id = vendor;
        desc.product_id = product;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1u;
        desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1u;
        desc.name = name;
        desc.userdata = this;
        desc.Rumble = Rumble;
        id = SDL_AttachVirtualJoystick(&desc);
        Check(id != 0 && SDL_IsGamepad(id), "virtual device has an SDL standard gamepad mapping");
        Check(SDL_GetGamepadTypeForID(id) == type, "SDL recognizes the virtual Xbox/PlayStation family");
        joystick = SDL_OpenJoystick(id);
        Check(joystick != nullptr, "open virtual joystick for reports");
        Axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
        Axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    }

    void Axis(SDL_GamepadAxis axis, Sint16 value) {
        Check(SDL_SetJoystickVirtualAxis(joystick, axis, value), "set virtual axis report");
        SDL_UpdateJoysticks();
    }

    void Button(SDL_GamepadButton button, bool down) {
        Check(SDL_SetJoystickVirtualButton(joystick, button, down), "set virtual button report");
        SDL_UpdateJoysticks();
    }

    void Detach() {
        if (!id) return;
        SDL_CloseJoystick(joystick);
        joystick = nullptr;
        Check(SDL_DetachVirtualJoystick(id), "detach virtual gamepad");
        id = 0;
    }

    ~VirtualPad() { Detach(); }
};

void HeadlessAndViewerIsolation() {
    Check(SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD) == 0, "test starts without SDL subsystems");
    std::string error;
    {
        platform::Platform headless;
        platform::Options options;
        options.window = false;
        options.guns = true;
        Check(headless.Init(options, &error), error.c_str());
        Check(headless.Guns() == nullptr, "headless mode has no gun source");
        Check(SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD) == 0,
              "headless gun option never initializes video or joysticks");
    }
    {
        platform::Platform viewer;
        platform::Options options;
        options.hidden = true;
        options.guns = false;
        Check(viewer.Init(options, &error), error.c_str());
        Check(viewer.Guns() == nullptr && SDL_WasInit(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD) == 0,
              "a viewer window does not initialize joysticks");
        Check(std::strcmp(SDL_GetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS), "0") == 0,
              "viewer preserves the background-gamepad hint");
    }
    Check(SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD) == 0, "viewer teardown releases its SDL subsystems");
}

void VirtualGamepads() {
    // Hide the window throughout: all reports come from SDL's virtual driver.
    Check(SDL_InitSubSystem(SDL_INIT_GAMEPAD), "initialize virtual-gamepad fixture");
    VirtualPad xbox, dualshock, dualsense;
    xbox.Attach("HOTD2 virtual Xbox One", 0x045e, 0x02ea, SDL_GAMEPAD_TYPE_XBOXONE);
    xbox.Button(SDL_GAMEPAD_BUTTON_START, true);
    xbox.Button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    xbox.Axis(SDL_GAMEPAD_AXIS_RIGHTX, SDL_JOYSTICK_AXIS_MAX);
    // Existing-device enumeration must work without relying on an ADDED event.
    SDL_FlushEvent(SDL_EVENT_GAMEPAD_ADDED);

    SDL_JoystickID lastPad = 0;
    {
        platform::Platform platform;
        platform::Options options;
        options.title = "HOTD2 hidden virtual gamepad test";
        options.input = platform::Input::Mouse;
        options.guns = true;
        options.hidden = true;
        options.width = 640;
        options.height = 480;
        std::string error;
        Check(platform.Init(options, &error), error.c_str());
        Check(!platform.RumbleGamepad(-1, 1, 1, 50) && !platform.RumbleGamepad(2, 1, 1, 50) &&
                  !platform.RumbleGamepad(1, 1, 1, 50),
              "rumble rejects invalid and unoccupied local slots");
        Check(platform.RumbleGamepad(0, 12000, 24000, 1000) && xbox.lowRumble == 12000 && xbox.highRumble == 24000,
              "native rumble reaches the assigned Xbox with both strengths intact");
        platform.StopGamepadRumble();
        Check(xbox.lowRumble == 0 && xbox.highRumble == 0, "explicit stop silences native gamepad rumble");
        input::GunSource* source = platform.Guns();
        Check(source != nullptr, "gamepad wrapper exists");
        Check(source->Player(0).gamepad && source->Player(0).connected &&
                  (source->Player(0).held & (input::kTrigger | input::kStart)) == (input::kTrigger | input::kStart),
              "opening a preexisting Xbox samples held Start and South");
        SDL_Delay(20);
        Pump(platform);
        Check(source->Player(0).x > .5f, "opening a preexisting Xbox samples its current stick axis");
        source->BeginFrame();
        xbox.Button(SDL_GAMEPAD_BUTTON_START, false);
        xbox.Button(SDL_GAMEPAD_BUTTON_SOUTH, false);
        xbox.Axis(SDL_GAMEPAD_AXIS_RIGHTX, 0);
        Pump(platform);
        Check(!source->Player(0).held && (source->Player(0).released & input::kTrigger), "Xbox South release clears the sampled fire button");

        dualshock.Attach("HOTD2 virtual DualShock 4", 0x054c, 0x05c4, SDL_GAMEPAD_TYPE_PS4);
        Pump(platform);
        Check(!source->Player(1).gamepad, "idle hotplugged pad does not take over the fallback player");
        source->BeginFrame();
        dualshock.Button(SDL_GAMEPAD_BUTTON_SOUTH, true);
        Pump(platform);
        Check(source->Player(1).gamepad && (source->Player(1).held & input::kTrigger) && (source->Player(1).menuPressed & 8),
              "DualShock Cross maps to fire and menu confirm for player two");
        Check(!source->Player(0).held, "two gamepads keep independent player buttons");
        const int xboxRumbles = xbox.rumbleCalls;
        Check(platform.RumbleGamepad(1, 30000, 9000, 1000) && dualshock.lowRumble == 30000 && dualshock.highRumble == 9000 &&
                  xbox.rumbleCalls == xboxRumbles,
              "player-two rumble reaches only the assigned DualShock");
        Check(platform.RumbleGamepad(1, 0, 0, 0) && dualshock.lowRumble == 0 && dualshock.highRumble == 0,
              "zero strengths stop one pad independently");
#ifdef _WIN32
        int windowCount = 0;
        SDL_Window** windows = SDL_GetWindows(&windowCount);
        Check(windowCount == 1, "hidden fixture owns one window for native focus messages");
        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(windows[0]), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        SDL_free(windows);
        Check(hwnd != nullptr, "hidden fixture exposes its native window");
        Check(platform.RumbleGamepad(0, 11111, 22222, 1000) && platform.RumbleGamepad(1, 33333, 44444, 1000),
              "both local feedback channels can be active");
        SendMessageW(hwnd, WM_ACTIVATEAPP, FALSE, 0);
        Pump(platform);
        Check(!xbox.lowRumble && !xbox.highRumble && !dualshock.lowRumble && !dualshock.highRumble,
              "native focus loss stops both feedback channels");
        const int inactiveRumbles = xbox.rumbleCalls;
        Check(!platform.RumbleGamepad(0, 1000, 1000, 1000) && xbox.rumbleCalls == inactiveRumbles,
              "inactive application cannot start gamepad feedback");
        SendMessageW(hwnd, WM_ACTIVATEAPP, TRUE, 0);
        Pump(platform);
#endif
        dualshock.Button(SDL_GAMEPAD_BUTTON_SOUTH, false);
        Pump(platform);

        source->BeginFrame();
        xbox.Button(SDL_GAMEPAD_BUTTON_SOUTH, true);
        xbox.Button(SDL_GAMEPAD_BUTTON_SOUTH, false);
        Pump(platform);
        Check((source->Player(0).pressed & input::kTrigger) && (source->Player(0).released & input::kTrigger) &&
                  !(source->Player(0).held & input::kTrigger) && (source->Player(0).menuPressed & 8),
              "button press/release inside one poll retains the tap edges");
        source->BeginFrame();
        xbox.Axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MAX);
        xbox.Axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
        Pump(platform);
        Check((source->Player(0).pressed & input::kTrigger) && (source->Player(0).released & input::kTrigger) && !source->Player(0).held,
              "analog-trigger tap retains both edges in one poll");

        source->BeginFrame();
        dualshock.Button(SDL_GAMEPAD_BUTTON_DPAD_UP, true);
        dualshock.Button(SDL_GAMEPAD_BUTTON_EAST, true);
        Pump(platform);
        Check((source->Player(1).menuHeld & 0x10) && (source->Player(1).menuPressed & 2) && (source->Player(1).held & input::kReload),
              "DualShock D-pad and Circle map to navigation, cancel and reload");
        dualshock.Button(SDL_GAMEPAD_BUTTON_DPAD_UP, false);
        dualshock.Button(SDL_GAMEPAD_BUTTON_EAST, false);
        Pump(platform);

        const float before = source->Player(0).x;
        xbox.Axis(SDL_GAMEPAD_AXIS_RIGHTX, SDL_JOYSTICK_AXIS_MAX);
        SDL_Delay(30);
        Pump(platform);
        const float right = source->Player(0).x;
        Check(right > before && right <= 1.0f, "normalized right stick moves aim over elapsed frame time");
        xbox.Axis(SDL_GAMEPAD_AXIS_RIGHTX, 0);
        Pump(platform);
        const auto aim = source->Player(0);
        platform.SetGameImage({80, 0, 480, 480});
        Check(source->Player(0).x == aim.x && source->Player(0).y == aim.y && source->Player(0).onScreen,
              "gamepad aim is already in game-image space and is not letterbox-transformed");
#ifdef _WIN32
        Check(platform.SetInput(platform::Input::RawInput, &error), error.c_str());
        Check(platform.Guns() == source && source->Player(0).gamepad && source->Player(0).x == aim.x,
              "switching fallback to raw input preserves wrapper identity and gamepad aim");
        Check(platform.SetInput(platform::Input::Mouse, &error), error.c_str());
        Check(platform.Guns() == source && source->Player(1).gamepad, "switching fallback back to mouse preserves both gamepad slots");
#endif

        input::FocusedGuns focused(*source);
        source->BeginFrame();
        focused.UpdateFocus(false);
        xbox.Button(SDL_GAMEPAD_BUTTON_SOUTH, true);
        Pump(platform);
        focused.UpdateFocus(false);
        Check(Neutral(focused.Player(0)) && focused.Player(0).gamepad, "background gamepad input is neutral while metadata remains available");
        focused.UpdateFocus(true);
        Check(Neutral(focused.Player(0)), "refocusing with Cross/A held does not fire or confirm");
        xbox.Button(SDL_GAMEPAD_BUTTON_SOUTH, false);
        Pump(platform);
        focused.UpdateFocus(true);
        focused.BeginFrame();
        xbox.Button(SDL_GAMEPAD_BUTTON_SOUTH, true);
        Pump(platform);
        focused.UpdateFocus(true);
        Check((focused.Player(0).pressed & input::kTrigger) && (focused.Player(0).menuPressed & 8), "fresh gamepad input works after refocus and release");
        xbox.Button(SDL_GAMEPAD_BUTTON_SOUTH, false);
        Pump(platform);

        dualsense.Attach("HOTD2 virtual DualSense", 0x054c, 0x0ce6, SDL_GAMEPAD_TYPE_PS5);
        dualsense.Button(SDL_GAMEPAD_BUTTON_WEST, true);
        Pump(platform);
        Check(!source->Player(0).held && !source->Player(1).held, "third connected pad cannot overwrite either occupied player slot");
        xbox.Detach();
        Pump(platform);
        Check(source->Player(0).gamepad && (source->Player(0).held & input::kReload), "waiting DualSense reuses the freed player-one slot and samples held Square");
        const int survivingRumbles = dualshock.rumbleCalls;
        Check(platform.RumbleGamepad(0, 14000, 28000, 1000) && dualsense.lowRumble == 14000 && dualsense.highRumble == 28000 &&
                  dualshock.rumbleCalls == survivingRumbles,
              "rumble follows replacement in the freed slot without renumbering player two");
        source->BeginFrame();
        dualshock.Button(SDL_GAMEPAD_BUTTON_SOUTH, true);
        Pump(platform);
        Check((source->Player(1).held & input::kTrigger) && !(source->Player(0).held & input::kTrigger), "removing player one never renumbers player two");
        dualsense.Detach();
        Pump(platform);
        Check(!source->Player(0).gamepad && !source->Player(0).held, "disconnect clears gamepad ownership and held buttons");
        Check(source->Player(1).gamepad && (source->Player(1).held & input::kTrigger), "other player's held state survives disconnect");
        Check(!platform.RumbleGamepad(0, 1000, 1000, 1000), "disconnected local slot cannot receive feedback");
        Check(platform.RumbleGamepad(1, 11000, 22000, 1000), "feedback remains available for the surviving player");
        lastPad = dualshock.id;
    }
    Check(!dualshock.lowRumble && !dualshock.highRumble, "platform teardown stops feedback before closing the surviving pad");
    Check(SDL_GetGamepadFromID(lastPad) == nullptr, "platform teardown closes its SDL gamepad handles");
    Check(std::strcmp(SDL_GetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS), "0") == 0, "platform teardown restores the previous background-events hint");
    dualshock.Detach();
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    Check(SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD) == 0, "virtual fixture and platform release all SDL subsystems");
}

}  // namespace

int main() {
    // Keep physical controllers from competing with deterministic virtual slots.
    // These hints apply only to this test process, never to the game executable.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_JOYSTICK_DIRECTINPUT, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "0");
    SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "0");
#endif
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0", SDL_HINT_OVERRIDE);
    HeadlessAndViewerIsolation();
    VirtualGamepads();
    std::printf("Gamepad platform: %d checks passed (hidden virtual Xbox, DualShock 4 and DualSense)\n", checks);
    return 0;
}
