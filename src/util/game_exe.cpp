// The package's game exe with the PC game's icon (game_exe.h).
#include "util/game_exe.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

#include "assets/pc_files.h"
#include "util/pe_icon.h"
#include "util/win_shell.h"
#endif

namespace hotd::util {

#ifdef _WIN32
namespace {
uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// an .ico file's images as the exe's icon resources (RT_ICON 1..n and RT_GROUP_ICON 1)
bool setExeIcon(const std::wstring& exe, const std::vector<uint8_t>& ico)
{
    if (ico.size() < 6) return false;
    const int count = ico[4] | ico[5] << 8;
    if (count <= 0 || ico.size() < size_t(6 + 16 * count)) return false;
    HANDLE h = BeginUpdateResourceW(exe.c_str(), FALSE);
    if (!h) return false;
    const WORD lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    std::vector<uint8_t> grp(size_t(6 + 14 * count), 0);
    grp[2] = 1;
    grp[4] = uint8_t(count & 0xFF);
    grp[5] = uint8_t(count >> 8);
    bool ok = true;
    for (int i = 0; i < count && ok; ++i) {
        const uint8_t* e = &ico[size_t(6 + 16 * i)];
        const uint32_t bytes = rd32(e + 8), off = rd32(e + 12);
        if (size_t(off) + bytes > ico.size()) { ok = false; break; }
        ok = UpdateResourceW(h, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(i + 1), lang, const_cast<uint8_t*>(&ico[off]), bytes) != FALSE;
        std::memcpy(&grp[size_t(6 + 14 * i)], e, 12);   // width .. bytes: the same as the file's entry
        grp[size_t(6 + 14 * i + 12)] = uint8_t(i + 1);
    }
    if (ok) ok = UpdateResourceW(h, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(1), lang, grp.data(), DWORD(grp.size())) != FALSE;
    return EndUpdateResourceW(h, ok ? FALSE : TRUE) != FALSE && ok;
}
} // namespace
#endif

bool makeGameExe(const std::string& packageRoot, const std::string& pcDir, std::string& msg)
{
#ifdef _WIN32
    namespace fs = std::filesystem;
    if (packageRoot.empty()) { msg = "not a package"; return false; }
    const fs::path root = fs::u8path(packageRoot), stub = root / "bin" / "hotd_launch.exe", out = root / kGameExeName;
    std::error_code ec;
    if (!fs::exists(stub, ec)) { msg = "no bin\\hotd_launch.exe"; return false; }
    if (fs::exists(out, ec) && fs::last_write_time(out, ec) >= fs::last_write_time(stub, ec)) { msg = "already made"; return true; }
    const fs::path tmp = root / "bin" / "hotd_launch.tmp";
    if (!fs::copy_file(stub, tmp, fs::copy_options::overwrite_existing, ec)) { msg = "cannot copy: " + ec.message(); return false; }
    // the icon: the player's THOTD.EXE (folder or zip), else this program's own
    std::vector<uint8_t> pe, ico;
    bool icon = !pcDir.empty() && hotd::pcfs::readFile(pcDir + "/THOTD.EXE", pe) && peicon::extractIco(std::move(pe), ico);
    if (!icon) icon = peicon::extractIco(std::string(::winshell::selfExePath()), ico);
    if (icon && !setExeIcon(tmp.wstring(), ico)) msg = "(icon not set) ";
    fs::rename(tmp, out, ec);
    if (ec) { fs::remove(tmp, ec); msg += "cannot write " + out.string(); return false; }
    msg += "made " + out.string();
    return true;
#else
    (void)packageRoot;
    (void)pcDir;
    msg = "none here (Windows only; the shortcuts start bin/hotd)";
    return false;
#endif
}

} // namespace hotd::util
