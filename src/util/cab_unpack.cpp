// Microsoft cabinet sets (cab_unpack.h).
#include "util/cab_unpack.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <fdi.h>
#pragma comment(lib, "cabinet.lib")
#else
#include "util/win_shell.h"
#endif

namespace hotd::cab {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s)
{
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

#ifdef _WIN32
struct Ctx {
    std::string out;
    int files = 0;
    bool failed = false;
};

FNALLOC(memAlloc) { return std::malloc(cb); }
FNFREE(memFree) { std::free(pv); }
FNOPEN(fileOpen) { return _open(pszFile, oflag | _O_BINARY, pmode); }
FNREAD(fileRead) { return UINT(_read(int(hf), pv, cb)); }
FNWRITE(fileWrite) { return UINT(_write(int(hf), pv, cb)); }
FNCLOSE(fileClose) { return _close(int(hf)); }
FNSEEK(fileSeek) { return long(_lseek(int(hf), dist, seektype)); }

FNFDINOTIFY(notify)
{
    Ctx* c = static_cast<Ctx*>(pfdin->pv);
    switch (fdint) {
    case fdintCOPY_FILE: {
        std::string name = pfdin->psz1 ? pfdin->psz1 : "";
        std::replace(name.begin(), name.end(), '\\', '/');
        if (name.empty() || name.find("..") != std::string::npos || name[0] == '/' || name.find(':') != std::string::npos)
            return 0;   // (skipped: never a path outside the folder)
        const fs::path p = fs::path(c->out) / fs::path(name);
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        const int h = _wopen(p.wstring().c_str(), _O_BINARY | _O_CREAT | _O_TRUNC | _O_WRONLY, _S_IREAD | _S_IWRITE);
        if (h < 0) c->failed = true;
        return h < 0 ? -1 : INT_PTR(h);
    }
    case fdintCLOSE_FILE_INFO:
        _close(int(pfdin->hf));
        ++c->files;
        return TRUE;
    case fdintNEXT_CABINET:
        return 0;   // (the next cabinet in the same folder: FDI opens psz3 + psz1)
    default:
        return 0;
    }
}
#endif

// "stage1/creature/creat1.rbh" -> "STAGE1" ... : the first folder name of a relative path
std::string firstDir(const std::string& rel)
{
    const size_t k = rel.find('/');
    return k == std::string::npos ? std::string() : rel.substr(0, k);
}

} // namespace

std::string gameCabinet(const std::string& dir)
{
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(fs::path(dir), ec)) return {};
    std::string cab1;
    bool exe = false;
    for (auto it = fs::recursive_directory_iterator(fs::path(dir), ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
        const std::string n = lower(it->path().filename().string());
        exe |= n == "thotd.exe";
        if (n == "cab1.cab" && cab1.empty() && lower(it->path().parent_path().filename().string()) == "cab")
            cab1 = it->path().string();
    }
    return exe ? std::string() : cab1;
}

bool extractSet(const std::string& cab1, const std::string& outDir, std::string& err, bool& toolMissing)
{
    toolMissing = false;
    std::error_code ec;
    fs::create_directories(fs::path(outDir), ec);
#ifdef _WIN32
    ERF erf{};
    HFDI h = FDICreate(memAlloc, memFree, fileOpen, fileRead, fileWrite, fileClose, fileSeek, cpuUNKNOWN, &erf);
    if (!h) { err = "the cabinet reader could not start"; return false; }
    Ctx c;
    c.out = outDir;
    const fs::path p = fs::path(cab1);
    std::string folder = p.parent_path().string();
    if (!folder.empty() && folder.back() != '\\' && folder.back() != '/') folder += '\\';
    // each cabinet of the set in turn (FDI copies the files that start in the one it is given, following a file
    // continued into the next; the ones continued from the one before are skipped there): CAB1, CAB2 ... while found
    BOOL ok = TRUE;
    for (int i = 1; ok && i < 100; ++i) {
        const std::string name = i == 1 ? p.filename().string() : "CAB" + std::to_string(i) + ".CAB";
        if (!fs::exists(fs::path(folder) / name, ec)) break;
        ok = FDICopy(h, const_cast<char*>(name.c_str()), const_cast<char*>(folder.c_str()), 0, notify, nullptr, &c);
    }
    FDIDestroy(h);
    if (!ok || c.failed || c.files == 0) {
        err = c.failed ? "a file could not be written (disk space?)" : "the cabinets could not be read (error " + std::to_string(erf.erfOper) + ")";
        return false;
    }
    return true;
#else
    // cabextract follows the set (the next cabinets beside the first); bsdtar reads each cabinet on its own
    if (const std::string t = winshell::findProgram("cabextract"); !t.empty())
        return winshell::runHidden(t, "-q -d \"" + outDir + "\" \"" + cab1 + "\"", 900) == 0 || (err = "cabextract failed", false);
    std::string tar;
#ifdef __APPLE__
    tar = "/usr/bin/tar";
    if (!fs::exists(tar, ec)) tar.clear();
#endif
    if (tar.empty()) tar = winshell::findProgram("bsdtar");
    if (tar.empty()) { toolMissing = true; err = "no cabextract or bsdtar"; return false; }
    int done = 0;
    for (const auto& e : fs::directory_iterator(fs::path(cab1).parent_path(), ec)) {
        const std::string n = lower(e.path().filename().string());
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".cab") == 0 &&
            winshell::runHidden(tar, "-xf \"" + e.path().string() + "\" -C \"" + outDir + "\"", 900) == 0)
            ++done;
    }
    if (!done) err = "bsdtar could not read the cabinets";
    return done > 0;
#endif
}

int arrange(const std::string& unpacked, const std::string& disc, const std::string& out,
            const std::vector<std::string>& knownPaths)
{
    // the checked files by name (lower case) -> their install path; a name in several folders (WORLD1.RBH ...) is
    // placed by its folders too (the cabinets keep STAGEn\WORLD\)
    std::multimap<std::string, std::string> byName;
    for (const std::string& k : knownPaths) byName.emplace(lower(fs::path(k).filename().string()), k);
    auto destFor = [&](std::string rel) -> std::string {
        std::replace(rel.begin(), rel.end(), '\\', '/');
        const std::string l = lower(rel), name = lower(fs::path(rel).filename().string());
        const auto [a, b] = byName.equal_range(name);
        for (auto it = a; it != b; ++it) {
            const std::string kl = lower(it->second);
            if (kl.size() >= l.size() && kl.compare(kl.size() - l.size(), l.size(), l) == 0) return it->second;
        }
        if (std::distance(a, b) == 1 && rel.find('/') == std::string::npos) return a->second;
        // the rest: a stage's folders under ART\ (STAGE1\CREATURE\ ...), a voice / sound under SOUND\, else as it is
        // (the three outside SOUND\ that the files check does not list: the installed game's own folders)
        if (name == "spr.rbh") return "SPRITE/" + rel;
        if (name == "camall.rbh") return "CAM/ALLSTAGE/" + rel;
        if (name == "evtall.rbh") return "EVT/ALLSTAGE/" + rel;
        const std::string top = lower(firstDir(rel));
        if (top.rfind("stage", 0) == 0) return "ART/" + rel;
        if (rel.find('/') == std::string::npos && name.size() > 4 && name.compare(name.size() - 4, 4, ".rbh") == 0)
            return "SOUND/" + rel;
        return rel;
    };
    int placed = 0;
    std::error_code ec;
    const fs::path o = fs::path(out);
    auto place = [&](const fs::path& from, const std::string& rel, bool move) {
        const fs::path to = o / fs::path(destFor(rel));
        if (fs::equivalent(from, to, ec)) { ++placed; return; }
        fs::create_directories(to.parent_path(), ec);
        fs::remove(to, ec);
        if (move) { fs::rename(from, to, ec); if (!ec) { ++placed; return; } }
        ec.clear();
        if (fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec)) ++placed;
    };
    const fs::path u = fs::path(unpacked);
    for (auto it = fs::recursive_directory_iterator(u, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec)) place(it->path(), fs::relative(it->path(), u, ec).generic_string(), true);
    }
    // the CD's loose game files: its ART\ folder (the three VMC files); the rest of the CD is its installer, the
    // cabinets and a copy protection's filler
    if (!disc.empty()) {
        const fs::path d = fs::path(disc);
        for (const auto& top : fs::directory_iterator(d, ec)) {
            if (lower(top.path().filename().string()) != "art" || !top.is_directory(ec)) continue;
            for (auto it = fs::recursive_directory_iterator(top.path(), ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (it->is_regular_file(ec)) place(it->path(), fs::relative(it->path(), d, ec).generic_string(), false);
            }
        }
    }
    return placed;
}

} // namespace hotd::cab
