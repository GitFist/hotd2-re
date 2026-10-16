// input_test.exe – shows a crosshair per player to verify two-gun / two-mouse input.
// Pull the trigger (left click) on each device to join as P1 / P2. R resets assignments.

#include <windows.h>

#include <cstdio>

#include "../input/raw_input.h"

namespace {
input::RawInput g_input;
int g_shots[input::kMaxPlayers] = {};

const COLORREF kColors[input::kMaxPlayers] = { RGB(255, 60, 60), RGB(60, 140, 255) };

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC screen = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetBkMode(dc, TRANSPARENT);

    for (int i = 0; i < input::kMaxPlayers; ++i) {
        const input::PlayerState& p = g_input.Player(i);
        char line[128];
        if (!p.connected) {
            snprintf(line, sizeof(line), "P%d: pull trigger to join", i + 1);
        } else {
            snprintf(line, sizeof(line), "P%d: x=%.3f y=%.3f %s shots=%d %s%s%s", i + 1, p.x, p.y,
                     p.onScreen ? "on-screen" : "OFF-SCREEN", g_shots[i], (p.held & input::kTrigger) ? "[TRIG]" : "",
                     (p.held & input::kReload) ? "[RELOAD]" : "", (p.held & input::kStart) ? "[START]" : "");
        }
        SetTextColor(dc, kColors[i]);
        TextOutA(dc, 10, 10 + i * 20, line, static_cast<int>(strlen(line)));

        if (p.connected) {
            HPEN pen = CreatePen(PS_SOLID, 2, kColors[i]);
            HGDIOBJ oldPen = SelectObject(dc, pen);
            const int x = static_cast<int>(p.x * rc.right), y = static_cast<int>(p.y * rc.bottom), r = 18;
            MoveToEx(dc, x - r, y, nullptr); LineTo(dc, x + r, y);
            MoveToEx(dc, x, y - r, nullptr); LineTo(dc, x, y + r);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Ellipse(dc, x - r / 2, y - r / 2, x + r / 2, y + r / 2);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
        }
    }
    const char* help = "Left = trigger   Right = reload   Middle = start   R = reset players";
    SetTextColor(dc, RGB(160, 160, 160));
    TextOutA(dc, 10, rc.bottom - 24, help, static_cast<int>(strlen(help)));

    BitBlt(screen, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_input.HandleMessage(msg, wParam, lParam)) {
        for (int i = 0; i < input::kMaxPlayers; ++i) {
            if (g_input.Player(i).pressed & input::kTrigger) {
                ++g_shots[i];
            }
        }
        g_input.BeginFrame();
        InvalidateRect(hwnd, nullptr, FALSE);
        return msg == WM_INPUT ? DefWindowProc(hwnd, msg, wParam, lParam) : 0;
    }
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == 'R') {
            g_input.ResetAssignments();
            g_shots[0] = g_shots[1] = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
}  // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wc.lpszClassName = "hotd2_input_test";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA(wc.lpszClassName, "hotd2-re input test", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, inst, nullptr);
    if (!g_input.Init(hwnd)) {
        MessageBoxA(hwnd, "RegisterRawInputDevices failed", "input_test", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, show);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
