#include "platform.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>

#include "audio/sdl_audio_backend.h"
#include "input/pointer_guns.h"
#include "input/touch_guns.h"

#ifdef _WIN32
#include <windows.h>

#include "audio/xaudio_backend.h"
#include "input/raw_input_guns.h"
#include "render/d3d11_device.h"
#endif
#ifdef HOTD2_HAVE_SDL_GPU
#include "render/sdl_gpu_device.h"
#endif

namespace platform {

Renderer DefaultRenderer() {
#ifdef _WIN32
    return Renderer::D3D11;
#else
    return Renderer::SdlGpu;
#endif
}

bool ParseRenderer(const std::string& s, Renderer* out) {
    if (s == "d3d11") *out = Renderer::D3D11;
    else if (s == "sdlgpu" || s == "sdl_gpu" || s == "gpu") *out = Renderer::SdlGpu;
    else return false;
    return true;
}

bool ParseInput(const std::string& s, Input* out) {
    if (s == "auto") *out = Input::Auto;
    else if (s == "raw" || s == "rawinput") *out = Input::RawInput;
    else if (s == "mouse") *out = Input::Mouse;
    else if (s == "touch") *out = Input::Touch;
    else return false;
    return true;
}

bool ParseAudio(const std::string& s, Audio* out) {
    if (s == "sdl") *out = Audio::Sdl;
    else if (s == "xaudio2" || s == "xaudio") *out = Audio::XAudio2;
    else return false;
    return true;
}

const char* Name(Renderer r) { return r == Renderer::D3D11 ? "d3d11" : "sdlgpu"; }

bool ParseCommonArg(int argc, char** argv, int& i, CommonOptions* o) {
    const std::string a = argv[i];
    if (i + 1 >= argc) return false;
    const std::string v = argv[i + 1];
    bool ok = false;
    if (a == "--renderer") ok = ParseRenderer(v, &o->renderer);
    else if (a == "--input") ok = ParseInput(v, &o->input);
    else if (a == "--audio-backend") ok = ParseAudio(v, &o->audio);
    else return false;
    if (!ok) fprintf(stderr, "bad value for %s: %s\n", a.c_str(), v.c_str());
    ++i;
    return true;
}

const char* CommonUsage() { return "[--renderer d3d11|sdlgpu] [--input auto|raw|mouse|touch] [--audio-backend sdl|xaudio2]"; }

namespace {

Key MapKey(SDL_Keycode k) {
    switch (k) {
    case SDLK_ESCAPE: return Key::Escape;
    case SDLK_RETURN: case SDLK_KP_ENTER: return Key::Enter;
    case SDLK_SPACE: return Key::Space;
    case SDLK_BACKSPACE: return Key::Backspace;
    case SDLK_LEFT: return Key::Left;
    case SDLK_RIGHT: return Key::Right;
    case SDLK_UP: return Key::Up;
    case SDLK_DOWN: return Key::Down;
    case SDLK_PAGEUP: return Key::PageUp;
    case SDLK_PAGEDOWN: return Key::PageDown;
    case SDLK_A: return Key::A;
    case SDLK_P: return Key::P;
    case SDLK_F8: return Key::F8;
    case SDLK_G: return Key::G;
    default: return Key::Unknown;
    }
}

}  // namespace

// A gun source whose aim is re-normalized from the window's client area to the game image rectangle inside it
// (Platform::SetGameImage): pillarbox / letterbox bars count as off screen. Wraps the raw-input and SDL mouse sources
// (the touch source maps its fingers itself, TouchConfig::aspect).
class MappedGuns final : public input::GunSource {
public:
    input::GunSource* inner = nullptr;
    SDL_Window* window = nullptr;
    input::ImageRect image;
    bool mapped = false, gameSpace = false;

    const char* Name() const override { return inner ? inner->Name() : "none"; }
    void BeginFrame() override {
        if (inner) inner->BeginFrame();
    }
    input::PlayerState Player(int index) const override {
        input::PlayerState ps = inner ? inner->Player(index) : input::PlayerState{};
        if (!ps.connected || !mapped || !window || gameSpace) return ps;
        int w = 1, h = 1;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        float gx, gy;
        const bool inside = input::WindowToImage(ps.x * static_cast<float>(w), ps.y * static_cast<float>(h), image, &gx, &gy);
        ps.x = gx;
        ps.y = gy;
        ps.onScreen = ps.onScreen && inside;
        return ps;
    }
};

struct Platform::Impl {
    Options opt;
    bool sdlVideo = false;
    SDL_Window* window = nullptr;
    SDL_Cursor* cursor = nullptr;
    Input input = Input::Auto;
    input::PointerGuns pointer;
    input::Lightguns lightguns;
    input::TouchGuns touch;
    MappedGuns mapped;
    // Application activation (Event::Type::Focus): the last state reported, and the state the platform saw last.
    bool activeReported = true, activeNow = true;
#ifdef _WIN32
    HWND hwnd = nullptr;
    input::RawInputGuns raw;
    // WM_ACTIVATEAPP is sent (not posted), so it never passes SDL's message hook: the window procedure is subclassed.
    WNDPROC sdlProc = nullptr;
    static Impl* activeImpl;
    static LRESULT CALLBACK ActivateProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        Impl* self = activeImpl;
        if (msg == WM_ACTIVATEAPP && self) self->activeNow = wp != 0;
        return CallWindowProcW(self ? self->sdlProc : DefWindowProcW, h, msg, wp, lp);
    }
    static bool SDLCALL MessageHook(void* user, MSG* msg) {
        Impl* self = static_cast<Impl*>(user);
        if (self->input == Input::RawInput && ((msg->message == WM_INPUT && self->activeNow) || msg->message == WM_INPUT_DEVICE_CHANGE))
            self->raw.HandleMessage(msg->message, msg->wParam, msg->lParam);
        return true;  // SDL still dispatches it (DefWindowProc cleans up WM_INPUT)
    }
#endif

    // The system pointer over the client area, normalized (outside the window: outside 0..1).
    bool PointerQuery(float* nx, float* ny) const {
#ifdef _WIN32
        POINT pt;
        RECT rc;
        if (!hwnd || !GetCursorPos(&pt)) return false;
        ScreenToClient(hwnd, &pt);
        GetClientRect(hwnd, &rc);
        *nx = rc.right > 0 ? static_cast<float>(pt.x) / rc.right : 0.5f;
        *ny = rc.bottom > 0 ? static_cast<float>(pt.y) / rc.bottom : 0.5f;
        return true;
#else
        if (!window) return false;
        float gx, gy;
        int wx, wy, ww, wh;
        SDL_GetGlobalMouseState(&gx, &gy);
        SDL_GetWindowPosition(window, &wx, &wy);
        SDL_GetWindowSize(window, &ww, &wh);
        *nx = ww > 0 ? (gx - wx) / ww : 0.5f;
        *ny = wh > 0 ? (gy - wy) / wh : 0.5f;
        return true;
#endif
    }

    void Normalize(float x, float y, float* nx, float* ny) const {
        int w = 1, h = 1;
        if (window) SDL_GetWindowSize(window, &w, &h);
        *nx = w > 0 ? x / w : 0.5f;
        *ny = h > 0 ? y / h : 0.5f;
    }
};

#ifdef _WIN32
Platform::Impl* Platform::Impl::activeImpl = nullptr;
#endif

Platform::Platform() : impl_(std::make_unique<Impl>()) {}

Platform::~Platform() {
#ifdef _WIN32
    if (impl_->sdlVideo) SDL_SetWindowsMessageHook(nullptr, nullptr);
    if (impl_->sdlProc && impl_->hwnd) SetWindowLongPtrW(impl_->hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(impl_->sdlProc));
    if (Impl::activeImpl == impl_.get()) Impl::activeImpl = nullptr;
#endif
    if (impl_->cursor) SDL_DestroyCursor(impl_->cursor);
    if (impl_->window) SDL_DestroyWindow(impl_->window);
    if (impl_->sdlVideo) SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
}

bool Platform::Init(const Options& o, std::string* error) {
    Impl& m = *impl_;
    m.opt = o;
    if (!o.window) return true;  // headless
    m.input = o.input;
    if (m.input == Input::Auto) {
#if defined(_WIN32)
        m.input = Input::RawInput;
#elif defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
        m.input = Input::Touch;
#else
        m.input = Input::Mouse;
#endif
    }
#ifndef _WIN32
    if (m.input == Input::RawInput) m.input = Input::Mouse;
#endif
    // Touch on a desktop: let the mouse produce finger events for testing (left button = one finger).
    if (o.guns && m.input == Input::Touch) SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        if (error) *error = std::string("SDL_Init: ") + SDL_GetError();
        return false;
    }
    m.sdlVideo = true;
    m.window = SDL_CreateWindow(o.title.c_str(), o.width, o.height, (o.resizable ? SDL_WINDOW_RESIZABLE : 0) | (o.hidden ? SDL_WINDOW_HIDDEN : 0));
    if (!m.window) {
        if (error) *error = std::string("SDL_CreateWindow: ") + SDL_GetError();
        return false;
    }
#ifdef _WIN32
    m.hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(m.window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (m.hwnd && !Impl::activeImpl) {
        Impl::activeImpl = &m;
        m.sdlProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(m.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Impl::ActivateProc)));
    }
#endif
    if (o.guns) {
        m.cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
        if (m.cursor) SDL_SetCursor(m.cursor);
#ifdef _WIN32
        m.raw.Raw().setup = &m.lightguns;
        SDL_SetWindowsMessageHook(&Impl::MessageHook, &m);
        if (m.input == Input::RawInput) {
            // Our raw-input registration on SDL's window. SDL only registers raw mouse input itself in relative mouse
            // mode (which would take the registration over), so the port never enables relative mode.
            if (!m.raw.Init(m.hwnd)) {
                if (error) *error = "RegisterRawInputDevices failed";
                return false;
            }
            SDL_SetWindowsMessageHook(&Impl::MessageHook, &m);
        }
#endif
        m.pointer.pointerQuery = [&m](float* nx, float* ny) { return m.PointerQuery(nx, ny); };
        int w, h;
        SDL_GetWindowSize(m.window, &w, &h);
        m.touch.config.displayW = static_cast<float>(w);
        m.touch.config.displayH = static_cast<float>(h);
    }
    return true;
}

bool Platform::PollEvent(Event* e) {
    Impl& m = *impl_;
    if (!m.sdlVideo) return false;
    const bool guns = m.opt.guns;
    SDL_Event ev;
    auto focusChange = [&] {
        if (m.activeNow == m.activeReported) return false;
        m.activeReported = m.activeNow;
        *e = Event{};
        e->type = Event::Type::Focus;
        e->active = m.activeNow;
        return true;
    };
    if (focusChange()) return true;
    while (SDL_PollEvent(&ev)) {
        *e = Event{};
        switch (ev.type) {
#ifndef _WIN32
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            m.activeNow = ev.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
            if (focusChange()) return true;
            continue;
#endif
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            e->type = Event::Type::Quit;
            return true;
        case SDL_EVENT_WINDOW_RESIZED:
            m.touch.config.displayW = static_cast<float>(ev.window.data1);
            m.touch.config.displayH = static_cast<float>(ev.window.data2);
            continue;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            const bool down = ev.type == SDL_EVENT_KEY_DOWN;
            e->type = down ? Event::Type::KeyDown : Event::Type::KeyUp;
            e->key = MapKey(ev.key.key);
            e->ch = ev.key.key >= 32 && ev.key.key < 127 ? static_cast<char>(ev.key.key) : 0;  // SDL keycodes are ASCII there
            e->shift = (ev.key.mod & SDL_KMOD_SHIFT) != 0;
            e->repeat = ev.key.repeat;
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const bool down = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            if (guns && m.input == Input::Mouse) {
                float nx, ny;
                m.Normalize(ev.button.x, ev.button.y, &nx, &ny);
                m.pointer.OnButton(ev.button.which, ev.button.button, down, nx, ny);
            }
            e->type = down ? Event::Type::MouseDown : Event::Type::MouseUp;
            e->button = ev.button.button;
            e->x = ev.button.x;
            e->y = ev.button.y;
            return true;
        }
        case SDL_EVENT_MOUSE_MOTION:
            if (guns && m.input == Input::Mouse) {
                float nx, ny;
                m.Normalize(ev.motion.x, ev.motion.y, &nx, &ny);
                m.pointer.OnMotion(ev.motion.which, nx, ny);
            }
            e->type = Event::Type::MouseMove;
            e->x = ev.motion.x;
            e->y = ev.motion.y;
            return true;
        case SDL_EVENT_MOUSE_WHEEL:
            e->type = Event::Type::MouseWheel;
            e->wheel = ev.wheel.y;
            return true;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            if (guns && m.input == Input::Touch) {
                const double t = static_cast<double>(ev.tfinger.timestamp) * 1e-9;
                if (ev.type == SDL_EVENT_FINGER_DOWN) m.touch.OnFingerDown(ev.tfinger.fingerID, ev.tfinger.x, ev.tfinger.y, t);
                else if (ev.type == SDL_EVENT_FINGER_MOTION) m.touch.OnFingerMove(ev.tfinger.fingerID, ev.tfinger.x, ev.tfinger.y, t);
                else m.touch.OnFingerUp(ev.tfinger.fingerID, t);
            }
            continue;
        default:
            continue;
        }
    }
    if (guns && m.input == Input::Touch) m.touch.Update(static_cast<double>(SDL_GetTicksNS()) * 1e-9);
    return false;
}

double Platform::Seconds() const {
    return static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(SDL_GetPerformanceFrequency());
}

void Platform::Delay(int ms) { SDL_Delay(static_cast<Uint32>(ms)); }

input::GunSource* Platform::Guns() {
    Impl& m = *impl_;
    if (!m.window || !m.opt.guns) return nullptr;
    m.mapped.window = m.window;
    m.mapped.gameSpace = m.input == Input::Touch || m.input == Input::RawInput;
    m.mapped.inner = m.input == Input::RawInput ? static_cast<input::GunSource*>(&m.lightguns) :
                     m.input == Input::Touch ? static_cast<input::GunSource*>(&m.touch) : &m.pointer;
    return &m.mapped; // stable address when the source changes with a Wii wrapper attached
}
input::Lightguns& Platform::Lightguns() { return impl_->lightguns; }
Input Platform::CurrentInput() const { return impl_->input; }
bool Platform::SetInput(Input mode, std::string* error) {
    auto& m=*impl_;
    if(mode!=Input::Mouse && mode!=Input::RawInput){if(error)*error="Choose mouse or light guns";return false;}
    if(!m.window || !m.opt.guns){if(error)*error="Input requires a game window";return false;}
    if(mode==m.input)return true;
#ifdef _WIN32
    if(mode==Input::RawInput && !m.raw.Init(m.hwnd)){if(error)*error="Could not read light guns";return false;}
#else
    if(mode==Input::RawInput){if(error)*error="Separate light guns require Windows";return false;}
#endif
    #ifdef _WIN32
    m.raw.Raw().ClearDevices();
    #endif
    m.lightguns.ClearLive();
    m.pointer = input::PointerGuns{};
    m.pointer.pointerQuery=[&m](float* x,float* y){return m.PointerQuery(x,y);};
    m.input=mode;
    Guns();
    return true;
}

void Platform::ClientSize(int* w, int* h) const {
    *w = impl_->opt.width;
    *h = impl_->opt.height;
    if (impl_->window) SDL_GetWindowSize(impl_->window, w, h);
}

bool Platform::SetFullscreen(bool on) {
    Impl& m = *impl_;
    if (!m.window) return false;
    if (!SDL_SetWindowFullscreen(m.window, on)) return false;  // no fullscreen mode set: borderless desktop fullscreen
    SDL_SyncWindow(m.window);
    return true;
}

bool Platform::IsFullscreen() const {
    return impl_->window && (SDL_GetWindowFlags(impl_->window) & SDL_WINDOW_FULLSCREEN) != 0;
}

bool Platform::SetWindowSize(int w, int h) {
    Impl& m = *impl_;
    if (!m.window || w <= 0 || h <= 0) return false;
    if (!SDL_SetWindowSize(m.window, w, h)) return false;
    const SDL_DisplayID d = SDL_GetDisplayForWindow(m.window);  // re-centred on its display (a bigger window may not fit where it was)
    SDL_SetWindowPosition(m.window, SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d));
    SDL_SyncWindow(m.window);
    return true;
}

bool Platform::UsableDisplaySize(int* w, int* h) const {
    const Impl& m = *impl_;
    if (!m.window) return false;
    SDL_Rect r;
    const SDL_DisplayID d = SDL_GetDisplayForWindow(m.window);
    if (!d || !SDL_GetDisplayUsableBounds(d, &r)) return false;
    int top = 0, left = 0, bottom = 0, right = 0;  // the window's decorations (title bar, frame)
    SDL_GetWindowBordersSize(m.window, &top, &left, &bottom, &right);
    *w = r.w - left - right;
    *h = r.h - top - bottom;
    if (*w <= 0) *w = r.w;
    if (*h <= 0) *h = r.h;
    return true;
}

void Platform::SetGameImage(const input::ImageRect& image) {
    Impl& m = *impl_;
    m.mapped.image = image;
    m.mapped.mapped = true;
    if (!m.window) return;
    int w = 1, h = 1;
    SDL_GetWindowSizeInPixels(m.window, &w, &h);
    // the touch source maps over the window's aspect itself: bars when the image does not fill the window
    const bool whole = image.x == 0 && image.y == 0 && image.w >= w && image.h >= h;
    m.touch.config.aspect = whole ? input::Aspect::Stretch : input::Aspect::Pillarbox43;
    m.touch.config.displayW = static_cast<float>(w);
    m.touch.config.displayH = static_cast<float>(h);
    m.lightguns.SetImage(float(image.x)/w,float(image.y)/h,float(image.w)/w,float(image.h)/h);
}

std::unique_ptr<port::GpuDevice> Platform::CreateGpu(Renderer r, int width, int height, bool followWindowSize, int syncInterval,
                                                     std::string* error) {
    Impl& m = *impl_;
    port::GpuDeviceDesc d;
    d.width = width;
    d.height = height;
    d.offscreen = m.window == nullptr;
    d.followWindowSize = followWindowSize;
    d.syncInterval = syncInterval;
    if (r == Renderer::D3D11) {
#ifdef _WIN32
        d.nativeWindow = m.hwnd;
        return port::CreateD3D11Device(d, error);
#else
        if (error) *error = "the D3D11 renderer is only available on Windows";
        return nullptr;
#endif
    }
#ifdef HOTD2_HAVE_SDL_GPU
    if (!m.sdlVideo) {  // SDL_CreateGPUDevice needs the video subsystem, also off-screen
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            if (error) *error = std::string("SDL_Init: ") + SDL_GetError();
            return nullptr;
        }
        m.sdlVideo = true;
    }
    d.nativeWindow = m.window;
    return port::CreateSdlGpuDevice(d, error);
#else
    if (error) *error = "built without the SDL_GPU renderer (HOTD2_SDL_GPU=OFF)";
    return nullptr;
#endif
}

std::unique_ptr<port::AudioOutput> Platform::CreateAudio(Audio a, const std::string& gameDir, std::string* error) {
    std::unique_ptr<port::AudioOutput> out;
    if (a == Audio::XAudio2) {
#ifdef _WIN32
        out = std::make_unique<port::XAudioBackend>();
#else
        if (error) *error = "XAudio2 is only available on Windows";
        return nullptr;
#endif
    } else {
        out = port::CreateSdlAudio();
    }
    if (!out->Init(gameDir)) {
        if (error) *error = std::string(out->Name()) + " audio init failed: " + SDL_GetError();
        return nullptr;
    }
    return out;
}

bool Platform::SavePng(const std::string& path, const std::vector<uint8_t>& rgba, int width, int height) {
    if (rgba.size() < static_cast<size_t>(width) * height * 4) return false;
    SDL_Surface* s = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, const_cast<uint8_t*>(rgba.data()), width * 4);
    if (!s) return false;
    const bool ok = SDL_SavePNG(s, path.c_str());
    SDL_DestroySurface(s);
    return ok;
}

}  // namespace platform
