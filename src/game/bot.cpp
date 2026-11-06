// Robustness testing (bot.h): the seeded human-like player and the watchdog. Testing only.
#include "game/bot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace hotd::game {

using G = A::G;
using port::a::Player;

namespace {
constexpr int kPlaying = 5, kOut = 4, kWaiting = 9;
constexpr uint64_t kSoftLockFrames = 3600;   // 60 s of game time without any progress

// on the arcade screen (the light gun's reach): the camera-space centre projected with the focal length 496
bool onScreen(Runtime& rt, uint32_t obj, float& x, float& y, float hw = 248.0f)
{
    const float cx = rt.mem.f32(obj + 0x38), cy = rt.mem.f32(obj + 0x3C), z = rt.mem.f32(obj + 0x40);
    if (!(z > 1.0f))
        return false;
    x = cx * 496.0f / z;
    y = cy * 496.0f / z;
    return std::abs(x) <= hw && std::abs(y) <= 192.0f;
}
// a part node's hit sphere centre (+0x5C, camera space) projected; walks the part tree (+0x34 children, +0x24 their
// array, +0x38 the part id) as body_find_part does
void weakParts(Runtime& rt, uint32_t part, const uint32_t* ids, size_t n, std::vector<std::pair<float, float>>& out, float hw)
{
    if (!rt.mem.mapped(part))
        return;
    const uint32_t id = rt.mem.u32(part + 0x38);
    if (std::find(ids, ids + n, id) != ids + n) {
        const float cx = rt.mem.f32(part + 0x5C), cy = rt.mem.f32(part + 0x60), z = rt.mem.f32(part + 0x64);
        if (z > 1.0f) {
            const float x = cx * 496.0f / z, y = cy * 496.0f / z;
            if (std::abs(x) <= hw && std::abs(y) <= 192.0f)
                out.push_back({x, y});
        }
    }
    const uint32_t k = rt.mem.u32(part + 0x34);
    for (uint32_t i = 0; i < k && i < 64; ++i)
        weakParts(rt, rt.mem.u32(rt.mem.u32(part + 0x24) + 4 * i), ids, n, out, hw);
}
// the screen distance from (sx, sy) to the edge of the nearest hit sphere of an object (< 0: inside), as the shot test
// sees it: the body parts' spheres (+0x5C camera space, +0x74 radius) when it tests them one by one (+0x6C bit 0),
// else the object's (+0x38, radius +0x90); 1e9 when none is in front of the camera
void partGap(Runtime& rt, uint32_t part, float sx, float sy, float& gap, int depth)
{
    if (!rt.mem.mapped(part) || depth > 16)
        return;
    const float r = rt.mem.f32(part + 0x74), z = rt.mem.f32(part + 0x64);
    if (rt.mem.u32(part) != 0 && r != 0.0f && z > 1.0f) {
        const float x = rt.mem.f32(part + 0x5C) * 496.0f / z, y = rt.mem.f32(part + 0x60) * 496.0f / z;
        gap = std::min(gap, std::sqrt((x - sx) * (x - sx) + (y - sy) * (y - sy)) - r * 496.0f / z);
    }
    const uint32_t k = rt.mem.u32(part + 0x34);
    for (uint32_t i = 0; i < k && i < 64; ++i)
        partGap(rt, rt.mem.u32(rt.mem.u32(part + 0x24) + 4 * i), sx, sy, gap, depth + 1);
}
float sphereGap(Runtime& rt, uint32_t obj, float sx, float sy)
{
    float gap = 1e9f;
    const uint32_t root = rt.mem.u32(obj + 0xC8);
    if ((rt.mem.at<uint16_t>(obj + 0x6C) & 1) && rt.mem.mapped(root)) {
        const uint32_t k = rt.mem.u32(root + 0x34);
        for (uint32_t i = 0; i < k && i < 64; ++i)
            partGap(rt, rt.mem.u32(rt.mem.u32(root + 0x24) + 4 * i), sx, sy, gap, 0);
        return gap;
    }
    const float z = rt.mem.f32(obj + 0x40);
    if (z > 1.0f) {
        const float x = rt.mem.f32(obj + 0x38) * 496.0f / z, y = rt.mem.f32(obj + 0x3C) * 496.0f / z;
        gap = std::sqrt((x - sx) * (x - sx) + (y - sy) * (y - sy)) - rt.mem.f32(obj + 0x90) * 496.0f / z;
    }
    return gap;
}
} // namespace

// hangcmp (2026-10-19): the Magician takes damage only on parts 4 / 6 / 11 / 13 (magician_hit 0x71110); a shot at his
// centre mostly sparks. Bots firing every 30-45 frames at the centre hit a weak part ~3 times in 100 shots (their
// fixed rhythm against his animation) and never killed him (the event's WAIT_ENNUM for ever); MAME's player took him
// 600 -> 410 in ~600 frames
bool aimPoint(Runtime& rt, uint32_t obj, uint32_t pick, float& x, float& y, float hw)
{
    if (rt.mem.u32(obj) == 0x71010) {   // magician_frame
        static const uint32_t ids[] = {4, 6, 11, 13};
        std::vector<std::pair<float, float>> pts;
        const uint32_t root = rt.mem.u32(obj + 0xC8);
        if (rt.mem.mapped(root))
            for (uint32_t k = 0; k < rt.mem.u32(root + 0x34) && k < 64; ++k)
                weakParts(rt, rt.mem.u32(rt.mem.u32(root + 0x24) + 4 * k), ids, 4, pts, hw);
        if (!pts.empty()) {
            x = pts[pick % pts.size()].first;
            y = pts[pick % pts.size()].second;
            return true;
        }
    }
    return onScreen(rt, obj, x, y, hw);
}

Bot::Bot(uint64_t seed, int players) : players_(players), rng_(seed * 0x9E3779B97F4A7C15ull + 12345)
{
    for (BotProfile& b : profile) {
        b.skill = 0.35f + 0.63f * rnd();
        b.reaction = int(4 + (1.0f - b.skill) * 30 + rnd() * 8);
        b.hostageShot = 0.1f * rnd() * rnd();
        b.fireGap = rint(5, 25);
        b.nearestBias = 0.3f + 0.6f * rnd();
        b.wildShot = 0.02f + 0.15f * rnd() * (1.0f - b.skill);
        b.lateReload = 0.4f * rnd();
        b.earlyReload = 0.2f * rnd();
        b.lapse = (0.2f + rnd()) / 900.0f;
        const float c = rnd();   // some never continue, some always, the rest sometimes
        b.continueChance = c < 0.25f ? 0.0f : c > 0.75f ? 1.0f : rnd();
    }
    p2NextJoin_ = uint64_t(rint(60, 9000));
    // the 4P mode's players 3 / 4: profiles from their own generator, so players 1 / 2's draws stay as before
    std::mt19937_64 r34(seed * 0x2545F4914F6CDD1Dull + 777);
    const auto u = [&] { return std::uniform_real_distribution<float>(0.0f, 1.0f)(r34); };
    for (int p = 2; p < 4; ++p) {
        BotProfile& b = profile[p];
        b.skill = 0.35f + 0.63f * u();
        b.reaction = int(4 + (1.0f - b.skill) * 30 + u() * 8);
        b.hostageShot = 0.1f * u() * u();
        b.fireGap = 5 + int(u() * 20);
        b.nearestBias = 0.3f + 0.6f * u();
        b.wildShot = 0.02f + 0.15f * u() * (1.0f - b.skill);
        b.lateReload = 0.4f * u();
        b.earlyReload = 0.2f * u();
        b.lapse = (0.2f + u()) / 900.0f;
        const float c = u();
        b.continueChance = c < 0.25f ? 0.0f : c > 0.75f ? 1.0f : u();
    }
}

void Bot::setSkill(int p, float s)
{
    BotProfile& b = profile[p];
    b.skill = std::clamp(s, 0.0f, 1.0f);
    b.reaction = int(4 + (1.0f - b.skill) * 30 + 4);
    b.wildShot = 0.02f + 0.08f * (1.0f - b.skill);
    b.fireGap = 5 + int((1.0f - b.skill) * 30.0f);
    b.hostageShot = 0.02f * (1.0f - b.skill);
}

// the researchers / hostages (a shot costs a life): by the routine's name (tools/ghidra/names.tsv)
bool Bot::isHostage(Runtime& rt, uint32_t obj)
{
    const uint32_t r = rt.mem.u32(obj);
    auto it = hostage_.find(r);
    if (it != hostage_.end())
        return it->second;
    const std::string n = rt.routineName(r);
    const size_t sp = n.find(' ');
    const std::string name = sp == std::string::npos ? "" : n.substr(sp + 1);
    const bool h = name.rfind("hostage", 0) == 0 || name.rfind("researcher", 0) == 0 || name.rfind("Researcher", 0) == 0;
    hostage_[r] = h;
    return h;
}

// cpu3 (2026-10-28): a person by the body's creature type (+0xCC), whatever routine runs it: the human types
// hostage_list_humans (0x5A050) lists (14, 15, 17, 19-23, 25-27: researchers, Sophie, the lady, the scene people)
bool Bot::isHuman(Runtime& rt, uint32_t obj)
{
    if (rt.mem.u32(obj + 0xC8) == 0)
        return false;
    const int t = rt.mem.at<int16_t>(obj + 0xCC);
    return t == 14 || t == 15 || t == 17 || (t >= 19 && t <= 23) || (t >= 25 && t <= 27);
}
// the iron-ball zombies' ball (Chapters 3 and 4): its trail of hit boxes and the ball itself take shots with a spark
// only; a player learns to shoot the zombie (MAME's recording: the zombies shot out of their wind-up). Bots that aimed
// at the nearest shootable spent their rounds on the trail and were hit by every throw (no one-player clear). Also the
// armoured Chariot's body (Chapter 4's event 0: one-player bots never got past it).
bool Bot::isDecoy(Runtime& rt, uint32_t obj)
{
    const uint32_t r = rt.mem.u32(obj);
    // the Chariot in its armour: shots at the body spark, only its weapon hand (the hit box task) takes damage
    if (r == 0x97D20)   // chariot_frame
        return rt.mem.at<uint8_t>(rt.mem.u32(obj + 0x37C) + 0xC) == 0;
    if (r == 0x97FE0)   // chariot_armour_break: the armour falling off (still type 35): every shot sparks
        return rt.mem.at<int16_t>(obj + 0xCC) == 35;
    auto it = decoy_.find(r);
    if (it != decoy_.end())
        return it->second;
    const std::string n = rt.routineName(r);
    const size_t sp = n.find(' ');
    const std::string name = sp == std::string::npos ? "" : n.substr(sp + 1);
    const bool d = name == "iron_ball_hitbox" || name == "iron_ball_follow" || name == "iron_ball_drop";
    decoy_[r] = d;
    return d;
}

void Bot::teammates(uint8_t mask, const int level[4])
{
    team_ = mask;
    for (int p = 0; p < 4; ++p) {
        if (!((mask >> p) & 1))
            continue;
        level_[p] = std::clamp(level[p], 1, 4);   // (the play: teamPlay, by kTeamLevel)
        profile[p].continueChance = 1.0f;
    }
}

uint64_t Bot::teamSeed(uint8_t mask, const int level[4])
{
    uint64_t s = 0x484F5444ull << 16 | mask;   // "HOTD"
    for (int p = 0; p < 4; ++p)
        s = s * 31 + uint64_t(std::clamp(level[p], 0, 4));
    return s;
}

// ---------------------------------------------------------------------------------------------------------------
// Computer teammates' play (user 2026-10-20: "they shoot all the time and at everything, even at creatures in the
// distance"; docs/coop4.md "Smarter CPU teammates"). Like a decent human partner: the most dangerous thing first (an
// enemy close by / attacking, a thrown axe / barrel / fireball on its way, a boss), nothing far off in the background,
// not yet in the fight, already dying or scenery; the crosshair travels to the target (no snapping) and fires in short
// bursts once it is on it; nothing to fear: no shots, and the gun is reloaded off screen (as at the arcade) in a lull
// or when empty; with several threats the teammates spread out and leave the human's target to the human. Every
// choice comes from the game state and the bot's own seeded generator: local runs repeat, and online the seat's input
// goes through the lockstep (seatInput).

namespace {
// cpuai (2026-10-26; user: "still robotic: aim-bot, no randomness, always aim at the same exact place"): the hand of a
// person. A new target: a reaction time (log-normal around `react`, now and then a longer look), then a flick that
// over- or undershoots (`gainSd` of the move, `perpSd` sideways), then micro-corrections every `corrMin..corrMax`
// frames that leave an error of about `settle` units (never zero: some shots miss); the spot is a body part (bosses:
// the weak part) and a place on it (`spot` x its radius); the trigger is pulled once the sight is on the part (now
// and then a hasty pull just before); the shake per shot; late reloads and pauses
struct TeamLevel {
    int react; float reactSd;  // frames from a new target to the first shot (median; log-normal spread)
    float hesitate;            // chance of a longer look at a new target (+12..40 frames)
    float speed, ease;         // the crosshair: at most `speed` units a frame, `ease` of the way there each frame
    float gainSd, perpSd;      // the flick's over / undershoot (fraction of the move, sd) and its sideways error
    int corrMin, corrMax;      // frames from the sight coming to rest to the next micro-correction
    float settle;              // the error a correction leaves (units, sd)
    float spot;                // where on the part (fraction of its radius, sd): the spread over the body
    float tremor;              // the hand's shake per shot (units, sd; at most 2.5 sd)
    float hasty;               // chance a frame to pull while the sight is still near the part, not on it
    int burstMin, burstMax;    // shots in a burst
    int gapMin, gapMax;        // frames between the shots of a burst
    int pauseMin, pauseMax;    // frames between bursts (the target looked at again)
    float lapse;               // chance a burst ends with a longer pause (30..90 frames: looking around)
    int emptyMin, emptyMax;    // frames from the last round to the reload
    float lateReload;          // chance the reload comes late (+30..90 frames)
    int lull;                  // frames without a threat before a partly empty gun is reloaded
    float follow;              // how much of a moving spot's move the hand follows each frame (team: 0.85 before)
};
// 1 easy: slow, often off; 2 normal: a fair partner (about a decent player's pace, not a carry); 3 hard: quick and
// sure, not inhuman (a human's flick: ~15-20 units a frame). team (2026-11-03; trailer comments: "Hard should really be
// hard"): hard quicker and surer (reaction 15 -> 11 frames, shake 2.5 -> 1.6, settle 6 -> 3.5, the hand on a moving
// spot), 4 expert: a very good player (reaction ~8 frames, a 24-unit flick, shake 1.0, no lapses)
constexpr TeamLevel kTeamLevel[5] = {
    {},
    {32, 0.35f, 0.25f, 7.0f, 0.12f, 0.26f, 0.11f, 8, 16, 14.0f, 0.75f, 5.0f, 0.045f, 1, 3, 12, 18, 34, 64, 0.20f, 24, 48, 0.40f, 90, 0.85f},
    {23, 0.30f, 0.15f, 10.0f, 0.16f, 0.19f, 0.08f, 6, 12, 10.0f, 0.65f, 3.5f, 0.035f, 2, 3, 9, 14, 20, 40, 0.12f, 14, 28, 0.25f, 60, 0.85f},
    {11, 0.22f, 0.04f, 18.0f, 0.28f, 0.10f, 0.04f, 3, 7, 3.5f, 0.42f, 1.6f, 0.015f, 2, 4, 6, 9, 8, 16, 0.03f, 6, 12, 0.05f, 30, 0.95f},
    {8, 0.18f, 0.0f, 24.0f, 0.35f, 0.07f, 0.03f, 2, 5, 2.0f, 0.30f, 1.0f, 0.008f, 3, 5, 5, 7, 4, 10, 0.0f, 4, 8, 0.0f, 25, 1.0f},
};
// a part's hit sphere on the gun's screen (centre and radius in screen units); false: not drawn / off the screen
bool partScreen(Runtime& rt, uint32_t part, float& x, float& y, float& r, float hw = 248.0f)
{
    if (!rt.mem.mapped(part) || rt.mem.u32(part) == 0 || (rt.mem.u32(part + 0x3C) & 0x80000000u))
        return false;
    const float rad = rt.mem.f32(part + 0x74), z = rt.mem.f32(part + 0x64);
    if (!(rad > 0.0f) || !(z > 1.0f))
        return false;
    x = rt.mem.f32(part + 0x5C) * 496.0f / z;
    y = rt.mem.f32(part + 0x60) * 496.0f / z;
    r = rad * 496.0f / z;
    return std::abs(x) <= hw && std::abs(y) <= 192.0f;
}
void listParts(Runtime& rt, uint32_t part, std::vector<uint32_t>& out, int depth)
{
    if (!rt.mem.mapped(part) || depth > 16)
        return;
    out.push_back(part);
    const uint32_t k = rt.mem.u32(part + 0x34);
    for (uint32_t i = 0; i < k && i < 64; ++i)
        listParts(rt, rt.mem.u32(rt.mem.u32(part + 0x24) + 4 * i), out, depth + 1);
}
// the damage a hit on the part does (part_hit's tables: the type's role class and the class's damage at the part's
// step; class 16 / 17: sparks, none); the Chariot without its armour: its own class table (chariot_part_damage_walk);
// the Magician: parts 4 / 6 / 11 / 13 only (magician_hit)
int partDamage(Runtime& rt, uint32_t obj, uint32_t part)
{
    const uint32_t routine = rt.mem.u32(obj), role = rt.mem.u32(part + 0x38);
    if (routine == 0x71010)   // magician_frame
        return role == 4 || role == 6 || role == 11 || role == 13 ? 10 : 0;
    if (rt.mem.at<int16_t>(obj + 0xCC) == 36) {   // the Chariot without armour (type 36): chariot::part_class 0x11A6CB, class_damage 0xCAC40
        const int cls = role < 64 ? rt.mem.at<int8_t>(0x11A6CB + 8 * role) : -1;
        return cls < 0 || cls > 15 ? 0 : rt.mem.at<int16_t>(0xCAC40 + 16 * uint32_t(cls));
    }
    const int32_t type = rt.mem.at<int16_t>(obj + 0xCC);
    if (type < 0 || type >= 128 || role >= 31)
        return 1;
    const uint32_t rec = G::part_roles + uint32_t(type * 31 + int32_t(role)) * 8;
    const int cls = rt.mem.at<int8_t>(rec + 3);
    if (cls == 16 || cls == 17)
        return 0;
    if (cls == 0)
        return rt.mem.at<int8_t>(rec + 1);
    if (cls < 0 || cls > 15)
        return 1;
    const int step = std::clamp(int(rt.mem.at<int8_t>(part + 0x78)), 0, 7);
    return rt.mem.at<uint16_t>(G::part_damage_table + uint32_t(type << 8) + uint32_t(cls << 4) + uint32_t(step) * 2);
}
bool hasAny(const std::string& n, std::initializer_list<const char*> words)
{
    for (const char* w : words)
        if (n.find(w) != std::string::npos)
            return true;
    return false;
}
} // namespace

bool partAim(Runtime& rt, uint32_t obj, uint32_t pick, float& x, float& y)
{
    const uint32_t root = rt.mem.u32(obj + 0xC8);
    if (!rt.mem.mapped(root))
        return false;
    std::vector<uint32_t> parts;
    listParts(rt, root, parts, 0);
    std::vector<std::pair<float, float>> spots;   // the parts on the screen that take damage, the biggest first
    std::vector<float> size;
    for (uint32_t part : parts) {
        float px, py, r;
        if (part == root || !partScreen(rt, part, px, py, r) || partDamage(rt, obj, part) <= 0)
            continue;
        size_t at = 0;
        while (at < size.size() && size[at] >= r) ++at;
        size.insert(size.begin() + std::ptrdiff_t(at), r);
        spots.insert(spots.begin() + std::ptrdiff_t(at), {px, py});
    }
    if (spots.empty())
        return false;
    x = spots[pick % spots.size()].first;
    y = spots[pick % spots.size()].second;
    return true;
}

// totd5 (The Typing of the Dead 0: a word's kill without a burst of shots): the hit a landed shot of `player` makes,
// on the biggest body part that takes damage (on the screen or not): the part's and the object's hit bits (player + 1,
// 0x100), as the weapons' blast marks a part (port shooting.cpp weapon_blast_hit); the creature's own enemy_part_hits
// takes it in its next frame. False: no such part (no body)
bool partHit(Runtime& rt, uint32_t obj, int player)
{
    const uint32_t root = rt.mem.u32(obj + 0xC8);
    if (!rt.mem.mapped(root))
        return false;
    std::vector<uint32_t> parts;
    listParts(rt, root, parts, 0);
    uint32_t best = 0;
    float bestR = -1;
    for (uint32_t part : parts) {
        if (part == root || rt.mem.u32(part) == 0 || (rt.mem.u32(part + 0x3C) & 0x80000000u) || partDamage(rt, obj, part) <= 0)
            continue;
        const float r = rt.mem.f32(part + 0x74);
        if (r > bestR) bestR = r, best = part;
    }
    if (!best)
        return false;
    const uint32_t bit = (1u << ((player & 1) + 1)) | 0x100u;
    rt.mem.at<uint32_t>(best + 0x3C) |= bit;
    rt.mem.at<uint32_t>(obj + 0x58) |= bit;
    return true;
}

Bot::Kind Bot::kindOf(Runtime& rt, uint32_t obj)
{
    if (isHostage(rt, obj) || isHuman(rt, obj))
        return kHostage;
    // a spit blob already shot (kind142_spit +0x58 bit 28: it tumbles down, blinking, still shootable): done (cpu3: the
    // CPU spent its rounds on falling blobs and never got to the spitting slugs: Chapter 1 event 11 waited for ever)
    if (rt.mem.u32(obj) == 0x63200 && ((rt.mem.u32(obj + 0x58) >> 28) & 1))
        return kDead;
    if (isDecoy(rt, obj))   // (the Chariot's armour comes off: asked each time)
        return kDecoyKind;
    const uint32_t r = rt.mem.u32(obj);
    if (auto it = kind_.find(r); it != kind_.end())
        return it->second;
    const std::string n = rt.routineName(r);
    const size_t sp = n.find(' ');
    const std::string name = sp == std::string::npos ? "" : n.substr(sp + 1);
    Kind k = kEnemy;
    if (r == 0x74150)   // magician_barrage_shot_frame (not in names.tsv)
        k = kProjectile;
    else if (name.empty())   // other unnamed routines: a body = a creature (e.g. 0x7EC60, the walkers), else scenery
        return rt.mem.u32(obj + 0xC8) != 0 ? kEnemy : kScenery;
    else if (name.rfind("kind64", 0) == 0 || name.rfind("kind17_researcher", 0) == 0 ||
             hasAny(name, {"kenkyu", "captive"}))
        k = kHostage;   // the researchers of the scripted scenes (kind 64, kind 17), the Hanged Man's captives (named
                        // hangedman_captives: they were taken for the boss): a shot costs a life too
    else if (hasAny(name, {"death", "dead", "dying", "_sink", "slug_fall", "slug_burst"}))
        k = kDead;   // (cpu3: kind142_slug_fall is a shot spitting slug flying off, kind141_slug_burst its burst)
    else if (hasAny(name, {"spitter"}))
        k = kEnemy;  // the spitting slugs (kind142_slug_spitter_frame) are creatures, not thrown things ("spit")
    else if (hasAny(name, {"flying_axe", "held_object_flight", "fireball", "barrage", "projectile", "knife_fly", "spit", "thrown"}))
        k = kProjectile;
    else if (hasAny(name, {"magician", "chariot", "hermit", "hangedman", "boss"}))
        k = kBoss;
    else if (name.rfind("prop", 0) == 0 || hasAny(name, {"lock", "sign", "route_spot", "elevator", "dropped_weapon", "held_object_frame", "item_", "kind146", "_prop"}))
        k = kScenery;   // scenery (the billiard balls of kind 146 too), padlocks, signs, items: left to the human
    kind_[r] = k;
    return k;
}

// once a frame: each shootable object's depth and its speed toward the camera
void Bot::trackFrame(Runtime& rt, uint64_t f)
{
    if (trackAt_ == f)
        return;
    trackAt_ = f;
    for (uint32_t obj : rt.shootable) {
        Track& t = track_[obj];
        const uint32_t r = rt.mem.u32(obj);
        const float z = rt.mem.f32(obj + 0x40);
        if (t.routine == r && t.f < f && f - t.f < 10)
            t.dz = 0.7f * t.dz + 0.3f * (t.z - z) / float(f - t.f);
        else
            t.dz = 0;
        t.routine = r, t.z = z, t.f = f;
        const bool out = rt.mem.at<int16_t>(obj + 0x72) > 0 && rt.mem.at<int16_t>(obj + 0x70) <= 0;
        t.down = !out ? 0 : t.down ? t.down : f;
        const Kind k = kindOf(rt, obj);
        float x, y;
        const bool on = onScreen(rt, obj, x, y);
        if (k == kHostage && rt.mem.f32(obj + 0x40) > 1.0f) {   // the hostage's speed on the screen (units / frame)
            const float hz = rt.mem.f32(obj + 0x40);
            x = rt.mem.f32(obj + 0x38) * 496.0f / hz, y = rt.mem.f32(obj + 0x3C) * 496.0f / hz;
            HostTrack& h = hostTrack_[obj];
            if (h.f && h.f < f && f - h.f < 10) {
                h.vx = 0.6f * h.vx + 0.4f * (x - h.x) / float(f - h.f);
                h.vy = 0.6f * h.vy + 0.4f * (y - h.y) / float(f - h.f);
            } else
                h.vx = h.vy = 0;
            h.x = x, h.y = y, h.f = f;
        }
        if (on) {   // the kinds audit
            Audit& a = audit_[rt.mem.u32(obj)];
            a.type = rt.mem.u32(obj + 0xC8) ? rt.mem.at<int16_t>(obj + 0xCC) : -1;
            a.kind = uint8_t(k);
            ++a.seen;
        }
    }
    if (f % 120 == 0) {
        for (auto it = track_.begin(); it != track_.end();)
            it = f - it->second.f > 120 ? track_.erase(it) : std::next(it);
        for (auto it = hostTrack_.begin(); it != hostTrack_.end();)
            it = f - it->second.f > 120 ? hostTrack_.erase(it) : std::next(it);
    }
    if (team_)
        tallyFrame(rt, f);
}

// (team 2026-11-03) what a shot thing is for the kill counts: 0 a bat, 1 a small creature (the leeches / worms /
// slugs / spiders / Murrers / frogs / rats), 2 a thrown thing, 3 another enemy, 4 a boss (the Chariot's hit box too)
int Bot::category(Runtime& rt, uint32_t obj)
{
    const Kind k = kindOf(rt, obj);
    const uint32_t r = rt.mem.u32(obj);
    if (k == kProjectile)
        return 2;
    if (k == kBoss || r == 0x97D20 || r == 0x97FE0 || r == 0x993A0)
        return 4;
    const std::string n = rt.routineName(r);
    if (hasAny(n, {" bat_"}))
        return 0;
    if (hasAny(n, {"ebitb", "ebita", "taran", "slug", "spitter", "molb", "leech", "worm", "frog", "_rat", "kind141", "kind142"}))
        return 1;
    return 3;
}

// the kill counts: a CPU seat's hit (Game::stat_hits went up, stat_last_hit the object) marks the object; a marked
// object that goes down (hit points used up, a dying routine) or leaves the shootable list within 60 frames was
// killed by it. A boss's hit points lost within 2 frames of a CPU hit on a boss count as its damage
void Bot::tallyFrame(Runtime& rt, uint64_t f)
{
    for (int p = 0; p < 4; ++p) {
        const uint32_t h = rt.stat_hits[p];
        if (((team_ >> p) & 1) && h != prevHits_[p] && rt.stat_last_hit[p] && rt.mem.mapped(rt.stat_last_hit[p])) {
            HitMark& m = hitMark_[rt.stat_last_hit[p]];
            m.f = f;
            m.cat = category(rt, rt.stat_last_hit[p]);
            if (static const bool tr = std::getenv("HOTD_TRACE_BOT") != nullptr; tr)
                std::printf("BOT f %llu P%d hit %s\n", (unsigned long long)f, p + 1, rt.routineName(rt.mem.u32(rt.stat_last_hit[p])).c_str());
            if (m.cat == 4) {
                ++tally.bossHits;
                lastBossHit_ = f;
            }
        }
        prevHits_[p] = h;
    }
    for (auto it = hitMark_.begin(); it != hitMark_.end();) {
        const uint32_t obj = it->first;
        const HitMark& m = it->second;
        bool gone = std::find(rt.shootable.begin(), rt.shootable.end(), obj) == rt.shootable.end();
        if (!gone && m.cat != 4) {
            const int hp = rt.mem.at<int16_t>(obj + 0x70), hpMax = rt.mem.at<int16_t>(obj + 0x72);
            gone = (hpMax > 0 && hp <= 0) || kindOf(rt, obj) == kDead;
        }
        if (gone && m.cat != 4 && f - m.f <= 60) {
            uint32_t* c[4] = {&tally.bats, &tally.small, &tally.projectiles, &tally.enemies};
            ++*c[m.cat];
            if (static const bool tr = std::getenv("HOTD_TRACE_BOT") != nullptr; tr)
                std::printf("BOT f %llu kill %s cat %d (hit at %llu)\n", (unsigned long long)f, rt.routineName(rt.mem.u32(obj)).c_str(), m.cat,
                            (unsigned long long)m.f);
        }
        it = gone || f - m.f > 60 ? hitMark_.erase(it) : std::next(it);
    }
    for (uint32_t obj : rt.shootable) {
        if (category(rt, obj) != 4)
            continue;
        const uint32_t owner = rt.mem.u32(obj) == 0x993A0 ? rt.mem.u32(obj + 0x18) : obj;   // (the hit box: its Chariot)
        if (!rt.mem.mapped(owner) || rt.mem.at<int16_t>(owner + 0x72) <= 0)
            continue;
        const int hp = rt.mem.at<int16_t>(owner + 0x70);
        auto it = bossHp_.find(owner);
        if (it != bossHp_.end() && hp < it->second && f - lastBossHit_ <= 2)
            tally.bossDamage += it->second - hp;
        bossHp_[owner] = hp;
    }
    for (uint32_t obj : rt.shootable)   // Tally::seen: new in the shootable list (not there last frame)
        if (std::find(prevShootable_.begin(), prevShootable_.end(), obj) == prevShootable_.end()) {
            const int c = category(rt, obj);
            if (c < 4 && !(c == 3 && kindOf(rt, obj) != kEnemy))
                ++tally.seen[c];
        }
    prevShootable_ = rt.shootable;
}

// a hostage's sphere gap at (x, y) now and where its motion on the screen takes it in the next 10 frames (cpu3: "plus a
// margin for their movement": a researcher running / pushed into the line of fire)
float Bot::hostageGap(Runtime& rt, uint32_t h, float x, float y)
{
    float g = sphereGap(rt, h, x, y);
    if (auto it = hostTrack_.find(h); it != hostTrack_.end() && (it->second.vx != 0 || it->second.vy != 0)) {
        for (float k : {5.0f, 10.0f})
            g = std::min(g, sphereGap(rt, h, x - it->second.vx * k, y - it->second.vy * k));
    }
    return g;
}

// a decoy (the iron ball's trail, the armoured Chariot) or a prop nearer than the target covers the point: the shot
// would spark on it (cpu3: "won't shoot until they are reasonably open")
bool Bot::blocked(Runtime& rt, uint32_t target, float x, float y)
{
    const float tz = rt.mem.f32(target + 0x40);
    for (uint32_t obj : rt.shootable) {
        if (obj == target)
            continue;
        const Kind k = kindOf(rt, obj);
        if (k != kDecoyKind && k != kScenery)
            continue;
        if (rt.mem.f32(obj + 0x40) < tz - 2.0f && sphereGap(rt, obj, x, y) < 0.0f)
            return true;
    }
    return false;
}

bool Bot::hostageCovers(Runtime& rt, uint32_t target, float x, float y)
{
    const float tz = rt.mem.f32(target + 0x40);
    for (uint32_t obj : rt.shootable)
        if (obj != target && kindOf(rt, obj) == kHostage && rt.mem.f32(obj + 0x40) < tz + 4.0f && hostageGap(rt, obj, x, y) < 6.0f)
            return true;
    return false;
}

void Bot::printAudit(Runtime& rt) const
{
    static const char* kn[] = {"enemy", "projectile", "boss", "hostage", "decoy", "dead", "scenery"};
    for (const auto& [r, a] : audit_)
        std::printf("AUDIT %s type %d kind %s seen %u cand %u target %u cpu_shots %u\n", rt.routineName(r).c_str(), a.type, a.kind < 7 ? kn[a.kind] : "?", a.seen, a.cand, a.target, a.shots);
}

// how much of a threat `obj` is now (0: not worth a shot) and where it is on the screen; damageable: a shot at it
// counts now (creatures take damage only while the camera is at a stop, enemy_part_hits; they attack only then too)
float Bot::threat(Runtime& rt, uint32_t obj, bool& damageable, float& x, float& y, bool* vis)
{
    damageable = false;
    if (vis)
        *vis = true;
    const Kind k = kindOf(rt, obj);
    if (k == kHostage || k == kDecoyKind || k == kDead || k == kScenery)
        return 0;
    const float z = rt.mem.f32(obj + 0x40);
    if (!aimPoint(rt, obj, 0, x, y, halfW_)) {
        // cpu3 (user 2026-10-27: "when they are not visible they know the general location but won't shoot until they
        // are reasonably open and visible"): a creature just off the screen (up to a screen's width / height beyond the
        // edge, in front of the camera) is known: the sight waits at the edge nearest it, no shots (damageable false)
        if (!vis || k == kProjectile || !(z > 1.0f) || z > 130.0f)
            return 0;
        const float ox = rt.mem.f32(obj + 0x38) * 496.0f / z, oy = rt.mem.f32(obj + 0x3C) * 496.0f / z;
        if (std::abs(ox) > 2.0f * halfW_ || std::abs(oy) > 384.0f)
            return 0;
        const int hp = rt.mem.at<int16_t>(obj + 0x70), hpMax = rt.mem.at<int16_t>(obj + 0x72);
        if (hpMax > 0 && hp <= 0)
            return 0;
        x = std::clamp(ox, -(halfW_ - 18.0f), halfW_ - 18.0f);
        y = std::clamp(oy, -175.0f, 175.0f);
        *vis = false;
        return std::max(60.0f, 600.0f - 3.5f * z);
    }
    const bool stopped = rt.mem.u32(G::camera_stopped) == 2 || rt.mem.at<uint8_t>(G::chapter) == 5;
    const auto t = track_.find(obj);
    const float dz = t != track_.end() ? t->second.dz : 0.0f;
    if (k == kProjectile) {   // thrown at a player: shoot it down, the nearer the sooner
        damageable = true;
        return z < 40.0f ? 1250.0f : std::max(200.0f, 950.0f - 3.0f * z);
    }
    // team (2026-11-03; trailer comments: "they don't shoot the bats"): a creature without a body (the bats, kinds
    // 66-68) runs its own hit check (bat_hit_check: the shot flag, at any time), not enemy_part_hits' (the camera at a
    // stop): a teammate shoots it while the camera moves too. It takes no attack position either: it is in the fight
    // once near (below). (Teammates only: The Typing of the Dead 0's classes stay as they were)
    const bool ownHits = team_ && rt.mem.u32(obj + 0xC8) == 0;
    damageable = stopped || (ownHits && k == kEnemy);
    if (k == kBoss)
        return 700.0f + (z < 40.0f ? 300.0f : 0.0f);
    const int hp = rt.mem.at<int16_t>(obj + 0x70), hpMax = rt.mem.at<int16_t>(obj + 0x72);
    // dying (the body falls; more shots only blow it apart). Some kinds go on with their hit points used up (kind 70
    // fights on at -30): one still shootable 2.5 s later is alive after all (a body takes up to ~2 s to go down)
    if (hpMax > 0 && hp <= 0 && (t == track_.end() || t->second.down == 0 || t->second.f - t->second.down < 150))
        return 0;
    const bool engaged = rt.mem.at<int8_t>(obj + 0x2E7) >= 0 || (ownHits && z < 200.0f);   // took an attack position
    if (z > 260.0f || (!engaged && z > 130.0f))   // far off in the background / not yet in the fight
        return 0;
    float s = std::max(60.0f, 1000.0f - 3.5f * z);
    if (engaged)
        s += 150.0f;
    if (dz > 0.4f)   // coming closer
        s += 100.0f;
    if (z < 35.0f)   // about to hit
        s += 300.0f;
    return s;
}

void Bot::teamPlay(Runtime& rt, int p, uint64_t f)
{
    P& s = pl_[p];
    const TeamLevel& L = kTeamLevel[std::clamp(level_[p], 1, 4)];
    const Player& pl = rt.mem.at<Player>(rt.player_rec(p));
    Runtime::GunInput& g = rt.gun[p];
    trackFrame(rt, f);
    for (int q = 0; q < rt.seat_count(); ++q)   // where the other players shoot (the human, the other PC's seats)
        if (q != p && !((team_ >> q) & 1) && rt.gun[q].trigger && rt.gun[q].onScreen)
            fired_[q] = f;
    // (shots off: a pull is a skip). cpu3: shots on with the bars up count too: letterbox_task's scene_state 0 puts the
    // bars on with shot_enable 1 (Chapter 1 event 11, the sewer slugs: the CPU waited for the bars to go and never shot,
    // so the event's "all enemies dead" wait never ended); a pull there is no skip (main.cpp: skips need shots off)
    const bool canShoot = rt.mem.u32(G::shot_enable) != 0;

    // the candidates and their threat; what the others are on
    // vis: on the screen (else known, tracked at the edge, never shot); held: holds a hostage / is right by one (cpu3:
    // the rescue is left to the humans; a shot only when it is clearly on the creature's open part)
    struct Cand { uint32_t obj; float x, y, score; bool dmg, vis, held; };
    Cand c[64];
    int n = 0, live = 0;
    std::vector<uint32_t> hostages, captors;
    for (uint32_t obj : rt.shootable)
        if (kindOf(rt, obj) == kHostage) {
            hostages.push_back(obj);
            // a hostage scene's person (kind 100 / 103, routines hostage*): +0x390 its captor
            if (isHostage(rt, obj) && rt.mem.u32(obj + 0x390))
                captors.push_back(rt.mem.u32(obj + 0x390));
        }
    for (uint32_t obj : rt.shootable) {
        float x, y;
        if (kindOf(rt, obj) == kHostage)
            continue;
        bool dmg, vis;
        const float sc = threat(rt, obj, dmg, x, y, &vis);
        if (sc > 0 && n < 64) {
            bool held = std::find(captors.begin(), captors.end(), obj) != captors.end();
            for (uint32_t h : hostages)
                held = held || hostageGap(rt, h, x, y) < 40.0f;
            c[n++] = {obj, x, y, sc - (held ? 600.0f : 0.0f), dmg && vis, vis, held};
            live += dmg && vis ? 1 : 0;
            ++audit_[rt.mem.u32(obj)].cand;
        }
    }
    if (live > 0)
        s.lastThreat = f;
    if (static const bool tr = std::getenv("HOTD_TRACE_BOT") != nullptr; tr && f % 120 == 0) {
        std::printf("BOT f %llu P%d states", (unsigned long long)f, p + 1);
        for (int q = 0; q < rt.seat_count(); ++q)
            std::printf(" %d", int(rt.mem.at<Player>(rt.player_rec(q)).state));
        std::printf(" in_game %d solo_p2 %d\n", int(rt.mem.at<int16_t>(G::players_in_game)), int(rt.mem.at<int8_t>(G::solo_player_is_p2)));
    }
    if (static const bool tr = std::getenv("HOTD_TRACE_BOT") != nullptr; tr && f % 120 == 0)
        for (uint32_t obj : rt.shootable) {
            bool dmg;
            float x = 0, y = 0;
            const float sc = threat(rt, obj, dmg, x, y);
            std::printf("BOT f %llu P%d sees %s kind %d z %.1f scr %.0f %.0f hp %d/%d slot %d threat %.0f dmg %d camstop %u\n",
                        (unsigned long long)f, p + 1, rt.routineName(rt.mem.u32(obj)).c_str(), int(kindOf(rt, obj)),
                        rt.mem.f32(obj + 0x40), x, y, int(rt.mem.at<int16_t>(obj + 0x70)), int(rt.mem.at<int16_t>(obj + 0x72)),
                        int(rt.mem.at<int8_t>(obj + 0x2E7)), sc, dmg ? 1 : 0, rt.mem.u32(G::camera_stopped));
        }
    // several threats: spread out (the others' targets count less). Developer: HOTD_BOT_NO_SPREAD=1 turns it off
    static const bool spread = std::getenv("HOTD_BOT_NO_SPREAD") == nullptr;
    if (live >= 2 && spread)
        for (int q = 0; q < rt.seat_count(); ++q) {
            if (q == p || rt.mem.u32(rt.player_rec(q)) != 5)
                continue;
            uint32_t claim = 0;
            if ((team_ >> q) & 1)
                claim = pl_[q].target;
            else if (rt.gun[q].onScreen && fired_[q] + 45 >= f) {   // the human's: the target nearest its sight
                float best = 50.0f * 50.0f;
                for (int i = 0; i < n; ++i) {
                    const float d = (c[i].x - rt.gun[q].x) * (c[i].x - rt.gun[q].x) + (c[i].y - rt.gun[q].y) * (c[i].y - rt.gun[q].y);
                    if (d < best)
                        best = d, claim = c[i].obj;
                }
            }
            for (int i = 0; i < n; ++i)
                if (c[i].obj == claim && claim)
                    c[i].score -= ((team_ >> q) & 1) ? 400.0f : 450.0f;
        }
    // the target: the biggest threat a shot can hurt now (else one to aim at while the camera moves); the current one
    // is kept unless something clearly worse comes up
    int best = -1, cur = -1;
    float bestScore = -1e9f;
    for (int i = 0; i < n; ++i) {
        float sc = c[i].score + (c[i].dmg ? 2000.0f : 0.0f);
        if (c[i].obj == s.target) {
            sc += 200.0f;
            cur = i;
        }
        if (sc > bestScore)
            bestScore = sc, best = i;
    }
    if (best >= 0 && best != cur) {
        const uint32_t prevTarget = s.target;
        const uint64_t prevAcquired = s.acquired;
        s.target = c[best].obj;
        s.score = c[best].score;
        s.acquired = f;
        float sx = c[best].x, sy = c[best].y, sr = 0;
        if (pickSpot(rt, p, s.target, kindOf(rt, s.target) == kBoss, hostages))
            spotPoint(rt, p, s.target, sx, sy, sr);
        // the flick: over / undershoot along the move, a little sideways
        const float dx = sx - s.aimX, dy = sy - s.aimY;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float gain = std::clamp(gauss(L.gainSd), -0.5f, 0.5f), side = std::clamp(gauss(L.perpSd), -0.3f, 0.3f);
        s.errX = dx * gain - dy * side;
        s.errY = dy * gain + dx * side;
        s.corrAt = 0;
        s.burst = rint(L.burstMin, L.burstMax);
        float react = float(L.react) * std::exp(std::clamp(gauss(L.reactSd), -0.7f, 1.0f));
        // cpu3: switching targets in the middle of a fight (the last one taken less than 2 s ago) is quicker than
        // the first look: the spitting slugs' blobs (a big threat once near) and the slugs took turns, every switch
        // waited a full reaction and the CPU hardly fired (Chapter 1 event 11 never ended on Easy)
        const bool inFight = prevTarget != 0 && f - prevAcquired < 120;
        if (inFight)
            react *= 0.4f;
        else if (rnd() < L.hesitate)   // a longer look first
            react += float(rint(12, 40));
        s.nextShot = std::max(s.nextShot, f + uint64_t(react * (0.6f + 0.4f * std::min(1.0f, dist / 120.0f))));
        if (static const bool tr = std::getenv("HOTD_TRACE_BOT") != nullptr; tr)
            std::printf("BOT f %llu P%d target %s z %.1f score %.0f dmg %d (%d candidates)\n", (unsigned long long)f, p + 1,
                        rt.routineName(rt.mem.u32(s.target)).c_str(), rt.mem.f32(s.target + 0x40), c[best].score,
                        c[best].dmg ? 1 : 0, n);
    } else if (best < 0)
        s.target = 0;
    if (best >= 0) {
        ++audit_[rt.mem.u32(c[best].obj)].target;
        // a tracked target coming into view: a short look before the first shot (a person sees it appear)
        if (c[best].vis && !s.targetVis)
            s.nextShot = std::max(s.nextShot, f + uint64_t(float(L.react) * (0.4f + 0.3f * rnd())));
        s.targetVis = c[best].vis;
    }
    if (static const bool tr = std::getenv("HOTD_TRACE_BOT") != nullptr; tr && live >= 2 && s.target) {
        // the spread: frames with several threats, and of them those with another teammate on the same target
        static uint64_t several = 0, shared = 0;
        ++several;
        for (int q = 0; q < rt.seat_count(); ++q)
            if (q != p && ((team_ >> q) & 1) && pl_[q].target == s.target) {
                ++shared;
                break;
            }
        if (several % 2000 == 0)
            std::printf("BOT spread: %llu seat-frames with several threats, %llu on a teammate's target\n",
                        (unsigned long long)several, (unsigned long long)shared);
    }

    // the crosshair travels toward the spot (a part of the target + the hand's error: the flick's over / undershoot,
    // then the micro-corrections); with nothing to shoot it drifts slowly to a rest point on its side of the screen
    // (players 1 / 3 left, 2 / 4 right: the sights do not stack up in the middle)
    static constexpr float kRest[4][2] = {{-60, -30}, {60, -30}, {-120, -60}, {120, -60}};
    float tx = kRest[p][0], ty = kRest[p][1], spotX = 0, spotY = 0, spotR = 0;
    const Cand* t = best >= 0 ? &c[best] : nullptr;
    bool spotOk = false;   // a real spot (a part clear of the hostages), not the fallback centre
    if (t) {
        const bool boss = kindOf(rt, t->obj) == kBoss;
        // the spot gone (the part cut off, out of sight) or a boss's weak part come into view: another one
        spotOk = true;
        if (!spotPoint(rt, p, t->obj, spotX, spotY, spotR) || (boss && !s.weak && f % 15 == uint64_t(p)))
            if (!pickSpot(rt, p, t->obj, boss, hostages) || !spotPoint(rt, p, t->obj, spotX, spotY, spotR))
                spotX = t->x, spotY = t->y, spotR = 6.0f, spotOk = false;
        tx = spotX + s.errX, ty = spotY + s.errY;
        // cpu3: the hand follows a moving spot (a person tracks what crawls / walks across the screen): the spot's
        // move since the last frame carried over (85 %, at most `speed`). Before, the eased sight lagged behind a
        // moving part and never came to rest on it: the sewer slugs (enemy_main type 46, crawling) were aimed at for
        // ever and never shot (Chapter 1 event 11 waited for ever)
        if (spotOk && s.followObj == t->obj && s.followPart == s.spotPart) {
            float mx = (spotX - s.followX) * L.follow, my = (spotY - s.followY) * L.follow;
            const float ml = std::sqrt(mx * mx + my * my);
            if (ml > L.speed)
                mx *= L.speed / ml, my *= L.speed / ml;
            s.aimX += mx, s.aimY += my;
        }
        s.followObj = spotOk ? t->obj : 0;
        s.followPart = s.spotPart, s.followX = spotX, s.followY = spotY;
    } else
        s.followObj = 0;
    tx = std::clamp(tx, -(halfW_ - 2.0f), halfW_ - 2.0f);
    ty = std::clamp(ty, -190.0f, 190.0f);
    {
        float dx = (tx - s.aimX) * (t ? L.ease : 0.03f), dy = (ty - s.aimY) * (t ? L.ease : 0.03f);
        const float len = std::sqrt(dx * dx + dy * dy), lim = t ? L.speed : 1.5f;
        if (len > lim)
            dx *= lim / len, dy *= lim / len;
        const float rx = tx - s.aimX, ry = ty - s.aimY;   // the last bit: straight on (no endless easing)
        if (t && rx * rx + ry * ry < 1.0f)
            dx = rx, dy = ry;
        s.aimX += dx, s.aimY += dy;
        // come to rest: a micro-correction a moment later (the error mostly gone, never all of it)
        if (t && rx * rx + ry * ry < 2.25f && s.corrAt == 0)
            s.corrAt = f + uint64_t(rint(L.corrMin, L.corrMax));
        if (t && s.corrAt && f >= s.corrAt) {
            const float keep = 0.1f + 0.3f * rnd();
            s.errX = s.errX * keep + std::clamp(gauss(L.settle), -2.5f * L.settle, 2.5f * L.settle);
            s.errY = s.errY * keep + std::clamp(gauss(L.settle), -2.5f * L.settle, 2.5f * L.settle);
            s.corrAt = 0;
        }
    }

    // reload off screen (as at the arcade): after the last round (now and then late), or with rounds gone in a lull,
    // never in a cut-scene (an off-screen pull there is a skip request)
    const int full = rt.pm_full_rounds(p);
    if (canShoot && s.reloadAt == 0 && (pl.rounds == 0 || (pl.rounds < full && f - s.lastThreat >= uint64_t(L.lull))))
        s.reloadAt = f + uint64_t(pl.rounds == 0 ? rint(L.emptyMin, L.emptyMax) + (rnd() < L.lateReload ? rint(30, 90) : 0)
                                                 : rint(2, 8));
    if (s.reloadAt) {
        if (!canShoot || pl.rounds >= full)
            s.reloadAt = 0;
        else if (f >= s.reloadAt) {
            g.trigger = true;   // off screen
            g.onScreen = false;
            g.x = s.aimX > 0 ? 400.0f : -400.0f;
            g.y = s.aimY;
            s.reloadAt = 0;
            s.nextShot = std::max(s.nextShot, f + 6);
            ++reloads;
            return;
        }
    }
    if (static const bool tr = std::getenv("HOTD_TRACE_BOT") != nullptr; tr && t && f % 30 == 0)
        std::printf("BOT f %llu P%d hold? dmg %d vis %d held %d can %d rounds %d wait %lld sight %.0f %.0f spot %.0f %.0f r %.1f to %.0f %.0f\n",
                    (unsigned long long)f, p + 1, t->dmg, t->vis, t->held, canShoot, int(pl.rounds), (long long)(s.nextShot - f),
                    s.aimX, s.aimY, spotX, spotY, spotR, tx, ty);
    if (!t || !t->dmg || !canShoot || pl.rounds == 0 || f < s.nextShot)
        return;
    // the pull: once the sight is on the part (as the player sees it: within its radius), now and then a hasty one
    // while it is still near
    // while it is still near; or once the hand has come to rest with an error too small to see (`settle`: the shot
    // may miss a small or moving part)
    const float ex = spotX - s.aimX, ey = spotY - s.aimY, on = std::max(3.0f, 0.9f * spotR);
    const float d2 = ex * ex + ey * ey;
    const float rx = tx - s.aimX, ry = ty - s.aimY;
    const bool rest = rx * rx + ry * ry < 9.0f && s.errX * s.errX + s.errY * s.errY < 4.0f * L.settle * L.settle;
    if (d2 > on * on && !rest && !(d2 < 6.25f * on * on && rnd() < L.hasty))
        return;
    // open and in view (cpu3): the spot inside the screen (a part clamped at the edge is mostly off it) and no decoy /
    // prop in front of it
    if (std::abs(spotX) > halfW_ - 10.0f || std::abs(spotY) > 182.0f || blocked(rt, t->obj, s.aimX, s.aimY)) {
        ++heldHidden;
        if (rnd() < 0.05f)
            s.spotU = 1e9f;
        return;
    }
    // the shot lands within 2.5 sd of the shake; a hostage / researcher whose hit sphere comes within that reach (+ a
    // margin, + where its motion takes it in the next 10 frames: hostageGap) of the sight: no shot (a partner never
    // risks it), and now and then the next look takes another spot. cpu3 (user 2026-10-27: "they still shoot at
    // researchers but less often"): the margin 6 -> 12
    const float reach = 2.5f * L.tremor + 12.0f;
    for (uint32_t h : hostages)
        if (hostageGap(rt, h, s.aimX, s.aimY) < reach) {
            ++heldForHostage;
            if (rnd() < 0.1f)
                s.spotU = 1e9f;   // (spotPoint fails: pickSpot next frame)
            return;
        }
    // a creature holding a hostage / right by one: only a sure shot on its open part (a part that takes damage, the
    // sight well inside it, the hostages a further 16 away); else the humans do the rescue
    if (t->held) {
        bool sure = spotOk && d2 <= 0.36f * spotR * spotR && (s.spotPart == 0 || partDamage(rt, t->obj, s.spotPart) > 0);
        for (uint32_t h : hostages)
            sure = sure && hostageGap(rt, h, s.aimX, s.aimY) >= reach + 16.0f;
        if (!sure) {
            ++heldCaptor;
            return;
        }
    }
    const float sx = s.aimX + std::clamp(gauss(L.tremor), -2.5f * L.tremor, 2.5f * L.tremor);
    const float sy = s.aimY + std::clamp(gauss(L.tremor), -2.5f * L.tremor, 2.5f * L.tremor);
    g.onScreen = g.trigger = true;
    g.x = std::clamp(sx, -(halfW_ - 1.0f), halfW_ - 1.0f);
    g.y = std::clamp(sy, -191.0f, 191.0f);
    ++shots;
    ++audit_[rt.mem.u32(t->obj)].shots;
    for (uint32_t h : hostages)   // (measuring: a shot that lands on a hostage's sphere; must stay 0)
        if (sphereGap(rt, h, g.x, g.y) < 0.0f) {
            ++onHostage;
            break;
        }
    for (uint32_t h : hostages)   // (and one within 20 units of one: a near miss the player sees)
        if (sphereGap(rt, h, g.x, g.y) < 20.0f) {
            ++nearHostage;
            break;
        }
    if (kindOf(rt, t->obj) == kBoss) {
        ++bossShots;
        bossWeakAimed += s.weak ? 1 : 0;
        bossWeakShots += s.weak && (g.x - spotX) * (g.x - spotX) + (g.y - spotY) * (g.y - spotY) <= spotR * spotR ? 1 : 0;
    }
    if (static const bool tr = std::getenv("HOTD_TRACE_BOT") != nullptr; tr) {
        std::printf("BOT f %llu P%d shot %.0f %.0f at %s spot %.0f %.0f r %.0f%s (hostage gaps", (unsigned long long)f, p + 1,
                    g.x, g.y, rt.routineName(rt.mem.u32(t->obj)).c_str(), spotX, spotY, spotR, s.weak ? " weak" : "");
        for (uint32_t h : hostages)
            std::printf(" %.0f", sphereGap(rt, h, g.x, g.y));
        std::printf(")\n");
    }
    if (--s.burst > 0)
        s.nextShot = f + uint64_t(rint(L.gapMin, L.gapMax));
    else {   // a burst over: a pause (now and then a longer one), and maybe another spot on the body
        s.burst = rint(L.burstMin, L.burstMax);
        s.nextShot = f + uint64_t(rint(L.pauseMin, L.pauseMax) + (rnd() < L.lapse ? rint(30, 90) : 0));
        if (!s.weak && rnd() < 0.4f)
            s.spotU = 1e9f;
    }
}

// the spot on the target (pickSpot): a body part with a place on it in its radius (U / V), or the object's centre
bool Bot::spotPoint(Runtime& rt, int p, uint32_t obj, float& x, float& y, float& r)
{
    P& s = pl_[p];
    if (s.spotU > 1e8f)
        return false;
    if (s.spotPart) {
        if (rt.mem.u32(s.spotPart + 0x40) != obj || !partScreen(rt, s.spotPart, x, y, r, halfW_))
            return false;
    } else {
        if (!aimPoint(rt, obj, 0, x, y, halfW_))
            return false;
        const float z = rt.mem.f32(obj + 0x40);
        r = std::clamp(rt.mem.f32(obj + 0x90) * 496.0f / std::max(1.0f, z) * 0.6f, 3.0f, 40.0f);
    }
    x = std::clamp(x + s.spotU * r, -(halfW_ - 1.0f), halfW_ - 1.0f);
    y = std::clamp(y + s.spotV * r, -191.0f, 191.0f);
    return true;
}

// a new spot on the target: bosses the part a hit hurts most (the weak spot: the Chariot's open plates, the Hanged
// Man's / the Hermit's by their part tables, the Magician's parts 4 / 6 / 11 / 13), the one nearest the sight; other
// creatures a part drawn by its size and the damage it takes (the head and the trunk mostly: a person aims at the
// body, not at one pixel); never one close to a hostage. The place on it: spread by `spot` x its radius
bool Bot::pickSpot(Runtime& rt, int p, uint32_t obj, bool boss, const std::vector<uint32_t>& hostages)
{
    P& s = pl_[p];
    const TeamLevel& L = kTeamLevel[std::clamp(level_[p], 1, 4)];
    const float spread = boss ? 0.6f * L.spot : L.spot;
    s.spotU = std::clamp(gauss(spread), -1.1f, 1.1f);
    s.spotV = std::clamp(gauss(spread), -1.1f, 1.1f);
    s.spotPart = 0;
    s.weak = false;
    const uint32_t root = rt.mem.u32(obj + 0xC8);
    std::vector<uint32_t> parts;
    if ((rt.mem.at<uint16_t>(obj + 0x6C) & 1) && rt.mem.mapped(root))
        for (uint32_t k = 0; k < rt.mem.u32(root + 0x34) && k < 64; ++k)
            listParts(rt, rt.mem.u32(rt.mem.u32(root + 0x24) + 4 * k), parts, 0);
    struct C { uint32_t part; float x, y, r; int dmg; };
    std::vector<C> cs;
    int maxDmg = 0;
    for (uint32_t part : parts) {
        float x, y, r;
        if (!partScreen(rt, part, x, y, r, halfW_))
            continue;
        bool nearH = false;
        for (uint32_t h : hostages)
            nearH = nearH || hostageGap(rt, h, x, y) < r + 18.0f;   // (cpu3: 10 -> 18, with its motion)
        if (nearH)
            continue;
        const int d = partDamage(rt, obj, part);
        cs.push_back({part, x, y, r, d});
        maxDmg = std::max(maxDmg, d);
    }
    if (cs.empty()) {   // no parts (or all near a hostage): the centre, unless a hostage is there
        float x, y;
        if (!aimPoint(rt, obj, 0, x, y, halfW_))
            return false;
        for (uint32_t h : hostages)
            if (hostageGap(rt, h, x, y) < 18.0f)
                return false;
        s.weak = rt.mem.u32(obj) == 0x993A0;   // chariot_hitbox_task: the armoured Chariot's weak spot (his weapon hand)
        return true;
    }
    size_t pick = 0;
    if (boss && maxDmg > 0) {   // the weak part nearest the sight (all alike, as the Hanged Man's: a big one near it)
        float bd = 1e30f;
        for (size_t i = 0; i < cs.size(); ++i)
            if (cs[i].dmg == maxDmg) {
                const float d = std::sqrt((cs[i].x - s.aimX) * (cs[i].x - s.aimX) + (cs[i].y - s.aimY) * (cs[i].y - s.aimY)) -
                                3.0f * cs[i].r;
                if (d < bd)
                    bd = d, pick = i;
            }
        s.weak = true;
    } else {   // by size and damage
        float sum = 0;
        std::vector<float> w(cs.size());
        for (size_t i = 0; i < cs.size(); ++i)
            sum += w[i] = cs[i].r * (cs[i].dmg > 0 ? 0.5f + float(cs[i].dmg) / float(std::max(1, maxDmg)) : 0.1f);
        float u = rnd() * sum;
        for (pick = 0; pick + 1 < cs.size() && u > w[pick]; ++pick)
            u -= w[pick];
    }
    s.spotPart = cs[pick].part;
    if (static const bool tr = std::getenv("HOTD_TRACE_BOT") != nullptr; tr && boss) {
        std::printf("BOT P%d boss spot %s type %d role %u dmg %d (max %d, %zu parts:", p + 1, rt.routineName(rt.mem.u32(obj)).c_str(),
                    int(rt.mem.at<int16_t>(obj + 0xCC)), rt.mem.u32(cs[pick].part + 0x38), cs[pick].dmg, maxDmg, cs.size());
        for (const C& k : cs)
            std::printf(" %u:%d", rt.mem.u32(k.part + 0x38), k.dmg);
        std::printf(")\n");
    }
    return true;
}

bool Bot::seatInput(Runtime& rt, int p, uint64_t f, Runtime::GunInput& out)
{
    if (rt.attract) {   // autostart: no teammate start in the attract (Bot::frame)
        out = {};
        pl_[p].startPlanned = false;
        pl_[p].lastState = -1;
        return false;
    }
    const Runtime::GunInput keep = rt.gun[p];
    rt.gun[p] = {};
    capture_ = true;
    started_ = false;
    playerFrame(rt, p, f);
    capture_ = false;
    out = rt.gun[p];
    rt.gun[p] = keep;
    return started_;
}

void Bot::frame(Runtime& rt, uint64_t f)
{
    if (team_) {   // teammates: only their seats (the human's input stays as it is)
        // autostart (user 2026-10-28): never in the attract / title / ranking: a waiting teammate "joins at once", and a
        // start there is a game's start (Play with players 4 + CPU seats began Chapter 1 by itself at the boot's
        // attract). Their state is looked at afresh once a human has started the game (they join then)
        if (rt.attract) {
            for (int p = 0; p < 4; ++p)
                if ((team_ >> p) & 1) {
                    if (p < rt.seat_count())
                        rt.gun[p] = {};
                    pl_[p].startPlanned = false;
                    pl_[p].lastState = -1;
                }
            return;
        }
        for (int p = 0; p < rt.seat_count(); ++p)
            if ((team_ >> p) & 1) {
                rt.gun[p] = {};
                playerFrame(rt, p, f);
            }
        return;
    }
    rt.gun[0] = rt.gun[1] = {};
    for (int p = 0; p < 2; ++p)
        if (p == 0 || players_ == 2 || rt.coop4_on())
            playerFrame(rt, p, f);
    if (rt.coop4_on()) {   // the 4P mode: players 3 / 4 play too
        rt.gun[2] = rt.gun[3] = {};
        for (int p = 2; p < 4; ++p)
            playerFrame(rt, p, f);
    }
}

void Bot::playerFrame(Runtime& rt, int p, uint64_t f)
{
    P& s = pl_[p];
    BotProfile& b = profile[p];
    const Player& pl = rt.mem.at<Player>(rt.player_rec(p));   // 4P: seats 3 / 4 in the 4P block
    const int state = pl.state;
    if (state != s.lastState) {
        if (state == kOut && team_) {   // a teammate continues soon
            s.startPlanned = true;
            s.startAt = f + uint64_t(rint(30, 150));
        } else if (state == kOut) {   // out of lives: continue (maybe too late: the countdown is ~15 s)
            s.startPlanned = !s.absent && rnd() < b.continueChance;
            s.startAt = f + uint64_t(rint(20, 1100));
        }
        if (state == kWaiting && team_) {   // a teammate joins at once
            s.startPlanned = true;
            s.startAt = f + uint64_t(rint(20, 60));
        } else if (state == kWaiting && p == 1) {   // player 2 not in: joins again some time (or in the next cut-scene)
            s.startPlanned = true;
            s.startAt = f + uint64_t(rint(60, 6000));
            s.absent = false;
        }
        if (state == kPlaying && s.lastState >= 0 && s.lastState != kPlaying) {
            if (s.lastState == kOut || s.lastState == 1)
                ++continues;
            else
                ++joins;
            s.startPlanned = false;
        }
        s.lastState = state;
    }
    // player 2's first join, and the start button in a cut-scene (letterbox) now and then
    if (p == 1 && state != kPlaying && !team_) {
        if (p2NextJoin_ && f >= p2NextJoin_) {
            s.startPlanned = true;
            s.startAt = f;
            p2NextJoin_ = 0;
        }
        if (!s.absent && rt.letterbox > 0 && rnd() < 0.004f) {
            press(rt, p);
            ++cutsceneJoins;
        }
    }
    if (s.startPlanned && f >= s.startAt) {
        press(rt, p);
        if (f > s.startAt + 30)   // pressed for half a second
            s.startPlanned = false;
    }
    if (state != kPlaying)
        return;
    if (team_) {   // a computer teammate: its own play (teamPlay)
        teamPlay(rt, p, f);
        return;
    }
    // player 2 leaves now and then (puts the gun down: dies out, no continue)
    if (p == 1 && !team_ && !s.absent && rnd() < 1.0f / 20000) {
        s.absent = true;
        ++leaves;
    }
    if (s.absent || f < s.idleUntil)
        return;
    if (!team_ && rnd() < 1.0f / 1500)   // the start button pressed in play (no effect expected)
        press(rt, p);
    if (rt.letterbox > 0 && team_)   // a teammate holds its fire in the cut-scenes
        return;
    if (rt.letterbox > 0 && rnd() < 1.0f / 90) {   // a shot in a cut-scene (a skip request)
        rt.gun[p].onScreen = rt.gun[p].trigger = true;
        rt.gun[p].x = float(rint(-200, 200));
        rt.gun[p].y = float(rint(-150, 150));
        ++shots;
        return;
    }
    if (rnd() < b.lapse) {
        s.idleUntil = f + uint64_t(rint(60, 300));
        return;
    }
    Runtime::GunInput& g = rt.gun[p];
    // reload: out of rounds (sometimes late), or early now and then
    if (pl.rounds == 0 || (pl.rounds < 3 && f >= s.nextShot && rnd() < b.earlyReload / 30)) {
        if (s.reloadAt == 0)
            s.reloadAt = f + (pl.rounds == 0 && rnd() < b.lateReload ? uint64_t(rint(30, 150)) : uint64_t(rint(4, 20)));
        if (f >= s.reloadAt) {
            g.trigger = true;   // off screen
            s.reloadAt = 0;
            s.nextShot = f + uint64_t(rint(4, 12));
            ++reloads;
        }
        return;
    }
    s.reloadAt = 0;
    // the target: kept while on screen, else a new one (the nearest, or any: hostages too)
    float x = 0, y = 0;
    bool have = false;
    if (s.target && std::find(rt.shootable.begin(), rt.shootable.end(), s.target) != rt.shootable.end())
        have = aimPoint(rt, s.target, uint32_t(shots), x, y);
    if (!have) {
        std::vector<uint32_t> seen;
        uint32_t nearest = 0;
        float bestZ = 1e30f;
        for (uint32_t obj : rt.shootable) {
            float ox, oy;
            if (!onScreen(rt, obj, ox, oy) || isDecoy(rt, obj))
                continue;
            if (isHostage(rt, obj) && !(rnd() < b.hostageShot))   // a person to save: left alone, mostly
                continue;
            seen.push_back(obj);
            if (rt.mem.f32(obj + 0x40) < bestZ) {
                bestZ = rt.mem.f32(obj + 0x40);
                nearest = obj;
            }
        }
        s.target = 0;
        if (!seen.empty()) {
            s.target = rnd() < b.nearestBias ? nearest : seen[size_t(rint(0, int(seen.size()) - 1))];
            s.acquired = f;
            const float sigma = 4.0f + (1.0f - b.skill) * 40.0f;
            s.errX = gauss(sigma);
            s.errY = gauss(sigma);
            have = aimPoint(rt, s.target, uint32_t(shots), x, y);
        }
    }
    if (have) {   // the crosshair eases toward the aim (a teammate's sight is drawn there)
        s.aimX += (std::clamp(x + s.errX, -248.0f, 248.0f) - s.aimX) * 0.35f;
        s.aimY += (std::clamp(y + s.errY, -192.0f, 192.0f) - s.aimY) * 0.35f;
    }
    if (f < s.nextShot)
        return;
    if (rnd() < b.wildShot) {   // a shot at nothing
        g.onScreen = g.trigger = true;
        g.x = float(rint(-248, 248));
        g.y = float(rint(-192, 192));
    } else if (have && f >= s.acquired + uint64_t(b.reaction)) {
        g.onScreen = g.trigger = true;
        g.x = std::clamp(x + s.errX + gauss(4.0f), -248.0f, 248.0f);
        g.y = std::clamp(y + s.errY + gauss(4.0f), -192.0f, 192.0f);
        s.errX *= 0.7f;   // the aim settles
        s.errY *= 0.7f;
    } else
        return;
    ++shots;
    s.nextShot = f + uint64_t(std::max(2, b.fireGap + rint(-3, 6)));
}

// ---------------------------------------------------------------------------------------------------------------

size_t Watchdog::countTasks(Runtime& rt, bool checkNan)
{
    const uint32_t head = rt.mem.u32(G::task_group_current);
    if (!rt.mem.mapped(head))
        return 0;
    size_t n = 0;
    uint32_t t = rt.mem.u32(head + 0x14);
    while (t != head && n < 100000) {
        if (!rt.mem.mapped(t)) {
            if (problem.empty()) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "broken task ring at 0x%X", t);
                problem = buf;
                problemFrame = rt.frameCount;
            }
            break;
        }
        // world objects only (0x3C8-byte tasks; small effect tasks such as splash_9f1 keep other data at +0x20)
        if (checkNan && rt.mem.u32(t + 0xC) == 0x80018691u &&
            std::find(rt.shootable.begin(), rt.shootable.end(), t) != rt.shootable.end())
            for (uint32_t k = 0; k < 3; ++k)
                if (!std::isfinite(rt.mem.f32(t + 0x20 + 4 * k)) && problem.empty()) {
                    char buf[128];
                    std::snprintf(buf, sizeof buf, "nan position: task 0x%X routine %s", t, rt.routineName(rt.mem.u32(t)).c_str());
                    problem = buf;
                    problemFrame = rt.frameCount;
                }
        ++n;
        t = rt.mem.u32(t + 0x14);
    }
    if (n >= 100000 && problem.empty()) {
        problem = "task ring does not close";
        problemFrame = rt.frameCount;
    }
    return n;
}

void Watchdog::check(Runtime& rt)
{
    const uint64_t f = rt.frameCount;
    // progress: anything a player could see change
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    mix(rt.mem.u32(G::evt_pc));
    mix(uint64_t(rt.mem.at<int8_t>(G::event_index)) & 0xFF);
    mix(uint64_t(uint16_t(rt.mem.at<int16_t>(G::game_mode))));
    mix(rt.mem.u32(G::game_submode));
    for (uint32_t k = 0; k < 3; ++k)
        mix(uint64_t(int64_t(std::floor(rt.mem.f32(G::camera_pos + 4 * k) * 4.0f))));
    // every seat (3P / 4P: seats 3 / 4 too; hunt2 2026-10-20: a Magician fight carried by player 3 alone looked frozen)
    for (int p = 0; p < rt.seat_count(); ++p) {
        const Player& pl = rt.mem.at<Player>(rt.player_rec(p));
        mix(uint64_t(uint32_t(pl.state)));
        mix(uint64_t(uint32_t(pl.lives)));
        mix(uint64_t(uint32_t(pl.score)));
        mix(uint64_t(uint32_t(pl.continue_timer >> 12)));
    }
    mix(uint64_t(uint16_t(rt.mem.at<int16_t>(G::enemies_alive))));
    mix(uint64_t(rt.letterbox));
    mix(rt.chapterDone ? 1 : 0);
    if (h != signature) {
        signature = h;
        lastProgress = f;
    }
    if (const int ev = rt.mem.at<int8_t>(G::event_index); ev != lastEvent) {
        lastEvent = ev;
        eventSince = f;
    }
    if (!problem.empty())
        return;
    if (f - lastProgress > kSoftLockFrames) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "softlock: nothing advanced since frame %llu (event %d, script op %s, mode %d/%u)",
                      (unsigned long long)lastProgress, lastEvent,
                      rt.routineName(rt.mem.u32(G::evt_handlers + rt.mem.u32(rt.mem.u32(G::evt_pc)) * 4)).c_str(),
                      rt.mem.at<int16_t>(G::game_mode), rt.mem.u32(G::game_submode));
        problem = buf;
        problemFrame = f;
        // what the event waits on: the shootable objects (camera-space centre, on the gun's screen or not)
        std::printf("ROBUST: softlock dump: enemies_alive=%d shootable=%zu\n", int(rt.mem.at<int16_t>(G::enemies_alive)),
                    rt.shootable.size());
        for (int p = 0; p < rt.seat_count(); ++p) {   // (4P: all four)
            const Player& pl = rt.mem.at<Player>(rt.player_rec(p));
            std::printf("ROBUST:   player %d state %d lives %d\n", p + 1, int(pl.state), int(pl.lives));
        }
        for (uint32_t obj : rt.shootable) {
            float x = 0, y = 0;
            const bool on = onScreen(rt, obj, x, y);
            std::printf("ROBUST:   0x%X %s cam=(%.1f %.1f %.1f) screen=(%.0f %.0f)%s +58=%08X +5C=%d +60=%d +66=%d +70=%d target %d\n", obj,
                        rt.routineName(rt.mem.u32(obj)).c_str(), rt.mem.f32(obj + 0x38), rt.mem.f32(obj + 0x3C),
                        rt.mem.f32(obj + 0x40), x, y, on ? "" : " OFF", rt.mem.u32(obj + 0x58), int(rt.mem.at<int16_t>(obj + 0x5C)),
                        int(rt.mem.at<int16_t>(obj + 0x60)), int(rt.mem.at<int16_t>(obj + 0x66)),
                        int(rt.mem.at<int16_t>(obj + 0x70)), int(rt.mem.at<int8_t>(obj + 0x2E8)));
        }
        return;
    }
    for (uint32_t k = 0; k < 3; ++k)
        if (!std::isfinite(rt.mem.f32(G::camera_pos + 4 * k))) {
            problem = "nan camera position";
            problemFrame = f;
            return;
        }
    if (f % 10 == 0) {
        const size_t n = countTasks(rt, true);
        maxTasks = std::max(maxTasks, n);
        if (n > 2000 && problem.empty()) {
            problem = "runaway tasks: " + std::to_string(n);
            problemFrame = f;
        }
    }
    maxShootable = std::max(maxShootable, rt.shootable.size());
    maxDraw = std::max(maxDraw, rt.drawList.size());
    for (const DrawModel& d : rt.drawList)   // what the player would see: a model drawn with a NaN matrix
        for (float v : d.m)
            if (!std::isfinite(v) && problem.empty()) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "nan draw matrix: model 0x%X", d.model);
                problem = buf;
                problemFrame = f;
            }
    if (rt.drawList.size() > 20000 && problem.empty()) {
        problem = "runaway draw list: " + std::to_string(rt.drawList.size());
        problemFrame = f;
    }
}

// ---------------------------------------------------------------------------------------------------------------

namespace {
const uint64_t* gFrame = nullptr;
#ifdef _WIN32
LONG WINAPI crashFilter(EXCEPTION_POINTERS* e)
{
    std::printf("ROBUST: result=crash code=0x%08lX address=%p frame=%llu\n", e->ExceptionRecord->ExceptionCode,
                e->ExceptionRecord->ExceptionAddress, gFrame ? (unsigned long long)*gFrame : 0ull);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
void crashSignal(int sig)
{
    std::printf("ROBUST: result=crash signal=%d frame=%llu\n", sig, gFrame ? (unsigned long long)*gFrame : 0ull);
    std::fflush(stdout);
    std::_Exit(3);
}
#endif
void abortHandler(int)
{
    std::printf("ROBUST: result=assert frame=%llu\n", gFrame ? (unsigned long long)*gFrame : 0ull);
    std::fflush(stdout);
    std::_Exit(4);
}
} // namespace

void installCrashHandler(const uint64_t* frameCounter)
{
    gFrame = frameCounter;
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(crashFilter);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#else
    for (const int s : {SIGSEGV, SIGBUS, SIGILL, SIGFPE})
        std::signal(s, crashSignal);
#endif
    std::signal(SIGABRT, abortHandler);
}

} // namespace hotd::game
