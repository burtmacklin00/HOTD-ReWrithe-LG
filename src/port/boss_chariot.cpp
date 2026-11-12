// Teaser L7: kind 37, the Chariot (Chapter 1's boss, event 19; creature type 35, then 36 once its armour is off), with
// its hit box task, life bar task, title card, effects (sparks, hit glow, landing dust, falling armour pieces) and the
// part draw hooks (the armour plates' state, the weapon hand's matrix for the hit box). Read from the arcade
// disassembly 0x97880..0x9AFB0 (docs/decompilation.md "The Chariot"); arcade forms only.
//
// The Chariot's block (+0x37C, mem_alloc(40)): +0 = 1 when the frame counter +0xF8 advances this frame (chariot_draw
// adds it), +4 bit 0 the hit box task lives / bit 1 the life bar task lives, +8 = 3 when it is dead (the hit box task
// ends), +0xC 1 = unarmoured phase, +0xD step, +0xE sequence of the action script, +0x10 / +0x11 the armour action's
// flags, +0x12 hit points at which the armour comes off, +0x14 hit points (record +2), +0x18 distance table, +0x1C the
// sequences (record +12), +0x20 point table, +0x24 the action held while combat is not active yet.
// Script steps (8 bytes): {action, a, b, c} as signed shorts -> +0x60, +0x31C, +0x320, +0x324; action -1 = back to
// step 1; 15 = to sequence (byte of b if hit points <= a, else of c); 16 = to sequence a.
// Part tables at 0x543A00 (17 counters: armour pieces left per group, 0x8000 = not set yet) and 0x543A50 (the part of
// each role, 18 roles); hit position of the last armour hit 0x5439F0, its effect class 0x5439E0.
#include "port/game.h"

namespace hotd::port {

namespace chariot {
constexpr uint32_t counters = 0x543A00;      // per armour group: plates left (int)
constexpr uint32_t parts = 0x543A50;         // the part of each role (18)
constexpr uint32_t part_matrices = 0x543650; // per role: the part's matrix (12 words, 48 bytes)
constexpr uint32_t hit_class = 0x5439E0;     // effect class of the last armour hit (0: none)
constexpr uint32_t hit_pos = 0x5439F0;       // its camera-space position
constexpr uint32_t walk = 0xCAD60;           // {motion, speed, footstep frames[4]} x 2 (12 bytes)
constexpr uint32_t retreat = 0xCAD78;        // {motion (short), footstep frames[4] (+2)} (8 bytes); the speed is
                                             // this same word read as a float (the original's: 2.2e-12)
constexpr uint32_t attacks = 0xCAD80;        // {motion, hit frame, side, footstep frames[4]} x 3 (12 bytes)
constexpr uint32_t hit_motions = 0xCADB0;    // hit reactions 19..26 -> motion
constexpr uint32_t land = 0xCAD40;           // landing: {gravity, start speed}
constexpr uint32_t push_limit = 0xCAD50;     // pushed back to: Chapter 1 / later {limit, ., limit2}
constexpr uint32_t part_class = 0x11A6CB;    // + 8 x role: the role's armour class (signed byte)
constexpr uint32_t class_effect = 0x1357F0;  // + class: effect class (signed byte)
constexpr uint32_t class_damage = 0xCAC40;   // + 16 x class: damage (short)
constexpr uint32_t group_role = 0xCAC20;     // + counter index: the role whose plates it counts (signed byte)
constexpr uint32_t plate_states = 0x97782;   // + 8 x role: the plates' starting state (byte)
constexpr uint32_t plate_models = 0xCA920;   // + 12 x role: models by plate state (shorts)
constexpr uint32_t piece_list = 0xCAA00;     // + 6 x role: extra pieces drawn on the part (-1 ends)
constexpr uint32_t pieces = 0xCAA70;         // + 24 x piece: {x, y, z, angles z, y, x}
constexpr uint32_t pair_group = 0xCAC00;     // + role: the part's counter (signed byte)
constexpr uint32_t pair_links = 0xCA8D0;     // + 4 x counter: two neighbouring counters (shorts, -1 none)
constexpr uint32_t part_draws = 0xCABB0;     // + 4 x role: the part's draw routine (after the armour is off)
constexpr uint32_t piece_fall = 0xCA7B0;     // + 16 x role: falling piece {model, keep}
constexpr uint32_t role_classes = 0x1184E0;  // + 248 x type + 8 x role + 3: the role's class (8 / 9: weapon hands)
constexpr uint32_t glow_rot = 0xAA690;       // 9 words: the effects' facing rotation
} // namespace chariot

// motion near its end: frame +0xF4 >= the motion's frame count - 2 (the original's test, signed)
template <class P> bool Game<P>::chariot_motion_end(uint32_t obj)
{
    const int32_t n = int32_t(mem.u32(G::motion_frame_counts + 4 * uint32_t(int32_t(of<int16_t>(obj, 0xD0)))));
    return of<int16_t>(obj, 0xF4) >= n - 2;
}

// footsteps: the motion frame (+0xF4) equal to one of the 4 frames (signed bytes, as 16 bits) -> the sound
template <class P> void Game<P>::chariot_footstep(uint32_t obj, uint32_t frames, uint32_t cue)
{
    const uint32_t f = of<uint16_t>(obj, 0xF4);
    for (uint32_t i = 0; i < 4; ++i)
        if ((uint32_t(int32_t(mem.at<int8_t>(frames + i))) & 0xFFFF) == f) {
            sound_cue(cue);
            return;
        }
}

// 0x97880 boss_chariot (kind 37): type 35, motion 5, the part index, the part draw hook 0x9A040; the block from the
// spawn record (+0x35C: armour threshold, hit points, distances, points, sequences); the hit box task (0x993A0) and
// the life bar task; counted as an enemy; the armour counters "not set" (0x8000); in Chapter 1 the title card task
// and the title sprites' bank 0x3F, then the entrance, else straight into the fight
template <class P> void Game<P>::boss_chariot(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    of<int16_t>(obj, 0xCC) = 35;
    mem.set32(G::hit_type, 35);
    body_init(obj, 35, 5);
    chariot_part_index_build(obj);
    of<uint32_t>(obj, 0x11C) = F::chariot_part_draw_hook;
    creature_type_setup(obj);
    const uint32_t blk = mem_alloc(40);
    of<uint32_t>(obj, 0x37C) = blk;
    mem.set32(blk + 4, 0);
    mem.set32(blk + 8, 0);
    const uint32_t hb = task_create(F::chariot_hitbox_task, 0x3C8);
    of<uint32_t>(obj, 0x360) = hb;
    mem.set32(blk + 4, mem.u32(blk + 4) | 1);
    mem.at<uint8_t>(blk + 0xC) = 0;
    mem.at<uint16_t>(blk + 0x12) = mem.at<uint16_t>(rec);
    mem.at<uint8_t>(blk + 0x10) = 0;
    mem.at<uint8_t>(blk + 0x11) = 0;
    mem.at<uint16_t>(blk + 0x14) = mem.at<uint16_t>(rec + 2);
    mem.set32(blk + 0x18, mem.u32(rec + 4));
    mem.set32(blk + 0x1C, rec + 12);
    mem.set32(blk + 0x20, mem.u32(rec + 8));
    pm_chariot_landed = mem.at<uint8_t>(G::chapter) == 0;   // PC 0x51558B (PC Mode's copy; unused in arcade games)
    of<int16_t>(obj, 0x6A) = 0;
    of<int16_t>(obj, 0x60) = 0;
    mem.set32(blk, 0);
    of<uint32_t>(obj, 0x58) &= 0xFFFFFEF8u;
    mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
    for (uint32_t i = 17; i-- > 0;)
        mem.set32(chariot::counters + 4 * i, 0x8000);
    mem.set32(hb + 0x58, 0);
    mem.set32(hb + 0xC8, 0);
    mem.set32(hb + 0x90, 0x3FA66666);   // 1.3
    const uint32_t lb = task_create(F::chariot_life_bar_task, 0x3C8);
    mem.set32(blk + 4, mem.u32(blk + 4) | 2);
    mem.at<int16_t>(lb + 0x66) = 0;
    if (mem.at<uint8_t>(G::chapter) == 0) {
        const uint32_t tc = task_create(F::chariot_title_card, 0x3C8);
        mem.at<int16_t>(tc + 0x66) = 0;
        sprite_bank_load(0x3F, 0xDC);
    }
    of<int16_t>(obj, 0x66) = 0;
    if (mem.at<uint8_t>(G::chapter) != 0) {
        of<uint32_t>(obj, 0) = F::chariot_start_direct;
        chariot_start_direct(obj);
    } else {
        of<uint32_t>(obj, 0) = F::chariot_entrance;
        chariot_entrance(obj);
    }
}

// 0x97A90 chariot_entrance (Chapter 1): falls from above (at camera frame 290 placed at 260, 542) to the floor, dust
// and the landing sounds; MO 4 (the roar: sound 0xA0125C), at its frame 38 the 0xA01344 cry and event flag 0x9C
// (Sophie is struck); MO 16 for 282 frames (sounds at 0x87 and 0xB5 left), then the fight
template <class P> void Game<P>::chariot_entrance(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    const uint32_t step = uint32_t(int32_t(of<int16_t>(obj, 0x66)));
    switch (step) {
    case 0:
        of<uint32_t>(obj, 0x7C) = 0;
        of<uint32_t>(obj, 0x88) = 0xBCDF0123;
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
        [[fallthrough]];
    case 1: {
        if (mem.f32(G::camera_frame) == 290.0f) {
            of<uint32_t>(obj, 0x20) = 0x43820000;   // 260
            of<uint32_t>(obj, 0x28) = 0x44078000;   // 542
        }
        const float y = of<float>(obj, 0x24);
        if (y > mem.f32(G::floor_y)) {
            const float v = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
            of<float>(obj, 0x7C) = v;
            of<float>(obj, 0x24) = y + v;
            break;
        }
        if (boss_next_on()) {   // PC 0x51564C: Boss Mode's undrawn walk-up (the camera may skip frame 290): placed here
            of<uint32_t>(obj, 0x20) = 0x43820000;
            of<uint32_t>(obj, 0x28) = 0x44078000;
        }
        of<uint32_t>(obj, 0x24) = mem.u32(G::floor_y);
        chariot_landing_dust(obj);
        sound_cue(0xA0125B);
        sound_cue(0xA00002);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
        [[fallthrough]];
    case 2:
        if (!((of<uint8_t>(obj, 0x3C5) >> 4) & 1)) {
            mem.set32(blk, 1);
            break;
        }
        set_motion_blend(obj, 4, 0, 8);
        sound_cue(0xA0125C);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
        [[fallthrough]];
    case 3:
        if (!((of<uint8_t>(obj, 0x3C5) >> 4) & 1)) {
            if (of<uint16_t>(obj, 0xF4) == 38) {
                sound_cue(0xA01344);
                mem.at<uint8_t>(G::event_flags + 0x9C) = 1;
            }
            mem.set32(blk, 1);
            break;
        }
        set_motion_blend(obj, 16, 0, 8);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
        of<int32_t>(obj, 0x324) = 0x11A;
        [[fallthrough]];
    case 4: {
        const int32_t t = of<int32_t>(obj, 0x324) - 1;
        of<int32_t>(obj, 0x324) = t;
        if (t == 0) {
            of<uint32_t>(obj, 0xC0) = 0;
            chariot_combat_start(obj);
            of<uint32_t>(obj, 0) = F::chariot_frame;
            chariot_frame(obj);
            return;
        }
        if (t == 0x87) {
            sound_cue(0xA0131F);
        } else if (t == 0xB5) {
            of<uint32_t>(obj, 0xF8) = 0;
            sound_cue(0xA0131F);
        }
        mem.set32(blk, 1);
        break;
    }
    default: break;
    }
    chariot_draw(obj);
}

// 0x97C80 chariot_start_direct (after Chapter 1): the fight at once
template <class P> void Game<P>::chariot_start_direct(uint32_t obj)
{
    chariot_combat_start(obj);
    of<uint32_t>(obj, 0) = F::chariot_frame;
    chariot_frame(obj);
}

// 0x97CA0 chariot_combat_start: attack slots, a target player, event flag 0x9F; it and its hit box shootable (bit 0),
// the hit bits cleared; the script from sequence 0 step 0
template <class P> void Game<P>::chariot_combat_start(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    const uint32_t hb = of<uint32_t>(obj, 0x360);
    attack_slot_take_wide(obj);
    attack_slot_front_take(obj);
    pick_target_player_rand(obj);
    mem.at<uint8_t>(G::event_flags + 0x9F) = 1;
    of<uint32_t>(obj, 0x58) |= 1;
    mem.set32(hb + 0x58, mem.u32(hb + 0x58) | 1);
    of<uint32_t>(obj, 0x58) &= 0xFFFFFEF9u;
    mem.set32(hb + 0x58, mem.u32(hb + 0x58) & 0xFFFFFEF9u);
    of<int16_t>(obj, 0x60) = 0;
    of<int16_t>(obj, 0x66) = 0;
    mem.at<uint8_t>(blk + 0xD) = 0;
    mem.at<uint8_t>(blk + 0xE) = 0;
}

// 0x97D20 chariot_frame: the next script step when the action is done (+0x60 = 0); attack actions 3 / 4 / 6 held as
// action 17 while combat is not active; the action; then the hits (armoured: which plate was hit, the hand hits;
// unarmoured: damage per part), drawn
template <class P> void Game<P>::chariot_frame(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    if (of<uint16_t>(obj, 0x60) == 0) {
        bool again;
        do {
            const uint32_t seqs = mem.u32(blk + 0x1C);
            const int32_t e = mem.at<int8_t>(blk + 0xE);
            const uint8_t d = uint8_t(mem.at<uint8_t>(blk + 0xD) + 1);
            mem.at<uint8_t>(blk + 0xD) = d;
            int32_t act = mem.at<int16_t>(mem.u32(seqs + 4 * uint32_t(e)) + 8 * uint32_t(int32_t(int8_t(d))));
            again = false;
            if (act == -1) {
                mem.at<uint8_t>(blk + 0xD) = 1;
                act = mem.at<int16_t>(mem.u32(seqs + 4 * uint32_t(e)) + 8);
            }
            of<int16_t>(obj, 0x60) = int16_t(act);
            const uint32_t st = mem.u32(seqs + 4 * uint32_t(int32_t(mem.at<int8_t>(blk + 0xE)))) +
                                8 * uint32_t(int32_t(mem.at<int8_t>(blk + 0xD)));
            of<int32_t>(obj, 0x31C) = mem.at<int16_t>(st + 2);
            of<int32_t>(obj, 0x320) = mem.at<int16_t>(st + 4);
            of<int16_t>(obj, 0x66) = 0;
            of<int32_t>(obj, 0x324) = mem.at<int16_t>(st + 6);
            const int32_t a = of<int16_t>(obj, 0x60);
            uint8_t seq = 0;
            if (a == 15) {
                seq = of<int16_t>(obj, 0x70) <= of<int32_t>(obj, 0x31C) ? of<uint8_t>(obj, 0x320) : of<uint8_t>(obj, 0x324);
                again = true;
            } else if (a == 16) {
                seq = of<uint8_t>(obj, 0x31C);
                again = true;
            }
            if (again) {
                mem.at<uint8_t>(blk + 0xD) = 0;
                mem.at<uint8_t>(blk + 0xE) = seq;
            }
        } while (again);
        if (combat_active() == 0) {
            const int32_t a = of<int16_t>(obj, 0x60);
            if (a >= 3 && (a <= 4 || a == 6)) {
                mem.at<uint16_t>(blk + 0x24) = of<uint16_t>(obj, 0x60);
                of<int16_t>(obj, 0x60) = 17;
            }
        }
        // (ours, totd8: The Typing of the Dead 0's boss strike hold, game.h: an attack action waits as the arcade's
        // own "combat not active" one does, in action 17's idle; null in the arcade)
        const auto attack = [](int32_t a) { return a == 8 || a == 9 || a == 11 || a == 18; };
        if (gBossHold && attack(of<int16_t>(obj, 0x60)) && gBossHold(obj)) {
            mem.at<uint16_t>(blk + 0x24) = of<uint16_t>(obj, 0x60);
            of<int16_t>(obj, 0x60) = 17;
        }
    }
    if (of<uint16_t>(obj, 0x60) == 17 && combat_active() != 0 &&
        !(gBossHold && (mem.at<int16_t>(blk + 0x24) == 8 || mem.at<int16_t>(blk + 0x24) == 9 ||
                        mem.at<int16_t>(blk + 0x24) == 11 || mem.at<int16_t>(blk + 0x24) == 18) && gBossHold(obj))) {
        of<int16_t>(obj, 0x66) = 0;
        of<uint16_t>(obj, 0x60) = mem.at<uint16_t>(blk + 0x24);
    }
    switch (of<uint16_t>(obj, 0x60)) {
    case 2: chariot_act2_land(obj); break;
    case 3: case 4: case 6: chariot_act_walk(obj); break;
    case 5: chariot_act5_armour(obj); break;
    case 7: chariot_act7_retreat(obj); break;
    case 8: case 9: case 11: case 18: chariot_act_attack(obj); break;
    case 12: chariot_act12_turn(obj); break;
    case 14: chariot_act14_face_players(obj); break;
    case 17: chariot_act17_wait_combat(obj); break;
    case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26: chariot_act_hit_reaction(obj); break;
    default: break;
    }
    mem.set32(G::hit_type, of<uint16_t>(obj, 0xCC));
    mem.set32(chariot::hit_class, 0);
    const uint32_t flags = of<uint32_t>(obj, 0x58);
    mem.at<int16_t>(G::hit_damage) = 0;
    if (mem.at<uint8_t>(blk + 0xC) == 0) {
        if (!((flags >> 28) & 1))
            chariot_armour_hit_test(obj);
        chariot_armoured_hits(obj);
    } else {
        // hordepub (ours, weapons): the explosions near it are shots at its nearest part with a plate left (its own
        // part test; the armoured phase's hand hit box is an item: weapon_blast_items marks it)
        if (weaponHooks && !((flags >> 28) & 1))
            weapon_blast_boss(obj, nullptr, 0);
        if (!((flags >> 28) & 1))
            chariot_part_damage(obj);
        chariot_unarmoured_hits(obj);
    }
    chariot_draw(obj);
}

// 0x97FE0 chariot_armour_break: MO 19 (sound 0xA0131A), MO 20, at its frame 3 the plates fall (pieces), the body
// becomes type 36 without armour (sound 0xA01230, unarmoured phase), MO 21 (0xA01231, 0xA0131F); at its end the
// script's sequence 7, hits taken again
template <class P> void Game<P>::chariot_armour_break(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    const uint32_t step = uint32_t(int32_t(of<int16_t>(obj, 0x66)));
    auto next = [&] { of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1); };
    auto busy = [&] { mem.set32(blk, 1); chariot_draw(obj); };
    switch (step) {
    case 0:
        set_motion_blend(obj, 19, 0, 7);
        sound_cue(0xA0131A);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 2);
        [[fallthrough]];
    case 2:
        if (!((of<uint8_t>(obj, 0x3C5) >> 4) & 1))
            return busy();
        set_motion(obj, 20);
        next();
        [[fallthrough]];
    case 3:
        if (of<int16_t>(obj, 0xF4) <= 2)
            return busy();
        chariot_armour_break_parts(obj);
        chariot_armour_off(obj, 20, 3);
        sound_cue(0xA01230);
        mem.at<uint8_t>(blk + 0xC) = 1;
        next();
        [[fallthrough]];
    case 4:
        if (!((of<uint8_t>(obj, 0x3C5) >> 4) & 1))
            return busy();
        set_motion(obj, 21);
        sound_cue(0xA01231);
        sound_cue(0xA0131F);
        next();
        [[fallthrough]];
    case 5:
        if (!((of<uint8_t>(obj, 0x3C5) >> 4) & 1))
            return busy();
        of<int16_t>(obj, 0x60) = 0;
        mem.at<uint8_t>(blk + 0xD) = 0;
        mem.at<uint8_t>(blk + 0xE) = 7;
        of<uint32_t>(obj, 0) = F::chariot_frame;
        of<uint32_t>(obj, 0x58) &= ~(1u << 28);
        chariot_frame(obj);
        return;
    default:
        chariot_draw(obj);
        return;
    }
}

// 0x98130 chariot_death: sounds (0xA00002 in Chapter 1, 0xA0131C; at MO frames 0xCE / 0xFA 0xA01223 / 0xA0123A);
// at the death motion's end the attack slot freed, uncounted, the block's dead mark (the hit box ends), the title
// sprites freed; then drawn until it leaves the view (coprocessor 0x7B, radius 20) and the life bar has ended
template <class P> void Game<P>::chariot_death(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    const int32_t step = of<int16_t>(obj, 0x66);
    if (step == 3) {   // Boss Mode (PC 0x515E60): 200 frames, then the win (0x412C08, each frame after); drawn
        const int32_t n = of<int32_t>(obj, 0x2EC);   // (PC +0x2F4: the PC's objects are 8 bytes longer in front)
        of<int32_t>(obj, 0x2EC) = n - 1;
        if (n <= 0)
            boss_event(kBossWin);
        chariot_death_draw(obj);
        return;
    }
    if (step == 0 || step == 1) {
        if (step == 0) {
            boss_event(kBossClockStop);   // PC 0x515D42
            if (mem.at<uint8_t>(G::chapter) == 0)
                sound_cue(0xA00002);
            sound_cue(0xA0131C);
            of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
        }
        if (of<uint16_t>(obj, 0xF4) == 0xCE)
            sound_cue(0xA01223);
        if (of<uint16_t>(obj, 0xF4) == 0xFA)
            sound_cue(0xA0123A);
        if (!chariot_motion_end(obj)) {
            mem.set32(blk, 1);
            chariot_death_draw(obj);
            return;
        }
        if (boss_on()) {   // PC 0x515DBD: Boss Mode: step 3, 200 frames to the win (no count / bank change)
            of<int16_t>(obj, 0x66) = 3;
            of<int32_t>(obj, 0x2EC) = 200;
            chariot_death_draw(obj);
            return;
        }
        if (of<uint8_t>(obj, 0x2E7) != 0xFF)
            enemy_attack_slot_free(obj);
        mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        mem.set32(blk + 8, 3);
        sprite_bank_free(0x3F);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    float vis;
    if (pm_boss()) {   // PC 0x515E35: the Chariot file's pc_view_test copy 0x514BA4
        vis = pm_view_test(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28), 20.0f);
    } else {
        Tgp::Out o = cam_copro(0x7B, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28), 0x41A00000});
        std::memcpy(&vis, &o.w[0], 4);
    }
    if (!(vis > 0.0f) && mem.u32(blk + 4) == 0) {
        body_free_parts(obj);
        enemy_free_extras(obj);
        task_exit();
        return;
    }
    chariot_death_draw(obj);
}

// 0x98298 chariot_death_draw (the tail of chariot_death, split off by Ghidra)
template <class P> void Game<P>::chariot_death_draw(uint32_t obj)
{
    chariot_draw(obj);
}

// 0x982B0 chariot_armoured_hits: a shot at the hit box (the weapon hand: player 1 / 2 bits) does 40, starts the hit
// glow and hit reaction 19 (an armour action in progress is marked done); other shots at an armour plate (class from
// chariot_armour_hit_test) spark (0xA0120A); the hit points down by the damage; at the step's threshold the armour
// comes off (chariot_armour_break), else 0xA01322 for a hit
template <class P> void Game<P>::chariot_armoured_hits(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    const uint32_t hb = of<uint32_t>(obj, 0x360);
    auto spark = [&] {
        if (mem.u32(chariot::hit_class) == 0)
            return;
        LocalScope scope(*this);
        const uint32_t p = local_alloc(12);
        for (uint32_t i = 0; i < 3; ++i)
            mem.set32(p + 4 * i, mem.u32(chariot::hit_pos + 4 * i));
        chariot_spark(p);
        sound_cue(0xA0120A);
    };
    auto hand_hit = [&] {
        mem.at<int16_t>(G::hit_damage) = 40;
        chariot_hit_glow_spawn(hb);
        of<int16_t>(obj, 0x66) = 0;
        of<int16_t>(obj, 0x60) = 19;
        of<uint32_t>(obj, 0x58) |= 1u << 28;
        if (mem.at<uint8_t>(blk + 0x10) != 0) {
            mem.at<uint8_t>(blk + 0x10) = 0;
            mem.at<uint8_t>(blk + 0x11) = 1;
        }
    };
    if ((of<uint32_t>(obj, 0x58) >> 28) & 1) {
        spark();
    } else if (mem.at<uint8_t>(hb + 0x58) & 6) {
        if (const uint32_t seats = coop4_on() ? coop4_hit_seats(hb) : 0; seats != 0) {
            for (int s = 0; s < 4; ++s)   // 4P: one hit per seat (players 1 and 3 in one frame: two)
                if ((seats >> s) & 1)
                    hand_hit();
        } else {
            if ((mem.u32(hb + 0x58) >> 1) & 1)
                hand_hit();
            if ((mem.u32(hb + 0x58) >> 2) & 1)
                hand_hit();
        }
    } else {
        spark();
    }
    const uint16_t hp = uint16_t(of<uint16_t>(obj, 0x70) - mem.at<uint16_t>(G::hit_damage));
    of<uint16_t>(obj, 0x70) = hp;
    if (!(int32_t(hp) > int32_t(mem.at<int16_t>(blk + 0x12)))) {
        of<int16_t>(obj, 0x66) = 0;
        of<uint32_t>(obj, 0) = F::chariot_armour_break;
        return;
    }
    if (mem.at<uint16_t>(G::hit_damage) != 0)
        sound_cue(0xA01322);
}

// 0x98450 chariot_unarmoured_hits: damage (from chariot_part_damage) once per hit reaction (flags bit 28); at 0 hit
// points 1500 points to the shooter, MO 6, chariot_death; else a hit reaction 20-26 by the hit's effect class, sounds
// 0xA01345 / 0xA01322
template <class P> void Game<P>::chariot_unarmoured_hits(uint32_t obj)
{
    if (mem.at<uint16_t>(G::hit_damage) == 0)
        return;
    uint32_t f = of<uint32_t>(obj, 0x58);
    if ((f >> 28) & 1)
        return;
    of<uint32_t>(obj, 0x58) = f | (1u << 28);
    of<int16_t>(obj, 0x66) = 0;
    const uint16_t hp = uint16_t(of<uint16_t>(obj, 0x70) - mem.at<uint16_t>(G::hit_damage));
    of<uint16_t>(obj, 0x70) = hp;
    if (!(int16_t(hp) > 0)) {
        kill_score_shooter(obj, 1500);
        of<int16_t>(obj, 0x70) = 0;
        set_motion_blend(obj, 6, 0, 7);
        of<uint32_t>(obj, 0) = F::chariot_death;
        return;
    }
    static constexpr int16_t reactions[7] = {20, 26, 21, 22, 23, 24, 25};
    const uint32_t k = mem.u32(chariot::hit_class) - 1;
    of<int16_t>(obj, 0x60) = k > 6 ? 26 : reactions[k];
    sound_cue(0xA01345);
    sound_cue(0xA01322);
}

// 0x98560 chariot_act_walk (actions 3 / 4 / 6): forward (walk_a, 0.1), back (walk_b, -0.2) or either at random; moves
// along its heading until the distance +0xA0 reaches the step's distance (table +0x18 [a]); footsteps 0xA0125B
template <class P> void Game<P>::chariot_act_walk(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    if (of<int16_t>(obj, 0x66) == 0) {
        const int32_t a = of<int16_t>(obj, 0x60);
        if (a == 3)
            of<int32_t>(obj, 0x328) = 0;
        else if (a == 4)
            of<int32_t>(obj, 0x328) = 1;
        else
            of<int32_t>(obj, 0x328) = int32_t(rand()) % 2;
        set_motion_blend(obj, uint32_t(int32_t(mem.at<int16_t>(chariot::walk + 12 * uint32_t(of<int32_t>(obj, 0x328))))), 8, 7);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    const uint32_t k = uint32_t(of<int32_t>(obj, 0x328));
    float v[3];
    chariot_heading_step(obj, mem.u32(chariot::walk + 4 + 12 * k), v);
    const float target = mem.f32(mem.u32(blk + 0x18) + 4 * uint32_t(of<int32_t>(obj, 0x31C)));
    if (!(of<float>(obj, 0xA0) <= target)) {
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + v[0];
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + v[1];
        of<float>(obj, 0x28) = of<float>(obj, 0x28) + v[2];
        mem.set32(blk, 1);
    } else {
        of<int16_t>(obj, 0x60) = 0;
    }
    chariot_footstep(obj, chariot::walk + 8 + 12 * uint32_t(of<int32_t>(obj, 0x328)), 0xA0125B);
}

// 0x98720 chariot_act5_armour: once (block +0x11 set: walk back, action 4) MO 19 (the armour motion), hits taken
// again at its end, then action 4
template <class P> void Game<P>::chariot_act5_armour(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    if (mem.at<uint8_t>(blk + 0x11) != 0) {
        of<int16_t>(obj, 0x60) = 4;
        chariot_act_walk(obj);
        return;
    }
    if (of<int16_t>(obj, 0x66) == 0) {
        mem.at<uint8_t>(blk + 0x10) = 1;
        set_motion_blend(obj, 19, 0, 7);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    if (!chariot_motion_end(obj)) {
        mem.set32(blk, 1);
        return;
    }
    of<uint32_t>(obj, 0x58) &= ~(1u << 28);
    mem.at<uint8_t>(blk + 0x10) = 0;
    mem.at<uint8_t>(blk + 0x11) = 1;
    of<int16_t>(obj, 0x66) = 0;
    of<int16_t>(obj, 0x60) = 4;
}

// 0x987C0 chariot_act7_retreat: MO 14 (koutai) restarted at its end, backing off until the distance reaches the
// step's distance; the step is the table's first word read as a float (2.2e-12: it does not really move)
template <class P> void Game<P>::chariot_act7_retreat(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    // PC Mode: the PC's step word (0x5F34AC, just before its table) is 0.0
    float v[3];
    chariot_heading_step(obj, pm_boss() ? 0u : mem.u32(chariot::retreat), v);
    if (of<int16_t>(obj, 0x66) == 0) {
        of<int32_t>(obj, 0x328) = 0;
        set_motion_blend(obj, uint32_t(int32_t(mem.at<int16_t>(chariot::retreat))), 8, 7);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    const float target = mem.f32(mem.u32(blk + 0x18) + 4 * uint32_t(of<int32_t>(obj, 0x31C)));
    if (!(of<float>(obj, 0xA0) >= target)) {
        if (chariot_motion_end(obj))
            set_motion_blend(obj, uint32_t(int32_t(mem.at<int16_t>(chariot::retreat + 8 * uint32_t(of<int32_t>(obj, 0x328))))), 8, 7);
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + v[0];
        of<float>(obj, 0x24) = of<float>(obj, 0x24) + v[1];
        of<float>(obj, 0x28) = of<float>(obj, 0x28) + v[2];
        mem.set32(blk, 1);
    } else {
        of<int16_t>(obj, 0x60) = 0;
    }
    chariot_footstep(obj, chariot::retreat + 2 + 8 * uint32_t(of<int32_t>(obj, 0x328)), 0xA0125B);
}

// 0x98950 chariot_act_attack (actions 8 / 9 / 18 / 11): attack a / b / r / a or b at random
template <class P> void Game<P>::chariot_act_attack(uint32_t obj)
{
    if (of<int16_t>(obj, 0x66) == 0) {
        const int32_t a = of<int16_t>(obj, 0x60);
        if (a == 8)
            of<int32_t>(obj, 0x328) = 0;
        else if (a == 9)
            of<int32_t>(obj, 0x328) = 1;
        else if (a == 18)
            of<int32_t>(obj, 0x328) = 2;
        else
            of<int32_t>(obj, 0x328) = int32_t(rand()) % 2;
        set_motion_blend(obj, uint32_t(int32_t(mem.at<int16_t>(chariot::attacks + 12 * uint32_t(of<int32_t>(obj, 0x328))))), 8, 7);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    const int32_t k = of<int32_t>(obj, 0x328);
    if (k == 0)
        chariot_attack_a(obj);
    else if (k == 1)
        chariot_attack_b(obj);
    else
        chariot_attack_r(obj);
}

// the attacks' hit: at the table's hit frame, one player (with two: at random) is hit on the table's side
template <class P> void Game<P>::chariot_attack_hit(uint32_t obj)
{
    const uint32_t e = chariot::attacks + 12 * uint32_t(of<int32_t>(obj, 0x328));
    if (of<uint16_t>(obj, 0xF4) != mem.at<uint16_t>(e + 2))
        return;
    const int player = mem.at<uint16_t>(G::players_in_game) == 2 ? int(rand() & 1) : int(mem.at<int8_t>(G::solo_player_is_p2));
    player_hit(player, 1, mem.at<int16_t>(e + 4));
}

// 0x98A20 chariot_attack_a: the swing hits at its frame (up to frame 128), then 30 frames' pause, then the motion's
// end; footsteps 0xA0125C
template <class P> void Game<P>::chariot_attack_a(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    switch (of<int16_t>(obj, 0x66)) {
    case 1:
        if (!(of<int16_t>(obj, 0xF4) > 128)) {
            chariot_attack_hit(obj);
            mem.set32(blk, 1);
            break;
        }
        of<int32_t>(obj, 0x31C) = 30;
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
        [[fallthrough]];
    case 2: {
        const int32_t t = of<int32_t>(obj, 0x31C) - 1;
        of<int32_t>(obj, 0x31C) = t;
        if (t != 0)
            break;
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
        [[fallthrough]];
    default:
        if (!chariot_motion_end(obj))
            mem.set32(blk, 1);
        else
            of<int16_t>(obj, 0x60) = 0;
        break;
    }
    chariot_footstep(obj, chariot::attacks + 6 + 12 * uint32_t(of<int32_t>(obj, 0x328)), 0xA0125C);
}

// 0x98B60 chariot_attack_b: the hit at its frame until the motion's end, then 30 frames; footsteps 0xA0125C
template <class P> void Game<P>::chariot_attack_b(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    bool wait = of<int16_t>(obj, 0x66) != 1;
    if (!wait) {
        if (!chariot_motion_end(obj)) {
            chariot_attack_hit(obj);
            mem.set32(blk, 1);
        } else {
            of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
            of<int32_t>(obj, 0x31C) = 30;
            wait = true;
        }
    }
    if (wait) {
        const int32_t t = of<int32_t>(obj, 0x31C) - 1;
        of<int32_t>(obj, 0x31C) = t;
        if (t == 0)
            of<int16_t>(obj, 0x60) = 0;
    }
    chariot_footstep(obj, chariot::attacks + 6 + 12 * uint32_t(of<int32_t>(obj, 0x328)), 0xA0125C);
}

// 0x98C80 chariot_attack_r: the hit at its frame until the motion's end
template <class P> void Game<P>::chariot_attack_r(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    if (chariot_motion_end(obj)) {
        of<int16_t>(obj, 0x60) = 0;
        return;
    }
    chariot_attack_hit(obj);
    mem.set32(blk, 1);
}

// the step (0, 0, step) along the heading +0x30 (coprocessor 0x21 / 0x15 / 0x1A; PC Mode: PC 0x51608c / 0x516288's
// push, the Chariot file's identity 0x513F40, rotate_y 0x5138F0 and transform 0x5134EC)
template <class P> void Game<P>::chariot_heading_step(uint32_t obj, uint32_t step, float v[3])
{
    if (pm_boss()) {
        pm::Matrix m;
        m.rotate_y(of<uint32_t>(obj, 0x30));
        m.transform(0.0f, 0.0f, ffrom(step), v[0], v[1], v[2]);
        return;
    }
    cam_copro(0x21, {});
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    Tgp::Out d = cam_copro(0x1A, {0, 0, step});
    cam_copro(0x06, {});
    std::memcpy(v, d.w, 12);
}

// the heading to a point (coprocessor 0x0F), half a turn more when the pitch is past 90 degrees (bits 15 / 14 differ).
// PC Mode: the Chariot file's pc_vec_angles copy 0x5135D4 (PC 0x516780 / 0x5167CC), which folds the pitch itself
template <class P> uint32_t Game<P>::chariot_heading(uint32_t obj, uint32_t x, uint32_t y, uint32_t z)
{
    auto sub = [](uint32_t a, uint32_t b) { float fa, fb; std::memcpy(&fa, &a, 4); std::memcpy(&fb, &b, 4); return fbits(fa - fb); };
    if (pm_boss()) {
        uint32_t yaw = 0, pitch = 0;
        pm::vec_angles(ffrom(sub(x, of<uint32_t>(obj, 0x20))), ffrom(sub(y, of<uint32_t>(obj, 0x24))),
                       ffrom(sub(z, of<uint32_t>(obj, 0x28))), yaw, pitch);
        of<uint32_t>(obj, 0x30) = yaw;
        return 0;
    }
    Tgp::Out o = cam_copro(0x0F, {sub(x, of<uint32_t>(obj, 0x20)), sub(y, of<uint32_t>(obj, 0x24)), sub(z, of<uint32_t>(obj, 0x28))});
    of<uint32_t>(obj, 0x30) = o.w[0];
    const uint32_t t = o.w[1] & 0xFFFF;
    return ((t << 1) ^ t) >> 15 & 1;
}

// 0x98D10 chariot_act14_face_players: turns to the camera (the players: camera record 0's eye)
template <class P> void Game<P>::chariot_act14_face_players(uint32_t obj)
{
    const uint32_t c = G::camera_records;
    if (chariot_heading(obj, mem.u32(c), mem.u32(c + 4), mem.u32(c + 8)))
        of<uint32_t>(obj, 0x30) += 0x8000;
    of<int16_t>(obj, 0x60) = 0;
}

// 0x98DB0 chariot_act12_turn: turns its back to point a of the script's point table (the heading + half a turn)
template <class P> void Game<P>::chariot_act12_turn(uint32_t obj)
{
    const uint32_t pt = mem.u32(of<uint32_t>(obj, 0x37C) + 0x20) + 12 * uint32_t(of<int32_t>(obj, 0x31C));
    uint32_t a = chariot_heading(obj, mem.u32(pt), mem.u32(pt + 4), mem.u32(pt + 8)) ? of<uint32_t>(obj, 0x30) + 0x8000
                                                                                    : of<uint32_t>(obj, 0x30);
    of<uint32_t>(obj, 0x30) = a + 0x8000;
    of<int16_t>(obj, 0x60) = 0;
}

// 0x98E70 chariot_act2_land: drops (gravity 0xCAD40) to the floor, dust and 0xA0125B; at the motion's end MO 17
// (the pose; 0xA0131F at its frame 6); +0xC0 = 0xC000 at the end
template <class P> void Game<P>::chariot_act2_land(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    switch (of<int16_t>(obj, 0x66)) {
    case 0:
        of<uint32_t>(obj, 0x88) = mem.u32(chariot::land);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
        of<uint32_t>(obj, 0x7C) = mem.u32(chariot::land + 4);
        [[fallthrough]];
    case 1: {
        const float y = of<float>(obj, 0x24);
        if (y > mem.f32(G::floor_y)) {
            const float v = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
            of<float>(obj, 0x7C) = v;
            of<float>(obj, 0x24) = y + v;
            return;
        }
        of<uint32_t>(obj, 0x24) = mem.u32(G::floor_y);
        chariot_landing_dust(obj);
        sound_cue(0xA0125B);
        pm_chariot_landed = true;   // PC 0x51690C
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
        [[fallthrough]];
    case 2:
        if (!chariot_motion_end(obj)) {
            mem.set32(blk, 1);
            return;
        }
        set_motion_blend(obj, 17, 0, 8);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
        [[fallthrough]];
    case 3:
        if (chariot_motion_end(obj)) {
            of<int16_t>(obj, 0x60) = 0;
            of<uint32_t>(obj, 0xC0) = 0xC000;
            return;
        }
        if (of<uint16_t>(obj, 0xF4) == 6)
            sound_cue(0xA0131F);
        mem.set32(blk, 1);
        return;
    default: return;
    }
}

// 0x98FA0 chariot_act_hit_reaction (actions 19-26): the reaction's motion (table 0xCADB0; 0xA0122A while armoured);
// pushed back from the camera to the arena's edge (80 units; 60 after Chapter 1) in the camera's heading frame
template <class P> void Game<P>::chariot_act_hit_reaction(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    if (of<int16_t>(obj, 0x66) == 0) {
        const uint32_t k = uint32_t(uint16_t(of<uint16_t>(obj, 0x60) - 19));
        of<int32_t>(obj, 0x328) = int32_t(k > 6 ? 7 : k);
        if (mem.at<uint8_t>(blk + 0xC) == 0)
            sound_cue(0xA0122A);
        set_motion(obj, mem.u32(chariot::hit_motions + 4 * uint32_t(of<int32_t>(obj, 0x328))));
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    if (chariot_motion_end(obj)) {
        chariot_hit_reaction_end(obj);
        return;
    }
    const uint32_t c = G::camera_records;
    auto sub = [&](uint32_t a, uint32_t b) { return fbits(mem.f32(a) - mem.f32(b)); };
    if (pm_boss()) {
        // PC 0x5169CC (its part after the motion): the offset from camera record 0's eye in the camera's heading
        // frame (x, 0, z); x below -40: back to -40; Chapter 1: z above 80 back to 80, Chapter 4: z below -100 back
        // to -100 (both with the first x); the arcade has only a z limit (80, or 60 after Chapter 1)
        const uint32_t yaw = mem.u32(c + 0x10);
        pm::Matrix m;
        m.rotate_y(yaw);
        float l[3];
        m.transform(ffrom(sub(obj + 0x20, c)), 0.0f, ffrom(sub(obj + 0x28, c + 8)), l[0], l[1], l[2]);
        auto back = [&](float x, float z) {
            pm::Matrix n;
            n.rotate_y(0u - yaw);
            float o[3];
            n.transform(x, 0.0f, z, o[0], o[1], o[2]);
            of<float>(obj, 0x20) = mem.f32(c) + o[0];
            of<float>(obj, 0x28) = mem.f32(c + 8) + o[2];
        };
        if (!(l[0] >= -40.0f))
            back(-40.0f, l[2]);
        bool clamp = false;
        float z = 0.0f;
        if (mem.at<uint8_t>(G::chapter) == 0 && l[2] > 80.0f) {
            clamp = true;
            z = 80.0f;
        }
        if (mem.at<uint8_t>(G::chapter) == 3 && !(l[2] >= -100.0f)) {
            clamp = true;
            z = -100.0f;
        }
        if (clamp)
            back(l[0], z);
        mem.set32(blk, 1);
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x10, {});
    cam_copro(0x15, {mem.u32(c + 0x10)});
    Tgp::Out o = cam_copro(0x1A, {sub(obj + 0x20, c), 0, sub(obj + 0x28, c + 8)});
    const uint32_t lim = chariot::push_limit + (mem.at<uint8_t>(G::chapter) != 0 ? 8 : 0);
    float z;
    std::memcpy(&z, &o.w[2], 4);
    if (!(z <= mem.f32(lim))) {
        cam_copro(0x10, {});
        cam_copro(0x15, {0u - mem.u32(c + 0x10)});
        Tgp::Out p = cam_copro(0x1A, {o.w[0], 0, mem.u32(chariot::push_limit + (mem.at<uint8_t>(G::chapter) != 0 ? 8 : 0))});
        float px, pz;
        std::memcpy(&px, &p.w[0], 4);
        std::memcpy(&pz, &p.w[2], 4);
        of<float>(obj, 0x20) = px + mem.f32(c);
        of<float>(obj, 0x28) = pz + mem.f32(c + 8);
    }
    cam_copro(0x06, {});
    mem.set32(blk, 1);
}

// 0x99210 chariot_hit_reaction_end: back to the script: sequence 15 (unarmoured) or 14, hits taken again (armoured:
// unless the hit points are still the record's)
template <class P> void Game<P>::chariot_hit_reaction_end(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    mem.at<uint8_t>(blk + 0xD) = 0;
    uint8_t seq;
    if (mem.at<uint8_t>(blk + 0xC) != 0) {
        of<uint32_t>(obj, 0x58) &= ~(1u << 28);
        seq = 15;
    } else {
        if (of<uint16_t>(obj, 0x70) != mem.at<uint16_t>(blk + 0x14))
            of<uint32_t>(obj, 0x58) &= ~(1u << 28);
        seq = 14;
    }
    mem.at<uint8_t>(blk + 0xE) = seq;
    of<int16_t>(obj, 0x60) = 0;
    // DEVIATION (soft-lock guard, not in the original): after Chapter 1 the fight starts while the Chariot is still
    // dropping (action 2), and a hand hit in the air (reaction 19) ends the drop; the original then walks on 78+ units
    // above the floor, never reaching its step distance (Chapter 4's event 0 waits on it for ever: bot seed 3, 2P).
    // Here the drop goes on (action 2 from its falling step), then the script as above. The recording
    // play_us_20261007_2238 never reaches this case (capture ch4p_chariot: 28 calls, 0 differ).
    if (of<float>(obj, 0x24) > mem.f32(G::floor_y)) {
        of<int16_t>(obj, 0x60) = 2;
        of<int16_t>(obj, 0x66) = 1;
    }
}

// 0x99260 chariot_act17_wait_combat: MO 17 (the pose, restarted at its end) until combat_active (chariot_frame)
template <class P> void Game<P>::chariot_act17_wait_combat(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    if (of<int16_t>(obj, 0x66) == 0) {
        set_motion_blend(obj, 17, 8, 7);
        of<int16_t>(obj, 0x66) = int16_t(of<uint16_t>(obj, 0x66) + 1);
    }
    if (chariot_motion_end(obj))
        set_motion_blend(obj, 17, 8, 7);
    mem.set32(blk, 1);
}

// 0x992D0 chariot_draw: shadow, the skin flag, the body; the frame counter by the block's advance flag; its position
// in camera space (stored matrix 5: +0x98, +0x9C, the distance +0xA0), hit bits cleared, depth list
template <class P> void Game<P>::chariot_draw(uint32_t obj)
{
    const uint32_t blk = of<uint32_t>(obj, 0x37C);
    of<uint32_t>(obj, 0x50) += 1;
    shadow_setup(mem.f32(G::floor_y));
    chariot_skin_flag(obj);
    object_draw(obj);
    of<uint32_t>(obj, 0xF8) += mem.u32(blk);
    mem.set32(blk, 0);
    if (pm_boss()) {   // PC 0x516CF4: stored matrix 5 through the Chariot file's transform
        float o[3];
        pm_m5().transform(of<float>(obj, 0x20), of<float>(obj, 0x24), of<float>(obj, 0x28), o[0], o[1], o[2]);
        of<float>(obj, 0x98) = o[0];
        of<float>(obj, 0x9C) = o[1];
        of<float>(obj, 0xA0) = o[2];
        of<uint32_t>(obj, 0x58) &= 0xFFFFFEF9u;
        depth_list_add(obj);
        return;
    }
    cam_copro(0x74, {5});
    Tgp::Out o = cam_copro(0x1A, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    of<uint32_t>(obj, 0x98) = o.w[0];
    of<uint32_t>(obj, 0x9C) = o.w[1];
    cam_copro(0x06, {});
    of<uint32_t>(obj, 0x58) &= 0xFFFFFEF9u;
    of<uint32_t>(obj, 0xA0) = o.w[2];
    depth_list_add(obj);
}

// 0x993A0 chariot_hitbox_task: a shot target at the weapon hand (its position written by the part draw hook), the
// Chariot's type; ends when the Chariot is dead (block +8 bit 0)
template <class P> void Game<P>::chariot_hitbox_task(uint32_t t)
{
    const uint32_t owner = mem.u32(t + 0x18);
    mem.at<uint16_t>(t + 0xCC) = of<uint16_t>(owner, 0xCC);
    const uint32_t blk = of<uint32_t>(owner, 0x37C);
    if (mem.at<uint8_t>(blk + 8) & 1) {
        mem.set32(blk + 4, mem.u32(blk + 4) & ~1u);
        task_exit();
        return;
    }
    if (pm_boss()) {
        // PC 0x516DB8: stored matrix 0; on the depth list (shootable) only once the Chariot is down (the PC's flag
        // 0xDA67FC: set up 1 in Chapter 1, else 0 until chariot_act2_land lands; the arcade has no such flag)
        float o[3];
        pm_m0().transform(mem.f32(t + 0x20), mem.f32(t + 0x24), mem.f32(t + 0x28), o[0], o[1], o[2]);
        mem.set32(t + 0x38, fbits(o[0]));
        mem.set32(t + 0x3C, fbits(o[1]));
        mem.set32(t + 0x40, fbits(o[2]));
        mem.set32(t + 0x58, mem.u32(t + 0x58) & 0xFFFFFEF9u);
        if (pm_chariot_landed)
            depth_list_add(t);
        return;
    }
    cam_copro(0x74, {0});
    Tgp::Out o = cam_copro(0x1A, {mem.u32(t + 0x20), mem.u32(t + 0x24), mem.u32(t + 0x28)});
    mem.set32(t + 0x38, o.w[0]);
    mem.set32(t + 0x3C, o.w[1]);
    cam_copro(0x06, {});
    mem.set32(t + 0x58, mem.u32(t + 0x58) & 0xFFFFFEF9u);
    mem.set32(t + 0x40, o.w[2]);
    depth_list_add(t);
}

// 0x99450 chariot_armour_off (obj, motion, frame): the body again as type 36 (no armour) at the motion's frame, the
// part index, the plain part draw dispatch, the plates' states
template <class P> void Game<P>::chariot_armour_off(uint32_t obj, uint32_t motion, uint32_t frame)
{
    body_free_parts(obj);
    of<int16_t>(obj, 0xCC) = 36;
    mem.set32(G::hit_type, 36);
    body_init(obj, 36, motion);
    of<uint16_t>(obj, 0xF4) = uint16_t(frame);
    of<uint32_t>(obj, 0xF8) = frame & 0xFFFF;
    chariot_part_index_build(obj);
    of<uint32_t>(obj, 0x11C) = F::chariot_part_draw_dispatch;
    chariot_plates_off(obj);
}

// 0x994B0 chariot_plate_state_set (part): the starting plate state of its role, for the subtree
template <class P> void Game<P>::chariot_plate_state_set(uint32_t part)
{
    mem.at<uint8_t>(part + 0x79) = mem.at<uint8_t>(chariot::plate_states + 8 * mem.u32(part + 0x38));
    for (int32_t i = 0; i < mem.at<int32_t>(part + 0x34); ++i)
        chariot_plate_state_set(mem.u32(mem.u32(part + 0x24) + 4 * uint32_t(i)));
}

// 0x99500 chariot_plates_off: the plate states of every part below the root (the body is current)
template <class P> void Game<P>::chariot_plates_off(uint32_t obj)
{
    const uint32_t root = of<uint32_t>(obj, 0xC8);
    mem.set32(G::body_current, obj);
    for (int32_t i = 0; i < mem.at<int32_t>(root + 0x34); ++i) {
        const uint32_t c = mem.u32(mem.u32(root + 0x24) + 4 * uint32_t(i));
        mem.at<uint8_t>(c + 0x79) = mem.at<uint8_t>(chariot::plate_states + 8 * mem.u32(c + 0x38));
        for (int32_t j = 0; j < mem.at<int32_t>(c + 0x34); ++j)
            chariot_plate_state_set(mem.u32(mem.u32(c + 0x24) + 4 * uint32_t(j)));
    }
}

// 0x99570 chariot_armour_piece_fade: a fallen armour piece: shown 80 frames, hidden until 118, then gone; drawn at its
// position and angles with the model +0x320 + timer / 2
template <class P> void Game<P>::chariot_armour_piece_fade(uint32_t t)
{
    switch (mem.at<int16_t>(t + 0x66)) {
    case 0: {
        const int32_t n = mem.at<int32_t>(t + 0x31C) + 1;
        mem.at<int32_t>(t + 0x31C) = n;
        if (n <= 0x4F)
            break;
        mem.at<uint8_t>(t + 0x3C4) |= 4;
        mem.at<uint16_t>(t + 0x66) = uint16_t(mem.at<uint16_t>(t + 0x66) + 1);
    }
        [[fallthrough]];
    case 1: {
        const int32_t n = mem.at<int32_t>(t + 0x31C) + 1;
        mem.at<int32_t>(t + 0x31C) = n;
        if (n <= 0x75)
            break;
    }
        [[fallthrough]];
    case 2:
        task_exit();
        return;
    default: break;
    }
    if ((mem.at<uint8_t>(t + 0x3C4) >> 2) & 1)
        return;
    mem.at<int32_t>(t + 0x54) = mem.at<int32_t>(t + 0x320) + mem.at<int32_t>(t + 0x31C) / 2;
    cam_copro(0x05, {});
    cam_copro(0x12, {mem.u32(t + 0x20), mem.u32(t + 0x24), mem.u32(t + 0x28)});
    cam_copro(0x86, {mem.u32(t + 0x34), mem.u32(t + 0x30), mem.u32(t + 0x2C)});
    geo_draw_model(mem.u32(t + 0x54));
    cam_copro(0x06, {});
}

// 0x99660 chariot_armour_pieces_spawn (part): for each part with a model in the subtree a falling piece task (model /
// keep from 0xCA7B0 by role) at the part's last matrix (world position and angles through stored matrix 1)
template <class P> void Game<P>::chariot_armour_pieces_spawn(uint32_t part)
{
    if (mem.u32(part) != 0) {
        const uint32_t t = task_create(F::chariot_armour_piece_reset, 0x3C8);
        task_clear_work(t);
        const uint32_t role = mem.u32(part + 0x38);
        mem.at<int32_t>(t + 0x320) = mem.at<int16_t>(chariot::piece_fall + 16 * role);
        for (uint32_t o : {0x80u, 0x7Cu, 0x78u, 0x8Cu, 0x88u, 0x84u})
            mem.set32(t + o, 0);
        mem.at<int32_t>(t + 0x324) = mem.at<int16_t>(chariot::piece_fall + 2 + 16 * role);
        const uint32_t m = chariot::part_matrices + 48 * role;
        if (pm_boss()) {
            // PC 0x51712F: the kept part matrix times stored matrix 1 (mul_stored 0x514140), its angles
            // (pc_matrix_angles 0x514904) and translation
            pm::Matrix w = pm_copro_words_at(m);
            w.mul_stored(pm_camera);
            uint32_t a = 0, b = 0, c = 0;
            pm::matrix_angles8(w, a, b, c);
            mem.set32(t + 0x2C, a);
            mem.set32(t + 0x30, b);
            mem.set32(t + 0x34, c);
            mem.set32(t + 0x20, fbits(w.e[9]));
            mem.set32(t + 0x24, fbits(w.e[10]));
            mem.set32(t + 0x28, fbits(w.e[11]));
        } else {
            Tgp::Out none;
            uint32_t w[12];
            for (uint32_t i = 0; i < 12; ++i)
                w[i] = mem.u32(m + 4 * i);
            copro(0x7F, w, 12, none);
            cam_copro(0x77, {1});
            Tgp::Out a = cam_copro(0x71, {});
            mem.set32(t + 0x34, a.w[0]);
            mem.set32(t + 0x30, a.w[1]);
            mem.set32(t + 0x2C, a.w[2]);
            Tgp::Out p = cam_copro(0x20, {});
            mem.set32(t + 0x20, p.w[0]);
            mem.set32(t + 0x24, p.w[1]);
            mem.set32(t + 0x28, p.w[2]);
            cam_copro(0x06, {});
        }
    }
    for (uint32_t i = 0; i < mem.u32(part + 0x34); ++i)
        chariot_armour_pieces_spawn(mem.u32(mem.u32(part + 0x24) + 4 * i));
}

// 0x997C0 chariot_armour_break_parts: the falling pieces of every part below the root
template <class P> void Game<P>::chariot_armour_break_parts(uint32_t obj)
{
    const uint32_t root = of<uint32_t>(obj, 0xC8);
    for (uint32_t i = 0; i < mem.u32(root + 0x34); ++i)
        chariot_armour_pieces_spawn(mem.u32(mem.u32(root + 0x24) + 4 * i));
}

// 0x997F0 chariot_part_index_rec (part): the part of each role (0x543A50), for the subtree
template <class P> void Game<P>::chariot_part_index_rec(uint32_t part)
{
    mem.set32(chariot::parts + 4 * mem.u32(part + 0x38), part);
    for (int32_t i = 0; i < mem.at<int32_t>(part + 0x34); ++i)
        chariot_part_index_rec(mem.u32(mem.u32(part + 0x24) + 4 * uint32_t(i)));
}

// 0x99830 chariot_part_index_build: the part index from the root
template <class P> void Game<P>::chariot_part_index_build(uint32_t obj)
{
    const uint32_t root = of<uint32_t>(obj, 0xC8);
    mem.set32(chariot::parts + 4 * mem.u32(root + 0x38), root);
    for (int32_t i = 0; i < mem.at<int32_t>(root + 0x34); ++i)
        chariot_part_index_rec(mem.u32(mem.u32(root + 0x24) + 4 * uint32_t(i)));
}

// 0x99870 chariot_armour_hit_find (part): a part hit this frame (player 1 / 2 bits) gives the hit position (its
// sphere's camera-space centre) and the effect class of its role's class; for the subtree
template <class P> void Game<P>::chariot_armour_hit_find(uint32_t part)
{
    const int32_t cls = mem.at<int8_t>(chariot::part_class + 8 * mem.u32(part + 0x38));
    if (mem.at<uint8_t>(part + 0x3C) & 6) {
        for (int bit = 1; bit <= 2; ++bit)
            if ((mem.u32(part + 0x3C) >> bit) & 1) {
                mem.set32(chariot::hit_pos, mem.u32(part + 0x5C));
                mem.set32(chariot::hit_pos + 4, mem.u32(part + 0x60));
                mem.set32(chariot::hit_class, uint32_t(int32_t(mem.at<int8_t>(chariot::class_effect + uint32_t(cls)))));
                mem.set32(chariot::hit_pos + 8, mem.u32(part + 0x64));
            }
    }
    for (int32_t i = 0; i < mem.at<int32_t>(part + 0x34); ++i)
        chariot_armour_hit_find(mem.u32(mem.u32(part + 0x24) + 4 * uint32_t(i)));
}

// 0x99940 chariot_armour_hit_test: chariot_armour_hit_find over every part below the root (the first level inline)
template <class P> void Game<P>::chariot_armour_hit_test(uint32_t obj)
{
    const uint32_t root = of<uint32_t>(obj, 0xC8);
    for (int32_t i = 0; i < mem.at<int32_t>(root + 0x34); ++i)
        chariot_armour_hit_find(mem.u32(mem.u32(root + 0x24) + 4 * uint32_t(i)));
}

// 0x99A30 chariot_part_damage_walk (part): per hit (player 1 / 2): one plate less (+0x79, down to 0: the last one
// gives the effect class), the class's damage added, blood; for the subtree
template <class P> void Game<P>::chariot_part_damage_walk(uint32_t part)
{
    const int32_t cls = mem.at<int8_t>(chariot::part_class + 8 * mem.u32(part + 0x38));
    if (mem.at<uint8_t>(part + 0x3C) & 6) {
        for (int bit = 1; bit <= 2; ++bit)
            if ((mem.u32(part + 0x3C) >> bit) & 1) {
                int32_t v = mem.at<int8_t>(part + 0x79);
                if (v > 0)
                    --v;
                mem.at<uint8_t>(part + 0x79) = uint8_t(v);
                if ((v & 0xFF) == 0)
                    mem.set32(chariot::hit_class, uint32_t(int32_t(mem.at<int8_t>(chariot::class_effect + uint32_t(cls)))));
                mem.at<uint16_t>(G::hit_damage) =
                    uint16_t(mem.at<uint16_t>(G::hit_damage) + mem.at<uint16_t>(chariot::class_damage + 16 * uint32_t(cls)));
                blood_burst_spawn(part);
            }
    }
    for (int32_t i = 0; i < mem.at<int32_t>(part + 0x34); ++i)
        chariot_part_damage_walk(mem.u32(mem.u32(part + 0x24) + 4 * uint32_t(i)));
}

// the plates of two neighbouring armour groups shared out (counter a / b of 0x543A00 against the part's plates): while
// the two counters hold more than the part, one of them (the one above 0; both: at random) gives one up
template <class P> void Game<P>::chariot_share_counters(uint32_t ca, uint32_t part)
{
    const uint32_t cb = ca + 4;
    if (mem.at<int32_t>(ca) > 30000) {
        const int32_t v = mem.at<int8_t>(part + 0x79);
        const int32_t h = (v / 2) & 0xFF;
        mem.at<int32_t>(ca) = h;
        mem.at<int32_t>(cb) = v - h;
    }
    int32_t r4 = mem.at<int8_t>(part + 0x79);
    if (r4 >= mem.at<int32_t>(ca) + mem.at<int32_t>(cb))
        return;
    do {
        const int32_t a = mem.at<int32_t>(ca), b = mem.at<int32_t>(cb);
        int32_t m = (a > 0 ? 1 : 0) | (b > 0 ? 2 : 0);
        int32_t pick;
        if (m == 1)
            pick = 0;
        else if (m == 0)
            pick = 2;
        else if (m == 2)
            pick = 1;
        else
            pick = (int32_t(rand()) % 2) != 0 ? 1 : 0;
        int32_t na = mem.at<int32_t>(ca), nb = mem.at<int32_t>(cb);
        if (pick == 0)
            --na;
        else if (pick == 1)
            --nb;
        ++r4;
        mem.at<int32_t>(ca) = na;
        mem.at<int32_t>(cb) = nb;
    } while (r4 < mem.at<int32_t>(ca) + mem.at<int32_t>(cb));
}

// two paired parts' plates evened out (the sum over both halves); one plate or none left: both 0
template <class P> void Game<P>::chariot_even_pair(uint32_t pa, uint32_t counter)
{
    const uint32_t p0 = mem.u32(pa), p1 = mem.u32(pa + 4);
    const int32_t sum = int32_t(mem.at<int8_t>(p0 + 0x79)) + int32_t(mem.at<int8_t>(p1 + 0x79));
    if (sum > 1) {
        mem.at<int32_t>(counter) = sum;
        const int32_t h = sum / 2;
        mem.at<uint8_t>(p0 + 0x79) = uint8_t(h);
        mem.at<uint8_t>(mem.u32(pa + 4) + 0x79) = uint8_t(sum - h);
    } else {
        mem.at<uint8_t>(p1 + 0x79) = 0;
        mem.at<uint8_t>(p0 + 0x79) = 0;
        mem.at<int32_t>(counter) = 0;
    }
}

// 0x99B30 chariot_part_damage (unarmoured phase): chariot_part_damage_walk over the parts below the root (first level
// inline); the 17 group counters from the parts' plates (groups 0 / 9 shared with the next counter, 13 / 16 the
// two halves evened, 1 / 10 kept); parts with no plate left lose their hit sphere (radius 0)
template <class P> void Game<P>::chariot_part_damage(uint32_t obj)
{
    const uint32_t root = of<uint32_t>(obj, 0xC8);
    for (int32_t i = 0; i < mem.at<int32_t>(root + 0x34); ++i)
        chariot_part_damage_walk(mem.u32(mem.u32(root + 0x24) + 4 * uint32_t(i)));
    for (uint32_t r8 = 0; r8 <= 16; ++r8) {
        const uint32_t c = chariot::counters + 4 * r8;
        switch (r8) {
        case 0: chariot_share_counters(chariot::counters, mem.u32(chariot::parts + 4)); break;
        case 9: chariot_share_counters(chariot::counters + 0x24, mem.u32(chariot::parts + 0x24)); break;
        case 13: chariot_even_pair(chariot::parts + 0x30, chariot::counters + 0x34); break;
        case 16: chariot_even_pair(chariot::parts + 0x40, chariot::counters + 0x40); break;
        case 1: case 10: break;
        default: {
            const int32_t role = mem.at<int8_t>(chariot::group_role + r8);
            mem.at<int32_t>(c) = mem.at<int8_t>(mem.u32(chariot::parts + 4 * uint32_t(role)) + 0x79);
            break;
        }
        }
    }
    for (uint32_t i = 0; i < 18; ++i) {
        const uint32_t p = mem.u32(chariot::parts + 4 * i);
        if (mem.at<uint8_t>(p + 0x79) == 0)
            mem.set32(p + 0x74, 0);
    }
}

// the class of the current body's role (8 / 9: the weapon hands) and the part matrices the hit box / pieces use;
// the part draw hooks' common head (0x9A040 / 0x9A660)
template <class P> int32_t Game<P>::chariot_role_class(uint32_t part)
{
    const uint32_t body = mem.u32(G::body_current);
    return mem.at<int8_t>(chariot::role_classes + 248 * uint32_t(int32_t(of<int16_t>(body, 0xCC))) + 3 + 8 * mem.u32(part + 0x38));
}

template <class P> void Game<P>::chariot_hand_matrix(int32_t cls)
{
    const uint32_t body = mem.u32(G::body_current);
    if (cls != 8 && cls != 9)
        return;
    Tgp::Out m = cam_copro(0x11, {});
    const uint32_t to = body + (cls == 8 ? 0x184 : 0x1B4);
    for (uint32_t i = 0; i < 12; ++i)
        mem.set32(to + 4 * i, m.w[i]);
}

template <class P> void Game<P>::chariot_home_point(int32_t cls)
{
    if (uint32_t(cls) != mem.u32(0x14A4BC))
        return;
    if (pm_boss()) {
        // PC 0x5177A0: the part's translation through stored matrix 1 (the Chariot file's transform)
        const pm::Matrix part = pm_copro_current();
        const uint32_t body = mem.u32(G::body_current);
        float o[3];
        pm_m1().transform(part.e[9], part.e[10], part.e[11], o[0], o[1], o[2]);
        mem.set32(body + 0x2D4, fbits(o[0]));
        mem.set32(body + 0x2D8, fbits(o[1]));
        mem.set32(body + 0x2DC, fbits(o[2]));
        return;
    }
    cam_copro(0x05, {});
    Tgp::Out p = cam_copro(0x20, {});
    cam_copro(0x73, {1});
    Tgp::Out o = cam_copro(0x1A, {p.w[0], p.w[1], p.w[2]});
    const uint32_t body = mem.u32(G::body_current);
    mem.set32(body + 0x2D4, o.w[0]);
    mem.set32(body + 0x2D8, o.w[1]);
    cam_copro(0x06, {});
    mem.set32(body + 0x2DC, o.w[2]);
}

// 0x9A040 chariot_part_draw_hook (armoured body's +0x11C): at the weapon hand (class 8) the hit box placed (a point
// of the hand through stored matrix 1) and the hand's matrix kept (+0x184; class 9: +0x1B4); the body's home point;
// the part's matrix kept (0x543650, for the falling pieces); the part's model
template <class P> void Game<P>::chariot_part_draw_hook(uint32_t part)
{
    const uint32_t body = mem.u32(G::body_current);
    const int32_t cls = chariot_role_class(part);
    const uint32_t hb = of<uint32_t>(body, 0x360);
    if (cls == 8 && pm_boss()) {
        // PC 0x517723 (the armoured body's part hook 0x5176CC): the part matrix (here the arcade body drawing's, in
        // the PC's layout) times stored matrix 1 (mul_stored 0x514140), the hand's point through it
        pm::Matrix m = pm_copro_current();
        m.mul_stored(pm_camera);
        float o[3];
        m.transform(2.5f, -2.1f, -0.8f, o[0], o[1], o[2]);
        mem.set32(hb + 0x20, fbits(o[0]));
        mem.set32(hb + 0x24, fbits(o[1]));
        mem.set32(hb + 0x28, fbits(o[2]));
    } else if (cls == 8) {
        cam_copro(0x05, {});
        cam_copro(0x77, {1});
        Tgp::Out o = cam_copro(0x1A, {0x40200000, 0xC0066666, 0xBF4CCCCD});   // (2.5, -2.1, -0.8)
        mem.set32(hb + 0x20, o.w[0]);
        mem.set32(hb + 0x24, o.w[1]);
        cam_copro(0x06, {});
        mem.set32(hb + 0x28, o.w[2]);
    }
    chariot_hand_matrix(cls);
    chariot_home_point(cls);
    Tgp::Out m = cam_copro(0x11, {});
    const uint32_t to = chariot::part_matrices + 48 * mem.u32(part + 0x38);
    for (uint32_t i = 0; i < 12; ++i)
        mem.set32(to + 4 * i, m.w[i]);
    if (pm_body()) {   // pmbody: the PC keeps its own part matrix there (in the coprocessor's layout here)
        const pm::Matrix& c = pm_cur();
        for (uint32_t row = 0; row < 3; ++row)
            for (uint32_t col = 0; col < 3; ++col)
                mem.set32(to + 4 * (col * 3 + row), fbits(c.e[row * 3 + col]));
        for (uint32_t k = 9; k < 12; ++k)
            mem.set32(to + 4 * k, fbits(c.e[k]));
    }
    geo_draw_model(mem.u32(part));
}

// 0x9A280 chariot_part_draw_dispatch (unarmoured body's +0x11C): the role's draw routine (table 0xCABB0)
template <class P> void Game<P>::chariot_part_draw_dispatch(uint32_t part)
{
    const uint32_t routine = mem.u32(chariot::part_draws + 4 * mem.u32(part + 0x38));
    switch (routine) {
    case F::chariot_part_draw_plate: chariot_part_draw_plate(part); break;
    case F::chariot_part_draw_chest: chariot_part_draw_chest(part); break;
    case F::chariot_part_draw_waist: chariot_part_draw_waist(part); break;
    case F::chariot_part_draw_pair: chariot_part_draw_pair(part); break;
    default: call_routine(routine, part); break;
    }
}

// the model by plate state k (0xCA920), then chariot_part_draw
template <class P> void Game<P>::chariot_plate_model(uint32_t part, uint32_t k)
{
    mem.set32(part, uint32_t(int32_t(mem.at<int16_t>(chariot::plate_models + 12 * mem.u32(part + 0x38) + 2 * k))));
    chariot_part_draw(part);
}

// 0x9A2A0 chariot_part_draw_plate: the plate's model by its state (2 plates / 1 / none)
template <class P> void Game<P>::chariot_part_draw_plate(uint32_t part)
{
    const int32_t v = mem.at<int8_t>(part + 0x79);
    chariot_plate_model(part, v == 1 ? 1 : v == 2 ? 0 : 2);
}

// 0x9A2E0 chariot_part_draw_chest: by the chest counters (0, 1; 10 the core): the opened chest's extra models
// (0x1091 / 0x1092 / 0x1093) and the beating heart (0x109A + frame % 30 at a fixed offset)
template <class P> void Game<P>::chariot_part_draw_chest(uint32_t part)
{
    const int32_t c0 = mem.at<int32_t>(chariot::counters), c1 = mem.at<int32_t>(chariot::counters + 4);
    const int32_t c3 = mem.at<int32_t>(chariot::counters + 0xC), c10 = mem.at<int32_t>(chariot::counters + 0x28);
    bool r9 = c10 <= 0, r7 = false, g3 = false, r5 = false;
    uint32_t k;
    switch ((c0 > 0 ? 1 : 0) | (c1 > 0 ? 2 : 0)) {
    case 0:
        if (c10 > 0) {
            k = 4;
        } else {
            k = 1;
            r9 = false;
        }
        r5 = r7 = g3 = true;
        break;
    case 1: k = 3; r5 = r7 = true; break;
    case 2: k = 2; g3 = true; break;
    default: k = 0; break;
    }
    if (g3 && c3 > 0)
        geo_draw_model(0x1092);
    if (r7 && mem.at<int32_t>(chariot::counters + 0x18) > 0)
        geo_draw_model(0x1091);
    if (r5) {
        const int32_t f = int32_t(mem.u32(G::blink_phase)) % 30;
        cam_copro(0x05, {});
        cam_copro(0x12, {0x3FDBB646, 0xBF925461, 0x3FC72B02});
        geo_draw_model(uint32_t(0x109A + f));
        cam_copro(0x06, {});
    }
    if (r9)
        geo_draw_model(0x1093);
    chariot_plate_model(part, k);
}

// 0x9A450 chariot_part_draw_waist: the waist's model by the counters 9-11 and 14; 0x107F unless the skin flag is set
template <class P> void Game<P>::chariot_part_draw_waist(uint32_t part)
{
    const uint32_t c = chariot::counters + 0x20;
    uint32_t m = (mem.at<int32_t>(c + 4) > 0 ? 1u : 0u) | (mem.at<int32_t>(c + 8) > 0 ? 2u : 0u);
    if (mem.at<int32_t>(c + 0xC) > 0)
        m |= 4;
    if (mem.at<int32_t>(chariot::counters + 0x38) > 0)
        m |= 8;
    uint32_t k;
    if (m & 1) {
        k = 0;
    } else {
        switch (m & 14) {
        case 0: k = 1; break;
        case 2: k = 2; break;
        case 4: case 6: k = 3; break;
        case 8: case 10: k = 4; break;
        default: k = 5; break;
        }
    }
    const uint32_t body = mem.u32(G::body_current);
    if (!((of<uint8_t>(body, 0x3C4) >> 4) & 1) && k != 1)
        geo_draw_model(0x107F);
    chariot_plate_model(part, k);
}

// 0x9A550 chariot_part_draw_pair: a part whose model shows its own counter and its two neighbours' (0xCA8D0)
template <class P> void Game<P>::chariot_part_draw_pair(uint32_t part)
{
    const int32_t i = mem.at<int8_t>(chariot::pair_group + mem.u32(part + 0x38));
    const int32_t a = mem.at<int16_t>(chariot::pair_links + 4 * uint32_t(i));
    const int32_t b = mem.at<int16_t>(chariot::pair_links + 2 + 4 * uint32_t(i));
    const int32_t v = mem.at<int32_t>(chariot::counters + 4 * uint32_t(i));
    const int32_t va = a >= 0 ? mem.at<int32_t>(chariot::counters + 4 * uint32_t(a)) : 0;
    const int32_t vb = b >= 0 ? mem.at<int32_t>(chariot::counters + 4 * uint32_t(b)) : 0;
    uint32_t k;
    if (v > 0) {
        k = 0;
    } else {
        switch ((va > 0 ? 1 : 0) | (vb > 0 ? 2 : 0)) {
        case 0: k = 1; break;
        case 1: k = 4; break;
        case 2: k = 3; break;
        default: k = 2; break;
        }
    }
    chariot_plate_model(part, k);
}

// 0x9A660 chariot_part_draw: the hand matrices and home point as in the hook; the part's extra pieces while it has
// plates (0xCAA00 list: at their offsets and angles, model 0x10B8 + the body's timer % 30); the part's model
template <class P> void Game<P>::chariot_part_draw(uint32_t part)
{
    const int32_t cls = chariot_role_class(part);
    chariot_hand_matrix(cls);
    chariot_home_point(cls);
    if (mem.at<int8_t>(part + 0x79) > 0) {
        uint32_t list = chariot::piece_list + 6 * mem.u32(part + 0x38);
        for (int32_t e = mem.at<int8_t>(list++); e != -1; e = mem.at<int8_t>(list++)) {
            const uint32_t r = chariot::pieces + 24 * uint32_t(e);
            cam_copro(0x05, {});
            cam_copro(0x12, {mem.u32(r), mem.u32(r + 4), mem.u32(r + 8) ^ 0x80000000u});
            const uint32_t body = mem.u32(G::body_current);
            cam_copro(0x86, {0u - mem.u32(r + 0x14), mem.u32(r + 0x10), mem.u32(r + 0xC)});
            geo_draw_model(uint32_t(0x10B8 + int32_t(of<uint32_t>(body, 0x50)) % 30));
            cam_copro(0x06, {});
        }
    }
    geo_draw_model(mem.u32(part));
}

// 0x9A890 chariot_skin_flag: +0x3C4 bit 4 = the waist counter (0x543A28) above 0
template <class P> void Game<P>::chariot_skin_flag(uint32_t obj)
{
    const uint32_t g = mem.at<int32_t>(chariot::counters + 0x28) > 0 ? 1 : 0;
    of<uint8_t>(obj, 0x3C4) = uint8_t((of<uint8_t>(obj, 0x3C4) & ~0x10u) | (g << 4));
}

// 0x9A8C0 chariot_title_card (Chapter 1): at camera frame 390 the camera's eye / look-at kept, the game camera held
// (0x51FE24), the title sprites 0xDC / 0xDF placed; for 180 frames the camera on path 32; then the camera back, the
// sprites cleared, gone. The sprites are boundaries (the HUD layer: R4)
template <class P> void Game<P>::chariot_title_card(uint32_t t)
{
    const uint32_t c = G::camera_records;
    switch (mem.at<int16_t>(t + 0x66)) {
    case 0:
        if (mem.f32(G::camera_frame) != 390.0f)
            break;
        for (uint32_t i = 0; i < 3; ++i) {
            mem.set32(t + 0x394 + 4 * i, mem.u32(c + 4 * i));
            mem.set32(t + 0x3A0 + 4 * i, mem.u32(c + 0x18 + 4 * i));
        }
        boss_event(kBossEntrance);   // PC 0x517CFB
        mem.set32(G::camera_hold_hostage, 1);
        sprite_place(0x1004000, 1, 0, 33, 5, 0xDC);
        sprite_place(0x1004000, 1, 0, 7, 5, 0xDF);
        mem.at<uint16_t>(t + 0x66) = uint16_t(mem.at<uint16_t>(t + 0x66) + 1);
        mem.set32(t + 0x50, 0);
        [[fallthrough]];
    case 1:
        if (mem.at<int32_t>(t + 0x50) == 180) {
            for (uint32_t i = 0; i < 3; ++i) {
                mem.set32(c + 4 * i, mem.u32(t + 0x394 + 4 * i));
                mem.set32(c + 0x18 + 4 * i, mem.u32(t + 0x3A0 + 4 * i));
            }
            mem.set32(G::camera_hold_hostage, 0);
            sprite_draw(0x1004000, 33, 5, 0xDC);
            sprite_draw(0x1004000, 7, 5, 0xDF);
            boss_event(kBossClockStart);   // PC 0x517DFC
            task_exit();
            return;
        } else {
            LocalScope scope(*this);
            const uint32_t loc = local_alloc(8);
            camera_path_eval(32, fbits(float(mem.at<int32_t>(t + 0x50))), c, c + 0x18, loc, loc + 4);
        }
        break;
    default: break;
    }
    mem.set32(t + 0x50, mem.u32(t + 0x50) + 1);
}

// 0x9AA30 chariot_life_bar_task: once the camera has stopped (camera_stopped 2), after 20 frames the boss life bar
// (life_bar_init 11, the hit points as maximum), updated with the hit points; at 0 emptied, then the block's bit 1
// cleared and gone. The bar's drawing (life_bar_init / _update) is a boundary (HUD: R4)
template <class P> void Game<P>::chariot_life_bar_task(uint32_t t)
{
    const uint32_t owner = mem.u32(t + 0x18);
    const uint32_t blk = of<uint32_t>(owner, 0x37C);
    switch (mem.at<int16_t>(t + 0x66)) {
    case 0:
        if (mem.u32(G::camera_stopped) != 2)
            return;
        mem.set32(t + 0x50, 0);
        mem.at<uint16_t>(t + 0x66) = uint16_t(mem.at<uint16_t>(t + 0x66) + 1);
        [[fallthrough]];
    case 1: {
        const int32_t n = mem.at<int32_t>(t + 0x50) + 1;
        mem.at<int32_t>(t + 0x50) = n;
        if (n != 20)
            return;
        coop4_boss_scale(owner + ob(0x70), owner + ob(0x70));   // 4P: the hit points for 3 / 4 players
        life_bar_init(11, 1, of<int16_t>(owner, 0x70));
        mem.at<uint16_t>(t + 0x66) = uint16_t(mem.at<uint16_t>(t + 0x66) + 1);
    }
        [[fallthrough]];
    case 2:
        if (of<uint16_t>(owner, 0x70) != 0) {
            life_bar_update(of<int16_t>(owner, 0x70), 0);
            return;
        }
        mem.at<uint16_t>(t + 0x66) = uint16_t(mem.at<uint16_t>(t + 0x66) + 1);
        [[fallthrough]];
    case 3:
        if (int32_t(life_bar_update(of<int16_t>(owner, 0x70), 1)) == -1) {
            mem.set32(blk + 4, mem.u32(blk + 4) & ~2u);
            task_exit();
        }
        return;
    default: return;
    }
}

// 0x9AB00 chariot_spark (point): a spark task (0x9AE20, model 0x8A) at the camera-space point 4 units nearer,
// placed in the world through stored matrix 1
template <class P> void Game<P>::chariot_spark(uint32_t point)
{
    const uint32_t t = task_create(F::chariot_spark_anim, 0x3C8);
    cam_copro(0x74, {1});
    Tgp::Out o = cam_copro(0x1A, {mem.u32(point), mem.u32(point + 4), fbits(mem.f32(point + 8) - 4.0f)});
    mem.set32(t + 0x20, o.w[0]);
    mem.set32(t + 0x24, o.w[1]);
    cam_copro(0x06, {});
    mem.set32(t + 0x54, 0x8A);
    mem.set32(t + 0x28, o.w[2]);
}

// 0x9AB90 chariot_hit_glow: the glow on the hit box (+0x35C) facing the camera, 2x, its size kept on screen (depth
// from 15 on); models 0x209..0x263; gone when the owner is no longer type 35
template <class P> void Game<P>::chariot_hit_glow(uint32_t t)
{
    const uint32_t owner = mem.u32(t + 0x35C);
    const int32_t m = mem.at<int32_t>(t + 0x54) + 1;
    mem.at<int32_t>(t + 0x54) = m;
    if (m == 0x264 || of<uint16_t>(owner, 0xCC) != 35) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(owner, 0x20), of<uint32_t>(owner, 0x24), of<uint32_t>(owner, 0x28)});
    Tgp::Out none;
    uint32_t r[9];
    for (uint32_t i = 0; i < 9; ++i)
        r[i] = mem.u32(chariot::glow_rot + 4 * i);
    copro(0x87, r, 9, none);
    cam_copro(0x13, {0x40000000, 0x40000000, 0x40000000});
    Tgp::Out o = cam_copro(0x20, {});
    float x, y, z;
    std::memcpy(&x, &o.w[0], 4);
    std::memcpy(&y, &o.w[1], 4);
    std::memcpy(&z, &o.w[2], 4);
    const float d = float(double(z) - 5.0);
    const float s = d >= 10.0f ? d : 10.0f;
    const float nx = (x * s) / z, ny = (y * s) / z;
    cam_copro(0x88, {fbits(nx), fbits(ny), fbits(s)});
    geo_draw_model(mem.u32(t + 0x54));
    cam_copro(0x06, {});
}

// 0x9AD60 chariot_hit_glow_spawn (hit box): the glow task, model 0x209
template <class P> void Game<P>::chariot_hit_glow_spawn(uint32_t hb)
{
    const uint32_t t = task_create(F::chariot_hit_glow, 0x3C8);
    mem.set32(t + 0x35C, hb);
    mem.set32(t + 0x54, 0x209);
}

// 0x9AD90 chariot_landing_dust: a dust task (0x9AF10) on the floor below it, model 0xD39
template <class P> void Game<P>::chariot_landing_dust(uint32_t obj)
{
    const uint32_t t = task_create(F::chariot_landing_dust_anim, 0x58);
    mem.set32(t + 0x30, 0);
    mem.set32(t + 0x20, of<uint32_t>(obj, 0x20));
    mem.set32(t + 0x24, mem.u32(G::floor_y));
    mem.set32(t + 0x54, 0xD39);
    mem.set32(t + 0x28, of<uint32_t>(obj, 0x28));
}

// 0x9ADD0 chariot_armour_piece_reset: a falling piece's first frame: hidden bit off, +0x40 = 32768; with no keep flag
// (+0x324) the fade, else gone
template <class P> void Game<P>::chariot_armour_piece_reset(uint32_t t)
{
    mem.at<int16_t>(t + 0x66) = 0;
    mem.set32(t + 0x31C, 0);
    mem.set32(t + 0x40, 0x47000000);
    mem.at<uint8_t>(t + 0x3C4) &= 0xFBu;
    if (mem.u32(t + 0x324) != 0) {
        task_exit();
        return;
    }
    mem.set32(t, F::chariot_armour_piece_fade);
    chariot_armour_piece_fade(t);
}

// 0x9AE20 chariot_spark_anim: models 0x8A..0xA0 facing the camera, 2x, then gone
template <class P> void Game<P>::chariot_spark_anim(uint32_t t)
{
    const int32_t m = mem.at<int32_t>(t + 0x54) + 1;
    mem.at<int32_t>(t + 0x54) = m;
    if (m == 0xA1) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {mem.u32(t + 0x20), mem.u32(t + 0x24), mem.u32(t + 0x28)});
    Tgp::Out none;
    uint32_t r[9];
    for (uint32_t i = 0; i < 9; ++i)
        r[i] = mem.u32(chariot::glow_rot + 4 * i);
    copro(0x87, r, 9, none);
    cam_copro(0x13, {0x40000000, 0x40000000, 0x40000000});
    geo_draw_model(mem.u32(t + 0x54));
    cam_copro(0x06, {});
}

// 0x9AF10 chariot_landing_dust_anim: models 0xD39..0xD57 at its position and heading, then gone
template <class P> void Game<P>::chariot_landing_dust_anim(uint32_t t)
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
    cam_copro(0x13, {0x3F800000, 0x3F800000, 0x3F800000});
    geo_draw_model(mem.u32(t + 0x54));
    cam_copro(0x06, {});
}

} // namespace hotd::port
