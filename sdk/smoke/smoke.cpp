// xbe_smoke: the runtime started without a game, as far as each platform's host side goes so far.
//   xbe_smoke [--window] [--frames N]
// Today: logging, the settings file and guest memory, then exit 0. --window and --frames are read but wait for the
// SDL3 window (they will open it and draw N frames through the Vulkan backend, with sound).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/app.h"
#include "core/log.h"
#include "kernel/memory.h"

// what a game's src/common/game_info.cpp gives
const app::GameInfo app::kGame = {
    .title = L"XBE smoke test",
    .short_title = L"Smoke",
    .name = L"XbeSmoke",
    .log = "smoke",
    .settings = "smoke.toml",
    .version = "xbe_smoke",
    .splash = L"",
    .missing = L"No game: the smoke test runs without one.",
    .images = {{"stand-in.xbe", "xbe_smoke"}},
};
// what CMake writes from the xbe (add_xbe_image)
extern const char kImageSha1[] = "";

namespace {

struct Options {
    bool window = false;
    int frames = 0;
};

bool Parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--window")) {
            o.window = true;
        } else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) {
            o.frames = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "usage: %s [--window] [--frames N]\n", argv[0]);
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

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!Parse(argc, argv, o)) return 2;
    logx::Init(app::LogFolder().string(), app::kGame.log);
    LOGR("smoke: start");
    app::Load();
    LOGR("smoke: settings from %s", app::SettingsFile().string().c_str());
    if (!mem::Init()) return Fail("guest memory");
    LOGR("smoke: guest memory up");
    if (o.window || o.frames) LOGR("smoke: --window and --frames wait for the SDL3 window (M1 workstream B)");
    LOGR("smoke: done");
    logx::Flush();
    std::printf("xbe_smoke: ok\n");
    return 0;
}
