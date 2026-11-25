// TEMPORARY (Linux M1, notes/mac-linux-plan.md): stand ins, off Windows, for what the Win32-only sources left out of
// that build provided (sdk/cmake/XbeRecomp.cmake, XBE_WIN32_ONLY_SOURCES), so the runtime links. Each workstream
// deletes its stand ins as the real code lands; once nothing is left the file goes too. Nothing here is final.
//   window/app layer (M1-B): app/main.cpp, keyboard.cpp, splash.cpp, netplay_window.cpp, discord.cpp,
//                            system_info.cpp, handover.cpp, settings.cpp's screens
//   renderer/audio (M1-C):   none left (gpu/vk.cpp's SDL surface, gpu/backend.cpp and audio/sdl_audio.cpp landed)
#ifndef _WIN32

#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "app/app.h"
#include "core/input.h"
#include "core/keys.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/netplay.h"

// ---- window/app layer (M1-B) ----

namespace {
// the command line, as /proc/self/cmdline holds it
std::vector<std::string> ReadArgs() {
    std::vector<std::string> v;
    std::ifstream f("/proc/self/cmdline", std::ios::binary);
    for (std::string a; std::getline(f, a, '\0');) v.push_back(a);
    return v;
}
std::string Narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += c < 0x80 ? char(c) : '?';
    return s;
}
}  // namespace

// stand in for main.cpp's wWinMain: replaced by app/main_sdl.cpp. Weak, so a test program's own main wins (sdk/smoke).
__attribute__((weak)) int main() {
    std::fprintf(stderr, "%ls: the Linux window layer is not built yet (notes/mac-linux-plan.md, M1)\n",
                 app::kGame.title);
    return 1;
}

namespace app {

// main.cpp's paths and command line: replaced by main_sdl.cpp
std::filesystem::path ExeDir() {
    std::error_code ec;
    static const std::filesystem::path dir = std::filesystem::read_symlink("/proc/self/exe", ec).parent_path();
    return dir;
}
std::filesystem::path ArgValue(const char* name) {
    static const std::vector<std::string> v = ReadArgs();
    for (size_t i = 1; i + 1 < v.size(); i++)
        if (v[i] == name) return v[i + 1];
    return {};
}
const CommandLine& Args() {
    static const CommandLine c = [] {
        CommandLine c;
        std::vector<std::string> v = ReadArgs();
        for (size_t i = 1; i < v.size(); i++) {
            const std::string& a = v[i];
            auto next = [&]() -> std::filesystem::path { return i + 1 < v.size() ? std::filesystem::path(v[++i]) : std::filesystem::path(); };
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
        std::error_code ec;
        for (auto* p : {&c.game, &c.config, &c.textures, &c.log, &c.saves, &c.shader_cache, &c.screenshots})
            if (!p->empty()) *p = std::filesystem::absolute(*p, ec);
        return c;
    }();
    return c;
}
int Instance() { return Args().instance; }
std::filesystem::path TextureDir() { return Args().textures.empty() ? ExeDir() / "textures" : Args().textures; }
std::filesystem::path LogFolder() { return Args().log.empty() ? ExeDir() / "logs" : Args().log.parent_path(); }
std::filesystem::path SavesDir() {
    if (auto p = netplay::SessionSaves(); !p.empty()) return p;
    std::error_code ec;
    std::filesystem::path base = !Args().saves.empty()                                   ? Args().saves
                                 : std::filesystem::is_directory(ExeDir() / "saves", ec) ? ExeDir() / "saves"
                                                                                         : ExeDir() / "hdd";
    if (Instance() > 1) base += std::to_string(Instance());
    return base;
}
std::filesystem::path ScreenshotDir() { return Args().screenshots.empty() ? ExeDir() / "Screenshots" : Args().screenshots; }
std::filesystem::path ShaderCacheDir() {
    return Args().shader_cache.empty() ? ExeDir() / "shadercache" : Args().shader_cache;
}

// main.cpp's exit, message box and crash handler: replaced by main_sdl.cpp (SDL message box, sigaction handler)
std::atomic<int> g_quit_code{kExitOk};
namespace {
std::atomic<int> g_exiting_with{-1};
}
int ExitingWith() { return g_exiting_with.load(); }
void Fail(int code, const std::wstring& message) {
    LOGE("Exit (%d): %s", code, Narrow(message).c_str());
    logx::Flush();
    std::fprintf(stderr, "%s\n", Narrow(message).c_str());
    netplay::End();
    g_exiting_with = code;
    std::_Exit(code);
}
bool AskYesNo(const std::wstring& message) {
    LOGW("question not asked (no window yet), answered No: %s", Narrow(message).c_str());
    return false;
}

// main.cpp's window: replaced by main_sdl.cpp (the SDL_Window, made with SDL_WINDOW_VULKAN)
void* Window() { return nullptr; }
bool WindowAlive() { return false; }
void WindowPixelSize(uint32_t& w, uint32_t& h) { w = h = 0; }
void ImGuiPlatformNewFrame() {}
void CloseGameWindow() {
    LOGR("Exit (%d)", g_quit_code.load());
    logx::Flush();
    std::_Exit(g_quit_code.load());
}
void OpenGameWindow(bool) {}
std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

// main.cpp's frame pacing and fast forward: replaced by main_sdl.cpp (clock_nanosleep plus a short spin, F9 via SDL)
namespace {
std::atomic<bool> g_ff_on{false};
std::atomic<uint32_t> g_game_speed{1};
}  // namespace
bool FastForward() { return kernel::Speed() > 1; }
void ToggleFastForward() {
    g_ff_on = !g_ff_on.load();
    LOGR("fast forward: %s", g_ff_on.load() ? "on" : "off");
}
void SetGameSpeed(uint32_t times) { g_game_speed = std::max(1u, std::min(8u, times)); }
void Pace() {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    uint32_t times = 1;
    if (!Paused()) {
        if (g_ff_on.load() && !netplay::Active()) times = std::max(2u, std::min(8u, Get().fast_forward_speed));
        times = std::max(times, g_game_speed.load());
    }
    kernel::SetSpeed(times);
    uint32_t cap = Get().fps_cap * times;
    auto now = clock::now();
    if (!cap) {
        next = now;
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    next += period;
    if (next < now - period) next = now;
    std::this_thread::sleep_until(next);
}

// settings.cpp's screens: replaced by app/screens_sdl.cpp (SDL_GetDisplays, SDL_GetDisplayBounds)
const std::vector<std::string>& ScreenNames() {
    static const std::vector<std::string> n{"main"};
    return n;
}
void* ScreenMonitor() { return nullptr; }
Mode Desktop() { return {1280, 720}; }

// splash.cpp: replaced by an SDL splash (after M1)
bool OpenSplash() { return false; }
void SplashFirstFrame() {}
void CloseSplash() {}

// netplay_window.cpp: replaced by an SDL status window (after M1); without it online co-op runs windowless
void NetplayStatus(const std::string& text) { LOGR("online co-op: %s", text.c_str()); }
bool NetplayCancelled() { return false; }
bool NetplayWindowless() { return true; }

// handover.cpp: replaced after M1 (a relaunch keeps no window up meanwhile)
bool StartHandover() { return false; }
bool HandingOver() { return false; }
void FinishHandover() {}

// system_info.cpp: replaced by /proc/cpuinfo and uname (after M1)
uint64_t OneCoreMask() { return 1; }
void LogSystem() {
    utsname u{};
    if (uname(&u) == 0) LOGR("System: %s %s %s", u.sysname, u.release, u.machine);
    LogSettings();
}

}  // namespace app

// keyboard.cpp: replaced by SDL3 keyboard state and relative mouse mode (M1-B)
namespace keys {
void Merge(int, input::Pad&) {}
void Mouse(void*, uint32_t, uintptr_t, intptr_t) {}
void Latch(uint32_t) {}
bool Claims(int) { return false; }
bool Over() { return false; }
void Swallow() {}
std::string FirstKey(Action) { return {}; }
}  // namespace keys

// discord.cpp: replaced after M1 (Discord's IPC socket instead of its named pipe)
namespace discord {
void Start() {}
}  // namespace discord

#endif  // !_WIN32
