// The game's ending after Chapter 4: mode 4 sub-modes 7 / 8 (game_ending_start 0x35160 / game_ending 0x352A0, table
// 0xAB1E0) and the ending's own kinds. The ending is chapter slot 6 (event list 6: event 0 player 1 alone, 1 player 2
// alone, 2 both; the world is Chapter 4's, tables 0xC0BB0 / 0xC0BD0 slot 6). First written in the runtime
// (src/game/ending.cpp, worker "ch2proof"), moved here for its capture proof (worker "verify", docs/proof.md "The
// ending"). Read from the arcade disassembly. Arcade forms only. Declarations: decl/ending.h.
#include "port/game.h"

namespace hotd::port {

namespace ending {
constexpr uint32_t kGroup = 0x53E390;      // the ending's task group
constexpr uint32_t kWinTop = 0x53E394;     // the letterbox window's top / bottom lines (the geometrizer's viewport)
constexpr uint32_t kWinBottom = 0x53E398;
constexpr uint32_t kSkyLast = 0x53E384;    // sky_task_ending: the sky index drawn last
constexpr uint32_t kCover = 0x1004000;     // the 2D cover layer
constexpr uint32_t kPlayers = 0x51F284;    // the player records (0xCC bytes: state +0, score +8, rank +0x14)
} // namespace ending

// 0x35160 game_ending_start (sub-mode 7): the grey look off (0x51F9AC), the heap, the sprites (sprite_init, the slots
// marked) and banks 0x44 / 0x52 / 0x28 / 0x2E, chapter slot 6 at event 0 / 1 (who plays alone) / 2 (both), the window
// open (8 .. 0x1F8), camera_status bit 1 off, chapter_ended 0, the black cover (sprite 0xA1), the characters' models,
// players still playing (not state 9) to state 7, the group 0xAC280 (evt_chapter_start, camera, sky_task_ending, the
// world, the players' tasks), the next sub-mode, then the first frame of game_ending
template <class P> void Game<P>::game_ending_start()
{
    using namespace ending;
    if (mem.template at<int16_t>(0x51F9AC) != 0)
        palette_restore_vblank();
    heap_reset();
    sprite_init();
    sprite_slots_mark();
    sprite_bank_load(0x44, 0x1D5);
    sprite_bank_load(0x52, 0x26E);
    sprite_bank_load(0x28, 0x16F);
    sprite_bank_load(0x2E, 0x1E5);
    const int16_t pig = mem.template at<int16_t>(G::players_in_game);
    mem.template at<int8_t>(G::chapter) = 6;
    const uint8_t ev = pig == 2 ? 2 : mem.template at<uint8_t>(G::solo_player_is_p2) != 0 ? 1 : 0;
    mem.set32(kWinTop, 8);
    mem.set32(kWinBottom, 0x1F8);
    mem.template at<uint8_t>(G::event_index) = ev;
    mem.template at<int16_t>(G::chapter_ended) = 0;
    mem.set32(G::camera_status, mem.u32(G::camera_status) & 0xFFFFFFFDu);
    sprite_draw_anim(kCover, 1, 0, 0, 0, 0xA1);
    player_char_models_set();
    if (mem.u32(kPlayers) != 9)
        mem.set32(kPlayers, 7);
    if (mem.u32(kPlayers + 0xCC) != 9)
        mem.set32(kPlayers + 0xCC, 7);
    const uint32_t group = task_group_create(0xAC280);
    mem.set32(kGroup, group);
    mem.template at<int16_t>(G::game_submode) = int16_t(mem.template at<int16_t>(G::game_submode) + 1);
    game_ending();
}

// 0x352A0 game_ending (sub-mode 8): the coprocessor's matrix stack reset (0x880080 = 0x808), geo_frame_begin, the
// window (8 .. 0x1F8 lines from kWinTop / kWinBottom: the geometrizer's viewport words 0x800030 / 0x800090 and the
// window at [0x790], hardware; the runtime's renderer does them), the focal length 496 both ways (FIFO command 0x7D),
// the group; when the event list has ended (chapter_ended) the ranking (game_ending_finish)
template <class P> void Game<P>::game_ending()
{
    Tgp::Out none;
    copro(0x08, nullptr, 0, none);
    geo_frame_begin();
    const uint32_t focal[2] = {0x43F80000u, 0x43F80000u};
    copro(0x7D, focal, 2, none);
    task_group_run(mem.u32(ending::kGroup));
    if (mem.template at<int16_t>(G::chapter_ended) == 0)
        return;
    game_ending_finish();
}

// game_ending's tail (0x35360): 0x51F42C = 0, rank_insert; a player in the top 10 (0x520AF9 / 0x520B05, the entry
// rows' places, not negative): the name entry (sub-mode 9); else the operator's books (play end per player still
// counted at 0x51F9B0 / 0x51F9B1, game end), no players, next_mode. The runtime overrides it with its own ranking flow
// (src/game/name_entry.cpp game_ending_rank: PC Mode, 4P)
template <class P> void Game<P>::game_ending_finish()
{
    if constexpr (!P::pc) {
        mem.set32(0x51F42C, 0);
        rank_insert();
        if (mem.template at<int8_t>(0x520AF9) >= 0 || mem.template at<int8_t>(0x520B05) >= 0) {
            mem.template at<int16_t>(G::game_submode) = 9;
            return;
        }
        if (mem.template at<uint8_t>(0x51F9B0) != 0)
            bookkeeping_play_end(0);
        if (mem.template at<uint8_t>(0x51F9B1) != 0)
            bookkeeping_play_end(1);
        bookkeeping_game_end();
        mem.template at<int16_t>(G::players_in_game) = 0;
        mem.template at<int16_t>(0x51F4A4) = 0;
        next_mode();
    }
}

// 0x34360 sky_task_ending_start: the sky task (0x58 bytes), the last index drawn cleared
template <class P> void Game<P>::sky_task_ending_start()
{
    task_create(0x34150, 0x58);
    mem.template at<uint8_t>(ending::kSkyLast) = 0;
}

// 0x34150 sky_task_ending: as sky_task (0x33F50) but not turning: the dome of sky_index (table 0xAC1F0: model,
// height, -, start angle; a new index restarts the angle +0x30) with the camera's rotation (0x88), lifted, turned by
// the task's angle; then the cloud layer 0x608, 85 lower except on camera path 0x8D and on 0x8C from path frame 527;
// the window lines (hardware)
template <class P> void Game<P>::sky_task_ending(uint32_t task)
{
    const uint32_t si = mem.template at<uint8_t>(G::sky_index);
    const uint32_t row = 0xAC1F0 + si * 16;
    if (si != mem.template at<uint8_t>(ending::kSkyLast))
        mem.set32(task + 0x30, mem.u32(row + 12));
    if (mem.template at<uint8_t>(G::sky_mode) != 0) {
        cam_copro(0x05, {});
        cam_copro(0x88, {0, 0, 0});
        cam_copro(0x12, {0, mem.u32(row + 4), 0});
        cam_copro(0x15, {mem.u32(task + 0x30)});
        geo_draw_model(mem.u32(row));
        cam_copro(0x06, {});
        cam_copro(0x05, {});
        const uint32_t path = mem.u32(G::camera_path);
        if (path == 0x8D || (path == 0x8C && !(mem.f32(0x51EB9C) < 527.0f)))
            cam_copro(0x88, {0, 0, 0});
        else
            cam_copro(0x88, {0, 0xC2AA0000u, 0});   // -85
        cam_copro(0x12, {0, mem.u32(row + 4), 0});
        geo_draw_model(0x608);
        cam_copro(0x06, {});
    }
    mem.template at<uint8_t>(ending::kSkyLast) = mem.template at<uint8_t>(G::sky_index);
}

// 0x353E0 kind120_letterbox_open: not in region 3 (there the task ends): +0x50 = 0, kind120_letterbox_open_frame next
template <class P> void Game<P>::kind120_letterbox_open(uint32_t obj)
{
    if (mem.template at<uint8_t>(G::region) == 3) {
        task_exit();
        return;
    }
    of<uint32_t>(obj, 0x50) = 0;
    of<uint32_t>(obj, 0) = 0x361C0;
}

// 0x361C0 kind120_letterbox_open_frame: the window's bottom line 0x1F8 - 8 t, the cover clipped to it with the
// caption sheet (table 0xAC2A0 by +0x70); 32 frames (t 0..31)
template <class P> void Game<P>::kind120_letterbox_open_frame(uint32_t obj)
{
    const int32_t t = of<int32_t>(obj, 0x50);
    const int32_t bottom = 0x1F8 - t * 8;
    mem.set32(ending::kWinBottom, uint32_t(bottom));
    const uint32_t sheet = mem.u32(0xAC2A0 + uint32_t(int32_t(of<int16_t>(obj, 0x70))) * 4);
    const int32_t v = bottom / 8;
    sprite_draw_clipped(ending::kCover, 1, 0, v, 0, v, v + t, 0, 0x2F, sheet);
    of<int32_t>(obj, 0x50) = t + 1;
    if (t > 30)
        task_exit();
}

// 0x35F10 kind121_ending_scene: the scene number (0x520704) at +0x50, kind121_ending_scene_draw from the next frame
template <class P> void Game<P>::kind121_ending_scene(uint32_t obj)
{
    const int32_t scene = mem.template at<int8_t>(0x520704);
    of<uint32_t>(obj, 0) = 0x35410;
    of<int32_t>(obj, 0x50) = scene;
}

// 0x35410 kind121_ending_scene_draw: the staff roll scenes' static props by the scene (+0x70 - 2): the models at fixed
// places (coprocessor 0x12 place, 0x15 y turn, 0x13 scale); scene 16's double door swings open on camera path 0x8D
// frames 285..300 (1024 a frame) and is gone after; the task ends when the scene number changes (+0x50)
template <class P> void Game<P>::kind121_ending_scene_draw(uint32_t obj)
{
    auto push = [&] { cam_copro(0x05, {}); };
    auto pop = [&] { cam_copro(0x06, {}); };
    auto at = [&](uint32_t x, uint32_t y, uint32_t z) { cam_copro(0x12, {x, y, z}); };
    auto turn = [&](uint32_t a) { cam_copro(0x15, {a}); };
    auto ten = [&] { cam_copro(0x13, {0x41200000u, 0x41200000u, 0x41200000u}); };
    auto model = [&](uint32_t m) { geo_draw_model(m); };
    const uint32_t path = mem.u32(G::camera_path);
    const float cf = mem.f32(0x51EB9C);
    switch (int32_t(int16_t(uint16_t(of<uint16_t>(obj, 0x70) - 2)))) {
    case 0:
        push(); at(0x440F345Au, 0xC25AD810u, 0x44EA9E0Cu); model(0x18EA); pop();
        push(); at(0x44030E04u, 0xC25AD810u, 0x44EA9E0Cu); model(0x18EB); pop();
        break;
    case 3:
        push(); at(0x444EB47Du, 0xC1A6535Bu, 0x43E22642u); model(0x1479); pop();
        break;
    case 4:
        push(); at(0x44158000u, 0xC1B00000u, 0x43AF0000u); model(0x12BF); pop();
        push(); at(0x441099AAu, 0xC1A6851Fu, 0x439738B4u); model(0x526); pop();
        break;
    case 5:
        push(); at(0x41E33333u, 0xC0D9999Au, 0x3ECCCCCDu); turn(0x4000); model(0x514); pop();
        push(); at(0xC23A6873u, 0xC0D9999Au, 0xC1203127u); turn(0x8000); model(0x51A); pop();
        push(); at(0xC23A6873u, 0xC0D9999Au, 0x41203127u); turn(0x8000); model(0x51E); pop();
        break;
    case 7:
        push(); at(0xC26B50E5u, 0xBF86A7F0u, 0x437424DDu); model(0x166); pop();
        break;
    case 8:
        push(); at(0x41700000u, 0, 0xC28C0000u); ten(); model(0x73A); pop();
        push(); at(0xC1700000u, 0, 0xC28C0000u); ten(); model(0x73B); pop();
        break;
    case 13:
        push(); at(0xC1800000u, 0xC1C80000u, 0xC4A8E000u); turn(0x8000); model(0x951);
        push(); at(0xC11228F6u, 0, 0xC11C3958u); turn(0x3000); model(0x950); pop();
        pop();
        break;
    case 14:
        if (path == 0x8D && cf >= 285.0f && cf < 300.0f) {
            const int32_t a = int32_t(-(cf - 285.0f) * 1024.0f), b = int32_t((cf - 285.0f) * 1024.0f);
            push(); at(0x41700000u, 0, 0xC29C0000u); turn(uint32_t(a)); ten(); model(0x73A); pop();
            push(); at(0xC1700000u, 0, 0xC29C0000u); turn(uint32_t(b)); ten(); model(0x73B); pop();
        } else if (!(path == 0x8D && cf >= 285.0f)) {
            push(); at(0x41700000u, 0, 0xC29C0000u); ten(); model(0x73A); pop();
            push(); at(0xC1700000u, 0, 0xC29C0000u); ten(); model(0x73B); pop();
        }
        break;
    case 15:
        push(); at(0x411B3333u, 0, 0x428F0000u); turn(0x8000); ten(); model(0x73E); pop();
        push(); at(0xC11B3333u, 0, 0x428F0000u); turn(0x8000); ten(); model(0x742); pop();
        break;
    case 0x40:
        push(); at(0x43DA493Bu, 0xC2F4B2B0u, 0x428A94CAu); model(0x535); pop();
        push(); at(0x43DA49BEu, 0xC2F4B2B0u, 0x425EC241u); model(0x536); pop();
        break;
    default:
        break;
    }
    if (of<int32_t>(obj, 0x50) != int32_t(mem.template at<int8_t>(0x520704)))
        task_exit();
}

// 0x35F30 kind105_ending_caption: the window reset; event 6: sound 0xA01207; the caption sheet (0xAC2A0 by +0x70) on
// the cover at the top, the END sprites 0x1E4 / 0x270 (banks 10 / 0x4D), the grey look; the task ends
template <class P> void Game<P>::kind105_ending_caption(uint32_t obj)
{
    using namespace ending;
    const uint8_t ev = mem.template at<uint8_t>(G::event_index);
    mem.set32(kWinTop, 8);
    mem.set32(kWinBottom, 0x1F8);
    if (ev == 6)
        sound_cue(0xA01207);
    sprite_draw(kCover, 0x20, 0, int(mem.u32(0xAC2A0 + uint32_t(int32_t(of<int16_t>(obj, 0x70))) * 4)));
    sprite_bank_load(10, 0x1E4);
    sprite_bank_load(0x4D, 0x270);
    sprite_draw_anim(kCover, 1, 0, 0x28, 0x22, 0x1E4);
    sprite_draw_anim(kCover, 1, 0, 0x26, 0x28, 0x270);
    palette_grey_build();
    task_exit();
}

// 0x35FF0 kind116_end_sprite: the full-screen sprite 0x106 (bank 0x10) on the cover; the task ends
template <class P> void Game<P>::kind116_end_sprite(uint32_t obj)
{
    (void)obj;
    sprite_bank_load(0x10, 0x106);
    sprite_draw_anim(ending::kCover, 1, 0, 0, 0, 0x106);
    task_exit();
}

// 0x360D0 kind122_ending_score: route_choice 1 when a player in the game has the top rank card (score_rank_get 0),
// else 2 when the score (both players' together) ends in 0, else 0; then (not in region 3) the window closes:
// +0x50 = 31, kind122_letterbox_close_frame
template <class P> void Game<P>::kind122_ending_score(uint32_t obj)
{
    using namespace ending;
    const int16_t pig = mem.template at<int16_t>(G::players_in_game);
    int32_t score;
    if (pig == 2)
        score = int32_t(mem.u32(kPlayers + 8)) + int32_t(mem.u32(kPlayers + 0xCC + 8));
    else
        score = int32_t(mem.u32(kPlayers + (mem.template at<uint8_t>(G::solo_player_is_p2) == 0 ? 0u : 0xCCu) + 8));
    const int32_t last = score % 10;
    bool top = false;
    if (pig == 2 && (score_rank_get(0, 2) == 0 || score_rank_get(1, 2) == 0))
        top = true;
    else if (mem.template at<int16_t>(G::players_in_game) == 1 &&
             score_rank_get(mem.template at<int8_t>(G::solo_player_is_p2), 1) == 0)
        top = true;
    mem.template at<uint8_t>(G::route_choice) = top ? 1 : last == 0 ? 2 : 0;
    if (mem.template at<uint8_t>(G::region) == 3) {
        task_exit();
        return;
    }
    of<uint32_t>(obj, 0x50) = 0x1F;
    of<uint32_t>(obj, 0) = 0x36030;
}

// 0x36030 kind122_letterbox_close_frame: the window's bottom line 0x1F8 - 8 t, sprite 0x1E1 on the cover at the top
// and the cover clipped to the window; t from 31 down; ends after t -1
template <class P> void Game<P>::kind122_letterbox_close_frame(uint32_t obj)
{
    using namespace ending;
    const int32_t bottom = 0x1F8 - of<int32_t>(obj, 0x50) * 8;
    mem.set32(kWinBottom, uint32_t(bottom));
    sprite_draw(kCover, 0x20, 0, 0x1E1);
    const int32_t v = int32_t(mem.u32(kWinBottom)) / 8;
    sprite_draw_clipped(kCover, 1, 0, v, 0, v, v + of<int32_t>(obj, 0x50), 0, 0x2F, 0x1E1);
    const int32_t t = of<int32_t>(obj, 0x50);
    of<int32_t>(obj, 0x50) = t - 1;
    if (t < 0)
        task_exit();
}

// 0x18B50 palette_curve_fade_in_start: a child task 0x189C0 (0x34 bytes) with a new fade generation (0x517CA0 + 1 in
// its +0x20), the step 0x517CA4 back to 0, then the first step at once. Read from the disassembly (ch4proof: the
// whole-game differential found the ending's task missing at its first frame, 83136)
template <class P> void Game<P>::palette_curve_fade_in_start()
{
    const uint32_t t = task_create_child(F::palette_curve_fade_in_task, 0x34);
    const uint32_t id = mem.u32(0x517CA0) + 1;
    mem.set32(0x517CA0, id);
    mem.set32(t + 0x20, id);
    mem.set32(0x517CA4, 0);
    palette_curve_fade_step(t);
}

// 0x189C0 palette_curve_fade_in_task: a newer fade (0x517CA0) or a step past 32 ends it; else one step
template <class P> void Game<P>::palette_curve_fade_in_task(uint32_t task)
{
    palette_curve_fade_step(task);
}

// the step (the body of both): step 32: 0x51F704 = 0; else the palette queue 0x517CB0 (0x1800 halfwords) = the low
// byte of each colour curve entry 0x512CA0.. x step >> 5, 0x51F704 = 1; then the step + 1, 0x51E8C4 cleared first and
// set at the end (an upload asked)
template <class P> void Game<P>::palette_curve_fade_step(uint32_t task)
{
    if (mem.u32(task + 0x20) != mem.u32(0x517CA0))
        task_exit();
    if (int32_t(mem.u32(0x517CA4)) > 32)
        task_exit();
    const uint32_t step = mem.u32(0x517CA4);
    mem.at<uint16_t>(0x51E8C4) = 0;
    if (step == 32) {
        mem.at<uint16_t>(0x51F704) = 0;
    } else {
        for (uint32_t i = 0; i < 0x1800; ++i)
            mem.at<uint16_t>(0x517CB0 + i * 2) = uint16_t((uint32_t(mem.at<uint8_t>(0x512CA0 + i * 2)) * step) >> 5);
        mem.at<uint16_t>(0x51F704) = 1;
    }
    mem.set32(0x517CA4, step + 1);
    mem.at<uint16_t>(0x51E8C4) = 1;
}

// 0x1E630 kind110_letterbox: the window reset, +0x70 = 2, the colour curves' fade-in (palette_curve_fade_in_start),
// kind110_wait next
template <class P> void Game<P>::kind110_letterbox(uint32_t obj)
{
    mem.set32(ending::kWinTop, 8);
    mem.set32(ending::kWinBottom, 0x1F8);
    of<int16_t>(obj, 0x70) = 2;
    palette_curve_fade_in_start();
    of<uint32_t>(obj, 0) = 0x1E580;
}

// 0x1E580 kind110_wait: +0x70 down each frame; past 0 the cover layer (0x1004000.., 0x60 bytes a step to 0x10057FC:
// tile RAM, not work RAM) cleared and the task ends
template <class P> void Game<P>::kind110_wait(uint32_t obj)
{
    const uint16_t c = uint16_t(of<uint16_t>(obj, 0x70) - 1);
    of<uint16_t>(obj, 0x70) = c;
    if (c != 0xFFFF)
        return;
    for (uint32_t a = ending::kCover; a <= 0x10057FCu; a += 0x60)
        for (uint32_t i = 0; i < 0x60; i += 4)
            if (mem.mapped(a + i))
                mem.set32(a + i, 0);
    task_exit();
}

// 0x1E7B0 kind111_texture_area: texture area +0x70 (cover sprite 0xA1); the task ends
template <class P> void Game<P>::kind111_texture_area(uint32_t obj)
{
    texture_area_load(uint32_t(int32_t(of<int16_t>(obj, 0x70))), 0xA1);
    task_exit();
}

// 0x9C020 kind179_chapter_results (the ending's results card): the grey look off, banks 0x32 / 0x3D / 0x41 / 0x42,
// the text layers cleared; for each player in state 7 its card (sprites from table 0xCD530), score (kind176_score_draw),
// rank (score_rank_get by the number of such players, kept at the record's +0x14) and rank cards; 240 frames, then
// kind179_chapter_results_frame (its first frame now)
template <class P> void Game<P>::kind179_chapter_results(uint32_t obj)
{
    using namespace ending;
    if (mem.template at<int16_t>(0x51F9AC) != 0)
        palette_restore_vblank();
    sprite_bank_load(0x32, 0x177);
    sprite_bank_load(0x3D, 0x179);
    sprite_bank_load(0x41, 0x1B5);
    sprite_bank_load(0x42, 0x1BE);
    text_layers_clear();
    int n = 0;
    if (mem.u32(kPlayers) == 7)
        ++n;
    if (mem.u32(kPlayers + 0xCC) == 7)
        ++n;
    if (mem.u32(kPlayers) == 7) {
        sprite_draw_anim(kCover, 1, 0, 0, 0, uint32_t(int32_t(mem.template at<int16_t>(0xCD530))));
        kind176_score_draw(0, 8, 0x2A, 0x1F);
        const int32_t r = score_rank_get(0, n);
        mem.set32(kPlayers + 0x14, uint32_t(r));
        sprite_draw_anim(0x1000000, 1, 0, 0, 0, uint32_t(r + 0x179));
        sprite_draw_anim(0x1000000, 1, 0, 6, 0x1E, uint32_t(int32_t(mem.template at<int16_t>(0xCD534)) + 7 - r));
    }
    if (mem.u32(kPlayers + 0xCC) == 7) {
        sprite_draw_anim(kCover, 1, 0, 0x1F, 0, uint32_t(int32_t(mem.template at<int16_t>(0xCD532))));
        kind176_score_draw(1, 8, 0x2A, 0x1F);
        const int32_t r = score_rank_get(1, n);
        mem.set32(kPlayers + 0xCC + 0x14, uint32_t(r));
        sprite_draw_anim(0x1000000, 1, 0, 0x1F, 0, uint32_t(r + 0x179));
        sprite_draw_anim(0x1000000, 1, 0, 0x25, 0x1E, uint32_t(int32_t(mem.template at<int16_t>(0xCD536)) + 7 - r));
    }
    of<uint32_t>(obj, 0x50) = 0xF0;
    of<uint32_t>(obj, 0) = 0x9C200;
    kind179_chapter_results_frame(obj);
}

// 0x9C200 kind179_chapter_results_frame: +0x50 down; below 1 event flag 254 (0x51EA3E)
template <class P> void Game<P>::kind179_chapter_results_frame(uint32_t obj)
{
    const int32_t t = of<int32_t>(obj, 0x50) - 1;
    of<int32_t>(obj, 0x50) = t;
    if (!(0 < t))
        mem.template at<uint8_t>(0x51EA3E) = 1;
}

// 0x9BE20 score_rank_get (player, table): the rank card (0 best .. 7) of the player's score: table 1 (one player)
// 62000 / 55000 / 49000 / 43000 / 37000 / 31000 / 25000, else (two) 62000 / 48000 / 37000 / 31000 / 25000 / 19000 / 13000
template <class P> int32_t Game<P>::score_rank_get(int p, int table)
{
    if constexpr (P::pc) {
        (void)p;
        (void)table;
        return 0;
    } else {
        static const int32_t t1[] = {0xF22F, 0xD6D7, 0xBF67, 0xA7F7, 0x9087, 0x7917, 0x61A7};
        static const int32_t t2[] = {0xF22F, 0xBB7F, 0x9087, 0x7917, 0x61A7, 0x4A37, 0x32C7};
        const int32_t score = int32_t(mem.u32(ending::kPlayers + uint32_t(p) * 0xCC + 8));
        const int32_t* t = table == 1 ? t1 : t2;
        for (int i = 0; i < 7; ++i)
            if (!(score <= t[i]))
                return i;
        return 7;
    }
}

// --- the ending's character bodies (kind 103 set-ups 0x58C40 / 0x59090 / 0x59660 / 0x59A80) ---
// p1 a / b: player 1's character (alone: event 0 / the 2-player event's left one); p2 a / b: player 2's. Each: the body
// (type from 0xCA690 / 0xCA694 by 0x51F42C, motion +0x70), a scene of motions timed by the camera path / frame, the
// hand's role-1 model animated in a camera window (the gun / torch), and (a / p1 b) the room's props drawn at an
// offset that follows the camera path changes (0x5433C4..CC); gone when the event changes (+0x2EC)

// the body type: table 0xCA690 / 0xCA694 by 0x51F42C. PC Mode (THOTD.EXE ending_p1_a .. ending_p2_b
// 0x4AA52C-0x4AB148): the seat's character's type (4P: players 3 / 4 alone, as the bodies)
template <class P> int32_t Game<P>::ending_body_type(uint32_t table)
{
    if (chars_on()) {
        const bool setB = coop4_on() && mem.u32(coop4::kMagic) == 0x34504334u &&
                          mem.template at<uint8_t>(coop4::kBodySetB) != 0;
        return pm_body_type((table == 0xCA690 ? 0 : 1) + (setB ? 2 : 0));
    }
    return int32_t(mem.u32(table + mem.u32(0x51F42C) * 8));
}

// the set-ups' common body: body_init, (b / p2: y -1.2 first), the frame routine, +0x2EC = the event, type lowType
// or 34 (the characters' alternates) at y -1.2, the props' offset (x0, 0, 2394)
template <class P> void Game<P>::ending_body_setup(uint32_t obj, uint32_t table, uint32_t frame, uint32_t x0,
                                                   bool lowAlways, int16_t lowType)
{
    body_init(obj, ending_body_type(table), uint32_t(int32_t(of<int16_t>(obj, 0x70))));
    if (lowAlways)
        of<uint32_t>(obj, 0x24) = 0xBF99999Au;   // -1.2
    const int16_t type = of<int16_t>(obj, 0xCC);
    of<uint32_t>(obj, 0x50) = 0;
    of<uint32_t>(obj, 0) = frame;
    of<int32_t>(obj, 0x2EC) = mem.template at<int8_t>(G::event_index);
    if (type == lowType || type == 34)
        of<uint32_t>(obj, 0x24) = 0xBF99999Au;
    mem.set32(0x5433C4, x0);
    mem.set32(0x5433C8, 0);
    mem.set32(0x5433CC, 0x4515A000u);   // 2394
}

template <class P> void Game<P>::ending_p1_a(uint32_t obj) { ending_body_setup(obj, 0xCA690, 0x58CC0, 0xC4460000u, false, 14); }
template <class P> void Game<P>::ending_p1_b(uint32_t obj) { ending_body_setup(obj, 0xCA690, 0x59120, 0xC447B168u, true, 14); }
template <class P> void Game<P>::ending_p2_a(uint32_t obj) { ending_body_setup(obj, 0xCA694, 0x596F0, 0xC4460000u, true, 15); }
template <class P> void Game<P>::ending_p2_b(uint32_t obj) { ending_body_setup(obj, 0xCA694, 0x59B10, 0xC447B168u, true, 15); }

// the frames' head: +0x6A = 0; state 0: the shadow (floor 0x51EA40) and object_draw, else enemy_frame_end
template <class P> void Game<P>::ending_body_head(uint32_t obj)
{
    const int16_t s = of<int16_t>(obj, 0x60);
    of<int16_t>(obj, 0x6A) = 0;
    if (s == 0) {
        shadow_setup(mem.f32(0x51EA40));
        object_draw(obj);
    } else {
        enemy_frame_end(obj);
    }
}

// charsel (user 2026-11-06: "the researcher's body with Rogan's head" in the ending): role 1 of the players' bodies is
// the head; the frames below put a talking head there, Rogan's (0x2B closed, 0x749.. / 0x748 talking) on player 1's
// body and G's (0x281, 0x283.. / 0x282) on player 2's, for every type but the alternates 14 / 15 / 34 (the arcade's
// pairs have no other). With the seats' characters (PC Mode, a Character Select pick) each body gets its own type's
// head: Rogan's on Rogan, G's on G, none on the others (Sophie, the Researcher, Rogan 2, G 2 keep their own head)
namespace {
struct EndHead { uint32_t rest, last, first; };
constexpr EndHead kRoganHead{0x2B, 0x748, 0x749}, kGHead{0x281, 0x282, 0x283};
bool endHeadOf(int32_t type, EndHead& h)
{
    if (type == 2)
        h = kRoganHead;
    else if (type == 13)
        h = kGHead;
    return type == 2 || type == 13;
}
} // namespace

// the hand's role-1 model animated: frame_counter % 48 / 3 = k: k 15 -> last, else first + k
template <class P> void Game<P>::ending_body_hand(uint32_t obj, uint32_t last, uint32_t first)
{
    const int32_t k = (int32_t(mem.u32(G::frame_counter)) % 48) / 3;
    body_role_model_set(obj, 1, k == 15 ? last : first + uint32_t(k));
}

// the room's props (models 0x1D1D / 0x1D1F, 0x1D1A turned 0xC000) at their places less the offset 0x5433C4..CC
template <class P> void Game<P>::ending_body_props()
{
    const float c4 = mem.f32(0x5433C4), c8 = mem.f32(0x5433C8), cc = mem.f32(0x5433CC);
    cam_copro(0x05, {});
    cam_copro(0x12, {fbits(ffrom(0xC445C323u) - c4), fbits(0.0f - c8), fbits(ffrom(0x44FD72E1u) - cc)});
    geo_draw_model(0x1D1D);
    geo_draw_model(0x1D1F);
    cam_copro(0x06, {});
    cam_copro(0x05, {});
    const float x = ffrom(0xC4480000u) - mem.f32(0x5433C4);
    const float y = 0.0f - mem.f32(0x5433C8);
    const float z = ffrom(0x44EED75Cu) - mem.f32(0x5433CC);
    cam_copro(0x12, {fbits(x), fbits(y), fbits(z)});
    cam_copro(0x15, {0xC000});
    geo_draw_model(0x1D1A);
    cam_copro(0x06, {});
}

// at a camera cut (shift) the body's x / z go into the props' offset (and back to 0); the path kept at 0x5433D0
template <class P> void Game<P>::ending_body_shift(uint32_t obj, bool shift)
{
    if (shift) {
        mem.set32(0x5433C4, fbits(mem.f32(0x5433C4) + of<float>(obj, 0x20)));
        mem.set32(0x5433CC, fbits(mem.f32(0x5433CC) + of<float>(obj, 0x28)));
        of<uint32_t>(obj, 0x20) = 0;
        of<uint32_t>(obj, 0x28) = 0;
    }
    mem.set32(0x5433D0, mem.u32(G::camera_path));
}

// the event changed (+0x2EC): the slot freed, the parts freed, the task ends
template <class P> void Game<P>::ending_body_end(uint32_t obj)
{
    if (of<int32_t>(obj, 0x2EC) != int32_t(mem.template at<int8_t>(G::event_index))) {
        attack_slot_free(obj);
        body_free_parts(obj);
        task_exit();
    }
}

// 0x58CC0 ending_p1_a_frame: player 1 alone (event 0)
template <class P> void Game<P>::ending_p1_a_frame(uint32_t obj)
{
    ending_body_head(obj);
    const int16_t type = of<int16_t>(obj, 0xCC);
    EndHead h = kRoganHead;
    if (type != 14 && type != 34 && (!chars_on() || endHeadOf(type, h))) {
        const uint32_t path = mem.u32(G::camera_path);
        const float cf = mem.f32(0x51EB9C);
        if (path == 0x77 && !(cf < 630.0f) && cf < 700.0f)
            ending_body_hand(obj, h.last, h.first);
        else
            body_role_model_set(obj, 1, h.rest);
    }
    const bool mf = (of<uint8_t>(obj, 0x3C5) >> 4) & 1;   // MotFinish
    switch (uint32_t(int32_t(of<int16_t>(obj, 0x60)))) {
    case 0: {
        const int32_t t = of<int32_t>(obj, 0x50);
        of<int32_t>(obj, 0x50) = t + 1;
        if (t > 0x4F)
            of<int16_t>(obj, 0x60) = 1;
        break;
    }
    case 1:
        if (mf) {
            set_motion(obj, 0x15B);
            of<int16_t>(obj, 0x60) = 2;
        }
        break;
    case 2:
        if (mem.u32(G::camera_path) == 0x75 && mem.f32(0x51EB9C) == float(int32_t(mem.u32(0x2AB64)) - 1)) {
            of<uint32_t>(obj, 0x30) = of<uint32_t>(obj, 0x30) ^ 0x8000;
            set_motion(obj, 0x29E);
            of<int16_t>(obj, 0x60) = 3;
        }
        break;
    case 3:
        if (mf) {
            set_motion(obj, 0x2A0);
            of<uint32_t>(obj, 0xF8) = 1;
            of<int16_t>(obj, 0x60) = 4;
        }
        break;
    case 4:
        if (mf && of<int16_t>(obj, 0xF6) == 1) {
            set_motion(obj, 0x29F);
            of<uint32_t>(obj, 0xF8) = 1;
            of<int16_t>(obj, 0x60) = 5;
        }
        break;
    case 5:
        if (mem.f32(0x51EB9C) == 500.0f) {
            set_motion(obj, 0x29D);
            of<int16_t>(obj, 0x60) = 7;
        } else if (mf) {
            of<uint32_t>(obj, 0xF8) = mem.u32(0xC8A4C) - 1;
        }
        break;
    case 7:
        if (mf) {
            set_motion(obj, 0x2A0);
            of<uint32_t>(obj, 0xF8) = 1;
        }
        break;
    default:
        break;
    }
    ending_body_props();
    ending_body_shift(obj, mem.u32(G::camera_path) == 0x77 && mem.u32(0x5433D0) == 0x75);
    ending_body_end(obj);
}

// 0x59120 ending_p1_b_frame: player 1 of two (event 2)
template <class P> void Game<P>::ending_p1_b_frame(uint32_t obj)
{
    ending_body_head(obj);
    const int16_t type = of<int16_t>(obj, 0xCC);
    EndHead h = kRoganHead;
    if (type != 14 && type != 34 && (!chars_on() || endHeadOf(type, h))) {
        const uint32_t path = mem.u32(G::camera_path);
        const float cf = mem.f32(0x51EB9C);
        if ((path == 0x7C && !(cf < 300.0f) && cf < 390.0f) || (path == 0x7B && !(cf < 600.0f) && cf < 670.0f))
            ending_body_hand(obj, h.last, h.first);
        else
            body_role_model_set(obj, 1, h.rest);
    }
    const bool mf = (of<uint8_t>(obj, 0x3C5) >> 4) & 1;
    switch (uint32_t(int32_t(of<int16_t>(obj, 0x60)))) {
    case 0: {
        const int32_t t = of<int32_t>(obj, 0x50);
        of<int32_t>(obj, 0x50) = t + 1;
        if (t > 0xC7)
            of<int16_t>(obj, 0x60) = 1;
        break;
    }
    case 1:
        if (mf) {
            set_motion(obj, 0x15B);
            of<int16_t>(obj, 0x60) = 5;
        }
        break;
    case 2:
        if (mf && of<int16_t>(obj, 0xF6) == 1) {
            set_motion_blend(obj, 0x17E, 0, 16);
            of<int16_t>(obj, 0x60) = 3;
        }
        break;
    case 3:
        if (mem.f32(0x51EB9C) == 640.0f) {
            set_motion(obj, 0x17F);
            of<int16_t>(obj, 0x60) = 4;
        } else if (mf) {
            of<uint32_t>(obj, 0xF8) = mem.u32(0xC85C8) - 1;
        }
        break;
    case 4:
        if (mf) {
            set_motion_blend(obj, 0x15B, 0, 16);
            of<int16_t>(obj, 0x60) = 5;
        }
        break;
    case 5:
        if (mem.u32(G::camera_path) == 0x7C && mem.f32(0x51EB9C) == float(int32_t(mem.u32(0x2AB80)) - 0xB5)) {
            of<uint32_t>(obj, 0x30) = of<uint32_t>(obj, 0x30) ^ 0x8000;
            set_motion(obj, 0x29E);
            of<int16_t>(obj, 0x60) = 6;
        }
        break;
    case 6:
        if (mf) {
            set_motion(obj, 0x2A0);
            of<uint32_t>(obj, 0xF8) = 1;
            of<int16_t>(obj, 0x60) = 7;
        }
        break;
    case 7:
        if (mf && of<int16_t>(obj, 0xF6) == 1) {
            set_motion(obj, 0x29F);
            of<uint32_t>(obj, 0xF8) = 1;
            of<int16_t>(obj, 0x60) = 8;
        }
        break;
    case 8:
        if (mem.f32(0x51EB9C) == 500.0f) {
            set_motion(obj, 0x29D);
            of<int16_t>(obj, 0x60) = 9;
        } else if (mf) {
            of<uint32_t>(obj, 0xF8) = mem.u32(0xC8A4C) - 1;
        }
        break;
    case 9:
        if (mf) {
            set_motion(obj, 0x2A0);
            of<uint32_t>(obj, 0xF8) = 1;
        }
        break;
    default:
        break;
    }
    ending_body_props();
    ending_body_shift(obj, mem.u32(G::camera_path) == 0x7B && mem.u32(0x5433D0) == 0x7C);
    ending_body_end(obj);
}

// 0x596F0 ending_p2_a_frame: player 2 alone (event 1)
template <class P> void Game<P>::ending_p2_a_frame(uint32_t obj)
{
    ending_body_head(obj);
    const int16_t type = of<int16_t>(obj, 0xCC);
    EndHead h = kGHead;
    if (type != 15 && type != 34 && (!chars_on() || endHeadOf(type, h))) {
        const float cf = mem.f32(0x51EB9C);
        if (mem.u32(G::camera_path) == 0x79 && !(cf < 400.0f) && cf < 490.0f)
            ending_body_hand(obj, h.last, h.first);
        else
            body_role_model_set(obj, 1, h.rest);
    }
    const bool mf = (of<uint8_t>(obj, 0x3C5) >> 4) & 1;
    switch (uint32_t(int32_t(of<int16_t>(obj, 0x60)))) {
    case 0: {
        const int32_t t = of<int32_t>(obj, 0x50);
        of<int32_t>(obj, 0x50) = t + 1;
        if (t > 0x63)
            of<int16_t>(obj, 0x60) = 1;
        break;
    }
    case 1:
        if (mf) {
            set_motion(obj, 0x15B);
            of<int16_t>(obj, 0x60) = 2;
        }
        break;
    case 2:
        if (mem.u32(G::camera_path) == 0x78 && mem.f32(0x51EB9C) == 254.0f) {
            of<uint32_t>(obj, 0x30) = of<uint32_t>(obj, 0x30) ^ 0x8000;
            set_motion(obj, 0x29B);
            of<int16_t>(obj, 0x60) = 3;
        }
        break;
    case 3:
        if (mf) {
            set_motion(obj, 0x29C);
            of<uint32_t>(obj, 0xF8) = 1;
            of<int16_t>(obj, 0x60) = 4;
        }
        break;
    case 4:
        if (mf && of<int16_t>(obj, 0xF6) == 1) {
            set_motion(obj, 0x29A);
            of<uint32_t>(obj, 0xF8) = 1;
            of<int16_t>(obj, 0x60) = 5;
        }
        break;
    case 5:
        if (mf) {
            set_motion(obj, 0x29C);
            of<uint32_t>(obj, 0xF8) = 1;
        }
        break;
    default:
        break;
    }
    ending_body_props();
    const uint32_t path = mem.u32(G::camera_path), last = mem.u32(0x5433D0);
    ending_body_shift(obj, (path == 0x78 && last == 0x75) || (path == 0x79 && last == 0x78));
    ending_body_end(obj);
}

// 0x59B10 ending_p2_b_frame: player 2 of two (event 2; no props: p1 b draws them)
template <class P> void Game<P>::ending_p2_b_frame(uint32_t obj)
{
    ending_body_head(obj);
    const int16_t type = of<int16_t>(obj, 0xCC);
    EndHead h = kGHead;
    if (type != 15 && type != 34 && (!chars_on() || endHeadOf(type, h))) {
        const float cf = mem.f32(0x51EB9C);
        const bool win = mem.u32(G::camera_path) == 0x7C &&
                         ((!(cf < 180.0f) && cf < 270.0f) || (!(cf < 500.0f) && cf < 590.0f));
        if (win)
            ending_body_hand(obj, h.last, h.first);
        else
            body_role_model_set(obj, 1, h.rest);
    }
    const bool mf = (of<uint8_t>(obj, 0x3C5) >> 4) & 1;
    switch (uint32_t(int32_t(of<int16_t>(obj, 0x60)))) {
    case 0: {
        const int32_t t = of<int32_t>(obj, 0x50);
        of<int32_t>(obj, 0x50) = t + 1;
        if (t > 0x63)
            of<int16_t>(obj, 0x60) = 1;
        break;
    }
    case 1:
        if (mf) {
            set_motion(obj, 0x15B);
            of<int16_t>(obj, 0x60) = 2;
        }
        break;
    case 2:
        if (mem.u32(G::camera_path) == 0x7C && mem.f32(0x51EB9C) == float(int32_t(mem.u32(0x2AB80)) - 0xB5)) {
            of<uint32_t>(obj, 0x30) = of<uint32_t>(obj, 0x30) ^ 0x8000;
            set_motion(obj, 0x29B);
        }
        if (mem.f32(0x51EB9C) == 120.0f)
            of<int16_t>(obj, 0x60) = 5;
        else
            of<uint32_t>(obj, 0xF8) = 0;
        break;
    case 3:
        if (mf) {
            set_motion(obj, 0x29C);
            of<uint32_t>(obj, 0xF8) = 1;
            of<int16_t>(obj, 0x60) = 4;
        }
        break;
    case 4:
        if (mf) {
            set_motion(obj, 0x29A);
            of<uint32_t>(obj, 0xF8) = 1;
            of<int16_t>(obj, 0x60) = 5;
        }
        break;
    case 5:
        if (mf) {
            set_motion(obj, 0x29C);
            of<uint32_t>(obj, 0xF8) = 1;
        }
        break;
    default:
        break;
    }
    if (mem.u32(G::camera_path) == 0x7B && mem.u32(0x5433D4) == 0x7C) {
        of<uint32_t>(obj, 0x20) = 0x4164CCCDu;   // 14.3
        of<uint32_t>(obj, 0x28) = 0xC1000000u;   // -8
    }
    mem.set32(0x5433D4, mem.u32(G::camera_path));
    ending_body_end(obj);
}

// 0x55B0 player_st7_idle (the players' state 7 in the ending): player_idle (a ret) from now on, its first frame now
template <class P> void Game<P>::player_st7_idle(uint32_t task)
{
    mem.set32(task, 0x55D0);
    player_idle(task);
}

} // namespace hotd::port
