// Crash logs off Windows (main.cpp writes a minidump there): SIGSEGV, SIGBUS, SIGILL, SIGFPE and SIGABRT, on a stack
// of their own, write CrashDumps/<name>.<date>_<time>.txt (UTC) beside the exe, or beside the log when --log puts it
// elsewhere: the signal and the address, the guest thread's registers, the host's call stack, and the log's last
// lines. That much is written with async-signal-safe calls only. Then, as the Windows filter does, the log gets the
// guest's stack and the game's note (g_crash_note): not safe (a crash inside the log's own lock would hang there), so
// an alarm ends the process if it takes more than a few seconds. The signal then goes on to its default action.
#ifndef _WIN32
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include "app/app.h"
#include "app/sdl_window.h"
#include "core/log.h"
#include "kernel/kernel.h"

namespace app {

namespace {

constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
char g_dir[1024];   // the CrashDumps folder, worked out at start (no allocation in the handler)
char g_name[64];    // GameInfo::name in ASCII
std::atomic<bool> g_crashing{false};

// writing without the C library's buffers or locale
struct Out {
    int fd;
    void Str(const char* s) const {
        if (fd >= 0 && s) (void)!write(fd, s, std::strlen(s));
    }
    void Hex(uint64_t v, int digits = 0) const {
        char b[17];
        int n = 0;
        do {
            b[16 - n++] = "0123456789ABCDEF"[v & 15];
            v >>= 4;
        } while ((v || n < digits) && n < 16);
        if (fd >= 0) (void)!write(fd, b + 17 - n, size_t(n));
    }
    void Dec(uint64_t v) const {
        char b[20];
        int n = 0;
        do {
            b[19 - n++] = char('0' + v % 10);
            v /= 10;
        } while (v && n < 20);
        if (fd >= 0) (void)!write(fd, b + 20 - n, size_t(n));
    }
};

const char* SignalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (access violation)";
        case SIGBUS: return "SIGBUS (bad memory access)";
        case SIGILL: return "SIGILL (illegal instruction)";
        case SIGFPE: return "SIGFPE (arithmetic error)";
        case SIGABRT: return "SIGABRT (abort)";
        default: return "signal";
    }
}

// CrashDumps/<name>.YYYY.MM.DD_HH.MM.SS.txt, the time in UTC (localtime_r isn't async-signal-safe)
void FileName(char* out, size_t size) {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    int64_t secs = ts.tv_sec, days = secs / 86400, rem = secs % 86400;
    // days since 1970 to a civil date (Howard Hinnant's algorithm)
    int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    int64_t y = yoe + era * 400 + (m <= 2);
    int v[6] = {int(y), int(m), int(d), int(rem / 3600), int(rem / 60 % 60), int(rem % 60)};
    size_t n = 0;
    auto put = [&](const char* s) {
        while (*s && n + 1 < size) out[n++] = *s++;
    };
    put(g_dir);
    put("/");
    put(g_name);
    for (int i = 0; i < 6; i++) {
        put(i == 0 ? "." : i == 3 ? "_" : ".");
        char b[5];
        int w = i == 0 ? 4 : 2, x = v[i];
        for (int k = w - 1; k >= 0; k--, x /= 10) b[k] = char('0' + x % 10);
        b[w] = 0;
        put(b);
    }
    put(".txt");
    out[n] = 0;
}

// the last part of the log file (it holds what the game did up to the crash)
void LogTail(const Out& o, const char* path) {
    int fd = path && *path ? open(path, O_RDONLY | O_CLOEXEC) : -1;
    if (fd < 0) return;
    off_t end = lseek(fd, 0, SEEK_END), from = end > 16384 ? end - 16384 : 0;
    lseek(fd, from, SEEK_SET);
    char b[4096];
    bool first = from > 0;  // a partial first line is left out
    for (ssize_t n; (n = read(fd, b, sizeof b)) > 0;) {
        char* p = b;
        if (first) {
            char* nl = static_cast<char*>(std::memchr(b, '\n', size_t(n)));
            if (!nl) continue;
            first = false;
            p = nl + 1;
        }
        (void)!write(o.fd, p, size_t(b + n - p));
    }
    close(fd);
}

void Handler(int sig, siginfo_t* si, void* uc_v) {
    if (g_crashing.exchange(true)) {
        // a crash in the crash handler, or on another thread meanwhile: the default action at once
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }
    alarm(5);  // SIGALRM's default action ends the process if what follows hangs
    char path[1200];
    FileName(path, sizeof path);
    mkdir(g_dir, 0755);
    Out f{open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644)};
    Out e{2};
    auto* uc = static_cast<ucontext_t*>(uc_v);
    uint64_t pc = 0;
#if defined(__x86_64__)
    if (uc) pc = uint64_t(uc->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
    if (uc) pc = uint64_t(uc->uc_mcontext.pc);
#endif
    uint64_t addr = uint64_t(reinterpret_cast<uintptr_t>(si ? si->si_addr : nullptr));
    uint64_t base = uint64_t(reinterpret_cast<uintptr_t>(kernel::g_base));
    for (const Out& o : {f, e}) {
        o.Str(g_name);
        o.Str(" crashed: ");
        o.Str(SignalName(sig));
        int code = si ? si->si_code : 0;
        o.Str(", code ");
        if (code < 0) o.Str("-");
        o.Dec(uint64_t(code < 0 ? -int64_t(code) : code));
        // the address a fault was at (a signal sent by kill or abort has none)
        if (code > 0 && sig != SIGABRT) {
            o.Str(", address 0x");
            o.Hex(addr);
            if (base && addr >= base && addr - base < (1ull << 32)) {
                o.Str(" (guest ");
                o.Hex(addr - base, 8);
                o.Str(")");
            }
        }
        o.Str(", host pc 0x");
        o.Hex(pc);
        Dl_info info{};
        if (pc && dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname) {
            o.Str(" (");
            o.Str(info.dli_fname);
            o.Str("+0x");
            o.Hex(pc - uint64_t(reinterpret_cast<uintptr_t>(info.dli_fbase)));
            o.Str(")");
        }
        o.Str("\n");
    }
    if (f.fd >= 0) {
        e.Str("crash log: ");
        e.Str(path);
        e.Str("\n");
    }
    // the guest thread's registers (the recompiled code keeps no EIP: its return addresses are on the guest stack,
    // which the log gets below) and its last indirect call
    kernel::Thread* t = kernel::Current();
    if (t && t->ctx) {
        const X86Context& c = *t->ctx;
        f.Str("guest thread ");
        f.Hex(t->id);
        f.Str(": eax ");
        f.Hex(c.eax, 8);
        f.Str(" ecx ");
        f.Hex(c.ecx, 8);
        f.Str(" edx ");
        f.Hex(c.edx, 8);
        f.Str(" ebx ");
        f.Hex(c.ebx, 8);
        f.Str(" esp ");
        f.Hex(c.esp, 8);
        f.Str(" ebp ");
        f.Hex(c.ebp, 8);
        f.Str(" esi ");
        f.Hex(c.esi, 8);
        f.Str(" edi ");
        f.Hex(c.edi, 8);
        f.Str(", last indirect call ");
        f.Hex(c.last_icall, 8);
        f.Str("\n");
    } else {
        f.Str("not on a guest thread\n");
    }
    // the host's stack: the recompiled functions are named by guest address (sub_XXXXXXXX); without the symbols
    // exported, addr2line -e <exe> <offset> names them
    f.Str("host stack:\n");
    void* frames[64];
    int n = backtrace(frames, 64);
    if (f.fd >= 0) backtrace_symbols_fd(frames, n, f.fd);
    // now the unsafe part, as the Windows filter: the guest stack and the game's note into the log
    if (t && t->ctx) {
        LOGE("signal %d at %p on guest thread %X", sig, si ? si->si_addr : nullptr, t->id);
        kernel::LogGuestStack(*t->ctx, 256);
        if (g_crash_note) g_crash_note(t->id);
    } else {
        LOGE("signal %d at %p", sig, si ? si->si_addr : nullptr);
    }
    logx::Flush();
    f.Str("\nlast log lines:\n");
    LogTail(f, logx::Path().c_str());
    if (f.fd >= 0) close(f.fd);
    // on to the default action: a fault comes back when the handler returns, an abort is raised again
    signal(sig, SIG_DFL);
    if (sig == SIGABRT || !si || si->si_code <= 0) raise(sig);
}

// a stack for the handler on the calling thread (a stack overflow leaves none to run it on)
void AltStack() {
    constexpr size_t kSize = 256 * 1024;
    void* p = mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return;
    stack_t ss{};
    ss.ss_sp = p;
    ss.ss_size = kSize;
    if (sigaltstack(&ss, nullptr) != 0) munmap(p, kSize);
}

}  // namespace

namespace sdl {

void InstallCrashHandler() {
    std::string dir = (Args().log.empty() ? ExeDir() / "CrashDumps" : LogFolder() / "CrashDumps").string();
    std::strncpy(g_dir, dir.c_str(), sizeof g_dir - 1);
    size_t n = 0;
    for (const wchar_t* w = kGame.name; w && *w && n + 1 < sizeof g_name; w++) g_name[n++] = *w < 0x80 ? char(*w) : '_';
    g_name[n] = 0;
    // backtrace loads libgcc the first time: done now, not in the handler
    void* warm[4];
    backtrace(warm, 4);
    AltStack();
    kernel::g_thread_started = AltStack;  // each guest thread its own
    struct sigaction sa{};
    sa.sa_sigaction = Handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : kSignals) sigaction(sig, &sa, nullptr);
}

}  // namespace sdl

}  // namespace app
#endif
