// The install / uninstall behind the setup (installer.h).
#include "game/installer.h"

#include "assets/archive.h"
#include "assets/pc_files.h"
#include "game/files_check.h"
#include "util/file64.h"
#include "util/game_exe.h"
#include "util/steam_shortcut.h"
#include "util/win_shell.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include "util/pe_icon.h"
#include "util/png.h"
#endif

namespace hotd::game {

namespace fs = std::filesystem;

namespace {

constexpr const char* kLinkName = "The House of the Dead - ReWrithe";
constexpr const char* kRegName = "HOTDReWrithe";

// the program in an install / package folder (linux 2026-11-03: bin/hotd on Linux / macOS)
fs::path programExe(const fs::path& root)
{
#ifdef _WIN32
    return root / "bin" / "hotd.exe";
#else
    return root / "bin" / "hotd";
#endif
}

std::string lower(std::string s)
{
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

bool startsWithI(const std::string& s, const std::string& p)
{
    return s.size() >= p.size() && lower(s.substr(0, p.size())) == lower(p);
}

// `inner` is `outer` or a folder inside it (case-blind, either separator)
bool insideOrSame(const fs::path& inner, const fs::path& outer)
{
    std::string a = lower(inner.generic_string()), b = lower(outer.generic_string());
    while (a.size() > 1 && a.back() == '/') a.pop_back();
    while (b.size() > 1 && b.back() == '/') b.pop_back();
    return a == b || (a.size() > b.size() && a.compare(0, b.size(), b) == 0 && (a[b.size()] == '/' || b.back() == '/'));
}

std::string sandbox()
{
    const char* s = std::getenv("HOTD_SETUP_SANDBOX");
    return s && *s ? std::string(s) : std::string();
}

std::string regKeyPath()
{
    return sandbox().empty() ? std::string("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + kRegName
                             : std::string("Software\\HOTD ReWrithe Test\\Uninstall\\") + kRegName;
}

bool readAll(const fs::path& p, std::vector<uint8_t>& out)
{
    std::FILE* f = std::fopen(p.string().c_str(), "rb");
    if (!f) return false;
    hotd::fseek64(f, 0, SEEK_END);
    const int64_t n = hotd::ftell64(f);
    hotd::fseek64(f, 0, SEEK_SET);
    out.resize(size_t(n > 0 ? n : 0));
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

bool writeAll(const fs::path& p, const std::vector<uint8_t>& d)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".new";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
        if (!f) { f.close(); fs::remove(tmp, ec); return false; }
    }
    fs::rename(tmp, p, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}

bool samePath(const fs::path& a, const fs::path& b)
{
    std::error_code ec;
    return fs::exists(a, ec) && fs::exists(b, ec) && fs::equivalent(a, b, ec);
}

// a path relative to root ('/' separators), or "" when outside it
std::string relTo(const fs::path& p, const fs::path& root)
{
    std::error_code ec;
    const fs::path r = fs::relative(p, root, ec);
    const std::string s = r.generic_string();
    if (ec || s.empty() || s.rfind("..", 0) == 0) return {};
    return s;
}

// the files of `where` (a file, or a folder up to 6 levels deep), as the files check lists them (files_check.cpp's
// listFiles: it was 4 here, so files 5-6 levels down passed the check and were then not installed)
std::vector<fs::path> listFiles(const fs::path& where)
{
    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::is_regular_file(where, ec)) { files.push_back(where); return files; }
    fs::recursive_directory_iterator it(where, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        if (it.depth() > 6) { it.disable_recursion_pending(); continue; }
        if (it->is_regular_file(ec)) files.push_back(it->path());
    }
    return files;
}

bool musicName(const std::string& name)
{
    const std::string n = lower(name);
    return (n.size() > 4 && (n.compare(n.size() - 4, 4, ".ogg") == 0 || n.compare(n.size() - 4, 4, ".mp3") == 0 ||
                           n.compare(n.size() - 4, 4, ".wav") == 0));   // (.wav: the setup's CD rips)
}

// the music files of the PC source: (path to read, file name)
std::vector<std::pair<std::string, std::string>> musicFiles(const std::string& pc)
{
    std::vector<std::pair<std::string, std::string>> out;
    std::error_code ec;
    if (pc.empty()) return out;
    if (pcfs::isZip(pc)) {
        std::vector<pcfs::ZipItem> items;
        std::string err;
        if (pcfs::listZip(pc, items, err))
            for (const pcfs::ZipItem& z : items)
                if (z.rel.rfind("music/", 0) == 0 && z.rel.find('/', 6) == std::string::npos && musicName(z.rel))
                    out.push_back({z.path, fs::path(z.rel).filename().string()});
        return out;
    }
    for (const auto& e : fs::directory_iterator(pc, ec)) {
        if (!e.is_directory(ec) || lower(e.path().filename().string()) != "music") continue;
        for (const auto& m : fs::directory_iterator(e.path(), ec))
            if (m.is_regular_file(ec) && musicName(m.path().filename().string()))
                out.push_back({m.path().string(), m.path().filename().string()});
    }
    return out;
}

// the PC game's data files the game reads: (relative path with '/', the path to read it by). ART\, SPRITE\, SOUND\,
// CAM\, EVT\ and the root's .PAL / .TBL / .BMP / .CFG; a zip's install may be one folder down (pcfs::listZip's rel)
std::vector<std::pair<std::string, std::string>> pcGameFiles(const std::string& pc)
{
    std::vector<std::pair<std::string, std::string>> out;
    auto wanted = [](const std::string& rel) {
        const std::string l = lower(rel);
        const size_t slash = l.find('/');
        const std::string ext = l.size() > 4 ? l.substr(l.size() - 4) : std::string();
        if (slash == std::string::npos)
            return ext == ".pal" || ext == ".tbl" || ext == ".bmp" || ext == ".cfg";
        const std::string top = l.substr(0, slash);
        return (top == "art" || top == "sprite" || top == "sound" || top == "cam" || top == "evt") &&
               l.find(".before") == std::string::npos && ext != ".bak" && ext != "orig" && ext != ".exe" && ext != ".dll";
    };
    std::error_code ec;
    if (pc.empty()) return out;
    if (pcfs::isZip(pc)) {
        std::vector<pcfs::ZipItem> items;
        std::string err;
        if (pcfs::listZip(pc, items, err))
            for (const pcfs::ZipItem& z : items)
                if (!z.rel.empty() && z.rel.back() != '/' && wanted(z.rel)) out.push_back({z.rel, z.path});
        return out;
    }
    for (const fs::path& f : listFiles(pc)) {
        const std::string rel = relTo(f, pc);
        if (!rel.empty() && wanted(rel)) out.push_back({rel, f.string()});
    }
    return out;
}

// the program's files of a package: everything but the player's own (settings\, screenshots\, data\arcade|pc|music,
// the Steam pictures, a made game exe, an install's list)
std::vector<std::string> programFiles(const fs::path& root)
{
    std::vector<std::string> out;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const std::string rel = relTo(it->path(), root);
        const std::string lr = lower(rel);
        if (it.depth() == 0 && it->is_directory(ec) && (lr == "settings" || lr == "screenshots")) {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        if (lr == lower(util::kGameExeName) || lr == lower(kInstallMarker)) continue;
        const bool player = lr.rfind("data/arcade/", 0) == 0 || lr.rfind("data/pc/", 0) == 0 || lr.rfind("data/music/", 0) == 0 ||
                            lr.rfind("data/steam/", 0) == 0 || lr == "data/hotd.zip";
        if (player && lower(it->path().extension().string()) != ".txt") continue;   // (the "put ... here" notes come along)
        if (player && it.depth() > 2) continue;
        out.push_back(rel);
    }
    std::sort(out.begin(), out.end());
    return out;
}

struct Manifest {
    std::vector<std::string> files, data, shortcuts;
    std::string regkey, version;
    bool steam = false;
};

Manifest readManifest(const fs::path& root)
{
    Manifest m;
    std::ifstream f(root / kInstallMarker);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "file") m.files.push_back(v);
        else if (k == "data") m.data.push_back(v);
        else if (k == "shortcut") m.shortcuts.push_back(v);
        else if (k == "regkey") m.regkey = v;
        else if (k == "version") m.version = v;
        else if (k == "steam") m.steam = v == "1";
    }
    return m;
}

bool writeManifest(const fs::path& root, const Manifest& m)
{
    std::ofstream f(root / kInstallMarker, std::ios::trunc);
    f << "# The House of the Dead: ReWrithe, installed copy. The uninstaller (Apps & features, or bin\\hotd.exe --uninstall)\n"
         "# removes exactly the files listed here. Do not edit.\n";
    f << "version=" << m.version << "\n";
    if (!m.regkey.empty()) f << "regkey=" << m.regkey << "\n";
    if (m.steam) f << "steam=1\n";
    for (const std::string& s : m.shortcuts) f << "shortcut=" << s << "\n";
    std::set<std::string> seen;
    for (const std::string& s : m.files) if (seen.insert(lower(s)).second) f << "file=" << s << "\n";
    for (const std::string& s : m.data) if (seen.insert(lower(s)).second) f << "data=" << s << "\n";
    return bool(f);
}

#ifdef _WIN32
std::wstring wide(const std::string& s)
{
    if (s.empty()) return {};
    std::wstring w(size_t(MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), int(w.size()));
    return w;
}
bool regSet(HKEY k, const wchar_t* name, const std::string& v)
{
    const std::wstring w = wide(v);
    return RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(w.c_str()), DWORD((w.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}
bool regDword(HKEY k, const wchar_t* name, DWORD v)
{
    return RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof v) == ERROR_SUCCESS;
}
#endif

// the Apps & features entry (HKCU: no admin)
bool writeUninstallEntry(const fs::path& root, const std::string& version, uint64_t bytes, std::string& key)
{
#ifdef _WIN32
    key = regKeyPath();
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, wide(key).c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;
    const std::string exe = programExe(root).string(), game = (root / util::kGameExeName).string();
    std::error_code ec;
    char date[16];
    const std::time_t t = std::time(nullptr);
    std::strftime(date, sizeof date, "%Y%m%d", std::localtime(&t));
    bool ok = regSet(k, L"DisplayName", "The House of the Dead: ReWrithe") &&
              regSet(k, L"DisplayVersion", version.size() > 1 && version[0] == 'v' ? version.substr(1) : version) &&
              regSet(k, L"Publisher", "Tawhid (fan project)") && regSet(k, L"InstallLocation", root.string()) &&
              regSet(k, L"DisplayIcon", fs::exists(game, ec) ? game : exe) &&
              regSet(k, L"UninstallString", "\"" + exe + "\" --uninstall") &&
              regSet(k, L"QuietUninstallString", "\"" + exe + "\" --uninstall --yes --keep-settings") &&
              regSet(k, L"URLInfoAbout", "https://github.com/TawhidI-602/The-House-of-the-Dead-ReWrithe") &&
              regSet(k, L"InstallDate", date) && regDword(k, L"NoModify", 1) && regDword(k, L"NoRepair", 1) &&
              regDword(k, L"EstimatedSize", DWORD(bytes / 1024));
    RegCloseKey(k);
    return ok;
#else
    (void)root, (void)version, (void)bytes, (void)key;
    return false;
#endif
}

void removeUninstallEntry(const std::string& key)
{
#ifdef _WIN32
    // only our own key names (never another program's)
    if (key != std::string("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + kRegName &&
        key != std::string("Software\\HOTD ReWrithe Test\\Uninstall\\") + kRegName)
        return;
    RegDeleteKeyW(HKEY_CURRENT_USER, wide(key).c_str());
    if (key.rfind("Software\\HOTD ReWrithe Test\\", 0) == 0) {   // (the test's parent keys, when empty)
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\HOTD ReWrithe Test\\Uninstall");
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\HOTD ReWrithe Test");
    }
#else
    (void)key;
#endif
}

// the folders under root left empty (deepest first), then root itself when empty
void removeEmptyDirs(const fs::path& root)
{
    std::error_code ec;
    std::vector<fs::path> dirs;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec) && !it->is_symlink(ec)) dirs.push_back(it->path());
    std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) { return a.string().size() > b.string().size(); });
    for (const fs::path& d : dirs)
        if (fs::is_empty(d, ec)) fs::remove(d, ec);   // (remove() of a folder: only an empty one)
    if (fs::is_empty(root, ec)) fs::remove(root, ec);
}

} // namespace

std::string defaultInstallDir()
{
#ifdef _WIN32
    return "C:\\Games\\HOTD ReWrithe";
#else
    const char* h = std::getenv("HOME");
    return std::string(h && *h ? h : ".") + "/Games/HOTD ReWrithe";
#endif
}

bool isInstall(const std::string& root)
{
    std::error_code ec;
    return !root.empty() && fs::is_regular_file(fs::path(root) / kInstallMarker, ec);
}

std::string installDirFor(const std::string& picked)
{
    std::error_code ec;
    const fs::path p(picked);
    if (picked.empty() || !fs::exists(p, ec) || isInstall(picked) || (fs::is_directory(p, ec) && fs::is_empty(p, ec)))
        return picked;
    return (p / "HOTD ReWrithe").string();
}

std::string installDirProblem(const std::string& dir, const std::string& programRoot)
{
    if (dir.empty()) return "Choose a folder for the game.";
    const fs::path p(dir);
#ifdef _WIN32
    if (!p.is_absolute()) return "Choose a full path (with its drive), e.g. " + defaultInstallDir() + ".";
#else
    if (!p.is_absolute()) return "Choose a full path, e.g. " + defaultInstallDir() + ".";
#endif
#ifdef _WIN32
    for (const char* env : {"ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"}) {
        const char* v = std::getenv(env);
        if (v && *v && startsWithI(p.string() + "\\", std::string(v) + "\\"))
            return "Not in Program Files: Windows lets programs write there only as administrator, and the game keeps its "
                   "settings and your files in its own folder. Choose another folder, e.g. " + defaultInstallDir() + ".";
    }
    const char* win = std::getenv("SystemRoot");
    if (win && *win && startsWithI(p.string() + "\\", std::string(win) + "\\"))
        return "Not in the Windows folder: choose another folder, e.g. " + defaultInstallDir() + ".";
#endif
    std::error_code ec;
    if (!fs::exists(p.root_path(), ec)) return "The drive " + p.root_name().string() + " is not there.";
    if (fs::exists(p, ec) && !fs::is_directory(p, ec)) return "There is a file of that name: choose a folder.";
    // (user 2026-11-02 "make sure their original files do not get touched": the setup builds a new copy) a folder
    // with other things in it (the PC game's own folder, a downloads folder): never written into
    if (fs::is_directory(p, ec) && !isInstall(dir) && !fs::is_empty(p, ec))
        return "That folder has other files in it: choose an empty or new folder, e.g. " + (p / "HOTD ReWrithe").string() + ".";
    if (!programRoot.empty()) {
        const std::string a = lower(fs::weakly_canonical(p, ec).string()), b = lower(fs::weakly_canonical(programRoot, ec).string());
        if (a == b && !isInstall(programRoot)) return "That is this folder: the game runs from here already (Play from here).";
        if (a.size() > b.size() && a.compare(0, b.size(), b) == 0 && (a[b.size()] == '\\' || a[b.size()] == '/'))
            return "Not inside this folder: choose a folder outside it.";
    }
    return {};
}

int64_t freeBytes(const std::string& dir)
{
    std::error_code ec;
    fs::path p(dir);
    while (!p.empty() && !fs::exists(p, ec)) {
        const fs::path up = p.parent_path();
        if (up == p) break;
        p = up;
    }
    if (p.empty()) return -1;
    const fs::space_info s = fs::space(p, ec);
    return ec ? -1 : int64_t(s.available);
}

int musicTracks(const std::string& pc)
{
    return int(musicFiles(pc).size());
}

InstallResult installGame(const InstallOptions& o, const std::function<void(float, const std::string&)>& progress)
{
    InstallResult r;
    auto say = [&](float f, const std::string& s) { if (progress) progress(f, s); };
    auto log = [&](const std::string& s) { r.lines.push_back(s); SDL_Log("[setup] %s", s.c_str()); };
    std::error_code ec;
    const fs::path dest(o.dest), from(o.programRoot);
    const bool repair = !o.programRoot.empty() && samePath(dest, from);
    if (const std::string why = repair ? std::string() : installDirProblem(o.dest, o.programRoot); !why.empty()) {
        r.err = why;
        return r;
    }
    if (!repair)   // the player's originals: only read, so never the new copy's folder or one around it
        for (const std::string& src : {o.arcade, o.pc}) {
            const fs::path s = fs::weakly_canonical(fs::path(src), ec);
            if (src.empty() || !fs::is_directory(s, ec)) continue;   // (a zip: only read, never next to anything of ours)
            if (insideOrSame(fs::weakly_canonical(dest, ec), s)) {   // the same folder, or one inside it
                r.err = "The new copy cannot go into the folder of your own game files (" + s.string() + "): choose another folder.";
                return r;
            }
        }
    fs::create_directories(dest, ec);
    if (!fs::is_directory(dest, ec)) { r.err = "Cannot make the folder " + o.dest + " (" + ec.message() + ")."; return r; }
    Manifest m = isInstall(o.dest) ? readManifest(dest) : Manifest{};
    m.version = o.version;
    // 1. the program (a fresh install or an update of an older install; Repair keeps it)
    std::vector<std::string> prog;
    if (!repair) {
        if (o.programRoot.empty() || !fs::exists(from / "hotd_package.txt", ec)) {
            r.err = "This copy is not a package (no hotd_package.txt): build it with tools\\make_portable_hotd.ps1.";
            return r;
        }
        prog = programFiles(from);
    }
    uint64_t total = 0;
    for (const std::string& rel : prog) total += uint64_t(fs::file_size(from / rel, ec));
    total += 200ull << 20;   // (the player's files: about 200 MB, the bar's second half)
    uint64_t done = 0;
    for (size_t i = 0; i < prog.size(); ++i) {
        const fs::path s = from / prog[i], d = dest / prog[i];
        if ((i & 7) == 0) say(float(double(done) / double(total)), "Copying " + prog[i]);
        fs::create_directories(d.parent_path(), ec);
        if (!fs::copy_file(s, d, fs::copy_options::overwrite_existing, ec)) {
            r.err = "Cannot copy " + prog[i] + ": " + ec.message() + (ec.value() == 32 ? " (close the game in that folder first)" : "");
            writeManifest(dest, m);   // (what was copied so far: the uninstaller can still remove it)
            return r;
        }
        m.files.push_back(prog[i]);
        done += uint64_t(fs::file_size(s, ec));
    }
    if (!repair) log("program: " + std::to_string(prog.size()) + " files copied to " + o.dest);
    writeManifest(dest, m);
    const float progBase = float(double(done) / double(total)), dataSpan = 1.0f - progBase;
    // 2. the arcade ROMs: exactly the 29 files of the set (by size + CRC-32, any names), from zips or loose files
    int nIds = 0;
    const ArcadeRomId* ids = arcadeRomIds(nIds);
    std::vector<bool> got(size_t(nIds), false);
    const fs::path arcadeOut = dest / "data" / "arcade";
    int copied = 0;
    auto take = [&](int i, const std::vector<uint8_t>& d, const fs::path& src) -> bool {
        const fs::path out = arcadeOut / ids[i].name;
        say(progBase + dataSpan * 0.5f * float(copied) / float(nIds), std::string("Importing ") + ids[i].name);
        const bool same = !src.empty() && samePath(src, out);
        if (!same && !writeAll(out, d)) {
            r.err = std::string("Cannot write ") + out.string();
            return false;
        }
        got[size_t(i)] = true;
        ++copied;
        if (!same) m.data.push_back("data/arcade/" + std::string(ids[i].name));   // (the uninstaller's list: only our copies)
        return true;
    };
    for (const fs::path& f : listFiles(o.arcade)) {
        if (copied == nIds) break;
        const uint64_t size = fs::file_size(f, ec);
        if (lower(f.extension().string()) == ".zip") {
            ZipFile z;
            std::string err;
            if (!z.open(f.string(), err)) continue;
            for (const ZipFile::Entry& e : z.entries())
                for (int i = 0; i < nIds; ++i) {
                    if (got[size_t(i)] || e.size != ids[i].size || e.crc != ids[i].crc) continue;
                    std::vector<uint8_t> d;
                    if (!z.read(e, d, err) || crc32(d.data(), d.size()) != ids[i].crc) break;
                    if (!take(i, d, {})) { writeManifest(dest, m); return r; }
                    break;
                }
            continue;
        }
        // a loose file: read once, then the set's file with its size AND CRC (many of the 29 share a size: stopping at
        // the first same-size one imported only 4 of a folder's / an unpacked 7z's files while the check, which matches
        // by CRC, said Revision A; YouTube report 2026-11-03)
        std::vector<uint8_t> d;
        uint32_t crc = 0;
        for (int i = 0; i < nIds; ++i) {
            if (got[size_t(i)] || size != ids[i].size) continue;
            if (d.empty()) {
                if (!readAll(f, d)) break;
                crc = crc32(d.data(), d.size());
            }
            if (crc != ids[i].crc) continue;
            if (!take(i, d, f)) { writeManifest(dest, m); return r; }
            break;
        }
    }
    if (copied != nIds) {
        r.err = "Only " + std::to_string(copied) + " of the " + std::to_string(nIds) + " arcade ROM files were found to import.";
        writeManifest(dest, m);
        return r;
    }
    log("arcade: " + std::to_string(copied) + " ROM files imported into data\\arcade");
    writeManifest(dest, m);
    // 3. the PC files the game reads (the files check's list: each checked file, wherever it was found)
    {
        std::map<std::string, std::string> found;
        const fs::path memo = dest / "settings" / "setup_pc_files.tsv";
        fs::create_directories(memo.parent_path(), ec);
        say(progBase + dataSpan * 0.5f, "Checking the PC files ...");
        const CheckReport pr = checkPc(o.pc, memo.string(), found);
        fs::remove(memo, ec);
        if (!pr.ok) {
            r.err = "The PC files did not pass the check (Back: the Game files page says why).";
            writeManifest(dest, m);
            return r;
        }
        int k = 0;
        for (const auto& [rel, real] : found) {
            const fs::path out = dest / "data" / "pc" / fs::path(rel);
            if ((k & 3) == 0) say(progBase + dataSpan * (0.5f + 0.4f * float(k) / float(std::max<size_t>(found.size(), 1))), "Importing " + rel);
            ++k;
            if (!samePath(fs::path(real), out)) {
                std::vector<uint8_t> d;
                const bool read = pcfs::readFile(real, d);
                if (read && fixEuBigger(rel, d)) log("PC: BIGGER.VMC of the European CD: its 5 changed bits set back (now the US file)");
                if (!read || !writeAll(out, d)) {
                    r.err = "Cannot import " + rel + " into " + out.string();
                    writeManifest(dest, m);
                    return r;
                }
                m.data.push_back("data/pc/" + fs::path(rel).generic_string());
            }
        }
        // the other PC files the game reads (the check's list is its subset: the menu art SPRITE\SPR.RBH, the models
        // ART\, the camera rails CAM\, the event scripts EVT\, the palettes *.PAL / *.TBL): those folders and the
        // root's .PAL / .TBL / .BMP / .CFG files; never the PC's programs, DLLs, installers or backups
        std::set<std::string> done;
        for (const auto& [rel, real] : found) done.insert(lower(fs::path(rel).generic_string()));
        int extra = 0;
        for (const auto& [rel, src] : pcGameFiles(o.pc)) {
            if (done.count(lower(rel))) continue;
            const fs::path out = dest / "data" / "pc" / fs::path(rel);
            if ((extra & 15) == 0) say(progBase + dataSpan * 0.9f, "Importing " + rel);
            std::vector<uint8_t> d;
            const bool same = samePath(fs::path(src), out);
            if (!same && (!pcfs::readFile(src, d) || !writeAll(out, d))) {
                r.err = "Cannot import " + rel + " into " + out.string();
                writeManifest(dest, m);
                return r;
            }
            if (!same) m.data.push_back("data/pc/" + rel);
            ++extra;
        }
        log("PC: " + std::to_string(found.size()) + " checked + " + std::to_string(extra) + " other game files imported into data\\pc");
    }
    writeManifest(dest, m);
    // 4. the CD music (optional)
    if (o.music) {
        int n = 0;
        for (const auto& [src, name] : musicFiles(o.pc)) {
            const fs::path out = dest / "data" / "music" / name;
            say(progBase + dataSpan * 0.92f, "Importing music " + name);
            std::vector<uint8_t> d;
            if (samePath(fs::path(src), out)) continue;
            if (!pcfs::readFile(src, d) || !writeAll(out, d)) continue;
            m.data.push_back("data/music/" + name);
            ++n;
        }
        log("music: " + std::to_string(n) + " CD track(s) imported into data\\music");
    }
    // 5. the settings of the copy the setup ran from (its choices, not its paths: data\ is found by itself)
    if (!o.settingsFrom.empty() && !repair && !fs::exists(dest / "settings" / "launcher.ini", ec) && fs::exists(o.settingsFrom, ec)) {
        std::ifstream in(o.settingsFrom);
        std::ofstream out(dest / "settings" / "launcher.ini");
        for (std::string line; std::getline(in, line);)
            if (line.rfind("arcade=", 0) != 0 && line.rfind("pc=", 0) != 0) out << line << "\n";
        log("settings: carried over from " + o.settingsFrom);
    }
    // 6. the game exe with the PC game's icon, the shortcuts, Apps & features
    say(0.97f, "Making the game's shortcut ...");
    std::string msg;
    const fs::path game = dest / util::kGameExeName;
    if (util::makeGameExe(dest.string(), (dest / "data" / "pc").string(), msg)) {
        r.gameExe = game.string();
        m.files.push_back(util::kGameExeName);
    } else {
        r.gameExe = programExe(dest).string();
    }
    log("game exe: " + msg);
    std::string icon = r.gameExe;
#ifndef _WIN32
    {   // linux (2026-11-03): the desktop entries' icon: THOTD.EXE's icon as a PNG in the install (the player's own file)
        std::vector<uint8_t> pe;
        peicon::Rgba img;
        const fs::path png = dest / "hotd-rewrithe.png";
        if (pcfs::readFile((dest / "data" / "pc" / "THOTD.EXE").string(), pe) && peicon::largestImage(std::move(pe), img) &&
            hotd::writePngRgba(png.string(), img.w, img.h, img.px)) {
            icon = png.string();
            if (std::find(m.files.begin(), m.files.end(), "hotd-rewrithe.png") == m.files.end()) m.files.push_back("hotd-rewrithe.png");
        }
    }
#endif
    const std::string sb = sandbox();
    for (int which = 0; which < 2; ++which) {
        if (which == 0 ? !o.desktop : !o.startMenu) continue;
        const std::string folder = !sb.empty() ? (fs::path(sb) / (which == 0 ? "Desktop" : "Start Menu")).string()
                                               : which == 0 ? winshell::desktopFolder() : winshell::startMenuFolder();
        if (folder.empty()) continue;
        std::string made;
        if (winshell::createShortcutTo(folder, kLinkName, r.gameExe, icon, made, msg) == winshell::ShortcutResult::Created) {
            if (std::find(m.shortcuts.begin(), m.shortcuts.end(), made) == m.shortcuts.end()) m.shortcuts.push_back(made);
        }
        log(msg);
    }
    if (o.steam) {
        steam::Options so;
        so.exe = programExe(dest).string();
        so.pcDir = (dest / "data" / "pc").string();
        const steam::Result sr = steam::addToSteam(so);
        m.steam = m.steam || sr.ok;
        log("Steam: " + sr.brief);
    }
    uint64_t bytes = 0;
    for (const std::string& s : m.files) bytes += uint64_t(fs::file_size(dest / s, ec));
    for (const std::string& s : m.data) bytes += uint64_t(fs::file_size(dest / s, ec));
    std::string key;
    if (writeUninstallEntry(dest, o.version, bytes, key)) {
        m.regkey = key;
        log("Apps & features entry: HKCU\\" + key);
    }
    if (!writeManifest(dest, m)) { r.err = "Cannot write " + (dest / kInstallMarker).string(); return r; }
    say(1.0f, "Done");
    r.ok = true;
    return r;
}

int uninstallGame(const std::string& rootIn, bool yes, int removeSettings)
{
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(fs::path(rootIn), ec);
    auto ask = [&](const std::string& title, const std::string& text, const char* yesText, const char* noText) {
        const SDL_MessageBoxButtonData b[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, yesText},
                                              {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, noText}};
        const SDL_MessageBoxData box = {SDL_MESSAGEBOX_WARNING, nullptr, title.c_str(), text.c_str(), 2, b, nullptr};
        int pick = 0;
        return SDL_ShowMessageBox(&box, &pick) && pick == 1;
    };
    if (!isInstall(root.string()) || !fs::exists(root / "hotd_package.txt", ec)) {
        const std::string msg = "This is not an installed copy of The House of the Dead: ReWrithe (no " + std::string(kInstallMarker) +
                                " in " + root.string() + "): nothing was removed. A portable copy is removed by deleting its folder.";
        std::printf("uninstall: %s\n", msg.c_str());
        if (!yes) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Uninstall", msg.c_str(), nullptr);
        return 2;
    }
    if (!yes && !ask("Uninstall The House of the Dead: ReWrithe",
                     "Remove The House of the Dead: ReWrithe from\n" + root.string() +
                         "?\n\nThe program, its shortcuts and the game files the setup imported are removed. Your own "
                         "originals (ROM zip, PC game) are not touched.",
                     "Uninstall", "Cancel"))
        return 1;
    if (removeSettings < 0)
        removeSettings = yes ? 0 : ask("Uninstall", "Also delete your settings and high scores (the settings folder)?\n\n"
                                                    "No keeps them in " + (root / "settings").string() + " for a later install.",
                                       "Delete them", "Keep them") ? 1 : 0;
    const Manifest m = readManifest(root);
    for (const std::string& s : m.shortcuts) {
        const fs::path p(s);
        const std::string ext = lower(p.extension().string());   // (Linux: the .desktop entries)
        if ((ext == ".lnk" || ext == ".desktop") && lower(p.stem().string()) == lower(kLinkName)) fs::remove(p, ec);
    }
    if (m.steam) {
        steam::Options so;
        so.exe = programExe(root).string();
        steam::removeFromSteam(so);
    }
    if (!m.regkey.empty()) removeUninstallEntry(m.regkey);
    // the listed files (only inside root; never a path with ..); the running exe and its DLLs after it exits
    const fs::path self = fs::weakly_canonical(fs::path(winshell::selfExePath()), ec);
    std::vector<fs::path> later;
    auto removeRel = [&](const std::string& rel) {
        if (rel.empty() || rel.find("..") != std::string::npos || fs::path(rel).is_absolute()) return;
        const fs::path p = root / fs::path(rel);
        if (!fs::is_regular_file(p, ec)) return;
        if (!fs::remove(p, ec) || ec) later.push_back(p);
    };
    for (const std::string& s : m.data) removeRel(s);
    for (const std::string& s : m.files) removeRel(s);
    {   // the updater's own folder (its copy of hotd_launch, the download, its log): never the player's
        const fs::path up = root / "settings" / "update";
        if (fs::is_directory(up, ec) && !fs::is_symlink(up, ec)) fs::remove_all(up, ec);
    }
    if (removeSettings == 1) {
        const fs::path st = root / "settings";
        if (fs::is_directory(st, ec) && !fs::is_symlink(st, ec)) fs::remove_all(st, ec);   // (that one folder, the player said yes)
    }
    fs::remove(root / kInstallMarker, ec);
    removeEmptyDirs(root);
    std::printf("uninstall: %s removed (%zu file(s) after the program exits, settings %s)\n", root.string().c_str(), later.size(),
                removeSettings == 1 ? "deleted" : "kept");
#ifdef _WIN32
    if (!later.empty()) {   // the running hotd.exe and its DLLs: cmd waits 3 s, deletes them by name, then the empty folders
        fs::path r = root;
        if (r.filename().empty()) r = r.parent_path();   // (no trailing separator before a quote)
        wchar_t sys[MAX_PATH] = {};
        GetSystemDirectoryW(sys, MAX_PATH);
        const std::wstring exe = std::wstring(sys) + L"\\cmd.exe";
        std::wstring cmd = L"\"" + exe + L"\" /d /s /c \"ping -n 4 127.0.0.1 >nul";
        for (fs::path p : later) cmd += L" & del /f /q \"" + p.make_preferred().wstring() + L"\"";
        cmd += L" & rmdir \"" + (r / "bin").make_preferred().wstring() + L"\" & rmdir \"" + r.make_preferred().wstring() + L"\"\"";
        STARTUPINFOW si{};
        si.cb = sizeof si;
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, nullptr,
                           sys, &si, &pi) ||
            CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, sys, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
#endif
    (void)self;
    if (!yes)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Uninstall",
                                 removeSettings == 1 ? "The House of the Dead: ReWrithe was removed."
                                                     : "The House of the Dead: ReWrithe was removed. Your settings and high scores are kept in its settings folder.",
                                 nullptr);
    return 0;
}

} // namespace hotd::game
