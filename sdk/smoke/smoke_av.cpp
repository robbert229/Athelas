// xbe_smoke's picture and sound: the real Vulkan backend on the game window, and a test tone through the platform
// audio output, with no game. Guest memory must be up (mem::Init) first.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>

#include <imgui.h>

#include "app/app.h"
#include "audio/audio.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/state.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#ifndef _WIN32
#include "app/sdl_window.h"
#endif

namespace smoke {

bool g_display_change = false;

namespace {

constexpr uint32_t kW = 640, kH = 480;
constexpr uint32_t kLinA8R8G8B8 = 0x12;  // the console's linear 32-bit colour format
constexpr uint32_t kTriangles = 3;

// a colour surface as Direct3D_CreateDevice makes the title's buffers (xdk/d3d8.cpp NewSurface)
gpu::Surface NewSurface() {
    uint32_t pitch = kW * 4;
    uint32_t data = mem::AllocContiguous(pitch * kH, 0, 0xFFFFFFFF, 0x1000);
    uint32_t hdr = mem::AllocSystem(24);
    kernel::W32(hdr + 0, 0x000D0001);
    kernel::W32(hdr + 4, mem::PhysicalFromVirtual(data));
    kernel::W32(hdr + 8, 0);
    kernel::W32(hdr + 12, 0x00010021 | (kLinA8R8G8B8 << 8));
    kernel::W32(hdr + 16, (kW - 1) | (kH - 1) << 12 | (pitch / 64 - 1) << 24);
    kernel::W32(hdr + 20, 0);
    gpu::Surface s;
    s.header = hdr;
    s.data = kernel::R32(hdr + 4);
    s.format = kernel::R32(hdr + 12);
    s.size = kernel::R32(hdr + 16);
    return s;
}

// a vertex buffer header whose data is n bytes of contiguous memory; the data's address in data
uint32_t NewVertexBuffer(uint32_t n, uint32_t& data) {
    data = mem::AllocContiguous(n, 0, 0xFFFFFFFF, 0x1000);
    uint32_t hdr = mem::AllocSystem(16);
    kernel::W32(hdr + 0, 0x00000001);  // a vertex buffer, one reference
    kernel::W32(hdr + 4, mem::PhysicalFromVirtual(data));
    return hdr;
}

// D3DFVF_XYZRHW | D3DFVF_DIFFUSE
struct Vertex {
    float x, y, z, rhw;
    uint32_t argb;
};

// the frame's ImGui window, over the picture (GameInfo::ui)
std::atomic<int> g_frame{0};

}  // namespace

void Ui(double fps) {
    ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin("smoke", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("xbe_smoke: Vulkan frame %d, %.0f fps", g_frame.load(), fps);
    ImGui::End();
}

// frames presented through the backend: a clear changing colour and kTriangles spinning triangles drawn with
// Backend::Draw from guest memory, as a title's fixed-function draws reach it
bool Vulkan(int frames) {
    std::unique_ptr<gpu::Backend> b = gpu::CreateBackend();
    if (!b->Init(kW, kH)) {
        const char* why = b->InitError();
        LOGE("smoke: the Vulkan backend did not start: %s", why ? why : "?");
        return false;
    }
    gpu::Surface rt = NewSurface();
    uint32_t vb_data = 0;
    uint32_t vb = NewVertexBuffer(sizeof(Vertex) * 3 * kTriangles * 2, vb_data);

    gpu::State s;
    s.rt = rt;
    s.viewport = {0, 0, kW, kH, 0.0f, 1.0f};
    s.vs_handle = 0x004 | 0x040;
    s.streams[0] = {vb, sizeof(Vertex)};
    s.simple[gpu::kMColorWriteEnable >> 2] = 0x01010101;  // D3DCOLORWRITEENABLE_ALL
    // stage 0 passes the vertex colour (SELECTARG2 of DIFFUSE), stage 1 off
    auto& t0 = s.tss[0];
    t0[gpu::kTssColorOp] = 3;
    t0[gpu::kTssColorArg2] = 0;
    t0[gpu::kTssAlphaOp] = 3;
    t0[gpu::kTssAlphaArg2] = 0;
    s.tss[1][gpu::kTssColorOp] = 1;
    s.tss[1][gpu::kTssAlphaOp] = 1;

    auto start = std::chrono::steady_clock::now();
    for (int f = 0; f < frames || frames <= 0; f++) {
#ifndef _WIN32  // (the window's own check; the smoke test's window is SDL's)
        if (!app::WindowAlive()) {
            LOGR("smoke: the window closed after %d frames", f);
            break;
        }
#endif
        g_frame = f;
#ifndef _WIN32
        // --display-change: the events pushed as SDL's own would come, the window thread working the screens out
        // again while this thread reads what it keeps (each frame, as Output is asked)
        if (g_display_change) {
            app::Mode m = app::Desktop();
            if (f % 30 == 15) {
                SDL_Event e{};
                e.type = f % 60 == 15 ? SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED : SDL_EVENT_WINDOW_HDR_STATE_CHANGED;
                if (e.type == SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED) e.display.displayID = app::sdl::Display();
                SDL_PushEvent(&e);
                float white = 0, headroom = 0;
                bool hdr = app::sdl::WindowHdr(white, headroom);
                LOGR("smoke: frame %d, %s pushed, desktop %ux%u, HDR %d", f,
                     e.type == SDL_EVENT_WINDOW_HDR_STATE_CHANGED ? "HDR change" : "display change", m.width, m.height, hdr);
            }
        }
#endif
        float t = float(f) / 60.0f;
        uint32_t r = uint32_t(40 + 30 * std::sin(t)), g = uint32_t(40 + 30 * std::sin(t + 2)), bl = uint32_t(60 + 40 * std::sin(t + 4));
        b->Clear(s, 0, nullptr, gpu::kClearTarget, 0xFF000000u | r << 16 | g << 8 | bl, 1.0f, 0);
        // the vertices of this frame in the half of the buffer the last frame's draw didn't read
        Vertex* v = static_cast<Vertex*>(kernel::HostPtr(vb_data)) + (f & 1) * 3 * kTriangles;
        for (uint32_t k = 0; k < kTriangles; k++) {
            float cx = kW * (k + 1) / (kTriangles + 1.0f), cy = kH / 2.0f, rad = kH / 5.0f;
            float a = t * (1.0f + 0.5f * float(k));
            static const uint32_t kColours[3] = {0xFFFF4040, 0xFF40FF40, 0xFF4080FF};
            for (uint32_t i = 0; i < 3; i++) {
                float ai = a + float(i) * 2.0943951f;
                v[k * 3 + i] = {cx + rad * std::cos(ai), cy + rad * std::sin(ai), 0.5f, 1.0f, kColours[(i + k) % 3]};
            }
        }
        b->Draw(s, 5, (f & 1) * 3 * kTriangles, 3 * kTriangles, nullptr);  // D3DPT_TRIANGLELIST
        b->Present(rt);
        app::Pace();
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    LOGR("smoke: %d Vulkan frames in %.1f s", g_frame.load() + 1, secs);
    b.reset();
    return true;
}

// a 440 Hz tone, alternating with 660 Hz each second, through audio::StartOutput as the game's mixer is pulled
std::atomic<uint64_t> g_pulled{0};
std::chrono::steady_clock::time_point g_tone_start;

void StartTone() {
    g_tone_start = std::chrono::steady_clock::now();
    audio::StartOutput([](float* out, uint32_t n) {
        for (uint32_t i = 0; i < n; i++) {
            uint64_t k = g_pulled++;
            double hz = (k / audio::kRate) % 2 ? 660.0 : 440.0;
            float x = 0.2f * float(std::sin(2.0 * 3.14159265358979 * hz * double(k) / audio::kRate));
            out[i * 2] = out[i * 2 + 1] = x;
        }
    });
}

// the frames pulled so far against the time since the start: a little short is the queue filling first, nothing
// pulled is no output
bool ToneOk() {
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_tone_start).count();
    uint64_t pulled = g_pulled.load();
    double pct = 100.0 * double(pulled) / (double(audio::kRate) * secs);
    LOGR("smoke: audio pulled %llu frames in %.1f s (%.0f%% of real time)", (unsigned long long)pulled, secs, pct);
    return pct > 50.0;
}

// --vulkan and --audio: the game window opened on this (the main) thread, which then runs its loop; the frames are
// drawn on a thread of their own, as a game's are, and the window closed after them. The process exit code.
int Run(bool vulkan, bool sound, int frames) {
    kernel::g_base = mem::Base();  // the guest memory the renderer reads, as kernel/boot.cpp sets it for a game
#ifndef _WIN32
    if (!app::StartWindow(true)) {
        LOGE("smoke: no game window");
        return 1;
    }
#endif
    static std::atomic<int> code{0};
    std::thread([vulkan, sound, frames] {
        if (sound) StartTone();
        if (vulkan && !Vulkan(frames)) code = 1;
        if (!vulkan) std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1000, frames * 1000 / 60)));
        if (sound && !ToneOk()) code = 1;
        LOGR("smoke: %s", code ? "failed" : "done");
        logx::Flush();
        app::CloseGameWindow();
    }).detach();
#ifndef _WIN32
    app::RunWindowLoop();
#endif
    return code;
}

}  // namespace smoke
