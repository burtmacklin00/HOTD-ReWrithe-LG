// The Typing of the Dead 0's HUD (typing_hud.h). Placeholder look: SDL's 8 x 8 debug font, drawn boxes.
#include "game/typing_hud.h"

#include "game/lang.h"

#include <SDL3/SDL.h>

#include <algorithm>
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

// the box's colours by what the prompt is on (the user's art replaces them: docs/typing.md "Assets")
void borderOf(const Typing::Label& l, uint8_t c[3])
{
    if (l.locked) { c[0] = 255, c[1] = 220, c[2] = 40; return; }      // locked: yellow
    switch (l.kind) {
    case 1: c[0] = 255, c[1] = 140, c[2] = 40; break;                // a projectile: orange
    case 2: c[0] = 220, c[1] = 40, c[2] = 40; break;                 // a boss: red
    case 3: c[0] = 80, c[1] = 220, c[2] = 90; break;                 // an item: green
    case 4:                                                          // a kill's grade (totd2): gold S / A, white B / C, grey
        if (l.text == "S" || l.text == "A") c[0] = 255, c[1] = 200, c[2] = 40;
        else if (l.text == "B" || l.text == "C") c[0] = 230, c[1] = 230, c[2] = 230;
        else c[0] = 120, c[1] = 120, c[2] = 120;
        break;
    default: c[0] = 150, c[1] = 150, c[2] = 150; break;              // an enemy: grey
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
    const int pad = 4, gap = 4, border = 1, bar = l.window >= 0 ? 3 : l.attack >= 0 ? 2 : 0;
    // (the bar: a boss sentence's window, or an enemy's attack timer: the time before it may strike, TotD's)
    const float barLeft = l.window >= 0 ? l.window : l.attack;
    size_t cols = 0;
    for (const auto& [a, b] : lines) cols = std::max(cols, b - a);
    std::string extra = l.left > 0 ? " +" + std::to_string(l.left) : std::string();   // prompts still to come
    const int bw = int(cols + extra.size()) * ch + 2 * pad + 2 * border;
    const int bh = int(lines.size()) * (ch + gap) - gap + 2 * pad + 2 * border + bar + (bar ? 2 : 0) + 2;
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
        SDL_SetRenderDrawColor(r, bc[0], bc[1], bc[2], 255);
        SDL_FRect box{0, 0, float(bw), float(bh - 2)};
        SDL_RenderFillRect(r, &box);
        SDL_SetRenderDrawColor(r, 8, 8, 12, l.locked ? 235 : 200);
        SDL_FRect in{float(border), float(border), float(bw - 2 * border), float(bh - 2 - 2 * border)};
        SDL_RenderFillRect(r, &in);
        // the pointer under the box (toward the enemy)
        SDL_SetRenderDrawColor(r, bc[0], bc[1], bc[2], 255);
        SDL_FRect tip{float(bw / 2 - 1), float(bh - 2), 3, 2};
        SDL_RenderFillRect(r, &tip);
        char one[2] = {0, 0};
        for (size_t li = 0; li < lines.size(); ++li) {
            const auto [a, b] = lines[li];
            const float y = float(border + pad) + float(li) * float(ch + gap);
            const float x0 = float(border + pad) + float((cols - (b - a)) * ch) / 2.0f;
            for (size_t i = a; i < b; ++i) {
                const float x = x0 + float((i - a) * ch);
                uint8_t col[3] = {240, 240, 230};
                if (i < l.typed)
                    col[0] = 230, col[1] = 60, col[2] = 40;             // typed: red (the shots landed)
                else if (i == l.typed && l.locked)
                    col[0] = 255, col[1] = 230, col[2] = 60;            // the next letter: yellow, underlined
                SDL_SetRenderDrawColor(r, col[0], col[1], col[2], 255);
                one[0] = s[i] == ' ' && i < l.typed ? '_' : s[i];       // (a typed space shows as _)
                if (hasGlyph(one[0]))   // the game's own glyph (drawn into the picture after the renderer, below)
                    glyphs.push_back({int(x), int(y), one[0], {col[0], col[1], col[2]}});
                else
                    SDL_RenderDebugText(r, x, y, one);
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
            const float y = float(bh - 2 - border - bar - 1);
            SDL_SetRenderDrawColor(r, 60, 20, 20, 255);
            SDL_FRect back{float(border + pad), y, float(bw - 2 * (border + pad)), float(bar)};
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
    lineReady_ = resultsReady_ = false;
}

void TypingHud::draw(const Runtime& rt, const Typing& t, int ww, int wh,
                     const std::function<void(float ax, float ay, float& px, float& py)>& toWindow)
{
    loadFont(rt);
    const int scale = wh >= 1400 ? 3 : wh >= 700 ? 2 : 1;
    // the prompts, the least dangerous first (the locked one drawn last: on top)
    for (size_t k = t.labels.size(); k-- > 0;) {
        const Typing::Label& l = t.labels[k];
        Slot* s = slot(k);
        if (!s) return;
        char key[96];
        std::snprintf(key, sizeof key, "%zu|%d|%d|%d|%d|%d|", l.typed, int(l.locked), l.kind, l.left, scale,
                      l.window >= 0 ? int(l.window * 40) : l.attack >= 0 ? 100 + int(l.attack * 40) : -1);
        const std::string k2 = key + l.text;
        if (s->key != k2) {
            int w = 0, h = 0;
            const std::vector<uint8_t> img = labelImage(l, scale, w, h);
            s->ov.setImage(img, w, h);
            s->key = k2;
        }
        float px = 0, py = 0;
        if (l.kind == 2) {   // a boss sentence: a fixed panel low in the middle (the boss moves a lot; its face stays clear)
            s->ov.drawAnchored(ww, wh, float(ww) * 0.5f, float(wh) * 0.74f, 0.5f, 1.0f);
            continue;
        }
        // above the aim point (the chest): about the head, the box over it; a small one (a bat, a thrown axe) just over it
        toWindow(l.x, l.y + (l.kind == 1 ? 14.0f : 40.0f), px, py);
        px = std::clamp(px, 60.0f, float(ww) - 60.0f);
        py = std::clamp(py, 40.0f, float(wh) - 8.0f);
        s->ov.drawAnchored(ww, wh, px, py, 0.5f, 1.0f);
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
        const std::string key = std::string("grade|") + std::to_string(scale + 1) + l.text;
        if (s->key != key) {
            int w = 0, h = 0;
            const std::vector<uint8_t> img = labelImage(l, scale + 1, w, h);
            s->ov.setImage(img, w, h);
            s->key = key;
        }
        const float age = float(t.frame() - p.frame);
        float px = 0, py = 0;
        if (p.boss) {
            px = float(ww) * 0.5f, py = float(wh) * 0.74f + float(16 * scale);
        } else {
            toWindow(p.x, p.y + 40.0f, px, py);
            py -= age * 0.4f * float(scale);
            px = std::clamp(px, 40.0f, float(ww) - 40.0f);
            py = std::clamp(py, 30.0f, float(wh) - 8.0f);
        }
        s->ov.drawAnchored(ww, wh, px, py, 0.5f, p.boss ? 0.0f : 1.0f);
    }
    // the typing line at the bottom: combo, accuracy, WPM (red for a moment after a miss)
    if (!lineReady_) {
        std::string err;
        lineReady_ = line_.init(err);
        line_.translate = false;
    }
    if (!lineReady_) return;
    const bool missed = rt.frameCount < uint64_t(t.lastMissFrame) + 12 && t.lastMissFrame != 0;
    if (int(missed) != lineMiss_) {   // (setStyle rebuilds the texture: only on a change)
        line_.setStyle(150, missed ? 255 : 235, missed ? 80 : 235, missed ? 60 : 220);
        lineMiss_ = int(missed);
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s %u   %s %.0f%%   %s %.0f", lang::tr("COMBO").c_str(), t.combo, lang::tr("ACCURACY").c_str(),
                  t.chapter.accuracy(), lang::tr("WPM").c_str(), t.chapter.wpm());
    if (!rt.clear.active) {
        line_.setText({buf}, scale);
        line_.drawAnchored(ww, wh, float(ww) * 0.5f, float(wh) - float(wh) / 9.0f, 0.5f, 1.0f);
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
        results_.setText(lines, scale);
        results_.drawAnchored(ww, wh, float(ww) * 0.5f, 6.0f, 0.5f, 0.0f);
    }
}

} // namespace hotd::game
