// hotd_launch (user 2026-10-31: "build their ReWrithe exe in that same folder like an actual game"): a tiny program
// with no DLLs of its own. The setup copies it next to the package's bin\ as "The House of the Dead ReWrithe.exe" and
// gives it the icon of the player's THOTD.EXE (util/game_exe.h); it starts bin\hotd.exe with the same arguments.
//
// setup2 (v1.1.0, the auto-updater, util/update_check.h): run as <install>\settings\update\hotd_update.exe
//   --update <zip> --root <install> --pid <hotd.exe's process> [--args <arguments for the restart>]
// it waits for that hotd.exe to exit, unpacks the release zip over the install (never settings\, screenshots\ or the
// player's data\arcade, pc, music, steam; never a path outside the install), adds the new files to hotd_install.txt
// (the uninstaller's list) and starts bin\hotd.exe again. A log: settings\update\update.log. (HOTD_UPDATE_URL set, a
// test: no message box on a failure, the log only.)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include "assets/archive.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

std::wstring wide(const std::string& s)
{
    if (s.empty()) return {};
    std::wstring w(size_t(MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), int(w.size()));
    return w;
}
std::string narrow(const std::wstring& w, UINT cp = CP_UTF8)
{
    if (w.empty()) return {};
    std::string s(size_t(WideCharToMultiByte(cp, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr)), '\0');
    WideCharToMultiByte(cp, 0, w.data(), int(w.size()), s.data(), int(s.size()), nullptr, nullptr);
    return s;
}
std::string lower(std::string s)
{
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

std::wstring gLog;
void logLine(const std::string& s)
{
    if (gLog.empty()) return;
    if (FILE* f = _wfopen(gLog.c_str(), L"ab")) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        std::fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, s.c_str());
        std::fclose(f);
    }
}

int fail(const std::string& why)
{
    logLine("FAILED: " + why);
    const char* test = std::getenv("HOTD_UPDATE_URL");
    if (!test || !*test)
        MessageBoxW(nullptr, wide("The update could not be installed:\n" + why + "\n\nIf the game does not start any more, "
                                  "download the new version from the GitHub page and unzip it over the game's folder.").c_str(),
                    L"The House of the Dead: ReWrithe - Update", MB_ICONERROR);
    return 1;
}

bool makeDirs(const std::wstring& dir)
{
    if (dir.empty()) return false;
    const DWORD a = GetFileAttributesW(dir.c_str());
    if (a != INVALID_FILE_ATTRIBUTES) return (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const size_t k = dir.find_last_of(L"\\/");
    if (k != std::wstring::npos && k > 2) makeDirs(dir.substr(0, k));
    return CreateDirectoryW(dir.c_str(), nullptr) != FALSE;
}

// the player's own things in an install: never written by an update
bool kept(const std::string& rel)
{
    const std::string r = lower(rel);
    for (const char* p : {"settings/", "screenshots/", "data/arcade/", "data/pc/", "data/music/", "data/steam/"})
        if (r.rfind(p, 0) == 0) return true;
    return r == "hotd_install.txt" || r == "data/hotd.zip";
}

// a zip entry's name as a safe relative path ("" when it is not: absolute, a drive, ..)
std::string safeRel(std::string n)
{
    std::replace(n.begin(), n.end(), '\\', '/');
    if (n.empty() || n[0] == '/' || n.find(':') != std::string::npos) return {};
    for (size_t at = 0; at <= n.size();) {
        const size_t e = std::min(n.find('/', at), n.size());
        const std::string part = n.substr(at, e - at);
        if (part == ".." || part == ".") return {};
        at = e + 1;
    }
    return n;
}

int update(const std::wstring& zipW, const std::wstring& rootW, DWORD pid, const std::wstring& args)
{
    gLog = rootW + L"\\settings\\update\\update.log";
    logLine("update from " + narrow(zipW) + " into " + narrow(rootW));
    if (GetFileAttributesW((rootW + L"\\hotd_package.txt").c_str()) == INVALID_FILE_ATTRIBUTES)
        return fail(narrow(rootW) + " is not the game's folder (no hotd_package.txt).");
    if (HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid)) {   // the game closes itself after the hand-over
        const DWORD w = WaitForSingleObject(p, 60000);
        CloseHandle(p);
        if (w != WAIT_OBJECT_0) return fail("the game did not close within a minute.");
    }
    Sleep(300);
    std::vector<std::string> written;
    {
        hotd::ZipFile z;
        std::string err;
        if (!z.open(narrow(zipW, CP_ACP), err)) return fail("cannot read the downloaded zip: " + err);
        // the release zip holds one folder ("HOTD ReWrithe/..."): its contents go into the install
        std::string top;
        for (const auto& e : z.entries()) {
            const std::string n = lower(e.name);
            const size_t k = n.find('/');
            if (k != std::string::npos && n.substr(k + 1) == "hotd_package.txt") top = e.name.substr(0, k + 1);
        }
        bool hasExe = false;
        for (const auto& e : z.entries()) hasExe |= lower(e.name) == lower(top) + "bin/hotd.exe";
        if (!hasExe) return fail("the zip is not a ReWrithe package (no bin\\hotd.exe).");
        for (const auto& e : z.entries()) {
            if (e.name.empty() || e.name.back() == '/' || e.name.compare(0, top.size(), top) != 0) continue;
            const std::string rel = safeRel(e.name.substr(top.size()));
            if (rel.empty() || kept(rel)) continue;
            std::vector<uint8_t> d;
            if (!z.read(e, d, err)) return fail("the zip is damaged at " + rel + ": " + err);
            std::wstring dst = rootW + L"\\" + wide(rel);
            std::replace(dst.begin(), dst.end(), L'/', L'\\');
            makeDirs(dst.substr(0, dst.find_last_of(L'\\')));
            const std::wstring tmp = dst + L".new";
            FILE* f = _wfopen(tmp.c_str(), L"wb");
            if (!f) return fail("cannot write " + narrow(tmp));
            const bool ok = d.empty() || std::fwrite(d.data(), 1, d.size(), f) == d.size();
            std::fclose(f);
            if (!ok) { DeleteFileW(tmp.c_str()); return fail("cannot write " + narrow(dst) + " (disk full?)"); }
            bool moved = false;
            for (int t = 0; t < 20 && !moved; ++t) {   // (a file still held for a moment: tried again for 4 s)
                moved = MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
                if (!moved) Sleep(200);
            }
            if (!moved) { DeleteFileW(tmp.c_str()); return fail("cannot replace " + narrow(dst) + " (is the game still open?)"); }
            written.push_back(rel);
        }
    }
    logLine("wrote " + std::to_string(written.size()) + " files");
    // an installed copy: the new files go on the uninstaller's list
    const std::wstring manifest = rootW + L"\\hotd_install.txt";
    if (GetFileAttributesW(manifest.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::set<std::string> have;
        {
            std::ifstream in(manifest);
            for (std::string l; std::getline(in, l);) have.insert(lower(l));
        }
        std::ofstream out(manifest, std::ios::app);
        for (const std::string& r : written)
            if (!have.count(lower("file=" + r))) out << "file=" << r << "\n";
    }
    DeleteFileW(zipW.c_str());
    // the game again
    const std::wstring exe = rootW + L"\\bin\\hotd.exe";
    std::wstring cmd = L"\"" + exe + L"\"" + (args.empty() ? L"" : L" " + args);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, (rootW + L"\\bin").c_str(), &si, &pi))
        return fail("the update is installed, but the game did not start: start it again yourself.");
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    logLine("done, the game started again");
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // the arguments after the program's own name
    const wchar_t* cl = GetCommandLineW();
    if (*cl == L'"') {
        ++cl;
        while (*cl && *cl != L'"') ++cl;
        if (*cl) ++cl;
    } else {
        while (*cl && *cl != L' ' && *cl != L'\t') ++cl;
    }
    {   // the updater: --update <zip> --root <root> --pid <n> [--args <rest>]
        const std::wstring rest = cl;
        const size_t a = rest.find(L" --args ");
        const std::wstring restart = a == std::wstring::npos ? std::wstring() : rest.substr(a + 8);
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc >= 2 && std::wstring(argv[1]) == L"--update") {
            std::wstring zip, root;
            DWORD pid = 0;
            for (int i = 1; i + 1 < argc; ++i) {
                const std::wstring k = argv[i];
                if (k == L"--args") break;
                if (k == L"--update") zip = argv[++i];
                else if (k == L"--root") root = argv[++i];
                else if (k == L"--pid") pid = DWORD(_wtoi(argv[++i]));
            }
            LocalFree(argv);
            if (zip.empty() || root.empty()) return fail("--update needs a zip and --root");
            return update(zip, root, pid, restart);
        }
        if (argv) LocalFree(argv);
    }
    std::wstring path(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    path.resize(n);
    const std::wstring dir = path.substr(0, path.find_last_of(L"\\/"));
    const std::wstring exe = dir + L"\\bin\\hotd.exe";
    // vrship (user 2026-11-09): this same stub copied as "The House of the Dead ReWrithe VR.exe" (util/game_exe.h
    // kGameVrExeName) starts the game in a headset: its own name ending in " VR" adds --vr openxr (the OpenGL renderer
    // comes with it: main.cpp), unless the arguments name a --vr backend already
    std::wstring self = path.substr(path.find_last_of(L"\\/") + 1);
    for (wchar_t& c : self) c = wchar_t(towlower(c));
    bool vr = self.size() > 7 && self.compare(self.size() - 7, 7, L" vr.exe") == 0;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i < argc; ++i)
            if (std::wstring(argv[i]) == L"--vr") vr = false;
        if (argv) LocalFree(argv);
    }
    std::wstring cmd = L"\"" + exe + L"\"" + (vr ? L" --vr openxr" : L"") + cl;
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        MessageBoxW(nullptr, L"bin\\hotd.exe was not found next to this program. Unzip the whole package again.",
                    L"The House of the Dead: ReWrithe", MB_ICONERROR);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
