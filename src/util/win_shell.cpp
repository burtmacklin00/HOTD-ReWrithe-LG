#include "util/win_shell.h"

#include <filesystem>
#include <fstream>

#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#else
#include "assets/pc_files.h"
#include "util/pe_icon.h"
#include "util/png.h"

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace winshell {

#ifndef _WIN32

namespace {

namespace fs = std::filesystem;

std::string home()
{
    const char* h = std::getenv("HOME");
    return h && *h ? h : ".";
}
// $XDG_DATA_HOME, else ~/.local/share (the freedesktop base directories)
fs::path dataHome()
{
    const char* d = std::getenv("XDG_DATA_HOME");
    return d && *d == '/' ? fs::path(d) : fs::path(home()) / ".local" / "share";
}
// the desktop folder: $XDG_DESKTOP_DIR (when exported), else ~/Desktop
fs::path desktopDir()
{
    const char* d = std::getenv("XDG_DESKTOP_DIR");
    return d && *d == '/' ? fs::path(d) : fs::path(home()) / "Desktop";
}
// a desktop entry's Exec argument: quoted, with " ` $ \ escaped (the Desktop Entry Specification)
std::string execQuote(const std::string& s)
{
    std::string r = "\"";
    for (const char c : s) {
        if (c == '"' || c == '`' || c == '$' || c == '\\') r += '\\';
        r += c;
    }
    return r + "\"";
}

}  // namespace

// Linux / other freedesktop systems: a .desktop file (the Desktop and, with startMenu, the applications menu in
// ~/.local/share/applications). Its icon: THOTD.EXE's icon as a PNG in ~/.local/share/icons (made on the player's own
// machine from their own file, like the Windows shortcut's icon; never shipped). macOS: not supported yet.
ShortcutResult createShortcut(const std::string& folder, bool startMenu, const std::string& pcDir, bool overwrite,
                              std::string& msg) {
#ifdef __APPLE__
    (void)folder, (void)startMenu, (void)pcDir, (void)overwrite;
    msg = "shortcuts: not supported on macOS yet (drag the app to the Dock)";
    return ShortcutResult::Failed;
#else
    std::vector<fs::path> dirs;
    if (!folder.empty()) {
        dirs.push_back(fs::path(folder));
    } else {
        dirs.push_back(desktopDir());
        if (startMenu) dirs.push_back(dataHome() / "applications");
    }
    const char* const kFile = "hotd-rewrithe.desktop";
    std::error_code ec;
    if (!overwrite)
        for (const fs::path& d : dirs)
            if (fs::exists(d / kFile, ec)) { msg = "A shortcut already exists in " + d.string(); return ShortcutResult::Exists; }
    const std::string exe = selfExePath();
    if (exe.empty()) { msg = "could not find this program's own path"; return ShortcutResult::Failed; }
    // the icon from the player's THOTD.EXE (folder or zip), else none
    std::string icon;
    {
        std::vector<uint8_t> pe;
        peicon::Rgba img;
        if (!pcDir.empty() && hotd::pcfs::readFile(pcDir + "/THOTD.EXE", pe) && peicon::largestImage(std::move(pe), img)) {
            const fs::path p = (folder.empty() ? dataHome() / "icons" : fs::path(folder)) / "hotd-rewrithe.png";
            fs::create_directories(p.parent_path(), ec);
            if (hotd::writePngRgba(p.string(), img.w, img.h, img.px)) icon = p.string();
        }
    }
    const std::string text = "[Desktop Entry]\nType=Application\nName=The House of the Dead: ReWrithe\n"
                             "Comment=The House of the Dead: ReWrithe\nExec=" + execQuote(exe) + "\nPath=" +
                             fs::path(exe).parent_path().string() + "\n" + (icon.empty() ? "" : "Icon=" + icon + "\n") +
                             "Terminal=false\nCategories=Game;ActionGame;\n";
    int made = 0;
    msg = "could not create the shortcut";
    for (const fs::path& d : dirs) {
        fs::create_directories(d, ec);
        const fs::path p = d / kFile;
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f << text;
        f.close();
        if (!f) continue;
        fs::permissions(p, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
        ++made;
        msg = "Shortcut created in " + d.string();
    }
    return made ? ShortcutResult::Created : ShortcutResult::Failed;
#endif
}

std::string selfExePath() {
#ifdef __APPLE__
    uint32_t n = 0;
    _NSGetExecutablePath(nullptr, &n);
    std::string s(n, '\0');
    if (n == 0 || _NSGetExecutablePath(s.data(), &n) != 0) return {};
    s.resize(std::strlen(s.c_str()));
    std::error_code ec;
    const fs::path c = fs::canonical(s, ec);
    return ec ? s : c.string();
#else
    std::error_code ec;
    const fs::path p = fs::read_symlink("/proc/self/exe", ec);   // Linux (also the Steam Deck)
    return ec ? std::string() : p.string();
#endif
}

#else

namespace {

namespace fs = std::filesystem;

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    std::wstring w(size_t(MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), int(w.size()));
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    std::string s(size_t(WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), int(s.size()), nullptr, nullptr);
    return s;
}

fs::path selfExe() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(sizeof(buf) / sizeof(buf[0])));
    return fs::path(std::wstring(buf, n));
}

fs::path knownFolder(const KNOWNFOLDERID& id) {
    PWSTR p = nullptr;
    fs::path r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) r = p;
    CoTaskMemFree(p);
    return r;
}

}  // namespace

ShortcutResult createShortcut(const std::string& folder, bool startMenu, const std::string& pcDir, bool overwrite,
                              std::string& msg) {
    std::vector<fs::path> dirs;
    if (!folder.empty()) {
        dirs.push_back(fs::path(wide(folder)));
    } else {
        dirs.push_back(knownFolder(FOLDERID_Desktop));
        if (startMenu) dirs.push_back(knownFolder(FOLDERID_Programs));
    }
    const fs::path exe = selfExe();
    std::error_code ec;
    const fs::path thotd = fs::path(wide(pcDir)) / L"THOTD.EXE";
    const bool haveIcon = !pcDir.empty() && fs::exists(thotd, ec);
    // the project's name (2026-10-17: "The House of the Dead: ReWrithe"; ':' is not allowed in a file name); a shortcut
    // under the old name counts as existing and is replaced by the new one
    const wchar_t* const kLink = L"The House of the Dead - ReWrithe.lnk";
    const wchar_t* const kOldLink = L"HOTD ReWrithe.lnk";
    if (!overwrite)
        for (const fs::path& d : dirs)
            if (!d.empty() && (fs::exists(d / kLink, ec) || fs::exists(d / kOldLink, ec))) { msg = "A shortcut already exists in " + narrow(d.wstring()); return ShortcutResult::Exists; }
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ShortcutResult result = ShortcutResult::Failed;
    msg = "could not create the shortcut";
    int made = 0;
    for (const fs::path& d : dirs) {
        if (d.empty()) continue;
        fs::create_directories(d, ec);
        IShellLinkW* link = nullptr;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link))))
            continue;
        link->SetPath(exe.c_str());
        link->SetWorkingDirectory(exe.parent_path().c_str());
        link->SetDescription(L"The House of the Dead: ReWrithe");
        link->SetIconLocation(haveIcon ? thotd.c_str() : exe.c_str(), 0);
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
            if (SUCCEEDED(file->Save((d / kLink).c_str(), TRUE))) {
                ++made;
                fs::remove(d / kOldLink, ec);   // the shortcut under the old name (only ours: that exact file name)
                msg = "Shortcut created in " + narrow(d.wstring());
            }
            file->Release();
        }
        link->Release();
    }
    if (made) result = ShortcutResult::Created;
    if (SUCCEEDED(co)) CoUninitialize();
    return result;
}

std::string selfExePath() { return narrow(selfExe().wstring()); }

ShortcutResult createShortcutTo(const std::string& linkFolder, const std::string& name, const std::string& target,
                                const std::string& iconFile, std::string& made, std::string& msg) {
    const fs::path dir(wide(linkFolder)), exe(wide(target));
    std::error_code ec;
    fs::create_directories(dir, ec);
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ShortcutResult result = ShortcutResult::Failed;
    msg = "could not create the shortcut in " + linkFolder;
    IShellLinkW* link = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link)))) {
        const fs::path icon = iconFile.empty() ? exe : fs::path(wide(iconFile));
        link->SetPath(exe.c_str());
        link->SetWorkingDirectory(exe.parent_path().c_str());
        link->SetDescription(L"The House of the Dead: ReWrithe");
        link->SetIconLocation(icon.c_str(), 0);
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
            const fs::path lnk = dir / (wide(name) + L".lnk");
            if (SUCCEEDED(file->Save(lnk.c_str(), TRUE))) {
                result = ShortcutResult::Created;
                made = narrow(lnk.wstring());
                msg = "Shortcut created: " + made;
            }
            file->Release();
        }
        link->Release();
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return result;
}

std::string desktopFolder() { return narrow(knownFolder(FOLDERID_Desktop).wstring()); }
std::string startMenuFolder() { return narrow(knownFolder(FOLDERID_Programs).wstring()); }

bool launch(const std::string& exe, const std::string& args, const std::string& workDir) {
    std::wstring cmd = L"\"" + wide(exe) + L"\"" + (args.empty() ? std::wstring() : L" " + wide(args));
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    const std::wstring wd = wide(workDir);
    if (!CreateProcessW(wide(exe).c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, wd.empty() ? nullptr : wd.c_str(),
                        &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

int runHidden(const std::string& exe, const std::string& args, int seconds) {
    std::wstring cmd = L"\"" + wide(exe) + L"\"" + (args.empty() ? std::wstring() : L" " + wide(args));
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(wide(exe).c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return -1;
    CloseHandle(pi.hThread);
    DWORD code = DWORD(-1);
    if (WaitForSingleObject(pi.hProcess, DWORD(seconds) * 1000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    return int(code);
}

#endif

#ifndef _WIN32
// linux (2026-11-03): the setup's install on Linux / macOS. Linux: "<linkFolder>/<name>.desktop" (a desktop entry, as
// createShortcut's), its icon the PNG iconFile when there is one; macOS: none yet (the package's .app is the shortcut)
ShortcutResult createShortcutTo(const std::string& linkFolder, const std::string& name, const std::string& target,
                                const std::string& iconFile, std::string& made, std::string& msg) {
#ifdef __APPLE__
    (void)linkFolder, (void)name, (void)target, (void)iconFile, (void)made;
    msg = "shortcuts: not supported on macOS yet (drag the app to the Dock)";
    return ShortcutResult::Failed;
#else
    std::error_code ec;
    const fs::path dir(linkFolder), exe(target), p = dir / (name + ".desktop");
    fs::create_directories(dir, ec);
    const bool icon = iconFile.size() > 4 && iconFile.compare(iconFile.size() - 4, 4, ".png") == 0 && fs::exists(iconFile, ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << "[Desktop Entry]\nType=Application\nName=The House of the Dead: ReWrithe\nComment=The House of the Dead: ReWrithe\n"
      << "Exec=" << execQuote(exe.string()) << "\nPath=" << exe.parent_path().string() << "\n"
      << (icon ? "Icon=" + iconFile + "\n" : std::string()) << "Terminal=false\nCategories=Game;ActionGame;\n";
    f.close();
    if (!f) { msg = "could not create the shortcut in " + linkFolder; return ShortcutResult::Failed; }
    fs::permissions(p, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
    made = p.string();
    msg = "Shortcut created: " + made;
    return ShortcutResult::Created;
#endif
}
// Linux: the freedesktop Desktop and applications menu folders; macOS: none (no desktop entries there)
std::string desktopFolder() {
#ifdef __APPLE__
    return {};
#else
    return desktopDir().string();
#endif
}
std::string startMenuFolder() {
#ifdef __APPLE__
    return {};
#else
    return (dataHome() / "applications").string();
#endif
}

namespace {
// a Windows-style command line ("quoted parts" keep their spaces) -> its arguments
std::vector<std::string> splitArgs(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false, any = false;
    for (const char c : s) {
        if (c == '"') { quoted = !quoted; any = true; continue; }
        if (!quoted && (c == ' ' || c == '\t')) {
            if (any) out.push_back(cur), cur.clear(), any = false;
            continue;
        }
        cur += c;
        any = true;
    }
    if (any) out.push_back(cur);
    return out;
}
// argv for execv (the strings stay owned by `keep`; made before fork: nothing is allocated in the child)
std::vector<char*> argvOf(std::vector<std::string>& keep) {
    std::vector<char*> v;
    for (std::string& a : keep) v.push_back(a.data());
    v.push_back(nullptr);
    return v;
}
}  // namespace

std::string findProgram(const std::string& name) {
    std::error_code ec;
    if (name.find('/') != std::string::npos) return access(name.c_str(), X_OK) == 0 ? name : std::string();
    const char* path = std::getenv("PATH");
    std::string p = path && *path ? path : "/usr/local/bin:/usr/bin:/bin";
    for (size_t a = 0; a <= p.size();) {
        size_t b = p.find(':', a);
        if (b == std::string::npos) b = p.size();
        const std::string dir = p.substr(a, b - a);
        a = b + 1;
        if (dir.empty()) continue;
        const std::string f = dir + "/" + name;
        if (fs::is_regular_file(f, ec) && access(f.c_str(), X_OK) == 0) return f;
    }
    return {};
}

// a detached child (double fork: no zombie; its own session, so it outlives this process)
bool launch(const std::string& exe, const std::string& args, const std::string& workDir) {
    const std::string path = findProgram(exe);
    if (path.empty()) return false;
    std::vector<std::string> keep = splitArgs(args);
    keep.insert(keep.begin(), path);
    std::vector<char*> argv = argvOf(keep);
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setsid();
        if (fork() != 0) _exit(0);
        if (!workDir.empty() && chdir(workDir.c_str()) != 0) _exit(127);
        execv(path.c_str(), argv.data());
        _exit(127);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

// no window here anyway: stdin / stdout / stderr to /dev/null; killed (its process group) after `seconds`
int runHidden(const std::string& exe, const std::string& args, int seconds) {
    const std::string path = findProgram(exe);
    if (path.empty()) return -1;
    std::vector<std::string> keep = splitArgs(args);
    keep.insert(keep.begin(), path);
    std::vector<char*> argv = argvOf(keep);
    const pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setpgid(0, 0);
        const int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
        execv(path.c_str(), argv.data());
        _exit(127);
    }
    setpgid(pid, pid);
    int st = 0;
    for (int ms = 0;; ms += 20) {
        const pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) break;
        if (r < 0 && errno != EINTR) return -1;
        if (ms >= seconds * 1000) {
            kill(-pid, SIGKILL);
            while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
            return -1;
        }
        usleep(20000);
    }
    if (!WIFEXITED(st)) return -1;
    return WEXITSTATUS(st) == 127 ? -1 : WEXITSTATUS(st);
}
#else
std::string findProgram(const std::string& name) {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = SearchPathW(nullptr, wide(name).c_str(), L".exe", DWORD(sizeof(buf) / sizeof(buf[0])), buf, nullptr);
    return n && n < sizeof(buf) / sizeof(buf[0]) ? narrow(std::wstring(buf, n)) : std::string();
}
#endif

void dialogFrame(void* hwnd, bool on)
{
#ifdef _WIN32
    HWND h = static_cast<HWND>(hwnd);
    if (!h) return;
    LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE);
    st = on ? st & ~LONG_PTR(WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME) : st | LONG_PTR(WS_MINIMIZEBOX);
    SetWindowLongPtrW(h, GWL_STYLE, st);
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
#else
    (void)hwnd, (void)on;
#endif
}

}  // namespace winshell
