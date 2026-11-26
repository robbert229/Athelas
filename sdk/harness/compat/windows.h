#pragma once
// off Windows (the headless harness, native Linux and Mac builds): the Win32 names portable runtime and game files
// touch
#include <cstdint>

typedef long HRESULT;
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* HWND;
typedef void* HCURSOR;
typedef const wchar_t* PCWSTR;
typedef unsigned int UINT;
typedef int BOOL;
#define WINAPI
#ifndef TRUE
#define TRUE 1
#endif

inline HMODULE GetModuleHandleW(const wchar_t*) { return nullptr; }
inline void* GetProcAddress(HMODULE, const char*) { return nullptr; }
inline HANDLE GetCurrentThread() { return nullptr; }
inline HANDLE GetCurrentProcess() { return nullptr; }
#include <cstdlib>
inline BOOL TerminateProcess(HANDLE, UINT code) { std::_Exit(int(code)); }

// window messages and keys GameInfo::message and the in-game menu (app/overlay.cpp) name: the window layer off
// Windows (app/main_sdl.cpp) hands its events over as these (the harness has no window)
enum : uint32_t {
    WM_CREATE = 0x0001, WM_ACTIVATE = 0x0006, WM_CLOSE = 0x0010, WM_SETCURSOR = 0x0020, WM_INPUT = 0x00FF,
    WM_KEYDOWN = 0x0100, WM_KEYUP = 0x0101, WM_SYSKEYDOWN = 0x0104, WM_SYSKEYUP = 0x0105, WM_SYSCOMMAND = 0x0112,
    WM_MOUSEMOVE = 0x0200, WM_LBUTTONDOWN = 0x0201, WM_LBUTTONUP = 0x0202, WM_RBUTTONDOWN = 0x0204,
    WM_RBUTTONUP = 0x0205, WM_MBUTTONDOWN = 0x0207, WM_MBUTTONUP = 0x0208, WM_MOUSEWHEEL = 0x020A,
    WM_XBUTTONDOWN = 0x020B, WM_XBUTTONUP = 0x020C,
};
enum { WA_INACTIVE = 0, WA_ACTIVE = 1, HTCLIENT = 1, XBUTTON1 = 1, XBUTTON2 = 2, SC_KEYMENU = 0xF100, WHEEL_DELTA = 120 };
enum {
    VK_LBUTTON = 0x01, VK_RBUTTON = 0x02, VK_MBUTTON = 0x04, VK_XBUTTON1 = 0x05, VK_XBUTTON2 = 0x06, VK_BACK = 0x08,
    VK_TAB = 0x09, VK_RETURN = 0x0D, VK_SHIFT = 0x10, VK_CONTROL = 0x11, VK_MENU = 0x12, VK_ESCAPE = 0x1B,
    VK_SPACE = 0x20, VK_LEFT = 0x25, VK_UP = 0x26, VK_RIGHT = 0x27, VK_DOWN = 0x28, VK_F1 = 0x70, VK_F2 = 0x71,
    VK_F3 = 0x72, VK_F4 = 0x73, VK_F5 = 0x74, VK_F6 = 0x75, VK_F7 = 0x76, VK_F8 = 0x77, VK_F9 = 0x78, VK_F10 = 0x79,
    VK_F11 = 0x7A, VK_F12 = 0x7B, VK_LSHIFT = 0xA0, VK_RSHIFT = 0xA1, VK_LCONTROL = 0xA2, VK_RCONTROL = 0xA3,
    VK_LMENU = 0xA4, VK_RMENU = 0xA5, VK_OEM_3 = 0xC0, VK_OEM_4 = 0xDB, VK_OEM_5 = 0xDC, VK_OEM_6 = 0xDD,
};
#define LOWORD(v) ((uint16_t)((uintptr_t)(v) & 0xFFFF))
#define HIWORD(v) ((uint16_t)(((uintptr_t)(v) >> 16) & 0xFFFF))
#define GET_WHEEL_DELTA_WPARAM(wp) ((short)HIWORD(wp))
#define MAKEINTRESOURCEW(i) ((const wchar_t*)(uintptr_t)(uint16_t)(i))
inline HCURSOR LoadCursorW(void*, const wchar_t*) { return nullptr; }
inline HCURSOR SetCursor(HCURSOR) { return nullptr; }
inline short GetAsyncKeyState(int) { return 0; }

