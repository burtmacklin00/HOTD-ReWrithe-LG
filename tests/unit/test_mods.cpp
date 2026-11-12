// mods: the built-in mods' manifests (data\mods\<id>\mod.ini) and the game's own copies of them agree, every setting's
// default is one of its choices, every choice configures the mod (older values still read), mod.ini parsing
#include "check.h"

#include "assets/mods.h"
#include "game/cosmods.h"
#include "game/ragdoll.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace fs = std::filesystem;
using namespace hotd;

namespace {
std::string modsDir() { return (fs::path(test::sourceDir()) / "data" / "mods").string(); }
const ModPack* byType(const std::vector<ModPack>& v, const std::string& t)
{
    for (const ModPack& p : v)
        if (p.type == t) return &p;
    return nullptr;
}
} // namespace

TEST(mods, manifests_well_formed)
{
    const auto packs = findModPacks({modsDir()});
    CHECK(packs.size() >= 5);
    std::set<std::string> ids;
    for (const ModPack& p : packs) {
        CHECK_MSG(ids.insert(p.id).second, p.id);
        CHECK_MSG(!p.name.empty() && p.name != p.id, p.id + ": no name in mod.ini");
        CHECK_MSG(!p.description.empty(), p.id + ": no description");
        CHECK_MSG(p.depth >= -1 && p.depth <= 1, p.id);
        CHECK_MSG(p.targets >= 1 && p.targets <= 3, p.id);
        std::set<std::string> keys;
        for (const ModSetting& s : p.settings) {
            CHECK_MSG(keys.insert(s.key).second, p.id + ": setting twice: " + s.key);
            CHECK_MSG(!s.label.empty() && !s.help.empty(), p.id + "." + s.key);
            CHECK_MSG(s.choices.size() >= 2, p.id + "." + s.key + ": fewer than two choices");
            CHECK_MSG(std::find(s.choices.begin(), s.choices.end(), s.def) != s.choices.end(),
                      p.id + "." + s.key + ": the default " + s.def + " is not a choice");
            std::set<std::string> ch(s.choices.begin(), s.choices.end());
            CHECK_MSG(ch.size() == s.choices.size(), p.id + "." + s.key + ": a choice twice");
        }
        if (p.type != "textures") CHECK_MSG(!p.defaultOn, p.id + ": a built-in mod on by default");
    }
    for (const char* t : {"ragdoll", "rainbowblood", "bighead", "hermithead"}) CHECK_MSG(byType(packs, t), t);
}

TEST(mods, builtin_copies_match_folders)
{
    // without data\mods the game makes the same mods itself (findMods' built-ins): the same texts and settings
    const std::string empty = test::tempDir("mods_none");
    const auto builtin = findMods({empty});
    const auto folders = findMods({modsDir()});
    for (const char* t : {"ragdoll", "rainbowblood", "bighead", "hermithead"}) {
        const ModPack* b = byType(builtin, t);
        const ModPack* f = byType(folders, t);
        if (!CHECK_MSG(b && f, t)) continue;
        CHECK(b->builtin && !f->builtin);
        CHECK_EQ(b->id, f->id);
        CHECK_EQ(b->name, f->name);
        CHECK_EQ(b->description, f->description);
        CHECK_EQ(b->defaultOn, f->defaultOn);
        if (!CHECK_EQ(b->settings.size(), f->settings.size())) continue;
        for (size_t i = 0; i < b->settings.size(); ++i) {
            const ModSetting &x = b->settings[i], &y = f->settings[i];
            CHECK_EQ(x.key, y.key);
            CHECK_MSG(x.label == y.label && x.def == y.def && x.help == y.help, std::string(t) + "." + x.key);
            CHECK_MSG(x.choices == y.choices, std::string(t) + "." + x.key + ": choices differ");
        }
    }
    // an older folder (a setting missing in its mod.ini): the built-in's setting is added after its own
    const fs::path old = fs::path(test::tempDir("mods_old")) / "ragdoll";
    fs::create_directories(old);
    std::ofstream(old / "mod.ini") << "[mod]\nname = Ragdoll\ntype = ragdoll\ndefault = off\n[settings]\n"
                                      "friction = Friction | Normal | Rough, Normal, Smooth | help\n";
    const auto withOld = findMods({old.parent_path().string()});
    const ModPack* r = byType(withOld, "ragdoll");
    if (CHECK(r)) {
        CHECK_EQ(r->settings.size(), byType(builtin, "ragdoll")->settings.size());
        CHECK_EQ(r->settings[0].key, std::string("friction"));
    }
}

TEST(mods, every_choice_configures)
{
    const auto packs = findMods({modsDir()});
    // Rainbow Blood
    const ModPack* rb = byType(packs, "rainbowblood");
    if (CHECK(rb)) {
        for (const ModSetting& s : rb->settings) {
            std::set<std::string> seen;
            for (const std::string& c : s.choices) {
                game::rainbowBloodConfigure(true, {{s.key, c}});
                game::magicianAfterimageConfigure(true, {{s.key, c}});
                const auto& r = game::rainbowBloodSettings();
                const auto& m = game::magicianAfterimageSettings();
                char b[160];
                std::snprintf(b, sizeof b, "%d %.3f %d %d %d %d %d", r.together, r.speed, r.creature, r.hud, r.menu, m.phase1, m.phase2);
                seen.insert(b);
                CHECK(r.on);
            }
            // each choice its own effect (afterimage colours: each its own colour)
            CHECK_MSG(seen.size() == s.choices.size(), "rainbow-blood." + s.key + ": two choices do the same");
        }
        // the old (v1.3.0) spelling still read
        game::rainbowBloodConfigure(true, {{"creature", "Own Colour Each"}});
        CHECK_EQ(game::rainbowBloodSettings().creature, 0);
        game::rainbowBloodConfigure(true, {{"creature", "Own Color Each"}});
        CHECK_EQ(game::rainbowBloodSettings().creature, 0);
        // the defaults (no values) are the manifest's defaults
        std::map<std::string, std::string> defs;
        for (const ModSetting& s : rb->settings) defs[s.key] = s.def;
        game::rainbowBloodConfigure(true, {});
        const auto a = game::rainbowBloodSettings();
        game::rainbowBloodConfigure(true, defs);
        const auto d = game::rainbowBloodSettings();
        CHECK(a.together == d.together && a.speed == d.speed && a.creature == d.creature && a.hud == d.hud && a.menu == d.menu);
        game::rainbowBloodConfigure(false, {});
        game::magicianAfterimageConfigure(false, {});
        CHECK(!game::rainbowBloodSettings().on);
    }
    // Big Head
    const ModPack* bh = byType(packs, "bighead");
    if (CHECK(bh)) {
        game::bigHeadConfigure(true, {{"size", "1.5x"}});
        CHECK_NEAR(game::bigHeadSettings().scale, 1.5, 1e-6);
        game::bigHeadConfigure(true, {{"size", "3x"}});
        CHECK_NEAR(game::bigHeadSettings().scale, 3.0, 1e-6);
        game::bigHeadConfigure(true, {{"size", "nonsense"}});
        CHECK_NEAR(game::bigHeadSettings().scale, 2.0, 1e-6);
        game::bigHeadConfigure(true, {{"who", "Enemies"}, {"bosses", "Off"}});
        CHECK(!game::bigHeadSettings().everyone && !game::bigHeadSettings().bosses);
        game::bigHeadConfigure(true, {});
        CHECK(game::bigHeadSettings().everyone && game::bigHeadSettings().bosses);
        game::bigHeadConfigure(false, {});
    }
    // Ragdoll: each Bodies Stay choice its own time, Arcade 0
    const ModPack* rd = byType(packs, "ragdoll");
    if (CHECK(rd)) {
        for (const ModSetting& s : rd->settings) {
            std::set<std::string> seen;
            for (const std::string& c : s.choices) {
                game::ragdollConfigure(true, {{s.key, c}});
                const auto& r = game::ragdollSettings();
                char b[96];
                std::snprintf(b, sizeof b, "%.3f %d %.3f %d", r.friction, r.stayFrames, r.gravity, r.dismember);
                seen.insert(b);
            }
            CHECK_MSG(seen.size() == s.choices.size(), "ragdoll." + s.key + ": two choices do the same");
        }
        game::ragdollConfigure(true, {{"stay", "Arcade"}});
        CHECK_EQ(game::ragdollSettings().stayFrames, 0);
        game::ragdollConfigure(true, {{"stay", "15 s"}});
        CHECK_EQ(game::ragdollSettings().stayFrames, 15 * 60);
        game::ragdollConfigure(false, {});
    }
}

TEST(mods, parsing)
{
    ModSetting s;
    CHECK(parseModSetting("size", "Head Size | 2x | 1.5x, 2x, 3x | How big", s));
    CHECK_EQ(s.key, std::string("size"));
    CHECK_EQ(s.label, std::string("Head Size"));
    CHECK_EQ(s.def, std::string("2x"));
    CHECK_EQ(s.choices.size(), size_t(3));
    CHECK_EQ(s.choices[0], std::string("1.5x"));
    CHECK_EQ(s.help, std::string("How big"));
    ModSetting t;
    CHECK(!parseModSetting("x", "just a label", t));
    CHECK_EQ(parseModDepth("auto"), -1);
    CHECK_EQ(parseModDepth(" Buffer "), 0);
    CHECK_EQ(parseModDepth("SORT"), 1);
    CHECK_EQ(parseModDepth("zbuffer"), -2);
    // a texture pack folder: mod.ini keys, comments after values, [recolour] rules, models = pc / arcade
    const fs::path root = test::tempDir("mods_parse");
    fs::create_directories(root / "MyPack" / "textures");
    std::ofstream(root / "MyPack" / "mod.ini") << "; comment\n[mod]\nname = My Pack ; trailing comment\nauthor = me\n"
                                                  "models = pc\ndepth = sort\ndefault = off\n[recolour]\nblood = #30C040\n"
                                                  "*-c1023 = tint #30C040\nzonbi_* = hue 90\n";
    std::ofstream(root / "MyPack" / "textures" / "world_p0517.png") << "x";
    fs::create_directories(root / "lang");   // (data/mods/lang: the languages' packs, never a mod)
    const auto v = findModPacks({root.string()});
    if (CHECK_EQ(v.size(), size_t(1))) {
        const ModPack& p = v[0];
        CHECK_EQ(p.id, std::string("MyPack"));
        CHECK_EQ(p.name, std::string("My Pack"));
        CHECK_EQ(p.author, std::string("me"));
        CHECK_EQ(p.targets, 1);
        CHECK_EQ(p.depth, 1);
        CHECK(!p.defaultOn);
        CHECK_EQ(p.bloodRgb, 0x30C040);
        CHECK_EQ(p.rules.size(), size_t(2));
        CHECK_EQ(p.pngs.size(), size_t(1));
        CHECK(p.pngs.count("p0517"));
        CHECK_EQ(modDepthFor(p, true), 1);
    }
    // depth: a mod's depth = switches what it draws (the engine's own check on a synthetic stage)
    CHECK_EQ(modDepthTest(), 0);
}
