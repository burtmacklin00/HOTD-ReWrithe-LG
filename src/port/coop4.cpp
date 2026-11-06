// The experimental 4-player mode (docs/coop4.md): seats 2 / 3 (players 3 / 4) in the reserved block (port/coop4.h).
// No original routine: everything here runs only while coop4_on() (the runtime's --players 4), never in a 1-2 player
// game or a proof. Phase 1: the seats' records, their per-frame routine (gun, lives, start button), the hit table that
// tells the four shooters apart (shot_hit_nearest / part_hit / enemy_part_hits). Enemy targeting, bosses, continues
// and cut-scene bodies for four: later phases (docs/coop4.md section 6).
#include "port/game.h"

namespace hotd::port {

// a new game / chapter with four seats: the block cleared (the hit table empty), seats 2 / 3 in play (state 5, the
// lives setting's lives, score 0, a full gun) when within `playersInPlay`, else waiting for their start (state 9);
// their tasks are made by the runtime (InitSetPlayerTask's 4P part)
template <class P> void Game<P>::coop4_reset(int playersInPlay, int lives)
{
    if constexpr (Seats<P>::max == 4) {
        if (!coop4_on())
            return;
        if (mem.u32(coop4::kMagic) == 0x34504334u && mem.at<uint8_t>(coop4::kBodySetB) != 0)
            mem.set32(0x51F42C, 0);   // the last game's set B (coop4_body_set) back to the plain pair
        for (uint32_t a = coop4::kBase; a < coop4::kEnd; a += 4)
            mem.set32(a, 0);
        mem.set32(coop4::kMagic, 0x34504334u);   // "C4P4"
        for (int p = 2; p < 4; ++p) {
            Player& pl = mem.at<Player>(player_rec(p));
            if (p < playersInPlay) {
                pl.state = 5;
                pm_join_char(p);
                pl.lives = pl.lives_saved = pm_on() ? pm_table(pm_seat_char(p)).hp : lives;   // PC Mode: the character's
                pl.score = 0;
                pl.rounds = pm_full_rounds(p);
                pl.empty = 0;
                mem.at<uint8_t>(coop4::kSeatsJoined) = uint8_t(mem.at<uint8_t>(coop4::kSeatsJoined) | (1u << p));
            } else {
                pl.state = 9;
            }
        }
    } else {
        (void)playersInPlay, (void)lives;
    }
}

// the players in play (state 5) over the four seats
template <class P> int Game<P>::coop4_players_real()
{
    int n = 0;
    for (int p = 0; p < seat_count(); ++p)
        n += mem.at<int32_t>(player_rec(p)) == 5 ? 1 : 0;
    return n;
}

// seat 2 / 3's frame (its task's routine kSeatRoutine), the 4P counterpart of player_in_game_frame /
// player_waiting_frame: in play (state 5) the gun (player_shoot: the seat's own record, gun record and ray), out of
// lives -> state 4, the invulnerability countdown; waiting (9) or out of lives (4): the start button (bit 11 of the
// seat's switch word) joins / continues with the lives setting's lives (free play; a continue keeps the score + 1 as
// player_st1_continue; PC Mode: a credit of the Continues option, as players 1 / 2). No cut-scene body, camera drive
// or 2D HUD cells (the runtime draws seats 3 / 4's HUD).
template <class P> void Game<P>::coop4_seat_frame(uint32_t task)
{
    if constexpr (Seats<P>::max == 4) {
        const int p = mem.at<int8_t>(task + 0x20);
        if (!coop4_on() || p < 2 || p > 3)
            return;
        Player& pl = mem.at<Player>(player_rec(p));
        const uint16_t mode = mem.at<uint16_t>(G::game_mode);
        uint16_t& sw = mem.at<uint16_t>(coop4::kStartButtons + uint32_t(p - 2) * 2);
        const bool start = (sw & 0x800) != 0;
        sw = uint16_t(sw & ~0x800);
        // out of lives (state 4): the seat's own CONTINUE? countdown as player_continue_frame's (9 .. 0 at -0x2D a
        // frame, a trigger pull skips to the next digit; paused while the team's shared countdown runs, sub-mode 4);
        // at 0 the seat's game is over (state 6, then waiting: state 9); start = continue
        if (pl.state == 4 && !start) {
            if (pm_on() && pl.continue_save[0] != pm_credits) {   // PC Mode: restarts at 9 when the credits change (0x435018)
                pl.continue_save[0] = pm_credits;
                pl.continue_timer = 0x9FFF;
            }
            if (mem.at<int16_t>(G::game_submode) != 4) {
                uint32_t t = uint32_t(pl.continue_timer);
                if ((pl.pressed & 1) && int32_t(t) <= 0x7FFF)
                    t = (t & 0xFFFFF000u) + 1;
                t -= 0x2D;
                pl.continue_timer = int32_t(t);
                if (int32_t(t) <= 0) {
                    pl.continue_timer = 0;
                    pl.state = 6;
                    pl.timer = 120;   // GAME OVER shown for 2 s (the runtime's HUD), then waiting
                }
            }
            mem.at<int16_t>(coop4::kPlayersReal) = int16_t(coop4_players_real());
            return;
        }
        if (pl.state == 6) {
            if (--pl.timer <= 0)
                pl.state = 9;
            return;
        }
        if (pl.state == 9 || pl.state == 4) {
            // PC Mode: the shared credits of the Continues option, as players 1 / 2 (player_start_check: can_continue,
            // the start takes one)
            // (Survival, survrevive: a seat out until the next round cannot start: seatsHeldOut)
            if (start && mode == 4 && (!pm_on() || pm_can_continue()) && !((seatsHeldOut >> p) & 1)) {
                if (pm_on())
                    pm_credit_take();
                coop4_seat_start(p);
            }
            mem.at<int16_t>(coop4::kPlayersReal) = int16_t(coop4_players_real());
            return;
        }
        if (pl.state != 5)
            return;
        int32_t lives = mode == 3 ? 1 : pl.lives;
        if (lives <= 0)
            lives = 0;
        pl.lives = lives;
        if (lives != 0)
            player_shoot(task);
        if (mode != 8 && mode != 3 && pl.lives == 0) {
            pl.state = 4;
            pl.continue_timer = 0x9FFF;   // CONTINUE? 9 (player_st4_out_of_lives)
            if (pm_on())
                pl.continue_save[0] = pm_credits;   // the credits' snapshot (0x434F38)
        }
        pl.lives_saved = pl.lives;
        int32_t& inv = mem.at<int32_t>(coop4::kInvulnerable + uint32_t(p - 2) * 4);
        if (inv > 0)
            --inv;
        mem.at<int16_t>(coop4::kPlayersReal) = int16_t(coop4_players_real());
    } else {
        (void)task;
    }
}

// seat 2 / 3's start (coop4_seat_frame's start button, Survival's return): from state 4 a continue (the score + 1),
// else a join (score 0); the lives setting's lives (PC Mode: the character's), a full gun, PlStartInit's invulnerability
template <class P> void Game<P>::coop4_seat_start(int p)
{
    if constexpr (Seats<P>::max == 4) {
        if (p < 2 || p > 3)
            return;
        Player& pl = mem.at<Player>(player_rec(p));
        // the invulnerability of PlStartInit's row (0xA8700: 1 continue, 3 join; +4), as players 1 / 2 get it
        mem.at<int32_t>(coop4::kInvulnerable + uint32_t(p - 2) * 4) = mem.at<int16_t>(0xA8700 + (pl.state == 4 ? 1u : 3u) * 0x14 + 4);
        if (pl.state == 4)
            pl.score += 1;
        else
            pl.score = 0;
        pl.state = 5;
        pm_join_char(p);
        pl.lives = pl.lives_saved = pm_on() ? int32_t(pm_table(pm_seat_char(p)).hp)
                                            : int32_t(mem.at<uint8_t>(0x51F5B1));   // the lives setting's start lives
        pl.rounds = pm_full_rounds(p);
        pl.empty = 0;
        mem.at<uint8_t>(coop4::kSeatsJoined) = uint8_t(mem.at<uint8_t>(coop4::kSeatsJoined) | (1u << p));
        // the start's gun-cock sound, as player_start_check gives players 1 / 2 (0x6020: 0xA01354 when one
        // started; user 2026-10-26: player 3's join was silent)
        sound_send3(0xA01354);
    } else {
        (void)p;
    }
}

// the cut-scene bodies at a chapter load (before player_char_models_set; docs/coop4.md section 3, the "two sets"
// fallback until the four-body scenes exist): the simulation keeps its two bodies; on one screen the lead pair's set
// shows: set A (the title code's pair, 0x51F42C) while player 1 or 2 is in play, else set B = pair 3 of 0xCA690
// (types 14 / 15, players 3 / 4's "Rogan 2" / "G 2"). A title code's own pair (0x51F42C != 0) is left alone.
template <class P> void Game<P>::coop4_body_set()
{
    if constexpr (Seats<P>::max == 4) {
        if (!coop4_on())
            return;
        // (state 2: in play, back at its start: game_chapter_start sets it before chapter_load)
        auto inPlay = [&](int s) { const int32_t st = mem.at<int32_t>(player_rec(s)); return st == 5 || st == 2; };
        const bool setA = inPlay(0) || inPlay(1);
        const bool b = mem.at<int32_t>(player_rec(2)) == 5 || mem.at<int32_t>(player_rec(3)) == 5;
        uint8_t& mine = mem.at<uint8_t>(coop4::kBodySetB);
        if (!setA && b && mem.u32(0x51F42C) == 0) {
            mem.set32(0x51F42C, 3);
            mine = 1;
        } else if (setA && mine != 0) {
            mem.set32(0x51F42C, 0);
            mine = 0;
        }
    }
}

// the hit table (4P): which seats' shots hit `target` this frame; shot_resolve empties it each frame
template <class P> void Game<P>::coop4_hit_add(uint32_t target, int seat)
{
    const uint32_t n = mem.u32(coop4::kHitTable);
    for (uint32_t i = 0; i < n; ++i)
        if (mem.u32(coop4::kHits + i * 8) == target) {
            mem.set32(coop4::kHits + i * 8 + 4, mem.u32(coop4::kHits + i * 8 + 4) | (1u << seat));
            return;
        }
    if (n >= coop4::kHitMax)
        return;
    mem.set32(coop4::kHits + n * 8, target);
    mem.set32(coop4::kHits + n * 8 + 4, 1u << seat);
    mem.set32(coop4::kHitTable, n + 1);
}

// the seat behind a proxy bit (0: player 1's bit = seats 0 / 2, 1: player 2's = seats 1 / 3): the side's seats in the
// hit table for `target`; one of them alone -> it; both (or none in the table) -> the proxy itself
template <class P> int Game<P>::coop4_shooter(uint32_t target, int proxy)
{
    if (!coop4_on() || (proxy != 0 && proxy != 1))
        return proxy;
    const uint32_t s = coop4_hit_seats(target) & (proxy == 0 ? 0x5u : 0xAu);
    if (s == (1u << (proxy + 2)))
        return proxy + 2;
    return proxy;
}

template <class P> uint32_t Game<P>::coop4_hit_seats(uint32_t target)
{
    const uint32_t n = mem.u32(coop4::kHitTable);
    for (uint32_t i = 0; i < n; ++i)
        if (mem.u32(coop4::kHits + i * 8) == target)
            return mem.u32(coop4::kHits + i * 8 + 4);
    return 0;
}

// a boss's hit points at the fight's start (just before life_bar_init; the Chariot, the Hanged Man and the Magician,
// the Hermit): with 3 / 4 players in play both the current hit points and the maximum (the same field for the Chariot)
// times 1 + coop4BossStep x (n - 2), at most 32767; the fight's fields are kept for late joiners (coop4_boss_frame)
template <class P> void Game<P>::coop4_boss_scale(uint32_t cur, uint32_t maxAddr)
{
    if (!coop4_on())
        return;
    const int n = coop4_players_real();
    const int32_t base = mem.at<int16_t>(maxAddr);
    mem.set32(coop4::kBossCur, cur);
    mem.set32(coop4::kBossMax, maxAddr);
    mem.set32(coop4::kBossBase, uint32_t(base));
    mem.at<uint8_t>(coop4::kBossPlayers) = uint8_t(std::max(n, 2));
    if (n <= 2)
        return;
    const float f = 1.0f + coop4BossStep * float(n - 2);
    auto scale = [&](uint32_t a) { mem.at<int16_t>(a) = int16_t(std::min(32767, int(float(mem.at<int16_t>(a)) * f + 0.5f))); };
    scale(cur);
    if (maxAddr != cur)
        scale(maxAddr);
}

// each frame of play (4P): a fight's life bar on and more players in play than its hit points were scaled for (a late
// joiner): base x coop4BossStep more hit points (and maximum, and the bar's maximum 0x543536) per new player; the fight
// over (the bar off): forgotten
template <class P> void Game<P>::coop4_boss_frame()
{
    if (!coop4_on())
        return;
    const uint32_t cur = mem.u32(coop4::kBossCur);
    if (cur == 0)
        return;
    if (mem.at<uint8_t>(0x543530) == 0) {   // the life bar is gone
        mem.set32(coop4::kBossCur, 0);
        return;
    }
    const int n = coop4_players_real(), was = mem.at<uint8_t>(coop4::kBossPlayers);
    if (n <= was || mem.at<int16_t>(cur) <= 0)
        return;
    const int add = int(float(int32_t(mem.u32(coop4::kBossBase))) * coop4BossStep * float(n - was) + 0.5f);
    const uint32_t maxAddr = mem.u32(coop4::kBossMax);
    mem.at<int16_t>(cur) = int16_t(std::min(32767, mem.at<int16_t>(cur) + add));
    if (maxAddr != cur)
        mem.at<int16_t>(maxAddr) = int16_t(std::min(32767, mem.at<int16_t>(maxAddr) + add));
    mem.at<int16_t>(0x543536) = int16_t(std::min(32767, mem.at<int16_t>(0x543536) + add));
    mem.at<uint8_t>(coop4::kBossPlayers) = uint8_t(n);
}

// enemy_part_hits after its parts (4P, 3 / 4 players in play, no boss life bar): this hit's damage (hit_damage) and
// each seat's tally / (1 + coop4EnemyStep x (n - 2)), rounded to the nearest (a hit that did damage keeps at least 1).
// The part hits themselves (armour counts, blow-offs, the eyes) are the arcade's; only the life lost is less
template <class P> void Game<P>::coop4_enemy_damage()
{
    if (!coop4_on() || coop4EnemyStep <= 0.0f || mem.at<uint8_t>(0x543530) != 0)
        return;
    const int n = coop4_players_real();
    if (n <= 2)
        return;
    const int div = int(100.0f * (1.0f + coop4EnemyStep * float(n - 2)) + 0.5f);   // (integer maths from here)
    auto scale = [&](uint32_t a) {
        const int32_t v = mem.at<int16_t>(a);
        if (v > 0)
            mem.at<int16_t>(a) = int16_t(std::max(1, (v * 100 + div / 2) / div));
    };
    scale(G::hit_damage);
    for (int s = 0; s < 4; ++s)
        scale(part_tally_at(s));
}

} // namespace hotd::port
