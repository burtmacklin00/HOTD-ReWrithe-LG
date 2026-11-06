// Chapter 2's own kinds (worker "ch2", docs/chapter2_3_plan.md): the route sign 183, the double door 168, the
// sound repeat 75, the prop groups 131 / 133 / 136 / 151 / 152 and the other Chapter 2 scene kinds. Read from the
// arcade decompilation / disassembly; arcade forms only. Proven on A (ch2proof, docs/teaser_status/ch2proof.md) but where marked UNVERIFIED (teaser).
#include "port/game.h"

namespace hotd::port {

namespace c2 {
constexpr uint32_t sign_models = 0xA85A8;      // kind 183: model per +0x70
constexpr uint32_t door_models = 0xCA750;      // kind 168: (left, right) model per damage step
constexpr uint32_t door_open_flag = 0x51E962;  // kind 168 opens when this byte is 1
constexpr uint32_t card_done = 0x51EA38;       // kind177_chapter_card_frame sets it
constexpr uint32_t k131_z = 0xAFBC0;           // kind 131: z offsets (floats)
} // namespace c2

// --- kind 183 (Chapter 2 events 1 / 5): the shootable route sign ---

// 0x3A40 kind183_shootable_sign: the record (+0x35C) gives the end flag (+0), the choice flag (+1) and the route
// values for model 0x318 (+2) / 0x319 (+3). Set-up: model from 0xA85A8[+0x70], hit radius 2. Shot (flags bit 8): the
// sign turns over (0x318 <-> 0x319, sound 0xA01249); the choice flag set: route_choice by the model shown, not
// shootable any more. Its camera-space point, the depth list while shootable.
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind183_shootable_sign(uint32_t obj)
{
    const uint32_t rec = of<uint32_t>(obj, 0x35C);
    if (mem.at<uint8_t>(G::event_flags + mem.at<uint8_t>(rec)) == 1) {
        task_exit();
        return;
    }
    const int16_t s = of<int16_t>(obj, 0x60);
    bool active = false;
    if (s == 1) {
        active = true;
    } else if (s == 0) {
        of<float>(obj, 0x90) = 2.0f;
        of<int16_t>(obj, 0x60) = int16_t(s + 1);
        of<uint32_t>(obj, 0x54) = mem.u32(c2::sign_models + uint32_t(int32_t(of<int16_t>(obj, 0x70))) * 4);
        active = true;
    }
    if (active) {
        if (mem.at<uint8_t>(G::event_flags + mem.at<uint8_t>(rec + 1)) == 1) {
            const uint32_t m = of<uint32_t>(obj, 0x54);
            if (m == 0x318)
                mem.at<uint8_t>(G::route_choice) = mem.at<uint8_t>(rec + 2);
            else if (m == 0x319)
                mem.at<uint8_t>(G::route_choice) = mem.at<uint8_t>(rec + 3);
            of<uint32_t>(obj, 0x58) &= 0xFFFFFFFEu;
            of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        } else if ((of<uint32_t>(obj, 0x58) >> 8) & 1) {
            uint32_t m = of<uint32_t>(obj, 0x54);
            if (m == 0x318)
                m = 0x319;
            else if (m == 0x319)
                m = 0x318;
            of<uint32_t>(obj, 0x54) = m;
            sound_cue(0xA01249);
        }
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    geo_matrix_position(of<uint32_t>(obj, 0x54), obj + ob(0x38));
    cam_copro(0x06, {});
    if (of<uint32_t>(obj, 0x58) & 1) {
        of<uint32_t>(obj, 0x58) &= 0xFFFFFEFFu;
        depth_list_add(obj);
    }
}

// --- kind 168 (Chapter 2 event 0, scene object): the courtyard double door ---

// 0x96EC0 kind168_double_door: +0x50 counts frames. State 0: closed (turned 0x8000) until the chapter card is done
// (0x51EA38); state 1: the damage models step at frames 45 / 77 / 108, flag 0x51E962 opens it (sound 0xA01221);
// state 2: swings 0x400 a frame to 0x2000, then 120 frames (+0x31C) and the end. Both halves drawn (the tail 0x96FDC)
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind168_double_door(uint32_t obj)
{
    const int16_t s = of<int16_t>(obj, 0x60);
    of<int32_t>(obj, 0x50) += 1;
    if (s == 1) {
        if (mem.at<uint8_t>(c2::door_open_flag) == 1) {
            sound_cue(0xA01221);
            of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        }
        const int32_t t = of<int32_t>(obj, 0x50);
        if (t == 0x2D)
            of<int16_t>(obj, 0x70) = int16_t(of<int16_t>(obj, 0x70) + 1);
        if (t == 0x4D)
            of<int16_t>(obj, 0x70) = int16_t(of<int16_t>(obj, 0x70) + 1);
        if (t == 0x6C)
            of<int16_t>(obj, 0x70) = int16_t(of<int16_t>(obj, 0x70) + 1);
    } else if (s == 0) {
        if (mem.at<uint8_t>(c2::card_done) == 1) {
            of<int32_t>(obj, 0x50) = 0;
            of<int16_t>(obj, 0x70) = 0;
            of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        }
        of<uint32_t>(obj, 0x30) = 0x8000;
    } else if (s == 2) {
        const int32_t a = of<int32_t>(obj, 0x30) - 0x400;
        of<int32_t>(obj, 0x30) = a;
        if (a == 0x2000) {
            of<uint32_t>(obj, 0x31C) = 120;
            of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        }
    } else if (s == 3) {
        const int32_t n = of<int32_t>(obj, 0x31C) - 1;
        of<int32_t>(obj, 0x31C) = n;
        if (n == 0) {
            task_exit();
            return;
        }
    }
    kind168_double_door_tail(obj);
}

// 0x96FDC (a Ghidra split of the above): the halves at (-46.603, -6.869, -/+10.012), turned +/- the angle
template <class P> void Game<P>::kind168_double_door_tail(uint32_t obj)
{
    const uint32_t m = c2::door_models + uint32_t(int32_t(of<int16_t>(obj, 0x70))) * 8;
    const uint32_t a = of<uint32_t>(obj, 0x30);
    cam_copro(0x05, {});
    cam_copro(0x12, {0xC23A6944u, 0xC0DBCC64u, 0xC1203127u});
    cam_copro(0x15, {a});
    geo_draw_model(mem.u32(m));
    cam_copro(0x06, {});
    cam_copro(0x05, {});
    cam_copro(0x12, {0xC23A6944u, 0xC0DBCC64u, 0x41203127u});
    cam_copro(0x15, {0u - a});
    geo_draw_model(mem.u32(m + 4));
    cam_copro(0x06, {});
}

// --- kind 75 (Chapter 2 event 5, scene object) ---

// 0x92BE0 kind075_sound_repeat: sound 0xA01250 every 15 frames for 60 frames, then the end
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind075_sound_repeat(uint32_t obj)
{
    const int16_t s = of<int16_t>(obj, 0x60);
    if (s == 0) {
        of<int32_t>(obj, 0x50) = 60;
        of<int16_t>(obj, 0x60) = int16_t(s + 1);
    } else if (s != 1) {
        return;
    }
    const int32_t t = of<int32_t>(obj, 0x50) - 1;
    of<int32_t>(obj, 0x50) = t;
    if (t % 15 == 0)
        sound_cue(0xA01250);
    if (of<int32_t>(obj, 0x50) < 0)
        task_exit();
}

// --- prop group kinds ---

// 0x3AAA0 kind131_prop_group (kind 131, Chapter 2 event 9): eight default props of sub-type 51, models 0x149C + 2i,
// at (1210.34, -59.011, 249.749 + 0xAFBC0[i] - 14); the group task ends
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind131_prop_group(uint32_t obj)
{
    if (pc_path()) {   // THOTD.EXE's kind 131 is only `jmp task_exit` (as kinds 137 / 146 / 147 / 152 / 175, pmrest2)
        task_exit();
        return;
    }
    uint32_t i = 0;
    do {
        const uint32_t t = task_create(F::prop_default, 0x3C8);
        task_clear_work(t);
        of<uint16_t>(t, 0xCC) = 0x33;
        prop_init(t);
        const uint32_t n = i & 0xFF;
        const float z = mem.f32(c2::k131_z + n * 4) - 14.0f;
        of<uint32_t>(t, 0x54) = 0x149C + n * 2;
        of<uint16_t>(t, 0x70) = of<uint16_t>(obj, 0x70);
        of<uint32_t>(t, 0x20) = 0x44974AEAu;
        of<uint32_t>(t, 0x24) = 0xC26C0B43u;
        of<uint16_t>(t, 0x64) = of<uint16_t>(obj, 0x64);
        of<float>(t, 0x28) = z + ffrom(0x4379BFBEu);
        ++i;
    } while ((i & 0xFF) <= 7);
    task_exit();
}

// 0x39BC0 kind133_prop_grid (kind 133, Chapter 2 event 4): prop015 props of its sub-type in 2 rows (-1, 0) x 3
// columns (-1..1) around its pose, rows 6 and columns 8 apart turned by its y angle; on the floor unless flags bit 12
// (record +0x4C above); flags bit 26 (glow); the group task ends
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind133_prop_grid(uint32_t obj)
{
    for (int32_t i = -1; i <= 0; ++i) {
        for (int32_t j = -1; j <= 1; ++j) {
            const uint32_t t = task_create(F::prop015_fuuu, 0x3C8);
            task_clear_work(t);
            of<uint16_t>(t, 0xCC) = of<uint16_t>(obj, 0xCC);
            prop_init(t);
            obj_copy_pose(t, obj);
            float a = float(i) * 6.0f;
            float b = float(j) * 8.0f;
            of<uint16_t>(t, 0x70) = of<uint16_t>(obj, 0x70);
            const uint32_t ang = of<uint32_t>(obj, 0x30);
            const float c = pc_path() ? pm::trig(ang, 1) : ffrom(cam_copro(0x1C, {ang}).w[0]);   // PC 0x479BCC (pmrest2)
            a = a * c;
            const float s = pc_path() ? pm::trig(ang, 0) : ffrom(cam_copro(0x1B, {ang}).w[0]);   // PC 0x479BAC
            b = b * s;
            of<float>(t, 0x20) = (a + of<float>(obj, 0x20)) - b;
            const uint32_t rec = 0xAC2C0 + uint32_t(of<uint16_t>(obj, 0xCC)) * 0x4C;
            const float base = (of<uint16_t>(obj, 0x58) >> 12) & 1 ? of<float>(obj, 0x24) : mem.f32(G::floor_y);
            of<float>(t, 0x24) = base + mem.f32(rec + 0x4C);
            float a2 = float(i) * 6.0f;
            float b2 = float(j) * 8.0f;
            const float s2 = pc_path() ? pm::trig(ang, 0) : ffrom(cam_copro(0x1B, {ang}).w[0]);
            a2 = a2 * s2;
            const float c2v = pc_path() ? pm::trig(ang, 1) : ffrom(cam_copro(0x1C, {ang}).w[0]);
            b2 = b2 * c2v;
            of<uint32_t>(t, 0x58) |= 0x4000000;
            of<float>(t, 0x28) = (a2 + of<float>(obj, 0x28)) + b2;
        }
    }
    task_exit();
}

// 0x39F60 kind136_prop_group (kind 136, Chapter 2 event 1): one prop023 (the falling leaves) of its sub-type and the
// 32 leaf records at 0x53E4C0 (24 bytes): leaves 0-23 around its position (x +-3, y -3..+5 + 4, z -3..+6), random
// y angle and phase (0x53E7C0); leaves 24-31 on the floor (+0.5) around (160, 10); the group task ends
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind136_prop_group(uint32_t obj)
{
    const uint32_t t = task_create(F::prop023_kokonoha_leaves, 0x3C8);
    of<uint16_t>(t, 0xCC) = of<uint16_t>(obj, 0xCC);
    prop_init(t);
    for (uint32_t i = 0; i <= 31; ++i) {
        const uint32_t r = 0x53E4C0 + i * 24;
        if (i <= 23) {
            int32_t v = int32_t(rand()) % 61 - 30;
            mem.at<float>(r) = pdiv(float(v), 10.0f) + of<float>(obj, 0x20);
            v = int32_t(rand()) % 81 - 30;
            mem.at<float>(r + 4) = (pdiv(float(v), 10.0f) + of<float>(obj, 0x24)) + 4.0f;
            v = int32_t(rand()) % 91 - 30;
            const float z = pdiv(float(v), 10.0f) + of<float>(obj, 0x28);
            mem.set32(r + 0xC, 0);
            mem.at<float>(r + 8) = z;
            const uint32_t a = rand();
            mem.set32(r + 0x14, 0);
            mem.set32(r + 0x10, a & 0xFFFF);
            mem.set32(0x53E7C0 + i * 4, rand() & 0xFFFF);
        } else {
            const int32_t v = int32_t(rand()) % 301 - 150;
            const float x = pdiv(float(v), 10.0f) + 160.0f;
            mem.at<float>(r + 4) = mem.f32(G::floor_y) + 0.5f;
            mem.at<float>(r) = x;
            const int32_t w = int32_t(rand()) % 51 - 25;
            mem.set32(r + 0xC, 0);
            mem.at<float>(r + 8) = pdiv(float(w), 10.0f) + 10.0f;
            const int32_t a = int32_t(rand()) % 0xFFFF;
            mem.set32(r + 0x14, 0);
            mem.set32(r + 0x10, uint32_t(a));
        }
    }
    task_exit();
}

// --- kind 132 (Chapter 2 events 8 / 12): the Ebitan that vomits at the player ---
// 0x543438 the vomit state (0 idle, 1 spit 0xA3, 3 the spit lands, 4 stream 0xA4, 5 streaming), 0x54343B set once a
// hit took part 2 (no more spitting); +0x2E8 the target player, +0x2EC its turn step (+-0x40), +0x2F0 stream frames

// turns toward a random player with two (+-0x40 a frame), else the one playing
template <class P> void Game<P>::kind132_pick_target(uint32_t obj)
{
    if (mem.at<uint16_t>(G::players_in_game_b) == 2) {
        const int32_t r = int32_t(rand()) % 2;
        of<uint8_t>(obj, 0x2E8) = uint8_t(r);
        const int32_t step = (r & 0xFF) == 1 ? 0x40 : -0x40;
        of<int32_t>(obj, 0x2EC) = step;
        of<int32_t>(obj, 0x30) = of<int32_t>(obj, 0x30) + step;
    } else {
        of<uint8_t>(obj, 0x2E8) = mem.at<uint8_t>(0x51F700);
    }
}

// 0x5FDA0 kind132_ebitb_vomit: body (MO 0x9B), type set-up, counted as an enemy, a target, facing the camera, sound
// 0xA01312
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind132_ebitb_vomit(uint32_t obj)
{
    mem.at<int32_t>(G::hit_type) = of<int16_t>(obj, 0xCC);
    body_init(obj, mem.at<int32_t>(G::hit_type), 0x9B);
    of<uint8_t>(obj, 0x3C6) |= 1;
    creature_type_setup(obj);
    of<uint32_t>(obj, 0x50) = 0;
    of<uint32_t>(obj, 0x2EC) = 0;
    of<uint32_t>(obj, 0x2F0) = 0xFFFFFFFFu;
    mem.at<uint8_t>(0x543432) = 0xFF;
    mem.at<uint8_t>(0x543433) = 0xFF;
    mem.at<uint8_t>(0x543437) = 0xFF;
    mem.at<uint8_t>(0x543439) = 0;
    mem.at<uint8_t>(0x54343A) = 0;
    mem.at<uint8_t>(0x54343B) = 0;
    mem.at<uint8_t>(0x543438) = 0;
    mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
    kind132_pick_target(obj);
    prop_face_camera(obj);
    sound_cue(0xA01312);
    of<uint32_t>(obj, 0) = F::kind132_ebitb_vomit_frame;
}

// 0x5FF60 kind132_ebitb_vomit_frame: part hits (hit motion by class, the vomit stops); shot dead: 80 points, MO 0x9C,
// kind132_ebitb_dead, uncounted, its voice. Idle (MO 0x9E) after its delay: a new delay (120 + rand % 60), spit
// (0xA3) or stream (0xA4) at a new target; the spit lands at frame 28, the stream starts at frame 10 (sound 0xA01320)
// and hits after 40 frames (player_hit side 4), the spit (or the rise at MO 0x9B frame 27) hits with side 10 (sound
// 0xA01254); then it turns back. Drawn with its shadow and drips, in the depth list
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind132_ebitb_vomit_frame(uint32_t obj)
{
    mem.at<uint8_t>(G::hit_class) = 0;
    mem.at<uint16_t>(G::hit_damage) = 0;
    mem.at<int32_t>(G::hit_type) = of<int16_t>(obj, 0xCC);
    enemy_part_hits(obj);
    if (mem.at<uint8_t>(G::hit_class) != 0) {
        const uint32_t c = mem.at<uint8_t>(G::hit_class);
        const uint32_t rem = uint32_t(int32_t(c) % 9), quo = uint32_t(int32_t(c) / 9);
        const uint32_t t = mem.u32(G::hit_motions_by_type + 4 * uint32_t(mem.at<int32_t>(G::hit_type)));
        set_motion_blend(obj, mem.u32(t + ((quo & 0xFF) << 2) + ((rem & 0xFF) << 3)), 8, 10);
        mem.at<uint8_t>(0x543438) = 0;
        if ((of<uint8_t>(obj, 0x2E4) >> 2) & 1)
            mem.at<uint8_t>(0x54343B) = 1;
    }
    const uint16_t hp = uint16_t(of<uint16_t>(obj, 0x70) - mem.at<uint16_t>(G::hit_damage));
    of<uint16_t>(obj, 0x70) = hp;
    if (!(int32_t(uint32_t(hp) << 16) > 0) || return_false() != 0) {
        add_score(of<int8_t>(obj, 0x2EB), 0x50);
        of<uint32_t>(obj, 0x58) |= 0x10000000;
        set_motion(obj, 0x9C);
        of<uint32_t>(obj, 0x50) = 0;
        of<uint32_t>(obj, 0) = F::kind132_ebitb_dead;
        if (mem.at<int16_t>(G::enemies_alive) > 1)
            of<uint32_t>(obj, 0x58) |= 0x100000;
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        enemy_voice(obj, 0);
    }
    const int32_t delay = of<int32_t>(obj, 0x50) - 1;
    of<int32_t>(obj, 0x50) = delay;
    // (ours, totd8: The Typing of the Dead 0's attack window, game.h: the spit / stream waits for its grace; null in
    // the arcade. Up to totd7 it came on its own timing and hit before its word's grace ran out)
    if (delay < 0 && of<uint16_t>(obj, 0xD0) == 0x9E && !(gAttackHold && gAttackHold(obj))) {
        of<int32_t>(obj, 0x50) = int32_t(rand()) % 60 + 0x78;
        uint32_t m;
        if (int32_t(rand()) % 2 == 1 && mem.at<uint8_t>(0x54343B) == 0) {
            mem.at<uint8_t>(0x543438) = 1;
            m = 0xA3;
        } else {
            mem.at<uint8_t>(0x543438) = 4;
            m = 0xA4;
        }
        set_motion_blend(obj, m, 8, 8);
        kind132_pick_target(obj);
    }
    if (mem.at<uint8_t>(0x543438) == 4 && of<int16_t>(obj, 0xF4) == 10) {
        mem.at<uint8_t>(0x543438) = 5;
        of<uint32_t>(obj, 0x2F0) = 0;
        sound_cue(0xA01320);
    }
    if (of<int32_t>(obj, 0x2F0) != -1)
        of<int32_t>(obj, 0x2F0) = of<int32_t>(obj, 0x2F0) + 1;
    uint8_t vs = mem.at<uint8_t>(0x543438);
    if (vs == 1 && of<int16_t>(obj, 0xF4) == 28)
        vs = 3;
    mem.at<uint8_t>(0x543438) = vs;
    if ((vs == 5 && of<int32_t>(obj, 0x2F0) == 40) || vs == 3 ||
        (of<uint16_t>(obj, 0xD0) == 0x9B && of<int16_t>(obj, 0xF4) == 27)) {
        if (vs == 5) {
            player_hit(of<int8_t>(obj, 0x2E8), 1, 4);
        } else {
            // (ours, totd8: the rise's spit (MO 0x9B frame 27) inside its grace: no hit, The Typing of the Dead 0 only)
            if (!((of<uint32_t>(obj, 0x58) >> 28) & 1) &&
                !(gAttackHold && vs != 3 && gAttackHold(obj)))
                player_hit(of<int8_t>(obj, 0x2E8), 1, 10);
            sound_cue(0xA01254);
        }
        of<uint8_t>(obj, 0x2E8) = 0xFF;
        of<uint32_t>(obj, 0x2F0) = 0;
        const int32_t step = 0 - of<int32_t>(obj, 0x2EC);
        of<int32_t>(obj, 0x2EC) = step;
        of<int32_t>(obj, 0x30) = of<int32_t>(obj, 0x30) + step;
        mem.at<uint8_t>(0x543438) = 0;
    }
    const int32_t step = of<int32_t>(obj, 0x2EC);
    if (step != 0) {
        const int32_t a = of<int32_t>(obj, 0x30);
        if (!(a > 0xA000) && a > 0x5FFF && a != 0x8000)
            of<int32_t>(obj, 0x30) = a + step;
    }
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
    shadow_setup(mem.f32(G::floor_y));
    object_draw(obj);
    if ((of<uint32_t>(obj, 0x3C4) & 0x21000) == 0x1000)
        set_motion(obj, 0x9E);
    kind132_drips_draw(obj);
    depth_list_add(obj);
}

// 0x60330 kind132_ebitb_dead: drawn every other frame until frame 58 of its motion, then not shootable, uncounted,
// the body freed, the end
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind132_ebitb_dead(uint32_t obj)
{
    mem.at<int32_t>(G::hit_type) = of<int16_t>(obj, 0xCC);
    const int32_t t = of<int32_t>(obj, 0x50) + 1;
    of<int32_t>(obj, 0x50) = t;
    if (t % 2 != 0) {
        shadow_setup(mem.f32(G::floor_y));
        object_draw(obj);
    }
    if (!(of<int16_t>(obj, 0xF4) > 57)) {
        of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
        return;
    }
    of<uint32_t>(obj, 0x58) &= 0xEFFFFFFEu;
    mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
    body_free_parts(obj);
    task_exit();
}

// 0x603E0 kind132_drips_draw: body_drips_draw over its body parts
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind132_drips_draw(uint32_t obj)
{
    const uint32_t body = of<uint32_t>(obj, 0xC8);
    cam_copro(0x05, {});
    for (int32_t i = 0; i < mem.at<int32_t>(body + 0x34); ++i)
        body_drips_draw(mem.u32(mem.u32(body + 0x24) + 4 * uint32_t(i)));
    cam_copro(0x06, {});
}

// --- kind 187 (Chapter 2 event 12, scene object): the fridge door ---

// 0x970B0 kind187_fridge_door: at (355.73, -122.33, 64.877); flag 0x51E9F0 opens it (sound 0xA0135D): slides 0.5 a
// counted frame (the count steps twice a frame) for 40 frames; flag 0x51E9F1 ends it; reizoko_door01a (0x539) drawn
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind187_fridge_door(uint32_t obj)
{
    of<int32_t>(obj, 0x50) += 1;
    const int16_t s = of<int16_t>(obj, 0x60);
    if (s == 1) {
        const int32_t t = of<int32_t>(obj, 0x50);
        const int32_t n = t + 1;
        if (t > 40)
            of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        of<int32_t>(obj, 0x50) = n;
        of<float>(obj, 0x28) = ffrom(0x4281C106u) - float(n) * 0.5f;
    } else if (s == 0) {
        if (mem.at<uint8_t>(0x51E9F0) == 1) {
            sound_cue(0xA0135D);
            of<int32_t>(obj, 0x50) = 0;
            of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        }
        of<uint32_t>(obj, 0x20) = 0x43B1DD70u;
        of<uint32_t>(obj, 0x24) = 0xC2F4A8F6u;
        of<uint32_t>(obj, 0x30) = 0;
        of<uint32_t>(obj, 0x28) = 0x4281C106u;
    } else if (s == 2) {
        if (mem.at<uint8_t>(0x51E9F1) == 1) {
            task_exit();
            return;
        }
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    geo_draw_model(0x539);
    cam_copro(0x06, {});
}

// --- kinds 184 / 185 (Chapter 2 events 6 / 11, scene objects) ---

// 0x3B90 kind184_lift: at (826.82, -55.925, 452.30), sound 0xA0123E, kind184_lift_frame at once
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind184_lift(uint32_t obj)
{
    of<uint32_t>(obj, 0x20) = 0x444EB47Du;
    of<uint32_t>(obj, 0x24) = 0xC25FB368u;
    of<uint32_t>(obj, 0x28) = 0x43E22642u;
    sound_cue(0xA0123E);
    of<uint32_t>(obj, 0) = F::kind184_lift_frame;
    kind184_lift_frame(obj);
}

// 0x3BE0 kind184_lift_frame: flag 0x51E9A2 ends it; rises 0.7 a frame to -20.78 (then sounds 0xA0123F / 0xA0121F,
// flag 0x51E9A8); model 0x1479 drawn at its position
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind184_lift_frame(uint32_t obj)
{
    if (mem.at<uint8_t>(0x51E9A2) == 1) {
        task_exit();
        return;
    }
    if (of<int16_t>(obj, 0x60) == 0) {
        const float y = of<float>(obj, 0x24) + ffrom(0x3F333333u);
        of<float>(obj, 0x24) = y;
        if (!(y < ffrom(0xC1A63EE0u))) {
            sound_cue(0xA0123F);
            sound_cue(0xA0121F);
            mem.at<uint8_t>(0x51E9A8) = 1;
            of<int16_t>(obj, 0x60) = int16_t(of<int16_t>(obj, 0x60) + 1);
        }
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    geo_draw_model(0x1479);
    cam_copro(0x06, {});
}

// 0x3CA0 kind185_elevator: at (187.37, -6.752, 0), sounds 0xA01217 / 0xA01367, kind185_elevator_frame at once
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind185_elevator(uint32_t obj)
{
    of<uint32_t>(obj, 0x20) = 0x433B5FBEu;
    of<uint32_t>(obj, 0x28) = 0;
    of<uint32_t>(obj, 0x24) = 0xC0D81062u;
    sound_cue(0xA01217);
    sound_cue(0xA01367);
    of<uint32_t>(obj, 0) = F::kind185_elevator_frame;
    kind185_elevator_frame(obj);
}

// 0x3CF0 kind185_elevator_frame: at camera frame 495 sound 0xA01217 and the end; PN_ro_01_ele (0x1239) drawn 15 below
// the camera eye
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind185_elevator_frame(uint32_t obj)
{
    if (mem.f32(G::camera_frame) == 495.0f) {
        sound_cue(0xA01217);
        task_exit();
        return;
    }
    const float y = mem.f32(G::camera_records + 4) - 15.0f;
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), fbits(y), of<uint32_t>(obj, 0x28)});
    geo_draw_model(0x1239);
    cam_copro(0x06, {});
}

// 0x3AEF0 kind151_prop_group (kind 151, Chapter 2 event 13): five statue fragments (prop080, models 0x13A3 / 0x13A4)
// scattered up to 5 units from its position, falling (-0.01 - rand / 1000) and spinning; the group task ends
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind151_prop_group(uint32_t obj)
{
    uint32_t i = 0;
    do {
        const uint32_t t = task_create(F::prop080_statue_piece, 0x3C8);
        task_clear_work(t);
        of<uint32_t>(t, 0x58) |= 0x1401;
        of<float>(t, 0x20) = pdiv(float(int32_t(rand()) % 50), 10.0f) + of<float>(obj, 0x20);
        of<uint32_t>(t, 0x24) = of<uint32_t>(obj, 0x24);
        const float z = pdiv(float(int32_t(rand()) % 50), 10.0f) + of<float>(obj, 0x28);
        of<uint32_t>(t, 0x54) = 0x13A3 + (i & 1);
        of<uint32_t>(t, 0x7C) = 0;
        of<uint32_t>(t, 0x80) = 0;
        of<float>(t, 0x28) = z;
        of<float>(t, 0x88) = ffrom(0xBC23D70Au) - pdiv(float(int32_t(rand()) % 20), 1000.0f);
        of<uint32_t>(t, 0x31C) = (rand() & 0x7FF) - 0x400;
        of<uint32_t>(t, 0x320) = (rand() & 0x7FF) - 0x400;
        ++i;
        of<uint32_t>(t, 0x94) = 0x3ECCCCCDu;
        of<uint16_t>(t, 0x6A) = 0;
        of<uint32_t>(t, 0x90) = 0x40200000u;
    } while ((i & 0xFF) <= 4);
    task_exit();
}

// 0x3A500 kind152_prop_group (kind 152, Chapter 2 event 12): twelve prop085 props from table 0xAF630 (28 bytes:
// sub-type, offset, angle offsets) around its pose; the group task ends
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind152_prop_group(uint32_t obj)
{
    if (pc_path()) {   // THOTD.EXE's kind 152 is only `jmp task_exit` (pmrest2)
        task_exit();
        return;
    }
    for (uint32_t i = 0; i <= 11; ++i) {
        const uint32_t t = task_create(F::prop085_fruit_scheduled, 0x3C8);
        task_clear_work(t);
        const uint32_t e = 0xAF630 + i * 28;
        of<uint16_t>(t, 0xCC) = mem.at<uint16_t>(e);
        prop_init(t);
        of<uint16_t>(t, 0x70) = of<uint16_t>(obj, 0x70);
        of<uint16_t>(t, 0x64) = of<uint16_t>(obj, 0x64);
        of<float>(t, 0x20) = of<float>(obj, 0x20) + mem.f32(e + 4);
        of<float>(t, 0x24) = of<float>(obj, 0x24) + mem.f32(e + 8);
        of<float>(t, 0x28) = of<float>(obj, 0x28) + mem.f32(e + 12);
        of<uint32_t>(t, 0x2C) = of<uint32_t>(obj, 0x2C) + mem.u32(e + 16);
        of<uint32_t>(t, 0x30) = of<uint32_t>(obj, 0x30) + mem.u32(e + 20);
        of<uint32_t>(t, 0x34) = of<uint32_t>(obj, 0x34) + mem.u32(e + 24);
    }
    task_exit();
}

// --- kind 150 (Chapter 2 events 10 / 13, Chapter 3 event 3): the Ebitan b sliding at the players ---
// +0x2E8 the target player, +0x2EC the delay (Chapter 2 action 1), +0x2F0 the motion to go back to after a hit;
// action 2 slides off a ledge and drops (not counted). Chapter 2: along x toward 50 (state 4); else (Chapter 3) down
// the slope (state 5)

// the event-3 shadow (Chapter 3's slope): ebitibkage 0x1526 / 0x1527 one unit below, turned (0xE000, 0x4000)
template <class P> void Game<P>::kind150_slope_shadow(uint32_t obj, uint32_t model)
{
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), fbits(of<float>(obj, 0x24) - 1.0f), of<uint32_t>(obj, 0x28)});
    cam_copro(0x15, {0x4000});
    cam_copro(0x14, {0xE000});
    geo_draw_model(model);
    cam_copro(0x06, {});
}

// one enemy fewer (inline in the frame routines): only while alive and some are alive
template <class P> void Game<P>::kind150_uncount(uint32_t obj)
{
    if ((of<uint32_t>(obj, 0x58) >> 28) & 1)
        return;
    const int16_t alive = mem.at<int16_t>(G::enemies_alive);
    if (!(alive > 0))
        return;
    if (alive > 1)
        of<uint32_t>(obj, 0x58) |= 0x100000;
    mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
    mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
}

// 0x61240 kind150_ebitan_slide: body (Chapter 2 action 1: MO 0x8E, else the slide 0xA7), type set-up, state 1,
// x acceleration -0.015, target = the action; not action 2: counted, a wide attack slot, accel -0.022 (Chapter 2
// action 1: state 0 with a 50-frame delay, back motion 0x8E); facing the camera
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind150_ebitan_slide(uint32_t obj)
{
    mem.at<int32_t>(G::hit_type) = of<int16_t>(obj, 0xCC);
    const bool ch2a1 = of<uint16_t>(obj, 0x64) == 1 && mem.at<uint8_t>(G::chapter) == 1;
    body_init(obj, mem.at<int32_t>(G::hit_type), ch2a1 ? 0x8E : 0xA7);
    of<uint8_t>(obj, 0x3C6) |= 1;
    creature_type_setup(obj);
    const uint8_t action = of<uint8_t>(obj, 0x64);
    of<uint32_t>(obj, 0x50) = 0;
    of<uint32_t>(obj, 0x78) = 0;
    of<int16_t>(obj, 0x5C) = 1;
    of<uint32_t>(obj, 0x84) = 0xBC75C28Fu;
    of<uint32_t>(obj, 0x7C) = 0;
    of<uint32_t>(obj, 0x2F0) = 0xA7;
    of<uint8_t>(obj, 0x2E8) = action;
    if (of<uint16_t>(obj, 0x64) != 2) {
        mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) + 1);
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) + 1);
        attack_slot_take_wide(obj);
        of<uint32_t>(obj, 0x2EC) = 0;
        if (of<uint16_t>(obj, 0x64) == 1 && mem.at<uint8_t>(G::chapter) == 1) {
            of<uint32_t>(obj, 0x2EC) = 50;
            of<int16_t>(obj, 0x5C) = 0;
            of<uint32_t>(obj, 0x2F0) = 0x8E;
        } else {
            of<uint32_t>(obj, 0x84) = 0xBCB43958u;
        }
    }
    prop_face_camera(obj);
    of<uint32_t>(obj, 0) = F::kind150_ebitan_slide_frame;
}

// 0x613D0 kind150_ebitan_slide_frame: part hits (hit motion 0xC5560 by class % 9, back to +0x2F0 at its end); shot
// dead (not action 2): 80 points, off its slot, MO 0x93 (Chapter 3 tipped 0xE000), kind150_ebitan_slide_dying,
// uncounted, its voice. Action 2: 1 slides (-0.4) and tips over the ledge at x 58 / 52 (MO 0x97, sound 0xA01223),
// 2 falls to y -150.2, 3 drops away below y -198 (dying). Else: 0 the delay; 1 slides (Chapter 2: x accelerating;
// Chapter 3: down the slope) to x 86 (Chapter 2: MO 0xA8, state 4) or 425 + 5 (1 - action) (MO 0xA8, tipped, state
// 5); 4 at x 50 hits its player (sound 0xA01254), falls away (3); 5 at x 390 hits its player, ends at x 380; 3 falls
// out below y -150 (dying). Drawn with its shadow (event 3: the slope shadow), in the depth list
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind150_ebitan_slide_frame(uint32_t obj)
{
    mem.at<uint8_t>(G::hit_class) = 0;
    mem.at<uint16_t>(G::hit_damage) = 0;
    mem.at<int32_t>(G::hit_type) = of<int16_t>(obj, 0xCC);
    enemy_part_hits(obj);
    if (mem.at<uint8_t>(G::hit_class) != 0) {
        of<int32_t>(obj, 0x2F0) = of<int16_t>(obj, 0xD0);
        const uint32_t c = uint32_t(int32_t(mem.at<int8_t>(G::hit_class)) % 9) & 0xFF;
        set_motion_blend(obj, mem.u32(0xC5560 + c * 4), 8, 8);
    }
    if (hostage_motion_done(obj) && of<int16_t>(obj, 0xD0) != of<int32_t>(obj, 0x2F0))
        set_motion_blend(obj, of<uint32_t>(obj, 0x2F0), 8, 8);
    if (of<uint16_t>(obj, 0x64) != 2 && !((of<uint32_t>(obj, 0x58) >> 28) & 1)) {
        const uint16_t hp = uint16_t(of<uint16_t>(obj, 0x70) - mem.at<uint16_t>(G::hit_damage));
        of<uint16_t>(obj, 0x70) = hp;
        if (!(int32_t(uint32_t(hp) << 16) > 0) || return_false() != 0) {
            add_score(of<int8_t>(obj, 0x2EB), 0x50);
            if (of<uint8_t>(obj, 0x2E7) != 0xFF)
                enemy_attack_slot_free(obj);
            of<uint32_t>(obj, 0x58) |= 0x10000000;
            set_motion_blend(obj, 0x93, 8, 8);
            if (mem.at<uint8_t>(G::chapter) == 2)
                of<uint32_t>(obj, 0x2C) = 0xE000;
            of<uint16_t>(obj, 0x70) = 0;
            of<uint32_t>(obj, 0) = F::kind150_ebitan_slide_dying;
            const int16_t alive = mem.at<int16_t>(G::enemies_alive);
            if (alive > 0) {
                if (alive > 1)
                    of<uint32_t>(obj, 0x58) |= 0x100000;
                mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
                mem.at<uint16_t>(G::enemy_count) = uint16_t(mem.at<uint16_t>(G::enemy_count) - 1);
            }
            enemy_voice(obj, 0);
        }
    }
    const bool ch2 = mem.at<uint8_t>(G::chapter) == 1;
    auto hit_player = [&](uint8_t who) {
        of<uint8_t>(obj, 0x2E8) = mem.at<uint16_t>(G::players_in_game_b) == 1 ? mem.at<uint8_t>(0x51F700) : who;
        player_hit(of<int8_t>(obj, 0x2E8), 1, 0);
        sound_cue(0xA01254);
    };
    if (of<uint16_t>(obj, 0x64) == 2) {
        const int16_t s = of<int16_t>(obj, 0x5C);
        if (s == 1) {
            const float x = of<float>(obj, 0x20) - ffrom(0x3ECCCCCDu);
            of<float>(obj, 0x20) = x;
            if (x < 58.0f) {
                const float vy = of<float>(obj, 0x7C) - ffrom(0x3D23D70Au);
                const float y = of<float>(obj, 0x24) + vy;
                of<uint32_t>(obj, 0x2C) = of<uint32_t>(obj, 0x2C) - 0x100;
                of<float>(obj, 0x7C) = vy;
                of<float>(obj, 0x24) = y;
            }
            if (of<float>(obj, 0x20) < 52.0f) {
                of<uint32_t>(obj, 0x2F0) = 0x97;
                set_motion_blend(obj, 0x97, 0, 8);
                of<uint32_t>(obj, 0x78) = 0;
                of<int16_t>(obj, 0x5C) = 2;
                of<uint32_t>(obj, 0x7C) = 0;
                sound_cue(0xA01223);
            }
        } else if (s == 2) {
            const float vx = of<float>(obj, 0x78) + ffrom(0x3C83126Fu);
            const float x = of<float>(obj, 0x20) + vx;
            const int32_t ax = of<int32_t>(obj, 0x2C);
            of<float>(obj, 0x78) = vx;
            of<float>(obj, 0x20) = x;
            if (!(ax > 0x238D))
                of<int32_t>(obj, 0x2C) = ax + 0x100;
            const float vy = of<float>(obj, 0x7C) - ffrom(0x3CA3D70Au);
            const float y = of<float>(obj, 0x24) + vy;
            of<float>(obj, 0x7C) = vy;
            of<float>(obj, 0x24) = y;
            if (y < ffrom(0xC3163333u)) {
                of<int16_t>(obj, 0x5C) = 3;
                of<float>(obj, 0x78) = of<float>(obj, 0x78) * 0.5f;
            }
        } else if (s == 3) {
            const float vy = float(double(of<float>(obj, 0x7C)) - 0.06);
            const float y = of<float>(obj, 0x24) + vy;
            const float vx = of<float>(obj, 0x78) + ffrom(0x3D8F5C29u);
            const float x = of<float>(obj, 0x20) + vx;
            of<float>(obj, 0x78) = vx;
            of<float>(obj, 0x7C) = vy;
            of<float>(obj, 0x20) = x;
            of<float>(obj, 0x24) = y;
            if (y < -198.0f) {
                set_motion_blend(obj, 0x93, 8, 8);
                of<uint32_t>(obj, 0x50) = 0;
                of<uint32_t>(obj, 0) = F::kind150_ebitan_slide_dying;
            }
        }
    } else {
        switch (of<int16_t>(obj, 0x5C)) {
        case 0: {
            const int32_t d = of<int32_t>(obj, 0x2EC) - 1;
            of<int32_t>(obj, 0x2EC) = d;
            if (!(d > 0)) {
                of<int16_t>(obj, 0x5C) = 1;
                set_motion_blend(obj, 0xA7, 8, 8);
                of<uint32_t>(obj, 0x2F0) = 0xA7;
            }
            break;
        }
        case 1:
            if (ch2) {
                const float vx = of<float>(obj, 0x78) + of<float>(obj, 0x84);
                of<float>(obj, 0x78) = vx;
                of<float>(obj, 0x20) = of<float>(obj, 0x20) + vx;
            } else {
                of<float>(obj, 0x78) = of<float>(obj, 0x78) - ffrom(0x3C23D70Au);
                of<float>(obj, 0x7C) = of<float>(obj, 0x7C) - ffrom(0x3BC49BA6u);
                of<float>(obj, 0x20) = of<float>(obj, 0x20) + of<float>(obj, 0x78);
                of<float>(obj, 0x24) = of<float>(obj, 0x24) + of<float>(obj, 0x7C);
            }
            if (mem.at<uint8_t>(G::chapter) == 1) {
                if (of<float>(obj, 0x20) < 86.0f && !((of<uint32_t>(obj, 0x58) >> 28) & 1)) {
                    set_motion_blend(obj, 0xA8, 8, 8);
                    of<uint32_t>(obj, 0x2F0) = 0xA8;
                    of<int16_t>(obj, 0x5C) = 4;
                }
            } else {
                const float lim = float(1 - int32_t(of<int16_t>(obj, 0x64))) * 5.0f + 425.0f;
                if (of<float>(obj, 0x20) < lim && !((of<uint32_t>(obj, 0x58) >> 28) & 1)) {
                    set_motion_blend(obj, 0xA8, 8, 4);
                    of<uint32_t>(obj, 0x2C) = 0xE000;
                    of<uint32_t>(obj, 0x2F0) = 0xA8;
                    of<int16_t>(obj, 0x5C) = 5;
                }
            }
            break;
        case 3: {
            const float vy = of<float>(obj, 0x7C) - ffrom(0x3D75C28Fu);
            const float y = of<float>(obj, 0x24) + vy;
            of<uint32_t>(obj, 0x2C) = of<uint32_t>(obj, 0x2C) - 0x300;
            of<float>(obj, 0x7C) = vy;
            of<float>(obj, 0x24) = y;
            if (y < -150.0f) {
                kind150_uncount(obj);
                of<uint32_t>(obj, 0) = F::kind150_ebitan_slide_dying;
            }
            break;
        }
        case 4: {
            const float vx = of<float>(obj, 0x78) + of<float>(obj, 0x84);
            const float x = of<float>(obj, 0x20) + vx;
            of<float>(obj, 0x78) = vx;
            of<float>(obj, 0x20) = x;
            if (x < 50.0f) {
                hit_player(uint8_t(1 - of<uint8_t>(obj, 0x64)));
                of<int16_t>(obj, 0x5C) = 3;
                of<uint8_t>(obj, 0x2E8) = 0xFF;
                if (of<uint8_t>(obj, 0x2E7) != 0xFF)
                    enemy_attack_slot_free(obj);
                of<uint16_t>(obj, 0x6C) = uint16_t(of<uint16_t>(obj, 0x6C) & ~2u);
            }
            break;
        }
        case 5: {
            of<float>(obj, 0x78) = of<float>(obj, 0x78) - ffrom(0x3C23D70Au);
            const float vy = of<float>(obj, 0x7C) - ffrom(0x3BC49BA6u);
            const float x = of<float>(obj, 0x20) + of<float>(obj, 0x78);
            of<float>(obj, 0x20) = x;
            of<float>(obj, 0x7C) = vy;
            of<uint32_t>(obj, 0x2C) = of<uint32_t>(obj, 0x2C) + 0x150;
            of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
            if (x < 390.0f && of<uint8_t>(obj, 0x2E8) != 0xFF) {
                hit_player(of<uint8_t>(obj, 0x64));
                of<uint8_t>(obj, 0x2E8) = 0xFF;
                if (of<uint8_t>(obj, 0x2E7) != 0xFF)
                    enemy_attack_slot_free(obj);
            }
            if (of<float>(obj, 0x20) < 380.0f) {
                kind150_uncount(obj);
                body_free_parts(obj);
                of<uint32_t>(obj, 0x58) &= ~1u;
                task_exit();
                return;
            }
            break;
        }
        default: break;
        }
    }
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
    shadow_setup(mem.f32(G::floor_y));
    object_draw(obj);
    if (mem.at<uint8_t>(G::event_index) == 3)
        kind150_slope_shadow(obj, of<int16_t>(obj, 0x5C) == 5 ? 0x1526 : 0x1527);
    depth_list_add(obj);
}

// 0x61C10 kind150_ebitan_slide_dying: part hits; Chapter 2 (not action 2) slides on slowing (x 0.95) and falls past x
// 55; the death motion to frame 77, then drawn every other frame; the slope shadow in event 3; ends after 120 frames
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::kind150_ebitan_slide_dying(uint32_t obj)
{
    mem.at<uint8_t>(G::hit_class) = 0;
    mem.at<uint16_t>(G::hit_damage) = 0;
    mem.at<int32_t>(G::hit_type) = of<int16_t>(obj, 0xCC);
    enemy_part_hits(obj);
    if (of<uint16_t>(obj, 0x64) != 2 && mem.at<uint8_t>(G::chapter) == 1) {
        const float vx = of<float>(obj, 0x78) * ffrom(0x3F733333u);
        const float x = of<float>(obj, 0x20) + vx;
        of<float>(obj, 0x78) = vx;
        of<float>(obj, 0x20) = x;
        if (x < 55.0f) {
            const float vy = of<float>(obj, 0x7C) - ffrom(0x3D75C28Fu);
            const float y = of<float>(obj, 0x24) + vy;
            of<uint32_t>(obj, 0x2C) = of<uint32_t>(obj, 0x2C) - 0x300;
            of<float>(obj, 0x7C) = vy;
            of<float>(obj, 0x24) = y;
        }
    }
    if (!(of<int16_t>(obj, 0xF4) > 0x4D) || of<int32_t>(obj, 0x50) % 2 != 0) {
        shadow_setup(mem.f32(G::floor_y));
        object_draw(obj);
    }
    if (mem.at<uint8_t>(G::event_index) == 3)
        kind150_slope_shadow(obj, 0x1526);
    if (!(of<int16_t>(obj, 0xF4) > 0x4D))
        of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
    const int32_t t = of<int32_t>(obj, 0x50) + 1;
    of<int32_t>(obj, 0x50) = t;
    if (t > 0x77) {
        body_free_parts(obj);
        of<uint32_t>(obj, 0x58) &= ~1u;
        task_exit();
        return;
    }
    depth_list_add(obj);
}

// --- kind 3 (and the other zombies of the 0x93xxx family): the death that falls to the floor ---

// 0x93340 enemy_fall_death_check: the damage against the life (or a one-hit kill: flags bit 8 with +0x308); dead: 80
// points to the killer, not shootable, the target freed; the second motion stopped; sub-state < 2: falls (MO 0x274,
// enemy_fall_death_land), else dies (MO 0x275, enemy_dying, uncounted, off its attack slot); its voice by the hit
// class (% 9 == 1: 2, else 1)
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::enemy_fall_death_check(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
        return;
    }
    const uint16_t hp = uint16_t(of<uint16_t>(obj, 0x70) - mem.at<uint16_t>(G::hit_damage));
    of<uint16_t>(obj, 0x70) = hp;
    bool dead = !(int32_t(uint32_t(hp) << 16) > 0);
    if (!dead && ((of<uint16_t>(obj, 0x58) >> 8) & 1) && of<uint32_t>(obj, 0x308) != 0)
        dead = true;
    if (!dead && return_false() == 0)
        return;
    if (of<uint8_t>(obj, 0x2EB) != 0xFF)
        add_score(int(of<uint8_t>(obj, 0x2EB)), 0x50);
    of<uint32_t>(obj, 0x58) &= ~1u;
    const uint8_t tgt = of<uint8_t>(obj, 0x2E8);
    if (tgt != 0xFF) {
        if (mem.at<uint8_t>(G::player_targeted + tgt) != 0)
            mem.at<uint8_t>(G::player_targeted + tgt) = 0;
        of<uint8_t>(obj, 0x2E8) = 0xFF;
    }
    if ((of<uint8_t>(obj, 0x3C6) >> 5) & 1)
        body_second_motion(obj, 0);
    if (of<int16_t>(obj, 0x60) < 2) {
        set_motion(obj, 0x274);
        of<uint32_t>(obj, 0) = F::enemy_fall_death_land;
    } else {
        set_motion(obj, 0x275);
        of<uint32_t>(obj, 0x50) = 0;
        of<uint32_t>(obj, 0) = F::enemy_dying;
        if (mem.at<int16_t>(G::enemies_alive) > 1)
            of<uint32_t>(obj, 0x58) |= 0x100000;
        if ((of<uint8_t>(obj, 0x3C4) >> 3) & 1)
            mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
        if (of<uint8_t>(obj, 0x2E7) != 0xFF)
            enemy_attack_slot_free(obj);
    }
    if (((mem.at<int8_t>(G::hit_class) % 9) & 0xFF) == 1)
        enemy_voice(obj, 2);
    else
        enemy_voice(obj, 1);
}

// 0x934B0 enemy_fall_death_land: falls (gravity +0x88) with its shadow on the floor (flags bit 23: its own y) until
// it reaches it: the splat, sound 0xA0125B, dies (MO 0x275, enemy_dying, uncounted, off its attack slot)
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::enemy_fall_death_land(uint32_t obj)
{
    const float floor = (of<uint32_t>(obj, 0x58) >> 23) & 1 ? of<float>(obj, 0x24) : mem.f32(G::floor_y);
    shadow_setup(floor);
    object_draw(obj);
    const float vy = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
    const float y = of<float>(obj, 0x24) + vy;
    of<int32_t>(obj, 0xF8) = of<int32_t>(obj, 0xF8) + 1;
    of<float>(obj, 0x7C) = vy;
    of<float>(obj, 0x24) = y;
    if (y > floor)
        return;
    floor_splat_spawn_b(obj);
    sound_cue(0xA0125B);
    set_motion(obj, 0x275);
    of<uint32_t>(obj, 0x50) = 0;
    of<uint32_t>(obj, 0) = F::enemy_dying;
    if (mem.at<int16_t>(G::enemies_alive) > 1)
        of<uint32_t>(obj, 0x58) |= 0x100000;
    if ((of<uint8_t>(obj, 0x3C4) >> 3) & 1)
        mem.at<uint16_t>(G::enemies_alive) = uint16_t(mem.at<uint16_t>(G::enemies_alive) - 1);
    if (of<uint8_t>(obj, 0x2E7) != 0xFF)
        enemy_attack_slot_free(obj);
}

// --- small core routines that were boundaries (worker "ch2play"; reached in Chapter 2 by the bats' leader molb, the
// frogs' splash, the kit zombie's held reach); PC forms not ported (vtable only) ---

// 0x2E870 obj_velocity_clear: speed (+0x74), velocity and acceleration 0
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::obj_velocity_clear(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;
        return;
    }
    of<uint32_t>(obj, 0x74) = 0;
    of<uint32_t>(obj, 0x80) = 0;
    of<uint32_t>(obj, 0x7C) = 0;
    of<uint32_t>(obj, 0x78) = 0;
    of<uint32_t>(obj, 0x8C) = 0;
    of<uint32_t>(obj, 0x88) = 0;
    of<uint32_t>(obj, 0x84) = 0;
}

// 0x2E550 area_heading_pick (rect {x max, x min, z max, z min}, pos, step, out angle): outside the rect 0; else the
// headings whose step stays inside (bit 0 +z: 0, bit 1 +x: 0xC000, bit 2 -z: 0x8000, bit 3 -x: 0x4000), one of several
// by rand (the original returns 1 / 0; its callers do not read it)
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::area_heading_pick(uint32_t rect, uint32_t pos, uint32_t step, uint32_t outAngle)
{
    if constexpr (P::pc) {
        (void)rect; (void)pos; (void)step; (void)outAngle;
        return;
    }
    const float xmax = mem.f32(rect), xmin = mem.f32(rect + 4), zmax = mem.f32(rect + 8), zmin = mem.f32(rect + 12);
    const float x = mem.f32(pos), z = mem.f32(pos + 8), s = ffrom(step);
    const auto inside = [&](float px, float pz) { return px <= xmax && xmin <= px && pz <= zmax && zmin <= pz; };
    if (!inside(x, z)) {
        mem.set32(outAngle, 0);
        return;
    }
    uint32_t bits = 0;
    if (inside(x, z + s)) bits |= 1;
    if (inside(x, z - s)) bits |= 4;
    if (inside(x + s, z)) bits |= 2;
    if (inside(x - s, z)) bits |= 8;
    uint32_t a;
    switch (bits) {
    case 0: mem.set32(outAngle, 0); return;
    case 1: a = 0; break;
    case 2: a = 0xC000; break;
    case 3: a = (rand() & 0x3FFF) + 0xC000; break;
    case 4: a = 0x8000; break;
    case 5: a = (rand() & 1) << 15; break;
    case 6: a = (rand() & 0x3FFF) + 0x8000; break;
    case 7: a = (rand() & 0x7FFF) + 0x8000; break;
    case 8: a = 0x4000; break;
    case 9: a = rand() & 0x3FFF; break;
    case 10: a = (rand() & 1) * 0x8000 + 0x4000; break;
    case 11: a = (rand() & 0x7FFF) + 0xC000; break;
    case 12: a = (rand() & 0x3FFF) + 0x4000; break;
    case 13: a = rand() & 0x7FFF; break;
    case 14: a = (rand() & 0x7FFF) + 0x4000; break;
    default: a = rand(); break;
    }
    mem.set32(outAngle, a);
}

// 0x308B0 frog_splash_spawn: a water splash (PN_mizuzuzu00a 0x415, the frog_splash task) at its x / z on the floor,
// sound 0xA0110C
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::frog_splash_spawn(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;
        return;
    }
    const uint32_t t = task_create(F::frog_splash, 0x3C8);
    of<uint32_t>(t, 0x20) = of<uint32_t>(obj, 0x20);
    of<uint32_t>(t, 0x24) = mem.u32(G::floor_y);
    of<uint32_t>(t, 0x54) = 0x415;
    of<uint32_t>(t, 0x28) = of<uint32_t>(obj, 0x28);
    sound_cue(0xA0110C);
}

// 0x30820 frog_splash: the splash's model frames up to 0x433 at its position (scale 1), then it ends
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::frog_splash(uint32_t obj)
{
    const uint32_t m = of<uint32_t>(obj, 0x54) + 1;
    of<uint32_t>(obj, 0x54) = m;
    if (m == 0x433) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x13, {0x3F800000u, 0x3F800000u, 0x3F800000u});
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

// 0x34920 point_clip_to_depth (point, origin, z): the point moved along its line from the origin to depth z
// Proven on A (ch2proof, docs/teaser_status/ch2proof.md)
template <class P> void Game<P>::point_clip_to_depth(uint32_t point, uint32_t from, float depth)
{
    if constexpr (P::pc) {
        (void)point; (void)from; (void)depth;
        return;
    }
    const float px = mem.f32(point), py = mem.f32(point + 4), pz = mem.f32(point + 8);
    const float ox = mem.f32(from), oy = mem.f32(from + 4), oz = mem.f32(from + 8);
    const float k = (depth - oz) / (pz - oz);
    mem.at<float>(point) = ox + k * (px - ox);
    mem.at<float>(point + 8) = depth;
    mem.at<float>(point + 4) = oy + k * (py - oy);
}

} // namespace hotd::port
