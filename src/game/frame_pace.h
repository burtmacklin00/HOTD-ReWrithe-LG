// GitHub #27 (2026-11-02: "micro-stuttering, inconsistent frame pacing at 60 FPS", any V-Sync / G-Sync setting; best with
// V-Sync and Smooth Motion off): the game loop's frame pacing. Three causes, found by the model below (--pace-check):
// 1. V-Sync with a frame cap at the refresh (60 fps on a 60 Hz display): the limiter (1 / 60 s from the loop's start)
//    and the swap (the display's refresh, never exactly 60.000 Hz) both paced the loop; their beat put a frame now and
//    then past its refresh (a frame shown twice, the next one late). Now, as the menus do (menuperf): when the swap
//    paces an even beat (the cap the refresh, or a whole fraction of it) the limiter runs a little faster, only the
//    fallback, and the swap interval is that fraction.
// 2. Smooth Motion's blend read the clock after the frame's game ticks (0, 1 or 2 of them, each a millisecond or more
//    of work): the blend's time jumped with the ticks' cost. And the ticks run up to a quarter period early (the slack
//    against 0 / 2 tick beats) while the blend was clamped at 0 for them: the picture stood still for a moment. Now the
//    blend is the loop's own time plus that quarter (blendAlpha: always 0 .. 1 with the ticks the loop ran).
// 3. With V-Sync pacing the frames, the loop's clock after the swap still wanders by the swap's / the sleep's wake-up
//    (a millisecond or two); FramePace makes it the display's steady beat (one refresh a frame, following the real rate),
//    back to the raw clock after a missed refresh or a stall.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace hotd::game {

// the loop's time for a frame shown with V-Sync pacing (periodNs: the refresh x the swap interval; 0: the raw clock)
struct FramePace {
    uint64_t t = 0, raw = 0;   // the paced time after the last swap, and the raw clock then
    // after the swap (and the limiter): the raw clock
    void shown(uint64_t rawNow, uint64_t periodNs)
    {
        if (!periodNs || !t) {
            t = raw = rawNow;
            return;
        }
        t += periodNs;
        const int64_t err = int64_t(rawNow - t);
        if (err > int64_t(periodNs / 2) || err < -int64_t(periodNs / 2))
            t = rawNow;            // (a missed refresh, a stall, the window moved: again from the clock)
        else
            t += uint64_t(err / 8);   // (follows the display's true rate: 59.94, 60.02 Hz)
        raw = rawNow;
    }
    // the loop's time now: the paced time plus the raw time since the swap (never more than half a period off the clock)
    uint64_t now(uint64_t rawNow, uint64_t periodNs) const
    {
        if (!periodNs || !t || rawNow < raw || rawNow - raw > periodNs / 2)
            return rawNow;
        return t + (rawNow - raw);
    }
    void reset() { t = raw = 0; }
};

// Smooth Motion's blend from the frame before the newest tick to the newest: the loop's time `now` (the ticks ran while
// now + slack >= next) plus the same slack, past the newest tick's due time (next - lastTickNs), in periods
inline float blendAlpha(uint64_t now, uint64_t next, uint64_t lastTickNs, uint64_t slackNs)
{
    const double since = double(int64_t(now + slackNs - (next - lastTickNs)));
    return float(std::clamp(since / double(std::max<uint64_t>(lastTickNs, 1)), 0.0, 1.0));
}

// --pace-check: the loop's pacing modelled (no window): a display at hz with V-Sync (the swap returns at the refresh plus
// up to 1 ms), the game at the arcade's 57.524 Hz (or 60), each tick ~1.5 ms of work, each render ~3 ms, the sleep's
// wake-up up to 0.3 ms late. Per shown frame the game time the picture shows (Smooth Motion); its step against the
// display's step is the stutter. `fixed`: this file's pacing, else the loop as before (#27)
struct PaceResult {
    double rmsMs = 0, maxMs = 0;   // the shown game time's step minus the display's step
    int hitches = 0;               // steps more than 4 ms off (a visible jerk)
    int repeats = 0;               // refreshes that showed no new picture (a frame shown twice)
    int frames = 0;
};
inline PaceResult paceModel(bool fixed, double hz, double tickHz, int capFps, uint32_t seed = 12345)
{
    auto rnd = [&seed]() {   // (a small LCG: the same run each time)
        seed = seed * 1664525u + 1013904223u;
        return double(seed >> 8) / double(1u << 24);
    };
    const double refresh = 1e9 / hz, P = 1e9 / tickHz, q = P / 4;
    // the limiter: before, the cap's period; fixed: the cap at the refresh: the swap paces, the limiter 8 % faster
    const bool even = std::abs(hz / capFps - 1.0) < 0.03;
    const double renderNs = fixed && even ? 1e9 / (hz * 1.08) : 1e9 / capFps;
    const uint64_t paceNs = fixed && even ? uint64_t(refresh) : 0;
    FramePace pace;
    double clock = 1e9, next = clock, vblank = 1e9 + refresh * 0.37;
    uint64_t ticks = 0;
    std::vector<std::pair<double, double>> shown;   // (display time, game time shown)
    double lastV = 0;
    PaceResult r;
    for (int i = 0; i < 6000; ++i) {
        const double t0 = clock;
        const double now = fixed ? double(pace.now(uint64_t(t0), paceNs)) : t0;
        int runs = 0;
        while (now + q >= next && runs < 4) {
            clock += 1.2e6 + rnd() * 0.6e6;   // (a tick's work)
            next += P;
            ++ticks, ++runs;
        }
        if (now > next + 4 * P)
            next = now;
        const double lastTick = P;
        double alpha;
        if (fixed)
            alpha = blendAlpha(uint64_t(now), uint64_t(next), uint64_t(lastTick), uint64_t(q));
        else
            alpha = std::clamp((clock - (next - lastTick)) / lastTick, 0.0, 1.0);   // (the clock after the ticks)
        const double game = (double(ticks) - 1.0 + alpha) * P;
        clock += 2.0e6 + rnd() * 2.0e6;   // (the render's CPU work)
        while (vblank < clock)
            vblank += refresh;
        const double shownAt = vblank;    // (the swap: the picture on the next refresh)
        clock = vblank + rnd() * 1.0e6;   // (the swap returns a little after it)
        vblank += refresh;
        const double spent = clock - t0;
        if (spent < renderNs)
            clock = t0 + renderNs + rnd() * 0.3e6;   // (the limiter's sleep)
        if (fixed)
            pace.shown(uint64_t(clock), paceNs);
        if (lastV > 0)
            r.repeats += int(std::lround((shownAt - lastV) / refresh)) - 1;
        lastV = shownAt;
        if (i > 200)
            shown.push_back({shownAt, game});
    }
    double sum = 0;
    for (size_t k = 1; k < shown.size(); ++k) {
        const double e = ((shown[k].second - shown[k - 1].second) - (shown[k].first - shown[k - 1].first)) / 1e6;
        sum += e * e;
        r.maxMs = std::max(r.maxMs, std::abs(e));
        r.hitches += std::abs(e) > 4.0 ? 1 : 0;
    }
    r.frames = int(shown.size());
    r.rmsMs = shown.size() > 1 ? std::sqrt(sum / double(shown.size() - 1)) : 0.0;
    return r;
}

} // namespace hotd::game
