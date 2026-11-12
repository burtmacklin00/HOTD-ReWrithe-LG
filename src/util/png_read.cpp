#include "util/png_read.h"

#include "assets/archive.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hotd {

bool readPngRgba(const std::string& path, int& width, int& height, std::vector<uint8_t>& rgba, std::string& err)
{
    std::vector<uint8_t> b;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        uint8_t tmp[65536];
        size_t n;
        while ((n = std::fread(tmp, 1, sizeof tmp, f)) > 0)
            b.insert(b.end(), tmp, tmp + n);
        std::fclose(f);
    } else {
        err = "cannot open " + path;
        return false;
    }
    return readPngRgbaMem(b.data(), b.size(), path, width, height, rgba, err);
}

bool readPngRgbaMem(const uint8_t* data, size_t size, const std::string& path, int& width, int& height,
                    std::vector<uint8_t>& rgba, std::string& err)
{
    const std::vector<uint8_t> b(data, data + size);
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 0x1A, 10};
    if (b.size() < 33 || !std::equal(sig, sig + 8, b.begin())) { err = path + ": not a PNG"; return false; }
    auto be32 = [&](size_t o) { return (uint32_t(b[o]) << 24) | (uint32_t(b[o + 1]) << 16) | (uint32_t(b[o + 2]) << 8) | b[o + 3]; };
    uint32_t w = 0, h = 0;
    int depth = 0, ctype = 0, interlace = 0;
    std::vector<uint8_t> idat, pal, trns;
    for (size_t o = 8; o + 12 <= b.size();) {
        const uint32_t len = be32(o);
        const char* t = reinterpret_cast<const char*>(&b[o + 4]);
        if (size_t(len) + o + 12 > b.size()) { err = path + ": truncated"; return false; }
        const uint8_t* d = &b[o + 8];
        if (!std::strncmp(t, "IHDR", 4) && len >= 13) {
            w = be32(o + 8); h = be32(o + 12); depth = d[8]; ctype = d[9]; interlace = d[12];
        } else if (!std::strncmp(t, "PLTE", 4)) pal.assign(d, d + len);
        else if (!std::strncmp(t, "tRNS", 4)) trns.assign(d, d + len);
        else if (!std::strncmp(t, "IDAT", 4)) idat.insert(idat.end(), d, d + len);
        else if (!std::strncmp(t, "IEND", 4)) break;
        o += size_t(len) + 12;
    }
    if (!w || !h || w > 4096 || h > 4096) { err = path + ": empty or larger than 4096 x 4096"; return false; }
    if (interlace) { err = path + ": interlaced PNGs are not supported (save it without interlacing)"; return false; }
    const int ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!ch || (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16)) { err = path + ": unsupported PNG type"; return false; }
    const size_t bpp = std::max<size_t>(1, size_t(ch) * size_t(depth) / 8);
    const size_t stride = (size_t(w) * size_t(ch) * size_t(depth) + 7) / 8;
    std::vector<uint8_t> raw((stride + 1) * h);
    if (idat.size() < 6 || !inflateRaw(idat.data() + 2, idat.size() - 2, raw.data(), raw.size())) { err = path + ": broken image data"; return false; }
    std::vector<uint8_t> img(stride * h);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t ft = raw[y * (stride + 1)];
        const uint8_t* s = &raw[y * (stride + 1) + 1];
        uint8_t* o = &img[y * stride];
        const uint8_t* up = y ? &img[(y - 1) * stride] : nullptr;
        for (size_t i = 0; i < stride; ++i) {
            const int a = i >= bpp ? o[i - bpp] : 0, u = up ? up[i] : 0, c = (up && i >= bpp) ? up[i - bpp] : 0;
            int v = s[i];
            switch (ft) {
            case 1: v += a; break;
            case 2: v += u; break;
            case 3: v += (a + u) / 2; break;
            case 4: {
                const int p = a + u - c, pa = std::abs(p - a), pb = std::abs(p - u), pc = std::abs(p - c);
                v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? u : c);
                break;
            }
            default: break;
            }
            o[i] = uint8_t(v);
        }
    }
    auto sample = [&](uint32_t y, uint32_t x, int c) -> int {   // channel c of pixel x, as a raw value
        const size_t bit = (size_t(x) * size_t(ch) + size_t(c)) * size_t(depth);
        const uint8_t* r = &img[y * stride];
        if (depth == 16) return (r[bit / 8] << 8) | r[bit / 8 + 1];
        if (depth == 8) return r[bit / 8];
        return (r[bit / 8] >> (8 - depth - int(bit % 8))) & ((1 << depth) - 1);
    };
    auto to8 = [&](int v) { return depth == 16 ? v >> 8 : depth == 8 ? v : v * 255 / ((1 << depth) - 1); };
    width = int(w);
    height = int(h);
    rgba.assign(size_t(w) * h * 4, 255);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* q = &rgba[(size_t(y) * w + x) * 4];
            if (ctype == 3) {
                const size_t i = size_t(sample(y, x, 0));
                if (i * 3 + 2 < pal.size()) { q[0] = pal[i * 3]; q[1] = pal[i * 3 + 1]; q[2] = pal[i * 3 + 2]; }
                if (i < trns.size()) q[3] = trns[i];
            } else if (ctype == 0 || ctype == 4) {
                q[0] = q[1] = q[2] = uint8_t(to8(sample(y, x, 0)));
                if (ctype == 4) q[3] = uint8_t(to8(sample(y, x, 1)));
                else if (trns.size() >= 2 && sample(y, x, 0) == ((trns[0] << 8) | trns[1])) q[3] = 0;
            } else {
                for (int c = 0; c < 3; ++c) q[c] = uint8_t(to8(sample(y, x, c)));
                if (ctype == 6) q[3] = uint8_t(to8(sample(y, x, 3)));
                else if (trns.size() >= 6 && sample(y, x, 0) == ((trns[0] << 8) | trns[1]) &&
                         sample(y, x, 1) == ((trns[2] << 8) | trns[3]) && sample(y, x, 2) == ((trns[4] << 8) | trns[5])) q[3] = 0;
            }
        }
    return true;
}

} // namespace hotd
