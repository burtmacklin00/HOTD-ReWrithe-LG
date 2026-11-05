// The chapter start (docs/teaser_plan.md R2): game_new's game part and chapter_load (0x25DA0, the port's since worker
// chstart: src/port/game_flow.cpp, with game_chapter_start 0x25D40) on top of a work RAM snapshot of a game in
// progress, so a chapter begins as the original begins it: the heap is reset, the chapter's task group is created
// from its start table (0xAB210: evt_chapter_start, camera_task_start, the input / player / attack-slot / depth-list
// tasks ...; the port's, src/port/chapter_tasks.cpp), then the camera, shot lists and chapter counters are cleared.
// UNVERIFIED (teaser): chapter_start's game_new part (the runtime's own form: any chapter / event, the settings).
#include "game/runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hotd::game {

using G = A::G;
using F = A::F;

namespace {
constexpr uint32_t kChapterGroup = 0x53E080;     // game_frame's group pointer (chapter_load's argument)
constexpr uint32_t kPlayerInGame = 0x50B0;       // player_in_game_frame
constexpr uint32_t kPlayerStateStarts = 0xA8790; // player state start routines (8 bytes each)
} // namespace

// heap_reset 0x14640 and task_group_create 0x14C10: the port's (src/port/tasks.cpp, proofs-7)

// The start table's routines (0xAB210): the port's (src/port/chapter_tasks.cpp; worker chstart moved them there).
// 0x5D30 InitSetPlayerTask: the port's, with the runtime's own form where the arcade has none: a player started
// straight in play (chapter_start: state 5, which has no start routine in table 0xA8790) gets player_in_game_frame,
// as after PlStartInit, and 4P seats 3 / 4 get their tasks after both players' (their frame: coop4_seat_frame)
void Runtime::InitSetPlayerTask()
{
    if (!coop4_on() && mem.at<port::a::Player>(G::players, 0).state != 5 && mem.at<port::a::Player>(G::players, 1).state != 5) {
        RuntimeBounds::InitSetPlayerTask();
        return;
    }
    for (uint32_t p = 0; p < 2; ++p) {
        port::a::Player& pl = mem.at<port::a::Player>(G::players, p);
        const uint32_t r = pl.state == 5 ? kPlayerInGame : mem.u32(kPlayerStateStarts + uint32_t(pl.state) * 8);
        const uint32_t t = task_create(r, 0x24);
        mem.at<port::a::Player>(G::players, p).task.a = t;
        mem.at<port::a::PlayerTask>(t).player = int8_t(p);
    }
    for (int p = 2; p < seat_count(); ++p) {
        const uint32_t t = task_create(port::coop4::kSeatRoutine, 0x24);
        mem.at<port::a::Player>(player_rec(p)).task.a = t;
        mem.at<port::a::PlayerTask>(t).player = int8_t(p);
    }
    mem.set32(G::input_source, 0);
}

// the PC's screen fade (Runtime::PcFade): 0x568730 sets the target and the time; 0x57CCE0 moves the level by
// (target - level) x elapsed / time left each drawn frame, i.e. a straight line
void Runtime::pcFadeStart(float from, float to, int ms)
{
    pcFade = PcFade{from, to, frameCount, std::max(1, ms * 60 / 1000)};
}

float Runtime::pcFadeLevel() const
{
    const uint64_t n = frameCount - pcFade.start;
    if (n >= uint64_t(pcFade.frames))
        return pcFade.to;
    return pcFade.from + (pcFade.to - pcFade.from) * float(n) / float(pcFade.frames);
}

// chapter_load's logic (0x25DA0) for `chapter` / `event` with `players` players in play: a new game (R4) with the
// settings applied (apply_settings) and the players in play started as PlStartInit's new-game row does (lives from
// the lives setting, score 0, full rounds); the others wait for their start button (state 9)
void Runtime::chapter_start(int chapter, int event, int players, int mode)
{
    mem.at<int8_t>(G::chapter) = int8_t(chapter);
    mem.at<int8_t>(G::event_index) = int8_t(event);
    apply_settings();
    const int32_t lives = mem.at<uint8_t>(0x51F5B1);
    // game_new (0x25C40), the game part: game_difficulty_set (0x26490: the dynamic difficulty's start value from
    // table 0xAB278 by the difficulty setting, 0x520714 / 0x520718 = 1, hits_counter 0), the route history
    // (scene_history 0x520720: 4 x 16 bytes of 0xFF), hostages_appeared -1, hostages_rescued 0
    if (mode == 4) {   // game_new: the sound queue emptied, all sound off and the board's setting, straight to the board
        sound_queue_reset();
        sound_send3(0xA00002);
        sound_send3(0xBE5000);
    }
    mem.at<int16_t>(0x51F492) = int16_t(mem.at<int8_t>(0xAB278 + uint32_t(int32_t(mem.at<int16_t>(G::difficulty)))));
    mem.set32(0x520718, 1);
    mem.set32(0x520714, 1);
    mem.at<uint8_t>(0x51F618) = 0;
    for (uint32_t i = 0; i < 0x40; ++i)
        mem.at<uint8_t>(0x520720 + i) = 0xFF;
    // a game started past Chapter 1 (--chapter, the chapter select, a game over's restart): the earlier chapters'
    // rows hold their default routes, as if played. The game over's route map (game_over_route_task 0x5CF50) walks
    // every row from Chapter 1; an empty row (event -1) points it at 0xC1810 - 0xC0 (garbage records: reads outside
    // memory, the map never ends: a soft-lock in mode 5 sub-mode 3; ch2proof bot seeds). Not in the arcade. This also
    // covers ch3own's finding: Chapter 1's first-event byte (read for the route in every chapter) is 0, not 0xFF.
    static const int8_t kDefaultRoutes[3][10] = {{0, 1, 3, 4, 5, 6, 19, -1}, {0, 1, 2, 3, 4, 5, 7, 8, 9, 15},
                                                 {0, 1, 3, 6, 7, -1}};
    for (int c = 0; c < chapter && c < 3; ++c)
        for (uint32_t i = 0; i < 10 && kDefaultRoutes[c][i] >= 0; ++i)
            mem.at<int8_t>(0x520720 + uint32_t(c) * 16 + i) = kDefaultRoutes[c][i];
    mem.at<int16_t>(0x51F426) = -1;
    mem.at<int16_t>(0x51F424) = 0;
    // mode 4 (the game, set_mode at the start button); game_new leaves sub-mode 2 (the play) after chapter_load
    // (title worker: mode 3 = the attract demo, demo_start 0x23B60 -> chapter_load, sub-mode 1 = demo_run)
    mem.at<int16_t>(G::game_mode) = int16_t(mode);
    mem.set32(G::game_submode, mode == 4 ? 2 : 1);
    // proofs-8 (polish round 2 #8): the frame counter 0x51F428 runs from power-on on the arcade (boot, title, coin),
    // and the HUD's blinks run on it (HOLD YOUR FIRE!: hud_sprite107_blink, on while counter % 60 < 45). A game started
    // straight into a chapter (no title: the counter still 0) starts it where the MAME reference fullgame_us has it at
    // event 0 (our frame f = its counter f + 1015; its snaps 0012 / 0015 / 0016 = counters 1319 / 1664 / 1779: off,
    // on, on, as hud_sprite107_blink gives), so the blinks keep the arcade's phase
    if (mode == 4 && mem.u32(G::frame_counter) == 0)
        mem.set32(G::frame_counter, 1015);
    mem.at<int16_t>(G::hostages_rescued_chapter) = 0;
    // GitHub #29: one player in player 2's seat (soloSeat 1; arcade games: PC Mode's seats are its character picks)
    const bool p2Solo = players == 1 && soloSeat == 1 && !pm_on() && mode == 4;
    for (int p = 0; p < 2; ++p) {
        port::a::Player& pl = mem.at<port::a::Player>(G::players, uint32_t(p));
        if (p2Solo ? p == 1 : p < players) {
            pl.state = 5;
            pm_join_char(p);
            pm_reload_at[p] = 0;
            pl.lives = pl.lives_saved = pm_on() ? pm_table(pm_seat_char(p)).hp : lives;   // PC Mode: the character's
            pl.score = 0;
            pl.rounds = pm_full_rounds(p);
            pl.empty = 0;
            pl.continue_timer = 0;
        } else {
            pl.state = 9;
        }
    }
    clear = ChapterClear{};
    // PC Mode: pc_continues_init 0x4488E4 (credits = the Continues option + 1 in both slots 0xD9F790 / 0xD9F794), then
    // the PC Mode start 0x43BAD8 takes 1 from slot 0 for player 1 and 1 from slot 1 for player 2 (when in), with or
    // without Free-Play. Only slot 0 is the shared pool (0xDA0B98 = 0), so the pool starts at the Continues option,
    // for 1 or 2 players (4P: seats 3 / 4 likewise take nothing from it)
    pm_credits = std::clamp(pm_continues, 0, 9);
    pm_credits_seen = pm_credits;
    // the PC's evt_chapter_start (0x4C0CF0): the screen fades in from black over 1000 ms (from white over 3000 for
    // the ending, chapter 6); real-PC harness captures (pcpolish2\gameover t253-t703, a millisecond a tick there): the
    // chapter card and the opening rise from black in a straight line
    if (pcModels)
        chapter == 6 ? pcFadeStart(2.0f, 1.0f, 3000) : pcFadeStart(0.0f, 1.0f, 1000);
    sprites2d_ = false;   // the 2D layer: set up again at its first use (src/game/sprites.cpp)
    coverOff_ = mode == 4 && event != 0;
    chapterDone = false;
    // 4P (docs/coop4.md): the original's counts see at most two (its [count][player] tables, the arcade's curve);
    // seats 3 / 4 and the real count live in the 4P block
    mem.at<int16_t>(G::players_in_game) = int16_t(std::min(players, 2));
    mem.at<int16_t>(G::players_in_game_b) = int16_t(std::min(players, 2));
    coop4_reset(players, lives);
    chapter_load(kChapterGroup);
    retry_point_take();
}

// GitHub #5: the retry point (players 1 / 2 in play or starting it, their scores and lives) as a chapter begins
void Runtime::retry_point_take()
{
    retryPoint.chapter = mem.at<int8_t>(G::chapter);
    for (int p = 0; p < 2; ++p) {
        const port::a::Player& pl = mem.at<port::a::Player>(G::players, uint32_t(p));
        retryPoint.in[p] = pl.state == 5 || pl.state == 2 || pl.state == 3;
        retryPoint.score[p] = pl.score;
        retryPoint.lives[p] = pl.lives;
    }
}

// 0x25D40 game_chapter_start (sub-mode 1, after game_chapter_end_wait put chapter + 1; the port's, src/port/
// game_flow.cpp: players 1 / 2 in play to state 2, chapter_load at the event NEXTEV left, sub-mode 2), then the
// runtime's side: 4P seats 3 / 4 in play get a full gun as PlStartInit gives players 1 / 2 (their task is
// coop4_seat_frame, not the state table's), the HUD's results view off, and chapterChanged tells the host to load
// the new chapter's world
void Runtime::game_chapter_start()
{
    RuntimeBounds::game_chapter_start();
    for (int p = 2; p < seat_count(); ++p) {
        port::a::Player& pl = mem.at<port::a::Player>(player_rec(p));
        if (pl.state == 5) {
            pl.rounds = pm_full_rounds(p);
            pl.empty = 0;
        }
    }
    clear = ChapterClear{};
    chapterDone = false;
    const int chapter = mem.at<int8_t>(G::chapter);
    chapterChanged = chapter;
    retry_point_take();   // (#5)
    if (log)
        log("chapter " + std::to_string(chapter + 1) + " starts (event " + std::to_string(int(mem.at<int8_t>(G::event_index))) + ")");
}

// 0x25DA0 chapter_load: the port's (src/port/game_flow.cpp), with the runtime's set-up around it: the 2D RAMs and
// the boot's 2D set-up for a new game (sprites2d_ off: the chapter's 2D part is chapter_load's own), PC Mode's task
// counts, the developer's character pair, the 4P bodies; after it the black cover lifted for a game started past
// event 0
void Runtime::chapter_load(uint32_t groupPtr)
{
    if (!sprites2d_) {
        sprites2d_ = true;
        sprites_boot2d();
    }
    pm_blood_tasks = 0;   // PC Mode: every task gone; the PC's sprite_init 0x412660 zeroes pc_blood_task_count
    // developer (coop4): HOTD_CHAR_PAIR=n sets the title code's character pair (0x51F42C, table 0xCA690: 3 = types
    // 14 / 15, "Rogan 2" / "G 2", the 4P mode's P3 / P4 bodies) as the title's code entry does (attract.cpp)
    if (const char* s = std::getenv("HOTD_CHAR_PAIR"))
        mem.set32(0x51F42C, uint32_t(std::atoi(s) & 7));
    coop4_body_set();   // the 4P mode: players 3 / 4 alone -> their bodies (types 14 / 15) in the cut-scenes
    RuntimeBounds::chapter_load(groupPtr);
    sprites_cover_lift();
    // developer: HOTD_DUMP_RAM=<file> writes the work RAM as the chapter starts (the boot / snapshot diff, s1.md)
    if (const char* path = std::getenv("HOTD_DUMP_RAM"))
        if (FILE* f = std::fopen(path, "wb")) {
            std::fwrite(mem.ptr(Mem::kRam), 1, Mem::kRamSize, f);
            std::fclose(f);
        }
}

// 0x34FF0 texture_area_load (TEXCHG): the engine draws with the area's texture set (GameView); color_curve_tables_set
// (0x1330) puts the area's own colour rows back, so the rows swapped in by color_row_set are dropped. The second
// argument, unless -1, is a cover sprite put over the screen first (sprite_place(0x1004000, 1, 0, 0, 0, cover), also
// for the area already loaded): 0xA1 (black) from chapter_load (over chapter_screen_reset's own) and the ending's
// kind111 (lifted by kind110_wait), 0x8D from the prologue's kind107 (lifted by kind104_wait)
void Runtime::texture_area_load(uint32_t area, int32_t cover)
{
    if (cover != -1) {
        sprites_ensure();
        sprite_place(0x1004000, 1, 0, 0, 0, cover);
    }
    // the work RAM part (ch4proof, the whole-game differential, read from the disassembly): a new area's texture
    // source (texture_area_upload 0xEB0 keeps it in 0x510EC4), the area's texture palette list for the vblank
    // (texture_palette_upload 0x2010: 0x511F04, vblank callback 0x1F70) and color_curve_tables_set 0x1330: the 14
    // colour rows of the area (table 0xA99A0) into the shadow tables 0x512D20 / 0x513D20 / 0x514D20, an upload asked
    // (0x51E8C4).
    if (area != mem.u32(G::texture_area))
        mem.set32(0x510EC4, mem.u32(0xA99E0 + area * 4));
    mem.set32(0x511F04, mem.u32(0xA9960 + area * 4));
    vblank_callback_add(0x1F70);
    mem.set32(0x51E8C4, 0);
    const uint32_t rows = mem.u32(0xA99A0 + area * 4);
    const float scale = mem.f32(0xA831C);
    for (uint32_t r = 0; r < 14; ++r) {
        const uint32_t src = mem.u32(rows + r * 4);
        for (uint32_t i = 0; i < 0x40; ++i)
            for (uint32_t c = 0; c < 3; ++c) {
                // ldob, cvtir, mulr by the constant 0xA831C, cvtzril, at most 0xFF (unsigned compare)
                const int32_t v = int32_t(float(mem.at<uint8_t>(src + i * 3 + c)) * scale);
                mem.at<uint16_t>(0x512D20 + c * 0x1000 + r * 0x100 + i * 2) = uint16_t(uint32_t(v) > 0xFF ? 0xFF : v);
            }
    }
    mem.set32(0x51E8C4, 1);
    textureArea = int(area);
    mem.set32(G::texture_area, area);   // (the original keeps the area it loaded: the iron ball's models, palettes)
    if (log && std::getenv("HOTD_TRACE_ROWS"))
        log("texture_area_load area " + std::to_string(area));
    colorRows.clear();
    bloodRows.clear();
    ++colorRowsSerial;
}

// 0x13F20 color_row_set(row, entry): one colour translation row from the row library (maincpu 0xE211, 0xC0 bytes an
// entry); the engine rebuilds the colour translation of the current texture set (GameView)
void Runtime::color_row_set(int32_t row, int32_t entry)
{
    colorRows[2 * row + 1] = entry;   // the odd table rows: 0x512D20 + row x 0x100 = table row 2 x row + 1
    bloodRows.erase(2 * row + 1);      // (GGSET marks it again when it is a blood row)
    ++colorRowsSerial;
    // the work RAM part (ch4proof, the whole-game differential): the row into the three shadow tables (r / g / b,
    // 0x40 halfwords from the library's byte triples) and colour_row_queue (palette_grey_build averages these tables)
    for (uint32_t i = 0; i < 0x40; ++i) {
        const uint32_t src = 0xE211 + uint32_t(entry) * 0xC0 + i * 3, dst = uint32_t(row) * 0x100 + i * 2;
        mem.at<uint16_t>(0x512D20 + dst) = mem.at<uint8_t>(src);
        mem.at<uint16_t>(0x513D20 + dst) = mem.at<uint8_t>(src + 1);
        mem.at<uint16_t>(0x514D20 + dst) = mem.at<uint8_t>(src + 2);
    }
    colour_row_queue(row);
    if (log && std::getenv("HOTD_TRACE_ROWS"))
        log("color_row_set row " + std::to_string(row) + " entry " + std::to_string(entry));
}

// 0x6C2A0 shadow_light_reset / 0x6C760 shadow_setup: the port's (src/port/shadow.cpp, proofs-4, capture-proven); the
// runtime keeps the floor height for the renderer's shadow pass
void Runtime::shadow_setup(float y)
{
    shadowFloor_ = y;   // render: the shadow pass (part_draw) squashes the shadow models onto this height (the view)
    port::Game<A>::shadow_setup(y);
}

// 0x6C9E0 shadow_update: the port's (src/port/blow_off.cpp, proofs-8: the projection onto the probed floor's plane);
// the view's floor height is that projection's height term (0x51FE18: the plane's height when it is level)
void Runtime::shadow_update(uint32_t obj)
{
    port::Game<A>::shadow_update(obj);
    shadowFloor_ = mem.f32(0x51FE18);
}

// 0x4B300 world_draw_task, the game-state part (the engine draws the world itself): the world's light from the zone's
// angles (table 0xC0BF0 by chapter -> byte by draw zone -> 0xC0C10: pitch, yaw) through matrix_identity_set into
// 0x5430C0 and the geometrizer's light record (0x8000A0, three words), then per placement of the zone its model-list
// position (0x5430D0: placements with a model list at +0x14 cycle through it, -1 restarts at 1) and its fade
// (0x543210: += the half-word at +0x12 unless +0x10 or the state is 0xFF; 0xFF from 0xFF on). The draws are the
// engine's; uv_scroll_begin / uv_scroll_model's work RAM is kept (arcpal). UNVERIFIED (teaser).
void Runtime::world_draw_task(uint32_t)
{
    mem.set32(0x51E800, 0);   // uv_scroll_begin 0x1E280
    const uint32_t ch = uint32_t(int32_t(mem.at<int8_t>(G::chapter)));
    const int32_t zone = mem.at<int8_t>(G::draw_zone);
    const int32_t idx = mem.at<int8_t>(mem.u32(0xC0BF0 + ch * 4) + uint32_t(zone));
    matrix_identity_set(mem.u32(0xC0C10 + uint32_t(idx) * 8), mem.u32(0xC0C10 + uint32_t(idx) * 8 + 4), 0x5430C0);
    geo_write(0x8000A0, 0xA0A);
    for (uint32_t k = 0; k < 3; ++k)
        geo_write(0x804000, mem.u32(0x5430C0 + 4 * k));
    const uint32_t placements = mem.u32(0x5430B4);
    for (uint32_t p = mem.u32(0x5430B0) + uint32_t(zone) * 0x32;; ++p) {
        const int32_t i = mem.at<int8_t>(p);
        if (i == -1)
            break;
        const uint32_t pl = placements + uint32_t(i) * 0x18;
        uint32_t model = mem.u32(pl);
        if (const uint32_t list = mem.u32(pl + 0x14)) {
            const uint32_t slot = 0x5430D0 + uint32_t(i) * 4;
            const uint32_t k = mem.u32(slot);
            mem.set32(slot, k + 1);
            model = mem.u32(list + k * 4);
            if (model == 0xFFFFFFFFu) {
                mem.set32(slot, 1);
                model = mem.u32(list);
            }
        }
        const uint32_t fade = 0x543210 + uint32_t(i) * 4;
        if (mem.at<int16_t>(pl + 0x10) != 0xFF && mem.u32(fade) != 0xFF) {
            const int32_t v = int32_t(mem.at<int16_t>(pl + 0x12)) + int32_t(mem.u32(fade));
            mem.set32(fade, v > 0xFE ? 0xFF : uint32_t(v));
        }
        // Chapter 1's canal water (the bridge's hashi02 / hashi03 layers), only for a placement the geometrizer sees:
        // 0x4B434 sends the placement's position and its model's radius (0x2EEC390[model] +0x18) to coprocessor
        // function 0x7B (sphere visible) and skips the placement (draw and uv_scroll_model) for a result <= 0
        if (ch == 0 && i >= 0x1C && i <= 0x1E) {
            const uint32_t in[4] = {mem.u32(pl + 4), mem.u32(pl + 8), mem.u32(pl + 0xC), mem.u32(mem.u32(0x2EEC390 + model * 4) + 0x18)};
            port::Tgp::Out o;
            copro(0x7B, in, 4, o);
            float vis = 0.0f;
            if (o.n > 0)
                std::memcpy(&vis, &o.w[0], 4);
            if (!(vis > 0.0f))
                continue;
            uv_scroll_model(uint32_t(0x916 + (i - 0x1C)));
            uv_scroll_model(uint32_t(0x91A + (i - 0x1C)));
        }
    }
}

// 0x1E290 uv_scroll_model(model), its work RAM (arcpal, 2026-10-27; the engine draws the water itself, view.cpp): the
// model's index in table 0x1B290 (9 models, else nothing), its texture points (ROM 0x1B2C0 + index x 0x230, two words
// each, 4 a polygon) scrolled by frame_counter % 1024 into the buffer 0x51E190 at the fill count 0x51E800 (in words),
// 0x8A4 the other way; the buffer's polygon-RAM address 0x808000 + count into 0x51E7D0 + 4 x index, the count += 8 x
// the model's polygon count (main data 0x2E73530 + 16 x model, +0xC). The geometrizer writes are not made.
void Runtime::uv_scroll_model(uint32_t model)
{
    uint32_t idx = 0;
    while (idx < 9 && mem.u32(0x1B290 + idx * 4) != model)
        ++idx;
    if (idx == 9)
        return;
    const int32_t f = int32_t(mem.u32(G::frame_counter)) % 0x400;
    const uint32_t count = mem.u32(0x51E800);
    const uint32_t n = mem.u32(0x2E7353C + model * 0x10);
    if ((n & 0x3FFFFFFFu) != 0) {
        uint32_t dst = 0x51E190 + (count / 2) * 8;
        uint32_t src = 0x1B2C0 + idx * 0x230;
        uint32_t k = 0;
        do {
            const uint32_t u = (mem.u32(src) & 0x1FFFFFFu) >> 5, v = (mem.u32(src + 4) & 0x1FFFFFFu) >> 5;
            if (model == 0x8A4) {
                mem.set32(dst, u);
                mem.set32(dst + 4, uint32_t(int32_t(v + 0x3FF) - f));
            } else {
                mem.set32(dst, uint32_t(f) + u);
                mem.set32(dst + 4, v);
            }
            ++k;
            src += 8;
            dst += 8;
        } while (k < (n << 2));
    }
    mem.set32(0x51E800, count + n * 8);
    mem.set32(0x51E7D0 + idx * 4, count + 0x808000);
}

// 0x33F50 sky_task: the port's (src/port/sky.cpp, proofs-5); its two models go to the draw list as backgrounds
// (drawn first, without depth writes)
void Runtime::sky_task(uint32_t task)
{
    skyDraw_ = true;
    port::Game<A>::sky_task(task);
    skyDraw_ = false;
}

} // namespace hotd::game
