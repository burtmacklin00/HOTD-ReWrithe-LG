#include "assets/pc_loader.h"

#include "assets/archive.h"
#include "assets/pc_files.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>

namespace hotd {

namespace {

// a PC file: the one the launcher's files check found (renamed / moved files too), else <install>/<rel>
std::string pcPath(const std::string& install, const std::string& rel)
{
    const std::string m = pcFileMapped(rel);
    return m.empty() ? install + "/" + rel : m;
}

uint32_t rd32(const std::vector<uint8_t>& b, size_t off)
{
    uint32_t v;
    std::memcpy(&v, b.data() + off, 4);
    return v;
}
uint16_t rd16(const std::vector<uint8_t>& b, size_t off)
{
    return static_cast<uint16_t>(b[off] | (b[off + 1] << 8));
}
float rdf(const std::vector<uint8_t>& b, size_t off)
{
    float v;
    std::memcpy(&v, b.data() + off, 4);
    return v;
}

// The PC gave the second placement of 0xA27 its own object ID (docs/formats.md §12). Canonical ID = the arcade's.
uint32_t canonicalId(uint32_t pcId)
{
    return pcId == 0x1D4B ? 0xA27 : pcId;
}

constexpr int kPage = 256;

// B_<colour>.VMC's PACK chunks (THOTD.EXE 0x410E68 unpacking with 0x410D3C / 0x410818; packed by 0x4109F4): the BODY's
// bytes in blocks of u16 raw length (0 ends the chunk), u16 packed length, then tokens: a signed varint n (first byte:
// bit 7 more, bit 6 sign, 6 bits; then 7 bits a byte) - n > 0: n literal bytes; n < 0: a varint offset, -n bytes
// copied from that offset of the block's output (overlapping copies byte by byte). maxBlocks limits how many blocks are
// read (the page header is in the first). Returns false on malformed data.
bool unpackVmcChunk(std::istream& f, uint32_t chunkSize, std::vector<uint8_t>& out, int maxBlocks)
{
    out.clear();
    std::vector<uint8_t> in;
    uint32_t used = 0;
    for (int blocks = 0; blocks < maxBlocks; ++blocks) {
        uint8_t hdr[2];
        if (used + 2 > chunkSize || !f.read(reinterpret_cast<char*>(hdr), 2)) return false;
        used += 2;
        const uint32_t raw = uint32_t(hdr[0] | hdr[1] << 8);
        if (raw == 0) return true;
        if (used + 2 > chunkSize || !f.read(reinterpret_cast<char*>(hdr), 2)) return false;
        used += 2;
        const uint32_t packed = uint32_t(hdr[0] | hdr[1] << 8);
        if (used + packed > chunkSize) return false;
        in.resize(packed);
        if (!f.read(reinterpret_cast<char*>(in.data()), std::streamsize(packed))) return false;
        used += packed;
        size_t p = 0;
        auto varint = [&](int32_t& v) {
            if (p >= in.size()) return false;
            uint8_t c = in[p++];
            const bool neg = c & 0x40;
            uint32_t u = c & 0x3F;
            for (int sh = 6; c & 0x80; sh += 7) {
                if (p >= in.size() || sh > 27) return false;
                c = in[p++];
                u |= uint32_t(c & 0x7F) << sh;
            }
            v = neg ? -int32_t(u) : int32_t(u);
            return true;
        };
        const size_t start = out.size();
        while (out.size() - start < raw) {
            int32_t n, off;
            if (!varint(n)) return false;
            if (n < 0) {
                if (!varint(off) || off < 0 || start + size_t(off) >= out.size()) return false;
                for (int32_t i = 0; i < -n; ++i) out.push_back(out[start + size_t(off) + size_t(i)]);
            } else {
                if (p + size_t(n) > in.size()) return false;
                out.insert(out.end(), in.begin() + ptrdiff_t(p), in.begin() + ptrdiff_t(p + size_t(n)));
                p += size_t(n);
            }
        }
        if (out.size() - start != raw || p != in.size()) return false;
    }
    return true;
}

// Reads the given VMC pages (by page number) as RGBA8 (BODY chunks, or PACK ones: B_<colour>.VMC). Missing pages stay
// magenta.
// tbl / pal (optional): the stage's colour table STAGEn.TBL (65536 bytes: palette index by 16-bit texel) and palette
// STAGEn.PAL (256 x RGBx); see PcSource::loadStage
bool readVmcPages(const std::string& path, const std::vector<uint16_t>& pages, std::vector<std::vector<uint8_t>>& out,
                  std::string& err, const std::vector<uint8_t>* tbl = nullptr, const std::vector<uint8_t>* pal = nullptr,
                  bool missingCleared = false)
{
    const std::unique_ptr<std::istream> stream = pcfs::openStream(path);
    if (!stream) { err = "can't open " + path; return false; }
    std::istream& f = *stream;
    char hdr[12];
    f.read(hdr, 12);
    if (!f || std::memcmp(hdr, "PIFF", 4) != 0 || std::memcmp(hdr + 8, "VVTP", 4) != 0) { err = path + ": not a VVTP file"; return false; }
    out.assign(pages.size(), std::vector<uint8_t>(size_t(kPage) * kPage * 4, 0));
    for (auto& p : out)
        for (size_t i = 0; i < p.size(); i += 4) { p[i] = 255; p[i + 2] = 255; p[i + 3] = 255; }
    std::vector<uint8_t> body, packedBody;
    std::vector<bool> seen(pages.size(), false);
    const int shift = pcVmcPageShift(path);   // pchotfix: the Japanese release's numbering
    for (;;) {
        char ch[8];
        f.read(ch, 8);
        if (!f) break;
        uint32_t size;
        std::memcpy(&size, ch + 4, 4);
        const bool packed = std::memcmp(ch, "PACK", 4) == 0;
        if (size > (1u << 24))
            break;   // pchotfix: no chunk is that big: a damaged file ends here (the pages read so far are kept)
        if ((std::memcmp(ch, "BODY", 4) != 0 && !packed) || size < 64) { f.seekg(size, std::ios::cur); continue; }
        uint8_t ph[64];
        const std::streampos chunkStart = f.tellg();
        if (packed) {
            // the page header is in the first block: unpack it alone, the whole chunk only for a wanted page
            if (!unpackVmcChunk(f, size, packedBody, 1) || packedBody.size() < 64) { err = path + ": bad PACK chunk"; return false; }
            std::memcpy(ph, packedBody.data(), 64);
        } else {
            f.read(reinterpret_cast<char*>(ph), 64);
        }
        uint32_t number;
        uint16_t w, h;
        std::memcpy(&number, ph + 4, 4);
        std::memcpy(&w, ph + 8, 2);
        std::memcpy(&h, ph + 10, 2);
        if (number > 0 && number < 512)
            number += uint32_t(shift);
        int slot = -1;
        for (size_t i = 0; i < pages.size(); ++i)
            if (pages[i] == number) slot = int(i);
        // pchotfix: a chunk too small for its page (a damaged / cut file) is passed over, not read past its end
        if (slot < 0 || w != kPage || h != kPage || (!packed && size < 64 + uint32_t(kPage) * kPage * 2)) {
            f.seekg(chunkStart + std::streamoff(size));
            continue;
        }
        if (packed) {
            f.seekg(chunkStart);
            if (!unpackVmcChunk(f, size, packedBody, 1 << 20) || packedBody.size() < 64 + size_t(kPage) * kPage * 2) {
                err = path + ": bad PACK chunk";
                return false;
            }
            f.seekg(chunkStart + std::streamoff(size));
            body.assign(packedBody.begin() + 64, packedBody.end());
            size = uint32_t(packedBody.size());
        } else {
            body.resize(size - 64);
            f.read(reinterpret_cast<char*>(body.data()), std::streamsize(body.size()));
        }
        std::vector<uint8_t>& px = out[slot];
        for (int i = 0; i < kPage * kPage; ++i) {
            uint16_t c = rd16(body, size_t(i) * 2);
            auto x5 = [](int v) { return uint8_t((v << 3) | (v >> 2)); };
            if (tbl && pal) {
                const uint8_t* e = pal->data() + size_t((*tbl)[c]) * 4;
                px[i * 4 + 0] = e[0];
                px[i * 4 + 1] = e[1];
                px[i * 4 + 2] = e[2];
            } else {
                px[i * 4 + 0] = x5(c >> 10 & 31);
                px[i * 4 + 1] = x5(c >> 5 & 31);
                px[i * 4 + 2] = x5(c & 31);
            }
            // world pages use magenta (0x7C1F) as a color key; other pages use the opaque bit
            px[i * 4 + 3] = ((c & 0x8000) && (c & 0x7FFF) != 0x7C1F) ? 255 : 0;
        }
        // give transparent texels the color of an opaque neighbor so bilinear filtering doesn't bleed the key color
        for (int pass = 0; pass < 1; ++pass)
            for (int y = 0; y < kPage; ++y)
                for (int x = 0; x < kPage; ++x) {
                    uint8_t* t = &px[size_t(y * kPage + x) * 4];
                    if (t[3]) continue;
                    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                    bool found = false;
                    for (int k = 0; k < 4 && !found; ++k) {
                        int nx = (x + dx[k]) & (kPage - 1), ny = (y + dy[k]) & (kPage - 1);
                        const uint8_t* n = &px[size_t(ny * kPage + nx) * 4];
                        if (n[3]) { t[0] = n[0]; t[1] = n[1]; t[2] = n[2]; found = true; }
                    }
                    if (!found) { t[0] = 0; t[1] = 0; t[2] = 0; }
                }
        seen[size_t(slot)] = true;
    }
    for (size_t i = 0; i < pages.size(); ++i)
        if (!seen[i] && missingCleared)
            out[i].clear();   // display2: the caller keeps its own page
        else if (!seen[i]) std::fprintf(stderr, "PC texture gap: page %u is not in %s (drawn magenta)\n", unsigned(pages[i]), path.c_str());
    return true;
}

} // namespace

int pcVmcPageShift(const std::string& path)
{
    static std::mutex lock;
    static std::map<std::string, int> known;
    const std::string key = path + "|" + std::to_string(pcfs::size(path));
    {
        std::lock_guard<std::mutex> g(lock);
        if (const auto it = known.find(key); it != known.end())
            return it->second;
    }
    int shift = 0;
    if (const std::unique_ptr<std::istream> stream = pcfs::openStream(path)) {
        std::istream& f = *stream;
        char hdr[12];
        if (f.read(hdr, 12) && std::memcmp(hdr, "PIFF", 4) == 0 && std::memcmp(hdr + 8, "VVTP", 4) == 0) {
            std::set<uint32_t> low;   // the page numbers below 512
            std::vector<uint8_t> buf;
            for (;;) {
                char ch[8];
                if (!f.read(ch, 8))
                    break;
                uint32_t size;
                std::memcpy(&size, ch + 4, 4);
                if (size > (1u << 24))
                    break;
                const std::streampos start = f.tellg();
                const bool packed = std::memcmp(ch, "PACK", 4) == 0;
                if ((packed || std::memcmp(ch, "BODY", 4) == 0) && size >= 64) {
                    uint8_t ph[64];
                    if (packed) {
                        if (!unpackVmcChunk(f, size, buf, 1) || buf.size() < 64)
                            break;
                        std::memcpy(ph, buf.data(), 64);
                    } else if (!f.read(reinterpret_cast<char*>(ph), 64)) {
                        break;
                    }
                    uint32_t number;
                    std::memcpy(&number, ph + 4, 4);
                    if (number > 0 && number < 512)
                        low.insert(number);
                }
                f.clear();
                f.seekg(start + std::streamoff(size));
                if (!f)
                    break;
            }
            // the Japanese release: exactly pages 1..330 (the US / European one: 1..337)
            if (low.size() == 330 && *low.begin() == 1 && *low.rbegin() == 330)
                shift = 7;
        }
    }
    if (shift)
        std::fprintf(stderr, "PC files: %s has the Japanese PC release's page numbers (read as the US release's)\n", path.c_str());
    std::lock_guard<std::mutex> g(lock);
    known[key] = shift;
    return shift;
}

// display2: the pages of the blood colour's set (B_<colour>.VMC, as THOTD.EXE 0x4273F0 rebuilds TEXTURE.VMC; custom:
// B_RED's blood texels, those that differ from B_GREEN's, tinted as loadStage does). Red / -1: false (BIGGER.VMC is
// the red set). UNVERIFIED (teaser)
bool pcBloodPages(const std::string& installDir, int blood, const std::vector<uint16_t>& pages,
                  std::vector<std::vector<uint8_t>>& out, std::string& err, const std::vector<uint8_t>* tbl,
                  const std::vector<uint8_t>* pal)
{
    static const char* const kBloodFiles[5] = {"B_RED", "B_GREEN", "B_BLUE", "B_PURPL", "B_YELLO"};
    const bool custom = (blood & PcConfig::kBloodCustom) != 0;
    if (pages.empty() || (!custom && (blood <= 0 || blood >= 5)))
        return false;
    // the blood texels: where B_RED and the other set (custom: B_GREEN) differ (out: alpha 0 elsewhere)
    const std::string r = pcPath(installDir, "ART/STAGE1/CREATURE/B_RED.VMC");
    const std::string o = pcPath(installDir, std::string("ART/STAGE1/CREATURE/") + (custom ? "B_GREEN" : kBloodFiles[blood]) + ".VMC");
    std::vector<std::vector<uint8_t>> other;
    if (!pcfs::exists(r) || !pcfs::exists(o) || !readVmcPages(r, pages, out, err, tbl, pal, true) ||
        !readVmcPages(o, pages, other, err, tbl, pal, true) || other.size() != out.size())
        return false;
    const int rgb[3] = {(blood >> 16) & 0xFF, (blood >> 8) & 0xFF, blood & 0xFF};
    for (size_t l = 0; l < out.size(); ++l) {
        if (other[l].size() != out[l].size()) {
            out[l].clear();
            continue;
        }
        for (size_t i = 0; i + 3 < out[l].size(); i += 4) {
            uint8_t* a = &out[l][i];
            const uint8_t* b = &other[l][i];
            if (!a[3] || !b[3] || (a[0] == b[0] && a[1] == b[1] && a[2] == b[2])) {
                a[3] = 0;
                continue;
            }
            if (custom) {
                const int v = std::max({a[0], a[1], a[2]});
                for (int c = 0; c < 3; ++c)
                    a[c] = uint8_t(rgb[c] * v / 255);
            } else {
                std::memcpy(a, b, 3);
            }
        }
    }
    return true;
}

PcSource::PcSource(PcConfig cfg) : cfg_(std::move(cfg))
{
    std::string probe = cfg_.installDir + "/ART/STAGE1/WORLD/WORLD1.RBH";
    if (!pcfs::exists(probe)) {
        status_ = "PC install not found (" + probe + ")";
        return;
    }
    available_ = true;
    status_ = "PC install in " + cfg_.installDir;
}

namespace {

struct RawPoly { float pos[4][3]; int nv; uint16_t tex; float uv[8]; bool textured; uint16_t hi; uint32_t color; };   // hi: the group head's bits 16-31; color: untextured 0xRRGGBB
struct RawObject { uint32_t id; std::vector<RawPoly> polys; };

// Reads the objects of an RBH model file: world files have variable object table entries (u16 id, 0xFFFF, mesh header
// offsets, u32 0; entrySize 12), creature files 8-byte ones (u16 id, 0xFFFF, u32 header offset). Same mesh headers and
// polygon groups (§9).
// want: object IDs to read (null = all). Textured polygons' page numbers are added to pages.
bool readRbhObjects(const std::string& path, size_t entrySize, const std::set<uint32_t>* want, std::vector<RawObject>& objects,
                    std::set<uint16_t>& pages, std::string& err)
{
    std::vector<uint8_t> d;
    if (!pcfs::readFile(path, d, &err)) return false;
    if (d.size() < 12 || std::memcmp(d.data(), "PIFF", 4) != 0 || std::memcmp(d.data() + 8, "RBHF", 4) != 0) {
        err = path + ": not an RBHF file";
        return false;
    }
    std::vector<std::pair<size_t, size_t>> bodies;
    for (size_t p = 12; p + 8 <= d.size();) {
        uint32_t size = rd32(d, p + 4);
        if (std::memcmp(d.data() + p, "BODY", 4) == 0) bodies.push_back({p + 8, size});
        p += 8 + size;
    }
    if (bodies.size() < 2) { err = path + ": missing BODY chunks"; return false; }
    const size_t b0 = bodies[0].first, n0 = bodies[0].second, b1 = bodies[1].first, nverts = bodies[1].second / 12;
    // the object table: creature files fixed 8-byte entries (u16 id, 0xFFFF, u32 mesh header); world files (THOTD.EXE
    // 0x419CF4) u16 id, 0xFFFF, then mesh header offsets up to a 0 (one or more meshes, all drawn; 0xCFD has none).
    // Reading the world table as fixed 12-byte entries lost every object after 0xCFD (0xCED, 0x111D, 0x111E, 0x91F,
    // 0xAC3: the courtyard wall hei01_01aa among them)
    std::vector<std::pair<uint32_t, std::vector<uint32_t>>> entries;
    for (size_t q = b0; q + 8 <= b0 + n0 && rd16(d, q + 2) == 0xFFFF;) {
        std::pair<uint32_t, std::vector<uint32_t>> e{rd16(d, q), {}};
        if (entrySize == 8) {
            e.second.push_back(rd32(d, q + 4));
            q += 8;
        } else {
            q += 4;
            for (uint32_t v; q + 4 <= b0 + n0 && (v = rd32(d, q)) != 0; q += 4)
                e.second.push_back(v);
            q += 4;
        }
        entries.push_back(std::move(e));
    }
    for (const auto& [id, headers] : entries) {
        if (want && !want->count(id)) continue;
        RawObject obj{id, {}};
        bool ok = true;
        for (uint32_t off : headers) {
        if (off == 0 || off + 0x18 > n0) continue;   // the last header can end the chunk early
        size_t h = b0 + off;
        uint32_t groups = rd32(d, h), voff = rd32(d, h + 4);
        // header + 8: the mesh's untextured colour table (an offset in BODY 0, relocated at load). THOTD.EXE's polygon
        // dispatcher 0x578BB0 passes [header + 8] to the untextured draws 0x577CA0 / 0x577A80: colour = table[u16 after
        // the indices] & 0xFFFFFF, 0 -> 0x080808. Read from the running PC game (ReadProcessMemory): the table lies just
        // before the header (0x5E4: 6 entries; 0x46B: none, the pointer is the header itself)
        const uint32_t colors = rd32(d, h + 8);
        if (groups >= n0) continue;
        uint32_t base = voff / 12;
        for (size_t g = b0 + groups; ok;) {
            if (g + 8 > b0 + n0) { ok = false; break; }
            uint32_t head = rd32(d, g), sizes = rd32(d, g + 4);
            if (head == 0xFFFF0000) break;
            // kind bits 16-21; bit 22 (kinds 64-68, CREAT2 only) is a flag with the same record layout (translucency? not used yet)
            uint32_t count = head & 0xFFFF, kind = (head >> 16) & 0x3F;
            size_t r = g + 8;
            for (uint32_t i = 0; i < count && ok; ++i) {
                RawPoly p{};
                p.nv = (kind == 1 || kind == 4) ? 4 : 3;
                bool good = true;   // a polygon with a vertex index outside the file is skipped (seen in the world files)
                for (int k = 0; k < p.nv; ++k) {
                    uint32_t idx = base + rd16(d, r + 2 * k);
                    if (idx >= nverts) { good = false; break; }
                    for (int c = 0; c < 3; ++c) p.pos[k][c] = rdf(d, b1 + size_t(idx) * 12 + 4 * c);
                }
                p.tex = rd16(d, r + 2 * p.nv);
                p.textured = kind == 2 || kind == 4;
                if (!p.textured) {
                    const size_t c = b0 + size_t(colors) + 4 * size_t(p.tex);
                    p.color = c + 4 <= b0 + n0 ? rd32(d, c) & 0xFFFFFF : 0;
                    if (p.color == 0) p.color = 0x080808;
                }
                p.hi = uint16_t(head >> 16);
                if (kind == 0) r += 8;
                else if (kind == 1) r += 10;
                else if (kind == 2 || kind == 4) {
                    for (int k = 0; k < 2 * p.nv; ++k) p.uv[k] = rdf(d, r + 12 + 4 * k);
                    r += kind == 2 ? 36 : 44;
                } else {
                    err = path + ": unknown polygon group kind " + std::to_string(kind);
                    return false;
                }
                if (good) obj.polys.push_back(p);
            }
            g += 8 + 2 * size_t(sizes >> 16);
        }
        }
        if (!ok || obj.polys.empty()) continue;
        for (const RawPoly& p : obj.polys)
            if (p.textured) pages.insert(p.tex);
        objects.push_back(std::move(obj));
    }
    return true;
}

} // namespace

std::unique_ptr<StageData> PcSource::loadStage(int stage, std::string& err)
{
    if (!available_) { err = status_; return nullptr; }
    if (stage < 0 || stage > 6) { err = "PC: stage " + std::to_string(stage) + " not supported"; return nullptr; }
    // THOTD.EXE pc_chapter_files_load 0x418AA0: chapter 0-3 -> n = chapter + 1, 4 / 5 (the how-to-play, the prologue)
    // -> 5, 6 (the ending) -> 6, anything else 1; the palette stage%d.pal takes n when 1..4, else 1
    const int fileN = stage <= 3 ? stage + 1 : stage <= 5 ? 5 : 6;
    const std::string n = std::to_string(fileN);
    auto out = std::make_unique<StageData>();
    out->source = Source::PC;
    out->stage = stage;

    std::vector<RawObject> world, extra;
    std::set<uint16_t> pageSet;
    if (!readRbhObjects(pcPath(cfg_.installDir, "ART/STAGE" + n + "/WORLD/WORLD" + n + ".RBH"), 12, nullptr, world, pageSet, err))
        return nullptr;
    // requested extra models (creature parts): CREAT1.RBH and CREAT2.RBH use the same object IDs as the arcade
    auto req = requested_.find(stage);
    if (req != requested_.end() && !req->second.empty()) {
        std::set<uint32_t> want(req->second.begin(), req->second.end());
        for (const char* f : {"CREAT1.RBH", "CREAT2.RBH"})
            if (!readRbhObjects(pcPath(cfg_.installDir, std::string("ART/STAGE1/CREATURE/") + f), 8, &want, extra, pageSet, err)) return nullptr;
    }

    // one texture set: the BIGGER.VMC pages these models use (world pages 512+, creature pages below)
    std::vector<uint16_t> pages(pageSet.begin(), pageSet.end());
    TextureSet ts;
    ts.shading = Shading::DirectRgba;
    ts.label = "PC, BIGGER.VMC pages";
    ts.width = ts.height = kPage;
    // the pages the PC game draws: TEXTURE.VMC, the cache of the set with the player's blood colour baked in (THOTD.EXE
    // 0x4186FB checks only that it exists with BIGGER.VMC's size; formats.md §3). THOTD.EXE 0x4273F0 rebuilds it from
    // ART\STAGE1\CREATURE\B_<colour>.VMC (table 0x5CE884: RED, GREEN, BLUE, PURPL, YELLO - the arcade's order, then
    // yellow), PACK-compressed (unpackVmcChunk); without that file it copies BIGGER.VMC. Checked: B_YELLO.VMC unpacks to
    // the install's TEXTURE.VMC byte for byte (421 chunks). PcConfig::blood (the launcher's, 0 red .. 3 purple; 4 yellow)
    // draws B_<colour>.VMC itself, else BIGGER.VMC (red); blood < 0: the install's TEXTURE.VMC (THOTD.CFG's colour).
    std::string vmc = pcPath(cfg_.installDir, "ART/STAGE1/CREATURE/BIGGER.VMC");
    static const char* const kBloodFiles[5] = {"B_RED", "B_GREEN", "B_BLUE", "B_PURPL", "B_YELLO"};
    const bool customBlood = (cfg_.blood & PcConfig::kBloodCustom) != 0;
    std::string greenVmc;   // custom blood: the green set, to find the blood texels (those that differ from B_RED's)
    int jaBlood = 0;        // pchotfix: > 0: BIGGER.VMC drawn, this blood colour's texels laid over it (the Japanese sets)
    if (customBlood) {
        const std::string r = pcPath(cfg_.installDir, "ART/STAGE1/CREATURE/B_RED.VMC");
        const std::string g = pcPath(cfg_.installDir, "ART/STAGE1/CREATURE/B_GREEN.VMC");
        if (pcfs::exists(r) && pcfs::exists(g)) {
            vmc = r;
            greenVmc = g;
        }
    } else if (cfg_.blood >= 0 && cfg_.blood < 5) {
        const std::string b = pcPath(cfg_.installDir, std::string("ART/STAGE1/CREATURE/") + kBloodFiles[cfg_.blood] + ".VMC");
        // pchotfix: the Japanese release's B_<colour>.VMC sets have 4 world pages (549, 552-554) unlike its own
        // BIGGER.VMC and the US sets (Chapter 1's zombies were drawn with other textures): its BIGGER.VMC (the same as
        // the US one's, page for page) is drawn, the blood colour's texels laid over it below (pcBloodPages)
        if (pcfs::exists(b) && (pcVmcPageShift(b) == 0 || !pcfs::exists(vmc)))
            vmc = b;
        else if (pcfs::exists(b))
            jaBlood = cfg_.blood;
    } else {
        const std::string cache = cfg_.installDir + "/ART/STAGE1/CREATURE/TEXTURE.VMC";
        const int64_t texSize = pcfs::size(cache);
        if (texSize >= 0 && texSize == pcfs::size(vmc))
            vmc = cache;
    }
    ts.label = "PC, " + vmc.substr(vmc.find_last_of("/\\") + 1) + " pages";
    // the stage's 8-bit palette: THOTD.EXE's stage start (0x418B07: "stage%d.pal", the stage clamped to 1..4, handed to
    // the render driver's palette call, vtable + 0x34; 0x419032) puts the textures through STAGEn.TBL (palette index by
    // 16-bit texel, opaque bit included) into STAGEn.PAL's 256 colours. Checked against the real PC (harness captures,
    // pc_lookw2 t764): the snow texel 1555 (29,25,25) -> index 241 = (220,209,189), the capture's highlights
    // (219,208,188); the direct 5-bit colour (239,206,206) was pink. HOTD_PC_NO_PAL=1: the direct colours.
    // UNVERIFIED (teaser): the driver's palette routine itself was not read (brown.pal, the sepia look, goes the same way)
    // The palette loader (0x566B30: <name>.pal, 1024 bytes, then <name>.tbl, or a nearest-colour table built from
    // the palette when missing) is the same for brown.pal: the PC's grey look (0x419028: while 0xDA0CBA is set
    // brown.pal, when it clears the stage palette again) is the textures through BROWN.TBL into BROWN.PAL; the stage's
    // own files go with WORLDn.RBH (pc_chapter_files_load 0x418AA0: chapter 0-3 -> n = chapter + 1, n clamped 1..4 for
    // the palette). Both conversions are made here; the renderer draws the brown pages while RenderOptions::grey.
    auto readPal = [&](const std::string& name, std::vector<uint8_t>& tbl, std::vector<uint8_t>& pal) {
        if (!pcfs::readFile(pcPath(cfg_.installDir, name + ".TBL"), tbl) || !pcfs::readFile(pcPath(cfg_.installDir, name + ".PAL"), pal) ||
            tbl.size() < 65536 || pal.size() < 1024) {
            tbl.clear();
            return;
        }
        tbl.resize(65536);
        pal.resize(1024);
    };
    std::vector<uint8_t> tbl, pal, btbl, bpal;
    if (!std::getenv("HOTD_PC_NO_PAL")) {
        readPal("STAGE" + std::to_string(fileN <= 4 ? fileN : 1), tbl, pal);
        readPal("BROWN", btbl, bpal);
    }
    if (!pages.empty() && !readVmcPages(vmc, pages, ts.layers, err, tbl.empty() ? nullptr : &tbl, tbl.empty() ? nullptr : &pal))
        return nullptr;
    if (!pages.empty() && jaBlood > 0) {   // pchotfix: the Japanese sets' blood texels over BIGGER.VMC
        std::vector<std::vector<uint8_t>> bl;
        std::string berr;
        if (pcBloodPages(cfg_.installDir, jaBlood, pages, bl, berr, tbl.empty() ? nullptr : &tbl, tbl.empty() ? nullptr : &pal) &&
            bl.size() == ts.layers.size())
            for (size_t l = 0; l < bl.size(); ++l)
                for (size_t i = 0; i + 3 < bl[l].size() && i + 3 < ts.layers[l].size(); i += 4)
                    if (bl[l][i + 3] && ts.layers[l][i + 3])
                        std::memcpy(&ts.layers[l][i], &bl[l][i], 3);
    }
    // custom blood colour (display, EXPERIMENTAL): the PC has only B_RED / GREEN / BLUE / PURPL / YELLO; the texels
    // where B_RED and B_GREEN differ are the blood, tinted here: the red set's brightness (its strongest channel) times
    // the chosen colour, alpha kept. The grey (brown.pal) pages stay as they are.
    if (!pages.empty() && !greenVmc.empty()) {
        std::vector<std::vector<uint8_t>> green;
        std::string gerr;
        if (readVmcPages(greenVmc, pages, green, gerr, tbl.empty() ? nullptr : &tbl, tbl.empty() ? nullptr : &pal) &&
            green.size() == ts.layers.size()) {
            const int rgb[3] = {(cfg_.blood >> 16) & 0xFF, (cfg_.blood >> 8) & 0xFF, cfg_.blood & 0xFF};
            size_t tinted = 0;
            for (size_t l = 0; l < ts.layers.size(); ++l) {
                std::vector<uint8_t>& a = ts.layers[l];
                const std::vector<uint8_t>& b = green[l];
                for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4) {
                    if (a[i] == b[i] && a[i + 1] == b[i + 1] && a[i + 2] == b[i + 2])
                        continue;
                    const int v = std::max({a[i], a[i + 1], a[i + 2]});
                    for (int c = 0; c < 3; ++c)
                        a[i + size_t(c)] = uint8_t(rgb[c] * v / 255);
                    if ((cfg_.blood & PcConfig::kBloodRainbow) && a[i + 3] >= 128)
                        a[i + 3] = 250;   // cosmods: Rainbow Blood's mark (render/renderer.h kRainbowAlpha: the renderer colours it)
                    ++tinted;
                }
            }
            ts.label += " (custom blood, tinted)";
            std::fprintf(stderr, "PC custom blood %06X: %zu texels tinted\n", unsigned(cfg_.blood & 0xFFFFFF), tinted);
        }
    }
    if (!pages.empty() && !btbl.empty() && !readVmcPages(vmc, pages, ts.layersBrown, err, &btbl, &bpal))
        return nullptr;
    ts.pageIds = pages;
    if (ts.layers.empty()) ts.layers.push_back(std::vector<uint8_t>(size_t(kPage) * kPage * 4, 255));
    out->textureSets.push_back(std::move(ts));

    auto build = [&](const RawObject& o) {
        Model& m = out->models[o.id];
        m.key = o.id;
        Mesh mesh, blended;   // blended: the alpha-0x7F polygons (Mesh::blend)
        mesh.textureSet = blended.textureSet = 0;
        blended.blend = true;
        // pcpolish (2026-10-16): the head bit 0x80 polygons show OPAQUE in the real PC game. THOTD.EXE queues them with
        // diffuse alpha 0x7F (0x577870 / 0x577600) under ALPHABLENDENABLE 1, SRCALPHA / INVSRCALPHA, MODULATEALPHA
        // (render states 0x583820), but the harness captures (pc_look\pcpolish\full t12050 / t13300, the Chariot room's
        // ceiling lights: 0x111D, 34 alpha quads on page 521 at y 50 over the black void) show the tiles at (242,242,244)
        // where alpha 0x7F over black gives ours (123,123,127). The PC's textures go through its 8-bit palette (no alpha
        // channel in the paletted surface; the driver path that drops the vertex alpha not read). Drawn opaque now;
        // HOTD_PC_HALF_ALPHA=1: the old alpha-0x7F pass. UNVERIFIED (teaser) for the other users of the bit (the hit
        // sparks 0x39-0x4A, effects 0x6-0x23, world 0x928 / 0xCF5)
        static const bool halfAlpha = std::getenv("HOTD_PC_HALF_ALPHA") != nullptr;
        static const char* dumpModel = std::getenv("HOTD_PC_DUMP_MODEL");   // debugging: one model's polygons
        const bool dump = dumpModel && std::strtoul(dumpModel, nullptr, 16) == o.id;
        for (const RawPoly& p : o.polys) {
            if (dump)
                std::fprintf(stderr, "model %X: nv %d page %u hi %04X col %06X uv %.0f,%.0f %.0f,%.0f %.0f,%.0f %.0f,%.0f\n",
                             unsigned(o.id), p.nv, unsigned(p.tex), unsigned(p.hi), unsigned(p.color), p.uv[0], p.uv[1],
                             p.uv[2], p.uv[3], p.uv[4], p.uv[5], p.uv[6], p.uv[7]);
            Vertex v[4]{};
            for (int k = 0; k < p.nv; ++k)
                for (int c = 0; c < 3; ++c) v[k].pos[c] = p.pos[k][c];
            float e1[3], e2[3], nr[3];
            for (int c = 0; c < 3; ++c) { e1[c] = v[1].pos[c] - v[0].pos[c]; e2[c] = v[2].pos[c] - v[0].pos[c]; }
            nr[0] = e1[1] * e2[2] - e1[2] * e2[1];
            nr[1] = e1[2] * e2[0] - e1[0] * e2[2];
            nr[2] = e1[0] * e2[1] - e1[1] * e2[0];
            float len = std::sqrt(nr[0] * nr[0] + nr[1] * nr[1] + nr[2] * nr[2]);
            uint16_t layer = 0;
            for (size_t i = 0; i < pages.size(); ++i)
                if (pages[i] == p.tex) layer = uint16_t(i);
            for (int k = 0; k < p.nv; ++k) {
                for (int c = 0; c < 3; ++c) v[k].normal[c] = len > 0 ? nr[c] / len : 0.0f;
                v[k].rect[2] = v[k].rect[3] = kPage;
                v[k].layer = layer;
                // THOTD.EXE's polygon dispatcher (0x578BB0): head bit 25 = double-sided, else the back-face test
                // 0x578F60; bit 23 = alpha 0x7F instead of 0xFF (0x577870 / 0x577CA0)
                v[k].flags = uint16_t((p.textured ? kTextured : 0) | ((p.hi & 0x200) ? kDoubleSided : 0) |
                                      ((p.hi & 0x80) && halfAlpha ? kHalfAlpha : 0));
                if (p.textured) {
                    v[k].uv[0] = p.uv[2 * k];
                    v[k].uv[1] = p.uv[2 * k + 1];
                }
                // untextured: the mesh's colour table entry (0x577CA0: D3DTLVERTEX diffuse = alpha << 24 | 0xRRGGBB)
                v[k].color[0] = uint8_t(p.color >> 16);
                v[k].color[1] = uint8_t(p.color >> 8);
                v[k].color[2] = uint8_t(p.color);
                v[k].color[3] = 255;
            }
            const int tri[2][3] = {{0, 1, 2}, {0, 2, 3}};
            for (int t = 0; t < (p.nv == 4 ? 2 : 1); ++t)
                for (int k = 0; k < 3; ++k) ((p.hi & 0x80) && halfAlpha ? blended : mesh).vertices.push_back(v[tri[t][k]]);
        }
        m.meshes.push_back(std::move(mesh));
        if (!blended.vertices.empty()) m.meshes.push_back(std::move(blended));
        m.computeBounds();
    };
    for (const RawObject& o : world) {
        build(o);
        out->placements.push_back({canonicalId(o.id), o.id, {0, 0, 0}});
    }
    for (const RawObject& o : extra)
        if (!out->models.count(o.id)) build(o);   // a world object with the same ID wins
    // survfix (user 2026-11-04: Survival shows no hand outside VR, the shop's and pickups' guns without it):
    // the players' gun hand 0x719 `ter00a` without the hand as kGunOnlyKey 0xF1000719, as the arcade's
    // (ArcadeSource::loadStage). The PC's 0x719 is one texture page (560) and its empty hand 0x36B another (551), so
    // the arcade's rule (the empty hand's texture rectangles) does not carry over: the hand's skin and cuff are the
    // page's corner u < 40, v >= 225 (rendered alone: the hand; u >= 225 the slide and grip, 97..118 one quad, the
    // untextured muzzle), those left out
    if (auto g = out->models.find(0x719); g != out->models.end() && !out->models.count(0xF1000719)) {
        Model gun = g->second;
        gun.key = 0xF1000719;
        gun.name += " gun only";
        for (Mesh& m : gun.meshes) {
            std::vector<Vertex> kept;
            for (size_t t = 0; t + 2 < m.vertices.size(); t += 3) {
                const Vertex* v = &m.vertices[t];
                const float u = (v[0].uv[0] + v[1].uv[0] + v[2].uv[0]) / 3.0f, w = (v[0].uv[1] + v[1].uv[1] + v[2].uv[1]) / 3.0f;
                if (!((v[0].flags & kTextured) && u < 40.0f && w >= 225.0f))
                    kept.insert(kept.end(), v, v + 3);
            }
            m.vertices = std::move(kept);
        }
        gun.computeBounds();
        if (gun.triangleCount() > 0)
            out->models[0xF1000719] = std::move(gun);
    }
    return out;
}

} // namespace hotd
