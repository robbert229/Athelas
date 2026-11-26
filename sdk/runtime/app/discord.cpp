// Discord status ([discord] enabled, off by default): what the game is doing ("Helm's Deep", "In the menus") shown on
// the player's Discord profile, through the Discord app's own local connection (the named pipe discord-ipc-N, the
// protocol Discord's Rich Presence libraries speak). Nothing goes over the network from here, and with Discord not
// running nothing happens. Needs the game's Discord application id (GameInfo::discord_app_id, or [discord] app_id).
// Off Windows the same frames go over Discord's Unix socket, discord-ipc-N in the runtime or temporary folder.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include "app/app.h"
#include "core/log.h"

namespace discord {

namespace {

std::string Json(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += char(c);
        else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else o += char(c);
    }
    return o + "\"";
}

// the frame Discord's IPC takes: opcode and length (little-endian, as both platforms are), then the JSON
std::vector<char> Frame(uint32_t op, const std::string& json) {
    std::vector<char> b(8 + json.size());
    uint32_t len = uint32_t(json.size());
    std::memcpy(b.data(), &op, 4);
    std::memcpy(b.data() + 4, &len, 4);
    std::memcpy(b.data() + 8, json.data(), json.size());
    return b;
}

#ifdef _WIN32
unsigned long Pid() { return GetCurrentProcessId(); }

struct Pipe {
    HANDLE h = INVALID_HANDLE_VALUE;
    bool Connected() const { return h != INVALID_HANDLE_VALUE; }
    bool Open() {
        for (int i = 0; i < 10 && h == INVALID_HANDLE_VALUE; i++) {
            std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
            h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        }
        return h != INVALID_HANDLE_VALUE;
    }
    void Close() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
    bool Send(uint32_t op, const std::string& json) {
        std::vector<char> b = Frame(op, json);
        DWORD w = 0;
        return WriteFile(h, b.data(), DWORD(b.size()), &w, nullptr) && w == b.size();
    }
    // whatever Discord has answered, read and dropped so the pipe never fills; false when it closed
    bool Drain() {
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false;
            if (!avail) return true;
            std::vector<char> b(avail);
            DWORD r = 0;
            if (!ReadFile(h, b.data(), avail, &r, nullptr)) return false;
        }
    }
};
#else
unsigned long Pid() { return static_cast<unsigned long>(getpid()); }

// Discord's socket: discord-ipc-0 to 9 in $XDG_RUNTIME_DIR, $TMPDIR (and $TMP, $TEMP) or /tmp, or in the Flatpak's
// or the Snap's folder inside them
struct Pipe {
    int fd = -1;
    bool Connected() const { return fd >= 0; }
    bool Open() {
        std::vector<std::string> dirs;
        for (const char* var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"})
            if (const char* v = std::getenv(var); v && *v) dirs.emplace_back(v);
        dirs.emplace_back("/tmp");
        for (size_t d = 0; d < dirs.size() && fd < 0; d++) {
            while (dirs[d].size() > 1 && dirs[d].back() == '/') dirs[d].pop_back();
            for (const char* sub : {"/", "/app/com.discordapp.Discord/", "/snap.discord/"})
                for (int i = 0; i < 10 && fd < 0; i++) Connect(dirs[d] + sub + "discord-ipc-" + std::to_string(i));
        }
        return fd >= 0;
    }
    void Connect(const std::string& path) {
        sockaddr_un a{};
        if (path.size() >= sizeof a.sun_path) return;
        a.sun_family = AF_UNIX;
        std::memcpy(a.sun_path, path.c_str(), path.size() + 1);
#ifdef SOCK_CLOEXEC
        int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else  // macOS: no SOCK_CLOEXEC, and SIGPIPE is the socket's option
        int s = socket(AF_UNIX, SOCK_STREAM, 0);
        int on = 1;
        if (s >= 0) setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
        if (s < 0) return;
        if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
            close(s);
            return;
        }
        fd = s;
    }
    void Close() {
        if (fd >= 0) close(fd);
        fd = -1;
    }
    bool Send(uint32_t op, const std::string& json) {
        std::vector<char> b = Frame(op, json);
        for (size_t at = 0; at < b.size();) {
            // Discord gone mid-write: an error, not SIGPIPE
            ssize_t n = send(fd, b.data() + at, b.size() - at, MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return false;
            at += size_t(n);
        }
        return true;
    }
    // whatever Discord has answered, read and dropped so the socket never fills; false when it closed
    bool Drain() {
        char b[4096];
        for (;;) {
            ssize_t n = recv(fd, b, sizeof b, MSG_DONTWAIT);
            if (n > 0) continue;
            if (n == 0) return false;
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        }
    }
};
#endif

std::string AppId() {
    const std::string& s = app::Get().discord_app_id;
    if (!s.empty()) return s;
    return app::kGame.discord_app_id ? app::kGame.discord_app_id : "";
}

}  // namespace

void Start() {
    if (!app::Get().discord) return;
    std::string id = AppId();
    if (id.empty()) {
        LOGR("discord: no application id for this game, status off");
        return;
    }
    std::thread([id] {
        const int64_t started = int64_t(std::time(nullptr));
        Pipe p;
        std::string shown;
        int nonce = 0;
        auto next_try = std::chrono::steady_clock::now();
        bool told = false;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!app::Get().discord) {  // switched off in the in-game menu: the status goes, the line closes
                if (p.Connected()) {
                    p.Send(1, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(Pid()) +
                                  "},\"nonce\":\"" + std::to_string(++nonce) + "\"}");
                    p.Close();
                    shown.clear();
                }
                continue;
            }
            if (!p.Connected()) {
                if (std::chrono::steady_clock::now() < next_try) continue;
                next_try = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                if (!p.Open()) {
                    if (!told) LOGR("discord: Discord isn't running, trying again now and then");
                    told = true;
                    continue;
                }
                if (!p.Send(0, "{\"v\":1,\"client_id\":" + Json(id) + "}")) {
                    p.Close();
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                if (!p.Drain()) {
                    p.Close();
                    continue;
                }
                LOGR("discord: connected");
                shown.clear();
            }
            std::string details = app::kGame.activity ? app::kGame.activity() : std::string();
            std::string state = app::kGame.activity_state ? app::kGame.activity_state() : std::string();
            if (details.empty()) details = "In the menus";
            std::string key = details + "\n" + state;
            if (key != shown) {
                std::string act = "{\"details\":" + Json(details) + (state.empty() ? "" : ",\"state\":" + Json(state)) +
                                  ",\"timestamps\":{\"start\":" + std::to_string(started) + "}}";
                std::string msg = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(Pid()) +
                                  ",\"activity\":" + act + "},\"nonce\":\"" + std::to_string(++nonce) + "\"}";
                if (!p.Send(1, msg)) {
                    LOGR("discord: connection closed");
                    p.Close();
                    continue;
                }
                shown = key;
                LOG_FIRST(20, "discord: status \"%s\"%s%s", details.c_str(), state.empty() ? "" : " / ", state.c_str());
            }
            if (!p.Drain()) {
                LOGR("discord: connection closed");
                p.Close();
            }
        }
    }).detach();
}

}  // namespace discord
