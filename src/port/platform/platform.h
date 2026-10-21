#pragma once

// Platform layer of the native port, on SDL3 (notes/agents/platform.md): window, event loop, clock, keyboard,
// gun input sources (Windows raw input / SDL mouse / touch), GPU device creation (D3D11 / SDL_GPU), sound output
// (SDL3 / XAudio2) and PNG output. The tools (play, scene_viewer, model_viewer) only talk to this header, the
// renderer (render/model_renderer.h) and the game library; no SDL / Win32 types leak into them.
//
// Headless use (screenshots): Options::window = false creates no window and does not initialise SDL video; the GPU
// device then renders off-screen and ReadPixels + SavePng write the image.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio/audio_output.h"
#include "input/aim_map.h"
#include "input/player_state.h"
#include "input/lightguns.h"
#include "render/gpu_device.h"

namespace platform {

enum class Renderer { D3D11, SdlGpu, D3D12 };
enum class Input { Auto, RawInput, Mouse, Touch };  // Auto: raw input on Windows, else mouse (desktop) / touch (mobile)
enum class Audio { Sdl, XAudio2 };

Renderer DefaultRenderer();  // D3D11 on Windows, SDL_GPU elsewhere
bool ParseRenderer(const std::string& s, Renderer* out);  // "d3d11" | "sdlgpu"
bool ParseInput(const std::string& s, Input* out);        // "auto" | "raw" | "mouse" | "touch"
bool ParseAudio(const std::string& s, Audio* out);        // "sdl" | "xaudio2"
const char* Name(Renderer r);

// Common tool options: --renderer R, --input I, --audio-backend A. Returns true (and advances i) if argv[i] was one.
struct CommonOptions {
    Renderer renderer = DefaultRenderer();
    Input input = Input::Auto;
    Audio audio = Audio::Sdl;
};
bool ParseCommonArg(int argc, char** argv, int& i, CommonOptions* o);
const char* CommonUsage();

enum class Key : uint8_t { Unknown, Escape, Enter, Space, Left, Right, Up, Down, PageUp, PageDown, A, P, Backspace, F8, G, H };

struct Event {
    // Focus: the application was activated / deactivated (active). Windows: WM_ACTIVATEAPP, the message Hod2.exe's
    // window callback 0x0049E780 reacts to; elsewhere SDL's window focus events. Only sent on a change.
    enum class Type : uint8_t { Quit, KeyDown, KeyUp, MouseDown, MouseUp, MouseMove, MouseWheel, Focus };
    Type type = Type::Quit;
    bool active = true;    // Focus
    Key key = Key::Unknown;
    char ch = 0;           // KeyDown / KeyUp: the key's printable ASCII (letters lower case, digits), 0 for others
    bool shift = false, repeat = false;
    int button = 0;        // 1 left, 2 middle, 3 right
    float x = 0, y = 0;    // pointer, window pixels
    float wheel = 0;       // + away from the user
};

struct Options {
    std::string title = "hotd2-re";
    int width = 960, height = 720;  // client size
    bool window = true;             // false: headless (no window, no SDL video)
    bool hidden = false;            // an actual hidden window for platform integration tests
    bool resizable = true;
    Input input = Input::Auto;      // gun source for the window (play.exe); Input::Auto picks per platform
    bool guns = false;              // create a gun source
};

class Platform {
public:
    Platform();
    ~Platform();
    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    bool Init(const Options& o, std::string* error);

    // Pumps the platform's events: feeds the gun source and returns the tool-level events one by one (false when empty).
    // A window close request returns Type::Quit.
    bool PollEvent(Event* e);

    double Seconds() const;  // monotonic
    void Delay(int ms);

    input::GunSource* Guns();       // null without a window / guns
    void ClientSize(int* w, int* h) const;
    Input CurrentInput() const;
    bool SetInput(Input mode, std::string* error);
    input::Lightguns& Lightguns();

    // Main thread: the system pointer is separate from the game's HUD sights. Headless/uninitialized instances
    // cannot change SDL's process-wide cursor; their visibility query returns the default visible state.
    bool SetCursorVisible(bool visible);
    bool CursorVisible() const;

    // Display settings of play.exe (notes/agents/display.md). Fullscreen = SDL's borderless desktop fullscreen;
    // SetWindowSize sets the windowed client size; UsableDisplaySize = the usable bounds of the window's display (the
    // "fit" window scale). SetGameImage: the rectangle of the client area (pixels) the frame is presented in (the GPU
    // device's PresentParams); Guns()' aim is normalized over it from then on (the bars are off screen), as the touch
    // source's bar taps are.
    bool SetFullscreen(bool on, bool exclusive = false);
    bool IsFullscreen() const;
    struct DisplayInfo { std::string name; int width = 0, height = 0; float refresh = 0; };
    std::vector<DisplayInfo> Displays() const;
    bool MoveToDisplay(int index);
    bool SetWindowSize(int w, int h);
    bool UsableDisplaySize(int* w, int* h) const;
    void SetGameImage(const input::ImageRect& image);

    // Main thread, at the title / main menu: offer the official release page or continue playing.
    void NotifyUpdate(const std::string& currentBuild, const std::string& availableTag);

    // GPU device for the window (fixed back buffer of width x height scaled to the window, or followWindowSize), or an
    // off-screen device when headless.
    std::unique_ptr<port::GpuDevice> CreateGpu(Renderer r, int width, int height, bool followWindowSize, int syncInterval,
                                               std::string* error);
    std::unique_ptr<port::AudioOutput> CreateAudio(Audio a, const std::string& gameDir, std::string* error);

    // RGBA8 rows, top row first.
    static bool SavePng(const std::string& path, const std::vector<uint8_t>& rgba, int width, int height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace platform
