// Update check (user 2026-10-31) and auto-updater (setup2, v1.1.0): at start-up one background request to GitHub's
// "latest release" of the project; when its tag is newer than this build's version, the setup window's Update page
// shows the version and its changelog (the release's "body") with Update / Not now (game/launcher.h). Update downloads
// the release's win64 zip with a progress bar, checks it against the release's SHA256SUMS.txt (a mismatch is refused),
// then hands over to bin\hotd_launch.exe --update (src/launch/launch_main.cpp): it waits for this hotd.exe to exit,
// unpacks the zip over the install (settings\ and the player's data\arcade, pc, music, steam are kept) and starts the
// game again. Nothing outside the install folder is written. Options > System > Check for Updates turns it off.
// Windows only (WinHTTP, BCrypt); elsewhere nothing happens.
//
// Testing: HOTD_UPDATE_URL=<a local .json file> is read instead of GitHub's API (also in automated runs); its
// browser_download_url values may be local file paths (copied instead of downloaded).
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace hotd::util {

constexpr const char* kReleasesPage = "https://github.com/burtmacklin00/HOTD-ReWrithe-LG/releases/latest";

// starts the request in the background (once; later calls do nothing)
void updateCheckStart(const char* currentVersion);
// true once the check has found a newer release: its tag ("v1.0.1") in `tag`
bool updateAvailable(std::string& tag);
// "v1.2.3" -> comparable number (major*1e6 + minor*1e3 + patch); -1 when not a version
long long versionNumber(const std::string& v);

struct UpdateInfo {
    std::string tag, body;            // the release's tag and its notes (markdown as written on GitHub)
    std::string zipUrl, zipName;      // the "*-win64.zip" asset
    std::string sumsUrl;              // "SHA256SUMS.txt"
};
// the newer release found (false: none / not yet)
bool updateInfo(UpdateInfo& out);

// the download and its check, on its own thread
class UpdateDownload {
public:
    enum State { Idle, Running, Verified, Failed };
    ~UpdateDownload();
    // into `folder` (made; the install's settings\update)
    void start(const UpdateInfo& info, const std::string& folder);
    State state() const { return state_; }
    float progress() const { return progress_; }   // 0..1
    uint64_t bytes() const { return bytes_; }
    uint64_t total() const { return total_; }
    std::string message() const { std::lock_guard<std::mutex> l(m_); return msg_; }
    std::string zipPath() const { std::lock_guard<std::mutex> l(m_); return zip_; }

private:
    std::thread t_;
    std::atomic<State> state_{Idle};
    std::atomic<float> progress_{0};
    std::atomic<uint64_t> bytes_{0}, total_{0};
    mutable std::mutex m_;
    std::string msg_, zip_;
    void run(UpdateInfo info, std::string folder);
    void fail(const std::string& s);
};

// the hand-over: bin\hotd_launch.exe copied to <root>\settings\update\hotd_update.exe (so every file of bin\ can be
// replaced) and started with --update <zip> --root <root> --pid <this process> [--args <restart arguments>]. The
// caller quits at once after true. msg: why not
bool handOverUpdate(const std::string& root, const std::string& zip, const std::string& restartArgs, std::string& msg);

// the SHA-256 of a file as 64 lower-case hex digits ("" when unreadable; Windows only)
std::string sha256File(const std::string& path);

} // namespace hotd::util
