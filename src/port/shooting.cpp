// Shooting: the shot test, A and PC (PA / PM) (docs/decompilation.md "Player, light gun and shooting"). Read from the
// disassembly of both where Ghidra's C loses the coprocessor FIFO traffic (ray_sign_test, shot_test_list_a / b) or
// the x87 order (the PC's ray_sign_test). The PC tests no scene quads.
#include "port/game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

namespace hotd::port {

// 0x33190 / PC 0x414AAC (player, x, y, z, radius): is the camera-space point within radius of the player's ray. c0,
// c1, s0, s1 = cam_trig; u = x*s1 - z*s0, v = ((x*c0)*s0 + y*c1) + (z*c0)*s1; A: distance by coprocessor function
// 0x1F from (u, 0, v) to the origin; PC: pc_fast_sqrt2(v*v + u*u) (its table square root), and in PC Mode the radius
// is first scaled by the player character's hit size (+0x30). 1 unless distance > radius (NaN counts as a hit), else -1.
template <class P> int Game<P>::ray_sign_test(int player, float x, float y, float z, float radius)
{
    ShotRay& r = mem.at<ShotRay>(shot_ray_rec(player));
    radius = radius * lg_hit_scale * lg_seat_scale[player & 3];   // Light Gun Edition: Shotgun Mode / the weapons (1 otherwise)
    const float c0 = r.cam_trig[0], c1 = r.cam_trig[1], s0 = r.cam_trig[2], s1 = r.cam_trig[3];
    if constexpr (P::pc) {
        if (mem.at<int32_t>(G::frontend_state) == 5)
            radius = radius * mem.f32(G::pc_characters + 0x30 + uint32_t(mem.at<int32_t>(G::pc_mode_chars + uint32_t(player) * 4)) * 0x34);
        const float a = (x * c0) * s0, b = x * s1, c = y * c1, d = z * s0, e = z * c0;
        const float u = b - d;
        const float v = (a + c) + e * s1;
        return pc_fast_sqrt2(v * v + u * u) > radius ? -1 : 1;
    }
    if (pc_path()) {   // ReWrithe's PC Mode / Boss Mode on the arcade logic: the PC's sum and table square root instead
                       // of coprocessor 0x1F (docs/pcmode.md rows 2 / 10); PC Mode: the character's hit size
        if (pm_on())
            radius = radius * pm_table(pm_seat_char(player)).hit_size;
        return pm::ray_sign(r, x, y, z, radius);
    }
    float v = (x * c0) * s0;
    float yc = y * c1;
    float u = x * s1 - z * s0;
    float zc = (z * c0) * s1;
    v = v + yc;
    v = v + zc;
    uint32_t in[6] = {fbits(u), 0, fbits(v), 0, 0, 0};
    Tgp::Out out;
    copro(0x1F, in, 6, out);
    float d = ffrom(out.w[0]);
    if (d > radius)
        return -1;
    return 1;
}

// 0x454E0 / PC 0x489C84: for each player whose Player +4 bit 1 is set (fired this frame): every depth-list object, the
// scene pairs (an empty loop) and the two scene-quad lists (A only); the nearest candidate gets the hit. Player 1
// before player 2.
template <class P> void Game<P>::shot_resolve()
{
    mem.set32(G::shot_last_count, 0);
    mem.set32(G::shot_candidate_count, 0);
    if (coop4_on())   // 4P (seats 3 / 4 after 1 / 2): the last frame's hit table has been used (part_hit ...)
        mem.set32(coop4::kHitTable, 0);
    if (weaponHooks)   // weapons (ours): the Magician's Hand's bolts in flight
        weapon_bolts_update();
    for (int p = 0; p < seat_count(); ++p) {
        if (p >= 1)
            mem.set32(G::shot_candidate_count, 0);
        Player& pl = mem.at<Player>(player_rec(p));
        if (((pl.flags >> 1) & 1) == 0)
            continue;
        for (uint32_t i = 0; i < mem.u32(G::depth_list_count); ++i)
            shot_test_object(i, p);
        if constexpr (!P::pc) {
            if (!pc_path()) {   // PC Mode on the arcade logic: the PC's shot test has no scene quads (docs/pcmode.md)
                shot_test_pairs(p);
                shot_test_list_a(p);
                shot_test_list_b(p);
            }
        }
        pl.flags &= ~2u;
        bool hitMain = mem.u32(G::shot_candidate_count) != 0;
        weaponAimSet[p & 3] = false;
        if (weaponHooks && weaponShot[p & 3].projectile) {
            // weapons, survfix: a projectile hits nothing itself: its target (the bolt's end) is what the aim ray meets
            // first: the shot's nearest hit or the stage (survfix3: hit or not)
            weapon_aim_target(p, hitMain);
            hitMain = false;
        }
        if (hitMain) {
            if (mem.u32(G::input_source) == 0) ++lg_hits[p & 3];   // Light Gun Edition: the Party Scoreboard's hits
            if (lg_splash[p & 3] && mem.u32(G::input_source) == 0) {
                // Light Gun Edition: the rocket: every object / body part in the blast is hit (as the nearest one is)
                const uint32_t bit = 1u << ((p & 1) + 1);
                const uint32_t n = mem.u32(G::shot_candidate_count);
                for (uint32_t i = 0; i < n; ++i) {
                    const ShotCandidate c = mem.at<ShotCandidate>(G::shot_candidates, i);
                    if ((c.flags >> 4) & 1)
                        continue;   // (a scene item: its own code, the nearest one's)
                    if (lg_roulette && ((c.flags >> 9) & 1) && !lg_part_ok(c.target.a))
                        continue;   // (Weak-Point Roulette: not the glowing part)
                    if (coop4_on()) {
                        coop4_hit_add(c.target.a, p);
                        if ((c.flags >> 9) & 1)
                            coop4_hit_add(mem.at<BodyPart>(c.target.a).owner.a, p);
                    }
                    if (((c.flags >> 9) & 1) == 0) {
                        Obj& o = mem.at<Obj>(c.target.a);
                        o.flags = bit | o.flags | 0x100;
                    } else {
                        BodyPart& part = mem.at<BodyPart>(c.target.a);
                        part.flags = bit | part.flags | 0x100;
                        Obj& o = mem.at<Obj>(part.owner.a);
                        o.flags = bit | o.flags | 0x100;
                    }
                }
            }
            shot_hit_nearest(p);
            if (lg_roulette && mem.u32(G::input_source) == 0) {
                // Light Gun Edition: Weak-Point Roulette: a zombie's part that is not its glowing one takes no hit
                const ShotCandidate c = mem.at<ShotCandidate>(G::shot_candidates, mem.u32(G::shot_nearest));
                if (((c.flags >> 4) & 1) == 0 && ((c.flags >> 9) & 1) && !lg_part_ok(c.target.a)) {
                    const uint32_t bit = 1u << ((p & 1) + 1);
                    BodyPart& part = mem.at<BodyPart>(c.target.a);
                    part.flags &= ~(bit | 0x100u);
                    Obj& o = mem.at<Obj>(part.owner.a);
                    o.flags &= ~(bit | 0x100u);
                }
            }
            if (lg_exploding && mem.u32(G::input_source) == 0) {
                // Light Gun Edition: Exploding Zombies: a glowing zombie hit anywhere but the head blows up
                const ShotCandidate c = mem.at<ShotCandidate>(G::shot_candidates, mem.u32(G::shot_nearest));
                if (((c.flags >> 4) & 1) == 0 && ((c.flags >> 9) & 1) && (!lg_roulette || lg_part_ok(c.target.a)))
                    lg_explode(p, c.target.a);
            }
            mem.set32(G::shot_last_count, mem.u32(G::shot_candidate_count));
        }
        if (weaponHooks || secondGun[p & 3].fire)   // weapons (ours, game/weapons.h): the seat's extra rays and its
            weapon_extra_shots(p, hitMain);         // explosion; vr dual pistols: the left gun's own ray
    }
    mem.set32(G::depth_list_count, 0);
    mem.set32(G::shot_pair_count, 0);
}

// weapons (ours, user 2026-11-04; game/weapons.h): after a seat's own shot (shot_resolve, weaponHooks only). The extra
// rays (the shotgun's pellets, the double pistols' second gun): each its own ray from the aim + offset (shot_ray_setup),
// every depth-list object tested again (no scene quads: the props' items stay the main shot's), the nearest within the
// weapon's range hit as shot_hit_nearest hits (the same flags, so part_hit gives the damage and the blood); the double
// pistols' second gun also its own shot flash (shot_effect at that aim). Then the ray is put back as the main shot's
// (the HUD's last shot, the debris). The explosion (the Magician's Hand): at the main shot's hit point (a miss: 300
// units along the ray), its flashes (shot_flash: the game's hit flash, larger) and the creatures near it hit in their
// own enemy_part_hits (weapon_blast_hit). The arcade logic only (the PC form has none of it)
template <class P> void Game<P>::weapon_extra_shots(int p, bool hitMain)
{
    if constexpr (P::pc) {
        (void)p, (void)hitMain;
    } else {
        const WeaponShot& w = weaponShot[p & 3];
        SecondGun& g2 = secondGun[p & 3];
        const bool second = g2.fire;
        g2.fire = false;
        if (w.rays <= 0 && w.area <= 0.0f && !second)
            return;
        Player& pl = mem.at<Player>(player_rec(p));
        const float ax = float(pl.shot_pos[0]), ay = float(pl.shot_pos[1]);
        const ShotResult& res = mem.at<ShotResult>(shot_result_rec(p));
        float blast[3] = {res.hit[0], res.hit[1], res.hit[2]};
        if (!hitMain || res.code != 0) {   // (a miss: 300 units along the ray, camera space: x / y up as the aim, z ahead)
            const float n = std::sqrt(ax * ax + ay * ay + 496.0f * 496.0f);
            blast[0] = ax / n * 300.0f, blast[1] = ay / n * 300.0f, blast[2] = 496.0f / n * 300.0f;
        }
        auto ray = [&](float x, float y, bool flash, float range) {   // one extra ray at (x, y); true: it hit
            shot_ray_setup(p, x, y, 496.0f);
            if (flash) {   // (shot_effect reads the aim: the second gun's)
                const int16_t keep[2] = {pl.aim[0], pl.aim[1]};
                pl.aim[0] = int16_t(x), pl.aim[1] = int16_t(y);
                shot_effect(uint32_t(pl.task.a));
                pl.aim[0] = keep[0], pl.aim[1] = keep[1];
            }
            mem.set32(G::shot_candidate_count, 0);
            for (uint32_t k = 0; k < mem.u32(G::depth_list_count); ++k)
                shot_test_object(k, p);
            if (mem.u32(G::shot_candidate_count) == 0)
                return false;
            shot_targets_sort();
            const ShotCandidate c = mem.at<ShotCandidate>(G::shot_candidates, mem.u32(G::shot_nearest));
            if (range > 0.0f && c.hit[2] > range)
                return false;
            shot_hit_nearest(p);
            return true;
        };
        for (int i = 0; i < w.rays && i < 16; ++i)
            ray(ax + w.off[i][0], ay + w.off[i][1], w.flashes, w.range);
        if (second) {   // vr dual pistols: the left gun, pulled with the right in this frame: its own aim and flash
            ++g2.shots;
            if (ray(g2.x, g2.y, true, 0.0f))
                ++g2.hits;
        }
        if (w.rays > 0 || second)
            shot_ray_setup(p, ax, ay, 496.0f);
        if (w.area > 0.0f && w.projectile) {   // survfix: a bolt from just ahead of the camera along the aim
            if (weaponAimSet[p & 3])
                std::copy(weaponAim[p & 3], weaponAim[p & 3] + 3, blast);
            const float n = std::sqrt(ax * ax + ay * ay + 496.0f * 496.0f);
            const float k = 30.0f / n;
            uint32_t in0[3] = {fbits(ax * k), fbits(ay * k - 3.0f), fbits(496.0f * k)}, in1[3] = {fbits(blast[0]), fbits(blast[1]), fbits(blast[2])};
            uint32_t w0[3], w1[3];
            fx_to_world(in0, w0);
            fx_to_world(in1, w1);
            WeaponBolt* slot = nullptr;
            for (WeaponBolt& bt : weaponBolts)
                if (bt.seat < 0) {
                    slot = &bt;
                    break;
                }
            if (slot) {
                *slot = WeaponBolt{};
                slot->seat = p;
                for (int i = 0; i < 3; ++i)
                    slot->from[i] = ffrom(w0[i]), slot->to[i] = ffrom(w1[i]);
                const float dx = slot->to[0] - slot->from[0], dy = slot->to[1] - slot->from[1], dz = slot->to[2] - slot->from[2];
                slot->frames = std::clamp(int(std::sqrt(dx * dx + dy * dy + dz * dz) / kWeaponBoltSpeed + 0.5f), 2, kWeaponBoltMaxFrames);
                slot->radius = w.area;
                slot->pct = w.damage;   // (survfix3: the shot's own, whatever the seat holds when it lands)
                slot->what = weaponAimSet[p & 3] ? weaponAimWhat[p & 3] : 0;
                if (weaponAimSet[p & 3] && weaponAimItem[p & 3]) {   // (a prop / crate / scene item: hit on arrival)
                    slot->item = true;
                    std::copy(weaponAimCand[p & 3], weaponAimCand[p & 3] + 9, slot->cand);
                    ShotCandidate c;
                    static_assert(sizeof c == sizeof slot->cand);
                    std::memcpy(&c, slot->cand, sizeof c);
                    if (((c.flags >> 4) & 1) == 0)
                        slot->serial = mem.at<Obj>(c.target.a).serial;
                }
                if (std::getenv("HOTD_WEAPONS_TRACE") || std::getenv("HOTD_BLAST_TRACE"))
                    std::printf("bolt frame %llu: seat %d, %d frames to (%.0f %.0f %.0f) camera, %s\n", (unsigned long long)weaponFrame,
                                p, slot->frames, blast[0], blast[1], blast[2],
                                slot->what == 2 ? "the stage" : slot->item ? "an item" : slot->what == 1 ? "a creature" : "nothing (its range)");
            }
        } else if (w.area > 0.0f) {
            weapon_blast_at(p, blast, w.area);
            sound_cue(0xA0120A);   // (the hit flash's sound)
        }
    }
}

// weapons: an explosion at a camera-space point: the creatures within the radius take a hit in their own
// enemy_part_hits (weapon_blast_hit, this frame or the next); its look: the game's hit flash (shot_flash 0x30E50,
// models 0x74..0x88, as shot_flash_spawn sets it up) at the point, 4.5 times its size, and four around it 3 times
template <class P> void Game<P>::weapon_blast_at(int seat, const float at[3], float radius, int pct)
{
    if constexpr (P::pc) {
        (void)seat, (void)at, (void)radius, (void)pct;
    } else {
        weaponBlast = WeaponBlast{};
        ++blastsN;
        std::copy(at, at + 3, weaponBlast.at);
        weaponBlast.radius = radius;
        weaponBlast.seat = seat;
        weaponBlast.pct = pct;
        weaponBlast.frame = weaponFrame;
        const float ring[5][2] = {{0, 0}, {1, 0.4f}, {-1, 0.3f}, {0.3f, 1}, {-0.4f, -0.8f}};
        for (int k = 0; k < 5; ++k) {
            const float s = radius * 0.45f;
            const uint32_t t = task_create(F::shot_flash, 0x3C8);
            uint32_t in[3] = {fbits(at[0] + ring[k][0] * s), fbits(at[1] + ring[k][1] * s), fbits(at[2] - 2.0f)}, wp[3];
            fx_to_world(in, wp);
            mem.set32(t + 0x20, wp[0]);
            mem.set32(t + 0x24, wp[1]);
            mem.set32(t + 0x28, wp[2]);
            mem.set32(t + 0x2C, 0);
            mem.set32(t + 0x30, uint32_t(k) * 0x3000u);
            mem.set32(t + 0x54, 0x74u + uint32_t(k) * 2);   // (each a few frames along: not in step)
            mem.set32(t + 0x31C, 0x88);
            mem.set32(t + 0x33C, fbits(k == 0 ? 4.5f : 3.0f));
        }
    }
}

// survfix: the bolts in flight (shot_resolve, before the shots: each frame): a frame along their line; on arrival the
// explosion at its end (the world point back to camera space through the effects' matrix: fx_to_world of the origin
// and the axes, the camera's rotation is orthonormal), the Magician's fireball's own burst cue 0xA01464
// (magician_fireball_frame), the burst's look for 14 frames (drawn by the weapons' overlay: Weapons::overlay)
template <class P> void Game<P>::weapon_bolts_update()
{
    if constexpr (P::pc) {
        return;
    } else {
        for (WeaponBolt& bt : weaponBolts) {
            if (bt.seat < 0)
                continue;
            if (bt.burst > 0) {
                if (--bt.burst == 0)
                    bt.seat = -1;
                continue;
            }
            if (++bt.age < bt.frames)
                continue;
            uint32_t o[3], e[3][3];
            const uint32_t zero[3] = {0, 0, 0}, ax[3][3] = {{fbits(1.0f), 0, 0}, {0, fbits(1.0f), 0}, {0, 0, fbits(1.0f)}};
            fx_to_world(zero, o);
            for (int i = 0; i < 3; ++i)
                fx_to_world(ax[i], e[i]);
            float cam[3];
            for (int i = 0; i < 3; ++i) {
                float d = 0;
                for (int k = 0; k < 3; ++k)
                    d += (bt.to[k] - ffrom(o[k])) * (ffrom(e[i][k]) - ffrom(o[k]));
                cam[i] = d;
            }
            weapon_blast_at(bt.seat, cam, bt.radius, bt.pct);
            blastsWorld += bt.what == 2 ? 1 : 0;
            // testing: HOTD_BLAST_TRACE=1: each explosion and the shootable objects near it (camera space)
            static const bool trace = std::getenv("HOTD_BLAST_TRACE") != nullptr;
            if (trace) {
                std::printf("blast frame %llu at %.0f %.0f %.0f on %s (camera_stopped %u, %u shootable)\n", (unsigned long long)weaponFrame, cam[0], cam[1],
                            cam[2], bt.what == 2 ? "the stage" : bt.item ? "an item" : bt.what == 1 ? "a creature" : "nothing", mem.u32(G::camera_stopped),
                            mem.u32(G::depth_list_count));
                for (uint32_t i = 0; i < mem.u32(G::depth_list_count); ++i) {
                    const Obj& ob = mem.at<Obj>(mem.at<DepthEntry>(G::depth_list, i).obj.a);
                    const float dx = ob.view_pos[0] - cam[0], dy = ob.view_pos[1] - cam[1], dz = ob.view_pos[2] - cam[2];
                    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (d < bt.radius * 2.0f)
                        std::printf("   near: obj %X (%s) d %.0f\n", mem.at<DepthEntry>(G::depth_list, i).obj.a, ob.part_root ? "creature" : "item", d);
                }
            }
            // survfix3 (user 2026-11-05: "also hits breakable items / crates as a shot there would"): the item the bolt
            // flew to, if it is still there, takes the seat's shot as shot_hit_nearest gives it (its flags: it breaks,
            // drops, pays as shot); then every other item within the radius
            uint32_t itemObj = 0;
            if (bt.item) {
                ShotCandidate c;
                std::memcpy(&c, bt.cand, sizeof c);
                bool there = true;
                if (((c.flags >> 4) & 1) == 0) {
                    const Obj& ob = mem.at<Obj>(c.target.a);
                    there = ob.serial == bt.serial && (ob.flags & 1) != 0;
                    if (there) {
                        std::copy(ob.view_pos, ob.view_pos + 3, c.hit);
                        itemObj = c.target.a;
                    }
                }
                if (there) {
                    const uint32_t n0 = mem.u32(G::shot_candidate_count);
                    mem.at<ShotCandidate>(G::shot_candidates, n0) = c;
                    mem.set32(G::shot_sort + 8 * n0, uint32_t(cvtzril(c.hit[2] * 10.0f)));
                    mem.set32(G::shot_sort + 8 * n0 + 4, n0);
                    mem.set32(G::shot_candidate_count, n0 + 1);
                    shot_hit_nearest(bt.seat);
                    mem.set32(G::shot_candidate_count, n0);
                    ++blastsItems;
                    if (trace)
                        std::printf("   item hit: %s %X\n", ((c.flags >> 4) & 1) ? "scene item code" : "obj", ((c.flags >> 4) & 1) ? uint32_t(c.code) : itemObj);
                }
            }
            weapon_blast_items(bt.seat, cam, bt.radius, itemObj);
            sound_cue(0xA01464);
            bt.burst = 14;
        }
    }
}

// survfix3 (user 2026-11-05: the Hand "does not do splash damage or any damage ... I expect to shoot the enemy
// or the ground or wall near it to do damage"): a projectile seat's target, what its aim ray meets first. The shot test
// knows only the shootable objects (creatures, props) and the scene items: a shot at the ground or a wall found none
// and the fireball flew on to 300 units away and burst there, out of everyone's reach (most shots that missed a
// creature's spheres). Now the stage too (weapon_world_ray: the view's own triangles, the ragdolls' walls and ground):
// the nearer of the shot's nearest hit and the stage; neither within kWeaponBoltRange: there. A candidate that is not a
// creature (a prop, a crate, a scene item) is kept to be hit on arrival (weapon_bolts_update)
template <class P> void Game<P>::weapon_aim_target(int p, bool hitMain)
{
    if constexpr (P::pc) {
        (void)p, (void)hitMain;
    } else {
        const Player& pl = mem.at<Player>(player_rec(p));
        const float ax = float(pl.shot_pos[0]), ay = float(pl.shot_pos[1]);
        const float n = std::sqrt(ax * ax + ay * ay + 496.0f * 496.0f);
        const float dir[3] = {ax / n, ay / n, 496.0f / n};   // (camera space: x right, y up, z ahead)
        float tc = 1e30f;
        ShotCandidate c{};
        if (hitMain) {
            shot_targets_sort();
            c = mem.at<ShotCandidate>(G::shot_candidates, mem.u32(G::shot_nearest));
            tc = std::sqrt(c.hit[0] * c.hit[0] + c.hit[1] * c.hit[1] + c.hit[2] * c.hit[2]);
        }
        // the ray in the world: the camera's place and the aim's direction through the effects' matrix
        const uint32_t zero[3] = {0, 0, 0}, ahead[3] = {fbits(dir[0] * 100.0f), fbits(dir[1] * 100.0f), fbits(dir[2] * 100.0f)};
        uint32_t wo[3], wa[3];
        fx_to_world(zero, wo);
        fx_to_world(ahead, wa);
        float o[3], d[3];
        for (int i = 0; i < 3; ++i)
            o[i] = ffrom(wo[i]), d[i] = ffrom(wa[i]) - o[i];
        const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);   // (world units a 100 camera units)
        float tw = 0.0f;   // (world units; the camera's: x 100 / dl)
        bool world = false;
        if (dl > 1e-3f) {
            for (int i = 0; i < 3; ++i)
                d[i] /= dl;
            world = weapon_world_ray(o, d, std::min(tc, kWeaponBoltRange) * dl / 100.0f, tw) && tw * 100.0f / dl < tc;
        }
        float* aim = weaponAim[p & 3];
        weaponAimItem[p & 3] = false;
        if (world) {   // (a unit short of the surface: the burst in front of the wall / on the ground, not in it)
            const float t = std::max(1.0f, tw * 100.0f / dl - 1.0f);
            for (int i = 0; i < 3; ++i)
                aim[i] = dir[i] * t;
            weaponAimWhat[p & 3] = 2;
        } else if (tc < 1e30f) {
            std::copy(c.hit, c.hit + 3, aim);
            weaponAimWhat[p & 3] = 1;
            const bool scene = ((c.flags >> 4) & 1) != 0, part = ((c.flags >> 9) & 1) != 0;
            if (scene || (!part && mem.at<Obj>(c.target.a).part_root == 0)) {
                weaponAimItem[p & 3] = true;
                std::memcpy(weaponAimCand[p & 3], &c, sizeof c);
            }
        } else {
            for (int i = 0; i < 3; ++i)
                aim[i] = dir[i] * kWeaponBoltRange;
            weaponAimWhat[p & 3] = 0;
        }
        weaponAimSet[p & 3] = true;
    }
}

// survfix3: the explosion's items: every shootable object without a body (a prop, a crate, a thrown weapon ...) within
// the radius (its sphere) takes the seat's hit as a shot's (shot_hit_nearest: its flags), but `skip` (hit already)
template <class P> void Game<P>::weapon_blast_items(int seat, const float at[3], float radius, uint32_t skip)
{
    if constexpr (P::pc) {
        (void)seat, (void)at, (void)radius, (void)skip;
    } else {
        const uint32_t bit = 1u << ((seat & 1) + 1);
        for (uint32_t i = 0; i < mem.u32(G::depth_list_count); ++i) {
            const uint32_t oa = mem.at<DepthEntry>(G::depth_list, i).obj.a;
            Obj& o = mem.at<Obj>(oa);
            if (oa == skip || o.part_root != 0 || (o.flags & 1) == 0)
                continue;
            const float dx = o.view_pos[0] - at[0], dy = o.view_pos[1] - at[1], dz = o.view_pos[2] - at[2];
            if (std::sqrt(dx * dx + dy * dy + dz * dz) - std::max(0.0f, o.hit_radius) > radius)
                continue;
            o.flags = bit | o.flags | 0x100;
            if (coop4_on())
                coop4_hit_add(oa, seat);
            ++blastsItems;
        }
    }
}

// weapons: the explosion's hit on a creature (enemy_part_hits, before its parts' hits): within the radius of the last
// explosion (this frame or the one before: the creatures run before or after the shot test), once a creature: its part
// nearest the point gets the seat's hit bit, as a shot's (shot_hit_nearest)
template <class P> void Game<P>::weapon_blast_hit(uint32_t obj)
{
    WeaponBlast& b = weaponBlast;
    blastObj = 0;
    if (b.seat < 0 || b.radius <= 0.0f || weaponFrame > b.frame + 1 || b.doneN >= 32)
        return;
    for (int i = 0; i < b.doneN; ++i)
        if (b.done[i] == obj)
            return;
    Obj& o = mem.at<Obj>(obj);
    if (o.part_root == 0)
        return;
    uint32_t best = 0;
    float bestD = b.radius;
    const std::function<void(uint32_t, int)> walk = [&](uint32_t pa, int depth) {
        if (pa == 0 || depth > 12 || !mem.mapped(pa))
            return;
        const BodyPart& part = mem.at<BodyPart>(pa);
        // survfix2: not a part that takes no damage (part_hit's classes 16 / 17: the sparks of a held weapon, an
        // armour): the creature's axe in front of it, nearest the burst, took every hit and the creature none (a
        // Hand-only player could never end the round: found by the balance runs' time to kill)
        const int8_t cls = mem.at<int8_t>(G::part_roles + uint32_t(int32_t(mem.u32(G::hit_type)) * 31) * sizeof(PartRole) +
                                          uint32_t(part.role) * sizeof(PartRole) + 3);
        // .. and not the root (depth 0: enemy_part_hits runs part_hit on the root's children only, so a burst marking
        // the root did nothing: a creature right in front of the camera, its root nearest, never died)
        if (depth > 0 && part.part_id != 0 && part.hit_radius > 0.0f && cls != 16 && cls != 17) {
            const float dx = part.view_pos[0] - b.at[0], dy = part.view_pos[1] - b.at[1], dz = part.view_pos[2] - b.at[2];
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz) - part.hit_radius;
            if (d < bestD)
                bestD = d, best = pa;
        }
        for (int32_t k = 0; k < part.child_count && k < 32; ++k)
            walk(mem.u32(uint32_t(part.children.a) + 4 * uint32_t(k)), depth + 1);
    };
    walk(uint32_t(o.part_root), 0);
    if (best == 0)
        return;
    // survfix3: the damage falls off with the distance from the burst to the part's sphere: the full percent within
    // 30 % of the radius (a hit on the creature, the ground at its feet), down to 40 % at the radius
    const float r0 = b.radius * 0.3f;
    const float f = bestD <= r0 ? 1.0f : 1.0f - 0.6f * std::min(1.0f, (bestD - r0) / (b.radius - r0));
    blastObj = obj;
    blastPct = std::max(1, int(float(b.pct) * f + 0.5f));
    if (std::getenv("HOTD_BLAST_TRACE"))
        std::printf("   blast hit: obj %X, %.0f units from its part, %d %% (life %d)\n", obj, bestD, blastPct, int(o.param));
    b.done[b.doneN++] = obj;
    ++blastHits;
    blastsHit += b.doneN == 1 ? 1 : 0;
    blastsMulti += b.doneN == 2 ? 1 : 0;
    const uint32_t bit = 1u << ((b.seat & 1) + 1);
    BodyPart& part = mem.at<BodyPart>(best);
    part.flags = bit | part.flags | 0x100;
    o.flags = bit | o.flags | 0x100;
    if (coop4_on()) {
        coop4_hit_add(best, b.seat);
        coop4_hit_add(obj, b.seat);
    }
}

// weapons: each seat's damage in percent of the game's own (part_hit's tallies and the frame's hit_damage), after
// coop4_enemy_damage
template <class P> void Game<P>::weapon_damage()
{
    const int32_t total = mem.at<int16_t>(G::hit_damage);
    int32_t add = 0;
    for (int s = 0; s < (coop4_on() ? 4 : 2); ++s) {
        const uint32_t a = coop4_on() ? part_tally_at(s) : G::part_tally + uint32_t(s) * 2;
        const int32_t v = mem.at<int16_t>(a);
        // (survfix3: the explosion's hit its own percent: the shot's when fired, with the fall-off; blastObj is this body)
        const int pct = blastObj != 0 && blastObj == mem.u32(G::body_current) && s == weaponBlast.seat ? blastPct : weaponShot[s].damage;
        if (v <= 0 || pct == 100)
            continue;
        const int32_t nv = std::clamp((v * pct + 50) / 100, 1, 30000);
        mem.at<int16_t>(a) = int16_t(nv);
        add += nv - v;
    }
    if (add != 0)
        mem.at<int16_t>(G::hit_damage) = int16_t(std::clamp(total + add, 0, 30000));
}

// 0x45620 / PC 0x489D0C (depth-list index, player): the object's sphere; inside it, an object with a part tree that has
// children and flags2 bit 0 has its parts tested (shot_test_body; the PC inlines it), else the object itself is a
// candidate.
template <class P> void Game<P>::shot_test_object(uint32_t index, int player)
{
    uint32_t oa = mem.at<DepthEntry>(G::depth_list, index).obj.a;
    Obj& o = mem.at<Obj>(oa);
    if (0 >= ray_sign_test(player, o.view_pos[0], o.view_pos[1], o.view_pos[2], o.hit_radius))
        return;
    bool body = false;
    if (o.part_root != 0 && 0 < mem.at<BodyPart>(uint32_t(o.part_root)).child_count && (o.flags2 & 1) != 0) {
        body = true;
        shot_test_body(oa, player);
    }
    if (body)
        return;
    uint32_t n = mem.u32(G::shot_candidate_count);
    ShotCandidate& c = mem.at<ShotCandidate>(G::shot_candidates, n);
    int32_t key = cvtzril(o.view_pos[2] * 10.0f);
    c.hit[0] = o.view_pos[0];
    mem.set32(G::shot_sort + 8 * n + 4, n);
    c.hit[1] = o.view_pos[1];
    c.hit[3] = 0.0f;
    c.w10[0] = 0.0f;
    c.w10[1] = 0.0f;
    c.target.a = oa;
    c.hit[2] = o.view_pos[2];
    mem.set32(G::shot_sort + 8 * n, uint32_t(key));
    c.code = 0;
    mem.set32(G::shot_candidate_count, n + 1);
    c.flags = o.flags;
}

// 0x45700: an empty loop over the pair count (the body was removed in this build)
template <class P> void Game<P>::shot_test_pairs(int player)
{
    mem.set32(G::shot_pair_player, uint32_t(player));
}

// 0x45730 / PC 0x489E2C (part, player): a part with part_id != 0 and a hit radius != 0 whose sphere the ray crosses is a candidate;
// then all its children, hit or not.
template <class P> void Game<P>::shot_test_part(uint32_t pa, int player)
{
    BodyPart& part = mem.at<BodyPart>(pa);
    if (part.part_id != 0 && part.hit_radius != 0.0f &&
        0 < ray_sign_test(player, part.view_pos[0], part.view_pos[1], part.view_pos[2], part.hit_radius)) {
        uint32_t n = mem.u32(G::shot_candidate_count);
        ShotCandidate& c = mem.at<ShotCandidate>(G::shot_candidates, n);
        int32_t key = cvtzril(part.view_pos[2] * 10.0f);
        c.hit[0] = part.view_pos[0];
        mem.set32(G::shot_sort + 8 * n + 4, n);
        c.hit[1] = part.view_pos[1];
        c.hit[3] = 0.0f;
        c.w10[0] = 0.0f;
        c.w10[1] = 0.0f;
        c.target.a = pa;
        c.hit[2] = part.view_pos[2];
        mem.set32(G::shot_sort + 8 * n, uint32_t(key));
        c.code = 0;
        mem.set32(G::shot_candidate_count, n + 1);
        c.flags = part.flags;
    }
    for (uint32_t i = 0; i < uint32_t(mem.at<BodyPart>(pa).child_count); ++i)
        shot_test_part(mem.u32(mem.at<BodyPart>(pa).children.a + 4 * i), player);
}

// 0x45810 (object, player): shot_test_part on every child of the part tree's root
template <class P> void Game<P>::shot_test_body(uint32_t oa, int player)
{
    uint32_t root = uint32_t(mem.at<Obj>(oa).part_root);
    for (uint32_t i = 0; i < uint32_t(mem.at<BodyPart>(root).child_count); ++i)
        shot_test_part(mem.u32(mem.at<BodyPart>(root).children.a + 4 * i), player);
}

// 0x45850 / 0x45B60 (player): every list number of shot_list_a / _b (count: scene_quads record +0x60 / +0x61) is a
// coprocessor ray test (0x62) of the player's world ray; on a hit (first word > 0) the hit point is taken to camera
// space (0x74 stored matrix 0 with a push, 0x1A transform, 0x06 pop) and becomes a candidate: hit = camera-space point,
// hit[3] / w10 = the quad's plane normal, no target, flags |= 0x10 (scene item; the slot's other bits are left as they
// were), code = the quad's attribute, sort key (int)(z x 10).
template <class P> void Game<P>::shot_test_list(int player, uint32_t list, int countOffset)
{
    ShotRay& r = mem.at<ShotRay>(shot_ray_rec(player));
    const float o0 = r.origin[0], o1 = r.origin[1], o2 = r.origin[2];
    const float d0 = r.dir[0], d1 = r.dir[1], d2 = r.dir[2];
    uint32_t scene = mem.u32(G::scene_quads);
    for (uint32_t i = 0; i < mem.at<uint8_t>(scene + countOffset); ++i) {
        uint32_t in[7] = {mem.u32(list + 4 * i), fbits(o0), fbits(o1), fbits(o2), fbits(d0), fbits(d1), fbits(d2)};
        Tgp::Out hit;
        copro(0x62, in, 7, hit);
        if (!(ffrom(hit.w[0]) > 0.0f))
            continue;
        // hit.w: 1.0, x, y, z, s^2, nx, ny, nz, attribute
        uint32_t slot[1] = {0};
        Tgp::Out none;
        copro(0x74, slot, 1, none);
        uint32_t pt[3] = {hit.w[1], hit.w[2], hit.w[3]};
        Tgp::Out cam;
        copro(0x1A, pt, 3, cam);
        float z = ffrom(cam.w[2]);
        int32_t key = cvtzril(z * 10.0f);
        copro(0x06, nullptr, 0, none);
        uint32_t n = mem.u32(G::shot_candidate_count);
        ShotCandidate& c = mem.at<ShotCandidate>(G::shot_candidates, n);
        c.hit[3] = ffrom(hit.w[5]);
        c.w10[0] = ffrom(hit.w[6]);
        mem.set32(G::shot_sort + 8 * n + 4, n);
        c.target.a = 0;
        c.w10[1] = ffrom(hit.w[7]);
        mem.set32(G::shot_sort + 8 * n, uint32_t(key));
        mem.set32(G::shot_candidate_count, n + 1);
        c.hit[0] = ffrom(cam.w[0]);
        c.hit[2] = z;
        c.flags |= 0x10;
        c.hit[1] = ffrom(cam.w[1]);
        c.code = int32_t(hit.w[8]);
    }
}

template <class P> void Game<P>::shot_test_list_a(int player) { shot_test_list(player, G::shot_list_a, 0x60); }
template <class P> void Game<P>::shot_test_list_b(int player) { shot_test_list(player, G::shot_list_b, 0x61); }

// 0x45E70 / PC 0x489FBC (player): sorts the candidates; the nearest (shot_sort[0]) takes the hit: a scene item (flags
// bit 4) only gives its code; a body part (bit 9) gets bit (player + 1) | 0x100 in its flags and so does its owner
// (+0x58); an object gets them in +0x58 (PC: both also set pc_shot_hit). The shot result keeps the hit point and the
// candidate's other values; a scene item with a code other than 0 / 4 spawns the hit flash (the PC passes the code
// too; on the PC only an object whose own flags have bit 4 can be a "scene item").
template <class P> void Game<P>::shot_hit_nearest(int player)
{
    shot_targets_sort();
    ShotCandidate c = mem.at<ShotCandidate>(G::shot_candidates, mem.u32(G::shot_nearest));
    ShotResult& res = mem.at<ShotResult>(shot_result_rec(player));
    // 4P: seats 2 / 3 set player 1's / 2's bit, so every "hit by a player" test (the `& 6`s) sees their hits; bits 3 / 4
    // are not free (bit 4 is the scene-item flag the candidates copy from the object's flags), so the hit table
    // (coop4_hit_add) names the real seats for part_hit / enemy_part_hits
    uint32_t bit = 1u << ((player & 1) + 1);
    if (coop4_on() && ((c.flags >> 4) & 1) == 0) {
        coop4_hit_add(c.target.a, player);
        if ((c.flags >> 9) & 1)
            coop4_hit_add(mem.at<BodyPart>(c.target.a).owner.a, player);
    }
    if (((c.flags >> 4) & 1) == 0) {
        ++stat_hits[player & 3];   // (testing only)
        stat_last_hit[player & 3] = ((c.flags >> 9) & 1) ? uint32_t(mem.at<BodyPart>(c.target.a).owner.a) : c.target.a;
        if (((c.flags >> 9) & 1) == 0) {
            Obj& o = mem.at<Obj>(c.target.a);
            o.flags = bit | o.flags | 0x100;
            res.code = 0;
            if constexpr (P::pc)
                mem.set32(G::pc_shot_hit, 1);
        } else {
            BodyPart& part = mem.at<BodyPart>(c.target.a);
            uint32_t owner = part.owner.a;
            part.flags = bit | part.flags | 0x100;
            res.code = 0;
            Obj& o = mem.at<Obj>(owner);
            o.flags = bit | o.flags | 0x100;
            if constexpr (P::pc)
                mem.set32(G::pc_shot_hit, 1);
        }
    } else {
        res.code = c.code;
    }
    res.hit[0] = c.hit[0];
    res.hit[1] = c.hit[1];
    res.hit[2] = c.hit[2];
    res.w18[0] = c.hit[3];
    res.w18[1] = c.w10[0];
    res.w18[2] = c.w10[1];
    if ((c.flags >> 4) & 1) {
        if (res.code != 0 && res.code != 4)
            shot_flash_spawn(res.hit[0], res.hit[1], res.hit[2], player, res.code);
    }
}

// 0x466D0 / PC 0x48A44C: stable LSD radix sort of shot_sort {key, index} by the key's low 16 bits (two 8-bit passes through
// shot_sort_scratch); counting from the last entry down keeps equal keys in candidate order.
template <class P> void Game<P>::shot_targets_sort()
{
    const uint32_t n = mem.u32(G::shot_candidate_count);
    for (int shift = 0; shift <= 8; shift += 8) {
        int32_t count[256] = {};
        for (uint32_t i = 0; i < n; ++i)
            ++count[(mem.u32(G::shot_sort + 8 * i) >> shift) & 0xFF];
        for (int b = 1; b < 256; ++b)
            count[b] += count[b - 1];
        for (int32_t i = int32_t(n) - 1; i >= 0; --i) {
            uint32_t key = mem.u32(G::shot_sort + 8 * uint32_t(i));
            uint32_t idx = mem.u32(G::shot_sort + 8 * uint32_t(i) + 4);
            int32_t pos = --count[(key >> shift) & 0xFF];
            mem.set32(G::shot_sort_scratch + 8 * uint32_t(pos), key);
            mem.set32(G::shot_sort_scratch + 8 * uint32_t(pos) + 4, idx);
        }
        for (uint32_t i = 0; i < n; ++i) {
            mem.set32(G::shot_sort + 8 * i, mem.u32(G::shot_sort_scratch + 8 * i));
            mem.set32(G::shot_sort + 8 * i + 4, mem.u32(G::shot_sort_scratch + 8 * i + 4));
        }
    }
}

// PC 0x4FC76C / 0x41347C / 0x4D7934 (three identical copies): the PC's table square root, up to 0.39 % off: the
// exponent halved, the mantissa's top 7 bits and the exponent's parity index pc_sqrt_table (tools/pc_model.py)
template <class P> float Game<P>::pc_fast_sqrt_table(float x)
{
    if constexpr (!P::pc)
        return pm::fast_sqrt(x);   // PC Mode on the arcade logic: the rebuilt table
    else {
        const uint32_t b = fbits(x);
        const int32_t e = int32_t(b - 0x3F800000u);
        const uint32_t index = ((b & 0x7FFFFFu) | (uint32_t(e) & 0x800000u)) >> 16;
        const int32_t half = (int32_t(uint32_t(e) & 0xFF000000u) >> 1) + 0x3F800000;
        return ffrom(mem.u32(G::pc_sqrt_table + index * 4) + uint32_t(half));
    }
}
template <class P> float Game<P>::pc_fast_sqrt(float x) { return pc_fast_sqrt_table(x); }
template <class P> float Game<P>::pc_fast_sqrt2(float x) { return pc_fast_sqrt_table(x); }
template <class P> float Game<P>::pc_fast_sqrt3(float x) { return pc_fast_sqrt_table(x); }

} // namespace hotd::port
