// The Jukebox's arcade lengths (jukebox.h ArcadeLength): the arcade sound board says nothing about its sounds, so each
// code is played once on a board of its own (src/audio/sound_board.h, the player's own ROM set) as fast as it runs,
// muted, and its output looked at in 10 ms steps:
//   the end:  the output stays below the quiet level (the Jukebox's own, 1e-4 peak) for 4 s (music) / 2 s (the others);
//             the length is up to the last sound;
//   a loop:   the output repeats itself: its level (the RMS of each side over 50 ms, every 10 ms) compared with the
//             same a lag L earlier, from the end backwards; the smallest L whose repeat holds for a whole round, a
//             margin and 150 s at least (s) is the loop (or the multiple of it that fits every hop: a song whose halves
//             nearly match), where the repeat starts the intro's end. (The samples themselves do not repeat exactly:
//             the SCSP's free-running LFOs; the level does, 99 % + of 10 ms steps alike.)
// Nothing is stored in the repository: the table is made on the player's PC from their ROMs (a cache file in the
// settings folder, keyed by a hash of the ROMs; Jukebox::measureArcadeFile, run as `hotd --jukebox-lengths` by the
// menu's Jukebox in a process of its own: one board per process).
#include "game/jukebox.h"

#ifdef HOTD_HAVE_ARCADE_SOUND
#include "audio/sound_board.h"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#ifdef HOTD_HAVE_ARCADE_SOUND
extern int hotd_scsp_fix;   // src/audio/sound_board.cpp
#endif

namespace hotd::game {

namespace {

constexpr int kHop = 441;               // 10 ms
constexpr int kWin = 5;                 // the level's window: 5 hops = 50 ms
constexpr float kQuietPeak = 1.0e-4f;   // the Jukebox's quiet level (jukebox.cpp kQuiet)
constexpr int kVersion = 2;             // the cache's format / method (a new one measures again; 2: sound2's SCSP fixes)

struct Level { float l, r; };   // the RMS of each side over the last 50 ms

bool near(const Level& a, const Level& b)
{
    auto n = [](float x, float y) { return std::fabs(x - y) <= 0.25f * std::max(x, y) + 1e-4f; };
    return n(a.l, b.l) && n(a.r, b.r);
}

// the output's mono samples (L + R) and their levels per hop, and the tests on them
struct Take {
    std::vector<Level> lv;    // per hop
    // the levels of the last `span` hops look the same L hops earlier (3 in 4 at least)
    bool looksRepeated(int L, int span) const
    {
        const int n = int(lv.size());
        int miss = 0;
        for (int k = n - span; k < n; ++k)
            if (!near(lv[size_t(k)], lv[size_t(k - L)]) && ++miss > span / 4)
                return false;
        return true;
    }
    // how many hops back from the end the levels repeat L hops earlier: 1 s chunks with 85 of their 100 hops alike,
    // then hop by hop into the chunk before (the place where the repeat starts)
    int matched(int L) const
    {
        const int n = int(lv.size());
        int good = n;
        for (int c = n - 100; c - L >= 0; c -= 100) {
            int same = 0;
            for (int k = c; k < c + 100; ++k)
                same += near(lv[size_t(k)], lv[size_t(k - L)]);
            if (same < 85)
                break;
            good = c;
        }
        for (int k = good - 1, miss = 0; k - L >= 0; --k) {
            if (near(lv[size_t(k)], lv[size_t(k - L)])) {
                miss = 0;
                good = k;
            } else if (++miss > 3)
                break;
        }
        return n - good;
    }
};

} // namespace

std::string Jukebox::ArcadeLength::text() const
{
    auto mmss = [](double s) {
        char b[24];
        const int t = int(s + 0.5);
        std::snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
        return std::string(b);
    };
    char b[96];
    if (!measured)
        return "not measured";
    if (loops && loop > 0)
        std::snprintf(b, sizeof b, "loops: intro %.2f s + loop %.2f s (%s)", intro, loop, mmss(loop).c_str());
    else if (loops)
        std::snprintf(b, sizeof b, "goes on (no repeat found in %.0f s)", length);
    else if (length > 0)
        std::snprintf(b, sizeof b, "%.2f s, once", length);
    else
        std::snprintf(b, sizeof b, "no sound");
    return b;
}

std::vector<std::pair<uint32_t, bool>> Jukebox::arcadeCodes()
{
    std::vector<std::pair<uint32_t, bool>> music, other;
    for (const auto& [code, cue] : cueMap())
        if (code >= 0xA01000 && code <= 0xA014FF)
            (code <= 0xA0100E ? music : other).push_back({code, code <= 0xA0100E});
    std::sort(music.begin(), music.end());
    std::sort(other.begin(), other.end());
    music.erase(std::unique(music.begin(), music.end()), music.end());
    other.erase(std::unique(other.begin(), other.end()), other.end());
    music.insert(music.end(), other.begin(), other.end());
    return music;
}

#ifdef HOTD_HAVE_ARCADE_SOUND
Jukebox::ArcadeLength Jukebox::measureArcade(audio::SoundBoard& board, uint32_t code, bool music, std::string* diag)
{
    ArcadeLength r;
    r.measured = true;
    board.reset();
    std::vector<float> buf(size_t(kHop) * 2);
    for (int i = 0; i < 25; ++i) {   // the board's start-up (jukebox.cpp kBoardBoot, 0.25 s)
        std::fill(buf.begin(), buf.end(), 0.0f);
        board.render(buf.data(), kHop, 1.0f);
    }
    board.command(code);
    const int quietHops = music ? 400 : 200;           // the end: 4 s / 2 s of quiet
    const char* capEnv = std::getenv("HOTD_JUKEBOX_CAP");   // (debugging: another cap, seconds)
    const int capHops = capEnv ? std::atoi(capEnv) * 100 : music ? 60000 : 6000;   // 10 min / 1 min
    const int minL = music ? 400 : 10;                 // a loop of at least 4 s / 0.1 s
    const int margin = music ? 800 : 100;              // and its repeat holding 8 s / 1 s more than a round,
    // and 150 s / 3 s at least (a phrase played over and over inside a song is no loop: Chapter 3's 9 s one held 45 s)
    const char* heldEnv = std::getenv("HOTD_JUKEBOX_HELD");   // (debugging: another, seconds)
    const int minHeld = heldEnv ? std::atoi(heldEnv) * 100 : music ? 15000 : 300;
    Take tk;
    std::vector<double> l2, r2;                        // per hop: the sums of squares
    int lastLoud = -1;
    for (int t = 0; t < capHops; ++t) {
        std::fill(buf.begin(), buf.end(), 0.0f);
        board.render(buf.data(), kHop, 1.0f);
        double sl = 0, sr = 0;
        float peak = 0;
        for (int i = 0; i < kHop; ++i) {
            const float a = buf[size_t(i) * 2], b = buf[size_t(i) * 2 + 1];
            sl += double(a) * a;
            sr += double(b) * b;
            peak = std::max(peak, std::max(std::fabs(a), std::fabs(b)));
        }
        l2.push_back(sl);
        r2.push_back(sr);
        double wl = 0, wr = 0;
        for (int k = std::max(0, t - kWin + 1); k <= t; ++k) {
            wl += l2[size_t(k)];
            wr += r2[size_t(k)];
        }
        tk.lv.push_back({float(std::sqrt(wl / (kWin * kHop))), float(std::sqrt(wr / (kWin * kHop)))});
        if (peak >= kQuietPeak)
            lastLoud = t;
        const int n = t + 1;
        if (lastLoud < 0 && n >= 300) {   // nothing in 3 s: a code without a sound
            r.length = 0;
            return r;
        }
        if (lastLoud >= 0 && n - 1 - lastLoud >= quietHops) {   // it ended
            r.length = (lastLoud + 1) * 0.01;
            return r;
        }
        // a repeat: looked for every second once a loop and its margin fit twice; the last 8 s (1 s) first (cheap),
        // then how far back it holds: a whole round and the margin
        if (n % 100 != 0 || n < 2 * minL + margin || lastLoud < n - 100)
            continue;
        for (int L = minL; 2 * L + margin <= n; ++L) {
            if (!tk.looksRepeated(L, margin))
                continue;
            int m = tk.matched(L);
            if (m < std::max(L + margin, minHeld))
                continue;
            // the lag that fits best: the levels are in 10 ms steps (the next 15 tried); and a loop of a few bars that
            // nearly repeats each half is the whole of it (the name entry's: 1 hop in 25 off at 6.85 s, 1 in 1000 at
            // 13.68 s): the smallest multiple that fits (almost) every hop
            auto fit = [&](int lag) {   // the share of the last (up to) 40 s alike one lag earlier
                const int w = std::min(4000, n - lag);
                if (w < std::min(500, margin))
                    return 0.0;
                int same = 0;
                for (int k = n - w; k < n; ++k)
                    same += near(tk.lv[size_t(k)], tk.lv[size_t(k - lag)]);
                return double(same) / w;
            };
            auto bestNear = [&](int from, int to, double& f) {
                int b = from;
                f = -1;
                for (int lag = from; lag <= to; ++lag)
                    if (const double q = fit(lag); q > f) {
                        f = q;
                        b = lag;
                    }
                return b;
            };
            double f1 = 0;
            int bestL = bestNear(L, L + 15, f1);
            for (int k = 2; f1 < 0.99 && k <= 8 && k * bestL + 2 * k + 500 <= n; ++k) {
                double fk = 0;
                const int lk = bestNear(k * bestL - 2 * k, k * bestL + 2 * k, fk);
                if ((fk >= 0.99 || fk >= 1 - (1 - f1) / 4) && tk.matched(lk) >= lk + margin) {
                    bestL = lk;
                    f1 = fk;
                    break;
                }
            }
            const int mb = tk.matched(bestL);
            m = bestL <= L + 15 ? std::max(m, mb) : mb;
            const int start = n - m;   // the first hop whose level repeats one loop earlier
            r.loops = true;
            r.loop = bestL * 0.01;
            r.intro = std::max(0, start - bestL) * 0.01;
            r.length = r.intro + r.loop;
            if (diag) {
                char b[160];
                std::snprintf(b, sizeof b, "repeat held %.2f s, %.1f %% of the last 40 s alike (first lag %.2f s; taken %.0f s)", m * 0.01,
                              f1 * 100, L * 0.01, n * 0.01);
                *diag = b;
            }
            return r;
        }
    }
    r.loops = true;   // still going at the cap, no repeat found
    r.length = capHops * 0.01;
    if (diag && std::getenv("HOTD_JUKEBOX_DIAG")) {   // (debugging: the lags whose levels came closest)
        const int n = int(tk.lv.size());
        std::vector<std::pair<double, int>> best;
        for (int L = minL; L <= n / 2; ++L) {
            int same = 0, cnt = 0;
            for (int k = std::max(L, n - 6000); k < n; ++k, ++cnt)
                same += near(tk.lv[size_t(k)], tk.lv[size_t(k - L)]);
            best.push_back({cnt ? double(same) / cnt : 0, L});
        }
        std::sort(best.rbegin(), best.rend());
        char b[128];
        for (size_t i = 0; i < std::min<size_t>(3, best.size()); ++i) {
            std::snprintf(b, sizeof b, "%sL %.2f s levels %.3f held %.2f s", i ? "; " : "closest: ", best[i].second * 0.01,
                          best[i].first, tk.matched(best[i].second) * 0.01);
            *diag += b;
        }
    }
    return r;
}
#endif

std::string Jukebox::romKey(const std::vector<uint8_t>& prog, const std::vector<uint8_t>& samples)
{
    uint64_t h = 1469598103934665603ull;
    for (const auto* v : {&prog, &samples})
        for (uint8_t b : *v) {
            h ^= b;
            h *= 1099511628211ull;
        }
    char b[48];
#ifdef HOTD_HAVE_ARCADE_SOUND
    // sound2: the SCSP fixes in use (HOTD_SCSP_MAME turns some off; the timers change the songs' lengths)
    std::snprintf(b, sizeof b, "v%d-%016llx-f%d", kVersion, (unsigned long long)h, hotd_scsp_fix);
#else
    std::snprintf(b, sizeof b, "v%d-%016llx", kVersion, (unsigned long long)h);
#endif
    return b;
}

std::map<uint32_t, Jukebox::ArcadeLength> Jukebox::readArcadeLengths(const std::string& file, std::string* key, bool* complete)
{
    std::map<uint32_t, ArcadeLength> out;
    if (complete) *complete = false;
    std::ifstream in(file);
    std::string line;
    if (!std::getline(in, line) || line.rfind("hotd-jukebox-arcade-lengths ", 0) != 0)
        return out;
    if (key) *key = line.substr(28);
    while (std::getline(in, line)) {
        if (line == "end") {
            if (complete) *complete = true;
            break;
        }
        unsigned code = 0;
        double len = 0, intro = 0, loop = 0;
        int loops = 0;
        if (std::sscanf(line.c_str(), "%x %lf %lf %lf %d", &code, &len, &intro, &loop, &loops) == 5) {
            ArcadeLength a;
            a.measured = true;
            a.length = len;
            a.intro = intro;
            a.loop = loop;
            a.loops = loops != 0;
            out[code] = a;
        }
    }
    return out;
}

int Jukebox::measureArcadeFile(const std::vector<uint8_t>& prog, const std::vector<uint8_t>& samples, const std::string& file,
                               int part, int parts,
                               const std::function<void(uint32_t, const ArcadeLength&, const std::string&)>& each)
{
#ifdef HOTD_HAVE_ARCADE_SOUND
    parts = std::max(1, parts);
    const std::string key = romKey(prog, samples) + "-" + std::to_string(part) + "of" + std::to_string(parts);
    std::string have;
    bool complete = false;
    std::map<uint32_t, ArcadeLength> done = readArcadeLengths(file, &have, &complete);
    if (have != key) {
        done.clear();
        complete = false;
    }
    if (complete)
        return 0;
    audio::SoundBoard board;
    std::string err;
    if (!board.load(prog, samples, err)) {
        std::fprintf(stderr, "jukebox lengths: %s\n", err.c_str());
        return -1;
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(file).parent_path(), ec);
    // (written again with what is known, then a line per code as it is measured: the menu reads it as it grows)
    std::FILE* f = std::fopen(file.c_str(), "w");
    if (!f)
        return -1;
    auto line = [&](uint32_t c, const ArcadeLength& a) {
        std::fprintf(f, "%06X %.2f %.2f %.2f %d\n", c, a.length, a.intro, a.loop, a.loops ? 1 : 0);
        std::fflush(f);
    };
    std::fprintf(f, "hotd-jukebox-arcade-lengths %s\n", key.c_str());
    for (const auto& [c, a] : done)
        line(c, a);
    int n = 0;
    const auto codes = arcadeCodes();
    for (size_t i = 0; i < codes.size(); ++i) {
        const auto [code, music] = codes[i];
        if (int(i % size_t(parts)) != part)   // (this process's share: every parts-th code)
            continue;
        if (done.count(code))
            continue;
        if (const char* only = std::getenv("HOTD_JUKEBOX_CODES")) {   // (debugging: these codes only, "A01001,A01004")
            char hexCode[8];
            std::snprintf(hexCode, sizeof hexCode, "%06X", code);
            if (!std::strstr(only, hexCode))
                continue;
        }
        std::string diag;
        const ArcadeLength a = measureArcade(board, code, music, &diag);
        line(code, a);
        ++n;
        if (each)
            each(code, a, diag);
    }
    std::fprintf(f, "end\n");
    std::fclose(f);
    return n;
#else
    (void)prog;
    (void)samples;
    (void)file;
    (void)part;
    (void)parts;
    (void)each;
    return -1;
#endif
}

std::string Jukebox::lengthsFile(const std::string& dir, int part)
{
    return (std::filesystem::path(dir) / ("jukebox_arcade_lengths_" + std::to_string(part + 1) + ".txt")).string();
}

std::map<uint32_t, Jukebox::ArcadeLength> Jukebox::readArcadeLengthsAll(const std::string& dir, bool* complete)
{
    std::map<uint32_t, ArcadeLength> all;
    bool every = true;
    for (int p = 0; p < kMeasureParts; ++p) {
        bool c = false;
        for (const auto& [code, a] : readArcadeLengths(lengthsFile(dir, p), nullptr, &c))
            all[code] = a;
        every = every && c;
    }
    if (complete)
        *complete = every;
    return all;
}

} // namespace hotd::game
