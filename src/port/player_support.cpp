// The robust worker's player support routines (first written in the runtime, src/game/enemy_support.cpp, robust
// 2026-10-16: they were boundary stubs), moved here for their capture proofs (proofs-4). Arcade forms only (the PC
// forms are replayed). Declarations: decl/core_bounds.h.
#include "port/game.h"

namespace hotd::port {

// 0x6640 projectile_hit_player (camera-space point, shake, side): a projectile at the screen (within +-240 x +-184 of
// the projected point) hits the player on that half (2 players: x >= 0 = player 2) or the solo player, as player_hit
// (not while invulnerable); 100 points off the player once more whether hit or not (as the original); then, unless the
// camera is at a stop or of kind 2, both players keep at least one life. 1 = at the screen, 0 = off it
template <class P> uint32_t Game<P>::projectile_hit_player(uint32_t point, int shake, int side)
{
    if constexpr (P::pc) {
        (void)point, (void)shake, (void)side;   // the PC form: not ported (vtable only; ProofPc replays it)
        return 0;
    } else {
        const float z = mem.f32(point + 8);
        const float x = mem.f32(point) * 496.0f / z, y = mem.f32(point + 4) * 496.0f / z;
        uint32_t result = 0;
        if (x > -240.0f && x < 240.0f && y > -184.0f && y < 184.0f) {
            int p = mem.template at<int16_t>(G::players_in_game) == 2 ? (x >= 0.0f ? 1 : 0)
                                                                    : int(mem.template at<int8_t>(G::solo_player_is_p2));
            if (coop4_on() && (p == 0 || p == 1))   // 4P: the hit goes round the players in play (coop4_victim)
                p = coop4_victim(p);
            if (p >= 0 && p < seat_count()) {
                const int32_t st = int32_t(mem.u32(player_rec(p)));
                const bool canHit = mem.template at<int16_t>(G::game_mode) != 4 || st == 1 || st == 4 || st == 5;
                if (canHit && !this->cheatInvincible && mem.u32(invuln_at(p)) == 0 &&   // (smallfix: Invincibility)
                    mem.template at<int16_t>(G::game_mode) != 3) {
                    const uint32_t pl = player_rec(p);
                    mem.template at<uint8_t>(G::hits_counter) = uint8_t(mem.template at<uint8_t>(G::hits_counter) - 1);
                    mem.set32(pl + 0xC, mem.u32(pl + 0xC) - 1);
                    add_score(p, -100);
                    if (shake != 0) {
                        mem.set32(pl + 0xB8, uint32_t(side));
                        mem.set32(pl + 0xB4, 1);
                    }
                    mem.set32(invuln_at(p), 0x5A);
                    if (mem.u32(G::camera_stopped) != 2 && mem.u32(G::camera_kind) != 2 && int32_t(mem.u32(pl + 0xC)) < 1)
                        mem.set32(pl + 0xC, 1);
                }
                add_score(p, -100);
            }
            result = 1;
        }
        if (mem.u32(G::camera_stopped) != 2 && mem.u32(G::camera_kind) != 2)
            for (int p = 0; p < seat_count(); ++p)
                if (int32_t(mem.u32(player_rec(p) + 0xC)) < 1)
                    mem.set32(player_rec(p) + 0xC, 1);
        return result;
    }
}

// 0x708E0 researcher_reached_camera (researcher): turns to the camera (0xC0 a frame); 1 when the camera is within 15
// on the floor (coprocessor 0x1F) (proofs-10: p10_gift, Chapter 1 event 7)
template <class P> uint32_t Game<P>::researcher_reached_camera(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
        return 0;
    } else {
        turn_toward_camera(obj, 0xC0);
        uint32_t in[6] = {mem.u32(obj + 0x20), 0, mem.u32(obj + 0x28), mem.u32(G::camera_pos), 0, mem.u32(G::camera_pos + 8)};
        Tgp::Out o;
        copro(0x1F, in, 6, o);
        return ffrom(o.w[0]) < 15.0f ? 1 : 0;
    }
}

// 0x70960 researcher_rescue_bonus: 400 points to the rescuer (0x5207B0: 0 player 1, 1 player 2, 2 one of them by
// rand() % 2, the other when that one is not in play) when in play (state 5)
template <class P> void Game<P>::researcher_rescue_bonus()
{
    if constexpr (P::pc) {
        // the PC form: not ported (vtable only; ProofPc replays it)
    } else {
        const uint32_t who = mem.u32(0x5207B0);
        const auto inPlay = [this](uint32_t p) { return mem.u32(G::players + p * 0xCC) == 5; };
        if (who == 0 || who == 1) {
            if (inPlay(who))
                add_score(int(who), 400);
            return;
        }
        if (who != 2)
            return;
        const uint32_t first = (rand() & 1) == 0 ? 0 : 1;
        if (inPlay(first))
            add_score(int(first), 400);
        else if (inPlay(1 - first))
            add_score(int(1 - first), 400);
    }
}

// 0x707E0 researcher_gift_spawn: a rescued researcher's gift: gift_effect (the extra-life item 0x3BB flying to the
// screen) at his home position, and a life by 0x5207B0 (0 player 1, 1 player 2, 2 rand() & 1). The PC (0x4CF2BC)
// gives the life to pc_last_killer and shows the effect only when a life was added (the runtime runs the arcade form)
template <class P> void Game<P>::researcher_gift_spawn(uint32_t obj)
{
    if constexpr (P::pc) {
        (void)obj;   // the PC form: not ported (vtable only; ProofPc replays it)
    } else if (pc_path()) {
        // PC Mode on the arcade logic: the PC's rule (0x4CF2BC): the life to the last killer, no random number; the
        // effect only when a life was added (docs/pcmode.md)
        const int k = pm_last_killer == 1 ? 1 : 0;
        if (player_life_add(k) != 0) {
            const uint32_t t = task_create(F::gift_effect, 0x58);
            mem.set32(t + 0x20, mem.u32(obj + 0x2D4));
            mem.set32(t + 0x24, mem.u32(obj + 0x2D8));
            mem.set32(t + 0x50, 0);
            mem.set32(t + 0x28, mem.u32(obj + 0x2DC));
        }
    } else {
        const uint32_t t = task_create(F::gift_effect, 0x58);
        mem.set32(t + 0x20, mem.u32(obj + 0x2D4));
        mem.set32(t + 0x24, mem.u32(obj + 0x2D8));
        mem.set32(t + 0x50, 0);
        mem.set32(t + 0x28, mem.u32(obj + 0x2DC));
        const uint32_t who = mem.u32(0x5207B0);
        if (who == 0)
            player_life_add(0);
        else if (who == 1)
            player_life_add(1);
        else if (who == 2)
            player_life_add((rand() & 1) == 0 ? 0 : 1);
    }
}

} // namespace hotd::port
