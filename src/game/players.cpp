// Joining and continuing (docs/teaser_plan.md R4): the original's start check and player start states, read from the
// typed decompilation (0x4F80..0x5870, 0x5620..0x5670, 0x6020, 0x273E0, 0x1FA80) and the game's settings
// (apply_settings 0xDC20). A player not in play (state 9) waits in player_waiting_frame: the start button joins
// (state 3, PlStartInit with the join row); a player out of lives (state 4) continues with the start button (state 1:
// score + 1, the dynamic difficulty - 2). The teaser always runs in free play (no coins). UNVERIFIED (teaser).
// Not here: the HUD prompts (hud_start_prompt, hud_popup_clear: the placeholder HUD reads the player states), the
// play-time bookkeeping (bookkeeping_play_end, play_time_*), the join's character body re-init (cut-scene bodies are
// not drawn yet).
#include "game/runtime.h"
#include "game/lives.h"

namespace hotd::game {

using G = A::G;

using port::a::Player;
using port::a::PlayerTask;

namespace {
constexpr uint32_t kStartRows = 0xA8700;        // PlStartInit's rows (0x14 bytes) by start kind 0-3
constexpr uint32_t kStateTable = 0xA8790;       // per state: routine, +4 "out", +5 "can start"
constexpr uint32_t kStartLives = 0x51F5B1;      // byte: lives at the start (the lives setting's first value)
constexpr uint32_t kLivesIcons = 0x51F5C0;      // 10 bytes a player: (shown, 0x2D) per life up to max_lives
constexpr uint32_t kFreePlay = 0x51FDB8;
constexpr uint32_t kSplitCredits = 0x51FDBC;
constexpr uint32_t kCredits = 0x51F4B8;
constexpr uint32_t kCreditsToStart = 0x51F604, kCreditsToContinue = 0x51F608;
constexpr uint32_t kStartButtons = 0x51F99C;    // u16 a player: bit 11 = start pressed this frame
constexpr uint32_t kStartBlink = 0x51F4E0;      // u16, 0x1C a player: bit 0 = "press start" blinks
constexpr uint32_t kPopups = 0x51F480;          // 8 bytes a player
constexpr uint32_t kDynDifficulty = 0x51F492;
constexpr uint32_t kJoinFlag = 0x51F430;
constexpr uint32_t kLivesTable = 0xD780;        // (start, max) by lives setting 0-11
} // namespace

// 0xDC20 apply_settings, the game part: the difficulty, lives and blood colour from the launcher's settings (the
// arcade reads them from its EEPROM); free play. The gun calibration is the engine's (the mouse needs none).
void Runtime::apply_settings()
{
    const int lives = livesCodeValid(settings.lives) ? settings.lives : 9;
    mem.at<int16_t>(G::difficulty) = int16_t(settings.difficulty < 0 || settings.difficulty > 4 ? 2 : settings.difficulty);
    if (lives < kLivesSettings) {   // the arcade's setting: its pair from the ROM's table
        mem.at<uint8_t>(kStartLives) = mem.at<uint8_t>(kLivesTable + uint32_t(lives) * 2);
        mem.at<uint8_t>(G::max_lives) = mem.at<uint8_t>(kLivesTable + uint32_t(lives) * 2 + 1);
    } else {   // GitHub #30: Starting Lives / Max Lives as no setting of the arcade has them (game/lives.h)
        mem.at<uint8_t>(kStartLives) = uint8_t(livesStartOf(lives));
        mem.at<uint8_t>(G::max_lives) = uint8_t(livesMostOf(lives));
    }
    mem.at<uint8_t>(G::blood_color) = uint8_t(settings.bloodColor & 3);
    mem.set32(kFreePlay, settings.freePlay ? 1 : 0);
}

// 0x6020 player_start_check: the port's (src/port/player_start.cpp, with player_can_start, credit_lockout_reset and
// the waiting player's routines 0x5620 / 0x5660 / 0x5670), then the log line
uint32_t Runtime::player_start_check(uint32_t task, int state)
{
    const uint32_t started = port::Game<A>::player_start_check(task, state);
    if (started && log) {
        const int p = mem.at<PlayerTask>(task).player;
        const int32_t s = mem.at<Player>(G::players, uint32_t(p)).state;
        log("player " + std::to_string(p + 1) + (s == 1 ? " continues" : s == 3 ? " joins" : " starts (state " + std::to_string(s) + ")"));
    }
    return started;
}

// 0x4F80 player_st1_continue, 0x4FE0 player_st2_rejoin, 0x4FF0 player_st3_join, 0x5870 PlStartInit: the port's
// (src/port/player_join.cpp, proofs-7)

// testing (--input-test's 4-seat section, pads_test.cpp): the 4P mode switched on in a running game: the block as at a
// game's start with players 3 / 4 waiting for their start button, and their tasks
void Runtime::coop4_enable()
{
    seats4 = 1;
    coop4_reset(2, mem.at<uint8_t>(0x51F5B1));
    for (int p = 2; p < 4; ++p) {
        const uint32_t t = task_create(port::coop4::kSeatRoutine, 0x24);
        mem.at<Player>(player_rec(p)).task.a = t;
        mem.at<PlayerTask>(t).player = int8_t(p);
    }
}

// the start button (keys 1 / 2, a mouse's middle button): seen by player_start_check on the player's next frame
void Runtime::player_join(int p)
{
    // GitHub #29: in a game in player 2's seat, player 1's input plays player 2 (main.cpp), its start button too (the
    // continue); player 2's start button joins seat 1. At the title each start button is its own seat's (the arcade's)
    if (const int16_t mode = mem.at<int16_t>(G::game_mode); soloSeat == 1 && p < 2 && (mode == 4 || mode == 5))
        p = 1 - p;
    if (lgModes.hardcore && lgOut_[p & 3])   // Light Gun Edition: Hardcore: out of lives is out for the game
        return;
    startRequest[p] = true;
}

} // namespace hotd::game
