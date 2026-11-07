// Teaser L6: kind 128 prop_object (the shootable scenery props), its sub-type routines used in Chapter 1, the shared
// prop helpers (impacts, break / collapse animations, flying pieces, the item drops: life item, gold coin, gold frog)
// and the prop group kinds 134, 137, 139, 147 (cluster 0x39930..0x43000). Read from the arcade disassembly; arcade
// forms only. Sub-type record 0xAC2C0 + 0x4C * type (names.tsv, "Props"): +0x20.. model per damage step, +0x24..
// (+0x04 + 4 * step) hit-mark height, +0x34.. hit radius per step, +0x48 scale, +0x4C height above the floor, +0x64
// sound when shot.
#include "port/game.h"

namespace hotd::port {

namespace pr {
// arcade addresses not in names.tsv yet (A only; names.tsv is shared, added after the teaser)
constexpr uint32_t types = 0xAC2C0;            // sub-type records, 0x4C bytes
constexpr uint32_t routines = 0xAF960;         // per sub-type: its frame routine (prop_object)
constexpr uint32_t schedules = 0xAE840;        // visibility schedules, 8 shorts each (-1 ends)
constexpr uint32_t lottery_count = 0x53E3B0;   // byte: props of the lottery group destroyed so far
constexpr uint32_t lottery_pick = 0x53E3B1;    // byte: the one that drops its item
constexpr uint32_t sparkle_colour = 0x5206AC;  // int16: colour_blend32's result for the item glow
constexpr uint32_t billboard = 0xAA690;        // 9 words: the facing matrix for coins and impact marks (coprocessor 0x87)
constexpr uint32_t player_shot = 0x51F2B4;     // player 0's shot position (int16 x, y); 0xCC per player
constexpr uint32_t light0 = 0x520A40;          // light record 0 (3 words to the geometrizer)
constexpr uint32_t eye = 0x51EAB0;             // camera record 0's eye (x, y, z)
constexpr uint32_t camera_frame = 0x51EB9C;    // camera record 0's frame (float)
inline uint32_t rec(int32_t type) { return types + uint32_t(type) * 0x4C; }
} // namespace pr

// ---------------------------------------------------------------------------------------------------------------
// helpers (coprocessor sequences inline in the original)

// light record 0 to the geometrizer (0xA0A to 0x8000A0, then its three words)
template <class P> void Game<P>::prop_light0()
{
    hw_write32(0x8000A0, 0xA0A);
    hw_write32(0x804000, mem.u32(pr::light0));
    hw_write32(0x804000, mem.u32(pr::light0 + 4));
    hw_write32(0x804000, mem.u32(pr::light0 + 8));
}

// translate to the object's position, rotate x / y / z by +0x2C / +0x30 / +0x34 (coprocessor 0x14 / 0x15 / 0x16)
template <class P> void Game<P>::prop_place_xyz(uint32_t obj)
{
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x14, {of<uint32_t>(obj, 0x2C)});
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    cam_copro(0x16, {of<uint32_t>(obj, 0x34)});
}

template <class P> void Game<P>::prop_scale(uint32_t s)
{
    cam_copro(0x13, {s, s, s});
}

// the additive glow pass: stored-matrix op 0x77 (3), fade 0, the model again, the geometrizer words back
template <class P> void Game<P>::prop_glow(uint32_t model)
{
    cam_copro(0x77, {3});
    draw_fade_begin(0);
    geo_draw_model(model);
    geo_send_words(0, 31, 0x7A0);
}

// the camera-space point of (x, y, z) (coprocessor 0x1A) into the object's view_pos (+0x38..)
template <class P> void Game<P>::prop_view_pos(uint32_t obj, uint32_t x, uint32_t y, uint32_t z)
{
    const Tgp::Out o = cam_copro(0x1A, {x, y, z});
    of<uint32_t>(obj, 0x38) = o.w[0];
    of<uint32_t>(obj, 0x3C) = o.w[1];
    of<uint32_t>(obj, 0x40) = o.w[2];
}

// the shot-mark height of the prop's damage step: record +0x50 + 4 * step
template <class P> float Game<P>::prop_mark_height(uint32_t obj)
{
    return mem.f32(pr::rec(of<int16_t>(obj, 0xCC)) + 0x50 + 4 * of<uint32_t>(obj, 0x32C));
}

// the facing angle toward the camera: coprocessor 0x0F of eye - position; out[0] -> +0x30, then +0x8000 when bits
// 14 / 15 of out[1] differ
template <class P> uint32_t Game<P>::prop_face_camera(uint32_t obj)
{
    const float dx = mem.f32(pr::eye) - of<float>(obj, 0x20);
    const float dy = mem.f32(pr::eye + 4) - of<float>(obj, 0x24);
    const float dz = mem.f32(pr::eye + 8) - of<float>(obj, 0x28);
    const Tgp::Out o = cam_copro(0x0F, {fbits(dx), fbits(dy), fbits(dz)});
    of<uint32_t>(obj, 0x30) = o.w[0];
    const uint32_t v = o.w[1] & 0xFFFF;
    if (((v ^ (v << 1)) >> 15) & 1) {
        of<uint32_t>(obj, 0x30) += 0x8000;
        return 1;
    }
    return 0;
}

// the shooter of a prop: both players this frame -> rand() % 2, else player 1 (0) when flags bit 1, else player 2
template <class P> int Game<P>::prop_shooter(uint32_t obj)
{
    const uint32_t f = of<uint32_t>(obj, 0x58);
    if ((f & 6) == 6)
        return int(int32_t(rand()) % 2);
    return (f >> 1) & 1 ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------------------------
// kind 128

// 0x39930 prop_object (kind 128): prop_init; on the floor (record +0x4C above floor_y) unless flags bit 12; per
// sub-type: 69 the timer limit by event (+0x30C), 27-29 the blood colour's model, 107 the lottery (count 0, the pick
// rand % +0x70), 83 +0x2F8 / +0x338, 108 a random model of 49; the bookshelf topple in Chapter 2 events 13 / 14,
// else the sub-type's routine (0xAF960)
template <class P> void Game<P>::prop_object(uint32_t obj)
{
    if (pc_path()) {
        // THOTD.EXE's prop_object 0x47B2C4 ends at once for sub-types 5, 10-14, 41, 59, 64, 70, 71, 82, 83 and 91:
        // the PC has none of those props (pmrest2: Chapter 2 event 12's extra props in the PC capture)
        const uint16_t st = of<uint16_t>(obj, 0xCC);
        if (st == 5 || (st >= 10 && st <= 14) || st == 41 || st == 59 || st == 64 || st == 70 || st == 71 || st == 82 ||
            st == 83 || st == 91) {
            task_exit();
            return;
        }
    }
    prop_init(obj);
    const int32_t type = of<int16_t>(obj, 0xCC);
    if (!((of<uint16_t>(obj, 0x58) >> 12) & 1))
        of<float>(obj, 0x24) = mem.f32(G::floor_y) + mem.f32(pr::rec(type) + 0x4C);
    if (type == 0x45) {
        const int8_t e = mem.at<int8_t>(G::event_index);
        if (e == 9)
            of<uint32_t>(obj, 0x30C) = 0x43860000;
        else if (e < 9) {
            if (e == 3)
                of<uint32_t>(obj, 0x30C) = 0x42C60000;
        } else if (e <= 12 && e >= 11)
            of<uint32_t>(obj, 0x30C) = 0x44830000;
    } else if (type < 0x45) {
        if (type <= 29 && type >= 27 && mem.at<uint8_t>(G::blood_color) != 0)
            of<int32_t>(obj, 0x54) += 1;
    } else if (type == 0x6B) {
        mem.at<uint8_t>(pr::lottery_count) = 0;
        const int32_t r = int32_t(rand());
        mem.at<uint8_t>(pr::lottery_pick) = uint8_t(r % int32_t(of<int16_t>(obj, 0x70)));
    } else if (type < 0x6B) {
        if (type == 0x53) {
            of<uint32_t>(obj, 0x2F8) = 1;
            of<uint32_t>(obj, 0x338) = 5;
        }
    } else if (type == 0x6C) {
        const int32_t r = int32_t(rand());
        of<int32_t>(obj, 0x54) += r % 49;
    }
    if (mem.at<uint8_t>(G::chapter) == 1) {
        const uint8_t e = mem.at<uint8_t>(G::event_index);
        if ((e == 13 || e == 14) && of<uint16_t>(obj, 0xCC) == 8) {
            of<uint32_t>(obj, 0) = F::prop008_bookshelf_topple;
            return;
        }
    }
    of<uint32_t>(obj, 0) = mem.u32(pr::routines + 4 * uint32_t(int32_t(of<int16_t>(obj, 0xCC))));
}

// 0x42740 prop_init: flags |= 0x401; model, hit radius and scale of damage step 0; the work area cleared; visible;
// +0x2EC the scene number
template <class P> void Game<P>::prop_init(uint32_t obj)
{
    const uint32_t r = pr::rec(of<int16_t>(obj, 0xCC));
    of<uint32_t>(obj, 0x58) |= 0x401;
    of<uint32_t>(obj, 0x54) = mem.u32(r + 0x20);
    of<uint32_t>(obj, 0x90) = mem.u32(r + 0x34);
    of<uint8_t>(obj, 0x2E6) = 0;
    of<uint32_t>(obj, 0x50) = 0;
    of<uint32_t>(obj, 0x94) = mem.u32(r + 0x48);
    const int32_t scene = mem.at<int8_t>(G::scene_number);
    for (uint32_t o : {0x2F4u, 0x2F8u, 0x308u, 0x30Cu, 0x310u, 0x314u, 0x318u, 0x31Cu, 0x320u, 0x324u, 0x328u, 0x32Cu,
                       0x330u, 0x334u, 0x338u})
        of<uint32_t>(obj, o) = 0;
    of<uint16_t>(obj, 0x6A) = 0;
    of<uint32_t>(obj, 0x2F0) = 1;
    of<int32_t>(obj, 0x2EC) = scene;
}

// 0x427E0 prop_shootable: the hit bits cleared; its camera-space point at the step's mark height; the depth list
template <class P> void Game<P>::prop_shootable(uint32_t obj)
{
    of<uint32_t>(obj, 0x58) &= 0xFFFFFFF9u;
    const float y = of<float>(obj, 0x24) + prop_mark_height(obj);
    prop_view_pos(obj, of<uint32_t>(obj, 0x20), fbits(y), of<uint32_t>(obj, 0x28));
    depth_list_add(obj);
}

// 0x42300 prop_draw: light 0, the model placed (T, Rx, Ry, Rz, S); with flags bit 26 the glow pass
template <class P> void Game<P>::prop_draw(uint32_t obj)
{
    prop_light0();
    cam_copro(0x05, {});
    prop_place_xyz(obj);
    prop_scale(of<uint32_t>(obj, 0x94));
    geo_draw_model(of<uint32_t>(obj, 0x54));
    if ((of<uint32_t>(obj, 0x58) >> 26) & 1)
        prop_glow(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

// 0x3FCB0 prop_schedule_update: after +0x70 scene changes the prop ends; the visibility schedule +0x64 (table 0xAE840)
// toggles +0x2F0 at each listed camera frame, ends at -1 while hidden; schedule 25 is ignored in event 7
template <class P> void Game<P>::prop_schedule_update(uint32_t obj)
{
    const int32_t scene = mem.at<int8_t>(G::scene_number);
    if (scene != of<int32_t>(obj, 0x2EC)) {
        of<int32_t>(obj, 0x2EC) = scene;
        const int32_t n = of<int32_t>(obj, 0x330) + 1;
        of<int32_t>(obj, 0x330) = n;
        if (n > of<int16_t>(obj, 0x70)) {
            task_exit();
            return;
        }
    }
    const int32_t s = of<int16_t>(obj, 0x64);
    if (s <= 0)
        return;
    if (mem.at<uint8_t>(G::event_index) == 7 && (uint32_t(s) & 0xFFFF) == 25)
        return;
    const uint32_t frame = uint32_t(int32_t(mem.f32(pr::camera_frame))) & 0xFFFF;
    const uint32_t table = pr::schedules + ((uint32_t(s) << 16) >> 12);
    uint32_t pos = of<uint32_t>(obj, 0x2F4);
    int32_t vis = of<int32_t>(obj, 0x2F0);
    if (mem.at<uint16_t>(table + 2 * pos) == frame) {
        vis = 1 - vis;
        pos += 1;
    }
    of<uint32_t>(obj, 0x2F4) = pos;
    of<int32_t>(obj, 0x2F0) = vis;
    if (vis == 0 && mem.at<uint16_t>(table + 2 * of<uint32_t>(obj, 0x2F4)) == 0xFFFF)
        task_exit();
}

// 0x3B160 prop_default (most sub-types): the schedule; in Chapter 1 event 7 scene 2 everything but sub-type 7 ends after
// camera frame 170; visible and not stacked: shot (flags bit 0 and a hit bit, and the step can be damaged) or sub-type
// 43 with its event flag set -> its sound, the bullet impact (not 43), the break effect per sub-type, the next damage
// step; past the last step the item drop (the lottery group: only its pick) and Chapter 2 event 4's bookshelf flags,
// then the prop ends; drawn (light 0, T R S, extra models of sub-types 11 / 12 and 26, glow with flags bit 26) and
// shootable unless flags bit 30
template <class P> void Game<P>::prop_default(uint32_t obj)
{
    prop_schedule_update(obj);
    if (mem.at<uint8_t>(G::chapter) == 0 && mem.at<uint8_t>(G::event_index) == 7 &&
        mem.at<uint8_t>(G::scene_number) == 2 && of<uint16_t>(obj, 0xCC) != 7 && mem.f32(pr::camera_frame) > 170.0f) {
        task_exit();
        return;
    }
    if (of<uint32_t>(obj, 0x2F0) == 0 || of<uint8_t>(obj, 0x2E6) != 0)
        return;
    if (!((of<uint32_t>(obj, 0x58) >> 30) & 1)) {
        const uint8_t fb = of<uint8_t>(obj, 0x58);
        bool hit = false;
        if ((fb & 1) && (fb & 6)) {
            const uint32_t r = pr::rec(of<int16_t>(obj, 0xCC));
            hit = mem.u32(r + 0x24 + 4 * of<uint32_t>(obj, 0x32C)) != 0xFFFFFFFFu;
        }
        if (!hit && of<uint16_t>(obj, 0xCC) == 0x2B && of<uint16_t>(obj, 0xCE) != 0 &&
            mem.at<uint8_t>(G::event_flags + uint32_t(int32_t(of<int16_t>(obj, 0xCE)))) != 0)
            hit = true;
        if (hit) {
            sound_cue(mem.u32(pr::rec(of<int16_t>(obj, 0xCC)) + 0x64));
            if (of<uint16_t>(obj, 0xCC) != 0x2B) {
                const float y = of<float>(obj, 0x24) + prop_mark_height(obj);
                prop_view_pos(obj, of<uint32_t>(obj, 0x20), fbits(y), of<uint32_t>(obj, 0x28));
                prop_bullet_impact(obj);
            }
            switch (uint32_t(uint16_t(of<uint16_t>(obj, 0xCC) - 3))) {
            case 0x00: case 0x4E: case 0x4F: case 0x58: case 0x71: case 0x72:
                prop_break_pieces(obj);
                break;
            case 0x05:
                of<uint32_t>(obj, 0x58) |= 0x4000000;
                prop_bookshelf_collapse(obj);
                break;
            case 0x07:
                prop_spurt_spawn(obj);
                break;
            case 0x0A: case 0x30:
                prop_hit_drop(obj);
                break;
            case 0x0D:
                prop_skull_burst(obj);
                break;
            case 0x0F:
                prop018_kkkdolam_break(obj);
                break;
            case 0x15: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F: case 0x6A: case 0x6B: case 0x6C:
            case 0x6D: case 0x6E: case 0x6F: case 0x70: case 0x73:
                prop_collapse_anim_spawn(obj);
                break;
            case 0x17: case 0x53:
                break;
            case 0x28:
                of<uint32_t>(obj, 0x58) |= 0x4000000;
                prop_bookshelf_collapse(obj);
                prop_bookshelf_remains(obj);
                break;
            case 0x31: case 0x38:
                prop_statue_break(obj);
                break;
            case 0x3E: case 0x3F: case 0x43: case 0x45:
                if (of<uint16_t>(obj, 0xCC) == 0x46 ||
                    (of<uint16_t>(obj, 0xCC) == 0x42 && of<uint32_t>(obj, 0x34) == 0xC000))
                    prop_lid_fly(obj);
                else
                    prop_tumble_copy(obj);
                break;
            case 0x44:
                prop_tumble_copy(obj);
                of<uint32_t>(obj, 0x54) = 0x115F;
                of<float>(obj, 0x24) = of<float>(obj, 0x24) + ffrom(0x40F33333);
                prop_tumble_copy(obj);
                break;
            case 0x46:
                prop_tumble_copy(obj);
                prop_plate_shards(obj);
                break;
            default:
                prop_break_anim_spawn(obj);
                break;
            }
            const uint32_t step = of<uint32_t>(obj, 0x32C) + 1;
            of<uint32_t>(obj, 0x32C) = step;
            const uint32_t r = pr::rec(of<int16_t>(obj, 0xCC));
            if (mem.u32(r + 0x20 + 4 * step) == 0) {
                auto drop = [&]() {
                    const uint8_t f = of<uint8_t>(obj, 0x58);
                    if ((f >> 5) & 1)
                        prop_drop_life_item(obj);
                    else if ((f >> 6) & 1)
                        prop_drop_gold_coin(obj);
                    else if ((f >> 7) & 1)
                        prop_drop_gold_frog(obj);
                };
                if (of<int32_t>(obj, 0x58) < 0) {
                    if (mem.at<uint8_t>(pr::lottery_count) == mem.at<uint8_t>(pr::lottery_pick))
                        drop();
                    mem.at<uint8_t>(pr::lottery_count) += 1;
                } else {
                    drop();
                }
                if (of<uint16_t>(obj, 0xCC) == 8 && mem.at<uint8_t>(G::chapter) == 1 &&
                    mem.at<uint8_t>(G::event_index) == 4) {
                    if (mem.at<uint8_t>(G::scene_number) == 2) {
                        if ((of<uint8_t>(obj, 0x58) >> 1) & 1)
                            mem.at<uint8_t>(G::event_flags + 0x10) = 1;
                        else
                            mem.at<uint8_t>(G::event_flags + 0x11) = 1;
                    } else if (mem.at<uint8_t>(G::scene_number) == 6) {
                        mem.at<uint8_t>(G::event_flags + 0x12) = 1;
                    }
                }
                task_exit();
                return;
            }
            if (of<uint16_t>(obj, 0xCC) == 0x33)
                of<uint32_t>(obj, 0x54) = of<uint32_t>(obj, 0x54) + 1;
            else
                of<uint32_t>(obj, 0x54) = mem.u32(r + 0x20 + 4 * of<uint32_t>(obj, 0x32C));
            of<uint32_t>(obj, 0x90) = mem.u32(pr::rec(of<int16_t>(obj, 0xCC)) + 0x34 + 4 * of<uint32_t>(obj, 0x32C));
        }
    }
    prop_light0();
    cam_copro(0x05, {});
    prop_place_xyz(obj);
    prop_scale(of<uint32_t>(obj, 0x94));
    geo_draw_model(of<uint32_t>(obj, 0x54));
    const int32_t type = of<int16_t>(obj, 0xCC);
    if (type > 10) {
        if (type < 13) {
            geo_draw_model(0x13A0);
        } else if (type == 26) {
            cam_copro(0x12, {0, 0x40600000, 0x40000000});
            geo_draw_model(0xD0D);
        }
    }
    if ((of<uint32_t>(obj, 0x58) >> 26) & 1)
        prop_glow(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
    if (!((of<uint32_t>(obj, 0x58) >> 30) & 1))
        prop_shootable(obj);
}

// 0x42460 prop_bullet_impact: a mark task (prop_bullet_impact_mark, model 0x75, metal / stone sub-types 0x8B; random
// angle; sub-types 53 / 56 / 57 / 67 upright, 90 scale 20); +0x308 = the shooter; the mark at the shot's screen point
// at the prop's depth through stored matrix 1, z then from the prop
template <class P> void Game<P>::prop_bullet_impact(uint32_t obj)
{
    const uint32_t t = task_create(F::prop_bullet_impact_mark, 0x3C8);
    of<uint32_t>(t, 0x54) = 0x75;
    of<uint32_t>(t, 0x50) = 0;
    of<uint32_t>(t, 0x2C) = 0x4000;
    of<uint32_t>(t, 0x34) = rand() & 0xFFFF;
    of<uint32_t>(t, 0x94) = 0x3F800000;
    const int32_t type = of<int16_t>(obj, 0xCC);
    switch (type) {
    case 0x00: case 0x03: case 0x05: case 0x08: case 0x14: case 0x18: case 0x2B: case 0x2E: case 0x2F: case 0x30:
    case 0x31: case 0x32: case 0x33: case 0x38: case 0x39: case 0x52: case 0x55: case 0x5B:
        of<uint32_t>(t, 0x54) = 0x8B;
        break;
    default:
        break;
    }
    if (type > 57 ? type == 0x43 : (type >= 56 || type == 53))
        of<uint32_t>(t, 0x2C) = 0;
    if (of<uint16_t>(obj, 0xCC) == 0x5A)
        of<uint32_t>(t, 0x94) = 0x41A00000;
    const uint32_t p = (of<uint8_t>(obj, 0x58) >> 1) & 1 ? 0 : 1;
    of<uint32_t>(obj, 0x308) = p;
    const float z = of<float>(obj, 0x40);
    const float sx = float(int32_t(mem.at<int16_t>(pr::player_shot + p * 0xCC)));
    const float sy = float(int32_t(mem.at<int16_t>(pr::player_shot + 2 + p * 0xCC)));
    const float x = (sx * z) / 496.0f;
    const float y = (sy * z) / 496.0f;
    cam_copro(0x74, {1});
    const Tgp::Out o = cam_copro(0x1A, {fbits(x), fbits(y), fbits(z)});
    of<uint32_t>(t, 0x20) = o.w[0];
    of<uint32_t>(t, 0x24) = o.w[1];
    of<uint32_t>(t, 0x28) = o.w[2];
    of<uint32_t>(t, 0x28) = of<uint32_t>(obj, 0x28);
    cam_copro(0x06, {});
}

// 0x42F20 prop_bullet_impact_mark: 21 frames, facing the camera (billboard matrix), turned by +0x34 / +0x2C, model
// +0x54 + frame
template <class P> void Game<P>::prop_bullet_impact_mark(uint32_t obj)
{
    const int32_t t = of<int32_t>(obj, 0x50);
    of<int32_t>(obj, 0x50) = t + 1;
    if (t > 20) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    prop_billboard();
    cam_copro(0x16, {of<uint32_t>(obj, 0x34)});
    cam_copro(0x14, {of<uint32_t>(obj, 0x2C)});
    prop_scale(of<uint32_t>(obj, 0x94));
    geo_draw_model(of<uint32_t>(obj, 0x54) + of<uint32_t>(obj, 0x50));
    cam_copro(0x06, {});
}

// the facing matrix (coprocessor 0x87 with the 9 words at 0xAA690)
template <class P> void Game<P>::prop_billboard()
{
    uint32_t w[9];
    for (uint32_t i = 0; i < 9; ++i)
        w[i] = mem.u32(pr::billboard + 4 * i);
    Tgp::Out none;
    copro(0x87, w, 9, none);
}

// 0x42860 obj_copy_pose: position and angles of b into a
template <class P> void Game<P>::obj_copy_pose(uint32_t a, uint32_t b)
{
    for (uint32_t o = 0x20; o <= 0x34; o += 4)
        of<uint32_t>(a, o) = of<uint32_t>(b, o);
}

// 0x3FE90 prop_break_anim_spawn: prop_break_anim at the prop, model 0x758, random y angle; scale 1.5 (11 / 12 0.8;
// 34 / 35 / 42 0.35 two higher; 75 0.5 2.2 higher)
template <class P> void Game<P>::prop_break_anim_spawn(uint32_t obj)
{
    const uint32_t t = task_create(F::prop_break_anim, 0x3C8);
    obj_copy_pose(t, obj);
    of<uint32_t>(t, 0x54) = 0x758;
    const uint32_t r = rand();
    of<uint32_t>(t, 0x30C) = 0;
    of<uint16_t>(t, 0x6A) = 0;
    of<uint32_t>(t, 0x30) = r & 0xFFFF;
    uint32_t s;
    switch (of<int16_t>(obj, 0xCC)) {
    case 34: case 35: case 42:
        of<float>(t, 0x24) = of<float>(t, 0x24) + 2.0f;
        s = 0x3EB33333;
        break;
    case 11: case 12:
        s = 0x3F4CCCCD;
        break;
    case 0x4B:
        of<float>(t, 0x24) = of<float>(t, 0x24) + ffrom(0x400CCCCD);
        s = 0x3F000000;
        break;
    default:
        s = 0x3FC00000;
        break;
    }
    of<uint32_t>(t, 0x94) = s;
}

// 0x3FD80 prop_break_anim: +0x30C += 0.5 until 29.5; model +0x54 + (int)+0x30C four lower, turned by y, scaled, glow
template <class P> void Game<P>::prop_break_anim(uint32_t obj)
{
    const float f = of<float>(obj, 0x30C) + 0.5f;
    of<float>(obj, 0x30C) = f;
    if (!(f < 29.5f)) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), fbits(of<float>(obj, 0x24) - 4.0f), of<uint32_t>(obj, 0x28)});
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    prop_scale(of<uint32_t>(obj, 0x94));
    const uint32_t m = uint32_t(int32_t(of<float>(obj, 0x30C))) + of<uint32_t>(obj, 0x54);
    geo_draw_model(m);
    cam_copro(0x77, {3});
    draw_fade_begin(0);
    geo_draw_model(uint32_t(int32_t(of<float>(obj, 0x30C))) + of<uint32_t>(obj, 0x54));
    geo_send_words(0, 31, 0x7A0);
    cam_copro(0x06, {});
}

// 0x40070 prop_collapse_anim_spawn: prop_collapse_anim at the prop with the sub-type's collapse models (24 kibako
// 0x36F, 109-115 kont / rkont / bagbag, else tarun_dam 0x6D0), the prop's scale
template <class P> void Game<P>::prop_collapse_anim_spawn(uint32_t obj)
{
    const uint32_t t = task_create(F::prop_collapse_anim, 0x3C8);
    obj_copy_pose(t, obj);
    uint32_t m;
    switch (of<int16_t>(obj, 0xCC)) {
    case 24: m = 0x36F; break;
    case 0x6D: m = 0x15C3; break;
    case 0x6E: m = 0x185D; break;
    case 0x6F: m = 0x1A05; break;
    case 0x70: m = 0x168F; break;
    case 0x71: m = 0x18ED; break;
    case 0x72: m = 0x14DA; break;
    case 0x73: m = 0x1716; break;
    default: m = 0x6D0; break;
    }
    of<uint32_t>(t, 0x54) = m;
    of<uint32_t>(t, 0x50) = 0;
    of<uint16_t>(t, 0x6A) = 0;
    of<uint32_t>(t, 0x94) = of<uint32_t>(obj, 0x94);
}

// 0x3FF70 prop_collapse_anim: model +0x54 + frame for 49 frames (blinking on even frames after 39), turned by y, glow
template <class P> void Game<P>::prop_collapse_anim(uint32_t obj)
{
    const int32_t t = of<int32_t>(obj, 0x50);
    of<int32_t>(obj, 0x50) = t + 1;
    if (t > 48) {
        task_exit();
        return;
    }
    const int32_t n = of<int32_t>(obj, 0x50);
    if (n > 39 && n % 2 == 0)
        return;
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    prop_scale(of<uint32_t>(obj, 0x94));
    geo_draw_model(of<uint32_t>(obj, 0x54) + of<uint32_t>(obj, 0x50));
    cam_copro(0x77, {3});
    draw_fade_begin(0);
    geo_draw_model(of<uint32_t>(obj, 0x54) + of<uint32_t>(obj, 0x50));
    geo_send_words(0, 31, 0x7A0);
    cam_copro(0x06, {});
}

// 0x401D0 prop_skull_burst: five pieces (prop_piece_fall, models 0x2CA / 0x2CB alternating, glow) at the prop with
// random x / z speeds (rand % 41 - 20) / 100 and y speed 0.8
template <class P> void Game<P>::prop_skull_burst(uint32_t obj)
{
    for (uint32_t i = 0; (i & 0xFF) <= 4; ++i) {
        const uint32_t t = task_create(F::prop_piece_fall, 0x3C8);
        task_clear_work(t);
        obj_copy_pose(t, obj);
        of<uint32_t>(t, 0x54) = 0x2CA;
        of<uint32_t>(t, 0x94) = of<uint32_t>(obj, 0x94);
        of<uint32_t>(t, 0x58) |= 0x4000000;
        if (i & 1)
            of<uint32_t>(t, 0x54) = 0x2CB;
        int32_t r = int32_t(rand()) % 41 - 20;
        of<uint32_t>(t, 0x7C) = 0x3F4CCCCD;
        of<float>(t, 0x78) = pdiv(float(r), 100.0f);
        r = int32_t(rand()) % 41 - 20;
        of<uint32_t>(t, 0x50) = 0;
        of<uint16_t>(t, 0x6A) = 0;
        of<float>(t, 0x80) = pdiv(float(r), 100.0f);
    }
}

// 0x42D90 prop_piece_fall: 60 frames; above the floor: moves, gravity 0.05, spins (+0x31C..); drawn (prop_draw),
// blinking after frame 29
template <class P> void Game<P>::prop_piece_fall(uint32_t obj)
{
    const int32_t t = of<int32_t>(obj, 0x50);
    of<int32_t>(obj, 0x50) = t + 1;
    if (t > 59) {
        task_exit();
        return;
    }
    float y = of<float>(obj, 0x24);
    if (!(y < mem.f32(G::floor_y))) {
        of<uint32_t>(obj, 0x2C) += of<uint32_t>(obj, 0x31C);
        of<float>(obj, 0x20) = of<float>(obj, 0x20) + of<float>(obj, 0x78);
        of<float>(obj, 0x28) = of<float>(obj, 0x28) + of<float>(obj, 0x80);
        const float vy = of<float>(obj, 0x7C) - ffrom(0x3D4CCCCD);
        of<uint32_t>(obj, 0x30) += of<uint32_t>(obj, 0x320);
        of<uint32_t>(obj, 0x34) += of<uint32_t>(obj, 0x324);
        of<float>(obj, 0x7C) = vy;
        y = y + vy;
    }
    of<float>(obj, 0x24) = y;
    const int32_t n = of<int32_t>(obj, 0x50);
    if (n % 2 != 0 || !(n > 29))
        prop_draw(obj);
}

// 0x41E20 prop018_kkkdolam_break (sub-type 18, the drum): two pieces (prop_break_piece, models 0x15F / 0x161, glow)
// five higher, random spins, thrown out by the angle rand % 91 - 45 + 180 * i (coprocessor 0x1B / 0x1C) / 6.5,
// vy 0.6 + (rand % 40) / 100, gravity -0.02 - (rand % 20) / 1000, floor 0.8, bounce 0.6
template <class P> void Game<P>::prop018_kkkdolam_break(uint32_t obj)
{
    for (uint32_t i = 0; (i & 0xFF) <= 1; ++i) {
        const uint32_t t = task_create(F::prop_break_piece, 0x3C8);
        task_clear_work(t);
        prop_piece_random(t, obj);
        of<uint32_t>(t, 0x54) = 0x15F;
        of<uint32_t>(t, 0x58) |= 0x4000000;
        if ((i & 0xFF) != 0)
            of<uint32_t>(t, 0x54) = 0x161;
        of<float>(t, 0x24) = of<float>(t, 0x24) + 5.0f;
        prop_piece_spin(t, 0x7FF);
        of<float>(t, 0x7C) = pdiv(float(int32_t(rand()) % 40), 100.0f) + ffrom(0x3F19999A);
        const int32_t r = int32_t(rand()) % 0x5B;
        const int32_t deg = int32_t((i & 0xFF) * 0xB4) + r - 45;
        const uint32_t a = uint32_t(int32_t(float(deg) * ffrom(0x43360B61)));
        prop_piece_throw(t, a, 6.5f);
        of<uint32_t>(t, 0x354) = 0x3F4CCCCD;
        of<uint32_t>(t, 0x358) = 0x3F19999A;
    }
}

// a random spin on one random axis (+0x31C + 4 * (rand % 3)): rand % limit, negated on an even rand (inline)
template <class P> void Game<P>::prop_piece_spin(uint32_t t, uint32_t limit)
{
    const int32_t axis = int32_t(rand()) % 3;
    const int32_t v = int32_t(rand()) % int32_t(limit);
    const int32_t r = int32_t(rand());
    of<int32_t>(t, 0x31C + 4 * uint32_t(axis)) = r % 2 != 0 ? v : -v;
}

// the throw out by angle a (coprocessor 0x1B / 0x1C: sine / cosine) / div into +0x78 / +0x80, after the gravity
// -0.02 - (rand % 20) / 1000 into +0x88 (inline in the piece spawners)
template <class P> void Game<P>::prop_piece_throw(uint32_t t, uint32_t a, float div)
{
    const Tgp::Out s = cam_copro(0x1B, {a});
    of<float>(t, 0x78) = ffrom(s.w[0]) / div + of<float>(t, 0x78);
    of<float>(t, 0x88) = ffrom(0xBCA3D70A) - pdiv(float(int32_t(rand()) % 20), 1000.0f);
    const Tgp::Out c = cam_copro(0x1C, {a});
    of<float>(t, 0x80) = ffrom(c.w[0]) / div + of<float>(t, 0x80);
}

// 0x428A0 prop_piece_random: a fragment of the prop: its position +-1 (rand % 21 - 10) / 10, x / z speeds
// (rand % 20) / 1000, random angles, spin cleared, the prop's sub-type and scale
template <class P> void Game<P>::prop_piece_random(uint32_t t, uint32_t obj)
{
    for (uint32_t o = 0x20; o <= 0x28; o += 4) {
        const int32_t r = int32_t(rand()) % 21 - 10;
        of<float>(t, o) = pdiv(float(r), 10.0f) + of<float>(obj, o);
    }
    of<float>(t, 0x78) = pdiv(float(int32_t(rand()) % 20), 1000.0f);
    of<float>(t, 0x80) = pdiv(float(int32_t(rand()) % 20), 1000.0f);
    of<uint32_t>(t, 0x2C) = rand() & 0xFFFF;
    of<uint32_t>(t, 0x30) = rand() & 0xFFFF;
    const uint32_t r = rand();
    of<uint32_t>(t, 0x31C) = 0;
    of<uint32_t>(t, 0x320) = 0;
    of<uint32_t>(t, 0x324) = 0;
    of<uint32_t>(t, 0x34) = r & 0xFFFF;
    of<uint16_t>(t, 0xCC) = of<uint16_t>(obj, 0xCC);
    of<uint32_t>(t, 0x50) = 0;
    of<uint16_t>(t, 0x6A) = 0;
    of<uint32_t>(t, 0x94) = of<uint32_t>(obj, 0x94);
}

// 0x403F0 prop_break_piece: 91 frames; moves; at the floor (+0x354 above floor_y) the speed becomes the bounce value
// +0x358 (then 0.1 less); y speed += gravity +0x88, x / z speeds grow by 1 / 150; spins; drawn, blinking after 59
template <class P> void Game<P>::prop_break_piece(uint32_t obj)
{
    const int32_t t = of<int32_t>(obj, 0x50) + 1;
    of<int32_t>(obj, 0x50) = t;
    if (t > 90) {
        task_exit();
        return;
    }
    float vx = of<float>(obj, 0x78), vy = of<float>(obj, 0x7C), vz = of<float>(obj, 0x80);
    of<float>(obj, 0x20) = of<float>(obj, 0x20) + vx;
    of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
    of<float>(obj, 0x28) = of<float>(obj, 0x28) + vz;
    if (!(of<float>(obj, 0x24) > mem.f32(G::floor_y) + of<float>(obj, 0x354))) {
        vy = of<float>(obj, 0x358);
        of<float>(obj, 0x358) = vy - ffrom(0x3DCCCCCD);
    }
    const float ax = vx / 150.0f, az = vz / 150.0f;
    of<uint32_t>(obj, 0x2C) += of<uint32_t>(obj, 0x31C);
    vy = vy + of<float>(obj, 0x88);
    vx = ax + vx;
    vz = az + vz;
    of<uint32_t>(obj, 0x30) += of<uint32_t>(obj, 0x320);
    of<uint32_t>(obj, 0x34) += of<uint32_t>(obj, 0x324);
    const int32_t n = of<int32_t>(obj, 0x50);
    of<float>(obj, 0x7C) = vy;
    of<float>(obj, 0x78) = vx;
    of<float>(obj, 0x80) = vz;
    if (!(n > 59) || n % 2 != 0)
        prop_draw(obj);
}

// 0x412C0 prop_statue_break (sub-types 52 / 59): ten pieces (prop_piece_bounce; models 0xD05 / 0xD06, Chapter 2
// 0x14C2 / 0x14C3; piece 5 0xD04 / Chapter 2 0x14C1, the others at 1.5 x scale) 18 higher, random spins, thrown out
// by the angle (rand % 9 - 4 + 36 * i) / 4.5, vy 0.35 + (rand % 10) / 100, floor 0.3, bounce 0.6
template <class P> void Game<P>::prop_statue_break(uint32_t obj)
{
    for (uint32_t i = 0; (i & 0xFF) <= 9; ++i) {
        const uint32_t t = task_create(F::prop_piece_bounce, 0x3C8);
        task_clear_work(t);
        prop_piece_random(t, obj);
        const uint32_t odd = i & 1;
        of<uint32_t>(t, 0x54) = 0xD05 + odd;
        of<uint32_t>(t, 0x58) |= 0x4000000;
        if (mem.at<uint8_t>(G::chapter) == 1)
            of<uint32_t>(t, 0x54) = 0x14C2 + odd;
        if ((i & 0xFF) == 5) {
            of<uint32_t>(t, 0x54) = 0xD04;
            if (mem.at<uint8_t>(G::chapter) == 1)
                of<uint32_t>(t, 0x54) = 0x14C1;
        } else {
            of<float>(t, 0x94) = of<float>(obj, 0x94) * 1.5f;
        }
        prop_piece_spin(t, 0x8FF);
        const int32_t r = int32_t(rand()) % 9;
        const int32_t deg = r - 4 + int32_t((i & 0xFF) * 9) * 4;
        const uint32_t a = uint32_t(int32_t(float(deg) * ffrom(0x43360B61)));
        of<float>(t, 0x24) = of<float>(t, 0x24) + 18.0f;
        const Tgp::Out s = cam_copro(0x1B, {a});
        of<float>(t, 0x78) = ffrom(s.w[0]) / 4.5f + of<float>(t, 0x78);
        of<float>(t, 0x7C) = pdiv(float(int32_t(rand()) % 10), 100.0f) + ffrom(0x3EB33333);
        of<float>(t, 0x88) = ffrom(0xBCA3D70A) - pdiv(float(int32_t(rand()) % 20), 1000.0f);
        const Tgp::Out c = cam_copro(0x1C, {a});
        of<uint32_t>(t, 0x354) = 0x3E99999A;
        of<uint32_t>(t, 0x358) = 0x3F19999A;
        of<float>(t, 0x80) = ffrom(c.w[0]) / 4.5f + of<float>(t, 0x80);
    }
}

// 0x416A0 prop_tumble_copy: a copy of the prop (prop_piece_bounce, its model and position) tumbling away: random spin,
// gravity -0.02 - (rand % 20) / 1000; sub-type 58 by a random angle -(rand & 0x4FFF) / 4.5, else away from the
// camera (coprocessor 0x0C, the unit vector) x 0.8 with vy 0.4 + (rand % 30) / 100 (vy first 0.2 + (rand % 20) /
// 100); sub-type 0 slower (x 0.2, vy 0.8, gravity -0.04, x spin 0x1000); floor 0.5, bounce 0.6
template <class P> void Game<P>::prop_tumble_copy(uint32_t obj)
{
    const uint32_t t = task_create(F::prop_piece_bounce, 0x3C8);
    task_clear_work(t);
    prop_piece_random(t, obj);
    of<uint32_t>(t, 0x54) = of<uint32_t>(obj, 0x54);
    of<uint32_t>(t, 0x20) = of<uint32_t>(obj, 0x20);
    of<uint32_t>(t, 0x24) = of<uint32_t>(obj, 0x24);
    of<uint32_t>(t, 0x28) = of<uint32_t>(obj, 0x28);
    prop_piece_spin(t, 0x6FF);
    of<float>(t, 0x7C) = pdiv(float(int32_t(rand()) % 20), 100.0f) + ffrom(0x3E4CCCCD);
    of<float>(t, 0x88) = ffrom(0xBCA3D70A) - pdiv(float(int32_t(rand()) % 20), 1000.0f);
    if (of<uint16_t>(obj, 0xCC) == 58) {
        const uint32_t a = 0u - (rand() & 0x4FFF);
        of<uint32_t>(t, 0x30) = a;
        const Tgp::Out sn = cam_copro(0x1B, {a});
        of<float>(t, 0x78) = ffrom(sn.w[0]) / 4.5f;
        const Tgp::Out cs = cam_copro(0x1C, {of<uint32_t>(t, 0x30)});
        of<float>(t, 0x80) = ffrom(cs.w[0]) / 4.5f;
    } else {
        const float dx = of<float>(obj, 0x20) - mem.f32(pr::eye);
        const float dy = of<float>(obj, 0x24) - mem.f32(pr::eye + 4);
        const float dz = of<float>(obj, 0x28) - mem.f32(pr::eye + 8);
        const Tgp::Out u = cam_copro(0x0C, {fbits(dx), fbits(dy), fbits(dz)});
        of<uint32_t>(t, 0x78) = u.w[0];
        of<uint32_t>(t, 0x7C) = u.w[1];
        of<float>(t, 0x78) = of<float>(t, 0x78) * ffrom(0x3F4CCCCD);
        of<float>(t, 0x80) = ffrom(u.w[2]) * ffrom(0x3F4CCCCD);
        of<float>(t, 0x7C) = pdiv(float(int32_t(rand()) % 30), 100.0f) + ffrom(0x3ECCCCCD);
    }
    if (of<uint16_t>(obj, 0xCC) == 0) {
        const float vx = of<float>(t, 0x78) * ffrom(0x3E4CCCCD);
        const float vz = of<float>(t, 0x80) * ffrom(0x3E4CCCCD);
        of<uint32_t>(t, 0x320) = 0x1000;
        of<uint32_t>(t, 0x7C) = 0x3F4CCCCD;
        of<uint32_t>(t, 0x88) = 0xBD23D70A;
        of<float>(t, 0x78) = vx;
        of<float>(t, 0x80) = vz;
    }
    of<uint32_t>(t, 0x354) = 0x3F000000;
    of<uint32_t>(t, 0x358) = 0x3F19999A;
}

// 0x40520 prop_piece_bounce: a tumbling piece, 91 frames: moves; walls of the Chapter 2 rooms by sub-type (59 the
// corner x 924..936 / z 230: speed -1/8; 71 / 72 x 430; 72 y -113; 65 / 66 / 72 z 83; 73 y -110 or the floor, z -22:
// reflected with the sub-type's sound); at the floor (+0x354 above floor_y; sub-type 59: y -68.4) the bounce value
// +0x358 (then 0.1 less); gravity +0x88, x / z speeds grow by 1 / 150; spins; drawn, blinking after 59
template <class P> void Game<P>::prop_piece_bounce(uint32_t obj)
{
    const int32_t t = of<int32_t>(obj, 0x50) + 1;
    of<int32_t>(obj, 0x50) = t;
    if (t > 90) {
        task_exit();
        return;
    }
    auto snd = [&]() { sound_cue(mem.u32(pr::rec(of<int16_t>(obj, 0xCC)) + 0x64)); };
    of<float>(obj, 0x24) = of<float>(obj, 0x24) + of<float>(obj, 0x7C);
    float vx = of<float>(obj, 0x78), vz = of<float>(obj, 0x80);
    const float x = of<float>(obj, 0x20) + vx;
    const float z = of<float>(obj, 0x28) + vz;
    const uint16_t type = of<uint16_t>(obj, 0xCC);
    if (int32_t(int16_t(type)) == 59) {
        if (!(x > 924.0f && !(x >= 936.0f) && z > 230.0f)) {
            vx = -vx / 8.0f;
            vz = -vz / 8.0f;
        }
    }
    of<float>(obj, 0x80) = vz;
    of<float>(obj, 0x78) = vx;
    of<float>(obj, 0x28) = z;
    of<float>(obj, 0x20) = x;
    if (type == 0x48 || type == 0x47) {
        if (of<float>(obj, 0x20) > 430.0f) {
            of<float>(obj, 0x78) = of<float>(obj, 0x78) * ffrom(0xBF19999A);
            of<float>(obj, 0x80) = of<float>(obj, 0x80) * ffrom(0xBF19999A);
            of<float>(obj, 0x20) = 430.0f;
            snd();
        }
        if (of<float>(obj, 0x24) < -113.0f && of<uint16_t>(obj, 0xCC) == 0x48) {
            of<float>(obj, 0x7C) = of<float>(obj, 0x7C) * ffrom(0xBF4CCCCD);
            of<float>(obj, 0x24) = -113.0f;
            snd();
        }
    }
    const uint16_t t2 = of<uint16_t>(obj, 0xCC);
    if ((t2 == 0x41 || t2 == 0x48 || t2 == 0x42) && of<float>(obj, 0x28) > 83.0f) {
        of<float>(obj, 0x78) = of<float>(obj, 0x78) * -0.5f;
        of<float>(obj, 0x80) = of<float>(obj, 0x80) * -0.5f;
        of<float>(obj, 0x28) = 83.0f;
        snd();
    }
    if (of<uint16_t>(obj, 0xCC) == 0x49) {
        const float y = of<float>(obj, 0x24);
        if (y < -110.0f && of<float>(obj, 0x28) > 24.0f && of<float>(obj, 0x20) < ffrom(0x4416C000)) {
            of<float>(obj, 0x7C) = of<float>(obj, 0x7C) * ffrom(0xBF4CCCCD);
            of<float>(obj, 0x24) = -110.0f;
            snd();
        } else if (y < mem.f32(G::floor_y)) {
            of<float>(obj, 0x7C) = of<float>(obj, 0x7C) * ffrom(0xBF4CCCCD);
            of<float>(obj, 0x24) = mem.f32(G::floor_y);
            snd();
        }
        if (of<float>(obj, 0x28) < -22.0f) {
            of<float>(obj, 0x80) = of<float>(obj, 0x80) * 0.5f;
            of<float>(obj, 0x28) = -22.0f;
            snd();
        }
    }
    const int32_t t3 = of<int16_t>(obj, 0xCC);
    float vy = of<float>(obj, 0x7C);
    bool bounce;
    if (t3 != 59)
        bounce = !(of<float>(obj, 0x24) > mem.f32(G::floor_y) + of<float>(obj, 0x354));
    else
        bounce = !(of<float>(obj, 0x24) > ffrom(0xC288CCCD));
    if (bounce) {
        vy = of<float>(obj, 0x358);
        of<float>(obj, 0x358) = vy - ffrom(0x3DCCCCCD);
    }
    vx = of<float>(obj, 0x78);
    vz = of<float>(obj, 0x80);
    const float ax = vx / 150.0f, az = vz / 150.0f;
    of<uint32_t>(obj, 0x2C) += of<uint32_t>(obj, 0x31C);
    vy = vy + of<float>(obj, 0x88);
    of<uint32_t>(obj, 0x30) += of<uint32_t>(obj, 0x320);
    of<uint32_t>(obj, 0x34) += of<uint32_t>(obj, 0x324);
    const int32_t n = of<int32_t>(obj, 0x50);
    of<float>(obj, 0x7C) = vy;
    of<float>(obj, 0x78) = ax + vx;
    of<float>(obj, 0x80) = az + vz;
    if (!(n > 59) || n % 2 != 0)
        prop_draw(obj);
}

// the glow colour of items: frame_counter % 32 as a triangle 0..15..0, doubled (inline)
template <class P> uint32_t Game<P>::prop_glow_phase()
{
    int32_t r = mem.at<int32_t>(G::frame_counter) % 32;
    if (r > 15)
        r = 31 - r;
    return uint32_t(r << 1);
}

// 0x3D4F0 prop_drop_life_item: the life item (item_life_frame, model 0x312, radius 3.5, scale 2.2, flags 0x401) at the
// destroyed prop, two above its floor (sub-type 49: at 2.0), the prop's schedule state; not in Chapter 4 (the prop ends
// instead); turned to face the camera
template <class P> void Game<P>::prop_drop_life_item(uint32_t obj)
{
    const uint32_t t = task_create(F::item_life_frame, 0x3C8);
    task_clear_work(t);
    of<uint32_t>(t, 0x58) = 0x401;
    of<uint32_t>(t, 0x54) = 0x312;
    of<uint32_t>(t, 0x90) = 0x40600000;
    of<uint16_t>(t, 0xCC) = 0;
    of<uint8_t>(t, 0x2E6) = 0;
    of<uint32_t>(t, 0x94) = 0x400CCCCD;
    of<int32_t>(t, 0x2EC) = mem.at<int8_t>(G::scene_number);
    of<uint32_t>(t, 0x330) = of<uint32_t>(obj, 0x330);
    of<uint32_t>(t, 0x2F0) = of<uint32_t>(obj, 0x2F0);
    of<uint32_t>(t, 0x2F4) = of<uint32_t>(obj, 0x2F4);
    of<uint16_t>(t, 0x70) = of<uint16_t>(obj, 0x70);
    of<uint32_t>(t, 0x20) = of<uint32_t>(obj, 0x20);
    of<float>(t, 0x24) = (of<float>(obj, 0x24) - mem.f32(pr::rec(of<int16_t>(obj, 0xCC)) + 0x4C)) + 2.0f;
    if (of<uint16_t>(obj, 0xCC) == 49)
        of<float>(t, 0x24) = 2.0f;
    of<uint32_t>(t, 0x2C) = 0;
    of<uint32_t>(t, 0x28) = of<uint32_t>(obj, 0x28);
    of<uint32_t>(t, 0x34) = 0;
    of<uint32_t>(t, 0x50) = 0;
    of<uint16_t>(t, 0x6A) = 0;
    of<uint32_t>(t, 0x30) = of<uint32_t>(obj, 0x30);
    if (mem.at<uint8_t>(G::chapter) == 3) {
        task_exit();
        return;
    }
    prop_face_camera(t);
    of<uint32_t>(t, 0x30) += 0x8000;
}

// 0x3D260 item_life_frame: 121 frames after it is taken; the schedule; taken (shot: flags bit 28): sound 0xA01351,
// item_gift_spawn (Chapter 3 event 0: flag 0x51E995 = 2), then the pick-up glow (model 0x2E0 + frame, colour blend)
// for 49 frames; drawn (light 0, T R S) every other frame once taken; shootable
template <class P> void Game<P>::item_life_frame(uint32_t obj)
{
    const uint32_t phase = prop_glow_phase();
    if (of<int32_t>(obj, 0x50) > 120) {
        task_exit();
        return;
    }
    prop_schedule_update(obj);
    if (of<uint32_t>(obj, 0x2F0) == 0 || of<uint8_t>(obj, 0x2E6) != 0)
        return;
    const uint8_t fb = of<uint8_t>(obj, 0x58);
    if ((fb & 1) && (fb & 6) && !((of<uint32_t>(obj, 0x58) >> 28) & 1)) {
        of<uint32_t>(obj, 0x58) |= 0x10000000;
        sound_cue(0xA01351);
        item_gift_spawn(obj);
        if (mem.at<uint8_t>(G::chapter) == 2 && mem.at<uint8_t>(G::event_index) == 0)
            mem.at<uint8_t>(G::event_flags + 0x55) = 2;
    }
    int32_t n = of<int32_t>(obj, 0x50);
    if ((of<uint32_t>(obj, 0x58) >> 28) & 1)
        n += 1;
    of<int32_t>(obj, 0x50) = n;
    if (n != 0 && !(of<int32_t>(obj, 0x50) > 49)) {
        const uint32_t c = colour_blend32(0xFF44, 0xFE52, phase);
        cam_copro(0x05, {});
        cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
        cam_copro(0x86, {0, 0, 0});
        prop_scale(0x3F4CCCCD);
        mem.at<uint16_t>(pr::sparkle_colour) = uint16_t(c);
        geo_draw_model(of<uint32_t>(obj, 0x50) + 0x2E0);
        cam_copro(0x06, {});
    }
    const int32_t m = of<int32_t>(obj, 0x50);
    if (m % 2 != 0 || m == 0) {
        prop_light0();
        cam_copro(0x05, {});
        cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
        cam_copro(0x86, {of<uint32_t>(obj, 0x34), of<uint32_t>(obj, 0x30), of<uint32_t>(obj, 0x2C)});
        prop_scale(of<uint32_t>(obj, 0x94));
        geo_draw_model(of<uint32_t>(obj, 0x54));
        cam_copro(0x06, {});
    }
    prop_shootable(obj);
}

// 0x42420 item_gift_spawn: hostage_gift_task at the object for its shooter (+0x2EB: flags bit 1 = player 1 (0),
// else player 2)
template <class P> void Game<P>::item_gift_spawn(uint32_t obj)
{
    const uint32_t t = task_create(F::hostage_gift_task, 0x3C8);
    obj_copy_pose(t, obj);
    of<uint32_t>(t, 0x50) = 0;
    of<uint8_t>(t, 0x2EB) = (of<uint8_t>(obj, 0x58) >> 1) & 1 ? 0 : 1;
    if (coop4_on())   // 4P: the real shooter (the hit table)
        of<uint8_t>(t, 0x2EB) = uint8_t(coop4_shooter(obj, of<uint8_t>(t, 0x2EB)));
}

// 0x3D920 prop_drop_gold_coin: the gold coin (prop_coin_run, model 0x3B5, scale 1.4, radius 2, flags 0x401) at the
// destroyed prop 0.22 above the floor, upright (x angle 0x4000), the prop's schedule state; not in Chapter 4
template <class P> void Game<P>::prop_drop_gold_coin(uint32_t obj)
{
    const uint32_t t = task_create(F::prop_coin_run, 0x3C8);
    task_clear_work(t);
    of<uint32_t>(t, 0x54) = 0x3B5;
    of<uint16_t>(t, 0xCC) = 0;
    of<uint32_t>(t, 0x58) = 0x401;
    of<uint32_t>(t, 0x94) = 0x3FB33333;
    of<uint8_t>(t, 0x2E6) = 0;
    of<uint32_t>(t, 0x90) = 0x40000000;
    of<int32_t>(t, 0x2EC) = mem.at<int8_t>(G::scene_number);
    of<uint32_t>(t, 0x330) = of<uint32_t>(obj, 0x330);
    of<uint32_t>(t, 0x2F0) = of<uint32_t>(obj, 0x2F0);
    of<uint32_t>(t, 0x2F4) = of<uint32_t>(obj, 0x2F4);
    of<uint16_t>(t, 0x70) = of<uint16_t>(obj, 0x70);
    of<float>(t, 0x24) = float(double(mem.f32(G::floor_y)) + ffrom64(0x3FCC28F5C28F5C29ull));
    of<uint32_t>(t, 0x20) = of<uint32_t>(obj, 0x20);
    of<uint32_t>(t, 0x30) = 0;
    of<uint32_t>(t, 0x34) = 0;
    of<uint32_t>(t, 0x50) = 0;
    of<uint16_t>(t, 0x6A) = 0;
    of<uint32_t>(t, 0x2C) = 0x4000;
    of<uint32_t>(t, 0x28) = of<uint32_t>(obj, 0x28);
    if (mem.at<uint8_t>(G::chapter) == 3)
        task_exit();
}

// 0x3D680 prop_coin_run: the schedule; shot: sound 0xA01351, its camera point, a sparkle, 200 points to the shooter,
// taken (flags bit 28, hit bits off, angle 0) and a tumbling copy; taken: gone after 180 frames, else it spins
// (+0x1000 a frame); drawn facing the camera (billboard, Ry, Rx, S, glow) and shootable until taken
template <class P> void Game<P>::prop_coin_run(uint32_t obj)
{
    prop_schedule_update(obj);
    if ((of<uint8_t>(obj, 0x58) & 6) && !((of<uint32_t>(obj, 0x58) >> 28) & 1)) {
        sound_cue(0xA01351);
        const float y = of<float>(obj, 0x24) + prop_mark_height(obj);
        prop_view_pos(obj, of<uint32_t>(obj, 0x20), fbits(y), of<uint32_t>(obj, 0x28));
        item_sparkle_spawn(of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28));
        add_score(prop_shooter(obj), 200);
        of<uint32_t>(obj, 0x30) = 0;
        of<uint32_t>(obj, 0x58) = (of<uint32_t>(obj, 0x58) | 0x10000000) & 0xFFFFFFF9u;
        prop_tumble_copy(obj);
    }
    if ((of<uint32_t>(obj, 0x58) >> 28) & 1) {
        const int32_t n = of<int32_t>(obj, 0x50) + 1;
        of<int32_t>(obj, 0x50) = n;
        if (n > 180) {
            task_exit();
            return;
        }
    } else {
        of<uint32_t>(obj, 0x30) += 0x1000;
    }
    if (of<uint32_t>(obj, 0x2F0) == 0 || of<uint8_t>(obj, 0x2E6) != 0 || ((of<uint32_t>(obj, 0x58) >> 28) & 1))
        return;
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    prop_billboard();
    cam_copro(0x15, {of<uint32_t>(obj, 0x30)});
    cam_copro(0x14, {of<uint32_t>(obj, 0x2C)});
    prop_scale(of<uint32_t>(obj, 0x94));
    geo_draw_model(of<uint32_t>(obj, 0x54));
    prop_glow(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
    prop_shootable(obj);
}

// 0x3DB30 prop_drop_gold_frog: the gold frog (prop_gold_frog_init, creature type 91, flags | 0x401) on the floor at
// the destroyed prop, its schedule state; not in Chapter 4
template <class P> void Game<P>::prop_drop_gold_frog(uint32_t obj)
{
    const uint32_t t = task_create(F::prop_gold_frog_init, 0x3C8);
    task_clear_work(t);
    of<uint16_t>(t, 0xCC) = 0x5B;
    of<uint32_t>(t, 0x58) |= 0x401;
    of<uint16_t>(t, 0x6A) = 0;
    of<uint16_t>(t, 0x70) = of<uint16_t>(obj, 0x70);
    of<int32_t>(t, 0x2EC) = mem.at<int8_t>(G::scene_number);
    of<uint32_t>(t, 0x330) = of<uint32_t>(obj, 0x330);
    of<uint32_t>(t, 0x2F0) = of<uint32_t>(obj, 0x2F0);
    of<uint32_t>(t, 0x2F4) = of<uint32_t>(obj, 0x2F4);
    of<uint32_t>(t, 0x20) = of<uint32_t>(obj, 0x20);
    of<uint32_t>(t, 0x24) = mem.u32(G::floor_y);
    of<uint32_t>(t, 0x2C) = 0;
    of<uint32_t>(t, 0x28) = of<uint32_t>(obj, 0x28);
    of<uint32_t>(t, 0x34) = 0;
    of<uint32_t>(t, 0x30) = of<uint32_t>(obj, 0x30);
    of<uint16_t>(t, 0x64) = of<uint16_t>(obj, 0x64);
    if (mem.at<uint8_t>(G::chapter) == 3)
        task_exit();
}

// 0x429B0 prop_gold_frog_init: body (motion 0xB2) and type set-up, motion_flags2 bit 0, flags2 bit 0 toggled; faces the
// camera (not in Chapter 4); prop_gold_frog_run
template <class P> void Game<P>::prop_gold_frog_init(uint32_t obj)
{
    mem.at<int32_t>(G::hit_type) = of<int16_t>(obj, 0xCC);
    body_init(obj, mem.at<int32_t>(G::hit_type), 0xB2);
    of<uint8_t>(obj, 0x3C6) |= 1;
    creature_type_setup(obj);
    of<uint16_t>(obj, 0x6C) ^= 1;
    if (mem.at<uint8_t>(G::chapter) != 3)
        prop_face_camera(obj);
    of<uint32_t>(obj, 0) = F::prop_gold_frog_run;
}

// 0x3DA10 prop_gold_frog_run: ends with its parts after +0x70 scene changes; shot: no longer shootable, sound
// 0xA01351, 1000 points to the shooter, motion 0xB3, a splash, a sparkle, 2.21 above the floor and leaps
// (prop_gold_frog_leap, vy 1.2); drawn (object_draw) and in the depth list
template <class P> void Game<P>::prop_gold_frog_run(uint32_t obj)
{
    const int32_t scene = mem.at<int8_t>(G::scene_number);
    if (scene != of<int32_t>(obj, 0x2EC)) {
        of<int32_t>(obj, 0x2EC) = scene;
        const int32_t n = of<int32_t>(obj, 0x330) + 1;
        of<int32_t>(obj, 0x330) = n;
        if (n > of<int16_t>(obj, 0x70)) {
            body_free_parts(obj);
            task_exit();
            return;
        }
    }
    if (of<uint8_t>(obj, 0x58) & 6) {
        of<uint32_t>(obj, 0x58) &= ~1u;
        sound_cue(0xA01351);
        add_score(prop_shooter(obj), 1000);
        set_motion(obj, 0xB3);
        prop_splash_spawn(obj);
        item_sparkle_spawn(of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28));
        of<uint32_t>(obj, 0x50) = 0;
        of<uint32_t>(obj, 0x7C) = 0x3F99999A;
        of<float>(obj, 0x24) = mem.f32(G::floor_y) + ffrom(0x400D70A4);
        of<uint32_t>(obj, 0) = F::prop_gold_frog_leap;
    }
    of<uint32_t>(obj, 0xF8) += 1;
    object_draw(obj);
    depth_list_add(obj);
}

// 0x3DBE0 prop_gold_frog_leap: 91 frames (then gone with its parts); above 2.2 over the floor it rises / falls
// (gravity 0.08) and spins (+0x300 x a frame); the motion advances for the first 10 frames of +0xF4; drawn, blinking
// (not drawn on even frames) after frame 39 (proofcheck: was 8, l6_p128a)
template <class P> void Game<P>::prop_gold_frog_leap(uint32_t obj)
{
    const int32_t n = of<int32_t>(obj, 0x50) + 1;
    of<int32_t>(obj, 0x50) = n;
    if (n > 90) {
        body_free_parts(obj);
        task_exit();
        return;
    }
    float y = of<float>(obj, 0x24);
    if (y > mem.f32(G::floor_y) + ffrom(0x400CCCCD)) {
        const float vy = of<float>(obj, 0x7C);
        of<uint32_t>(obj, 0x2C) += 0x300;
        y = y + vy;
        of<float>(obj, 0x7C) = vy - ffrom(0x3DA3D70A);
    }
    of<float>(obj, 0x24) = y;
    if (!(of<int16_t>(obj, 0xF4) > 9))
        of<uint32_t>(obj, 0xF8) += 1;
    const int32_t m = of<int32_t>(obj, 0x50);
    if (!(m > 0x27) || m % 2 != 0)
        object_draw(obj);
}

// 0x3FC70 item_sparkle_spawn (x, y, z): item_sparkle there (a 0x58-byte task, model 0x2E0)
template <class P> void Game<P>::item_sparkle_spawn(uint32_t x, uint32_t y, uint32_t z)
{
    const uint32_t t = task_create(F::item_sparkle, 0x58);
    of<uint32_t>(t, 0x20) = x;
    of<uint32_t>(t, 0x24) = y;
    of<uint32_t>(t, 0x28) = z;
    of<uint32_t>(t, 0x54) = 0x2E0;
}

// 0x42C20 item_sparkle: the glow colour (colour_blend32), the next model until 0x312, drawn at scale 0.8
template <class P> void Game<P>::item_sparkle(uint32_t obj)
{
    const uint32_t c = colour_blend32(0xABDE, 0x9ADF, prop_glow_phase());
    const uint32_t m = of<uint32_t>(obj, 0x54) + 1;
    mem.at<uint16_t>(pr::sparkle_colour) = uint16_t(c);
    of<uint32_t>(obj, 0x54) = m;
    if (m == 0x312) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    prop_scale(0x3F4CCCCD);
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

// 0x3E590 prop_splash_spawn: prop_splash at the object (model 0x589; scale 0.7 for sub-type 63, else 0.3)
template <class P> void Game<P>::prop_splash_spawn(uint32_t obj)
{
    const uint32_t t = task_create(F::prop_splash, 0x3C8);
    of<uint32_t>(t, 0x20) = of<uint32_t>(obj, 0x20);
    of<uint32_t>(t, 0x24) = of<uint32_t>(obj, 0x24);
    of<uint32_t>(t, 0x28) = of<uint32_t>(obj, 0x28);
    of<uint32_t>(t, 0x94) = 0x3F333333;
    if (of<uint16_t>(obj, 0xCC) != 0x3F)
        of<uint32_t>(t, 0x94) = 0x3E99999A;
    of<uint32_t>(t, 0x54) = 0x589;
}

// 0x42AB0 prop_splash: the next model until 0x5A7, facing the camera, scaled
template <class P> void Game<P>::prop_splash(uint32_t obj)
{
    const uint32_t m = of<uint32_t>(obj, 0x54) + 1;
    of<uint32_t>(obj, 0x54) = m;
    if (m == 0x5A7) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    prop_billboard();
    prop_scale(of<uint32_t>(obj, 0x94));
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

// 0x3E7B0 prop069_powa (sub-type 69): gone after camera frame +0x30C; models 0xA06..0xA22 in a loop; prop_draw
template <class P> void Game<P>::prop069_powa(uint32_t obj)
{
    if (mem.f32(pr::camera_frame) > of<float>(obj, 0x30C)) {
        task_exit();
        return;
    }
    const int32_t m = of<int32_t>(obj, 0x54);
    of<int32_t>(obj, 0x54) = m > 0xA21 ? 0xA06 : m + 1;
    prop_draw(obj);
}

// 0x41900 prop_break_pieces (sub-types 3 chair, 68, 81, 82, 91, 116 / 117; PC: also 121): six fragments (prop_break_piece) with the
// sub-type's models, lift and speeds (a discarded rand first), glow, a random spin, thrown out around the prop's y
// angle by (rand % 61 - 30 + 120 * i) / 4.5; floor 0.5, bounce 0.6
template <class P> void Game<P>::prop_break_pieces(uint32_t obj)
{
    for (uint32_t i = 0; (i & 0xFF) <= 5; ++i) {
        const uint32_t t = task_create(F::prop_break_piece, 0x3C8);
        task_clear_work(t);
        prop_piece_random(t, obj);
        const uint32_t n = i & 0xFF;
        auto lift = [&](float dy, int32_t vyMod, uint32_t vyBase, int32_t gMod, uint32_t gBase) {
            of<float>(t, 0x24) = of<float>(t, 0x24) + dy;
            rand();
            of<float>(t, 0x7C) = pdiv(float(int32_t(rand()) % vyMod), 100.0f) + ffrom(vyBase);
            const float g = pdiv(float(int32_t(rand()) % gMod), 1000.0f);
            of<float>(t, 0x88) = ffrom(gBase) - g;
        };
        const int32_t type = of<int16_t>(obj, 0xCC);
        if (type == 3) {
            of<uint32_t>(t, 0x54) = n == 3 ? 0xBB2 : (n == 2 || n == 4) ? 0xBB1 : 0xBB0;
            lift(3.0f, 40, 0x3ECCCCCD, 20, 0xBCA3D70A);
        } else if (type == 0x5B) {
            of<uint32_t>(t, 0x54) = n == 3 ? 0x159C : (n == 2 || n == 4) ? 0x159B : 0x159A;
            lift(6.0f, 50, 0x3F000000, 20, 0xBCA3D70A);
        } else if (type == 0x51) {
            of<uint32_t>(t, 0x54) = n == 3 ? 0x11A7 : (n == 2 || n == 4) ? 0x11A6 : 0x11A5;
            lift(3.0f, 40, 0x3ECCCCCD, 20, 0xBCA3D70A);
        } else if (type == 0x44) {
            of<uint32_t>(t, 0x54) = n == 1 ? 0xBA6 : n == 5 ? 0xBA7 : 0xBA8 + (i & 1);
            lift(1.5f, 20, 0x3E4CCCCD, 30, 0xBCF5C28F);
        } else if (type == 0x52) {
            of<uint32_t>(t, 0x54) = 0x113E + (i & 1);
            lift(2.0f, 30, 0x3E99999A, 20, 0xBCA3D70A);
            of<float>(t, 0x94) = of<float>(obj, 0x94) * ffrom(0x3EE66666);
        } else if (type == 0x74 || type == 0x75 || (type == 0x79 && pc_path())) {
            // (0x79, Chapter 3's machine item prop 121: THOTD.EXE 0x482C49 takes it here too, the arcade has no case
            // for it: PC Mode / Boss Mode give its pieces 0x74's model and lift, three more random numbers each; pmch34)
            of<uint32_t>(t, 0x54) = 0x15C0;
            if (of<uint16_t>(obj, 0xCC) == 0x75)
                of<uint32_t>(t, 0x54) = 0x1A03;
            lift(3.0f, 40, 0x3F19999A, 20, 0xBCF5C28F);
            of<float>(t, 0x94) = float(double(of<float>(obj, 0x94)) * 0.5);
        }
        of<uint32_t>(t, 0x58) |= 0x4000000;
        prop_piece_spin(t, 0x6FF);
        const int32_t r = int32_t(rand()) % 61;
        const int32_t deg = r - 30 + int32_t(n * 15) * 8;
        const uint32_t d = uint32_t(int32_t(float(deg) * ffrom(0x43360B61)));
        const uint32_t a = of<uint32_t>(obj, 0x30) - d;
        const Tgp::Out sn = cam_copro(0x1B, {a});
        of<float>(t, 0x78) = ffrom(sn.w[0]) / 4.5f + of<float>(t, 0x78);
        const Tgp::Out cs = cam_copro(0x1C, {a});
        of<uint32_t>(t, 0x354) = 0x3F000000;
        of<uint32_t>(t, 0x358) = 0x3F19999A;
        of<float>(t, 0x80) = ffrom(cs.w[0]) / 4.5f + of<float>(t, 0x80);
    }
}

// 0x40290 prop_plate_shards (sub-types 53 plates, 73 / 76, 85 fruit): four shards (prop_piece_fall) at the prop with
// random speeds (rand % 41 - 20) / 100, vy 0.6 + (rand % 10) / 100 and spins ((rand % 17) - 8) << 8; the prop's scale
template <class P> void Game<P>::prop_plate_shards(uint32_t obj)
{
    for (uint32_t i = 0; (i & 0xFF) <= 3; ++i) {
        const uint32_t t = task_create(F::prop_piece_fall, 0x3C8);
        task_clear_work(t);
        obj_copy_pose(t, obj);
        switch (of<int16_t>(obj, 0xCC)) {
        case 0x49: case 0x4C: of<uint32_t>(t, 0x54) = 0x1242 + (i & 0xFF); break;
        case 0x55: of<uint32_t>(t, 0x54) = 0x1142; break;
        case 53: of<uint32_t>(t, 0x54) = 0xCFF + (i & 0xFF); break;
        default: break;
        }
        of<float>(t, 0x78) = pdiv(float(int32_t(rand()) % 41 - 20), 100.0f);
        of<float>(t, 0x7C) = pdiv(float(int32_t(rand()) % 10), 100.0f) + ffrom(0x3F19999A);
        of<float>(t, 0x80) = pdiv(float(int32_t(rand()) % 41 - 20), 100.0f);
        of<int32_t>(t, 0x31C) = (int32_t(rand()) % 17 - 8) << 8;
        of<int32_t>(t, 0x320) = (int32_t(rand()) % 17 - 8) << 8;
        of<int32_t>(t, 0x324) = (int32_t(rand()) % 17 - 8) << 8;
        if (of<uint16_t>(obj, 0xCC) == 0x49)
            of<uint32_t>(t, 0x94) = 0x40000000;
        of<uint32_t>(t, 0x50) = 0;
        of<uint16_t>(t, 0x6A) = 0;
        of<uint32_t>(t, 0x94) = of<uint32_t>(obj, 0x94);
    }
}

// 0x3B910 prop041_tabul_scheduled (sub-type 41 and kind 147's props): not shootable between camera frames 158 and 190
// (sub-type 4 never again); the scene count; the visibility schedule +0x64 (signed, compared as a float); shot (the
// step can be damaged): its sound, the impact and a break by sub-type (16 skull burst, 3 break pieces, 53 plate
// shards, else the break animation), then gone; drawn (light 0, T R S), shootable unless flags bit 30
template <class P> void Game<P>::prop041_tabul_scheduled(uint32_t obj)
{
    const float frame = mem.f32(pr::camera_frame);
    if (frame > 158.0f && !(frame >= 190.0f))
        of<uint32_t>(obj, 0x58) |= 0x40000000;
    else if (of<uint16_t>(obj, 0xCC) != 4)
        of<uint32_t>(obj, 0x58) &= ~0x40000000u;
    const int32_t scene = mem.at<int8_t>(G::scene_number);
    if (scene != of<int32_t>(obj, 0x2EC)) {
        of<int32_t>(obj, 0x2EC) = scene;
        const int32_t n = of<int32_t>(obj, 0x330) + 1;
        of<int32_t>(obj, 0x330) = n;
        if (n > of<int16_t>(obj, 0x70)) {
            task_exit();
            return;
        }
    }
    const int32_t s = of<int16_t>(obj, 0x64);
    uint32_t pos = of<uint32_t>(obj, 0x2F4);
    const float at = float(int32_t(mem.at<int16_t>(pr::schedules + uint32_t(s << 4) + 2 * pos)));
    int32_t vis = of<int32_t>(obj, 0x2F0);
    if (at == mem.f32(pr::camera_frame)) {
        vis = 1 - vis;
        pos += 1;
    }
    of<uint32_t>(obj, 0x2F4) = pos;
    of<int32_t>(obj, 0x2F0) = vis;
    if (vis == 0 &&
        mem.at<uint16_t>(pr::schedules + ((uint32_t(s) << 16) >> 12) + 2 * of<uint32_t>(obj, 0x2F4)) == 0xFFFF) {
        task_exit();
        return;
    }
    if (of<uint32_t>(obj, 0x2F0) == 0 || of<uint8_t>(obj, 0x2E6) != 0)
        return;
    const uint32_t f = of<uint32_t>(obj, 0x58);
    if (!((f >> 30) & 1) && (f & 6)) {
        const uint32_t r = pr::rec(of<int16_t>(obj, 0xCC));
        if (mem.u32(r + 0x24 + 4 * of<uint32_t>(obj, 0x32C)) != 0xFFFFFFFFu) {
            sound_cue(mem.u32(r + 0x64));
            const float y = of<float>(obj, 0x24) + prop_mark_height(obj);
            prop_view_pos(obj, of<uint32_t>(obj, 0x20), fbits(y), of<uint32_t>(obj, 0x28));
            prop_bullet_impact(obj);
            switch (of<int16_t>(obj, 0xCC)) {
            case 16: prop_skull_burst(obj); break;
            case 3: prop_break_pieces(obj); break;
            case 53: prop_plate_shards(obj); break;
            default: prop_break_anim_spawn(obj); break;
            }
            task_exit();
            return;
        }
    }
    prop_light0();
    cam_copro(0x05, {});
    prop_place_xyz(obj);
    prop_scale(of<uint32_t>(obj, 0x94));
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
    if (!((of<uint32_t>(obj, 0x58) >> 30) & 1))
        prop_shootable(obj);
}

// 0x3C0F0 prop005_tokei_clock (sub-type 5, kind 137's three stacked clock objects): the schedule; the spare levels end
// when the clock is used up (level 2 at count 2, level 1 at count 3); shot (not flags bit 30): the impact, hit flag
// 0x53E3B2; the visible level (0) takes the hit: its sound, the next damage model and the pieces, or the pieces and the
// end after the last; drawn (light 0, T R S) with the pendulum swinging (+0x320 / +0x324, model 0xD16) until count 2
// and the hands (0xD18 by +0x2F8, 0xD17 by +0x328) until count 1; always shootable
template <class P> void Game<P>::prop005_tokei_clock(uint32_t obj)
{
    constexpr uint32_t hit = 0x53E3B2, count = 0x53E3B3;
    prop_schedule_update(obj);
    const uint8_t lvl = of<uint8_t>(obj, 0x2E6);
    if ((lvl == 2 && mem.at<uint8_t>(count) == 2) || (lvl == 1 && mem.at<uint8_t>(count) == 3)) {
        task_exit();
        return;
    }
    const uint8_t fb = of<uint8_t>(obj, 0x58);
    if ((fb & 1) && (fb & 6) && !((of<uint32_t>(obj, 0x58) >> 30) & 1)) {
        prop_bullet_impact(obj);
        mem.at<uint8_t>(hit) = 1;
    }
    if (of<uint8_t>(obj, 0x2E6) == 0 && mem.at<uint8_t>(hit) != 0) {
        const uint32_t r = pr::rec(of<int16_t>(obj, 0xCC));
        sound_cue(mem.u32(r + 0x64));
        const uint8_t c = uint8_t(mem.at<uint8_t>(count) + 1);
        const uint32_t m = mem.u32(pr::rec(of<int16_t>(obj, 0xCC)) + 0x20 + 4 * uint32_t(c));
        mem.at<uint8_t>(count) = c;
        if (m != 0) {
            of<uint32_t>(obj, 0x54) =
                mem.u32(pr::rec(of<int16_t>(obj, 0xCC)) + 0x20 + 4 * uint32_t(int32_t(mem.at<int8_t>(count))));
            prop005_clock_pieces(obj);
            mem.at<uint8_t>(hit) = 0;
        } else {
            prop005_clock_pieces(obj);
            task_exit();
            return;
        }
    }
    if (of<uint32_t>(obj, 0x2F0) != 0 && of<uint8_t>(obj, 0x2E6) == 0) {
        prop_light0();
        cam_copro(0x05, {});
        prop_place_xyz(obj);
        prop_scale(of<uint32_t>(obj, 0x94));
        geo_draw_model(of<uint32_t>(obj, 0x54));
        if (!(mem.at<int8_t>(count) > 1)) {
            const int32_t sw = of<int32_t>(obj, 0x320);
            int32_t step = of<int32_t>(obj, 0x324);
            if ((sw < 0 ? -sw : sw) > 96)
                step = -step;
            of<int32_t>(obj, 0x324) = step;
            of<int32_t>(obj, 0x31C) = of<int32_t>(obj, 0x31C) + sw;
            of<int32_t>(obj, 0x320) = sw + step;
            cam_copro(0x12, {0, 0x41780000, 0x40000000});
            cam_copro(0x16, {of<uint32_t>(obj, 0x31C)});
            geo_draw_model(0xD16);
            cam_copro(0x16, {0u - of<uint32_t>(obj, 0x31C)});
        }
        if (!(mem.at<int8_t>(count) > 0)) {
            cam_copro(0x12, {0xBDCCCCCD, 0x4026147B, 0x3F333333});
            cam_copro(0x16, {of<uint32_t>(obj, 0x2F8)});
            geo_draw_model(0xD18);
            cam_copro(0x16, {0u - of<uint32_t>(obj, 0x2F8)});
            cam_copro(0x16, {of<uint32_t>(obj, 0x328)});
            geo_draw_model(0xD17);
        }
        cam_copro(0x06, {});
    }
    prop_shootable(obj);
}

// 0x40FB0 prop005_clock_pieces: five pieces (prop_break_piece) by the clock's damage model (0xD13: hands 0xD17 /
// 0xD18 and glass 0xBAA; 0xD14: pendulum 0xD16 and 0xBAB; else the glass 0xD15; the rest wood 0xBAC / 0xBAD at 4 x
// scale), glow, at the clock's height for the count, random spins, thrown toward the camera side by (30 * i - rand % 9
// - 4) (event 19: a quarter turn more) / 6.5, vy 0.1 + (rand % 40) / 100, gravity -0.03 - (rand % 10) / 1000
template <class P> void Game<P>::prop005_clock_pieces(uint32_t obj)
{
    for (uint32_t i = 0; (i & 0xFF) <= 4; ++i) {
        const uint32_t t = task_create(F::prop_break_piece, 0x3C8);
        task_clear_work(t);
        prop_piece_random(t, obj);
        const uint32_t n = i & 0xFF;
        const uint32_t wood = 0xBAC + (i & 1);
        uint32_t m;
        switch (of<uint32_t>(obj, 0x54)) {
        case 0xD13: m = n == 3 ? 0xD18 : n == 4 ? 0xBAA : n == 2 ? 0xD17 : wood; break;
        case 0xD14: m = n == 0 ? 0xD16 : (n == 1 || n == 4) ? 0xBAB : wood; break;
        case 0xD15: m = wood; break;
        default: m = n == 1 ? 0xD15 : wood; break;
        }
        of<uint32_t>(t, 0x54) = m;
        of<uint32_t>(t, 0x58) |= 0x4000000;
        const float k = float(int32_t(mem.at<int8_t>(0x53E3B3))) * 1.5f;
        of<float>(t, 0x24) = (of<float>(obj, 0x24) + 8.0f) + (4.5f - k);
        const uint8_t c = mem.at<uint8_t>(0x53E3B3);
        if (c == 1 || c == 2)
            of<float>(t, 0x24) = of<float>(t, 0x24) + 8.0f;
        prop_piece_spin(t, 0x8FF);
        const int32_t r = int32_t(rand()) % 9;
        const int32_t deg = int32_t(n * 15) * 2 - r - 4;
        const uint32_t d = uint32_t(int32_t(float(deg) * ffrom(0x43360B61)));
        uint32_t a = d + of<uint32_t>(t, 0x30);
        if (mem.at<uint8_t>(G::event_index) == 19)
            a += 0x4000;
        const Tgp::Out sn = cam_copro(0x1B, {a});
        of<float>(t, 0x78) = of<float>(t, 0x78) - ffrom(sn.w[0]) / 6.5f;
        of<float>(t, 0x7C) = pdiv(float(int32_t(rand()) % 40), 100.0f) + ffrom(0x3DCCCCCD);
        of<float>(t, 0x88) = ffrom(0xBCF5C28F) - pdiv(float(int32_t(rand()) % 10), 1000.0f);
        const Tgp::Out cs = cam_copro(0x1C, {a});
        of<float>(t, 0x80) = of<float>(t, 0x80) - ffrom(cs.w[0]) / 6.5f;
        if (of<uint32_t>(t, 0x54) == 0xBAC || of<uint32_t>(t, 0x54) == 0xBAD)
            of<float>(t, 0x94) = of<float>(obj, 0x94) * 4.0f;
        of<uint32_t>(t, 0x354) = 0x3E99999A;
        of<uint32_t>(t, 0x358) = 0x3F19999A;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// prop group kinds

// 0x39AC0 kind137_prop_group (kind 137): the clock: three prop005 objects at its pose stacked 9 apart (levels 0-2 in
// +0x2E6; level 0 gets the pendulum swing and random hand angles), on the floor unless flags bit 12; the clock's hit
// flag and count cleared; the group task ends.
// THOTD.EXE's kind 137 (table 0x5D8118 + 137 x 4 = 0x47B498) is only `jmp task_exit` (and its prop_object drops type 5):
// the PC has no clock, so in PC Mode and Boss Mode (the PC's own modes) none is made and its two rand calls are not
// made either (pccap: PC captures of Boss Mode, the Chariot's room has no clock task; the arcade's two draws had
// shifted the whole fight's random numbers against the PC's)
template <class P> void Game<P>::kind137_prop_group(uint32_t obj)
{
    if (pc_path() || boss_on()) {
        task_exit();
        return;
    }
    mem.at<uint8_t>(0x53E3B2) = 0;
    mem.at<uint8_t>(0x53E3B3) = 0;
    for (uint32_t i = 0; (i & 0xFF) <= 2; ++i) {
        const uint32_t t = task_create(F::prop005_tokei_clock, 0x3C8);
        task_clear_work(t);
        of<uint16_t>(t, 0xCC) = of<uint16_t>(obj, 0xCC);
        prop_init(t);
        obj_copy_pose(t, obj);
        of<uint16_t>(t, 0x70) = of<uint16_t>(obj, 0x70);
        of<uint8_t>(t, 0x2E6) = uint8_t(i);
        of<uint16_t>(t, 0x64) = of<uint16_t>(obj, 0x64);
        if ((i & 0xFF) == 0) {
            of<uint32_t>(t, 0x31C) = 0x800;
            of<uint32_t>(t, 0x320) = 0xFFFFFFE0u;
            of<uint32_t>(t, 0x324) = 0xFFFFFFFEu;
            of<uint32_t>(t, 0x2F8) = rand() & 0xFFFF;
            of<uint32_t>(t, 0x328) = rand() & 0xFFFF;
        }
        const float h = float(int32_t(i & 0xFF)) * 9.0f;
        const float base = (of<uint16_t>(obj, 0x58) >> 12) & 1 ? of<float>(obj, 0x24) : mem.f32(G::floor_y);
        of<float>(t, 0x24) = h + base;
    }
    task_exit();
}

// 0x3A980 kind139_prop_group (kind 139): default props of its sub-type with model 0x688 at the x / z points of table
// 0xADF10 (7 x 16 bytes per sub-type, x -1 ends), y 0; sub-type 49's third drops the life item, sub-type 50's first
// and sub-type 48's third the gold coin; the group task ends
template <class P> void Game<P>::kind139_prop_group(uint32_t obj)
{
    uint32_t i = 0;
    do {
        const uint32_t t = task_create(F::prop_default, 0x3C8);
        task_clear_work(t);
        of<uint16_t>(t, 0xCC) = of<uint16_t>(obj, 0xCC);
        prop_init(t);
        of<uint32_t>(t, 0x54) = 0x688;
        of<uint16_t>(t, 0x70) = of<uint16_t>(obj, 0x70);
        of<uint16_t>(t, 0x64) = of<uint16_t>(obj, 0x64);
        const uint32_t n = i & 0xFF;
        const uint32_t table = 0xADF10 + uint32_t(int32_t(of<int16_t>(obj, 0xCC)) * 7) * 16;
        of<uint32_t>(t, 0x24) = 0;
        of<uint32_t>(t, 0x20) = mem.u32(table + 8 * n);
        of<uint32_t>(t, 0x28) = mem.u32(table + 8 * n + 4);
        const uint16_t type = of<uint16_t>(obj, 0xCC);
        if (type == 49 && n == 2)
            of<uint32_t>(t, 0x58) |= 0x20;
        if ((type == 50 && n == 0) || (type == 48 && n == 2))
            of<uint32_t>(t, 0x58) |= 0x40;
        ++i;
    } while (mem.f32(0xADF10 + uint32_t(int32_t(of<int16_t>(obj, 0xCC)) * 7) * 16 + 8 * (i & 0xFF)) != -1.0f);
    task_exit();
}

// 0x3A400 kind147_prop_group (kind 147): fifteen prop041 props from table 0xAED50 (28 bytes: sub-type, offset, angle
// offsets) around its pose (chairs, the table, plates, a skull); sub-type 4 never shootable; a prop with a z angle
// gets it cleared and schedule 28; the group task ends
template <class P> void Game<P>::kind147_prop_group(uint32_t obj)
{
    // issues5 (GitHub #52): PC models in an arcade game too: the PC's world has no table there (its model 0xD0A has
    // no PC copy), so the chairs, plates and skull floated; as PC Mode, none of the group (Chapter 1 event 6)
    if (pc_path() || models_pc()) {   // THOTD.EXE's kind 147 is only `jmp task_exit`: no table props on the PC (pmrest2)
        task_exit();
        return;
    }
    for (uint32_t i = 0; (i & 0xFF) <= 14; ++i) {
        const uint32_t t = task_create(F::prop041_tabul_scheduled, 0x3C8);
        task_clear_work(t);
        const uint32_t e = 0xAED50 + (i & 0xFF) * 28;
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
        if (of<int16_t>(t, 0xCC) == 4)
            of<uint32_t>(t, 0x58) |= 0x40000000;
        if (of<uint32_t>(t, 0x34) != 0) {
            of<uint32_t>(t, 0x34) = 0;
            of<uint16_t>(t, 0x64) = 28;
        }
    }
    task_exit();
}

// 0x3A1B0 kind134_burst_pieces (kind 134): eight falling pieces (kind134_piece) from table 0xAEAB0 (28 bytes: model,
// offset, angles) for sub-type 32 / 33 / 45 at their room points, gravity -0.01 - (rand % 20) / 1000, random x / z
// spins ((rand & 0x9F) - 0x50); in Chapter 1 piece 4 raises dust; sound 0xA01235; the group task ends
template <class P> void Game<P>::kind134_burst_pieces(uint32_t obj)
{
    uint32_t idx = 0;
    for (uint32_t i = 0; (i & 0xFF) <= 7; ++i) {
        const uint32_t t = task_create(F::kind134_piece, 0x3C8);
        task_clear_work(t);
        of<uint16_t>(t, 0xCC) = of<uint16_t>(obj, 0xCC);
        prop_init(t);
        of<uint16_t>(t, 0x70) = of<uint16_t>(obj, 0x70);
        const int32_t type = of<int16_t>(obj, 0xCC);
        auto at = [&](uint32_t k) { return 0xAEAB0 + (k & 0xFF) * 28; };
        if (type == 32) {
            const uint32_t e = at(i);
            of<float>(t, 0x20) = mem.f32(e + 4) + ffrom(0x43F44CCD);
            of<float>(t, 0x24) = mem.f32(e + 8) + ffrom(0x41E9999A);
            of<float>(t, 0x28) = mem.f32(e + 12) + ffrom(0x43248000);
            idx = i;
        } else if (type == 33) {
            idx = i + 8;
            const uint32_t e = at(idx);
            of<float>(t, 0x20) = mem.f32(e + 4) + ffrom(0x43F44CCD);
            of<float>(t, 0x24) = mem.f32(e + 8) + ffrom(0x41E9999A);
            of<float>(t, 0x28) = mem.f32(e + 12) + ffrom(0x43598000);
        } else if (type == 45) {
            idx = i + 16;
            const uint32_t e = at(idx);
            of<float>(t, 0x20) = mem.f32(e + 4) - ffrom(0x3FBF7CEE);
            of<float>(t, 0x24) = mem.f32(e + 8) + 13.0f;
            of<float>(t, 0x28) = mem.f32(e + 12) + ffrom(0x43020312);
        }
        const uint32_t e = at(idx);
        of<uint32_t>(t, 0x54) = mem.u32(e);
        of<uint32_t>(t, 0x2C) = mem.u32(e + 0x10);
        of<uint32_t>(t, 0x30) = mem.u32(e + 0x14);
        of<uint32_t>(t, 0x7C) = 0;
        of<uint32_t>(t, 0x34) = mem.u32(e + 0x18);
        if (pc_path())   // THOTD.EXE 0x47BA96: times 0.001 (0x5A2D3C), not / 1000 (pmrest2)
            of<float>(t, 0x88) = ffrom(0xBC23D70A) - float(int32_t(rand()) % 20) * ffrom(0x3A83126F);
        else
            of<float>(t, 0x88) = ffrom(0xBC23D70A) - pdiv(float(int32_t(rand()) % 20), 1000.0f);
        of<int32_t>(t, 0x31C) = int32_t(rand() & 0x9F) - 0x50;
        const uint32_t r = rand();
        of<uint32_t>(t, 0x94) = 0x3F800000;
        of<int32_t>(t, 0x320) = int32_t(r & 0x9F) - 0x50;
        of<uint16_t>(t, 0x64) = of<uint16_t>(obj, 0x64);
        if (mem.at<uint8_t>(G::chapter) == 0 && (i & 0xFF) == 4)
            kind134_dust_spawn(of<uint32_t>(t, 0x20), fbits(of<float>(t, 0x24) + 1.0f), of<uint32_t>(t, 0x28));
    }
    sound_cue(0xA01235);
    task_exit();
}

// 0x3DFC0 kind134_piece: gone below y -80; falls (gravity +0x88) and spins (x, z); prop_draw
template <class P> void Game<P>::kind134_piece(uint32_t obj)
{
    if (of<float>(obj, 0x24) < -80.0f) {
        task_exit();
        return;
    }
    const float vy = of<float>(obj, 0x7C) + of<float>(obj, 0x88);
    of<uint32_t>(obj, 0x2C) += of<uint32_t>(obj, 0x31C);
    of<uint32_t>(obj, 0x34) += of<uint32_t>(obj, 0x320);
    of<float>(obj, 0x7C) = vy;
    of<float>(obj, 0x24) = of<float>(obj, 0x24) + vy;
    prop_draw(obj);
}

// 0x3FC30 kind134_dust_spawn (x, y, z): kind134_dust there (a 0x58-byte task, model 0xBB4)
template <class P> void Game<P>::kind134_dust_spawn(uint32_t x, uint32_t y, uint32_t z)
{
    const uint32_t t = task_create(F::kind134_dust, 0x58);
    of<uint32_t>(t, 0x20) = x;
    of<uint32_t>(t, 0x24) = y;
    of<uint32_t>(t, 0x28) = z;
    of<uint32_t>(t, 0x54) = 0xBB4;
}

// 0x42BA0 kind134_dust: the next model until 0xBD2, drawn at scale 2
template <class P> void Game<P>::kind134_dust(uint32_t obj)
{
    const uint32_t m = of<uint32_t>(obj, 0x54) + 1;
    of<uint32_t>(obj, 0x54) = m;
    if (m == 0xBD2) {
        task_exit();
        return;
    }
    cam_copro(0x05, {});
    cam_copro(0x12, {of<uint32_t>(obj, 0x20), of<uint32_t>(obj, 0x24), of<uint32_t>(obj, 0x28)});
    prop_scale(0x40000000);
    geo_draw_model(of<uint32_t>(obj, 0x54));
    cam_copro(0x06, {});
}

} // namespace hotd::port
