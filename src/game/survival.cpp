// Survival Mode (ours): see survival.h and docs/survival.md
#include "game/survival.h"

#include "game/audio.h"
#include "game/lang.h"
#include "game/ragdoll.h"
#include "game/runtime.h"
#include "game/weapons.h"

#include <algorithm>
#include <bit>
#include <filesystem>
#include <functional>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace hotd::game {

using G = A::G;
using F = A::F;

namespace {
constexpr uint32_t kTaskUnlink = 0x15110;     // task_unlink: the task is freed on its turn (runtime.cpp)
constexpr uint32_t kRank = 0x51F492;          // difficulty_level's rank (0..15): the creatures' aggression
constexpr int kFirstBreak = 90;               // the break before round 1 (the arena's name)
constexpr uint32_t kBodyTable = 0x7E0000;     // Survival's body slots 16.. (outside the original map; the port's locals: 0x700000)
// survdogs: the dogs' rounds. The story's Kenfis records (kind 97, Chapter 1 event 3, the courtyard's first dogs: logged
// with HOTD_SURVIVAL_LOGCAP): E13F0 type 8 (the black dog) set-up 2 (jumping in), E1430 type 0 set-up 2, E1470 type 8
// set-up 1 (running in); each 0x40 bytes (kind, flags, place, angles, hit points 1, then its own part +0x24: type,
// set-up, the jump angle, the circling point +0x34..; the story's 37 units from its camera stop). A dog is made from a
// copy (kDogRecs, a ring: the dog keeps a pointer to its part) moved to a spawn point, facing the camera, its circling
// point kDogCircle in front of the camera
constexpr uint32_t kDogSrc[3] = {0xE13F0, 0xE1430, 0xE1470};
constexpr uint32_t kDogRecs = 0x7E1000, kDogRecSize = 0x50, kDogRing = 32;
constexpr float kDogCircle = 40.0f;
constexpr uint64_t kDogStall = 3600;           // a dog alive this long (60 s) is ended (the round never stalls)
constexpr float kNear = 110.0f;              // a spawn place at least this far from the camera
constexpr float kRoom = 450.0f;              // earlier sections' spawn places this near the camera are the arena's too
constexpr int kLeadInCap = 30000;             // the lead-in's longest run (frames) before the arena is taken as it is

// the record kinds the lead-in captures: the creatures (kinds 0-99 but 33 murrer_reset, 75 sound_repeat, 76 slider;
// 100-103 the hostages, corpses and kind 102; the slugs 141 / 142, 149 prop_creature, 153 Taran, 175 the rats); only
// the standard walkers' places (kinds 0 / 1 / 4 / 9) become spawn points
bool creatureKind(uint32_t k)
{
    return (k < 100 && k != 33 && k != 75 && k != 76) || (k >= 100 && k <= 103) || k == 141 || k == 142 || k == 149 ||
           k == 153 || k == 175;
}
bool pointKind(uint32_t k) { return k == 0 || k == 1 || k == 4 || k == 9; }

// the hit points the game gives each standard type (the commonest of its kind 0 records, all chapters; docs/survival.md)
int baseLife(int type)
{
    switch (type) {
    case 11: return 100; case 16: return 200; case 24: return 90; case 28: return 120; case 29: return 160;
    case 3: return 140; case 30: return 250; case 57: return 150; case 32: return 220; case 33: return 200;
    case 45: return 200; case 51: return 150; case 58: return 220; case 53: return 210; case 61: return 90;
    case 62: return 150; case 63: return 220; case 65: return 200; case 72: return 200; case 73: return 150;
    case 83: return 150; case 84: return 220; case 86: return 90; case 89: return 200;
    case 41: case 42: case 43: case 46: return 1;   // the small ones: one hit (kind000_enemy_spawn)
    default: return 120;
    }
}
// the motion-list action of a type's records (types 29 / 30 / 32 / 58 / 63 / 84: action 1)
int baseAction(int type) { return type == 29 || type == 30 || type == 32 || type == 58 || type == 63 || type == 84 ? 1 : 0; }
} // namespace

const std::vector<SurvivalArena>& survivalArenas()
{
    // the arenas (docs/survival.md "Arenas"): the start event and which creature wait of its lead-in; the types: the
    // chapter's standard creatures (kind 0 records), the weaker ones first (the early waves take from the front)
    static const std::vector<SurvivalArena> k = {
        // (picked from the lead-in's logged waits, HOTD_SURVIVAL_WAIT, and pictures of each: docs/survival.md)
        // arenas (user 2026-11-05: "the chapter locations are weird, especially Chapter 2: not open enough"): every
        // creature wait of the four chapters (all routes) pictured and measured (the walkable floor at the camera's
        // height in its view, the walls, the ways in); one arena a chapter, the most open; the doors: the room's
        // entrances on its floor, each with a clear walk to the camera (docs/survival.md "Arenas")
        // (the courtyard keeps the original's own places, no doors: the user 2026-11-05, "they spawn super far now, it was
        // perfect last time": the arenas pass's doors there were 700-1100 units away, the mansion gate and the far side)
        {"courtyard", "Chapter 1: the courtyard", 0, 1, 0, {11, 28, 24, 16, 57, 29, 30}, {}, {}},
        // (Chapters 2-4 removed, user 2026-11-05: "get rid of chapter 2-4 locations, they don't work"; the tried ones,
        // the green bedroom / dining hall, the laboratory / furnace bridge and the octagon hall with their door lists,
        // are in git history (2188e467) and docs/survival.md "Arenas")
        // survmaps (user 2026-11-09; dev builds only until the user tries them: main.cpp survivalArenaCount)
        // Chapter 3's platform under the overhead rail (event 1, the first stop on it, camera 466 / -282 / 561): the
        // story's three hanging zombies (kind 7: down their chains on the rail, then the jump off; types 28, 28, 65) come
        // again on their own chains, 60 % of the spawns; the rest walk in from the platform's far end (its floor:
        // x 405..573, z 535..858 at y -282, the railing at z 836); the story's own view of the stop
        // (user 2026-11-10: "I only picked that location wise": no chain zombies; everyone walks in from the far end)
        // fix140b (user 2026-11-10, v1.4.0: "creatures spawn too far away"): the far end was 253-264 units from the
        // camera, a 19-21 s walk to the player (the courtyard's places: 110 units, 4.3 s); now three places on the
        // platform's floor 124-128 units out (about 5 s), each a clear walk to the camera (0 wall triangles crossed)
        {"platform", "Chapter 3: the platform", 2, 1, 0, {28, 61, 62, 29, 53, 72},
         {{{425, -282, 678}}, {{470, -282, 688}}, {{530, -282, 672}}}, {}, 7, -1, 0, {}},
        // Chapter 4's roof, the Magician's fight (event 4: the wait after he comes, kind 20, is taken; he is not
        // there): the camera on the roof (its floor x -1021..-561, z 2274..2559 at y 0) facing the doorway he blasted
        // open out of Curien's laboratory (x -906..-676 at z 2253..2274); the creatures come out of it, from the
        // laboratory's floor just inside
        // fix140b (user 2026-11-10, v1.4.0: "creatures spawn too far away"): inside the doorway was 242-245 units
        // from the camera (a 19 s walk); now on the roof's floor before it, 125-126 units out (as the courtyard's 110)
        {"roof", "Chapter 4: the Magician's roof", 3, 4, 0, {11, 53, 72},
         {{{-848, 0, 2357}}, {{-792, 0, 2345}}, {{-736, 0, 2357}}}, {}, 20, -1, 0,
         {-791.9f, 15.0f, 2470.0f, -791.9f, 25.0f, 2264.0f}},
    };
    return k;
}

int survivalArenaParse(const std::string& v)
{
    const auto& a = survivalArenas();
    for (size_t i = 0; i < a.size(); ++i) {
        if (v == a[i].key) return int(i);
        for (const char* old : a[i].aliases)
            if (v == old) return int(i);
    }
    const int n = std::atoi(v.c_str());
    return n >= 1 && n <= int(a.size()) ? n - 1 : v.empty() ? 0 : -1;
}

const std::vector<SurvivalCut>& survivalCuts()
{
    // docs/cut_content.md: no event record spawns these types; the ROM keeps their skeletons, part models (with
    // textures, gallery_cut.cpp) and for three of them a motion list (enemy_motion_lists 0xC6B60): Disiprin the
    // zombies' (0xC6B00), Neozom Zom's (0xC6B10), Debu Bentley's (0xC6B30). Syndy and Baba (18 joints like the
    // zombies, no list) borrow one; the area: where their textures are. hordepub (user 2026-11-09: "Syndy's animation
    // when she is approaching the player looks wrong"): she borrowed Neil's (28) list, the plain zombies' row 0xC6B00
    // (stance MO_z_a_kamae2a, walk MO_z_a_walk7a: a shamble with the arms swinging free), so the long mallet in her
    // hand (her bagbag parts) flailed about at her hip with every step, at her type's scale 1.2 (0x118060); her own
    // only motion, MO_synd_demo (452), is a standing pose (hands on the hips: the Gallery), no walk. Now Cyril's (29)
    // row with his axe set-up (action 1: MO_z_ono_kamae / _walk / _back, the two-handed weapon carried in front, and
    // his axe attacks): the mallet is held and swung as a weapon
    static const std::vector<SurvivalCut> k = {
        {52, "Disiprin", 2, 0, 160},
        {12, "Debu", 5, 0, 220},
        {56, "Neozom", 6, 0, 120},
        {10, "Syndy", 7, 29, 180, 1},
        {66, "Baba", 3, 29, 140},
    };
    return k;
}

// the cut creatures' borrowed tables and damage rows (written into the program ROM image, set back by
// Runtime::survival_restore): Survival's start (and the other modes' placed cut creatures)
void survivalCutTables(Runtime& rt)
{
    rt.survival_restore();
    // (and its attacks 0xC6F10, attack choice 0xC6D30 and death motion 0x762C0: Syndy's and Baba's words are 0 there,
    // where Disiprin's, Debu's and Neozom's are set like any standard creature's)
    for (const SurvivalCut& c : survivalCuts())
        if (c.donor != 0)
            for (const uint32_t table : {G::enemy_motion_lists, G::enemy_attacks, G::enemy_attack_choice, G::enemy_death_motions}) {
                const uint32_t at = table + 4 * uint32_t(c.type);
                rt.survivalPatches_.push_back({at, rt.mem.u32(at)});
                rt.mem.set32(at, rt.mem.u32(table + 4 * uint32_t(c.donor)));
            }
    // their floor shadows (user 2026-11-05: "the hammer lady's shadow is just a squished-down Y level of her"): part_draw's
    // shadow pass (0x36240, flags2 bit 1) draws, under the shadow projection (stored matrix 3), the model of the part's
    // role from the type's shadow table (0x14BA80: the low-polygon dark `_DD` / `*kage` copies), or, where the type has
    // no table, the part's own textured model squashed onto the floor. Syndy's word is 0 (as the cut MrG's and the
    // Hangedman's / Hermit's types), so she cast a flat copy of herself; every creature that shares her 17-role tree
    // (Sam, Neil, Gilmore, ...) has the zombies' table 0x14A730 (ff_*_DD), which her donor's word gives her: the same
    // shadow, by her own pose, as the others (docs/survival.md "Cut creatures")
    for (const SurvivalCut& c : survivalCuts()) {
        constexpr uint32_t kShadowModels = 0x14BA80;
        const uint32_t at = kShadowModels + 4 * uint32_t(c.type), donor = kShadowModels + 4 * uint32_t(c.donor);
        if (c.donor != 0 && rt.mem.u32(at) == 0 && rt.mem.u32(donor) != 0) {
            rt.survivalPatches_.push_back({at, rt.mem.u32(at)});
            rt.mem.set32(at, rt.mem.u32(donor));
        }
    }
    // their damage (user 2026-11-03: Syndy, the tall woman with the mallet, could not be hurt; Baba's body "glitched out"
    // when shot; docs/survival.md "Cut creatures"). The per-type part tables (part_hit, damage.cpp) for the two:
    // - part_damage_table (0x12F9F0, 0x100 bytes a type: the damage of a hit by part class and damage step): Syndy's
    //   are all 0, so a hit did only the difficulty bonus (0x14AAF0: +20..30 at rank 0, 0 or less from rank 6 / 7: she
    //   died in the early test waves and never later); Baba's are 10 for every class. Both now take their donor's.
    // - part_roles (0x1184E0, 31 x 8 bytes a type): Baba's limbs and head have armour 1 and sever 1 but its
    //   part_replacement rows (0x11DFF0) are empty (no stump or damage models of its own: the donor's are another
    //   creature's meshes), so the first hit on each cut it off with nothing in its place (the skinned body stretched to
    //   the lost joints). Every role is set as Syndy's and Disiprin's are: armour 0xFF (no damage steps), sever 0 (no
    //   part loss): the body takes hits and dies whole.
    const auto patch = [&](uint32_t at, uint32_t v) {
        rt.survivalPatches_.push_back({at, rt.mem.u32(at)});
        rt.mem.set32(at, v);
    };
    for (const SurvivalCut& c : survivalCuts()) {
        if (c.donor == 0)
            continue;
        const uint32_t t = uint32_t(c.type), d = uint32_t(c.donor);
        for (uint32_t i = 0; i < 0x100; i += 4)
            patch(G::part_damage_table + (t << 8) + i, rt.mem.u32(G::part_damage_table + (d << 8) + i));
        for (uint32_t r = 1; r < 31; ++r) {
            const uint32_t at = G::part_roles + (t * 31 + r) * 8;   // PartRole: +2 armour, +4 sever (little-endian words)
            if (rt.mem.u32(at) == 0 && rt.mem.u32(at + 4) == 0)
                continue;
            patch(at, rt.mem.u32(at) | 0x00FF0000u);
            patch(at + 4, rt.mem.u32(at + 4) & 0xFFFFFF00u);
        }
    }
}

uint32_t SurvivalMode::next()
{
    rng_ = rng_ * 1103515245u + 12345u;
    return (rng_ >> 8) & 0xFFFFFF;
}

uint64_t SurvivalMode::digest() const
{
    uint64_t h = 0xCBF29CE484222325ull;   // (FNV-1a over the logic fields' values; no texts: the banner is translated)
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i, v >>= 8)
            h = (h ^ (v & 0xFF)) * 0x100000001B3ull;
    };
    auto two = [&mix](int a, int b) { mix(uint64_t(uint32_t(a)) | uint64_t(uint32_t(b)) << 32); };
    two(arena, int(phase));
    two(wave, special);
    two(toSpawn, waveTotal);
    two(spawnTimer, breakTimer);
    two(leadFrames, waitsSeen);
    two(kills, spawned);
    two(cutSpawned, fastSpawned);
    two(alive, score);
    two(maxAlive, players_);
    mix(uint64_t(smgOn) | uint64_t(frozen) << 1 | uint64_t(reported_) << 2 | uint64_t(cutCreatures) << 3 |
        uint64_t(testBuyer) << 4 | uint64_t(rng_) << 32);
    mix(overFrame);
    for (int p = 0; p < 4; ++p) {
        const Seat& s = seat[p];
        two(s.points, s.earned);
        two(s.hits, s.kills);
        two(s.heads, s.spent);
        for (int it = 0; it < kItems; ++it)
            mix(uint32_t(s.buys[it]));
        two(int(s.ownedAtClear), int(s.livesSet) | int(s.out) << 1 | int(s.returning) << 2);
        two(s.outPoints, s.outRound);
        two(s.outRounds, int(s.outOwned));
        two(leadLives_[p], lastLives_[p]);
    }
    two(outN, backN);
    two(retiredN, engage.retired);
    mix(points.size() | uint64_t(mine.size()) << 32);
    for (const Mine& m : mine) {
        mix(m.task | uint64_t(m.serial) << 32);
        mix(m.born);
        two(m.type, m.cut);
    }
    two(int(entries.size()), entrySpawned);   // (survmaps: the chains' entry records)
    for (const Entry& e : entries) {
        two(int(e.rec), e.type);
        mix(e.freeAt);
    }
    two(dogWave, dogRounds);   // (survdogs: the dogs' rounds, their stream, Max Ammo)
    two(dogSpawned, dogKilled);
    two(dogStalled, refills);
    mix(dogRng_);
    for (const Mine& m : mine)
        mix(uint64_t(m.dog));
    return h;
}

void SurvivalMode::begin(int arenaIndex)
{
    const int keepArena = arenaIndex, keepSmg = smgWave, keepMax = maxAlive, keepWeapons = weaponsWave;
    const bool keepCut = cutCreatures, keepBuyer = testBuyer, keepCycle = cycleTest;
    const uint32_t keepSeed = seed;
    *this = SurvivalMode();
    arena = keepArena, smgWave = keepSmg, maxAlive = keepMax, cutCreatures = keepCut, seed = keepSeed, testBuyer = keepBuyer;
    testIdle = survivalEnv("TEST_IDLE") ? 1 : 0;
    weaponsWave = keepWeapons, cycleTest = keepCycle;
    // testing (survrevive): HOTD_SURVIVAL_DRAIN=<seat>@<round>[+frames],.. (1-based seat)
    for (const char* d = survivalEnv("DRAIN"); d && *d;) {
        Drain x{0, 0, 300};
        char* e = nullptr;
        x.seat = int(std::strtol(d, &e, 10)) - 1;
        if (e && *e == '@')
            x.round = int(std::strtol(e + 1, &e, 10));
        if (e && *e == '+')
            x.frames = int(std::strtol(e + 1, &e, 10));
        if (x.seat >= 0 && x.seat < 4 && x.round >= 1)
            drains.push_back(x);
        d = e && *e == ',' ? e + 1 : "";
    }
    rng_ = seed;
    // survdogs: the first dogs' round 7 or 8, from a stream of its own ("DOGS" over the seed: the same seed, the same
    // rounds; the director's draws untouched). Testing: HOTD_SURVIVAL_DOG_WAVE=n the first one at round n
    dogRng_ = seed ^ 0x444F4753u;
    dogWave = dogNext();
    if (const char* d = survivalEnv("DOG_WAVE"); d && std::atoi(d) >= 1)
        dogWave = std::atoi(d);
    phase = arena >= 0 ? LeadIn : Off;
}

void SurvivalMode::startGame(Runtime& rt, int players)
{
    begin(arena);
    if (!active())
        return;
    SurvivalArena a = survivalArenas()[size_t(arena)];
    // developer (arenas, finding arenas): HOTD_SURVIVAL_START=c:e starts at chapter c's (1-based) event e instead (with
    // HOTD_SURVIVAL_WAIT: any wait of any chapter; the types stay the arena's)
    if (const char* s = survivalEnv("START")) {
        int c = 0, e = 0;
        if (std::sscanf(s, "%d:%d", &c, &e) == 2 && c >= 1 && c <= 4 && e >= 0)
            a.chapter = c - 1, a.event = e;
    }
    rt.bossNoContinue = true;   // no continue: game over as soon as every player is out (as Boss Mode)
    rt.seatsHeldOut = 0, rt.livesKeptSkip = 0;
    players_ = std::max(1, players);
    rt.weapons.begin(rt, Weapons::Survival);   // (the handgun's 6 until the shop gives more: game/weapons.h)
    rt.survivalScoring = true;                 // Survival's points instead of the game's score (game.h survival_hits)
    // the cut creatures without a list of their own borrow their relative's (enemy_motion_lists 0xC6B60, a table in the
    // program ROM image: set back by Runtime::survival_restore when the game ends or another starts)
    survivalCutTables(rt);
    // more bodies than the original's 14 creature slots (game.h extraBodySlots; docs/survival.md "Limits"): slots 16.. in
    // a table outside the original map (the skin buffers grow with it at the chapter's heap reset)
    if (!rt.mem.mapped(kBodyTable))
        rt.mem.map(kBodyTable, 0x1000);
    for (uint32_t i = 0; i < 0x1000; i += 4)
        rt.mem.set32(kBodyTable + i, 0);
    rt.extraBodyTable = kBodyTable;
    rt.extraBodySlots = std::max(0, maxAlive + 8 - 14);   // (+8: corpses, the uncaptured, the players' own)
    // survdogs: the dogs' record copies (outside the original map, as the body table; in the snapshots: mem's regions)
    if (!rt.mem.mapped(kDogRecs))
        rt.mem.map(kDogRecs, kDogRecSize * kDogRing);
    for (uint32_t i = 0; i < kDogRecSize * kDogRing; i += 4)
        rt.mem.set32(kDogRecs + i, 0);
    // from the event's section 0 (its BGTB / GGCOL: the room's draw zone and colours), as Boss Mode starts (bossbug)
    rt.startAtSection0 = a.event != 0;
    rt.chapter_start(a.chapter, a.event, players);
    rt.survival_areas();
    if (rt.log)
        rt.log("survival: " + std::string(a.name) + " (chapter " + std::to_string(a.chapter + 1) + " event " + std::to_string(a.event) +
               "), seed " + std::to_string(seed));
}

void SurvivalMode::captured(Runtime& rt, uint32_t task, uint32_t rec)
{
    if (phase != LeadIn || task == 0)
        return;
    const uint32_t kind = rt.mem.u32(rec);
    if (survivalEnv("LOGCAP") && rt.log) {   // developer (survmaps: finding arenas): each capture, its record and the camera
        char b[300];
        std::snprintf(b, sizeof b, "survcap: f%llu ev %d kind %u type %d rec %X pos %.0f %.0f %.0f ang %d | %08X %08X %08X %08X %08X %08X %08X %08X",
                      (unsigned long long)rt.frameCount, rt.mem.at<int8_t>(G::event_index), kind, rt.mem.at<int16_t>(task + 0xCC), rec,
                      rt.mem.f32(task + 0x20), rt.mem.f32(task + 0x24), rt.mem.f32(task + 0x28), int(rt.mem.u32(task + 0x30)), rt.mem.u32(rec + 4),
                      rt.mem.u32(rec + 8), rt.mem.u32(rec + 12), rt.mem.u32(rec + 16), rt.mem.u32(rec + 20), rt.mem.u32(rec + 24), rt.mem.u32(rec + 28),
                      rt.mem.u32(rec + 32));
        rt.log(b);
        std::snprintf(b, sizeof b, "survcap2: cam %.1f %.1f %.1f | +24: %08X %08X %08X %08X %08X %08X %08X %08X", rt.mem.f32(G::camera_pos),
                      rt.mem.f32(G::camera_pos + 4), rt.mem.f32(G::camera_pos + 8), rt.mem.u32(rec + 0x24), rt.mem.u32(rec + 0x28), rt.mem.u32(rec + 0x2C),
                      rt.mem.u32(rec + 0x30), rt.mem.u32(rec + 0x34), rt.mem.u32(rec + 0x38), rt.mem.u32(rec + 0x3C), rt.mem.u32(rec + 0x40));
        rt.log(b);
    }
    if (!creatureKind(kind))
        return;
    // survmaps: the arena's take kind seen (its wait comes next), its entry kind's records kept (their type: kind 7's
    // first byte of its own part, rec + 0x24)
    if (arena >= 0) {
        const SurvivalArena& ar = survivalArenas()[size_t(arena)];
        if (int(kind) == ar.takeKind)
            takeSeen_ = true;
        if (int(kind) == ar.entryKind)
            sectionEntries_.push_back({rec, rt.mem.at<uint8_t>(rec + 0x24), 0});
    }
    if (pointKind(kind)) {
        Point p{};
        for (int i = 0; i < 3; ++i) {
            p.pos[i] = rt.mem.f32(task + 0x20 + 4 * uint32_t(i));
            p.ang[i] = int32_t(rt.mem.u32(task + 0x2C + 4 * uint32_t(i)));
        }
        p.flags = rt.mem.u32(task + 0x58);
        p.kind = int(kind);
        p.type = rt.mem.at<int16_t>(task + 0xCC);
        section.push_back(p);
        all_.push_back(p);
    }
    rt.mem.set32(task, kTaskUnlink);   // never runs: freed on its turn
}

bool SurvivalMode::eventFrame(Runtime& rt)
{
    if (phase == Off)
        return false;
    if (phase == LeadIn) {
        ++leadFrames;
        // the lead-in is not played: a hit there (a creature it did not capture, e.g. Chapter 2's on the way to the
        // bedroom) costs no life. The lives of its first frame are kept (seats(): at most kLives at the arena)
        for (int p = 0; p < rt.seat_count() && p < 4; ++p) {
            port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
            if (leadFrames == 1)
                leadLives_[p] = pl.lives;
            else if (pl.lives < leadLives_[p])
                pl.lives = pl.lives_saved = leadLives_[p];
        }
        const uint32_t pc = rt.mem.u32(G::evt_pc);
        const uint32_t op = rt.mem.u32(G::evt_handlers + rt.mem.u32(pc) * 4);
        const bool creatureWait = op == F::evt_WAIT_ENNUM || op == F::evt_WAIT_ENALV;
        if (survivalEnv("LOGCAP") && rt.log && pc != stallPc_) {   // developer (HOTD_SURVIVAL_LOGCAP): each script step of the lead-in
            char b[200];
            std::snprintf(b, sizeof b, "survpc: f%llu pc %X %s %08X cam %.1f %.1f %.1f", (unsigned long long)rt.frameCount, pc, rt.routineName(op).c_str(),
                          rt.mem.u32(pc + 4), rt.mem.f32(G::camera_pos), rt.mem.f32(G::camera_pos + 4), rt.mem.f32(G::camera_pos + 8));
            rt.log(b);
        }
        bool take = false;
        // survmaps: an arena by its take kind counts only the creature waits after a capture of that kind
        const int takeKind = survivalEnv("START") || survivalEnv("WAIT") ? -1 : survivalArenas()[size_t(arena)].takeKind;
        if (creatureWait && takeKind >= 0 && !takeSeen_)
            sectionEntries_.clear();   // (a wait for other creatures: the entries are the ones just before the arena's)
        if (creatureWait && (takeKind >= 0 ? takeSeen_ : !section.empty())) {
            // developer: HOTD_SURVIVAL_WAIT=n takes the (n + 1)-th wait instead (finding arenas: each wait is logged)
            static const int waitForce = survivalEnv("WAIT") ? std::atoi(survivalEnv("WAIT")) : -1;
            if (rt.log) {
                char b[160];
                std::snprintf(b, sizeof b, "survival: lead-in wait %d at frame %llu: event %d, %zu points, camera %.0f %.0f %.0f", waitsSeen + 1,
                              (unsigned long long)rt.frameCount, rt.mem.at<int8_t>(G::event_index), section.size(),
                              rt.mem.f32(G::camera_pos), rt.mem.f32(G::camera_pos + 4), rt.mem.f32(G::camera_pos + 8));
                rt.log(b);
            }
            if (waitsSeen >= (waitForce >= 0 ? waitForce : survivalArenas()[size_t(arena)].waitSkip))
                take = true;
            else {
                ++waitsSeen;
                section.clear();
                takeSeen_ = false;
                sectionEntries_.clear();
            }
        }
        // the lead-in's own waits that only the captured creatures would have ended: after 2 s at one place, a wait on
        // an event flag (one a creature sets: kind 1's wake ...) gets its flag, a creature wait its camera stop done
        // (enemy_left_route: the camera's turn back after the attackers, which never came)
        stallFrames_ = pc == stallPc_ ? stallFrames_ + 1 : 0;
        stallPc_ = pc;
        if (stallFrames_ == 120 && rt.log) {
            char b[160];
            std::snprintf(b, sizeof b, "survival: lead-in stalled at 0x%X (%s) frame %llu: alive %d count %d route %d", pc,
                          rt.routineName(op).c_str(), (unsigned long long)rt.frameCount, rt.mem.at<int16_t>(G::enemies_alive),
                          rt.mem.at<int16_t>(G::enemy_count), rt.mem.at<uint8_t>(G::enemy_left_route));
            rt.log(b);
        }
        if (stallFrames_ >= 120 && !take) {
            if (op == F::evt_WAIT_ITEM)
                rt.mem.at<uint8_t>(G::event_flags + rt.mem.u32(pc + 4)) = 1;
            else if (creatureWait)
                rt.mem.at<uint8_t>(G::enemy_left_route) = 1;
            // a creature not captured (made by another object) holds the wait: passed as the teaser's guard does
            if (creatureWait && stallFrames_ >= 300) {
                rt.mem.set32(G::evt_pc, pc + 8);
                rt.mem.set32(G::evt_yield, 0);
            }
        }
        if (!take && leadFrames < kLeadInCap)
            return false;
        // the spawn points: this wait's creatures' places, and those of earlier sections near the camera (the same room:
        // the original sends the room's creatures in several sections), at most 16, none twice
        points = section;
        // arenas (2026-11-05): an arena with its doors takes them instead (the captured places were where the original's
        // creatures stood up close, or behind walls in the next room, or on another floor): each faces the camera, with
        // the flags of the arena's own creatures
        const SurvivalArena& ar = survivalArenas()[size_t(arena)];
        // (fix140b: an arena with its own view measures from that eye, where the camera is set below: the roof's lead-in
        // camera stood 66 units nearer the doorway, so places near the arena's eye were pushed out by kNear from it)
        const bool ownView = ar.view != std::array<float, 6>{} && !survivalEnv("START");
        const float cx = ownView ? ar.view[0] : rt.mem.f32(G::camera_pos), cz = ownView ? ar.view[2] : rt.mem.f32(G::camera_pos + 8);
        const bool doors = !ar.doors.empty() && !survivalEnv("START") && !survivalEnv("WAIT");   // (finding arenas: the old rule)
        if (doors) {
            const uint32_t fl = section.empty() ? 0u : section.front().flags;
            points.clear();
            for (const auto& d : ar.doors) {
                Point q{};
                q.pos[0] = d[0], q.pos[1] = d[1], q.pos[2] = d[2];
                // the game's heading: 0x10000 a turn, 0 = +z, a quarter turn = -x (the records' own: ENTB3's +0x2C)
                q.ang[1] = int32_t(std::lround(-std::atan2(cx - d[0], cz - d[2]) / 6.2831853 * 65536.0)) & 0xFFFF;
                q.flags = fl, q.kind = 0, q.type = section.empty() ? 0 : section.front().type;
                points.push_back(q);
            }
        }
        for (const Point& q : doors ? std::vector<Point>{} : all_) {
            const float dx = q.pos[0] - cx, dz = q.pos[2] - cz;
            bool dup = false;
            for (const Point& r : points)
                dup = dup || (std::abs(r.pos[0] - q.pos[0]) < 10.0f && std::abs(r.pos[2] - q.pos[2]) < 10.0f);
            if (!dup && dx * dx + dz * dz < kRoom * kRoom && points.size() < 16)
                points.push_back(q);
        }
        // a place right at the camera (the original's creatures there are scripted to step up close) moved out to
        // kNear along its own direction (a creature born in the player's face could not be shot in time); with fewer
        // than 4 places, each also turned 30 degrees either way about the camera (more sides to come from)
        for (Point& q : points) {
            const float dx = q.pos[0] - cx, dz = q.pos[2] - cz, d = std::sqrt(dx * dx + dz * dz);
            if (d < kNear && d > 0.01f)
                q.pos[0] = cx + dx / d * kNear, q.pos[2] = cz + dz / d * kNear;
        }
        if (points.size() < 4 && !doors)
            for (size_t i = 0, n = points.size(); i < n; ++i)
                for (const float a : {0.52f, -0.52f}) {
                    Point q = points[i];
                    const float dx = q.pos[0] - cx, dz = q.pos[2] - cz;
                    q.pos[0] = cx + dx * std::cos(a) - dz * std::sin(a);
                    q.pos[2] = cz + dx * std::sin(a) + dz * std::cos(a);
                    q.ang[1] = int32_t(q.ang[1] + int32_t(a / 6.2831853f * 65536.0f));
                    points.push_back(q);
                }
        if (points.size() < 2 && rt.log)
            rt.log("survival: few spawn points in the arena (" + std::to_string(points.size()) + ")");
        // survmaps: the entry records (each once) and the arena's own view: the game camera's stop point (the eye held
        // there, the look-at its rest: game_cam_look_at_attackers / _attackers_midpoint) and the view record 0 at once
        entries.clear();
        for (const Entry& e : sectionEntries_) {
            bool dup = false;
            for (const Entry& f : entries)
                dup = dup || f.rec == e.rec;
            if (!dup)
                entries.push_back(e);
        }
        if (ownView) {
            // record 0's eye / look-at, its angles by the game's own look-at (camera_look_at), copied to the stop point;
            // camera_pos at the eye less the record's height and the heading its yaw (as camera_frame_mode4 sets them:
            // the attack positions are placed from these)
            const uint32_t r0 = G::camera_records, sp = G::game_cam_stop_point;
            for (int i = 0; i < 3; ++i) {
                rt.mem.set32(r0 + 4 * uint32_t(i), std::bit_cast<uint32_t>(ar.view[size_t(i)]));
                rt.mem.set32(r0 + 0x18 + 4 * uint32_t(i), std::bit_cast<uint32_t>(ar.view[size_t(3 + i)]));
            }
            rt.camera_look_at(r0, r0 + 0x18, 0);
            for (uint32_t i = 0; i < 0x30; i += 4)
                rt.mem.set32(sp + i, rt.mem.u32(r0 + i));
            rt.mem.set32(G::camera_pos, rt.mem.u32(r0));
            rt.mem.set32(G::camera_pos + 4, std::bit_cast<uint32_t>(ar.view[1] - rt.mem.f32(r0 + 0xD8)));
            rt.mem.set32(G::camera_pos + 8, rt.mem.u32(r0 + 8));
            rt.mem.set32(G::camera_heading, rt.mem.u32(r0 + 0x10));
            rt.mem.set32(G::camera_heading + 4, 0);
        }
        if (rt.log) {
            char b[200];
            const uint32_t r0 = G::camera_records;
            std::snprintf(b, sizeof b, "survival: arena camera: mode %u sub-state %u job %s, eye %.1f %.1f %.1f look %.1f %.1f %.1f, %zu entry records",
                          rt.mem.u32(G::camera_mode), rt.mem.u32(G::camera_drive_flag), rt.routineName(rt.mem.u32(r0 + 0x4C)).c_str(),
                          rt.mem.f32(r0), rt.mem.f32(r0 + 4), rt.mem.f32(r0 + 8), rt.mem.f32(r0 + 0x18), rt.mem.f32(r0 + 0x1C), rt.mem.f32(r0 + 0x20),
                          entries.size());
            rt.log(b);
        }
        // fix140b (user 2026-11-10, the roof: "the arena is in a cut-scene ... a bad arena" in every run, the headless
        // test's too: not a timing or settings matter): the roof's wait comes while the Magician's entrance scene ends,
        // its bars still sliding away (letterbox_task's scene_state 1, 40 frames, under the arena's name). The arena
        // starts in play at once: scene_state 6 (bars off, shots on, then 2),
        // whatever cut-scene state the lead-in left (a scene_state 4 would have held the bars and shots off for good)
        if (rt.mem.u32(G::shot_enable) == 0 || rt.letterbox != 0) {
            if (rt.log)
                rt.log("survival: the lead-in ended in a cut-scene (scene state " + std::to_string(rt.mem.at<uint8_t>(G::scene_state)) +
                       ", bars " + std::to_string(rt.letterbox) + ", shots " + std::to_string(rt.mem.u32(G::shot_enable)) + "): the arena starts in play");
            rt.mem.at<uint8_t>(G::scene_state) = 6;
            rt.mem.set32(G::shot_enable, 1);
            rt.letterbox = 0;
        }
        frozen = true;
        if (const char* w = survivalEnv("START_WAVE"))   // testing: the waves from this one (the limits' stress run)
            wave = std::max(0, std::atoi(w) - 1);
        phase = Break;
        breakTimer = kFirstBreak;
        {
            std::string n = lang::tr(survivalArenas()[size_t(arena)].name);   // (upper case, as the other banners; survpub: translated)
            for (char& c : n) c = char(std::toupper(uint8_t(c)));
            say(n, 120, Plain);
        }
        if (rt.log)
            rt.log("survival: the arena at frame " + std::to_string(rt.frameCount) + " (lead-in " + std::to_string(leadFrames) +
                   " frames, " + std::to_string(points.size()) + " spawn points, wait " + std::to_string(waitsSeen + 1) + ")");
        if (rt.survivalMusic_ != 0)
            rt.sound_cue(rt.survivalMusic_);   // the lead-in's last music (its cues were silent)
        return true;
    }
    // frozen: the director, in the event task's slot (the game's task group, as the script's own spawns)
    countMine(rt);
    // survmaps: an arena with its own view keeps it: the stop's wait kind 3 (game_cam_stop_update keeps the sub-state)
    // and the sub-state "look at the attackers" (the eye held at the stop point, the look-at eased to the attackers
    // or, none, to the stop's own look-at), never "clear" (that eases back to the story's path: the Magician's sky);
    // a requested turn (7) as it is
    if (survivalArenas()[size_t(arena)].view != std::array<float, 6>{} && !survivalEnv("START")) {
        rt.mem.at<uint8_t>(G::evt_cgmfnsh) = 3;
        if (rt.mem.u32(G::camera_drive_flag) != 7)
            rt.mem.set32(G::camera_drive_flag, 3);
    }
    // developer (arenas: judging an arena by numbers, scratch arenas.py): HOTD_SURVIVAL_DUMP=<file> writes, 30 frames
    // into the first break (the camera settled, the view drawn), the camera's place and view matrix, the spawn points
    // and the stage's walls / floors near the camera (the Ragdoll mod's, the view builds them with --render); then
    // every 4 frames of the rounds each creature's place (its walk: checked against the walls)
    if (static const char* dumpTo = survivalEnv("DUMP"); dumpTo && *dumpTo) {
        static FILE* df = nullptr;
        if (!df && phase == Break && breakTimer == kFirstBreak - 30 && (df = std::fopen(dumpTo, "w")) != nullptr) {
            const float cx = rt.mem.f32(G::camera_pos), cy = rt.mem.f32(G::camera_pos + 4), cz = rt.mem.f32(G::camera_pos + 8);
            std::fprintf(df, "cam %g %g %g\nview", cx, cy, cz);
            for (const float v : rt.camera)
                std::fprintf(df, " %g", v);
            std::fprintf(df, "\n");
            for (const Point& p : points)
                std::fprintf(df, "pt %g %g %g %d %d %d\n", p.pos[0], p.pos[1], p.pos[2], p.ang[1], p.kind, p.type);
            const RagdollWalls* w = rt.ragdollWorld ? rt.ragdollWorld() : nullptr;
            for (size_t o = 0; w && o < w->objs.size(); ++o) {
                const RagdollWalls::Obj& ob = w->objs[o];
                if (ob.lo[0] > cx + 1500 || ob.hi[0] < cx - 1500 || ob.lo[2] > cz + 1500 || ob.hi[2] < cz - 1500)
                    continue;
                for (int f = 0; f < 2; ++f) {
                    const std::vector<float>& t = f ? ob.floors : ob.tris;
                    for (size_t i = 0; i + 12 <= t.size(); i += 12)
                        std::fprintf(df, "%s %zu %g %g %g %g %g %g %g %g %g\n", f ? "fl" : "wl", o, t[i], t[i + 1], t[i + 2], t[i + 3], t[i + 4],
                                     t[i + 5], t[i + 6], t[i + 7], t[i + 8]);
                }
            }
            std::fflush(df);
        }
        if (df && phase == Wave && rt.frameCount % 4 == 0) {
            for (const Mine& m : mine)
                if (m.serial != 0 && rt.mem.u32(m.task + 8) == m.serial) {
                    const port::a::Enemy& e = rt.mem.at<port::a::Enemy>(m.task);
                    std::fprintf(df, "e %llu %X %u %g %g %g\n", (unsigned long long)rt.frameCount, m.task, m.serial, e.pos[0], e.pos[1], e.pos[2]);
                }
            if (rt.frameCount % 400 == 0)
                std::fflush(df);
        }
    }
    // survfix (user 2026-11-04): a creature that will never attack does not hold the round (game/engage.h: decided
    // from its AI state; the scripted kinds, the queue at the waiting positions, the walkers still coming all count);
    // it leaves as the game's attackers leave. HOTD_SURVIVAL_KEEP_IDLE=1 (testing): kept, as before
    if (phase == Wave && !survivalEnv("KEEP_IDLE"))
        for (const Mine& m : mine) {
            if (m.serial == 0 || rt.mem.u32(m.task + 8) != m.serial)
                continue;
            const std::string why = engage.check(rt, m.task, m.serial);
            if (why.empty())
                continue;
            EngageWatch::retire(rt, m.task);
            engage.forget(m.task);
            ++engage.retired, ++retiredN;
            if (rt.log)
                rt.log("survival: a type " + std::to_string(m.type) + " creature will never attack (" + why + "): sent away at frame " +
                       std::to_string(rt.frameCount));
        }
    if (testIdle)
        testIdleFrame(rt);
    if (static const char* tr = survivalEnv("TRACE_ALIVE"); tr && phase == Wave && rt.frameCount % uint64_t(std::max(1, std::atoi(tr))) == 0) {
        const float cx = rt.mem.f32(G::camera_pos), cz = rt.mem.f32(G::camera_pos + 8);
        // (hordepub: the motions' names, 0xC8BE0: a pointer to "MO_..." per motion; every name once with the trace)
        const auto moName = [&](int mo) {
            std::string s;
            const uint32_t p = mo >= 0 && mo < 674 ? rt.mem.u32(0xC8BE0 + 4 * uint32_t(mo)) : 0;
            for (uint32_t a = p; p && rt.mem.mapped(a) && rt.mem.at<uint8_t>(a) && s.size() < 40; ++a)
                s += char(rt.mem.at<uint8_t>(a));
            return s;
        };
        if (static bool listed = false; !listed && survivalEnv("TRACE_MOTIONS")) {
            listed = true;
            for (int mo = 0; mo < 674; ++mo)
                std::printf("motion %d %s\n", mo, moName(mo).c_str());
            for (int ty : {10, 28, 29, 66}) {   // (the stance / walk lists: {motion, speed} by sub-state, action 0)
                const uint32_t row = rt.mem.u32(G::enemy_motion_lists + 4 * uint32_t(ty)), list = row ? rt.mem.u32(row) : 0;
                std::printf("list type %d row %X list %X:", ty, row, list);
                for (uint32_t k = 0; list && k < 8; ++k)
                    std::printf(" [%u] %d %.3f", k, rt.mem.at<int32_t>(list + 8 * k), rt.mem.f32(list + 8 * k + 4));
                std::printf("\n");
            }
        }
        for (const Mine& m : mine) {
            if (rt.mem.u32(m.task + 8) != m.serial || m.serial == 0)
                continue;
            const port::a::Enemy& e = rt.mem.at<port::a::Enemy>(m.task);
            const float dx = e.pos[0] - cx, dz = e.pos[2] - cz;
            // (hordepub: + the motion, its frame and the forward speed: the cut creatures' walk check)
            std::printf("alive f%llu round %d task %X type %d%s age %llu %s state %d sub %d delay %d slot %d flags %08X dist %.0f view z %.0f life %d"
                        " motion %d frame %d speed %.3f\n",
                        (unsigned long long)rt.frameCount, wave, m.task, m.type, m.cut ? " cut" : "", (unsigned long long)(rt.frameCount - m.born),
                        rt.routineName(e.routine).c_str(), e.state, e.substate, e.delay, e.attack_slot, e.flags, std::sqrt(dx * dx + dz * dz),
                        e.view_pos[2], e.life, int(rt.mem.at<int16_t>(m.task + 0xD0)), int(rt.mem.at<int16_t>(m.task + 0xF4)),
                        rt.mem.f32(m.task + 0x74));
        }
    }
    // testing (HOTD_SURVIVAL_DRAIN): a seat's lives to 0 in a round, as a creature's last hit would
    if (phase == Wave && !stats.empty())
        for (const Drain& d : drains)
            if (d.round == wave && d.seat < rt.seat_count() && rt.frameCount == stats.back().start + uint64_t(std::max(1, d.frames))) {
                port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(d.seat));
                if (pl.state != 5)
                    continue;
                pl.lives = 0;
                rt.livesKeptSkip = uint8_t(rt.livesKeptSkip | (1u << d.seat));
                if (rt.log)
                    rt.log("survival: test: " + std::to_string(d.seat + 1) + "P's lives drained in round " + std::to_string(wave) + " at frame " +
                           std::to_string(rt.frameCount));
            }
    seats(rt);
    if (bannerFrames > 0 && --bannerFrames == 0)
        banner.clear();
    if (phase == Over)
        return true;
    if (phase == Break) {
        if (testBuyer && shopOpen() && breakTimer % 20 == 0)   // testing: the bots' shopping (testBuy), a buy each 20 frames
            for (int p = 0; p < rt.seat_count(); ++p)
                if (const int it = testBuy(rt, p); it >= 0)
                    buy(rt, p, it);
        // (survhand: a named buyer's seat takes its own weapon back into the hand when it has ammo again (bought Ammo):
        // a player switching to the gun they paid for)
        if (testBuyer && breakTimer == 1)
            if (const char* b = survivalEnv("BUYER"))
                for (int p = 0; p < rt.seat_count() && p < 4; ++p)
                    for (int k = 1; k < kWeapons; ++k)
                        if (rt.weapons.def(k).key == b)
                            rt.weapons.select(rt, p, k);
        if (--breakTimer <= 0)
            startWave(rt);
        return true;
    }
    // testing (the self-test): each seat switches to its next weapon (the switch's path) once the one in its hand has
    // been reloaded (each weapon fires and reloads), at the latest after 900 frames
    if (cycleTest && (rt.frameCount % 30) == 0)
        for (int p = 0; p < rt.seat_count() && p < 4; ++p) {
            const Weapons::Seat& w = rt.weapons.seat[p];
            if (w.cur != cycleWeapon_[p]) {
                cycleWeapon_[p] = w.cur, cycleAt_[p] = rt.frameCount, cycleReloads_[p] = w.reloads[w.cur];
                port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
                pl.rounds = uint8_t(std::min<int>(pl.rounds, 3));   // (a short magazine: its reload comes soon)
            } else if (w.reload == 0 && (w.reloads[w.cur] > cycleReloads_[p] || rt.frameCount > cycleAt_[p] + 900)) {
                rt.weapons.request[p] = 1;
                cycleAt_[p] = rt.frameCount;
            }
        }
    // a round (Call of Duty Zombies: the creatures come one by one, a few at once, the gap shrinking each round; rounds
    // 1-3 gentle: 2, 3, 4 at once)
    const int cap = std::min(maxAlive, special == 1 ? 4 + wave : wave <= 3 ? 1 + wave : 2 + wave);
    if (dogs()) {
        // survdogs: the dogs come in twos and threes, a few at once (2 + the round's number among the dogs' rounds +
        // the players, at most 8: the original's 14 body slots, where its pack list looks), a gap of 0.6-1.6 s
        const int dogCap = std::min({maxAlive, 8, 2 + dogRounds + players_});
        if (toSpawn > 0 && --spawnTimer <= 0 && alive < dogCap && !points.empty()) {
            spawnDog(rt);
            spawnTimer = std::max(20, 50 - 5 * dogRounds) + rand(60);
        }
        // a dog that never came in (stuck in an entrance or circling for good: the camera never moves here) is ended
        // after 60 s by the game's own death (its hit points 0), so the round never stalls
        for (const Mine& m : mine)
            if (m.dog && m.serial != 0 && rt.mem.u32(m.task + 8) == m.serial && rt.frameCount - m.born == kDogStall) {
                port::a::Enemy& e = rt.mem.at<port::a::Enemy>(m.task);
                if (e.life > 0 && (e.flags & 1) != 0) {
                    e.life = 0;
                    ++dogStalled;
                    if (rt.log)
                        rt.log("survival: a dog stalled 60 s (routine " + rt.routineName(e.routine) + ", state " + std::to_string(e.state) +
                               "): ended at frame " + std::to_string(rt.frameCount));
                }
            }
    } else if (toSpawn > 0 && --spawnTimer <= 0 && alive < cap && (!points.empty() || !entries.empty())) {
        spawnOne(rt);
        spawnTimer = std::max(12, int(80.0 * std::pow(0.93, double(wave - 1)))) + rand(20);
    }
    if (toSpawn == 0 && alive == 0) {
        // the round is cleared: no free life (survival2). survdogs (user 2026-11-10, "Only dog rounds"): Max Ammo after
        // a dogs' round and no other (the runners' / brutes' rounds gave it until then): every weapon each player in
        // play holds, its magazine and reserve full; the game's own item pick-up sound (as a buy's)
        std::string s = lang::fmt("ROUND {n} CLEARED", {{"n", std::to_string(wave)}});
        if (dogs()) {
            for (int p = 0; p < rt.seat_count() && p < 4; ++p)
                if (rt.mem.at<port::a::Player>(rt.player_rec(p)).state == 5) {
                    rt.weapons.refill(rt, p, -1);
                    ++refills;
                }
            s += "  " + lang::tr("MAX AMMO");
            rt.sound_cue(0xA01351);
            dogKilled += waveTotal;   // (every dog of the round dead: shot, or the stall guard's, counted apart)
        }
        // the weapons are kept after a round (the user, 2026-11-04): what each seat holds now, checked at the next start
        for (int p = 0; p < 4; ++p) {
            seat[p].ownedAtClear = 0;
            for (int w = 0; w < kWeapons; ++w)
                seat[p].ownedAtClear |= rt.weapons.seat[p].own[w] ? 1u << w : 0u;
        }
        say(s, 150, Good);
        if (rt.audio)   // data/survival/wave_clear.wav when it is there (no stand-in)
            rt.audio->playExtra(survivalSoundDir(), kSurvivalSfx[3], -1, 1.0f, 1.0f);
        if (!stats.empty()) {
            stats.back().end = rt.frameCount;
            for (int p = 0; p < rt.seat_count(); ++p)
                stats.back().earned += seat[p].earned, stats.back().banked += seat[p].points;
        }
        phase = Break;
        breakTimer = kBreakFrames;
        if (const char* pts = survivalEnv("POINTS"))   // testing (the shop's pictures): seat p has n x (4 - p) / 4 points
            for (int p = 0; p < 4; ++p)
                seat[p].points = std::max(0, std::atoi(pts)) * (4 - p) / 4;
        if (rt.log)
            rt.log("survival: round " + std::to_string(wave) + " cleared at frame " + std::to_string(rt.frameCount) + " (peak " +
                   std::to_string(peakAlive) + ")");
    }
    return true;
}

// testing (HOTD_SURVIVAL_TEST_IDLE=1, docs/survival.md; run without a shooter, lives kept): round 1 gets two more
// creatures 250 units away, out of the camera's view: A is put back on its place each frame of its approach (enemy_main
// state 1: it walks on the spot), a creature that will never attack; B waits 1500 frames first (its delay, record
// +0x320 x 10: "it comes later") and then comes as any other. Every creature that reaches an attack (AI state 5 or
// more: it would hit the player) is killed by the test (its hit points to 0, the game's own death). The round must end
// without A (sent away) and only after B (killed when it attacks); with HOTD_SURVIVAL_KEEP_IDLE=1 it never ends
void SurvivalMode::testIdleFrame(Runtime& rt)
{
    if (wave == 1 && phase == Wave && idleTask_[0] == 0 && spawned >= 1) {
        const float* m = rt.camera;   // (the view matrix: the camera's forward in the world is its third column)
        const float fx = m[2], fz = m[8], n = std::sqrt(fx * fx + fz * fz);
        const float cx = rt.mem.f32(G::camera_pos), cz = rt.mem.f32(G::camera_pos + 8);
        for (int k = 0; k < 2; ++k) {
            const float side = k == 0 ? -60.0f : 60.0f;
            float pos[3] = {cx + fx / n * 250.0f + fz / n * side, points.empty() ? 0.0f : points[0].pos[1], cz + fz / n * 250.0f - fx / n * side};
            if (m[2] * pos[0] + m[5] * pos[1] + m[8] * pos[2] + m[11] > 0.0f)   // (in front: the other way)
                pos[0] = cx - fx / n * 250.0f + fz / n * side, pos[2] = cz - fz / n * 250.0f - fx / n * side;
            const int32_t ang[3] = {0, points.empty() ? 0 : points[0].ang[1], 0};
            const int type = survivalArenas()[size_t(arena)].types[0];
            const uint32_t t = rt.survival_spawn(0, 1, pos, ang, type, baseAction(type), baseLife(type), k == 0 ? 0u : 150u, 0);
            idleTask_[k] = t, idleSerial_[k] = rt.mem.u32(t + 8);
            mine.push_back({t, idleSerial_[k], rt.frameCount, type, false});
            ++spawned;
            if (rt.log)
                rt.log(std::string("survival test: ") + (k == 0 ? "A (held: never attacks)" : "B (delayed: comes later)") + " at frame " +
                       std::to_string(rt.frameCount));
        }
    }
    if (idleTask_[0] && rt.mem.u32(idleTask_[0] + 8) == idleSerial_[0]) {   // (A: held where it stands in state 1)
        port::a::Enemy& e = rt.mem.at<port::a::Enemy>(idleTask_[0]);
        if (e.routine == A::F::enemy_main && e.state == 1) {
            if (idleHeld_[0] == 0.0f && idleHeld_[1] == 0.0f)
                idleHeld_[0] = e.pos[0], idleHeld_[1] = e.pos[2];
            e.pos[0] = idleHeld_[0], e.pos[2] = idleHeld_[1];
        }
    }
    for (const Mine& m : mine) {   // (the test's shooter: whoever reaches an attack dies)
        if (m.serial == 0 || rt.mem.u32(m.task + 8) != m.serial)
            continue;
        port::a::Enemy& e = rt.mem.at<port::a::Enemy>(m.task);
        if (e.routine == A::F::enemy_main && e.state >= 5 && e.life > 0) {
            e.life = 0;
            if (rt.log && m.task == idleTask_[1])
                rt.log("survival test: B attacks at frame " + std::to_string(rt.frameCount) + ": killed");
        }
    }
}

void SurvivalMode::startWave(Runtime& rt)
{
    ++wave;
    // every 5th round a special one (as the dogs' rounds): 5, 15, 25 .. the runners (every creature dashing from the
    // start, the arena's lighter types, more at once); 10, 20 .. the brutes (the arena's heaviest types, fewer, tougher)
    special = wave % 5 != 0 ? 0 : (wave / 5) % 2 == 1 ? 1 : 2;
    int count = roundCount(wave, players_);
    if (special == 1)
        count = count * 3 / 4;
    else if (special == 2)
        count = std::max(4, count / 2);
    // survdogs: the dogs' round (it takes the place of a runners' / brutes' one on the same round): 6 dogs for one
    // player the first time, 2 more each time after, + half for each other player (fast: far fewer than a round's
    // creatures), at most 40; the next one 7 or 8 rounds on
    if (wave >= dogWave) {   // (>=: HOTD_SURVIVAL_START_WAVE may start past it)
        special = kDogs;
        ++dogRounds;
        count = std::min(40, int((6.0 + 2.0 * (dogRounds - 1)) * (1.0 + 0.5 * double(std::clamp(players_, 1, 4) - 1)) + 0.5));
        dogWave = wave + dogNext();
    }
    waveTotal = toSpawn = count;
    spawnTimer = 30;
    phase = Wave;
    WaveStat st;
    st.wave = wave, st.count = waveTotal, st.start = rt.frameCount, st.special = special;
    stats.push_back(st);
    // the game's own aggression (difficulty_level's rank 0x51F492, 0..15: how often the creatures attack; also the
    // difficulty bonus of the players' hits, part_hit): a floor that rises with the round
    const uint16_t floor = uint16_t(std::min((wave - 1) * 2 / 3, 15));
    if (rt.mem.at<uint16_t>(kRank) < floor)
        rt.mem.at<uint16_t>(kRank) = floor;
    std::string s = lang::fmt("ROUND {n}", {{"n", std::to_string(wave)}});
    if (special != 0)
        s += "  " + lang::tr(special == 1 ? "RUNNERS" : special == 2 ? "BRUTES" : "DOGS");
    // the weapons held at the last clear are still held (the test's kept= check: bought ones may have been added)
    for (int p = 0; p < 4; ++p) {
        unsigned now = 0;
        for (int w = 0; w < kWeapons; ++w)
            now |= rt.weapons.seat[p].own[w] ? 1u << w : 0u;
        if (wave > 1 && (seat[p].ownedAtClear & ~now) != 0) {
            keptOk = false;
            if (rt.log)
                rt.log("survival: " + std::to_string(p + 1) + "P lost a weapon between the rounds");
        }
    }
    // testing: the SMG free from round smgWave; every weapon free from round weaponsWave
    for (int p = 0; p < rt.seat_count(); ++p) {
        if (rt.mem.at<port::a::Player>(rt.player_rec(p)).state != 5)
            continue;
        if (smgWave >= 1 && wave >= smgWave && !rt.weapons.seat[p].own[kSmg]) {
            rt.weapons.give(rt, p, kSmg);
            rt.weapons.select(rt, p, kSmg);
            s += "  SMG";
        }
        if (weaponsWave >= 1 && wave == weaponsWave)
            for (int w = 1; w < kWeapons; ++w)
                rt.weapons.give(rt, p, w);
    }
    say(s, 120);
    // survrevive: the seats that went out are back with the round's banner (half their points, the start lives)
    for (int p = 0; p < rt.seat_count() && p < 4; ++p)
        if (seat[p].out)
            comeBack(rt, p);
    if (rt.audio)   // data/survival/wave_start.wav when it is there (no stand-in)
        rt.audio->playExtra(survivalSoundDir(), kSurvivalSfx[2], -1, 1.0f, 1.0f);
    if (rt.log)
        rt.log("survival: round " + std::to_string(wave) + " (" + std::to_string(waveTotal) + " creatures" +
               (special == 1 ? ", runners" : special == 2 ? ", brutes" : dogs() ? ", dogs" : "") + ") at frame " + std::to_string(rt.frameCount) +
               (dogs() ? ", the next dogs' round " + std::to_string(dogWave) : std::string()));
}

// the creatures of a round (Call of Duty Zombies' curve, World at War / Black Ops, from round 10 24 x 0.15 x round),
// a quarter more for each other player; at most 150. survfix2 (the balance runs): rounds 1-9 rise by 3 or 4 a round
// (6, 9, 13, 17, 21, 24, 27, 30, 33: CoD's 18 -> 24 at round 5 and 32 -> 36 at round 10 were the curve's steps)
int SurvivalMode::roundCount(int round, int players)
{
    static const int kEarly[5] = {0, 6, 9, 13, 17};
    double n = round < 5 ? kEarly[std::max(1, round)] : round < 10 ? 21.0 + 3.0 * (round - 5) : 24.0 * 0.15 * round;
    n *= 1.0 + 0.25 * double(std::clamp(players, 1, 4) - 1);
    return std::min(150, int(n + 0.5));
}

// the creatures' hit points (x their type's): as the first rounds up to round 3, then + 12 % a round up to round 9,
// then x 1.1 a round (Call of Duty: + 100 a round to 9, then x 1.1)
float SurvivalMode::roundHealth(int round)
{
    if (round <= 3)
        return 1.0f;
    if (round <= 9)
        return 1.0f + 0.12f * float(round - 3);
    return 1.72f * std::pow(1.1f, float(round - 9));
}

int SurvivalMode::pickType(const Runtime& rt, bool& cut)
{
    cut = false;
    // testing: HOTD_SURVIVAL_TYPE=t: every spawn that type (a cut creature's pictures and behaviour)
    static const int force = survivalEnv("TYPE") ? std::atoi(survivalEnv("TYPE")) : -1;
    if (force >= 0) {
        for (const SurvivalCut& c : survivalCuts())
            cut = cut || c.type == force;
        return force;
    }
    const std::vector<int>& t = survivalArenas()[size_t(arena)].types;
    const int all = int(t.size());
    if (special == 1)   // the runners: the lighter front half of the list
        return t[size_t(rand(std::max(2, all / 2)))];
    if (special == 2) {   // the brutes: the heavier back half (and the cut creatures among them)
        if (cutCreatures && !rt.pcModels && rand(100) < 20) {
            cut = true;
            const auto& c = survivalCuts();
            return c[size_t(rand(int(c.size())))].type;
        }
        return t[size_t(all / 2 + rand(all - all / 2))];
    }
    if (cutCreatures && !rt.pcModels && wave >= 2 && rand(100) < std::min(5 + 2 * wave, 25)) {
        cut = true;
        const auto& c = survivalCuts();
        return c[size_t(rand(int(c.size())))].type;
    }
    // the early rounds from the front of the list (the weaker types), then all of it
    const int n = std::clamp(1 + wave, 2, all);
    return t[size_t(rand(n))];
}

bool SurvivalMode::spawnEntry(Runtime& rt, int life)
{
    // a free chain (its last copy off it), at random; none free: false
    int freeN = 0;
    for (const Entry& e : entries)
        freeN += e.freeAt <= rt.frameCount ? 1 : 0;
    if (freeN == 0)
        return false;
    int k = rand(freeN);
    for (Entry& e : entries) {
        if (e.freeAt > rt.frameCount || k-- != 0)
            continue;
        if (life > 1) {   // (its type's hit points, risen with the round as every spawn's)
            const float m = roundHealth(wave) * (special == 2 ? 1.25f : 1.0f);
            life = std::clamp(int(float(baseLife(e.type)) * m + 0.5f), 1, 30000);
        }
        const uint32_t t = rt.survival_spawn_record(e.rec, life);
        // (kind 7: the slide's frames at +0x10 of its part, then the jump off; the next copy 60 frames after)
        e.freeAt = rt.frameCount + uint64_t(std::min<uint32_t>(rt.mem.u32(e.rec + 0x24 + 0x10), 600)) + 60;
        mine.push_back({t, rt.mem.u32(t + 8), rt.frameCount, e.type, false});
        --toSpawn;
        ++spawned;
        ++entrySpawned;
        return true;
    }
    return false;
}

// survdogs: a dog, as the story makes its dogs (ENTBE on a kind 97 record: Runtime::survival_spawn_record), from a copy of
// one of Chapter 1's three records (kDogSrc, at random: the black dog or the grey one, jumping or running in) with the
// place a spawn point (spread +-20), the heading toward the camera, the circling point kDogCircle in front of the camera
// on the line to the dog. Its hit points: the story's 1 (every hit kills) in the first dogs' round, then 40 more a time
void SurvivalMode::spawnDog(Runtime& rt)
{
    const Point& p = points[size_t(rand(int(points.size())))];
    const uint32_t src = kDogSrc[rand(3)];
    const uint32_t rec = kDogRecs + kDogRecSize * (uint32_t(dogSpawned) % kDogRing);
    if (rt.mem.mapped(src) && rt.mem.u32(src) == 97) {
        for (uint32_t i = 0; i < 0x40; i += 4)
            rt.mem.set32(rec + i, rt.mem.u32(src + i));
    } else {   // (not that ROM: the grey dog jumping in, as E1430)
        for (uint32_t i = 0; i < 0x40; i += 4)
            rt.mem.set32(rec + i, 0);
        rt.mem.set32(rec, 97);
        rt.mem.set32(rec + 0x24, 0x18000200u);
    }
    float pos[3] = {p.pos[0], p.pos[1], p.pos[2]};
    pos[0] += float(rand(41) - 20);
    pos[2] += float(rand(41) - 20);
    const float cx = rt.mem.f32(G::camera_pos), cz = rt.mem.f32(G::camera_pos + 8);
    const float dx = pos[0] - cx, dz = pos[2] - cz, d = std::max(1.0f, std::sqrt(dx * dx + dz * dz));
    for (int i = 0; i < 3; ++i)
        rt.mem.set32(rec + 8 + 4 * uint32_t(i), std::bit_cast<uint32_t>(pos[i]));
    rt.mem.set32(rec + 0x14, 0);
    rt.mem.set32(rec + 0x18, uint32_t(int32_t(std::lround(-std::atan2(cx - pos[0], cz - pos[2]) / 6.2831853 * 65536.0)) & 0xFFFF));
    rt.mem.set32(rec + 0x1C, 0);
    const float circle[3] = {cx + dx / d * kDogCircle, pos[1], cz + dz / d * kDogCircle};
    for (int i = 0; i < 3; ++i)
        rt.mem.set32(rec + 0x34 + 4 * uint32_t(i), std::bit_cast<uint32_t>(circle[i]));
    static const int lifeForce = survivalEnv("DOG_LIFE") ? std::atoi(survivalEnv("DOG_LIFE")) : 0;   // (testing)
    const int life = lifeForce > 0 ? lifeForce : std::min(1 + 40 * (dogRounds - 1), 400);
    const uint32_t t = rt.survival_spawn_record(rec, life);
    const int type = rt.mem.at<uint8_t>(rec + 0x24);
    mine.push_back({t, rt.mem.u32(t + 8), rt.frameCount, type, false, true});
    --toSpawn;
    ++spawned;
    ++dogSpawned;
}

void SurvivalMode::spawnOne(Runtime& rt)
{
    // survmaps: the arena's entry records first (entryShare %), or when it has no other places
    const SurvivalArena& ar = survivalArenas()[size_t(arena)];
    if (!entries.empty() && (points.empty() || rand(100) < ar.entryShare)) {
        if (spawnEntry(rt, 2) || points.empty())
            return;   // (none free and no other places: the next try after the gap)
    }
    const Point& p = points[size_t(rand(int(points.size())))];
    bool cut = false;
    const int type = pickType(rt, cut);
    int life = baseLife(type);
    if (cut)
        for (const SurvivalCut& c : survivalCuts())
            if (c.type == type) life = c.life;
    if (life > 1) {   // the hit points rise with the round (roundHealth); the brutes' round x 1.25 more
        float m = roundHealth(wave) * (special == 2 ? 1.25f : 1.0f);
        life = std::clamp(int(float(life) * m + 0.5f), 1, 30000);   // (+0x70 / +0x72 are 16-bit)
    }
    float pos[3] = {p.pos[0], p.pos[1], p.pos[2]};
    // spread over the place (not one on another); a door's +-8 (arenas: a doorway is narrow, +-20 put some in its frame)
    const int spread = survivalArenas()[size_t(arena)].doors.empty() ? 20 : 8;
    pos[0] += float(rand(2 * spread + 1) - spread);
    pos[2] += float(rand(2 * spread + 1) - spread);
    uint32_t flags = (p.flags & ~(1u << 19)) | 1u;   // (no headless count)
    // the runners: the start sub-state 2 (kind000_enemy_spawn: the type's motion list entry 2, its dash, and
    // walk_variant 1, so it dashes again after each stop: enemy_ai's walkOn): none in rounds 1-3, then a growing share
    // (at most 60 %); in the runners' round every one, with the AI at once. survfix2: 6 / 8 % a round (were 8 / 10:
    // with the counts and the hit points, the lives lost tripled from round 6 to 7)
    const bool runner = special == 1 || (wave >= 4 && rand(100) < std::min(6 * (wave - 3), 60));
    if (special == 1 || (wave >= 4 && rand(100) < std::min(8 * (wave - 3), 70)))
        flags |= 1u << 22;   // AI at once (enemy_take_ai_at_once): no waiting before it comes
    int cutAction = 0;   // (hordepub: a cut creature's own set-up, SurvivalCut::action)
    for (const SurvivalCut& c : survivalCuts())
        if (cut && c.type == type) cutAction = c.action;
    const uint32_t t = rt.survival_spawn(0, flags, pos, p.ang, type, cut ? cutAction : baseAction(type), life,
                                      wave >= 3 ? 0 : uint32_t(rand(3)), runner ? 2u : 0u);
    mine.push_back({t, rt.mem.u32(t + 8), rt.frameCount, type, cut});
    --toSpawn;
    ++spawned;
    fastSpawned += runner ? 1 : 0;
    cutSpawned += cut ? 1 : 0;
    if (cut && !stats.empty())
        ++stats.back().cut;
    if (rt.log && cut)
        rt.log("survival: cut creature type " + std::to_string(type) + " at frame " + std::to_string(rt.frameCount) + " (task 0x" +
               [&] { char h[16]; std::snprintf(h, sizeof h, "%X", t); return std::string(h); }() + ")");
}

void SurvivalMode::countMine(Runtime& rt)
{
    // the creatures alive: the game's own count (the script is frozen: every creature now is Survival's)
    alive = rt.mem.at<int16_t>(G::enemy_count);
    if (alive < 0) alive = 0;
    peakAlive = std::max(peakAlive, alive);
    int bodies = 0;   // body slots taken (the original's 14 creature slots, Survival's own from 16)
    for (uint32_t i = 0; i < 14; ++i)
        bodies += rt.mem.u32(G::player_bodies + 4 * i) != 0 ? 1 : 0;
    for (int j = 0; j < rt.extraBodySlots; ++j)
        bodies += rt.mem.u32(rt.extraBodyTable + 4 * uint32_t(j)) != 0 ? 1 : 0;
    peakBodies = std::max(peakBodies, bodies);
    if (!stats.empty() && phase == Wave)
        stats.back().peak = std::max(stats.back().peak, alive);
    if (rt.frameCount % 120 == 0)   // (the ones gone: their task's serial changed)
        mine.erase(std::remove_if(mine.begin(), mine.end(), [&](const Mine& m) { return m.serial == 0 || rt.mem.u32(m.task + 8) != m.serial; }),
                   mine.end());
    kills = std::max(0, spawned - alive - retiredN);   // (every creature now is Survival's: the ones no longer counted were killed or sent away)
}

void SurvivalMode::tick(Runtime& rt)
{
    justFinished = false;
    if (phase == Off)
        return;
    if (phase == Over) {   // the game over scene's end (game_over: mode 5 done): the host files the score now
        if (rt.gameOverDone && !reported_) {
            reported_ = true;
            justFinished = true;
        }
        return;
    }
    // survrevive (user 2026-11-05, 2-4 players): a seat out of lives (the game's state 4, the arcade's CONTINUE?) while
    // another plays on is out until the next round, no continue. Checked after the frame (before it is drawn), so the
    // seat that went to state 4 in it (player_in_game_frame / coop4_seat_frame) never shows CONTINUE? and its
    // CONTINUE? routine (player_st4_out_of_lives) never runs. Every seat out at once: the game over as before
    // (game_play: all out -> the continue's sub-modes -> bossNoContinue -> mode 5). 1 player: never (nobody else)
    if (frozen && (phase == Wave || phase == Break) && rt.mem.at<int16_t>(G::game_mode) == 4)
        for (int p = 0; p < rt.seat_count() && p < 4; ++p) {
            if (seat[p].out || rt.mem.at<port::a::Player>(rt.player_rec(p)).state != 4)
                continue;
            bool other = false;   // (in play, or starting / joining / held by the camera: states 1, 2, 3, 5, 10)
            for (int q = 0; q < rt.seat_count() && q < 4 && !other; ++q) {
                const int32_t st = rt.mem.at<port::a::Player>(rt.player_rec(q)).state;
                other = q != p && !seat[q].out && (st == 5 || st == 1 || st == 2 || st == 3 || st == 10);
            }
            if (other)
                goOut(rt, p);
        }
    if (rt.mem.at<int16_t>(G::game_mode) == 5 || rt.gameOverDone) {
        phase = Over;
        justFinished = rt.gameOverDone;
        reported_ = rt.gameOverDone;
        overFrame = rt.frameCount;
        score = 0;
        for (int p = 0; p < rt.seat_count(); ++p)
            score += seat[p].earned;
        // the results over the game over scene: the title block's two lines (titleLine / statusLine; the table follows
        // in the menus). survfix2: no banner (its line ran across the GAME OVER logo and the map's labels)
        banner.clear(), bannerFrames = 0;
        // (survrevive: every seat was out at once: nobody comes back; the start buttons as in any game over)
        for (Seat& s : seat)
            s.out = false;
        rt.seatsHeldOut = 0, rt.livesKeptSkip = 0;
        if (rt.log)
            rt.log("survival: " + summary(rt));
    }
}

std::string SurvivalMode::titleLine() const
{
    if (phase == Off || phase == LeadIn || wave <= 0)
        return {};
    if (phase == Over)   // (the results)
        return lang::fmt("REACHED ROUND {r}", {{"r", std::to_string(wave)}});
    if (phase == Break)
        return lang::fmt("ROUND {n} IN {s}", {{"n", std::to_string(wave + 1)}, {"s", std::to_string((breakTimer + 59) / 60)}});
    return lang::fmt("ROUND {n}", {{"n", std::to_string(wave)}});
}

std::string SurvivalMode::statusLine() const
{
    if (phase == Off || phase == LeadIn || wave <= 0)
        return {};
    if (phase == Over)
        return lang::fmt("POINTS {p}   KILLS {k}", {{"p", std::to_string(score)}, {"k", std::to_string(kills)}});
    if (phase == Break)
        return shopOpen() ? lang::tr("SHOOT AN ITEM TO BUY") : std::string();
    if (phase == Wave)
        return lang::fmt("LEFT {k}", {{"k", std::to_string(toSpawn + alive)}});
    return {};
}

std::string SurvivalMode::summary(const Runtime& rt) const
{
    char b[512];
    int pts = 0, earned = 0, hitsN = 0, heads = 0, spent = 0, buysN[kItems] = {};
    for (int p = 0; p < 4; ++p) {
        pts += seat[p].points, earned += seat[p].earned, hitsN += seat[p].hits, heads += seat[p].heads, spent += seat[p].spent;
        for (int i = 0; i < kItems; ++i)
            buysN[i] += seat[p].buys[i];
    }
    std::snprintf(b, sizeof b,
                  "arena=%s round=%d score=%d points=%d spent=%d hits=%d headkills=%d buys=ammo%d,smg%d,life%d,quick%d,bigmag%d,"
                  "rifle%d,dual%d,shotgun%d,hand%d spawned=%d cut=%d runners=%d killed=%d alive=%d peak=%d bodies=%d frame=%llu kept=%s",
                  arena >= 0 ? survivalArenas()[size_t(arena)].key : "-", wave, earned, pts, spent, hitsN, heads, buysN[Ammo], buysN[Smg],
                  buysN[Life], buysN[Quick], buysN[BigMag], buysN[Rifle], buysN[Dual], buysN[Shotgun], buysN[Hand], spawned, cutSpawned,
                  fastSpawned, kills, alive, peakAlive, peakBodies, (unsigned long long)rt.frameCount, keptOk ? "ok" : "FAIL");
    std::snprintf(b + std::strlen(b), sizeof b - std::strlen(b), " retired=%d blasts=%d/%d/%d hits=%d stage=%d items=%d blastkills=%d", retiredN, rt.blastsN, rt.blastsHit, rt.blastsMulti, rt.blastHits, rt.blastsWorld, rt.blastsItems, rt.blastKills);
    if (rt.blastsDouble > 0)   // (HOTD_BLAST_DOUBLE: creatures two explosions of one frame both hit)
        std::snprintf(b + std::strlen(b), sizeof b - std::strlen(b), " blastdouble=%d", rt.blastsDouble);
    // survdogs: the dogs' rounds (begun), the dogs made / dead / ended by the stall guard, Max Ammo given (seats), the next
    std::snprintf(b + std::strlen(b), sizeof b - std::strlen(b), " dogrounds=%d dogs=%d/%d stalled=%d maxammo=%d nextdogs=%d", dogRounds,
                  dogSpawned, dogKilled, dogStalled, refills, dogWave);
    if (outN > 0)   // survrevive: the seats that went out / came back (2-4 players)
        std::snprintf(b + std::strlen(b), sizeof b - std::strlen(b), " out=%d back=%d revive=%s", outN, backN, reviveOk ? "ok" : "FAIL");
    std::string ttk = " ttk";   // (the balance runs: per weapon kills / mean frames from the first hit to the death)
    for (int w = 0; w < kWeapons; ++w) {
        char t[48];
        std::snprintf(t, sizeof t, "%s%s=%d/%.0f", w ? "," : "=", rt.weapons.def(w).key.c_str(), ttkKills[w],
                      ttkKills[w] ? double(ttkFrames[w]) / ttkKills[w] : 0.0);
        ttk += t;
    }
    return std::string(b) + " " + rt.weapons.summary() + ttk;
}

// ---- the points and the shop ----

// the signs between the rounds (arcade screen units, y up): three rows of three across the picture, each with an
// in-game model turning over it (docs/weapons.md "The shop's models"): the guns the players' handgun without the hand
// (kGunOnlyKey: the game has no rifle or shotgun model), the Hand the Magician's own hand (boss4_ter01), AMMO and
// QUICK RELOAD the game's gold coin (kinka, the hidden props' bonus), LIFE the game's life item (item_life, the props'
// drop), BIG MAG the gun. survfix2 (user 2026-11-04: the Hand's model covered its name): each sign a stack (the
// model's box, the name, the price, the seats that can buy it: survival.h kModelBox ..), 72 units a row, columns at
// x -135 / 0 / 135: the top row under the title block and players 3 / 4's points, the bottom row over players 1 / 2's
// at 4:3; each model scaled to its box and lifted to its middle (measured on the shop's pictures)
const SurvivalMode::Sign SurvivalMode::kSigns[kItems] = {
    // (measured: the gun 26 units high a unit of scale, its middle 5.5 over its origin; the life item 18.4 / +0.7; the
    // coin 13 across, centred; the Hand 25 high, turned about its middle since survfix3: weapons.cpp turning)
    {"AMMO", 500, 135.0f, 18.0f, -1, 0x3B5, 2.15f, 0.0f},
    {"SMG", 0, -135.0f, 90.0f, kSmg, 0xF1000719, 1.08f, -5.9f},
    {"LIFE", 1500, -135.0f, -54.0f, -1, 0x312, 1.5f, -1.0f},
    {"QUICK RELOAD", 2000, 0.0f, -54.0f, -1, 0x3B5, 2.15f, 0.0f},
    {"BIG MAG", 2500, 135.0f, -54.0f, -1, 0xF1000719, 1.08f, -5.9f},
    {"RIFLE", 0, 0.0f, 90.0f, kRifle, 0xF1000719, 1.15f, -6.3f},
    {"DOUBLE PISTOLS", 0, 135.0f, 90.0f, kDual, 0xF1000719, 1.08f, -5.9f},
    {"SHOTGUN", 0, -135.0f, 18.0f, kShotgun, 0xF1000719, 1.2f, -6.6f},
    {"MAGICIAN'S HAND", 0, 0.0f, 18.0f, kHand, 0x1C6C, 1.0f, 0.0f},
};

int SurvivalMode::price(int item) const
{
    if (item < 0 || item >= kItems)
        return 0;
    return kSigns[item].weapon >= 0 ? weaponDefs()[size_t(kSigns[item].weapon)].price : kSigns[item].price;
}

int SurvivalMode::signAt(float x, float y) const
{
    // the whole sign: its model's box, name, price and seats
    for (int i = 0; i < kItems; ++i)
        if (std::abs(x - kSigns[i].x) <= kSignW * 0.5f && y >= kSigns[i].y - kSignBottom && y <= kSigns[i].y + kModelUp + kModelBox * 0.5f)
            return i;
    return -1;
}

bool SurvivalMode::canBuy(const Runtime& rt, int p, int item) const
{
    if (item < 0 || item >= kItems || !shopOpen())
        return false;
    const Seat& s = seat[p & 3];
    const Weapons::Seat& w = rt.weapons.seat[p & 3];
    const port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
    if (pl.state != 5 || s.points < price(item) || price(item) <= 0)
        return false;
    if (kSigns[item].weapon >= 0)
        return !w.own[kSigns[item].weapon];
    switch (item) {
    case Ammo:   // (the reserves of the weapons held; the handgun never runs dry)
        for (int k = 1; k < kWeapons; ++k)
            if (w.own[k] && w.reserve[k] < rt.weapons.reserveFull(p, k))
                return true;
        return false;
    case Life: return pl.lives < kMaxLives;
    case Quick: return !w.quick;
    case BigMag: return !w.bigMag;
    default: return false;
    }
}

bool SurvivalMode::buy(Runtime& rt, int p, int item)
{
    if (!canBuy(rt, p, item))
        return false;
    Seat& s = seat[p & 3];
    Weapons::Seat& w = rt.weapons.seat[p & 3];
    port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
    s.points -= price(item);
    s.spent += price(item);
    ++s.buys[item];
    if (const int k = kSigns[item].weapon; k >= 0) {   // a weapon: in the hand at once, full
        rt.weapons.give(rt, p, k);
        rt.weapons.select(rt, p, k);
    }
    switch (item) {
    case Ammo:
        for (int k = 1; k < kWeapons; ++k)
            if (w.own[k])
                rt.weapons.refill(rt, p, k);
        break;
    case Life: ++pl.lives; break;
    case Quick: w.quick = true; break;
    case BigMag:   // (every magazine x 1.5: the one in the hand full at once)
        w.bigMag = true;
        rt.survivalRounds[p & 3] = rt.weapons.magazine(p, w.cur);
        pl.rounds = uint8_t(rt.weapons.magazine(p, w.cur));
        pl.empty = 0;
        for (int k = 1; k < kWeapons; ++k)
            if (w.own[k])
                w.reserve[k] = std::max(w.reserve[k], rt.weapons.reserveFull(p, k));
        break;
    default: break;
    }
    const std::string what = kSigns[item].weapon >= 0 ? weaponDefs()[size_t(kSigns[item].weapon)].name : kSigns[item].name;
    say(lang::fmt("{p}P BOUGHT {what}", {{"p", std::to_string(p + 1)}, {"what", lang::tr(what)}}), 90, Tone(Seat1 + (p & 3)));
    rt.sound_cue(0xA01351);   // (the life item's pick-up sound: the game's own for a buy)
    if (rt.log)
        rt.log("survival: " + std::to_string(p + 1) + "P bought " + what + " (" + std::to_string(s.points) + " left) at frame " +
               std::to_string(rt.frameCount));
    return true;
}

// the test players' shopping (the self-test, the balance runs; what a sensible player does): the SMG first, ammo when
// a reserve is under two magazines, a life when one was lost, Quick Reload, Big Mag, then the other weapons, lives
int SurvivalMode::testBuy(const Runtime& rt, int p) const
{
    const Weapons::Seat& w = rt.weapons.seat[p & 3];
    const port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
    if (canBuy(rt, p, Smg)) return Smg;
    // the balance runs (survfix2): HOTD_SURVIVAL_BUYER=saver: after the SMG every point saved for the Magician's Hand;
    // =upgrades: the SMG, ammo when low, Quick Reload, Big Mag, then saved for the Hand
    static const char* buyer = survivalEnv("BUYER");
    // (handsync, the online weapon tests: =rifle / dual / shotgun / hand: after the SMG every point saved for that one)
    static const int goal = !buyer ? Hand : std::strcmp(buyer, "rifle") == 0 ? Rifle : std::strcmp(buyer, "dual") == 0 ? Dual
                                          : std::strcmp(buyer, "shotgun") == 0 ? Shotgun : std::strcmp(buyer, "smg") == 0 ? Smg : Hand;
    if (buyer && seat[p & 3].buys[goal] == 0) {
        if (std::strcmp(buyer, "upgrades") == 0) {
            if (w.own[kSmg] && w.reserve[kSmg] < rt.weapons.magazine(p, kSmg) * 2 && canBuy(rt, p, Ammo)) return Ammo;
            if (canBuy(rt, p, Quick)) return Quick;
            if (w.quick && canBuy(rt, p, BigMag)) return BigMag;
            if (!w.quick || !w.bigMag) return -1;
        }
        return canBuy(rt, p, goal) ? goal : -1;
    }
    for (int k = 1; k < kWeapons; ++k)
        if (w.own[k] && w.reserve[k] < rt.weapons.magazine(p, k) * 2 && canBuy(rt, p, Ammo)) return Ammo;
    if (pl.lives < kLives && canBuy(rt, p, Life)) return Life;
    if (canBuy(rt, p, Quick)) return Quick;
    if (canBuy(rt, p, BigMag)) return BigMag;
    // (survhand: a named buyer, =smg / rifle / dual / shotgun / hand, keeps to that one weapon, bought again when used
    // up: the comparisons' runs each measure one weapon)
    static const bool one = buyer && std::strcmp(buyer, "saver") != 0 && std::strcmp(buyer, "upgrades") != 0;
    for (const int it : {Rifle, Dual, Shotgun, Hand})
        if ((!one || it == goal) && canBuy(rt, p, it)) return it;
    if (canBuy(rt, p, Life)) return Life;
    return -1;
}

// each frame (frozen): a seat first in play gets at most kLives; the weapons flag; the score = the points earned; the
// shop's models (the weapons' overlay draws them over the signs in the break)
void SurvivalMode::seats(Runtime& rt)
{
    bool any = false;
    for (int p = 0; p < rt.seat_count(); ++p) {
        Seat& s = seat[p];
        port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
        if (pl.state == 5 && !s.livesSet) {
            s.livesSet = true;
            if (pl.lives > kLives)
                pl.lives = kLives;
            pl.lives_saved = pl.lives;
        }
        if (pl.state == 5 && s.returning && p < 4) {
            // survrevive: back in play (the join's first frame): the magazine it had in the hand (the join's full gun
            // would be a free refill), then the test's check: the start lives, half the points, every weapon still held
            s.returning = false;
            Weapons::Seat& w = rt.weapons.seat[p];
            pl.rounds = uint8_t(std::clamp(s.outRounds, 0, rt.weapons.magazine(p, w.cur)));
            pl.empty = pl.rounds == 0 ? 1 : 0;
            w.mag[w.cur] = pl.rounds;
            unsigned owned = 0;
            for (int k = 0; k < kWeapons; ++k)
                owned |= w.own[k] ? 1u << k : 0u;
            const bool ok = pl.lives == startLives(rt) && s.points == s.outPoints / 2 / kHitPoints * kHitPoints && (s.outOwned & ~owned) == 0;
            reviveOk = reviveOk && ok;
            if (rt.log)
                rt.log("survival: " + std::to_string(p + 1) + "P is back at frame " + std::to_string(rt.frameCount) + ": lives " +
                       std::to_string(pl.lives) + ", points " + std::to_string(s.points) + " (half of " + std::to_string(s.outPoints) +
                       "), weapons " + std::to_string(owned) + " (had " + std::to_string(s.outOwned) + "), players in game " +
                       std::to_string(rt.mem.at<int16_t>(G::players_in_game)) + " / " + std::to_string(rt.mem.at<int16_t>(G::players_in_game_b)) +
                       ", " + (ok ? "ok" : "FAIL"));
        }
        pl.score = s.earned;
        if (pl.state == 5 && pl.lives < lastLives_[p] && phase == Wave && !stats.empty())
            stats.back().hurt += lastLives_[p] - pl.lives;
        lastLives_[p] = pl.state == 5 ? pl.lives : 0;
        for (int w = 1; w < kWeapons && p < 4; ++w)
            any = any || rt.weapons.seat[p].own[w];
    }
    smgOn = any;
    rt.weapons.shopItems.clear();
    if (shopOpen())
        for (int i = 0; i < kItems; ++i)
            rt.weapons.shopItems.push_back({kSigns[i].model, kSigns[i].x, kSigns[i].y + kModelUp + kSigns[i].lift, kSigns[i].scale});
}

// a seat's lives at its start: the Lives setting's start lives (0x51F5B1, what a join gives), at most kLives (seats())
int SurvivalMode::startLives(const Runtime& rt)
{
    return std::min<int>(rt.mem.at<uint8_t>(0x51F5B1), kLives);
}

// survrevive: seat p (state 4: its last life lost) out until the next round. No CONTINUE? and no countdown: straight to
// waiting (state 9, as the arcade's seat after its countdown ran out and its GAME OVER), but its start button held
// (Game::seatsHeldOut: player_start_check / coop4_seat_frame) and no PRESS START (hud_start_prompt, the 4P HUD). Its
// gun is off while it waits (no state 5: no shots, no aim, no shop), its points frozen (only state 5 seats hit)
void SurvivalMode::goOut(Runtime& rt, int p)
{
    Seat& s = seat[p];
    port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
    s.out = true, s.returning = false;
    s.outPoints = s.points, s.outRound = wave, s.outRounds = pl.rounds;
    s.outOwned = 0;
    for (int k = 0; k < kWeapons; ++k)
        s.outOwned |= rt.weapons.seat[p].own[k] ? 1u << k : 0u;
    ++outN;
    rt.seatsHeldOut = uint8_t(rt.seatsHeldOut | (1u << p));
    pl.continue_timer = 0;
    if (p < 2) {
        // players 1 / 2: their task to the waiting routine (player_state_set's table) and one player fewer in the
        // attack tables, as player_continue_frame does when the countdown runs out (4P: game_play counts every frame)
        if (!rt.coop4_on())
            rt.mem.at<int16_t>(G::players_in_game_b) = int16_t(rt.mem.at<int16_t>(G::players_in_game_b) - 1);
        const uint32_t task = rt.mem.u32(rt.player_rec(p) + 0xA4);
        rt.hud_player_full_draw(task);   // (its frame, torches and bullets off, as player_st4_out_of_lives does)
        rt.player_state_set(9, 1, task);
    } else {
        pl.state = 9;   // (seats 3 / 4: coop4_seat_frame reads the state)
    }
    if (rt.log)
        rt.log("survival: " + std::to_string(p + 1) + "P is out in round " + std::to_string(wave) + " at frame " + std::to_string(rt.frameCount) +
               " (points " + std::to_string(s.points) + ", no continue): back next round; players in game " +
               std::to_string(rt.mem.at<int16_t>(G::players_in_game)) + " / " + std::to_string(rt.mem.at<int16_t>(G::players_in_game_b)));
}

// survrevive: seat p back at a round's start (startWave, with the round's banner): the Survival start lives (seats():
// livesSet), half the points it had when it went out (down to the points' step, kHitPoints = 10), its weapons as they
// were (Survival keeps them), the magazine in its hand as it was (seats(): returning). Players 1 / 2 join through the
// game's own start (state 3: player_st3_join -> PlStartInit's join row: the counters, a full gun, the join's
// invulnerability; held by the camera: state 10 with 3 next, as player_start_check), players 3 / 4 through
// coop4_seat_start. Deterministic: only game state (no input, no random), the same on every PC
void SurvivalMode::comeBack(Runtime& rt, int p)
{
    Seat& s = seat[p];
    port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
    s.out = false, s.returning = true, s.livesSet = false;
    s.points = s.outPoints / 2 / kHitPoints * kHitPoints;
    rt.seatsHeldOut = uint8_t(rt.seatsHeldOut & ~(1u << p));
    rt.livesKeptSkip = uint8_t(rt.livesKeptSkip & ~(1u << p));
    ++backN;
    if (pl.state == 9) {
        if (p < 2) {
            const uint32_t task = rt.mem.u32(rt.player_rec(p) + 0xA4);
            rt.mem.set32(0x51F430, 1);   // the join flag (player_waiting_frame; player_st3_join clears it)
            if (rt.mem.at<uint8_t>(G::camera_status) & 1) {
                rt.player_state_set(3, 1, task);
            } else {
                rt.mem.set32(rt.player_rec(p) + 0x80, 3);
                rt.player_state_set(10, 1, task);
            }
            rt.sound_send3(0xA01354);   // (the start's gun-cock, as player_start_check)
        } else {
            rt.coop4_seat_start(p);
        }
    }
    rt.weapons.notice[p] = lang::fmt("{p}P IS BACK", {{"p", std::to_string(p + 1)}});
    rt.weapons.noticeUntil[p] = rt.frameCount + 180;
    if (rt.log)
        rt.log("survival: " + std::to_string(p + 1) + "P comes back in round " + std::to_string(wave) + " at frame " + std::to_string(rt.frameCount) +
               " with " + std::to_string(s.points) + " points (half of " + std::to_string(s.outPoints) + ")");
}

void SurvivalMode::hits(Runtime& rt, uint32_t obj, uint32_t seatsHit, int killer, uint32_t headSeats)
{
    if (!frozen || phase == Over)
        return;
    // (the balance runs: the time to kill, from the creature's first hit to its death, by the killer's weapon)
    auto first = std::find_if(firstHits.begin(), firstHits.end(), [&](const FirstHit& f) { return f.obj == obj; });
    if (first == firstHits.end()) {
        firstHits.push_back({obj, rt.frameCount});
        first = firstHits.end() - 1;
    }
    if (killer >= 0 && killer < 4) {
        const int w = std::clamp(rt.weapons.seat[killer].cur, 0, 7);
        ttkFrames[w] += (long long)(rt.frameCount - first->frame), ++ttkKills[w];
        firstHits.erase(first);
    }
    if (firstHits.size() > 256)   // (the ones that left unkilled)
        firstHits.erase(firstHits.begin(), firstHits.begin() + 128);
    for (int p = 0; p < 4; ++p) {
        if (((seatsHit >> p) & 1) == 0)
            continue;
        seat[p].points += kHitPoints, seat[p].earned += kHitPoints;
        ++seat[p].hits;
    }
    if (killer >= 0 && killer < 4) {
        const bool head = ((headSeats >> killer) & 1) != 0;
        const int pts = head ? kHeadKillPoints : kKillPoints;
        seat[killer].points += pts, seat[killer].earned += pts;
        ++seat[killer].kills;
        seat[killer].heads += head ? 1 : 0;
    }
    for (int p = 0; p < rt.seat_count(); ++p)
        rt.mem.at<port::a::Player>(rt.player_rec(p)).score = seat[p].earned;
}

// the gun of seat p before the game reads it (Runtime::input_task_stand_in): only while the arena is held. The shop: a
// shot at a sign (or its model) buys it, the shot not fired. The rest (the switch, the reload by hand, the held fire)
// is the weapons' (Weapons::gunInput, right after)
void SurvivalMode::gunInput(Runtime& rt, int p, bool& trigger, bool onScreen, bool held, float x, float y)
{
    (void)held;
    if (!frozen || phase == Over || p < 0 || p > 3)
        return;
    Seat& s = seat[p];
    if (rt.mem.at<port::a::Player>(rt.player_rec(p)).state != 5)
        return;
    if (shopOpen() && trigger && onScreen) {
        if (const int i = signAt(x, y); i >= 0) {
            trigger = false;
            if (!buy(rt, p, i)) {
                const std::string what = kSigns[i].weapon >= 0 ? weaponDefs()[size_t(kSigns[i].weapon)].name : kSigns[i].name;
                say(lang::fmt(s.points < price(i) ? "{p}P {what} NOT ENOUGH POINTS" : "{p}P {what} NOT NOW",
                              {{"p", std::to_string(p + 1)}, {"what", lang::tr(what)}}), 60);
            }
        }
    }
}

// ---- the runtime's part ----

// as evt_ENTB3 (0x31900) makes a creature from a record: the kind's routine (obj_kinds) as a new task of the current
// group, its work cleared, flags | 1 (shootable), type, action, hit points, position and angles, record bytes +0x26 /
// +0x27 -> +0x320 (the delay, x 10 frames; x 5 walking) / +0x324 (the start sub-state: 2 = walking)
uint32_t Runtime::survival_spawn(uint32_t kind, uint32_t flags, const float pos[3], const int32_t ang[3], int type, int action,
                              int life, uint32_t delay, uint32_t substate)
{
    const uint32_t t = task_create(mem.u32(G::obj_kinds + kind * 4), 0x3C8);
    task_clear_work(t);
    of<uint8_t>(t, 0x3C4) &= uint8_t(0xFD);
    of<uint32_t>(t, 0x58) = flags | 1;
    of<uint16_t>(t, 0xCC) = uint16_t(type);
    of<uint16_t>(t, 0x64) = uint16_t(action);
    of<uint16_t>(t, 0x72) = uint16_t(life);
    of<uint16_t>(t, 0x70) = uint16_t(life);
    for (uint32_t i = 0; i < 3; ++i) {
        of<float>(t, 0x20 + 4 * i) = pos[i];
        of<int32_t>(t, 0x2C + 4 * i) = ang[i];
    }
    of<uint32_t>(t, 0x320) = delay;
    of<uint32_t>(t, 0x324) = substate;
    return t;
}

// as evt_ENTBE (0x319C0): flags | 1, the record's place and angles, +0x35C = rec + 0x24 (the kind's own fields: kind 7's
// type, its slide's step and frames); the hit points ours (the record's +0x20 in the story)
uint32_t Runtime::survival_spawn_record(uint32_t rec, int life)
{
    const uint32_t t = task_create(mem.u32(G::obj_kinds + mem.u32(rec) * 4), 0x3C8);
    task_clear_work(t);
    of<uint32_t>(t, 0x58) = mem.u32(rec + 4) | 1;
    for (uint32_t i = 0; i < 6; ++i)
        of<uint32_t>(t, 0x20 + 4 * i) = mem.u32(rec + 8 + 4 * i);
    of<uint32_t>(t, 0x35C) = rec + 0x24;
    of<uint16_t>(t, 0x72) = uint16_t(life);
    of<uint16_t>(t, 0x70) = uint16_t(life);
    return t;
}

std::string survivalSoundDir()
{
    static const std::string dir = [] {   // (looked up once: the data folders are set at the start)
        namespace fs = std::filesystem;
        std::error_code ec;
        for (const char* sub : {"survival"})
            for (const std::string& d : lang::dataDirs())
                if (fs::is_directory(fs::path(d) / "data" / sub, ec))
                    return (fs::path(d) / "data" / sub).string();
        return lang::dataDirs().empty() ? std::string("data/survival") : (fs::path(lang::dataDirs()[0]) / "data" / "survival").string();
    }();
    return dir;
}

const char* survivalEnv(const char* name)
{
    return std::getenv((std::string("HOTD_SURVIVAL_") + name).c_str());
}

// the sounds of Survival Mode (sound_cue's first look): the lead-in plays nothing (its last music start is kept for the
// arena). The weapons' own shots and reloads: Weapons::cue (game/weapons.h). true: handled
bool Runtime::survival_cue(uint32_t cue)
{
    if (survival.leadIn()) {
        if (audio && audio->isMusicStart(cue))
            survivalMusic_ = cue;
        return true;
    }
    return false;
}

// the cut creatures' textures are in texture areas the chapters do not load (gallery_cut.cpp): their part models are
// drawn with that area's set (the cut-scene viewer's actorAreas: the chapter's own set when it is that area, else
// Chapter 4's stage's, which has every area) and the rows of their Gallery look
void Runtime::survival_areas()
{
    // survdogs: the dogs' rounds' Kenfis (types 0 and 8: Chapter 1's area 1, its look type 0's) in an arena of another
    // chapter the same way
    std::vector<SurvivalCut> list;
    if (survival.cutCreatures)
        list = survivalCuts();
    if (survival.active() && mem.at<int8_t>(G::chapter) != 0)
        for (const int t : {8, 0})   // (the last one gives the area its look: type 0's rows, galleryLook)
            list.push_back({t, "Kenfis", 1, 0, 1});
    for (const SurvivalCut& c : list) {
        std::function<void(uint32_t, int)> walk = [&](uint32_t part, int depth) {
            if (part == 0 || depth > 16 || !mem.mapped(part))
                return;
            if (const uint32_t m = mem.u32(part); m != 0)
                actorAreas[m] = c.area;
            const int32_t n = mem.at<int32_t>(part + 0x18);
            for (int32_t i = 0; i >= 0 && i < n && n <= 32; ++i)
                walk(mem.u32(part + 0x1C + 4 * uint32_t(i)), depth + 1);
        };
        const uint32_t h = mem.u32(G::creature_type_headers + 4 * uint32_t(c.type));
        const int32_t n = h ? mem.at<int32_t>(h + 0x18) : 0;
        for (int32_t i = 0; i >= 0 && i < n && n <= 32; ++i)
            walk(mem.u32(h + 0x1C + 4 * uint32_t(i)), 1);
        actorAreaLook[c.area] = c.type;
    }
}

void Runtime::survival_restore()
{
    for (auto it = survivalPatches_.rbegin(); it != survivalPatches_.rend(); ++it)
        mem.set32(it->first, it->second);
    survivalPatches_.clear();
}

// ---- the Survival table ----

SurvivalScores::SurvivalScores()
{
    static const char* kNames[10] = {"TAWHID", "ROGAN", "G", "THOMAS", "SOPHIE", "CURIEN", "KAGEO", "HERMIT", "CHARIOT", "ZOMBIE"};
    for (int i = 0; i < 10; ++i) {
        rows[i].name = kNames[i];
        rows[i].wave = 12 - i;              // (round 12 .. 3: about where a decent player gets on Normal)
        rows[i].score = (12 - i) * 2500;
    }
}

bool SurvivalScores::load(const std::string& path)
{
    std::ifstream f(path);
    if (!f)
        return false;
    std::string line;
    int n = 0;
    Row got[10];
    while (n < 10 && std::getline(f, line)) {
        std::istringstream s(line);
        Row r;
        if (!(s >> r.score >> r.wave >> r.arena))
            continue;
        std::getline(s >> std::ws, r.name);
        got[n++] = r;
    }
    for (int i = 0; i < n; ++i)
        rows[i] = got[i];
    return n > 0;
}

bool SurvivalScores::save(const std::string& path) const
{
    std::ofstream f(path);
    if (!f)
        return false;
    for (const Row& r : rows)
        f << r.score << ' ' << r.wave << ' ' << r.arena << ' ' << r.name << '\n';
    return bool(f);
}

bool SurvivalScores::isHigh(int score) const { return score > 0 && score > rows[9].score; }

int SurvivalScores::insert(const Row& r)
{
    if (!isHigh(r.score))
        return -1;
    int at = 9;
    while (at > 0 && rows[at - 1].score < r.score)
        --at;
    for (int i = 9; i > at; --i)
        rows[i] = rows[i - 1];
    rows[at] = r;
    return at;
}

} // namespace hotd::game
