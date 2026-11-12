// Windows shell integration on the player's own machine: the desktop / Start menu shortcut, whose icon points at the
// player's THOTD.EXE (nothing of Sega's is copied or shipped; no executable is ever modified). Linux: a .desktop file; macOS: "not supported".
#pragma once
#include <string>

namespace winshell {

enum class ShortcutResult { Created, Exists, Failed };

// A "The House of the Dead - ReWrithe.lnk" (the old "HOTD ReWrithe.lnk" is replaced) pointing at this hotd.exe (working folder = its folder), IconLocation = <pcDir>\THOTD.EXE,0.
// folder empty: the user's Desktop (and the Start menu programs folder when startMenu); else only `folder` (testing).
// An existing shortcut is left alone (Exists, nothing written) unless overwrite.
ShortcutResult createShortcut(const std::string& folder, bool startMenu, const std::string& pcDir, bool overwrite,
                              std::string& msg);

// setup2 (the installer): a shortcut "<linkFolder>/<name>.lnk" to `target` (working folder = target's folder, icon =
// iconFile or the target). Linux: "<linkFolder>/<name>.desktop" (icon: iconFile when a .png); macOS: Failed. `made`: its path.
// args: the command line after the target ("" none; vr3 2026-11-09: the setup's VR shortcut, "--vr openxr")
ShortcutResult createShortcutTo(const std::string& linkFolder, const std::string& name, const std::string& target,
                                const std::string& iconFile, std::string& made, std::string& msg, const std::string& args = "");
// the user's Desktop and Start menu Programs folders (Linux: ~/Desktop and ~/.local/share/applications; macOS: "")
std::string desktopFolder();
std::string startMenuFolder();
// starts a program (not waited for); args: the command line after the program's quoted name
bool launch(const std::string& exe, const std::string& args, const std::string& workDir);
// runs a program without a window and waits for it (at most `seconds`); its exit code, -1: not started / timed out
// (Linux / macOS: args split as on Windows, "quoted parts" kept whole; no shell)
int runHidden(const std::string& exe, const std::string& args, int seconds);
// a program's full path: a path as it is when it runs, else the first one on PATH ("" when none)
std::string findProgram(const std::string& name);

// this program's own path (Windows: GetModuleFileName; Linux: /proc/self/exe; macOS: _NSGetExecutablePath)
std::string selfExePath();

// setup (2026-10-31): Windows: the window's frame as a fixed dialog (on: no minimize / maximize box, no sizing border;
// off: the minimize box back, the rest is SDL's again). hwnd = SDL's SDL_PROP_WINDOW_WIN32_HWND_POINTER; elsewhere nothing
void dialogFrame(void* hwnd, bool on);

}  // namespace winshell
