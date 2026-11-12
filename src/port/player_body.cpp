// The players' character bodies (Rogan / G) in the cut-scenes (docs/teaser_status/r4.md): set up by
// player_char_models_set (0x95530, chapter_load), drawn by the camera-drive body routines (player +0xAC:
// player_body_draw_camdr, player_body_camdr, player_body_hold) and placed by the scene routines (player +0xB0: the
// CCTB 33 / CAMDR bodies at the camera, the opening's run from the car, player_scene00_kousya_run ...). Arcade forms
// only, read from the disassembly; proven except the routines marked UNVERIFIED in decl/player_body.h.
#include "port/game.h"

namespace hotd::port {

namespace pb {
constexpr uint32_t body_types = 0x51F438;        // int32 x 2: the two players' body types (player_char_models_set)
constexpr uint32_t char_pair = 0x51F42C;         // which pair of characters (table 0xCA690, 8 bytes a pair)
constexpr uint32_t char_types = 0xCA690;
constexpr uint32_t skin_types = 0x20E40;          // int16 per type: the skin template (-1 none)
constexpr uint32_t at_camera_offsets = 0xC0CA8;   // float [players_in_game_b][player]: the body's x offset from the camera
constexpr uint32_t place_offsets = 0xC0CB8;       // float [0x51F4A8][player] (player_scene_pos_table)
constexpr uint32_t run_start = 0x543390;          // set by the event script: the opening's run starts
constexpr uint32_t run_pos_x = 0xC0CB8, run_pos_z = 0xC0CC0;   // [players_in_game][player] x 0xC
constexpr uint32_t run2_pos_x = 0xC0CE8, run2_pos_z = 0xC0CF0;
constexpr uint32_t run2_motion = 0xC0D30, run2_step_frame = 0xC0D38;
} // namespace pb

// 0x95530 player_char_models_set: both players' bodies (0x3C8-byte objects) with the pair's types, motions 0x1CE /
// 0x1D2, slots 14 / 15 in player_bodies, the skin template when the type has one. PC Mode (THOTD.EXE 0x50C114): the
// types are the seats' characters' (+0x10 of table 0x5CE348), not the title code's pair; 4P with only players 3 / 4
// in play (coop4_body_set's set B): their characters
template <class P> void Game<P>::player_char_models_set()
{
    bool setB = false;
    if constexpr (Seats<P>::max == 4)
        setB = coop4_on() && mem.u32(coop4::kMagic) == 0x34504334u && mem.at<uint8_t>(coop4::kBodySetB) != 0;
    for (uint32_t p = 0; p < 2; ++p) {
        const uint32_t body = mem_alloc(0x3C8);
        int32_t type = int32_t(mem.u32(pb::char_types + mem.u32(pb::char_pair) * 8 + p * 4));
        if constexpr (!P::pc)
            if (chars_on())
                type = pm_body_type(int(p) + (setB ? 2 : 0));
        mem.set32(pb::body_types + p * 4, uint32_t(type));
        mem.at<Player>(G::players, p).body.a = body;
        body_init(body, type, p == 0 ? 0x1CE : 0x1D2);
        Obj& o = mem.at<Obj>(body);
        mem.set32(body_slot_addr(int32_t(o.slot)), 0);   // (G::player_bodies + slot x 4; Survival: 16.. its own)
        o.slot = int8_t(14 + p);
        mem.set32(0x51F478 + p * 4, body);
        if (mem.at<int16_t>(pb::skin_types + uint32_t(int32_t(o.type)) * 2) != -1)
            skin_template_copy(body);
    }
}

// the coprocessor sequence of player_body_at_camera / player_body_place: the camera position (0x88, translation),
// its z / y / x turns (0x16 / 0x15 / 0x14 with 0x51F264 / 0x51F260 / 0x51F25C), then (offset, 0, 0) through it (0x1A):
// the body stands at the camera, `offset` to the side, and turns with it
template <class P> void Game<P>::player_body_at(uint32_t task, uint32_t offset)
{
    cam_copro(0x21, {});   // (the command port 0x880210: push, identity)
    const uint32_t body = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20)))).body.a;
    cam_copro(0x88, {mem.u32(G::camera_pos), mem.u32(G::camera_pos + 4), mem.u32(G::camera_pos + 8)});
    cam_copro(0x16, {mem.u32(0x51F264)});
    cam_copro(0x15, {mem.u32(0x51F260)});
    cam_copro(0x14, {mem.u32(0x51F25C)});
    const Tgp::Out o = cam_copro(0x1A, {offset, 0, 0});
    Obj& b = mem.at<Obj>(body);
    b.pos[0] = ffrom(o.w[0]);
    b.pos[1] = ffrom(o.w[1]);
    cam_copro(0x06, {});   // (0x880060: pop)
    b.ang[0] = int32_t(mem.u32(0x51F25C));
    b.ang[1] = int32_t(mem.u32(0x51F260));
    b.ang[2] = int32_t(mem.u32(0x51F264));
    b.pos[2] = ffrom(o.w[2]);
}

// 0x4B850 player_body_at_camera (camdr_mode2_body 0x4B810, camdr_mode3_body 0x4B820, cctb33_mode4_body 0x4B830 and
// cctb33_body_follow 0x4B840 only call it): the offset by players_in_game_b and the player
template <class P> void Game<P>::player_body_at_camera(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    player_body_at(task, mem.u32(pb::at_camera_offsets + uint32_t(int32_t(mem.at<int16_t>(G::players_in_game_b))) * 8 + p * 4));
}

// 0x4B980 player_body_place: the offset by 0x51F4A8 and the player
template <class P> void Game<P>::player_body_place(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    player_body_at(task, mem.u32(pb::place_offsets + mem.u32(0x51F4A8) * 8 + p * 4));
}

// 0x6240 player_body_draw_camdr: drawn while the player's CCTB 16 flag (cam_flags bit 0) is set; its clock runs on
template <class P> void Game<P>::player_body_draw_camdr(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    cam_copro(0x05, {});   // (0x880050: push)
    Player& pl = mem.at<Player>(G::players, p);
    if (pl.cam_flags & 1)
        body_draw(pl.body.a);
    mem.at<Obj>(mem.at<Player>(G::players, p).body.a).anim_time += 1;
    cam_copro(0x06, {});   // (0x880060: pop)
}

// 0x62C0 player_body_camdr: drawn; at the motion's end it holds its last frame (player_body_hold)
template <class P> void Game<P>::player_body_camdr(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t body = mem.at<Player>(G::players, p).body.a;
    body_draw(body);
    Obj& o = mem.at<Obj>(body);
    if ((o.motion_flags >> 4 & 1) == 0) {
        o.anim_time += 1;
        return;
    }
    const uint16_t n = mem.at<uint16_t>(G::motion_frame_counts + uint32_t(int32_t(o.motion)) * 4);
    o.frame = int16_t(n - 2);
    o.anim_time = int32_t(int16_t(n - 2));
    mem.at<Player>(G::players, p).body_routine = 0x6330;
}

// 0x6330 player_body_hold
template <class P> void Game<P>::player_body_hold(uint32_t task)
{
    body_draw(mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20)))).body.a);
}

// 0x4BAB0 player_scene00_kousya_run: the opening (the school yard, "kousya"): waits for the script's start (0x543390)
template <class P> void Game<P>::player_scene00_kousya_run(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    mem.set32(pb::run_start, 0);
    pl.scene_routine = 0x4BAF0;
    pl.cam_flags = uint8_t(pl.cam_flags & 0xFE);
}

// 0x4BAF0: at the start, the body at its place (table by players_in_game and player, y -25), shown, motion 0x168
template <class P> void Game<P>::player_scene00_kousya_run_p(uint32_t task)
{
    if (mem.u32(pb::run_start) == 0)
        return;
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t at = p * 0xC + uint32_t(int32_t(mem.at<int16_t>(G::players_in_game))) * 0x18;
    Player& pl = mem.at<Player>(G::players, p);
    Obj& o = mem.at<Obj>(pl.body.a);
    o.ang[0] = o.ang[1] = o.ang[2] = 0;
    o.pos[1] = -25.0f;
    o.pos[0] = mem.f32(pb::run_pos_x + at);
    o.pos[2] = mem.f32(pb::run_pos_z + at);
    pl.cam_flags = uint8_t(pl.cam_flags | 1);
    set_motion(pl.body.a, 0x168);
    mem.at<Player>(G::players, p).scene_routine = 0x4BBA0;
}

// 0x4BBA0: at the motion's end, 90 frames of the next step
template <class P> void Game<P>::player_scene00_kousya_run_p_p1(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    if ((mem.at<Obj>(pl.body.a).motion_flags >> 4 & 1) != 0) {
        pl.timer = 0x5A;
        pl.scene_routine = 0x4BBE0;
    }
}

// 0x4BBE0: on its first frame the body at its second place (y -26, turned -0xE00) with the player's motion; a step
// sound at the table's frame; after 90 frames motion 0x173, a quarter turn, a random start frame, then 0x4BD00
template <class P> void Game<P>::player_scene00_kousya_run_p_p1_p(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t body = mem.at<Player>(G::players, p).body.a;
    if (mem.at<Player>(G::players, p).timer == 0x5A) {
        const uint32_t at = p * 0xC + uint32_t(int32_t(mem.at<int16_t>(G::players_in_game))) * 0x18;
        Obj& o = mem.at<Obj>(body);
        o.pos[1] = -26.0f;
        o.pos[0] = mem.f32(pb::run2_pos_x + at);
        o.ang[1] = -0xE00;
        o.pos[2] = mem.f32(pb::run2_pos_z + at);
        set_motion(body, uint32_t(int16_t(mem.u32(pb::run2_motion + p * 4))));
    }
    const int32_t f = mem.at<Obj>(body).frame;
    const int32_t step = int32_t(mem.u32(pb::run2_step_frame + p * 4));
    if (step <= f && f <= step)
        sound_cue(0xA01209);
    Player& pl = mem.at<Player>(G::players, p);
    if (--pl.timer == 0) {
        const uint32_t r = rand();
        set_motion(body, 0x173);
        Obj& o = mem.at<Obj>(body);
        o.ang[1] += 0x4000;
        o.anim_time = int32_t(p + 1) * (int32_t(r) % 0xE);
        mem.at<Player>(G::players, p).scene_routine = 0x4BD00;
    }
}

// 0x4BD00 (unnamed, after the run): at camera frame 450 (0x51EB9C) the motion by players_in_game and player (table
// 0xC0D38), blended over 10 frames; then 0x4BD70
template <class P> void Game<P>::player_scene00_turn(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    if (mem.f32(0x51EB9C) != 450.0f)
        return;
    const uint32_t motion = mem.u32(pb::run2_step_frame + uint32_t(int32_t(mem.at<int16_t>(G::players_in_game))) * 8 + p * 4);
    set_motion_blend(mem.at<Player>(G::players, p).body.a, motion, 0, 10);
    mem.at<Player>(G::players, p).scene_routine = 0x4BD70;
}

// 0x4BD70: at the motion's second-last frame, 0x4BDB0
template <class P> void Game<P>::player_scene00_turn_end(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    const Obj& o = mem.at<Obj>(pl.body.a);
    if (int32_t(o.frame) == int32_t(mem.u32(G::motion_frame_counts + uint32_t(int32_t(o.motion)) * 4)) - 2)
        pl.scene_routine = 0x4BDB0;
}

// 0x4BDB0: holds the motion's second-last frame
template <class P> void Game<P>::player_scene00_hold(uint32_t task)
{
    Obj& o = mem.at<Obj>(mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20)))).body.a);
    o.anim_time = int32_t(mem.u32(G::motion_frame_counts + uint32_t(int32_t(o.motion)) * 4)) - 2;
}

// --- player_scene02_shoot_three (0x4C0D0..0x4C390): the players shoot three zombies in a cut-scene (event 4) ---
namespace pb {
constexpr uint32_t shot_count = 0x543350;           // frames since the shooting motion started
constexpr uint32_t saved_pose = 0x543360;           // per player 0x18: pos x / y / z, ang x, then ang y / z at +0x10
constexpr uint32_t three_pos = 0xC0D68;             // [players_in_game][player] x 0xC: x, y, z
constexpr uint32_t three_p2_x = 0xC0D98;            // player 2 of two: x (by player x 0xC)
constexpr uint32_t three_p2_motion = 0xA86E0, three_motion = 0xA86D8;   // int32 by player
constexpr uint32_t finish_pos = 0xC0E88, finish_motion = 0xC0EC8;
} // namespace pb

// 0x4C0D0: hidden, 60 frames, then 0x4C110
template <class P> void Game<P>::player_scene02_shoot_three(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    pl.timer = 0x3C;
    pl.scene_routine = 0x4C110;
    pl.cam_flags = uint8_t(pl.cam_flags & 0xFE);
}

// 0x4C110: after 60 frames shown; player 2 of two stands aside (its own place and motion, then idle 0x4C390), the
// others take the shooting scene (0x4BFB0)
template <class P> void Game<P>::player_scene02_shoot_three_p(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t body = mem.at<Player>(G::players, p).body.a;
    if (--mem.at<Player>(G::players, p).timer != 0)
        return;
    const int32_t pig = mem.at<int16_t>(G::players_in_game);
    if (pig == 2 && p == 1) {
        Obj& o = mem.at<Obj>(body);
        const uint32_t at = p * 0xC + uint32_t(pig) * 0x18;
        o.pos[0] = mem.f32(pb::three_p2_x + p * 0xC);
        o.pos[1] = mem.f32(pb::three_pos + 4 + at);
        o.ang[1] += 0x7000;
        o.pos[2] = mem.f32(pb::three_pos + 8 + at);
        set_motion(body, uint32_t(int16_t(mem.u32(pb::three_p2_motion + p * 4))));
        mem.at<Player>(G::players, p).scene_routine = 0x4C390;
    } else {
        player_scene02_shoot_three_p_sub(task);
    }
    Player& pl = mem.at<Player>(G::players, p);
    pl.cam_flags = uint8_t(pl.cam_flags | 1);
}

// 0x4BFB0: the cut-scene camera (record 1: eye / look-at, camera_look_at, view 1); the body 10 units back, walking
// in over 60 frames (0x4C210)
template <class P> void Game<P>::player_scene02_shoot_three_p_sub(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t rec = G::camera_records + 0x154;
    mem.at<float>(rec + 0x18) = 5.1039f;
    mem.at<float>(rec + 0) = -33.1543f;
    mem.at<float>(rec + 4) = 3.5883f;
    mem.at<float>(rec + 8) = -103.0485f;
    mem.at<float>(rec + 0x1C) = 16.2116f;
    mem.at<float>(rec + 0x20) = -84.1157f;
    const uint32_t body = mem.at<Player>(G::players, p).body.a;
    camera_look_at(rec, rec + 0x18, 0);
    mem.set32(G::camera_view, 1);
    Player& pl = mem.at<Player>(G::players, p);
    pl.scene_routine = 0x4C210;
    pl.timer = 0x3C;
    const uint32_t at = p * 0xC + uint32_t(int32_t(mem.at<int16_t>(G::players_in_game))) * 0x18;
    Obj& o = mem.at<Obj>(body);
    o.pos[0] = mem.f32(pb::three_pos + at);
    o.pos[1] = mem.f32(pb::three_pos + 4 + at);
    o.pos[2] = mem.f32(pb::three_pos + 8 + at) - 9.999999f;
}

// 0x4C210: 1/6 unit a frame forward; after 60 frames the shooting motion (0x4C280)
template <class P> void Game<P>::player_scene02_shoot_three_p_sub_p(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    Player& pl = mem.at<Player>(G::players, p);
    mem.at<Obj>(pl.body.a).pos[2] += 0.16666666f;
    if (--pl.timer == 0) {
        pl.scene_routine = 0x4C280;
        set_motion(pl.body.a, uint32_t(int16_t(mem.u32(pb::three_motion + p * 4))));
        mem.set32(pb::shot_count, 0);
    }
}

// 0x4C280: three shots (frames 45, 77, 108: muzzle flash 0 and the gunshot sound by the byte 0x51F280 + p x 0xCC); at
// motion frame 156 event flag 0 is set (the script goes on), then 0x4C320
template <class P> void Game<P>::player_scene02_shoot_three_p_sub_p_p(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t n = mem.u32(pb::shot_count) + 1;
    mem.set32(pb::shot_count, n);
    const uint32_t body = mem.at<Player>(G::players, p).body.a;
    if (n == 0x2D || n == 0x4D || n == 0x6C) {
        muzzle_flash_spawn(0);
        sound_cue(shot_cue(mem.at<uint8_t>(0x51F280 + p * 0xCC) == 0 ? 0 : 1));   // (charsel4: by look_seat)
    }
    if (mem.at<Obj>(body).frame == 0x9C) {
        mem.at<Player>(G::players, p).scene_routine = 0x4C320;
        mem.at<uint8_t>(G::event_flags) = 1;
    }
}

// 0x4C320: at the motion's end the player's camera-drive motion (camdr_motions) and the pose saved (0x543360)
template <class P> void Game<P>::player_scene02_shoot_three_p_sub_p_p_p(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t body = mem.at<Player>(G::players, p).body.a;
    if ((mem.at<Obj>(body).motion_flags >> 4 & 1) == 0)
        return;
    mem.at<Player>(G::players, p).scene_routine = 0x4C390;
    set_motion(body, uint32_t(mem.at<int16_t>(G::camdr_motions + p * 2)));
    mem.set32(pb::shot_count, 0);
    for (uint32_t k = 0; k < 16; k += 4)
        mem.set32(pb::saved_pose + p * 0x18 + k, mem.u32(body + 0x20 + k));
    for (uint32_t k = 0; k < 8; k += 4)
        mem.set32(pb::saved_pose + p * 0x18 + 0x10 + k, mem.u32(body + 0x30 + k));
}

// --- player_scene08_finish_pose (0x4CEE0..0x4D090): the pose at the end of the Chapter 1 boss (event 19) ---

// 0x4CEE0: the place and motion by players_in_game and player, facing 0x56C0
template <class P> void Game<P>::player_scene08_finish_pose(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const int32_t pig = mem.at<int16_t>(G::players_in_game);
    const uint32_t at = p * 0xC + uint32_t(pig) * 0x18;
    const uint32_t body = mem.at<Player>(G::players, p).body.a;
    Obj& o = mem.at<Obj>(body);
    o.pos[0] = mem.f32(pb::finish_pos + at);
    o.pos[1] = mem.f32(pb::finish_pos + 4 + at);
    o.ang[0] = 0;
    o.ang[2] = 0;
    o.pos[2] = mem.f32(pb::finish_pos + 8 + at);
    set_motion(body, uint32_t(int16_t(mem.u32(pb::finish_motion + uint32_t(pig) * 8 + p * 4))));
    mem.at<Obj>(body).ang[1] = 0x56C0;
    mem.at<Player>(G::players, p).scene_routine = 0x4CFB0;
}

// 0x4CFB0: at the motion's end motion 0x173 (5-frame blend)
template <class P> void Game<P>::player_scene08_finish_pose_p(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t body = mem.at<Player>(G::players, p).body.a;
    if ((mem.at<Obj>(body).motion_flags >> 4 & 1) == 0)
        return;
    set_motion_blend(body, 0x173, 0, 5);
    mem.at<Player>(G::players, p).scene_routine = 0x4CFF0;
}

// 0x4CFF0: at camera frame 80, motion 0x177 from frame 100
template <class P> void Game<P>::player_scene08_finish_pose_p_p(uint32_t task)
{
    if (mem.f32(G::camera_records + 0xEC) != 80.0f)
        return;
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    set_motion_blend(mem.at<Player>(G::players, p).body.a, 0x177, 100, 5);
    mem.at<Player>(G::players, p).scene_routine = 0x4D050;
}

// 0x4D050: at anim_time 110, held at 109 (0x4D090)
template <class P> void Game<P>::player_scene08_finish_pose_p_p_p(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    Obj& o = mem.at<Obj>(pl.body.a);
    if (o.anim_time == 0x6E) {
        o.anim_time = 0x6D;
        pl.scene_routine = 0x4D090;
    }
}

// 0x4D090
template <class P> void Game<P>::player_scene08_finish_pose_p_p_p_p(uint32_t task)
{
    mem.at<Obj>(mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20)))).body.a).anim_time = 0x6D;
}

// --- the branch events' player cut-scenes 03-07 (0x4C3A0..0x4CED0; T1, read from the decompilation and the
// disassembly's constants); proven in captures of routes 1:0+1:3, 1:5, 1:4+1:7 but the parts marked in the decl ---

// their common start: camera record 1's eye / look-at (bits), camera_look_at, view 1; the body at x / z from the
// scene's table (+ player x 0xC + players_in_game x 0x18), angles x / z 0
template <class P> uint32_t Game<P>::player_scene_start(uint32_t task, const uint32_t cam[6], uint32_t table)
{
    const uint32_t rec = G::camera_records + 0x154;
    for (uint32_t k = 0; k < 3; ++k) {
        mem.set32(rec + 4 * k, cam[k]);
        mem.set32(rec + 0x18 + 4 * k, cam[3 + k]);
    }
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t body = mem.at<Player>(G::players, p).body.a;
    camera_look_at(rec, rec + 0x18, 0);
    mem.set32(G::camera_view, 1);
    const uint32_t at = table + p * 0xC + uint32_t(int32_t(mem.at<int16_t>(G::players_in_game))) * 0x18;
    Obj& o = mem.at<Obj>(body);
    o.pos[0] = mem.f32(at);
    o.pos[1] = mem.f32(at + 4);
    o.ang[0] = 0;
    o.ang[2] = 0;
    o.pos[2] = mem.f32(at + 8);
    return body;
}

// 0x4C3A0 player_scene03_ladder: Rogan climbs (motion 0x160, 0x4C500); player 2 of two looks up (0x15F, anim 60,
// facing 0xA800, idle)
template <class P> void Game<P>::player_scene03_ladder(uint32_t task)
{
    static const uint32_t cam[6] = {0xC3A60000u, 0, 0x440B0000u, 0xC3AC0000u, 0xC2200000u, 0x440B0000u};
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t body = player_scene_start(task, cam, 0xC0D98);
    Player& pl = mem.at<Player>(G::players, p);
    if (mem.at<int16_t>(G::players_in_game) == 2 && p == 1) {
        set_motion(body, 0x15F);
        mem.at<Obj>(body).anim_time = 0x3C;
        mem.at<Obj>(body).ang[1] = 0xA800;
        pl.scene_routine = 0x4C540;
        return;
    }
    set_motion(body, 0x160);
    mem.at<Obj>(body).ang[1] = 0;
    pl.scene_routine = 0x4C500;
}

// 0x4C500: a rung (7.2164) up at every loop of the motion
template <class P> void Game<P>::player_scene03_ladder_p1(uint32_t task)
{
    Obj& o = mem.at<Obj>(mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20)))).body.a);
    if ((o.motion_flags >> 4) & 1)
        o.pos[1] = o.pos[1] + ffrom(0x40E6ECC0u);
}

// 0x4C550 player_scene04_fall_backside: y -75, facing 0x4000; Rogan sits down hard (0x174, sound 0xA01456, idle);
// player 2 of two jumps down (0x179, sound 0xA01459, then 0x4C6C0)
template <class P> void Game<P>::player_scene04_fall_backside(uint32_t task)
{
    static const uint32_t cam[6] = {0xC27BDE01u, 0xC21B03FEu, 0x43A55B02u, 0xC1C693DEu, 0xC28F53B6u, 0x43A82F9Eu};
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t body = player_scene_start(task, cam, 0xC0DC8);
    Obj& o = mem.at<Obj>(body);
    o.pos[1] = -75.0f;
    o.ang[1] = 0x4000;
    Player& pl = mem.at<Player>(G::players, p);
    if (mem.at<int16_t>(G::players_in_game) == 2 && p == 1) {
        set_motion(body, 0x179);
        pl.scene_routine = 0x4C6C0;
        sound_cue(0xA01459);
        return;
    }
    set_motion(body, 0x174);
    pl.scene_routine = 0x4C6B0;
    sound_cue(0xA01456);
}

// 0x4C6C0: at the motion's end 0xC1 (blend 20), idle
template <class P> void Game<P>::player_scene04_fall_backside_p2(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    if ((mem.at<Obj>(pl.body.a).motion_flags >> 4) & 1) {
        set_motion_blend(pl.body.a, 0xC1, 0, 0x14);
        pl.scene_routine = 0x4C6B0;
    }
}

// 0x4C700 player_scene05_knocked_flying: y 40, rising 0.5, facing 0xC000; player 1 0x156 (sound 0xA01457), player 2
// 0xBD (sound 0xA0145A); then 0x4C860
template <class P> void Game<P>::player_scene05_knocked_flying(uint32_t task)
{
    static const uint32_t cam[6] = {0xC239A752u, 0x4179075Fu, 0x43552106u, 0xC1A6ACA5u, 0x4217C9A0u, 0x43421646u};
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t body = player_scene_start(task, cam, 0xC0DF8);
    Obj& o = mem.at<Obj>(body);
    o.pos[1] = 40.0f;
    o.vel[1] = 0.5f;
    o.ang[1] = 0xC000;
    set_motion(body, p == 0 ? 0x156 : 0xBD);
    mem.at<Player>(G::players, p).scene_routine = 0x4C860;
    sound_cue(p == 0 ? 0xA01457 : 0xA0145A);
}

// 0x4C860 / 0x4C8E0 / 0x4C960: flying (gravity 0.0272, x - 0.04 a frame); at anim_time 149 held at 148; landed (y <= 0)
// it stays
template <class P> void Game<P>::player_scene05_knocked_flying_p(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    Obj& o = mem.at<Obj>(pl.body.a);
    const float vy = o.vel[1] - ffrom(0x3CDF0123u);
    const int32_t t = o.anim_time;
    o.vel[1] = vy;
    o.pos[0] = o.pos[0] - ffrom(0x3D23D70Au);
    o.pos[1] = vy + o.pos[1];
    if (t == 0x95) {
        o.anim_time = 0x94;
        pl.scene_routine = 0x4C8E0;
    }
}
template <class P> void Game<P>::player_scene05_knocked_flying_p_p(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    Obj& o = mem.at<Obj>(pl.body.a);
    const float vy = o.vel[1] - ffrom(0x3CDF0123u);
    const float y = vy + o.pos[1];
    o.vel[1] = vy;
    o.pos[0] = o.pos[0] - ffrom(0x3D23D70Au);
    o.pos[1] = y;
    if (y <= 0.0f)
        pl.scene_routine = 0x4C960;
    o.anim_time = 0x94;
}
template <class P> void Game<P>::player_scene05_knocked_flying_p_p_p(uint32_t task)
{
    mem.at<Obj>(mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20)))).body.a).anim_time = 0x94;
}

// 0x4C980 player_scene06_reload_shoot: y 30.048; player 2 of two reloads (0xC3, facing 0x9000, 0x4CB70); the shooter
// shoots (0x1CF, facing 0xC000, 0x4CAD0)
template <class P> void Game<P>::player_scene06_reload_shoot(uint32_t task)
{
    static const uint32_t cam[6] = {0x42E5A12Du, 0x424C6B51u, 0x43A9D354u, 0x42FE04D0u, 0x4234020Cu, 0x43B3E0A4u};
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t body = player_scene_start(task, cam, 0xC0E28);
    Obj& o = mem.at<Obj>(body);
    o.pos[1] = ffrom(0x41F0624Eu);
    Player& pl = mem.at<Player>(G::players, p);
    if (mem.at<int16_t>(G::players_in_game) == 2 && p == 1) {
        set_motion(body, 0xC3);
        mem.at<Obj>(body).ang[1] = 0x9000;
        pl.scene_routine = 0x4CB70;
        return;
    }
    mem.set32(pb::shot_count, 0);
    set_motion(body, 0x1CF);
    mem.at<Obj>(body).ang[1] = 0xC000;
    pl.scene_routine = 0x4CAD0;
}

// 0x4CAD0: three shots (frames 45, 77, 108: muzzle flash 1, the gunshot by the character); at frame 0xD6 event flag
// 0x22, idle
template <class P> void Game<P>::player_scene06_reload_shoot_p1(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t n = mem.u32(pb::shot_count) + 1;
    mem.set32(pb::shot_count, n);
    const uint32_t body = mem.at<Player>(G::players, p).body.a;
    if (n == 0x2D || n == 0x4D || n == 0x6C) {
        muzzle_flash_spawn(1);
        sound_cue(shot_cue(mem.at<uint8_t>(0x51F280 + p * 0xCC) == 0 ? 0 : 1));   // (charsel4: by look_seat)
    }
    if (mem.at<Obj>(body).frame == 0xD6) {
        mem.at<Player>(G::players, p).scene_routine = 0x4CBF0;
        mem.at<uint8_t>(G::event_flags + 0x22) = 1;
    }
}

// 0x4CB70 / 0x4CBB0: at the motion's end 0xBE, then 0xC4, idle
template <class P> void Game<P>::player_scene06_reload_shoot_p2(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    if ((mem.at<Obj>(pl.body.a).motion_flags >> 4) & 1) {
        set_motion(pl.body.a, 0xBE);
        pl.scene_routine = 0x4CBB0;
    }
}
template <class P> void Game<P>::player_scene06_reload_shoot_p2_p(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    if ((mem.at<Obj>(pl.body.a).motion_flags >> 4) & 1) {
        set_motion(pl.body.a, 0xC4);
        pl.scene_routine = 0x4CBF0;
    }
}

// 0x4CC00 player_scene07_jump_down_a: y 30.048, facing 0xC000; player 2 of two waits (0xC4, anim 60, idle); the
// jumper 0x178 (type 0x22: 400), then 0x4CD60
template <class P> void Game<P>::player_scene07_jump_down_a(uint32_t task)
{
    static const uint32_t cam[6] = {0x432C991Du, 0x415CBD3Cu, 0x43AE41CBu, 0x4310AE6Au, 0x420F8952u, 0x43B5A20Cu};
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t body = player_scene_start(task, cam, 0xC0E58);
    Obj& o = mem.at<Obj>(body);
    o.pos[1] = ffrom(0x41F0624Eu);
    o.ang[1] = 0xC000;
    Player& pl = mem.at<Player>(G::players, p);
    if (mem.at<int16_t>(G::players_in_game) == 2 && p == 1) {
        set_motion(body, 0xC4);
        mem.at<Obj>(body).anim_time = 0x3C;
        pl.scene_routine = 0x4CED0;
        return;
    }
    set_motion(body, mem.at<Obj>(body).type == 0x22 ? 400 : 0x178);
    pl.scene_routine = 0x4CD60;
}

// 0x4CD60: at anim_time 32 vy 0, 0x4CDA0
template <class P> void Game<P>::player_scene07_jump_down_a_p1(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    Obj& o = mem.at<Obj>(pl.body.a);
    if (o.anim_time == 0x20) {
        o.vel[1] = 0.0f;
        pl.scene_routine = 0x4CDA0;
    }
}

// 0x4CDA0: x + 0.2 a frame; at anim_time 50 vy 0, 0x4CDF0
template <class P> void Game<P>::player_scene07_jump_down_a_p1_p(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    Obj& o = mem.at<Obj>(pl.body.a);
    o.pos[0] = o.pos[0] + ffrom(0x3E4CCCCDu);
    if (o.anim_time == 0x32) {
        o.vel[1] = 0.0f;
        pl.scene_routine = 0x4CDF0;
    }
}

// 0x4CDF0 / 0x4CE70: falling (gravity 0.0272, x + 0.2); at anim_time 96 held at 95
template <class P> void Game<P>::player_scene07_jump_down_a_p1_p_p(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    Obj& o = mem.at<Obj>(pl.body.a);
    const float vy = o.vel[1] - ffrom(0x3CDF0123u);
    const int32_t t = o.anim_time;
    o.vel[1] = vy;
    o.pos[0] = o.pos[0] + ffrom(0x3E4CCCCDu);
    o.pos[1] = vy + o.pos[1];
    if (t == 0x60) {
        o.anim_time = 0x5F;
        pl.scene_routine = 0x4CE70;
    }
}
template <class P> void Game<P>::player_scene07_jump_down_a_p1_p_p_p(uint32_t task)
{
    Obj& o = mem.at<Obj>(mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20)))).body.a);
    const float vy = o.vel[1] - ffrom(0x3CDF0123u);
    o.anim_time = 0x5F;
    o.vel[1] = vy;
    o.pos[0] = o.pos[0] + ffrom(0x3E4CCCCDu);
    o.pos[1] = vy + o.pos[1];
}

// --- Chapter 2's player cut-scenes 09-14 (0x4D0B0..0x4DA3F), read from the disassembly (the decompilation's float
// constants are rounded); proven on A (ch2proof) ---

// 0x4D0B0 player_scene09_stance_shoot: no camera; the body at the table 0xC0EC8; player 2 of two stands (0xBF,
// facing 0, 0x4D2E0 idle); the shooter shoots (0x1CF, facing 0xC000, 0x4D1A0)
template <class P> void Game<P>::player_scene09_stance_shoot(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const int32_t pig = mem.at<int16_t>(G::players_in_game);
    const uint32_t at = 0xC0EC8 + p * 0xC + uint32_t(pig) * 0x18;
    const uint32_t body = mem.at<Player>(G::players, p).body.a;
    Obj& o = mem.at<Obj>(body);
    o.pos[0] = mem.f32(at);
    o.pos[1] = mem.f32(at + 4);
    o.ang[0] = 0;
    o.ang[2] = 0;
    o.pos[2] = mem.f32(at + 8);
    Player& pl = mem.at<Player>(G::players, p);
    if (pig == 2 && p == 1) {
        set_motion(body, 0xBF);
        mem.at<Obj>(body).ang[1] = 0;
        pl.scene_routine = 0x4D2E0;
        return;
    }
    mem.set32(pb::shot_count, 0);
    set_motion(body, 0x1CF);
    mem.at<Obj>(body).ang[1] = 0xC000;
    pl.scene_routine = 0x4D1A0;
}

// 0x4D1A0: as 0x4CAD0 (three shots at 45, 77, 108; event flag 0x22 at frame 0xD6), then 0x4D240
template <class P> void Game<P>::player_scene09_stance_shoot_p1(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t n = mem.u32(pb::shot_count) + 1;
    mem.set32(pb::shot_count, n);
    const uint32_t body = mem.at<Player>(G::players, p).body.a;
    if (n == 0x2D || n == 0x4D || n == 0x6C) {
        muzzle_flash_spawn(1);
        sound_cue(shot_cue(mem.at<uint8_t>(0x51F280 + p * 0xCC) == 0 ? 0 : 1));   // (charsel4: by look_seat)
    }
    if (mem.at<Obj>(body).frame == 0xD6) {
        mem.at<Player>(G::players, p).scene_routine = 0x4D240;
        mem.at<uint8_t>(G::event_flags + 0x22) = 1;
    }
}

// 0x4D240: at the motion's end the gun stance 0x162 (blend 10)
template <class P> void Game<P>::player_scene09_stance_shoot_p1_p(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    if ((mem.at<Obj>(pl.body.a).motion_flags >> 4) & 1) {
        set_motion_blend(pl.body.a, 0x162, 0, 10);
        pl.scene_routine = 0x4D280;
    }
}

// 0x4D280 / 0x4D2C0: at anim_time 28 held at 27
template <class P> void Game<P>::player_scene09_stance_shoot_p1_p_p(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    Obj& o = mem.at<Obj>(pl.body.a);
    if (o.anim_time == 0x1C) {
        o.anim_time = 0x1B;
        pl.scene_routine = 0x4D2C0;
    }
}
template <class P> void Game<P>::player_scene09_stance_shoot_p1_p_p_p(uint32_t task)
{
    mem.at<Obj>(mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20)))).body.a).anim_time = 0x1B;
}

// the jump-downs 10 / 11 (as scene 07: player 2 of two waits, 0xC4 anim 60, 0x4CED0; the jumper 0x178 / 400 (type
// 0x22), 0x4CD60): the camera, the table, y; scene 11's waiting player 2 faces 0x8000
template <class P> void Game<P>::player_scene_jump_down(uint32_t task, const uint32_t cam[6], uint32_t table, uint32_t y,
                                                        int32_t wait_facing)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t body = player_scene_start(task, cam, table);
    Obj& o = mem.at<Obj>(body);
    o.pos[1] = ffrom(y);
    o.ang[1] = 0xC000;
    Player& pl = mem.at<Player>(G::players, p);
    if (mem.at<int16_t>(G::players_in_game) == 2 && p == 1) {
        o.ang[1] = wait_facing;
        set_motion(body, 0xC4);
        mem.at<Obj>(body).anim_time = 0x3C;
        pl.scene_routine = 0x4CED0;
        return;
    }
    set_motion(body, mem.at<Obj>(body).type == 0x22 ? 400 : 0x178);
    pl.scene_routine = 0x4CD60;
}

// 0x4D2F0 player_scene10_jump_down_b: y -20.783
template <class P> void Game<P>::player_scene10_jump_down_b(uint32_t task)
{
    static const uint32_t cam[6] = {0x444D8181u, 0xC20C1C5Du, 0x43E8076Du, 0x4449FE39u, 0xC142BC02u, 0x43E29852u};
    player_scene_jump_down(task, cam, 0xC0EF8, 0xC1A64396u, 0xC000);
}

// 0x4D450 player_scene11_jump_down_c: y -6.864
template <class P> void Game<P>::player_scene11_jump_down_c(uint32_t task)
{
    static const uint32_t cam[6] = {0x42094EA5u, 0xC19FF5F7u, 0xC0A1844Du, 0x41B386C2u, 0xBFFEE2EBu, 0x3D2F4F0Eu};
    player_scene_jump_down(task, cam, 0xC0F28, 0xC0DBA5E3u, 0x8000);
}

// 0x4D5C0 player_scene12_reload_kick: y -122.36; player 2 of two reloads (0xC3, facing 0x3800, 0x4CB70); Rogan kicks
// (0x1D1, facing 0, idle)
template <class P> void Game<P>::player_scene12_reload_kick(uint32_t task)
{
    static const uint32_t cam[6] = {0x440641A7u, 0xC2D0FF7Du, 0x433A4312u, 0x440BAE4Bu, 0xC2DB9E35u, 0x434A799Au};
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t body = player_scene_start(task, cam, 0xC0F58);
    mem.at<Obj>(body).pos[1] = ffrom(0xC2F4B852u);
    Player& pl = mem.at<Player>(G::players, p);
    if (mem.at<int16_t>(G::players_in_game) == 2 && p == 1) {
        set_motion(body, 0xC3);
        mem.at<Obj>(body).ang[1] = 0x3800;
        pl.scene_routine = 0x4CB70;
        return;
    }
    mem.set32(pb::shot_count, 0);
    set_motion(body, 0x1D1);
    mem.at<Obj>(body).ang[1] = 0;
    pl.scene_routine = 0x4CBF0;
}

// 0x4D710 player_scene13_knocked_flying_b: as scene 05 (y -20.813, rising 0.5, facing 0xC000; player 1 0x156, sound
// 0xA01457, player 2 0xBD, sound 0xA0145A; then 0x4C860)
template <class P> void Game<P>::player_scene13_knocked_flying_b(uint32_t task)
{
    static const uint32_t cam[6] = {0x43D35E07u, 0xC2217924u, 0x43125CEEu, 0x43DA01B7u, 0xC19852BDu, 0x4308AA7Fu};
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t body = player_scene_start(task, cam, 0xC0F88);
    Obj& o = mem.at<Obj>(body);
    o.pos[1] = ffrom(0xC1A68106u);
    o.vel[1] = 0.5f;
    o.ang[1] = 0xC000;
    set_motion(body, p == 0 ? 0x156 : 0xBD);
    mem.at<Player>(G::players, p).scene_routine = 0x4C860;
    sound_cue(p == 0 ? 0xA01457 : 0xA0145A);
}

// 0x4D870 player_scene14_rattle_door: the body at the table 0xC0FB8 (y too), facing 0; the motion from 0xC0FF8
// [players_in_game][player] (madgacha), then 0x4D9B0
template <class P> void Game<P>::player_scene14_rattle_door(uint32_t task)
{
    static const uint32_t cam[6] = {0x44B4FE1Du, 0xC207CF0Eu, 0xC43A14AEu, 0x44B7B1E3u, 0xC1ACD4CAu, 0xC440376Du};
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t body = player_scene_start(task, cam, 0xC0FB8);
    const int32_t pig = mem.at<int16_t>(G::players_in_game);
    set_motion(body, uint32_t(int16_t(mem.u32(0xC0FF8 + uint32_t(pig) * 8 + p * 4))));
    mem.at<Obj>(body).ang[1] = 0;
    mem.at<Player>(G::players, p).scene_routine = 0x4D9B0;
}

// 0x4D9B0: motions 0x170 / 0x171 rattle at frame 80 (sound 0xA01209); at anim_time 112 held at 111 (0x4DA20)
template <class P> void Game<P>::player_scene14_rattle_door_p(uint32_t task)
{
    Player& pl = mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20))));
    const uint32_t body = pl.body.a;
    {
        const Obj& o = mem.at<Obj>(body);
        if ((o.motion == 0x171 || o.motion == 0x170) && o.frame == 0x50)
            sound_cue(0xA01209);
    }
    Obj& o = mem.at<Obj>(body);
    if (o.anim_time == 0x70) {
        o.anim_time = 0x6F;
        pl.scene_routine = 0x4DA20;
    }
}
template <class P> void Game<P>::player_scene14_rattle_door_p_p(uint32_t task)
{
    mem.at<Obj>(mem.at<Player>(G::players, uint32_t(int32_t(mem.at<int8_t>(task + 0x20)))).body.a).anim_time = 0x6F;
}

} // namespace hotd::port
