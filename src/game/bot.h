// Robustness testing (worker "robust", 2026-10-16): a seeded human-like player for headless runs (--bot <seed>) and
// the watchdog that looks for what a real player must never see: a crash, a soft-lock (nothing advances for 60 s of
// game time), runaway task / object counts, NaN positions. Testing only; nothing here runs in a normal game.
#pragma once

#include "game/runtime.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace hotd::game {

// one player's habits, drawn from the seed
struct BotProfile {
    float skill = 0.6f;          // 0..1: aim error, reaction
    int reaction = 20;           // frames from a new target to the first shot
    int fireGap = 10;            // frames between shots at the same target
    float nearestBias = 0.6f;    // chance to pick the nearest target (else any on screen: hostages too)
    float hostageShot = 0.02f;   // chance to take a hostage / researcher as a target (a shot costs a life)
    float wildShot = 0.05f;     // chance a shot goes anywhere on screen
    float lateReload = 0.3f;     // chance to reload late (30..150 frames after the last round)
    float earlyReload = 0.1f;    // chance to reload with rounds left
    float lapse = 1.0f / 900;    // chance per frame to stop shooting for 1..5 s (enemies hit)
    float continueChance = 0.5f; // chance to press start in the continue countdown (at a random moment, maybe late)
};

class Bot {
public:
    // players: 1 = only player 1 plays; 2 = player 2 joins and leaves at random times (also in cut-scenes)
    Bot(uint64_t seed, int players);
    void frame(Runtime& rt, uint64_t gameFrame);   // the guns and start buttons of this frame
    void restarted() { for (P& p : pl_) p.reset(); }
    int joins = 0, continues = 0, leaves = 0, shots = 0, reloads = 0, cutsceneJoins = 0;
    BotProfile profile[4];   // players 3 / 4: the 4P mode (docs/coop4.md), drawn after players 1 / 2's
    // --bot-skill (4P balance runs): player p's skill set to s (0..1) with what follows from it (aim error, reaction,
    // shots at nothing, the gap between shots: a weak player fires slowly and misses)
    void setSkill(int p, float s);
    // computer teammates (local play, user 2026-10-20): the bot plays only the seats in `mask` (bit p), at `level` 1 easy /
    // 2 normal / 3 hard / 4 expert (team 2026-11-03), as a partner: it joins at once, always continues, never leaves or
    // skips a cut-scene, never shoots hostages, picks targets by threat (teamPlay); the human seats' guns are not touched
    void teammates(uint8_t mask, const int level[4]);
    uint8_t teamMask() const { return team_; }
    // (team 2026-11-03) the game area's half width in arcade units (4:3 248, widescreen up to the window's): the
    // teammates shoot what the player sees, the widescreen sides too. The PC that plays the seat sets it (online its
    // input goes through the lockstep, so the other PCs' screens do not matter)
    void setHalfW(float w) { halfW_ = std::clamp(w, 128.0f, 1000.0f); }
    // headless measuring (the TEAM line's KILLS part): the CPU seats' kills by kind (a CPU hit, then the thing went down
    // or away within 60 frames), the hit points the bosses lost while a CPU seat hit them
    struct Tally { uint32_t bats = 0, small = 0, projectiles = 0, enemies = 0, bossHits = 0; int bossDamage = 0;
                   uint32_t seen[4] = {};   // how many of each (bats, small creatures, thrown things, others) appeared (new in the shootable list)
    };
    Tally tally;
    // online (docs/netplay.md "CPU teammates"): seat p's input for frame f without touching the game: the gun in `out`,
    // true = its start button. The PC that owns the seat asks once per input frame and sends it like a player's input
    // (rollback re-runs frames with what was sent, never asks again), so every PC runs the same frames
    bool seatInput(Runtime& rt, int p, uint64_t f, Runtime::GunInput& out);
    // The Typing of the Dead 0 (game/typing.h): the target classes and threats for its prompts (the bot itself never
    // plays then). targetKind: 0 enemy, 1 projectile, 2 boss, 3 hostage, 4 decoy, 5 dead, 6 scenery. targetThreat:
    // threat() after the frame's tracking (f: the game frame). hostageCovers: a hostage / person nearer than the
    // target within a few units of the point (a shot there could hit them)
    int targetKind(Runtime& rt, uint32_t obj) { return int(kindOf(rt, obj)); }
    float targetThreat(Runtime& rt, uint64_t f, uint32_t obj, bool& damageable, float& x, float& y)
    {
        trackFrame(rt, f);
        return threat(rt, obj, damageable, x, y);
    }
    bool hostageCovers(Runtime& rt, uint32_t target, float x, float y);
    // (The Typing of the Dead 0, totd2: the word's length by threat) how fast the object comes toward the camera, units
    // a frame, from the tracking of targetThreat (0: not tracked yet)
    float approach(uint32_t obj) const
    {
        const auto it = track_.find(obj);
        return it == track_.end() ? 0.0f : it->second.dz;
    }
    // the teammates' seed: from the game's own settings (the seats and levels), never the clock, so runs repeat
    static uint64_t teamSeed(uint8_t mask, const int level[4]);
    // a teammate's crosshair (arcade screen units, centre 0 / 0, y up) for the HUD
    void aim(int p, float& x, float& y) const { x = pl_[p].aimX; y = pl_[p].aimY; }

private:
    struct P {
        uint32_t target = 0;
        uint64_t acquired = 0, nextShot = 0, idleUntil = 0, reloadAt = 0, startAt = 0;
        float errX = 0, errY = 0;
        float aimX = 0, aimY = 0;   // where its crosshair is (arcade screen units; eased toward the target)
        int lastState = -1;
        bool absent = false;      // player 2 left: no shots until it dies out and maybe rejoins
        bool startPlanned = false;
        // teamPlay: the shots left in this burst, the frame the last threat was seen (a lull reloads), the target's
        // threat when it was taken
        int burst = 0;
        uint64_t lastThreat = 0;
        float score = 0;
        // teamPlay's hand (cpuai 2026-10-26): the spot it means to hit (a body part and a place on it, in its radius;
        // part 0: the object's centre), when the next micro-correction comes (0: still on its way), whether the spot
        // is a boss's weak part
        uint32_t spotPart = 0;
        float spotU = 0, spotV = 0;
        uint64_t corrAt = 0;
        bool weak = false;
        bool targetVis = true;   // cpu3: the target was on the screen last frame (else tracked at the edge)
        uint32_t followObj = 0, followPart = 0;   // cpu3: last frame's spot (the hand follows its move)
        float followX = 0, followY = 0;
        void reset() { *this = P(); }
    };
    P pl_[4];
    uint8_t team_ = 0;   // teammates(): the seats it plays (0: the testing bot, all seats)
    int level_[4] = {};  // teammates(): 1 easy .. 3 hard
    // teamPlay (docs/coop4.md "Smarter CPU teammates"): what a target is (by its routine, cached), how much of a
    // threat it is now (0 = not worth a shot), the frame each seat last fired on screen (where the others shoot)
    enum Kind : uint8_t { kEnemy, kProjectile, kBoss, kHostage, kDecoyKind, kDead, kScenery };
    Kind kindOf(Runtime& rt, uint32_t obj);
    // vis (cpu3): false = known but off the screen (its place clamped to the edge): tracked, never fired at
    float threat(Runtime& rt, uint32_t obj, bool& damageable, float& x, float& y, bool* vis = nullptr);
    void teamPlay(Runtime& rt, int p, uint64_t f);
    // teamPlay's spot on a target: a body part (bosses: the part that takes the most damage, "the weak spot"; others:
    // by size and damage, away from hostages) and a place on it; false: nothing of it on screen
    bool pickSpot(Runtime& rt, int p, uint32_t obj, bool boss, const std::vector<uint32_t>& hostages);
    bool spotPoint(Runtime& rt, int p, uint32_t obj, float& x, float& y, float& r);
public:
    // headless measuring (the TEAM line): CPU shots at a boss and those aimed on one of its weak parts
    uint32_t bossShots = 0, bossWeakAimed = 0, bossWeakShots = 0, heldForHostage = 0, onHostage = 0;
    // cpu3 (2026-10-28): CPU shots landing within 20 units of a hostage's sphere (near misses: the user's "shoots at
    // researchers"), pulls held for a held hostage (the captor not clearly open) / a target hidden or off screen
    uint32_t nearHostage = 0, heldCaptor = 0, heldHidden = 0;
    // the kinds audit: every routine seen shootable while a CPU seat played (its kind, the seat-frames it was on
    // screen / a candidate / the target, the CPU shots at it); the TEAM line's caller prints it (HOTD_BOT_AUDIT=1)
    void printAudit(Runtime& rt) const;
private:
    struct Audit { int type = -1; uint8_t kind = 0; uint32_t seen = 0, cand = 0, target = 0, shots = 0; };
    std::map<uint32_t, Audit> audit_;
    // the hostages' screen places (their speed on the screen: the safety margin grows with it)
    struct HostTrack { float x = 0, y = 0, vx = 0, vy = 0; uint64_t f = 0; };
    std::map<uint32_t, HostTrack> hostTrack_;
    bool isHuman(Runtime& rt, uint32_t obj);
    float hostageGap(Runtime& rt, uint32_t h, float x, float y);   // sphereGap, also where its motion takes it
    bool blocked(Runtime& rt, uint32_t target, float x, float y);  // a decoy / prop in front of the target there
    void trackFrame(Runtime& rt, uint64_t f);
    std::map<uint32_t, Kind> kind_;
    float halfW_ = 248.0f;
    // the kill counts (Tally): per object the frame of a CPU seat's last hit on it and what it is (0 bat, 1 small
    // creature, 2 projectile, 3 other enemy, 4 boss), the seats' hit counts last frame, the bosses' hit points
    struct HitMark { uint64_t f = 0; int cat = 3; int hp = 0; };
    std::map<uint32_t, HitMark> hitMark_;
    uint32_t prevHits_[4] = {};
    std::map<uint32_t, int> bossHp_;
    std::vector<uint32_t> prevShootable_;   // (Tally::seen: the shootable objects of the last frame)
    uint64_t lastBossHit_ = 0;
    int category(Runtime& rt, uint32_t obj);
    void tallyFrame(Runtime& rt, uint64_t f);
    struct Track { uint32_t routine = 0; float z = 0, dz = 0; uint64_t f = 0, down = 0; };   // down: hit points <= 0 since (0 = not)
    std::map<uint32_t, Track> track_;   // per object: its depth and how fast it comes closer (units / frame)
    uint64_t trackAt_ = ~0ull;
    uint64_t fired_[4] = {};
    int players_;
    std::mt19937_64 rng_;
    uint64_t p2NextJoin_ = 0;
    float rnd() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng_); }
    int rint(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng_); }
    float gauss(float s) { return std::normal_distribution<float>(0.0f, s)(rng_); }
    void playerFrame(Runtime& rt, int p, uint64_t f);
    bool capture_ = false, started_ = false;   // seatInput: the start button kept, not pressed
    void press(Runtime& rt, int p) { if (capture_) started_ = true; else rt.player_join(p); }
    bool isHostage(Runtime& rt, uint32_t obj);
    bool isDecoy(Runtime& rt, uint32_t obj);   // shootable, but a shot only sparks (the iron ball): left alone
    std::map<uint32_t, bool> hostage_, decoy_;
};

// the watchdog: call after each frame
struct Watchdog {
    uint64_t lastProgress = 0;     // the frame the signature last changed
    uint64_t signature = 0;
    uint64_t eventSince = 0;
    int lastEvent = -100;
    size_t maxTasks = 0, maxShootable = 0, maxDraw = 0;
    std::string problem;           // empty = all well; else the first problem ("softlock ...", "nan ...", "runaway ...")
    uint64_t problemFrame = 0;
    void check(Runtime& rt);
    size_t countTasks(Runtime& rt, bool checkNan);
};

// the point a player aims at on an object (arcade screen units, centre 0 / 0): its centre, or for bosses whose damage
// is taken by some parts only (the Magician: parts 4 / 6 / 11 / 13) one of those parts' hit spheres (part +0x5C, camera
// space, as the shot test reads it), chosen by `pick`. False when nothing of it is on screen. Testing (bots, --autofire)
bool aimPoint(Runtime& rt, uint32_t obj, uint32_t pick, float& x, float& y, float hw = 248.0f);
// The Typing of the Dead 0 (game/typing.h): a body part of the object that takes damage, on the screen (the biggest
// hit spheres first, `pick` going round them); false: none (no part tree, or none on the screen)
bool partAim(Runtime& rt, uint32_t obj, uint32_t pick, float& x, float& y);
// totd5: a landed shot's hit of `player` on the object's biggest body part that takes damage (no shot fired); false: no body
bool partHit(Runtime& rt, uint32_t obj, int player);

void installCrashHandler(const uint64_t* frameCounter);   // a crash prints "ROBUST: result=crash" and exits 3

} // namespace hotd::game
