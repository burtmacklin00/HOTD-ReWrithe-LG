#include "assets/arcade_categories.h"

#include "assets/arcade_loader.h"

#include <cstring>

namespace hotd {

namespace {

constexpr Category Z = Category::Zombies, B = Category::Bosses, H = Category::Humans;

// Creature types (maincpu 0xC7240; developer names BO_ at 0xC8A60) with the official names where known (docs/names.md).
struct TypeInfo { Category cat; const char* what; };
const TypeInfo kTypes[94] = {
    {Z, "Kenfis (dog)"}, {Z, "Saruzou (monkey)"}, {H, "Thomas Rogan"}, {Z, "Ebitan A"}, {Z, "Ebitan B"},
    {Z, "MrG (cut)"}, {Z, "Murrer (molb)"}, {Z, "Bouere (frog)"}, {Z, "Kenfis, dark"}, {Z, "Saruzou, dark"},
    {Z, "syndy (cut)"}, {Z, "Sam"}, {Z, "debu (cut)"}, {H, "G"}, {H, "human (haride)"},
    {H, "human (staje)"}, {Z, "Simon"}, {H, "researcher"}, {Z, "Rubin (semu)"}, {H, "researcher"},
    {H, "researcher"}, {H, "researcher"}, {H, "researcher"}, {H, "researcher (woman)"}, {Z, "Kageo (mummy)"},
    {H, "researcher (woman)"}, {H, "researcher (woman)"}, {H, "researcher (woman)"}, {Z, "Neil"}, {Z, "Cyril"},
    {Z, "Samson"}, {Z, "Harris"}, {Z, "Gilmore"}, {Z, "Bentley"}, {H, "Sophie"},
    {B, "Chariot, armoured"}, {B, "Chariot, armour off"}, {Z, "Drake"}, {Z, "kage"}, {Z, "lubin (cut)"},
    {Z, "Kenfis, variant"}, {Z, "neopetit"}, {Z, "neopetit 2"}, {Z, "neopetit 3"}, {Z, "pkenb"},
    {Z, "Bentley b"}, {Z, "Ebitan C (pdolob)"}, {Z, "Harris b"}, {Z, "Taran (taraba)"}, {H, "researcher"},
    {B, "Hangedman"}, {Z, "Bentley c"}, {Z, "disiprin (cut)"}, {Z, "Robert (tetuman)"}, {Z, "Harris, 21 joints"},
    {Z, "Devilon (small devilon)"}, {Z, "neozom (cut)"}, {Z, "Bourbon (kageo2)"}, {Z, "Gilmore b"}, {Z, "Moody"},
    {H, "Dr. Curien"}, {Z, "Kageo b"}, {Z, "Bourbon b"}, {Z, "Gilmore c"}, {B, "Hermit"},
    {Z, "Bentley d"}, {Z, "baba (cut)"}, {H, "Sophie, creature (bad ending)"}, {Z, "Parlor (kageta)"}, {B, "Hangedman (devilonx)"},
    {Z, "moodyb"}, {Z, "Harris c"}, {Z, "Bentley e"}, {Z, "Bourbon c"}, {Z, "Gilmore d"},
    {Z, "hiru (cut leech)"}, {B, "Magician"}, {Z, "Harris d"}, {H, "Dr. Curien, second model"}, {Z, "Kenfis jp"},
    {Z, "Kenfis jp, dark"}, {Z, "Kenfis jp, variant"}, {Z, "taalu"}, {Z, "taalu b"}, {Z, "Burner"},
    {Z, "Burner b"}, {Z, "Kageo c"}, {Z, "kageta b"}, {Z, "moodyc"}, {Z, "Bentley f"},
    {Z, "Taran b (tarabab)"}, {Z, "gfrog"}, {Z, "moodyb b"}, {Z, "moodyc c"},
};

// Boss parts outside the skeletons, by name prefix
// (not the small Devilon swarm, type 55: "devilonm_*" and "devilon_*_min")
const char* const kBossPrefixes[] = {"hyum", "devilon", "elien", "boss3", "boss4"};
bool bossName(const std::string& n)
{
    if (n.rfind("devilonm", 0) == 0 || (n.size() > 4 && n.compare(n.size() - 4, 4, "_min") == 0)) return false;
    for (const char* pre : kBossPrefixes)
        if (n.rfind(pre, 0) == 0) return true;
    return false;
}

} // namespace

Category CreatureCategories::ofType(int type) { return type >= 0 && type < 94 ? kTypes[type].cat : Category::Zombies; }
const char* CreatureCategories::typeName(int type) { return type >= 0 && type < 94 ? kTypes[type].what : "?"; }

void CreatureCategories::build(const CreatureAnimations& anim)
{
    names_ = arcadeObjectNames(anim.mainData());
    byId_.clear();
    // name rules first, then the skeletons (a part used by several types keeps the first boss/human category it gets)
    for (uint32_t id = 0; id < names_.size(); ++id)
        if (bossName(names_[id])) byId_[id] = Category::Bosses;
    for (int t = 0; t < anim.typeCount() && t < 94; ++t)
        for (const SkeletonNode& n : anim.skeleton(t).nodes) {
            if (!n.model) continue;
            auto it = byId_.find(n.model);
            if (it == byId_.end() || it->second == Category::Zombies) byId_[n.model] = kTypes[t].cat;
        }
    // shadow models ("<part>_DD") follow their part
    std::map<std::string, uint32_t> byName;
    for (uint32_t id = 0; id < names_.size(); ++id) byName[names_[id]] = id;
    for (uint32_t id = 0; id < names_.size(); ++id) {
        const std::string& s = names_[id];
        if (s.size() > 3 && s.compare(s.size() - 3, 3, "_DD") == 0)
            if (auto p = byName.find(s.substr(0, s.size() - 3)); p != byName.end())
                if (auto c = byId_.find(p->second); c != byId_.end()) byId_[id] = c->second;
    }
}

std::optional<Category> CreatureCategories::of(uint32_t id) const
{
    auto it = byId_.find(id);
    if (it == byId_.end()) return std::nullopt;
    return it->second;
}

std::string CreatureCategories::name(uint32_t id) const { return id < names_.size() ? names_[id] : std::string(); }

} // namespace hotd
