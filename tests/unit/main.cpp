// hotd_unit (worker "tests", 2026-11-09; docs/testing.md "CTest"): the unit tests that need no game data (the source
// tree's own data\ folder only). Usage: hotd_unit [suite ...] [--list]; no suite: every test. Prints a line per test
// and "N checks, M failed"; exit 0 when nothing failed. CTest runs one suite per test (tests/CMakeLists.txt).
#include "check.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <set>

#ifndef HOTD_UNIT_SOURCE_DIR
#define HOTD_UNIT_SOURCE_DIR "."
#endif

namespace hotd::test {

namespace fs = std::filesystem;

std::string sourceDir()
{
    if (const char* e = std::getenv("HOTD_UNIT_SOURCE"); e && *e) return e;
    return HOTD_UNIT_SOURCE_DIR;
}

static fs::path& tempRoot()
{
    static fs::path p;
    return p;
}

std::string tempDir(const std::string& name)
{
    if (tempRoot().empty()) {
        std::error_code ec;
        const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
        tempRoot() = fs::temp_directory_path(ec) / ("hotd_unit_" + std::to_string(uint64_t(t) & 0xFFFFFFFFu));
    }
    const fs::path p = tempRoot() / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.string();
}

} // namespace hotd::test

int main(int argc, char** argv)
{
    using namespace hotd::test;
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_WARN);   // (the modules' SDL_Log lines: only warnings and errors)
    std::set<std::string> only;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--list") list = true;
        else only.insert(a);
    }
    std::vector<Case> all = cases();
    std::stable_sort(all.begin(), all.end(), [](const Case& a, const Case& b) { return std::string(a.suite) < b.suite; });
    if (list) {
        for (const Case& c : all) std::printf("%s.%s\n", c.suite, c.name);
        return 0;
    }
    Counts& n = counts();
    int ran = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (const Case& c : all) {
        if (!only.empty() && !only.count(c.suite) && !only.count(std::string(c.suite) + "." + c.name)) continue;
        n.current = &c;
        const int before = n.failed, checksBefore = n.checks;
        c.fn();
        ++ran;
        std::printf("%s %s.%s (%d checks)\n", n.failed == before ? "ok  " : "FAIL", c.suite, c.name, n.checks - checksBefore);
        std::fflush(stdout);
    }
    n.current = nullptr;
    if (!tempRoot().empty()) {
        std::error_code ec;
        std::filesystem::remove_all(tempRoot(), ec);
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (ran == 0) {
        std::printf("no test matched\n");
        return 2;
    }
    std::printf("%d tests, %d checks, %d failed (%.2f s)\n", ran, n.checks, n.failed, s);
    return n.failed ? 1 : 0;
}
