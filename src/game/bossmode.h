// Boss Mode (THOTD.EXE 1998, the PC port's own mode; docs/teaser_status/bossmode.md), on our runtime (the arcade logic).
//
// The PC (front-end state 0x5CED98 = 4): the main menu's Boss Mode (0x422B90, screen 7) shows the five boss pictures;
// each handler (0x422EE4 / 0x422F24 / 0x422F64 / 0x422FA4 / 0x422FE8) sets boss_mode 0xC723B8 (2 Chariot, 3
// Hangedman, 4 Hermit, 5 Magician, 6 all four), boss_index 0xC723BC (0..3; 0 for all four), the result 0xC723C0 = 0
// and pc_boss_next 0x5CD360 = 1, and starts the game. game_new 0x458588 takes chapter / event / route from boss_starts
// 0x5CD384[boss_index] = Chariot 1 / 19, Hangedman 2 / 15, Hermit 3 / 7, Magician 4 / 4 (route 0). While pc_boss_next
// is set the main loop 0x419168 runs frames without drawing them and ENTB3 spawns nothing: the walk to the boss is
// skipped; the boss's entrance (chariot_title_card 0x517CC8, hangedman_intro 0x49A300, hermit_title_task 0x4EF3A4,
// the Magician 0x4D24A8) calls 0x412BC4: pc_boss_next = 0, the timer shown (0x5CD380 = 1), the boss song (cue 12; the
// Magician 19). The fight's clock (timeGetTime, real ms; it stops while paused) starts when the entrance hands the
// camera back (0x412B00) and stops at the boss's death (0x412B14); the HUD shows it as mm:ss:cc at (500, 440) in the
// font of flag 0x2000000 (0x418CC8). The win (0x412C08: result 1, the clock hidden) comes at the end of the death:
// the Chariot 200 frames after it (a PC-only branch of chariot_frame 0x515DBD), the Hangedman at the end of his fall
// (0x49A2D1), the Hermit (0x4EEA8A) and the Magician (0x4D2464) in their death reactions. There is no continue: when
// every player is out, game over at once (game_continue_begin 0x458808 / _countdown 0x4588A0), and the game over
// scene sets result 7 (0x51310C). The main loop then: result 1 with all four and boss_index < 3: the next boss (a new
// game: index + 1, pc_boss_next = 1, the clock kept); otherwise the time (0x412B78) against that mode's table
// (IsHighScore 0x425748: modes 2-6 keep the ten lowest times) -> the name entry (0x5CE340 = 3) or the table
// (screens 10-14, 0x423A90 ..). Result 7: the table without an entry. The clock is reset only when a game window of
// a single boss, or of all four's first, is made (0x418A25 region).
//
// Ours: the same table, start positions, the no-continue rule (Game::bossNoContinue), the next-boss chain, the clock
// and the times tables (bosstimes.cfg: the PC's defaults 0x5CD5E4 ..). The arcade logic has no PC hooks, so the
// entrance / death / win points are read from the boss life bar (life_bar_init / life_bar_update: 0x543530 up,
// 0x543531 = the hit points reached 0): the clock runs from the bar's start (the PC's 0x412B00 is a few frames
// earlier: the bar waits for the camera's stop + 20 frames) to the boss's death (as the PC); the win follows the
// death after the PC's counts (the Chariot 200 frames, the Magician 180) or when the chapter's clear starts (the
// Hangedman, the Hermit: their PC win calls sit at the end of their death sequences; a cap of 1500 frames).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hotd::game {

class Runtime;

struct BossStart { int chapter, event; };   // chapter 0-based
inline constexpr BossStart kBossStarts[4] = {{0, 19}, {1, 15}, {2, 7}, {3, 4}};   // boss_starts 0x5CD384
inline constexpr const char* kBossNames[5] = {"Chariot", "Hangedman", "Hermit", "Magician", "All Bosses"};

// a boss mode word (command line / menu): chariot hangedman hermit magician all, or 2..6; 0 = not one
int bossModeParse(const std::string& v);

class BossMode {
public:
    int mode = 0;          // 0xC723B8: 0 off, 2-5 one boss, 6 all four
    int index = 0;         // 0xC723BC: the boss being fought (0 Chariot .. 3 Magician)
    enum Phase { Approach, Entrance, Fight, Dying, Done };
    Phase phase = Approach;
    int result = 0;        // 0xC723C0: 1 won, 7 game over
    double timeMs = 0;     // 0xC723D0: the clock (ms)
    bool clockOn = false;  // 0x5CD37C != 0: running
    bool shown = false;    // 0x5CD380: the HUD shows it
    int dyingFrames = 0;   // after the boss's death: frames to the win
    bool justFinished = false;   // the game ended this frame (result 1 with the last boss, or 7)
    int players = 1;             // the players a next boss's game starts with (ours: the game's setting)
    bool nextStarted = false;    // the next boss's game (All Bosses) was set up this frame
    bool rush = false;           // Light Gun Edition: Boss Rush: All Bosses with each player's lives / score carried on (nothing refills)
    // the clock's step a game tick (ms): a local game sets it before each frame (the tick's real time, Sonic Mode's
    // share); online it is fixed at the start (1000 / the game's tick rate: game ticks, alike on every PC)
    double tickMs = 1000.0 / 60.0;

    bool active() const { return mode >= 2 && mode <= 6; }
    // the menu handlers 0x422EE4 ..: mode 2-6; the clock reset (a new window of the first boss)
    void begin(int m);
    BossStart start() const { return kBossStarts[index]; }
    // the boss's game (game_new 0x458588 in front-end state 4): chapter_start at the boss's event from the event's
    // section 0 (boss_starts +8), as the PC does: the opening section sets the room's draw zone (BGTB), its colour
    // rows (GGCOL) and plays the boss song; a start at section 1 (the arcade's game start) left the Hermit's room
    // undrawn (only Dr. Curien and his platform, the zone 0) until event 7's next BGTB (bossbug, user 2026-10-26)
    void startGame(Runtime& rt, int players) const;
    // the end of each game frame (Runtime::frame: inside the game step, so rollback's re-runs and a joiner's replay run
    // it too): false when nothing changed; true when the host must act (the next boss's game already started on the
    // runtime: nextStarted; or the end: justFinished)
    bool tick(Runtime& rt, double stepMs);
    // 0x418CC8's text "%02d:%02d:%02d" (minutes, seconds, hundredths; at most 60:00:00)
    std::string clockText() const;
    static std::string timeText(int ms);   // the tables' format (the same)
    uint32_t ms() const { return uint32_t(timeMs > 3600000.0 ? 3600000.0 : timeMs); }
};

// the five tables of boss times (scores.cfg's modes 2-6: 10 rows of 28 bytes: time ms +0, name +0xC); ours in
// bosstimes.cfg (text: one "mode name ms" line a row)
struct BossTimes {
    struct Row { std::string name; int ms = 0; };
    Row rows[5][10];
    BossTimes();                                   // the PC's defaults (0x5CD5E4, 0x5CD6FC, 0x5CD814, 0x5CD92C, 0x5CDA44)
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    bool isHigh(int mode, int ms) const;           // 0x4256A8: a row slower than ms
    int insert(int mode, const std::string& name, int ms);   // 0x4255F8: sorted in (the 11th row dropped); the rank or -1
};

} // namespace hotd::game
