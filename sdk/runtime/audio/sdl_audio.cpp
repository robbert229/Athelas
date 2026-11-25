// mixer output through SDL3 (not Windows; xaudio2.cpp there): XAudio2's 10 ms buffers and queue depth, into an audio
// stream on the default playback device. SDL moves the stream to a new default device by itself; a device lost with
// none to move to is opened again once there is one.
#ifndef _WIN32
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "app/app.h"
#include "audio/audio.h"
#include "core/log.h"

namespace audio {

namespace {

std::atomic<SDL_AudioDeviceID> g_device{0};
std::atomic<bool> g_lost{false};     // the device went away (SDL's event)
std::atomic<bool> g_starved{false};  // the device asked for more than was queued

// SDL's device thread, each time it takes from the stream: more needed than was there is the output running dry
void SDLCALL Taken(void*, SDL_AudioStream*, int additional, int) {
    if (additional > 0) g_starved = true;
}

// the thread that adds the event, at SDL_PumpEvents (the window's loop)
bool SDLCALL Watch(void*, SDL_Event* e) {
    if (e->type == SDL_EVENT_AUDIO_DEVICE_REMOVED && e->adevice.which == g_device) g_lost = true;
    return true;
}

// interleaved stereo float at kRate, as XAudio2's source voice; SDL converts to the device's own format and rate.
// It opens paused, and starts once the first buffers are queued.
SDL_AudioStream* Open() {
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, int(kRate)};
    SDL_AudioStream* s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Taken, nullptr);
    if (!s) return nullptr;
    g_device = SDL_GetAudioStreamDevice(s);
    g_lost = false;
    SDL_AudioSpec dev{};
    int frames = 0;
    SDL_GetAudioDeviceFormat(g_device, &dev, &frames);
    const char* name = SDL_GetAudioDeviceName(g_device);
    LOGR("audio: SDL %s, %s, %d Hz, %d channels, %d frames a period", SDL_GetCurrentAudioDriver(),
         name ? name : "default device", dev.freq, dev.channels, frames);
    return s;
}

}  // namespace

void StartOutput(std::function<void(float*, uint32_t)> render) {
    std::thread([render] {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            LOGE("audio: SDL audio unavailable (%s), no sound", SDL_GetError());
            return;
        }
        SDL_AddEventWatch(Watch, nullptr);
        SDL_AudioStream* stream = Open();
        if (!stream) LOGE("audio: no output device (%s), no sound until one is there", SDL_GetError());
        constexpr uint32_t kFrames = kRate / 100;
        constexpr uint32_t kMinQueued = 3;
        constexpr uint32_t kMaxQueued = 10;
        constexpr int kBytes = int(kFrames * 8);
        std::vector<float> b(kFrames * 2);
        uint32_t depth = kMinQueued;
        uint64_t n = 0;  // buffers put since the device was opened
        auto retry = std::chrono::steady_clock::now();
        for (;;) {
            if (g_lost && stream) {
                LOGR("audio: the output device was lost, opening the default one again");
                SDL_DestroyAudioStream(stream);
                stream = nullptr;
            }
            if (!stream) {  // none: tried again every second, the mixer standing still meanwhile
                auto now = std::chrono::steady_clock::now();
                if (now >= retry) {
                    retry = now + std::chrono::seconds(1);
                    stream = Open();
                    n = 0;
                }
                if (!stream) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
            }
            // a starved device crackles: one buffer more queued each time it runs dry, up to kMaxQueued (100 ms), as
            // XAudio2's path; a device that takes more at a time than is queued settles on what it needs
            if (g_starved.exchange(false) && n && depth < kMaxQueued) {
                depth++;
                LOGR("audio: output ran dry, queueing %u ms from now", depth * 10);
            }
            if (SDL_GetAudioStreamQueued(stream) >= int(depth) * kBytes) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            // paused (app::Held): the mixer takes no time, so the game's sounds and music wait where they were;
            // muted, they play on unheard
            if (app::Held()) std::fill(b.begin(), b.end(), 0.0f);
            else {
                render(b.data(), kFrames);
                if (app::Muted()) std::fill(b.begin(), b.end(), 0.0f);
            }
            SDL_PutAudioStreamData(stream, b.data(), kBytes);
            if (++n == depth && SDL_AudioStreamDevicePaused(stream)) {
                g_starved = false;
                SDL_ResumeAudioStreamDevice(stream);
            }
        }
    }).detach();
}

}  // namespace audio

#endif  // _WIN32
