// xbe_smoke: the runtime started without a game, as far as each platform's host side goes so far.
//   xbe_smoke [--window] [--vulkan] [--audio] [--frames N] [--fps N] [--display-change] [--splash] [--discord ID]
// Logging, the settings file and guest memory, then exit 0. --window (off Windows) opens the game window and runs its
// event loop as a port does: keys, mouse buttons, the wheel, focus and close reach GameInfo::message as Win32
// messages and are logged, and a frame thread paces frames at --fps (60; 0 none), logging the rate each second.
// --frames N closes the window after N frames (0: when it is closed).
// --vulkan (off Windows, instead of --window) opens the game window and draws the frames through the Vulkan backend
// with the ImGui overlay, paced at --fps too (with v-sync, the screen's refresh is the most); --audio plays a test tone through the platform audio output, alone for N / 60 seconds or
// while the frames are drawn (smoke_av.cpp). Exit 1 when either fails.
// --display-change (off Windows, with --vulkan): a display's mode changed and the window's HDR state changed, as SDL
// says them on a hot-plug, twice a second while the frames read the desktop mode the window thread keeps.
// --splash (off Windows, with --window or --vulkan): the boot splash, smoke-splash.png or .bmp beside the exe, up
// first, the window opening behind it at the first frame as a game's does.
// --discord <application id> (with --window): the Discord status on, the frame count as its state, switched off
// again for the last quarter of the frames (the status cleared, the connection closed).
// --crash segv|abort|overflow (off Windows): the crash handler installed, then that crash, on a thread the way the
// kernel starts the game's (its own signal stack): a crash log should land in CrashDumps beside the exe.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>  // off Windows the compat header: the WM_ and VK_ names

#include "app/app.h"
#include "core/keys.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#ifndef _WIN32
#include <csignal>

#include "app/sdl_window.h"
#endif

namespace {
bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);
std::atomic<int> g_frames{0};
std::string Activity() { return "Smoke test"; }
std::string ActivityState() { return "Frame " + std::to_string(g_frames.load() / 60 * 60); }
}
namespace smoke {  // smoke_av.cpp
extern bool g_display_change;
extern bool g_splash;
void Ui(double fps);
int Run(bool vulkan, bool sound, int frames);
}  // namespace smoke

// what a game's src/common/game_info.cpp gives
const app::GameInfo app::kGame = {
    .title = L"XBE smoke test",
    .short_title = L"Smoke",
    .name = L"XbeSmoke",
    .log = "smoke",
    .settings = "smoke.toml",
    .version = "xbe_smoke",
    .splash = L"smoke-splash",  // --splash
    .missing = L"No game: the smoke test runs without one.",
    .images = {{"stand-in.xbe", "xbe_smoke"}},
    .message = Message,
    .ui = smoke::Ui,
    .activity = Activity,            // --discord
    .activity_state = ActivityState,
};
// what CMake writes from the xbe (add_xbe_image)
extern const char kImageSha1[] = "";

namespace {

struct Options {
    std::string discord;
    bool window = false, vulkan = false, audio = false;
    int frames = 0;
    int fps = 60;
    std::string crash;
};

// what the window hands the game, logged as it comes (a game's own hooks see the same)
bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t*) {
    auto x = int(int16_t(LOWORD(lp))), y = int(int16_t(HIWORD(lp)));
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            LOGR("smoke: %s %02X, scan code %u%s (shift %d)", msg == WM_KEYDOWN ? "key down" : "system key down",
                 unsigned(wp), unsigned(lp >> 16 & 0xFF), lp & (intptr_t(1) << 30) ? " repeat" : "", keys::Held(VK_SHIFT));
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP: LOGR("smoke: key up %02X", unsigned(wp)); break;
        case WM_LBUTTONDOWN: LOGR("smoke: left button down at %d,%d", x, y); break;
        case WM_RBUTTONDOWN: LOGR("smoke: right button down at %d,%d", x, y); break;
        case WM_MBUTTONDOWN: LOGR("smoke: middle button down at %d,%d", x, y); break;
        case WM_XBUTTONDOWN: LOGR("smoke: side button %u down at %d,%d", unsigned(HIWORD(wp)), x, y); break;
        case WM_MOUSEWHEEL: LOGR("smoke: wheel %d", int(GET_WHEEL_DELTA_WPARAM(wp))); break;
        case WM_ACTIVATE: LOGR("smoke: %s", LOWORD(wp) == WA_INACTIVE ? "focus lost" : "focus"); break;
        case WM_CLOSE: LOGR("smoke: close"); break;
    }
    return false;
}

#ifndef _WIN32
__attribute__((noinline)) int Deeper(volatile int* p) {
    volatile char pad[4096];
    pad[0] = char(*p);
    return Deeper(p) + pad[0];
}

void Crash(const std::string& kind) {
    app::sdl::InstallCrashHandler();
    std::thread t([&] {
        if (kernel::g_thread_started) kernel::g_thread_started();
        volatile int zero = 0;
        if (kind == "abort") std::abort();
        if (kind == "overflow") Deeper(&zero);
        *reinterpret_cast<volatile int*>(uintptr_t(zero) + 16) = 1;
    });
    t.join();
}

// the window thread's loop on the main thread, as a port's; frames from another thread, as the game's
int RunWindow(const Options& o) {
    bool splash = smoke::g_splash && app::OpenSplash();
    if (!app::StartWindow(!splash)) return 1;
    if (!o.discord.empty()) {
        app::Get().discord = true;
        app::Get().discord_app_id = o.discord;
        discord::Start();
    }
    uint32_t w = 0, h = 0;
    app::WindowPixelSize(w, h);
    LOGR("smoke: window up, %ux%u pixels, alive %d", w, h, app::WindowAlive());
    std::atomic<bool> stop{false};
    std::thread frames([&] {
        using clock = std::chrono::steady_clock;
        auto start = clock::now(), second = start;
        int n = 0, last = 0;
        while (!stop && (!o.frames || n < o.frames)) {
            app::SplashFirstFrame();
            app::Pace();
            n++;
            g_frames = n;
            if (!o.discord.empty() && o.frames && n == o.frames * 3 / 4) {
                LOGR("smoke: Discord status off");
                app::Get().discord = false;
            }
            if (clock::now() - second >= std::chrono::seconds(1)) {
                uint32_t pw = 0, ph = 0;
                app::WindowPixelSize(pw, ph);
                LOGR("smoke: %d frames a second, %ux%u pixels", n - last, pw, ph);
                last = n;
                second += std::chrono::seconds(1);
            }
        }
        double secs = std::chrono::duration<double>(clock::now() - start).count();
        LOGR("smoke: %d frames in %.3f s (%.2f a second)", n, secs, secs > 0 ? n / secs : 0.0);
        if (!stop) app::CloseGameWindow();
    });
    int code = app::RunWindowLoop();
    stop = true;
    frames.join();
    LOGR("smoke: window closed (%d)", code);
    return code;
}
#endif

bool Parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--window")) {
            o.window = true;
        } else if (!std::strcmp(argv[i], "--vulkan")) {
            o.vulkan = true;
        } else if (!std::strcmp(argv[i], "--audio")) {
            o.audio = true;
        } else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) {
            o.frames = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--fps") && i + 1 < argc) {
            o.fps = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--display-change")) {
            smoke::g_display_change = true;
        } else if (!std::strcmp(argv[i], "--splash")) {
            smoke::g_splash = true;
        } else if (!std::strcmp(argv[i], "--discord") && i + 1 < argc) {
            o.discord = argv[++i];
        } else if (!std::strcmp(argv[i], "--crash") && i + 1 < argc) {
            o.crash = argv[++i];
        } else {
            std::fprintf(stderr, "usage: %s [--window] [--vulkan] [--audio] [--frames N] [--fps N] [--display-change] [--splash] [--discord ID] [--crash segv|abort|overflow]\n", argv[0]);
            return false;
        }
    }
    return true;
}

int Fail(const char* what) {
    LOGE("smoke: %s failed", what);
    logx::Flush();
    std::fprintf(stderr, "xbe_smoke: %s failed (logs/)\n", what);
    return 1;
}

int Smoke(int argc, char** argv) {
    Options o;
    if (!Parse(argc, argv, o)) return 2;
    logx::Init(app::LogFolder().string(), app::kGame.log);
    LOGR("smoke: start");
    app::Load();
    app::Get().fps_cap = uint32_t(std::max(o.fps, 0));  // the frame cap of --window's and --vulkan's frames
    LOGR("smoke: settings from %s", app::SettingsFile().string().c_str());
    if (!mem::Init()) return Fail("guest memory");
    LOGR("smoke: guest memory up");
#ifndef _WIN32
    if (!o.crash.empty()) Crash(o.crash);
    if (o.vulkan || o.audio) {
        // (the sound's thread is still running: no destructors)
        int code = smoke::Run(o.vulkan, o.audio, o.frames);
        logx::Flush();
        std::printf("xbe_smoke: %s\n", code ? "failed (logs/)" : "ok");
        std::fflush(stdout);
        std::_Exit(code);
    }
    if (o.window) {
        if (int code = RunWindow(o)) return Fail("the window");
    }
#endif
    LOGR("smoke: done");
    logx::Flush();
    std::printf("xbe_smoke: ok\n");
    return 0;
}

}  // namespace

#ifdef _WIN32
// MinGW links with -municode (XbeRecomp.cmake), which starts at wmain: the arguments as UTF-8
int wmain(int argc, wchar_t** wargv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string a(size_t(n > 0 ? n : 1), '\0');
        if (n > 0) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, a.data(), n, nullptr, nullptr);
        a.resize(std::strlen(a.c_str()));
        args.push_back(a);
    }
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    return Smoke(argc, argv.data());
}
#else
int main(int argc, char** argv) { return Smoke(argc, argv); }
#endif
