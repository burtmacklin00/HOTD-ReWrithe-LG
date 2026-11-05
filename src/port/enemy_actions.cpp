// Enemies, A behaviour: the per-frame routines of the guard actions (scripted enemies set up by enemy_guard) and the
// frame helpers they share, read from the disassembly.
#include "port/game.h"

#include <cstring>

namespace hotd::port {

// 0x1D820 (enemy): hit type = its type, hit class / damage cleared, then this frame's part hits
template <class P> void Game<P>::enemy_frame_begin(uint32_t obj)
{
    const int32_t type = mem.at<Enemy>(obj).type;
    mem.at<uint8_t>(G::hit_class) = 0;
    mem.at<int16_t>(G::hit_damage) = 0;
    mem.set32(G::hit_type, uint32_t(type));
    enemy_part_hits(obj);
}

// 0x1D850 (enemy): shadow (draw_flags bit 6: at its own y, else the floor), draw, motion frame counters
template <class P> void Game<P>::enemy_frame_end(uint32_t obj)
{
    Enemy& e = mem.at<Enemy>(obj);
    if ((e.draw_flags >> 6) & 1)
        shadow_setup(e.pos[1]);
    else
        shadow_setup(mem.f32(G::floor_y));
    object_draw(obj);
    Enemy& f = mem.at<Enemy>(obj);
    if ((f.motion_flags2 >> 5) & 1)
        f.motion2_time = f.motion2_time + 1;
    f.anim_time = f.anim_time + 1;
}

// the enemy leaves (actions 1 and 20 when their delay changed): attack slot, counts, parts, task_exit
template <class P> void Game<P>::guard_leave(uint32_t obj)
{
    Enemy& e = mem.at<Enemy>(obj);
    if (uint8_t(e.attack_slot) != 0xFF) {
        mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(e.attack_slot)) * 20) = 0;
        e.attack_slot = -1;
    }
    mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
    body_free_parts(obj);
    task_exit();
}

// 0x55FA0 (action 1, scenery): drawn only; leaves once its delay differs from the guard delay (0x520704)
template <class P> void Game<P>::enemy_act1_scenery(uint32_t obj)
{
    enemy_frame_end(obj);
    if (mem.at<Enemy>(obj).delay == int32_t(mem.at<int8_t>(G::scene_number)))
        return;
    guard_leave(obj);
}

// 0x55F40 (action 2): hits; while shootable walks to the record's point (turn 0x8000, within 8: ENGuardZombieRunAfter-
// Researcher next)
template <class P> void Game<P>::enemy_act2_approach_target(uint32_t obj)
{
    const uint32_t rec = mem.u32(obj + ob(0x368));
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    if (walk_to(obj, mem.f32(rec + 4), mem.f32(rec + 0xC), 0x8000, 8.0f) != 0)
        mem.at<Enemy>(obj).routine = F::ENGuardZombieRunAfterResearcher;
    enemy_frame_end(obj);
    depth_list_add(obj);
}

// 0x55CF0 (action 3): unless the camera is within 10, walks to its attack position (step 0.5); then becomes the standard
// enemy (state 1, sub-state 1, action 1, blend 8)
template <class P> void Game<P>::enemy_act3_start_variant1(uint32_t obj)
{
    Enemy& e = mem.at<Enemy>(obj);
    if (pc_path()) {
        const float dx = mem.f32(G::camera_pos) - e.pos[0], dz = mem.f32(G::camera_pos + 8) - e.pos[2];
        if (pc_fast_sqrt5(dx * dx + dz * dz) >= 10.0f)
            walk_to_attack_position(obj, 0.5f);
    } else {
        float d = floor_distance(e.pos[0], e.pos[2], mem.f32(G::camera_pos), mem.f32(G::camera_pos + 8));
        if (!(d < 10.0f))
            walk_to_attack_position(obj, 0.5f);
    }
    enemy_frame_end(obj);
    Enemy& f = mem.at<Enemy>(obj);
    f.routine = F::enemy_main;
    f.state = 1;
    f.substate = 1;
    f.action = 1;
    uint32_t row = mem.u32(G::enemy_motion_lists + uint32_t(int32_t(f.type)) * 4);
    uint32_t list = mem.u32(row + 4);
    set_motion_blend(obj, mem.u32(list + uint32_t(int32_t(f.substate)) * 8), 0, 8);
    enemy_target_distance(obj);
}

// 0x560E0 (action 4): nothing
template <class P> void Game<P>::enemy_act4_inert(uint32_t) {}

// 0x55DE0 (action 5): drawn; when the camera stops, the standard enemy walking (state 1, sub-state 2, action 0)
template <class P> void Game<P>::enemy_act5_wait_stop(uint32_t obj)
{
    enemy_frame_end(obj);
    if (mem.u32(G::camera_stopped) != 2)
        return;
    Enemy& e = mem.at<Enemy>(obj);
    e.routine = F::enemy_main;
    e.state = 1;
    e.substate = 2;
    e.action = 0;
    e.walk_variant = 1;
    uint32_t row = mem.u32(G::enemy_motion_lists + uint32_t(int32_t(e.type)) * 4);
    uint32_t list = mem.u32(row);
    set_motion(obj, mem.u32(list + uint32_t(int32_t(e.substate)) * 8));
    enemy_target_distance(obj);
}

// 0x55EC0 (action 12): hits; while shootable walks to the record's point (turn 0x100, within 1: motion 0x216 blended
// 16 / 4 and enemy_act12_frame next); depth list
template <class P> void Game<P>::enemy_act12_walk_to_spot(uint32_t obj)
{
    const uint32_t rec = mem.u32(obj + ob(0x368));
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    enemy_frame_end(obj);
    if (walk_to(obj, mem.f32(rec + 4), mem.f32(rec + 0xC), 0x100, 1.0f) != 0 && !(gPreyHold && gPreyHold(obj))) {   // (TotD 0: the carrier waits)
        set_motion_blend(obj, 0x216, 16, 4);
        mem.at<Enemy>(obj).routine = F::enemy_act12_frame;
    }
    depth_list_add(obj);
}

// 0x56020 (action 20): hits, walks after its captor (turn 0x180, 8), drawn; leaves when its delay changed; depth list
template <class P> void Game<P>::ZombieWalk2Tom(uint32_t obj)
{
    const uint32_t captor = mem.u32(obj + ob(0x390));
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    walk_to(obj, mem.f32(captor + ob(0x20)), mem.f32(captor + ob(0x28)), 0x180, 8.0f);
    enemy_frame_end(obj);
    if (mem.at<Enemy>(obj).delay != int32_t(mem.at<int8_t>(G::scene_number))) {
        guard_leave(obj);
        return;
    }
    depth_list_add(obj);
}

// 0x56250 (action 8, the opening's dummy): hidden while 0x51F9AC is set or 0x51E9CD set without 0x51E9CE (body_draw
// only); else a sound when 0x51F9AC fell to 0, walks after its captor (0x8000, 6), drawn; with 0x51E9CE it jumps to
// (-95, -1285) and opening_dummy_walk takes over; 0x5433B0 keeps 0x51F9AC.
template <class P> void Game<P>::ENGuardOpeningDummy(uint32_t obj)
{
    const int32_t type = mem.at<Enemy>(obj).type;
    const uint16_t flag = mem.at<uint16_t>(G::palette_grey_on);
    const uint32_t captor = mem.u32(obj + ob(0x390));
    if (P::pc && captor == 0)
        pc_debug_print();
    mem.set32(G::hit_type, uint32_t(type));
    if (flag != 0 || (mem.at<uint8_t>((G::event_flags + 0x8D)) != 0 && mem.at<uint8_t>((G::event_flags + 0x8E)) == 0)) {
        body_draw(obj);
    } else {
        if (mem.u32(G::opening_dummy_grey_prev) != 0 && flag == 0)
            sound_cue(P::pc ? 0x64 : 0xA01319);
        walk_to(obj, mem.f32(captor + ob(0x20)), mem.f32(captor + ob(0x28)), 0x8000, 6.0f);
        enemy_frame_end(obj);
    }
    if (mem.at<uint8_t>((G::event_flags + 0x8E)) != 0) {
        Enemy& e = mem.at<Enemy>(obj);
        e.pos[0] = -95.0f;
        e.pos[2] = -1285.0f;
        e.routine = F::opening_dummy_walk;
    }
    mem.set32(G::opening_dummy_grey_prev, uint32_t(int32_t(mem.at<int16_t>(G::palette_grey_on))));
}

// 0x53EA0 (actions 9-11): hits; while shootable runs after its prey (turn 0x8000, or 0x180 for action 2 in chapter
// index 2 event 2; within 8): on arrival the bite motion (0x264, or 0x260 with the head lost; blend 42 / 4), the slot
// of that motion's entry in table 0xC14D0 and ENGuardZombieBiteAgent next; every frame its +0x348 step, drawn. When the
// prey is gone (task magic 0, not shootable, or the chapter index 2 event 0 flag 0x51E9D8): motion 0x1F1 (60 / 8),
// enguard_scripted_frame next. Depth list except in chapter index 2 event 0 with 0x51E9D8 clear.
template <class P> void Game<P>::ENGuardZombieRunAfterResearcher(uint32_t obj)
{
    const uint32_t prey = mem.u32(obj + ob(0x390));
    if (pc_path() && prey == 0)
        pc_debug_print();
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    const bool slow = mem.at<Enemy>(obj).action == 2 && mem.at<uint8_t>(G::chapter) == 2 && mem.at<uint8_t>(G::event_index) == 2;
    if ((!pc_path() || prey != 0) && walk_to(obj, mem.f32(prey + ob(0x20)), mem.f32(prey + ob(0x28)), slow ? 0x180u : 0x8000u, 8.0f) != 0 && !(gPreyHold && gPreyHold(obj))) {
        set_motion_blend(obj, ((mem.at<Enemy>(obj).parts_lost >> 1) & 1) ? 0x260 : 0x264, 42, 4);
        guard_bite_slot(obj, F::ENGuardZombieBiteAgent);
    }
    move_along_heading(obj, mem.f32(obj + ob(0x348)));
    enemy_frame_end(obj);
    const bool flag = mem.at<uint8_t>(G::chapter) == 2 && mem.at<uint8_t>(G::event_index) == 0 && mem.at<uint8_t>((G::event_flags + 0x98)) != 0;
    if ((!pc_path() || prey != 0) && (mem.u32(prey + ob(0xC)) == 0 || (mem.at<uint8_t>(prey + ob(0x58)) & 1) == 0 || flag)) {
        Enemy& e = mem.at<Enemy>(obj);
        mem.set32(obj + ob(0x390), 0);
        e.motion_flags &= 0xF7;
        set_motion_blend(obj, 0x1F1, 60, 8);
        mem.at<Enemy>(obj).routine = F::enguard_scripted_frame;
    }
    if (!(mem.at<uint8_t>(G::chapter) == 2 && mem.at<uint8_t>(G::event_index) == 0 && mem.at<uint8_t>((G::event_flags + 0x98)) == 0))
        depth_list_add(obj);
    if (pc_path()) {
        // PC (and PC Mode): no prey: a message, action 5's frame, and when out of view the enemy leaves
        if (prey == 0) {
            pc_debug_print();
            enemy_act5_wait_stop(obj);
            Enemy& e = mem.at<Enemy>(obj);
            if (pc_view_test(e.pos[0], e.pos[1], e.pos[2], e.hit_radius) < 0.0f) {
                pc_debug_print();
                if (uint8_t(e.attack_slot) != 0xFF) {
                    mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(e.attack_slot)) * 20) = 0;
                    e.attack_slot = -1;
                }
                mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
                mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
                body_free_parts(obj);
                task_exit();
            }
        }
    }
}

// the bite set-up shared by the run-after routines: the slot of the motion just set, from table 0xC14D0 (motion, slot)
template <class P> void Game<P>::guard_bite_slot(uint32_t obj, uint32_t next)
{
    const uint32_t want = pc_bite_frame_lookup(obj);
    mem.at<Enemy>(obj).routine = next;
    mem.set32(obj + ob(0x338), want);
}

// the bite frame of the current motion: guard_bite_slots (motion, frame; 16 bytes each) searched until the motion
// (PC 0x4A323C pc_bite_frame_lookup; the arcade has it inline)
template <class P> uint32_t Game<P>::pc_bite_frame_lookup(uint32_t obj)
{
    const int32_t m = mem.at<Enemy>(obj).motion;
    uint32_t i = 0;
    while (int32_t(mem.u32(G::guard_bite_slots + i * 16)) != m)
        ++i;
    return mem.u32((G::guard_bite_slots + 4) + i * 16);
}

// prey gone (task magic 0 or not shootable): unlinked, motion 0x1F1 (60 / 8), `next` takes over
template <class P> void Game<P>::guard_prey_lost(uint32_t obj, uint32_t prey, uint32_t next)
{
    if (mem.u32(prey + ob(0xC)) != 0 && (mem.at<uint8_t>(prey + ob(0x58)) & 1) != 0)
        return;
    Enemy& e = mem.at<Enemy>(obj);
    mem.set32(obj + ob(0x390), 0);
    e.motion_flags &= 0xF7;
    set_motion_blend(obj, 0x1F1, 60, 8);
    mem.at<Enemy>(obj).routine = next;
}

// 0x54090 (action 13): as ENGuardZombieRunAfterResearcher with turn 0x8000, within 16, no chapter cases
template <class P> void Game<P>::ENGuardZombieRunAfterResearcher2(uint32_t obj)
{
    const uint32_t prey = mem.u32(obj + ob(0x390));
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    if (walk_to(obj, mem.f32(prey + ob(0x20)), mem.f32(prey + ob(0x28)), 0x8000, 16.0f) != 0 && !(gPreyHold && gPreyHold(obj))) {
        set_motion_blend(obj, ((mem.at<Enemy>(obj).parts_lost >> 1) & 1) ? 0x260 : 0x264, 42, 4);
        guard_bite_slot(obj, F::ENGuardZombieBiteAgent);
    }
    move_along_heading(obj, mem.f32(obj + ob(0x348)));
    enemy_frame_end(obj);
    guard_prey_lost(obj, prey, F::enguard_scripted_frame);
    depth_list_add(obj);
}

// 0x54290 (action 16): runs after the prey (0x8000, 16); on arrival type 63 motion 0x24A (0 / 8), others 0x264 (42 / 4),
// enemy_act16_frame next; prey gone: enemy_act16_frame_b
template <class P> void Game<P>::ENGuardZombieRunAfterResearcher_St3_00(uint32_t obj)
{
    const uint32_t prey = mem.u32(obj + ob(0x390));
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    if (walk_to(obj, mem.f32(prey + ob(0x20)), mem.f32(prey + ob(0x28)), 0x8000, 16.0f) != 0 && !(gPreyHold && gPreyHold(obj))) {
        if (uint16_t(mem.at<Enemy>(obj).type) == 0x3F)
            set_motion_blend(obj, 0x24A, 0, 8);
        else
            set_motion_blend(obj, 0x264, 42, 4);
        guard_bite_slot(obj, F::enemy_act16_frame);
    }
    move_along_heading(obj, mem.f32(obj + ob(0x348)));
    enemy_frame_end(obj);
    guard_prey_lost(obj, prey, F::enemy_act16_frame_b);
    depth_list_add(obj);
}

// 0x541A0 (action 16, biting): at the slot's frame the prey (if shootable) gets flags bit 28 and the attack voice;
// prey not shootable: unlinked, flags2 bit 0; past the frame at the motion end (or before it) with no prey: type 63
// motion 0x248 (50 / 8), others 0x1F1 (75 / 4), enemy_act16_frame_b next
template <class P> void Game<P>::enemy_act16_frame(uint32_t obj)
{
    const uint32_t prey = mem.u32(obj + ob(0x390));
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    enemy_frame_end(obj);
    if (int32_t(mem.at<Enemy>(obj).frame) == int32_t(mem.u32(obj + ob(0x338))) && (mem.at<uint8_t>(prey + ob(0x58)) & 1) != 0) {
        mem.set32(prey + ob(0x58), mem.u32(prey + ob(0x58)) | (1u << 28));
        enemy_voice(obj, 3);
    }
    if ((mem.at<uint8_t>(prey + ob(0x58)) & 1) == 0) {
        uint16_t f2 = mem.at<Enemy>(obj).flags2;
        mem.set32(obj + ob(0x390), 0);
        mem.at<Enemy>(obj).flags2 = uint16_t(f2 | 1);
    }
    Enemy& e = mem.at<Enemy>(obj);
    if (!(int32_t(e.frame) <= int32_t(mem.u32(obj + ob(0x338)))) && ((e.motion_flags >> 4) & 1) == 0) {
        depth_list_add(obj);
        return;
    }
    if (mem.u32(obj + ob(0x390)) == 0) {
        e.motion_flags &= 0xF7;
        if (uint16_t(e.type) == 0x3F)
            set_motion_blend(obj, 0x248, 50, 8);
        else
            set_motion_blend(obj, 0x1F1, 0x4B, 4);
        mem.at<Enemy>(obj).routine = F::enemy_act16_frame_b;
    }
    depth_list_add(obj);
}

// chapter index 2, event 0 with flag 0x51E971: the scripted zombie leaves the count and its attack slot and ends
// (enemy_frame_end_ch3_ev0); true if so
template <class P> bool Game<P>::guard_ch2_release(uint32_t obj)
{
    if (!(mem.at<uint8_t>(G::chapter) == 2 && mem.at<uint8_t>(G::event_index) == 0 && mem.at<uint8_t>((G::event_flags + 0x31)) != 0))
        return false;
    mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
    Enemy& e = mem.at<Enemy>(obj);
    mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(e.attack_slot)) * 20) = 0;
    e.attack_slot = -1;
    e.routine = F::enemy_frame_end_ch3_ev0;
    return true;
}

// depth list unless chapter index 2, event 0 with flag 0x51E9D8 clear
template <class P> void Game<P>::guard_depth_add(uint32_t obj)
{
    if (mem.at<uint8_t>(G::chapter) == 2 && mem.at<uint8_t>(G::event_index) == 0 && mem.at<uint8_t>((G::event_flags + 0x98)) == 0)
        return;
    depth_list_add(obj);
}

// 0x53910 (after a bite / a lost prey): hits; turns to the camera (except the opening's action 8); with draw_flags bit 6
// at the group head's height; from frame 0x56 (or with a second motion) and a player in state 5, unless above y -338 or
// in chapter index 2 event 0 with 0x51E9D8 clear: the standard AI (enemy_scripted_to_ai)
template <class P> void Game<P>::enguard_scripted_frame(uint32_t obj)
{
    const uint32_t head = mem.u32(obj + ob(0x18));
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    if (!(mem.at<uint8_t>(G::chapter) == 0 && mem.at<uint8_t>(G::event_index) == 0 && mem.at<Enemy>(obj).action == 8))
        turn_toward_camera(obj, 0x200);
    if ((mem.at<Enemy>(obj).draw_flags >> 6) & 1) {
        float y = mem.f32(head + ob(0x24));
        mem.at<Enemy>(obj).pos[1] = y;
        shadow_setup(y);
    }
    enemy_frame_end(obj);
    Enemy& e = mem.at<Enemy>(obj);
    if (int32_t(e.frame) > 0x55 || !(0 >= int32_t(mem.at<int16_t>(obj + ob(0xF6))))) {
        if (mem.u32(G::players) == 5 || mem.u32(G::players + 0xCC) == 5) {
            bool go = true;
            if (((e.draw_flags >> 6) & 1) && e.pos[1] >= -338.0f)
                go = false;
            if (go && mem.at<uint8_t>(G::chapter) == 2 && mem.at<uint8_t>(G::event_index) == 0 && mem.at<uint8_t>((G::event_flags + 0x98)) == 0)
                go = false;
            if (go) {
                e.motion_flags &= 0xFD;
                enemy_scripted_to_ai(obj);
            }
        }
    }
    if (guard_ch2_release(obj))
        return;
    guard_depth_add(obj);
}

// 0x53AA0 (biting the agent): at the slot's frame with the prey shootable: in the opening (chapter 0 event 0, action 8)
// a hit effect at matrix 1 x (+0x178 + 3, +0x17C, +0x180); prey flags bit 28, attack voice. Prey not shootable, or
// chapter index 2 event 0 with 0x51E9D8: released. Losing an arm (parts_lost & 6 = 2 / 4) swaps the bite motion
// 0x264 / 0x260 or 0x25B / 0x25A (30 / 12) and its slot. Past the frame at the motion end (or before) with no prey:
// type 63 motion 0x248, the opening's 0x1DB (draw_flags bit 7), others 0x1F1; enguard_scripted_frame next.
template <class P> void Game<P>::ENGuardZombieBiteAgent(uint32_t obj)
{
    const uint32_t prey = mem.u32(obj + ob(0x390));
    if (P::pc && prey == 0)
        pc_debug_print();
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    enemy_frame_end(obj);
    const bool opening = mem.at<uint8_t>(G::chapter) == 0 && mem.at<uint8_t>(G::event_index) == 0;
    if ((!P::pc || prey != 0) && int32_t(mem.at<Enemy>(obj).frame) == int32_t(mem.u32(obj + ob(0x338))) &&
        (mem.at<uint8_t>(prey + ob(0x58)) & 1) != 0) {
        if (pc_path()) {
          if (opening && mem.at<Enemy>(obj).action == 8) {
            // PC: stored matrix 1 (the camera) on the software stack, the point (x + 3, y, z) through it
            pc_matrix_push();
            pcm_load_camera();
            float p[3];
            pc_matrix_transform3(mem.f32(obj + ob(0x178)) + 3.0f, mem.f32(obj + ob(0x17C)), mem.f32(obj + ob(0x180)), p[0], p[1], p[2]);
            pcm_pop();
            hit_effect_at(p[0], p[1], p[2]);
          }
        } else if (opening && mem.at<Enemy>(obj).action == 8) {
            Tgp::Out o, none;
            uint32_t slot[1] = {1};
            copro(0x74, slot, 1, none);
            float x = mem.f32(obj + ob(0x178)) + 3.0f;
            uint32_t v[3];
            std::memcpy(&v[0], &x, 4);
            v[1] = mem.u32(obj + ob(0x17C));
            v[2] = mem.u32(obj + ob(0x180));
            copro(0x1A, v, 3, o);
            copro(0x06, nullptr, 0, none);
            float p[3];
            std::memcpy(p, o.w, 12);
            hit_effect_at(p[0], p[1], p[2]);
        }
        mem.set32(prey + ob(0x58), mem.u32(prey + ob(0x58)) | (1u << 28));
        enemy_voice(obj, 3);
    }
    if ((!P::pc || prey != 0) && ((mem.at<uint8_t>(prey + ob(0x58)) & 1) == 0 ||
        (mem.at<uint8_t>(G::chapter) == 2 && mem.at<uint8_t>(G::event_index) == 0 && mem.at<uint8_t>((G::event_flags + 0x98)) != 0))) {
        uint16_t f2 = mem.at<Enemy>(obj).flags2;
        mem.set32(obj + ob(0x390), 0);
        mem.at<Enemy>(obj).flags2 = uint16_t(f2 | 1);
    }
    if (guard_ch2_release(obj))
        return;
    auto swap = [&](uint32_t to) {
        set_motion_blend(obj, to, 30, 12);
        mem.set32(obj + ob(0x338), pc_bite_frame_lookup(obj));
    };
    uint32_t lost = mem.at<Enemy>(obj).parts_lost & 6u;
    uint16_t mo = uint16_t(mem.at<Enemy>(obj).motion);
    if (lost == 2 && mo == 0x264)
        swap(0x260);
    else if (lost == 2 && mo == 0x25B)
        swap(0x25A);
    lost = mem.at<Enemy>(obj).parts_lost & 6u;
    mo = uint16_t(mem.at<Enemy>(obj).motion);
    if (lost == 4 && mo == 0x260)
        swap(0x264);
    else if (lost == 4 && mo == 0x25A)
        swap(0x25B);
    Enemy& e = mem.at<Enemy>(obj);
    if (!(int32_t(e.frame) <= int32_t(mem.u32(obj + ob(0x338)))) && ((e.motion_flags >> 4) & 1) == 0) {
        guard_depth_add(obj);
        return;
    }
    if (mem.u32(obj + ob(0x390)) == 0) {
        e.motion_flags &= 0xF7;
        if (uint16_t(e.type) == 0x3F) {
            set_motion_blend(obj, 0x248, 50, 8);
        } else if (opening && mem.at<Enemy>(obj).action == 8) {
            set_motion_blend(obj, 0x1DB, 50, 8);
            mem.at<Enemy>(obj).draw_flags |= 0x80;
        } else {
            set_motion_blend(obj, 0x1F1, 0x4B, 4);
        }
        mem.at<Enemy>(obj).routine = F::enguard_scripted_frame;
    }
    guard_depth_add(obj);
}

// 0x55E50 (action 12 at its spot): after frame 30 turns to the camera (step 0xC0); at the motion end route_choice = 1,
// motion 0x1F1 (72 / 16), enguard_scripted_frame next; depth list
template <class P> void Game<P>::enemy_act12_frame(uint32_t obj)
{
    enemy_frame_begin(obj);
    enemy_frame_end(obj);
    if (!(30 >= int32_t(mem.at<Enemy>(obj).frame)))
        turn_toward_camera(obj, 0xC0);
    if ((mem.at<Enemy>(obj).motion_flags >> 4) & 1) {
        mem.at<uint8_t>(G::route_choice) = 1;
        set_motion_blend(obj, 0x1F1, 0x48, 16);
        mem.at<Enemy>(obj).routine = F::enguard_scripted_frame;
    }
    depth_list_add(obj);
}

// 0x560F0: enemy_frame_end; in chapter index 2 event 0 the task ends once flag 0x51E972 is set
template <class P> void Game<P>::enemy_frame_end_ch3_ev0(uint32_t obj)
{
    enemy_frame_end(obj);
    if (mem.at<uint8_t>(G::chapter) == 2 && mem.at<uint8_t>(G::event_index) == 0 && mem.at<uint8_t>((G::event_flags + 0x32)) != 0)
        task_exit();
}

// 0x56120 (action 16 after the bite): hits; turns to the camera; from frame 0x56 (or with a second motion), with flag
// 0x51E981 and a player in state 5: the standard AI; depth list
template <class P> void Game<P>::enemy_act16_frame_b(uint32_t obj)
{
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    turn_toward_camera(obj, 0x200);
    enemy_frame_end(obj);
    if (int32_t(mem.at<Enemy>(obj).frame) > 0x55 || !(0 >= int32_t(mem.at<int16_t>(obj + ob(0xF6))))) {
        if (mem.at<uint8_t>((G::event_flags + 0x41)) != 0 && (mem.u32(G::players) == 5 || mem.u32(G::players + 0xCC) == 5))
            enemy_scripted_to_ai(obj);
    }
    depth_list_add(obj);
}

// 0x561A0 (the opening's dummy, walking): walks to its captor (0x8000, 8); on arrival the motion 0xC1490 (0 / 8),
// motion_flags bit 3, the bite slot and ENGuardZombieBiteAgent next; drawn
template <class P> void Game<P>::opening_dummy_walk(uint32_t obj)
{
    const uint32_t captor = mem.u32(obj + ob(0x390));
    if (P::pc && captor == 0)
        pc_debug_print();
    const int32_t type = mem.at<Enemy>(obj).type;
    mem.set32(G::hit_type, uint32_t(type));
    if (walk_to(obj, mem.f32(captor + ob(0x20)), mem.f32(captor + ob(0x28)), 0x8000, 8.0f) != 0) {
        set_motion_blend(obj, mem.u32(G::guard_bite_motion), 0, 8);
        mem.at<Enemy>(obj).motion_flags |= 8;
        guard_bite_slot(obj, F::ENGuardZombieBiteAgent);
    }
    enemy_frame_end(obj);
}

// 0x54D60 (action 15, on the elevator): sub-state 0: at the motion end with the second motion at 1, motion 0x289
// (0 / 4) and sub-state 1; 1: runs to the prey (0x8000, 24), then the bite (0x25A, or 0x25B with parts_lost bit 2;
// 0 / 8) and ENGuardZombieBiteAgent. Rides at the elevator's height (group head +0x24). Prey gone: motion 0x255 unless
// playing (0 / 4), unlinked, enguard_scripted_frame next. Depth list.
template <class P> void Game<P>::enemy_act15_attack_researcher(uint32_t obj)
{
    const uint32_t prey = mem.u32(obj + ob(0x390));
    const uint32_t head = mem.u32(obj + ob(0x18));
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    const int16_t sub = mem.at<Enemy>(obj).substate;
    if (sub == 0) {
        if (((mem.at<Enemy>(obj).motion_flags >> 4) & 1) && mem.at<uint16_t>(obj + ob(0xF6)) == 1) {
            set_motion_blend(obj, 0x289, 0, 4);
            mem.at<Enemy>(obj).substate = 1;
        }
    } else if (sub == 1) {
        if (walk_to(obj, mem.f32(prey + ob(0x20)), mem.f32(prey + ob(0x28)), 0x8000, 24.0f) != 0 && !(gPreyHold && gPreyHold(obj))) {
            set_motion_blend(obj, ((mem.at<Enemy>(obj).parts_lost >> 2) & 1) ? 0x25B : 0x25A, 0, 8);
            guard_bite_slot(obj, F::ENGuardZombieBiteAgent);
        }
    }
    float y = mem.f32(head + ob(0x24));
    mem.at<Enemy>(obj).pos[1] = y;
    shadow_setup(y);
    enemy_frame_end(obj);
    if (mem.u32(prey + ob(0xC)) == 0 || (mem.at<uint8_t>(prey + ob(0x58)) & 1) == 0) {
        if (uint16_t(mem.at<Enemy>(obj).motion) != 0x255)
            set_motion_blend(obj, 0x255, 0, 4);
        mem.set32(obj + ob(0x390), 0);
        mem.at<Enemy>(obj).routine = F::enguard_scripted_frame;
    }
    depth_list_add(obj);
}

// 0x543B0 (action 6, attacking a researcher): sub-state 0 runs to the prey (0x8000, 15), then bites (0x264, 30 / 4;
// sub-state 1, slot from 0xC14D0) and steps by +0x348; 1 at the slot's frame (no second motion) the prey gets flags
// bit 28 and the attack voice, sub-state 2; 2 at the motion end turns away (motion 0x288, 0, 24 frames, 0x8000). Prey
// gone (sub-states 0 / 1): unlinked, the same turn (16 frames in sub-state 0). Then enemy_act6_frame; drawn, depth list.
template <class P> void Game<P>::enemy_act6_attack_researcher(uint32_t obj)
{
    const uint32_t prey = mem.u32(obj + ob(0x390));
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    auto gone = [&]() { return mem.u32(prey + ob(0xC)) == 0 || (mem.at<uint8_t>(prey + ob(0x58)) & 1) == 0; };
    auto turnAway = [&](int frames) {
        turn_motion_set_speed(obj, 0x288, 0, frames, 0x8000);
        mem.at<Enemy>(obj).routine = F::enemy_act6_frame;
    };
    const int16_t sub = mem.at<Enemy>(obj).substate;
    if (sub == 0) {
        if (walk_to(obj, mem.f32(prey + ob(0x20)), mem.f32(prey + ob(0x28)), 0x8000, 15.0f) != 0 && !(gPreyHold && gPreyHold(obj))) {
            set_motion_blend(obj, 0x264, 30, 4);
            const int32_t m = mem.at<Enemy>(obj).motion;
            uint32_t i = 0;
            while (int32_t(mem.u32(G::guard_bite_slots + i * 16)) != m)
                ++i;
            uint32_t want = mem.u32((G::guard_bite_slots + 4) + i * 16);
            mem.at<Enemy>(obj).substate = 1;
            mem.set32(obj + ob(0x338), want);
        }
        move_along_heading(obj, mem.f32(obj + ob(0x348)));
        if (gone()) {
            mem.set32(obj + ob(0x390), 0);
            turnAway(16);
        }
    } else if (sub == 1) {
        if (int32_t(mem.at<Enemy>(obj).frame) == int32_t(mem.u32(obj + ob(0x338))) &&
            ((mem.at<Enemy>(obj).motion_flags2 >> 1) & 1) == 0 && (mem.at<uint8_t>(prey + ob(0x58)) & 1) != 0) {
            mem.set32(prey + ob(0x58), mem.u32(prey + ob(0x58)) | (1u << 28));
            enemy_voice(obj, 3);
            mem.at<Enemy>(obj).substate = 2;
        }
        if (gone()) {
            mem.set32(obj + ob(0x390), 0);
            turnAway(24);
        }
    } else if (sub == 2) {
        if ((mem.at<Enemy>(obj).motion_flags >> 4) & 1)
            turnAway(24);
    }
    enemy_frame_end(obj);
    depth_list_add(obj);
}

// 0x54960 (action 14): sub-state 0 at the motion end roars (0x23C, 0 / 4; sub-state 1); 1 runs to the prey (0x8000, 16)
// and bites (0x24A, 0 / 8; ENGuardZombieBiteAgent). Prey gone, or chapter index 2 event 0 with 0x51E9D8: motion 0x248
// unless playing (0 / 4), unlinked, enguard_scripted_frame. Chapter index 2 release; depth list.
template <class P> void Game<P>::enemy_act14_attack_researcher(uint32_t obj)
{
    const uint32_t prey = mem.u32(obj + ob(0x390));
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    const int16_t sub = mem.at<Enemy>(obj).substate;
    if (sub == 0) {
        if ((mem.at<Enemy>(obj).motion_flags >> 4) & 1) {
            set_motion_blend(obj, 0x23C, 0, 4);
            mem.at<Enemy>(obj).substate = 1;
        }
    } else if (sub == 1) {
        if (walk_to(obj, mem.f32(prey + ob(0x20)), mem.f32(prey + ob(0x28)), 0x8000, 16.0f) != 0 && !(gPreyHold && gPreyHold(obj))) {
            set_motion_blend(obj, 0x24A, 0, 8);
            guard_bite_slot(obj, F::ENGuardZombieBiteAgent);
        }
    }
    enemy_frame_end(obj);
    if (mem.u32(prey + ob(0xC)) == 0 || (mem.at<uint8_t>(prey + ob(0x58)) & 1) == 0 ||
        (mem.at<uint8_t>(G::chapter) == 2 && mem.at<uint8_t>(G::event_index) == 0 && mem.at<uint8_t>((G::event_flags + 0x98)) != 0)) {
        if (uint16_t(mem.at<Enemy>(obj).motion) != 0x248)
            set_motion_blend(obj, 0x248, 0, 4);
        mem.set32(obj + ob(0x390), 0);
        mem.at<Enemy>(obj).routine = F::enguard_scripted_frame;
    }
    if (guard_ch2_release(obj))
        return;
    guard_depth_add(obj);
}

// 0x54B40 (actions 17 / 18): sub-state 0 at the motion end motion 0x289 (0 / 4; sub-state 1); 1 runs to the prey (0x8000,
// 24), then in event 1 motion 0x26C (30 / 16), else the bite 0x25A / 0x25B (parts_lost bit 2; 0 / 8), slot,
// ENGuardZombieBiteAgent. Prey gone (or chapter index 2 event 0 with 0x51E9D8): motion_flags bit 3 off, unlinked,
// 0x255 (60 / 8), enguard_scripted_frame. Chapter index 2 release; depth list; attacker list.
template <class P> void Game<P>::enemy_act17_attack_researcher(uint32_t obj)
{
    const uint32_t prey = mem.u32(obj + ob(0x390));
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    const int16_t sub = mem.at<Enemy>(obj).substate;
    if (sub == 0) {
        if ((mem.at<Enemy>(obj).motion_flags >> 4) & 1) {
            set_motion_blend(obj, 0x289, 0, 4);
            mem.at<Enemy>(obj).substate = 1;
        }
    } else if (sub == 1) {
        if (walk_to(obj, mem.f32(prey + ob(0x20)), mem.f32(prey + ob(0x28)), 0x8000, 24.0f) != 0 && !(gPreyHold && gPreyHold(obj))) {
            if (mem.at<uint8_t>(G::event_index) == 1)
                set_motion_blend(obj, 0x26C, 30, 16);
            else
                set_motion_blend(obj, ((mem.at<Enemy>(obj).parts_lost >> 2) & 1) ? 0x25B : 0x25A, 0, 8);
            guard_bite_slot(obj, F::ENGuardZombieBiteAgent);
        }
    }
    enemy_frame_end(obj);
    if (mem.u32(prey + ob(0xC)) == 0 || (mem.at<uint8_t>(prey + ob(0x58)) & 1) == 0 ||
        (mem.at<uint8_t>(G::chapter) == 2 && mem.at<uint8_t>(G::event_index) == 0 && mem.at<uint8_t>((G::event_flags + 0x98)) != 0)) {
        Enemy& e = mem.at<Enemy>(obj);
        mem.set32(obj + ob(0x390), 0);
        e.motion_flags &= 0xF7;
        set_motion_blend(obj, 0x255, 60, 8);
        mem.at<Enemy>(obj).routine = F::enguard_scripted_frame;
    }
    if (guard_ch2_release(obj))
        return;
    guard_depth_add(obj);
    attacker_list_add(obj);
}

// 0x54720 (action 7, the chainsaw): sub-state 0 at the motion end revs up (0x229, 0 / 4; sub-state 1); 1 runs to the
// prey: within 8 the attack 0x225 (24 / 8); with the head lost (parts_lost bit 1) within 11 motion 0x21C (40 / 24),
// else within 14 0x21C (30 / 12): slot from 0xC14D0, routine 0x54540, and nothing else this frame. Otherwise drawn;
// prey gone: 0x229 unless playing, draw_flags bit 7, unlinked, routine 0x54FE0; hit class 3: motion 0x20C (0 / 8).
// Depth list unless still in sub-state 0 before frame 0x73.
template <class P> void Game<P>::enemy_act7_chainsaw_attack(uint32_t obj)
{
    const uint32_t prey = mem.u32(obj + ob(0x390));
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    auto attack = [&](uint32_t motion, int a, int frames) {
        set_motion_blend(obj, motion, a, frames);
        const int32_t m = mem.at<Enemy>(obj).motion;
        uint32_t i = 0;
        while (int32_t(mem.u32(G::guard_bite_slots + i * 16)) != m)
            ++i;
        uint32_t want = mem.u32((G::guard_bite_slots + 4) + i * 16);
        mem.at<Enemy>(obj).routine = F::enemy_act7_frame;
        mem.set32(obj + ob(0x338), want);
    };
    const int16_t sub = mem.at<Enemy>(obj).substate;
    if (sub == 0) {
        if ((mem.at<Enemy>(obj).motion_flags >> 4) & 1) {
            set_motion_blend(obj, 0x229, 0, 4);
            mem.at<Enemy>(obj).substate = 1;
        }
    } else if (sub == 1) {
        const float px = mem.f32(prey + ob(0x20)), pz = mem.f32(prey + ob(0x28));
        if (((mem.at<Enemy>(obj).parts_lost >> 1) & 1) == 0) {
            if (walk_to(obj, px, pz, 0x8000, 8.0f) != 0 && !(gPreyHold && gPreyHold(obj))) {
                attack(0x225, 24, 8);
                return;
            }
        } else if (walk_to(obj, px, pz, 0x8000, 11.0f) != 0) {
            if (!(gPreyHold && gPreyHold(obj))) {   // (held: no second step this frame)
                attack(0x21C, 40, 24);
                return;
            }
        } else if (walk_to(obj, mem.f32(prey + ob(0x20)), mem.f32(prey + ob(0x28)), 0x8000, 14.0f) != 0 && !(gPreyHold && gPreyHold(obj))) {
            attack(0x21C, 30, 12);
            return;
        }
    }
    enemy_frame_end(obj);
    if (mem.u32(prey + ob(0xC)) == 0 || (mem.at<uint8_t>(prey + ob(0x58)) & 1) == 0) {
        if (uint16_t(mem.at<Enemy>(obj).motion) != 0x229)
            set_motion_blend(obj, 0x229, 0, 4);
        uint8_t df = mem.at<Enemy>(obj).draw_flags;
        mem.set32(obj + ob(0x390), 0);
        mem.at<Enemy>(obj).routine = F::enemy_act7_frame_b;
        mem.at<Enemy>(obj).draw_flags = uint8_t(df | 0x80);
    } else if (mem.at<uint8_t>(G::hit_class) == 3) {
        set_motion_blend(obj, 0x20C, 0, 8);
    }
    if (mem.at<uint16_t>(obj + ob(0x60)) == 0 && int32_t(mem.at<Enemy>(obj).frame) <= 0x72)
        return;
    depth_list_add(obj);
}

// 0x53810 (the elevator of action 15, a child task): drawn as model 0x1686 at its position and angles (push, 0x12,
// 0x86 rotate z / y / x, pop); until it is below y -338.2 it sinks 0.2 a frame, shootable (flags 0x401, radius 15) with its
// camera-space point = the current matrix x (x, y - 15, z - 15)
template <class P> void Game<P>::elevator_ride(uint32_t obj)
{
    Tgp::Out o, none;
    copro(0x05, nullptr, 0, none);
    uint32_t t[3] = {mem.u32(obj + ob(0x20)), mem.u32(obj + ob(0x24)), mem.u32(obj + ob(0x28))};
    copro(0x12, t, 3, none);
    uint32_t r[3] = {mem.u32(obj + ob(0x34)), mem.u32(obj + ob(0x30)), mem.u32(obj + ob(0x2C))};
    copro(0x86, r, 3, none);
    geo_draw_model(0x1686);
    copro(0x06, nullptr, 0, none);
    Obj& e = mem.at<Obj>(obj);
    if (e.pos[1] < ffrom(0xC3A9199Au))   // -338.2 (ch4proof: the differential, Chapter 3: the float constant was -338.4)
        return;
    const float y = e.pos[1] - 0.2f;
    const float vy = y - 15.0f, vz = e.pos[2] - 15.0f;
    e.flags = 0x401;
    e.hit_radius = 15.0f;
    e.pos[1] = y;
    uint32_t v[3];
    v[0] = mem.u32(obj + ob(0x20));
    std::memcpy(&v[1], &vy, 4);
    std::memcpy(&v[2], &vz, 4);
    copro(0x1A, v, 3, o);
    std::memcpy(mem.ptr(obj + ob(0x38)), o.w, 12);
    depth_list_add(obj);
}

// 0x54540 (the chainsaw at work): at the slot's frame (no second motion, prey shootable) the hit lands for motion 0x225
// with the head on, or 0x21C with the head lost: prey flags bit 28, attack voice. Prey not shootable: unlinked, flags2
// bit 0; else within 6 the prey is pushed back (z - 0.6). Head lost during 0x225 after frame 39: 0x21C (30 / 12) and
// its slot. Past the frame at the motion end (or before) with no prey: 0x1F1 (75 / 4), enguard_scripted_frame. Depth.
template <class P> void Game<P>::enemy_act7_frame(uint32_t obj)
{
    const uint32_t prey = mem.u32(obj + ob(0x390));
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    enemy_frame_end(obj);
    auto headLost = [&]() { return ((mem.at<Enemy>(obj).parts_lost >> 1) & 1) != 0; };
    auto motion = [&]() { return uint16_t(mem.at<Enemy>(obj).motion); };
    if (int32_t(mem.at<Enemy>(obj).frame) == int32_t(mem.u32(obj + ob(0x338))) &&
        ((mem.at<Enemy>(obj).motion_flags2 >> 1) & 1) == 0 && (mem.at<uint8_t>(prey + ob(0x58)) & 1) != 0) {
        if ((motion() == 0x225 && !headLost()) || (motion() == 0x21C && headLost())) {
            mem.set32(prey + ob(0x58), mem.u32(prey + ob(0x58)) | (1u << 28));
            enemy_voice(obj, 3);
        }
    }
    if ((mem.at<uint8_t>(prey + ob(0x58)) & 1) == 0) {
        uint16_t f2 = mem.at<Enemy>(obj).flags2;
        mem.set32(obj + ob(0x390), 0);
        mem.at<Enemy>(obj).flags2 = uint16_t(f2 | 1);
    } else {
        Enemy& e = mem.at<Enemy>(obj);
        float d = floor_distance(e.pos[0], e.pos[2], mem.f32(prey + ob(0x20)), mem.f32(prey + ob(0x28)));
        if (d < 6.0f)
            mem.at<float>(prey + ob(0x28)) = mem.f32(prey + ob(0x28)) - 0.6f;
    }
    if (!(int32_t(mem.at<Enemy>(obj).frame) <= 39) && headLost() && motion() == 0x225) {
        set_motion_blend(obj, 0x21C, 30, 12);
        const int32_t m = mem.at<Enemy>(obj).motion;
        uint32_t i = 0;
        while (int32_t(mem.u32(G::guard_bite_slots + i * 16)) != m)
            ++i;
        mem.set32(obj + ob(0x338), mem.u32((G::guard_bite_slots + 4) + i * 16));
    }
    Enemy& e = mem.at<Enemy>(obj);
    if (!(!(int32_t(e.frame) <= int32_t(mem.u32(obj + ob(0x338)))) && ((e.motion_flags >> 4) & 1) == 0)) {
        if (mem.u32(obj + ob(0x390)) == 0) {
            e.motion_flags &= 0xF7;
            set_motion_blend(obj, 0x1F1, 0x4B, 4);
            mem.at<Enemy>(obj).routine = F::enguard_scripted_frame;
        }
    }
    depth_list_add(obj);
}

// 0x54FE0 (the chainsaw zombie without prey): walks to the record's point (0x8000, 8), then the standard AI; drawn, depth
template <class P> void Game<P>::enemy_act7_frame_b(uint32_t obj)
{
    const uint32_t rec = mem.u32(obj + ob(0x368));
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if ((mem.at<uint8_t>(obj + ob(0x58)) & 1) == 0)
        return;
    if (walk_to(obj, mem.f32(rec + 4), mem.f32(rec + 0xC), 0x8000, 8.0f) != 0)
        enemy_scripted_to_ai(obj);
    enemy_frame_end(obj);
    depth_list_add(obj);
}

// boundaries: not ported yet

// 0x1CA50 (enemy, step): aims at the camera (one enemy) or at its attack position beside the camera (x + slot * 6 - 3,
// in double precision; more than one enemy); with no enemies counted the direction is 0. Velocity = coprocessor 0x0C
// of the direction, heading = its angle (0x0F, folded into the front half), turned toward at 0x100, then a step.
template <class P> void Game<P>::walk_to_attack_position(uint32_t obj, float step)
{
    if (pc_path()) {
        // PC 0x447450: x87 float32 (the slot offset rounded per operation, not in double), velocity = d / its table
        // length, the angles by pc_vec_angles3 (its fold included)
        const int16_t count = mem.at<int16_t>(G::enemy_count);
        const float camx = mem.f32(G::camera_pos), camz = mem.f32(G::camera_pos + 8);
        float dx = 0.0f, dz = 0.0f;
        const float dy = 0.0f;
        if (count == 1) {
            dz = camz - mem.f32(obj + ob(0x28));
            dx = camx - mem.f32(obj + ob(0x20));
        } else if (count >= 2) {
            const float t = (float(mem.at<int8_t>(obj + ob(0x2E7))) * 6.0f + camx) + -3.0f;
            dz = camz - mem.f32(obj + ob(0x28));
            dx = t - mem.f32(obj + ob(0x20));
        }
        const float inv = 1.0f / pc_fast_sqrt4((dx * dx + dy * dy) + dz * dz);
        mem.set32(obj + ob(0x78), fbits(dx * inv));
        mem.set32(obj + ob(0x7C), fbits(dy * inv));
        mem.set32(obj + ob(0x80), fbits(dz * inv));
        uint32_t yaw = 0, pitch = 0;
        pc_vec_angles3(dx, dy, dz, yaw, pitch);
        angle_turn_toward(obj, yaw, 0x100);
        move_along_heading(obj, step);
        return;
    }
    auto fb = [](float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; };
    const uint16_t count = mem.at<uint16_t>(G::enemy_count);
    const float camx = mem.f32(G::camera_pos), camz = mem.f32(G::camera_pos + 8);
    float dx = 0.0f, dz = 0.0f;
    if (count == 1) {
        dx = camx - mem.f32(obj + ob(0x20));
        dz = camz - mem.f32(obj + ob(0x28));
    } else if (int32_t(uint32_t(count) << 16) > 0x10000) {
        double t = double(mem.at<int8_t>(obj + ob(0x2E7))) * 6.0;
        t = double(camx) + t;
        t = t - 3.0;
        t = t - double(mem.f32(obj + ob(0x20)));
        dz = camz - mem.f32(obj + ob(0x28));
        dx = float(t);
    }
    Tgp::Out o;
    uint32_t d[3] = {fb(dx), 0, fb(dz)};
    copro(0x0C, d, 3, o);
    mem.set32(obj + ob(0x78), o.w[0]);
    mem.set32(obj + ob(0x7C), o.w[1]);
    mem.set32(obj + ob(0x80), o.w[2]);
    copro(0x0F, d, 3, o);
    uint32_t a0 = o.w[0];
    const uint32_t a1 = o.w[1] & 0xFFFF;
    if (((a1 << 1) ^ a1) & 0x8000)
        a0 += 0x8000;
    angle_turn_toward(obj, a0, 0x100);
    move_along_heading(obj, step);
}

// PC copies of the shared helpers (same code, other places in the PC program)
template <class P> float Game<P>::pc_fast_sqrt4(float x) { return pc_fast_sqrt_table(x); }   // PC 0x445A8C
template <class P> float Game<P>::pc_fast_sqrt5(float x) { return pc_fast_sqrt_table(x); }   // PC 0x4A198C
template <class P> void Game<P>::pc_vec_angles3(float x, float y, float z, uint32_t& yaw, uint32_t& pitch)   // PC 0x445338
{
    pc_vec_angles(x, y, z, yaw, pitch);
}
template <class P> void Game<P>::pc_matrix_transform3(float x, float y, float z, float& ox, float& oy, float& oz)   // PC 0x4A1150
{
    pc_matrix_transform(x, y, z, ox, oy, oz);
}

} // namespace hotd::port
