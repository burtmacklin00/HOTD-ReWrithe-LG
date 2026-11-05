// The cosmetic built-in mods (game/cosmods.h, docs/modding.md): Rainbow Blood's settings (the colour itself is the
// renderer's: render/renderer.cpp uRainbow) and Big Head. Draw only: nothing here writes the address space.
#include "game/cosmods.h"
#include "game/runtime.h"

#include "assets/arcade_categories.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <vector>

namespace hotd::game {

namespace {

constexpr uint32_t kPartRoles = 0x1184E0;   // 0xF8 per type, 8 per role (byte 3: the part's class; creature.cpp)

std::string valueOf(const std::map<std::string, std::string>& values, const char* k, const char* def)
{
    auto it = values.find(k);
    return it != values.end() ? it->second : std::string(def);
}

} // namespace

RainbowBloodSettings& rainbowBloodSettings()
{
    static RainbowBloodSettings s;
    return s;
}

void rainbowBloodConfigure(bool on, const std::map<std::string, std::string>& values)
{
    RainbowBloodSettings s;
    s.on = on;
    s.together = valueOf(values, "colours", "Each Splash") == "Together";
    const std::string sp = valueOf(values, "speed", "Normal");
    s.speed = sp == "Still" ? 0.0f : sp == "Slow" ? 0.1f : sp == "Fast" ? 60.0f / 135.0f : 0.2f;
    const std::string cr = valueOf(values, "creature", "Rainbow Cycle");
    s.creature = cr == "Own Colour Each" ? 0 : cr == "Off" ? 2 : 1;
    const std::string hud = valueOf(values, "hud", "Off");
    s.hud = hud == "Crosshair" ? 1 : hud == "HUD" ? 2 : hud == "Both" ? 3 : 0;
    rainbowBloodSettings() = s;
}

// rainbow2: the renderer's colour on the CPU (the 2D blood, the HUD, the sights): phase from the game's frame, the
// table 0xAB120 (palette_animation's Chapter 4 screens: RGB555, R in the low bits) blended between its steps
bool rainbowColour(const Runtime& rt, float offset, float rgb[3], bool always)
{
    const RainbowBloodSettings& s = rainbowBloodSettings();
    if (!s.on)
        return false;
    double h = std::fmod(double(rt.frameCount) * s.speed / 60.0, 1.0) + (s.together && !always ? 0.0 : double(offset));
    h -= std::floor(h);
    bool table = rt.mem.mapped(0xAB120);
    float c[90][3];
    for (int i = 0; i < 90 && table; ++i) {
        const uint16_t v = rt.mem.at<uint16_t>(0xAB120 + 2u * uint32_t(i));
        const int ch[3] = {v & 31, v >> 5 & 31, v >> 10 & 31};
        table = ch[0] == 31 || ch[1] == 31 || ch[2] == 31;
        for (int k = 0; k < 3; ++k) c[i][k] = float(ch[k]) / 31.0f;
    }
    if (table) {
        const double x = h * 90.0;
        const int i = int(x) % 90, j = (i + 1) % 90;
        const float f = float(x - std::floor(x));
        for (int k = 0; k < 3; ++k) rgb[k] = c[i][k] + (c[j][k] - c[i][k]) * f;
    } else {
        for (int k = 0; k < 3; ++k) {   // (the shader's plain hue)
            double t = h + (k == 0 ? 0.0 : k == 1 ? 2.0 / 3.0 : 1.0 / 3.0);
            t = std::fabs((t - std::floor(t)) * 6.0 - 3.0) - 1.0;
            rgb[k] = float(std::clamp(t, 0.0, 1.0));
        }
    }
    return true;
}

int& customWoundBlood()
{
    static int rgb = -1;
    return rgb;
}

namespace {

struct CreatureHue { int type; uint64_t seen; float hue; };
std::map<uint32_t, CreatureHue>& creatureHues()
{
    static std::map<uint32_t, CreatureHue> m;
    return m;
}

} // namespace

// The user's idea (2026-11-03): each creature its own blood colour, chosen when it appears and kept for its life. The
// game's own spawn is not read (draws only): a body drawn for the first time, after 120 frames unseen or with another
// type is a new creature. Its hue: a hash of the body address and that frame, 0.08..0.92 (never the plain red)
void creatureBloodFrame(Runtime& rt)
{
    auto& m = creatureHues();
    const RainbowBloodSettings& s = rainbowBloodSettings();
    const bool custom = !s.on && customWoundBlood() >= 0;   // the custom Blood Color: the enemies' wounds too
    if (!custom && !s.on) {   // (rainbow2: kept with Creature Blood Off too: the Chariot, creatureBloodKeepsRows)
        m.clear();
        return;
    }
    const uint64_t now = rt.frameCount;
    uint32_t last = 0;
    for (const DrawModel& d : rt.drawList) {
        if (d.body == 0 || d.body == last)
            continue;
        last = d.body;
        if (!rt.mem.mapped(d.body) || !rt.mem.mapped(d.body + 0xCC))
            continue;
        const int type = rt.mem.at<int16_t>(d.body + 0xCC);
        auto it = m.find(d.body);
        if (it != m.end() && it->second.type == type && now - it->second.seen <= 120) {
            it->second.seen = now;
            continue;
        }
        static const int kBoss[] = {35, 36, 50, 55, 64, 69, 76};
        const bool enemy = type >= 0 && type < 94 && CreatureCategories::ofType(type) == Category::Zombies &&
                           std::find(std::begin(kBoss), std::end(kBoss), type) == std::end(kBoss);
        uint32_t h = d.body * 2654435761u ^ uint32_t(now) * 2246822519u;
        h ^= h >> 15, h *= 2246822519u, h ^= h >> 13;
        m[d.body] = {type, now, enemy ? 0.08f + 0.84f * float(h & 0xFFFF) / 65536.0f : -1.0f};
        if (std::getenv("HOTD_CREATURE_BLOOD_TRACE") && rt.log)   // (testing: each new creature and its hue)
            rt.log("creature blood: body " + std::to_string(d.body) + " type " + std::to_string(type) + " hue " + std::to_string(m[d.body].hue));
    }
    for (auto it = m.begin(); it != m.end();)
        it = now - it->second.seen > 600 ? m.erase(it) : std::next(it);
}

bool creatureBloodKeepsRows(uint32_t body)
{
    // the user (2026-11-03) wants the Chariot to cycle like the game's own blood colours recolour it: nothing kept now
    // (was the Chariot, types 35 / 36)
    (void)body;
    return false;
}

float creatureBloodHue(uint32_t body)
{
    const RainbowBloodSettings& s = rainbowBloodSettings();
    const bool custom = !s.on && customWoundBlood() >= 0;
    if (!s.on && !custom || body == 0)
        return -1.0f;
    // smooth (user 2026-11-03, a clip: a corpse flashed another colour for a few frames): a body's parts cycle together
    // with one offset of their own, never by where each part is (a twitch moved the hue)
    const float offset = float((body * 2654435761u) >> 16 & 0xFFFF) / 65536.0f;
    const auto it = creatureHues().find(body);
    const bool enemy = it != creatureHues().end() && it->second.hue >= 0.0f;
    if (custom)
        return enemy ? it->second.hue : -1.0f;
    // Creature Blood Off: cycling, the wounds as painted. The Chariot too (types 35 / 36, user 2026-11-03: its red body
    // stays): its whole body is drawn in the blood rows (the game's own green blood turns all of it green)
    if (s.creature == 2 || creatureBloodKeepsRows(body))
        return 6.0f + offset;
    if (!enemy)
        return 4.0f + offset;   // the people, the bosses: cycling, their blood rows too (rainbow2), not the red-pixel test
    return s.creature == 1 ? 2.0f + it->second.hue : it->second.hue;
}

BigHeadSettings& bigHeadSettings()
{
    static BigHeadSettings s;
    return s;
}

void bigHeadConfigure(bool on, const std::map<std::string, std::string>& values)
{
    BigHeadSettings s;
    s.on = on;
    const std::string size = valueOf(values, "size", "2x");
    s.scale = std::clamp(float(std::atof(size.c_str())), 1.0f, 4.0f);
    if (s.scale <= 1.0f) s.scale = 2.0f;
    s.everyone = valueOf(values, "who", "Everyone") != "Enemies";   // (default Everyone: user 2026-11-03)
    s.bosses = valueOf(values, "bosses", "On") != "Off";
    bigHeadSettings() = s;
}

bool bigHeadTypeOn(int type)
{
    const BigHeadSettings& s = bigHeadSettings();
    if (!s.on || s.scale == 1.0f || type < 0 || type >= 94)
        return false;
    // the bosses (also the Hangedman's fly-by 55 and the Chariot without armour 36: they keep their own list)
    static const int kBoss[] = {35, 36, 50, 55, 64, 69, 76};
    const bool boss = std::find(std::begin(kBoss), std::end(kBoss), type) != std::end(kBoss) ||
                      CreatureCategories::ofType(type) == Category::Bosses;
    if (boss) return s.bosses;
    if (CreatureCategories::ofType(type) == Category::Humans) return s.everyone;
    return true;
}

// Big Head. The game draws a creature part by part (body_draw -> body_part_draw_hook 0x9CE60, DrawModel::part), each
// part's models with the part's matrix: the joint at the model's origin. The head is the part of class 1 (the part
// role table 0x1184E0, byte 3: the class whose look angles body_part_draw_hook turns, +0x120..+0x128, and whose matrix
// it keeps at +0x154 for the eye beams and the effects). Every model drawn for the head or a part under it (BodyPart
// +0x20: the parent) gets its axes times the scale and its place moved away from the head's pivot by the same scale,
// so the head and whatever hangs off it (hair, jaw, a held hat) grow together. Shadows (DrawModel::shadow) use their
// own pivot (the head's shadow draw). The hit test is the game's (its own part spheres, never the draw list), so a big
// head is hit exactly where the normal head would be.
void bigHeadFrame(Runtime& rt)
{
    const BigHeadSettings& s = bigHeadSettings();
    if (!s.on || s.scale == 1.0f || rt.drawList.empty())
        return;
    auto& mem = rt.mem;
    auto valid = [&](uint32_t a) { return a != 0 && mem.mapped(a) && mem.mapped(a + 0x100); };
    // the bodies drawn: whether their type takes a big head (Settings: Heads / Bosses)
    struct BodyInfo { uint32_t body; int type; bool ok; };
    std::vector<BodyInfo> bodies;
    auto bodyOk = [&](uint32_t body, int& type) {
        for (const BodyInfo& b : bodies)
            if (b.body == body) return type = b.type, b.ok;
        type = valid(body) ? mem.at<int16_t>(body + 0xCC) : -1;
        const bool ok = bigHeadTypeOn(type);
        bodies.push_back({body, type, ok});
        return ok;
    };
    auto classOf = [&](int type, uint32_t part) -> int {
        const uint32_t joint = mem.u32(part + 0x38);
        if (joint >= 31) return -1;
        return mem.at<int8_t>(kPartRoles + uint32_t(type) * 0xF8 + joint * 8 + 3);
    };
    // the head a part belongs to: the topmost class-1 part on its way up the part tree (itself included); 0 none
    struct PartHead { uint32_t part, head; };
    std::vector<PartHead> heads;
    auto headOf = [&](int type, uint32_t part) -> uint32_t {
        for (const PartHead& h : heads)
            if (h.part == part) return h.head;
        uint32_t head = 0, p = part;
        for (int depth = 0; depth < 32 && valid(p); ++depth) {
            if (classOf(type, p) == 1) head = p;
            const uint32_t up = mem.u32(p + 0x20);
            if (up == p) break;
            p = up;
        }
        heads.push_back({part, head});
        return head;
    };
    // pass 1: each head's pivot (the place of its own first draw: the head part's matrix), per body and shadow pass
    struct Pivot { uint32_t body, head; bool shadow; float t[3]; };
    std::vector<Pivot> pivots;
    std::vector<int> headIndex(rt.drawList.size(), -1);   // the pivot each draw scales about (-1: none)
    for (size_t i = 0; i < rt.drawList.size(); ++i) {
        const DrawModel& d = rt.drawList[i];
        const uint32_t dp = d.part ? d.part : d.bossPart;   // (relfix: the Chariot, the Hermit: their own part hooks)
        if (d.body == 0 || dp == 0 || d.background || !valid(dp))
            continue;
        int type = -1;
        if (!bodyOk(d.body, type))
            continue;
        const uint32_t head = headOf(type, dp);
        if (head == 0)
            continue;
        int k = -1;
        for (size_t j = 0; j < pivots.size(); ++j)
            if (pivots[j].body == d.body && pivots[j].head == head && pivots[j].shadow == d.shadow) k = int(j);
        if (k < 0 && dp == head) {
            pivots.push_back({d.body, head, d.shadow, {d.m[9], d.m[10], d.m[11]}});
            k = int(pivots.size() - 1);
        }
        headIndex[i] = k;
    }
    // pass 2: scaled about the pivot (a part under the head drawn before the head's own model: none; parents come first)
    const float sc = s.scale;
    for (size_t i = 0; i < rt.drawList.size(); ++i) {
        if (headIndex[i] < 0)
            continue;
        DrawModel& d = rt.drawList[i];
        const float* P = pivots[size_t(headIndex[i])].t;
        for (int k = 0; k < 9; ++k)
            d.m[k] *= sc;
        for (int k = 0; k < 3; ++k)
            d.m[9 + k] = P[k] + (d.m[9 + k] - P[k]) * sc;
    }
}

} // namespace hotd::game
