// T1 integration (docs/teaser_status/t1.md): boundaries of the port the Chapter 1 play-through calls most, implemented
// as runtime overrides (and dispatch cases for routines that are object routines), read from the typed decompilation
// and the disassembly (private/decomp, tools/i960dis.py). Written in the port's style so they can move into src/port
// with proofs. All UNVERIFIED (teaser): no capture proof yet.
#include "game/runtime.h"

#include <cstdlib>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace hotd::game {

using G = A::G;
using port::fbits;
using port::ffrom;

namespace {
constexpr uint32_t kSlots = G::attack_slots;   // AttackSlot[16]: +0 taken, +4 holder, +8 pos[3]
constexpr uint32_t slot(int32_t i) { return kSlots + uint32_t(i) * 0x14; }
constexpr uint32_t kSlotFlags = 0x51FB68;      // the enemy slots' flag bytes (0x2C each, by obj +0x2E6)
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Attack positions

// ---------------------------------------------------------------------------------------------------------------
// Scores

// ---------------------------------------------------------------------------------------------------------------
// Scripted enemies (the guard actions)

// ---------------------------------------------------------------------------------------------------------------
// Effects and system tasks

// 0x1DCD0 hostage_shot_effect / 0x1DB30 researcher_blood_effect (task): the port's blood marks
// (src/port/combat_support.cpp); the play-through log shows the hostages / researchers the players shot (fix-world)
void Runtime::hostage_shot_effect(uint32_t obj)
{
    if (mem.u32(obj + 0x50) == 0)
        ++stat_hostages[mem.at<uint8_t>(obj + 0x2EB) & 3];   // (testing only: the shooter)
    if (mem.u32(obj + 0x50) == 0 && log)
        log("hostage shot by a player (frame " + std::to_string(frameCount) + ")");
    port::Game<A>::hostage_shot_effect(obj);
}
void Runtime::researcher_blood_effect(uint32_t obj)
{
    if (mem.u32(obj + 0x50) == 0 && log)
        log("researcher shot by a player (frame " + std::to_string(frameCount) + ")");
    port::Game<A>::researcher_blood_effect(obj);
}

// 0x5DC0 shake_task -> 0x6350 hit_screen_shake (the port, src/port/body_hooks.cpp). The runtime runs this task before
// the players' camera-drive routine (player_hit_notice 0x6290), which would never see the hit: the hit mark is shown
// here first (teaser order fix, hud)
void Runtime::hit_screen_shake()
{
    for (uint32_t p = 0; p < 2; ++p) {
        if (mem.u32(G::players + p * 0xCC + 0xB4) == 1) {
            const uint32_t task = mem.u32(G::players + p * 0xCC + 0xA4);
            if (task != 0)
                hud_player_notice_show(task, int32_t(mem.u32(G::players + p * 0xCC + 0xB8)));
        }
    }
    // 4P: players 3 / 4's notices (the HUD draws them: Hud::drawSeatHud), as hud_player_notice_show / hud_popup_update:
    // one at a time for 60 frames, its sound (0xCA660[kind]) at once and the hit voice 5 frames later (player 3
    // player 1's voices, player 4 player 2's; which of the two by the frame instead of rand(): no game state used)
    for (int p = 2; p < seat_count(); ++p) {
        SeatNotice& sn = seatNotice[p - 2];
        const bool up = sn.kind >= 0 && frameCount < sn.until;
        if (mem.u32(player_rec(p) + 0xB4) == 1 && !up) {
            sn = SeatNotice{int32_t(mem.u32(player_rec(p) + 0xB8)), frameCount + 60};
            if (sn.kind >= 0 && sn.kind < 8)
                sound_cue(mem.u32(0xCA660 + uint32_t(sn.kind) * 4));
            if (std::getenv("HOTD_TRACE_NOTICE") && log) log("notice seat " + std::to_string(p + 1) + " kind " + std::to_string(sn.kind));
        } else if (up && frameCount + 55 == sn.until) {
            const bool odd = (frameCount & 1) != 0;
            sound_cue(voice_cue(p, p == 2 ? (odd ? 0xA01455 : 0xA01456) : (odd ? 0xA01458 : 0xA01459)));   // (fix141 #88)
        }
    }
    port::Game<A>::hit_screen_shake();
}

// ---------------------------------------------------------------------------------------------------------------
// Floors and movement (the Molb groups of event 4, the props, kind 70)

// ---------------------------------------------------------------------------------------------------------------
// Jump arcs, splines, room bounds, targets (kind 70, the frogs, the Molb, kind 32)

// ---------------------------------------------------------------------------------------------------------------
// Draw type 1 (object_draw's table 0xC6690 entry 1): the older skeletal body draw without the part hooks

// 0x37D80 object_draw_body, 0x37B80 body_part_draw_mat: the port's (src/port/support_c.cpp, proofs-7)

// 0x9CE60 body_part_draw_hook, the cases of types 30 and 34 (type - 2 = 28 / 32): another model in the part's place.
// Type 30: joint 5 with +0x78 clear blinks (0x557 / 0x558 by blink_phase). Type 34 (Sophie): the head (class 1) by
// motion: 0x61F (looking), 0x610.. (talking, 15 frames), 0x60E (eyes closed), else 0x60F with a 3-in-90 blink (not in
// chapter 7); the hand (class 6) 0x631 in the motions that hold it. true = drawn UNVERIFIED (teaser)
bool Runtime::body_hook_special(uint32_t part, uint32_t body, int32_t k, int32_t cls)
{
    const int32_t blink = int32_t(mem.u32(G::blink_phase));
    if (k == 28) {
        if (mem.u32(part + 0x38) == 5 && mem.at<uint8_t>(part + 0x78) == 0) {
            geo_draw_model(uint32_t(blink % 2 + 0x557));
            return true;
        }
        return false;
    }
    const int32_t m = mem.at<int16_t>(body + 0xD0);
    auto in = [](int32_t v, int32_t lo, int32_t hi) { return v >= lo && v <= hi; };   // inclusive
    if (cls != 1 || k == 24) {
        if (cls != 6)
            return false;
        const bool hand = in(m, 0x170, 0x171) || m == 0xD3 || in(m, 0xC1, 0xC5) || in(m, 0xBC, 0xBF) || m == 199 ||
                          in(m, 0x15A, 0x15B) || in(m, 0x153, 0x156) || in(m, 0x15E, 0x162) || in(m, 0x17E, 0x180) ||
                          in(m, 0x173, 0x174) || in(m, 0x177, 0x17B) || in(m, 0x1CB, 0x1D2) || m == 400 ||
                          in(m, 0x29A, 0x2A0);
        if (!hand)
            return false;
        // fix142 (GitHub #90: PC Mode's Researcher held no gun in the cut-scenes): THOTD.EXE's part hook 0x51D014
        // takes type 26 (the Researcher, PC Mode's character 6) into this case with Sophie (0x51D389 / 0x51D31B ->
        // 0x51D8FD): the same motions draw the gun hand 0x631 in the hand's place, except in Chapter 1's event 14
        // (0x51DA50: chapter 0xDA0CF4 == 0 and event 0xDA0CF3 == 14 keep the part's own hand: the captive researchers
        // there). The arcade has type 34 only and no exception (0x9DF30); ours takes the PC's case with the seats'
        // characters drawn (chars_on: PC Mode, Character Select picks; the caller adds k 24 only then)
        if (chars_on() && mem.at<int8_t>(G::chapter) == 0 && mem.at<int8_t>(G::event_index) == 14)
            return false;
        geo_draw_model(0x631);
        return true;
    }
    auto look = [&]() { geo_draw_model(0x61F); return true; };
    auto closed = [&]() { geo_draw_model(0x60E); return true; };
    auto open = [&]() {   // LAB_0009ddf4
        if (mem.at<int8_t>(G::chapter) == 6 || blink % 0x5A > 2)
            geo_draw_model(0x60F);
        else
            geo_draw_model(0x60E);
        return true;
    };
    if (in(m, 0x173, 0x174) || in(m, 0x153, 0x156) || in(m, 0xC1, 0xC5) || in(m, 0xBC, 0xBF) || m == 199 || m == 0xD3 ||
        in(m, 0x15E, 0x162) || in(m, 0x15A, 0x15B) || in(m, 0x170, 0x171) || in(m, 0x17E, 0x180) || in(m, 0x177, 0x17B) ||
        in(m, 0x190, 0x190) || (in(m, 0x192, 0x7FFF) && (in(m, 0x1CB, 0x1D2) || in(m, 0x29A, 0x2A0))))
        return look();
    if (m == 0x18A) {
        geo_draw_model(uint32_t((mem.u32(G::frame_counter) / 2) % 15 + 0x610));
        return true;
    }
    if (m == 0x182 || in(m, 0x187, 0x188) || m == 0x18C || m == 0x191)
        return closed();
    if (m == 399) {
        const int32_t f = mem.at<int16_t>(body + 0xF4);
        if (f > 0x35 && f < 0x45) {
            geo_draw_model((mem.u32(body + 0xF8) - 0x38u) % 15 + 0x610);
            return true;
        }
        if ((f < 10 || f > 12) && (f < 0x46 || f > 0x5F)) {
            geo_draw_model(0x60F);
            return true;
        }
        return closed();
    }
    return open();
}

// ---------------------------------------------------------------------------------------------------------------
// Severed parts

// ---------------------------------------------------------------------------------------------------------------
// Part-tree model helpers

// ---------------------------------------------------------------------------------------------------------------
// Hit effects: the spawners and their tasks moved into the port (src/port/hit_effects.cpp, proofs-2)

// ---------------------------------------------------------------------------------------------------------------
// Tumbling boxes (kind 38 barrels, thrown objects; tumble_corners 0x6A970, tumble_update 0x6ADD0, tumble_start 0x6AF70:
// the port's, src/port/support_c.cpp, proofs-7): the box block (+0x37C): +0x00..+0x14 the half sizes (x-, x+, y-,
// y+, z-, z+), +0x18 rock speed, +0x1C rock angle, +0x20 crossings left, +0x28..+0x30 rock axis, +0x34 state,
// +0x38 last side, +0x3C..+0x44 the local "up" axis. Corners in 0x5434C0 (8 x 3 floats), lowest corner 0x5434B0.

// the coprocessor's rotation about an axis (command 0x85: angle, axis), as a matrix product (0x09) with the
// coprocessor's own sine / cosine; T1's stand-in for the unmodelled 0x85, not bit-exact
void Runtime::copro_axis_rotate(uint32_t angle, float x, float y, float z)
{
    Tgp::Out so, co, none;
    uint32_t a[1] = {angle};
    copro(0x1B, a, 1, so);
    copro(0x1C, a, 1, co);
    const float s = ffrom(so.w[0]), c = ffrom(co.w[0]), t = 1.0f - c;
    const float m[12] = {c + t * x * x, t * x * y - s * z, t * x * z + s * y,
                         t * x * y + s * z, c + t * y * y, t * y * z - s * x,
                         t * x * z - s * y, t * y * z + s * x, c + t * z * z,
                         0.0f, 0.0f, 0.0f};
    uint32_t w[12];
    for (int k = 0; k < 12; ++k)
        w[k] = fbits(m[k]);
    copro(0x09, w, 12, none);
}

// 0x6B380 tumble_bounce(obj, box): the "up" axis in world, seen from the rock axis' frame: its side of the rock
// (sign of its y after undoing the axis' angles and the roll) flips -> a crossing (speed x -2; the last one ends the
// tumble, state 3); angle += speed; the lowest corner is lifted onto floor_y (the object with it); the object turns
// by the angle about the rock axis through that corner (its new position and angles from the matrix) UNVERIFIED
// (teaser)
void Runtime::tumble_bounce(uint32_t obj, uint32_t box)
{
    Tgp::Out none;
    const uint32_t n0 = mem.u32(box + 0x28), n1 = mem.u32(box + 0x2C), n2 = mem.u32(box + 0x30);
    copro(0x21, nullptr, 0, none);
    uint32_t r[3] = {mem.u32(obj + 0x34), mem.u32(obj + 0x30), mem.u32(obj + 0x2C)};
    copro(0x86, r, 3, none);
    uint32_t up[3] = {mem.u32(box + 0x3C), mem.u32(box + 0x40), mem.u32(box + 0x44)};
    Tgp::Out u;
    copro(0x1A, up, 3, u);
    copro(0x06, nullptr, 0, none);
    copro(0x21, nullptr, 0, none);
    uint32_t nv[3] = {n0, n1, n2};
    Tgp::Out an;
    copro(0x0F, nv, 3, an);
    uint32_t yaw = an.w[0], pitch = an.w[1];
    const uint32_t s16 = pitch & 0xFFFF;
    if (((s16 << 1) ^ s16) & 0x8000) {
        pitch = 0x8000 - pitch;
        yaw = yaw + 0x8000;
    }
    uint32_t mp[1] = {0u - pitch}, my[1] = {0u - yaw};
    copro(0x14, mp, 1, none);
    copro(0x15, my, 1, none);
    uint32_t uw[3] = {u.w[0], u.w[1], u.w[2]};
    Tgp::Out v;
    copro(0x1A, uw, 3, v);
    uint32_t xy[2] = {v.w[0], fbits(-ffrom(v.w[1]))};
    Tgp::Out ra;
    copro(0x0A, xy, 2, ra);
    copro(0x10, nullptr, 0, none);
    uint32_t mr[1] = {0u - ra.w[0]};
    copro(0x16, mr, 1, none);
    copro(0x14, mp, 1, none);
    copro(0x15, my, 1, none);
    uint32_t e[3] = {mem.u32(0xC6820), mem.u32(0xC6824), mem.u32(0xC6828)};
    Tgp::Out wv;
    copro(0x1A, e, 3, wv);
    copro(0x06, nullptr, 0, none);
    const uint32_t side = ffrom(wv.w[1]) >= 0.0f ? 0 : 1;
    int32_t speed = int32_t(mem.u32(box + 0x18));
    if (side != mem.u32(box + 0x38)) {
        const int32_t left = int32_t(mem.u32(box + 0x20)) - 1;
        mem.set32(box + 0x20, uint32_t(left));
        if (left == 0)
            mem.set32(box + 0x34, mem.u32(box + 0x34) + 1);
        speed = port::cvtzril(float(speed) * -2.0f);
    }
    mem.set32(box + 0x18, uint32_t(speed));
    mem.set32(box + 0x38, side);
    mem.set32(box + 0x1C, mem.u32(box + 0x1C) + uint32_t(speed));
    // the lowest corner (the first of equal ones)
    uint32_t low = 0x5434C0;
    for (uint32_t i = 1; i < 8; ++i)
        if (mem.f32(low + 4) > mem.f32(0x5434C0 + 12 * i + 4))
            low = 0x5434C0 + 12 * i;
    for (uint32_t k = 0; k < 3; ++k)
        mem.set32(0x5434B0 + 4 * k, mem.u32(low + 4 * k));
    const float floor = mem.f32(G::floor_y), ly = mem.f32(0x5434B4);
    if (floor > ly) {
        const float d = floor - ly;
        mem.set32(0x5434B4, fbits(d + ly));
        mem.set32(obj + 0x24, fbits(d + mem.f32(obj + 0x24)));
        for (uint32_t i = 1; i < 8; ++i)   // corners 1-7 (the original's loop starts at 0x5434D0)
            mem.set32(0x5434C4 + 12 * i, fbits(d + mem.f32(0x5434C4 + 12 * i)));
    }
    const uint32_t px = mem.u32(0x5434B0), py = mem.u32(0x5434B4), pz = mem.u32(0x5434B8);
    copro(0x21, nullptr, 0, none);
    uint32_t t1[3] = {px, py, pz};
    copro(0x12, t1, 3, none);
    copro_axis_rotate(mem.u32(box + 0x1C), ffrom(n0), ffrom(n1), ffrom(n2));
    uint32_t t2[3] = {px ^ 0x80000000u, py ^ 0x80000000u, pz ^ 0x80000000u};
    copro(0x12, t2, 3, none);
    uint32_t t3[3] = {mem.u32(obj + 0x20), mem.u32(obj + 0x24), mem.u32(obj + 0x28)};
    copro(0x12, t3, 3, none);
    uint32_t r2[3] = {mem.u32(obj + 0x34), mem.u32(obj + 0x30), mem.u32(obj + 0x2C)};
    copro(0x86, r2, 3, none);
    Tgp::Out tr, ang;
    copro(0x20, nullptr, 0, tr);
    mem.set32(obj + 0x20, tr.w[0]);
    mem.set32(obj + 0x24, tr.w[1]);
    mem.set32(obj + 0x28, tr.w[2]);
    copro(0x71, nullptr, 0, ang);
    mem.set32(obj + 0x34, ang.w[0]);
    mem.set32(obj + 0x30, ang.w[1]);
    mem.set32(obj + 0x2C, ang.w[2]);
    copro(0x06, nullptr, 0, none);
}

// ---------------------------------------------------------------------------------------------------------------
// Smaller effects and helpers

// 0x15010 task_create_child_alloc, 0x33EC0 spin_axis_from_velocity: the port's (src/port/support_c.cpp, proofs-7)

// the per-type part damage routine (table 0xC66A0 by hit_type: head damage models, blood) through the dispatch, so an
// unported one is listed by its name in the boundary report
void Runtime::part_damage_callback(uint32_t routine, uint32_t part)
{
    run(routine, part);   // the port's part_damage_type* (src/port/damage_types.cpp, proofs-2), else logged as unported
}

// 0x5730 player_paused (player state 10, set by 0x5720): blinks sprite 0x22B in its corner (x 3 / 0x29); when
// camera_status (0x51F564) bit 0 is set it returns to its saved state (+0x84 of the 0x51F280 block -> the state
// table 0xA8790) and runs it at once UNVERIFIED (teaser)
void Runtime::player_paused(uint32_t task)
{
    const int32_t p = mem.at<int8_t>(task + 0x20);
    const uint32_t blk = 0x51F280 + uint32_t(p) * 0xCC;
    uint32_t blink = mem.u32(G::frame_counter) & 0x20;
    if (mem.at<uint8_t>(0x51F564) & 1) {
        const uint32_t next = mem.u32(blk + 0x84);
        const uint32_t routine = mem.u32(0xA8790 + next * 8);
        blink = 0;
        mem.set32(blk + 4, next);
        mem.set32(task, routine);
        run(routine, task);
    }
    const int x = p == 0 ? 3 : 0x29;
    if (blink != 0)
        sprite_place(0x1000000, 1, 0, x, 1, 0x22B);
    else
        sprite_draw(0x1000000, x, 1, 0x22B);
}

} // namespace hotd::game
