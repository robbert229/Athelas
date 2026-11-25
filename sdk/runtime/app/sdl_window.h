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
SDL_DisplayID Display();                  // the screen setting's display (ScreenMonitor), the primary one without it
void ScreensChanged();                    // window thread: a display was added, removed or changed mode
void LogScreens();                        // each screen's setting name, SDL name and desktop mode
void InstallCrashHandler();               // crash_posix.cpp: at start, on the main thread
}  // namespace app::sdl
