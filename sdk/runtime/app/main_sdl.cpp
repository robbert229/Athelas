// the host off Windows: the game window, its events, the frame cap and the message boxes through SDL3, the twin of
// main.cpp. The window's events reach ImGui, the in-game menu (OverlayMessage) and the game (GameInfo::message) as
// the Win32 messages main.cpp's window gets (WM_KEYDOWN with VK_ codes, WM_ACTIVATE, WM_LBUTTONDOWN...), so the
// games' hooks are the same on every system. The main thread is the window thread: SDL wants its windows there.
#ifndef _WIN32
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <windows.h>  // off Windows the harness's compat header: the WM_ and VK_ names the hooks take

#include "app/app.h"
#include "app/host.h"
#include "app/sdl_window.h"
#include "core/input.h"
#include "core/keys.h"
#include "core/log.h"
#include "image_symbols.h"
#include "kernel/kernel.h"
#include "xdk/netplay.h"

namespace app {

namespace {

std::vector<std::string> g_argv;
std::thread::id g_main_thread;
SDL_Window* g_window = nullptr;
std::atomic<bool> g_alive{false};
std::atomic<uint32_t> g_pixel_w{0}, g_pixel_h{0};
std::atomic<bool> g_looping{false}, g_loop_done{false};
std::atomic<int> g_exiting_with{-1};
uint32_t g_call_event = 0;   // a function to run on the window thread (RunOnWindowThread)
bool g_quit = false;         // window thread: the loop ends
bool g_exclusive = false;    // window thread: fullscreen in a mode other than the desktop's
float g_motion_x = 0, g_motion_y = 0;  // window thread: relative movement's fractions, kept for the next event

// UTF-32 (Linux's wchar_t) to UTF-8
std::string Utf8(const std::wstring& w) {
    std::string s;
    for (wchar_t wc : w) {
        uint32_t c = uint32_t(wc);
        if (c < 0x80) {
            s += char(c);
        } else if (c < 0x800) {
            s += char(0xC0 | (c >> 6));
            s += char(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            s += char(0xE0 | (c >> 12));
            s += char(0x80 | ((c >> 6) & 0x3F));
            s += char(0x80 | (c & 0x3F));
        } else {
            s += char(0xF0 | ((c >> 18) & 0x07));
            s += char(0x80 | ((c >> 12) & 0x3F));
            s += char(0x80 | ((c >> 6) & 0x3F));
            s += char(0x80 | (c & 0x3F));
        }
    }
    return s;
}

// the command line: main's, or /proc/self/cmdline for a program with its own main (the smoke test)
const std::vector<std::string>& Argv() {
    if (g_argv.empty()) {
        std::ifstream f("/proc/self/cmdline", std::ios::binary);
        for (std::string a; std::getline(f, a, '\0');) g_argv.push_back(a);
    }
    return g_argv;
}

bool OnWindowThread() { return std::this_thread::get_id() == g_main_thread; }

// runs fn on the window thread and waits for it (Win32's SendMessage). Before the loop runs the call waits in SDL's
// queue; after it has ended (or when it doesn't answer in 10 s) fn runs here instead.
struct Call {
    std::function<void()> fn;
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
};
void RunOnWindowThread(const std::function<void()>& fn) {
    if (OnWindowThread() || g_loop_done || !g_call_event) {
        fn();
        return;
    }
    auto* c = new Call{fn};
    SDL_Event e{};
    e.type = g_call_event;
    e.user.data1 = c;
    if (!SDL_PushEvent(&e)) {
        delete c;
        fn();
        return;
    }
    std::unique_lock lock(c->m);
    if (!c->cv.wait_for(lock, std::chrono::seconds(10), [&] { return c->done; })) {
        LOGW("window: the window thread didn't answer, going on without it");
        return;  // c is the window thread's to free if it ever gets to it
    }
    lock.unlock();
    delete c;
}

void UpdatePixelSize() {
    int w = 0, h = 0;
    if (g_window && SDL_GetWindowSizeInPixels(g_window, &w, &h)) {
        g_pixel_w = uint32_t(std::max(w, 0));
        g_pixel_h = uint32_t(std::max(h, 0));
    }
}

bool Gamescope() {
    // as main.cpp: gamescope (Steam Deck Game Mode) scales the game itself and owns the screen
    static const bool gamescope = [] {
        const char* deck = std::getenv("SteamDeck");
        return std::getenv("GAMESCOPE_WAYLAND_DISPLAY") || (deck && std::strcmp(deck, "1") == 0);
    }();
    return gamescope;
}

// window thread: where the window is now, for the log
void LogPlace() {
    static const char* const kModes[] = {"fullscreen", "borderless", "windowed"};
    const char* on = SDL_GetDisplayName(SDL_GetDisplayForWindow(g_window));
    LOGR("display: %s on %s, %ux%u pixels", kModes[uint32_t(Get().display_mode) % 3], on ? on : "a screen",
         g_pixel_w.load(), g_pixel_h.load());
}

// window thread: the display settings onto the window, as main.cpp's ApplyNow: windowed (centred, the picture at the
// size asked for in pixels), borderless over the screen, or fullscreen in another mode when one is asked for
void ApplyNow() {
    if (!g_window) return;
    const Settings& s = Get();
    SDL_DisplayID id = sdl::Display();
    const SDL_DisplayMode* desk = id ? SDL_GetDesktopDisplayMode(id) : nullptr;
    Mode desktop = Desktop(), out = Output();
    uint32_t fw = s.width && s.height ? s.width : desktop.width, fh = s.width && s.height ? s.height : desktop.height;
    bool other_size = fw != desktop.width || fh != desktop.height;
    float desk_hz = desk ? desk->refresh_rate : 0.0f;
    bool other_rate = s.refresh && std::fabs(float(s.refresh) - desk_hz) > 0.5f;
    // a screen moved to: out of fullscreen first, so the window goes over the other screen
    bool fullscreen = SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN;
    if (fullscreen && SDL_GetDisplayForWindow(g_window) != id) SDL_SetWindowFullscreen(g_window, false);
    if (Gamescope() && s.display_mode == DisplayMode::Fullscreen && (other_size || other_rate))
        LOGR("display: under gamescope (Steam Deck / Steam's Game Mode): the screen's own mode is kept, gamescope scales");
    g_exclusive = false;
    if (s.display_mode == DisplayMode::Windowed) {
        SDL_SetWindowFullscreen(g_window, false);
        SDL_SetWindowBordered(g_window, true);
        SDL_SetWindowResizable(g_window, true);
        // the size asked for in pixels: in screen coordinates that is less on a scaled (HiDPI) screen
        float density = SDL_GetWindowPixelDensity(g_window);
        if (density <= 0) density = 1;
        SDL_SetWindowSize(g_window, std::max(1, int(out.width / density + 0.5f)), std::max(1, int(out.height / density + 0.5f)));
        SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(id), SDL_WINDOWPOS_CENTERED_DISPLAY(id));
    } else {
        SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(id), SDL_WINDOWPOS_CENTERED_DISPLAY(id));
        SDL_DisplayMode mode{};
        bool change = !Gamescope() && s.display_mode == DisplayMode::Fullscreen && (other_size || other_rate);
        if (change && !SDL_GetClosestFullscreenDisplayMode(id, int(fw), int(fh), float(s.refresh), false, &mode) &&
            !SDL_GetClosestFullscreenDisplayMode(id, int(fw), int(fh), 0.0f, false, &mode)) {
            LOGR("display: no %ux%u mode on the screen, the desktop mode kept", fw, fh);
            change = false;
        }
        // borderless (and fullscreen at the desktop's own mode): a window over the screen, no mode change. Wayland
        // places no windows, so there the screen is named by its own desktop mode (which changes nothing on it)
        const SDL_DisplayMode* use = change ? &mode : nullptr;
        SDL_DisplayMode own{};
        if (!change && desk && std::strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0 &&
            SDL_GetClosestFullscreenDisplayMode(id, desk->w, desk->h, desk->refresh_rate, true, &own))
            use = &own;
        bool ok = SDL_SetWindowFullscreenMode(g_window, use) && SDL_SetWindowFullscreen(g_window, true);
        g_exclusive = change && ok;
        if (change)
            LOGR("display: %ux%u at %.0f Hz%s", uint32_t(mode.w), uint32_t(mode.h), mode.refresh_rate,
                 g_exclusive ? "" : " failed, the desktop mode kept");
        if (!ok) LOGW("display: fullscreen failed: %s", SDL_GetError());
    }
    if (SDL_GetWindowFlags(g_window) & SDL_WINDOW_HIDDEN) {
        UpdatePixelSize();
        return;  // the rest when it is shown (ShowWindow)
    }
    SDL_SyncWindow(g_window);
    UpdatePixelSize();
    LogPlace();
}

void ShowWindow() {
    SDL_ShowWindow(g_window);
    SDL_RaiseWindow(g_window);
    SDL_SyncWindow(g_window);
    UpdatePixelSize();
    LogPlace();
}

// a message to the hooks in main.cpp's order: the menu, the game, then the window's own handling
void Dispatch(uint32_t msg, uintptr_t wp, intptr_t lp) {
    if (intptr_t result = 0; OverlayMessage(g_window, msg, wp, lp, &result)) return;
    if (intptr_t result = 0; kGame.message && kGame.message(g_window, msg, wp, lp, &result)) return;
    switch (msg) {
        case WM_KEYDOWN: HostKey(wp, lp); break;
        case WM_ACTIVATE: SetFocus(LOWORD(wp) != WA_INACTIVE); break;
        case WM_CLOSE: g_quit = true; break;
    }
}

intptr_t PointLp(float x, float y) {
    float density = SDL_GetWindowPixelDensity(g_window);
    if (density <= 0) density = 1;
    auto px = uint16_t(int16_t(std::lround(x * density))), py = uint16_t(int16_t(std::lround(y * density)));
    return intptr_t(uint32_t(px) | uint32_t(py) << 16);
}

void Key(const SDL_KeyboardEvent& k) {
    uint32_t vk = sdl::VirtualKey(k.scancode, k.key);
    if (!vk) return;
    sdl::SetHeld(vk, k.down);
    // the messages name Shift, Ctrl and Alt without a side, as Windows'
    uint32_t wp = vk == 0xA0 || vk == 0xA1 ? 0x10 : vk == 0xA2 || vk == 0xA3 ? 0x11 : vk == 0xA4 || vk == 0xA5 ? 0x12 : vk;
    // F10 and Alt with a key are system keys there (WM_SYSKEYDOWN); AltGr (Ctrl with it) is not
    bool alt = (k.mod & SDL_KMOD_ALT) || wp == 0x12;
    bool sys = (alt && !(k.mod & SDL_KMOD_CTRL)) || vk == 0x79;
    intptr_t lp = 1 | intptr_t(k.scancode & 0xFF) << 16;
    if (k.repeat) lp |= intptr_t(1) << 30;
    if (!k.down) lp |= intptr_t(3) << 30;
    Dispatch(k.down ? (sys ? WM_SYSKEYDOWN : WM_KEYDOWN) : (sys ? WM_SYSKEYUP : WM_KEYUP), wp, lp);
}

void Button(const SDL_MouseButtonEvent& b) {
    uint32_t vk = sdl::MouseButtonKey(b.button);
    if (!vk) return;
    sdl::SetHeld(vk, b.down);
    intptr_t lp = PointLp(b.x, b.y);
    switch (vk) {
        case 0x01: Dispatch(b.down ? WM_LBUTTONDOWN : WM_LBUTTONUP, 0, lp); break;
        case 0x02: Dispatch(b.down ? WM_RBUTTONDOWN : WM_RBUTTONUP, 0, lp); break;
        case 0x04: Dispatch(b.down ? WM_MBUTTONDOWN : WM_MBUTTONUP, 0, lp); break;
        default: Dispatch(b.down ? WM_XBUTTONDOWN : WM_XBUTTONUP, uintptr_t(vk == 0x05 ? XBUTTON1 : XBUTTON2) << 16, lp);
    }
}

// the mouse is held in the window (relative mode: raw movement past the screen's edges, the pointer hidden) while
// the game is played full screen with the mouse in use, as Windows' raw input reads it there; windowed, the pointer
// stays free and is hidden over the game. The menu shows it.
void UpdatePointer() {
    const Settings& s = Get();
    bool menu = OverlayOpen();
    bool mouse = s.keyboard && (s.mouse_aim || (kGame.mouse_camera && s.mouse_camera));
    bool hold = sdl::g_active && !menu && mouse && s.display_mode != DisplayMode::Windowed;
    if (SDL_GetWindowRelativeMouseMode(g_window) != hold) {
        SDL_SetWindowRelativeMouseMode(g_window, hold);
        g_motion_x = g_motion_y = 0;
    }
    bool show = menu || !sdl::g_active;
    if (show != SDL_CursorVisible()) show ? SDL_ShowCursor() : SDL_HideCursor();
    sdl::g_pointer_over = hold || SDL_GetMouseFocus() == g_window;
}

// ImGui takes the mouse in the window's pixels, as the Win32 backend does (the renderer draws the menu at pixel size)
void ToImGui(const SDL_Event& in) {
    std::lock_guard lock(UiLock());
    if (!ImGui::GetCurrentContext()) return;
    SDL_Event e = in;
    float d = SDL_GetWindowPixelDensity(g_window);
    if (d <= 0) d = 1;
    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION: e.motion.x *= d, e.motion.y *= d; break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: e.button.x *= d, e.button.y *= d; break;
        case SDL_EVENT_MOUSE_WHEEL: e.wheel.mouse_x *= d, e.wheel.mouse_y *= d; break;
    }
    // relative mode: the pointer is the game's, not the menu's
    if (e.type == SDL_EVENT_MOUSE_MOTION && SDL_GetWindowRelativeMouseMode(g_window)) return;
    ImGui_ImplSDL3_ProcessEvent(&e);
    // the backend's NewFrame would take the pointer away on the next frame; it isn't called (ImGuiPlatformNewFrame)
    if (e.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}

void Handle(const SDL_Event& e) {
    if (e.type == g_call_event) {
        auto* c = static_cast<Call*>(e.user.data1);
        c->fn();
        {
            std::lock_guard lock(c->m);
            c->done = true;
        }
        c->cv.notify_all();
        return;
    }
    if (!g_window) return;
    ToImGui(e);
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: Key(e.key); break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: Button(e.button); break;
        case SDL_EVENT_MOUSE_WHEEL: {
            float y = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
            auto delta = uint16_t(int16_t(std::clamp(std::lround(y * 120.0f), -32768l, 32767l)));
            Dispatch(WM_MOUSEWHEEL, uintptr_t(delta) << 16, PointLp(e.wheel.mouse_x, e.wheel.mouse_y));
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            // raw counts for the mouse's aim and camera (keyboard.cpp), whole ones; the rest waits for the next event
            g_motion_x += e.motion.xrel;
            g_motion_y += e.motion.yrel;
            int32_t dx = int32_t(g_motion_x), dy = int32_t(g_motion_y);
            g_motion_x -= float(dx);
            g_motion_y -= float(dy);
            if (dx || dy) keys::MouseMove(dx, dy);
            Dispatch(WM_MOUSEMOVE, 0, PointLp(e.motion.x, e.motion.y));
            break;
        }
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            sdl::g_active = true;
            Dispatch(WM_ACTIVATE, WA_ACTIVE, 0);
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            sdl::g_active = false;
            sdl::ReleaseAll();
            Dispatch(WM_ACTIVATE, WA_INACTIVE, 0);
            break;
        case SDL_EVENT_WINDOW_MOUSE_ENTER: sdl::g_pointer_over = true; break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE: sdl::g_pointer_over = SDL_GetWindowRelativeMouseMode(g_window); break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: UpdatePixelSize(); break;
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            // windowed onto a screen of another scale: the picture kept at the size asked for in pixels
            if (Get().display_mode == DisplayMode::Windowed) ApplyNow();
            UpdatePixelSize();
            break;
        case SDL_EVENT_DISPLAY_ADDED:
        case SDL_EVENT_DISPLAY_REMOVED:
        case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
        case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED: sdl::ScreensChanged(); break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        case SDL_EVENT_QUIT: Dispatch(WM_CLOSE, 0, 0); break;
        case SDL_EVENT_WINDOW_DESTROYED: g_alive = false; break;
    }
}

std::string Title() {
    std::string t = Utf8(kGame.title);
    if (Instance()) t += " [" + std::to_string(Instance()) + "]";
    return t;
}

}  // namespace

std::filesystem::path ExeDir() {
    static const std::filesystem::path dir = [] {
        std::error_code ec;
        auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
        return ec ? std::filesystem::current_path(ec) : p.parent_path();
    }();
    return dir;
}

const CommandLine& Args() {
    static const CommandLine c = [] {
        CommandLine c;
        const auto& v = Argv();
        size_t n = v.size();
        for (size_t i = 1; i < n; i++) {
            const std::string& a = v[i];
            auto next = [&]() -> std::filesystem::path { return i + 1 < n ? std::filesystem::path(v[++i]) : std::filesystem::path(); };
            if (a == "--game") c.game = next();
            else if (a == "--config") c.config = next();
            else if (a == "--textures") c.textures = next();
            else if (a == "--log") c.log = next();
            else if (a == "--saves") c.saves = next();
            else if (a == "--shader-cache") c.shader_cache = next();
            else if (a == "--screenshots") c.screenshots = next();
            else if (a == "--instance") c.instance = std::max(0, std::min(8, std::atoi(next().c_str())));
            else if (a == "--no-dialogs") c.no_dialogs = true;
            else if (a == "--any-release") c.any_release = true;
            else if (a == "--launcher") c.launcher = true;
            else if (a == "--version") c.version = true;
            else if (a == "--describe-settings") c.describe = true;
        }
        // relative paths are taken from where the launcher started us, before anything changes the directory
        std::error_code ec;
        for (auto* p : {&c.game, &c.config, &c.textures, &c.log, &c.saves, &c.shader_cache, &c.screenshots})
            if (!p->empty()) *p = std::filesystem::absolute(*p, ec);
        return c;
    }();
    return c;
}

std::filesystem::path ArgValue(const char* name) {
    std::filesystem::path p;
    const auto& v = Argv();
    for (size_t i = 1; i + 1 < v.size(); i++)
        if (v[i] == name) p = v[i + 1];
    return p;
}

int ExitingWith() { return g_exiting_with.load(); }

void Fail(int code, const std::wstring& message) {
    std::string text = Utf8(message);
    LOGE("Exit (%d): %s", code, text.c_str());
    logx::Flush();
    // the game's full-screen window would cover the box: hidden first (a changed mode goes back to the desktop's)
    RunOnWindowThread([&] {
        if (g_window) SDL_HideWindow(g_window);
        if (!Args().no_dialogs) {
            std::string title = Utf8(kGame.short_title);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), nullptr);
        }
    });
    netplay::End();         // online co-op: as kernel::Shutdown
    g_exiting_with = code;  // only now (after the box): the renderer's teardown then doesn't wait for a thread the exit ends
    std::_Exit(code);
}

bool AskYesNo(const std::wstring& message) {
    if (Args().no_dialogs) return false;
    std::string text = Utf8(message), title = Utf8(kGame.short_title);
    int pressed = 0;
    RunOnWindowThread([&] {
        const SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No"},
            {0, 1, "Yes"},
        };
        SDL_MessageBoxData box{SDL_MESSAGEBOX_WARNING, g_window, title.c_str(), text.c_str(), 2, buttons, nullptr};
        if (!SDL_ShowMessageBox(&box, &pressed)) pressed = 0;
    });
    return pressed == 1;
}

void* Window() { return g_window; }

bool WindowAlive() { return g_alive.load(); }

void WindowPixelSize(uint32_t& w, uint32_t& h) {
    w = g_pixel_w.load();
    h = g_pixel_h.load();
}

// the renderer's thread: ImGui's display size and clock. ImGui_ImplSDL3_NewFrame isn't called here: it sets the
// pointer's shape, reads the global mouse and opens pads through SDL, which belong on the window thread; the
// window thread feeds ImGui the events (ToImGui) and shows or hides the pointer (UpdatePointer)
void ImGuiPlatformNewFrame() {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(g_pixel_w.load()), float(g_pixel_h.load()));
    io.DisplayFramebufferScale = ImVec2(1, 1);
    static uint64_t last = SDL_GetTicksNS();
    uint64_t now = SDL_GetTicksNS();
    io.DeltaTime = now > last ? float(double(now - last) / 1e9) : 1.0f / 60.0f;
    last = now;
}

void OpenGameWindow(bool apply) {
    RunOnWindowThread([apply] {
        CloseSplash();
        if (apply) ApplyNow();
        ShowWindow();
    });
}

void CloseGameWindow() {
    SDL_Event e{};
    e.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    e.window.windowID = g_window ? SDL_GetWindowID(g_window) : 0;
    if (!g_window || !SDL_PushEvent(&e)) {
        e.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&e);
    }
}

namespace sdl {

bool StartVideo() {
    if (SDL_WasInit(SDL_INIT_VIDEO)) return true;
    // the name and id the desktop shows (Wayland's app id), set before video starts
    std::string title = Title(), id = "athelas.";
    for (wchar_t c : std::wstring(kGame.name)) id += char(std::tolower(int(c & 0x7F)));
    SDL_SetAppMetadata(title.c_str(), kGame.version, id.c_str());
    // under gamescope the game keeps the screen when another window takes the focus (main.cpp's ApplyNow)
    if (Gamescope()) SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO)) return true;
    LOGE("window: SDL's video didn't start: %s", SDL_GetError());
    return false;
}

}  // namespace sdl

bool StartWindow(bool show) {
    g_main_thread = std::this_thread::get_id();
    if (!sdl::StartVideo()) return false;
    std::string title = Title();
    LOGR("window: SDL %d.%d.%d, %s", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION, SDL_GetCurrentVideoDriver());
    sdl::LogScreens();
    g_call_event = SDL_RegisterEvents(1);
    SDL_WindowFlags flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_VULKAN;
    g_window = SDL_CreateWindow(title.c_str(), 1280, 720, flags);
    if (!g_window) {
        // no Vulkan loader: the window anyway, and the renderer says what is missing
        LOGE("window: no Vulkan window (%s), a plain one", SDL_GetError());
        g_window = SDL_CreateWindow(title.c_str(), 1280, 720, flags & ~SDL_WINDOW_VULKAN);
    }
    if (!g_window) {
        LOGE("window: SDL couldn't make the window: %s", SDL_GetError());
        return false;
    }
    g_alive = true;
    ApplyNow();
    if (show) ShowWindow();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDL3_InitForVulkan(g_window);
    // the in-game menu reads the controllers itself (overlay.cpp), as on Windows
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
    OverlayInit();
    return true;
}

int RunWindowLoop() {
    g_looping = true;
    while (!g_quit) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 10)) {
            Handle(e);
            while (!g_quit && SDL_PollEvent(&e)) Handle(e);
        }
        if (g_window) UpdatePointer();
    }
    g_loop_done = true;
    // the window goes at once (a changed display mode back to the desktop's); the renderer may still be presenting
    // to it, so it is hidden, not destroyed: the process ends next
    if (g_window) {
        SDL_SetWindowRelativeMouseMode(g_window, false);
        SDL_HideWindow(g_window);
    }
    // calls still waiting
    SDL_Event e;
    while (SDL_PollEvent(&e))
        if (e.type == g_call_event) Handle(e);
    return g_quit_code.load();
}

void Pace() {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    uint32_t cap = PaceCap(Focused() && keys::Held(0x78));  // VK_F9
    auto now = clock::now();
    if (!cap) {
        next = now;
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    next += period;
    if (next < now - period) next = now;
    // an absolute sleep on the monotonic clock (steady_clock's) wakes within a few tens of microseconds; the last
    // quarter millisecond is spun, as on Windows
    auto wake = next - std::chrono::microseconds(250);
    if (wake > clock::now()) {
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(wake.time_since_epoch()).count();
        timespec ts{time_t(ns / 1000000000), long(ns % 1000000000)};
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
        }
    }
    while (clock::now() < next) std::this_thread::yield();
}

}  // namespace app

// the game (a port's own main; a test such as the smoke target links its own)
__attribute__((weak)) int main(int argc, char** argv) {
    app::g_argv.assign(argv, argv + argc);
    // a launcher's questions, answered before anything else starts
    if (app::Args().version) {
        std::printf("%s\n", app::kGame.version);
        return app::kExitOk;
    }
    if (app::Args().describe) {
        std::fputs(cfg::Describe(app::SettingsList(), app::kGame.presets).c_str(), stdout);
        return app::kExitOk;
    }
    app::sdl::InstallCrashHandler();
    // one log a session: a relaunch into another image carries on its predecessor's
    bool relaunched = std::find(app::g_argv.begin(), app::g_argv.end(), "--relaunched") != app::g_argv.end();
    std::string log = app::kGame.log;
    if (app::Instance() > 1) log += "-" + std::to_string(app::Instance());
    if (!app::Args().log.empty()) log = app::Args().log.stem().string();
    logx::Init(app::LogFolder().string(), log, relaunched);
    netplay::g_status = app::NetplayStatus;
    netplay::g_cancelled = app::NetplayCancelled;
    netplay::g_windowless = app::NetplayWindowless;
    app::Load();
    if (app::kGame.init) app::kGame.init();
    if (relaunched && !logx::kVerbose) LOGR("Relaunched: %s", IMAGE_XBE);
    else app::LogSystem();

    app::GameFiles game;
    if (!app::FindGame(game)) return app::kExitNoGame;

    // behind a splash the window stays hidden until the game's first frame; handed over, it is set up hidden
    bool splash = app::OpenSplash(), handover = app::StartHandover();
    if (!app::StartWindow(!splash && !handover))
        app::Fail(1, L"The game window couldn't be opened, see the log in the logs folder.");

    input::Init();
    ach::Start();
    discord::Start();
    if (app::kGame.window) app::kGame.window(app::g_window);
    if (app::Get().one_core) {
        uint64_t m = app::OneCoreMask();
        kernel::SetGuestAffinity(m);
        LOGR("system: the game's threads on one core (mask %llX)", (unsigned long long)m);
    }
    if (!kernel::Boot(std::move(game.xbe), IMAGE_XBE, game.dir.string(), app::SavesDir().string())) {
        if (kernel::g_boot_wrong_release)
            app::Fail(app::kExitWrongRelease,
                      L"This copy of the game is a different version from the US release this port was made from, so it "
                      L"can't start. Only the US version is supported for now (Athelas' language packs work on top of it).");
        app::Fail(1, L"The game failed to start, see the log in the logs folder.");
    }

    int code = app::RunWindowLoop();
    ach::Flush();
    logx::Flush();
    kernel::Shutdown(code);
}
#endif
