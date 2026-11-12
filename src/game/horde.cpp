// Horde Mode (ours): see horde.h and docs/horde.md
#include "game/horde.h"

#include "game/ragdoll.h"
#include "game/runtime.h"
#include "game/survival.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <vector>

namespace hotd::game {

using G = A::G;

namespace {
constexpr uint32_t kTaskSize = 0x3C8;   // a creature's task (ENTB3's task_create size, the arcade logic)
constexpr uint32_t kWork = 0x20;        // the work area (task_clear_work: +0x20 .. the end); below it the task's links


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

// feel (testing, HOTD_HORDE_TRACE): the walkers made by the spawn opcodes and their copies, followed for the trace
struct Tracked { uint32_t task; int32_t serial; float x, z; bool seen, gone; };
std::vector<Tracked> gTracked;
void track(const Runtime& rt, uint32_t t)
{
    if (std::getenv("HOTD_HORDE_TRACE"))
        gTracked.push_back({t, int32_t(rt.mem.u32(t + 8)), 0, 0, false, false});
}

// horde3 (testing, HOTD_HORDE_WALLS=1 with --render; user 2026-11-05 "they walk through walls and objects"): every
// creature the spawn opcodes made (any kind, in play) and every copy, followed frame by frame: its step (last frame's
// place to this one's, at knee and chest height: 6 and 14 units over its feet) tested against the stage's standing
// triangles, the ones the ragdolls use (view.cpp "the ragdolls' walls": the world's, then this frame's moving props).
// A crossing prints HWALL (who, where, which wall) with the running totals; HWALLS lines every 3000 frames. Steps
// longer than 8 units (a place set, not walked) are not tested
bool wallTest() { static const bool w = std::getenv("HOTD_HORDE_WALLS") != nullptr; return w; }
struct Probe { uint32_t task, serial, kind; bool copy; float p[3]; bool have; int cross; };
std::vector<Probe> gProbe;
struct WallCounts { long long steps = 0, cross[2] = {0, 0}, walking[2] = {0, 0}, creatures[2] = {0, 0}, crossers[2] = {0, 0}; };
WallCounts gWall;
std::vector<uint32_t> gFreed;   // (the copies off their trail: the HWALL lines say "free")
void probeAdd(const Runtime& rt, uint32_t t, uint32_t kind, bool copy)
{
    if (!wallTest())
        return;
    gProbe.push_back({t, rt.mem.u32(t + 8), kind, copy, {0, 0, 0}, false, 0});
    gFreed.erase(std::remove(gFreed.begin(), gFreed.end(), t), gFreed.end());
    ++gWall.creatures[copy ? 1 : 0];
}
// the segment a -> b through the triangle t (its corners t[0..8]): Moller-Trumbore, 0 <= s <= 1
bool segmentHits(const float* a, const float* b, const float* t)
{
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float e1[3] = {t[3] - t[0], t[4] - t[1], t[5] - t[2]}, e2[3] = {t[6] - t[0], t[7] - t[1], t[8] - t[2]};
    const float p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
    const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (std::abs(det) < 1e-9f)
        return false;
    const float inv = 1.0f / det;
    const float s[3] = {a[0] - t[0], a[1] - t[1], a[2] - t[2]};
    const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
    if (u < 0.0f || u > 1.0f)
        return false;
    const float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
    const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (v < 0.0f || u + v > 1.0f)
        return false;
    const float k = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    return k >= 0.0f && k <= 1.0f;
}
// horde4 (testing, HOTD_HORDE_WALLS with --render): a placement's way in checked against the walls: the straight line
// from its place to the camera's foot (6 and 14 units over the floor) and a floor under it; blocked: the nearest place
// within 60 units (rings of 4) that is clear, printed (HPLACECHECK) for the table
bool lineBlocked(const RagdollWalls& w, const float a0[3], const float b0[3])
{
    for (const float h : {6.0f, 14.0f}) {
        const float a[3] = {a0[0], a0[1] + h, a0[2]}, b[3] = {b0[0], b0[1] + h, b0[2]};
        float lo[3], hi[3];
        for (int k = 0; k < 3; ++k)
            lo[k] = std::min(a[k], b[k]), hi[k] = std::max(a[k], b[k]);
        for (const RagdollWalls::Obj& o : w.objs) {
            if (o.hi[0] < lo[0] || o.lo[0] > hi[0] || o.hi[1] < lo[1] || o.lo[1] > hi[1] || o.hi[2] < lo[2] || o.lo[2] > hi[2])
                continue;
            for (size_t t = 0; t + 11 < o.tris.size(); t += 12)
                if (segmentHits(a, b, &o.tris[t]))
                    return true;
        }
    }
    return false;
}
bool floorUnder(const RagdollWalls& w, float x, float y, float z)
{
    for (const RagdollWalls::Obj& o : w.objs)
        for (size_t t = 0; t + 11 < o.floors.size(); t += 12) {
            const float* p = &o.floors[t];
            const float ax = p[0], az = p[2], bx = p[3] - ax, bz = p[5] - az, cx = p[6] - ax, cz = p[8] - az;
            const float den = bx * cz - bz * cx;
            if (std::abs(den) < 1e-6f)
                continue;
            const float px = x - ax, pz = z - az;
            const float v = (px * cz - pz * cx) / den, u = (bx * pz - bz * px) / den;
            if (v < 0 || u < 0 || v + u > 1)
                continue;
            const float hy = p[1] + v * (p[4] - p[1]) + u * (p[7] - p[1]);
            if (std::abs(hy - y) < 4.0f)
                return true;
        }
    return false;
}
void placeCheck(const Runtime& rt, int index, const float pos[3], const float cam[3])
{
    const RagdollWalls* w = wallTest() && rt.ragdollWorld ? rt.ragdollWorld() : nullptr;
    if (!w)
        return;
    // clear to the camera's foot and to 12 units either side of it (the attack places beside the players)
    const auto clear = [&](const float q[3]) {
        const float dx = cam[0] - q[0], dz = cam[2] - q[2], l = std::max(1e-3f, std::hypot(dx, dz));
        for (const float s : {0.0f, 12.0f, -12.0f}) {
            const float foot[3] = {cam[0] - dz / l * s, q[1], cam[2] + dx / l * s};
            if (lineBlocked(*w, q, foot))
                return false;
        }
        return floorUnder(*w, q[0], q[1], q[2]);
    };
    if (clear(pos)) {
        std::printf("HPLACECHECK %d ok %.1f %.1f\n", index, pos[0], pos[2]);
        return;
    }
    for (float r = 4.0f; r <= 60.0f; r += 4.0f)
        for (int k = 0; k < 16; ++k) {
            const float q[3] = {pos[0] + r * std::cos(float(k) * 0.3927f), pos[1], pos[2] + r * std::sin(float(k) * 0.3927f)};
            if (clear(q)) {
                std::printf("HPLACECHECK %d blocked %.1f %.1f -> try %.1f %.1f\n", index, pos[0], pos[2], q[0], q[2]);
                return;
            }
        }
    std::printf("HPLACECHECK %d blocked %.1f %.1f, nothing clear within 60\n", index, pos[0], pos[2]);
}
void wallProbe(const Runtime& rt)
{
    if (!wallTest())
        return;
    const RagdollWalls* w = rt.ragdollWorld ? rt.ragdollWorld() : nullptr;
    static size_t lastObjs = SIZE_MAX;
    if (w && w->statics != lastObjs) {
        size_t tris = 0;
        for (size_t i = 0; i < w->statics && i < w->objs.size(); ++i)
            tris += w->objs[i].tris.size() / 12;
        std::printf("HWALLS walls %zu world objects, %zu standing triangles (chapter %d)\n", w->statics, tris,
                    int(rt.mem.at<int8_t>(G::chapter)));
        lastObjs = w->statics;
    }
    for (Probe& pr : gProbe) {
        if (pr.task == 0)
            continue;
        const uint32_t routine = rt.mem.u32(pr.task);
        if (routine == 0 || rt.mem.u32(pr.task + 8) != pr.serial) {
            pr.task = 0;
            continue;
        }
        if (routine == 0x14E90)   // (a copy still waiting: the scheduler's ret)
            continue;
        const float c[3] = {rt.mem.f32(pr.task + 0x20), rt.mem.f32(pr.task + 0x24), rt.mem.f32(pr.task + 0x28)};
        const float step = std::hypot(c[0] - pr.p[0], c[1] - pr.p[1], c[2] - pr.p[2]);
        if (w && pr.have && step > 0.01f && step < 8.0f) {
            ++gWall.steps;
            int hitObj = -1, hitTri = -1;
            for (const float h : {6.0f, 14.0f}) {
                const float a[3] = {pr.p[0], pr.p[1] + h, pr.p[2]}, b[3] = {c[0], c[1] + h, c[2]};
                float lo[3], hi[3];
                for (int k = 0; k < 3; ++k)
                    lo[k] = std::min(a[k], b[k]), hi[k] = std::max(a[k], b[k]);
                for (size_t oi = 0; oi < w->objs.size() && hitObj < 0; ++oi) {
                    const RagdollWalls::Obj& o = w->objs[oi];
                    if (o.hi[0] < lo[0] || o.lo[0] > hi[0] || o.hi[1] < lo[1] || o.lo[1] > hi[1] || o.hi[2] < lo[2] || o.lo[2] > hi[2])
                        continue;
                    for (size_t t = 0; t + 11 < o.tris.size(); t += 12) {
                        const float* q = &o.tris[t];
                        bool out = false;
                        for (int k = 0; k < 3 && !out; ++k)
                            out = std::max({q[k], q[3 + k], q[6 + k]}) < lo[k] || std::min({q[k], q[3 + k], q[6 + k]}) > hi[k];
                        if (!out && segmentHits(a, b, q)) {
                            hitObj = int(oi), hitTri = int(t / 12);
                            break;
                        }
                    }
                }
                if (hitObj >= 0)
                    break;
            }
            if (hitObj >= 0) {
                const int who = pr.copy ? 1 : 0;
                const bool walking = routine == A::F::enemy_main || routine == A::F::enemy_wait_camera_stop;
                ++gWall.cross[who];
                gWall.walking[who] += walking ? 1 : 0;
                if (pr.cross++ == 0)
                    ++gWall.crossers[who];
                std::printf("HWALL %llu %s kind %u task %X routine %X%s%s (%.1f %.1f %.1f) -> (%.1f %.1f %.1f) %s %d:%d | "
                            "originals %lld (walking %lld, %lld of %lld creatures) copies %lld (walking %lld, %lld of %lld)\n",
                            (unsigned long long)rt.frameCount, pr.copy ? "copy" : "original", pr.kind, pr.task, routine,
                            walking ? " walking" : "", pr.copy && std::find(gFreed.begin(), gFreed.end(), pr.task) != gFreed.end() ? " free" : "", pr.p[0], pr.p[1], pr.p[2], c[0], c[1], c[2],
                            size_t(hitObj) < w->statics ? "world" : "prop", hitObj, hitTri, gWall.cross[0], gWall.walking[0],
                            gWall.crossers[0], gWall.creatures[0], gWall.cross[1], gWall.walking[1], gWall.crossers[1],
                            gWall.creatures[1]);
            }
        }
        std::copy(c, c + 3, pr.p);
        pr.have = true;
    }
    if (rt.frameCount % 3000 == 0) {
        gProbe.erase(std::remove_if(gProbe.begin(), gProbe.end(), [](const Probe& p) { return p.task == 0; }), gProbe.end());
        std::printf("HWALLS %llu steps %lld | originals %lld crossings (walking %lld), %lld of %lld creatures | copies %lld "
                    "(walking %lld), %lld of %lld%s\n", (unsigned long long)rt.frameCount, gWall.steps, gWall.cross[0],
                    gWall.walking[0], gWall.crossers[0], gWall.creatures[0], gWall.cross[1], gWall.walking[1], gWall.crossers[1],
                    gWall.creatures[1], w ? "" : " (no walls: --render)");
    }
}
} // namespace

bool HordeMode::testing()
{
    static const bool t = std::getenv("HOTD_HORDE_TRACE") != nullptr || std::getenv("HOTD_HORDE_WALLS") != nullptr;
    return t;
}

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
    // horde4: the placed cut creatures (the table): their borrowed tables and texture areas, as Survival's (survival.cpp)
    rt.survival_restore();
    survivalCutTables(rt);
    rt.survival_areas();
    if (rt.log)
        rt.log(std::string("horde: size ") + sizeName(size) + " (" + std::to_string(size) + " copies a creature)");
}

// horde3 (user 2026-11-05, played Horde: "where the creatures come from does not make sense and they walk through walls
// and objects"; horde2's copies came 40+ units behind their creature at one of the event's places, a few units to a
// side, and walked from there as if they stood at their record's place: across walls and props). Now each copy is its
// creature again: the same spawn record's work area (the place, the heading, the record delay, the action, the start
// sub-state), so it starts exactly where the level design starts that creature (a door, a corridor's end, a window)
// and walks its way (the standard walker walks along its heading for its delay, then turns to its player: enemy_ai).
// The copies of a creature queue at its place: the next one 0.5-3 s after the last one from there and only once that
// one has walked off the place (or is gone), while the camera is at the stop they walk to. 70 % of the time a copy is
// another type of the same set-up (spawn routine, action, start sub-state, weight: only set-ups the game made in this
// chapter), its record delay scaled to its walk speed so its delay walk ends where its creature's does. At most kBurst
// creatures appear within half a second (kBurstNear of them near the camera); a near place's copies come later. The kit
// zombies (32) and the agile ones (70) are not copied themselves (their records drive event flags, routes and shared
// slots: docs/horde.md): each gets standard walkers as escorts, queued at one of the event's walker places.
namespace {
constexpr uint32_t kTaskNopRoutine = 0x14E90;    // the scheduler's ret (runtime.cpp kTaskNop): a waiting copy
constexpr uint32_t kTaskUnlinkRoutine = 0x15110; // task_unlink: freed on its turn (a copy dropped)
constexpr int kBurst = 4;                        // creatures appearing within kBurstFrames at most (originals too)
constexpr int kBurstNear = 3;                    // ... of them within kNearCam of the camera
constexpr float kNearCam = 75.0f;               // (about arm's length: the creatures attack from ~25)
constexpr int kBurstFrames = 30;
constexpr float kNearSpawn = 110.0f;             // a place this near the camera: its copies 1-3 s apart (else 0.5-3 s)
constexpr float kNoCopyNear = 150.0f;            // an entrance this near the camera: one copy at a time from it (user 2026-11-05)
// horde4 (2026-11-09): the test runs found Horde Normal *thinner* than the plain game (Chapter 1: 1.18 creatures in play
// on average against 1.40, 38 s of the chapter with nothing to shoot while copies waited, counted, for a way): every
// arcade walker of Chapter 1 steps out within 150 units of the camera, so "at most 1 (Normal) / 2 (High) copies from
// such an entrance" made Horde one copy a creature at most, and a copy whose creature was shot before it walked waited
// 2 s for another's way, then was dropped. Now an entrance near the camera sends kNearCopies, one at a time (the last
// one from it kNearOff units off its place, or gone), while fewer than kNearCrowd creatures are within kNearCam of the
// camera (no pile at arm's length: the bridge), and a copy with no way known walks its creature's own way as its
// creature (its type and speed): a pioneer, its steps the lane's trail for the next ones. (Tried, Chapter 1 Normal:
// the last one gone, crowd 2-3: 1.38-1.61 in play, the chapter 1.4-1.5x as long: the bot kills a lone copy as it steps
// out, so a trickle; 15 units off, crowd 4: 1.78, 1.33x; waiting while two stand in front changed nothing)
constexpr int kNearCopies[4] = {0, 2, 4, 6};     // copies in all from such an entrance (Low / Normal / High)
constexpr int kNearCrowd[4] = {0, 3, 4, 5};      // its next copy waits while this many creatures are within kNearCam
constexpr float kNearOff = 15.0f;                // ... and until the last one from it is this far off its place (or gone)
constexpr float kArmsLength = 30.0f;             // horde4: no copies from a place this near the camera
constexpr float kPioneerWalk = 16.0f;           // horde4: a pioneer walks this far along its record's heading before it turns
constexpr float kMoveNear = 60.0f;              // horde4: a copy with no way known goes to another place this near (else a pioneer)
constexpr float kClear = 6.0f;                  // the last creature from a place this far off it: the next may come
constexpr float kCamMoved = 25.0f;               // the camera this far from the lane's stop: its copies are dropped (it sways a little at a stop)
constexpr float kTrailSide = 0.3f;               // a copy kept this near its lane's trail (its creature passed door posts closer than a unit)
constexpr size_t kTrailMin = 8;                  // a trail this short (its creature shot at its place): its copies from another place
constexpr int kStuck = 360;                      // frames a place may stay taken (the camera stopped) before its copy is dropped
constexpr int kCopies[4] = {0, 3, 6, 10};        // the copies of a standard walker (Low / Normal / High)
constexpr int kReserve[4] = {0, 6, 12, 18};       // the dropped copies kept for the next creatures at most
constexpr int kPerPlace = 2;                     // copies made (and counted) a place at a time; the rest its credit
constexpr int kTaken = 45;                       // frames a place stays taken (its creature stands there) before a copy goes to another
constexpr int kEscorts[4] = {0, 1, 2, 3};        // the walkers sent with a kind 32 / 70 creature

bool tracing() { static const bool t = std::getenv("HOTD_HORDE_TRACE") != nullptr; return t; }

void countsAdd(Runtime& rt, int d)
{
    for (const uint32_t a : {G::enemy_count, G::enemies_alive}) {
        const int v = int(rt.mem.at<int16_t>(a)) + d;
        rt.mem.at<int16_t>(a) = int16_t(std::max(0, v));
    }
}

bool alive(const Runtime& rt, uint32_t task, uint32_t serial)
{
    return task != 0 && rt.mem.u32(task) != 0 && rt.mem.u32(task + 8) == serial;
}

} // namespace

// a set-up's template (added once: the chapter's walkers seen so far)
int HordeMode::templateOf(uint32_t routine, int16_t type, int16_t action, int16_t life, uint32_t sub)
{
    for (size_t i = 0; i < pool_.size(); ++i)
        if (pool_[i].routine == routine && pool_[i].type == type && pool_[i].action == action && pool_[i].sub == sub)
            return int(i);
    if (pool_.size() >= 64)
        return -1;
    pool_.push_back({routine, type, action, life, sub});
    return int(pool_.size()) - 1;
}

// the lanes' trails: where each lane's original walks (a point a unit or more from the last), until it dies or attacks
void HordeMode::trailsTick(Runtime& rt)
{
    for (Lane& l : lanes_) {
        if (l.trailDone)
            continue;
        if (!alive(rt, l.trailTask, l.trailSerial)) {
            l.trailDone = true;
            continue;
        }
        if (rt.mem.u32(l.trailTask) != A::F::enemy_main)
            continue;   // (waiting at its place, or shot: a hit's stagger is no way to walk)
        if (rt.mem.at<int16_t>(l.trailTask + 0x5C) >= 7 || l.trail.size() >= 600) {   // (its lunge / attack: at the player)
            l.trailDone = true;
            continue;
        }
        const float x = rt.mem.f32(l.trailTask + 0x20), z = rt.mem.f32(l.trailTask + 0x28);
        if (l.trail.empty())
            l.trail.insert(l.trail.end(), {l.pos[0], l.pos[2]});
        if (std::hypot(x - l.trail[l.trail.size() - 2], z - l.trail.back()) >= 1.0f)
            l.trail.insert(l.trail.end(), {x, z});
    }
}

// the walkers each frame: their set-up's walk speed measured (the step a frame while they walk: the motion lists'
// speeds are 0 for these set-ups, their steps come from their motions; a copy of another type takes the record delay
// scaled by the two); held to their record's heading while they stand at their place; a copy on its lane's trail
void HordeMode::walkersTick(Runtime& rt)
{
    for (Walker& w : walkers_) {
        if (w.task == 0)
            continue;
        if (!alive(rt, w.task, w.serial)) {
            w.task = 0;
            continue;
        }
        float c[3] = {rt.mem.f32(w.task + 0x20), rt.mem.f32(w.task + 0x24), rt.mem.f32(w.task + 0x28)};
        const uint32_t routine = rt.mem.u32(w.task);
        const int state = rt.mem.at<int16_t>(w.task + 0x5C);
        // its way from the place: a walker near its player with no front attack place stands (enemy_ai state 2) and
        // turns to its player there, then walks straight: from a place near the camera (a door beside it, a corner),
        // the turn while walking is what takes the record's way round the door post; so while it stands at its place
        // its heading stays the record's (its creature walks off from there as the level design has it)
        if (w.lane >= 0 && routine == A::F::enemy_main && state == 2 && std::hypot(c[0] - w.home[0], c[2] - w.home[1]) < 6.0f)
            rt.mem.set32(w.task + 0x30, w.ang);
        // horde4: a pioneer walks its record's heading off its place first, as its creature did while the camera came
        // (the spawn routine's walk-in, sub-state 2: Chapter 1's side passage, 16 units straight on before it turned);
        // the camera already stopped, its creature's copy turned to its player at once and went through the wall beside
        if (w.pioneer && routine == A::F::enemy_main && (state == 1 || state == 3 || state == 5) && std::hypot(c[0] - w.home[0], c[2] - w.home[1]) < kPioneerWalk)
            rt.mem.set32(w.task + 0x30, w.ang);
        // a copy on its trail does not stand at its place for a front attack place (enemy_ai state 2: it would block
        // its place for the next ones): it walks on along the trail as state 3 has it (the walk motion; at 40 units
        // from its player it takes a place or waits there, state 4: in the crowd in front of the players)
        if (w.lane >= 0 && w.gen == lanesGen_ && routine == A::F::enemy_main && state == 2) {
            port::a::Enemy& e = rt.mem.at<port::a::Enemy>(w.task);
            e.state = 3;
            e.substate = int16_t(e.walk_variant == 1 ? 2 : 1);
            uint32_t m;
            if (e.walk_variant > 1) {
                m = rt.mem.u32(G::enemy_walk_motions_b + uint32_t(e.walk_variant) * 4);
            } else {
                const uint32_t row = rt.mem.u32(G::enemy_motion_lists + uint32_t(int32_t(e.type)) * 4);
                const uint32_t list = rt.mem.u32(row + uint32_t(int32_t(e.action)) * 4);
                m = rt.mem.u32(list + uint32_t(e.substate) * 8);
            }
            rt.set_motion_blend(w.task, m, int(next() % 10), 10);
            ++walkedOn;
        }
        // a copy walks where its lane's original walked (its own target, attack place and speed differ: left alone, it
        // would cut the corners its creature went round): its place kept within kTrailSide of the trail, its heading along
        // it, until the trail's end (then the game's own walk to its player, from where its creature was)
        if (w.lane >= 0 && (w.gen != lanesGen_ || size_t(w.lane) >= lanes_.size()))
            w.lane = -1, gFreed.push_back(w.task);
        if (w.lane >= 0 && routine == A::F::enemy_main) {
            const Lane& l = lanes_[size_t(w.lane)];
            const int n = int(l.trail.size() / 2);
            if (n < 2 && l.trailDone) {
                w.lane = -1, ++trailFreed, gFreed.push_back(w.task);
            } else if (n >= 2) {
                int best = -1;
                float bt = 0, bd = 1e30f, bq[2] = {0, 0};
                for (int s = std::max(0, w.along - 3); s <= std::min(n - 2, w.along + 10); ++s) {
                    const float* a = &l.trail[size_t(s) * 2];
                    const float dx = a[2] - a[0], dz = a[3] - a[1], len2 = dx * dx + dz * dz;
                    float t = len2 > 1e-6f ? ((c[0] - a[0]) * dx + (c[2] - a[1]) * dz) / len2 : 0.0f;
                    t = std::clamp(t, 0.0f, 1.0f);
                    const float q[2] = {a[0] + dx * t, a[1] + dz * t};
                    const float d = std::hypot(c[0] - q[0], c[2] - q[1]);
                    if (d < bd - 1e-4f)
                        best = s, bt = t, bd = d, bq[0] = q[0], bq[1] = q[1];
                }
                // its end, its creature shot there: on along another of the event's trails that
                // passes here (a creature from the same door, the same corridor) when one goes on further
                int other = -1, otherAt = 0;
                if (best == n - 2 && bt >= 1.0f && l.trailDone) {
                    int most = 3;
                    for (size_t o = 0; o < lanes_.size(); ++o) {
                        const std::vector<float>& tr = lanes_[o].trail;
                        const int m = int(tr.size() / 2);
                        for (int j = 0; j + 3 < m; ++j)
                            if (std::hypot(tr[size_t(j) * 2] - c[0], tr[size_t(j) * 2 + 1] - c[2]) < 3.0f) {
                                if (m - j > most)
                                    most = m - j, other = int(o), otherAt = j;
                                break;
                            }
                    }
                }
                if (other >= 0) {
                    w.lane = other, w.along = otherAt, ++trailSwitched;
                } else if (best == n - 2 && bt >= 1.0f && l.trailDone) {
                    w.lane = -1, ++trailFreed, gFreed.push_back(w.task);   // (its end: from here as its creature did)
                    if (tracing())
                        std::printf("HFREE %llu task %X at %.1f %.1f trail %d camera %.0f original %s\n", (unsigned long long)rt.frameCount, w.task,
                                    c[0], c[2], n, std::hypot(c[0] - rt.mem.f32(G::camera_pos), c[2] - rt.mem.f32(G::camera_pos + 8)),
                                    alive(rt, l.trailTask, l.trailSerial) ? "attacks" : "gone");
                } else if (best >= 0) {
                    w.along = best;
                    if (bd > kTrailSide) {
                        c[0] = bq[0] + (c[0] - bq[0]) * (kTrailSide / bd), c[2] = bq[1] + (c[2] - bq[1]) * (kTrailSide / bd);
                        rt.mem.at<float>(w.task + 0x20) = c[0];
                        rt.mem.at<float>(w.task + 0x28) = c[2];
                        ++trailPulled;
                    }
                    if (state == 1 || state == 3 || state == 5) {
                        const float* a = &l.trail[size_t(best) * 2];
                        const float dx = a[2] - a[0], dz = a[3] - a[1];
                        if (dx * dx + dz * dz > 1e-4f)   // (the step: x -sin, z cos of the heading)
                            rt.mem.set32(w.task + 0x30, uint32_t(int32_t(std::lround(std::atan2(-dx, dz) * 65536.0f / 6.2831853f))) & 0xFFFF);
                    }
                }
            }
        }
        const float step = std::hypot(c[0] - w.p[0], c[2] - w.p[2]);
        if (w.have && routine == A::F::enemy_main && step > 0.01f && step < 3.0f && size_t(w.tmpl) < pool_.size()) {
            pool_[size_t(w.tmpl)].walked += step;
            ++pool_[size_t(w.tmpl)].frames;
        }
        std::copy(c, c + 3, w.p);
        w.have = true;
    }
    if (rt.frameCount % 600 == 0)
        walkers_.erase(std::remove_if(walkers_.begin(), walkers_.end(), [](const Walker& w) { return w.task == 0; }), walkers_.end());
}

void HordeMode::spawned(Runtime& rt, uint32_t task, uint32_t rec)
{
    if (task == 0)
        return;
    const uint32_t kind = rt.mem.u32(rec);
    const bool play = rt.mem.u32(G::shot_enable) != 0 && rt.letterbox == 0;
    if ((kind == 0 || kind == 9) && play)
        track(rt, task);
    if (play)
        probeAdd(rt, task, kind, false);
    const float cx = rt.mem.f32(G::camera_pos), cz = rt.mem.f32(G::camera_pos + 8);
    const float px = rt.mem.f32(task + 0x20), py = rt.mem.f32(task + 0x24), pz = rt.mem.f32(task + 0x28);
    const float dist = std::hypot(px - cx, pz - cz);
    if (tracing() && play)
        std::printf("HSPAWN %llu kind %u type %d action %d sub %u life %d dist %.0f at %.1f %.1f %.1f\n", (unsigned long long)rt.frameCount,
                    kind, int(rt.mem.at<int16_t>(task + 0xCC)), int(rt.mem.at<int16_t>(task + 0x64)), rt.mem.u32(task + 0x324),
                    int(rt.mem.at<int16_t>(task + 0x70)), dist, px, py, pz);
    if (on())
        made.push_back({task, rt.mem.u32(task + 8), kind, rt.frameCount, false});   // (survfix: the creatures followed)
    if (!on() || !play)
        return;   // (a creature of a cut-scene: as the script has it)
    const int chapter = rt.mem.at<int8_t>(G::chapter), event = rt.mem.at<int8_t>(G::event_index);
    if (chapter != poolChapter_)
        pool_.clear(), walkers_.clear(), poolChapter_ = chapter, reserve_ = 0;
    if (event != lanesEvent_ || chapter != pendingChapter_) {
        dropPending(rt, -1);
        // the last event's lanes kept while a copy still walks one of their trails (into this event: the camera moved
        // on before it was shot), else cleared
        const bool walking = chapter == pendingChapter_ && std::any_of(walkers_.begin(), walkers_.end(), [&](const Walker& w) {
            return w.lane >= 0 && w.gen == lanesGen_ && alive(rt, w.task, w.serial);
        });
        if (!walking)
            lanes_.clear(), ++lanesGen_;
        lanesEvent_ = event;
    }
    pendingEvent_ = event, pendingChapter_ = chapter;
    // the standard walkers (kind 0: kind000_enemy_spawn, kind 9: kind009_enemy_spawn_b) get copies; the kit zombies
    // (32) and the agile ones (70) escorts; every other kind (the bosses, hostages, their scripted attackers, the scene
    // creatures) stays as the script has it
    const bool walker = kind == 0 || kind == 9;
    if (!walker && kind != 32 && kind != 70)
        return;
    appeared_.push_back(rt.frameCount);
    if (dist < kNearCam)
        appearedNear_.push_back(rt.frameCount);
    if (walker) {
        ++originals;
        Lane l;
        l.work.resize((kTaskSize - kWork) / 4);
        for (size_t i = 0; i < l.work.size(); ++i)   // the creature as the opcode set it (before it first ran)
            l.work[i] = rt.mem.u32(task + kWork + 4 * uint32_t(i));
        l.routine = rt.mem.u32(task);
        l.pos[0] = px, l.pos[1] = py, l.pos[2] = pz;
        l.type = rt.mem.at<int16_t>(task + 0xCC), l.action = rt.mem.at<int16_t>(task + 0x64), l.life = rt.mem.at<int16_t>(task + 0x70);
        l.sub = rt.mem.u32(task + 0x324);
        l.lastTask = task, l.lastSerial = rt.mem.u32(task + 8), l.lastAt = rt.frameCount;
        l.trailTask = task, l.trailSerial = l.lastSerial;
        const int tmpl = templateOf(l.routine, l.type, l.action, l.life, l.sub);
        if (tmpl >= 0)
            walkers_.push_back({task, l.lastSerial, tmpl, {0, 0, 0}, false, {px, pz}, l.work[(0x30 - kWork) / 4]});
        l.event = event;
        if (lanes_.size() >= 96)
            return;
        lanes_.push_back(std::move(l));
        // (the copies dropped before: the camera moved on before their turn, or no way known from their place: they come
        // with the next creatures; never counted while they wait for them, so no wait of the script waits for them)
        const int extra = std::min(reserve_, kCopies[size]);
        reserve_ -= extra;
        for (int i = 0; i < kCopies[size] + extra; ++i)
            queueCopy(rt, int(lanes_.size()) - 1, false);
        return;
    }
    // escorts: queued at one of the event's walker places in front of the camera (a far one when there is one)
    std::vector<int> far, front;
    for (size_t i = 0; i < lanes_.size(); ++i) {
        const Lane& l = lanes_[i];
        const float* m = rt.camera;
        if (l.event != event || m[2] * l.pos[0] + m[5] * l.pos[1] + m[8] * l.pos[2] + m[11] < 30.0f)
            continue;
        front.push_back(int(i));
        if (std::hypot(l.pos[0] - cx, l.pos[2] - cz) >= kNearSpawn)
            far.push_back(int(i));
    }
    const std::vector<int>& from = far.empty() ? front : far;
    if (from.empty())
        return;
    for (int i = 0; i < kEscorts[size]; ++i)
        queueCopy(rt, from[next() % from.size()], true);
}

void HordeMode::queueCopy(Runtime& rt, int lane, bool escort)
{
    Lane& l = lanes_[size_t(lane)];
    // a few made (and counted) at a time a place; the rest are its credit, made as those come (else the waiting
    // ones would fill the cap and the waits of the script with copies that come many seconds later)
    if (std::count_if(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.lane == lane; }) >= kPerPlace) {
        ++l.credit;
        if (escort)
            ++escorts;
        return;
    }
    // the cap: creatures alive now (the waiting copies are counted), and body slots for the script's own creatures
    const int count = std::max(0, int(rt.mem.at<int16_t>(G::enemy_count)));
    if (count + 1 > kCap || bodiesNow(rt) + int(pending_.size()) + 1 > 14 + rt.extraBodySlots - 4) {
        if (count + 1 > kCap && l.credit < kCopies[size]) {   // (full now: later, as the creatures fall)
            ++l.credit;
            return;
        }
        ++skipped;
        return;
    }
    const uint32_t t = rt.task_create(kTaskNopRoutine, kTaskSize);
    if (t == 0) {
        ++skipped;
        return;
    }
    for (uint32_t o = kWork; o < kTaskSize; o += 4)
        rt.mem.set32(t + o, l.work[(o - kWork) / 4]);
    // 0.5-3 s after the last one from the place (a near place's 1-3 s), the early end more often (a column, spread)
    const float r = float(next() % 1000) / 1000.0f;
    const float d = std::hypot(l.pos[0] - rt.mem.f32(G::camera_pos), l.pos[2] - rt.mem.f32(G::camera_pos + 8));
    const int gap = d < kNearSpawn ? 40 + int(50.0f * r * r) : 30 + int(30.0f * r * r * r);
    pending_.push_back({t, rt.mem.u32(t + 8), l.routine, lane, gap, rt.frameCount, 0, 1.0f, l.type});
    pendingPeak = std::max(pendingPeak, int(pending_.size()));
    countsAdd(rt, 1);
    ++copies;
    if (escort)
        ++escorts;
    if (tracing())
        std::printf("HCOPY %llu lane %d gap %d otype %d%s\n", (unsigned long long)rt.frameCount, lane, gap, int(l.type),
                    escort ? " escort" : "");
}

// the type a copy comes as (at its moment: the chapter's walkers seen by then): another of the chapter's walkers of
// the same set-up (spawn routine, action, start sub-state; the one-hit small ones among themselves; a walk speed not far
// from its creature's) 70 % of the time, its creature's otherwise. Its walk speed over its creature's (1: the same)
float HordeMode::pickType(Runtime& rt, const Lane& l, uint32_t t, int16_t& type)
{
    type = l.type;
    const int own = templateOf(l.routine, l.type, l.action, l.life, l.sub);
    const float v0 = own >= 0 ? pool_[size_t(own)].speed() : 0.0f;
    std::vector<const Template*> other;
    for (const Template& p : pool_) {
        if (p.routine != l.routine || p.action != l.action || p.sub != l.sub || p.type == l.type || (p.life <= 1) != (l.life <= 1))
            continue;
        const float v = p.speed();
        if (v0 > 0.0f && v > 0.0f && v / v0 > 0.75f && v / v0 < 1.33f)
            other.push_back(&p);
    }
    if (tracing())
        std::printf("HTYPE %llu type %d speed %.3f candidates %zu pool %zu\n", (unsigned long long)rt.frameCount, int(l.type), v0,
                    other.size(), pool_.size());
    // hordepub (user 2026-11-09: "Horde has none of the cut creatures ... add them to Horde's copies too, where the
    // original area's creatures fit"): with the arcade models (the PC files have no cut meshes), one copy in kCutCopy
    // of a creature with a cut relative is that cut creature, with Survival's set-up (survivalCuts: its own model and
    // motion list or its relative's, its hit points; survivalCutTables at Horde's start): Neil (28) -> Syndy (her look:
    // the tall ones; she walks with Cyril's axe set), Cyril (29, walking: action 0) -> Baba (his), Sam (11) -> Neozom (Sam's parts and list) or
    // Disiprin (the plain zombie set), Bentley and his variants -> Debu (Bentley's textures and lists)
    static const bool noCut = std::getenv("HOTD_HORDE_CUT") && std::string(std::getenv("HOTD_HORDE_CUT")) == "0";
    if (!rt.pcModels && !noCut && l.life > 1) {
        int pick[2] = {-1, -1}, n = 0;
        if (l.type == 28) pick[n++] = 10;
        else if (l.type == 29 && l.action == 0) pick[n++] = 66;
        else if (l.type == 11) pick[n++] = 56, pick[n++] = 52;
        else if (l.type == 33 || l.type == 45 || l.type == 51 || l.type == 65 || l.type == 72 || l.type == 89) pick[n++] = 12;
        if (n > 0 && next() % kCutCopy == 0) {
            const int ct = pick[next() % uint32_t(n)];
            for (const SurvivalCut& c : survivalCuts())
                if (c.type == ct) {
                    type = int16_t(ct);
                    rt.mem.at<int16_t>(t + 0xCC) = int16_t(ct);
                    rt.mem.at<int16_t>(t + 0x70) = int16_t(c.life);
                    rt.mem.at<int16_t>(t + 0x72) = int16_t(c.life);
                    rt.mem.at<int16_t>(t + 0x64) = int16_t(c.action);   // (its set-up: Syndy Cyril's axe row)
                    rt.mem.set32(t + 0x58, rt.mem.u32(t + 0x58) & ~(1u << 19));
                    ++cutCopies;
                    if (tracing())
                        std::printf("HCUT %llu type %d -> %s (%d)\n", (unsigned long long)rt.frameCount, int(l.type), c.name, ct);
                    return 1.0f;   // (its own walk speed; it follows its lane's trail as every copy)
                }
        }
    }
    if (other.empty() || next() % 100 >= 70)
        return 1.0f;
    const Template* tp = other[next() % other.size()];
    type = tp->type;
    rt.mem.at<int16_t>(t + 0xCC) = tp->type;
    rt.mem.at<int16_t>(t + 0x70) = tp->life;
    rt.mem.at<int16_t>(t + 0x72) = tp->life;
    uint32_t flags = rt.mem.u32(t + 0x58);
    if (tp->type != 30 && tp->type != 84)
        flags &= ~(1u << 19);   // (the headless ones' count: theirs only)
    rt.mem.set32(t + 0x58, flags);
    ++otherType;
    return tp->speed() / v0;
}

// the copies whose turn came: they run their spawn routine from the next frame (kind000 / kind009_enemy_spawn: the
// body, the count, the wait for the camera's stop); in each lane the first waiting one only, kBurst at most in
// kBurstFrames
void HordeMode::activate(Runtime& rt)
{
    // the copies set going last frame: their spawn routine has run (the delay set): the delay walk scaled to their speed
    for (const Fix& f : fix_)
        if (alive(rt, f.task, f.serial) && rt.mem.u32(f.task) != f.routine) {
            const int32_t dl = int32_t(rt.mem.u32(f.task + 0x2EC));
            if (dl > 0 && f.speed > 0.0f)
                rt.mem.set32(f.task + 0x2EC, uint32_t(std::max(0, int32_t(std::lround(float(dl) / f.speed)))));
        }
    fix_.clear();
    const int chapter = rt.mem.at<int8_t>(G::chapter), event = rt.mem.at<int8_t>(G::event_index);
    if (pending_.empty())
        return;
    if (chapter != pendingChapter_ || event != pendingEvent_ || rt.mem.u32(G::shot_enable) == 0 || rt.letterbox != 0 ||
        rt.mem.at<int16_t>(G::game_mode) != 4) {
        dropPending(rt, -1);
        return;
    }
    const auto recent = [&](std::vector<uint64_t>& v) {
        v.erase(std::remove_if(v.begin(), v.end(), [&](uint64_t f) { return f + kBurstFrames <= rt.frameCount; }), v.end());
        return int(v.size());
    };
    const float cam[3] = {rt.mem.f32(G::camera_pos), rt.mem.f32(G::camera_pos + 4), rt.mem.f32(G::camera_pos + 8)};
    const bool stopped = rt.mem.u32(G::camera_stopped) == 2;
    // the lanes' stops: the camera's place when a lane's creature first walks (its spawn routine's wait is over)
    for (Lane& l : lanes_)
        if (!l.camSet && stopped && alive(rt, l.lastTask, l.lastSerial) && rt.mem.u32(l.lastTask) == A::F::enemy_main) {
            std::copy(cam, cam + 3, l.cam);
            l.camSet = true;
        }
    // horde4: the walkers (the script's and the copies) in play within kNearCam of the camera (a near entrance's copies
    // wait while there are kNearCrowd[size] of them)
    int nearNow = 0;
    for (const Made& m : made)
        if ((m.copy || m.kind == 0 || m.kind == 9 || m.kind == 32 || m.kind == 70) && alive(rt, m.task, m.serial) &&
            rt.mem.u32(m.task) != kTaskNopRoutine &&
            std::hypot(rt.mem.f32(m.task + 0x20) - cam[0], rt.mem.f32(m.task + 0x28) - cam[2]) < kNearCam)
            ++nearNow;
    std::vector<char> busy(lanes_.size(), 0);   // (one a lane each frame, in their order)
    for (size_t k = 0; k < pending_.size();) {
        Pending& p = pending_[k];
        if (rt.mem.u32(p.task) != kTaskNopRoutine || rt.mem.u32(p.task + 8) != p.serial) {   // (freed with its group)
            countsAdd(rt, -1);
            pending_.erase(pending_.begin() + std::ptrdiff_t(k));
            continue;
        }
        if (p.lane < 0) {   // horde4: a placed one (the table): at its moment, while the camera is at its stop
            if (std::hypot(cam[0] - p.cam[0], cam[2] - p.cam[1]) > kCamMoved) {
                rt.mem.set32(p.task, kTaskUnlinkRoutine);
                countsAdd(rt, -1);
                ++dropped, --placed;
                pending_.erase(pending_.begin() + std::ptrdiff_t(k));
                continue;
            }
            const float px = rt.mem.f32(p.task + 0x20), pz = rt.mem.f32(p.task + 0x28);
            const bool nearCam = std::hypot(px - cam[0], pz - cam[2]) < kNearCam;
            if (rt.frameCount < p.at || recent(appeared_) >= kBurst || (nearCam && recent(appearedNear_) >= kBurstNear)) {
                ++k;
                continue;
            }
            countsAdd(rt, -1);   // (its spawn routine counts it)
            rt.mem.set32(p.task, p.routine);
            made.push_back({p.task, p.serial, 0, rt.frameCount, true});
            appeared_.push_back(rt.frameCount);
            if (nearCam)
                appearedNear_.push_back(rt.frameCount);
            if (tracing()) {
                std::printf("HPLACE %llu type %d at %.1f %.1f cam %.0f\n", (unsigned long long)rt.frameCount, int(p.type), px, pz,
                            std::hypot(px - cam[0], pz - cam[2]));
                track(rt, p.task);
            }
            probeAdd(rt, p.task, 0, true);
            pending_.erase(pending_.begin() + std::ptrdiff_t(k));
            continue;
        }
        Lane& l = lanes_[size_t(p.lane)];
        if (busy[size_t(p.lane)]) {
            ++k;
            continue;
        }
        busy[size_t(p.lane)] = 1;
        // the camera has left the stop the lane's creatures walk to: from there they would walk to another place
        if (l.camSet && std::hypot(cam[0] - l.cam[0], cam[1] - l.cam[1], cam[2] - l.cam[2]) > kCamMoved) {
            if (tracing())
                std::printf("HDROP %llu lane %d camera moved %.1f stopped %d waiting %zu\n", (unsigned long long)rt.frameCount, p.lane,
                            std::hypot(cam[0] - l.cam[0], cam[1] - l.cam[1], cam[2] - l.cam[2]), int(stopped), pending_.size());
            dropPending(rt, p.lane);
            k = 0;
            std::fill(busy.begin(), busy.end(), char(0));
            continue;
        }
        // its creature was shot before it walked off its place (a few steps at most): no way known from there; the copy
        // comes from another place of the event whose creature walked (the nearest), else as a pioneer (below)
        bool pioneer = false;
        if (l.trailDone && l.trail.size() / 2 < kTrailMin) {
            int to = -1;
            float best = 1e30f;
            for (size_t o = 0; o < lanes_.size(); ++o) {
                const Lane& m = lanes_[o];
                const float d = std::hypot(m.pos[0] - l.pos[0], m.pos[2] - l.pos[2]);
                // (horde4: only a place of this stop, near this one: an event spans several stops in Chapter 3, and a
                // copy handed to the last stop's place was dropped there the next frame, the camera gone from it)
                if (m.camSet && std::hypot(cam[0] - m.cam[0], cam[1] - m.cam[1], cam[2] - m.cam[2]) > kCamMoved)
                    continue;
                if (int(o) != p.lane && m.event == l.event && m.trail.size() / 2 >= kTrailMin && d < kMoveNear && d < best)
                    best = d, to = int(o);
            }
            if (tracing() && to >= 0)
                std::printf("HMOVE %llu lane %d -> %d (%.0f units)\n", (unsigned long long)rt.frameCount, p.lane, to, best);
            if (to >= 0) {
                for (uint32_t o = kWork; o < kTaskSize; o += 4)
                    rt.mem.set32(p.task + o, lanes_[size_t(to)].work[(o - kWork) / 4]);
                p.lane = to, p.routine = lanes_[size_t(to)].routine, p.type = lanes_[size_t(to)].type;
                ++moved;
                ++k;
                continue;
            }
            // horde4: no creature of the event has walked: this copy is its creature again (its type, its speed) and
            // walks the record's own way; its steps are the lane's new trail (horde3 waited 2 s here, counted, then dropped it)
            pioneer = true;
        }
        // the place: free once the last creature from it walked off (or is gone), and its gap passed
        const bool last = alive(rt, l.lastTask, l.lastSerial);
        const float off = last ? std::hypot(rt.mem.f32(l.lastTask + 0x20) - l.pos[0], rt.mem.f32(l.lastTask + 0x28) - l.pos[2]) : 1e9f;
        // (and its creature's way known that far: its trail kTrailMin long, or it still walks it)
        if ((last && off < kClear) || (!pioneer && !l.trailDone && l.trail.size() / 2 < kTrailMin)) {
            // its creature stands there (no free attack place in front: the game's walkers wait where they are): after
            // a while the copy comes from another place of the event that is free (its creature walked on)
            if (stopped && p.stuck >= kTaken) {
                int to = -1;
                float best = 1e30f;
                for (size_t o = 0; o < lanes_.size(); ++o) {
                    const Lane& m = lanes_[o];
                    if (int(o) == p.lane || m.event != l.event || m.trail.size() / 2 < kTrailMin || busy[o] ||
                        rt.frameCount < m.lastAt + 30 ||
                        (m.camSet && std::hypot(cam[0] - m.cam[0], cam[1] - m.cam[1], cam[2] - m.cam[2]) > kCamMoved))
                        continue;
                    if (alive(rt, m.lastTask, m.lastSerial) &&
                        std::hypot(rt.mem.f32(m.lastTask + 0x20) - m.pos[0], rt.mem.f32(m.lastTask + 0x28) - m.pos[2]) < kClear)
                        continue;
                    const float d = std::hypot(m.pos[0] - l.pos[0], m.pos[2] - l.pos[2]);
                    if (d < kMoveNear && d < best)   // (horde4: not from another room)
                        best = d, to = int(o);
                }
                if (to >= 0) {
                    if (tracing())
                        std::printf("HMOVE %llu lane %d -> %d (%.0f units, its place taken)\n", (unsigned long long)rt.frameCount, p.lane, to, best);
                    for (uint32_t o = kWork; o < kTaskSize; o += 4)
                        rt.mem.set32(p.task + o, lanes_[size_t(to)].work[(o - kWork) / 4]);
                    p.lane = to, p.routine = lanes_[size_t(to)].routine, p.type = lanes_[size_t(to)].type, p.stuck = 0;
                    ++moved;
                    ++k;
                    continue;
                }
            }
            if (stopped && ++p.stuck > kStuck) {   // (it stays there: this copy never comes)
                if (tracing())
                    std::printf("HDROP %llu lane %d place taken\n", (unsigned long long)rt.frameCount, p.lane);
                rt.mem.set32(p.task, kTaskUnlinkRoutine);
                countsAdd(rt, -1);
                ++dropped, --copies, reserve_ = std::min(reserve_ + 1, kReserve[size]);
                pending_.erase(pending_.begin() + std::ptrdiff_t(k));
                continue;
            }
            ++k;
            continue;
        }
        if (rt.frameCount < l.lastAt + uint64_t(p.gap)) {
            ++k;
            continue;
        }
        const float d0 = std::hypot(l.pos[0] - cam[0], l.pos[2] - cam[2]);
        // (user 2026-11-05, screenshots on Chapter 1's bridge: "where they come from does not make sense", three or four
        // creatures piled up at arm's length): an entrance this near the camera (most of the arcade's own: its creatures
        // step out close by) sends one copy at a time: the next only once the one before it from there is gone (horde4:
        // or kNearOff units on its way), so they never stack on one spot; and only kNearCopies[size] of them in all (horde4: 2 / 4 / 6; horde3's 1 / 1 / 2 left
        // Horde thinner than the plain game), and only while fewer than kNearCrowd[size] creatures are near the camera
        // (horde4: none from a place within kArmsLength: the creatures that start beside the camera, Chapter 3's lab
        // tables, Chapter 2's: a copy there is one at arm's length out of nowhere; they go with the next creatures)
        if (d0 < kNoCopyNear && l.nearSent >= (d0 < kArmsLength ? 0 : kNearCopies[size])) {
            rt.mem.set32(p.task, kTaskUnlinkRoutine);
            countsAdd(rt, -1);
            ++dropped, --copies, reserve_ = std::min(reserve_ + 1, kReserve[size]);
            pending_.erase(pending_.begin() + std::ptrdiff_t(k));
            continue;
        }
        if (d0 < kNoCopyNear && ((last && off < kNearOff) || nearNow >= kNearCrowd[size])) {
            ++k;
            continue;
        }
        const bool nearCam = d0 < kNearCam;
        if (recent(appeared_) >= kBurst || (nearCam && recent(appearedNear_) >= kBurstNear)) {
            ++burstHeld;
            ++k;
            continue;
        }
        if (d0 < kNoCopyNear)   // (horde4: counted once it comes: horde3 counted every frame the burst limit held it)
            ++l.nearSent, ++nearNow;
        countsAdd(rt, -1);   // (its spawn routine counts it)
        if (pioneer) {   // (the lane's trail from its steps: trailsTick)
            p.type = l.type, p.speed = 1.0f;
            l.trail.clear(), l.trailDone = false, l.trailTask = p.task, l.trailSerial = p.serial;
            ++pioneers;
        } else {
            p.speed = pickType(rt, l, p.task, p.type);
        }
        if (const int tm = templateOf(l.routine, p.type, l.action, rt.mem.at<int16_t>(p.task + 0x70), l.sub); tm >= 0)
            walkers_.push_back({p.task, p.serial, tm, {0, 0, 0}, false, {l.pos[0], l.pos[2]}, l.work[(0x30 - kWork) / 4], pioneer ? -1 : p.lane, 0, lanesGen_, pioneer}),
                trailFollowers += pioneer ? 0 : 1;
        rt.mem.set32(p.task, p.routine);
        if (std::abs(p.speed - 1.0f) > 0.01f)
            fix_.push_back({p.task, p.serial, p.routine, p.speed});
        l.lastTask = p.task, l.lastSerial = p.serial, l.lastAt = rt.frameCount;
        // survfix's never-attack rule covers it from now on (a waiting copy is never in `made`: never retired as idle)
        made.push_back({p.task, p.serial, 0, rt.frameCount, true});
        appeared_.push_back(rt.frameCount);
        if (nearCam)
            appearedNear_.push_back(rt.frameCount);
        if (tracing()) {
            std::printf("HAPPEAR %llu after %llu cam %.0f lane %d gap %d type %d last %s %.0f task %X%s\n", (unsigned long long)rt.frameCount,
                        (unsigned long long)(rt.frameCount - p.made), d0, p.lane, p.gap, int(p.type), last ? "off" : "gone",
                        last ? off : -1.0f, p.task, pioneer ? " pioneer" : "");
            track(rt, p.task);
        }
        probeAdd(rt, p.task, 0, true);
        pending_.erase(pending_.begin() + std::ptrdiff_t(k));
    }
    // the places' credit: the next copies made as the waiting ones come (a few a place at a time)
    for (size_t i = 0; i < lanes_.size(); ++i) {
        Lane& l = lanes_[i];
        while (l.credit > 0 && l.event == pendingEvent_ && int(rt.mem.at<int16_t>(G::enemy_count)) + 1 <= kCap &&
               std::count_if(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.lane == int(i); }) < kPerPlace) {
            --l.credit;
            const size_t before = pending_.size();
            queueCopy(rt, int(i), false);
            if (pending_.size() == before)
                break;
        }
    }
}

// horde4: the placement table (horde_places.inc): at a stop of the table (the camera's place and look when it stops),
// its creatures made at once (counted: the script's waits wait for them; waiting unseen) and set going at their moment
// (activate: the burst limit; dropped if the camera leaves the stop first). Low: every other one of a stop
namespace {
struct Place { int chapter; float cx, cz, lx, lz; int delay; float x, y, z; int type; };
const Place kPlaces[] = {
#include "game/horde_places.inc"
};
constexpr int kChapterTypes[4][4] = {{11, 28, 16, 29}, {11, 28, 16, 29}, {28, 61, 62, 73}, {28, 53, 72, 83}};
int placeLife(int type)
{
    switch (type) {
    case 11: return 100; case 16: return 200; case 28: return 120; case 29: return 160; case 53: return 210;
    case 61: return 90; case 62: return 150; case 72: return 200; case 73: return 150; case 83: return 150;
    default: return 120;
    }
}
} // namespace

void HordeMode::placeAtStop(Runtime& rt)
{
    const uint32_t st = rt.mem.u32(G::camera_stopped);
    const bool arrived = st == 2 && lastStopped_ != 2;
    lastStopped_ = st;
    const int chapter = rt.mem.at<int8_t>(G::chapter);
    if (chapter != placedChapter_)
        placedStops_.clear(), placedChapter_ = chapter;
    if (!arrived || rt.mem.u32(G::shot_enable) == 0 || rt.letterbox != 0)
        return;
    const float* m = rt.camera;
    const float cw[3] = {-(m[0] * m[9] + m[1] * m[10] + m[2] * m[11]), -(m[3] * m[9] + m[4] * m[10] + m[5] * m[11]),
                         -(m[6] * m[9] + m[7] * m[10] + m[8] * m[11])};
    const float ll = std::max(1e-4f, std::hypot(m[2], m[8]));
    int n = 0;
    for (size_t i = 0; i < std::size(kPlaces); ++i) {
        const Place& p = kPlaces[i];
        if (p.chapter != chapter || std::hypot(cw[0] - p.cx, cw[2] - p.cz) > 12.0f || (m[2] * p.lx + m[8] * p.lz) / ll < 0.95f)
            continue;
        // (each stop once a chapter: its first line's number)
        const int stop = int(&p - kPlaces) - [&] { int k = 0; while (int(i) - k > 0 && kPlaces[i - size_t(k) - 1].cx == p.cx &&
                                                                  kPlaces[i - size_t(k) - 1].cz == p.cz && kPlaces[i - size_t(k) - 1].lx == p.lx) ++k; return k; }();
        if (std::find(placedStops_.begin(), placedStops_.end(), stop) != placedStops_.end() && n == 0)
            return;
        if (n == 0)
            placedStops_.push_back(stop);
        ++n;
        if (size == 1 && n % 2 == 0)
            continue;   // (Low: every other one)
        const int count = std::max(0, int(rt.mem.at<int16_t>(G::enemy_count)));
        if (count + 1 > kCap || bodiesNow(rt) + int(pending_.size()) + 1 > 14 + rt.extraBodySlots - 4) {
            ++skipped;
            continue;
        }
        int type = p.type;
        bool cut = false;
        for (const SurvivalCut& c : survivalCuts())
            cut = cut || c.type == type;
        if (type < 0 || (cut && rt.pcModels))   // (the PC files have no cut creatures' meshes)
            type = kChapterTypes[std::clamp(chapter, 0, 3)][next() % 4], cut = false;
        int life = placeLife(type);
        if (cut)
            for (const SurvivalCut& c : survivalCuts())
                if (c.type == type) life = c.life;
        int action = !cut && (type == 29 || type == 30) ? 1 : 0;
        for (const SurvivalCut& c : survivalCuts())
            if (cut && c.type == type) action = c.action;   // (hordepub: its own set-up)
        const float pos[3] = {p.x, p.y, p.z};
        // facing the camera (the step: x -sin, z cos of the heading), walking in (start sub-state 2)
        const int32_t ang[3] = {0, int32_t(std::lround(std::atan2(-(cw[0] - p.x), cw[2] - p.z) * 65536.0f / 6.2831853f)) & 0xFFFF, 0};
        placeCheck(rt, int(i), pos, cw);
        const uint32_t t = rt.survival_spawn(0, 1, pos, ang, type, action, life, 0, 2);
        if (t == 0) {
            ++skipped;
            continue;
        }
        const uint32_t routine = rt.mem.u32(t);
        rt.mem.set32(t, kTaskNopRoutine);
        Pending q{t, rt.mem.u32(t + 8), routine, -1, 0, rt.frameCount, 0, 1.0f, int16_t(type)};
        q.at = rt.frameCount + uint64_t(p.delay), q.cam[0] = cw[0], q.cam[1] = cw[2];
        pending_.push_back(q);
        pendingPeak = std::max(pendingPeak, int(pending_.size()));
        countsAdd(rt, 1);
        ++placed;
        placedCut += cut ? 1 : 0;
    }
    if (n > 0) {
        pendingEvent_ = rt.mem.at<int8_t>(G::event_index), pendingChapter_ = chapter;
        if (tracing())
            std::printf("HSTOPPLACE %llu chapter %d camera %.1f %.1f: %d placed\n", (unsigned long long)rt.frameCount, chapter, cw[0], cw[2], n);
    }
}

// hordepub (user 2026-11-09: "some creatures should already be there instead of all arriving late: when a stop starts,
// part of its copies already standing in place ... only on walkable ground the original creatures use, never in
// walls"): the stand table (horde_stands.inc, docs/horde.md "Standing"): for each camera stop of the plain game (its
// place and look when it stops), up to 4 points where the arcade's own walkers walked while the camera was there (in
// front of it, 30-120 units away, past their doors: measured, stands.py). At such a stop, kStands[size] of them (the
// nearest first) get a creature at once, facing the camera, walking in (start sub-state 2): the chapter's own walker
// type when the walker there was one, else one of them; counted (the script's waits wait for them), as the placement
// table's are (activate: the burst limit; gone if the camera leaves first); each stop once a chapter
namespace {
struct Stand { int chapter; float cx, cz, lx, lz, x, y, z; int type; };
const Stand kStandTable[] = {
#include "game/horde_stands.inc"
};
constexpr int kStands[4] = {0, 1, 2, 3};
} // namespace

void HordeMode::standAtStop(Runtime& rt)
{
    const uint32_t st = rt.mem.u32(G::camera_stopped);
    const bool arrived = st == 2 && standStopped_ != 2;
    standStopped_ = st;
    const int chapter = rt.mem.at<int8_t>(G::chapter);
    if (chapter != standChapter_)
        standStops_.clear(), standChapter_ = chapter;
    if (!arrived || rt.mem.u32(G::shot_enable) == 0 || rt.letterbox != 0 || rt.mem.at<int16_t>(G::game_mode) != 4)
        return;
    const float* m = rt.camera;
    const float cw[3] = {-(m[0] * m[9] + m[1] * m[10] + m[2] * m[11]), -(m[3] * m[9] + m[4] * m[10] + m[5] * m[11]),
                         -(m[6] * m[9] + m[7] * m[10] + m[8] * m[11])};
    const float ll = std::max(1e-4f, std::hypot(m[2], m[8]));
    int n = 0, made = 0;
    for (size_t i = 0; i < std::size(kStandTable); ++i) {
        const Stand& p = kStandTable[i];
        if (p.chapter != chapter || std::hypot(cw[0] - p.cx, cw[2] - p.cz) > 12.0f || (m[2] * p.lx + m[8] * p.lz) / ll < 0.95f)
            continue;
        const int stop = int(std::lround(p.cx * 7.0f + p.cz * 13.0f + p.lx * 1000.0f));   // (the stop's key)
        if (n == 0 && std::find(standStops_.begin(), standStops_.end(), stop) != standStops_.end())
            return;
        if (n == 0)
            standStops_.push_back(stop);
        ++n;
        {   // (the wall test, HOTD_HORDE_WALLS with --render: every point of the stop checked, its way to the camera)
            const float pos[3] = {p.x, p.y, p.z};
            placeCheck(rt, 1000 + int(i), pos, cw);
        }
        if (made >= kStands[size])
            continue;
        const int count = std::max(0, int(rt.mem.at<int16_t>(G::enemy_count)));
        if (count + 1 > kCap || bodiesNow(rt) + int(pending_.size()) + 1 > 14 + rt.extraBodySlots - 4) {
            ++skipped;
            continue;
        }
        const int* types = kChapterTypes[std::clamp(chapter, 0, 3)];
        int type = p.type;
        if (std::find(types, types + 4, type) == types + 4)
            type = types[next() % 4];
        const int action = type == 29 || type == 30 ? 1 : 0;
        const float pos[3] = {p.x, p.y, p.z};
        const int32_t ang[3] = {0, int32_t(std::lround(std::atan2(-(cw[0] - p.x), cw[2] - p.z) * 65536.0f / 6.2831853f)) & 0xFFFF, 0};
        const uint32_t t = rt.survival_spawn(0, 1, pos, ang, type, action, placeLife(type), 0, 2);
        if (t == 0) {
            ++skipped;
            continue;
        }
        const uint32_t routine = rt.mem.u32(t);
        rt.mem.set32(t, kTaskNopRoutine);
        Pending q{t, rt.mem.u32(t + 8), routine, -1, 0, rt.frameCount, 0, 1.0f, int16_t(type)};
        q.at = rt.frameCount + uint64_t(made) * 6, q.cam[0] = cw[0], q.cam[1] = cw[2];
        pending_.push_back(q);
        pendingPeak = std::max(pendingPeak, int(pending_.size()));
        countsAdd(rt, 1);
        ++made, ++stood, ++placed;   // (placed: as the table's, its drop counts it back)
        if (tracing())
            std::printf("HSTAND %llu chapter %d type %d at %.1f %.1f %.1f cam %.0f\n", (unsigned long long)rt.frameCount, chapter, type, p.x, p.y, p.z,
                        std::hypot(p.x - cw[0], p.z - cw[2]));
    }
    if (made > 0)
        pendingEvent_ = rt.mem.at<int8_t>(G::event_index), pendingChapter_ = chapter;
}

// the waiting copies dropped (a lane's: -1 all): freed, uncounted
void HordeMode::dropPending(Runtime& rt, int lane)
{
    if (tracing() && lane < 0 && !pending_.empty())
        std::printf("HDROP %llu all %zu event %d shots %u letterbox %d mode %d\n", (unsigned long long)rt.frameCount, pending_.size(),
                    int(rt.mem.at<int8_t>(G::event_index)), rt.mem.u32(G::shot_enable), int(rt.letterbox),
                    int(rt.mem.at<int16_t>(G::game_mode)));
    for (size_t i = 0; i < lanes_.size(); ++i)   // (the credit too: with the next creatures)
        if (lane < 0 || int(i) == lane) {
            reserve_ = std::min(reserve_ + lanes_[i].credit, kReserve[size]);
            dropped += lanes_[i].credit;
            lanes_[i].credit = 0;
        }
    for (size_t k = 0; k < pending_.size();) {
        const Pending& p = pending_[k];
        if (lane >= 0 && p.lane != lane) {
            ++k;
            continue;
        }
        if (rt.mem.u32(p.task) == kTaskNopRoutine && rt.mem.u32(p.task + 8) == p.serial)
            rt.mem.set32(p.task, kTaskUnlinkRoutine);
        countsAdd(rt, -1);
        ++dropped, --copies, reserve_ = std::min(reserve_ + 1, kReserve[size]);
        pending_.erase(pending_.begin() + std::ptrdiff_t(k));
    }
}

void HordeMode::killed(Runtime& rt, uint32_t obj, int killer)
{
    if (!on())
        return;
    const int seat = killer >= 0 && killer < 4 ? killer : 0;
    Chain& c = chain[seat];
    ++c.kills;
    c.timer = kChainFrames;
    c.mult = std::min(5, 1 + c.kills / 5);
    c.multi = c.multiTimer > 0 ? c.multi + 1 : 1;
    c.multiTimer = kMultiFrames;
    if (c.multi >= 2)
        c.banner = c.multi, c.bannerAge = 0;
    bestChain = std::max(bestChain, c.kills), bestMulti = std::max(bestMulti, c.multi);
    const int points = 100 * c.mult;
    if (tracing())   // (qa1, testing: whose kill, the seat's chain)
        std::printf("HKILL %llu seat %d chain %d x%d\n", (unsigned long long)rt.frameCount, seat, c.kills, c.mult);
    rt.add_score(seat, points);
    bonusPoints += points;
    // the pop: where the creature was last drawn (its camera-space middle, Obj +0x38), on the arcade screen
    const float v[3] = {rt.mem.f32(obj + 0x38), rt.mem.f32(obj + 0x3C), rt.mem.f32(obj + 0x40)};
    if (v[2] > 5.0f) {
        if (pops.size() >= 24)
            pops.erase(pops.begin());
        pops.push_back({std::clamp(v[0] * 496.0f / v[2], -236.0f, 236.0f), std::clamp(v[1] * 496.0f / v[2] + 12.0f, -150.0f, 150.0f),
                        points, 0, seat});
    }
}

void HordeMode::feedbackTick(Runtime& rt)
{
    for (Pop& p : pops)
        ++p.age;
    pops.erase(std::remove_if(pops.begin(), pops.end(), [](const Pop& p) { return p.age >= kPopFrames; }), pops.end());
    // hordepub: the upgrades' announcement ages only while it can be seen (play, the HUD's own conditions: Hud::drawHorde)
    if (announceAge >= 0 && rt.mem.at<int16_t>(G::game_mode) == 4 && rt.mem.at<uint8_t>(G::scene_state) == 2 && !rt.clear.active &&
        ++announceAge >= kAnnounceFrames)
        announceAge = -1;
    for (int s = 0; s < 4; ++s) {
        Chain& c = chain[s];
        const int lives = rt.mem.at<port::a::Player>(rt.player_rec(s)).lives;
        if (c.lives >= 0 && lives < c.lives)
            c.timer = 0;   // (a life lost: the chain ends)
        c.lives = lives;
        if (c.timer > 0 && --c.timer == 0)
            c.kills = 0, c.mult = 1;
        if (c.multiTimer > 0)
            --c.multiTimer;
        if (c.banner > 0 && ++c.bannerAge >= kBannerFrames)
            c.banner = 0;
    }
}

// hordepub: a crate / breakable prop broken for good (its last break step): one in kPropChance a weapon pickup in front
// of it (8 units nearer the camera, 6 up: over its own life item when it drops one), while the shots are on
void HordeMode::propBroken(Runtime& rt, uint32_t obj)
{
    if (!on() || !rt.weapons.live || rt.mem.u32(G::shot_enable) == 0)
        return;
    const bool upgrade = next() % kUpgradeChance == 0;   // (hordepub: an upgrade, else one in kPropChance a weapon)
    if (!upgrade && next() % kPropChance != 0)
        return;
    const float v[3] = {rt.mem.f32(obj + 0x38), rt.mem.f32(obj + 0x3C), rt.mem.f32(obj + 0x40)};
    const float d = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (v[2] < 12.0f || d < 1.0f)
        return;
    const float pull = std::max(0.5f, 1.0f - 8.0f / d);
    const float c[3] = {v[0] * pull, v[1] * pull + 6.0f, v[2] * pull};
    if (upgrade) {
        if (rt.weapons.dropAt(rt, c, kPistol, Weapons::kPickupFrames, false, "a crate", true))
            ++propUpgrades;
    } else if (const int w = rt.weapons.pickDrop(); w >= 0 && rt.weapons.dropAt(rt, c, w, Weapons::kPickupFrames, false, "a crate")) {
        ++propDrops;
    }
}

// hordepub: the chapter's upgrades. Each frame of the chapter: who plays (state 5) and who lost a life; at its clear
// (Runtime::clear turning active, kind 176's results) each seat that played: an upgrade for every researcher of the
// chapter saved (hostages_rescued_chapter = the researchers that appeared since the chapter began, one at least) and
// one for no life lost, announced
void HordeMode::awardsTick(Runtime& rt)
{
    const int chapter = rt.mem.at<int8_t>(G::chapter);
    const int seats = std::min(4, rt.seat_count());
    if (chapter != awardChapter_) {
        awardChapter_ = chapter, appearedAt_ = rt.mem.at<int16_t>(G::hostages_appeared);
        for (int s = 0; s < 4; ++s)
            played_[s] = hurt_[s] = false, livesSeen_[s] = -1;
    }
    for (int s = 0; s < seats; ++s) {
        const port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(s));
        if (pl.state == 5 && rt.mem.at<int16_t>(G::game_mode) == 4) {
            played_[s] = true;
            if (livesSeen_[s] >= 0 && pl.lives < livesSeen_[s])
                hurt_[s] = true;
            livesSeen_[s] = pl.lives;
        } else if (pl.lives <= 0 && played_[s]) {
            hurt_[s] = true;   // (out of lives: a life was lost)
        }
    }
    const bool was = clearSeen_;   // (the clear's first frame only: the chapter number changes while it shows)
    clearSeen_ = rt.clear.active;
    if (!rt.clear.active || was)
        return;
    const int appeared = rt.mem.at<int16_t>(G::hostages_appeared) - appearedAt_;
    const int rescued = rt.mem.at<int16_t>(G::hostages_rescued_chapter);
    const bool saved = appeared > 0 && rescued >= appeared;
    std::string got;
    bool anyClean = false;
    for (int s = 0; s < seats; ++s) {
        if (!played_[s])
            continue;
        std::string mine;
        if (saved) {
            mine = rt.weapons.givePerk(rt, s);
            ++savedUpgrades;
        }
        if (!hurt_[s]) {
            mine += (mine.empty() ? "" : " + ") + rt.weapons.givePerk(rt, s);
            ++cleanUpgrades;
            anyClean = true;
        }
        if (!mine.empty())
            got += (got.empty() ? "" : "  ") + std::to_string(s + 1) + "P " + mine;
    }
    if (rt.log)
        rt.log("horde: chapter " + std::to_string(chapter + 1) + " clear: researchers " + std::to_string(rescued) + " of " + std::to_string(appeared) +
               (saved ? " (all saved)" : "") + (anyClean ? ", no damage" : "") + (got.empty() ? "" : ": " + got));
    if (got.empty())
        return;
    announce[0] = saved && anyClean ? "ALL SAVED + NO DAMAGE" : saved ? "ALL RESEARCHERS SAVED" : "NO DAMAGE";
    announce[1] = got;
    announceAge = 0;
}

// hordepub: each researcher rescued (hostages_rescued_chapter up by one): a weapon pickup in front of the camera at
// its next stop (a little low; its time held while the shots are off); Chapter 4's bonus room (event 3: the
// route kind191_route_by_rescues takes when every researcher was saved): kBonusDelay frames into its play (the room),
// kBonusWeapons different weapons across the room, 20 s
void HordeMode::giftsTick(Runtime& rt)
{
    const int chapter = rt.mem.at<int8_t>(G::chapter), event = rt.mem.at<int8_t>(G::event_index);
    const int rescued = rt.mem.at<int16_t>(G::hostages_rescued_chapter);
    if (chapter != giftChapter_)
        giftChapter_ = chapter, rescuedSeen_ = rescued, bonusDone_ = false, bonusPlay_ = 0, giftsDue_ = 0;
    if (!rt.weapons.on())
        return;
    if (rescued > rescuedSeen_)
        giftsDue_ += rescued - rescuedSeen_;
    rescuedSeen_ = rescued;
    // (given at the camera's next stop after the rescue, the shots on: the rescue's scene goes on with the shots on, the
    // researcher walking to the camera, and the camera moves on after it)
    const uint32_t stopped = rt.mem.u32(G::camera_stopped);
    const bool arrived = stopped == 2 && giftStopped_ != 2;
    giftStopped_ = stopped;
    if (giftsDue_ > 0 && arrived && rt.mem.u32(G::shot_enable) != 0 && rt.letterbox == 0 && rt.mem.at<int16_t>(G::game_mode) == 4) {
        for (int k = 0; k < giftsDue_; ++k) {
            const float c[3] = {(float(k) - float(giftsDue_ - 1) * 0.5f) * 14.0f, -5.0f, 45.0f};
            if (const int w = rt.weapons.pickDrop(); w >= 0 && rt.weapons.dropAt(rt, c, w, 600, true, "a researcher saved"))
                ++rescueDrops;
        }
        giftsDue_ = 0;
    }
    // (the camera walks through the dark corridor into the room and turns about it without a stop: kBonusDelay frames
    // of play into the event it looks across the room's floor, its first-aid kits at the sides)
    if (chapter == 3 && event == 3 && !bonusDone_ && rt.mem.u32(G::shot_enable) != 0 && rt.mem.at<int16_t>(G::game_mode) == 4 &&
        ++bonusPlay_ >= kBonusDelay) {
        bonusDone_ = true;
        bool used[kWeapons] = {};
        for (int i = 0, tries = 0; i < kBonusWeapons && tries < 40; ++tries) {
            const int w = rt.weapons.pickDrop();
            if (w < 0 || used[w])
                continue;
            used[w] = true;
            const float c[3] = {float(i - 1) * 16.0f, -6.0f, 45.0f};
            if (rt.weapons.dropAt(rt, c, w, 1200, true, "the bonus room"))
                ++bonusDrops;
            ++i;
        }
    }
}

void HordeMode::tick(Runtime& rt)
{
    // feel (testing, HOTD_HORDE_TRACE=N): the creatures alive / body slots / shots on every Nth tick
    static const int trace = std::getenv("HOTD_HORDE_TRACE") ? std::max(1, std::atoi(std::getenv("HOTD_HORDE_TRACE"))) : 0;
    if (on()) {
        // the Remake placements (horde4's table) are off (user 2026-11-05: "Horde does not make sense at all", the cheap
        // fix first): only copies from the arcade's own entrances; HOTD_HORDE_PLACES=1 brings the table back for testing
        static const bool places = std::getenv("HOTD_HORDE_PLACES") != nullptr && std::string(std::getenv("HOTD_HORDE_PLACES")) != "0";
        if (places)
            placeAtStop(rt);
        // hordepub: the stand table (on; HOTD_HORDE_STANDS=0 leaves it out: the before / after runs)
        static const bool stands = !(std::getenv("HOTD_HORDE_STANDS") && std::string(std::getenv("HOTD_HORDE_STANDS")) == "0");
        if (stands)
            standAtStop(rt);
        activate(rt);
        feedbackTick(rt);
        giftsTick(rt);
        awardsTick(rt);
        trailsTick(rt);
        walkersTick(rt);
    }
    wallProbe(rt);
    // horde4 (testing, HOTD_HORDE_TRACE): each camera stop (camera_stopped turning 2): `HSTOP frame chapter event camera
    // x y z look x z`; HOTD_HORDE_PICK=F:Y:u,v;u,v...: at frame F the floor point (plane y = Y) under each arcade-screen
    // point (u, v: centre 0, y up, 496 the focal length) for the placement table
    if (tracing()) {
        static uint32_t lastStopped = 0;
        const uint32_t st = rt.mem.u32(G::camera_stopped);
        const float* m = rt.camera;
        // the camera's place: -R^T t (camera = R world + t; m: column-major 3 x 3, then t)
        const float cw[3] = {-(m[0] * m[9] + m[1] * m[10] + m[2] * m[11]), -(m[3] * m[9] + m[4] * m[10] + m[5] * m[11]),
                             -(m[6] * m[9] + m[7] * m[10] + m[8] * m[11])};
        if (st == 2 && lastStopped != 2)
            std::printf("HSTOP %llu chapter %d event %d camera %.1f %.1f %.1f look %.3f %.3f\n", (unsigned long long)rt.frameCount,
                        int(rt.mem.at<int8_t>(G::chapter)), int(rt.mem.at<int8_t>(G::event_index)), cw[0], cw[1], cw[2], m[2], m[8]);
        lastStopped = st;
        if (const char* pk = std::getenv("HOTD_HORDE_PICK"); pk && std::strtoull(pk, nullptr, 10) == rt.frameCount) {
            const char* p = std::strchr(pk, ':');
            const float y = p ? float(std::atof(p + 1)) : 0.0f;
            p = p ? std::strchr(p + 1, ':') : nullptr;
            while (p && *p) {
                float u = 0, v = 0;
                if (std::sscanf(p + 1, "%f,%f", &u, &v) != 2)
                    break;
                const float dc[3] = {u / 496.0f, v / 496.0f, 1.0f};
                const float dw[3] = {m[0] * dc[0] + m[1] * dc[1] + m[2] * dc[2], m[3] * dc[0] + m[4] * dc[1] + m[5] * dc[2],
                                     m[6] * dc[0] + m[7] * dc[1] + m[8] * dc[2]};
                const float k = std::abs(dw[1]) > 1e-6f ? (y - cw[1]) / dw[1] : -1.0f;
                std::printf("HPICK %llu (%.0f, %.0f) -> %.1f %.1f %.1f%s\n", (unsigned long long)rt.frameCount, u, v, cw[0] + dw[0] * k, y,
                            cw[2] + dw[2] * k, k > 0 ? "" : " (behind)");
                p = std::strchr(p + 1, ';');
            }
        }
    }
    if (trace && rt.frameCount % uint64_t(trace) == 0) {
        // the tracked walkers: alive, moved since the last line (> 2 units), standing far (camera z > 60), standing near
        int alive = 0, moving = 0, standFar = 0, standNear = 0;
        for (Tracked& k : gTracked) {
            if (k.gone)
                continue;
            if (rt.mem.u32(k.task) == 0 || int32_t(rt.mem.u32(k.task + 8)) != k.serial) {
                k.gone = true;
                continue;
            }
            const float x = rt.mem.f32(k.task + 0x20), z = rt.mem.f32(k.task + 0x28), cz = rt.mem.f32(k.task + 0x40);
            ++alive;
            const float d = std::hypot(x - k.x, z - k.z);
            // (HOTD_HORDE_DUMP=F: each tracked creature's place at frame F: camera space as last drawn, its routine)
            static const uint64_t dumpAt = std::getenv("HOTD_HORDE_DUMP") ? std::strtoull(std::getenv("HOTD_HORDE_DUMP"), nullptr, 10) : 0;
            if (dumpAt == rt.frameCount)
                std::printf("HDUMP %llu task %X routine %X type %d cam %.0f %.0f %.0f\n", (unsigned long long)rt.frameCount, k.task,
                            rt.mem.u32(k.task), int(rt.mem.at<int16_t>(k.task + 0xCC)), rt.mem.f32(k.task + 0x38),
                            rt.mem.f32(k.task + 0x3C), cz);
            // (horde3, HOTD_HORDE_FOLLOW=A:B: each tracked creature's world place, routine, state, delay, heading every
            // 10th frame from A to B, with the camera's)
            static const char* fol = std::getenv("HOTD_HORDE_FOLLOW");
            static const uint64_t folA = fol ? std::strtoull(fol, nullptr, 10) : 0;
            static const uint64_t folB = fol && std::strchr(fol, ':') ? std::strtoull(std::strchr(fol, ':') + 1, nullptr, 10) : 0;
            if (fol && rt.frameCount >= folA && rt.frameCount <= folB && rt.frameCount % 10 == 0)
                std::printf("HFOLLOW %llu task %X routine %X state %d sub %d delay %d ang %04X at %.1f %.1f camera %.1f %.1f stopped %u y %.1f type %d\n",
                            (unsigned long long)rt.frameCount, k.task, rt.mem.u32(k.task), int(rt.mem.at<int16_t>(k.task + 0x5C)),
                            int(rt.mem.at<int16_t>(k.task + 0x60)), int32_t(rt.mem.u32(k.task + 0x2EC)), rt.mem.u32(k.task + 0x30) & 0xFFFF, x, z,
                            rt.mem.f32(G::camera_pos), rt.mem.f32(G::camera_pos + 8), rt.mem.u32(G::camera_stopped), rt.mem.f32(k.task + 0x24),
                            int(rt.mem.at<int16_t>(k.task + 0xCC)));   // (hordepub: y and type, the stand table's source)
            if (k.seen && d > 2.0f)
                ++moving;
            else if (k.seen)
                ++(cz > 60.0f ? standFar : standNear);
            k.x = x, k.z = z, k.seen = true;
        }
        // horde4: the pile: the most tracked walkers within 10 units of one of them, among those nearer the camera than 75
        // (camera z as last drawn; the user's "piled up at arm's length" on Chapter 1's bridge)
        int pile = 0;
        for (const Tracked& a : gTracked) {
            if (a.gone || rt.mem.u32(a.task) == kTaskNopRoutine || rt.mem.f32(a.task + 0x40) > kNearCam || rt.mem.f32(a.task + 0x40) <= 0.0f)
                continue;
            int n = 0;
            for (const Tracked& b : gTracked)
                if (!b.gone && rt.mem.u32(b.task) != kTaskNopRoutine && rt.mem.f32(b.task + 0x40) > 0.0f &&
                    std::hypot(rt.mem.f32(a.task + 0x20) - rt.mem.f32(b.task + 0x20), rt.mem.f32(a.task + 0x28) - rt.mem.f32(b.task + 0x28)) < 10.0f)
                    ++n;
            pile = std::max(pile, n);
        }
        std::printf("HTRACE %llu alive %d bodies %d shots %u event %d tracked %d moving %d standfar %d standnear %d pile %d pending %d\n",
                    (unsigned long long)rt.frameCount, int(rt.mem.at<int16_t>(G::enemy_count)), bodiesNow(rt),
                    rt.mem.u32(G::shot_enable), int(rt.mem.at<int8_t>(G::event_index)), alive, moving, standFar, standNear, pile,
                    int(pending_.size()));
    }
    if (!on())
        return;
        peakAlive = std::max(peakAlive, int(rt.mem.at<int16_t>(G::enemy_count)) - int(pending_.size()));
    if (static const char* tr = std::getenv("HOTD_HORDE_TRACE_ALIVE"); tr && rt.frameCount % uint64_t(std::max(1, std::atoi(tr))) == 0) {
        const float cx = rt.mem.f32(G::camera_pos), cz = rt.mem.f32(G::camera_pos + 8);
        std::printf("horde f%llu enemy_count %d alive %d camera_stopped %u\n", (unsigned long long)rt.frameCount,
                    rt.mem.at<int16_t>(G::enemy_count), rt.mem.at<int16_t>(G::enemies_alive), rt.mem.u32(G::camera_stopped));
        for (const Made& m : made) {
            if (m.serial == 0 || rt.mem.u32(m.task + 8) != m.serial)
                continue;
            const port::a::Enemy& e = rt.mem.at<port::a::Enemy>(m.task);
            const float dx = e.pos[0] - cx, dz = e.pos[2] - cz;
            std::printf("  task %X kind %u%s age %llu %s state %d sub %d delay %d slot %d flags %08X dist %.0f view z %.0f life %d\n", m.task, m.kind,
                        m.copy ? " copy" : "", (unsigned long long)(rt.frameCount - m.born), rt.routineName(e.routine).c_str(), e.state,
                        e.substate, e.delay, e.attack_slot, e.flags, std::sqrt(dx * dx + dz * dz), e.view_pos[2], e.life);
        }
    }
    peakBodies = std::max(peakBodies, bodiesNow(rt));
    // survfix (user 2026-11-04): a copy that will never attack does not hold the script's waits (game/engage.h: from
    // its AI state; the script's own creatures stay as the arcade has them). It leaves as the game's attackers leave
    for (const Made& m : made) {
        if (!m.copy || m.serial == 0 || rt.mem.u32(m.task + 8) != m.serial || rt.mem.u32(m.task) == kTaskNopRoutine)
            continue;
        const std::string why = engage.check(rt, m.task, m.serial);
        if (why.empty())
            continue;
        EngageWatch::retire(rt, m.task);
        engage.forget(m.task);
        ++engage.retired;
        if (rt.log)
            rt.log("horde: a copy will never attack (" + why + "): sent away at frame " + std::to_string(rt.frameCount));
    }
    if (rt.frameCount % 300 == 0)   // (the ones gone: their task's serial changed)
        made.erase(std::remove_if(made.begin(), made.end(), [&](const Made& m) { return m.serial == 0 || rt.mem.u32(m.task + 8) != m.serial; }),
                   made.end());
}

std::string HordeMode::summary() const
{
    char b[512];
    std::snprintf(b, sizeof b, "size=%s originals=%d copies=%d escorts=%d skipped=%d dropped=%d other_type=%d peak=%d bodies=%d retired=%d waiting=%d "
                  "held=%d trail=%d/%d/%d moved=%d pioneers=%d placed=%d cut=%d chain=%d multi=%d bonus=%d gifts=%d/%d/%d upgrades=%d/%d/%d stood=%d cutcopies=%d",
                  sizeName(size), originals, copies, escorts, skipped, dropped, otherType, peakAlive, peakBodies, engage.retired,
                  pendingPeak, burstHeld, trailFollowers, trailSwitched, trailFreed, moved, pioneers, placed, placedCut, bestChain, bestMulti, bonusPoints,
                  propDrops, rescueDrops, bonusDrops, propUpgrades, savedUpgrades, cleanUpgrades, stood, cutCopies);
    return b;
}

} // namespace hotd::game
