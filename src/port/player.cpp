// The player block (teaser L3): a hit on a player (player_hit, player_hit_lose_life: a life, 100 points, 90 frames of
// invulnerability, the hit's side for the screen shake), an extra life (player_life_add) and the player task's
// in-game frame (player_in_game_frame: the scene routine, the gun, out of lives, the HUD). Arcade forms only.
// Player p's record is players + p x 0xCC (types.tsv Player): +0 state, +0xC lives, +0x10 lives_saved, +0xA8 the
// camera-drive routine, +0xAC the scene routine, +0xB4 shake, +0xB8 hit_side.
#include "port/game.h"

namespace hotd::port {

namespace pl {
constexpr uint32_t hud_state = 0x51F4B5;     // byte: the scene_state the HUD last showed (unnamed)
constexpr uint32_t hud_start_shown = 0x512080;   // byte: "press start" is shown (unnamed)
constexpr uint32_t state_routines = 0xA8790;     // per player state: its routine (8 bytes a state)
} // namespace pl

// the player-check of player_hit / player_hit_lose_life: 0 = can be hit; -1 not a player; -3 in mode 4 a state other
// than 1, 4 or 5
template <class P> int32_t Game<P>::player_hit_check(int player)
{
    if (player != 0 && player != 1 && !(coop4_on() && (player == 2 || player == 3)))   // 4P: seats 2 / 3 too
        return -1;
    if (mem.at<int16_t>(G::game_mode) != 4)
        return 0;
    const int32_t s = mem.at<int32_t>(player_rec(player));
    if (s == 1 || s == 4 || s == 5)
        return 0;
    return -3;
}

// the common end of a hit: hits_counter, a life and 100 points off; with shake the side for the screen shake; then,
// unless the camera is at a stop or of kind 2, a player left with no lives keeps one
template <class P> void Game<P>::player_hit_take(int player, int shake, int side)
{
    const uint32_t rec = player_rec(player);
    if constexpr (!P::pc)   // the PC's hits_counter is not paired yet (no PC form of this file)
        mem.at<uint8_t>(G::hits_counter) = uint8_t(mem.at<uint8_t>(G::hits_counter) - 1);
    mem.at<int32_t>(rec + 0xC) -= 1;
    add_score(player, -100);
    if (shake != 0) {
        mem.at<int32_t>(rec + 0xB8) = side;
        mem.at<int32_t>(rec + 0xB4) = 1;
    }
}

// 4P (docs/coop4.md, phase 3): the enemies' attacks rotate over the players in play. An enemy aims at side 0 / 1 as
// the arcade does (its slots, motions, the screen side); the hit itself goes to the next seat in the rotation that can
// be hit now (in play, not invulnerable); none: the side asked for (whose own tests then decide)
template <class P> int Game<P>::coop4_victim(int side)
{
    if (!coop4_on() || mem.at<uint16_t>(G::game_mode) != 4)
        return side;
    uint8_t& turn = mem.at<uint8_t>(coop4::kTargetTurn);
    for (int k = 1; k <= 4; ++k) {
        const int s = (turn + k) & 3;
        if (mem.at<int32_t>(player_rec(s)) == 5 && mem.at<int32_t>(invuln_at(s)) == 0) {
            turn = uint8_t(s);
            return s;
        }
    }
    // none hittable now: a seat in play (its invulnerability stops the hit), never one that is out (a hit there took
    // lives below 0); nobody in play: no hit (-1)
    if (mem.at<int32_t>(player_rec(side)) == 5)
        return side;
    for (int s = 0; s < 4; ++s)
        if (mem.at<int32_t>(player_rec(s)) == 5)
            return s;
    return -1;
}

template <class P> void Game<P>::player_hit_keep_life(int player)
{
    if (mem.u32(G::camera_stopped) == 2 || mem.u32(G::camera_kind) == 2)
        return;
    const uint32_t rec = player_rec(player);
    if (mem.at<int32_t>(rec + 0xC) <= 0)
        mem.at<int32_t>(rec + 0xC) = 1;
}

// 0x6410 player_hit (player, shake, side): not while invulnerable or in the attract demo (mode 3); 90 frames of
// invulnerability after
template <class P> void Game<P>::player_hit(int player, int shake, int side)
{
    if (coop4_on() && (player == 0 || player == 1))
        player = coop4_victim(player);
    if (player_hit_check(player) != 0)
        return;
    if (mem.at<int32_t>(invuln_at(player)) != 0)
        return;
    if (mem.at<uint16_t>(G::game_mode) == 3)
        return;
    if (gHitHold && gHitHold(player))   // (ours: The Typing of the Dead 0's boss window, game.h; null in the arcade)
        return;
    player_hit_take(player, shake, side);
    mem.at<int32_t>(invuln_at(player)) = 0x5A;
    player_hit_keep_life(player);
}

// 0x6520 player_hit_lose_life (player, shake, side, force, frames): as player_hit, but `force` hits an invulnerable
// player too, the demo is not tested, and the invulnerability is `frames` (-1: left as it is)
template <class P> void Game<P>::player_hit_lose_life(int player, int shake, int side, int force, int frames)
{
    if (player_hit_check(player) != 0)
        return;
    if (mem.at<int32_t>(invuln_at(player)) != 0 && force == 0)
        return;
    player_hit_take(player, shake, side);
    if (frames != -1)
        mem.at<int32_t>(invuln_at(player)) = frames;
    player_hit_keep_life(player);
}

// 0x6960 player_life_add (player): one life more, up to max_lives; 1 if added
template <class P> uint32_t Game<P>::player_life_add(int player)
{
    const uint32_t rec = player_rec(player);
    if (!(mem.at<int32_t>(rec + 0xC) < int32_t(mem.at<uint8_t>(G::max_lives))))
        return 0;
    mem.at<int32_t>(rec + 0xC) += 1;
    return 1;
}

// 0x6190 player_state_set (state, set the routine, task): the player's state; with the flag the task's routine from
// the state table
template <class P> void Game<P>::player_state_set(int state, int setRoutine, uint32_t task)
{
    const uint32_t rec = G::players + uint32_t(int32_t(mem.at<int8_t>(task + 0x20))) * 0xCC;
    mem.at<int32_t>(rec) = state;
    if (setRoutine != 0)
        mem.set32(task, mem.u32(pl::state_routines + uint32_t(state) * 8));
}

// 0x6220 player_camdr_run (task): the player's camera-drive routine (+0xAC)
template <class P> void Game<P>::player_camdr_run(uint32_t task)
{
    const uint32_t rec = G::players + uint32_t(int32_t(mem.at<int8_t>(task + 0x20))) * 0xCC;
    call_routine(mem.u32(rec + 0xAC), task);
}

// 0x50B0 player_in_game_frame (task, the player's task in play): the scene routine (+0xB0), the camera drive; lives
// (1 in the demo, never below 0) and, with lives, the gun (player_shoot); outside the demo and mode 8: out of lives ->
// one player fewer, state 4 (out of lives) and the ammo HUD; else the HUD by scene_state (hostage prompts) and the
// lives HUD, lives_saved; the HUD pop-ups; in the demo player_waiting_frame
template <class P> void Game<P>::player_in_game_frame(uint32_t task)
{
    const uint32_t p = uint32_t(int32_t(mem.at<int8_t>(task + 0x20)));
    const uint32_t rec = G::players + p * 0xCC;
    call_routine(mem.u32(rec + 0xB0), task);
    player_camdr_run(task);
    int32_t lives = mem.at<uint16_t>(G::game_mode) == 3 ? 1 : mem.at<int32_t>(rec + 0xC);
    if (lives <= 0)
        lives = 0;
    mem.at<int32_t>(rec + 0xC) = lives;
    if (lives != 0)
        player_shoot(task);
    const uint16_t mode = mem.at<uint16_t>(G::game_mode);
    if (mode != 8 && mode != 3) {
        if (mem.at<int32_t>(rec + 0xC) == 0) {
            mem.at<int16_t>(G::players_in_game) = int16_t(mem.at<int16_t>(G::players_in_game) - 1);
            player_state_set(4, 1, task);
            if (mem.u32(G::ammo_enabled) != 0)
                hud_ammo_draw(task);
        } else {
            const uint8_t s = mem.at<uint8_t>(G::scene_state);
            if (s != mem.at<uint8_t>(pl::hud_state)) {
                if (s == 3 || s == 5) {
                    hud_player_full_draw(task);
                    if (mem.u32(G::ammo_enabled) != 0)
                        hud_ammo_draw(task);
                } else if (s == 1 || s == 6) {
                    hud_sprite107_draw(task);
                }
            }
            if (mem.at<uint8_t>(pl::hud_start_shown) != 0)
                hud_player_start_draw(task);
            const uint8_t s2 = mem.at<uint8_t>(G::scene_state);
            if (s2 == 2) {
                hud_lives_draw(task);
                if (mem.u32(G::ammo_enabled) != 0)
                    hud_ammo_draw(task);
            } else if (s2 == 4) {
                hud_sprite107_blink(task);
            }
            mem.at<int32_t>(rec + 0x10) = mem.at<int32_t>(rec + 0xC);
        }
    }
    hud_popup_update(task);
    if (is_demo() != 0)
        player_waiting_frame(task);
}

} // namespace hotd::port
