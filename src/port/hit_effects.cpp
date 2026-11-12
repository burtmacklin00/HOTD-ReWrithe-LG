// Hit effects: blood (drips, bursts, bleeding parts), splashes, dust, sparks, the shot's impact and the gun's hit flash
// (the spawners and their tasks). First written as runtime overrides (T1, src/game/t1_bounds.cpp), read from the
// disassembly. Arcade forms (decl/support_b.h).
#include "port/game.h"

#include <cstring>

namespace hotd::port {

// camera space -> world: coprocessor 0x74 (push + stored matrix 1), 0x1A, pop (inline in the spawners)
template <class P> void Game<P>::fx_to_world(const uint32_t in[3], uint32_t out[3])
{
    Tgp::Out none, o;
    uint32_t one[1] = {1};
    copro(0x74, one, 1, none);
    copro(0x1A, in, 3, o);
    copro(0x06, nullptr, 0, none);
    for (int k = 0; k < 3; ++k)
        out[k] = o.w[k];
}

// 0x43510 blood_drip_at(x, y, z camera space): a blood_drip task (0x44E70) at the world point, model 0x46A
template <class P> void Game<P>::blood_drip_at(float x, float y, float z)
{
    if constexpr (P::pc) {
        (void)x, (void)y, (void)z;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        if (!pm_blood_take()) {   // PC Mode: the PC's cap of 9 bleed / drip tasks (0x486378; docs/pcmode.md row 9)
            uint32_t in[3] = {fbits(x), fbits(y), fbits(z)}, w[3];
            fx_to_world(in, w);   // (the coprocessor traffic kept as without the cap)
            return;
        }
        const uint32_t t = task_create(F::blood_drip, 0x58);
        uint32_t in[3] = {fbits(x), fbits(y), fbits(z)}, w[3];
        fx_to_world(in, w);
        mem.set32(t + 0x20, w[0]);
        mem.set32(t + 0x24, w[1]);
        mem.set32(t + 0x54, 0x46A);
        mem.set32(t + 0x28, w[2]);
    }
}

// 0x44E70 blood_drip (task): models 0x46B..0x4A3 one a frame at its place, scale 0.4, then gone
template <class P> void Game<P>::blood_drip(uint32_t t)
{
    const uint32_t m = mem.u32(t + 0x54) + 1;
    mem.set32(t + 0x54, m);
    if (m == 0x4A4) {
        pm_blood_end();   // PC Mode: pc_blood_task_count - 1 (0x4862BF)
        task_exit();
    }
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(t + 0x20), mem.u32(t + 0x24), mem.u32(t + 0x28)};
    copro(0x12, p, 3, none);
    uint32_t s[3] = {0x3ECCCCCDu, 0x3ECCCCCDu, 0x3ECCCCCDu};
    copro(0x13, s, 3, none);
    geo_draw_model(m);
    copro(0x06, nullptr, 0, none);
}

// 0x437A0 blood_burst_spawn(part): a blood_burst task (0x44EF0) on the part, from model 6
template <class P> void Game<P>::blood_burst_spawn(uint32_t part)
{
    if constexpr (P::pc) {
        (void)part;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const uint32_t t = task_create(F::blood_burst, 0x60);
        mem.set32(t + 0x5C, part);
        mem.set32(t + 0x54, 6);
        mem.set32(t + 0x58, mem.u32(part + 0x40));
    }
}

// 0x44EF0 blood_burst (task): models 7..0x23 one a frame at the part's camera-space place (+0x5C.., z less the part's
// +0x74), scaled by depth (1 beyond 20, else z / 26.67 + 0.25)
template <class P> void Game<P>::blood_burst(uint32_t t)
{
    const int32_t m = int32_t(mem.u32(t + 0x54));
    mem.set32(t + 0x54, uint32_t(m + 1));
    if (m > 0x22)
        task_exit();
    const uint32_t part = mem.u32(t + 0x5C);
    const float z = mem.f32(part + 0x64) - mem.f32(part + 0x74);
    const float s = z > 20.0f ? 1.0f : z / ffrom(0x41D55532u) + 0.25f;
    Tgp::Out none;
    copro(0x21, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(part + 0x5C), mem.u32(part + 0x60), fbits(z)};
    copro(0x12, p, 3, none);
    uint32_t sc[3] = {fbits(s), fbits(s), fbits(s)};
    copro(0x13, sc, 3, none);
    geo_draw_model(mem.u32(t + 0x54));
    copro(0x06, nullptr, 0, none);
}

// mem_free of a body part node. nan (2026-10-16) first ended the blood_burst tasks still on the part (a burst on a part
// cut off the next frame reads the freed block for the rest of its 29 frames: denormal / NaN places). ch4proof (the
// whole-game differential, Chapter 4 frame 71502: MAME's burst ran on, ours had ended) put the original back: the burst
// reads the freed block as the arcade does; geo_draw_model's guard skips its non-finite draws (robust.md fix 3).
template <class P> void Game<P>::part_node_free(uint32_t part)
{
    mem_free(part);
}

// 0x43740 part_bleed_spawn(part): a part_bleed task (0x435A0) on the part
template <class P> void Game<P>::part_bleed_spawn(uint32_t part)
{
    if constexpr (P::pc) {
        (void)part;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        if (!pm_blood_take())   // PC Mode: the PC's cap of 9 bleed / drip tasks (0x4864DC / 0x486520)
            return;
        const uint32_t t = task_create(F::part_bleed, 0x60);
        mem.set32(t + 0x50, 0);
        mem.set32(t + 0x5C, part);
        mem.set32(t + 0x58, mem.u32(part + 0x40));
    }
}

// 0x43770 part_bleed_start(part): a part_bleed task on the part's parent (+0x20)
template <class P> void Game<P>::part_bleed_start(uint32_t part)
{
    if constexpr (P::pc) {
        (void)part;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        if (!pm_blood_take())   // PC Mode: the PC's cap of 9 bleed / drip tasks (0x4864DC / 0x486520)
            return;
        const uint32_t t = task_create(F::part_bleed, 0x60);
        mem.set32(t + 0x50, 0);
        mem.set32(t + 0x5C, mem.u32(part + 0x20));
        mem.set32(t + 0x58, mem.u32(part + 0x40));
    }
}

// 0x435A0 part_bleed (task): while its body lives (magic +0xC) for 100 frames, every 20th frame a drip from the
// part's place when that point is in view (coprocessor 0x7B, radius 3)
template <class P> void Game<P>::part_bleed(uint32_t t)
{
    if (mem.u32(mem.u32(t + 0x58) + 0xC) == 0 || int32_t(mem.u32(t + 0x50)) > 0x63) {
        pm_blood_end();   // PC Mode: pc_blood_task_count - 1 (0x486416)
        task_exit();
    }
    const uint32_t n = mem.u32(t + 0x50) + 1;
    mem.set32(t + 0x50, n);
    if (int32_t(n) % 20 != 1)
        return;
    const uint32_t part = mem.u32(t + 0x5C);
    uint32_t cam[3] = {mem.u32(part + 0x5C), mem.u32(part + 0x60), mem.u32(part + 0x64)}, w[3];
    fx_to_world(cam, w);
    uint32_t in[4] = {w[0], w[1], w[2], fbits(3.0f)};
    Tgp::Out v;
    copro(0x7B, in, 4, v);
    if (!(ffrom(v.w[0]) > 0.0f))
        return;
    if (!pm_blood_take()) {   // PC Mode: the cap (the PC's drip through 0x486378)
        fx_to_world(cam, w);
        return;
    }
    const uint32_t d = task_create(F::blood_drip, 0x58);
    fx_to_world(cam, w);
    mem.set32(d + 0x20, w[0]);
    mem.set32(d + 0x24, w[1]);
    mem.set32(d + 0x54, 0x46A);
    mem.set32(d + 0x28, w[2]);
}

// 0x43DF0 hit_effect_at(x, y, z): a hit_splash task (0x43D50) at the world point (+0x48 a random angle)
template <class P> void Game<P>::hit_effect_at(float x, float y, float z)
{
    if constexpr (P::pc) {
        (void)x, (void)y, (void)z;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const uint32_t t = task_create(F::hit_splash, 0x58);
        mem.set32(t + 0x48, rand() & 0xFFFF);
        mem.set32(t + 0x38, fbits(x));
        mem.set32(t + 0x3C, fbits(y));
        mem.set32(t + 0x50, 0);
        mem.set32(t + 0x40, fbits(z));
    }
}

// 0x43D50 hit_splash (task): models 0x589..0x5A6 one a frame, flat (x -0x4000), scale 0.25
template <class P> void Game<P>::hit_splash(uint32_t t)
{
    const uint32_t n = mem.u32(t + 0x50);
    mem.set32(t + 0x50, n + 1);
    mem.set32(t + 0x54, n + 0x589);
    if (int32_t(n + 0x589) > 0x5A6)
        task_exit();
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(t + 0x38), mem.u32(t + 0x3C), mem.u32(t + 0x40)};
    copro(0x12, p, 3, none);
    uint32_t a[1] = {0xFFFFC000u};
    copro(0x14, a, 1, none);
    uint32_t s[3] = {0x3E800000u, 0x3E800000u, 0x3E800000u};
    copro(0x13, s, 3, none);
    geo_draw_model(mem.u32(t + 0x54));
    copro(0x06, nullptr, 0, none);
}

// 0x43EC0 floor_splat_spawn(obj): a floor splat (0x43E50) at (x, floor_y, z)
template <class P> void Game<P>::floor_splat_spawn(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const uint32_t t = task_create(F::floor_splat, 0x58);
        mem.set32(t + 0x20, mem.u32(obj + 0x20));
        mem.set32(t + 0x24, mem.u32(G::floor_y));
        mem.set32(t + 0x50, 0);
        mem.set32(t + 0x28, mem.u32(obj + 0x28));
    }
}

// 0x43F80 floor_splat_spawn_b(obj): a floor_dust task (0x43F00) at (x, z)
template <class P> void Game<P>::floor_splat_spawn_b(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const uint32_t t = task_create(F::floor_dust, 0x58);
        mem.set32(t + 0x20, mem.u32(obj + 0x20));
        mem.set32(t + 0x50, 0);
        mem.set32(t + 0x28, mem.u32(obj + 0x28));
    }
}

// 0x43E50 floor_splat / 0x43F00 floor_dust (task): models 0x320..0x333 one a frame at its place (floor_dust: y = the
// camera eye's y - 15)
template <class P> void Game<P>::floor_splat_frame(uint32_t t, bool dust)
{
    const uint32_t n = mem.u32(t + 0x50);
    mem.set32(t + 0x50, n + 1);
    mem.set32(t + 0x54, n + 0x320);
    if (int32_t(n + 0x320) > 0x333)
        task_exit();
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(t + 0x20), dust ? fbits(mem.f32(0x51EAB4) - 15.0f) : mem.u32(t + 0x24), mem.u32(t + 0x28)};
    copro(0x12, p, 3, none);
    geo_draw_model(mem.u32(t + 0x54));
    copro(0x06, nullptr, 0, none);
}
template <class P> void Game<P>::floor_splat(uint32_t t) { floor_splat_frame(t, false); }
template <class P> void Game<P>::floor_dust(uint32_t t) { floor_splat_frame(t, true); }

// 0x44080 wall_dust_spawn(obj, angle): a dust puff (task 0x43FB0) at the object's home place, standing on the wall
// (x 0x4000, y = the wall's angle)
template <class P> void Game<P>::wall_dust_spawn(uint32_t obj, uint32_t angle)
{
    const uint32_t t = task_create(F::wall_dust, 0x58);
    mem.set32(t + 0x30, angle);
    mem.set32(t + 0x2C, 0x4000);
    mem.set32(t + 0x20, mem.u32(obj + 0x2D4));
    mem.set32(t + 0x24, mem.u32(obj + 0x2D8));
    mem.set32(t + 0x50, 0);
    mem.set32(t + 0x28, mem.u32(obj + 0x2DC));
}

// 0x43FB0 wall_dust (task): models 0x320..0x333 one a frame, turned (y +0x30, x +0x2C), scale 0.75
template <class P> void Game<P>::wall_dust(uint32_t t)
{
    const uint32_t n = mem.u32(t + 0x50);
    mem.set32(t + 0x50, n + 1);
    mem.set32(t + 0x54, n + 0x320);
    if (int32_t(n + 0x320) > 0x333)
        task_exit();
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(t + 0x20), mem.u32(t + 0x24), mem.u32(t + 0x28)};
    copro(0x12, p, 3, none);
    uint32_t ry[1] = {mem.u32(t + 0x30)};
    copro(0x15, ry, 1, none);
    uint32_t rx[1] = {mem.u32(t + 0x2C)};
    copro(0x14, rx, 1, none);
    uint32_t s[3] = {0x3F400000u, 0x3F400000u, 0x3F400000u};
    copro(0x13, s, 3, none);
    geo_draw_model(mem.u32(t + 0x54));
    copro(0x06, nullptr, 0, none);
}

// 0x44B50 hop_dust_spawn(x, y, z): a dust puff (task 0x45340, models 0x320..0x333, scale 0.4)
template <class P> void Game<P>::hop_dust_spawn(float x, float y, float z)
{
    if constexpr (P::pc) {
        (void)x, (void)y, (void)z;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const uint32_t t = task_create(F::hop_dust, 0x58);
        mem.set32(t + 0x54, 799);
        mem.set32(t + 0x38, fbits(x));
        mem.set32(t + 0x3C, fbits(y));
        mem.set32(t + 0x40, fbits(z));
    }
}

// 0x45340 hop_dust (task)
template <class P> void Game<P>::hop_dust(uint32_t t)
{
    const uint32_t m = mem.u32(t + 0x54) + 1;
    mem.set32(t + 0x54, m);
    if (m == 0x334)
        task_exit();
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(t + 0x38), mem.u32(t + 0x3C), mem.u32(t + 0x40)};
    copro(0x12, p, 3, none);
    uint32_t s[3] = {0x3ECCCCCDu, 0x3ECCCCCDu, 0x3ECCCCCDu};
    copro(0x13, s, 3, none);
    geo_draw_model(m);
    copro(0x06, nullptr, 0, none);
}

// 0x438F0 prop_spurt_spawn(obj): the spurt (task 0x451A0, models 0x58A..0x5A6, scale 0.25) at the prop's camera-space
// place (+0x38.., z less its radius +0x90)
template <class P> void Game<P>::prop_spurt_spawn(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const uint32_t t = task_create(F::prop_spurt, 0x60);
        mem.set32(t + 0x58, obj);
        mem.set32(t + 0x54, 0x589);
    }
}

// 0x451A0 prop_spurt (task)
template <class P> void Game<P>::prop_spurt(uint32_t t)
{
    const uint32_t m = mem.u32(t + 0x54) + 1;
    mem.set32(t + 0x54, m);
    if (m == 0x5A7)
        task_exit();
    const uint32_t o = mem.u32(t + 0x58);
    Tgp::Out none;
    copro(0x21, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(o + 0x38), mem.u32(o + 0x3C), fbits(mem.f32(o + 0x40) - mem.f32(o + 0x90))};
    copro(0x12, p, 3, none);
    uint32_t s[3] = {0x3E800000u, 0x3E800000u, 0x3E800000u};
    copro(0x13, s, 3, none);
    geo_draw_model(m);
    copro(0x06, nullptr, 0, none);
}

// 0x437D0 part_hit_spark(part): a metal part hit: sparks (task 0x44FC0, models 0x8D..0x9F two a frame) at the part's
// place (3 nearer) in world
template <class P> void Game<P>::part_hit_spark(uint32_t part)
{
    if constexpr (P::pc) {
        (void)part;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const uint32_t t = task_create(F::part_spark_anim, 0x60);
        uint32_t in[3] = {mem.u32(part + 0x5C), mem.u32(part + 0x60), fbits(mem.f32(part + 0x64) - 3.0f)}, w[3];
        fx_to_world(in, w);
        mem.set32(t + 0x20, w[0]);
        mem.set32(t + 0x24, w[1]);
        mem.set32(t + 0x54, 0x8B);
        mem.set32(t + 0x28, w[2]);
    }
}

// 0x44FC0 part_spark_anim (task): model + 2 a frame (gone past 0x9F), at its place with the fixed orientation of
// table 0xAA690 (coprocessor 0x87), scale 0.8
template <class P> void Game<P>::part_spark_anim(uint32_t t)
{
    const int32_t m = int32_t(mem.u32(t + 0x54)) + 2;
    mem.set32(t + 0x54, uint32_t(m));
    if (m > 0x9F)
        task_exit();
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(t + 0x20), mem.u32(t + 0x24), mem.u32(t + 0x28)};
    copro(0x12, p, 3, none);
    uint32_t r[9];
    for (uint32_t k = 0; k < 9; ++k)
        r[k] = mem.u32(0xAA690 + 4 * k);
    copro(0x87, r, 9, none);
    uint32_t s[3] = {0x3F4CCCCDu, 0x3F4CCCCDu, 0x3F4CCCCDu};
    copro(0x13, s, 3, none);
    geo_draw_model(uint32_t(m));
    copro(0x06, nullptr, 0, none);
}

// 0x30EF0 shot_flash_spawn(x, y, depth, player, code): the gun's hit flash (task 0x30E50) at the shot point (camera
// space, 2 nearer) in world, turned like the player's shot (shot_results 0x520950 + 0x28 x player, +0x18): code 3
// models 0x8A..0x9E, 5: 0x414..0x428 (scale 0.5, sound 0xA0125F), 4: nothing, else 0x74..0x88 (scale 1.5, sound
// 0xA0120A)
template <class P> void Game<P>::shot_flash_spawn(float x, float y, float z, int player, int code)
{
    if constexpr (P::pc) {
        (void)x, (void)y, (void)z, (void)player, (void)code;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const uint32_t t = task_create(F::shot_flash, 0x3C8);
        uint32_t in[3] = {fbits(x), fbits(y), fbits(z - 2.0f)}, w[3];
        fx_to_world(in, w);
        mem.set32(t + 0x20, w[0]);
        mem.set32(t + 0x24, w[1]);
        mem.set32(t + 0x28, w[2]);
        const uint32_t r = shot_result_rec(player);   // 0x520950 + player x 0x28 (4P: seats 2 / 3 in the block)
        uint32_t v[3] = {mem.u32(r + 0x18), mem.u32(r + 0x1C), mem.u32(r + 0x20)};
        Tgp::Out a;
        copro(0x0F, v, 3, a);
        uint32_t yaw = a.w[0], pitch = a.w[1];
        const uint32_t s = pitch & 0xFFFF;
        if (((s << 1) ^ s) & 0x8000) {
            yaw += 0x8000;
            pitch = 0x8000 - pitch;
        }
        mem.set32(t + 0x2C, pitch);
        mem.set32(t + 0x30, yaw);
        uint32_t first = 0x74, last = 0x88, scale = 0x3FC00000u, cue = 0xA0120A;
        if (code == 4)
            return;
        if (code == 3) {
            first = 0x8A;
            last = 0x9E;
        } else if (code == 5) {
            first = 0x414;
            last = 0x428;
            scale = 0x3F000000u;
            cue = 0xA0125F;
        }
        mem.set32(t + 0x54, first);
        mem.set32(t + 0x31C, last);
        mem.set32(t + 0x33C, scale);
        sound_cue(cue);
    }
}

// 0x30E50 shot_flash (task): model + 1 a frame up to +0x31C, at its place turned +0x30 on y, scale +0x33C
template <class P> void Game<P>::shot_flash(uint32_t t)
{
    const int32_t m = int32_t(mem.u32(t + 0x54)) + 1;
    mem.set32(t + 0x54, uint32_t(m));
    if (m > int32_t(mem.u32(t + 0x31C)))
        task_exit();
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(t + 0x20), mem.u32(t + 0x24), mem.u32(t + 0x28)};
    copro(0x12, p, 3, none);
    uint32_t ry[1] = {mem.u32(t + 0x30)};
    copro(0x15, ry, 1, none);
    const uint32_t sc = mem.u32(t + 0x33C);
    uint32_t s[3] = {sc, sc, sc};
    copro(0x13, s, 3, none);
    geo_draw_model(uint32_t(m));
    copro(0x06, nullptr, 0, none);
}

// 0x95270 shot_effect(player task): the impact at the aim point: debris (0x95630, model 0x660 + player) from the near
// point (aim / 496, 1) flying 20 units a frame along the shot ray toward (aim / 496 x 1000, 1000), its angles; the
// flash (0x95140, model table 0xCA738[player]) at the near point in camera space
template <class P> void Game<P>::shot_effect(uint32_t task)
{
    if constexpr (P::pc) {
        (void)task;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const int32_t p = mem.at<int8_t>(task + 0x20);
        const uint32_t pl = player_rec(p) - 4;   // 0x51F280 + p x 0xCC (4P: seats 2 / 3 in the block)
        const float ax = float(mem.at<int16_t>(pl + 0x38)) / 496.0f, ay = float(mem.at<int16_t>(pl + 0x3A)) / 496.0f;
        const uint32_t d = task_create(F::shot_effect_debris, 0x64);
        // charsel4: Rogan / G's spark and flash are players 1 / 2's own in any seat (look_seat; in seats 3 / 4 then
        // as they are: no ring entry, no ramp)
        const int look = look_seat(p);
        mem.set32(d + 0x54, uint32_t((look & 1) + 0x660));
        if (coop4_on() && look >= 2) {   // 4P: player 2's spark (blue: its ramp reads clearly), the seat noted for the view
            mem.set32(d + 0x54, 0x661);
            const uint32_t k = mem.at<uint8_t>(coop4::kSparkNext) & 3;   // a ring of four
            mem.set32(coop4::kSparks + k * 8, d);
            mem.set32(coop4::kSparks + k * 8 + 4, uint32_t(p) | mem.u32(G::frame_counter) << 8);   // seat, frame
            mem.at<uint8_t>(coop4::kSparkNext) = uint8_t((k + 1) & 3);
        }
        Tgp::Out none, o, f;
        uint32_t one[1] = {1};
        copro(0x74, one, 1, none);
        uint32_t nearP[3] = {fbits(ax), fbits(ay), fbits(1.0f)};
        copro(0x1A, nearP, 3, o);
        mem.set32(d + 0x20, o.w[0]);
        mem.set32(d + 0x24, o.w[1]);
        mem.set32(d + 0x28, o.w[2]);
        uint32_t farP[3] = {fbits(ax * 1000.0f), fbits(ay * 1000.0f), fbits(1000.0f)};
        copro(0x1A, farP, 3, f);
        copro(0x06, nullptr, 0, none);
        uint32_t v[3] = {fbits(ffrom(f.w[0]) - mem.f32(d + 0x20)), fbits(ffrom(f.w[1]) - mem.f32(d + 0x24)),
                         fbits(ffrom(f.w[2]) - mem.f32(d + 0x28))};
        Tgp::Out u;
        copro(0x0C, v, 3, u);
        const float vx = ffrom(u.w[0]) * 20.0f, vy = ffrom(u.w[1]) * 20.0f, vz = ffrom(u.w[2]) * 20.0f;
        mem.set32(d + 0x5C, fbits(vy));
        mem.set32(d + 0x58, fbits(vx));
        mem.set32(d + 0x60, fbits(vz));
        uint32_t vv[3] = {fbits(vx), fbits(vy), fbits(vz)};
        Tgp::Out an;
        copro(0x0F, vv, 3, an);
        mem.set32(d + 0x30, an.w[0]);
        mem.set32(d + 0x2C, an.w[1]);
        const uint32_t s = mem.at<uint16_t>(d + 0x2C);
        if (((s << 1) ^ s) & 0x8000) {
            mem.set32(d + 0x2C, 0x8000 - an.w[1]);
            mem.set32(d + 0x30, mem.u32(d + 0x30) + 0x8000);
        }
        mem.set32(d + 0x50, 0);
        mem.set32(d + 0x34, mem.u32(0x51EAC4 + mem.u32(0x51F620) * 0x154));
        const uint32_t fl = task_create(F::shot_effect_flash, 0x58);
        uint32_t pos[3] = {mem.u32(d + 0x20), mem.u32(d + 0x24), mem.u32(d + 0x28)};
        Tgp::Out c;
        copro(0x1A, pos, 3, c);
        mem.set32(fl + 0x38, c.w[0]);
        mem.set32(fl + 0x3C, c.w[1]);
        mem.set32(fl + 0x40, c.w[2]);
        mem.set32(fl + 0x50, 0);
        mem.set32(fl + 0x54, mem.u32(0xCA738 + uint32_t(look < 2 ? look : p) * 4));
        if (coop4_on() && look >= 2) {
            // 4P: the table has two players' flashes (its words 2 / 3 are code: no model): player 2's flash as the
            // spark above, the seat noted for the view (its colour ramp, Teammate Flashes)
            mem.set32(fl + 0x54, mem.u32(0xCA73C));
            const uint32_t k = mem.at<uint8_t>(coop4::kFlashNext) & 3;
            mem.set32(coop4::kFlashes + k * 8, fl);
            mem.set32(coop4::kFlashes + k * 8 + 4, uint32_t(p) | mem.u32(G::frame_counter) << 8);
            mem.at<uint8_t>(coop4::kFlashNext) = uint8_t((k + 1) & 3);
        }
    }
}

// 0x95140 shot_effect_flash (task): facing the camera at its camera-space place, scale 0.1, model +0x54 + frame, 10
// frames
template <class P> void Game<P>::shot_effect_flash(uint32_t t)
{
    Tgp::Out none, a;
    copro(0x21, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(t + 0x38), mem.u32(t + 0x3C), mem.u32(t + 0x40)};
    copro(0x0F, p, 3, a);
    mem.set32(t + 0x48, a.w[0]);
    mem.set32(t + 0x44, a.w[1]);
    const uint32_t s = mem.at<uint16_t>(t + 0x44);
    if (((s << 1) ^ s) & 0x8000) {
        mem.set32(t + 0x44, 0x8000 - a.w[1]);
        mem.set32(t + 0x48, mem.u32(t + 0x48) + 0x8000);
    }
    copro(0x12, p, 3, none);
    uint32_t ry[1] = {mem.u32(t + 0x48)};
    copro(0x15, ry, 1, none);
    uint32_t rx[1] = {mem.u32(t + 0x44)};
    copro(0x14, rx, 1, none);
    uint32_t sc[3] = {0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DCCCCCDu};
    copro(0x13, sc, 3, none);
    geo_draw_model(mem.u32(t + 0x54) + mem.u32(t + 0x50));
    copro(0x06, nullptr, 0, none);
    const int32_t n = int32_t(mem.u32(t + 0x50));
    mem.set32(t + 0x50, uint32_t(n + 1));
    if (n > 8)
        task_exit();
}

// 0x95630 shot_effect_debris (task): flies by its velocity, turning (+0x34 + 0x1000 a frame on z), 60 frames
template <class P> void Game<P>::shot_effect_debris(uint32_t t)
{
    const uint32_t n = mem.u32(t + 0x50) + 1;
    mem.set32(t + 0x50, n);
    if (n == 61)
        task_exit();
    for (uint32_t k = 0; k < 3; ++k)
        mem.set32(t + 0x20 + 4 * k, fbits(mem.f32(t + 0x20 + 4 * k) + mem.f32(t + 0x58 + 4 * k)));
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(t + 0x20), mem.u32(t + 0x24), mem.u32(t + 0x28)};
    copro(0x12, p, 3, none);
    uint32_t ry[1] = {mem.u32(t + 0x30)};
    copro(0x15, ry, 1, none);
    uint32_t rx[1] = {mem.u32(t + 0x2C)};
    copro(0x14, rx, 1, none);
    mem.set32(t + 0x34, mem.u32(t + 0x34) + 0x1000);
    uint32_t rz[1] = {mem.u32(t + 0x34)};
    copro(0x16, rz, 1, none);
    geo_draw_model(mem.u32(t + 0x54));
    copro(0x06, nullptr, 0, none);
}

// 0x43200 muzzle_flash_spawn(which): a cut-scene gun's muzzle flash (task 0x44CF0, from model 0x37) at the point of
// row 0xAFBE0 + which x 0x18 (coprocessor 0x88), turned by its +0x10, offset (rand % 10 / 10 - 0.5, rand % 10 / 10 +
// 11.5, 0)
template <class P> void Game<P>::muzzle_flash_spawn(int which)
{
    if constexpr (P::pc) {
        (void)which;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const uint32_t t = task_create(F::muzzle_flash, 0x58);
        mem.set32(t + 0x54, 0x37);
        const uint32_t row = 0xAFBE0 + uint32_t(which) * 0x18;
        mem.set32(t + 0x30, mem.u32(row + 0x10));
        Tgp::Out none, o;
        copro(0x21, nullptr, 0, none);
        uint32_t in[3] = {mem.u32(row), mem.u32(row + 4), mem.u32(row + 8)};
        copro(0x88, in, 3, none);
        uint32_t a[1] = {mem.u32(t + 0x30)};
        copro(0x15, a, 1, none);
        const float x = pdiv(float(int32_t(rand()) % 10), 10.0f) + -0.5f;
        const float y = pdiv(float(int32_t(rand()) % 10), 10.0f) + 11.5f;
        uint32_t v[3] = {fbits(x), fbits(y), 0};
        copro(0x1A, v, 3, o);
        mem.set32(t + 0x20, o.w[0]);
        mem.set32(t + 0x24, o.w[1]);
        copro(0x06, nullptr, 0, none);
        mem.set32(t + 0x28, o.w[2]);
    }
}

// 0x44CF0 muzzle_flash (task): model + 2 a frame, gone after 0x48; drawn at its place, turned, scaled 5
template <class P> void Game<P>::muzzle_flash(uint32_t task)
{
    const uint32_t m = mem.u32(task + 0x54) + 2;
    mem.set32(task + 0x54, m);
    if (int32_t(m) > 0x48)
        task_exit();
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    uint32_t t[3] = {mem.u32(task + 0x20), mem.u32(task + 0x24), mem.u32(task + 0x28)};
    copro(0x12, t, 3, none);
    uint32_t a[1] = {mem.u32(task + 0x30)};
    copro(0x15, a, 1, none);
    uint32_t s[3] = {0x40A00000, 0x40A00000, 0x40A00000};
    copro(0x13, s, 3, none);
    geo_draw_model(mem.u32(task + 0x54));
    copro(0x06, nullptr, 0, none);
}

// 0x43330 caaat_piece_spawn(obj, x, y, z): a caaat_piece task (0x44D80, 0x68 bytes, model 0x6A PN_caaat) at the
// camera-space point (x, y, z) taken to the world by stored matrix 1 (coprocessor 0x74 1 / 0x1A), its velocity
// (0.1, 0, -0.1) turned by the object's heading +0x30 (0x10 / 0x15 / 0x1A): x into +0x58, z into +0x60; +0x5C rand %
// 5 / 100 + 0.35 up, +0x64 gravity -0.027222, heading the object's, the floor +0x3C its y. Read from the disassembly
// (ch4proof: the whole-game differential, Chapter 1's cat scene: the original's task was missing)
template <class P> void Game<P>::caaat_piece_spawn(uint32_t obj, uint32_t x, uint32_t y, uint32_t z)
{
    if constexpr (P::pc) {
        (void)obj, (void)x, (void)y, (void)z;   // the PC form: not ported
    } else {
        const uint32_t t = task_create(F::caaat_piece, 0x68);
        mem.set32(t + 0x54, 0x6A);
        Tgp::Out none, pos, vel;
        uint32_t slot[1] = {1};
        copro(0x74, slot, 1, none);
        uint32_t p[3] = {x, y, z};
        copro(0x1A, p, 3, pos);
        copro(0x10, nullptr, 0, none);
        uint32_t a[1] = {mem.u32(obj + 0x30)};
        copro(0x15, a, 1, none);
        uint32_t v[3] = {0x3DCCCCCDu, 0, 0xBDCCCCCDu};
        copro(0x1A, v, 3, vel);
        copro(0x06, nullptr, 0, none);
        const float up = pdiv(float(int32_t(rand()) % 5), 100.0f) + ffrom(0x3EB33333u);
        mem.set32(t + 0x5C, fbits(up));
        mem.set32(t + 0x64, 0xBCDF0123u);
        mem.set32(t + 0x20, pos.w[0]);
        mem.set32(t + 0x24, pos.w[1]);
        mem.set32(t + 0x28, pos.w[2]);
        mem.set32(t + 0x58, vel.w[0]);
        mem.set32(t + 0x60, vel.w[2]);
        mem.set32(t + 0x30, mem.u32(obj + 0x30));
        mem.set32(t + 0x3C, mem.u32(obj + 0x24));
    }
}

// 0x44D80 caaat_piece (task): at or below its floor (+0x3C) it ends; else +0x5C += +0x64, the place moves by
// (+0x58, +0x5C, +0x60), turned 0x1000 more on x (+0x2C) and drawn (model +0x54) at the place with heading +0x30
template <class P> void Game<P>::caaat_piece(uint32_t t)
{
    if (!(mem.f32(t + 0x24) > mem.f32(t + 0x3C))) {
        task_exit();
    } else {
        const float vy = mem.f32(t + 0x5C) + mem.f32(t + 0x64);
        mem.set32(t + 0x5C, fbits(vy));
        mem.set32(t + 0x20, fbits(mem.f32(t + 0x20) + mem.f32(t + 0x58)));
        mem.set32(t + 0x28, fbits(mem.f32(t + 0x28) + mem.f32(t + 0x60)));
        mem.set32(t + 0x24, fbits(mem.f32(t + 0x24) + vy));
    }
    Tgp::Out none;
    copro(0x05, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(t + 0x20), mem.u32(t + 0x24), mem.u32(t + 0x28)};
    copro(0x12, p, 3, none);
    uint32_t a[1] = {mem.u32(t + 0x30)};
    copro(0x15, a, 1, none);
    const uint32_t rx = mem.u32(t + 0x2C) + 0x1000;
    mem.set32(t + 0x2C, rx);
    uint32_t b[1] = {rx};
    copro(0x14, b, 1, none);
    geo_draw_model(mem.u32(t + 0x54));
    copro(0x06, nullptr, 0, none);
}

// 0x9CCD0 magazine_drop (task): the dropped magazine (model 0x3F4) of a cut-scene reload: falls (+0x7C, gravity
// -0.027222), drifts by (0, 0, +0x78) turned by its heading +0x30 (coprocessor 0x21 / 0x15 / 0x1A: x and z), drawn
// at its place with headings y / x / z (+0x30 / +0x2C / +0x34); ends below floor_y or after 120 frames (+0x50)
template <class P> void Game<P>::magazine_drop(uint32_t t)
{
    const float v = mem.f32(t + 0x7C);
    mem.set32(t + 0x24, fbits(mem.f32(t + 0x24) + v));
    mem.set32(t + 0x7C, fbits(v + ffrom(0xBCDF0123u)));
    Tgp::Out none, d;
    copro(0x21, nullptr, 0, none);
    uint32_t a[1] = {mem.u32(t + 0x30)};
    copro(0x15, a, 1, none);
    uint32_t dv[3] = {0, 0, mem.u32(t + 0x78)};
    copro(0x1A, dv, 3, d);
    copro(0x06, nullptr, 0, none);
    mem.set32(t + 0x20, fbits(mem.f32(t + 0x20) + ffrom(d.w[0])));
    mem.set32(t + 0x28, fbits(mem.f32(t + 0x28) + ffrom(d.w[2])));
    copro(0x05, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(t + 0x20), mem.u32(t + 0x24), mem.u32(t + 0x28)};
    copro(0x12, p, 3, none);
    copro(0x15, a, 1, none);
    uint32_t rx[1] = {mem.u32(t + 0x2C)};
    copro(0x14, rx, 1, none);
    uint32_t rz[1] = {mem.u32(t + 0x34)};
    copro(0x16, rz, 1, none);
    geo_draw_model(0x3F4);
    copro(0x06, nullptr, 0, none);
    const bool below = mem.f32(t + 0x24) < mem.f32(G::floor_y);
    const int32_t n = int32_t(mem.u32(t + 0x50)) + 1;
    mem.set32(t + 0x50, uint32_t(n));
    if (below || n > 0x77)
        task_exit();
}

// 0x83030 hit_spark (task): model + 1 a frame up to +0x328; at its place with table 0xAA690's orientation, x 0xC000,
// scale 2, then moved along its view ray to a depth of at least 10 (z + +0x33C; coprocessor 0x20 / 0x88)
template <class P> void Game<P>::hit_spark(uint32_t t)
{
    const uint32_t m = mem.u32(t + 0x54) + 1;
    mem.set32(t + 0x54, m);
    if (m == mem.u32(t + 0x328))
        task_exit();
    Tgp::Out none, tr;
    copro(0x05, nullptr, 0, none);
    uint32_t p[3] = {mem.u32(t + 0x20), mem.u32(t + 0x24), mem.u32(t + 0x28)};
    copro(0x12, p, 3, none);
    uint32_t r[9];
    for (uint32_t k = 0; k < 9; ++k)
        r[k] = mem.u32(0xAA690 + 4 * k);
    copro(0x87, r, 9, none);
    uint32_t rx[1] = {0xC000};
    copro(0x14, rx, 1, none);
    uint32_t s[3] = {fbits(2.0f), fbits(2.0f), fbits(2.0f)};
    copro(0x13, s, 3, none);
    copro(0x20, nullptr, 0, tr);
    const float x = ffrom(tr.w[0]), y = ffrom(tr.w[1]), z = ffrom(tr.w[2]);
    float d = z + mem.f32(t + 0x33C);
    if (!(d >= 10.0f))
        d = 10.0f;
    uint32_t n[3] = {fbits((x * d) / z), fbits((y * d) / z), fbits(d)};
    copro(0x88, n, 3, none);
    geo_draw_model(mem.u32(t + 0x54));
    copro(0x06, nullptr, 0, none);
}

} // namespace hotd::port
