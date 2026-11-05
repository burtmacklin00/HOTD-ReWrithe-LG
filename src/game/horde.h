// Horde Mode (ours, user 2026-11-03; docs/horde.md): the arcade game as it is (the chapters, the story, the rescues, the
// bosses, the routes) with many more creatures at once, as HOTD1 Remake's Horde mode.
//
// Each creature the event script sends with the spawn opcodes (ENTB / ENTB3 / ENTBE: Game::evt_spawned) that is a
// standard walker (kind 0 / kind 9, the standard enemy's two set-ups) gets 1-3 copies (Horde Size Low / Normal / High:
// about 2x / 3x / 4x the creatures): the same kind, record fields, type and hit points, set beside it (to its sides and a
// step behind, so they do not stack) and a moment later. They count themselves as every creature does
// (enemy_counts_add), so the script's creature waits (WAIT_ENNUM / WAIT_ENALV, the camera stops) wait for them too: the
// kills-to-proceed, doors and chapter ends are the game's own. Every other kind (the bosses, the hostages and their
// scripted attackers, the scene creatures, props, doors) is left as the game has it.
//
// The creature limits: the original's 14 body slots are raised (Game::extraBodySlots, as Survival Mode does: slots
// 16.. in a table outside the original map, the skin buffers grown); a copy is not made while kCap creatures are alive
// (enemy_count) or the bodies are near the slots' end. Normal play is unchanged: HordeMode::size 0 and nothing runs.
#pragma once

#include <cstdint>
#include <string>

namespace hotd::game {

class Runtime;

class HordeMode {
public:
    int size = 0;                       // 0 off; 1 Low, 2 Normal, 3 High (the copies of each creature)
    static constexpr int kCap = 24;     // creatures alive at once (the copies stop there; the script's own never do)
    static constexpr uint32_t kBodyTable = 0x7E1000;   // the extra body slots' table (outside the original map)
    static constexpr int kBodySlots = 26;              // body slots past the original's 14 (cap + corpses)

    // the stats (the log, the test line)
    int copies = 0;                     // copies made
    int skipped = 0;                    // copies not made (the cap, the body slots)
    int originals = 0;                  // the script's creatures that got copies
    int peakAlive = 0;                  // the most creatures alive at once (enemy_count)
    int peakBodies = 0;                 // the most body slots taken at once

    bool on() const { return size > 0; }
    // the game's start (before chapter_start): the extra body slots
    void begin(Runtime& rt, int sizeSetting);
    // the spawn opcodes' hook (Runtime::evt_spawned): a standard creature's copies
    void spawned(Runtime& rt, uint32_t task, uint32_t rec);
    // after each frame: the peaks
    void tick(Runtime& rt);
    std::string summary() const;

    static const char* sizeName(int s);   // "Low" / "Normal" / "High" (0: "Off")
    static int sizeParse(const std::string& v);   // low / normal / high or 1-3; -1 none

private:
    uint32_t rng_ = 0x484F5244;   // the copies' places and delays (own stream: the game's rand() is not touched)
    uint32_t next();
};

} // namespace hotd::game
