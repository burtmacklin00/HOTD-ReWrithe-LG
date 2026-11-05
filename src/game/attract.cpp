// The attract loop and the title screen (docs/teaser_status/title.md): the arcade's own modes before a game, read from
// the typed decompilation (private/decomp/arcade) and the disassembly; UNVERIFIED (teaser), checked against a MAME
// attract capture (tools\run_mame_attract.ps1: frames and the mode / sub-mode timeline), no capture proof.
//
// USA next-mode table (0xA9630, region 1): 1 -> 9 -> 2 -> 8 -> 3 -> 6 -> 1.
//   mode 1  "WINNERS DON'T USE DRUGS" (attract_start / attract_run 0x239A0 / 0x23A40): sprite 10, 360 frames
//   mode 9  the "1998.12.18" prologue (chapter slot 5): not ported here (Chapter 1 data only), skipped
//   mode 2  the title (title_start 0xD100, 0xD290, title_code_screen 0xD2C0): the mansion (Chapter 1's world, title
//           zone list 0xC0C20, texture area 1) on camera path 0x69, the logo sprites from frame 184, 658 frames
//   mode 8  the how-to-play demo (chapter slot 4): skipped
//   mode 3  the attract demo (demo_list 0xAB0E0): see attract_demo below
//   mode 6  the ranking: skipped
// The start button (player_start_check 0x6020, the port's) calls set_mode(4) from any of them; mode 4 sub-mode 0 is
// game_new -> chapter_load: Runtime::chapter_start (Chapter 1, event 0).
#include "game/runtime.h"

#include <cstring>

namespace hotd::game {

using G = A::G;
using port::a::Player;

namespace {
constexpr uint32_t kAttractGroup = 0x53DE80;    // mode 1's task group, 0x53DE84 its timer
constexpr uint32_t kTitleGroup = 0x512290;      // mode 2's task group, 0x512294 its frame counter
constexpr uint32_t kTitleCode = 0x51F780;       // the title code buffer (L / R, 15 + 1), 0x51F790 its length
constexpr uint32_t kPrompt = 0x51E860;          // coin_update's prompt record (PRESS START BUTTON)
constexpr uint32_t kCount = 0x51E880;           // coin_update's credit record (FREE PLAY)
constexpr uint32_t kHudCounter = 0x51F560;
constexpr uint32_t kCamFrame = G::camera_records + 0xEC;   // camera_records[0].frame
constexpr uint32_t kLayer0 = 0x1000000, kLayer2 = 0x1004000;

uint32_t fbits(float v)
{
    uint32_t u;
    std::memcpy(&u, &v, 4);
    return u;
}
} // namespace

// 0xDDC0 next_mode: the next mode from table 0xA9630[region][mode]; players to state 9 outside modes 4 / 5
void Runtime::next_mode()
{
    const int16_t mode = mem.at<int16_t>(G::game_mode);
    if (mode == 5 && !titleFlow) {   // game_over_wait: the host starts the chapter again (no title)
        gameOverDone = true;
        return;
    }
    if (mode == 5) {   // title-2: the title's loop goes on (USA: mode 7, the ranking)
        attract = true;
        textureArea = -1;
    }
    const int8_t next = mem.at<int8_t>(mem.u32(0xA9630 + uint32_t(mem.at<uint8_t>(G::region)) * 4) + uint32_t(mode));
    mem.at<int16_t>(G::players_in_game_b) = 0;
    mem.at<int16_t>(G::players_in_game) = 0;
    mem.at<int16_t>(0x51F4A6) = 0;
    mem.at<int16_t>(0x51F4A2) = 0;
    mem.at<Player>(G::players, 0).hit = 0;
    mem.at<Player>(G::players, 1).hit = 0;
    mem.set32(G::shake_timer, 0);
    mem.at<int16_t>(0x51F778) = 0;
    mem.set32(G::game_submode, 0);
    mem.set32(G::camera_status, mem.u32(G::camera_status) | 2);
    mem.at<int16_t>(G::game_mode) = next;
    if (next > 5 || next < 4) {
        mem.at<Player>(G::players, 0).state = 9;
        mem.at<Player>(G::players, 1).state = 9;
    }
    mem.set32(0x51F610, 0);
    whiteFade = -1;
    if (log)
        log("game mode " + std::to_string(next) + " (next_mode from " + std::to_string(mode) + ")");
}

// 0x205A0 hud_mode_init: the prompt records by mode (table 0xAA1E0: hud_layout_mode_a 0x20450 for modes 0, 1, 4, 5,
// hud_layout_mode_b 0x20500 for the others: PRESS START blinking, FREE PLAY shown)
void Runtime::hud_mode_init()
{
    mem.set32(kHudCounter, 0);
    const bool b = mem.u32(0xAA1E0 + uint32_t(mem.at<int16_t>(G::game_mode)) * 4) == 0x20500;
    for (uint32_t r : {kPrompt, kPrompt + 0xC, kCount, kCount + 0xC}) {
        for (uint32_t k = 0; k < 8; k += 2)
            mem.at<uint16_t>(r + k) = 0;
        if (b) {
            mem.at<uint16_t>(r) = 1;                                   // shown
            mem.at<uint16_t>(r + 4) = r == kPrompt || r == kPrompt + 0xC;   // the prompt blinks
        } else {
            mem.at<uint16_t>(r + 2) = 1;                               // drawn: cleared on the next frame
        }
    }
    for (uint32_t r : {kPrompt + 8u, kCount + 8u, kPrompt + 0x14u, kCount + 0x14u})
        mem.set32(r, 0);
    mem.set32(0x51E8A0, 0);
    mem.set32(0x51E8A8, 0);
}

// 0x20BD0 hud_layout_by_mode: the row of the prompts' y tables
int Runtime::hud_layout_by_mode() const
{
    switch (mem.at<int16_t>(G::game_mode)) {
    case 2: return 2;
    case 3:
    case 8: return 3;
    case 4: return mem.u32(G::game_submode) == 9 || mem.u32(G::game_submode) == 10 ? 0 : 1;
    case 5: return 1;
    case 9: return 4;
    default: return 0;
    }
}

// arcpal (2026-10-27): coin_update's credit check, every frame in every mode (sprites_frame, after the counter):
// credit_prompt_draw / credit_count_draw keep the credits they last showed in their record (+8 the credits 0x51F4B8,
// +10 the coin fraction 0x51F4C0; shared credits). A change (or the lockout 0x51E828 / 0x51E82A at 0x7F with
// 0x51E824 set) copies them and restarts coin_update's counter 0x51F560. hud_mode_init zeroes the copies, so in free
// play (credits 1) this runs on the frame after every mode change (the whole-RAM differential's 0x51E868 /
// 0x51E888). The arcade then also draws the prompt that frame in the modes of table 0xAA210; not done here (the
// free-play prompts' look is kept).
void Runtime::coin_credits_seen()
{
    const int32_t credits = int32_t(mem.u32(0x51F4B8)), frac = int32_t(mem.u32(0x51F4C0));
    const bool lock = mem.at<int16_t>(0x51E828) == 0x7F || mem.at<int16_t>(0x51E82A) == 0x7F;
    for (uint32_t rec : {kPrompt, kCount})
        if (mem.at<int16_t>(rec + 8) != credits || mem.at<int16_t>(rec + 10) != frac || (lock && mem.at<int16_t>(0x51E824) != 0)) {
            mem.at<int16_t>(rec + 10) = int16_t(frac);
            mem.set32(kHudCounter, 0);
            mem.at<int16_t>(rec + 8) = int16_t(credits);
        }
}

// coin_update (0x1F980, every frame), its free-play prompts only: credit_prompt_draw (0x20010) and credit_count_draw
// (0x1FC00) for the shared credits (0x51FDBC = 0) in free play (0x51FDB8 = 1). credit_blink_state (0x20260): the
// record's +0 shown, +2 drawn, +4 blink mode (1: by bit 5 of the counter), +6 countdown. The coins themselves are
// not modelled (no coin in the teaser).
void Runtime::coin_prompts()
{
    auto blink = [&](uint32_t rec, bool& on, bool& off) {
        uint16_t shown = mem.at<uint16_t>(rec);
        const uint16_t modeB = mem.at<uint16_t>(rec + 4);
        if (modeB == 1) {
            shown = uint16_t(~(mem.u32(kHudCounter) >> 5) & 1);
        } else if (modeB == 2) {
            const uint16_t c = mem.at<uint16_t>(rec + 6);
            mem.at<uint16_t>(rec + 6) = uint16_t(c - 1);
            shown = c != 1;
            if (c == 1)
                mem.at<uint16_t>(rec + 4) = 0;
        }
        mem.at<uint16_t>(rec) = shown;
        const int s = (shown ? 2 : 0) | (mem.at<uint16_t>(rec + 2) != 0 ? 1 : 0);
        on = s == 2;
        off = s == 1;
    };
    // PC models: THOTD's attract has no PRESS START BUTTON / FREE PLAY, it draws "Press SPACE to skip Demo"
    // (Hud::drawPc2D, 0x41B3F8); UNVERIFIED (teaser)
    if (pcModels && attract && mem.at<int16_t>(G::game_mode) != 4 && mem.at<int16_t>(G::game_mode) != 5)
        return;
    const int row = hud_layout_by_mode();
    bool on, off;
    // PRESS START BUTTON: table 0xAA290 entry 3 (sprite, x, y table), the clear rect at +0x30
    blink(kPrompt, on, off);
    if (on) {
        mem.at<uint16_t>(kPrompt + 2) = 1;
        sprite_draw_anim(kLayer0, 1, 0, mem.at<int16_t>(0xAA2AA), mem.at<int16_t>(mem.u32(0xAA2AC) + uint32_t(row) * 2),
                         uint32_t(mem.at<int16_t>(0xAA2A8)));
    }
    if (off) {
        mem.at<uint16_t>(kPrompt + 2) = 0;
        tilemap_clear_rect(kLayer0, mem.at<int16_t>(0xAA2C0), mem.at<int16_t>(mem.u32(0xAA2C4) + uint32_t(row) * 2),
                           mem.at<int16_t>(0xAA2C8), mem.at<int16_t>(0xAA2CA));
    }
    // FREE PLAY: table 0xAA350 entry 0, the clear rect at 0xAA394
    blink(kCount, on, off);
    if (on) {
        mem.at<uint16_t>(kCount + 2) = 1;
        sprite_draw_anim(kLayer0, 1, 0, mem.at<int16_t>(0xAA352), mem.at<int16_t>(mem.u32(0xAA354) + uint32_t(row) * 2),
                         uint32_t(mem.at<int16_t>(0xAA350)));
    }
    if (off) {
        mem.at<uint16_t>(kCount + 2) = 0;
        tilemap_clear_rect(kLayer0, mem.at<int16_t>(0xAA394), mem.at<int16_t>(mem.u32(0xAA398) + uint32_t(row) * 2),
                           mem.at<int16_t>(0xAA39C), mem.at<int16_t>(0xAA39E));
    }
}

// 0x1EA30 body_parts_mark_role (object, role) / 0x1E9B0 body_parts_mark_role_rec (part, role): the object's flags
// +0x58 |= 0x102, then every part of its body (root +0xC8: children +0x24, count +0x34) whose role's class (table
// 0x1184E0, 8 bytes a role, 31 roles a type: the owner's type +0xCC; class at +3, signed) equals role gets +0x3C |=
// 0x102 (shot / blown off), recursively. UNVERIFIED (teaser)
void Runtime::body_parts_mark_role(uint32_t obj, int32_t role)
{
    auto mark = [&](auto& self, uint32_t part) -> void {
        const uint32_t owner = mem.u32(part + 0x40);
        const int32_t type = mem.at<int16_t>(owner + 0xCC);
        const int32_t cls = mem.at<int8_t>(G::part_roles + uint32_t(mem.u32(part + 0x38) + uint32_t(type) * 0x1F) * 8 + 3);
        if (cls == role)
            mem.set32(part + 0x3C, mem.u32(part + 0x3C) | 0x102);
        const uint32_t kids = mem.u32(part + 0x24);
        for (int32_t i = 0; i < int32_t(mem.u32(part + 0x34)); ++i)
            self(self, mem.u32(kids + uint32_t(i) * 4));
    };
    const uint32_t root = mem.u32(obj + 0xC8);
    mem.set32(obj + 0x58, mem.u32(obj + 0x58) | 0x102);
    const uint32_t parts = mem.u32(root + 0x24);
    for (int32_t i = 0; i < int32_t(mem.u32(root + 0x34)); ++i)
        mark(mark, mem.u32(parts + uint32_t(i) * 4));
}

// the host's start: the boot's first mode (USA: 1) runs; the 2D layer from scratch (sprite_init)
void Runtime::attract_begin()
{
    attract = true;
    titleFlow = true;
    sprites2d_ = false;
    sprites_ensure();
    log && (log("attract: game mode " + std::to_string(mem.at<int16_t>(G::game_mode))), true);
}

// the modes outside the game (game_mode_frame sends every mode but 4 / 5 here)
void Runtime::attract_frame()
{
    const int16_t mode = mem.at<int16_t>(G::game_mode);
    const uint32_t sub = mem.u32(G::game_submode);
    switch (mode) {
    case 1:   // mode1_attract 0x23980 (table 0xAAFA0)
        // the PC release has no such screen (its sprite 10 is another picture); the arcade's is optional, off by default
        // (launcher "FBI screen", --fbi-screen): skipped as the original's next-mode table goes on (1 -> 9)
        if (pcModels || !fbiScreen) {
            next_mode();
            return;
        }
        if (sub == 0) attract_start(); else attract_run();
        break;
    case 2:   // mode2_title 0xD0E0 (table 0xA95D0)
        if (sub == 0) title_start(); else title_run();
        break;
    case 3:
        attract_demo();
        break;
    case 8:   // mode8 0x26590 (table 0xAB280)
        how_to_play();
        break;
    case 6:
    case 7:   // mode67_ranking 0x21D90 (table 0xAA460)
        ranking();
        break;
    case 9:   // mode9_tutorial 0x15C70 (table 0xA9A80)
        if (sub == 0) prologue_start(); else prologue_run();
        break;
    default:   // 0 (the Japanese warning): not ported, skipped
        if (log)
            log("attract: mode " + std::to_string(mode) + " not ported (skipped)");
        next_mode();
        return;
    }
    if (mem.at<int16_t>(G::game_mode) != 4)
        coin_prompts();
}

// 0x239A0 attract_start: music, the 2D layers cleared, sprite 10 (WINNERS DON'T USE DRUGS, USA) on layer 2, the
// group 0xAAFB0 (an empty camera task, input, players, shake), 360 frames
void Runtime::attract_start()
{
    sound_cue(0xA00002);
    sound_cue(0xA01356);
    heap_reset();
    sprites2d_ = false;   // sprite_init (0x170C0): the text layers cleared, the banks from scratch
    sprites_ensure();
    sprite_slots_mark();
    tilemap_clear_rect(kLayer0, 0, 0, 0x3E, 0x30);
    sprite_place(kLayer2, 0, 0, 0, 0, 10);
    mem.set32(kAttractGroup + 4, 360);
    mem.set32(kAttractGroup, task_group_create(0xAAFB0));
    hud_mode_init();
    mem.set32(G::game_submode, mem.u32(G::game_submode) + 1);
}

// 0x23A40 attract_run
void Runtime::attract_run()
{
    geo_frame_begin();
    task_group_run(mem.u32(kAttractGroup));
    if (mem.at<int16_t>(G::game_mode) != 1)
        return;
    const int32_t t = int32_t(mem.u32(kAttractGroup + 4)) - 1;
    mem.set32(kAttractGroup + 4, uint32_t(t));
    if (t == -1) {
        tilemap_clear_rect(kLayer2, 0, 0, 0x3E, 0x30);
        palette_fade_finish();
        next_mode();
    }
}

// 0xD100 title_start: (attract sound) the title music, the 2D layers cleared (the black cover 0xA1 on layer 2),
// texture area 1, sky 0 turning, floor 0, the title code cleared, the group 0xA95E0 (title camera, input, players,
// shake, sky, world: world_init takes the title's zone list in mode 2), the frame counter at 180
void Runtime::title_start()
{
    palette_fade_finish();
    if (mem.at<int16_t>(0x51F778) != 0) {
        sound_send3(0xA00002);   // (straight to the board, as the original's title_start / ranking_start)
        sound_send3(0xBE5000);
        sound_cue(0xA01000);
    }
    heap_reset();
    sprite_place(kLayer2, 1, 0, 0, 0, 0xA1);
    tilemap_clear_rect(kLayer0, 0, 0, 0x3E, 0x30);
    tilemap_clear_rect(0x1002000, 0, 0x20, 0x3E, 0x10);
    texture_area_load(1, -1);
    mem.set32(0x51F9B4, 1);
    mem.set32(kTitleGroup + 4, 0xB4);
    mem.at<uint8_t>(G::sky_index) = 0;
    mem.at<uint8_t>(G::sky_mode) = 1;
    mem.set32(G::floor_y, 0);
    sprite_slots_mark();
    for (uint32_t i = 0; i < 0x11; ++i)
        mem.at<uint8_t>(kTitleCode + i) = 0;
    mem.set32(kTitleGroup, task_group_create(0xA95E0));
    hud_mode_init();
    mem.set32(G::game_submode, mem.u32(G::game_submode) + 1);
}

// 0xD290: the group, then (still mode 2) title_code_screen 0xD2C0
void Runtime::title_run()
{
    geo_frame_begin();
    task_group_run(mem.u32(kTitleGroup));
    if (mem.at<int16_t>(G::game_mode) == 2) {
        title_input();   // scoredisp: the triggers' edges and the delayed trigger start (stand_ins.cpp)
        title_code_screen();
    }
}

// 0xD2C0 title_code_screen: the P1 / P2 gun triggers (bit 0 of the input blocks' pressed edges +4: 0x51F518 /
// 0x51F534, written by title_input, stand_ins.cpp) write L / R into the title code (the left / right gun: LRRLLR 6, RLRRLL 5,
// LRLLRR 4, LRLRLR 3, RRLRR 2, LLRLL 1, LLLRR 7 -> 0x51F42C: 1-6 the players' character pair (table 0xCA690, the
// cut-scene bodies), 7 the score display (hud_lives_draw's SCORE:%05d; tip RUNESA: "Left three times, Right twice,
// then Start"). The timeline: 0xB5 the banks, 0xB8 the logo, 0x10E the voice, 0x292 next_mode (the code back to 0).
// scoredisp (2026-10-26): the match was "the buffer is the start of a code" (one L gave 6); str_starts_with 0x5E480
// (code, buffer) returns 1 when every character of the code matches the buffer's (i960 disassembly), so only a whole
// code counts. MAME (tools/run_mame_title_code.ps1, hotd, MAME 0.289): L / LL / LLL / LLLR: 0, LLLRR: 7, kept into the
// game (SCORE:00000 at the top); LLRLL: 1
void Runtime::title_code_screen()
{
    int8_t& len = mem.at<int8_t>(kTitleCode + 0x10);
    if (len < 0xF) {
        const bool l = (mem.at<uint16_t>(0x51F518) & 1) != 0;   // P1's trigger (input block +4, pressed)
        const bool r = (mem.at<uint16_t>(G::cam_mode5_input) & 1) != 0;   // P2's (0x51F534)
        if (l || r) {   // (both in one frame: L, the original's branch order)
            mem.at<uint8_t>(kTitleCode + uint32_t(len)) = l ? 'L' : 'R';
            ++len;
            mem.at<uint8_t>(kTitleCode + uint32_t(len)) = 0;
        }
        static const struct { const char* code; int v; } codes[] = {{"LRRLLR", 6}, {"RLRRLL", 5}, {"LRLLRR", 4},
                                                                     {"LRLRLR", 3}, {"RRLRR", 2}, {"LLRLL", 1}, {"LLLRR", 7}};
        int v = 0;
        for (const auto& c : codes) {
            // str_starts_with(code, buffer): the buffer starts with the whole code (it may go on)
            bool match = true;
            for (uint32_t i = 0; c.code[i] != 0 && match; ++i)
                match = uint8_t(c.code[i]) == mem.at<uint8_t>(kTitleCode + i);
            if (match) { v = c.v; break; }
        }
        if ((l || r || v != int(mem.u32(0x51F42C))) && log)
            log("title code " + std::string(reinterpret_cast<const char*>(mem.ptr(kTitleCode))) + ": " + std::to_string(v) +
                (v == 7 ? " (the score display)" : ""));
        mem.set32(0x51F42C, uint32_t(v));
    }
    const uint32_t t = mem.u32(kTitleGroup + 4) + 1;
    mem.set32(kTitleGroup + 4, t);
    if (t == 0xB5) {   // title_code_screen_load 0xD610
        sprite_bank_load(0x46, 0x26);
        sprite_bank_load(0x47, 0x27);
        sprite_bank_load(0x3C, 0xD);
    } else if (t == 0xB8) {   // title_code_screen_draw 0xD550
        tilemap_clear_rect(kLayer2, 0, 0, 0x3E, 0x30);
        sprite_draw_tinted(kLayer0, 1, 0, 1, 3, 0x26);
        sprite_draw_tinted(kLayer2, 1, 0, 1, 3, 0x27);
        sprite_draw_anim(kLayer0, 1, 0, 0x17, 0x29, 0xD);
        sprite_draw_anim(kLayer0, 1, 0, 0x2E, 0x2B, 0x65);
    } else if (t == 0x10E) {
        sound_cue(0xA01403);
    } else if (t == 0x292) {
        mem.set32(0x51F42C, 0);
        next_mode();
    }
}

// title task 0 (0xD6A0 title_camera_start): the camera task, camera_globals_reset, path frame 1561
void Runtime::title_camera_start()
{
    task_create(0xD6D0, 0x20);
    camera_globals_reset();
    mem.set32(kCamFrame, fbits(1561.0f));
}

// 0xD6D0 title_camera_task: path 0x69 at the frame; after 180 frames the routine 0xD650 from frame 1563 on.
// 0xD650: the frame + 1 each frame (the slow push in after the logo)
void Runtime::title_camera_task(uint32_t task, bool moving)
{
    port::Game<A>::LocalScope scope(*this);
    const uint32_t loc = local_alloc(8);
    if (moving)
        mem.set32(kCamFrame, fbits(mem.f32(kCamFrame) + 1.0f));
    camera_path_eval(0x69, mem.u32(kCamFrame), G::camera_records, G::camera_records + 0x18, loc, loc + 4);
    camera_look_at(G::camera_records, G::camera_records + 0x18, mem.u32(loc));
    if (!moving && mem.u32(kTitleGroup + 4) > 0xB3) {
        mem.set32(task, 0xD650);
        mem.set32(kCamFrame, fbits(1563.0f));
    }
    camera_matrices_update();
}

// mode 3, the attract demo (mode3_demo 0x23B40, table 0xAB0D0). demo_list 0xAB0E0 {chapter, event, frames}: (0, 5,
// 1092), (0, 15, 1023), (1, 9, 1389); the third is Chapter 2 (not loaded by the teaser): skipped. The demo players
// start through player_demo_start (PlStartInit set-up 4) and play the recorded input (SCNINIT: input_source 1).
void Runtime::attract_demo()
{
    if (mem.u32(G::game_submode) == 0) {   // 0x23B60 demo_start
        uint32_t idx = mem.u32(G::demo_index);
        for (int tries = 0; tries < 8; ++tries) {
            if (int32_t(mem.u32(0xAB0E0 + idx * 12)) == -1)
                idx = 0;
            if (mem.u32(0xAB0E0 + idx * 12) == 0)
                break;
            ++idx;   // not Chapter 1: skipped (teaser)
        }
        mem.set32(G::demo_index, idx);
        mem.set32(0x53DE94, 1);
        const int event = int(mem.u32(0xAB0E4 + idx * 12));
        if (log)
            log("attract demo " + std::to_string(idx) + ": Chapter 1 event " + std::to_string(event));
        chapter_start(0, event, 0, 3);   // game_difficulty_set, chapter_load (mode 3, sub-mode 1)
        mem.set32(0x520714, 1);
        mem.set32(0x520718, 0);
        mem.set32(0x53DE90, mem.u32(0xAB0E8 + idx * 12));
        hud_mode_init();
        return;
    }
    // 0x23C00 demo_run
    game_frame(0x53E080);
    if (mem.at<int16_t>(G::game_mode) != 3)
        return;
    if (mem.u32(0x53DE94) == 0) {
        mem.set32(G::demo_index, mem.u32(G::demo_index) + 1);
        sprite_place(kLayer2, 1, 0, 0, 0, 0xA1);
        next_mode();
    }
}

// 0x15C90 mode9_story_start: the "1998.12.18" prologue = chapter slot 5 event 0 (its event list; the world, zone lists
// and placements are Chapter 1's: tables 0xC0BB0 / 0xC0BD0 slot 5), texture area 2, sky 0 turning, the scene
// state bytes 0x51E940..0x51EA3F cleared, the black cover, the caption banks 0x14-0x18 (sheets 0x38-0x3C), the
// characters' bodies, the group 0xA9A90 (evt_chapter_start, camera, input, players, shake, sky, world)
void Runtime::prologue_start()
{
    tilemap_clear_rect(kLayer0, 0, 0, 0x3E, 0x30);
    tilemap_clear_rect(0x1002000, 0, 0x20, 0x3E, 0x10);
    mem.at<int8_t>(G::event_index) = 0;
    mem.at<int8_t>(G::chapter) = 5;
    sound_send3(0xA00002);   // (straight to the board, as the original's mode9_story_start)
    sound_send3(0xBE5000);
    heap_reset();
    texture_area_load(2, -1);
    mem.set32(0x51F9B4, 2);
    mem.at<int16_t>(G::chapter_ended) = 0;
    mem.at<uint8_t>(G::sky_mode) = 1;
    mem.set32(0x5123B4, 0);
    mem.set32(G::floor_y, 0);
    mem.at<uint8_t>(G::sky_index) = 0;
    for (uint32_t a = 0x51E940; a < 0x51EA40; ++a)
        mem.at<uint8_t>(a) = 0;
    sprite_slots_mark();
    sprite_draw_anim(kLayer2, 1, 0, 0, 0, 0xA1);
    for (int b = 0; b < 5; ++b)   // mode9_sprites_load 0x16170
        sprite_bank_load(0x14 + b, 0x38 + b);
    player_char_models_set();
    // chapter_load's resets the prologue's group relies on (the attract's leftovers otherwise): camera, shot lists,
    // the chapter counters (event flags, scene state)
    mem.set32(0x51F260, 0);
    mem.set32(0x51F264, 0);
    for (uint32_t k = 0; k < 4; ++k)
        mem.set32(G::camera_pos + 4 * k, 0);
    shot_lists_clear();
    chapter_counters_reset();
    mem.set32(0x5123B0, task_group_create(0xA9A90));
    hud_mode_init();
    mem.set32(G::game_submode, mem.u32(G::game_submode) + 1);
}

// 0x15E50 mode9_story: the group (the event script), the frame counters; when the event list ends, next_mode
void Runtime::prologue_run()
{
    geo_frame_begin();
    task_group_run(mem.u32(0x5123B0));
    if (mem.at<int16_t>(G::game_mode) != 9)
        return;
    mem.set32(0x5123B4, mem.u32(0x5123B4) + 1);
    mem.set32(0x51F9D4, mem.u32(0x51F9D4) + 1);
    if (mem.u32(G::evt_gmtmf) != 0)
        mem.set32(G::evt_gmt, mem.u32(G::evt_gmt) + 1);
    if (mem.at<int16_t>(G::chapter_ended) != 0) {
        mem.at<int8_t>(G::chapter) = 0;
        mem.at<int16_t>(G::chapter_ended) = 0;   // (the game's chapter_load clears it in the original)
        if (prologueForGame_ != 0) {   // the PC's order: the chapter after the prologue
            mem.at<int16_t>(G::game_mode) = 4;
            mem.set32(G::game_submode, 0);
            attract_game_new();
            return;
        }
        next_mode();
    }
}

// the prologue's object routines (the event list's kinds and scenarios); run() sends them here. UNVERIFIED (teaser)
bool Runtime::prologue_routine(uint32_t routine, uint32_t o)
{
    using port::a::Obj;
    Obj& ob = mem.at<Obj>(o);
    auto sceneEnd = [&] {   // every scene routine: freed when the scene number changes
        if (ob.w2ec != int32_t(mem.at<int16_t>(G::scene_number))) {
            attack_slot_free(o);
            body_free_parts(o);
            task_exit();
        }
    };
    auto sceneInit = [&](uint32_t next) {
        body_init(o, int32_t(mem.u32(G::hit_type)), uint32_t(int32_t(ob.param)));
        ob.w2ec = mem.at<int16_t>(G::scene_number);
        ob.routine = next;
    };
    auto modelTimed = [&](uint32_t model) {   // kinds 113 / 114: a model at the object's place for +0x70 frames
        Tgp::Out none;
        copro(0x05, nullptr, 0, none);
        uint32_t t[3] = {mem.u32(o + 0x20), mem.u32(o + 0x24), mem.u32(o + 0x28)};
        copro(0x12, t, 3, none);
        geo_draw_model(model);
        copro(0x06, nullptr, 0, none);
        const int16_t c = ob.param;
        ob.param = int16_t(c - 1);
        if (c < 0)
            task_exit();
    };
    const float camFrame = mem.f32(kCamFrame);
    switch (routine) {
    case 0x15EE0: {   // sprite_scroll: the date / caption sprites (tables 0xA9AB0 / 0xA9AD0 by the frame), +0x70 frames
        const int16_t c = ob.param;
        ob.param = int16_t(c - 1);
        if (c < 0) {
            tilemap_clear_rect(kLayer2, 0, 0, 0x3E, 0x30);
            for (int b = 0x14; b <= 0x18; ++b)
                sprite_bank_free(b);
            palette_fade_finish();
            sprite_bank_load(0x27, 0x12A);
            task_exit();
        }
        const int32_t t = int32_t(mem.u32(0x5123B4));
        const bool first = t < 300 && (t > 0xA2 || t < 0x8A) && (t > 0x3C || t < 0x30);
        const uint32_t table = first ? 0xA9AB0 : 0xA9AD0;
        const int32_t f = int32_t(mem.u32(G::frame_counter));
        if (pcModels) {   // the PC sprites are whole pictures (pc_cells_put): the frame unclipped, every other frame
            if ((f & 1) == 0)
                sprite_draw_anim(kLayer2, 1, 0, 0, 0, mem.u32(table + uint32_t((f % 10) / 2) * 4));
            return true;
        }
        // the two halves of the screen on alternate frames (rows 0x18-0x2F, then 0-0x17)
        if ((f & 1) == 0)
            sprite_draw_clipped(kLayer2, 1, 0, 0, 0, 0, 0x3D, 0x18, 0x2F, mem.u32(table + uint32_t((f % 10) / 2) * 4));
        if (((f + 1) & 1) == 0)
            sprite_draw_clipped(kLayer2, 1, 0, 0, 0, 0, 0x3D, 0, 0x17, mem.u32(table + uint32_t(((f + 1) % 10) / 2) * 4));
        return true;
    }
    case 0x1E560: {   // kind104_white_fade: kind104_fade_start (0x19060), then kind104_wait
        // title-2: the fade from white (palette_blend_white 0x1A110: every colour = white - (white - colour) x level /
        // 32, level 0 .. 32 a step a frame, kind104_fade_task 0x18E30; one fade at a time: id 0x517CA0); the HUD
        // draws it as white over the picture with alpha (32 - level) / 32
        ob.param = 2;
        const uint32_t t = task_create(0x18E30, 0x34);
        const uint32_t id = mem.u32(0x517CA0) + 1;
        mem.set32(0x517CA0, id);
        mem.set32(t + 0x20, id);
        whiteFade = 0;
        mem.set32(0x517CA4, 1);
        if (pcModels)
            pcFadeStart(2.0f, 1.0f, 1000);   // the PC's kind 104 (0x448674 -> 0x41B228): the screen from white, 1000 ms
        ob.routine = 0x1EAE0;
        if (log)
            log("prologue: the white fade (kind 104)");
        return true;
    }
    case 0x18E30: {   // kind104_fade_task: one step a frame; ends after 32
        if (mem.u32(o + 0x20) != mem.u32(0x517CA0)) {
            task_exit();
            return true;
        }
        const int32_t level = int32_t(mem.u32(0x517CA4));
        if (level > 0x20) {
            whiteFade = -1;
            task_exit();
            return true;
        }
        whiteFade = level >= 0x20 ? -1 : level;
        mem.set32(0x517CA4, uint32_t(level + 1));
        return true;
    }
    case 0x1EAE0: {   // kind104_wait: after +0x70 frames the cover layer (0x1004000..0x10057FF) cleared
        const int16_t c = ob.param;
        ob.param = int16_t(c - 1);
        if (c == 0) {
            for (uint32_t a = 0x1004000; a < 0x1005800; a += 4)
                mem.set32(a, 0);
            pc_cells_clear(kLayer2, 0, 0, 0x40, 0x30);
            task_exit();
        }
        return true;
    }
    case 0x161B0: modelTimed(0xA71); return true;   // kind113_model_timed
    case 0x16210: modelTimed(0xCF3); return true;   // kind114_model_timed
    case 0x1E790:   // kind107_texture_area
        texture_area_load(uint32_t(int32_t(ob.param)), 0x8D);
        if (log)
            log("prologue: texture area " + std::to_string(ob.param));
        task_exit();
        return true;
    case 0x1E7D0: {   // kind108_zone_set: the zone lists / placements of slot +0x70 (the extra tiles are the engine's)
        const uint32_t s = uint32_t(int32_t(ob.param));
        mem.set32(0x5430B0, mem.u32(0xC0BB0 + s * 4));
        mem.set32(0x5430B4, mem.u32(0xC0BD0 + s * 4));
        if (log)
            log("prologue: zone slot " + std::to_string(s));
        task_exit();
        return true;
    }
    case 0x5C2A0: {   // kind109_door_pair: the double door to0a / to0b (0x73A / 0x73B) at (+-15, 0, -73), scale 10
        // attdoor (2026-10-31): the disassembly (0x5C2BC..0x5C358) sends a translate (0x09001212: 15 / -15, 0, -73)
        // before each wing's scale; the typed decompilation lost those two stq's, so the doors were drawn at the world's
        // origin (behind the camera: the prologue's Sophie stood in front of an empty doorway)
        Tgp::Out none;
        for (uint32_t model : {0x73Au, 0x73Bu}) {
            copro(0x05, nullptr, 0, none);
            uint32_t t[3] = {fbits(model == 0x73A ? 15.0f : -15.0f), 0, fbits(-73.0f)};
            copro(0x12, t, 3, none);
            uint32_t s[3] = {fbits(10.0f), fbits(10.0f), fbits(10.0f)};
            copro(0x13, s, 3, none);
            geo_draw_model(model);
            copro(0x06, nullptr, 0, none);
        }
        const int16_t c = ob.param;
        ob.param = int16_t(c - 1);
        if (c < 0)
            task_exit();
        return true;
    }
    case 0x58A60: sceneInit(0x58AC0); return true;   // scene_person
    case 0x58AC0:   // scene_person_frame
        if (mem.u32(G::palette_grey_on) == 0) enemy_frame_end(o); else object_draw(o);
        sceneEnd();
        return true;
    case 0x58B40: sceneInit(0x58B90); return true;   // scene_tom
    case 0x58B90:   // Tom_Adv_00_0: at camera frame 211 his motion 0x1D0
        ob.draw_param = 0;
        enemy_frame_end(o);
        if (camFrame >= 211.0f) {
            set_motion_blend(o, 0x1D0, 0, 0x18);
            ob.routine = 0x5C600;
        }
        if (ob.frame == 1 && ((ob.motion_flags2 >> 1) & 1) == 0) {
            // 0x1EA30 body_parts_mark_role (captor, role from table 0x58B70 by the sub-frame): the zombie Tom shoots
            // has the parts of that role marked shot / blown off (0x102); its own routine then reacts (the hit voice
            // 0xA0134B and the set's sound). UNVERIFIED (teaser): read from the decompilation, no capture proof
            const uint32_t captor = ob.captor.a;
            if (captor)
                body_parts_mark_role(captor, int32_t(mem.u32(0x58B70 + uint32_t(int32_t(ob.frame_sub)) * 4)));
            sound_cue(0xA01208);
        }
        sceneEnd();
        return true;
    case 0x5C600:   // Tom_Adv_00_0_n
        enemy_frame_end(o);
        if ((ob.motion_flags >> 4) & 1) {
            ob.routine = 0x5C5C0;
            ob.anim_time -= 1;
        }
        sceneEnd();
        return true;
    case 0x5C5C0:   // Tom_Adv_00_0_n_n
    case 0x5C650:   // scene_person_once_idle
        shadow_setup(mem.f32(G::floor_y));
        object_draw(o);
        sceneEnd();
        return true;
    case 0x595E0: sceneInit(0x59610); return true;   // scene_person_once
    case 0x59610:   // scene_person_once_frame
        enemy_frame_end(o);
        if ((ob.motion_flags >> 4) & 1) {
            ob.routine = 0x5C650;
            ob.anim_time -= 1;
        }
        sceneEnd();
        return true;
    case 0x59DD0:   // scene_person_talk
        sceneInit(0x59E10);
        ob.timer = 0;
        return true;
    case 0x59E10:   // scene_person_talk_frame: the head models 0x1CEF.. talking until camera frame 185, 0x1CFE.. later
        if (camFrame >= 185.0f) {
            if (camFrame >= 235.0f && camFrame < 250.0f)
                body_role_model_set(o, 1, uint32_t(int32_t((camFrame - 235.0f) / 3.0f + 7422.0f)));
        } else {
            body_role_model_set(o, 1, uint32_t((ob.timer % 0x5A) / 6 + 0x1CEF));
            ob.timer += 1;
        }
        enemy_frame_end(o);
        sceneEnd();
        return true;
    case 0x59EF0: sceneInit(0x59F20); return true;   // scene_zombie_walk
    case 0x59F20:   // scene_zombie_walk_frame: at the motion's end z_walk_b (0x284)
        enemy_frame_end(o);
        if ((ob.motion_flags >> 4) & 1) {
            set_motion_at_frame(o, 0x284, 0, 0x20);
            ob.routine = 0x5C690;
        }
        sceneEnd();
        return true;
    case 0x5C690:   // scene_zombie_walk_end
        enemy_frame_end(o);
        sceneEnd();
        return true;
    default:
        return lesson_routine(routine, o);
    }
}

// mode 8, the how-to-play demo (mode8 0x26590, table 0xAB280): chapter slot 4 event 0 (Chapter 1's world, the
// courtyard) for up to 3600 frames (0x53E0C4; the lesson zombie ends it), the recorded input 0xC5BC0 (SCNINIT), its
// own HUD (lives 0x53E0CE, bullets 0x53E0CF), the lesson zombie kind 130
void Runtime::how_to_play()
{
    if (mem.u32(G::game_submode) == 0) {   // 0x265B0 mode8_start
        palette_fade_finish();
        if (log)
            log("attract: how to play (chapter slot 4)");
        chapter_start(4, 0, 0, 8);   // chapter_load (texture area 1 first: table 0xAB0B0 slot 4)
        mem.at<int16_t>(0x51F492) = mem.at<int8_t>(0xAB27A);   // game_difficulty_set: the demo's fixed entry
        mem.set32(0x53E0C4, 0xE10);
        mem.at<uint8_t>(0x53E0C8) = 0;
        mem.at<int16_t>(0x53E0CA) = 0;
        mem.at<int16_t>(0x53E0CC) = 0;
        mem.set32(0x520714, 1);
        mem.set32(0x520718, 0);
        mem.at<int8_t>(0x53E0CF) = 6;
        mem.at<int8_t>(0x53E0CE) = mem.at<int8_t>(G::max_lives);
        for (uint32_t a = G::player_bodies; a < 0x51F480; a += 4)
            mem.set32(a, 0);
        mem.set32(G::demo_input_advance, 0);
        tilemap_clear_rect(kLayer0, 0, 0, 0x3E, 0x30);
        sprite_place(kLayer2, 1, 0, 0, 0, 0xA1);
        // mode8_sprites_load 0x27150
        static const int banks[][2] = {{0x1A, 0xE}, {0x1F, 0x14}, {0x1E, 0xB7}, {0x20, 0x10}, {4, 0x28}, {0x3E, 0x83},
                                       {0x51, 0x268}, {0x20, 0x10}, {0x37, 0x36}, {0x38, 0x37}, {0x4F, 0x26D}};
        for (const auto& b : banks)
            sprite_bank_load(b[0], b[1]);
        task_create(0x26FB0, 0x58);   // mode8_lives_task_start
        mode8_ammo_draw();
        hud_mode_init();
        return;
    }
    // 0x26740 mode8_frame
    game_frame(0x53E080);
    if (mem.at<int16_t>(G::game_mode) != 8)
        return;
    if (mem.at<uint8_t>(0x512080) == 1)
        sprite_draw_anim(kLayer0, 1, 0, 2, 0x2A, 0xE);   // mode8_hud_frame_draw
    if (mem.u32(0x53E0C4) == 0) {
        sprite_place(kLayer2, 1, 0, 0, 0, 0xA1);
        tilemap_clear_rect(kLayer0, 0, 0, 0x3E, 0x30);
        for (int b : {0x1A, 0x1F, 0x1E, 0x20, 4, 0x3E, 0x51, 0x20, 0x37, 0x38, 0x4F})
            sprite_bank_free(b);
        next_mode();
    }
}

// 0x270D0 mode8_ammo_draw: the bullets
void Runtime::mode8_ammo_draw()
{
    for (int i = 0; i < mem.at<int8_t>(0x53E0CF); ++i)
        sprite_draw_anim(kLayer0, 1, 0, 2 + 2 * i, 0x26, 0x10);
}

// kind 130 and mode 8's tasks (run() through prologue_routine)
bool Runtime::lesson_routine(uint32_t routine, uint32_t o)
{
    using port::a::Obj;
    Obj& ob = mem.at<Obj>(o);
    const uint32_t region = text_region();
    auto byRegion = [&](uint32_t table) { return uint32_t(int32_t(mem.at<int16_t>(table + region * 2))); };
    int8_t& stage = mem.at<int8_t>(0x53E0C8);
    int16_t& count = mem.at<int16_t>(0x53E0CA);
    int16_t& count2 = mem.at<int16_t>(0x53E0CC);
    int8_t& lives = mem.at<int8_t>(0x53E0CE);
    int8_t& bullets = mem.at<int8_t>(0x53E0CF);
    auto spriteClear = [&] { sprite_draw(kLayer0, int(bullets) * 2, 0x26, 0x10); };   // kind130_sprite_clear 0x27120
    switch (routine) {
    case 0x26FB0: {   // mode8_lives_task: the demo player's torches, flames (each its phase from 0xAB2F0) and the
                      // lamps: the base 0x14's lower row only (clipped to row 0x2C, x from 0xAB2E4; read from the
                      // disassembly: the flames' 2 rows cover row 0x2B)
        for (int i = 0; i < lives; ++i)
            sprite_draw_anim(kLayer0, 1, 0, 7 + 4 * i, 0x2A,
                             ((mem.u32(0xAB2F0 + uint32_t(i) * 4) + mem.u32(G::frame_counter)) / 6) % 7 + 0xB7);
        for (int i = 0; i < lives; ++i) {
            const int x = int(mem.u32(0xAB2E4)) + 4 * i;
            sprite_draw_clipped(kLayer0, 1, 0, x, 0x2B, x, x + 2, 0x2C, 0x2C, 0x14);
        }
        return true;
    }
    case 0x27300: {   // kind130_prompt_task: the torch going out (blinking), then cleared
        const int16_t c = count2;
        count2 = int16_t(c + 1);
        const int x = int(lives) * 4 + 6;
        if (c < 0x29) {
            sprite_draw_anim(kLayer0, 1, 0, x, 0x2A, uint32_t((int(count2) / 6) % 8 + 0xBE));
            return true;
        }
        sprite_draw(kLayer0, x, 0x2A, 0xBE);
        sprite_draw(kLayer0, x, 0x2B, 0x14);
        task_exit();
        return true;
    }
    case 0x26EC0:   // kind130_lesson_zombie
        mem.set32(G::hit_type, uint32_t(int32_t(ob.type)));
        body_init(o, int32_t(ob.type), 0x266);
        creature_type_setup(o);
        ob.draw_param = 1;
        ob.draw_mode = 0;
        ob.anim_time = 0x28;
        mem.at<int16_t>(G::enemy_count) = int16_t(mem.at<int16_t>(G::enemy_count) + 1);
        ob.routine = 0x26830;
        return true;
    case 0x26830:   // kind130_lesson
        break;
    case 0x267C0:   // kind129_lesson_player: the demo player's character body
        mem.set32(G::hit_type, uint32_t(int32_t(ob.type)));
        body_init(o, int32_t(ob.type), 0x298);
        creature_type_setup(o);
        ob.draw_mode = 0;
        ob.draw_param = 1;
        ob.motion_flags2 = uint8_t(ob.motion_flags2 & 0xFE);
        ob.motion_flags = uint8_t(ob.motion_flags & 0xF7);
        mem.at<int16_t>(G::enemy_count) = int16_t(mem.at<int16_t>(G::enemy_count) + 1);
        ob.routine = 0x27240;
        return true;
    case 0x27240: {   // kind129_lesson_player_frame: walks in; at x <= 90 the lesson's step 1; at x <= 80 (step 3) gone
        const float x = mem.f32(o + 0x20);
        bool walk = false;
        if (stage == 0) {
            if (x <= 90.0f) stage = 1; else walk = true;
        } else if (stage == 3) {
            if (x <= 80.0f) {
                mem.at<int16_t>(G::enemy_count) = int16_t(mem.at<int16_t>(G::enemy_count) - 1);
                body_free_parts(o);
                task_exit();
            }
            walk = true;
        }
        if (walk)
            ob.anim_time += 1;
        mem.set32(o + 0x24, fbits(-27.4f));
        shadow_setup(-26.4f);
        object_draw(o);
        return true;
    }
    default:
        return ranking_routine(routine, o);
    }
    // 0x26830 kind130_lesson: the lesson script
    mem.at<int8_t>(G::hit_class) = 0;
    mem.at<int16_t>(G::hit_damage) = 0;
    enemy_part_hits(o);
    const int hc = mem.at<int8_t>(G::hit_class);
    if (hc != 0 && ob.motion != 0x230) {
        uint32_t m;
        if (hc % 9 == 3)
            m = 0x24F;
        else if (hc % 9 == 4)
            m = 0x24E;
        else
            m = mem.u32(mem.u32(G::hit_motions_by_type + mem.u32(G::hit_type) * 4) + uint32_t((hc % 9) * 8 + (hc / 9) * 4));
        set_motion_blend(o, m, 0, 4);
    }
    const float camFrame = mem.f32(kCamFrame);
    switch (stage) {
    case 0:
        ob.anim_time += 1;
        break;
    case 1: {
        const bool first = count == 0;
        count = int16_t(count + 1);
        if (first) {   // kind130_life_lost_draw 0x26F20: the title sprites
            sprite_draw_anim(kLayer2, 1, 0, 1, 6, byRegion(0xAB290));
            sprite_draw_anim(kLayer2, 1, 0, 0x17, 0x27, byRegion(0xAB29A));
        }
        if (count > 0xB3) {
            set_motion_blend(o, 0x25D, 8, 8);
            sprite_draw(kLayer2, 1, 6, int(byRegion(0xAB290)));
            sprite_draw(kLayer2, 0x17, 0x27, int(byRegion(0xAB29A)));
            stage = 3;
            count = count2 = 0;
        }
        break;
    }
    case 3:
        if (ob.motion != 0x260 || ((ob.motion_flags >> 4) & 1) == 0)
            ob.anim_time += 1;
        if (int(camFrame) == 0x96 && ob.motion == 0x25D)
            set_motion_blend(o, 0x260, 8, 8);
        if (ob.motion == 0x260 && ob.frame == 0x3F) {   // the axe hits the demo player
            mem.at<Player>(G::players, 0).hit = 1;
            lives = int8_t(lives - 1);
            player_hit(0, 1, 3);
            task_create(0x27300, 0x58);   // kind130_prompt_task_start
        }
        {
            const bool at = count == 0x11A;
            count = int16_t(count + 1);
            if (at)
                sprite_draw_anim(kLayer2, 1, 0, 0x18, 0x26, byRegion(0xAB2A4));
        }
        if (count > 0x1C1) {
            sprite_draw(kLayer2, 0x18, 0x26, int(byRegion(0xAB2A4)));
            mem.set32(G::demo_input_advance, 1);
            stage = 4;
            count = count2 = 0;
        }
        break;
    case 4:
        if (mem.at<Player>(G::players, 0).pressed & 1) {
            spriteClear();
            bullets = int8_t(bullets - 1);
        }
        if (ob.motion != 0x260)
            ob.anim_time += 1;
        if (bullets == 0) {
            count2 = int16_t(count2 + 1);
            mem.set32(G::demo_input_advance, 0);
            if (count2 > 0x1E) {
                count = count2 = 0;
                sprite_draw_anim(kLayer2, 1, 0, 0x16, 5, byRegion(0xAB2AE));
                stage = 5;
            }
        }
        break;
    case 5: {
        const int16_t c = count;
        count = int16_t(c + 1);
        if (c < 0xF0) {
            if (int(count) % 0x3C < 0x2D) {   // the gun off screen, blinking; RELOAD after 120 frames
                sprite_draw_anim(kLayer2, 1, 0, 1, 0x14, 0x36);
                if (count > 0x77)
                    sprite_draw_anim(kLayer2, 1, 0, 1, 0x1B, byRegion(0xAB2D0));
            } else {
                sprite_draw(kLayer2, 1, 0x14, 0x36);
            }
        } else if (count == 0xF1) {   // reloaded: 6 bullets
            sprite_draw(kLayer2, 0x16, 5, int(byRegion(0xAB2AE)));
            sprite_draw_anim(kLayer2, 1, 0, 0x16, 3, byRegion(0xAB2B8));
            bullets = 6;
            mode8_ammo_draw();
            sprite_draw(kLayer2, 1, 0x14, 0x36);
            sprite_draw(kLayer2, 1, 0x1B, int(byRegion(0xAB2D0)));
        }
        if (count == 0x1E0) {
            sprite_draw(kLayer2, 0x16, 3, int(byRegion(0xAB2B8)));
            mem.set32(G::demo_input_advance, 1);
            stage = 6;
            count = count2 = 0;
        }
        break;
    }
    case 6:
        if (mem.at<Player>(G::players, 0).pressed & 1) {
            spriteClear();
            bullets = int8_t(bullets - 1);
            count2 = int16_t(count2 + 1);
        }
        if (bullets < 6 && count2 != 4)
            ob.anim_time += 1;
        if (count2 == 2) {
            mem.set32(G::demo_input_advance, 0);
            set_motion_blend(o, 0x233, 8, 8);
            count2 = 3;
        }
        if (ob.motion == 0x233 && ob.frame == 0xA0)
            count2 = 4;
        if (count2 == 4) {
            count = int16_t(count + 1);
            if (count > 0x3B) {   // dead: mode 8's timer ends
                body_free_parts(o);
                mem.at<int16_t>(G::enemy_count) = int16_t(mem.at<int16_t>(G::enemy_count) - 1);
                mem.set32(0x53E0C4, 0);
                task_exit();
            }
        }
        break;
    default:
        break;
    }
    shadow_setup(-26.4f);   // 0xC1D33333
    object_draw(o);
    depth_list_add(o);
    return true;
}

// modes 6 / 7, the ranking (mode67_ranking 0x21D90, table 0xAA460): texture area 5, sky 2, the room model
// PN_room2_01a (0x123B) under a rising camera, the header sprites 0x120-0x123 and the top 10 of backup RAM 0x1D00104
// (12 bytes an entry: name, chapter reached, score) five at a time, 240 frames a page. The backup RAM holds the
// defaults (backup_ranking_clear 0x225D0: ROM 0xAA4A0); the name entry after a game is not ported.
void Runtime::ranking()
{
    ranking_backup_ensure();   // the backup RAM's ranking block (title-2: name_entry.cpp, kept in a file)
    if (mem.u32(G::game_submode) == 0) {   // 0x21DB0 ranking_start
        palette_fade_finish();
        sound_send3(0xA00002);   // (straight to the board, as the original's title_start / ranking_start)
        sound_send3(0xBE5000);
        heap_reset();
        sprite_place(kLayer2, 1, 0, 0, 0, 0xA1);
        tilemap_clear_rect(kLayer0, 0, 0, 0x3E, 0x30);
        tilemap_clear_rect(0x1002000, 0, 0x20, 0x3E, 0x10);
        texture_area_load(5, -1);
        mem.set32(0x51F9B4, 5);
        sprite_slots_mark();
        mem.at<uint8_t>(G::sky_index) = 2;
        mem.at<uint8_t>(G::sky_mode) = 1;
        color_row_set(9, 0x76);
        mem.set32(0x51E8B4, 0xF0);
        mem.at<int8_t>(0x51E8B8) = 0;
        mem.set32(0x51E8B0, task_group_create(0xAA470));
        hud_mode_init();
        mem.set32(G::game_submode, 1);
        return;
    }
    // 0x21EC0 ranking_run
    geo_frame_begin();
    task_group_run(mem.u32(0x51E8B0));
    const int16_t mode = mem.at<int16_t>(G::game_mode);
    if (mode >= 6 && mode < 8 && mem.at<int8_t>(0x51E8B8) == 10) {
        for (int b : {0x33, 0x34, 0x35, 0x36, 0x48, 0x49, 0x50, 0x53})   // ranking_sprites_free 0x22360
            sprite_bank_free(b);
        sprite_place(kLayer2, 1, 0, 0, 0, 0xA1);
        next_mode();
    }
}

// the ranking's tasks (run() through prologue_routine)
bool Runtime::ranking_routine(uint32_t routine, uint32_t task)
{
    auto charDraw = [&](int x, int y, uint32_t c) {   // 0x224A0 ranking_char_draw: the sprite font (table 0x22330)
        sprite_draw_anim(kLayer2, 1, 0, x, y, mem.u32(0x22330 + (c & 0xFF) * 4));
    };
    auto textDraw = [&](int x, int y, uint32_t s) {   // 0x224D0 ranking_text_draw
        for (uint8_t c; (c = mem.at<uint8_t>(s)) != 0; ++s) {
            if (c > 0x20 && c < 0x5B)
                sprite_draw_anim(kLayer2, 1, 0, x, y, mem.u32(0x22330 + uint32_t(c) * 4));
            x += c == 0x20 ? 2 : 3;
        }
    };
    switch (routine) {
    case 0x21F40:   // ranking_camera_start
        task_create(0x21F30, 0x20);
        camera_globals_reset();
        return true;
    case 0x21F30:   // ranking_camera_task
        camera_matrices_update();
        return true;
    case 0x222B0:   // ranking_room_start: the room task, the camera's height
        task_create(0x22760, 0x58);
        mem.set32(G::camera_records + 4, fbits(-175.0f));
        mem.set32(G::camera_records + 8, fbits(-160.0f));
        return true;
    case 0x22760: {   // ranking_room_task: the camera rises 0.4 a frame; PN_room2_01a at the origin
        mem.set32(G::camera_records + 4, fbits(mem.f32(G::camera_records + 4) + 0.4f));
        Tgp::Out none;
        copro(0x05, nullptr, 0, none);
        uint32_t t[3] = {0, 0, 0};
        copro(0x12, t, 3, none);
        geo_draw_model(0x123B);
        copro(0x06, nullptr, 0, none);
        return true;
    }
    case 0x222F0: {   // ranking_sprites_load
        static const int banks[][2] = {{0x33, 0x120}, {0x34, 0x121}, {0x35, 0x122}, {0x36, 0x123},
                                       {0x48, 0x18B}, {0x49, 0x195}, {0x50, 0x231}, {0x53, 0x22C}};
        for (const auto& b : banks)
            sprite_bank_load(b[0], b[1]);
        return true;
    }
    case 0x22020:   // ranking_header_start
        mem.set32(task_create(0x21F60, 0x58) + 0x50, 0);
        return true;
    case 0x21F60: {   // ranking_header_task: after 4 frames the cover off, the title and the column headers
        const uint32_t n = mem.u32(task + 0x50) + 1;
        mem.set32(task + 0x50, n);
        if (n != 4)
            return true;
        tilemap_clear_rect(kLayer2, 0, 0, 0x3E, 0x30);
        sprite_draw_anim(kLayer0, 1, 0, 0x12, 1, 0x120);
        sprite_draw_anim(kLayer0, 1, 0, 6, 8, 0x121);
        sprite_draw_anim(kLayer0, 1, 0, 0x20, 8, 0x122);
        sprite_draw_anim(kLayer0, 1, 0, 0x2F, 8, 0x123);
        // pcrank: the PC files have no room PN_room2_01a (0x123B): with the PC models the camera saw the sky dome's
        // lower edge and black behind the rows. The PC's own high-score screen (THOTD.EXE screen_high_scores 0x423038)
        // and its name entry (0x4B3E0C, sprite_draw_anim(1, ..., 0xD2)) stand on the desk picture 0xD2, the arcade's
        // name-entry picture 0x11F: placed under the table (only in the PC 2D layers, below every table sprite)
        if (pcModels) {
            const uint32_t lay = sheet_layout(0x11F);
            pc_cells_put(kLayer2, 0, 0, 0x11F, mem.at<uint16_t>(lay), mem.at<uint16_t>(lay + 2));
        }
        task_exit();
        return true;
    }
    case 0x22290:   // ranking_table_start
        task_create(0x22040, 0x58);
        return true;
    case 0x22040: {   // ranking_table_task
        int32_t t = int32_t(mem.u32(0x51E8B4)) - 1;
        mem.set32(0x51E8B4, uint32_t(t));
        const int page = mem.at<int8_t>(0x51E8B8);
        if (t == 0xEB) {
            for (int k = 4; k >= 0; --k) {   // bottom row first (the rank bars reach up into the rows above)
                const uint32_t e = 0x1D00104 + uint32_t(page + k) * 12;
                const int y = k * 7 + 0xC;
                if (page + k < 9)
                    charDraw(2, y, uint32_t(page + k + 0x31));
                else
                    textDraw(0, y, 0x22038);   // "10"
                const uint8_t chap = mem.at<uint8_t>(e + 4);
                sprite_draw_anim(kLayer2, 1, 0, 5, mem.at<int8_t>(0xAA518 + chap) + y, 0x231u + chap);
                textDraw(0x22, y, e);
                const uint32_t score = mem.u32(e + 8);
                if (score > 9999) charDraw(0x2D, y, score / 10000 + 0x30);
                if (score > 999) charDraw(0x30, y, (score % 10000) / 1000 + 0x30);
                if (score > 99) charDraw(0x33, y, (score % 1000) / 100 + 0x30);
                if (score > 9) charDraw(0x36, y, (score % 100) / 10 + 0x30);
                charDraw(0x39, y, score % 10 + 0x30);
            }
            return true;
        }
        if (t == 0) {
            tilemap_clear_rect(kLayer2, 0, 0xB, 0x3E, 0x25);
            mem.set32(0x51E8B4, 0xF0);
            mem.at<int8_t>(0x51E8B8) = int8_t(page + 5);
            if (page == 5)
                task_exit();
        }
        return true;
    }
    default:
        return name_entry_routine(routine, task);
    }
}

// 0x57F0 player_lesson_start: PlStartInit set-up 5 (the how-to-play player)
void Runtime::player_lesson_start(uint32_t task)
{
    PlStartInit(task, 5);
}

// 0x57E0 player_demo_start: PlStartInit set-up 4 (the demo player)
void Runtime::player_demo_start(uint32_t task)
{
    PlStartInit(task, 4);
}

// mode 4 sub-mode 0 after a start in the attract (game_new 0x25C40 -> chapter_load): Chapter 1 from event 0 with the
// players who pressed start (state 0 from player_start_check)
void Runtime::attract_game_new()
{
    int players = 0;
    for (uint32_t p = 0; p < 2; ++p)
        if (mem.at<Player>(G::players, p).state != 9)
            ++players;
    if (players == 0)
        players = 1;
    // GitHub #29: player 2's start alone plays in player 2's seat, as the arcade (was: player 1's seat always)
    const int seat = players == 1 && mem.at<Player>(G::players, 0).state == 9 && mem.at<Player>(G::players, 1).state != 9 ? 1 : 0;
    if (prologueForGame_ == 0)
        prologuePlayers_ = players, prologueSeat_ = seat;
    // PC models: the PC release plays the prologue after the start, before the chapter card (PC 0x43BB28 =
    // set_mode(9)); a start during it skips to the chapter
    if (pcModels && prologueForGame_ == 0 && modeBeforeSet_ != 9) {   // (not when it was just playing)
        if (log)
            log("attract: start pressed, the prologue first (the PC's order)");
        prologueForGame_ = 1;
        mem.at<int16_t>(G::game_mode) = 9;
        mem.set32(G::game_submode, 0);
        mem.at<Player>(G::players, 0).state = 9;
        mem.at<Player>(G::players, 1).state = 9;
        prologue_start();
        return;
    }
    players = prologuePlayers_;
    soloSeat = players == 1 ? prologueSeat_ : 0;
    prologueForGame_ = 0;
    if (log)
        log("attract: start pressed, Chapter 1 with " + std::to_string(players) + " player(s)" + (soloSeat ? " (player 2's seat)" : ""));
    attract = false;
    textureArea = -1;
    for (uint32_t p = 0; p < 2; ++p)   // the demo players' scores (a player joining later starts from 0)
        mem.at<Player>(G::players, p).score = 0;
    chapter_start(0, 0, players);
}

} // namespace hotd::game
