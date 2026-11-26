// Online co-op off Windows: the small window while the two PCs connect (xdk/netplay.h), before the game's own window
// opens, as netplay_window.cpp is on Windows: what it waits for, the address to give the friend, Copy address and
// Cancel. SDL3's window and 2D renderer with Dear ImGui, in the in-game menu's look and faces (menu_look.cpp): dark
// brown, a gold frame and heading.
// It runs on a thread of its own, as the Win32 one does (the game thread waits in netplay::Prepare). That thread starts
// SDL's video for the window and stops it again when the window closes, so the game window's thread (main_sdl.cpp)
// then starts it as its own: SDL wants one thread to own the video.
#ifndef _WIN32

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "app/app.h"
#include "app/menu_look.h"
#include "core/log.h"

namespace app {

namespace {

constexpr float kWidth = 600, kPad = 24, kHeadH = 40, kButtonW = 120, kCopyW = 170, kButtonH = 34;
constexpr float kHeadSize = 26, kBodySize = 19;

std::mutex g_m;
std::condition_variable g_cv;
std::thread g_thread;
bool g_up = false;        // the window is shown (guarded by g_m)
bool g_close = false;     // the game asked it to close (connected)
bool g_failed = false;    // the window couldn't be made: the connection goes on without one
std::string g_text;       // the body text shown
std::string g_address;    // the first address:port in it, for Copy (empty: no Copy button)
std::atomic<bool> g_copied{false};  // Copy pressed: its label says so until the address changes
std::atomic<bool> g_cancel{false};

ImU32 Rgb(int r, int g, int b) { return IM_COL32(r, g, b, 255); }
// the launcher's colours (launcher/src/app.h, ui.cpp), as the Win32 window has them
const ImU32 kBack = Rgb(24, 19, 14), kText = Rgb(234, 223, 200), kGold = Rgb(206, 166, 78),
            kGoldLight = Rgb(232, 196, 112), kButton = Rgb(44, 35, 25), kButtonHot = Rgb(70, 55, 34),
            kLine = Rgb(120, 95, 50);

std::string Utf8(const wchar_t* w) {
    std::string s;
    for (; w && *w; w++) {
        uint32_t c = uint32_t(*w);
        if (c < 0x80) {
            s += char(c);
        } else if (c < 0x800) {
            s += char(0xC0 | (c >> 6)), s += char(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            s += char(0xE0 | (c >> 12)), s += char(0x80 | ((c >> 6) & 0x3F)), s += char(0x80 | (c & 0x3F));
        } else {
            s += char(0xF0 | ((c >> 18) & 0x07)), s += char(0x80 | ((c >> 12) & 0x3F));
            s += char(0x80 | ((c >> 6) & 0x3F)), s += char(0x80 | (c & 0x3F));
        }
    }
    return s;
}

// "Online co-op: waiting for ..." -> "Waiting for ..." (the heading already says Online co-op)
std::string Body(const std::string& text) {
    std::string t = text;
    const std::string prefix = "Online co-op: ";
    if (t.rfind(prefix, 0) == 0) {
        t.erase(0, prefix.size());
        if (!t.empty()) t[0] = char(std::toupper(static_cast<unsigned char>(t[0])));
    }
    return t;
}

bool Digit(char c) { return c >= '0' && c <= '9'; }

// the first "a.b.c.d:port" in the text (the address player 1 sends their friend), or empty; not the home-network one
// (its own paragraph, for a friend in the same house)
std::string FindAddress(const std::string& text) {
    const std::string t = text.substr(0, text.find("A friend on the same home network"));
    for (size_t i = 0; i < t.size(); i++) {
        if (!Digit(t[i]) || (i > 0 && (Digit(t[i - 1]) || t[i - 1] == '.'))) continue;
        size_t j = i;
        int dots = 0;
        while (j < t.size() && (Digit(t[j]) || t[j] == '.')) dots += t[j++] == '.';
        if (dots != 3 || j >= t.size() || t[j] != ':' || j + 1 >= t.size() || !Digit(t[j + 1])) continue;
        size_t k = j + 1;
        while (k < t.size() && Digit(t[k])) k++;
        return t.substr(i, k - i);
    }
    return {};
}

void Copy() {
    std::string a;
    {
        std::lock_guard<std::mutex> l(g_m);
        a = g_address;
    }
    if (a.empty()) return;
    if (SDL_SetClipboardText(a.c_str())) g_copied = true;
    else LOGW("online co-op: the address couldn't be copied (%s)", SDL_GetError());
}

// the client area's height for the text (wrapped to the width)
float ClientHeight(ImFont* body, const std::string& text) {
    const float th = text.empty() ? 0 : body->CalcTextSizeA(kBodySize, FLT_MAX, kWidth - 2 * kPad, text.c_str()).y;
    return kPad + kHeadH + 14 + std::max(th, 24.f) + 18 + kButtonH + kPad;
}

// one frame: the frame, heading, text and buttons; true when Cancel was pressed
bool Draw(const look::Fonts& f, const std::string& text, bool copy) {
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##coop", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled({0, 0}, size, kBack);
    // the frame: gold, and a fainter one inside it
    dl->AddRect({6.5f, 6.5f}, {size.x - 6.5f, size.y - 6.5f}, kGold);
    dl->AddRect({10.5f, 10.5f}, {size.x - 10.5f, size.y - 10.5f}, kLine);
    // the heading and its rule
    const float head_y = kPad - 4 + (kHeadH - kHeadSize) * 0.5f;
    dl->AddText(f.head, kHeadSize, {kPad, std::round(head_y)}, kGold, "Online Co-op");
    dl->AddLine({kPad, kPad + kHeadH + 0.5f}, {size.x - kPad, kPad + kHeadH + 0.5f}, kLine);
    // the text
    dl->AddText(f.body, kBodySize, {kPad, kPad + kHeadH + 14}, kText, text.c_str(), nullptr, size.x - 2 * kPad);
    // the buttons: the launcher's, gold edged; Copy address only when the text has one
    bool cancel = false;
    auto button = [&](const char* id, const char* label, float right, float w) {
        const ImVec2 a(right - w, size.y - kPad - kButtonH), b(right, size.y - kPad);
        ImGui::SetCursorScreenPos(a);
        const bool pressed = ImGui::InvisibleButton(id, {w, kButtonH});
        const bool hot = ImGui::IsItemHovered(), down = ImGui::IsItemActive();
        dl->AddRectFilled(a, b, down ? kLine : hot ? kButtonHot : kButton);
        dl->AddRect({a.x + 0.5f, a.y + 0.5f}, {b.x - 0.5f, b.y - 0.5f}, hot ? kGoldLight : kGold);
        const ImVec2 ts = f.body->CalcTextSizeA(kBodySize, FLT_MAX, 0, label);
        dl->AddText(f.body, kBodySize, {std::round(a.x + (w - ts.x) * 0.5f), std::round(a.y + (kButtonH - ts.y) * 0.5f)},
                    hot ? kGoldLight : kText, label);
        return pressed;
    };
    if (button("##cancel", "Cancel", size.x - kPad, kButtonW)) cancel = true;
    if (copy && button("##copy", g_copied ? "Copied" : "Copy address", size.x - kPad - kButtonW - 12, kCopyW)) Copy();
    ImGui::End();
    return cancel;
}

// the window's thread: SDL's video, the window and its loop, then all of it taken down again
void Run() {
    auto failed = [](const char* what) {
        LOGW("online co-op: no window (%s: %s); the connection goes on without one", what, SDL_GetError());
        std::lock_guard<std::mutex> l(g_m);
        g_failed = true;
        g_cv.notify_all();
    };
    // the app's name and id, as the game window has them (main_sdl.cpp's StartVideo), and SDL's signal handlers off
    std::string title = Utf8(kGame.title), id = "athelas.";
    for (const wchar_t* c = kGame.name; c && *c; c++) id += char(std::tolower(int(*c & 0x7F)));
    if (Instance()) title += " [" + std::to_string(Instance()) + "]";
    SDL_SetAppMetadata(title.c_str(), kGame.version, id.c_str());
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return failed("SDL's video");
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    ImGuiContext* ctx = nullptr;
    auto down = [&] {
        if (ctx) {
            ImGui_ImplSDLRenderer3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext(ctx);
        }
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    };
    win = SDL_CreateWindow(title.c_str(), int(kWidth), 200, SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (win) ren = SDL_CreateRenderer(win, nullptr);
    if (!win || !ren) {
        failed(win ? "SDL's renderer" : "SDL's window");
        return down();
    }
    // no v-sync: the loop paces itself, and a hidden window's present would wait for a frame that never comes (Wayland)
    SDL_SetRenderVSync(ren, 0);
    // an ImGui of its own, made and ended here: the game's (overlay.cpp) is made after this window has gone
    ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    const look::Fonts fonts = look::AddFonts(io.Fonts, nullptr);
    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
    ImGui_ImplSDLRenderer3_Init(ren);

    std::string text;
    bool copy = false;
    float shown_h = 0;
    {
        std::lock_guard<std::mutex> l(g_m);
        g_up = true;
    }
    g_cv.notify_all();
    LOGI("online co-op: window up (%s, %s)", SDL_GetCurrentVideoDriver(), SDL_GetRendererName(ren));
    bool shown = false, again = false;
    for (;;) {
        SDL_Event e;
        bool cancel = false;
        // a frame every 50 ms at most while nothing happens; at once on input, or after a change of size
        if (again ? SDL_PollEvent(&e) : SDL_WaitEventTimeout(&e, 50)) {
            do {
                ImGui_ImplSDL3_ProcessEvent(&e);
                if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED || e.type == SDL_EVENT_QUIT) cancel = true;  // its X: Cancel
                if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) cancel = true;
                if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_C && (e.key.mod & SDL_KMOD_CTRL)) Copy();  // the address too
            } while (SDL_PollEvent(&e));
        }
        again = false;
        {
            std::lock_guard<std::mutex> l(g_m);
            if (g_close) break;
            text = g_text;
            copy = !g_address.empty();
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        // as tall as the text needs (the host's has several paragraphs), centred on the screen; drawn from the next
        // frame, at that size
        const float h = ClientHeight(fonts.body, text);
        if (h != shown_h) {
            shown_h = h;
            SDL_SetWindowSize(win, int(kWidth), int(h));
            SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            ImGui::EndFrame();
            again = true;
            continue;
        }
        cancel = Draw(fonts, text, copy) || cancel;
        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 24, 19, 14, 255);
        SDL_RenderClear(ren);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
        SDL_RenderPresent(ren);
        if (!shown) {  // shown once sized and drawn, so it doesn't open blank or at the wrong size
            shown = true;
            SDL_ShowWindow(win);
            SDL_RaiseWindow(win);
        }
        if (cancel) {
            LOGR("online co-op: cancelled in the window");
            g_cancel = true;
            break;
        }
    }
    down();
    std::lock_guard<std::mutex> l(g_m);
    g_up = false;
    g_cv.notify_all();
}

}  // namespace

void NetplayStatus(const std::string& text) {
    std::unique_lock<std::mutex> l(g_m);
    if (text.empty()) {
        // closed, and its thread waited for: SDL's video is stopped before the game window starts it again
        if (g_thread.joinable()) {
            g_close = true;
            l.unlock();
            g_thread.join();
        }
        return;
    }
    if (g_cancel || g_failed) return;  // cancelled (the game is ending) or no window possible
    g_text = Body(text);
    // only player 1's waiting window has an address to send (an error's example address isn't one)
    std::string address = g_text.rfind("Waiting for player 2", 0) == 0 ? FindAddress(g_text) : std::string();
    if (address != g_address) g_copied = false;
    g_address = address;
    if (g_up) return;  // the window shows it from its next frame
    // the window may open again for a message after it closed
    if (g_thread.joinable()) {
        l.unlock();
        g_thread.join();
        l.lock();
    }
    g_close = false;
    g_thread = std::thread(Run);
    g_cv.wait(l, [] { return g_up || g_failed; });
    if (!g_up) {
        l.unlock();
        g_thread.join();
    }
}

bool NetplayCancelled() { return g_cancel.load(); }

// no window could be made: the connection goes on without one, and a message that waits for Cancel doesn't wait
bool NetplayWindowless() {
    std::lock_guard<std::mutex> l(g_m);
    return g_failed;
}

}  // namespace app

#endif  // !_WIN32
