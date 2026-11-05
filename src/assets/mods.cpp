#include "assets/mods.h"

#include "util/png.h"
#include "util/png_read.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace hotd {

namespace {

namespace fs = std::filesystem;

std::string lower(std::string s)
{
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

std::string trim(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

// a texture ID token: p<digits> (PC page), 16 hex digits (arcade), or 16 hex digits + "-c<palette entry 0-1023>"
// (arcade, the polygons in that colour only)
bool validId(const std::string& t)
{
    auto digits = [](const std::string& s) {
        return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(uint8_t(c)); });
    };
    if (t.size() >= 2 && t[0] == 'p' && digits(t.substr(1)))
        return true;
    if (t.size() < 16 || !std::all_of(t.begin(), t.begin() + 16, [](char c) { return std::isxdigit(uint8_t(c)); }))
        return false;
    return t.size() == 16 || (t.size() > 18 && t.compare(16, 2, "-c") == 0 && digits(t.substr(18)) && std::atoi(t.c_str() + 18) < 1024);
}
// the ID with a palette entry, as written: <16 hex>-c<entry> (no leading zeros)
std::string paletteId(const std::string& id, uint16_t pal) { return id + "-c" + std::to_string(pal); }
// a pack's "-c0042" also matches entry 42
std::string normaliseId(const std::string& t)
{
    const size_t c = t.find("-c");
    return c == std::string::npos ? t : t.substr(0, c + 2) + std::to_string(std::atoi(t.c_str() + c + 2));
}

bool glob(const char* p, const char* s)
{
    for (; *p; ++p, ++s) {
        if (*p == '*') {
            for (const char* t = s;; ++t) {
                if (glob(p + 1, t)) return true;
                if (!*t) return false;
            }
        }
        if (!*s || (*p != '?' && std::tolower(uint8_t(*p)) != std::tolower(uint8_t(*s)))) return false;
    }
    return !*s;
}

std::string sanitise(const std::string& name)
{
    std::string n = name;
    if (n.rfind("PN_", 0) == 0 || n.rfind("pn_", 0) == 0) n = n.substr(3);
    std::string o;
    for (char c : n) {
        if (o.size() >= 24) break;
        o += std::isalnum(uint8_t(c)) || c == '_' || c == '-' ? char(std::tolower(uint8_t(c))) : '-';
    }
    while (!o.empty() && (o.back() == '_' || o.back() == '-')) o.pop_back();
    return o.empty() ? "tex" : o;
}

uint32_t arcadeKeyOf(const Vertex& v)
{
    auto code = [](int s) {
        int c = 0;
        while ((32 << c) < s && c < 7) ++c;
        return c;
    };
    return uint32_t((v.layer & 1) << 17 | ((v.rect[0] >> 5) & 63) << 11 | ((v.rect[1] >> 5) & 31) << 6 | code(v.rect[2]) << 3 |
                    code(v.rect[3]));
}

std::string arcadeHash(const TextureSet& ts, int sheet, int x, int y, int w, int h)
{
    uint64_t f = 1469598103934665603ull;
    auto add = [&](uint8_t b) { f = (f ^ b) * 1099511628211ull; };
    add(uint8_t(w)), add(uint8_t(w >> 8)), add(uint8_t(h)), add(uint8_t(h >> 8));
    const std::vector<uint8_t>& s = ts.layers[size_t(sheet)];
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const int xx = x + i, yy = y + j;
            add(xx < ts.width && yy < ts.height ? s[size_t(yy) * size_t(ts.width) + size_t(xx)] : 0);
        }
    char b[20];
    std::snprintf(b, sizeof b, "%016llx", (unsigned long long)f);
    return b;
}


// custom models: a Wavefront OBJ (v / vt / f, polygons fanned to triangles, negative indices; usemtl + mtllib: newmtl,
// Kd, map_Kd). Positions are the original model's own space (hotd --export-models writes it)
struct ObjMaterial {
    std::string name;      // newmtl (the export's: tex_<texture ID>-c<palette entry> / page_p<page>: the original texture)
    std::string picture;   // map_Kd, absolute
    float kd[3] = {1, 1, 1};
};
struct ObjModel {
    struct Tri { float p[3][3]; float t[3][2]; int mat; };
    std::vector<Tri> tris;
    std::vector<ObjMaterial> mats;
};

// relprep (compliance): a file a mod's OBJ / MTL names stays inside the mod's folder: a relative path without "..",
// no drive or root (a mod never reads other files of the PC); "" = refused
fs::path insideDir(const fs::path& dir, const std::string& name)
{
    const fs::path n(name);
    if (name.empty() || n.has_root_name() || n.has_root_directory())
        return fs::path();
    for (const fs::path& part : n)
        if (part == "..")
            return fs::path();
    return dir / n;
}

bool readObj(const std::string& path, ObjModel& out, std::string& err)
{
    std::ifstream f(path);
    if (!f) { err = path + ": cannot open"; return false; }
    const fs::path dir = fs::path(path).parent_path();
    std::vector<std::array<float, 3>> v;
    std::vector<std::array<float, 2>> vt;
    std::map<std::string, int> matIndex;
    int cur = -1;
    auto loadMtl = [&](const std::string& name) {
        const fs::path mp = insideDir(dir, name);
        if (mp.empty()) return;   // (outside the mod's folder: ignored)
        std::ifstream m(mp);
        std::string line;
        int at = -1;
        while (std::getline(m, line)) {
            std::istringstream s(line);
            std::string k;
            s >> k;
            if (k == "newmtl") {
                std::string n;
                std::getline(s >> std::ws, n);
                n = trim(n);
                auto [it, fresh] = matIndex.emplace(n, int(out.mats.size()));
                if (fresh) { out.mats.push_back({}); out.mats.back().name = n; }
                at = it->second;
            } else if (k == "Kd" && at >= 0) {
                s >> out.mats[size_t(at)].kd[0] >> out.mats[size_t(at)].kd[1] >> out.mats[size_t(at)].kd[2];
            } else if (k == "map_Kd" && at >= 0) {
                std::string rest;
                std::getline(s >> std::ws, rest);
                rest = trim(rest);
                const size_t sp = rest.find_last_of(' ');   // options (-s ...) before the file name: the last word
                out.mats[size_t(at)].picture = insideDir(dir, sp == std::string::npos ? rest : rest.substr(sp + 1)).string();
            }
        }
    };
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream s(line);
        std::string k;
        s >> k;
        if (k == "v") {
            std::array<float, 3> p{};
            s >> p[0] >> p[1] >> p[2];
            v.push_back(p);
        } else if (k == "vt") {
            std::array<float, 2> t{};
            s >> t[0] >> t[1];
            vt.push_back(t);
        } else if (k == "mtllib") {
            std::string n;
            std::getline(s >> std::ws, n);
            loadMtl(trim(n));
        } else if (k == "usemtl") {
            std::string n;
            std::getline(s >> std::ws, n);
            n = trim(n);
            auto [it, fresh] = matIndex.emplace(n, int(out.mats.size()));
            if (fresh) { out.mats.push_back({}); out.mats.back().name = n; }
            cur = it->second;
        } else if (k == "f") {
            std::vector<std::pair<int, int>> c;   // (v, vt) 0-based, vt -1 = none
            std::string w;
            while (s >> w) {
                int a = 0, b = 0;
                const size_t sl = w.find('/');
                a = std::atoi(w.c_str());
                if (sl != std::string::npos && sl + 1 < w.size() && w[sl + 1] != '/') b = std::atoi(w.c_str() + sl + 1);
                a = a < 0 ? int(v.size()) + a : a - 1;
                b = b < 0 ? int(vt.size()) + b : b - 1;
                if (a < 0 || a >= int(v.size())) { err = path + ": bad face index"; return false; }
                c.push_back({a, b >= 0 && b < int(vt.size()) ? b : -1});
            }
            for (size_t i = 1; i + 1 < c.size(); ++i) {
                ObjModel::Tri t{};
                const std::pair<int, int> q[3] = {c[0], c[i], c[i + 1]};
                for (int k2 = 0; k2 < 3; ++k2) {
                    for (int a = 0; a < 3; ++a) t.p[k2][a] = v[size_t(q[k2].first)][size_t(a)];
                    if (q[k2].second >= 0) { t.t[k2][0] = vt[size_t(q[k2].second)][0]; t.t[k2][1] = vt[size_t(q[k2].second)][1]; }
                }
                t.mat = cur;
                out.tris.push_back(t);
            }
        }
    }
    if (out.tris.empty()) { err = path + ": no faces"; return false; }
    return true;
}

} // namespace

uint32_t arcadeTextureKey(const Vertex& v) { return arcadeKeyOf(v); }

std::vector<TexEntry> listStageTextures(const StageData& stage, bool allSets)
{
    std::vector<TexEntry> out;
    if (stage.textureSets.empty()) return out;
    if (stage.source == Source::PC) {
        const TextureSet& ts = stage.textureSets[0];
        if (ts.layers.empty() || ts.pageIds.size() != ts.layers.size()) return out;
        std::vector<std::set<std::string>> users(ts.layers.size());
        for (const auto& [key, m] : stage.models)
            for (const Mesh& me : m.meshes)
                for (size_t i = 0; i + 2 < me.vertices.size(); i += 3) {
                    const Vertex& v = me.vertices[i];
                    if ((v.flags & kTextured) && v.layer < users.size()) {
                        char b[16];
                        std::snprintf(b, sizeof b, "%x", unsigned(key));
                        users[v.layer].insert(b);
                    }
                }
        for (size_t l = 0; l < ts.layers.size(); ++l) {
            TexEntry e;
            e.key = uint32_t(l);
            char b[16];
            std::snprintf(b, sizeof b, "p%04u", unsigned(ts.pageIds[l]));
            e.id = b;
            e.label = "page";   // (world and creature pages alike: 512 and up)
            e.width = ts.width;
            e.height = ts.height;
            e.users = users[l];
            out.push_back(std::move(e));
        }
        return out;
    }
    // arcade: per rectangle key, its polygons' flags, usage counts of (palette, luma curve), users and the sets it is
    // drawn in (bit mask; the export: each model's own sets), then one entry per (set, key)
    struct Acc { TexEntry e; std::map<uint32_t, int> uses; uint64_t sets = 0; };
    std::map<uint32_t, Acc> acc;
    const int nSets = std::min(int(stage.textureSets.size()), 64);
    for (const auto& [key, m] : stage.models) {
        uint64_t mask = 0;
        if (allSets) mask = nSets >= 64 ? ~0ull : (1ull << nSets) - 1;
        else if (!m.validVariants.empty()) { for (int s : m.validVariants) if (s >= 0 && s < nSets) mask |= 1ull << s; }
        else mask = 1ull << std::clamp(m.preferredVariant, 0, nSets - 1);
        for (const Mesh& me : m.meshes) {
            const uint64_t mm = me.textureSet >= 0 && me.textureSet < nSets ? 1ull << me.textureSet : mask;
            Acc* a = nullptr;
            uint32_t last = ~0u;
            for (size_t i = 0; i + 2 < me.vertices.size(); i += 3) {
                const Vertex& v = me.vertices[i];
                if (!(v.flags & kTextured)) continue;
                const uint32_t k = arcadeKeyOf(v);
                if (k != last) {
                    last = k;
                    a = &acc[k];
                    if (a->e.width == 0) {
                        a->e.key = k;
                        a->e.sheet = v.layer & 1;
                        a->e.x = v.rect[0];
                        a->e.y = v.rect[1];
                        a->e.width = v.rect[2];
                        a->e.height = v.rect[3];
                    }
                    a->sets |= mm;
                    if (a->e.users.size() < 12) a->e.users.insert(m.name.empty() ? std::to_string(key) : m.name);
                }
                if (v.flags & kTranslucent) a->e.translucent = true;
                if (v.palette == 1023) a->e.blood = true;
                ++a->uses[uint32_t(v.palette) << 8 | v.color[3]];
            }
        }
    }
    for (auto& [k, a] : acc) {
        if (a.e.width > 2048 || a.e.height > 1024) continue;
        uint32_t best = 0;
        int n = -1;
        for (const auto& [pl, c] : a.uses) {
            if (c > n) n = c, best = pl;
            a.e.palettes.emplace(uint16_t(pl >> 8), uint8_t(pl & 0x7F));   // (the luma curve most used with it: first seen)
        }
        a.e.palette = uint16_t(best >> 8);
        a.e.lumaBase = uint8_t(best & 0x7F);
        a.e.label = sanitise(a.e.users.empty() ? std::string("tex") : *a.e.users.begin());
        for (int s = 0; s < nSets; ++s) {
            const TextureSet& ts = stage.textureSets[size_t(s)];
            if (!(a.sets >> s & 1) || ts.layers.size() < 2) continue;
            TexEntry e = a.e;
            e.set = s;
            e.id = arcadeHash(ts, e.sheet, e.x, e.y, e.width, e.height);
            out.push_back(std::move(e));
        }
    }
    return out;
}

bool textureRgba(const StageData& stage, const TexEntry& e, ReplImage& out)
{
    if (e.set < 0 || e.set >= int(stage.textureSets.size())) return false;
    const TextureSet& ts = stage.textureSets[size_t(e.set)];
    out.width = e.width;
    out.height = e.height;
    out.rgba.assign(size_t(e.width) * size_t(e.height) * 4, 0);
    if (ts.shading == Shading::DirectRgba) {
        if (e.key >= ts.layers.size() || ts.layers[e.key].size() < out.rgba.size()) return false;
        std::copy_n(ts.layers[e.key].begin(), out.rgba.size(), out.rgba.begin());
        return true;
    }
    if (ts.layers.size() < 2 || ts.luma.size() < 128 * 128 || ts.xlat.size() < 3 * 32 * 256) return false;
    // entry 1023 is the blood colour the view sets every frame (game/view.cpp: 0x801F red for the default setting), not
    // the loaded effect record's value
    const uint16_t pal = e.palette == 1023 ? 0x801F : e.palette < ts.basePalette.size() ? ts.basePalette[e.palette] : 0x7FFF;
    const int base[3] = {pal & 31, (pal >> 5) & 31, (pal >> 10) & 31};
    const std::vector<uint8_t>& sh = ts.layers[size_t(e.sheet)];
    for (int j = 0; j < e.height; ++j)
        for (int i = 0; i < e.width; ++i) {
            const int x = e.x + i, y = e.y + j;
            const int s = x < ts.width && y < ts.height ? sh[size_t(y) * size_t(ts.width) + size_t(x)] : 0;
            // the shader's pipeline at polygon luma 255: lum * 255 / 256, capped at 63, through the colour table
            const int lum = ts.luma[size_t(e.lumaBase) * 128 + size_t(s) * 8];
            const int b = std::min(lum * 255 / 256, 63);
            uint8_t* p = &out.rgba[(size_t(j) * size_t(e.width) + size_t(i)) * 4];
            for (int c = 0; c < 3; ++c) p[c] = ts.xlat[(size_t(c) * 32 + size_t(base[c])) * 256 + size_t(b)];
            p[3] = e.translucent && s == 15 ? 0 : 255;
        }
    return true;
}

bool parseModSetting(const std::string& key, const std::string& value, ModSetting& out)
{
    std::vector<std::string> f;
    std::string cur;
    for (char c : value + "|")
        if (c == '|') { f.push_back(trim(cur)); cur.clear(); }
        else cur += c;
    if (key.empty() || f.size() < 3 || f[0].empty())
        return false;
    out = ModSetting{};
    out.key = key;
    out.label = f[0];
    out.def = f[1];
    std::string item;
    for (char c : f[2] + ",")
        if (c == ',') { if (!trim(item).empty()) out.choices.push_back(trim(item)); item.clear(); }
        else item += c;
    if (f.size() > 3) out.help = f[3];
    if (out.choices.empty())
        return false;
    if (std::find(out.choices.begin(), out.choices.end(), out.def) == out.choices.end())
        out.def = out.choices.front();
    return true;
}

// the built-in Ragdoll mod's manifest (the same as data\mods\ragdoll\mod.ini, used when that folder is missing)
static ModPack builtinRagdoll()
{
    ModPack p;
    p.id = "ragdoll";
    p.name = "Ragdoll";
    p.author = "ReWrithe";
    p.version = "2";   // (ragrevert, user 2026-10-28: back to v2; only the Dismemberment setting kept from v3)
    p.description = "Dead creatures fall as ragdolls; your shots push the bodies along the floor (visual only)";
    p.type = "ragdoll";
    p.defaultOn = false;
    p.builtin = true;
    p.targets = 3;
    static const char* const kSettings[][2] = {
        // (leftovers, user 2026-10-26: no Shot Push, no hockey names; Floor is now Friction: Rough / Normal / Smooth)
        {"friction", "Friction | Normal | Rough, Normal, Smooth | How quickly a pushed body stops sliding (Rough: soon, Smooth: further)"},
        {"stay", "Bodies Stay | 8 s | Arcade, 4 s, 8 s, 15 s, 30 s | How long a body stays after the arcade would remove it"},
        {"gravity", "Gravity | Normal | Low, Normal, Heavy | How fast the bodies fall"},
        // (ragdoll2, user 2026-10-27: gore / dismemberment off "so the ragdoll potential is fully shown")
        {"gore", "Dismemberment | On | On, Off | Off: no heads or limbs come off, the bodies stay whole (a mod setting: visual only)"}};
    for (const auto& s : kSettings) {
        ModSetting st;
        if (parseModSetting(s[0], s[1], st)) p.settings.push_back(st);
    }
    return p;
}

// cosmods (GitHub #8 / #10, 2026-11-03): the built-in cosmetic mods' manifests (the same as data\mods\rainbow-blood
// and data\mods\big-head\mod.ini; game/cosmods.h)
static ModPack builtinCosmetic(const std::string& type)
{
    ModPack p;
    p.author = "ReWrithe";
    p.version = "1";
    p.type = type;
    p.defaultOn = false;
    p.builtin = true;
    p.targets = 3;
    std::vector<std::pair<const char*, const char*>> settings;
    if (type == "rainbowblood") {
        p.id = "rainbow-blood";
        p.name = "Rainbow Blood";
        p.description = "The blood, the gore and the blood marks cycle through the colours of the rainbow (visual only)";
        settings = {{"colours", "Colours | Each Splash | Each Splash, Together | Each Splash: every splash and wound its own colour; Together: all blood one colour at a time"},
                    {"speed", "Speed | Normal | Still, Slow, Normal, Fast | How fast the colours change"},
                    // (user 2026-11-03: each creature its own colour for its painted wounds, chosen when it appears)
                    {"creature", "Creature Blood | Rainbow Cycle | Rainbow Cycle, Own Colour Each, Off | The wounds painted on each creature: cycling with the rest, its own colour for its whole life, or as the game paints them"},
                    // rainbow2 (user 2026-11-03): the players' sights and / or HUD in the rainbow too
                    {"hud", "HUD & Crosshair | Off | Off, Crosshair, HUD, Both | The players' crosshairs (each player its own phase) and / or HUD (bullets, 1P / 2P, torches, the score) in the rainbow too"}};
    } else {
        p.id = "big-head";
        p.name = "Big Head";
        p.description = "Big Head mode: the creatures' heads drawn big, the classic cheat (visual only: they are hit where the heads really are)";
        settings = {{"size", "Head Size | 2x | 1.5x, 2x, 3x | How big the heads are"},
                    {"who", "Heads | Everyone | Enemies, Everyone | Everyone: the people too (Rogan and G in the cut-scenes, Sophie, the researchers)"},
                    {"bosses", "Bosses | On | On, Off | The bosses' heads too"}};
    }
    for (const auto& [k, v] : settings) {
        ModSetting st;
        if (parseModSetting(k, v, st)) p.settings.push_back(st);
    }
    return p;
}

std::vector<ModPack> findMods(const std::vector<std::string>& roots)
{
    std::vector<ModPack> all = findModPacks(roots);
    // ragdoll2: a Ragdoll folder from an older version (its mod.ini without a newer setting, e.g. Dismemberment) gets
    // the built-in's missing settings after its own
    // (cosmods: the same for Rainbow Blood / Big Head, e.g. Creature Blood)
    for (ModPack& p : all)
        if (p.type == "ragdoll" || p.type == "rainbowblood" || p.type == "bighead")
            for (const ModSetting& b : (p.type == "ragdoll" ? builtinRagdoll() : builtinCosmetic(p.type)).settings)
                if (std::none_of(p.settings.begin(), p.settings.end(), [&](const ModSetting& q) { return q.key == b.key; }))
                    p.settings.push_back(b);
    bool added = false;
    if (std::none_of(all.begin(), all.end(), [](const ModPack& p) { return p.type == "ragdoll"; }))
        all.push_back(builtinRagdoll()), added = true;
    for (const char* t : {"rainbowblood", "bighead"})   // cosmods
        if (std::none_of(all.begin(), all.end(), [t](const ModPack& p) { return p.type == t; }))
            all.push_back(builtinCosmetic(t)), added = true;
    if (added)
        std::sort(all.begin(), all.end(), [](const ModPack& a, const ModPack& b) { return lower(a.id) < lower(b.id); });
    return all;
}

int parseModDepth(const std::string& value)
{
    const std::string m = lower(trim(value));
    return m == "auto" ? -1 : m == "buffer" ? 0 : m == "sort" ? 1 : -2;
}

int modDepthFor(const ModPack& p, bool model)
{
    // auto: a custom model per pixel (its own shape: no arcade sort keys, and new geometry crossing the old is drawn
    // right that way); pictures on arcade models: the game's setting (Arcade Sort unless --draw-depth says otherwise)
    return p.depth >= 0 ? p.depth : model ? 0 : -1;
}

std::vector<ModPack> findModPacks(const std::vector<std::string>& roots)
{
    std::map<std::string, ModPack> found;
    std::error_code ec;
    for (const std::string& root : roots) {
        if (!fs::is_directory(root, ec)) continue;
        for (const auto& d : fs::directory_iterator(root, ec)) {
            if (!d.is_directory(ec)) continue;
            ModPack p;
            p.id = d.path().filename().string();
            if (lower(p.id) == "lang") continue;      // (lang: data/mods/lang/<code>/: the languages' own packs)
            if (found.count(lower(p.id))) continue;   // the first folder's pack of that name wins
            p.dir = d.path().string();
            p.name = p.id;
            std::ifstream ini(d.path() / "mod.ini");
            std::string line, section = "mod";
            while (std::getline(ini, line)) {
                for (const char* c : {" ;", "\t;"})   // a comment after the value (docs/mods.md's examples)
                    if (const size_t sc = line.find(c); sc != std::string::npos) line.resize(sc);
                line = trim(line);
                if (line.empty() || line[0] == ';' || line[0] == '#') continue;
                if (line[0] == '[') {
                    section = lower(trim(line.substr(1, line.find(']') - 1)));
                    continue;
                }
                const size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string k = lower(trim(line.substr(0, eq))), v = trim(line.substr(eq + 1));
                if (section == "mod") {
                    if (k == "name") p.name = v;
                    else if (k == "author") p.author = v;
                    else if (k == "version") p.version = v;
                    else if (k == "description") p.description = v;
                    else if (k == "models") {
                        const std::string m = lower(v);
                        p.targets = m == "pc" ? 1 : m == "arcade" ? 2 : 3;
                    } else if (k == "type") {
                        p.type = lower(v);
                        if (p.type != "textures" && p.type != "ragdoll" && p.type != "rainbowblood" && p.type != "bighead")
                            std::fprintf(stderr, "mods: %s: type = %s: textures, ragdoll, rainbowblood or bighead (docs/modding.md)\n",
                                         p.id.c_str(), v.c_str());
                    } else if (k == "default") {
                        const std::string m = lower(v);
                        p.defaultOn = !(m == "off" || m == "0" || m == "no");
                    } else if (k == "depth") {
                        const int dm = parseModDepth(v);
                        if (dm >= -1) p.depth = dm;
                        else std::fprintf(stderr, "mods: %s: depth = %s: auto, sort or buffer (docs/modding.md)\n", p.id.c_str(), v.c_str());
                    }
                } else if (section == "settings") {
                    ModSetting st;
                    if (parseModSetting(k, v, st))
                        p.settings.push_back(st);
                    else
                        std::fprintf(stderr, "mods: %s: [settings] %s: <label> | <default> | <choice>, <choice> | <help>\n", p.id.c_str(), k.c_str());
                } else if (section == "recolour" || section == "recolor") {
                    Recolour r;
                    if (k == "blood") {
                        if (parseRecolour(v, r) && r.op == Recolour::Tint)
                            p.bloodRgb = r.rgb[0] << 16 | r.rgb[1] << 8 | r.rgb[2];
                        else
                            std::fprintf(stderr, "mods: %s: blood = %s: a colour #RRGGBB\n", p.id.c_str(), v.c_str());
                    } else if (parseRecolour(v, r)) {
                        p.rules.push_back({k, r});
                    } else {
                        std::fprintf(stderr, "mods: %s: [recolour] %s = %s: not tint / hue / multiply\n", p.id.c_str(), k.c_str(), v.c_str());
                    }
                }
            }
            const fs::path tex = d.path() / "textures";
            if (fs::is_directory(tex, ec))
                for (auto it = fs::recursive_directory_iterator(tex, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
                    if (ec || !it->is_regular_file(ec) || lower(it->path().extension().string()) != ".png") continue;
                    const std::string stem = lower(it->path().stem().string());
                    const size_t us = stem.find_last_of('_');
                    const std::string id = us == std::string::npos ? stem : stem.substr(us + 1);
                    if (!validId(id)) {
                        std::fprintf(stderr, "mods: %s: %s: no texture ID at the end of the name (p<page> or 16 hex digits)\n",
                                     p.id.c_str(), it->path().filename().string().c_str());
                        continue;
                    }
                    p.pngs.emplace(normaliseId(id), it->path().string());
                }
            // custom models: models\<label>_o<object ID in hex>.obj (with its .mtl and PNGs)
            const fs::path mdl = d.path() / "models";
            if (fs::is_directory(mdl, ec))
                for (auto it = fs::recursive_directory_iterator(mdl, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
                    if (ec || !it->is_regular_file(ec) || lower(it->path().extension().string()) != ".obj") continue;
                    const std::string stem = lower(it->path().stem().string());
                    const size_t us = stem.find_last_of('_');
                    const std::string id = us == std::string::npos ? stem : stem.substr(us + 1);
                    char* end = nullptr;
                    const unsigned long v = id.size() > 1 && id[0] == 'o' ? std::strtoul(id.c_str() + 1, &end, 16) : 0;
                    if (!end || *end || v == 0) {
                        std::fprintf(stderr, "mods: %s: %s: no object ID at the end of the name (o<hex>, e.g. fountain_o0a23.obj)\n",
                                     p.id.c_str(), it->path().filename().string().c_str());
                        continue;
                    }
                    p.objs.emplace(uint32_t(v), it->path().string());
                }
            found.emplace(lower(p.id), std::move(p));
        }
    }
    std::vector<ModPack> out;
    for (auto& [k, p] : found) out.push_back(std::move(p));
    return out;
}

void TexturePacks::setActive(std::vector<ModPack> packs)
{
    std::lock_guard<std::mutex> lk(mu_);
    active_ = std::move(packs);
    cache_.clear();
}

int TexturePacks::bloodRgb() const
{
    for (const ModPack& p : active_)
        if (p.bloodRgb >= 0) return p.bloodRgb;
    return -1;
}

std::shared_ptr<const ReplImage> TexturePacks::load(const std::string& file) const
{
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (auto it = cache_.find(file); it != cache_.end()) return it->second;
    }
    auto img = std::make_shared<ReplImage>();
    std::string err;
    if (!readPngRgba(file, img->width, img->height, img->rgba, err)) {
        std::fprintf(stderr, "mods: %s\n", err.c_str());
        img.reset();
    } else {
        while (img->width > 2048 || img->height > 2048) {   // the atlas holds up to 2048 x 2048: halved (box filter)
            const int w = std::max(img->width / 2, 1), h = std::max(img->height / 2, 1);
            std::vector<uint8_t> o(size_t(w) * size_t(h) * 4);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    for (int c = 0; c < 4; ++c) {
                        int s = 0;
                        for (int k = 0; k < 4; ++k) {
                            const int sx = std::min(x * 2 + (k & 1), img->width - 1), sy = std::min(y * 2 + (k >> 1), img->height - 1);
                            s += img->rgba[(size_t(sy) * size_t(img->width) + size_t(sx)) * 4 + size_t(c)];
                        }
                        o[(size_t(y) * size_t(w) + size_t(x)) * 4 + size_t(c)] = uint8_t(s / 4);
                    }
            img->width = w, img->height = h, img->rgba.swap(o);
        }
    }
    std::lock_guard<std::mutex> lk(mu_);
    cache_[file] = img;
    return img;
}

StageReplacements TexturePacks::build(const StageData& stage) const
{
    StageReplacements r;
    if (active_.empty()) return r;
    const int bit = stage.source == Source::PC ? 1 : 2;
    bool any = false;
    for (const ModPack& p : active_)
        any |= (p.targets & bit) && (!p.pngs.empty() || !p.rules.empty() || !p.objs.empty());
    if (!any) return r;
    r.sets.resize(stage.textureSets.size());
    std::map<const ReplImage*, uint32_t> index;
    auto add = [&](const std::shared_ptr<const ReplImage>& img) {
        auto [it, fresh] = index.emplace(img.get(), uint32_t(r.images.size()));
        if (fresh) r.images.push_back(img);
        return it->second;
    };
    size_t fromPng = 0, fromRules = 0, unchanged = 0;
    // the picture for texture `e` under ID `id`: the first pack's PNG, then every matching recolour rule over it (or over
    // the original's colours); null = none (the original stays)
    std::map<std::string, std::shared_ptr<const ReplImage>> generated;   // ID -> recoloured picture (one per ID)
    const ModPack* from = nullptr;   // depthopt: the pack whose picture / rule the last resolve took (its depth =)
    auto resolve = [&](const TexEntry& e, const std::string& id) -> std::shared_ptr<const ReplImage> {
        std::shared_ptr<const ReplImage> img;
        from = nullptr;
        for (const ModPack& p : active_) {
            if (!(p.targets & bit)) continue;
            if (auto f = p.pngs.find(id); f != p.pngs.end()) {
                img = load(f->second);
                if (img) { from = &p; break; }
            }
        }
        // the recolour rules of every active pack (the lowest priority first, so the first pack's rule ends on top)
        std::vector<const Recolour*> ops;
        const std::string full = e.label + "_" + id;
        for (auto p = active_.rbegin(); p != active_.rend(); ++p) {
            if (!(p->targets & bit)) continue;
            for (const ModPack::Rule& rule : p->rules)
                if (glob(rule.pattern.c_str(), full.c_str()) || glob(rule.pattern.c_str(), id.c_str())) {
                    ops.push_back(&rule.op);
                    if (!img) from = &*p;   // (the last one here: the highest priority)
                }
        }
        if (ops.empty()) return img;
        if (auto g = generated.find(id); g != generated.end()) return g->second;
        auto out = std::make_shared<ReplImage>();
        if (img) *out = *img;
        else if (!textureRgba(stage, e, *out)) out.reset();
        if (out)
            for (const Recolour* op : ops) recolourRgba(out->rgba.data(), out->rgba.size() / 4, *op);
        generated[id] = out;
        return out;
    };
    // arcade: the original's colours under this ID (what the export wrote): the shader matches the picture's brightness
    // to the game's own colour of the pixel through it. A picture equal to it is the original: left out
    std::map<std::string, std::shared_ptr<const ReplImage>> refs;
    std::map<std::string, std::vector<const TexEntry*>> idEntries;   // texture ID -> every (set, rectangle) with its texels
    std::map<uint32_t, int> texDepth;   // depthopt: shader key -> the depth mode its picture's pack sets (depth = sort / buffer)
    auto place = [&](const TexEntry& e, const std::string& id, uint32_t key) {
        std::shared_ptr<const ReplImage> img = resolve(e, id);
        if (!img) return false;
        uint32_t ref = StageReplacements::kNoRef;
        bool additive = false;
        if (stage.source == Source::Arcade) {
            // which palette entry the picture was exported in: a texture drawn in several polygon colours was written in
            // the one its export saw most (which can differ from this stage's); the candidate whose colours lie closest
            // to the picture (on a 16 x 16 grid of samples) is taken. Recoloured pictures and -c<entry> ones: their own
            // candidates: (set, palette entry) of every rectangle drawn with these texels in this stage (the export may
            // have written it from another set: the same entry has other colours in another texture area)
            std::vector<TexEntry> cand;
            auto addCand = [&](const TexEntry& src, uint16_t pal, uint8_t luma) {
                for (const TexEntry& c : cand)
                    if (c.set == src.set && c.palette == pal) return;
                TexEntry et = src;
                et.palette = pal;
                et.lumaBase = luma;
                cand.push_back(et);
            };
            addCand(e, e.palette, e.lumaBase);
            if (!generated.count(id) && id.find("-c") == std::string::npos)
                for (const TexEntry* o : idEntries[id])
                    for (const auto& [pal, luma] : o->palettes) addCand(*o, pal, luma);
            std::shared_ptr<const ReplImage> rf;
            double best = 1e300;
            for (const TexEntry& et : cand) {
                auto& slot = refs[id + "/" + std::to_string(et.set) + "/" + std::to_string(et.palette)];
                if (!slot) {
                    auto t = std::make_shared<ReplImage>();
                    if (!textureRgba(stage, et, *t)) continue;
                    slot = t;
                }
                double d = 0;
                for (int j = 0; j < 16; ++j)
                    for (int i = 0; i < 16; ++i) {
                        const uint8_t* a = &slot->rgba[(size_t(j * slot->height / 16) * size_t(slot->width) + size_t(i * slot->width / 16)) * 4];
                        const uint8_t* b = &img->rgba[(size_t(j * img->height / 16) * size_t(img->width) + size_t(i * img->width / 16)) * 4];
                        for (int c = 0; c < 3; ++c) d += std::abs(int(a[c]) - int(b[c]));
                    }
                if (d < best) best = d, rf = slot;
            }
            static const bool keepSame = std::getenv("HOTD_MODS_KEEP_SAME") != nullptr;   // testing: the shader path on an unchanged export
            if (!keepSame && rf && rf->width == img->width && rf->height == img->height && (rf->rgba == img->rgba || best / (16.0 * 16.0 * 3.0) < 1.0)) {
                ++unchanged;
                return false;
            }
            if (rf) ref = add(rf);
            // close to the reference (the export's palette entry found here): the change is added to the game's own colour
            // (every polygon colour kept); else (repainted, or exported in another chapter's colours) the picture's colours
            // at the game's brightness
            additive = rf && best / (16.0 * 16.0 * 3.0) < 6.0;
        }
        (generated.count(id) ? fromRules : fromPng)++;
        r.sets[size_t(e.set)].push_back({key, add(img), ref, additive});
        if (from && modDepthFor(*from, false) >= 0) texDepth[key] = modDepthFor(*from, false);
        return true;
    };
    const std::vector<TexEntry> entries = listStageTextures(stage, true);
    for (const TexEntry& e : entries) {
        idEntries[e.id].push_back(&e);
    }
    for (const TexEntry& e : entries) {
        place(e, e.id, e.key);
        // arcade: pictures for one polygon colour (<id>-c<entry>) over the general one
        for (const auto& [pal, luma] : e.palettes) {
            TexEntry ep = e;
            ep.palette = pal;
            ep.lumaBase = luma;
            const std::string pid = paletteId(e.id, pal);
            if (!resolve(ep, pid)) continue;
            auto [vk, fresh] = r.virtualKeys.emplace(uint64_t(e.key) << 10 | pal, uint32_t(r.virtualKeys.size()));
            if (vk->second >= kMaxVirtual || !place(ep, pid, kVirtualKey + vk->second)) {
                if (fresh) r.virtualKeys.erase(vk);
                continue;
            }
        }
    }
    // custom models (models\<label>_o<object ID>.obj): the first pack's OBJ for a model this stage has. A material named
    // as the export names it (arcade tex_<texture ID>-c<palette entry>, PC page_p<page>) draws the original texture
    // (its rectangle / page, polygon colour, the game's colours) while its picture is the export's; a repainted one is
    // drawn over the original's colours as texture pictures are (arcade) or keyed on its own (PC). Any other material:
    // its picture, keyed on its own (PC: a layer past the pages; arcade: a virtual key, lit by the polygon's light), or
    // its Kd colour untextured
    const uint32_t pcLayers = stage.textureSets.empty() ? 0 : uint32_t(stage.textureSets[0].layers.size());
    uint32_t pcExtra = 0;
    std::map<std::string, const TexEntry*> byId;   // arcade: a rectangle of each texture ID
    for (const TexEntry& e : entries)
        if (!byId.count(e.id)) byId[e.id] = &e;
    for (const ModPack& p : active_) {
        if (!(p.targets & bit)) continue;
        for (const auto& [id, path] : p.objs) {
            if (r.models.count(id) || !stage.models.count(id)) continue;
            ObjModel om;
            std::string err;
            if (!readObj(path, om, err)) { std::fprintf(stderr, "mods: %s\n", err.c_str()); continue; }
            struct Mat { int64_t key = -1; Vertex base{}; bool native = false; float w = 1, h = 1; };
            std::vector<Mat> mats(om.mats.size());
            auto ownKey = [&](size_t m) -> int64_t {
                if (stage.source == Source::PC) return pcLayers + pcExtra++;
                const uint32_t n = uint32_t(r.virtualKeys.size());
                if (n >= kMaxVirtual) return -1;
                r.virtualKeys.emplace(1ull << 63 | uint64_t(id) << 16 | m, n);
                return kVirtualKey + n;
            };
            for (size_t m = 0; m < om.mats.size(); ++m) {
                const ObjMaterial& om2 = om.mats[m];
                Mat& mt = mats[m];
                std::shared_ptr<const ReplImage> img = om2.picture.empty() ? nullptr : load(om2.picture);
                const std::string name = lower(om2.name);
                // the export's own material: the original texture
                if (stage.source == Source::Arcade && name.rfind("tex_", 0) == 0 && name.find("-c") != std::string::npos) {
                    const std::string tid = name.substr(4, name.find("-c") - 4);
                    const uint16_t pal = uint16_t(std::atoi(name.c_str() + name.find("-c") + 2) & 0x3FF);
                    if (auto b = byId.find(tid); b != byId.end()) {
                        TexEntry ep = *b->second;
                        ep.palette = pal;
                        if (auto pl = ep.palettes.find(pal); pl != ep.palettes.end()) ep.lumaBase = pl->second;
                        mt.native = true;
                        mt.w = float(ep.width), mt.h = float(ep.height);
                        Vertex& v = mt.base;
                        v.rect[0] = uint16_t(ep.x), v.rect[1] = uint16_t(ep.y), v.rect[2] = uint16_t(ep.width), v.rect[3] = uint16_t(ep.height);
                        v.layer = uint16_t(ep.sheet);
                        v.palette = pal;
                        v.color[3] = ep.lumaBase;
                        v.flags = uint16_t(kTextured | (ep.translucent ? kTranslucent : 0));
                        ReplImage orig;
                        const bool have = textureRgba(stage, ep, orig);
                        const bool same = !img || (have && orig.width == img->width && orig.height == img->height && orig.rgba == img->rgba);
                        if (!same && (mt.key = ownKey(m)) >= 0) {   // repainted: over the original (reference: the export's colours)
                            const uint32_t ref = have ? add(std::make_shared<ReplImage>(orig)) : StageReplacements::kNoRef;
                            const uint32_t idx = add(img);
                            for (size_t s = 0; s < r.sets.size(); ++s) r.sets[s].push_back({uint32_t(mt.key), idx, ref, true});
                        }
                        continue;
                    }
                }
                if (stage.source == Source::PC && name.rfind("page_p", 0) == 0 && !stage.textureSets.empty()) {
                    const TextureSet& ts = stage.textureSets[0];
                    const unsigned page = unsigned(std::atoi(name.c_str() + 6));
                    for (size_t l = 0; l < ts.pageIds.size() && l < ts.layers.size(); ++l)
                        if (ts.pageIds[l] == page) {
                            if (!img || (img->width == ts.width && img->height == ts.height && img->rgba == ts.layers[l])) {
                                mt.native = true;
                                mt.w = float(ts.width), mt.h = float(ts.height);
                                mt.base.rect[2] = uint16_t(ts.width), mt.base.rect[3] = uint16_t(ts.height);
                                mt.base.layer = uint16_t(l);
                                mt.base.flags = kTextured;
                            }
                            break;
                        }
                    if (mt.native) continue;
                }
                if (!img || (mt.key = ownKey(m)) < 0) continue;
                const uint32_t idx = add(img);
                for (size_t s = 0; s < r.sets.size(); ++s)
                    if (stage.source == Source::Arcade || s == 0) r.sets[s].push_back({uint32_t(mt.key), idx, StageReplacements::kNoRef, false});
            }
            std::vector<Vertex>& out = r.models[id];
            if (modDepthFor(p, true) >= 0) r.modelDepth[id] = modDepthFor(p, true);   // depthopt (auto: buffer)
            for (const ObjModel::Tri& t : om.tris) {
                const int m = t.mat;
                const Mat* mt = m >= 0 ? &mats[size_t(m)] : nullptr;
                const float* kd = m >= 0 ? om.mats[size_t(m)].kd : nullptr;
                float e1[3], e2[3], nr[3];
                for (int c = 0; c < 3; ++c) { e1[c] = t.p[1][c] - t.p[0][c]; e2[c] = t.p[2][c] - t.p[0][c]; }
                nr[0] = e1[1] * e2[2] - e1[2] * e2[1];
                nr[1] = e1[2] * e2[0] - e1[0] * e2[2];
                nr[2] = e1[0] * e2[1] - e1[1] * e2[0];
                const float len = std::sqrt(nr[0] * nr[0] + nr[1] * nr[1] + nr[2] * nr[2]);
                const bool native = mt && mt->native;
                for (int k = 0; k < 3; ++k) {
                    Vertex v{};
                    if (native) v = mt->base;
                    for (int c = 0; c < 3; ++c) {
                        v.pos[c] = t.p[k][c];
                        v.normal[c] = len > 0 ? nr[c] / len : 0.0f;
                    }
                    // OBJ v runs up, texture rows down; native materials in texels of the original texture
                    const float w = native ? mt->w : 1.0f, h = native ? mt->h : 1.0f;
                    v.uv[0] = t.t[k][0] * w;
                    v.uv[1] = (1.0f - t.t[k][1]) * h;
                    v.flags = uint16_t(v.flags | kDoubleSided);
                    if (!native) {
                        v.rect[0] = v.rect[1] = 0;
                        v.rect[2] = v.rect[3] = 1;   // the shader scales uv by the picture's size over this
                        v.palette = 0xFFFF;
                        if (mt && mt->key >= 0) v.flags = uint16_t(v.flags | kTextured);
                        if (stage.source == Source::PC) {
                            v.layer = uint16_t(mt && mt->key >= 0 ? mt->key : 0);
                            for (int c = 0; c < 3; ++c) v.color[c] = uint8_t(std::clamp(kd ? kd[c] : 1.0f, 0.0f, 1.0f) * 255.0f);
                            v.color[3] = 255;
                        } else {
                            for (int c = 0; c < 3; ++c) v.color[c] = uint8_t(std::clamp(kd ? kd[c] : 1.0f, 0.0f, 1.0f) * 31.0f);
                            v.color[3] = 0;
                        }
                    }
                    if (stage.source == Source::Arcade && mt && mt->key >= 0) v.lodCoef = uint16_t((uint32_t(mt->key) - kVirtualKey + 1) << 5);
                    out.push_back(v);
                }
            }
            std::fprintf(stderr, "mods: %s stage %d: model %X from %s (%zu triangles, %zu materials)\n", sourceName(stage.source),
                         stage.stage, unsigned(id), path.c_str(), om.tris.size(), om.mats.size());
        }
    }
    std::fprintf(stderr, "mods: %s stage %d: %zu textures from pack pictures, %zu recoloured, %zu unchanged (left out), %zu "
                 "pictures, %zu colour-only, %zu models\n", sourceName(stage.source), stage.stage, fromPng, fromRules, unchanged,
                 r.images.size(), r.virtualKeys.size(), r.models.size());
    // depthopt: the models drawing a picture of a pack with depth = sort / buffer get that mode (a custom model keeps its own)
    if (!texDepth.empty())
        for (const auto& [key, m] : stage.models) {
            if (r.models.count(key)) continue;
            auto modeOf = [&]() {
                for (const Mesh& me : m.meshes)
                    for (size_t i = 0; i + 2 < me.vertices.size(); i += 3) {
                        const Vertex& v = me.vertices[i];
                        if (!(v.flags & kTextured)) continue;
                        uint32_t k = stage.source == Source::PC ? uint32_t(v.layer) : arcadeTextureKey(v);
                        if (stage.source == Source::Arcade)
                            if (auto vk = r.virtualKeys.find(uint64_t(k) << 10 | (v.palette & 0x3FF)); vk != r.virtualKeys.end())
                                k = kVirtualKey + vk->second;
                        if (auto t = texDepth.find(k); t != texDepth.end()) return t->second;
                    }
                return -1;
            };
            if (const int dm = modeOf(); dm >= 0) r.modelDepth.emplace(key, dm);
        }
    if (r.images.empty()) r.sets.clear();
    return r;
}

TexturePacks& texturePacks()
{
    static TexturePacks p;
    return p;
}

int modDepthTest()
{
    // a mod folder in the temp folder: mod.ini with depth = <value>, models\test_o1.obj (one triangle) for model 1 of a
    // small PC stage (models 1 and 2); the stage's modelDepth for model 1 is checked, model 2 must have none
    int checks = 0, failed = 0;
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) / ("hotd_mods_depth_test_" + std::to_string(std::rand()));
    fs::create_directories(root / "depthmod" / "models", ec);
    {
        std::ofstream o(root / "depthmod" / "models" / "test_o1.obj");
        o << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }
    StageData st;
    st.source = Source::PC;
    st.models[1].key = 1;
    st.models[2].key = 2;
    struct Case { const char* line; int want; };
    for (const Case& c : {Case{"", 0}, Case{"depth = auto", 0}, Case{"depth = buffer", 0}, Case{"depth = sort", 1}}) {
        {
            std::ofstream o(root / "depthmod" / "mod.ini");
            o << "[mod]\nname = Depth Test\n" << c.line << "\n";
        }
        TexturePacks tp;
        tp.setActive(findModPacks({root.string()}));
        const StageReplacements r = tp.build(st);
        auto m1 = r.modelDepth.find(1);
        const int got = m1 == r.modelDepth.end() ? -1 : m1->second;
        const bool ok = r.models.count(1) && got == c.want && !r.modelDepth.count(2);
        ++checks;
        if (!ok) ++failed;
        std::printf("mods test: depth: mod.ini \"%s\": custom model drawn with %s%s\n", c.line,
                    got == 0 ? "Depth Buffer" : got == 1 ? "Arcade Sort" : "the game's setting", ok ? "" : "   FAILED");
    }
    // the values (and a picture-only pack's auto: the game's own)
    ModPack p;
    ++checks;
    if (parseModDepth(" Buffer") != 0 || parseModDepth("sort") != 1 || parseModDepth("auto") != -1 || parseModDepth("x") != -2 ||
        modDepthFor(p, false) != -1 || modDepthFor(p, true) != 0)
        ++failed, std::printf("mods test: depth: the values   FAILED\n");
    fs::remove_all(root, ec);
    return failed;
}

int exportStageTextures(const StageData& stage, const std::string& folder, ExportIndex& index, const std::set<int>* bloodLayers)
{
    std::error_code ec;
    fs::create_directories(folder, ec);
    int written = 0;
    for (TexEntry& e : listStageTextures(stage, false)) {
        if (bloodLayers && bloodLayers->count(int(e.key))) e.blood = true;
        auto [it, fresh] = index.rows.emplace(e.id, ExportIndex::Row{});
        ExportIndex::Row& row = it->second;
        row.chapters.insert(stage.stage + 1);
        row.translucent |= e.translucent;
        row.blood |= e.blood;
        for (const std::string& u : e.users)
            if (row.users.size() < 16) row.users.insert(u);
        for (const auto& [pal, luma] : e.palettes) row.palettes.insert(pal);
        // arcade blood: the texture again in the blood colour entry (1023) alone, <id>-c1023 (a yellow-blood pack
        // recolours these)
        if (e.palettes.count(1023) && row.bloodFile.empty()) {
            TexEntry eb = e;
            eb.palette = 1023;
            eb.lumaBase = e.palettes.at(1023);
            ReplImage img;
            if (textureRgba(stage, eb, img)) {
                row.bloodFile = "blood-" + e.label + "_" + paletteId(e.id, 1023) + ".png";
                if (writePngRgba((fs::path(folder) / row.bloodFile).string(), img.width, img.height, img.rgba)) ++written;
            }
        }
        if (!fresh) continue;
        ReplImage img;
        if (!textureRgba(stage, e, img)) continue;
        row.file = e.label + "_" + e.id + ".png";
        row.width = img.width;
        row.height = img.height;
        if (writePngRgba((fs::path(folder) / row.file).string(), img.width, img.height, img.rgba)) ++written;
    }
    return written;
}

bool writeExportIndex(const ExportIndex& index, const std::string& file, const std::string& source)
{
    std::ofstream f(file);
    if (!f) return false;
    f << "# hotd --export-textures (" << source << " models): one line per texture. The ID (the name's last part, after the\n"
         "# last '_') is what a pack's PNG must end with; the rest of the name is free. docs/mods.md\n"
         "# Arcade: the picture applies to every polygon colour (palette entry) the texture is drawn in; <id>-c<entry>\n"
         "# applies to that entry only (palettes column; 1023 = the blood colour, written as blood-*-c1023.png).\n"
         "# id\tfile\twidth\theight\tflags\tchapters\tpalettes\tused by\n";
    for (const auto& [id, r] : index.rows) {
        if (r.file.empty()) continue;
        std::string flags;
        if (r.blood) flags += "blood ";
        if (r.translucent) flags += "translucent ";
        if (flags.empty()) flags = "-";
        else flags.pop_back();
        std::string ch, us, pl;
        for (int c : r.chapters) ch += (ch.empty() ? "" : ",") + std::to_string(c);
        for (const std::string& u : r.users) us += (us.empty() ? "" : " ") + u;
        for (uint16_t p : r.palettes) pl += (pl.empty() ? "" : ",") + std::to_string(p);
        f << id << '\t' << r.file << '\t' << r.width << '\t' << r.height << '\t' << flags << '\t' << ch << '\t'
          << (pl.empty() ? "-" : pl) << '\t' << us << '\n';
        if (!r.bloodFile.empty())
            f << paletteId(id, 1023) << '\t' << r.bloodFile << '\t' << r.width << '\t' << r.height << "\tblood\t" << ch << "\t1023\t" << us << '\n';
    }
    return true;
}

} // namespace hotd

#include "assets/arcade_loader.h"
#include "assets/pc_loader.h"

namespace hotd {

int exportTextures(const std::string& folder, int models, const std::string& regions, const std::string& pcDir, std::string& err)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string src = models == 1 ? "pc" : "arcade";
    const std::string dir = (fs::path(folder) / src).string();
    fs::create_directories(dir, ec);
    if (ec) { err = "cannot make " + dir + ": " + ec.message(); return -1; }
    std::vector<uint32_t> ids;
    for (uint32_t id = 1; id < 0x2000; ++id) ids.push_back(id);   // every model the game may draw (as GameView::init)
    ExportIndex index;
    int written = 0, stages = 0;
    if (models == 1) {
        if (pcDir.empty()) { err = "no PC files (the launcher's PC folder, or --pc)"; return -1; }
        PcSource pc(PcConfig{pcDir, 0});
        if (!pc.available()) { err = pc.status(); return -1; }
        for (int c = 0; c <= 6; ++c) {   // chapters 1-4, then WORLD5 / WORLD6 (the prologue / ending scenes)
            pc.requestModels(c, ids);
            std::string e;
            std::unique_ptr<StageData> st = pc.loadStage(c, e);
            if (!st) { std::fprintf(stderr, "export: PC stage %d: %s\n", c, e.c_str()); continue; }
            ++stages;
            // the blood texels: where the green blood set differs from the red one (BIGGER.VMC / B_RED)
            std::set<int> blood;
            if (!st->textureSets.empty()) {
                const TextureSet& ts = st->textureSets[0];
                std::vector<std::vector<uint8_t>> green;
                std::string be;
                if (pcBloodPages(pcDir, 1, ts.pageIds, green, be) && green.size() == ts.layers.size())
                    for (size_t l = 0; l < green.size(); ++l)
                        for (size_t i = 0; i + 3 < green[l].size() && i + 3 < ts.layers[l].size(); i += 4)
                            if (green[l][i + 3] && ts.layers[l][i + 3] && (green[l][i] != ts.layers[l][i] || green[l][i + 1] != ts.layers[l][i + 1])) {
                                blood.insert(int(l));
                                break;
                            }
            }
            written += exportStageTextures(*st, dir, index, &blood);
            std::fprintf(stderr, "export: PC stage %d: %d files so far\n", c, written);
        }
    } else {
        ArcadeSource arc(ArcadeConfig{regions, std::string(), std::string(), true, false});
        if (!arc.available()) { err = arc.status(); return -1; }
        for (int c = 0; c < arc.stageCount(); ++c) {
            arc.requestModels(c, ids);
            std::string e;
            std::unique_ptr<StageData> st = arc.loadStage(c, e);
            if (!st) { std::fprintf(stderr, "export: arcade chapter %d: %s\n", c + 1, e.c_str()); continue; }
            ++stages;
            written += exportStageTextures(*st, dir, index);
            std::fprintf(stderr, "export: arcade chapter %d: %d files so far\n", c + 1, written);
        }
    }
    if (!stages) { err = "no stage could be loaded"; return -1; }
    writeExportIndex(index, (fs::path(dir) / "index.tsv").string(), src);
    return written;
}

} // namespace hotd

namespace hotd {

namespace {

// one model as OBJ + MTL + its pictures (custom models start from it: the same space, the same parts)
bool writeModelObj(const StageData& stage, const Model& model, const std::string& dir, std::string& err)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    char base[64];
    std::snprintf(base, sizeof base, "%s_o%04x", sanitise(model.name.empty() ? "model" : model.name).c_str(), unsigned(model.key));
    std::ofstream obj(fs::path(dir) / (std::string(base) + ".obj")), mtl(fs::path(dir) / (std::string(base) + ".mtl"));
    if (!obj || !mtl) { err = "cannot write " + dir; return false; }
    obj << "# hotd --export-models: model " << std::hex << model.key << std::dec << " (" << model.name << "), "
        << sourceName(stage.source) << " models, in its own space (left-handed: x right, y up, z forward). docs/mods.md\n"
        << "mtllib " << base << ".mtl\n";
    std::map<std::string, std::string> mats;   // material name -> picture file ("" = untextured)
    std::string cur;
    size_t nv = 0;
    const int set = std::clamp(model.preferredVariant, 0, std::max(int(stage.textureSets.size()) - 1, 0));
    for (const Mesh& me : model.meshes) {
        if (me.variant >= 0 && me.variant != model.preferredVariant) continue;
        for (size_t i = 0; i + 2 < me.vertices.size(); i += 3) {
            const Vertex& v0 = me.vertices[i];
            std::string mat;
            float w = 1, h = 1;
            if (v0.flags & kTextured) {
                TexEntry e;
                const TextureSet& ts = stage.textureSets[size_t(stage.source == Source::PC ? 0 : set)];
                if (stage.source == Source::PC) {
                    if (v0.layer >= ts.pageIds.size()) continue;
                    char b[24];
                    std::snprintf(b, sizeof b, "page_p%04u", unsigned(ts.pageIds[v0.layer]));
                    mat = b;
                    e.set = 0;
                    e.key = v0.layer;
                    e.width = e.height = ts.width;
                } else {
                    e.set = set;
                    e.sheet = v0.layer & 1;
                    e.x = v0.rect[0];
                    e.y = v0.rect[1];
                    e.width = v0.rect[2];
                    e.height = v0.rect[3];
                    e.translucent = (v0.flags & kTranslucent) != 0;
                    e.palette = v0.palette;
                    e.lumaBase = uint8_t(v0.color[3] & 0x7F);
                    if (ts.layers.size() < 2 || e.width > 2048 || e.height > 1024) continue;
                    mat = "tex_" + arcadeHash(ts, e.sheet, e.x, e.y, e.width, e.height) + "-c" + std::to_string(e.palette);
                }
                w = float(e.width);
                h = float(e.height);
                if (!mats.count(mat)) {
                    ReplImage img;
                    if (textureRgba(stage, e, img)) writePngRgba((fs::path(dir) / (mat + ".png")).string(), img.width, img.height, img.rgba);
                    mats[mat] = mat + ".png";
                    mtl << "newmtl " << mat << "\nKd 1 1 1\nmap_Kd " << mat << ".png\n\n";
                }
            } else {
                char b[32];
                float rgb[3];
                if (stage.source == Source::PC) {
                    for (int c = 0; c < 3; ++c) rgb[c] = v0.color[c] / 255.0f;
                } else {
                    const TextureSet& ts = stage.textureSets[size_t(set)];
                    const uint16_t p = v0.palette < ts.basePalette.size() ? ts.basePalette[v0.palette] : 0x7FFF;
                    rgb[0] = (p & 31) / 31.0f, rgb[1] = ((p >> 5) & 31) / 31.0f, rgb[2] = ((p >> 10) & 31) / 31.0f;
                }
                std::snprintf(b, sizeof b, "colour_%02x%02x%02x", int(rgb[0] * 255), int(rgb[1] * 255), int(rgb[2] * 255));
                mat = b;
                if (!mats.count(mat)) {
                    mats[mat] = "";
                    mtl << "newmtl " << mat << "\nKd " << rgb[0] << ' ' << rgb[1] << ' ' << rgb[2] << "\n\n";
                }
            }
            if (mat != cur) { obj << "usemtl " << mat << "\n"; cur = mat; }
            for (int k = 0; k < 3; ++k) {
                const Vertex& v = me.vertices[i + size_t(k)];
                obj << "v " << v.pos[0] << ' ' << v.pos[1] << ' ' << v.pos[2] << "\n";
                obj << "vt " << v.uv[0] / w << ' ' << 1.0f - v.uv[1] / h << "\n";
            }
            obj << "f " << nv + 1 << '/' << nv + 1 << ' ' << nv + 2 << '/' << nv + 2 << ' ' << nv + 3 << '/' << nv + 3 << "\n";
            nv += 3;
        }
    }
    if (!nv) { err = "model has no triangles"; return false; }
    return true;
}

} // namespace

int exportModels(const std::string& folder, int models, const std::string& regions, const std::string& pcDir,
                 const std::vector<uint32_t>& ids, std::string& err)
{
    namespace fs = std::filesystem;
    const std::string dir = (fs::path(folder) / (models == 1 ? "pc" : "arcade") / "models").string();
    std::set<uint32_t> left(ids.begin(), ids.end());
    int written = 0;
    auto take = [&](const StageData& st) {
        for (auto it = left.begin(); it != left.end();) {
            auto m = st.models.find(*it);
            std::string e;
            if (m != st.models.end() && m->second.triangleCount() > 0 && writeModelObj(st, m->second, dir, e)) {
                ++written;
                it = left.erase(it);
            } else {
                ++it;
            }
        }
    };
    if (models == 1) {
        PcSource pc(PcConfig{pcDir, 0});
        if (!pc.available()) { err = pc.status(); return -1; }
        for (int c = 0; c <= 6 && !left.empty(); ++c) {
            pc.requestModels(c, ids);
            std::string e;
            if (auto st = pc.loadStage(c, e)) take(*st);
        }
    } else {
        ArcadeSource arc(ArcadeConfig{regions, std::string(), std::string(), true, false});
        if (!arc.available()) { err = arc.status(); return -1; }
        for (int c = 0; c < arc.stageCount() && !left.empty(); ++c) {
            arc.requestModels(c, ids);
            std::string e;
            if (auto st = arc.loadStage(c, e)) take(*st);
        }
    }
    for (uint32_t id : left) std::fprintf(stderr, "export: model %X: not found (or no triangles)\n", unsigned(id));
    return written;
}

} // namespace hotd
