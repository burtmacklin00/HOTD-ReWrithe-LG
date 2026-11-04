// Sonic Mode (the user's experimental fast mode, ROADMAP "Future: experimental Fast mode"; docs/teaser_status/fastmode.md):
// during play the deterministic logic runs more ticks per second of real time, at the normal rate everywhere else
// (cut-scenes, voice lines, the chapter card, the results, name entry, continue, game over, menus, pause). The logic
// itself is not changed: every tick is the arcade's tick, so a headless run or an online game with Sonic Mode runs the
// very same frames as with it off; only the time each tick takes on the screen is shorter. The schedule is a pure
// function of the game's state at each tick (no wall clock): a tick in play costs 1/factor of a display frame, counted
// in exact units of 1/42 frame, so every PC (online co-op: the host's preset) paces the same ticks the same way.
//   Off        x1     (the arcade game, unchanged)
//   Frenzy     x1.2, the Magician x1.3
//   Overkill   x1.5, the Magician x1.75
//   Nightmare  x1.5, a boss fight x1.75, the Magician x2 (user 2026-10-20: the Magician a step faster than the rest)
// A boss fight = the boss life bar is up (0x543530, life_bar_init: the Chariot, the Hangedman, the Hermit, the
// Magician; their intros are cut-scenes, so they run at the normal rate).
#pragma once

#include "game/launcher.h"   // the presets' names (sonicKey / sonicName / sonicParse)
#include "game/runtime.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace hotd::game {

// the phase of the game this tick: 0 normal rate, 1 play, 2 a boss fight in play
inline int sonicPhase(Runtime& rt)
{
    auto& mem = rt.mem;
    if (mem.at<int16_t>(A::G::game_mode) != 4 || mem.at<int16_t>(A::G::game_submode) != 2)   // mode 4 sub-mode 2 = game_play
        return 0;
    if (mem.at<uint8_t>(A::G::scene_state) != 2 || rt.letterbox != 0 || mem.u32(A::G::shot_enable) == 0 || rt.clear.active)
        return 0;   // a cut-scene (letterbox_task: scene_state 4 / the bars sliding), the results
    if (mem.u32(A::G::evt_clrgfst_flags) & 0x20)   // the chapter card (kind 177) is up
        return 0;
    if (rt.voiceFrame + 2 >= rt.frameCount && rt.voiceFrame != 0)   // a voice line (kind 161) ran this frame or the last
        return 0;
    bool inPlay = false;   // a player in play (state 5); otherwise the continue countdowns
    for (uint32_t p = 0; p < 2; ++p)
        inPlay = inPlay || mem.at<port::a::Player>(A::G::players, p).state == 5;
    if (!inPlay)
        return 0;
    if (mem.at<uint8_t>(0x543530) == 0)   // the boss life bar (scene_init_flag; hud_state.cpp life_bar_init)
        return 1;
    // the Magician (user 2026-10-20: a separate, higher speed): Chapter 4's event 4; the event is the last entry of
    // the chapter's row in scene_history (0x520720: 16 bytes per chapter, 0xFF = not played)
    const int chapter = mem.at<uint8_t>(A::G::chapter);
    if (chapter == 3) {
        int event = -1;
        for (uint32_t i = 0; i < 16; ++i) {
            const int8_t e = mem.at<int8_t>(0x520720 + 3 * 16 + i);
            if (e < 0) break;
            event = e;
        }
        if (event == 4)
            return 3;
    }
    return 2;
}

// the tick's speed factor x 20 (20 = x1)
inline int sonicFactor20(int preset, int phase)
{
    if (phase == 0) return 20;
    switch (preset) {   // phase 3 = the Magician: a step above the other bosses (user 2026-10-20)
    case 1: return phase == 3 ? 26 : 24;                    // Frenzy: x1.2, the Magician x1.3
    case 2: return phase == 3 ? 35 : 30;                    // Overkill: x1.5, the Magician x1.75
    case 3: return phase == 3 ? 40 : phase == 2 ? 35 : 30;  // Nightmare: x1.5, bosses x1.75, the Magician x2
    default: return 20;
    }
}

// the schedule: what one tick costs in 1/42 display frames (x1 42, x1.2 35, x1.5 28, x1.75 24)
struct SonicClock {
    static constexpr uint64_t kUnits = 42;
    int preset = 0;
    uint64_t units = 0;            // display time so far (1/42 frames)
    uint64_t ticks[4] = {};        // ticks by phase (3 = the Magician)
    uint64_t unitsBy[4] = {};      // display time by phase
    uint64_t nsCarry = 0;          // the window loop's remainder (advanceNs)
    int lastPhase = 0;             // the last tick's phase
    uint64_t wallNs[4] = {};       // a window run: the real time spent by phase (the last tick's; the timing report)
    // the cost of the tick about to run (its phase from the state before it)
    uint64_t cost(Runtime& rt, int* phaseOut = nullptr) const
    {
        const int ph = preset == 0 ? 0 : sonicPhase(rt);
        if (phaseOut) *phaseOut = ph;
        int f = sonicFactor20(preset, ph);
        // Light Gun Edition: Turbo Speed Run: the whole game at x1.5 / x2 (cut-scenes, voices, music too), not the menus
        if (turbo != 0 && (rt.mem.at<int16_t>(A::G::game_mode) == 4 || rt.mem.at<int16_t>(A::G::game_mode) == 5))
            f = std::max(f, turbo == 1 ? 30 : 40);
        return kUnits * 20 / uint64_t(f);
    }
    int turbo = 0;   // Turbo Speed Run: 0 off, 1 x1.5, 2 x2 (local games: a window run's pace only)
    // counts the tick; returns its cost
    uint64_t account(Runtime& rt)
    {
        int ph = 0;
        const uint64_t c = cost(rt, &ph);
        units += c;
        ++ticks[ph];
        lastPhase = ph;
        unitsBy[ph] += c;
        return c;
    }
    // the window loop: how far `next` moves for a tick of this cost, the tick's normal length being num / den ns (exact
    // over time: the remainder is carried, in 1 / (den x 42) ns). Off: exactly num / den (the loop's own schedule)
    uint64_t advanceNs(uint64_t num, uint64_t den, uint64_t c)
    {
        const uint64_t t = num * c + nsCarry;
        nsCarry = t % (den * kUnits);
        return t / (den * kUnits);
    }
    uint64_t advanceNs(uint64_t frameNs, uint64_t c) { return advanceNs(frameNs, 1, c); }
    double displayFrames() const { return double(units) / double(kUnits); }
};

} // namespace hotd::game
