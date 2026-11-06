// The arcade's 2D layer compositor (sprites_arcade.h). UNVERIFIED (teaser): the cell / tile / palette layout is checked
// against the ROM's sprites (tools/sprites_arcade.py draws them: the Chariot's card, the HUD), the window masks and
// the scroll registers follow MAME's System 24 tile chip as far as the game uses them; no MAME frame compared yet.
#include "assets/sprites_arcade.h"

#include "render/gamma.h"

#include <array>
#include <cstring>

namespace hotd::assets {

namespace {
inline uint16_t rd16(const uint8_t* p, uint32_t off) { return uint16_t(p[off] | p[off + 1] << 8); }

// perf (2026-10-21): the same pictures as before, faster (the HUD composed the screen up to seven times a frame, ~1 ms):
// straight into the output (a later layer's pixel replaces an earlier one's; was a second buffer copied at the end),
// each cell's lookups once per run of its pixels instead of per pixel, the gamma from a table, and the widescreen
// side pictures in one pass (each cell's pixels go to the picture of its side, as three separate passes did).
// out[n]: the pictures (kWidth x kHeight x 4, cleared here); side null: everything into out[0]; want >= 0: only the
// cells of that side, into out[0]; want -1: every cell into out[its side] (sides 0..count-1). any[n]: drawn into out[n]
void composeInto(const uint8_t* tileRam, const uint8_t* charRam, const uint8_t* palette, bool high, const uint8_t* side,
                 int want, std::vector<uint8_t>* const* out, bool* any, int count)
{
    constexpr int kWidth = Arcade2D::kWidth, kHeight = Arcade2D::kHeight;
    uint8_t* px[3] = {};
    for (int n = 0; n < count; ++n) {
        out[n]->assign(size_t(kWidth) * kHeight * 4, 0);
        px[n] = out[n]->data();
        any[n] = false;
    }
    // MAME's output: the colour table (taken as linear here), then its gamma max(v - 64, 0) x 255 / 191;
    // checked against MAME snapshots (bullet 189,121,36; life bar 20,0,255). Dark colours become black,
    // and stay opaque (only pen 0 is transparent: title-2, the title logo's dark shading and the
    // figure showed the 3D through, MAME attract_us snap 0058 has them black)
    // bugs4 (#23): the 64 is Video > Black Level (render/gamma.h m2Curve; 64: the table as before), a table per level
    static const auto gms = [] {
        std::array<std::array<uint8_t, 32>, kBlackMax + 1> all{};
        for (int b = 0; b <= kBlackMax; ++b)
            for (uint32_t c5 = 0; c5 < 32; ++c5)
                all[size_t(b)][c5] = uint8_t(m2Curve(int(c5 * 255 / 31), b));
        return all;
    }();
    const std::array<uint8_t, 32>& gm = gms[size_t(blackLevel())];
    for (int layer = 3; layer >= 0; --layer) {
        const uint16_t pri = high ? 0x8000 : 0;
        const uint16_t hs = rd16(tileRam, 0xA000 + 2 * layer), vs = rd16(tileRam, 0xA008 + 2 * layer);
        if (vs & 0x8000)
            continue;   // the layer is off
        const uint32_t map = uint32_t(layer) * 0x2000, mask = layer < 2 ? 0xC000u : 0xD000u;
        const bool window = layer & 1;
        const uint8_t* const sideLayer = side ? side + uint32_t(layer) * 4096 : nullptr;
        for (int sy = 0; sy < kHeight; ++sy) {
            const int ty = (sy + (vs & 0x1FF)) & 511;
            const uint32_t maskLine = mask + uint32_t(sy) * 8;
            const uint32_t cellRow = uint32_t(ty >> 3) * 64, charRow = uint32_t(ty & 7) * 4;
            const size_t lineAt = size_t(sy) * kWidth * 4;
            // the line's 64 cells once: their output (-1: not drawn), palette and the tile's line (pixels 0-3, 4-7)
            struct CellLine { int to; uint32_t colour; uint16_t words[2]; };
            CellLine cl[64];
            bool lineAny = false;
            for (uint32_t c = 0; c < 64; ++c) {
                const uint32_t ci = cellRow + c;
                const uint16_t cell = rd16(tileRam, map + ci * 2);
                CellLine& e = cl[c];
                e.to = -1;
                if ((cell & 0x8000) == pri) {
                    if (!sideLayer)
                        e.to = 0;
                    else if (want >= 0)
                        e.to = sideLayer[ci] == want ? 0 : -1;
                    else
                        e.to = sideLayer[ci] < count ? int(sideLayer[ci]) : -1;
                }
                if (e.to >= 0) {
                    const uint32_t tile = cell & 0x3FFF;
                    e.colour = (cell >> 7) & 0xFF;
                    e.words[0] = rd16(charRam, tile * 32 + charRow);
                    e.words[1] = rd16(charRam, tile * 32 + charRow + 2);
                    if ((e.words[0] | e.words[1]) == 0)
                        e.to = -1;   // (all pen 0: nothing)
                }
                lineAny |= e.to >= 0;
            }
            if (!lineAny)
                continue;
            for (int cx = 0; cx < 64; ++cx) {
                // the mask is per screen cell column (64 bits a line)
                const int scx = cx;
                if (scx * 8 >= kWidth)
                    break;
                const bool bit = (rd16(tileRam, maskLine + uint32_t(scx >> 4) * 2) >> (15 - (scx & 15))) & 1;
                if (bit != window)
                    continue;
                const int tx0 = (scx * 8 + (hs & 0x1FF)) & 511;
                if (cl[tx0 >> 3].to < 0 && cl[((tx0 + 7) & 511) >> 3].to < 0)
                    continue;   // (the one or two cells under these 8 pixels draw nothing)
                for (int k = 0; k < 8; ++k) {
                    const int sx = scx * 8 + k;
                    const int tx = (sx + (hs & 0x1FF)) & 511;
                    const CellLine& e = cl[tx >> 3];
                    if (e.to < 0)
                        continue;
                    const uint16_t w = e.words[(tx & 7) >> 2];
                    const uint32_t c = (w >> (12 - 4 * (tx & 3))) & 15;
                    if (c == 0)
                        continue;   // pen 0: transparent
                    const uint16_t col = rd16(palette, (e.colour * 16 + c) * 2);
                    uint8_t* const p = px[e.to] + lineAt + size_t(sx) * 4;
                    p[0] = gm[col & 31];
                    p[1] = gm[col >> 5 & 31];
                    p[2] = gm[col >> 10 & 31];
                    p[3] = 255;
                    any[e.to] = true;
                }
            }
        }
    }
}
} // namespace

bool Arcade2D::compose(const uint8_t* tileRam, const uint8_t* charRam, const uint8_t* palette, bool high,
                       std::vector<uint8_t>& rgba, const uint8_t* side, int want)
{
    std::vector<uint8_t>* out[1] = {&rgba};
    bool any[1] = {false};
    composeInto(tileRam, charRam, palette, high, side, side ? want : 0, out, any, 1);
    return any[0];
}

void Arcade2D::composeSides(const uint8_t* tileRam, const uint8_t* charRam, const uint8_t* palette, bool high,
                            const uint8_t* side, std::vector<uint8_t> (&rgba)[3], bool (&any)[3])
{
    std::vector<uint8_t>* out[3] = {&rgba[0], &rgba[1], &rgba[2]};
    composeInto(tileRam, charRam, palette, high, side, -1, out, any, 3);
}

} // namespace hotd::assets
