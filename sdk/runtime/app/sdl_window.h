#pragma once
// SDL3 builds (not Windows): what main_sdl.cpp, keyboard_sdl.cpp, screens_sdl.cpp and crash_posix.cpp share
#include <atomic>
#include <cstdint>

#include <SDL3/SDL.h>

namespace app::sdl {
// window thread: the game window has the keyboard focus; the pointer is over it (or held in relative mode)
extern std::atomic<bool> g_active, g_pointer_over;
// the virtual-key code (VK_*) of a key, as Windows would give it on a US layout (the letters follow the layout, as
// Windows' do); 0 for a key it has none for
uint32_t VirtualKey(SDL_Scancode sc, SDL_Keycode key);
uint32_t MouseButtonKey(uint8_t button);  // SDL_BUTTON_LEFT ... X2: VK_LBUTTON ... VK_XBUTTON2, 0 for others
void SetHeld(uint32_t vk, bool down);     // window thread, as keys and buttons go down and up
void ReleaseAll();                        // window thread: the focus went elsewhere
// the screen setting's display (ScreenMonitor), the primary one without it; off the window thread, as it last saw it
SDL_DisplayID Display();
void ScreensChanged();                    // window thread: a display was added, removed or changed mode
void LogScreens();                        // each screen's setting name, SDL name and desktop mode
bool StartVideo();                        // main thread: SDL's video, once (the app's name and hints first), then its audio
bool AudioUp();                           // any thread: SDL's audio started (with video, on the main thread)
// window thread: SDL_ShowWindow, on X11 going on after 3 s when the window manager doesn't map it (screens off)
void Show(SDL_Window* window);
bool Relaunched();                        // --relaunched: started again by kernel::Relaunch (no splash)
// any thread: the game window shows HDR, with its SDR white and headroom in scRGB units, as the window thread last read
bool WindowHdr(float& white, float& headroom);
void InstallCrashHandler();               // crash_posix.cpp: at start, on the main thread
}  // namespace app::sdl
