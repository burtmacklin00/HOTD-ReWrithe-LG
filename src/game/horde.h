// Horde Mode (ours, user 2026-11-03; docs/horde.md): the arcade game as it is (the chapters, the story, the rescues, the
// bosses, the routes) with many more creatures at once, as HOTD1 Remake's Horde mode.
//
// Each creature the event script sends with the spawn opcodes (ENTB / ENTB3 / ENTBE: Game::evt_spawned) that is a
// standard walker (kind 0 / kind 9, the standard enemy's two set-ups) gets 2 / 4 / 6 copies (Horde Size Low / Normal /
// High; horde3 2026-11-05: each starts exactly where its creature started (the spawn record's place: the door, the
// corridor's end, the window the level design has) and walks its creature's own way (the record's heading, delay,
// action, start sub-state), queued behind it 0.5-3 s apart, as another walker type of the chapter now and then, paced:
// horde.cpp); the kit zombies (32) and agile ones (70) get walkers as escorts from the event's walker places. The
// waiting copies are counted (enemy_count), so the script's creature waits (WAIT_ENNUM / WAIT_ENALV, the camera stops)
// wait for them too: the kills-to-proceed, doors and chapter ends are the game's own. Every other kind (the bosses, the
// hostages and their scripted attackers, the scene creatures, props, doors) is left as the game has it.
//
// The creature limits: the original's 14 body slots are raised (Game::extraBodySlots, as Survival Mode does: slots
// 16.. in a table outside the original map, the skin buffers grown); a copy is not made while kCap creatures are alive
// (enemy_count) or the bodies are near the slots' end. Normal play is unchanged: HordeMode::size 0 and nothing runs.
#pragma once

#include "game/engage.h"

#include <cstdint>
#include <string>
#include <vector>

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
    struct Made { uint32_t task = 0, serial = 0, kind = 0; uint64_t born = 0; bool copy = false; };
    std::vector<Made> made;             // the creatures the script sent and the copies (task + serial: alive while it matches)
    EngageWatch engage;                 // survfix: copies that will never attack are sent away (game/engage.h)

    bool on() const { return size > 0; }
    // testing (the plain game's too): HOTD_HORDE_TRACE (the feel trace) or HOTD_HORDE_WALLS (horde3: the wall test,
    // with --render: every creature's step each frame against the stage's walls, horde.cpp wallProbe)
    static bool testing();
    // the game's start (before chapter_start): the extra body slots
    void begin(Runtime& rt, int sizeSetting);
    // the spawn opcodes' hook (Runtime::evt_spawned): a standard creature's copies
    void spawned(Runtime& rt, uint32_t task, uint32_t rec);
    // after each frame: the peaks
    void tick(Runtime& rt);
    std::string summary() const;

    static const char* sizeName(int s);   // "Low" / "Normal" / "High" (0: "Off")
    static int sizeParse(const std::string& v);   // low / normal / high or 1-3; -1 none

    // horde2 (user 2026-11-05, feel report: "the Remake's +100 at the hit, x2-x5, N-KILL"): the crowd feedback, Horde
    // only. A kill (Game::weapon_kill) pops its points where the creature was; each seat's chain of kills (the next
    // within kChainFrames, no life lost) raises its multiplier (x2 at 5 kills .. x5 at 20); kills close together
    // (kMultiFrames) make an "N KILL" banner. The chain's bonus (100 x the multiplier a kill) goes to the score
    static constexpr int kChainFrames = 150;   // 2.5 s between kills keeps the chain
    static constexpr int kMultiFrames = 24;    // kills within 0.4 s of each other: one multi-kill
    static constexpr int kPopFrames = 48;      // a score pop's life
    static constexpr int kBannerFrames = 75;   // the multi-kill banner's
    struct Pop { float x, y; int points, age, seat; };   // x / y: the arcade screen's (centre 0, y up)
    std::vector<Pop> pops;
    struct Chain { int kills = 0, timer = 0, mult = 1, multi = 0, multiTimer = 0, banner = 0, bannerAge = 0, lives = -1; };
    Chain chain[4];
    int bonusPoints = 0, bestChain = 0, bestMulti = 0;   // (the stats)
    void killed(Runtime& rt, uint32_t obj, int killer);  // a creature's hits killed it (Runtime::weapon_kill)

    // hordepub (user 2026-11-09: "weapons and ammo from other places"): pickups (game/weapons.h) also from a crate /
    // breakable prop broken (one in kPropChance: at the prop, as its life item), each researcher rescued (one in front of
    // the camera at its next stop) and Chapter 4's bonus room (the route for saving every researcher: kBonusWeapons
    // floating across the room)
    static constexpr int kPropChance = 3;
    static constexpr int kBonusWeapons = 3;
    void propBroken(Runtime& rt, uint32_t obj);   // Runtime::prop_broken
    int propDrops = 0, rescueDrops = 0, bonusDrops = 0;   // (the stats)
    // hordepub (user 2026-11-09: "upgrades (Survival's Big Mag, Quick Reload, ...) from crates, saving all researchers
    // of a chapter, or not taking damage for a whole chapter; announce it"): one broken crate in kUpgradeChance drops an
    // upgrade pickup (the gold coin); at a chapter's clear each seat that played it gets an upgrade (Weapons::givePerk)
    // for every researcher of the chapter saved and one for no life lost, announced in the score line's font (two
    // lines: the reason, then each seat's upgrade) for kAnnounceFrames of play
    static constexpr int kUpgradeChance = 6;
    static constexpr int kAnnounceFrames = 240;
    std::string announce[2];
    int announceAge = -1;               // (-1: none)
    int propUpgrades = 0, savedUpgrades = 0, cleanUpgrades = 0;   // (the stats)

private:
    uint32_t rng_ = 0x484F5244;   // the copies' places and delays (own stream: the game's rand() is not touched)
    uint32_t next();
    int range(int lo, int hi) { return lo + int(next() % uint32_t(hi - lo + 1)); }

    // horde3 (user 2026-11-05, played: "where the creatures come from does not make sense and they walk through walls
    // and objects"): a copy is its creature again, from the same spawn record. A template: a standard walker the script
    // sent in this chapter (its spawn routine, type, action, hit points, start sub-state): a copy takes another type of
    // the same set-up (routine, action, start sub-state, weight) so every model / motion list it takes is loaded and
    // valid and its way is its creature's (with a walk speed near its creature's, measured: walked / frames)
    struct Template {
        uint32_t routine; int16_t type, action, life; uint32_t sub;
        double walked = 0; int frames = 0;
        float speed() const { return frames >= 30 ? float(walked / frames) : 0.0f; }
    };
    std::vector<Template> pool_;
    int poolChapter_ = -1;
    // the lanes' walkers (originals and copies): their steps measure their set-up's speed; a copy walks its lane's trail
    // (lane >= 0) until its end
    struct Walker {
        uint32_t task, serial; int tmpl; float p[3]; bool have; float home[2]; uint32_t ang;
        int lane = -1; int along = 0; int gen = 0;
        bool pioneer = false;           // horde4: walks the record's heading off its place first (walkersTick)
    };
    std::vector<Walker> walkers_;
    int templateOf(uint32_t routine, int16_t type, int16_t action, int16_t life, uint32_t sub);
    void walkersTick(Runtime& rt);
    void trailsTick(Runtime& rt);
    int trailFollowers = 0, trailFreed = 0, trailPulled = 0, trailSwitched = 0, moved = 0, walkedOn = 0, pioneers = 0;
    int reserve_ = 0;                   // copies dropped (the camera moved on, no way known) that come with the next creatures   // (the stats, the trace)
    // a lane: one of the script's walkers of this event, its work area as the opcode set it (the record's place,
    // heading, delay, action, start sub-state ...). Its copies start exactly there, one after another: the next once
    // the last one from there has walked off the place (or is gone) and its gap passed, while the camera is at the stop
    // the lane's creatures walk to (it moved on: the lane's waiting copies are dropped)
    struct Lane {
        std::vector<uint32_t> work;
        uint32_t routine = 0;
        float pos[3] = {0, 0, 0};
        int16_t type = 0, action = 0, life = 0;
        uint32_t sub = 0;
        uint32_t lastTask = 0, lastSerial = 0;   // the last creature that came from here (its original, then each copy)
        uint64_t lastAt = 0;                     // ... the frame it appeared
        int nearSent = 0;                        // copies sent from here while it was near the camera (kNoCopyNear)
        bool camSet = false;
        float cam[3] = {0, 0, 0};                // the camera's stop its creatures walk to
        // the trail: where the lane's original walked (x / z, a point a unit or more apart), recorded until it dies or
        // attacks; its copies walk it (the game's own way from this place round the door posts and corners) to its end
        std::vector<float> trail;
        uint32_t trailTask = 0, trailSerial = 0;
        bool trailDone = false;
        int credit = 0;                          // copies still to come from here (made a few at a time: kPerPlace)
        int event = -1;                          // (the event it is of: the last event's are kept while copies walk them)
    };
    std::vector<Lane> lanes_;
    int lanesGen_ = 0;                  // (lanes_ cleared: the walkers' lane numbers are stale)
    int lanesEvent_ = -1;
    // a copy waits unseen (its routine the scheduler's ret) for its turn in its lane; counted as a creature meanwhile, so
    // the script's creature waits wait for it
    struct Pending {
        uint32_t task, serial, routine;
        int lane;
        int gap;                        // frames after the lane's last creature (0.5-3 s)
        uint64_t made;
        int stuck;                      // frames its lane's place stayed taken with the camera stopped
        float speed;                    // its walk speed over its creature's (the record's delay walk scaled to it)
        int16_t type;
        uint64_t at = 0;                // horde4: a placed one (lane -1): its moment, and the camera stop it was placed for
        float cam[2] = {0, 0};
    };
    // horde4 (user 2026-11-05: "copy where the Remake places its additional creatures, and add the cut creatures"): the
    // placement table (horde_places.inc, docs/horde.md "Placements"): at a camera stop of the table, its creatures are
    // placed (counted, waiting unseen) and appear at their moment, walking in from their place
    void placeAtStop(Runtime& rt);
    std::vector<int> placedStops_;      // the table's stops used in this chapter
    int placedChapter_ = -1;
    uint32_t lastStopped_ = 0;
    // hordepub: the stand table (horde_stands.inc): creatures already standing at a stop as it starts, on ground the
    // arcade's walkers walked there (horde.cpp standAtStop)
    void standAtStop(Runtime& rt);
    std::vector<int> standStops_;
    int standChapter_ = -1;
    uint32_t standStopped_ = 0;
public:
    int placed = 0, placedCut = 0;      // (the stats: the table's creatures, cut creatures among them)
    int stood = 0;                      // (hordepub: the stand table's creatures)
    static constexpr int kCutCopy = 6;  // hordepub: one copy in 6 of a creature with a cut relative is that cut creature
    int cutCopies = 0;                  // (the stats)
private:
    std::vector<Pending> pending_;
    struct Fix { uint32_t task, serial, routine; float speed; };
    std::vector<Fix> fix_;              // copies whose spawn routine runs this frame: their delay scaled after it
    std::vector<uint64_t> appeared_;   // the frames creatures appeared at (the burst limit)
    std::vector<uint64_t> appearedNear_;   // ... within kNearCam of the camera
    int pendingEvent_ = -1, pendingChapter_ = -1;
    void queueCopy(Runtime& rt, int lane, bool escort);
    float pickType(Runtime& rt, const Lane& l, uint32_t task, int16_t& type);
    void activate(Runtime& rt);
    void dropPending(Runtime& rt, int lane);
    void feedbackTick(Runtime& rt);
    void giftsTick(Runtime& rt);        // hordepub: the rescues' and the bonus room's pickups
    int rescuedSeen_ = -1, giftChapter_ = -1;
    bool bonusDone_ = false;
    int bonusPlay_ = 0;                 // (frames of play in the bonus room's event)
    int giftsDue_ = 0;                  // researchers rescued whose pickup comes at the camera's next stop
    uint32_t giftStopped_ = 0;
    void awardsTick(Runtime& rt);       // hordepub: the chapter's upgrades at its clear
    int appearedAt_ = 0, awardChapter_ = -1;
    bool played_[4] = {}, hurt_[4] = {}, clearSeen_ = false;
    int livesSeen_[4] = {-1, -1, -1, -1};
    static constexpr int kBonusDelay = 230;
public:
    int escorts = 0, pendingPeak = 0, burstHeld = 0, dropped = 0, otherType = 0;   // (the stats: escorts made, copies
                                        // waiting at most, held back, dropped (the camera moved on, the place stayed taken), another type)
    int pendingCount() const { return int(pending_.size()); }
};

} // namespace hotd::game
