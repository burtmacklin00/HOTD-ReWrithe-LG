// menus3: the settings' pictures on the CPU (see preview_art.h)
#include "game/preview_art.h"

#include <algorithm>
#include <cmath>

namespace hotd::game {

namespace {
// each kind's cut of the render (render pixels, 16:9) and its aim on the zombie (the picture's fractions). Blood: the
// door with the zombie and the two researchers; Models (prevbridge): Chapter 1's bridge, the whole frame
struct Cut {
    int x, y, w, h;
    float aimX, aimY;
};
constexpr Cut kCut[2] = {{200, 120, 640, 360, 0.49f, 0.44f}, {0, 0, 960, 540, 0.56f, 0.58f}};
const Cut& cutOf(int kind) { return kCut[kind == kPreviewModels ? 1 : 0]; }

float hashf(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12, h *= 0x297A2D39u, h ^= h >> 15;
    return float(h & 0xFFFFFF) / float(0x1000000);
}
float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
} // namespace

PreviewImg previewCrop(const std::vector<uint8_t>& rgb, int w, int h, int kind)
{
    PreviewImg o;
    const Cut& c = cutOf(kind);
    const int kCropX = c.x, kCropY = c.y, kCropW = c.w, kCropH = c.h;
    if (w < kCropX + kCropW || h < kCropY + kCropH || rgb.size() < size_t(w) * size_t(h) * 3)
        return o;
    o.w = kPreviewPicW, o.h = kPreviewPicH;
    o.rgba.assign(size_t(o.w) * size_t(o.h) * 4, 255);
    const float k = float(kCropW) / float(o.w);
    for (int y = 0; y < o.h; ++y)
        for (int x = 0; x < o.w; ++x) {   // (a box of the render's pixels under each one)
            const float sx0 = float(kCropX) + float(x) * k, sy0 = float(kCropY) + float(y) * k;
            float acc[3] = {}, n = 0;
            for (int yy = int(sy0); yy < int(std::ceil(sy0 + k)); ++yy)
                for (int xx = int(sx0); xx < int(std::ceil(sx0 + k)); ++xx) {
                    const uint8_t* p = &rgb[(size_t(std::min(yy, h - 1)) * size_t(w) + size_t(std::min(xx, w - 1))) * 3];
                    acc[0] += p[0], acc[1] += p[1], acc[2] += p[2], n += 1;
                }
            uint8_t* d = &o.rgba[(size_t(y) * size_t(o.w) + size_t(x)) * 4];
            for (int c = 0; c < 3; ++c) d[c] = uint8_t(std::lround(acc[c] / std::max(n, 1.0f)));
        }
    return o;
}

PreviewImg previewCropFx(const std::vector<uint8_t>& rgb, int w, int h, int kind)
{
    // (render pixels of kPreviewRenderW x kPreviewRenderH; marks: 1:1 round the zombie's face, pools: 3x the bridge's)
    struct Fx { int x, y, s; };
    const Fx c = kind == kPreviewMarks ? Fx{250, 40, 1} : Fx{350, 262, 3};
    PreviewImg o;
    const int cw = kPreviewPicW / c.s, ch = kPreviewPicH / c.s;
    if (w < c.x + cw || h < c.y + ch || rgb.size() < size_t(w) * size_t(h) * 3)
        return o;
    o.w = kPreviewPicW, o.h = kPreviewPicH;
    o.rgba.assign(size_t(o.w) * size_t(o.h) * 4, 255);
    for (int y = 0; y < o.h; ++y)
        for (int x = 0; x < o.w; ++x) {
            const uint8_t* p = &rgb[(size_t(c.y + y / c.s) * size_t(w) + size_t(c.x + x / c.s)) * 3];
            uint8_t* d = &o.rgba[(size_t(y) * size_t(o.w) + size_t(x)) * 4];
            d[0] = p[0], d[1] = p[1], d[2] = p[2];
        }
    return o;
}

uint32_t previewBloodRgb(int blood, int customRgb)
{
    if (blood == 5) {   // (Frontend::bloodColour's custom: its brightest channel at the red's level)
        const int r = (customRgb >> 16) & 255, g = (customRgb >> 8) & 255, b = customRgb & 255, m = std::max({r, g, b});
        if (m < 8)
            return 0x2A2626;
        const float k = 176.0f / float(m);
        return uint32_t(float(r) * k) << 16 | uint32_t(float(g) * k) << 8 | uint32_t(float(b) * k);
    }
    static const uint32_t kHue[5] = {0xA00C08, 0x1E8A12, 0x1630B4, 0x7C129E, 0xB09A10};
    return kHue[std::clamp(blood, 0, 4)];
}

PreviewImg previewCompose(const PreviewImg& base, uint32_t bloodRgb, const SightArt& sight, int kind)
{
    PreviewImg o = base;
    if (o.w <= 0 || o.h <= 0)
        return o;
    const float ax = cutOf(kind).aimX * float(o.w), ay = cutOf(kind).aimY * float(o.h), u = float(o.h) / 270.0f;
    auto blend = [&](int x, int y, const float c[3], float a) {
        if (x < 0 || y < 0 || x >= o.w || y >= o.h || a <= 0)
            return;
        uint8_t* d = &o.rgba[(size_t(y) * size_t(o.w) + size_t(x)) * 4];
        for (int k = 0; k < 3; ++k) d[k] = uint8_t(std::clamp(float(d[k]) * (1 - a) + c[k] * a + 0.5f, 0.0f, 255.0f));
    };
    if (bloodRgb) {
        // the burst where the shot hit: a lobed blob, drops thrown out (stretched along their flight, smaller further
        // out) and a few fine specks: a thickness field, thick = dark, thin = the colour's lighter see-through edge
        const int R = int(84 * u);
        const int x0 = int(ax) - R, y0 = int(ay) - R, n = 2 * R + 1;
        std::vector<float> t(size_t(n) * size_t(n), 0.0f);
        auto put = [&](int x, int y, float v) {
            if (x < 0 || y < 0 || x >= n || y >= n) return;
            float& q = t[size_t(y) * size_t(n) + size_t(x)];
            q = std::max(q, v);
        };
        const float br = 13.0f * u;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const float dx = float(x - R), dy = float(y - R), th = std::atan2(dy, dx);
                const float r = br * (1.0f + 0.32f * std::sin(3 * th + 1.3f) + 0.18f * std::sin(7 * th + 0.4f) + 0.1f * std::sin(11 * th));
                put(x, y, clamp01((r - std::hypot(dx, dy)) / (2.5f * u)) * 1.4f);
            }
        for (uint32_t i = 0; i < 70; ++i) {
            const float th = hashf(i, 1) * 6.2832f, far = std::pow(hashf(i, 2), 0.8f);
            const float dist = (13.0f + 64.0f * far) * u;
            const float r = std::max(0.8f, (4.4f - 3.2f * far) * (0.6f + 0.6f * hashf(i, 3))) * u;
            const float el = 1.0f + 2.5f * far;   // (stretched along the flight)
            const float cx = std::cos(th) * dist, cy = std::sin(th) * dist * 0.85f + dist * dist * 0.004f / u;
            const float ex = std::cos(th), ey = std::sin(th);
            const int ext = int(r * el) + 2;
            for (int y = int(cy) - ext; y <= int(cy) + ext; ++y)
                for (int x = int(cx) - ext; x <= int(cx) + ext; ++x) {
                    const float px = float(x) - cx, py = float(y) - cy;
                    const float a = (px * ex + py * ey) / el, b = -px * ey + py * ex;
                    put(x + R, y + R, clamp01((r - std::hypot(a, b)) / 0.9f) * (1.2f - 0.4f * far));
                }
            if (i % 3 == 0)   // (a streak back towards the hit)
                for (float s = 0; s < dist * 0.45f; s += 0.5f)
                    put(int(cx - ex * s) + R, int(cy - ey * s) + R, 0.55f * (1 - s / (dist * 0.45f)));
        }
        const float hue[3] = {float((bloodRgb >> 16) & 255), float((bloodRgb >> 8) & 255), float(bloodRgb & 255)};
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const float v = t[size_t(y) * size_t(n) + size_t(x)];
                if (v <= 0.02f) continue;
                const float k = 1.6f - 0.75f * clamp01(v - 0.3f);   // (thick: darker; thin: lighter, seen on the dark scene)
                const float c[3] = {std::min(255.0f, hue[0] * k), std::min(255.0f, hue[1] * k), std::min(255.0f, hue[2] * k)};
                blend(x0 + x, y0 + y, c, clamp01(v * 1.3f) * 0.94f);
            }
    }
    if (sight.w > 0 && sight.h > 0 && sight.dw > 0 && sight.dh > 0 && sight.rgba.size() >= size_t(sight.w) * size_t(sight.h) * 4) {
        // the sight as the game draws it in a window o.h * 2 lines high (dw x dh), its (cx, cy) on the aim
        const float sx = float(sight.w) / float(sight.dw), sy = float(sight.h) / float(sight.dh);
        const float ox = ax - float(sight.cx) / sx, oy = ay - float(sight.cy) / sy;
        for (int y = 0; y < sight.dh; ++y)
            for (int x = 0; x < sight.dw; ++x) {
                const int ix = std::min(sight.w - 1, int((float(x) + 0.5f) * sx)), iy = std::min(sight.h - 1, int((float(y) + 0.5f) * sy));
                const uint8_t* p = &sight.rgba[(size_t(iy) * size_t(sight.w) + size_t(ix)) * 4];
                const float c[3] = {float(p[0]), float(p[1]), float(p[2])};
                blend(int(ox) + x, int(oy) + y, c, float(p[3]) / 255.0f);
            }
    }
    return o;
}

} // namespace hotd::game
