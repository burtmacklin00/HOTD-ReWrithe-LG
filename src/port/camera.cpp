// The camera, A behaviour: the camera task (chapter task 1) and its CCTB job ring, the camera jobs (CCTB 16-21, 32, 33,
// 64), the camera modes (CAMDR 1-3, CCTB 33 4-7) with their per-frame routines, the game camera at the stops
// (game_cam_*), camera path evaluation and the view matrices; arcade 0x2ABD0..0x2DA20 and 0x6CA60..0x6D0B0, read from
// the disassembly (Ghidra's C loses the coprocessor FIFO traffic and the register reuse).
//
// Camera records: 6 x 0x154 bytes from 0x51EAB0 (camera_records; the view matrices of record n sit 0x60 bytes before
// it, camera_matrices). Record offsets: +0 eye, +0xC pitch / yaw / roll, +0x18 look-at point, +0x4C job routine,
// +0x80 / +0x84 / +0x8C the path job's current frame / end frame / flags, +0xC0.. the CCTB operands (64: start, end,
// path, flags), +0xD0..+0xEB the preset words (CCTB 19, SCNINIT), +0xEC the frame (float), +0xF0 a look mode
// (game_cam_look_at_attackers). A job reports done through evt_state = 1 (slot 0; evt_camjobs - 1) or by putting nop
// in its slot; camera_run_jobs then takes the next CCTB from the ring (evt_cctb_ring, 16 entries).
#include "port/game.h"
#include <cmath>

namespace hotd::port {

template <class P> Tgp::Out Game<P>::cam_copro(int n, std::initializer_list<uint32_t> in)
{
    Tgp::Out o;
    uint32_t w[16] = {};
    int k = 0;
    for (uint32_t v : in)
        w[k++] = v;
    copro(n, w, k, o);
    return o;
}

// the yaw (w0) and pitch (w1) of a direction: coprocessor 0x0F; PC Mode / Boss Mode: the camera file's pc_vec_angles
// copy 0x45D300 (folded already: the callers' fold then leaves it as it is; pmbody)
template <class P> Tgp::Out Game<P>::cam_angles(float x, float y, float z)
{
    if (pc_path()) {
        Tgp::Out o;
        uint32_t yaw = 0, pitch = 0;
        pm::vec_angles(x, y, z, yaw, pitch);
        o.put(yaw);
        o.put(pitch);
        return o;
    }
    return cam_copro(0x0F, {fbits(x), fbits(y), fbits(z)});
}

// slot 0: evt_state = 1 and one camera job less (WAIT_CAMJOB); other slots: the slot's job becomes nop
template <class P> void Game<P>::cam_job_end(int slot)
{
    if (slot == 0) {
        const uint32_t jobs = mem.u32(G::evt_camjobs);
        mem.set32(G::evt_state, 1);
        mem.set32(G::evt_camjobs, jobs - 1);
    } else {
        mem.set32(cam_rec(slot) + 0x4C, F::nop);
    }
}

// a call through the camera's tables (main jobs, slot jobs, CCTB job types, camera setups and modes, the game camera's
// sub-states): the ported routines directly, the rest as boundaries
template <class P> void Game<P>::camera_call(uint32_t routine, uint32_t arg)
{
    const int slot = int(arg);
    switch (routine) {
    case F::nop: return;
    case F::camera_run_jobs_follow: camera_run_jobs_follow(); return;
    case F::camera_run_jobs: camera_run_jobs(); return;
    case F::camera_frame_mode4: camera_frame_mode4(); return;
    case F::camera_frame_mode6: camera_frame_mode6(); return;
    case F::camera_frame_mode7: camera_frame_mode7(); return;
    case F::camera_frame_mode7_look: camera_frame_mode7_look(); return;
    case F::game_cam_stop_update: game_cam_stop_update(); return;
    case F::game_cam_nop: game_cam_nop(); return;
    case F::game_cam_clear: game_cam_clear(); return;
    case F::game_cam_clear_turn_back: game_cam_clear_turn_back(); return;
    case F::game_cam_clear_now: game_cam_clear_now(); return;
    case F::game_cam_look_at_attackers: game_cam_look_at_attackers(); return;
    case F::game_cam_idle: game_cam_idle(); return;
    case F::game_cam_turn_to_point: game_cam_turn_to_point(); return;
    case F::cam_slot_overhead: cam_slot_overhead(slot); return;
    case F::cam_job16_player_flag: cam_job16_player_flag(slot); return;
    case F::cam_job17_resetup: cam_job17_resetup(slot); return;
    case F::cam_job18_player_scene: cam_job18_player_scene(slot); return;
    case F::cam_job19_preset: cam_job19_preset(slot); return;
    case F::cam_job20: cam_job20(slot); return;
    case F::cam_job20_wait: cam_job20_wait(slot); return;
    case F::cam_job20_nop: cam_job20_nop(slot); return;
    case F::cam_job21_set: cam_job21_set(slot); return;
    case F::cam_job32_offset: cam_job32_offset(slot); return;
    case F::cam_job33_mode: cam_job33_mode(slot); return;
    case F::cam_job64_path: cam_job64_path(slot); return;
    case F::cam_job64_play: cam_job64_play(slot); return;
    case F::camera_mode_routine_clear: camera_mode_routine_clear(); return;
    case F::camdr_mode1: camdr_mode1(); return;
    case F::camdr_mode2: camdr_mode2(); return;
    case F::camdr_mode3: camdr_mode3(); return;
    case F::cctb33_mode4: cctb33_mode4(); return;
    case F::cctb33_mode5: cctb33_mode5(); return;
    case F::cctb33_mode6: cctb33_mode6(); return;
    case F::cctb33_mode7: cctb33_mode7(); return;
    case F::ChgCamMode_undefined: ChgCamMode_undefined(); return;
    case F::camdr_mode1_frame: camdr_mode1_frame(); return;
    case F::camdr_mode3_frame: camdr_mode3_frame(); return;
    case F::cctb33_mode4_start: cctb33_mode4_start(); return;
    case F::cctb33_mode4_idle: cctb33_mode4_idle(); return;
    case F::cctb33_mode5_start: cctb33_mode5_start(); return;
    case F::cctb33_mode5_frame: cctb33_mode5_frame(); return;
    case F::cctb33_mode6_start: cctb33_mode6_start(); return;
    case F::cctb33_mode6_walk: cctb33_mode6_walk(); return;
    case F::cctb33_mode7_start: cctb33_mode7_start(); return;
    case F::cctb33_mode7_walk: cctb33_mode7_walk(); return;
    default: call_routine(routine, arg); return;
    }
}

// 0x2ABD0: the view matrices of the 6 camera records, through the coprocessor: reset the matrix stack, stored matrix 1
// copied to 2; per record: identity, translate by the eye, rotate Y (yaw +0x10), X (pitch +0xC), Z (roll +0x14), read
// into matrices +0x30; then the inverse (identity, Z / X / Y by the negated angles, translate by the negated eye) into
// matrices +0; record 0's two also go to stored matrices 6 / 5. Then the camera_view record's view into stored 1 and
// its inverse into stored 0 (the camera); the four light directions; the first to the geometrizer; the update count;
// the shadow at floor_y; camera_drive_mode / camera_stopped from camera_mode / camera_kind; camera_status bit 0.
template <class P> void Game<P>::camera_matrices_update()
{
    cam_copro(0x08, {});
    cam_copro(0x73, {1});
    cam_copro(0x75, {2});
    for (uint32_t k = 0; k < 6; ++k) {
        const uint32_t b = G::camera_matrices + k * 0x154, r = b + 0x60;
        cam_copro(0x10, {});
        cam_copro(0x12, {mem.u32(r), mem.u32(r + 4), mem.u32(r + 8)});
        cam_copro(0x15, {mem.u32(r + 0x10)});
        cam_copro(0x14, {mem.u32(r + 0xC)});
        cam_copro(0x16, {mem.u32(r + 0x14)});
        Tgp::Out o = cam_copro(0x11, {});
        for (uint32_t i = 0; i < 12; ++i)
            mem.set32(b + 0x30 + 4 * i, o.w[i]);
        if (k == 0)
            cam_copro(0x75, {6});
        cam_copro(0x10, {});
        cam_copro(0x16, {0u - mem.u32(r + 0x14)});
        cam_copro(0x14, {0u - mem.u32(r + 0xC)});
        cam_copro(0x15, {0u - mem.u32(r + 0x10)});
        cam_copro(0x12, {mem.u32(r) ^ 0x80000000u, mem.u32(r + 4) ^ 0x80000000u, mem.u32(r + 8) ^ 0x80000000u});
        o = cam_copro(0x11, {});
        for (uint32_t i = 0; i < 12; ++i)
            mem.set32(b + 4 * i, o.w[i]);
        if (k == 0)
            cam_copro(0x75, {5});
    }
    const uint32_t v = G::camera_matrices + mem.u32(G::camera_view) * 0x154;
    uint32_t m[12];
    for (uint32_t i = 0; i < 12; ++i)
        m[i] = mem.u32(v + 0x30 + 4 * i);
    Tgp::Out none;
    copro(0x07, m, 12, none);
    cam_copro(0x75, {1});
    for (uint32_t i = 0; i < 12; ++i)
        m[i] = mem.u32(v + 4 * i);
    copro(0x07, m, 12, none);
    cam_copro(0x75, {0});
    if (pc_path()) {
        // PC Mode on the arcade logic (docs/pcmode.md row 10): the PC's own camera matrix for the shot ray (its stored
        // matrix 1: the same record through the PC's table sin / cos and x87 order, THOTD.EXE 0x45F140); the arcade's
        // matrices above still draw the world
        const uint32_t r = v + 0x60;
        const float eye[3] = {mem.f32(r), mem.f32(r + 4), mem.f32(r + 8)};
        pm::camera_matrix(eye, mem.u32(r + 0xC), mem.u32(r + 0x10), mem.u32(r + 0x14), pm_camera);
        pm::camera_inverse(eye, mem.u32(r + 0xC), mem.u32(r + 0x10), mem.u32(r + 0x14), pm_camera_inv);
        const uint32_t r0 = G::camera_matrices + 0x60;   // record 0's inverse: the PC's stored matrix 5
        const float eye0[3] = {mem.f32(r0), mem.f32(r0 + 4), mem.f32(r0 + 8)};
        pm::camera_inverse(eye0, mem.u32(r0 + 0xC), mem.u32(r0 + 0x10), mem.u32(r0 + 0x14), pm_camera0_inv);
        pm::camera_matrix(eye0, mem.u32(r0 + 0xC), mem.u32(r0 + 0x10), mem.u32(r0 + 0x14), pm_camera0);   // stored 6
        pm_msp = 0;   // the PC leaves the inverse as its current matrix (the bottom of PM's stack)
        for (int i = 0; i < 12; ++i)
            pm_mstack[0].e[i] = pm_camera_inv[i];
    }
    matrix_identity_set(0xE000, 0xC000, G::camera_light_dirs);
    matrix_identity_set(0x4000, 0xC000, G::camera_light_dirs + 0xC);
    matrix_identity_set(0x4000, 0xC000, G::camera_light_dirs + 0x18);
    matrix_identity_set(0x2000, 0, G::camera_light_dirs + 0x24);
    geo_write(0x8000A0, 0xA0A);
    geo_write(0x804000, mem.u32(G::camera_light_dirs));
    geo_write(0x804000, mem.u32(G::camera_light_dirs + 4));
    geo_write(0x804000, mem.u32(G::camera_light_dirs + 8));
    const uint32_t count = mem.u32(G::camera_update_count + 8);
    mem.set32(G::camera_update_count, count);
    mem.set32(G::camera_update_count + 8, count + 1);
    shadow_setup(mem.f32(G::floor_y));
    const uint32_t kind = mem.u32(G::camera_kind);
    mem.set32(G::camera_drive_mode, mem.u32(G::camera_mode));
    mem.set32(G::camera_stopped, kind);
    uint32_t status = mem.u32(G::camera_status);
    if (kind == 1 && mem.at<uint16_t>(G::game_mode) != 3 && (mem.at<uint8_t>(G::camera_status) & 0x30) == 0)
        status &= ~1u;
    else
        status |= 1;
    mem.set32(G::camera_status, status);
}

// 0x2B040: camera_globals_reset, camera_stopped / camera_kind 1, drive mode / camera mode 1, camdr_mode1 (setups row 1
// entry 1), the CCTB ring and camera jobs cleared, evt_state 2, the slots' start jobs
template <class P> void Game<P>::camera_chapter_reset()
{
    camera_globals_reset();
    mem.set32(G::camera_stopped, 1);
    mem.set32(G::camera_kind, 1);
    mem.set32(G::camera_drive_mode, 1);
    mem.set32(G::camera_mode, 1);
    camera_call(mem.u32(G::camera_setups + 0x28), 0);
    mem.set32(G::evt_cctb_ring_pos, 0);
    mem.set32(G::evt_cctb_ring_read, 0);
    mem.set32(G::evt_camjobs, 0);
    mem.set32(G::evt_state, 2);
    for (int k = 0; k < 6; ++k)
        mem.set32(cam_rec(k) + 0x4C, mem.u32(G::camera_slot_jobs_init + uint32_t(k) * 4));
}

// 0x2B100 (chapter task 1), per frame: the main camera's routine by camera_stopped (camera_main_jobs), the jobs of
// slots 1-5, then the view matrices
template <class P> void Game<P>::camera_task(uint32_t task)
{
    camera_call(mem.u32(G::camera_main_jobs + mem.u32(G::camera_stopped) * 4), task);
    for (int k = 1; k < 6; ++k)
        camera_call(mem.u32(cam_rec(k) + 0x4C), uint32_t(k));
    camera_matrices_update();
}

// 0x2B180: slot 0's job; when it is done (evt_state 1 / 2) and the ring has a CCTB, the CCTB's job type starts with its
// operands (+0xC0); evt_state 1 runs the new job at once
template <class P> void Game<P>::cam_next_job()
{
    bool again;
    do {
        camera_call(mem.u32(cam_rec(0) + 0x4C), 0);
        const uint32_t rd = mem.u32(G::evt_cctb_ring_read);
        const int32_t st = int32_t(mem.u32(G::evt_state));
        again = false;
        if (rd == mem.u32(G::evt_cctb_ring_pos)) {
            if (st >= 1 && st <= 2)
                mem.set32(cam_rec(0) + 0x4C, F::nop);
        } else {
            if (st >= 1 && st <= 2) {
                const uint32_t ins = mem.u32(G::evt_cctb_ring + rd * 4);
                mem.set32(G::evt_cctb_ring_read, (rd + 1) & 15);
                mem.set32(cam_rec(0) + 0x4C, mem.u32(G::cam_job_types + mem.u32(ins + 4) * 4));
                for (uint32_t i = 0; i < 16; i += 4)
                    mem.set32(cam_rec(0) + 0xC0 + i, mem.u32(ins + 8 + i));
                again = st == 1;
            }
            mem.set32(G::evt_state, 0);
        }
    } while (again);
}

// 0x2B230: cam_next_job's loop, then camera_slot2_follow
template <class P> void Game<P>::camera_run_jobs_follow()
{
    bool again;
    do {
        camera_call(mem.u32(cam_rec(0) + 0x4C), 0);
        const uint32_t rd = mem.u32(G::evt_cctb_ring_read);
        const int32_t st = int32_t(mem.u32(G::evt_state));
        again = false;
        if (rd == mem.u32(G::evt_cctb_ring_pos)) {
            if (st >= 1 && st <= 2)
                mem.set32(cam_rec(0) + 0x4C, F::nop);
        } else {
            if (st >= 1 && st <= 2) {
                const uint32_t ins = mem.u32(G::evt_cctb_ring + rd * 4);
                mem.set32(G::evt_cctb_ring_read, (rd + 1) & 15);
                mem.set32(cam_rec(0) + 0x4C, mem.u32(G::cam_job_types + mem.u32(ins + 4) * 4));
                for (uint32_t i = 0; i < 16; i += 4)
                    mem.set32(cam_rec(0) + 0xC0 + i, mem.u32(ins + 8 + i));
                again = st == 1;
            }
            mem.set32(G::evt_state, 0);
        }
    } while (again);
    camera_slot2_follow();
}

// 0x2B2F0: the same loop without camera_slot2_follow
template <class P> void Game<P>::camera_run_jobs()
{
    bool again;
    do {
        camera_call(mem.u32(cam_rec(0) + 0x4C), 0);
        const uint32_t rd = mem.u32(G::evt_cctb_ring_read);
        const int32_t st = int32_t(mem.u32(G::evt_state));
        again = false;
        if (rd == mem.u32(G::evt_cctb_ring_pos)) {
            if (st >= 1 && st <= 2)
                mem.set32(cam_rec(0) + 0x4C, F::nop);
        } else {
            if (st >= 1 && st <= 2) {
                const uint32_t ins = mem.u32(G::evt_cctb_ring + rd * 4);
                mem.set32(G::evt_cctb_ring_read, (rd + 1) & 15);
                mem.set32(cam_rec(0) + 0x4C, mem.u32(G::cam_job_types + mem.u32(ins + 4) * 4));
                for (uint32_t i = 0; i < 16; i += 4)
                    mem.set32(cam_rec(0) + 0xC0 + i, mem.u32(ins + 8 + i));
                again = st == 1;
            }
            mem.set32(G::evt_state, 0);
        }
    } while (again);
}

// 0x2B3A0: chapter task 1 = camera_task (task size 0x20), then camera_chapter_reset's body
template <class P> void Game<P>::camera_task_start()
{
    task_create(F::camera_task, 0x20);
    camera_globals_reset();
    mem.set32(G::camera_stopped, 1);
    mem.set32(G::camera_kind, 1);
    mem.set32(G::camera_drive_mode, 1);
    mem.set32(G::camera_mode, 1);
    camera_call(mem.u32(G::camera_setups + 0x28), 0);
    mem.set32(G::evt_cctb_ring_pos, 0);
    mem.set32(G::evt_cctb_ring_read, 0);
    mem.set32(G::evt_camjobs, 0);
    mem.set32(G::evt_state, 2);
    for (int k = 0; k < 6; ++k)
        mem.set32(cam_rec(k) + 0x4C, mem.u32(G::camera_slot_jobs_init + uint32_t(k) * 4));
}

// 0x2B470: with drive mode 3, camera record 2 by camera_slot2_mode: 0 a copy of record 0 (roll = record 0's), 1 at
// (5, 3, 170) looking just below record 0's eye, 2 / 3 fixed views, 4 the copy with the yaw turned by gun x x -16
// (player 1, or the solo player), 5 its own look-at point and roll; above 5 nothing
template <class P> void Game<P>::camera_slot2_follow()
{
    if (mem.u32(G::camera_drive_mode) != 3)
        return;
    const uint32_t mode = mem.u32(G::camera_slot2_mode);
    if (mode > 5)
        return;
    const uint32_t r0 = cam_rec(0), r2 = cam_rec(2);
    auto copy = [&]() {
        for (uint32_t i = 0; i < 0x30; i += 4)
            mem.set32(r2 + i, mem.u32(r0 + i));
    };
    switch (mode) {
    case 0:
        copy();
        camera_look_at(r2, r2 + 0x18, mem.u32(r0 + 0x14));
        return;
    case 1: {
        const uint32_t ex = mem.u32(r0), ez = mem.u32(r0 + 8);
        const float ey = float(double(mem.f32(r0 + 4)) - 1.0);
        cam_setf(r2 + 8, 170.0f);
        cam_setf(r2, 5.0f);
        cam_setf(r2 + 4, 3.0f);
        mem.set32(r2 + 0x20, ez);
        mem.set32(r2 + 0x18, ex);
        cam_setf(r2 + 0x1C, ey);
        camera_look_at(r2, r2 + 0x18, 0);
        return;
    }
    case 2:
        cam_setf(r2 + 8, 90.0f);
        cam_setf(r2, 70.0f);
        cam_setf(r2 + 4, 2.0f);
        cam_setf(r2 + 0x20, 140.0f);
        cam_setf(r2 + 0x18, 58.0f);
        cam_setf(r2 + 0x1C, 10.0f);
        camera_look_at(r2, r2 + 0x18, 0);
        return;
    case 3:
        cam_setf(r2, 120.0f);
        cam_setf(r2 + 4, 20.0f);
        cam_setf(r2 + 8, 100.0f);
        cam_setf(r2 + 0x20, 70.0f);
        cam_setf(r2 + 0x18, 190.0f);
        cam_setf(r2 + 0x1C, 10.0f);
        camera_look_at(r2, r2 + 0x18, 0);
        return;
    case 4: {
        uint32_t gun = G::gun_inputs;
        if (mem.at<uint16_t>(G::players_in_game) == 1)
            gun += uint32_t(int32_t(mem.at<int8_t>(G::solo_player_is_p2))) * 0x1C;
        const float x = -float(int32_t(mem.at<int16_t>(gun))) * 16.0f;
        copy();
        const int32_t turn = cvtzril(x);
        camera_look_at(r2, r2 + 0x18, mem.u32(r0 + 0x14));
        mem.set32(r2 + 0x10, mem.u32(r2 + 0x10) + uint32_t(turn));
        return;
    }
    default:
        camera_look_at(r2, r2 + 0x18, mem.u32(r2 + 0x14));
        return;
    }
}

// 0x2B740 (CCTB 33 mode 4, per frame): after a camera mode change the stop is reset, the stop record's look-at point
// put 30 units ahead of the eye (coprocessor), camera_pos at the eye less the record's height +0xD8, heading = the
// yaw; then the job becomes game_cam_stop_update and runs
template <class P> void Game<P>::camera_frame_mode4()
{
    const uint32_t r0 = cam_rec(0);
    if (mem.u32(G::camera_mode_changed) != 0) {
        game_cam_stop_reset();
        const uint32_t ex = mem.u32(r0), ey = mem.u32(r0 + 4), ez = mem.u32(r0 + 8), yaw = mem.u32(r0 + 0x10);
        cam_copro(0x21, {});
        cam_copro(0x88, {ex, ey, ez});
        cam_copro(0x15, {yaw});
        cam_copro(0x14, {mem.u32(r0 + 0xC)});
        Tgp::Out o = cam_copro(0x1A, {0, 0, fbits(30.0f)});
        const float h = mem.f32(r0 + 0xD8);
        cam_copro(0x06, {});
        mem.set32(G::game_cam_stop_point + 0x18, o.w[0]);
        mem.set32(G::game_cam_stop_point + 0x1C, o.w[1]);
        mem.set32(G::game_cam_stop_point + 0x20, o.w[2]);
        mem.set32(G::camera_heading, yaw);
        mem.set32(G::camera_heading + 4, 0);
        mem.set32(G::camera_pos, ex);
        cam_setf(G::camera_pos + 4, ffrom(ey) - h);
        mem.set32(G::camera_pos + 8, ez);
        mem.set32(G::camera_pos + 0xC, 0);
    }
    mem.set32(r0 + 0x4C, F::game_cam_stop_update);
    game_cam_stop_update();
}

// 0x2B890 (mode 6, per frame): the same without camera_pos
template <class P> void Game<P>::camera_frame_mode6()
{
    const uint32_t r0 = cam_rec(0);
    if (mem.u32(G::camera_mode_changed) != 0) {
        game_cam_stop_reset();
        cam_copro(0x21, {});
        cam_copro(0x88, {mem.u32(r0), mem.u32(r0 + 4), mem.u32(r0 + 8)});
        cam_copro(0x15, {mem.u32(r0 + 0x10)});
        cam_copro(0x14, {mem.u32(r0 + 0xC)});
        Tgp::Out o = cam_copro(0x1A, {0, 0, fbits(30.0f)});
        cam_copro(0x06, {});
        mem.set32(G::game_cam_stop_point + 0x18, o.w[0]);
        mem.set32(G::game_cam_stop_point + 0x1C, o.w[1]);
        mem.set32(G::game_cam_stop_point + 0x20, o.w[2]);
    }
    mem.set32(r0 + 0x4C, F::game_cam_stop_update);
    game_cam_stop_update();
}

// 0x2B980: the game camera's sub-state: 3 (look at the attackers) while attack positions 0-3 are held or enemies of
// the stop's wait kind remain (kinds 0 / 1: enemies_alive, 2: enemy_count), else 2 (clear); kinds above 2 (or below
// 0) keep it; 6 while camera_hold_hostage is 1; 7 when a turn is requested or running. Not 2: the turning flags clear.
// Then the sub-state's routine, and camera_frame = the stop's frame.
template <class P> void Game<P>::game_cam_stop_update()
{
    bool held = false;
    for (uint32_t s = 0; s < 4; ++s)
        if (mem.at<uint8_t>(G::attack_slots + s * 0x14) != 0)
            held = true;
    const int32_t kind = mem.at<int8_t>(G::evt_cgmfnsh);
    uint32_t sub = mem.u32(G::camera_drive_flag);
    if (kind >= 0) {
        if (kind <= 1)
            sub = mem.at<uint16_t>(G::enemies_alive) != 0 || held ? 3 : 2;
        else if (kind == 2)
            sub = mem.at<uint16_t>(G::enemy_count) != 0 || held ? 3 : 2;
    }
    if (mem.u32(G::camera_hold_hostage) == 1)
        sub = 6;
    if (sub != 2) {
        mem.at<uint8_t>(G::enemy_left_route) = 0;
        mem.at<uint8_t>(G::game_cam_turning) = 0;
    }
    if (mem.at<uint8_t>(G::game_cam_turn_request) != 0 || mem.at<uint8_t>(G::game_cam_turn_active) != 0)
        sub = 7;
    const uint32_t routine = mem.u32(G::game_cam_substates + sub * 4);
    mem.set32(G::camera_drive_flag, sub);
    camera_call(routine, 0);
    mem.set32(G::camera_frame, mem.u32(G::game_cam_stop_frame));
}

// 0x2BAA0 (sub-states 0, 1, 4): the developers' "??? : GameCam30Func_Nop", then the original stops (b .)
template <class P> void Game<P>::game_cam_nop()
{
    text_cursor(0, 40);
    text_printf(0x2BA80);
}

// 0x2BAC0 (sub-state 2): by the stop's wait kind (game_cam_clear_kinds)
template <class P> void Game<P>::game_cam_clear()
{
    camera_call(mem.u32(G::game_cam_clear_kinds + uint32_t(int32_t(mem.at<int8_t>(G::evt_cgmfnsh))) * 4), 0);
}

// 0x2BAE0: the stop is over: while not done, the eye and the look-at ease back to the path (game_cam_path_eye /
// _look); once both the angles toward the path's look point and the view's angles are within 0x40 of each other the
// stop is done (enemy_left_route = 1); done: the camera sits on the path at camera_frame. Then the view.
template <class P> void Game<P>::game_cam_clear_turn_back()
{
    LocalScope scope(*this);
    const uint32_t loc = local_alloc(0x10);
    const uint32_t r0 = cam_rec(0);
    uint32_t state = mem.at<uint8_t>(G::game_cam_turning) != 0 ? 1 : 0;
    if (mem.at<uint8_t>(G::enemy_left_route) != 0)
        state |= 2;
    if (state == 0)
        mem.at<uint8_t>(G::game_cam_turning) = 1;
    if (state <= 1) {
        game_cam_path_eye();
        game_cam_path_look();
        const uint32_t a = G::game_cam_attackers_look, p = G::game_cam_path_point;
        Tgp::Out o = cam_angles(mem.f32(a) - mem.f32(p), mem.f32(a + 4) - mem.f32(p + 4), mem.f32(a + 8) - mem.f32(p + 8));
        mem.set32(G::game_cam_back_angles, o.w[1]);
        mem.set32(G::game_cam_back_angles + 4, o.w[0]);
        uint32_t s = mem.at<uint16_t>(G::game_cam_back_angles);
        if (((s << 1) ^ s) & 0x8000) {
            mem.set32(G::game_cam_back_angles, 0x8000 - mem.u32(G::game_cam_back_angles));
            mem.set32(G::game_cam_back_angles + 4, mem.u32(G::game_cam_back_angles + 4) + 0x8000);
        }
        o = cam_angles(mem.f32(r0 + 0x18) - mem.f32(r0), mem.f32(r0 + 0x1C) - mem.f32(r0 + 4), mem.f32(r0 + 0x20) - mem.f32(r0 + 8));
        uint32_t yaw = o.w[0], pitch = o.w[1];
        s = pitch & 0xFFFF;
        if (((s << 1) ^ s) & 0x8000) {
            pitch = 0x8000 - pitch;
            yaw = yaw + 0x8000;
        }
        auto diff = [](uint32_t a, uint32_t b) {
            int32_t d = int32_t((a - b) & 0xFFFF);
            if (d > 0x7FFF)
                d -= 0x10000;
            return d < 0 ? -d : d;
        };
        const int32_t dp = diff(mem.u32(G::game_cam_back_angles), pitch);
        const int32_t dy = diff(mem.u32(G::game_cam_back_angles + 4), yaw);
        if (dp <= 0x40 && dy <= 0x40) {
            mem.at<uint8_t>(G::game_cam_turning) = 0;
            mem.at<uint8_t>(G::enemy_left_route) = 1;
        }
    } else {
        camera_path_eval(mem.u32(G::camera_path), mem.u32(G::camera_frame), r0, r0 + 0x18, loc + 8, loc + 0xC);
    }
    camera_look_at(r0, r0 + 0x18, 0);
}

// 0x2BD90 (wait kind 1): the stop is done at once; the eye and the look-at ease toward the path, the view
template <class P> void Game<P>::game_cam_clear_now()
{
    mem.at<uint8_t>(G::game_cam_turning) = 0;
    mem.at<uint8_t>(G::enemy_left_route) = 1;
    game_cam_path_eye();
    game_cam_path_look();
    camera_look_at(cam_rec(0), cam_rec(0) + 0x18, 0);
}

// 0x2BDD0 (sub-state 3): the eye held at the stop point; the look-at eased to the attackers' midpoint (weight n:
// (look n + target) / (n + 1)), the view; record 0's look mode +0xF0 1 / 2 turns the view a quarter turn about X
// (coprocessor: the view matrix times a pitch of -/+0x4000, its angles, then the pitch back); then the weight for the
// next frame (game_cam_look_at_attackers_sub), or 0 when no enemy is alive and the wait kind is 2
template <class P> void Game<P>::game_cam_look_at_attackers()
{
    const uint32_t r0 = cam_rec(0), a = G::game_cam_attackers_look;
    game_cam_position_hold();
    game_cam_attackers_midpoint();
    const int32_t n = mem.at<int16_t>(G::game_cam_look_weight);
    const float w = float(n), w1 = float(n + 1);
    float lx = (mem.f32(r0 + 0x18) * w + mem.f32(a)) / w1;
    float lz = (mem.f32(r0 + 0x20) * w + mem.f32(a + 8)) / w1;
    float ly = (mem.f32(r0 + 0x1C) * w + mem.f32(a + 4)) / w1;
    if (pc_path()) {   // PC Mode / Boss Mode: THOTD.EXE 0x45FF68 multiplies by 1 / (n + 1) (pmbody)
        const float r = 1.0f / w1;
        lx = (mem.f32(r0 + 0x18) * w + mem.f32(a)) * r;
        ly = (mem.f32(r0 + 0x1C) * w + mem.f32(a + 4)) * r;
        lz = (mem.f32(r0 + 0x20) * w + mem.f32(a + 8)) * r;
    }
    const float oldz = mem.f32(r0 + 0x20);
    cam_setf(r0 + 0x20, lz);
    cam_setf(r0 + 0x18, lx);
    cam_setf(r0 + 0x1C, ly);
    if (pc_path()) {
        // THOTD.EXE 0x45FF68's inline look-at reads the look-at z before it stores the new one (0x46002E runs before
        // the store at 0x46004A): the angles from the new x / y and last frame's z (pmbody, seen in the PC capture)
        uint32_t yaw = 0, pitch = 0;
        pm::vec_angles(mem.f32(r0) - lx, mem.f32(r0 + 4) - ly, mem.f32(r0 + 8) - oldz, yaw, pitch);
        mem.set32(r0 + 0xC, 0u - pitch);
        mem.set32(r0 + 0x10, yaw + 0x8000);
        mem.set32(r0 + 0x14, 0);
    } else {
        camera_look_at(r0, r0 + 0x18, 0);
    }
    const int32_t look = int32_t(mem.u32(r0 + 0xF0));
    if (pc_path()) {
        // THOTD.EXE 0x460093: only look mode 2 (no quarter turn: yaw and roll 0, the pitch -(pc_atan2(dy, table
        // square root of dx dx + dz dz) x 16), the eye minus the look-at); mode 1 keeps the plain look-at (pmrest2)
        if (look == 2) {
            const float eye[3] = {mem.f32(r0), mem.f32(r0 + 4), mem.f32(r0 + 8)};
            const float at[3] = {mem.f32(r0 + 0x18), mem.f32(r0 + 0x1C), mem.f32(r0 + 0x20)};
            mem.set32(r0 + 0x14, 0);
            mem.set32(r0 + 0x10, 0);
            mem.set32(r0 + 0xC, pm::look2_pitch(eye, at));
        }
    } else if (look >= 1 && look <= 2) {
        const uint32_t q = look == 1 ? 0x4000u : 0xFFFFC000u;
        cam_copro(0x21, {});
        cam_copro(0x15, {mem.u32(r0 + 0x10)});
        cam_copro(0x14, {mem.u32(r0 + 0xC)});
        cam_copro(0x16, {mem.u32(r0 + 0x14)});
        Tgp::Out m = cam_copro(0x11, {});
        cam_copro(0x10, {});
        cam_copro(0x14, {0u - q});
        Tgp::Out none;
        copro(0x09, m.w, 12, none);
        Tgp::Out o = cam_copro(0x6F, {});
        cam_copro(0x10, {});
        cam_copro(0x14, {q});
        cam_copro(0x15, {o.w[0]});
        cam_copro(0x14, {o.w[1]});
        Tgp::Out o2 = cam_copro(0x6F, {});
        mem.set32(r0 + 0x10, o2.w[0]);
        mem.set32(r0 + 0xC, o2.w[1]);
        cam_copro(0x06, {});
        mem.set32(r0 + 0x14, o2.w[2]);
    }
    if (mem.at<uint16_t>(G::enemies_alive) == 0 && mem.at<uint8_t>(G::evt_cgmfnsh) == 2) {
        mem.at<int16_t>(G::game_cam_look_weight) = 0;
        return;
    }
    game_cam_look_at_attackers_sub();
}

// 0x2C050 (sub-state 6): nothing
template <class P> void Game<P>::game_cam_idle() {}

// 0x2C060 (sub-state 7): a requested turn (game_cam_turn_request) restarts at step 0. Step 0: the step vector = 0.8 x
// the unit direction from the eye to the target, step 1; step 1: the look-at eases to the target (x 15 / 16), the eye
// moves by the step; within 10 of the target: step 2; step 2: the look-at keeps easing; with a return request, the
// step toward the stop point, step 3; step 3: moves by the step until within 8 of the stop point, then the eye is put
// there and the requests clear. Then the view.
template <class P> void Game<P>::game_cam_turn_to_point()
{
    const uint32_t r0 = cam_rec(0), t = G::game_cam_turn_target, v = G::game_cam_turn_vec;
    const uint32_t sp = G::game_cam_stop_point;
    if (mem.at<uint8_t>(G::game_cam_turn_request) != 0) {
        mem.at<uint8_t>(G::game_cam_turn_request) = 0;
        mem.set32(G::game_cam_turn_step, 0);
    }
    auto ease = [&](uint32_t to, float target) { return (mem.f32(to) * 15.0f + target) / 16.0f; };
    auto aim = [&](uint32_t to) {
        Tgp::Out o = cam_copro(0x0C, {fbits(mem.f32(to) - mem.f32(r0)), fbits(mem.f32(to + 4) - mem.f32(r0 + 4)),
                                      fbits(mem.f32(to + 8) - mem.f32(r0 + 8))});
        mem.set32(v, o.w[0]);
        mem.set32(v + 4, o.w[1]);
        const float x = mem.f32(v) * 0.8f, z = ffrom(o.w[2]) * 0.8f, y = mem.f32(v + 4) * 0.8f;
        mem.set32(G::game_cam_turn_step, mem.u32(G::game_cam_turn_step) + 1);
        cam_setf(v + 8, z);
        cam_setf(v, x);
        cam_setf(v + 4, y);
    };
    const int32_t step = int32_t(mem.u32(G::game_cam_turn_step));
    if (step == 0 || step == 1) {
        if (step == 0) {
            aim(t);
            mem.at<uint8_t>(G::game_cam_turn_active) = 1;
        }
        const float lx = ease(r0 + 0x18, mem.f32(t));
        const float ly = ease(r0 + 0x1C, mem.f32(t + 4));
        const float lz = ease(r0 + 0x20, mem.f32(t + 8));
        const float ex = mem.f32(r0) + mem.f32(v), ey = mem.f32(r0 + 4) + mem.f32(v + 4);
        const float ez = mem.f32(r0 + 8) + mem.f32(v + 8);
        Tgp::Out o = cam_copro(0x1F, {mem.u32(t), mem.u32(t + 4), mem.u32(t + 8), fbits(ex), fbits(ey), fbits(ez)});
        cam_setf(r0, ex);
        cam_setf(r0 + 4, ey);
        cam_setf(r0 + 8, ez);
        cam_setf(r0 + 0x20, lz);
        cam_setf(r0 + 0x18, lx);
        cam_setf(r0 + 0x1C, ly);
        if (!(ffrom(o.w[0]) >= 10.0f))
            mem.set32(G::game_cam_turn_step, mem.u32(G::game_cam_turn_step) + 1);
    } else if (step == 2) {
        const float lx = ease(r0 + 0x18, mem.f32(t));
        const float lz = ease(r0 + 0x20, mem.f32(t + 8));
        const float ly = ease(r0 + 0x1C, mem.f32(t + 4));
        cam_setf(r0 + 0x20, lz);
        cam_setf(r0 + 0x18, lx);
        cam_setf(r0 + 0x1C, ly);
        if (mem.at<uint8_t>(G::game_cam_return_request) != 0)
            aim(sp);
    } else if (step == 3) {
        Tgp::Out o = cam_copro(0x1F, {mem.u32(r0), mem.u32(r0 + 4), mem.u32(r0 + 8), mem.u32(sp), mem.u32(sp + 4),
                                      mem.u32(sp + 8)});
        if (!(ffrom(o.w[0]) <= 8.0f)) {
            cam_setf(r0, mem.f32(v) + mem.f32(r0));
            cam_setf(r0 + 4, mem.f32(v + 4) + mem.f32(r0 + 4));
            cam_setf(r0 + 8, mem.f32(r0 + 8) + mem.f32(v + 8));
        } else {
            mem.at<uint8_t>(G::game_cam_return_request) = 0;
            mem.at<uint8_t>(G::game_cam_turn_request) = 0;
            mem.at<uint8_t>(G::game_cam_turn_active) = 0;
            mem.set32(r0 + 8, mem.u32(sp + 8));
            mem.set32(r0, mem.u32(sp));
            mem.set32(r0 + 4, mem.u32(sp + 4));
        }
    }
    camera_look_at(r0, r0 + 0x18, 0);
}

// 0x2C450 (mode 7, per frame): after a mode change the stop is reset (sub-state 0, done flag set); then the job
// becomes camera_frame_mode7_look and runs
template <class P> void Game<P>::camera_frame_mode7()
{
    if (mem.u32(G::camera_mode_changed) != 0) {
        game_cam_stop_reset();
        mem.set32(G::camera_drive_flag, 0);
        mem.at<uint8_t>(G::enemy_left_route) = 1;
    }
    mem.set32(cam_rec(0) + 0x4C, F::camera_frame_mode7_look);
    camera_frame_mode7_look();
}

// 0x2C490: done flag = no attack position 0-3 held; camera_frame = the stop's frame; record 0 = the stop record
// (eye, angles, look-at and +0x28 / +0x2C)
template <class P> void Game<P>::camera_frame_mode7_look()
{
    uint8_t free = 1;
    for (uint32_t s = 0; s < 4; ++s)
        if (mem.at<uint8_t>(G::attack_slots + s * 0x14) != 0)
            free = 0;
    const uint32_t r0 = cam_rec(0), sp = G::game_cam_stop_point;
    mem.at<uint8_t>(G::enemy_left_route) = free;
    mem.set32(G::camera_frame, mem.u32(G::game_cam_stop_frame));
    for (uint32_t i = 0; i < 0x30; i += 4)
        mem.set32(r0 + i, mem.u32(sp + i));
}

// 0x2C530: the path's eye at camera_frame into game_cam_path_point; the eye eases toward it ((eye x 15 + p) / 16)
template <class P> void Game<P>::game_cam_path_eye()
{
    LocalScope scope(*this);
    const uint32_t loc = local_alloc(0x30);
    const uint32_t r0 = cam_rec(0), p = G::game_cam_path_point;
    camera_path_eval(mem.u32(G::camera_path), mem.u32(G::camera_frame), p, loc, loc + 0x20, loc + 0x24);
    const float x = (mem.f32(r0) * 15.0f + mem.f32(p)) / 16.0f;
    const float y = (mem.f32(r0 + 4) * 15.0f + mem.f32(p + 4)) / 16.0f;
    const float z = (mem.f32(r0 + 8) * 15.0f + mem.f32(p + 8)) / 16.0f;
    cam_setf(r0, x);
    cam_setf(r0 + 4, y);
    cam_setf(r0 + 8, z);
}

// 0x2C5C0: the eye at the stop point
template <class P> void Game<P>::game_cam_position_hold()
{
    const uint32_t r0 = cam_rec(0), sp = G::game_cam_stop_point;
    mem.set32(r0 + 8, mem.u32(sp + 8));
    mem.set32(r0, mem.u32(sp));
    mem.set32(r0 + 4, mem.u32(sp + 4));
}

// 0x2C5F0: the path's look point at camera_frame into game_cam_attackers_look; the look-at eases toward it
template <class P> void Game<P>::game_cam_path_look()
{
    LocalScope scope(*this);
    const uint32_t loc = local_alloc(0x30);
    const uint32_t r0 = cam_rec(0), a = G::game_cam_attackers_look;
    camera_path_eval(mem.u32(G::camera_path), mem.u32(G::camera_frame), loc, a, loc + 0x20, loc + 0x24);
    const float x = (mem.f32(r0 + 0x18) * 15.0f + mem.f32(a)) / 16.0f;
    const float z = (mem.f32(r0 + 0x20) * 15.0f + mem.f32(a + 8)) / 16.0f;
    const float y = (mem.f32(r0 + 0x1C) * 15.0f + mem.f32(a + 4)) / 16.0f;
    cam_setf(r0 + 0x20, z);
    cam_setf(r0 + 0x18, x);
    cam_setf(r0 + 0x1C, y);
}

// 0x2C680: the look point (game_cam_attackers_look) from the holders of attack positions 0 / 1 (+0x2D4 aim point;
// +0x2E8 0xFF = not aimed at): both held: their midpoint, or the one aimed at; one held: that one; else positions 2 /
// 3 the same way; none: the stop record's look-at point
template <class P> void Game<P>::game_cam_attackers_midpoint()
{
    const uint32_t s = G::attack_slots, a = G::game_cam_attackers_look;
    auto taken = [&](uint32_t i) { return mem.at<uint8_t>(s + i * 0x14); };
    auto holder = [&](uint32_t i) { return mem.u32(s + i * 0x14 + 4); };
    auto one = [&](uint32_t h) {
        mem.set32(a, mem.u32(h + 0x2D4));
        mem.set32(a + 4, mem.u32(h + 0x2D8));
        mem.set32(a + 8, mem.u32(h + 0x2DC));
    };
    auto mid = [&](uint32_t h0, uint32_t h1) {
        cam_setf(a, (mem.f32(h0 + 0x2D4) + mem.f32(h1 + 0x2D4)) / 2.0f);
        cam_setf(a + 4, (mem.f32(h0 + 0x2D8) + mem.f32(h1 + 0x2D8)) / 2.0f);
        cam_setf(a + 8, (mem.f32(h0 + 0x2DC) + mem.f32(h1 + 0x2DC)) / 2.0f);
    };
    const uint8_t t0 = taken(0);
    const uint32_t h0 = holder(0);
    if (t0 != 0 && taken(1) != 0) {
        const uint32_t h1 = holder(1);
        const bool aim0 = mem.at<uint8_t>(h0 + 0x2E8) != 0xFF, aim1 = mem.at<uint8_t>(h1 + 0x2E8) != 0xFF;
        if (aim0 && !aim1) {
            one(h0);
            return;
        }
        if (!aim0 && aim1) {
            one(h1);
            return;
        }
        mid(h0, h1);
        return;
    }
    if (t0 != 0 && taken(1) == 0) {
        one(h0);
        return;
    }
    if (t0 == 0 && taken(1) != 0) {
        one(holder(1));
        return;
    }
    if (taken(2) != 0 && taken(3) != 0) {
        mid(holder(2), holder(3));
        return;
    }
    if (taken(2) != 0 && taken(3) == 0) {
        one(holder(2));
        return;
    }
    if (taken(2) == 0 && taken(3) != 0) {
        one(holder(3));
        return;
    }
    const uint32_t l = G::game_cam_stop_point + 0x18;
    mem.set32(a, mem.u32(l));
    mem.set32(a + 4, mem.u32(l + 4));
    mem.set32(a + 8, mem.u32(l + 8));
}

// 0x2C8A0: a new stop: its frame = camera_frame, frames left 0x7FFFFFFF, look weight 0x200, view record 0, the turn
// and done flags and the wait kind cleared, the players' CCTB 16 flags cleared
template <class P> void Game<P>::game_cam_stop_reset()
{
    mem.set32(G::game_cam_stop_frame, mem.u32(G::camera_frame));
    mem.set32(G::camera_frames_left, 0x7FFFFFFF);
    mem.at<int16_t>(G::game_cam_look_weight) = 0x200;
    mem.set32(G::camera_view, 0);
    Player& p1 = mem.at<Player>(G::players, 1);
    Player& p0 = mem.at<Player>(G::players, 0);
    const uint8_t f1 = p1.cam_flags, f0 = p0.cam_flags;
    mem.at<uint8_t>(G::enemy_left_route) = 0;
    mem.at<uint8_t>(G::game_cam_turning) = 0;
    mem.at<uint8_t>(G::game_cam_turn_request) = 0;
    mem.at<uint8_t>(G::game_cam_return_request) = 0;
    mem.at<uint8_t>(G::game_cam_turn_active) = 0;
    mem.at<uint8_t>(G::evt_cgmfnsh) = 0;
    p1.cam_flags = uint8_t(f1 & ~1);
    p0.cam_flags = uint8_t(f0 & ~1);
}

// 0x2C930 (slot job): that camera 100 units above camera_pos, looking straight down (pitch -0x4000)
template <class P> void Game<P>::cam_slot_overhead(int slot)
{
    const uint32_t r = cam_rec(slot);
    const uint32_t x = mem.u32(G::camera_pos), z = mem.u32(G::camera_pos + 8);
    const float y = float(double(mem.f32(G::camera_pos + 4)) + 100.0);
    mem.set32(r, x);
    mem.set32(r + 8, z);
    mem.set32(r + 0x10, 0);
    mem.set32(r + 0x14, 0);
    mem.set32(r + 0xC, 0xFFFFC000);
    cam_setf(r + 4, y);
}

// 0x2C990 (CCTB 20 n): the slot waits n frames (cam_job20_wait), or forever with n = 0 (cam_job20_nop)
template <class P> void Game<P>::cam_job20(int slot)
{
    const uint32_t r = cam_rec(slot);
    mem.set32(r + 0x4C, mem.u32(r + 0xC0) != 0 ? F::cam_job20_wait : F::cam_job20_nop);
}

// 0x2C9C0: counts operand 0 down; at 0 the job is done
template <class P> void Game<P>::cam_job20_wait(int slot)
{
    const uint32_t a = cam_rec(slot) + 0xC0;
    const uint32_t n = mem.u32(a) - 1;
    mem.set32(a, n);
    if (n == 0)
        cam_job_end(slot);
}

// 0x2CA20: nothing
template <class P> void Game<P>::cam_job20_nop(int) {}

// 0x2CA30 (CCTB 32 frames n): the eye and angles from camera_offsets[n] every frame, the stop's done flag set; with
// frames != 0 the job is done after that many frames (0: it stays)
template <class P> void Game<P>::cam_job32_offset(int slot)
{
    const uint32_t r = cam_rec(slot);
    const uint32_t src = G::camera_offsets + mem.u32(r + 0xC4) * 0x18;
    const uint32_t frames = mem.u32(r + 0xC0);
    mem.set32(r + 0x10, mem.u32(src + 0x10));
    mem.set32(r + 0x14, mem.u32(src + 0x14));
    mem.at<uint8_t>(G::enemy_left_route) = 1;
    for (uint32_t i = 0; i < 0x10; i += 4)
        mem.set32(r + i, mem.u32(src + i));
    if (frames == 0)
        return;
    const uint32_t n = mem.u32(r + 0xC0) - 1;
    mem.set32(r + 0xC0, n);
    if (n == 0)
        cam_job_end(slot);
}

// 0x2CAD0 (CCTB 64 start end path flags): start = end: jump to that frame; flags bit 1: keep the segment for later;
// else play it
template <class P> void Game<P>::cam_job64_path(int slot)
{
    const uint32_t r = cam_rec(slot);
    if (mem.u32(r + 0xC0) == mem.u32(r + 0xC4)) {
        cam_job64_jump(slot);
        return;
    }
    if (mem.at<uint8_t>(r + 0xCC) & 2) {
        cam_job64_defer(slot);
        return;
    }
    cam_job64_play_start(slot);
}

// 0x2CB20: the camera at frame `start` of the path (camera_path = it), the view; done
template <class P> void Game<P>::cam_job64_jump(int slot)
{
    LocalScope scope(*this);
    const uint32_t loc = local_alloc(0x10);
    const uint32_t r = cam_rec(slot);
    cam_setf(r + 0xEC, float(int32_t(mem.u32(r + 0xC0))));
    const uint32_t path = mem.u32(r + 0xC8);
    mem.set32(G::camera_path, path);
    camera_path_eval(path, mem.u32(r + 0xEC), r, r + 0x18, loc, loc + 4);
    camera_look_at(r, r + 0x18, mem.u32(loc));
    cam_job_end(slot);
}

// 0x2CBD0: camera_path = the path; start (-1: the record's frame + 1), end and flags kept in cam_deferred_*; done
template <class P> void Game<P>::cam_job64_defer(int slot)
{
    const uint32_t r = cam_rec(slot);
    mem.set32(G::camera_path, mem.u32(r + 0xC8));
    uint32_t start = mem.u32(r + 0xC0);
    if (start == 0xFFFFFFFF)
        start = uint32_t(cvtzril(mem.f32(r + 0xEC)) + 1);
    mem.set32(G::cam_deferred_start, start);
    mem.set32(G::cam_deferred_end, mem.u32(r + 0xC4));
    mem.set32(G::cam_deferred_flags, mem.u32(r + 0xCC));
    cam_job_end(slot);
}

// 0x2CC90: the frame range: flags bit 2 = the deferred one (the frame from operand 0); else camera_path = the path,
// from start (-1: the record's frame) to end; then cam_job64_play, at once
template <class P> void Game<P>::cam_job64_play_start(int slot)
{
    const uint32_t r = cam_rec(slot);
    if (mem.at<uint8_t>(r + 0xCC) & 4) {
        const float f = float(int32_t(mem.u32(r + 0xC0)));
        mem.set32(r + 0x80, mem.u32(G::cam_deferred_start));
        mem.set32(r + 0x84, mem.u32(G::cam_deferred_end));
        mem.set32(r + 0x8C, mem.u32(r + 0xCC));
        cam_setf(r + 0xEC, f);
    } else {
        const uint32_t start = mem.u32(r + 0xC0);
        mem.set32(G::camera_path, mem.u32(r + 0xC8));
        if (start != 0xFFFFFFFF && boss_next_on() &&
            !(mem.at<uint8_t>(G::chapter) == 3 && mem.at<uint8_t>(G::event_flags + 213) != 0)) {
            // Boss Mode's undrawn walk-up (PC 0x461158 while pc_boss_next, except Chapter 4 after flag 213): the path
            // starts 5 frames before its end (or at its start when that is later)
            const int32_t e5 = int32_t(mem.u32(r + 0xC4)) - 5;
            const int32_t f = int32_t(start) < e5 ? e5 : int32_t(start);
            cam_setf(r + 0xEC, float(f));
            mem.set32(r + 0x80, uint32_t(f));
        } else if (start != 0xFFFFFFFF) {
            mem.set32(r + 0x80, start);
            cam_setf(r + 0xEC, float(int32_t(start)));
        } else {
            mem.set32(r + 0x80, uint32_t(cvtzril(mem.f32(r + 0xEC))));
        }
        mem.set32(r + 0x84, mem.u32(r + 0xC4));
        mem.set32(r + 0x8C, mem.u32(r + 0xCC));
    }
    mem.set32(r + 0x4C, F::cam_job64_play);
    cam_job64_play(slot);
}

// 0x2CD70, per frame: the camera at the current frame, the view; frames left = end - current; at the end the job is
// done; current + 1
template <class P> void Game<P>::cam_job64_play(int slot)
{
    LocalScope scope(*this);
    const uint32_t loc = local_alloc(0x10);
    const uint32_t r = cam_rec(slot);
    const float f = float(int32_t(mem.u32(r + 0x80)));
    cam_setf(r + 0xEC, f);
    camera_path_eval(mem.u32(G::camera_path), fbits(f), r, r + 0x18, loc, loc + 4);
    camera_look_at(r, r + 0x18, mem.u32(loc));
    const int32_t end = int32_t(mem.u32(r + 0x84)), cur = int32_t(mem.u32(r + 0x80));
    mem.set32(G::camera_frames_left, uint32_t(end - cur));
    if (!(cur < end))
        cam_job_end(slot);
    mem.set32(r + 0x80, mem.u32(r + 0x80) + 1);
}

// 0x2CE50 (CCTB 17 n): setup n of the current camera kind (camera_setups[kind][n]); camera_stopped = the kind, drive
// mode and camera mode = n; done
template <class P> void Game<P>::cam_job17_resetup(int slot)
{
    const uint32_t kind = mem.u32(G::camera_kind), n = mem.u32(cam_rec(slot) + 0xC0);
    const uint32_t routine = mem.u32(G::camera_setups + kind * 36 + n * 4);
    mem.set32(G::camera_stopped, kind);
    mem.set32(G::camera_kind, kind);
    mem.set32(G::camera_drive_mode, n);
    mem.set32(G::camera_mode, n);
    camera_call(routine, uint32_t(slot) * 0x154);
    cam_job_end(slot);
}

// 0x2CEF0 (CCTB 33 m): camera mode m: sub-state 3, the targeted flags cleared, camera kind 2, the mode's setup
// (camera_setups row 2); camera_status bit 0; slot 0's job = the mode's per-frame routine; evt_state 0 (the job stays)
template <class P> void Game<P>::cam_job33_mode(int slot)
{
    mem.set32(G::camera_drive_flag, 3);
    mem.at<uint8_t>(G::player_targeted) = 0;
    const uint32_t m = mem.u32(cam_rec(slot) + 0xC0);
    mem.at<uint8_t>(G::player_targeted + 1) = 0;
    const uint32_t routine = mem.u32(G::camera_setups + 2 * 36 + m * 4);
    mem.set32(G::camera_kind, 2);
    mem.set32(G::camera_mode, m);
    camera_call(routine, uint32_t(slot) * 0x154);
    const uint32_t mode = mem.u32(G::camera_mode);
    const uint32_t status = mem.u32(G::camera_status);
    const uint32_t job = mem.u32(G::camera_frame_modes + mode * 4);
    mem.set32(G::evt_state, 0);
    mem.set32(G::camera_status, status | 1);
    mem.set32(cam_rec(0) + 0x4C, job);
}

// 0x2CF80 (CCTB 18 n): both players' cut-scene routine = player_scene_routines[n]; done
template <class P> void Game<P>::cam_job18_player_scene(int slot)
{
    const uint32_t n = mem.u32(cam_rec(slot) + 0xC0);
    mem.at<Player>(G::players, 0).scene_routine = mem.u32(G::player_scene_routines + n * 4);
    mem.at<Player>(G::players, 1).scene_routine = mem.u32(G::player_scene_routines + n * 4);
    if (coop4_on())   // 4P: which scene (players 3 / 4's drawn bodies, game/creature.cpp)
        coop4_scene = int(n);
    scene_started = int(n);   // the cut-scene viewer (ours; no game state)
    ++scene_starts;
    cam_job_end(slot);
}

// 0x2D000 (CCTB 21 v): camera_view = v; done
template <class P> void Game<P>::cam_job21_set(int slot)
{
    mem.set32(G::camera_view, mem.u32(cam_rec(slot) + 0xC0));
    cam_job_end(slot);
}

// 0x2D060 (CCTB 16 b): bit 0 of both players' camera flags = b's bit 0; done
template <class P> void Game<P>::cam_job16_player_flag(int slot)
{
    const uint32_t r = cam_rec(slot);
    Player& p1 = mem.at<Player>(G::players, 1);
    Player& p0 = mem.at<Player>(G::players, 0);
    const uint8_t b = mem.at<uint8_t>(r + 0xC0);
    const uint32_t w = mem.u32(r + 0xC0);
    p1.cam_flags = uint8_t((p1.cam_flags & ~1) | (b & 1));
    p0.cam_flags = uint8_t((p0.cam_flags & ~1) | (w & 1));
    cam_job_end(slot);
}

// 0x2D100 (CCTB 19 n): the 7 preset words n (scninit_camera, 0x1C bytes) into the record's +0xD0..+0xEB; done
template <class P> void Game<P>::cam_job19_preset(int slot)
{
    const uint32_t r = cam_rec(slot);
    const uint32_t src = G::scninit_camera + mem.u32(r + 0xC0) * 0x1C;
    mem.set32(r + 0xE0, mem.u32(src + 0x10));
    mem.set32(r + 0xE4, mem.u32(src + 0x14));
    for (uint32_t i = 0; i < 0x10; i += 4)
        mem.set32(r + 0xD0 + i, mem.u32(src + i));
    mem.set32(r + 0xE8, mem.u32(src + 0x18));
    cam_job_end(slot);
}

// 0x2D190 (record, point, roll): the angles from the point to the eye (coprocessor 0x0F, with the pitch fold): pitch
// +0xC = -pitch, yaw +0x10 = yaw + 0x8000, roll +0x14
template <class P> void Game<P>::camera_look_at(uint32_t rec, uint32_t point, uint32_t roll)
{
    const float dx = mem.f32(rec) - mem.f32(point), dy = mem.f32(rec + 4) - mem.f32(point + 4);
    const float dz = mem.f32(rec + 8) - mem.f32(point + 8);
    if (pc_path()) {
        // PC Mode / Boss Mode on the arcade logic: THOTD.EXE 0x461A30 (and its copies inline in camera_slot2_follow
        // 0x45F4F0): the camera file's pc_vec_angles copy 0x45D300 (the PC's atan table: angles in steps of 16, its
        // fold), pitch negated, yaw + 0x8000, and the roll always 0 (pmbody)
        uint32_t yaw = 0, pitch = 0;
        pm::vec_angles(dx, dy, dz, yaw, pitch);
        mem.set32(rec + 0xC, 0u - pitch);
        mem.set32(rec + 0x10, yaw + 0x8000);
        mem.set32(rec + 0x14, 0);
        (void)roll;
        return;
    }
    Tgp::Out o = cam_copro(0x0F, {fbits(dx), fbits(dy), fbits(dz)});
    mem.set32(rec + 0x10, o.w[0]);
    mem.set32(rec + 0xC, o.w[1]);
    const uint32_t s = mem.at<uint16_t>(rec + 0xC);
    uint32_t yaw = mem.u32(rec + 0x10), pitch = o.w[1];
    if (((s << 1) ^ s) & 0x8000) {
        pitch = 0x8000 - o.w[1];
        yaw = yaw + 0x8000;
    }
    pitch = 0u - pitch;
    yaw = yaw + 0x8000;
    mem.set32(rec + 0xC, pitch);
    mem.set32(rec + 0x10, yaw);
    mem.set32(rec + 0x14, roll);
}

// 0x2D240: the look weight: game_cam_look_weight_set -1: by the cosine of the angle between the view and the
// direction to the attackers' midpoint (coprocessor 0x89): above 0.95: 64, else (c - 0.92) x 1600 + 16 truncated,
// at least 16 (as a 16-bit value); else that weight
template <class P> void Game<P>::game_cam_look_at_attackers_sub()
{
    const uint16_t set = mem.at<uint16_t>(G::game_cam_look_weight_set);
    if (set != 0xFFFF) {
        mem.at<uint16_t>(G::game_cam_look_weight) = set;
        return;
    }
    const uint32_t r0 = cam_rec(0), a = G::game_cam_attackers_look;
    const float ex = mem.f32(r0), ey = mem.f32(r0 + 4), ez = mem.f32(r0 + 8);
    if (pc_path()) {
        // PC Mode / Boss Mode: THOTD.EXE 0x461690: the camera file's vec_cos copy 0x45DB2C (table square roots), above
        // 0.95 64, else (c - 0.92) x 1600 + 16 chopped, at least 16 (pmbody)
        const float c = pm::vec_cos(mem.f32(r0 + 0x18) - ex, mem.f32(r0 + 0x1C) - ey, mem.f32(r0 + 0x20) - ez,
                                    mem.f32(a) - ex, mem.f32(a + 4) - ey, mem.f32(a + 8) - ez);
        if (c > ffrom(0x3F733333)) {
            mem.at<int16_t>(G::game_cam_look_weight) = 64;
            return;
        }
        const int16_t n = int16_t(cvtzril((c + ffrom(0xBF6B851F)) * 1600.0f + 16.0f));
        mem.at<int16_t>(G::game_cam_look_weight) = n < 16 ? int16_t(16) : n;
        return;
    }
    Tgp::Out o = cam_copro(0x89, {fbits(mem.f32(r0 + 0x18) - ex), fbits(mem.f32(r0 + 0x1C) - ey),
                                  fbits(mem.f32(r0 + 0x20) - ez), fbits(mem.f32(a) - ex), fbits(mem.f32(a + 4) - ey),
                                  fbits(mem.f32(a + 8) - ez)});
    const float c = ffrom(o.w[0]);
    if (!(c <= ffrom(0x3F733333))) {
        mem.at<int16_t>(G::game_cam_look_weight) = 64;
        return;
    }
    const float v = (c - ffrom(0x3F6B851F)) * ffrom(0x44C8000D) + 16.0f;
    int32_t n = cvtzril(v);
    if (!(int32_t(uint32_t(n) << 16) > (15 << 16)))
        n = 16;
    mem.at<int16_t>(G::game_cam_look_weight) = int16_t(n);
}

// 0x2D350: every camera record's eye / angles / look-at from camera_record_init, frames 0; the look modes, the look
// weight setting, camera_view .. camera_kind, slot 2's mode, drive mode / camera mode cleared; camera_setups[0][0]
// (camera_mode_routine_clear); the done / turning flags; the update count 0xC000; the shadow at 0
template <class P> void Game<P>::camera_globals_reset()
{
    const uint32_t i = G::camera_record_init;
    for (int k = 0; k < 6; ++k) {
        const uint32_t r = cam_rec(k);
        for (uint32_t j = 0; j < 0x10; j += 4) {
            mem.set32(r + j, mem.u32(i + j));
            mem.set32(r + 0x18 + j, mem.u32(i + j));
        }
        mem.set32(r + 0xEC, 0);
    }
    for (int k = 0; k < 6; ++k) {
        const uint32_t r = cam_rec(k);
        for (uint32_t j = 0; j < 8; j += 4) {
            mem.set32(r + 0x10 + j, mem.u32(i + 0x10 + j));
            mem.set32(r + 0x28 + j, mem.u32(i + 0x10 + j));
        }
    }
    camera_records_clear();
    camera_target_clear();
    for (uint32_t j = 0; j < 0x10; j += 4)
        mem.set32(G::camera_view + j, 0);
    mem.set32(G::camera_slot2_mode, 0);
    mem.set32(G::camera_drive_mode, 0);
    mem.set32(G::camera_mode, 0);
    camera_call(mem.u32(G::camera_setups), 0);
    mem.at<uint8_t>(G::enemy_left_route) = 0;
    mem.at<uint8_t>(G::game_cam_turning) = 0;
    mem.set32(G::camera_update_count, 0xC000);
    mem.set32(G::camera_update_count + 4, 0);
    mem.set32(G::camera_update_count + 8, 0);
    mem.set32(G::camera_update_count + 0xC, 0);
    shadow_setup(0.0f);
}

// 0x2D4E0 (path, frame, eye out, look-at out, roll out, zoom out): coprocessor 0x6E (the path's keyframed curves at
// the frame): eye, look-at, roll, zoom value; zoom = 413.168 / value for paths flagged in camera_path_zoom, else 496;
// the zoom to the geometrizer (command 9) and to the coprocessor's projection (0x7D)
template <class P> void Game<P>::camera_path_eval(uint32_t path, uint32_t frame, uint32_t eye, uint32_t look,
                                                  uint32_t roll, uint32_t zoom)
{
    // Attract Gore (user 2026-11-04, the attract as the 1998 PC plays it): the prologue's scene 7 (mode 9, chapter slot
    // 5; path 107, the hostage and the two zombies) on the PC's camera keys. CAMALL.RBH's path 107 ends higher than
    // the ROM's (frame 100: eye y 13.79 instead of 10.79, look-at y 18 instead of 16; every other path matches the
    // ROM's keys): copro data words 0xC803 / 0xC7CC / 0xC7D0 read as the PC's values for this call. Off: Revision A
    // (PC Mode's camera already plays CAMALL.RBH's keys: pc_path below)
    static constexpr uint32_t kPcKeys[3][2] = {{0xC803, 0x415CA565}, {0xC7CC, 0x41900000}, {0xC7D0, 0x41900000}};
    const bool pcKeys = !P::pc && gAttractGore && path == 107 && !pc_path() && mem.at<int16_t>(G::game_mode) == 9 &&
                        mem.at<int8_t>(G::chapter) == 5 && tgp.data_word(0xC803) == 0x412CA565 &&
                        tgp.data_word(0xC7CC) == 0x41800000 && tgp.data_word(0xC7D0) == 0x41800000;
    if (pcKeys)
        tgp.set_patch(kPcKeys, 3);
    Tgp::Out o = cam_copro(0x6E, {path, frame});
    if (pcKeys)
        tgp.set_patch(nullptr, 0);
    if constexpr (!P::pc) {
        if (pc_path()) {
            // PC Mode / Boss Mode on the arcade logic: THOTD.EXE camera_path_eval 0x461980: the frame truncated to an
            // integer, eye and look-at by the PC's curve evaluation 0x461858 (pm::path_curve) on the PC's own camera
            // file (CAM\ALLSTAGE\CAMALL.RBH: its keys differ from the ROM's in the last digits, the tangents rounded;
            // without the PC files the ROM's keys); the roll / zoom words stay the coprocessor's (the PC writes
            // neither: locals of the callers; pmbody)
            const int32_t F = int32_t(std::trunc(ffrom(frame)));
            const uint32_t base = tgp.data_word(path);
            const size_t nc = pm_cam.size();
            auto pcw = [&](uint32_t i) { return i < nc ? pm_cam[i] : 0u; };   // CAMALL.RBH's BODY (byte offsets)
            auto rom = [&](uint32_t i) { return tgp.data_word(i); };
            for (uint32_t j = 0; j < 6; ++j)
                o.w[j] = fbits(nc != 0 && path < pcw(0) / 4
                                   ? pm::path_curve(pcw, pcw(pcw(path) / 4 + j) / 4, F)
                                   : pm::path_curve(rom, tgp.data_word(base + j), F));
        }
    }
    mem.set32(eye, o.w[0]);
    mem.set32(eye + 4, o.w[1]);
    mem.set32(eye + 8, o.w[2]);
    mem.set32(look, o.w[3]);
    mem.set32(look + 4, o.w[4]);
    mem.set32(look + 8, o.w[5]);
    mem.set32(roll, o.w[6]);
    mem.set32(zoom, o.w[7]);
    float z = 496.0f;
    if (mem.at<uint8_t>(G::camera_path_zoom + path) != 0)
        z = ffrom(0x43CE9581) / ffrom(o.w[7]);
    cam_setf(zoom, z);
    geo_write(0x800090, 0x909);
    geo_write(0x804000, fbits(z));
    geo_write(0x804000, fbits(z));
    const uint32_t zb = mem.u32(zoom);
    cam_copro(0x7D, {zb, zb});
}

// 0x2D5D0 (request): a turn of the game camera: game_cam_turn_request, the target and two more words (6 words)
template <class P> void Game<P>::unused_2d5d0(uint32_t request)
{
    mem.at<uint8_t>(G::game_cam_turn_request) = 1;
    for (uint32_t i = 0; i < 0x18; i += 4)
        mem.set32(G::game_cam_turn_target + i, mem.u32(request + i));
}

// 0x2D600: after the turn, back to the stop point
template <class P> void Game<P>::unused_2d600()
{
    mem.at<uint8_t>(G::game_cam_return_request) = 1;
}

// 0x2D610: the look modes (+0xF0) of the 6 records cleared
template <class P> void Game<P>::camera_records_clear()
{
    for (int k = 5; k >= 0; --k)
        mem.set32(cam_rec(k) + 0xF0, 0);
}

// 0x2D660: look weight setting -1 (by angle)
template <class P> void Game<P>::camera_target_clear()
{
    mem.at<int16_t>(G::game_cam_look_weight_set) = -1;
}

// 0x2D670: camera_mode_routine = nop
template <class P> void Game<P>::camera_mode_routine_clear()
{
    mem.set32(G::camera_mode_routine, F::nop);
}

// 0x2D690 (CAMDR 1): camera_pos at the players' bodies (camdr_mode1_frame); both bodies drawn by
// player_body_draw_camdr, motions camdr_motions; camera_mode_changed
template <class P> void Game<P>::camdr_mode1()
{
    Player& p0 = mem.at<Player>(G::players, 0);
    Player& p1 = mem.at<Player>(G::players, 1);
    const uint32_t body = p0.body.a, motion = mem.u32(G::camdr_motions);
    mem.set32(G::camera_mode_routine, F::camdr_mode1_frame);
    p1.body_routine = F::player_body_draw_camdr;
    p0.body_routine = F::player_body_draw_camdr;
    set_motion(body, motion);
    set_motion(mem.at<Player>(G::players, 1).body.a, mem.u32(G::camdr_motions + 4));
    mem.set32(G::camera_mode_changed, 1);
}

// 0x2D6F0 (CAMDR 2): no camera mode routine; bodies drawn, scene routine camdr_mode2_body
template <class P> void Game<P>::camdr_mode2()
{
    Player& p0 = mem.at<Player>(G::players, 0);
    Player& p1 = mem.at<Player>(G::players, 1);
    const uint32_t body = p0.body.a, motion = mem.u32(G::camdr_motions);
    p0.body_routine = F::player_body_draw_camdr;
    p0.scene_routine = F::camdr_mode2_body;
    mem.set32(G::camera_mode_routine, F::nop);
    p1.body_routine = F::player_body_draw_camdr;
    p1.scene_routine = F::camdr_mode2_body;
    set_motion(body, motion);
    set_motion(mem.at<Player>(G::players, 1).body.a, mem.u32(G::camdr_motions + 4));
    mem.set32(G::camera_mode_changed, 1);
}

// 0x2D770 (CAMDR 3): the view from record 2; camera_pos from it (camdr_mode3_frame); scene routine camdr_mode3_body
template <class P> void Game<P>::camdr_mode3()
{
    Player& p0 = mem.at<Player>(G::players, 0);
    Player& p1 = mem.at<Player>(G::players, 1);
    const uint32_t body = p0.body.a, motion = mem.u32(G::camdr_motions);
    mem.set32(G::camera_view, 2);
    mem.set32(G::camera_mode_routine, F::camdr_mode3_frame);
    p1.scene_routine = F::camdr_mode3_body;
    p0.scene_routine = F::camdr_mode3_body;
    set_motion(body, motion);
    set_motion(mem.at<Player>(G::players, 1).body.a, mem.u32(G::camdr_motions + 4));
    mem.set32(G::camera_mode_changed, 1);
}

// CCTB 33 modes 4-7: body routine player_hit_notice, the mode's scene routine and camera mode routine
#define HOTD_CCTB33_MODE(NAME, SCENE, ROUTINE)                                                                         \
    template <class P> void Game<P>::NAME()                                                                            \
    {                                                                                                                  \
        Player& p0 = mem.at<Player>(G::players, 0);                                                                    \
        Player& p1 = mem.at<Player>(G::players, 1);                                                                    \
        const uint32_t body = p0.body.a, motion = mem.u32(G::camdr_motions);                                           \
        p0.body_routine = F::player_hit_notice;                                                                        \
        p0.scene_routine = F::SCENE;                                                                                   \
        mem.set32(G::camera_mode_routine, F::ROUTINE);                                                                 \
        p1.body_routine = F::player_hit_notice;                                                                        \
        p1.scene_routine = F::SCENE;                                                                                   \
        set_motion(body, motion);                                                                                      \
        set_motion(mem.at<Player>(G::players, 1).body.a, mem.u32(G::camdr_motions + 4));                              \
    }
// 0x2D7E0 (mode 4, a stop), 0x2D860 (5, impulses), 0x2D8E0 (6, walk the deferred segment), 0x2D960 (7, the same)
HOTD_CCTB33_MODE(cctb33_mode4, cctb33_mode4_body, cctb33_mode4_start)
HOTD_CCTB33_MODE(cctb33_mode5, cctb33_body_follow, cctb33_mode5_start)
HOTD_CCTB33_MODE(cctb33_mode6, cctb33_body_follow, cctb33_mode6_start)
HOTD_CCTB33_MODE(cctb33_mode7, cctb33_body_follow, cctb33_mode7_start)
#undef HOTD_CCTB33_MODE

// 0x2DA00 (every unused setup slot): the developers' "ChgCamMode not defined!!", then the original stops (b .)
template <class P> void Game<P>::ChgCamMode_undefined()
{
    text_cursor(0, 40);
    text_printf(0x2D9E0);
}

// 0x6C1F0 (pitch, yaw, out): the direction (0, 0, 1) turned by yaw / pitch through stored matrix 0 (the camera)
// without its translation (coprocessor)
template <class P> void Game<P>::matrix_identity_set(uint32_t pitch, uint32_t yaw, uint32_t out)
{
    cam_copro(0x74, {0});
    cam_copro(0x88, {0, 0, 0});
    cam_copro(0x15, {yaw});
    cam_copro(0x14, {pitch});
    Tgp::Out o = cam_copro(0x1A, {0, 0, fbits(1.0f)});
    mem.set32(out, o.w[0]);
    mem.set32(out + 4, o.w[1]);
    cam_copro(0x06, {});
    mem.set32(out + 8, o.w[2]);
}

// 0x6CA60: camera_pos and heading 0, floor_y 0, camera_mode_routine nop
template <class P> void Game<P>::camera_reset()
{
    mem.set32(G::camera_heading, 0);
    mem.set32(G::camera_heading + 4, 0);
    for (uint32_t i = 0; i < 0x10; i += 4)
        mem.set32(G::camera_pos + i, 0);
    mem.set32(G::floor_y, 0);
    mem.set32(G::camera_mode_routine, F::nop);
}

// 0x6CAB0 (input_task, per frame): the camera mode routine
template <class P> void Game<P>::camera_mode_run()
{
    camera_call(mem.u32(G::camera_mode_routine), 0);
}

// 0x6CAD0 (CAMDR 3, per frame): camera_pos from record 0
template <class P> void Game<P>::camdr_mode3_frame()
{
    camdr_mode3_view_pos(0);
}

// 0x6CAE0 (CCTB 33 mode 4): the stop point = the eye; then nothing per frame
template <class P> void Game<P>::cctb33_mode4_start()
{
    const uint32_t r0 = cam_rec(0), sp = G::game_cam_stop_point;
    mem.set32(G::camera_mode_routine, F::cctb33_mode4_idle);
    mem.set32(sp + 8, mem.u32(r0 + 8));
    mem.set32(sp, mem.u32(r0));
    mem.set32(sp + 4, mem.u32(r0 + 4));
    cctb33_mode4_idle();
}

// 0x6CB20: nothing
template <class P> void Game<P>::cctb33_mode4_idle() {}

// 0x6CB30 (CCTB 33 mode 5): the impulse cleared; heading = coprocessor 0x0A of (-(z - eye z), x - eye x) toward the
// point after camera_mode_routine (+4 / +0xC); camera_pos = the eye at floor_y (+ pitch), camera_heading = yaw, roll;
// then cctb33_mode5_frame per frame
template <class P> void Game<P>::cctb33_mode5_start()
{
    const uint32_t r0 = cam_rec(0), t = G::camera_mode_routine;
    const uint32_t ex = mem.u32(r0), ez = mem.u32(r0 + 8), pitch = mem.u32(r0 + 0xC);
    const float dz = mem.f32(t + 0xC) - ffrom(ez), dx = mem.f32(t + 4) - ffrom(ex);
    const uint32_t yaw = mem.u32(r0 + 0x10), roll = mem.u32(r0 + 0x14);
    mem.set32(G::cam_mode5_push + 8, 0);
    mem.set32(G::cam_mode5_push, 0);
    mem.set32(G::cam_mode5_push + 4, 0);
    mem.set32(G::cam_mode5_push + 0x18, 0);
    mem.set32(G::cam_mode5_push + 0x10, 0);
    mem.set32(G::cam_mode5_push + 0x14, 0);
    mem.set32(G::cam_mode5_heading + 8, 0);
    mem.set32(G::camera_mode_routine, F::cctb33_mode5_frame);
    const uint32_t floor = mem.u32(G::floor_y);
    Tgp::Out o = cam_copro(0x0A, {fbits(dz) ^ 0x80000000u, fbits(dx)});
    mem.set32(G::camera_heading, yaw);
    mem.set32(G::camera_heading + 4, roll);
    mem.set32(G::camera_pos, ex);
    mem.set32(G::camera_pos + 4, floor);
    mem.set32(G::camera_pos + 8, ez);
    mem.set32(G::camera_pos + 0xC, pitch);
    mem.set32(G::cam_mode5_heading, o.w[0]);
    mem.set32(G::cam_mode5_heading + 4, 0);
    cctb33_mode5_frame();
}

// 0x6CC20, per frame: input bit 1 requests an impulse; cctb33_mode5_impulse
template <class P> void Game<P>::cctb33_mode5_frame()
{
    if (mem.at<uint16_t>(G::cam_mode5_input) & 2)
        mem.set32(G::cam_mode5_heading + 4, 1);
    cctb33_mode5_impulse();
}

// 0x6CC40: a request while idle: the push along the heading (coprocessor: (0, 0, -1) turned by it, x 8) as a velocity
// x 0.0667 and a decrement x -0.00222, for 30 frames; each of them: velocity += decrement, camera_pos += velocity;
// the request clears on the last
template <class P> void Game<P>::cctb33_mode5_impulse()
{
    const uint32_t h = G::cam_mode5_heading, v = G::cam_mode5_push;
    if (mem.u32(h + 4) != 0 && mem.u32(h + 0xC) == 0 && mem.u32(h + 8) == 0) {
        const uint32_t tx = mem.u32(G::camera_mode_routine + 4), tz = mem.u32(G::camera_mode_routine + 0xC);
        cam_copro(0x21, {});
        cam_copro(0x1F, {tx, 0, tz, mem.u32(G::camera_pos), 0, mem.u32(G::camera_pos + 8)});
        cam_copro(0x15, {mem.u32(h)});
        Tgp::Out o = cam_copro(0x1A, {0, 0, 0xBF800000});
        const float k1 = ffrom(0xBB11A2B4), k2 = ffrom(0x3D888889);
        const float z = ffrom(o.w[2]) * 8.0f, x = ffrom(o.w[0]) * 8.0f, y = ffrom(o.w[1]) * 8.0f;
        mem.set32(h + 0xC, 30);
        cam_copro(0x06, {});
        cam_setf(v + 8, z * k1);
        cam_setf(v + 0x18, z * k2);
        cam_setf(v, x * k1);
        cam_setf(v + 4, y * k1);
        cam_setf(v + 0x10, x * k2);
        cam_setf(v + 0x14, y * k2);
    }
    uint32_t n = mem.u32(h + 0xC);
    if (n != 0) {
        if (n == 1)
            mem.set32(h + 4, 0);
        --n;
        const float vx = mem.f32(v + 0x10) + mem.f32(v), vy = mem.f32(v + 0x14) + mem.f32(v + 4);
        const float vz = mem.f32(v + 0x18) + mem.f32(v + 8);
        const float px = mem.f32(G::camera_pos) + vx, py = mem.f32(G::camera_pos + 4) + vy;
        const float pz = mem.f32(G::camera_pos + 8) + vz;
        cam_setf(v + 0x18, vz);
        cam_setf(v + 0x10, vx);
        cam_setf(v + 0x14, vy);
        cam_setf(G::camera_pos, px);
        cam_setf(G::camera_pos + 4, py);
        cam_setf(G::camera_pos + 8, pz);
    }
    mem.set32(h + 0xC, n);
}

// 0x6CE60 (CCTB 33 mode 6): walk the deferred segment from its start
template <class P> void Game<P>::cctb33_mode6_start()
{
    const uint32_t start = mem.u32(G::cam_deferred_start);
    mem.set32(G::camera_mode_routine, F::cctb33_mode6_walk);
    mem.set32(G::cam_mode6_first, start);
    cctb33_mode6_walk();
}

// 0x6CE90, per frame: until the segment's end, one frame on while players are in and the screen does not shake; the
// stop record at that frame (camera_path_eval, camera_look_at), camera_pos = the floor under its eye (floor_probe),
// heading = its yaw; frames left
template <class P> void Game<P>::cctb33_mode6_walk()
{
    LocalScope scope(*this);
    const uint32_t loc = local_alloc(0x30);
    const int32_t s = int32_t(mem.u32(G::cam_deferred_start)), e = int32_t(mem.u32(G::cam_deferred_end));
    if (!(s >= e)) {
        int32_t n = s;
        if (mem.u32(G::shake_timer) == 0 && mem.at<uint16_t>(G::players_in_game) != 0)
            n = s + 1;
        const float f = float(n);
        const uint32_t sp = G::game_cam_stop_point;
        mem.set32(G::cam_deferred_start, uint32_t(n));
        cam_setf(G::game_cam_stop_frame, f);
        camera_path_eval(mem.u32(G::camera_path), fbits(f), sp, sp + 0x18, loc + 0x20, loc + 0x24);
        camera_look_at(sp, sp + 0x18, mem.u32(loc + 0x20));
        mem.set32(loc, mem.u32(sp));
        mem.set32(loc + 4, 0x47000000);
        mem.set32(loc + 8, mem.u32(sp + 8));
        floor_probe(loc, G::camera_pos, loc + 0x10, loc + 0x28);
        mem.set32(G::camera_pos + 0xC, 0);
        mem.set32(G::camera_heading, mem.u32(sp + 0x10));
        mem.set32(G::camera_heading + 4, 0);
    }
    mem.set32(G::camera_frames_left, mem.u32(G::cam_deferred_end) - mem.u32(G::cam_deferred_start));
}

// 0x6CF90 (CCTB 33 mode 7): walk the deferred segment
template <class P> void Game<P>::cctb33_mode7_start()
{
    mem.set32(G::camera_mode_routine, F::cctb33_mode7_walk);
    cctb33_mode7_walk();
}

// 0x6CFB0, per frame: as mode 6 up to and including the end frame; camera_pos = the eye at its own height
// (cam_mode7_eye_y 1) or at floor_y
template <class P> void Game<P>::cctb33_mode7_walk()
{
    LocalScope scope(*this);
    const uint32_t loc = local_alloc(0x10);
    const int32_t s = int32_t(mem.u32(G::cam_deferred_start)), e = int32_t(mem.u32(G::cam_deferred_end));
    if (!(s > e)) {
        int32_t n = s;
        if (mem.u32(G::shake_timer) == 0 && mem.at<uint16_t>(G::players_in_game) != 0)
            n = s + 1;
        const float f = float(n);
        const uint32_t sp = G::game_cam_stop_point;
        mem.set32(G::cam_deferred_start, uint32_t(n));
        cam_setf(G::game_cam_stop_frame, f);
        camera_path_eval(mem.u32(G::camera_path), fbits(f), sp, sp + 0x18, loc, loc + 4);
        camera_look_at(sp, sp + 0x18, mem.u32(loc));
        const uint32_t y = mem.u32(G::cam_mode7_eye_y) == 1 ? mem.u32(sp + 4) : mem.u32(G::floor_y);
        mem.set32(G::camera_pos, mem.u32(sp));
        mem.set32(G::camera_pos + 4, y);
        mem.set32(G::camera_pos + 8, mem.u32(sp + 8));
        mem.set32(G::camera_pos + 0xC, 0);
        mem.set32(G::camera_heading, mem.u32(sp + 0x10));
        mem.set32(G::camera_heading + 4, 0);
    }
    mem.set32(G::camera_frames_left, mem.u32(G::cam_deferred_end) - mem.u32(G::cam_deferred_start));
}

// 0x6D2B0 (slot): camera_pos and camera_heading for the record's view (coprocessor): with the record's angles and
// its preset angles (+0xE0 pitch, -(+0xE4) yaw), the direction of (1, 0, 0) gives the heading (0x0F, folded twice:
// the second time when yaw - pitch + 0x4000 has bit 15); (0, 1, 0) turned back by the heading gives camera_pos +0xC
// (0x0A); then from the record's view matrix, translated by (0, 0, +0xD0), pitch +0xE0, yaw -(+0xE4) and back by the
// preset point (+0xD4..+0xDC): camera_pos = that translation
template <class P> void Game<P>::camdr_mode3_view_pos(int slot)
{
    const uint32_t r = cam_rec(slot);
    if (pc_path()) {
        // PC Mode / Boss Mode: THOTD.EXE camdr_mode3_frame 0x4CA680 on the PC's matrices (its helpers block 0x4C87C0,
        // the heading 0x4C9D28 = pm::camera_heading; the record's view matrix: record 0's = the PC's stored 6; pmbody)
        pm::Matrix m;
        m.rotate_y(mem.u32(r + 0x10));
        m.rotate_x(mem.u32(r + 0xC));
        m.rotate_z(mem.u32(r + 0x14));
        m.rotate_x(mem.u32(r + 0xE0));
        m.rotate_y(0u - mem.u32(r + 0xE4));
        uint32_t h0 = 0, h1 = 0, c = 0;
        pm::camera_heading(m, mem.u32(r + 0x10), h0, h1, c);
        mem.set32(G::camera_heading, h0);
        mem.set32(G::camera_heading + 4, h1);
        mem.set32(G::camera_pos + 0xC, c);
        pm::Matrix v;
        if (slot == 0) {
            v = pm_m6();
        } else {
            const float eye[3] = {mem.f32(r), mem.f32(r + 4), mem.f32(r + 8)};
            pm::camera_matrix(eye, mem.u32(r + 0xC), mem.u32(r + 0x10), mem.u32(r + 0x14), v.e);
        }
        v.translate(0.0f, 0.0f, mem.f32(r + 0xD0));
        v.rotate_x(mem.u32(r + 0xE0));
        v.rotate_y(0u - mem.u32(r + 0xE4));
        v.translate(-mem.f32(r + 0xD4), -mem.f32(r + 0xD8), -mem.f32(r + 0xDC));
        for (uint32_t k = 0; k < 3; ++k)
            mem.set32(G::camera_pos + 4 * k, fbits(v.e[9 + k]));
        return;
    }
    cam_copro(0x21, {});
    cam_copro(0x15, {mem.u32(r + 0x10)});
    cam_copro(0x14, {mem.u32(r + 0xC)});
    cam_copro(0x16, {mem.u32(r + 0x14)});
    cam_copro(0x14, {mem.u32(r + 0xE0)});
    cam_copro(0x15, {0u - mem.u32(r + 0xE4)});
    const uint32_t yaw = mem.u32(r + 0x10);
    cam_copro(0x05, {});
    cam_copro(0x88, {0, 0, 0});
    Tgp::Out o = cam_copro(0x1A, {fbits(1.0f), 0, 0});
    Tgp::Out q = cam_copro(0x0F, {o.w[1], o.w[2], o.w[0]});
    uint32_t a0 = q.w[0], a1 = q.w[1];
    fixAngles(a0, a1);
    if (((yaw - (a1 & 0xFFFF)) + 0x4000) & 0x8000) {
        a1 = 0x8000 - a1;
        a0 = a0 + 0x8000;
    }
    mem.set32(G::camera_heading, a1);
    mem.set32(G::camera_heading + 4, a0);
    Tgp::Out w = cam_copro(0x1A, {0, fbits(1.0f), 0});
    cam_copro(0x10, {});
    cam_copro(0x15, {0u - mem.u32(G::camera_heading)});
    cam_copro(0x16, {0u - mem.u32(G::camera_heading + 4)});
    Tgp::Out u = cam_copro(0x1A, {w.w[0], w.w[1], w.w[2]});
    Tgp::Out a = cam_copro(0x0A, {u.w[1], u.w[2] ^ 0x80000000u});
    cam_copro(0x06, {});
    uint32_t m[12];
    for (uint32_t i = 0; i < 12; ++i)
        m[i] = mem.u32(G::camera_matrices + 0x30 + uint32_t(slot) * 0x154 + 4 * i);
    Tgp::Out none;
    copro(0x07, m, 12, none);
    mem.set32(G::camera_pos + 0xC, a.w[0]);
    cam_copro(0x12, {0, 0, mem.u32(r + 0xD0)});
    cam_copro(0x14, {mem.u32(r + 0xE0)});
    cam_copro(0x15, {0u - mem.u32(r + 0xE4)});
    cam_copro(0x12, {mem.u32(r + 0xD4) ^ 0x80000000u, mem.u32(r + 0xD8) ^ 0x80000000u, mem.u32(r + 0xDC) ^ 0x80000000u});
    Tgp::Out v = cam_copro(0x20, {});
    cam_copro(0x06, {});
    mem.set32(G::camera_pos, v.w[0]);
    mem.set32(G::camera_pos + 4, v.w[1]);
    mem.set32(G::camera_pos + 8, v.w[2]);
}

// 0x6D990 (CAMDR 1, per frame): camera_pos (and its +0xC angle) and camera_heading at the one player's body, or
// halfway between both bodies (angles: half the signed 16-bit difference, truncated)
template <class P> void Game<P>::camdr_mode1_frame()
{
    const uint32_t b0 = mem.at<Player>(G::players, 0).body.a, b1 = mem.at<Player>(G::players, 1).body.a;
    uint32_t h0 = mem.u32(G::camera_heading), h1 = mem.u32(G::camera_heading + 4);
    if (mem.at<int16_t>(G::players_in_game) == 1) {
        const uint32_t b = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(G::solo_player_is_p2)))).body.a;
        for (uint32_t i = 0; i < 0x10; i += 4)
            mem.set32(G::camera_pos + i, mem.u32(b + 0x20 + i));
        h0 = mem.u32(b + 0x30);
        h1 = mem.u32(b + 0x34);
    } else {
        auto half = [](uint32_t a, uint32_t b) {
            int32_t d = int32_t((b - a) & 0xFFFF);
            if (d > 0x7FFF)
                d -= 0x10000;
            return a + uint32_t(d / 2);
        };
        const float x = (mem.f32(b1 + 0x20) + mem.f32(b0 + 0x20)) * 0.5f;
        const float y = (mem.f32(b1 + 0x24) + mem.f32(b0 + 0x24)) * 0.5f;
        const float z = (mem.f32(b1 + 0x28) + mem.f32(b0 + 0x28)) * 0.5f;
        const uint32_t a = half(mem.u32(b0 + 0x2C), mem.u32(b1 + 0x2C));
        cam_setf(G::camera_pos, x);
        cam_setf(G::camera_pos + 4, y);
        cam_setf(G::camera_pos + 8, z);
        mem.set32(G::camera_pos + 0xC, a);
        h0 = half(mem.u32(b0 + 0x30), mem.u32(b1 + 0x30));
        h1 = half(mem.u32(b0 + 0x34), mem.u32(b1 + 0x34));
    }
    mem.set32(G::camera_heading, h0);
    mem.set32(G::camera_heading + 4, h1);
}

} // namespace hotd::port
