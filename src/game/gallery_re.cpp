// regallery (2026-10-22, user: "The Remake-style menu still has the Classic Gallery layout and font. Revamp it to look
// like a Resident Evil gallery"): the Gallery and its Cut-scenes page in the Remake menu style, laid out in the manner
// of the Resident Evil 2 / 3 / 4 remakes' and Requiem's galleries (only the layout and the feel; every picture is ours or
// the player's own files, the letters the PC's font as the other Remake pages draw it): the model large on a dark, softly
// lit stage (the host draws it under the page: Gallery::reStage, a floor reflection, the panels' shade), a slim left
// column (the title, the categories, the category's entries in a scrolling list), the "file" on the right (the name in
// large outlined caps, a thin rule in the blood colour, the category and chapters, the description, the stats as small
// labelled rows, the boss's weak-point card; under it the viewer's settings as rows), the controls as key caps along
// the bottom. Switching entries cross-fades and slides the file and fades the model in (Gallery::select); a new
// category slides the list in. The Classic style keeps the Classic page (frontend.cpp); K still switches the layouts.
// The Cut-scenes page in the same language: the scenes by chapter in the middle, the scene's file with its options.
#include "game/frontend.h"
#include "util/utf8.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

namespace hotd::game {

namespace {
float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
// the colours (frontend_remake.cpp's)
constexpr uint32_t kInkLit = 0xE0261C;
constexpr uint32_t kRowText = 0xC4BDB5, kRowLit = 0xFFF2E6, kRowOff = 0x6A625C;
constexpr uint32_t kBody = 0xCDC5BD;    // the description
constexpr uint32_t kDim = 0x8E867E;     // the labels, the other categories / entries
constexpr int kTx = 3;                  // kTxOutline | kTxShadow
constexpr int kListRows = 9, kRowStep = 17;
constexpr int kOptArrows = 4;          // galfix2: drawReOpt's user bit: "< value >"
constexpr float kOptArrowW = 11.0f;    // (the room each arrow takes, units)

// the page's columns (units of the wide screen, 480 high)
struct Cols {
    int m, lx, lw, rx, rw, stageX, stageW;
};
Cols cols(int cw, bool info)
{
    Cols c;
    c.m = cw >= 760 ? 30 : 20;
    c.lw = std::clamp(int(float(cw) * 0.19f), 128, 176);
    c.rw = std::clamp(int(float(cw) * 0.30f), 196, 280);
    c.lx = c.m;
    c.rx = cw - c.m - c.rw;
    c.stageX = c.lx + c.lw + 12;
    c.stageW = (info ? c.rx - 14 : cw - c.m) - c.stageX;
    return c;
}

std::vector<std::string> sentences(const std::string& s)
{
    std::vector<std::string> out;
    size_t p = 0;
    while (p < s.size()) {
        size_t e = s.find(". ", p);
        std::string t = s.substr(p, e == std::string::npos ? std::string::npos : e - p);
        while (!t.empty() && (t.back() == '.' || t.back() == ' ')) t.pop_back();
        if (!t.empty()) out.push_back(t);
        if (e == std::string::npos) break;
        p = e + 2;
    }
    return out;
}
}   // namespace

// ---- the drawing ----
void Frontend::drawReRule(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const float S = ps_;
    const uint32_t c = tint(0xB8140C);
    for (int py = int(float(y) * S); py < int((float(y) + 1.2f) * S); ++py)
        for (int px = int(float(x) * S); px < int(float(x + ww) * S); ++px) {
            const float t = (float(px) / S - float(x)) / float(std::max(1, ww));
            rBlend(px, py, c, 0.9f * (1.0f - t) * (1.0f - t) + 0.08f);
        }
}

void Frontend::drawReCat(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const bool lit = (w.flags & 0x40000) != 0, cur = (w.user & 1) != 0, grey = (w.user & 2) != 0;
    const float S = ps_;
    if (cur || lit)   // the current one: a red bar fading right, a mark at its left (hovered: fainter)
        for (int py = int(float(y) * S); py < int(float(y + hh) * S); ++py)
            for (int px = int(float(x) * S); px < int(float(x + ww) * S); ++px) {
                const float t = (float(px) / S - float(x)) / float(ww);
                rBlend(px, py, tint(0x7A0A08), (cur ? 0.5f : 0.22f) * (1.0f - t) * (1.0f - t));
                if (cur && float(px) / S < float(x) + 2.5f)
                    rBlend(px, py, tint(0xD0180E), 0.95f);
            }
    const float th = 13.0f;
    rText(w.text, float(x + 10), float(y) + (float(hh) - th) / 2, th, grey ? kRowOff : cur || lit ? kRowLit : kDim, 0, 0.0f, w.seed, kTx);
}

void Frontend::drawReRow(Widget& w)
{
    if (!(w.flags & 0x40000))   // (the row's text is in the list's part: only the hover is drawn here)
        return;
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const float S = ps_;
    for (int py = int(float(y) * S); py < int(float(y + hh) * S); ++py)
        for (int px = int(float(x) * S); px < int(float(x + ww) * S); ++px) {
            const float t = (float(px) / S - float(x)) / float(ww);
            rBlend(px, py, tint(0x7A0A08), 0.3f * (1.0f - t));
        }
    float th = 11.0f;   // (as the list draws it: a long name smaller, then cut)
    std::string s = w.text;
    const float room = float(ww) - 18.0f;
    if (const float tw = rTextWidth(s, th); tw > room) th = std::max(9.0f, th * room / tw);
    s = util::utf8Fit(s, "..", [&](const std::string& t) { return rTextWidth(t, th) <= room; });   // (misc18: by letters)
    rText(s, float(x + 10), float(y) + (float(hh) - th) / 2, th, kRowLit, 0, 0.0f, w.seed, kTx);
}

void Frontend::drawRePrompt(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const bool lit = (w.flags & 0x40000) != 0 && w.activate;
    const size_t bar = w.value.find('|');
    const std::string key = padPrompts_ ? padKey(w.value.substr(bar + 1)) : w.value.substr(0, bar);
    const float kh = 15.0f, kt = 7.0f;
    const bool ring = ringKey(key);
    const size_t nIcons = padIconsOf(key).size();   // padglyphs: the pad's icons, one cap wide each
    const float kw = nIcons ? kh * float(nIcons) : ring ? kh : std::max(kh + 4.0f, rTextWidth(key, kt) + 10.0f);
    const float ky = float(y) + (float(hh) - kh) / 2;
    if (!rPadGlyph(key, float(x) + kw / 2, ky + kh / 2, kh / 2)) {
        rKeyCap(float(x), ky, kw, kh, ring);
        if (!rPadSymbol(key, float(x) + kw / 2, ky + kh / 2, kh / 2))   // (padpass: a PlayStation pad's shapes)
            rText(key, float(x) + kw / 2, ky + (kh - kt) / 2, kt, 0xF4ECE4, 1, 0.0f, 0);
    }
    const float lh = 10.0f;
    rText(w.text, float(x) + kw + 6.0f, ky + (kh - lh) / 2, lh, lit ? tint(kInkLit) : 0xD8D0C8, 0, 0.0f, w.seed, kTx);
}

void Frontend::drawReOpt(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const bool lit = (w.flags & 0x40000) != 0 && w.activate, grey = (w.user & 1) != 0;
    const bool arrows = (w.user & kOptArrows) != 0 && !grey;   // galfix2: "< value >" (a value that steps both ways)
    const float S = ps_;
    if (lit)
        for (int py = int(float(y) * S); py < int(float(y + hh) * S); ++py)
            for (int px = int(float(x - 6) * S); px < int(float(x + ww) * S); ++px) {
                const float t = (float(px) / S - float(x - 6)) / float(ww + 6);
                rBlend(px, py, tint(0x7A0A08), 0.32f * (1.0f - t * 0.8f));
            }
    for (int px = int(float(x) * S); px < int(float(x + ww) * S); ++px)   // a faint line under the row
        for (int py = int(float(y + hh) * S) - 1; py < int(float(y + hh) * S); ++py)
            rBlend(px, py, 0xFFFFFF, 0.08f);
    const float lh = 9.0f, vh = 10.5f;
    rText(w.text, float(x), float(y) + (float(hh) - lh) / 2, lh, grey ? kRowOff : kDim, 0, 0.0f, w.seed, kTx);
    const float ax = arrows ? kOptArrowW : 0.0f;   // (the room each arrow takes)
    std::string v = w.value;
    {
        const float vroom = float(ww) - rTextWidth(w.text, lh) - 16.0f - 2.0f * ax;
        v = util::utf8Fit(v, "..", [&](const std::string& t) { return rTextWidth(t, vh) <= vroom; });   // (misc18: by letters)
    }
    const uint32_t vc = grey ? kRowOff : lit ? tint(kInkLit) : kRowLit;
    rText(v, float(x + ww) - ax, float(y) + (float(hh) - vh) / 2, vh, vc, 2, 0.0f, w.seed + 1, kTx);
    if (arrows) {   // small solid triangles either side of the value, pointing out
        const float cy = float(y) + float(hh) / 2, r = 3.2f;
        const float xr = float(x + ww) - ax / 2, xl = float(x + ww) - ax - rTextWidth(v, vh) - ax / 2;
        const uint32_t ac = lit ? tint(kInkLit) : kDim;
        for (int k = 0; k < 2; ++k) {
            const float cx = k ? xr : xl, dir = k ? 1.0f : -1.0f;
            for (int py = int((cy - r) * S); py < int((cy + r) * S) + 1; ++py)
                for (int px = int((cx - r) * S); px < int((cx + r) * S) + 1; ++px) {
                    const float ux = (float(px) + 0.5f) / S - cx, uy = (float(py) + 0.5f) / S - cy;
                    const float t = (ux * dir + r * 0.7f) / (r * 1.4f);   // 0 at the base, 1 at the tip
                    if (t < 0.0f || t > 1.0f) continue;
                    const float half = r * (1.0f - t);
                    if (std::fabs(uy) <= half) rBlend(px, py, ac, clamp01((half - std::fabs(uy)) * S + 0.5f) * 0.95f);
                }
        }
    }
}

// galfix2: a Viewer row's "<" (a click on it steps back): the left arrow's middle, as drawReOpt places it
float Frontend::reOptLeftArrow(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const float lh = 9.0f, vh = 10.5f, ax = kOptArrowW;
    std::string v = w.value;
    {
        const float vroom = float(ww) - rTextWidth(w.text, lh) - 16.0f - 2.0f * ax;
        v = util::utf8Fit(v, "..", [&](const std::string& t) { return rTextWidth(t, vh) <= vroom; });   // (misc18: by letters)
    }
    return float(x + ww) - ax - rTextWidth(v, vh) - ax / 2;
}

void Frontend::drawReButton(Widget& w)
{
    int x, y, ww, hh;
    absRect(w, x, y, ww, hh);
    const bool lit = (w.flags & 0x40000) != 0 && w.activate, main = w.user == 1;
    const float S = ps_;
    for (int py = int(float(y) * S); py < int(float(y + hh) * S); ++py)
        for (int px = int(float(x) * S); px < int(float(x + ww) * S); ++px) {
            const float ux = float(px) / S - float(x), uy = float(py) / S - float(y);
            const float d = std::min({ux, uy, float(ww) - ux, float(hh) - uy});
            if (d < 1.0f)
                rBlend(px, py, main || lit ? tint(0xC8180E) : 0x8A827A, 0.9f);
            else
                rBlend(px, py, main ? tint(lit ? 0x8A0E08 : 0x5A0906) : lit ? tint(0x4A0806) : 0x0C0909, main ? 0.85f : 0.7f);
        }
    const float th = 12.0f;
    rText(w.text, float(x + ww / 2), float(y) + (float(hh) - th) / 2, th, kRowLit, 1, 0.0f, w.seed, kTx);
}

// the Cut-scenes page's ground: the left column and the file panel a little darker than the stage
void Frontend::drawReRoot(Widget&)
{
    if (screen_ != 19)
        return;
    const Cols c = cols(cw_, true);
    rCached("RE19," + std::to_string(cw_), 0.0f, 0.0f, float(cw_), float(kHeight), 0.0f, [&]() {
        const float S = ps_;
        for (int py = 0; py < rph(); ++py)
            for (int px = 0; px < rpw(); ++px) {
                const float u = float(px) / S, v = float(py) / S;
                float a = 0.0f;
                const float l = clamp01((float(c.lx + c.lw + 60) - u) / 110.0f), r = clamp01((u - float(c.rx - 60)) / 110.0f);
                a = 0.4f * std::max(l * l * (3 - 2 * l), r * r * (3 - 2 * r));
                const float b = clamp01((v - 400.0f) / 70.0f);
                a = std::max(a, 0.45f * b * b * (3 - 2 * b));
                if (a > 0.004f) rBlend(px, py, 0x000000, a);
            }
    });
}

// ---- the parts that cross-fade ----
void Frontend::reAnimate()
{
    const double now = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (reParts_.size() != 4)
        reParts_.assign(4, RemakeLayers::Part{});
    const std::string size = "|" + std::to_string(rpw()) + "x" + std::to_string(rph());
    ReAnim* anims[2] = {&reList_, &reFile_};
    for (int k = 0; k < 2; ++k) {
        ReAnim& a = *anims[k];
        RemakeLayers::Part& cur = reParts_[size_t(k * 2 + 1)];
        RemakeLayers::Part& old = reParts_[size_t(k * 2)];
        const std::string want = a.wantSig.empty() ? std::string() : a.wantSig + size;
        if (want != a.sig) {
            RemakeLayers::Part t;
            if (!want.empty() && a.draw) {
                t.x0 = std::max(0, int(float(a.box[0]) * ps_)), t.y0 = std::max(0, int(float(a.box[1]) * ps_));
                t.w = std::min(int(float(a.box[0] + a.box[2]) * ps_) + 1, rpw()) - t.x0;
                t.h = std::min(int(float(a.box[1] + a.box[3]) * ps_) + 1, rph()) - t.y0;
                if (t.w > 0 && t.h > 0) {
                    t.rgba.assign(size_t(t.w) * size_t(t.h) * 4, 0);
                    std::vector<uint8_t>* saveC = canvas_;
                    const int sx = rOffX_, sy = rOffY_, sw = rW_, sh = rH_;
                    canvas_ = &t.rgba, rOffX_ = t.x0, rOffY_ = t.y0, rW_ = t.w, rH_ = t.h;
                    a.draw();
                    canvas_ = saveC, rOffX_ = sx, rOffY_ = sy, rW_ = sw, rH_ = sh;
                } else
                    t.w = t.h = 0;
            }
            if (a.wantKey != a.key) {   // another entry / category: the one shown fades out, the new one in
                if (cur.w > 0 && !a.key.empty() && !want.empty()) {
                    old = std::move(cur);
                    a.hasOld = true;
                } else {
                    old = RemakeLayers::Part{};
                    a.hasOld = false;
                }
                a.t0 = a.key.empty() ? now - 1e6 : now;
            }
            cur = std::move(t);
            a.key = a.wantKey, a.sig = want;
            ++reGen_;
        }
        // the fade (0.26 s, eased): the new one in from the slide's side, the old one out the other way
        const float t = clamp01(float(now - a.t0) / 260.0f), e = t * t * (3.0f - 2.0f * t);
        cur.alpha = e, cur.dx = (1.0f - e) * float(a.slide) * 14.0f * ps_;
        if (a.hasOld) {
            old.alpha = 1.0f - e, old.dx = -e * float(a.slide) * 10.0f * ps_;
            if (t >= 1.0f) {
                a.hasOld = false;
                old = RemakeLayers::Part{};
                ++reGen_;
            }
        } else
            old.alpha = 0.0f;
    }
}

// ---- the common parts: the title and the categories, the controls ----
void Frontend::reColumn(const GalleryText& g, int selected)
{
    const Cols c = cols(cw_, true);
    Widget* title = rWidget(c.lx, 22, c.lw, 22, &Frontend::drawRText, "Gallery");
    title->user = int(kRowLit), title->id = 0, title->seed = 11;
    rWidget(c.lx, 50, c.lw, 2, &Frontend::drawReRule);
    static const char* const kHelp[] = {
        "The two agents and their second looks. Key: 1",
        "Sophie, Dr. Curien and the researchers to rescue. Key: 2",
        "Every creature the game sends at you, with the official names. Key: 3",
        "The four chapter bosses. Key: 4",
        "The golden frog, the items hidden in props, and props worth a look. Key: 5",
        "Creatures and models in the arcade data that the game never uses. Key: 6",
    };
    std::vector<std::string> cats = g.categories;
    if (cats.empty())
        cats = {"Players", "Humans", "Creatures", "Bosses", "Items", "Cut Content"};
    const int n = int(cats.size());
    for (int k = 0; k <= n; ++k) {
        const bool scenes = k == n;
        Widget* w = rWidget(c.lx - 4, 60 + k * 22, c.lw + 4, 20, &Frontend::drawReCat, scenes ? "Cut-scenes" : cats[size_t(k)]);
        w->user = (k == selected ? 1 : 0) | (scenes && !onScenes ? 2 : 0);
        w->seed = 31 + k * 3;
        if (scenes) {
            w->help = "The players' cut-scenes, played by the game itself: 1-4 players, arcade or PC models. Key: C";
            if (onScenes && selected != n)
                setHandlers(w, [this](Widget&) {
                    walking_ = false;
                    if (onGallery) {   // (the Gallery's entry kept, the model freed: as the C key)
                        const auto f = onGallery;
                        f(17);
                    }
                    screenScenes();
                }, &Frontend::hitRect);
        } else {
            w->help = k < int(sizeof kHelp / sizeof kHelp[0]) ? kHelp[k] : "";
            setHandlers(w, [this, k](Widget&) {
                walking_ = false;
                if (screen_ == 19 && openGallery) {   // from the Cut-scenes page: the Gallery at that category
                    const auto og = openGallery;
                    og();
                }
                if (onGallery) {
                    const auto f = onGallery;
                    f(100 + k);
                }
                screenGallery();
            }, &Frontend::hitRect);
        }
    }
}

void Frontend::reLegend(const std::vector<RePrompt>& all, int left, int right)
{
    std::vector<RePrompt> items;   // (a key with no pad button ("-"): left out while a pad is used)
    for (const RePrompt& p : all)
        if (!padPrompts_ || p.key.substr(p.key.find('|') + 1) != "-")
            items.push_back(p);
    // one row from the right edge (wrapped onto a row above when it would pass `left`)
    const float kh = 15.0f, kt = 7.0f, lh = 10.0f, gap = 14.0f;
    std::vector<int> widths;
    for (const RePrompt& p : items) {
        const size_t bar = p.key.find('|');
        const std::string key = padPrompts_ ? padKey(p.key.substr(bar + 1)) : p.key.substr(0, bar);
        const bool ring = ringKey(key);
        const size_t nIcons = padIconsOf(key).size();   // (padglyphs)
        const float kw = nIcons ? kh * float(nIcons) : ring ? kh : std::max(kh + 4.0f, rTextWidth(key, kt) + 10.0f);
        widths.push_back(int(std::ceil(kw + 6.0f + rTextWidth(p.label, lh))) + 2);
    }
    // one row at the bottom right; too wide: two rows (the first items on the upper one), split where they are most even
    const int n = int(items.size());
    int total = 0;
    for (int k = 0; k < n; ++k) total += widths[size_t(k)] + (k ? int(gap) : 0);
    int split = 0;   // (the items before it: the upper row)
    if (total > right - left) {
        int best = 1 << 30, acc = 0;
        for (int s = 1; s < n; ++s) {
            acc += widths[size_t(s - 1)] + (s > 1 ? int(gap) : 0);
            const int worst = std::max(acc, total - acc - int(gap));
            if (worst < best) best = worst, split = s;
        }
    }
    int rowY = kHeight - 30, x = right;
    for (int k = n - 1; k >= 0; --k) {
        const int w = widths[size_t(k)];
        if (k == split - 1) {
            rowY -= 22;
            x = right;
        }
        Widget* p = rWidget(x - w, rowY, w, 18, &Frontend::drawRePrompt, items[size_t(k)].label);
        p->value = items[size_t(k)].key;
        p->seed = 950 + k * 3;
        p->tag = items[size_t(k)].tag;
        if (items[size_t(k)].act)
            setHandlers(p, [this, a = items[size_t(k)].act](Widget&) {
                walking_ = false;
                a();
            }, &Frontend::hitRect);
        x -= w + int(gap);
    }
}

// ---- the Gallery (screen 17) ----
void Frontend::remakeGallery(const GalleryText& g)
{
    remakePage(17, "gallery", true);
    rebuild_ = [this]() { screenGallery(); };
    galleryRe_ = true;
    root_->draw = &Frontend::drawReRoot;
    const bool full = g.full, info = g.info && !full;
    const Cols c = cols(cw_, info);
    auto act = [this](int a) {
        return [this, a]() {
            if (onGallery) {
                const auto f = onGallery;
                f(a);
            }
            if (a == 6)
                screenMainMenu();
            else if (screen_ == 17)
                screenGallery();
        };
    };
    // the stage: the model framed between the columns, a little above the controls (the full view: the window)
    if (full) {
        reFrame_[0] = 0, reFrame_[1] = 0, reFrame_[2] = cw_, reFrame_[3] = kHeight;
        rePanels_[0] = 0, rePanels_[1] = cw_, rePanels_[2] = kHeight - 40;
    } else {
        reFrame_[0] = c.stageX, reFrame_[1] = 26, reFrame_[2] = c.stageW, reFrame_[3] = 400;
        rePanels_[0] = c.lx + c.lw + 6, rePanels_[1] = info ? c.rx - 10 : cw_, rePanels_[2] = kHeight - 44;
    }
    rHelp_[0] = reFrame_[0] + reFrame_[2] / 2, rHelp_[1] = 8, rHelp_[2] = std::max(200, reFrame_[2] - 60);   // (polish: clear of the columns)
    // the controls along the bottom
    std::vector<RePrompt> legend = {
        {"Drag|RS", "Rotate", nullptr},
        {"Wheel|LT/RT", "Zoom", nullptr},
        {"R-Drag|LS", "Pan", nullptr},
        {"L|-", "Light", act(14)},
        {"Space|X", g.playing ? "Pause" : "Play", act(4)},
        {"M|Y", "Next Motion", act(3)},
        {"T|Start", "Hide UI", act(11)},
        {"B|View", "Background", act(7)},
        {"H|-", "Blood / Damage", act(34)},   // (gallery blood: the panels under the file)
        {"Esc|B", "Back", act(6), 1},
    };
    if (full) {   // (the full view: T / Escape show the page again)
        legend.erase(legend.begin() + 6);
        legend.back() = {"T/Esc|Start/B", "Show UI", act(11)};
    }
    if (full) {   // the full view: the name small at the bottom left, the controls (the host fades them out)
        reFile_.wantKey = reFile_.wantSig = "";
        reList_.wantKey = reList_.wantSig = "";
        Widget* nm = rWidget(c.m, kHeight - 54, 300, 16, &Frontend::drawRText, g.name);
        nm->user = int(kRowLit), nm->id = 0, nm->seed = 13;
        Widget* gr = rWidget(c.m, kHeight - 32, 300, 9, &Frontend::drawRText,
                             g.group + "   " + std::to_string(g.index + 1) + " / " + std::to_string(g.count));
        gr->user = int(kDim), gr->id = 0, gr->seed = 14;
        reLegend(legend, c.m + 310, cw_ - c.m);
        if (!g.status.empty()) {
            Widget* st = rWidget(0, kHeight / 2 - 6, cw_, 12, &Frontend::drawRText, g.status);
            st->user = int(kRowText), st->id = 1;
        }
        return;
    }
    reColumn(g, g.category);
    // the list: the category's entries, the shown one marked (kept in sight)
    const int n = int(g.entries.size()), listY = 250;
    // menus3 (user 2026-10-28: "the scroll does not work in the gallery humans section"): the list scrolls by itself
    // (the wheel over it, the bar dragged: reListScroll_); a new entry / category: kept round the shown one again
    if (g.category * 100000 + g.index != reListSeen_) {
        reListSeen_ = g.category * 100000 + g.index;
        reListScroll_ = -1;
    }
    reListN_ = n;
    const int centred = std::clamp(g.index - kListRows / 2 + 1, 0, std::max(0, n - kListRows));
    const int first = reListScroll_ >= 0 ? std::clamp(reListScroll_, 0, std::max(0, n - kListRows)) : centred;
    {
        Widget* hd = rWidget(c.lx, 228, c.lw, 10, &Frontend::drawRText, g.group);
        hd->user = int(tint(0xC8241A)), hd->id = 0, hd->seed = 41;
        Widget* cnt = rWidget(c.lx, 228, c.lw, 10, &Frontend::drawRText, std::to_string(g.index + 1) + " / " + std::to_string(g.count));
        cnt->user = int(kDim), cnt->id = 2, cnt->seed = 42;
        rWidget(c.lx, 243, c.lw, 2, &Frontend::drawReRule);
    }
    for (int r = 0; r < kListRows && first + r < n; ++r) {
        const int k = first + r;
        const std::string name = size_t(k) < g.entryNames.size() ? g.entryNames[size_t(k)] : std::string();
        Widget* w = rWidget(c.lx - 4, listY + r * kRowStep, c.lw + 4, kRowStep - 1, &Frontend::drawReRow, name);
        w->seed = 300 + r;
        w->help = k == g.index ? "Shown now" : "Show this one (Q / E: the one before / after)";
        if (k != g.index && size_t(k) < g.entries.size())
            setHandlers(w, [act, e = g.entries[size_t(k)]](Widget&) { act(1000 + e)(); }, &Frontend::hitRect);
    }
    reList_.wantKey = "c" + std::to_string(g.category);
    reList_.slide = -1;
    reList_.box[0] = c.lx - 4, reList_.box[1] = listY - 6, reList_.box[2] = c.lw + 8, reList_.box[3] = kListRows * kRowStep + 16;
    {
        std::string sig = reList_.wantKey + "|" + std::to_string(g.index) + "|" + std::to_string(first) + "|" + std::to_string(bloodHue_);
        for (const std::string& s : g.entryNames) sig += "|" + s;
        reList_.wantSig = sig;
    }
    reList_.draw = [this, names = g.entryNames, cur = g.index, first, n, lx = c.lx, lw = c.lw]() {
        const float S = ps_;
        for (int r = 0; r < kListRows && first + r < n; ++r) {
            const int k = first + r, y = listY + r * kRowStep, h = kRowStep - 1;
            if (k == cur)
                for (int py = int(float(y) * S); py < int(float(y + h) * S); ++py)
                    for (int px = int(float(lx - 4) * S); px < int(float(lx + lw) * S); ++px) {
                        const float t = (float(px) / S - float(lx - 4)) / float(lw + 4);
                        rBlend(px, py, tint(0x7A0A08), 0.5f * (1.0f - t) * (1.0f - t));
                        if (float(px) / S < float(lx - 4) + 2.5f) rBlend(px, py, tint(0xD0180E), 0.95f);
                    }
            std::string s = size_t(k) < names.size() ? names[size_t(k)] : std::string();
            float th = 11.0f;   // (a long name: smaller, then cut)
            if (const float tw = rTextWidth(s, th); tw > float(lw - 14)) th = std::max(9.0f, th * float(lw - 14) / tw);
            s = util::utf8Fit(s, "..", [&](const std::string& t) { return rTextWidth(t, th) <= float(lw - 14); });
            rText(s, float(lx + 6), float(y) + (float(h) - th) / 2, th, k == cur ? kRowLit : kDim, 0, 0.0f, 300 + r, kTx);
        }
        auto chevron = [&](float cy, float s) { rChevron(float(lx + lw / 2), cy, s, kDim, 0.9f); };
        if (first > 0) chevron(float(listY) - 3.0f, -4.0f);
        if (first + kListRows < n) chevron(float(listY + kListRows * kRowStep) + 3.0f, 4.0f);
    };
    // galfix3 (user 2026-10-28: "the scroll bar for the gallery is not the same as the main menu scroll bar"): the
    // list's bar is the settings lists' / dropdowns' own (Frontend::scrollBar: the same groove, thumb, size, drag and
    // click; it replaces galfix2's thin drawn line and its own drag code)
    if (n > kListRows)
        scrollBar(root_, c.lx + c.lw - 6, listY, 7, kListRows * kRowStep - 1, first, kListRows, n, [this](int f) {
            reListScroll_ = f;
            screenGallery();
        });
    // the controls (the list's keys first)
    legend.insert(legend.begin(), {{"Q/E|LB/RB", "Entry", nullptr}, {"Tab|L3", "Category", nullptr}});
    reLegend(legend, c.lx, cw_ - c.m);
    if (!g.status.empty()) {   // (a model that cannot be drawn: why, in the stage's middle)
        Widget* st = rWidget(reFrame_[0], reFrame_[1] + reFrame_[3] / 2 - 6, reFrame_[2], 12, &Frontend::drawRText, g.status);
        st->user = int(kRowText), st->id = 1, st->seed = 51;
    }
    if (!info) {   // (the file hidden: I brings it back)
        reFile_.wantKey = reFile_.wantSig = "";
        const int hw = int(std::ceil(25.0f + rTextWidth("Show the file", 10.0f))) + 2;   // (a key cap, as the controls)
        Widget* hint = rWidget(cw_ - c.m - hw, 26, hw, 18, &Frontend::drawRePrompt, "Show the file");
        hint->value = "I|-", hint->seed = 52;
        setHandlers(hint, [act](Widget&) { act(12)(); }, &Frontend::hitRect);
        return;
    }
    // the viewer's settings under the file (rows: a click steps them)
    // galfix2 (user: "Add < > so players know they can change the motion, model, light, background, layout"): each
    // row's value between arrows; a click on "<" (or Left) steps back, on the rest of the row (or Right) forward
    struct Opt { std::string label, value, help; int action, back; bool on; };
    std::vector<Opt> opts;
    // gallery blood (user 2026-11-03): two more panels under the file, H (or a click on their names) between them: Blood
    // (the Gallery's own Blood Color, its custom colour, Rainbow Blood and its settings, Blood Particles: the model
    // shown in them at once; Options and Mods keep theirs) and Damage (the game's damage and part-loss models per body
    // part)
    const int panel = std::clamp(g.panel, 0, 2);
    if (panel == 1) {
        static const char* const kColours[6] = {"Red", "Green", "Blue", "Purple", "Yellow", "Custom"};
        const char* const keep = "The Gallery's own: Options and Mods keep theirs.";
        auto channel = [&](const char* name, int shift, int up) {
            return Opt{name, std::to_string((g.bloodRgb >> shift) & 255),
                       std::string("The custom colour's ") + (shift == 16 ? "red" : shift == 8 ? "green" : "blue") +
                           ", by 16 (picks Custom). Click < or >",
                       up, up + 1, true};
        };
        opts = {
            {"Blood Color", kColours[std::clamp(g.bloodColor, 0, 5)],
             std::string("The blood's colour on the model, its wounds and its blood. Key: G (Shift+G back). ") + keep, 40, 41, true},
            channel("Custom Red", 16, 42),
            channel("Custom Green", 8, 44),
            channel("Custom Blue", 0, 46),
            {"Rainbow Blood", g.rainbow ? "On" : "Off", std::string("The Rainbow Blood mod's colours. Key: N. ") + keep, 48, 48, true},
            {"Rainbow Colours", g.rainbowColours, "Rainbow Blood: all blood one colour at a time, or each splash its own", 49, 49, g.rainbow},
            {"Rainbow Speed", g.rainbowSpeed, "Rainbow Blood: how fast the colours change", 50, 51, g.rainbow},
            {"Creature Blood", g.creatureBlood, "Rainbow Blood: the creature's own wounds (its own colour, cycling, or as painted)", 52, 53, g.rainbow},
            {"Blood Particles", g.particles ? "On" : "Off",
             std::string("The blood burst of a new damage step, the drips from a cut part. Key: J. ") + keep, 54, 55, true},
        };
    } else if (panel == 2) {
        bool any = false;
        for (size_t p = 0; p < g.partNames.size() && p < g.partStates.size() && p < g.partSteps.size(); ++p) {
            const bool on = g.partSteps[p] > 0;
            any = any || on;
            opts.push_back({g.partNames[p], g.partStates[p],
                            on ? "The game's own damage models for this part, in the order shots bring them (Off: shot away). "
                                 "Keys: Z the part, X its next state (Shift+X back). Click < or >"
                               : "The game has no damage models for this part of it",
                            60 + int(p), 70 + int(p), on});
        }
        opts.push_back({"All Parts", "Whole", "Every part whole again. Key: Shift+Z", 59, 59, any});
    } else {
        opts = {
            {"Motion", g.motion.empty() ? std::string("-") : g.motion.substr(0, g.motion.find("  (")),
             "Its next motion (or damage step). Key: M (Shift+M back), pad: Y. Click < or > (or Left / Right)", 3, 30, !g.motion.empty()},
            {"Model", g.source, g.otherSource ? "The arcade's or the PC release's models. Key: S. Click < or >" : "Only one model source is there", 2, 2, g.otherSource},
            {"Light", g.light, "The light: the game's, or from the side (8 ways round). Key: L. Click < or > (or Left / Right)", 14, 31, true},
            {"Background", g.background, "The stage: dark, the game's skies, plain colours, rooms. Key: B, pad: View. Click < or >", 7, 32, true},
            {"Layout", "Remake", "The Gallery's other layouts: the portraits, the Classic page. Key: K. Click < or >", 15, 33, true},
        };
    }
    const int optH = 15, optY = 430 - int(opts.size()) * optH - 34;
    {
        // the panels' names (the shown one lit; a click shows it, H the next)
        static const char* const kPanels[3] = {"Viewer", "Blood", "Damage"};
        // polish (user 2026-11-03: the tabs were small words crowded on the rule, the hand pointing at one covered it):
        // each a little larger with room round it, its hit area a box from 6 above the word to the rows (the hand can
        // point at the box's left or top and leave the word in sight), the current one underlined
        float x = float(c.rx);
        for (int p = 0; p < 3; ++p) {
            const float w = rTextWidth(kPanels[p], 10.0f) + 22.0f;
            Widget* vh = rWidget(int(x), optY - 18, int(w), 10, &Frontend::drawRText, kPanels[p]);
            vh->user = int(p == panel ? tint(0xC8241A) : kDim), vh->id = 0, vh->seed = 59 + p;
            vh->help = "The settings under the file: the viewer's, the blood's (the Gallery's own), the body parts' damage. Key: H";
            Widget* hit = rWidget(int(x) - 6, optY - 24, int(w), 22, nullptr, "");   // (the larger hit area)
            hit->help = vh->help;
            setHandlers(hit, [act, p](Widget&) { act(36 + p)(); }, &Frontend::hitRect);
            if (p == panel) {   // the current tab's underline
                Widget* ul = rWidget(int(x), optY - 6, int(w - 22.0f), 1, &Frontend::drawReRule);
                (void)ul;
            }
            x += w;
        }
    }
    if (panel == 2 && !g.damageNote.empty()) {   // (a creature without damage models, a still model: why)
        Widget* dn = rWidget(c.rx, optY + int(opts.size()) * optH + 2, c.rw, 8, &Frontend::drawRText, g.damageNote);
        dn->user = int(kDim), dn->id = 0, dn->seed = 58;
    }
    for (size_t i = 0; i < opts.size(); ++i) {
        Widget* o = rWidget(c.rx, optY + int(i) * optH, c.rw, optH - 1, &Frontend::drawReOpt, opts[i].label);
        o->value = opts[i].value;
        o->help = opts[i].help;
        o->seed = 600 + int(i) * 3;
        o->user = opts[i].on ? kOptArrows : 1;
        if (opts[i].on) {
            setHandlers(o, [this, act, a = opts[i].action, b = opts[i].back](Widget& w) {
                // (the pointer on the "<": back; Enter / a pad's A with the pointer elsewhere: forward)
                act(float(px_) < reOptLeftArrow(w) + kOptArrowW * 0.6f && float(px_) > reOptLeftArrow(w) - kOptArrowW ? b : a)();
            }, &Frontend::hitRect);
            o->left = [act, b = opts[i].back]() { act(b)(); };
            o->right = [act, a = opts[i].action]() { act(a)(); };
        }
    }
    // the file: the name, the rule, the category and chapters, the description, the stats, the weak-point card
    std::shared_ptr<std::vector<uint8_t>> card;
    int cardW = 0, cardH = 0;
    if (galleryCard)
        if (const std::vector<uint8_t>* cv = galleryCard(); cv && cv->size() >= 4) {
            cardW = (*cv)[0] | ((*cv)[1] << 8), cardH = (*cv)[2] | ((*cv)[3] << 8);
            if (cardW > 0 && cardH > 0 && cv->size() >= 4 + size_t(cardW) * size_t(cardH) * 4)
                card = std::make_shared<std::vector<uint8_t>>(cv->begin() + 4, cv->begin() + 4 + size_t(cardW) * size_t(cardH) * 4);
        }
    reFile_.wantKey = "e" + g.key + g.name;
    reFile_.slide = 1;
    reFile_.box[0] = c.rx - 8, reFile_.box[1] = 20, reFile_.box[2] = c.rw + 16, reFile_.box[3] = optY - 40;
    reFile_.wantSig = reFile_.wantKey + "|" + g.text + "|" + g.facts + "|" + g.where + "|" + g.group + std::to_string(g.index) + "/" +
                      std::to_string(g.count) + "|" + (card ? "card" : "") + std::to_string(bloodHue_) +
                      "|b" + std::to_string(optY);   // (polish: the room it was laid out for: another panel's rows, another layout)
    const int bottom = optY - 26;   // (above "Viewer"; polish: its tabs' hit boxes)
    reFile_.draw = [this, g, card, cardW, cardH, rx = c.rx, rw = c.rw, bottom]() {
        const float W = float(rw), X = float(rx);
        // (lang: the text translated, then cut by the language's widths (English: as before); its lines drawn raw)
        auto wrap = [&](const std::string& sIn, float h, float width) {
            const std::string s = T(sIn);
            const RawText raw(this);
            if (lang::active())
                return lang::wrapBy(s, width, [&](const std::string& t) { return rTextWidthRaw(t, h); });
            std::vector<std::string> out;
            std::string line;
            size_t p = 0;
            while (p < s.size()) {
                size_t e = s.find(' ', p);
                const std::string word = s.substr(p, e == std::string::npos ? std::string::npos : e - p);
                p = e == std::string::npos ? s.size() : e + 1;
                if (word.empty()) continue;
                const std::string t = line.empty() ? word : line + " " + word;
                if (!line.empty() && rTextWidth(t, h) > width) {
                    out.push_back(line);
                    line = word;
                } else
                    line = t;
            }
            if (!line.empty()) out.push_back(line);
            return lang::noteLines(out);
        };
        // the name: large caps (a part in brackets on a smaller line under it; too long: smaller, then two lines)
        float y = 34.0f;
        std::string name = g.name, aside;
        if (const size_t b = name.find(" ("); b != std::string::npos && rTextWidth(name, 28.0f) > W)
            aside = name.substr(b + 1), name = name.substr(0, b);
        float nh = 28.0f;
        // (galfix2: one line down to 16 high before two lines, with a little slack (the widths are not quite linear in
        // the height: a name sized to the width exactly came out a hair wider and went onto two lines, "The Hangedman"
        // among them, which left the weak-point card and the description no room)
        if (const float tw = rTextWidth(name, nh); tw > W) {
            const float fit = nh * W / tw * 0.96f;
            nh = fit >= 16.0f ? fit : std::max(19.0f, fit);
        }
        std::vector<std::string> nameLines = rTextWidth(name, nh) > W ? wrap(name, nh, W) : std::vector<std::string>{T(name)};
        for (const std::string& l : nameLines) {
            const RawText raw(this);
            rText(l, X, y, nh, kRowLit, 0, 0.0f, 71, kTx);
            y += nh + 6.0f;
        }
        if (!aside.empty()) {
            rText(aside, X, y - 2.0f, 12.0f, kRowText, 0, 0.0f, 72, kTx);
            y += 16.0f;
        }
        // the rule
        {
            const float S = ps_;
            const uint32_t col = tint(0xB8140C);
            for (int py = int((y + 2.0f) * S); py < int((y + 3.2f) * S); ++py)
                for (int px = int(X * S); px < int((X + W) * S); ++px) {
                    const float t = (float(px) / S - X) / W;
                    rBlend(px, py, col, 0.9f * (1.0f - t) * (1.0f - t) + 0.08f);
                }
            y += 10.0f;
        }
        // the category (red) and the place in it
        rText(g.group, X, y, 10.0f, tint(0xD0281C), 0, 0.0f, 73, kTx);
        rText(std::to_string(g.index + 1) + " / " + std::to_string(g.count), X + W, y, 10.0f, kDim, 2, 0.0f, 74, kTx);
        y += 22.0f;
        // the stats (labelled rows) and the notes (the facts' other sentences, after the description)
        struct Stat { std::string label, value; };
        std::vector<Stat> stats;
        std::vector<std::string> notes;
        if (!g.where.empty()) stats.push_back({"Appears", g.where});
        for (const std::string& s : sentences(g.facts)) {
            if (s.rfind("Type ", 0) == 0) stats.push_back({"Type", s.substr(5)});
            else if (s.rfind("Hit points ", 0) == 0) stats.push_back({"Hit points", s.substr(11)});
            else if (s.rfind("Developer name ", 0) == 0 && s.find('(') == std::string::npos) stats.push_back({"Developer name", s.substr(15)});
            else if (s.rfind("Developer name ", 0) == 0) notes.push_back("A developers' name: no official one is known");
            else if (s.rfind("BO_", 0) == 0) stats.push_back({"Developer name", s});
            else if (s.rfind("U: ", 0) == 0) notes.push_back("Key U: " + s.substr(3));
            else if (const size_t colon = s.find(": "); colon != std::string::npos && colon < 24)
                stats.push_back({s.substr(0, colon), s.substr(colon + 2)});
            else notes.push_back(s);
        }
        // what fits: the description and notes, the stats, the card (smaller when the room is short)
        const float bh = 11.0f, step = 16.5f;
        std::vector<std::string> body = wrap(g.text, bh, W);
        std::vector<std::string> noteLines;
        for (const std::string& n : notes)
            for (const std::string& l : wrap(n + ".", 9.5f, W)) noteLines.push_back(l);
        // (beside a card: two lines a stat; polish: one line each when the room is short and the values fit beside their
        // labels, so a short panel (the Blood one's nine rows) keeps the description whole: the Hermit's was cut)
        bool tall = card != nullptr;
        float statH = float(stats.size()) * (tall ? 29.0f : 17.0f);
        float cardDH = 0, cardDW = 0, cardMinK = 0.7f;
        if (card) {
            // galact (user 2026-10-28, again: "Hangedman weak-point card bigger"): beside the stats (right), up to 58 % of
            // the panel wide and 130 high, 3 times the picture at most (it was under them at 1.6 times: the viewer rows
            // below left it no more room); the room left decides, down to the old size (its 70 %)
            const float s = std::min({W * 0.58f / float(cardW), 130.0f / float(cardH), 3.0f});
            const float old = std::min({W / float(cardW), 92.0f / float(cardH), 1.6f});
            cardDW = float(cardW) * s, cardDH = float(cardH) * s;
            cardMinK = std::min(1.0f, 0.5f * old / s);   // (polish: down to half the old size before the description gives way)
        }
        auto block = [&]() { return card ? std::max(statH, 22.0f + cardDH) : statH; };   // (the stats and the card side by side)
        float need = float(body.size()) * step + (noteLines.empty() ? 0.0f : 6.0f + float(noteLines.size()) * 13.0f) + 12.0f + block();
        const float room = float(bottom) - y;
        if (need > room && card) {
            bool fits = true;
            const float sw = W - cardDW * cardMinK - 12.0f;
            for (const Stat& s : stats)
                fits = fits && rTextWidth(s.label, 8.5f) + 14.0f + rTextWidth(s.value, 10.0f) <= sw;
            if (fits) tall = false, statH = float(stats.size()) * 17.0f;
        }
        need = float(body.size()) * step + (noteLines.empty() ? 0.0f : 6.0f + float(noteLines.size()) * 13.0f) + 12.0f + block();
        if (need > room && card) {   // the card smaller first
            const float k = std::clamp((cardDH - (need - room)) / cardDH, cardMinK, 1.0f);   // (galfix2: not below 70 %: the description gives way)
            cardDW *= k, cardDH *= k;
        }
        while (!body.empty() && y + float(body.size()) * step + 12.0f + block() > float(bottom)) {
            body.pop_back();   // (then the description's last lines)
            if (!body.empty()) body.back() += " ..";
        }
        for (const std::string& l : body) {
            const RawText raw(this);
            rText(l, X, y, bh, kBody, 0, 0.0f, 75, kTx);
            y += step;
        }
        if (!noteLines.empty() && y + 6.0f + float(noteLines.size()) * 13.0f + 12.0f + block() <= float(bottom)) {
            y += 6.0f;
            for (const std::string& l : noteLines) {
                const RawText raw(this);
                rText(l, X, y, 9.5f, kDim, 0, 0.0f, 76, kTx);
                y += 13.0f;
            }
        }
        y += 12.0f;
        const float S = ps_;
        const float top = y;
        const float SW = card ? W - cardDW - 12.0f : W;   // (the stats' width: the card on their right)
        for (const Stat& s : stats) {
            rText(s.label, X, y + 1.0f, 8.5f, kDim, 0, 0.0f, 77, kTx);
            // (galact: translated before it is cut to fit: a cut value matched no key and stayed English)
            std::string v = T(s.value);
            // (beside a card: the value on its own line under the label, so the narrower column keeps it whole)
            const float vroom = tall ? SW : SW - rTextWidth(s.label, 8.5f) - 14.0f;
            const RawText raw(this);
            v = util::utf8Fit(v, "..", [&](const std::string& t) { return rTextWidthRaw(t, 10.0f) <= vroom; });
            if (tall)
                rText(v, X, y + 12.0f, 10.0f, kRowLit, 0, 0.0f, 78, kTx);
            else
                rText(v, X + SW, y, 10.0f, kRowLit, 2, 0.0f, 78, kTx);
            const float ry = tall ? y + 26.0f : y + 14.0f;
            for (int px = int(X * S); px < int((X + SW) * S); ++px)
                for (int py = int(ry * S); py < int(ry * S) + 1; ++py)
                    rBlend(px, py, 0xFFFFFF, 0.1f);
            y += tall ? 29.0f : 17.0f;
        }
        if (card) {
            if (top + 14.0f + cardDH > float(bottom) && cardDH > 0.0f) {   // (polish: never over the rows under the file)
                const float k = std::max(0.0f, float(bottom) - top - 14.0f) / cardDH;
                cardDW *= k, cardDH *= k;
            }
            const float cx = X + W - cardDW;
            float cy = top;
            rText("Weak point", cx, cy, 8.5f, kDim, 0, 0.0f, 79, kTx);
            cy += 14.0f;
            for (int py = int((cy - 1.0f) * S); py < int((cy + cardDH + 1.0f) * S); ++py)   // a thin frame round it
                for (int px = int((cx - 1.0f) * S); px < int((cx + cardDW + 1.0f) * S); ++px)
                    rBlend(px, py, 0x000000, 0.6f);
            rImage(*card, cardW, cardH, cx, cy, cardDW, cardDH, 1.0f);
        }
    };
}

// ---- the Cut-scenes page (screen 19) ----
void Frontend::remakeScenes(const ScenesText& s, const GalleryText& g)
{
    remakePage(19, "scenes", true);
    reListHide_[0] = reListHide_[1] = reListHide_[2] = reListHide_[3] = 0;   // (galact: set again while the Actors list is open)
    rebuild_ = [this]() { screenScenes(); };
    galleryRe_ = true;
    root_->draw = &Frontend::drawReRoot;
    const Cols c = cols(cw_, true);
    auto act = [this](int a) {
        return [this, a]() {
            if (onScenes) {
                const auto f = onScenes;
                f(a);
            }
            if (a == 3)
                screenMainMenu();
            else if (screen_ == 19)
                screenScenes();
        };
    };
    reColumn(g, int(g.categories.empty() ? 6 : g.categories.size()));
    rHelp_[0] = (c.stageX + c.rx) / 2, rHelp_[1] = 8, rHelp_[2] = std::max(200, c.rx - c.stageX - 60);   // (polish)
    reFrame_[0] = c.stageX, reFrame_[1] = 26, reFrame_[2] = c.stageW, reFrame_[3] = 400;
    rePanels_[0] = c.lx + c.lw + 6, rePanels_[1] = c.rx - 10, rePanels_[2] = kHeight - 44;
    // the scenes in the middle, under their chapters' headings
    const int n = int(s.rows.size());
    const int mx = c.stageX + 16, mw = std::min(320, c.stageW - 32);
    struct Line { int scene; std::string text; int y; };
    std::vector<Line> lines;   // (y set below, from the first shown line)
    {
        int chapter = 0;
        for (int i = 0; i < n; ++i) {
            const std::string& r = s.rows[size_t(i)];
            const int ch = r.empty() ? 0 : r[0] - '0';
            if (ch != chapter) {
                lines.push_back({-1, "Chapter " + std::to_string(ch), 0});
                chapter = ch;
            }
            lines.push_back({i, r, 0});
        }
    }
    // a scroll bar when the lines do not fit (the other lists' bar and wheel): lines from `first` down, to y 430
    const int total = int(lines.size());
    auto layout = [&](int first, bool set) {   // the lines that fit from `first` (the heading's gap before a chapter)
        int y = 40, cnt = 0;
        for (int i = first; i < total; ++i) {
            if (i > first && lines[size_t(i)].scene < 0) y += 6;
            if (y + 17 > 430) break;
            if (set) lines[size_t(i)].y = y;
            y += 18, ++cnt;
        }
        return cnt;
    };
    int top = 0;
    while (top < total - 1 && layout(top, false) < total - top)
        ++top;
    int selLine = 0;
    for (int i = 0; i < total; ++i)
        if (lines[size_t(i)].scene == s.selected) selLine = i;
    if (s.selected != reSceneSel_) {   // another scene chosen: it is shown (the list scrolled by the user stays otherwise)
        reSceneSel_ = s.selected;
        if (selLine < reSceneFirst_ || selLine >= reSceneFirst_ + layout(reSceneFirst_, false))
            reSceneFirst_ = selLine >= 1 && lines[size_t(selLine - 1)].scene < 0 ? selLine - 1 : selLine;
    }
    reSceneTop_ = top;
    reSceneFirst_ = std::clamp(reSceneFirst_, 0, top);
    const int shown = layout(reSceneFirst_, true);
    {
        std::vector<Line> vis(lines.begin() + reSceneFirst_, lines.begin() + reSceneFirst_ + shown);
        lines.swap(vis);
    }
    if (top > 0)
        scrollBar(root_, mx + mw + 10, 40, 7, 390, reSceneFirst_, total - top, total, [this](int f) {
            reSceneFirst_ = f;
            screenScenes();
        });
    for (const Line& l : lines)
        if (l.scene >= 0) {
            const size_t sp = l.text.find(' ');
            Widget* w = rWidget(mx - 4, l.y, mw + 4, 17, &Frontend::drawReRow, sp == std::string::npos ? l.text : l.text.substr(sp + 1));
            w->seed = 400 + l.scene;
            w->help = l.scene == s.selected ? "The chosen scene: Play shows it" : "Choose this scene (Q / E: the one before / after)";
            if (l.scene != s.selected)
                setHandlers(w, [act, i = l.scene](Widget&) { act(100 + i)(); }, &Frontend::hitRect);
        }
    reList_.wantKey = "scenes";
    reList_.slide = -1;
    reList_.box[0] = mx - 8, reList_.box[1] = 30, reList_.box[2] = mw + 12, reList_.box[3] = 400;
    reList_.wantSig = "scenes|" + std::to_string(s.selected) + "|" + std::to_string(reSceneFirst_) + "|" + std::to_string(n) + "|" + std::to_string(bloodHue_) + "|" + std::to_string(cw_);
    reList_.draw = [this, lines, sel = s.selected, mx, mw]() {
        const int* hide = reListHide_;   // (galact: the rows under the open Actors list)
        const float S = ps_;
        for (const Line& l : lines) {
            if (hide[2] > 0 && l.y + 17 > hide[1] && l.y < hide[1] + hide[3])
                continue;
            if (l.scene < 0) {   // a chapter's heading: small red caps, a rule
                rText(l.text, float(mx + 6), float(l.y) + 4.0f, 9.0f, tint(0xD0281C), 0, 0.0f, 90, kTx);
                for (int py = int((float(l.y) + 16.0f) * S); py < int((float(l.y) + 17.0f) * S); ++py)
                    for (int px = int(float(mx + 6) * S); px < int(float(mx + mw) * S); ++px)
                        rBlend(px, py, 0xFFFFFF, 0.1f * (1.0f - (float(px) / S - float(mx + 6)) / float(mw)));
                continue;
            }
            const int y = l.y, h = 17;
            const bool cur = l.scene == sel;
            if (cur)
                for (int py = int(float(y) * S); py < int(float(y + h) * S); ++py)
                    for (int px = int(float(mx - 4) * S); px < int(float(mx + mw) * S); ++px) {
                        const float t = (float(px) / S - float(mx - 4)) / float(mw + 4);
                        rBlend(px, py, tint(0x7A0A08), 0.5f * (1.0f - t) * (1.0f - t));
                        if (float(px) / S < float(mx - 4) + 2.5f) rBlend(px, py, tint(0xD0180E), 0.95f);
                    }
            const size_t sp = l.text.find(' ');
            const std::string num = l.text.substr(0, sp), name = sp == std::string::npos ? std::string() : l.text.substr(sp + 1);
            rText(name, float(mx + 6), float(y) + 3.0f, 11.0f, cur ? kRowLit : kDim, 0, 0.0f, 400 + l.scene, kTx);
            rText(num, float(mx + mw - 4), float(y) + 4.0f, 9.0f, cur ? kRowText : kRowOff, 2, 0.0f, 500 + l.scene, kTx);
        }
    };
    // the scene's file: its name, the rule, its place, its line; the options, Play, Attract Demo
    const std::string row = s.selected >= 0 && s.selected < n ? s.rows[size_t(s.selected)] : std::string();
    const size_t sp = row.find(' ');
    const std::string sceneName = sp == std::string::npos ? row : row.substr(sp + 1);
    reFile_.wantKey = "s" + std::to_string(s.selected);
    reFile_.slide = 1;
    const int optY = 214;
    reFile_.box[0] = c.rx - 8, reFile_.box[1] = 20, reFile_.box[2] = c.rw + 16, reFile_.box[3] = optY - 24;
    reFile_.wantSig = reFile_.wantKey + "|" + s.line + "|" + std::to_string(bloodHue_);
    reFile_.draw = [this, sceneName, lineIn = s.line, sel = s.selected, n, rx = c.rx, rw = c.rw]() {
        const float W = float(rw), X = float(rx), S = ps_;
        float y = 34.0f, nh = 26.0f;
        if (const float tw = rTextWidth(sceneName, nh); tw > W) nh = std::max(16.0f, nh * W / tw);
        rText(sceneName, X, y, nh, kRowLit, 0, 0.0f, 81, kTx);
        y += nh + 8.0f;
        const uint32_t col = tint(0xB8140C);
        for (int py = int(y * S); py < int((y + 1.2f) * S); ++py)
            for (int px = int(X * S); px < int((X + W) * S); ++px) {
                const float t = (float(px) / S - X) / W;
                rBlend(px, py, col, 0.9f * (1.0f - t) * (1.0f - t) + 0.08f);
            }
        y += 9.0f;
        rText("Cut-scene", X, y, 10.0f, tint(0xD0281C), 0, 0.0f, 82, kTx);
        rText(std::to_string(sel + 1) + " / " + std::to_string(n), X + W, y, 10.0f, kDim, 2, 0.0f, 83, kTx);
        y += 22.0f;
        std::string cur;
        std::vector<std::string> out;
        size_t p = 0;
        const std::string line = T(lineIn);   // (lang: translated, then wrapped; English: as before)
        const RawText raw(this);
        if (lang::active())
            out = lang::wrapBy(line, W, [&](const std::string& t) { return rTextWidthRaw(t, 11.0f); }), p = line.size();
        while (p < line.size()) {   // the line, wrapped
            size_t e = line.find(' ', p);
            const std::string word = line.substr(p, e == std::string::npos ? std::string::npos : e - p);
            p = e == std::string::npos ? line.size() : e + 1;
            const std::string t = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() && rTextWidth(t, 11.0f) > W) out.push_back(cur), cur = word;
            else cur = t;
        }
        if (!cur.empty()) out.push_back(cur);
        lang::noteLines(out);
        for (const std::string& l : out) {
            rText(l, X, y, 11.0f, kBody, 0, 0.0f, 84, kTx);
            y += 16.5f;
        }
    };
    struct Opt { std::string label, value, help; int action; bool on; };
    std::vector<Opt> opts = {
        {"Players", std::to_string(s.players), "1 to 4 players in the game; 3 or 4: the 4P mode. Key: P", 0, true},
        {"Models", s.pc ? "PC" : "Arcade", s.pcThere ? "The arcade's or the 1998 PC release's models. Key: M" : "Only the arcade models are there", 1, s.pcThere},
        {"Clean View", s.clean ? "On" : "Off", "On: only the picture while watching (no HUD, crosshairs or cursor; the keys show for 2 s). Key: C", 4, true},
        {"Subtitles", s.subtitles ? "On" : "Off", "The cut-scenes' captions. Off with Clean View On: only the picture, for recording. Key: T", 5, true},
    };
    // actorswap: Actors, a row per role (Left / Right or a click: its actor; keys 1-4, Shift: back)
    for (size_t r = 0; r < s.castRoles.size() && r < s.castActors.size(); ++r)
        opts.push_back({s.castRoles[r], s.castActors[r], "Who plays this part while you watch (the picture only). Left / Right, a click or keys 1-4", 10 + int(r), true});
    const int optH = s.castRoles.empty() ? 17 : 15;
    // galact (user 2026-10-28: "I think a list could be added here"): an Actors row's click / Enter / A opens a dropdown
    // list of every actor (the settings rows' dropdown state: ddRow_ = the role, on this page); Left / Right still step
    const bool castList = !s.castChoices.empty() && s.castNow.size() == s.castRoles.size();
    int castRow[4][4] = {};
    for (size_t i = 0; i < opts.size(); ++i) {
        Widget* o = rWidget(c.rx, optY + int(i) * optH, c.rw, optH - 1, &Frontend::drawReOpt, opts[i].label);
        o->value = opts[i].value;
        o->help = opts[i].help;
        o->seed = 620 + int(i) * 3;
        o->user = opts[i].on ? 0 : 1;
        const bool actorRow = opts[i].action >= 10 && opts[i].action < 20;
        if (actorRow && castList && opts[i].action - 10 < 4) {
            const int r = opts[i].action - 10;
            castRow[r][0] = o->x, castRow[r][1] = o->y, castRow[r][2] = o->w, castRow[r][3] = o->h;
            o->help = "Who plays this part while you watch (the picture only). Click / Enter: the list; Left / Right, keys 1-4";
            setHandlers(o, [this, r](Widget&) {
                walking_ = false;
                ddRow_ = r, ddKey_ = remakeKey_, ddScroll_ = -1, ddFocus_ = true;
                screenScenes();
            }, &Frontend::hitRect);
        } else if (opts[i].on)
            setHandlers(o, [act, a = opts[i].action](Widget&) { act(a)(); }, &Frontend::hitRect);
        if (actorRow) {   // (Left: the actor before, Right: the next)
            o->left = [act, a = opts[i].action]() { act(a + 10)(); };
            o->right = [act, a = opts[i].action]() { act(a)(); };
        }
    }
    int y = optY + int(opts.size()) * optH + 10;
    Widget* play = rWidget(c.rx, y, c.rw, 24, &Frontend::drawReButton, "Play");
    play->user = 1, play->seed = 640;
    play->help = "Watch it. The game runs ahead to it first, the next time at once. Key: F5";
    setHandlers(play, [act](Widget&) { act(2)(); }, &Frontend::hitRect);
    y += 30;
    Widget* demo = rWidget(c.rx, y, c.rw, 22, &Frontend::drawReButton, "Attract Demo");
    demo->seed = 641;
    demo->help = "The arcade's attract (title, story, demo play) full screen with sound. Any key or button: back";
    setHandlers(demo, [this](Widget&) {
        walking_ = false;
        attractAsked = true;
    }, &Frontend::hitRect);
    y += 30;
    if (!s.status.empty()) {   // (galfix2: wrapped to the column; one long line ran off the screen's right edge)
        std::vector<std::string> lines;
        std::string cur;
        for (size_t p = 0; p < s.status.size();) {
            size_t e = s.status.find(' ', p);
            if (e == std::string::npos) e = s.status.size();
            const std::string word = s.status.substr(p, e - p), t = cur.empty() ? word : cur + " " + word;
            p = e + 1;
            if (!cur.empty() && rTextWidth(t, 10.0f) > float(c.rw)) lines.push_back(cur), cur = word;
            else cur = t;
        }
        if (!cur.empty()) lines.push_back(cur);
        for (size_t k = 0; k < lines.size(); ++k) {
            Widget* st = rWidget(c.rx, y, c.rw, 10, &Frontend::drawRText, lines[k]);
            st->user = int(tint(0xE0402C)), st->id = 0, st->seed = 642 + int(k);
            y += 13;
        }
        y += 3;
    }
    for (const char* l : {"While watching: Space pause, R replay, M slow,", "F free cam, C clean, T subtitles, A actors, Esc back"}) {
        Widget* k = rWidget(c.rx, y, c.rw, 8, &Frontend::drawRText, l);
        k->user = int(kRowOff), k->id = 0;
        y += 12;
    }
    // the controls along the bottom
    reLegend({{"Q/E|LB/RB", "Scene", nullptr},
              {"Tab|LT/RT", "Category", nullptr},
              {"P|X", "Players", act(0)},
              {"M|Y", "Models", act(1)},
              {"C|-", "Clean View", act(4)},
              {"T|-", "Subtitles", act(5)},
              {"1-4|-", "Actors", nullptr},
              {"F5|Start", "Play", act(2)},
              {"Esc|B", "Back", act(3), 1}},
             c.lx, cw_ - c.m);
    // galact: an Actors row's dropdown (over everything): every actor, the role's pick marked; a long list scrolls (Up /
    // Down past its ends, the wheel, its arrows / bar); a pick sets the role's actor; Esc / B or a click outside closes
    // it (Frontend::input / remakeNav: ddOpen). The page's other widgets take no focus while it is open
    rScroll_ = nullptr;
    if (!(ddOpen() && castList && ddRow_ < int(s.castRoles.size()) && ddRow_ < 4))
        return;
    const int role = ddRow_;
    const int nItems = int(s.castChoices.size()), curK = s.castNow[size_t(role)];
    for (Widget* w = root_->child; w; w = w->next)
        w->nav = false;
    const int itemH = 20, m = 5;
    float tw = 0;
    for (const std::string& ch : s.castChoices)
        tw = std::max(tw, rTextWidth(ch, 13.0f));
    const int* rr = castRow[role];
    // (beside the rows, on their left over the scenes' list, centred on the row: under / over the row it met the
    // scene's name above and the Play button below, both drawn over it)
    int vis = std::min(nItems, 10);
    auto heightOf = [&](int v) { return v * itemH + 2 * m + (v < nItems ? 24 : 0); };
    while (vis > 3 && heightOf(vis) > kHeight - 12)
        --vis;
    const bool scroll = vis < nItems;
    const int lh = heightOf(vis), arrowH = scroll ? 12 : 0, sbW = scroll ? 14 : 0;
    const int lw = std::min(cw_ - 12, std::max(150, int(std::ceil(tw)) + 46) + sbW);
    const int lx = std::max(6, rr[0] - lw - 8);
    const int ly = std::clamp(rr[1] + rr[3] / 2 - lh / 2, 6, kHeight - 6 - lh);
    if (ddScroll_ < 0)
        ddScroll_ = curK - vis / 2;
    ddScroll_ = std::clamp(ddScroll_, 0, nItems - vis);
    ddRows_ = nullptr;   // (no settings rows: no icons)
    auto childCount = [this]() { int k = 0; for (Widget* w = root_->child; w; w = w->next) ++k; return k; };
    const int before = childCount();
    ddRect_[0] = lx, ddRect_[1] = ly, ddRect_[2] = lw, ddRect_[3] = lh;
    reListHide_[0] = lx, reListHide_[1] = ly, reListHide_[2] = lw, reListHide_[3] = lh;
    reList_.wantSig += "|list" + std::to_string(ly) + "," + std::to_string(lh);
    Widget* panel = widgetNew();
    addChild(root_, panel);
    setRect(panel, lx, ly, lw, lh);
    panel->draw = &Frontend::drawRDrop;
    panel->flags |= 0x100;
    panel->seed = 760 + role * 7;
    Widget* focus = nullptr;
    for (int v = 0; v < vis; ++v) {
        const int k = ddScroll_ + v;
        Widget* it = widgetNew();
        addChild(root_, it);
        setRect(it, lx + 4, ly + m + arrowH + v * itemH, lw - 8 - sbW, itemH);
        it->text = s.castChoices[size_t(k)];
        it->id = int16_t(k);
        it->user = k == curK ? 1 : 0;
        it->seed = 780 + k;
        it->help = "Who plays " + s.castRoles[size_t(role)] + " while you watch";
        it->draw = &Frontend::drawRDropItem;
        it->flags |= 0x100;
        it->nav = true;
        if (it->user)
            focus = it;
        setHandlers(it, [this, act, role, k, rr0 = rr[0] + rr[2] / 2, rr1 = rr[1] + rr[3] / 2](Widget&) {
            walking_ = false;
            ddRow_ = -1;
            act(-(1000 + 100 * role + k))();   // (the page built again)
            if (!handShown_)
                px_ = rr0, py_ = rr1;   // (the focus back on the row)
        }, &Frontend::hitRect);
    }
    rScroll_ = [this, nItems, vis](int dir) {
        const int s2 = std::clamp(ddScroll_ + dir, 0, nItems - vis);
        if (s2 == ddScroll_)
            return false;
        ddScroll_ = s2;
        screenScenes();
        return true;
    };
    for (int d = 0; d < 2 && scroll; ++d) {   // (more actors above / below)
        if (d == 0 ? ddScroll_ == 0 : ddScroll_ + vis >= nItems)
            continue;
        Widget* a = widgetNew();
        addChild(root_, a);
        setRect(a, lx + (lw - sbW) / 2 - 20, d ? ly + lh - m - arrowH : ly + m, 40, arrowH);
        a->user = d;
        a->draw = &Frontend::drawRArrow;
        a->flags |= 0x100;
        setHandlers(a, [this, d, vis](Widget&) {
            walking_ = false;
            if (rScroll_) {
                const auto f = rScroll_;
                f(d ? vis - 1 : 1 - vis);
            }
        }, &Frontend::hitRect);
    }
    if (scroll)
        scrollBar(root_, lx + lw - sbW - 2, ly + m + arrowH, 7, vis * itemH, ddScroll_, vis, nItems, [this](int first) {
            ddScroll_ = first;
            screenScenes();
        });
    {   // drawn last (over the page): the list's widgets (added newest first at the head) moved to the end, in order
        const int added = childCount() - before;
        std::vector<Widget*> mine;
        for (int k = 0; k < added && root_->child; ++k) {
            Widget* h = root_->child;
            root_->child = h->next;
            h->next = nullptr;
            mine.insert(mine.begin(), h);
        }
        // the page's widgets under the list: hidden and dead while it is open (the scenes' rows are drawn in the live
        // pass, over it, and a click there went to them)
        for (Widget* w = root_->child; w; w = w->next)
            if (w->x < lx + lw && w->x + w->w > lx && w->y < ly + lh && w->y + w->h > ly)
                w->draw = nullptr, w->activate = nullptr, w->nav = false;
        Widget** tail = &root_->child;
        while (*tail)
            tail = &(*tail)->next;
        for (Widget* m2 : mine) {
            *tail = m2;
            tail = &m2->next;
        }
    }
    if (ddFocus_ && !handShown_ && focus)   // (opened by a key: the focus on the role's actor)
        px_ = focus->x + focus->w / 2, py_ = focus->y + focus->h / 2;
    ddFocus_ = false;
}

// menus3: the list's wheel (2 rows a notch; true: it was over the list). Its bar: Frontend::scrollBar (galfix3)
bool Frontend::galleryListWheel(int ux, int uy, int notches)
{
    if (!galleryRE() || screen_ != 17 || reListN_ <= kListRows)
        return false;
    const int* b = reList_.box;
    if (ux < b[0] - 4 || ux >= b[0] + b[2] + 6 || uy < b[1] - 14 || uy >= b[1] + b[3] + 4)
        return false;
    const int top = std::max(0, reListN_ - kListRows);
    const int now = reListScroll_ >= 0 ? reListScroll_ : std::clamp(reListSeen_ % 100000 - kListRows / 2 + 1, 0, top);
    reListScroll_ = std::clamp(now - notches * 2, 0, top);
    return true;
}

}   // namespace hotd::game
