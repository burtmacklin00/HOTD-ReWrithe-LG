// The players' colours (docs/coop4.md): P1 red and P2 blue (the arcade's guns, drawn in each piece of art's own
// shades), P3 green and P4 yellow for the experimental 4P mode. Players 3 / 4 never get edited art: their crosshairs and
// fire lamps are player 1's / 2's own art recoloured in code when it is built (util/recolour.h, the step texture packs
// use too).
#pragma once

#include "util/recolour.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hotd::game {

// the colour a player's art is tinted to (P1 / P2: the reference shades of their art; P3 green, P4 yellow)
inline constexpr uint8_t kPlayerRgb[4][3] = {{255, 60, 60}, {90, 170, 255}, {70, 235, 70}, {255, 225, 40}};

// the art players 3 / 4 are made from: player 2's for both (its blue flames and sight separate cleanly from the brass /
// dark parts by hue; player 1's red flames do not from the brass), players 1 / 2 their own
inline int playerArtSource(int p) { return p >= 2 ? 1 : p; }

// the colour ramps of players 3 / 4 (user 2026-10-20: "proper shades", not a flat recolour): shadow, deep, mid, hot,
// white-hot core, hand-tuned to read as green / amber-yellow next to player 1's red and player 2's blue
inline constexpr uint8_t kPlayerRamp[2][5][3] = {
    {{4, 24, 8}, {14, 88, 30}, {40, 176, 56}, {140, 242, 112}, {236, 255, 226}},     // P3 green
    {{44, 26, 0}, {146, 92, 0}, {236, 180, 16}, {255, 230, 84}, {255, 252, 216}},    // P4 yellow (mid less orange: 2026-10-20)
};

// player 2's hit notices (the blood splash / claw marks) in the 3-4 player mode: the red art through a blue ramp
// (user 2026-10-26), shadow .. core, in the shades of player 2's flames
inline constexpr uint8_t kPlayer2BloodRamp[5][3] = {{2, 8, 36}, {12, 36, 120}, {36, 100, 225}, {120, 186, 255}, {228, 240, 255}};

// the recolour of that art for player p: player 2's blue texels through the seat's ramp (none for players 1 / 2)
// "2P" label can be drawn in the other player's colours: the label's coloured texels (red-dominant for player 1's art,
// blue-dominant for player 2's; the dark rims and white cores are neither and stay) each take the colour of the other
// label's coloured texel of the same brightness rank (dark .. bright), the way players 3 / 4's digit takes the "2"'s
// shades: the other label's own colours, none made up. A texel is coloured when its channel beats the other two by 16
inline bool artTexelColoured(const uint8_t* c, int art)
{
    const int k = art == 0 ? 0 : 2;
    return c[3] != 0 && c[k] > c[1] + 16 && c[k] > c[2 - k] + 16;
}

// px: W x H RGBA, its rectangle (x0, y0, w, h) recoloured from player `from`'s art (0 / 1) to the colours of `ref`
// (refTexels RGBA texels of player `to`'s art); false: nothing to do (no coloured texels in one of them)
inline bool rankRecolour(uint8_t* px, int W, int H, int x0, int y0, int w, int h, int from, const uint8_t* ref, size_t refTexels, int to)
{
    auto level = [](const uint8_t* c) { return c[0] * 299 + c[1] * 587 + c[2] * 114; };
    std::vector<const uint8_t*> r;
    for (size_t i = 0; i < refTexels; ++i)
        if (artTexelColoured(ref + i * 4, to))
            r.push_back(ref + i * 4);
    std::vector<uint8_t*> mine;
    for (int y = std::max(0, y0); y < std::min(H, y0 + h); ++y)
        for (int x = std::max(0, x0); x < std::min(W, x0 + w); ++x)
            if (uint8_t* c = &px[(size_t(y) * size_t(W) + size_t(x)) * 4]; artTexelColoured(c, from))
                mine.push_back(c);
    if (r.empty() || mine.empty())
        return false;
    std::stable_sort(r.begin(), r.end(), [&](const uint8_t* a, const uint8_t* b) { return level(a) < level(b); });
    std::stable_sort(mine.begin(), mine.end(), [&](const uint8_t* a, const uint8_t* b) { return level(a) < level(b); });
    for (size_t i = 0; i < mine.size(); ++i)
        std::copy_n(r[i * r.size() / mine.size()], 3, mine[i]);
    return true;
}

// the recolour of that art for player p: player 2's blue texels through the seat's ramp (none for players 1 / 2)
inline Recolour playerRecolour(int p)
{
    Recolour r;
    if (p >= 2) {
        r.op = Recolour::Ramp;
        r.select = Recolour::BlueDominant;
        for (int k = 0; k < 5; ++k)
            for (int c = 0; c < 3; ++c)
                r.ramp[k][c] = kPlayerRamp[p - 2][k][c];
    }
    return r;
}

} // namespace hotd::game
