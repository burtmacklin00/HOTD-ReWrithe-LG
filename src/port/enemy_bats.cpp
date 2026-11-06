// Enemies: kinds 66-68, the bats (arcade cluster 0x832C0..0x85430), read from the arcade disassembly (the typed
// decompilation garbles their coprocessor sequences). A bat's frame (bat_frame; the Hangedman's swarm: bat_swarm_frame)
// runs a state routine (bat_states 0xC9D80, by +0x5C): 1 hanging, 2 taking off, 3 circling, 4 diving at a player, 5
// shot, 6 falling dead, 9 / 10 the swarm's circling and dive. Set-ups (bat_setups 0xC9DB0) by the record action
// (kind 66), the record +0x70 (kind 67) or always 0 (kind 68). Words: +0x394.. the circle's centre, +0x30C / +0x310
// its radii (x / z), +0x44 the circle angle and +0x2F8 its step, +0x3A0.. the last position, +0x3AC.. where the dive
// started, +0x3B8.. the dive's target point (in front of the player's view), +0x2EC a countdown before diving.
// Wing frames: models 0x54..0x67 (at 0x54 and 0x5E the bob +0x88 changes sign).
#include "port/game.h"

namespace hotd::port {

// a call through bat_setups / bat_states: the ported routines directly, anything else as a boundary
template <class P> void Game<P>::bat_call(uint32_t routine, uint32_t obj)
{
    switch (routine) {
    case F::bat_st0_idle: return;
    case F::bat_st1_hanging: bat_st1_hanging(obj); return;
    case F::bat_st2_take_off: bat_st2_take_off(obj); return;
    case F::bat_st3_circle: bat_st3_circle(obj); return;
    case F::bat_st4_dive: bat_st4_dive(obj); return;
    case F::bat_st5_shot: bat_st5_shot(obj); return;
    case F::bat_st6_fall: bat_st6_fall(obj); return;
    case F::bat_setup_flying: bat_setup_flying(obj); return;
    case F::bat_setup_group5: bat_setup_group5(); return;
    case F::bat_setup_flying_low: bat_setup_flying_low(obj); return;
    case F::bat_setup_hangedman_swarm: bat_setup_hangedman_swarm(); return;
    case F::bat_st9_swarm_circle: bat_st9_swarm_circle(obj); return;
    case F::bat_st10_swarm_dive: bat_st10_swarm_dive(obj); return;
    default: call_routine(routine, obj); return;
    }
}

// 0x83330 (kind 66): sub-step 0, the set-up of the record's action
template <class P> void Game<P>::kind66_bat(uint32_t obj)
{
    const int32_t action = of<int16_t>(obj, 0x64);
    of<int16_t>(obj, 0x60) = 0;
    bat_call(mem.u32(G::bat_setups + uint32_t(action) * 4), obj);
}

// 0x83350 (kind 67, the Hangedman's swarm): the set-up by +0x70, then the spawner ends
template <class P> void Game<P>::kind67_bat_swarm(uint32_t obj)
{
    const int32_t n = of<int16_t>(obj, 0x70);
    of<int16_t>(obj, 0x60) = 0;
    bat_call(mem.u32(G::bat_setups + uint32_t(n) * 4), obj);
    task_exit();
}

// 0x83370 (kind 68): its record is the owner's (+0x35C); +0x2EC and the action from the record's first two halves;
// set-up 0
template <class P> void Game<P>::kind68_bat(uint32_t obj)
{
    const uint32_t r = of<uint32_t>(obj, 0x35C);
    of<uint32_t>(obj, 0x368) = r;
    of<int32_t>(obj, 0x2EC) = int32_t(mem.at<int16_t>(r));
    of<int16_t>(obj, 0x64) = mem.at<int16_t>(r + 2);
    bat_setup_flying(obj);
}

// the hit check of both frames: shot (flags bit 8) or return_false: the state and step saved in +0x5E / +0x62,
// state 5
template <class P> void Game<P>::bat_hit_check(uint32_t obj)
{
    if (((of<uint16_t>(obj, 0x58) >> 8) & 1) == 0 && return_false() == 0)
        return;
    const int16_t sub = of<int16_t>(obj, 0x60);
    of<int16_t>(obj, 0x60) = 0;
    const int16_t st = of<int16_t>(obj, 0x5C);
    of<int16_t>(obj, 0x5C) = 5;
    of<int16_t>(obj, 0x5E) = st;
    of<int16_t>(obj, 0x62) = sub;
}

// 0x83390: hit check; at the end of chapter 1 event 9 (event flag 0x31) every bat falls (state 6 step 2: removed);
// the state routine; its shadow on the ground below; drawn faded (+0x2E0) at its position and angles, scale 1; home
// position = position; depth list, attacker list unless flags bit 12
template <class P> void Game<P>::bat_frame(uint32_t obj)
{
    of<int16_t>(obj, 0x2E0) = 0xC8;
    bat_hit_check(obj);
    if (mem.at<uint8_t>(G::chapter) == 0 && mem.at<uint8_t>(G::event_index) == 9 &&
        mem.at<uint8_t>(G::event_flags + 0x31) == 1) {
        of<int16_t>(obj, 0x5C) = 6;
        of<int16_t>(obj, 0x60) = 2;
    }
    bat_call(mem.u32(G::bat_states + uint32_t(int32_t(of<int16_t>(obj, 0x5C))) * 4), obj);
    {
        LocalScope ls(*this);
        const uint32_t q = local_alloc(12);   // the probe point (fp + 0x40): x, 32768, z
        mem.set32(q, of<uint32_t>(obj, 0x20));
        mem.set32(q + 4, 0x47000000u);
        mem.set32(q + 8, of<uint32_t>(obj, 0x28));
        shadow_setup(ground_height(q));
    }
    if constexpr (P::pc) {
        // PC: the software matrix stack (the bats file's copies), the draw-state calls empty; the model only with the
        // option pc_draw_bats, through stored matrix 3 (no scale, no coprocessor function 0x77)
        pc_matrix_push();
        pc_matrix_translate6(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28));
        pc_matrix_rotate_y6(of<uint32_t>(obj, 0x30));
        pc_matrix_rotate_x2(of<uint32_t>(obj, 0x2C));
        pc_matrix_rotate_b3(of<uint32_t>(obj, 0x34));
        pc_draw_state(int32_t(of<int16_t>(obj, 0x2E0)));
        pc_matrix_position_draw(of<uint32_t>(obj, 0x54), obj + ob(0x38));
        if (mem.u32(G::pc_draw_bats) != 0) {
            pc_matrix_mul_stored(G::pc_stored_matrices + 0x90);
            pc_draw_state(0);
            geo_draw_model(of<uint32_t>(obj, 0x54));
            pc_draw_state(0);
        }
        pcm_pop();
        of<uint32_t>(obj, 0x2D4) = of<uint32_t>(obj, 0x20);
        of<uint32_t>(obj, 0x2D8) = of<uint32_t>(obj, 0x24);
        of<uint32_t>(obj, 0x2DC) = of<uint32_t>(obj, 0x28);
        depth_list_add(obj);
        if (((of<uint16_t>(obj, 0x58) >> 12) & 1) == 0)
            attacker_list_add(obj);
        return;
    }
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
    draw_fade_begin(int32_t(of<int16_t>(obj, 0x2E0)));
    geo_matrix_position(of<uint32_t>(obj, 0x54), obj + ob(0x38));
    const uint32_t three[1] = {3};
    copro(0x77, three, 1, none);
    draw_fade_begin(0);
    geo_draw_model(of<uint32_t>(obj, 0x54));
    geo_send_words(0, 31, 0x7A0);
    copro(0x06, nullptr, 0, none);
    of<uint32_t>(obj, 0x2D4) = of<uint32_t>(obj, 0x20);
    of<uint32_t>(obj, 0x2D8) = of<uint32_t>(obj, 0x24);
    of<uint32_t>(obj, 0x2DC) = of<uint32_t>(obj, 0x28);
    depth_list_add(obj);
    if (((of<uint16_t>(obj, 0x58) >> 12) & 1) == 0)
        attacker_list_add(obj);
}

// 0x83590 (state 1): hanging (model 0xA72); now and then (rand bit 6 clear) it shakes: models 0xA71..0xA84 with flags
// bit 5, then 0xA72 for (rand & 7) x 10 frames
template <class P> void Game<P>::bat_st1_hanging(uint32_t obj)
{
    int16_t sub = of<int16_t>(obj, 0x60);
    if (sub == 0) {
        of<uint32_t>(obj, 0x54) = 0xA72;
        of<int16_t>(obj, 0x60) = int16_t(sub + 1);
        sub = 1;
    } else if (sub < 0 || sub > 3) {
        return;
    }
    if (sub == 1) {
        if ((rand() >> 6) & 1)
            return;
        of<uint32_t>(obj, 0x54) = 0xA71;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x20;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        sub = 2;
    }
    if (sub == 2) {
        const int32_t m = of<int32_t>(obj, 0x54) + 1;
        of<int32_t>(obj, 0x54) = m;
        if (!(m > 0xA84))
            return;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x20u;
        of<int32_t>(obj, 0x50) = int32_t((rand() & 7) * 10);
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    }
    of<uint32_t>(obj, 0x54) = 0xA72;
    const int32_t t = of<int32_t>(obj, 0x50) - 1;
    of<int32_t>(obj, 0x50) = t;
    if (!(0 < t))
        of<int16_t>(obj, 0x60) = 0;
}

// 0x83660 (state 2): counted as an enemy; drops (gravity -0.027222222 - (rand & 7) x 0.0065) for (rand & 7) + 4
// frames (model 100), then three wing beats (a model every third frame), then circles (state 3, a random wing frame)
template <class P> void Game<P>::bat_st2_take_off(uint32_t obj)
{
    int16_t sub = of<int16_t>(obj, 0x60);
    if (sub < 0 || sub > 2)
        return;
    if (sub == 0) {
        mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
        of<float>(obj, 0x7C) = 0.0f;
        const float r = float(int32_t(rand() & 7));
        const float a = r * ffrom(0xBBD4FDF4u) + ffrom(0xBCDF0123u);
        of<int32_t>(obj, 0x50) = 0;
        of<uint32_t>(obj, 0x54) = 100;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        of<float>(obj, 0x88) = a;
        sub = 1;
    }
    if (sub == 1) {
        const float v = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
        const float y = of<float>(obj, 0x24) + v;
        const int32_t t = of<int32_t>(obj, 0x50);
        of<int32_t>(obj, 0x50) = t + 1;
        of<float>(obj, 0x7C) = v;
        of<float>(obj, 0x24) = y;
        if (t < int32_t((rand() & 7) + 4))
            return;
        of<int32_t>(obj, 0x2EC) = 0;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    }
    const float v = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
    const float y = of<float>(obj, 0x24) + v;
    int32_t c = of<int32_t>(obj, 0x2EC);
    int32_t beats = of<int32_t>(obj, 0x2F0);
    const bool wrap = c == 2;
    c = c + 1;
    of<float>(obj, 0x7C) = v;
    of<float>(obj, 0x24) = y;
    if (wrap) {
        c = 0;
        of<int32_t>(obj, 0x54) = of<int32_t>(obj, 0x54) + 1;
        beats = beats + 1;
    }
    of<int32_t>(obj, 0x2EC) = c;
    of<int32_t>(obj, 0x2F0) = beats;
    if (beats == 3) {
        of<int32_t>(obj, 0x2C) = 0;
        const uint32_t r = rand();
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 3;
        of<uint32_t>(obj, 0x54) = (r & 7) * 2 + 0x54;
    }
}

// the next wing frame: models 0x54..0x67
template <class P> void Game<P>::bat_wing(uint32_t obj)
{
    const int32_t m = of<int32_t>(obj, 0x54);
    of<int32_t>(obj, 0x54) = m > 0x66 ? 0x54 : m + 1;
}

// the bob: at wing frames 0x54 / 0x5E the bob speed +0x88 changes sign; y + the bob speed
template <class P> void Game<P>::bat_bob(uint32_t obj)
{
    if (pc_path()) {
        (void)obj;   // PC: the bats do not bob while circling or diving (+0x88 is set but unused)
    } else {
        const int32_t m = of<int32_t>(obj, 0x54);
        uint32_t a = of<uint32_t>(obj, 0x88);
        if (m == 0x54 || m == 0x5E)
            a ^= 0x80000000u;
        of<uint32_t>(obj, 0x88) = a;
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + ffrom(a);
    }
}

// the circle step of states 3 / 9: the circle angle (+0x44) + its step; x / z on the circle (coprocessor sine /
// cosine 0x1B / 0x1C, radii +0x30C / +0x310 around +0x394 / +0x39C); heading along the way moved (from +0x3A0..)
template <class P> void Game<P>::bat_circle_step(uint32_t obj)
{
    const uint32_t ang = of<uint32_t>(obj, 0x44) + of<uint32_t>(obj, 0x2F8);
    of<uint32_t>(obj, 0x44) = ang;
    if (pc_path()) {
        // PC: sine / cosine from pc_trig_table, the heading by the file's pc_vec_angles copy
        of<float>(obj, 0x20) = pc_sin2(ang) * of<float>(obj, 0x30C) + of<float>(obj, 0x394);
        const float z = pc_cos2(of<uint32_t>(obj, 0x44)) * of<float>(obj, 0x310) + of<float>(obj, 0x39C);
        const float dx = of<float>(obj, 0x20) - of<float>(obj, 0x3A0);
        const float dy = of<float>(obj, 0x24) - of<float>(obj, 0x3A4);
        of<float>(obj, 0x28) = z;
        const float dz = z - of<float>(obj, 0x3A8);
        uint32_t yaw = 0, pitch = 0;
        pc_vec_angles7(dx, dy, dz, yaw, pitch);
        of<uint32_t>(obj, 0x30) = yaw;
        return;
    }
    Tgp::Out o;
    const uint32_t a1[1] = {ang};
    copro(0x1B, a1, 1, o);
    const float x = ffrom(o.w[0]) * of<float>(obj, 0x30C) + of<float>(obj, 0x394);
    of<float>(obj, 0x20) = x;
    Tgp::Out o2;
    const uint32_t a2[1] = {of<uint32_t>(obj, 0x44)};
    copro(0x1C, a2, 1, o2);
    const float z = ffrom(o2.w[0]) * of<float>(obj, 0x310) + of<float>(obj, 0x39C);
    const float dx = of<float>(obj, 0x20) - of<float>(obj, 0x3A0);
    const float dy = of<float>(obj, 0x24) - of<float>(obj, 0x3A4);
    of<float>(obj, 0x28) = z;
    const float dz = z - of<float>(obj, 0x3A8);
    of<int32_t>(obj, 0x30) = int32_t(yaw_toward_a(dx, dy, dz));
}

// 0x837C0 (state 3): circles (step 0: bob 0.3, the last position, heading 0, timer 0); action 3: pick_target_player_any
// and dive once +0x2EC runs out; others (not flags bit 12): once +0x2EC runs out and fewer than 3 bats attack,
// pick_target_player (marked targeted; none: +0x2EC 0). Diving: one more attacker, a front attack slot, state 4
template <class P> void Game<P>::bat_st3_circle(uint32_t obj)
{
    bat_wing(obj);
    const int16_t sub = of<int16_t>(obj, 0x60);
    if (sub == 0) {
        of<uint32_t>(obj, 0x88) = 0x3E99999Au;   // 0.3
        of<uint32_t>(obj, 0x3A0) = of<uint32_t>(obj, 0x20);
        of<uint32_t>(obj, 0x3A4) = of<uint32_t>(obj, 0x24);
        of<int32_t>(obj, 0x30) = 0;
        of<int32_t>(obj, 0x50) = 0;
        of<int16_t>(obj, 0x60) = int16_t(sub + 1);
        of<uint32_t>(obj, 0x3A8) = of<uint32_t>(obj, 0x28);
    } else if (sub != 1) {
        return;
    }
    bat_circle_step(obj);
    of<uint32_t>(obj, 0x3A0) = of<uint32_t>(obj, 0x20);
    of<uint32_t>(obj, 0x3A8) = of<uint32_t>(obj, 0x28);
    of<uint32_t>(obj, 0x3A4) = of<uint32_t>(obj, 0x24);
    bat_bob(obj);
    if (of<int16_t>(obj, 0x64) == 3) {
        pick_target_player_any(obj);
        if (of<uint8_t>(obj, 0x2E8) == 0xFF)
            return;
        const int32_t c = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = c;
        if (!(c < 0))
            return;
        // (ours, totd8: The Typing of the Dead 0's attack window, game.h: the dive waits as the others' do; null in
        // the arcade. Up to totd7 these bats dove on their own timing and hit before their key's grace ran out)
        if (gAttackHold && gAttackHold(obj)) {
            of<int32_t>(obj, 0x2EC) = 0;
            return;
        }
    } else {
        if ((of<uint16_t>(obj, 0x58) >> 12) & 1)
            return;
        const int32_t c = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = c;
        if (0 <= c)
            return;
        if (!(int32_t(mem.u32(G::bats_attacking)) <= 2))
            return;
        pick_target_player(obj);
        mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(of<int8_t>(obj, 0x2E8)))) = 1;
        if (of<uint8_t>(obj, 0x2E8) == 0xFF) {
            of<int32_t>(obj, 0x2EC) = 0;
            return;
        }
    }
    mem.set32(G::bats_attacking, mem.u32(G::bats_attacking) + 1);
    attack_slot_front_take(obj);
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 4;
}

// the dive's target point (+0x3B8..): 3 in front of the target player's view (x 124 to the side of player 2 / -124
// player 1, 0 with one player in the game; y -200), through stored matrix 1 (coprocessor 0x74 / 0x1A / 0x06)
template <class P> void Game<P>::bat_dive_point(uint32_t obj)
{
    float sx = of<uint8_t>(obj, 0x2E8) != 0 ? 124.0f : -124.0f;
    float sy = -200.0f;
    if (mem.at<uint16_t>(G::players_in_game) == 1) {
        sx = 0.0f;
        sy = -200.0f;
    }
    if (pc_path()) {
        // PC: times 1 / 496; stored matrix 1 on the software matrix stack, the file's pc_matrix_transform copy
        const float x = (sx * 3.0f) * ffrom(0x3B042108u), y = (sy * 3.0f) * ffrom(0x3B042108u);
        pc_matrix_push();
        pcm_load_camera();
        float p[3];
        pc_matrix_transform(x, y, 3.0f, p[0], p[1], p[2]);
        of<float>(obj, 0x3B8) = p[0];
        of<float>(obj, 0x3BC) = p[1];
        of<float>(obj, 0x3C0) = p[2];
        pcm_pop();
        return;
    }
    const float x = (sx * 3.0f) / 496.0f, y = (sy * 3.0f) / 496.0f;
    Tgp::Out o, none;
    const uint32_t slot[1] = {1};
    copro(0x74, slot, 1, none);
    const uint32_t v[3] = {fbits(x), fbits(y), fbits(3.0f)};
    copro(0x1A, v, 3, o);
    of<uint32_t>(obj, 0x3B8) = o.w[0];
    of<uint32_t>(obj, 0x3BC) = o.w[1];
    of<uint32_t>(obj, 0x3C0) = o.w[2];
    copro(0x06, nullptr, 0, none);
}

// one step toward (tx, ty, tz): the unit direction (coprocessor 0x0C) as the velocity times `speed` (1: unscaled),
// the heading toward it (0x0F), moved; returns the direction (before the move)
template <class P> void Game<P>::bat_fly_at(uint32_t obj, float tx, float ty, float tz, const float* speed, float* d)
{
    d[0] = tx - of<float>(obj, 0x20);
    d[1] = ty - of<float>(obj, 0x24);
    d[2] = tz - of<float>(obj, 0x28);
    if (pc_path()) {
        // PC: the unit vector by the file's table square root and a reciprocal; the heading by its pc_vec_angles
        const float inv = 1.0f / pc_fast_sqrt8((d[0] * d[0] + d[1] * d[1]) + d[2] * d[2]);
        of<float>(obj, 0x78) = d[0] * inv;
        of<float>(obj, 0x7C) = d[1] * inv;
        of<float>(obj, 0x80) = d[2] * inv;
        uint32_t yaw = 0, pitch = 0;
        pc_vec_angles7(d[0], d[1], d[2], yaw, pitch);
        of<uint32_t>(obj, 0x30) = yaw;
        if (speed) {
            of<float>(obj, 0x78) = *speed * of<float>(obj, 0x78);
            of<float>(obj, 0x7C) = *speed * of<float>(obj, 0x7C);
            of<float>(obj, 0x80) = *speed * of<float>(obj, 0x80);
        }
        of<float>(obj, 0x20) = of<float>(obj, 0x78) + of<float>(obj, 0x20);
        of<float>(obj, 0x24) = of<float>(obj, 0x7C) + of<float>(obj, 0x24);
        of<float>(obj, 0x28) = of<float>(obj, 0x80) + of<float>(obj, 0x28);
        return;
    }
    Tgp::Out o;
    const uint32_t v[3] = {fbits(d[0]), fbits(d[1]), fbits(d[2])};
    copro(0x0C, v, 3, o);
    of<uint32_t>(obj, 0x78) = o.w[0];
    of<uint32_t>(obj, 0x7C) = o.w[1];
    of<uint32_t>(obj, 0x80) = o.w[2];
    of<int32_t>(obj, 0x30) = int32_t(yaw_toward_a(d[0], d[1], d[2]));
    if (speed) {
        of<float>(obj, 0x78) = of<float>(obj, 0x78) * *speed;
        of<float>(obj, 0x7C) = of<float>(obj, 0x7C) * *speed;
        of<float>(obj, 0x80) = of<float>(obj, 0x80) * *speed;
    }
    of<float>(obj, 0x20) = of<float>(obj, 0x20) + of<float>(obj, 0x78);
    of<float>(obj, 0x24) = of<float>(obj, 0x24) + of<float>(obj, 0x7C);
    of<float>(obj, 0x28) = of<float>(obj, 0x28) + of<float>(obj, 0x80);
}

// coprocessor 0x1F: the distance from the bat to (x, y, z)
template <class P> float Game<P>::bat_distance(uint32_t obj, float x, float y, float z)
{
    if (pc_path()) {
        const float dx = x - of<float>(obj, 0x20), dy = y - of<float>(obj, 0x24), dz = z - of<float>(obj, 0x28);
        return pc_fast_sqrt8((dy * dy + dx * dx) + dz * dz);
    }
    const uint32_t in[6] = {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28), fbits(x), fbits(y),
                            fbits(z)};
    Tgp::Out o;
    copro(0x1F, in, 6, o);
    return ffrom(o.w[0]);
}

// 0x83A30 (state 4): the dive. Step 0: start point (+0x3AC..), bob 0.2. Step 1: at the dive point; within 2: sounds
// 0xA0134F / 0xA0134D, player_hit, 15 frames hovering (step 2); then actions 2-4 fly on (not shootable, slot freed,
// step 4: away until 10 from the player's body, then gone), the others go back (step 3: to the start point, then
// circle again, state 3)
template <class P> void Game<P>::bat_st4_dive(uint32_t obj)
{
    const int32_t tgt = of<int8_t>(obj, 0x2E8);
    const uint32_t body = mem.u32(G::player1_body + uint32_t(tgt * 0xCC));
    bat_wing(obj);
    const uint32_t sub = uint32_t(int32_t(of<int16_t>(obj, 0x60)));
    if (sub > 4)
        return;
    float d[3];
    switch (sub) {
    case 0:
        of<uint32_t>(obj, 0x3AC) = of<uint32_t>(obj, 0x20);
        of<int32_t>(obj, 0x50) = 0;
        of<uint32_t>(obj, 0x3B0) = of<uint32_t>(obj, 0x24);
        of<uint32_t>(obj, 0x88) = 0x3E4CCCCDu;   // 0.2
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        of<uint32_t>(obj, 0x3B4) = of<uint32_t>(obj, 0x28);
        [[fallthrough]];
    case 1:
        bat_dive_point(obj);
        bat_fly_at(obj, of<float>(obj, 0x3B8), of<float>(obj, 0x3BC), of<float>(obj, 0x3C0), nullptr, d);
        bat_bob(obj);
        if (bat_distance(obj, of<float>(obj, 0x3B8), of<float>(obj, 0x3BC), of<float>(obj, 0x3C0)) > 2.0f)
            return;
        sound_cue(P::pc ? 0x8A : 0xA0134F);
        sound_cue(P::pc ? 0x88 : 0xA0134D);
        player_hit(int32_t(of<int8_t>(obj, 0x2E8)), 1, 1);
        of<int32_t>(obj, 0x50) = 15;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        [[fallthrough]];
    case 2: {
        bat_bob(obj);
        const int32_t t = of<int32_t>(obj, 0x50) - 1;
        of<int32_t>(obj, 0x50) = t;
        if (t > 0)
            return;
        const int16_t a = of<int16_t>(obj, 0x64);
        if (a == 2 || a == 3 || a == 4) {
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
            mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 20) = 0;
            of<int16_t>(obj, 0x60) = 4;
            return;
        }
        mem.set32(G::bats_attacking, mem.u32(G::bats_attacking) - 1);
        mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(of<int8_t>(obj, 0x2E8)))) = 0;
        of<int8_t>(obj, 0x2E8) = -1;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        [[fallthrough]];
    }
    case 3:
        bat_fly_at(obj, of<float>(obj, 0x3AC), of<float>(obj, 0x3B0), of<float>(obj, 0x3B4), nullptr, d);
        bat_bob(obj);
        if (bat_distance(obj, of<float>(obj, 0x3AC), of<float>(obj, 0x3B0), of<float>(obj, 0x3B4)) > 2.0f)
            return;
        mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 20) = 0;
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 3;
        return;
    case 4:
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + of<float>(obj, 0x78);
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + of<float>(obj, 0x7C);
        of<float>(obj, 0x28) = of<float>(obj, 0x28) + of<float>(obj, 0x80);
        bat_bob(obj);
        if (bat_distance(obj, mem.f32(body + ob(0x20)), mem.f32(body + ob(0x24)), mem.f32(body + ob(0x28))) < 10.0f)
            return;
        mem.set32(G::bats_attacking, mem.u32(G::bats_attacking) - 1);
        mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(of<int8_t>(obj, 0x2E8)))) = 0;
        of<int8_t>(obj, 0x2E8) = -1;
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
        task_exit();
        return;
    }
}

// 0x84180 (state 5, shot): kill score 80 to the shooter; not shootable, slot and target freed; in events 15 / 1 the
// Hangedman's bat count (+0x32C) - 1. By the state it was in (+0x5E): hanging (1) it takes off (state 2) and wakes
// (rand & 1) + 1 hanging bats of hanging_bats; taking off / diving swarm... (2, 5, 6) it goes on as it was; diving (4,
// 10) one attacker less (the swarm's last attacker of a wave moves the Hangedman's wave index +0x320 on); else it
// falls (state 6; one enemy less unless flags bit 12)
template <class P> void Game<P>::bat_st5_shot(uint32_t obj)
{
    const uint32_t hm = mem.u32(G::hangedman_obj);
    const uint32_t r = rand();
    const uint32_t wakes = (r & 1) + 1;
    kill_score_shooter(obj, 0x50);
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xFFFFFEFEu;
    mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 20) = 0;
    if (of<uint8_t>(obj, 0x2E8) != 0xFF) {
        mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(of<int8_t>(obj, 0x2E8)))) = 0;
        of<int8_t>(obj, 0x2E8) = -1;
    }
    const uint8_t ev = mem.at<uint8_t>(G::event_index);
    if (ev == 15 || ev == 1)
        mem.set32(hm + ob(0x32C), mem.u32(hm + ob(0x32C)) - 1);
    const int16_t was = of<int16_t>(obj, 0x5E);
    const uint32_t k = uint32_t(int32_t(int16_t(uint16_t(was - 1))));
    switch (k) {
    case 0: {   // hanging: takes off
        of<int16_t>(obj, 0x5C) = 2;
        of<uint32_t>(obj, 0x90) = 0x40200000u;   // 2.5
        of<int16_t>(obj, 0x60) = 0;
        for (uint32_t i = 0; i < wakes; ++i) {
            for (uint32_t row = G::hanging_bats; row <= G::hanging_bats + 0x4C; row += 20) {
                bool found = false;
                for (uint32_t j = 0; j < 5; ++j) {
                    const uint32_t b = mem.u32(row + j * 4);
                    if (mem.at<int16_t>(b + ob(0x5C)) == 1) {
                        mem.at<int16_t>(b + ob(0x5C)) = 2;
                        mem.at<int16_t>(b + ob(0x60)) = 0;
                        mem.set32(b + ob(0x90), 0x40200000u);
                        found = true;
                        break;
                    }
                }
                if (found)
                    break;
            }
        }
        return;
    }
    case 1:
    case 4:
    case 5:
        of<int16_t>(obj, 0x5C) = was;
        of<int16_t>(obj, 0x60) = of<int16_t>(obj, 0x62);
        return;
    case 3:
    case 9: {
        const int32_t n = int32_t(mem.u32(G::bats_attacking)) - 1;
        mem.set32(G::bats_attacking, uint32_t(n));
        if (was == 10 && !(0 < n)) {
            const int32_t w = int32_t(mem.u32(hm + ob(0x320)));
            if (int32_t(mem.u32(G::hangedman_bat_waves + 4 + uint32_t(w) * 4)) == -1)
                mem.set32(hm + ob(0x320), 0);
            else
                mem.set32(hm + ob(0x320), uint32_t(w + 1));
        }
        break;
    }
    default:
        break;
    }
    if (((of<uint16_t>(obj, 0x58) >> 12) & 1) == 0)
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 6;
}

// 0x843B0 (state 6): dead: a hit effect where it was, sound 0xA0134E, one enemy less (unless flags bit 12), upside
// down (model 0x68, nothing set in flags); falls (gravity 0.027222222) to the floor, then ends
template <class P> void Game<P>::bat_st6_fall(uint32_t obj)
{
    int16_t sub = of<int16_t>(obj, 0x60);
    if (sub < 0 || sub > 2)
        return;
    if (sub == 0) {
        hit_effect_at(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28));
        sound_cue(P::pc ? 0x89 : 0xA0134E);
        if (((of<uint16_t>(obj, 0x58) >> 12) & 1) == 0)
            mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
        of<float>(obj, 0x7C) = 0.0f;
        of<int32_t>(obj, 0x2C) = 0x8000;
        of<uint32_t>(obj, 0x58) = 0;
        of<uint32_t>(obj, 0x54) = 0x68;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        sub = 1;
    }
    if (sub == 1) {
        const float v = of<float>(obj, 0x7C) + ffrom(0xBCDF0123u);
        const float y = of<float>(obj, 0x24) + v;
        of<float>(obj, 0x7C) = v;
        of<float>(obj, 0x24) = y;
        if (y > mem.f32(G::floor_y))
            return;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    }
    task_exit();
}

// set-ups 0 / 4: a flying bat at the record's position (enemies alive before it in +0x31C; the first resets
// bats_attacking), circle radii 7 x `rz`, a random wing frame and circle step, state 3
template <class P> void Game<P>::bat_setup_fly(uint32_t obj, uint32_t rz)
{
    of<int32_t>(obj, 0x31C) = int32_t(mem.at<int16_t>(G::enemies_alive));
    mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
    of<int8_t>(obj, 0x2E7) = -1;
    of<int8_t>(obj, 0x2E8) = -1;
    enemy_slot_take(obj);
    of<uint32_t>(obj, 0x0) = F::bat_frame;
    if (of<int32_t>(obj, 0x31C) == 0)
        mem.set32(G::bats_attacking, 0);
    of<int32_t>(obj, 0x2C) = 0;
    const uint32_t r = rand();
    of<uint32_t>(obj, 0x90) = 0x40200000u;   // 2.5
    of<int16_t>(obj, 0x2E0) = 0xC8;
    of<uint32_t>(obj, 0x394) = of<uint32_t>(obj, 0x20);
    of<uint32_t>(obj, 0x398) = of<uint32_t>(obj, 0x24);
    of<uint32_t>(obj, 0x39C) = of<uint32_t>(obj, 0x28);
    of<uint32_t>(obj, 0x30C) = 0x40E00000u;   // 7
    of<uint32_t>(obj, 0x310) = rz;
    of<uint32_t>(obj, 0x54) = (r & 7) * 2 + 0x54;
    const uint32_t r2 = rand();
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 3;
    of<uint32_t>(obj, 0x2F8) = ((r2 & 3) << 5) + 0x380;
}

// 0x844A0 (set-up 0): radii 7 x 6
template <class P> void Game<P>::bat_setup_flying(uint32_t obj)
{
    bat_setup_fly(obj, 0x40C00000u);
}

// 0x846B0 (set-up 4): radii 7 x 2
template <class P> void Game<P>::bat_setup_flying_low(uint32_t obj)
{
    bat_setup_fly(obj, 0x40000000u);
}

// 0x84590 (set-up 1): five bats at bat_group5_points (y lowered by (rand & 3) x 0.2), not counted (flags bit 12),
// circling 7 x 6
template <class P> void Game<P>::bat_setup_group5()
{
    for (uint32_t i = 0; i <= 4; ++i) {
        const uint32_t pt = G::bat_group5_points + i * 12;
        const uint32_t t = task_create(F::bat_frame, uint32_t(sizeof(Obj)));
        task_clear_work(t);
        of<uint32_t>(t, 0x58) = 0x1001;
        of<uint32_t>(t, 0x31C) = i;
        of<uint32_t>(t, 0x20) = mem.u32(pt);
        const float dy = float(int32_t(rand() & 3)) * ffrom(0x3E4CCCCDu);
        const float y0 = mem.f32(pt + 4);
        const uint32_t z = mem.u32(pt + 8);
        of<uint32_t>(t, 0x2C) = 0;
        of<uint32_t>(t, 0x30) = 0;
        of<uint32_t>(t, 0x34) = 0;
        of<uint32_t>(t, 0x28) = z;
        const float y = y0 - dy;
        of<uint32_t>(t, 0x39C) = z;
        of<uint32_t>(t, 0x90) = 0x40200000u;
        of<uint32_t>(t, 0x394) = of<uint32_t>(t, 0x20);
        of<float>(t, 0x24) = y;
        of<float>(t, 0x398) = y;
        of<uint32_t>(t, 0x54) = (rand() & 7) * 2 + 0x54;
        const uint32_t r = rand();
        of<int16_t>(t, 0x2E0) = 0xC8;
        of<uint32_t>(t, 0x30C) = 0x40E00000u;
        of<uint32_t>(t, 0x310) = 0x40C00000u;
        of<uint32_t>(t, 0x44) = (r & 7) << 12;
        const uint32_t r2 = rand();
        of<int8_t>(t, 0x2E8) = -1;
        of<int16_t>(t, 0x60) = 0;
        of<int16_t>(t, 0x5C) = 3;
        of<uint32_t>(t, 0x2F8) = ((r2 & 3) << 5) + 0x380;
    }
}

// 0x84790 (set-up 5): 20 bats (30 in event 15) in formation around the Hangedman (bat_swarm_offsets; y + 16.1497 in
// double precision), his angles, not counted (flags 0x1001), state 9; the count to his +0x32C
template <class P> void Game<P>::bat_setup_hangedman_swarm()
{
    const uint32_t n = mem.at<uint8_t>(G::event_index) == 15 ? 30 : 20;
    const uint32_t hm = mem.u32(G::hangedman_obj);
    uint32_t i = 0;
    for (; i < n; ++i) {
        const uint32_t off = G::bat_swarm_offsets + i * 12;
        const uint32_t t = task_create(F::bat_swarm_frame, uint32_t(sizeof(Obj)));
        task_clear_work(t);
        const float oy = float(double(mem.f32(off + 4)) + 16.1497);   // 0x40302652BD3C3611
        of<uint32_t>(t, 0x31C) = i;
        of<uint32_t>(t, 0x58) = 0x1001;
        of<uint32_t>(t, 0x350) = mem.u32(off);
        of<uint32_t>(t, 0x358) = mem.u32(off + 8) ^ 0x80000000u;
        of<float>(t, 0x354) = oy;
        of<float>(t, 0x20) = mem.f32(hm + ob(0x20)) + of<float>(t, 0x350);
        of<float>(t, 0x24) = mem.f32(hm + ob(0x24)) + of<float>(t, 0x354);
        of<float>(t, 0x28) = mem.f32(hm + ob(0x28)) + of<float>(t, 0x358);
        of<uint32_t>(t, 0x2C) = mem.u32(hm + ob(0x2C));
        of<uint32_t>(t, 0x30) = mem.u32(hm + ob(0x30));
        const uint32_t az = mem.u32(hm + ob(0x34));
        of<uint32_t>(t, 0x394) = of<uint32_t>(t, 0x20);
        of<uint32_t>(t, 0x90) = 0x40300000u;   // 2.75
        of<uint32_t>(t, 0x398) = of<uint32_t>(t, 0x24);
        of<uint32_t>(t, 0x39C) = of<uint32_t>(t, 0x28);
        of<uint32_t>(t, 0x34) = az;
        of<uint32_t>(t, 0x54) = (rand() & 7) * 2 + 0x54;
        of<uint32_t>(t, 0x44) = (rand() & 7) << 12;
        of<int16_t>(t, 0x2E0) = mem.at<uint8_t>(G::event_index) == 15 ? 0x96 : 0xC8;
        of<uint32_t>(t, 0x30C) = 0x40E00000u;
        of<uint32_t>(t, 0x310) = 0x40C00000u;
        const uint32_t r = rand();
        of<int8_t>(t, 0x2E8) = -1;
        of<int16_t>(t, 0x5C) = 9;
        of<int16_t>(t, 0x60) = 0;
        of<uint32_t>(t, 0x2F8) = ((r & 3) << 5) + 0x380;
    }
    mem.set32(hm + ob(0x32C), i);
}

// 0x84940: hit check; in events 15 / 1 once the Hangedman is beaten (event flag 0xC5) every swarm bat falls (sound
// 0xA0134E, model 0x68, state 6 step 1); the state routine; drawn faded at its position / heading (no shadow, no
// model call); depth list, home position, attacker list unless flags bit 12
template <class P> void Game<P>::bat_swarm_frame(uint32_t obj)
{
    bat_hit_check(obj);
    const uint8_t ev = mem.at<uint8_t>(G::event_index);
    if ((ev == 15 || ev == 1) && mem.at<uint8_t>(G::event_flags + 0xC5) == 1 && of<int16_t>(obj, 0x5C) != 6) {
        sound_cue(P::pc ? 0x89 : 0xA0134E);
        of<uint32_t>(obj, 0x54) = 0x68;
        of<int16_t>(obj, 0x5C) = 6;
        of<uint32_t>(obj, 0x58) = 0;
        of<int16_t>(obj, 0x60) = 1;
    }
    bat_call(mem.u32(G::bat_states + uint32_t(int32_t(of<int16_t>(obj, 0x5C))) * 4), obj);
    if constexpr (P::pc) {
        pc_matrix_push();
        pc_matrix_translate6(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28));
        pc_matrix_rotate_y6(of<uint32_t>(obj, 0x30));
        pc_draw_state(int32_t(of<int16_t>(obj, 0x2E0)));
        pc_matrix_position_draw(of<uint32_t>(obj, 0x54), obj + ob(0x38));
        pc_draw_state(0);
        pcm_pop();
        depth_list_add(obj);
        of<uint32_t>(obj, 0x2D4) = of<uint32_t>(obj, 0x20);
        of<uint32_t>(obj, 0x2D8) = of<uint32_t>(obj, 0x24);
        of<uint32_t>(obj, 0x2DC) = of<uint32_t>(obj, 0x28);
        if (((of<uint16_t>(obj, 0x58) >> 12) & 1) == 0)
            attacker_list_add(obj);
        return;
    }
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    const uint32_t t[3] = {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)};
    copro(0x12, t, 3, none);
    const uint32_t ry[1] = {of<uint32_t>(obj, 0x30)};
    copro(0x15, ry, 1, none);
    draw_fade_begin(int32_t(of<int16_t>(obj, 0x2E0)));
    geo_matrix_position(of<uint32_t>(obj, 0x54), obj + ob(0x38));
    geo_send_words(0, 31, 0x7A0);
    copro(0x06, nullptr, 0, none);
    depth_list_add(obj);
    of<uint32_t>(obj, 0x2D4) = of<uint32_t>(obj, 0x20);
    of<uint32_t>(obj, 0x2D8) = of<uint32_t>(obj, 0x24);
    of<uint32_t>(obj, 0x2DC) = of<uint32_t>(obj, 0x28);
    if (((of<uint16_t>(obj, 0x58) >> 12) & 1) == 0)
        attacker_list_add(obj);
}

// 0x84A80 (state 9): circles with the swarm (step 0: bob 0.3, not shootable); while the Hangedman calls the bats
// (his +0x2E5 = 1) pick_target_player_any; one more attacker if the wave allows (hangedman_bat_waves[+0x330][+0x320]):
// shootable, a front attack slot, state 10
template <class P> void Game<P>::bat_st9_swarm_circle(uint32_t obj)
{
    bat_wing(obj);
    const uint32_t hm = mem.u32(G::hangedman_obj);
    const int16_t sub = of<int16_t>(obj, 0x60);
    if (sub == 0 || sub == 1) {
        if (sub == 0) {
            of<uint32_t>(obj, 0x88) = 0x3E99999Au;
            of<uint32_t>(obj, 0x3A0) = of<uint32_t>(obj, 0x20);
            of<uint32_t>(obj, 0x3A4) = of<uint32_t>(obj, 0x24);
            of<int32_t>(obj, 0x30) = 0;
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
            of<int32_t>(obj, 0x50) = 0;
            of<int16_t>(obj, 0x60) = int16_t(sub + 1);
            of<uint32_t>(obj, 0x3A8) = of<uint32_t>(obj, 0x28);
        }
        bat_circle_step(obj);
        of<uint32_t>(obj, 0x3A0) = of<uint32_t>(obj, 0x20);
        of<uint32_t>(obj, 0x3A4) = of<uint32_t>(obj, 0x24);
        of<uint32_t>(obj, 0x3A8) = of<uint32_t>(obj, 0x28);
    }
    bat_bob(obj);
    if (mem.at<uint8_t>(hm + ob(0x2E5)) != 1)
        return;
    pick_target_player_any(obj);
    if (of<uint8_t>(obj, 0x2E8) == 0xFF)
        return;
    const uint32_t wave = mem.u32(hm + ob(0x330)), w = mem.u32(hm + ob(0x320));
    const int32_t limit = int32_t(mem.u32(G::hangedman_bat_waves + wave * 20 + w * 4));
    if (!(int32_t(mem.u32(G::bats_attacking)) + 1 <= limit))
        return;
    mem.set32(G::bats_attacking, mem.u32(G::bats_attacking) + 1);
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 1;
    attack_slot_front_take(obj);
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 10;
}

// 0x84CD0 (state 10): the swarm dive (drawn unfaded). Step 0: start point, bob 0.2, the dive point, facing it, 15
// frames; step 1: sound 0xA0134F, the dive speed from the Hangedman's speed table (+0x35C: +0x14 / +0x10 by his
// +0x66); step 2: flies at the point; within the speed on each axis: sounds, the hit (players 1 / 2 not
// invulnerable: player_hit, his +0x320 - 1), 15 frames hovering (step 3; a hit: sound 0xA01263), the speed back
// (+0x1C / +0x18); step 4: back to the start point, then one attacker less, faded again, state 9
template <class P> void Game<P>::bat_st10_swarm_dive(uint32_t obj)
{
    const uint32_t hm = mem.u32(G::hangedman_obj);
    const uint32_t spd = mem.u32(hm + ob(0x35C));
    of<int16_t>(obj, 0x2E0) = 0xFF;
    bat_wing(obj);
    const uint32_t sub = uint32_t(int32_t(of<int16_t>(obj, 0x60)));
    if (sub > 4)
        return;
    float d[3];
    switch (sub) {
    case 0: {
        of<uint32_t>(obj, 0x3AC) = of<uint32_t>(obj, 0x20);
        of<int32_t>(obj, 0x50) = 0;
        of<uint32_t>(obj, 0x3B0) = of<uint32_t>(obj, 0x24);
        of<uint32_t>(obj, 0x88) = 0x3E4CCCCDu;
        of<int32_t>(obj, 0x334) = 0;
        of<uint32_t>(obj, 0x3B4) = of<uint32_t>(obj, 0x28);
        bat_dive_point(obj);
        const float dx = of<float>(obj, 0x3B8) - of<float>(obj, 0x20);
        const float dy = of<float>(obj, 0x3BC) - of<float>(obj, 0x24);
        const float dz = of<float>(obj, 0x3C0) - of<float>(obj, 0x28);
        of<int32_t>(obj, 0x30) = int32_t(yaw_toward_a(dx, dy, dz));
        of<int32_t>(obj, 0x50) = 15;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        [[fallthrough]];
    }
    case 1: {
        const int32_t t = of<int32_t>(obj, 0x50) - 1;
        of<int32_t>(obj, 0x50) = t;
        if (0 < t)
            return;
        sound_cue(P::pc ? 0x8A : 0xA0134F);
        of<uint32_t>(obj, 0x34C) = mem.at<int16_t>(hm + ob(0x66)) == 0 ? mem.u32(spd + 0x14) : mem.u32(spd + 0x10);
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        [[fallthrough]];
    }
    case 2: {
        const float s = of<float>(obj, 0x34C);
        bat_fly_at(obj, of<float>(obj, 0x3B8), of<float>(obj, 0x3BC), of<float>(obj, 0x3C0), &s, d);
        bat_bob(obj);
        auto mag = [](float v) { return v < 0.0f ? ffrom(fbits(v) ^ 0x80000000u) : v; };
        if (mag(d[0]) > of<float>(obj, 0x34C) || mag(d[1]) > of<float>(obj, 0x34C) || mag(d[2]) > of<float>(obj, 0x34C))
            return;
        sound_cue(P::pc ? 0x8A : 0xA0134F);
        sound_cue(P::pc ? 0x88 : 0xA0134D);
        const uint8_t tg = of<uint8_t>(obj, 0x2E8);
        if ((tg == 0 || tg == 1) &&
            mem.u32(G::player_invulnerable + uint32_t(int32_t(of<int8_t>(obj, 0x2E8))) * 4) == 0) {
            of<int32_t>(obj, 0x334) = 1;
            player_hit(int32_t(of<int8_t>(obj, 0x2E8)), 1, 1);
            const int32_t c = int32_t(mem.u32(hm + ob(0x320)));
            if (0 < c)
                mem.set32(hm + ob(0x320), uint32_t(c - 1));
        }
        of<int32_t>(obj, 0x50) = 15;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        [[fallthrough]];
    }
    case 3: {
        bat_bob(obj);
        const int32_t t = of<int32_t>(obj, 0x50) - 1;
        of<int32_t>(obj, 0x50) = t;
        if (t > 0)
            return;
        if (of<int32_t>(obj, 0x334) == 1)
            sound_cue(P::pc ? 0x59 : 0xA01263);
        of<uint32_t>(obj, 0x34C) = mem.at<int16_t>(hm + ob(0x66)) == 0 ? mem.u32(spd + 0x1C) : mem.u32(spd + 0x18);
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        [[fallthrough]];
    }
    case 4: {
        const float s = of<float>(obj, 0x34C);
        bat_fly_at(obj, of<float>(obj, 0x3AC), of<float>(obj, 0x3B0), of<float>(obj, 0x3B4), &s, d);
        bat_bob(obj);
        if (bat_distance(obj, of<float>(obj, 0x3AC), of<float>(obj, 0x3B0), of<float>(obj, 0x3B4)) > 2.0f)
            return;
        mem.set32(G::bats_attacking, mem.u32(G::bats_attacking) - 1);
        mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(of<int8_t>(obj, 0x2E8)))) = 0;
        const int32_t slot = of<int8_t>(obj, 0x2E7);
        of<int8_t>(obj, 0x2E8) = -1;
        mem.at<uint8_t>(G::attack_slots + uint32_t(slot) * 20) = 0;
        of<int16_t>(obj, 0x2E0) = mem.at<uint8_t>(G::event_index) == 15 ? 0x96 : 0xC8;
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 9;
        return;
    }
    }
}

// the bats file's PC copies: sine / cosine from pc_trig_table (index = 16-bit angle / 16, truncated), the table square
// root, pc_vec_angles
template <class P> float Game<P>::pc_sin2(uint32_t angle)
{
    if constexpr (!P::pc)
        return pm::trig(angle, 0);   // PC Mode on the arcade logic: the rebuilt table
    else {
        const int32_t i = (int32_t(int16_t(angle & 0xFFFF)) / 16) & 0xFFF;
        return mem.f32(G::pc_trig_table + uint32_t(i) * 8);
    }
}
template <class P> float Game<P>::pc_cos2(uint32_t angle)
{
    if constexpr (!P::pc)
        return pm::trig(angle, 1);   // PC Mode on the arcade logic: the rebuilt table
    else {
        const int32_t i = (int32_t(int16_t(angle & 0xFFFF)) / 16) & 0xFFF;
        return mem.f32(G::pc_trig_table + uint32_t(i) * 8 + 4);
    }
}
template <class P> float Game<P>::pc_fast_sqrt8(float x) { return pc_fast_sqrt_table(x); }   // PC 0x4E7F5C
template <class P> void Game<P>::pc_vec_angles7(float x, float y, float z, uint32_t& yaw, uint32_t& pitch)   // PC 0x4E7808
{
    pc_vec_angles(x, y, z, yaw, pitch);
}

} // namespace hotd::port
