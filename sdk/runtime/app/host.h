#pragma once
// the window layer's shared parts (app/host.cpp), for the Win32 window (main.cpp) and the SDL3 one (main_sdl.cpp)
#include <cstdint>

namespace app {
// Pace's frame cap this frame, after fast forward (f9: the key held in the game window) and the game's own speed-up
// have set the guest's speed; 0 no limit
uint32_t PaceCap(bool f9);
// the window's own keys (screenshot, texture packs, colour looks, sharpening, M's capture): a WM_KEYDOWN no one took
void HostKey(uintptr_t vk, intptr_t lp);
}  // namespace app
