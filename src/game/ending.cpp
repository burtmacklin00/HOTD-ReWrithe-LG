// The game's ending after Chapter 4: the runtime's side. The ending's routines (game_ending_start 0x35160,
// game_ending 0x352A0, the ending's kinds and the characters' bodies) are the port's (src/port/ending.cpp, moved there
// for their capture proof by worker "verify"); the runtime gives them its own forms of the 2D set-up and the ranking.
#include "game/runtime.h"

namespace hotd::game {

// 0x170C0 sprite_init (game_ending_start's): the 2D RAMs mapped and reloaded (the runtime's sprite layer). The
// original's sprite_init does not run hud_mode_init 0x205A0, which sprites_ensure's chapter part does: its words
// (coin_update's counter 0x51F560, the prompts' 0x51E8A0 / 0x51E8A8) are kept (arcfix2: the whole-game differential's
// two-player run, the counter reset at the ending's start, MAME frame 21488)
void Runtime::sprite_init()
{
    const uint32_t counter = mem.u32(0x51F560), p0 = mem.u32(0x51E8A0), p1 = mem.u32(0x51E8A8);
    sprites2d_ = false;
    sprites_ensure();
    mem.set32(0x51F560, counter);
    mem.set32(0x51E8A0, p0);
    mem.set32(0x51E8A8, p1);
    // bugs1 (#34): the cut-scene bars are layer 2 sprites in the original (letterbox_task), gone with the 2D RAMs here,
    // and the chapter's letterbox task with the heap: the ending (the farewell, the staff roll, the ranking) has none
    // (MAME fullgame_us snaps 0723 -> 0724: the blue sky with bars, Rogan's scene without). The HUD's bars stayed up
    letterbox = 0;
}

// game_ending_start 0x35160 (the port's), logged
void Runtime::game_ending_start()
{
    RuntimeBounds::game_ending_start();
    if (log)
        log("the ending: event " + std::to_string(int(mem.at<int8_t>(G::event_index))));
}

// game_ending's tail (0x35360): the runtime's ranking flow (name_entry.cpp game_ending_rank: rank_insert, the name
// entry or the ranking; PC Mode's table, 4P's)
void Runtime::game_ending_finish()
{
    if (log)
        log("the ending is over");
    game_ending_rank();
}

} // namespace hotd::game
