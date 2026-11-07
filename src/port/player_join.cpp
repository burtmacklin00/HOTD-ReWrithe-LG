// The players' start states (proofs-7): player_st1_continue 0x4F80, player_st2_rejoin 0x4FE0, player_st3_join 0x4FF0
// (+ player_body_reinit 0x5008, its debug-mode tail), PlStartInit 0x5870 and hud_popup_clear 0x94430; first written in
// the runtime (src/game/players.cpp), moved here for their capture proof. Arcade forms only (the PC's are replayed).
// Declarations: decl/support_b.h.
#include "port/game.h"

namespace hotd::port {

namespace join {
constexpr uint32_t kStartRows = 0xA8700;        // PlStartInit's rows (0x14 bytes) by start kind 0-3
constexpr uint32_t kStartLives = 0x51F5B1;      // byte: lives at the start (the lives setting's first value)
constexpr uint32_t kLivesIcons = 0x51F5C0;      // 10 bytes a player: (shown, 0x2D) per life up to max_lives
constexpr uint32_t kPopups = 0x51F480;          // 8 bytes a player
constexpr uint32_t kDynDifficulty = 0x51F492;
constexpr uint32_t kJoinFlag = 0x51F430;
} // namespace join

// 0x94430 hud_popup_clear (task): the player's popup off (its bytes 0 / 1), its sprite taken off the layer (position
// table 0x94368 by kind and slot + the popup's x offset, sprite table 0xCA5FC)
template <class P> void Game<P>::hud_popup_clear(uint32_t task)
{
    if constexpr (!P::pc) {
        const uint32_t rec = join::kPopups + uint32_t(int32_t(mem.template at<int8_t>(task + 0x20))) * 8;
        const uint32_t slot = mem.u32(rec + 4);
        const int32_t kind = mem.template at<int8_t>(rec + 3);
        const int32_t dx = mem.template at<int8_t>(rec + 2);
        mem.template at<uint8_t>(rec + 1) = 0;
        mem.template at<uint8_t>(rec) = 0;
        const uint32_t pos = 0x94368 + uint32_t(kind) * 16 + slot * 8;
        sprite_draw(0x1004000, int32_t(mem.u32(pos)) + dx, int32_t(mem.u32(pos + 4)),
                    int32_t(mem.u32(0xCA5FC + uint32_t(kind) * 8 + slot * 4)));
    } else {
        (void)task;
    }
}

// 0x4F80 player_st1_continue: the popup off, the play time booked and restarted, one point for the continue (the
// score's last digit counts them), the dynamic difficulty 2 lower (not below 0), then the continue row
template <class P> void Game<P>::player_st1_continue(uint32_t task)
{
    if constexpr (!P::pc) {
        const int p = mem.template at<int8_t>(task + 0x20);
        if (mem.template at<uint8_t>(join::kPopups + uint32_t(p) * 8) != 0)
            hud_popup_clear(task);
        bookkeeping_play_end(mem.template at<int8_t>(task + 0x20));
        play_time_continue(mem.template at<int8_t>(task + 0x20));
        add_score(mem.template at<int8_t>(task + 0x20), 1);
        int16_t d = int16_t(mem.template at<uint16_t>(join::kDynDifficulty) - 2);
        if (d < 0)
            d = 0;
        mem.template at<int16_t>(join::kDynDifficulty) = d;
        PlStartInit(task, 1);
    } else {
        (void)task;
    }
}

// 0x4FE0 player_st2_rejoin
template <class P> void Game<P>::player_st2_rejoin(uint32_t task)
{
    if constexpr (!P::pc)
        PlStartInit(task, 2);
    else
        (void)task;
}

// 0x4FF0 player_st3_join: the join flag cleared; in the debug mode (0x51F42C) the player's character body made again
// (player_body_reinit 0x5008: its parts freed, body_init with the player's type and motion, its slot in 0x51F440
// moved to 14 + player, the skin template); the play time started; the join row
template <class P> void Game<P>::player_st3_join(uint32_t task)
{
    if constexpr (!P::pc) {
        const uint32_t debug = mem.u32(0x51F42C);
        mem.set32(join::kJoinFlag, 0);
        // fix28 (user 2026-10-27: "the cutscene does not account for a player who joins mid match when starting for 1
        // player"): the arcade makes both bodies at the chapter load and a join counts the player in (PlStartInit's
        // row: players_in_game + 1), so its next cut-scenes show the joined player as in a game started with them
        // (checked: Chapter 1, player 2 joining at 2000, the scenes' routines and pictures as a 2-player start's). PC
        // Mode (ReWrithe's): a seat that started as No Player had No Player's body (Rogan's type) and the join gives it
        // Rogan or G (pm_join_char, again in PlStartInit: no change there): its body made again as the debug pair's
        // (4P set B: players 3 / 4's bodies, left alone)
        bool remake = debug != 0;
        if (chars_on()) {
            const int32_t p = mem.template at<int8_t>(task + 0x20);
            bool setB = false;
            if constexpr (Seats<P>::max == 4)
                setB = coop4_on() && mem.u32(coop4::kMagic) == 0x34504334u && mem.template at<uint8_t>(coop4::kBodySetB) != 0;
            pm_join_char(p);
            if (p >= 0 && p < 2 && !setB && int32_t(mem.u32(0x51F438 + uint32_t(p) * 4)) != pm_body_type(p)) {
                mem.set32(0x51F438 + uint32_t(p) * 4, uint32_t(pm_body_type(p)));
                remake = true;
            }
        }
        if (remake) {
            const uint32_t body = mem.u32(0x51F340 + uint32_t(int32_t(mem.template at<int8_t>(task + 0x20))) * 0xCC);
            body_free_parts(body);
            const int32_t p = mem.template at<int8_t>(task + 0x20);
            body_init(body, int32_t(mem.u32(0x51F438 + uint32_t(p) * 4)), mem.u32(0xA86B8 + uint32_t(p) * 4));
            mem.set32(0x51F440 + uint32_t(int32_t(mem.template at<int8_t>(body + 0x2E6))) * 4, 0);
            const uint8_t slot = uint8_t(mem.template at<uint8_t>(task + 0x20) + 14);
            mem.template at<uint8_t>(body + 0x2E6) = slot;
            const uint16_t skin = mem.template at<uint16_t>(0x20E40 + uint32_t(int32_t(mem.template at<int16_t>(body + 0xCC))) * 2);
            mem.set32(0x51F440 + uint32_t(slot) * 4, body);
            if (skin != 0xFFFF)
                skin_template_copy(body);
        }
        play_time_start(mem.template at<int8_t>(task + 0x20));
        PlStartInit(task, 3);
    } else {
        (void)task;
    }
}

// 0x5870 PlStartInit (task, how): the row of table 0xA8700 for how (0 new game, 1 continue, 2 rejoin, 3 join): flags
// (bit 0 lives from the setting with their icons, bit 2 score cleared, bit 3 the start draw in play, bit 4 / 5 the
// players-in-game counters), the camera drive's two set-up routines, the HUD, the popups, the gun and the player
// cleared (6 rounds), invulnerability, the state (routine not set), then the routine the task goes on with
template <class P> void Game<P>::PlStartInit(uint32_t task, int how)
{
    if constexpr (!P::pc) {
        const int32_t p = mem.template at<int8_t>(task + 0x20);
        pm_join_char(p);   // PC Mode only: No Player -> Rogan / G
        const uint32_t row = join::kStartRows + uint32_t(how) * 0x14;
        const uint8_t flags = mem.template at<uint8_t>(row);
        uint32_t maxLives = mem.template at<uint8_t>(G::max_lives);
        const uint32_t rec = 0x51F280 + uint32_t(p) * 0xCC;
        const uint32_t gun = 0x51F570 + uint32_t(p) * 28;
        if (flags & 1) {
            // PC Mode (ReWrithe on the arcade logic; THOTD.EXE PlStartInit 0x43557C): the character's hit points
            const uint32_t lives = pm_on() ? uint32_t(pm_table(pm_seat_char(p)).hp)
                                           : uint32_t(mem.template at<uint8_t>(join::kStartLives));
            mem.set32(rec + 0x10, lives);
            mem.set32(rec + 0x14, lives);
            pm_lives_most(int32_t(lives));   // (#62: PC Mode, a character's hit points over Max Lives)
            maxLives = mem.template at<uint8_t>(G::max_lives);
            uint32_t icon = join::kLivesIcons + uint32_t(p) * 10;
            uint32_t i = 0;
            // GitHub #25 (Max Lives up to 9): the icons stop at the five places a player has (10 bytes), the rest of
            // the lives are counted by the HUD (hud_lives_draw); 5 or less as the arcade
            const uint32_t places = 5;
            if (lives != 0) {
                do {
                    ++i;
                    mem.template at<uint8_t>(icon) = 1;
                    mem.template at<uint8_t>(icon + 1) = 0x2D;
                    icon += 2;
                } while ((i & 0xFF) < lives && i < places);
            }
            for (; (i & 0xFF) < mem.template at<uint8_t>(G::max_lives) && i < places;) {
                ++i;
                mem.template at<uint8_t>(icon) = 0;
                mem.template at<uint8_t>(icon + 1) = 0x2D;
                icon += 2;
                if ((i & 0xFF) >= (maxLives & 0xFF))
                    break;
            }
        }
        if ((flags >> 2) & 1)
            mem.set32(rec + 0xC, 0);
        call_routine(mem.u32(row + 8), task);
        call_routine(mem.u32(row + 0xC), task);
        if (mem.template at<int16_t>(G::game_mode) == 3)
            hud_player_full_draw(task);
        else if (((mem.template at<uint8_t>(row) >> 3) & 1) && mem.template at<uint8_t>(G::scene_state) == 2)
            hud_player_start_draw(task);
        const uint8_t f = mem.template at<uint8_t>(row);
        if ((f >> 4) & 1)
            mem.template at<int16_t>(G::players_in_game) = int16_t(mem.template at<int16_t>(G::players_in_game) + 1);
        if ((f >> 5) & 1)
            mem.template at<int16_t>(G::players_in_game_b) = int16_t(mem.template at<int16_t>(G::players_in_game_b) + 1);
        mem.template at<uint8_t>(join::kPopups + uint32_t(int32_t(mem.template at<int8_t>(task + 0x20))) * 8) = 0;
        mem.template at<uint8_t>(join::kPopups + uint32_t(int32_t(mem.template at<int8_t>(task + 0x20))) * 8 + 1) = 0;
        const int32_t p2 = mem.template at<int8_t>(task + 0x20);
        mem.set32(rec + 0xA4, 0);
        mem.template at<uint8_t>(gun + 0x1A) = 0;
        mem.template at<uint16_t>(gun + 6) = 0;
        mem.template at<uint16_t>(gun + 4) = 0;
        mem.template at<uint16_t>(rec + 0x3A) = 0;
        mem.template at<uint16_t>(rec + 0x38) = 0;
        mem.template at<uint8_t>(rec + 0x42) = 0;
        mem.set32(rec + 0x44, 0);
        mem.template at<uint8_t>(rec + 0x41) = pm_full_rounds(p2);   // 6; PC Mode: the character's rounds
        mem.set32(G::player_invulnerable + uint32_t(p2) * 4, uint32_t(int32_t(mem.template at<int16_t>(row + 4))));
        mem.template at<uint8_t>(0x5430A0 + uint32_t(int32_t(mem.template at<int8_t>(task + 0x20)))) = 0;
        mem.template at<uint8_t>(0x5430A2 + uint32_t(int32_t(mem.template at<int8_t>(task + 0x20)))) = 0;
        player_state_set(mem.template at<uint16_t>(row + 2), 0, task);
        const uint32_t next = mem.u32(row + 0x10);
        mem.set32(task, next);
        call_routine(next, task);
    } else {
        (void)task, (void)how;
    }
}

// PlStartInit's camera-drive set-ups (row +8 / +0xC): the body routine (player +0xB0: 0x6240 player_body_draw_camdr /
// 0x6290 player_hit_notice, +0xC8 cleared) and the scene routine (+0xB4: 0x4B820 camdr_mode3_body / 0x4B830
// cctb33_mode4_body)
template <class P> void Game<P>::player_camdr_set(uint32_t task, uint32_t bodyRoutine)
{
    const uint32_t rec = 0x51F280 + uint32_t(int32_t(mem.template at<int8_t>(task + 0x20))) * 0xCC;
    mem.set32(rec + 0xC8, 0);
    mem.set32(rec + 0xB0, bodyRoutine);
}

// 0x61C0 player_camdr_mode1
template <class P> void Game<P>::player_camdr_mode1(uint32_t task) { player_camdr_set(task, 0x6240); }

// 0x61F0 player_camdr_mode4
template <class P> void Game<P>::player_camdr_mode4(uint32_t task) { player_camdr_set(task, 0x6290); }

// 0x4B7D0 player_camdr_mode3
template <class P> void Game<P>::player_camdr_mode3(uint32_t task)
{
    mem.set32(0x51F334 + uint32_t(int32_t(mem.template at<int8_t>(task + 0x20))) * 0xCC, 0x4B820);
}

// 0x4B7F0 player_camdr2_mode4
template <class P> void Game<P>::player_camdr2_mode4(uint32_t task)
{
    mem.set32(0x51F334 + uint32_t(int32_t(mem.template at<int8_t>(task + 0x20))) * 0xCC, 0x4B830);
}

} // namespace hotd::port
