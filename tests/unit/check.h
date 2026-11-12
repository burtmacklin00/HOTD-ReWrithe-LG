// hotd_unit's own test harness (worker "tests", 2026-11-09; docs/testing.md "CTest"): header-only, no third-party
// library (MIT, the project's own). A test is a function registered with TEST(suite, name); CHECK / CHECK_EQ count a
// check and print each failure with its file and line; a test's failures do not stop the others. main.cpp runs the
// tests of the suites named on the command line (none: all) and ends with "N checks, M failed" (exit 1 on a failure).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <type_traits>
#include <sstream>
#include <string>
#include <vector>

namespace hotd::test {

struct Case {
    const char* suite;
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& cases()
{
    static std::vector<Case> c;
    return c;
}

struct Counts {
    int checks = 0, failed = 0;
    const Case* current = nullptr;
};

inline Counts& counts()
{
    static Counts c;
    return c;
}

struct Reg {
    Reg(const char* suite, const char* name, void (*fn)()) { cases().push_back({suite, name, fn}); }
};

inline bool check(bool ok, const std::string& what, const char* file, int line)
{
    Counts& c = counts();
    ++c.checks;
    if (!ok) {
        ++c.failed;
        std::printf("FAIL %s.%s (%s:%d): %s\n", c.current ? c.current->suite : "?", c.current ? c.current->name : "?",
                    file, line, what.c_str());
        std::fflush(stdout);
    }
    return ok;
}

template <class T>
std::string show(const T& v)
{
    std::ostringstream s;
    if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>)
        s << int(v);
    else if constexpr (std::is_same_v<T, bool>)
        s << (v ? "true" : "false");
    else if constexpr (requires { s << v; })
        s << v;
    else
        s << "(value)";
    return s.str();
}

template <class A, class B>
bool checkEq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line)
{
    const bool ok = a == b;
    return check(ok, ok ? std::string() : std::string(ea) + " == " + eb + "  (" + show(a) + " vs " + show(b) + ")", file, line);
}

inline bool checkNear(double a, double b, double tol, const char* ea, const char* eb, const char* file, int line)
{
    const bool ok = std::fabs(a - b) <= tol;
    return check(ok, ok ? std::string() : std::string(ea) + " ~= " + eb + "  (" + show(a) + " vs " + show(b) + ", tol " + show(tol) + ")",
                 file, line);
}

// the folder of the source tree (data\lang, data\mods ...): HOTD_UNIT_SOURCE (environment) or the build's own
std::string sourceDir();
// a fresh empty folder for one test's files (removed at the end of the run)
std::string tempDir(const std::string& name);

} // namespace hotd::test

#define HOTD_TEST_CAT2(a, b) a##b
#define HOTD_TEST_CAT(a, b) HOTD_TEST_CAT2(a, b)
#define TEST(suite, name)                                                                                              \
    static void HOTD_TEST_CAT(test_, HOTD_TEST_CAT(suite, HOTD_TEST_CAT(_, name)))();                                  \
    static ::hotd::test::Reg HOTD_TEST_CAT(reg_, HOTD_TEST_CAT(suite, HOTD_TEST_CAT(_, name)))(                       \
        #suite, #name, &HOTD_TEST_CAT(test_, HOTD_TEST_CAT(suite, HOTD_TEST_CAT(_, name))));                           \
    static void HOTD_TEST_CAT(test_, HOTD_TEST_CAT(suite, HOTD_TEST_CAT(_, name)))()

#define CHECK(cond) ::hotd::test::check(bool(cond), #cond, __FILE__, __LINE__)
#define CHECK_MSG(cond, msg) ::hotd::test::check(bool(cond), std::string(#cond) + ": " + (msg), __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::hotd::test::checkEq((a), (b), #a, #b, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol) ::hotd::test::checkNear(double(a), double(b), double(tol), #a, #b, __FILE__, __LINE__)
