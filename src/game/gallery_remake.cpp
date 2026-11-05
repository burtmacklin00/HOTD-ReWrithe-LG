// gallery3 (2026-10-21): the Gallery's optional Remake layout (launcher.ini gallerylayout=1, key K on the Gallery page),
// after the user's screenshot of The House of the Dead: Remake's gallery, drawn with our own pictures and the 1998 fonts:
// a grid of portraits on the left (rendered from the models, Gallery::makeThumbs; the shown one in an orange frame; Cut
// Content as dark silhouettes), "Gallery Navigation" with Q / E at the top, a dark torn panel on the right with the name
// in red, the description in pages (Tab), the boss's weak-point card (the game's own intro pictures from the PC files)
// and the controls, "Esc Main Menu" at the bottom left. The page is 640 x 480 like every menu page; the host draws it
// in three parts (Gallery::drawSplit: [0, 200) at the window's left edge, [200, 440) in the middle, [440, 640) at the
// right edge), the model and its room over the whole window under it. Contained here so the layout can be dropped.
#include "game/frontend.h"

#include <algorithm>
#include <sstream>

namespace hotd::game {

namespace {

std::vector<std::string> wrapLines(const std::string& s, size_t width)
{
    std::vector<std::string> out;
    std::string line, word;
    std::istringstream ss(s);
    while (ss >> word) {
        if (!line.empty() && line.size() + 1 + word.size() > width) {
            out.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

void blend(uint8_t* p, uint32_t rgb, int alpha)
{
    const int a = std::clamp(alpha, 0, 255);
    for (int c = 0; c < 3; ++c) {
        const int v = int((rgb >> (16 - 8 * c)) & 0xFF);
        p[c] = uint8_t((v * a + p[c] * (255 - a)) / 255);
    }
    p[3] = uint8_t(std::max(int(p[3]), a));
}

// a small hash for the torn edges (the same teeth every frame)
int tooth(int x) { return int((uint32_t(x) * 2654435761u) >> 29); }   // 0..7

constexpr int kTileX = 14, kTileY = 46, kTileW = 54, kTileH = 74, kTileStepX = 58, kTileStepY = 78, kRows = 5;

}   // namespace

void Frontend::drawFill(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const uint32_t argb = uint32_t(w.tag);
    const int a = int(argb >> 24);
    for (int j = std::max(0, y); j < std::min(kHeight, y + hh); ++j)
        for (int i = std::max(0, x); i < std::min(kWidth, x + ww); ++i) {
            if (w.id == 1 && (j - y < tooth(i) || y + hh - 1 - j < tooth(i * 7 + 3))) continue;   // torn top / bottom
            uint8_t* p = &(*canvas_)[(size_t(j) * kWidth + size_t(i)) * 4];
            const bool edge = i == x || j == y || i == x + ww - 1 || j == y + hh - 1;
            blend(p, w.id == 2 && edge ? 0xC85050 : argb & 0xFFFFFF, w.id == 2 && edge ? 255 : a);
        }
}

void Frontend::drawTile(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const bool shown = (w.id & 1) != 0;
    for (int j = 0; j < hh; ++j)   // the tile: a dark gradient (blue-grey at the top), the portrait over it
        for (int i = 0; i < ww; ++i) {
            const int px = x + i, py = y + j;
            if (px < 0 || px >= kWidth || py < 0 || py >= kHeight) continue;
            uint8_t* p = &(*canvas_)[(size_t(py) * kWidth + size_t(px)) * 4];
            const float t = float(j) / float(hh);
            p[0] = uint8_t(14 + 10 * (1 - t)), p[1] = uint8_t(18 + 22 * (1 - t)), p[2] = uint8_t(26 + 46 * (1 - t)), p[3] = 255;
        }
    if (galleryThumb)
        if (const std::vector<uint8_t>* t = galleryThumb(w.tag); t && t->size() >= size_t(50 * 70 * 4)) {
            const int tw = 50, th = 70, ox = x + (ww - tw) / 2, oy = y + (hh - th) / 2;
            for (int j = 0; j < th; ++j)
                for (int i = 0; i < tw; ++i) {
                    const uint8_t* s = &(*t)[(size_t(j) * tw + size_t(i)) * 4];
                    const int px = ox + i, py = oy + j;
                    if (!s[3] || px < 0 || px >= kWidth || py < 0 || py >= kHeight) continue;
                    blend(&(*canvas_)[(size_t(py) * kWidth + size_t(px)) * 4], uint32_t(s[0]) << 16 | uint32_t(s[1]) << 8 | s[2], s[3]);
                }
        }
    // the frame: orange 2 pixels round the shown one, else a thin dark red line (lit when the hand is on it)
    const uint32_t col = shown ? 0xF08A28 : (w.flags & 0x40000) ? 0xB03030 : 0x5A1414;
    const int th = shown ? 2 : 1;
    for (int j = 0; j < hh; ++j)
        for (int i = 0; i < ww; ++i) {
            if (i >= th && j >= th && i < ww - th && j < hh - th) continue;
            const int px = x + i, py = y + j;
            if (px >= 0 && px < kWidth && py >= 0 && py < kHeight) blend(&(*canvas_)[(size_t(py) * kWidth + size_t(px)) * 4], col, 255);
        }
}

void Frontend::drawCard(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const std::vector<uint8_t>* c = galleryCard ? galleryCard() : nullptr;
    if (!c || c->size() < 4) return;
    const int cw = (*c)[0] | ((*c)[1] << 8), ch = (*c)[2] | ((*c)[3] << 8);
    if (cw <= 0 || ch <= 0 || c->size() < 4 + size_t(cw) * ch * 4) return;
    for (int j = 0; j < hh; ++j)   // a dark card with a lighter border
        for (int i = 0; i < ww; ++i) {
            const int px = x + i, py = y + j;
            if (px < 0 || px >= kWidth || py < 0 || py >= kHeight) continue;
            const bool edge = i == 0 || j == 0 || i == ww - 1 || j == hh - 1;
            blend(&(*canvas_)[(size_t(py) * kWidth + size_t(px)) * 4], edge ? 0x8A6A50 : 0x1C1412, 255);
        }
    const float s = std::min(float(ww - 6) / float(cw), float(hh - 6) / float(ch));
    const int dw = int(cw * s), dh = int(ch * s), ox = x + (ww - dw) / 2, oy = y + (hh - dh) / 2;
    for (int j = 0; j < dh; ++j)
        for (int i = 0; i < dw; ++i) {
            const uint8_t* p = &(*c)[4 + (size_t(std::min(ch - 1, int(j / s))) * cw + size_t(std::min(cw - 1, int(i / s)))) * 4];
            const int px = ox + i, py = oy + j;
            if (p[3] && px >= 0 && px < kWidth && py >= 0 && py < kHeight)
                blend(&(*canvas_)[(size_t(py) * kWidth + size_t(px)) * 4], uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2], p[3]);
        }
}

void Frontend::drawDots(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const int n = std::max(1, int(w.tag)), gap = 12, x0 = x + ww / 2 - (n - 1) * gap / 2, cy = y + hh / 2;
    for (int k = 0; k < n; ++k) {
        const int r = k == w.id ? 3 : 2;
        for (int j = -r; j <= r; ++j)
            for (int i = -r; i <= r; ++i)
                if (i * i + j * j <= r * r + 1) {
                    const int px = x0 + k * gap + i, py = cy + j;
                    if (px >= 0 && px < kWidth && py >= 0 && py < kHeight)
                        blend(&(*canvas_)[(size_t(py) * kWidth + size_t(px)) * 4], k == w.id ? 0xF0E8E0 : 0x806A6A, 255);
                }
    }
}

void Frontend::screenGalleryRemake(const GalleryText& g, Widget* root)
{
    // (a widget added later is drawn earlier: the texts first, the panels after them, the clear window last)
    auto text = [&](const std::string& s, int x, int y, int w, int h, uint32_t flags, uint32_t tint = 0) {
        Widget* t = widgetNew();
        addChild(root, t);
        setText(t, s, flags);
        setRect(t, x, y, w, h);
        t->tint = tint;
        return t;
    };
    auto fill = [&](int x, int y, int w, int h, uint32_t argb, int style) {
        Widget* f = widgetNew();
        addChild(root, f);
        setRect(f, x, y, w, h);
        f->draw = &Frontend::drawFill;
        f->flags |= 0x100;
        f->tag = int32_t(argb);
        f->id = int16_t(style);
        return f;
    };
    auto act = [this](int a) {
        return [this, a](Widget&) {
            walking_ = false;
            if (onGallery) {
                const auto f = onGallery;
                f(a);
            }
            if (a == 6)
                screenMainMenu();
            else
                screenGallery();
        };
    };
    std::vector<Widget*> later;   // the key caps' fills: added after their texts
    auto keycap = [&](const std::string& k, int x, int y) {   // a small key / button cap, its width from the text
        const int w = int(k.size()) * 6 + 7;
        Widget* t = text(k, x, y + 2, w, 10, 0x800020);
        later.push_back(nullptr);
        Widget* f = widgetNew();
        setRect(f, x, y, w, 13);
        f->draw = &Frontend::drawFill;
        f->flags |= 0x100;
        f->tag = int32_t(0xFF4A0C0Cu);
        f->id = 2;
        later.back() = f;
        return t;
    };
    // ---- the right panel: the name in red, the description in pages, the card, the controls ----
    const int px = 446, pw = 188;
    {
        int tw, th;
        measure(g.name, 0x20000, tw, th);
        if (tw <= pw - 8)
            text(g.name, px, 42, pw, 24, 0x20020, 0xD82020);
        else
            text(g.name, px, 46, pw, 12, 0x800020, 0xE03030);
    }
    if (!g.motion.empty())
        text((g.playing ? "Motion " : "Pose ") + g.motion.substr(0, g.motion.find("  (")), px, 66, pw, 12, 0x800020, 0x9A8A8A);
    // (lang: each text translated, then cut: the lines' widgets raw)
    std::vector<std::string> lines = wrapTr(g.text, 24, wrapLines);
    if (!g.where.empty()) {
        lines.push_back("");
        lines.push_back(T("Appears:") + " " + T(g.where));
    }
    for (const std::string& l : wrapTr(g.facts, 24, wrapLines)) lines.push_back(l);
    if (!g.status.empty()) {
        lines.push_back("");
        for (const std::string& l : wrapTr(g.status, 24, wrapLines)) lines.push_back(l);
    }
    // gallery blood / gallery dismemberment: the Gallery's blood colour, the keys' body part and its damage state
    lines.push_back("");
    lines.push_back(T("Blood:") + " " + T(g.blood) + g.bloodHex() + (g.particles ? std::string() : "  " + T("(no particles)")));
    if (!g.damageNote.empty())
        for (const std::string& l : wrapTr(g.damageNote, 24, wrapLines)) lines.push_back(l);
    else if (g.damagePart >= 0 && size_t(g.damagePart) < g.partNames.size() && size_t(g.damagePart) < g.partStates.size())
        lines.push_back(T(g.partNames[size_t(g.damagePart)]) + ": " + T(g.partStates[size_t(g.damagePart)]));
    const std::vector<uint8_t>* card = galleryCard ? galleryCard() : nullptr;
    const int perPage = card ? 8 : 12;
    const int pages = std::max(1, (int(lines.size()) + perPage - 1) / perPage);
    const int page = ((g.page % pages) + pages) % pages;
    for (int i = 0; i < perPage && page * perPage + i < int(lines.size()); ++i)
        text(lines[size_t(page * perPage + i)], px + 8, 80 + i * 11, pw - 12, 11, 0x800000)->raw = true;
    int y = 80 + perPage * 11 + 2;
    if (pages > 1) {
        Widget* d = widgetNew();
        addChild(root, d);
        setRect(d, px, y, pw, 8);
        d->draw = &Frontend::drawDots;
        d->flags |= 0x100;
        d->tag = pages;
        d->id = int16_t(page);
    }
    y += 12;
    if (card) {
        Widget* c = widgetNew();
        addChild(root, c);
        setRect(c, px + 8, y, pw - 16, 96);
        c->draw = &Frontend::drawCard;
        c->flags |= 0x100;
        y += 102;
    }
    struct Row { const char* key; std::string label; };
    std::vector<Row> rows = g.pad
        ? std::vector<Row>{{"RS", "Rotation"}, {"LT/RT", "Zoom"}, {"D-pad", "Navigation"}, {"LS", "Panning"},
                           {"Start", "Hide UI"}, {"X", g.playing ? "Pause Animation" : "Play Animation"}, {"Y", "Next motion"},
                           {"View", "Background"}}
        : std::vector<Row>{{"Drag", "Rotation"}, {"Wheel", "Zoom"}, {"WASD", "Navigation"}, {"R-drag", "Panning"},
                           {"T", "Hide UI"}, {"Space", g.playing ? "Pause Animation" : "Play Animation"}, {"M", "Next motion"},
                           {"B", "Background"}, {"K", "Classic layout"}, {"G/N", "Blood Color, Rainbow"},
                           {"Z/X", "Damage: part, state"}};
    if (pages > 1) rows.push_back({g.pad ? "L3" : "Tab", "More text"});
    const int rowH = std::min(15, (436 - y) / std::max(1, int(rows.size())));
    for (const Row& r : rows) {
        keycap(r.key, px + 10, y);
        text(r.label, px + 58, y + 2, pw - 60, 10, 0x800000);
        y += rowH;
    }
    // ---- the top bar: Q / E, the category ----
    text("Gallery Navigation", 200, 8, 240, 10, 0x800020, 0xC8B8A8);
    setHandlers(keycap(g.pad ? "LB" : "Q", 212, 6), act(8), &Frontend::hitRect);
    setHandlers(keycap(g.pad ? "RB" : "E", g.pad ? 408 : 414, 6), act(9), &Frontend::hitRect);
    text(g.group + "  " + std::to_string(g.index + 1) + " / " + std::to_string(g.count), 200, 20, 240, 24, 0x20020);
    // ---- the left: the portraits, 3 across, the shown one's row kept in sight ----
    const int n = int(g.entries.size());
    const int rowsAll = (n + 2) / 3;
    const int first = std::clamp(g.index / 3 - 2, 0, std::max(0, rowsAll - kRows));
    if (first > 0) text("^", 14, 36, 166, 10, 0x800020);
    if (first + kRows < rowsAll) text("v", 14, 438, 166, 10, 0x800020);
    for (int k = first * 3; k < std::min(n, (first + kRows) * 3); ++k) {
        Widget* t = widgetNew();
        addChild(root, t);
        const int r = k / 3 - first, c = k % 3;
        setRect(t, kTileX + c * kTileStepX, kTileY + r * kTileStepY, kTileW, kTileH);
        t->draw = &Frontend::drawTile;
        t->flags |= 0x100;
        t->tag = g.entries[size_t(k)];
        t->id = k == g.index ? 1 : 0;
        setHandlers(t, act(1000 + g.entries[size_t(k)]), &Frontend::hitRect);
    }
    if (rowsAll > kRows) {   // galfix2 (user: "since there is 26, add a scroll bar"): the grid's place in the category
        // galfix3 (user: "not the same as the main menu scroll bar"): the settings lists' own bar (Frontend::scrollBar:
        // its look, size, drag and click); a row dragged to: the entry in the shown one's column there (the grid follows
        // the shown entry)
        scrollBar(root, 185, kTileY, 6, kRows * kTileStepY - 4, first, kRows, rowsAll,
                  [this, act, entries = g.entries, col = g.index % 3, n, rowsAll](int f) {
                      const int row = std::min(rowsAll - 1, f + 2);
                      const int k = std::min(n - 1, row * 3 + col);
                      Widget dummy;
                      act(1000 + entries[size_t(k)])(dummy);
                  });
    }
    // ---- bottom left: Esc Main Menu ----
    keycap(g.pad ? "B" : "Esc", 12, 452);
    Widget* back = text("Main Menu", 44, 454, 80, 10, 0x800000);
    back->tag = 1;   // Escape
    setHandlers(back, act(6), &Frontend::hitText);
    // the key caps, then the panels (drawn under the texts), then the clear window (drawn first)
    for (Widget* f : later) addChild(root, f);
    fill(204, 2, 232, 44, 0xD8120A0Au, 1);       // the top bar
    fill(px, 34, pw, 406, 0xD8140A0Au, 1);       // the right panel
    fill(6, 34, 188, 408, 0xC8100808u, 0);       // the grid's panel (galfix3: + the scroll bar)
    fill(6, 448, 124, 22, 0xA0100808u, 0);       // Esc Main Menu
    Widget* hole = widgetNew();
    addChild(root, hole);
    setRect(hole, 0, 0, kWidth, kHeight);
    hole->draw = &Frontend::drawHole;
    hole->flags |= 0x100;
}

}   // namespace hotd::game
