// Text-to-speech with the system's own voice (tts.h).
#include "util/tts.h"

#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32) && defined(_MSC_VER)
#define HOTD_TTS_SAPI 1
#include <windows.h>
#include <sapi.h>
#pragma comment(lib, "ole32.lib")
#elif !defined(_WIN32) && !defined(__ANDROID__)
#define HOTD_TTS_SPAWN 1
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace hotd::util::tts {

namespace {
struct State {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::string> queue;
    std::thread worker;
    int ready = -1;            // -1 not known yet, 0 no voice, 1 a voice
    bool stop = false;
    std::string backend;
#if HOTD_TTS_SPAWN
    std::string program;       // the speaking program's path
#endif
};
State& st()
{
    static State* s = new State;   // (never freed: a detached worker may still be speaking at exit)
    return *s;
}
constexpr size_t kBacklog = 3;   // texts waiting at most (older ones dropped)

#if HOTD_TTS_SPAWN
std::string onPath(const char* name)
{
    const char* path = std::getenv("PATH");
    std::string p = path ? path : "/usr/bin:/bin:/usr/local/bin";
    size_t a = 0;
    while (a <= p.size()) {
        const size_t b = p.find(':', a);
        const std::string dir = p.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (!dir.empty()) {
            const std::string f = dir + "/" + name;
            struct stat sb {};
            if (::stat(f.c_str(), &sb) == 0 && S_ISREG(sb.st_mode) && ::access(f.c_str(), X_OK) == 0) return f;
        }
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return {};
}
#endif

void run()
{
    State& s = st();
#if HOTD_TTS_SAPI
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ISpVoice* voice = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(SpVoice), nullptr, CLSCTX_ALL, __uuidof(ISpVoice), reinterpret_cast<void**>(&voice))) && voice)
        voice->SetRate(2);   // (a little quicker than the default: words on a screen of zombies)
    {
        std::lock_guard<std::mutex> lk(s.mu);
        s.ready = voice ? 1 : 0;
        s.backend = voice ? "Windows speech (SAPI)" : "";
    }
#elif HOTD_TTS_SPAWN
    {
        std::lock_guard<std::mutex> lk(s.mu);
        for (const char* p : {"say", "spd-say", "espeak-ng", "espeak"})
            if (s.program = onPath(p); !s.program.empty()) {
                s.backend = p;
                break;
            }
        s.ready = s.program.empty() ? 0 : 1;
    }
#else
    {
        std::lock_guard<std::mutex> lk(s.mu);
        s.ready = 0;
    }
#endif
    s.cv.notify_all();
    for (;;) {
        std::string text;
        {
            std::unique_lock<std::mutex> lk(s.mu);
            s.cv.wait(lk, [&] { return s.stop || !s.queue.empty(); });
            if (s.stop || s.ready != 1) break;
            text = std::move(s.queue.front());
            s.queue.pop_front();
        }
        if (text.empty()) continue;
#if HOTD_TTS_SAPI
        const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        std::vector<wchar_t> w(size_t(n > 0 ? n : 1), L'\0');
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, w.data(), n);
        voice->Speak(w.data(), SPF_DEFAULT | SPF_IS_NOT_XML, nullptr);   // (this thread waits: one text at a time)
#elif HOTD_TTS_SPAWN
        if (text[0] == '-') text.insert(0, " ");   // (never taken for an option; no shell: argv only)
        std::vector<std::string> args = {s.backend};
        if (s.backend == "say") args.push_back("-r"), args.push_back("220");
        else if (s.backend == "spd-say") args.push_back("-w");
        else args.push_back("-s"), args.push_back("190");
        args.push_back(text);
        std::vector<char*> argv;
        for (std::string& a : args) argv.push_back(a.data());
        argv.push_back(nullptr);
        pid_t pid = 0;
        if (posix_spawn(&pid, s.program.c_str(), nullptr, nullptr, argv.data(), environ) == 0) {
            int status = 0;
            waitpid(pid, &status, 0);
        }
#endif
    }
#if HOTD_TTS_SAPI
    if (voice) voice->Release();
    if (SUCCEEDED(co)) CoUninitialize();
#endif
}

void ensure()
{
    State& s = st();
    std::unique_lock<std::mutex> lk(s.mu);
    if (!s.worker.joinable() && !s.stop) {
        s.worker = std::thread(run);
    }
    if (s.stop && !s.worker.joinable()) { s.ready = 0; return; }
    s.cv.wait(lk, [&] { return s.ready >= 0; });
}
} // namespace

bool available()
{
    ensure();
    std::lock_guard<std::mutex> lk(st().mu);
    return st().ready == 1;
}

std::string backend()
{
    ensure();
    std::lock_guard<std::mutex> lk(st().mu);
    return st().backend;
}

void speak(const std::string& text)
{
    if (!available()) return;
    State& s = st();
    {
        std::lock_guard<std::mutex> lk(s.mu);
        s.queue.push_back(text);
        while (s.queue.size() > kBacklog) s.queue.pop_front();
    }
    s.cv.notify_all();
}

void shutdown()
{
    State& s = st();
    {
        std::lock_guard<std::mutex> lk(s.mu);
        s.stop = true;
        s.queue.clear();
    }
    s.cv.notify_all();
    if (s.worker.joinable())
        s.worker.detach();   // (a word being spoken is not waited for at exit)
}

} // namespace hotd::util::tts
