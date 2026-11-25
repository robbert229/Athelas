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
// Windows hands its events over as these (the harness has no window)
enum : uint32_t {
    WM_CREATE = 0x0001, WM_ACTIVATE = 0x0006, WM_CLOSE = 0x0010, WM_SETCURSOR = 0x0020, WM_INPUT = 0x00FF,
    WM_KEYDOWN = 0x0100, WM_SYSKEYDOWN = 0x0104, WM_SYSKEYUP = 0x0105, WM_SYSCOMMAND = 0x0112,
    WM_LBUTTONDOWN = 0x0201, WM_LBUTTONUP = 0x0202, WM_RBUTTONDOWN = 0x0204, WM_MBUTTONDOWN = 0x0207,
    WM_MOUSEWHEEL = 0x020A, WM_XBUTTONDOWN = 0x020B,
};
enum { WA_INACTIVE = 0, HTCLIENT = 1, XBUTTON1 = 1, SC_KEYMENU = 0xF100 };
enum {
    VK_LBUTTON = 0x01, VK_RBUTTON = 0x02, VK_MBUTTON = 0x04, VK_XBUTTON1 = 0x05, VK_XBUTTON2 = 0x06, VK_RETURN = 0x0D,
    VK_ESCAPE = 0x1B, VK_F3 = 0x72, VK_F10 = 0x79, VK_OEM_3 = 0xC0,
};
#define LOWORD(v) ((uint16_t)((uintptr_t)(v) & 0xFFFF))
#define HIWORD(v) ((uint16_t)(((uintptr_t)(v) >> 16) & 0xFFFF))
#define MAKEINTRESOURCEW(i) ((const wchar_t*)(uintptr_t)(uint16_t)(i))
inline HCURSOR LoadCursorW(void*, const wchar_t*) { return nullptr; }
inline HCURSOR SetCursor(HCURSOR) { return nullptr; }
inline short GetAsyncKeyState(int) { return 0; }

