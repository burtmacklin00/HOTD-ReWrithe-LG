// Boss Mode (THOTD.EXE): see bossmode.h and docs/teaser_status/bossmode.md
#include "game/bossmode.h"

#include "game/runtime.h"
#include "util/utf8.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace hotd::game {

int bossModeParse(const std::string& v)
{
    if (v == "chariot" || v == "2") return 2;
    if (v == "hangedman" || v == "3") return 3;
    if (v == "hermit" || v == "4") return 4;
    if (v == "magician" || v == "5") return 5;
    if (v == "all" || v == "6" || v.empty()) return 6;
    return 0;
}

void BossMode::begin(int m)
{
    mode = m;
    index = m == 6 ? 0 : m - 2;   // 0x422EE4 ..
    phase = Approach;
    result = 0;
    timeMs = 0;                   // 0x412B40 (the first boss's window)
    clockOn = shown = false;
    dyingFrames = 0;
    justFinished = nextStarted = false;
}

void BossMode::startGame(Runtime& rt, int players) const
{
    rt.startAtSection0 = true;   // game_new: the section from boss_starts +8 (0)
    rt.boss_mode = mode;         // the bosses' own Boss Mode rules (frontend_state 4; port/game.h)
    rt.boss_next = true;         // pc_boss_next = 1 (0x422EE4 .. / 0x419168's next boss): the walk-up runs undrawn
    rt.boss_events = 0;
    rt.chapter_start(start().chapter, start().event, players);
}

bool BossMode::tick(Runtime& rt, double stepMs)
{
    justFinished = nextStarted = false;
    if (!active() || phase == Done)
        return false;
    if (clockOn)
        timeMs += stepMs;
    auto log = [&](const std::string& s) { if (rt.log) rt.log("boss mode: " + s); };
    const uint32_t ev = rt.boss_events;
    rt.boss_events = 0;
    if (rt.mem.at<int16_t>(A::G::game_mode) == 5 && clockOn) {   // game_over_start 0x512FC8: 0x412B14, 0x412C34
        clockOn = shown = false;
    }
    if (rt.gameOverDone) {   // game_over_scene 0x5130C4: result 7
        clockOn = shown = false;
        result = 7;
        phase = Done;
        justFinished = true;
        log(std::string(kBossNames[index]) + ": game over");
        return true;
    }
    if (ev & Runtime::kBossEntrance) {   // 0x412BC4: the walk-up's end, the clock shown, the boss song (cue 12 / 19)
        shown = true;
        phase = Entrance;
        rt.sound_cue(index < 3 ? 0xA01004u : 0xA0100Bu);   // (the arcade codes of the PC's cues 12 / 19)
        log(std::string(kBossNames[index]) + ": the entrance (frame " + std::to_string(rt.frameCount) + ")");
    }
    if (ev & Runtime::kBossClockStart) {   // 0x412B00: the title card's end
        clockOn = true;
        phase = Fight;
        log(std::string(kBossNames[index]) + ": the fight starts (frame " + std::to_string(rt.frameCount) + ")");
    }
    if (ev & Runtime::kBossClockStop) {   // 0x412B14: the boss's death
        clockOn = false;
        phase = Dying;
        char b[96];
        std::snprintf(b, sizeof b, "%s beaten at %s (frame %llu)", kBossNames[index], clockText().c_str(),
                      (unsigned long long)rt.frameCount);
        log(b);
    }
    if (!(ev & Runtime::kBossWin))
        return false;
    // 0x412C08 (result 1, the clock hidden), then 0x419168: all four and boss_index < 3: the next boss (a new game:
    // index + 1, pc_boss_next = 1, the clock kept); else the end
    shown = false;
    clockOn = false;
    if (mode == 6 && index < 3) {
        ++index;
        phase = Approach;
        log(std::string("next: ") + kBossNames[index]);
        rt.clear.active = false;
        rt.chapterDone = false;
        int32_t lives[4] = {}, score[4] = {}, state[4] = {};
        for (int p = 0; p < 4 && rush; ++p) {   // Boss Rush: the lives / scores go on to the next boss
            const uint32_t rec = rt.player_rec(p);
            state[p] = rt.mem.at<int32_t>(rec);
            lives[p] = rt.mem.at<int32_t>(rec + 0xC);
            score[p] = rt.mem.at<int32_t>(rec + 8);
        }
        startGame(rt, players);
        for (int p = 0; p < 4 && rush; ++p) {
            const uint32_t rec = rt.player_rec(p);
            rt.mem.at<int32_t>(rec + 8) = score[p];
            if (rt.mem.at<int32_t>(rec) == 5)   // (in the new game: its lives as they were; out: 0, out at once)
                rt.mem.at<int32_t>(rec + 0xC) = state[p] == 5 ? std::max(lives[p], 1) : 0;
        }
        rt.chapterChanged = start().chapter;
        nextStarted = true;
        return true;
    }
    result = 1;
    phase = Done;
    justFinished = true;
    log(std::string(kBossNames[mode - 2]) + " cleared in " + clockText());
    return true;
}

std::string BossMode::timeText(int ms)
{
    ms = std::clamp(ms, 0, 3600000);
    char b[32];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", ms / 60000, (ms % 60000) / 1000, (ms % 60000) % 1000 / 10);
    return b;
}

std::string BossMode::clockText() const { return timeText(int(ms())); }

BossTimes::BossTimes()
{
    static const char* const kNames[10] = {"BART", "BEN", "JOHN", "PAUL", "SCOTTIE", "STEVE", "BRETT", "SHANE", "ANDY", "ROHAN"};
    static const int kFirst[5] = {24000, 74000, 40000, 70000, 225000}, kStep[5] = {2000, 2000, 2000, 2000, 5000};
    for (int m = 0; m < 5; ++m)
        for (int i = 0; i < 10; ++i)
            rows[m][i] = {kNames[i], kFirst[m] + i * kStep[m]};
}

bool BossTimes::load(const std::string& path)
{
    std::ifstream f(path);
    if (!f)
        return false;
    BossTimes t;
    int n[5] = {};
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream s(line);
        int mode = 0, ms = 0;
        std::string name;
        if (!(s >> mode >> name >> ms) || mode < 2 || mode > 6 || n[mode - 2] >= 10)
            continue;
        if (name == "_") name.clear();
        std::replace(name.begin(), name.end(), '_', ' ');   // (a space in a name is kept as '_')
        t.rows[mode - 2][n[mode - 2]++] = {util::utf8Head(name, 15), std::clamp(ms, 0, 3600000)};
    }
    *this = t;
    return true;
}

bool BossTimes::save(const std::string& path) const
{
    std::ofstream f(path, std::ios::trunc);
    if (!f)
        return false;
    for (int m = 0; m < 5; ++m)
        for (const Row& r : rows[m]) {
            std::string n = r.name;
            std::replace(n.begin(), n.end(), ' ', '_');   // (a space in a name is kept as '_')
            f << (m + 2) << ' ' << (n.empty() ? "_" : n) << ' ' << r.ms << '\n';
        }
    return bool(f);
}

bool BossTimes::isHigh(int mode, int ms) const
{
    if (mode < 2 || mode > 6) return false;
    for (const Row& r : rows[mode - 2])
        if (ms < r.ms) return true;
    return false;
}

int BossTimes::insert(int mode, const std::string& name, int ms)
{
    if (!isHigh(mode, ms)) return -1;
    Row* t = rows[mode - 2];
    int at = 0;
    while (at < 10 && t[at].ms <= ms) ++at;
    for (int i = 9; i > at; --i) t[i] = t[i - 1];
    std::string n;
    for (char c : name) if (c != '\t' && c != '\n' && c != '_') n += c;   // (the name entry's spaces are kept)
    while (!n.empty() && n.back() == ' ') n.pop_back();
    t[at] = {util::utf8Head(n, 15), ms};
    return at;
}

} // namespace hotd::game
