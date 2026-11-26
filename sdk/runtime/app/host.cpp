// the window layer's parts that are the same on every system: the folders, fast forward and the frame cap's speed,
// the window's own keys. The window itself is main.cpp's (Win32) or main_sdl.cpp's (SDL3).
#include "app/host.h"

#include <algorithm>
#include <atomic>
#include <string>

#include "app/app.h"
#include "core/diag.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"
#include "xdk/netplay.h"

namespace app {

int Instance() { return Args().instance; }

std::filesystem::path TextureDir() { return Args().textures.empty() ? ExeDir() / "textures" : Args().textures; }

std::filesystem::path LogFolder() { return Args().log.empty() ? ExeDir() / "logs" : Args().log.parent_path(); }

std::filesystem::path SavesDir() {
    if (auto p = netplay::SessionSaves(); !p.empty()) return p;  // online co-op, player 2: player 1's saves
    // started on its own: the saves Athelas keeps beside the exe (saves) when it has played the game, else hdd
    std::error_code ec;
    std::filesystem::path base = !Args().saves.empty()                              ? Args().saves
                                 : std::filesystem::is_directory(ExeDir() / "saves", ec) ? ExeDir() / "saves"
                                                                                      : ExeDir() / "hdd";
    // a second copy for system link tests keeps a disk of its own
    if (Instance() > 1) base += std::to_string(Instance());
    return base;
}

std::filesystem::path ScreenshotDir() { return Args().screenshots.empty() ? ExeDir() / "Screenshots" : Args().screenshots; }

std::filesystem::path ShaderCacheDir() {
    return Args().shader_cache.empty() ? ExeDir() / "shadercache" : Args().shader_cache;
}

std::atomic<int> g_quit_code{kExitOk};

std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

bool FastForward() { return kernel::Speed() > 1; }

namespace {
std::atomic<bool> g_ff_on{false};        // fast forward turned on (F9 in toggle mode, or the controller's combination)
std::atomic<uint32_t> g_game_speed{1};   // SetGameSpeed

void DumpThreads() {
    gpu::g_capture = true;
    for (auto& t : kernel::Threads())
        if (t->ctx) {
            LOGI("thread %X", t->id);
            kernel::LogGuestStack(*t->ctx, 32);
        }
}
}  // namespace

void ToggleFastForward() {
    bool on = !g_ff_on.load();
    g_ff_on = on;
    LOGR("fast forward: %s", on ? "on" : "off");
}

void SetGameSpeed(uint32_t times) { g_game_speed = std::max(1u, std::min(8u, times)); }

uint32_t PaceCap(bool f9) {
    // fast forward: F9 held in the game window while it isn't paused (or, with [system] fast_forward_toggle, pressed
    // once to turn it on and again to turn it off); the guest's clocks run faster (kernel::SetSpeed) and the frame
    // limit, the vertical blanks and V-Sync let that many more frames through
    static bool f9_was = false;
    // toggle mode switched off from the menu while fast forward was on: off with it, or it would stay on with no F9
    // press left to end it (review of 1.8.0)
    static bool toggle_was = Get().fast_forward_toggle;
    if (toggle_was && !Get().fast_forward_toggle && g_ff_on.load()) ToggleFastForward();
    toggle_was = Get().fast_forward_toggle;
    if (Get().fast_forward_toggle && f9 && !f9_was && !Paused()) ToggleFastForward();
    f9_was = f9;
    bool ff = g_ff_on.load() || (!Get().fast_forward_toggle && f9);
    uint32_t times = 1;
    if (!Paused()) {
        if (ff && !netplay::Active()) times = std::max(2u, std::min(8u, Get().fast_forward_speed));  // not online
        // the game's own speed-up (its battles) when fast forward isn't faster
        times = std::max(times, g_game_speed.load());
    }
    static bool told = false;
    if (ff && times > 1 && !told) {
        told = true;
        LOGR("fast forward: %ux (sound off while it runs)", times);
    }
    kernel::SetSpeed(times);
    return Get().fps_cap * times;
}

void HostKey(uintptr_t wp, intptr_t lp) {
    constexpr uintptr_t kOem4 = 0xDB, kOem5 = 0xDC, kOem6 = 0xDD, kF7 = 0x76, kF8 = 0x77;  // VK_OEM_4 ...
    if (lp & (1 << 30)) return;  // held: once a press
    // M: a frame capture and a thread dump, for testing only (XBE_CAPTURE_DIR set, capture.bat): a player who
    // presses it by accident would otherwise find alarming "error:" lines in the log
    if (wp == 'M' && !diag::CaptureDir().empty()) DumpThreads();
    // \, the key above Enter on a US keyboard (VK_OEM_5): a screenshot, once a press
    if (wp == kOem5) gpu::g_screenshot = true;
    // F8: texture packs off and on, to compare; F7: a pair of screenshots, with and without them
    if (wp == kF8) {
        bool off = !gpu::g_pack_off;
        gpu::g_pack_off = off;
        LOGR("texture packs %s (F8)", off ? "off" : "on");
    }
    if (wp == kF7) gpu::g_pack_pair = true;
    // ] (VK_OEM_6): the next colour look, as ROTK Fix
    if (wp == kOem6) {
        static const char* const kNames[] = {"original", "film", "vivid", "dark fantasy"};
        uint32_t n = (gpu::g_colour_look + 1) % gpu::kColourLooks;
        gpu::g_colour_look = n;
        LOGR("colour look %s (])", kNames[n]);
    }
    // [ (VK_OEM_4): sharpening off and on, to compare, as ROTK Fix
    if (wp == kOem4 && Get().sharpening) {
        bool off = !gpu::g_sharpen_off;
        gpu::g_sharpen_off = off;
        LOGR("sharpening %s ([)", off ? "off" : "on");
    }
}

}  // namespace app
