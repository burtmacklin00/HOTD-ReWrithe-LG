// Update check (user 2026-10-31): at start-up one background request to GitHub's "latest release" of the project;
// when its tag is newer than this build's version, the main menu asks once whether to open the download page. Nothing
// is downloaded or installed by the program; Options > System > Check for Updates turns it off. Windows only (WinHTTP);
// elsewhere it does nothing.
#pragma once

#include <string>

namespace hotd::util {

constexpr const char* kReleasesPage = "https://github.com/burtmacklin00/HOTD-ReWrithe-LG/releases/latest";

// starts the request in the background (once; later calls do nothing)
void updateCheckStart(const char* currentVersion);
// true once the check has found a newer release: its tag ("v1.0.1") in `tag`
bool updateAvailable(std::string& tag);
// "v1.2.3" -> comparable number (major*1e6 + minor*1e3 + patch); -1 when not a version
long long versionNumber(const std::string& v);

} // namespace hotd::util
