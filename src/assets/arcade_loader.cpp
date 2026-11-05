#include "assets/arcade_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace hotd {

namespace {

constexpr uint32_t kModelTable = 0xE73530;   // main_data: 16 bytes per object ID (§12)
// generated skins (decompilation.md "World drawing"): maincpu tables by creature type, main_data tables by skin number
constexpr size_t kSkinByType = 0x20E40;          // maincpu: int16 skin number per type (-1 none)
constexpr size_t kSkinHeader = 0x20F00;          // maincpu: texture point, texture header per type
constexpr size_t kSkinNodeRole = 0x1184E3;       // maincpu: + type x 0xF8 + node x 8: 8 = corners (A), 9 = drawn with (B)
constexpr size_t kSkinTemplates = 0xF803A0;      // main_data: polygon model per skin, 0x640 bytes
constexpr size_t kSkinCorners = 0xF8B2A0;        // main_data: corner points (in A space), 0x90 bytes per skin
constexpr size_t kSkinVertexMap = 0xF8C260;      // main_data: vertex numbers, 0x60 bytes per skin
constexpr size_t kSkinCornerMap = 0xF8CCE0;      // main_data: corner -> vertex map entry, 0x30 bytes per skin
constexpr size_t kSkinPolygonCount = 0xF8D220;   // main_data: polygons per skin
constexpr size_t kSkinShared = 0xF8D290;         // main_data: shared vertex, its copy (-1 = p1) per skin
constexpr int kSkinCount = 28;
constexpr uint32_t kLumaTable = 0xE9AAC0;    // main_data: 128 curves x 128 entries (§13)
constexpr uint32_t kPlacementTables[4] = {0xBEF00, 0xBFEA0, 0xC0640, 0xC0A90};  // maincpu, Chapters 1-4 (§12)
// Draw zones: per chapter a pointer to 0x32-byte lists of placement indices (0xFF ends a list); the world task (maincpu
// 0x4B300) draws the list of the current zone (work RAM 0x51FDD4, set by the event opcode BGTB)
constexpr uint32_t kZoneListPtrs = 0xC0BB0;
constexpr int kZoneListSize = 0x32, kMaxZones = 64;
constexpr int kSheetW = 2048, kSheetH = 1024;

// Placements that are alternate states of another placement at the same position (probably damaged/intact versions).
// Drawing both z-fights; until the event scripts say which state is shown, only the first is drawn.
constexpr uint32_t kAlternateStates[] = {0x91F, 0xCED, 0x111D, 0x111E};

// Built-in texture sets (MAME dump tags in private\mame_dumps\fullgame_us), used when no texset file is given:
// the first gameplay texture set of each chapter.
const char* const kDefaultTags[4] = {"t00300", "t03500", "t06700", "t11100"};

uint32_t rd32(const std::vector<uint8_t>& b, size_t off)
{
    uint32_t v;
    std::memcpy(&v, b.data() + off, 4);
    return v;
}
float rdf(const std::vector<uint8_t>& b, size_t off)
{
    float v;
    std::memcpy(&v, b.data() + off, 4);
    return v;
}
uint16_t rd16(const std::vector<uint8_t>& b, size_t off)
{
    return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
}

struct V3 { float x, y, z; };

V3 rdv(const std::vector<uint8_t>& b, size_t word)
{
    return {rdf(b, word * 4), rdf(b, word * 4 + 4), rdf(b, word * 4 + 8)};
}

} // namespace

ArcadeSource::ArcadeSource(ArcadeConfig cfg) : cfg_(std::move(cfg))
{
    const char* need[] = {"maincpu.bin", "main_data.bin", "polygons.bin", "textures.bin"};
    for (const char* n : need)
        if (!fileExists(cfg_.regionsDir + "/" + n)) {
            status_ = "arcade data not found (" + cfg_.regionsDir + "/" + n + ")";
            return;
        }
    // no texset file: every stage's texture sets come from the ROM (romStageTags, filled once the regions are read);
    // a texset file without a "sets" line for a stage falls back to the MAME dump tags of kDefaultTags
    if (!cfg_.texsetFile.empty()) {
        for (int s = 0; s < 4; ++s)
            stageTags_[s] = {kDefaultTags[s]};
        std::ifstream f(cfg_.texsetFile);
        std::string line;
        bool any = false;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            std::string kind;
            ss >> kind;
            int stage = -1;
            if (kind == "sets" && (ss >> stage) && stage >= 0 && stage < 4) {
                if (!any) for (auto& t : stageTags_) t.clear();
                any = true;
                std::string tag;
                while (ss >> tag) stageTags_[stage].push_back(tag);
            } else if (kind == "light" && (ss >> stage) && stage >= 0 && stage < 4) {
                int set = 0;
                Light& l = lights_[stage][(ss >> set, set)];
                ss >> l.dir[0] >> l.dir[1] >> l.dir[2];
                for (int i = 0; i < 32; ++i) ss >> l.diffuse[i] >> l.ambient[i];
                l.known = bool(ss);
            } else if (kind == "valid" && (ss >> stage) && stage >= 0 && stage < 4) {
                std::string id;
                int set;
                if (ss >> id) {
                    auto& v = modelValid_[stage][static_cast<uint32_t>(std::stoul(id, nullptr, 16))];
                    // later lines add sets: a set the replay saw the model drawn in stays valid even when the PC-match
                    // block (texset_pcmatch.py) prefers another one (e.g. 0x923, the courtyard fence, formats.md §16)
                    while (ss >> set)
                        if (std::find(v.begin(), v.end(), set) == v.end()) v.push_back(set);
                }
            } else if (kind == "rows" && (ss >> stage) && stage >= 0 && stage < 4) {
                int set;
                std::string pair;
                if (ss >> set) {
                    auto& v = rowSwaps_[stage][set];
                    v.clear();
                    while (ss >> pair)
                        if (auto colon = pair.find(':'); colon != std::string::npos)
                            v.push_back({std::stoi(pair.substr(0, colon)), std::stoi(pair.substr(colon + 1))});
                }
            } else if (kind == "sky" && (ss >> stage) && stage >= 0 && stage < 4) {
                int set;
                std::string id;
                StageData::Sky sky{};
                if (ss >> set >> id >> sky.offset[0] >> sky.offset[1] >> sky.offset[2]) {
                    sky.modelKey = static_cast<uint32_t>(std::stoul(id, nullptr, 16));
                    skies_[stage][set].push_back(sky);
                }
            } else if (kind == "zone" && (ss >> stage) && stage >= 0 && stage < 4) {
                StageData::Zone z{};
                if (ss >> z.variant >> z.pos[0] >> z.pos[1] >> z.pos[2]) zones_[stage].push_back(z);
            } else if (kind == "model" && (ss >> stage) && stage >= 0 && stage < 4) {
                std::string id;
                int set = 0;
                if (ss >> id >> set) modelSet_[stage][static_cast<uint32_t>(std::stoul(id, nullptr, 16))] = set;
            }
        }
        for (int s = 0; s < 4; ++s)
            if (stageTags_[s].empty()) stageTags_[s] = {kDefaultTags[s]};
    }
    available_ = true;
    status_ = cfg_.texsetFile.empty() ? "arcade ROM regions in " + cfg_.regionsDir + " (textures and colours from the ROM)"
                                      : "arcade ROM regions in " + cfg_.regionsDir + ", texture sets " + cfg_.texsetFile +
                                            ", MAME dumps in " + cfg_.dumpsDir;
}

// The texture sets of a stage from the ROM alone: the texture areas chapter_load picks per event (maincpu 0xAB0B0: a
// pointer per chapter to one word per event; the lists follow each other, the last ends at 0xAB0B0), in the order the
// chapter first loads them, then every other game area (TEXCHG in the event scripts can load any: Chapter 1's boss
// room is area 4, which no event starts in) in ascending order. Area 2 (the attract demo) is left out: its palette
// record is applied before every set anyway (loadTextureSet). Each area is rebuilt from the ROM ("rom:<n>"); the
// GameView draws with the set of the area the game loaded last.
std::vector<std::string> ArcadeSource::romStageTags(int stage) const
{
    constexpr uint32_t kAreaLists = 0xAB0B0;
    std::vector<uint32_t> starts;
    for (uint32_t c = 0; c < 5; ++c)
        starts.push_back(rd32(maincpu_, kAreaLists + c * 4));
    auto listOf = [&](uint32_t c) {
        std::vector<int> out;
        uint32_t end = kAreaLists;
        for (uint32_t s : starts)
            if (s > starts[c] && s < end) end = s;
        for (uint32_t p = starts[c]; p < end; p += 4) {
            const uint32_t a = rd32(maincpu_, p);
            if (a >= 1 && a <= 12) out.push_back(int(a));
        }
        return out;
    };
    std::vector<int> order;
    auto add = [&](int a) {
        if (a != 2 && std::find(order.begin(), order.end(), a) == order.end()) order.push_back(a);
    };
    for (int a : listOf(uint32_t(stage)))
        add(a);
    std::vector<int> rest;
    for (uint32_t c = 0; c < 5; ++c)
        for (int a : listOf(c)) rest.push_back(a);
    for (int a = 1; a <= 12; ++a)
        if (std::find(rest.begin(), rest.end(), a) != rest.end()) add(a);
    // title worker: area 2 is the prologue's (mode9_story_start, chapter slot 5 = Chapter 1's world): last in Chapter
    // 1's sets, so the chapter's own order is unchanged
    if (stage == 0)
        order.push_back(2);
    // title-2: area 12 is the prologue's last scene (kind107 in Chapter 4's world, kind108 slot 3), in no chapter's
    // list: last in Chapter 4's sets (the attract loads that stage for the scene, GameView::ensureExtra)
    if (stage == 3 && std::find(order.begin(), order.end(), 12) == order.end())
        order.push_back(12);
    // galfix3: area 2 is also the ending's (chapter slot 6 in Chapter 4's world: kind111_texture_area loads it in events
    // 5 / 6; the zombie Sophie, 67, is drawn there): last in Chapter 4's sets too
    if (stage == 3)
        order.push_back(2);
    std::vector<std::string> tags;
    for (int a : order)
        tags.push_back("rom:" + std::to_string(a));
    return tags;
}

bool ArcadeSource::loadRegions(std::string& err)
{
    if (!polygons_.empty()) return true;
    const bool ok = readFile(cfg_.regionsDir + "/maincpu.bin", maincpu_, err) &&
                    readFile(cfg_.regionsDir + "/main_data.bin", mainData_, err) &&
                    readFile(cfg_.regionsDir + "/polygons.bin", polygons_, err) &&
                    readFile(cfg_.regionsDir + "/textures.bin", textures_, err);
    if (ok) loadNames();
    return ok;
}

std::vector<std::string> arcadeObjectNames(const std::vector<uint8_t>& mainData)
{
    constexpr size_t kNamePtrs = 0xEE4EB0, kIds = 7477;
    std::vector<std::string> out(kIds);
    for (size_t id = 0; id < kIds && kNamePtrs + id * 4 + 4 <= mainData.size(); ++id) {
        const size_t p = size_t(rd32(mainData, kNamePtrs + id * 4)) - 0x2000000;
        if (p + 3 > mainData.size() || std::memcmp(mainData.data() + p, "PN_", 3) != 0) continue;
        size_t n = 3;
        while (p + n < mainData.size() && mainData[p + n]) ++n;
        out[id].assign(reinterpret_cast<const char*>(mainData.data() + p + 3), n - 3);
    }
    return out;
}

void ArcadeSource::loadNames() { names_ = arcadeObjectNames(mainData_); }

// Color translation tables as the game builds them when it loads area n (docs/formats.md §19): the startup generator at
// maincpu 0x1180 for every base value, then the odd rows 1-27 from the area's row list (maincpu 0xA99A0[n] -> 14 pointers to
// 64 x RGB bytes, scaled by the float at 0xA831C), as the area loader at maincpu 0x3506C does. Scene events later swap some odd
// rows (e.g. rows 7 and 9 in the courtyard) from the same row library; that isn't emulated.
void ArcadeSource::buildColorXlat(int n, const std::vector<std::pair<int, int>>& rowSwaps, std::vector<uint8_t>& out) const
{
    out.assign(3 * 32 * 256, 0);
    auto put = [&](int c, int base, int bright, int v) { out[(size_t(c) * 32 + base) * 256 + bright] = uint8_t(std::min(v, 255)); };
    for (int b = 0; b < 32; ++b) {
        // single/double precision as in the i960 code (sqrtr/mulr single, mulrl/addrl double)
        const float c = float(b * 8 + b / 4) / 255.0f;
        const float m = float(double(std::sqrt(c) * 0.5f) + 0.5 * double(c) * double(c));
        for (int l = 0; l < 64; ++l) {
            const float x = (float(l) / 63.0f) * m;
            const float y = float(double(std::sqrt(x) * 0.5f) + 0.5 * double(x) * double(x));
            for (int ch = 0; ch < 3; ++ch) put(ch, b, l, int(y * 255.0f));
        }
        for (int l = 64; l < 256; ++l)
            for (int ch = 0; ch < 3; ++ch) put(ch, b, l, int(m * 255.0f));
    }
    float k;
    const uint32_t kBits = rd32(maincpu_, 0xA831C);
    std::memcpy(&k, &kBits, 4);
    // bugs2 (GitHub #28, red pixels on the Chapter 1 wall weapons): the row library's white ramps (entries 5-10, 38, 68,
    // 100) end before brightness 63: they reach white at 56-57 (entry 38: 51-52), then hold marker bytes, not colours
    // ((255, 0..162, 0) then black, blue, green: entry 100 = area 3's row 21, the axes' and swords' metal). The game
    // copies all 64 (MAME's colour RAM dumps hold them too), but the brightest texels of a fully lit polygon (luma curve
    // 76 at shades 14-15: brightness 58-62) then took the marker red, which the cabinets do not show and the PC port's
    // own textures of the same models (white blades) do not have. Such a row's steps after the ramp keep its last colour
    auto rampEnd = [&](size_t p) {
        auto lum = [&](int l) { return 299 * maincpu_[p + size_t(l) * 3] + 587 * maincpu_[p + size_t(l) * 3 + 1] + 114 * maincpu_[p + size_t(l) * 3 + 2]; };
        for (int l = 48; l < 64; ++l) {
            const bool white = maincpu_[p + size_t(l - 1) * 3] >= 0xF0 && maincpu_[p + size_t(l - 1) * 3 + 1] >= 0xF0 &&
                               maincpu_[p + size_t(l - 1) * 3 + 2] >= 0xF0;
            if (white && lum(l) * 10 < lum(l - 1) * 6) return l;
        }
        return 64;
    };
    const size_t list = rd32(maincpu_, 0xA99A0 + size_t(n) * 4);
    for (int i = 0; i < 14; ++i) {
        const size_t p = rd32(maincpu_, list + size_t(i) * 4);
        const int end = rampEnd(p);
        for (int l = 0; l < 64; ++l)
            for (int ch = 0; ch < 3; ++ch) put(ch, 1 + 2 * i, l, int(float(maincpu_[p + size_t(std::min(l, end - 1)) * 3 + ch]) * k));
    }
    // single-row loads (maincpu 0x13F20: row library at 0xE211, 192 bytes per entry, copied unscaled)
    for (auto [row, entry] : rowSwaps) {
        const size_t p = 0xE211 + size_t(entry) * 192;
        if (row < 1 || row > 31 || p + 192 > maincpu_.size()) continue;
        const int end = rampEnd(p);
        for (int l = 0; l < 64; ++l)
            for (int ch = 0; ch < 3; ++ch) put(ch, row, l, maincpu_[p + size_t(std::min(l, end - 1)) * 3 + ch]);
    }
}

// Texture RAM as the game fills it for area block n (1-12), from the ROM alone (docs/formats.md §17), as the program
// uploads it: texture_ram_init (maincpu 0xD40) puts block 0 (main_data 0x000000) into RAM 0 at boot,
// texture_area_upload (0xEB0, table 0xA99E0: block n at main_data n x 0x100000) puts the area into RAM 1, then
// texture_area_extra_upload (0x5CA30) adds the area's tiles. Each block is 512 x 512 words of level 0 and, from
// +0x80000, a staircase of mip levels that alternates between the two RAMs (rect_copy_512, rows of 512 words); every
// level is in the ROM. Byte-exact against MAME's texture RAM for every game area (s1, 2026-10-16: the fullgame_us
// dumps, 0 differing words).
bool ArcadeSource::buildTextureRam(int n, std::vector<uint8_t>& out, std::string& err)
{
    if (n < 1 || n > 12) { err = "texture area block must be 1-12"; return false; }
    constexpr size_t kBlock = 0x100000, kTileTable = 0xC1610, kRam1 = 0x200000;
    out.assign(0x400000, 0);
    auto copy = [&](size_t dst, size_t src, size_t len) { std::memcpy(out.data() + dst, mainData_.data() + src, len); };
    // rect_copy_512(src, dst, x0, y0, x1, y1)
    auto rect = [&](size_t src, size_t dst, size_t x0, size_t y0, size_t x1, size_t y1) {
        for (size_t y = y0; y <= y1; ++y)
            copy(dst + (y * 512 + x0) * 2, src + (y * 512 + x0) * 2, (x1 + 1 - x0) * 2);
    };
    // one block: level 0 into RAM `a`, the mip staircase into a / b (texture_ram_init: a = RAM 0; the area: RAM 1)
    auto upload = [&](size_t src, size_t a, size_t b) {
        rect(src, a, 0, 0, 0x1FF, 0x1FF);
        const size_t s = src + 0x80000;
        rect(s, a + 0x80000, 0, 0, 0x1FF, 0xFF);
        rect(s, b + 0x80000, 0, 0x100, 0xFF, 0x1FF);
        rect(s, b + 0x80000, 0x100, 0x100, 0x1FF, 0x17F);
        rect(s, a + 0x80000, 0x100, 0x180, 0x17F, 0x1FF);
        rect(s, a + 0x80000, 0x180, 0x180, 0x1FF, 0x1BF);
        rect(s, b + 0x80000, 0x180, 0x1C0, 0x1BF, 0x1FF);
        rect(s, b + 0x80000, 0x1C0, 0x1C0, 0x1FF, 0x1DF);
        rect(s, a + 0x80000, 0x1C0, 0x1E0, 0x1DF, 0x1FF);
        rect(s, a + 0x80000, 0x1E0, 0x1E0, 0x1FF, 0x1EF);
        rect(s, b + 0x80000, 0x1E0, 0x1F0, 0x1FF, 0x1FF);
    };
    upload(0, 0, kRam1);
    upload(size_t(n) * kBlock, kRam1, 0);
    // 8 tiles of 128 x 128 texels (8 KB, 128 bytes per RAM row) into RAM 0's x 0-255, y 0-511, row-major 2 x 4;
    // tile addresses (CPU 0x02Exxxxx) per area block from maincpu 0xC1610
    for (int k = 0; k < 8; ++k) {
        const uint32_t addr = rd32(maincpu_, kTileTable + (size_t(n - 1) * 8 + k) * 4);
        if (addr == 0) continue;   // no tile in this slot (texture_area_extra_upload skips it)
        size_t src = addr - 0x02000000;
        if (src + 0x2000 > mainData_.size()) { err = "bad texture tile address"; return false; }
        for (int r = 0; r < 64; ++r) copy((size_t(k / 2) * 64 + r) * 0x400 + (k % 2) * 0x80, src + size_t(r) * 0x80, 0x80);
    }
    return true;
}

bool ArcadeSource::loadTextureSet(int stage, int setIndex, const std::string& tag, TextureSet& out, std::vector<uint16_t>& palette,
                                  std::string& err)
{
    // "<texture source>/<color tag>" or one tag for both. Texture source: "rom:<area block 1-12>" (rebuilt from the ROM,
    // docs/formats.md §17) or a MAME dump tag (<tag>_texram.bin).
    std::vector<uint8_t> texram, pal, xlat;
    const size_t slash = tag.find('/');
    const std::string texTag = tag.substr(0, slash), colorTag = slash == std::string::npos ? tag : tag.substr(slash + 1);
    std::string base = cfg_.dumpsDir + "/" + texTag, colors = cfg_.dumpsDir + "/" + colorTag;
    if (texTag.rfind("rom:", 0) == 0) {
        if (!buildTextureRam(std::atoi(texTag.c_str() + 4), texram, err)) return false;
    } else if (!readFile(base + "_texram.bin", texram, err)) {
        return false;
    }
    const int romArea = texTag.rfind("rom:", 0) == 0 ? std::atoi(texTag.c_str() + 4) : 0;
    if (const char* dump = std::getenv("HOTD_DUMP_TEXSETS"))   // debugging: <prefix><stage>_<set>_texram.bin
        std::ofstream(std::string(dump) + std::to_string(stage) + "_" + std::to_string(setIndex) + "_texram.bin", std::ios::binary)
            .write(reinterpret_cast<const char*>(texram.data()), std::streamsize(texram.size()));
    const bool romColors = romArea > 0 && cfg_.romColors;
    if (romColors) {
        // everything from the ROM: the dump files aren't needed. (Palette entries no record covers stay black.)
        pal.assign(0x4000, 0);
        xlat.assign(3 * 0x4000, 0);
    } else if (!readFile(colors + "_palette.bin", pal, err) || !readFile(colors + "_colorxlat.bin", xlat, err)) {
        return false;
    }
    if (texram.size() < 0x300000 || pal.size() < 0x2800 || xlat.size() < 3 * 0x4000) {
        err = "MAME dump " + base + " is too small";
        return false;
    }
    out.shading = Shading::Model2Indexed;
    out.romArea = romArea;
    out.label = "arcade, texture RAM " + texTag + (romColors ? ", colors from the ROM" : ", colors from MAME dump " + colorTag);
    out.width = kSheetW;
    out.height = kSheetH;
    out.layers.assign(2, std::vector<uint8_t>(size_t(kSheetW) * kSheetH));
    // Each texture RAM (sheet 0 at byte 0, sheet 1 at byte 0x200000) is 1024 x 2048 texels in RAM layout, one 16-bit word =
    // 2 x 2 texels; the rasterizer sees 2048 x 1024: x < 1024 from RAM rows 0-1023, x >= 1024 from rows 1024-2047.
    for (int s = 0; s < 2; ++s) {
        std::vector<uint8_t>& sheet = out.layers[s];
        size_t ram = size_t(s) * 0x200000;
        for (int wy = 0; wy < 1024; ++wy)
            for (int wx = 0; wx < 512; ++wx) {
                uint16_t w = rd16(texram, ram + (size_t(wy) * 512 + wx) * 2);
                int ry = wy * 2, rx = wx * 2;
                uint8_t t[4] = {uint8_t(w >> 12 & 15), uint8_t(w >> 8 & 15), uint8_t(w >> 4 & 15), uint8_t(w & 15)};
                for (int k = 0; k < 4; ++k) {
                    int y = ry + (k >> 1), x = rx + (k & 1);
                    int sx = y < 1024 ? x : x + 1024, sy = y < 1024 ? y : y - 1024;
                    sheet[size_t(sy) * kSheetW + sx] = t[k];
                }
            }
    }
    out.luma.resize(128 * 128);
    for (size_t i = 0; i < out.luma.size(); ++i)
        out.luma[i] = mainData_[kLumaTable + i];
    // color translation RAM: red, green, blue tables 0x4000 bytes apart, [base * 256 + brightness], low byte = output
    out.xlat.resize(3 * 32 * 256);
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 32 * 256; ++i)
            out.xlat[size_t(c) * 32 * 256 + i] = xlat[size_t(c) * 0x4000 + size_t(i) * 2];
    palette.resize(pal.size() / 2);
    for (size_t i = 0; i < palette.size(); ++i)
        palette[i] = rd16(pal, i * 2);
    if (romColors) {
        auto sw = rowSwaps_[stage].find(setIndex);
        buildColorXlat(romArea, sw != rowSwaps_[stage].end() ? sw->second : std::vector<std::pair<int, int>>{}, out.xlat);
    }
    if (romColors) {
        // polygon palette from the ROM (docs/formats.md §18): record 0 at entry 0, the area's record at entry 500; each record
        // is u16 count + count colors, pointers at maincpu 0xA9960.
        // Entries past the end of an area's record keep the previous areas' values, so the records of every area loaded
        // before this one along the route (earlier chapters, then this chapter's earlier sets) are applied first.
        constexpr size_t kPaletteRecords = 0xA9960;
        std::vector<std::pair<int, size_t>> loads = {{0, 0}, {2, 500}};   // area 2 = the attract demo, shown before every game
        for (int s = 0; s <= stage; ++s)
            for (size_t i = 0; i < stageTags_[s].size() && (s < stage || int(i) < setIndex); ++i)
                if (stageTags_[s][i].rfind("rom:", 0) == 0) loads.push_back({std::atoi(stageTags_[s][i].c_str() + 4), 500});
        loads.push_back({romArea, 500});
        for (auto [rec, at] : loads) {
            size_t p = rd32(maincpu_, kPaletteRecords + size_t(rec) * 4);
            size_t count = rd16(maincpu_, p);
            for (size_t i = 0; i < count && 0x1000 + at + i < palette.size(); ++i)
                palette[0x1000 + at + i] = rd16(maincpu_, p + 2 + 2 * i);
        }
        // effect colors (maincpu 0x24D70, called after every record load): record at 0xA35C8 written downward from entry 1023.
        // About 30 of these 100 entries are then animated by the script copied from 0xA35CC (not emulated: initial values).
        constexpr size_t kEffectRecord = 0xA35C8;
        const size_t effectCount = rd16(maincpu_, kEffectRecord);
        for (size_t i = 0; i < effectCount && i < 1024; ++i)
            palette[0x1000 + 1023 - i] = rd16(maincpu_, kEffectRecord + 2 + 2 * i);
        // entry 211: the routine at maincpu 0x24E90 stores a constant there (the immediate of its `lda` at 0x24F18)
        palette[0x1000 + 211] = uint16_t(rd32(maincpu_, 0x24F1C) & 0xFFFF);
        // Entries 924-1023 from the set's dump, if there is one (starting values; the animated ones, 959 and 984-1021, are
        // replaced every frame by ArcadePaletteFx, formats.md §24). Whether the ROM alone gives the rest: not checked yet.
        std::vector<uint8_t> dumpPal;
        std::string ignored;
        if (slash != std::string::npos && readFile(colors + "_palette.bin", dumpPal, ignored) && dumpPal.size() >= 0x2800) {
            for (size_t i = 924; i < 1024; ++i) palette[0x1000 + i] = rd16(dumpPal, (0x1000 + i) * 2);
        }
    }
    // the polygons' base colours (perf: looked up per set by the renderer; buildMeshes reads the same entries)
    out.basePalette.resize(1024);
    for (size_t i = 0; i < 1024; ++i)
        out.basePalette[i] = palette[(0x1000 + i) % palette.size()];
    return true;
}

std::unique_ptr<StageData> ArcadeSource::loadStage(int stage, std::string& err)
{
    if (!available_) { err = status_; return nullptr; }
    if (stage < 0 || stage >= 4) { err = "arcade: no placement table for stage " + std::to_string(stage); return nullptr; }
    if (!loadRegions(err)) return nullptr;
    for (int s = 0; s < 4; ++s)
        if (stageTags_[s].empty()) stageTags_[s] = romStageTags(s);

    auto out = std::make_unique<StageData>();
    out->source = Source::Arcade;
    out->stage = stage;
    palettes_[stage].clear();
    for (const std::string& tag : stageTags_[stage]) {
        TextureSet ts;
        std::vector<uint16_t> pal;
        if (!loadTextureSet(stage, int(out->textureSets.size()), tag, ts, pal, err)) return nullptr;
        if (cfg_.texsetFile.empty()) {
            // no texset file: the lighting from the ROM. The 32 lighting parameters (diffuse, ambient) are the table
            // at maincpu 0x7A0 (8 bytes an entry: parameter word, distance) that hw_init_3d and the draw routines send
            // with geo_send_words(0, 0x1F, 0x7A0): entries 0-30; entry 31, which that call leaves out, is (0, 255) as
            // in MAME's display lists. The world's light: world_draw_task (0x4B300) builds it with matrix_identity_set
            // from the zone's angles (table 0xC0BF0 -> 0xC0C10: 0xC000 / 0xE000 in every Chapter 1 zone, a light
            // straight down), (0, -1, 0) in world space as the display lists show for every chapter.
            Light& l = lights_[stage][int(out->textureSets.size())];
            l.known = true;
            l.dir[0] = 0.0f;
            l.dir[1] = -1.0f;
            l.dir[2] = 0.0f;
            for (int i = 0; i < 32; ++i) {
                const uint32_t w = i < 31 ? rd32(maincpu_, 0x7A0 + size_t(i) * 8) : 0xFF00u;
                l.diffuse[i] = float(w & 0xFF);
                l.ambient[i] = float((w >> 8) & 0xFF);
                l.specScale[i] = float((w >> 16) & 0xFF);   // entries 12 / 13 (and 28 / 29): 255, control 1 / 3
                l.specCtl[i] = float(w >> 24);
            }
        }
        auto li = lights_[stage].find(int(out->textureSets.size()));
        if (li != lights_[stage].end() && li->second.known) {
            ts.lightKnown = true;
            std::copy_n(li->second.dir, 3, ts.lightDir);
            std::copy_n(li->second.diffuse, 32, ts.diffuse);
            std::copy_n(li->second.ambient, 32, ts.ambient);
            std::copy_n(li->second.specScale, 32, ts.specScale);
            std::copy_n(li->second.specCtl, 32, ts.specCtl);
        }
        if (const char* dump = std::getenv("HOTD_DUMP_TEXSETS")) {   // debugging: <prefix><stage>_<set>_{palette,xlat,luma}.bin
            const std::string pre = std::string(dump) + std::to_string(stage) + "_" + std::to_string(out->textureSets.size());
            std::ofstream(pre + "_palette.bin", std::ios::binary).write(reinterpret_cast<const char*>(pal.data()), std::streamsize(pal.size() * 2));
            std::ofstream(pre + "_xlat.bin", std::ios::binary).write(reinterpret_cast<const char*>(ts.xlat.data()), std::streamsize(ts.xlat.size()));
            std::ofstream(pre + "_luma.bin", std::ios::binary).write(reinterpret_cast<const char*>(ts.luma.data()), std::streamsize(ts.luma.size()));
        }
        out->textureSets.push_back(std::move(ts));
        palettes_[stage].push_back(std::move(pal));
    }
    if (cfg_.texsetFile.empty() && cfg_.defaultSkies) {
        // no texset file (the packaged viewer): the sky models per texture area as the texset file has them
        // (measured in MAME: the sky drawn around the camera in each outdoor area), by area instead of set index.
        // Without them the viewer drew no sky at all (viewer fix, 2026-10-20); the game draws its own (sky_task).
        struct DefaultSky { int stage, area; uint32_t id; float y; };
        static const DefaultSky kSkies[] = {
            {0, 1, 0x608, -9}, {0, 1, 0xA29, -9}, {1, 5, 0x608, -9}, {1, 5, 0x1238, -9}, {1, 6, 0x608, -200},
            {1, 6, 0x137C, -200}, {1, 7, 0x608, -9}, {1, 7, 0x14B9, -9}, {3, 12, 0x608, -9}, {3, 12, 0x1D20, -9},
        };
        skies_[stage].clear();
        for (const DefaultSky& d : kSkies)
            for (size_t i = 0; i < out->textureSets.size(); ++i)
                if (d.stage == stage && out->textureSets[i].romArea == d.area) {
                    StageData::Sky sky{};
                    sky.modelKey = d.id;
                    sky.offset[1] = d.y;
                    skies_[stage][int(i)].push_back(sky);
                }
    }

    // draw zones (with them, the zone decides between alternate states, so none is dropped below)
    const size_t zl = rd32(maincpu_, kZoneListPtrs + 4 * size_t(stage));
    for (int z = 0; z < kMaxZones && zl + size_t(z + 1) * kZoneListSize <= maincpu_.size(); ++z) {
        std::vector<int> list;
        for (int k = 0; k < kZoneListSize && maincpu_[zl + size_t(z) * kZoneListSize + size_t(k)] != 0xFF; ++k)
            list.push_back(maincpu_[zl + size_t(z) * kZoneListSize + size_t(k)]);
        out->zoneLists.push_back(std::move(list));
    }
    // placements (24 bytes: id, x, y, z, flag, pointer; ends with 0xFFFFFFFF)
    int index = 0;
    for (size_t p = kPlacementTables[stage]; rd32(maincpu_, p) != 0xFFFFFFFF; p += 24, ++index) {
        Placement pl;
        pl.romIndex = index;
        pl.id = pl.modelKey = rd32(maincpu_, p);
        pl.pos[0] = rdf(maincpu_, p + 4);
        pl.pos[1] = rdf(maincpu_, p + 8);
        pl.pos[2] = rdf(maincpu_, p + 12);
        // world_draw_task (0x4B300) turns three placements after their translation (copro 0x12): Chapter 1 indices
        // 0x37 / 0x47 by copro 0x15 (rotate Y) 0x4000, Chapter 2 index 0x1B by copro 0x16 (rotate Z) 0x4000. 90 degrees
        // as Tgp::rot turns the matrix rows: 0x15 (x, y, z) -> (x c - z s, y, x s + z c), 0x16 -> (x c + y s, y c - x s, z).
        // Without it Chapter 1's room5a_CT model list (the corridor's windows before the Chariot room, 0xCE8 / 0xD18..)
        // stood across the corridor instead of on its right wall (user play-test 2026-10-16).
        if ((stage == 0 && (index == 0x37 || index == 0x47)) || (stage == 1 && index == 0x1B)) {
            const float r0[9] = {0, 0, -1, 0, 1, 0, 1, 0, 0}, r1[9] = {0, 1, 0, -1, 0, 0, 0, 0, 1};
            std::copy_n(stage == 0 ? r0 : r1, 9, pl.rot);
        }
        bool alternate = false;
        if (!cfg_.keepAlternates && out->zoneLists.empty())
            for (uint32_t a : kAlternateStates) alternate |= pl.id == a;
        // sky models can also be in the placement table (e.g. 0x14B9 in Chapter 2); the game only draws them around the camera
        for (const auto& [set, list] : skies_[stage])
            for (const StageData::Sky& sky : list) alternate |= pl.id == sky.modelKey;
        if (!alternate) out->placements.push_back(pl);
    }

    const size_t polyWords = polygons_.size() / 4;
    // models: the placed ones and the sky models (drawn around the camera, not placed)
    std::vector<uint32_t> keys;
    for (const Placement& pl : out->placements) keys.push_back(pl.modelKey);
    if (auto req = requested_.find(stage); req != requested_.end()) keys.insert(keys.end(), req->second.begin(), req->second.end());
    for (const auto& [set, list] : skies_[stage])
        for (const StageData::Sky& sky : list) keys.push_back(sky.modelKey);
    for (uint32_t key : keys) {
        if (out->models.count(key)) continue;
        if (kModelTable + size_t(key) * 16 + 16 > mainData_.size()) continue;   // (a generated model's key: kGunOnlyKey)
        Model& model = out->models[key];
        model.key = key;
        if (key < names_.size()) model.name = names_[key];
        size_t rec = kModelTable + size_t(key) * 16;
        uint32_t tpa = rd32(mainData_, rec), tha = rd32(mainData_, rec + 4), obj = rd32(mainData_, rec + 8);
        uint32_t links = rd32(mainData_, rec + 12);
        if (!(obj & 0x800000)) continue;
        size_t addr = obj & 0x7FFFFF;
        if (addr + 7 > polyWords) continue;

        auto setIt = modelSet_[stage].find(key);
        model.preferredVariant =
            setIt != modelSet_[stage].end() && setIt->second < int(out->textureSets.size()) ? setIt->second : 0;
        if (auto vi = modelValid_[stage].find(key); vi != modelValid_[stage].end()) model.validVariants = vi->second;
        buildMeshes(stage, polygons_, addr, tpa, tha, links, model);
        model.computeBounds();
    }
    // generated skins (decompilation.md "World drawing"): template models, the caller moves their corners (rebuildSkin)
    if (auto req = skinRequests_.find(stage); req != skinRequests_.end())
        for (const SkinRequest& r : req->second) {
            Skin sk;
            if (!skinInfo(r.type, sk, r.which)) continue;
            Model& model = out->models[r.key];
            model.key = r.key;
            model.name = "skin " + std::to_string(sk.skin);
            model.preferredVariant = 0;
            buildMeshes(stage, sk.words, 0, sk.texPoint, sk.texHeader, uint32_t(sk.polygons), model);
            model.computeBounds();
        }
    // rellook (user 2026-10-30, the zombie Sophie in Rogan's part: her gun hand "looks healthy"): the players' gun hand
    // 0x719 `ter00a` without its hand, as kGunOnlyKey: its polygons on the texture rectangles of the players' empty hand
    // 0x36B `kenter00a` (the skin and the cuff) left out, the gun kept. The cut-scene viewer's zombie Sophie draws it with
    // her own hand (Runtime::actor_gun_model)
    if (auto g = out->models.find(kPlayerGunHand); g != out->models.end() && !out->models.count(kGunOnlyKey)) {
        std::set<std::array<uint16_t, 5>> skin;   // the empty hand's rectangles (x, y, w, h, sheet)
        const size_t rec = kModelTable + size_t(kPlayerEmptyHand) * 16;
        const uint32_t obj = rd32(mainData_, rec + 8);
        if ((obj & 0x800000) && (obj & 0x7FFFFF) + 7 <= polyWords) {
            Model empty;
            empty.preferredVariant = g->second.preferredVariant;
            buildMeshes(stage, polygons_, obj & 0x7FFFFF, rd32(mainData_, rec), rd32(mainData_, rec + 4), rd32(mainData_, rec + 12), empty);
            for (const Mesh& m : empty.meshes)
                for (const Vertex& v : m.vertices)
                    skin.insert({v.rect[0], v.rect[1], v.rect[2], v.rect[3], uint16_t(v.layer & 1)});
        }
        if (!skin.empty()) {
            Model gun = g->second;
            gun.key = kGunOnlyKey;
            gun.name += " gun only";
            for (Mesh& m : gun.meshes) {
                std::vector<Vertex> kept;
                std::vector<Mesh::SortKey> gunKeys;
                std::vector<uint32_t> newTri(m.vertices.size() / 3, UINT32_MAX);
                for (size_t t = 0; t + 2 < m.vertices.size(); t += 3) {
                    const Vertex& v = m.vertices[t];
                    if (skin.count({v.rect[0], v.rect[1], v.rect[2], v.rect[3], uint16_t(v.layer & 1)}))
                        continue;
                    newTri[t / 3] = uint32_t(kept.size() / 3);
                    kept.insert(kept.end(), m.vertices.begin() + ptrdiff_t(t), m.vertices.begin() + ptrdiff_t(t + 3));
                }
                for (Mesh::SortKey k : m.sortKeys)
                    if (k.tri < newTri.size() && newTri[k.tri] != UINT32_MAX)
                        k.tri = newTri[k.tri], gunKeys.push_back(k);
                m.vertices = std::move(kept);
                m.sortKeys = std::move(gunKeys);
            }
            gun.computeBounds();
            if (gun.triangleCount() > 0)
                out->models[kGunOnlyKey] = std::move(gun);
        }
    }
    out->zones = zones_[stage];
    for (const auto& [set, list] : skies_[stage])
        if (set < int(out->textureSets.size()))
            for (StageData::Sky sky : list) {
                sky.variant = set;
                out->skies.push_back(sky);
            }
    return out;
}

bool ArcadeSource::skinInfo(int type, Skin& out, int which)
{
    std::string err;
    if (type < 0 || type >= 100 || !available_ || !loadRegions(err)) return false;
    auto role = [&](uint8_t r) {   // the last node of the type with this role byte (later draws overwrite the matrix)
        int found = -1;
        for (int n = 0; n < 31; ++n)
            if (maincpu_[kSkinNodeRole + size_t(type) * 0xF8 + size_t(n) * 8] == r) found = n;
        return found;
    };
    if (which == 1) {
        // Sophie's strip (sophie_skin_draw, type 34): template maincpu 0x21640 (2 polygons), corners 0x21770 follow the
        // node with role 14 (+0x2A4), drawn with role 13 (+0x274). Her second strip (0x216BC, 4 polygons) is written to
        // polygon RAM too, but the display list draws only this one (the walk stops at this strip's end marker).
        if (type != 34) return false;
        out.skin = 100;
        out.polygons = 2;
        out.texPoint = 0x80182;
        out.texHeader = 0x8019A;
        out.nodeA = role(14);
        out.nodeB = role(13);
        if (out.nodeA < 0 || out.nodeB < 0) return false;
        out.words.assign(maincpu_.begin() + 0x21640, maincpu_.begin() + 0x21640 + 27 * 4);
        out.corners.clear();
        out.vertex.clear();
        for (int i = 0; i < 2; ++i) {
            const V3 c = rdv(maincpu_, 0x21770 / 4 + size_t(i) * 3);
            out.corners.push_back({c.x, c.y, c.z});
            out.vertex.push_back(int32_t(rd32(maincpu_, 0x217C0 + 4 * size_t(rd32(maincpu_, 0x217E8 + 4 * size_t(i))))));
        }
        out.sharedVertex = out.sharedCopy = -2;
        return true;
    }
    const int skin = int16_t(rd16(maincpu_, kSkinByType + 2 * size_t(type)));
    if (skin < 0 || skin >= kSkinCount) return false;
    out.skin = skin;
    out.polygons = int(rd32(mainData_, kSkinPolygonCount + 4 * size_t(skin)));
    out.texPoint = rd32(maincpu_, kSkinHeader + 8 * size_t(type));
    out.texHeader = rd32(maincpu_, kSkinHeader + 8 * size_t(type) + 4);
    if (out.texPoint == 0xFFFFFFFF || out.polygons <= 0 || out.polygons > 30) return false;
    // the part-draw routines save the matrices by this byte per type and node (8: corners, 9: the skin itself)
    out.nodeA = role(8);
    out.nodeB = role(9);
    if (out.nodeA < 0 || out.nodeB < 0) return false;
    const size_t tmpl = kSkinTemplates + size_t(skin) * 0x640;
    out.words.assign(mainData_.begin() + ptrdiff_t(tmpl), mainData_.begin() + ptrdiff_t(tmpl + (size_t(out.polygons) * 10 + 7) * 4));
    out.corners.clear();
    out.vertex.clear();
    for (int i = 0; i < out.polygons / 2; ++i) {
        const V3 c = rdv(mainData_, (kSkinCorners + size_t(skin) * 0x90) / 4 + size_t(i) * 3);
        out.corners.push_back({c.x, c.y, c.z});
        const int m = int(rd32(mainData_, kSkinCornerMap + size_t(skin) * 0x30 + size_t(i) * 4));
        out.vertex.push_back(int(rd32(mainData_, kSkinVertexMap + size_t(skin) * 0x60 + size_t(m) * 4)));
    }
    out.sharedVertex = int(rd32(mainData_, kSkinShared + size_t(skin) * 8));
    out.sharedCopy = int(rd32(mainData_, kSkinShared + size_t(skin) * 8 + 4));
    return true;
}

void ArcadeSource::rebuildSkin(int stage, const Skin& sk, const std::vector<std::array<float, 3>>& moved, Model& model) const
{
    std::vector<uint8_t> words = sk.words;
    auto put = [&](int vertex, const std::array<float, 3>& p) {   // vertex k: word 10k of the model (0 = p0, -1 = p1)
        std::memcpy(&words[vertex < 0 ? 12 : size_t(vertex) * 40], p.data(), 12);
    };
    for (size_t i = 0; i < moved.size() && i < sk.vertex.size(); ++i) {
        put(sk.vertex[i], moved[i]);
        if (sk.sharedVertex >= 0 && sk.vertex[i] == sk.sharedVertex) {   // skin_build: a shared corner is also written to p1 or another vertex
            if (sk.sharedCopy == -1) std::memcpy(&words[12], moved[i].data(), 12);
            else put(sk.sharedCopy, moved[i]);
        }
    }
    const int preferred = model.preferredVariant;
    model.meshes.clear();
    buildMeshes(stage, words, 0, sk.texPoint, sk.texHeader, uint32_t(sk.polygons), model);
    model.preferredVariant = preferred;
}

void ArcadeSource::buildMeshes(int stage, const std::vector<uint8_t>& poly, size_t addr, uint32_t tpa, uint32_t tha, uint32_t links,
                               Model& model) const
{
        const size_t polyWords = poly.size() / 4;
        if (addr + 7 > polyWords) return;
        // one mesh for every texture set (perf, 2026-10-16; was one copy per set, ~1 GB in Chapter 1): the renderer takes
        // the base colours from the drawn set's palette (TextureSet::basePalette by Vertex::palette); Vertex::color holds
        // the preferred set's (debugging lists)
        if (palettes_[stage].empty()) return;
        {
        const int set = std::clamp(model.preferredVariant, 0, int(palettes_[stage].size()) - 1);
        const std::vector<uint16_t>& palette = palettes_[stage][size_t(set)];
        Mesh mesh;
        mesh.textureSet = -1;
        mesh.variant = -1;

        const bool untexturedModel = tpa == tha;
        size_t tp = tpa, th = tha;
        V3 p0 = rdv(poly, addr), p1 = rdv(poly, addr + 3);
        size_t q = addr + 6;
        // Draw Depth "Arcade Sort" (Mesh::sortKeys): the key polygon of z-sort mode 0, the last polygon (hidden ones
        // too) with its own mode (MAME model2_3d_process_polygon: raster->polygon_z)
        Mesh::SortKey key{0, {}, kSortFar};
        for (uint32_t n = 0; n <= links + 1 && q < polyWords; ++n) {
            uint32_t attr = rd32(poly, q * 4);
            if ((attr & 3) == 0) break;
            if (q + 10 > polyWords) break;
            V3 nrm = rdv(poly, q + 1), pa = rdv(poly, q + 4), pb = rdv(poly, q + 7);
            const bool quad = attr & 1;
            const int link = (attr >> 8) & 3;
            const int nv = quad ? 4 : 3;
            const uint8_t sortMode = uint8_t((attr >> 10) & 3);
            if (sortMode != 0) {
                const V3 kp[4] = {p1, p0, pa, quad ? pb : pa};
                for (int i = 0; i < 4; ++i) key.p[i][0] = kp[i].x, key.p[i][1] = kp[i].y, key.p[i][2] = kp[i].z;
                key.mode = sortMode;
            }
            if (link != 0) {   // link type 0 = hidden polygon: only moves the edge
                V3 pv[4] = {p1, p0, pa, pb};
                uint16_t uvw[8] = {};
                if (!untexturedModel)
                    for (int i = 0; i < 2 * nv; ++i) uvw[i] = rd16(textures_, (tp + i) * 2);
                uint16_t h[4];
                for (int i = 0; i < 4; ++i) h[i] = rd16(textures_, (th + i) * 2);
                Vertex base{};
                base.normal[0] = nrm.x; base.normal[1] = nrm.y; base.normal[2] = nrm.z;
                const bool textured = !untexturedModel && (h[0] >> 14 & 1);
                base.rect[0] = uint16_t(32 * (h[2] & 0x3F));
                base.rect[1] = uint16_t(32 * ((h[2] >> 6) & 0x1F));
                base.rect[2] = uint16_t(32 << (h[0] & 7));
                base.rect[3] = uint16_t(32 << ((h[0] >> 3) & 7));
                base.layer = uint16_t((h[2] >> 12) & 1);
                // microtexture (MAME model2_3d_render; polish2): header 0 bit 12 on, bits 10-11 its minimum level; header 2
                // bit 13 its x / 128, bits 14-15 its y / 128 (on the other sheet); for the shader in layer bits 8-13
                if (h[0] >> 12 & 1)
                    base.layer = uint16_t(base.layer | 0x100 | ((h[0] >> 10 & 3) << 9) | ((h[2] >> 13 & 1) << 11) | ((h[2] >> 14 & 3) << 12));
                base.flags = uint16_t((textured ? kTextured : 0) | (h[0] >> 8 & 1 ? kMirrorU : 0) | (h[0] >> 9 & 1 ? kMirrorV : 0) |
                                      (h[0] >> 13 & 1 ? kTranslucent : 0) | (h[0] >> 15 & 1 ? kChecker : 0) |
                                      (attr >> 17 & 1 ? kDoubleSided : 0) | (((attr >> 18) & 0x1F) << kLightParamShift));
                base.palette = uint16_t((h[3] >> 6) & 0x3FF);
                base.lodCoef = uint16_t(attr >> 27);
                uint16_t col = palette[(0x1000 + base.palette) % palette.size()] & 0x7FFF;
                base.color[0] = uint8_t(col & 31);
                base.color[1] = uint8_t(col >> 5 & 31);
                base.color[2] = uint8_t(col >> 10 & 31);
                base.color[3] = uint8_t(h[1] & 0xFF);
                Vertex v[4];
                for (int i = 0; i < nv; ++i) {
                    v[i] = base;
                    v[i].pos[0] = pv[i].x; v[i].pos[1] = pv[i].y; v[i].pos[2] = pv[i].z;
                    v[i].uv[0] = uvw[2 * i + 1] / 8.0f;   // texture point: v, u per vertex, 13.3 fixed point
                    v[i].uv[1] = uvw[2 * i] / 8.0f;
                }
                // a polygon showing (part of) its texture once, not repeated: at its edges the bilinear filter would blend
                // in the texels of the texture's other side (Chapter 2: the moon quad 0x14AC's top edge took the moon's
                // middle and drew a thin bright line across the sky; MAME draws none)
                bool inside = textured;
                for (int i = 0; i < nv && inside; ++i)
                    inside = v[i].uv[0] >= 0.0f && v[i].uv[1] >= 0.0f && v[i].uv[0] <= base.rect[2] && v[i].uv[1] <= base.rect[3];
                if (inside)
                    for (int i = 0; i < nv; ++i) v[i].flags = uint16_t(v[i].flags | kInsideRect);
                const int tri[2][3] = {{0, 1, 2}, {0, 2, 3}};
                for (int t = 0; t < (quad ? 2 : 1); ++t) {
                    if (sortMode == 0) {
                        key.tri = uint32_t(mesh.vertices.size() / 3);
                        mesh.sortKeys.push_back(key);
                    }
                    const uint16_t sb = uint16_t((sortMode << kSortShift) | (quad ? (t == 0 ? kSortQuadFirst : kSortQuadSecond) : 0));
                    for (int k = 0; k < 3; ++k) {
                        mesh.vertices.push_back(v[tri[t][k]]);
                        mesh.vertices.back().layer = uint16_t(mesh.vertices.back().layer | sb);
                    }
                }
            }
            if (!untexturedModel) tp += 2 * size_t(nv);
            int step = (attr >> 12) & 0x1F;
            th += size_t(ptrdiff_t(step & 0x10 ? step - 32 : step) * 4);
            if (!quad) pb = pa;
            if (link == 0 || link == 2) { p0 = pa; p1 = pb; }
            else if (link == 1) p1 = pa;
            else p0 = pb;
            q += 10;
        }
        model.meshes.push_back(std::move(mesh));
        }
}

} // namespace hotd
