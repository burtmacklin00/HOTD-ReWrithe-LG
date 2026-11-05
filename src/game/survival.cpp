// Survival Mode (ours): see survival.h and docs/survival.md
#include "game/survival.h"

#include "game/audio.h"
#include "game/lang.h"
#include "game/runtime.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
        {"courtyard", "Chapter 1: the courtyard", 0, 1, 0, {11, 28, 24, 16, 57, 29, 30}},
        {"bedroom", "Chapter 2: the green bedroom", 1, 1, 5, {11, 28, 16, 29, 51, 33, 45, 30, 32, 58}},
        {"lab", "Chapter 3: the laboratory", 2, 1, 0, {28, 61, 86, 62, 73, 83, 3, 29, 53, 65, 72, 63, 84}},
        {"furnace", "Chapter 3: the furnace bridge", 2, 1, 6, {28, 61, 86, 62, 73, 83, 3, 29, 53, 65, 72, 63, 84}},
    };
    return k;
}

int survivalArenaParse(const std::string& v)
{
    const auto& a = survivalArenas();
    for (size_t i = 0; i < a.size(); ++i)
        if (v == a[i].key) return int(i);
    const int n = std::atoi(v.c_str());
    return n >= 1 && n <= int(a.size()) ? n - 1 : v.empty() ? 0 : -1;
}

const std::vector<SurvivalCut>& survivalCuts()
{
    // docs/cut_content.md: no event record spawns these types; the ROM keeps their skeletons, part models (with
    // textures, gallery_cut.cpp) and for three of them a motion list (enemy_motion_lists 0xC6B60): Disiprin the
    // zombies' (0xC6B00), Neozom Zom's (0xC6B10), Debu Bentley's (0xC6B30). Syndy and Baba (18 joints like the
    // zombies, no list) borrow one; the area: where their textures are
    static const std::vector<SurvivalCut> k = {
        {52, "Disiprin", 2, 0, 160},
        {12, "Debu", 5, 0, 220},
        {56, "Neozom", 6, 0, 120},
        {10, "Syndy", 7, 28, 180},
        {66, "Baba", 3, 29, 140},
    };
    return k;
}

uint32_t SurvivalMode::next()
{
    rng_ = rng_ * 1103515245u + 12345u;
    return (rng_ >> 8) & 0xFFFFFF;
}

void SurvivalMode::begin(int arenaIndex)
{
    const int keepArena = arenaIndex, keepSmg = smgWave, keepMax = maxAlive;
    const bool keepCut = cutCreatures, keepBuyer = testBuyer;
    const uint32_t keepSeed = seed;
    *this = SurvivalMode();
    arena = keepArena, smgWave = keepSmg, maxAlive = keepMax, cutCreatures = keepCut, seed = keepSeed, testBuyer = keepBuyer;
    rng_ = seed;
    phase = arena >= 0 ? LeadIn : Off;
}

void SurvivalMode::startGame(Runtime& rt, int players)
{
    begin(arena);
    if (!active())
        return;
    const SurvivalArena& a = survivalArenas()[size_t(arena)];
    rt.bossNoContinue = true;   // no continue: game over as soon as every player is out (as Boss Mode)
    players_ = std::max(1, players);
    for (int p = 0; p < 4; ++p)
        rt.survivalRounds[p] = kPistolRounds;   // (the handgun's 6 until the shop changes it)
    rt.survivalScoring = true;                  // Survival's points instead of the game's score (game.h survival_hits)
    // the cut creatures without a list of their own borrow their relative's (enemy_motion_lists 0xC6B60, a table in the
    // program ROM image: set back by Runtime::survival_restore when the game ends or another starts)
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
    // more bodies than the original's 14 creature slots (game.h extraBodySlots; docs/survival.md "Limits"): slots 16.. in
    // a table outside the original map (the skin buffers grow with it at the chapter's heap reset)
    if (!rt.mem.mapped(kBodyTable))
        rt.mem.map(kBodyTable, 0x1000);
    for (uint32_t i = 0; i < 0x1000; i += 4)
        rt.mem.set32(kBodyTable + i, 0);
    rt.extraBodyTable = kBodyTable;
    rt.extraBodySlots = std::max(0, maxAlive + 8 - 14);   // (+8: corpses, the uncaptured, the players' own)
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
    if (!creatureKind(kind))
        return;
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
        bool take = false;
        if (creatureWait && !section.empty()) {
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
        const float cx = rt.mem.f32(G::camera_pos), cz = rt.mem.f32(G::camera_pos + 8);
        for (const Point& q : all_) {
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
        if (points.size() < 4)
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
        if ((rt.mem.u32(G::shot_enable) == 0 || rt.letterbox != 0) && rt.log)   // (an arena inside a cut-scene: no shots)
            rt.log("survival: the arena is in a cut-scene (shots off / letterbox): a bad arena");
        frozen = true;
        if (const char* w = survivalEnv("START_WAVE"))   // testing: the waves from this one (the limits' stress run)
            wave = std::max(0, std::atoi(w) - 1);
        phase = Break;
        breakTimer = kFirstBreak;
        {
            std::string n = survivalArenas()[size_t(arena)].name;   // (upper case, as the other banners)
            for (char& c : n) c = char(std::toupper(uint8_t(c)));
            say(n, 120);
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
        if (--breakTimer <= 0)
            startWave(rt);
        return true;
    }
    // a round (Call of Duty Zombies: the creatures come one by one, a few at once, the gap shrinking each round; rounds
    // 1-3 gentle: 2, 3, 4 at once)
    const int cap = std::min(maxAlive, special == 1 ? 4 + wave : wave <= 3 ? 1 + wave : 2 + wave);
    if (toSpawn > 0 && --spawnTimer <= 0 && alive < cap && !points.empty()) {
        spawnOne(rt);
        spawnTimer = std::max(12, int(80.0 * std::pow(0.93, double(wave - 1)))) + rand(20);
    }
    if (toSpawn == 0 && alive == 0) {
        // the round is cleared: no free life (survival2); after a special round, Max Ammo as after the dogs' rounds
        char b[64];
        std::snprintf(b, sizeof b, "ROUND %d CLEARED", wave);
        std::string s = b;
        if (special != 0) {
            for (int p = 0; p < rt.seat_count(); ++p) {
                port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
                if (pl.state != 5)
                    continue;
                if (seat[p].smg)
                    seat[p].reserve = magazine(p) * kSmgReserveMags;
                pl.rounds = uint8_t(magazine(p));
                pl.empty = 0;
                seat[p].reload = 0;
            }
            s += "  -  MAX AMMO";
        }
        say(s, 150);
        if (rt.audio)   // data/survival/wave_clear.wav when it is there (no stand-in)
            rt.audio->playExtra(survivalSoundDir(), kSurvivalSfx[3], -1, 1.0f, 1.0f);
        if (!stats.empty())
            stats.back().end = rt.frameCount;
        phase = Break;
        breakTimer = kBreakFrames;
        if (rt.log)
            rt.log("survival: round " + std::to_string(wave) + " cleared at frame " + std::to_string(rt.frameCount) + " (peak " +
                   std::to_string(peakAlive) + ")");
    }
    return true;
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
    char b[64];
    std::snprintf(b, sizeof b, "ROUND %d%s", wave, special == 1 ? "  -  RUNNERS" : special == 2 ? "  -  BRUTES" : "");
    std::string s = b;
    if (smgWave >= 1 && wave >= smgWave)   // testing: the SMG free from that round
        for (int p = 0; p < rt.seat_count(); ++p)
            if (!seat[p].smg && rt.mem.at<port::a::Player>(rt.player_rec(p)).state == 5) {
                giveSmg(rt, p);
                s = b + std::string("  -  SMG");
            }
    say(s, 120);
    if (rt.audio)   // data/survival/wave_start.wav when it is there (no stand-in)
        rt.audio->playExtra(survivalSoundDir(), kSurvivalSfx[2], -1, 1.0f, 1.0f);
    if (rt.log)
        rt.log("survival: round " + std::to_string(wave) + " (" + std::to_string(waveTotal) + " creatures" +
               (special == 1 ? ", runners" : special == 2 ? ", brutes" : "") + ") at frame " + std::to_string(rt.frameCount));
}

// the creatures of a round (Call of Duty Zombies' curve, World at War / Black Ops: 6, 8, 13, 18, then 24 rising
// slowly, from round 10 24 x 0.15 x round), a quarter more for each other player; at most 150
int SurvivalMode::roundCount(int round, int players)
{
    static const int kEarly[5] = {0, 6, 8, 13, 18};
    double n = round < 5 ? kEarly[std::max(1, round)] : round < 10 ? 24.0 + 2.0 * (round - 5) : 24.0 * 0.15 * round;
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

void SurvivalMode::spawnOne(Runtime& rt)
{
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
    pos[0] += float(rand(41) - 20);   // spread over the place (not one on another)
    pos[2] += float(rand(41) - 20);
    uint32_t flags = (p.flags & ~(1u << 19)) | 1u;   // (no headless count)
    // the runners: the start sub-state 2 (kind000_enemy_spawn: the type's motion list entry 2, its dash, and
    // walk_variant 1, so it dashes again after each stop: enemy_ai's walkOn): none in rounds 1-3, then a growing share
    // (at most 60 %); in the runners' round every one, with the AI at once
    const bool runner = special == 1 || (wave >= 4 && rand(100) < std::min(8 * (wave - 3), 60));
    if (special == 1 || (wave >= 4 && rand(100) < std::min(10 * (wave - 3), 70)))
        flags |= 1u << 22;   // AI at once (enemy_take_ai_at_once): no waiting before it comes
    const uint32_t t = rt.survival_spawn(0, flags, pos, p.ang, type, cut ? 0 : baseAction(type), life,
                                      wave >= 3 ? 0 : uint32_t(rand(3)), runner ? 2u : 0u);
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
    kills = std::max(0, spawned - alive);   // (every creature now is Survival's: the ones no longer counted were killed)
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
    if (rt.mem.at<int16_t>(G::game_mode) == 5 || rt.gameOverDone) {
        phase = Over;
        justFinished = rt.gameOverDone;
        reported_ = rt.gameOverDone;
        overFrame = rt.frameCount;
        score = 0;
        for (int p = 0; p < rt.seat_count(); ++p)
            score += seat[p].earned;
        char b[96];   // the results line over the game over scene (the table follows in the menus)
        std::snprintf(b, sizeof b, "REACHED ROUND %d   POINTS %d   KILLS %d", wave, score, kills);
        say(b, 100000);
        if (rt.log)
            rt.log("survival: " + summary(rt));
    }
}

std::vector<std::string> SurvivalMode::hudLines(const Runtime& rt) const
{
    std::vector<std::string> out;
    if (phase == Off || phase == LeadIn)
        return out;
    char b[128];
    if (phase == Break && wave >= 1) {
        std::snprintf(b, sizeof b, "ROUND %d IN %d   -   SHOOT A SIGN TO BUY", wave + 1, (breakTimer + 59) / 60);
        out.push_back(b);
    } else if (wave > 0) {
        std::snprintf(b, sizeof b, "ROUND %d   LEFT %d", wave, toSpawn + alive);
        out.push_back(b);
    }
    // a line per player in play: points, the gun's rounds (the SMG's reserve), reloading, the perks
    for (int p = 0; p < rt.seat_count(); ++p) {
        const port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
        if (pl.state != 5)
            continue;
        const Seat& s = seat[p];
        std::string gun;
        if (s.reload > 0)
            gun = "RELOADING";
        else if (s.smg)
            gun = "SMG " + std::to_string(int(pl.rounds)) + "+" + std::to_string(s.reserve);
        else
            gun = std::to_string(int(pl.rounds)) + "/" + std::to_string(magazine(p));
        std::snprintf(b, sizeof b, "%dP  %d PTS   %s%s%s", p + 1, s.points, gun.c_str(), s.quick ? "   QUICK" : "", s.bigMag ? "   BIG MAG" : "");
        out.push_back(b);
    }
    return out;
}

std::string SurvivalMode::summary(const Runtime& rt) const
{
    char b[384];
    int pts = 0, earned = 0, hitsN = 0, heads = 0, spent = 0, buysN[kItems] = {};
    for (int p = 0; p < 4; ++p) {
        pts += seat[p].points, earned += seat[p].earned, hitsN += seat[p].hits, heads += seat[p].heads, spent += seat[p].spent;
        for (int i = 0; i < kItems; ++i)
            buysN[i] += seat[p].buys[i];
    }
    std::snprintf(b, sizeof b,
                  "arena=%s round=%d score=%d points=%d spent=%d hits=%d headkills=%d buys=ammo%d,smg%d,life%d,quick%d,bigmag%d "
                  "spawned=%d cut=%d runners=%d killed=%d alive=%d peak=%d bodies=%d frame=%llu",
                  arena >= 0 ? survivalArenas()[size_t(arena)].key : "-", wave, earned, pts, spent, hitsN, heads, buysN[Ammo], buysN[Smg],
                  buysN[Life], buysN[Quick], buysN[BigMag], spawned, cutSpawned, fastSpawned, kills, alive, peakAlive, peakBodies,
                  (unsigned long long)rt.frameCount);
    return b;
}

// ---- the points and the shop ----

// the signs between the rounds (arcade screen units, y up): a row across the lower part of the picture
const SurvivalMode::Sign SurvivalMode::kSigns[kItems] = {
    {"AMMO", 500, -200.0f, -110.0f},
    {"SMG", 1000, -100.0f, -110.0f},
    {"LIFE", 1500, 0.0f, -110.0f},
    {"QUICK RELOAD", 2000, 100.0f, -110.0f},
    {"BIG MAG", 2500, 200.0f, -110.0f},
};

int SurvivalMode::signAt(float x, float y) const
{
    for (int i = 0; i < kItems; ++i)
        if (std::abs(x - kSigns[i].x) <= kSignW * 0.5f && std::abs(y - kSigns[i].y) <= kSignH * 0.5f)
            return i;
    return -1;
}

int SurvivalMode::magazine(int p) const
{
    const Seat& s = seat[p & 3];
    const int m = s.smg ? kSmgRounds : kPistolRounds;
    return s.bigMag ? m * kBigMag / 2 : m;   // (Big Mag: x 1.5: 9 / 45)
}

int SurvivalMode::reloadFrames(int p) const
{
    const Seat& s = seat[p & 3];
    const int f = s.smg ? kSmgReload : kPistolReload;
    return s.quick ? f / 2 : f;
}

bool SurvivalMode::canBuy(const Runtime& rt, int p, int item) const
{
    if (item < 0 || item >= kItems || !shopOpen())
        return false;
    const Seat& s = seat[p & 3];
    const port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
    if (pl.state != 5 || s.points < kSigns[item].price)
        return false;
    switch (item) {
    case Ammo: return s.smg && s.reserve < magazine(p) * kSmgReserveMags;   // (the handgun never runs dry)
    case Smg: return !s.smg;
    case Life: return pl.lives < kMaxLives;
    case Quick: return !s.quick;
    case BigMag: return !s.bigMag;
    default: return false;
    }
}

void SurvivalMode::giveSmg(Runtime& rt, int p)
{
    Seat& s = seat[p & 3];
    s.smg = true;
    s.reload = 0;
    rt.survivalRounds[p & 3] = magazine(p);
    s.reserve = magazine(p) * kSmgReserveMags;
    port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
    pl.rounds = uint8_t(magazine(p));   // a full magazine at once
    pl.empty = 0;
}

bool SurvivalMode::buy(Runtime& rt, int p, int item)
{
    if (!canBuy(rt, p, item))
        return false;
    Seat& s = seat[p & 3];
    port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
    s.points -= kSigns[item].price;
    s.spent += kSigns[item].price;
    ++s.buys[item];
    switch (item) {
    case Ammo: s.reserve = magazine(p) * kSmgReserveMags; break;
    case Smg: giveSmg(rt, p); break;
    case Life: ++pl.lives; break;
    case Quick: s.quick = true; break;
    case BigMag:
        s.bigMag = true;
        rt.survivalRounds[p & 3] = magazine(p);
        pl.rounds = uint8_t(magazine(p));
        pl.empty = 0;
        if (s.smg)
            s.reserve = std::max(s.reserve, magazine(p) * kSmgReserveMags);
        break;
    default: break;
    }
    char b[64];
    std::snprintf(b, sizeof b, "%dP BOUGHT %s", p + 1, kSigns[item].name);
    say(b, 90);
    rt.sound_cue(0xA01200);   // (the reload's click: the game's own sound for a buy)
    if (rt.log)
        rt.log("survival: " + std::string(b) + " (" + std::to_string(s.points) + " left) at frame " + std::to_string(rt.frameCount));
    return true;
}

// the test players' shopping (the self-test, the balance runs; what a sensible player does): the SMG first, ammo when
// its reserve is under two magazines, a life when one was lost, then Quick Reload, Big Mag, lives up to the most
int SurvivalMode::testBuy(const Runtime& rt, int p) const
{
    const Seat& s = seat[p & 3];
    const port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
    if (canBuy(rt, p, Smg)) return Smg;
    if (s.smg && s.reserve < magazine(p) * 2 && canBuy(rt, p, Ammo)) return Ammo;
    if (pl.lives < kLives && canBuy(rt, p, Life)) return Life;
    if (canBuy(rt, p, Quick)) return Quick;
    if (canBuy(rt, p, BigMag)) return BigMag;
    if (canBuy(rt, p, Life)) return Life;
    return -1;
}

// each frame (frozen): a seat first in play gets at most kLives; the magazines; the SMG flag; the score = the points earned
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
        rt.survivalRounds[p] = magazine(p);
        if (pl.rounds > magazine(p))
            pl.rounds = uint8_t(magazine(p));
        pl.score = s.earned;
        any = any || s.smg;
    }
    smgOn = any;
}

void SurvivalMode::hits(Runtime& rt, uint32_t obj, uint32_t seatsHit, int killer, uint32_t headSeats)
{
    (void)obj;
    if (!frozen || phase == Over)
        return;
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

// the gun of seat p before the game reads it (Runtime::input_task_stand_in): only while the arena is held
void SurvivalMode::gunInput(Runtime& rt, int p, bool& trigger, bool onScreen, bool held, float x, float y)
{
    if (!frozen || phase == Over || p < 0 || p > 3)
        return;
    Seat& s = seat[p];
    port::a::Player& pl = rt.mem.at<port::a::Player>(rt.player_rec(p));
    if (pl.state != 5)
        return;
    rt.survivalRounds[p] = magazine(p);
    // the shop: a shot at a sign buys it (the shot is not fired)
    if (shopOpen() && trigger && onScreen) {
        if (const int i = signAt(x, y); i >= 0) {
            trigger = false;
            if (!buy(rt, p, i))
                say(std::to_string(p + 1) + "P: " + kSigns[i].name + (s.points < kSigns[i].price ? " - NOT ENOUGH POINTS" : " - NOT NOW"), 60);
            return;
        }
    }
    // the reload in progress: the gun does nothing until it is done; then the magazine (the SMG's from its reserve)
    if (s.reload > 0) {
        trigger = false;
        s.smgTimer = 0, s.smgBurst = 0;
        if (--s.reload == 0) {
            const int mag = magazine(p);
            if (s.smg) {
                const int take = std::min(mag - int(pl.rounds), s.reserve);
                s.reserve -= std::max(0, take);
                pl.rounds = uint8_t(int(pl.rounds) + std::max(0, take));
            } else {
                pl.rounds = uint8_t(mag);
            }
            pl.empty = pl.rounds == 0 ? 1 : 0;
        }
        return;
    }
    const bool shots = rt.mem.at<int16_t>(G::game_mode) == 4 && rt.mem.u32(G::shot_enable) != 0;
    // a pull off the screen (or the reload button): the reload by hand, which takes reloadFrames (never by itself:
    // the launcher's Auto Reload is off in Survival, main.cpp autoReloadSeats). The SMG with nothing left: the handgun
    if (trigger && !onScreen && shots) {
        trigger = false;   // (the game's own instant reload not run: player_shoot / the input's refill)
        if (s.smg && s.reserve == 0 && pl.rounds == 0) {
            s.smg = false;
            say(std::to_string(p + 1) + "P: SMG EMPTY", 90);
        }
        if (int(pl.rounds) >= magazine(p))
            return;
        s.reload = reloadFrames(p);
        reloadSeat = p;
        rt.sound_cue(0xA01200);
        return;
    }
    // the SMG: a held trigger on the screen fires every kSmgPeriod frames; a pull from an input that cannot tell held
    // (pads, light guns): a burst of kSmgBurst
    if (s.smg) {
        if (trigger && onScreen) {
            s.smgTimer = 0;
            s.smgBurst = kSmgBurst - 1;
        } else if (onScreen && (held || s.smgBurst > 0) && ++s.smgTimer >= kSmgPeriod) {
            trigger = true;
            s.smgTimer = 0;
            if (!held && s.smgBurst > 0)
                --s.smgBurst;
        } else if (!onScreen) {
            s.smgBurst = 0;
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

std::string survivalSoundDir()
{
    static const std::string dir = [] {   // (looked up once: the data folders are set at the start)
        namespace fs = std::filesystem;
        std::error_code ec;
        for (const char* sub : {"survival", "horde"})   // (data/horde: the folder's name before the mode was renamed)
            for (const std::string& d : lang::dataDirs())
                if (fs::is_directory(fs::path(d) / "data" / sub, ec))
                    return (fs::path(d) / "data" / sub).string();
        return lang::dataDirs().empty() ? std::string("data/survival") : (fs::path(lang::dataDirs()[0]) / "data" / "survival").string();
    }();
    return dir;
}

const char* survivalEnv(const char* name)
{
    if (const char* v = std::getenv((std::string("HOTD_SURVIVAL_") + name).c_str()))
        return v;
    return std::getenv((std::string("HOTD_HORDE_") + name).c_str());
}

// the sounds of Survival Mode (sound_cue's first look): the lead-in plays nothing (its last music start is kept for the
// arena); with the SMG its shots and reload are its own sounds (data/survival/smg_fire.wav / smg_reload.wav, else
// stand-ins: the handgun's shot (PC cue 30) faster and cut short, the reload (cue 28) lower). true: handled
bool Runtime::survival_cue(uint32_t cue)
{
    if (survival.leadIn()) {
        if (audio && audio->isMusicStart(cue))
            survivalMusic_ = cue;
        return true;
    }
    if (!survival.smgOn)
        return false;
    const std::string& dir = survivalSoundDir();
    if (cue == 0xA01208 || cue == 0xA01207) {   // player_shoot's shot (players 1 / 2): the SMG's when that seat has it
        if (!survival.seat[cue == 0xA01208 ? 0 : 1].smg)
            return false;
        // relfix2 (user 2026-11-03): nothing to play (no file, no PC sound bank for the stand-in): the handgun's own
        // shot instead (the board's or the PC's), never silence. HOTD_SURVIVAL_SMG_SILENT (testing): the SMG's shots
        // unheard (a WAV of the same run without them: the difference is the SMG's sound alone)
        static const bool silent = survivalEnv("SMG_SILENT") != nullptr;
        const bool ok = silent || (audio && audio->playExtra(dir, kSurvivalSfx[0], 30, 1.4f, 0.4f, 0.9f));
        if (audio && audio->traceCues)
            std::printf("[sound %llu] 0x%06X -> SMG shot: %s\n", (unsigned long long)audio->frameNo, cue,
                        silent ? "silent (test)" : ok ? audio->extraSource(kSurvivalSfx[0]).c_str() : "the handgun's");
        return ok;
    }
    if (cue == 0xA01200 && survival.seat[survival.reloadSeat & 3].smg)   // the reload (the SMG's); none: the handgun's
        return audio && audio->playExtra(dir, kSurvivalSfx[1], 28, 0.8f, 1.0f);
    return false;
}

// the cut creatures' textures are in texture areas the chapters do not load (gallery_cut.cpp): their part models are
// drawn with that area's set (the cut-scene viewer's actorAreas: the chapter's own set when it is that area, else
// Chapter 4's stage's, which has every area) and the rows of their Gallery look
void Runtime::survival_areas()
{
    if (!survival.cutCreatures)
        return;
    for (const SurvivalCut& c : survivalCuts()) {
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
