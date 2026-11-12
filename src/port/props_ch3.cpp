// Chapter 3's kind 128 prop sub-types (worker "ch3own", docs/chapter2_3_plan.md), reached through the sub-type table
// 0xAF960 (prop_object). Read from the arcade decompilation / disassembly; arcade forms only. Proven on A (ch3own_full, vf_c3a: decl/props_ch3.h).
#include "port/game.h"

namespace hotd::port {

// 0x3F0D0 prop_ch3_machine: sub-types 87 / 88 / 93 / 95 (Chapter 3 machines). 93: animates models 0xD58-0xD79 until
// camera frame 430 (then 0x51E9B2 = 1, ends); 95: ends at camera frame 601 (event 0) / 338 (event 3), model 0x16CF in
// event 3; the others: model 0x1120 -> 0x1121 from frame 311; shot: the sub-type's sound (0xAC324), the impact at
// its height (0xAC310 by kit), then shakes for 20 frames around the place it had (+0x33C / +0x340); shootable
template <class P> void Game<P>::prop_ch3_machine(uint32_t obj)
{
    prop_schedule_update(obj);
    const int16_t type = of<int16_t>(obj, 0xCC);
    const float frame = mem.f32(G::camera_frame);
    if (type == 0x5D) {
        if (!(frame < 430.0f)) {
            mem.at<uint8_t>(0x51E9B2) = 1;
            task_exit();
            return;
        }
        const int32_t m = of<int32_t>(obj, 0x54);
        of<int32_t>(obj, 0x54) = m > 0xD79 ? 0xD58 : m + 1;
        prop_draw(obj);
        return;
    }
    if (type == 0x5F) {
        const int8_t ev = mem.at<int8_t>(G::event_index);
        if ((ev == 0 && frame >= 601.0f) || (ev == 3 && !(frame < 338.0f))) {
            task_exit();
            return;
        }
        if (mem.at<int8_t>(G::event_index) == 3)
            of<int32_t>(obj, 0x54) = 0x16CF;
        prop_draw(obj);
        return;
    }
    if (of<int32_t>(obj, 0x54) == 0x1120 && !(frame < 311.0f))
        of<int32_t>(obj, 0x54) = 0x1121;
    if (of<int32_t>(obj, 0x50) == 0 && (of<uint8_t>(obj, 0x58) & 1) && (of<uint8_t>(obj, 0x58) & 6)) {
        sound_cue(mem.u32(0xAC324 + uint32_t(int32_t(type)) * 0x4C));
        const uint32_t h = 0xAC310 + uint32_t(int32_t(of<int16_t>(obj, 0xCC))) * 0x4C + of<uint32_t>(obj, 0x32C) * 4;
        const Tgp::Out v = cam_copro(0x1A, {of<uint32_t>(obj, 0x20), fbits(of<float>(obj, 0x24) + mem.f32(h)), of<uint32_t>(obj, 0x28)});
        of<uint32_t>(obj, 0x38) = v.w[0];
        of<uint32_t>(obj, 0x3C) = v.w[1];
        of<uint32_t>(obj, 0x40) = v.w[2];
        prop_bullet_impact(obj);
        of<int32_t>(obj, 0x50) = 1;
        of<uint32_t>(obj, 0x33C) = of<uint32_t>(obj, 0x20);
        of<uint32_t>(obj, 0x340) = of<uint32_t>(obj, 0x28);
    }
    if (of<int32_t>(obj, 0x50) != 0) {
        float r = float(int32_t(rand()) % 21);
        of<float>(obj, 0x20) = float((double(r) - 10.0) / 80.0 + double(of<float>(obj, 0x33C)));
        r = float(int32_t(rand()) % 21);
        const double z = (double(r) - 10.0) / 80.0 + double(of<float>(obj, 0x340));
        const int32_t t = of<int32_t>(obj, 0x50) + 1;
        of<int32_t>(obj, 0x50) = t;
        of<float>(obj, 0x28) = float(z);
        if (t > 20) {
            of<int32_t>(obj, 0x50) = 0;
            of<uint32_t>(obj, 0x20) = of<uint32_t>(obj, 0x33C);
            of<uint32_t>(obj, 0x28) = of<uint32_t>(obj, 0x340);
        }
    }
    prop_draw(obj);
    prop_shootable(obj);
}

// 0x3F3A0 prop094_r3_window: sub-type 94 (r3_01a_mad11a): breaks (sound 0xA01101, prop_hit_drop, model 0x1670) once
// the iron ball has been pulled out (0x51E980)
template <class P> void Game<P>::prop094_r3_window(uint32_t obj)
{
    prop_schedule_update(obj);
    if (mem.at<uint8_t>(0x51E980) != 0 && of<int32_t>(obj, 0x54) == 0x166F) {
        sound_cue(0xA01101);
        prop_hit_drop(obj);
        of<int32_t>(obj, 0x54) = 0x1670;
    }
    prop_draw(obj);
}

// 0x3F3F0 prop096_koba: sub-type 96 (koba01a): turns (+0x34 by +0x31C, 0x800); from camera frame 872, past x 500,
// slows down by 4 a frame
template <class P> void Game<P>::prop096_koba(uint32_t obj)
{
    prop_schedule_update(obj);
    int32_t s = of<int32_t>(obj, 0x31C);
    if (mem.f32(G::camera_frame) >= 872.0f) {
        if (of<float>(obj, 0x20) > 500.0f && s > 0)
            s -= 4;
    } else {
        s = 0x800;
    }
    of<int32_t>(obj, 0x31C) = s;
    of<int32_t>(obj, 0x34) = of<int32_t>(obj, 0x34) + s;
    prop_draw(obj);
}

// 0x3FA80 prop121_kikai_item: sub-type 121 (kikai_b, a machine holding an item): at set-up (+0x64 0) a life item
// (flags bit 5) by 0x51E995 (1: drops it and ends, 2: gone, ends; 0: stays), else gone when 0x51E996 is set; staying:
// prop121_kikai_item_run from the next frame
// Proven on A (vf_c3a: 2 captures, 0 differ)
template <class P> void Game<P>::prop121_kikai_item(uint32_t obj)
{
    if (of<int16_t>(obj, 0x64) == 0) {
        if ((of<uint8_t>(obj, 0x58) >> 5) & 1) {
            const int8_t f = mem.at<int8_t>(0x51E995);
            if (f == 1) {
                prop_drop_life_item(obj);
                task_exit();
                return;
            }
            if (f == 2) {
                task_exit();
                return;
            }
            if (f != 0)
                return;
        } else if (mem.at<uint8_t>(0x51E996) != 0) {
            task_exit();
            return;
        }
    }
    of<uint32_t>(obj, 0) = F::prop121_kikai_item_run;
}

// 0x3FAE0 prop121_kikai_item_run: shot: the sub-type's sound (0xAC324), the impact at its height (0xAC310 by kit),
// breaks (prop_break_pieces), drops the life item (0x51E995 = 1) or sets 0x51E996, ends; else drawn, shootable at
// that height
template <class P> void Game<P>::prop121_kikai_item_run(uint32_t obj)
{
    prop_schedule_update(obj);
    if (of<uint8_t>(obj, 0x58) & 6) {
        sound_cue(mem.u32(0xAC324 + uint32_t(int32_t(of<int16_t>(obj, 0xCC))) * 0x4C));
        const uint32_t h = 0xAC310 + uint32_t(int32_t(of<int16_t>(obj, 0xCC))) * 0x4C + of<uint32_t>(obj, 0x32C) * 4;
        const Tgp::Out v = cam_copro(0x1A, {of<uint32_t>(obj, 0x20), fbits(of<float>(obj, 0x24) + mem.f32(h)), of<uint32_t>(obj, 0x28)});
        of<uint32_t>(obj, 0x38) = v.w[0];
        of<uint32_t>(obj, 0x3C) = v.w[1];
        of<uint32_t>(obj, 0x40) = v.w[2];
        prop_bullet_impact(obj);
        prop_break_pieces(obj);
        prop_broken(obj);   // (ours: Horde Mode's drops; nothing in a normal game)
        if ((of<uint8_t>(obj, 0x58) >> 5) & 1) {
            prop_drop_life_item(obj);
            mem.at<uint8_t>(0x51E995) = 1;
        } else {
            mem.at<uint8_t>(0x51E996) = 1;
        }
        task_exit();
        return;
    }
    prop_draw(obj);
    of<uint32_t>(obj, 0x58) = of<uint32_t>(obj, 0x58) & 0xFFFFFFF9u;
    const uint32_t h = 0xAC310 + uint32_t(int32_t(of<int16_t>(obj, 0xCC))) * 0x4C + of<uint32_t>(obj, 0x32C) * 4;
    const Tgp::Out v = cam_copro(0x1A, {of<uint32_t>(obj, 0x20), fbits(of<float>(obj, 0x24) + mem.f32(h)), of<uint32_t>(obj, 0x28)});
    of<uint32_t>(obj, 0x38) = v.w[0];
    of<uint32_t>(obj, 0x3C) = v.w[1];
    of<uint32_t>(obj, 0x40) = v.w[2];
    depth_list_add(obj);
}

} // namespace hotd::port
