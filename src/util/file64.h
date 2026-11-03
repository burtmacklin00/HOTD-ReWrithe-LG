// 64-bit file seeks on every system (docs/porting.md): MSVC's _fseeki64 / _ftelli64 on Windows, POSIX fseeko / ftello
// (64-bit off_t on 64-bit Linux / macOS) elsewhere. Files over 2 GB (captures, big zips) need them.
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#ifndef _WIN32
#include <sys/types.h>
#endif

namespace hotd {

inline int fseek64(std::FILE* f, int64_t off, int origin)
{
#ifdef _WIN32
    return _fseeki64(f, off, origin);
#else
    return fseeko(f, off_t(off), origin);
#endif
}

inline int64_t ftell64(std::FILE* f)
{
#ifdef _WIN32
    return _ftelli64(f);
#else
    return int64_t(ftello(f));
#endif
}

// a path for opening: Windows: wide, absolute, with the \\?\ prefix (user 2026-11-03: an unpacked EU disc's track in a
// deep folder was over 260 characters and could not be opened); elsewhere as it is
inline std::filesystem::path longPath(const std::string& p)
{
#ifdef _WIN32
    std::error_code ec;
    std::filesystem::path a = std::filesystem::absolute(std::filesystem::path(p), ec);   // (as the program reads paths)
    const std::wstring w = a.make_preferred().wstring();
    if (w.rfind(L"\\\\?\\", 0) == 0) return std::filesystem::path(w);
    if (w.rfind(L"\\\\", 0) == 0) return std::filesystem::path(L"\\\\?\\UNC\\" + w.substr(2));
    return std::filesystem::path(L"\\\\?\\" + w);
#else
    return std::filesystem::path(p);
#endif
}
inline std::FILE* fopenLong(const std::string& p, const char* mode = "rb")
{
#ifdef _WIN32
    const std::string m(mode);
    return _wfopen(longPath(p).c_str(), std::wstring(m.begin(), m.end()).c_str());
#else
    return std::fopen(p.c_str(), mode);
#endif
}

} // namespace hotd
