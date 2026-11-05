// Game flow (docs/runtime.md "Continue and game over"): mode 4's sub-modes 2-4 (play, continue begin, continue
// countdown), the players' out-of-lives / continue / game over states, and mode 5 (game over). Read from the typed
// decompilation (0x25F40, 0x26080, 0x26100, 0x5230..0x5540, 0x971E0..0x97430); UNVERIFIED (teaser). The sprites
// they place (CONTINUE, the digits, GAME OVER) are the HUD's (src/game/hud.cpp), which reads the same state. Mode 5's
// sub-modes (0x97200, 0x97340, 0x5CB50, 0x5CBF0, 0x973A0, 0x97430), the game-over states 0x5470 / 0x5540, the scene's
// camera 0x97490 and the route map's tasks 0x5CCD0 / 0x5CF50 are the port's (src/port/game_over.cpp), capture-proven
// (proofs-5); the rest here is UNVERIFIED.
#include "game/audio.h"
#include "game/runtime.h"

namespace hotd::game {

using G = A::G;
using port::a::Player;
using port::a::PlayerTask;

namespace {
constexpr uint32_t kStateTable = 0xA8790;     // per player state: start routine, "out" byte (+4), "can start" (+5)
constexpr uint32_t kContinueStart = 0x52C0;
constexpr uint32_t kContinueFrame = 0x5300;
constexpr uint32_t kCredits = 0x51F4B8;       // 16 bytes: the coin / credit counters the countdowns watch
constexpr uint32_t kCountdown = 0x52071C;     // both players out: the shared continue countdown (0x9FFF = 9)
constexpr uint32_t kCreditsSaved = 0x53E090;

// 0x18680 palette_fade_finish, called by game_over_start / game_over_text when a fade runs (0x51F702): full
// brightness (0x517CA4 = 0x20), a palette upload requested, the fade state cleared. The runtime has no palette fade
// (0x51F702 stays 0), so this only keeps the globals as the original leaves them. gameover-2, UNVERIFIED (teaser)
template <class M> void palette_fade_finish_if_running(M& mem)
{
    if (mem.template at<uint16_t>(0x51F702) == 0)
        return;
    mem.set32(0x517CA4, 0x20);
    mem.template at<uint8_t>(0x51E8C4) = 1;
    mem.template at<uint16_t>(0x51F706) = 0;   // 1 in the original, cleared by the next vblank (the busy wait)
    mem.template at<uint16_t>(0x51F702) = 0;
    mem.set32(0x517CA8, 0);
    mem.template at<uint16_t>(0x51F704) = 0;
    mem.set32(0x517CAC, 0);
    mem.template at<uint16_t>(0x51F9EE) = 0;
}
} // namespace

// 0x154D0 sound_cue: the command goes into the 16-entry queue (0x512360, count 0x512344 below 15, write index
// 0x51234C, 0x512350 cleared); the drain sends one queued command a frame to the board (MAME 0.289 attract: several
// cues of one frame reach the i8251 a frame apart). With the PC sound the cue plays at once instead, as THOTD's own
// sound_cue (0x412000) does. (The original's gate outside modes 4 / 5, the attract-sound setting 0x51F9C1, is not
// modelled: the attract sound is on.) UNVERIFIED (teaser)
void Runtime::sound_cue(uint32_t cue)
{
    if (boss_next_on())   // Boss Mode's undrawn walk-up: THOTD.EXE's sound_cue 0x412000 plays nothing while pc_boss_next
        return;
    if (survival.active() && survival_cue(cue))   // Survival Mode: its silent lead-in, the SMG's own sound (survival.cpp)
        return;
    if (!audio || !audio->boardOn()) {
        if (cueLog)
            cueLog->push_back(cue);
        if (audio)
            audio->cue(cue);
        else if (silentCues)
            silentCues->emplace_back(frameCount, cue);
        return;
    }
    const uint32_t n = mem.u32(0x512344);
    if (int32_t(n) < 0xF) {
        mem.set32(0x512344, n + 1);
        const uint32_t w = mem.u32(0x51234C);
        mem.set32(0x512360 + (w & 0xF) * 4, cue);
        mem.set32(0x51234C, (w + 1) & 0xF);
    }
    mem.set32(0x512350, 0);
}

// 0x15590 sound_send3: straight to the board, past the queue
void Runtime::sound_send3(uint32_t v)
{
    if (boss_next_on() || survival.leadIn())   // (as sound_cue: the PC has one sound_cue; Survival's lead-in too)
        return;
    if (cueLog)
        cueLog->push_back(v);
    if (audio)
        audio->cue(v);
    else if (silentCues)
        silentCues->emplace_back(frameCount, v);
}

// 0x15630 sound_queue_reset: the queue emptied (read index = write index; the count is left as the original does)
void Runtime::sound_queue_reset()
{
    mem.set32(0x512350, 0);
    mem.set32(0x512348, mem.u32(0x51234C));
}

// the queue's drain (the shape of 0x15430: one command when not held (0x512350) and the queue not empty)
void Runtime::sound_queue_drain()
{
    const uint32_t r = mem.u32(0x512348);
    if (mem.u32(0x512350) != 0 || r == mem.u32(0x51234C))
        return;
    const uint32_t cue = mem.u32(0x512360 + (r & 0xF) * 4);
    const int32_t n = int32_t(mem.u32(0x512344)) - 1;
    mem.set32(0x512344, uint32_t(n < 1 ? 0 : n));
    mem.set32(0x512348, (r + 1) & 0xF);
    if (cueLog)
        cueLog->push_back(cue);
    if (audio)
        audio->cue(cue);
}

bool Runtime::player_out(int p) const
{
    const Player& pl = mem.at<Player>(G::players, uint32_t(p));
    return mem.at<uint8_t>(kStateTable + uint32_t(pl.state) * 8 + 4) != 0;
}

// the top of the game's frame: mode 5 (game over), else mode 4 (mode4_game 0x25C20 and its sub-modes 2-6: the port's,
// src/port/game_flow.cpp, proofs-7)
void Runtime::game_mode_frame()
{
    const int16_t mode = mem.at<int16_t>(G::game_mode);
    if (mode == 5) {
        mode5_game_over();
        return;
    }
    if (mode != 4) {   // the title and the attract loop (attract.cpp)
        attract_frame();
        return;
    }
    if (mem.at<int16_t>(G::game_submode) == 0) {   // mode 4 sub-mode 0 (game_new): a start in the attract
        attract_game_new();
        return;
    }
    switch (mem.at<int16_t>(G::game_submode)) {   // title-2's sub-modes (name_entry.cpp) around the port's mode4_game
    case 9: game_name_entry_start(); break;
    case 10: game_name_entry(); break;
    default: mode4_game(); break;
    }
}

// 0xDE70 set_mode: the port's (src/port/game_flow.cpp, proofs-7) + the log line
void Runtime::set_mode(int mode)
{
    modeBeforeSet_ = mem.at<int16_t>(G::game_mode);   // title worker: the attract mode a start came from
    port::Game<A>::set_mode(mode);
    if (log)
        log("game mode " + std::to_string(mode));
}

// 0x5260 player_st4_out_of_lives, 0x52C0 player_continue_start, 0x5300 player_continue_frame: the port's
// (src/port/game_flow.cpp, proofs-7)

// 0x5470 player_st6_game_over / 0x5540 player_game_over_wait: the port's (src/port/game_over.cpp, proofs-5)

// mode 5 (0x971E0 through table 0xCA770): 0x97200 start (players out -> state 6, music: game over), 0x97340 the
// scene (360 frames down to 90), 0x5CB50 / 0x5CBF0 the map (180 frames), 0x973A0 GAME OVER, 0x97430 the wait (180
// frames), then next_mode: in the teaser,
// gameOverDone (the host starts the chapter again)
void Runtime::mode5_game_over()
{
    // scroll_regs_vblank (0x17190): the scroll shadows 0x51E8F4.. to the tilemap registers 0x100A000..
    auto scroll_regs = [&] {
        if (mem.mapped(0x100A000))
            for (uint32_t i = 0; i < 0x10; i += 2)
                mem.at<uint16_t>(0x100A000 + i) = mem.at<uint16_t>(0x51E8F4 + i);
    };
    // proofs-5: the sub-modes are the port's (src/port/game_over.cpp, capture-proven)
    switch (mem.u32(G::game_submode)) {
    case 0: game_over_start(); break;       // 0x97200
    case 1: game_over_scene(); break;       // 0x97340
    case 2: game_over_map_start(); break;   // 0x5CB50
    case 3:                                 // 0x5CBF0
        game_over_map();
        scroll_regs();
        break;
    case 4:                                 // 0x973A0
        game_over_text();
        scroll_regs();
        break;
    default: game_over_wait(); break;       // 0x97430 (next_mode: gameOverDone)
    }
}

// 0x18680 palette_fade_finish (the port's mode-5 routines call it when a fade runs)
void Runtime::palette_fade_finish()
{
    palette_fade_finish_if_running(mem);
}

// 0x97490 game_over_camera: the port's (src/port/game_over.cpp, proofs-5)

// 0x97740 game_over_state0_camera, 0x5CCA0 / 0x5D4F0 / 0x5D4D0 / 0x5CE70 (the map's group): the port's (proofs-5)
// 0x5CCD0 game_over_walker_task, 0x5CF50 game_over_route_task: the port's (src/port/game_over.cpp, proofs-5)

} // namespace hotd::game
