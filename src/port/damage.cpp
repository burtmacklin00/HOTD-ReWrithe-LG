// Damage: this frame's hits on a creature's body parts (enemy_part_hits, part_hit) and the small helpers they use
// (rand, add_score, difficulty_get), A and PC (PA / PM). Read from the disassembly of both (docs/decompilation.md
// "part_hit exact level").
#include "port/game.h"
#include <cstdio>
#include <cstdlib>

namespace hotd::port {

// Gameplay "Attract Gore" (user 2026-10-31): the attract demo's hits change models / blow parts off and show their
// effects as the 1998 PC does (no game-mode test); off = Revision A (only in game mode 4). Play (mode 4) is unchanged.
bool gAttractGore = true;
// The Typing of the Dead 0's attack window (game.h)
bool (*gAttackHold)(uint32_t obj) = nullptr;
bool (*gHitHold)(int player) = nullptr;
bool (*gPreyHold)(uint32_t obj) = nullptr;
bool (*gBossHold)(uint32_t obj) = nullptr;


// 0x34A50 / PC 0x471828: x = x * 0x41C64E6D + 0x3039 (rand_state), returns bits 16-31
template <class P> uint32_t Game<P>::rand()
{
    uint32_t x = mem.u32(G::rand_state) * 0x41C64E6Du + 0x3039u;
    mem.set32(G::rand_state, x);
    // HOTD_TRACE_RAND (pccap, the differential run): every draw with the task that made it
    static const bool tr = std::getenv("HOTD_TRACE_RAND") != nullptr;
    if (tr) std::printf("rand -> %08X task 0x%X routine 0x%X\n", x, mem.u32(G::task_current), mem.u32(mem.u32(G::task_current)));
    return x >> 16;
}

// 0x69B0 / PC 0x437344 (player, points): score + points, floored at 0
template <class P> void Game<P>::add_score(int player, int32_t points)
{
    if (survivalScoring)   // (Survival Mode: its own points, survival_hits; game.h)
        return;
    Player& pl = mem.at<Player>(player_rec(player));   // 4P: seats 2 / 3 in the block
    int32_t s = pl.score + points;
    if (!(0 <= s))
        s = 0;
    pl.score = s;
}

// 0x264E0 / PC 0x458C1C: the difficulty setting (the signed short at difficulty + 2)
template <class P> int32_t Game<P>::difficulty_get() { return mem.at<int16_t>(G::difficulty + 2); }

// 0x684B0 / PC 0x4C4168 (creature): only while the camera is at a stop and the creature is shootable, or in chapter
// index 5. part_hit on every child of the root; then the killer: the player whose own damage kills while the other's
// does not, else (both or neither) A: rand() & 1; PC: player 0 if only it is in state 5, player 1 if only it is, else
// the previous killer (pc_last_killer, which every other choice sets); not dead: -1. Eye hits: 120 points to that
// player, with both players' hits to the killer, with no killer to a random player. Then body_free_cut_parts.
template <class P> void Game<P>::enemy_part_hits(uint32_t obj)
{
    Obj& o = mem.at<Obj>(obj);
    if (!((mem.u32(G::camera_stopped) == 2 && (o.flags & 1) != 0) ||
          mem.at<uint8_t>(G::chapter) == 5))
        return;
    mem.set32(G::body_current, obj);
    const uint32_t root = uint32_t(o.part_root);
    mem.at<uint8_t>(G::eye_hits) = 0xFF;
    survivalHitSeats = 0;
    mem.at<int16_t>(G::part_tally + 2) = 0;
    mem.at<int16_t>(G::part_tally) = 0;
    if (coop4_on()) {   // 4P: seats 2 / 3's tallies
        mem.at<int16_t>(part_tally_at(2)) = 0;
        mem.at<int16_t>(part_tally_at(3)) = 0;
        mem.at<uint8_t>(coop4::kEyeSeats) = 0;
    }
    if (weaponHooks)   // weapons (ours, game/weapons.h): the Magician's Hand's explosion near it
        weapon_blast_hit(obj);
    uint32_t list = mem.at<BodyPart>(root).children.a;
    for (uint32_t i = 0; i < uint32_t(mem.at<BodyPart>(root).child_count); ++i, list += 4)
        part_hit(mem.u32(list));
    if (coop4_on())   // 4P, 3 / 4 players: the creatures take more shots (docs/coop4.md "Enemy pressure")
        coop4_enemy_damage();
    // Light Gun Edition: the shotgun (Shotgun Mode, Arsenal's): each seat's damage this frame x lg_dmg_scale (its tally
    // and the total, so the killer is chosen on the real damage)
    for (int s = 0; s < (coop4_on() ? 4 : 2); ++s) {
        if (lg_dmg_scale[s] <= 1.0f)
            continue;
        int16_t& t = mem.at<int16_t>(part_tally_at(s));
        if (t <= 0)
            continue;
        const int extra = int(float(t) * (lg_dmg_scale[s] - 1.0f) + 0.5f);
        t = int16_t(std::min(0x7FFF, t + extra));
        mem.at<int16_t>(G::hit_damage) = int16_t(std::min(0x7FFF, int(mem.at<int16_t>(G::hit_damage)) + extra));
    }
    if (weaponHooks)   // weapons: each seat's weapon's damage
        weapon_damage();
    Obj& b = mem.at<Obj>(mem.u32(G::body_current));
    const int32_t life = b.param;
    // PC: every killer chosen here is also the last killer
    auto credit = [&](int8_t k) {
        b.killer = k;
        if constexpr (P::pc)
            mem.at<int32_t>(G::pc_last_killer) = b.killer;
        else if (pc_path())   // PC Mode on the arcade logic: the runtime's copy of pc_last_killer
            pm_last_killer = b.killer;
    };
    if (life - int32_t(mem.at<int16_t>(G::hit_damage)) > 0) {
        b.killer = -1;
    } else if (coop4_on()) {
        // 4P: the seat whose own damage kills while no other's does; else (several or none) a random one of the seats
        // that did damage (none: seat 0 / 1 as the arcade's rand() & 1)
        int own[4], n = 0, dealt[4], m = 0;
        for (int s = 0; s < 4; ++s) {
            const int32_t t = mem.at<int16_t>(part_tally_at(s));
            if (life - t <= 0)
                own[n++] = s;
            if (t > 0)
                dealt[m++] = s;
        }
        const uint32_t r = rand();
        b.killer = int8_t(n == 1 ? own[0] : m > 0 ? dealt[r % uint32_t(m)] : int(r & 1));
    } else if (life - int32_t(mem.at<int16_t>(G::part_tally)) <= 0 && life - int32_t(mem.at<int16_t>(G::part_tally + 2)) > 0) {
        credit(0);
    } else if (life - int32_t(mem.at<int16_t>(G::part_tally + 2)) <= 0 && life - int32_t(mem.at<int16_t>(G::part_tally)) > 0) {
        credit(1);
    } else if constexpr (P::pc) {
        const int32_t s0 = mem.at<Player>(G::players, 0).state, s1 = mem.at<Player>(G::players, 1).state;
        if (s0 == 5 && s0 != s1)
            credit(0);
        else if (s0 != 5 && s1 == 5)
            credit(1);
        else
            b.killer = mem.at<int8_t>(G::pc_last_killer);
    } else if (pc_path()) {   // PC Mode on the arcade logic: the PC's rule above, no random number (docs/pcmode.md)
        const int32_t s0 = mem.at<Player>(G::players, 0).state, s1 = mem.at<Player>(G::players, 1).state;
        if (s0 == 5 && s0 != s1)
            credit(0);
        else if (s0 != 5 && s1 == 5)
            credit(1);
        else
            b.killer = int8_t(pm_last_killer);
    } else {
        uint32_t r = rand();
        mem.at<Obj>(mem.u32(G::body_current)).killer = int8_t(r & 1);
    }
    const uint8_t eye = mem.at<uint8_t>(G::eye_hits);
    if (eye != 0xFF && coop4_on()) {
        // 4P: the eye's points to its shooter (kEyeSeats); several: the killer when one of them, else one at random
        const uint32_t seats = mem.at<uint8_t>(coop4::kEyeSeats);
        const int k = mem.at<Obj>(mem.u32(G::body_current)).killer;
        int in[4], n = 0;
        for (int s = 0; s < 4; ++s)
            if ((seats >> s) & 1)
                in[n++] = s;
        if (n == 1)
            add_score(in[0], 120);
        else if (n > 1)
            add_score(k >= 0 && k < 4 && ((seats >> k) & 1) ? k : in[uint32_t(rand()) % uint32_t(n)], 120);
    } else if (eye != 0xFF) {
        if (eye != 2) {
            add_score(int8_t(eye), 120);
        } else {
            Obj& k = mem.at<Obj>(mem.u32(G::body_current));
            if (uint8_t(k.killer) == 0xFF)
                add_score(int(rand() & 1), 120);
            else
                add_score(k.killer, 120);
        }
    }
    if (survivalScoring) {   // Survival Mode's points (game.h survival_hits): the seats that hit, the killer, the head hits
        const bool four = coop4_on();
        const uint32_t seats = survivalHitSeats;   // (part_hit's: the parts hit this frame; the object's bits 1 / 2 stay set)
        const uint32_t head = eye == 0xFF ? 0 : four ? mem.at<uint8_t>(coop4::kEyeSeats) : eye == 2 ? 3u : 1u << (eye & 1);
        if (seats != 0 || head != 0)
            survival_hits(obj, seats | head, life - int32_t(mem.at<int16_t>(G::hit_damage)) > 0 ? -1 : int(mem.at<Obj>(obj).killer), head);
    }
    if (weaponHooks && life > 0 && mem.at<int16_t>(G::hit_damage) > 0 && life - int32_t(mem.at<int16_t>(G::hit_damage)) <= 0) {
        if (blastObj == obj) {   // (tests: a creature the Hand's explosion hit in this frame)
            ++blastKills;
            static const bool trace = std::getenv("HOTD_BLAST_TRACE") != nullptr;
            if (trace)
                std::printf("   blast kill: obj %X\n", obj);
        }
        weapon_kill(obj, mem.at<Obj>(obj).killer);   // weapons: the drops (a killed creature may drop a pickup)
    }
    body_free_cut_parts(root);
}

// 0x68010 / PC 0x4C3D58 (part): per player whose hit bit (player + 1) is set on a part with part_id != 0: armour - 1
// (if > 0); at 0 the damage step advances (replacement model, hit points, severing, the type's damage callback, eye
// pop / blow-off; A: model changes only in game mode 4, the PC has no such test); the hit's damage (PM: times the
// player character's damage factor, truncated) goes to hit_damage and the player's tally. Then the hit effect (A: game
// mode 4 only), bleeding when the creature dies, and the children. The PC packs the part roles in 5 bytes.
template <class P> void Game<P>::part_hit(uint32_t pa)
{
    BodyPart& part = mem.at<BodyPart>(pa);
    const int32_t type = int32_t(mem.u32(G::hit_type));
    constexpr uint32_t kRole = sizeof(PartRole);
    const int8_t cls = mem.at<int8_t>(G::part_roles + uint32_t(type * 31) * kRole + uint32_t(part.role) * kRole + 3);
    uint8_t effect = 0;   // 1 sparks, 2 class-17 effect, else blood
    uint8_t severed = 0;
    if (part.part_id != 0 && (uint8_t(part.flags) & 6) != 0) {
        // 4P: the seats from the hit table (seats 2 / 3 set player 1's / 2's bit: shot_hit_nearest); a part not in it
        // keeps its bits' players. `pp` is the player the original's per-player tables see (seats 2 / 3 as 1 / 2).
        const bool four = coop4_on();
        uint32_t seats = 0;
        if (four) {
            seats = coop4_hit_seats(pa);
            if (seats == 0)
                seats = (part.flags >> 1) & 3;
        }
        for (uint32_t p = 0; (p & 0xFF) <= (four ? 3u : 1u); ++p) {
            if (four ? ((seats >> p) & 1) == 0 : ((part.flags >> (((p & 0xFF) + 1) & 0x1F)) & 1) == 0)
                continue;
            const uint32_t pp = four ? (p & 1) : p;
            survivalHitSeats |= 1u << (p & 3);   // (Survival Mode's points: the seats whose shots hit this frame)
            const int32_t ht = int32_t(mem.u32(G::hit_type));
            const uint8_t sever = mem.at<uint8_t>(G::part_roles + uint32_t(ht * 31) * kRole + uint32_t(part.role) * kRole + 4);
            if (cls == 1)
            {
                mem.at<uint8_t>(G::eye_hits) = uint8_t(mem.at<uint8_t>(G::eye_hits) + pp + 1);
                if (four)
                    mem.at<uint8_t>(coop4::kEyeSeats) = uint8_t(mem.at<uint8_t>(coop4::kEyeSeats) | (1u << p));
            }
            int32_t armour = part.armour;
            if (armour > 0)
                armour -= 1;
            part.armour = int8_t(armour);
            if ((armour & 0xFF) == 0) {
                int32_t h = int32_t(mem.u32(G::hit_type));
                int32_t step = int8_t(part.w78);
                uint32_t model = mem.u32(G::part_replacement + uint32_t(h << 9) + uint32_t(int32_t(cls) << 5) + uint32_t(step * 4));
                if (model != 0) {
                    mem.at<uint8_t>(G::hit_class) = mem.at<uint8_t>(G::part_class_hit + uint32_t(int32_t(cls)));
                    if (P::pc || mem.at<uint16_t>(G::game_mode) == 4 || gAttractGore) {
                        int8_t bit = mem.at<int8_t>(G::part_class_lost_bit + uint32_t(int32_t(cls)));
                        if (!(0 > bit)) {
                            Obj& b = mem.at<Obj>(mem.u32(G::body_current));
                            b.parts_lost = uint8_t(b.parts_lost | uint8_t(1u << bit));
                        }
                        bool keep = (sever == 1 || sever == 2) && (mem.at<Obj>(mem.u32(G::body_current)).motion_flags & 2) != 0;
                        if (!keep && model != 0x7B0)
                            part.part_id = int32_t(model);
                        const int32_t id = part.part_id;
                        part.w78 = uint8_t(part.w78 + 1);
                        if (id == 0) {
                            part.armour = -1;
                        } else {
                            Obj& b = mem.at<Obj>(mem.u32(G::body_current));
                            part.hit_radius = mem.f32(G::part_model_radius + uint32_t(id) * 16) * b.scale;
                            const int32_t ns = int8_t(part.w78);
                            const int32_t h2 = int32_t(mem.u32(G::hit_type));
                            part.armour = int8_t(mem.at<uint8_t>(G::part_hit_points + uint32_t(h2 << 8) + uint32_t(int32_t(cls) << 4) + uint32_t(ns * 2)));
                            uint32_t next = mem.u32(G::part_replacement + uint32_t(h2 << 9) + uint32_t(int32_t(cls) << 5) + uint32_t(ns * 4));
                            if (next == 0 && (sever == 1 || sever == 2) &&
                                (mem.at<Obj>(mem.u32(G::body_current)).motion_flags & 2) == 0) {
                                part.flags |= 0x80000000u;
                                if (id == 1)
                                    part.hit_radius = 0.0f;
                            }
                            if ((mem.at<Obj>(mem.u32(G::body_current)).motion_flags & 2) == 0)
                                part_damage_callback(mem.u32(G::part_damage_callbacks + uint32_t(mem.u32(G::hit_type)) * 4), pa);
                        }
                        if (cls == 1 && part.w78 == 2)
                            eyeball_pop_spawn(int(pp), part.view_pos[0], part.view_pos[1], part.view_pos[2]);
                        if (cls == 1 && part.w78 == mem.at<uint8_t>(G::part_blow_off_step + uint32_t(mem.u32(G::hit_type)))) {
                            part_blow_off(int(pp), pa);
                            severed = 1;
                        }
                    }
                } else {
                    if (sever == 1 || sever == 2) {
                        Obj& b = mem.at<Obj>(mem.u32(G::body_current));
                        if (b.w308 != 0)
                            severed = 1;
                        if ((b.motion_flags & 2) == 0)
                            part.flags |= 0x80000000u;
                        mem.at<uint8_t>(G::hit_class) = mem.at<uint8_t>(G::part_class_hit + uint32_t(int32_t(cls)));
                        int8_t bit = mem.at<int8_t>(G::part_class_lost_bit + uint32_t(int32_t(cls)));
                        if (!(0 > bit))
                            b.parts_lost = uint8_t(b.parts_lost | uint8_t(1u << bit));
                    }
                    part.armour = -1;
                }
            }
            // the hit's damage
            const int32_t h = int32_t(mem.u32(G::hit_type));
            int32_t dmg;
            if (cls == 16) {
                dmg = 0;
                effect = 1;
            } else if (uint32_t(int32_t(cls)) > 16u && cls == 17) {
                dmg = 0;
                effect = 2;
            } else if (cls == 0) {
                dmg = mem.at<int8_t>(G::part_roles + uint32_t(h * 31) * kRole + uint32_t(part.role) * kRole + 1);
            } else {
                dmg = mem.at<uint16_t>(G::part_damage_table + uint32_t(h << 8) + uint32_t(int32_t(cls) << 4) + uint32_t(int32_t(int8_t(part.w78)) * 2));
            }
            int32_t bonus = 0;
            if (mem.u32(G::part_difficulty_bonus + uint32_t(h) * 4) != 0) {
                int32_t d = difficulty_get();
                uint32_t table = mem.u32(G::part_difficulty_bonus + mem.u32(G::hit_type) * 4);
                bonus = mem.at<int8_t>(table + uint32_t(part.role) * 16 + uint32_t(d));
            }
            int32_t total = dmg + bonus;
            if constexpr (P::pc) {
                if (mem.at<int32_t>(G::frontend_state) == 5) {   // PM: the character's damage factor (+0x28)
                    const int32_t ch = mem.at<int32_t>(G::pc_mode_chars + (p & 0xFF) * 4);
                    const float f = mem.f32(G::pc_characters + 0x28 + uint32_t(ch) * 0x34);
                    total = int32_t(float(int16_t(total)) * f);
                }
            } else if (pm_on()) {   // ReWrithe's PC Mode on the arcade logic: the same factor (docs/pcmode.md)
                total = int32_t(float(int16_t(total)) * pm_table(pm_seat_char(int(p & 0xFF))).damage);
            }
            if (int32_t(uint32_t(total) << 16) > 0) {
                uint32_t slot = four ? part_tally_at(int(p)) : G::part_tally + (p & 0xFF) * 2;
                mem.at<uint16_t>(slot) = uint16_t(mem.at<uint16_t>(slot) + total);
                mem.at<uint16_t>(G::hit_damage) = uint16_t(mem.at<uint16_t>(G::hit_damage) + total);
            }
        }
        if (P::pc || mem.at<int16_t>(G::game_mode) == 4 || gAttractGore) {
            if (effect == 1) {
                part_hit_spark(pa);
                sound_cue(P::pc ? 0x37 : 0xA0122A);
            } else if (effect == 2) {
                part_hit_class17(pa);
                sound_cue(P::pc ? 0x20 : 0xA0120A);
            } else {
                blood_burst_spawn(pa);
            }
            Obj& b = mem.at<Obj>(mem.u32(G::body_current));
            if (!(0 < int32_t(b.param) - int32_t(mem.at<int16_t>(G::hit_damage)))) {
                if (severed)
                    part_bleed_start(pa);
                else
                    part_bleed_spawn(pa);
            }
        }
    }
    uint32_t list = mem.at<BodyPart>(pa).children.a;
    for (uint32_t i = 0; 0 < mem.at<BodyPart>(pa).child_count && int32_t(i & 0xFFFF) < mem.at<BodyPart>(pa).child_count;
         ++i, list += 4)
        part_hit(mem.u32(list));
}

// boundaries: not ported yet

} // namespace hotd::port
