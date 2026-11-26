// the log's opening lines off Windows (system_info.cpp's twin): what the game runs on and with which settings, from
// /proc, /sys, /etc/os-release and uname
#ifndef _WIN32
#include <sched.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "image_symbols.h"

namespace app {

namespace {

namespace fs = std::filesystem;

std::string Trim(std::string s) {
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    return s;
}

std::string ReadLine(const fs::path& path) {
    std::ifstream f(path);
    std::string line;
    std::getline(f, line);
    return Trim(line);
}

std::string Env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

// "key: value" or "key=value" lines (/proc/cpuinfo, /proc/meminfo, os-release): each value of key, in order
std::vector<std::string> Values(const fs::path& path, const std::string& key, char sep) {
    std::vector<std::string> out;
    std::ifstream f(path);
    for (std::string line; std::getline(f, line);) {
        size_t at = line.find(sep);
        if (at == std::string::npos || Trim(line.substr(0, at)) != key) continue;
        std::string v = Trim(line.substr(at + 1));
        if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()) v = v.substr(1, v.size() - 2);
        out.push_back(v);
    }
    return out;
}

std::string OsVersion() {
    std::string name;
    for (const char* f : {"/etc/os-release", "/usr/lib/os-release"}) {
        auto v = Values(f, "PRETTY_NAME", '=');
        if (!v.empty()) {
            name = v[0];
            break;
        }
    }
    if (name.empty()) name = "unknown";
    utsname u{};
    if (uname(&u) == 0) name += std::string(" (") + u.sysname + " " + u.release + ")";
    // a container's os-release is the container's (distrobox, toolbox, Flatpak); an AppImage's isn't
    std::error_code ec;
    if (fs::exists("/.flatpak-info", ec)) name += ", in Flatpak";
    else if (fs::exists("/run/.containerenv", ec) || fs::exists("/.dockerenv", ec)) name += ", in a container";
    // an AppImage's runtime sets APPDIR, which programs it starts inherit: this one must be inside it
    std::string appdir = Env("APPDIR");
    if (!appdir.empty() && ExeDir().string().compare(0, appdir.size(), appdir) == 0) name += ", from an AppImage";
    return name;
}

// the desktop and its display server, and gamescope or a Steam Deck (as main_sdl.cpp's Gamescope tells them)
std::string Session() {
    std::string desktop = Env("XDG_CURRENT_DESKTOP");
    std::replace(desktop.begin(), desktop.end(), ':', '/');
    std::string type = Env("XDG_SESSION_TYPE");
    if (type == "wayland" || (type.empty() && !Env("WAYLAND_DISPLAY").empty())) type = "Wayland";
    else if (type == "x11" || (type.empty() && !Env("DISPLAY").empty())) type = "X11";
    else if (type.empty() || type == "tty") type = "no display server";
    std::string s = desktop.empty() ? type : desktop + " on " + type;
    // an SDL_VIDEO_DRIVER the player set chooses which one the game uses
    if (std::string d = Env("SDL_VIDEO_DRIVER"); !d.empty()) s += ", SDL_VIDEO_DRIVER=" + d;
    if (!Env("GAMESCOPE_WAYLAND_DISPLAY").empty() && desktop != "gamescope") s += ", in gamescope";
    if (Env("SteamDeck") == "1") s += ", Steam Deck";
    else if (!Env("SteamGameId").empty() || !Env("SteamAppId").empty()) s += ", started by Steam";
    return s;
}

// the processor's name and its cores and threads: /proc/cpuinfo ("model name" on x86, often nothing on ARM)
std::string CpuName(unsigned& cores, unsigned& threads) {
    std::ifstream f("/proc/cpuinfo");
    std::string name, physical, core;
    std::set<std::pair<std::string, std::string>> seen;
    threads = 0;
    for (std::string line; std::getline(f, line);) {
        size_t at = line.find(':');
        std::string key = Trim(line.substr(0, at)), value = at == std::string::npos ? "" : Trim(line.substr(at + 1));
        if (key == "processor") {
            threads++;
            physical.clear();
        } else if (key == "physical id") {
            physical = value;
        } else if (key == "core id") {
            seen.emplace(physical, value);
        } else if (name.empty() && (key == "model name" || key == "Model" || key == "Hardware")) {
            name = value;
        }
    }
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    if (online > 0) threads = unsigned(online);
    cores = seen.empty() ? threads : unsigned(seen.size());
    return name.empty() ? "unknown" : name;
}

double RamGb() {
    // "MemTotal:  32768000 kB"
    auto v = Values("/proc/meminfo", "MemTotal", ':');
    return v.empty() ? 0.0 : std::strtod(v[0].c_str(), nullptr) / 1048576.0;
}

// a PCI device's name from the system's pci.ids (hwdata), "10de:2208" without one
std::string PciName(const std::string& vendor, const std::string& device) {
    for (const char* file : {"/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids", "/usr/share/pci.ids"}) {
        std::ifstream f(file);
        if (!f) continue;
        std::string vendor_name;
        for (std::string line; std::getline(f, line);) {
            if (line.empty() || line[0] == '#') continue;
            if (line[0] != '\t') {
                if (!vendor_name.empty()) break;  // past the vendor's devices
                if (line.compare(0, 4, vendor) == 0) vendor_name = Trim(line.substr(4));
            } else if (!vendor_name.empty() && line.size() > 6 && line[1] != '\t' && line.compare(1, 4, device) == 0) {
                // "GA102 [GeForce RTX 3080 Ti]": the marketing name in the brackets, the chip's without them
                std::string d = Trim(line.substr(5));
                size_t open = d.find('['), close = d.rfind(']');
                if (open != std::string::npos && close > open) d = d.substr(open + 1, close - open - 1);
                std::string short_vendor = vendor_name.substr(0, vendor_name.find_first_of(" ,"));
                if (vendor == "1002") short_vendor = "AMD";
                return short_vendor + " " + d;
            }
        }
        if (!vendor_name.empty()) return vendor_name + " " + vendor + ":" + device;
        break;
    }
    return vendor + ":" + device;
}

// one line per graphics card: the DRM cards in /sys, named by NVIDIA's driver or pci.ids, with their kernel driver
std::vector<std::string> Gpus() {
    std::vector<std::pair<std::string, std::string>> cards;  // PCI address, line
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/sys/class/drm", ec)) {
        std::string n = e.path().filename().string();
        if (n.compare(0, 4, "card") != 0 || n.size() == 4 || n.find_first_not_of("0123456789", 4) != std::string::npos)
            continue;
        fs::path dev = e.path() / "device";
        std::string addr = fs::canonical(dev, ec).filename().string();
        if (ec || std::any_of(cards.begin(), cards.end(), [&](auto& c) { return c.first == addr; })) continue;
        std::string vendor = ReadLine(dev / "vendor"), device = ReadLine(dev / "device");
        if (vendor.size() < 6 || device.size() < 6) continue;  // "0x10de"
        vendor = vendor.substr(2), device = device.substr(2);
        std::string name;
        auto model = Values(fs::path("/proc/driver/nvidia/gpus") / addr / "information", "Model", ':');
        if (!model.empty()) name = model[0];
        if (name.empty()) name = PciName(vendor, device);
        std::string driver = fs::read_symlink(dev / "driver", ec).filename().string();
        if (!driver.empty()) name += " (" + driver + ")";
        if (ReadLine(dev / "boot_vga") == "1") name += " (primary)";
        cards.emplace_back(addr, name);
    }
    std::sort(cards.begin(), cards.end());
    std::vector<std::string> out;
    for (auto& c : cards) out.push_back(c.second);
    return out;
}

// a core's speed for one_core: its capacity where the kernel knows one (ARM and hybrid x86), else its top clock
long CoreSpeed(int cpu) {
    fs::path dir = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
    std::string capacity = ReadLine(dir / "cpu_capacity");
    if (!capacity.empty()) return std::atol(capacity.c_str()) << 24;
    return std::atol(ReadLine(dir / "cpufreq" / "cpuinfo_max_freq").c_str());
}

}  // namespace

uint64_t OneCoreMask() {
    // the first of the fastest cores this process may run on: a performance core where they differ
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof set, &set) != 0) return 1;
    int best = -1;
    long fastest = -1;
    for (int i = 0; i < 64; i++) {
        if (!CPU_ISSET(i, &set)) continue;
        long speed = CoreSpeed(i);
        if (speed > fastest) fastest = speed, best = i;
    }
    return best < 0 ? 1 : 1ull << best;
}

void LogSystem() {
    std::time_t now = std::time(nullptr);
    char date[32] = {};
    std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    LOGR("%ls (%s), %s", kGame.title, IMAGE_XBE, date);
    LOGR("Path: %s", ExeDir().string().c_str());
    LOGR("OS: %s", OsVersion().c_str());
    LOGR("Session: %s", Session().c_str());
    unsigned cores = 0, threads = 0;
    std::string cpu = CpuName(cores, threads);
    LOGR("CPU: %s (%u cores, %u threads)", cpu.c_str(), cores, threads);
#if defined(__aarch64__)
    LOGR("Build: ARM64");
#else
    LOGR("Build: x64");
#endif
    LOGR("RAM: %.1f GB", RamGb());
    auto gpus = Gpus();
    for (size_t i = 0; i < gpus.size(); i++) LOGR("GPU %zu: %s", i + 1, gpus[i].c_str());
    Mode desk = Desktop();
    LOGR("Desktop: %ux%u", desk.width, desk.height);
    LogSettings();
}

}  // namespace app

#endif  // !_WIN32
