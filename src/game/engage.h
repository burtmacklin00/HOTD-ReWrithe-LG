// Survival's creatures that will never attack (ours, survfix, user 2026-11-04: "creatures far away that
// never attack the player must not matter: they must not hold up a round's end / wave clear, and must not count as
// needed kills"; docs/survival.md "Creatures that never attack").
//
// Decided from the creature's own AI (src/port/enemy_ai.cpp enemy_ai, enemy.cpp enemy_main, attack_slots.cpp), never
// from distance or visibility alone. A creature MATTERS (it holds the round / the script's wait as the game has it):
//  - any routine but the standard enemy's enemy_main (0x77410): enemy_wait_camera_stop (walking in until the camera
//    stops, then the AI), the scripted kinds (kind 1 waking, kind 2 kicking, kind 8 rising into view, the door /
//    corner / hanging / dropping kinds, the bosses' helpers), dying and corpses (enemy_dying / enemy_corpse leave by
//    themselves): the game's own timing decides when they come;
//  - enemy_main in AI states 2-9: it holds a waiting or front attack position (attack_slot_take_waiting), and
//    attack_slots_update re-orders the waiting ones by depth every frame, so it reaches a front position once the
//    ones before it are gone (state 2 / 4 wait there, 3 / 5 / 7 approach, 6 / 8 attack, 9 steps back);
//  - enemy_main in state 1 (the approach: enemy_ai turns it toward its target and steps it at its walk motion's
//    speed) while its delay runs (+0x2EC: "it comes later"), while it is within 70 units of its target (the state's
//    own switch distance), or while it still gets nearer to its target (2 units or more in the last kStuckFrames).
// A creature NEVER ATTACKS when it is enemy_main and:
//  - not shootable (flags bit 0 clear: enemy_main runs enemy_ai only with it) and not dying, or in state 0 or above 9
//    (enemy_ai's switch acts on states 1-9 only: nothing ever moves it on); or
//  - in state 1, its delay over, farther than 70 units from its target and no nearer for kStuckFrames: state 1 leaves
//    only within 70 units, so a creature that has stopped getting nearer never will (stood on a standing motion,
//    walking in place against its target's side, a stale target).
// Such a creature is sent away by the game's own leave (kind10_remove 0x76FC0, as an attack's "leave afterwards":
// enemy_leave frees its attack position and target, takes it off enemy_count / enemies_alive, ends its task), so
// the counts every wait reads (Survival's round, the script's WAIT_ENNUM / WAIT_ENALV, the camera stops) let go.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hotd::game {

class Runtime;

class EngageWatch {
public:
    static constexpr int kStuckFrames = 300;    // ~5 s without getting 2 units nearer: stuck
    static constexpr float kNear = 70.0f;       // enemy_ai state 1's own distance (it moves on within it)
    // why a creature will never attack ("" = it matters), and its bookkeeping (call once a frame per creature)
    std::string check(Runtime& rt, uint32_t task, uint32_t serial);
    // the game's own leave on its next turn (kind10_remove, flags bit 21 cleared first: no route_choice)
    static void retire(Runtime& rt, uint32_t task);
    void forget(uint32_t task);
    void clear() { v_.clear(); }
    int retired = 0;                            // creatures sent away (the test line)
private:
    struct E { uint32_t task = 0, serial = 0; float best = 1e9f; uint64_t since = 0; };
    std::vector<E> v_;
};

} // namespace hotd::game
