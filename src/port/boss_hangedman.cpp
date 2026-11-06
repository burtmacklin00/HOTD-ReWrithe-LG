// The Hangedman's fight (kind 18 set-up 1, Chapter 2 event 15) and rematch (set-up 2, Chapter 4), with its captives,
// intro, life bar and death effects, and kind 19 (the event's billboard), worker "hanged". Read from the arcade
// disassembly (tools/i960dis.py) with the typed decompilation as a guide; arcade forms only. Proven on A (hanged_full,
// 0 differ); the set-ups, the captives and the rematch intro: ch2proof (docs/teaser_status/ch2proof.md, 0 differ).
//
// The object: +0x35C the set-up block (u16 hit points, byte 4 type, bytes 6 / 7 hit-point shares in tenths for the
// bat call-off / the flight, 9 / 0xA hits to break a claw / dive, 0xB / 0xC claw start frames, floats 0x20.. speeds
// and the dive path's scale / offset), +0x5C state (table 0xC1150), +0x60 step, +0x66 aggression level
// (boss_aggression_update), +0x320 attack pattern (spin[1]), +0x324 flags (bit 3 not hurt, bit 11 off the attacker
// list, bit 29 pattern reset), +0x32C swarm bats left (bat_setup_hangedman_swarm), +0x330 row of hangedman_bat_waves,
// +0x338 hit reaction step, +0x3A0 target point, +0x394 dive start, +0x3AC the camera reference.
// Event flags (0x51E940 + n): 192 life bar up, 193 back after the bats, 194 gone (fled), 195 / 196 the captives drop /
// the camera back, 197 swarm called off, 198 / 200 the event's go-aheads for the dive, 199 back in place, 201 the
// event's signal to come back, 202 captives fall; 253 ends the billboard.
#include "port/game.h"

#include <cmath>

namespace hotd::port {

namespace hangedman {
constexpr uint32_t hover_times = 0xC11A0;   // frames hovered by the attack pattern (+0x320)
}

// the camera's direction from the object (coprocessor 0x0F, folded) as its heading +0x30
template <class P> void Game<P>::hangedman_face_camera(uint32_t obj)
{
    of<uint32_t>(obj, 0x30) = yaw_toward_a(mem.f32(G::camera_pos) - of<float>(obj, 0x20),
                                           mem.f32(G::camera_pos + 4) - of<float>(obj, 0x24),
                                           mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28));
}

// coprocessor 0x0F on a direction: heading and pitch, folded (pitch 0x8000 - p, heading + 0x8000)
template <class P> void Game<P>::hangedman_angles(float dx, float dy, float dz, uint32_t& yaw, uint32_t& pitch)
{
    if (pm_boss()) {   // PC Mode: the Hangedman file's pc_vec_angles copy 0x497158 (it folds the pitch itself)
        pm::vec_angles(dx, dy, dz, yaw, pitch);
        return;
    }
    const uint32_t in[3] = {fbits(dx), fbits(dy), fbits(dz)};
    Tgp::Out o;
    copro(0x0F, in, 3, o);
    yaw = o.w[0];
    pitch = o.w[1];
    const uint32_t s = pitch & 0xFFFF;
    if (((s << 1) ^ s) & 0x8000) {
        pitch = 0x8000 - pitch;
        yaw += 0x8000;
    }
}

// (hit points max / 10) x the set-up block's signed byte: the hit-point share tests
template <class P> int32_t Game<P>::hangedman_share(uint32_t obj, uint32_t byteOff)
{
    const uint32_t blk = of<uint32_t>(obj, 0x35C);
    const int32_t tenth = int32_t(int16_t(of<int16_t>(obj, 0x72) / 10));
    return tenth * int32_t(mem.at<int8_t>(blk + byteOff));
}

// frees the attack position (0x51F9F0 + 20 x slot)
template <class P> void Game<P>::hangedman_slot_free(uint32_t obj)
{
    const int32_t slot = of<int8_t>(obj, 0x2E7);
    mem.at<uint8_t>(G::attack_slots + uint32_t(slot * 20)) = 0;
}

// 0x4F960 (set-up 1): the block's type and hit points, the body (MO 22), counted alive, an enemy slot, facing the
// camera; hangedman_fight_frame, state 3 (intro); the two captives
template <class P> void Game<P>::hangedman_fight_init(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x35C);
    mem.set32(G::hangedman_obj, obj);
    const uint8_t type = mem.at<uint8_t>(blk + 4);
    of<uint16_t>(obj, 0xCC) = type;
    mem.at<int32_t>(G::hit_type) = type;
    body_init(obj, type, 22);
    creature_type_setup(obj);
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
    of<int8_t>(obj, 0x2E7) = -1;
    enemy_slot_take(obj);
    hangedman_face_camera(obj);
    of<uint16_t>(obj, 0x2E0) = 0xFF;
    of<uint16_t>(obj, 0x6C) = uint16_t(of<uint16_t>(obj, 0x6C) & ~2u);
    const uint16_t hp = mem.at<uint16_t>(blk);
    of<uint32_t>(obj, 0) = F::hangedman_fight_frame;
    of<int16_t>(obj, 0x5C) = 3;
    of<int32_t>(obj, 0x2F4) = 0;
    of<int32_t>(obj, 0x318) = 0;
    of<int16_t>(obj, 0x60) = 0;
    of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) | 4);
    of<uint16_t>(obj, 0x72) = hp;
    of<uint16_t>(obj, 0x70) = hp;
    hangedman_captives();
}

// 0x4FAA0: aggression, the bob (0.1 a frame; +0x50 counts 3 a frame while rising), hits and the hit reaction, the
// state routine unless frozen (flags bit 27), velocity + acceleration, drawn unless hidden (bit 25), motion frames,
// depth list, attacker list unless +0x324 bit 11
template <class P> void Game<P>::hangedman_fight_frame(uint32_t obj)
{
    boss_aggression_update(obj);
    float y = of<float>(obj, 0x24);
    int32_t t = of<int32_t>(obj, 0x50) + 1;
    if (y - of<float>(obj, 0x344) > of<float>(obj, 0x340))
        t += 2;
    of<int32_t>(obj, 0x50) = t;
    of<float>(obj, 0x340) = y;
    const int32_t type = of<int16_t>(obj, 0xCC);
    const int32_t model = of<int32_t>(obj, 0x54);
    float step;
    if ((type == 50 && model <= 0x1432) || (type == 0x45 && model <= 0x1990)) {
        y = y - 0.1f;
        step = -0.1f;
    } else {
        step = 0.1f;
        y = y + 0.1f;
    }
    of<float>(obj, 0x344) = step;
    of<uint16_t>(obj, 0x2E0) = 0xFF;
    of<float>(obj, 0x24) = y;
    if ((of<uint32_t>(obj, 0x58) & 1) != 0 && (of<uint8_t>(obj, 0x324) & 8) == 0)
        hangedman_hit(obj);
    if (((of<uint32_t>(obj, 0x58) >> 28) & 1) != 0)
        hangedman_hit_reaction(obj);
    if (((of<uint32_t>(obj, 0x58) >> 27) & 1) == 0) {
        const uint32_t r = mem.u32(G::hangedman_states + uint32_t(int32_t(of<int16_t>(obj, 0x5C))) * 4);
        switch (r) {
        case F::hangedman_intro: hangedman_intro(obj); break;
        case F::hangedman_flap: hangedman_flap(obj); break;
        case F::hangedman_rejoin: hangedman_rejoin(obj); break;
        case F::hangedman_hover: hangedman_hover(obj); break;
        case F::hangedman_claw_attack: hangedman_claw_attack(obj); break;
        case F::hangedman_return: hangedman_return(obj); break;
        case F::hangedman_flee: hangedman_flee(obj); break;
        case F::hangedman_wait_reappear: hangedman_wait_reappear(obj); break;
        case F::hangedman_dive_approach: hangedman_dive_approach(obj); break;
        case F::hangedman_dive_attack: hangedman_dive_attack(obj); break;
        case F::hangedman_death_fall: hangedman_death_fall(obj); break;
        case F::hangedman_rematch_intro: hangedman_rematch_intro(obj); break;
        default: call_routine(r, obj); break;
        }
    }
    for (uint32_t i = 0; i < 3; ++i) {
        const float v = of<float>(obj, 0x78 + 4 * i) + of<float>(obj, 0x84 + 4 * i);
        of<float>(obj, 0x78 + 4 * i) = v;
        of<float>(obj, 0x20 + 4 * i) = v + of<float>(obj, 0x20 + 4 * i);
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

// 0x4FC40: the part hits; with damage (flags bit 8): x0.8 with two players, off the hit points; unless +0x324 bit 4
// the hit reaction starts (flags bit 28, target freed); at 0 hit points state 13 (Chapter 4 on the ground: state 8,
// pattern 1); else the hurt cry
template <class P> void Game<P>::hangedman_hit(uint32_t obj)
{
    mem.at<uint8_t>(G::hit_class) = 0;
    mem.at<uint16_t>(G::hit_damage) = 0;
    mem.at<int32_t>(G::hit_type) = of<int16_t>(obj, 0xCC);
    enemy_part_hits(obj);
    if (((of<uint16_t>(obj, 0x58) >> 8) & 1) == 0 || mem.at<uint16_t>(G::hit_damage) == 0)
        return;
    of<int32_t>(obj, 0x334) = mem.at<int8_t>(G::hit_class);
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x100u;
    if (mem.at<uint16_t>(G::players_in_game) == 2)
        mem.at<int16_t>(G::hit_damage) = int16_t(int32_t(float(mem.at<int16_t>(G::hit_damage)) * 0.8f));
    of<uint16_t>(obj, 0x70) = uint16_t(of<uint16_t>(obj, 0x70) - mem.at<uint16_t>(G::hit_damage));
    if ((of<uint8_t>(obj, 0x324) & 0x10) == 0) {
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x10000000u;
        const uint8_t target = of<uint8_t>(obj, 0x2E8);
        if (target != 0xFF) {
            mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(int8_t(target)))) = 0;
            of<int8_t>(obj, 0x2E8) = -1;
        }
        of<int32_t>(obj, 0x338) = 0;
    }
    if (of<int16_t>(obj, 0x70) > 0 && return_false() == 0) {
        sound_cue(0xA01342);
        return;
    }
    if ((of<uint8_t>(obj, 0x324) & 0x10) != 0) {
        sound_cue(0xA01342);
        return;
    }
    if (mem.at<uint8_t>(G::chapter) == 3) {
        LocalScope scope(*this);
        const uint32_t p = local_alloc(12);
        for (uint32_t i = 0; i < 3; ++i)
            mem.set32(p + 4 * i, of<uint32_t>(obj, 0x20 + 4 * i));
        // PC 0x499C54 (pccap): no ground_height here, the constant -180.0 (0x5A32A4) against floor_y (0 in Chapter 4's
        // events on the PC), so the PC's Chapter 4 Hangedman falls dead (state 13); with ground_height = floor_y (PC
        // Mode) the arcade's test would always be true and it would never die (Chapter 4 event 1 waited forever)
        if ((pc_path() ? -180.0f : ground_height(p)) == mem.f32(G::floor_y)) {
            of<int32_t>(obj, 0x320) = 1;
            of<int16_t>(obj, 0x5C) = 8;
            of<int16_t>(obj, 0x60) = 0;
            of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 0x20000008u;
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xE7FFFFFFu;
            return;
        }
    }
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 13;
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xE7FFFFFEu;
}

// 0x4FDF0: the hit reaction by +0x338: 0 = frozen (flags bit 27, not hurt), by state: flapping below the block's
// share 6 calls the swarm off (flag 197, step 10); clawing / diving count hits (block bytes 9 / 0xA, +2 with two
// players; clawing also below share 7) and break off into the flinch (MO 35); 2 = the flinch ends (MO 22): from the
// claw back to state 8, else the pattern changes
template <class P> void Game<P>::hangedman_hit_reaction(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x35C);
    const int32_t w = of<int32_t>(obj, 0x338);
    auto notHurtLow = [&] {
        if (of<int16_t>(obj, 0x70) < hangedman_share(obj, 7) && mem.at<uint8_t>(G::event_index) == 15)
            of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 8;
    };
    auto count = [&](uint32_t off) {
        int32_t n = mem.at<int8_t>(blk + off);
        if (mem.at<uint16_t>(G::players_in_game) == 2)
            n += 2;
        const int32_t c = of<int32_t>(obj, 0x328) + 1;
        of<int32_t>(obj, 0x328) = c;
        return c >= n;
    };
    bool flinch = false;   // 0x4FFDC
    if (w == 1) {
        flinch = true;
    } else if (w == 0) {
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 8;
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x08000000u;
        const uint32_t idx = uint32_t(uint16_t(of<uint16_t>(obj, 0x5C) - 4));
        if (idx > 8 || idx == 1 || idx == 5 || idx == 6 || idx == 7) {
            hangedman_vuln_clear(obj);
            return;
        }
        if (idx == 0) {
            hangedman_vuln_clear(obj);
            if (of<int16_t>(obj, 0x70) < hangedman_share(obj, 6)) {
                of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 8;
                sound_cue(0xA0120D);
                mem.at<uint8_t>(G::event_flags + 197) = 1;
                of<int16_t>(obj, 0x5C) = 4;
                of<int16_t>(obj, 0x60) = 10;
            }
            return;
        }
        if (idx == 8) {
            if (of<int8_t>(obj, 0x2E5) <= 0) {
                hangedman_vuln_clear(obj);
                return;
            }
            of<int32_t>(obj, 0x2F4) = 15;
            sound_cue(0xA01212);
            if (!count(0xA)) {
                hangedman_vuln_clear(obj);
                return;
            }
        } else {
            if (idx == 3)
                of<int32_t>(obj, 0x2F4) = 15;
            bool broke = false;
            if (of<int16_t>(obj, 0x5C) == 7) {
                sound_cue(0xA01212);
                broke = count(9) || of<int16_t>(obj, 0x70) < hangedman_share(obj, 7);
            }
            if (!broke) {
                hangedman_vuln_clear(obj);
                notHurtLow();
                return;
            }
        }
        of<int32_t>(obj, 0x338) = of<int32_t>(obj, 0x338) + 1;
        flinch = true;
    } else if (w != 2) {
        return;
    }
    if (flinch) {
        of<int32_t>(obj, 0x2C) = 0;
        obj_stop(obj);
        set_motion_blend(obj, 35, 0, 4);
        sound_cue(0xA0120D);
        of<int32_t>(obj, 0x338) = of<int32_t>(obj, 0x338) + 1;
    }
    if ((of<uint8_t>(obj, 0x3C5) & 0x10) == 0)
        return;
    set_motion_blend(obj, 22, 0, 4);
    hangedman_vuln_clear(obj);
    if (of<int16_t>(obj, 0x5C) == 7) {
        notHurtLow();
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 8;
        return;
    }
    const int32_t pat = of<int32_t>(obj, 0x320);
    of<uint8_t>(obj, 0x2E5) = 0xFE;
    if (pat == 1) {
        of<int32_t>(obj, 0x320) = 0;
    } else if (pat == 0) {
        of<int32_t>(obj, 0x320) = 1;
    } else if (pat == 2 || pat == 3) {
        of<uint8_t>(obj, 0x2E5) = 0xFF;
        of<int32_t>(obj, 0x320) = 3;
    }
}

// 0x500E0: hurt again (+0x324 bit 3), flags bits 27 / 28 cleared
template <class P> void Game<P>::hangedman_vuln_clear(uint32_t obj)
{
    of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & ~8u;
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xE7FFFFFFu;
}

// 0x50110 (state 13): kill score 1500, uncounted; falls (MO 31, gravity, 0.5 up) facing the camera; on the ground the
// death effect, then blinks for 180 frames (hidden the last 90) and ends
template <class P> void Game<P>::hangedman_death_fall(uint32_t obj)
{
    const int16_t sub = of<int16_t>(obj, 0x60);
    auto ground = [&] {
        LocalScope scope(*this);
        const uint32_t p = local_alloc(12);
        for (uint32_t i = 0; i < 3; ++i)
            mem.set32(p + 4 * i, of<uint32_t>(obj, 0x20 + 4 * i));
        // PC 0x49A0E5 / 0x49A1B9 (pccap): Chapter 4's ground is the constant -180.0, else ground_height (floor_y)
        if (pc_path() && mem.at<uint8_t>(G::chapter) == 3)
            return -180.0f;
        return ground_height(p);
    };
    switch (sub) {
    case 0: {
        boss_event(kBossClockStop);   // PC 0x49A060
        if (boss_death_center) {   // (the cut-scene viewer only: he falls where the camera looks, on the grey road below him)
            const uint32_t rec = G::camera_matrices + 0x60 + mem.u32(G::camera_view) * 0x154;
            of<float>(obj, 0x20) = mem.f32(rec + 0x18) + 50.0f;
            of<float>(obj, 0x28) = mem.f32(rec + 0x20) + 50.0f;   // (the road's place, found by trial: 50 on and 50 down the courtyard)
        }
        enemy_kill_score(obj, 1500);
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        const uint8_t target = of<uint8_t>(obj, 0x2E8);
        if (target != 0xFF) {
            mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(int8_t(target)))) = 0;
            of<int8_t>(obj, 0x2E8) = -1;
        }
        of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
    }
        [[fallthrough]];
    case 1:
        obj_stop(obj);
        of<int32_t>(obj, 0x2C) = 0;
        of<uint32_t>(obj, 0x88) = 0xBCDF0123u;
        of<float>(obj, 0x7C) = 0.5f;
        of<float>(obj, 0x348) = ground();
        hangedman_face_camera(obj);
        set_motion_blend(obj, 31, 0, 4);
        if (mem.at<uint8_t>(G::chapter) == 1)
            sound_cue(0xA00002);
        sound_cue(0xA0131C);
        sound_cue(0xA01239);
        of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
        [[fallthrough]];
    case 2: {
        if ((of<uint8_t>(obj, 0x3C5) & 0x10) != 0)
            set_motion_blend(obj, 31, 100, 4);
        const float g = ground();
        if (of<float>(obj, 0x24) > of<float>(obj, 0x348)) {
            of<float>(obj, 0x348) = g;
            return;
        }
        obj_stop(obj);
        sound_cue(0xA0123A);
        hangedman_death_effect(obj);
        // PC 0x49A03C (hangedman_death_fall): 270 blink frames in Boss Mode (frontend_state 4), else 180
        of<int32_t>(obj, 0x2F8) = boss_on() ? 270 : 180;
        of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
    }
        [[fallthrough]];
    case 3: {
        const int32_t n = of<int32_t>(obj, 0x2F8) - 1;
        uint32_t f = of<uint32_t>(obj, 0x58);
        if (n % 2 == 0 && n > 0x59)
            f &= ~0x02000000u;
        else
            f |= 0x02000000u;
        of<uint32_t>(obj, 0x58) = f;
        of<int32_t>(obj, 0x2F8) = n;
        if (n < 1) {
            of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x02000000u;
            hangedman_slot_free(obj);
            if (mem.at<uint16_t>(G::enemy_count) == 1)
                mem.at<uint16_t>(G::enemy_count) = 0;
            body_free_parts(obj);
            boss_event(kBossWin);   // PC 0x49A2D1
            task_exit();
        }
        return;
    }
    default: return;
    }
}

// 0x503E0 (state 3, Chapter 2): voice messages 31 / 32 and the wing effects; after 365 frames the captives fall (flag
// 202, then 195: the camera held on them, uncounted for 135 frames, then flag 196); the title (sprites 0xDD / 0xE0)
// for 180 frames with the camera on the Hangedman; then the camera back, the life bar task, flag 192; state 4
template <class P> void Game<P>::hangedman_intro(uint32_t obj)
{
    of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 1;
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    const uint32_t cam = G::camera_records;
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        of<int32_t>(obj, 0x2F8) = 0;
        next();
        [[fallthrough]];
    case 1: {
        const int32_t n = of<int32_t>(obj, 0x2F8) + 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n == 60) {
            voice_message(31);
            hangedman_intro_effect_spawn(10);
            return;
        }
        if (n < 60) {
            if (n == 7) {
                sound_cue(0xA01004);
                sound_cue(0xA01263);
            }
            return;
        }
        if (n == 240) {
            voice_message(32);
            return;
        }
        if (n != 0x16D)
            return;
        for (uint32_t i = 0; i < 3; ++i)
            of<uint32_t>(obj, 0x3AC + 4 * i) = mem.u32(cam + 4 * i);
        of<int32_t>(obj, 0x2F8) = 30;
        mem.at<uint8_t>(G::event_flags + 202) = 1;
        next();
    }
        [[fallthrough]];
    case 2: {
        const int32_t n = of<int32_t>(obj, 0x2F8) - 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n > 0)
            return;
        mem.at<uint8_t>(G::event_flags + 195) = 1;
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        mem.set32(G::camera_hold_hostage, 1);
        of<int32_t>(obj, 0x2F8) = 0x87;
        next();
    }
        [[fallthrough]];
    case 3: {
        const int32_t n = of<int32_t>(obj, 0x2F8) - 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n > 0)
            return;
        mem.at<uint8_t>(G::event_flags + 196) = 1;
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
        next();
    }
        [[fallthrough]];
    case 4:
        boss_event(kBossEntrance);   // PC 0x49A471
        sprite_place(0x1004000, 1, 0, 27, 17, 0xDD);
        sprite_place(0x1004000, 1, 0, 7, 5, 0xE0);
        of<int32_t>(obj, 0x2F8) = 180;
        next();
        [[fallthrough]];
    case 5: {
        const float y = of<float>(obj, 0x24) + 14.0f;
        camera_set_direct(fbits(of<float>(obj, 0x3AC) + 60.0f), fbits(y), fbits(of<float>(obj, 0x3B4) - 17.0f),
                          of<uint32_t>(obj, 0x20), fbits(y), fbits(of<float>(obj, 0x28) - 8.0f));
        if (of<int32_t>(obj, 0x2F8) == 150)
            sound_cue(0xA01263);
        const int32_t n = of<int32_t>(obj, 0x2F8) - 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n > 0)
            return;
        for (uint32_t i = 0; i < 3; ++i)
            mem.set32(cam + 4 * i, of<uint32_t>(obj, 0x3AC + 4 * i));
        mem.set32(G::camera_hold_hostage, 0);
        sprite_draw(0x1004000, 27, 17, 0xDD);
        sprite_draw(0x1004000, 7, 5, 0xE0);
        boss_event(kBossClockStart);   // PC 0x49A598
        const uint32_t t = task_create(F::boss_life_bar_task, 0x3C8);
        mem.at<int16_t>(t + 0x60) = 0;
        mem.at<uint8_t>(G::event_flags + 192) = 1;
        of<int32_t>(obj, 0x2F8) = 0;
        next();
    }
        [[fallthrough]];
    case 6: {
        const int32_t n = of<int32_t>(obj, 0x2F8) - 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n > 0)
            return;
        of<int32_t>(obj, 0x320) = 0;
        boss_aggression_init(obj);
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 4;
        return;
    }
    default: return;
    }
}

// 0x50700 (state 4): waits for a target; with swarm bats left (+0x32C) beats its wings (MO 23, sound) and lets the
// bats dive (+0x2E5 = 1 while hangedman_bat_waves allows more), MO 25 then 22; with none left the swarm is called
// off (flag 197) and after 120 frames (step 10 / 11) state 5
template <class P> void Game<P>::hangedman_flap(uint32_t obj)
{
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    auto motionEnd = [&] { return (of<uint8_t>(obj, 0x3C5) & 0x10) != 0; };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        of<uint8_t>(obj, 0x2E5) = 0;
        of<int32_t>(obj, 0x2F8) = 0;
        next();
        [[fallthrough]];
    case 1: {
        pick_target_player(obj);
        if (of<uint8_t>(obj, 0x2E8) == 0xFF)
            return;
        if (of<int32_t>(obj, 0x32C) <= 0) {
            sound_cue(0xA0120D);
            mem.at<uint8_t>(G::event_flags + 197) = 1;
            of<int16_t>(obj, 0x60) = 10;
            return;
        }
        const int32_t n = of<int32_t>(obj, 0x2F8) - 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n > 0)
            return;
        set_motion_blend(obj, 23, 0, 4);
        of<int32_t>(obj, 0x2F8) = 60;
        of<int16_t>(obj, 0x60) = 5;
        return;
    }
    case 5: {
        of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 1;
        const int32_t n = of<int32_t>(obj, 0x2F8) - 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n == 20)
            sound_cue(0xA01109);
        if (of<int32_t>(obj, 0x2F8) > 0)
            return;
        mem.set32(G::bats_attacking, 0);
        of<uint8_t>(obj, 0x2E5) = 1;
        int32_t row = int32_t(mem.at<int16_t>(G::players_in_game)) - 1;
        if (row < 0)
            row = 0;
        of<int32_t>(obj, 0x330) = row;
        next();
    }
        [[fallthrough]];
    case 6: {
        const int32_t lim = mem.at<int32_t>(G::hangedman_bat_waves + uint32_t(of<int32_t>(obj, 0x330) * 20) +
                                            uint32_t(of<int32_t>(obj, 0x320) * 4));
        const int32_t ba = mem.at<int32_t>(G::bats_attacking);
        if (ba >= lim || of<int32_t>(obj, 0x32C) == ba)
            of<uint8_t>(obj, 0x2E5) = 0;
        if (!motionEnd())
            return;
        set_motion_blend(obj, 25, 0, 4);
        of<uint8_t>(obj, 0x2E5) = 0;
        next();
    }
        [[fallthrough]];
    case 7:
        if (!motionEnd())
            return;
        set_motion_blend(obj, 22, 0, 4);
        next();
        [[fallthrough]];
    case 8:
        if (mem.at<int32_t>(G::bats_attacking) > 0)
            return;
        of<int16_t>(obj, 0x60) = 0;
        return;
    case 10:
        set_motion_blend(obj, 22, 0, 4);
        of<int32_t>(obj, 0x2F8) = 120;
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 8;
        next();
        [[fallthrough]];
    case 11: {
        const int32_t n = of<int32_t>(obj, 0x2F8) - 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n > 0)
            return;
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 5;
        return;
    }
    default: return;
    }
}

// 0x50930 (state 5): one frame out of the count and its attack position (flag 193), then back with a new enemy
// slot; pattern -1, hurt again, state 7
template <class P> void Game<P>::hangedman_rejoin(uint32_t obj)
{
    const int16_t sub = of<int16_t>(obj, 0x60);
    if (sub == 0) {
        mem.at<uint8_t>(G::event_flags + 193) = 1;
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 0x800;
        hangedman_slot_free(obj);
        of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
        return;
    }
    if (sub != 1)
        return;
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
    of<int8_t>(obj, 0x2E7) = -1;
    of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & ~0x800u;
    enemy_slot_take(obj);
    of<uint8_t>(obj, 0x2E5) = 0;
    of<int32_t>(obj, 0x320) = -1;
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 7;
    of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & ~8u;
}

// 0x509F0 (state 6): hovers at hangedman_hover_point (camera path 66 at +0x30C, stepping by the block's +0x24) facing
// the camera for the pattern's time (table 0xC11A0), then attacks a player (state 7); in event 15 below the block's
// share 7: not hurt, and from the block's camera frame (+0x20) on flees (state 9)
template <class P> void Game<P>::hangedman_hover(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x35C);
    of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 2;
    const int16_t sub = of<int16_t>(obj, 0x60);
    if (sub == 0) {
        obj_stop(obj);
        of<float>(obj, 0x30C) = -1.0f;
        of<uint8_t>(obj, 0x3C6) = uint8_t(of<uint8_t>(obj, 0x3C6) | 1);
        of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) | 8);
        of<uint8_t>(obj, 0x2E5) = 0xFF;
        of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
        of<int32_t>(obj, 0x2F8) = mem.at<int32_t>(hangedman::hover_times + uint32_t(of<int32_t>(obj, 0x320) * 4));
    } else if (sub != 1) {
        return;
    }
    const int16_t m = of<int16_t>(obj, 0xD0);
    if ((m == 29 || m == 26) && (of<uint8_t>(obj, 0x3C5) & 0x10) != 0)
        set_motion_blend(obj, 22, 0, 4);
    float f = of<float>(obj, 0x30C) + mem.f32(blk + 0x24);
    if (!(f < 219.0f))
        f = 0.0f;
    of<float>(obj, 0x30C) = f;
    {
        LocalScope scope(*this);
        const uint32_t p = local_alloc(16);
        hangedman_hover_point(obj, 0x42, fbits(f), p);
        for (uint32_t i = 0; i < 3; ++i)
            of<uint32_t>(obj, 0x20 + 4 * i) = mem.u32(p + 4 * i);
    }
    hangedman_face_camera(obj);
    int16_t next;
    if (mem.at<uint8_t>(G::event_index) == 15 && of<int16_t>(obj, 0x70) < hangedman_share(obj, 7)) {
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 8;
        if (!(mem.f32(G::camera_frame) >= mem.f32(blk + 0x20)))
            return;
        next = 9;
    } else {
        const int32_t n = of<int32_t>(obj, 0x2F8) - 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n > 0)
            return;
        if (gBossHold && gBossHold(obj)) {   // (ours, totd8: the boss strike hold, game.h: hovers on, asked again)
            of<int32_t>(obj, 0x2F8) = 1;
            return;
        }
        pick_target_player(obj);
        if (of<uint8_t>(obj, 0x2E8) == 0xFF) {
            of<int32_t>(obj, 0x2F8) = mem.at<int32_t>(hangedman::hover_times + uint32_t(of<int32_t>(obj, 0x320) * 4));
            return;
        }
        next = 7;
    }
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = next;
}

// 0x50C10 (state 7): the next pattern (0-7; 1 after a reset), speed from the block (+0x2C / +0x28 by aggression);
// flies to a point in front of the camera (coprocessor 0x74 / 0x1A on the block's +0x48.. ), facing it; within 77 the
// claw (MO 29 from the block's start frame) and the camera's movement followed (patterns 1, 3, 4, 6); after frame 61
// the player is hit (unless not hurt), state 8
template <class P> void Game<P>::hangedman_claw_attack(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x35C);
    of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 2;
    const int16_t sub = of<int16_t>(obj, 0x60);
    if (sub != 2) {
        if (sub != 0 && sub != 1)
            return;
        if (sub == 0) {
            // (ours, totd8: The Typing of the Dead 0's boss strike hold, game.h: the claw run waits where he is)
            if (gBossHold && gBossHold(obj))
                return;
            int32_t n = of<int32_t>(obj, 0x320) + 1;
            if (n > 7)
                n = 0;
            if ((of<uint32_t>(obj, 0x324) & 0x20000000u) != 0)
                n = 1;
            of<int32_t>(obj, 0x320) = n;
            const int32_t lvl = of<int16_t>(obj, 0x66);
            of<int32_t>(obj, 0x328) = 0;
            of<uint8_t>(obj, 0x2E5) = 2;
            of<int32_t>(obj, 0x330) = lvl;
            of<uint32_t>(obj, 0x34C) = lvl == 0 ? mem.u32(blk + 0x2C) : mem.u32(blk + 0x28);
            of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
        }
        of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 2;
        const float f50 = mem.f32(blk + 0x50);
        Tgp::Out o;
        if (pm_boss()) {
            // PC 0x49ABC8: the screen point times 0.0020161290 (1 / 496 as a float, not a division), through stored
            // matrix 1 (the Hangedman file's transform 0x497070)
            const float a = mem.f32(blk + 0x48) * f50 * 0.002016129f;
            const float b = mem.f32(blk + 0x4C) * f50 * 0.002016129f;
            float t[3];
            pm_m1().transform(a, b, f50, t[0], t[1], t[2]);
            for (uint32_t i = 0; i < 3; ++i)
                of<float>(obj, 0x3A0 + 4 * i) = t[i];
        } else {
            const float a = mem.f32(blk + 0x48) * f50 / 496.0f;
            const float b = mem.f32(blk + 0x4C) * f50 / 496.0f;
            cam_copro(0x74, {1});
            o = cam_copro(0x1A, {fbits(a), fbits(b), mem.u32(blk + 0x50)});
            for (uint32_t i = 0; i < 3; ++i)
                of<uint32_t>(obj, 0x3A0 + 4 * i) = o.w[i];
        }
        const float dx = of<float>(obj, 0x3A0) - of<float>(obj, 0x20);
        const float dy = of<float>(obj, 0x3A4) - of<float>(obj, 0x24);
        const float dz = of<float>(obj, 0x3A8) - of<float>(obj, 0x28);
        if (!pm_boss())
            cam_copro(0x06, {});
        o = boss_unit(fbits(dx), fbits(dy), fbits(dz));
        for (uint32_t i = 0; i < 3; ++i)
            of<uint32_t>(obj, 0x78 + 4 * i) = o.w[i];
        of<uint32_t>(obj, 0x30) = yaw_toward_a(dx, dy, dz);
        const float s = of<float>(obj, 0x34C);
        for (uint32_t i = 0; i < 3; ++i) {
            of<float>(obj, 0x78 + 4 * i) = of<float>(obj, 0x78 + 4 * i) * s;
            of<uint32_t>(obj, 0x84 + 4 * i) = 0;
        }
        float dist;
        if (pm_boss()) {   // PC 0x49AD22: the table square root of (dy dy + dx dx) + dz dz
            const float ex = of<float>(obj, 0x3A0) - of<float>(obj, 0x20), ey = of<float>(obj, 0x3A4) - of<float>(obj, 0x24),
                        ez = of<float>(obj, 0x3A8) - of<float>(obj, 0x28);
            dist = pm::fast_sqrt((ey * ey + ex * ex) + ez * ez);
        } else {
            o = cam_copro(0x1F, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28),
                                 of<uint32_t>(obj, 0x3A0), of<uint32_t>(obj, 0x3A4), of<uint32_t>(obj, 0x3A8)});
            dist = ffrom(o.w[0]);
        }
        const uint32_t cp = G::camera_pos;
        if (of<int16_t>(obj, 0xD0) == 22 && dist < 77.0f) {
            const int32_t start = of<int32_t>(obj, 0x330) == 0 ? mem.at<int8_t>(blk + 0xC) : mem.at<int8_t>(blk + 0xB);
            set_motion_blend(obj, 29, start, 4);
            sound_cue(0xA01109);
            sound_cue(0xA0134F);
            for (uint32_t i = 0; i < 3; ++i)
                of<uint32_t>(obj, 0x3AC + 4 * i) = mem.u32(cp + 4 * i);
        }
        if (dist < 77.0f) {
            const uint32_t k = of<uint32_t>(obj, 0x320) & 7;
            // PC Mode: the PC's table 0x49AAEC follows the camera in the odd patterns (1, 3, 5, 7)
            const bool follow = pm_boss() ? (k & 1) != 0 : (k == 1 || k == 3 || k == 4 || k == 6);
            if (follow)
                for (uint32_t i = 0; i < 3; ++i)
                    of<float>(obj, 0x20 + 4 * i) = (mem.f32(cp + 4 * i) - of<float>(obj, 0x3AC + 4 * i)) +
                                                   of<float>(obj, 0x20 + 4 * i);
            for (uint32_t i = 0; i < 3; ++i)
                of<uint32_t>(obj, 0x3AC + 4 * i) = mem.u32(cp + 4 * i);
        }
        if (of<int16_t>(obj, 0xD0) != 29 || of<int16_t>(obj, 0xF4) <= 61)
            return;
        of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
    }
    if ((of<uint8_t>(obj, 0x324) & 8) == 0) {
        pick_target_player_any(obj);
        player_hit(of<int8_t>(obj, 0x2E8), 1, 9);
        of<int8_t>(obj, 0x2E8) = -1;
    }
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 8;
}

// 0x51020 (state 8): flies back to the hover point (frame 0) at the block's +0x30, facing the camera, off the attacker
// list while behind the camera; there MO 26, state 6 (a pattern reset makes it hurt again)
template <class P> void Game<P>::hangedman_return(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x35C);
    const int16_t sub = of<int16_t>(obj, 0x60);
    if (sub == 0) {
        obj_stop(obj);
        sound_cue(0xA01350);
        of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
    } else if (sub != 1) {
        return;
    }
    of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 2;
    if (of<int16_t>(obj, 0xD0) == 29 && (of<uint8_t>(obj, 0x3C5) & 0x10) != 0)
        set_motion_blend(obj, 22, 0, 4);
    const float vz = of<float>(obj, 0x40);
    if (vz < 0.0f && ((of<uint16_t>(obj, 0x324) >> 11) & 1) == 0)
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 0x800;
    if (vz > 0.0f && ((of<uint16_t>(obj, 0x324) >> 11) & 1) != 0)
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & ~0x800u;
    float d[3];
    {
        LocalScope scope(*this);
        const uint32_t p = local_alloc(16);
        hangedman_hover_point(obj, 0x42, 0, p);
        for (uint32_t i = 0; i < 3; ++i) {
            d[i] = mem.f32(p + 4 * i) - of<float>(obj, 0x20 + 4 * i);
            of<uint32_t>(obj, 0x3A0 + 4 * i) = mem.u32(p + 4 * i);
        }
    }
    Tgp::Out o = boss_unit(fbits(d[0]), fbits(d[1]), fbits(d[2]));
    for (uint32_t i = 0; i < 3; ++i)
        of<uint32_t>(obj, 0x78 + 4 * i) = o.w[i];
    hangedman_face_camera(obj);
    const float s = mem.f32(blk + 0x30);
    for (uint32_t i = 0; i < 3; ++i) {
        of<float>(obj, 0x78 + 4 * i) = of<float>(obj, 0x78 + 4 * i) * s;
        of<uint32_t>(obj, 0x84 + 4 * i) = 0;
    }
    for (uint32_t i = 0; i < 3; ++i)
        if (std::fabs(d[i]) > s)
            return;
    uint32_t f = of<uint32_t>(obj, 0x324);
    if ((f >> 29) & 1)
        f &= ~8u;
    of<uint32_t>(obj, 0x324) = f;
    set_motion_blend(obj, 26, 0, 4);
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 6;
}

// 0x51290 (state 9, Chapter 2): not hurt; MO 35 then 22, climbs to y 1000 then toward (1300, 1000, -1500) for 180
// frames; state 10
template <class P> void Game<P>::hangedman_flee(uint32_t obj)
{
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        obj_stop(obj);
        of<uint32_t>(obj, 0x3A0) = 0x44A28000u;   // 1300
        of<uint32_t>(obj, 0x3A4) = 0x447A0000u;   // 1000
        of<uint32_t>(obj, 0x3A8) = 0xC4BB8000u;   // -1500
        of<int32_t>(obj, 0x2F8) = 180;
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 8;
        sound_cue(0xA0120D);
        sound_cue(0xA0134F);
        set_motion_blend(obj, 35, 0, 4);
        next();
        [[fallthrough]];
    case 1:
        if ((of<uint8_t>(obj, 0x3C5) & 0x10) == 0)
            return;
        set_motion_blend(obj, 22, 0, 4);
        sound_cue(0xA0134F);
        next();
        [[fallthrough]];
    case 2: {
        const float dy = of<float>(obj, 0x3A4) - of<float>(obj, 0x24);
        of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 1;
        Tgp::Out o = boss_unit(0, fbits(dy), 0);
        for (uint32_t i = 0; i < 3; ++i)
            of<float>(obj, 0x84 + 4 * i) = ffrom(o.w[i]) * 0.1f;
        hangedman_face_camera(obj);
        if (of<float>(obj, 0x24) < of<float>(obj, 0x3A4))
            return;
        next();
    }
        [[fallthrough]];
    case 3: {
        float d[3];
        for (uint32_t i = 0; i < 3; ++i)
            d[i] = of<float>(obj, 0x3A0 + 4 * i) - of<float>(obj, 0x20 + 4 * i);
        of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 1;
        Tgp::Out o = boss_unit(fbits(d[0]), fbits(d[1]), fbits(d[2]));
        for (uint32_t i = 0; i < 3; ++i)
            of<float>(obj, 0x84 + 4 * i) = ffrom(o.w[i]) * 0.2f;
        of<uint32_t>(obj, 0x30) = yaw_toward_a(d[0], d[1], d[2]);
        const int32_t n = of<int32_t>(obj, 0x2F8) - 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n > 0)
            return;
    }
        [[fallthrough]];
    case 4:
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 10;
        return;
    default: return;
    }
}

// 0x515B0 (state 10): hidden, uncounted, off its attack position (flag 194) until the event's flag 201; then in at a
// fixed place gliding (accel 0.1, speed at most 2) toward (1376.18, 49, -913.5) with sounds when near the camera;
// there placed at (1420, -90, -599), counted again (flag 199); after flag 198 a voice by player, after flag 200 60
// frames; then pattern 0, state 11
template <class P> void Game<P>::hangedman_wait_reappear(uint32_t obj)
{
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    switch (of<uint16_t>(obj, 0x60)) {
    case 0:
        mem.at<uint8_t>(G::event_flags + 194) = 1;
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 0x800;
        hangedman_slot_free(obj);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) | 0x02000000u;
        next();
        return;
    case 1:
        if (mem.at<uint8_t>(G::event_flags + 201) != 1)
            return;
        of<int8_t>(obj, 0x2E7) = -1;
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & ~0x800u;
        enemy_slot_take(obj);
        of<uint32_t>(obj, 0x20) = 0x44AA4DB2u;
        of<uint32_t>(obj, 0x28) = 0xC4543B3Bu;
        of<uint32_t>(obj, 0x3A4) = 0x42440000u;
        of<uint32_t>(obj, 0x24) = 0x42E5A787u;
        of<uint32_t>(obj, 0x3A0) = 0x44AC05C3u;
        of<uint32_t>(obj, 0x3A8) = 0xC4646000u;
        of<int32_t>(obj, 0x330) = 0;
        obj_stop(obj);
        of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & ~0x02000000u;
        next();
        [[fallthrough]];
    case 2: {
        float d[3];
        for (uint32_t i = 0; i < 3; ++i)
            d[i] = of<float>(obj, 0x3A0 + 4 * i) - of<float>(obj, 0x20 + 4 * i);
        of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 2;
        Tgp::Out o = boss_unit(fbits(d[0]), fbits(d[1]), fbits(d[2]));
        for (uint32_t i = 0; i < 3; ++i)
            of<float>(obj, 0x84 + 4 * i) = ffrom(o.w[i]) * 0.1f;
        for (uint32_t i = 0; i < 3; ++i) {
            const uint32_t v = 0x78 + 4 * i;
            if (std::fabs(of<float>(obj, v)) > 2.0f) {
                if (of<float>(obj, v) < 0.0f)
                    of<float>(obj, v) = -2.0f;
                if (of<float>(obj, v) > 0.0f)
                    of<float>(obj, v) = 2.0f;
            }
        }
        hangedman_face_camera(obj);
        if (of<int32_t>(obj, 0x330) == 0 && of<float>(obj, 0x40) < 195.0f) {
            of<int32_t>(obj, 0x330) = 1;
            sound_cue(0xA01263);
            sound_cue(0xA0134F);
        }
        if (of<float>(obj, 0x28) > of<float>(obj, 0x3A8))
            return;
        of<int32_t>(obj, 0x330) = 0;
        obj_stop(obj);
        of<uint32_t>(obj, 0x20) = 0x44B18000u;   // 1420
        of<uint32_t>(obj, 0x24) = 0xC2B40000u;   // -90
        of<uint32_t>(obj, 0x28) = 0xC415C000u;   // -599
        hangedman_face_camera(obj);
        mem.at<uint8_t>(G::event_flags + 199) = 1;
        mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
        next();
    }
        [[fallthrough]];
    case 3: {
        if (mem.at<uint8_t>(G::event_flags + 198) != 1)
            return;
        sound_cue(0xA01226);
        bool b;
        if (mem.at<uint16_t>(G::players_in_game) == 2) {
            const int32_t r = int32_t(rand());
            b = r % 2 == 0;
        } else {
            b = mem.at<uint8_t>(G::solo_player_is_p2) != 0;
        }
        sound_cue(b ? 0xA0145A : 0xA01457);
        next();
    }
        [[fallthrough]];
    case 4:
        if (mem.at<uint8_t>(G::event_flags + 200) != 1)
            return;
        of<int32_t>(obj, 0x2F8) = 60;
        next();
        [[fallthrough]];
    case 5: {
        if (of<int32_t>(obj, 0x2F8) == 45)
            sound_cue(0xA01263);
        const int32_t n = of<int32_t>(obj, 0x2F8) - 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n > 0)
            return;
        of<float>(obj, 0x30C) = -1.0f;
        of<int32_t>(obj, 0x320) = 0;
        of<int32_t>(obj, 0x330) = 0;
        of<int32_t>(obj, 0xC0) = 0;
        of<int32_t>(obj, 0x32C) = 0;
        of<int16_t>(obj, 0x60) = 0;
        of<uint8_t>(obj, 0x2E5) = 0xFF;
        of<int16_t>(obj, 0x5C) = 11;
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & ~8u;
        return;
    }
    default: return;
    }
}

// 0x51B40 (state 11): flies at 3 to the dive's start: camera path 70 (patterns 0 / 1: in front of the camera) or 72
// (2 / 3: 400 beyond it) turned by +0xC0; there state 12 with the start kept (+0x394)
template <class P> void Game<P>::hangedman_dive_approach(uint32_t obj)
{
    const int16_t sub = of<int16_t>(obj, 0x60);
    if (sub == 0) {
        obj_stop(obj);
        sound_cue(0xA01350);
        of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
    } else if (sub != 1) {
        return;
    }
    of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 2;
    if (of<int16_t>(obj, 0xD0) == 29 && (of<uint8_t>(obj, 0x3C5) & 0x10) != 0)
        set_motion_blend(obj, 22, 0, 4);
    const float vz = of<float>(obj, 0x40);
    if (vz < 0.0f && ((of<uint16_t>(obj, 0x324) >> 11) & 1) == 0)
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) | 0x800;
    if (vz > 0.0f && ((of<uint16_t>(obj, 0x324) >> 11) & 1) != 0)
        of<uint32_t>(obj, 0x324) = of<uint32_t>(obj, 0x324) & ~0x800u;
    cam_copro(0x21, {});
    {
        LocalScope scope(*this);
        const uint32_t eye = local_alloc(16), look = local_alloc(16), roll = local_alloc(16);
        const uint32_t cp = G::camera_pos;
        Tgp::Out o;
        if (pm_boss()) {   // PC 0x49BF0D: the same placing on the Hangedman file's helpers
            if ((of<uint32_t>(obj, 0x320) & 3) < 2) {
                camera_path_eval(0x46, 0, eye, look, roll, roll + 4);
                const uint32_t t[3] = {mem.u32(cp), mem.u32(cp + 4), mem.u32(cp + 8)};
                o = boss_xform(t, false, 0, of<uint32_t>(obj, 0xC0), mem.f32(eye), mem.f32(eye + 4) * 30.0f + -70.0f,
                               mem.f32(eye + 8) * 20.0f + 400.0f);
            } else {
                camera_path_eval(0x48, 0, eye, look, roll, roll + 4);
                const uint32_t t[3] = {mem.u32(cp), mem.u32(cp + 4), fbits(mem.f32(cp + 8) + 400.0f)};
                o = boss_xform(t, false, 0, of<uint32_t>(obj, 0xC0), mem.f32(eye) * 25.0f, mem.f32(eye + 4) * 20.0f,
                               mem.f32(eye + 8) * 40.0f);
            }
        } else if ((of<uint32_t>(obj, 0x320) & 3) < 2) {
            cam_copro(0x88, {mem.u32(cp), mem.u32(cp + 4), mem.u32(cp + 8)});
            camera_path_eval(0x46, 0, eye, look, roll, roll + 4);
            cam_copro(0x15, {of<uint32_t>(obj, 0xC0)});
            o = cam_copro(0x1A, {mem.u32(eye), fbits(mem.f32(eye + 4) * 30.0f - 70.0f),
                                 fbits(mem.f32(eye + 8) * 20.0f + 400.0f)});
        } else {
            cam_copro(0x88, {mem.u32(cp), mem.u32(cp + 4), fbits(mem.f32(cp + 8) + 400.0f)});
            camera_path_eval(0x48, 0, eye, look, roll, roll + 4);
            cam_copro(0x15, {of<uint32_t>(obj, 0xC0)});
            o = cam_copro(0x1A, {fbits(mem.f32(eye) * 25.0f), fbits(mem.f32(eye + 4) * 20.0f),
                                 fbits(mem.f32(eye + 8) * 40.0f)});
        }
        for (uint32_t i = 0; i < 3; ++i)
            of<uint32_t>(obj, 0x3A0 + 4 * i) = o.w[i];
    }
    float d[3];
    for (uint32_t i = 0; i < 3; ++i)
        d[i] = of<float>(obj, 0x3A0 + 4 * i) - of<float>(obj, 0x20 + 4 * i);
    cam_copro(0x06, {});
    Tgp::Out o = boss_unit(fbits(d[0]), fbits(d[1]), fbits(d[2]));
    for (uint32_t i = 0; i < 3; ++i)
        of<uint32_t>(obj, 0x78 + 4 * i) = o.w[i];
    hangedman_face_camera(obj);
    for (uint32_t i = 0; i < 3; ++i) {
        of<float>(obj, 0x78 + 4 * i) = of<float>(obj, 0x78 + 4 * i) * 3.0f;
        of<uint32_t>(obj, 0x84 + 4 * i) = 0;
    }
    for (uint32_t i = 0; i < 3; ++i)
        if (std::fabs(d[i]) > 3.0f)
            return;
    obj_stop(obj);
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x5C) = 12;
    for (uint32_t i = 0; i < 3; ++i)
        of<uint32_t>(obj, 0x394 + 4 * i) = of<uint32_t>(obj, 0x20 + 4 * i);
}

// 0x51FA0 (state 12): the dive along a camera path (+0x30C the path frame): pattern 0 path 70 backwards (block +0x38 /
// +0x34 a frame), 1 path 70 forwards scaled / offset by the block (+0x40 / +0x3C), 2 / 3 path 72 from the start
// (block +0x44, rolling by +0xC4); at the path's end state 11 with the next pattern; faces the camera (turned by
// pitch), swoops (MO 33 then 27) inside the pattern's window and hits the player at its end (state stays, step 1)
template <class P> void Game<P>::hangedman_dive_attack(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x35C);
    of<int32_t>(obj, 0x50) = of<int32_t>(obj, 0x50) + 2;
    const int16_t sub = of<int16_t>(obj, 0x60);
    auto w30c = [&]() -> float& { return of<float>(obj, 0x30C); };
    auto ai = [&]() -> uint8_t& { return of<uint8_t>(obj, 0x2E5); };
    bool hit = false;
    if (sub == 2) {
        hit = true;
    } else if (sub == 0 || sub == 1) {
        if (sub == 0)
            of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
        cam_copro(0x21, {});
        const uint32_t k = of<uint32_t>(obj, 0x320) & 3;
        of<uint32_t>(obj, 0x320) = k;
        const uint32_t cp = G::camera_pos;
        auto toApproach = [&] {   // the dive ends early: pattern 2 next (the old one kept in +0x32C)
            const int32_t old = of<int32_t>(obj, 0x320);
            of<int32_t>(obj, 0x320) = 2;
            of<uint32_t>(obj, 0xC4) = 0x96C0;
            w30c() = 0.0f;
            of<int32_t>(obj, 0xC0) = 0;
            of<int16_t>(obj, 0x60) = 0;
            of<int16_t>(obj, 0x5C) = 11;
            of<int32_t>(obj, 0x32C) = old;
        };
        LocalScope scope(*this);
        const uint32_t eye = local_alloc(16), look = local_alloc(16), roll = local_alloc(16);
        auto setPos = [&](const Tgp::Out& o) {
            for (uint32_t i = 0; i < 3; ++i)
                of<uint32_t>(obj, 0x20 + 4 * i) = o.w[i];
        };
        if (k == 0) {
            cam_copro(0x88, {mem.u32(cp), mem.u32(cp + 4), mem.u32(cp + 8)});
            const float f = of<int16_t>(obj, 0x66) == 0 ? mem.f32(blk + 0x38) : mem.f32(blk + 0x34);
            const float w = w30c() - f;
            w30c() = w;
            if (!(w > 1.0f) && ai() == 0xFE) {
                toApproach();
            } else {
                if (w30c() < 0.0f)
                    w30c() = 399.0f;
                camera_path_eval(0x46, boss_path_frame(fbits(w30c())), eye, look, roll, roll + 4);
                const float ey = mem.f32(eye + 4) * 30.0f - 70.0f;
                const float ez = mem.f32(eye + 8) * 20.0f + 405.0f;
                if (pm_boss()) {   // PC 0x49C3E8
                    const uint32_t t[3] = {mem.u32(cp), mem.u32(cp + 4), mem.u32(cp + 8)};
                    setPos(boss_xform(t, false, 0, of<uint32_t>(obj, 0xC0), mem.f32(eye), ey, ez));
                } else {
                    cam_copro(0x15, {of<uint32_t>(obj, 0xC0)});
                    setPos(cam_copro(0x1A, {mem.u32(eye), fbits(ey), fbits(ez)}));
                }
            }
        } else if (k == 1) {
            cam_copro(0x88, {mem.u32(cp), mem.u32(cp + 4), mem.u32(cp + 8)});
            const float f = of<int16_t>(obj, 0x66) == 0 ? mem.f32(blk + 0x40) : mem.f32(blk + 0x3C);
            const float w = f + w30c();
            w30c() = w;
            if (!(w < 399.0f) && ai() == 0xFE) {
                toApproach();
            } else {
                if (!(w30c() < 399.0f))
                    w30c() = 0.0f;
                camera_path_eval(0x46, boss_path_frame(fbits(w30c())), eye, look, roll, roll + 4);
                const float x = mem.f32(blk + 0x54) * mem.f32(eye) + mem.f32(blk + 0x60);
                const float y = mem.f32(blk + 0x58) * mem.f32(eye + 4) + mem.f32(blk + 0x64);
                const float z = mem.f32(blk + 0x5C) * mem.f32(eye + 8) + mem.f32(blk + 0x68);
                if (pm_boss()) {   // PC 0x49C527
                    const uint32_t t[3] = {mem.u32(cp), mem.u32(cp + 4), mem.u32(cp + 8)};
                    setPos(boss_xform(t, false, 0, of<uint32_t>(obj, 0xC0), x, y, z));
                } else {
                    cam_copro(0x15, {of<uint32_t>(obj, 0xC0)});
                    setPos(cam_copro(0x1A, {fbits(x), fbits(y), fbits(z)}));
                }
            }
        } else {
            cam_copro(0x88, {of<uint32_t>(obj, 0x394), of<uint32_t>(obj, 0x398), of<uint32_t>(obj, 0x39C)});
            const float f = k == 2 ? mem.f32(blk + 0x44) : -mem.f32(blk + 0x44);
            const float w = f + w30c();
            w30c() = w;
            if (!(w < 399.0f) || (k == 3 && !(w > 1.0f))) {
                ai() = 0xFF;
                w30c() = 0.0f;
                of<int32_t>(obj, 0x320) = of<int32_t>(obj, 0x32C);
                const uint32_t r = rand();
                of<int16_t>(obj, 0x60) = 0;
                of<int16_t>(obj, 0x5C) = 11;
                of<int32_t>(obj, 0xC0) = int32_t(r & 0xFFF) - 0x7FF;
            } else {
                camera_path_eval(0x48, boss_path_frame(fbits(w30c())), eye, look, roll, roll + 4);
                of<uint32_t>(obj, 0xC4) = of<uint32_t>(obj, 0xC4) + 0xC0;
                if (pm_boss()) {   // PC 0x49C672
                    const uint32_t t[3] = {of<uint32_t>(obj, 0x394), of<uint32_t>(obj, 0x398), of<uint32_t>(obj, 0x39C)};
                    setPos(boss_xform(t, true, of<uint32_t>(obj, 0xC4), 0, mem.f32(eye) * 25.0f, mem.f32(eye + 4) * 20.0f,
                                      mem.f32(eye + 8) * 40.0f));
                } else {
                    cam_copro(0x16, {of<uint32_t>(obj, 0xC4)});
                    cam_copro(0x15, {0});
                    setPos(cam_copro(0x1A, {fbits(mem.f32(eye) * 25.0f), fbits(mem.f32(eye + 4) * 20.0f),
                                            fbits(mem.f32(eye + 8) * 40.0f)}));
                }
            }
        }
        cam_copro(0x06, {});
        uint32_t yaw, pitch;
        hangedman_angles(mem.f32(cp) - of<float>(obj, 0x20), mem.f32(cp + 4) - of<float>(obj, 0x24),
                         mem.f32(cp + 8) - of<float>(obj, 0x28), yaw, pitch);
        const int32_t pat = of<int32_t>(obj, 0x320);
        if (pat != 1 || w30c() <= 150.0f || !(w30c() < 210.0f)) {
            of<uint32_t>(obj, 0x30) = yaw;
            hangedman_turn_toward(obj, pitch, 0x100);
            of<uint32_t>(obj, 0x2C) = of<int8_t>(obj, 0x2E5) >= 0 ? pitch : 0;
        }
        bool swoop = false;
        uint8_t swoopAi = 0;
        if (pat == 1) {
            if (w30c() > 100.0f && w30c() < 200.0f && of<int8_t>(obj, 0x2E5) < 0)
                swoopAi = 4;
        } else if (pat == 0) {
            if (w30c() < 300.0f && w30c() > 200.0f && of<int8_t>(obj, 0x2E5) < 0)
                swoopAi = 3;
        } else if (pat == 2 || pat == 3) {
            if (w30c() > 100.0f && w30c() < 200.0f && ai() == 0xFE)
                swoopAi = 5;
        }
        // (ours, totd8: The Typing of the Dead 0's boss strike hold, game.h: no swoop, the dive flies on)
        if (swoopAi && !(gBossHold && gBossHold(obj))) {
            ai() = swoopAi;
            swoop = true;
        }
        if (swoop) {
            of<int32_t>(obj, 0x328) = 0;
            sound_cue(0xA01109);
            sound_cue(0xA0134F);
            set_motion_blend(obj, 33, 0, 4);
        }
        if ((of<uint8_t>(obj, 0x3C5) & 0x10) != 0 && of<int16_t>(obj, 0xD0) == 33)
            set_motion_blend(obj, 27, 0, 4);
        const int32_t p2 = of<int32_t>(obj, 0x320);
        if (p2 == 1) {
            if (w30c() < 200.0f || ai() != 4)
                return;
        } else if (p2 == 0) {
            if (w30c() > 200.0f || ai() != 3)
                return;
        } else if (p2 == 2 || p2 == 3) {
            if (w30c() < 200.0f || ai() != 5)
                return;
        }
        if (p2 >= 0 && p2 <= 3)
            of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
        hit = true;
    }
    if (!hit)
        return;
    pick_target_player(obj);
    const uint8_t t = of<uint8_t>(obj, 0x2E8);
    if ((t == 0 || t == 1) && (of<uint8_t>(obj, 0x324) & 8) == 0)
        player_hit(int8_t(t), 1, 4);
    sound_cue(0xA0134F);
    set_motion_blend(obj, 22, 0, 8);
    ai() = 0xFF;
    of<int16_t>(obj, 0x60) = 1;
}

// 0x527A0 (set-up 2, Chapter 4): as set-up 1 but state 14 and no captives
template <class P> void Game<P>::hangedman_rematch_init(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x35C);
    mem.set32(G::hangedman_obj, obj);
    const uint8_t type = mem.at<uint8_t>(blk + 4);
    of<uint16_t>(obj, 0xCC) = type;
    mem.at<int32_t>(G::hit_type) = type;
    body_init(obj, type, 22);
    creature_type_setup(obj);
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
    of<int8_t>(obj, 0x2E7) = -1;
    enemy_slot_take(obj);
    hangedman_face_camera(obj);
    of<uint16_t>(obj, 0x2E0) = 0xFF;
    of<uint16_t>(obj, 0x6C) = uint16_t(of<uint16_t>(obj, 0x6C) & ~2u);
    const uint16_t hp = mem.at<uint16_t>(blk);
    of<uint32_t>(obj, 0) = F::hangedman_fight_frame;
    of<int16_t>(obj, 0x5C) = 14;
    of<int32_t>(obj, 0x2F4) = 0;
    of<int32_t>(obj, 0x318) = 0;
    of<int16_t>(obj, 0x60) = 0;
    of<uint8_t>(obj, 0x3C5) = uint8_t(of<uint8_t>(obj, 0x3C5) | 4);
    of<uint16_t>(obj, 0x72) = hp;
    of<uint16_t>(obj, 0x70) = hp;
}

// 0x528D0 (state 14, Chapter 4): 180 frames, the life bar task, state 4
template <class P> void Game<P>::hangedman_rematch_intro(uint32_t obj)
{
    if (pc_path()) {
        // PC Mode / Boss Mode: THOTD.EXE 0x49CA74 has no wait: the life bar task, +0x320 0, the aggression and state
        // 4 at once (pmch34)
        const uint32_t t = task_create(F::boss_life_bar_task, 0x3C8);
        mem.at<int16_t>(t + 0x60) = 0;
        of<int32_t>(obj, 0x320) = 0;
        boss_aggression_init(obj);
        of<int16_t>(obj, 0x5C) = 4;
        of<int16_t>(obj, 0x60) = 0;
        return;
    }
    auto next = [&] { of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1); };
    switch (of<int16_t>(obj, 0x60)) {
    case 0:
        of<int32_t>(obj, 0x2F8) = 180;
        next();
        [[fallthrough]];
    case 1: {
        const int32_t n = of<int32_t>(obj, 0x2F8) - 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n > 0)
            return;
        next();
    }
        [[fallthrough]];
    case 3: {
        const uint32_t t = task_create(F::boss_life_bar_task, 0x3C8);
        mem.at<int16_t>(t + 0x60) = 0;
        of<int32_t>(obj, 0x2F8) = 0;
        next();
    }
        [[fallthrough]];
    case 6: {
        const int32_t n = of<int32_t>(obj, 0x2F8) - 1;
        of<int32_t>(obj, 0x2F8) = n;
        if (n > 0)
            return;
        of<int32_t>(obj, 0x320) = 0;
        boss_aggression_init(obj);
        of<int16_t>(obj, 0x60) = 0;
        of<int16_t>(obj, 0x5C) = 4;
        return;
    }
    default: return;
    }
}

// 0x52980: the hover point: camera path `path` at `frame`, then from the camera's position (pushed, 0x88) turned
// (0x15) and the path point scaled by 3 and offset by the pattern (+0x320 & 7) and event (15: the fight, else the
// rematch), into out[0..2]
template <class P> void Game<P>::hangedman_hover_point(uint32_t obj, uint32_t path, uint32_t frame, uint32_t out)
{
    LocalScope scope(*this);
    const uint32_t eye = local_alloc(16), look = local_alloc(16), roll = local_alloc(16);
    camera_path_eval(path, boss_path_frame(frame), eye, look, roll, roll + 4);
    const uint32_t cp = G::camera_pos;
    if (pm_boss()) {
        // PC 0x49CAD0: the camera's position as the translation, turned about y, the path point scaled by 2 (the
        // arcade: 3); the PC's table 0x49CAB0 takes the odd patterns as the first group (the arcade: 1, 3, 4, 6), and
        // its second group adds 30 to x (x87 order: (2 x + 40) + 30 in the rematch)
        pm::Matrix m;
        m.e[9] = mem.f32(cp);
        m.e[10] = mem.f32(cp + 4);
        m.e[11] = mem.f32(cp + 8);
        const float ex = mem.f32(eye), ey = mem.f32(eye + 4), ez = mem.f32(eye + 8);
        const bool ev15 = mem.at<uint8_t>(G::event_index) == 15;
        float x, y, z = ez * 2.0f + 100.0f;
        if ((of<uint32_t>(obj, 0x320) & 1) != 0) {
            m.rotate_y(ev15 ? 0xFFFFC000u : 0x4000u);
            x = ex * 2.0f;
            y = ey * 2.0f + (ev15 ? 30.0f : -5.0f);
        } else if (ev15) {
            m.rotate_y(0xFFFFF800u);
            y = ey * 2.0f + 20.0f;
            x = ex * 2.0f + 30.0f;
        } else {
            m.rotate_y(0x8000u);
            y = ey * 2.0f + 10.0f;
            x = (ex * 2.0f + 40.0f) + 30.0f;
        }
        float o[3];
        m.transform(x, y, z, o[0], o[1], o[2]);
        for (uint32_t i = 0; i < 3; ++i)
            mem.set32(out + 4 * i, fbits(o[i]));
        return;
    }
    cam_copro(0x21, {});
    cam_copro(0x88, {mem.u32(cp), mem.u32(cp + 4), mem.u32(cp + 8)});
    const float ex = mem.f32(eye), ey = mem.f32(eye + 4), ez = mem.f32(eye + 8);
    const bool ev15 = mem.at<uint8_t>(G::event_index) == 15;
    const uint32_t k = of<uint32_t>(obj, 0x320) & 7;
    Tgp::Out o;
    if (k == 1 || k == 3 || k == 4 || k == 6) {
        if (ev15) {
            cam_copro(0x15, {0xFFFFC000u});
            o = cam_copro(0x1A, {fbits(ex * 3.0f), fbits(ey * 3.0f + 30.0f), fbits(ez * 3.0f + 100.0f)});
        } else {
            cam_copro(0x15, {0x4000u});
            o = cam_copro(0x1A, {fbits(ex * 3.0f), fbits(ey * 3.0f - 5.0f), fbits(ez * 3.0f + 100.0f)});
        }
    } else {
        if (ev15) {
            cam_copro(0x15, {0xFFFFF800u});
            o = cam_copro(0x1A, {fbits(ex * 3.0f), fbits(ey * 3.0f + 20.0f), fbits(ez * 3.0f + 100.0f)});
        } else {
            cam_copro(0x15, {0x8000u});
            o = cam_copro(0x1A, {fbits(ex * 3.0f + 40.0f), fbits(ey * 3.0f + 10.0f), fbits(ez * 3.0f + 100.0f)});
        }
    }
    for (uint32_t i = 0; i < 3; ++i)
        mem.set32(out + 4 * i, o.w[i]);
    cam_copro(0x06, {});
}

// 0x52C30: the boss life bar: once the camera has stopped (camera_stopped 2), after 20 frames the bar (life_bar_init
// 11, maximum +0x72 of the creating object), updated with its hit points; at 0 emptied, then gone
template <class P> void Game<P>::boss_life_bar_task(uint32_t t)
{
    const uint32_t owner = mem.u32(t + 0x18);
    auto next = [&] { mem.at<uint16_t>(t + 0x60) = uint16_t(mem.at<uint16_t>(t + 0x60) + 1); };
    switch (mem.at<int16_t>(t + 0x60)) {
    case 0:
        if (mem.u32(G::camera_stopped) != 2)
            return;
        mem.set32(t + 0x50, 0);
        next();
        [[fallthrough]];
    case 1: {
        const int32_t n = mem.at<int32_t>(t + 0x50) + 1;
        mem.at<int32_t>(t + 0x50) = n;
        if (n != 20)
            return;
        coop4_boss_scale(owner + ob(0x70), owner + ob(0x72));   // 4P: the hit points for 3 / 4 players
        life_bar_init(11, 1, of<int16_t>(owner, 0x72));
        next();
    }
        [[fallthrough]];
    case 2: {
        const int32_t hp = of<int16_t>(owner, 0x70);
        if (hp > 0) {
            life_bar_update(hp, 0);
            return;
        }
        next();
    }
        [[fallthrough]];
    case 3:
        if (life_bar_update(0, 1) == -1)
            task_exit();
        return;
    default: return;
    }
}

// 0x52CE0: the death blast task (hangedman_explosion, model 0xD39) where he lands
template <class P> void Game<P>::hangedman_death_effect(uint32_t obj)
{
    const uint32_t t = task_create(F::hangedman_explosion, 0x58);
    mem.set32(t + 0x30, 0);
    mem.set32(t + 0x20, of<uint32_t>(obj, 0x20));
    mem.set32(t + 0x24, of<uint32_t>(obj, 0x348));
    mem.set32(t + 0x54, 0xD39);
    mem.set32(t + 0x28, of<uint32_t>(obj, 0x28));
}

// 0x52D20: +0x2C turned toward `angle` (16 bits, either way round) by at most `step`
template <class P> void Game<P>::hangedman_turn_toward(uint32_t obj, uint32_t angle, int32_t step)
{
    const int32_t cur = int32_t(of<uint16_t>(obj, 0x2C));
    const int32_t a = int32_t(angle & 0xFFFF);
    int32_t d0 = cur - a;
    if (d0 < 0)
        d0 = -d0;
    int32_t d1 = a - (cur - 0x10000);
    if (d1 < 0)
        d1 = -d1;
    int32_t d2 = cur - (a - 0x10000);
    if (d2 < 0)
        d2 = -d2;
    int32_t r;
    if (d0 <= step || d1 <= step || d2 <= step)
        r = a;
    else if (cur < a)
        r = d0 > d2 ? cur - step : cur + step;
    else
        r = d0 > d1 ? cur + step : cur - step;
    of<uint32_t>(obj, 0x2C) = uint32_t(r);
}

// 0x52DA0: the blast models 0xD39..0xD57 at its place (heading +0x30), one a frame, then gone
template <class P> void Game<P>::hangedman_explosion(uint32_t t)
{
    const int32_t m = mem.at<int32_t>(t + 0x54) + 1;
    mem.at<int32_t>(t + 0x54) = m;
    if (m > 0xD57) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {mem.u32(t + 0x20), mem.u32(t + 0x24), mem.u32(t + 0x28)});
    cam_copro(0x15, {mem.u32(t + 0x30)});
    cam_copro(0x13, {0x3F800000u, 0x3F800000u, 0x3F800000u});
    geo_draw_model(mem.u32(t + 0x54));
    cam_copro(0x06, {});
}

// 0x301E0: the main camera record's eye and look-at
template <class P> void Game<P>::camera_set_direct(uint32_t ex, uint32_t ey, uint32_t ez, uint32_t lx, uint32_t ly,
                                                   uint32_t lz)
{
    const uint32_t c = G::camera_records;
    mem.set32(c + 8, ez);
    mem.set32(c, ex);
    mem.set32(c + 4, ey);
    mem.set32(c + 0x18, lx);
    mem.set32(c + 0x1C, ly);
    mem.set32(c + 0x20, lz);
}

// 0x305A0: aggression level 1, its frame counter cleared, the players' lives total kept
template <class P> void Game<P>::boss_aggression_init(uint32_t obj)
{
    const int32_t lives = mem.at<int32_t>(G::player1_lives) + mem.at<int32_t>(G::player1_lives + 0xCC);
    of<int32_t>(obj, 0x300) = 0;
    of<int16_t>(obj, 0x66) = 1;
    of<int32_t>(obj, 0x2FC) = lives;
}

// 0x305D0: a life lost lowers the level (not below 0) and restarts the count; the count runs while players are in;
// past 1200 frames (900 for type 76) / players the level goes up (kept at 1 above 1)
template <class P> void Game<P>::boss_aggression_update(uint32_t obj)
{
    const int32_t lives = mem.at<int32_t>(G::player1_lives) + mem.at<int32_t>(G::player1_lives + 0xCC);
    int32_t kept = of<int32_t>(obj, 0x2FC);
    if (lives != kept) {
        if (lives < kept) {
            const uint16_t l = uint16_t(of<uint16_t>(obj, 0x66) - 1);
            of<uint16_t>(obj, 0x66) = l;
            if ((l >> 15) & 1)
                of<uint16_t>(obj, 0x66) = 0;
        }
        kept = lives;
        of<int32_t>(obj, 0x300) = 0;
    }
    of<int32_t>(obj, 0x2FC) = kept;
    const uint16_t pig = mem.at<uint16_t>(G::players_in_game);
    int32_t n = of<int32_t>(obj, 0x300);
    if (pig != 0)
        n += 1;
    int32_t players = int32_t(int16_t(pig));
    if (players <= 0)
        players = 1;
    const int32_t span = of<uint16_t>(obj, 0xCC) == 0x4C ? 900 : 1200;
    of<int32_t>(obj, 0x300) = n;
    if (n > span / players) {
        const uint16_t l = uint16_t(of<uint16_t>(obj, 0x66) + 1);
        of<uint16_t>(obj, 0x66) = l;
        if (int32_t(uint32_t(l) << 16) > 0x10000)
            of<int16_t>(obj, 0x66) = 1;
    }
}

// 0x30690 (kind 19): model 0x14AC (scale 5) facing the camera (heading and pitch) until flag 253
template <class P> void Game<P>::billboard_model(uint32_t obj)
{
    if (mem.at<uint8_t>(G::event_flags + 253) == 1) {
        task_exit();
        return;
    }
    int16_t s = of<int16_t>(obj, 0x60);
    if (s == 0) {
        s = 1;
        of<uint32_t>(obj, 0x54) = 0x14AC;
    }
    of<int16_t>(obj, 0x60) = s;
    uint32_t yaw, pitch;
    hangedman_angles(mem.f32(G::camera_pos) - of<float>(obj, 0x20), mem.f32(G::camera_pos + 4) - of<float>(obj, 0x24),
                     mem.f32(G::camera_pos + 8) - of<float>(obj, 0x28), yaw, pitch);
    of<uint32_t>(obj, 0x30) = yaw;
    of<uint32_t>(obj, 0x2C) = pitch;
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    cam_copro(0x14, {of<uint32_t>(obj, 0x2C) - 0x4000u});
    cam_copro(0x16, {of<uint32_t>(obj, 0x34)});
    cam_copro(0x13, {0x40A00000u, 0x40A00000u, 0x40A00000u});
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

// 0x31120: a hangedman_intro_effect task, delayed n x 61 frames
template <class P> void Game<P>::hangedman_intro_effect_spawn(int n)
{
    const uint32_t t = task_create(F::hangedman_intro_effect, 0x3C8);
    task_clear_work(t);
    mem.at<int16_t>(t + 0x60) = 0;
    mem.at<int32_t>(t + 0x50) = n * 61;
}

// 0x31160: counts down, the wing sound 0xA01352 every 60 frames, then gone
template <class P> void Game<P>::hangedman_intro_effect(uint32_t t)
{
    const int16_t s = mem.at<int16_t>(t + 0x60);
    if (s == 0)
        mem.at<uint16_t>(t + 0x60) = uint16_t(mem.at<uint16_t>(t + 0x60) + 1);
    else if (s != 1)
        return;
    const int32_t n = mem.at<int32_t>(t + 0x50) - 1;
    mem.at<int32_t>(t + 0x50) = n;
    if (n % 60 == 0)
        sound_cue(0xA01352);
    if (mem.at<int32_t>(t + 0x50) < 0)
        task_exit();
}

// 0x70190: a captive hangs at its offset from the Hangedman's +0x2D4 point until flag 202, then falls (gravity) with
// its cry at flag 195 (the woman: the camera follows her down until flag 196), moved once (50, -150); the man takes
// motion 0x185; gone below -600
template <class P> void Game<P>::hangedman_captive_frame(uint32_t obj)
{
    const uint32_t hm = mem.u32(G::hangedman_obj);
    const int16_t sub = of<int16_t>(obj, 0x60);
    bool fall = sub == 1;
    if (sub == 0) {
        for (uint32_t i = 0; i < 3; ++i)
            of<float>(obj, 0x20 + 4 * i) = mem.f32(hm + 0x2D4 + 4 * i) + of<float>(obj, 0x3AC + 4 * i);
        if (mem.at<uint8_t>(G::event_flags + 202) == 1) {
            of<int32_t>(obj, 0x334) = 0;
            of<uint32_t>(obj, 0x88) = 0xBCDF0123u;
            of<int16_t>(obj, 0x60) = int16_t(of<uint16_t>(obj, 0x60) + 1);
            fall = true;
        }
    }
    if (fall) {
        const bool f195 = mem.at<uint8_t>(G::event_flags + 195) == 1;
        if (f195 && of<int32_t>(obj, 0x334) == 0) {
            sound_cue(of<int32_t>(obj, 0x320) == 0 ? 0xA0133D : 0xA0132B);
            of<int32_t>(obj, 0x334) = 1;
        }
        if (of<int32_t>(obj, 0x320) == 0 && f195 && mem.at<uint8_t>(G::event_flags + 196) == 0)
            camera_set_direct(of<uint32_t>(obj, 0x20), fbits(mem.f32(hm + 0x24) - 10.0f), of<uint32_t>(obj, 0x28),
                              of<uint32_t>(obj, 0x20), 0xC4160000u, of<uint32_t>(obj, 0x28));
        const int32_t who = of<int32_t>(obj, 0x320);
        if (who == 0 && mem.at<uint8_t>(G::event_flags + 195) == 1 && of<int32_t>(obj, 0x31C) == 0) {
            of<int32_t>(obj, 0x31C) = 1;
            of<float>(obj, 0x20) = of<float>(obj, 0x20) + 50.0f;
            of<float>(obj, 0x28) = of<float>(obj, 0x28) + -150.0f;
        }
        if (who == 1 && mem.at<uint8_t>(G::event_flags + 195) == 1 && of<int32_t>(obj, 0x31C) == 0) {
            of<int32_t>(obj, 0x31C) = 1;
            of<float>(obj, 0x20) = of<float>(obj, 0x20) + 50.0f;
            of<float>(obj, 0x28) = of<float>(obj, 0x28) + -150.0f;
            set_motion(obj, 0x185);
        }
        const float vy = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
        const float y = of<float>(obj, 0x24) + vy;
        of<float>(obj, 0x7C) = vy;
        of<float>(obj, 0x24) = y;
        if (y + vy < -600.0f) {
            body_free_parts(obj);
            task_exit();
            return;
        }
    }
    object_draw(obj);
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
    of<int32_t>(obj, 0x10C) = of<int32_t>(obj, 0x10C) + 1;
}

// 0x703B0: the two captives (type 26 the woman at (-8, -24, 0), type 17 the researcher at (6, -24, 2) turned round),
// motion 0xDA, fade 0xE1
template <class P> void Game<P>::hangedman_captives()
{
    uint32_t t = task_create(F::hangedman_captive_frame, 0x3C8);
    task_clear_work(t);
    of<uint32_t>(t, 0x3AC) = 0xC1000000u;   // -8
    of<uint32_t>(t, 0x3B0) = 0xC1C00000u;   // -24
    of<uint16_t>(t, 0x2E0) = 0xE1;
    of<int32_t>(t, 0x320) = 1;
    of<uint32_t>(t, 0x3B4) = 0;
    of<uint16_t>(t, 0xCC) = 26;
    of<uint32_t>(t, 0x30) = 0;
    body_init(t, 26, 0xDA);
    of<uint16_t>(t, 0x6C) = uint16_t(of<uint16_t>(t, 0x6C) & ~2u);
    t = task_create(F::hangedman_captive_frame, 0x3C8);
    task_clear_work(t);
    of<uint32_t>(t, 0x3AC) = 0x40C00000u;   // 6
    of<uint32_t>(t, 0x3B0) = 0xC1C00000u;   // -24
    of<uint32_t>(t, 0x3B4) = 0x40000000u;   // 2
    of<uint32_t>(t, 0x30) = 0x8000;
    of<uint16_t>(t, 0x2E0) = 0xE1;
    of<uint16_t>(t, 0xCC) = 17;
    of<int32_t>(t, 0x320) = 0;
    body_init(t, 17, 0xDA);
    of<uint16_t>(t, 0x6C) = uint16_t(of<uint16_t>(t, 0x6C) & ~2u);
}

} // namespace hotd::port
