// remakepages (2026-10-22, user: "some menus don't account for the new style"): Chapter Select, Boss Mode and its
// times tables, High Scores and the Gallery's backdrop in the Remake style (docs/menu_remake.md "The other pages"); PC
// Mode and the Jukebox build their Remake layouts in their own files (frontend_pcmode.cpp, frontend_jukebox.cpp) with
// the drawing here. Each page keeps its Classic page's items, texts and actions; only the layout and the look are the
// Remake style's: the live attract backdrop and the splatter, torn panels, parchment strips and tabs, the grungy
// small caps, the prompts at the bottom right, as wide as the window. Classic is untouched (frontend.cpp).
#include "game/frontend.h"

#include "port/game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace hotd::game {

namespace {
using Chars = port::Game<port::A>;
float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
float noise1(int x, int y)
{
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + 0x9E3779B9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}
// the Remake style's colours (frontend_remake.cpp)
constexpr uint32_t kInk = 0xA81410, kInkLit = 0xE0261C, kInkGrey = 0x5E5650;
constexpr uint32_t kRowText = 0xC4BDB5, kRowLit = 0xFFF2E6, kRowOff = 0x6A625C;
std::vector<std::string> fields(const std::string& s)
{
    std::vector<std::string> out;
    size_t p = 0;
    for (size_t e; (e = s.find('|', p)) != std::string::npos; p = e + 1)
        out.push_back(s.substr(p, e - p));
    out.push_back(s.substr(p));
    return out;
}
} // namespace

// ---- the pages' common parts ----
void Frontend::remakePage(int screen, const std::string& key, bool plain)
{
    remakeBegin(screen, key);
    rPlain_ = plain;
    root_->draw = &Frontend::drawRPage;
    rPanel_[0] = rPanel_[1] = rPanel_[2] = rPanel_[3] = 0;   // (the prompts: a column; a page with a panel sets it)
    rHome_ = nullptr;
}

Widget* Frontend::rWidget(int x, int y, int w, int h, void (Frontend::*draw)(Widget&), const std::string& text)
{
    Widget* wd = widgetNew();
    addChild(root_, wd);
    setRect(wd, x, y, w, h);
    wd->draw = draw;
    wd->flags |= 0x100;
    wd->text = text;
    return wd;
}

Widget* Frontend::rStripButton(int x, int y, int w, int h, const std::string& text, const std::string& help,
                               std::function<void()> act, int seed)
{
    Widget* s = rWidget(x, y, w, h, &Frontend::drawRStrip, text);
    s->help = help;
    s->seed = seed;
    s->user = act ? 0 : 1;
    if (act) {
        setHandlers(s, [this, act](Widget&) {
            walking_ = false;
            act();
        }, &Frontend::hitRect);
        s->nav = true;
    }
    return s;
}

// the arrows on a page of cards (rGrid_): the nav widget nearest that way (along it first, then across)
void Frontend::remakeFocusDir(int dx, int dy)
{
    std::vector<Widget*> nav;
    Widget* cur = nullptr;
    for (Widget* w = root_ ? root_->child : nullptr; w; w = w->next)
        if (w->nav && w->activate) {
            nav.push_back(w);
            if (px_ >= w->x && px_ < w->x + w->w && py_ >= w->y && py_ < w->y + w->h)
                cur = w;
        }
    if (!cur) {   // (no focus yet: the page's first)
        if (rHome_) {
            remakeCue();
            px_ = rHome_->x + rHome_->w / 2, py_ = rHome_->y + rHome_->h / 2;
        } else
            remakeFocus(0);
        return;
    }
    const float cx = float(cur->x) + float(cur->w) / 2, cy = float(cur->y) + float(cur->h) / 2;
    Widget* best = nullptr;
    float bestScore = 1e9f;
    for (Widget* w : nav) {
        if (w == cur)
            continue;
        const float ox = float(w->x) + float(w->w) / 2 - cx, oy = float(w->y) + float(w->h) / 2 - cy;
        const float along = ox * float(dx) + oy * float(dy), across = std::fabs(dx ? oy : ox);
        if (along <= 4.0f)
            continue;
        const float score = along + across * 2.0f;
        if (score < bestScore)
            bestScore = score, best = w;
    }
    if (best) {
        remakeCue();   // (remakefix's move sound)
        px_ = best->x + best->w / 2, py_ = best->y + best->h / 2;
    }
}

// the root: the splatter (as the main menu's), a PC picture centred (PC Mode's tombstones), the page's torn panel
void Frontend::drawRPage(Widget&)
{
    if (!gpuLayers) {   // (the GPU menu: the splatter is its own layer, Frontend::splatterLayer)
        const int bx = int(float(cw_) * 0.36f), ex = cw_ - bx, th = int(kHeight * 0.3f);
        auto px = [&](int u) { return int(float(u) * ps_); };
        rCached("SL" + std::to_string(cw_), 0.0f, 0.0f, float(bx), float(kHeight), 0.0f, [&]() { rSplatter(0, 0, px(bx), rph()); });
        rCached("SR" + std::to_string(cw_), float(ex), 0.0f, float(cw_ - ex), float(kHeight), 0.0f,
                [&]() { rSplatter(px(ex), 0, rpw(), rph()); });
        rCached("SM" + std::to_string(cw_), float(bx), 0.0f, float(ex - bx), float(th), 0.0f, [&]() { rSplatter(px(bx), 0, px(ex), px(th)); });
    }
    if (rArt_ >= 0) {   // the picture at its 640 x 480, centred; PC Mode's 0xA4: only its two tombstones, soft edged
        const int id = rArt_, iw = spr_.width(id), ih = spr_.height(id);
        const std::vector<uint8_t>& img = glyphImage(id);
        const float ox = float(cw_ - kWidth) / 2;
        if (iw > 0 && ih > 0 && img.size() >= size_t(iw) * size_t(ih) * 4)
            rCached("ART" + std::to_string(id) + "," + std::to_string(cw_), ox, 0.0f, float(kWidth), float(kHeight), 0.0f, [&]() {
                const float S = ps_;
                // a stone: x0..x1, its arch from y 7 (a half ellipse down to y 111), then straight down
                auto stone = [](float u, float v, float x0, float x1) {
                    const float cx = (x0 + x1) / 2, rx = (x1 - x0) / 2, top = 7.0f, base = 111.0f;
                    float d = std::min(u - x0, x1 - u);
                    if (v < base) {
                        const float ex = (u - cx) / rx, ey = (v - base) / (base - top);
                        d = std::min(d, (1.0f - std::sqrt(ex * ex + ey * ey)) * std::min(rx, base - top));
                    }
                    return d;
                };
                for (int py = 0; py < rph(); ++py)
                    for (int pxl = std::max(0, int(ox * S)); pxl < std::min(rpw(), int((ox + float(kWidth)) * S) + 1); ++pxl) {
                        const float u = float(pxl) / S - ox, v = float(py) / S;
                        float a = 1.0f, edge = 1e9f;
                        if (id == 0xA4)
                            edge = std::max(stone(u, v, 4.0f, 311.0f), stone(u, v, 322.0f, 638.0f)), a = clamp01((edge + 1.0f) / 4.0f);
                        else
                            a = clamp01(std::min(u, float(kWidth) - u) / 24.0f);
                        a *= clamp01((float(kHeight) - v) / 44.0f);   // (the bottom fades into the backdrop)
                        if (a <= 0.0f)
                            continue;
                        const float sx = std::clamp(u * float(iw) / float(kWidth) - 0.5f, 0.0f, float(iw - 1));
                        const float sy = std::clamp(v * float(ih) / float(kHeight) - 0.5f, 0.0f, float(ih - 1));
                        const int x0 = int(sx), y0 = int(sy), x1 = std::min(x0 + 1, iw - 1), y1 = std::min(y0 + 1, ih - 1);
                        const float fx = sx - float(x0), fy = sy - float(y0);
                        float c[3];
                        for (int k = 0; k < 3; ++k) {
                            auto at = [&](int xx, int yy) { return float(img[(size_t(yy) * size_t(iw) + size_t(xx)) * 4 + size_t(k)]); };
                            c[k] = (at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy;
                        }
                        // pmpick (user 2026-10-30: bits of the PC menu's brown backdrop above / beside the stones): near
                        // the mask's edge (the stones' arches and sides curve inside it) the PC's warm brown ground is
                        // keyed out; the stones are blue grey (B at least R), the ground brown and its fog olive (B under R / G)
                        if (edge < 30.0f) {
                            const float brown = clamp01((std::max(c[0], c[1]) - c[2] - 6.0f) / 8.0f);
                            a *= 1.0f - brown;
                            if (a <= 0.0f)
                                continue;
                        }
                        rBlend(pxl, py, (uint32_t(c[0]) << 16) | (uint32_t(c[1]) << 8) | uint32_t(c[2]), a);
                    }
            });
    }
    if (rPage_[2] > 0)
        rTorn(float(rPage_[0]), float(rPage_[1]), float(rPage_[2]), float(rPage_[3]), rPage_[4], true);
}

// a card: a dark torn panel with its red glow, the picture (the PC's boss portraits; lit: the hover picture), the name
// on a parchment strip, a line under it
void Frontend::drawRCard(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const bool lit = (w.flags & 0x40000) != 0, grey = w.user == 1;
    const float S = ps_;
    if (lit) {   // a red glow around it (as the strips')
        const float R = 12.0f, x0 = float(x), y0 = float(y), x1 = float(x + ww), y1 = float(y + hh);
        for (int py = std::max(0, int((y0 - R) * S)); py < std::min(rph(), int((y1 + R) * S)); ++py)
            for (int px = std::max(0, int((x0 - R) * S)); px < std::min(rpw(), int((x1 + R) * S)); ++px) {
                const float ux = float(px) / S, uy = float(py) / S;
                const float d = std::hypot(std::max({x0 - ux, ux - x1, 0.0f}), std::max({y0 - uy, uy - y1, 0.0f}));
                if (d < R)
                    rBlend(px, py, tint(0xB01008), 0.5f * (1.0f - d / R) * (1.0f - d / R));
            }
    }
    rTorn(float(x), float(y), float(ww), float(hh), w.seed, true, 0.94f);
    const int id = lit && w.idHover >= 0 ? w.idHover : w.id;
    const int stripH = 26, picTop = y + 12, picH = hh - stripH - (w.value.empty() ? 30 : 46);
    if (id > 0) {
        const int iw = spr_.width(id), ih = spr_.height(id);
        const std::vector<uint8_t>& img = glyphImage(id);
        if (iw > 0 && ih > 0) {
            const float s = std::min({float(ww - 16) / float(iw), float(picH) / float(ih), 1.5f});
            const float dw = float(iw) * s, dh = float(ih) * s;
            rImage(img, iw, ih, float(x) + (float(ww) - dw) / 2, float(picTop) + (float(picH) - dh) / 2, dw, dh, grey ? 0.35f : 1.0f);
        }
    }
    const int sy = picTop + picH + 4;
    const float tilt = (float(w.seed % 3) - 1.0f) * 0.006f;
    rPaper(float(x + 8), float(sy), float(ww - 16), float(stripH), w.seed + 3, tilt, lit ? 0 : grey ? 2 : 1);
    float th = 15.0f;
    if (const float tw = rTextWidth(w.text, th); tw > float(ww - 34))
        th *= float(ww - 34) / tw;
    rText(w.text, float(x + ww / 2), float(sy) + (float(stripH) - th) / 2 + 1, th,
          lit ? paperInk(kInkLit) : grey ? kInkGrey : paperInk(kInk), 1, 0.12f, w.seed, grey ? kTxShadow : kTxOutline | kTxShadow);
    if (lit)   // the arcade's claw marks (0x2A) at the strip's left end
        rBlood(2, float(x + 6), float(sy + stripH / 2), float(stripH) * 1.5f, 0.0f, 0.9f);
    if (!w.value.empty()) {
        float vh = 11.0f;
        if (const float tw = rTextWidth(w.value, vh); tw > float(ww - 16))
            vh *= float(ww - 16) / tw;
        rText(w.value, float(x + ww / 2), float(sy + stripH + 8), vh, grey ? kRowOff : lit ? kRowLit : kRowText, 1, 0.05f, w.seed + 5,
              kTxOutline | kTxShadow);
    }
}

// a PC sprite fitted in the rectangle, centred across, from its top (user 1: at half size, as the 4P panels')
void Frontend::drawRSprite(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const int iw = spr_.width(w.id), ih = spr_.height(w.id);
    const std::vector<uint8_t>& img = glyphImage(w.id);
    if (iw <= 0 || ih <= 0)
        return;
    const float s = std::min({w.user == 1 ? 0.5f : 1.0f, float(ww) / float(iw), float(hh) / float(ih)});
    rImage(img, iw, ih, float(x) + (float(ww) - float(iw) * s) / 2, float(y), float(iw) * s, float(ih) * s, 1.0f);
}

void Frontend::drawRStat(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const float th = w.user > 0 ? float(w.user) : 15.0f, ty = float(y) + (float(hh) - th) / 2;
    rText(w.text, float(x), ty, th, kRowText, 0, 0.05f, w.seed + 1, kTxOutline | kTxShadow);
    rText(w.value, float(x + ww), ty, th, kRowLit, 2, 0.05f, w.seed + 2, kTxOutline | kTxShadow);
}

// a left / right arrow: a chevron with a soft shadow (lit: red)
void Frontend::drawRSide(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const bool lit = (w.flags & 0x40000) != 0 && w.activate;
    const uint32_t col = w.seed == 1 ? kRowOff : lit ? tint(kInkLit) : kRowLit;
    const float S = ps_, s = float(std::min(ww, hh)) * 0.42f, dir = w.user ? 1.0f : -1.0f;
    const float cx = float(x) + float(ww) / 2, cy = float(y) + float(hh) / 2;
    for (int pass = 0; pass < 2; ++pass) {
        const float ox = pass ? 0.0f : 0.8f, oy = pass ? 0.0f : 1.2f;
        for (int py = std::max(0, int((cy - s) * S)); py < std::min(rph(), int((cy + s + 2) * S)); ++py)
            for (int px = std::max(0, int((cx - s) * S)); px < std::min(rpw(), int((cx + s + 2) * S)); ++px) {
                const float ux = (float(px) + 0.5f) / S - cx - ox, uy = (float(py) + 0.5f) / S - cy - oy;
                const float ax = ux * dir;   // (pointing right: a ">" of two strokes)
                const float line = std::fabs(ax + std::fabs(uy) * 0.85f - s * 0.25f);
                const float wdt = s * 0.2f;
                if (std::fabs(uy) <= s * 0.7f && line < wdt)
                    rBlend(px, py, pass ? col : 0x000000, clamp01((wdt - line) * S) * (pass ? 1.0f : 0.6f));
            }
    }
}

// a table row: the name left, the other fields right-aligned in their columns (the last at the right edge, a picture
// right of it when id is set); user bit 0: the row just entered (a red bar), bit 1: a faint stripe
void Frontend::drawRScore(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const float S = ps_;
    const bool hi = (w.user & 1) != 0;
    if (hi || (w.user & 2))
        for (int py = int(float(y) * S); py < int(float(y + hh) * S); ++py)
            for (int px = int(float(x) * S); px < int(float(x + ww) * S); ++px) {
                const float t = (float(px) / S - float(x)) / float(ww);
                if (hi)
                    rBlend(px, py, tint(0x7A0A08), 0.45f * (1.0f - t * 0.7f));
                else
                    rBlend(px, py, 0x000000, 0.22f);
            }
    const float th = 15.0f, ty = float(y) + (float(hh) - th) / 2;
    const uint32_t col = hi ? kRowLit : kRowText;
    rText(w.text, float(x + 10), ty, th, col, 0, 0.05f, w.seed + 1, kTxOutline | kTxShadow);
    const std::vector<std::string> f = fields(w.value);
    const int n = int(f.size());
    float right = float(x + ww - 10);
    if (w.id > 0 && spr_.width(w.id) > 0) {   // the character's small picture at the right end
        const int iw = spr_.width(w.id), ih = spr_.height(w.id);
        const float s = std::min(1.0f, float(hh - 2) / float(ih));
        rImage(glyphImage(w.id), iw, ih, right - float(iw) * s, float(y) + (float(hh) - float(ih) * s) / 2, float(iw) * s, float(ih) * s, 1.0f);
        right -= float(iw) * s + 6;
    }
    // the columns' right edges: one field: the right; two: 62 %, the right; three: 40 %, 62 %, the right
    for (int i = 0; i < n; ++i) {
        const float at = i == n - 1 ? right : float(x) + float(ww) * (n == 2 ? 0.62f : i == 0 ? 0.42f : 0.64f);
        float fh = th, fy = ty;
        if (n == 3 && i == n - 1) {   // pmpick: PC Mode's character name kept a clear gap off the score (any language)
            const float room = right - (float(x) + float(ww) * 0.64f) - 24.0f, tw = rTextWidth(f[size_t(i)], th);
            if (tw > room && room > 0)
                fh = std::max(th * 0.6f, th * room / tw), fy = ty + (th - fh) / 2;
        }
        rText(f[size_t(i)], at, fy, fh, i == n - 1 ? (hi ? kRowLit : 0xE2DAD2) : col, 2, 0.05f, w.seed + 3 + i, kTxOutline | kTxShadow);
    }
}

// 4P: a seat's quarter panel: a dark veil, a two-unit frame in the seat's colour
void Frontend::drawRFrame(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const float S = ps_;
    const uint32_t c = uint32_t(w.user) & 0xFFFFFF;
    for (int py = std::max(0, int(float(y) * S)); py < std::min(rph(), int(float(y + hh) * S)); ++py)
        for (int px = std::max(0, int(float(x) * S)); px < std::min(rpw(), int(float(x + ww) * S)); ++px) {
            const float ux = float(px) / S - float(x), uy = float(py) / S - float(y);
            const float d = std::min({ux, uy, float(ww) - ux, float(hh) - uy});
            if (d < 2.0f)
                rBlend(px, py, c, 0.9f);
            else
                rBlend(px, py, 0x080506, 0.5f);
        }
}

// the Jukebox's list row: the chosen one on a red bar (the focus's look), the hovered one lit; its number at the
// right (Info on)
void Frontend::drawRJRow(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const bool lit = (w.flags & 0x40000) != 0, chosen = (w.user & 1) != 0;
    const float S = ps_;
    const bool rtl = w.rtlRow;   // (leftovers: mirrored: the bar fading to the left, its mark at the right, the text right)
    if (chosen || lit) {
        const int bx0 = rtl ? x : x - 8, bx1 = rtl ? x + ww + 8 : x + ww;
        for (int py = int(float(y) * S); py < int(float(y + hh) * S); ++py)
            for (int px = int(float(bx0) * S); px < int(float(bx1) * S); ++px) {
                const float t = rtl ? (float(bx1) - float(px) / S) / float(ww + 8) : (float(px) / S - float(x - 8)) / float(ww + 8);
                rBlend(px, py, tint(0x7A0A08), (chosen ? 0.42f : 0.22f) * (1.0f - t * 0.85f));
            }
        if (chosen)
            for (int py = int(float(y + 2) * S); py < int(float(y + hh - 2) * S); ++py)
                for (int px = int(float(rtl ? x + ww + 5 : x - 8) * S); px < int(float(rtl ? x + ww + 8 : x - 5) * S); ++px)
                    rBlend(px, py, tint(0xD0180E), 0.95f);
    }
    const float th = 14.0f, ty = float(y) + (float(hh) - th) / 2;
    rText(w.text, float(rtl ? x + ww - 6 : x + 6), ty, th, chosen || lit ? kRowLit : kRowText, rtl ? 2 : 0, 0.05f, w.seed + 1,
          kTxOutline | kTxShadow);
    if (!w.value.empty())
        rText(w.value, float(rtl ? x + 6 : x + ww - 6), ty + 1, 12.0f, kRowOff, rtl ? 0 : 2, 0.0f, 0, kTxShadow);
}

// the progress bar: a dark trough with a thin rim, the played part in the blood red (a little uneven, wet at its end)
void Frontend::drawRJBar(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const float S = ps_, fill = float(ww) * float(std::clamp(w.user, 0, 1000)) / 1000.0f;
    for (int py = int(float(y) * S); py < int(float(y + hh) * S); ++py)
        for (int px = int(float(x) * S); px < int(float(x + ww) * S); ++px) {
            const float ux = float(px) / S - float(x), uy = float(py) / S - float(y);
            const float d = std::min({ux, uy, float(ww) - ux, float(hh) - uy});
            if (d < 1.0f) {
                rBlend(px, py, 0x6A2018, 0.9f);
                continue;
            }
            if (ux < fill) {
                const float n = noise1(int(ux * 0.7f), int(uy * 0.7f)) * 0.25f, end = clamp01(1.0f - (fill - ux) / 6.0f);
                rBlend(px, py, tint(0xA00C08), 1.0f);
                rBlend(px, py, 0x2A0302, n + 0.25f * clamp01(uy / float(hh)));
                rBlend(px, py, 0xF06050, end * 0.35f * (1.0f - uy / float(hh)));
            } else
                rBlend(px, py, 0x0C0706, 0.85f);
        }
}

void Frontend::drawRBox(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    rTorn(float(x), float(y), float(ww), float(hh), w.seed, false, 0.92f);
}

// plain Remake text: id the alignment (0 left, 1 centre, 2 right), the rectangle's height the letters' size, user the
// colour (an item with a click: red while hovered)
void Frontend::drawRText(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const bool lit = (w.flags & 0x40000) != 0 && w.activate;
    const float ax = w.id == 1 ? float(x) + float(ww) / 2 : w.id == 2 ? float(x + ww) : float(x);
    rText(w.text, ax, float(y), float(hh), lit ? tint(kInkLit) : uint32_t(w.user), w.id, 0.05f, w.seed, kTxOutline | kTxShadow);
}

// ---- Chapter Select (screen 9): the four chapters as cards across the width, the ending, Exit ----
void Frontend::remakeChapters()
{
    remakePage(9, "chapters", true);
    rebuild_ = [this]() { remakeChapters(); };
    rGrid_ = true;
    Widget* title = rWidget(0, 12, cw_, 44, &Frontend::drawRLabel, "Chapter Select");
    title->user = int(kRowLit), title->seed = 17;
    static const char* const kBoss[4] = {"The Chariot", "The Hanged Man", "The Hermit", "The Magician"};
    const int gap = 14, cardW = std::min(176, (cw_ - 48 - 3 * gap) / 4), x0 = (cw_ - (4 * cardW + 3 * gap)) / 2;
    for (int c = 0; c < 4; ++c) {
        const bool on = chapterEnabled[c] && onChapter;
        Widget* card = rWidget(x0 + c * (cardW + gap), 72, cardW, 252, &Frontend::drawRCard, "Chapter " + std::to_string(c + 1));
        card->value = kBoss[c];
        card->id = int16_t(0xE3 + c);
        card->idHover = int16_t(on ? 0xE8 + c : -1);
        card->user = on ? 0 : 1;
        card->seed = 41 + 13 * c;
        card->help = on ? "Start Chapter " + std::to_string(c + 1) + " (its boss: " + kBoss[c] + ")"
                        : "Chapter " + std::to_string(c + 1) + ": not playable yet";
        if (on) {
            setHandlers(card, [this, c](Widget&) {
                walking_ = false;
                if (onChapter) {   // playerpick: the players first (Back: Chapter Select)
                    const auto f = onChapter;
                    askPlayers("Chapter " + std::to_string(c + 1), [f, c]() { f(c); }, [this]() { screenChapters(); });
                }
            }, &Frontend::hitRect);
            card->nav = true;
        }
    }
    // the ending: no start of its own (it plays after Chapter 4's Magician)
    const int ew = std::min(240, cw_ - 80);
    Widget* end = rWidget((cw_ - ew) / 2, 338, ew, 26, &Frontend::drawRStrip, "The Ending");
    end->user = chapterEnabled[4] ? 0 : 1;
    end->seed = 211;
    end->help = chapterEnabled[4] ? "The ending plays after the Magician: clear Chapter 4 to see it" : "The ending: not ported yet";
    Widget* note = rWidget(0, 366, cw_, 18, &Frontend::drawRLabel, chapterEnabled[4] ? "after Chapter 4" : "not ported yet");
    note->user = int(kRowText), note->seed = 19;
    rStripButton(cw_ / 2 - 80, 404, 160, 26, label(0x24), "Back to the main menu (Escape)", [this]() { screenMainMenu(); }, 300);
    remakePrompts(root_, false, [this]() { screenMainMenu(); });
    if (remakeFirstFocus_ && !handShown_)
        remakeFocus(0);
}

// ---- Boss Mode (screen 7): the four bosses and All Bosses as cards across the width, Exit ----
void Frontend::remakeBossMode()
{
    remakePage(7, "bossmode", true);
    rebuild_ = [this]() { remakeBossMode(); };
    rGrid_ = true;
    Widget* title = rWidget(0, 12, cw_, 44, &Frontend::drawRLabel, label(0x42));   // "Boss Mode"
    title->user = int(kRowLit), title->seed = 23;
    static const char* const kHelp[5] = {
        "The Chariot (Chapter 1's boss): beat it as fast as you can, the best times go in its table",
        "The Hanged Man (Chapter 2's boss): beat it as fast as you can, the best times go in its table",
        "The Hermit (Chapter 3's boss): beat it as fast as you can, the best times go in its table",
        "The Magician (Chapter 4's boss): beat it as fast as you can, the best times go in its table",
        "All Bosses: the four bosses one after the other on one clock"};
    static const char* const kLine[5] = {"Chapter 1", "Chapter 2", "Chapter 3", "Chapter 4", "One clock"};
    const int gap = 10, cardW = std::min(160, (cw_ - 36 - 4 * gap) / 5), x0 = (cw_ - (5 * cardW + 4 * gap)) / 2;
    for (int b = 0; b < 5; ++b) {
        Widget* card = rWidget(x0 + b * (cardW + gap), 72, cardW, 280, &Frontend::drawRCard, label(0x45 + b));   // "Chariot Boss" ..
        card->value = kLine[b];
        card->id = int16_t(0xE3 + b);
        card->idHover = int16_t(0xE3 + b + 5);
        card->seed = 61 + 11 * b;
        card->help = kHelp[b];
        setHandlers(card, [this, b](Widget&) {   // (0x422EE4 ..: boss mode 2-6)
            walking_ = false;
            if (onBossMode) {   // playerpick: the players first (Back: Boss Mode)
                const auto f = onBossMode;
                askPlayers(label(0x45 + b), [f, b]() { f(b + 2); }, [this]() { screenBossMode(); });
            }
        }, &Frontend::hitRect);
        card->nav = true;
    }
    rStripButton(cw_ / 2 - 80, 404, 160, 26, label(0x24), "Back to the main menu (Escape)", [this]() { screenMainMenu(); }, 300);
    remakePrompts(root_, false, [this]() { screenMainMenu(); });
    if (remakeFirstFocus_ && !handShown_)
        remakeFocus(0);
}

// ---- High Scores (screen 8) and Boss Mode's times (32-36): the table on a dark torn panel, its name on a raised
// tab, the tables turned by Left / Right (Q / E, the prompts, the arrows beside the tab) ----
void Frontend::remakeRanking()
{
    remakePage(8, "ranking", false);
    rebuild_ = [this]() { remakeRanking(); };
    const int nTables = std::max(1, int(rankingTables.size()));
    rankingTable = std::clamp(rankingTable, 0, nTables - 1);
    const std::string name = rankingTable == 0 || rankingTables.empty() ? std::string(label(0x44)) : rankingTables[size_t(rankingTable)];
    const std::vector<RankRow> rows = rankingTableRows ? rankingTableRows(rankingTable) : rankingRows ? rankingRows() : std::vector<RankRow>();
    // the page's frame: the title, the tab, the panel, Exit (as the boss tables')
    Widget* title = rWidget(0, 4, cw_, 44, &Frontend::drawRLabel, label(0x43));   // "High Scores"
    title->user = int(kRowLit), title->seed = 29;
    const int pw = std::min(600, cw_ - 60), px = (cw_ - pw) / 2, py = 94, ph = 284;
    rPage_[0] = px, rPage_[1] = py, rPage_[2] = pw, rPage_[3] = ph, rPage_[4] = 9;
    rPanel_[0] = px, rPanel_[1] = 4, rPanel_[2] = pw, rPanel_[3] = 412 - 4;   // (the prompts: a row when they would meet)
    const int tw = std::min(300, pw - 80);
    Widget* tab = rWidget((cw_ - tw) / 2, 50, tw, 44, &Frontend::drawRTab, name);
    tab->user = 1, tab->seed = 500;
    tab->help = nTables > 1 ? "This table (Left / Right or Q / E: the others)" : "The arcade's table";
    if (nTables > 1) {
        remakeTabs_ = nTables, rTabCur_ = rankingTable;
        rTabFn_ = [this](int i) {
            rankingTable = i;
            screenRanking();
        };
        for (int d = 0; d < 2; ++d) {
            Widget* a = rWidget(d ? (cw_ + tw) / 2 + 6 : (cw_ - tw) / 2 - 34, 58, 28, 28, &Frontend::drawRSide);
            a->user = d;
            a->help = d ? "The next table" : "The table before";
            setHandlers(a, [this, d, nTables](Widget&) {
                walking_ = false;
                rankingTable = (rankingTable + (d ? 1 : nTables - 1)) % nTables;
                screenRanking();
            }, &Frontend::hitRect);
        }
    }
    if (rows.empty()) {
        Widget* none = rWidget(px, py + ph / 2 - 20, pw, 34, &Frontend::drawRLabel, "No scores yet");
        none->user = int(kRowOff), none->seed = 31;
    } else {
        const bool pc = rows[0].ch >= 0;   // PC Mode's table: the character too (its name and small picture)
        bool stages = false;
        for (const RankRow& r : rows)
            stages = stages || (!r.stage.empty() && r.stage != "-");
        for (int i = 0; i < 10; ++i) {
            const bool has = size_t(i) < rows.size();
            const RankRow r = has ? rows[size_t(i)] : RankRow{};
            Widget* w = rWidget(px + 22, py + 14 + i * 26, pw - 44, 24, &Frontend::drawRScore, r.name);
            w->value = r.time ? std::string("|") + r.stage   // a boss's times: the time in the last column
                              : (stages || pc ? r.stage : std::string()) + "|" + (has ? std::to_string(r.score) : std::string());
            w->id = -1;
            if (pc && has) {
                const auto& ch = Chars::pm_table(r.ch);
                w->value += std::string("|") + (r.ch == 0 ? "No Player" : ch.name);
                w->id = int16_t(pcPortraitId(r.ch, true));
            }
            w->user = (i & 1) ? 2 : 0;
            w->seed = 700 + i * 3;
        }
    }
    rStripButton(cw_ / 2 - 80, 386, 160, 26, label(0x24), "Back to the main menu (Escape)", [this]() { screenMainMenu(); }, 300);
    rHelp_[1] = 421;
    remakePrompts(root_, nTables > 1, [this]() { screenMainMenu(); });
    if (remakeFirstFocus_ && !handShown_)
        remakeFocus(0);
}

void Frontend::remakeBossTimes(int mode, int highlight)
{
    remakePage(30 + mode, "bosstimes", false);   // (one key: the focus stays on Exit while the tables turn)
    rebuild_ = [this, mode, highlight]() { remakeBossTimes(mode, highlight); };
    Widget* title = rWidget(0, 4, cw_, 44, &Frontend::drawRLabel, label(0x43));   // "High Scores"
    title->user = int(kRowLit), title->seed = 29;
    const int pw = std::min(560, cw_ - 60), px = (cw_ - pw) / 2, py = 94, ph = 284;
    rPage_[0] = px, rPage_[1] = py, rPage_[2] = pw, rPage_[3] = ph, rPage_[4] = 9;
    rPanel_[0] = px, rPanel_[1] = 4, rPanel_[2] = pw, rPanel_[3] = 412 - 4;
    const int tw = std::min(300, pw - 80);
    Widget* tab = rWidget((cw_ - tw) / 2, 50, tw, 44, &Frontend::drawRTab, label(0x45 + mode - 2));   // "Chariot Boss" ..
    tab->user = 1, tab->seed = 500 + mode;
    tab->help = "This boss's best times (Left / Right or Q / E: the others)";
    remakeTabs_ = 5, rTabCur_ = mode - 2;
    rTabFn_ = [this](int i) { screenBossTimes(2 + i); };
    for (int d = 0; d < 2; ++d) {   // (the PC's arrows 0x424D64 / 0x424E10: the boss before / after)
        Widget* a = rWidget(d ? (cw_ + tw) / 2 + 6 : (cw_ - tw) / 2 - 34, 58, 28, 28, &Frontend::drawRSide);
        a->user = d;
        a->help = d ? "The next boss's times" : "The previous boss's times";
        setHandlers(a, [this, d, mode](Widget&) {
            walking_ = false;
            screenBossTimes(2 + (mode - 2 + (d ? 1 : 4)) % 5);
        }, &Frontend::hitRect);
    }
    const std::vector<RankRow> rows = bossRows ? bossRows(mode) : std::vector<RankRow>();
    for (int i = 0; i < 10; ++i) {
        const RankRow r = size_t(i) < rows.size() ? rows[size_t(i)] : RankRow{};
        Widget* w = rWidget(px + 22, py + 14 + i * 26, pw - 44, 24, &Frontend::drawRScore, r.name);
        w->value = r.stage;   // (the time "%02d:%02d:%02d")
        w->id = -1;
        w->user = (i == highlight ? 1 : 0) | ((i & 1) ? 2 : 0);
        w->seed = 760 + i * 3;
    }
    rStripButton(cw_ / 2 - 80, 386, 160, 26, label(0x24), "Back to the main menu (Escape)", [this]() { screenMainMenu(); }, 300);
    rHelp_[1] = 421;
    remakePrompts(root_, true, [this]() { screenMainMenu(); });
    if (remakeFirstFocus_ && !handShown_)
        remakeFocus(0);
}

// ---- the Gallery (its Classic layout) and the Cut-scenes page over the Remake backdrop ----
// the page's own ground: a dark veil over its 640 units, soft at the sides (the backdrop shows beside it)
void Frontend::drawBackdropShade(Widget&)
{
    for (int j = 0; j < kHeight; ++j)
        for (int i = 0; i < cw_; ++i) {
            const float edge = clamp01(std::min(float(i), float(cw_ - 1 - i)) / 48.0f);
            (*canvas_)[(size_t(j) * size_t(cw_) + size_t(i)) * 4 + 3] = uint8_t(150.0f * edge * edge * (3 - 2 * edge));
        }
}

const Frontend::RemakeLayers& Frontend::backdropLayers()
{
    const int cw = cw_;
    const bool gl = gpuLayers, plain = rPlain_;
    cw_ = wideW_, gpuLayers = true, rPlain_ = true;   // (as wide as the window; untinted, as the main menu's)
    bloodHue_ = bloodColour();
    rBackground();
    gpuLayers = gl, rPlain_ = plain;
    RemakeLayers& L = layers_;
    // the splatter (as remakeRender's): drawn on a worker when its pattern, colour or size changes
    const int PH = rph();
    const std::string want = std::to_string(cw_) + "," + std::to_string(PH) + "," + std::to_string(splatSeed) + "," + std::to_string(bloodHue_);
    if (splatJob_.valid() && splatJob_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        splatImg_ = splatJob_.get();
        splatImgW_ = splatJobW_, splatImgH_ = splatJobH_;
        splatKey_ = splatJobKey_;
        ++splatGen_;
    }
    if (want != splatKey_ && !splatJob_.valid()) {
        splatJobKey_ = want, splatJobW_ = rpw(), splatJobH_ = PH;
        splatJob_ = std::async(std::launch::async, &Frontend::splatterLayer, cw_, ps_, splatSeed, bloodHue_);
    }
    L.splat = splatImg_.empty() ? nullptr : &splatImg_;
    L.spw = splatImgW_, L.sph = splatImgH_, L.splatGen = splatGen_;
    L.fg = nullptr, L.parts = nullptr, L.hand = nullptr, L.anim = nullptr, L.noBackdrop = false;   // (regallery: none of its parts)
    L.fw = rpw(), L.fh = PH, L.unitsW = cw_, L.fade = 0.0f;
    cw_ = cw;
    return L;
}

std::vector<uint8_t> Frontend::backdropPicture(int width, int height)
{
    std::vector<uint8_t> out(size_t(width) * size_t(height) * 3, 0);
    const int cw = cw_;
    const bool gl = gpuLayers, plain = rPlain_;
    cw_ = wideW_, gpuLayers = false, rPlain_ = true;
    bloodHue_ = bloodColour();
    bgKey_ = -1;
    rBackground();   // bgLine_: cw_ x 480 units, RGB
    const std::vector<uint8_t> splat = splatterLayer(cw_, ps_, splatSeed, bloodHue_);
    const int PW = rpw(), PH = rph();
    const float s = std::min(float(width) / float(cw_), float(height) / float(kHeight));
    const float x0 = (float(width) - float(cw_) * s) / 2, y0 = (float(height) - float(kHeight) * s) / 2;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const float ux = (float(x) + 0.5f - x0) / s, uy = (float(y) + 0.5f - y0) / s;
            if (ux < 0 || uy < 0 || ux >= float(cw_) || uy >= float(kHeight))
                continue;
            const uint8_t* b = &bgLine_[(size_t(uy) * size_t(cw_) + size_t(ux)) * 3];
            const int sx = std::min(PW - 1, int(ux * ps_)), sy = std::min(PH - 1, int(uy * ps_));
            const uint8_t* p = &splat[(size_t(sy) * size_t(PW) + size_t(sx)) * 4];
            uint8_t* d = &out[(size_t(y) * size_t(width) + size_t(x)) * 3];
            for (int k = 0; k < 3; ++k)
                d[k] = uint8_t((p[k] * p[3] + b[k] * (255 - p[3]) + 127) / 255);
        }
    gpuLayers = gl, rPlain_ = plain;
    cw_ = cw;
    return out;
}

} // namespace hotd::game
