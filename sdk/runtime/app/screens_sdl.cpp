// SDL3 builds: the screens, for [display] screen and the desktop mode (settings.cpp has Windows' monitors). They are
// named DISPLAY1, DISPLAY2... in SDL's order, as the setting names Windows' displays; "main" is the primary one.
#ifndef _WIN32
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <strings.h>
#include <vector>

#include "app/app.h"
#include "app/sdl_window.h"
#include "core/log.h"

namespace app {

namespace {

struct Screen {
    SDL_DisplayID id;
    std::string name;
};

// asked before the window is up too (the settings' list, the system's log): video started then, on the main thread
bool VideoUp() { return SDL_WasInit(SDL_INIT_VIDEO) || SDL_InitSubSystem(SDL_INIT_VIDEO); }

std::vector<Screen> Screens() {
    std::vector<Screen> out;
    if (!VideoUp()) return out;
    int n = 0;
    SDL_DisplayID* d = SDL_GetDisplays(&n);
    for (int i = 0; d && i < n; i++) out.push_back({d[i], "DISPLAY" + std::to_string(i + 1)});
    SDL_free(d);
    return out;
}

// the chosen screen and its desktop mode, worked out again after ScreensChanged (asked each frame by Output)
std::mutex g_mutex;
bool g_known = false;
std::string g_for;  // the screen setting they were worked out for
SDL_DisplayID g_display = 0;
Mode g_desktop{1280, 720};

void Refresh() {
    const std::string& want = Get().screen;
    if (g_known && g_for == want) return;
    g_known = true;
    g_for = want;
    g_display = VideoUp() ? SDL_GetPrimaryDisplay() : 0;
    if (!want.empty() && want != "main") {
        bool found = false;
        for (const Screen& s : Screens()) {
            const char* sdl_name = SDL_GetDisplayName(s.id);
            if (strcasecmp(s.name.c_str(), want.c_str()) == 0 || (sdl_name && strcasecmp(sdl_name, want.c_str()) == 0)) {
                g_display = s.id;
                found = true;
            }
        }
        static bool logged = false;
        if (!found && !logged) {
            logged = true;
            LOGR("display: screen %s is not connected, using the main one", want.c_str());
        }
    }
    // the desktop mode in pixels (a mode's size is in screen coordinates, times its pixel density)
    if (const SDL_DisplayMode* m = g_display ? SDL_GetDesktopDisplayMode(g_display) : nullptr) {
        float density = m->pixel_density > 0 ? m->pixel_density : 1.0f;
        g_desktop = {uint32_t(m->w * density + 0.5f), uint32_t(m->h * density + 0.5f)};
    }
}

}  // namespace

const std::vector<std::string>& ScreenNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n{"main"};
        for (const Screen& s : Screens()) {
            n.push_back(s.name);
            const char* sdl_name = SDL_GetDisplayName(s.id);
            LOGI("display: %s is %s", s.name.c_str(), sdl_name ? sdl_name : "unnamed");
        }
        return n;
    }();
    return names;
}

// an SDL_DisplayID in the pointer
void* ScreenMonitor() { return reinterpret_cast<void*>(uintptr_t(sdl::Display())); }

Mode Desktop() {
    std::lock_guard lock(g_mutex);
    Refresh();
    return g_desktop;
}

namespace sdl {

SDL_DisplayID Display() {
    std::lock_guard lock(g_mutex);
    Refresh();
    return g_display;
}

void ScreensChanged() {
    std::lock_guard lock(g_mutex);
    g_known = false;
}

}  // namespace sdl

}  // namespace app
#endif
