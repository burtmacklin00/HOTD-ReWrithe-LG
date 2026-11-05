// Kind 102 (the enemy that waits for the camera, then walks in), guard action 6 (attacking a researcher), kind 101 (a
// lying corpse), the turn into a motion and the voice messages. First written as runtime overrides (T1,
// src/game/t1_bounds.cpp), read from the disassembly. Arcade forms (decl/support_b.h).
#include "port/game.h"

#include <cmath>

namespace hotd::port {

namespace {
constexpr uint32_t kEnemySlots = 0x51FB40;      // the enemy slots (0x2C each, by obj +0x2E6): +0x28 flag byte (arcade)
constexpr uint32_t kAttackSlots = 0x51F9F0;     // the attack positions (0x14 each, by obj +0x2E7) (arcade)
constexpr uint32_t kCameraRecords = 0x51EA50;   // camera records (0x154 each, by 0x51F620) (arcade)
} // namespace

// 0x55B30 kind102_enemy_wait_camera (event 2): waits until the point 80 ahead of the camera (record's angles) is within
// 12 in z; then body_init (motion 0x1E4), its enemy-slot flags cleared, creature_type_setup, counted (enemy_count,
// enemies_alive), an attack position, anim_time 15, kind102_enemy_walk_in
template <class P> void Game<P>::kind102_enemy_wait_camera(uint32_t obj)
{
    const uint32_t rec = kCameraRecords + mem.u32(0x51F620) * 0x154;
    Tgp::Out none, o;
    copro(0x21, nullptr, 0, none);
    uint32_t a[3] = {mem.u32(rec + 0x74), mem.u32(rec + 0x70), mem.u32(rec + 0x6C)};
    copro(0x86, a, 3, none);
    uint32_t v[3] = {0, 0, fbits(80.0f)};
    copro(0x1A, v, 3, o);
    const float d = std::fabs((mem.f32(rec + 0x68) + ffrom(o.w[2])) - mem.f32(obj + 0x28));
    copro(0x06, nullptr, 0, none);
    if (d >= 12.0f)
        return;
    mem.set32(G::hit_type, uint32_t(int32_t(mem.at<int16_t>(obj + 0xCC))));
    body_init(obj, int32_t(mem.u32(G::hit_type)), 0x1E4);
    auto s = [&] { return kEnemySlots + uint32_t(int32_t(mem.at<int8_t>(obj + 0x2E6))) * 0x2C; };
    mem.set32(obj + 0x348, 0);
    mem.at<uint8_t>(obj + 0x3C4) |= 0x80;
    mem.at<uint8_t>(s() + 0x28) &= 0xFE;
    mem.set32(s() + 4, 0);
    mem.set32(s() + 0x1C, 0);
    mem.at<uint8_t>(s() + 0x28) &= 0xFD;
    mem.at<uint8_t>(s() + 0x28) &= 0xFB;
    creature_type_setup(obj);
    mem.at<int16_t>(G::enemy_count) = int16_t(mem.at<int16_t>(G::enemy_count) + 1);
    mem.at<int16_t>(G::enemies_alive) = int16_t(mem.at<int16_t>(G::enemies_alive) + 1);
    enemy_slot_take(obj);
    mem.set32(obj + 0xF8, 0xF);
    mem.set32(obj, F::kind102_enemy_walk_in);
}

// 0x563E0 kind102_enemy_walk_in: walks 0.8 a frame until its motion loops once; then enemy_main, state 1, sub-state
// 2, action 0, walk variant 1, the motion of the type's list by the sub-state
template <class P> void Game<P>::kind102_enemy_walk_in(uint32_t obj)
{
    if (mem.at<int16_t>(obj + 0xF6) == 1) {
        mem.set32(obj, F::enemy_main);
        mem.at<int16_t>(obj + 0x5C) = 1;
        mem.at<int16_t>(obj + 0x60) = 2;
        mem.at<int16_t>(obj + 0x64) = 0;
        mem.set32(obj + 0x330, 1);
        const uint32_t list = mem.u32(mem.u32(G::enemy_motion_lists + uint32_t(int32_t(mem.at<int16_t>(obj + 0xCC))) * 4));
        set_motion(obj, mem.u32(list + uint32_t(int32_t(mem.at<int16_t>(obj + 0x60))) * 8));
        enemy_target_distance(obj);
    }
    move_along_heading(obj, ffrom(0x3F4CCCCDu));
    mem.set32(obj + 0xF8, mem.u32(obj + 0xF8) + 1);
    body_motion2_frame_flags(obj);
}

// 0x56360 enemy_act6_frame (guard action 6, attacking a researcher): scripted hits; while shootable and its loop count
// (+0xF6) is not negative: no longer shootable, its attack position freed, enemies_alive - 1, enemy_act6_bite
template <class P> void Game<P>::enemy_act6_frame(uint32_t obj)
{
    enemy_frame_begin(obj);
    enemy_scripted_hit(obj);
    if (!(mem.at<uint8_t>(obj + 0x58) & 1))
        return;
    if (mem.at<int16_t>(obj + 0xF6) >= 0) {
        mem.set32(obj + 0x58, mem.u32(obj + 0x58) & ~1u);
        mem.at<uint8_t>(kAttackSlots + uint32_t(int32_t(mem.at<int8_t>(obj + 0x2E7))) * 0x14) = 0;
        mem.at<int8_t>(obj + 0x2E7) = -1;
        mem.at<int16_t>(G::enemies_alive) = int16_t(mem.at<int16_t>(G::enemies_alive) - 1);
        mem.set32(obj + 0x50, 0);
        mem.set32(obj, F::enemy_act6_bite);
    }
    enemy_frame_end(obj);
    depth_list_add(obj);
}

// 0x56320 enemy_act6_bite: at loop 3 no killer, enemy_scripted_dead
template <class P> void Game<P>::enemy_act6_bite(uint32_t obj)
{
    enemy_frame_begin(obj);
    if (mem.at<int16_t>(obj + 0xF6) == 3) {
        mem.at<int8_t>(obj + 0x2EB) = -1;
        mem.set32(obj, F::enemy_scripted_dead);
    }
    enemy_frame_end(obj);
    depth_list_add(obj);
}

// 0x5BE60 kind101_corpse: a lying corpse (+0x64: 0x5E6 / 0x5E8 / 0x5E4, the blood-colour versions 0x5E7 / 0x5E9 /
// 0x5E5; type 14: 0x5E4 turned another 0x6000) at its place and heading; gone once the camera frame reaches +0x70
template <class P> void Game<P>::kind101_corpse(uint32_t obj)
{
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(obj + 0x20), mem.u32(obj + 0x24), mem.u32(obj + 0x28)};
    copro(0x12, p, 3, none);
    uint32_t ry[1] = {mem.u32(obj + 0x30)};
    copro(0x15, ry, 1, none);
    const int16_t v = mem.at<int16_t>(obj + 0x64);
    if (mem.at<int16_t>(obj + 0xCC) == 14) {
        uint32_t r2[1] = {0x6000};
        copro(0x15, r2, 1, none);
        geo_draw_model(0x5E4);
    } else if (mem.at<uint8_t>(G::blood_color) == 0) {
        if (v == 0) geo_draw_model(0x5E6);
        else if (v == 1) geo_draw_model(0x5E8);
        else if (v == 2) geo_draw_model(0x5E4);
    } else {
        if (v == 0) geo_draw_model(0x5E7);
        else if (v == 1) geo_draw_model(0x5E9);
        else if (v == 2) geo_draw_model(0x5E5);
    }
    const float limit = float(mem.at<int16_t>(obj + 0x70));
    copro(0x06, nullptr, 0, none);
    if (!(limit > mem.f32(0x51EB9C)))
        task_exit();
}

// 0x5C0A0 kind118_corpse: kind 101's corpse kept for one scene: +0x70 = scene_number, then kind118_corpse_frame. In
// Revision A no script spawns it (its four records 0xF0624 / 0xF0654 / 0xFE310 / 0xFE340 are referenced by nothing);
// the PC's prologue does (0xFE310 / 0xFE340, the sewer): restored under Attract Gore (event.cpp evt_attract_restore)
template <class P> void Game<P>::kind118_corpse(uint32_t obj)
{
    mem.set32(obj, F::kind118_corpse_frame);
    mem.at<uint16_t>(obj + 0x70) = uint16_t(int16_t(mem.at<int8_t>(G::scene_number)));
}

// 0x5BF80 kind118_corpse_frame: drawn as kind101_corpse; gone once scene_number is no longer +0x70
template <class P> void Game<P>::kind118_corpse_frame(uint32_t obj)
{
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(obj + 0x20), mem.u32(obj + 0x24), mem.u32(obj + 0x28)};
    copro(0x12, p, 3, none);
    uint32_t ry[1] = {mem.u32(obj + 0x30)};
    copro(0x15, ry, 1, none);
    const int16_t v = mem.at<int16_t>(obj + 0x64);
    if (mem.at<int16_t>(obj + 0xCC) == 14) {
        uint32_t r2[1] = {0x6000};
        copro(0x15, r2, 1, none);
        geo_draw_model(0x5E4);
    } else if (mem.at<uint8_t>(G::blood_color) == 0) {
        if (v == 0) geo_draw_model(0x5E6);
        else if (v == 1) geo_draw_model(0x5E8);
        else if (v == 2) geo_draw_model(0x5E4);
    } else {
        if (v == 0) geo_draw_model(0x5E7);
        else if (v == 1) geo_draw_model(0x5E9);
        else if (v == 2) geo_draw_model(0x5E5);
    }
    const uint16_t scene = uint16_t(int16_t(mem.at<int8_t>(G::scene_number)));
    const uint16_t kept = mem.at<uint16_t>(obj + 0x70);
    copro(0x06, nullptr, 0, none);
    if (kept != scene)
        task_exit();
}

// 0x387B0 turn_motion_set(obj, motion, a, frames): the heading of the body's anchor matrix (+0x184, through stored
// matrix 1, turned z +0x4000 / y -0x4000; coprocessor 0x6F) less the object's heading is spread over the frames as
// the turn rate (+0xE8) of a set_motion_blend
template <class P> void Game<P>::turn_motion_set(uint32_t obj, uint32_t motion, int a, int frames)
{
    if constexpr (P::pc) {
        (void)obj, (void)motion, (void)a, (void)frames;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        Tgp::Out none, ang;
        uint32_t one[1] = {1};
        copro(0x74, one, 1, none);
        uint32_t m[12];
        for (uint32_t k = 0; k < 12; ++k)
            m[k] = mem.u32(obj + 0x184 + 4 * k);
        copro(0x09, m, 12, none);
        uint32_t z[1] = {0x4000};
        copro(0x16, z, 1, none);
        uint32_t y[1] = {0xFFFFC000u};
        copro(0x15, y, 1, none);
        copro(0x6F, nullptr, 0, ang);
        copro(0x06, nullptr, 0, none);
        const int32_t d = int16_t(uint16_t(ang.w[0] & 0xFFFF) - mem.at<uint16_t>(obj + 0x30));
        set_motion_blend(obj, motion, a, frames);
        mem.set32(obj + 0xE8, uint32_t(frames != 0 ? d / frames : d));
    }
}

// 0x311C0 voice_message(n): a voice_message_task (0x9B380) for message n (+0x70 / +0x72)
template <class P> void Game<P>::voice_message(int n)
{
    if constexpr (P::pc) {
        (void)n;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const uint32_t t = task_create(0x9B380, 0x3C8);
        task_clear_work(t);
        mem.at<int16_t>(t + 0x72) = int16_t(n);
        mem.at<int16_t>(t + 0x70) = int16_t(n);
    }
}

} // namespace hotd::port
