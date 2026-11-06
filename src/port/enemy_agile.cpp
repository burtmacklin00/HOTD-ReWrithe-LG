// Enemies: kind 70, the agile creatures (the Saruzou monkeys, types 1 / 9, and kage 38; Rubin 18; Drake 37 with his
// knives; Moody 59 / 92 / 93 on walls and ceilings; kageta 68 / 87): one engine, arcade cluster 0x8BEC0..0x92D30, PC
// file 0x4FDE44..0x5036D4, read from the arcade and PC disassembly. The spawn runs the set-up of the record's action
// code (kind70_setups); kind70_frame then runs every frame: the hits, the hit reactions (ground / airborne), the state
// routine (kind70_states by +0x5C, its step +0x60), the velocity (+0x78..+0x80, y acceleration +0x88), the draw. Life
// +0x70, the hit class +0x334, the hit-reaction step +0x338; flags +0x58 (bit 0 alive, 8 hit this frame, 25 not drawn,
// 26 frame counters held, 27 frozen: no state routine, 28 hit reaction) and +0x324 (bit 3 no hits, 4 no reaction, 5
// airborne reaction). The creature slot +0x31C indexes kind70_slot_state.
#include "port/game.h"

#include <cmath>

namespace hotd::port {

// a call through kind70_setups / kind70_states: the ported routines directly, the rest as boundaries
template <class P> void Game<P>::kind70_call(uint32_t routine, uint32_t obj)
{
    switch (routine) {
    // the states in kind70_states order (12-16, 18-22, 25: set-ups again), then set-up 12
    case F::kind70_st00_none: kind70_st00_none(obj); return;
    case F::kind70_st01_death: kind70_st01_death(obj); return;
    case F::kind70_st02_approach: kind70_st02_approach(obj); return;
    case F::kind70_st03_think: kind70_st03_think(obj); return;
    case F::kind70_st04_wall: kind70_st04_wall(obj); return;
    case F::kind70_st05_bite: kind70_st05_bite(obj); return;
    case F::kind70_st06_wall_x: kind70_st06_wall_x(obj); return;
    case F::kind70_st07_wall_z: kind70_st07_wall_z(obj); return;
    case F::kind70_st08_leap: kind70_st08_leap(obj); return;
    case F::kind70_st09_turn: kind70_st09_turn(obj); return;
    case F::kind70_st10_stance: kind70_st10_stance(obj); return;
    case F::kind70_st11_last: kind70_st11_last(obj); return;
    case F::kind70_spawn0_jump_in: kind70_spawn0_jump_in(obj); return;
    case F::kind70_spawn2_eating: kind70_spawn2_eating(obj); return;
    case F::kind70_spawn1_jump_across: kind70_spawn1_jump_across(obj); return;
    case F::kind70_spawn5_waiting: kind70_spawn5_waiting(obj); return;
    case F::kind70_spawn7: kind70_spawn7(obj); return;
    case F::kind70_st17_bite_b: kind70_st17_bite_b(obj); return;
    case F::kind70_spawn8_wall_attack: kind70_spawn8_wall_attack(obj); return;
    case F::kind70_spawn3: kind70_spawn3(obj); return;
    case F::kind70_spawn9_fade_in: kind70_spawn9_fade_in(obj); return;
    case F::kind70_spawn10_stance_bite: kind70_spawn10_stance_bite(obj); return;
    case F::kind70_spawn6: kind70_spawn6(obj); return;
    case F::kind70_st23_knife_throw: kind70_st23_knife_throw(obj); return;
    case F::kind70_st24_roll: kind70_st24_roll(obj); return;
    case F::kind70_spawn11_hanging: kind70_spawn11_hanging(obj); return;
    case F::kind70_st26_none: kind70_st26_none(obj); return;
    case F::kind70_st27_wait: kind70_st27_wait(obj); return;
    case F::kind70_st28_moody_wall: kind70_st28_moody_wall(obj); return;
    case F::kind70_st29_moody_ceiling: kind70_st29_moody_ceiling(obj); return;
    case F::kind70_spawn12: kind70_spawn12(obj); return;
    default: call_routine(routine, obj); return;
    }
}

// 0x2FF70 / PC 0x46B468: velocity +0x78..+0x80 and acceleration +0x84..+0x8C cleared
template <class P> void Game<P>::obj_stop(uint32_t obj)
{
    for (uint32_t a = 0x78; a < 0x90; a += 4)
        of<uint32_t>(obj, a) = 0;
}

// 0x8BEC0 / PC 0x4FDE44 (kind 70): the record (+0x35C, kept at +0x368): +0xC / +0xE (halfwords) to +0x2F4 / +0x2F8,
// the type (+0x10), the action code (+0x11), the creature slot (+0x13: +0x31C); step 0; the action's set-up
template <class P> void Game<P>::kind70_saru_saruzou(uint32_t obj)
{
    const uint32_t r = of<uint32_t>(obj, 0x35C);
    of<uint32_t>(obj, 0x368) = r;
    of<int32_t>(obj, 0x2F4) = mem.at<int16_t>(r + 0xC);
    of<int32_t>(obj, 0x2F8) = mem.at<int16_t>(r + 0xE);
    of<int16_t>(obj, 0xCC) = int16_t(mem.at<uint8_t>(r + 0x10));
    of<int16_t>(obj, 0x64) = int16_t(mem.at<uint8_t>(r + 0x11));
    of<int16_t>(obj, 0x60) = 0;
    of<uint32_t>(obj, 0x31C) = mem.at<uint8_t>(r + 0x13);
    kind70_call(mem.u32(G::kind70_setups + uint32_t(int32_t(of<int16_t>(obj, 0x64))) * 4), obj);
}

// 0x8BF10 / PC 0x4FDEA4: body (motion 0x194); the AI flags (+0x2E5) from the record's +0x12; the slot's state -1;
// counted as an enemy, an enemy slot, camera side; not targeted (+0x2E8 -1); routine kind70_frame; types 0x25 / 0x44 /
// 0x57 clear kind70_attack_turn
template <class P> void Game<P>::kind70_spawn_init(uint32_t obj)
{
    const int32_t type = of<int16_t>(obj, 0xCC);
    const uint32_t r = of<uint32_t>(obj, 0x35C);
    mem.set32(G::hit_type, uint32_t(type));
    body_init(obj, type, 0x194);
    creature_type_setup(obj);
    of<uint8_t>(obj, 0x2E5) = mem.at<uint8_t>(r + 0x12);
    mem.at<int32_t>(G::kind70_slot_state + of<uint32_t>(obj, 0x31C) * 4) = -1;
    mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
    enemy_slot_take(obj);
    camera_side_set(obj);
    of<int8_t>(obj, 0x2E8) = -1;
    of<uint32_t>(obj, 0x0) = F::kind70_frame;
    const uint16_t t = of<uint16_t>(obj, 0xCC);
    if (t == 0x25 || t == 0x44 || t == 0x57)
        mem.set32(G::kind70_attack_turn, 0);
}

// 0x8BFC0 / PC 0x4FDF4C (set-up 12): kind70_spawn_init (inline on the arcade), then state 23 (Drake's knife throw)
template <class P> void Game<P>::kind70_spawn12(uint32_t obj)
{
    kind70_spawn_init(obj);
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 23;
}

// 0x8F090 / PC 0x500800 (set-up 0, the jump-in; also state 12), by step: 0: kind70_spawn_init (inline on the
// arcade), +0x320 -1, the wait line z 32 (+0x3A8), +0x3C5 bit 2, not shootable (+0x58 bit 0), MO 0x1C0, state 12;
// 1: once z <= the line: a 39-frame jump (slot 0: to (9.5, 39.5, 8), else (-8.5, 39.5, -8)), MO 0x19E; 2: landed:
// stopped, event flag +3 set, MO 0x19B; 3: carried along by the ride (its position change, ride_pos - ride_prev);
// event flag +4: MO 0x1B1, gravity, +0x3C5 bit 2; 4: below the floor + 10: hopping off (vy 0.5, vz -0.15), MO
// 0x19E; 5: below the floor: stopped, +0x320 0, shootable, MO 0x1BD; 6 (from frame 13): a 36-frame jump 40 forward
// onto the floor; 7: landed; 8 (after frame 56): home = here with z - 15, state 2. 9 (state 14 / the leaving
// one): not shootable, its attack slot freed, no longer counted, the exit point (+-52, 0, -39), MO 0x1C0; slot != 0:
// state 14 step 10, else 10: walking to the exit (facing it); within 1.5: three jumps away ((52, 5, -16), (52, 20,
// 0), (52, 30, 30)), the body freed, the task ends
template <class P> void Game<P>::kind70_spawn0_jump_in(uint32_t obj)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    auto arc = [&](uint32_t tx, uint32_t ty, uint32_t tz, int frames) {
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28), tx, ty, tz, frames);
    };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        kind70_spawn_init(obj);
        of<int32_t>(obj, 0x320) = -1;
        of<float>(obj, 0x3A8) = 32.0f;
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) | 4);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
        of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) & ~1u);
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~8u);
        set_motion(obj, 0x1C0);
        of<int16_t>(obj, 0x5C) = 12;
        step();
        [[fallthrough]];
    case 1:
        if (of<float>(obj, 0x28) > of<float>(obj, 0x3A8))
            return;
        if (of<uint32_t>(obj, 0x31C) == 0)
            arc(0x41180000u, 0x421E0000u, 0x41000000u, 39);
        else
            arc(0xC1080000u, 0x421E0000u, 0xC1000000u, 39);
        set_motion(obj, 0x19E);
        step();
        [[fallthrough]];
    case 2:
        if (jump_arc_step_b(obj) != 1)
            return;
        obj_stop(obj);
        mem.at<uint8_t>(G::event_flags + 3) = 1;
        set_motion_blend(obj, 0x19B, 0, 4);
        step();
        [[fallthrough]];
    case 3:
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + (mem.f32(G::ride_pos_x) - mem.f32(G::ride_prev_x));
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + (mem.f32(G::ride_pos_y) - mem.f32(G::ride_prev_y));
        of<float>(obj, 0x28) = of<float>(obj, 0x28) + (mem.f32(G::ride_pos_z) - mem.f32(G::ride_prev_z));
        if (mem.at<uint8_t>(G::event_flags + 4) == 0)
            return;
        set_motion_blend(obj, 0x1B1, 0, 4);
        of<uint32_t>(obj, 0x88) = 0xBCDF0123u;
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) | 4);
        step();
        [[fallthrough]];
    case 4:
        if (!(of<float>(obj, 0x24) < mem.f32(G::floor_y) + 10.0f))
            return;
        obj_stop(obj);
        of<float>(obj, 0x7C) = 0.5f;
        of<uint32_t>(obj, 0x80) = 0xBE19999Au;
        of<uint32_t>(obj, 0x88) = 0xBCDF0123u;
        set_motion(obj, 0x19E);
        step();
        [[fallthrough]];
    case 5:
        if (!(of<float>(obj, 0x24) < mem.f32(G::floor_y)))
            return;
        obj_stop(obj);
        of<uint32_t>(obj, 0x320) = 0;
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~4u);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 1u;
        set_motion(obj, 0x1BD);
        step();
        [[fallthrough]];
    case 6:
        if (of<int16_t>(obj, 0xF4) <= 12)
            return;
        arc(of<uint32_t>(obj, 0x20), mem.u32(G::floor_y), fbits(of<float>(obj, 0x28) + 40.0f), 36);
        step();
        [[fallthrough]];
    case 7:
        if (jump_arc_step_b(obj) != 1)
            return;
        step();
        [[fallthrough]];
    case 8:
        if (of<int16_t>(obj, 0xF4) <= 56)
            return;
        of<int16_t>(obj, 0x60) = 0;
        of<uint32_t>(obj, 0x3AC) = of<uint32_t>(obj, 0x20);
        of<int16_t>(obj, 0x5C) = 2;
        of<uint32_t>(obj, 0x3B0) = of<uint32_t>(obj, 0x24);
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~4u);
        of<float>(obj, 0x3B4) = float(double(of<float>(obj, 0x28)) - 15.0);
        return;
    case 9:
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
        mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 20) = 0;
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
        of<float>(obj, 0x3A8) = -39.0f;
        of<uint32_t>(obj, 0x3A4) = 0;
        of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) & ~1u);
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~8u);
        set_motion_blend(obj, 0x1C0, 0, 4);
        if (of<uint32_t>(obj, 0x31C) != 0) {
            of<float>(obj, 0x3A0) = -52.0f;
            of<int16_t>(obj, 0x5C) = 14;
            of<int16_t>(obj, 0x60) = 10;
            return;
        }
        of<float>(obj, 0x3A0) = 52.0f;
        step();
        kind70_leave(obj, 0x42500000u);
        return;
    case 10: case 11: case 12: case 13:
        kind70_leave(obj, 0x42500000u);
        return;
    default:
        return;
    }
}

// steps 10-13 of set-ups 0 / 1 (leaving): 10: walking to the exit (+0x3A0 / +0x3A8, facing it); within 1.5: three
// 39-frame jumps away (x `x`: (x, 5, -16), (x, 20, 0), (x, 30, 30)), MO 0x19E; then the body freed, the task ends
template <class P> void Game<P>::kind70_leave(uint32_t obj, uint32_t x)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    auto arc = [&](uint32_t ty, uint32_t tz) {
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28), x, ty, tz, 39);
    };
    switch (of<uint16_t>(obj, 0x60)) {
    case 10: {
        kind70_face(obj, of<float>(obj, 0x3A0) - of<float>(obj, 0x20), 0.0f, of<float>(obj, 0x3A8) - of<float>(obj, 0x28));
        float d;
        if (pc_path())
            d = pc_floor_distance10(of<float>(obj, 0x20), of<float>(obj, 0x28), of<float>(obj, 0x3A0), of<float>(obj, 0x3A8));
        else
            d = floor_distance(of<float>(obj, 0x20), of<float>(obj, 0x28), of<float>(obj, 0x3A0), of<float>(obj, 0x3A8));
        if (d > 1.5f)
            return;
        of<uint32_t>(obj, 0x30) = 0;
        set_motion(obj, 0x19E);
        arc(0x40A00000u, 0xC1800000u);
        step();
    }
        [[fallthrough]];
    case 11:
        if (jump_arc_step_b(obj) != 1)
            return;
        arc(0x41A00000u, 0);
        step();
        [[fallthrough]];
    case 12:
        if (jump_arc_step_b(obj) != 1)
            return;
        arc(0x41F00000u, 0x41F00000u);
        step();
        [[fallthrough]];
    case 13:
        if (jump_arc_step_b(obj) != 1)
            return;
        body_free_parts(obj);
        task_exit();
        return;
    default:
        return;
    }
}

// 0x8F780 / PC 0x500CD4 (set-up 1, state 14): only steps 10-13, leaving to the other side (x -52); set-up 0's step 9
// sends slot 1 here
template <class P> void Game<P>::kind70_spawn1_jump_across(uint32_t obj) { kind70_leave(obj, 0xC2500000u); }

// 0x8FD90 / PC 0x5010D0 (set-up 7): the spawn set-up (inline on the arcade), +0x3C5 bit 2, +0x3C6 bit 0 cleared, home
// here, facing the camera (y 0), state 4 (the wall)
template <class P> void Game<P>::kind70_spawn7(uint32_t obj)
{
    if (of<int16_t>(obj, 0x60) != 0)
        return;
    kind70_spawn_init(obj);
    of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) | 4);
    of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) & ~1u);
    of<uint32_t>(obj, 0x3AC) = of<uint32_t>(obj, 0x20);
    of<uint32_t>(obj, 0x3B4) = of<uint32_t>(obj, 0x28);
    of<uint32_t>(obj, 0x3B0) = of<uint32_t>(obj, 0x24);
    kind70_face(obj, mem.f32(G::camera_pos) - of<float>(obj, 0x20), 0.0f, mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28));
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 4;
}

// 0x90D30 / PC 0x501BA0 (state 23, the knife throw), by step: 0: camera side, a 12-frame hop 6 up, MO 0x83
// (dr_knif_tobi2); 1: at its top vy 0.075, the throw by the hand (+0x320: 0 MO 0x85, else 0x81); 2: the knife leaves
// the hand (MO 0x85 frame 18, 0x81 frame 22: kind70_attack_turn + 1, kind70_knife_spawn); after it, a 24-frame drop to
// the floor; 3: at the motion's end the landing (0x85 -> 0x84 dr_l_chaku, 0x81 -> 0x80); landed: stopped, the AI
template <class P> void Game<P>::kind70_st23_knife_throw(uint32_t obj)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        camera_side_set(obj);
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28), of<uint32_t>(obj, 0x20),
                     fbits(of<float>(obj, 0x24) + 6.0f), of<uint32_t>(obj, 0x28), 12);
        set_motion_blend(obj, 0x83, 0, 4);
        step();
        [[fallthrough]];
    case 1:
        if (jump_arc_velocity_b(obj) != 1)
            return;
        of<uint32_t>(obj, 0x7C) = 0x3D99999Au;
        set_motion_blend(obj, of<uint32_t>(obj, 0x320) == 0 ? 0x85 : 0x81, 0, 4);
        step();
        [[fallthrough]];
    case 2:
        if (of<uint16_t>(obj, 0xD0) == 0x85 && of<uint16_t>(obj, 0xF4) == 18) {
            mem.at<int32_t>(G::kind70_attack_turn) = mem.at<int32_t>(G::kind70_attack_turn) + 1;
            kind70_knife_spawn(obj);
        }
        if (of<uint16_t>(obj, 0xD0) == 0x81 && of<uint16_t>(obj, 0xF4) == 22) {
            mem.at<int32_t>(G::kind70_attack_turn) = mem.at<int32_t>(G::kind70_attack_turn) + 1;
            kind70_knife_spawn(obj);
        }
        if (of<uint16_t>(obj, 0xD0) == 0x85 && of<int16_t>(obj, 0xF4) <= 17)
            return;
        if (of<uint16_t>(obj, 0xD0) == 0x81 && of<int16_t>(obj, 0xF4) <= 21)
            return;
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28), of<uint32_t>(obj, 0x20),
                     mem.u32(G::floor_y), of<uint32_t>(obj, 0x28), 24);
        step();
        [[fallthrough]];
    case 3:
        if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) && of<uint16_t>(obj, 0xD0) == 0x85)
            set_motion_blend(obj, 0x84, 0, 4);
        if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) && of<uint16_t>(obj, 0xD0) == 0x81)
            set_motion_blend(obj, 0x80, 0, 4);
        if (jump_arc_velocity_b(obj) != 1)
            return;
        obj_stop(obj);
        kind70_after_hit_state(obj);
        return;
    default:
        return;
    }
}

// 0x90F20 / PC 0x501D80 (state 24, the roll): step 0: camera side, +0x3C6 bit 0 / +0x3C5 bit 3 cleared, MO 0x7A
// (dr_goro); nearer than 25 to the camera: state 4
template <class P> void Game<P>::kind70_st24_roll(uint32_t obj)
{
    if (of<int16_t>(obj, 0x60) == 0) {
        camera_side_set(obj);
        of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) & ~1u);
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~8u);
        set_motion_blend(obj, 0x7A, 0, 4);
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    }
    if (!(kind70_camera_distance(obj) >= 25.0f)) {
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 4;
    }
}

// 0x91F00 / PC 0x502A18 (state 28, Moody on the wall), by step (the wall by the slot's state: 7 / 6): 0: MO 0x145 /
// 0x146 (moodkabeful), stopped; 1: MO 0x147 / 0x148; 2: the throw at frame 29 (moody_throw); 3: at the motion's end
// back to 0x145 / 0x146; once the thrown object (+0x360) is gone: the hand model back, MO 0x149 / 0x14A; 4: at the
// motion's end a 49-frame jump down to +0x394 / +0x39C, MO 0x19A; 5: landed on the floor; 6: at the motion's end
// facing the camera (y 0), the cry 0xA01225, state 10
template <class P> void Game<P>::kind70_st28_moody_wall(uint32_t obj)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    auto slot = [&] { return mem.at<int32_t>(G::kind70_slot_state + of<uint32_t>(obj, 0x31C) * 4); };
    auto ended = [&] { return ((of<uint8_t>(obj, 0x3C5) >> 4) & 1) != 0; };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        if (slot() == 7)
            set_motion_blend(obj, 0x145, 0, 4);
        if (slot() == 6)
            set_motion_blend(obj, 0x146, 0, 4);
        obj_stop(obj);
        step();
        [[fallthrough]];
    case 1:
        if (slot() == 7)
            set_motion_blend(obj, 0x147, 0, 4);
        if (slot() == 6)
            set_motion_blend(obj, 0x148, 0, 4);
        step();
        [[fallthrough]];
    case 2:
        if (of<uint16_t>(obj, 0xF4) != 29)
            return;
        moody_throw(obj);
        step();
        [[fallthrough]];
    case 3:
        if (ended()) {
            if (slot() == 7 && of<uint16_t>(obj, 0xD0) == 0x147)
                set_motion_blend(obj, 0x145, 0, 4);
            if (slot() == 6 && of<uint16_t>(obj, 0xD0) == 0x148)
                set_motion_blend(obj, 0x146, 0, 4);
        }
        if (of<uint8_t>(of<uint32_t>(obj, 0x360), 0x58) & 1)
            return;
        agile_hand_model_restore(obj);
        if (slot() == 7)
            set_motion_blend(obj, 0x149, 0, 4);
        if (slot() == 6)
            set_motion_blend(obj, 0x14A, 0, 4);
        step();
        [[fallthrough]];
    case 4:
        if (!ended())
            return;
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                     of<uint32_t>(obj, 0x394), mem.u32(G::floor_y), of<uint32_t>(obj, 0x39C), 49);
        set_motion_blend(obj, 0x19A, 0, 4);
        step();
        [[fallthrough]];
    case 5:
        if (jump_arc_velocity(obj) != 1)
            return;
        step();
        of<uint32_t>(obj, 0x24) = mem.u32(G::floor_y);
        [[fallthrough]];
    case 6:
        if (!ended())
            return;
        kind70_face(obj, mem.f32(G::camera_pos) - of<float>(obj, 0x20), 0.0f, mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28));
        sound_cue(P::pc ? 0x32u : 0xA01225u);
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 10;
        of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) & ~1u);
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~8u);
        return;
    default:
        return;
    }
}

// 0x92210 / PC 0x502CA4 (state 29, Moody on the ceiling), by step: 0: camera side, a 29-frame hop 6 up, MO 0x143
// (moodjimeko); 1: at its top vy 0.01875, the throw (moody_throw); 2: MO 0x142; 3: once the thrown object (+0x360)
// is gone: the hand model back, MO 0x144; 4: at the motion's end a 24-frame drop to the floor, MO 0x80; 5: landed:
// stopped, the AI
template <class P> void Game<P>::kind70_st29_moody_ceiling(uint32_t obj)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        camera_side_set(obj);
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28), of<uint32_t>(obj, 0x20),
                     fbits(of<float>(obj, 0x24) + 6.0f), of<uint32_t>(obj, 0x28), 29);
        set_motion_blend(obj, 0x143, 0, 4);
        step();
        [[fallthrough]];
    case 1:
        if (jump_arc_velocity_b(obj) != 1)
            return;
        of<uint32_t>(obj, 0x7C) = 0x3C99999Au;
        moody_throw(obj);
        step();
        [[fallthrough]];
    case 2:
        set_motion_blend(obj, 0x142, 0, 4);
        step();
        [[fallthrough]];
    case 3:
        if (of<uint8_t>(of<uint32_t>(obj, 0x360), 0x58) & 1)
            return;
        agile_hand_model_restore(obj);
        set_motion_blend(obj, 0x144, 0, 4);
        step();
        [[fallthrough]];
    case 4:
        if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) == 0)
            return;
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28), of<uint32_t>(obj, 0x20),
                     mem.u32(G::floor_y), of<uint32_t>(obj, 0x28), 24);
        set_motion_blend(obj, 0x80, 0, 4);
        step();
        [[fallthrough]];
    case 5:
        if (jump_arc_velocity_b(obj) != 1)
            return;
        obj_stop(obj);
        kind70_after_hit_state(obj);
        return;
    default:
        return;
    }
}

// Moody's thrown object model by type (0x3B: 0x1650, 0x5C: 0x18CD, 0x5D: 0x1A8E; else 0)
template <class P> uint32_t Game<P>::moody_held_model(uint32_t obj)
{
    switch (of<int16_t>(obj, 0xCC)) {
    case 0x3B: return 0x1650;
    case 0x5C: return 0x18CD;
    case 0x5D: return 0x1A8E;
    default: return 0;
    }
}

// 0x92A00 / PC 0x5033F4: with the hand (node 5) empty, Moody gets its object back (role 6, the type's model) with
// the cry 0xA01250
template <class P> void Game<P>::agile_hand_model_restore(uint32_t obj)
{
    if (body_node_model(obj, 5) != 0)
        return;
    const uint32_t m = moody_held_model(obj);
    if (m == 0)
        return;
    body_role_model_set(obj, 6, m);
    sound_cue(P::pc ? 0x4Fu : 0xA01250u);
}

// 0x92A90 / PC 0x503454 (Moody's throw): the projectile task (moody_projectile, 0x3C8 bytes, cleared); with the
// type's object in the hand (node 5) it starts at the hand point (+0x268, through the camera matrix: A coprocessor
// 0x74 slot 1 / 0x1A / 0x06, PC the stored matrix 1 and 0x4FBF30) and leaves the hand (role 6 model 0); its model,
// angles (0x8000, -0x4000), alive, owner, scale 1.0 / 1.5; the thrower keeps it in +0x360
template <class P> void Game<P>::moody_throw(uint32_t obj)
{
    const uint32_t t = task_create(F::moody_projectile, P::pc ? 0x3D0 : 0x3C8);
    task_clear_work(t);
    const uint32_t m = moody_held_model(obj);
    if (body_node_model(obj, 5) == m) {
        if (pc_path()) {   // the PC, and PC Mode on the arcade logic (docs/pcmode.md row 10)
            pc_matrix_push();
            pcm_load_camera();
            float ox, oy, oz;
            pc_matrix_transform(of<float>(obj, 0x268), of<float>(obj, 0x26C), of<float>(obj, 0x270), ox, oy, oz);
            of<float>(t, 0x20) = ox;
            of<float>(t, 0x24) = oy;
            of<float>(t, 0x28) = oz;
            body_role_model_set(obj, 6, 0);
            pcm_pop();
        } else {
            Tgp::Out o, none;
            const uint32_t one[1] = {1};
            copro(0x74, one, 1, none);
            const uint32_t v[3] = {of<uint32_t>(obj, 0x268), of<uint32_t>(obj, 0x26C), of<uint32_t>(obj, 0x270)};
            copro(0x1A, v, 3, o);
            of<uint32_t>(t, 0x20) = o.w[0];
            of<uint32_t>(t, 0x24) = o.w[1];
            of<uint32_t>(t, 0x28) = o.w[2];
            body_role_model_set(obj, 6, 0);
            copro(0x06, nullptr, 0, none);
        }
    }
    of<uint32_t>(t, 0x54) = m;
    of<uint32_t>(obj, 0x360) = t;
    of<uint32_t>(t, 0x30) = 0x8000;
    of<uint32_t>(t, 0x2C) = 0xFFFFC000u;
    of<uint32_t>(t, 0x58) = 1;
    of<uint16_t>(t, 0x2E0) = 0xFF;
    of<uint32_t>(t, 0x34) = 0;
    of<int16_t>(t, 0x60) = 0;
    of<float>(t, 0x94) = 1.0f;
    if constexpr (P::pc)
        of<float>(t, 0x90) = of<float>(t, 0x94) * 1.5f;
    else
        of<float>(t, 0x90) = 1.5f;
    of<uint32_t>(t, 0x35C) = obj;
    of<uint32_t>(t, 0x31C) = 0;
    of<int16_t>(t, 0x70) = 1;
}

// 0x915C0 / PC 0x502378 (Drake's / kageta's knife leaves the hand): the knife task (kind70_knife_fly, 0x3C8 bytes,
// cleared); the camera matrix (A: coprocessor 0x74 slot 1 ... 0x06; PC: the stored matrix 1 pushed / popped); the
// knife in the hand of node 8 (+0x238 hand point, hand 0) or node 5 (+0x268, hand 1) gives its start point (the
// coprocessor's 0x1A / PC 0x4FBF30); Drake's hand model becomes the empty one (role 7: 0x181, role 6: 0x183); the
// knife model by type (0x175, 0x1857, 0x1A00), angles (0x8000, 0, 0), alive, owner, hand (+0x320, -1 none), scale
// 1.2 / 1.8, the thrower's type
template <class P> void Game<P>::kind70_knife_spawn(uint32_t obj)
{
    const uint32_t t = task_create(F::kind70_knife_fly, P::pc ? 0x3D0 : 0x3C8);
    task_clear_work(t);
    Tgp::Out none;
    const bool pc = pc_path();   // the PC, and PC Mode on the arcade logic (docs/pcmode.md row 10)
    if (pc) {
        pc_matrix_push();
        pcm_load_camera();
    } else {
        const uint32_t one[1] = {1};
        copro(0x74, one, 1, none);
    }
    // the hand point through the camera matrix to the knife's position
    auto start = [&](uint32_t hand) {
        if (pc) {
            float ox, oy, oz;
            pc_matrix_transform(of<float>(obj, hand), of<float>(obj, hand + 4), of<float>(obj, hand + 8), ox, oy, oz);
            of<float>(t, 0x20) = ox;
            of<float>(t, 0x24) = oy;
            of<float>(t, 0x28) = oz;
        } else {
            Tgp::Out o;
            const uint32_t v[3] = {of<uint32_t>(obj, hand), of<uint32_t>(obj, hand + 4), of<uint32_t>(obj, hand + 8)};
            copro(0x1A, v, 3, o);
            of<uint32_t>(t, 0x20) = o.w[0];
            of<uint32_t>(t, 0x24) = o.w[1];
            of<uint32_t>(t, 0x28) = o.w[2];
        }
    };
    int32_t hand = -1;
    uint32_t m8 = 0, m5 = 0, knife = 0;
    switch (of<int16_t>(obj, 0xCC)) {
    case 0x25: m8 = 0x180; m5 = 0x182; knife = 0x175; break;
    case 0x44: m8 = 0x1855; m5 = 0x1856; knife = 0x1857; break;
    case 0x57: m8 = 0x19FE; m5 = 0x19FF; knife = 0x1A00; break;
    default: break;
    }
    if (m8 != 0) {
        if (body_node_model(obj, 8) == m8) {
            start(0x238);
            if (of<int16_t>(obj, 0xCC) == 0x25)
                body_role_model_set(obj, 7, 0x181);
            hand = 0;
        } else if (body_node_model(obj, 5) == m5) {
            start(0x268);
            if (of<int16_t>(obj, 0xCC) == 0x25)
                body_role_model_set(obj, 6, 0x183);
            hand = 1;
        }
    }
    if (pc)
        pcm_pop();
    else
        copro(0x06, nullptr, 0, none);
    if (knife != 0)
        of<uint32_t>(t, 0x54) = knife;
    of<uint32_t>(t, 0x30) = 0x8000;
    of<uint32_t>(t, 0x58) = 1;
    of<uint32_t>(t, 0x2C) = 0;
    of<uint32_t>(t, 0x34) = 0;
    of<int16_t>(t, 0x60) = 0;
    of<uint32_t>(t, 0x35C) = obj;
    of<uint32_t>(t, 0x31C) = 0;
    of<int32_t>(t, 0x320) = hand;
    of<uint16_t>(t, 0x2E0) = 0xFF;
    of<float>(t, 0x94) = ffrom(0x3F99999Au);
    if constexpr (P::pc)
        of<float>(t, 0x90) = of<float>(t, 0x94) * 1.5f;
    else
        of<uint32_t>(t, 0x90) = 0x3FE66667u;
    of<int16_t>(t, 0xCC) = of<int16_t>(obj, 0xCC);
}

// 0x90FF0 / PC 0x501E38 (the thrown knife's task), by step: 0: a target player (pick_target_player_any), the target
// point (kind70_knife_fly_sub), the throw sound 0xA01255; 1: spinning (+0x30 -/+ 0x1800 by the hand), 0.9 a frame
// along the unit vector to the target (A: coprocessor 0x0C; PC: 1 / the table square root); within 0.9 in x and z:
// 2: not shootable, facing the camera (kageta's knife 0x1857 turned -0x4000), the hit on players 1 / 2 (player_hit
// side 3), 45 frames; 3: held at the target point, blinking (+0x58 bit 25 on odd counts), then removed. Shot or
// thrower dead: 4: the cry 0xA0120A, +0x31C 1, gravity, not shootable, vy 0.3; 5: falling until below the floor, then
// removed (kind70_attack_turn - 1). Drawn unless bit 25: the shadow on the ground, the model at its position / angles
// / scale +0x94, faded (+0x2E0); PC: its matrix helpers, the model only with the option pc_draw_bats
template <class P> void Game<P>::kind70_knife_fly(uint32_t obj)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    auto removed = [&] {
        mem.at<int32_t>(G::kind70_attack_turn) = mem.at<int32_t>(G::kind70_attack_turn) - 1;
        task_exit();
    };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        pick_target_player_any(obj);
        kind70_knife_fly_sub(obj);
        sound_cue(P::pc ? 0x52u : 0xA01255u);
        step();
        [[fallthrough]];
    case 1: {
        of<uint32_t>(obj, 0x30) = of<uint32_t>(obj, 0x30) + (of<uint32_t>(obj, 0x320) == 0 ? 0xFFFFE800u : 0x1800u);
        const float dx = of<float>(obj, 0x3A0) - of<float>(obj, 0x20);
        const float dy = of<float>(obj, 0x3A4) - of<float>(obj, 0x24);
        const float dz = of<float>(obj, 0x3A8) - of<float>(obj, 0x28);
        float vx, vy, vz;
        if (pc_path()) {
            const float inv = 1.0f / pc_fast_sqrt(dx * dx + dy * dy + dz * dz);
            vx = dx * inv;
            vy = dy * inv;
            vz = dz * inv;
        } else {
            Tgp::Out o;
            const uint32_t v[3] = {fbits(dx), fbits(dy), fbits(dz)};
            copro(0x0C, v, 3, o);
            vx = ffrom(o.w[0]);
            vy = ffrom(o.w[1]);
            vz = ffrom(o.w[2]);
        }
        const float k = ffrom(0x3F666666u);
        vx = vx * k;
        vy = vy * k;
        vz = vz * k;
        of<float>(obj, 0x78) = vx;
        of<float>(obj, 0x7C) = vy;
        of<float>(obj, 0x80) = vz;
        of<float>(obj, 0x20) = vx + of<float>(obj, 0x20);
        of<float>(obj, 0x24) = vy + of<float>(obj, 0x24);
        of<float>(obj, 0x28) = vz + of<float>(obj, 0x28);
        if (std::fabs(dx) > k || std::fabs(dz) > k)
            break;
        step();
    }
        [[fallthrough]];
    case 2: {
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
        kind70_face_camera(obj);
        if (of<uint32_t>(obj, 0x54) == 0x1857)
            of<uint32_t>(obj, 0x30) = of<uint32_t>(obj, 0x30) - 0x4000;
        const uint8_t p = of<uint8_t>(obj, 0x2E8);
        if (p == 0 || p == 1)
            player_hit(int32_t(p), 1, 3);
        of<int32_t>(obj, 0x50) = 45;
        step();
    }
        [[fallthrough]];
    case 3: {
        kind70_knife_fly_sub(obj);
        of<uint32_t>(obj, 0x20) = of<uint32_t>(obj, 0x3A0);
        of<uint32_t>(obj, 0x24) = of<uint32_t>(obj, 0x3A4);
        of<uint32_t>(obj, 0x28) = of<uint32_t>(obj, 0x3A8);
        const int32_t t = of<int32_t>(obj, 0x50);
        if ((t % 2) != 0)
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x02000000u;
        else
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x02000000u;
        of<int32_t>(obj, 0x50) = t - 1;
        if (t - 1 < 0) {
            removed();
            return;
        }
        break;
    }
    case 4:
        sound_cue(P::pc ? 0x20u : 0xA0120Au);
        of<uint32_t>(obj, 0x31C) = 1;
        of<uint32_t>(obj, 0x88) = 0xBCDF0123u;
        of<uint32_t>(obj, 0x7C) = 0x3E99999Au;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xFFFFFEFEu;
        step();
        [[fallthrough]];
    case 5: {
        const float vy = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
        of<float>(obj, 0x7C) = vy;
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
        if (!(of<float>(obj, 0x24) >= mem.f32(G::floor_y))) {
            removed();
            return;
        }
        break;
    }
    default:
        break;
    }
    if (of<uint32_t>(obj, 0x31C) == 0 &&
        (((of<uint16_t>(obj, 0x58) >> 8) & 1) || (of<uint8_t>(of<uint32_t>(obj, 0x35C), 0x58) & 1) == 0))
        of<int16_t>(obj, 0x60) = 4;
    if (((of<uint32_t>(obj, 0x58) >> 25) & 1) == 0) {
        {
            LocalScope ls(*this);
            const uint32_t q = local_alloc(12);   // the probe point: x, 32768, z
            mem.set32(q, of<uint32_t>(obj, 0x20));
            mem.set32(q + 4, 0x47000000u);
            mem.set32(q + 8, of<uint32_t>(obj, 0x28));
            shadow_setup(ground_height(q));
        }
        if constexpr (P::pc) {
            pc_matrix_push();
            pc_matrix_translate10(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28));
            pc_matrix_rotate_y10(of<uint32_t>(obj, 0x30));
            pc_matrix_rotate_x10(of<uint32_t>(obj, 0x2C));
            pc_matrix_rotate_b10(of<uint32_t>(obj, 0x34));
            const float s = of<float>(obj, 0x94);
            const uint32_t m = mem.u32(G::pc_matrix_ptr);
            for (uint32_t i = 0; i < 9; ++i)
                mem.at<float>(m + 4 * i) = mem.at<float>(m + 4 * i) * s;
            pc_draw_state(int32_t(of<int16_t>(obj, 0x2E0)));
            pc_matrix_position_draw(of<uint32_t>(obj, 0x54), obj + ob(0x38));
            if (mem.u32(G::pc_draw_bats) != 0) {
                pc_matrix_mul_stored10(G::pc_stored_matrices + 0x90);
                pc_draw_state(0);
                geo_draw_model(of<uint32_t>(obj, 0x54));
                pc_draw_state(0);
            }
            pcm_pop();
        } else {
            Tgp::Out none;
            copro(0x05, nullptr, 0, none);
            const uint32_t t[3] = {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)};
            copro(0x12, t, 3, none);
            const uint32_t ry[1] = {of<uint32_t>(obj, 0x30)};
            copro(0x15, ry, 1, none);
            const uint32_t rx[1] = {of<uint32_t>(obj, 0x2C)};
            copro(0x14, rx, 1, none);
            const uint32_t rz[1] = {of<uint32_t>(obj, 0x34)};
            copro(0x16, rz, 1, none);
            const uint32_t sc[3] = {of<uint32_t>(obj, 0x94), of<uint32_t>(obj, 0x94), of<uint32_t>(obj, 0x94)};
            copro(0x13, sc, 3, none);
            draw_fade_begin(int32_t(of<int16_t>(obj, 0x2E0)));
            geo_matrix_position(of<uint32_t>(obj, 0x54), obj + ob(0x38));
            const uint32_t three[1] = {3};
            copro(0x77, three, 1, none);
            draw_fade_begin(0);
            geo_draw_model(of<uint32_t>(obj, 0x54));
            geo_send_words(0, 31, 0x7A0);
            copro(0x06, nullptr, 0, none);
        }
    }
    depth_list_add(obj);
}

// 0x92380 / PC 0x502E14 (Moody's thrown object), by step: 0: a target player (pick_target_player_any), the target
// point (moody_projectile_target), the sound 0xA0134A; 1: its model cycling (+0x54 + 2, back by 18 past the type's
// last), 0.9 a frame along the unit vector to the target (A: coprocessor 0x0C; PC: 1 / the table square root), turned
// to it - 0x4000; within 0.9 in x and z: 2: the hit on players 1 / 2 (player_hit side 2), 45 frames; 3: held at the
// target point, blinking, then not shootable and removed. Thrower dead: 4: +0x31C 1, gravity, not shootable, vy 0.3;
// 5: falling until below the floor, then removed. Shot: an impact effect (surface 3), life +0x70 - 1; at 0 the sound
// 0xA0124A, removed. Drawn unless bit 25: shadow, the model at its position / angles / scale +0x94, faded; the
// current matrix kept for the thrower's slot (kind70_slot_matrix); PC: its matrix helpers, the model only with the
// option pc_draw_bats
template <class P> void Game<P>::moody_projectile(uint32_t obj)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    const uint32_t owner = of<uint32_t>(obj, 0x35C);
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        pick_target_player_any(obj);
        moody_projectile_target(obj);
        sound_cue(P::pc ? 0x86u : 0xA0134Au);
        step();
        [[fallthrough]];
    case 1: {
        of<int32_t>(obj, 0x54) = of<int32_t>(obj, 0x54) + 2;
        int32_t last = 0;
        switch (of<int16_t>(owner, 0xCC)) {
        case 0x3B: last = 0x1662; break;
        case 0x5C: last = 0x18DF; break;
        case 0x5D: last = 0x1AA0; break;
        default: break;
        }
        if (last != 0 && of<int32_t>(obj, 0x54) > last)
            of<int32_t>(obj, 0x54) = last - 18;
        const float dx = of<float>(obj, 0x3A0) - of<float>(obj, 0x20);
        const float dy = of<float>(obj, 0x3A4) - of<float>(obj, 0x24);
        const float dz = of<float>(obj, 0x3A8) - of<float>(obj, 0x28);
        float vx, vy, vz;
        uint32_t yaw;
        if (pc_path()) {
            const float inv = 1.0f / pc_fast_sqrt(dx * dx + dy * dy + dz * dz);
            vx = dx * inv;
            vy = dy * inv;
            vz = dz * inv;
            of<float>(obj, 0x78) = vx;
            of<float>(obj, 0x7C) = vy;
            of<float>(obj, 0x80) = vz;
            uint32_t pitch = 0;
            pc_vec_angles10(of<float>(obj, 0x3A0) - of<float>(obj, 0x20), of<float>(obj, 0x3A4) - of<float>(obj, 0x24),
                            of<float>(obj, 0x3A8) - of<float>(obj, 0x28), yaw, pitch);
        } else {
            Tgp::Out o;
            const uint32_t v[3] = {fbits(dx), fbits(dy), fbits(dz)};
            copro(0x0C, v, 3, o);
            of<uint32_t>(obj, 0x78) = o.w[0];
            of<uint32_t>(obj, 0x7C) = o.w[1];
            of<uint32_t>(obj, 0x80) = o.w[2];
            yaw = yaw_toward_a(of<float>(obj, 0x3A0) - of<float>(obj, 0x20), of<float>(obj, 0x3A4) - of<float>(obj, 0x24),
                               of<float>(obj, 0x3A8) - of<float>(obj, 0x28));
        }
        const float k = ffrom(0x3F666666u);
        of<float>(obj, 0x78) = of<float>(obj, 0x78) * k;
        of<float>(obj, 0x7C) = of<float>(obj, 0x7C) * k;
        of<float>(obj, 0x80) = of<float>(obj, 0x80) * k;
        of<uint32_t>(obj, 0x30) = yaw - 0x4000;
        of<float>(obj, 0x20) = of<float>(obj, 0x78) + of<float>(obj, 0x20);
        of<float>(obj, 0x24) = of<float>(obj, 0x7C) + of<float>(obj, 0x24);
        of<float>(obj, 0x28) = of<float>(obj, 0x80) + of<float>(obj, 0x28);
        if (std::fabs(dx) > k || std::fabs(dz) > k)
            break;
        step();
    }
        [[fallthrough]];
    case 2: {
        const uint8_t p = of<uint8_t>(obj, 0x2E8);
        if (p == 0 || p == 1)
            player_hit(of<int8_t>(obj, 0x2E8), 1, 2);
        of<int32_t>(obj, 0x50) = 45;
        step();
    }
        [[fallthrough]];
    case 3: {
        moody_projectile_target(obj);
        of<uint32_t>(obj, 0x20) = of<uint32_t>(obj, 0x3A0);
        of<uint32_t>(obj, 0x24) = of<uint32_t>(obj, 0x3A4);
        of<uint32_t>(obj, 0x28) = of<uint32_t>(obj, 0x3A8);
        const int32_t t = of<int32_t>(obj, 0x50);
        of<int32_t>(obj, 0x50) = t - 1;
        if ((t % 2) != 0)
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x02000000u;
        else
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x02000000u;
        if (of<int32_t>(obj, 0x50) >= 0)
            break;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
        task_exit();
        return;
    }
    case 4:
        of<uint32_t>(obj, 0x31C) = 1;
        of<uint32_t>(obj, 0x88) = 0xBCDF0123u;
        of<uint32_t>(obj, 0x7C) = 0x3E99999Au;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xFFFFFEFEu;
        step();
        [[fallthrough]];
    case 5: {
        const float vy = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
        of<float>(obj, 0x7C) = vy;
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
        if (!(of<float>(obj, 0x24) >= mem.f32(G::floor_y))) {
            task_exit();
            return;
        }
        break;
    }
    default:
        break;
    }
    if (of<uint32_t>(obj, 0x31C) == 0 && (of<uint8_t>(owner, 0x58) & 1) == 0)
        of<int16_t>(obj, 0x60) = 4;
    if ((of<uint16_t>(obj, 0x58) >> 8) & 1) {
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x100u;
        impact_effect_spawn(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28), 3);
        of<int16_t>(obj, 0x70) = int16_t(of<int16_t>(obj, 0x70) - 1);
        if (of<int16_t>(obj, 0x70) <= 0) {
            sound_cue(P::pc ? 0x4Du : 0xA0124Au);
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
            task_exit();
            return;
        }
    }
    if (((of<uint32_t>(obj, 0x58) >> 25) & 1) == 0) {
        {
            LocalScope ls(*this);
            const uint32_t q = local_alloc(12);   // the probe point: x, 32768, z
            mem.set32(q, of<uint32_t>(obj, 0x20));
            mem.set32(q + 4, 0x47000000u);
            mem.set32(q + 8, of<uint32_t>(obj, 0x28));
            shadow_setup(ground_height(q));
        }
        const uint32_t keep = G::kind70_slot_matrix + of<uint32_t>(owner, 0x31C) * 0x30;
        if constexpr (P::pc) {
            pc_matrix_push();
            pc_matrix_translate10(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28));
            pc_matrix_rotate_y10(of<uint32_t>(obj, 0x30));
            pc_matrix_rotate_x10(of<uint32_t>(obj, 0x2C));
            pc_matrix_rotate_b10(of<uint32_t>(obj, 0x34));
            const float s = of<float>(obj, 0x94);
            const uint32_t m = mem.u32(G::pc_matrix_ptr);
            for (uint32_t i = 0; i < 9; ++i)
                mem.at<float>(m + 4 * i) = mem.at<float>(m + 4 * i) * s;
            pc_draw_state(int32_t(of<int16_t>(obj, 0x2E0)));
            for (uint32_t i = 0; i < 12; ++i)
                mem.set32(keep + 4 * i, mem.u32(m + 4 * i));
            pc_matrix_position_draw(of<uint32_t>(obj, 0x54), obj + ob(0x38));
            if (mem.u32(G::pc_draw_bats) != 0) {
                pc_matrix_mul_stored10(G::pc_stored_matrices + 0x90);
                pc_draw_state(0);
                geo_draw_model(of<uint32_t>(obj, 0x54));
                pc_draw_state(0);
            }
            pcm_pop();
        } else {
            Tgp::Out none, mo;
            copro(0x05, nullptr, 0, none);
            const uint32_t t[3] = {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)};
            copro(0x12, t, 3, none);
            const uint32_t ry[1] = {of<uint32_t>(obj, 0x30)};
            copro(0x15, ry, 1, none);
            const uint32_t rx[1] = {of<uint32_t>(obj, 0x2C)};
            copro(0x14, rx, 1, none);
            const uint32_t rz[1] = {of<uint32_t>(obj, 0x34)};
            copro(0x16, rz, 1, none);
            const uint32_t sc[3] = {of<uint32_t>(obj, 0x94), of<uint32_t>(obj, 0x94), of<uint32_t>(obj, 0x94)};
            copro(0x13, sc, 3, none);
            draw_fade_begin(int32_t(of<int16_t>(obj, 0x2E0)));
            copro(0x11, nullptr, 0, mo);
            for (uint32_t i = 0; i < 12; ++i)
                mem.set32(keep + 4 * i, mo.w[i]);
            geo_matrix_position(of<uint32_t>(obj, 0x54), obj + ob(0x38));
            const uint32_t three[1] = {3};
            copro(0x77, three, 1, none);
            draw_fade_begin(0);
            geo_draw_model(of<uint32_t>(obj, 0x54));
            geo_send_words(0, 31, 0x7A0);
            copro(0x06, nullptr, 0, none);
        }
    }
    depth_list_add(obj);
}

// 0x906C0 / PC 0x5017A0 (set-up 9, the fade-in; its own task routine until it lands), by step: 0: +0x58 bit 14,
// home from the record (+0x368), model 0x1478, routine itself, state 20; 1: drawn as a plain model at its position /
// angles (A: faded 0xFF, coprocessor push ... pop; PC: unless pc_no_extra_model, its matrix helpers); event flag 9:
// removed; with event flag 4 it waits +0x2F4 frames; 2: event flag 5 (slot 0) or 6 set, the cry 0xA01231, counted,
// the body (motion 0x19E), type set-up, AI flags, an enemy slot, a 40-frame jump home, routine kind70_frame; 3:
// landed: facing the camera (y 0), camera side, state 18 step 1, bit 14 cleared
template <class P> void Game<P>::kind70_spawn9_fade_in(uint32_t obj)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    const uint32_t r = of<uint32_t>(obj, 0x368);
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x4000u;
        of<uint32_t>(obj, 0x3AC) = mem.u32(r);
        of<uint32_t>(obj, 0x3B0) = mem.u32(r + 4);
        of<uint32_t>(obj, 0x3B4) = mem.u32(r + 8);
        of<uint32_t>(obj, 0x54) = 0x1478;
        of<uint32_t>(obj, 0x0) = F::kind70_spawn9_fade_in;
        of<int16_t>(obj, 0x5C) = 20;
        step();
        [[fallthrough]];
    case 1:
        if constexpr (P::pc) {
            if (mem.u32(G::pc_no_extra_model) == 0) {
                pc_draw_state(0xFF);
                pc_matrix_push();
                pc_matrix_translate10(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28));
                pc_matrix_rotate_y10(of<uint32_t>(obj, 0x30));
                pc_matrix_rotate_x10(of<uint32_t>(obj, 0x2C));
                pc_matrix_rotate_b10(of<uint32_t>(obj, 0x34));
                geo_draw_model(of<uint32_t>(obj, 0x54));
                pcm_pop();
                pc_draw_state(0);
            }
        } else {
            draw_fade_begin(0xFF);
            Tgp::Out none;
            copro(0x05, nullptr, 0, none);
            const uint32_t t[3] = {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)};
            copro(0x12, t, 3, none);
            const uint32_t ry[1] = {of<uint32_t>(obj, 0x30)};
            copro(0x15, ry, 1, none);
            const uint32_t rx[1] = {of<uint32_t>(obj, 0x2C)};
            copro(0x14, rx, 1, none);
            const uint32_t rz[1] = {of<uint32_t>(obj, 0x34)};
            copro(0x16, rz, 1, none);
            const uint32_t sc[3] = {0x3F800000u, 0x3F800000u, 0x3F800000u};
            copro(0x13, sc, 3, none);
            geo_draw_model(of<uint32_t>(obj, 0x54));
            copro(0x06, nullptr, 0, none);
            geo_send_words(0, 31, 0x7A0);
        }
        if (mem.at<uint8_t>(G::event_flags + 9) == 1) {
            task_exit();
            return;
        }
        if (mem.at<uint8_t>(G::event_flags + 4) != 1)
            return;
        of<int32_t>(obj, 0x2F4) = of<int32_t>(obj, 0x2F4) - 1;
        if (of<int32_t>(obj, 0x2F4) > 0)
            return;
        step();
        [[fallthrough]];
    case 2: {
        mem.at<uint8_t>(G::event_flags + (of<uint32_t>(obj, 0x31C) == 0 ? 5 : 6)) = 1;
        sound_cue(P::pc ? 0x3Cu : 0xA01231u);
        mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
        of<int8_t>(obj, 0x2E7) = -1;
        const int32_t type = of<int16_t>(obj, 0xCC);
        mem.set32(G::hit_type, uint32_t(type));
        body_init(obj, type, 0x19E);
        creature_type_setup(obj);
        of<uint8_t>(obj, 0x2E5) = mem.at<uint8_t>(r + 0x12);
        enemy_slot_take(obj);
        of<uint16_t>(obj, 0x2E0) = 0xFF;
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) | 4);
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                     of<uint32_t>(obj, 0x3AC), of<uint32_t>(obj, 0x3B0), of<uint32_t>(obj, 0x3B4), 40);
        of<uint32_t>(obj, 0x0) = F::kind70_frame;
        step();
    }
        [[fallthrough]];
    case 3:
        if (jump_arc_step_b(obj) != 1)
            return;
        kind70_face(obj, mem.f32(G::camera_pos) - of<float>(obj, 0x20), 0.0f, mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28));
        camera_side_set(obj);
        of<int16_t>(obj, 0x5C) = 18;
        of<int16_t>(obj, 0x60) = 1;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x4000u;
        return;
    default:
        return;
    }
}

// 0x8C080 / PC 0x4FDF64 (state 0): nothing
template <class P> void Game<P>::kind70_st00_none(uint32_t obj) { (void)obj; }

// 0x8C090 / PC 0x4FDF68: +0x2E0 = 0xE1; the Chapter 1 route check; the hits (alive, +0x324 bit 3 clear); a hit reaction
// (+0x58 bit 28: airborne by +0x324 bit 5); the state routine (unless frozen, bit 27); x += vx, vy += ay, y += vy,
// z += vz; drawn unless bit 25; the frame counters +0xF8 / +0x10C unless bit 26; depth list, attackers list
template <class P> void Game<P>::kind70_frame(uint32_t obj)
{
    of<int16_t>(obj, 0x2E0) = 0xE1;
    kind70_ch1_route_check(obj);
    if ((of<uint8_t>(obj, 0x58) & 1) != 0 && (of<uint8_t>(obj, 0x324) & 8) == 0)
        kind70_hits(obj);
    if ((of<uint32_t>(obj, 0x58) >> 28) & 1) {
        if ((of<uint8_t>(obj, 0x324) & 0x20) == 0)
            kind70_hit_reaction(obj);
        else
            kind70_hit_reaction_air(obj);
    }
    if (((of<uint32_t>(obj, 0x58) >> 27) & 1) == 0)
        kind70_call(mem.u32(G::kind70_states + uint32_t(int32_t(of<int16_t>(obj, 0x5C))) * 4), obj);
    const float vy = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
    of<float>(obj, 0x20) = of<float>(obj, 0x20) + of<float>(obj, 0x78);
    of<float>(obj, 0x7C) = vy;
    of<float>(obj, 0x28) = of<float>(obj, 0x28) + of<float>(obj, 0x80);
    of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
    if (((of<uint32_t>(obj, 0x58) >> 25) & 1) == 0)
        object_draw(obj);
    if (((of<uint32_t>(obj, 0x58) >> 26) & 1) == 0) {
        of<uint32_t>(obj, 0xF8) = of<uint32_t>(obj, 0xF8) + 1;
        of<uint32_t>(obj, 0x10C) = of<uint32_t>(obj, 0x10C) + 1;
    }
    depth_list_add(obj);
    attacker_list_add(obj);
}

// 0x8C1A0 / PC 0x4FE098: Chapter 1 event 4 with this creature's +0x320 clear and it the last enemy alive: +0x58 bits 0
// and 28 cleared; in state 2 it goes to state 12 and, at step 8 or below, to step 9 with route_choice = (slot != 0)
template <class P> void Game<P>::kind70_ch1_route_check(uint32_t obj)
{
    if (mem.at<uint8_t>(G::chapter) != 0 || mem.at<uint8_t>(G::event_index) != 4 || of<uint32_t>(obj, 0x320) != 0
        || mem.at<uint16_t>(G::enemies_alive) != 1)
        return;
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xEFFFFFFEu;
    if (of<int16_t>(obj, 0x5C) != 2)
        return;
    of<int16_t>(obj, 0x5C) = 12;
    if (of<int16_t>(obj, 0x60) > 8)
        return;
    of<int16_t>(obj, 0x60) = 9;
    mem.at<uint8_t>(G::route_choice) = of<uint32_t>(obj, 0x31C) != 0 ? 1 : 0;
}

// 0x8C230 / PC 0x4FE118: the part hits (enemy_part_hits); a hit with damage: the hit class to +0x334, life -= damage;
// unless +0x324 bit 4: the reaction starts (+0x338 = 0 when none was running, +0x330 = 0, bit 28; airborne for hit
// classes 1 / 2 / 5 modulo 9 or off the floor) and the target player is released. Killed (life <= 0 and no +0x324 bit
// 4): enemies_alive -1, the attack slot freed, not alive, the death cry (monkeys 0xA0120E, others 0xA0131F), state 1;
// else the hurt cry 0xA01212 and, for Moody (0x3B / 0x5C / 0x5D) on the ground, no reaction
template <class P> void Game<P>::kind70_hits(uint32_t obj)
{
    mem.at<uint8_t>(G::hit_class) = 0;
    mem.at<uint16_t>(G::hit_damage) = 0;
    mem.set32(G::hit_type, uint32_t(int32_t(of<int16_t>(obj, 0xCC))));
    enemy_part_hits(obj);
    if (((of<uint16_t>(obj, 0x58) >> 8) & 1) == 0 || mem.at<uint16_t>(G::hit_damage) == 0)
        return;
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x100u;
    of<int32_t>(obj, 0x334) = mem.at<int8_t>(G::hit_class);
    of<uint16_t>(obj, 0x70) = uint16_t(of<uint16_t>(obj, 0x70) - mem.at<uint16_t>(G::hit_damage));
    if ((of<uint8_t>(obj, 0x324) & 0x10) == 0) {
        if (((of<uint32_t>(obj, 0x58) >> 28) & 1) == 0)
            of<uint32_t>(obj, 0x338) = 0;
        const int32_t c = of<int32_t>(obj, 0x334) % 9;
        of<uint32_t>(obj, 0x330) = 0;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x10000000u;
        if (c == 1 || c == 2 || c == 5 || !(of<float>(obj, 0x24) == mem.at<float>(G::floor_y)))
            of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 0x20;
        if (of<uint8_t>(obj, 0x2E8) != 0xFF) {
            mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(of<int8_t>(obj, 0x2E8)))) = 0;
            of<int8_t>(obj, 0x2E8) = -1;
        }
    }
    if ((of<int16_t>(obj, 0x70) <= 0 || return_false() != 0) && (of<uint8_t>(obj, 0x324) & 0x10) == 0) {
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        if ((of<uint8_t>(obj, 0x2E5) >> 6) & 1)
            mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 20) = 0;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
        const int16_t t = of<int16_t>(obj, 0xCC);
        if (t == 1 || t == 9)
            sound_cue(P::pc ? 0x24u : 0xA0120Eu);
        else
            sound_cue(P::pc ? 0x68u : 0xA0131Fu);
        if (of<uint8_t>(obj, 0x2E8) != 0xFF) {
            mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(of<int8_t>(obj, 0x2E8)))) = 0;
            of<int8_t>(obj, 0x2E8) = -1;
        }
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xE7FFFFFFu;
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 1;
        return;
    }
    sound_cue(P::pc ? 0x25u : 0xA01212u);
    const uint16_t t = of<uint16_t>(obj, 0xCC);
    if ((t == 0x3B || t == 0x5C || t == 0x5D) && (of<uint8_t>(obj, 0x324) & 0x20) == 0)
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x10000000u;
}

// 0x8C420 / PC 0x4FE320 (the ground hit reaction, by its step +0x338): 0: stopped, frozen (bit 27), the type's hit
// motion (hit_motions_by_type[type][class % 9][class / 9]); 1: waits for +0x3C5 bit 4 (the motion's end); 2: bits 27 /
// 28 cleared, back to the AI (kind70_after_hit_state)
template <class P> void Game<P>::kind70_hit_reaction(uint32_t obj)
{
    switch (of<uint32_t>(obj, 0x338)) {
    case 0: {
        obj_stop(obj);
        const int32_t c = of<int32_t>(obj, 0x334);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x08000000u;
        const uint32_t row = mem.u32(G::hit_motions_by_type + uint32_t(int32_t(of<int16_t>(obj, 0xCC))) * 4);
        set_motion(obj, mem.u32(row + uint32_t(c % 9) * 8 + uint32_t(c / 9) * 4));
        of<uint32_t>(obj, 0x338) = of<uint32_t>(obj, 0x338) + 1;
    }
        [[fallthrough]];
    case 1:
        if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) == 0)
            return;
        of<uint32_t>(obj, 0x338) = of<uint32_t>(obj, 0x338) + 1;
        [[fallthrough]];
    case 2:
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xE7FFFFFFu;
        kind70_after_hit_state(obj);
        return;
    default:
        return;
    }
}

// the ground height under the creature (ground_height on a local copy of its position)
template <class P> float Game<P>::kind70_ground(uint32_t obj)
{
    LocalScope ls(*this);
    const uint32_t q = local_alloc(12);
    mem.set32(q, of<uint32_t>(obj, 0x20));
    mem.set32(q + 4, of<uint32_t>(obj, 0x24));
    mem.set32(q + 8, of<uint32_t>(obj, 0x28));
    return ground_height(q);
}

// the start of a fall (the airborne hit reaction, the death): the fall target = the screen point +0x38 / +0x3C /
// +0x40 + `depth` through the camera matrix (A: coprocessor 0x74 slot 1, 0x1A, 0x06; PC: the stored matrix 1 and
// 0x4FBF30), facing the camera (`level`: y 0), a 30-frame jump arc to the target 3 higher, the bounce count +0x328 and
// +0x334 cleared, the ground height to +0x33C
template <class P> void Game<P>::kind70_fall_start(uint32_t obj, float depth, bool level)
{
    if (pc_path()) {
        const float z = of<float>(obj, 0x40) + depth;
        pc_matrix_push();
        pcm_load_camera();
        float ox, oy, oz;
        pc_matrix_transform(of<float>(obj, 0x38), of<float>(obj, 0x3C), z, ox, oy, oz);
        of<float>(obj, 0x3A0) = ox;
        of<float>(obj, 0x3A4) = oy;
        of<float>(obj, 0x3A8) = oz;
        pcm_pop();
    } else {
        Tgp::Out o, none;
        const uint32_t one[1] = {1};
        copro(0x74, one, 1, none);
        const uint32_t v[3] = {of<uint32_t>(obj, 0x38), of<uint32_t>(obj, 0x3C), fbits(of<float>(obj, 0x40) + depth)};
        copro(0x1A, v, 3, o);
        of<uint32_t>(obj, 0x3A0) = o.w[0];
        of<uint32_t>(obj, 0x3A4) = o.w[1];
        of<uint32_t>(obj, 0x3A8) = o.w[2];
        copro(0x06, nullptr, 0, none);
    }
    kind70_face(obj, mem.f32(G::camera_pos) - of<float>(obj, 0x20),
                level ? 0.0f : mem.f32(G::camera_pos + 4) - of<float>(obj, 0x24),
                mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28));
    jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28), of<uint32_t>(obj, 0x3A0),
                 fbits(of<float>(obj, 0x3A4) + 3.0f), of<uint32_t>(obj, 0x3A8), 30);
    of<uint32_t>(obj, 0x328) = 0;
    of<uint32_t>(obj, 0x334) = 0;
    of<float>(obj, 0x33C) = kind70_ground(obj);
}

// a wall of the room box (or the ceiling, AI flag bit 4) hit while falling: MO 0x1B8 again, +0x58 bit 26 cleared
template <class P> void Game<P>::kind70_fall_wall(uint32_t obj)
{
    const uint32_t hit = ((of<uint8_t>(obj, 0x2E5) >> 4) & 1) ? room_ceiling_bounce(obj) : room_bounds_bounce(obj);
    if (hit == 1) {
        set_motion(obj, 0x1B8);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x04000000u;
    }
}

// the first part of a fall: after frame 27 the frame is held at 29 (+0x58 bit 26); walls; the arc; 1 when it is over
// (gravity +0x88 = -0.0272)
template <class P> bool Game<P>::kind70_fall_arc(uint32_t obj)
{
    if (of<int16_t>(obj, 0xF4) > 27 && ((of<uint32_t>(obj, 0x58) >> 26) & 1) == 0) {
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x04000000u;
        of<int16_t>(obj, 0xF4) = 29;
    }
    kind70_fall_wall(obj);
    if (jump_arc_velocity_b(obj) != 1)
        return false;
    of<uint32_t>(obj, 0x88) = 0xBCDF0123u;
    return true;
}

// the falling part: walls; the motion's end holds the frame (bit 26); above the ground: the ground height kept, 0;
// on it: bounce (vy x -0.2 in double, on the ground + vy), the floor splat on the first, MO 0x1A0, the cry
// 0xA01224; 1, or 2 after the fourth bounce (`ground`: the ground height)
template <class P> int Game<P>::kind70_fall_floor(uint32_t obj, float& ground)
{
    kind70_fall_wall(obj);
    if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) && ((of<uint32_t>(obj, 0x58) >> 26) & 1) == 0)
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x04000000u;
    ground = kind70_ground(obj);
    if (!(of<float>(obj, 0x24) <= of<float>(obj, 0x33C))) {
        of<float>(obj, 0x33C) = ground;
        return 0;
    }
    const float vy = float(double(of<float>(obj, 0x7C)) * -0.2);
    of<float>(obj, 0x7C) = vy;
    of<float>(obj, 0x24) = vy + ground;
    if (of<uint32_t>(obj, 0x328) == 0)
        floor_splat_spawn(obj);
    set_motion(obj, 0x1A0);
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x04000000u;
    sound_cue(P::pc ? 0x31u : 0xA01224u);
    of<int32_t>(obj, 0x328) = of<int32_t>(obj, 0x328) + 1;
    return of<int32_t>(obj, 0x328) > 3 ? 2 : 1;
}

// 0x8C620 / PC 0x4FE53C (the airborne hit reaction, by +0x338): 0: stopped, the hand model back, frozen (bit 27), MO
// 0x1B8 (sarusube), the fall towards the screen point 8 deeper; 1: the arc; 2: falling and bouncing; after the fourth
// bounce: stopped on the ground, bit 26, the slot's state -1, timer 40, +0x3C5 bit 2 cleared; 3, 4: +0x324 bits 3 / 5
// and +0x58 bits 26-28 cleared, back to the AI
template <class P> void Game<P>::kind70_hit_reaction_air(uint32_t obj)
{
    auto step = [&] { of<uint32_t>(obj, 0x338) = of<uint32_t>(obj, 0x338) + 1; };
    switch (of<uint32_t>(obj, 0x338)) {
    case 0:
        obj_stop(obj);
        agile_hand_model_restore(obj);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x08000000u;
        set_motion(obj, 0x1B8);
        kind70_fall_start(obj, 8.0f, false);
        step();
        [[fallthrough]];
    case 1:
        if (!kind70_fall_arc(obj))
            return;
        step();
        [[fallthrough]];
    case 2: {
        float ground;
        if (kind70_fall_floor(obj, ground) != 2)
            return;
        obj_stop(obj);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x04000000u;
        mem.at<int32_t>(G::kind70_slot_state + of<uint32_t>(obj, 0x31C) * 4) = -1;
        of<float>(obj, 0x24) = ground;
        of<int32_t>(obj, 0x50) = 40;
        step();
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~4u);
    }
        [[fallthrough]];
    case 3:
        step();
        [[fallthrough]];
    case 4:
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & 0xFFFFFFD7u;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xE3FFFFFFu;
        kind70_after_hit_state(obj);
        return;
    default:
        return;
    }
}

// 0x8CA40 / PC 0x4FE8B8 (state 1, the death): 0: 80 points (enemy_kill_score), MO 0x1B8, stopped, the hand model
// back, the fall towards the screen point 12 deeper (facing level); 1: the arc; 2: falling and bouncing; after the
// fourth: stopped, its attack slot freed (AI flag bit 6 clear), bit 26, the slot's state -1, on the ground, timer 40,
// +0x3C5 bit 2 cleared; 3: blinking (+0x58 bit 25 on odd timer values) until the timer runs out; 4: enemy_count - 1,
// the body freed, the task ends
template <class P> void Game<P>::kind70_st01_death(uint32_t obj)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        enemy_kill_score(obj, 80);
        set_motion(obj, 0x1B8);
        obj_stop(obj);
        agile_hand_model_restore(obj);
        kind70_fall_start(obj, 12.0f, true);
        step();
        [[fallthrough]];
    case 1:
        if (!kind70_fall_arc(obj))
            return;
        step();
        [[fallthrough]];
    case 2: {
        float ground;
        if (kind70_fall_floor(obj, ground) != 2)
            return;
        obj_stop(obj);
        if (((of<uint8_t>(obj, 0x2E5) >> 6) & 1) == 0)
            mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 20) = 0;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x04000000u;
        mem.at<int32_t>(G::kind70_slot_state + of<uint32_t>(obj, 0x31C) * 4) = -1;
        of<float>(obj, 0x24) = ground;
        of<int32_t>(obj, 0x50) = 40;
        step();
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~4u);
    }
        [[fallthrough]];
    case 3: {
        const int32_t t = of<int32_t>(obj, 0x50);
        if ((t % 2) != 0)
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x02000000u;
        else
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x02000000u;
        of<int32_t>(obj, 0x50) = t - 1;
        if (t - 1 >= 0)
            return;
        step();
    }
        [[fallthrough]];
    case 4:
        mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
        body_free_parts(obj);
        task_exit();
        return;
    default:
        return;
    }
}

// 0x8C4C0 / PC 0x4FE3F4 (state 11, by +0x338): +0x324 bit 3, frozen (bit 27), MO 0x1B7 (sarusaigo00a), stopped,
// MO 0x1BD (the turn), a 24-frame jump home (+0x3AC, on the floor), MO 0x19A, all at once; landed: on the floor, the
// cry 0xA01225; then +0x3C5 bit 2 cleared, stopped, +0x324 bits 3 / 5 and +0x58 bits 26-28 cleared, back to the AI
template <class P> void Game<P>::kind70_st11_last(uint32_t obj)
{
    auto step = [&] { of<uint32_t>(obj, 0x338) = of<uint32_t>(obj, 0x338) + 1; };
    switch (of<uint32_t>(obj, 0x338)) {
    case 0:
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 8;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x08000000u;
        set_motion(obj, 0x1B7);
        obj_stop(obj);
        step();
        [[fallthrough]];
    case 1:
        step();
        [[fallthrough]];
    case 2:
        set_motion(obj, 0x1BD);
        step();
        [[fallthrough]];
    case 3:
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                     of<uint32_t>(obj, 0x3AC), mem.u32(G::floor_y), of<uint32_t>(obj, 0x3B4), 24);
        set_motion_blend(obj, 0x19A, 0, 4);
        step();
        [[fallthrough]];
    case 4:
        if (jump_arc_velocity(obj) != 1)
            return;
        of<uint32_t>(obj, 0x24) = mem.u32(G::floor_y);
        sound_cue(P::pc ? 0x32u : 0xA01225u);
        step();
        [[fallthrough]];
    case 5:
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~4u);
        of<uint32_t>(obj, 0x24) = mem.u32(G::floor_y);
        obj_stop(obj);
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & 0xFFFFFFD7u;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xE3FFFFFFu;
        kind70_after_hit_state(obj);
        return;
    default:
        return;
    }
}

// 0x92D10 (PC: inline): state 2 (approach) when the AI flags +0x2E5 have bit 3, else 3 (think); step 0
template <class P> void Game<P>::kind70_after_hit_state(uint32_t obj)
{
    of<int16_t>(obj, 0x5C) = ((of<uint8_t>(obj, 0x2E5) >> 3) & 1) ? 2 : 3;
    of<int16_t>(obj, 0x60) = 0;
}

// PC 0x4FBFB0 (kind 69 / 70 file): (ax, az, bx, bz) floor distance through the table square root
template <class P> float Game<P>::pc_floor_distance10(float ax, float az, float bx, float bz)
{
    const float dx = bx - ax, dz = bz - az;
    return pc_fast_sqrt_table(dx * dx + dz * dz);
}
// PC 0x4FC018: the kind 69 / 70 file's copy of pc_vec_angles (its square root pc_fast_sqrt)
template <class P> void Game<P>::pc_vec_angles10(float x, float y, float z, uint32_t& yaw, uint32_t& pitch)
{
    const float h = pc_fast_sqrt(z * z + x * x);
    pitch = 0u - (uint32_t(pc_atan2(-y, h)) << 4);
    yaw = 0u - (uint32_t(pc_atan2(x, z)) << 4);
    fixAngles(yaw, pitch);
}

// the floor distance to the camera (A: coprocessor 0x1F on y = 0; PC: the table square root)
template <class P> float Game<P>::kind70_camera_distance(uint32_t obj)
{
    const float x = of<float>(obj, 0x20), z = of<float>(obj, 0x28);
    if (pc_path())
        return pc_floor_distance10(x, z, mem.f32(G::camera_pos), mem.f32(G::camera_pos + 8));
    else
        return floor_distance(x, z, mem.f32(G::camera_pos), mem.f32(G::camera_pos + 8));
}

// the heading (+0x30) toward (dx, dy, dz) (A: coprocessor 0x0F and the angle fold; PC: pc_vec_angles10)
template <class P> void Game<P>::kind70_face(uint32_t obj, float dx, float dy, float dz)
{
    if (pc_path()) {
        uint32_t yaw = 0, pitch = 0;
        pc_vec_angles10(dx, dy, dz, yaw, pitch);
        of<uint32_t>(obj, 0x30) = yaw;
    } else {
        of<uint32_t>(obj, 0x30) = yaw_toward_a(dx, dy, dz);
    }
}

// 0x2F730 / PC 0x46ADA0: the heading by the camera side (+0x2E9): 0 0x4000, 1 0xC000, 2 0x8000, 3 0
template <class P> void Game<P>::kind70_st02_sub(uint32_t obj)
{
    switch (of<int8_t>(obj, 0x2E9)) {
    case 0: of<uint32_t>(obj, 0x30) = 0x4000; return;
    case 1: of<uint32_t>(obj, 0x30) = 0xC000; return;
    case 2: of<uint32_t>(obj, 0x30) = 0x8000; return;
    case 3: of<uint32_t>(obj, 0x30) = 0; return;
    default: return;
    }
}

// 0x8D190 / PC 0x4FEE84 (state 2, approach): step 0: camera side, the side's heading, +0x3C6 bit 0 / +0x3C5 bit 3
// cleared, MO 0x1C0 over 4 frames; then the step cry 0xA01225 at frames 24 / 30, the heading toward the camera (the
// original passes its own y, not the difference), and within 25 of the camera: state 4
template <class P> void Game<P>::kind70_st02_approach(uint32_t obj)
{
    const int16_t s = of<int16_t>(obj, 0x60);
    if (s == 0) {
        camera_side_set(obj);
        kind70_st02_sub(obj);
        of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) & ~1u);
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~8u);
        set_motion_blend(obj, 0x1C0, 0, 4);
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    } else if (s != 1) {
        return;
    }
    const uint16_t f = of<uint16_t>(obj, 0xF4);
    if (f == 24 || f == 30)
        sound_cue(P::pc ? 0x32u : 0xA01225u);
    kind70_face(obj, mem.f32(G::camera_pos) - of<float>(obj, 0x20), of<float>(obj, 0x24),
                mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28));
    if (!(kind70_camera_distance(obj) >= 25.0f)) {
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 4;
    }
}

// 0x8D310 / PC 0x4FEF90 (state 3, think): step 0: camera side, facing the camera, +0x3C6 bit 0 / +0x3C5 bit 3
// cleared, the type's waiting motion (monkeys, kage, Moody 0x1C0; Rubin, Drake 0x89; kageta 0x7B) over 4 frames; then
// the step cry on MO 0x1C0 frames 24 / 30 and the type's think routine
template <class P> void Game<P>::kind70_st03_think(uint32_t obj)
{
    const int16_t s = of<int16_t>(obj, 0x60);
    if (s == 0) {
        camera_side_set(obj);
        kind70_face(obj, mem.f32(G::camera_pos) - of<float>(obj, 0x20), mem.f32(G::camera_pos + 4) - of<float>(obj, 0x24),
                    mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28));
        of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) & ~1u);
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~8u);
        switch (of<uint16_t>(obj, 0xCC)) {
        case 1: case 9: case 0x26: case 0x3B: case 0x5C: case 0x5D: set_motion_blend(obj, 0x1C0, 0, 4); break;
        case 0x12: case 0x25: set_motion_blend(obj, 0x89, 0, 4); break;
        case 0x44: case 0x57: set_motion_blend(obj, 0x7B, 0, 4); break;
        default: break;
        }
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    } else if (s != 1) {
        return;
    }
    const uint16_t f = of<uint16_t>(obj, 0xF4);
    if (of<uint16_t>(obj, 0xD0) == 0x1C0 && (f == 24 || f == 30))
        sound_cue(P::pc ? 0x32u : 0xA01225u);
    switch (of<uint16_t>(obj, 0xCC)) {
    case 1: case 9: case 0x26: kind70_think_monkey(obj); return;
    case 0x12: kind70_think_rubin(obj); return;
    case 0x25: kind70_think_drake(obj); return;
    case 0x3B: case 0x5C: case 0x5D: kind70_think_moody(obj); return;
    case 0x44: case 0x57: kind70_think_kageta(obj); return;
    default: return;
    }
}

// 0x8D7A0 / PC 0x4FF1BC (types 1, 9, 38): with no move pending (+0x330) and the camera nearer than 60: a random next
// state (rand & 7: 0 turn (9); 1 none; 2 / 4 / 6 along the wall in x (6) if the AI flags allow (+0x2E5 bit 2); 3 / 5 /
// 7 in z (7) if bit 1; when not allowed, a move is pending), stopped unless still thinking, the slot's state, step 0;
// nearer than 25: state 4 (the wall), nothing pending
template <class P> void Game<P>::kind70_think_monkey(uint32_t obj)
{
    if (of<uint32_t>(obj, 0x330) == 0 && !(kind70_camera_distance(obj) >= 60.0f)) {
        const uint32_t r = rand() & 7;
        int16_t st = of<int16_t>(obj, 0x5C);
        switch (r) {
        case 0:
            of<uint32_t>(obj, 0x330) = 0;
            st = 9;
            break;
        case 1:
            of<uint32_t>(obj, 0x330) = 1;
            break;
        case 2: case 4: case 6:
            if ((of<uint8_t>(obj, 0x2E5) >> 2) & 1) {
                of<uint32_t>(obj, 0x330) = 0;
                st = 6;
            } else {
                of<uint32_t>(obj, 0x330) = 1;
            }
            break;
        default:
            if ((of<uint8_t>(obj, 0x2E5) >> 1) & 1) {
                st = 7;
                of<uint32_t>(obj, 0x330) = 0;
            } else {
                of<uint32_t>(obj, 0x330) = 1;
            }
            break;
        }
        of<int16_t>(obj, 0x5C) = st;
        if (st != 3)
            obj_stop(obj);
        mem.at<int32_t>(G::kind70_slot_state + of<uint32_t>(obj, 0x31C) * 4) = of<int16_t>(obj, 0x5C);
        of<int16_t>(obj, 0x60) = 0;
    }
    if (!(kind70_camera_distance(obj) >= 25.0f)) {
        of<int16_t>(obj, 0x60) = 0;
        of<uint32_t>(obj, 0x330) = 0;
        of<int16_t>(obj, 0x5C) = 4;
        mem.at<int32_t>(G::kind70_slot_state + of<uint32_t>(obj, 0x31C) * 4) = 4;
    }
}

// 0x8DC50 / PC 0x4FF59C (type 18, Rubin): the camera nearer than 60: a random next state (rand & 7: 0 the turn (9);
// 1-3 along a wall (AI flags bits 1 and 2: 7 or 6 by a second rand, only bit 1: 7, bit 2: 6, neither: 24, the roll);
// 4-7 the roll (24)), step 0, the slot's state
template <class P> void Game<P>::kind70_think_rubin(uint32_t obj)
{
    if (kind70_camera_distance(obj) >= 60.0f)
        return;
    const uint32_t r = rand() & 7;
    if (r == 0) {
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 9;
    } else if (r <= 3) {
        const int32_t r2 = int32_t(rand());
        const uint8_t ai = of<uint8_t>(obj, 0x2E5);
        int16_t s = 24;
        if ((ai & 6) == 6) {
            s = (r2 % 2) == 0 ? 7 : 6;
        } else {
            if ((ai >> 1) & 1)
                s = 7;
            if ((ai >> 2) & 1)
                s = 6;
        }
        of<int16_t>(obj, 0x5C) = s;
        of<int16_t>(obj, 0x60) = 0;
    } else {
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 24;
    }
    mem.at<int32_t>(G::kind70_slot_state + of<uint32_t>(obj, 0x31C) * 4) = of<int16_t>(obj, 0x5C);
}

// 0x8DD80 / PC 0x4FF6E0 (types 59, 92, 93, Moody): as the monkeys (kind70_think_monkey), but rand & 7: 0 the turn,
// 1 pending, 2 / 3 the wall leap (29), 4 / 6 along the wall in x (6) with AI flag bit 2, 5 / 7 in z (7) with bit 1,
// else 29
template <class P> void Game<P>::kind70_think_moody(uint32_t obj)
{
    if (of<uint32_t>(obj, 0x330) == 0 && !(kind70_camera_distance(obj) >= 60.0f)) {
        const uint32_t r = rand() & 7;
        int16_t st = of<int16_t>(obj, 0x5C);
        const uint8_t ai = of<uint8_t>(obj, 0x2E5);
        if (r == 0) {
            of<uint32_t>(obj, 0x330) = 0;
            st = 9;
        } else if (r == 1) {
            of<uint32_t>(obj, 0x330) = 1;
        } else if ((r == 4 || r == 6) && ((ai >> 2) & 1)) {
            of<uint32_t>(obj, 0x330) = 0;
            st = 6;
        } else if ((r == 5 || r == 7) && ((ai >> 1) & 1)) {
            st = 7;
            of<uint32_t>(obj, 0x330) = 0;
        } else {
            of<uint32_t>(obj, 0x330) = 0;
            st = 29;
        }
        of<int16_t>(obj, 0x5C) = st;
        if (st != 3)
            obj_stop(obj);
        mem.at<int32_t>(G::kind70_slot_state + of<uint32_t>(obj, 0x31C) * 4) = of<int16_t>(obj, 0x5C);
        of<int16_t>(obj, 0x60) = 0;
    }
    if (!(kind70_camera_distance(obj) >= 25.0f)) {
        of<int16_t>(obj, 0x60) = 0;
        of<uint32_t>(obj, 0x330) = 0;
        of<int16_t>(obj, 0x5C) = 4;
        mem.at<int32_t>(G::kind70_slot_state + of<uint32_t>(obj, 0x31C) * 4) = 4;
    }
}

// the think routines of Drake (0x8D960 / PC 0x4FF30C) and kageta (0x8DF40 / PC 0x4FF870), the same code for the
// knife throwers (types 0x25, 0x44, 0x57): with no move pending and the camera nearer than 60: rand & 7: 0 the turn
// (9); kageta: 1 / 2 pending; else, when too many throw at once (kind70_attack_turn + 1 above 1 for Drake, 2 for the
// others) or no player is in, the turn; else the throwing hand (+0x320: -1, 0 when node 8 holds the type's knife model,
// 1 when node 5 does), then near (25) or no knife: pending, else the throw (state 23); the slot's state. Nearer than
// 25: state 4, nothing pending
template <class P> void Game<P>::kind70_think_knife(uint32_t obj, bool kageta)
{
    if (of<uint32_t>(obj, 0x330) == 0 && !(kind70_camera_distance(obj) >= 60.0f)) {
        const uint32_t r = rand() & 7;
        const uint32_t slot = G::kind70_slot_state + of<uint32_t>(obj, 0x31C) * 4;
        auto set = [&](int16_t s) {
            of<int16_t>(obj, 0x60) = 0;
            of<int16_t>(obj, 0x5C) = s;
        };
        if (r == 0) {
            set(9);
        } else if (kageta && r <= 2) {
            of<uint32_t>(obj, 0x330) = 1;
        } else {
            const int32_t turn = mem.at<int32_t>(G::kind70_attack_turn) + 1;
            if (turn > (of<int16_t>(obj, 0xCC) == 0x25 ? 1 : 2) || mem.at<uint16_t>(G::players_in_game) == 0) {
                set(9);
            } else {
                of<int32_t>(obj, 0x320) = -1;
                uint32_t m8 = 0, m5 = 0;
                switch (of<int16_t>(obj, 0xCC)) {
                case 0x25: m8 = 0x180; m5 = 0x182; break;
                case 0x44: m8 = 0x1855; m5 = 0x1856; break;
                case 0x57: m8 = 0x19FE; m5 = 0x19FF; break;
                default: break;
                }
                if (m8 != 0) {
                    if (body_node_model(obj, 8) == m8)
                        of<int32_t>(obj, 0x320) = 0;
                    else if (body_node_model(obj, 5) == m5)
                        of<int32_t>(obj, 0x320) = 1;
                }
                if (kind70_camera_distance(obj) < 25.0f || of<int32_t>(obj, 0x320) == -1)
                    of<uint32_t>(obj, 0x330) = 1;
                else
                    set(23);
            }
        }
        mem.at<int32_t>(slot) = of<int16_t>(obj, 0x5C);
    }
    if (!(kind70_camera_distance(obj) >= 25.0f)) {
        of<int16_t>(obj, 0x60) = 0;
        of<uint32_t>(obj, 0x330) = 0;
        of<int16_t>(obj, 0x5C) = 4;
    }
}
// 0x8D960 / PC 0x4FF30C (type 37, Drake)
template <class P> void Game<P>::kind70_think_drake(uint32_t obj) { kind70_think_knife(obj, false); }
// 0x8DF40 / PC 0x4FF870 (types 68, 87, kageta)
template <class P> void Game<P>::kind70_think_kageta(uint32_t obj) { kind70_think_knife(obj, true); }

// states 6 / 7 (0x8E260 / 0x8E340, PC 0x4FFADC / 0x4FFB88): the leap target on the room box's wall by the camera side
// (+0x2E9; z walls for 0 / 1 with x a third of the way to the camera, x walls for 2 / 3), the wall motion, then state
// 8 (27 with AI flag bit 5); `swap` picks the other wall of each pair (state 7). A third: A / 3.0, PC * (1 / 3)
template <class P> void Game<P>::kind70_wall_move(uint32_t obj, bool swap, uint32_t motion)
{
    const int8_t side = of<int8_t>(obj, 0x2E9);
    auto third = [this](float d) {
        if (pc_path())
            return d * ffrom(0x3EAAAAABu);
        else
            return d / 3.0f;
    };
    if (side >= 0 && side < 2) {
        of<float>(obj, 0x3A8) = mem.f32(((side == 0) != swap) ? G::room_z1 : G::room_z0);
        of<float>(obj, 0x3A0) = of<float>(obj, 0x20) + third(mem.f32(G::camera_pos) - of<float>(obj, 0x20));
    } else if (side >= 2 && side < 4) {
        of<float>(obj, 0x3A0) = mem.f32(((side == 2) != swap) ? G::room_x0 : G::room_x1);
        of<float>(obj, 0x3A8) = of<float>(obj, 0x28) + third(mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28));
    }
    set_motion(obj, motion);
    of<int16_t>(obj, 0x5C) = ((of<uint8_t>(obj, 0x2E5) >> 5) & 1) ? 27 : 8;
    of<int16_t>(obj, 0x60) = 0;
}
// 0x8E260 / PC 0x4FFADC (state 6): MO 0x1A2 (sarukabe00x)
template <class P> void Game<P>::kind70_st06_wall_x(uint32_t obj) { kind70_wall_move(obj, false, 0x1A2); }
// 0x8E340 / PC 0x4FFB88 (state 7): MO 0x1A3 (sarukabe00z)
template <class P> void Game<P>::kind70_st07_wall_z(uint32_t obj) { kind70_wall_move(obj, true, 0x1A3); }

// states 8 / 27 (the leap): step 0 from frame 6: a jump arc of 17 frames to the target (+0x3A0 / +0x3A8) at the
// floor + a quarter (even rand) or half of the room height (A: / 4.0, / 2.0; PC * 0.25, * 0.5); 1: the arc until it
// lands, the timer +0x50 = 0 (state 27: the cry 0xA01225); 2: at the motion's end its frame held at 28; the timer
// runs out: state `next`
template <class P> void Game<P>::kind70_leap(uint32_t obj, bool cry, int16_t next)
{
    const int16_t s = of<int16_t>(obj, 0x60);
    if (s == 0) {
        if (of<int16_t>(obj, 0xF4) <= 5)
            return;
        const int32_t r = int32_t(rand());
        const float h = mem.f32(G::room_y1) - mem.f32(G::room_y0);
        float y;
        if (pc_path())
            y = h * ((r % 2) != 0 ? 0.5f : 0.25f) + mem.f32(G::floor_y);
        else
            y = mem.f32(G::floor_y) + h / ((r % 2) == 0 ? 4.0f : 2.0f);
        of<float>(obj, 0x3A4) = y;
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                     of<uint32_t>(obj, 0x3A0), fbits(y), of<uint32_t>(obj, 0x3A8), 17);
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    } else if (s != 1 && s != 2) {
        return;
    }
    if (of<int16_t>(obj, 0x60) == 1) {
        if (jump_arc_velocity(obj) != 1)
            return;
        of<int32_t>(obj, 0x50) = 0;
        if (cry)
            sound_cue(P::pc ? 0x32u : 0xA01225u);
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    }
    if ((of<uint8_t>(obj, 0x3C5) >> 4) & 1) {
        of<uint32_t>(obj, 0xF8) = 28;
        of<uint16_t>(obj, 0xF4) = 28;
    }
    of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) - 1;
    if (of<int32_t>(obj, 0x50) < 1) {
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = next;
    }
}
// 0x8E420 / PC 0x4FFC34 (state 8): the leap, then state 4
template <class P> void Game<P>::kind70_st08_leap(uint32_t obj) { kind70_leap(obj, false, 4); }
// 0x91DE0 / PC 0x5028F4 (state 27, Moody): the leap with the landing cry, then state 28 (along the wall)
template <class P> void Game<P>::kind70_st27_wait(uint32_t obj) { kind70_leap(obj, true, 28); }
// 0x91DD0 / PC 0x5028F0 (state 26): nothing
template <class P> void Game<P>::kind70_st26_none(uint32_t obj) { (void)obj; }

// 0x8CEB0 / PC 0x4FEC58 (set-up 6, also state 22): 0: the home point from the record (+0x368: x, y, z), the spawn
// set-up (inline on the arcade), +0x334 0, state 22, no hit reactions (+0x324 bit 4), +0x3C5 bit 2; 1: a jump arc
// home over +0x2F8 frames; 2: after +0x2F4 frames: MO 0x7C, MO 0x1B4 blended over half the jump; 3: water splashes
// (Chapter 2 event 9 above the floor, Chapter 3 event 2; once), at the jump's half MO 0x1B5 (half - 8 frames);
// 4: Chapter 2 event 9 splash; landed: the cry 0xA01225; 5: at the motion's end: reactions on, stopped, the AI
template <class P> void Game<P>::kind70_spawn6(uint32_t obj)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    auto splash9 = [&] {
        if (mem.at<uint8_t>(G::chapter) == 1 && mem.at<uint8_t>(G::event_index) == 9 &&
            !(of<float>(obj, 0x24) < mem.f32(G::floor_y)) && of<uint32_t>(obj, 0x334) == 0) {
            frog_splash_spawn(obj);
            of<uint32_t>(obj, 0x334) = 1;
        }
    };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0: {
        const uint32_t r = of<uint32_t>(obj, 0x368);
        of<uint32_t>(obj, 0x3AC) = mem.u32(r);
        of<uint32_t>(obj, 0x3B0) = mem.u32(r + 4);
        of<uint32_t>(obj, 0x3B4) = mem.u32(r + 8);
        kind70_spawn_init(obj);
        of<uint32_t>(obj, 0x334) = 0;
        of<int16_t>(obj, 0x5C) = 22;
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 0x10;
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) | 4);
        step();
    }
        [[fallthrough]];
    case 1:
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                     of<uint32_t>(obj, 0x3AC), of<uint32_t>(obj, 0x3B0), of<uint32_t>(obj, 0x3B4), of<int32_t>(obj, 0x2F8));
        step();
        [[fallthrough]];
    case 2:
        of<int32_t>(obj, 0x2F4) = of<int32_t>(obj, 0x2F4) - 1;
        if (of<int32_t>(obj, 0x2F4) > 0)
            return;
        set_motion(obj, 0x7C);
        set_motion_blend(obj, 0x1B4, 0, of<int32_t>(obj, 0x2F8) / 2);
        step();
        [[fallthrough]];
    case 3:
        splash9();
        if (mem.at<uint8_t>(G::chapter) == 2 && mem.at<uint8_t>(G::event_index) == 2 && of<uint32_t>(obj, 0x334) == 0) {
            frog_splash_spawn(obj);
            of<uint32_t>(obj, 0x334) = 1;
        }
        jump_arc_step_b(obj);
        if (of<int32_t>(obj, 0x2EC) != of<int32_t>(obj, 0x2F8) / 2)
            return;
        set_motion_blend(obj, 0x1B5, 0, of<int32_t>(obj, 0x2F8) / 2 - 8);
        step();
        [[fallthrough]];
    case 4:
        splash9();
        if (jump_arc_step_b(obj) != 1)
            return;
        sound_cue(P::pc ? 0x32u : 0xA01225u);
        step();
        [[fallthrough]];
    case 5:
        if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) == 0)
            return;
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & ~0x10u;
        obj_stop(obj);
        kind70_after_hit_state(obj);
        return;
    default:
        return;
    }
}

// 0x8F990 / PC 0x500E68 (set-up 2, also state 13, eating), by step: 0: the spawn set-up (inline on the arcade),
// +0x320 -1, +0x3C5 bit 2, +0x3C6 bit 0 cleared; slot 0: MO 0x1AF (sarukuu) at once, else MO 0x1B2 with the cry
// 0xA01324 for 30 frames; state 13, step 1: then slot 0 goes to step 10 (120 frames, event flag 0x08 set, 60 more
// frames, step 2); 2: a 78-frame jump to (x, floor, -10), MO 0x19E; 3: landed: event flag 0x1A, home here on the
// floor, camera side 2, state 2
template <class P> void Game<P>::kind70_spawn2_eating(uint32_t obj)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    auto timer = [&] { of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) - 1; return of<int32_t>(obj, 0x50) > 0; };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        kind70_spawn_init(obj);
        of<int32_t>(obj, 0x320) = -1;
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) | 4);
        of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) & ~1u);
        if (of<uint32_t>(obj, 0x31C) == 0) {
            set_motion_blend(obj, 0x1AF, 0, 8);
            of<int32_t>(obj, 0x50) = 0;
        } else {
            set_motion_blend(obj, 0x1B2, 0, 8);
            sound_cue(P::pc ? 0x6Du : 0xA01324u);
            of<int32_t>(obj, 0x50) = 30;
        }
        of<int16_t>(obj, 0x5C) = 13;
        of<int16_t>(obj, 0x60) = 1;
        [[fallthrough]];
    case 1:
        if (timer())
            return;
        if (of<uint32_t>(obj, 0x31C) == 0) {
            of<int16_t>(obj, 0x60) = 10;
            return;
        }
        step();
        [[fallthrough]];
    case 2:
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                     of<uint32_t>(obj, 0x20), mem.u32(G::floor_y), 0xC1200000u, 78);
        set_motion_blend(obj, 0x19E, 0, 8);
        step();
        [[fallthrough]];
    case 3:
        if (jump_arc_step_b(obj) != 1)
            return;
        mem.at<uint8_t>(G::event_flags + 0x1A) = 1;
        of<uint32_t>(obj, 0x3AC) = of<uint32_t>(obj, 0x20);
        of<uint32_t>(obj, 0x3B0) = mem.u32(G::floor_y);
        of<uint32_t>(obj, 0x3B4) = of<uint32_t>(obj, 0x28);
        of<int8_t>(obj, 0x2E9) = 2;
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 2;
        return;
    case 10:
        of<int32_t>(obj, 0x50) = 120;
        step();
        [[fallthrough]];
    case 11:
        if (timer())
            return;
        mem.at<uint8_t>(G::event_flags + 8) = 1;
        of<int32_t>(obj, 0x50) = 60;
        step();
        [[fallthrough]];
    case 12:
        if (timer())
            return;
        of<int16_t>(obj, 0x60) = 2;
        return;
    default:
        return;
    }
}

// 0x8FF00 / PC 0x501194 (set-up 8, also state 18, the attack from the wall), by step: 0: the spawn set-up (inline on
// the arcade); 1: MO 0x1B1; 2: a target player (pick_target_player_any; none: wait), targeted, the leap target, a jump
// arc to it over +0x2F8 frames; home = its x / z on the floor, moved 30 off by the camera side and sideways by 5
// (attack_side 1; the direction by the slot); frame 1, +0x3C5 bit 2, MO 0x1AA, state 18; 3: +0xF8 - 1, the arc;
// within 30 frames of its end: Drake's / Rubin's cry 0xA01109; 4: frame 22: the hit (player_hit side 2, and side 1 for
// players 1 / 2); landed: a 60-frame jump home, MO 0x1BD; 5: landed; 6 (after frame 56): camera side, the player
// released, on the floor, the AI
template <class P> void Game<P>::kind70_spawn8_wall_attack(uint32_t obj)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        kind70_spawn_init(obj);
        step();
        [[fallthrough]];
    case 1:
        set_motion(obj, 0x1B1);
        step();
        [[fallthrough]];
    case 2: {
        // (ours, totd8: The Typing of the Dead 0's attack window, game.h: the leap from the wall waits for its grace;
        // null in the arcade. Up to totd7 it leapt on its own timing and hit before its word's grace ran out)
        if (gAttackHold && gAttackHold(obj))
            return;
        pick_target_player_any(obj);
        if (of<uint8_t>(obj, 0x2E8) == 0xFF)
            return;
        mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(of<int8_t>(obj, 0x2E8)))) = 1;
        kind70_wall_attack_target(obj);
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                     of<uint32_t>(obj, 0x3A0), of<uint32_t>(obj, 0x3A4), of<uint32_t>(obj, 0x3A8), of<int32_t>(obj, 0x2F8));
        of<uint32_t>(obj, 0x3AC) = of<uint32_t>(obj, 0x3A0);
        of<uint32_t>(obj, 0x3B0) = mem.u32(G::floor_y);
        of<uint32_t>(obj, 0x3B4) = of<uint32_t>(obj, 0x3A8);
        const float side = mem.at<uint8_t>(G::attack_side) == 1 ? 5.0f : 0.0f;
        const bool slot0 = of<uint32_t>(obj, 0x31C) == 0;
        float& hx = of<float>(obj, 0x3AC);
        float& hz = of<float>(obj, 0x3B4);
        switch (of<int8_t>(obj, 0x2E9)) {
        case 0: hx = hx + 30.0f; hz = slot0 ? hz - side : hz + side; break;
        case 1: hx = hx - 30.0f; hz = slot0 ? hz + side : hz - side; break;
        case 2: hz = hz + 30.0f; hx = slot0 ? hx - side : hx + side; break;
        case 3: hz = hz - 30.0f; hx = slot0 ? hx + side : hx - side; break;
        default: break;
        }
        of<uint16_t>(obj, 0xF4) = 1;
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) | 4);
        of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) & ~1u);
        set_motion(obj, 0x1AA);
        of<int16_t>(obj, 0x5C) = 18;
        step();
    }
        [[fallthrough]];
    case 3: {
        of<uint32_t>(obj, 0xF8) = of<uint32_t>(obj, 0xF8) - 1;
        jump_arc_step_b(obj);
        if (of<int32_t>(obj, 0x2F0) - of<int32_t>(obj, 0x2EC) > 30)
            return;
        const uint16_t t = of<uint16_t>(obj, 0xCC);
        if (t == 0x25 || t == 0x12)
            sound_cue(P::pc ? 0x19u : 0xA01109u);
        step();
    }
        [[fallthrough]];
    case 4:
        if (of<uint16_t>(obj, 0xF4) == 22) {
            player_hit(of<int8_t>(obj, 0x2E8), 1, 2);
            const uint8_t p = of<uint8_t>(obj, 0x2E8);
            if (p == 0 || p == 1)
                player_hit(of<int8_t>(obj, 0x2E8), 1, 1);
        }
        if (jump_arc_step_b(obj) != 1)
            return;
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                     of<uint32_t>(obj, 0x3AC), mem.u32(G::floor_y), of<uint32_t>(obj, 0x3B4), 60);
        set_motion_blend(obj, 0x1BD, 0, 4);
        step();
        [[fallthrough]];
    case 5:
        if (jump_arc_step_b(obj) != 1)
            return;
        step();
        [[fallthrough]];
    case 6:
        if (of<int16_t>(obj, 0xF4) <= 56)
            return;
        camera_side_set(obj);
        mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(of<int8_t>(obj, 0x2E8)))) = 0;
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~4u);
        of<int8_t>(obj, 0x2E8) = -1;
        of<uint32_t>(obj, 0x24) = mem.u32(G::floor_y);
        kind70_after_hit_state(obj);
        return;
    default:
        return;
    }
}

// 0x909F0 / PC 0x5019D4 (set-up 10, also state 21, the stance and bite on another creature), by step: 0: the spawn
// set-up (inline on the arcade), MO 0x1F1 (z_a_kamae2a), state 21; 1: the nearest attacker (attack_slot_nearest_object,
// kept in +0x35C); with scene_count_520794 set it waits for one; MO 0x1C0; 2: none (and the count clear) or the camera
// nearer than 15: step 4; else facing it, within 10 of it: MO 0x1AE (sarukami); 3: at frame 30 the bitten one's
// +0x324 bit 1; at the motion's end: 4: camera side, state 18 step 1
template <class P> void Game<P>::kind70_spawn10_stance_bite(uint32_t obj)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        kind70_spawn_init(obj);
        set_motion_blend(obj, 0x1F1, 0, 8);
        of<int16_t>(obj, 0x5C) = 21;
        step();
        return;
    case 1: {
        const uint32_t t = attack_slot_nearest_object(obj);
        of<uint32_t>(obj, 0x35C) = t;
        if (mem.at<uint16_t>(G::scene_count_520794) != 0 && t == 0)
            return;
        set_motion_blend(obj, 0x1C0, 0, 4);
        step();
    }
        [[fallthrough]];
    case 2: {
        if ((mem.at<uint16_t>(G::scene_count_520794) == 0 && of<uint32_t>(obj, 0x35C) == 0) ||
            !(kind70_camera_distance(obj) >= 15.0f)) {
            of<int16_t>(obj, 0x60) = 4;
            return;
        }
        const uint32_t t = of<uint32_t>(obj, 0x35C);
        kind70_face(obj, of<float>(t, 0x20) - of<float>(obj, 0x20), of<float>(t, 0x24) - of<float>(obj, 0x24),
                    of<float>(t, 0x28) - of<float>(obj, 0x28));
        float d;
        if (pc_path())
            d = pc_floor_distance10(of<float>(obj, 0x20), of<float>(obj, 0x28), of<float>(t, 0x20), of<float>(t, 0x28));
        else
            d = floor_distance(of<float>(obj, 0x20), of<float>(obj, 0x28), of<float>(t, 0x20), of<float>(t, 0x28));
        if (d > 10.0f)
            return;
        set_motion_blend(obj, 0x1AE, 0, 8);
        step();
    }
        [[fallthrough]];
    case 3:
        if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) == 0) {
            if (of<uint16_t>(obj, 0xF4) == 30) {
                const uint32_t t = of<uint32_t>(obj, 0x35C);
                of<uint32_t>(t, 0x324) = of<uint32_t>(t, 0x324) | 2;
            }
            return;
        }
        step();
        [[fallthrough]];
    case 4:
        camera_side_set(obj);
        of<int16_t>(obj, 0x5C) = 18;
        of<int16_t>(obj, 0x60) = 1;
        return;
    default:
        return;
    }
}

// 0x91960 / PC 0x502600 (set-up 11, also state 25, hanging upside down), by step: 0: the spawn set-up (inline on the
// arcade), MO 0x63 (d_sakasa), home from the record (+0x368), +0x3C5 bit 2, +0x3C6 bit 0 cleared, facing the camera,
// state 25, no hit reactions; 1: the cry 0xA01263 at count 50, waits for +0x2F4 to run out; 2: MO 0x62 (d_sakaoli),
// gravity; 3: at its end MO 0x78; on 0x78 the frame held at 1 (bit 26); reaching the floor: stopped; 4: the cry
// 0xA01224 at frame 8; at the motion's end reactions on, MO 0x1C0; 5: facing home, within 1.5 of it in x and z:
// facing the camera, the AI
template <class P> void Game<P>::kind70_spawn11_hanging(uint32_t obj)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0: {
        const uint32_t r = of<uint32_t>(obj, 0x368);
        kind70_spawn_init(obj);
        set_motion(obj, 0x63);
        of<uint32_t>(obj, 0x3AC) = mem.u32(r);
        of<uint32_t>(obj, 0x3B0) = mem.u32(r + 4);
        of<uint32_t>(obj, 0x3B4) = mem.u32(r + 8);
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) | 4);
        of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) & ~1u);
        kind70_face_camera(obj);
        of<int16_t>(obj, 0x5C) = 25;
        of<int16_t>(obj, 0x60) = 1;
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 0x10;
    }
        [[fallthrough]];
    case 1:
        if (of<int32_t>(obj, 0x2F4) == 50)
            sound_cue(P::pc ? 0x59u : 0xA01263u);
        of<int32_t>(obj, 0x2F4) = of<int32_t>(obj, 0x2F4) - 1;
        if (of<int32_t>(obj, 0x2F4) > 0)
            return;
        step();
        [[fallthrough]];
    case 2:
        set_motion_blend(obj, 0x62, 0, 4);
        of<uint32_t>(obj, 0x88) = 0xBCDF0123u;
        step();
        [[fallthrough]];
    case 3:
        if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) && of<uint16_t>(obj, 0xD0) == 0x62)
            set_motion_blend(obj, 0x78, 0, 4);
        if (of<uint16_t>(obj, 0xD0) == 0x78 && of<int16_t>(obj, 0xF4) > 1 && ((of<uint32_t>(obj, 0x58) >> 26) & 1) == 0) {
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x04000000u;
            of<int16_t>(obj, 0xF4) = 1;
        }
        if (of<float>(obj, 0x24) + of<float>(obj, 0x7C) > mem.f32(G::floor_y))
            return;
        obj_stop(obj);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x04000000u;
        step();
        [[fallthrough]];
    case 4:
        if (of<uint16_t>(obj, 0xF4) == 8)
            sound_cue(P::pc ? 0x31u : 0xA01224u);
        if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) == 0)
            return;
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & ~0x10u;
        set_motion_blend(obj, 0x1C0, 0, 4);
        step();
        [[fallthrough]];
    case 5: {
        const float dx = of<float>(obj, 0x3AC) - of<float>(obj, 0x20);
        const float dz = of<float>(obj, 0x3B4) - of<float>(obj, 0x28);
        kind70_face(obj, dx, 0.0f, dz);
        if (std::fabs(dx) > 1.5f || !(std::fabs(dz) <= 1.5f))
            return;
        kind70_face_camera(obj);
        kind70_after_hit_state(obj);
        return;
    }
    default:
        return;
    }
}

// 0x902F0 / PC 0x5014F8 (set-ups 3 / 4, also state 19), by step: 0: the spawn set-up (inline on the arcade),
// +0x58 bit 14, home from the record (+0x368), state 19, +0x3C5 bit 2, +0x3C6 bit 0 cleared; 1: a jump home over
// +0x2F8 frames, MO 0x19E (monkeys: the cry 0xA01323); 2: landed: facing the camera (y 0), bit 14 cleared, camera
// side, the leap target +0x3A0 / +0x3A8 moved 20 by the side; action 3: MO 0x1BD at once, else MO 0x1B2 for 60
// frames; the cry 0xA01225; 3: after the timer (action 4: MO 0x1BE); 4 (from frame 13): a 36-frame jump to the leap
// target on the floor; 5: landed: the cry; 6 (after frame 56): MO 0x19E for 80 frames; 7: then the AI
template <class P> void Game<P>::kind70_spawn3(uint32_t obj)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0: {
        const uint32_t r = of<uint32_t>(obj, 0x368);
        kind70_spawn_init(obj);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x4000u;
        of<uint32_t>(obj, 0x3AC) = mem.u32(r);
        of<uint32_t>(obj, 0x3B0) = mem.u32(r + 4);
        of<int16_t>(obj, 0x5C) = 19;
        of<int16_t>(obj, 0x60) = 1;
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) | 4);
        of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) & ~1u);
        of<uint32_t>(obj, 0x3B4) = mem.u32(r + 8);
    }
        [[fallthrough]];
    case 1:
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                     of<uint32_t>(obj, 0x3AC), of<uint32_t>(obj, 0x3B0), of<uint32_t>(obj, 0x3B4), of<int32_t>(obj, 0x2F8));
        set_motion_blend(obj, 0x19E, 0, 8);
        if (of<uint16_t>(obj, 0xCC) == 1)
            sound_cue(P::pc ? 0x6Cu : 0xA01323u);
        step();
        [[fallthrough]];
    case 2:
        if (jump_arc_step_b(obj) != 1)
            return;
        kind70_face(obj, mem.f32(G::camera_pos) - of<float>(obj, 0x20), 0.0f, mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28));
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x4000u;
        camera_side_set(obj);
        switch (of<int8_t>(obj, 0x2E9)) {
        case 0: of<float>(obj, 0x3A0) = of<float>(obj, 0x3A0) + 20.0f; break;
        case 1: of<float>(obj, 0x3A0) = of<float>(obj, 0x3A0) - 20.0f; break;
        case 2: of<float>(obj, 0x3A8) = of<float>(obj, 0x3A8) + 20.0f; break;
        case 3: of<float>(obj, 0x3A8) = of<float>(obj, 0x3A8) - 20.0f; break;
        default: break;
        }
        if (of<uint16_t>(obj, 0x64) == 3) {
            set_motion(obj, 0x1BD);
            of<int32_t>(obj, 0x50) = 0;
        } else {
            set_motion(obj, 0x1B2);
            of<int32_t>(obj, 0x50) = 60;
        }
        sound_cue(P::pc ? 0x32u : 0xA01225u);
        step();
        [[fallthrough]];
    case 3:
        of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) - 1;
        if (of<int32_t>(obj, 0x50) > 0)
            return;
        if (of<uint16_t>(obj, 0x64) == 4)
            set_motion(obj, 0x1BE);
        step();
        [[fallthrough]];
    case 4:
        if (of<int16_t>(obj, 0xF4) <= 12)
            return;
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                     of<uint32_t>(obj, 0x3A0), mem.u32(G::floor_y), of<uint32_t>(obj, 0x3A8), 36);
        step();
        [[fallthrough]];
    case 5:
        if (jump_arc_step_b(obj) != 1)
            return;
        sound_cue(P::pc ? 0x32u : 0xA01225u);
        step();
        [[fallthrough]];
    case 6:
        if (of<int16_t>(obj, 0xF4) <= 56)
            return;
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~4u);
        set_motion(obj, 0x19E);
        of<int32_t>(obj, 0x50) = 80;
        step();
        [[fallthrough]];
    case 7:
        of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) - 1;
        if (of<int32_t>(obj, 0x50) >= 0)
            return;
        kind70_after_hit_state(obj);
        return;
    default:
        return;
    }
}

// 0x8FC30 / PC 0x501000 (set-up 5, also state 15, waiting): 0: the spawn set-up (inline on the arcade), not shootable,
// MO 0x1B2 (sarumachi22), home = here, state 15; 1: waits for its event's flag (event 6: flag 0x17, 7: 0x30, 15 / 16:
// 0x3C), then shootable and on to the AI
template <class P> void Game<P>::kind70_spawn5_waiting(uint32_t obj)
{
    const int16_t s = of<int16_t>(obj, 0x60);
    if (s == 0) {
        kind70_spawn_init(obj);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
        set_motion(obj, 0x1B2);
        of<uint32_t>(obj, 0x3AC) = of<uint32_t>(obj, 0x20);
        of<uint32_t>(obj, 0x3B0) = of<uint32_t>(obj, 0x24);
        of<uint32_t>(obj, 0x3B4) = of<uint32_t>(obj, 0x28);
        of<int16_t>(obj, 0x5C) = 15;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    } else if (s != 1) {
        return;
    }
    const uint8_t ev = mem.at<uint8_t>(G::event_index);
    if (ev == 6 && mem.at<uint8_t>(G::event_flags + 0x17) != 1)
        return;
    if (ev == 7 && mem.at<uint8_t>(G::event_flags + 0x30) != 1)
        return;
    if ((ev == 15 || ev == 16) && mem.at<uint8_t>(G::event_flags + 0x3C) != 1)
        return;
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 1u;
    kind70_after_hit_state(obj);
}

// 0x8E530 / PC 0x4FFD38 (state 9, the turn): step 0: +0x3C5 bit 2, MO 0x1BD (saruturn01a) over 4 frames; at the
// motion's end the cry 0xA01225, back to the AI, bit 2 cleared
template <class P> void Game<P>::kind70_st09_turn(uint32_t obj)
{
    const int16_t s = of<int16_t>(obj, 0x60);
    if (s == 0) {
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) | 4);
        set_motion_blend(obj, 0x1BD, 0, 4);
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    } else if (s != 1) {
        return;
    }
    if ((of<uint8_t>(obj, 0x3C5) >> 4) & 1) {
        sound_cue(P::pc ? 0x32u : 0xA01225u);
        kind70_after_hit_state(obj);
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~4u);
    }
}

// the heading toward the camera (dx, dy, dz)
template <class P> void Game<P>::kind70_face_camera(uint32_t obj)
{
    kind70_face(obj, mem.f32(G::camera_pos) - of<float>(obj, 0x20), mem.f32(G::camera_pos + 4) - of<float>(obj, 0x24),
                mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28));
}

// 0x8E5A0 / PC 0x4FFDC4 (state 10, the stance): step 0: stopped; Drake / Rubin MO 0x87 for 45 frames with the cry
// 0xA01263; monkeys / kage 0x1B2 (even rand) or 0x1A1, Moody 0x14B, both until the motion ends (timer 999); then the
// monkeys' cries 0xA01323 (MO 0x1A1 frame 9, 0x1B2 frame 30); at the motion's end or the timer's: back to the AI.
// Always facing the camera
template <class P> void Game<P>::kind70_st10_stance(uint32_t obj)
{
    const int16_t s = of<int16_t>(obj, 0x60);
    if (s == 0 || s == 1) {
        if (s == 0) {
            obj_stop(obj);
            int16_t t = of<int16_t>(obj, 0xCC);
            if (t == 0x25 || t == 0x12) {
                set_motion_blend(obj, 0x87, 0, 4);
                of<int32_t>(obj, 0x50) = 45;
                sound_cue(P::pc ? 0x59u : 0xA01263u);
            }
            t = of<int16_t>(obj, 0xCC);
            if (t == 1 || t == 9 || t == 0x26) {
                const int32_t r = int32_t(rand());
                set_motion_blend(obj, (r % 2) == 0 ? 0x1B2 : 0x1A1, 0, 4);
                of<int32_t>(obj, 0x50) = 999;
            }
            t = of<int16_t>(obj, 0xCC);
            if (t == 0x3B || t == 0x5C || t == 0x5D) {
                set_motion_blend(obj, 0x14B, 0, 4);
                of<int32_t>(obj, 0x50) = 999;
            }
            of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        }
        const int16_t t = of<int16_t>(obj, 0xCC);
        if (t == 1 || t == 9 || t == 0x26) {
            if (of<uint16_t>(obj, 0xD0) == 0x1A1 && of<uint16_t>(obj, 0xF4) == 9)
                sound_cue(P::pc ? 0x6Cu : 0xA01323u);
            if (of<uint16_t>(obj, 0xD0) == 0x1B2 && of<uint16_t>(obj, 0xF4) == 30)
                sound_cue(P::pc ? 0x6Cu : 0xA01323u);
        }
        if ((of<uint8_t>(obj, 0x3C5) >> 4) & 1) {
            kind70_after_hit_state(obj);
        } else {
            of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) - 1;
            if (of<int32_t>(obj, 0x50) < 1)
                kind70_after_hit_state(obj);
        }
    }
    kind70_face_camera(obj);
}

// a point in front of the target player (+0x2E8: player 1 x -`sx`, player 2 x +`sx`, y `sy`; one player (the word
// `oneVar` is 1): x 0; neither: (0, 0)), scaled by `depth` / 496 at depth `depth` and taken through the camera matrix
// (A: coprocessor 0x74 slot 1, 0x1A, 0x06; PC: pc_matrix_push, the stored matrix 1, 0x4FBF30, popped; x 1 / 496) to
// +0x3A0..+0x3A8
template <class P> void Game<P>::kind70_aim_point(uint32_t obj, float sx, float sy, float depth, uint32_t oneVar)
{
    float x = 0.0f, y = 0.0f;
    if (of<uint8_t>(obj, 0x2E8) == 0) {
        x = -sx;
        y = sy;
    }
    if (of<uint8_t>(obj, 0x2E8) == 1) {
        x = sx;
        y = sy;
    }
    if (mem.at<uint16_t>(oneVar) == 1) {
        x = 0.0f;
        y = sy;
    }
    if (pc_path()) {
        const float px = (x * depth) * ffrom(0x3B042108u);
        const float py = (y * depth) * ffrom(0x3B042108u);
        pc_matrix_push();
        pcm_load_camera();
        float ox, oy, oz;
        pc_matrix_transform(px, py, depth, ox, oy, oz);
        of<float>(obj, 0x3A0) = ox;
        of<float>(obj, 0x3A4) = oy;
        of<float>(obj, 0x3A8) = oz;
        pcm_pop();
    } else {
        const float px = (x * depth) / 496.0f;
        const float py = (y * depth) / 496.0f;
        Tgp::Out o, none;
        const uint32_t one[1] = {1};
        copro(0x74, one, 1, none);
        const uint32_t v[3] = {fbits(px), fbits(py), fbits(depth)};
        copro(0x1A, v, 3, o);
        of<uint32_t>(obj, 0x3A0) = o.w[0];
        of<uint32_t>(obj, 0x3A4) = o.w[1];
        of<uint32_t>(obj, 0x3A8) = o.w[2];
        copro(0x06, nullptr, 0, none);
    }
}

// 0x92C40 / PC 0x5035F4: the leap target 124 to the side, 350 down at depth 8 (one player: players_in_game)
template <class P> void Game<P>::kind70_wall_attack_target(uint32_t obj)
{
    kind70_aim_point(obj, 124.0f, -350.0f, 8.0f, G::players_in_game);
}
// 0x914F0 / PC 0x502298: the knife's target, 60 to the side, 32 up at depth 4 (one player: players_in_game_b)
template <class P> void Game<P>::kind70_knife_fly_sub(uint32_t obj)
{
    kind70_aim_point(obj, 60.0f, 32.0f, 4.0f, G::players_in_game_b);
}
// 0x92930 / PC 0x503314: Moody's projectile target, as the knife's
template <class P> void Game<P>::moody_projectile_target(uint32_t obj)
{
    kind70_aim_point(obj, 60.0f, 32.0f, 4.0f, G::players_in_game_b);
}

// 0x8E790 / PC 0x4FFFA0 (state 4, on the wall): step 0: the wall motion by the slot's state (7: MO 0x1AC, 6: 0x1AD
// over 4 frames, else 0x1B1 (even rand) or 0x1A1), stopped; 1: a target player (pick_target_player; none: wait), it is
// targeted, the leap target (kind70_wall_attack_target); 2: the attack motion (6: 0x1A9, 7: 0x1AA, else facing the
// camera 0x1BB), the cry 0xA0125B, state 5 (17 for action 7)
template <class P> void Game<P>::kind70_st04_wall(uint32_t obj)
{
    const int16_t s = of<int16_t>(obj, 0x60);
    const uint32_t slot = G::kind70_slot_state + of<uint32_t>(obj, 0x31C) * 4;
    if (s == 0) {
        if (mem.at<int32_t>(slot) == 7)
            set_motion_blend(obj, 0x1AC, 0, 4);
        if (mem.at<int32_t>(slot) == 6)
            set_motion_blend(obj, 0x1AD, 0, 4);
        if (mem.at<int32_t>(slot) != 7 && mem.at<int32_t>(slot) != 6) {
            const int32_t r = int32_t(rand());
            set_motion(obj, (r % 2) == 0 ? 0x1B1 : 0x1A1);
        }
        obj_stop(obj);
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    } else if (s != 1 && s != 2) {
        return;
    }
    if (of<int16_t>(obj, 0x60) == 1) {
        pick_target_player(obj);
        if (of<uint8_t>(obj, 0x2E8) == 0xFF)
            return;
        mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(of<int8_t>(obj, 0x2E8)))) = 1;
        kind70_wall_attack_target(obj);
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    }
    switch (mem.at<int32_t>(slot)) {
    case 6: set_motion(obj, 0x1A9); break;
    case 7: set_motion(obj, 0x1AA); break;
    default:
        kind70_face_camera(obj);
        set_motion(obj, 0x1BB);
        break;
    }
    sound_cue(P::pc ? 0x55u : 0xA0125Bu);
    of<int16_t>(obj, 0x5C) = of<int16_t>(obj, 0x64) == 7 ? 17 : 5;
    of<int16_t>(obj, 0x60) = 0;
}

// 0x8E9C0 / PC 0x500158 (state 5, the leap and bite), by step: 0 (from frame 8): a 23-frame jump arc to the leap
// target (+0x3A0); 1: from the wall (slot state 6 / 7) the bite at frame 27 (player_hit side 0), landed: MO 0x1AE;
// 2: otherwise the bite now (side 1); the jump back (49 frames) to +0x394 / the floor / +0x39C, MO 0x19A; 3: landed:
// the cry, the player released, on the floor; Rubin, Drake, kageta: step 7; else MO 0x1BD (the turn), 4 (from frame
// 13): a 36-frame jump to the home point +0x3AC; 5: landed: the cry; 6: after frame 56: on the floor, state 10.
// 7: MO 0x76, +0x3C5 bit 3, on the floor; 8: the cry at the motion's end; frames 8-26: walking home (the unit vector
// toward +0x3AC / +0x3B4 x 4, A: coprocessor 0x0C; PC: 1 / the table square root), facing away from it; within 4: at
// home, 9: at the motion's end facing the camera (y 0), the cry, state 10
template <class P> void Game<P>::kind70_st05_bite(uint32_t obj)
{
    const uint32_t slot = G::kind70_slot_state + of<uint32_t>(obj, 0x31C) * 4;
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    auto targetOk = [&] { const uint8_t t = of<uint8_t>(obj, 0x2E8); return t == 0 || t == 1; };
    auto landed = [&] { return jump_arc_velocity(obj) == 1; };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        if (of<int16_t>(obj, 0xF4) <= 7)
            return;
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                     of<uint32_t>(obj, 0x3A0), of<uint32_t>(obj, 0x3A4), of<uint32_t>(obj, 0x3A8), 23);
        step();
        [[fallthrough]];
    case 1:
        if ((mem.at<int32_t>(slot) == 6 || mem.at<int32_t>(slot) == 7) && of<uint16_t>(obj, 0xF4) == 27 && targetOk())
            player_hit(of<int8_t>(obj, 0x2E8), 1, 0);
        if (!landed())
            return;
        set_motion_blend(obj, 0x1AE, 0, 4);
        step();
        [[fallthrough]];
    case 2:
        if (mem.at<int32_t>(slot) != 6 && mem.at<int32_t>(slot) != 7 && targetOk())
            player_hit(of<int8_t>(obj, 0x2E8), 1, 1);
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                     of<uint32_t>(obj, 0x394), mem.u32(G::floor_y), of<uint32_t>(obj, 0x39C), 49);
        set_motion_blend(obj, 0x19A, 0, 4);
        step();
        [[fallthrough]];
    case 3: {
        if (!landed())
            return;
        sound_cue(P::pc ? 0x32u : 0xA01225u);
        mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(of<int8_t>(obj, 0x2E8)))) = 0;
        of<int8_t>(obj, 0x2E8) = -1;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0x7FFFFFFFu;
        of<uint32_t>(obj, 0x24) = mem.u32(G::floor_y);
        const int16_t t = of<int16_t>(obj, 0xCC);
        if (t == 0x25 || t == 0x12 || t == 0x44 || t == 0x57) {
            of<int16_t>(obj, 0x60) = 7;
            return;
        }
        set_motion(obj, 0x1BD);
        step();
    }
        [[fallthrough]];
    case 4:
        if (of<int16_t>(obj, 0xF4) <= 12)
            return;
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                     of<uint32_t>(obj, 0x3AC), of<uint32_t>(obj, 0x3B0), of<uint32_t>(obj, 0x3B4), 36);
        step();
        [[fallthrough]];
    case 5:
        if (!landed())
            return;
        sound_cue(P::pc ? 0x32u : 0xA01225u);
        step();
        [[fallthrough]];
    case 6:
        if (of<int16_t>(obj, 0xF4) <= 56)
            return;
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~4u);
        of<uint32_t>(obj, 0x24) = mem.u32(G::floor_y);
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 10;
        return;
    case 7:
        set_motion_blend(obj, 0x76, 0, 4);
        of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) & ~1u);
        of<uint8_t>(obj, 0x3C5) = uint8_t((of<uint8_t>(obj, 0x3C5) & ~4u) | 8u);
        step();
        of<uint32_t>(obj, 0x24) = mem.u32(G::floor_y);
        [[fallthrough]];
    case 8: {
        if ((of<uint8_t>(obj, 0x3C5) >> 4) & 1)
            sound_cue(P::pc ? 0x32u : 0xA01225u);
        const int16_t f = of<int16_t>(obj, 0xF4);
        if (f <= 7 || f > 26) {
            of<uint32_t>(obj, 0x78) = 0;
            of<uint32_t>(obj, 0x80) = 0;
            return;
        }
        float dx, dz;
        if (pc_path()) {
            const float hx = of<float>(obj, 0x3AC) - of<float>(obj, 0x20), y = of<float>(obj, 0x24);
            const float hz = of<float>(obj, 0x3B4) - of<float>(obj, 0x28);
            const float inv = 1.0f / pc_fast_sqrt(hx * hx + y * y + hz * hz);
            of<float>(obj, 0x78) = hx * inv;
            of<float>(obj, 0x7C) = y * inv;
            of<float>(obj, 0x80) = hz * inv;
            dx = of<float>(obj, 0x20) - of<float>(obj, 0x3AC);
            dz = of<float>(obj, 0x28) - of<float>(obj, 0x3B4);
            uint32_t yaw = 0, pitch = 0;
            pc_vec_angles10(dx, of<float>(obj, 0x24), dz, yaw, pitch);
            of<uint32_t>(obj, 0x30) = yaw;
        } else {
            Tgp::Out o;
            const uint32_t v[3] = {fbits(of<float>(obj, 0x3AC) - of<float>(obj, 0x20)), of<uint32_t>(obj, 0x24),
                                   fbits(of<float>(obj, 0x3B4) - of<float>(obj, 0x28))};
            copro(0x0C, v, 3, o);
            of<uint32_t>(obj, 0x78) = o.w[0];
            of<uint32_t>(obj, 0x7C) = o.w[1];
            of<uint32_t>(obj, 0x80) = o.w[2];
            dx = of<float>(obj, 0x20) - of<float>(obj, 0x3AC);
            dz = of<float>(obj, 0x28) - of<float>(obj, 0x3B4);
            of<uint32_t>(obj, 0x30) = yaw_toward_a(dx, of<float>(obj, 0x24), dz);
        }
        of<uint32_t>(obj, 0x7C) = 0;
        of<float>(obj, 0x78) = of<float>(obj, 0x78) * 4.0f;
        of<float>(obj, 0x80) = of<float>(obj, 0x80) * 4.0f;
        if (std::fabs(dx) > 4.0f || std::fabs(dz) > 4.0f)
            return;
        of<uint32_t>(obj, 0x78) = 0;
        of<uint32_t>(obj, 0x20) = of<uint32_t>(obj, 0x3AC);
        of<uint32_t>(obj, 0x80) = 0;
        step();
        of<uint32_t>(obj, 0x28) = of<uint32_t>(obj, 0x3B4);
    }
        [[fallthrough]];
    case 9:
        if ((of<uint8_t>(obj, 0x3C5) >> 4) & 1) {
            kind70_face(obj, mem.f32(G::camera_pos) - of<float>(obj, 0x20), 0.0f,
                        mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28));
            sound_cue(P::pc ? 0x32u : 0xA01225u);
            of<int16_t>(obj, 0x60) = 0;
            of<int16_t>(obj, 0x5C) = 10;
            of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) & ~1u);
            of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~8u);
        }
        return;
    default:
        return;
    }
}

// 0x8EEE0 / PC 0x500628 (state 17, the bite that holds on; action 7), by step: 0 (from frame 6): a 23-frame jump arc
// to the leap target, +0x58 bit 31; 1: landed: MO 0x1AE over 8 frames, 25 frames; 2: then the bite (player_hit side
// 1); 3: held at the leap target (taken again each frame) for one more frame, then the 49-frame jump home (+0x3AC),
// MO 0x19A; 4: landed: bit 31 cleared, the player released, at the home height, state 4
template <class P> void Game<P>::kind70_st17_bite_b(uint32_t obj)
{
    auto step = [&] { of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1); };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        if (of<int16_t>(obj, 0xF4) <= 5)
            return;
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                     of<uint32_t>(obj, 0x3A0), of<uint32_t>(obj, 0x3A4), of<uint32_t>(obj, 0x3A8), 23);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x80000000u;
        step();
        [[fallthrough]];
    case 1:
        if (jump_arc_velocity(obj) != 1)
            return;
        of<int32_t>(obj, 0x50) = 25;
        set_motion_blend(obj, 0x1AE, 0, 8);
        step();
        [[fallthrough]];
    case 2: {
        const int32_t t = of<int32_t>(obj, 0x50);
        of<int32_t>(obj, 0x50) = t - 1;
        if (t >= 0)
            return;
        const uint8_t p = of<uint8_t>(obj, 0x2E8);
        if (p == 0 || p == 1)
            player_hit(p, 1, 1);
        of<int32_t>(obj, 0x50) = 0;
        step();
    }
        [[fallthrough]];
    case 3: {
        kind70_wall_attack_target(obj);
        of<uint32_t>(obj, 0x20) = of<uint32_t>(obj, 0x3A0);
        of<uint32_t>(obj, 0x24) = of<uint32_t>(obj, 0x3A4);
        of<uint32_t>(obj, 0x28) = of<uint32_t>(obj, 0x3A8);
        const int32_t t = of<int32_t>(obj, 0x50);
        of<int32_t>(obj, 0x50) = t - 1;
        if (t >= 0)
            return;
        jump_arc_set(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                     of<uint32_t>(obj, 0x3AC), of<uint32_t>(obj, 0x3B0), of<uint32_t>(obj, 0x3B4), 49);
        set_motion_blend(obj, 0x19A, 0, 8);
        step();
    }
        [[fallthrough]];
    case 4:
        if (jump_arc_velocity(obj) != 1)
            return;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0x7FFFFFFFu;
        mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(of<int8_t>(obj, 0x2E8)))) = 0;
        of<int8_t>(obj, 0x2E8) = -1;
        of<uint32_t>(obj, 0x24) = of<uint32_t>(obj, 0x3B0);
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 4;
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~4u);
        return;
    default:
        return;
    }
}

} // namespace hotd::port
