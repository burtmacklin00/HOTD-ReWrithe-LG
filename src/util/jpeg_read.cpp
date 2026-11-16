// Minimal JPEG reader (menu141, GitHub #83): see util/jpeg_read.h. Written from ITU-T T.81 (the JPEG standard):
// the markers (B.2), the Huffman tables' canonical codes (Annex C, F.2.2.3), the DC prediction and the AC run / size
// coding (F.2.2), the zig-zag order (A.3.6), the inverse DCT (A.3.3) and the JFIF YCbCr -> RGB conversion.
#include "util/jpeg_read.h"

#include "util/png_read.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace hotd {

namespace {

constexpr uint8_t kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                                 41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                                 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

struct Huff {
    uint8_t vals[256] = {};
    int mincode[17] = {}, maxcode[17] = {}, valptr[17] = {};
    bool ok = false;
    void build(const uint8_t* counts)   // (C.2: the codes of each length, counting up)
    {
        int code = 0, k = 0;
        for (int l = 1; l <= 16; ++l) {
            valptr[l] = k;
            mincode[l] = code;
            code += counts[l - 1];
            k += counts[l - 1];
            maxcode[l] = counts[l - 1] ? code - 1 : -1;
            code <<= 1;
        }
        ok = true;
    }
};

struct Comp {
    int id = 0, h = 1, v = 1, tq = 0, td = 0, ta = 0;
    int pred = 0;
    int stride = 0, rows = 0;
    std::vector<uint8_t> px;
};

struct Decoder {
    const uint8_t* d = nullptr;
    size_t n = 0, pos = 0;
    uint32_t buf = 0;
    int cnt = 0;
    bool marker = false;   // the entropy data ran into a marker (the rest reads as zeros)
    std::string err;
    uint16_t q[4][64] = {};
    Huff dc[4], ac[4];
    Comp comp[4];
    int ncomp = 0, width = 0, height = 0, hmax = 1, vmax = 1, restart = 0;
    bool frame = false, adobeRgb = false, adobe = false;
    float idct[8][8] = {};   // idct[x][u] = C(u) / 2 cos((2x + 1) u pi / 16)

    int bit()
    {
        if (cnt == 0) {
            uint8_t b = 0;
            if (!marker && pos < n) {
                b = d[pos];
                if (b == 0xFF) {
                    const uint8_t nb = pos + 1 < n ? d[pos + 1] : 0xD9;
                    if (nb == 0x00)
                        pos += 2;   // (F.1.2.3: a stuffed zero after a 0xFF data byte)
                    else
                        marker = true, b = 0;   // (a marker: not consumed)
                } else
                    ++pos;
            }
            buf = b;
            cnt = 8;
        }
        --cnt;
        return int(buf >> cnt) & 1;
    }
    int bits(int s)
    {
        int v = 0;
        while (s-- > 0) v = (v << 1) | bit();
        return v;
    }
    static int extend(int v, int s) { return s == 0 ? 0 : v < (1 << (s - 1)) ? v - (1 << s) + 1 : v; }   // (F.2.2.1)
    int huff(const Huff& t)
    {
        int code = 0;
        for (int l = 1; l <= 16; ++l) {
            code = (code << 1) | bit();
            if (t.maxcode[l] >= 0 && code <= t.maxcode[l] && code >= t.mincode[l])
                return t.vals[t.valptr[l] + code - t.mincode[l]];
        }
        return -1;
    }
    // one 8 x 8 block of component c into its plane at block (bx, by)
    bool block(Comp& c, int bx, int by)
    {
        const Huff& hd = dc[c.td];
        const Huff& ha = ac[c.ta];
        if (!hd.ok || !ha.ok) { err = "a scan uses a Huffman table that is not defined"; return false; }
        float f[64] = {};
        const int t = huff(hd);
        if (t < 0 || t > 11) { err = "broken image data (DC)"; return false; }
        c.pred += extend(bits(t), t);
        const uint16_t* qt = q[c.tq];
        f[0] = float(c.pred * qt[0]);
        for (int k = 1; k < 64;) {
            const int rs = huff(ha);
            if (rs < 0) { err = "broken image data (AC)"; return false; }
            const int r = rs >> 4, s = rs & 15;
            if (s == 0) {
                if (r != 15) break;   // EOB
                k += 16;              // ZRL
                continue;
            }
            k += r;
            if (k > 63) { err = "broken image data (AC run)"; return false; }
            f[kZigzag[k]] = float(extend(bits(s), s) * qt[k]);
            ++k;
        }
        // the inverse DCT, rows then columns (A.3.3), + 128, clamped
        float tmp[64];
        for (int v = 0; v < 8; ++v)
            for (int x = 0; x < 8; ++x) {
                float s = 0;
                for (int u = 0; u < 8; ++u) s += idct[x][u] * f[v * 8 + u];
                tmp[v * 8 + x] = s;
            }
        uint8_t* out = &c.px[size_t(by) * 8 * size_t(c.stride) + size_t(bx) * 8];
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                float s = 0;
                for (int v = 0; v < 8; ++v) s += idct[y][v] * tmp[v * 8 + x];
                out[size_t(y) * size_t(c.stride) + size_t(x)] = uint8_t(std::clamp(int(std::lround(s + 128.0f)), 0, 255));
            }
        return true;
    }
    // a restart marker: the bits dropped, RSTn skipped, the predictions back to 0
    void restartAt()
    {
        cnt = 0;
        marker = false;
        while (pos + 1 < n && !(d[pos] == 0xFF && d[pos + 1] >= 0xD0 && d[pos + 1] <= 0xD7)) ++pos;
        if (pos + 1 < n) pos += 2;
        for (Comp& c : comp) c.pred = 0;
    }
    uint16_t be16(size_t o) const { return uint16_t((d[o] << 8) | d[o + 1]); }

    bool scan(size_t at, size_t len)
    {
        if (!frame) { err = "a scan before the frame header"; return false; }
        const int ns = d[at];
        if (ns < 1 || ns > 4 || len < size_t(4 + 2 * ns)) { err = "a broken scan header"; return false; }
        Comp* sc[4] = {};
        for (int i = 0; i < ns; ++i) {
            const int id = d[at + 1 + 2 * i], tables = d[at + 2 + 2 * i];
            for (int k = 0; k < ncomp; ++k)
                if (comp[k].id == id) sc[i] = &comp[k];
            if (!sc[i]) { err = "a scan names a component the frame does not have"; return false; }
            sc[i]->td = (tables >> 4) & 3, sc[i]->ta = tables & 3;
            sc[i]->pred = 0;
        }
        pos = at + len;
        cnt = 0, marker = false;
        int todo = restart;
        if (ns == 1) {   // a non-interleaved scan: the component's own blocks, left to right, top to bottom (A.2.2)
            Comp& c = *sc[0];
            const int cw = (width * c.h + hmax - 1) / hmax, ch = (height * c.v + vmax - 1) / vmax;
            const int bw = (cw + 7) / 8, bh = (ch + 7) / 8;
            for (int by = 0; by < bh; ++by)
                for (int bx = 0; bx < bw; ++bx) {
                    if (restart && todo-- == 0) restartAt(), todo = restart - 1;
                    if (!block(c, bx, by)) return false;
                }
        } else {   // interleaved: the MCUs, each with h x v blocks of each component (A.2.3)
            const int mx = (width + 8 * hmax - 1) / (8 * hmax), my = (height + 8 * vmax - 1) / (8 * vmax);
            for (int y = 0; y < my; ++y)
                for (int x = 0; x < mx; ++x) {
                    if (restart && todo-- == 0) restartAt(), todo = restart - 1;
                    for (int i = 0; i < ns; ++i) {
                        Comp& c = *sc[i];
                        for (int v = 0; v < c.v; ++v)
                            for (int h = 0; h < c.h; ++h)
                                if (!block(c, x * c.h + h, y * c.v + v)) return false;
                    }
                }
        }
        // on to the next marker
        while (pos + 1 < n && !(d[pos] == 0xFF && d[pos + 1] != 0x00 && !(d[pos + 1] >= 0xD0 && d[pos + 1] <= 0xD7))) ++pos;
        return true;
    }

    bool run()
    {
        for (int x = 0; x < 8; ++x)
            for (int u = 0; u < 8; ++u)
                idct[x][u] = (u == 0 ? float(1.0 / std::sqrt(2.0)) : 1.0f) * 0.5f *
                             float(std::cos(double(2 * x + 1) * double(u) * 3.14159265358979323846 / 16.0));
        if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) { err = "not a JPEG"; return false; }
        pos = 2;
        bool scanned = false;
        while (pos + 4 <= n) {
            if (d[pos] != 0xFF) { ++pos; continue; }
            const uint8_t m = d[pos + 1];
            if (m == 0xFF) { ++pos; continue; }   // (fill bytes)
            if (m == 0xD9) break;                  // EOI
            if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { pos += 2; continue; }
            const size_t len = be16(pos + 2);
            if (len < 2 || pos + 2 + len > n) { err = "truncated"; return false; }
            const size_t at = pos + 4, end = pos + 2 + len;
            switch (m) {
            case 0xC0: case 0xC1: {   // SOF0 / SOF1: baseline / extended sequential, Huffman
                if (len < 8 || d[at] != 8) { err = "only 8-bit JPEGs are supported"; return false; }
                height = be16(at + 1), width = be16(at + 3), ncomp = d[at + 5];
                if (!width || !height || width > 8192 || height > 8192) { err = "empty or larger than 8192 x 8192"; return false; }
                if ((ncomp != 1 && ncomp != 3) || len < size_t(8 + 3 * ncomp)) { err = "only grey or colour (3 components) JPEGs are supported"; return false; }
                for (int k = 0; k < ncomp; ++k) {
                    Comp& c = comp[k];
                    c.id = d[at + 6 + 3 * k];
                    c.h = d[at + 7 + 3 * k] >> 4, c.v = d[at + 7 + 3 * k] & 15, c.tq = d[at + 8 + 3 * k] & 3;
                    if (c.h < 1 || c.h > 4 || c.v < 1 || c.v > 4) { err = "a broken sampling factor"; return false; }
                    hmax = std::max(hmax, c.h), vmax = std::max(vmax, c.v);
                }
                const int mx = (width + 8 * hmax - 1) / (8 * hmax), my = (height + 8 * vmax - 1) / (8 * vmax);
                for (int k = 0; k < ncomp; ++k) {
                    Comp& c = comp[k];
                    c.stride = mx * c.h * 8, c.rows = my * c.v * 8;
                    c.px.assign(size_t(c.stride) * size_t(c.rows), 128);
                }
                frame = true;
                break;
            }
            case 0xC2: case 0xC6: case 0xCA: case 0xCE:
                err = "progressive JPEGs are not supported (save it as a baseline JPEG or a PNG)";
                return false;
            case 0xC3: case 0xC5: case 0xC7: case 0xC9: case 0xCB: case 0xCD: case 0xCF:
                err = "lossless / hierarchical / arithmetic-coded JPEGs are not supported (save it as a baseline JPEG or a PNG)";
                return false;
            case 0xC4: {   // DHT: one or more tables
                for (size_t o = at; o + 17 <= end;) {
                    const int tc = d[o] >> 4, th = d[o] & 15;
                    if (tc > 1 || th > 3) { err = "a broken Huffman table"; return false; }
                    int total = 0;
                    for (int l = 0; l < 16; ++l) total += d[o + 1 + l];
                    if (total > 256 || o + 17 + size_t(total) > end) { err = "a broken Huffman table"; return false; }
                    Huff& t = tc ? ac[th] : dc[th];
                    std::memcpy(t.vals, &d[o + 17], size_t(total));
                    t.build(&d[o + 1]);
                    o += 17 + size_t(total);
                }
                break;
            }
            case 0xDB: {   // DQT
                for (size_t o = at; o < end;) {
                    const int pq = d[o] >> 4, tq = d[o] & 15;
                    if (tq > 3 || o + 1 + (pq ? 128u : 64u) > end) { err = "a broken quantization table"; return false; }
                    for (int k = 0; k < 64; ++k)
                        q[tq][k] = pq ? be16(o + 1 + 2 * size_t(k)) : d[o + 1 + size_t(k)];
                    o += 1 + (pq ? 128u : 64u);
                }
                break;
            }
            case 0xDD:   // DRI
                if (len >= 4) restart = be16(at);
                break;
            case 0xEE:   // APP14 "Adobe": its transform (0 with 3 components: RGB, not YCbCr)
                if (len >= 14 && !std::memcmp(&d[at], "Adobe", 5))
                    adobe = true, adobeRgb = d[at + 11] == 0;
                break;
            case 0xDA:   // SOS: the scan's entropy-coded data follows its header
                if (!scan(at, len - 2)) return false;
                scanned = true;
                continue;   // (pos: the next marker)
            default:
                break;   // APPn, COM ...: skipped
            }
            pos = end;
        }
        if (!scanned) { err = "no image data"; return false; }
        return true;
    }
};

} // namespace

bool readJpegRgbaMem(const uint8_t* data, size_t size, const std::string& path, int& width, int& height,
                     std::vector<uint8_t>& rgba, std::string& err)
{
    Decoder dec;
    dec.d = data, dec.n = size;
    if (!dec.run()) {
        err = path + ": " + dec.err;
        return false;
    }
    width = dec.width, height = dec.height;
    rgba.assign(size_t(width) * size_t(height) * 4, 255);
    const bool rgb = dec.ncomp == 3 && dec.adobe && dec.adobeRgb;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            uint8_t* o = &rgba[(size_t(y) * size_t(width) + size_t(x)) * 4];
            auto sample = [&](const Comp& c) {   // (the component's sample under this pixel: its sampling factors)
                return float(c.px[size_t(y * c.v / dec.vmax) * size_t(c.stride) + size_t(x * c.h / dec.hmax)]);
            };
            if (dec.ncomp == 1) {
                o[0] = o[1] = o[2] = uint8_t(sample(dec.comp[0]));
                continue;
            }
            const float a = sample(dec.comp[0]), b = sample(dec.comp[1]), c = sample(dec.comp[2]);
            if (rgb) {
                o[0] = uint8_t(a), o[1] = uint8_t(b), o[2] = uint8_t(c);
                continue;
            }
            const float cb = b - 128.0f, cr = c - 128.0f;   // (JFIF: YCbCr -> RGB)
            o[0] = uint8_t(std::clamp(int(std::lround(a + 1.402f * cr)), 0, 255));
            o[1] = uint8_t(std::clamp(int(std::lround(a - 0.344136f * cb - 0.714136f * cr)), 0, 255));
            o[2] = uint8_t(std::clamp(int(std::lround(a + 1.772f * cb)), 0, 255));
        }
    return true;
}

static bool readFile(const std::string& path, std::vector<uint8_t>& b, std::string& err)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        err = "cannot open " + path;
        return false;
    }
    uint8_t tmp[65536];
    size_t n;
    while ((n = std::fread(tmp, 1, sizeof tmp, f)) > 0)
        b.insert(b.end(), tmp, tmp + n);
    std::fclose(f);
    return true;
}

bool readJpegRgba(const std::string& path, int& width, int& height, std::vector<uint8_t>& rgba, std::string& err)
{
    std::vector<uint8_t> b;
    return readFile(path, b, err) && readJpegRgbaMem(b.data(), b.size(), path, width, height, rgba, err);
}

bool readImageRgba(const std::string& path, int& width, int& height, std::vector<uint8_t>& rgba, std::string& err)
{
    std::vector<uint8_t> b;
    if (!readFile(path, b, err))
        return false;
    if (b.size() >= 2 && b[0] == 0xFF && b[1] == 0xD8)
        return readJpegRgbaMem(b.data(), b.size(), path, width, height, rgba, err);
    return readPngRgbaMem(b.data(), b.size(), path, width, height, rgba, err);
}

} // namespace hotd
