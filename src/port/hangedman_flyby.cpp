// The Hangedman's fly-by in Chapter 1 event 3 (kind 18 set-up 0: not a fight) and kind 17, the researcher it drops
// (event 3's ENTB2), teaser L5. Read from the arcade disassembly; arcade forms only. The fly-by states drive event
// flags 0xB3..0xB6 (0x51E9F3..), which kind 17 waits for; kind 17 rides the Hangedman (hangedman_obj) while it
// collapses. A skipped scene (evt_skip_end) puts both and the camera at the scene's end.
#include "port/game.h"

namespace hotd::port {

// 0x4EE70 (kind 18): the set-up by action (+0x64; +0x70 when spawned with a record tail at +0x35C)
template <class P> void Game<P>::boss_hangedman(uint32_t obj)
{
    const int32_t i = of<uint32_t>(obj, 0x35C) == 0 ? of<int16_t>(obj, 0x64) : of<int16_t>(obj, 0x70);
    const uint32_t r = mem.u32(G::hangedman_setups + uint32_t(i) * 4);
    if (r == F::hangedman_flyby_init)
        hangedman_flyby_init(obj);
    else
        call_routine(r, obj);
}

// 0x4EEA0 (state 0)
template <class P> void Game<P>::hangedman_idle(uint32_t obj) {}

// 0x4EEB0 (set-up 0): the body (MO 22), counted as an enemy, 40 units right of / above / in front of the camera,
// facing it; hangedman_flyby_frame, state 1
template <class P> void Game<P>::hangedman_flyby_init(uint32_t obj)
{
    const int32_t type = of<int16_t>(obj, 0xCC);
    mem.set32(G::hangedman_obj, obj);
    mem.at<int32_t>(G::hit_type) = type;
    body_init(obj, type, 22);
    creature_type_setup(obj);
    mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
    of<int8_t>(obj, 0x2E7) = -1;
    of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~4u);
    enemy_slot_take(obj);
    of<float>(obj, 0x20) = mem.f32(G::camera_pos) + 40.0f;
    of<float>(obj, 0x24) = mem.f32(G::floor_y) + 40.0f;
    of<int16_t>(obj, 0x2E0) = 0xFF;
    of<float>(obj, 0x28) = mem.f32(G::camera_pos + 8) - 40.0f;
    of<uint32_t>(obj, 0x30) = yaw_toward_a(mem.f32(G::camera_pos) - of<float>(obj, 0x20),
                                           mem.f32(G::camera_pos + 4) - of<float>(obj, 0x24),
                                           mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28));
    of<uint32_t>(obj, 0) = F::hangedman_flyby_frame;
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 1;
}

// 0x4F000: bobs 0.1 a frame (up while +0x54 > 0x1432), the state routine (hangedman_states), drawn, both motion
// frames (unless flags bit 26), the attacker list
template <class P> void Game<P>::hangedman_flyby_frame(uint32_t obj)
{
    if (of<int32_t>(obj, 0x54) > 0x1432)
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + 0.1f;
    else
        of<float>(obj, 0x24) = of<float>(obj, 0x24) - 0.1f;
    const uint32_t r = mem.u32(G::hangedman_states + uint32_t(int32_t(of<int16_t>(obj, 0x5C))) * 4);
    switch (r) {
    case F::hangedman_idle: hangedman_idle(obj); break;
    case F::hangedman_flyby_start: hangedman_flyby_start(obj); break;
    case F::hangedman_flyby: hangedman_flyby(obj); break;
    default: call_routine(r, obj); break;
    }
    object_draw(obj);
    if (((of<uint32_t>(obj, 0x58) >> 26) & 1) == 0) {
        of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
        of<int32_t>(obj, 0x10C) = of<int32_t>(obj, 0x10C) + 1;
    }
    attacker_list_add(obj);
}

// 0x4F080 (state 1): the wing sounds, state 2
template <class P> void Game<P>::hangedman_flyby_start(uint32_t obj)
{
    const int16_t sub = of<int16_t>(obj, 0x60);
    if (sub == 0) {
        sound_cue(0xA0134F);
        sound_cue(0xA01239);
    } else if (sub != 1) {
        return;
    }
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 2;
}

// the fly-by's moves: the unit vector of d (coprocessor 0x0C) times `speed` added to the position and kept as the
// velocity
template <class P> void Game<P>::hangedman_flyby_move(uint32_t obj, const float d[3], float speed)
{
    const Tgp::Out o = boss_unit(fbits(d[0]), fbits(d[1]), fbits(d[2]));   // PC Mode: the PC's inline normalise
    const float vx = ffrom(o.w[0]) * speed, vy = ffrom(o.w[1]) * speed, vz = ffrom(o.w[2]) * speed;
    of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
    of<float>(obj, 0x28) = of<float>(obj, 0x28) + vz;
    of<float>(obj, 0x7C) = vy;
    of<float>(obj, 0x80) = vz;
    of<float>(obj, 0x78) = vx;
    of<float>(obj, 0x20) = of<float>(obj, 0x20) + vx;
}

// 0x4F0C0 (state 2), steps falling through: 0 to the camera (3 in front) at 1.5, facing it; there (x / z within 1.5):
// event flag 0xB3, sound, voice message 6; 1 to (camera + (-80, 40, 20)) at 1.5, turning toward the camera; 2 event
// flag 0xB5; 3 / 4 275 frames (voice message 8 at 0xD7 left), sound 0xA01263; 5 turns 0x800 a frame toward the point
// (-5, camera y + 180, 100); 6 event flag 0xB6, sound, scene_state 1 (unless 2); 7 flies there at 3.75; 8 event flag
// 0xB4, off the enemy counts and its attack slot, removed
template <class P> void Game<P>::hangedman_flyby(uint32_t obj)
{
    const float speed1 = 1.5f, speed2 = 3.75f;
    switch (of<int16_t>(obj, 0x60)) {
    case 0: {
        const float cx = mem.f32(G::camera_pos), cy = mem.f32(G::camera_pos + 4), cz = mem.f32(G::camera_pos + 8);
        const float d[3] = {cx - of<float>(obj, 0x20), cy - of<float>(obj, 0x24), (cz + 3.0f) - of<float>(obj, 0x28)};
        const Tgp::Out o = boss_unit(fbits(d[0]), fbits(d[1]), fbits(d[2]));
        of<uint32_t>(obj, 0x78) = o.w[0];
        of<uint32_t>(obj, 0x7C) = o.w[1];
        of<uint32_t>(obj, 0x80) = o.w[2];
        of<uint32_t>(obj, 0x30) = yaw_toward_a(d[0], d[1], d[2]);
        const float vx = of<float>(obj, 0x78) * speed1, vy = of<float>(obj, 0x7C) * speed1;
        const float vz = of<float>(obj, 0x80) * speed1;
        of<float>(obj, 0x7C) = vy;
        of<float>(obj, 0x80) = vz;
        of<float>(obj, 0x78) = vx;
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + vx;
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
        of<float>(obj, 0x28) = of<float>(obj, 0x28) + vz;
        if (std::fabs(d[0]) > speed1 || !(std::fabs(d[2]) <= speed1))
            return;
        mem.at<uint8_t>(G::event_flags + 0xB3) = 1;
        sound_cue(0xA0134F);
        voice_message(6);
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    }
        [[fallthrough]];
    case 1: {
        of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 1;
        const float cx = mem.f32(G::camera_pos), cy = mem.f32(G::camera_pos + 4), cz = mem.f32(G::camera_pos + 8);
        const float d[3] = {(cx - 80.0f) - of<float>(obj, 0x20), (cy + 40.0f) - of<float>(obj, 0x24),
                            (cz + 20.0f) - of<float>(obj, 0x28)};
        hangedman_flyby_move(obj, d, speed1);
        const uint32_t yaw = yaw_toward_a(mem.f32(G::camera_pos) - of<float>(obj, 0x20),
                                          mem.f32(G::camera_pos + 4) - of<float>(obj, 0x24),
                                          mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28));
        int32_t a = of<int32_t>(obj, 0x30);
        if (a > int32_t(yaw))
            a -= 0x200;
        of<int32_t>(obj, 0x30) = a;
        if (std::fabs(d[0]) > speed1 || std::fabs(d[1]) > speed1 || !(std::fabs(d[2]) <= speed1))
            return;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    }
        [[fallthrough]];
    case 2: {
        const uint32_t e[3] = {fbits(mem.f32(G::camera_pos) - of<float>(obj, 0x20)),
                               fbits(mem.f32(G::camera_pos + 4) - of<float>(obj, 0x24)),
                               fbits(mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28))};
        Tgp::Out o;
        if (!pm_boss())
            copro(0x0F, e, 3, o);   // the result is not used
        mem.at<uint8_t>(G::event_flags + 0xB5) = 1;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    }
        [[fallthrough]];
    case 3:
        of<int32_t>(obj, 0x31C) = 0x113;
        of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 4;
        sound_cue(0xA01263);
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        [[fallthrough]];
    case 4: {
        of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 1;
        if (of<int32_t>(obj, 0x31C) == 0xD7)
            voice_message(8);
        const int32_t n = of<int32_t>(obj, 0x31C) - 1;
        of<int32_t>(obj, 0x31C) = n;
        if (n > 0)
            return;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    }
        [[fallthrough]];
    case 5: {
        const float dx = float(ffrom64(0xC014000000000000ull) - double(of<float>(obj, 0x20)));
        of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 4;
        const float dz = float(100.0 - double(of<float>(obj, 0x28)));
        const float dy = (mem.f32(G::camera_pos + 4) + 180.0f) - of<float>(obj, 0x24);
        const uint32_t yaw = yaw_toward_a(dx, dy, dz);
        const int32_t a = of<int32_t>(obj, 0x30) + 0x800;
        of<int32_t>(obj, 0x30) = a;
        if (a < int32_t(yaw))
            return;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    }
        [[fallthrough]];
    case 6:
        mem.at<uint8_t>(G::event_flags + 0xB6) = 1;
        sound_cue(0xA0134F);
        if (mem.at<uint8_t>(G::scene_state) != 2)
            mem.at<uint8_t>(G::scene_state) = 1;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        [[fallthrough]];
    case 7: {
        of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 3;
        const float dx = float(ffrom64(0xC014000000000000ull) - double(of<float>(obj, 0x20)));
        const float dz = float(100.0 - double(of<float>(obj, 0x28)));
        const float dy = (mem.f32(G::camera_pos + 4) + 180.0f) - of<float>(obj, 0x24);
        const Tgp::Out o = boss_unit(fbits(dx), fbits(dy), fbits(dz));
        of<uint32_t>(obj, 0x78) = o.w[0];
        of<uint32_t>(obj, 0x7C) = o.w[1];
        of<uint32_t>(obj, 0x80) = o.w[2];
        of<uint32_t>(obj, 0x30) = yaw_toward_a(dx, dy, dz);
        const float vx = of<float>(obj, 0x78) * speed2, vy = of<float>(obj, 0x7C) * speed2;
        const float vz = of<float>(obj, 0x80) * speed2;
        of<float>(obj, 0x7C) = vy;
        of<float>(obj, 0x80) = vz;
        of<float>(obj, 0x78) = vx;
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + vx;
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
        of<float>(obj, 0x28) = of<float>(obj, 0x28) + vz;
        if (std::fabs(dx) > speed2 || std::fabs(dy) > speed2 || !(std::fabs(dz) <= speed2))
            return;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    }
        [[fallthrough]];
    case 8:
        mem.at<uint8_t>(G::event_flags + 0xB4) = 1;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
        mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 20) = 0;
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
        body_free_parts(obj);
        task_exit();
        return;
    default: return;
    }
}

// --- kind 17: the researcher the Hangedman drops (event 3) ---

// 0x95710: the plain model 0x621 at (10, floor, -619) facing back, type 34, kind17_researcher_frame, state 1
template <class P> void Game<P>::kind17_kenkyu_researcher_collapse(uint32_t obj)
{
    of<float>(obj, 0x20) = 10.0f;
    of<uint32_t>(obj, 0x24) = mem.u32(G::floor_y);
    of<uint32_t>(obj, 0x28) = 0xC41AC000;
    of<int32_t>(obj, 0x30) = 0x8000;
    of<int16_t>(obj, 0x2E0) = 0xFF;
    of<int16_t>(obj, 0xCC) = 34;
    of<uint32_t>(obj, 0x54) = 0x621;
    of<uint32_t>(obj, 0) = F::kind17_researcher_frame;
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 1;
    // fix141 (GitHub #81: "Sophie's body is not visible" by the fountain, "Sophie!?", with the PC models): the 1998 PC
    // files have no 0x621 (sofi_d), and THOTD.EXE never draws it: its kind17_kenkyu_researcher_collapse (0x50DDC4)
    // sets the body up at once (body_init MO 0x186, creature_type_setup), its motion held (flags bit 26) until state 1
    // lets it go, and kind17_researcher_frame (0x50DE70) always draws the body. The PC's way with the PC models (as
    // kind 147's #52) and in PC Mode; the arcade's own games keep the plain model. mixmodels (#49): Models: Mixed
    // takes the Players and Humans row's way (she is drawn with that row's models)
    if (pc_path() || models_pc_humans()) {
        const int32_t type = of<int16_t>(obj, 0xCC);
        mem.at<int32_t>(G::hit_type) = type;
        body_init(obj, type, 0x186);
        creature_type_setup(obj);
        of<int32_t>(obj, 0xF8) = 0;
        of<int32_t>(obj, 0x10C) = 0;
        of<int32_t>(obj, 0x30) = 0x8000;
        of<int8_t>(obj, 0x2E7) = -1;
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~4u);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | (1u << 26);
    }
}

// 0x95770: a skipped scene: gakkuri from frame 48, event flags 0xB2 / 0xB3, state 3 step 3, the camera and the
// Hangedman put at the scene's end; the state routine (kind17_states); drawn (the plain model until the body exists,
// flags bit 25; nothing with bit 29); both motion frames unless bit 26; the attacker list while event flag 0xB0
template <class P> void Game<P>::kind17_researcher_frame(uint32_t obj)
{
    const uint32_t hm = mem.u32(G::hangedman_obj);
    if (mem.u32(G::evt_skip_end) != 0) {
        set_motion_blend(obj, 0xDC, 48, 1);
        mem.at<uint8_t>(G::event_flags + 0xB2) = 1;
        mem.at<uint8_t>(G::event_flags + 0xB3) = 1;
        of<int16_t>(obj, 0x5C) = 3;
        of<int16_t>(obj, 0x60) = 3;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xDBFFFFFFu;
        sound_cue(0xBE5000);
        mem.set32(G::camera_pos + 8, 0xC4258BB5);
        mem.set32(G::camera_pos, 0x40BD7660);
        mem.set32(G::camera_pos + 4, 0xC1200000);
        of<float>(hm, 0x20) = ffrom(0x40BD7660) - 80.0f;
        of<float>(hm, 0x24) = mem.f32(G::camera_pos + 4) + 40.0f;
        of<int16_t>(hm, 0x5C) = 2;
        of<int16_t>(hm, 0x60) = 5;
        of<float>(hm, 0x28) = mem.f32(G::camera_pos + 8) + 20.0f;
    }
    const uint32_t r = mem.u32(G::kind17_states + uint32_t(int32_t(of<int16_t>(obj, 0x5C))) * 4);
    switch (r) {
    case F::kind17_researcher_st0_none: kind17_researcher_st0_none(obj); break;
    case F::kind17_researcher_st1_appear: kind17_researcher_st1_appear(obj); break;
    case F::kind17_researcher_st2_run: kind17_researcher_st2_run(obj); break;
    case F::kind17_researcher_st3_collapse: kind17_researcher_st3_collapse(obj); break;
    default: call_routine(r, obj); break;
    }
    const uint32_t f = of<uint32_t>(obj, 0x58);
    if (pc_path() || models_pc_humans()) {   // (fix141 #81: THOTD.EXE's: the body, from the start)
        if (((f >> 29) & 1) == 0)
            object_draw(obj);
    } else if (((f >> 25) & 1) == 0)
        kind17_researcher_model_draw(obj);
    else if (((f >> 29) & 1) == 0)
        object_draw(obj);
    if (((of<uint32_t>(obj, 0x58) >> 26) & 1) == 0) {
        of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
        of<int32_t>(obj, 0x10C) = of<int32_t>(obj, 0x10C) + 1;
    }
    if (mem.at<uint8_t>(G::event_flags + 0xB0) == 1)
        attacker_list_add(obj);
}

// 0x958C0 (state 0)
template <class P> void Game<P>::kind17_researcher_st0_none(uint32_t obj) {}

// 0x958D0 (state 1): at event flag 0xB0 the body (MO 0x186), counted as an enemy, facing back, an enemy slot; state 2
template <class P> void Game<P>::kind17_researcher_st1_appear(uint32_t obj)
{
    if (of<int16_t>(obj, 0x60) != 0 || mem.at<uint8_t>(G::event_flags + 0xB0) != 1)
        return;
    if (pc_path() || models_pc_humans()) {   // (fix141 #81: THOTD.EXE's 0x50DF78: the body is there already; its motion goes on)
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~(1u << 26);
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
        mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
        enemy_slot_take(obj);
        of<int16_t>(obj, 0x5C) = 2;
        of<int16_t>(obj, 0x60) = 0;
        return;
    }
    const int32_t type = of<int16_t>(obj, 0xCC);
    mem.at<int32_t>(G::hit_type) = type;
    body_init(obj, type, 0x186);
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | (1u << 25);
    creature_type_setup(obj);
    mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
    of<int32_t>(obj, 0x30) = 0x8000;
    of<int8_t>(obj, 0x2E7) = -1;
    of<int32_t>(obj, 0xF8) = 0;
    of<int32_t>(obj, 0x10C) = 0;
    of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) & ~4u);
    enemy_slot_take(obj);
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 2;
}

// 0x95980 (state 2), steps falling through: 0 / 1 once the motion ends: on the floor, MO 0xFF, event flag 0xB1;
// 2 faces the camera; within 30 in z: event flag 0xB2, not shootable, off its attack slot, 15 frames; 3 then
// hidden (flags bit 29); event flag 0xB3: state 3
template <class P> void Game<P>::kind17_researcher_st2_run(uint32_t obj)
{
    switch (of<int16_t>(obj, 0x60)) {
    case 0:
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        [[fallthrough]];
    case 1:
        if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) == 0)
            return;
        of<float>(obj, 0x24) = mem.f32(G::floor_y) - ffrom(0x3F8CCCCD);
        set_motion_blend(obj, 0xFF, 0, 4);
        mem.at<uint8_t>(G::event_flags + 0xB1) = 1;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        [[fallthrough]];
    case 2: {
        const float cz = mem.f32(G::camera_pos + 8);
        of<uint32_t>(obj, 0x30) = yaw_toward_a(mem.f32(G::camera_pos) - of<float>(obj, 0x20),
                                               mem.f32(G::camera_pos + 4) - of<float>(obj, 0x24),
                                               cz - of<float>(obj, 0x28));
        of<uint8_t>(obj, 0x3C4) = uint8_t(of<uint8_t>(obj, 0x3C4) | 0x80);
        if (std::fabs(mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28)) > 30.0f)
            return;
        mem.at<uint8_t>(G::event_flags + 0xB2) = 1;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
        mem.at<uint8_t>(G::attack_slots + uint32_t(int32_t(of<int8_t>(obj, 0x2E7))) * 20) = 0;
        of<int32_t>(obj, 0x2EC) = 15;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
    }
        [[fallthrough]];
    case 3: {
        int32_t c = of<int32_t>(obj, 0x2EC) - 1;
        if (c < 0) {
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | (1u << 29);
            c = 0;
        }
        of<int32_t>(obj, 0x2EC) = c;
        if (mem.at<uint8_t>(G::event_flags + 0xB3) == 1) {
            of<int16_t>(obj, 0x60) = 0;
            of<int16_t>(obj, 0x5C) = 3;
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~(1u << 29);
        }
        return;
    }
    default: return;
    }
}

// 0x95B50 (state 3), steps falling through: 0 gakkuri (sounds 0xA0132B / 0xA0134F); 1 held at frame 49 (bit 26)
// until event flag 0xB5: gakkuri again; 3 held again until 0xB6; 4 at 0xB4 off the enemy counts, removed; each frame
// carried 6 up / 7 ahead in the Hangedman's frame, turned a quarter from it
template <class P> void Game<P>::kind17_researcher_st3_collapse(uint32_t obj)
{
    const uint32_t hm = mem.u32(G::hangedman_obj);
    switch (of<int16_t>(obj, 0x60)) {
    case 0:
        set_motion(obj, 0xDC);
        sound_cue(0xA0132B);
        sound_cue(0xA0134F);
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        [[fallthrough]];
    case 1:
        if (of<int16_t>(obj, 0xF4) <= 48)
            break;
        of<int16_t>(obj, 0xF4) = 49;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | (1u << 26);
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        [[fallthrough]];
    case 2:
        if (mem.at<uint8_t>(G::event_flags + 0xB5) != 1)
            break;
        set_motion(obj, 0xDC);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~(1u << 26);
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        [[fallthrough]];
    case 3:
        if (of<int16_t>(obj, 0xF4) <= 48)
            break;
        of<int16_t>(obj, 0xF4) = 49;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | (1u << 26);
        if (mem.at<uint8_t>(G::event_flags + 0xB6) != 1)
            break;
        of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        [[fallthrough]];
    case 4:
        if (mem.at<uint8_t>(G::event_flags + 0xB4) != 1)
            break;
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
        body_free_parts(obj);
        task_exit();
        return;
    default: break;
    }
    if (pm_boss()) {   // PC 0x50E271: the same carry on the kind 17 file's helpers (0x50C230 ..)
        pm::Matrix m;
        m.e[9] = of<float>(hm, 0x20);
        m.e[10] = of<float>(hm, 0x24);
        m.e[11] = of<float>(hm, 0x28);
        m.rotate_z(of<uint32_t>(hm, 0x34));
        m.rotate_y(of<uint32_t>(hm, 0x30));
        m.rotate_x(of<uint32_t>(hm, 0x2C));
        float p[3];
        m.transform(0.0f, 6.0f, 7.0f, p[0], p[1], p[2]);
        of<float>(obj, 0x20) = p[0];
        of<float>(obj, 0x24) = p[1];
        of<float>(obj, 0x28) = p[2];
        of<int32_t>(obj, 0x30) = of<int32_t>(hm, 0x30) - 0x4000;
        return;
    }
    Tgp::Out none, o;
    copro(0x21, nullptr, 0, none);
    const uint32_t t[3] = {of<uint32_t>(hm, 0x20), of<uint32_t>(hm, 0x24), of<uint32_t>(hm, 0x28)};
    copro(0x88, t, 3, none);
    const uint32_t rz[1] = {of<uint32_t>(hm, 0x34)};
    copro(0x16, rz, 1, none);
    const uint32_t ry[1] = {of<uint32_t>(hm, 0x30)};
    copro(0x15, ry, 1, none);
    const uint32_t rx[1] = {of<uint32_t>(hm, 0x2C)};
    copro(0x14, rx, 1, none);
    const uint32_t p[3] = {0, fbits(6.0f), fbits(7.0f)};
    copro(0x1A, p, 3, o);
    of<uint32_t>(obj, 0x20) = o.w[0];
    of<uint32_t>(obj, 0x24) = o.w[1];
    copro(0x06, nullptr, 0, none);
    of<int32_t>(obj, 0x30) = of<int32_t>(hm, 0x30) - 0x4000;
    of<uint32_t>(obj, 0x28) = o.w[2];
}

// 0x95D70: the plain model (+0x54) with its fade (+0x2E0), the shadow and light 3; its place becomes the home position
template <class P> void Game<P>::kind17_researcher_model_draw(uint32_t obj)
{
    shadow_setup(mem.f32(G::floor_y));
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
    const uint32_t s[3] = {fbits(1.0f), fbits(1.0f), fbits(1.0f)};
    copro(0x13, s, 3, none);
    draw_fade_begin(of<int16_t>(obj, 0x2E0));
    geo_matrix_position(of<uint32_t>(obj, 0x54), obj + 0x38);
    const uint32_t l[1] = {3};
    copro(0x77, l, 1, none);
    draw_fade_begin(0);
    geo_draw_model(of<uint32_t>(obj, 0x54));
    geo_send_words(0, 31, 0x7A0);
    copro(0x06, nullptr, 0, none);
    of<uint32_t>(obj, 0x2D4) = of<uint32_t>(obj, 0x20);
    of<uint32_t>(obj, 0x2D8) = of<uint32_t>(obj, 0x24);
    of<uint32_t>(obj, 0x2DC) = of<uint32_t>(obj, 0x28);
}

} // namespace hotd::port
