// util: recolour, resolution (sizes, the visible area), utf8, the PNG writer / reader, the cabinet set's layout
#include "check.h"

#include "util/cab_unpack.h"
#include "util/png.h"
#include "util/png_read.h"
#include "util/recolour.h"
#include "util/resolution.h"
#include "util/utf8.h"
#include "game/files_check.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace hotd;

TEST(util, recolour_parse)
{
    Recolour r;
    CHECK(parseRecolour("#FF8000", r));
    CHECK_EQ(int(r.op), int(Recolour::Tint));
    CHECK_EQ(r.rgb[0], 255);
    CHECK_EQ(r.rgb[1], 128);
    CHECK_EQ(r.rgb[2], 0);
    CHECK(parseRecolour("  tint 102030", r));
    CHECK_EQ(int(r.op), int(Recolour::Tint));
    CHECK_EQ(r.rgb[2], 0x30);
    CHECK(parseRecolour("TINT #0a0B0c", r));
    CHECK_EQ(r.rgb[1], 0x0B);
    CHECK(parseRecolour("mul #808080", r));
    CHECK_EQ(int(r.op), int(Recolour::Multiply));
    CHECK(parseRecolour("multiply 404040", r));
    CHECK_EQ(r.rgb[0], 0x40);
    CHECK(parseRecolour("hue 120", r));
    CHECK_EQ(int(r.op), int(Recolour::Hue));
    CHECK_NEAR(r.hueDeg, 120.0, 1e-6);
    CHECK(parseRecolour("hue -45.5", r));
    CHECK_NEAR(r.hueDeg, -45.5, 1e-6);
    // refused, and the output left as it was
    Recolour keep;
    keep.op = Recolour::Hue, keep.hueDeg = 7;
    for (const char* bad : {"", "   ", "tint #12345", "tint #1234567", "hue", "hue x", "blur 3", "mul"}) {
        Recolour o = keep;
        CHECK_MSG(!parseRecolour(bad, o), bad);
        CHECK_MSG(o.op == Recolour::Hue && o.hueDeg == 7, bad);
    }
}

TEST(util, recolour_pixels)
{
    // tint: the brightest channel times the colour; alpha 0 untouched; alpha kept
    uint8_t px[12] = {200, 10, 10, 255, 9, 8, 7, 0, 0, 0, 0, 128};
    Recolour t;
    CHECK(parseRecolour("#00FF00", t));
    recolourRgba(px, 3, t);
    CHECK_EQ(px[0], 0);
    CHECK_EQ(px[1], 200);
    CHECK_EQ(px[2], 0);
    CHECK_EQ(px[3], 255);
    CHECK_EQ(px[4], 9);   // (alpha 0: left alone)
    CHECK_EQ(px[5], 8);
    CHECK_EQ(px[11], 128);
    // multiply by white: nothing changes; by black: black
    uint8_t m[4] = {12, 34, 56, 200};
    Recolour w;
    CHECK(parseRecolour("mul #FFFFFF", w));
    recolourRgba(m, 1, w);
    CHECK(m[0] == 12 && m[1] == 34 && m[2] == 56 && m[3] == 200);
    CHECK(parseRecolour("mul #000000", w));
    recolourRgba(m, 1, w);
    CHECK(m[0] == 0 && m[1] == 0 && m[2] == 0 && m[3] == 200);
    // hue: red turned 120 degrees is green, 240 blue, 360 red again; greys stay grey
    for (const auto& [deg, want] : std::vector<std::pair<float, int>>{{120.f, 1}, {240.f, 2}, {360.f, 0}, {-120.f, 2}}) {
        uint8_t h[8] = {255, 0, 0, 255, 90, 90, 90, 255};
        Recolour hr;
        hr.op = Recolour::Hue, hr.hueDeg = deg;
        recolourRgba(h, 2, hr);
        CHECK_MSG(h[want] == 255 && h[(want + 1) % 3] == 0 && h[(want + 2) % 3] == 0, std::to_string(deg));
        CHECK(h[4] == 90 && h[5] == 90 && h[6] == 90);
    }
    // None: no change
    uint8_t n[4] = {1, 2, 3, 4};
    recolourRgba(n, 1, Recolour{});
    CHECK(n[0] == 1 && n[1] == 2 && n[2] == 3 && n[3] == 4);
    // a ramp: black stays the first stop, white becomes the last; RedDominant leaves blue alone
    Recolour rp;
    rp.op = Recolour::Ramp;
    for (int k = 0; k < 5; ++k) rp.ramp[k][0] = uint8_t(k * 60), rp.ramp[k][1] = 0, rp.ramp[k][2] = uint8_t(255 - k * 60);
    uint8_t g[8] = {0, 0, 0, 255, 255, 255, 255, 255};
    recolourRgba(g, 2, rp);
    CHECK(g[0] == 0 && g[2] == 255);
    CHECK(g[4] == 240 && g[6] == 15);
    rp.select = Recolour::RedDominant;
    uint8_t b[8] = {10, 10, 200, 255, 200, 10, 10, 255};
    recolourRgba(b, 2, rp);
    CHECK(b[0] == 10 && b[1] == 10 && b[2] == 200);   // (blue: not selected)
    CHECK(!(b[4] == 200 && b[5] == 10 && b[6] == 10));   // (red: recoloured)
}

TEST(util, resolution_sizes)
{
    using util::Size;
    CHECK(Size{}.automatic());
    CHECK_EQ(util::sizeKey(Size{}), std::string("auto"));
    CHECK_EQ(util::sizeName(Size{}), std::string("Auto"));
    CHECK_EQ(util::sizeKey(Size{1920, 1080}), std::string("1920x1080"));
    CHECK_EQ(util::sizeName(Size{1280, 720}), std::string("1280 x 720"));
    for (const Size s : {Size{320, 240}, Size{640, 480}, Size{1920, 1080}, Size{3840, 2160}, Size{16384, 16384}, Size{2560, 1080}})
        CHECK_MSG(util::parseSize(util::sizeKey(s)) == s, util::sizeKey(s));
    for (const char* bad : {"auto", "", "1920", "x", "319x240", "320x239", "16385x100", "-640x480", "abc"})
        CHECK_MSG(util::parseSize(bad).automatic(), bad);
}

TEST(util, resolution_visible_area)
{
    const SDL_Rect d{0, 0, 1920, 1080};
    // a window inside its display: all of it
    SDL_Rect r = util::visibleArea(d, 100, 100, 800, 600, false);
    CHECK(r.x == 0 && r.y == 0 && r.w == 800 && r.h == 600);
    // a window partly past the right edge (more than half shown): the part on the display
    r = util::visibleArea(d, 1520, 0, 800, 600, false);
    CHECK(r.x == 0 && r.y == 0 && r.w == 400 && r.h == 600);
    // partly above the top
    r = util::visibleArea(d, 0, -100, 800, 600, false);
    CHECK(r.x == 0 && r.y == 100 && r.w == 800 && r.h == 500);
    // mostly off its display: its own edges
    r = util::visibleArea(d, 1800, 0, 800, 600, false);
    CHECK(r.x == 0 && r.y == 0 && r.w == 800 && r.h == 600);
    // whole (fullscreen / gamescope), or no display bounds: the whole window
    r = util::visibleArea(d, 1520, 0, 800, 600, true);
    CHECK(r.w == 800 && r.h == 600);
    r = util::visibleArea(SDL_Rect{0, 0, 0, 0}, 1520, 0, 800, 600, false);
    CHECK(r.w == 800 && r.h == 600);
    // a display that is not at the origin (a second monitor on the left)
    const SDL_Rect left{-1280, 0, 1280, 1024};
    r = util::visibleArea(left, -1000, 200, 1000, 1000, false);
    CHECK(r.x == 0 && r.y == 0 && r.w == 1000 && r.h == 824);
}

TEST(util, utf8)
{
    using namespace util;
    CHECK(utf8Valid(""));
    CHECK(utf8Valid("plain"));
    CHECK(utf8Valid("Espa\xC3\xB1ol \xE6\x97\xA5\xE6\x9C\xAC \xF0\x9F\x98\x80"));
    CHECK(!utf8Valid("\xC3"));                 // cut short
    CHECK(!utf8Valid("\x80"));                 // a lone continuation
    CHECK(!utf8Valid("\xE6\x97" "a"));         // a continuation missing
    CHECK(!utf8Valid("\xF8\x88\x80\x80\x80"));   // 5 bytes: never
    std::string s = "a\xC3\xB1";
    utf8PopBack(s);
    CHECK_EQ(s, std::string("a"));
    utf8PopBack(s);
    CHECK(s.empty());
    utf8PopBack(s);   // (empty: nothing)
    CHECK(s.empty());
    const std::string j = "\xE6\x97\xA5\xE6\x9C\xAC";   // two 3-byte characters
    CHECK_EQ(utf8Head(j, 4), std::string("\xE6\x97\xA5"));
    CHECK_EQ(utf8Head(j, 2), std::string());
    CHECK_EQ(utf8Head(j, 6), j);
    CHECK_EQ(utf8Tail(j, 4), std::string("\xE6\x9C\xAC"));
    CHECK_EQ(utf8Tail(j, 1), std::string());
    CHECK(utf8Valid(utf8Head(j, 5)) && utf8Valid(utf8Tail(j, 5)));
    // fit: the longest head that fits with the tail, spaces before the tail dropped
    auto fits = [](const std::string& x) { return x.size() <= 8; };
    CHECK_EQ(utf8Fit("short", "...", fits), std::string("short"));
    CHECK_EQ(utf8Fit("abcd efghij", "...", fits), std::string("abcd..."));
    CHECK_EQ(utf8Fit("\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1", "..", fits), std::string("\xC3\xB1\xC3\xB1\xC3\xB1.."));
}

TEST(util, png_round_trip)
{
    const std::string dir = test::tempDir("png");
    // RGBA: odd sizes, every byte value
    const int w = 37, h = 23;
    std::vector<uint8_t> rgba(size_t(w) * h * 4);
    for (size_t i = 0; i < rgba.size(); ++i) rgba[i] = uint8_t(i * 7 + (i >> 9));
    const std::string a = dir + "/rgba.png";
    CHECK(writePngRgba(a, w, h, rgba));
    int rw = 0, rh = 0;
    std::vector<uint8_t> back;
    std::string err;
    CHECK_MSG(readPngRgba(a, rw, rh, back, err), err);
    CHECK(rw == w && rh == h);
    CHECK(back == rgba);
    // RGB: alpha 255 on the way back
    std::vector<uint8_t> rgb(size_t(w) * h * 3);
    for (size_t i = 0; i < rgb.size(); ++i) rgb[i] = uint8_t(255 - i);
    const std::string b = dir + "/rgb.png";
    CHECK(writePng(b, w, h, rgb));
    CHECK_MSG(readPngRgba(b, rw, rh, back, err), err);
    bool same = rw == w && rh == h && back.size() == size_t(w) * h * 4;
    for (size_t p = 0; same && p < size_t(w) * h; ++p)
        same = back[p * 4] == rgb[p * 3] && back[p * 4 + 1] == rgb[p * 3 + 1] && back[p * 4 + 2] == rgb[p * 3 + 2] && back[p * 4 + 3] == 255;
    CHECK(same);
    // a large picture (several deflate blocks of 64 KB)
    const int W = 300, H = 200;
    std::vector<uint8_t> big(size_t(W) * H * 4, 0x5A);
    CHECK(writePngRgba(dir + "/big.png", W, H, big));
    CHECK(readPngRgba(dir + "/big.png", rw, rh, back, err) && back == big);
    // not a PNG / missing / cut short: refused with a reason
    {
        std::ofstream(dir + "/bad.png", std::ios::binary) << "not a png at all";
    }
    CHECK(!readPngRgba(dir + "/bad.png", rw, rh, back, err) && !err.empty());
    err.clear();
    CHECK(!readPngRgba(dir + "/missing.png", rw, rh, back, err) && !err.empty());
    {
        std::ifstream in(a, std::ios::binary);
        std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::ofstream(dir + "/cut.png", std::ios::binary) << all.substr(0, all.size() / 2);
    }
    err.clear();
    CHECK(!readPngRgba(dir + "/cut.png", rw, rh, back, err));
}

namespace {
void touch(const fs::path& p, const std::string& text = "x")
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p, std::ios::binary) << text;
}
} // namespace

TEST(util, cab_arrange_layout)
{
    // the Spanish CD's cabinets unpacked flat / by stage, the CD's loose ART\ folder: placed in the installed layout
    const fs::path root = test::tempDir("cab");
    const fs::path un = root / "unpacked", disc = root / "disc", out = root / "out";
    touch(un / "O_AAAH.RBH", "voice");
    touch(un / "GAMESNDS.RBH");
    touch(un / "STAGE1" / "WORLD" / "WORLD1.RBH", "world1");
    touch(un / "STAGE1" / "CREATURE" / "CREAT1.RBH");
    touch(un / "SPR.RBH");
    touch(un / "CAMALL.RBH");
    touch(un / "EVTALL.RBH");
    touch(un / "STAGE2" / "OTHER.DAT");
    touch(un / "NEWVOICE.RBH");   // (not in the manifest: a sound by its type)
    touch(un / "README.TXT");     // (as it is)
    touch(un / "THOTD.EXE");
    touch(disc / "ART" / "STAGE1" / "CREATURE" / "BIGGER.VMC", "vmc");
    touch(disc / "SETUP.EXE");    // (the CD's installer: not copied)
    const std::vector<std::string> known = game::pcManifestPaths();
    CHECK(!known.empty());
    const int placed = cab::arrange(un.string(), disc.string(), out.string(), known);
    CHECK_EQ(placed, 12);
    for (const char* want : {"SOUND/O_AAAH.RBH", "SOUND/GAMESNDS.RBH", "ART/STAGE1/WORLD/WORLD1.RBH", "ART/STAGE1/CREATURE/CREAT1.RBH",
                             "SPRITE/SPR.RBH", "CAM/ALLSTAGE/CAMALL.RBH", "EVT/ALLSTAGE/EVTALL.RBH", "ART/STAGE2/OTHER.DAT",
                             "SOUND/NEWVOICE.RBH", "README.TXT", "THOTD.EXE", "ART/STAGE1/CREATURE/BIGGER.VMC"})
        CHECK_MSG(fs::is_regular_file(out / want), want);
    CHECK(!fs::exists(out / "SETUP.EXE"));
    CHECK(fs::exists(disc / "ART" / "STAGE1" / "CREATURE" / "BIGGER.VMC"));   // (the CD: copied, never moved)
    std::ifstream w(out / "ART/STAGE1/WORLD/WORLD1.RBH");
    std::string t;
    w >> t;
    CHECK_EQ(t, std::string("world1"));
    // gameCabinet: a CAB\CAB1.CAB without THOTD.EXE beside it
    const fs::path cd = root / "cd";
    touch(cd / "CAB" / "CAB1.CAB");
    CHECK(!cab::gameCabinet(cd.string()).empty());
    touch(cd / "THOTD.EXE");
    CHECK(cab::gameCabinet(cd.string()).empty());
    CHECK(cab::gameCabinet((root / "nothing").string()).empty());
}
