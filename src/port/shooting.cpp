// Shooting: the shot test, A and PC (PA / PM) (docs/decompilation.md "Player, light gun and shooting"). Read from the
// disassembly of both where Ghidra's C loses the coprocessor FIFO traffic (ray_sign_test, shot_test_list_a / b) or
// the x87 order (the PC's ray_sign_test). The PC tests no scene quads.
#include "port/game.h"

#include <cstring>

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
        if (mem.u32(G::shot_candidate_count) != 0) {
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
    }
    mem.set32(G::depth_list_count, 0);
    mem.set32(G::shot_pair_count, 0);
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
