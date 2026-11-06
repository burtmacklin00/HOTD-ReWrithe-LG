// Declarations of class Game, included inside its body (port/game.h): the experimental 4-player mode (coop4.cpp,
// docs/coop4.md). No original routine: these run only while coop4_on().
    // --- the 4-player mode (coop4.cpp) ---
    // the per-player entries of seats 2 / 3 (players 3 / 4) next to the originals'
    static constexpr uint32_t gun_input_rec(int p) { return seat_at(G::gun_inputs, coop4::kGunInputs, 0x1C, p); }
    static constexpr uint32_t gun_shot_at(int p) { return seat_at(G::gun_shot, coop4::kGunShot, 1, p); }
    static constexpr uint32_t shot_ray_rec(int p) { return seat_at(G::shot_rays, coop4::kShotRays, uint32_t(sizeof(ShotRay)), p); }
    static constexpr uint32_t shot_result_rec(int p) { return seat_at(G::shot_results, coop4::kShotResults, uint32_t(sizeof(ShotResult)), p); }
    static constexpr uint32_t part_tally_at(int p) { return seat_at(G::part_tally, coop4::kPartTally, 2, p); }
    static constexpr uint32_t invuln_at(int p) { return seat_at(G::player_invulnerable, coop4::kInvulnerable, 4, p); }
    // a new game / chapter with four seats: the block cleared, seats 2 / 3 started (in play) or waiting (state 9)
    void coop4_reset(int playersInPlay, int lives);
    // the per-frame routine of seats 2 / 3 (their tasks' +0: kCoop4SeatRoutine): the gun, lives, the start button
    void coop4_seat_frame(uint32_t task);
    // seat 2 / 3 in play (state 5) from waiting (9, a join) or out of lives (4, a continue): the start button's part of
    // coop4_seat_frame (also Survival's return of a seat that was out: survrevive, game/survival.cpp)
    void coop4_seat_start(int p);
    // the hit table: the seats whose shot hit `target` (an object or a part) this frame (shot_hit_nearest)
    void coop4_hit_add(uint32_t target, int seat);
    uint32_t coop4_hit_seats(uint32_t target);
    // the players in play over the four seats
    int coop4_players_real();
    // the cut-scene bodies' set at a chapter load (docs/coop4.md section 3, "two sets"): players 3 / 4 alone -> types 14 / 15
    void coop4_body_set();
    // the seat an enemy's hit on side 0 / 1 goes to (phase 3: the rotation over the players in play)
    int coop4_victim(int side);
    // the seat behind player 1's / 2's hit bit (proxy 0 / 1) on `target` this frame: seat 2 / 3 when only it of that side
    // hit (the hit table), else the proxy (phase 2: credit and penalties of the kinds that read the bits)
    int coop4_shooter(uint32_t target, int proxy);
    // bosses (docs/coop4.md, user-decided): at the fight's start (the life bar) the hit points times
    // 1 + boss_step x (players - 2) for 3 / 4 players; a player joining in the fight adds base x boss_step
    // (coop4_boss_frame); 1-2 players: unchanged
    float coop4BossStep = 0.5f;   // the runtime's --4p-boss-step (tuned: docs/coop4.md "Boss balance runs")
    void coop4_boss_scale(uint32_t cur, uint32_t maxAddr);
    void coop4_boss_frame();
    // the creatures with 3 / 4 players in play (cpuai 2026-10-26, user: "3P / 4P too easy"): a hit's damage (and the
    // seats' tallies) / (1 + enemy_step x (players - 2)) in enemy_part_hits, so they take more shots and live long
    // enough to attack; not in a boss fight (the bosses: coop4BossStep); 1-2 players: unchanged
    float coop4EnemyStep = 0.5f;    // the runtime's --4p-enemy-step / launcher.ini enemy4p= / 100
    void coop4_enemy_damage();
