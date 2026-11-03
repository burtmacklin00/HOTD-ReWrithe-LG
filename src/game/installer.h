// setup2 (v1.1.0, user 2026-11-02 "a modern setup"): the install behind the setup's pages (game/launcher.h). An install
// is a package folder (bin\, data\, hotd_package.txt) in a folder the player picks, plus the player's own files
// IMPORTED into its data\ (only the 29 arcade ROM files of MAME's hotd set, the PC files the game reads, and the CD
// music when chosen), "The House of the Dead ReWrithe.exe" with the PC game's icon, the Desktop / Start menu shortcuts,
// and an "Apps & features" entry under HKCU (no admin) whose uninstaller is bin\hotd.exe --uninstall.
// hotd_install.txt in the install's folder lists every file the install wrote (file= program, data= imported,
// shortcut=, regkey=): the uninstaller removes exactly those (never a whole folder, except settings\ when the player
// says yes), then the folders left empty.
//
// Testing: HOTD_SETUP_SANDBOX=<folder>: the shortcuts go to <folder>\Desktop and <folder>\Start Menu and the uninstall
// entry to HKCU\Software\HOTD ReWrithe Test\Uninstall (removed again by the uninstall), never the player's own.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hotd::game {

constexpr const char* kInstallMarker = "hotd_install.txt";

// C:\Games\HOTD ReWrithe (Windows), ~/Games/HOTD ReWrithe elsewhere
std::string defaultInstallDir();
// a folder the player picked: itself when empty, missing or an install already; else <picked>\HOTD ReWrithe
std::string installDirFor(const std::string& picked);
// why the game cannot be installed there ("" = fine): Program Files (needs admin, Windows protects it), the package
// itself, a drive that is not there, a file of that name
std::string installDirProblem(const std::string& dir, const std::string& programRoot);
// free bytes on the drive of `dir` (its nearest existing parent); -1 unknown
int64_t freeBytes(const std::string& dir);
// `root` is an installed copy (hotd_install.txt)
bool isInstall(const std::string& root);
// the CD music tracks (Music\TrackNN.ogg / .mp3) in the PC game's folder or zip
int musicTracks(const std::string& pc);

struct InstallOptions {
    std::string dest;          // the install folder
    std::string programRoot;   // the package the setup runs from (copied from); == dest: Repair (the data only)
    std::string arcade, pc;    // the player's sources (zip / folder; a disc image already copied out to a folder)
    std::string settingsFrom;  // a launcher.ini to carry over (the portable copy's), "" none
    bool desktop = true, startMenu = true, steam = false, music = true;
    std::string version;       // this build's ("v1.1.0")
};
struct InstallResult {
    bool ok = false;
    std::string err;                  // the first problem, for the page
    std::vector<std::string> lines;   // what was done (the log)
    std::string gameExe;              // the exe to start (the game exe, else bin\hotd.exe)
};
// progress(fraction 0..1, a short line); runs on the caller's thread (the setup runs it on its own)
InstallResult installGame(const InstallOptions& o, const std::function<void(float, const std::string&)>& progress);

// bin\hotd.exe --uninstall [--yes] [--remove-settings | --keep-settings]: removes the install `root` (asks first
// unless yes; asks about settings\ unless one of the two options). Returns the exit code (0 removed)
int uninstallGame(const std::string& root, bool yes, int removeSettings /* -1 ask, 0 keep, 1 remove */);

} // namespace hotd::game
