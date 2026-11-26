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
bool VideoUp() { return sdl::StartVideo(); }

std::vector<Screen> Screens() {
    std::vector<Screen> out;
    if (!VideoUp()) return out;
    int n = 0;
    SDL_DisplayID* d = SDL_GetDisplays(&n);
    for (int i = 0; d && i < n; i++) out.push_back({d[i], "DISPLAY" + std::to_string(i + 1)});
    SDL_free(d);
    return out;
}

// the chosen screen and its desktop mode (asked each frame by Output). SDL's display calls belong on the thread that
// started its video (the window thread), so they are worked out there, again after ScreensChanged or a change of the
// screen setting; other threads read the last ones.
std::mutex g_mutex;
bool g_known = false;
std::string g_for;  // the screen setting they were worked out for
SDL_DisplayID g_display = 0;
Mode g_desktop{1280, 720};

void Refresh() {
    const std::string& want = Get().screen;
    if ((g_known && g_for == want) || !SDL_IsMainThread()) return;
    g_known = true;
    g_for = want;
    g_display = VideoUp() ? SDL_GetPrimaryDisplay() : 0;
    if (!want.empty() && want != "main") {
        bool found = false;
        // DISPLAY2, its number alone (2), or the screen's own name (LG ULTRAGEAR, as the log's display lines say)
        std::string as_name = want.find_first_not_of("0123456789") == std::string::npos ? "DISPLAY" + want : want;
        for (const Screen& s : Screens()) {
            const char* sdl_name = SDL_GetDisplayName(s.id);
            if (strcasecmp(s.name.c_str(), as_name.c_str()) == 0 || (sdl_name && strcasecmp(sdl_name, want.c_str()) == 0)) {
                g_display = s.id;
                found = true;
                break;  // two screens of one model: the first
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
        for (const Screen& s : Screens()) n.push_back(s.name);
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

void LogScreens() {
    SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    for (const Screen& s : Screens()) {
        const char* name = SDL_GetDisplayName(s.id);
        const SDL_DisplayMode* m = SDL_GetDesktopDisplayMode(s.id);
        LOGR("display: %s is %s, %dx%d at %.0f Hz, scale %.2f%s", s.name.c_str(), name ? name : "unnamed", m ? m->w : 0,
             m ? m->h : 0, m ? m->refresh_rate : 0.0f, SDL_GetDisplayContentScale(s.id), s.id == primary ? " (main)" : "");
    }
}

SDL_DisplayID Display() {
    std::lock_guard lock(g_mutex);
    Refresh();
    return g_display;
}

void ScreensChanged() {
    std::lock_guard lock(g_mutex);
    g_known = false;
    Refresh();
    LOGR("display: the screens changed, the desktop %ux%u", g_desktop.width, g_desktop.height);
}

}  // namespace sdl

}  // namespace app
#endif
