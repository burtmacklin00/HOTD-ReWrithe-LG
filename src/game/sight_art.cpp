// The sights' pictures on the CPU (game/sights.h SightArt; optui 2026-10-26): moved from Hud::buildSight /
// Hud::buildPixelSight (the same pixels) so the menu's crosshair preview and the Crosshair dropdown's icons draw exactly
// what the game draws.
#include "game/sights.h"

#include "assets/sprites_pc.h"
#include "game/player_colours.h"
#include "util/png_read.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace hotd::game {

const CustomSight* findSight(const std::vector<CustomSight>& sets, const std::string& name)
{
    auto low = [](std::string s) {
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    for (const CustomSight& c : sets)
        if (low(c.name) == low(name))
            return &c;
    return nullptr;
}

namespace {
// firing range: a picture in one colour, each pixel's brightness against the brightest one kept (the dark rims stay dark)
void colourArt(std::vector<uint8_t>& px, uint32_t rgb)
{
    const float fill[3] = {float((rgb >> 16) & 255), float((rgb >> 8) & 255), float(rgb & 255)};
    int top = 1;
    for (size_t i = 0; i + 3 < px.size(); i += 4)
        if (px[i + 3]) top = std::max(top, int(std::max({px[i], px[i + 1], px[i + 2]})));
    for (size_t i = 0; i + 3 < px.size(); i += 4) {
        if (!px[i + 3]) continue;
        const float l = float(std::max({px[i], px[i + 1], px[i + 2]})) / float(top);
        for (int c = 0; c < 3; ++c) px[i + c] = uint8_t(std::min(255.0f, fill[c] * l + 0.5f));
    }
}
}

bool seatSightArt(const SeatSight& s, bool pcModels, const assets::PcSprites* pc, const std::vector<CustomSight>& sets,
                  int seat, int wh, SightArt& out, std::string* err)
{
    int style = sightStyleKept(s.style);
    if (style == kSightOff) {   // gunfb: no crosshair: no picture (not an error)
        out = SightArt{};
        return true;
    }
    if (style == 0)
        style = pcModels ? 2 : 1;
    const uint32_t rgb = sightColourRgb(s.colour);
    if (style == 1 || style == 2)
        return sightSpriteArt(pc, style == 2 ? 1 : 0, seat, wh, out, rgb) || sightSpriteArt(nullptr, 0, seat, wh, out, rgb);
    return sightPixelArt(style, s.size, s.thick, style == kSightCustom ? findSight(sets, s.custom) : nullptr, wh, seat, out, err, rgb);
}

bool sightSpriteArt(const assets::PcSprites* pc, int kind, int seat, int wh, SightArt& out, uint32_t rgb)
{
    const int p = playerArtSource(seat);
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    // the arcade style (user 2026-10-16: "exactly like the PC crosshair but keep the colour"): the PC sight's shape,
    // recoloured below in the arcade's player colours; the drawn triangles only when the PC sprites are missing
    const bool havePc = pc && pc->loaded();
    const bool pcShape = kind == 1 || (kind == 0 && havePc);
    if (pcShape) {
        if (!havePc)
            return false;
        const int W = assets::PcSprites::kWidth, H = assets::PcSprites::kHeight;
        std::vector<uint8_t> canvas(size_t(W) * H * 4, 0);
        pc->draw(canvas, 0x45E + p, W / 2, H / 2);
        int x0 = W, y0 = H, x1 = -1, y1 = -1;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (canvas[(size_t(y) * W + x) * 4 + 3]) {
                    x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
                }
        if (x1 < 0)
            return false;
        // centred on the sprite's middle (its four arrows point at the aim)
        w = x1 - x0 + 1;
        h = y1 - y0 + 1;
        px.assign(size_t(w) * h * 4, 0);
        for (int y = 0; y < h; ++y)
            std::copy_n(&canvas[(size_t(y0 + y) * W + x0) * 4], size_t(w) * 4, &px[size_t(y) * w * 4]);
        if (kind == 0) {
            // keep the PC sprite's shading (each pixel's brightness against the brightest one) in 1P red / 2P blue
            const float fill[3] = {p == 0 ? 255.0f : 40.0f, p == 0 ? 40.0f : 110.0f, p == 0 ? 30.0f : 255.0f};
            int top = 1;
            for (size_t i = 0; i < px.size(); i += 4)
                if (px[i + 3]) top = std::max(top, int(std::max({px[i], px[i + 1], px[i + 2]})));
            for (size_t i = 0; i < px.size(); i += 4) {
                if (!px[i + 3]) continue;
                const float l = float(std::max({px[i], px[i + 1], px[i + 2]})) / float(top);
                for (int c = 0; c < 3; ++c) px[i + c] = uint8_t(std::min(255.0f, fill[c] * l + 0.5f));
            }
        }
    } else if (kind == 2) {
        // the simple cross: a "+" 25 x 25 pixels (of the PC's 640 x 480), arms 3 wide with a 1-pixel dark rim, its
        // centre pixel (12, 12) the texture's middle, so drawSight puts it on the aim
        w = h = 25;
        px.assign(size_t(w) * h * 4, 0);
        const uint8_t fill[3] = {uint8_t(p == 0 ? 255 : 90), uint8_t(p == 0 ? 60 : 170), uint8_t(p == 0 ? 60 : 255)};
        auto on = [](int x, int y, int r) { return (std::abs(x - 12) <= r && std::abs(y - 12) <= 12) || (std::abs(y - 12) <= r && std::abs(x - 12) <= 12); };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                uint8_t* q = &px[(size_t(y) * w + x) * 4];
                if (on(x, y, 0) || (on(x, y, 1) && std::abs(x - 12) <= 11 && std::abs(y - 12) <= 11)) {
                    q[0] = fill[0]; q[1] = fill[1]; q[2] = fill[2]; q[3] = 255;
                } else if (on(x, y, 2)) {
                    q[0] = 30; q[1] = 0; q[2] = 10; q[3] = 255;   // dark rim (readable on bright ground)
                }
            }
    } else {
        w = h = 36;
        px.assign(size_t(w) * h * 4, 0);
        const uint8_t fill[3] = {uint8_t(p == 0 ? 255 : 40), uint8_t(p == 0 ? 40 : 110), uint8_t(p == 0 ? 30 : 255)};
        // four thin triangles pointing at the middle: base 6 wide at the edge, tip 4 pixels from the centre (feet3:
        // the PC sprites 0x45E / 0x45F's line weight; the 12-wide base was too fat, user play-test)
        auto inside = [](float x, float y) {
            const float c = 17.5f;
            for (int k = 0; k < 4; ++k) {
                // arrow k (left, right, top, bottom): d = distance from the centre along it, b = across it
                const float d = k == 0 ? c - x : k == 1 ? x - c : k == 2 ? c - y : y - c;
                const float b = k < 2 ? y - c : x - c;
                if (d >= 4.0f && d <= 16.0f && std::abs(b) <= (d - 4.0f) * 0.25f)
                    return true;
            }
            return false;
        };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                uint8_t* q = &px[(size_t(y) * w + x) * 4];
                if (inside(float(x), float(y))) {
                    q[0] = fill[0]; q[1] = fill[1]; q[2] = fill[2]; q[3] = 255;
                } else if (inside(float(x) - 1, float(y)) || inside(float(x) + 1, float(y)) ||
                           inside(float(x), float(y) - 1) || inside(float(x), float(y) + 1)) {
                    q[0] = 40; q[1] = 0; q[2] = 20; q[3] = 255;   // the dark rim the PC's sprites have too
                }
            }
    }
    if (rgb)   // (firing range: the player's chosen colour)
        colourArt(px, rgb);
    else if (seat >= 2)
        recolourRgba(px.data(), px.size() / 4, playerRecolour(seat));
    out.rgba = std::move(px);
    out.w = w, out.h = h;
    // (Hud::sightRect: the PC's 640 x 480 pixels scaled to the window's height, centred)
    const float s = float(wh) / float(assets::PcSprites::kHeight);
    out.dw = std::max(1, int(std::lround(float(w) * s))), out.dh = std::max(1, int(std::lround(float(h) * s)));
    out.cx = out.dw / 2, out.cy = out.dh / 2;
    out.linear = true;
    return true;
}

bool sightPixelArt(int style, int size, int thick, const CustomSight* custom, int wh, int seat, SightArt& out, std::string* err,
                   uint32_t rgb)
{
    style = style >= 3 && style <= kSightCapped ? style : 3;
    int drawStyle = style;
    const int p = playerArtSource(seat);
    const float sizeF = sightSizeFactor(size);
    const int half = std::max(4, int(std::lround(12.0f * float(wh) / float(assets::PcSprites::kHeight) * sizeF)));
    // the capped cross one step thinner (user 2026-10-31: its Medium, the default, = the first version's Thin)
    if (style == kSightCapped) thick = std::max(thick - 1, 0);
    const int t = std::max(thick == 0 ? 1 : 2, int(std::lround(sightThicknessPx(thick) * float(wh) / 1080.0f)));
    bool ok = true;
    std::vector<uint8_t> pic[2];
    int cw[2] = {}, ch[2] = {};
    if (style == kSightCustom) {
        // the named set's pictures (a missing / broken set: the simple cross)
        std::string e = "not found";
        ok = custom != nullptr && hotd::readPngRgba(custom->p1, cw[0], ch[0], pic[0], e);
        if (ok && !custom->p2.empty() && !custom->tint)
            ok = hotd::readPngRgba(custom->p2, cw[1], ch[1], pic[1], e);
        else if (ok) {
            cw[1] = cw[0]; ch[1] = ch[0]; pic[1] = pic[0];
        }
        if (ok && custom->tint) {
            const int fill[3] = {p == 0 ? 255 : 90, p == 0 ? 60 : 170, p == 0 ? 60 : 255};
            for (size_t i = 0; i < pic[p].size(); i += 4)
                for (int k = 0; k < 3; ++k) pic[p][i + k] = uint8_t(pic[p][i + k] * fill[k] / 255);
        }
        if (!ok) {
            if (err) *err = e;
            drawStyle = 3;
        }
    }
    const bool isCustom = drawStyle == kSightCustom;
    const float scale = float(wh) / 1080.0f * sizeF;
    std::vector<uint8_t> px;
    int tw, th;
    out.linear = false;
    if (isCustom) {   // drawn size: 1 picture pixel = 1 window pixel at 1080 lines (medium size)
        out.dw = std::max(1, int(std::lround(float(cw[0]) * scale)));
        out.dh = std::max(1, int(std::lround(float(ch[0]) * scale)));
        out.cx = out.dw / 2;
        out.cy = out.dh / 2;
        out.linear = out.dw != cw[0] || out.dh != ch[0];
        px = pic[p];
        tw = cw[p];
        th = ch[p];
    } else {
        out.dw = out.dh = 2 * half + 1;
        out.cx = out.cy = half;
        const int N = out.dw;
        tw = th = N;
        px.assign(size_t(N) * N * 4, 0);
        const uint8_t fill[3] = {uint8_t(p == 0 ? 255 : 90), uint8_t(p == 0 ? 60 : 170), uint8_t(p == 0 ? 60 : 255)};
        auto put = [&](int xx, int yy, const uint8_t* c) {
            uint8_t* q = &px[(size_t(yy) * N + xx) * 4];
            q[0] = c[0]; q[1] = c[1]; q[2] = c[2]; q[3] = 255;
        };
        const uint8_t rim[3] = {20, 0, 8};
        if (drawStyle == 3) {
            for (int yy = 0; yy < N; ++yy)
                for (int xx = 0; xx < N; ++xx) {
                    const int a0 = half - (t - 1) / 2, a1 = half + t / 2;   // the line's pixels across
                    const bool v = xx >= a0 && xx <= a1, h = yy >= a0 && yy <= a1;
                    const bool rv = xx >= a0 - 1 && xx <= a1 + 1 && yy >= 1 && yy < N - 1;   // with the rim
                    const bool rh = yy >= a0 - 1 && yy <= a1 + 1 && xx >= 1 && xx < N - 1;
                    if ((v && yy >= 1 && yy < N - 1) || (h && xx >= 1 && xx < N - 1))
                        put(xx, yy, fill);
                    else if (rv || rh)
                        put(xx, yy, rim);
                }
        } else {
            const float c = float(half) + ((t % 2 == 0) ? 0.5f : 0.0f);   // the line's middle
            const float rg = float(half - 1);                              // the shape's reach (the rim fits in)
            const float ht = std::max(float(t) / 2.0f, 0.5f);
            const float dr = float(t) * 0.5f + 1.0f;                       // a dot's radius
            std::vector<uint8_t> mask(size_t(N) * N, 0);
            for (int yy = 0; yy < N; ++yy)
                for (int xx = 0; xx < N; ++xx) {
                    const float dx = float(xx) - c, dy = float(yy) - c, ax = std::fabs(dx), ay = std::fabs(dy);
                    const float d = std::sqrt(dx * dx + dy * dy);
                    bool on = false;
                    switch (drawStyle) {
                    case 4: {   // ring + dot
                        const float rr = rg * 0.8f, band = std::max(ht, 0.75f);
                        on = std::fabs(d - rr) <= band || d <= dr;
                        break;
                    }
                    case 6: {   // corner brackets and a centre dot
                        const float rr = rg * 0.85f, len = rr * 0.5f;
                        on = (ax >= rr - ht && ax <= rr + ht && ay >= rr - len && ay <= rr + ht) ||
                             (ay >= rr - ht && ay <= rr + ht && ax >= rr - len && ax <= rr + ht) || d <= dr;
                        break;
                    }
                    case kSightCapped: {   // capped cross (user 2026-10-31): thick arms, each ending in a thin bar across
                        const float hb = ht * 1.6f + 0.5f;   // the arms' half width (thicker than the caps)
                        const float cw = rg * 0.45f;          // a cap's half length
                        const float ce = rg - 2.0f * ht;      // a cap's inner edge (the cap: the line's thickness)
                        on = (ax <= hb && ay <= rg) || (ay <= hb && ax <= rg) || (ay >= ce && ay <= rg && ax <= cw) ||
                             (ax >= ce && ax <= rg && ay <= cw);
                        break;
                    }
                    default: {   // 7: gap cross
                        const float gap = rg * 0.25f;
                        on = (ax <= ht && ay >= gap && ay <= rg) || (ay <= ht && ax >= gap && ax <= rg);
                        break;
                    }
                    }
                    mask[size_t(yy) * N + xx] = on;
                }
            for (int yy = 0; yy < N; ++yy)
                for (int xx = 0; xx < N; ++xx) {
                    if (mask[size_t(yy) * N + xx]) { put(xx, yy, fill); continue; }
                    const bool nb = (xx > 0 && mask[size_t(yy) * N + xx - 1]) || (xx + 1 < N && mask[size_t(yy) * N + xx + 1]) ||
                                    (yy > 0 && mask[size_t(yy - 1) * N + xx]) || (yy + 1 < N && mask[size_t(yy + 1) * N + xx]);
                    if (nb) put(xx, yy, rim);
                }
        }
    }
    if (rgb && !(isCustom && custom && !custom->tint))   // (firing range: the chosen colour; a set's own p1 / p2: as they are)
        colourArt(px, rgb);
    else if (seat >= 2)
        recolourRgba(px.data(), px.size() / 4, playerRecolour(seat));
    out.rgba = std::move(px);
    out.w = tw, out.h = th;
    return ok;
}

} // namespace hotd::game
