// TEMPORARY (Linux M1, notes/mac-linux-plan.md): stand ins, off Windows, for what the Win32-only sources left out of
// that build provided (sdk/cmake/XbeRecomp.cmake, XBE_WIN32_ONLY_SOURCES), so the runtime links. Each workstream
// deletes its stand ins as the real code lands; once nothing is left the file goes too. Nothing here is final.
//   window/app layer (M1-B): splash.cpp, netplay_window.cpp (M2), system_info_posix.cpp, handover_posix.cpp; the
//                            window, keyboard, frame cap and crash log are main_sdl.cpp, keyboard(_sdl).cpp,
//                            screens_sdl.cpp and crash_posix.cpp
//   renderer/audio (M1-C):   none left (gpu/vk.cpp's SDL surface, gpu/backend.cpp and audio/sdl_audio.cpp landed)
#ifndef _WIN32

#include <string>

#include "app/app.h"
#include "core/log.h"

// ---- window/app layer (M1-B), until M2 ----

namespace app {

// splash.cpp: replaced by an SDL splash (M2); without it the game window opens at once
bool OpenSplash() { return false; }
void SplashFirstFrame() {}
void CloseSplash() {}

// netplay_window.cpp: replaced by an SDL status window (M2). Without it a player couldn't cancel the wait for the
// partner, so online co-op says it isn't there yet and the run ends (before the game window opens)
void NetplayStatus(const std::string& text) {
    if (text.empty()) return;
    LOGR("online co-op: %s", text.c_str());
    Fail(1, L"Online co-op isn't in the Linux build yet. Turn it off on Athelas' Network tab (or set online_coop = "
            L"\"off\" under [network] in the settings file) to play.");
}
bool NetplayCancelled() { return false; }
bool NetplayWindowless() { return true; }

}  // namespace app

#endif  // !_WIN32
