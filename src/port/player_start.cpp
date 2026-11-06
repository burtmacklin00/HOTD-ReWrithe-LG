// A player waiting to join (state 9) and the start check: credits (or free play), the start button, the credits
// taken, the player's next state, the "press start" blink and prompt. First written in the runtime (src/game/players.cpp,
// R4), read from the disassembly. Arcade forms (decl/support_b.h).
#include "port/game.h"

namespace hotd::port {

namespace {
constexpr uint32_t kPlayerBlocks = 0x51F280;    // the player blocks (0xCC each): +0 index byte, +4 state, +0x84 next (arcade)
constexpr uint32_t kFreePlay = 0x51FDB8, kSplitCredits = 0x51FDBC, kCredits = 0x51F4B8;
constexpr uint32_t kCreditsToStart = 0x51F604, kCreditsToContinue = 0x51F608;
constexpr uint32_t kStartButtons = 0x51F99C;    // u16 a player: bit 11 = start pressed this frame
constexpr uint32_t kStartBlink = 0x51F4E0;      // u16, 0x1C a player: bit 0 = "press start" blinks
constexpr uint32_t kStateTable = 0xA8790;       // per state (8 bytes): routine, +5 "can start"
constexpr uint32_t kJoinFlag = 0x51F430;
constexpr uint32_t kCameraStatus = 0x51F564;   // bit 0: players may act, bit 1: may start (arcade)
} // namespace

// 0x273E0 player_can_start(p): free play, or enough credits (shared or per player) to start / continue
template <class P> uint32_t Game<P>::player_can_start(int p)
{
    if (mem.u32(kFreePlay) == 1)
        return 1;
    const int32_t state = int32_t(mem.u32(kPlayerBlocks + 4 + uint32_t(p) * 0xCC));
    const uint32_t slot = mem.u32(kSplitCredits) != 0 ? uint32_t(p) : 0;
    const int32_t credits = int32_t(mem.u32(kCredits + slot * 4));
    const int32_t need = int32_t(mem.u32(state == 4 ? kCreditsToContinue : kCreditsToStart));
    return credits >= need ? 1 : 0;
}

// 0x27450 player_start_pressed(p): the start button this frame (bit 11)
template <class P> uint32_t Game<P>::player_start_pressed(int p)
{
    return (mem.at<uint16_t>(kStartButtons + uint32_t(p) * 2) >> 11) & 1;
}

// 0x1FA80 credit_lockout_reset(p, credits): the start lock-out and, unless free play, the credits taken (never below
// 0; 0x51F9E4 = 1). The backup RAM copy (0x1D0000A + slot x 2) is not modelled
template <class P> void Game<P>::credit_lockout_reset(int p, int32_t credits)
{
    uint32_t slot = uint32_t(p);
    if (mem.u32(kSplitCredits) == 0) {
        mem.at<int16_t>(0x51E82E) = 0;
        mem.at<int16_t>(0x51E82C) = 0;
        slot = 0;
    } else {
        mem.at<int16_t>(0x51E82C + uint32_t(p) * 2) = 0;
    }
    if (mem.u32(kFreePlay) == 1)
        return;
    const uint32_t c = kCredits + slot * 4;
    int32_t left = int32_t(mem.u32(c) - uint32_t(credits));
    if (!(0 <= left))
        left = 0;
    mem.set32(c, uint32_t(left));
    mem.set32(0x51F9E4, 1);
}

// 0x6020 player_start_check(task, state): with credits (or free play), the start button, outside the game over and
// with the camera allowing it (camera_status bit 1): the credits are taken, mode 4 starts if needed (camera_status
// bit 0 set), and the player goes to `state` (or, while the camera holds the players back (bit 0 clear), to 10 =
// paused with `state` next). The "press start" blink bit (frame % 30 > 15); the start sound. 1 = started
template <class P> uint32_t Game<P>::player_start_check(uint32_t task, int state)
{
    if constexpr (P::pc) {
        (void)task, (void)state;   // the PC form: not ported (vtable only; ProofPc replays it)
        return 0;
    } else {
        const int32_t p = mem.at<int8_t>(task + 0x20);
        const uint32_t blk = kPlayerBlocks + uint32_t(p) * 0xCC;
        // PC Mode (THOTD.EXE player_waiting_frame 0x436B60 / player_continue_frame 0x435018): can_continue 0x45B8F0
        // (the shared credits from the Continues option) instead of free play; the start takes one (0x448934)
        // (Survival, survrevive: a seat out until the next round cannot start, no blink: seatsHeldOut)
        const uint32_t can = (seatsHeldOut >> (p & 7)) & 1 ? 0u : pm_on() ? (pm_can_continue() ? 1u : 0u) : player_can_start(p);
        uint32_t started = 0;
        if (can != 0 && player_start_pressed(p) != 0 && mem.at<int16_t>(G::game_mode) != 5 &&
            ((mem.at<uint8_t>(kCameraStatus) >> 1) & 1)) {
            started = 1;
            if (pm_on())
                pm_credit_take();
            credit_lockout_reset(p, int32_t(mem.u32(mem.u32(blk + 4) == 4 ? kCreditsToContinue : kCreditsToStart)));
            if (mem.at<int16_t>(G::game_mode) != 4) {
                set_mode(4);
                mem.set32(kCameraStatus, mem.u32(kCameraStatus) | 1);
            }
            if (mem.at<uint8_t>(kCameraStatus) & 1) {
                player_state_set(state, 1, task);
            } else {
                mem.set32(blk + 0x84, uint32_t(state));
                player_state_set(10, 1, task);
            }
        }
        const bool blinkOk = mem.at<uint8_t>(kStateTable + 5 + mem.u32(blk + 4) * 8) != 0 && mem.at<int16_t>(G::game_mode) != 5 &&
                             ((mem.at<uint8_t>(kCameraStatus) >> 1) & 1);
        const uint32_t b = kStartBlink + uint32_t(p) * 0x1C;
        mem.at<uint16_t>(b) = uint16_t(mem.at<uint16_t>(b) & 0xFFFE);
        if (can != 0 && blinkOk && int32_t(mem.u32(G::frame_counter)) % 30 > 15)
            mem.at<uint16_t>(b) = uint16_t(mem.at<uint16_t>(b) | 1);
        if (started)
            sound_send3(0xA01354);
        return started;
    }
}

// 0x5620 player_st9_waiting (player state 9): the attract demo (mode 3: player_demo_start 0x57E0) / the lesson (mode 8:
// player_lesson_start 0x57F0) start the player at once; otherwise player_waiting (0x5660) from now on
template <class P> void Game<P>::player_st9_waiting(uint32_t task)
{
    const int16_t mode = mem.at<int16_t>(G::game_mode);
    if (mode == 3) {
        player_demo_start(task);
        return;
    }
    if (mode == 8) {
        player_lesson_start(task);
        return;
    }
    mem.set32(task, F::player_waiting);
    player_waiting(task);
}

// 0x5660 player_waiting
template <class P> void Game<P>::player_waiting(uint32_t task) { player_waiting_frame(task); }

// 0x5670 player_waiting_frame: the start check (join = state 3 in mode 4, else 0); joined: the join flag; the PRESS
// START prompt (hud_start_prompt) while the game runs, the camera allows it and the player waits
template <class P> void Game<P>::player_waiting_frame(uint32_t task)
{
    if constexpr (P::pc) {
        (void)task;   // the PC form: not ported (vtable only; ProofPc replays it)
        return;
    }
    const uint32_t blk = kPlayerBlocks + uint32_t(int32_t(mem.at<int8_t>(task + 0x20))) * 0xCC;
    player_start_check(task, mem.at<int16_t>(G::game_mode) == 4 ? 3 : 0);
    const uint32_t state = mem.u32(blk + 4);
    if (state == 3)
        mem.set32(kJoinFlag, 1);
    int on = 0;
    if ((mem.at<uint8_t>(kCameraStatus) >> 1) & 1)
        on = mem.at<int16_t>(G::game_mode) == 4 ? 1 : 0;
    if (state != 9)
        on = 0;
    hud_start_prompt(int(mem.at<uint8_t>(blk)), 0, on);
}

} // namespace hotd::port
