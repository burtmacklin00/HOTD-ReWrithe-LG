// Kind 20, the Magician (Chapter 4's boss, event 4), worker "hermit": set-up 0 (the intro with Dr. Curien) and set-up 1
// (the fight: descent and title, the attack pattern table 0xC69C0 of fireball volleys, swoops and the barrage, hit
// reactions by hit-point shares, the death with its flash and palette fades), his projectiles (fireballs, barrage
// shots, the barrage's homing helper), afterimages, trackers and the part effects; the palette fades and body-part
// searches they reach. Read from the arcade disassembly 0x70AB0..0x75610 (tools/i960dis.py); the typed decompilation
// was only a guide (its coprocessor sequences drop the translate / rotate steps and two routines, 0x74150 and 0x74C70,
// are not in its index). Arcade forms only; proven on A but 0x70AD0 / 0x73820 (decl/magician.h).
//
// The Magician (+0x35C the spawn record: +0 hit points, +2 damage a hit, +4 creature type, +7 / +0xB / +0xD / +0x11 /
// +0x12 / +0x13 / +0x14 / +0x15 signed counts, +8 / +0xC / +0x18.. floats: speeds, scales, the death timings +0x90..
// +0xAC), +0x5C state (table 0xC6990), +0x60 step, +0x2E0 fade, +0x2E5 hits counted, +0x2EC timer, +0x2F8 afterimage
// counter, +0x304 trackers alive, +0x308 swoop index, +0x31C projectiles out, +0x320 point index, +0x328 / +0x32C the
// attack counters (kit), +0x330 pattern step, +0x334 hit part, +0x338 hit reaction step, +0x34C swoop speed, +0x394
// home, +0x3A0 target. Event flags (0x51E940 + n): 96 / 97, 112, 208-226 (the event's cues both ways).
#include "port/game.h"

#include <cmath>
#include <array>
#include <cstring>

namespace hotd::port {

namespace magician {
constexpr uint32_t states = 0xC6990;       // +0x5C -> state routine
constexpr uint32_t pattern = 0xC69C0;      // {state, hit-point share (float, tenths)} per step, state -1 ends
constexpr uint32_t points_a = 0xC69F0;     // the fireball volleys' 4 points around the camera
constexpr uint32_t points_b = 0xC6A20;     // the swoops' 4 points
constexpr uint32_t swoop_counts = 0xC6A50; // swoops per round (-1 ends)
constexpr uint32_t swoops = 0xC6A60;       // {speed factor (float), wait (int)} x 8
constexpr uint32_t shot_speeds = 0xC6AA0;  // {speed, speed 2 (floats), wait} x 3 (by +0x32C)
constexpr uint32_t barrage_shot = 0x74150; // magician_barrage_shot_frame
constexpr uint32_t barrage_proj = 0x74C70; // magician_barrage_projectile
constexpr uint32_t error_text = 0x71F20;   // "error:tbl is end..."
constexpr uint32_t camera_status = 0x51F564;
constexpr uint32_t fade_id = 0x517CA0, fade_level = 0x517CA4, fade_src = 0x512CA0, fade_dst = 0x517CB0;
inline uint32_t flag(uint32_t n) { return 0x51E940 + n; }
} // namespace magician

// coprocessor 0x0F on (camera - position): the heading +0x30, folded
template <class P> void Game<P>::magician_face_camera(uint32_t obj)
{
    of<uint32_t>(obj, 0x30) = yaw_toward_a(mem.f32(G::camera_pos) - of<float>(obj, 0x20),
                                           mem.f32(G::camera_pos + 4) - of<float>(obj, 0x24),
                                           mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28));
}

// velocity = normalize(target - position) (coprocessor 0x0C) x speed; true when every axis is within speed
template <class P> bool Game<P>::magician_fly_to(uint32_t obj, uint32_t target, float speed)
{
    const float dx = mem.f32(target) - of<float>(obj, 0x20);
    const float dy = mem.f32(target + 4) - of<float>(obj, 0x24);
    const float dz = mem.f32(target + 8) - of<float>(obj, 0x28);
    const Tgp::Out o = boss_unit(fbits(dx), fbits(dy), fbits(dz));
    for (uint32_t i = 0; i < 3; ++i) {
        float v;
        std::memcpy(&v, &o.w[i], 4);
        of<float>(obj, 0x78 + 4 * i) = v * speed;
    }
    return std::fabs(dx) <= speed && std::fabs(dy) <= speed && std::fabs(dz) <= speed;
}

// (Magician - shot) through the identity translated to the camera's position (coprocessor 0x21 / 0x88 / 0x1A / 0x06;
// PC Mode: PC 0x4D4BA5's identity with the translation words, the Magician file's transform 0x4CF570)
template <class P> Tgp::Out Game<P>::magician_from_camera(uint32_t mag, uint32_t obj)
{
    const float x = of<float>(mag, 0x20) - of<float>(obj, 0x20), y = of<float>(mag, 0x24) - of<float>(obj, 0x24),
                z = of<float>(mag, 0x28) - of<float>(obj, 0x28);
    if (pm_boss()) {
        pm::Matrix m;
        m.e[9] = mem.f32(G::camera_pos);
        m.e[10] = mem.f32(G::camera_pos + 4);
        m.e[11] = mem.f32(G::camera_pos + 8);
        Tgp::Out o;
        float r[3];
        m.transform(x, y, z, r[0], r[1], r[2]);
        for (int k = 0; k < 3; ++k)
            o.w[k] = fbits(r[k]);
        o.n = 3;
        return o;
    }
    cam_copro(0x21, {});
    cam_copro(0x88, {mem.u32(G::camera_pos), mem.u32(G::camera_pos + 4), mem.u32(G::camera_pos + 8)});
    const Tgp::Out o = cam_copro(0x1A, {fbits(x), fbits(y), fbits(z)});
    cam_copro(0x06, {});
    return o;
}

// the effects' matrix: push, at the position, headings y / x / z, the scale
template <class P> void Game<P>::magician_effect_matrix(uint32_t obj)
{
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    cam_copro(0x14, {of<uint32_t>(obj, 0x2C)});
    cam_copro(0x16, {of<uint32_t>(obj, 0x34)});
    const uint32_t s = of<uint32_t>(obj, 0x94);
    cam_copro(0x13, {s, s, s});
}

// ---- shared helpers ----

// 0x18F00 palette_fade_in_task: one brightness step every frames / 32 (0x517CA4 0 -> 32); a newer fade ends it
template <class P> void Game<P>::palette_fade_in_task(uint32_t t)
{
    using namespace magician;
    if (mem.u32(t + 0x20) != mem.u32(fade_id)) {
        task_exit();
        return;
    }
    if (mem.at<int32_t>(fade_level) > 32) {
        hw_write32(0x1800124, 0);
        task_exit();
        return;
    }
    const int32_t lvl = mem.at<int32_t>(fade_level);
    mem.at<uint16_t>(0x51E8C4) = 0;
    if (lvl == 32) {
        mem.at<uint16_t>(0x51F704) = 0;
    } else {
        palette_blend_white(lvl, fade_src, fade_dst);
        mem.at<uint16_t>(0x51F704) = 1;
    }
    int32_t v = mem.at<int32_t>(fade_level);
    mem.at<uint16_t>(0x51E8C4) = 1;
    if (v < 0 || v > 31)
        v = 31;
    const uint32_t c = uint32_t(31 - v);
    hw_write32(0x1800124, (c << 10) | (c << 5) | 0x8000u | c);   // the screen's brightness register (16 bits)
    const float f = mem.f32(t + 0x24);
    mem.set32(t + 0x24, fbits(f + 1.0f));
    if (!(f < mem.f32(t + 0x28))) {
        mem.set32(t + 0x24, 0);
        mem.set32(fade_level, mem.u32(fade_level) + 1);
    }
}

// 0x19130 palette_fade_in_start (frames, float bits): a fade from black (child task), its first step now
template <class P> void Game<P>::palette_fade_in_start(uint32_t frames)
{
    using namespace magician;
    float fr;
    std::memcpy(&fr, &frames, 4);
    const uint32_t t = task_create_child(F::palette_fade_in_task, 0x34);
    const float step = fr / 32.0f;
    const uint32_t id = mem.u32(fade_id) + 1;
    mem.set32(fade_id, id);
    mem.set32(t + 0x20, id);
    mem.set32(fade_level, 0);
    mem.set32(t + 0x24, 0);
    mem.set32(t + 0x2C, frames);
    mem.set32(t + 0x28, fbits(step));
    if (mem.u32(t + 0x20) != id) {
        task_exit();
        return;
    }
    if (mem.at<int32_t>(fade_level) > 32) {
        hw_write32(0x1800124, 0);
        task_exit();
        return;
    }
    const int32_t lvl = mem.at<int32_t>(fade_level);
    mem.at<uint16_t>(0x51E8C4) = 0;
    if (lvl == 32) {
        mem.at<uint16_t>(0x51F704) = 0;
    } else {
        palette_blend_white(lvl, fade_src, fade_dst);
        mem.at<uint16_t>(0x51F704) = 1;
    }
    mem.at<uint16_t>(0x51E8C4) = 1;
    const float f = mem.f32(t + 0x24);
    mem.set32(t + 0x24, fbits(f + 1.0f));
    if (!(f < mem.f32(t + 0x28))) {
        mem.set32(t + 0x24, 0);
        mem.set32(fade_level, mem.u32(fade_level) + 1);
    }
    hw_write32(0x1800124, 0x7FFF);
}

// 0x19860 palette_fade_out_task: one brightness step down every frames / 32 (32 -> 0); a newer fade ends it
template <class P> void Game<P>::palette_fade_out_task(uint32_t t)
{
    using namespace magician;
    if (mem.u32(t + 0x20) != mem.u32(fade_id) || mem.at<int32_t>(fade_level) < 0) {
        task_exit();
        return;
    }
    mem.at<uint16_t>(0x51E8C4) = 0;
    palette_blend_white(mem.at<int32_t>(fade_level), fade_src, fade_dst);
    int32_t v = mem.at<int32_t>(fade_level);
    mem.at<uint16_t>(0x51F704) = 1;
    mem.at<uint16_t>(0x51E8C4) = 1;
    if (v < 0 || v > 31)
        v = 31;
    const uint32_t c = uint32_t(31 - v);
    hw_write32(0x1800124, (c << 10) | (c << 5) | 0x8000u | c);
    const float f = mem.f32(t + 0x24);
    mem.set32(t + 0x24, fbits(f + 1.0f));
    if (!(f < mem.f32(t + 0x28))) {
        mem.set32(t + 0x24, 0);
        mem.set32(fade_level, mem.u32(fade_level) - 1);
    }
}

// 0x199C0 palette_fade_out_start (frames, float bits): a fade to black (brightness 32 -> 0)
template <class P> void Game<P>::palette_fade_out_start(uint32_t frames)
{
    using namespace magician;
    float fr;
    std::memcpy(&fr, &frames, 4);
    const uint32_t t = task_create(F::palette_fade_out_task, 0x34);
    const float step = fr / 32.0f;
    const uint32_t id = mem.u32(fade_id) + 1;
    mem.set32(fade_id, id);
    mem.set32(t + 0x20, id);
    mem.set32(fade_level, 32);
    mem.set32(t + 0x24, 0);
    mem.set32(t + 0x2C, frames);
    mem.set32(t + 0x28, fbits(step));
}

// 0x1A110 palette_blend_white (level 0..32, source, destination): 0xC00 words, white - (white - source) x level / 32
template <class P> void Game<P>::palette_blend_white(int level, uint32_t src, uint32_t dst)
{
    for (uint32_t i = 0; i < 0xC00; ++i) {
        const uint32_t s = mem.u32(src + 4 * i);
        const int32_t m = int32_t((0xFF00FFu - s) * uint32_t(level));
        mem.set32(dst + 4 * i, 0xFF00FFu - (uint32_t(m >> 5) & 0xFF00FFu));
    }
}

// 0x2F780 any_player_in_play: 1 when either player is in play, else -1
template <class P> uint32_t Game<P>::any_player_in_play()
{
    if (player_in_play(0) == 1 || player_in_play(1) == 1)
        return 1;
    return 0xFFFFFFFFu;
}

// 0x30260 body_find_part_rec (part, id): the part with that id among the part and its children, 0 if none
template <class P> uint32_t Game<P>::body_find_part_rec(uint32_t part, uint32_t id)
{
    if (mem.u32(part + 0x38) == id)
        return part;
    for (uint32_t i = 0; i < mem.u32(part + 0x34); ++i) {
        const uint32_t r = body_find_part_rec(mem.u32(mem.u32(part + 0x24) + 4 * i), id);
        if (r != 0)
            return r;
    }
    return 0;
}

// 0x302B0 body_find_part (body, id): the part node with that id in the body's part tree, 0 if none
template <class P> uint32_t Game<P>::body_find_part(uint32_t obj, uint32_t id)
{
    const uint32_t root = of<uint32_t>(obj, 0xC8);
    for (uint32_t k = 0; k < mem.u32(root + 0x34); ++k) {
        const uint32_t p = mem.u32(mem.u32(root + 0x24) + 4 * k);
        uint32_t r = 0;
        if (mem.u32(p + 0x38) == id) {
            r = p;
        } else {
            for (uint32_t i = 0; i < mem.u32(p + 0x34) && r == 0; ++i)
                r = body_find_part_rec(mem.u32(mem.u32(p + 0x24) + 4 * i), id);
        }
        if (r != 0)
            return r;
    }
    return 0;
}

// 0x30330 body_hit_part_find_rec: the id of the part hit by the last shot (+0x3C bit 8) in a part and its children
template <class P> uint32_t Game<P>::body_hit_part_find_rec(uint32_t part)
{
    if ((mem.at<uint16_t>(part + 0x3C) >> 8) & 1)
        return mem.u32(part + 0x38);
    for (uint32_t i = 0; i < mem.u32(part + 0x34); ++i) {
        const uint32_t r = body_hit_part_find_rec(mem.u32(mem.u32(part + 0x24) + 4 * i));
        if (r != 0xFFFFFFFFu)
            return r;
    }
    return 0xFFFFFFFFu;
}

// 0x30380 body_hit_part_find (body): the id of the part the last shot hit, -1 if none
template <class P> uint32_t Game<P>::body_hit_part_find(uint32_t obj)
{
    const uint32_t root = of<uint32_t>(obj, 0xC8);
    for (uint32_t k = 0; k < mem.u32(root + 0x34); ++k) {
        const uint32_t p = mem.u32(mem.u32(root + 0x24) + 4 * k);
        uint32_t r = 0xFFFFFFFFu;
        if ((mem.at<uint16_t>(p + 0x3C) >> 8) & 1) {
            r = mem.u32(p + 0x38);
        } else {
            for (uint32_t i = 0; i < mem.u32(p + 0x34) && r == 0xFFFFFFFFu; ++i)
                r = body_hit_part_find_rec(mem.u32(mem.u32(p + 0x24) + 4 * i));
        }
        if (r != 0xFFFFFFFFu)
            return r;
    }
    return 0xFFFFFFFFu;
}

// ---- the Magician ----

// 0x70AB0 boss_magician (kind 20): the set-up (table 0xC69B0 by +0x70: 0 the intro, 1 the fight)
template <class P> void Game<P>::boss_magician(uint32_t obj)
{
    const uint32_t r = mem.u32(0xC69B0 + 4 * uint32_t(int32_t(of<int16_t>(obj, 0x70))));
    if (r == F::magician_intro)
        magician_intro(obj);
    else if (r == F::magician_fight_init)
        magician_fight_init(obj);
    else
        call_routine(r, obj);
}

// 0x70AD0 magician_idle (state 0): nothing
template <class P> void Game<P>::magician_idle(uint32_t obj)
{
    (void)obj;
}

// 0x70AE0 magician_intro (set-up 0): the scene with Dr. Curien: the camera held on him (flags 212 / 215), shown at
// flag 224 (225 once MO 0x5D ends), facing the camera; flags 112 / 208 / 209: MO 0x57 and the intro object (the
// fireball into Curien), cries; flag 226: back to MO 0x56 (flag 210); at flag 211 gone
template <class P> void Game<P>::magician_intro(uint32_t obj)
{
    using namespace magician;
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    if (mem.at<uint8_t>(flag(212)) == 1 && mem.at<uint8_t>(flag(215)) == 0) {
        mem.set32(G::camera_records + 8, 0x44FD2B17);
        mem.set32(G::camera_hold_hostage, 1);
        mem.set32(G::camera_records, 0xC447C4A2);
        mem.set32(G::camera_records + 4, 0x42B53CC6);
        mem.set32(G::camera_records + 0x18, 0xC445A328);
        mem.set32(G::camera_records + 0x1C, 0x42BA49E2);
        mem.set32(G::camera_records + 0x20, 0x44FD7137);
    }
    if (mem.at<uint8_t>(flag(224)) == 1 && mem.at<uint8_t>(flag(225)) == 0 && ((of<uint32_t>(obj, 0x58) >> 26) & 1))
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~(1u << 26);
    if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) && of<uint16_t>(obj, 0xD0) == 0x5D) {
        set_motion_blend(obj, 0x56, 0, 4);
        mem.at<uint8_t>(flag(225)) = 1;
    }
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    auto draw = [&] {
        object_draw(obj);
        if (((of<uint32_t>(obj, 0x58) >> 26) & 1) == 0) {
            of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
            of<int32_t>(obj, 0x10C) = of<int32_t>(obj, 0x10C) + 1;
        }
    };
    switch (uint32_t(int32_t(of<int16_t>(obj, 0x60)))) {
    case 0: {
        const uint8_t type = mem.at<uint8_t>(rec + 4);
        of<uint16_t>(obj, 0xCC) = type;
        mem.set32(G::hit_type, type);
        body_init(obj, type, 0x5D);
        creature_type_setup(obj);
        for (uint32_t i = 1; i <= 15; ++i) {
            const uint32_t p = body_find_part(obj, i);
            if (p != 0)   // (the original writes through it unchecked)
                mem.set32(p + 0xAC, 0);
        }
        of<uint16_t>(obj, 0x2E0) = 0xFF;
        of<uint32_t>(obj, 0) = F::magician_intro;
        of<int16_t>(obj, 0x60) = 1;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | (1u << 26);
        of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) | 1);
    }
        [[fallthrough]];
    case 1:
        magician_face_camera(obj);
        if (mem.at<uint8_t>(flag(112)) != 1)
            return draw();
        next();
        [[fallthrough]];
    case 2:
        if (mem.at<uint8_t>(flag(208)) != 1)
            return draw();
        next();
        [[fallthrough]];
    case 3:
        if (mem.at<uint8_t>(flag(209)) != 1)
            return draw();
        set_motion_blend(obj, 0x57, 0, 4);
        of<int32_t>(obj, 0x31C) = 0;
        magician_intro_object_spawn(obj);
        mem.at<uint8_t>(flag(215)) = 1;
        mem.set32(G::camera_hold_hostage, 0);
        next();
        [[fallthrough]];
    case 4:
        if (of<uint16_t>(obj, 0xF4) == 10)
            sound_cue(0xA01465);
        if (of<int16_t>(obj, 0xF4) <= 0x42)
            return draw();
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | (1u << 26);
        sound_cue(0xA01466);
        next();
        [[fallthrough]];
    case 5:
        if (mem.at<uint8_t>(flag(226)) == 0)
            return draw();
        mem.at<uint8_t>(flag(210)) = 1;
        set_motion_blend(obj, 0x56, 0, 4);
        next();
        [[fallthrough]];
    case 6:
        if (mem.at<uint8_t>(flag(211)) != 1)
            return draw();
        next();
        [[fallthrough]];
    case 7:
        body_free_parts(obj);
        task_exit();
        return;
    default: return draw();
    }
}

// 0x70E80 magician_fight_init (set-up 1): type and body (MO 0x56), counted, facing the camera, an enemy slot, the
// record's hit points; magician_frame, state 2 (the descent)
template <class P> void Game<P>::magician_fight_init(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    const uint8_t type = mem.at<uint8_t>(rec + 4);
    of<uint16_t>(obj, 0xCC) = type;
    mem.set32(G::hit_type, type);
    body_init(obj, type, 0x56);
    creature_type_setup(obj);
    for (uint32_t i = 1; i <= 15; ++i) {
        const uint32_t p = body_find_part(obj, i);
        if (p != 0)
            mem.set32(p + 0xAC, 0);
    }
    magician_face_camera(obj);
    mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
    of<int8_t>(obj, 0x2E7) = -1;
    enemy_slot_take(obj);
    const uint16_t hp = mem.at<uint16_t>(rec);
    of<uint16_t>(obj, 0x2E0) = 0xFF;
    of<uint32_t>(obj, 0) = F::magician_frame;
    of<int16_t>(obj, 0x5C) = 2;
    of<int16_t>(obj, 0x60) = 0;
    of<uint16_t>(obj, 0x72) = hp;
    of<uint16_t>(obj, 0x70) = hp;
    of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) | 1);
}

// 0x71010 magician_frame: aggression, opaque; hits, the hit reaction, the state (table 0xC6990) unless frozen (bit 27);
// velocity + acceleration into the position; drawn unless hidden (bit 25); motion frames; depth / attacker lists
template <class P> void Game<P>::magician_frame(uint32_t obj)
{
    boss_aggression_update(obj);
    of<uint16_t>(obj, 0x2E0) = 0xFF;
    if (of<uint8_t>(obj, 0x58) & 1)
        magician_hit(obj);
    if ((of<uint32_t>(obj, 0x58) >> 28) & 1)
        magician_hit_reaction(obj);
    if (((of<uint32_t>(obj, 0x58) >> 27) & 1) == 0) {
        const uint32_t r = mem.u32(magician::states + 4 * uint32_t(int32_t(of<int16_t>(obj, 0x5C))));
        switch (r) {
        case F::magician_idle: magician_idle(obj); break;
        case F::magician_dying: magician_dying(obj); break;
        case F::magician_descend: magician_descend(obj); break;
        case F::magician_next_pattern: magician_next_pattern(obj); break;
        case F::magician_fireballs: magician_fireballs(obj); break;
        case F::magician_attack: magician_attack(obj); break;
        case F::magician_barrage: magician_barrage(obj); break;
        default: call_routine(r, obj); break;
        }
    }
    for (uint32_t i = 0; i < 3; ++i) {
        const float v = of<float>(obj, 0x78 + 4 * i) + of<float>(obj, 0x84 + 4 * i);
        of<float>(obj, 0x78 + 4 * i) = v;
        of<float>(obj, 0x20 + 4 * i) = of<float>(obj, 0x20 + 4 * i) + v;
    }
    if (((of<uint32_t>(obj, 0x58) >> 25) & 1) == 0)
        object_draw(obj);
    if (((of<uint32_t>(obj, 0x58) >> 26) & 1) == 0) {
        of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
        of<int32_t>(obj, 0x10C) = of<int32_t>(obj, 0x10C) + 1;
    }
    depth_list_add(obj);
    if (((of<uint16_t>(obj, 0x324) >> 11) & 1) == 0)
        attacker_list_add(obj);
}

// 0x71110 magician_hit: a shot (flags bit 8): the part hit (+0x334); parts 4 / 6 / 11 / 13 (or none while guarded,
// +0x324 bit 3) take the record's damage (x0.8 with two players) and start the hit reaction; at 0 state 1 (dying);
// other parts: sparks
template <class P> void Game<P>::magician_hit(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    if (((of<uint16_t>(obj, 0x58) >> 8) & 1) == 0)
        return;
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x100u;
    const uint32_t part = body_hit_part_find(obj);
    of<uint32_t>(obj, 0x334) = part;
    bool damage;
    if ((of<uint8_t>(obj, 0x324) >> 3) & 1) {
        damage = of<uint32_t>(obj, 0x334) == 0xFFFFFFFFu;
    } else {
        if (part == 0xFFFFFFFFu)
            return;
        damage = part == 4 || part == 6 || part == 11 || part == 13;
    }
    if (!damage) {
        magician_hit_spark(obj);
        return;
    }
    uint32_t d = mem.at<uint16_t>(rec + 2);
    if (mem.at<uint16_t>(G::players_in_game) == 2)
        d = uint32_t(int32_t(float(int32_t(int16_t(d))) * 0.8f));
    const uint16_t hp = uint16_t(of<uint16_t>(obj, 0x70) - d);
    if (((of<uint8_t>(obj, 0x324) >> 4) & 1) == 0) {
        of<int32_t>(obj, 0x338) = 0;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x10000000u;
    }
    of<uint16_t>(obj, 0x70) = hp;
    if (int32_t(uint32_t(hp) << 16) > 0 || ((of<uint8_t>(obj, 0x324) >> 4) & 1)) {
        sound_cue(0xA01342);
        return;
    }
    mem.set32(magician::camera_status, mem.u32(magician::camera_status) & ~2u);
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 1;
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xE7FFFFFEu;
}

// 0x71260 magician_hit_spark: sparks (impact class 10) at the hit part, through stored matrix 1
template <class P> void Game<P>::magician_hit_spark(uint32_t obj)
{
    const uint32_t part = body_find_part(obj, of<uint32_t>(obj, 0x334));
    const Tgp::Out o = boss_stored_xform(1, mem.u32(part + 0x5C), mem.u32(part + 0x60), mem.u32(part + 0x64));
    float x, y, z;
    std::memcpy(&x, &o.w[0], 4);
    std::memcpy(&y, &o.w[1], 4);
    std::memcpy(&z, &o.w[2], 4);
    impact_effect_spawn(x, y, z, 10);
}

// 0x712F0 magician_hit_reaction (+0x338): 0 = frozen and guarded, the state's check (4 / 5 / 6, else none);
// 1 = the flinch (MO 0x4C, cry); 2 = at its end MO 0x56, step 1 of the state
template <class P> void Game<P>::magician_hit_reaction(uint32_t obj)
{
    const int32_t w = of<int32_t>(obj, 0x338);
    if (w == 0) {
        of<int32_t>(obj, 0x2F4) = 9;
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 8;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x08000000u;
        const int16_t st = of<int16_t>(obj, 0x5C);
        if (st == 5)
            magician_hit_check_b(obj);
        else if (st == 4)
            magician_hit_check_a(obj);
        else if (st == 6)
            magician_hit_check_c(obj);
        else
            magician_vuln_clear(obj);
        return;
    }
    if (w == 1) {
        obj_stop(obj);
        set_motion_blend(obj, 0x4C, 0, 4);
        sound_cue(0xA0145C);
        of<int32_t>(obj, 0x338) = of<int32_t>(obj, 0x338) + 1;
    } else if (w != 2) {
        return;
    }
    if ((of<uint8_t>(obj, 0x3C5) >> 4) & 1) {
        set_motion_blend(obj, 0x56, 0, 4);
        magician_vuln_clear(obj);
        of<uint8_t>(obj, 0x2E5) = 0;
        of<int16_t>(obj, 0x60) = 1;
    }
}

// 0x713D0 magician_vuln_clear: not guarded (+0x324 bit 3), flags bits 27 / 28 off
template <class P> void Game<P>::magician_vuln_clear(uint32_t obj)
{
    of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & ~8u;
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xE7FFFFFFu;
}

// 0x71400 magician_hit_check_a (state 4): below the step's hit-point share: guarded, cry, state 3 (next step)
template <class P> void Game<P>::magician_hit_check_a(uint32_t obj)
{
    const float share = float(int32_t(int16_t(of<int16_t>(obj, 0x72) / 10)));
    const float hp = float(int32_t(of<int16_t>(obj, 0x70)));
    magician_vuln_clear(obj);
    if (hp < share * mem.f32(magician::pattern + 4 + 8 * of<uint32_t>(obj, 0x330))) {
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 8;
        sound_cue(0xA0145C);
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 3;
    }
}

// 0x71490 magician_hit_check_b (state 5): below the share: state 3; past step 9 the record's hits (+9, +1 with two
// players) start the flinch, else hurt again
template <class P> void Game<P>::magician_hit_check_b(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    const float share = float(int32_t(int16_t(of<int16_t>(obj, 0x72) / 10)));
    const float hp = float(int32_t(of<int16_t>(obj, 0x70)));
    if (hp < share * mem.f32(magician::pattern + 4 + 8 * of<uint32_t>(obj, 0x330))) {
        magician_vuln_clear(obj);
        sound_cue(0xA01466);
        sound_cue(0xA0145C);
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 3;
    }
    int32_t n = mem.at<int8_t>(rec + 9);
    if (mem.at<uint16_t>(G::players_in_game) == 2)
        ++n;
    if (of<int16_t>(obj, 0x60) > 9) {
        const uint8_t c = uint8_t(of<uint8_t>(obj, 0x2E5) + 1);
        of<uint8_t>(obj, 0x2E5) = c;
        if (int32_t(int8_t(c)) >= n) {
            sound_cue(0xA01466);
            of<int32_t>(obj, 0x338) = of<int32_t>(obj, 0x338) + 1;
            return;
        }
    }
    magician_vuln_clear(obj);
}

// 0x71590 magician_hit_check_c (state 6): as check a with the cry 0xA01475 first
template <class P> void Game<P>::magician_hit_check_c(uint32_t obj)
{
    const float share = float(int32_t(int16_t(of<int16_t>(obj, 0x72) / 10)));
    const float hp = float(int32_t(of<int16_t>(obj, 0x70)));
    magician_vuln_clear(obj);
    if (hp < share * mem.f32(magician::pattern + 4 + 8 * of<uint32_t>(obj, 0x330))) {
        sound_cue(0xA01475);
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 8;
        sound_cue(0xA0145C);
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 3;
    }
}

// 0x71620 magician_dying (state 1): kill score 2500, the death cries; MO 0x5E, MO 0x5F with part effects; drifts to
// his place (scene state 3); MO 0x60, out of the count; waits for the event's flags 219 / 220 / 223 / 221; MO 0x5F
// and more effects; hidden, the death flash, the palette dark and back (the record's colour rows and timings);
// flag 222, gone
template <class P> void Game<P>::magician_dying(uint32_t obj)
{
    using namespace magician;
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    auto part_effect = [&](bool cry) {
        const uint32_t r = rand();
        const uint32_t p = body_find_part(obj, (r & 7) * 2 + 1);
        if (p != 0) {
            magician_part_effect(p);
            if (cry)
                sound_cue(0xA0146E);
        }
    };
    const int16_t s0 = of<int16_t>(obj, 0x60);
    if (s0 > 3 && s0 <= 13) {
        const int32_t t = of<int32_t>(obj, 0x2EC) + 1;
        of<int32_t>(obj, 0x2EC) = t;
        if (t % 50 == 0)
            part_effect(false);
    }
    switch (uint32_t(int32_t(of<int16_t>(obj, 0x60)))) {
    case 0:
        boss_event(kBossClockStop);   // PC 0x4D1F63
        ++magician_deaths;
        kill_score_shooter(obj, 2500);
        obj_stop(obj);
        of<int32_t>(obj, 0x2EC) = 60;
        sound_send3(0xA01475);
        sound_send3(0xA01466);
        sound_send3(0xA00002);
        mem.at<uint8_t>(flag(96)) = 0;
        next();
        [[fallthrough]];
    case 1: {
        const int32_t t = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = t;
        if (t > 0)
            return;
        set_motion_blend(obj, 0x5E, 0, 4);
        next();
    }
        [[fallthrough]];
    case 2:
        if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) == 0)
            return;
        set_motion_blend(obj, 0x5F, 0, 4);
        sound_cue(0xA0131C);
        of<uint32_t>(obj, 0x2EC) = mem.u32(rec + 0x90);
        next();
        [[fallthrough]];
    case 3: {
        if (of<int32_t>(obj, 0x2EC) % 10 == 0)
            part_effect(true);
        const int32_t t = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = t;
        if (t > 0)
            return;
        if (boss_on()) {   // PC 0x4D1EE4 (frontend_state 4): Boss Mode skips the flight home and the fall: step 30
            of<int16_t>(obj, 0x60) = 30;
            of<int32_t>(obj, 0x2EC) = 0x78;
            return;
        }
        of<uint32_t>(obj, 0x3A0) = 0xC445C323;   // -791.049
        of<uint32_t>(obj, 0x3A4) = 0x423FAA7F;   // 47.9165
        of<uint32_t>(obj, 0x3A8) = 0x451A0971;   // 2464.59
        mem.at<uint8_t>(G::scene_state) = 3;
        next();
    }
        [[fallthrough]];
    case 4: {
        const float dx = of<float>(obj, 0x3A0) - of<float>(obj, 0x20);
        const float dy = of<float>(obj, 0x3A4) - of<float>(obj, 0x24);
        const float dz = of<float>(obj, 0x3A8) - of<float>(obj, 0x28);
        const Tgp::Out o = boss_unit(fbits(dx), fbits(dy), fbits(dz));
        of<uint32_t>(obj, 0x78) = o.w[0];
        of<uint32_t>(obj, 0x7C) = o.w[1];
        of<uint32_t>(obj, 0x80) = o.w[2];
        magician_face_camera(obj);
        for (uint32_t i = 0; i < 3; ++i)
            of<float>(obj, 0x78 + 4 * i) = 0.35f * of<float>(obj, 0x78 + 4 * i);
        if (!(std::fabs(dx) <= 0.35f && std::fabs(dy) <= 0.35f && std::fabs(dz) <= 0.35f))
            return;
        obj_stop(obj);
        set_motion_blend(obj, 0x60, 0, 4);
        of<int16_t>(obj, 0x60) = 10;
    }
        [[fallthrough]];
    case 10: {
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 0x800u;
        const int32_t slot = of<int8_t>(obj, 0x2E7);
        mem.at<uint8_t>(G::attack_slots + uint32_t(slot * 20)) = 0;
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
        next();
    }
        [[fallthrough]];
    case 11:
        if (of<uint16_t>(obj, 0xD0) == 0x60 && ((of<uint8_t>(obj, 0x3C5) >> 4) & 1))
            set_motion_blend(obj, 0x61, 0, 4);
        if (mem.at<uint8_t>(flag(219)) != 1)
            return;
        next();
        [[fallthrough]];
    case 12:
        if (of<uint16_t>(obj, 0xD0) == 0x60 && ((of<uint8_t>(obj, 0x3C5) >> 4) & 1))
            set_motion_blend(obj, 0x61, 0, 4);
        if (mem.at<uint8_t>(flag(220)) != 1)
            return;
        next();
        [[fallthrough]];
    case 13:
        if (mem.at<uint8_t>(flag(223)) != 1)
            return;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~(1u << 26);
        next();
        [[fallthrough]];
    case 14:
        if (mem.at<uint8_t>(flag(221)) != 1)
            return;
        set_motion_blend(obj, 0x5F, 0, 4);
        of<uint32_t>(obj, 0x2EC) = mem.u32(rec + 0x94);
        sound_cue(0xA0145B);
        next();
        [[fallthrough]];
    case 15: {
        if (of<int32_t>(obj, 0x2EC) % mem.at<int32_t>(rec + 0x98) == 0)
            part_effect(true);
        const int32_t t = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = t;
        if (t > 0)
            return;
        of<int32_t>(obj, 0x2EC) = 0;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | (1u << 26);
        next();
    }
        [[fallthrough]];
    case 16: {
        const int32_t t = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = t;
        if (t > 0)
            return;
        next();
    }
        [[fallthrough]];
    case 17:
        magician_death_flash_spawn(obj);
        if (pc_path()) {
            // PC Mode / Boss Mode: THOTD.EXE 0x4D1EE4 steps 0x11-0x14 wait for nothing: the flash, a music fade
            // (0x41B268), then the 180 frames at once (the arcade's palette dark / back waits are not there; pmch34)
            of<int32_t>(obj, 0x2EC) = 0xB4;
            of<int16_t>(obj, 0x60) = 21;
            goto last;
        }
        mem.set32(fade_level, 32);
        next();
        [[fallthrough]];
    case 18:
        if (mem.at<int32_t>(fade_level) >= 0)
            return;
        color_row_set(0, mem.at<int32_t>(rec + 0xA8));
        color_row_set(6, mem.at<int32_t>(rec + 0xAC));
        of<uint32_t>(obj, 0x2EC) = mem.u32(rec + 0x9C);
        next();
        [[fallthrough]];
    case 19: {
        const int32_t t = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = t;
        if (t > 0)
            return;
        mem.set32(fade_level, 0);
        palette_fade_in_start(mem.u32(rec + 0xA4));
        next();
    }
        [[fallthrough]];
    case 20:
        if (mem.at<int32_t>(fade_level) <= 32)
            return;
        mem.at<uint16_t>(0x51F702) = 0;
        hw_write32(0x1800124, 0x7FFF);
        mem.at<uint16_t>(0x51F704) = 0;
        of<int32_t>(obj, 0x2EC) = 0xB4;
        next();
        [[fallthrough]];
    case 21:
    last: {
        const int32_t t = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = t;
        if (t > 0)
            return;
        mem.at<uint8_t>(flag(222)) = 1;
        body_free_parts(obj);
        boss_event(kBossWin);   // PC 0x4D2464 (only Boss Mode's flag acts on it)
        task_exit();
        return;
    }
    case 30: {   // Boss Mode (PC 0x4D246F): 120 frames (minus 0x5CD364, always 1), then the win, each frame after
        const int32_t t = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = t;
        if (t < 1)
            boss_event(kBossWin);   // PC 0x4D2485
        return;
    }
    default: return;
    }
}

// 0x71C80 magician_descend (state 2): guarded, facing the camera (flags 96 / 97); at flag 213 the title sprites
// 0x1C9 / 0xE2 for 180 frames; then sinks (to -0.4 a frame) to the floor + 3, the boss life bar; after 75 frames flag
// 214, his home place, the aggression, state 3 step 4 (the first pattern step)
template <class P> void Game<P>::magician_descend(uint32_t obj)
{
    using namespace magician;
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    switch (uint32_t(int32_t(of<int16_t>(obj, 0x60)))) {
    case 0:
        mem.at<uint8_t>(flag(96)) = 1;
        mem.at<uint8_t>(flag(97)) = 1;
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 8;
        magician_face_camera(obj);
        next();
        [[fallthrough]];
    case 1:
        if (mem.at<uint8_t>(flag(213)) == 0)
            return;
        boss_event(kBossEntrance);   // PC 0x4D2536
        of<int32_t>(obj, 0x2EC) = 0xB4;
        sprite_place(0x1004000, 1, 0, 34, 6, 0x1C9);
        sprite_place(0x1004000, 1, 0, 7, 30, 0xE2);
        next();
        [[fallthrough]];
    case 2: {
        const int32_t t = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = t;
        if (t > 0)
            return;
        sprite_draw(0x1004000, 34, 6, 0x1C9);
        sprite_draw(0x1004000, 7, 30, 0xE2);
        boss_event(kBossClockStart);   // PC 0x4D25C4
        of<uint32_t>(obj, 0x88) = 0xBC23D70A;   // -0.01
        next();
    }
        [[fallthrough]];
    case 3: {
        if (of<float>(obj, 0x7C) < -0.4f) {
            of<uint32_t>(obj, 0x88) = 0;
            of<uint32_t>(obj, 0x7C) = 0xBECCCCCD;   // -0.4
        }
        if (of<float>(obj, 0x24) > mem.f32(G::floor_y) + 3.0f)
            return;
        obj_stop(obj);
        of<float>(obj, 0x24) = mem.f32(G::floor_y) + 3.0f;
        const uint32_t lb = task_create(F::boss_life_bar_task, 0x3C8);
        mem.at<int16_t>(lb + 0x60) = 0;
        of<int32_t>(obj, 0x2EC) = 0x4B;
        next();
    }
        [[fallthrough]];
    case 4: {
        const int32_t t = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = t;
        if (t > 0)
            return;
        mem.at<uint8_t>(flag(214)) = 1;
        of<uint32_t>(obj, 0x394) = of<uint32_t>(obj, 0x20);
        of<uint32_t>(obj, 0x398) = of<uint32_t>(obj, 0x24);
        of<int32_t>(obj, 0x330) = -1;
        of<uint32_t>(obj, 0x39C) = of<uint32_t>(obj, 0x28);
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & ~8u;
        mem.at<uint8_t>(flag(97)) = 0;
        boss_aggression_init(obj);
        of<int16_t>(obj, 0x5C) = 3;
        of<int16_t>(obj, 0x60) = 4;
        return;
    }
    default: return;
    }
}

// 0x71F40 magician_next_pattern (state 3): guarded, the flinch MO 0x4C, MO 0x56, 60 frames; back home (4 a frame,
// afterimages); the next pattern step (its state; "error:tbl is end..." at the table's end); facing the camera
template <class P> void Game<P>::magician_next_pattern(uint32_t obj)
{
    using namespace magician;
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    switch (uint32_t(int32_t(of<int16_t>(obj, 0x60)))) {
    case 0:
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 8;
        obj_stop(obj);
        set_motion_blend(obj, 0x4C, 0, 4);
        next();
        [[fallthrough]];
    case 1:
        if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) == 0)
            break;
        set_motion_blend(obj, 0x56, 0, 4);
        of<int32_t>(obj, 0x2EC) = 60;
        next();
        [[fallthrough]];
    case 2: {
        const int32_t t = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = t;
        if (t > 0)
            break;
        sound_cue(0xA01473);
        next();
    }
        [[fallthrough]];
    case 3: {
        const int32_t n = of<int32_t>(obj, 0x2F8) + 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n % int32_t(mem.at<int8_t>(rec + 0xF)) == 0)
            magician_afterimage_spawn(obj);
        if (!magician_fly_to(obj, obj + 0x394, 4.0f))
            break;
        obj_stop(obj);
        next();
    }
        [[fallthrough]];
    case 4: {
        hw_write32(0x180248E, 0xFEC0);   // (a 16-bit colour register)
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & ~8u;
        const uint32_t step = of<uint32_t>(obj, 0x330) + 1;
        of<uint32_t>(obj, 0x330) = step;
        if (mem.at<int32_t>(pattern + 8 * step) == -1) {
            text_cursor(10, 20);
            text_printf(error_text);
            of<int16_t>(obj, 0x60) = 0;
            of<uint32_t>(obj, 0x330) = of<uint32_t>(obj, 0x330) - 1;
        } else {
            of<int16_t>(obj, 0x60) = 0;
            of<uint16_t>(obj, 0x5C) = mem.at<uint16_t>(pattern + 8 * of<uint32_t>(obj, 0x330));
        }
        break;
    }
    default: break;
    }
    magician_face_camera(obj);
}

// 0x72210 magician_fireballs (state 4): a tracker; flies to a point around the camera (a random neighbour of the
// last, table 0xC69F0) leaving afterimages; with a player in play throws right / left / both (MO 0x55 / 0x54 /
// 0x53), 2 fireballs a hand (4 with two players); again once they are gone; step 10: the flinch, then state 5
template <class P> void Game<P>::magician_fireballs(uint32_t obj)
{
    using namespace magician;
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    auto two = [&](int hand) {
        magician_fireball_spawn(obj, hand);
        magician_fireball_spawn(obj, hand);
    };
    switch (uint32_t(int32_t(of<int16_t>(obj, 0x60)))) {
    case 0:
        of<int32_t>(obj, 0x31C) = 0;
        of<int32_t>(obj, 0x32C) = -1;
        of<int32_t>(obj, 0x320) = 0;
        if (of<int32_t>(obj, 0x304) <= 0)
            magician_tracker_spawn(obj);
        next();
        [[fallthrough]];
    case 1: {
        const int32_t r = int32_t(rand());
        int32_t k = of<int32_t>(obj, 0x320) - 1 + (r % 2) * 2;
        if (k < 0)
            k = 3;
        if (k > 3)
            k = 0;
        of<int32_t>(obj, 0x320) = k;
        const uint32_t e = points_a + 12 * uint32_t(k);
        of<float>(obj, 0x3A0) = mem.f32(G::camera_pos) + mem.f32(e);
        of<float>(obj, 0x3A4) = mem.f32(G::camera_pos + 4) + mem.f32(e + 4);
        of<float>(obj, 0x3A8) = mem.f32(G::camera_pos + 8) + mem.f32(e + 8);
        sound_cue(0xA01473);
        next();
    }
        [[fallthrough]];
    case 2: {
        const int32_t n = of<int32_t>(obj, 0x2F8) + 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n % int32_t(mem.at<int8_t>(rec + 0xF)) == 0)
            magician_afterimage_spawn(obj);
        if (!magician_fly_to(obj, obj + 0x3A0, mem.f32(rec + 0x24)))
            break;
        obj_stop(obj);
        next();
    }
        [[fallthrough]];
    case 3:
        if (any_player_in_play() == 0xFFFFFFFFu)
            break;
        next();
        [[fallthrough]];
    case 4: {
        int32_t h = of<int32_t>(obj, 0x32C) + 1;
        if (h > 2)
            h = 0;
        of<int32_t>(obj, 0x32C) = h;
        const bool pair = mem.at<uint16_t>(G::players_in_game) == 2;
        if (h == 0) {
            set_motion_blend(obj, 0x55, 0, 4);
            two(0);
            if (pair)
                two(0);
        } else if (h == 1) {
            set_motion_blend(obj, 0x54, 0, 4);
            two(1);
            if (pair)
                two(1);
        } else if (h == 2) {
            set_motion_blend(obj, 0x53, 0, 4);
            two(0);
            two(1);
        }
        next();
    }
        [[fallthrough]];
    case 5:
        if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) == 0)
            break;
        set_motion_blend(obj, 0x56, 0, 4);
        next();
        [[fallthrough]];
    case 6:
        if (of<int32_t>(obj, 0x31C) <= 0)
            of<int16_t>(obj, 0x60) = 1;
        break;
    case 10:
        set_motion_blend(obj, 0x4C, 0, 4);
        next();
        [[fallthrough]];
    case 11:
        if ((of<uint8_t>(obj, 0x3C5) >> 4) & 1) {
            obj_stop(obj);
            set_motion_blend(obj, 0x56, 0, 4);
            of<int16_t>(obj, 0x60) = 0;
            of<int16_t>(obj, 0x5C) = 5;
        }
        break;
    default: break;
    }
    magician_face_camera(obj);
}

// 0x72680 magician_attack (state 5): rounds of swoops to the points 0xC6A20 (speed table 0xC6A60 x the record's), a
// wait each; then (step 10) a lunge at the camera: MO 0x42 when near, a fireball from the hand, at frame 47 the hit on
// the players in play; then the next round
template <class P> void Game<P>::magician_attack(uint32_t obj)
{
    using namespace magician;
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    switch (uint32_t(int32_t(of<int16_t>(obj, 0x60)))) {
    case 0:
        of<int32_t>(obj, 0x320) = 0;
        of<int32_t>(obj, 0x328) = 0;
        of<int32_t>(obj, 0x32C) = 0;
        of<int32_t>(obj, 0x308) = 0;
        hw_write32(0x180248E, 0x801F);
        next();
        [[fallthrough]];
    case 1: {
        if (of<int32_t>(obj, 0x304) <= 0)
            magician_tracker_spawn(obj);
        const int32_t c = of<int32_t>(obj, 0x328) + 1;
        const int32_t kit = of<int32_t>(obj, 0x32C);
        of<int32_t>(obj, 0x328) = c;
        // (ours, totd8: The Typing of the Dead 0's boss strike hold, game.h: no lunge yet, one more swoop, asked again
        // at its end)
        if (!(c < mem.at<int32_t>(swoop_counts + 4 * uint32_t(kit))) && gBossHold && gBossHold(obj))
            of<int32_t>(obj, 0x328) = c - 1;
        else if (!(c < mem.at<int32_t>(swoop_counts + 4 * uint32_t(kit)))) {
            of<int32_t>(obj, 0x328) = 0;
            const int32_t k2 = of<int32_t>(obj, 0x32C) + 1;
            of<int32_t>(obj, 0x32C) = k2;
            if (mem.at<int32_t>(swoop_counts + 4 * uint32_t(k2)) == -1)
                of<int32_t>(obj, 0x32C) = 0;
            obj_stop(obj);
            of<int16_t>(obj, 0x60) = 10;
            break;
        }
        const int32_t r = int32_t(rand());
        int32_t k = of<int32_t>(obj, 0x320) - 1 + (r % 2) * 2;
        of<int32_t>(obj, 0x320) = k;
        if (k < 0)
            of<int32_t>(obj, 0x320) = 3;
        if (of<int32_t>(obj, 0x320) > 3)
            of<int32_t>(obj, 0x320) = 0;
        const uint32_t e = points_b + 12 * of<uint32_t>(obj, 0x320);
        of<float>(obj, 0x3A0) = mem.f32(G::camera_pos) + mem.f32(e);
        of<float>(obj, 0x3A4) = mem.f32(G::camera_pos + 4) + mem.f32(e + 4);
        of<float>(obj, 0x3A8) = mem.f32(G::camera_pos + 8) + mem.f32(e + 8);
        sound_cue(0xA01473);
        if (of<uint16_t>(obj, 0xD0) == 0x42 && ((of<uint8_t>(obj, 0x3C5) >> 4) & 1))
            set_motion_blend(obj, 0x56, 0, 4);
        next();
    }
        [[fallthrough]];
    case 2: {
        const int32_t n = of<int32_t>(obj, 0x2F8) + 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n % int32_t(mem.at<int8_t>(rec + 0xF)) == 0)
            magician_afterimage_spawn(obj);
        const float speed = mem.f32(rec + 0x3C) * mem.f32(swoops + 8 * of<uint32_t>(obj, 0x308));
        if (!magician_fly_to(obj, obj + 0x3A0, speed))
            break;
        obj_stop(obj);
        next();
        of<uint32_t>(obj, 0x2EC) = mem.u32(swoops + 4 + 8 * of<uint32_t>(obj, 0x308));
    }
        [[fallthrough]];
    case 3: {
        const int32_t t = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = t;
        if (t > 0)
            break;
        next();
    }
        [[fallthrough]];
    case 4:
        if (any_player_in_play() != 0xFFFFFFFFu)
            of<int16_t>(obj, 0x60) = 1;
        break;
    case 10: {
        const Tgp::Out a = boss_stored_xform(0, mem.u32(G::camera_pos), fbits(mem.f32(G::camera_pos + 4) + 5.0f),
                                             mem.u32(G::camera_pos + 8));
        float az;
        std::memcpy(&az, &a.w[2], 4);
        const Tgp::Out b = boss_stored_xform(1, a.w[0], a.w[1], fbits(az + 4.0f));
        of<uint32_t>(obj, 0x3A0) = b.w[0];
        of<uint32_t>(obj, 0x3A4) = b.w[1];
        of<uint8_t>(obj, 0x2E5) = 0;
        const int32_t w = of<int32_t>(obj, 0x308) + 1;
        of<uint32_t>(obj, 0x3A8) = b.w[2];
        of<int32_t>(obj, 0x308) = w;
        if (w > 7)
            of<int32_t>(obj, 0x308) = 0;
        const int32_t st = of<int16_t>(obj, 0x66);
        of<int32_t>(obj, 0x2F0) = st;
        of<uint32_t>(obj, 0x34C) = st == 0 ? mem.u32(rec + 0x48) : mem.u32(rec + 0x44);
        next();
    }
        [[fallthrough]];
    case 11: {
        const float dx = of<float>(obj, 0x3A0) - of<float>(obj, 0x20);
        const float dy = of<float>(obj, 0x3A4) - of<float>(obj, 0x24);
        const float dz = of<float>(obj, 0x3A8) - of<float>(obj, 0x28);
        const Tgp::Out o = boss_unit(fbits(dx), fbits(dy), fbits(dz));
        for (uint32_t i = 0; i < 3; ++i) {
            float v;
            std::memcpy(&v, &o.w[i], 4);
            of<float>(obj, 0x78 + 4 * i) = v * of<float>(obj, 0x34C);
        }
        float dist;
        if (pm_boss()) {   // PC 0x4D33D4: the table square root of (dy dy + dx dx) + dz dz
            dist = pm::fast_sqrt((dy * dy + dx * dx) + dz * dz);
        } else {
            const Tgp::Out d = cam_copro(0x1F, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                                                of<uint32_t>(obj, 0x3A0), of<uint32_t>(obj, 0x3A4), of<uint32_t>(obj, 0x3A8)});
            std::memcpy(&dist, &d.w[0], 4);
        }
        const float frames = dist / of<float>(obj, 0x34C);
        if (of<int32_t>(obj, 0x2F0) == 0 && frames <= 74.8f && of<uint16_t>(obj, 0xD0) == 0x56)
            set_motion_blend(obj, 0x42, 9, 4);
        if (of<int32_t>(obj, 0x2F0) == 1 && frames <= 74.8f && of<uint16_t>(obj, 0xD0) == 0x56)
            set_motion_blend(obj, 0x42, 15, 4);
        if (of<uint16_t>(obj, 0xD0) != 0x42)
            break;
        if (of<int32_t>(obj, 0x31C) <= 0)
            magician_fireball_spawn(obj, 2);
        if (of<uint16_t>(obj, 0xF4) == 47) {
            sound_cue(0xA01463);
            if (player_in_play(0) == 1)
                player_hit(0, 1, 4);
            if (player_in_play(1) == 1)
                player_hit(1, 1, 4);
        }
        if (of<int16_t>(obj, 0xF4) > 46)
            obj_stop(obj);
        if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) == 0)
            break;
        sound_cue(0xA01466);
        set_motion_blend(obj, 0x56, 0, 4);
        of<int16_t>(obj, 0x60) = 1;
        break;
    }
    default: break;
    }
    magician_face_camera(obj);
}

// 0x72DE0 magician_barrage (state 6): stops, unguarded; with a player in play MO 0x5A: every 3rd frame from frame 61
// a shot from a random part (the first starts the homing helper) up to the record's +0x13; MO 0x5C, 300 frames, again
template <class P> void Game<P>::magician_barrage(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    switch (uint32_t(int32_t(of<int16_t>(obj, 0x60)))) {
    case 0:
        obj_stop(obj);
        of<int32_t>(obj, 0x2EC) = 0;
        of<int32_t>(obj, 0x31C) = 0;
        of<int32_t>(obj, 0x32C) = -1;
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & ~8u;
        next();
        [[fallthrough]];
    case 1: {
        if (of<int32_t>(obj, 0x31C) != 0 || any_player_in_play() == 0xFFFFFFFFu)
            break;
        set_motion_blend(obj, 0x5A, 0, 4);
        int32_t k = of<int32_t>(obj, 0x32C) + 1;
        if (k > 7)
            k = 1;
        of<int32_t>(obj, 0x32C) = k;
        sound_cue(0xA01474);
        next();
    }
        [[fallthrough]];
    case 2: {
        const int32_t t = of<int32_t>(obj, 0x2EC) + 1;
        of<int32_t>(obj, 0x2EC) = t;
        if (t % 3 == 0 && of<int32_t>(obj, 0x31C) < int32_t(mem.at<int8_t>(rec + 0x13)) && of<int16_t>(obj, 0xF4) > 60) {
            const uint32_t r = rand();
            const uint32_t part = body_find_part(obj, (r & 7) * 2 + 1);
            if (part != 0) {
                if (of<int32_t>(obj, 0x31C) == 0)
                    magician_barrage_spawn(obj);
                of<int32_t>(obj, 0x31C) = of<int32_t>(obj, 0x31C) + 1;
                magician_barrage_shot(obj, part);
            }
        }
        if (of<int16_t>(obj, 0xF4) > 0xF4)
            sound_cue(0xA01475);
        if (((of<uint8_t>(obj, 0x3C5) >> 4) & 1) == 0)
            break;
        set_motion_blend(obj, 0x5C, 0, 4);
        of<int32_t>(obj, 0x2EC) = 0;
        next();
    }
        [[fallthrough]];
    case 3: {
        const int32_t t = of<int32_t>(obj, 0x2EC) + 1;
        of<int32_t>(obj, 0x2EC) = t;
        if (t > 299) {
            set_motion_blend(obj, 0x56, 0, 4);
            of<int16_t>(obj, 0x60) = 1;
        }
        break;
    }
    default: break;
    }
    magician_face_camera(obj);
}

// 0x73010 magician_fireball (a task; +0x320 the hand: 0 right, 1 left, 2 the lunge's): held at the hand (stored matrix
// 1 on the hand's place, the Magician +0x268 / +0x238) until the throw frame; then thrown at magician_fireball_aim's
// point (speed from the record, x0.75 for the double throw or two players), spinning, a trail; shot down after its
// hit points; at the camera the players are hit; the model animates 0x1B98.. (0x1CAB..0x1CB9 looping), grows
template <class P> void Game<P>::magician_fireball(uint32_t obj)
{
    const uint32_t mag = of<uint32_t>(obj, 0x35C);
    const uint32_t rec = of<uint32_t>(mag, 0x35C);
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    auto at_hand = [&](uint32_t off) {
        const Tgp::Out o = boss_stored_xform(1, mem.u32(mag + off), mem.u32(mag + off + 4), mem.u32(mag + off + 8));
        of<uint32_t>(obj, 0x20) = o.w[0];
        of<uint32_t>(obj, 0x24) = o.w[1];
        of<uint32_t>(obj, 0x28) = o.w[2];
    };
    auto to_frame = [&] {
        of<int16_t>(obj, 0x60) = 0;
        of<uint32_t>(obj, 0) = F::magician_fireball_frame;
    };
    const int16_t sub = of<int16_t>(obj, 0x60);
    bool fly = false;
    if (sub == 0) {
        const int32_t hand = of<int32_t>(obj, 0x320);
        if (hand == 0) {
            at_hand(0x268);
            if ((of<uint16_t>(mag, 0xD0) == 0x53 && of<uint16_t>(mag, 0xF4) == 0x5A) ||
                (of<uint16_t>(mag, 0xD0) == 0x55 && of<uint16_t>(mag, 0xF4) == 0x46))
                next();
        } else if (hand == 1) {
            at_hand(0x238);
            if ((of<uint16_t>(mag, 0xD0) == 0x53 && of<uint16_t>(mag, 0xF4) == 0x5A) ||
                (of<uint16_t>(mag, 0xD0) == 0x54 && of<uint16_t>(mag, 0xF4) == 0x46))
                next();
        } else if (hand == 2) {
            at_hand(0x238);
            if (of<uint16_t>(mag, 0xD0) == 0x42 && of<uint16_t>(mag, 0xF4) == 21)
                sound_cue(0xA01465);
            const int32_t c = of<int32_t>(obj, 0x2EC) + 1;
            of<int32_t>(obj, 0x2EC) = c;
            if (!(c < of<int32_t>(obj, 0x2F0))) {
                of<int32_t>(obj, 0x2EC) = 0;
                fireball_trail_copy(obj);
            }
            if (((of<uint8_t>(mag, 0x3C5) >> 4) & 1) || ((of<uint32_t>(mag, 0x58) >> 28) & 1))
                to_frame();
        }
    } else if (sub == 1) {
        const float s = of<float>(obj, 0x94) * 0.3f;
        of<float>(obj, 0x94) = s;
        of<float>(obj, 0x90) = s * mem.f32(rec + 0x18);
        magician_fireball_aim(obj);
        sound_cue(0xA01463);
        next();
        fly = true;
    } else if (sub == 2) {
        fly = true;
    }
    if (fly) {
        if ((of<uint16_t>(obj, 0x58) >> 8) & 1) {
            of<uint16_t>(obj, 0x70) = uint16_t(of<uint16_t>(obj, 0x70) - 1);
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x100u;
            if (of<int16_t>(obj, 0x70) <= 0)
                to_frame();
        }
        float speed = of<uint16_t>(mag, 0x66) == 0 ? mem.f32(rec + 0x20) : mem.f32(rec + 0x1C);
        if (of<int32_t>(obj, 0x32C) == 2 || mem.at<uint16_t>(G::players_in_game) == 2)
            speed = speed * 0.75f;
        const float dx = of<float>(obj, 0x3A0) - of<float>(obj, 0x20);
        const float dy = of<float>(obj, 0x3A4) - of<float>(obj, 0x24);
        const float dz = of<float>(obj, 0x3A8) - of<float>(obj, 0x28);
        of<uint32_t>(obj, 0x30) = yaw_toward_a(dx, dy, dz);
        const Tgp::Out o = boss_unit(fbits(dx), fbits(dy), fbits(dz));
        for (uint32_t i = 0; i < 3; ++i) {
            float v;
            std::memcpy(&v, &o.w[i], 4);
            of<float>(obj, 0x78 + 4 * i) = v * speed;
        }
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + of<float>(obj, 0x78);
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + of<float>(obj, 0x7C);
        of<uint32_t>(obj, 0x2C) = of<uint32_t>(obj, 0x2C) + 0x1000;
        of<uint32_t>(obj, 0x34) = of<uint32_t>(obj, 0x34) + 0x1000;
        const int32_t c = of<int32_t>(obj, 0x2EC) + 1;
        of<int32_t>(obj, 0x2EC) = c;
        of<float>(obj, 0x28) = of<float>(obj, 0x28) + of<float>(obj, 0x80);
        if (!(c < of<int32_t>(obj, 0x2F0))) {
            of<int32_t>(obj, 0x2EC) = 0;
            fireball_trail_copy(obj);
        }
        depth_list_add(obj);
        if (std::fabs(dx) <= speed && std::fabs(dy) <= speed && std::fabs(dz) <= speed) {
            obj_stop(obj);
            magician_fireball_hit(obj);
            to_frame();
        }
    }
    const int32_t a = of<int32_t>(obj, 0x330) - 1;
    of<int32_t>(obj, 0x330) = a;
    if (a <= 0) {
        uint32_t m = of<uint32_t>(obj, 0x54) + 1;
        of<uint32_t>(obj, 0x54) = m;
        if (m == 0x1BBF || int32_t(m) > 0x1CB9)
            of<uint32_t>(obj, 0x54) = 0x1CAB;
        const float s = of<float>(obj, 0x94) + of<float>(obj, 0x340);
        of<float>(obj, 0x94) = s;
        if (!(s < of<float>(obj, 0x33C)))
            of<uint32_t>(obj, 0x340) = 0;
    }
    magician_effect_matrix(obj);
    draw_fade_begin(of<int16_t>(obj, 0x2E0));
    geo_matrix_position(of<uint32_t>(obj, 0x54), obj + 0x38);
    cam_copro(0x77, {3});
    draw_fade_begin(0);
    geo_draw_model(of<uint32_t>(obj, 0x54));
    geo_send_words(0, 31, 0x7A0);
    cam_copro(0x06, {});
    if (of<uint16_t>(mag, 0x5C) == 3 || of<int16_t>(mag, 0x70) <= 0)
        to_frame();
}

// 0x736C0 magician_fireball_aim: a point on the screen (stored matrix 1, depth 4): in front of one player (side by
// the fireball's count +0x334, +-124 / with two players -164 .. 164) or the centre, a little random
template <class P> void Game<P>::magician_fireball_aim(uint32_t obj)
{
    float ox = 0.0f, oy = 0.0f;
    int32_t k = of<int32_t>(obj, 0x32C);
    if (mem.at<uint16_t>(G::players_in_game) == 2)
        k = 2;
    if (k >= 0 && k <= 2) {
        const int32_t n = of<int32_t>(obj, 0x334);
        if (k <= 1) {
            ox = n == 1 ? -124.0f : n == 2 ? 124.0f : 0.0f;
        } else {
            ox = n == 2 ? -82.0f : n == 1 ? -164.0f : n == 3 ? 82.0f : n == 4 ? 164.0f : 0.0f;
        }
        oy = 32.0f;
    }
    const uint32_t r1 = rand();
    const float x = float(int32_t(7 - (r1 & 15))) + ox;
    const uint32_t r2 = rand();
    const float y = float(int32_t(31 - (r2 & 0x3F))) + oy;
    // PC Mode: PC 0x4D3F1E multiplies by 0.0020161290 (1 / 496 as a float)
    const float px = pm_boss() ? (x * 4.0f) * 0.002016129f : (x * 4.0f) / 496.0f;
    const float py = pm_boss() ? (y * 4.0f) * 0.002016129f : (y * 4.0f) / 496.0f;
    const Tgp::Out o = boss_stored_xform(1, fbits(px), fbits(py), fbits(4.0f));
    of<uint32_t>(obj, 0x3A0) = o.w[0];
    of<uint32_t>(obj, 0x3A4) = o.w[1];
    of<uint32_t>(obj, 0x3A8) = o.w[2];
}

// 0x73820 magician_fireball_hit: with both players in the game (0x51F4A4) both in play are hit; else the one on its
// side of the screen
template <class P> void Game<P>::magician_fireball_hit(uint32_t obj)
{
    const float side = (of<float>(obj, 0x38) * 496.0f) / of<float>(obj, 0x40);
    if (mem.at<uint16_t>(0x51F4A4) == 1) {
        if (player_in_play(0) == 1)
            player_hit(0, 1, 4);
        if (player_in_play(1) == 1)
            player_hit(1, 1, 4);
        return;
    }
    if (side > 0.0f) {
        if (player_in_play(1) == 1)
            player_hit(1, 1, 4);
        return;
    }
    if (player_in_play(0) == 1)
        player_hit(0, 1, 4);
}

// 0x738D0 magician_fireball_spawn (magician, hand): one fireball (model 0x1B98, the record's hit points +0xB, trail
// rate +0xD / +0x11), counted in +0x31C
template <class P> void Game<P>::magician_fireball_spawn(uint32_t obj, int hand)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    const uint32_t t = task_create(F::magician_fireball, 0x3C8);
    task_clear_work(t);
    of<int32_t>(obj, 0x31C) = of<int32_t>(obj, 0x31C) + 1;
    mem.set32(t + 0x35C, obj);
    mem.set32(t + 0x320, uint32_t(hand));
    mem.set32(t + 0x328, 0);
    mem.set32(t + 0x58, 1);
    mem.set32(t + 0x32C, of<uint32_t>(obj, 0x32C));
    mem.set32(t + 0x94, 0);
    mem.set32(t + 0x90, 0);
    mem.set32(t + 0x2EC, 0);
    mem.set32(t + 0x2F4, 0);
    mem.set32(t + 0x33C, 0x3FE00000);   // 1.75
    mem.set32(t + 0x334, of<uint32_t>(obj, 0x31C));
    const int16_t hp = mem.at<int8_t>(rec + 0xB);
    mem.set32(t + 0x54, 0x1B98);
    mem.at<uint16_t>(t + 0x2E0) = 0xFF;
    mem.at<int16_t>(t + 0x72) = hp;
    mem.at<int16_t>(t + 0x70) = hp;
    mem.at<int16_t>(t + 0x60) = 0;
    mem.at<uint16_t>(t + 0x5C) = of<uint16_t>(obj, 0x5C);
    uint32_t delay;
    if (hand == 2) {
        mem.set32(t + 0x2F0, uint32_t(int32_t(mem.at<int8_t>(rec + 0x11))));
        delay = 0;
    } else {
        mem.set32(t + 0x2F0, uint32_t(int32_t(mem.at<int8_t>(rec + 0xD))));
        delay = of<int32_t>(obj, 0x32C) == 2 ? 35 : 0;
    }
    mem.set32(t + 0x340, 0x3F000000);   // 0.5
    mem.set32(t + 0x330, delay);
}

// 0x739D0 magician_intro_object (the intro's fireball into Dr. Curien): at the Magician's hand until his frame 68;
// then flies to (-1112.55, 10, 2027.59) (flag 226 on arrival), 45 frames, then magician_fireball_frame; trail, the
// animated model, drawn turned along its flight
template <class P> void Game<P>::magician_intro_object(uint32_t obj)
{
    const uint32_t mag = of<uint32_t>(obj, 0x35C);
    const uint32_t rec = of<uint32_t>(mag, 0x35C);
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    auto trail = [&] {
        const int32_t c = of<int32_t>(obj, 0x2EC) + 1;
        of<int32_t>(obj, 0x2EC) = c;
        if (!(c < of<int32_t>(obj, 0x2F0))) {
            of<int32_t>(obj, 0x2EC) = 0;
            fireball_trail_copy(obj);
        }
    };
    const int16_t sub = of<int16_t>(obj, 0x60);
    bool fly = false;
    if (sub == 0) {
        const Tgp::Out o = boss_stored_xform(1, mem.u32(mag + 0x238), mem.u32(mag + 0x23C), mem.u32(mag + 0x240));
        of<uint32_t>(obj, 0x20) = o.w[0];
        of<uint32_t>(obj, 0x24) = o.w[1];
        of<uint32_t>(obj, 0x28) = o.w[2];
        if (of<int16_t>(mag, 0xF4) > 0x43) {
            of<uint32_t>(obj, 0x3A0) = 0xC48B119A;   // -1112.55
            of<uint32_t>(obj, 0x3A4) = 0x41200000;   // 10.0
            of<uint32_t>(obj, 0x3A8) = 0x44FD72E1;   // 2027.59
            of<float>(obj, 0x94) = of<float>(obj, 0x94) * mem.f32(rec + 0xC);
            sound_cue(0xA01239);
            next();
            fly = true;
        }
    } else if (sub == 1) {
        fly = true;
    } else if (sub == 2 || sub == 3) {
        if (sub == 2) {
            of<int32_t>(obj, 0x2F4) = 45;
            next();
        }
        trail();
        const int32_t t = of<int32_t>(obj, 0x2F4) - 1;
        of<int32_t>(obj, 0x2F4) = t;
        if (t <= 0) {
            of<int16_t>(obj, 0x60) = 0;
            of<uint32_t>(obj, 0) = F::magician_fireball_frame;
        }
    }
    if (fly) {
        const float speed = mem.f32(rec + 8);
        const float dx = of<float>(obj, 0x3A0) - of<float>(obj, 0x20);
        const float dy = of<float>(obj, 0x3A4) - of<float>(obj, 0x24);
        const float dz = of<float>(obj, 0x3A8) - of<float>(obj, 0x28);
        if (pm_boss()) {   // PC 0x4D428A: the Magician file's pc_vec_angles copy (folded)
            uint32_t yaw = 0, pitch = 0;
            pm::vec_angles(dx, dy, dz, yaw, pitch);
            of<uint32_t>(obj, 0x30) = yaw;
            of<uint32_t>(obj, 0x2C) = pitch;
        } else {
            const uint32_t in[3] = {fbits(dx), fbits(dy), fbits(dz)};
            Tgp::Out a;
            copro(0x0F, in, 3, a);
            of<uint32_t>(obj, 0x30) = a.w[0];
            of<uint32_t>(obj, 0x2C) = a.w[1];
            const uint32_t s = of<uint16_t>(obj, 0x2C);
            if (((s << 1) ^ s) & 0x8000) {
                of<uint32_t>(obj, 0x2C) = 0x8000 - a.w[1];
                of<uint32_t>(obj, 0x30) = of<uint32_t>(obj, 0x30) + 0x8000;
            }
        }
        const Tgp::Out o = boss_unit(fbits(dx), fbits(dy), fbits(dz));
        for (uint32_t i = 0; i < 3; ++i) {
            float v;
            std::memcpy(&v, &o.w[i], 4);
            of<float>(obj, 0x78 + 4 * i) = v * speed;
        }
        if (std::fabs(dx) <= speed && std::fabs(dy) <= speed && std::fabs(dz) <= speed) {
            mem.at<uint8_t>(magician::flag(226)) = 1;
            obj_stop(obj);
            next();
        }
        trail();
    }
    const int32_t a = of<int32_t>(obj, 0x330) - 1;
    of<int32_t>(obj, 0x330) = a;
    if (a <= 0) {
        uint32_t m = of<uint32_t>(obj, 0x54) + 1;
        if (m == 0x1BBF || int32_t(m) > 0x1CB9)
            m = 0x1CAB;
        const float s = of<float>(obj, 0x94) + of<float>(obj, 0x340);
        of<uint32_t>(obj, 0x54) = m;
        of<float>(obj, 0x94) = s;
        if (!(s < of<float>(obj, 0x33C)))
            of<uint32_t>(obj, 0x340) = 0;
    }
    for (uint32_t i = 0; i < 3; ++i)
        of<float>(obj, 0x20 + 4 * i) = of<float>(obj, 0x20 + 4 * i) + of<float>(obj, 0x78 + 4 * i);
    magician_effect_matrix(obj);
    draw_fade_begin(of<int16_t>(obj, 0x2E0));
    geo_matrix_position(of<uint32_t>(obj, 0x54), obj + 0x38);
    cam_copro(0x77, {3});
    draw_fade_begin(0);
    geo_draw_model(of<uint32_t>(obj, 0x54));
    geo_send_words(0, 31, 0x7A0);
    cam_copro(0x06, {});
}

// 0x73E60 magician_intro_object_spawn (magician): the intro's fireball, counted in his +0x31C
template <class P> void Game<P>::magician_intro_object_spawn(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    const uint32_t t = task_create(F::magician_intro_object, 0x3C8);
    task_clear_work(t);
    of<int32_t>(obj, 0x31C) = of<int32_t>(obj, 0x31C) + 1;
    mem.set32(t + 0x35C, obj);
    mem.set32(t + 0x328, 0);
    mem.set32(t + 0x94, 0);
    mem.set32(t + 0x2EC, 0);
    mem.set32(t + 0x58, 1);
    mem.set32(t + 0x54, 0x1B98);
    mem.at<uint16_t>(t + 0x2E0) = 0xFF;
    mem.set32(t + 0x33C, 0x3FE00000);
    mem.set32(t + 0x330, 0);
    mem.set32(t + 0x340, 0x3F000000);
    mem.set32(t + 0x2F0, uint32_t(int32_t(mem.at<int8_t>(rec + 7))));
}

// 0x73EE0 fireball_trail: a fading copy: drifts (its gravity), shrinks 0.03, fades 5 a frame down to 90, then gone
template <class P> void Game<P>::fireball_trail(uint32_t obj)
{
    const float v = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
    const float s = of<float>(obj, 0x94) - 0.03f;
    const uint16_t f = uint16_t(of<uint16_t>(obj, 0x2E0) - 5);
    of<uint16_t>(obj, 0x2E0) = f;
    of<float>(obj, 0x7C) = v;
    of<float>(obj, 0x94) = s;
    of<float>(obj, 0x24) = of<float>(obj, 0x24) + v;
    if (int32_t(uint32_t(f) << 16) <= 0x5A0000) {
        of<uint16_t>(obj, 0x2E0) = 0x5A;
        task_exit();
        return;
    }
    magician_effect_matrix(obj);
    draw_fade_begin(of<int16_t>(obj, 0x2E0));
    geo_draw_model(of<uint32_t>(obj, 0x54));
    geo_send_words(0, 31, 0x7A0);
    cam_copro(0x06, {});
}

// 0x74030 fireball_trail_copy (fireball): a trail copy at its place and angles, its model and fade, 0.15 larger
template <class P> void Game<P>::fireball_trail_copy(uint32_t obj)
{
    const uint32_t t = task_create(F::fireball_trail, 0x3C8);
    task_clear_work(t);
    mem.set32(t + 0x35C, obj);
    mem.set32(t + 0x58, 1);
    for (uint32_t i = 0; i < 6; ++i)
        mem.set32(t + 0x20 + 4 * i, of<uint32_t>(obj, 0x20 + 4 * i));
    const uint32_t r = rand();
    mem.set32(t + 0x88, fbits(float(int32_t((r & 3) + 1)) * 0.0015f));
    mem.at<uint16_t>(t + 0x2E0) = of<uint16_t>(obj, 0x2E0);
    mem.set32(t + 0x54, of<uint32_t>(obj, 0x54));
    mem.set32(t + 0x94, fbits(of<float>(obj, 0x94) + 0.15f));
}

// 0x740D0 magician_afterimage_spawn: an afterimage (model 0x1C20) at his home point +0x2D4 with his angles and fade
template <class P> void Game<P>::magician_afterimage_spawn(uint32_t obj)
{
    const uint32_t t = task_create(F::magician_afterimage, 0x3C8);
    task_clear_work(t);
    mem.set32(t + 0x35C, obj);
    mem.set32(t + 0x58, 1);
    for (uint32_t i = 0; i < 3; ++i) {
        mem.set32(t + 0x20 + 4 * i, of<uint32_t>(obj, 0x2D4 + 4 * i));
        mem.set32(t + 0x2C + 4 * i, of<uint32_t>(obj, 0x2C + 4 * i));
    }
    const uint16_t f = of<uint16_t>(obj, 0x2E0);
    mem.set32(t + 0x54, 0x1C20);
    mem.set32(t + 0x94, 0x3F800000);
    mem.at<uint16_t>(t + 0x2E0) = f;
}

// 0x74150 magician_barrage_shot_frame (a barrage shot, owner the Magician): circles him on camera path 102 (radius
// to 15, speed to the record's +0x50) until his frame 245; then rises to a point above, waits; then flies at the
// player its target chose (the record's points by player) and hits; shot down after its hit points; the model
// animates 0x1D21..0x1D34, grows
template <class P> void Game<P>::magician_barrage_shot_frame(uint32_t obj)
{
    const uint32_t mag = of<uint32_t>(obj, 0x35C);
    const uint32_t rec = of<uint32_t>(mag, 0x35C);
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    auto to_frame = [&] {
        of<int16_t>(obj, 0x60) = 0;
        of<uint32_t>(obj, 0) = F::magician_fireball_frame;
    };
    auto move = [&](float speed) {   // normalize(target - position) x speed into velocity and position
        const float dx = of<float>(obj, 0x3A0) - of<float>(obj, 0x20);
        const float dy = of<float>(obj, 0x3A4) - of<float>(obj, 0x24);
        const float dz = of<float>(obj, 0x3A8) - of<float>(obj, 0x28);
        const Tgp::Out o = boss_unit(fbits(dx), fbits(dy), fbits(dz));
        for (uint32_t i = 0; i < 3; ++i) {
            float v;
            std::memcpy(&v, &o.w[i], 4);
            of<float>(obj, 0x78 + 4 * i) = v * speed;
        }
        for (uint32_t i = 0; i < 3; ++i)
            of<float>(obj, 0x20 + 4 * i) = of<float>(obj, 0x20 + 4 * i) + of<float>(obj, 0x78 + 4 * i);
        return std::array<float, 3>{dx, dy, dz};
    };
    const uint32_t kit = of<uint32_t>(mag, 0x32C);
    switch (uint32_t(int32_t(of<int16_t>(obj, 0x60)))) {
    case 0:
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & ~8u;
        of<uint32_t>(obj, 0x344) = 0;
        of<uint32_t>(obj, 0x348) = 0;
        obj_stop(obj);
        next();
        of<uint32_t>(obj, 0x398) = of<uint32_t>(obj, 0x24);
        [[fallthrough]];
    case 1: {
        if (!pm_boss()) {
            cam_copro(0x21, {});
            cam_copro(0x88, {of<uint32_t>(mag, 0x2D4), of<uint32_t>(obj, 0x398), of<uint32_t>(mag, 0x2DC)});
        }
        float rr = of<float>(obj, 0x344) + 0.2f;
        of<float>(obj, 0x344) = rr;
        if (rr > 15.0f)
            of<uint32_t>(obj, 0x344) = 0x41700000;
        const float sp = of<float>(obj, 0x348) + 0.04f;
        of<float>(obj, 0x348) = sp;
        if (sp > mem.f32(rec + 0x50))
            of<uint32_t>(obj, 0x348) = mem.u32(rec + 0x50);
        const float ph = of<float>(obj, 0x314) + of<float>(obj, 0x348);
        of<float>(obj, 0x314) = ph;
        if (ph > mem.f32(rec + 0x54) - 1.0f)
            of<uint32_t>(obj, 0x314) = 0;
        if (of<float>(obj, 0x314) < 0.0f)
            of<float>(obj, 0x314) = mem.f32(rec + 0x54) - 1.0f;
        LocalScope scope(*this);
        const uint32_t loc = local_alloc(0x48);
        camera_path_eval(0x66, boss_path_frame(of<uint32_t>(obj, 0x314)), loc, loc + 0x20, loc + 0x40, loc + 0x44);
        const float s = of<float>(obj, 0x344);
        if (pm_boss()) {
            // PC 0x4D4A1D: the identity with the Magician's home point (x, z) and the start height as its
            // translation, the path point scaled (the Magician file's transform 0x4CF570)
            pm::Matrix m;
            m.e[9] = of<float>(mag, 0x2D4);
            m.e[10] = of<float>(obj, 0x398);
            m.e[11] = of<float>(mag, 0x2DC);
            float r[3];
            m.transform(mem.f32(loc) * s, mem.f32(loc + 4) * s, mem.f32(loc + 8) * s, r[0], r[1], r[2]);
            of<float>(obj, 0x20) = r[0];
            of<float>(obj, 0x24) = r[1];
            of<float>(obj, 0x28) = r[2];
        } else {
            const Tgp::Out o = cam_copro(0x1A, {fbits(mem.f32(loc) * s), fbits(mem.f32(loc + 4) * s), fbits(mem.f32(loc + 8) * s)});
            of<uint32_t>(obj, 0x20) = o.w[0];
            of<uint32_t>(obj, 0x24) = o.w[1];
            of<uint32_t>(obj, 0x28) = o.w[2];
            cam_copro(0x06, {});
        }
        if (of<int16_t>(mag, 0xF4) <= 0xF4)
            break;
        sound_cue(0xA01463);
        const Tgp::Out p = magician_from_camera(mag, obj);
        of<uint32_t>(obj, 0x3A0) = p.w[0];
        of<uint32_t>(obj, 0x3A4) = p.w[1];
        of<uint32_t>(obj, 0x3A8) = p.w[2];
        of<uint32_t>(obj, 0x3A4) = 0x43480000;   // 200.0
        next();
    }
        [[fallthrough]];
    case 2: {
        of<uint32_t>(obj, 0x2C) = of<uint32_t>(obj, 0x2C) + 0x1000;
        of<uint32_t>(obj, 0x34) = of<uint32_t>(obj, 0x34) + 0x1000;
        const float speed = mem.f32(rec + 0x58) * mem.f32(magician::shot_speeds + 12 * kit);
        const auto d = move(speed);
        if (std::fabs(d[1]) > speed)
            break;
        of<uint32_t>(obj, 0x2EC) = mem.u32(magician::shot_speeds + 8 + 12 * kit);
        next();
    }
        [[fallthrough]];
    case 3: {
        const int32_t t = of<int32_t>(obj, 0x2EC) - 1;
        of<int32_t>(obj, 0x2EC) = t;
        if (t > 0)
            break;
        next();
    }
        [[fallthrough]];
    case 4: {
        pick_target_player_any(obj);
        float px = 0.0f, py = 0.0f;
        const int8_t tp = of<int8_t>(obj, 0x2E8);
        if (tp == 0) {
            px = mem.f32(rec + 0x68);
            py = mem.f32(rec + 0x6C);
        } else if (tp == 1) {
            px = mem.f32(rec + 0x70);
            py = mem.f32(rec + 0x74);
        }
        if (mem.at<uint16_t>(G::players_in_game) == 1) {
            px = mem.f32(rec + 0x78);
            py = mem.f32(rec + 0x7C);
        }
        const float z = mem.f32(rec + 0x80);
        // PC Mode: PC 0x4D4E56 multiplies by 0.0020161290 (1 / 496 as a float)
        const float x = pm_boss() ? (px * z) * 0.002016129f : (px * z) / 496.0f;
        const float y = pm_boss() ? (py * z) * 0.002016129f : (py * z) / 496.0f;
        const Tgp::Out o = boss_stored_xform(1, fbits(x), fbits(y), fbits(z));
        of<uint32_t>(obj, 0x3A0) = o.w[0];
        of<uint32_t>(obj, 0x3A4) = o.w[1];
        of<uint32_t>(obj, 0x3A8) = o.w[2];
        const float base = of<uint16_t>(mag, 0x66) == 0 ? mem.f32(rec + 0x60) : mem.f32(rec + 0x5C);
        of<float>(obj, 0x34C) = base * mem.f32(magician::shot_speeds + 4 + 12 * kit);
        if (mem.at<uint16_t>(G::players_in_game) == 2)
            of<float>(obj, 0x34C) = of<float>(obj, 0x34C) * mem.f32(rec + 0x64);
        next();
    }
        [[fallthrough]];
    case 5: {
        of<uint32_t>(obj, 0x2C) = of<uint32_t>(obj, 0x2C) + 0x1000;
        of<uint32_t>(obj, 0x34) = of<uint32_t>(obj, 0x34) + 0x1000;
        const auto d = move(of<float>(obj, 0x34C));
        const float s = of<float>(obj, 0x34C);
        if (!(std::fabs(d[0]) <= s && std::fabs(d[1]) <= s && std::fabs(d[2]) <= s))
            break;
        next();
    }
        [[fallthrough]];
    case 6: {
        const uint8_t tp = of<uint8_t>(obj, 0x2E8);
        if (tp == 0 || tp == 1)
            player_hit(of<int8_t>(obj, 0x2E8), 1, 4);
        to_frame();
        break;
    }
    default: break;
    }
    if (((of<uint16_t>(obj, 0x58) >> 8) & 1) && (of<uint8_t>(obj, 0x58) & 1)) {
        of<uint16_t>(obj, 0x70) = uint16_t(of<uint16_t>(obj, 0x70) - 1);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x100u;
        if (of<int16_t>(obj, 0x70) <= 0) {
            to_frame();
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
        }
    }
    if (of<uint8_t>(obj, 0x58) & 1) {
        uint32_t m = of<uint32_t>(obj, 0x54) + 1;
        of<uint32_t>(obj, 0x54) = m;
        if (int32_t(m) > 0x1D34)
            of<uint32_t>(obj, 0x54) = 0x1D34 - 19;
        const float s = of<float>(obj, 0x94) + of<float>(obj, 0x340);
        of<float>(obj, 0x94) = s;
        of<float>(obj, 0x90) = s * mem.f32(rec + 0x4C);
        if (!(of<float>(obj, 0x94) < of<float>(obj, 0x33C)))
            of<uint32_t>(obj, 0x340) = 0;
    }
    magician_effect_matrix(obj);
    draw_fade_begin(of<int16_t>(obj, 0x2E0));
    geo_matrix_position(of<uint32_t>(obj, 0x54), obj + 0x38);
    geo_send_words(0, 31, 0x7A0);
    cam_copro(0x06, {});
    depth_list_add(obj);
    if (of<uint16_t>(mag, 0x5C) == 3 || of<int16_t>(mag, 0x70) <= 0) {
        to_frame();
        of<uint32_t>(obj, 0x94) = of<uint32_t>(obj, 0x33C);
    }
}

// 0x749F0 magician_barrage_shot (magician, part): a barrage shot task at the part (stored matrix 1), model 0x1D20, the
// record's hit points +0x12
template <class P> void Game<P>::magician_barrage_shot(uint32_t obj, uint32_t part)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    const uint32_t t = task_create(magician::barrage_shot, 0x3C8);
    task_clear_work(t);
    mem.set32(t + 0x58, 1);
    mem.set32(t + 0x54, 0x1D20);
    mem.at<uint16_t>(t + 0x2E0) = 0xFF;
    mem.set32(t + 0x35C, obj);
    mem.at<int16_t>(t + 0x60) = 0;
    mem.set32(t + 0x94, 0);
    mem.set32(t + 0x90, 0);
    mem.set32(t + 0x2F4, 0);
    mem.set32(t + 0x33C, 0x3FE00000);
    const int16_t hp = mem.at<int8_t>(rec + 0x12);
    mem.at<int16_t>(t + 0x72) = hp;
    mem.at<int16_t>(t + 0x70) = hp;
    const Tgp::Out o = boss_stored_xform(1, mem.u32(part + 0x5C), mem.u32(part + 0x60), mem.u32(part + 0x64));
    mem.set32(t + 0x20, o.w[0]);
    mem.set32(t + 0x24, o.w[1]);
    mem.set32(t + 0x32C, 0);
    mem.set32(t + 0x340, 0x3C23D70A);   // 0.01
    mem.set32(t + 0x28, o.w[2]);
}

// 0x74AD0 magician_fireball_frame (a fireball / shot's end): 1.5x, not shootable, the Magician's count one less; fades
// out at the record's rate (+0x88 frames, down to +0x8C), then gone
template <class P> void Game<P>::magician_fireball_frame(uint32_t obj)
{
    const uint32_t mag = of<uint32_t>(obj, 0x35C);
    const uint32_t rec = of<uint32_t>(mag, 0x35C);
    const int16_t sub = of<int16_t>(obj, 0x60);
    if (sub == 0) {
        const float s = of<float>(obj, 0x94) * 1.5f;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
        of<int32_t>(mag, 0x31C) = of<int32_t>(mag, 0x31C) - 1;
        of<uint16_t>(obj, 0x2E0) = 0xFF;
        of<float>(obj, 0x94) = s;
        of<uint32_t>(obj, 0x2F0) = mem.u32(rec + 0x88);
        const int32_t lo = mem.at<int32_t>(rec + 0x8C);
        of<int32_t>(obj, 0x31C) = lo;
        of<float>(obj, 0x30C) = float((0xFF - lo) / of<int32_t>(obj, 0x2F0));
        sound_cue(0xA01464);
        of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
    }
    if (sub == 0 || sub == 1) {
        const float f = float(int32_t(of<int16_t>(obj, 0x2E0))) - of<float>(obj, 0x30C);
        const int32_t v = int32_t(f);
        of<uint16_t>(obj, 0x2E0) = uint16_t(v);
        if (!(int32_t(int16_t(v)) > of<int32_t>(obj, 0x31C))) {
            task_exit();
            return;
        }
    }
    magician_effect_matrix(obj);
    draw_fade_begin(of<int16_t>(obj, 0x2E0));
    geo_draw_model(of<uint32_t>(obj, 0x54));
    geo_send_words(0, 31, 0x7A0);
    cam_copro(0x06, {});
}

// 0x74C70 magician_barrage_projectile (the barrage's helper, an enemy slot and an attacker entry): above the Magician
// until his frame 245, then rises to a point 200 up and on; gone with the barrage
template <class P> void Game<P>::magician_barrage_projectile(uint32_t obj)
{
    const uint32_t mag = of<uint32_t>(obj, 0x35C);
    if (of<int16_t>(mag, 0x70) <= 0 || of<int32_t>(mag, 0x31C) <= 0 ||
        mem.at<int32_t>(magician::pattern + 8 * of<uint32_t>(mag, 0x330)) != 6) {
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
        const int32_t slot = of<int8_t>(obj, 0x2E7);
        mem.at<uint8_t>(G::attack_slots + uint32_t(slot * 20)) = 0;
        task_exit();
        return;
    }
    const int16_t sub = of<int16_t>(obj, 0x60);
    bool fly = sub == 1;
    if (sub == 0) {
        of<uint32_t>(obj, 0x20) = of<uint32_t>(mag, 0x20);
        of<float>(obj, 0x24) = of<float>(mag, 0x24) + 30.0f;
        of<uint32_t>(obj, 0x28) = of<uint32_t>(mag, 0x28);
        if (of<int16_t>(mag, 0xF4) > 0xF4) {
            attack_slot_front_take(obj);
            const Tgp::Out o = magician_from_camera(mag, obj);
            of<uint32_t>(obj, 0x3A0) = o.w[0];
            of<uint32_t>(obj, 0x3A4) = o.w[1];
            of<uint32_t>(obj, 0x3A4) = 0x43480000;   // 200.0
            of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
            of<uint32_t>(obj, 0x3A8) = o.w[2];
            fly = true;
        }
    }
    if (fly) {
        const float dx = of<float>(obj, 0x3A0) - of<float>(obj, 0x20);
        const float dy = of<float>(obj, 0x3A4) - of<float>(obj, 0x24);
        const float dz = of<float>(obj, 0x3A8) - of<float>(obj, 0x28);
        const float speed = mem.f32(magician::shot_speeds + 12 * of<uint32_t>(mag, 0x32C));
        const Tgp::Out o = boss_unit(fbits(dx), fbits(dy), fbits(dz));
        float v[3];
        for (uint32_t i = 0; i < 3; ++i)
            std::memcpy(&v[i], &o.w[i], 4);
        const float vy = speed * v[1], vz = speed * v[2], vx = speed * v[0];
        of<float>(obj, 0x7C) = vy;
        of<float>(obj, 0x80) = vz;
        of<float>(obj, 0x78) = vx;
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + vx;
        of<float>(obj, 0x28) = of<float>(obj, 0x28) + vz;
        if (std::fabs(dy) <= speed)
            of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
    }
    for (uint32_t i = 0; i < 3; ++i)
        of<uint32_t>(obj, 0x2D4 + 4 * i) = of<uint32_t>(obj, 0x20 + 4 * i);
    attacker_list_add(obj);
}

// 0x74EC0 magician_barrage_spawn (magician): the barrage's helper at his place, an enemy slot
template <class P> void Game<P>::magician_barrage_spawn(uint32_t obj)
{
    const uint32_t t = task_create(magician::barrage_proj, 0x3C8);
    task_clear_work(t);
    mem.set32(t + 0x35C, obj);
    mem.set32(t + 0x58, 1);
    mem.set32(t + 0x20, of<uint32_t>(obj, 0x20));
    mem.set32(t + 0x24, of<uint32_t>(obj, 0x24));
    mem.at<int8_t>(t + 0x2E7) = -1;
    mem.at<int16_t>(t + 0x60) = 0;
    mem.set32(t + 0x28, of<uint32_t>(obj, 0x28));
    enemy_slot_take(t);
}

// 0x74F20 magician_tracker_spawn (magician): a tracker (enemy slot, a front attack slot) following his home point
template <class P> void Game<P>::magician_tracker_spawn(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    const uint32_t t = task_create(F::magician_tracker, 0x3C8);
    task_clear_work(t);
    of<int32_t>(obj, 0x304) = of<int32_t>(obj, 0x304) + 1;
    mem.set32(t + 0x35C, obj);
    mem.set32(t + 0x31C, 0);
    mem.set32(t + 0x58, 1);
    mem.set32(t + 0x320, mem.at<uint8_t>(rec + 0x14));
    mem.set32(t + 0x328, of<uint32_t>(obj, 0x328));
    mem.set32(t + 0x2EC, 0);
    mem.set32(t + 0x330, of<uint32_t>(obj, 0x330));
    mem.set32(t + 0x2F0, uint32_t(int32_t(mem.at<int8_t>(rec + 0x15))));
    mem.set32(t + 0x20, of<uint32_t>(obj, 0x20));
    mem.set32(t + 0x24, of<uint32_t>(obj, 0x24));
    mem.at<int8_t>(t + 0x2E7) = -1;
    mem.at<int16_t>(t + 0x60) = 0;
    mem.set32(t + 0x28, of<uint32_t>(obj, 0x28));
    enemy_slot_take(t);
    attack_slot_front_take(t);
}

// 0x74FC0 magician_part_effect_task: effect models 0x1EB..0x1FF at a part; the part flashes (+0x3C bit 8) on even ones
template <class P> void Game<P>::magician_part_effect_task(uint32_t obj)
{
    const uint32_t part = of<uint32_t>(obj, 0x35C);
    const uint32_t m = of<uint32_t>(obj, 0x54) + 1;
    of<uint32_t>(obj, 0x54) = m;
    if (int32_t(m) > 0x1FF) {
        task_exit();
        return;
    }
    if (of<int32_t>(obj, 0x54) % 2 == 0)
        mem.set32(part + 0x3C, mem.u32(part + 0x3C) | 0x100u);
    else
        mem.set32(part + 0x3C, mem.u32(part + 0x3C) & ~0x100u);
    magician_effect_matrix(obj);
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

// 0x750D0 magician_part_effect (part): an effect task at the part (stored matrix 1, 1 nearer), random angles, size
// 1..4 (half while dying with the camera's drift); the body's +0x2F4 to 8
template <class P> void Game<P>::magician_part_effect(uint32_t part)
{
    const uint32_t t = task_create(F::magician_part_effect_task, 0x3C8);
    task_clear_work(t);
    const Tgp::Out o = boss_stored_xform(1, mem.u32(part + 0x5C), mem.u32(part + 0x60), fbits(mem.f32(part + 0x64) - 1.0f));
    mem.set32(t + 0x20, o.w[0]);
    mem.set32(t + 0x24, o.w[1]);
    mem.set32(t + 0x35C, part);
    mem.set32(t + 0x54, 0x1EA);
    mem.set32(t + 0x360, mem.u32(part + 0x40));
    mem.set32(t + 0x28, o.w[2]);
    mem.set32(t + 0x30, rand() & 0xFFFF);
    mem.set32(t + 0x2C, rand() & 0xFFFF);
    mem.set32(t + 0x34, rand() & 0xFFFF);
    const uint32_t body = mem.u32(part + 0x40);
    float s;
    if (mem.at<uint16_t>(body + 0x5C) == 1 && mem.at<int16_t>(body + 0x60) > 3 && mem.at<int16_t>(body + 0x60) <= 13)
        s = float(int32_t((rand() & 3) + 1)) * 0.5f;
    else
        s = float(int32_t((rand() & 3) + 1));
    mem.set32(t + 0x94, fbits(s));
    if (mem.at<int32_t>(body + 0x2F4) <= 0)
        mem.set32(body + 0x2F4, 8);
}

// 0x75200 magician_death_flash: shrinks away, 15 frames, then the PN_bosssaigo models 0x1CBA..0x1CE0 (one each 3
// frames; the Magician hidden, sound 0xA0146F); at 0x1CC2 the fade to black (flag 218, the record's +0xA0 frames)
template <class P> void Game<P>::magician_death_flash(uint32_t obj)
{
    const uint32_t mag = of<uint32_t>(obj, 0x35C);
    const uint32_t rec = of<uint32_t>(mag, 0x35C);
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    const int16_t sub = of<int16_t>(obj, 0x60);
    bool c1 = sub == 1, c2 = sub == 2;
    if (sub == 0) {
        const float s = float(double(of<float>(obj, 0x94)) - 0.03);
        of<float>(obj, 0x94) = s;
        if (!(s > 0.0f)) {
            of<uint32_t>(obj, 0x94) = 0;
            of<int32_t>(obj, 0x50) = 15;
            next();
            c1 = true;
        }
    }
    if (c1) {
        const int32_t t = of<int32_t>(obj, 0x50) - 1;
        of<int32_t>(obj, 0x50) = t;
        if (t <= 0) {
            of<uint32_t>(obj, 0x54) = 0x1CBA;
            of<uint32_t>(obj, 0x94) = 0x3F800000;
            of<uint32_t>(mag, 0x58) = of<uint32_t>(mag, 0x58) | (1u << 25);
            sound_cue(0xA0146F);
            of<int32_t>(obj, 0x50) = 0;
            next();
            c2 = true;
        }
    }
    if (c2) {
        const int32_t t = of<int32_t>(obj, 0x50) + 1;
        of<int32_t>(obj, 0x50) = t;
        if (t > 2) {
            of<int32_t>(obj, 0x50) = 0;
            const uint32_t m = of<uint32_t>(obj, 0x54) + 1;
            of<uint32_t>(obj, 0x54) = m;
            if (m == 0x1CC2 && mem.at<uint8_t>(magician::flag(218)) == 0) {
                mem.at<uint8_t>(magician::flag(218)) = 1;
                palette_fade_out_start(mem.u32(rec + 0xA0));
            }
            if (int32_t(of<uint32_t>(obj, 0x54)) > 0x1CE0) {
                task_exit();
                return;
            }
        }
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    const uint32_t s = of<uint32_t>(obj, 0x94);
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    cam_copro(0x13, {s, s, s});
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

// 0x753B0 magician_death_flash_spawn (magician): the death flash at his home point, model 0x1CBF, full size
template <class P> void Game<P>::magician_death_flash_spawn(uint32_t obj)
{
    const uint32_t t = task_create(F::magician_death_flash, 0x3C8);
    task_clear_work(t);
    mem.set32(t + 0x35C, obj);
    mem.set32(t + 0x20, of<uint32_t>(obj, 0x2D4));
    mem.set32(t + 0x24, of<uint32_t>(obj, 0x2D8));
    mem.set32(t + 0x30, 0);
    mem.set32(t + 0x50, 0);
    mem.set32(t + 0x54, 0x1CBF);
    mem.at<int16_t>(t + 0x60) = 0;
    mem.set32(t + 0x94, 0x3F800000);
    mem.set32(t + 0x28, of<uint32_t>(obj, 0x2DC));
}

// 0x75410 magician_afterimage: fades 5 a frame down to 90, drawn with its fade, then gone
template <class P> void Game<P>::magician_afterimage(uint32_t obj)
{
    const uint16_t f = uint16_t(of<uint16_t>(obj, 0x2E0) - 5);
    of<uint16_t>(obj, 0x2E0) = f;
    if (!(int32_t(uint32_t(f) << 16) > 0x5A0000)) {
        of<uint16_t>(obj, 0x2E0) = 0x5A;
        task_exit();
        return;
    }
    magician_effect_matrix(obj);
    draw_fade_begin(of<int16_t>(obj, 0x2E0));
    geo_draw_model(of<uint32_t>(obj, 0x54));
    geo_send_words(0, 31, 0x7A0);
    cam_copro(0x06, {});
}

// 0x75530 magician_tracker: while the Magician lives in the same pattern step (and not past a swoop round), jumps to
// his home point every +0x320 x +0x2F0 frames; an attacker entry; else frees its slot and ends
template <class P> void Game<P>::magician_tracker(uint32_t obj)
{
    const uint32_t mag = of<uint32_t>(obj, 0x35C);
    if (of<int16_t>(mag, 0x70) <= 0 || of<uint32_t>(mag, 0x330) != of<uint32_t>(obj, 0x330) ||
        (of<uint16_t>(mag, 0x5C) == 5 && of<int16_t>(mag, 0x60) > 9)) {
        of<int32_t>(mag, 0x304) = 0;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~1u;
        const int32_t slot = of<int8_t>(obj, 0x2E7);
        mem.at<uint8_t>(G::attack_slots + uint32_t(slot * 20)) = 0;
        task_exit();
        return;
    }
    if (of<uint32_t>(obj, 0x328) != of<uint32_t>(mag, 0x328)) {
        of<uint32_t>(obj, 0x328) = of<uint32_t>(mag, 0x328);
        of<int32_t>(obj, 0x31C) = 0;
    }
    const int32_t c = of<int32_t>(obj, 0x2EC) + 1;
    of<int32_t>(obj, 0x2EC) = c;
    if (c > of<int32_t>(obj, 0x2F0)) {
        of<int32_t>(obj, 0x2EC) = 0;
        const int32_t k = of<int32_t>(obj, 0x31C) + 1;
        of<int32_t>(obj, 0x31C) = k;
        if (!(k < of<int32_t>(obj, 0x320))) {
            of<int32_t>(obj, 0x31C) = 0;
            for (uint32_t i = 0; i < 3; ++i)
                of<uint32_t>(obj, 0x20 + 4 * i) = of<uint32_t>(mag, 0x2D4 + 4 * i);
        }
    }
    for (uint32_t i = 0; i < 3; ++i)
        of<uint32_t>(obj, 0x2D4 + 4 * i) = of<uint32_t>(obj, 0x20 + 4 * i);
    attacker_list_add(obj);
}

} // namespace hotd::port
