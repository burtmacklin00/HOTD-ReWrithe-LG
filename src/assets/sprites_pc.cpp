// The PC port's 2D sprites (sprites_pc.h). UNVERIFIED (teaser): the layout is read from the files and the PC code
// (0x4434B0, 0x4432F0); not compared with the PC game's screen yet.
#include "assets/sprites_pc.h"

#include "assets/archive.h"
#include "assets/pc_files.h"
#include "assets/pc_loader.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <memory>
#include <mutex>
#include <sstream>
#include <set>
#include <thread>

namespace hotd::assets {

namespace {

std::vector<uint8_t> readAll(const std::string& path)
{
    std::vector<uint8_t> d;
    if (!hotd::pcfs::readFile(path, d))
        d.clear();
    return d;
}

std::string pcPath(const std::string& install, const std::string& rel)
{
    const std::string m = pcFileMapped(rel);
    return m.empty() ? install + "/" + rel : m;
}

template <class T> T rd(const std::vector<uint8_t>& b, size_t off)
{
    T v{};
    if (off + sizeof(T) <= b.size())
        std::memcpy(&v, b.data() + off, sizeof(T));
    return v;
}

// perf (2026-10-21): the sprite pages' texels as BIGGER.VMC holds them (BODY chunks of 256 x 256 x 16 bits, file
// order), read once per file and set of pages: the menu loads the sprites twice (its MENU and TITLE colours) and the
// HUD once more (~75 ms of reading each; a zipped install inflated the 55 MB file each time)
struct RawPages {
    std::string path, spanish;   // (spanish2: the Spanish text pages' file, pcSpanishTextures)
    std::set<int> pages;
    bool ok = false;   // the file is there and a VMC
    std::vector<std::pair<int, std::vector<uint8_t>>> list;
};
// the pages of one VMC file into r->list; false: not a VMC
bool readRawPages(const std::string& path, const std::set<int>& pages, RawPages* r);
std::shared_ptr<const RawPages> rawPages(const std::string& path, const std::set<int>& pages)
{
    static std::mutex lock;
    static std::shared_ptr<const RawPages> last;
    std::lock_guard<std::mutex> g(lock);
    const std::string& es = pcSpanishTextures();
    if (last && last->path == path && last->pages == pages && last->spanish == es)
        return last;
    auto r = std::make_shared<RawPages>();
    r->path = path;
    r->spanish = es;
    r->pages = pages;
    if (!readRawPages(path, pages, r.get()))
        return r;   // (not kept: tried again next time)
    r->ok = true;
    // spanish2: the translated text pages from the Spanish copy (pc_loader.h), the others as read
    std::set<int> want;
    for (int p : pages)
        if (pcSpanishTextPage(p)) want.insert(p);
    RawPages sp;
    if (!es.empty() && es != path && !want.empty() && readRawPages(es, want, &sp))
        for (auto& [n, body] : sp.list)
            for (auto& e : r->list)
                if (e.first == n) e.second = std::move(body);
    last = r;
    return r;
}

bool readRawPages(const std::string& path, const std::set<int>& pages, RawPages* r)
{
    const std::unique_ptr<std::istream> stream = pcfs::openStream(path);
    std::istringstream none;
    std::istream& f = stream ? *stream : none;
    char hdr[12];
    if (!f.read(hdr, 12) || std::memcmp(hdr, "PIFF", 4) != 0 || std::memcmp(hdr + 8, "VVTP", 4) != 0)
        return false;
    const int shift = pcVmcPageShift(path);   // pchotfix: the Japanese release's page numbers (pc_loader.h)
    std::vector<uint8_t> body;
    for (;;) {
        char ch[8];
        if (!f.read(ch, 8))
            break;
        uint32_t size;
        std::memcpy(&size, ch + 4, 4);
        if (size > (1u << 24))
            break;   // pchotfix: a damaged file: the pages read so far are kept
        if (std::memcmp(ch, "BODY", 4) != 0 || size < 64) {
            f.seekg(size, std::ios::cur);
            continue;
        }
        uint8_t ph[64];
        f.read(reinterpret_cast<char*>(ph), 64);
        uint32_t number;
        uint16_t w, h;
        std::memcpy(&number, ph + 4, 4);
        std::memcpy(&w, ph + 8, 2);
        std::memcpy(&h, ph + 10, 2);
        if (number > 0 && number < 512)
            number += uint32_t(shift);
        // pchotfix: a page whose chunk is too small for its 256 x 256 texels (a cut file) is not taken
        if (!pages.count(int(number)) || w != 256 || h != 256 || size < 64 + 256 * 256 * 2) {
            f.seekg(size - 64, std::ios::cur);
            continue;
        }
        body.resize(size - 64);
        if (!f.read(reinterpret_cast<char*>(body.data()), std::streamsize(body.size())))
            break;   // the file ends inside the page
        r->list.emplace_back(int(number), body);
    }
    return true;
}

} // namespace

bool PcSprites::load(const std::string& installDir, std::string& err, int stage, int bloodColour, const std::string& palette)
{
    const std::vector<uint8_t> rbh = readAll(pcPath(installDir, "SPRITE/SPR.RBH"));
    if (rbh.size() < 0x30 || std::memcmp(rbh.data(), "PIFF", 4) != 0 || std::memcmp(rbh.data() + 0x20, "BODY", 4) != 0) {
        err = "SPRITE/SPR.RBH missing or not an RBH file";
        return false;
    }
    // pchotfix (players 2026-11-02): the body as the file says, kept inside the file (a cut / damaged file)
    const size_t base = 0x28, bodySize = std::min<size_t>(rd<uint32_t>(rbh, 0x24), rbh.size() - base);
    const uint32_t first = rd<uint32_t>(rbh, base);
    const size_t n = std::min<size_t>(first / 4, bodySize / 4);
    // pchotfix: the Japanese PC release's SPR.RBH (1479 sprites, 71,868 bytes; also in repacks of the PC game) lacks
    // the US / European release's first 161 sprites (a picture, a logo and the grey 24 font, 2..160, on its VMC pages
    // 1-7): its sprite i is their i + 161 and its pages below 512 are 7 lower (pcVmcPageShift). Read as the US file's
    // numbers, so the menu, HUD and fonts find the same sprites; the missing grey font is drawn from the white one.
    // Checked against the Japanese disc: 1453 of its 1479 sprites equal the US file's i + 161 (the others are equal
    // duplicates elsewhere)
    constexpr size_t kUsSprites = 1640, kJaSprites = 1479;
    const size_t idShift = n == kJaSprites ? kUsSprites - kJaSprites : 0;
    const int pageShift = idShift ? 7 : 0;
    if (n != kUsSprites && n != kJaSprites)
        std::fprintf(stderr, "PC sprites: SPRITE/SPR.RBH has %zu sprites (the 1998 release: %zu): another version\n", n, kUsSprites);
    sprites_.assign(n + idShift, Sprite{});
    std::set<int> pages;
    size_t bad = 0;
    for (size_t i = 0; i < n; ++i) {
        const size_t o = base + rd<uint32_t>(rbh, base + 4 * i);
        if (o < base || o + 16 > base + bodySize) {
            ++bad;
            continue;
        }
        Sprite& s = sprites_[i + idShift];
        // (the first 64 pieces, as before; pchotfix: only those inside the file)
        const uint32_t count = std::min<uint32_t>({rd<uint32_t>(rbh, o), 64u, uint32_t((base + bodySize - o - 16) / 16)});
        for (int k = 0; k < 4; ++k)
            s.box[k] = rd<int16_t>(rbh, o + 4 + 2 * k);
        s.w = rd<int16_t>(rbh, o + 12);
        s.h = rd<int16_t>(rbh, o + 14);
        if (s.w < 0 || s.h < 0 || s.w > 8192 || s.h > 8192) {
            s = Sprite{};   // pchotfix: no such picture (image() would make it): a damaged file
            ++bad;
            continue;
        }
        for (uint32_t k = 0; k < count; ++k) {
            const size_t q = o + 16 + 16 * k;
            Piece p{rd<int16_t>(rbh, q), rd<int16_t>(rbh, q + 2), rd<int16_t>(rbh, q + 4), rd<int16_t>(rbh, q + 6),
                    rd<uint8_t>(rbh, q + 8), rd<uint8_t>(rbh, q + 9), rd<int16_t>(rbh, q + 12)};
            if (p.page > 0 && p.page < 512)
                p.page = int16_t(p.page + pageShift);
            s.pieces.push_back(p);
            pages.insert(p.page);
        }
    }
    if (bad)
        std::fprintf(stderr, "PC sprites: SPRITE/SPR.RBH: %zu of %zu sprites unreadable (a damaged file?)\n", bad, n);
    if (idShift)
        std::fprintf(stderr, "PC sprites: the Japanese PC release's SPR.RBH (%zu sprites): read as the US release's numbers\n", n);
    // the pages, from BIGGER.VMC (docs/formats.md §3)
    const std::shared_ptr<const RawPages> raw = rawPages(pcPath(installDir, "ART/STAGE1/CREATURE/BIGGER.VMC"), pages);
    if (!raw->ok) {
        err = "ART/STAGE1/CREATURE/BIGGER.VMC missing";
        sprites_.clear();
        return false;
    }
    // pcpolish (2026-10-16): the 2D goes through the stage's 8-bit palette like the 3D textures (pc_loader.cpp
    // readVmcPages: STAGEn.TBL = palette index by 16-bit texel, STAGEn.PAL = 256 x RGBx; THOTD.EXE 0x418B07 hands the
    // palette to the render driver for everything it draws). Real PC harness capture of the results screen
    // (pc_look\pcpolish\full t14810), the metal plate p50 / p90: (147,155,180) / (182,192,206); through STAGE1
    // (146,153,180) / (176,190,205); the direct 5-bit colours were teal. HOTD_PC_NO_PAL=1: the direct colours.
    std::vector<uint8_t> tbl, pal;
    if (!std::getenv("HOTD_PC_NO_PAL")) {
        const std::string n = !palette.empty() ? palette : "STAGE" + std::to_string(stage < 1 ? 1 : stage > 4 ? 4 : stage);
        tbl = readAll(pcPath(installDir, n + ".TBL"));
        pal = readAll(pcPath(installDir, n + ".PAL"));
        if (tbl.size() != 65536 || pal.size() < 1024) tbl.clear();
    }
    // pcpolish3: the PC's grey look (0x419028: brown.pal while 0xDA0CBA is set) goes to the render driver's one
    // palette, so the 2D turns brown with the 3D: real PC harness capture pc_look\pcpolish3\full t14350 (Sophie's
    // death, kind 182): HOLD YOUR FIRE! and the subtitle in the brown tones
    std::vector<uint8_t> btbl, bpal;
    if (!tbl.empty()) {
        btbl = readAll(pcPath(installDir, "BROWN.TBL"));
        bpal = readAll(pcPath(installDir, "BROWN.PAL"));
        if (btbl.size() != 65536 || bpal.size() < 1024) btbl.clear();
    }
    brownPages_.clear();
    pages_.clear();   // pchotfix: only this load's pages (drawable)
    brown_ = false;
    dir_ = installDir, tbl_ = tbl, pal_ = pal;   // (rainbow2: the blood texels, found on first use)
    bloodMask_.clear();
    bloodMaskTried_ = false;
    // perf (2026-10-21): the pages converted on several threads (each page on its own; a page number read twice takes
    // the later one's texels, as when the later one overwrote the earlier)
    struct Job { const std::vector<uint8_t>* body; std::vector<uint8_t>* px; std::vector<uint8_t>* bx; };
    std::map<int, const std::vector<uint8_t>*> latest;
    for (const auto& [number, body] : raw->list)
        latest[number] = &body;
    std::vector<Job> jobs;
    for (const auto& [number, body] : latest)
        jobs.push_back({body, &pages_[number], btbl.empty() ? nullptr : &brownPages_[number]});
    auto convert = [&](const Job& j) {
        const std::vector<uint8_t>& body = *j.body;
        std::vector<uint8_t>& px = *j.px;
        std::vector<uint8_t>* const bx = j.bx;
        px.assign(256 * 256 * 4, 0);
        if (bx)
            bx->assign(256 * 256 * 4, 0);
        for (int i = 0; i < 256 * 256; ++i) {
            const uint16_t c = uint16_t(body[size_t(i) * 2] | body[size_t(i) * 2 + 1] << 8);
            if (!(c & 0x8000) || (c & 0x7FFF) == 0x7C1F)
                continue;
            if (bx) {
                const uint8_t* e = bpal.data() + size_t(btbl[c]) * 4;
                std::memcpy(&(*bx)[size_t(i) * 4], e, 3);
                (*bx)[size_t(i) * 4 + 3] = 255;
            }
            if (!tbl.empty()) {
                const uint8_t* e = pal.data() + size_t(tbl[c]) * 4;
                px[size_t(i) * 4 + 0] = e[0];
                px[size_t(i) * 4 + 1] = e[1];
                px[size_t(i) * 4 + 2] = e[2];
                px[size_t(i) * 4 + 3] = 255;
                continue;
            }
            auto x5 = [](int v) { return uint8_t((v << 3) | (v >> 2)); };
            px[size_t(i) * 4 + 0] = x5(c >> 10 & 31);
            px[size_t(i) * 4 + 1] = x5(c >> 5 & 31);
            px[size_t(i) * 4 + 2] = x5(c & 31);
            px[size_t(i) * 4 + 3] = 255;
        }
    };
    std::atomic<size_t> next{0};
    auto run = [&] {
        for (size_t k = next++; k < jobs.size(); k = next++)
            convert(jobs[k]);
    };
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < std::clamp(std::thread::hardware_concurrency(), 1u, 8u) && jobs.size() > 8; ++t)
        pool.emplace_back(run);
    run();
    for (std::thread& t : pool)
        t.join();
    // pchotfix: the grey 24 font (2..160, the US release's VMC pages 1-7) when the files have no such pages (the
    // Japanese release, or its BIGGER.VMC with the US SPR.RBH): the white 24 font's same glyph (id + 0x19B), drawn
    // darker. The cursor and the other menu pictures are past these and come from the files as they are.
    {
        size_t aliased = 0;
        for (int id = 2; id <= 160 && size_t(id + 0x19B) < sprites_.size(); ++id)
            if (!drawable(id) && drawable(id + 0x19B)) {
                sprites_[size_t(id)] = sprites_[size_t(id + 0x19B)];
                sprites_[size_t(id)].shade = 150;
                ++aliased;
            }
        if (aliased)
            std::fprintf(stderr, "PC sprites: the grey font drawn from the white one (%zu glyphs; not in these PC files)\n", aliased);
    }
    // display2: the blood colour. The PC game draws its sprites from TEXTURE.VMC too, i.e. the B_<colour>.VMC set
    // (pc_loader.cpp). Of the sprite pages only 219 / 220 differ between the sets (the GAME OVER screen 0x4E3, red ->
    // the blood colour); the hit marks 0x47E..0x483 (pages 103-114) are the same red in every set, and the PC's
    // hud_player_notice_show 0x50B444 picks them from a fixed table (0x5F2DE8): red with every blood colour, as the
    // arcade's (0x945D0: table 0xCA5FC; no 2D routine reads blood_color). UNVERIFIED (teaser): no PC capture with
    // non-red blood
    std::vector<uint16_t> list;
    for (const auto& [number, px] : pages_)
        list.push_back(uint16_t(number));
    std::vector<std::vector<uint8_t>> blood;
    std::string berr;
    if (pcBloodPages(installDir, bloodColour, list, blood, berr, tbl.empty() ? nullptr : &tbl, tbl.empty() ? nullptr : &pal) &&
        blood.size() == list.size()) {
        size_t changed = 0;
        static const bool trace = std::getenv("HOTD_TRACE_SPRITES") != nullptr;
        for (size_t k = 0; k < list.size(); ++k) {
            std::vector<uint8_t>& px = pages_[int(list[k])];
            size_t n = 0;
            for (size_t i = 0; i + 3 < px.size() && i + 3 < blood[k].size(); i += 4)
                if (px[i + 3] && blood[k][i + 3] && std::memcmp(&px[i], &blood[k][i], 3) != 0) {
                    std::memcpy(&px[i], &blood[k][i], 3);
                    ++n;
                }
            if (trace && n)
                std::fprintf(stderr, "PC sprites: page %d: %zu blood texels\n", int(list[k]), n);
            changed += n;
        }
        std::fprintf(stderr, "PC sprites: blood colour %X: %zu texels from the blood set\n", unsigned(bloodColour), changed);
    } else if (!berr.empty()) {
        std::fprintf(stderr, "PC sprites: blood colour %X not applied: %s\n", unsigned(bloodColour), berr.c_str());
    }
    return true;
}

int PcSprites::width(int id) const
{
    if (const auto c = custom_.find(id); c != custom_.end())
        return c->second.w;
    return id > 0 && size_t(id) < sprites_.size() ? sprites_[size_t(id)].w : 0;
}

int PcSprites::height(int id) const
{
    if (const auto c = custom_.find(id); c != custom_.end())
        return c->second.h;
    return id > 0 && size_t(id) < sprites_.size() ? sprites_[size_t(id)].h : 0;
}

void PcSprites::draw(std::vector<uint8_t>& canvas, int id, int x, int y) const
{
    draw(canvas, id, x, y, 0, 0, kWidth, kHeight);
}

void PcSprites::draw(std::vector<uint8_t>& canvas, int id, int x, int y, int cx0, int cy0, int cx1, int cy1,
                     int canvasW) const
{
    const int canvasH = canvasW > 0 ? int(canvas.size() / (size_t(canvasW) * 4)) : 0;
    cx0 = std::max(cx0, 0), cy0 = std::max(cy0, 0), cx1 = std::min(cx1, canvasW), cy1 = std::min(cy1, canvasH);
    if (const auto c = custom_.find(id); c != custom_.end()) {   // pmfix2: a picture of ours (addImage)
        const Custom& cu = c->second;
        for (int j = 0; j < cu.h; ++j) {
            const int sy = y + cu.oy + j;
            if (sy < cy0 || sy >= cy1)
                continue;
            for (int i = 0; i < cu.w; ++i) {
                const int sx = x + cu.ox + i;
                const uint8_t* s = &cu.rgba[(size_t(j) * size_t(cu.w) + size_t(i)) * 4];
                if (sx < cx0 || sx >= cx1 || !s[3])
                    continue;
                uint8_t* d = &canvas[(size_t(sy) * size_t(canvasW) + size_t(sx)) * 4];
                for (int k = 0; k < 3; ++k)
                    d[k] = uint8_t((s[k] * s[3] + d[k] * (255 - s[3])) / 255);
                d[3] = 255;
            }
        }
        return;
    }
    if (id <= 0 || size_t(id) >= sprites_.size())
        return;
    const std::map<int, std::vector<uint8_t>>& pages = brown_ ? brownPages_ : pages_;
    const Sprite& sp = sprites_[size_t(id)];
    for (const Piece& p : sp.pieces) {
        const auto it = pages.find(p.page);
        if (it == pages.end() || it->second.size() < 256 * 256 * 4)
            continue;
        const std::vector<uint8_t>& px = it->second;
        const std::vector<uint8_t>* mask = nullptr;   // rainbow2: the page's blood texels (setBloodTexelTint)
        if (bloodTexOn_ && !brown_)
            if (const auto bm = bloodMask_.find(p.page); bm != bloodMask_.end()) mask = &bm->second;
        for (int j = 0; j < p.y1 - p.y0; ++j) {
            const int sy = y + p.y0 + j, v = p.v + j;
            if (sy < cy0 || sy >= cy1 || v > 255)
                continue;
            for (int i = 0; i < p.x1 - p.x0; ++i) {
                const int sx = x + p.x0 + i, u = p.u + i;
                if (sx < cx0 || sx >= cx1 || u > 255)
                    continue;
                const uint8_t* s = &px[size_t(v * 256 + u) * 4];
                if (!s[3])
                    continue;
                uint8_t* d = &canvas[(size_t(sy) * size_t(canvasW) + size_t(sx)) * 4];
                std::memcpy(d, s, 4);
                if (sp.shade)
                    for (int k = 0; k < 3; ++k)
                        d[k] = uint8_t(s[k] * sp.shade / 255);
                if (mask && (*mask)[size_t(v * 256 + u)])
                    tintTexel(d, 2, bloodTex_);
                else if (tintMode_)
                    tintTexel(d, tintMode_, tint_);
            }
        }
    }
}

// rainbow2 (Rainbow Blood): 1 the red share of a texel (blood art: the grey under it kept), 2 its brightest channel
// (a blood texel, the HUD: the shading kept), times rgb
void PcSprites::tintTexel(uint8_t* d, int mode, const float rgb[3])
{
    if (mode == 1) {
        const int m = std::max(d[1], d[2]), share = d[0] - m;
        if (share > 0)
            for (int k = 0; k < 3; ++k)
                d[k] = uint8_t(std::clamp(float(k == 0 ? m : d[k]) + float(share) * rgb[k] + 0.5f, 0.0f, 255.0f));
    } else {
        // v1.3.1 (the HUD's rainbow as P3 / P4's colours): the red texels through a ramp of the colour, shading kept
        // (flames and label, not the brass: P1's red ones, P2's blue ones)
        const bool redFlame = d[0] > d[1] + 16 && d[0] > d[2] + 16 && (d[0] >= 230 || d[1] * 2 < d[0]);
        if (!redFlame && !(d[2] > d[0] + 16 && d[2] > d[1] + 16))
            return;
        const float lum = (0.299f * d[0] + 0.587f * d[1] + 0.114f * d[2]) / 255.0f;
        const float t = std::clamp(0.5f * lum + 0.5f * float(std::max({d[0], d[1], d[2]})) / 255.0f, 0.0f, 1.0f) * 4.0f;
        const int s = std::min(3, int(t));
        const float f = t - float(s);
        for (int k = 0; k < 3; ++k) {
            const float st[5] = {0.08f * rgb[k], 0.4f * rgb[k], rgb[k], rgb[k] + (1.0f - rgb[k]) * 0.45f,
                                 rgb[k] + (1.0f - rgb[k]) * 0.85f};
            d[k] = uint8_t(std::clamp((st[s] + (st[s + 1] - st[s]) * f) * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
}

void PcSprites::setTint(int mode, const float* rgb)
{
    tintMode_ = rgb ? mode : 0;
    if (rgb)
        for (int k = 0; k < 3; ++k) tint_[k] = rgb[k];
}

// the blood texels: those that differ between the green and the blue blood sets (B_GREEN.VMC / B_BLUE.VMC: the GAME
// OVER screen's blood, pages 219 / 220), found once on first use
void PcSprites::setBloodTexelTint(const float* rgb)
{
    bloodTexOn_ = rgb != nullptr;
    if (!rgb)
        return;
    for (int k = 0; k < 3; ++k) bloodTex_[k] = rgb[k];
    if (bloodMaskTried_)
        return;
    bloodMaskTried_ = true;
    std::vector<uint16_t> list;
    for (const auto& [number, px] : pages_)
        list.push_back(uint16_t(number));
    std::vector<std::vector<uint8_t>> green, blue;
    std::string err;
    const std::vector<uint8_t>* t = tbl_.empty() ? nullptr : &tbl_;
    const std::vector<uint8_t>* p = tbl_.empty() ? nullptr : &pal_;
    if (!pcBloodPages(dir_, 1, list, green, err, t, p) || !pcBloodPages(dir_, 2, list, blue, err, t, p) ||
        green.size() != list.size() || blue.size() != list.size())
        return;
    size_t n = 0;
    for (size_t k = 0; k < list.size(); ++k) {
        std::vector<uint8_t> m(256 * 256, 0);
        bool any = false;
        for (size_t i = 0; i < m.size() && i * 4 + 3 < green[k].size() && i * 4 + 3 < blue[k].size(); ++i)
            if (green[k][i * 4 + 3] && std::memcmp(&green[k][i * 4], &blue[k][i * 4], 3) != 0)
                m[i] = 1, any = true, ++n;
        if (any)
            bloodMask_[int(list[k])] = std::move(m);
    }
    std::fprintf(stderr, "PC sprites: %zu blood texels for Rainbow Blood\n", n);
}

bool PcSprites::drawable(int id) const
{
    if (custom_.count(id))
        return true;
    if (id <= 0 || size_t(id) >= sprites_.size() || sprites_[size_t(id)].pieces.empty())
        return false;
    for (const Piece& p : sprites_[size_t(id)].pieces)
        if (!pages_.count(p.page))
            return false;
    return true;
}

bool PcSprites::onPage(int id, bool (*pred)(int)) const
{
    if (id <= 0 || size_t(id) >= sprites_.size())
        return false;
    for (const Piece& p : sprites_[size_t(id)].pieces)
        if (pred(p.page))
            return true;
    return false;
}

std::vector<uint8_t> PcSprites::image(int id) const
{
    if (const auto c = custom_.find(id); c != custom_.end())
        return c->second.rgba;
    const int w = width(id), h = height(id);
    if (w <= 0 || h <= 0)
        return {};
    std::vector<uint8_t> out(size_t(w) * h * 4, 0);
    const std::map<int, std::vector<uint8_t>>& pages = brown_ ? brownPages_ : pages_;
    const Sprite& sp = sprites_[size_t(id)];
    for (const Piece& p : sp.pieces) {
        const auto it = pages.find(p.page);
        if (it == pages.end() || it->second.size() < 256 * 256 * 4)
            continue;
        for (int j = 0; j < p.y1 - p.y0; ++j) {
            const int y = p.y0 + j, v = p.v + j;
            if (y < 0 || y >= h || v > 255)
                continue;
            for (int i = 0; i < p.x1 - p.x0; ++i) {
                const int x = p.x0 + i, u = p.u + i;
                if (x < 0 || x >= w || u > 255)
                    continue;
                const uint8_t* s = &it->second[size_t(v * 256 + u) * 4];
                if (!s[3])
                    continue;
                uint8_t* d = &out[(size_t(y) * w + size_t(x)) * 4];
                std::memcpy(d, s, 4);
                if (sp.shade)
                    for (int k = 0; k < 3; ++k)
                        d[k] = uint8_t(s[k] * sp.shade / 255);
            }
        }
    }
    return out;
}

} // namespace hotd::assets

namespace hotd::assets {

void PcSprites::addImage(int id, int w, int h, std::vector<uint8_t> rgba, int ox, int oy)
{
    if (w <= 0 || h <= 0 || rgba.size() < size_t(w) * size_t(h) * 4)
        return;
    custom_[id] = Custom{w, h, ox, oy, std::move(rgba)};
}

void PcSprites::origin(int id, int& ox, int& oy) const
{
    ox = oy = 0;
    if (id <= 0 || size_t(id) >= sprites_.size() || sprites_[size_t(id)].pieces.empty())
        return;
    ox = oy = 1 << 20;
    for (const Piece& p : sprites_[size_t(id)].pieces)
        ox = std::min<int>(ox, p.x0), oy = std::min<int>(oy, p.y0);
}

} // namespace hotd::assets
