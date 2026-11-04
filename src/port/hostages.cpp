// Kind 100, the hostages (scene people; arcade cluster 0x56460..0x5C8B0), teaser L5: read from the arcade disassembly
// (the typed decompilation for the structure). hostage picks the scenario by the record's action (+0x64, table
// hostage_scenarios 0xC1580: 0-15 researchers held by creatures, 16-25 cut-scene people); Chapter 1 uses scenarios
// 0-6 (events 0, 3, 6, 13, 15, 18), ported here with every routine they reach; scenarios 7-25 stay boundaries.
// Object words: +0x390 the captor (the creature holding him: wait until it is gone), +0x2EB the rescuer (the captor's
// killer, 0 / 1), +0x2EC the scene number at the spawn, +0x2D4 the home position (the effects start there), +0x36C
// the end routine (hostage_end_wait when the record's +0x70 bit 14 is set, else hostage_end_remove), +0x2E7 the
// attack slot. draw_flags bit 7 = facing the camera (turned toward it), bit 6 = rides the object at +0x18 (its y).
// Arcade forms only.
#include "port/game.h"

namespace hotd::port {

// helpers without an original routine of their own (inline in the original)
template <class P> void Game<P>::hostage_slot_free_inline(uint32_t obj)
{
    if (of<uint8_t>(obj, 0x2E7) != 0xFF) {
        mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 20) = 0;
        of<int8_t>(obj, 0x2E7) = -1;
    }
}

template <class P> bool Game<P>::hostage_motion_done(uint32_t obj)
{
    return ((of<uint8_t>(obj, 0x3C5) >> 4) & 1) != 0;
}

// the two rescue counters, the game's and the chapter's
template <class P> void Game<P>::hostage_count_rescue()
{
    mem.at<uint16_t>(G::hostages_rescued) = uint16_t(mem.at<uint16_t>(G::hostages_rescued) + 1);
    mem.at<uint16_t>(G::hostages_rescued_chapter) = uint16_t(mem.at<uint16_t>(G::hostages_rescued_chapter) + 1);
}

// 400 points to the rescuer (+0x2EB) when that player is in the game (state 5)
template <class P> void Game<P>::hostage_rescuer_score(uint32_t obj, int32_t points)
{
    const int32_t p = of<int8_t>(obj, 0x2EB);
    if (mem.at<Player>(G::players, uint32_t(p)).state == 5)
        add_score((uint32_t(p) & 0xFF) != 0 ? 1 : 0, points);
}

// the lip-sync head while a line is spoken: frames (timer % 32) / 2, `closed` on step 15, else `first` + step
template <class P> void Game<P>::hostage_lips32(uint32_t obj, uint32_t closed, uint32_t first)
{
    const int32_t t = of<int32_t>(obj, 0x50);
    const int32_t v = (t % 32) / 2;
    body_role_model_set(obj, 1, v == 15 ? closed : uint32_t(int32_t(first) + v));
    of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 1;
}

// PC Mode / Boss Mode: THOTD.EXE's held hostage 0x4AC932 and researcher_shot_by_player 0x4AD064 draw one more random
// number after the arcade's pick and clear a shooter bit by it (player 1's on an odd number, else player 2's), so with
// one shooter the penalty goes to the other seat half of the time (hostage_shot 0x4ACD28 does not; pmrest2)
template <class P> void Game<P>::pm_shot_bit_drop(uint32_t obj)
{
    if (!pc_path())
        return;
    const uint32_t r = rand();
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~(1u << ((r & 1) == 0 ? 2 : 1));
}

// the hit / shot effect task at the home position (hostage_shot_effect), the shooter (flags bits 1 / 2; both: one at
// random) loses a life (player_hit_lose_life, forced), the effect's +0x2EB = the shooter
template <class P> void Game<P>::hostage_shot_penalty(uint32_t obj, bool held)
{
    if (lg_hostage_crisis)
        lg_crisis_fail = true;   // Light Gun Edition: Hostage Crisis: game over for everyone
    const uint32_t t = task_create(F::hostage_shot_effect, 0x3C8);
    of<uint32_t>(t, 0x20) = of<uint32_t>(obj, 0x2D4);
    of<uint32_t>(t, 0x24) = of<uint32_t>(obj, 0x2D8);
    of<uint32_t>(t, 0x50) = 0;
    of<uint32_t>(t, 0x28) = of<uint32_t>(obj, 0x2DC);
    if ((of<uint32_t>(obj, 0x58) & 6) == 6) {
        const int32_t r = int32_t(rand());
        if (r % 2 != 0)
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~2u;
        else
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~4u;
    }
    if (held)
        pm_shot_bit_drop(obj);
    if (coop4_on()) {   // 4P: the real shooter of player 1's / 2's bit (the hit table)
        const int who = coop4_shooter(obj, ((of<uint8_t>(obj, 0x58) >> 1) & 1) ? 0 : 1);
        player_hit_lose_life(who, 0, 0, 1, -1);
        of<uint8_t>(t, 0x2EB) = uint8_t(who);
    } else if ((of<uint8_t>(obj, 0x58) >> 1) & 1) {
        player_hit_lose_life(0, 0, 0, 1, -1);
        of<uint8_t>(t, 0x2EB) = 0;
    } else {
        player_hit_lose_life(1, 0, 0, 1, -1);
        of<uint8_t>(t, 0x2EB) = 1;
    }
    researcher_scream_penalty(obj, hostage_is_female(obj), 1);
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xFFFFFEFEu;
}

// the fall of a dropped / thrown researcher: y += vy, vy += -0.0544 (in double precision)
template <class P> void Game<P>::hostage_fall_step(uint32_t obj)
{
    const float vy = of<float>(obj, 0x7C);
    of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
    of<float>(obj, 0x7C) = float(double(vy) + ffrom64(0xBFABE02460000000ull));
}

// a call through the scenario table or the end routine (+0x36C): the ported routines directly, the rest a boundary
template <class P> void Game<P>::hostage_call(uint32_t routine, uint32_t obj)
{
    switch (routine) {
    case F::hostage_s00_bystander: hostage_s00_bystander(obj); return;
    case F::hostage_s01_hands_up: hostage_s01_hands_up(obj); return;
    case F::hostage_s02_flee: hostage_s02_flee(obj); return;
    case F::hostage_s03_carried: hostage_s03_carried(obj); return;
    case F::hostage_s04_message: hostage_s04_message(obj); return;
    case F::hostage_s05_held_walk: hostage_s05_held_walk(obj); return;
    case F::hostage_s06_hanging: hostage_s06_hanging(obj); return;
    case F::hostage_end_wait: hostage_end_wait(obj); return;
    case F::hostage_end_countdown: hostage_end_countdown(obj); return;
    case F::hostage_end_remove: hostage_end_remove(obj); return;
    default: call_routine(routine, obj); return;
    }
}

// 0x56460 (kind 100): the scenario's set-up, the end routine by the record's +0x70 bit 14 (cleared), draw_flags bit
// 5, the type set-up, an enemy slot, the human list
template <class P> void Game<P>::hostage(uint32_t obj)
{
    mem.at<int32_t>(G::hit_type) = of<int16_t>(obj, 0xCC);
    of<int16_t>(obj, 0xCE) = 0;
    mem.at<uint16_t>(G::scene_count_520794) = uint16_t(mem.at<uint16_t>(G::scene_count_520794) + 1);
    mem.at<uint16_t>(G::hostages_appeared) = uint16_t(mem.at<uint16_t>(G::hostages_appeared) + 1);
    hostage_call(mem.u32(G::hostage_scenarios + uint32_t(int32_t(of<int16_t>(obj, 0x64))) * 4), obj);
    uint16_t p = of<uint16_t>(obj, 0x70);
    if (((p >> 14) & 1) == 0) {
        of<uint32_t>(obj, 0x36C) = F::hostage_end_remove;
    } else {
        of<uint32_t>(obj, 0x36C) = F::hostage_end_wait;
        p = uint16_t(p & 0xBFFF);
    }
    of<uint16_t>(obj, 0x70) = p;
    of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) | 0x20);
    creature_type_setup(obj);
    enemy_slot_take(obj);
    mem.set32(G::hostage_humans_count, hostage_list_humans());
}

// 0x56510 (kind 103): the second person of a scene: the scenario without the counters, the end routine or the slot
template <class P> void Game<P>::hostage_b(uint32_t obj)
{
    mem.at<int32_t>(G::hit_type) = of<int16_t>(obj, 0xCC);
    of<int16_t>(obj, 0xCE) = 0;
    hostage_call(mem.u32(G::hostage_scenarios + uint32_t(int32_t(of<int16_t>(obj, 0x64))) * 4), obj);
    creature_type_setup(obj);
}

// 0x56540 (scenario 0, atozu): a bystander backing away (MO 0xD4, state 4); 100 points to each playing player
template <class P> void Game<P>::hostage_s00_bystander(uint32_t obj)
{
    body_init(obj, mem.at<int32_t>(G::hit_type), 0xD4);
    of<int16_t>(obj, 0x5C) = 4;
    of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) | 9);
    if (mem.at<Player>(G::players, 0).state == 5)
        add_score(0, 100);
    if (mem.at<Player>(G::players, 1).state == 5)
        add_score(1, 100);
    of<uint32_t>(obj, 0) = F::hostage_s00_frame;
}

// 0x565B0: drawn only (body_draw) while the palette is grey or event flag 0x8D is set without 0x8E; else the struck
// reaction, the frame, the attacker list (sound 0xA01331 on the first frame after the grey palette)
template <class P> void Game<P>::hostage_s00_frame(uint32_t obj)
{
    const uint16_t grey = mem.at<uint16_t>(G::palette_grey_on);
    if (grey != 0 || (mem.at<uint8_t>(G::event_flags + 0x8D) != 0 && mem.at<uint8_t>(G::event_flags + 0x8E) == 0)) {
        body_draw(obj);
    } else {
        if (mem.at<int32_t>(G::hostage_grey_prev) != 0 && grey == 0)
            sound_cue(0xA01331);
        hostage_struck(obj);
        enemy_frame_end(obj);
        attacker_list_add(obj);
    }
    mem.at<int32_t>(G::hostage_grey_prev) = mem.at<int16_t>(G::palette_grey_on);
}

// 0x56630 (scenario 1, teanige): hands up (MO 0x10E, state 5)
template <class P> void Game<P>::hostage_s01_hands_up(uint32_t obj)
{
    body_init(obj, mem.at<int32_t>(G::hit_type), 0x10E);
    of<int16_t>(obj, 0x5C) = 5;
    of<uint32_t>(obj, 0) = F::hostage_s01_wait;
}

// 0x56660: shot / struck; freed (hostage_captor_gone): runs off, route_choice = 1 (rescuing him changes the route)
template <class P> void Game<P>::hostage_s01_wait(uint32_t obj)
{
    hostage_shot(obj);
    hostage_struck(obj);
    if (hostage_captor_gone(obj) != 0) {
        set_motion_blend(obj, 0xFF, 0, 2);
        of<int16_t>(obj, 0x60) = 0;
        of<uint32_t>(obj, 0) = F::hostage_s01_run;
        of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) | 0x80);
        mem.at<uint8_t>(G::route_choice) = 1;
        return;
    }
    enemy_frame_end(obj);
    depth_list_add(obj);
}

// 0x566E0: runs (hostage_s01_run_steps); shootable while 25 or more from the camera with enemies alive
template <class P> void Game<P>::hostage_s01_run(uint32_t obj)
{
    const float cx = mem.f32(G::camera_pos), cz = mem.f32(G::camera_pos + 8);
    float d = floor_distance(of<float>(obj, 0x20), of<float>(obj, 0x28), cx, cz);
    if (!(d < 25.0f) && mem.at<uint16_t>(G::enemies_alive) != 0) {
        hostage_shot(obj);
        if (of<uint32_t>(obj, 0) == F::hostage_s01_run_task)
            return;
    }
    hostage_s01_run_steps(obj);
    d = floor_distance(of<float>(obj, 0x20), of<float>(obj, 0x28), mem.f32(G::camera_pos), mem.f32(G::camera_pos + 8));
    if (!(d < 25.0f) && mem.at<uint16_t>(G::enemies_alive) != 0)
        depth_list_add(obj);
    enemy_frame_end(obj);
}

// 0x56810 (scenario 2, tenonige): hands up (MO 0x10F, state 5)
template <class P> void Game<P>::hostage_s02_flee(uint32_t obj)
{
    body_init(obj, mem.at<int32_t>(G::hit_type), 0x10F);
    of<int16_t>(obj, 0x5C) = 5;
    of<uint32_t>(obj, 0) = F::hostage_s02_wait;
}

// 0x56840: shot / struck; freed: hostage_s02_thank
template <class P> void Game<P>::hostage_s02_wait(uint32_t obj)
{
    hostage_shot(obj);
    hostage_struck(obj);
    if (hostage_captor_gone(obj) != 0) {
        set_motion_blend(obj, 0xFF, 0, 2);
        of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) | 0x80);
        of<int16_t>(obj, 0x60) = 0;
        of<uint32_t>(obj, 0) = F::hostage_s02_thank;
        return;
    }
    enemy_frame_end(obj);
    depth_list_add(obj);
}

// 0x568B0 (also scenario 7)
template <class P> void Game<P>::hostage_s02_thank(uint32_t obj)
{
    hostage_thank_and_leave(obj);
    enemy_frame_end(obj);
}

// 0x568D0 (scenario 3, bata): carried by a creature, flailing (MO 0xD9, state 1)
template <class P> void Game<P>::hostage_s03_carried(uint32_t obj)
{
    body_init(obj, mem.at<int32_t>(G::hit_type), 0xD9);
    of<int16_t>(obj, 0x5C) = 1;
    of<uint32_t>(obj, 0) = F::hostage_s03_start;
}

// 0x56900 (scenario 4, atozu): backing away (MO 0xD4, state 4), the help voice by sex
template <class P> void Game<P>::hostage_s04_message(uint32_t obj)
{
    body_init(obj, mem.at<int32_t>(G::hit_type), 0xD4);
    of<int16_t>(obj, 0x5C) = 4;
    of<uint32_t>(obj, 0) = F::hostage_s04_wait;
    sound_cue(hostage_is_female(obj) != 0 ? 0xA01328 : 0xA0133B);
}

// 0x56960: shot / struck; freed: turns toward the camera (hokan_l 0xE5 / hokan_r 0xE6 by its side), hostage_s04_rescue
template <class P> void Game<P>::hostage_s04_wait(uint32_t obj)
{
    hostage_shot(obj);
    hostage_struck(obj);
    if (hostage_captor_gone(obj) != 0) {
        const int32_t a = int32_t(angle_to_camera(obj));
        set_motion_blend(obj, a > 0x7FFF ? 0xE6 : 0xE5, 0, 2);
        of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) | 0x80);
        of<int16_t>(obj, 0x60) = 0;
        of<uint32_t>(obj, 0) = F::hostage_s04_rescue;
        return;
    }
    enemy_frame_end(obj);
    depth_list_add(obj);
    attacker_list_add(obj);
}

// 0x569F0: 0 the turn ends (frame 20 / 40): runs up (turn_motion_set), scene_state 5; 1 within 15 of the camera:
// voice line 12 (a voice_message_task), MO 0xEB, item 0x71C in hand; 2 MO 0xEC, head 0x4DB (lips meanwhile);
// 3 thanks (0x113 in Chapter 1 events 8 / 13 / 18, else 0x116), counted, 400 points, scene_state 6; 4 off the attack
// slot, MO 0xFF; 5 hostage_scene_end_frame
template <class P> void Game<P>::hostage_s04_rescue(uint32_t obj)
{
    switch (of<int16_t>(obj, 0x60)) {
    case 0: {
        const int32_t a = int32_t(angle_to_camera(obj));
        const int32_t f = of<int16_t>(obj, 0xF4);
        if (a > 0x7FFF ? f <= 39 : f <= 19)
            break;
        turn_motion_set(obj, 0xFF, 0, 2);
        mem.at<uint8_t>(G::scene_state) = 5;
        of<int16_t>(obj, 0x60) = 1;
        break;
    }
    case 1: {
        if ((of<uint8_t>(obj, 0x3C6) >> 1) & 1)
            break;
        if (hostage_camera_near(obj) == 0)
            break;
        const uint32_t t = task_create(F::voice_message_task, 0x3C8);
        task_clear_work(t);
        of<int16_t>(t, 0x70) = 12;
        set_motion_blend(obj, 0xEB, 0, 2);
        body_role_model_set(obj, 6, 0x71C);
        of<int16_t>(obj, 0x60) = 2;
        break;
    }
    case 2:
        if (hostage_motion_done(obj)) {
            set_motion_blend(obj, 0xEC, 0, 2);
            body_role_model_set(obj, 6, 0x4DB);
            of<int16_t>(obj, 0x60) = 3;
        } else {
            hostage_lips32(obj, 0x4AC, 0x4BE);
        }
        break;
    case 3:
        if (hostage_motion_done(obj)) {
            const uint8_t ev = mem.at<uint8_t>(G::event_index);
            const bool ch1 = mem.at<uint8_t>(G::chapter) == 0 && (ev == 8 || ev == 13 || ev == 18);
            set_motion_blend(obj, ch1 ? 0x113 : 0x116, 0, 4);
            hostage_count_rescue();
            hostage_rescuer_score(obj, 400);
            mem.at<uint8_t>(G::scene_state) = 6;
            of<int16_t>(obj, 0x60) = 4;
        } else {
            hostage_lips32(obj, 0x4AC, 0x4BE);
        }
        break;
    case 4:
        attack_slot_free(obj);
        if (hostage_motion_done(obj)) {
            set_motion_blend(obj, 0xFF, 0, 4);
            of<int16_t>(obj, 0x60) = 5;
        }
        break;
    case 5:
        if (hostage_motion_done(obj)) {
            of<int16_t>(obj, 0x60) = 0;
            of<uint32_t>(obj, 0) = F::hostage_scene_end_frame;
        }
        break;
    default: break;
    }
    enemy_frame_end(obj);
}

// 0x56C90 (scenario 5, holdtati): held while walking with a creature (MO 0xE8, state 18, facing the camera); counts
// as an enemy (enemy_count, enemies_alive) until rescued
template <class P> void Game<P>::hostage_s05_held_walk(uint32_t obj)
{
    body_init(obj, mem.at<int32_t>(G::hit_type), 0xE8);
    of<int16_t>(obj, 0x5C) = 18;
    of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) | 0x80);
    of<uint32_t>(obj, 0) = F::hostage_s05_frame;
    of<int32_t>(obj, 0x2EC) = mem.at<int8_t>(G::scene_number);
    mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
}

// 0x56D00: shot (researcher_shot_by_player); 0 turns to the camera; 1 walks to (30, 290) (thanks voice); 2 within 30
// of the camera: off the attack slot and the enemy counts; 6 rescued once the scene moves on: 400 points to each
// playing player, hostage_scene_end_frame
template <class P> void Game<P>::hostage_s05_frame(uint32_t obj)
{
    researcher_shot_by_player(obj);
    switch (of<int16_t>(obj, 0x60)) {
    case 0:
        turn_toward_camera(obj, 0xC0);
        if (hostage_motion_done(obj)) {
            set_motion_blend(obj, 0xFF, 0, 2);
            of<int16_t>(obj, 0x60) = 1;
        }
        break;
    case 1:
        if (walk_to(obj, 30.0f, 290.0f, 0x800, 1.0f) != 0) {
            sound_cue(0xA0132A);
            of<int16_t>(obj, 0x60) = 2;
        }
        break;
    case 2: {
        turn_toward_camera(obj, 0x200);
        const float d = floor_distance(of<float>(obj, 0x20), of<float>(obj, 0x28), mem.f32(G::camera_pos),
                                       mem.f32(G::camera_pos + 8));
        if (!(d < 30.0f))
            break;
        attack_slot_free(obj);
        const uint16_t alive = mem.at<uint16_t>(G::enemies_alive);
        if (alive != 0) {
            mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
            mem.at<uint16_t>(G::enemies_alive) = uint16_t(alive - 1);
        }
        of<int16_t>(obj, 0x60) = 6;
        break;
    }
    case 6:
        if (of<int32_t>(obj, 0x2EC) + 1 < int32_t(mem.at<int8_t>(G::scene_number))) {
            hostage_count_rescue();
            if (mem.at<Player>(G::players, 0).state == 5)
                add_score(0, 400);
            if (mem.at<Player>(G::players, 1).state == 5)
                add_score(1, 400);
            of<int16_t>(obj, 0x60) = 0;
            of<uint32_t>(obj, 0) = F::hostage_scene_end_frame;
        }
        break;
    default: break;
    }
    enemy_frame_end(obj);
    if (of<uint16_t>(obj, 0x60) != 6)
        depth_list_add(obj);
}

// 0x56EF0 (scenario 6, bulajita): hanging (MO 0xDA, state 9)
template <class P> void Game<P>::hostage_s06_hanging(uint32_t obj)
{
    body_init(obj, mem.at<int32_t>(G::hit_type), 0xDA);
    of<int16_t>(obj, 0x5C) = 9;
    of<uint32_t>(obj, 0) = F::hostage_s06_wait;
    of<int32_t>(obj, 0x2EC) = mem.at<int8_t>(G::scene_number);
}

// 0x56F30: shot / struck; freed: climbs up (yojino 0x120, thanks voice), hostage_s06_thank; the scene moved on:
// hostage_scene_end_frame
template <class P> void Game<P>::hostage_s06_wait(uint32_t obj)
{
    hostage_shot(obj);
    hostage_struck(obj);
    if (hostage_captor_gone(obj) != 0) {
        set_motion_blend(obj, 0x120, 0, 2);
        of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) | 0x80);
        sound_cue(0xA0132A);
        of<uint32_t>(obj, 0) = F::hostage_s06_thank;
        return;
    }
    if (of<int32_t>(obj, 0x2EC) < int32_t(mem.at<int8_t>(G::scene_number)))
        of<uint32_t>(obj, 0) = F::hostage_scene_end_frame;
    enemy_frame_end(obj);
    depth_list_add(obj);
}

// 0x56FC0
template <class P> void Game<P>::hostage_s06_thank(uint32_t obj)
{
    hostage_thank_and_leave(obj);
    enemy_frame_end(obj);
}

// 0x5A020: 1 unless a male researcher (types 17, 19-22)
template <class P> uint32_t Game<P>::hostage_is_female(uint32_t obj)
{
    const uint16_t t = of<uint16_t>(obj, 0xCC);
    return (t == 17 || t == 19 || t == 20 || t == 21 || t == 22) ? 0 : 1;
}

// 0x5A050: the human objects (types 14, 15, 17, 19-23, 25-27) of the 14 enemy slots, packed into prey_objects (the
// rest cleared); returns their count
template <class P> uint32_t Game<P>::hostage_list_humans()
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < 14; ++i) {
        const uint32_t o = mem.u32(G::player_bodies + 4 * i);
        mem.set32(G::prey_objects + 4 * i, 0);
        if (o == 0)
            continue;
        const uint16_t t = of<uint16_t>(o, 0xCC);
        if (t == 17 || (t >= 19 && t <= 23) || (t >= 25 && t <= 27) || t == 14 || t == 15)
            mem.set32(G::prey_objects + 4 * n++, o);
    }
    return n;
}

// 0x5A130: the thanks motion by the camera's side: 0x114 (0..0xFFF), 0x112 (0xF000..0xFFFF), 0x110 (the left half),
// else 0x111 (a man) / 0x116 (a woman)
template <class P> void Game<P>::hostage_s03_thank_motion(uint32_t obj)
{
    const int32_t a = int32_t(angle_to_camera(obj));
    uint32_t m;
    if (a >= 0 && a <= 0xFFF)
        m = 0x114;
    else if (a > 0xEFFF && a <= 0xFFFF)
        m = 0x112;
    else if (a > 0x7FFF)
        m = 0x110;
    else
        m = hostage_is_female(obj) != 0 ? 0x116 : 0x111;
    set_motion_blend(obj, m, 0, 4);
}

// 0x5A1E0: frees the object's attack position (+0x2E7) in attack_slots
template <class P> void Game<P>::attack_slot_free(uint32_t obj)
{
    hostage_slot_free_inline(obj);
}

// 0x5A210: turns toward the camera; 1 when it is closer than 15
template <class P> uint32_t Game<P>::hostage_camera_near(uint32_t obj)
{
    turn_toward_camera(obj, 0xC0);
    const float d = floor_distance(of<float>(obj, 0x20), of<float>(obj, 0x28), mem.f32(G::camera_pos),
                                   mem.f32(G::camera_pos + 8));
    return d < 15.0f ? 1 : 0;
}

// 0x5A290: the leave: MO 0xFF once the thanks end, then the end routine
template <class P> void Game<P>::hostage_s12_leave(uint32_t obj)
{
    const int16_t sub = of<int16_t>(obj, 0x60);
    if (sub == 0) {
        if (hostage_motion_done(obj)) {
            set_motion_blend(obj, 0xFF, 0, 2);
            of<int16_t>(obj, 0x60) = 1;
        }
    } else if (sub == 1) {
        hostage_end(obj);
    }
    enemy_frame_end(obj);
}

// 0x5A2E0: counted, 400 points to the rescuer, the thanks voice by sex, the leave motion by the state (0 haitati
// 0xDE, 9 yojino 0x120 -> hostage_thank_leave_step, else the thanks by side), off the attack slot
template <class P> void Game<P>::hostage_thank_and_leave(uint32_t obj)
{
    hostage_count_rescue();
    hostage_rescuer_score(obj, 400);
    sound_cue(hostage_is_female(obj) != 0 ? 0xA0132A : 0xA01332);
    const int32_t st = of<int16_t>(obj, 0x5C);
    uint32_t next = F::hostage_s12_leave;
    if (st == 0) {
        set_motion_blend(obj, 0xDE, 30, 16);
    } else if (st == 9) {
        set_motion_blend(obj, 0x120, 0, 4);
        next = F::hostage_thank_leave_step;
    } else {
        const int32_t a = int32_t(angle_to_camera(obj));
        uint32_t m;
        if (a > 0x7FFF)
            m = 0x110;
        else
            m = hostage_is_female(obj) != 0 ? 0x116 : 0x111;
        set_motion_blend(obj, m, 0, 4);
    }
    hostage_slot_free_inline(obj);
    of<uint32_t>(obj, 0) = next;
    of<int16_t>(obj, 0x60) = 0;
}

// 0x5A4B0: the run of scenario 1: 0 toward (camera + (15, 20)) at the unit vector, turning 0xC0; within 20: voice
// line 2, off the attack slot, MO 0xF7 from frame 16 turning 0x600, counted, 400 points; 4 at frame 27 no longer
// facing the camera, at 50 MO 0xFF; 5 the leave (hostage_s12_leave)
template <class P> void Game<P>::hostage_s01_run_steps(uint32_t obj)
{
    const int16_t sub = of<int16_t>(obj, 0x60);
    if (sub == 0) {
        const float tx = mem.f32(G::camera_pos) + 15.0f, tz = mem.f32(G::camera_pos + 8) + 20.0f;
        const float dx = tx - of<float>(obj, 0x20), dz = tz - of<float>(obj, 0x28);
        const uint32_t v[3] = {fbits(dx), 0, fbits(dz)};
        Tgp::Out o;
        copro(0x0C, v, 3, o);
        of<uint32_t>(obj, 0x78) = o.w[0];
        of<uint32_t>(obj, 0x7C) = o.w[1];
        of<uint32_t>(obj, 0x80) = o.w[2];
        angle_turn_toward(obj, yaw_toward_a(dx, 0.0f, dz), 0xC0);
        const float d = floor_distance(of<float>(obj, 0x20), of<float>(obj, 0x28), mem.f32(G::camera_pos) + 15.0f,
                                       mem.f32(G::camera_pos + 8) + 20.0f);
        if (!(d < 20.0f))
            return;
        const uint32_t t = task_create(F::voice_message_task, 0x3C8);
        task_clear_work(t);
        of<int16_t>(t, 0x70) = 2;
        hostage_slot_free_inline(obj);
        set_motion_at_frame(obj, 0xF7, 16, 4);
        of<int32_t>(obj, 0xE8) = 0x600;
        hostage_count_rescue();
        hostage_rescuer_score(obj, 400);
        of<int16_t>(obj, 0x60) = 4;
    } else if (sub == 4) {
        const uint16_t f = of<uint16_t>(obj, 0xF4);
        if (f == 27)
            of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) & 0x7F);
        if (f == 50) {
            set_motion_at_frame(obj, 0xFF, 0, 2);
            of<int16_t>(obj, 0x60) = 5;
        }
    } else if (sub == 5) {
        if (hostage_motion_done(obj)) {
            of<int16_t>(obj, 0x60) = 0;
            of<uint32_t>(obj, 0) = F::hostage_s12_leave;
        }
    }
}

// 0x5A770 (a child task of a struck researcher, +0x18 its parent): the mark (model 0xA28) at the parent's node
// +0x238 through stored matrix 1, 1 to the left, turned; ends with the parent
template <class P> void Game<P>::hostage_struck_mark(uint32_t task)
{
    const uint32_t parent = of<uint32_t>(task, 0x18);
    Tgp::Out none, o;
    const uint32_t one[1] = {1};
    copro(0x74, one, 1, none);
    const uint32_t p[3] = {of<uint32_t>(parent, 0x238), of<uint32_t>(parent, 0x23C), of<uint32_t>(parent, 0x240)};
    copro(0x1A, p, 3, o);
    of<uint32_t>(task, 0x20) = o.w[0];
    of<uint32_t>(task, 0x24) = o.w[1];
    of<uint32_t>(task, 0x28) = o.w[2];
    copro(0x06, nullptr, 0, none);
    copro(0x05, nullptr, 0, none);
    const uint32_t tr[3] = {fbits(of<float>(task, 0x20) - 1.0f), of<uint32_t>(task, 0x24), of<uint32_t>(task, 0x28)};
    copro(0x12, tr, 3, none);
    const uint32_t a[1] = {0xFFFFE000u};
    copro(0x14, a, 1, none);
    const uint32_t b[1] = {0x1000};
    copro(0x16, b, 1, none);
    geo_draw_model(0xA28);
    copro(0x06, nullptr, 0, none);
    if (of<uint32_t>(parent, 0xC) == 0)
        task_exit();
}

// 0x5A860: after the struck reaction (action 0): 0 at event flag 0x8F the mark task (+0x50 = +0x70), flags2 bit 0;
// 1 off the attack slot, head 0x33E once the motion ends, lips meanwhile; 2 the end routine
template <class P> void Game<P>::hostage_struck_fall(uint32_t obj)
{
    shadow_setup(mem.f32(G::floor_y));
    object_draw(obj);
    const int16_t sub = of<int16_t>(obj, 0x60);
    if (sub == 0) {
        if (mem.at<uint8_t>(G::event_flags + 0x8F) == 0)
            return;
        const uint32_t t = task_create_child_alloc(F::hostage_struck_mark, 0x58, obj);
        of<int32_t>(t, 0x50) = of<int16_t>(obj, 0x70);
        of<int16_t>(obj, 0x60) = 1;
        of<uint16_t>(obj, 0x6C) = uint16_t(of<uint16_t>(obj, 0x6C) | 1);
    } else if (sub == 1) {
        if (hostage_motion_done(obj)) {
            hostage_slot_free_inline(obj);
            body_role_model_set(obj, 1, 0x33E);
            of<int16_t>(obj, 0x60) = 2;
            return;
        }
        const int32_t v = (of<int32_t>(obj, 0x50) % 30) / 2;
        of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
        body_role_model_set(obj, 1, uint32_t(0x34F + v));
        of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 1;
    } else if (sub == 2) {
        hostage_end(obj);
    }
}

// 0x5A970: the scene's end: off the attack slot and free of the captor, then the end routine each frame (riding:
// the y of the object at +0x18)
template <class P> void Game<P>::hostage_scene_end_frame(uint32_t obj)
{
    const uint32_t ride = of<uint32_t>(obj, 0x18);
    const uint32_t captor = of<uint32_t>(obj, 0x390);
    if (of<int16_t>(obj, 0x60) == 0) {
        hostage_slot_free_inline(obj);
        if (captor != 0)
            of<uint32_t>(captor, 0x390) = 0;
        of<int16_t>(obj, 0x60) = 1;
    }
    if ((of<uint8_t>(obj, 0x3C4) >> 6) & 1)
        of<uint32_t>(obj, 0x24) = of<uint32_t>(ride, 0x24);
    shadow_setup(mem.f32(G::floor_y));
    object_draw(obj);
    hostage_end(obj);
}

// 0x5A9F0: the shot reaction (no longer facing the camera): off the attack slot at frame 80 of state 2; at its end
// (or frame 76 of state 9) hostage_scene_end_frame
template <class P> void Game<P>::hostage_s01_run_task(uint32_t obj)
{
    const uint8_t df = uint8_t(of<uint8_t>(obj, 0x3C4) & 0x7F);
    const uint32_t ride = of<uint32_t>(obj, 0x18);
    of<uint8_t>(obj, 0x3C4) = df;
    if ((df >> 6) & 1)
        of<uint32_t>(obj, 0x24) = of<uint32_t>(ride, 0x24);
    shadow_setup(mem.f32(G::floor_y));
    object_draw(obj);
    if (of<uint16_t>(obj, 0x5C) == 2 && of<uint16_t>(obj, 0xF4) == 0x50 && of<uint8_t>(obj, 0x2E7) != 0xFF) {
        mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 20) = 0;
        of<int8_t>(obj, 0x2E7) = -1;
    }
    if (hostage_motion_done(obj) || (of<uint16_t>(obj, 0x5C) == 9 && of<uint16_t>(obj, 0xF4) == 0x4C)) {
        of<int16_t>(obj, 0x60) = 0;
        of<uint32_t>(obj, 0) = F::hostage_scene_end_frame;
        return;
    }
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x5AAB0: thrown at the camera: flies at +0x78 along the heading (turned a quarter), falls; within 5 of the camera
// he hits a player (one at random with two in the game, else the solo player) and is gone; below the floor: gone
// (Chapter 1: event flag 0x90)
template <class P> void Game<P>::hostage_s03_thrown(uint32_t obj)
{
    hostage_slot_free_inline(obj);
    body_draw(obj);
    if (!(of<int16_t>(obj, 0xD0) == 0xDC && hostage_motion_done(obj)))
        of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
    of<int32_t>(obj, 0x30) = of<int32_t>(obj, 0x30) - 0x4000;
    move_along_heading(obj, of<float>(obj, 0x78));
    of<int32_t>(obj, 0x30) = of<int32_t>(obj, 0x30) + 0x4000;
    const float d = floor_distance(of<float>(obj, 0x20), of<float>(obj, 0x28), mem.f32(G::camera_pos),
                                   mem.f32(G::camera_pos + 8));
    if (d < 5.0f) {
        int32_t p;
        if (mem.at<uint16_t>(G::players_in_game) == 2)
            p = int32_t(rand()) % int32_t(mem.at<int16_t>(G::players_in_game));
        else
            p = mem.at<int8_t>(G::solo_player_is_p2);
        player_hit(p, 1, 4);
        mem.at<uint16_t>(G::scene_count_520794) = uint16_t(mem.at<uint16_t>(G::scene_count_520794) - 1);
        body_free_parts(obj);
        task_exit();
        return;
    }
    if (of<float>(obj, 0x24) + of<float>(obj, 0x7C) < mem.f32(G::floor_y)) {
        if (mem.at<uint8_t>(G::chapter) == 0)
            mem.at<uint8_t>(G::event_flags + 0x90) = 1;
        mem.at<uint16_t>(G::scene_count_520794) = uint16_t(mem.at<uint16_t>(G::scene_count_520794) - 1);
        body_free_parts(obj);
        task_exit();
        return;
    }
    hostage_fall_step(obj);
}

// 0x5AC90: dropped and rescued: kept on the ground; 0 gets up (facing the camera from frame 60), event flag 0x90;
// 1 runs to the camera; within 15: MO 0xEB, the gift in hand (0x313), voice 0xA01418; 2 the gift task (rescuer:
// 400 points), MO 0xEC, head 0x4DB, scene_state 6 (lips meanwhile); 3 the thanks by side, off the attack slot;
// 4 turns away; 5 hostage_scene_end_frame
template <class P> void Game<P>::hostage_s03_rescue(uint32_t obj)
{
    {
        LocalScope scope(*this);
        const uint32_t pt = local_alloc(12);
        mem.set32(pt, of<uint32_t>(obj, 0x20));
        mem.set32(pt + 4, fbits(32768.0f));
        mem.set32(pt + 8, of<uint32_t>(obj, 0x28));
        const float g = ground_height(pt) - 1.0f;
        if (g < of<float>(obj, 0x24))
            of<float>(obj, 0x24) = g;
    }
    switch (of<int16_t>(obj, 0x60)) {
    case 0:
        if (of<int16_t>(obj, 0xF4) > 59)
            of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) | 0x80);
        if (hostage_motion_done(obj)) {
            set_motion(obj, 0xFF);
            mem.at<uint8_t>(G::event_flags + 0x90) = 1;
            of<int16_t>(obj, 0x60) = 1;
        }
        break;
    case 1: {
        turn_toward_camera(obj, 0x800);
        turn_toward_camera(obj, 0xC0);
        const float d = floor_distance(of<float>(obj, 0x20), of<float>(obj, 0x28), mem.f32(G::camera_pos),
                                       mem.f32(G::camera_pos + 8));
        if (d < 15.0f) {
            set_motion_blend(obj, 0xEB, 0, 2);
            body_role_model_set(obj, 6, 0x313);
            sound_cue(0xA01418);
            of<int16_t>(obj, 0x60) = 2;
        }
        break;
    }
    case 2:
        if (hostage_motion_done(obj)) {
            const uint32_t t = task_create(F::hostage_gift_task, 0x3C8);
            of<uint32_t>(t, 0x20) = of<uint32_t>(obj, 0x2D4);
            of<uint32_t>(t, 0x24) = of<uint32_t>(obj, 0x2D8);
            of<uint32_t>(t, 0x50) = 0;
            of<uint32_t>(t, 0x28) = of<uint32_t>(obj, 0x2DC);
            const int32_t r = of<int8_t>(obj, 0x2EB);
            if (mem.at<Player>(player_rec(r)).state == 5) {   // 4P: rescuers 3 / 4 too
                int p;
                if (coop4_on()) {
                    of<uint8_t>(t, 0x2EB) = uint8_t(r);
                    p = r;
                } else if ((uint32_t(r) & 0xFF) == 0) {
                    of<uint8_t>(t, 0x2EB) = 0;
                    p = 0;
                } else {
                    of<uint8_t>(t, 0x2EB) = 1;
                    p = 1;
                }
                add_score(p, 400);
            }
            set_motion(obj, 0xEC);
            body_role_model_set(obj, 6, 0x4DB);
            mem.at<uint8_t>(G::scene_state) = 6;
            of<int16_t>(obj, 0x60) = 3;
        } else {
            hostage_lips32(obj, 0x33B, 0x340);
        }
        break;
    case 3:
        if (hostage_motion_done(obj)) {
            hostage_s03_thank_motion(obj);
            hostage_slot_free_inline(obj);
            of<int16_t>(obj, 0x60) = 4;
        } else {
            body_role_model_set(obj, 1, uint32_t(0x340 + (of<int32_t>(obj, 0x50) % 30) / 2));
            of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 1;
        }
        break;
    case 4:
        if (hostage_motion_done(obj)) {
            turn_motion_set(obj, 0xFF, 0, 4);
            of<int16_t>(obj, 0x60) = 5;
            of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) & 0x7F);
        }
        break;
    case 5:
        if (hostage_motion_done(obj)) {
            of<int16_t>(obj, 0x60) = 0;
            of<uint32_t>(obj, 0) = F::hostage_scene_end_frame;
        }
        break;
    default: break;
    }
    enemy_frame_end(obj);
}

// 0x5AFD0
template <class P> void Game<P>::hostage_s03_start(uint32_t obj)
{
    of<uint32_t>(obj, 0) = F::hostage_s03_held;
}

// 0x5AFE0: held: shot (flags bit 8): the effect, the shooter loses a life, scream, gakkuri 0xDC from frame 5
// (NoBias); falling dead (gakkuri with Supplement) y - 0.1 a frame; else between the captor's two hand nodes
// (+0x238, +0x268) through stored matrix 1, 2.5 up, turned a quarter from the captor (the yaw of the hands is
// computed and not used). The captor's throw (MO 0x216 past frame 39): scream, thrown (60 units a second at angle
// 0x1000) -> hostage_s03_thrown; the captor killed (not shootable any more): the rescuer = its killer, dropped to the
// ground -> hostage_s03_dropped
template <class P> void Game<P>::hostage_s03_held(uint32_t obj)
{
    const uint32_t captor = of<uint32_t>(obj, 0x390);
    if ((of<uint16_t>(obj, 0x58) >> 8) & 1) {
        hostage_shot_penalty(obj, true);
        set_motion_blend(obj, 0xDC, 5, 4);
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) | 8);
    }
    if (of<int16_t>(obj, 0xD0) == 0xDC && ((of<uint8_t>(obj, 0x3C6) >> 1) & 1)) {
        of<float>(obj, 0x24) = float(double(of<float>(obj, 0x24)) + ffrom64(0xBFB999999999999Aull));
    } else if (captor != 0) {
        Tgp::Out none, a, b, o;
        const uint32_t one[1] = {1};
        copro(0x74, one, 1, none);
        const uint32_t pa[3] = {of<uint32_t>(captor, 0x238), of<uint32_t>(captor, 0x23C), of<uint32_t>(captor, 0x240)};
        copro(0x1A, pa, 3, a);
        const uint32_t pb[3] = {of<uint32_t>(captor, 0x268), of<uint32_t>(captor, 0x26C), of<uint32_t>(captor, 0x270)};
        copro(0x1A, pb, 3, b);
        copro(0x06, nullptr, 0, none);
        const uint32_t d[3] = {fbits(ffrom(b.w[0]) - ffrom(a.w[0])), fbits(ffrom(b.w[1]) - ffrom(a.w[1])),
                               fbits(ffrom(b.w[2]) - ffrom(a.w[2]))};
        copro(0x0F, d, 3, o);
        if (pc_path()) {   // PC Mode / Boss Mode: THOTD.EXE 0x4ACA2B: the hands through its stored matrix 1 (pmbody)
            float va[3], vb[3];
            pm_m1().transform(of<float>(captor, 0x238), of<float>(captor, 0x23C), of<float>(captor, 0x240), va[0], va[1], va[2]);
            pm_m1().transform(of<float>(captor, 0x268), of<float>(captor, 0x26C), of<float>(captor, 0x270), vb[0], vb[1], vb[2]);
            for (int k = 0; k < 3; ++k) {
                a.w[k] = fbits(va[k]);
                b.w[k] = fbits(vb[k]);
            }
        }
        const float y = (ffrom(a.w[1]) + ffrom(b.w[1])) * 0.5f + 2.5f;
        of<int32_t>(obj, 0x2C) = 0;
        of<float>(obj, 0x20) = (ffrom(a.w[0]) + ffrom(b.w[0])) * 0.5f;
        of<float>(obj, 0x28) = (ffrom(a.w[2]) + ffrom(b.w[2])) * 0.5f;
        of<float>(obj, 0x24) = y;
        of<int32_t>(obj, 0x34) = 0;
        of<int32_t>(obj, 0x30) = of<int32_t>(captor, 0x30) + 0x4000;
    }
    shadow_setup(mem.f32(G::floor_y));
    object_draw(obj);
    if (!(of<int16_t>(obj, 0xD0) == 0xDC && hostage_motion_done(obj)))
        of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
    if (of<int16_t>(captor, 0xD0) == 0x216 && of<int16_t>(captor, 0xF4) > 39) {
        researcher_scream_penalty(obj, hostage_is_female(obj), 0);
        mem.set32(G::dog_jump_speed, fbits(60.0f));
        mem.set32(G::hostage_throw_angle, 0x1000);
        Tgp::Out s, c;
        const uint32_t ang[1] = {mem.u32(G::hostage_throw_angle)};
        copro(0x1C, ang, 1, s);
        of<float>(obj, 0x78) = ffrom(s.w[0]) * mem.f32(G::dog_jump_speed) * ffrom(0x3C888F86);
        const uint32_t ang2[1] = {mem.u32(G::hostage_throw_angle)};
        copro(0x1B, ang2, 1, c);
        of<uint32_t>(obj, 0) = F::hostage_s03_thrown;
        of<float>(obj, 0x7C) = ffrom(c.w[0]) * mem.f32(G::dog_jump_speed) * ffrom(0x3C888F86);
    }
    if (captor != 0 && ((of<uint8_t>(captor, 0x58) & 1) == 0)) {
        of<uint32_t>(obj, 0x78) = 0;
        of<uint32_t>(obj, 0x7C) = 0;
        of<uint8_t>(obj, 0x2EB) = of<uint8_t>(captor, 0x2EB);
        LocalScope scope(*this);
        const uint32_t pt = local_alloc(12);
        mem.set32(pt, of<uint32_t>(obj, 0x20));
        mem.set32(pt + 4, fbits(32768.0f));
        mem.set32(pt + 8, of<uint32_t>(obj, 0x28));
        mem.set32(G::floor_y, fbits(ground_height(pt)));
        of<uint32_t>(obj, 0) = F::hostage_s03_dropped;
    }
    depth_list_add(obj);
}

// 0x5B4A0 (end routine, record +0x70 bit 14): once the camera moves on, hostage_end_countdown
template <class P> void Game<P>::hostage_end_wait(uint32_t obj)
{
    if (mem.u32(G::camera_stopped) != 2)
        of<uint32_t>(obj, 0x36C) = F::hostage_end_countdown;
}

// 0x5B4C0: +0x70 frames, then removed
template <class P> void Game<P>::hostage_end_countdown(uint32_t obj)
{
    const int16_t p = of<int16_t>(obj, 0x70);
    of<int16_t>(obj, 0x70) = int16_t(p - 1);
    if (p > 0)
        return;
    mem.at<uint16_t>(G::scene_count_520794) = uint16_t(mem.at<uint16_t>(G::scene_count_520794) - 1);
    body_free_parts(obj);
    task_exit();
}

// 0x5B500 (end routine): removed once camera_frame reaches +0x70
template <class P> void Game<P>::hostage_end_remove(uint32_t obj)
{
    if (float(of<int16_t>(obj, 0x70)) > mem.f32(G::camera_frame))
        return;
    mem.at<uint16_t>(G::scene_count_520794) = uint16_t(mem.at<uint16_t>(G::scene_count_520794) - 1);
    body_free_parts(obj);
    task_exit();
}

// 0x5B540: the end routine (+0x36C)
template <class P> void Game<P>::hostage_end(uint32_t obj)
{
    hostage_call(of<uint32_t>(obj, 0x36C), obj);
}

// 0x5B550: shot by a player (flags bit 8 with bit 1 / 2): the penalty, the reaction by the state (9 hanging: turned,
// MO 0xBD, off the slot; 0 MO 0xDD; 20 MO 0xFC -> hostage_shot_fall; 19 MO 0x106; else by the camera's side
// 0x11E / 0x11D / 0x119) -> hostage_s01_run_task
template <class P> void Game<P>::hostage_shot(uint32_t obj)
{
    if (((of<uint16_t>(obj, 0x58) >> 8) & 1) == 0 || (of<uint8_t>(obj, 0x58) & 6) == 0)
        return;
    hostage_shot_penalty(obj);
    const int32_t a = int32_t(angle_to_camera(obj));
    switch (of<int16_t>(obj, 0x5C)) {
    case 9:
        hostage_slot_free_inline(obj);
        of<uint32_t>(obj, 0x30) = of<uint32_t>(obj, 0x30) ^ 0x8000u;
        set_motion(obj, 0xBD);
        break;
    case 0:
        set_motion_blend(obj, 0xDD, 0, 4);
        break;
    case 20:
        of<uint16_t>(obj, 0x6C) = uint16_t(of<uint16_t>(obj, 0x6C) & ~2u);
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~8u);
        set_motion_blend(obj, 0xFC, 0, 4);
        of<uint32_t>(obj, 0) = F::hostage_shot_fall;
        return;
    case 19:
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~8u);
        set_motion_blend(obj, 0x106, 0, 4);
        break;
    default:
        if (a > 0x8FFF && a <= 0xE7FF)
            set_motion_blend(obj, 0x11E, 26, 8);
        else if (a > 0x17FF && a <= 0x6FFF)
            set_motion_blend(obj, 0x11D, 16, 8);
        else
            set_motion_blend(obj, 0x119, 16, 8);
        break;
    }
    of<uint32_t>(obj, 0) = F::hostage_s01_run_task;
}

// 0x5B7D0: as hostage_shot for a researcher counted as an enemy (scenario 5): also off the enemy counts; state 2
// MO 0x11B from frame 45 (else 0x11F); sides 0x11E / 0x11D / 0x119 by other angles
template <class P> void Game<P>::researcher_shot_by_player(uint32_t obj)
{
    if (((of<uint16_t>(obj, 0x58) >> 8) & 1) == 0 || (of<uint8_t>(obj, 0x58) & 6) == 0)
        return;
    const uint32_t t = task_create(F::hostage_shot_effect, 0x3C8);
    const uint16_t alive = mem.at<uint16_t>(G::enemies_alive);
    of<uint32_t>(t, 0x20) = of<uint32_t>(obj, 0x2D4);
    const uint16_t count = mem.at<uint16_t>(G::enemy_count);
    of<uint32_t>(t, 0x24) = of<uint32_t>(obj, 0x2D8);
    of<uint32_t>(t, 0x50) = 0;
    of<uint32_t>(t, 0x28) = of<uint32_t>(obj, 0x2DC);
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(alive - 1);
    mem.at<uint16_t>(G::enemy_count) = uint16_t(count - 1);
    if ((of<uint32_t>(obj, 0x58) & 6) == 6) {
        const int32_t r = int32_t(rand());
        if (r % 2 != 0)
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~2u;
        else
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~4u;
    }
    pm_shot_bit_drop(obj);
    if (coop4_on()) {   // 4P: the real shooter of player 1's / 2's bit (the hit table)
        const int who = coop4_shooter(obj, ((of<uint8_t>(obj, 0x58) >> 1) & 1) ? 0 : 1);
        player_hit_lose_life(who, 0, 0, 1, -1);
        of<uint8_t>(t, 0x2EB) = uint8_t(who);
    } else if ((of<uint8_t>(obj, 0x58) >> 1) & 1) {
        player_hit_lose_life(0, 0, 0, 1, -1);
        of<uint8_t>(t, 0x2EB) = 0;
    } else {
        player_hit_lose_life(1, 0, 0, 1, -1);
        of<uint8_t>(t, 0x2EB) = 1;
    }
    researcher_scream_penalty(obj, hostage_is_female(obj), 1);
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xFFFFFEFEu;
    const int32_t a = int32_t(angle_to_camera(obj));
    switch (of<int16_t>(obj, 0x5C)) {
    case 2:
        if (of<int16_t>(obj, 0xF4) > 44)
            set_motion_blend(obj, 0x11B, 30, 12);
        else
            set_motion_blend(obj, 0x11F, 0, 12);
        break;
    case 0:
        set_motion_blend(obj, 0xDD, 0, 4);
        break;
    case 9:
        hostage_slot_free_inline(obj);
        of<uint32_t>(obj, 0x30) = of<uint32_t>(obj, 0x30) ^ 0x8000u;
        set_motion(obj, 0xBD);
        break;
    default:
        if (a > 0x9FFF && a <= 0xDFFF)
            set_motion_blend(obj, 0x11E, 26, 8);
        else if (a > 0x1FFF && a <= 0x5FFF)
            set_motion_blend(obj, 0x11D, 16, 8);
        else
            set_motion_blend(obj, 0x119, 16, 8);
        break;
    }
    of<uint32_t>(obj, 0) = F::hostage_s01_run_task;
}

// 0x5BA40: struck (flags bit 28): a hit effect at node +0x1A8 (4 up; not in the opening scene's state 4), the scream,
// not shootable, the reaction by the state (0 MO 0xDD; 4 the opening's donyu 0x297 from frame 15, else 0x119; 9 turned,
// MO 0xBD; 19 MO 0x106; 20 MO 0xFC -> hostage_shot_fall; else 0xD3, 0x119 in Chapter 3 event 2) ->
// hostage_struck_wait
template <class P> void Game<P>::hostage_struck(uint32_t obj)
{
    if (((of<uint32_t>(obj, 0x58) >> 28) & 1) == 0)
        return;
    const bool opening = mem.at<uint8_t>(G::chapter) == 0 && mem.at<uint8_t>(G::event_index) == 0;
    if (!(opening && of<uint16_t>(obj, 0x5C) == 4)) {
        Tgp::Out none, o;
        const uint32_t one[1] = {1};
        copro(0x74, one, 1, none);
        const uint32_t p[3] = {of<uint32_t>(obj, 0x1A8), of<uint32_t>(obj, 0x1AC), of<uint32_t>(obj, 0x1B0)};
        copro(0x1A, p, 3, o);
        const float z = ffrom(o.w[2]) + 4.0f;
        copro(0x06, nullptr, 0, none);
        hit_effect_at(ffrom(o.w[0]), ffrom(o.w[1]), z);
    }
    researcher_scream_penalty(obj, hostage_is_female(obj), 0);
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
    of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~1u);
    switch (of<int16_t>(obj, 0x5C)) {
    case 0:
        set_motion_blend(obj, 0xDD, 0, 4);
        break;
    case 4:
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~8u);
        if (mem.at<uint8_t>(G::chapter) == 0 && mem.at<uint8_t>(G::event_index) == 0) {
            set_motion_blend(obj, 0x297, 16, 8);
            of<int32_t>(obj, 0xF8) = 15;
        } else {
            set_motion_blend(obj, 0x119, 0, 4);
        }
        break;
    case 9:
        of<uint32_t>(obj, 0x30) = of<uint32_t>(obj, 0x30) ^ 0x8000u;
        set_motion(obj, 0xBD);
        hostage_slot_free_inline(obj);
        break;
    case 19:
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~8u);
        set_motion_blend(obj, 0x106, 0, 4);
        break;
    case 20:
        of<uint16_t>(obj, 0x6C) = uint16_t(of<uint16_t>(obj, 0x6C) & ~2u);
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~8u);
        set_motion_blend(obj, 0xFC, 0, 4);
        hostage_slot_free_inline(obj);
        of<uint32_t>(obj, 0) = F::hostage_shot_fall;
        return;
    default:
        if (mem.at<uint8_t>(G::chapter) == 2 && mem.at<uint8_t>(G::event_index) == 2)
            set_motion_blend(obj, 0x119, 0, 4);
        else
            set_motion_blend(obj, 0xD3, 16, 8);
        break;
    }
    of<uint32_t>(obj, 0) = F::hostage_struck_wait;
}

// 0x5BD30: 1 when the captor (+0x390) is gone: none, or not shootable / ended (the rescuer = its killer); another
// creature of the enemy slots holding him (+0x390 = this one, shootable) becomes the captor (0)
template <class P> uint32_t Game<P>::hostage_captor_gone(uint32_t obj)
{
    const uint32_t c = of<uint32_t>(obj, 0x390);
    if (c == 0)
        return 1;
    if ((of<uint8_t>(c, 0x58) & 1) != 0 && of<uint32_t>(c, 0xC) != 0)
        return 0;
    of<uint8_t>(obj, 0x2EB) = of<uint8_t>(c, 0x2EB);
    of<uint32_t>(c, 0x390) = 0;
    for (uint32_t i = 0; i < 14; ++i) {
        const uint32_t o = mem.u32(G::player_bodies + 4 * i);
        if (o != 0 && of<uint32_t>(o, 0x390) == obj && (of<uint8_t>(o, 0x58) & 1) != 0) {
            of<uint32_t>(obj, 0x390) = o;
            return 0;
        }
    }
    of<uint32_t>(obj, 0x390) = 0;
    return 1;
}

// 0x5C3A0: the struck reaction plays (to its end, or frame 76 of state 9); then off the slot: action 0 falls
// (MO 0x176, turned) -> hostage_struck_fall, else hostage_scene_end_frame
template <class P> void Game<P>::hostage_struck_wait(uint32_t obj)
{
    shadow_setup(mem.f32(G::floor_y));
    object_draw(obj);
    if (hostage_motion_done(obj) || (of<uint16_t>(obj, 0x5C) == 9 && of<uint16_t>(obj, 0xF4) == 0x4C)) {
        hostage_slot_free_inline(obj);
        if (of<uint16_t>(obj, 0x64) == 0) {
            set_motion(obj, 0x176);
            of<int16_t>(obj, 0x60) = 0;
            of<uint32_t>(obj, 0) = F::hostage_struck_fall;
            of<uint32_t>(obj, 0x30) = of<uint32_t>(obj, 0x30) ^ 0x8000u;
        } else {
            of<int16_t>(obj, 0x60) = 0;
            of<uint32_t>(obj, 0) = F::hostage_scene_end_frame;
        }
        return;
    }
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x5C460: dead on the floor until the motion ends
template <class P> void Game<P>::hostage_s03_dead(uint32_t obj)
{
    shadow_setup(mem.f32(G::floor_y));
    object_draw(obj);
    if (hostage_motion_done(obj)) {
        of<int16_t>(obj, 0x60) = 0;
        of<uint32_t>(obj, 0) = F::hostage_scene_end_frame;
        return;
    }
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x5C4B0: falls to the floor; shot dead (gakkuri 0xDC): event flag 0x90, hostage_s03_dead; else gets up (haitati
// 0xDE), route_choice 0, counted (scene_state 5 while shootable), hostage_s03_rescue
template <class P> void Game<P>::hostage_s03_dropped(uint32_t obj)
{
    body_draw(obj);
    const float floor = mem.f32(G::floor_y);
    const bool landed = of<float>(obj, 0x24) + of<float>(obj, 0x7C) < floor;
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
    if (!landed) {
        hostage_fall_step(obj);
        return;
    }
    of<float>(obj, 0x24) = floor;
    if (of<int16_t>(obj, 0xD0) == 0xDC) {
        mem.at<uint8_t>(G::event_flags + 0x90) = 1;
        of<uint32_t>(obj, 0) = F::hostage_s03_dead;
        return;
    }
    set_motion_blend(obj, 0xDE, 30, 16);
    mem.at<uint8_t>(G::route_choice) = 0;
    hostage_count_rescue();
    if (of<uint8_t>(obj, 0x58) & 1)
        mem.at<uint8_t>(G::scene_state) = 5;
    of<int16_t>(obj, 0x60) = 0;
    of<uint32_t>(obj, 0) = F::hostage_s03_rescue;
}

// 0x5C770: after climbing up: MO 0x112, then the end routine
template <class P> void Game<P>::hostage_thank_leave_step(uint32_t obj)
{
    const int16_t sub = of<int16_t>(obj, 0x60);
    if (sub == 0) {
        if (hostage_motion_done(obj)) {
            set_motion_blend(obj, 0x112, 0, 8);
            of<int16_t>(obj, 0x60) = 1;
        }
    } else if (sub == 1) {
        hostage_end(obj);
    }
    enemy_frame_end(obj);
}

// 0x5C7C0: slides down (y - 0.4, x - 0.6 a frame, in double precision) until 30 below the floor
template <class P> void Game<P>::hostage_shot_slide(uint32_t obj)
{
    shadow_setup(mem.f32(G::floor_y));
    object_draw(obj);
    of<float>(obj, 0x24) = float(double(of<float>(obj, 0x24)) - ffrom64(0x3FD999999999999Aull));
    of<float>(obj, 0x20) = float(double(of<float>(obj, 0x20)) - ffrom64(0x3FE3333333333333ull));
    if (of<float>(obj, 0x24) < mem.f32(G::floor_y) - 30.0f) {
        of<int16_t>(obj, 0x60) = 0;
        of<uint32_t>(obj, 0) = F::hostage_scene_end_frame;
    }
}

// 0x5C870: the shot fall plays, then hostage_shot_slide
template <class P> void Game<P>::hostage_shot_fall(uint32_t obj)
{
    shadow_setup(mem.f32(G::floor_y));
    object_draw(obj);
    if (hostage_motion_done(obj)) {
        of<uint32_t>(obj, 0) = F::hostage_shot_slide;
        return;
    }
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
}

// 0x1E200 (a task at the researcher's home position): the gift for the rescuer (+0x2EB; neither player: one at
// random): an extra life (player_life_add -> hostage_gift_fly), at full lives 300 points instead
template <class P> void Game<P>::hostage_gift_task(uint32_t task)
{
    const uint8_t r = of<uint8_t>(task, 0x2EB);
    if (r != 0 && r != 1 && !(coop4_on() && (r == 2 || r == 3)))   // 4P: players 3 / 4's gifts
        of<uint8_t>(task, 0x2EB) = uint8_t(int32_t(rand()) % 2);
    const int32_t p = of<int8_t>(task, 0x2EB);
    if (mem.at<Player>(player_rec(p)).state == 5) {
        if (player_life_add(p) != 0) {
            of<uint32_t>(task, 0) = F::hostage_gift_fly;
            return;
        }
        add_score(of<int8_t>(task, 0x2EB), 300);
    }
    task_exit();
}

// 0x1E040: the extra-life item (model 0x3BB / 0x3BC by player) drawn at its place in camera space (2 down; depth
// kept between 10 and 50, focal 496, scale 0.0012), blinking from frame 40, gone after 60
template <class P> void Game<P>::hostage_gift_fly(uint32_t task)
{
    gift_fly_step(task, of<uint8_t>(task, 0x2EB) == 0 ? 0x3BB : 0x3BC);   // (4P: players 3 / 4 in player 2's, ramped by the view)
}

// 0x1DE90 gift_effect (a 0x58-byte task at a rescued researcher, researcher_gift_spawn): the same flight and blink as
// hostage_gift_fly, always the player 1 item 0x3BB
template <class P> void Game<P>::gift_effect(uint32_t task)
{
    gift_fly_step(task, 0x3BB);
}

// the shared body of 0x1E040 / 0x1DE90 (inline twice in the original)
template <class P> void Game<P>::gift_fly_step(uint32_t task, uint32_t model)
{
    const int32_t t = of<int32_t>(task, 0x50);
    const bool draw = t <= 39 || (t <= 59 && t % 2 != 0);
    if (draw) {
        Tgp::Out o, none;
        const uint32_t p[3] = {of<uint32_t>(task, 0x20), fbits(of<float>(task, 0x24) - 2.0f), of<uint32_t>(task, 0x28)};
        copro(0x1A, p, 3, o);
        of<uint32_t>(task, 0x38) = o.w[0];
        of<uint32_t>(task, 0x3C) = o.w[1];
        float vx = of<float>(task, 0x38), vy = of<float>(task, 0x3C), vz = ffrom(o.w[2]);
        if (!(vz < 50.0f)) {
            const float k = 50.0f / vz;
            vx = k * vx;
            vy = k * vy;
            vz = k * vz;
        } else if (!(vz > 10.0f)) {
            const float k = 10.0f / vz;
            vx = k * vx;
            vy = k * vy;
            vz = k * vz;
        }
        const float sx = vx / vz;
        const float s = (496.0f / vz) * ffrom(0x3A9D4952);
        const float sy = vy / vz;
        of<float>(task, 0x38) = vx;
        of<float>(task, 0x3C) = vy;
        of<float>(task, 0x40) = vz;
        copro(0x21, nullptr, 0, none);
        const uint32_t tr[3] = {fbits(sx), fbits(sy), fbits(1.0f)};
        copro(0x12, tr, 3, none);
        const uint32_t r[1] = {0x800};
        copro(0x14, r, 1, none);
        const uint32_t sc[3] = {fbits(s), fbits(s), fbits(s)};
        copro(0x13, sc, 3, none);
        geo_draw_model(model);
        copro(0x06, nullptr, 0, none);
    } else if (t > 59) {
        task_exit();
        return;
    }
    of<int32_t>(task, 0x50) = of<int32_t>(task, 0x50) + 1;
}

} // namespace hotd::port
