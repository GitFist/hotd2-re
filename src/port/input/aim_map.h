#pragma once

// Pure coordinate conversions for pointer aiming (no Win32 calls; unit-tested in tests/input_test_unit.cpp).

namespace input {

struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;
    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
};

// Absolute device report (0..65535 over the primary monitor or virtual desktop) -> screen pixels.
void AbsoluteToScreen(long rawX, long rawY, const Rect& desktop, float* sx, float* sy);

// Screen pixels -> normalized 0..1 over a client rect given in screen coordinates.
// Returns false (and clamps) when the point is outside.
bool ScreenToNormalized(float sx, float sy, const Rect& clientOnScreen, float* nx, float* ny);

enum class Aspect {
    Stretch,     // game image fills the whole area
    Pillarbox43  // game image is a centered 4:3 region (black bars), e.g. GPU "preserve aspect ratio"
};

// Normalized position over the displayed area -> normalized position over the game image.
// With Pillarbox43, points on the bars are reported off-screen. displayW/H: size of the area the
// normalized position refers to (used for its aspect ratio).
bool ApplyAspect(float nx, float ny, float displayW, float displayH, Aspect aspect, float* gx, float* gy);

// Normalized game position -> integer game pixels, clamped to [0, size-1].
// HOTD2 aims in 640x480 pixels, origin top-left (crosshair = (cx + 320, 240 - cy) at 0x004169C0).
void NormalizedToGame(float gx, float gy, int gameW, int gameH, int* x, int* y);

// The game image inside a window (play.exe's display settings): Stretch fills the client area; Pillarbox43 is the
// largest centred 4:3 rectangle (bars left / right on a wide window, top / bottom on a tall one). Pixels; the
// presentation pass draws into this rectangle and the aim is mapped over it.
struct ImageRect {
    int x = 0, y = 0, w = 0, h = 0;
};
ImageRect FitImage(int windowW, int windowH, Aspect aspect);

// Window pixel position -> normalized position over the image rectangle (false = outside: off screen; the result is
// then clamped). The inverse of the present pass's mapping, so the aim hits what is under the pointer.
bool WindowToImage(float px, float py, const ImageRect& image, float* gx, float* gy);

}  // namespace input
