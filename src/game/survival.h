// Survival Mode (ours, user 2026-11-03; docs/survival.md): endless rounds of creatures in an arena taken from a stage, the
// game's cut creatures brought back among them; played like the old Call of Duty Zombies (survival2, the user's
// 2026-11-03 brief: it was too easy): rounds that keep getting harder, points for hits and kills spent between the rounds
// (the SMG, ammo, a life, two perks), reloads by hand, few lives.
//
// Layered on the arcade logic like Boss Mode (game/bossmode.h): a normal game of the arena's chapter starts at the
// arena's event; the lead-in (the script up to the arena: the camera's walk, doors, the room's set-up) runs at once,
// undrawn and silent, while every creature the spawn opcodes make is captured instead of let loose (its place, facing
// and kind become the arena's spawn points; the task is unlinked before it first runs). At the arena's wait (the n-th
// WAIT_ENNUM / WAIT_ENALV after captures: the camera holds there in the original too, enemies coming at it) the event
// script is frozen; from then on Survival Mode's own director runs in the event task's slot each frame (so its spawns are
// made where the script's are: in the game's task group, by the same fields as ENTB3), and the game's own logic does
// the rest: the creatures walk to the attack places, attack, take hits and die as everywhere else. The rounds: a
// count, a cap of creatures at once, a spawn interval, hit points, a share of runners and a mix that rise with the
// round; every 5th round a special one; a break between rounds (the shop: signs shot to buy). The game ends when every
// player is out (no continue, as Boss Mode); its points and round go into the Survival table (survival_scores.txt).
//
// Normal play is unchanged: every Survival hook in the logic (Game::evt_spawned, Game::survivalRounds,
// Game::survivalScoring, the runtime's event task case, the input's gun, the creature limits) does nothing while
// Survival Mode is off.
#pragma once

#include "game/engage.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hotd::game {

class Runtime;

// the sound files Survival Mode plays (data/survival/<name>.wav; until they are there: stand-ins from the PC's own sounds)
inline constexpr const char* kSurvivalSfx[] = {"smg_fire", "smg_reload", "wave_start", "wave_clear"};
std::string survivalSoundDir();   // the data/survival folder (the first data folder that has one)
// a testing variable HOTD_SURVIVAL_<name>
const char* survivalEnv(const char* name);

// an arena: a place in a stage where the camera holds and creatures can come from several sides
struct SurvivalArena {
    const char* key;        // command-line word
    const char* name;       // shown
    int chapter;            // 0-based
    int event;              // the event the game starts at (its section 1, as a chapter start)
    int waitSkip;           // the arena is the (waitSkip + 1)-th creature wait of the lead-in
    std::vector<int> types; // the chapter's own standard creature types (kind 0 records of that chapter), weakest first
    // arenas (2026-11-05): the room's entrances (world x, floor y, z): doors, corridors, stairs, the far end, each on the
    // room's floor with a clear walk to the camera (checked against the stage's walls: docs/survival.md "Arenas"); the
    // creatures come from these. Empty: the places the lead-in captured (the old rule)
    std::vector<std::array<float, 3>> doors;
    // old keys still taken by --survival (the arenas replaced: bedroom, furnace)
    std::vector<const char*> aliases;
};
const std::vector<SurvivalArena>& survivalArenas();
int survivalArenaParse(const std::string& v);   // a key, an old key or a number (1-based); -1 none

// a cut creature brought back (docs/survival.md "Cut creatures"): its type, its look and how it moves
struct SurvivalCut {
    int type;               // BO_ type
    const char* name;
    int area;               // the texture area its textures are in (gallery_cut.cpp), drawn with that area's set
    int donor;              // 0: its own motion list (the ROM has one); else the type whose motion list it borrows
    int life;               // hit points (its nearest relative's)
};
const std::vector<SurvivalCut>& survivalCuts();
void survivalCutTables(Runtime& rt);   // the cut creatures' borrowed tables (survival.cpp)

class SurvivalMode {
public:
    // settings (command line / menu)
    int arena = -1;                 // index into survivalArenas(); -1 off
    uint32_t seed = 0x484F5244;     // the director's random stream ("HORD"); the same seed plays the same rounds
    int smgWave = 0;                // testing: the SMG given free from this round (0: only bought in the shop)
    bool cutCreatures = true;       // the cut creatures in the mix (only with the arcade models: the PC has no meshes)
    int maxAlive = 24;              // Survival's cap of creatures at once (the original's attack places hold 14; docs)
    bool testBuyer = false;         // testing (the self-test, the balance runs): the players buy by testBuy's rule

    enum Phase { Off, LeadIn, Break, Wave, Over };
    Phase phase = Off;
    int wave = 0;                   // the round being fought (1-based; 0 before the first)
    int special = 0;                // this round: 0 plain, 1 the runners' round, 2 the brutes' round
    int toSpawn = 0;                // this round's creatures still to come
    int waveTotal = 0;              // this round's count
    int spawnTimer = 0;             // frames to the next spawn
    int breakTimer = 0;             // frames left in the break
    int leadFrames = 0;             // frames the lead-in ran
    int waitsSeen = 0;              // creature waits passed in the lead-in
    int kills = 0;                  // creatures killed (Survival's own spawns that left play)
    int spawned = 0;                // Survival's spawns in all
    int cutSpawned = 0;             // of which cut creatures
    int fastSpawned = 0;            // of which runners (the dash from the start)
    int peakAlive = 0;              // the most creatures alive at once (enemy_count)
    int peakBodies = 0;             // the most body slots taken at once (creatures and their corpses)
    int alive = 0;                  // creatures alive now
    int score = 0;                  // the points earned by every player (at the end)
    bool smgOn = false;             // any seat holds the SMG
    bool justFinished = false;      // the game ended this frame (the host files the score)
    bool frozen = false;            // the event script is frozen (the arena is reached)
    uint64_t overFrame = 0;         // the frame the game ended
    std::string banner;             // the centre text of the moment ("ROUND 3", "1P BOUGHT THE SMG" ..), empty: none
    int bannerFrames = 0;
    // its colour (survfix2): a warning (red: a round starts, NOT ENOUGH POINTS), good news (gold: cleared, the results),
    // plain (white: the arena's name), a seat's own (what it bought)
    enum Tone { Alert, Good, Plain, Seat1, Seat2, Seat3, Seat4 };
    Tone bannerTone = Alert;

    // ---- the points and the shop (Call of Duty Zombies' rules, the game's own guns and lives) ----
    static constexpr int kHitPoints = 10;        // a frame of hits on a creature (each seat that hit it)
    static constexpr int kKillPoints = 50;       // the kill (to the game's own killer)
    static constexpr int kHeadKillPoints = 100;  // the kill with a head hit (part class 1, the game's eye / head hit)
    static constexpr int kLives = 3;             // the lives at the start (fewer when the setting is lower)
    static constexpr int kMaxLives = 5;          // bought lives up to this
    static constexpr int kBreakFrames = 480;    // the break between rounds (~8 s): the shop is open
    // the shop's items (weapons: game/weapons.h, their prices from data/weapons.ini); Smg stays item 1 (the test line)
    enum Item { Ammo, Smg, Life, Quick, BigMag, Rifle, Dual, Shotgun, Hand, kItems };
    // a sign: its name, price (weapons: weaponDefs' price), place (arcade screen units, centre 0 / 0, y up: the top of
    // its name), the weapon it sells (-1 none), the in-game model floating over it (docs/weapons.md "The shop's
    // models"), its scale and the lift that centres it in its box (a model's origin is not its middle)
    struct Sign { const char* name; int price; float x, y; int weapon; uint32_t model; float scale, lift; };
    static const Sign kSigns[kItems];
    // survfix2: a sign's stack (arcade screen units from the top of its name): the model's box above (kModelBox high,
    // its middle kModelUp over the name), the name (a cell), the price (two cells) kPriceDown under the name's top, the
    // seats that can buy it (a cell; two players or more) kSeatsDown under it, the sign's bottom kSignBottom. No part of
    // a sign overlaps another part, another sign or the HUD's corners (docs/survival.md "The shop")
    static constexpr float kSignW = 110.0f;
    static constexpr float kModelBox = 28.0f, kModelUp = 19.0f;   // (the box's bottom 5 over the name: the bob is +-3)
    static constexpr float kPriceDown = 10.0f, kSeatsDown = 28.0f, kSignBottom = 36.0f;
    struct Seat {
        int points = 0, earned = 0;            // to spend / earned in all (the score)
        int hits = 0, kills = 0, heads = 0, spent = 0;
        bool livesSet = false;
        int buys[kItems] = {};
        unsigned ownedAtClear = 0;             // the weapons held when the last round was cleared (kept: the test)
        // survrevive (user 2026-11-05, 2-4 players: "if a player loses all their lives, they come back in the next wave
        // where they get half their points back"): out until the next round (no continue; BACK NEXT ROUND), its
        // points (frozen) and weapons when it went out, the round, the magazine in the hand; back: waiting for state 5
        bool out = false, returning = false;
        int outPoints = 0, outRound = 0, outRounds = 0;
        unsigned outOwned = 0;
        uint64_t backUntil = 0;                // its "nP IS BACK" notice shows until this frame
    };
    Seat seat[4];
    int outN = 0, backN = 0;        // survrevive: seats that went out / came back this game (the summary)
    bool reviveOk = true;           // the test: each return had the start lives, half the points, the weapons held
    // (testing, HOTD_SURVIVAL_DRAIN=<seat>@<round>[+frames],..: that seat's lives to 0 that many frames (default 300)
    // into that round, as a creature's last hit would; --lives-kept leaves it out)
    struct Drain { int seat, round, frames; };
    std::vector<Drain> drains;
    static int startLives(const Runtime& rt);   // a seat's lives at its start in Survival: min(the Lives setting, kLives)
    int weaponsWave = 0;            // testing: every weapon given free from this round (0: only bought)
    bool cycleTest = false;         // testing (the self-test): each seat switches weapon every 300 frames of a round
    bool keptOk = true;             // the test: every weapon held at a round's clear was still held at the next start
    int price(int item) const;      // the sign's price (a weapon's from weaponDefs)
    bool shopOpen() const { return phase == Break && frozen && wave >= 1; }
    int signAt(float x, float y) const;         // the sign (or its model) at that aim (-1 none)
    bool canBuy(const Runtime& rt, int p, int item) const;
    bool buy(Runtime& rt, int p, int item);
    int testBuy(const Runtime& rt, int p) const;   // the test players' pick in the shop (-1 nothing)
    // the input's gun (Runtime::input_task_stand_in) for seat p, before the game and the weapons read it: the shop's
    // signs (a shot at a sign or its model buys it; the shot is not fired)
    void gunInput(Runtime& rt, int p, bool& trigger, bool onScreen, bool held, float x, float y);
    // enemy_part_hits' report (Game::survival_hits): the points
    void hits(Runtime& rt, uint32_t obj, uint32_t seats, int killer, uint32_t headSeats);

    // (survfix2, the balance runs: the points every seat had earned / still had when the round was cleared)
    struct WaveStat { int wave = 0, count = 0, cut = 0, peak = 0, special = 0, earned = 0, banked = 0, hurt = 0; uint64_t start = 0, end = 0; };   // (hurt: lives lost)
    // the balance runs (survfix2): each creature's first hit, and per weapon the kills and the frames from a
    // creature's first hit to its death (the time to kill, the killer's weapon)
    struct FirstHit { uint32_t obj; uint64_t frame; };
    std::vector<FirstHit> firstHits;
    long long ttkFrames[8] = {};
    int ttkKills[8] = {};
    std::vector<WaveStat> stats;    // one per round begun (end 0: not cleared)
    struct Point { float pos[3]; int32_t ang[3]; uint32_t flags; int kind; int type; };
    std::vector<Point> points;      // the arena's spawn points (the lead-in's captured creatures)
    std::vector<Point> section;     // captured in the lead-in since the last creature wait
    struct Mine { uint32_t task = 0, serial = 0; uint64_t born = 0; int type = 0; bool cut = false; };
    std::vector<Mine> mine;         // Survival's creatures made this game (task, its serial: alive while it matches)
    EngageWatch engage;             // survfix: the ones that will never attack are sent away (game/engage.h)
    int retiredN = 0;               // .. how many (not kills; the round does not wait for them)
    int testIdle = 0;               // testing (HOTD_SURVIVAL_TEST_IDLE): round 1's extra creatures, see testIdleFrame
    uint32_t idleTask_[2] = {}, idleSerial_[2] = {};
    float idleHeld_[2] = {};

    bool active() const { return phase != Off; }
    bool leadIn() const { return phase == LeadIn; }
    void begin(int arenaIndex);
    void startGame(Runtime& rt, int players);
    // the spawn opcodes' hook (Runtime::evt_spawned): in the lead-in, a creature is captured and unlinked
    void captured(Runtime& rt, uint32_t task, uint32_t rec);
    // the event task's slot (Runtime::run, evt_run): true = Survival ran instead of the script (frozen)
    bool eventFrame(Runtime& rt);
    // after each frame (Runtime::frame): the end of the game
    void tick(Runtime& rt);
    // the round's numbers (docs/survival.md "Rounds")
    static int roundCount(int round, int players);
    static float roundHealth(int round);
    // the HUD's title block (survfix2: one for 1-4 players, rounds and shop): the round (two cells high; in the break
    // the next one and its countdown; at the end REACHED ROUND n) and under it the status (one cell: the creatures
    // left; in the shop SHOOT AN ITEM TO BUY; at the end the points and kills), translated (lang keys)
    std::string titleLine() const;
    std::string statusLine() const;
    std::string summary(const Runtime& rt) const;   // one line: the result (the test and the log)

private:
    uint32_t rng_ = 0;
    std::vector<Point> all_;        // every place the lead-in captured
    uint32_t stallPc_ = 0;         // the lead-in: the script's place and the frames it stayed there
    int stallFrames_ = 0;
    bool reported_ = false;         // the end reported (justFinished once)
    int players_ = 1;
    int leadLives_[4] = {};         // the lives at the lead-in's start (kept through it)
    int lastLives_[4] = {};         // (the balance runs: the lives a frame ago, the round's lives lost)
    int cycleWeapon_[4] = {}, cycleReloads_[4] = {};   // cycleTest: the weapon in the hand, its reloads when picked ..
    uint64_t cycleAt_[4] = {};                         // .. and the frame
    uint32_t next();                // LCG
    int rand(int n) { return n > 0 ? int(next() % uint32_t(n)) : 0; }
    void startWave(Runtime& rt);
    void testIdleFrame(Runtime& rt);
    void spawnOne(Runtime& rt);
    int pickType(const Runtime& rt, bool& cut);
    void countMine(Runtime& rt);
    void seats(Runtime& rt);        // each frame: lives at the start, the weapons flag, the score, the shop's models
    void goOut(Runtime& rt, int p); // survrevive: seat p out until the next round (no CONTINUE?)
    void comeBack(Runtime& rt, int p);   // .. and back at the next round's start
    void say(const std::string& s, int frames, Tone tone = Alert) { banner = s; bannerFrames = frames; bannerTone = tone; }
};

// the Survival table (survival_scores.txt in the settings folder): ten rows of points, round, arena and name
struct SurvivalScores {
    struct Row { std::string name; int score = 0, wave = 0, arena = 0; };
    Row rows[10];
    SurvivalScores();
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    bool isHigh(int score) const;
    int insert(const Row& r);   // its rank (0-based) or -1
};

} // namespace hotd::game
