// The runtime's part of the enemy support routines: the player hit's log line, and the robust worker's
// projectile_hit_player / researcher_reached_camera / researcher_rescue_bonus. The routines themselves (the depth and
// attacker lists, the target point, the dynamic difficulty, dying and corpses, turning and walking) moved into the port
// (src/port/enemy_helpers.cpp, proofs-2); kind 96 runs the port's (src/port/enemy_walkers.cpp).
#include "game/runtime.h"

#include <cstdlib>

#include <string>

namespace hotd::game {

using G = A::G;

// 0x6410 player_hit (player, shake, side): the port's (src/port/player.cpp), then the log line
void Runtime::player_hit(int player, int shake, int side)
{
    if (coop4_on()) {   // 4P: the hit went to a seat of the rotation (coop4_victim): the one that became invulnerable
        uint32_t inv[4];
        for (int p = 0; p < 4; ++p)
            inv[p] = mem.u32(invuln_at(p));
        port::Game<A>::player_hit(player, shake, side);
        for (int p = 0; p < 4; ++p)
            if (log && inv[p] == 0 && mem.u32(invuln_at(p)) == 0x5A)
                log("player " + std::to_string(p + 1) + " hit: lives " + std::to_string(int32_t(mem.u32(player_rec(p) + 0xC))) + (std::getenv("HOTD_TRACE_NOTICE") ? " shake " + std::to_string(shake) + " b4 " + std::to_string(mem.u32(player_rec(p) + 0xB4)) : std::string()));
        return;
    }
    port::Game<A>::player_hit(player, shake, side);
    if (log && (player == 0 || player == 1) && mem.u32(G::player_invulnerable + uint32_t(player) * 4) == 0x5A)
        log("player " + std::to_string(player + 1) + " hit: lives " +
            std::to_string(int32_t(mem.u32(G::players + uint32_t(player) * 0xCC + 0xC))));
}

// 0x6640 projectile_hit_player: the port's (src/port/player_support.cpp, proofs-4, capture-proven), then the log line;
// researcher_reached_camera 0x708E0 / researcher_rescue_bonus 0x70960 are the port's as well
// totd5 (The Typing of the Dead 0, game/typing.cpp; null outside its ticks): true = the projectile running now (the
// current task: a thrown axe / barrel, a fireball, a Murrer's leap) was typed: it reaches the screen without a hit
// (0: as one off the screen; the kits then end it)
bool (*gTotdProjectileHold)() = nullptr;

uint32_t Runtime::projectile_hit_player(uint32_t point, int shake, int side)
{
    if (gTotdProjectileHold && gTotdProjectileHold())
        return 0;
    uint32_t inv[4];
    for (int p = 0; p < seat_count(); ++p)
        inv[p] = mem.u32(invuln_at(p));
    const uint32_t r = port::Game<A>::projectile_hit_player(point, shake, side);
    for (int p = 0; p < seat_count(); ++p)   // (4P: players 3 / 4 too)
        if (log && inv[p] == 0 && mem.u32(invuln_at(p)) == 0x5A)
            log("player " + std::to_string(p + 1) + " hit by a projectile: lives " +
                std::to_string(int32_t(mem.u32(player_rec(p) + 0xC))));
    return r;
}

} // namespace hotd::game
