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

#include <cstdint>
#include <cstdlib>
#include <cstring>
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
ShortcutResult createShortcutTo(const std::string&, const std::string&, const std::string&, const std::string&, std::string&,
                                std::string& msg) {
    msg = "Windows only";
    return ShortcutResult::Failed;
}
std::string desktopFolder() { return {}; }
std::string startMenuFolder() { return {}; }
bool launch(const std::string&, const std::string&, const std::string&) { return false; }
int runHidden(const std::string& exe, const std::string& args, int) { return std::system(("\"" + exe + "\" " + args).c_str()); }
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
