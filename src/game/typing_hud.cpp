// The Typing of the Dead 0's HUD (typing_hud.h). Placeholder look: SDL's 8 x 8 debug font, drawn boxes.
#include "game/typing_hud.h"

#include "game/lang.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace hotd::game {

namespace {
constexpr size_t kWrapAt = 22;   // a prompt longer than this goes onto two lines (at the space nearest its middle)
constexpr uint32_t kFontRom = 0x2EF3870, kCharRam = 0x1080000, kFontPalette = 0x168A0;

// the game's 8 x 8 text font (TypingHud::loadFont): each glyph's pens, the pens' brightness (palette 0 through the
// MAME gamma of sprites_arcade.cpp), which glyphs have any pixel
struct GameFont {
    bool loaded = false;
    uint8_t pen[128][8][8] = {};
    uint8_t lum[16] = {};
    bool has[128] = {};
};
GameFont gFont;

// the box's colours. totd4 (the user's reference, TotD 2000 on the PC): every box green, yellow once its zombie is at
// you (or a thrown thing, a boss sentence running out); the word being typed has red corner marks (labelImage)
// totd7 (user 2026-11-05: "the red triangles / border burst look vibe coded, the locked box needs a UI rework"): the word
// being typed has one solid border in the game's own accent red (the red of its "1P" / HOLD YOUR FIRE! text), a pixel
// thicker, drawn over the others; no corner marks, no burst, no flashing. A quick key (G / H) is a small key cap with a
// yellow border, as the PC TotD's one-key prompts
void borderOf(const Typing::Label& l, uint8_t c[3])
{
    if (l.locked) { c[0] = 225, c[1] = 35, c[2] = 25; return; }       // being typed: the game's red
    if (l.quick || l.danger) { c[0] = 250, c[1] = 220, c[2] = 40; return; }   // a quick key; at you: yellow
    c[0] = 60, c[1] = 220, c[2] = 70;                                // green
}

// a kill's rank letter's colour (TotD: "RANK" in white, the letter coloured: A light blue, B green ...)
void rankColour(char g, uint8_t c[3])
{
    switch (g) {
    case 'S': c[0] = 255, c[1] = 200, c[2] = 40; break;    // gold
    case 'A': c[0] = 90, c[1] = 200, c[2] = 255; break;    // light blue
    case 'B': c[0] = 80, c[1] = 220, c[2] = 90; break;     // green
    case 'C': c[0] = 235, c[1] = 235, c[2] = 225; break;   // white
    case 'D': c[0] = 150, c[1] = 150, c[2] = 150; break;   // grey
    default: c[0] = 200, c[1] = 60, c[2] = 50; break;      // F: red
    }
}
} // namespace

void TypingHud::loadFont(const Runtime& rt)
{
    if (gFont.loaded)
        return;
    port::Mem& mem = rt.mem;
    const uint32_t src = mem.mapped(kFontRom) ? kFontRom : mem.mapped(kCharRam) ? kCharRam : 0;
    if (!src || !mem.mapped(kFontPalette))
        return;
    // the tile layout of sprites_arcade.cpp: a row = 4 bytes (two little-endian words), pixel k of the row = the
    // nibble (word k / 4) >> (12 - 4 (k & 3))
    for (uint32_t g = 0; g < 128; ++g)
        for (uint32_t y = 0; y < 8; ++y) {
            const uint32_t row = src + g * 32 + y * 4;
            const uint16_t w[2] = {mem.at<uint16_t>(row), mem.at<uint16_t>(row + 2)};
            for (uint32_t x = 0; x < 8; ++x) {
                const uint8_t p = uint8_t((w[x >> 2] >> (12 - 4 * (x & 3))) & 15);
                gFont.pen[g][y][x] = p;
                gFont.has[g] = gFont.has[g] || p != 0;
            }
        }
    for (uint32_t c = 0; c < 16; ++c) {
        const uint16_t col = mem.at<uint16_t>(kFontPalette + c * 2);
        int m = 0;
        for (int sh : {0, 5, 10}) {
            const int v = int(((col >> sh) & 31) * 255 / 31) - 64;
            m = std::max(m, v > 0 ? v * 255 / 191 : 0);
        }
        gFont.lum[c] = uint8_t(m);
    }
    gFont.has[' '] = false;
    gFont.loaded = true;
    if (std::getenv("HOTD_TOTD_FONT")) {   // (developer: the glyphs the game has, as text)
        std::printf("TOTD font: game glyphs for:");
        for (int c = 33; c < 127; ++c)
            if (gFont.has[c]) std::printf("%c", char(c));
        std::printf("\nTOTD font: missing:");
        for (int c = 33; c < 127; ++c)
            if (!gFont.has[c]) std::printf("%c", char(c));
        std::printf("\n");
        for (const char c : std::string(std::getenv("HOTD_TOTD_FONT"))) {
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x)
                    std::printf("%c", gFont.pen[uint8_t(c)][y][x] ? char('0' + gFont.pen[uint8_t(c)][y][x] % 10) : '.');
                std::printf("\n");
            }
            std::printf("\n");
        }
    }
}

bool TypingHud::hasGlyph(char c)
{
    return gFont.loaded && uint8_t(c) < 128 && gFont.has[uint8_t(c)];
}

std::vector<uint8_t> TypingHud::labelImage(const Typing::Label& l, int scale, int& w, int& h)
{
    // the lines: one, or two split at the space nearest the middle
    std::vector<std::pair<size_t, size_t>> lines;   // [begin, end) in the prompt
    const std::string& s = l.text;
    if (s.size() > kWrapAt) {
        size_t best = std::string::npos;
        for (size_t i = 1; i + 1 < s.size(); ++i)
            if (s[i] == ' ' && (best == std::string::npos ||
                                std::abs(int(i) - int(s.size() / 2)) < std::abs(int(best) - int(s.size() / 2))))
                best = i;
        if (best != std::string::npos)
            lines = {{0, best + 1}, {best + 1, s.size()}};   // (the space stays at the end of the first line: it is typed)
    }
    if (lines.empty())
        lines = {{0, s.size()}};
    const int ch = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    // totd4: a kill's rank (TotD's "RANK A" over the kill: no box, "RANK" in white, the letter in its colour)
    const bool rank = l.kind == 4;
    // totd4 (the user's reference, TotD 2000 on the PC): a prompt that fits one line shows two rows, the prompt and
    // under it the letters still to type in their places (the typed ones gone from it); a longer one wraps as before
    // (typed letters red). m: the margin for the locked word's corner marks and a letter's hit spikes
    // (qa1: not a one-letter word: a frog's / a bat's "e" stood twice in a tall box, "e" over "e")
    const bool twoRow = !rank && lines.size() == 1 && s.size() > 1;
    // (totd5, the feel report: TotD's boxes have thick borders and a lock-on burst: border 2, a wider margin for the
    // burst's blades)
    // (totd7: no corner marks / burst any more: no margin; the locked box's border one pixel thicker; a quick key a
    // square key cap, its letter in capitals)
    // totd7 (user 2026-11-05: "the borders look a little too fat, make them thinner; maybe a pixel shadow"): every
    // box's border one game pixel (was 2, the locked one 3: the locked box is told by its colour), whole pixels at every
    // size (the image is scaled up by whole steps); TypingHud::shadow: a one-pixel dark drop shadow down-right of the
    // box (off by default: typing.ini box_shadow, HOTD_TOTD_SHADOW=1)
    const int m = 0, sh = rank ? 0 : shadow ? 1 : 0;
    const int pad = rank ? 1 : l.quick ? 3 : 4, gap = 3, border = rank ? 0 : 1, bar = l.window >= 0 ? 3 : l.attack >= 0 ? 2 : 0;
    // (the bar: a boss sentence's window, or an enemy's attack timer: the time before it may strike, TotD's)
    const float barLeft = l.window >= 0 ? l.window : l.attack;
    const std::string shown = rank ? "RANK " + s : l.quick ? std::string(1, char(std::toupper(static_cast<unsigned char>(s.empty() ? '?' : s[0])))) : s;
    if (rank)
        lines = {{0, shown.size()}};
    size_t cols = 0;
    for (const auto& [a, b] : lines) cols = std::max(cols, b - a);
    std::string extra = l.left > 0 ? " +" + std::to_string(l.left) : std::string();   // prompts still to come
    const int rows = twoRow ? 2 : int(lines.size());
    const int boxW = int(cols + extra.size()) * ch + 2 * pad + 2 * border;
    const int boxH = rows * (ch + gap) - gap + 2 * pad + 2 * border + bar + (bar ? 2 : 0);
    const int bw = boxW + 2 * m + sh, bh = boxH + 2 * m + sh;
    w = bw * scale, h = bh * scale;
    std::vector<uint8_t> out;
    SDL_Surface* surf = SDL_CreateSurface(bw, bh, SDL_PIXELFORMAT_RGBA32);
    if (!surf) { w = h = 0; return out; }
    struct Glyph { int x, y; char c; uint8_t col[3]; };
    std::vector<Glyph> glyphs;
    if (SDL_Renderer* r = SDL_CreateSoftwareRenderer(surf)) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        SDL_RenderClear(r);
        uint8_t bc[3];
        borderOf(l, bc);
        if (!rank) {
            if (sh) {   // (the drop shadow: the box's shape one pixel down-right, dark; user 2026-11-05: on, "opacity
                        // slightly down, a hard soft shadow": a hard edge at about 55 % instead of 78 %)
                SDL_SetRenderDrawColor(r, 0, 0, 0, 140);
                SDL_FRect s2{float(m + sh), float(m + sh), float(boxW), float(boxH)};
                SDL_RenderFillRect(r, &s2);
            }
            SDL_SetRenderDrawColor(r, bc[0], bc[1], bc[2], 255);
            SDL_FRect box{float(m), float(m), float(boxW), float(boxH)};
            SDL_RenderFillRect(r, &box);
            SDL_SetRenderDrawColor(r, 6, 6, 8, 225);
            SDL_FRect in{float(m + border), float(m + border), float(boxW - 2 * border), float(boxH - 2 * border)};
            SDL_RenderFillRect(r, &in);
            // (totd7, user 2026-11-05: the red corner triangles / lock-on burst and the letters' red spikes are gone:
            // the locked box is told by its solid red border alone, borderOf)
        }
        char one[2] = {0, 0};
        const auto put = [&](float x, float y, char c, const uint8_t col[3]) {
            SDL_SetRenderDrawColor(r, col[0], col[1], col[2], 255);
            one[0] = c;
            if (hasGlyph(c))   // the game's own glyph (drawn into the picture after the renderer, below)
                glyphs.push_back({int(x), int(y), c, {col[0], col[1], col[2]}});
            else
                SDL_RenderDebugText(r, x, y, one);
        };
        const uint8_t white[3] = {240, 240, 230};
        for (size_t li = 0; li < lines.size(); ++li) {
            const auto [a, b] = lines[li];
            const float y = float(m + border + pad) + float(li) * float(ch + gap);
            const float x0 = float(m + border + pad) + float((cols - (b - a)) * ch) / 2.0f;
            for (size_t i = a; i < b; ++i) {
                const float x = x0 + float((i - a) * ch);
                uint8_t col[3] = {240, 240, 230};
                if (rank) {
                    if (i + 1 == b) rankColour(shown[i], col);
                    put(x, y, shown[i], col);
                    continue;
                }
                if (l.quick) {   // (totd7: the key, white, in capitals)
                    put(x, y, shown[0], white);
                    continue;
                }
                if (twoRow) {   // the top row: the prompt as it is, all white
                    put(x, y, s[i], white);
                    if (i < l.typed) continue;   // the second row: what is left, in place
                    if (i == l.typed && l.locked)
                        col[0] = 255, col[1] = 230, col[2] = 60;   // the next letter: yellow
                    put(x, y + float(ch + gap), s[i], col);
                    continue;
                }
                if (i < l.typed)
                    col[0] = 230, col[1] = 60, col[2] = 40;             // typed: red (the shots landed)
                else if (i == l.typed && l.locked)
                    col[0] = 255, col[1] = 230, col[2] = 60;            // the next letter: yellow, underlined
                put(x, y, s[i] == ' ' && i < l.typed ? '_' : s[i], col);   // (a typed space shows as _)
                if (i == l.typed && l.locked) {
                    SDL_FRect u{x, y + float(ch), float(ch), 1};
                    SDL_RenderFillRect(r, &u);
                }
            }
            if (li == 0 && !extra.empty()) {
                SDL_SetRenderDrawColor(r, 150, 150, 150, 255);
                SDL_RenderDebugText(r, x0 + float((b - a) * ch), y, extra.c_str());
            }
        }
        if (bar) {   // the time left (green, then yellow, red at the end)
            const float y = float(m + boxH - border - bar - 1);
            SDL_SetRenderDrawColor(r, 60, 20, 20, 255);
            SDL_FRect back{float(m + border + pad), y, float(boxW - 2 * (border + pad)), float(bar)};
            SDL_RenderFillRect(r, &back);
            SDL_SetRenderDrawColor(r, barLeft > 0.6f ? 90 : barLeft > 0.3f ? 230 : 255, barLeft > 0.6f ? 220 : barLeft > 0.3f ? 200 : 60, 40, 255);
            SDL_FRect left{back.x, y, back.w * std::clamp(barLeft, 0.0f, 1.0f), float(bar)};
            SDL_RenderFillRect(r, &left);
        }
        SDL_RenderPresent(r);
        SDL_DestroyRenderer(r);
    }
    // the game's glyphs: each pen's brightness times the letter's colour; dark pens (the font's shading) stay opaque,
    // pen 0 lets the box through
    if (SDL_LockSurface(surf)) {
        uint8_t* px = static_cast<uint8_t*>(surf->pixels);
        for (const Glyph& g : glyphs)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    const uint8_t p = gFont.pen[uint8_t(g.c)][y][x];
                    const int X = g.x + x, Y = g.y + y;
                    if (!p || X < 0 || Y < 0 || X >= surf->w || Y >= surf->h)
                        continue;
                    uint8_t* d = px + size_t(Y) * size_t(surf->pitch) + size_t(X) * 4;
                    for (int k = 0; k < 3; ++k)
                        d[k] = uint8_t(int(g.col[k]) * int(gFont.lum[p]) / 255);
                    d[3] = 255;
                }
        SDL_UnlockSurface(surf);
    }
    // scaled up (nearest) into the picture drawn 1:1
    out.assign(size_t(w) * size_t(h) * 4, 0);
    const uint8_t* px = static_cast<const uint8_t*>(surf->pixels);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            std::copy_n(px + size_t(y / scale) * size_t(surf->pitch) + size_t(x / scale) * 4, 4, &out[(size_t(y) * size_t(w) + size_t(x)) * 4]);
    SDL_DestroySurface(surf);
    return out;
}

// totd6 (the PC TotD's HUD: "PERFECT 32" in the top left, a gold word with a dark edge): a line of text in the
// game's own 8 x 8 font, no box; a character the font lacks from SDL's
std::vector<uint8_t> TypingHud::textImage(const std::string& s, const uint8_t col[3], int scale, int& w, int& h)
{
    const int ch = 8, m = 1;   // (m: the outline)
    const int bw = int(s.size()) * ch + 2 * m, bh = ch + 2 * m;
    std::vector<uint8_t> mask(size_t(bw) * size_t(bh), 0);   // 0 none, else the pen's brightness + 1
    SDL_Surface* fb = nullptr;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        const int x0 = m + int(i) * ch;
        if (hasGlyph(c)) {
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    if (const uint8_t p = gFont.pen[uint8_t(c)][y][x])
                        mask[size_t(m + y) * size_t(bw) + size_t(x0 + x)] = uint8_t(std::max(1, int(gFont.lum[p])));
        } else if (c != ' ') {   // (SDL's 8 x 8 glyph)
            if (!fb) fb = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32);
            SDL_Renderer* r = fb ? SDL_CreateSoftwareRenderer(fb) : nullptr;
            if (!r) continue;
            SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
            SDL_RenderClear(r);
            SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
            const char one[2] = {c, 0};
            SDL_RenderDebugText(r, 0, 0, one);
            SDL_RenderPresent(r);
            SDL_DestroyRenderer(r);
            if (SDL_LockSurface(fb)) {
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        if (static_cast<const uint8_t*>(fb->pixels)[size_t(y) * size_t(fb->pitch) + size_t(x) * 4 + 3])
                            mask[size_t(m + y) * size_t(bw) + size_t(x0 + x)] = 255;
                SDL_UnlockSurface(fb);
            }
        }
    }
    if (fb) SDL_DestroySurface(fb);
    w = bw * scale, h = bh * scale;
    std::vector<uint8_t> out(size_t(w) * size_t(h) * 4, 0);
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            const uint8_t v = mask[size_t(y) * size_t(bw) + size_t(x)];
            uint8_t px[4] = {0, 0, 0, 0};
            if (v)
                for (int k = 0; k < 3; ++k) px[k] = uint8_t(int(col[k]) * int(v) / 255), px[3] = 255;
            else   // the dark edge round the letters
                for (int dy = -1; dy <= 1 && !px[3]; ++dy)
                    for (int dx = -1; dx <= 1 && !px[3]; ++dx) {
                        const int X = x + dx, Y = y + dy;
                        if (X >= 0 && Y >= 0 && X < bw && Y < bh && mask[size_t(Y) * size_t(bw) + size_t(X)])
                            px[0] = 20, px[1] = 12, px[2] = 6, px[3] = 230;
                    }
            for (int sy = 0; sy < scale; ++sy)
                for (int sx = 0; sx < scale; ++sx)
                    std::copy_n(px, 4, &out[(size_t(y * scale + sy) * size_t(w) + size_t(x * scale + sx)) * 4]);
        }
    return out;
}

TypingHud::Slot* TypingHud::slot(size_t i)
{
    while (slots_.size() <= i)
        slots_.emplace_back();
    Slot& s = slots_[i];
    if (!s.ready) {
        std::string err;
        if (!s.ov.init(err))
            return nullptr;
        s.ready = true;
    }
    return &s;
}

void TypingHud::shutdown()
{
    for (Slot& s : slots_)
        if (s.ready) s.ov.shutdown();
    slots_.clear();
    if (lineReady_) line_.shutdown();
    if (resultsReady_) results_.shutdown();
    if (perfectReady_) perfect_.shutdown();
    if (liveReady_) live_.shutdown();
    if (drillReady_) drill_.shutdown();
    drillReady_ = false;
    drillKey_.clear();
    liveReady_ = false;
    liveKey_.clear();
    lineReady_ = resultsReady_ = perfectReady_ = false;
    perfectKey_.clear();
}

void TypingHud::drawDrill(const Typing& t, int ww, int wh)
{
    shadow = t.config().boxShadow;
    const int scale = wh >= 1800 ? 5 : wh >= 1000 ? 4 : wh >= 600 ? 3 : 2;
    const float s = std::min(float(ww) / 496.0f, float(wh) / 384.0f);
    for (size_t k = 0; k < t.labels.size(); ++k) {
        const Typing::Label& l = t.labels[k];
        Slot* sl = slot(k);
        if (!sl) return;
        char key[64];
        std::snprintf(key, sizeof key, "%zu|%d|%d|%d|%d|", l.typed, int(l.locked), int(l.danger), int(l.hitAge < 5), scale);
        const std::string k2 = key + l.text;
        if (sl->key != k2) {
            const std::vector<uint8_t> img = labelImage(l, scale, sl->w, sl->h);
            sl->ov.setImage(img, sl->w, sl->h);
            sl->key = k2;
        }
    }
    for (size_t k = t.labels.size(); k-- > 0;) {   // (the locked one first in the list: drawn last, on top)
        const Typing::Label& l = t.labels[k];
        slot(k)->ov.drawAnchored(ww, wh, float(ww) * 0.5f + l.x * s, float(wh) * 0.5f - l.y * s, 0.5f, 0.5f);
    }
    if (!drillReady_) {
        std::string err;
        drillReady_ = drill_.init(err);
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "PRACTICE %u / %d   WPM %d   ACCURACY %d%%", std::min<uint32_t>(t.drillShown, uint32_t(t.drillTotal)),
                  t.drillTotal, int(t.game.wpm() + 0.5f), int(t.game.accuracy() + 0.5f));
    const int ps = std::max(2, scale - 1);
    const std::string key = std::string(buf) + "|" + std::to_string(ps);
    if (drillReady_ && key != drillKey_) {
        const uint8_t white[3] = {235, 235, 225};
        int w = 0, h = 0;
        const std::vector<uint8_t> img = textImage(buf, white, ps, w, h);
        drill_.setImage(img, w, h);
        drillKey_ = key;
    }
    if (drillReady_)
        drill_.drawAnchored(ww, wh, float(ww) * 0.5f, float(wh) * 0.5f - 150.0f * s, 0.5f, 0.5f);
}

void TypingHud::draw(const Runtime& rt, const Typing& t, int ww, int wh,
                     const std::function<void(float ax, float ay, float& px, float& py)>& toWindow, float alpha)
{
    loadFont(rt);
    static const bool shadowEnv = std::getenv("HOTD_TOTD_SHADOW") != nullptr && std::string(std::getenv("HOTD_TOTD_SHADOW")) != "0";
    shadow = shadowEnv || t.config().boxShadow;
    // (totd4: one step larger from 1000 lines: TotD's words are about 1/50 of the screen's height)
    // (totd5, the feel report: TotD's boxes are ~14 % of the screen's height: one step larger again)
    const int scale = wh >= 1800 ? 5 : wh >= 1000 ? 4 : wh >= 600 ? 3 : 2;
    // totd5 (the feel report 2026-11-05: "prompt boxes pile on top of each other; TotD stacks neighbours without
    // overlap"): each box's place first (the locked one, then the most dangerous: the labels' order), each later one
    // pushed up or down (the nearer way) off those placed; then drawn the least dangerous first, the locked one on top
    struct Place { float x, y, w, h; };
    std::vector<Place> places(t.labels.size());
    for (size_t k = 0; k < t.labels.size(); ++k) {
        const Typing::Label& l = t.labels[k];
        Slot* s = slot(k);
        if (!s) return;
        char key[96];
        std::snprintf(key, sizeof key, "%zu|%d|%d|%d|%d|%d|%d|%d|%d|", l.typed, int(l.locked), l.kind, l.left, scale,
                      l.window >= 0 ? int(l.window * 40) : l.attack >= 0 ? 100 + int(l.attack * 40) : -1, int(l.danger),
                      int(l.hitAge < 5), std::min(l.lockAge, 11));
        const std::string k2 = key + l.text;
        if (s->key != k2) {
            const std::vector<uint8_t> img = labelImage(l, scale, s->w, s->h);
            s->ov.setImage(img, s->w, s->h);
            s->key = k2;
        }
        // totd4 (the reference: TotD puts the box on the enemy's body, over its chest, not above the head): centred on
        // the aim point (the chest); a small one (a bat, a thrown axe) on it too. totd5 (user 2026-11-04: "the TotD UI in
        // the bottom middle covers things up"; the PC TotD: the boss's sentence sits on the boss): a boss sentence on its
        // weak point too (totd2-4: a fixed panel low in the middle, over the creatures and the thrown things there);
        // kept inside the picture (off the screen: at the nearest edge)
        float px = 0, py = 0;
        // totd6 (Smooth Motion: the picture is blended between the last two ticks): the aim point before the tick plus
        // alpha of its step, so the box stays on the body the picture shows
        toWindow(l.x + alpha * l.dx, l.y + alpha * l.dy, px, py);
        if (static const bool rects = std::getenv("HOTD_TOTD_RECTS") != nullptr; rects) {   // (testing: the boxes' drift)
            float nx = 0, ny = 0, ox = 0, oy = 0;
            toWindow(l.x + l.dx, l.y + l.dy, nx, ny);   // (the newest tick's place; totd5 drew it at the place before the tick)
            toWindow(l.x, l.y, ox, oy);
            std::printf("TRECT %llu %08X a %.3f body %.1f %.1f newest %.1f %.1f before %.1f %.1f kind %d\n", (unsigned long long)rt.frameCount,
                        l.obj, double(alpha), px, py, nx, ny, ox, oy, l.kind);
        }
        // (a far creature: the box above it, not over it: a thrower far off stays in sight)
        // totd8 (user 2026-11-05: "the boxes must not block the zombies too much"; the PC TotD): a creature whose body
        // on the screen is short beside its box (under 2.2 box heights: far off, crouching) has its box just above its
        // head, as the reference's far ones; a bigger one's on its belly (Typing: box_anchor)
        if (l.kind == 0 && l.bottom > l.top) {
            float tx = 0, ty = 0, bx = 0, by = 0;
            toWindow(l.x + alpha * l.dx, l.top + alpha * l.dy, tx, ty);
            toWindow(l.x + alpha * l.dx, l.bottom + alpha * l.dy, bx, by);
            if (std::abs(by - ty) < 2.2f * float(s->h))
                py = std::min(ty, by) - float(s->h) * 0.5f - float(wh) * 0.01f;
        } else if (l.kind == 0 && l.z > 70.0f)
            py -= float(s->h) * 0.5f + float(wh) * 0.02f;
        const float mx = l.kind == 2 ? float(ww) * 0.22f : float(s->w) * 0.5f + 4.0f, my = l.kind == 2 ? float(wh) * 0.12f : float(s->h) * 0.5f + 4.0f;
        px = std::clamp(px, mx, float(ww) - mx);
        py = std::clamp(py, my, float(wh) - my);
        Place p{px, py, float(s->w), float(s->h)};
        for (int pass = 0; pass < 6 && k > 0; ++pass) {
            bool moved = false;
            for (size_t j = 0; j < k; ++j) {
                const Place& q = places[j];
                const float ox = (p.w + q.w) * 0.5f - std::abs(p.x - q.x), oy = (p.h + q.h) * 0.5f - std::abs(p.y - q.y);
                if (ox <= 0 || oy <= 0) continue;
                const float up = q.y - (p.h + q.h) * 0.5f - 2.0f, down = q.y + (p.h + q.h) * 0.5f + 2.0f;
                const bool upOk = up >= p.h * 0.5f, downOk = down <= float(wh) - p.h * 0.5f;
                p.y = (upOk && (!downOk || std::abs(up - p.y) <= std::abs(down - p.y))) ? up : downOk ? down : p.y;
                if (!upOk && !downOk)   // (no room above or below: beside it)
                    p.x = p.x < q.x ? q.x - (p.w + q.w) * 0.5f - 2.0f : q.x + (p.w + q.w) * 0.5f + 2.0f;
                moved = true;
            }
            if (!moved) break;
        }
        // totd7 (user 2026-11-05: "when enemies get close and attack, their box goes past the screen border; you should
        // have the chance until you take damage"): every box last of all kept wholly inside the picture (the arcade
        // picture's edges in the window: a pillarboxed 4:3 picture's sides, else the window's) less a safe margin, with
        // room for the PERFECT line at the top and the lives at the bottom; as near its creature as that allows. A
        // continuous clamp (no jump): a creature filling the screen keeps its box at the edge nearest its aim point
        {
            float lx = 0, ly = 0, rx = 0, ry = 0;
            toWindow(-248.0f, 0.0f, lx, ly);
            toWindow(248.0f, 0.0f, rx, ry);
            const float left = std::clamp(std::min(lx, rx), 0.0f, float(ww)), right = std::clamp(std::max(lx, rx), 0.0f, float(ww));
            const float edge = std::max(4.0f, float(wh) * 0.015f);
            const float top = float(wh) * 0.09f, bottom = float(wh) * 0.90f;   // (PERFECT nn above; the lives below)
            const bool outside = p.x - p.w * 0.5f < left || p.x + p.w * 0.5f > right || p.y - p.h * 0.5f < 0 || p.y + p.h * 0.5f > float(wh);
            const float x0 = left + edge + p.w * 0.5f, x1 = right - edge - p.w * 0.5f;
            const float y0 = top + p.h * 0.5f, y1 = bottom - p.h * 0.5f;
            p.x = x0 <= x1 ? std::clamp(p.x, x0, x1) : (left + right) * 0.5f;
            p.y = y0 <= y1 ? std::clamp(p.y, y0, y1) : float(wh) * 0.5f;
            ++boxStats[0];
            boxStats[1] += outside;
            boxStats[2] += p.x - p.w * 0.5f < left || p.x + p.w * 0.5f > right || p.y - p.h * 0.5f < 0 || p.y + p.h * 0.5f > float(wh);
        }
        // totd8 (testing, HOTD_TOTD_COVER=1): the share of the creature's body (its hit spheres in the window) its own
        // box hides, and of its head (the highest sphere)
        if (static const bool cov = std::getenv("HOTD_TOTD_COVER") != nullptr; cov && l.kind == 0 && !l.quick && l.group == 1) {
            static std::vector<Typing::Circle> body;
            if (Typing::bodyCircles(const_cast<Runtime&>(rt), l.obj, body)) {
                struct W { float x, y, r; };
                std::vector<W> wc;
                float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
                size_t head = 0;
                for (size_t c = 0; c < body.size(); ++c) {
                    float ax = 0, ay = 0, bx = 0, by = 0;
                    toWindow(body[c].x, body[c].y, ax, ay);
                    toWindow(body[c].x + body[c].r, body[c].y, bx, by);
                    const float r = std::abs(bx - ax);
                    wc.push_back({ax, ay, r});
                    x0 = std::min(x0, ax - r), x1 = std::max(x1, ax + r), y0 = std::min(y0, ay - r), y1 = std::max(y1, ay + r);
                    if (body[c].y - body[c].r < body[head].y - body[head].r) head = c;
                }
                const int band = l.z < 30.0f ? 0 : l.z > 70.0f ? 2 : 1;
                double n = 0, in = 0, hn = 0, hin = 0;
                for (int gy = 0; gy < 32; ++gy)
                    for (int gx = 0; gx < 32; ++gx) {
                        const float sx = x0 + (x1 - x0) * (float(gx) + 0.5f) / 32.0f, sy = y0 + (y1 - y0) * (float(gy) + 0.5f) / 32.0f;
                        if (sx < 0 || sy < 0 || sx > float(ww) || sy > float(wh)) continue;   // (on the screen only)
                        bool inBody = false, inHead = false;
                        for (size_t c = 0; c < wc.size(); ++c) {
                            const bool i = (sx - wc[c].x) * (sx - wc[c].x) + (sy - wc[c].y) * (sy - wc[c].y) <= wc[c].r * wc[c].r;
                            inBody = inBody || i;
                            inHead = inHead || (i && c == head);
                        }
                        if (!inBody) continue;
                        const bool hid = std::abs(sx - p.x) <= p.w * 0.5f && std::abs(sy - p.y) <= p.h * 0.5f;
                        n += 1, in += hid;
                        if (inHead) hn += 1, hin += hid;
                    }
                if (n > 0) {
                    cover[band][0] += 1, cover[band][1] += in / n;
                    if (hn > 0) cover[band][2] += 1, cover[band][3] += hin / hn, cover[band][4] += hin / hn > 0.5 ? 1 : 0;
                }
            }
        }
        places[k] = p;
    }
    for (size_t k = t.labels.size(); k-- > 0;) {
        Slot* s = slot(k);
        s->ov.drawAnchored(ww, wh, places[k].x, places[k].y, 0.5f, 0.5f);
    }
    // totd2 (user 2026-11-03: "each enemy killed shows a grade next to the enemy"): the kills' grades for a moment,
    // over where the enemy was (rising a little), a boss sentence's under its panel
    for (size_t i = 0; i < t.popups.size(); ++i) {
        const Typing::Popup& p = t.popups[i];
        Slot* s = slot(t.labels.size() + i);
        if (!s) return;
        Typing::Label l;
        l.text = std::string(1, p.grade);
        l.kind = 4;
        const std::string key = std::string("grade|") + std::to_string(scale) + l.text;
        if (s->key != key) {
            int w = 0, h = 0;
            const std::vector<uint8_t> img = labelImage(l, scale, w, h);
            s->ov.setImage(img, w, h);
            s->key = key;
            s->w = w, s->h = h;   // (qa1: the layout pass below needs its size; a slot a box used before kept the box's)
        }
        const float age = float(t.frame() - p.frame);
        float px = 0, py = 0;
        if (p.boss) {   // (totd5: where the sentence was, on the weak point)
            toWindow(p.x, p.y, px, py);
            px = std::clamp(px, float(ww) * 0.22f, float(ww) * 0.78f);
            py = std::clamp(py + float(16 * scale), float(wh) * 0.12f, float(wh) * 0.9f);
        } else {
            toWindow(p.x, p.y + 10.0f, px, py);   // (totd4: where the box was, as TotD's RANK)
            py -= age * 0.25f * float(scale);
            px = std::clamp(px, 40.0f, float(ww) - 40.0f);
            py = std::clamp(py, 30.0f, float(wh) - 8.0f);
        }
        // qa1 (the user's play: two "RANK" pop-ups on top of each other and on the boxes near them): each pop-up
        // pushed off the boxes and the pop-ups placed before it, as the boxes are (up first: it rises; else down)
        Place q{px, p.boss ? py + float(s->h) * 0.5f : py - float(s->h) * 0.5f, float(s->w), float(s->h)};
        // qa2 (QA: pushed up box after box, a pop-up over a tall stack ended far above its kill): the free place nearest
        // the kill: where it is, or just above / below / left / right of any box or pop-up it would touch, inside the
        // window and touching nothing (none free: where it is)
        auto clear = [&](const Place& c) {
            if (c.x < c.w * 0.5f || c.x > float(ww) - c.w * 0.5f || c.y < c.h * 0.5f || c.y > float(wh) - c.h * 0.5f)
                return false;
            for (const Place& o : places)
                if ((c.w + o.w) * 0.5f - std::abs(c.x - o.x) > 0 && (c.h + o.h) * 0.5f - std::abs(c.y - o.y) > 0)
                    return false;
            return true;
        };
        if (!clear(q)) {
            const Place home = q;
            float best = -1.0f;
            for (const Place& o : places) {
                const Place cand[4] = {{home.x, o.y - (home.h + o.h) * 0.5f - 2.0f, home.w, home.h},
                                       {home.x, o.y + (home.h + o.h) * 0.5f + 2.0f, home.w, home.h},
                                       {o.x - (home.w + o.w) * 0.5f - 2.0f, home.y, home.w, home.h},
                                       {o.x + (home.w + o.w) * 0.5f + 2.0f, home.y, home.w, home.h}};
                for (const Place& c : cand) {
                    const float d = (c.x - home.x) * (c.x - home.x) + (c.y - home.y) * (c.y - home.y);
                    if ((best < 0 || d < best) && clear(c))
                        best = d, q = c;
                }
            }
        }
        places.push_back(q);
        s->ov.drawAnchored(ww, wh, q.x, q.y, 0.5f, 0.5f);
    }
    // totd6 (user 2026-11-05: "the HUD like the PC TotD"): the PC TotD's counter, top left: "PERFECT nn", the words
    // typed in a row without a wrong letter (our combo), gold, in play; a wrong letter shows "MISS!!" in its place for
    // half a second (light blue) and the count starts again. Accuracy and WPM stay for the chapter's results (the PC
    // TotD shows none in play). totd4-5: a COMBO / ACCURACY / WPM line in the system font
    const bool missed = rt.frameCount < uint64_t(t.lastMissFrame) + 30 && t.lastMissFrame != 0;
    if (!rt.clear.active && Typing::inPlay(const_cast<Runtime&>(rt))) {
        if (!perfectReady_) {
            std::string err;
            perfectReady_ = perfect_.init(err);
        }
        char buf[48];
        if (missed)
            std::snprintf(buf, sizeof buf, "MISS!!");
        else
            std::snprintf(buf, sizeof buf, "PERFECT %02u", std::min<uint32_t>(t.combo, 999));
        const int ps = std::max(2, scale);   // (the PC TotD's: about 4 % of the height)
        const std::string key = std::string(buf) + "|" + std::to_string(ps);
        if (perfectReady_ && key != perfectKey_) {
            const uint8_t gold[3] = {255, 214, 70}, blue[3] = {120, 200, 255};
            int w = 0, h = 0;
            const std::vector<uint8_t> img = textImage(buf, missed ? blue : gold, ps, w, h);
            perfect_.setImage(img, w, h);
            perfectKey_ = key;
        }
        // (the PC TotD's place: the top left, under its gauge at ~15 % of the height; ours has no gauge: a little higher)
        if (perfectReady_)
            perfect_.drawAnchored(ww, wh, float(ww) * 0.03f, float(wh) * 0.13f, 0.0f, 0.0f);
        // totdex (user 2026-11-09, after Tenome's Typing of the Dead EX: "a live WPM / accuracy line, off by default"):
        // the game's WPM and accuracy so far under PERFECT, white, one step smaller; whole numbers (no flicker of tenths)
        if (liveLine) {
            if (!liveReady_) {
                std::string err;
                liveReady_ = live_.init(err);
                live_.translate = false;
            }
            const int ls = std::max(1, ps - 1);
            const std::string wpmT = lang::tr("WPM"), accT = lang::tr("Accuracy");
            char lb[128];
            std::snprintf(lb, sizeof lb, "%s %d  %s %d%%", wpmT.c_str(), int(t.game.wpm() + 0.5f), accT.c_str(), int(t.game.accuracy() + 0.5f));
            const std::string key = std::string(lb) + "|" + std::to_string(ls);
            bool ascii = true;
            for (const char c : std::string(lb)) ascii = ascii && uint8_t(c) < 0x80;
            if (liveReady_ && key != liveKey_) {
                if (ascii) {
                    std::string up = lb;   // (capitals: the font's small letters read badly)
                    for (char& c : up) c = char(std::toupper(uint8_t(c)));
                    static const uint8_t white[3] = {235, 235, 235};
                    int w = 0, h = 0;
                    const std::vector<uint8_t> img = textImage(up, white, ls, w, h);
                    live_.setImage(img, w, h);
                } else
                    live_.setText({std::string(lb)}, scale);
                liveKey_ = key;
            }
            if (liveReady_)
                live_.drawAnchored(ww, wh, float(ww) * 0.03f, float(wh) * 0.13f + float(ps * 11), 0.0f, 0.0f);
        }
    }
    // the chapter's typing results, while the clear's results are up
    if (rt.clear.active && t.resultsShown && !t.chapters.empty()) {
        if (!resultsReady_) {
            std::string err;
            resultsReady_ = results_.init(err);
            results_.translate = false;
            results_.setStyle(190, 240, 240, 230);
        }
        if (!resultsReady_) return;
        const TypingStats& c = t.chapters.back();
        std::vector<std::string> lines;
        char b[160];
        // four lines over the clear's picture (the top of the screen is free then): the SEGA games' results rows
        std::snprintf(b, sizeof b, "%s   %s %c", lang::tr("TYPING RESULTS").c_str(), lang::tr("GRADE").c_str(), t.grade(c));
        lines.push_back(b);
        std::snprintf(b, sizeof b, "%s %u  %s %u  %s %.1f%%  %s %.1f", lang::tr("Letters").c_str(), c.letters,
                      lang::tr("Misses").c_str(), c.misses, lang::tr("Accuracy").c_str(), c.accuracy(), lang::tr("WPM").c_str(), c.wpm());
        lines.push_back(b);
        std::snprintf(b, sizeof b, "%s %u  %s %u", lang::tr("Words").c_str(), c.words, lang::tr("Best combo").c_str(), c.maxCombo);
        std::string l3 = b;
        if (c.fastestMs > 0) {
            std::snprintf(b, sizeof b, "  %s %.2f s (%s)", lang::tr("Fastest").c_str(), c.fastestMs / 1000.0, c.fastestText.substr(0, 24).c_str());
            l3 += b;
        }
        lines.push_back(l3);
        // totd2: the kills' grades
        std::snprintf(b, sizeof b, "%s  S %u  A %u  B %u  C %u  D %u  F %u", lang::tr("Kills").c_str(), c.killGrade[0], c.killGrade[1],
                      c.killGrade[2], c.killGrade[3], c.killGrade[4], c.killGrade[5]);
        lines.push_back(b);
        // qa1 (QA: the four lines in the system font on a dark panel looked like a debug print and ran over the top
        // of the clear's rescue picture): in the game's own font as "PERFECT nn" (the title gold, the rest white, a
        // dark edge, no panel), at the largest whole scale that fits above the picture (its frame from ~16 % of
        // the height) and inside the window's width. A translation with letters the font lacks: the system font
        bool ascii = true;
        for (const std::string& s : lines)
            for (const char c : s) ascii = ascii && uint8_t(c) < 0x80;
        if (!ascii) {
            results_.setText(lines, scale);
            results_.drawAnchored(ww, wh, float(ww) * 0.5f, 6.0f, 0.5f, 0.0f);
            return;
        }
        size_t cols = 0;
        for (const std::string& s : lines) cols = std::max(cols, s.size());
        int rs = 1;   // (a line: 10 font pixels with its edge and gap; 4 lines in the top 15 %)
        while (float((rs + 1) * 12 * int(lines.size())) <= float(wh) * 0.15f && float((rs + 1) * (int(cols) * 8 + 2)) <= float(ww) * 0.94f)
            ++rs;
        const std::string key = std::to_string(rs) + "|" + lines[0] + lines[1] + lines[2] + lines[3];
        if (key != resultsKey_) {
            static const uint8_t gold[3] = {255, 214, 70}, white[3] = {235, 235, 235};
            std::vector<std::vector<uint8_t>> imgs(lines.size());
            std::vector<int> iw(lines.size()), ih(lines.size());
            int W = 0, H = 0;
            const int lineGap = rs * 2;   // (the lines' edges touched)
            for (size_t i = 0; i < lines.size(); ++i) {
                std::string up = lines[i];   // (capitals, as the game's own HUD: the font's small letters read badly, "Words" as "Wards")
                for (char& c : up) c = char(std::toupper(uint8_t(c)));
                imgs[i] = textImage(up, i == 0 ? gold : white, rs, iw[i], ih[i]);
                W = std::max(W, iw[i]);
                H += ih[i] + (i + 1 < lines.size() ? lineGap : 0);
            }
            std::vector<uint8_t> all(size_t(W) * size_t(H) * 4, 0);
            for (size_t i = 0, y = 0; i < lines.size(); y += size_t(ih[i] + lineGap), ++i)   // (left aligned: the columns line up)
                for (int r = 0; r < ih[i]; ++r)
                    std::copy_n(&imgs[i][size_t(r) * size_t(iw[i]) * 4], size_t(iw[i]) * 4, &all[((y + size_t(r)) * size_t(W)) * 4]);
            results_.setImage(all, W, H);
            resultsKey_ = key;
        }
        results_.drawAnchored(ww, wh, float(ww) * 0.5f, float(wh) * 0.008f, 0.5f, 0.0f);
    }
}

} // namespace hotd::game
