// launcher.ini (game/launcher.h LaunchConfig): load / save round trips of every setting, older values still read,
// the cheats never saved, a damaged file set aside
#include "check.h"

#include "game/launcher.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using namespace hotd;
using game::LaunchConfig;

namespace {

using Lines = std::vector<std::pair<std::string, std::string>>;

Lines readIni(const std::string& file)
{
    Lines out;
    std::ifstream f(file);
    for (std::string l; std::getline(f, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        const size_t eq = l.find('=');
        if (eq != std::string::npos) out.emplace_back(l.substr(0, eq), l.substr(eq + 1));
    }
    return out;
}

std::map<std::string, std::string> asMap(const Lines& l)
{
    std::map<std::string, std::string> m;
    for (const auto& [k, v] : l) m[k] = v;
    return m;
}

void writeIni(const std::string& file, const Lines& lines)
{
    std::ofstream f(file, std::ios::binary);
    for (const auto& [k, v] : lines) f << k << '=' << v << '\n';
}

std::string slurp(const std::string& file)
{
    std::ifstream f(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool isInt(const std::string& s)
{
    if (s.empty()) return false;
    size_t i = s[0] == '-' ? 1 : 0;
    if (i == s.size()) return false;
    for (; i < s.size(); ++i)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

// the settings whose saved line is derived from another's (written for older builds / readers): changing them alone
// is not expected to round trip, and changing their source changes them too
const std::set<std::string> kDerived = {"startlives", "maxlives", "lives", "displaymode", "fullscreen", "exclusive",
                                        "cursorspeed", "gyrocursorspeed"};
// the lines that keep their value whatever is written: the format versions (padv, bloodv) and the derived ones
const std::set<std::string> kFixed = {"padv", "bloodv", "lives", "fullscreen", "cursorspeed", "gyrocursorspeed"};

} // namespace

TEST(launcher, defaults_round_trip)
{
    LaunchConfig::readOnly = false;
    const std::string dir = test::tempDir("launcher_defaults");
    const std::string a = dir + "/a.ini", b = dir + "/b.ini";
    LaunchConfig d;
    d.save(a);
    CHECK(fs::exists(a));
    CHECK(!fs::exists(a + ".new"));   // (written to .new, then put in place)
    const Lines la = readIni(a);
    CHECK(la.size() > 100);
    // no key twice
    std::set<std::string> keys;
    for (const auto& [k, v] : la) CHECK_MSG(keys.insert(k).second, "twice: " + k);
    LaunchConfig l;
    CHECK(l.load(a));
    l.save(b);
    CHECK_MSG(slurp(a) == slurp(b), "defaults -> load -> save changed the file");
    // a missing file: false, the defaults stay
    LaunchConfig m;
    CHECK(!m.load(dir + "/missing.ini"));
    m.save(b);
    CHECK(slurp(a) == slurp(b));
}

TEST(launcher, every_setting_round_trips)
{
    // each saved key in turn set to other values (a number +-1 and a few small ones; text: a word), loaded, saved: a
    // value the game accepts must come back as written, and no other key may change (save(load(x)) is stable)
    LaunchConfig::readOnly = false;
    const std::string dir = test::tempDir("launcher_each");
    const std::string base = dir + "/base.ini", f1 = dir + "/one.ini", f2 = dir + "/two.ini", f3 = dir + "/three.ini";
    LaunchConfig().save(base);
    const Lines defs = readIni(base);
    const auto defMap = asMap(defs);
    int covered = 0, fixedOnes = 0;
    std::string notCovered;
    const bool verbose = std::getenv("HOTD_UNIT_VERBOSE") != nullptr;
    for (size_t i = 0; i < defs.size(); ++i) {
        const std::string& key = defs[i].first;
        const std::string& v = defs[i].second;
        if (key == "arcade" || key == "pc" || key == "spanishpc") continue;   // (paths: the path test below)
        std::vector<std::string> cand;
        if (isInt(v)) {
            const int n = std::atoi(v.c_str());
            for (int c : {n + 1, n - 1, 0, 2, 60}) cand.push_back(std::to_string(c));
        } else if (v.find(',') != std::string::npos) {
            std::string w = v;
            for (char& ch : w) if (ch >= '0' && ch <= '8') ch = char(ch + 1);
            cand.push_back(w);
        } else {
            for (const char* c : {"1", "test", "0.80", "60", "gl", "unlimited", "auto", "640x480", "frenzy", "FF0000", "tv"})
                cand.push_back(c);
        }
        bool any = false;
        for (const std::string& c : cand) {
            if (c == v) continue;
            Lines one = defs;
            one[i].second = c;
            writeIni(f1, one);
            LaunchConfig l;
            CHECK(l.load(f1));
            l.save(f2);
            const auto got = asMap(readIni(f2));
            auto it = got.find(key);
            const bool back = it != got.end() && it->second == c;
            if (back && !any) {   // stable: loading what was saved saves the same again
                LaunchConfig l2;
                CHECK(l2.load(f2));
                l2.save(f3);
                CHECK_MSG(slurp(f2) == slurp(f3), "not stable after " + key + "=" + c);
            }
            any |= back;
            // no other setting changed (except the derived lines)
            for (const auto& [k2, v2] : got) {
                if (k2 == key || kDerived.count(k2) || kDerived.count(key)) continue;
                auto d = defMap.find(k2);
                if (d != defMap.end() && d->second != v2)
                    CHECK_MSG(false, key + "=" + c + " changed " + k2 + " from " + d->second + " to " + v2);
            }
        }
        if (any) ++covered;
        else {
            ++fixedOnes;
            notCovered += " " + key + "=" + v;
            CHECK_MSG(kFixed.count(key), "a changed value of " + key + " (default " + v + ") never came back");
        }
    }
    if (verbose) std::printf("  launcher: %d settings round trip a changed value; %d keep theirs:%s\n", covered, fixedOnes, notCovered.c_str());
    CHECK(covered > 150);
}

TEST(launcher, paths_relative_to_package)
{
    LaunchConfig::readOnly = false;
    const std::string dir = test::tempDir("launcher_paths");
    LaunchConfig c;
    c.base = dir;
    c.arcade = (fs::path(dir) / "data" / "arcade").string();
    c.pc = (fs::path(dir) / "data" / "pc").string();
    c.save(dir + "/l.ini");
    const auto m = asMap(readIni(dir + "/l.ini"));
    CHECK_MSG(m.at("arcade").find(dir) == std::string::npos, m.at("arcade"));   // (saved relative to the package)
    LaunchConfig b;
    b.base = dir;
    CHECK(b.load(dir + "/l.ini"));
    CHECK_EQ(fs::path(b.arcade).lexically_normal(), fs::path(c.arcade).lexically_normal());
    CHECK_EQ(fs::path(b.pc).lexically_normal(), fs::path(c.pc).lexically_normal());
    // an outside path stays absolute
    LaunchConfig o;
    o.base = dir;
    o.arcade = (fs::temp_directory_path() / "elsewhere" / "hotd.zip").string();
    o.save(dir + "/o.ini");
    LaunchConfig ob;
    ob.base = dir;
    CHECK(ob.load(dir + "/o.ini"));
    CHECK_EQ(fs::path(ob.arcade).lexically_normal(), fs::path(o.arcade).lexically_normal());
}

TEST(launcher, cheats_never_saved)
{
    LaunchConfig::readOnly = false;
    const std::string dir = test::tempDir("launcher_cheats");
    game::cheats().invincible = true;
    game::cheats().infiniteAmmo = true;
    LaunchConfig().save(dir + "/c.ini");
    game::cheats() = game::Cheats{};
    const std::string all = slurp(dir + "/c.ini");
    for (const char* w : {"invinc", "infinite", "cheat", "godmode"})
        CHECK_MSG(all.find(w) == std::string::npos, w);
    // a file with such lines (hand-written) turns nothing on
    {
        std::ofstream(dir + "/h.ini") << "invincible=1\ninfiniteammo=1\ncheats=1\ndifficulty=2\n";
    }
    LaunchConfig h;
    CHECK(h.load(dir + "/h.ini"));
    CHECK(!game::cheats().any());
}

TEST(launcher, damaged_file_set_aside)
{
    LaunchConfig::readOnly = false;
    const std::string dir = test::tempDir("launcher_damaged");
    const std::string f = dir + "/launcher.ini";
    {
        std::ofstream o(f, std::ios::binary);
        o << "difficulty=3\n" << std::string("\0\0\0", 3) << "lives=2\n";
    }
    LaunchConfig l;
    CHECK(!l.load(f));
    CHECK(fs::exists(f + ".damaged"));
    CHECK(!fs::exists(f));
    CHECK_EQ(l.difficulty, LaunchConfig().difficulty);
    // garbage with no setting at all
    {
        std::ofstream(f, std::ios::binary) << "just some words\nno settings\n";
    }
    LaunchConfig g;
    CHECK(!g.load(f));
    // an empty file is fine (no settings, nothing damaged)
    {
        std::ofstream(f, std::ios::binary) << "";
    }
    LaunchConfig e;
    CHECK(e.load(f));
    // CRLF line ends (edited in Notepad) read as well
    {
        std::ofstream(f, std::ios::binary) << "difficulty=4\r\nfreeplay=1\r\n";
    }
    LaunchConfig w;
    CHECK(w.load(f));
    CHECK_EQ(w.difficulty, 4);
    CHECK(w.freePlay);
    // read only (a test run without HOTD_PREF_DIR): nothing written
    LaunchConfig::readOnly = true;
    LaunchConfig().save(dir + "/ro.ini");
    CHECK(!fs::exists(dir + "/ro.ini"));
    LaunchConfig::readOnly = false;
}

TEST(launcher, frame_rates_and_songs)
{
    int n = 0;
    const int* k = game::frameRates(n);
    CHECK(n > 3);
    // the cycle visits every rate once and comes back
    int v = k[0];
    std::set<int> seen;
    for (int i = 0; i < n; ++i) {
        seen.insert(v);
        v = game::nextFrameRate(v);
    }
    CHECK_EQ(int(seen.size()), n);
    CHECK_EQ(v, k[0]);
    CHECK_EQ(game::parseFrameRate("unlimited"), -1);
    CHECK_EQ(game::parseFrameRate("arcade"), game::kFrameRateArcade);
    CHECK_EQ(game::parseFrameRate("144"), 144);
    CHECK_NEAR(game::frameRateHz(game::kFrameRateArcade), 57.524, 0.001);
    CHECK_EQ(game::frameRateHz(0), 0.0);
    CHECK_EQ(game::nextFrameRate(100) > 100, true);   // (an older build's rate: the next one up)
    int ns = 0;
    const game::MenuSong* s = game::menuSongs(ns);
    std::set<int> tracks;
    for (int i = 0; i < ns; ++i) CHECK_MSG(tracks.insert(s[i].track).second, s[i].name);
    int t = s[0].track;
    for (int i = 0; i < ns; ++i) t = game::nextMenuSong(t);
    CHECK_EQ(t, s[0].track);
    CHECK_EQ(game::menuSongOf(s[ns - 1].track).track, s[ns - 1].track);
    // Sonic Mode presets: each key parses back
    for (int p = 0; p < game::kSonicPresets; ++p) CHECK_EQ(game::sonicParse(game::sonicKey(p)), p);
    CHECK_EQ(game::sonicParse("arcade"), 0);
    CHECK_EQ(game::sonicParse("hasty"), 1);
    CHECK_EQ(game::sonicParse("nonsense"), -1);
    CHECK_NEAR(game::shotFlashScaleOf(0), 0.5, 0);
    CHECK_NEAR(game::shotFlashScaleOf(1), 1.0, 0);
    CHECK_NEAR(game::shotFlashScaleOf(9), 1.5, 0);
}
