// Enemies: the standard enemy's per-frame routine and its helpers, A and PC (PA / PM) (docs/decompilation.md "Enemies:
// deep pass"), read from the disassembly of both (Ghidra's C drops the action = 0 stores of the head-lost case).
#include "port/game.h"

#include <cstring>

namespace hotd::port {

// the leave sequence (PC 0x4D9694 enemy_leave; the arcade has it inline in enemy_main): not shootable,
// enemy_left_route, target released, attack slot freed, enemies_alive / enemy_count - 1, parts and extras freed,
// task_exit
template <class P> void Game<P>::enemy_leave(uint32_t obj)
{
    Enemy& e = mem.at<Enemy>(obj);
    mem.at<uint8_t>(G::enemy_left_route) = 1;
    e.flags &= ~1u;
    if (uint8_t(e.target) != 0xFF) {
        uint32_t t = G::player_targeted + uint32_t(int32_t(e.target));
        if (mem.at<uint8_t>(t) != 0)
            mem.at<uint8_t>(t) = 0;
        e.target = -1;
    }
    if (uint8_t(e.attack_slot) != 0xFF)
        enemy_attack_slot_free(obj);
    mem.at<int16_t>(G::enemies_alive) = int16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
    mem.at<int16_t>(G::enemy_count) = int16_t(mem.at<uint16_t>(G::enemy_count) - 1);
    body_free_parts(obj);
    enemy_free_extras(obj);
    task_exit();
}

// 0x76FC0 / PC 0x4D95F8 kind10_remove (the arcade's enemy_ai state 8 has the same code inline): route_choice = 1 with flags bit 21,
// then the leave sequence
template <class P> void Game<P>::kind10_remove(uint32_t obj)
{
    if ((mem.at<Enemy>(obj).flags >> 21) & 1)
        mem.at<uint8_t>(G::route_choice) = 1;
    enemy_leave(obj);
}

// 0x77410 / PC 0x4D9A90 (enemy): (1) flags bit 21 with route_choice 1: the enemy leaves (task_exit). (2) footsteps. (3) action 1 with
// the head lost (parts_lost bit 1): types 32, 58, 63, 74, 85 stop acting (action 0) unless flags bit 31; types 30 / 84
// first count G::headless_countdown down (sound at 0). (4) hits, hit reaction, death, AI. (5) shadow (flags bit 23: at the camera
// eye y - 15), draw, motion timers; type 3 in motion 0x1F3 draws an extra model; attacker list; motion AI, depth list.
template <class P> void Game<P>::enemy_main(uint32_t obj)
{
    Enemy& e = mem.at<Enemy>(obj);
    if (((e.flags >> 21) & 1) != 0 && mem.at<uint8_t>(G::route_choice) == 1) {
        enemy_leave(obj);   // ends in task_exit (the PC code has no return after the call)
        if constexpr (!P::pc)
            return;
    }
    enemy_footsteps(obj);
    if (e.action == 1 && ((e.parts_lost >> 1) & 1) != 0) {
        uint32_t k = uint32_t(int32_t(int16_t(uint16_t(e.type) - 30)) & 0xFFFF);
        k = uint32_t(int32_t(int16_t(k)));
        bool stop = false;
        if (k <= 55) {
            switch (k) {
            case 0:    // type 30
            case 54: { // type 84
                if ((e.flags >> 19) & 1) {
                    e.flags &= ~(1u << 19);
                    int32_t n = int32_t(mem.u32(G::headless_countdown)) - 1;
                    mem.set32(G::headless_countdown, uint32_t(n));
                    if (n == 0)
                        sound_cue(k == 0 ? (P::pc ? 0x90 : 0xA01356) : (P::pc ? 0xFC : 0xA01466));
                }
                stop = !(0 > int32_t(e.flags));
                break;
            }
            case 2: case 28: case 33: case 44: case 55:   // types 32, 58, 63, 74, 85
                stop = !(0 > int32_t(e.flags));
                break;
            default:
                break;
            }
        }
        if (stop)
            e.action = 0;
    }
    mem.set32(G::hit_type, uint32_t(int32_t(e.type)));
    mem.at<uint8_t>(G::hit_class) = 0;
    mem.at<int16_t>(G::hit_damage) = 0;
    enemy_part_hits(obj);
    enemy_hit_reaction(obj);
    enemy_check_death(obj);
    if (mem.at<uint8_t>(obj + ob(0x58)) & 1)
        enemy_ai(obj);
    if ((e.flags >> 23) & 1) {
        float y = mem.f32(G::camera_records + 4) - 15.0f;
        e.pos[1] = y;
        shadow_setup(y);
    } else {
        shadow_setup(mem.f32(G::floor_y));
    }
    object_draw(obj);
    e.anim_time = e.anim_time + 1;
    if ((e.motion_flags2 >> 5) & 1)
        e.motion2_time = e.motion2_time + 1;
    if constexpr (P::pc) {
        // PC: unless pc_no_extra_model; the software matrix stack (signed frame test)
        if (mem.u32(G::pc_no_extra_model) == 0 && e.type == 3 && uint16_t(e.motion) == 0x1F3 && e.anim_time >= 10 &&
            e.anim_time < 100) {
            pc_matrix_push();
            const uint32_t m = mem.u32(G::pc_matrix_ptr);
            for (uint32_t i = 0; i < 12; ++i)
                mem.set32(m + 4 * i, mem.u32(obj + ob(0x154) + 4 * i));
            pc_matrix_rotate_y(0xFFFFC000u);
            pc_matrix_rotate_b(0x4000);
            pc_matrix_translate(0.0f, -0.5f, 0.5f);
            geo_draw_model(uint32_t(mem.at<Enemy>(obj).anim_time + 0x200));
            pcm_pop();
        }
    } else if (e.type == 3 && uint16_t(e.motion) == 0x1F3 && !(9u >= uint32_t(e.anim_time)) && !(uint32_t(e.anim_time) > 0x63u)) {
        // an extra model (anim_time + 0x200) with the object's matrix, turned and moved by half a unit
        uint32_t m[12];
        std::memcpy(m, mem.ptr(obj + ob(0x154)), sizeof m);
        Tgp::Out none;
        copro(0x7F, m, 12, none);
        uint32_t a[1] = {0xFFFFC000u};
        copro(0x15, a, 1, none);
        a[0] = 0x4000;
        copro(0x16, a, 1, none);
        uint32_t t[3] = {0, fbits(-0.5f), fbits(0.5f)};
        copro(0x12, t, 3, none);
        geo_draw_model(uint32_t(mem.at<Enemy>(obj).anim_time + 0x200));
        copro(0x06, nullptr, 0, none);
    }
    if (mem.at<Enemy>(obj).attack_slot > 1)
        attacker_list_add(obj);
    if (mem.at<uint8_t>(obj + ob(0x58)) & 1) {
        enemy_ai_motion(obj);
        depth_list_add(obj);
    }
}

// 0x78F50 / PC 0x4DB344 (enemy): unless motion_flags3 bit 1: footstep sound in motion 0x1E3 at frames 10 / 40, 0x20C at 20 / 40
template <class P> void Game<P>::enemy_footsteps(uint32_t obj)
{
    Enemy& e = mem.at<Enemy>(obj);
    if ((e.motion_flags3 >> 1) & 1)
        return;
    const int32_t m = e.motion;
    const uint16_t f = uint16_t(e.frame);
    if (m == 0x1E3) {
        if (f == 10 || f == 40)
            sound_cue(P::pc ? 0x32 : 0xA01225);
    } else if (m == 0x20C) {
        if (f == 20 || f == 40)
            sound_cue(P::pc ? 0x32 : 0xA01225);
    }
}

// 0x777F0 / PC 0x4D9D28 (enemy): a hit this frame (hit_class != 0), not dying (flags bit 31) and not in state 7 / 8: the hit motion
// enemy_hit_motions[type][action][hit_class % 9], blended (hold 20) for classes 0-4, set at once for 5-8; flags bit 28;
// hit voice
template <class P> void Game<P>::enemy_hit_reaction(uint32_t obj)
{
    const uint8_t hc = mem.at<uint8_t>(G::hit_class);
    Enemy& e = mem.at<Enemy>(obj);
    if (hc == 0 || 0 > int32_t(e.flags) || uint16_t(e.state) == 7 || uint16_t(e.state) == 8)
        return;
    const int32_t c = int32_t(int8_t(hc)) % 9;
    const uint32_t row = mem.u32(G::enemy_hit_motions + uint32_t(int32_t(e.type)) * 4);
    const uint32_t list = mem.u32(row + uint32_t(int32_t(e.action)) * 4);
    const uint32_t motion = mem.u32(list + uint32_t(int32_t(int8_t(c))) * 4);
    if (!(4 < int32_t(int8_t(c))))
        set_motion_blend_hold(obj, motion, 0, 1, 20);
    else
        set_motion(obj, motion);
    mem.at<Enemy>(obj).flags |= 1u << 28;
    enemy_voice(obj, 0);
}

// 0x27500: always 0 (a debug hook left in enemy_check_death: a non-zero result would kill the enemy)
template <class P> uint32_t Game<P>::return_false() { return 0; }

// 0x778C0 / PC 0x4D9DFC (enemy): life - hit_damage; dead when life <= 0, or with flags bit 8 and w308 set (or the debug hook):
// 80 points to the killer, not shootable, target released, second motion stopped, the death motion (death_style & 3:
// 1 = 0x27C, 2 = one of three, else the type's, or one of five), headless count for types 30 / 84, the dying routine,
// flags bit 20 while other enemies live, enemies_alive - 1 with draw_flags bit 3, attack slot freed, death voice
// (2 for hit class 1, else 1), route_choice cleared with flags bit 21.
template <class P> void Game<P>::enemy_check_death(uint32_t obj)
{
    Enemy& e = mem.at<Enemy>(obj);
    // (Light Gun Edition: Sudden Death: one hit is enough; only a frame with a hit: this runs every frame, damage 0
    // without one (user 2026-11-05: zombies died that nobody shot))
    const uint16_t dmg = mem.at<uint16_t>(G::hit_damage);
    const uint32_t nl = lg_one_hit && dmg != 0 ? 0u : uint32_t(uint16_t(e.life)) - uint32_t(dmg);
    e.life = int16_t(nl);
    if (!(0 >= int32_t(nl << 16))) {
        if (!((uint16_t(e.flags) >> 8) & 1) || e.one_hit_kill == 0) {
            if (return_false() == 0)
                return;
        }
    }
    if (uint8_t(e.killer) != 0xFF)
        add_score(e.killer, 0x50);
    if (e.killer >= 0 && e.killer < (coop4_on() ? 4 : 2))   // Light Gun Edition: the kill's count (not "either": 2 in 1-2P)
        ++lg_kills[e.killer & 3];
    e.flags &= ~1u;
    if (uint8_t(e.target) != 0xFF) {
        uint32_t t = G::player_targeted + uint32_t(int32_t(e.target));
        if (mem.at<uint8_t>(t) != 0)
            mem.at<uint8_t>(t) = 0;
        e.target = -1;
    }
    if ((e.motion_flags2 >> 5) & 1)
        body_second_motion(obj, 0);
    const uint32_t style = uint32_t(uint16_t(mem.at<Enemy>(obj).death_style)) & 3;
    if (style == 1) {
        set_motion(obj, 0x27C);
    } else {
        uint32_t motion;
        if (style == 2) {
            motion = mem.u32(G::enemy_death_motions3 + uint32_t(int32_t(rand()) % 3) * 4);
        } else {
            const int32_t type = mem.at<Enemy>(obj).type;
            if (mem.u32(G::enemy_death_motions + uint32_t(type) * 4) == 1)
                motion = mem.u32(G::enemy_death_motions5 + uint32_t(int32_t(rand()) % 5) * 4);
            else
                motion = mem.u32(G::enemy_death_motions + uint32_t(int32_t(mem.at<Enemy>(obj).type)) * 4);
        }
        set_motion(obj, motion);
    }
    Enemy& d = mem.at<Enemy>(obj);
    if ((d.flags >> 19) & 1) {
        if (d.type == 30 || d.type == 0x54) {
            d.flags &= ~(1u << 19);
            int32_t n = int32_t(mem.u32(G::headless_countdown)) - 1;
            mem.set32(G::headless_countdown, uint32_t(n));
            if (n == 0)
                sound_cue(d.type == 30 ? (P::pc ? 0x90 : 0xA01356) : (P::pc ? 0xFC : 0xA01466));
        }
    }
    d.timer = 0;
    d.routine = (uint32_t(uint16_t(d.death_style)) & 3) == 1 ? F::Zombie_Dead : F::enemy_dying;
    if (!(1 >= int32_t(mem.at<int16_t>(G::enemies_alive))))
        d.flags |= 1u << 20;
    if ((d.draw_flags >> 3) & 1)
        mem.at<int16_t>(G::enemies_alive) = int16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
    if (uint8_t(d.attack_slot) != 0xFF)
        enemy_attack_slot_free(obj);
    if ((int32_t(int8_t(mem.at<uint8_t>(G::hit_class))) % 9 & 0xFF) == 1)
        enemy_voice(obj, 2);
    else
        enemy_voice(obj, 1);
    if ((mem.at<Enemy>(obj).flags >> 21) & 1)
        mem.at<uint8_t>(G::route_choice) = 0;
}

// 0x6BF30 / PC 0x4C631C (creature, kind 0..4): its voice. Types 3, 4, 12, 16, 30-33, 45-47, 51, 53, 58, 63, 65, 71, 72, 74, 77, 84,
// 85, 89 take set 0, the others set 1. Kind 0 (hit): sound table 0xC6880[rand() & 4] (only entries 0 and 4: a quirk),
// then the set's hit sound; 1 (death): set 0 one sound, set 1 rand() % 2 of two; 2: 0xA0134B and the set's sound; 3:
// rand() % 2 of the set's two; 4: the set's sound; other kinds: sound 0.
template <class P> void Game<P>::enemy_voice(uint32_t obj, int kind)
{
    static const uint8_t kSet0[] = {3, 4, 12, 16, 30, 31, 32, 33, 45, 46, 47, 51, 53, 58, 63, 65, 71, 72, 74, 77, 84, 85, 89};
    const uint32_t t = uint32_t(int32_t(int16_t(uint16_t(mem.at<Enemy>(obj).type) - 3)));
    int set = 1;
    if (t <= 0x56)
        for (uint8_t v : kSet0)
            if (uint32_t(v - 3) == t)
                set = 0;
    auto mod2 = [](uint32_t r) { int32_t x = int32_t(r); return uint32_t(x % 2); };
    // the table's entry at an arcade offset (the PC table has no word at +0x1C: later entries 4 bytes earlier)
    auto at = [&](uint32_t off) { return G::enemy_voice_sounds + (P::pc && off >= 0x20 ? off - 4 : off); };
    uint32_t cue = 0;
    switch (uint32_t(int32_t(int8_t(kind)))) {
    case 0:
        sound_cue(mem.u32(at((rand() & 4) * 4)));
        cue = mem.u32(at(set ? 0x14 : 0x10));
        break;
    case 1:
        cue = set ? mem.u32(at(0x20) + mod2(rand()) * 4) : mem.u32(at(0x18));
        break;
    case 2:
        sound_cue(P::pc ? 0x87 : 0xA0134B);
        cue = mem.u32(at(set ? 0x2C : 0x28));
        break;
    case 3:
        cue = mem.u32(at(set ? 0x38 : 0x30) + mod2(rand()) * 4);
        break;
    case 4:
        cue = mem.u32(at(set ? 0x44 : 0x40));
        break;
    default:
        break;
    }
    sound_cue(cue);
}

// the motion of enemy_motion_lists[type][action][sub] (MotionStep: motion, speed)
template <class P, class E> static uint32_t listMotion(Mem& mem, const E& e, int32_t sub)
{
    uint32_t row = mem.u32(P::G::enemy_motion_lists + uint32_t(int32_t(e.type)) * 4);
    uint32_t list = mem.u32(row + uint32_t(int32_t(e.action)) * 4);
    return mem.u32(list + uint32_t(sub) * 8);
}

// the walking motion: walk variants > 1 have their own motions for sub-states 1 (0x757B8) and 0 (0x757D8)
template <class P, class E> static uint32_t walkMotion(Mem& mem, const E& e)
{
    const int32_t wv = e.walk_variant;
    const uint16_t sub = uint16_t(e.substate);
    if (!(1 >= wv) && sub == 1)
        return mem.u32(P::G::enemy_walk_motions_b + uint32_t(wv) * 4);
    if (!(1 >= wv) && sub == 0)
        return mem.u32(P::G::enemy_stand_motions_b + uint32_t(wv) * 4);
    return listMotion<P>(mem, e, int16_t(sub));
}

// 0x78B70 / PC 0x4DAE90 (enemy): (1) at the end of a motion (motion_flags bit 5) with flags bit 28, a chance per chapter and type
// (0x765F0) to start walking: states 1 / 3 / 5 take sub-state 2's motion (blend 5); every case sets walk_variant 1.
// (2) at the end of a loop (motion_flags bit 4), by state: 1 / 3 the walking motion, 2 / 4 the same unless already
// playing, 5 / 6 / 9 the list motion unless playing, 7 the attack's lunge motion unless playing (all only with flags
// bit 28, which they clear); 8: the attack is over: target released, state 9 sub-state 3, retreat motion (blend 10),
// target = the attack slot's position.
template <class P> void Game<P>::enemy_ai_motion(uint32_t obj)
{
    Enemy& e = mem.at<Enemy>(obj);
    if (((e.motion_flags >> 5) & 1) && ((e.flags >> 28) & 1)) {
        int32_t r = int32_t(rand()) % 100;
        uint8_t chance = mem.at<uint8_t>(G::enemy_walk_on_chance + uint32_t(int32_t(mem.at<int8_t>(G::chapter))) +
                                         uint32_t(int32_t(e.type)) * 4);
        if (!(r >= int32_t(chance))) {
            uint32_t k = uint32_t(int32_t(int16_t(uint16_t(e.state) - 1)));
            if (k <= 5 && (k & 1) == 0) {
                e.substate = 2;
                e.walk_variant = 1;
                set_motion_blend(obj, listMotion<P>(mem, e, e.substate), 0, 5);
                mem.at<Enemy>(obj).flags &= ~(1u << 28);
            } else {
                e.walk_variant = 1;
            }
        }
    }
    Enemy& f = mem.at<Enemy>(obj);
    if (((f.motion_flags >> 4) & 1) == 0)
        return;
    uint32_t k = uint32_t(int32_t(int16_t(uint16_t(f.state) - 1)));
    if (k > 8)
        return;
    auto blendUnlessPlaying = [&](uint32_t m) {
        if (int32_t(mem.at<Enemy>(obj).motion) != int32_t(m))
            set_motion_blend(obj, m, 0, 5);
        mem.at<Enemy>(obj).flags &= ~(1u << 28);
    };
    switch (k) {
    case 0: case 2:   // states 1, 3
        if ((f.flags >> 28) & 1) {
            set_motion_blend(obj, walkMotion<P>(mem, f), 0, 5);
            mem.at<Enemy>(obj).flags &= ~(1u << 28);
        }
        return;
    case 1: case 3:   // states 2, 4
        if ((f.flags >> 28) & 1)
            blendUnlessPlaying(walkMotion<P>(mem, f));
        return;
    case 4: case 5: case 8:   // states 5, 6, 9
        if ((f.flags >> 28) & 1)
            blendUnlessPlaying(listMotion<P>(mem, f, f.substate));
        return;
    case 6: {   // state 7
        if ((f.flags >> 28) & 1) {
            uint32_t row = mem.u32(G::enemy_attacks + uint32_t(int32_t(f.type)) * 4);
            uint32_t list = mem.u32(row + uint32_t(int32_t(f.action)) * 4);
            blendUnlessPlaying(mem.u32(list + uint32_t(int32_t(f.attack)) * 24 + 4));
        }
        return;
    }
    case 7: {   // state 8
        mem.at<uint8_t>(G::player_targeted + uint32_t(int32_t(f.target))) = 0;
        f.target = -1;
        f.state = 9;
        f.substate = 3;
        f.flags &= ~(1u << 31);
        set_motion_blend(obj, listMotion<P>(mem, f, 3), 0, 10);
        Enemy& h = mem.at<Enemy>(obj);
        uint32_t slot = G::attack_slots + uint32_t(int32_t(h.attack_slot)) * 20;
        float x = mem.f32(slot + 8);
        h.target_pos[1] = 0.0f;
        h.target_pos[0] = x;
        h.target_pos[2] = mem.f32(slot + 0x10);
        return;
    }
    default:
        return;
    }
}

// boundaries: not ported yet

} // namespace hotd::port
