// SDL3 builds: the keys and mouse buttons held, by Windows virtual-key code, as the window thread's events leave them,
// for keyboard.cpp (Held, Active, Over) and the frame cap's F9. Any thread reads them.
#ifndef _WIN32
#include <atomic>

#include "app/app.h"
#include "app/sdl_window.h"
#include "core/keys.h"

namespace app::sdl {

std::atomic<bool> g_active{false}, g_pointer_over{false};

namespace {
std::atomic<uint64_t> g_held[4];
}  // namespace

uint32_t VirtualKey(SDL_Scancode sc, SDL_Keycode key) {
    // letters by the layout's own (AZERTY's A is where a US Q is), as Windows' virtual keys
    if (key >= 'a' && key <= 'z') return uint32_t(key - 'a' + 'A');
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return uint32_t(sc - SDL_SCANCODE_A + 'A');
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return uint32_t(sc - SDL_SCANCODE_1 + '1');
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return uint32_t(sc - SDL_SCANCODE_F1 + 0x70);
    if (sc >= SDL_SCANCODE_F13 && sc <= SDL_SCANCODE_F24) return uint32_t(sc - SDL_SCANCODE_F13 + 0x7C);
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return uint32_t(sc - SDL_SCANCODE_KP_1 + 0x61);
    switch (sc) {
        case SDL_SCANCODE_0: return '0';
        case SDL_SCANCODE_RETURN: return 0x0D;
        case SDL_SCANCODE_ESCAPE: return 0x1B;
        case SDL_SCANCODE_BACKSPACE: return 0x08;
        case SDL_SCANCODE_TAB: return 0x09;
        case SDL_SCANCODE_SPACE: return 0x20;
        case SDL_SCANCODE_MINUS: return 0xBD;
        case SDL_SCANCODE_EQUALS: return 0xBB;
        case SDL_SCANCODE_LEFTBRACKET: return 0xDB;
        case SDL_SCANCODE_RIGHTBRACKET: return 0xDD;
        case SDL_SCANCODE_BACKSLASH: return 0xDC;
        case SDL_SCANCODE_NONUSHASH: return 0xDC;
        case SDL_SCANCODE_SEMICOLON: return 0xBA;
        case SDL_SCANCODE_APOSTROPHE: return 0xDE;
        case SDL_SCANCODE_GRAVE: return 0xC0;
        case SDL_SCANCODE_COMMA: return 0xBC;
        case SDL_SCANCODE_PERIOD: return 0xBE;
        case SDL_SCANCODE_SLASH: return 0xBF;
        case SDL_SCANCODE_CAPSLOCK: return 0x14;
        case SDL_SCANCODE_PRINTSCREEN: return 0x2C;
        case SDL_SCANCODE_SCROLLLOCK: return 0x91;
        case SDL_SCANCODE_PAUSE: return 0x13;
        case SDL_SCANCODE_INSERT: return 0x2D;
        case SDL_SCANCODE_HOME: return 0x24;
        case SDL_SCANCODE_PAGEUP: return 0x21;
        case SDL_SCANCODE_DELETE: return 0x2E;
        case SDL_SCANCODE_END: return 0x23;
        case SDL_SCANCODE_PAGEDOWN: return 0x22;
        case SDL_SCANCODE_RIGHT: return 0x27;
        case SDL_SCANCODE_LEFT: return 0x25;
        case SDL_SCANCODE_DOWN: return 0x28;
        case SDL_SCANCODE_UP: return 0x26;
        case SDL_SCANCODE_NUMLOCKCLEAR: return 0x90;
        case SDL_SCANCODE_KP_DIVIDE: return 0x6F;
        case SDL_SCANCODE_KP_MULTIPLY: return 0x6A;
        case SDL_SCANCODE_KP_MINUS: return 0x6D;
        case SDL_SCANCODE_KP_PLUS: return 0x6B;
        case SDL_SCANCODE_KP_ENTER: return 0x0D;
        case SDL_SCANCODE_KP_0: return 0x60;
        case SDL_SCANCODE_KP_PERIOD: return 0x6E;
        case SDL_SCANCODE_NONUSBACKSLASH: return 0xE2;
        case SDL_SCANCODE_APPLICATION: return 0x5D;
        case SDL_SCANCODE_LCTRL: return 0xA2;
        case SDL_SCANCODE_LSHIFT: return 0xA0;
        case SDL_SCANCODE_LALT: return 0xA4;
        case SDL_SCANCODE_LGUI: return 0x5B;
        case SDL_SCANCODE_RCTRL: return 0xA3;
        case SDL_SCANCODE_RSHIFT: return 0xA1;
        case SDL_SCANCODE_RALT: return 0xA5;
        case SDL_SCANCODE_RGUI: return 0x5C;
        default: return 0;
    }
}

uint32_t MouseButtonKey(uint8_t button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return 0x01;
        case SDL_BUTTON_RIGHT: return 0x02;
        case SDL_BUTTON_MIDDLE: return 0x04;
        case SDL_BUTTON_X1: return 0x05;
        case SDL_BUTTON_X2: return 0x06;
        default: return 0;
    }
}

void SetHeld(uint32_t vk, bool down) {
    if (!vk || vk > 255) return;
    uint64_t bit = 1ull << (vk & 63);
    if (down) g_held[vk >> 6] |= bit;
    else g_held[vk >> 6] &= ~bit;
}

void ReleaseAll() {
    for (auto& w : g_held) w = 0;
}

}  // namespace app::sdl

namespace keys {

bool Held(uint32_t vk) {
    // Shift, Ctrl and Alt without a side: either
    if (vk == 0x10) return Held(0xA0) || Held(0xA1);
    if (vk == 0x11) return Held(0xA2) || Held(0xA3);
    if (vk == 0x12) return Held(0xA4) || Held(0xA5);
    if (!vk || vk > 255) return false;
    return (app::sdl::g_held[vk >> 6].load() >> (vk & 63)) & 1;
}

bool Active() { return app::sdl::g_active.load(); }

bool Over() { return Active() && app::sdl::g_pointer_over.load(); }

}  // namespace keys
#endif
