// firstrun: hotd.log, the crash report and the start-up error box (util/crash_log.h)
#include "util/crash_log.h"

#include <SDL3/SDL.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <cstdint>
#include <dlfcn.h>
#include <signal.h>
#include <unistd.h>
#include <vector>
#if defined(__ANDROID__) && __ANDROID_API__ < 33
// quest: Android before API 33 has no execinfo.h: the frames from the unwinder, no symbol names (dladdr below gives
// the module + offset)
#include <unwind.h>
namespace {
struct UnwindState { void** at; int left, n; };
_Unwind_Reason_Code unwindOne(_Unwind_Context* c, void* arg)
{
    auto* s = static_cast<UnwindState*>(arg);
    if (s->left <= 0) return _URC_END_OF_STACK;
    if (const uintptr_t pc = _Unwind_GetIP(c)) *s->at++ = reinterpret_cast<void*>(pc), --s->left, ++s->n;
    return _URC_NO_REASON;
}
int backtrace(void** frames, int max)
{
    UnwindState s{frames, max, 0};
    _Unwind_Backtrace(unwindOne, &s);
    return s.n;
}
char** backtrace_symbols(void* const*, int) { return nullptr; }
} // namespace
#else
#include <execinfo.h>
#endif
#endif

namespace hotd::util {

namespace {

std::mutex gLogMutex;
std::FILE* gLog = nullptr;
std::string gLogPath, gCrashDir;
bool gCrashBox = false;
std::deque<std::string> gRecent;   // the last log lines (for the crash report)
SDL_LogOutputFunction gDefaultOut = nullptr;
void* gDefaultData = nullptr;

#ifdef _WIN32
std::wstring wide(const std::string& s)
{
    std::wstring w(size_t(MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), int(w.size()));
    return w;
}
#endif

std::FILE* openUtf8(const std::string& path, const char* mode)
{
#ifdef _WIN32
    return _wfopen(wide(path).c_str(), wide(mode).c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

// rescrash (a player's crash left no report: a crash Windows ends at once, heap corruption, a driver's own failure,
// or another module's exception filter): <settings>hotd.running while a logged run goes on ("pid started"); removed
// at a normal end (atexit) and when a report is written. Found at the next start: that run's report from its log
std::string gMarker;
bool gMarkerOwn = false;

void removeFile(const std::string& path)
{
#ifdef _WIN32
    DeleteFileW(wide(path).c_str());
#else
    std::remove(path.c_str());
#endif
}

void markerReported()
{
    if (gMarkerOwn) {
        gMarkerOwn = false;
        removeFile(gMarker);
    }
}

unsigned long thisPid()
{
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

bool pidAlive(unsigned long pid)
{
    if (pid == 0 || pid == thisPid())
        return false;
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h)
        return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    return kill(pid_t(pid), 0) == 0;
#endif
}

std::string clock()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[32];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

void SDLCALL logOut(void* data, int category, SDL_LogPriority priority, const char* message)
{
    {
        std::lock_guard<std::mutex> lock(gLogMutex);
        const std::string line = clock() + "  " + (message ? message : "");
        if (gLog) {
            std::fputs(line.c_str(), gLog);
            std::fputc('\n', gLog);
            std::fflush(gLog);   // the log survives a crash
        }
        gRecent.push_back(line);
        if (gRecent.size() > 60) gRecent.pop_front();
    }
    if (gDefaultOut) gDefaultOut(gDefaultData, category, priority, message);
}

void showBox(const std::string& title, const std::string& text)
{
#ifdef _WIN32
    MessageBoxW(nullptr, wide(text).c_str(), wide(title).c_str(), MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
#else
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), nullptr);
#endif
}

// the report: what happened, where (module + offset per frame), the last log lines; returns the file's path
std::string writeReport(const std::string& what, const std::function<std::string()>& trace)
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char name[64];
    std::snprintf(name, sizeof name, "crash-%04d%02d%02d-%02d%02d%02d.txt", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    const std::string path = gCrashDir + name;
    std::FILE* f = openUtf8(path, "wb");
    if (f) {
        // rescrash: what happened and the last log lines first (flushed), the stack after them: a stack walk that
        // fails (a broken driver's frames) still leaves a report
        std::fprintf(f, "The House of the Dead: ReWrithe crash report\n%s\n\n%s\n\n", name, what.c_str());
        std::fprintf(f, "Last log lines (the whole log: %s):\n", gLogPath.c_str());
        for (const std::string& l : gRecent) std::fprintf(f, "  %s\n", l.c_str());
        std::fflush(f);
        std::fprintf(f, "\nStack (module + offset):\n%s\n", trace ? trace().c_str() : "  (none)\n");
        std::fclose(f);
    }
    markerReported();
    std::fprintf(stderr, "CRASH: %s (report: %s)\n", what.c_str(), f ? path.c_str() : "not written");
    std::fflush(stderr);
    if (gLog) {
        std::fprintf(gLog, "%s  CRASH: %s (report: %s)\n", clock().c_str(), what.c_str(), path.c_str());
        std::fflush(gLog);
    }
    return f ? path : std::string();
}

void tellPlayer(const std::string& report)
{
    if (!gCrashBox) return;
    showBox("The House of the Dead: ReWrithe",
            "Sorry, the game stopped because of an error.\n\n" +
            (report.empty() ? std::string("No crash report could be written.")
                            : "A crash report was saved here:\n" + report) +
            "\n\nIf you report the problem, please attach that file and hotd.log from the same folder "
            "(they contain no game files). Starting the game again usually works.");
}

#ifdef _WIN32
std::string stackOf(CONTEXT ctx, HANDLE thread)
{
    std::string out;
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
    const bool sym = SymInitialize(proc, nullptr, TRUE) != FALSE;
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = ctx.Rip;
    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Rbp;
    sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Rsp;
    sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 48; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &sf, &ctx, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr) || sf.AddrPC.Offset == 0)
            break;
        const DWORD64 pc = sf.AddrPC.Offset;
        char line[512];
        HMODULE mod = nullptr;
        char modName[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(pc), &mod) && mod) {
            char full[MAX_PATH];
            if (GetModuleFileNameA(mod, full, MAX_PATH)) {
                const char* b = std::strrchr(full, '\\');
                std::snprintf(modName, sizeof modName, "%s", b ? b + 1 : full);
            }
        }
        std::snprintf(line, sizeof line, "  #%02d %s+0x%llX", i, modName,
                      (unsigned long long)(mod ? pc - reinterpret_cast<DWORD64>(mod) : pc));
        out += line;
        alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO* si = reinterpret_cast<SYMBOL_INFO*>(buf);
        si->SizeOfStruct = sizeof(SYMBOL_INFO);
        si->MaxNameLen = 255;
        DWORD64 disp = 0;
        if (sym && SymFromAddr(proc, pc, &disp, si)) {
            std::snprintf(line, sizeof line, "  %s+0x%llX", si->Name, (unsigned long long)disp);
            out += line;
        }
        out += '\n';
    }
    if (sym) SymCleanup(proc);
    return out.empty() ? "  (no stack)\n" : out;
}

struct CrashJob {
    EXCEPTION_POINTERS* e;
    HANDLE thread;
};

DWORD WINAPI crashThread(void* p)   // on its own stack: a stack overflow too
{
    const CrashJob* j = static_cast<CrashJob*>(p);
    char what[160];
    const DWORD code = j->e->ExceptionRecord->ExceptionCode;
    std::snprintf(what, sizeof what, "unhandled exception 0x%08lX%s at %p", code,
                  code == EXCEPTION_ACCESS_VIOLATION ? " (access violation)"
                  : code == EXCEPTION_STACK_OVERFLOW ? " (stack overflow)" : "",
                  j->e->ExceptionRecord->ExceptionAddress);
    tellPlayer(writeReport(what, [j] { return stackOf(*j->e->ContextRecord, j->thread); }));
    return 0;
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS* e)
{
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1) == 0) {
        CrashJob j{e, nullptr};
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &j.thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
        if (HANDLE t = CreateThread(nullptr, 256 * 1024, crashThread, &j, 0, nullptr)) {
            WaitForSingleObject(t, INFINITE);
            CloseHandle(t);
        }
    }
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}

std::string hereStack()
{
    CONTEXT ctx{};
    RtlCaptureContext(&ctx);
    return stackOf(ctx, GetCurrentThread());
}
#else
// Linux / macOS: module + offset per frame (dladdr), and the symbol when the exe exports it (backtrace_symbols)
std::string stackOf(void* const* frames, int n)
{
    std::string out;
    char** names = backtrace_symbols(frames, n);
    for (int i = 0; i < n; ++i) {
        char line[512];
        Dl_info di{};
        if (dladdr(frames[i], &di) && di.dli_fname) {
            const char* b = std::strrchr(di.dli_fname, '/');
            std::snprintf(line, sizeof line, "  #%02d %s+0x%llX", i, b ? b + 1 : di.dli_fname,
                          (unsigned long long)(reinterpret_cast<uintptr_t>(frames[i]) - reinterpret_cast<uintptr_t>(di.dli_fbase)));
        } else {
            std::snprintf(line, sizeof line, "  #%02d %p", i, frames[i]);
        }
        out += line;
        if (names && names[i]) { out += "  "; out += names[i]; }
        out += '\n';
    }
    std::free(names);
    return out.empty() ? "  (no stack)\n" : out;
}

std::string hereStack()
{
    void* frames[48];
    return stackOf(frames, backtrace(frames, 48));
}

// a fatal signal (on its own stack: a stack overflow too): the report, then the default action (the exit code / core)
void crashSignal(int sig, siginfo_t* info, void*)
{
    static volatile sig_atomic_t once = 0;
    if (!once) {
        once = 1;
        char what[160];
        std::snprintf(what, sizeof what, "signal %d%s at %p", sig,
                      sig == SIGSEGV ? " (segmentation fault)" : sig == SIGBUS ? " (bus error)"
                      : sig == SIGILL ? " (illegal instruction)" : sig == SIGFPE ? " (arithmetic error)" : "",
                      info ? info->si_addr : nullptr);
        tellPlayer(writeReport(what, [] {
            void* frames[48];
            return stackOf(frames, backtrace(frames, 48));
        }));
    }
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void installSignals()
{
    static std::vector<char> altStack(256 * 1024);
    stack_t ss{};
    ss.ss_sp = altStack.data();
    ss.ss_size = altStack.size();
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = crashSignal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (const int s : {SIGSEGV, SIGBUS, SIGILL, SIGFPE})
        sigaction(s, &sa, nullptr);
}
#endif

void abortOrTerminate(const char* what)
{
    static bool once = false;
    if (!once) {
        once = true;
        tellPlayer(writeReport(what, [] { return hereStack(); }));
    }
    std::_Exit(4);
}

} // namespace

void logToFile(const std::string& dir)
{
    namespace fs = std::filesystem;
    {
    std::lock_guard<std::mutex> lock(gLogMutex);   // (released before the marker below: it logs)
    if (gLog) return;
    gLogPath = dir + "hotd.log";
    std::error_code ec;
    const fs::path p = fs::path(gLogPath), prev = fs::path(dir + "hotd.prev.log");
    if (fs::exists(p, ec)) {
        fs::remove(prev, ec);
        fs::rename(p, prev, ec);
    }
    gLog = openUtf8(gLogPath, "wb");
    if (gLog) {
        const std::time_t t = std::time(nullptr);
        char b[64];
        std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        std::fprintf(gLog, "The House of the Dead: ReWrithe log, started %s (SDL %d.%d.%d)\n", b, SDL_MAJOR_VERSION,
                     SDL_MINOR_VERSION, SDL_MICRO_VERSION);
        std::fflush(gLog);
    }
    SDL_GetLogOutputFunction(&gDefaultOut, &gDefaultData);
    SDL_SetLogOutputFunction(logOut, nullptr);
    }
    // rescrash: the run before ended without its report (and without a normal end): a report of it now
    gMarker = dir + "hotd.running";
    unsigned long pid = 0;
    std::string started;
    bool other = false;
    if (std::FILE* m = openUtf8(gMarker, "rb")) {
        char b[256] = {};
        const size_t n = std::fread(b, 1, sizeof b - 1, m);
        std::fclose(m);
        std::istringstream in(std::string(b, n));
        in >> pid;
        std::getline(in >> std::ws, started);
        other = pidAlive(pid);   // (another copy of the game still running: its marker, left alone)
        if (!other) {
            const std::time_t t = std::time(nullptr);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char name[80];
            std::snprintf(name, sizeof name, "crash-%04d%02d%02d-%02d%02d%02d-previous-run.txt", tm.tm_year + 1900,
                          tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
            std::deque<std::string> tail;
            if (std::FILE* pl = openUtf8(dir + "hotd.prev.log", "rb")) {
                std::string line;
                for (int c; (c = std::fgetc(pl)) != EOF;)
                    if (c == '\n') {
                        tail.push_back(line), line.clear();
                        if (tail.size() > 80) tail.pop_front();
                    } else if (c != '\r')
                        line += char(c);
                if (!line.empty()) tail.push_back(line);
                std::fclose(pl);
            }
            if (std::FILE* f = openUtf8(dir + name, "wb")) {
                std::fprintf(f, "The House of the Dead: ReWrithe crash report\n%s\n\n"
                                "The previous run (started %s) stopped without closing normally and without a crash report\n"
                                "of its own: a crash Windows ends at once (heap corruption, a graphics driver failure), the\n"
                                "game ended from outside (Task Manager, a debugger), or the PC turned off.\n\n"
                                "Last lines of that run's log (the whole log: %shotd.prev.log):\n",
                             name, started.empty() ? "?" : started.c_str(), dir.c_str());
                for (const std::string& l : tail) std::fprintf(f, "  %s\n", l.c_str());
                std::fclose(f);
                SDL_Log("crash report: the previous run ended without one: %s%s", dir.c_str(), name);
            }
        }
    }
    if (!other) {
        if (std::FILE* m = openUtf8(gMarker, "wb")) {
            char b[64];
            const std::time_t t = std::time(nullptr);
            std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
            std::fprintf(m, "%lu %s\n", thisPid(), b);
            std::fclose(m);
            gMarkerOwn = true;
            std::atexit([] { markerReported(); });   // (a normal end: no report)
        }
    }
}

void crashHandlerCheck()
{
#ifdef _WIN32
    if (gCrashDir.empty())
        return;
    const LPTOP_LEVEL_EXCEPTION_FILTER prev = SetUnhandledExceptionFilter(crashFilter);
    if (prev != crashFilter) {   // (a driver / an overlay put its own: ours again, said once per module)
        static void* said = nullptr;
        if (prev != said) {
            said = reinterpret_cast<void*>(prev);
            HMODULE mod = nullptr;
            char full[MAX_PATH] = "?";
            if (prev && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           reinterpret_cast<LPCSTR>(prev), &mod) && mod)
                GetModuleFileNameA(mod, full, MAX_PATH);
            SDL_Log("crash report: another exception handler was set (%s): the game's own again", prev ? full : "none");
        }
    }
#endif
    // HOTD_CRASH_TEST=2 (testing the handler after the window and the graphics are up): an access violation here
    // =3: a crash no handler sees (as heap corruption: Windows ends the process at once): the next start's report
    static const char late = [] {
        const char* c = std::getenv("HOTD_CRASH_TEST");
        return c ? *c : '\0';
    }();
    if (late == '2') {
        volatile int* p = nullptr;
        *p = 2;
    }
    if (late == '3') {
#ifdef _WIN32
        __fastfail(7);   // (FAST_FAIL_FATAL_APP_EXIT)
#else
        std::_Exit(9);
#endif
    }
}

const std::string& logFile() { return gLogPath; }

void installCrashReport(const std::string& dir, bool messageBox)
{
    gCrashDir = dir;
    gCrashBox = messageBox;
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(crashFilter);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#else
    installSignals();
#endif
    std::signal(SIGABRT, [](int) { abortOrTerminate("abort() (an assertion or a fatal runtime error)"); });
    std::set_terminate([]() { abortOrTerminate("std::terminate (an unhandled C++ exception)"); });
    // HOTD_CRASH_TEST=1 (testing the handler): an access violation right here
    if (const char* c = std::getenv("HOTD_CRASH_TEST"); c && *c == '1') {
        volatile int* p = nullptr;
        *p = 1;
    }
}

int fatalError(const std::string& what, bool messageBox)
{
    SDL_Log("cannot start: %s", what.c_str());
    if (messageBox)
        showBox("The House of the Dead: ReWrithe", what + (gLogPath.empty() ? std::string() : "\n\nDetails: " + gLogPath));
    return 1;
}

void dropOwnConsole()
{
#ifdef _WIN32
    DWORD ids[2];
    if (GetConsoleWindow() && GetConsoleProcessList(ids, 2) == 1)
        FreeConsole();
#endif
}

} // namespace hotd::util
