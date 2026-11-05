// Enemy support routines every frame needs (the depth and attacker lists, the target point, the dynamic difficulty, the
// angle to the camera, dying and corpses, freeing a body, turning and walking). First written in the runtime
// (src/game/enemy_support.cpp, T1 / R4), read from the arcade disassembly / decompilation, moved here for their proofs.
// Arcade forms (decl/support_b.h).
#include "port/game.h"

#include <cstdlib>

namespace hotd::port {

// 0x45480 depth_list_add: a shootable object (flags bit 0) in front of the camera joins the frame's depth list (the
// shot test's candidates), key (int)(z x 10). Only z < 0 is left out (`cmpr`, `bl`): a NaN z joins too, key 0 (the
// whole-game differential, Chapter 3 frames 54784 / 54818: a thrown axe's first frame, flying_axe_spin, still has the
// task block's old word 0xFFFFFF82 in +0x40; MAME's list got it with key 0)
template <class P> void Game<P>::depth_list_add(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        if (!(mem.u32(obj + 0x58) & 1))
            return;
        const float z = mem.f32(obj + 0x40);
        if (z < 0.0f)
            return;
        const uint32_t n = mem.u32(G::depth_list_count);
        a::DepthEntry& e = mem.template at<a::DepthEntry>(G::depth_list, n);
        e.obj.a = obj;
        e.depth = z != z ? 0 : cvtzril(z * 10.0f);
        mem.set32(G::depth_list_count, n + 1);
        mem.template at<a::DepthEntry>(G::depth_list, n).flags = mem.u32(obj + 0x58);
    }
}

// 0x32390 attacker_list_add: a shootable enemy holding an attack position past 0 / 1 joins the attackers (up to 13),
// keyed by (int)(distance to the camera x 10) (coprocessor 0x1F)
template <class P> void Game<P>::attacker_list_add(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        if (!(mem.u32(obj + 0x58) & 1) || mem.at<int8_t>(obj + 0x2E7) <= 1)
            return;
        const uint32_t n = mem.u32(G::attack_users_count);
        if (int32_t(n) > 12)
            return;
        int32_t key;
        if (pc_path()) {   // PC Mode (docs/pcmode.md row 10): the PC's 0x46EEE0 (table square root)
            const float pos[3] = {mem.f32(obj + 0x20), mem.f32(obj + 0x24), mem.f32(obj + 0x28)};
            const float cam[3] = {mem.f32(G::camera_pos), mem.f32(G::camera_pos + 4), mem.f32(G::camera_pos + 8)};
            key = pm::attacker_key(pos, cam);
        } else {
            uint32_t in[6] = {mem.u32(obj + 0x20), mem.u32(obj + 0x24), mem.u32(obj + 0x28),
                              mem.u32(G::camera_pos), mem.u32(G::camera_pos + 4), mem.u32(G::camera_pos + 8)};
            Tgp::Out o;
            copro(0x1F, in, 6, o);
            key = cvtzril(ffrom(o.w[0]) * 10.0f);
        }
        mem.set32(G::attack_users + 8 * n + 4, obj);
        mem.set32(G::attack_users_count, n + 1);
        mem.set32(G::attack_users + 8 * n, uint32_t(key));
    }
}

// 0x768F0 enemy_target_distance: the point the enemy heads for (+0x3A0, y 0) and its side of the screen (+0x338, from
// its camera-space x): one player: that player's body (side only when attack_side is set); two players: the body of
// the player on its side; otherwise the camera. +0x334 = the player count.
template <class P> void Game<P>::enemy_target_distance(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const int16_t players = mem.at<int16_t>(G::players_in_game);
        auto sideOf = [&]() {
            if (pc_path()) {   // PC Mode: the PC's 0x4D900C transforms with its own current matrix (pc_matrix_transform2)
                float ox = 0.0f, oy = 0.0f, oz = 0.0f;
                pm_cur().transform(mem.f32(obj + 0x20), mem.f32(obj + 0x24), mem.f32(obj + 0x28), ox, oy, oz);
                return 0.0f <= ox ? 1u : 0u;
            }
            uint32_t in[3] = {mem.u32(obj + 0x20), mem.u32(obj + 0x24), mem.u32(obj + 0x28)};
            Tgp::Out o;
            copro(0x1A, in, 3, o);
            return ffrom(o.w[0]) >= 0.0f ? 1u : 0u;
        };
        auto toBody = [&](uint32_t p) {
            const uint32_t body = mem.u32(G::players + 0xBC + p * 0xCC);
            mem.set32(obj + 0x3A4, 0);
            mem.set32(obj + 0x3A0, mem.u32(body + 0x20));
            mem.set32(obj + 0x3A8, mem.u32(body + 0x28));
        };
        if (players == 1) {
            toBody(uint32_t(int32_t(mem.at<int8_t>(0x51F700))));
            mem.set32(obj + 0x338, mem.at<uint8_t>(G::attack_side) != 0 ? sideOf() : 0);
        } else if (players == 2) {
            const uint32_t side = sideOf();
            mem.set32(obj + 0x338, side);
            toBody(side);
        } else {
            mem.set32(obj + 0x338, mem.at<uint8_t>(G::attack_side) != 0 ? sideOf() : 0);
            mem.set32(obj + 0x3A4, 0);
            mem.set32(obj + 0x3A0, mem.u32(G::camera_pos));
            mem.set32(obj + 0x3A8, mem.u32(G::camera_pos + 8));
        }
        mem.set32(obj + 0x334, uint32_t(int32_t(players)));
    }
}

// 0x264F0 difficulty_level: (players in game - last count) x 4 + the level, +1 every 3000 frames (0x520714), + the
// hits counter unless 0x520798; 0..15
template <class P> void Game<P>::difficulty_level()
{
    const int16_t players = mem.at<int16_t>(G::players_in_game);
    uint32_t v = uint32_t((int32_t(players) - int32_t(mem.at<int16_t>(0x51F4A2))) * 4) + mem.at<uint16_t>(0x51F492);
    if (mem.u32(0x520714) % 3000 == 0)
        v = v + 1;
    if (mem.at<int16_t>(0x520798) == 0)
        v = uint32_t(int32_t(mem.at<int8_t>(G::hits_counter))) + v;
    mem.at<int8_t>(G::hits_counter) = 0;
    if ((v >> 15) & 1)
        v = 0;
    mem.at<uint16_t>(0x51F492) = uint16_t(v);
    if (0xF0000 < int32_t(v << 16))
        mem.at<uint16_t>(0x51F492) = 0xF;
    mem.at<int16_t>(0x51F4A2) = players;
    mem.at<int16_t>(0x51F4A6) = mem.at<int16_t>(G::players_in_game_b);
}

// 0x69A80 angle_to_camera: the camera's direction in the object's own frame (coprocessor 0x0F), the yaw (16 bits)
template <class P> uint32_t Game<P>::angle_to_camera(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
        return 0;
    } else {
        if (pc_path()) {   // PC Mode (docs/pcmode.md row 10): the PC's 0x4C5048 on its own matrix maths
            const float pos[3] = {mem.f32(obj + 0x20), mem.f32(obj + 0x24), mem.f32(obj + 0x28)};
            const float cam[3] = {mem.f32(G::camera_pos), mem.f32(G::camera_pos + 4), mem.f32(G::camera_pos + 8)};
            return pm::angle_to_point(pos, mem.u32(obj + 0x2C), mem.u32(obj + 0x30), mem.u32(obj + 0x34), cam);
        }
        Tgp::Out none;
        copro(0x21, nullptr, 0, none);
        uint32_t a[1] = {0u - mem.u32(obj + 0x2C)};
        copro(0x14, a, 1, none);
        a[0] = 0u - mem.u32(obj + 0x30);
        copro(0x15, a, 1, none);
        a[0] = 0u - mem.u32(obj + 0x34);
        copro(0x16, a, 1, none);
        uint32_t t[3] = {mem.u32(obj + 0x20) ^ 0x80000000u, mem.u32(obj + 0x24) ^ 0x80000000u,
                         mem.u32(obj + 0x28) ^ 0x80000000u};
        copro(0x12, t, 3, none);
        uint32_t c[3] = {mem.u32(G::camera_pos), mem.u32(G::camera_pos + 4), mem.u32(G::camera_pos + 8)};
        Tgp::Out v;
        copro(0x1A, c, 3, v);
        Tgp::Out ang;
        copro(0x0F, v.w, 3, ang);
        uint32_t yaw = ang.w[0], pitch = ang.w[1];
        fixAngles(yaw, pitch);
        copro(0x06, nullptr, 0, none);
        return yaw & 0xFFFF;
    }
}

// 0x76B80 enemy_dying: the death motion plays (drawn, anim_time + 1); at its end the corpse routine from the second to
// last frame (NoBias); motion 0x1FD at frame 53 over water (floor type 5) sinks with a splash (kind 160), else a floor
// splat
template <class P> void Game<P>::enemy_dying(uint32_t obj)
{
    if ((mem.u32(obj + 0x58) >> 23) & 1) {
        const float y = mem.f32(G::camera_records + 4) - 15.0f;
        mem.set32(obj + 0x24, fbits(y));
        shadow_setup(y);
    } else {
        shadow_setup(mem.f32(G::floor_y));
    }
    object_draw(obj);
    if ((mem.at<uint8_t>(obj + 0x3C5) >> 4) & 1) {
        const uint16_t n = uint16_t(mem.at<uint16_t>(G::motion_frame_counts + 4 * uint32_t(mem.at<uint16_t>(obj + 0xD0))) - 2);
        mem.set32(obj, F::enemy_corpse);
        mem.at<uint16_t>(obj + 0xF4) = n;
        mem.set32(obj + 0xF8, uint32_t(n));
        mem.at<uint8_t>(obj + 0x3C5) |= 0x08;
        return;
    }
    mem.set32(obj + 0xF8, mem.u32(obj + 0xF8) + 1);
    if (mem.at<int16_t>(obj + 0xD0) == 0x1FD && mem.at<int16_t>(obj + 0xF4) == 53) {
        LocalScope scope(*this);
        const uint32_t out = local_alloc(0x30);
        if (floor_probe(obj + 0x20, out, out + 0x10, out + 0x20) == 1 && mem.u32(out + 0x20) == 5) {
            const uint32_t t = task_create(0x3970, 0x3C8);
            task_clear_work(t);
            mem.set32(t + 0x20, mem.u32(obj + 0x20));
            mem.set32(t + 0x24, mem.u32(obj + 0x24));
            mem.set32(t + 0x28, mem.u32(obj + 0x28));
            mem.set32(obj, F::enemy_corpse_sink);
            return;
        }
        floor_splat_spawn(obj);
    }
}

// 0x76D60 enemy_corpse_sink: every other frame drawn, sinking by frame x 0.0272; after 40 frames removed (enemy_count
// - 1 with draw_flags bit 3)
template <class P> void Game<P>::enemy_corpse_sink(uint32_t obj)
{
    const int32_t t = int32_t(mem.u32(obj + 0x50));
    mem.set32(obj + 0x50, uint32_t(t + 1));
    if ((t % 2) != 0) {
        const float k = float(t + 1) * ffrom(0x3CDF0123u);
        if (!((mem.u32(obj + 0x58) >> 23) & 1)) {
            mem.set32(obj + 0x24, fbits(mem.f32(obj + 0x24) - k));
            shadow_setup(mem.f32(G::floor_y));
        } else {
            const float y = (mem.f32(G::camera_records + 4) - 15.0f) - k;
            mem.set32(obj + 0x24, fbits(y));
            shadow_setup(y);
        }
        object_draw(obj);
    }
    if (t + 1 > 0x27) {
        if ((mem.at<uint8_t>(obj + 0x3C4) >> 3) & 1)
            mem.at<int16_t>(G::enemy_count) = int16_t(mem.at<int16_t>(G::enemy_count) - 1);
        body_free_parts(obj);
        enemy_free_extras(obj);
        task_exit();
    }
}

// 0x76CB0 enemy_corpse: drawn every other frame (blinking); after 40 frames freed (enemy_count - 1 when draw_flags
// bit 3)
template <class P> void Game<P>::enemy_corpse(uint32_t obj)
{
    const int32_t t = int32_t(mem.u32(obj + 0x50));
    mem.set32(obj + 0x50, uint32_t(t + 1));
    if (t % 2 != 0) {
        if ((mem.u32(obj + 0x58) >> 23) & 1) {
            const float y = mem.f32(G::camera_records + 4) - 15.0f;
            mem.set32(obj + 0x24, fbits(y));
            shadow_setup(y);
        } else {
            shadow_setup(mem.f32(G::floor_y));
        }
        object_draw(obj);
    }
    if (int32_t(mem.u32(obj + 0x50)) > 39) {
        if ((mem.at<uint8_t>(obj + 0x3C4) >> 3) & 1)
            mem.at<int16_t>(G::enemy_count) = int16_t(mem.at<int16_t>(G::enemy_count) - 1);
        body_free_parts(obj);
        enemy_free_extras(obj);
        task_exit();
    }
}

// 0x38F50 part_tree_free_children
template <class P> void Game<P>::part_tree_free_children(uint32_t node)
{
    const uint32_t list = mem.u32(node + 0x24);
    for (int32_t i = 0; i < int32_t(mem.u32(node + 0x34)); ++i) {
        const uint32_t c = mem.u32(list + 4 * uint32_t(i));
        part_tree_free_children(c);
        part_node_free(c);
    }
    if (mem.u32(node + 0x24) != 0)
        mem_free(mem.u32(node + 0x24));
}

// 0x39030 body_free_parts: the part tree, the pose buffers, the object's player_bodies entry
template <class P> void Game<P>::body_free_parts(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const uint32_t root = mem.u32(obj + 0xC8);
        const uint32_t list = mem.u32(root + 0x24);
        for (int32_t i = 0; i < int32_t(mem.u32(root + 0x34)); ++i) {
            const uint32_t c = mem.u32(list + 4 * uint32_t(i));
            const uint32_t cl = mem.u32(c + 0x24);
            for (int32_t j = 0; j < int32_t(mem.u32(c + 0x34)); ++j) {
                part_tree_free_children(mem.u32(cl + 4 * uint32_t(j)));
                part_node_free(mem.u32(cl + 4 * uint32_t(j)));
            }
            if (mem.u32(c + 0x24) != 0)
                mem_free(mem.u32(c + 0x24));
            part_node_free(c);
        }
        if (mem.u32(root + 0x24) != 0)
            mem_free(mem.u32(root + 0x24));
        part_node_free(root);
        mem_free(mem.u32(obj + 0xDC));
        mem_free(mem.u32(obj + 0xE0));
        mem_free(mem.u32(obj + 0x114));
        mem_free(mem.u32(obj + 0x118));
        mem.set32(body_slot_addr(int32_t(mem.at<int8_t>(obj + 0x2E6))), 0);   // (G::player_bodies + 4 x slot; Survival: 16.. its own)
    }
}

// 0x32C90 enemy_free_extras: the blocks at +0x37C..+0x388
template <class P> void Game<P>::enemy_free_extras(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        for (uint32_t off = 0x37C; off <= 0x388; off += 4) {
            if (mem.u32(obj + off) != 0) {
                mem_free(mem.u32(obj + off));
                mem.set32(obj + off, 0);
            }
        }
    }
}

// 0x1CF10 angle_turn_toward(obj, angle, rate): the heading (+0x30, 16 bits) turns toward `angle` by `rate` the short
// way round; within `rate` (also across 0 / 0x10000) it takes `angle`
template <class P> void Game<P>::angle_turn_toward(uint32_t obj, uint32_t angle, uint32_t rate)
{
    if constexpr (P::pc) {
        (void)obj, (void)angle, (void)rate;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const int32_t cur = mem.at<uint16_t>(obj + 0x30), tgt = int32_t(angle & 0xFFFF), r = int32_t(rate);
        const int32_t d = std::abs(cur - tgt), dUp = std::abs(tgt - (cur - 0x10000)), dDown = std::abs(cur - (tgt - 0x10000));
        int32_t out;
        if (d <= r || dUp <= r || dDown <= r)
            out = tgt;
        else if (cur < tgt)
            out = d > dDown ? cur - r : cur + r;
        else
            out = d > dUp ? cur + r : cur - r;
        mem.set32(obj + 0x30, uint32_t(out));
    }
}

// 0x1D2D0 walk_to(obj, x, z, turn, within): the unit direction to (x, z) on the floor (coprocessor 0x0C) at +0x78; the
// heading turned toward it by `turn` (its angle from coprocessor 0x0F, folded by half a turn); 1 when the floor
// distance (0x1F) is less than `within`
template <class P> uint32_t Game<P>::walk_to(uint32_t obj, float x, float z, uint32_t turn, float within)
{
    if constexpr (P::pc) {
        (void)obj, (void)x, (void)z, (void)turn, (void)within;   // the PC form: not ported (vtable only)
        return 0;
    } else {
        if (pc_path()) {   // PC Mode (docs/pcmode.md row 10): the PC's 0x4479CC (table square root, pc_vec_angles3)
            float dir[3];
            uint32_t yaw = 0;
            const uint32_t within1 = pm::walk_to(mem.f32(obj + 0x20), mem.f32(obj + 0x28), x, z, within, dir, yaw);
            for (uint32_t k = 0; k < 3; ++k)
                mem.set32(obj + 0x78 + 4 * k, fbits(dir[k]));
            angle_turn_toward(obj, yaw & 0xFFFF, turn);
            return within1;
        }
        const float dx = x - mem.f32(obj + 0x20), dz = z - mem.f32(obj + 0x28);
        Tgp::Out o;
        uint32_t d[3] = {fbits(dx), 0, fbits(dz)};
        copro(0x0C, d, 3, o);
        mem.set32(obj + 0x78, o.w[0]);
        mem.set32(obj + 0x7C, o.w[1]);
        mem.set32(obj + 0x80, o.w[2]);
        copro(0x0F, d, 3, o);
        uint32_t a0 = o.w[0];
        const uint32_t a1 = o.w[1] & 0xFFFF;
        if (((a1 << 1) ^ a1) & 0x8000)
            a0 += 0x8000;
        angle_turn_toward(obj, a0 & 0xFFFF, turn);
        uint32_t p[6] = {mem.u32(obj + 0x20), 0, mem.u32(obj + 0x28), fbits(x), 0, fbits(z)};
        copro(0x1F, p, 6, o);
        return ffrom(o.w[0]) < within ? 1u : 0u;
    }
}

// 0x54EB0 enemy_scripted_to_ai: a scripted zombie becomes an ordinary attacker (enemy_main, state 1): action /
// sub-state by its type and the flag of its enemy slot (0x51FB68 + slot x 0x2C, bit 0), the motion of that list entry
// (0x20C for type 0x1E with part bit 1 lost), blended over 8 frames when it differs, then its target distance
template <class P> void Game<P>::enemy_scripted_to_ai(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        mem.set32(obj, F::enemy_main);
        const int16_t type = mem.at<int16_t>(obj + 0xCC);
        int16_t action, sub;
        const bool slotFlag = (mem.at<uint8_t>(0x51FB68 + uint32_t(int32_t(mem.at<int8_t>(obj + 0x2E6))) * 0x2C) & 1) != 0;
        if (!slotFlag && type != 0x1D) {
            if (type == 0x1E || type == 0x3F || type == 0x49) {
                action = type == 0x49 ? 0 : 1;
                mem.set32(obj + 0x330, 1);   // walk_variant
                sub = 2;
            } else {
                action = 0;
                sub = 1;
            }
        } else {
            if (mem.at<int16_t>(obj + 0x64) == 10 && mem.at<int8_t>(G::chapter) == 0 && mem.at<int8_t>(G::event_index) == 0) {
                mem.set32(obj + 0x330, 1);
                sub = 2;
            } else {
                sub = 1;
            }
            action = 1;
        }
        mem.at<int16_t>(obj + 0x64) = action;
        mem.at<int16_t>(obj + 0x60) = sub;
        uint32_t motion;
        if (type == 0x1E && ((mem.at<uint8_t>(obj + 0x2E4) >> 1) & 1))
            motion = 0x20C;
        else {
            const uint32_t row = mem.u32(G::enemy_motion_lists + uint32_t(int32_t(type)) * 4);
            motion = mem.u32(mem.u32(row + uint32_t(int32_t(action)) * 4) + uint32_t(int32_t(sub)) * 8);
        }
        if (uint32_t(int32_t(mem.at<int16_t>(obj + 0xD0))) != motion)
            set_motion_blend(obj, motion, 0, 8);
        mem.at<int16_t>(obj + 0x5C) = 1;
        enemy_target_distance(obj);
    }
}

// 0x1CF90 turn_toward_camera(obj, step): toward the camera (with two or more enemies, 3 units left or right of it by the
// attack slot's parity, in double precision): the unit direction at +0x78, the heading turned by `step` (as
// angle_turn_toward)
template <class P> void Game<P>::turn_toward_camera(uint32_t obj, uint32_t step)
{
    if constexpr (P::pc) {
        (void)obj, (void)step;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const float camx = mem.f32(G::camera_pos), camz = mem.f32(G::camera_pos + 8);
        if (pc_path()) {   // PC Mode (docs/pcmode.md row 10): the PC's 0x44782C (table square root, pc_vec_angles3)
            const float dx = pm::camera_aim_dx(camx, mem.f32(obj + 0x20), mem.at<int16_t>(G::enemy_count) >= 2,
                                               mem.at<int8_t>(obj + 0x2E7));
            const float dz = camz - mem.f32(obj + 0x28);
            float dir[3];
            pm::floor_dir(dx, dz, dir);
            for (uint32_t k = 0; k < 3; ++k)
                mem.set32(obj + 0x78 + 4 * k, fbits(dir[k]));
            uint32_t yaw = 0, pitch = 0;
            pm::vec_angles(dx, 0.0f, dz, yaw, pitch);
            angle_turn_toward(obj, yaw & 0xFFFF, step);
            return;
        }
        float dx;
        if (mem.at<int16_t>(G::enemy_count) < 2)
            dx = camx - mem.f32(obj + 0x20);
        else
            dx = float(double(int32_t(mem.at<int8_t>(obj + 0x2E7)) % 2) * 6.0 + double(camx) - 3.0 - double(mem.f32(obj + 0x20)));
        const float dz = camz - mem.f32(obj + 0x28);
        Tgp::Out o;
        uint32_t d[3] = {fbits(dx), 0, fbits(dz)};
        copro(0x0C, d, 3, o);
        mem.set32(obj + 0x78, o.w[0]);
        mem.set32(obj + 0x7C, o.w[1]);
        mem.set32(obj + 0x80, o.w[2]);
        copro(0x0F, d, 3, o);
        uint32_t a0 = o.w[0];
        const uint32_t a1 = o.w[1] & 0xFFFF;
        if (((a1 << 1) ^ a1) & 0x8000)
            a0 += 0x8000;
        angle_turn_toward(obj, a0 & 0xFFFF, step);
    }
}

// 0x69DD0 move_along_heading(obj, step): (0, 0, step) turned by the object's angles (z +0x34, y +0x30, x +0x2C) added to
// its position
template <class P> void Game<P>::move_along_heading(uint32_t obj, float step)
{
    if constexpr (P::pc) {
        (void)obj, (void)step;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        if (pc_path()) {   // PC Mode (docs/pcmode.md row 10): the PC's 0x4C521C on its own matrix maths
            float s[3];
            pm::heading_step(mem.u32(obj + 0x2C), mem.u32(obj + 0x30), mem.u32(obj + 0x34), step, s);
            mem.set32(obj + 0x28, fbits(mem.f32(obj + 0x28) + s[2]));
            mem.set32(obj + 0x24, fbits(mem.f32(obj + 0x24) + s[1]));
            mem.set32(obj + 0x20, fbits(mem.f32(obj + 0x20) + s[0]));
            return;
        }
        Tgp::Out none, o;
        copro(0x21, nullptr, 0, none);
        uint32_t a[3] = {mem.u32(obj + 0x34), mem.u32(obj + 0x30), mem.u32(obj + 0x2C)};
        copro(0x86, a, 3, none);
        uint32_t v[3] = {0, 0, fbits(step)};
        copro(0x1A, v, 3, o);
        mem.set32(obj + 0x28, fbits(ffrom(o.w[2]) + mem.f32(obj + 0x28)));
        mem.set32(obj + 0x20, fbits(ffrom(o.w[0]) + mem.f32(obj + 0x20)));
        mem.set32(obj + 0x24, fbits(ffrom(o.w[1]) + mem.f32(obj + 0x24)));
        copro(0x06, nullptr, 0, none);
    }
}

} // namespace hotd::port
