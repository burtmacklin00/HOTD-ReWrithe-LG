// Update check: GitHub's REST API, GET /repos/<owner>/<repo>/releases/latest, the "tag_name" field (update_check.h).
#include "util/update_check.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace hotd::util {

namespace {
std::atomic<bool> gStarted{false};
std::atomic<bool> gNewer{false};
std::mutex gMutex;
std::string gTag;

#ifdef _WIN32
// the response body of the API request ("" on any failure; short timeouts, no retries)
std::string fetchLatest()
{
    std::string body;
    HINTERNET s = WinHttpOpen(L"HOTD-ReWrithe-update-check", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return body;
    WinHttpSetTimeouts(s, 3000, 3000, 3000, 5000);
    HINTERNET c = WinHttpConnect(s, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", L"/repos/burtmacklin00/HOTD-ReWrithe-LG/releases/latest",
                                         nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                    : nullptr;
    if (r && WinHttpSendRequest(r, L"Accept: application/vnd.github+json\r\n", DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, nullptr)) {
        DWORD status = 0, len = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                            &len, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(r, &avail) || avail == 0 || body.size() > (1u << 20)) break;
                std::string chunk(avail, '\0');
                DWORD got = 0;
                if (!WinHttpReadData(r, chunk.data(), avail, &got) || got == 0) break;
                body.append(chunk.data(), got);
            }
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return body;
}
#endif
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
        const size_t k = body.find("\"tag_name\"");
        if (k == std::string::npos) return;
        const size_t q1 = body.find('"', body.find(':', k) + 1);
        const size_t q2 = q1 == std::string::npos ? q1 : body.find('"', q1 + 1);
        if (q2 == std::string::npos) return;
        const std::string tag = body.substr(q1 + 1, q2 - q1 - 1);
        const long long latest = versionNumber(tag), mine = versionNumber(cur);
        if (latest > 0 && mine > 0 && latest > mine) {
            std::lock_guard<std::mutex> lock(gMutex);
            gTag = tag;
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
    tag = gTag;
    return true;
}

} // namespace hotd::util
