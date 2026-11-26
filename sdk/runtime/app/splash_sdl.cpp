// The boot splash off Windows (splash.cpp's twin): GameInfo::splash .bmp or .png beside the exe, centred in a
// borderless SDL window. The game boots behind it with its window hidden and waits at its first frame until the
// splash has been up kMinimum. No file, or a relaunch: no splash.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "app/app.h"
#include "app/sdl_window.h"
#include "core/log.h"
#include "gpu/stb/stb_image.h"

namespace app {

namespace {

constexpr auto kMinimum = std::chrono::seconds(1);

SDL_Window* g_splash = nullptr;
SDL_Renderer* g_draw = nullptr;
SDL_Cursor* g_wait = nullptr;
std::atomic<bool> g_waiting{false};
std::chrono::steady_clock::time_point g_until;

// RGBA, premultiplied as a compositor takes a see-through window's (UpdateLayeredWindow's PBGRA on Windows); clear:
// some pixel isn't opaque
SDL_Surface* Read(const std::filesystem::path& path, bool& clear) {
    SDL_Surface* s = nullptr;
    if (path.extension() == ".bmp") {
        if (SDL_Surface* bmp = SDL_LoadBMP(path.c_str())) {
            s = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(bmp);
        }
    } else {
        std::ifstream f(path, std::ios::binary);
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        int w = 0, h = 0, n = 0;
        stbi_uc* px = data.empty() ? nullptr : stbi_load_from_memory(data.data(), int(data.size()), &w, &h, &n, 4);
        if (px && (s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32)))
            for (int y = 0; y < h; y++)
                std::copy_n(px + size_t(y) * w * 4, size_t(w) * 4, static_cast<uint8_t*>(s->pixels) + size_t(y) * s->pitch);
        if (px) stbi_image_free(px);
    }
    if (!s || !s->w || !s->h) {
        LOGW("splash: %s could not be read (%s)", path.string().c_str(), s ? "empty" : SDL_GetError());
        if (s) SDL_DestroySurface(s);
        return nullptr;
    }
    clear = false;
    for (int y = 0; y < s->h; y++) {
        auto* p = static_cast<uint8_t*>(s->pixels) + size_t(y) * s->pitch;
        for (int x = 0; x < s->w; x++, p += 4) {
            if (p[3] == 255) continue;
            clear = true;
            for (int c = 0; c < 3; c++) p[c] = uint8_t((p[c] * p[3] + 127) / 255);
        }
    }
    return s;
}

}  // namespace

bool OpenSplash() {
    if (sdl::Relaunched()) return false;
    SDL_Surface* image = nullptr;
    bool clear = false;
    std::filesystem::path path;
    for (const char* ext : {".bmp", ".png"}) {
        std::error_code ec;
        path = ExeDir() / (std::filesystem::path(kGame.splash).string() + ext);
        if (std::filesystem::exists(path, ec) && (image = Read(path, clear))) break;
    }
    if (!image) return false;
    if (!sdl::StartVideo()) {
        SDL_DestroySurface(image);
        return false;
    }

    // centred on the primary screen at one image pixel per screen pixel (a scaled desktop's points are larger)
    SDL_DisplayID screen = SDL_GetPrimaryDisplay();
    float scale = SDL_GetDisplayContentScale(screen);
    if (scale <= 0) scale = 1;
    int w = std::max(1, int(std::lround(image->w / scale))), h = std::max(1, int(std::lround(image->h / scale)));
    std::string title = std::filesystem::path(kGame.title).string();
    SDL_WindowFlags flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (clear) flags |= SDL_WINDOW_TRANSPARENT;
    g_splash = SDL_CreateWindow(title.c_str(), w, h, flags);
    if (!g_splash) {
        LOGW("splash: no window (%s)", SDL_GetError());
        SDL_DestroySurface(image);
        return false;
    }
    SDL_SetWindowPosition(g_splash, int(SDL_WINDOWPOS_CENTERED_DISPLAY(screen)), int(SDL_WINDOWPOS_CENTERED_DISPLAY(screen)));
    g_wait = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
    if (g_wait) SDL_SetCursor(g_wait);
    // drawn through SDL's renderer: on X11 its software one (at once, no GL context); Wayland has no window surface
    // to write to, so its GPU one there
    const char* video = SDL_GetCurrentVideoDriver();
    if (video && std::strcmp(video, "x11") == 0) g_draw = SDL_CreateRenderer(g_splash, SDL_SOFTWARE_RENDERER);
    if (!g_draw) g_draw = SDL_CreateRenderer(g_splash, nullptr);
    SDL_Texture* tex = g_draw ? SDL_CreateTextureFromSurface(g_draw, image) : nullptr;
    if (!tex) {
        LOGW("splash: can't draw it (%s)", SDL_GetError());
        CloseSplash();
        SDL_DestroySurface(image);
        return false;
    }
    sdl::Show(g_splash);
    SDL_SyncWindow(g_splash);
    // the picture once, as it is (premultiplied, see-through where it is) over a clear window; the compositor keeps
    // it. Scaled only when the screen's scale rounds the window's size
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    SDL_SetRenderDrawColor(g_draw, 0, 0, 0, 0);
    SDL_RenderClear(g_draw);
    SDL_RenderTexture(g_draw, tex, nullptr, nullptr);
    SDL_RenderPresent(g_draw);
    SDL_DestroyTexture(tex);
    SDL_RaiseWindow(g_splash);
    LOGI("splash: drawn by SDL's %s renderer", SDL_GetRendererName(g_draw));
    g_until = std::chrono::steady_clock::now() + kMinimum;
    g_waiting = true;
    LOGI("splash: %s, %dx%d", path.string().c_str(), image->w, image->h);
    SDL_DestroySurface(image);
    return true;
}

void SplashFirstFrame() {
    if (!g_waiting.exchange(false)) return;
    std::this_thread::sleep_until(g_until);
    OpenGameWindow();
}

void CloseSplash() {
    if (g_draw) SDL_DestroyRenderer(g_draw);
    g_draw = nullptr;
    if (g_splash) SDL_DestroyWindow(g_splash);
    g_splash = nullptr;
    if (g_wait) {
        SDL_SetCursor(SDL_GetDefaultCursor());
        SDL_DestroyCursor(g_wait);
        g_wait = nullptr;
    }
}

}  // namespace app
