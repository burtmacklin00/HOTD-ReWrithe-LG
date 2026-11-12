// quest (2026-11-03): the Android (Meta Quest) start-up (util/android_app.h, docs/quest.md)
#include "util/android_app.h"

#include "hotd_version.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace hotd::android {

namespace fs = std::filesystem;

namespace {

std::string trimSlash(std::string s)
{
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}

bool isZip(const fs::path& p)
{
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return e == ".zip";
}

std::string firstZip(const fs::path& dir)
{
    std::error_code ec;
    std::vector<std::string> zips;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && isZip(e.path())) zips.push_back(e.path().string());
    std::sort(zips.begin(), zips.end());
    return zips.empty() ? std::string() : zips.front();
}

bool hasExe(const fs::path& dir)
{
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && SDL_strcasecmp(e.path().filename().string().c_str(), "THOTD.EXE") == 0) return true;
    return false;
}

// an APK asset (SDL reads a relative path from the APK's assets on Android) into a file
bool copyAsset(const std::string& asset, const fs::path& to)
{
    size_t size = 0;
    void* data = SDL_LoadFile(asset.c_str(), &size);
    if (!data) return false;
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char*>(data), std::streamsize(size));
    SDL_free(data);
    return bool(out);
}

// the game's own data/ (fonts, ui, lang, typing, mods, vr: not game data) from the APK's assets into the internal
// folder, once per build: assets/data_index.txt lists them (tools/make_quest_apk.ps1 writes it); its text kept beside
// the copy says which build's files are there
std::string unpackData()
{
    const fs::path root = fs::path(internalDir()) / "rewrithe";
    size_t size = 0;
    char* idx = static_cast<char*>(SDL_LoadFile("data_index.txt", &size));
    if (!idx) {
        SDL_Log("quest: the APK has no data_index.txt (assets): the game's data folder is missing");
        return root.string();
    }
    const std::string index = std::string(HOTD_VERSION_TEXT) + "\n" + std::string(idx, size);
    SDL_free(idx);
    std::error_code ec;
    {
        std::ifstream in(root / "data_index.txt", std::ios::binary);
        std::stringstream had;
        had << in.rdbuf();
        if (in && had.str() == index) return root.string();   // (this build's files are there)
    }
    fs::remove_all(root / "data", ec);
    std::istringstream lines(index);
    std::string line;
    std::getline(lines, line);   // (the version)
    int copied = 0, failed = 0;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (copyAsset(line, root / line)) ++copied;
        else ++failed, SDL_Log("quest: asset %s could not be copied", line.c_str());
    }
    std::ofstream(root / "data_index.txt", std::ios::binary | std::ios::trunc) << index;
    SDL_Log("quest: the game's data: %d files from the APK into %s (%d failed)", copied, root.string().c_str(), failed);
    return root.string();
}

} // namespace

std::string externalDir()
{
    const char* p = SDL_GetAndroidExternalStoragePath();
    return p ? trimSlash(p) : std::string();
}

std::string internalDir()
{
    const char* p = SDL_GetAndroidInternalStoragePath();
    return p ? trimSlash(p) : std::string(".");
}

GameFiles findGameFiles()
{
    GameFiles f;
    const std::string ext = externalDir();
    if (ext.empty()) return f;
    std::error_code ec;
    const fs::path base(ext);
    // the arcade ROMs: hotd.zip (any *.zip at the top too), else roms/ (a zip in it, or the loose ROM files)
    if (fs::is_regular_file(base / "hotd.zip", ec)) f.arcade = (base / "hotd.zip").string();
    if (f.arcade.empty()) {   // vrship: no hotd.zip: the only zip at the top under any name (two or more: none chosen)
        int zips = 0;
        for (const auto& e : fs::directory_iterator(base, ec))
            if (e.is_regular_file(ec) && isZip(e.path())) ++zips, f.arcade = e.path().string();
        if (zips != 1) f.arcade.clear();
    }
    if (f.arcade.empty() && fs::is_directory(base / "roms", ec)) {
        f.arcade = firstZip(base / "roms");
        if (f.arcade.empty())
            for (const auto& e : fs::recursive_directory_iterator(base / "roms", ec))
                if (e.is_regular_file(ec) && SDL_strcasecmp(e.path().extension().string().c_str(), ".txt") != 0) {
                    f.arcade = (base / "roms").string();
                    break;
                }
    }
    // the PC game: pc/ with THOTD.EXE, else a folder one down with it (adb push of a folder into an existing pc/), else
    // a zip in pc/
    if (hasExe(base / "pc")) f.pc = (base / "pc").string();
    if (f.pc.empty() && fs::is_directory(base / "pc", ec)) {
        std::vector<std::string> dirs;
        for (const auto& e : fs::directory_iterator(base / "pc", ec))
            if (e.is_directory(ec) && hasExe(e.path())) dirs.push_back(e.path().string());
        std::sort(dirs.begin(), dirs.end());
        if (!dirs.empty()) f.pc = dirs.front();
        if (f.pc.empty()) f.pc = firstZip(base / "pc");
    }
    return f;
}

std::vector<std::string> missingFilesLines(const GameFiles& f)
{
    const std::string ext = externalDir().empty() ? std::string("/sdcard/Android/data/com.tawhid.hotdrewrithe/files")
                                                  : externalDir();
    // vrship (user 2026-11-09): the copy without adb first (a PC's file browser over USB, SideQuest's file manager), the
    // folder as a PC shows it; adb as the other way
    std::string shown = ext;   // (/storage/emulated/0/Android/... -> Android/...: the PC's "Internal shared storage")
    if (const size_t a = shown.find("/Android/"); a != std::string::npos) shown = shown.substr(a + 1);
    std::vector<std::string> l;
    l.push_back("The House of the Dead: ReWrithe needs your own game files.");
    l.push_back("");
    l.push_back(std::string("Arcade ROMs (hotd.zip): ") + (f.arcade.empty() ? "MISSING" : "found"));
    l.push_back(std::string("1998 PC game files (THOTD.EXE): ") + (f.pc.empty() ? "MISSING" : "found"));
    l.push_back("");
    l.push_back("Connect the headset to your PC by USB (in the headset: Allow access to data),");
    l.push_back("then in the PC's file browser (or SideQuest's file manager) open");
    l.push_back("Quest 3 > Internal shared storage > " + shown);
    l.push_back("Copy hotd.zip there, and your PC game's folder into pc");
    l.push_back("");
    l.push_back("Or with adb: adb push hotd.zip " + ext + "/");
    l.push_back("adb push \"C:\\Games\\HOTD\" " + ext + "/pc");
    l.push_back("");
    l.push_back("This page looks for them again every 3 seconds.");
    return l;
}

std::vector<std::string> startArgs(int argc, char** argv)
{
    std::vector<std::string> a;
    for (int i = 0; i < argc; ++i) a.push_back(argv[i] ? argv[i] : "");
    if (a.empty()) a.push_back("hotd");
    auto has = [&](const char* o) { return std::find(a.begin(), a.end(), o) != a.end(); };
    const std::string ext = externalDir();
    std::error_code ec;
    // settings, scores, the log: the external files folder's settings/ (the player can adb pull them)
    if (!ext.empty() && !std::getenv("HOTD_PREF_DIR")) {
        fs::create_directories(fs::path(ext) / "settings", ec);
        SDL_setenv_unsafe("HOTD_PREF_DIR", (ext + "/settings/").c_str(), 1);
    }
    const std::string root = unpackData();
    if (!has("--root")) a.insert(a.end(), {"--root", root});
    // vr.ini where the player can edit it: the external folder's vr/ (seeded with the default once)
    if (!ext.empty() && !fs::exists(fs::path(ext) / "vr" / "vr.ini", ec)) {
        fs::create_directories(fs::path(ext) / "vr", ec);
        fs::copy_file(fs::path(root) / "data" / "vr" / "vr.ini", fs::path(ext) / "vr" / "vr.ini", ec);
    }
    // (testing, questfb2: <external folder>/args.txt, one option per line, adds options to the start: e.g. a
    // headless --audio-wav run to compare the Quest's sound with the PC's byte for byte; "--no-vr": no --vr openxr)
    bool noVr = false;
    if (!ext.empty()) {
        std::ifstream in(fs::path(ext) / "args.txt");
        for (std::string line; std::getline(in, line);) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line == "--no-vr") noVr = true;
            else if (line.rfind("env:", 0) == 0 && line.find('=') != std::string::npos) {   // (env:KEY=VALUE)
                const size_t eq = line.find('=');
                SDL_setenv_unsafe(line.substr(4, eq - 4).c_str(), line.substr(eq + 1).c_str(), 1);
            } else if (!line.empty()) a.push_back(line);
        }
        if (in.is_open()) SDL_Log("quest: args.txt read (%zu options now)", a.size());
    }
    if (!has("--vr") && !noVr) a.insert(a.end(), {"--vr", "openxr"});
    const GameFiles f = findGameFiles();
    if (!has("--arcade") && !f.arcade.empty()) a.insert(a.end(), {"--arcade", f.arcade});
    if (!has("--pc") && !f.pc.empty()) a.insert(a.end(), {"--pc", f.pc});
    SDL_Log("quest: external folder %s; arcade %s; PC %s", ext.c_str(), f.arcade.empty() ? "(missing)" : f.arcade.c_str(),
            f.pc.empty() ? "(missing)" : f.pc.c_str());
    return a;
}

} // namespace hotd::android
