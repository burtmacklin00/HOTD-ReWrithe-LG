// Light Gun Edition: the quick settings as silent programs (as the Lost World Native LG's). One program, copied by the
// packaging (tools/make_portable_hotd.ps1) under each setting's name ("2 - Display - 4x3.exe", "2 - Crosshairs - Off.exe"
// ...): it does what its own name says, with no window (a .bat would flash a console). Only a problem is shown, in a
// message box. Takes effect the next time the game starts.
//
//   2 - Display - 4x3 / 16x9   the lines of 4x3\launcher.ini or 16x9\launcher.ini (aspect=1 / aspect=0; add more lines
//                              there to change them only in that mode) put into settings\launcher.ini
//   2 - Crosshairs - On / Off  crosshairs=1 / 0
//   3 - Online - Host / Join   start the game through the launcher (Launch HOTD ReWrithe - MLGB.ahk if it is there, else
//                              Launch HOTD ReWrithe.ahk; no AutoHotkey: bin\hotd.exe itself) with host / join: the game
//                              opens Online Co-op by itself (--online-host / --online-join: the game's code copied /
//                              a game on this network joined, else the code on the clipboard)
//
// settings\launcher.ini keeps every other line as it was; a key that is not there is added.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

namespace {

using Keys = std::vector<std::pair<std::string, std::string>>;

struct Preset {
    const wchar_t* name;     // the program's name, without .exe
    const wchar_t* folder;   // its lines come from <folder>\launcher.ini ("" none)
    Keys keys;               // else (or with the folder's file missing) these
    const wchar_t* launch = nullptr;   // a launch (3 - Online): the launcher's argument (host / join), no settings
};

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = {
        {L"2 - Display - 4x3", L"4x3", {{"aspect", "1"}}},
        {L"2 - Display - 16x9", L"16x9", {{"aspect", "0"}}},
        {L"2 - Crosshairs - On", L"", {{"crosshairs", "1"}}},
        {L"2 - Crosshairs - Off", L"", {{"crosshairs", "0"}}},
        {L"3 - Online - Host", L"", {}, L"host"},
        {L"3 - Online - Join", L"", {}, L"join"},
    };
    return list;
}

std::wstring g_dir;   // the program's folder (the game's), with a backslash

void fail(const std::wstring& what)
{
    MessageBoxW(nullptr, what.c_str(), L"The House of the Dead ReWrithe - Light Gun Edition", MB_ICONERROR | MB_OK);
}

std::string trim(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool readLines(const std::wstring& file, std::vector<std::string>& lines)
{
    FILE* f = _wfopen(file.c_str(), L"rb");
    if (!f) return false;
    std::string all;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
    fclose(f);
    if (all.size() >= 3 && static_cast<unsigned char>(all[0]) == 0xEF && static_cast<unsigned char>(all[1]) == 0xBB &&
        static_cast<unsigned char>(all[2]) == 0xBF)
        all.erase(0, 3);   // (a UTF-8 BOM from Notepad)
    size_t at = 0;
    while (at < all.size()) {
        const size_t e = all.find('\n', at);
        std::string line = all.substr(at, e == std::string::npos ? std::string::npos : e - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        if (e == std::string::npos) break;
        at = e + 1;
    }
    return true;
}

// <folder>\launcher.ini's "key=value" lines (comments ; # and blank lines skipped)
bool folderKeys(const std::wstring& file, Keys& keys)
{
    std::vector<std::string> lines;
    if (!readLines(file, lines)) return false;
    keys.clear();
    for (const std::string& raw : lines) {
        const std::string line = trim(raw);
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq));
        if (!k.empty()) keys.push_back({k, trim(line.substr(eq + 1))});
    }
    return true;
}

// settings\launcher.ini with `keys` set (the game's own "key=value" form): their lines replaced, the rest kept
bool setKeys(const Keys& keys)
{
    if (keys.empty()) return true;
    CreateDirectoryW((g_dir + L"settings").c_str(), nullptr);
    const std::wstring ini = g_dir + L"settings\\launcher.ini", tmp = ini + L".tmp";
    std::vector<std::string> lines;
    readLines(ini, lines);
    while (!lines.empty() && trim(lines.back()).empty()) lines.pop_back();
    std::vector<bool> done(keys.size(), false);
    for (std::string& line : lines) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        for (size_t k = 0; k < keys.size(); k++)
            if (key == keys[k].first) {
                line = keys[k].first + "=" + keys[k].second;
                done[k] = true;
            }
    }
    for (size_t k = 0; k < keys.size(); k++)
        if (!done[k]) lines.push_back(keys[k].first + "=" + keys[k].second);
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) return false;
    for (const std::string& line : lines) fprintf(f, "%s\n", line.c_str());
    const bool ok = fclose(f) == 0;
    return ok && MoveFileExW(tmp.c_str(), ini.c_str(), MOVEFILE_REPLACE_EXISTING);
}

// the game of this folder (bin\hotd.exe) is running: it writes launcher.ini when it closes, which would undo the change
bool gameRunning()
{
    const std::wstring game = g_dir + L"bin\\hotd.exe";
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    for (BOOL more = Process32FirstW(snap, &pe); more && !found; more = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"hotd.exe") != 0) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) continue;
        wchar_t path[MAX_PATH * 2];
        DWORD len = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(h, 0, path, &len) && _wcsicmp(path, game.c_str()) == 0) found = true;
        CloseHandle(h);
    }
    CloseHandle(snap);
    return found;
}

// 3 - Online: the launcher (its Esc, Hook of the Reaper, a front-end's steps) with the argument, else the game itself
bool launch(const std::wstring& arg)
{
    for (const wchar_t* name : {L"Launch HOTD ReWrithe - MLGB.ahk", L"Launch HOTD ReWrithe.ahk"}) {
        const std::wstring ahk = g_dir + name;
        if (GetFileAttributesW(ahk.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        // (ShellExecute's result above 32: started; a .ahk with no AutoHotkey installed fails: the game itself next)
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", ahk.c_str(), arg.c_str(), g_dir.c_str(), SW_SHOWNORMAL)) > 32)
            return true;
        break;
    }
    const std::wstring game = g_dir + L"bin\\hotd.exe", args = L"--online-" + arg;
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", game.c_str(), args.c_str(), (g_dir + L"bin").c_str(),
                                                   SW_SHOWNORMAL)) > 32;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    wchar_t path[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH * 2);
    const std::wstring self(path, n);
    const size_t slash = self.find_last_of(L"\\/");
    g_dir = self.substr(0, slash + 1);
    std::wstring name = self.substr(slash + 1);
    if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".exe") == 0) name.resize(name.size() - 4);

    const Preset* preset = nullptr;
    for (const Preset& p : presets())
        if (_wcsicmp(p.name, name.c_str()) == 0) preset = &p;
    if (!preset) {
        fail(L"\"" + name + L".exe\" is not one of the quick settings (2 - Display, 2 - Crosshairs, 3 - Online): keep these programs' "
             L"names as they came.");
        return 1;
    }
    if (gameRunning()) {
        fail(L"Close the game first, then run \"" + name + L"\" again (the game saves its settings when it closes).");
        return 1;
    }
    if (preset->launch) {
        if (!launch(preset->launch)) {
            fail(L"Could not start the game (bin\\hotd.exe): keep \"" + name + L".exe\" in the game's folder.");
            return 1;
        }
        return 0;
    }
    Keys keys = preset->keys;
    if (*preset->folder) {
        const std::wstring file = g_dir + preset->folder + L"\\launcher.ini";
        Keys fromFolder;
        if (folderKeys(file, fromFolder) && !fromFolder.empty()) keys = fromFolder;
    }
    if (!setKeys(keys)) {
        fail(L"Could not write settings\\launcher.ini (is the folder read-only?)");
        return 1;
    }
    return 0;
}
