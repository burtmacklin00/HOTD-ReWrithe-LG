// Shooting, A behaviour: the gun input of a frame and the shot ray (player_shoot, shot_ray_setup), read from the
// disassembly (Ghidra's C loses the coprocessor FIFO traffic and some byte accesses).
#include "port/game.h"

#include <climits>
#include <cmath>
#include <cstring>

namespace hotd::port {


// 0x5A80 (player task): the gun's input for this frame by input source (0 live: the trigger counts from flash phase 2
// on when gun_shot is set; 1 recorded: the mode-8 stream or the attract demo stream; 2 none), then at phase 2 with a
// trigger, the gun on screen and rounds left: a shot (round counted, ray built with focal 496, shot effect and gunshot
// sound unless gun_test_mode), or with shot_enable clear only skip_request; the trigger off screen reloads.
// gun_flash_phase and skip_request are byte accesses (the phase byte is written back on every call).
template <class P> void Game<P>::player_shoot(uint32_t task)
{
    if constexpr (P::pc) {
        player_shoot_pc(task);
        return;
    } else {
    const int8_t p = mem.at<PlayerTask>(task).player;
    Player& pl = mem.at<Player>(player_rec(p));   // 4P: seats 2 / 3 in the 4P block (port/coop4.h)
    GunInput& gun = mem.at<GunInput>(gun_input_rec(p));
    const int32_t source = int32_t(mem.u32(G::input_source));
    uint8_t phase = mem.at<uint8_t>(G::gun_flash_phase);
    pl.flags &= ~2u;
    if (source == 0) {
        if (int32_t(uint32_t(phase) << 24) <= 0x1000000)
            gun.trigger = 0;
        else if (mem.at<uint8_t>(gun_shot_at(p)) != 0)
            gun.trigger = 1;
    } else if (source == 1) {
        if (mem.at<int16_t>(G::game_mode) == 8) {
            pl.rounds = 6;
            pl.on_screen = 1;
            mem.set32(G::shot_enable, 1);
            int16_t ax = pl.aim_recorded[0], ay = pl.aim_recorded[1];
            phase = 2;
            gun.trigger = uint8_t(pl.pressed & 1);
            pl.aim[0] = ax;
            pl.aim[1] = ay;
        } else {
            uint8_t trig = pl.demo_trigger, on = pl.demo_on_screen;
            int16_t ax = pl.demo_aim[0], ay = pl.demo_aim[1];
            phase = pl.demo_gun_mode;
            gun.trigger = trig;
            pl.on_screen = on;
            pl.aim[0] = ax;
            pl.aim[1] = ay;
            mem.set32(G::shot_enable, uint32_t(int32_t(int8_t(pl.demo_shot))));
        }
    } else {
        gun.trigger = 0;
        pl.on_screen = 0;
        pl.aim[0] = 0;
        pl.aim[1] = 0;
        phase = 0;
        mem.set32(G::shot_enable, 0);
    }
    mem.at<uint8_t>(G::gun_flash_phase) = phase;
    if (source == 0 && pm_on() && pm_auto_reload && pl.rounds == 0) {
        // PC Mode (THOTD.EXE pc_gun_input 0x4358CC, its start): Auto Reload on and no rounds: the reload timer started,
        // empty cleared, the character's rounds; no sound and no test of the game mode or shot_enable (docs/pcmode.md)
        pm_reload_at[p & 3] = pm_clock_ms != 0 ? pm_clock_ms : 1;
        pl.empty = 0;
        pl.rounds = pm_full_rounds(p);
    }
    if (source == 0 && pm_on() && pm_reload_at[p & 3] != 0) {
        // PC Mode (ReWrithe on the arcade logic; THOTD.EXE pc_gun_input 0x4363A2): the reload timer; the PC compares
        // the time since the reload with the character's dword +0x24, the rounds (not the reload ms +0x2C)
        if (pm_clock_ms - pm_reload_at[p & 3] > uint32_t(pm_table(pm_seat_char(p)).rounds))
            pm_reload_at[p & 3] = 0;
        else
            gun.trigger = 0;
    }
    if (phase == 2 && gun.trigger != 0 && pl.on_screen != 0 && pl.rounds != 0) {
        float x = float(int32_t(pl.aim[0])), y = float(int32_t(pl.aim[1]));
        if (mem.u32(G::ammo_enabled) != 0) {
            uint8_t r = uint8_t(pl.rounds - 1);
            pl.rounds = r;
            if (r == 0)
                pl.empty = 1;
        }
        if (mem.u32(G::shot_enable) != 0) {
            pl.flags |= 2;
            if (source == 0) ++lg_shots[p & 3];   // Light Gun Edition: the outputs' recoil (ours)
            shot_ray_setup(p, x, y, 496.0f);
            uint32_t test = mem.u32(G::gun_test_mode);
            gun.trigger = 0;
            pl.shot_pos[0] = int16_t(int32_t(x));
            pl.shot_pos[1] = int16_t(int32_t(y));
            if (test == 0) {
                shot_effect(task);
                sound_cue((p & 1) == 0 ? 0xA01208 : 0xA01207);
            }
            return;
        }
        mem.at<uint8_t>(G::skip_request) = 1;
        if (mem.u32(G::ammo_enabled) != 0)
            pl.rounds = uint8_t(pl.rounds + 1);
        return;
    }
    if (phase == 2 && gun.trigger != 0 && pl.on_screen == 0) {
        uint32_t enable = mem.u32(G::shot_enable);
        pl.rounds = pm_full_rounds(p);   // 6; PC Mode: the character's (THOTD.EXE player_shoot 0x4365C4)
        if (pm_on())
            pm_reload_at[p & 3] = pm_clock_ms != 0 ? pm_clock_ms : 1;   // (the PC's timeGetTime is never 0)
        pl.empty = 0;
        if (enable != 0) {
            if (mem.u32(G::gun_test_mode) == 0 && mem.at<int16_t>(G::game_mode) != 3)
                sound_cue(0xA01200);
            return;
        }
        mem.at<uint8_t>(G::skip_request) = 1;
    }
    }
}

// PC 0x4365C4, the PC form of player_shoot (the developers' one routine; written apart because almost every line
// differs): input source 0 = pc_gun_input (mouse / keyboard into the gun record), 1 = the mode-8 stream or the PC's
// attract demo stream (its own layout: Player +0x56 trigger, +0x57 reload, +0x58 shot enable, +0x59 phase, +0x5A on
// screen, +0x5C aim), else none. In the attract, a hit of the last shot (pc_shot_hit) plays the shot effect and sound
// 0x1E. A shot needs only the trigger and a round (no flash phase); the aim is the gun record's; rounds are not used up
// while pc_ammo_hold is set; sounds 0x1E / 0x1D per player, none in the attract. A reload (the gun's reload request)
// refills to 6, in PC Mode to the character's rounds (+0x24) and starts the reload timer (timeGetTime).
template <class P> void Game<P>::player_shoot_pc(uint32_t task)
{
    if constexpr (P::pc) {
    const int8_t p = mem.at<PlayerTask>(task).player;
    const uint32_t pa = G::players + uint32_t(int32_t(p)) * 0xCC;
    Player& pl = mem.at<Player>(pa);
    GunInput& gun = mem.at<GunInput>(G::gun_inputs, uint32_t(int32_t(p)));
    pl.flags &= ~2u;
    const uint32_t source = mem.u32(G::input_source);
    if (source == 0) {
        pc_gun_input(pa - 4, G::gun_inputs + uint32_t(int32_t(p)) * 0x1C);
    } else if (source == 1) {
        if (mem.at<int16_t>(G::game_mode) == 8) {
            pl.rounds = 6;
            gun.trigger = (pl.pressed & 1) != 0;
            pl.on_screen = 1;
            mem.set32(G::shot_enable, 1);
            gun.aim[0] = pl.aim_recorded[0];
            mem.at<uint8_t>(G::gun_flash_phase) = 2;
            gun.aim[1] = pl.aim_recorded[1];
        } else {
            gun.trigger = mem.at<uint8_t>(pa + 0x56);
            gun.reload = mem.at<uint8_t>(pa + 0x57);
            pl.on_screen = mem.at<uint8_t>(pa + 0x5A);
            gun.aim[0] = mem.at<int16_t>(pa + 0x5C);
            gun.aim[1] = mem.at<int16_t>(pa + 0x5E);
            mem.set32(G::shot_enable, uint32_t(int32_t(mem.at<int8_t>(pa + 0x58))));
            mem.at<uint8_t>(G::gun_flash_phase) = mem.at<uint8_t>(pa + 0x59);
        }
    } else {
        gun.trigger = 0;
        pl.on_screen = 0;
        mem.set32(G::shot_enable, 0);
        gun.aim[0] = 0;
        mem.at<uint8_t>(G::gun_flash_phase) = 0;
        gun.aim[1] = 0;
    }
    if (mem.at<int32_t>(G::frontend_state) == 6 && mem.u32(G::pc_shot_hit) != 0) {
        shot_effect(task);
        mem.set32(G::pc_shot_hit, 0);
        sound_cue(0x1E);
    }
    if (gun.trigger != 0 && pl.rounds != 0) {
        const float x = float(gun.aim[0]), y = float(gun.aim[1]);
        if (mem.u32(G::ammo_enabled) != 0) {
            if (mem.u32(G::pc_ammo_hold) == 0)
                pl.rounds = uint8_t(pl.rounds - 1);
            if (pl.rounds == 0)
                pl.empty = 1;
        }
        if (mem.u32(G::shot_enable) != 0) {
            pl.flags |= 2;
            if (source == 0) ++lg_shots[p & 3];   // Light Gun Edition: the outputs' recoil (ours)
            shot_ray_setup(p, x, y, 496.0f);
            gun.trigger = 0;
            pl.shot_pos[0] = int16_t(int32_t(x));
            pl.shot_pos[1] = int16_t(int32_t(y));
            if (mem.at<int32_t>(G::frontend_state) != 6) {
                shot_effect(task);
                sound_cue(p == 0 ? 0x1E : 0x1D);
            }
            return;
        }
        mem.at<uint8_t>(G::skip_request) = 1;
        if (mem.u32(G::ammo_enabled) != 0)
            pl.rounds = uint8_t(pl.rounds + 1);
        return;
    }
    if (gun.reload == 0)
        return;
    gun.trigger = 0;
    gun.reload = 0;
    if (mem.at<int32_t>(G::frontend_state) == 5) {
        const int32_t ch = mem.at<int32_t>(G::pc_mode_chars + uint32_t(int32_t(p)) * 4);
        pl.rounds = mem.at<uint8_t>(G::pc_characters + 0x24 + uint32_t(ch) * 0x34);
        mem.set32(G::pc_reload_time + uint32_t(int32_t(p)) * 4, pc_time_ms());
    } else {
        pl.rounds = 6;
    }
    pl.empty = 0;
    if (mem.u32(G::shot_enable) != 0) {
        if (mem.u32(G::gun_test_mode) == 0 && mem.at<int16_t>(G::game_mode) != 3)
            sound_cue(0x1C);
        return;
    }
    mem.at<uint8_t>(G::skip_request) = 1;
    }
}

// 0x32CF0 (player, aim x, aim y, focal): the player's shot ray. cam_dir = 0x0C normalize(x, y, focal), cam_origin = 0;
// (a0, a1) = 0x0F(cam_dir) fixed; with stored matrix 1 (0x74, pushed): origin = 0x1A(cam_origin), translation set to
// 0 (0x88), dir = 0x1A(cam_dir); pop; (b0, b1) = 0x0F(dir) fixed; aim stored; cam_ang_neg = (-a1, -a0), cam_trig =
// 0x1B / 0x1C of -a1, then of -a0; world_ang_neg = (-b1, -b0), world_trig likewise.
template <class P> void Game<P>::shot_ray_setup(int player, float x, float y, float focal)
{
    ShotRay& r = mem.at<ShotRay>(shot_ray_rec(player));
    if constexpr (P::pc) {
        // PC: x87 maths in float32, its table square root, pc_vec_angles for the angles, the software matrix stack
        // with stored matrix 1 (the camera), the 4096-entry sin / cos table
        const float inv = 1.0f / pc_fast_sqrt2((x * x + y * y) + focal * focal);
        r.cam_dir[0] = x * inv;
        r.cam_dir[1] = y * inv;
        r.cam_dir[2] = focal * inv;
        r.cam_origin[0] = 0.0f;
        r.cam_origin[1] = 0.0f;
        r.cam_origin[2] = 0.0f;
        uint32_t a0 = 0, a1 = 0;
        pc_vec_angles(r.cam_dir[0], r.cam_dir[1], r.cam_dir[2], a0, a1);
        pc_matrix_push();
        pcm_load_camera();
        pc_matrix_transform(r.cam_origin[0], r.cam_origin[1], r.cam_origin[2], r.origin[0], r.origin[1], r.origin[2]);
        pcm_set(9, 0.0f);
        pcm_set(10, 0.0f);
        pcm_set(11, 0.0f);
        pc_matrix_transform(r.cam_dir[0], r.cam_dir[1], r.cam_dir[2], r.dir[0], r.dir[1], r.dir[2]);
        pcm_pop();
        uint32_t b0 = 0, b1 = 0;
        pc_vec_angles(r.dir[0], r.dir[1], r.dir[2], b0, b1);
        r.aim[0] = x;
        r.aim[1] = y;
        // the table index: the 16-bit angle / 16, truncated toward zero, & 0xFFF; (sin, cos) pairs
        auto trig = [&](uint32_t angle, int k) {
            const int32_t i = (int32_t(int16_t(angle & 0xFFFF)) / 16) & 0xFFF;
            return mem.f32(G::pc_trig_table + uint32_t(i) * 8 + uint32_t(k) * 4);
        };
        r.cam_ang_neg[0] = int32_t(0 - a1);
        r.cam_ang_neg[1] = int32_t(0 - a0);
        r.cam_trig[0] = trig(0 - a1, 0);
        r.cam_trig[1] = trig(0 - a1, 1);
        r.cam_trig[2] = trig(0 - a0, 0);
        r.cam_trig[3] = trig(0 - a0, 1);
        r.world_ang_neg[0] = int32_t(0 - b1);
        r.world_ang_neg[1] = int32_t(0 - b0);
        r.world_trig[0] = trig(0 - b1, 0);
        r.world_trig[1] = trig(0 - b1, 1);
        r.world_trig[2] = trig(0 - b0, 0);
        r.world_trig[3] = trig(0 - b0, 1);
        return;
    } else if (pc_path()) {
        // PC Mode (docs/pcmode.md row 10): the PC's branch above on plain values (port/pm_maths.h), with the PC's own
        // camera matrix (its stored matrix 1, built by camera_matrices_update in PM: pm_camera)
        pm::shot_ray(x, y, focal, pm_camera, r);
        return;
    }
    Tgp::Out o, none;
    uint32_t in[3] = {fbits(x), fbits(y), fbits(focal)};
    copro(0x0C, in, 3, o);
    r.cam_dir[0] = ffrom(o.w[0]);
    r.cam_dir[1] = ffrom(o.w[1]);
    const float n2 = ffrom(o.w[2]);
    r.cam_origin[2] = 0.0f;
    r.cam_origin[0] = 0.0f;
    r.cam_origin[1] = 0.0f;
    r.cam_dir[2] = n2;
    in[0] = fbits(r.cam_dir[0]);
    in[1] = fbits(r.cam_dir[1]);
    in[2] = fbits(n2);
    copro(0x0F, in, 3, o);
    uint32_t a0 = o.w[0], a1 = o.w[1];
    fixAngles(a0, a1);
    in[0] = 1;
    copro(0x74, in, 1, none);
    in[0] = fbits(r.cam_origin[0]);
    in[1] = fbits(r.cam_origin[1]);
    in[2] = fbits(r.cam_origin[2]);
    copro(0x1A, in, 3, o);
    r.origin[0] = ffrom(o.w[0]);
    r.origin[1] = ffrom(o.w[1]);
    r.origin[2] = ffrom(o.w[2]);
    in[0] = in[1] = in[2] = 0;
    copro(0x88, in, 3, none);
    in[0] = fbits(r.cam_dir[0]);
    in[1] = fbits(r.cam_dir[1]);
    in[2] = fbits(r.cam_dir[2]);
    copro(0x1A, in, 3, o);
    r.dir[0] = ffrom(o.w[0]);
    r.dir[1] = ffrom(o.w[1]);
    const float d2 = ffrom(o.w[2]);
    copro(0x06, nullptr, 0, none);
    r.dir[2] = d2;
    in[0] = fbits(r.dir[0]);
    in[1] = fbits(r.dir[1]);
    in[2] = fbits(r.dir[2]);
    copro(0x0F, in, 3, o);
    uint32_t b0 = o.w[0], b1 = o.w[1];
    fixAngles(b0, b1);
    r.aim[0] = x;
    r.aim[1] = y;
    r.cam_ang_neg[0] = int32_t(0 - a1);
    r.cam_ang_neg[1] = int32_t(0 - a0);
    auto trig = [&](int n, uint32_t angle) {
        uint32_t w[1] = {angle};
        copro(n, w, 1, o);
        return ffrom(o.w[0]);
    };
    r.cam_trig[0] = trig(0x1B, 0 - a1);
    r.cam_trig[1] = trig(0x1C, 0 - a1);
    r.cam_trig[2] = trig(0x1B, 0 - a0);
    r.cam_trig[3] = trig(0x1C, 0 - a0);
    r.world_ang_neg[0] = int32_t(0 - b1);
    r.world_ang_neg[1] = int32_t(0 - b0);
    r.world_trig[0] = trig(0x1B, 0 - b1);
    r.world_trig[1] = trig(0x1C, 0 - b1);
    r.world_trig[2] = trig(0x1B, 0 - b0);
    r.world_trig[3] = trig(0x1C, 0 - b0);
}

// x87 fistp of a value already truncated (pc_float_chop): out of the int32 range (or NaN) the "integer indefinite"
static int32_t pc_fistp(double v)
{
    if (!(v > -2147483649.0 && v < 2147483648.0))
        return INT32_MIN;
    return int32_t(v);
}

// PC 0x568F10 (a, b): an angle in 1/4096 turns from two floats: both x 65536 truncated (X from a, Y from b), made
// positive (quadrant: 2 for a negative X, + 1 for a negative Y), halved together until both are at most 2^20; Y = 0:
// 0x400; X <= Y: pc_atan_table[(X x 1024 + Y / 2) / Y], else 0x400 - pc_atan_table[(Y x 1024 + X / 2) / X]; then by
// quadrant r, 0x800 - r, -r, r - 0x800.
template <class P> int32_t Game<P>::pc_atan2(float a, float b)
{
    if constexpr (!P::pc)
        return pm::atan2(a, b);   // PC Mode on the arcade logic: the rebuilt table
    else {
        int32_t X = pc_fistp(std::trunc(double(a) * 65536.0)), Y = pc_fistp(std::trunc(double(b) * 65536.0));
        int q = 0;
        if (X < 0) {
            X = int32_t(0u - uint32_t(X));
            q = 2;
        }
        if (Y < 0) {
            Y = int32_t(0u - uint32_t(Y));
            ++q;
        }
        while (0x100000 < Y || 0x100000 < X) {
            X >>= 1;
            Y >>= 1;
        }
        int32_t r;
        if (Y == 0)
            r = 0x400;
        else if (X <= Y)
            r = mem.at<int32_t>(G::pc_atan_table + uint32_t((int64_t(X) * 1024 + Y / 2) / Y) * 4);
        else
            r = 0x400 - mem.at<int32_t>(G::pc_atan_table + uint32_t((int64_t(Y) * 1024 + X / 2) / X) * 4);
        switch (q) {
        case 0: return r;
        case 1: return 0x800 - r;
        case 2: return -r;
        default: return r - 0x800;
        }
    }
}

// PC 0x412D28 (x, y, z, &yaw, &pitch): pitch = -(pc_atan2(-y, pc_fast_sqrt2(z z + x x)) << 4), yaw = -(pc_atan2(x, z)
// << 4), then the pitch fold (pitch leaving -0x4000..0x3FFF: 0x8000 - pitch, yaw + 0x8000)
template <class P> void Game<P>::pc_vec_angles(float x, float y, float z, uint32_t& yaw, uint32_t& pitch)
{
    const float h = pc_fast_sqrt2(z * z + x * x);
    pitch = 0u - (uint32_t(pc_atan2(-y, h)) << 4);
    yaw = 0u - (uint32_t(pc_atan2(x, z)) << 4);
    fixAngles(yaw, pitch);
}

// PC 0x569600: the software matrix stack: the current matrix (12 floats) copied one slot up, which becomes current
template <class P> void Game<P>::pc_matrix_push()
{
    if constexpr (P::pc) {
        const uint32_t m = mem.u32(G::pc_matrix_ptr);
        mem.set32(G::pc_matrix_ptr, m + 0x30);
        for (uint32_t i = 0; i < 12; ++i)
            mem.set32(m + 0x30 + 4 * i, mem.u32(m + 4 * i));
    } else {   // PC Mode on the arcade logic: the runtime's own stack (Game::pm_mstack)
        pm_mstack[(pm_msp + 1) & 31] = pm_cur();
        ++pm_msp;
    }
}

// PC 0x412C40 (x, y, z, &ox, &oy, &oz): the point through the current matrix m: ox = (m1 y + m0 x) + m2 z + m9, oy =
// (m3 x + m4 y) + m5 z + m10, oz = (m7 y + m6 x) + m8 z + m11 (x87 order)
template <class P> void Game<P>::pc_matrix_transform(float x, float y, float z, float& ox, float& oy, float& oz)
{
    if constexpr (P::pc) {
        const uint32_t m = mem.u32(G::pc_matrix_ptr);
        auto e = [&](int i) { return mem.f32(m + uint32_t(i) * 4); };
        ox = ((e(1) * y + e(0) * x) + e(2) * z) + e(9);
        oy = ((e(3) * x + e(4) * y) + e(5) * z) + e(10);
        oz = ((e(7) * y + e(6) * x) + e(8) * z) + e(11);
    } else {
        pm_cur().transform(x, y, z, ox, oy, oz);
    }
}

// boundaries: 0x95270 the gun's shot effect task, 0x154D0 a sound cue to the sound board's queue; not ported yet

} // namespace hotd::port
