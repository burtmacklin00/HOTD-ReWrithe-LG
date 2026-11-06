// Kind 40, the Hermit (Chapter 3's boss, event 7; creature type 64), with its spit ball, title card, life bar, drop
// and death effects, and kind 24, Dr. Curien's scene after the fight; worker "hermit". Read from the arcade disassembly
// 0x85430..0x87F10 (tools/i960dis.py) with the typed decompilation as a guide (its coprocessor sequences and a few
// table reads are wrong: hermit_act21_lunge's distance, hermit_spit_ball_draw's matrix; every FIFO sequence here is
// the disassembly's, in its order). Arcade forms only. Proven on A (hermit_full, 0 differ) but
// hermit_act24_guard_end: UNVERIFIED (teaser), see decl/hermit.h.
//
// The Hermit's block (+0x37C, mem_alloc(0x1BC), also at 0x543634): +0 = 1 when the motion frame advances this frame
// (hermit_hits_and_draw adds it to +0xF8), +1 draw mode 3 (nothing) while set, +2 step / +3 sequence of the action
// script (signed bytes), +4 the part role that marks the claw (27 = none; the part hook sets 0x520AE2 for it), +5 = 1
// once he has dropped (action 31: the second place, on the floor), +6 = 1 when out of the hole (the title card starts),
// +7 combat was active at the last held action, +8 hurt blink counter (48 frames), +9 spit round (0..5), +0xA hit
// points at the start (the life bar's maximum), +0xC hit points at which he drops (record +0), +0x190 the mouth's
// place (the spit balls start there; the part hook), +0x19C distance table (record +4), +0x1A0 spit counts (record +8:
// {balls per round x6, frames between x6}, 24 more bytes with two players), +0x1A4 the sequences (record +12), +0x1A8..
// +0x1B7 bytes / floats the original sets and nothing reads, +0x1B8 the action held while combat is not active.
// Script steps (8 bytes): {action, a, b, c} as signed shorts -> +0x60, +0x31C, +0x320, +0x324; action -1 = back to
// step 1; 25-30 / 35 branch to another sequence (by the distance +0xA0, the distance table, random, the hit points).
// Event flags (0x51E940 + n): 124 the event's go-ahead after the drop, 125 out of the hole, 127 fight started;
// Curien: 128 rises, 129 floats away.
#include "port/game.h"

#include <cstring>

namespace hotd::port {

namespace hermit {
constexpr uint32_t block = 0x543634;       // the Hermit's block (the spit ball resets its round, +9)
constexpr uint32_t start = 0xCA000;        // fight place: x, y, z, angle x; +0x10 angles y, z
constexpr uint32_t turns = 0xCA020;        // {motion, angle, footstep frame, footstep frame} x 3 (16 bytes)
constexpr uint32_t steps = 0xCA050;        // {motion (short), footstep frames (2 ushorts), speed z (float)} x 3
constexpr uint32_t walks = 0xCA080;        // {motion (short), footstep frames (3 ushorts)} x 4
constexpr uint32_t drop_speed = 0xCA0A0;   // the drop's start speed y
constexpr uint32_t drop_place = 0xCA0B0;   // the floor place after the drop: x, y, z, angle x; +0x10 angles y, z
constexpr uint32_t attacks = 0xCA0D0;      // {motion (short), claw role (byte), ., hit frame, lunge distance} x 4
constexpr uint32_t hurt_motions = 0xCA100; // MO_b3_dam_a / dam_b (shorts)
constexpr uint32_t hurt_speed = 0xCA104;   // pushed back per frame while hurt
constexpr uint32_t ball_radius = 0xCA108;
constexpr uint32_t home_floor = 0xCA110;   // x, y on the floor (after the drop)
constexpr uint32_t home_wall = 0xCA11C;    // x at +0, z at +8 (on the wall)
constexpr uint32_t mouth = 0xCA130;        // the mouth's offset in the head part
constexpr uint32_t dust_place = 0xCA140;   // x, y, z; +0xC the dust's scale
constexpr uint32_t spit_speed = 0x85420;   // the spit ball's speed (a word in the program)
constexpr uint32_t models = 0x2E73530;     // the model table (16 bytes a model)
// sound cues (words of the program's sound table)
constexpr uint32_t snd_step = 0xC9FD4, snd_drop = 0xC9FD8, snd_hurt = 0xC9FDC, snd_spit = 0xC9FD0,
                   snd_ball_shot = 0xC9FF8, snd_land = 0xC9FE4, snd_drop_end = 0xC9FE8, snd_die_a = 0xC9FF0,
                   snd_die_b = 0xC9FF4;
} // namespace hermit

// motion near its end: frame +0xF4 >= the motion's frame count - 2 (signed)
template <class P> bool Game<P>::hermit_motion_end(uint32_t obj)
{
    const int32_t n = int32_t(mem.u32(G::motion_frame_counts + 4 * uint32_t(int32_t(of<int16_t>(obj, 0xD0)))));
    return of<int16_t>(obj, 0xF4) >= n - 2;
}

// the object's place in camera space (stored matrix 5): +0x98, +0x9C, the depth +0xA0
template <class P> void Game<P>::hermit_view_point(uint32_t obj)
{
    if (pm_boss()) {   // PC 0x4EEBA3 / 0x4EEC5A: stored matrix 5 through the Hermit file's transform 0x4EB224
        float o[3];
        pm_m5().transform(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28), o[0], o[1], o[2]);
        of<float>(obj, 0x98) = o[0];
        of<float>(obj, 0x9C) = o[1];
        of<float>(obj, 0xA0) = o[2];
        return;
    }
    cam_copro(0x74, {5});
    const Tgp::Out o = cam_copro(0x1A, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    of<uint32_t>(obj, 0x98) = o.w[0];
    of<uint32_t>(obj, 0x9C) = o.w[1];
    cam_copro(0x06, {});
    of<uint32_t>(obj, 0xA0) = o.w[2];
}

// 0x85430 boss_hermit (kind 40): type 64, motion 61, the part draw hook, no speed, the block (record: drop hit points
// +0, distances +4, spit counts +8, sequences +12), counted as an enemy, an attack slot; the life bar and title card
// tasks; then the entrance
template <class P> void Game<P>::boss_hermit(uint32_t obj)
{
    mem.set32(G::hit_type, 64);
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    body_init(obj, 64, 61);
    of<uint32_t>(obj, 0x11C) = F::hermit_part_draw_hook;
    creature_type_setup(obj);
    for (uint32_t i = 0; i < 3; ++i) {
        of<uint32_t>(obj, 0x84 + 4 * i) = 0;
        of<uint32_t>(obj, 0x78 + 4 * i) = 0;
    }
    const uint32_t blk = mem_alloc(0x1BC);
    of<uint32_t>(obj, 0x37C) = blk;
    mem.set32(hermit::block, blk);
    of<uint16_t>(obj, 0x6C) = uint16_t(of<uint16_t>(obj, 0x6C) & ~2u);
    of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) | 1);
    mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
    attack_slot_take_wide(obj);
    of<int16_t>(obj, 0x66) = 0;
    of<uint32_t>(obj, 0x58) = 1;
    mem.at<uint8_t>(blk + 1) = 0;
    mem.at<uint8_t>(blk + 6) = 0;
    mem.at<uint8_t>(blk + 8) = 0;
    mem.at<uint8_t>(blk + 7) = 1;
    mem.at<uint16_t>(blk + 0xA) = of<uint16_t>(obj, 0x70);
    mem.at<uint8_t>(blk + 9) = 1;
    mem.at<uint16_t>(blk + 0xC) = mem.at<uint16_t>(rec);
    const uint32_t lb = task_create(F::hermit_life_bar_task, 0x3C8);
    mem.at<int16_t>(lb + 0x66) = 0;
    const uint32_t tc = task_create(F::hermit_title_task, 0x3C8);
    mem.at<int16_t>(tc + 0x66) = 0;
    of<uint32_t>(obj, 0) = F::hermit_entrance;
    hermit_entrance(obj);
}

// 0x85550 hermit_entrance: MO 61 to its end, out of the hole (block +6: the title card), MO 63 for 180 frames, MO 62;
// near its end flag 125 and the fight (hermit_frame_outer)
template <class P> void Game<P>::hermit_entrance(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    auto next = [&] { of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1); };
    auto busy = [&] { mem.at<uint8_t>(blk) = 1; hermit_hits_and_draw(obj); };
    switch (of<int16_t>(obj, 0x66)) {
    case 0:
        next();
        [[fallthrough]];
    case 1: {
        const int32_t n = int32_t(mem.u32(G::motion_frame_counts + 4 * uint32_t(int32_t(of<int16_t>(obj, 0xD0)))));
        if (of<int16_t>(obj, 0xF4) < n - 1)
            return busy();
        mem.at<uint8_t>(blk + 6) = 1;
        of<int32_t>(obj, 0x324) = 0xB4;
        set_motion(obj, 63);
        next();
    }
        [[fallthrough]];
    case 2: {
        const int32_t c = of<int32_t>(obj, 0x324) - 1;
        of<int32_t>(obj, 0x324) = c;
        if (c != 0)
            return busy();
        set_motion(obj, 62);
        next();
        break;
    }
    default: break;
    }
    if (!hermit_motion_end(obj))
        return busy();
    mem.at<uint8_t>(G::event_flags + 125) = 1;
    of<uint32_t>(obj, 0) = F::hermit_frame_outer;
    hermit_frame_outer(obj);
}

// 0x85640 hermit_frame_outer: the fight's place and heading (0xCA000), the camera record's +0xF0 = 2, the block's
// script (sequence 0), flag 127; then hermit_frame
template <class P> void Game<P>::hermit_frame_outer(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    mem.set32(G::camera_records + 0xF0, 2);
    for (uint32_t i = 0; i < 4; ++i)
        of<uint32_t>(obj, 0x20 + 4 * i) = mem.u32(hermit::start + 4 * i);
    of<int16_t>(obj, 0x66) = 0;
    of<int16_t>(obj, 0x60) = 0;
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) & ~1u);
    of<uint32_t>(obj, 0x30) = mem.u32(hermit::start + 0x10);
    of<uint32_t>(obj, 0x34) = mem.u32(hermit::start + 0x14);
    for (uint32_t i = 0; i < 4; ++i)
        mem.at<uint8_t>(blk + i) = 0;
    mem.set32(blk + 0x19C, mem.u32(rec + 4));
    mem.at<uint8_t>(blk + 0x1A8) = 1;
    mem.set32(blk + 0x1AC, 0x44078000);   // 542.0
    mem.set32(blk + 0x1A4, rec + 12);
    mem.at<uint8_t>(blk + 5) = 0;
    mem.at<uint8_t>(blk + 0x1A9) = 0;
    mem.at<uint8_t>(blk + 4) = 27;
    mem.at<uint8_t>(blk + 0x1AA) = 1;
    mem.set32(blk + 0x1A0, mem.u32(rec + 8));
    mem.set32(blk + 0x1B4, 0x4519E000);   // 2462.0
    mem.at<uint8_t>(G::event_flags + 127) = 1;
    of<uint32_t>(obj, 0) = F::hermit_frame;
    hermit_frame(obj);
}

// 0x85700 hermit_frame: the action script (as chariot_frame): with no action the next step; actions 25-30 / 35 pick
// another sequence and step again; while combat is not active the attacks are held (33 / 34, the action kept at
// +0x1B8) and come back once it is; the action routine; below the block's drop hit points once: action 31
template <class P> void Game<P>::hermit_frame(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    const uint32_t spin = obj + 0x31C;
    if (of<uint16_t>(obj, 0x60) == 0) {
        bool again;
        do {
            const uint8_t d = uint8_t(mem.at<uint8_t>(blk + 2) + 1);
            mem.at<uint8_t>(blk + 2) = d;
            const uint32_t seqs = mem.u32(blk + 0x1A4);
            const uint32_t seq = mem.u32(seqs + 4 * uint32_t(int32_t(mem.at<int8_t>(blk + 3))));
            int32_t act = mem.at<int16_t>(seq + 8 * uint32_t(int32_t(int8_t(d))));
            again = false;
            if (act == -1) {
                mem.at<uint8_t>(blk + 2) = 1;
                act = mem.at<int16_t>(seq + 8);
            }
            of<int16_t>(obj, 0x60) = int16_t(act);
            auto st = [&](uint32_t off) {
                const uint32_t s = mem.u32(seqs + 4 * uint32_t(int32_t(mem.at<int8_t>(blk + 3))));
                return int32_t(mem.at<int16_t>(s + 8 * uint32_t(int32_t(mem.at<int8_t>(blk + 2))) + off));
            };
            of<int32_t>(obj, 0x31C) = st(2);
            of<int32_t>(obj, 0x320) = st(4);
            const int32_t c = st(6);
            of<int16_t>(obj, 0x66) = 0;
            of<int32_t>(obj, 0x324) = c;
            const float dist = of<float>(obj, 0xA0);
            uint32_t pick = 0;   // the spin word whose low byte is the new sequence
            switch (uint32_t(uint16_t(of<uint16_t>(obj, 0x60) - 25))) {
            case 0: pick = dist <= 100.0f ? spin : spin + 4; again = true; break;
            case 1: pick = dist <= 150.0f ? (dist <= 100.0f ? spin : spin + 4) : spin + 8; again = true; break;
            case 2:
                pick = dist <= mem.f32(mem.u32(blk + 0x19C) + 4 * of<uint32_t>(obj, 0x31C)) ? spin + 4 : spin + 8;
                again = true;
                break;
            case 3: pick = spin + 4 * uint32_t(int32_t(rand()) % 2); again = true; break;
            case 4: pick = spin + 4 * uint32_t(int32_t(rand()) % 3); again = true; break;
            case 5: pick = of<int16_t>(obj, 0x70) <= of<int32_t>(obj, 0x31C) ? spin + 4 : spin + 8; again = true; break;
            case 10: pick = spin; again = true; break;
            default: break;
            }
            if (again) {
                const uint8_t s = mem.at<uint8_t>(pick);
                mem.at<uint8_t>(blk + 2) = 0;
                mem.at<uint8_t>(blk + 3) = s;
            }
        } while (again);
        if (combat_active() == 0) {
            const uint32_t a = uint32_t(uint16_t(of<uint16_t>(obj, 0x60) - 11));
            if (a <= 3 || a == 9 || a == 10) {
                mem.at<uint16_t>(blk + 0x1B8) = of<uint16_t>(obj, 0x60);
                of<int16_t>(obj, 0x60) = 33;
            } else if (a == 4 || a == 12) {
                mem.at<uint16_t>(blk + 0x1B8) = of<uint16_t>(obj, 0x60);
                of<int16_t>(obj, 0x60) = 34;
            }
        }
        // (ours, totd8: The Typing of the Dead 0's boss strike hold, game.h: the claws (11-15) and the lunge (21) wait
        // as the arcade's own "combat not active" ones do, in a pose (action 33); the spit's balls carry their own key)
        if (gBossHold && ((of<int16_t>(obj, 0x60) >= 11 && of<int16_t>(obj, 0x60) <= 15) || of<int16_t>(obj, 0x60) == 21) &&
            gBossHold(obj)) {
            mem.at<uint16_t>(blk + 0x1B8) = of<uint16_t>(obj, 0x60);
            of<int16_t>(obj, 0x60) = 33;
        }
    }
    const int16_t heldAct = mem.at<int16_t>(blk + 0x1B8);
    if ((of<int16_t>(obj, 0x60) == 33 || of<int16_t>(obj, 0x60) == 34) && combat_active() != 0 &&
        mem.at<uint8_t>(blk + 7) != 0 &&
        !(gBossHold && ((heldAct >= 11 && heldAct <= 15) || heldAct == 21) && gBossHold(obj))) {
        const uint16_t held = mem.at<uint16_t>(blk + 0x1B8);
        of<int16_t>(obj, 0x66) = 0;
        of<uint16_t>(obj, 0x60) = held;
    }
    switch (of<uint16_t>(obj, 0x60)) {
    case 1: hermit_act1_pose(obj); break;
    case 2: case 3: case 4: case 5: case 6: case 7: hermit_act_walk(obj); break;
    case 8: case 9: case 10: hermit_act_step(obj); break;
    case 11: case 12: case 13: case 14: case 15: hermit_act_attack(obj); break;
    case 16: case 17: case 18: case 19: hermit_act_turn(obj); break;
    case 20: hermit_act20_spit(obj); break;
    case 21: hermit_act21_lunge(obj); break;
    case 22: hermit_act22_guard(obj); break;
    case 23: hermit_act23_guard_hold(obj); break;
    case 24: hermit_act24_guard_end(obj); break;
    case 31: hermit_act31_drop(obj); break;
    case 32: hermit_act32_hit_reaction(obj); break;
    case 33: case 34: hermit_act_wait_combat(obj); break;
    default: break;
    }
    if (of<int16_t>(obj, 0x70) < mem.at<int16_t>(blk + 0xC) && mem.at<uint8_t>(blk + 5) == 0) {
        mem.at<uint8_t>(blk + 5) = uint8_t(mem.at<uint8_t>(blk + 5) + 1);
        of<int16_t>(obj, 0x66) = 0;
        of<int16_t>(obj, 0x60) = 31;
    }
}

// 0x85B30 hermit_act1_pose (action 1): one of MO 55 / 56 / 57; a counts the poses (+0x31C), each pose's end puts him
// back in his place
template <class P> void Game<P>::hermit_act1_pose(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    const int16_t step = of<int16_t>(obj, 0x66);
    if (step == 0) {
        const int32_t r = int32_t(rand()) % 3;
        set_motion_blend(obj, r == 0 ? 55 : r == 2 ? 56 : 57, 8, 7);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    if (step == 0 || step == 1) {
        bool keep = true;
        of<int32_t>(obj, 0x31C) = of<int32_t>(obj, 0x31C) - 1;
        if ((of<uint8_t>(obj, 0x3C5) >> 4) & 1) {
            hermit_action_done(obj);
            if (of<int32_t>(obj, 0x31C) <= 0)
                keep = false;
            else
                of<int16_t>(obj, 0x66) = 0;
        }
        if (!keep)
            of<int16_t>(obj, 0x60) = 0;
    }
    mem.at<uint8_t>(blk) = 1;
    hermit_hits_and_draw(obj);
}

// 0x85BE0 hermit_act_wait_combat (actions 33 / 34, an attack held while combat is not active): a pose (33) or MO 51
// (34, the guard); at its end back in place, combat_active kept in the block's +7
template <class P> void Game<P>::hermit_act_wait_combat(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    const int16_t step = of<int16_t>(obj, 0x66);
    if (step == 0) {
        uint32_t m = 51;
        if (of<uint16_t>(obj, 0x60) == 33) {
            const int32_t r = int32_t(rand()) % 3;
            m = r == 0 ? 55 : r == 1 ? 56 : 57;
        }
        set_motion_blend(obj, m, 8, 7);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    if ((step == 0 || step == 1) && ((of<uint8_t>(obj, 0x3C5) >> 4) & 1)) {
        hermit_action_done(obj);
        of<int16_t>(obj, 0x66) = 0;
        mem.at<uint8_t>(blk + 7) = uint8_t(combat_active());
    }
    mem.at<uint8_t>(blk) = 1;
    hermit_hits_and_draw(obj);
}

// 0x85C80 hermit_act_turn (actions 16-19): 16 / 17 / 18 one of the turns (table 0xCA020), 19 a random one; footsteps;
// at the end the turn's angle added (heading y on the wall, angle z on the floor)
template <class P> void Game<P>::hermit_act_turn(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    if (of<int16_t>(obj, 0x66) == 0) {
        const int16_t a = of<int16_t>(obj, 0x60);
        if (a == 17)
            of<int32_t>(obj, 0x320) = 1;
        else if (a == 16)
            of<int32_t>(obj, 0x320) = 0;
        else if (a == 18)
            of<int32_t>(obj, 0x320) = 2;
        else
            of<int32_t>(obj, 0x320) = int32_t(rand()) % 3;
        set_motion_blend(obj, mem.u32(hermit::turns + 16 * of<uint32_t>(obj, 0x320)), 8, 7);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    const uint32_t e = hermit::turns + 16 * of<uint32_t>(obj, 0x320);
    if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) == 0) {
        const int32_t f = of<int16_t>(obj, 0xF4);
        if (f == mem.at<int32_t>(e + 8) || f == mem.at<int32_t>(e + 0xC))
            sound_cue(mem.u32(hermit::snd_step));
    } else {
        if (mem.at<uint8_t>(blk + 5) == 0)
            of<int32_t>(obj, 0x30) = mem.at<int32_t>(e + 4) + of<int32_t>(obj, 0x30);
        else
            of<int32_t>(obj, 0x34) = of<int32_t>(obj, 0x34) - mem.at<int32_t>(e + 4);
        of<int16_t>(obj, 0x60) = 0;
    }
    mem.at<uint8_t>(blk) = 1;
    hermit_hits_and_draw(obj);
}

// 0x85D90 hermit_act_step (actions 8 / 9 / 10): the step's speed z (table 0xCA050 by the action) each frame,
// footsteps; the motion is the table's entry by a (+0x31C, as the original reads it); at the end back in place
template <class P> void Game<P>::hermit_act_step(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    if (of<int16_t>(obj, 0x66) == 0) {
        const int16_t a = of<int16_t>(obj, 0x60);
        of<int32_t>(obj, 0x320) = a == 8 ? 0 : a == 9 ? 1 : 2;
        set_motion_blend(obj, uint32_t(int32_t(mem.at<int16_t>(hermit::steps + 12 * of<uint32_t>(obj, 0x31C)))), 8, 7);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) == 0) {
        const uint32_t e = hermit::steps + 12 * of<uint32_t>(obj, 0x320);
        of<float>(obj, 0x28) = of<float>(obj, 0x28) + mem.f32(e + 8);
        const uint16_t f = of<uint16_t>(obj, 0xF4);
        if (f == mem.at<uint16_t>(e + 2) || f == mem.at<uint16_t>(e + 4))
            sound_cue(mem.u32(hermit::snd_step));
    } else {
        hermit_action_done(obj);
        of<int16_t>(obj, 0x60) = 0;
    }
    mem.at<uint8_t>(blk) = 1;
    hermit_hits_and_draw(obj);
}

// 0x85E70 hermit_act_walk (actions 2-7): 6 = 5 (back) or 2 (walk) by the distance; the walk (table 0xCA080) until
// the step's distance (the distance table's a): walking 2 / 3 / 7 until near, the others until far; footsteps
template <class P> void Game<P>::hermit_act_walk(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    auto limit = [&] { return mem.f32(mem.u32(blk + 0x19C) + 4 * of<uint32_t>(obj, 0x31C)); };
    if (of<int16_t>(obj, 0x66) == 0) {
        int16_t a = of<int16_t>(obj, 0x60);
        if (a == 6)
            a = of<float>(obj, 0xA0) <= limit() ? 5 : 2;
        of<int16_t>(obj, 0x60) = a;
        if (a == 3)
            of<int32_t>(obj, 0x320) = 1;
        else if (a == 2)
            of<int32_t>(obj, 0x320) = 0;
        else if (a == 5)
            of<int32_t>(obj, 0x320) = 2;
        else if (a == 7)
            of<int32_t>(obj, 0x320) = 3;
        else
            of<int32_t>(obj, 0x320) = int32_t(rand()) % 2;
        set_motion_blend(obj, uint32_t(int32_t(mem.at<int16_t>(hermit::walks + 8 * of<uint32_t>(obj, 0x320)))), 8, 7);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    const int16_t a = of<int16_t>(obj, 0x60);
    const float dist = of<float>(obj, 0xA0);
    bool keep;
    if (a < 2 || (a > 3 && a != 7))
        keep = dist <= limit();
    else
        keep = dist >= limit();
    const int32_t f = of<int16_t>(obj, 0xF4);
    const uint32_t e = hermit::walks + 8 * of<uint32_t>(obj, 0x320);
    if (f == int32_t(mem.at<uint16_t>(e + 2)) || f == int32_t(mem.at<uint16_t>(e + 4)) ||
        f == int32_t(mem.at<uint16_t>(e + 6)))
        sound_cue(mem.u32(hermit::snd_step));
    if (!keep)
        of<int16_t>(obj, 0x60) = 0;
    mem.at<uint8_t>(blk) = 1;
    hermit_hits_and_draw(obj);
}

// 0x86000 hermit_act22_guard (action 22): MO 50, near its end action 23
template <class P> void Game<P>::hermit_act22_guard(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    const int16_t step = of<int16_t>(obj, 0x66);
    if (step == 0) {
        set_motion_blend(obj, 50, 8, 7);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    if ((step == 0 || step == 1) && hermit_motion_end(obj)) {
        of<int16_t>(obj, 0x66) = 0;
        of<int16_t>(obj, 0x60) = 23;
    }
    mem.at<uint8_t>(blk) = 1;
    hermit_hits_and_draw(obj);
}

// 0x86070 hermit_act23_guard_hold (action 23): MO 51 held a frames (+0x31C), then to its end and back in place
template <class P> void Game<P>::hermit_act23_guard_hold(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    const int16_t step = of<int16_t>(obj, 0x66);
    bool check = step >= 2 || step < 0;
    if (step == 0) {
        set_motion_blend(obj, 51, 8, 7);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    if (step == 0 || step == 1) {
        const int32_t c = of<int32_t>(obj, 0x31C) - 1;
        of<int32_t>(obj, 0x31C) = c;
        if (c == 0) {
            of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
            check = true;
        }
    }
    if (check && hermit_motion_end(obj)) {
        hermit_action_done(obj);
        of<int16_t>(obj, 0x60) = 0;
    }
    mem.at<uint8_t>(blk) = 1;
    hermit_hits_and_draw(obj);
}

// 0x86100 hermit_act24_guard_end (action 24): MO 52 to its end, back in place
template <class P> void Game<P>::hermit_act24_guard_end(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    if (of<int16_t>(obj, 0x66) == 0) {
        set_motion(obj, 52);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    if (hermit_motion_end(obj)) {
        hermit_action_done(obj);
        of<int16_t>(obj, 0x60) = 0;
    }
    mem.at<uint8_t>(blk) = 1;
    hermit_hits_and_draw(obj);
}

// 0x86160 hermit_act31_drop (action 31, once below the drop hit points): off the wall (attack slot freed, not
// shootable, MO 54), falls (gravity -0.0272, start speed 0xCA0A0) and bounces twice on the floor y -314 (the first
// time the landing sound and dust), MO 64 (the drop sounds) until z 2498 (out of the count); waits for flag 124,
// then on the floor place (0xCA0B0) with sequence 6, shootable, an attack slot, MO 55
template <class P> void Game<P>::hermit_act31_drop(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    const float y0 = of<float>(obj, 0x24);
    auto next = [&] { of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1); };
    const uint32_t step = uint32_t(int32_t(of<int16_t>(obj, 0x66)));
    bool dflt = step > 4;
    switch (dflt ? 5u : step) {
    case 0:
        if (of<uint8_t>(obj, 0x2E7) != 0xFF)
            enemy_attack_slot_free(obj);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
        of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) & ~1u);
        set_motion(obj, 54);
        of<int32_t>(obj, 0x31C) = 0;
        sound_cue(mem.u32(hermit::snd_drop));
        next();
        [[fallthrough]];
    case 1:
        if (!hermit_motion_end(obj)) {
            mem.at<uint8_t>(blk) = 1;
            break;
        }
        of<uint32_t>(obj, 0x88) = 0xBCDF0123;   // -0.0272222
        next();
        of<uint32_t>(obj, 0x7C) = mem.u32(hermit::drop_speed);
        [[fallthrough]];
    case 2: {
        const float v = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
        of<float>(obj, 0x7C) = v;
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + v;
        break;
    }
    case 3:
        of<int32_t>(obj, 0x2C) = 0;
        of<int32_t>(obj, 0x30) = 0;
        of<int32_t>(obj, 0x34) = 0;
        set_motion(obj, 64);
        hermit_drop_sound_start();
        next();
        [[fallthrough]];
    case 4:
        if (double(of<float>(obj, 0x28)) < 2498.0) {
            mem.at<uint8_t>(blk) = 1;
            break;
        }
        mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        next();
        dflt = true;
        [[fallthrough]];
    default:
        if (mem.at<uint8_t>(G::event_flags + 124) == 0) {
            mem.at<uint8_t>(blk) = 1;
            break;
        }
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x10000000u;
        mem.set32(G::camera_records + 0xF0, 0);
        mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 1;
        attack_slot_take_wide(obj);
        mem.at<uint8_t>(blk + 3) = 6;
        mem.at<uint8_t>(blk + 2) = 0;
        set_motion(obj, 55);
        for (uint32_t i = 0; i < 4; ++i)
            of<uint32_t>(obj, 0x20 + 4 * i) = mem.u32(hermit::drop_place + 4 * i);
        of<int16_t>(obj, 0x60) = 0;
        of<uint32_t>(obj, 0x30) = mem.u32(hermit::drop_place + 0x10);
        of<uint32_t>(obj, 0x34) = mem.u32(hermit::drop_place + 0x14);
        break;
    }
    (void)dflt;
    if (double(of<float>(obj, 0x24)) < -314.0 && of<int16_t>(obj, 0x66) <= 2) {
        if (of<int32_t>(obj, 0x31C) > 1) {
            of<uint32_t>(obj, 0x24) = 0xC39D0000;   // -314.0
            of<int16_t>(obj, 0x66) = 3;
        } else {
            if (of<int32_t>(obj, 0x31C) == 0) {
                sound_cue(mem.u32(hermit::snd_land));
                hermit_drop_dust_spawn();
            }
            const float v = of<float>(obj, 0x7C) * -0.15f;
            of<int32_t>(obj, 0x31C) = of<int32_t>(obj, 0x31C) + 1;
            of<float>(obj, 0x7C) = v;
            of<float>(obj, 0x24) = y0 + v;
        }
    }
    hermit_hits_and_draw(obj);
}

// 0x86410 hermit_drop_sound_start: the drop sound task
template <class P> void Game<P>::hermit_drop_sound_start()
{
    const uint32_t t = task_create(F::hermit_drop_sound, 0x58);
    mem.set32(t + 0x50, 0);
}

// 0x86430 hermit_act21_lunge (action 21): MO 48 towards the player, one of the attacks 11-13 picked with its lunge
// distance (table 0xCA0D0 +8); within it that attack; footsteps
template <class P> void Game<P>::hermit_act21_lunge(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    if (of<int16_t>(obj, 0x66) == 0) {
        set_motion_blend(obj, 48, 8, 7);
        const int32_t r = int32_t(rand()) % 3;
        const uint32_t k = r == 0 ? 0 : r == 1 ? 1 : 2;
        of<int32_t>(obj, 0x31C) = int32_t(11 + k);
        of<uint32_t>(obj, 0x33C) = mem.u32(hermit::attacks + 8 + 12 * k);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    if (of<float>(obj, 0xA0) <= of<float>(obj, 0x33C)) {
        of<int16_t>(obj, 0x66) = 0;
        of<uint16_t>(obj, 0x60) = of<uint16_t>(obj, 0x31C);
    } else {
        const uint16_t f = of<uint16_t>(obj, 0xF4);
        if (f == mem.at<uint16_t>(hermit::steps + 12 + 2) || f == mem.at<uint16_t>(hermit::steps + 12 + 4))
            sound_cue(mem.u32(hermit::snd_step));
    }
    mem.at<uint8_t>(blk) = 1;
    hermit_hits_and_draw(obj);
}

// 0x86510 hermit_act_attack (actions 11-15): the claw (table 0xCA0D0: 11 / 12 / 13, 14 a random one, others 3); its
// role marked (block +4); at the hit frame the player is hit (the claw's place through the camera first, as the
// original computes it); near its end back in place, no claw
template <class P> void Game<P>::hermit_act_attack(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    auto next = [&] { of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1); };
    auto busy = [&] { mem.at<uint8_t>(blk) = 1; hermit_hits_and_draw(obj); };
    switch (of<int16_t>(obj, 0x66)) {
    case 0: {
        const int16_t a = of<int16_t>(obj, 0x60);
        if (a == 12)
            of<int32_t>(obj, 0x320) = 1;
        else if (a == 11)
            of<int32_t>(obj, 0x320) = 0;
        else if (a == 13)
            of<int32_t>(obj, 0x320) = 2;
        else if (a == 14)
            of<int32_t>(obj, 0x320) = int32_t(rand()) % 3;
        else
            of<int32_t>(obj, 0x320) = 3;
        const uint32_t e = hermit::attacks + 12 * of<uint32_t>(obj, 0x320);
        set_motion_blend(obj, uint32_t(int32_t(mem.at<int16_t>(e))), 8, 7);
        mem.at<uint8_t>(blk + 4) = mem.at<uint8_t>(e + 2);
        of<uint32_t>(obj, 0x31C) = mem.u32(e + 4);
        next();
    }
        [[fallthrough]];
    case 1:
        if (of<int16_t>(obj, 0xF4) < of<int32_t>(obj, 0x31C) - 4)
            return busy();
        next();
        [[fallthrough]];
    case 2:
        if (of<int16_t>(obj, 0xF4) < of<int32_t>(obj, 0x31C))
            return busy();
        if (!pm_boss()) {   // the result is not used (PC 0x4EE32C has no such transform)
            cam_copro(0x74, {5});
            cam_copro(0x1A, {of<uint32_t>(obj, 0x2D4), of<uint32_t>(obj, 0x2D8), of<uint32_t>(obj, 0x2DC)});
            cam_copro(0x06, {});
        }
        hermit_attack_hit(2);
        next();
        break;
    default: break;
    }
    if (hermit_motion_end(obj)) {
        hermit_action_done(obj);
        mem.at<uint8_t>(blk + 4) = 27;
        of<int16_t>(obj, 0x60) = 0;
    }
    busy();
}

// 0x866A0 hermit_act20_spit (action 20): the round's counts (block +9, up to 5; balls and frames between from the
// record's spit table, the two-player half with two players), MO 47; from frame 38 a spit ball every few frames from
// the mouth towards a random point 10 units in front of the camera; after the last one 16 frames, then to the end
template <class P> void Game<P>::hermit_act20_spit(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    auto next = [&] { of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1); };
    auto table = [&] {
        uint32_t t = mem.u32(blk + 0x1A0);
        if (mem.at<uint16_t>(G::players_in_game) == 2)
            t += 24;
        return t;
    };
    auto countdown = [&] {   // 0x86938
        const int32_t c = of<int32_t>(obj, 0x31C) - 1;
        of<int32_t>(obj, 0x31C) = c;
        if (c != 0)
            return false;
        next();
        return true;
    };
    auto end = [&] {   // 0x86958
        if ((of<uint8_t>(obj, 0x3C5) >> 4) & 1)
            of<int16_t>(obj, 0x60) = 0;
        else
            mem.at<uint8_t>(blk) = 1;
        hermit_hits_and_draw(obj);
    };
    switch (of<int16_t>(obj, 0x66)) {
    case 0: {
        int8_t r = mem.at<int8_t>(blk + 9);
        if (r <= 4)
            ++r;
        mem.at<int8_t>(blk + 9) = r;
        of<int32_t>(obj, 0x31C) = 0;
        of<int32_t>(obj, 0x324) = mem.at<int16_t>(table() + 0xC + 2 * uint32_t(int32_t(mem.at<int8_t>(blk + 9))));
        of<int32_t>(obj, 0x328) = mem.at<int16_t>(table() + 2 * uint32_t(int32_t(mem.at<int8_t>(blk + 9))));
        set_motion_blend(obj, 47, 8, 7);
        next();
    }
        [[fallthrough]];
    case 1:
        if (of<int16_t>(obj, 0xF4) <= 37) {
            mem.at<uint8_t>(blk) = 1;
            hermit_hits_and_draw(obj);
            return;
        }
        next();
        [[fallthrough]];
    case 2: {
        const int32_t c = of<int32_t>(obj, 0x31C) + 1;
        of<int32_t>(obj, 0x31C) = c;
        if (c < of<int32_t>(obj, 0x324))
            return hermit_hits_and_draw(obj);
        of<int32_t>(obj, 0x31C) = 0;
        const uint32_t t = task_create(F::hermit_spit_ball, 0x3C8);
        for (uint32_t i = 0; i < 3; ++i)
            mem.set32(t + 0x20 + 4 * i, mem.u32(blk + 0x190 + 4 * i));
        const float s = of<float>(obj, 0x94) * 0.3f;
        mem.at<int16_t>(t + 0x66) = 0;
        mem.set32(t + 0x94, fbits(s));
        float w[3];
        if (pm_boss()) {
            // PC 0x4EE627: the aim point times 0.0020161290 (1 / 496 as a float) through stored matrix 6
            const float x = (float(int32_t(rand() & 0xFF)) + -127.5f) * 15.56f * 0.002016129f;
            const float y = (float(int32_t(rand() & 0xFF)) + -127.5f) * 11.67f * 0.002016129f;
            pm_m6().transform(x, y, 10.0f, w[0], w[1], w[2]);
        } else {
            cam_copro(0x74, {6});
            const float x = ((float(int32_t(rand() & 0xFF)) - 127.5f) * 15.56f) / 496.0f;
            const float y = ((float(int32_t(rand() & 0xFF)) - 127.5f) * 11.67f) / 496.0f;
            const Tgp::Out p = cam_copro(0x1A, {fbits(x), fbits(y), fbits(10.0f)});
            for (uint32_t i = 0; i < 3; ++i)
                std::memcpy(&w[i], &p.w[i], 4);
            cam_copro(0x06, {});
        }
        const Tgp::Out d = boss_unit(fbits(w[0] - mem.f32(t + 0x20)), fbits(w[1] - mem.f32(t + 0x24)),
                                     fbits(w[2] - mem.f32(t + 0x28)));
        // the speed: a program word read as a float in the arcade (0x85420), 1.0 on the PC (0x4EB1C0)
        const float k = pm_boss() ? 1.0f : mem.f32(hermit::spit_speed);
        float v[3];
        for (uint32_t i = 0; i < 3; ++i)
            std::memcpy(&v[i], &d.w[i], 4);
        mem.set32(t + 0x78, fbits(v[0] * k));
        mem.set32(t + 0x80, fbits(v[2] * k));
        mem.set32(t + 0x7C, fbits(v[1] * k));
        const int32_t n = of<int32_t>(obj, 0x328) - 1;
        of<int32_t>(obj, 0x328) = n;
        if (n == 0) {
            of<int32_t>(obj, 0x31C) = 16;
            next();
        }
        if (!countdown())
            return hermit_hits_and_draw(obj);
        return end();
    }
    case 3:
        if (!countdown())
            return hermit_hits_and_draw(obj);
        return end();
    default: return end();
    }
}

// 0x86990 hermit_act32_hit_reaction (action 32, after a hit): back in place, no claw, MO_b3_dam_a / _b (0xCA100) at
// random; pushed back (on the wall up to y -310, on the floor up to z 2700) until near its end, then the next sequence
template <class P> void Game<P>::hermit_act32_hit_reaction(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    const int16_t step = of<int16_t>(obj, 0x66);
    if (step == 0) {
        hermit_action_done(obj);
        mem.at<uint8_t>(blk + 4) = 27;
        const int32_t r = int32_t(rand()) % 2;
        set_motion(obj, uint32_t(int32_t(mem.at<int16_t>(hermit::hurt_motions + 2 * uint32_t(r)))));
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    if (step == 0 || step == 1) {
        if (hermit_motion_end(obj)) {
            hermit_hit_reaction_pick(obj);
        } else {
            const float d = mem.f32(hermit::hurt_speed);
            if (mem.at<uint8_t>(blk + 5) != 0) {
                const float z = of<float>(obj, 0x28) + d;
                of<float>(obj, 0x28) = z;
                if (double(z) > 2700.0)
                    of<uint32_t>(obj, 0x28) = 0x4528C000;   // 2700.0
            } else {
                const float y = of<float>(obj, 0x24) - d;
                of<float>(obj, 0x24) = y;
                if (double(y) < -310.0)
                    of<uint32_t>(obj, 0x24) = 0xC39B0000;   // -310.0
            }
            mem.at<uint8_t>(blk) = 1;
        }
    }
    hermit_hits_and_draw(obj);
}

// 0x86AB0 hermit_hit_reaction_pick: the sequence after a hit by the place (wall / floor) and the distance (100):
// 31 / 30 on the wall, 29 / 28 on the floor; hit taken again (flags bit 28)
template <class P> void Game<P>::hermit_hit_reaction_pick(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    const bool far = !(of<float>(obj, 0xA0) <= 100.0f);
    uint8_t s;
    if (mem.at<uint8_t>(blk + 5) != 0)
        s = far ? 28 : 29;
    else
        s = far ? 30 : 31;
    mem.at<uint8_t>(blk + 3) = s;
    mem.at<uint8_t>(blk + 2) = 0;
    of<int16_t>(obj, 0x60) = 0;
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x10000000u;
}

// 0x86B20 hermit_hits_and_draw (every action): the part hits; a hit (once until the next hit reaction: flags bit 28)
// takes the damage off the hit points: at 0 the kill score 1500, the death sounds and hermit_death_sink; else the
// hurt sound, 48 frames of the hurt blink and action 32. Drawn (nothing while block +1), the motion frame advanced by
// block +0, the place in camera space, the depth list
template <class P> void Game<P>::hermit_hits_and_draw(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    mem.at<uint8_t>(G::hit_class) = 0;
    mem.at<uint16_t>(G::hit_damage) = 0;
    mem.set32(G::hit_type, uint32_t(int32_t(of<int16_t>(obj, 0xCC))));
    enemy_part_hits(obj);
    const uint32_t blk2 = of<uint32_t>(obj, 0x37C);
    if (mem.at<uint16_t>(G::hit_damage) != 0 && ((of<uint32_t>(obj, 0x58) >> 28) & 1) == 0) {
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x10000000u;
        const uint16_t hp = uint16_t(of<uint16_t>(obj, 0x70) - mem.at<uint16_t>(G::hit_damage));
        of<uint16_t>(obj, 0x70) = hp;
        if (int32_t(uint32_t(hp) << 16) <= 0) {
            kill_score_shooter(obj, 1500);
            const uint32_t t = task_create(F::hermit_hit_sounds, 0x58);
            mem.set32(t + 0x50, 0);
            of<int16_t>(obj, 0x70) = 0;
            of<int16_t>(obj, 0x66) = 0;
            of<uint32_t>(obj, 0) = F::hermit_death_sink;
        } else {
            sound_cue(mem.u32(hermit::snd_hurt));
            mem.at<uint8_t>(blk2 + 8) = 48;
            of<int16_t>(obj, 0x66) = 0;
            of<int16_t>(obj, 0x60) = 32;
        }
    }
    of<int16_t>(obj, 0x68) = mem.at<uint8_t>(blk + 1) != 0 ? 3 : 0;
    uint8_t b = mem.at<uint8_t>(blk + 8);
    if (b != 0)
        --b;
    mem.at<uint8_t>(blk + 8) = b;
    object_draw(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + int32_t(mem.at<int8_t>(blk));
    mem.at<uint8_t>(blk) = 0;
    hermit_view_point(obj);
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xFFFFFEF9u;
    depth_list_add(obj);
}

// 0x86CB0 hermit_spit_ball (a task from the mouth): shootable, model 0x1B96, spinning, growing from 0 to 1.5x its
// start scale on the way (+0x33C full scale, +0x340 growth, +0x344 per frame); within 10 units of the camera it is
// clipped there and hits the player (the spit round restarts, block +9) and blinks out in 30 frames, or ends when it
// missed; shot: sound and hermit_spit_ball_shot
template <class P> void Game<P>::hermit_spit_ball(uint32_t obj)
{
    const int16_t step = of<int16_t>(obj, 0x66);
    bool fly = step == 1;
    if (step == 0) {
        of<uint32_t>(obj, 0x58) = 1;
        of<uint32_t>(obj, 0x340) = 0;
        of<uint32_t>(obj, 0xC8) = 0;
        of<float>(obj, 0x33C) = of<float>(obj, 0x94) * 1.5f;
        hermit_view_point(obj);
        of<uint32_t>(obj, 0x2C) = rand();
        of<uint32_t>(obj, 0x30) = rand();
        const uint32_t r = rand();
        const float d = of<float>(obj, 0xA0);
        of<uint32_t>(obj, 0x54) = 0x1B96;
        of<uint32_t>(obj, 0x34) = r;
        of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) & ~4u);
        of<uint32_t>(obj, 0x90) = mem.u32(hermit::ball_radius);
        of<float>(obj, 0x344) = of<float>(obj, 0x80) / d;
        sound_cue(mem.u32(hermit::snd_spit));
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
        fly = true;
    }
    if (fly) {
        LocalScope scope(*this);
        const uint32_t from = local_alloc(12);
        mem.set32(from, of<uint32_t>(obj, 0x98));
        mem.set32(from + 4, of<uint32_t>(obj, 0x9C));
        mem.set32(from + 8, of<uint32_t>(obj, 0xA0));
        const float grow = of<float>(obj, 0x340);
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + of<float>(obj, 0x78);
        of<float>(obj, 0x28) = of<float>(obj, 0x28) + of<float>(obj, 0x80);
        of<uint32_t>(obj, 0x2C) = of<uint32_t>(obj, 0x2C) + 0x600;
        of<float>(obj, 0x340) = grow + of<float>(obj, 0x344);
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + of<float>(obj, 0x7C);
        of<float>(obj, 0x94) = of<float>(obj, 0x33C) * grow;
        hermit_view_point(obj);
        if (of<float>(obj, 0xA0) > 10.0f) {
            fly = false;   // on to the shot test
        } else {
            point_clip_to_depth(obj + 0x98, from, 10.0f);
            if (pm_boss()) {   // PC 0x4EEDEA: back through stored matrix 6
                float o[3];
                pm_m6().transform(of<float>(obj, 0x98), of<float>(obj, 0x9C), of<float>(obj, 0xA0), o[0], o[1], o[2]);
                of<float>(obj, 0x20) = o[0];
                of<float>(obj, 0x24) = o[1];
                of<float>(obj, 0x28) = o[2];
            } else {
                cam_copro(0x74, {6});
                const Tgp::Out p = cam_copro(0x1A, {of<uint32_t>(obj, 0x98), of<uint32_t>(obj, 0x9C), of<uint32_t>(obj, 0xA0)});
                of<uint32_t>(obj, 0x20) = p.w[0];
                of<uint32_t>(obj, 0x24) = p.w[1];
                cam_copro(0x06, {});
                of<uint32_t>(obj, 0x28) = p.w[2];
            }
            if (projectile_hit_player(obj + 0x98, 1, 4) == 0) {
                task_exit();
                return;
            }
            mem.at<uint8_t>(mem.u32(hermit::block) + 9) = 0;
            of<int32_t>(obj, 0x31C) = 30;
            of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) | 4);
            of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
            fly = true;   // on to the blink count
        }
        if (fly) {
            const int32_t c = of<int32_t>(obj, 0x31C) - 1;
            of<int32_t>(obj, 0x31C) = c;
            if (c == 0) {
                task_exit();
                return;
            }
        }
    } else {
        const int32_t c = of<int32_t>(obj, 0x31C) - 1;
        of<int32_t>(obj, 0x31C) = c;
        if (c == 0) {
            task_exit();
            return;
        }
    }
    if ((of<uint16_t>(obj, 0x58) >> 8) & 1) {
        sound_cue(mem.u32(hermit::snd_ball_shot));
        of<uint32_t>(obj, 0) = F::hermit_spit_ball_shot;
        of<int16_t>(obj, 0x66) = 0;
        hermit_spit_ball_shot(obj);
        return;
    }
    hermit_spit_ball_draw(obj);
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xFFFFFEF9u;
    depth_list_add(obj);
}

// 0x87060 hermit_spit_ball_shot: blinks 30 frames, then gone
template <class P> void Game<P>::hermit_spit_ball_shot(uint32_t obj)
{
    const int16_t step = of<int16_t>(obj, 0x66);
    if (step == 0) {
        of<int32_t>(obj, 0x2EC) = 30;
        of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) | 4);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    if (step == 0 || step == 1) {
        const int32_t c = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = c;
        if (c == 0) {
            task_exit();
            return;
        }
    }
    hermit_spit_ball_draw(obj);
}

// 0x870C0 hermit_spit_ball_draw: at its place, angles and scale; while blinking (draw flag 2) not on even blink
// phases; its camera-space place +0x38
template <class P> void Game<P>::hermit_spit_ball_draw(uint32_t obj)
{
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x86, {of<uint32_t>(obj, 0x34), of<uint32_t>(obj, 0x30), of<uint32_t>(obj, 0x2C)});
    const uint32_t s = of<uint32_t>(obj, 0x94);
    cam_copro(0x13, {s, s, s});
    if (!((of<uint8_t>(obj, 0x3C4) >> 2) & 1) || mem.at<int32_t>(G::blink_phase) % 2 != 0)
        geo_draw_model(of<uint32_t>(obj, 0x54));
    const Tgp::Out o = cam_copro(0x20, {});
    of<uint32_t>(obj, 0x38) = o.w[0];
    of<uint32_t>(obj, 0x3C) = o.w[1];
    cam_copro(0x06, {});
    of<uint32_t>(obj, 0x40) = o.w[2];
}

// 0x871A0 hermit_action_done: back in his place (on the floor x / y 0xCA110, on the wall x / z 0xCA11C)
template <class P> void Game<P>::hermit_action_done(uint32_t obj)
{
    if (mem.at<uint8_t>(of<uint32_t>(obj, 0x37C) + 5) != 0) {
        of<uint32_t>(obj, 0x20) = mem.u32(hermit::home_floor);
        of<uint32_t>(obj, 0x24) = mem.u32(hermit::home_floor + 4);
        return;
    }
    of<uint32_t>(obj, 0x20) = mem.u32(hermit::home_wall);
    of<uint32_t>(obj, 0x28) = mem.u32(hermit::home_wall + 8);
}

// 0x871F0 hermit_attack_hit (who: 0 / 1 a player, else either): with two players that one, alone the player in the
// game; player_hit(.., 1, 6)
template <class P> void Game<P>::hermit_attack_hit(int who)
{
    int p;
    if (mem.at<uint16_t>(G::players_in_game) == 2) {
        if (who == 0)
            p = 0;
        else if (who == 1)
            p = 1;
        else
            p = int32_t(rand()) % 2;
    } else {
        p = mem.at<int8_t>(G::solo_player_is_p2);
    }
    player_hit(p, 1, 6);
}

// 0x87560 hermit_part_draw_hook (part): by the part's role (0x1184E0): the claw's role (block +4) marks 0x520AE2;
// role 1 (the head) keeps the mouth's place (block +0x190) through stored matrix 1; role 27 (the face) draws the
// part and the face animation 0x1AEE + blink phase % 50, during the hurt blink on odd frames with the geometrizer's
// flash attribute (0x802400, written to it directly); other parts their model
template <class P> void Game<P>::hermit_part_draw_hook(uint32_t part)
{
    const uint32_t body = mem.u32(G::body_current);
    const uint32_t type = uint32_t(int32_t(mem.at<int16_t>(body + 0xCC)));
    const uint32_t blk = mem.u32(body + 0x37C);
    const int8_t role = mem.at<int8_t>(0x1184E0 + 248 * type + 8 * mem.u32(part + 0x38));
    if (role == mem.at<int8_t>(blk + 4))
        mem.at<uint16_t>(0x520AE2) = 1;
    if (role == 1 && pm_boss()) {
        // PC 0x4EF315: the part matrix (here the arcade body drawing's, in the PC's layout) times stored matrix 1
        // (mul_stored 0x4EBE78), the mouth point through it
        pm::Matrix m = pm_copro_current();
        m.mul_stored(pm_camera);
        float o[3];
        m.transform(0.0f, -2.0f, -5.0f, o[0], o[1], o[2]);   // the PC's mouth offset 0x5F26C0
        mem.set32(blk + 0x190, fbits(o[0]));
        mem.set32(blk + 0x194, fbits(o[1]));
        mem.set32(blk + 0x198, fbits(o[2]));
    } else if (role == 1) {
        cam_copro(0x05, {});
        cam_copro(0x77, {1});
        const Tgp::Out o = cam_copro(0x1A, {mem.u32(hermit::mouth), mem.u32(hermit::mouth + 4), mem.u32(hermit::mouth + 8)});
        mem.set32(blk + 0x190, o.w[0]);
        mem.set32(blk + 0x194, o.w[1]);
        cam_copro(0x06, {});
        mem.set32(blk + 0x198, o.w[2]);
    }
    const uint32_t model = mem.u32(part);
    if (role != 27) {
        geo_draw_model(model);
        return;
    }
    if (mem.at<uint8_t>(blk + 8) == 0 || mem.at<int32_t>(G::frame_counter) % 2 == 0) {
        geo_draw_model(model);
        geo_draw_model(uint32_t(mem.at<int32_t>(G::blink_phase) % 50 + 0x1AEE));
        return;
    }
    // the flash draw goes to the geometrizer directly (the matrix, then the model with attribute 0x802400); the runtime
    // draws such models (Runtime::geo_write). UNVERIFIED (teaser): hardware writes are not in the captures
    const uint32_t fifo = mem.u32(0x790);
    auto flash = [&](uint32_t m) {
        const Tgp::Out mx = cam_copro(0x11, {});
        hw_write32(0x8000B0, 0xB0B);
        for (int k = 0; k < 12; ++k)
            hw_write32(fifo, mx.w[k]);
        const uint32_t e = hermit::models + 16 * m;
        hw_write32(0x800010, 0x101);
        hw_write32(fifo, mem.u32(e));
        hw_write32(fifo, 0x802400);
        hw_write32(fifo, mem.u32(e + 8));
        hw_write32(fifo, mem.u32(e + 12));
    };
    flash(model);
    flash(uint32_t(mem.at<int32_t>(G::blink_phase) % 50 + 0x1AEE));
}

// 0x87770 hermit_title_task: once he is out of the hole (block +6) the camera's eye / look-at kept and the game
// camera held; the title sprites 0xDE / 0xE1 at frame 1; for 180 frames the camera on path 92; then the camera back,
// the sprites cleared, gone
template <class P> void Game<P>::hermit_title_task(uint32_t t)
{
    const uint32_t blk = mem.u32(mem.u32(t + 0x18) + 0x37C);
    const uint32_t c = G::camera_records;
    const int16_t step = mem.at<int16_t>(t + 0x66);
    bool run = step == 1;
    if (step == 0 && mem.at<uint8_t>(blk + 6) != 0) {
        for (uint32_t i = 0; i < 3; ++i) {
            mem.set32(t + 0x394 + 4 * i, mem.u32(c + 4 * i));
            mem.set32(t + 0x3A0 + 4 * i, mem.u32(c + 0x18 + 4 * i));
        }
        mem.set32(G::camera_hold_hostage, 1);
        mem.set32(t + 0x50, 0);
        mem.at<uint16_t>(t + 0x66) = uint16_t(mem.at<uint16_t>(t + 0x66) + 1);
        run = true;
    }
    if (run) {
        if (mem.at<int32_t>(t + 0x50) == 180) {
            for (uint32_t i = 0; i < 3; ++i) {
                mem.set32(c + 4 * i, mem.u32(t + 0x394 + 4 * i));
                mem.set32(c + 0x18 + 4 * i, mem.u32(t + 0x3A0 + 4 * i));
            }
            mem.set32(G::camera_hold_hostage, 0);
            sprite_draw(0x1004000, 1, 6, 0xDC);
            sprite_draw(0x1004000, 33, 34, 0xE1);
            boss_event(kBossClockStart);   // PC 0x4EF4D5
            task_exit();
            return;
        }
        if (mem.at<int32_t>(t + 0x50) == 1) {
            boss_event(kBossEntrance);   // PC 0x4EF41D
            sprite_place(0x1004000, 1, 0, 1, 6, 0xDE);
            sprite_place(0x1004000, 1, 0, 33, 34, 0xE1);
        }
        LocalScope scope(*this);
        const uint32_t loc = local_alloc(8);
        camera_path_eval(92, fbits(float(mem.at<int32_t>(t + 0x50))), c, c + 0x18, loc, loc + 4);
    }
    mem.set32(t + 0x50, mem.u32(t + 0x50) + 1);
}

// 0x878E0 hermit_life_bar_task: once the scene state is 2, after 20 frames the boss life bar (life_bar_init 11, the
// block's start hit points), updated with the hit points; at 0 emptied, then gone
template <class P> void Game<P>::hermit_life_bar_task(uint32_t t)
{
    const uint32_t owner = mem.u32(t + 0x18);
    const uint32_t blk = of<uint32_t>(owner, 0x37C);
    switch (mem.at<int16_t>(t + 0x66)) {
    case 0:
        if (mem.at<uint8_t>(G::scene_state) != 2)
            return;
        mem.set32(t + 0x50, 0);
        mem.at<uint16_t>(t + 0x66) = uint16_t(mem.at<uint16_t>(t + 0x66) + 1);
        [[fallthrough]];
    case 1: {
        const int32_t n = mem.at<int32_t>(t + 0x50) + 1;
        mem.at<int32_t>(t + 0x50) = n;
        if (n != 20)
            return;
        coop4_boss_scale(owner + ob(0x70), blk + 0xA);   // 4P: the hit points for 3 / 4 players
        life_bar_init(11, 1, mem.at<int16_t>(blk + 0xA));
        mem.at<uint16_t>(t + 0x66) = uint16_t(mem.at<uint16_t>(t + 0x66) + 1);
    }
        [[fallthrough]];
    case 2:
        if (of<uint16_t>(owner, 0x70) != 0) {
            life_bar_update(of<int16_t>(owner, 0x70), 0);
            return;
        }
        mem.at<uint16_t>(t + 0x66) = uint16_t(mem.at<uint16_t>(t + 0x66) + 1);
        [[fallthrough]];
    case 3:
        if (life_bar_update(of<int16_t>(owner, 0x70), 1) == -1)
            task_exit();
        return;
    default: return;
    }
}

// 0x879A0 hermit_drop_dust_spawn: the landing dust (models 799..0x333) at 0xCA140
template <class P> void Game<P>::hermit_drop_dust_spawn()
{
    const uint32_t t = task_create(F::hermit_drop_dust, 0x58);
    mem.set32(t + 0x2C, 0);
    mem.set32(t + 0x30, 0);
    mem.set32(t + 0x34, 0);
    for (uint32_t i = 0; i < 3; ++i)
        mem.set32(t + 0x20 + 4 * i, mem.u32(hermit::dust_place + 4 * i));
    mem.set32(t + 0x54, 799);
}

// 0x879E0 hermit_death_sink: on the floor (y -310, no roll), the cry, MO 46; at frame 190 the sound; near its end out
// of the count, the attack slot freed, the title sprites' bank 0x3F freed
template <class P> void Game<P>::hermit_death_sink(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    const int16_t step = of<int16_t>(obj, 0x66);
    if (step == 3) {   // Boss Mode (PC 0x4EEA73): 200 frames, then the win (0x412C08, each frame after)
        const int32_t n = of<int32_t>(obj, 0x2EC) - 1;   // (PC +0x2F4)
        of<int32_t>(obj, 0x2EC) = n;
        if (n <= 0)
            boss_event(kBossWin);
        hermit_hits_and_draw(obj);
        return;
    }
    if (step == 0) {
        boss_event(kBossClockStop);   // PC 0x4EE96F
        of<int32_t>(obj, 0x34) = 0;
        of<uint32_t>(obj, 0x24) = 0xC39B0000;   // -310.0
        sound_cue(0xA00002);
        set_motion(obj, 46);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    if (step == 0 || step == 1) {
        if (!hermit_motion_end(obj)) {
            if (of<uint16_t>(obj, 0xF4) == 0xBE)
                sound_cue(0xA0123A);
            mem.at<uint8_t>(blk) = 1;
        } else if (boss_on()) {   // PC 0x4EE9D9: Boss Mode: step 3, 200 frames to the win (no count / bank change)
            of<int16_t>(obj, 0x66) = 3;
            of<int32_t>(obj, 0x2EC) = 200;
        } else {
            mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
            mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
            if (of<uint8_t>(obj, 0x2E7) != 0xFF)
                enemy_attack_slot_free(obj);
            sprite_bank_free(0x3F);
            of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
        }
    } else if (step == 2 && pc_path() && mem.at<uint8_t>(G::event_flags + 0xFD) == 1) {
        // PC Mode / Boss Mode: THOTD.EXE 0x4EEA4B: once event flag 0xFD is set the body is freed and the task ends
        // (the arcade keeps drawing it until the chapter ends; pmch34)
        enemy_free_extras(obj);
        body_free_parts(obj);
        task_exit();
        return;
    }
    hermit_hits_and_draw(obj);
}

// 0x87AD0 hermit_drop_sound: the drop sound at frames 1 and 4, then gone
template <class P> void Game<P>::hermit_drop_sound(uint32_t t)
{
    const int32_t n = mem.at<int32_t>(t + 0x50) + 1;
    mem.at<int32_t>(t + 0x50) = n;
    if (n == 1) {
        sound_cue(mem.u32(hermit::snd_drop_end));
    } else if (n == 4) {
        sound_cue(mem.u32(hermit::snd_drop_end));
        task_exit();
    }
}

// 0x87B10 hermit_hit_sounds: the death cries at frames 1 and 8, then gone
template <class P> void Game<P>::hermit_hit_sounds(uint32_t t)
{
    const int32_t n = mem.at<int32_t>(t + 0x50) + 1;
    mem.at<int32_t>(t + 0x50) = n;
    if (n == 1) {
        sound_cue(mem.u32(hermit::snd_die_a));
    } else if (n == 8) {
        sound_cue(mem.u32(hermit::snd_die_b));
        task_exit();
    }
}

// 0x87B50 hermit_drop_dust: models 800..0x333 at its place and heading, scaled (0xCA14C), then gone
template <class P> void Game<P>::hermit_drop_dust(uint32_t t)
{
    const int32_t m = mem.at<int32_t>(t + 0x54) + 1;
    mem.at<int32_t>(t + 0x54) = m;
    if (m > 0x333) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {mem.u32(t + 0x20), mem.u32(t + 0x24), mem.u32(t + 0x28)});
    const uint32_t s = mem.u32(hermit::dust_place + 0xC);
    cam_copro(0x15, {mem.u32(t + 0x30)});
    cam_copro(0x13, {s, s, s});
    geo_draw_model(mem.u32(t + 0x54));
    cam_copro(0x06, {});
}

// kind 24's draw: shadow at its height, the body, model 0x1B79 (his machine) at his place
template <class P> void Game<P>::kyurian_draw(uint32_t obj)
{
    shadow_setup(of<float>(obj, 0x24));
    object_draw(obj);
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    geo_draw_model(0x1B79);
    cam_copro(0x06, {});
}

// 0x87C50 kind24_kyurian_curien (kind 24): Dr. Curien (type 60, motion 0x12A) in his place after the fight
template <class P> void Game<P>::kind24_kyurian_curien(uint32_t obj)
{
    of<int16_t>(obj, 0xCC) = 60;
    mem.set32(G::hit_type, 60);
    body_init(obj, 60, 0x12A);
    creature_type_setup(obj);
    of<uint32_t>(obj, 0x20) = 0x44074000;   // 541.0
    of<uint32_t>(obj, 0x28) = 0x451A0000;   // 2464.0
    of<uint32_t>(obj, 0x30) = 0x8000;
    of<uint32_t>(obj, 0x24) = 0xC2858F5C;   // -66.78
    of<uint32_t>(obj, 0) = F::kind24_kyurian_wait;
    kyurian_draw(obj);
}

// 0x87D10 kind24_kyurian_wait: until flag 128
template <class P> void Game<P>::kind24_kyurian_wait(uint32_t obj)
{
    if (mem.at<uint8_t>(G::event_flags + 128) != 0)
        of<uint32_t>(obj, 0) = F::kind24_kyurian_rise;
    kyurian_draw(obj);
}

// 0x87D80 kind24_kyurian_rise: at flag 129 MO 0x126 and the float (acceleration -0.01)
template <class P> void Game<P>::kind24_kyurian_rise(uint32_t obj)
{
    if (mem.at<uint8_t>(G::event_flags + 129) != 0) {
        set_motion_blend(obj, 0x126, 60, 10);
        of<uint32_t>(obj, 0x88) = 0xBC23D70A;   // -0.01
        of<uint32_t>(obj, 0x7C) = 0;
        of<uint32_t>(obj, 0) = F::kind24_kyurian_float;
    }
    kyurian_draw(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x87E20 kind24_kyurian_float: speed y to -0.3 at most; past y -96.78 his parts and block freed, gone
template <class P> void Game<P>::kind24_kyurian_float(uint32_t obj)
{
    float v = of<float>(obj, 0x7C);
    if (v <= -0.3f)
        v = -0.3f;
    else
        v = v + of<float>(obj, 0x88);
    const float y = of<float>(obj, 0x24) + v;
    of<float>(obj, 0x7C) = v;
    of<float>(obj, 0x24) = y;
    if (!(y > -96.78f)) {
        body_free_parts(obj);
        enemy_free_extras(obj);
        task_exit();
        return;
    }
    kyurian_draw(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

} // namespace hotd::port
