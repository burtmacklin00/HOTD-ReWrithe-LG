// Teaser L6: the prop-like scene kinds of Chapter 1 besides kind 128 (props.cpp): 21 (a scene animation), 38 (the
// barrel), 76 / 77 (the cell slider and its lock), 140 (a dropped weapon), 169 (a camera-window model), 175 (the
// rat), 181 (the opening car), and the branch-event kinds. Read from the arcade disassembly; arcade forms only.
#include "port/game.h"

namespace hotd::port {

namespace pk {
// arcade addresses not in names.tsv yet (A only)
constexpr uint32_t camera_frame = 0x51EB9C;     // camera record 0's frame (float)
constexpr uint32_t window_models = 0xA8520;     // kind 169: 3 words per entry: model, model after the trigger, sound
constexpr uint32_t barrel_kits = 0xC97A0;       // kind 38: 0x34 bytes per kit (+4 floor height, +8 model offset,
                                                // +0x14 tumble block start, +0x30 model)
constexpr uint32_t model_table = 0x2EEC390;     // main_data: per model its header (+0x18 the bounding radius)
constexpr uint32_t move_routine = 0xC681C;      // the routine kind38_barrel_break calls each frame (obj_move_velocity)
constexpr uint32_t car_done = 0x543390;         // kind 181: 1 once the car has stopped
constexpr uint32_t opening_skip = 0x520948;     // the opening scene was skipped
constexpr uint32_t title_code = 0x51F42C;       // the character models of player_char_props_draw (8 bytes each)
constexpr uint32_t players_in = 0x51F4A0;       // int16: 2 with both players
constexpr uint32_t player2_only = 0x51F700;     // byte: only player 2 is in
} // namespace pk

// ---------------------------------------------------------------------------------------------------------------
// shared small routines

// 0x6A830 obj_move_velocity: velocity += acceleration (+0x84..), position += velocity (+0x78..)
template <class P> void Game<P>::obj_move_velocity(uint32_t obj)
{
    const float vx = of<float>(obj, 0x78) + of<float>(obj, 0x84);
    const float vy = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
    const float vz = of<float>(obj, 0x80) + of<float>(obj, 0x8C);
    of<float>(obj, 0x78) = vx;
    of<float>(obj, 0x7C) = vy;
    of<float>(obj, 0x80) = vz;
    of<float>(obj, 0x20) = of<float>(obj, 0x20) + vx;
    of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
    of<float>(obj, 0x28) = of<float>(obj, 0x28) + vz;
}

// 0x637B0 obj_model_draw_colour: the light record of +0x6A, the model placed (T, Rx, Ry, Rz, S), light 0 again
template <class P> void Game<P>::obj_model_draw_colour(uint32_t obj)
{
    const uint32_t rec = 0x520A40 + uint32_t(int32_t(of<int16_t>(obj, 0x6A))) * 12;
    hw_write32(0x8000A0, 0xA0A);
    hw_write32(0x804000, mem.u32(rec));
    hw_write32(0x804000, mem.u32(rec + 4));
    hw_write32(0x804000, mem.u32(rec + 8));
    cam_copro(0x05, {});
    prop_place_xyz(obj);
    prop_scale(of<uint32_t>(obj, 0x94));
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
    prop_light0();
}

// ---------------------------------------------------------------------------------------------------------------
// kind 169

// 0x3390 kind169_camera_window (kind 169): a model (table 0xA8520 by +0x70) shown from its creation; at the record's
// first camera frame (+0x35C -> +0) or with its first flag (+8) the second model and the sound; gone at the second
// camera frame (+4) or with the second flag (+9); drawn turned z (negated), y, x
template <class P> void Game<P>::kind169_camera_window(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    const float frame = mem.f32(pk::camera_frame);
    auto ended = [&]() {
        return float(mem.at<int32_t>(rec + 4)) == mem.f32(pk::camera_frame) ||
               mem.at<uint8_t>(G::event_flags + uint32_t(int32_t(mem.at<int8_t>(rec + 9)))) == 1;
    };
    (void)frame;
    int32_t s = of<int16_t>(obj, 0x60);
    bool check2 = false;
    if (s == 0) {
        const int32_t a = of<int16_t>(obj, 0x70);
        of<int16_t>(obj, 0x64) = int16_t(a);
        of<uint32_t>(obj, 0x54) = mem.u32(pk::window_models + uint32_t(a * 3) * 4);
        of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
        s = 1;
    }
    if (s == 1) {
        if (ended()) {
            task_exit();
            return;
        }
        if (float(mem.at<int32_t>(rec)) == mem.f32(pk::camera_frame) ||
            mem.at<uint8_t>(G::event_flags + uint32_t(int32_t(mem.at<int8_t>(rec + 8)))) == 1) {
            const uint32_t e = pk::window_models + uint32_t(int32_t(of<int16_t>(obj, 0x64)) * 3) * 4;
            if (mem.u32(e + 8) != 0xFFFFFFFFu)
                sound_cue(mem.u32(e + 8));
            of<uint32_t>(obj, 0x54) = mem.u32(pk::window_models + uint32_t(int32_t(of<int16_t>(obj, 0x64)) * 3) * 4 + 4);
            of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
            check2 = true;
        }
    } else if (s == 2) {
        check2 = true;
    }
    if (check2 && ended()) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x16, {0u - of<uint32_t>(obj, 0x34)});
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    cam_copro(0x14, {of<uint32_t>(obj, 0x2C)});
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

// ---------------------------------------------------------------------------------------------------------------
// kind 21

// 0x311F0 kind21_scene_mover (kind 21): waits for the record's camera frame (+0x35C -> +0xC, unsigned) or its flag
// (+0x10), then plays models 0x3BD..0x3F2 one per frame, placed T Rz Ry Rx and scaled by the record's +0 / +4 / +8,
// then ends
template <class P> void Game<P>::kind21_scene_mover(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    int32_t s = of<int16_t>(obj, 0x60);
    if (s == 0) {
        of<uint32_t>(obj, 0x33C) = mem.u32(rec);
        of<uint32_t>(obj, 0x340) = mem.u32(rec + 4);
        of<uint32_t>(obj, 0x344) = mem.u32(rec + 8);
        of<uint32_t>(obj, 0x54) = 0x3BC;
        of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
        s = 1;
    }
    if (s == 1) {
        const int32_t w = mem.at<int32_t>(rec + 0xC);
        float at = float(w);
        if (w < 0)
            at = at + 4294967296.0f;
        if (at > mem.f32(pk::camera_frame) &&
            mem.at<uint8_t>(G::event_flags + uint32_t(int32_t(mem.at<int8_t>(rec + 0x10)))) != 1)
            return;
        of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
    } else if (s != 2) {
        return;
    }
    const uint32_t m = of<uint32_t>(obj, 0x54) + 1;
    of<uint32_t>(obj, 0x54) = m;
    if (int32_t(m) > 0x3F2) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x16, {of<uint32_t>(obj, 0x34)});
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    cam_copro(0x14, {of<uint32_t>(obj, 0x2C)});
    cam_copro(0x13, {of<uint32_t>(obj, 0x33C), of<uint32_t>(obj, 0x340), of<uint32_t>(obj, 0x344)});
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

// ---------------------------------------------------------------------------------------------------------------
// kind 181, the car of the opening scene

// 0x4E280 kind181_car (kind 181): not stopped yet (0x543390); kind181_car_wait
template <class P> void Game<P>::kind181_car(uint32_t obj)
{
    mem.set32(pk::car_done, 0);
    of<uint32_t>(obj, 0) = F::kind181_car_wait;
}

// 0x4E2A0 kind181_car_wait: with flag 0x51E9CD: tyre angle -1, 60 frames of kind181_car_drive_in; skipped opening:
// kind181_car_skipped
template <class P> void Game<P>::kind181_car_wait(uint32_t obj)
{
    if (mem.at<uint8_t>(G::event_flags + 0x8D) != 0) {
        of<uint32_t>(obj, 0x34) = 0xFFFFFFFFu;
        of<uint32_t>(obj, 0x50) = 60;
        of<uint32_t>(obj, 0) = F::kind181_car_drive_in;
    }
    if (mem.u32(pk::opening_skip) != 0)
        of<uint32_t>(obj, 0) = F::kind181_car_skipped;
}

// the car body with its wheels (inline in the car routines): T(pos) Ry(+0x30); the front wheels (0x953, the first
// steered by `steer` when given, both rolling by +0x2C unless `roll` is false for the second), the tyre animation
// (0xA49 / 0xA5C + frame % 19), the chassis 0x952 half a unit lower, then the body 0x94F turned by +0x34 (`bodyCmd`)
// and the players inside (player_char_props_draw)
template <class P> void Game<P>::car_draw(uint32_t obj, bool steer, uint32_t bodyCmd)
{
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    cam_copro(0x05, {});
    cam_copro(0x12, {0xC0C5D97F, 0x4069999A, 0xC18C1518});
    if (steer)
        cam_copro(0x15, {of<uint32_t>(obj, 0x44)});
    cam_copro(0x14, {of<uint32_t>(obj, 0x2C)});
    geo_draw_model(0x953);
    cam_copro(0x06, {});
    cam_copro(0x05, {});
    cam_copro(0x12, {0xC0E0AE7D, 0x4069999A, 0x411D2196});
    if (!steer)
        cam_copro(0x14, {of<uint32_t>(obj, 0x2C)});
    geo_draw_model(0x953);
    int32_t f = mem.at<int32_t>(G::frame_counter) % 19;
    cam_copro(0x06, {});
    cam_copro(0x05, {});
    cam_copro(0x12, {0xC0C5D97F, 0x4069999A, 0xC18C1518});
    geo_draw_model(uint32_t(f + 0xA49));
    f = mem.at<int32_t>(G::frame_counter) % 19;
    cam_copro(0x06, {});
    cam_copro(0x05, {});
    cam_copro(0x12, {0x40C5D97F, 0x4069999A, 0xC18C1518});
    geo_draw_model(uint32_t(f + 0xA5C));
    cam_copro(0x06, {});
    cam_copro(0x05, {});
    cam_copro(0x12, {0, 0xBF000000, 0});
    geo_draw_model(0x952);
    cam_copro(0x06, {});
    cam_copro(int(bodyCmd), {of<uint32_t>(obj, 0x34)});
    geo_draw_model(0x94F);
    player_char_props_draw();
    cam_copro(0x06, {});
}

// the parked car (inline): T(pos) Ry(+0x30), the body 0x951, the open door 0x950 at (-9.13, 0, -9.76) turned y by
// `door` when given
template <class P> void Game<P>::car_parked_draw(uint32_t obj, bool door)
{
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    geo_draw_model(0x951);
    cam_copro(0x05, {});
    cam_copro(0x12, {0xC112288D, 0, 0xC11C37B5});
    if (door)
        cam_copro(0x15, {of<uint32_t>(obj, 0x2C)});
    geo_draw_model(0x950);
    cam_copro(0x06, {});
}

// 0x4E2F0 kind181_car_drive_in: 4 units a frame along x, the body bobbing (sine of frame * 5 << 12 x 96), wheels
// rolling 0x6000 a frame; drawn; sound 0xA0135B at 30 frames left; after 60 frames kind181_car_brake (80 frames);
// skipped opening: kind181_car_skipped
template <class P> void Game<P>::kind181_car_drive_in(uint32_t obj)
{
    of<float>(obj, 0x20) = of<float>(obj, 0x20) + 4.0f;
    const uint32_t a = uint32_t(mem.at<int32_t>(G::frame_counter) * 5) << 12;
    const Tgp::Out s = cam_copro(0x1B, {a});
    const float b = ffrom(s.w[0]) * 96.0f;
    of<uint32_t>(obj, 0x2C) += 0x6000;
    of<int32_t>(obj, 0x34) = int32_t(b);
    car_draw(obj, false, 0x14);
    if (of<int32_t>(obj, 0x50) == 30)
        sound_cue(0xA0135B);
    int32_t t = of<int32_t>(obj, 0x50);
    if (t == 1) {
        of<uint32_t>(obj, 0x44) = 0;
        of<uint32_t>(obj, 0) = F::kind181_car_brake;
        t = 0x50;
    } else {
        t -= 1;
    }
    of<int32_t>(obj, 0x50) = t;
    if (mem.u32(pk::opening_skip) != 0)
        of<uint32_t>(obj, 0) = F::kind181_car_skipped;
}

// the turn toward heading 0x8000 by 1 / div of the difference (inline in brake / stop)
template <class P> void Game<P>::car_turn(uint32_t obj, int32_t div)
{
    const int32_t h = of<int32_t>(obj, 0x30);
    int32_t d = int32_t(uint32_t(0x8000 - h) & 0xFFFF);
    if (d > 0x7FFF)
        d -= 0x10000;
    of<int32_t>(obj, 0x30) = h + d / div;
}

// 0x4E600 kind181_car_brake: slows down (x = x * 0.952 - 0.762), turns (1/31), the body tilts back (to -0x300),
// wheels steer out (+0x44 to 0x1800 while more than 75 frames are left, then back to -0x1800); drawn; after 80 frames
// kind181_car_stop (80 frames); skipped opening: kind181_car_skipped
template <class P> void Game<P>::kind181_car_brake(uint32_t obj)
{
    of<float>(obj, 0x20) = of<float>(obj, 0x20) * ffrom(0x3F73CF3D) + ffrom(0xBF430C31);
    car_turn(obj, 31);
    int32_t tilt = of<int32_t>(obj, 0x34);
    if (tilt > -0x300)
        tilt -= 0x40;
    of<int32_t>(obj, 0x34) = tilt;
    of<uint32_t>(obj, 0x2C) += 0x6000;
    int32_t steer = of<int32_t>(obj, 0x44);
    if (of<int32_t>(obj, 0x50) > 75) {
        if (steer != 0x1800)
            steer += 0x400;
    } else if (steer != -0x1800) {
        steer -= 0x400;
    }
    of<int32_t>(obj, 0x44) = steer;
    car_draw(obj, true, 0x16);
    int32_t t = of<int32_t>(obj, 0x50);
    if (t == 1) {
        of<uint32_t>(obj, 0x34) = 0;
        of<uint32_t>(obj, 0) = F::kind181_car_stop;
        t = 0x50;
    } else {
        t -= 1;
    }
    of<int32_t>(obj, 0x50) = t;
    if (mem.u32(pk::opening_skip) != 0)
        of<uint32_t>(obj, 0) = F::kind181_car_skipped;
}

// 0x4E960 kind181_car_stop: x = x * 0.952 - 0.762, z = z * 0.9 - 135.12, turns (1/21); drawn parked (doors shut);
// after 80 frames sound 0xA0135C, stopped (0x543390), placed at its end point, kind181_car_parked; skipped opening:
// kind181_car_skipped
template <class P> void Game<P>::kind181_car_stop(uint32_t obj)
{
    const float x = of<float>(obj, 0x20) * ffrom(0x3F73CF3D) + ffrom(0xBF430C31);
    const float z = of<float>(obj, 0x28) * ffrom(0x3F666666) + ffrom(0xC3071E28);
    of<float>(obj, 0x28) = z;
    of<float>(obj, 0x20) = x;
    car_turn(obj, 21);
    car_parked_draw(obj, false);
    player_char_props_draw();
    cam_copro(0x06, {});
    const int32_t t = of<int32_t>(obj, 0x50) - 1;
    of<int32_t>(obj, 0x50) = t;
    if (t == 0) {
        sound_cue(0xA0135C);
        mem.set32(pk::car_done, 1);
        of<uint32_t>(obj, 0x28) = 0xC4A8E5B2;
        of<uint32_t>(obj, 0x20) = 0xC1800000;
        of<uint32_t>(obj, 0x30) = 0x8000;
        of<uint32_t>(obj, 0x2C) = 0;
        of<uint32_t>(obj, 0) = F::kind181_car_parked;
    }
    if (mem.u32(pk::opening_skip) != 0)
        of<uint32_t>(obj, 0) = F::kind181_car_skipped;
}

// 0x4EAF0 kind181_car_parked: gone at camera frame 450; the door opens (+0x2C by 0x200 to 0x3000); drawn; skipped
// opening: kind181_car_skipped
template <class P> void Game<P>::kind181_car_parked(uint32_t obj)
{
    if (mem.f32(pk::camera_frame) == 450.0f) {
        task_exit();
        return;
    }
    int32_t d = of<int32_t>(obj, 0x2C);
    if (!(d > 0x2FFF))
        d += 0x200;
    of<int32_t>(obj, 0x2C) = d;
    car_parked_draw(obj, true);
    cam_copro(0x06, {});
    if (mem.u32(pk::opening_skip) != 0)
        of<uint32_t>(obj, 0) = F::kind181_car_skipped;
}

// 0x4EC10 kind181_car_skipped: gone at camera frame 450; parked at its end point with the door open; drawn
template <class P> void Game<P>::kind181_car_skipped(uint32_t obj)
{
    if (mem.f32(pk::camera_frame) == 450.0f) {
        task_exit();
        return;
    }
    of<uint32_t>(obj, 0x20) = 0xC1800000;
    of<uint32_t>(obj, 0x28) = 0xC4A8E5B2;
    of<uint32_t>(obj, 0x2C) = 0x3000;
    of<uint32_t>(obj, 0x30) = 0x8000;
    car_parked_draw(obj, true);
    cam_copro(0x06, {});
}

// 0x4ED20 player_char_props_draw: the characters in the car (models from table 0xC10E0 by the title code): both
// players: both seats; else player 1's seat, or player 2's when only player 2 is in. PC Mode (THOTD.EXE 0x496F2C):
// the seat's character's model (+0x14: 0x188 Rogan, 0x189 G, 0x1D38.. the others), the arcade's when the runtime has
// no such model (the arcade files). carchars (ours): the runtime puts a seat's own character there when it has no car
// model of its own in the model set (Runtime::car_seats_dress; draws only)
template <class P> void Game<P>::player_char_props_draw()
{
    const uint32_t code = mem.u32(pk::title_code);
    auto model = [&](int seat, uint32_t arcade) {
        if constexpr (!P::pc) {
            car_seat_mark(seat);
            if (chars_on()) {
                const uint32_t m = uint32_t(pm_table(pm_seat_char(seat)).model);
                if (pm_model_drawable(m))
                    return m;
            }
        } else {
            (void)seat;
        }
        return arcade;
    };
    if (mem.at<uint16_t>(pk::players_in) == 2) {
        cam_copro(0x05, {});
        cam_copro(0x12, {0xC090EF35, 0x40B08E8A, 0xC079999A});
        geo_draw_model(model(0, mem.u32(0xC10E0 + 8 * code)));
        cam_copro(0x06, {});
        cam_copro(0x05, {});
        cam_copro(0x12, {0x4097BF48, 0x40B08E8A, 0xC0833333});
        geo_draw_model(model(1, mem.u32(0xC10E4 + 8 * mem.u32(pk::title_code))));
    } else if (mem.at<uint8_t>(pk::player2_only) == 0) {
        cam_copro(0x05, {});
        cam_copro(0x12, {0xC090EF35, 0x40B08E8A, 0xC079999A});
        geo_draw_model(model(0, mem.u32(0xC10E0 + 8 * code)));
    } else {
        cam_copro(0x05, {});
        cam_copro(0x12, {0xC090EF35, 0x40B08E8A, 0xC0833333});
        geo_draw_model(model(1, mem.u32(0xC10E4 + 8 * code)));
    }
    cam_copro(0x06, {});
}

// ---------------------------------------------------------------------------------------------------------------
// kind 175, the rat

// 0x4960 kind175_rat (kind 175): shootable (flags 0x401), model 0xB6D, radius 1.5; with +0x70 it stops to sniff after
// 30 frames (else +0x50 = -1); kind175_rat_run
// THOTD.EXE's kind 175 (table 0x5D8118 + 175 x 4 = 0x4328CC) is only `jmp task_exit`: the PC has no rats, so in PC
// Mode and Boss Mode none runs (pmrest2: the PC capture's Chapter 1 rats end in their first tick)
template <class P> void Game<P>::kind175_rat(uint32_t obj)
{
    if (pc_path()) {
        task_exit();
        return;
    }
    of<uint32_t>(obj, 0x58) = 0x401;
    of<uint32_t>(obj, 0x54) = 0xB6D;
    of<uint32_t>(obj, 0x90) = 0x3FC00000;
    of<int32_t>(obj, 0x50) = of<uint16_t>(obj, 0x70) == 0 ? -1 : 30;
    of<uint32_t>(obj, 0x2EC) = 0;
    of<uint32_t>(obj, 0) = F::kind175_rat_run;
}

// the rat shot (inline in run / sniff): model 0xB6C thrown up (gravity -0.02725, 60 frames, kind175_rat_shot), no
// longer shootable, a spurt, sound 0xA01349
template <class P> void Game<P>::rat_check_shot(uint32_t obj)
{
    const uint8_t f = of<uint8_t>(obj, 0x58);
    if (!(f & 1) || !(f & 6))
        return;
    of<uint32_t>(obj, 0x54) = 0xB6C;
    of<uint32_t>(obj, 0x88) = 0xBCDF0123;
    of<uint32_t>(obj, 0x50) = 60;
    of<uint32_t>(obj, 0x7C) = 0;
    of<uint32_t>(obj, 0) = F::kind175_rat_shot;
    of<uint32_t>(obj, 0x58) &= ~1u;
    prop_spurt_spawn(obj);
    sound_cue(0xA01349);
}

// 0x49B0 kind175_rat_run: shot check; the sniff countdown (+0x50, to kind175_rat_sniff at 1); gone after 121 frames;
// runs 0.4 a frame (-x when its y angle is 0x4000, else -z) cycling models 0xB6D..0xB79; drawn; shootable
template <class P> void Game<P>::kind175_rat_run(uint32_t obj)
{
    rat_check_shot(obj);
    if (of<uint8_t>(obj, 0x58) & 1) {
        int32_t t = of<int32_t>(obj, 0x50);
        if (t > 0) {
            if (t == 1) {
                of<uint32_t>(obj, 0x54) = 0xBFA;
                of<uint32_t>(obj, 0) = F::kind175_rat_sniff;
            }
            t -= 1;
        }
        const int32_t n = of<int32_t>(obj, 0x2EC);
        of<int32_t>(obj, 0x50) = t;
        of<int32_t>(obj, 0x2EC) = n + 1;
        if (n > 120) {
            task_exit();
            return;
        }
        if (of<uint32_t>(obj, 0) != F::kind175_rat_sniff) {
            int32_t m = of<int32_t>(obj, 0x54) + 1;
            if (m > 0xB79)
                m = 0xB6D;
            of<int32_t>(obj, 0x54) = m;
            if (of<uint32_t>(obj, 0x30) == 0x4000)
                of<float>(obj, 0x20) = of<float>(obj, 0x20) - ffrom(0x3ECCCCCD);
            else
                of<float>(obj, 0x28) = of<float>(obj, 0x28) - ffrom(0x3ECCCCCD);
        }
    }
    kind175_rat_draw(obj);
    of<uint32_t>(obj, 0x58) &= 0xFFFFFFF9u;
    if (of<uint8_t>(obj, 0x58) & 1)
        depth_list_add(obj);
}

// 0x4AF0 kind175_rat_sniff: shot check; models 0xBFA..0xC0E, holding 60 frames at 0xC05; then running again
// (kind175_rat_run, no more sniffing); drawn; shootable
template <class P> void Game<P>::kind175_rat_sniff(uint32_t obj)
{
    rat_check_shot(obj);
    if (of<uint8_t>(obj, 0x58) & 1) {
        uint32_t m = of<uint32_t>(obj, 0x54);
        if (m == 0xC04) {
            of<uint32_t>(obj, 0x50) = 60;
            m = 0xC05;
        }
        const int32_t t = of<int32_t>(obj, 0x50);
        of<uint32_t>(obj, 0x54) = m;
        if (t != 0) {
            of<int32_t>(obj, 0x50) = t - 1;
        } else {
            const int32_t n = of<int32_t>(obj, 0x54) + 1;
            of<int32_t>(obj, 0x54) = n;
            if (n > 0xC0E) {
                of<uint32_t>(obj, 0x54) = 0xB6D;
                of<int32_t>(obj, 0x50) = -1;
                of<uint32_t>(obj, 0) = F::kind175_rat_run;
            }
        }
    }
    kind175_rat_draw(obj);
    of<uint32_t>(obj, 0x58) &= 0xFFFFFFF9u;
    if (of<uint8_t>(obj, 0x58) & 1)
        depth_list_add(obj);
}

// 0x4BF0 kind175_rat_shot: above y 0 it flies (+0.2 x, gravity) and spins; below it blinks out over +0x50 frames
template <class P> void Game<P>::kind175_rat_shot(uint32_t obj)
{
    const float y = of<float>(obj, 0x24);
    if (y < 0.0f) {
        const int32_t t = of<int32_t>(obj, 0x50) - 1;
        of<int32_t>(obj, 0x50) = t;
        if (!(t > 0)) {
            task_exit();
            return;
        }
        if (of<int32_t>(obj, 0x50) % 2 != 0)
            kind175_rat_draw(obj);
        return;
    }
    const float vy = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
    of<uint32_t>(obj, 0x34) += 0x200;
    of<float>(obj, 0x7C) = vy;
    of<float>(obj, 0x20) = of<float>(obj, 0x20) + ffrom(0x3E4CCCCD);
    of<float>(obj, 0x24) = y + vy;
    kind175_rat_draw(obj);
}

// 0x4C90 kind175_rat_draw: T Ry Rz, the model; its camera-space point one higher (view_pos)
template <class P> void Game<P>::kind175_rat_draw(uint32_t obj)
{
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    cam_copro(0x16, {of<uint32_t>(obj, 0x34)});
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
    prop_view_pos(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28));
    of<float>(obj, 0x3C) = of<float>(obj, 0x3C) + 1.0f;
}

// ---------------------------------------------------------------------------------------------------------------
// kind 38, the barrel

// 0x4360 kind38_barrel (kind 38): +0x37C cleared, the record (+0x35C) kept in +0x368, the kit's model; the ballistic
// arc to the record's point (velocity_for_arc: gravity record +0x10, frames +0x24); mode (record +0x26 & 3) 0: the
// tumble block (0x48 bytes: the kit's +0x14..+0x2B) and kind38_barrel_fall; 1: kind38_barrel_break
template <class P> void Game<P>::kind38_barrel(uint32_t obj)
{
    obj_clear_37c(obj);
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    of<uint32_t>(obj, 0x368) = rec;
    const uint32_t kit = pk::barrel_kits + uint32_t(int32_t(mem.at<int16_t>(rec)) * 0x34);
    of<int16_t>(obj, 0x60) = 0;
    of<uint32_t>(obj, 0x54) = mem.u32(kit + 0x30);
    const float frames = float(int32_t(mem.at<int16_t>(rec + 0x24)));
    velocity_for_arc(obj, obj + 0x20, rec + 4, mem.u32(rec + 0x10), fbits(frames));
    const uint32_t mode = mem.at<uint16_t>(rec + 0x26) & 3;
    if (mode == 0) {
        const uint32_t b = mem_alloc(0x48);
        of<uint32_t>(obj, 0x31C) = 0;
        of<uint32_t>(obj, 0x37C) = b;
        const uint32_t k = pk::barrel_kits + uint32_t(int32_t(mem.at<int16_t>(rec)) * 0x34);
        mem.set32(b + 0x34, 0);
        for (uint32_t i = 0; i < 6; ++i)
            mem.set32(b + 4 * i, mem.u32(k + 0x14 + 4 * i));
        of<uint32_t>(obj, 0) = F::kind38_barrel_fall;
        kind38_barrel_fall(obj);
    } else if (mode == 1) {
        of<uint32_t>(obj, 0) = F::kind38_barrel_break;
        kind38_barrel_break(obj);
    }
}

// the barrel's spin by the record's +0x18 / +0x1C / +0x20 (inline)
template <class P> void Game<P>::barrel_spin(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x368);
    of<uint32_t>(obj, 0x2C) += mem.u32(rec + 0x18);
    of<uint32_t>(obj, 0x30) += mem.u32(rec + 0x1C);
    of<uint32_t>(obj, 0x34) += mem.u32(rec + 0x20);
}

// 0x4430 kind38_barrel_fall: spins; at the kit's floor height it bounces (vy x -restitution, record +0x14), after the
// second bounce kind38_barrel_rise_start; drawn
template <class P> void Game<P>::kind38_barrel_fall(uint32_t obj)
{
    barrel_spin(obj);
    const uint32_t rec = of<uint32_t>(obj, 0x368);
    const float floor =
        mem.f32(G::floor_y) + mem.f32(pk::barrel_kits + uint32_t(int32_t(mem.at<int16_t>(rec)) * 0x34) + 4);
    if (floor < of<float>(obj, 0x24)) {
        kind38_barrel_draw(obj);
        return;
    }
    of<float>(obj, 0x24) = floor;
    const float vy = -mem.f32(rec + 0x14) * of<float>(obj, 0x7C);
    const int32_t n = of<int32_t>(obj, 0x31C) + 1;
    of<int32_t>(obj, 0x31C) = n;
    of<float>(obj, 0x7C) = vy;
    if (n <= 1) {
        kind38_barrel_draw(obj);
        return;
    }
    kind38_barrel_rise_start(obj);
}

// 0x44D0 kind38_barrel_rise_start: kind38_barrel_rise
template <class P> void Game<P>::kind38_barrel_rise_start(uint32_t obj)
{
    of<uint32_t>(obj, 0) = F::kind38_barrel_rise;
    kind38_barrel_rise(obj);
}

// 0x44F0 kind38_barrel_rise: spins while rising, then kind38_barrel_tumble_start; drawn while rising
template <class P> void Game<P>::kind38_barrel_rise(uint32_t obj)
{
    barrel_spin(obj);
    if (of<float>(obj, 0x7C) > 0.0f) {
        kind38_barrel_draw(obj);
        return;
    }
    kind38_barrel_tumble_start(obj);
}

// 0x4540 kind38_barrel_tumble_start: kind38_barrel_tumble
template <class P> void Game<P>::kind38_barrel_tumble_start(uint32_t obj)
{
    of<uint32_t>(obj, 0) = F::kind38_barrel_tumble;
    kind38_barrel_tumble(obj);
}

// the tumble step (inline): identity pushed (0x21), T(pos) R(z, y, x), the box corners (tumble_corners), popped;
// tumble_update
template <class P> void Game<P>::barrel_tumble_step(uint32_t obj)
{
    if (pm_boss()) {
        // PC Mode (docs/pcmode.md "kind 38"): THOTD.EXE 0x432500 / 0x432648 on PM's matrix stack: push, the file's
        // identity 0x42FF74, translate 0x42F704 (pos), rotate z 0x42FA2C (+0x34), y 0x42F924 (+0x30), x 0x42FBB8
        // (+0x2C) (the same maths as the kit file's copies: hotd_proof pmmaths), tumble_corners 0x4C5924, pop, the
        // PC's tumble_update 0x4C5B1C. The flow around it is the arcade's (0x4323A4 / 0x4324A0 / 0x432500 / 0x432648
        // / 0x4326D4 = fall / rise / tumble / settle / rest; 0x432448 and 0x4324F8 have no caller)
        const uint32_t b = of<uint32_t>(obj, 0x37C);
        pc_matrix_push();
        pc_matrix_identity8();
        pc_matrix_translate8(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28));
        pc_matrix_rotate_z8(of<uint32_t>(obj, 0x34));
        pc_matrix_rotate_y7(of<uint32_t>(obj, 0x30));
        pc_matrix_rotate_x8(of<uint32_t>(obj, 0x2C));
        pm_tumble_corners(b);
        pcm_pop();
        pm_tumble_update(obj, b);
        return;
    }
    cam_copro(0x21, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    const uint32_t b = of<uint32_t>(obj, 0x37C);
    cam_copro(0x86, {of<uint32_t>(obj, 0x34), of<uint32_t>(obj, 0x30), of<uint32_t>(obj, 0x2C)});
    tumble_corners(b);
    cam_copro(0x06, {});
    tumble_update(obj, b);
}

// 0x4560 kind38_barrel_tumble: spins and tumbles; landed (block +0x34) -> kind38_barrel_settle_start; drawn
template <class P> void Game<P>::kind38_barrel_tumble(uint32_t obj)
{
    barrel_spin(obj);
    barrel_tumble_step(obj);
    if (mem.u32(of<uint32_t>(obj, 0x37C) + 0x34) != 0) {
        kind38_barrel_settle_start(obj);
        return;
    }
    kind38_barrel_draw(obj);
}

// 0x4620 kind38_barrel_settle_start: velocity and acceleration cleared; kind38_barrel_settle
template <class P> void Game<P>::kind38_barrel_settle_start(uint32_t obj)
{
    for (uint32_t o : {0x80u, 0x7Cu, 0x78u, 0x8Cu, 0x88u, 0x84u})
        of<uint32_t>(obj, o) = 0;
    of<uint32_t>(obj, 0) = F::kind38_barrel_settle;
    kind38_barrel_settle(obj);
}

// 0x4650 kind38_barrel_settle: tumbles until the block's state is 3 (at rest) -> kind38_barrel_rest_start; drawn
template <class P> void Game<P>::kind38_barrel_settle(uint32_t obj)
{
    barrel_tumble_step(obj);
    if (mem.u32(of<uint32_t>(obj, 0x37C) + 0x34) == 3) {
        kind38_barrel_rest_start(obj);
        return;
    }
    kind38_barrel_draw(obj);
}

// 0x46E0 kind38_barrel_rest_start: kind38_barrel_rest
template <class P> void Game<P>::kind38_barrel_rest_start(uint32_t obj)
{
    of<uint32_t>(obj, 0) = F::kind38_barrel_rest;
    kind38_barrel_rest(obj);
}

// 0x4700 kind38_barrel_rest: out of view (coprocessor 0x7B with the model's radius < 0): extras freed, gone; else drawn
template <class P> void Game<P>::kind38_barrel_rest(uint32_t obj)
{
    const uint32_t hdr = mem.u32(pk::model_table + 4 * of<uint32_t>(obj, 0x54));
    float vis;
    if (pm_boss()) {   // PC Mode: THOTD.EXE 0x4326D4's view test 0x430BD8 (pc_view_test's copy) with the model's radius
        vis = pm_view_test(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28), mem.f32(hdr + 0x18));
    } else {
        const Tgp::Out o = cam_copro(
            0x7B, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28), mem.u32(hdr + 0x18)});
        vis = ffrom(o.w[0]);
    }
    if (vis < 0.0f) {
        enemy_free_extras(obj);
        task_exit();
        return;
    }
    kind38_barrel_draw(obj);
}

// 0x4770 kind38_barrel_draw: moves by its velocity (obj_move_velocity); T(pos) R(z, y, x), minus the kit's model
// offset, the model
template <class P> void Game<P>::kind38_barrel_draw(uint32_t obj)
{
    const uint32_t kit =
        pk::barrel_kits + uint32_t(int32_t(mem.at<int16_t>(of<uint32_t>(obj, 0x368))) * 0x34);
    obj_move_velocity(obj);
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x86, {of<uint32_t>(obj, 0x34), of<uint32_t>(obj, 0x30), of<uint32_t>(obj, 0x2C)});
    cam_copro(0x12, {mem.u32(kit + 8) ^ 0x80000000u, mem.u32(kit + 0xC) ^ 0x80000000u,
                     mem.u32(kit + 0x10) ^ 0x80000000u});
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

// 0x4810 kind38_barrel_break (mode 1): moves (the routine at 0xC681C) for the record's +0x24 frames, then sound
// 0xA0122B and the break models 0x6D0..0x714, then event flag (record +0x28) set, extras freed, gone; drawn (T R,
// blinking with +0x3C4 bit 2)
template <class P> void Game<P>::kind38_barrel_break(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x368);
    int32_t s = of<int16_t>(obj, 0x60);
    if (s == 0) {
        of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
        of<int32_t>(obj, 0x2EC) = mem.at<int16_t>(rec + 0x24);
        s = 1;
    }
    if (s == 1) {
        const uint32_t r = mem.u32(pk::move_routine);
        if (r == F::obj_move_velocity)
            obj_move_velocity(obj);
        else
            call_routine(r, obj);
        if (pc_path())   // PC Mode / Boss Mode: THOTD.EXE 0x432804: the height = floor_y + the frames left (pmbody)
            of<float>(obj, 0x24) = float(of<int32_t>(obj, 0x2EC)) + mem.f32(G::floor_y);
        const int32_t n = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = n;
        if (n == 0) {
            sound_cue(0xA0122B);
            of<uint32_t>(obj, 0x54) = 0x6CF;
            of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
            s = 2;
        }
    }
    if (s == 2) {
        const int32_t m = of<int32_t>(obj, 0x54) + 1;
        of<int32_t>(obj, 0x54) = m;
        if (m > 0x714) {
            mem.at<uint8_t>(G::event_flags + uint32_t(int32_t(mem.at<int16_t>(rec + 0x28)))) = 1;
            of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
            s = 3;
        }
    }
    if (s == 3) {
        enemy_free_extras(obj);
        task_exit();
        return;
    }
    if (((of<uint8_t>(obj, 0x3C4) >> 2) & 1) && mem.at<int32_t>(G::blink_phase) % 2 == 0)
        return;
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x86, {of<uint32_t>(obj, 0x34), of<uint32_t>(obj, 0x30), of<uint32_t>(obj, 0x2C)});
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

// ---------------------------------------------------------------------------------------------------------------
// kinds 76 / 77, a hostage's cell (Chapter 1 events 6 / 14)

// 0x70490 kind77_lock (kind 77): the lock (model 0x314, one hit point, radius 2.8); shootable from flag 0x51E979 (in
// event 14 only then); a shot: sparks, sound 0xA0124A, a hit point off; at 0 flag 0x51E952 (the hostage is freed) and
// gone; flag 0x51E956: no longer shootable; gone with flag 0x51E954; drawn (T Ry S, its point by geo_matrix_position)
// and in the depth list
template <class P> void Game<P>::kind77_lock(uint32_t obj)
{
    const uint32_t s = uint32_t(int32_t(of<int16_t>(obj, 0x60)));
    if (s <= 4) {
        uint32_t st = s;
        if (st == 0) {
            of<int16_t>(obj, 0x70) = 1;
            of<uint32_t>(obj, 0x54) = 0x314;
            of<uint32_t>(obj, 0x94) = 0x3F800000;
            of<uint32_t>(obj, 0x90) = 0x40333333;
            of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
            st = 1;
        }
        bool draw = false;
        if (st == 1) {
            if (mem.at<uint8_t>(G::event_flags + 0x39) == 0 && mem.at<uint8_t>(G::event_index) == 14) {
                draw = true;
            } else {
                of<uint32_t>(obj, 0x58) = 1;
                of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
                st = 2;
            }
        }
        if (!draw && st == 2) {
            if (mem.at<uint8_t>(G::event_flags + 0x16) == 1) {
                of<int16_t>(obj, 0x60) = 4;
                of<uint32_t>(obj, 0x58) &= ~1u;
                draw = true;
            } else if (!((of<uint16_t>(obj, 0x58) >> 8) & 1)) {
                draw = true;
            } else {
                kind77_lock_spark_spawn(obj);
                sound_cue(0xA0124A);
                const uint16_t hp = uint16_t(of<uint16_t>(obj, 0x70) - 1);
                of<uint16_t>(obj, 0x70) = hp;
                if (int32_t(uint32_t(hp) << 16) > 0) {
                    of<uint32_t>(obj, 0x58) &= ~0x100u;
                    draw = true;
                } else {
                    of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
                    st = 3;
                }
            }
        }
        if (!draw && st == 3) {
            mem.at<uint8_t>(G::event_flags + 0x12) = 1;
            task_exit();
            return;
        }
        if (!draw && st == 4 && mem.at<uint8_t>(G::event_flags + 0x14) == 1) {
            task_exit();
            return;
        }
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    prop_scale(of<uint32_t>(obj, 0x94));
    geo_matrix_position(of<uint32_t>(obj, 0x54), obj + 0x38);
    cam_copro(0x06, {});
    depth_list_add(obj);
}

// 0x707A0 kind77_lock_spark_spawn: kind77_lock_spark at the lock (a 0x58-byte task, model 55)
template <class P> void Game<P>::kind77_lock_spark_spawn(uint32_t obj)
{
    const uint32_t t = task_create(F::kind77_lock_spark, 0x58);
    of<uint32_t>(t, 0x54) = 55;
    of<uint32_t>(t, 0x20) = of<uint32_t>(obj, 0x20);
    of<uint32_t>(t, 0x24) = of<uint32_t>(obj, 0x24);
    of<uint32_t>(t, 0x28) = of<uint32_t>(obj, 0x28);
}

// 0x70A30 kind77_lock_spark: the next model until 0x4A, drawn at scale 5
template <class P> void Game<P>::kind77_lock_spark(uint32_t obj)
{
    const uint32_t m = of<uint32_t>(obj, 0x54) + 1;
    of<uint32_t>(obj, 0x54) = m;
    if (m == 0x4A) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    prop_scale(0x40A00000);
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

// 0x70630 kind076_slider (kind 76): the cell door (model 0xCCD); once flag 0x51E952 is set (the lock is shot) it slides
// 0.5 a frame along x (sound 0xA01217) to 100 (flag 0x51E955 from 90); flag 0x51E956: stays; gone with flag
// 0x51E954; drawn (T Ry S)
template <class P> void Game<P>::kind076_slider(uint32_t obj)
{
    int32_t s = of<int16_t>(obj, 0x60);
    if (s == 0) {
        of<uint32_t>(obj, 0x54) = 0xCCD;
        of<uint32_t>(obj, 0x94) = 0x3F800000;
        of<uint32_t>(obj, 0x90) = 0x3F800000;
        of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
        s = 1;
    }
    if (s == 1) {
        if (mem.at<uint8_t>(G::event_flags + 0x16) == 1) {
            of<int16_t>(obj, 0x60) = 3;
            s = -1;
        } else if (mem.at<uint8_t>(G::event_flags + 0x12) == 1) {
            sound_cue(0xA01217);
            of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
            s = 2;
        }
    }
    if (s == 2) {
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + 0.5f;
        if (!(of<float>(obj, 0x20) < 90.0f)) {
            mem.at<uint8_t>(G::event_flags + 0x15) = 1;
            if (!(of<float>(obj, 0x20) < 100.0f)) {
                sound_cue(0xA01217);
                of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
                s = 3;
            }
        }
    }
    if (s == 3 && mem.at<uint8_t>(G::event_flags + 0x14) == 1) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    prop_scale(of<uint32_t>(obj, 0x94));
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

// ---------------------------------------------------------------------------------------------------------------
// kind 140

// 0x61DD0 kind140_dropped_weapon (kind 140): a shootable object (model 0x2C8, scale 0.5, radius 3.5, gravity -0.02725,
// 60 frames on the floor); kind140_dropped_weapon_frame
template <class P> void Game<P>::kind140_dropped_weapon(uint32_t obj)
{
    of<uint32_t>(obj, 0x58) = 0x401;
    of<uint32_t>(obj, 0x54) = 0x2C8;
    of<uint32_t>(obj, 0x94) = 0x3F000000;
    of<uint32_t>(obj, 0x90) = 0x40600000;
    of<uint32_t>(obj, 0x88) = 0xBCDF0123;
    of<uint32_t>(obj, 0x50) = 60;
    of<uint32_t>(obj, 0x7C) = 0;
    of<uint32_t>(obj, 0x80) = 0;
    of<uint32_t>(obj, 0) = F::kind140_dropped_weapon_frame;
}

// 0x61E30 kind140_dropped_weapon_frame: shot: sound 0xA01259, knocked away (0.1, 0.3, 0.5); above the floor it flies
// and spins (x, y by 0x800); on it lies flat (x angle 0x8000) and counts down +0x50 to its end; drawn
// (obj_model_draw_colour), its camera point, hit bits off, the depth list
template <class P> void Game<P>::kind140_dropped_weapon_frame(uint32_t obj)
{
    const uint8_t f = of<uint8_t>(obj, 0x58);
    if ((f & 1) && (f & 6)) {
        sound_cue(0xA01259);
        of<uint32_t>(obj, 0x78) = 0x3DCCCCCD;
        of<uint32_t>(obj, 0x7C) = 0x3E99999A;
        of<uint32_t>(obj, 0x80) = 0x3F000000;
    }
    const float y = of<float>(obj, 0x24);
    if (y > mem.f32(G::floor_y)) {
        const float vy = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
        of<uint32_t>(obj, 0x2C) += 0x800;
        of<uint32_t>(obj, 0x30) += 0x800;
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + of<float>(obj, 0x78);
        of<float>(obj, 0x7C) = vy;
        of<float>(obj, 0x28) = of<float>(obj, 0x28) + of<float>(obj, 0x80);
        of<float>(obj, 0x24) = y + vy;
    } else {
        of<uint32_t>(obj, 0x30) = 0;
        of<uint32_t>(obj, 0x24) = mem.u32(G::floor_y);
        of<uint32_t>(obj, 0x34) = 0;
        of<uint32_t>(obj, 0x2C) = 0x8000;
        const int32_t t = of<int32_t>(obj, 0x50) - 1;
        of<int32_t>(obj, 0x50) = t;
        if (!(t > 0)) {
            task_exit();
            return;
        }
    }
    obj_model_draw_colour(obj);
    prop_view_pos(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28));
    of<uint32_t>(obj, 0x58) &= 0xFFFFFFF9u;
    depth_list_add(obj);
}

// ---------------------------------------------------------------------------------------------------------------
// kind 135, the ebitb creature dropping from the ceiling (Chapter 1 events 6 / 14)

// 0x606D0 kind135_ebitb_drop (kind 135): body with the hanging motion (action 2: 0xA7; Chapter 2 events 13 / 14
// (+0x2EC = 1): 0x95; else 0x9E, kept in +0x2F0), type set-up; delay +0x50 20 (action 1: 100, Chapter 2 event 2: 40;
// action 3: 160), vy -0.7; counted as an enemy and an attack position taken (action 2, the decoy: vy -1, neither)
template <class P> void Game<P>::kind135_ebitb_drop(uint32_t obj)
{
    mem.at<int32_t>(G::hit_type) = of<int16_t>(obj, 0xCC);
    if (mem.at<uint8_t>(G::chapter) == 1 &&
        (mem.at<uint8_t>(G::event_index) == 13 || mem.at<uint8_t>(G::event_index) == 14))
        of<uint32_t>(obj, 0x2EC) = 1;
    else
        of<uint32_t>(obj, 0x2EC) = 0;
    uint32_t m;
    if (of<uint16_t>(obj, 0x64) == 2)
        m = 0xA7;
    else if (of<uint32_t>(obj, 0x2EC) != 0)
        m = 0x95;
    else
        m = 0x9E;
    body_init(obj, mem.at<int32_t>(G::hit_type), m);
    of<uint32_t>(obj, 0x2F0) = m;
    of<uint8_t>(obj, 0x3C6) |= 1;
    creature_type_setup(obj);
    of<uint8_t>(obj, 0x2E8) = 0xFF;
    const uint16_t action = of<uint16_t>(obj, 0x64);
    of<uint32_t>(obj, 0x7C) = 0xBF333333;
    of<uint32_t>(obj, 0x50) = 20;
    if (action == 1) {
        of<uint32_t>(obj, 0x50) = 100;
        if (mem.at<uint8_t>(G::chapter) == 1 && mem.at<uint8_t>(G::event_index) == 2)
            of<uint32_t>(obj, 0x50) = 40;
    }
    if (action == 3)
        of<uint32_t>(obj, 0x50) = 160;
    of<uint8_t>(obj, 0x2E7) = 0xFF;
    if (action != 2) {
        mem.at<uint16_t>(G::enemy_count) += 1;
        mem.at<uint16_t>(G::enemies_alive) += 1;
        attack_slot_take_wide(obj);
    } else {
        of<uint32_t>(obj, 0x7C) = 0xBF800000;
    }
    of<uint32_t>(obj, 0) = F::kind135_ebitb_frame;
}

// one enemy fewer (inline): flags bit 20 while more than one is alive, then the counts down
template <class P> void Game<P>::ebitb_uncount(uint32_t obj)
{
    if (mem.at<int16_t>(G::enemies_alive) > 1)
        of<uint32_t>(obj, 0x58) |= 0x100000;
    mem.at<uint16_t>(G::enemies_alive) -= 1;
    mem.at<uint16_t>(G::enemy_count) -= 1;
}

// 0x60820 kind135_ebitb_frame: part hits; a hit plays the hit motion (Chapter 2 table 0xC5560, else the type's
// table 0x14B900 by class / 9 and % 9) and goes back to +0x2F0 at its end; shot dead (not the decoy): 80 points, its
// attack position freed, falls (kind135_ebitb_dead_fall; Chapter 2 kind135_ebitb_dead_glide), uncounted, its voice;
// after the delay it falls (gravity 0.001, turning; Chapter 2 drifting toward the camera, else along its heading by
// 1/15); near the floor the landing motion (0x96 / 0xA3); at head height (11 over the floor, Chapter 2 six more,
// Chapter 2 event 2 action 1 nine more) it hits its player once (sound 0xA01254, player_hit) and frees its position;
// at the floor it dies; the decoy (action 2) lands with sound 0xA01223 and bounces (kind135_ebitb_decoy_land);
// drawn with its shadow and drips, in the depth list
template <class P> void Game<P>::kind135_ebitb_frame(uint32_t obj)
{
    mem.at<uint8_t>(G::hit_class) = 0;
    mem.at<uint16_t>(G::hit_damage) = 0;
    mem.at<int32_t>(G::hit_type) = of<int16_t>(obj, 0xCC);
    enemy_part_hits(obj);
    if (mem.at<uint8_t>(G::hit_class) != 0 && of<uint16_t>(obj, 0x64) != 2) {
        uint32_t m;
        of<int32_t>(obj, 0x2F0) = of<int16_t>(obj, 0xD0);
        const int32_t c = mem.at<int8_t>(G::hit_class);
        if (of<uint32_t>(obj, 0x2EC) != 0) {
            m = mem.u32(0xC5560 + 4 * uint32_t(c));
        } else {
            const uint32_t rem = uint32_t(c % 9), quo = uint32_t(c / 9);
            const uint32_t t = mem.u32(G::hit_motions_by_type + 4 * uint32_t(mem.at<int32_t>(G::hit_type)));
            m = mem.u32(t + ((quo & 0xFF) << 2) + ((rem & 0xFF) << 3));
        }
        set_motion_blend(obj, m, 4, 4);
    }
    if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) && of<int16_t>(obj, 0xD0) != of<int32_t>(obj, 0x2F0))
        set_motion_blend(obj, of<uint32_t>(obj, 0x2F0), 4, 4);
    if (of<uint16_t>(obj, 0x64) != 2) {
        const uint16_t hp = uint16_t(of<uint16_t>(obj, 0x70) - mem.at<uint16_t>(G::hit_damage));
        of<uint16_t>(obj, 0x70) = hp;
        if (!(int32_t(uint32_t(hp) << 16) > 0) || return_false() != 0) {
            add_score(mem.at<int8_t>(obj + 0x2EB), 0x50);
            if (of<uint8_t>(obj, 0x2E7) != 0xFF)
                enemy_attack_slot_free(obj);
            of<uint16_t>(obj, 0x70) = 0;
            of<uint32_t>(obj, 0x7C) = 0x3F000000;
            of<uint32_t>(obj, 0x50) = 0;
            of<uint32_t>(obj, 0x31C) = 0x200;
            of<uint32_t>(obj, 0x58) |= 0x10000000;
            if (of<uint32_t>(obj, 0x2EC) != 0) {
                if (of<uint16_t>(obj, 0x64) == 3)
                    of<uint32_t>(obj, 0x78) = 0xBE19999A;
                else
                    of<float>(obj, 0x80) = float(int32_t(of<int8_t>(obj, 0x2E8))) * ffrom(0x3E99999A) +
                                           ffrom(0xBE19999A);
                of<uint32_t>(obj, 0) = F::kind135_ebitb_dead_glide;
            } else {
                of<uint32_t>(obj, 0) = F::kind135_ebitb_dead_fall;
            }
            ebitb_uncount(obj);
            enemy_voice(obj, 0);
        }
    }
    if ((of<uint32_t>(obj, 0x58) >> 28) & 1)
        return;
    const int32_t t = of<int32_t>(obj, 0x50) - 1;
    of<int32_t>(obj, 0x50) = t;
    if (!(t > 0)) {
        const float vy = of<float>(obj, 0x7C) - ffrom(0x3A83126F);
        of<float>(obj, 0x7C) = vy;
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
        if (of<uint16_t>(obj, 0x64) != 2 && of<uint32_t>(obj, 0x2EC) == 0)
            of<uint32_t>(obj, 0x2C) -= 0x100;
        if (of<uint32_t>(obj, 0x2EC) != 0 && of<uint16_t>(obj, 0x64) != 2) {
            const float x = of<float>(obj, 0x20);
            of<float>(obj, 0x20) = (mem.f32(G::camera_records) - x) / 80.0f + x;
            const float z = of<float>(obj, 0x28);
            of<float>(obj, 0x28) = (mem.f32(G::camera_records + 8) - z) / 80.0f + z;
        } else {
            const Tgp::Out s = cam_copro(0x1B, {of<uint32_t>(obj, 0x30)});
            of<float>(obj, 0x20) = ffrom(s.w[0]) / 15.0f + of<float>(obj, 0x20);
            const Tgp::Out c = cam_copro(0x1C, {of<uint32_t>(obj, 0x30)});
            of<float>(obj, 0x28) = ffrom(c.w[0]) / 15.0f + of<float>(obj, 0x28);
        }
    }
    auto ev2a1 = [&](uint16_t action) {
        return mem.at<uint8_t>(G::chapter) == 1 && mem.at<uint8_t>(G::event_index) == 2 && action == 1 ? 1 : 0;
    };
    uint16_t action = of<uint16_t>(obj, 0x64);
    if (action != 2) {
        const float h = of<float>(obj, 0x24) - mem.f32(G::floor_y);
        if (h < float(ev2a1(action)) * 10.0f + 40.0f) {
            const uint16_t mo = of<uint16_t>(obj, 0xD0);
            if (mo == 0x9E || mo == 0xA7 || mo == 0x95) {
                if (of<uint32_t>(obj, 0x2EC) != 0) {
                    of<uint32_t>(obj, 0x2F0) = 0x96;
                    set_motion_blend(obj, 0x96, 4, 4);
                } else {
                    of<uint32_t>(obj, 0x2F0) = 0xA3;
                    set_motion_blend(obj, 0xA3, 4, 8);
                }
            }
        }
    }
    action = of<uint16_t>(obj, 0x64);
    if (action != 2) {
        const float base = mem.f32(G::floor_y) + 11.0f;
        const float k = float(ev2a1(action)) * 9.0f;
        const float b2 = mem.at<uint8_t>(G::chapter) == 1 ? base + 6.0f : base + 0.0f;
        if (!(of<float>(obj, 0x24) > b2 + k) && !(of<int32_t>(obj, 0x58) < 0)) {
            of<uint32_t>(obj, 0x58) |= 0x80000000u;
            if (mem.at<uint16_t>(G::players_in_game_b) == 1) {
                of<uint8_t>(obj, 0x2E8) = mem.at<uint8_t>(0x51F700);
            } else {
                of<uint8_t>(obj, 0x2E8) = of<uint8_t>(obj, 0x64);
                if (of<uint16_t>(obj, 0x64) == 3)
                    of<uint8_t>(obj, 0x2E8) = 0;
            }
            sound_cue(0xA01254);
            player_hit(of<int8_t>(obj, 0x2E8), 1, 8);
            of<uint8_t>(obj, 0x2E8) = 0xFF;
            if (of<uint8_t>(obj, 0x2E7) != 0xFF)
                enemy_attack_slot_free(obj);
        }
    }
    if (of<uint16_t>(obj, 0x64) != 2) {
        const float floor = mem.f32(G::floor_y);
        if (!(of<float>(obj, 0x24) > floor + 1.0f)) {
            of<uint32_t>(obj, 0x50) = 0;
            of<float>(obj, 0x24) = floor - 10.0f;
            of<uint32_t>(obj, 0) =
                of<uint32_t>(obj, 0x2EC) == 0 ? F::kind135_ebitb_dead_fall : F::kind135_ebitb_dead_glide;
            ebitb_uncount(obj);
        }
    }
    if (of<uint16_t>(obj, 0x64) == 2 && !(of<float>(obj, 0x24) > mem.f32(G::floor_y))) {
        sound_cue(0xA01223);
        set_motion_blend(obj, 0x93, 45, 8);
        of<uint32_t>(obj, 0x7C) = 0x3F800000;
        of<uint32_t>(obj, 0x24) = mem.u32(G::floor_y);
        kind135_splash_spawn(obj);
        of<uint32_t>(obj, 0) = F::kind135_ebitb_decoy_land;
    }
    of<uint32_t>(obj, 0xF8) += 1;
    shadow_setup(mem.f32(G::floor_y));
    object_draw(obj);
    const uint32_t body = of<uint32_t>(obj, 0xC8);
    cam_copro(0x05, {});
    for (int32_t i = 0; i < mem.at<int32_t>(body + 0x34); ++i)
        body_drips_draw(mem.u32(mem.u32(body + 0x24) + 4 * uint32_t(i)));
    cam_copro(0x06, {});
    depth_list_add(obj);
}

// 0x60E40 kind135_ebitb_dead_fall: dead and above the floor: drifts back along its heading (1/8), falls (gravity
// 0.03), turns (+0x31C, to 0x3000; Chapter 2 action 1 below 44 slower); drawn with its shadow on odd frames; gone
// with its parts after 40 frames
template <class P> void Game<P>::kind135_ebitb_dead_fall(uint32_t obj)
{
    mem.at<int32_t>(G::hit_type) = of<int16_t>(obj, 0xCC);
    if (of<uint16_t>(obj, 0x70) == 0 && of<float>(obj, 0x24) > mem.f32(G::floor_y)) {
        of<float>(obj, 0x78) = of<float>(obj, 0x78) + ffrom(0x3C23D70A);
        const Tgp::Out s = cam_copro(0x1B, {of<uint32_t>(obj, 0x30)});
        of<float>(obj, 0x20) = of<float>(obj, 0x20) - ffrom(s.w[0]) / 8.0f;
        const Tgp::Out c = cam_copro(0x1C, {of<uint32_t>(obj, 0x30)});
        const float dz = ffrom(c.w[0]) / 8.0f;
        const float vy = of<float>(obj, 0x7C) + ffrom(0xBCF5C28F);
        of<float>(obj, 0x7C) = vy;
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
        of<float>(obj, 0x28) = of<float>(obj, 0x28) - dz;
    } else {
        of<uint32_t>(obj, 0x24) = 0;
    }
    const int32_t a = of<int32_t>(obj, 0x2C);
    if (a > 0x3000) {
        of<uint32_t>(obj, 0x2C) = 0x3000;
    } else {
        const int32_t n = a + of<int32_t>(obj, 0x31C);
        of<int32_t>(obj, 0x2C) = n;
        if (mem.at<uint8_t>(G::chapter) == 1 && of<uint16_t>(obj, 0x64) == 1 && !(of<float>(obj, 0x24) < 44.0f) &&
            n > 0x5FF && of<uint32_t>(obj, 0x31C) == 0x200)
            of<uint32_t>(obj, 0x31C) = 0xFFFFFE80u;
    }
    const int32_t t = of<int32_t>(obj, 0x50) + 1;
    of<int32_t>(obj, 0x50) = t;
    if (t % 2 != 0 && of<uint16_t>(obj, 0x70) == 0) {
        shadow_setup(mem.f32(G::floor_y));
        object_draw(obj);
    }
    if (of<int32_t>(obj, 0x50) > 39) {
        of<uint32_t>(obj, 0x58) &= 0xEFFFFFFEu;
        body_free_parts(obj);
        task_exit();
    }
}

// 0x61000 kind135_ebitb_decoy_land: part hits; bounces down to the floor (gravity 0.05); the motion advances for the
// first 79 frames of +0xF4; drawn with its shadow; gone with its parts after 90 frames; in the depth list
template <class P> void Game<P>::kind135_ebitb_decoy_land(uint32_t obj)
{
    mem.at<uint8_t>(G::hit_class) = 0;
    mem.at<uint16_t>(G::hit_damage) = 0;
    mem.at<int32_t>(G::hit_type) = of<int16_t>(obj, 0xCC);
    enemy_part_hits(obj);
    float y = of<float>(obj, 0x24);
    float vy = of<float>(obj, 0x7C);
    if (y > mem.f32(G::floor_y) || vy > 0.0f) {
        y = y + vy;
        vy = vy - ffrom(0x3D4CCCCD);
    } else {
        y = mem.f32(G::floor_y);
    }
    of<float>(obj, 0x7C) = vy;
    of<float>(obj, 0x24) = y;
    if (!(of<int16_t>(obj, 0xF4) > 0x4E))
        of<uint32_t>(obj, 0xF8) += 1;
    shadow_setup(mem.f32(G::floor_y));
    object_draw(obj);
    const int32_t t = of<int32_t>(obj, 0x50) + 1;
    of<int32_t>(obj, 0x50) = t;
    if (t > 0x59) {
        of<uint32_t>(obj, 0x58) &= ~1u;
        body_free_parts(obj);
        task_exit();
        return;
    }
    depth_list_add(obj);
}

// 0x610E0 kind135_ebitb_dead_glide (Chapter 2): part hits (a hit lifts it, vy + 0.5); dead and above the floor it
// glides (velocity, gravity 0.05, spinning 0x300); drawn with its shadow (blinking after 79 frames); gone with its
// parts after 160 frames; in the depth list
template <class P> void Game<P>::kind135_ebitb_dead_glide(uint32_t obj)
{
    mem.at<int32_t>(G::hit_type) = of<int16_t>(obj, 0xCC);
    mem.at<uint8_t>(G::hit_class) = 0;
    mem.at<uint16_t>(G::hit_damage) = 0;
    enemy_part_hits(obj);
    if (mem.at<uint8_t>(G::hit_class) != 0)
        of<float>(obj, 0x7C) = of<float>(obj, 0x7C) + 0.5f;
    const uint16_t hp = of<uint16_t>(obj, 0x70);
    float y = of<float>(obj, 0x24);
    if (hp == 0 && y > mem.f32(G::floor_y)) {
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + of<float>(obj, 0x78);
        of<float>(obj, 0x28) = of<float>(obj, 0x28) + of<float>(obj, 0x80);
        const float vy = of<float>(obj, 0x7C) + ffrom(0xBD4CCCCD);
        of<uint32_t>(obj, 0x2C) += 0x300;
        of<float>(obj, 0x7C) = vy;
        y = y + vy;
    } else {
        y = mem.f32(G::floor_y);
    }
    of<float>(obj, 0x24) = y;
    of<uint32_t>(obj, 0xF8) += 1;
    if (!(int32_t(uint32_t(hp) << 16) > 0)) {
        const int32_t n = of<int32_t>(obj, 0x50);
        if (!(n > 0x4F) || n % 2 != 0) {
            shadow_setup(mem.f32(G::floor_y));
            object_draw(obj);
        }
    }
    const int32_t t = of<int32_t>(obj, 0x50) + 1;
    of<int32_t>(obj, 0x50) = t;
    if (t > 0x9F) {
        of<uint32_t>(obj, 0x58) &= 0xEFFFFFFEu;
        body_free_parts(obj);
        task_exit();
        return;
    }
    depth_list_add(obj);
}

// 0x61F80 kind135_splash_spawn: kind135_splash two below the floor at the object (a 0x58-byte task)
template <class P> void Game<P>::kind135_splash_spawn(uint32_t obj)
{
    const uint32_t t = task_create(F::kind135_splash, 0x58);
    of<uint32_t>(t, 0x30) = 0;
    of<float>(t, 0x24) = mem.f32(G::floor_y) - 2.0f;
    of<uint32_t>(t, 0x20) = of<uint32_t>(obj, 0x20);
    of<uint32_t>(t, 0x50) = 0;
    of<uint32_t>(t, 0x28) = of<uint32_t>(obj, 0x28);
}

// ---------------------------------------------------------------------------------------------------------------
// branch-event kinds (events 9-11, 13): 143-145, 148

// 0x3AB50 kind145_breakable_wall (kind 145, event 11): the wall's seven pieces (kind145_wall_piece) from table 0xAF560
// (28 bytes: model, offset, angles) at the room point (-102.49, 30.2, 191.73); only the two middle pieces (0xBF1 /
// 0xBF2) are shootable (the sub-type's radius); the hit count and route_choice cleared; the group task ends
template <class P> void Game<P>::kind145_breakable_wall(uint32_t obj)
{
    for (uint32_t i = 0; (i & 0xFF) < 7; ++i) {
        const uint32_t t = task_create(F::kind145_wall_piece, 0x3C8);
        task_clear_work(t);
        of<uint16_t>(t, 0xCC) = of<uint16_t>(obj, 0xCC);
        prop_init(t);
        const uint32_t e = 0xAF560 + (i & 0xFF) * 28;
        const uint32_t m = mem.u32(e);
        of<uint32_t>(t, 0x90) = 0;
        of<uint32_t>(t, 0x54) = m;
        of<uint32_t>(t, 0x94) = 0x3F800000;
        if (m == 0xBF1 || m == 0xBF2)
            of<uint32_t>(t, 0x90) = mem.u32(0xAC2F4 + uint32_t(int32_t(of<int16_t>(obj, 0xCC)) * 0x4C));
        else
            of<uint32_t>(t, 0x58) |= 0x40000000;
        of<uint16_t>(t, 0x70) = of<uint16_t>(obj, 0x70);
        of<uint16_t>(t, 0x64) = of<uint16_t>(obj, 0x64);
        of<float>(t, 0x20) = mem.f32(e + 4) - ffrom(0x42CCFAE1);
        of<float>(t, 0x24) = mem.f32(e + 8) + ffrom(0x41F1978D);
        of<float>(t, 0x28) = mem.f32(e + 0xC) + ffrom(0x433FBBA6);
        of<uint32_t>(t, 0x2C) = mem.u32(e + 0x10);
        of<uint32_t>(t, 0x30) = mem.u32(e + 0x14);
        of<uint32_t>(t, 0x78) = 0;
        of<uint32_t>(t, 0x7C) = 0;
        of<uint32_t>(t, 0x34) = mem.u32(e + 0x18);
    }
    mem.at<uint8_t>(0x53E860) = 0;
    mem.at<uint8_t>(G::route_choice) = 0;
    task_exit();
}

// 0x3E030 kind145_wall_piece: the schedule; gone after 150 frames of falling; shot: the impact, the shared hit count
// 0x53E860: hits 1-2 chips (sound 0xA0120C), the third route_choice = 1 (sound 0xA01235); broken: the middle pieces
// burst (tumbling copies; the first starts kind145_route_hold) and end, the others fall with a random spin and drift
// (events flags 0x51E965 at frame 45, 0x51E966 at 90; blinking after 89); drawn (prop_draw), shootable unless bit 30
template <class P> void Game<P>::kind145_wall_piece(uint32_t obj)
{
    constexpr uint32_t hits = 0x53E860;
    prop_schedule_update(obj);
    if (of<int32_t>(obj, 0x50) > 0x96) {
        task_exit();
        return;
    }
    const uint32_t f = of<uint32_t>(obj, 0x58);
    if (!((f >> 30) & 1) && (f & 6)) {
        prop_bullet_impact(obj);
        const uint8_t n = uint8_t(mem.at<uint8_t>(hits) + 1);
        mem.at<uint8_t>(hits) = n;
        if (n > 2) {
            mem.at<uint8_t>(G::route_choice) = 1;
            sound_cue(0xA01235);
        } else {
            sound_cue(0xA0120C);
            kind145_wall_chips_spawn(obj);
        }
    }
    if (mem.at<uint8_t>(hits) > 2 && (of<uint32_t>(obj, 0x54) == 0xBF1 || of<uint32_t>(obj, 0x54) == 0xBF2)) {
        prop_tumble_copy(obj);
        if (of<uint32_t>(obj, 0x54) == 0xBF1)
            kind145_route_hold_start();
        task_exit();
        return;
    }
    if (mem.at<uint8_t>(hits) > 2) {
        const float vy0 = of<float>(obj, 0x7C);
        of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 1;
        if (vy0 == 0.0f) {
            of<uint32_t>(obj, 0x31C) = 0u - (rand() & 0x3F);
            const uint32_t r = rand();
            of<float>(obj, 0x7C) = vy0;
            of<float>(obj, 0x78) = vy0;
            of<uint32_t>(obj, 0x320) = 0u - (r & 15);
            of<float>(obj, 0x88) = ffrom(0xBC23D70A) - pdiv(float(int32_t(rand()) % 40), 10000.0f);
            of<float>(obj, 0x84) = ffrom(0xBC23D70A) - pdiv(float(int32_t(rand()) % 60), 10000.0f);
        }
        const uint32_t spin = of<uint32_t>(obj, 0x31C) + of<uint32_t>(obj, 0x320);
        const float vx = of<float>(obj, 0x78) + of<float>(obj, 0x84);
        const float vy = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
        of<uint32_t>(obj, 0x31C) = spin;
        of<uint32_t>(obj, 0x34) += spin;
        of<float>(obj, 0x7C) = vy;
        of<float>(obj, 0x78) = vx;
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + vx;
        const float y = of<float>(obj, 0x24);
        if (y > mem.f32(G::floor_y) + 11.0f)
            of<float>(obj, 0x24) = y + of<float>(obj, 0x7C);
    }
    const int32_t n = of<int32_t>(obj, 0x50);
    if (mem.at<uint8_t>(hits) == 0 || !(n > 0x59) || n % 2 != 0)
        prop_draw(obj);
    if (of<int32_t>(obj, 0x50) == 45)
        mem.at<uint8_t>(G::event_flags + 0x25) = 1;
    if (of<int32_t>(obj, 0x50) == 90)
        mem.at<uint8_t>(G::event_flags + 0x26) = 1;
    if (!((of<uint32_t>(obj, 0x58) >> 30) & 1))
        prop_shootable(obj);
}

// 0x3E290 kind145_route_hold_start: kind145_route_hold (a 0x58-byte task)
template <class P> void Game<P>::kind145_route_hold_start()
{
    task_create(F::kind145_route_hold, 0x58);
}

// 0x42A90 kind145_route_hold: route_choice = 1 every frame until scene_number becomes 1
template <class P> void Game<P>::kind145_route_hold(uint32_t obj)
{
    (void)obj;
    mem.at<uint8_t>(G::route_choice) = 1;
    if (mem.at<uint8_t>(G::scene_number) == 1)
        task_exit();
}

// 0x414D0 kind145_wall_chips_spawn: eight chips (prop_break_piece, models 0xBF1 / 0xBF2 at 0.15 x scale) thrown back
// from the piece by (25 * i + rand % 9 - 4) degrees around its heading + 0x8000, random spins, vy 0.15 + (rand % 20)
// / 100; floor 0.2, bounce 0.6
template <class P> void Game<P>::kind145_wall_chips_spawn(uint32_t obj)
{
    for (uint32_t i = 0; (i & 0xFF) <= 7; ++i) {
        const uint32_t t = task_create(F::prop_break_piece, 0x3C8);
        task_clear_work(t);
        prop_piece_random(t, obj);
        of<uint32_t>(t, 0x54) = 0xBF1 + uint32_t(int32_t(rand()) % 2);
        const int32_t axis = int32_t(rand()) % 3;
        const int32_t v = int32_t(rand()) % 0x8FF;
        const int32_t r = int32_t(rand());
        of<float>(t, 0x94) = of<float>(obj, 0x94) * ffrom(0x3E19999A);
        of<int32_t>(t, 0x31C + 4 * uint32_t(axis)) = r % 2 != 0 ? v : -v;
        const int32_t k = int32_t(rand()) % 9;
        const int32_t deg = int32_t((i & 0xFF) * 25) - 4 + k;
        const int32_t d = int32_t(float(deg) * ffrom(0x43360B61)) - 0x8000;
        const uint32_t a = of<uint32_t>(obj, 0x30) - uint32_t(d);
        const Tgp::Out sn = cam_copro(0x1B, {a});
        of<float>(t, 0x78) = of<float>(t, 0x78) - ffrom(sn.w[0]) / 4.5f;
        of<float>(t, 0x7C) = pdiv(float(int32_t(rand()) % 20), 100.0f) + ffrom(0x3E19999A);
        of<float>(t, 0x88) = ffrom(0xBCA3D70A) - pdiv(float(int32_t(rand()) % 20), 1000.0f);
        const Tgp::Out cs = cam_copro(0x1C, {a});
        of<uint32_t>(t, 0x354) = 0x3E4CCCCD;
        of<uint32_t>(t, 0x358) = 0x3F19999A;
        of<float>(t, 0x80) = of<float>(t, 0x80) - ffrom(cs.w[0]) / 4.5f;
    }
}

// 0x39EA0 kind148_prop_group (kind 148, event 10): four prop025 props from table 0xAEA40 (28 bytes: model, position,
// angles), scale 1; the group task ends
template <class P> void Game<P>::kind148_prop_group(uint32_t obj)
{
    for (uint32_t i = 0; (i & 0xFF) <= 3; ++i) {
        const uint32_t t = task_create(F::prop025_haguuu, 0x3C8);
        task_clear_work(t);
        of<uint16_t>(t, 0xCC) = of<uint16_t>(obj, 0xCC);
        prop_init(t);
        const uint32_t e = 0xAEA40 + (i & 0xFF) * 28;
        of<uint32_t>(t, 0x54) = mem.u32(e);
        of<uint16_t>(t, 0x64) = of<uint16_t>(obj, 0x64);
        of<uint16_t>(t, 0x70) = of<uint16_t>(obj, 0x70);
        of<uint32_t>(t, 0x94) = 0x3F800000;
        for (uint32_t k = 0; k < 6; ++k)
            of<uint32_t>(t, 0x20 + 4 * k) = mem.u32(e + 4 + 4 * k);
        of<uint16_t>(t, 0x6A) = 0;
    }
    task_exit();
}

// 0x3D190 prop025_haguuu (sub-type 25): the schedule; models 0x8A8..0x8C5 in a loop, the last three (0x8C6..0x8C8)
// rocking (+0x34 by 0x80; 0x8C6 publishes its angle at 0x53E4B0); drawn (prop_draw), the loop models faded (200)
template <class P> void Game<P>::prop025_haguuu(uint32_t obj)
{
    prop_schedule_update(obj);
    const int32_t m = of<int32_t>(obj, 0x54);
    if (m > 0x8C5 && !(m > 0x8C8)) {
        if (m == 0x8C8) {
            of<uint32_t>(obj, 0x34) -= 0x80;
        } else {
            const uint32_t a = of<uint32_t>(obj, 0x34) + 0x80;
            of<uint32_t>(obj, 0x34) = a;
            if (m == 0x8C6)
                mem.set32(0x53E4B0, a);
        }
    } else {
        of<int32_t>(obj, 0x54) = m > 0x8C4 ? 0x8A8 : m + 1;
    }
    if (of<uint32_t>(obj, 0x2F0) == 0)
        return;
    const int32_t n = of<int32_t>(obj, 0x54);
    if (n > 0x8A7 && !(n > 0x8C5)) {
        draw_fade_begin(200);
        prop_draw(obj);
        geo_send_words(0, 31, 0x7A0);
        return;
    }
    prop_draw(obj);
}

// 0x3A0F0 kind144_prop_group (kind 144, event 9): four ceiling drips (kind144_drip) at random points (x -137 + rand
// % 60, y -37, z -170 + rand % 400), levels 0-3, glow; the group task ends
template <class P> void Game<P>::kind144_prop_group(uint32_t obj)
{
    uint32_t i = 0;
    do {
        const uint32_t t = task_create(F::kind144_drip, 0x3C8);
        task_clear_work(t);
        of<uint16_t>(t, 0xCC) = of<uint16_t>(obj, 0xCC);
        prop_init(t);
        const float x = float(int32_t(rand()) % 60) + ffrom(0xC3090000);
        of<uint32_t>(t, 0x24) = 0xC2140000;
        of<float>(t, 0x20) = x;
        const float z = float(int32_t(rand()) % 400);
        of<uint32_t>(t, 0x7C) = 0;
        of<uint32_t>(t, 0x2C) = 0;
        of<uint8_t>(t, 0x2E6) = uint8_t(i);
        of<uint32_t>(t, 0x30) = 0;
        ++i;
        of<uint32_t>(t, 0x34) = 0;
        of<uint32_t>(t, 0x58) |= 0x4000000;
        of<float>(t, 0x28) = z + ffrom(0xC32A0000);
    } while (int32_t(i << 24) <= int32_t(3u << 24));
    task_exit();
}

// 0x3CB50 kind144_drip: gone in scene 10; waits (1 in 60 a frame) to start; grows for 18 frames (models 0xA2A + frame
// up to 30), then falls (gravity 0.001); at the floor a splash (splash_9f1_spawn), a drip sound and a new random
// point, waiting again; drawn (prop_draw)
template <class P> void Game<P>::kind144_drip(uint32_t obj)
{
    if (mem.at<uint8_t>(G::scene_number) == 10) {
        task_exit();
        return;
    }
    if (of<uint32_t>(obj, 0x2EC) == 0) {
        if (int32_t(rand()) % 60 == 1)
            of<uint32_t>(obj, 0x2EC) = 1;
        return;
    }
    int32_t t = of<int32_t>(obj, 0x50);
    float y = of<float>(obj, 0x24);
    if (t > 17) {
        const float vy = of<float>(obj, 0x7C) - ffrom(0x3A83126F);
        of<float>(obj, 0x7C) = vy;
        y = y + vy;
    }
    if (t != 30)
        t += 1;
    of<float>(obj, 0x24) = y;
    of<int32_t>(obj, 0x50) = t;
    if (y < mem.f32(G::floor_y)) {
        splash_9f1_spawn(of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28));
        sound_cue(0xA01240 + uint32_t(int32_t(rand()) % 3));
        of<uint32_t>(obj, 0x50) = 0;
        const float x = float(int32_t(rand()) % 60) + ffrom(0xC3090000);
        of<uint32_t>(obj, 0x24) = 0xC2140000;
        of<float>(obj, 0x20) = x;
        const float z = float(int32_t(rand()) % 400) + ffrom(0xC32A0000);
        of<uint32_t>(obj, 0x2EC) = 0;
        of<float>(obj, 0x28) = z;
    }
    of<uint32_t>(obj, 0x54) = of<uint32_t>(obj, 0x50) + 0xA2A;
    prop_draw(obj);
}

// 0x44B10 splash_9f1_spawn (x, y, z): splash_9f1 there (a 0x58-byte task, model 0x9F1, the point in +0x38..)
template <class P> void Game<P>::splash_9f1_spawn(uint32_t x, uint32_t y, uint32_t z)
{
    const uint32_t t = task_create(F::splash_9f1, 0x58);
    of<uint32_t>(t, 0x54) = 0x9F1;
    of<uint32_t>(t, 0x38) = x;
    of<uint32_t>(t, 0x3C) = y;
    of<uint32_t>(t, 0x40) = z;
}

// 0x452C0 splash_9f1: the next model until 0xA06, drawn at its point (+0x38..), scale 0.6
template <class P> void Game<P>::splash_9f1(uint32_t obj)
{
    const uint32_t m = of<uint32_t>(obj, 0x54) + 1;
    of<uint32_t>(obj, 0x54) = m;
    if (m == 0xA06) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x38), of<uint32_t>(obj, 0x3C), of<uint32_t>(obj, 0x40)});
    prop_scale(0x3F19999A);
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

// 0x3A5D0 kind143_prop_group (kind 143, event 10): stacks of crates (prop062, sub-type 24; Chapter 3: 109) in three
// columns, the counts per chapter from table 0xAFBB0; level n at 4.2 * n over the floor, columns 6.5 apart along -z
// (Chapter 3: 9 along +z); each column's z at 0x53E850, occupancy bytes at 0x53E840 + 5 * column; small random
// speeds and spins for their wobble; the group task ends
template <class P> void Game<P>::kind143_prop_group(uint32_t obj)
{
    for (uint32_t c = 0; (c & 0xFF) <= 2; ++c) {
        const uint32_t col = c & 0xFF;
        auto count = [&]() {
            const int32_t ch = mem.at<int8_t>(G::chapter);
            return mem.at<uint8_t>(0xAFBB0 + uint32_t(ch * 3) + col);
        };
        if (count() == 0)
            continue;
        for (uint32_t l = 0; (l & 0xFF) < count(); ++l) {
            const uint32_t t = task_create(F::prop062_kibako_stack, 0x3C8);
            task_clear_work(t);
            of<uint16_t>(t, 0xCC) = 24;
            if (mem.at<uint8_t>(G::chapter) == 2)
                of<uint16_t>(t, 0xCC) = 0x6D;
            prop_init(t);
            obj_copy_pose(t, obj);
            const float lv = float(int32_t(l & 0xFF));
            const float cf = float(int32_t(col));
            of<uint16_t>(t, 0x70) = of<uint16_t>(obj, 0x70);
            of<uint32_t>(t, 0x328) = col;
            of<uint8_t>(t, 0x2E6) = uint8_t(l);
            of<uint16_t>(t, 0x64) = of<uint16_t>(obj, 0x64);
            of<float>(t, 0x24) = lv * ffrom(0x40866666) + mem.f32(G::floor_y);
            of<float>(t, 0x28) = of<float>(obj, 0x28) - cf * 6.5f;
            if (mem.at<uint8_t>(G::chapter) == 2)
                of<float>(t, 0x28) = cf * 9.0f + of<float>(obj, 0x28);
            mem.set32(0x53E850 + 4 * col, of<uint32_t>(t, 0x28));
            of<uint32_t>(t, 0x7C) = 0;
            of<float>(t, 0x78) = pdiv(float(int32_t(rand()) % 17 - 8), 100.0f);
            of<float>(t, 0x80) = pdiv(float(int32_t(rand()) % 17 - 8), 100.0f);
            of<int32_t>(t, 0x31C) = (int32_t(rand()) % 21 - 10) << 5;
            of<int32_t>(t, 0x320) = (int32_t(rand()) % 21 - 10) << 5;
            mem.at<uint8_t>(0x53E840 + col * 5 + (l & 0xFF)) = 1;
        }
    }
    task_exit();
}

// 0x3DCA0 prop062_kibako_stack (sub-type 62 and kind 143's crates): the schedule; visible: shot: its sound, the impact,
// the collapse animation, its place freed, gone; a crate whose place below is empty falls (gravity 0.025, wobbling),
// kept clear of the neighbouring columns (column 0 / 2 against column 1, column 1 always: z back by its speed); one
// level lower it settles with a new wobble and takes that place; drawn (prop_draw) and shootable
template <class P> void Game<P>::prop062_kibako_stack(uint32_t obj)
{
    const uint32_t col = of<uint32_t>(obj, 0x328);
    const uint32_t occ = 0x53E840 + col * 5;
    prop_schedule_update(obj);
    if (of<uint32_t>(obj, 0x2F0) == 0)
        return;
    const uint8_t fb = of<uint8_t>(obj, 0x58);
    if ((fb & 1) && (fb & 6)) {
        sound_cue(mem.u32(0xAC324 + uint32_t(int32_t(of<int16_t>(obj, 0xCC)) * 0x4C)));
        prop_bullet_impact(obj);
        prop_collapse_anim_spawn(obj);
        mem.at<uint8_t>(occ + uint32_t(int32_t(of<int8_t>(obj, 0x2E6)))) = 0;
        task_exit();
        return;
    }
    const uint32_t lvl = of<uint8_t>(obj, 0x2E6);
    if (lvl != 0 && mem.at<uint8_t>(occ + lvl - 1) == 0) {
        if (of<float>(obj, 0x7C) == 0.0f)
            mem.at<uint8_t>(occ + lvl) = 0;
        float vz = of<float>(obj, 0x80);
        const float vy = of<float>(obj, 0x7C) - ffrom(0x3CCCCCCD);
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + of<float>(obj, 0x78);
        of<uint32_t>(obj, 0x2C) += of<uint32_t>(obj, 0x31C);
        of<float>(obj, 0x28) = of<float>(obj, 0x28) + vz;
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
        of<uint32_t>(obj, 0x34) += of<uint32_t>(obj, 0x320);
        of<float>(obj, 0x7C) = vy;
        bool back = false;
        const float z = of<float>(obj, 0x28);
        if (col == 1) {
            if (mem.at<uint8_t>(0x53E840 + lvl) != 0 && z > mem.f32(0x53E850) - 3.0f) {
                back = true;
            } else {
                int32_t g = 0;
                if (mem.at<uint8_t>(0x53E84A + lvl) != 0 && z < mem.f32(0x53E858))
                    g = 1;
                back = !(float(g) + 3.0f == 0.0f);
            }
        } else if (col == 0) {
            back = mem.at<uint8_t>(0x53E845 + lvl) != 0 && z < mem.f32(0x53E854) - 3.0f;
        } else if (col == 2) {
            back = mem.at<uint8_t>(0x53E845 + lvl) != 0 && z > mem.f32(0x53E854) + 3.0f;
        }
        if (back) {
            of<float>(obj, 0x28) = of<float>(obj, 0x28) - vz;
            vz = 0.0f;
        }
        const float below = float(int32_t(lvl) - 1) * ffrom(0x40866666) + mem.f32(G::floor_y);
        of<float>(obj, 0x80) = vz;
        if (of<float>(obj, 0x24) < below) {
            const uint8_t nl = uint8_t(of<uint8_t>(obj, 0x2E6) - 1);
            of<uint8_t>(obj, 0x2E6) = nl;
            of<uint32_t>(obj, 0x2C) = 0;
            of<uint32_t>(obj, 0x34) = 0;
            of<float>(obj, 0x24) = float(int32_t(nl)) * ffrom(0x40866666) + mem.f32(G::floor_y);
            of<uint32_t>(obj, 0x7C) = 0;
            of<float>(obj, 0x78) = pdiv(float(int32_t(rand()) % 17 - 8), 100.0f);
            of<float>(obj, 0x80) = pdiv(float(int32_t(rand()) % 17 - 8), 100.0f);
            of<int32_t>(obj, 0x31C) = (int32_t(rand()) % 21 - 10) << 5;
            of<int32_t>(obj, 0x320) = (int32_t(rand()) % 21 - 10) << 5;
            mem.at<uint8_t>(occ + uint32_t(int32_t(of<int8_t>(obj, 0x2E6)))) = 1;
        }
    }
    prop_draw(obj);
    prop_shootable(obj);
}

// 0x3AC90 kind149_prop_creature (kind 149, event 10): a creature used as a prop: type 7 the frog (motion 0xB9, its
// orbit phase +0x2EC from 0x53E4B0, prop063_frog) or type 0x4B (motion 0xC8, visible, prop119_creature); other types
// nothing
template <class P> void Game<P>::kind149_prop_creature(uint32_t obj)
{
    const int32_t type = of<int16_t>(obj, 0xCC);
    mem.at<int32_t>(G::hit_type) = type;
    if (type == 7) {
        body_init(obj, mem.at<int32_t>(G::hit_type), 0xB9);
        of<uint8_t>(obj, 0x3C6) |= 1;
        const int32_t seed = mem.at<int32_t>(0x53E4B0);
        of<uint32_t>(obj, 0x50) = 0;
        of<int32_t>(obj, 0x2EC) = seed % 0x1555;
        creature_type_setup(obj);
        of<uint16_t>(obj, 0x6A) = 0;
        of<uint32_t>(obj, 0) = F::prop063_frog;
    } else if (type == 0x4B) {
        body_init(obj, mem.at<int32_t>(G::hit_type), 0xC8);
        of<uint8_t>(obj, 0x3C6) |= 1;
        creature_type_setup(obj);
        of<uint32_t>(obj, 0x2F4) = 0;
        of<uint32_t>(obj, 0x330) = 0;
        of<uint32_t>(obj, 0x2F0) = 1;
        of<uint16_t>(obj, 0x6A) = 0;
        of<uint32_t>(obj, 0) = F::prop119_creature;
        of<uint16_t>(obj, 0x6C) ^= 1;
    }
}

// 0x3E2B0 prop063_frog (sub-type 63, kind 149's frog): shot: no longer shootable, 80 points to the shooter, motion
// 0xB3, sound 0xA01341, a splash, falls (prop063_frog_fall, vy 2); while shootable it circles (sine / cosine of
// +0x2EC: 84 around (-175.36, -127.28), +0x80 a frame); past phase 0x1A00 it jumps once (motion 0xBB, vx 1.2, vy
// 0.3, flags bit 5) and flies (gravity 0.04) until it ends below y -127; the motion advances except frame 29 of
// motion 0xBB; drawn and in the depth list
template <class P> void Game<P>::prop063_frog(uint32_t obj)
{
    if (of<uint8_t>(obj, 0x58) & 6) {
        of<uint32_t>(obj, 0x58) &= ~1u;
        add_score(prop_shooter(obj), 0x50);
        of<uint32_t>(obj, 0x7C) = 0x40000000;
        set_motion_blend(obj, 0xB3, 0, 4);
        sound_cue(0xA01341);
        prop_splash_spawn(obj);
        of<uint32_t>(obj, 0) = F::prop063_frog_fall;
    }
    if (!(of<uint8_t>(obj, 0x58) & 1))
        return;
    if (of<int32_t>(obj, 0x2EC) > 0x1A00 && !((of<uint8_t>(obj, 0x58) >> 5) & 1)) {
        of<uint32_t>(obj, 0x78) = 0x3F99999A;
        of<uint32_t>(obj, 0x7C) = 0x3E99999A;
        of<uint32_t>(obj, 0x58) |= 0x20;
        set_motion(obj, 0xBB);
    }
    if ((of<uint8_t>(obj, 0x58) >> 5) & 1) {
        const float vy = of<float>(obj, 0x7C);
        const float y = of<float>(obj, 0x24) + vy;
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + of<float>(obj, 0x78);
        of<float>(obj, 0x24) = y;
        of<float>(obj, 0x7C) = float(double(vy) - ffrom64(0x3FA47AE147AE147Bull));
        if (y < -127.0f) {
            body_free_parts(obj);
            task_exit();
            return;
        }
    } else {
        const Tgp::Out s = cam_copro(0x1B, {of<uint32_t>(obj, 0x2EC)});
        of<float>(obj, 0x20) = ffrom(s.w[0]) * 84.0f + ffrom(0xC32F5C29);
        const Tgp::Out c = cam_copro(0x1C, {of<uint32_t>(obj, 0x2EC)});
        const float y = ffrom(c.w[0]) * 84.0f + ffrom(0xC2FE8F5C);
        of<uint32_t>(obj, 0x2EC) = (of<uint32_t>(obj, 0x2EC) + 0x80) & 0xFFFF;
        of<float>(obj, 0x24) = y;
    }
    if (of<uint16_t>(obj, 0xD0) != 0xBB || of<uint16_t>(obj, 0xF4) != 29)
        of<uint32_t>(obj, 0xF8) += 1;
    object_draw(obj);
    depth_list_add(obj);
}

// 0x3E4E0 prop063_frog_fall: gone with its parts below y -127; falls (gravity 0.05) toward +z (1.5 a frame) tumbling;
// the motion advances except frame 10 of motion 0xB3; drawn
template <class P> void Game<P>::prop063_frog_fall(uint32_t obj)
{
    if (of<float>(obj, 0x24) < -127.0f) {
        body_free_parts(obj);
        task_exit();
        return;
    }
    const float vy = of<float>(obj, 0x7C) - ffrom(0x3D4CCCCD);
    of<uint32_t>(obj, 0x2C) += 0x800;
    of<uint32_t>(obj, 0x34) -= 0x800;
    of<float>(obj, 0x7C) = vy;
    of<float>(obj, 0x28) = of<float>(obj, 0x28) + 1.5f;
    of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
    if (of<uint16_t>(obj, 0xD0) != 0xB3 || of<uint16_t>(obj, 0xF4) != 10)
        of<uint32_t>(obj, 0xF8) += 1;
    object_draw(obj);
}

// 0x3F8B0 prop119_creature (sub-type 119, kind 149's type 0x4B): ends with its parts after +0x70 scene changes; shot:
// no longer shootable, sound 0xA01341, 80 points to the shooter, a splash (prop_splash_spawn's code inline), upright,
// motion 0xCD, thrown up (vy 1.5, at least 0.1 over the floor) to prop119_creature_fall; drawn and in the depth list
template <class P> void Game<P>::prop119_creature(uint32_t obj)
{
    const int32_t scene = mem.at<int8_t>(G::scene_number);
    if (scene != of<int32_t>(obj, 0x2EC)) {
        of<int32_t>(obj, 0x2EC) = scene;
        const int32_t n = of<int32_t>(obj, 0x330) + 1;
        of<int32_t>(obj, 0x330) = n;
        if (n > of<int16_t>(obj, 0x70)) {
            body_free_parts(obj);
            task_exit();
            return;
        }
    }
    if (of<uint8_t>(obj, 0x58) & 6) {
        of<uint32_t>(obj, 0x58) &= ~1u;
        sound_cue(0xA01341);
        add_score(prop_shooter(obj), 0x50);
        prop_splash_spawn(obj);
        of<uint32_t>(obj, 0x2C) = 0;
        of<uint32_t>(obj, 0x34) = 0;
        set_motion_blend(obj, 0xCD, 16, 0);
        of<uint32_t>(obj, 0x7C) = 0x3FC00000;
        if (!(of<float>(obj, 0x24) > mem.f32(G::floor_y)))
            of<float>(obj, 0x24) = mem.f32(G::floor_y) + ffrom(0x3DCCCCCD);
        of<uint32_t>(obj, 0) = F::prop119_creature_fall;
    }
    of<uint32_t>(obj, 0xF8) += 1;
    object_draw(obj);
    depth_list_add(obj);
}

// 0x3FA20 prop119_creature_fall: gone with its parts below the floor; rises / falls (gravity 0.08); drawn
template <class P> void Game<P>::prop119_creature_fall(uint32_t obj)
{
    if (of<float>(obj, 0x24) < mem.f32(G::floor_y)) {
        body_free_parts(obj);
        task_exit();
        return;
    }
    const float vy = of<float>(obj, 0x7C);
    const float y = of<float>(obj, 0x24) + vy;
    of<uint32_t>(obj, 0xF8) += 1;
    of<float>(obj, 0x24) = y;
    of<float>(obj, 0x7C) = vy - ffrom(0x3DA3D70A);
    object_draw(obj);
}

// 0x39D80 kind146_prop_group (kind 146, event 13): five balls (kind146_ball) of its sub-type (56 / 57): models 0xB44 +
// 4 * set + rand % 5 + 4 * i (the roll frame in +0x338), at table 0xAFB60 (40 bytes per set: x / z pairs) around its
// position, their motion records at 0x53E3C0 + 120 * set + 24 * i (position, velocity), random headings; the group
// task ends
template <class P> void Game<P>::kind146_prop_group(uint32_t obj)
{
    if (pc_path()) {   // THOTD.EXE's kind 146 is only `jmp task_exit` (pmrest2)
        task_exit();
        return;
    }
    for (uint32_t i = 0; (i & 0xFF) <= 4; ++i) {
        const uint32_t t = task_create(F::kind146_ball, 0x3C8);
        task_clear_work(t);
        of<uint16_t>(t, 0xCC) = of<uint16_t>(obj, 0xCC);
        prop_init(t);
        const uint32_t set = uint32_t(of<uint8_t>(obj, 0xCC) - 56) & 0xFF;
        const int32_t r = int32_t(rand()) % 5;
        const uint32_t n = i & 0xFF;
        of<uint32_t>(t, 0x54) = 0xB44 + set * 4 + uint32_t(r) + n * 4;
        const uint32_t tab = 0xAFB60 + set * 40;
        of<int32_t>(t, 0x338) = r;
        const float x = mem.f32(tab + n * 8) + of<float>(obj, 0x20);
        const uint32_t blk = 0x53E3C0 + n * 24 + set * 120;
        mem.set32(blk, fbits(x));
        of<float>(t, 0x20) = x;
        mem.set32(blk + 4, of<uint32_t>(obj, 0x24));
        of<uint32_t>(t, 0x24) = of<uint32_t>(obj, 0x24);
        const float z = mem.f32(tab + n * 8 + 4) + of<float>(obj, 0x28);
        mem.set32(blk + 8, fbits(z));
        of<float>(t, 0x28) = z;
        of<uint32_t>(t, 0x34) = rand() & 0xFFFF;
        of<uint16_t>(t, 0x70) = of<uint16_t>(obj, 0x70);
        of<uint32_t>(t, 0x344) = of<uint32_t>(obj, 0x20);
        of<uint32_t>(t, 0x334) = n;
        mem.set32(blk + 0xC, 0);
        of<uint32_t>(t, 0x348) = of<uint32_t>(obj, 0x28);
        mem.set32(blk + 0x10, 0);
        mem.set32(blk + 0x14, 0);
        of<uint16_t>(t, 0x64) = of<uint16_t>(obj, 0x64);
    }
    task_exit();
}

// 0x3CC90 kind146_ball: the schedule; visible: shot: the impact, a random push ((rand % 10) / 10 - 0.5 in x / z), a
// random heading; against each other ball closer than 1 in x and z the velocities are exchanged (the other's z then
// moved by this ball's velocity, as in the original); moves, bounces off the table edges (x -13..+14, z -5..+7 around
// the group's point), friction 0.99, the roll frames while moving faster than 0.01; faces the camera; prop_draw,
// shootable
template <class P> void Game<P>::kind146_ball(uint32_t obj)
{
    const uint32_t set = uint32_t(of<uint8_t>(obj, 0xCC) - 56) & 0xFF;
    const uint32_t me = of<uint8_t>(obj, 0x334);
    prop_schedule_update(obj);
    if (of<uint32_t>(obj, 0x2F0) == 0)
        return;
    const uint32_t base = 0x53E3C0 + set * 120;
    const uint32_t blk = base + me * 24;
    const uint8_t fb = of<uint8_t>(obj, 0x58);
    if ((fb & 1) && (fb & 6)) {
        prop_bullet_impact(obj);
        const float vx = float(int32_t(rand()) % 10) * ffrom(0x3DCCCCCD) - 0.5f;
        mem.set32(blk + 0xC, fbits(vx));
        const float vz = float(int32_t(rand()) % 10) * ffrom(0x3DCCCCCD) - 0.5f;
        mem.set32(blk + 0x14, fbits(vz));
        of<uint32_t>(obj, 0x34) = rand() & 0xFFFF;
    }
    for (uint32_t j = 0; (j & 0xFF) <= 4; ++j) {
        const uint32_t o = base + (j & 0xFF) * 24;
        if ((j & 0xFF) == me)
            continue;
        float d = mem.f32(blk) - mem.f32(o);
        if (d < 0.0f)
            d = -d;
        if (!(d < 1.0f))
            continue;
        d = mem.f32(blk + 8) - mem.f32(o + 8);
        if (d < 0.0f)
            d = -d;
        if (!(d < 1.0f))
            continue;
        const float mx = mem.f32(blk + 0xC), ox = mem.f32(o + 0xC);
        const float mz = mem.f32(blk + 0x14), oz = mem.f32(o + 0x14);
        const float sx = mx + ox, sz = mz + oz;
        const float nmx = (sx + (ox - mx)) / 2.0f, nox = (sx + (mx - ox)) / 2.0f;
        const float nmz = (sz + (oz - mz)) / 2.0f, noz = (sz + (mz - oz)) / 2.0f;
        mem.set32(blk + 0xC, fbits(nmx));
        mem.set32(blk + 0x14, fbits(nmz));
        mem.set32(o + 0xC, fbits(nox));
        mem.set32(o + 0x14, fbits(noz));
        mem.set32(blk, fbits(mem.f32(blk) + mem.f32(blk + 0xC)));
        mem.set32(o, fbits(mem.f32(o) + mem.f32(o + 0xC)));
        mem.set32(blk + 8, fbits(mem.f32(blk + 8) + mem.f32(blk + 0x14)));
        mem.set32(o + 8, fbits(mem.f32(o + 8) + mem.f32(blk + 0x14)));
        of<uint32_t>(obj, 0x34) = rand() & 0xFFFF;
    }
    mem.set32(blk, fbits(mem.f32(blk) + mem.f32(blk + 0xC)));
    mem.set32(blk + 8, fbits(mem.f32(blk + 8) + mem.f32(blk + 0x14)));
    const float x1 = of<float>(obj, 0x344) + 14.0f;
    if (mem.f32(blk) > x1) {
        mem.set32(blk + 0xC, mem.u32(blk + 0xC) ^ 0x80000000u);
        mem.set32(blk, fbits(x1));
    }
    const float x0 = of<float>(obj, 0x344) + ffrom(0xC1500000);
    if (mem.f32(blk) < x0) {
        mem.set32(blk + 0xC, mem.u32(blk + 0xC) ^ 0x80000000u);
        mem.set32(blk, fbits(x0));
    }
    const float z1 = of<float>(obj, 0x348) + 7.0f;
    if (mem.f32(blk + 8) > z1) {
        mem.set32(blk + 0x14, mem.u32(blk + 0x14) ^ 0x80000000u);
        mem.set32(blk + 8, fbits(z1));
    }
    const float z0 = of<float>(obj, 0x348) + ffrom(0xC0A00000);
    if (mem.f32(blk + 8) < z0) {
        mem.set32(blk + 0x14, mem.u32(blk + 0x14) ^ 0x80000000u);
        mem.set32(blk + 8, fbits(z0));
    }
    of<uint32_t>(obj, 0x20) = mem.u32(blk);
    of<uint32_t>(obj, 0x28) = mem.u32(blk + 8);
    mem.set32(blk + 0xC, fbits(mem.f32(blk + 0xC) * ffrom(0x3F7D70A4)));
    mem.set32(blk + 0x14, fbits(mem.f32(blk + 0x14) * ffrom(0x3F7D70A4)));
    float ax = mem.f32(blk + 0xC);
    if (ax < 0.0f)
        ax = -ax;
    bool roll = ax > ffrom(0x3C23D70A);
    if (!roll) {
        float az = mem.f32(blk + 0x14);
        if (az < 0.0f)
            az = -az;
        roll = az > ffrom(0x3C23D70A);
    }
    if (roll) {
        const int32_t f = of<int32_t>(obj, 0x338);
        of<int32_t>(obj, 0x338) = f + 1;
        if (f > 3) {
            of<uint32_t>(obj, 0x338) = 0;
            of<uint32_t>(obj, 0x54) -= 4;
        } else {
            of<uint32_t>(obj, 0x54) += 1;
        }
    }
    prop_face_camera(obj);
    of<uint32_t>(obj, 0x30) += 0x8000;
    prop_draw(obj);
    prop_shootable(obj);
}

// ---------------------------------------------------------------------------------------------------------------
// kinds 141 / 142, the slugs (event 11)

// a light record to the geometrizer (inline: the draw param's or record 0)
template <class P> void Game<P>::slug_light(int32_t param)
{
    const uint32_t rec = 0x520A40 + uint32_t(param) * 12;
    hw_write32(0x8000A0, 0xA0A);
    hw_write32(0x804000, mem.u32(rec));
    hw_write32(0x804000, mem.u32(rec + 4));
    hw_write32(0x804000, mem.u32(rec + 8));
}

// the end of a slug frame (inline): its camera point, home, the last enemy's home while it is the last, hit bits
// off, the depth list
template <class P> void Game<P>::slug_frame_end(uint32_t obj)
{
    prop_view_pos(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28));
    of<uint32_t>(obj, 0x2D4) = of<uint32_t>(obj, 0x20);
    of<uint32_t>(obj, 0x2D8) = of<uint32_t>(obj, 0x24);
    of<uint32_t>(obj, 0x2DC) = of<uint32_t>(obj, 0x28);
    if (!(mem.at<int16_t>(G::enemies_alive) > 1) && !((of<uint32_t>(obj, 0x58) >> 20) & 1)) {
        mem.set32(0x51FDE0, of<uint32_t>(obj, 0x2D4));
        mem.set32(0x51FDE4, of<uint32_t>(obj, 0x2D8));
        mem.set32(0x51FDE8, of<uint32_t>(obj, 0x2DC));
    }
    of<uint32_t>(obj, 0x58) &= 0xFFFFFFF9u;
    depth_list_add(obj);
}

// 0x647B0 bspline_basis (segment, t, w0..w3): the cubic B-spline weights, clamped forms for segments 0, 2, 12, 14
template <class P> void Game<P>::bspline_basis(uint32_t seg, float t, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3)
{
    float w0, w1, w2, w3;
    switch (seg & 0xFF) {
    case 0: {
        float a = t * 21.0f, b = t * -11.0f;
        a = a * t;
        b = b * t;
        a = a * t;
        float c = t * 9.0f;
        const float d = t * 3.0f;
        float e = t * t;
        b = b * t;
        a = a / 12.0f;
        const float u = 1.0f - t;
        c = c * t;
        float f = d * t;
        e = e * t;
        b = b / 12.0f;
        float g = u * u;
        c = c / 2.0f;
        f = f / 2.0f;
        e = e / 6.0f;
        g = g * u;
        a = a - c;
        a = a + d;
        b = b + f;
        w0 = g, w1 = a, w2 = b, w3 = e;
        break;
    }
    case 2: {
        float a = -t, b = t * 7.0f, c = t * 5.0f;
        a = a * t;
        b = b * t;
        float u = 1.0f - t;
        c = c * t;
        a = a * t;
        b = b * t;
        float r7 = u * u;
        c = c / 4.0f;
        a = a / 2.0f;
        b = b / 12.0f;
        const float r4 = t / 4.0f;
        r7 = r7 * u;
        u = t / 2.0f;
        float r8 = t * t;
        r7 = r7 / 4.0f;
        const float r9 = r8 / 2.0f;
        r8 = r8 * t;
        b = b - c;
        a = a + r9;
        b = b + r4;
        a = a + u;
        r8 = r8 / 6.0f;
        b = b + ffrom(0x3F155555);
        a = a + ffrom(0x3E2AAAAB);
        w0 = r7, w1 = b, w2 = a, w3 = r8;
        break;
    }
    case 12: {
        float u = 1.0f - t;
        float a = u * 7.0f, r8 = u * u, r6 = u * 5.0f;
        a = a * u;
        float r9 = r8 * u;
        r6 = r6 * u;
        a = a * u;
        float r7 = t * t;
        float n = -r9;
        r6 = r6 / 4.0f;
        a = a / 12.0f;
        r7 = r7 * t;
        r8 = r8 / 2.0f;
        n = n / 2.0f;
        const float q = u / 4.0f;
        r7 = r7 / 4.0f;
        a = a - r6;
        n = n + r8;
        a = a + q;
        u = u / 2.0f;
        r9 = r9 / 6.0f;
        n = n + u;
        a = a + ffrom(0x3F155555);
        n = n + ffrom(0x3E2AAAAB);
        w3 = r7, w2 = a, w1 = n, w0 = r9;
        break;
    }
    case 14: {
        float u = 1.0f - t;
        float a = u * 21.0f, b = u * -11.0f;
        a = a * u;
        b = b * u;
        float r5 = u * 9.0f;
        a = a * u;
        b = b * u;
        float r6 = u * u;
        r5 = r5 * u;
        a = a / 12.0f;
        b = b / 12.0f;
        float r4 = t * t;
        float r7 = r6 * u;
        r6 = r6 / 2.0f;
        r5 = r5 / 2.0f;
        r7 = r7 / 6.0f;
        u = u * 3.0f;
        r4 = r4 * t;
        a = a - r5;
        a = a + u;
        b = b + r6;
        w3 = r4, w2 = a, w1 = b, w0 = r7;
        break;
    }
    default: {
        float g6 = -t;
        g6 = g6 + 1.0f;
        float g7 = t * 0.5f;
        g6 = g6 * t;
        float r4 = t * t;
        const float u = 1.0f - t;
        g7 = g7 - 1.0f;
        g6 = g6 + 1.0f;
        float g13 = u * u;
        r4 = r4 * t;
        g7 = g7 * t;
        g6 = g6 * 0.5f;
        g13 = g13 * u;
        r4 = r4 / 6.0f;
        g13 = g13 / 6.0f;
        g7 = g7 * t;
        g6 = g6 * t;
        g7 = g7 + ffrom(0x3F2AAAAB);
        g6 = g6 + ffrom(0x3E2AAAAB);
        w0 = g13, w1 = g7, w2 = g6, w3 = r4;
        break;
    }
    }
    mem.set32(p0, fbits(w0));
    mem.set32(p1, fbits(w1));
    mem.set32(p2, fbits(w2));
    mem.set32(p3, fbits(w3));
}

// 0x61FD0 kind141_slug (kind 141): a crawling slug (model 0x971 + rand % 44 as its frame, radius 4, scale 0.5, the
// knock-back speed (1.75, 1.1), draw param 1); counted as an enemy, an attack position taken; kind141_slug_frame
template <class P> void Game<P>::kind141_slug(uint32_t obj)
{
    of<uint32_t>(obj, 0x58) = 0x401;
    of<uint32_t>(obj, 0x54) = 0x971;
    const int32_t r = int32_t(rand()) % 44;
    of<uint32_t>(obj, 0x90) = 0x40800000;
    of<uint32_t>(obj, 0x94) = 0x3F000000;
    of<uint32_t>(obj, 0x78) = 0x3FE00000;
    of<uint32_t>(obj, 0x7C) = 0x3F8CCCCD;
    of<int16_t>(obj, 0x6A) = 1;
    for (uint32_t o : {0x2ECu, 0x30Cu, 0x2F0u, 0x33Cu, 0x340u})
        of<uint32_t>(obj, o) = 0;
    of<int32_t>(obj, 0x50) = r;
    mem.at<uint16_t>(G::enemy_count) += 1;
    mem.at<uint16_t>(G::enemies_alive) += 1;
    of<uint8_t>(obj, 0x2E7) = 0xFF;
    attack_slot_take_wide(obj);
    of<uint32_t>(obj, 0) = F::kind141_slug_frame;
}

// the slug's end as an enemy (inline): flags bit 20 while more than one is alive, one fewer alive, its attack
// position freed
template <class P> void Game<P>::slug_uncount(uint32_t obj)
{
    if (mem.at<int16_t>(G::enemies_alive) > 1)
        of<uint32_t>(obj, 0x58) |= 0x100000;
    mem.at<uint16_t>(G::enemies_alive) -= 1;
    if (of<uint8_t>(obj, 0x2E7) != 0xFF)
        enemy_attack_slot_free(obj);
}

// 0x62290 kind141_slug_frame: action 1 waits 60 frames first; shot: 80 points to the shooter, a spurt, sound 0xA01341,
// kind141_slug_burst; within 30 of the camera it attacks (flags bit 31); crawling: along the B-spline of path table
// 0xC5590 (action x 72 bytes, segment +0x2EC, t +0x30C by 0.01; past segment 6 it attacks) facing its direction;
// attacking: leaps at the camera (gravity 0.0125) and within 3 hits its player (player_hit) and bursts; the crawl
// frames (+0x50, 44); drawn (the draw param's light, T R S, model + frame; attacking: the spit model 0x9BE + frame %
// 30 at x 16), its camera point and home, the depth list
template <class P> void Game<P>::kind141_slug_frame(uint32_t obj)
{
    const uint32_t tbits = of<uint32_t>(obj, 0x30C);
    const uint32_t seg = of<uint8_t>(obj, 0x2EC);
    if (of<uint16_t>(obj, 0x64) == 1) {
        const int32_t w = of<int32_t>(obj, 0x2F0);
        of<int32_t>(obj, 0x2F0) = w + 1;
        if (w <= 59)
            return;
    }
    const uint8_t fb = of<uint8_t>(obj, 0x58);
    if (((fb & 1) && (fb & 6)) || return_false() != 0) {
        add_score(prop_shooter(obj), 0x50);
        prop_spurt_spawn(obj);
        sound_cue(0xA01341);
        of<uint32_t>(obj, 0x50) = 0;
        of<uint32_t>(obj, 0x7C) = 0;
        of<uint32_t>(obj, 0) = F::kind141_slug_burst;
        of<uint32_t>(obj, 0x58) = (of<uint32_t>(obj, 0x58) | 0x10000000) & ~1u;
        slug_uncount(obj);
    }
    const uint32_t cam = mem.u32(0x51F620) * 0x154;
    if (mem.f32(0x51EAB0 + cam) - of<float>(obj, 0x20) < 30.0f && !(of<int32_t>(obj, 0x58) < 0)) {
        of<uint32_t>(obj, 0x50) = 0;
        of<uint32_t>(obj, 0x58) |= 0x80000000u;
    }
    const uint32_t f = of<uint32_t>(obj, 0x58);
    if (!((f >> 28) & 1)) {
        if (int32_t(f) >= 0) {
            LocalScope scope(*this);
            const uint32_t w = local_alloc(16);
            of<uint32_t>(obj, 0x33C) = of<uint32_t>(obj, 0x20);
            of<uint32_t>(obj, 0x340) = of<uint32_t>(obj, 0x28);
            bspline_basis(seg, ffrom(tbits), w, w + 4, w + 8, w + 12);
            const float w0 = mem.f32(w), w1 = mem.f32(w + 4), w2 = mem.f32(w + 8), w3 = mem.f32(w + 12);
            const uint32_t tab = 0xC5590 + uint32_t(int32_t(of<int16_t>(obj, 0x64)) * 9) * 8 + (seg & 0xFF) * 4;
            float x = w0 * mem.f32(tab);
            x = x + w1 * mem.f32(tab + 8);
            x = x + w2 * mem.f32(tab + 0x10);
            x = x + w3 * mem.f32(tab + 0x18);
            of<float>(obj, 0x20) = x;
            float z = w0 * mem.f32(tab + 4);
            z = z + w1 * mem.f32(tab + 0xC);
            z = z + w2 * mem.f32(tab + 0x14);
            z = z + w3 * mem.f32(tab + 0x1C);
            const float nt = of<float>(obj, 0x30C) + ffrom(0x3C23D70A);
            of<float>(obj, 0x30C) = nt;
            of<float>(obj, 0x28) = z;
            if (nt > 1.0f) {
                const int32_t s = of<int32_t>(obj, 0x2EC) + 2;
                of<uint32_t>(obj, 0x30C) = 0;
                of<int32_t>(obj, 0x2EC) = s;
                if (s > 6) {
                    of<uint32_t>(obj, 0x50) = 0;
                    of<uint32_t>(obj, 0x58) |= 0x80000000u;
                }
            }
            const float dx = of<float>(obj, 0x20) - of<float>(obj, 0x33C);
            const float dz = of<float>(obj, 0x28) - of<float>(obj, 0x340);
            const Tgp::Out o = cam_copro(0x0F, {fbits(dx), 0, fbits(dz)});
            uint32_t head = o.w[0];
            const uint32_t v = o.w[1] & 0xFFFF;
            if (((v ^ (v << 1)) >> 15) & 1)
                head += 0x8000;
            if (of<float>(obj, 0x30C) != ffrom(0x3C23D70A))
                of<uint32_t>(obj, 0x30) = head;
        } else {
            const uint32_t base = 0x51EA50 + mem.u32(0x51F620) * 0x154;
            const float dx = mem.f32(base + 0x60) - of<float>(obj, 0x20);
            const float dz = mem.f32(base + 0x68) - of<float>(obj, 0x28);
            const Tgp::Out o = cam_copro(0x0F, {fbits(dx), 0, fbits(dz)});
            of<uint32_t>(obj, 0x30) = o.w[0];
            const uint32_t v = o.w[1] & 0xFFFF;
            if (((v ^ (v << 1)) >> 15) & 1)
                of<uint32_t>(obj, 0x30) += 0x8000;
            const float vy = of<float>(obj, 0x7C);
            const float y = of<float>(obj, 0x24) + vy;
            const float x = of<float>(obj, 0x20) + of<float>(obj, 0x78);
            const float nvy = float(double(vy) - ffrom64(0x3F8999999999999Aull));
            of<float>(obj, 0x24) = y;
            of<float>(obj, 0x20) = x;
            of<float>(obj, 0x7C) = nvy;
            const uint32_t c2 = mem.u32(0x51F620) * 0x154;
            if (mem.f32(0x51EAB0 + c2) - of<float>(obj, 0x20) < 3.0f) {
                if (mem.at<uint16_t>(G::players_in_game_b) == 1)
                    player_hit(mem.at<int8_t>(0x51F700), 1, 4);
                else
                    player_hit(1 - int32_t(of<int16_t>(obj, 0x64)), 1, 4);
                of<uint32_t>(obj, 0x50) = 0;
                of<uint32_t>(obj, 0) = F::kind141_slug_burst;
                of<uint32_t>(obj, 0x58) &= ~1u;
                slug_uncount(obj);
            }
        }
    }
    const int32_t fr = of<int32_t>(obj, 0x50);
    of<int32_t>(obj, 0x50) = fr + 1;
    if (fr > 43)
        of<uint32_t>(obj, 0x50) = 0;
    slug_light(of<int16_t>(obj, 0x6A));
    cam_copro(0x05, {});
    prop_place_xyz(obj);
    prop_scale(of<uint32_t>(obj, 0x94));
    geo_draw_model(of<uint32_t>(obj, 0x54) + of<uint32_t>(obj, 0x50));
    if (of<int32_t>(obj, 0x58) < 0) {
        cam_copro(0x14, {0x800});
        prop_scale(0x41800000);
        geo_draw_model(uint32_t(of<int32_t>(obj, 0x50) % 30 + 0x9BE));
    }
    cam_copro(0x06, {});
    prop_light0();
    slug_frame_end(obj);
}

// 0x62EB0 kind141_slug_burst: the burst shrinking (+0x94 by 0.015 to 0.1), then gone after 80 frames (one enemy
// fewer); kind141_slug_burst_draw
template <class P> void Game<P>::kind141_slug_burst(uint32_t obj)
{
    const float s = of<float>(obj, 0x94);
    if (s > ffrom(0x3DCCCCCD) && ((of<uint32_t>(obj, 0x58) >> 28) & 1)) {
        of<float>(obj, 0x94) = s - ffrom(0x3C75C28F);
    } else if (of<int32_t>(obj, 0x50) > 0x50 || !((of<uint32_t>(obj, 0x58) >> 28) & 1)) {
        of<uint32_t>(obj, 0x58) &= 0xEFFFFFFEu;
        mem.at<uint16_t>(G::enemy_count) -= 1;
        task_exit();
        return;
    }
    kind141_slug_burst_draw(obj);
}

// 0x62F4C kind141_slug_burst_draw: falls to the floor (gravity 0.04); drawn (model 0x99F scaled 1 - s, s, 1 - s)
// on odd frames and while larger than 0.1
template <class P> void Game<P>::kind141_slug_burst_draw(uint32_t obj)
{
    float y = of<float>(obj, 0x24);
    if (y > mem.f32(G::floor_y)) {
        const float vy = of<float>(obj, 0x7C) - ffrom(0x3D23D70A);
        of<float>(obj, 0x7C) = vy;
        y = y + vy;
    } else {
        y = mem.f32(G::floor_y);
    }
    const int32_t n = of<int32_t>(obj, 0x50) + 1;
    of<float>(obj, 0x24) = y;
    of<int32_t>(obj, 0x50) = n;
    if (n % 2 == 0 && !(of<float>(obj, 0x94) > ffrom(0x3DCCCCCD)))
        return;
    slug_light(of<int16_t>(obj, 0x6A));
    cam_copro(0x05, {});
    const float s = of<float>(obj, 0x94);
    const float k = float(1.0 - double(s));
    prop_place_xyz(obj);
    cam_copro(0x13, {fbits(k), fbits(s), fbits(k)});
    geo_draw_model(0x99F);
    cam_copro(0x06, {});
    prop_light0();
}

// 0x62090 kind142_slug_spitter (kind 142): 27 spitting slugs (kind142_slug_spitter_frame) at the points of table
// 0xC5620 in rows of five (+0x2E6 = i / 5), headings by position; one per row (0, 6, 11, 16, 21, 26) counts as an
// enemy; the spit state (0x543440..: row flags, spit count / limit 3, interval 90, the last spit frame, the scene,
// the row allowed) reset; the group task ends
template <class P> void Game<P>::kind142_slug_spitter(uint32_t obj)
{
    for (uint32_t i = 0; (i & 0xFF) <= 26; ++i) {
        const uint32_t t = task_create(F::kind142_slug_spitter_frame, 0x3C8);
        task_clear_work(t);
        of<uint32_t>(t, 0x58) = 0x401;
        const int32_t r = int32_t(rand()) % 44;
        const uint32_t n = i & 0xFF;
        const uint32_t e = 0xC5620 + n * 12;
        of<uint32_t>(t, 0x20) = mem.u32(e);
        of<uint32_t>(t, 0x2C) = 0x8000;
        of<uint32_t>(t, 0x30) = 0;
        of<uint32_t>(t, 0x24) = mem.u32(e + 4);
        of<uint32_t>(t, 0x34) = 0xC000;
        of<uint32_t>(t, 0x28) = mem.u32(e + 8);
        of<uint32_t>(t, 0x54) = uint32_t(r + 0x971);
        if (n > 14) {
            of<uint32_t>(t, 0x2C) = 0;
            of<uint32_t>(t, 0x30) = 0x8000;
            of<uint32_t>(t, 0x34) = 0xC000;
        }
        if (n == 25) {
            of<uint32_t>(t, 0x30) = 0x8000;
            of<uint32_t>(t, 0x2C) = 0;
            of<uint32_t>(t, 0x34) = 0x7800;
        } else if (n == 26) {
            of<uint32_t>(t, 0x2C) = 0;
            of<uint32_t>(t, 0x30) = 0x8000;
            of<uint32_t>(t, 0x34) = 0x8800;
        }
        of<uint8_t>(t, 0x2E7) = 0xFF;
        if (n == 0 || n == 6 || n == 11 || n == 16 || n == 21 || n == 26) {
            of<uint8_t>(t, 0x2E7) = 0;
            mem.at<uint16_t>(G::enemy_count) += 1;
            mem.at<uint16_t>(G::enemies_alive) += 1;
        }
        of<uint32_t>(t, 0x90) = 0x40A00000;
        of<uint32_t>(t, 0x78) = 0;
        of<uint32_t>(t, 0x7C) = 0;
        of<uint32_t>(t, 0x2EC) = 0;
        of<uint32_t>(t, 0x94) = 0x3F000000;
        of<int16_t>(t, 0x6A) = 1;
        of<uint8_t>(t, 0x2E6) = uint8_t(n / 5);
        mem.at<uint8_t>(0x543440 + uint32_t(int32_t(of<int8_t>(obj, 0x2E6)))) = 0;
    }
    for (uint32_t a : {0x543440u, 0x543441u, 0x543442u, 0x543443u, 0x543444u, 0x543445u, 0x543448u})
        mem.at<uint8_t>(a) = 0;
    const int32_t r = int32_t(rand()) % 6;
    mem.at<uint8_t>(0x543449) = 3;
    mem.at<uint8_t>(0x543450) = 0x5A;
    const uint32_t frame = mem.u32(0x51F9D4);
    const uint8_t scene = mem.at<uint8_t>(G::scene_number);
    mem.at<uint8_t>(0x543453) = 0;
    mem.set32(0x54344C, frame - 60);
    mem.at<uint8_t>(0x543451) = scene;
    mem.at<uint8_t>(0x543452) = uint8_t(r);
    task_exit();
}

// 0x62A20 kind142_slug_spitter_frame: the spit pacing (every 300 frames one more spit allowed up to 11 and the
// interval 10 shorter; 2 players: 3 / 90); shot, or its row shot: the whole row (0x543440 + row) dies: 80 points to
// the shooter, sound, a spurt, the burst model 0x99F flying off (kind142_slug_fall) and one enemy fewer per counted
// slug; the row picked at random (0x543452) spits when the interval passed and the count allows (1 in 6 a frame):
// the spit models 0x9A0..0x9BD, then the blob (sound 0xA01213, kind142_spit_spawn) and back; crawls 0.1 a frame
// toward -z for 150 frames (not the one at x -355); crawl frames 0x971..0x99C; drawn (obj_model_draw_colour), its
// camera point and home, the depth list; the slug sharing the first one's model steps one frame on
template <class P> void Game<P>::kind142_slug_spitter_frame(uint32_t obj)
{
    mem.set32(0x543454, of<uint32_t>(obj, 0x54));
    const int32_t r = int32_t(rand()) % 6;
    const int32_t fr = mem.at<int32_t>(0x51F9D4);
    mem.at<uint8_t>(0x543452) = uint8_t(r);
    bool clear = true;
    if (fr % 300 == 0 && mem.at<uint8_t>(0x543453) == 0) {
        uint8_t lim = mem.at<uint8_t>(0x543449);
        mem.at<uint8_t>(0x543453) = 1;
        if (!(lim > 10))
            lim = uint8_t(lim + 1);
        mem.at<uint8_t>(0x543449) = lim;
        if (mem.at<uint8_t>(0x543450) != 0)
            mem.at<uint8_t>(0x543450) = uint8_t(mem.at<uint8_t>(0x543450) + 0xF6);
        clear = false;
    }
    if (clear && mem.at<int32_t>(0x51F9D4) % 300 != 0)
        mem.at<uint8_t>(0x543453) = 0;
    if (mem.at<uint16_t>(0x51F4C8) == 4) {
        mem.at<uint8_t>(0x543449) = 3;
        mem.at<uint8_t>(0x543450) = 0x5A;
    }
    const uint8_t fb = of<uint8_t>(obj, 0x58);
    const uint32_t row = of<uint8_t>(obj, 0x2E6);
    if (((fb & 1) && (fb & 6)) || mem.at<uint8_t>(0x543440 + row) != 0) {
        mem.at<uint8_t>(0x543440 + row) = 1;
        if (of<uint8_t>(obj, 0x58) & 6) {
            add_score(prop_shooter(obj), 0x50);
            sound_cue(0xA01341);
            prop_spurt_spawn(obj);
        }
        of<uint32_t>(obj, 0x50) = 0;
        of<uint32_t>(obj, 0x54) = 0x99F;
        of<uint32_t>(obj, 0x58) = (of<uint32_t>(obj, 0x58) | 0x10000000) & ~1u;
        const float d = float(int32_t(rand()) % 11) + 20.0f;
        of<float>(obj, 0x78) = (ffrom(0xC3AA0000) - of<float>(obj, 0x20)) / d;
        of<float>(obj, 0x7C) = pdiv(float(int32_t(rand()) % 11), 100.0f) + ffrom(0xBE99999A);
        of<uint32_t>(obj, 0) = F::kind142_slug_fall;
        if (of<uint8_t>(obj, 0x2E7) != 0xFF) {
            if (mem.at<int16_t>(G::enemies_alive) > 1)
                of<uint32_t>(obj, 0x58) |= 0x100000;
            mem.at<uint16_t>(G::enemies_alive) -= 1;
            mem.at<uint16_t>(G::enemy_count) -= 1;
        }
    }
    if (of<uint8_t>(obj, 0x58) & 1) {
        const uint32_t since = mem.u32(0x51F9D4) - mem.u32(0x54344C);
        if (since > mem.at<uint8_t>(0x543450) && mem.at<uint8_t>(0x543452) == uint8_t(of<int8_t>(obj, 0x2E6)) &&
            mem.at<uint8_t>(0x543448) < mem.at<uint8_t>(0x543449) && !(of<int32_t>(obj, 0x58) < 0) &&
            int32_t(rand()) % 6 == 1) {
            mem.set32(0x54344C, mem.u32(0x51F9D4));
            mem.at<uint8_t>(0x543448) += 1;
            of<uint32_t>(obj, 0x50) = 0;
            of<uint32_t>(obj, 0x54) = 0x9A0;
            of<uint32_t>(obj, 0x58) |= 0x80000000u;
        }
        if (of<int32_t>(obj, 0x58) < 0) {
            const int32_t m = of<int32_t>(obj, 0x54);
            of<int32_t>(obj, 0x54) = m + 1;
            if (m > 0x9BC) {
                sound_cue(0xA01213);
                kind142_spit_spawn(obj);
                of<uint32_t>(obj, 0x58) = (of<uint32_t>(obj, 0x58) & 0x7FFFFFFFu) | 0x40000000u;
            }
        }
        uint32_t f = of<uint32_t>(obj, 0x58);
        if ((f >> 30) & 1) {
            const int32_t m = of<int32_t>(obj, 0x54);
            of<int32_t>(obj, 0x54) = m - 1;
            if (!(m > 0x9A0)) {
                f &= ~0x40000000u;
                of<uint32_t>(obj, 0x54) = 0x971;
            }
        }
        const int32_t steps = of<int32_t>(obj, 0x2EC) + 1;
        of<int32_t>(obj, 0x2EC) = steps;
        if (!(steps > 0x95) && of<float>(obj, 0x20) != ffrom(0xC3B18000))
            of<float>(obj, 0x28) = of<float>(obj, 0x28) - ffrom(0x3DCCCCCD);
        of<uint32_t>(obj, 0x58) = f;
        if ((f & 0xC0000000u) == 0) {
            const int32_t m = of<int32_t>(obj, 0x54);
            of<int32_t>(obj, 0x54) = m + 1;
            if (m > 0x99C)
                of<uint32_t>(obj, 0x54) = 0x971;
        }
        obj_model_draw_colour(obj);
        slug_frame_end(obj);
    }
    const uint32_t m = of<uint32_t>(obj, 0x54);
    if (m == mem.u32(0x543454))
        of<uint32_t>(obj, 0x54) = m + 1;
}

// 0x63120 kind142_slug_fall: a shot spitting slug flies off (velocity, gravity 0.06, turning by its row) to 2 over the
// floor; gone after 120 frames; drawn (obj_model_draw_colour), blinking after 79
template <class P> void Game<P>::kind142_slug_fall(uint32_t obj)
{
    const int32_t t = of<int32_t>(obj, 0x50) + 1;
    of<int32_t>(obj, 0x50) = t;
    if (t > 120) {
        task_exit();
        return;
    }
    float y = of<float>(obj, 0x24);
    uint32_t a = of<uint32_t>(obj, 0x34);
    if (y > mem.f32(G::floor_y) + 2.0f) {
        const float vy = of<float>(obj, 0x7C);
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + of<float>(obj, 0x78);
        of<float>(obj, 0x7C) = vy - ffrom(0x3D75C28F);
        y = y + vy;
        a = of<int8_t>(obj, 0x2E6) > 2 ? a - 0x400 : a + 0x400;
    } else {
        a = 0;
    }
    of<uint32_t>(obj, 0x34) = a;
    of<float>(obj, 0x24) = y;
    const int32_t n = of<int32_t>(obj, 0x50);
    if (!(n > 0x4F) || n % 2 != 0)
        obj_model_draw_colour(obj);
}

// 0x635D0 kind142_spit_spawn: a spit blob (kind142_spit, model 0x9BE, radius 2.2, scale 2.5) two left of the slug,
// aimed at the camera (1/120 of the distance a frame, x off by the random player +-0.015) on an arc (vy + |sum| / 6,
// gravity -|sum| / 315; rows 3-4 four further right; row 5 one right, 2.6 lower, flat, gravity -|sum| / 300)
template <class P> void Game<P>::kind142_spit_spawn(uint32_t obj)
{
    const uint32_t t = task_create(F::kind142_spit, 0x3C8);
    task_clear_work(t);
    of<uint32_t>(t, 0x58) = 0x401;
    of<uint32_t>(t, 0x54) = 0x9BE;
    of<uint32_t>(t, 0x90) = 0x400CCCCD;
    of<uint32_t>(t, 0x94) = 0x40200000;
    of<float>(t, 0x20) = of<float>(obj, 0x20) - 2.0f;
    of<uint32_t>(t, 0x24) = of<uint32_t>(obj, 0x24);
    of<uint32_t>(t, 0x28) = of<uint32_t>(obj, 0x28);
    of<uint32_t>(t, 0x2C) = of<uint32_t>(obj, 0x2C);
    of<uint32_t>(t, 0x30) = of<uint32_t>(obj, 0x30);
    of<uint32_t>(t, 0x34) = of<uint32_t>(obj, 0x34);
    of<uint8_t>(t, 0x2E8) = uint8_t(int32_t(rand()) % 2);
    float vx = mem.f32(G::camera_records) - of<float>(obj, 0x20);
    const float side = float(int32_t(of<int8_t>(t, 0x2E8))) * ffrom(0x3CF5C28F);
    vx = vx / 120.0f;
    vx = vx - ffrom(0x3C75C28F);
    vx = vx + side;
    of<float>(t, 0x78) = vx;
    of<float>(t, 0x7C) = (mem.f32(G::camera_records + 4) - of<float>(obj, 0x24)) / 120.0f;
    const float vz = (mem.f32(G::camera_records + 8) - of<float>(obj, 0x28)) / 120.0f;
    const float vy = of<float>(t, 0x7C);
    float sum = of<float>(t, 0x78) + vy;
    sum = sum + vz;
    if (sum < 0.0f)
        sum = -sum;
    const float up = sum / 6.0f;
    const float g = -sum / ffrom(0x439D8000);
    of<float>(t, 0x80) = vz;
    mem.set32(0x543458, fbits(sum));
    of<float>(t, 0x7C) = up + vy;
    of<float>(t, 0x88) = g;
    const int32_t row = of<int8_t>(obj, 0x2E6);
    if (row > 2 && !(row > 4))
        of<float>(t, 0x20) = of<float>(t, 0x20) + 4.0f;
    if (of<uint8_t>(obj, 0x2E6) == 5) {
        const float g5 = -mem.f32(0x543458) / 300.0f;
        of<uint32_t>(t, 0x7C) = 0;
        of<float>(t, 0x20) = of<float>(t, 0x20) + 1.0f;
        of<float>(t, 0x24) = of<float>(t, 0x24) - ffrom(0x40266666);
        of<float>(t, 0x88) = g5;
    }
    of<uint32_t>(t, 0x30C) = 0;
    of<uint32_t>(t, 0x50) = 0;
}

// 0x63200 kind142_spit: gone after camera frame 940; shot: knocked down (gravity -0.05, vz 1); reaching the camera
// (its z past the eye's in the spit scene) it hits its player (player_hit) and ends; gone at the floor or after 60
// animation steps (the spit count one less); flies; shot it tumbles and blinks; drawn (the draw param's light, T R S,
// models +0x54 + (int)(+0x30C += 0.25) % 30); its camera point, the depth list
template <class P> void Game<P>::kind142_spit(uint32_t obj)
{
    if (mem.f32(pk::camera_frame) > 940.0f) {
        of<uint32_t>(obj, 0x58) &= 0xEFFFFFFEu;
        task_exit();
        return;
    }
    const uint32_t f0 = of<uint32_t>(obj, 0x58);
    if ((f0 & 1) && !((f0 >> 28) & 1) && (f0 & 6)) {
        of<uint32_t>(obj, 0x58) = f0 | 0x10000000;
        of<uint32_t>(obj, 0x88) = 0xBD4CCCCD;
        of<uint32_t>(obj, 0x80) = 0x3F800000;
    }
    if (mem.f32(G::camera_records + 8) > of<float>(obj, 0x28) + 2.0f &&
        int32_t(mem.at<int8_t>(G::scene_number)) == int32_t(mem.at<uint8_t>(0x543451))) {
        if (mem.at<uint16_t>(G::players_in_game_b) == 1)
            player_hit(mem.at<int8_t>(0x51F700), 1, 4);
        else
            player_hit(of<int8_t>(obj, 0x2E8), 1, 4);
        mem.at<uint8_t>(0x543448) -= 1;
        of<uint32_t>(obj, 0x58) &= 0xEFFFFFFEu;
        task_exit();
        return;
    }
    if (!(of<float>(obj, 0x24) > mem.f32(G::floor_y)) || of<float>(obj, 0x30C) > 60.0f) {
        mem.at<uint8_t>(0x543448) -= 1;
        of<uint32_t>(obj, 0x58) &= 0xEFFFFFFEu;
        task_exit();
        return;
    }
    const float vy = of<float>(obj, 0x7C);
    of<float>(obj, 0x20) = of<float>(obj, 0x20) + of<float>(obj, 0x78);
    of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
    of<float>(obj, 0x28) = of<float>(obj, 0x28) + of<float>(obj, 0x80);
    of<float>(obj, 0x7C) = vy + of<float>(obj, 0x88);
    const uint32_t f = of<uint32_t>(obj, 0x58);
    if ((f >> 28) & 1)
        of<uint32_t>(obj, 0x2C) -= 0x400;
    bool draw = true;
    if ((f >> 28) & 1) {
        const int32_t n = of<int32_t>(obj, 0x50) + 1;
        of<int32_t>(obj, 0x50) = n;
        draw = n % 2 == 0;
    }
    if (draw) {
        slug_light(of<int16_t>(obj, 0x6A));
        cam_copro(0x05, {});
        prop_place_xyz(obj);
        prop_scale(of<uint32_t>(obj, 0x94));
        const float a = of<float>(obj, 0x30C) + 0.25f;
        const int32_t k = int32_t(a) % 30;
        of<float>(obj, 0x30C) = a;
        geo_draw_model(uint32_t(k) + of<uint32_t>(obj, 0x54));
        cam_copro(0x06, {});
        prop_light0();
    }
    prop_view_pos(obj, of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28));
    of<uint32_t>(obj, 0x58) &= 0xFFFFFFF9u;
    depth_list_add(obj);
}

// 0x64B10 kind135_splash: models 0x320..0x333 one per frame (then gone), turned y, scale 1
template <class P> void Game<P>::kind135_splash(uint32_t obj)
{
    const uint32_t n = of<uint32_t>(obj, 0x50);
    of<uint32_t>(obj, 0x50) = n + 1;
    of<uint32_t>(obj, 0x54) = n + 0x320;
    if (int32_t(n + 0x320) > 0x333) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    prop_scale(0x3F800000);
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

} // namespace hotd::port
