// Update check: GitHub's REST API, GET /repos/<owner>/<repo>/releases/latest (update_check.h): "tag_name", "body" and
// the assets' "browser_download_url"; the download (WinHTTP, following GitHub's redirect to its file host), the
// SHA-256 check (BCrypt) and the hand-over to hotd_launch.exe --update.
#include "util/update_check.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#endif

namespace hotd::util {

namespace {
namespace fs = std::filesystem;

std::atomic<bool> gStarted{false};
std::atomic<bool> gNewer{false};
std::mutex gMutex;
UpdateInfo gInfo;

const char* testUrl()
{
    const char* u = std::getenv("HOTD_UPDATE_URL");
    return u && *u ? u : nullptr;
}

// a JSON string value starting at the opening quote `q`: unescaped (\n \" \\ \/ \t \uXXXX as UTF-8)
std::string jsonString(const std::string& s, size_t q, size_t* end = nullptr)
{
    std::string out;
    size_t i = q + 1;
    for (; i < s.size() && s[i] != '"'; ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { out += s[i]; continue; }
        const char c = s[++i];
        if (c == 'n') out += '\n';
        else if (c == 't') out += '\t';
        else if (c == 'r') {}
        else if (c == 'u' && i + 4 < s.size()) {
            const unsigned cp = unsigned(std::strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16));
            i += 4;
            if (cp < 0x80) out += char(cp);
            else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
            else { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        } else out += c;
    }
    if (end) *end = i;
    return out;
}

// the first "key": "value" at or after `from`
std::string jsonField(const std::string& s, const std::string& key, size_t from = 0, size_t* at = nullptr)
{
    const size_t k = s.find("\"" + key + "\"", from);
    if (k == std::string::npos) return {};
    const size_t colon = s.find(':', k);
    const size_t q = colon == std::string::npos ? colon : s.find_first_not_of(" \t\r\n", colon + 1);
    if (q == std::string::npos || s[q] != '"') return {};
    if (at) *at = k;
    return jsonString(s, q);
}

#ifdef _WIN32
std::wstring wide(const std::string& s)
{
    if (s.empty()) return {};
    std::wstring w(size_t(MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), int(w.size()));
    return w;
}

// GET an https URL; each block to `sink` (false: stop). total: Content-Length (0 unknown). "" ok, else the problem
std::string httpGet(const std::string& url, const std::function<bool(const char*, size_t)>& sink, uint64_t* total, bool api)
{
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host, uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path, uc.dwUrlPathLength = 2048;
    const std::wstring wurl = wide(url);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return "not an https address";
    HINTERNET s = WinHttpOpen(L"HOTD-ReWrithe-updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return "no internet access (WinHTTP)";
    if (api) WinHttpSetTimeouts(s, 3000, 3000, 3000, 5000);
    else WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
    std::string problem = "the server did not answer";
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                    : nullptr;
    if (r && WinHttpSendRequest(r, api ? L"Accept: application/vnd.github+json\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS, DWORD(-1),
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, nullptr)) {
        DWORD status = 0, len = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len,
                            WINHTTP_NO_HEADER_INDEX);
        if (total) {
            wchar_t cl[32] = {};
            DWORD n = sizeof cl;
            *total = WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, cl, &n, WINHTTP_NO_HEADER_INDEX)
                         ? std::wcstoull(cl, nullptr, 10) : 0;
        }
        if (status == 200) {
            problem.clear();
            std::vector<char> buf(1 << 16);
            for (;;) {
                DWORD avail = 0, got = 0;
                if (!WinHttpQueryDataAvailable(r, &avail)) { problem = "the download broke off"; break; }
                if (avail == 0) break;
                if (!WinHttpReadData(r, buf.data(), DWORD(std::min<size_t>(avail, buf.size())), &got) || got == 0) {
                    problem = "the download broke off";
                    break;
                }
                if (!sink(buf.data(), got)) { problem = "stopped"; break; }
            }
        } else {
            problem = "the server answered " + std::to_string(status);
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return problem;
}
#endif

// the release's JSON: GitHub's API, or the test's local file
std::string fetchLatest()
{
    if (const char* t = testUrl()) {
        std::ifstream f(t, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
#ifdef _WIN32
    std::string body;
    httpGet("https://api.github.com/repos/burtmacklin00/HOTD-ReWrithe-LG/releases/latest"   /* Light Gun Edition: its own releases */,
            [&](const char* d, size_t n) { body.append(d, n); return body.size() < (2u << 20); }, nullptr, true);
    return body;
#else
    return {};
#endif
}

// one URL (https, or a local file in the test) into `out`; progress per block
std::string fetchTo(const std::string& url, const fs::path& out, const std::function<void(uint64_t, uint64_t)>& progress)
{
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) return "cannot write " + out.string();
    uint64_t got = 0, total = 0;
    std::string problem;
    if (url.rfind("https://", 0) != 0) {   // (HOTD_UPDATE_URL's local release: a file path)
        if (!testUrl()) return "not an https address";
        std::error_code ec;
        std::string p = url.rfind("file:///", 0) == 0 ? url.substr(8) : url;
        total = fs::file_size(p, ec);
        std::ifstream in(p, std::ios::binary);
        if (!in) return "cannot read " + p;
        std::vector<char> buf(1 << 16);
        while (in) {
            in.read(buf.data(), std::streamsize(buf.size()));
            const size_t n = size_t(in.gcount());
            if (!n) break;
            f.write(buf.data(), std::streamsize(n));
            got += n;
            if (progress) progress(got, total);
        }
    } else {
#ifdef _WIN32
        problem = httpGet(url, [&](const char* d, size_t n) {
            f.write(d, std::streamsize(n));
            got += n;
            if (progress) progress(got, total);
            return bool(f);
        }, &total, false);
#else
        problem = "Windows only";
#endif
    }
    f.close();
    if (problem.empty() && !f) problem = "cannot write " + out.string() + " (disk full?)";
    return problem;
}

} // namespace

long long versionNumber(const std::string& v)
{
    size_t i = (!v.empty() && (v[0] == 'v' || v[0] == 'V')) ? 1 : 0;
    long long parts[3] = {0, 0, 0};
    for (int k = 0; k < 3; ++k) {
        if (i >= v.size() || v[i] < '0' || v[i] > '9') return k == 0 ? -1 : parts[0] * 1000000 + parts[1] * 1000 + parts[2];
        while (i < v.size() && v[i] >= '0' && v[i] <= '9') parts[k] = parts[k] * 10 + (v[i++] - '0');
        if (i < v.size() && v[i] == '.') ++i; else break;
    }
    return parts[0] * 1000000 + parts[1] * 1000 + parts[2];
}

void updateCheckStart(const char* currentVersion)
{
    if (gStarted.exchange(true)) return;
#ifdef _WIN32
    std::string cur = currentVersion ? currentVersion : "";
    std::thread([cur]() {
        const std::string body = fetchLatest();
        UpdateInfo info;
        info.tag = jsonField(body, "tag_name");
        if (info.tag.empty()) return;
        info.body = jsonField(body, "body");
        // the assets: each "name" with its "browser_download_url"
        for (size_t at = body.find("\"assets\""); at != std::string::npos;) {
            size_t k = std::string::npos;
            const std::string url = jsonField(body, "browser_download_url", at + 1, &k);
            if (url.empty()) break;
            at = k;
            const std::string name = url.substr(url.find_last_of("/\\") + 1);
            if (name.size() > 10 && name.compare(name.size() - 10, 10, "-win64.zip") == 0) info.zipUrl = url, info.zipName = name;
            else if (name == "SHA256SUMS.txt") info.sumsUrl = url;
        }
        const long long latest = versionNumber(info.tag), mine = versionNumber(cur);
        std::printf("update check: latest %s (this %s), zip %s, sums %s\n", info.tag.c_str(), cur.c_str(),
                    info.zipName.empty() ? "-" : info.zipName.c_str(), info.sumsUrl.empty() ? "-" : "yes");
        std::fflush(stdout);
        if (latest > 0 && mine > 0 && latest > mine) {
            std::lock_guard<std::mutex> lock(gMutex);
            gInfo = info;
            gNewer = true;
        }
    }).detach();
#else
    (void)currentVersion;
#endif
}

bool updateAvailable(std::string& tag)
{
    if (!gNewer) return false;
    std::lock_guard<std::mutex> lock(gMutex);
    tag = gInfo.tag;
    return true;
}

bool updateInfo(UpdateInfo& out)
{
    if (!gNewer) return false;
    std::lock_guard<std::mutex> lock(gMutex);
    out = gInfo;
    return true;
}

std::string sha256File(const std::string& path)
{
#ifdef _WIN32
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    std::string hex;
    std::ifstream in(path, std::ios::binary);
    if (in && BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<char> buf(1 << 16);
        bool ok = true;
        while (in && ok) {
            in.read(buf.data(), std::streamsize(buf.size()));
            const ULONG n = ULONG(in.gcount());
            if (n) ok = BCryptHashData(h, reinterpret_cast<PUCHAR>(buf.data()), n, 0) == 0;
        }
        UCHAR d[32];
        if (ok && BCryptFinishHash(h, d, sizeof d, 0) == 0) {
            char b[3];
            for (UCHAR c : d) { std::snprintf(b, sizeof b, "%02x", c); hex += b; }
        }
        BCryptDestroyHash(h);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return hex;
#else
    (void)path;
    return {};
#endif
}

UpdateDownload::~UpdateDownload()
{
    if (t_.joinable()) t_.join();
}

void UpdateDownload::start(const UpdateInfo& info, const std::string& folder)
{
    if (state_ == Running) return;
    if (t_.joinable()) t_.join();
    state_ = Running;
    progress_ = 0;
    bytes_ = total_ = 0;
    t_ = std::thread(&UpdateDownload::run, this, info, folder);
}

void UpdateDownload::fail(const std::string& s)
{
    std::lock_guard<std::mutex> l(m_);
    msg_ = s;
    state_ = Failed;
}

void UpdateDownload::run(UpdateInfo info, std::string folder)
{
    if (info.zipUrl.empty() || info.sumsUrl.empty()) {
        fail("This release has no Windows zip or no SHA256SUMS.txt: update by hand from the download page.");
        return;
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    const fs::path zip = fs::path(folder) / info.zipName, sums = fs::path(folder) / "SHA256SUMS.txt";
    const fs::path part = zip.string() + ".part";
    {
        std::lock_guard<std::mutex> l(m_);
        msg_ = "Downloading " + info.zipName + " ...";
    }
    std::string p = fetchTo(info.zipUrl, part, [this](uint64_t got, uint64_t total) {
        bytes_ = got;
        total_ = total;
        progress_ = total ? float(double(got) / double(total)) : 0.0f;
    });
    if (!p.empty()) { fs::remove(part, ec); fail("The download failed: " + p + "."); return; }
    p = fetchTo(info.sumsUrl, sums, {});
    if (!p.empty()) { fs::remove(part, ec); fail("SHA256SUMS.txt could not be downloaded: " + p + "."); return; }
    // the zip's line: "<64 hex>  <name>"
    std::string want;
    {
        std::ifstream f(sums);
        for (std::string line; std::getline(f, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t sp = line.find(' ');
            if (sp == 64 && line.size() > sp && line.substr(line.find_first_not_of(" *", sp)) == info.zipName) want = line.substr(0, 64);
        }
    }
    {
        std::lock_guard<std::mutex> l(m_);
        msg_ = "Checking the download (SHA-256) ...";
    }
    std::string have = sha256File(part.string());
    for (char& c : want) c = char(std::tolower(uint8_t(c)));
    if (want.empty() || have.empty() || want != have) {
        fs::remove(part, ec);
        fail(want.empty() ? "SHA256SUMS.txt has no line for " + info.zipName + ": the download was not installed."
                          : "The download does not match its SHA-256 sum (damaged or changed on the way): it was deleted, nothing was installed.");
        return;
    }
    fs::remove(zip, ec);
    fs::rename(part, zip, ec);
    if (ec) { fail("Cannot keep the download: " + ec.message()); return; }
    {
        std::lock_guard<std::mutex> l(m_);
        zip_ = zip.string();
        msg_ = "Downloaded and checked.";
    }
    progress_ = 1.0f;
    state_ = Verified;
}

bool handOverUpdate(const std::string& root, const std::string& zip, const std::string& restartArgs, std::string& msg)
{
#ifdef _WIN32
    std::error_code ec;
    fs::path r = fs::path(root).lexically_normal().make_preferred();
    if (r.filename().empty()) r = r.parent_path();   // (no trailing separator: "...\\" before a quote would escape it)
    const fs::path stub = r / "bin" / "hotd_launch.exe", dir = r / "settings" / "update", updater = dir / "hotd_update.exe";
    if (!fs::exists(r / "hotd_package.txt", ec) || !fs::exists(stub, ec)) { msg = "not a package folder (no bin\\hotd_launch.exe)"; return false; }
    fs::create_directories(dir, ec);
    if (!fs::copy_file(stub, updater, fs::copy_options::overwrite_existing, ec)) { msg = "cannot copy the updater: " + ec.message(); return false; }
    std::wstring cmd = L"\"" + updater.wstring() + L"\" --update \"" + fs::path(zip).lexically_normal().make_preferred().wstring() + L"\" --root \"" + r.wstring() + L"\" --pid " +
                       std::to_wstring(GetCurrentProcessId());
    if (!restartArgs.empty()) cmd += L" --args " + wide(restartArgs);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(updater.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        msg = "cannot start the updater";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    msg = "the updater runs";
    return true;
#else
    (void)root, (void)zip, (void)restartArgs;
    msg = "Windows only";
    return false;
#endif
}

} // namespace hotd::util
