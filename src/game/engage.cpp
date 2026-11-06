// Creatures that will never attack (ours): see engage.h and docs/survival.md "Creatures that never attack"
#include "game/engage.h"

#include "game/runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hotd::game {

using F = A::F;

std::string EngageWatch::check(Runtime& rt, uint32_t task, uint32_t serial)
{
    const port::a::Enemy& e = rt.mem.at<port::a::Enemy>(task);
    auto it = std::find_if(v_.begin(), v_.end(), [&](const E& x) { return x.task == task && x.serial == serial; });
    if (it == v_.end()) {
        v_.push_back({task, serial, 1e9f, rt.frameCount});
        it = v_.end() - 1;
    }
    E& w = *it;
    auto matters = [&] { w.best = 1e9f, w.since = rt.frameCount; return std::string(); };
    // (each reason has to hold kStuckFrames on end: a moment between two states is not enough)
    auto held = [&](const std::string& why) { return rt.frameCount - w.since >= uint64_t(kStuckFrames) ? why : std::string(); };
    if (e.routine != F::enemy_main)
        return matters();   // (the scripted kinds, the walk-in to the camera stop, dying, corpses: the game's own timing)
    if ((e.flags & 1) == 0)
        return held("enemy_main, not shootable: enemy_main runs no AI without flags bit 0");
    if (e.state < 1 || e.state > 9)
        return held("enemy_main in state " + std::to_string(e.state) + ": enemy_ai acts on states 1-9 only");
    if (e.state != 1 || e.delay != 0)
        return matters();   // (an attack position held, or its delay still running: it comes)
    const float dx = e.target_pos[0] - e.pos[0], dz = e.target_pos[2] - e.pos[2];
    const float d = std::sqrt(dx * dx + dz * dz);
    if (d <= kNear || d < w.best - 2.0f) {   // (within its switch distance, or still getting nearer)
        w.best = std::min(w.best, d), w.since = rt.frameCount;
        return {};
    }
    char b[128];
    std::snprintf(b, sizeof b, "enemy_main state 1, %.0f units from its target, no nearer for %d frames (state 1 moves on within 70)", d,
                  kStuckFrames);
    return held(b);
}

void EngageWatch::retire(Runtime& rt, uint32_t task)
{
    port::a::Enemy& e = rt.mem.at<port::a::Enemy>(task);
    e.flags &= ~(1u << 21);
    e.routine = F::kind10_remove;
}

void EngageWatch::forget(uint32_t task)
{
    v_.erase(std::remove_if(v_.begin(), v_.end(), [&](const E& x) { return x.task == task; }), v_.end());
}

} // namespace hotd::game
