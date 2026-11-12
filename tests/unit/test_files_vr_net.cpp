// files: the files check's tables (pc_manifest.inc, pc_manifest_es.inc, the arcade set); vr: the aim math's self test
// and the shake-to-reload detector; net: the crypto known-answer tests
#include "check.h"

#include "game/files_check.h"
#include "net/crypto.h"
#include "vr/vr.h"
#include "vr/vr_aim.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>

using namespace hotd;

namespace {
struct ManifestRow {
    const char* path;
    uint64_t size;
    const char* sha1;
};
const ManifestRow kUs[] = {
#include "game/pc_manifest.inc"
};
const ManifestRow kEs[] = {
#include "game/pc_manifest_es.inc"
};

bool hex40(const char* s)
{
    if (!s || std::strlen(s) != 40) return false;
    for (const char* p = s; *p; ++p)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
    return true;
}
} // namespace

TEST(files, pc_manifests_consistent)
{
    std::map<std::string, const ManifestRow*> us;
    std::set<std::string> hashes;
    for (const ManifestRow& r : kUs) {
        const std::string p = r.path;
        CHECK_MSG(us.emplace(p, &r).second, "twice: " + p);
        CHECK_MSG(r.size > 0, p);
        CHECK_MSG(hex40(r.sha1), p + ": not a lower-case SHA-1");
        CHECK_MSG(p.find('\\') == std::string::npos && p == [&] { std::string u = p; for (char& c : u) c = char(std::toupper(uint8_t(c))); return u; }(),
                  p + ": not an upper-case path with /");
        hashes.insert(r.sha1);
    }
    CHECK(us.count("THOTD.EXE") && us.count("ART/STAGE1/CREATURE/BIGGER.VMC"));
    // the game's own list is this table
    const auto paths = game::pcManifestPaths();
    CHECK_EQ(paths.size(), us.size());
    for (const std::string& p : paths) CHECK_MSG(us.count(p), p);
    // the Spanish release: only files of the US list, each with another hash; the same name twice only for BIGGER.VMC
    // (B_SI.VMC, the "Blood: YES" textures, listed as one too)
    std::map<std::string, int> esCount;
    for (const ManifestRow& r : kEs) {
        const std::string p = r.path;
        ++esCount[p];
        CHECK_MSG(us.count(p), "Spanish file not in the US list: " + p);
        CHECK_MSG(hex40(r.sha1), p);
        CHECK_MSG(r.size > 0, p);
        CHECK_MSG(!hashes.count(r.sha1), p + ": the same hash as a US file (then it need not be listed)");
    }
    for (const auto& [p, n] : esCount) CHECK_MSG(n == 1 || (p == "ART/STAGE1/CREATURE/BIGGER.VMC" && n == 2), p);
    CHECK(esCount.size() > 50);
}

TEST(files, arcade_set)
{
    int n = 0;
    const game::ArcadeRomId* ids = game::arcadeRomIds(n);
    CHECK_EQ(n, 29);   // (MAME hotd, Revision A)
    std::set<std::string> names;
    std::set<uint32_t> crcs;
    for (int i = 0; i < n; ++i) {
        CHECK_MSG(names.insert(ids[i].name).second, ids[i].name);
        CHECK_MSG(crcs.insert(ids[i].crc).second, ids[i].name);
        CHECK_MSG(ids[i].size >= 0x10000 && (ids[i].size & (ids[i].size - 1)) == 0, ids[i].name);
    }
    // Spanish voices: Auto follows the language, Spanish always, English never
    CHECK(game::spanishVoiceOn(0, "es"));
    CHECK(!game::spanishVoiceOn(0, "en"));
    CHECK(game::spanishVoiceOn(1, "en"));
    CHECK(!game::spanishVoiceOn(2, "es"));
    CHECK(game::spanishVoiceDir("", "", "").empty());
    CHECK(!game::isSpanishRelease(test::tempDir("files_empty")));
}

TEST(vr, aim_self_test)
{
    int checks = 0;
    const int failures = vr::selfTest(checks);
    CHECK_EQ(failures, 0);
    CHECK(checks > 10);
    // the screen's centre straight ahead; a point and its inverse agree
    const float pos[3] = {0, 0, 0};
    const vr::GameCamera cam = vr::gameCameraFrom(pos, 0.0f, 0.0f, 496.0f);
    const float up[3] = {0, 1, 0};
    for (const auto& [x, y] : std::vector<std::pair<float, float>>{{0, 0}, {100, 50}, {-240, -180}, {200, -100}}) {
        float w[3];
        vr::arcadeToWorld(cam, x, y, 300.0f, w);
        const float o[3] = {0, 0, 0};
        const vr::AimResult a = vr::aimFromRay(cam, o, w, 1000.0f, up, 60.0f);
        CHECK_MSG(a.onScreen && std::fabs(a.x - x) < 0.05f && std::fabs(a.y - y) < 0.05f,
                  std::to_string(x) + "," + std::to_string(y) + " -> " + std::to_string(a.x) + "," + std::to_string(a.y));
    }
    // pointing away: behind, off screen
    float ahead[3];
    vr::arcadeToWorld(cam, 0, 0, 100.0f, ahead);
    const float o[3] = {0, 0, 0};
    const float back[3] = {-ahead[0], -ahead[1], -ahead[2]};
    const vr::AimResult b = vr::aimFromRay(cam, o, back, 1000.0f, up, 60.0f);
    CHECK(!b.onScreen);
}

TEST(vr, cpu_ray)
{
    // vr4: the CPU ray tests (aim depth on OpenGL ES, the HUD's distance): a placed, turned and scaled box; triangles
    const float o[3] = {0, 0, 0}, d[3] = {0, 0, 1};
    const float lo[3] = {-1, -1, -1}, hi[3] = {1, 1, 1};
    const float ident[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float t = 0;
    const float pos[3] = {0, 0, 10};
    CHECK(vr::rayPlacedBox(o, d, ident, pos, lo, hi, 0.0f, 100.0f, t) && std::fabs(t - 9.0f) < 1e-4f);
    const float scaled[9] = {2, 0, 0, 0, 2, 0, 0, 0, 2};   // (twice as big: its near side at z 8)
    CHECK(vr::rayPlacedBox(o, d, scaled, pos, lo, hi, 0.0f, 100.0f, t) && std::fabs(t - 8.0f) < 1e-4f);
    const float c = std::cos(0.7854f), s = std::sin(0.7854f);   // (turned 45 degrees about y: its corner nearest)
    const float turned[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
    CHECK(vr::rayPlacedBox(o, d, turned, pos, lo, hi, 0.0f, 100.0f, t) && std::fabs(t - (10.0f - std::sqrt(2.0f))) < 1e-3f);
    const float aside[3] = {5, 0, 10};
    CHECK(!vr::rayPlacedBox(o, d, ident, aside, lo, hi, 0.0f, 100.0f, t));
    CHECK(!vr::rayPlacedBox(o, d, ident, pos, lo, hi, 0.0f, 5.0f, t));   // (beyond maxT)
    const float tris[24] = {-1, -1, 4, 1, -1, 4, 0, 1, 4, 0, 0, -1,   // a triangle at z 4, one at z 2 beside the ray
                            3, 3, 2, 4, 3, 2, 3, 4, 2, 0, 0, -1};
    float best = 100.0f;
    CHECK(vr::rayTriangles(o, d, tris, 2, 0.5f, best) && std::fabs(best - 4.0f) < 1e-4f);
    best = 100.0f;
    CHECK(!vr::rayTriangles(o, d, tris, 2, 4.5f, best));   // (nearer than minT)
    // the HUD's distance: nearer quickly, held, then back out slowly
    vr::HudDistance h;
    CHECK_EQ(h.update(3.0f, 0.0f), 3.0f);
    float n = 3.0f;
    for (int i = 0; i < 9; ++i) n = h.update(1.0f, 1.0f / 90.0f);   // 0.1 s
    CHECK(n < 1.2f);
    for (int i = 0; i < 27; ++i) n = h.update(3.0f, 1.0f / 90.0f);   // 0.3 s: still held
    CHECK(n < 1.2f);
    for (int i = 0; i < 180; ++i) n = h.update(3.0f, 1.0f / 90.0f);   // 2 s more: back out
    CHECK(n > 2.9f);
    vr::Settings st;
    CHECK_EQ(st.hudDistance, 1);
    st.set("hud_distance", "fixed");
    CHECK_EQ(st.hudDistance, 0);
}

TEST(vr, quest_display_recentre)
{
    // vr4: the refresh rate asked (auto: 90 on a standalone headset, nothing on a PC; else the nearest offered)
    const float quest3[] = {72, 80, 90, 120}, quest1[] = {60, 72};
    CHECK_EQ(vr::chooseRefreshRate(quest3, 4, 0, true), 90.0f);
    CHECK_EQ(vr::chooseRefreshRate(quest1, 2, 0, true), 72.0f);
    CHECK_EQ(vr::chooseRefreshRate(quest3, 4, 0, false), 0.0f);
    CHECK_EQ(vr::chooseRefreshRate(quest3, 4, 120, false), 120.0f);
    CHECK_EQ(vr::chooseRefreshRate(quest3, 4, 72, true), 72.0f);
    CHECK_EQ(vr::chooseRefreshRate(quest1, 2, 120, true), 72.0f);
    CHECK_EQ(vr::chooseRefreshRate(nullptr, 0, 90, true), 0.0f);
    // the recentre: the head's heading (yaw only) and place; no valid position: the heading, the old place kept
    vr::Pose head, out;
    vr::quatFromYawPitchRoll(0.6f, 0.3f, 0.1f, head.rot);
    head.pos[0] = 0.2f, head.pos[1] = 1.1f, head.pos[2] = -0.4f;
    const float keep[3] = {0.5f, 0.0f, 0.5f};
    CHECK(vr::recentrePose(head, true, true, keep, out));
    CHECK(std::fabs(2.0f * std::atan2(out.rot[1], out.rot[3]) - 0.6f) < 1e-3f && std::fabs(out.rot[0]) < 1e-6f && std::fabs(out.rot[2]) < 1e-6f);
    CHECK(std::fabs(out.pos[1] - 1.1f) < 1e-6f);
    CHECK(vr::recentrePose(head, true, false, keep, out));
    CHECK(std::fabs(out.pos[0] - 0.5f) < 1e-6f && std::fabs(out.pos[2] - 0.5f) < 1e-6f);
    CHECK(std::fabs(2.0f * std::atan2(out.rot[1], out.rot[3]) - 0.6f) < 1e-3f);
    CHECK(!vr::recentrePose(head, false, true, keep, out));
    // away and back: over 3 s once, a short look away none, never with 0
    vr::AwayClock a;
    CHECK(!a.update(true, 0.0, 3.0));
    CHECK(!a.update(false, 1.0, 3.0));
    CHECK(!a.update(true, 2.5, 3.0));    // (1.5 s away)
    CHECK(!a.update(false, 3.0, 3.0));
    CHECK(!a.update(false, 6.0, 3.0));
    CHECK(a.update(true, 6.5, 3.0));     // (3.5 s away)
    CHECK(!a.update(true, 7.0, 3.0));    // (once)
    vr::AwayClock never;
    never.update(true, 0.0, 0.0);
    never.update(false, 1.0, 0.0);
    CHECK(!never.update(true, 100.0, 0.0));
    vr::Settings s;
    s.set("refresh_rate", "120");
    CHECK_EQ(s.refreshRate, 120);
    s.set("refresh_rate", "auto");
    CHECK_EQ(s.refreshRate, 0);
    s.set("auto_recentre", "off");
    CHECK_EQ(s.autoRecentre, 0.0f);
}

TEST(vr, offscreen_reload)
{
    // vr4: offscreen_reload = below (0): beside / above / behind the screen a pull does nothing, below it (and pointing
    // down) it reloads; any (1): every pull off the screen reloads; on the screen always a shot. The 16:9 half width
    // widens the screen and the "below" wedge with it; vr.ini's words map to the policy
    const float pos[3] = {0, 0, 0}, up[3] = {0, 1, 0}, o[3] = {0, 0, 0};
    const vr::GameCamera cam = vr::gameCameraFrom(pos, 0.0f, 0.0f, 496.0f);
    auto at = [&](float x, float y) {
        float w[3];
        vr::arcadeToWorld(cam, x, y, 200.0f, w);
        return vr::aimFromRay(cam, o, w, 200.0f, up, 55.0f);
    };
    using P = vr::Pull;
    CHECK(vr::pullAction(at(0, 0), 248, 192, 0) == P::Shoot);
    CHECK(vr::pullAction(at(-500, 40), 248, 192, 0) == P::Ignore);
    CHECK(vr::pullAction(at(-500, 40), 248, 192, 1) == P::Reload);
    CHECK(vr::pullAction(at(0, -300), 248, 192, 0) == P::Reload);
    CHECK(vr::pullAction(at(300, 0), 248, 192, 0) == P::Ignore);
    vr::AimResult wide = at(300, 0);
    wide.onScreen = std::fabs(wide.x) < 341.0f && std::fabs(wide.y) < 192.0f;   // (16:9)
    CHECK(vr::pullAction(wide, 341, 192, 0) == P::Shoot);
    CHECK(vr::pullAction(at(400, -200), 341, 192, 0) == P::Reload);
    vr::Settings s;
    CHECK_EQ(s.offscreenReload, 0);
    s.set("offscreen_reload", "any");
    CHECK_EQ(s.offscreenReload, 1);
    s.set("offscreen_reload", "below");
    CHECK_EQ(s.offscreenReload, 0);
}

TEST(vr, shake_detector)
{
    // a quick back-and-forth flick (the muzzle 1.5 m/s each way) is one shake; a steady swing, slow aiming and a
    // second flick within the quiet time are none
    auto run = [](const std::function<float(double)>& x, double seconds, float threshold) {
        vr::ShakeDetector d;
        int shakes = 0;
        for (int i = 0; i <= int(seconds * 90); ++i) {   // (90 Hz: a headset's rate)
            const double t = i / 90.0;
            vr::Pose p;
            p.valid = true;
            p.pos[0] = x(t);
            shakes += d.update(p, int64_t(t * 1e9), threshold);
        }
        return shakes;
    };
    const double pi = 3.14159265358979;
    // a flick: 4 cm amplitude at 6 Hz (peak speed 1.5 m/s), for 0.25 s
    CHECK_EQ(run([&](double t) { return t < 0.25 ? float(0.04 * std::sin(2 * pi * 6 * t)) : 0.0f; }, 1.0, 1.0f), 1);
    // a steady swing one way at 2 m/s: none
    CHECK_EQ(run([](double t) { return float(2.0 * t); }, 0.5, 1.0f), 0);
    // slow aiming (0.3 m/s back and forth): none
    CHECK_EQ(run([&](double t) { return float(0.05 * std::sin(2 * pi * 1 * t)); }, 2.0, 1.0f), 0);
    // shaking for 0.5 s: one reload (the quiet time), then again after it
    CHECK_EQ(run([&](double t) { return t < 0.5 ? float(0.04 * std::sin(2 * pi * 6 * t)) : 0.0f; }, 1.0, 1.0f), 1);
    CHECK(run([&](double t) { return float(0.04 * std::sin(2 * pi * 6 * t)); }, 2.0, 1.0f) >= 2);
    // a higher threshold (in g since vrgest; the flick peaks near 5.8 g): the same flick is not enough
    CHECK_EQ(run([&](double t) { return t < 0.25 ? float(0.04 * std::sin(2 * pi * 6 * t)) : 0.0f; }, 1.0, 8.0f), 0);
}

namespace {
int gCryptoChecks = 0;
void cryptoReport(bool ok, const char* what)
{
    ++gCryptoChecks;
    CHECK_MSG(ok, what);
}
} // namespace

TEST(net, crypto_known_answers)
{
    gCryptoChecks = 0;
    CHECK_EQ(net::crypto::selfTest(cryptoReport), 0);
    CHECK(gCryptoChecks >= 4);
}
