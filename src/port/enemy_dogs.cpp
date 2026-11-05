// Enemies: kind 97, the Kenfis dogs (creature types 0 / 8 / 40 / 79-81; arcade cluster 0x7C9A0..0x7EC2C plus helpers
// 0x1C820..0x1DB30, 0x69880, 0x6A0B0; PC file 0x4DD5C4..0x4DFB3D), read from the arcade and PC disassembly. Dogs come
// in packs: a pack (the object's +0x2E6) shares a spawn_slots record (0x2C bytes): +4 the pack's phase, +8 / +0x10 /
// +0x14 / +0x18 the motion index of state 0-3 (-1 = not in that state), +0x1C the hit side, +0x20 the target player,
// +0x24 the dog's index among the live dogs (dog_pack_list_build). The object's state is +0x5C (0 bite, 1 ..., 2 leap /
// pack ready, 3 circling), its sub-step +0x60. The set-ups (dog_setups by the record's action) bring a dog in; dog_frame
// then runs the state routines (dog_states). The set-up 2 jump height is computed in double precision on the arcade
// (divided by 30) and at the PC's 24-bit x87 precision (times 1 / 30): kept per version.
#include "port/game.h"

#include <climits>
#include <cmath>

namespace hotd::port {

namespace {

// the i960's logrl src1, src2 -> src2 x log2(src1) as MAME computes it (t2 x log(t1) / log(2.0), double); the dogs
// pass src2 = ln 2 (0x3FE62E42_FEFA39EF), which makes it the natural log
double i960_logrl(double t1, double t2) { return t2 * std::log(t1) / std::log(2.0); }

// cvtzri / the PC's chop + fistp: toward zero; out of range (or NaN) the integer indefinite 0x80000000
int32_t trunc_int(double v) { return (v > -2147483649.0 && v < 2147483648.0) ? int32_t(v) : INT_MIN; }

} // namespace

// the dog's pack record (spawn_slots + pack * 0x2C)
template <class P> uint32_t Game<P>::dog_rec(uint32_t obj)
{
    return G::spawn_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E6))) * 0x2C;
}

// a call through dog_setups / dog_states: the ported routines directly, the rest as boundaries
template <class P> void Game<P>::dog_call(uint32_t routine, uint32_t obj)
{
    switch (routine) {
    case F::dog_set0_pack: dog_set0_pack(obj); return;
    case F::dog_set1_run_in: dog_set1_run_in(obj); return;
    case F::dog_set2_jump_in: dog_set2_jump_in(obj); return;
    case F::dog_set3: dog_set3(obj); return;
    case F::dog_set4_look_around: dog_set4_look_around(obj); return;
    case F::dog_st0_bite: dog_st0_bite(obj); return;
    case F::dog_st2_leap: dog_st2_leap(obj); return;
    case F::dog_st1: dog_st1(obj); return;
    case F::dog_st3_circle: dog_st3_circle(obj); return;
    default: call_routine(routine, obj); return;
    }
}

// 0x7C9A0 / PC 0x4DD5C4 (kind 97): type and action from the record (+0 / +1); body (dog_motions_c[0]); the live dogs
// listed (dog_pack_count); the target player (2 players: the dog's index modulo the players, also the pack's +0x20;
// 1 player: the one playing); the pack record's phase 0 and states -1; +0x3C4 bit 7; the set-up; type set-up, counted,
// a wide attack slot; state 2 for a lone dog (pack state-2 motion 0), else 3 (state-3 motion 3)
template <class P> void Game<P>::kind97_ddog_kenfis(uint32_t obj)
{
    const uint32_t r = of<uint32_t>(obj, 0x35C);
    of<uint32_t>(obj, 0x368) = r;
    of<int16_t>(obj, 0xCC) = int16_t(mem.at<uint8_t>(r));
    of<int16_t>(obj, 0x64) = int16_t(mem.at<uint8_t>(r + 1));
    const int32_t type = of<int16_t>(obj, 0xCC);
    mem.set32(G::hit_type, uint32_t(type));
    body_init(obj, type, mem.u32(G::dog_motions_c));
    mem.set32(G::dog_pack_count, uint32_t(dog_pack_list_build()));
    const uint32_t rec = dog_rec(obj);
    if (mem.at<uint16_t>(G::players_in_game) == 2) {
        const int32_t n = mem.at<int16_t>(G::players_in_game);
        of<int8_t>(obj, 0x2E8) = int8_t(mem.at<int32_t>(rec + 0x24) % n);
        mem.at<int32_t>(rec + 0x20) = mem.at<int32_t>(rec + 0x24) % n;
    } else {
        of<uint8_t>(obj, 0x2E8) = mem.at<uint8_t>(G::solo_player_is_p2);
        mem.at<int32_t>(rec + 0x20) = mem.at<int8_t>(G::solo_player_is_p2);
    }
    mem.at<int32_t>(rec + 4) = 0;
    mem.at<int32_t>(rec + 8) = -1;
    mem.at<int32_t>(rec + 0x10) = -1;
    mem.at<int32_t>(rec + 0x14) = -1;
    mem.at<int32_t>(rec + 0x18) = -1;
    of<uint8_t>(obj, 0x3C4) = of<uint8_t>(obj, 0x3C4) | 0x80;
    dog_call(mem.u32(G::dog_setups + uint32_t(int32_t(of<int16_t>(obj, 0x64))) * 4), obj);
    creature_type_setup(obj);
    mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
    attack_slot_take_wide(obj);
    if (mem.u32(G::dog_pack_count) == 1) {
        of<int16_t>(obj, 0x5C) = 2;
        mem.at<int32_t>(rec + 0x14) = 0;
    } else {
        of<int16_t>(obj, 0x5C) = 3;
        mem.at<int32_t>(rec + 0x18) = 3;
    }
}

// 0x7CB90 / PC 0x4DD800 (set-up 0): straight to dog_frame
template <class P> void Game<P>::dog_set0_pack(uint32_t obj)
{
    of<uint32_t>(obj, 0x0) = F::dog_frame;
}

// set-ups 1-3 (0x7CBA0 / 0x7CC00 / 0x7CC60): state 2 with motion index `m`, blended in over 3 frames, then `frame`
template <class P> void Game<P>::dog_set_in(uint32_t obj, int32_t m, uint32_t frame, bool step0)
{
    const uint32_t rec = dog_rec(obj);
    of<int16_t>(obj, 0x5C) = 2;
    mem.at<int32_t>(rec + 0x14) = m;
    set_motion_blend(obj, mem.u32(G::dog_motions_c + uint32_t(mem.at<int32_t>(rec + 0x14)) * 4), 0, 3);
    if (step0)
        of<int16_t>(obj, 0x60) = 0;
    of<uint32_t>(obj, 0x0) = frame;
}

// 0x7CBA0 / PC 0x4DD808 (set-up 1, running in): motion 5 (ddogghashiru), dog_set1_frame
template <class P> void Game<P>::dog_set1_run_in(uint32_t obj) { dog_set_in(obj, 5, F::dog_set1_frame, false); }
// 0x7CC00 / PC 0x4DD874 (set-up 2, jumping in): motion 5, dog_set2_frame
template <class P> void Game<P>::dog_set2_jump_in(uint32_t obj) { dog_set_in(obj, 5, F::dog_set2_frame, false); }
// 0x7CC60 / PC 0x4DD8E0 (set-up 3): motion 4, sub-step 0, dog_set3_frame
template <class P> void Game<P>::dog_set3(uint32_t obj) { dog_set_in(obj, 4, F::dog_set3_frame, true); }

// 0x7CCC0 / PC 0x4DD954 (set-up 4, looking around): state 2 (motion index 0), MO 0x6E (ddoggkyoro) over 3 frames,
// sub-step 0, dog_set4_frame
template <class P> void Game<P>::dog_set4_look_around(uint32_t obj)
{
    of<int16_t>(obj, 0x5C) = 2;
    mem.at<int32_t>(dog_rec(obj) + 0x14) = 0;
    set_motion_blend(obj, 0x6E, 0, 3);
    of<int16_t>(obj, 0x60) = 0;
    of<uint32_t>(obj, 0x0) = F::dog_set4_frame;
}

// 0x7CD10 / PC 0x4DD9F8: the live (shootable) dogs among the 14 player_bodies slots into dog_list (the rest cleared),
// each dog's index in its pack record's +0x24; the count
template <class P> int32_t Game<P>::dog_pack_list_build()
{
    int32_t n = 0;
    uint32_t out = G::dog_list;
    for (uint32_t i = 0; i < 14; ++i) {
        const uint32_t o = mem.u32(G::player_bodies + 4 * i);
        mem.set32(G::dog_list + 4 * i, 0);
        if (o == 0)
            continue;
        const int16_t t = of<int16_t>(o, 0xCC);
        if (!(t == 0 || t == 8 || t == 0x28 || t == 0x4F || t == 0x50 || t == 0x51))
            continue;
        if ((of<uint8_t>(o, 0x58) & 1) == 0)
            continue;
        mem.set32(out, o);
        out += 4;
        mem.at<int32_t>(G::spawn_slots + uint32_t(int32_t(of<int8_t>(o, 0x2E6))) * 0x2C + 0x24) = n;
        ++n;
    }
    return n;
}

// the motion of state `s` (0-3) with index `i`: dog_motions_a (16-byte rows) / _b / _c / _d
template <class P> uint32_t Game<P>::dog_motion_of(int32_t s, int32_t i)
{
    switch (s) {
    case 0: return mem.u32(G::dog_motions_a + uint32_t(i) * 16);
    case 1: return mem.u32(G::dog_motions_b + uint32_t(i) * 4);
    case 2: return mem.u32(G::dog_motions_c + uint32_t(i) * 4);
    default: return mem.u32(G::dog_motions_d + uint32_t(i) * 4);
    }
}

// 0x7CE20 / PC 0x4DDB00: the motion of its state (+0x5C 0-3): the other states' indices -1, this one's index (0 if
// unset), blended in over 4 frames
template <class P> void Game<P>::dog_motion_by_state(uint32_t obj)
{
    const int16_t s = of<int16_t>(obj, 0x5C);
    if (s < 0 || s > 3)
        return;
    const uint32_t rec = dog_rec(obj);
    static const uint32_t field[4] = {8, 0x10, 0x14, 0x18};
    for (int k = 0; k < 4; ++k)
        if (k != s)
            mem.at<int32_t>(rec + field[k]) = -1;
    if (mem.at<int32_t>(rec + field[s]) == -1)
        mem.at<int32_t>(rec + field[s]) = 0;
    set_motion_blend(obj, dog_motion_of(s, mem.at<int32_t>(rec + field[s])), 0, 4);
}

// 0x7D080 / PC 0x4DDE40 (object, state, index, frames): state +0x5C, the pack's motion indices -1 but this one's;
// its motion blended in
template <class P> void Game<P>::dog_motion_set(uint32_t obj, int32_t s, int32_t i, int32_t frames)
{
    const uint32_t rec = dog_rec(obj);
    of<int16_t>(obj, 0x5C) = int16_t(s);
    mem.at<int32_t>(rec + 8) = -1;
    mem.at<int32_t>(rec + 0x10) = -1;
    mem.at<int32_t>(rec + 0x14) = -1;
    mem.at<int32_t>(rec + 0x18) = -1;
    const int16_t st = of<int16_t>(obj, 0x5C);
    if (st < 0 || st > 3)
        return;
    static const uint32_t field[4] = {8, 0x10, 0x14, 0x18};
    mem.at<int32_t>(rec + field[st]) = i;
    set_motion_blend(obj, dog_motion_of(st, i), 0, frames);
}

// 0x7D190 / PC 0x4DDFB4 (object, state, index, a, b): as dog_motion_set with set_motion_blend's two arguments
template <class P> void Game<P>::dog_set_motion_step(uint32_t obj, int32_t s, int32_t i, int32_t a, int32_t b)
{
    const uint32_t rec = dog_rec(obj);
    of<int16_t>(obj, 0x5C) = int16_t(s);
    mem.at<int32_t>(rec + 8) = -1;
    mem.at<int32_t>(rec + 0x10) = -1;
    mem.at<int32_t>(rec + 0x14) = -1;
    mem.at<int32_t>(rec + 0x18) = -1;
    const int16_t st = of<int16_t>(obj, 0x5C);
    if (st < 0 || st > 3)
        return;
    static const uint32_t field[4] = {8, 0x10, 0x14, 0x18};
    mem.at<int32_t>(rec + field[st]) = i;
    set_motion_blend(obj, dog_motion_of(st, i), a, b);
}

// 0x7D2B0 / PC 0x4DE12C (set-up 1): drawn, anim time + 1; once the camera stops: state 2 (pack phase motion 0), its
// running motion again, dog_frame
template <class P> void Game<P>::dog_set1_frame(uint32_t obj)
{
    body_draw(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
    if (mem.u32(G::camera_stopped) != 2)
        return;
    const uint32_t rec = dog_rec(obj);
    of<int16_t>(obj, 0x5C) = 2;
    mem.at<int32_t>(rec + 8) = 0;
    set_motion_blend(obj, mem.u32(G::dog_motions_c + uint32_t(mem.at<int32_t>(rec + 0x14)) * 4), 0, 3);
    of<uint32_t>(obj, 0x0) = F::dog_frame;
}

// 0x7D330 / PC 0x4DE1B4 (set-up 2): drawn, anim time + 1. 0: once the camera stops MO 0x70 (inutobitobi); 1: at the
// motion's end MO 0x71; 2: forward 1.0 a frame, height floor + 20.83 - (frame - 25)^2 / 30 (A: double; PC: x87 at 24
// bits, times 1 / 30); at the motion's end its state's motion and dog_frame
template <class P> void Game<P>::dog_set2_frame(uint32_t obj)
{
    body_draw(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
    const int16_t s = of<int16_t>(obj, 0x60);
    if (s == 0) {
        if (mem.u32(G::camera_stopped) == 2) {
            set_motion_blend(obj, 0x70, 0, 3);
            of<int16_t>(obj, 0x60) = 1;
        }
        return;
    }
    if (s == 1) {
        if ((of<uint8_t>(obj, 0x3C5) >> 4) & 1) {
            set_motion(obj, 0x71);
            of<int16_t>(obj, 0x60) = 2;
        }
        return;
    }
    if (s != 2)
        return;
    move_along_heading(obj, 1.0f);
    const float t = float(int32_t(of<int16_t>(obj, 0xF4)));
    float y;
    if (pc_path()) {
        const float d = t + -25.0f;
        const float sq = d * d;
        const float q = float(double(sq) * 0.03333333333333333);          // 0x5A6FBC
        const float f = float(double(mem.f32(G::floor_y)) + 20.833333333333332);   // 0x5A6FB4
        y = f - q;
    } else {
        const float d = t - 25.0f;
        const float sq = d * d;
        const double q = double(sq) / 30.0;
        const float f = float(double(mem.f32(G::floor_y)) + 20.833333333333332);   // 0x4034D555 55555555
        y = float(double(f) - q);
    }
    of<float>(obj, 0x24) = y;
    if ((of<uint8_t>(obj, 0x3C5) >> 4) & 1) {
        dog_motion_by_state(obj);
        of<int16_t>(obj, 0x60) = 0;
        of<uint32_t>(obj, 0x0) = F::dog_frame;
    }
}

// sine (cos = false) / cosine of an angle: A coprocessor 0x1B / 0x1C; PC pc_trig_table (angle / 16, 0x1000 entries)
template <class P> float Game<P>::dog_trig(uint32_t angle, bool cos)
{
    if (pc_path()) {   // the PC, and PC Mode on the arcade logic (docs/pcmode.md row 10)
        return pc_trig(angle, cos ? 1 : 0);
    } else {
        Tgp::Out o;
        const uint32_t a[1] = {angle};
        copro(cos ? 0x1C : 0x1B, a, 1, o);
        return ffrom(o.w[0]);
    }
}

// 0x7D460 / PC 0x4DE28C (set-up 3, a jump to the record's point): drawn. 0: turned toward the point (A coprocessor
// 0x0F + the angle fix; PC pc_vec_angles9), the jump speed for the record's angle (+2) from dog_set3_fall, MO 0x71,
// velocity cos / sin x speed. 1: forward by the x speed (move_along_heading), anim time held once the motion ends;
// falling (gravity 0.0544: A the float value as a double, PC the double; added at the version's precision); landing on
// the floor: MO 0x70, 8 frames, then 2: its state's motion and dog_frame
template <class P> void Game<P>::dog_set3_frame(uint32_t obj)
{
    object_draw(obj);
    const int16_t s = of<int16_t>(obj, 0x60);
    if (s == 0) {
        const float x = of<float>(obj, 0x20), y = of<float>(obj, 0x24), z = of<float>(obj, 0x28);
        const uint32_t r = of<uint32_t>(obj, 0x368);
        const float tx = mem.f32(r + 4), ty = mem.f32(r + 8), tz = mem.f32(r + 0xC);
        if (pc_path()) {
            pc_vec_angles9(tx - x, 0.0f, tz - z, obj + ob(0x30), obj + ob(0x2C));
        } else {
            Tgp::Out o;
            const uint32_t d[3] = {fbits(tx - x), 0, fbits(tz - z)};
            copro(0x0F, d, 3, o);
            uint32_t yaw = o.w[0], pitch = o.w[1];
            of<uint32_t>(obj, 0x30) = yaw;
            of<uint32_t>(obj, 0x2C) = pitch;
            fixAngles(yaw, pitch);
            of<uint32_t>(obj, 0x30) = yaw;
            of<uint32_t>(obj, 0x2C) = pitch;
        }
        mem.set32(G::dog_jump_angle, mem.at<uint16_t>(r + 2));
        const float speed = dog_set3_fall(x, y, z, tx, ty, tz, mem.u32(G::dog_jump_angle));
        mem.at<float>(G::dog_jump_speed) = speed;
        set_motion(obj, 0x71);
        of<float>(obj, 0x78) = dog_trig(mem.u32(G::dog_jump_angle), true) * mem.f32(G::dog_jump_speed);
        of<float>(obj, 0x7C) = dog_trig(mem.u32(G::dog_jump_angle), false) * mem.f32(G::dog_jump_speed);
        of<int16_t>(obj, 0x60) = 1;
        return;
    }
    if (s == 1) {
        move_along_heading(obj, of<float>(obj, 0x78));
        uint8_t b = of<uint8_t>(obj, 0x3C5);
        if ((b >> 4) & 1)
            b = uint8_t(b | 8);
        of<uint8_t>(obj, 0x3C5) = b;
        if (((b >> 3) & 1) == 0)
            of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
        const float vy = of<float>(obj, 0x7C);
        const float y = of<float>(obj, 0x24) + vy;
        if (y < mem.f32(G::floor_y)) {
            of<uint32_t>(obj, 0x24) = mem.u32(G::floor_y);
            set_motion(obj, 0x70);
            of<int32_t>(obj, 0xF8) = 8;
            of<int16_t>(obj, 0x60) = 2;
            of<uint8_t>(obj, 0x3C5) = of<uint8_t>(obj, 0x3C5) & 0xF7;
            return;
        }
        of<float>(obj, 0x24) = y;
        if (pc_path())
            of<float>(obj, 0x7C) = float(double(vy) + ffrom64(0xBFABE02468ACF135ull));
        else
            of<float>(obj, 0x7C) = float(double(vy) + ffrom64(0xBFABE02460000000ull));
        return;
    }
    if (s == 2) {
        const int32_t t = of<int32_t>(obj, 0xF8) - 1;
        of<int32_t>(obj, 0xF8) = t;
        if (t == 0) {
            dog_motion_by_state(obj);
            of<uint32_t>(obj, 0x0) = F::dog_frame;
        }
    }
}

// set-up 4's end: its state's motion, dog_frame, +0x3C6 bit 0 off
template <class P> void Game<P>::dog_set4_done(uint32_t obj)
{
    dog_motion_by_state(obj);
    of<uint32_t>(obj, 0x0) = F::dog_frame;
    of<uint8_t>(obj, 0x3C6) = of<uint8_t>(obj, 0x3C6) & 0xFE;
}

// 0x7D780 / PC 0x4DE4B8 (set-up 4): drawn, anim time + 1. 0: +0x3C6 bit 0; at frame 30 its state-2 motion. 1: along
// the curve to the record's point (dog_set4_path, x angle 0xE800); reaching its height: facing the camera (angle to
// it outside 0x1800..0xE7FF) -> done, else MO 0x65 and 2: turning by -0x400 a frame, moving 0.3 along it, until it
// faces the camera -> done
template <class P> void Game<P>::dog_set4_frame(uint32_t obj)
{
    body_draw(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
    const int16_t s = of<int16_t>(obj, 0x60);
    if (s == 0) {
        of<uint8_t>(obj, 0x3C6) = of<uint8_t>(obj, 0x3C6) | 1;
        if (of<uint16_t>(obj, 0xF4) == 30) {
            set_motion_blend(obj, mem.u32(G::dog_motions_c + uint32_t(mem.at<int32_t>(dog_rec(obj) + 0x14)) * 4), 0, 3);
            of<int16_t>(obj, 0x60) = 1;
        }
        return;
    }
    if (s == 1) {
        const uint32_t r = of<uint32_t>(obj, 0x368);
        of<uint32_t>(obj, 0x2C) = 0xE800;
        dog_set4_path(obj, mem.f32(r + 4), mem.f32(r + 8), mem.f32(r + 0xC), 1.0f);
        if (!(of<float>(obj, 0x24) < mem.f32(r + 8)))
            return;
        of<uint32_t>(obj, 0x24) = mem.u32(r + 8);
        of<uint32_t>(obj, 0x2C) = 0;
        const int32_t a = int32_t(angle_to_camera(obj));
        if (a > 0xE7FF || a <= 0x17FF) {
            dog_set4_done(obj);
            return;
        }
        of<int16_t>(obj, 0x60) = 2;
        of<uint8_t>(obj, 0x3C6) = of<uint8_t>(obj, 0x3C6) | 1;
        set_motion_blend(obj, 0x65, 0, 3);
        return;
    }
    if (s != 2)
        return;
    const uint32_t h = (of<uint32_t>(obj, 0x30) - 0x400) & 0xFFFF;
    of<uint32_t>(obj, 0x30) = h;
    of<float>(obj, 0x20) = dog_trig(0xFFFF - h, false) * 0.3f + of<float>(obj, 0x20);
    of<float>(obj, 0x28) = dog_trig(0xFFFF - of<uint32_t>(obj, 0x30), true) * 0.3f + of<float>(obj, 0x28);
    const int32_t a = int32_t(angle_to_camera(obj));
    if (a > 0xE7FF || a <= 0x17FF)
        dog_set4_done(obj);
}

// 0xA4E18 (the C library's rand, separate from the game's rand): A seed crand_seed = seed x 0x5D588B65 mod 2^31 - 1
// (the folded 64-bit product); PC 0x578A6B: the library's own (per-thread seed x 0x41C64E6D + 0x3039, bits 16-30), a
// boundary here. PC Mode on the arcade logic: the PC's rand on the runtime's copy of its seed (pm::crand,
// pm_crand_seed: 1 at start-up as the PC's, never reseeded in play)
template <class P> uint32_t Game<P>::crand()
{
    if constexpr (P::pc) {
        return pc_crand();
    } else {
        if (pcmode != 0)
            return pm::crand(pm_crand_seed);
        const uint64_t prod = uint64_t(mem.u32(G::crand_seed)) * 0x5D588B65u;
        const uint32_t lo = uint32_t(prod), hi = uint32_t(prod >> 32);
        const uint32_t r = (lo + ((hi << 1) | (lo >> 31))) & 0x7FFFFFFFu;
        mem.set32(G::crand_seed, r);
        return r;
    }
}

// the two front attack positions (attack_slots 0 / 1): a dog waiting in another position (+0x2E7 above 1) moves to a
// free front one
template <class P> void Game<P>::dog_take_front_slot(uint32_t obj)
{
    const int32_t s = of<int8_t>(obj, 0x2E7);
    if (s <= 1)
        return;
    int32_t to;
    if (mem.at<uint8_t>(G::attack_slots) == 0)
        to = 0;
    else if (mem.at<uint8_t>(G::attack_slots + 0x14) == 0)
        to = 1;
    else
        return;
    mem.set32(G::attack_slots + 4 + uint32_t(s) * 0x14, 0);
    mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 0x14) = 0;
    of<int8_t>(obj, 0x2E7) = int8_t(to);
    mem.set32(G::attack_slots + 4 + uint32_t(to) * 0x14, obj);
    mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 0x14) = 1;
}

// after a bite or a leap: the next target (2 players: the other one; 1 player: the one playing), then crand: odd (or
// a lone set-up-4 dog) state 3 motion 3 turning -0x700, else motion 2 turning +0x700
template <class P> void Game<P>::dog_after_attack(uint32_t obj, bool retarget)
{
    const uint32_t rec = dog_rec(obj);
    if (retarget) {
        const int16_t n = mem.at<int16_t>(G::players_in_game);
        if (n == 2)
            mem.at<int32_t>(rec + 0x20) = mem.at<int32_t>(rec + 0x20) != 0 ? 0 : 1;
        else if (n == 1)
            mem.at<int32_t>(rec + 0x20) = mem.at<int8_t>(G::solo_player_is_p2);
    }
    const int32_t r = int32_t(crand());
    if (r % 2 != 0 || (of<uint16_t>(obj, 0x64) == 4 && mem.u32(G::dog_pack_count) == 1)) {
        dog_motion_set(obj, 3, 3, 4);
        of<int32_t>(obj, 0xE8) = -0x700;
    } else {
        dog_motion_set(obj, 3, 2, 4);
        of<int32_t>(obj, 0xE8) = 0x700;
    }
}

// 0x7DCB0 / PC 0x4DEA08 (state 0, biting): its target from the pack; a front attack position when free; on frame 42
// of each loop of MO 0x6D the target (if playing) is hit; past frame 49 of the loop: the next target and away
// (dog_after_attack)
template <class P> void Game<P>::dog_st0_bite(uint32_t obj)
{
    const uint32_t rec = dog_rec(obj);
    of<uint8_t>(obj, 0x2E8) = mem.at<uint8_t>(rec + 0x20);
    dog_take_front_slot(obj);
    const int32_t m = of<int16_t>(obj, 0xD0);
    const uint32_t f = of<uint32_t>(obj, 0xF8);
    const uint32_t fc = mem.u32(G::motion_frame_counts + uint32_t(m) * 4);
    if (f % fc == 42 && (uint32_t(m) & 0xFFFF) == 0x6D) {
        const int32_t p = of<int8_t>(obj, 0x2E8);
        // (ours: The Typing of the Dead 0's window, game.h: a held bite snaps at the air; null in the arcade)
        if (mem.at<Player>(G::players, uint32_t(p)).state == 5 && !(gAttackHold && gAttackHold(obj))) {
            player_hit(p, 1, 1);
            return;
        }
    }
    if (f % fc <= 49 || (uint32_t(m) & 0xFFFF) != 0x6D)
        return;
    dog_after_attack(obj, true);
}

// 0x7DED0 / PC 0x4DEC88 (state 2, leaping in): no target yet; facing the camera (angle outside 0x2000..0xDFFF) after
// MO 0x6B: state 2 motion 1, else turning toward it (0x100); a front attack position when free. Running (state-2
// motions 1 / 0): sideways toward its target's side (2 players: dog_st2_move -8 / +8, speed 0.6 / 0.9) or toward its
// attack position (walk_to_attack_position); motions 3 / 2: forward 0.6 until the motion ends (then motion 0). Within
// 24 of the camera (A coprocessor 0x1F; PC the table square root): a playing target -> the bite (a sound, state 0
// over 20 frames; 2 players: +0xE8 -/+0x100 by the target), else dog_after_attack
template <class P> void Game<P>::dog_st2_leap(uint32_t obj)
{
    const uint32_t rec = dog_rec(obj);
    of<int8_t>(obj, 0x2E8) = -1;
    const int32_t a = int32_t(angle_to_camera(obj));
    if ((a > 0xDFFF || a <= 0x2000) && of<uint16_t>(obj, 0xD0) == 0x6B)
        dog_motion_set(obj, 2, 1, 8);
    else
        turn_toward_camera(obj, 0x100);
    dog_take_front_slot(obj);
    const int32_t m = of<int16_t>(obj, 0xD0);
    const int32_t mc0 = mem.at<int32_t>(G::dog_motions_c), mc1 = mem.at<int32_t>(G::dog_motions_c + 4);
    const int32_t mc2 = mem.at<int32_t>(G::dog_motions_c + 8), mc3 = mem.at<int32_t>(G::dog_motions_c + 0xC);
    auto run = [&](float speed) {
        if (mem.at<uint16_t>(G::players_in_game) == 2)
            dog_st2_move(obj, mem.at<int32_t>(rec + 0x20) == 0 ? -8.0f : 8.0f, 5.0f, speed);
        else
            walk_to_attack_position(obj, speed);
    };
    if (m == mc1) {
        run(ffrom(0x3F19999Au));   // 0.6
    } else if (int32_t(uint32_t(m) & 0xFFFF) == mc0) {
        run(ffrom(0x3F666666u));   // 0.9
    } else if (int32_t(uint32_t(m) & 0xFFFF) == mc3 || int32_t(uint32_t(m) & 0xFFFF) == mc2) {
        move_along_heading(obj, ffrom(0x3F19999Au));
        if ((of<uint8_t>(obj, 0x3C5) >> 4) & 1)
            dog_motion_set(obj, 2, 0, 4);
    }
    bool far;
    if (pc_path()) {
        const float dx = mem.f32(G::camera_pos) - of<float>(obj, 0x20);
        const float dz = mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28);
        far = pc_fast_sqrt_table(dx * dx + dz * dz) > 24.0f;
    } else {
        Tgp::Out o;
        const uint32_t v[6] = {of<uint32_t>(obj, 0x20), 0, of<uint32_t>(obj, 0x28), mem.u32(G::camera_pos), 0,
                               mem.u32(G::camera_pos + 8)};
        copro(0x1F, v, 6, o);
        far = double(ffrom(o.w[0])) > 24.0;
    }
    if (far)
        return;
    if (mem.at<Player>(G::players, uint32_t(mem.at<int32_t>(rec + 0x20))).state == 5) {
        sound_cue(P::pc ? 0x92u : 0xA01358u);
        dog_set_motion_step(obj, 0, 0, 20, 8);
        if (mem.at<uint16_t>(G::players_in_game) == 2) {
            if (mem.at<int32_t>(rec + 0x20) == 0)
                of<int32_t>(obj, 0xE8) = of<int32_t>(obj, 0xE8) - 0x100;
            else
                of<int32_t>(obj, 0xE8) = of<int32_t>(obj, 0xE8) + 0x100;
        }
        return;
    }
    dog_after_attack(obj, false);
}

// the pack is ready (A inline in dog_st1; PC 0x4DD9A4): the live dogs listed (dog_pack_count); one dog, or no dog
// in state 0 (biting) or 2 (leaping)
template <class P> bool Game<P>::dog_pack_ready()
{
    const int32_t n = dog_pack_list_build();
    mem.set32(G::dog_pack_count, uint32_t(n));
    if (n <= 1)
        return true;
    int32_t k = 0;
    for (; k < n; ++k) {
        const uint16_t s = of<uint16_t>(mem.u32(G::dog_list + uint32_t(k) * 4), 0x5C);
        if (s == 0 || s == 2)
            break;
    }
    return k == n;
}

// walking round in place (state 1's sub-states 2 / 3): heading +-0x400, 0.25 along it (A coprocessor 0x1B / 0x1C,
// PC pc_trig_table), unless +0x3C6 bit 1; facing the camera -> state 1 motion 0
template <class P> void Game<P>::dog_st1_turn(uint32_t obj, uint32_t step)
{
    if (((of<uint8_t>(obj, 0x3C6) >> 1) & 1) == 0) {
        const uint32_t h = (of<uint32_t>(obj, 0x30) + step) & 0xFFFF;
        of<uint32_t>(obj, 0x30) = h;
        of<float>(obj, 0x20) = dog_trig(0xFFFF - h, false) * 0.25f + of<float>(obj, 0x20);
        of<float>(obj, 0x28) = dog_trig(0xFFFF - of<uint32_t>(obj, 0x30), true) * 0.25f + of<float>(obj, 0x28);
    }
    const int32_t a = int32_t(angle_to_camera(obj));
    if (a > 0xE7FF || a <= 0x17FF)
        dog_motion_set(obj, 1, 0, 8);
}

// 0x7E270 / PC 0x4DF044 (state 1, waiting): no target; a waiting attack position (attack_slots 2 / 3) when free; by the
// pack's state-1 motion index: 0 / 1 once the pack is ready state 2 (motion 0; 1 player: the target is the one
// playing), 1 also back to state 1 motion 0 at the motion's end; 2 / 3 turning round (dog_st1_turn)
template <class P> void Game<P>::dog_st1(uint32_t obj)
{
    const uint32_t rec = dog_rec(obj);
    of<int8_t>(obj, 0x2E8) = -1;
    const int32_t s = of<int8_t>(obj, 0x2E7);
    if (s <= 1) {
        int32_t to = -1;
        if (mem.at<uint8_t>(G::attack_slots + 2 * 0x14) == 0)
            to = 2;
        else if (mem.at<uint8_t>(G::attack_slots + 3 * 0x14) == 0)
            to = 3;
        if (to >= 0) {
            mem.set32(G::attack_slots + 4 + uint32_t(s) * 0x14, 0);
            mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 0x14) = 0;
            of<int8_t>(obj, 0x2E7) = int8_t(to);
            mem.set32(G::attack_slots + 4 + uint32_t(to) * 0x14, obj);
            mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 0x14) = 1;
        }
    }
    const int32_t sw = mem.at<int32_t>(rec + 0x10);
    if (sw == 0 || sw == 1) {
        if (dog_pack_ready()) {
            dog_motion_set(obj, 2, 0, 8);
            if (mem.at<uint16_t>(G::players_in_game) == 1)
                mem.at<int32_t>(rec + 0x20) = mem.at<int8_t>(G::solo_player_is_p2);
        }
        if (sw == 1 && ((of<uint8_t>(obj, 0x3C5) >> 4) & 1))
            dog_motion_set(obj, 1, 0, 8);
        return;
    }
    if (sw == 2)
        dog_st1_turn(obj, 0x400);
    else if (sw == 3)
        dog_st1_turn(obj, 0xFFFFFC00u);
}

// its distance to the camera in x / z (A coprocessor 0x1F; PC the table square root of the squares)
template <class P> float Game<P>::dog_camera_distance(uint32_t obj)
{
    if (pc_path()) {
        const float dx = mem.f32(G::camera_pos) - of<float>(obj, 0x20);
        const float dz = mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28);
        return pc_fast_sqrt_table(dx * dx + dz * dz);
    } else {
        Tgp::Out o;
        const uint32_t v[6] = {of<uint32_t>(obj, 0x20), 0, of<uint32_t>(obj, 0x28), mem.u32(G::camera_pos), 0,
                               mem.u32(G::camera_pos + 8)};
        copro(0x1F, v, 6, o);
        return ffrom(o.w[0]);
    }
}

// circling (state 3's sub-states 2 / 3): heading + `step`, 0.25 along it unless +0x3C6 bit 1; facing the record's
// point (dog_angle_to_target outside 0x1800..0xE7FF) -> state 3 motion 4
template <class P> void Game<P>::dog_st3_turn(uint32_t obj, uint32_t step)
{
    const uint32_t r = of<uint32_t>(obj, 0x368);
    if (((of<uint8_t>(obj, 0x3C6) >> 1) & 1) == 0) {
        const uint32_t h = (of<uint32_t>(obj, 0x30) + step) & 0xFFFF;
        of<uint32_t>(obj, 0x30) = h;
        of<float>(obj, 0x20) = dog_trig(0xFFFF - h, false) * 0.25f + of<float>(obj, 0x20);
        of<float>(obj, 0x28) = dog_trig(0xFFFF - of<uint32_t>(obj, 0x30), true) * 0.25f + of<float>(obj, 0x28);
    }
    const int32_t a = int32_t(dog_angle_to_target(obj, mem.f32(r + 0x10), mem.f32(r + 0x14), mem.f32(r + 0x18)));
    if (a > 0xE7FF || a <= 0x17FF)
        dog_motion_set(obj, 3, 4, 8);
}

// 0x7E620 / PC 0x4DF364 (state 3, around the record's point +0x10..): a waiting attack position when free; by the
// pack's state-3 motion index (+0x18): 0 backing off (walk_to_attack_position -1) until the motion ends -> state 1
// (motion 2 when alone, else 0); 1 walking in until within 50 of the camera -> state 1; 2 / 3 circling (+0x280 /
// -0x400); 4 turning toward the point (dog_angle_to_target: 0x8000..0xEFFF motion 3 turning -0x380, 0x1000..0x7FFF
// motion 2 turning +0x380) and steering (dog_st3_steer 1.35); arrived: state 1 by the game's rand (odd, or a lone
// set-up-4 dog: motion 2 turning +0x380, else motion 3 turning -0x380)
template <class P> void Game<P>::dog_st3_circle(uint32_t obj)
{
    const uint32_t rec = dog_rec(obj);
    const uint32_t r = of<uint32_t>(obj, 0x368);
    of<int8_t>(obj, 0x2E8) = -1;
    const int32_t s = of<int8_t>(obj, 0x2E7);
    if (s <= 1) {
        int32_t to = -1;
        if (mem.at<uint8_t>(G::attack_slots + 2 * 0x14) == 0)
            to = 2;
        else if (mem.at<uint8_t>(G::attack_slots + 3 * 0x14) == 0)
            to = 3;
        if (to >= 0) {
            mem.set32(G::attack_slots + 4 + uint32_t(s) * 0x14, 0);
            mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 0x14) = 0;
            of<int8_t>(obj, 0x2E7) = int8_t(to);
            mem.set32(G::attack_slots + 4 + uint32_t(to) * 0x14, obj);
            mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 0x14) = 1;
        }
    }
    const uint32_t sw = mem.u32(rec + 0x18);
    if (sw > 4)
        return;
    switch (sw) {
    case 0:
        walk_to_attack_position(obj, -1.0f);
        if ((of<uint8_t>(obj, 0x3C5) >> 4) & 1)
            dog_motion_set(obj, 1, dog_pack_list_build() == 1 ? 2 : 0, 8);
        return;
    case 1:
        walk_to_attack_position(obj, 1.0f);
        if (dog_camera_distance(obj) < 50.0f)
            dog_motion_set(obj, 1, 0, 8);
        return;
    case 2:
        dog_st3_turn(obj, 0x280);
        return;
    case 3:
        dog_st3_turn(obj, 0xFFFFFC00u);
        return;
    default: {
        const int32_t a = int32_t(dog_angle_to_target(obj, mem.f32(r + 0x10), mem.f32(r + 0x14), mem.f32(r + 0x18)));
        if (a > 0x7FFF && a <= 0xEFFF) {
            dog_motion_set(obj, 3, 3, 8);
            of<int32_t>(obj, 0xE8) = -0x380;
        }
        if (a <= 0x7FFF && a > 0xFFF) {
            dog_motion_set(obj, 3, 2, 8);
            of<int32_t>(obj, 0xE8) = 0x380;
        }
        if (dog_st3_steer(obj, mem.f32(r + 0x10), mem.f32(r + 0x18), ffrom(0x3FACCCCDu)) == 0)
            return;
        const int32_t rn = int32_t(rand());
        if (rn % 2 != 0 || (of<uint16_t>(obj, 0x64) == 4 && mem.u32(G::dog_pack_count) == 1)) {
            dog_motion_set(obj, 1, 2, 8);
            of<int32_t>(obj, 0xE8) = 0x380;
        } else {
            dog_motion_set(obj, 1, 3, 8);
            of<int32_t>(obj, 0xE8) = -0x380;
        }
        return;
    }
    }
}

// 0x7EA80 / PC 0x4DF7BC (the dogs' frame routine): frame begin; A only: a landing dust task (dog_dust, 0x58 bytes) on
// frame 23 of MO 0x6B, 21 of 0x6D, 6 / 32 of 0xCF; while the pack's +4 runs, knocked back 1.5 a frame
// (dog_knockback); dog_kill_cue in chapter 1: a sound and life 0; dog_hits; unless dying (enemy_scripted_dead) or
// reacting (flags bit 28), the state routine (dog_states by +0x5C); frame end, depth list
template <class P> void Game<P>::dog_frame(uint32_t obj)
{
    enemy_frame_begin(obj);
    if constexpr (!P::pc) {
        const uint16_t m = of<uint16_t>(obj, 0xD0), f = of<uint16_t>(obj, 0xF4);
        if ((m == 0x6B && f == 23) || (m == 0x6D && f == 21) || (m == 0xCF && f == 6) || (m == 0xCF && f == 32)) {
            const uint32_t t = task_create(F::dog_dust, 0x58);
            for (uint32_t a = 0x20; a < 0x30; a += 4)
                of<uint32_t>(t, a) = of<uint32_t>(obj, a);
            of<int32_t>(t, 0x50) = 0;
            of<uint32_t>(t, 0x30) = of<uint32_t>(obj, 0x30);
            of<uint32_t>(t, 0x34) = of<uint32_t>(obj, 0x34);
        }
    }
    const uint32_t rec = dog_rec(obj);
    if (mem.at<int32_t>(rec + 4) != 0) {
        mem.at<int32_t>(rec + 4) = mem.at<int32_t>(rec + 4) - 1;
        dog_knockback(obj, 1.5f);
    }
    if (mem.at<uint8_t>(G::dog_kill_cue) != 0 && mem.at<uint8_t>(G::dog_kill_block) == 0 && mem.at<uint8_t>(G::chapter) == 0) {
        sound_cue(P::pc ? 0x55u : 0xA0125Bu);
        of<int16_t>(obj, 0x70) = 0;
    }
    dog_hits(obj);
    if (of<uint32_t>(obj, 0x0) == F::enemy_scripted_dead)
        return;
    if (((of<uint32_t>(obj, 0x58) >> 28) & 1) == 0)
        dog_call(mem.u32(G::dog_states + uint32_t(int32_t(of<int16_t>(obj, 0x5C))) * 4), obj);
    enemy_frame_end(obj);
    depth_list_add(obj);
}

// 0x7EBB0 / PC 0x4DDA88 (A only: the landing dust task): model 0x2CC + its frame at its position and angles (PC: only
// without pc_no_extra_model, the file's own matrix helpers); ends after 19 frames
template <class P> void Game<P>::dog_dust(uint32_t obj)
{
    if constexpr (P::pc) {
        if (mem.u32(G::pc_no_extra_model) == 0) {
            pc_matrix_push();
            pc_matrix_translate9(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28));
            pc_matrix_rotate_z9(of<uint32_t>(obj, 0x34));
            pc_matrix_rotate_y9(of<uint32_t>(obj, 0x30));
            pc_matrix_rotate_x9(of<uint32_t>(obj, 0x2C));
            geo_draw_model(of<uint32_t>(obj, 0x50) + 0x2CC);
            pcm_pop();
        }
    } else {
        Tgp::Out none;
        copro(0x05, nullptr, 0, none);
        const uint32_t t[3] = {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)};
        copro(0x12, t, 3, none);
        const uint32_t r[3] = {of<uint32_t>(obj, 0x34), of<uint32_t>(obj, 0x30), of<uint32_t>(obj, 0x2C)};
        copro(0x86, r, 3, none);
        geo_draw_model(of<uint32_t>(obj, 0x50) + 0x2CC);
        copro(0x06, nullptr, 0, none);
    }
    const int32_t n = of<int32_t>(obj, 0x50) + 1;
    of<int32_t>(obj, 0x50) = n;
    if (n > 18)
        task_exit();
}

// 0x7D960 / PC 0x4DE69C: a new hit (flags bit 8 without bit 28): reacting (bit 28); the side by its angle to the
// camera (0x1800..0x7000 1, 0x9000..0xE800 2, else 0; 3 while biting, MO 0x6D) into the pack's +0x1C, the pack knocked
// back 4 frames (not while biting); its hit motion. Reacting: at the end of the bite reaction (frame 60) state 3
// turned round (walk motion 4); of the others (frame 20) state 2 with motion 2 or 3 at random. Life minus hit_damage
// at 0 (or return_false): not shootable, its death motion by the angle to the camera
// (MO 0x74 / 0x73 / 0x72 / 0x6A), enemy_scripted_dead, one less alive (flags bit 20 while others live), a sound
template <class P> void Game<P>::dog_hits(uint32_t obj)
{
    const uint32_t rec = dog_rec(obj);
    const uint32_t f = of<uint32_t>(obj, 0x58);
    if ((f & 0x10000100u) == 0x100u) {
        of<uint32_t>(obj, 0x58) = (f | 0x10000000u) & ~0x100u;
        if (of<uint16_t>(obj, 0xD0) == 0x6D) {
            mem.at<int32_t>(rec + 0x1C) = 3;
        } else {
            const int32_t a = int32_t(angle_to_camera(obj));
            if (a > 0x17FF && a <= 0x7000)
                mem.at<int32_t>(rec + 0x1C) = 1;
            else if (a > 0x8FFF && a <= 0xE800)
                mem.at<int32_t>(rec + 0x1C) = 2;
            else
                mem.at<int32_t>(rec + 0x1C) = 0;
            mem.at<int32_t>(rec + 4) = 4;
        }
        set_motion(obj, mem.u32(G::dog_hit_motions + uint32_t(mem.at<int32_t>(rec + 0x1C)) * 4));
    } else if ((f >> 28) & 1) {
        if (int32_t(of<int16_t>(obj, 0xD0)) == mem.at<int32_t>(G::dog_hit_motions + 0xC)) {
            if (of<int16_t>(obj, 0xF4) > 59) {
                of<int16_t>(obj, 0x5C) = 3;
                of<uint32_t>(obj, 0x30) = of<uint32_t>(obj, 0x30) ^ 0x8000u;
                of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x10000000u;
                mem.at<int32_t>(rec + 8) = -1;
                mem.at<int32_t>(rec + 0x10) = -1;
                mem.at<int32_t>(rec + 0x14) = -1;
                mem.at<int32_t>(rec + 0x18) = 4;
                set_motion(obj, mem.u32(G::dog_motions_d + uint32_t(mem.at<int32_t>(rec + 0x18)) * 4));
            }
        } else if (of<int16_t>(obj, 0xF4) > 19) {
            of<int16_t>(obj, 0x5C) = 2;
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x10000000u;
            const int32_t r = int32_t(rand());
            dog_motion_set(obj, 2, (r % 2) != 0 ? 2 : 3, 8);
        }
    }
    const int16_t hp = int16_t(of<int16_t>(obj, 0x70) - mem.at<int16_t>(G::hit_damage));
    of<int16_t>(obj, 0x70) = hp;
    if (hp > 0 && return_false() == 0)
        return;
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
    const int32_t a = int32_t(angle_to_camera(obj));
    uint32_t m;
    if (a > 0x9FFF && a <= 0xDFFF)
        m = 0x74;
    else if (a > 0x1FFF && a <= 0x5FFF)
        m = 0x73;
    else if (a > 0x5FFF && a <= 0x9FFF)
        m = 0x72;
    else
        m = 0x6A;
    set_motion_blend(obj, m, 0, 4);
    of<int32_t>(obj, 0x50) = 0;
    of<uint32_t>(obj, 0x0) = F::enemy_scripted_dead;
    if (mem.at<int16_t>(G::enemies_alive) > 1)
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x100000u;
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
    sound_cue(P::pc ? 0x93u : 0xA01359u);
}

// the arcade's direction -> heading: coprocessor 0x0F, folded into the front half (pitch word bit 15 != bit 14: the
// yaw + 0x8000)
template <class P> uint32_t Game<P>::dog_copro_angle(const uint32_t* v)
{
    Tgp::Out o;
    copro(0x0F, v, 3, o);
    uint32_t yaw = o.w[0];
    const uint32_t p = o.w[1] & 0xFFFF;
    if (((p << 1) ^ p) & 0x8000)
        yaw += 0x8000;
    return yaw;
}

// the square of the natural log of a distance: A logrl (double, MAME's form) rounded to a float, squared as floats;
// PC fyl2x (not rounded by the 24-bit precision control) squared and rounded to 24 bits
template <class P> float Game<P>::dog_log_sq(float d)
{
    if (pc_path()) {
        const double l = std::log(double(d));
        return float(l * l);
    } else {
        const float l = float(i960_logrl(double(d), ffrom64(0x3FE62E42FEFA39EFull)));
        return l * l;
    }
}

// 0x69880 / PC 0x4C4F5C (object, step): the dog moved `step` along the direction from the camera to it (the
// camera-relative position's heading, A coprocessor 0x0F + the fold, PC pc_vec_angles12; then (0, 0, step) turned by
// it: A 0x10 / 0x15 / 0x1A, PC the identity copied in, pc_matrix_rotate_y12, pc_matrix_transform12)
template <class P> void Game<P>::dog_knockback(uint32_t obj, float step)
{
    if (pc_path()) {
        pc_matrix_push();
        pc_matrix_identity12();
        pc_matrix_translate12(-mem.f32(G::camera_pos), -mem.f32(G::camera_pos + 4), -mem.f32(G::camera_pos + 8));
        float rx, ry, rz;
        pc_matrix_transform12(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28), rx, ry, rz);
        uint32_t yaw = 0, pitch = 0;
        pc_vec_angles12(rx, ry, rz, yaw, pitch);
        pcm_identity();
        pc_matrix_rotate_y12(yaw);
        float vx, vy, vz;
        pc_matrix_transform12(0.0f, 0.0f, step, vx, vy, vz);
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + vx;
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
        of<float>(obj, 0x28) = of<float>(obj, 0x28) + vz;
        pcm_pop();
    } else {
        Tgp::Out o, none;
        copro(0x21, nullptr, 0, none);
        const uint32_t c[3] = {mem.u32(G::camera_pos) ^ 0x80000000u, mem.u32(G::camera_pos + 4) ^ 0x80000000u,
                               mem.u32(G::camera_pos + 8) ^ 0x80000000u};
        copro(0x12, c, 3, none);
        const uint32_t p[3] = {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)};
        copro(0x1A, p, 3, o);
        const uint32_t r[3] = {o.w[0], o.w[1], o.w[2]};
        const uint32_t yaw[1] = {dog_copro_angle(r)};
        copro(0x10, nullptr, 0, none);
        copro(0x15, yaw, 1, none);
        const uint32_t s[3] = {0, 0, fbits(step)};
        copro(0x1A, s, 3, o);
        const float vx = ffrom(o.w[0]), vy = ffrom(o.w[1]), vz = ffrom(o.w[2]);
        copro(0x06, nullptr, 0, none);
        of<float>(obj, 0x28) = vz + of<float>(obj, 0x28);
        of<float>(obj, 0x20) = vx + of<float>(obj, 0x20);
        of<float>(obj, 0x24) = vy + of<float>(obj, 0x24);
    }
}

// 0x1CD60 / PC 0x447698 (object, side, within, speed; state 2 with two players): the point (side, 0, within) in the
// camera's frame (its position, its yaw: camera_records + 0x10); velocity toward it (A 0x0C; PC over the table
// length), turned toward it at 0x100 and a step of `speed`
template <class P> void Game<P>::dog_st2_move(uint32_t obj, float side, float within, float speed)
{
    if (pc_path()) {
        pc_matrix_push();
        pc_matrix_identity11();
        pc_matrix_translate11(mem.f32(G::camera_pos), 0.0f, mem.f32(G::camera_pos + 8));
        pc_matrix_rotate_z11(0);
        pc_matrix_rotate_y11(mem.u32(G::camera_records + 0x10));
        pc_matrix_rotate_x11(0);
        pc_matrix_translate11(side, 0.0f, within);
        const float dx = pcm(9) - of<float>(obj, 0x20);
        const float dz = pcm(11) - of<float>(obj, 0x28);
        pcm_pop();
        const float inv = 1.0f / pc_fast_sqrt4(dx * dx + dz * dz);
        of<float>(obj, 0x78) = dx * inv;
        of<float>(obj, 0x7C) = 0.0f * inv;
        of<float>(obj, 0x80) = dz * inv;
        uint32_t yaw = 0, pitch = 0;
        pc_vec_angles3(dx, 0.0f, dz, yaw, pitch);
        angle_turn_toward(obj, yaw, 0x100);
    } else {
        Tgp::Out o, none;
        copro(0x21, nullptr, 0, none);
        const uint32_t c[3] = {mem.u32(G::camera_pos), 0, mem.u32(G::camera_pos + 8)};
        copro(0x12, c, 3, none);
        const uint32_t r[3] = {0, mem.u32(G::camera_records + 0x10), 0};
        copro(0x86, r, 3, none);
        const uint32_t s[3] = {fbits(side), 0, fbits(within)};
        copro(0x12, s, 3, none);
        copro(0x20, nullptr, 0, o);
        const float dx = ffrom(o.w[0]) - of<float>(obj, 0x20);
        const float dz = ffrom(o.w[2]) - of<float>(obj, 0x28);
        copro(0x06, nullptr, 0, none);
        const uint32_t d[3] = {fbits(dx), 0, fbits(dz)};
        copro(0x0C, d, 3, o);
        of<uint32_t>(obj, 0x78) = o.w[0];
        of<uint32_t>(obj, 0x7C) = o.w[1];
        of<uint32_t>(obj, 0x80) = o.w[2];
        angle_turn_toward(obj, dog_copro_angle(d), 0x100);
    }
    move_along_heading(obj, speed);
}

// 0x1D9A0 / PC 0x447E60 (object, point): the point in the dog's own frame (minus its angles x, y, z, minus its
// position), its heading (A 0x0F + the fold; PC pc_vec_angles3): the low 16 bits
template <class P> uint32_t Game<P>::dog_angle_to_target(uint32_t obj, float x, float y, float z)
{
    if (pc_path()) {
        pc_matrix_push();
        pc_matrix_identity11();
        pc_matrix_rotate_x11(0u - of<uint32_t>(obj, 0x2C));
        pc_matrix_rotate_y11(0u - of<uint32_t>(obj, 0x30));
        pc_matrix_rotate_z11(0u - of<uint32_t>(obj, 0x34));
        pc_matrix_translate11(-of<float>(obj, 0x20), -of<float>(obj, 0x24), -of<float>(obj, 0x28));
        float lx, ly, lz;
        pc_matrix_transform11(x, y, z, lx, ly, lz);
        uint32_t yaw = 0, pitch = 0;
        pc_vec_angles3(lx, ly, lz, yaw, pitch);
        pcm_pop();
        return yaw & 0xFFFF;
    } else {
        Tgp::Out o, none;
        copro(0x21, nullptr, 0, none);
        const uint32_t ax[1] = {0u - of<uint32_t>(obj, 0x2C)};
        copro(0x14, ax, 1, none);
        const uint32_t ay[1] = {0u - of<uint32_t>(obj, 0x30)};
        copro(0x15, ay, 1, none);
        const uint32_t az[1] = {0u - of<uint32_t>(obj, 0x34)};
        copro(0x16, az, 1, none);
        const uint32_t t[3] = {of<uint32_t>(obj, 0x20) ^ 0x80000000u, of<uint32_t>(obj, 0x24) ^ 0x80000000u,
                               of<uint32_t>(obj, 0x28) ^ 0x80000000u};
        copro(0x12, t, 3, none);
        const uint32_t p[3] = {fbits(x), fbits(y), fbits(z)};
        copro(0x1A, p, 3, o);
        const uint32_t l[3] = {o.w[0], o.w[1], o.w[2]};
        const uint32_t yaw = dog_copro_angle(l);
        copro(0x06, nullptr, 0, none);
        return yaw & 0xFFFF;
    }
}

// 0x6A0B0 / PC 0x4C53D8 (object, point x / z, speed; state 3 sub-state 4): velocity toward the point; the heading
// turned toward it at a rate of 6553.6 / ln(distance)^2 (angle_turn_toward written out; PC: the heading's upper half
// cleared first); a step of `speed` (A: (0, 0, speed) through its angles; PC move_along_heading); 1 once within
// 2 x speed
template <class P> uint32_t Game<P>::dog_st3_steer(uint32_t obj, float x, float z, float speed)
{
    uint32_t yaw;
    int32_t rate;
    if (pc_path()) {
        const float ex = x - of<float>(obj, 0x20), ez = z - of<float>(obj, 0x28);
        const float d = pc_fast_sqrt12(ex * ex + ez * ez);
        const float r = 6553.6f / dog_log_sq(d);   // 0x5A4078
        const float dx = x - of<float>(obj, 0x20), dz = z - of<float>(obj, 0x28);
        const float s = dx * dx + dz * dz;
        rate = trunc_int(double(r));
        const float inv = 1.0f / pc_fast_sqrt12(s);
        of<float>(obj, 0x78) = dx * inv;
        of<float>(obj, 0x7C) = 0.0f * inv;
        of<float>(obj, 0x80) = dz * inv;
        uint32_t pitch = 0;
        yaw = 0;
        pc_vec_angles12(dx, 0.0f, dz, yaw, pitch);
        yaw &= 0xFFFF;
        of<uint16_t>(obj, 0x32) = 0;
    } else {
        Tgp::Out o;
        const uint32_t p[6] = {of<uint32_t>(obj, 0x20), 0, of<uint32_t>(obj, 0x28), fbits(x), 0, fbits(z)};
        copro(0x1F, p, 6, o);
        const float d = ffrom(o.w[0]);
        const float dx = x - of<float>(obj, 0x20), dz = z - of<float>(obj, 0x28);
        const uint32_t v[3] = {fbits(dx), 0, fbits(dz)};
        copro(0x0C, v, 3, o);
        of<uint32_t>(obj, 0x78) = o.w[0];
        of<uint32_t>(obj, 0x7C) = o.w[1];
        of<uint32_t>(obj, 0x80) = o.w[2];
        const float l2 = dog_log_sq(d);
        yaw = dog_copro_angle(v) & 0xFFFF;
        rate = trunc_int(double(ffrom(0x45CCCCCDu) / l2));   // 6553.6
    }
    // the turn (A: the heading's low 16 bits; PC: the heading after its upper half was cleared)
    const int32_t cur = pc_path() ? of<int32_t>(obj, 0x30) : int32_t(of<uint16_t>(obj, 0x30));
    const int32_t tgt = int32_t(yaw);
    auto absd = [](int32_t a, int32_t b) { const int32_t v = int32_t(uint32_t(a) - uint32_t(b)); return v < 0 ? int32_t(0u - uint32_t(v)) : v; };
    const int32_t a = absd(cur, tgt), b = absd(tgt + 0x10000, cur), c = absd(cur + 0x10000, tgt);
    uint32_t h;
    if (a <= rate || b <= rate || c <= rate)
        h = uint32_t(tgt);
    else if (cur < tgt)
        h = a > c ? uint32_t(cur) - uint32_t(rate) : uint32_t(cur) + uint32_t(rate);
    else
        h = a > b ? uint32_t(cur) + uint32_t(rate) : uint32_t(cur) - uint32_t(rate);
    of<uint32_t>(obj, 0x30) = h;
    float d2;
    if (pc_path()) {
        move_along_heading(obj, speed);
        const float ex = x - of<float>(obj, 0x20), ez = z - of<float>(obj, 0x28);
        d2 = pc_fast_sqrt12(ex * ex + ez * ez);
        return speed * 2.0f > d2 ? 1u : 0u;   // 0x5A407C
    } else {
        Tgp::Out o, none;
        copro(0x21, nullptr, 0, none);
        const uint32_t r[3] = {of<uint32_t>(obj, 0x34), of<uint32_t>(obj, 0x30), of<uint32_t>(obj, 0x2C)};
        copro(0x86, r, 3, none);
        const uint32_t s[3] = {0, 0, fbits(speed)};
        copro(0x1A, s, 3, o);
        copro(0x06, nullptr, 0, none);
        of<float>(obj, 0x24) = ffrom(o.w[1]) + of<float>(obj, 0x24);
        of<float>(obj, 0x20) = ffrom(o.w[0]) + of<float>(obj, 0x20);
        of<float>(obj, 0x28) = ffrom(o.w[2]) + of<float>(obj, 0x28);
        const uint32_t p[6] = {of<uint32_t>(obj, 0x20), 0, of<uint32_t>(obj, 0x28), fbits(x), 0, fbits(z)};
        copro(0x1F, p, 6, o);
        d2 = ffrom(o.w[0]);
        return d2 >= speed + speed ? 0u : 1u;
    }
}

// 0x1D710 / PC 0x447C34 (from, to, angle; set-up 3's jump): the speed for a jump at `angle` that falls 0.0544 a frame:
// sqrt(-0.0544 d^2 / (2 cos (cos (ty - y) - sin d))), d the floor distance (A coprocessor 0x1F, 0x1B / 0x1C, 0x2D;
// PC the table square root, pc_trig_table, the table square root written out, its own operation order)
template <class P> float Game<P>::dog_set3_fall(float x, float y, float z, float tx, float ty, float tz, uint32_t angle)
{
    if (pc_path()) {
        const float ex = tx - x, ez = tz - z;
        const float d = pc_fast_sqrt4(ex * ex + ez * ez);
        const float s = dog_trig(angle, false), c = dog_trig(angle, true);
        const float a = (ty - y) * c - s * d;
        const float b = a * (c * 2.0f);   // 0x5A288C
        const float n = d * (d * ffrom(0xBD5F0123u));   // 0x5A2890: -0.0544
        return pc_fast_sqrt_table(n / b);
    } else {
        Tgp::Out o;
        const uint32_t p[6] = {fbits(x), 0, fbits(z), fbits(tx), 0, fbits(tz)};
        copro(0x1F, p, 6, o);
        const float d = ffrom(o.w[0]);
        const uint32_t ang[1] = {angle};
        copro(0x1B, ang, 1, o);
        const float sd = d * ffrom(o.w[0]);
        copro(0x1C, ang, 1, o);
        const float c = ffrom(o.w[0]);
        float k = ffrom(0xBD5F0123u) * d;
        float a = c * (ty - y);
        const float c2 = c + c;
        a = a - sd;
        k = d * k;
        const float b = a * c2;
        const uint32_t q[1] = {fbits(k / b)};
        copro(0x2D, q, 1, o);
        return ffrom(o.w[0]);
    }
}

// 0x1C820 / PC 0x4472EC (object, point, step; set-up 4's curve): velocity toward the point; turned toward it at a
// rate of 36 / ln(d)^2 x 65536 / 360 (d its floor distance; A double precision after the logrl, PC at 24 bits after
// fyl2x, both truncated), a step; 1 once within 2 x step
template <class P> uint32_t Game<P>::dog_set4_path(uint32_t obj, float x, float y, float z, float step)
{
    if (pc_path()) {
        const float ex = x - of<float>(obj, 0x20), ez = z - of<float>(obj, 0x28);
        const float d = pc_fast_sqrt4(ex * ex + ez * ez);
        float r = float(36.0 / double(dog_log_sq(d)));   // 0x5A283C
        const float dx = x - of<float>(obj, 0x20), dy = y - of<float>(obj, 0x24);
        r = float(double(r) * 65536.0);   // 0x5A2844
        const float dz = z - of<float>(obj, 0x28);
        const float s = (dx * dx + dy * dy) + dz * dz;
        r = float(double(r) * ffrom64(0x3F66C16C16C16C16ull));   // 0x5A284C: 1 / 360
        const int32_t rate = trunc_int(double(r));
        const float inv = 1.0f / pc_fast_sqrt4(s);
        of<float>(obj, 0x78) = dx * inv;
        of<float>(obj, 0x7C) = dy * inv;
        of<float>(obj, 0x80) = dz * inv;
        uint32_t yaw = 0, pitch = 0;
        pc_vec_angles3(dx, dy, dz, yaw, pitch);
        angle_turn_toward(obj, yaw, uint32_t(rate));
        move_along_heading(obj, step);
        const float fx = x - of<float>(obj, 0x20), fz = z - of<float>(obj, 0x28);
        return step * 2.0f > pc_fast_sqrt4(fx * fx + fz * fz) ? 1u : 0u;   // 0x5A2854
    } else {
        Tgp::Out o;
        const uint32_t p[6] = {of<uint32_t>(obj, 0x20), 0, of<uint32_t>(obj, 0x28), fbits(x), 0, fbits(z)};
        copro(0x1F, p, 6, o);
        double r = 36.0 / double(dog_log_sq(ffrom(o.w[0])));
        r = 65536.0 * r;
        r = r / 360.0;
        const float dx = x - of<float>(obj, 0x20), dy = y - of<float>(obj, 0x24), dz = z - of<float>(obj, 0x28);
        const uint32_t v[3] = {fbits(dx), fbits(dy), fbits(dz)};
        copro(0x0C, v, 3, o);
        of<uint32_t>(obj, 0x78) = o.w[0];
        of<uint32_t>(obj, 0x7C) = o.w[1];
        of<uint32_t>(obj, 0x80) = o.w[2];
        const uint32_t yaw = dog_copro_angle(v);
        angle_turn_toward(obj, yaw, uint32_t(trunc_int(r)));
        move_along_heading(obj, step);
        const uint32_t q[6] = {of<uint32_t>(obj, 0x20), 0, of<uint32_t>(obj, 0x28), fbits(x), 0, fbits(z)};
        copro(0x1F, q, 6, o);
        return ffrom(o.w[0]) >= step + step ? 0u : 1u;
    }
}

// the PC copies of the generic matrix / vector helpers in the shared helpers file (11) and the 0x4C1xxx file (12)
template <class P> void Game<P>::pc_matrix_identity11() { pc_matrix_identity(); }
template <class P> void Game<P>::pc_matrix_rotate_y11(uint32_t angle) { pc_matrix_rotate_y(angle); }
template <class P> void Game<P>::pc_matrix_transform11(float x, float y, float z, float& ox, float& oy, float& oz)
{
    pc_matrix_transform(x, y, z, ox, oy, oz);
}
template <class P> void Game<P>::pc_matrix_identity12() { pc_matrix_identity(); }
template <class P> void Game<P>::pc_matrix_rotate_y12(uint32_t angle) { pc_matrix_rotate_y(angle); }
template <class P> void Game<P>::pc_matrix_transform12(float x, float y, float z, float& ox, float& oy, float& oz)
{
    pc_matrix_transform(x, y, z, ox, oy, oz);
}
template <class P> void Game<P>::pc_vec_angles12(float x, float y, float z, uint32_t& yaw, uint32_t& pitch)
{
    pc_vec_angles(x, y, z, yaw, pitch);
}
template <class P> float Game<P>::pc_fast_sqrt12(float x) { return pc_fast_sqrt_table(x); }

} // namespace hotd::port
