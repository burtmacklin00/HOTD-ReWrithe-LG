// quest (2026-11-03, docs/quest.md): the Android (Meta Quest) start-up. SDLActivity starts SDL_main with no
// arguments; this gives the game the ones a desktop shortcut would: --root (the game's own data/ folder: fonts, ui,
// lang, vr.ini, copied from the APK's assets into the app's internal folder on the first start of each version),
// --vr openxr (the headset), and the player's game files from the app's external files folder when they are there:
//   /sdcard/Android/data/com.tawhid.hotdrewrithe/files/
//     hotd.zip            the arcade ROMs (zip), or roms/ (a zip in it, or the loose ROM files)
//     pc/                 the 1998 PC game's files (THOTD.EXE in pc/ or one folder down, or a zip of them)
//     settings/           launcher.ini, scores, the log (made by the game)
//     vr/vr.ini           the VR settings (copied there on the first start: edit it with adb pull / push)
// No game data is in the APK. Android only (the file is built on Android alone).
#pragma once

#include <string>
#include <vector>

namespace hotd::android {

std::string externalDir();    // the app's external files folder (no trailing slash; empty: none)
std::string internalDir();    // the app's internal files folder (no trailing slash)

struct GameFiles {
    std::string arcade, pc;   // empty: not found
};
GameFiles findGameFiles();

// the in-VR message's lines when files are missing (where to copy them; the adb push commands)
std::vector<std::string> missingFilesLines(const GameFiles& f);

// SDL_main's arguments with the Quest's defaults added (the given ones win)
std::vector<std::string> startArgs(int argc, char** argv);

} // namespace hotd::android
