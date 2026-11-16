// mods (menu141, GitHub #83): the JPEG reader the splash screen mods use (util/jpeg_read.h). A small baseline encoder
// here makes the test pictures (grey; colour 4:2:0 with restart markers, two quantization tables, odd sizes): each one
// decoded must match its source closely; progressive / broken files are refused without a crash
#include "check.h"

#include "util/jpeg_read.h"
#include "util/png.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

using namespace hotd;

namespace {

constexpr uint8_t kZz[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                             41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                             30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// a baseline JPEG: our own Huffman tables (DC: the 12 sizes at 4 bits; AC: EOB, ZRL and every run / size at 8 bits),
// quantization q[0] for Y, q[1] for Cb / Cr; colour: Y sampled 2 x 2 (4:2:0); restart: DRI's interval in MCUs (0 none)
struct Enc {
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int nacc = 0;
    int dcCode[12] = {}, acCode[256] = {};
    std::vector<uint8_t> acSyms;
    void put8(int b) { out.push_back(uint8_t(b)); }
    void put16(int v) { put8(v >> 8), put8(v & 255); }
    void bits(uint32_t v, int n)
    {
        for (int i = n - 1; i >= 0; --i) {
            acc = (acc << 1) | ((v >> i) & 1);
            if (++nacc == 8) {
                put8(int(acc));
                if (acc == 0xFF) put8(0);
                acc = 0, nacc = 0;
            }
        }
    }
    void flush() { while (nacc) bits(1, 1); }
    static int size(int v) { int s = 0; for (v = std::abs(v); v; v >>= 1) ++s; return s; }
    void value(int v, int s) { if (s) bits(uint32_t(v < 0 ? v + (1 << s) - 1 : v), s); }
    void block(const float* px, int stride, const int* q, int& pred)
    {
        int c[64];
        for (int v = 0; v < 8; ++v)
            for (int u = 0; u < 8; ++u) {
                double s = 0;
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        s += (double(px[y * stride + x]) - 128.0) * std::cos((2 * x + 1) * u * 3.14159265358979 / 16) *
                             std::cos((2 * y + 1) * v * 3.14159265358979 / 16);
                s *= 0.25 * (u ? 1.0 : std::sqrt(0.5)) * (v ? 1.0 : std::sqrt(0.5));
                c[v * 8 + u] = int(std::lround(s / q[v * 8 + u]));
            }
        const int diff = c[0] - pred;
        pred = c[0];
        const int ds = size(diff);
        bits(uint32_t(dcCode[ds]), 4), value(diff, ds);
        int run = 0;
        for (int k = 1; k < 64; ++k) {
            const int a = c[kZz[k]];
            if (!a) { ++run; continue; }
            while (run > 15) bits(uint32_t(acCode[0xF0]), 8), run -= 16;
            const int s = size(a);
            bits(uint32_t(acCode[(run << 4) | s]), 8), value(a, s);
            run = 0;
        }
        if (run) bits(uint32_t(acCode[0]), 8);
    }
    // rgb: w x h x 3 (grey: w x h)
    std::vector<uint8_t> make(const std::vector<uint8_t>& img, int w, int h, bool colour, int restart)
    {
        const int q[2] = {1, 2};
        int qt[2][64];
        for (int t = 0; t < 2; ++t)
            for (int k = 0; k < 64; ++k) qt[t][k] = q[t];
        put16(0xFFD8);
        put16(0xFFDB), put16(2 + 65 * 2);
        for (int t = 0; t < 2; ++t) { put8(t); for (int k = 0; k < 64; ++k) put8(q[t]); }
        const int nc = colour ? 3 : 1;
        put16(0xFFC0), put16(8 + 3 * nc), put8(8), put16(h), put16(w), put8(nc);
        put8(1), put8(colour ? 0x22 : 0x11), put8(0);
        if (colour) put8(2), put8(0x11), put8(1), put8(3), put8(0x11), put8(1);
        for (int s = 0; s < 12; ++s) dcCode[s] = s;
        acSyms = {0x00, 0xF0};
        for (int r = 0; r < 16; ++r)
            for (int s = 1; s <= 10; ++s) acSyms.push_back(uint8_t((r << 4) | s));
        for (size_t i = 0; i < acSyms.size(); ++i) acCode[acSyms[i]] = int(i);
        put16(0xFFC4), put16(2 + 17 + 12 + 17 + int(acSyms.size()));
        put8(0x00);
        for (int l = 1; l <= 16; ++l) put8(l == 4 ? 12 : 0);
        for (int s = 0; s < 12; ++s) put8(s);
        put8(0x10);
        for (int l = 1; l <= 16; ++l) put8(l == 8 ? int(acSyms.size()) : 0);
        for (uint8_t s : acSyms) put8(s);
        if (restart) put16(0xFFDD), put16(4), put16(restart);
        put16(0xFFDA), put16(6 + 2 * nc), put8(nc);
        for (int i = 0; i < nc; ++i) put8(i + 1), put8(0x00);
        put8(0), put8(63), put8(0);
        // the planes, edges repeated to whole MCUs
        const int m = colour ? 16 : 8, mx = (w + m - 1) / m, my = (h + m - 1) / m;
        const int pw = mx * m, ph = my * m;
        std::vector<float> Y(size_t(pw) * ph), Cb(size_t(pw / 2) * (ph / 2)), Cr(Cb.size());
        for (int y = 0; y < ph; ++y)
            for (int x = 0; x < pw; ++x) {
                const int sx = std::min(x, w - 1), sy = std::min(y, h - 1);
                if (!colour) { Y[size_t(y) * pw + x] = img[size_t(sy) * w + sx]; continue; }
                const uint8_t* p = &img[(size_t(sy) * w + sx) * 3];
                Y[size_t(y) * pw + x] = float(0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]);
            }
        if (colour)
            for (int y = 0; y < ph / 2; ++y)
                for (int x = 0; x < pw / 2; ++x) {
                    double cb = 0, cr = 0;
                    for (int k = 0; k < 4; ++k) {
                        const int sx = std::min(2 * x + (k & 1), w - 1), sy = std::min(2 * y + (k >> 1), h - 1);
                        const uint8_t* p = &img[(size_t(sy) * w + sx) * 3];
                        cb += -0.168736 * p[0] - 0.331264 * p[1] + 0.5 * p[2] + 128;
                        cr += 0.5 * p[0] - 0.418688 * p[1] - 0.081312 * p[2] + 128;
                    }
                    Cb[size_t(y) * (pw / 2) + x] = float(cb / 4), Cr[size_t(y) * (pw / 2) + x] = float(cr / 4);
                }
        int pred[3] = {0, 0, 0}, todo = restart, rst = 0;
        for (int by = 0; by < my; ++by)
            for (int bx = 0; bx < mx; ++bx) {
                if (restart && todo-- == 0) {
                    flush();
                    put8(0xFF), put8(0xD0 + (rst++ & 7));
                    pred[0] = pred[1] = pred[2] = 0, todo = restart - 1;
                }
                if (!colour) { block(&Y[size_t(by) * 8 * pw + bx * 8], pw, qt[0], pred[0]); continue; }
                for (int k = 0; k < 4; ++k)
                    block(&Y[(size_t(by) * 16 + (k >> 1) * 8) * pw + bx * 16 + (k & 1) * 8], pw, qt[0], pred[0]);
                block(&Cb[size_t(by) * 8 * (pw / 2) + bx * 8], pw / 2, qt[1], pred[1]);
                block(&Cr[size_t(by) * 8 * (pw / 2) + bx * 8], pw / 2, qt[1], pred[2]);
            }
        flush();
        put16(0xFFD9);
        return out;
    }
};

} // namespace

TEST(mods, jpeg_grey_round_trip)
{
    const int w = 37, h = 21;
    std::vector<uint8_t> img(size_t(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) img[size_t(y) * w + x] = uint8_t((x * 7 + y * 11 + (x * y) % 13) & 255);
    const std::vector<uint8_t> j = Enc().make(img, w, h, false, 0);
    int dw = 0, dh = 0;
    std::vector<uint8_t> rgba;
    std::string err;
    if (!CHECK_MSG(readJpegRgbaMem(j.data(), j.size(), "grey", dw, dh, rgba, err), err)) return;
    CHECK_EQ(dw, w);
    CHECK_EQ(dh, h);
    int worst = 0;
    for (int i = 0; i < w * h; ++i) worst = std::max(worst, std::abs(int(rgba[size_t(i) * 4]) - int(img[size_t(i)])));
    CHECK_MSG(worst <= 2, "grey: largest error " + std::to_string(worst));
}

TEST(mods, jpeg_colour_420_restarts)
{
    const int w = 45, h = 29;
    std::vector<uint8_t> img(size_t(w) * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &img[(size_t(y) * w + x) * 3];
            p[0] = uint8_t(40 + x * 4), p[1] = uint8_t(200 - y * 5), p[2] = uint8_t(60 + (x + y) * 2);
        }
    for (int restart : {0, 1, 2}) {
        const std::vector<uint8_t> j = Enc().make(img, w, h, true, restart);
        int dw = 0, dh = 0;
        std::vector<uint8_t> rgba;
        std::string err;
        if (!CHECK_MSG(readJpegRgbaMem(j.data(), j.size(), "colour", dw, dh, rgba, err), err)) continue;
        CHECK_EQ(dw, w);
        CHECK_EQ(dh, h);
        int worst = 0;
        for (int i = 0; i < w * h; ++i)
            for (int c = 0; c < 3; ++c) worst = std::max(worst, std::abs(int(rgba[size_t(i) * 4 + c]) - int(img[size_t(i) * 3 + c])));
        CHECK_MSG(worst <= 16, "colour (restart " + std::to_string(restart) + "): largest error " + std::to_string(worst));
    }
}

TEST(mods, jpeg_refused)
{
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    std::string err;
    // progressive (SOF2) refused with its message
    const uint8_t prog[] = {0xFF, 0xD8, 0xFF, 0xC2, 0x00, 0x0B, 0x08, 0x00, 0x10, 0x00, 0x10, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xD9};
    CHECK(!readJpegRgbaMem(prog, sizeof prog, "p", w, h, rgba, err));
    CHECK_MSG(err.find("progressive") != std::string::npos, err);
    // not a JPEG; random bytes after SOI; every cut of a real one: no crash (refused or decoded, never past the data)
    const uint8_t junk[] = {1, 2, 3, 4, 5};
    CHECK(!readJpegRgbaMem(junk, sizeof junk, "j", w, h, rgba, err));
    std::vector<uint8_t> rnd = {0xFF, 0xD8};
    uint32_t seed = 12345;
    for (int i = 0; i < 4000; ++i) seed = seed * 1103515245u + 12345u, rnd.push_back(uint8_t(seed >> 16));
    readJpegRgbaMem(rnd.data(), rnd.size(), "r", w, h, rgba, err);
    std::vector<uint8_t> img(16 * 16 * 3, 100);
    const std::vector<uint8_t> j = Enc().make(img, 16, 16, true, 1);
    for (size_t cut = 2; cut < j.size(); cut += 7)
        readJpegRgbaMem(j.data(), cut, "cut", w, h, rgba, err);
    CHECK(true);
    // readImageRgba: a PNG and a JPEG by their first bytes
    const std::string dir = (std::filesystem::temp_directory_path() / "hotd_jpeg_test").string();
    std::filesystem::create_directories(dir);
    std::vector<uint8_t> rgb(4 * 3 * 3, 200);
    CHECK(writePng(dir + "/a.png", 4, 3, rgb));
    CHECK(readImageRgba(dir + "/a.png", w, h, rgba, err) && w == 4 && h == 3);
    if (FILE* f = std::fopen((dir + "/b.jpg").c_str(), "wb")) {
        std::fwrite(j.data(), 1, j.size(), f);
        std::fclose(f);
    }
    CHECK(readImageRgba(dir + "/b.jpg", w, h, rgba, err) && w == 16 && h == 16);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
