#include "raw_input.h"

#include <algorithm>
#include <vector>
#include <iomanip>
#include <cwctype>
#include <sstream>

namespace input {

namespace {
constexpr USHORT kUsagePageGeneric = 0x01;
constexpr USHORT kUsageMouse = 0x02;

Rect FromWin(const RECT& r) { return { r.left, r.top, r.right, r.bottom }; }

std::string DeviceKey(HANDLE handle) {
    UINT count=0;
    if(GetRawInputDeviceInfoW(handle,RIDI_DEVICENAME,nullptr,&count)==UINT(-1)||!count)return {};
    std::vector<wchar_t> name(count+1);
    if(GetRawInputDeviceInfoW(handle,RIDI_DEVICENAME,name.data(),&count)==UINT(-1))return {};
    // Store an opaque identifier locally, never the Windows device path.
    uint64_t hash=14695981039346656037ULL;
    for(wchar_t ch:name){if(!ch)break;hash^=uint16_t(towlower(ch));hash*=1099511628211ULL;}
    std::ostringstream text;text<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;return text.str();
}
}  // namespace

bool RawInput::Init(HWND window, HWND coordWindow) {
    window_ = window;
    coordWindow_ = coordWindow ? coordWindow : window;
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = kUsagePageGeneric;
    rid.usUsage = kUsageMouse;
    rid.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    rid.hwndTarget = window;
    return RegisterRawInputDevices(&rid, 1, sizeof(rid)) == TRUE;
}

Geometry RawInput::QueryGeometry() const {
    if (geometryProvider) {
        return geometryProvider();
    }
    Geometry g;
    RECT client = {};
    GetClientRect(coordWindow_, &client);
    POINT origin = { 0, 0 };
    ClientToScreen(coordWindow_, &origin);
    g.client = { origin.x, origin.y, origin.x + client.right, origin.y + client.bottom };
    g.primary = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g.virtualDesk = { vx, vy, vx + GetSystemMetrics(SM_CXVIRTUALSCREEN), vy + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
    return g;
}

bool RawInput::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_INPUT_DEVICE_CHANGE) {
        if (wParam == GIDC_REMOVAL) {
            RemoveDevice(reinterpret_cast<HANDLE>(lParam));
        }
        return true;
    }
    if (msg != WM_INPUT) {
        return false;
    }

    UINT size = 0;
    GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    if (size == 0) {
        return true;
    }
    std::vector<BYTE> buf(size);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buf.data(), &size,
                        sizeof(RAWINPUTHEADER)) != size) {
        return true;
    }
    const RAWINPUT* ri = reinterpret_cast<const RAWINPUT*>(buf.data());
    if (ri->header.dwType == RIM_TYPEMOUSE) {
        ProcessMouse(ri->header.hDevice, ri->data.mouse);
    }
    return true;
}

void RawInput::RemoveDevice(HANDLE handle) {
    auto it = devices_.find(handle);
    if (it == devices_.end()) {
        return;
    }
    if(setup)setup->RemoveDevice(it->second.key);
    if (it->second.player >= 0) {
        players_[it->second.player] = PlayerState{};
    }
    devices_.erase(it);
}

bool RawInput::ProcessMouse(HANDLE handle, const RAWMOUSE& m) {
    // No device handle = synthetic input (SendInput/automation). Never let it join or fire.
    if (!handle) {
        return false;
    }
    Device& dev = devices_[handle];

    const USHORT flags = m.usButtonFlags;
    uint8_t down = 0, up = 0;
    if (flags & RI_MOUSE_LEFT_BUTTON_DOWN)   down |= kTrigger;
    if (flags & RI_MOUSE_LEFT_BUTTON_UP)     up   |= kTrigger;
    if (flags & RI_MOUSE_RIGHT_BUTTON_DOWN)  down |= kReload;
    if (flags & RI_MOUSE_RIGHT_BUTTON_UP)    up   |= kReload;
    if (flags & RI_MOUSE_MIDDLE_BUTTON_DOWN) down |= kStart;
    if (flags & RI_MOUSE_MIDDLE_BUTTON_UP)   up   |= kStart;

    if(setup){
        if(dev.key.empty())dev.key=deviceKeyProvider?deviceKeyProvider(handle):DeviceKey(handle);
        if(dev.key.empty())return false;
        if(flags&RI_MOUSE_BUTTON_4_DOWN)down|=8;
        if(flags&RI_MOUSE_BUTTON_4_UP)up|=8;
        if(flags&RI_MOUSE_BUTTON_5_DOWN)down|=16;
        if(flags&RI_MOUSE_BUTTON_5_UP)up|=16;
        const auto g=QueryGeometry();bool inside=false;
        float x=dev.relX,y=dev.relY;
        if(m.usFlags&MOUSE_MOVE_ABSOLUTE){
            float sx,sy;AbsoluteToScreen(m.lLastX,m.lLastY,(m.usFlags&MOUSE_VIRTUAL_DESKTOP)?g.virtualDesk:g.primary,&sx,&sy);
            inside=ScreenToNormalized(sx,sy,g.client,&x,&y);dev.absolute=true;dev.hasPosition=true;
        }else if(!dev.absolute){
            POINT pt{};
            if(GetCursorPos(&pt))inside=ScreenToNormalized(float(pt.x),float(pt.y),g.client,&x,&y);
            dev.hasPosition=true;
        }else inside=x>=0&&x<1&&y>=0&&y<1;
        dev.relX=x;dev.relY=y;
        setup->Feed(dev.key,x,y,inside&&dev.hasPosition,!dev.absolute,down,up);
        return true;
    }

    // Unassigned devices join on their first trigger pull.
    if (dev.player < 0) {
        if (!(down & kTrigger) || AssignPlayer(dev) < 0) {
            return true;
        }
    }
    PlayerState& p = players_[dev.player];
    const Geometry g = QueryGeometry();

    if (m.usFlags & MOUSE_MOVE_ABSOLUTE) {
        const Rect& desk = (m.usFlags & MOUSE_VIRTUAL_DESKTOP) ? g.virtualDesk : g.primary;
        float sx, sy;
        AbsoluteToScreen(m.lLastX, m.lLastY, desk, &sx, &sy);
        p.onScreen = ScreenToNormalized(sx, sy, g.client, &p.x, &p.y);
        p.relative = false;
    } else if (m.lLastX || m.lLastY) {
        p.relative = true;
        const float w = static_cast<float>(std::max(g.client.Width(), 1));
        const float h = static_cast<float>(std::max(g.client.Height(), 1));
        const float scale = relativeSensitivity / 1000.0f;
        dev.relX = std::clamp(dev.relX + m.lLastX * scale, 0.0f, 1.0f);
        dev.relY = std::clamp(dev.relY + m.lLastY * scale * (w / h), 0.0f, 1.0f);
        p.x = dev.relX;
        p.y = dev.relY;
        p.onScreen = true;
    }

    p.pressed |= down & ~p.held;
    p.released |= up & p.held;
    p.held = (p.held | down) & ~up;
    return true;
}

int RawInput::AssignPlayer(Device& dev) {
    for (int i = 0; i < kMaxPlayers; ++i) {
        if (!players_[i].connected) {
            players_[i] = PlayerState{};
            players_[i].connected = true;
            dev.player = i;
            return i;
        }
    }
    return -1;
}

void RawInput::BeginFrame() {
    for (PlayerState& p : players_) {
        p.pressed = 0;
        p.released = 0;
    }
}

void RawInput::ResetAssignments() {
    for (auto& [handle, dev] : devices_) {
        dev.player = -1;
    }
    players_ = {};
}

}  // namespace input
