// Horde Mode (ours): see horde.h and docs/horde.md
#include "game/horde.h"

#include "game/runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hotd::game {

using G = A::G;

namespace {
constexpr uint32_t kTaskSize = 0x3C8;   // a creature's task (ENTB3's task_create size, the arcade logic)
constexpr uint32_t kWork = 0x20;        // the work area (task_clear_work: +0x20 .. the end); below it the task's links
constexpr float kSide = 34.0f;          // a copy's distance to the side of the creature (then 2x for the third)

// the body slots taken now (the original's 14 creature slots and the extra ones)
int bodiesNow(const Runtime& rt)
{
    int n = 0;
    for (uint32_t i = 0; i < 14; ++i)
        n += rt.mem.u32(G::player_bodies + 4 * i) != 0 ? 1 : 0;
    for (int j = 0; j < rt.extraBodySlots; ++j)
        n += rt.mem.u32(rt.extraBodyTable + 4 * uint32_t(j)) != 0 ? 1 : 0;
    return n;
}
} // namespace

uint32_t HordeMode::next()
{
    rng_ = rng_ * 1103515245u + 12345u;
    return (rng_ >> 8) & 0xFFFFFF;
}

const char* HordeMode::sizeName(int s)
{
    static const char* const k[4] = {"Off", "Low", "Normal", "High"};
    return k[std::clamp(s, 0, 3)];
}

int HordeMode::sizeParse(const std::string& v)
{
    if (v == "low" || v == "1") return 1;
    if (v == "normal" || v == "2" || v.empty()) return 2;
    if (v == "high" || v == "3") return 3;
    return -1;
}

void HordeMode::begin(Runtime& rt, int sizeSetting)
{
    *this = HordeMode();
    size = std::clamp(sizeSetting, 0, 3);
    if (!on())
        return;
    // more bodies than the original's 14 creature slots (game.h extraBodySlots; docs/survival.md "Limits": the same
    // as Survival Mode's): slots 16.. in a table outside the original map, the skin buffers grown at each chapter's
    // heap reset (tasks.cpp)
    if (!rt.mem.mapped(kBodyTable))
        rt.mem.map(kBodyTable, 0x1000);
    for (uint32_t i = 0; i < 0x1000; i += 4)
        rt.mem.set32(kBodyTable + i, 0);
    rt.extraBodyTable = kBodyTable;
    rt.extraBodySlots = kBodySlots;
    if (rt.log)
        rt.log(std::string("horde: size ") + sizeName(size) + " (" + std::to_string(size) + " copies a creature)");
}

void HordeMode::spawned(Runtime& rt, uint32_t task, uint32_t rec)
{
    if (!on() || task == 0)
        return;
    // the standard walkers only (kind 0: kind000_enemy_spawn, kind 9: kind009_enemy_spawn_b); every other kind (the
    // bosses, hostages, their scripted attackers, the scene creatures) stays as the script has it
    const uint32_t kind = rt.mem.u32(rec);
    if (kind != 0 && kind != 9)
        return;
    if (rt.mem.u32(G::shot_enable) == 0 || rt.letterbox != 0)
        return;   // (a creature of a cut-scene: as the script has it)
    ++originals;
    const uint32_t routine = rt.mem.u32(task);
    const float yaw = float(int32_t(rt.mem.u32(task + 0x30))) * (6.2831853f / 65536.0f);
    const float sx = std::cos(yaw), sz = -std::sin(yaw);   // the creature's side (its facing turned 90 degrees)
    for (int i = 1; i <= size; ++i) {
        // the cap: creatures alive now (the copies not yet run count from their first frame, so the ones of this
        // opcode are added), and body slots for the script's own creatures to come
        const int alive = std::max(0, int(rt.mem.at<int16_t>(G::enemy_count)));
        if (alive + i > kCap || bodiesNow(rt) + i > 14 + rt.extraBodySlots - 4) {
            ++skipped;
            continue;
        }
        const uint32_t t = rt.task_create(routine, kTaskSize);
        if (t == 0) {
            ++skipped;
            continue;
        }
        for (uint32_t o = kWork; o < kTaskSize; o += 4)   // the creature as the opcode set it (before it first ran)
            rt.mem.set32(t + o, rt.mem.u32(task + o));
        // beside it: the first copy to one side, the second to the other, the third farther out; a little jitter
        const float side = float((i & 1) ? 1 : -1) * kSide * float((i + 1) / 2);
        const float jx = float(int(next() % 13) - 6), jz = float(int(next() % 13) - 6);
        rt.mem.at<float>(t + 0x20) = rt.mem.f32(task + 0x20) + sx * side + jx;
        rt.mem.at<float>(t + 0x28) = rt.mem.f32(task + 0x28) + sz * side + jz;
        // and a moment later (the delay before it comes: +0x320, x 10 frames, x 5 walking)
        rt.mem.set32(t + 0x320, rt.mem.u32(task + 0x320) + uint32_t(i));
        ++copies;
    }
}

void HordeMode::tick(Runtime& rt)
{
    if (!on())
        return;
    peakAlive = std::max(peakAlive, int(rt.mem.at<int16_t>(G::enemy_count)));
    peakBodies = std::max(peakBodies, bodiesNow(rt));
}

std::string HordeMode::summary() const
{
    char b[160];
    std::snprintf(b, sizeof b, "size=%s originals=%d copies=%d skipped=%d peak=%d bodies=%d", sizeName(size), originals, copies,
                  skipped, peakAlive, peakBodies);
    return b;
}

} // namespace hotd::game
