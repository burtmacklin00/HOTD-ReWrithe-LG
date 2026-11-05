// Enemies: the Murrer (kinds 33-36 and 39, creature type 6 `molb`; arcade cluster 0x64BB0..0x66820), read from the
// arcade disassembly (Ghidra's C loses the coprocessor FIFO words and the stack locals). Murrers crawl along a
// quadratic spline to the front, take their place in the attack queue (attack_slots used as a queue: the front Murrer
// lunges), leap at the camera to hit a player, drop back, menace, regroup inside molb_area and come again.
// Per-object words by arcade offset: +0x31C / +0x2EC counters, +0x320 landed / bounce flag, +0x328 counted in
// molb_out_count, +0x32C leap spot (bit of molb_leaping, -1 none), +0x368 the spawn record, +0x37C the spline buffer,
// +0xC0 the heading to turn to, +0x66 kind 39's step.
#include "port/game.h"

#include <cstring>

namespace hotd::port {

// ---------------------------------------------------------------- the attack queue (attack_slots as a queue)

// 0x64C70: slot i's holder moves one place back (after slot i + 1's, recursively); its +0x2E7 counts up
template <class P> void Game<P>::attack_queue_shift(int32_t i)
{
    const uint32_t s = G::attack_slots + uint32_t(i) * 20;
    if (mem.at<uint8_t>(s) != 1)
        return;
    attack_queue_shift(i + 1);
    const uint32_t h = mem.u32(s + 4);
    of<int8_t>(h, 0x2E7) = int8_t(of<int8_t>(h, 0x2E7) + 1);
    mem.at<uint8_t>(s) = 0;
    const uint32_t g = mem.u32(s + 4);
    mem.set32(s + 4, 0);
    mem.at<uint8_t>(s + 20) = 1;
    mem.set32(s + 24, g);
}

// 0x64CD0: the object leaves its place and goes to the front (the others move back one)
template <class P> void Game<P>::attack_queue_push_front(uint32_t obj)
{
    const int32_t own = of<int8_t>(obj, 0x2E7);
    mem.at<uint8_t>(G::attack_slots + uint32_t(own) * 20) = 0;
    mem.set32(G::attack_slots + uint32_t(of<int8_t>(obj, 0x2E7)) * 20 + 4, 0);
    if (mem.at<uint8_t>(G::attack_slots) == 1) {
        attack_queue_shift(1);
        const uint32_t h = mem.u32(G::attack_slots + 4);
        of<int8_t>(h, 0x2E7) = int8_t(of<int8_t>(h, 0x2E7) + 1);
        mem.at<uint8_t>(G::attack_slots + 20) = 1;
        mem.set32(G::attack_slots + 24, h);
    }
    mem.set32(G::attack_slots + 4, obj);
    mem.at<uint8_t>(G::attack_slots) = 1;
    of<int8_t>(obj, 0x2E7) = 0;
}

// ---------------------------------------------------------------- kind 34: a group of Murrers

// 0x64BB0 (kind 34): molb_out_count / molb_leaping / molb_lunge_target cleared, molb_area from the record (+4..+0x13);
// one member task per 0x34-byte member record from record +0x18 (until a member whose +0x1A is 0), each with the
// record's first word (its event flag) in +0x324; then idle
template <class P> void Game<P>::kind34_molb_group(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    mem.set32(G::molb_out_count, 0);
    mem.set32(G::molb_leaping, 0);
    mem.set32(G::molb_lunge_target, 0);
    for (int i = 0; i < 4; ++i)
        mem.set32(G::molb_area + uint32_t(i) * 4, mem.u32(rec + 4 + uint32_t(i) * 4));
    uint32_t m = rec + 0x18;
    do {
        const uint32_t t = task_create(F::kind34_molb_member, uint32_t(sizeof(Obj)));
        if constexpr (P::pc)
            task_clear_work(t);
        of<uint32_t>(t, 0x368) = m;
        of<uint32_t>(t, 0x324) = mem.u32(rec);
        m += 0x34;
    } while (mem.at<uint16_t>(m + 0x1A) != 0);
    of<uint32_t>(obj, 0x31C) = 0;
    mem.at<Obj>(obj).routine = F::kind34_molb_group_idle;
    of<uint32_t>(obj, 0x320) = 0;
    kind34_molb_group_idle(obj);
}

// 0x64C30: the group object itself does nothing; PC: it ends in Chapter 1 event 4 scene 2 (also checked at the end of
// kind34_molb_group, inline)
template <class P> void Game<P>::kind34_molb_group_idle(uint32_t)
{
    if (pc_path()) {
        if (mem.at<uint8_t>(G::chapter) == 0 && mem.at<uint8_t>(G::event_index) == 4 &&
            mem.at<uint8_t>(G::scene_number) == 2)
            task_exit();
    }
}

// the floor height of a Murrer at a probed ground height g: g + the type's height; PC: (g + pc_molb_height_offset) +
// the type's height
template <class P> float Game<P>::molb_floor_y(uint32_t obj, float g)
{
    const float h = mem.f32(G::creature_floor_height + uint32_t(int32_t(of<int16_t>(obj, 0xCC))) * 4);
    if (pc_path())
        return (g + molb_height_offset()) + h;
    else
        return g + h;
}

// 0x64C40 (kind 33): molb_area from the record (+0..+0xF), molb_leaping / molb_lunge_target cleared, ends
template <class P> void Game<P>::kind033_murrer_reset(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    uint32_t a[4];
    for (int i = 0; i < 4; ++i)
        a[i] = mem.u32(rec + uint32_t(i) * 4);
    mem.set32(G::molb_leaping, 0);
    mem.set32(G::molb_lunge_target, 0);
    for (int i = 0; i < 4; ++i)
        mem.set32(G::molb_area + uint32_t(i) * 4, a[i]);
    task_exit();
}

// 0x64E10: a member: state 6, type 6, body (MO 0x12F), random start frame (rand() & 31), position / angles from its
// member record, put on the floor (ground_height + the type's floor height), type set-up, a wide attack position, a
// spline buffer (0x48 bytes), waits with the record's delay (+0x1A) in +0x31C
template <class P> void Game<P>::kind34_molb_member(uint32_t obj)
{
    if (pc_path()) {
        // PC: the members of Chapter 1 event 4 scene 1 stand 12 higher
        const bool raised = mem.at<uint8_t>(G::chapter) == 0 && mem.at<uint8_t>(G::event_index) == 4 &&
                            mem.at<uint8_t>(G::scene_number) == 1;
        molb_height_offset_set(raised ? 0x41400000u : 0);
    }
    of<int16_t>(obj, 0x5C) = 6;
    of<int16_t>(obj, 0xCC) = 6;
    of<uint32_t>(obj, 0x58) = 0;
    of<uint32_t>(obj, 0xC8) = 0;
    mem.set32(G::hit_type, 6);
    const uint32_t rec = of<uint32_t>(obj, 0x368);
    body_init(obj, 6, 0x12F);
    of<uint32_t>(obj, 0xF8) = rand() & 31;
    for (uint32_t i = 0; i < 0x18; i += 4)
        of<uint32_t>(obj, 0x20 + i) = mem.u32(rec + i);
    of<uint32_t>(obj, 0x24) = 0x47000000u;   // 32768: probe from high above
    const float g = ground_height(obj + ob(0x20));
    of<float>(obj, 0x24) = molb_floor_y(obj, g);
    if constexpr (P::pc) {
        ground_height(obj + ob(0x20));   // again, for the developers' debug message (cdecl, values not kept)
        pc_debug_print();
    }
    creature_type_setup(obj);
    attack_slot_take_wide(obj);
    if constexpr (P::pc)
        mem.set32(G::pc_wide_slot_kind, 4);
    const uint32_t buf = mem_alloc(0x48);
    of<int16_t>(obj, 0x6C) = 2;
    of<uint32_t>(obj, 0x74) = 0;
    of<uint32_t>(obj, 0xC0) = 0;
    of<uint32_t>(obj, 0xBC) = 0;
    of<int8_t>(obj, 0x2E8) = -1;
    of<int16_t>(obj, 0x68) = 1;
    of<uint32_t>(obj, 0x37C) = buf;
    const int32_t delay = mem.at<int16_t>(rec + 0x1A);
    of<int32_t>(obj, 0x32C) = -1;
    of<uint32_t>(obj, 0x320) = 0;
    mem.at<Obj>(obj).routine = F::kind34_molb_member_wait;
    of<uint32_t>(obj, 0x328) = 0;
    of<int32_t>(obj, 0x31C) = delay;
    kind34_molb_member_wait(obj);
}

// 0x64F00: at its event flag (+0x324) the members come out in waves: the group (+0x18) counts waves in its +0x31C
// (even: a wave; odd: wait until no enemy is alive) and members in +0x320; the wave table (group record +0x14, a byte
// per wave, 0xFF = all) says how many come out in it
template <class P> void Game<P>::kind34_molb_member_wait(uint32_t obj)
{
    const uint32_t grp = of<uint32_t>(obj, 0x18);
    const uint32_t waves = of<uint32_t>(grp, 0x35C) + 0x14;
    if (mem.at<uint8_t>(G::event_flags + of<uint32_t>(obj, 0x324)) != 0) {
        if ((of<uint8_t>(grp, 0x31C) & 1) == 0) {
            const int32_t w = of<int32_t>(grp, 0x31C) / 2;
            const uint8_t n = mem.at<uint8_t>(waves + uint32_t(w));
            if (n == 0xFF || of<int32_t>(grp, 0x320) < int32_t(int8_t(n))) {
                mem.set32(G::molb_out_count, mem.u32(G::molb_out_count) + 1);
                of<int32_t>(grp, 0x320) = of<int32_t>(grp, 0x320) + 1;
                of<uint32_t>(obj, 0x328) = 1;
                kind34_molb_member_wake(obj);
                return;
            }
            of<int32_t>(grp, 0x31C) = of<int32_t>(grp, 0x31C) + 1;
        } else if (mem.at<uint16_t>(G::enemies_alive) == 0) {
            const int32_t w = of<int32_t>(grp, 0x31C);
            of<uint32_t>(grp, 0x320) = 0;
            of<int32_t>(grp, 0x31C) = w + 1;
        }
    }
    kind35_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x64FC0: counted as an enemy, shootable, then the delay
template <class P> void Game<P>::kind34_molb_member_wake(uint32_t obj)
{
    enemy_counts_add(1);
    mem.at<Obj>(obj).routine = F::kind34_molb_member_delay;
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 1;
    kind34_molb_member_delay(obj);
}

// 0x65010: after +0x31C frames the crawl
template <class P> void Game<P>::kind34_molb_member_delay(uint32_t obj)
{
    const int32_t n = of<int32_t>(obj, 0x31C) - 1;
    of<int32_t>(obj, 0x31C) = n;
    if (n == 0) {
        kind35_molb_crawl_start(obj);
        return;
    }
    kind35_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// ---------------------------------------------------------------- kind 35: the Murrer's behaviour

// 0x65050: MO 0x12E, speed 0.35, random frame (rand() & 15); moves up to the first free attack position in front of
// its own; a spline from its position through the record's two points (+0x1C / +0x24, +0x28 / +0x30, each on the
// floor), the first step along it (0.008), state 0, then kind35_molb_follow_path
template <class P> void Game<P>::kind35_molb_crawl_start(uint32_t obj)
{
    LocalScope scope(*this);
    const uint32_t L = local_alloc(0x50);   // the original's fp + 0x40 .. + 0x8F
    const uint32_t rec = of<uint32_t>(obj, 0x368);
    set_motion(obj, 0x12E);
    const uint32_t r = rand();
    of<uint32_t>(obj, 0x31C) = 0;
    of<uint32_t>(obj, 0x74) = 0x3EB33333u;   // 0.35
    of<uint32_t>(obj, 0xF8) = r & 15;
    const int32_t own = of<int8_t>(obj, 0x2E7);
    for (int32_t i = 0; i < own; ++i) {
        const uint32_t s = G::attack_slots + uint32_t(i) * 20;
        if (mem.at<uint8_t>(s) == 0) {
            mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 20) = 0;
            mem.set32(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 20 + 4, 0);
            mem.set32(s + 4, obj);
            mem.at<uint8_t>(s) = 1;
            of<int8_t>(obj, 0x2E7) = int8_t(i);
            break;
        }
    }
    // the points: L + 0x00 = position, + 0x0C = (rec +0x1C, floor, rec +0x24), + 0x18 = (rec +0x28, floor, rec +0x30)
    mem.set32(L + 0x00, of<uint32_t>(obj, 0x20));
    mem.set32(L + 0x04, of<uint32_t>(obj, 0x24));
    mem.set32(L + 0x08, of<uint32_t>(obj, 0x28));
    mem.set32(L + 0x0C, mem.u32(rec + 0x1C));
    mem.set32(L + 0x14, mem.u32(rec + 0x24));
    const uint32_t q = L + 0x30;   // the probe point (fp + 0x70)
    mem.set32(q, mem.u32(rec + 0x1C));
    mem.set32(q + 4, 0x47000000u);
    mem.set32(q + 8, mem.u32(rec + 0x24));
    float g = ground_height(q);
    mem.set32(L + 0x18, mem.u32(rec + 0x28));
    mem.set32(q, mem.u32(rec + 0x28));
    mem.set32(L + 0x20, mem.u32(rec + 0x30));
    mem.set32(q + 8, mem.u32(rec + 0x30));
    mem.at<float>(L + 0x10) = molb_floor_y(obj, g);
    g = ground_height(q);
    const float h2 = molb_floor_y(obj, g);
    of<uint32_t>(obj, 0x33C) = 0;
    mem.at<float>(L + 0x1C) = h2;
    spline_segment_init(of<uint32_t>(obj, 0x37C), L);
    spline_advance(obj, of<uint32_t>(obj, 0x37C), of<uint32_t>(obj, 0x74), 0x3C03126Fu, L + 0x40, L + 0x44);
    relocate(4, L + 0x40, 4);
    relocate(5, L + 0x44, 4);
    mem.at<Obj>(obj).routine = F::kind35_molb_follow_path;
    of<uint32_t>(obj, 0xC4) = 0;
    of<uint32_t>(obj, 0x34) = 0;
    const uint32_t a = mem.u32(L + 0x40), b = mem.u32(L + 0x44);
    of<int16_t>(obj, 0x5C) = 0;
    of<uint32_t>(obj, 0xBC) = a;
    of<uint32_t>(obj, 0x2C) = a;
    of<uint32_t>(obj, 0xC0) = b;
    of<uint32_t>(obj, 0x30) = b;
    kind35_molb_follow_path(obj);
}

// 0x65230: past the spline's end (+0x33C > 1) the lunge; else a step along it (0.015) with its angles
template <class P> void Game<P>::kind35_molb_follow_path(uint32_t obj)
{
    // PC: compared as integers (the same for the spline parameters that occur)
    const bool past = pc_path() ? of<int32_t>(obj, 0x33C) > 0x3F800000 : of<float>(obj, 0x33C) > 1.0f;
    if (past) {
        kind35_molb_lunge_start(obj);
        return;
    }
    LocalScope scope(*this);
    const uint32_t L = local_alloc(0x10);   // fp + 0x40
    spline_advance(obj, of<uint32_t>(obj, 0x37C), of<uint32_t>(obj, 0x74), 0x3C75C28Fu, L, L + 4);
    relocate(4, L, 4);
    relocate(5, L + 4, 4);
    const uint32_t a = mem.u32(L), b = mem.u32(L + 4);
    of<uint32_t>(obj, 0xBC) = a;
    of<uint32_t>(obj, 0x2C) = a;
    of<uint32_t>(obj, 0xC0) = b;
    of<uint32_t>(obj, 0x30) = b;
    kind35_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// the lunge set-up (0x652A0 and inline in kind35_molb_face_turn / kind36_molb_murrer_attack): stopped, state 2,
// facing the camera, the front of the queue, the one Murrer that picks a target player (molb_lunge_target), MO 0x132
// from frame 22, kind35_molb_lunge_wait
template <class P> void Game<P>::molb_lunge_setup(uint32_t obj)
{
    for (uint32_t o : {0x80u, 0x7Cu, 0x78u, 0x8Cu, 0x88u, 0x84u})
        of<uint32_t>(obj, o) = 0;
    of<int16_t>(obj, 0x5C) = 2;
    of<uint32_t>(obj, 0x30) = mem.u32(G::camera_records + 0x10) - 0x8000u;
    attack_queue_push_front(obj);
    if (mem.u32(G::molb_lunge_target) == 0) {
        mem.set32(G::molb_lunge_target, obj);
        pick_target_player_rand(obj);
    } else {
        of<int8_t>(obj, 0x2E8) = -1;
    }
    set_motion(obj, 0x132);
    of<uint32_t>(obj, 0xF8) = 22;
    mem.at<Obj>(obj).routine = F::kind35_molb_lunge_wait;
    kind35_molb_lunge_wait(obj);
}

// 0x652A0
template <class P> void Game<P>::kind35_molb_lunge_start(uint32_t obj) { molb_lunge_setup(obj); }

// 0x65330: after motion frame 30 the leap decision
template <class P> void Game<P>::kind35_molb_lunge_wait(uint32_t obj)
{
    if (of<int16_t>(obj, 0xF4) > 30) {
        kind35_molb_leap_decide(obj);
        return;
    }
    kind35_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x65360: in combat, with no Murrer leaping and rand() & 7 != 0: a leap at a free screen spot, an arc of 26 frames
// (gravity 0.0408); else molb_lunge_target is freed and it regroups
template <class P> void Game<P>::kind35_molb_leap_decide(uint32_t obj)
{
    if (combat_active() == 0 || mem.u32(G::molb_leaping) != 0 || ((rand() & 7) & 0xFF) == 0 ||
        (gAttackHold && gAttackHold(obj))) {   // (ours: The Typing of the Dead 0's attack window, game.h: it regroups)
        mem.set32(G::molb_lunge_target, 0);
        kind35_molb_regroup(obj);
        return;
    }
    LocalScope scope(*this);
    const uint32_t L = local_alloc(0x10);
    of<uint32_t>(obj, 0x2EC) = 26;
    of<int16_t>(obj, 0x5C) = 3;
    molb_leap_target_pick(obj, L);
    velocity_for_arc(obj, obj + ob(0x20), L, 0x3D2740DAu, 0x41D00000u);
    const uint32_t yaw = mem.u32(G::camera_records + 0x10);
    mem.at<Obj>(obj).routine = F::kind35_molb_leap;
    of<uint32_t>(obj, 0x30) = yaw - 0x8000u;
    kind35_molb_leap(obj);
}

// 0x65410: the flight (+0x2EC frames)
template <class P> void Game<P>::kind35_molb_leap(uint32_t obj)
{
    const int32_t n = of<int32_t>(obj, 0x2EC) - 1;
    of<int32_t>(obj, 0x2EC) = n;
    if (n == 0) {
        kind35_molb_leap_hit(obj);
        return;
    }
    kind35_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x65450: at the screen: projectile_hit_player at its camera-space point, stopped, holds 30 frames
template <class P> void Game<P>::kind35_molb_leap_hit(uint32_t obj)
{
    projectile_hit_player(obj + ob(0x38), 1, 1);
    obj_velocity_clear(obj);
    of<uint32_t>(obj, 0x2EC) = 30;
    mem.at<Obj>(obj).routine = F::kind35_molb_leap_hold;
    kind35_molb_leap_hold(obj);
}

// 0x65490
template <class P> void Game<P>::kind35_molb_leap_hold(uint32_t obj)
{
    const int32_t n = of<int32_t>(obj, 0x2EC) - 1;
    of<int32_t>(obj, 0x2EC) = n;
    if (n == 0) {
        kind35_molb_drop_start(obj);
        return;
    }
    kind35_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x654D0: drops back: the point 30 in front of the camera (coprocessor: camera matrix, (0, 0, 30)), moved by two
// random amounts ((rand() & 255) - 127.5) / 25.5 in x and z, kept inside molb_area (1 inside its edges), put on the
// floor (floor_probe + the type's height); an arc to it at speed 0.5 (gravity 0.0136); state 3, kind35_molb_drop
template <class P> void Game<P>::kind35_molb_drop_start(uint32_t obj)
{
    LocalScope scope(*this);
    const uint32_t L = local_alloc(0x40);   // fp + 0x40 .. + 0x7F
    if (pc_path()) {
        // the PC's software matrix stack (the kind file's copies of the helpers)
        pc_matrix_push();
        pc_matrix_identity();
        pc_matrix_translate4(mem.f32(G::camera_records), mem.f32(G::camera_records + 4), mem.f32(G::camera_records + 8));
        pc_matrix_rotate_y(mem.u32(G::camera_records + 0x10));
        float ox = 0.0f, oy = 0.0f, oz = 0.0f;
        pc_matrix_transform2(0.0f, 0.0f, 30.0f, ox, oy, oz);
        mem.at<float>(L) = ox;
        mem.at<float>(L + 4) = oy;
        mem.at<float>(L + 8) = oz;
        pcm_pop();
    } else {
        Tgp::Out o, none;
        copro(0x21, nullptr, 0, none);
        const uint32_t cam[3] = {mem.u32(G::camera_records), mem.u32(G::camera_records + 4), mem.u32(G::camera_records + 8)};
        copro(0x12, cam, 3, none);
        const uint32_t yaw[1] = {mem.u32(G::camera_records + 0x10)};
        copro(0x15, yaw, 1, none);
        const uint32_t v[3] = {0, 0, 0x41F00000u};   // (0, 0, 30)
        copro(0x1A, v, 3, o);
        copro(0x06, nullptr, 0, none);
        mem.set32(L, o.w[0]);
        mem.set32(L + 4, o.w[1]);
        mem.set32(L + 8, o.w[2]);
    }
    const float k = ffrom(0x3D20A0A1u);   // 1 / 25.5
    float d = float(int32_t(rand() & 0xFF)) - 127.5f;
    mem.at<float>(L) = mem.f32(L) + d * k;
    d = float(int32_t(rand() & 0xFF)) - 127.5f;
    float x = mem.f32(L), z = mem.f32(L + 8) + d * k;
    const float* area = &mem.at<float>(G::molb_area);
    const float a0 = area[0], a1 = area[1], a2 = area[2], a3 = area[3];
    if (x > a0 - 1.0f)
        x = a0 - 1.0f;
    if (!(x >= a1 + 1.0f))
        x = a1 + 1.0f;
    if (z > a2 - 1.0f)
        z = a2 - 1.0f;
    if (!(z >= a3 + 1.0f))
        z = a3 + 1.0f;
    mem.at<float>(L) = x;
    mem.set32(L + 4, 0x47000000u);
    mem.at<float>(L + 8) = z;
    floor_probe(L, L + 0x10, L + 0x20, L + 0x30);
    relocate(1, L + 0x10, 0x10);   // the floor point (its y is read)
    mem.at<float>(L + 4) = molb_floor_y(obj, mem.f32(L + 0x14));
    velocity_for_arc_speed(obj, obj + ob(0x20), L, 0x3C5F0123u, 0x3F000000u, L + 0x34);
    of<int16_t>(obj, 0x5C) = 3;
    mem.at<Obj>(obj).routine = F::kind35_molb_drop;
    of<uint32_t>(obj, 0x320) = 0;
    kind35_molb_drop(obj);
}

// 0x656A0: until landed (+0x320, set by the floor routine)
template <class P> void Game<P>::kind35_molb_drop(uint32_t obj)
{
    if (of<uint32_t>(obj, 0x320) != 0) {
        kind35_molb_landed(obj);
        return;
    }
    kind35_molb_frame(obj);
    if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) == 0)
        of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// frees the object's leap spot (+0x32C) in molb_leaping
template <class P> void Game<P>::molb_leap_spot_free(uint32_t obj)
{
    const int32_t s = of<int32_t>(obj, 0x32C);
    if (s != -1) {
        mem.set32(G::molb_leaping, mem.u32(G::molb_leaping) & ~(1u << (uint32_t(s) & 31)));
        of<int32_t>(obj, 0x32C) = -1;
    }
}

// 0x656D0: stopped, its leap spot freed, no target, state 4, MO 0x130 (menacing)
template <class P> void Game<P>::kind35_molb_landed(uint32_t obj)
{
    obj_velocity_clear(obj);
    molb_leap_spot_free(obj);
    mem.set32(G::molb_lunge_target, 0);
    of<int8_t>(obj, 0x2E8) = -1;
    of<int16_t>(obj, 0x5C) = 4;
    set_motion(obj, 0x130);
    mem.at<Obj>(obj).routine = F::kind35_molb_menace;
    kind35_molb_menace(obj);
}

// 0x65740: at the motion's end it regroups
template <class P> void Game<P>::kind35_molb_menace(uint32_t obj)
{
    if ((of<uint8_t>(obj, 0x3C5) >> 4) & 1) {
        kind35_molb_regroup(obj);
        return;
    }
    kind35_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x65770: a heading inside molb_area (step 5) into +0xC0, the turn toward it
template <class P> void Game<P>::kind35_molb_regroup(uint32_t obj)
{
    area_heading_pick(G::molb_area, obj + ob(0x20), 0x40A00000u, obj + ob(0xC0));
    kind35_molb_turn_start(obj);
    mem.at<Obj>(obj).routine = F::kind35_molb_regroup_turn;   // (the original also computes the turn's delta again, unused)
    kind35_molb_regroup_turn(obj);
}

// 0x657D0: +0x31C turn frames
template <class P> void Game<P>::kind35_molb_regroup_turn(uint32_t obj)
{
    const int32_t n = of<int32_t>(obj, 0x31C) - 1;
    of<int32_t>(obj, 0x31C) = n;
    if (n == 0) {
        kind35_molb_regroup_crawl(obj);
        return;
    }
    kind35_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x65810: speed 1/12 along the heading +0xC0 for 60 frames, state 0, MO 0x12E
template <class P> void Game<P>::kind35_molb_regroup_crawl(uint32_t obj)
{
    of<uint32_t>(obj, 0x74) = 0x3DAAAAABu;
    of<uint32_t>(obj, 0x2EC) = 60;
    of<uint32_t>(obj, 0xBC) = 0;
    const uint32_t h = of<uint32_t>(obj, 0xC0);
    of<uint32_t>(obj, 0x2C) = 0;
    of<int16_t>(obj, 0x5C) = 0;
    of<uint32_t>(obj, 0x30) = h;
    set_motion(obj, 0x12E);
    mem.at<Obj>(obj).routine = F::kind35_molb_regroup_crawl_wait;
    kind35_molb_regroup_crawl_wait(obj);
}

// 0x65860
template <class P> void Game<P>::kind35_molb_regroup_crawl_wait(uint32_t obj)
{
    const int32_t n = of<int32_t>(obj, 0x2EC) - 1;
    of<int32_t>(obj, 0x2EC) = n;
    if (n == 0) {
        kind35_molb_face_camera(obj);
        return;
    }
    kind35_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x658A0: the heading back to the camera (camera yaw + 0x8000), the turn
template <class P> void Game<P>::kind35_molb_face_camera(uint32_t obj)
{
    of<uint32_t>(obj, 0xC0) = mem.u32(G::camera_records + 0x10) + 0x8000u;
    kind35_molb_turn_start(obj);
    mem.at<Obj>(obj).routine = F::kind35_molb_face_turn;
    kind35_molb_face_turn(obj);
}

// 0x658E0: after the turn frames the lunge (inline)
template <class P> void Game<P>::kind35_molb_face_turn(uint32_t obj)
{
    const int32_t n = of<int32_t>(obj, 0x31C) - 1;
    of<int32_t>(obj, 0x31C) = n;
    if (n == 0) {
        molb_lunge_setup(obj);
        return;
    }
    kind35_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x659A0: shot dead: speed 0, MO 0x138, part 8's model 0x446, a hit effect at it, state 1, its leap spot and
// molb_lunge_target freed, falls (acceleration / velocity from 0xC5840), sound 0xA01213; next frame kind35_molb_fall
template <class P> void Game<P>::kind35_molb_killed(uint32_t obj)
{
    of<uint32_t>(obj, 0x74) = 0;
    set_motion(obj, 0x138);
    body_part_set_model(obj, 8, 0x446);
    hit_effect_at(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28));
    of<int16_t>(obj, 0x5C) = 1;
    of<uint32_t>(obj, 0x320) = 0;
    molb_leap_spot_free(obj);
    mem.set32(G::molb_lunge_target, 0);
    const uint32_t t = G::molb_state_routines + 0x40;   // 0xC5840: 6 floats
    of<uint32_t>(obj, 0x84) = mem.u32(t);
    of<uint32_t>(obj, 0x88) = mem.u32(t + 4);
    of<uint32_t>(obj, 0x8C) = mem.u32(t + 8);
    of<uint32_t>(obj, 0x78) = mem.u32(t + 12);
    of<uint32_t>(obj, 0x7C) = mem.u32(t + 16);
    of<uint32_t>(obj, 0x80) = mem.u32(t + 20);
    mem.at<Obj>(obj).routine = F::kind35_molb_fall;
    sound_cue(P::pc ? 0x26 : 0xA01213);
}

// 0x65A70: until it lands (+0x320 == 1): then stopped and 30 frames
template <class P> void Game<P>::kind35_molb_fall(uint32_t obj)
{
    if (of<uint32_t>(obj, 0x320) == 1) {
        for (uint32_t o : {0x80u, 0x7Cu, 0x78u, 0x8Cu})
            of<uint32_t>(obj, o) = 0;
        of<uint32_t>(obj, 0x31C) = 30;
        of<uint32_t>(obj, 0x88) = 0;
        mem.at<Obj>(obj).routine = F::kind35_molb_dead_wait;
        of<uint32_t>(obj, 0x84) = 0;
        kind35_molb_dead_wait(obj);
        return;
    }
    kind35_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x65AD0: after 30 frames: one less alive (and out, if counted), its attack position freed, fading (+0x3C4 bit 2)
template <class P> void Game<P>::kind35_molb_dead_wait(uint32_t obj)
{
    const int32_t n = of<int32_t>(obj, 0x31C) - 1;
    of<int32_t>(obj, 0x31C) = n;
    if (n == 0) {
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        if (of<uint32_t>(obj, 0x328) != 0)
            mem.set32(G::molb_out_count, mem.u32(G::molb_out_count) - 1);
        enemy_attack_slot_free(obj);
        of<uint32_t>(obj, 0x31C) = 30;
        mem.at<Obj>(obj).routine = F::kind35_molb_remove;
        of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) | 4);
        kind35_molb_remove(obj);
        return;
    }
    kind35_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x65B60: after 30 more frames the extras and the body are freed, one enemy less, the task ends
template <class P> void Game<P>::kind35_molb_remove(uint32_t obj)
{
    const int32_t n = of<int32_t>(obj, 0x31C) - 1;
    of<int32_t>(obj, 0x31C) = n;
    if (n == 0) {
        enemy_free_extras(obj);
        body_free_parts(obj);
        mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
        task_exit();
        return;
    }
    kind35_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x65BC0: the common frame: its role in the attack queue (when it has a place), the move and floor routines of its
// state (molb_state_routines), shot (flags bit 0 with bit 8, or return_false) -> kill score 80, hit bits cleared,
// kind35_molb_killed; shadow and draw unless fading (+0x3C4 bit 2) on an odd frame (0x51F420 bit 0); depth list
template <class P> void Game<P>::kind35_molb_frame(uint32_t obj)
{
    if (of<uint8_t>(obj, 0x2E7) != 0xFF)
        attack_queue_role(obj);
    const int32_t st = of<int16_t>(obj, 0x5C);
    call_routine(mem.u32(G::move_routines + mem.u32(G::molb_state_routines + uint32_t(st) * 8) * 4), obj);
    const int32_t st2 = of<int16_t>(obj, 0x5C);
    call_routine(mem.u32(G::molb_floor_routines + mem.u32(G::molb_state_routines + uint32_t(st2) * 8 + 4) * 4), obj);
    if ((of<uint8_t>(obj, 0x58) & 1) && (((of<uint16_t>(obj, 0x58) >> 8) & 1) || return_false() != 0)) {
        kill_score_shooter(obj, 0x50);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xFFFFFEF8u;
        kind35_molb_killed(obj);
    }
    if (((of<uint8_t>(obj, 0x3C4) >> 2) & 1) == 0 || (mem.at<uint8_t>(G::blink_phase) & 1)) {
        shadow_update(obj);
        object_draw(obj);
    }
    depth_list_add(obj);
}

// 0x65C80: the turn toward +0xC0: delta = (+0xC0 - yaw) folded to +-0x8000; +0x31C = |delta| x 60 / 0x8000 + 1
// frames, MO 0x133 (delta >= 0) / 0x134; state 5, stopped
template <class P> void Game<P>::kind35_molb_turn_start(uint32_t obj)
{
    int32_t d = int32_t((of<uint32_t>(obj, 0xC0) - of<uint32_t>(obj, 0x30)) & 0xFFFF);
    if (d > 0x7FFF)
        d -= 0x10000;
    if (d >= 0) {
        of<int32_t>(obj, 0x31C) = (d * 60) / 0x8000 + 1;
        set_motion(obj, 0x133);
    } else {
        of<int32_t>(obj, 0x31C) = (-d * 60) / 0x8000 + 1;
        set_motion(obj, 0x134);
    }
    of<int16_t>(obj, 0x5C) = 5;
    obj_velocity_clear(obj);
}

// 0x65D10 (kind 35): state 7, type 6, body (MO 0x130), type set-up, a wide attack position, a spline buffer, on the
// floor, stopped, counted; waits for the camera to stop
template <class P> void Game<P>::kind35_molb_murrer(uint32_t obj)
{
    LocalScope scope(*this);
    const uint32_t L = local_alloc(0x10);
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    if (pc_path())
        molb_height_offset_set(0);
    of<int16_t>(obj, 0x5C) = 7;
    of<int16_t>(obj, 0xCC) = 6;
    of<uint32_t>(obj, 0x58) = 0;
    of<uint32_t>(obj, 0x368) = rec;
    mem.set32(G::hit_type, 6);
    body_init(obj, 6, 0x130);
    creature_type_setup(obj);
    attack_slot_take_wide(obj);
    if constexpr (P::pc)
        mem.set32(G::pc_wide_slot_kind, 5);
    const uint32_t buf = mem_alloc(0x48);
    of<uint32_t>(obj, 0x37C) = buf;
    mem.set32(L, of<uint32_t>(obj, 0x20));
    mem.set32(L + 4, 0x47000000u);
    mem.set32(L + 8, of<uint32_t>(obj, 0x28));
    const float y = molb_floor_y(obj, ground_height(L));
    of<int8_t>(obj, 0x2E8) = -1;
    of<int16_t>(obj, 0x6C) = 2;
    of<int16_t>(obj, 0x68) = 1;
    for (uint32_t o : {0x80u, 0x7Cu, 0x78u, 0x8Cu, 0x88u, 0x84u})
        of<uint32_t>(obj, o) = 0;
    of<float>(obj, 0x24) = y;
    enemy_counts_add(1);
    mem.at<Obj>(obj).routine = F::kind35_molb_wait_stop;
    kind35_molb_wait_stop(obj);
}

// 0x65E10: when the camera stops (camera_stopped 2): shootable, the record's delay (+0x1A); else the common frame
// (inline)
template <class P> void Game<P>::kind35_molb_wait_stop(uint32_t obj)
{
    if (mem.u32(G::camera_stopped) == 2) {
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 1;
        const int32_t delay = mem.at<int16_t>(of<uint32_t>(obj, 0x368) + 0x1A);
        mem.at<Obj>(obj).routine = F::kind35_molb_delay;
        of<int32_t>(obj, 0x31C) = delay;
        kind35_molb_delay(obj);
        return;
    }
    kind35_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x65F10: after +0x31C frames the crawl; else the common frame (inline)
template <class P> void Game<P>::kind35_molb_delay(uint32_t obj)
{
    const int32_t n = of<int32_t>(obj, 0x31C) - 1;
    of<int32_t>(obj, 0x31C) = n;
    if (n == 0) {
        kind35_molb_crawl_start(obj);
        return;
    }
    kind35_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x66000 (kind 36): shootable at once, type 6, body (MO 0x130), set-up, a wide attack position, on the floor,
// counted, then the lunge (inline)
template <class P> void Game<P>::kind36_molb_murrer_attack(uint32_t obj)
{
    LocalScope scope(*this);
    const uint32_t L = local_alloc(0x10);
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    if (pc_path())
        molb_height_offset_set(0);
    of<uint32_t>(obj, 0x58) = 1;
    of<int16_t>(obj, 0xCC) = 6;
    of<uint32_t>(obj, 0x368) = rec;
    mem.set32(G::hit_type, 6);
    body_init(obj, 6, 0x130);
    creature_type_setup(obj);
    attack_slot_take_wide(obj);
    mem.set32(L, of<uint32_t>(obj, 0x20));
    mem.set32(L + 4, 0x47000000u);
    mem.set32(L + 8, of<uint32_t>(obj, 0x28));
    const float y = molb_floor_y(obj, ground_height(L));
    of<int8_t>(obj, 0x2E8) = -1;
    of<int16_t>(obj, 0x6C) = 2;
    of<int16_t>(obj, 0x68) = 1;
    of<float>(obj, 0x24) = y;
    enemy_counts_add(1);
    molb_lunge_setup(obj);
}

// ---------------------------------------------------------------- kind 39: a Murrer that jumps at the camera

// 0x66150 (kind 39): shootable, body (MO 0x130; no type store), set-up, a wide attack position, stopped, state 0,
// heading = its yaw, counted; record byte 1: 0 = MO 0x131 from frame 15, kind39_jump_wait; 1 = kind39_crawl_floor
template <class P> void Game<P>::kind39_molb_murrer_jump(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    if (pc_path())
        molb_height_offset_set(0);
    of<uint32_t>(obj, 0x58) = 1;
    of<uint32_t>(obj, 0x368) = rec;
    mem.set32(G::hit_type, 6);
    body_init(obj, 6, 0x130);
    creature_type_setup(obj);
    attack_slot_take_wide(obj);
    of<uint32_t>(obj, 0xBC) = 0;
    const uint32_t yaw = of<uint32_t>(obj, 0x30);
    of<uint32_t>(obj, 0x74) = 0;
    of<int16_t>(obj, 0x5C) = 0;
    of<int16_t>(obj, 0x6C) = 0;
    of<int8_t>(obj, 0x2E8) = -1;
    of<int16_t>(obj, 0x68) = 1;
    of<uint32_t>(obj, 0xC0) = yaw;
    enemy_counts_add(1);
    if (mem.at<int8_t>(rec + 1) == 0) {
        set_motion(obj, 0x131);
        of<uint32_t>(obj, 0xF8) = 15;
        of<int16_t>(obj, 0xF4) = 15;
    }
    of<int16_t>(obj, 0x66) = 0;
    const int8_t mode = mem.at<int8_t>(rec + 1);
    if (mode == 0) {
        mem.at<Obj>(obj).routine = F::kind39_jump_wait;
        kind39_jump_wait(obj);
    } else if (mode == 1) {
        mem.at<Obj>(obj).routine = F::kind39_crawl_floor;
        kind39_crawl_floor(obj);
    }
}

// 0x66240: until motion frame 22 (0xC586C), then the jump
template <class P> void Game<P>::kind39_jump_wait(uint32_t obj)
{
    if (of<int16_t>(obj, 0x66) == 0) {
        if (of<int16_t>(obj, 0xF4) < int32_t(mem.at<int8_t>(G::molb_state_routines + (P::pc ? 0x64 : 0x6C)))) {
            kind39_molb_frame(obj);
            of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
            return;
        }
        of<int16_t>(obj, 0x66) = int16_t(of<int16_t>(obj, 0x66) + 1);
    }
    kind39_jump_start(obj);
}

// 0x66290: kept on the floor (floor_probe from its position + the type's height)
template <class P> void Game<P>::kind39_crawl_floor(uint32_t obj)
{
    LocalScope scope(*this);
    const uint32_t L = local_alloc(0x30);   // fp + 0x40 .. + 0x6F
    mem.set32(L + 0x10, of<uint32_t>(obj, 0x20));
    mem.set32(L + 0x14, of<uint32_t>(obj, 0x24));
    mem.set32(L + 0x18, of<uint32_t>(obj, 0x28));
    floor_probe(L + 0x10, obj + ob(0x20), L, L + 0x20);
    const float h = mem.f32(G::creature_floor_height + uint32_t(int32_t(of<int16_t>(obj, 0xCC))) * 4);
    if (pc_path())
        of<float>(obj, 0x24) = (molb_height_offset() + h) + of<float>(obj, 0x24);
    else
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + h;
    kind39_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x662F0
template <class P> void Game<P>::kind39_jump_start(uint32_t obj)
{
    of<int16_t>(obj, 0x66) = 0;
    mem.at<Obj>(obj).routine = F::kind39_jump;
    kind39_jump(obj);
}

// 0x66310: step 0: state 3, a leap spot, an arc of 26 frames, facing the camera, MO 0x12D; 1: until motion frame 30;
// 2: within 10 of its point (+0xA0): projectile_hit_player, stopped, 16 frames (0xC586D); 3: then fading, 30 frames
// (0xC586E); 4: its leap spot and molb_lunge_target freed, the task ends
template <class P> void Game<P>::kind39_jump(uint32_t obj)
{
    LocalScope scope(*this);
    const uint32_t L = local_alloc(0x10);
    bool anim = false;
    int32_t step = of<int16_t>(obj, 0x66);
    if (uint32_t(step) <= 4) {
        if (step == 0) {
            of<int16_t>(obj, 0x5C) = 3;
            molb_leap_target_pick(obj, L);
            velocity_for_arc(obj, obj + ob(0x20), L, 0x3D2740DAu, 0x41D00000u);
            of<uint32_t>(obj, 0x30) = mem.u32(G::camera_records + 0x10) - 0x8000u;
            set_motion(obj, 0x12D);
            of<int16_t>(obj, 0x66) = int16_t(of<int16_t>(obj, 0x66) + 1);
            step = 1;
        }
        if (step == 1) {
            if (!(of<int16_t>(obj, 0xF4) > 30)) {
                anim = true;
                step = -1;
            } else {
                of<int16_t>(obj, 0x66) = int16_t(of<int16_t>(obj, 0x66) + 1);
                step = 2;
            }
        }
        if (step == 2) {
            // PC: compared as integers
            if (pc_path() ? of<int32_t>(obj, 0xA0) > 0x41200000 : of<float>(obj, 0xA0) > 10.0f) {
                step = -1;
            } else {
                projectile_hit_player(obj + ob(0x38), 1, 1);
                obj_velocity_clear(obj);
                const int16_t s = int16_t(of<int16_t>(obj, 0x66) + 1);
                of<int32_t>(obj, 0x2EC) = mem.at<int8_t>(G::molb_state_routines + (P::pc ? 0x65 : 0x6D));
                of<int16_t>(obj, 0x66) = s;
                step = 3;
            }
        }
        if (step == 3) {
            const int32_t n = of<int32_t>(obj, 0x2EC) - 1;
            of<int32_t>(obj, 0x2EC) = n;
            if (n != 0) {
                step = -1;
            } else {
                of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) | 4);
                of<int16_t>(obj, 0x66) = int16_t(of<int16_t>(obj, 0x66) + 1);
                of<int32_t>(obj, 0x2EC) = mem.at<int8_t>(G::molb_state_routines + (P::pc ? 0x66 : 0x6E));
                step = 4;
            }
        }
        if (step == 4) {
            molb_leap_spot_free(obj);
            mem.set32(G::molb_lunge_target, 0);
            task_exit();
            return;
        }
    }
    kind39_molb_frame(obj);
    if (anim)
        of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x664A0: shot: one less alive, fading, 16 frames (+0x31C); then its attack position and body freed, one enemy
// less, the task ends
template <class P> void Game<P>::kind39_molb_dying(uint32_t obj)
{
    const int32_t step = of<int16_t>(obj, 0x66);
    if (step == 0) {
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        of<uint32_t>(obj, 0x31C) = 16;
        of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) | 4);
        of<int16_t>(obj, 0x66) = int16_t(step + 1);
    }
    if (step == 0 || step == 1) {
        const int32_t n = of<int32_t>(obj, 0x31C) - 1;
        of<int32_t>(obj, 0x31C) = n;
        if (n == 0) {
            enemy_attack_slot_free(obj);
            body_free_parts(obj);
            mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
            task_exit();
            return;
        }
    }
    kind39_molb_frame(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x66540: the move routine of its state; shot -> kind39_molb_dying (step 0, hit bits cleared); else shadow and draw
// (unless fading on an odd frame), its camera-space point (+0x38) and its point through stored matrix 5 (+0x98,
// +0xA0 = its distance), the depth list
template <class P> void Game<P>::kind39_molb_frame(uint32_t obj)
{
    const int32_t st = of<int16_t>(obj, 0x5C);
    call_routine(mem.u32(G::move_routines + mem.u32(G::molb_state_routines + uint32_t(st) * 8) * 4), obj);
    if ((of<uint8_t>(obj, 0x58) & 1) && (((of<uint16_t>(obj, 0x58) >> 8) & 1) || return_false() != 0)) {
        const uint32_t f = of<uint32_t>(obj, 0x58);
        mem.at<Obj>(obj).routine = F::kind39_molb_dying;
        of<int16_t>(obj, 0x66) = 0;
        of<uint32_t>(obj, 0x58) = f & 0xFFFFFEF8u;
        kind39_molb_dying(obj);
        return;
    }
    if (((of<uint8_t>(obj, 0x3C4) >> 2) & 1) == 0 || (mem.at<uint8_t>(G::blink_phase) & 1)) {
        shadow_update(obj);
        object_draw(obj);
    }
    if (pc_path()) {
        // PC: the current matrix, then stored matrix 5 copied onto a pushed one (software matrix stack)
        auto xf = [&](uint32_t out) {
            float ox = 0.0f, oy = 0.0f, oz = 0.0f;
            pc_matrix_transform2(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28), ox, oy, oz);
            of<float>(obj, out) = ox;
            of<float>(obj, out + 4) = oy;
            of<float>(obj, out + 8) = oz;
        };
        xf(0x38);
        pc_matrix_push();
        pcm_load_stored5();
        xf(0x98);
        pcm_pop();
    } else {
        Tgp::Out o, none;
        const uint32_t p[3] = {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)};
        copro(0x1A, p, 3, o);
        of<uint32_t>(obj, 0x38) = o.w[0];
        of<uint32_t>(obj, 0x3C) = o.w[1];
        of<uint32_t>(obj, 0x40) = o.w[2];
        const uint32_t m[1] = {5};
        copro(0x74, m, 1, none);
        const uint32_t p2[3] = {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)};
        copro(0x1A, p2, 3, o);
        of<uint32_t>(obj, 0x98) = o.w[0];
        of<uint32_t>(obj, 0x9C) = o.w[1];
        copro(0x06, nullptr, 0, none);
        of<uint32_t>(obj, 0xA0) = o.w[2];
    }
    depth_list_add(obj);
}

// 0x66680 (obj, out): a free one of 8 screen spots (bits of molb_leaping; the two nearest the object's own side,
// 0 / 4 on the right or 3 / 7 on the left, are never taken), searched from rand() & 7; the spot's screen point
// (column (s & 3) x 124 - 186, row (s & 4) x 48 - 96, each moved by a random amount) put 10 in front of the camera
// (stored matrix 1, coprocessor 0x1A at depth 10) into out
template <class P> void Game<P>::molb_leap_target_pick(uint32_t obj, uint32_t out)
{
    Tgp::Out o, none;
    if (pc_path()) {
        pc_matrix_push();
        pcm_load_camera();
    } else {
        const uint32_t one[1] = {1};
        copro(0x74, one, 1, none);
    }
    const uint32_t r = rand() & 7;
    uint32_t a, b;
    if (of<float>(obj, 0x38) > 0.0f) {
        a = 0;
        b = 4;
    } else {
        a = 3;
        b = 7;
    }
    const uint32_t old = mem.u32(G::molb_leaping);
    const uint32_t mask = (1u << a) | (1u << b);
    const uint32_t keep = old & mask;
    uint32_t used = mask | old;
    uint32_t s = 0;
    for (uint32_t i = 0; i <= 7; ++i) {
        s = (r + i) & 7;
        if (((used >> s) & 1) == 0)
            break;
    }
    used |= 1u << s;
    mem.set32(G::molb_leaping, used);
    of<uint32_t>(obj, 0x32C) = s;
    mem.set32(G::molb_leaping, (used & ~mask) | keep);
    if (pc_path()) {
        // PC: the same sums in x87 order, then x 10 x (1 / 496) where the arcade divides by 496
        const float inv = ffrom(0x3B042108u);   // the PC constant 0x5A39ED (0.0020161)
        float x = (float(int32_t(rand() & 0xFF)) + -127.5f) * ffrom(0x3EF8F8F9u);
        x = x + (float(int32_t(s & 3)) * 124.0f + -186.0f);
        x = x * 10.0f * inv;
        float y = (float(int32_t(rand() & 0xFF)) + -127.5f) * ffrom(0x3F40C0C1u);
        y = y + (float(int32_t(s & 4)) * 48.0f + -96.0f);
        y = y * 10.0f * inv;
        float ox = 0.0f, oy = 0.0f, oz = 0.0f;
        pc_matrix_transform2(x, y, 10.0f, ox, oy, oz);
        mem.at<float>(out) = ox;
        mem.at<float>(out + 4) = oy;
        mem.at<float>(out + 8) = oz;
        pcm_pop();
        return;
    }
    float x = float(int32_t(rand() & 0xFF)) - 127.5f;
    float cx = float(int32_t(s & 3)) * 124.0f;
    x = x * ffrom(0x3EF8F8F9u);
    cx = cx - 186.0f;
    x = cx + x;
    x = x * 10.0f;
    x = x / 496.0f;
    const float cy0 = float(int32_t(s & 4));
    float y = float(int32_t(rand() & 0xFF)) - 127.5f;
    float cy = cy0 * 48.0f;
    y = y * ffrom(0x3F40C0C1u);
    cy = cy - 96.0f;
    y = cy + y;
    y = y * 10.0f;
    const uint32_t v[3] = {fbits(x), fbits(y / 496.0f), fbits(10.0f)};
    copro(0x1A, v, 3, o);
    mem.set32(out, o.w[0]);
    mem.set32(out + 4, o.w[1]);
    copro(0x06, nullptr, 0, none);
    mem.set32(out + 8, o.w[2]);
}

} // namespace hotd::port
